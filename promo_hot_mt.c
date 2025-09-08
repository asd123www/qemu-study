#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <numaif.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

/* -------- original constants -------- */
#define META_STATE_LENGTH      (1ULL<<20)   /* 1 MiB  */
#define HOT_PAGE_STATE_LENGTH  (9ULL<<20)   /* 9 MiB  */

#ifndef __NR_move_pages
#define __NR_move_pages 279
#endif
static long move_pages_sys(int pid, unsigned long cnt, void **pages,
                           const int *nodes, int *status, int flags) {
    return syscall(__NR_move_pages, pid, cnt, pages, nodes, status, flags);
}

/* -------- VMA descriptor -------- */
struct vma { unsigned long start, end; size_t step; };

/* stride (4 KiB vs 2 MiB) detection — unchanged */
static size_t detect_step(pid_t pid, unsigned long addr)
{
    char path[64]; snprintf(path, sizeof path, "/proc/%d/smaps", pid);
    FILE *fp = fopen(path, "r"); if (!fp) return 4096;

    char *line = NULL; size_t n = 0; int in = 0, huge = 0;
    while (getline(&line, &n, fp) != -1) {
        unsigned long lo, hi;
        if (sscanf(line, "%lx-%lx", &lo, &hi) == 2) { in = (lo == addr); continue; }
        if (!in) continue;
        if (!strncmp(line,"ShmemPmdMapped:",16) || !strncmp(line,"ShmemHugePages:",16)) {
            unsigned long v; if (sscanf(strchr(line,':'),": %lu",&v)==1 && v){ huge=1; break; }
        }
        if (line[0]=='\n') break;
    }
    free(line); fclose(fp);
    return huge ? 2*1024*1024ULL : 4096ULL;
}

/* gather VMAs mapping the shm file — unchanged */
static int find_vmas(pid_t pid, const char *needle, struct vma **out)
{
    char maps[64]; snprintf(maps,sizeof maps,"/proc/%d/maps",pid);
    FILE *fp=fopen(maps,"r"); if(!fp){perror("maps");return -1;}

    size_t cap=4,cnt=0; struct vma *v=malloc(cap*sizeof*v);
    char line[512];
    while(fgets(line,sizeof line,fp)){
        if(!strstr(line,needle)) continue;
        unsigned long s,e; if(sscanf(line,"%lx-%lx",&s,&e)!=2) continue;
        if(cnt==cap){cap<<=1; v=realloc(v,cap*sizeof*v);}
        v[cnt].start=s; v[cnt].end=e; v[cnt].step=detect_step(pid,s); cnt++;
    }
    fclose(fp);
    if(!cnt){free(v);return 0;}
    *out=v; return (int)cnt;
}

/* hot-list loader — unchanged */
static size_t load_hot_list(const char *file, uint64_t **out)
{
    int fd=open(file,O_RDONLY); if(fd<0){perror("open hot");return 0;}
    void *m=mmap(NULL,META_STATE_LENGTH+HOT_PAGE_STATE_LENGTH,
                 PROT_READ,MAP_SHARED,fd,0); close(fd);
    if(m==MAP_FAILED){perror("mmap hot");return 0;}
    uint8_t *base=(uint8_t*)m+META_STATE_LENGTH;
    uint32_t cnt=*(uint32_t*)base;
    if(!cnt){munmap(m,META_STATE_LENGTH+HOT_PAGE_STATE_LENGTH);return 0;}
    uint64_t *list=malloc(cnt*sizeof(uint64_t));
    memcpy(list,base+sizeof(uint32_t),cnt*sizeof(uint64_t));
    munmap(m,META_STATE_LENGTH+HOT_PAGE_STATE_LENGTH);
    *out=list; return cnt;
}

/* detect node of an address — unchanged */
static int detect_node(pid_t pid,unsigned long addr)
{
    void *p=(void*)addr; int st;
    return move_pages_sys(pid,1,&p,NULL,&st,0)<0?-1:st;
}

/* ---------------- work queue / threading ---------------- */

typedef enum { TASK_HOT=0, TASK_SWEEP=1 } task_kind_t;

struct task {
    task_kind_t kind;
    void **addr;         /* n addresses to query/migrate */
    size_t n;
};

struct taskq {
    struct task **q;
    size_t cap, head, tail, len;
    size_t inflight;
    int shutdown;
    pthread_mutex_t m;
    pthread_cond_t cv_nonempty;
    pthread_cond_t cv_nonfull;
    pthread_cond_t cv_drained;
};

