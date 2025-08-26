// promote.c — parallel, batched page promotion via move_pages()
// Drop-in replacement for your current file.
//
// Build: gcc -O3 -pthread -Wall -Wextra -o promote promote.c
// Usage: sudo ./promote <pid> [file] [slow fast]
// Tunables (env):
//   NTHREADS: number of worker threads (default: min(num_vmas, online_cpus))
//   BATCH   : max addresses per move_pages() batch (default: 131072)

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <numaif.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#define META_STATE_LENGTH      (1ULL<<20)   /* 1 MiB  */
#define HOT_PAGE_STATE_LENGTH  (9ULL<<20)   /* 9 MiB  */
#define DEFAULT_BATCH          131072UL     /* addresses per syscall batch */

#ifndef __NR_move_pages
#define __NR_move_pages 279
#endif
static long move_pages_sys(int pid, unsigned long cnt, void **pages,
                           const int *nodes, int *status, int flags)
{
    return syscall(__NR_move_pages, pid, cnt, pages, nodes, status, flags);
}

struct vma { unsigned long start, end; size_t step; };

static size_t get_online_cpus(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (size_t)n : 1;
}

/* stride (4 KiB vs 2 MiB) detection from /proc/<pid>/smaps */
static size_t detect_step(pid_t pid, unsigned long addr)
{
    char path[64]; snprintf(path, sizeof path, "/proc/%d/smaps", pid);
    FILE *fp = fopen(path, "r"); if (!fp) return 4096;

    char *line = NULL; size_t n = 0; int in = 0, huge = 0;
    while (getline(&line, &n, fp) != -1) {
        unsigned long lo, hi;
        if (sscanf(line, "%lx-%lx", &lo, &hi) == 2) {
            in = (lo == addr);
            continue;
        }
        if (!in) continue;
        if (!strncmp(line,"ShmemPmdMapped:",16) || !strncmp(line,"ShmemHugePages:",16)) {
            unsigned long v;
            if (sscanf(strchr(line,':'),": %lu",&v)==1 && v) { huge=1; break; }
        }
        if (line[0]=='\n') break;
    }
    free(line); fclose(fp);
    return huge ? (2*1024*1024ULL) : 4096ULL;
}

/* gather VMAs mapping the shm file */
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

/* hot-list loader: returns count, sets *out to malloc'd array of byte offsets */
static size_t load_hot_list(const char *file, uint64_t **out)
{
    int fd=open(file,O_RDONLY); if(fd<0){perror("open hot");return 0;}
    void *m=mmap(NULL,META_STATE_LENGTH+HOT_PAGE_STATE_LENGTH,
                 PROT_READ,MAP_SHARED,fd,0); close(fd);
    if(m==MAP_FAILED){perror("mmap hot");return 0;}
    uint8_t *base=(uint8_t*)m+META_STATE_LENGTH;
    uint32_t cnt=*(uint32_t*)base;
    if(!cnt){munmap(m,META_STATE_LENGTH+HOT_PAGE_STATE_LENGTH);return 0;}
    uint64_t *list=malloc((size_t)cnt*sizeof(uint64_t));
    memcpy(list,base+sizeof(uint32_t),(size_t)cnt*sizeof(uint64_t));
    munmap(m,META_STATE_LENGTH+HOT_PAGE_STATE_LENGTH);
    *out=list; return cnt;
}

/* sort & unique helpers for hot address dedup */
static int cmp_ptr(const void *a, const void *b) {
    uintptr_t aa=(uintptr_t)*(void * const *)a;
    uintptr_t bb=(uintptr_t)*(void * const *)b;
    if (aa<bb) return -1; if (aa>bb) return 1; return 0;
}
static size_t uniq_ptrs(void **a, size_t n) {
    if (n==0) return 0;
    size_t w=1;
    for (size_t i=1;i<n;i++){
        if (a[i]!=a[w-1]) a[w++]=a[i];
    }
    return w;
}

static int detect_node(pid_t pid,unsigned long addr)
{
    void *p=(void*)addr; int st;
    return move_pages_sys(pid,1,&p,NULL,&st,0)<0?-1:st;
}

