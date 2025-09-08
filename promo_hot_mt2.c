// promo_hot_mt.c — chunk-level parallel page promotion with throttling
// Usage: sudo ./promo_hot_mt <pid> [file] [slow fast]
// Env:
//   NTHREADS          : worker threads (default: online CPUs)
//   BATCH             : max addresses per move_pages() (default: 131072)
//   BATCH_BYTES       : optional bytes-per-syscall cap (default: 0=off)
//   HOT_CHUNK         : hot-list entries per task (default: 65536)
//   SWEEP_CHUNK_PAGES : sweep pages per task (default: BATCH)
//   THROTTLE_MBPS     : limit migration bandwidth in MB/s (default: 0=off)
//   PAUSE_US          : fixed sleep after each processed window in µs (default: 0)
//   HOT_FIRST         : "1" hot first (default), "0" sweep first
//   THP_2M_ONLY       : if set, force 2 MiB stride (skip smaps parsing)

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <numaif.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define META_STATE_LENGTH      (1ULL<<20)   /* 1 MiB  */
#define HOT_PAGE_STATE_LENGTH  (9ULL<<20)   /* 9 MiB  */
#define DEFAULT_BATCH          131072UL

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

static uint64_t now_us(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec*1000000ULL + ts.tv_nsec/1000ULL;
}