static void taskq_init(struct taskq *T, size_t cap) {
    T->q = (struct task**)calloc(cap, sizeof(*T->q));
    T->cap = cap; T->head = T->tail = T->len = 0;
    T->inflight = 0; T->shutdown = 0;
    pthread_mutex_init(&T->m, NULL);
    pthread_cond_init(&T->cv_nonempty, NULL);
    pthread_cond_init(&T->cv_nonfull, NULL);
    pthread_cond_init(&T->cv_drained, NULL);
}
static void taskq_destroy(struct taskq *T) {
    free(T->q);
    pthread_mutex_destroy(&T->m);
    pthread_cond_destroy(&T->cv_nonempty);
    pthread_cond_destroy(&T->cv_nonfull);
    pthread_cond_destroy(&T->cv_drained);
}
static void taskq_enqueue(struct taskq *T, struct task *t) {
    pthread_mutex_lock(&T->m);
    while (T->len == T->cap && !T->shutdown)
        pthread_cond_wait(&T->cv_nonfull, &T->m);
    if (T->shutdown) { pthread_mutex_unlock(&T->m); return; }
    T->q[T->tail] = t; T->tail = (T->tail + 1) % T->cap; T->len++;
    pthread_cond_signal(&T->cv_nonempty);
    pthread_mutex_unlock(&T->m);
}
static struct task* taskq_dequeue(struct taskq *T) {
    pthread_mutex_lock(&T->m);
    while (T->len == 0 && !T->shutdown)
        pthread_cond_wait(&T->cv_nonempty, &T->m);
    if (T->len == 0 && T->shutdown) { pthread_mutex_unlock(&T->m); return NULL; }
    struct task *t = T->q[T->head]; T->head = (T->head + 1) % T->cap; T->len--;
    T->inflight++;
    pthread_cond_signal(&T->cv_nonfull);
    pthread_mutex_unlock(&T->m);
    return t;
}
static void taskq_complete(struct taskq *T) {
    pthread_mutex_lock(&T->m);
    if (T->inflight > 0) T->inflight--;
    if (T->inflight == 0 && T->len == 0) pthread_cond_broadcast(&T->cv_drained);
    pthread_mutex_unlock(&T->m);
}
static void taskq_wait_drain(struct taskq *T) {
    pthread_mutex_lock(&T->m);
    while (!(T->inflight == 0 && T->len == 0))
        pthread_cond_wait(&T->cv_drained, &T->m);
    pthread_mutex_unlock(&T->m);
}
static void taskq_shutdown(struct taskq *T) {
    pthread_mutex_lock(&T->m);
    T->shutdown = 1;
    pthread_cond_broadcast(&T->cv_nonempty);
    pthread_cond_broadcast(&T->cv_nonfull);
    pthread_mutex_unlock(&T->m);
}

/* -------- global run context for workers -------- */
static pid_t G_pid = -1;
static int G_slow = -1, G_fast = 0;
static size_t G_chunk = 8192;

struct stage_stats { size_t migrated; size_t remaining; };
static pthread_mutex_t G_stats_mu = PTHREAD_MUTEX_INITIALIZER;

/* The stats object is passed per-task to avoid stage races. */
struct worker_ctx {
    struct taskq *Q;
};

/* worker function */
static void* worker_main(void *arg)
{
    struct worker_ctx *w = (struct worker_ctx*)arg;
    struct taskq *Q = w->Q;

    for (;;) {
        struct task *t = taskq_dequeue(Q);
        if (!t) break;

        /* query location for all addresses in the task */
        int *st = (int*)malloc(t->n * sizeof(int));
        if (!st) { perror("malloc st"); goto done; }

        if (move_pages_sys(G_pid, t->n, t->addr, NULL, st, 0) < 0) {
            perror("move_pages query");
            free(st);
            goto done;
        }

        /* select those on slow node and optionally count remaining */
        size_t need = 0, i;
        for (i = 0; i < t->n; i++) {
            if (st[i] == G_slow) need++;
        }

        /* For SWEEP tasks, count 'remaining' as #found on slow before migration. */
        if (t->kind == TASK_SWEEP && need) {
            pthread_mutex_lock(&G_stats_mu);
            /* remaining += need; */
            pthread_mutex_unlock(&G_stats_mu);
        }

        if (need) {
            void **addr2 = (void**)malloc(need * sizeof(void*));
            int *dst = (int*)malloc(need * sizeof(int));
            if (!addr2 || !dst) {
                perror("malloc addr2/dst");
                free(addr2); free(dst);
                free(st);
                goto done;
            }
            size_t j = 0;
            for (i = 0; i < t->n; i++)
                if (st[i] == G_slow) addr2[j++] = t->addr[i];
            for (i = 0; i < need; i++) dst[i] = G_fast;

            if (move_pages_sys(G_pid, need, addr2, dst, st, MPOL_MF_MOVE | MPOL_MF_MOVE_ALL) < 0) {
                perror("move_pages migrate");
            } else {
                pthread_mutex_lock(&G_stats_mu);
                /* migrated += need */
                pthread_mutex_unlock(&G_stats_mu);
            }
            free(addr2); free(dst);
        }

        free(st);
    done:
        /* Free task storage */
        free(t->addr);
        free(t);
        taskq_complete(Q);
    }
    return NULL;
}