/* ---------- batched move_pages query+move on a window of addresses ---------- */
struct batch_bufs {
    void   **q_addrs;   /* query addrs      */
    int    *q_stat;     /* query statuses   */
    void   **m_addrs;   /* to-migrate addrs */
    int    *m_node;     /* dst nodes (fast) */
    size_t  cap;        /* capacity per buffer */
};

static int ensure_batch(struct batch_bufs *b, size_t need)
{
    if (b->cap >= need) return 0;
    size_t newcap = need;
    void **q = realloc(b->q_addrs, newcap*sizeof(void*));
    int  *qs = realloc(b->q_stat , newcap*sizeof(int));
    void **m = realloc(b->m_addrs, newcap*sizeof(void*));
    int  *mn = realloc(b->m_node , newcap*sizeof(int));
    if (!q || !qs || !m || !mn) return -1;
    b->q_addrs=q; b->q_stat=qs; b->m_addrs=m; b->m_node=mn; b->cap=newcap;
    return 0;
}

/* Process a contiguous subarray [base, base+n): query, filter slow->fast, move. */
static void process_window(pid_t pid, void **base, size_t n,
                           int slow, int fast, int move_flags,
                           struct batch_bufs *buf,
                           size_t *migrated, size_t *remaining)
{
    if (n==0) return;
    if (ensure_batch(buf, n) != 0) { perror("alloc batch"); return; }

    /* Query */
    if (move_pages_sys(pid, n, base, NULL, buf->q_stat, 0) < 0) {
        perror("move_pages query");
        return;
    }

    /* Filter slow pages */
    size_t need=0;
    for (size_t i=0;i<n;i++) {
        if (buf->q_stat[i]==slow) {
            buf->m_addrs[need]=base[i];
            buf->m_node [need]=fast;
            need++;
        }
    }
    (*remaining) += need; /* pages still on slow before move */

    if (need) {
        if (move_pages_sys(pid, need, buf->m_addrs, buf->m_node,
                           buf->q_stat, move_flags) < 0) {
            perror("move_pages migrate");
            return;
        }
        (*migrated) += need;
    }
}

/* ------------------------------- threading ------------------------------- */
struct thread_ctx {
    pid_t pid;
    int slow, fast;
    int move_flags;
    size_t batch;        /* max addrs per syscall */
    const uint64_t *hot; /* shared hot offsets list */
    size_t nhot;

    struct vma *vmas;
    int idx_begin, idx_end; /* [begin, end) */

    /* per-thread stats */
    size_t hot_migrated;
    size_t sweep_migrated;
    size_t remaining;
};

static void pin_to_cpu(size_t cpu) {
    cpu_set_t set; CPU_ZERO(&set);
    CPU_SET((int)(cpu % get_online_cpus()), &set);
    sched_setaffinity(0, sizeof(set), &set);
}

static void migrate_hot_for_vma(struct thread_ctx *tc, struct vma *v)
{
    if (!tc->nhot) return;

    struct batch_bufs buf = {0};
    size_t migrated = 0, remaining = 0;

    const unsigned long vlen = v->end - v->start;
    void **win = malloc(tc->batch * sizeof(void*));
    if (!win) { perror("alloc hot window"); return; }

    size_t i = 0;
    while (i < tc->nhot) {
        size_t nwin = tc->nhot - i;
        if (nwin > tc->batch) nwin = tc->batch;

        /* build the window directly from the hot list (already deduped/sorted upstream) */
        size_t cnt = 0;
        for (size_t k = 0; k < nwin; ++k) {
            uint64_t off = tc->hot[i + k];
            if (off < vlen) win[cnt++] = (void*)(v->start + off);
        }

        if (cnt) {
            process_window(tc->pid, win, cnt, tc->slow, tc->fast,
                           tc->move_flags, &buf, &migrated, &remaining);
        }
        i += nwin;
    }

    tc->hot_migrated += migrated;
    tc->remaining    += remaining;

    free(buf.q_addrs); free(buf.q_stat);
    free(buf.m_addrs); free(buf.m_node);
    free(win);
}