/* stride (4 KiB vs 2 MiB) detection from /proc/<pid>/smaps OR force 2 MiB */
static size_t detect_step(pid_t pid, unsigned long addr)
{
    if (getenv("THP_2M_ONLY")) return 2*1024*1024ULL;

    char path[64]; snprintf(path, sizeof path, "/proc/%d/smaps", pid);
    FILE *fp = fopen(path, "r"); if (!fp) return 4096;

    char *line = NULL; size_t n = 0; int in = 0, huge = 0;
    while (getline(&line, &n, fp) != -1) {
        unsigned long lo, hi;
        if (sscanf(line, "%lx-%lx", &lo, &hi) == 2) { in = (lo == addr); continue; }
        if (!in) continue;
        if (!strncmp(line,"ShmemPmdMapped:",16) || !strncmp(line,"ShmemHugePages:",16)) {
            unsigned long v; char *c = strchr(line, ':');
            if (c && sscanf(c, ": %lu",&v)==1 && v) { huge=1; break; }
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
    if(!v){fclose(fp);return -1;}
    char line[512];
    while(fgets(line,sizeof line,fp)){
        if(!strstr(line,needle)) continue;
        unsigned long s,e; if(sscanf(line,"%lx-%lx",&s,&e)!=2) continue;
        if(cnt==cap){cap<<=1; struct vma *tmp=realloc(v,cap*sizeof*v); if(!tmp){free(v); fclose(fp); return -1;} v=tmp;}
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
    if(!list){munmap(m,META_STATE_LENGTH+HOT_PAGE_STATE_LENGTH);return 0;}
    memcpy(list,base+sizeof(uint32_t),(size_t)cnt*sizeof(uint64_t));
    munmap(m,META_STATE_LENGTH+HOT_PAGE_STATE_LENGTH);
    *out=list; return cnt;
}

static int detect_node(pid_t pid,unsigned long addr)
{
    void *p=(void*)addr; int st;
    return move_pages_sys(pid,1,&p,NULL,&st,0)<0?-1:st;
}

/* ---------- batched move_pages query+move on a window of addresses ---------- */
struct batch_bufs {
    void   **q_addrs;
    int    *q_stat;
    void   **m_addrs;
    int    *m_node;
    size_t  cap;
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

static void process_window(pid_t pid, void **base, size_t n,
                           int slow, int fast, int move_flags,
                           struct batch_bufs *buf,
                           size_t *migrated, size_t *remaining)
{
    if (n==0) return;
    if (ensure_batch(buf, n) != 0) { perror("alloc batch"); return; }

    if (move_pages_sys(pid, n, base, NULL, buf->q_stat, 0) < 0) {
        perror("move_pages query");
        return;
    }

    size_t need=0;
    for (size_t i=0;i<n;i++) {
        if (buf->q_stat[i]==slow) {
            buf->m_addrs[need]=base[i];
            buf->m_node [need]=fast;
            need++;
        }
    }
    (*remaining) += need;

    if (need) {
        if (move_pages_sys(pid, need, buf->m_addrs, buf->m_node,
                           buf->q_stat, move_flags) < 0) {
            perror("move_pages migrate");
            return;
        }
        (*migrated) += need;
    }
}

/* ---------------------------- work-queue model ---------------------------- */
enum task_kind { TASK_HOT=1, TASK_SWEEP=2 };

struct task {
    enum task_kind kind;
    int vma_idx;          /* which VMA */
    size_t start;         /* HOT: hot index start; SWEEP: page index start */
    size_t count;         /* HOT: #hot entries;   SWEEP: #pages          */
};

struct shared {
    pid_t pid;
    int slow, fast, move_flags;
    size_t batch;          /* address-count cap per syscall */
    size_t batch_bytes;    /* optional bytes-per-syscall cap */
    size_t throttle_mbps;  /* MB/s limit (0=off) */
    uint64_t pause_us;     /* fixed sleep after each window */

    struct vma *vmas; int nv;
    const uint64_t *hot; size_t nhot;

    struct task *tasks; size_t ntasks;
    _Atomic size_t next_task;
};

struct thread_ctx {
    struct shared *S;
    size_t tidx;
    size_t hot_migrated, sweep_migrated, remaining;
};

static void pin_to_cpu(size_t cpu) {
    cpu_set_t set; CPU_ZERO(&set);
    CPU_SET((int)(cpu % get_online_cpus()), &set);
    (void)sched_setaffinity(0, sizeof(set), &set);
}

/* Per-VMA window cap = min(address-cap, bytes-cap/step) */
static size_t compute_win_cap(const struct shared *S, const struct vma *v)
{
    size_t cap = S->batch ? S->batch : DEFAULT_BATCH;
    if (S->batch_bytes) {
        size_t step = v->step ? v->step : 4096;
        size_t by = S->batch_bytes / step; /* bytes -> addresses */
        if (by >= 1 && by < cap) cap = by;
    }
    return cap;
}

/* Optional pacing: sleep so bytes moved don’t exceed THROTTLE_MBPS */
static void maybe_throttle(const struct shared *S, size_t page_bytes,
                           size_t migrated_this_window, uint64_t elapsed_us)
{
    if (!S->throttle_mbps || migrated_this_window==0) {
        if (S->pause_us) usleep((useconds_t)S->pause_us);
        return;
    }
    const uint64_t bytes = (uint64_t)page_bytes * (uint64_t)migrated_this_window;
    const uint64_t desired_us =
        (bytes * 1000000ULL) / ( (uint64_t)S->throttle_mbps * 1024ULL * 1024ULL );
    if (elapsed_us < desired_us) usleep((useconds_t)(desired_us - elapsed_us));
    if (S->pause_us) usleep((useconds_t)S->pause_us);
}

/* Process one HOT task for VMA v */
static void do_task_hot(struct thread_ctx *tc, const struct task *tk)
{
    struct shared *S = tc->S;
    const struct vma *v = &S->vmas[tk->vma_idx];

    const size_t page_bytes = v->step ? v->step : 4096;
    size_t win_cap = compute_win_cap(S, v);

    struct batch_bufs buf = {0};
    void **win = malloc(win_cap * sizeof(void*));
    if (!win) { perror("alloc hot window"); return; }

    const unsigned long vlen = v->end - v->start;
    size_t migrated=0, remaining=0;

    size_t i = tk->start, end = tk->start + tk->count;
    while (i < end) {
        size_t nwin = end - i;
        if (nwin > win_cap) nwin = win_cap;

        size_t cnt = 0;
        for (size_t k=0; k<nwin; k++) {
            uint64_t off = S->hot[i + k];
            if (off < vlen) win[cnt++] = (void*)(v->start + off);
        }

        const uint64_t t0 = now_us();
        size_t before = migrated;
        if (cnt) process_window(S->pid, win, cnt, S->slow, S->fast,
                                S->move_flags, &buf, &migrated, &remaining);
        const uint64_t t1 = now_us();

        maybe_throttle(S, page_bytes, migrated - before, t1 - t0);
        i += nwin;
    }

    tc->hot_migrated += migrated;
    tc->remaining    += remaining;

    free(buf.q_addrs); free(buf.q_stat);
    free(buf.m_addrs); free(buf.m_node);
    free(win);
}

/* Process one SWEEP task for VMA v (by page index range) */
static void do_task_sweep(struct thread_ctx *tc, const struct task *tk)
{
    struct shared *S = tc->S;
    const struct vma *v = &S->vmas[tk->vma_idx];

    const size_t page_bytes = v->step ? v->step : 4096;
    size_t win_cap = compute_win_cap(S, v);

    struct batch_bufs buf = {0};
    void **win = malloc(win_cap * sizeof(void*));
    if (!win) { perror("alloc sweep window"); return; }

    const size_t step = v->step ? v->step : 4096;
    unsigned long addr = v->start + tk->start * step;
    size_t left = tk->count;

    size_t migrated=0, remaining=0;
    while (left) {
        size_t n = left > win_cap ? win_cap : left;
        for (size_t i=0;i<n;i++) { win[i] = (void*)addr; addr += step; }

        const uint64_t t0 = now_us();
        size_t before = migrated;
        process_window(S->pid, win, n, S->slow, S->fast,
                       S->move_flags, &buf, &migrated, &remaining);
        const uint64_t t1 = now_us();

        maybe_throttle(S, page_bytes, migrated - before, t1 - t0);
        left -= n;
    }

    tc->sweep_migrated += migrated;
    tc->remaining      += remaining;

    free(buf.q_addrs); free(buf.q_stat);
    free(buf.m_addrs); free(buf.m_node);
    free(win);
}

static void *worker(void *arg)
{
    struct thread_ctx *tc = (struct thread_ctx*)arg;
    struct shared *S = tc->S;
    pin_to_cpu(tc->tidx);

    for (;;) {
        size_t idx = atomic_fetch_add(&S->next_task, 1);
        if (idx >= S->ntasks) break;
        struct task *tk = &S->tasks[idx];
        if (tk->kind == TASK_HOT) do_task_hot(tc, tk);
        else                      do_task_sweep(tc, tk);
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

    int move_flags = MPOL_MF_MOVE;
    if (geteuid() == 0) move_flags |= MPOL_MF_MOVE_ALL;

    /* Lower priority so the app wins CPU when needed (still fast with THP) */
    nice(19);

    /* Tunables */
    size_t batch = DEFAULT_BATCH;
    size_t batch_bytes = 0;
    size_t hot_chunk = 65536;
    size_t sweep_chunk_pages; /* default to batch later */
    size_t throttle_mbps = 0;
    uint64_t pause_us = 0;
    int hot_first = 1;

    const char *benv = getenv("BATCH");
    if (benv) { unsigned long b = strtoul(benv, NULL, 10); if (b >= 1024) batch = b; }
    const char *bbenv = getenv("BATCH_BYTES");
    if (bbenv){ unsigned long long bb = strtoull(bbenv, NULL, 10); batch_bytes = (size_t)bb; }
    const char *henv = getenv("HOT_CHUNK");
    if (henv) { unsigned long x = strtoul(henv, NULL, 10); if (x >= 1024) hot_chunk = x; }
    sweep_chunk_pages = batch;
    const char *senv = getenv("SWEEP_CHUNK_PAGES");
    if (senv) { unsigned long x = strtoul(senv, NULL, 10); if (x >= 1024) sweep_chunk_pages = x; }
    const char *tenv = getenv("NTHREADS");
    size_t nthreads = get_online_cpus();
    if (tenv) { unsigned long t = strtoul(tenv, NULL, 10); if (t >= 1) nthreads = t; }
    const char *tmenv = getenv("THROTTLE_MBPS");
    if (tmenv){ unsigned long tm = strtoul(tmenv, NULL, 10); throttle_mbps = tm; }
    const char *penv = getenv("PAUSE_US");
    if (penv) { unsigned long long p = strtoull(penv, NULL, 10); pause_us = (uint64_t)p; }
    const char *hfenv = getenv("HOT_FIRST");
    if (hfenv && !strcmp(hfenv,"0")) hot_first = 0;

    printf("Promoting %d VMA(s) node %d → %d (threads=%zu, batch=%zu, batch_bytes=%zu, hot_chunk=%zu, sweep_chunk_pages=%zu, throttle_mbps=%zu, pause_us=%llu, hot_first=%d)\n",
           nv, slow, fast, nthreads, batch, batch_bytes, hot_chunk, sweep_chunk_pages,
           throttle_mbps, (unsigned long long)pause_us, hot_first);

    size_t iter=0;
    while (1) {
        uint64_t *hot=NULL; size_t nhot=load_hot_list(file,&hot);

        /* Count tasks */
        size_t ntasks = 0;
        if (nhot) ntasks += (size_t)nv * ((nhot + hot_chunk - 1) / hot_chunk);
        for (int i=0;i<nv;i++) {
            const size_t step = v[i].step ? v[i].step : 4096;
            const size_t pages = (v[i].end - v[i].start) / step;
            ntasks += (pages + sweep_chunk_pages - 1) / sweep_chunk_pages;
        }

        struct task *tasks = malloc(ntasks * sizeof(*tasks));
        if(!tasks){ if(hot) free(hot); free(v); return 1; }
        size_t tcur = 0;

        if (!hot_first) { /* SWEEP then HOT */
            for (int i=0;i<nv;i++) {
                const size_t step = v[i].step ? v[i].step : 4096;
                const size_t pages = (v[i].end - v[i].start) / step;
                for (size_t p=0; p<pages; p+=sweep_chunk_pages) {
                    size_t cnt = (pages - p > sweep_chunk_pages) ? sweep_chunk_pages : (pages - p);
                    tasks[tcur++] = (struct task){ .kind=TASK_SWEEP, .vma_idx=i, .start=p, .count=cnt };
                }
            }
            if (nhot) {
                for (int i=0;i<nv;i++) {
                    for (size_t s=0; s<nhot; s+=hot_chunk) {
                        size_t cnt = (nhot - s > hot_chunk) ? hot_chunk : (nhot - s);
                        tasks[tcur++] = (struct task){ .kind=TASK_HOT, .vma_idx=i, .start=s, .count=cnt };
                    }
                }
            }
        } else { /* HOT then SWEEP */
            if (nhot) {
                for (int i=0;i<nv;i++) {
                    for (size_t s=0; s<nhot; s+=hot_chunk) {
                        size_t cnt = (nhot - s > hot_chunk) ? hot_chunk : (nhot - s);
                        tasks[tcur++] = (struct task){ .kind=TASK_HOT, .vma_idx=i, .start=s, .count=cnt };
                    }
                }
            }
            for (int i=0;i<nv;i++) {
                const size_t step = v[i].step ? v[i].step : 4096;
                const size_t pages = (v[i].end - v[i].start) / step;
                for (size_t p=0; p<pages; p+=sweep_chunk_pages) {
                    size_t cnt = (pages - p > sweep_chunk_pages) ? sweep_chunk_pages : (pages - p);
                    tasks[tcur++] = (struct task){ .kind=TASK_SWEEP, .vma_idx=i, .start=p, .count=cnt };
                }
            }
        }

        struct shared S = {
            .pid=pid, .slow=slow, .fast=fast, .move_flags=move_flags,
            .batch=batch, .batch_bytes=batch_bytes,
            .throttle_mbps=throttle_mbps, .pause_us=pause_us,
            .vmas=v, .nv=nv, .hot=hot, .nhot=nhot,
            .tasks=tasks, .ntasks=ntasks, .next_task=0
        };

        pthread_t *ths = malloc(nthreads * sizeof(*ths));
        struct thread_ctx *ctx = calloc(nthreads, sizeof(*ctx));
        for (size_t t=0;t<nthreads;t++) {
            ctx[t].S = &S; ctx[t].tidx = t;
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
        free(tasks);
        if (hot) free(hot);

        printf("[iter %zu] hot_list=%zu  hot_migrated=%zu  sweep_migrated=%zu  remaining=%zu  (tasks=%zu)\n",
               ++iter, nhot, hot_mig, sweep_mig, remaining, ntasks);
        fflush(stdout);

        if ((sweep_mig + hot_mig) == 0) break;
        if (!remaining) break;

        usleep(2000); /* 20 ms between iterations */
    }

    puts("Done.");
    free(v);
    return 0;
}