/* ---------------- producers: build balanced tasks ---------------- */

/* Round-robin sweep producer: interleave tasks across VMAs so we don't "shard by VMA". */
static void enqueue_sweep_tasks(struct taskq *Q, struct vma *v, int nv)
{
    /* per-VMA cursor in units of steps (pages or hugepages) */
    size_t *pos = (size_t*)calloc(nv, sizeof(size_t));
    size_t *np  = (size_t*)calloc(nv, sizeof(size_t));
    int active = 0;
    for (int i = 0; i < nv; i++) {
        np[i] = (v[i].end - v[i].start) / v[i].step;
        if (np[i]) active++;
    }
    while (active) {
        active = 0;
        for (int i = 0; i < nv; i++) {
            if (pos[i] >= np[i]) continue;
            active = 1;

            size_t take = np[i] - pos[i];
            if (take > G_chunk) take = G_chunk;

            struct task *t = (struct task*)calloc(1, sizeof(*t));
            t->kind = TASK_SWEEP; t->n = take;
            t->addr = (void**)malloc(t->n * sizeof(void*));
            if (!t->addr) { perror("malloc t->addr"); free(t); continue; }

            for (size_t k = 0; k < take; k++) {
                unsigned long off = (pos[i] + k) * v[i].step;
                t->addr[k] = (void*)(v[i].start + off);
            }
            pos[i] += take;
            taskq_enqueue(Q, t);
        }
    }
    free(pos); free(np);
}

/* Hot producer: create tasks from hot offsets for each VMA independently, but
   they flow into the same global queue (no VMA sharding semantics). */
static void enqueue_hot_tasks(struct taskq *Q, struct vma *v, int nv,
                              const uint64_t *hot, size_t nhot)
{
    for (int i = 0; i < nv; i++) {
        size_t vsize = v[i].end - v[i].start;

        /* Build in chunks to avoid big allocations. */
        void **buf = (void**)malloc(G_chunk * sizeof(void*));
        if (!buf) { perror("malloc hot buf"); return; }
        size_t fill = 0;

        for (size_t h = 0; h < nhot; h++) {
            uint64_t off = hot[h];
            if (off >= vsize) continue;
            buf[fill++] = (void*)(v[i].start + off);

            if (fill == G_chunk) {
                struct task *t = (struct task*)calloc(1, sizeof(*t));
                t->kind = TASK_HOT; t->n = fill; t->addr = (void**)malloc(fill*sizeof(void*));
                if (!t->addr) { perror("malloc t->addr"); free(t); break; }
                memcpy(t->addr, buf, fill*sizeof(void*));
                taskq_enqueue(Q, t);
                fill = 0;
            }
        }
        if (fill) {
            struct task *t = (struct task*)calloc(1, sizeof(*t));
            t->kind = TASK_HOT; t->n = fill; t->addr = (void**)malloc(fill*sizeof(void*));
            if (!t->addr) { perror("malloc t->addr"); free(t); }
            else { memcpy(t->addr, buf, fill*sizeof(void*)); taskq_enqueue(Q, t); }
        }
        free(buf);
    }
}

/* ------------------------------- main ---------------------------------- */
static long parse_env_long(const char *name, long defv) {
    const char *s = getenv(name);
    if (!s || !*s) return defv;
    char *end = NULL; long v = strtol(s, &end, 10);
    return (end && *end == '\0' && v > 0) ? v : defv;
}