static void sweep_vma(struct thread_ctx *tc, struct vma *v)
{
    struct batch_bufs buf = {0};
    size_t migrated=0, remaining=0;

    const size_t step = v->step ? v->step : 4096;
    const size_t total_pages = (v->end - v->start) / step;

    /* Allocate a rolling window pointer view; we will *reference* it, no copy */
    void **window = malloc(tc->batch * sizeof(void*));
    if (!window) { perror("alloc window"); return; }

    size_t produced = 0; /* produced into 'window' */
    unsigned long addr = v->start;

    for (size_t done=0; done<total_pages; ) {
        /* Fill window */
        produced = 0;
        while (produced < tc->batch && done < total_pages) {
            window[produced++] = (void*)addr;
            addr += step;
            done++;
        }
        /* Process this window */
        process_window(tc->pid, window, produced, tc->slow, tc->fast,
                       tc->move_flags, &buf, &migrated, &remaining);
    }

    tc->sweep_migrated += migrated;
    tc->remaining      += remaining;

    free(buf.q_addrs); free(buf.q_stat);
    free(buf.m_addrs); free(buf.m_node);
    free(window);
}

static void *worker(void *arg)
{
    struct thread_ctx *tc = (struct thread_ctx*)arg;
    /* Optional: light CPU pinning to spread workers */
    pin_to_cpu((size_t)(tc - ((struct thread_ctx*)0))); /* cheap unique idx */

    for (int i = tc->idx_begin; i < tc->idx_end; i++) {
        migrate_hot_for_vma(tc, &tc->vmas[i]);
    }
    for (int i = tc->idx_begin; i < tc->idx_end; i++) {
        sweep_vma(tc, &tc->vmas[i]);
    }
    return NULL;
}

/* ------------------------------- main ---------------------------------- */
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

    /* flags: use MOVE_ALL when possible (root), otherwise best-effort */
    int move_flags = MPOL_MF_MOVE;
    if (geteuid() == 0) move_flags |= MPOL_MF_MOVE_ALL;

    /* tunables */
    size_t batch = DEFAULT_BATCH;
    const char *benv = getenv("BATCH");
    if (benv) {
        unsigned long b = strtoul(benv, NULL, 10);
        if (b >= 1024) batch = b; /* guard silly small values */
    }
    size_t nthreads = (size_t)nv < get_online_cpus() ? (size_t)nv : get_online_cpus();
    const char *tenv = getenv("NTHREADS");
    if (tenv) {
        unsigned long t = strtoul(tenv, NULL, 10);
        if (t >= 1) nthreads = t;
    }
    if (nthreads < 1) nthreads = 1;

    printf("Promoting %d VMA(s) node %d → %d (batch=%zu, threads=%zu)\n",
           nv, slow, fast, batch, nthreads);

    size_t iter=0;
    while (1) {
        uint64_t *hot=NULL; size_t nhot=load_hot_list(file,&hot);

        /* spawn threads, partition VMAs evenly */
        pthread_t *ths = malloc(nthreads * sizeof(*ths));
        struct thread_ctx *ctx = calloc(nthreads, sizeof(*ctx));

        int per = nv / (int)nthreads, rem = nv % (int)nthreads;
        int cur = 0;
        for (size_t t=0;t<nthreads;t++) {
            int take = per + (rem>0 ? 1 : 0); if (rem>0) rem--;
            ctx[t].pid=pid; ctx[t].slow=slow; ctx[t].fast=fast; ctx[t].move_flags=move_flags;
            ctx[t].batch=batch; ctx[t].hot=hot; ctx[t].nhot=nhot;
            ctx[t].vmas=v; ctx[t].idx_begin=cur; ctx[t].idx_end=cur+take;
            cur += take;
            pthread_create(&ths[t], NULL, worker, &ctx[t]);
        }

        size_t hot_mig=0, sweep_mig=0, remaining=0;
        for (size_t t=0;t<nthreads;t++) {
            pthread_join(ths[t], NULL);
            hot_mig   += ctx[t].hot_migrated;
            sweep_mig += ctx[t].sweep_migrated;
            remaining += ctx[t].remaining;
        }

        free(ths); free(ctx);
        if (hot) free(hot);

        printf("[iter %zu] hot_list=%zu  hot_migrated=%zu  sweep_migrated=%zu  remaining=%zu\n",
               ++iter, nhot, hot_mig, sweep_mig, remaining);
        fflush(stdout);

        if ((sweep_mig + hot_mig) == 0) break;
        if (!remaining) break;

        /* small backoff to let the system settle / producer update hot list */
        usleep(2000); /* 20 ms */
    }

    puts("Done.");
    free(v);
    return 0;
}