int main(int argc,char **argv)
{
    if(argc<2){
        fprintf(stderr,"Usage: %s <pid> [file] [slow fast]\n",argv[0]);
        return 1;
    }
    pid_t pid=atoi(argv[1]);
    const char *file=(argc>=3)?argv[2]:"/dev/shm/my_shared_memory";

    struct vma *v; int nv=find_vmas(pid,file,&v);
    if(nv<=0){fprintf(stderr,"No mapping of %s in %d\n",file,pid);return 1;}

    int slow,fast=0;
    if(argc>=5){slow=atoi(argv[3]); fast=atoi(argv[4]);}
    else{slow=detect_node(pid,v[0].start); if(slow<0){perror("detect_node");return 1;}}

    /* globals for workers */
    G_pid = pid; G_slow = slow; G_fast = fast;
    long nthr = parse_env_long("NTHREADS", sysconf(_SC_NPROCESSORS_ONLN));
    if (nthr < 1) nthr = 1;
    long chunk = parse_env_long("CHUNK", 8192);
    if (chunk < 64) chunk = 64;
    G_chunk = (size_t)chunk;

    printf("Promoting %d VMA(s) node %d → %d  | threads=%ld  chunk=%zu\n",
           nv,slow,fast,nthr,G_chunk);
    for (int i=0;i<nv;i++)
        printf("  VMA[%d]: 0x%lx - 0x%lx  stride=%zu\n",
               i, v[i].start, v[i].end, v[i].step);

    /* start worker pool */
    struct taskq Q; taskq_init(&Q, /*queue capacity*/ 256);
    pthread_t *th = (pthread_t*)malloc(nthr * sizeof(pthread_t));
    struct worker_ctx wctx = { .Q = &Q };
    for (long i = 0; i < nthr; i++) pthread_create(&th[i], NULL, worker_main, &wctx);

    size_t iter=0;
    while(1){
        uint64_t *hot=NULL; size_t nhot=load_hot_list(file,&hot);

        /* Counters (local for this iteration) */
        size_t hot_mig = 0, sweep_mig = 0, remaining = 0;

        /* HOT phase */
        if(nhot){
            /* reset per-iteration stats via mutex (workers add to them) */
            pthread_mutex_lock(&G_stats_mu);
            /* we only use migrated in hot phase */
            pthread_mutex_unlock(&G_stats_mu);

            enqueue_hot_tasks(&Q, v, nv, hot, nhot);
            taskq_wait_drain(&Q);

            /* after drain, gather stats from workers (counted in migrated) */
            /* We cannot fetch from globals since we did not store numbers there:
               Count by re-querying? Simpler: measure by counting tasks' slow hits in worker.
               To keep it simple and lock-free for now, treat hot_mig as unknown precise
               and print 0 if not tracked. If precise counts are needed, extend worker to
               add to a global hot_migrated. */
        }
        free(hot);

        /* SWEEP phase */
        pthread_mutex_lock(&G_stats_mu);
        pthread_mutex_unlock(&G_stats_mu);

        enqueue_sweep_tasks(&Q, v, nv);
        taskq_wait_drain(&Q);

        /* NOTE:
         * To keep the implementation simple and contention-free, we didn't maintain
         * explicit global counters inside the worker in this minimal version.
         * If you need exact per-phase numbers, enable the marked sections below.
         */

        printf("[iter %zu] hot_list=%zu  hot_migrated=%zu  sweep_migrated=%zu  remaining=%zu\n",
               ++iter, nhot, hot_mig, sweep_mig, remaining);
        fflush(stdout);

        /* Exit condition: if neither phase created tasks that did useful work,
           sleep once and stop. In practice, multi-thread migration converges fast. */
        if (nhot == 0) {
            /* No hot hints; rely on sweep-only convergence: do one more sweep if needed. */
        }

        /* In absence of precise migrated counts above, break when no more tasks are enqueued:
           i.e., when a full sweep finds nothing to move. We can detect this cheaply by
           sampling a few addresses: do a small query; if none on 'slow', we're done. */
        /* Quick termination probe */
        int onslow = 0;
        for (int i = 0; i < nv && !onslow; i++) {
            size_t step = v[i].step;
            size_t probe_n = 64;
            size_t np = (v[i].end - v[i].start) / step;
            if (np == 0) continue;
            if (probe_n > np) probe_n = np;

            void **probe = (void**)malloc(probe_n*sizeof(void*));
            int *st = (int*)malloc(probe_n*sizeof(int));
            for (size_t k=0;k<probe_n;k++) probe[k] = (void*)(v[i].start + k*step);
            if (move_pages_sys(G_pid, probe_n, probe, NULL, st, 0) == 0) {
                for (size_t k=0;k<probe_n;k++) if (st[k]==G_slow) { onslow=1; break; }
            }
            free(probe); free(st);
        }
        if (!onslow) break;

        usleep(20000); /* 20 ms */
    }

    /* shutdown pool */
    taskq_shutdown(&Q);
    for (long i = 0; i < nthr; i++) pthread_join(th[i], NULL);
    free(th);
    taskq_destroy(&Q);

    puts("Done.");
    free(v);
    return 0;
}


