/* promo_hot_and_remained.c - Migrates hot pages first, then all remaining pages.
 * Build: gcc -O3 -march=native -pthread promo_hot_and_remained.c -o promo_hot_and_remained
 */
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
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <sched.h>
#include <stdatomic.h> // For atomic operations

#ifndef MPOL_MF_MOVE
#define MPOL_MF_MOVE      (1<<1)
#endif
#ifndef MPOL_MF_MOVE_ALL
#define MPOL_MF_MOVE_ALL  (1<<2)
#endif

#define NTHREADS              16
#define REM_NTHREADS          1
#define MP_BATCH              1024      /* Optimal batch size */
#define META_STATE_LENGTH     (1ULL<<20)    
#define HOT_PAGE_STATE_LENGTH (9ULL<<20)    
#define TWO_MIB               (1ULL<<21)
#define FOUR_KIB              (1ULL<<12)
#define RAM_FILE_BASE         (META_STATE_LENGTH + HOT_PAGE_STATE_LENGTH)

/* move_pages syscall */
#ifndef __NR_move_pages
#define __NR_move_pages 279
#endif

static long move_pages_sys(int pid, unsigned long cnt, void **pages,
                           const int *nodes, int *status, int flags)
{
    return syscall(__NR_move_pages, pid, cnt, pages, nodes, status, flags);
}

/* HOT map structures */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t epoch;
    uint32_t cap;
    uint32_t entry_bytes;
    uint32_t n_items;
    uint32_t pad;
} HotHdr;

typedef struct {
    uint64_t key_off_2m;    
    uint32_t last_epoch;
    uint16_t freq;          
    uint16_t pad;
} HotEntry;

/* VMA info */
typedef struct {
    unsigned long start, end;
    uint64_t pgoff_bytes;
} VMA;

/* Bitmap to track migrated pages */
typedef struct {
    _Atomic uint64_t *bits;
    size_t size_in_bits;
} VmaBitmap;

static void bitmap_init(VmaBitmap *b, size_t num_pages) {
    b->size_in_bits = num_pages;
    size_t size_in_bytes = ((num_pages + 63) / 64) * sizeof(uint64_t);
    b->bits = calloc(1, size_in_bytes);
}

static void bitmap_set(VmaBitmap *b, size_t page_idx) {
    if (page_idx >= b->size_in_bits) return;
    size_t idx = page_idx / 64;
    uint64_t mask = 1ULL << (page_idx % 64);
    atomic_fetch_or_explicit(&b->bits[idx], mask, memory_order_relaxed);
}

static int bitmap_is_set(const VmaBitmap *b, size_t page_idx) {
    if (page_idx >= b->size_in_bits) return 0;
    size_t idx = page_idx / 64;
    uint64_t mask = 1ULL << (page_idx % 64);
    return (atomic_load_explicit(&b->bits[idx], memory_order_relaxed) & mask) != 0;
}

static void bitmap_free(VmaBitmap *b) {
    free(b->bits);
}


/* Hash for dedup */
static inline uint64_t hash64(uint64_t x) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    return x;
}

/* Thread-local dedup set */
typedef struct {
    uint64_t *keys;
    size_t cap;
    size_t mask;
} DedupSet;

static void dedup_init(DedupSet *d, size_t size) {
    d->cap = 1;
    while (d->cap < size * 2) d->cap <<= 1;
    d->mask = d->cap - 1;
    d->keys = calloc(d->cap, sizeof(uint64_t));
}

static int dedup_add(DedupSet *d, uint64_t key) {
    size_t idx = hash64(key) & d->mask;
    for (size_t i = 0; i < 32; i++) {
        if (d->keys[idx] == 0) {
            d->keys[idx] = key;
            return 0;
        }
        if (d->keys[idx] == key) return 1;
        idx = (idx + 1) & d->mask;
    }
    return 0;
}

static void dedup_free(DedupSet *d) {
    free(d->keys);
}

/* Find VMAs */
static int find_vmas(pid_t pid, const char *needle, VMA **out) {
    char maps_path[128];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);
    FILE *fp = fopen(maps_path, "r");
    if (!fp) return -1;
    
    size_t cap = 8, cnt = 0;
    VMA *vmas = malloc(cap * sizeof(VMA));
    char line[512];
    
    while (fgets(line, sizeof(line), fp)) {
        if (!strstr(line, needle)) continue;
        
        unsigned long start, end;
        unsigned long long pgoff_pages;
        
        if (sscanf(line, "%lx-%lx %*s %llx", &start, &end, &pgoff_pages) < 3)
            continue;
        
        if (cnt == cap) {
            cap <<= 1;
            vmas = realloc(vmas, cap * sizeof(VMA));
        }
        
        vmas[cnt].start = start;
        vmas[cnt].end = end;
        vmas[cnt].pgoff_bytes = pgoff_pages * 4096ULL;
        cnt++;
    }
    
    fclose(fp);
    *out = vmas;
    return cnt;
}

/* Load and sort hot windows */
static size_t load_hot_windows(const char *file, uint64_t **out) {
    *out = NULL;
    const size_t map_len = META_STATE_LENGTH + HOT_PAGE_STATE_LENGTH;
    
    int fd = open(file, O_RDONLY);
    if (fd < 0) return 0;
    
    void *m = mmap(NULL, map_len, PROT_READ, MAP_SHARED | MAP_POPULATE, fd, 0);
    close(fd);
    if (m == MAP_FAILED) return 0;
    
    uint8_t *hot_base = (uint8_t *)m + META_STATE_LENGTH;
    const HotHdr *hdr = (const HotHdr *)hot_base;
    
    if (hdr->magic != 0x484F5431u || hdr->version != 1) {
        munmap(m, map_len);
        return 0;
    }
    
    const HotEntry *tab = (const HotEntry *)(hot_base + sizeof(HotHdr));
    
    /* Count and extract hot entries */
    size_t cnt = 0;
    for (size_t i = 0; i < hdr->cap; i++) {
        if (tab[i].freq > 0) cnt++;
    }
    
    if (cnt == 0) {
        munmap(m, map_len);
        return 0;
    }
    
    /* Sort by recency (newest first) */
    typedef struct { uint64_t off; uint32_t age; uint16_t freq; } Item;
    Item *items = malloc(cnt * sizeof(Item));
    size_t j = 0;
    
    for (size_t i = 0; i < hdr->cap && j < cnt; i++) {
        if (tab[i].freq > 0) {
            items[j].off = tab[i].key_off_2m;
            items[j].age = hdr->epoch - tab[i].last_epoch;
            items[j].freq = tab[i].freq;
            j++;
        }
    }
    
    /* Sort by age (newest first), then by frequency */
    for (size_t i = 0; i < cnt - 1; i++) {
        for (size_t k = i + 1; k < cnt; k++) {
            if (items[k].age < items[i].age ||
                (items[k].age == items[i].age && items[k].freq > items[i].freq)) {
                Item tmp = items[i];
                items[i] = items[k];
                items[k] = tmp;
            }
        }
    }
    
    uint64_t *list = malloc(cnt * sizeof(uint64_t));
    for (size_t i = 0; i < cnt; i++) {
        list[i] = items[i].off;
    }
    
    free(items);
    munmap(m, map_len);
    *out = list;
    return cnt;
}

/* Fast batch migration */
static size_t migrate_batch_fast(pid_t pid, void **addrs, size_t n,
                                 int src_node, int dst_node) {
    if (n == 0) return 0;
    
    int *status = malloc(n * sizeof(int));
    int *dst_nodes = malloc(n * sizeof(int));
    
    if (!status || !dst_nodes) {
        free(status);
        free(dst_nodes);
        return 0;
    }
    
    /* Set all destination nodes */
    for (size_t i = 0; i < n; i++) {
        dst_nodes[i] = dst_node;
    }
    
    /* Migrate pages */
    long ret = move_pages_sys(pid, n, addrs, dst_nodes, status,
                              MPOL_MF_MOVE | MPOL_MF_MOVE_ALL);
    
    size_t moved = 0;
    if (ret >= 0) {
        /* Count successful moves - pages that ended up on dst_node */
        for (size_t i = 0; i < n; i++) {
            if (status[i] == dst_node) {
                moved++;
            }
        }
    }
    
    free(status);
    free(dst_nodes);
    return moved;
}

/* Worker thread for HOT pages */
typedef struct {
    pid_t pid;
    const VMA *vmas;
    VmaBitmap *bitmaps; // Pointer to array of bitmaps
    int nvmas;
    const uint64_t *hot_windows;
    size_t start_idx, end_idx;
    int src_node, dst_node;
    DedupSet *dedup;
    size_t pages_moved;
    size_t pages_attempted;
    int tid;
} Worker;

static void* worker_thread(void *arg) {
    Worker *w = (Worker*)arg;
    
    /* Set CPU affinity for better cache usage */
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(w->tid % sysconf(_SC_NPROCESSORS_ONLN), &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
    
    /* Process hot windows */
    void **batch = malloc(MP_BATCH * sizeof(void*));
    size_t batch_size = 0;
    
    for (size_t i = w->start_idx; i < w->end_idx; i++) {
        uint64_t hot_file_off = w->hot_windows[i];
        
        /* Check dedup */
        if (dedup_add(w->dedup, hot_file_off))
            continue;
        
        /* VMA[2] mapping (pgoff=0xa00000000) */
        if (hot_file_off >= RAM_FILE_BASE) {
            uint64_t ram_offset = hot_file_off - RAM_FILE_BASE;
            
            for (int v = 0; v < w->nvmas; v++) {
                if (w->vmas[v].pgoff_bytes == 0xa00000000ULL && ram_offset < (w->vmas[v].end - w->vmas[v].start)) {
                    unsigned long vaddr_start = w->vmas[v].start + ram_offset;
                    size_t pages_to_add = TWO_MIB / FOUR_KIB;
                    if (vaddr_start + TWO_MIB > w->vmas[v].end) {
                        pages_to_add = (w->vmas[v].end - vaddr_start) / FOUR_KIB;
                    }
                    
                    for (size_t p = 0; p < pages_to_add; p++) {
                        unsigned long vaddr = vaddr_start + p * FOUR_KIB;
                        batch[batch_size++] = (void*)vaddr;
                        w->pages_attempted++;
                        bitmap_set(&w->bitmaps[v], (vaddr - w->vmas[v].start) / FOUR_KIB);
                        if (batch_size == MP_BATCH) {
                             w->pages_moved += migrate_batch_fast(w->pid, batch, batch_size, w->src_node, w->dst_node);
                             batch_size = 0;
                        }
                    }
                    break; 
                }
            }
        }
        
        /* VMA[0] direct mapping (pgoff=0) */
        if (hot_file_off < 0x540000000ULL) { /* 21GB limit of VMA[0] */
            for (int v = 0; v < w->nvmas; v++) {
                if (w->vmas[v].pgoff_bytes == 0 && hot_file_off < (w->vmas[v].end - w->vmas[v].start)) {
                    unsigned long vaddr_start = w->vmas[v].start + hot_file_off;
                    
                    size_t pages_to_add = TWO_MIB / FOUR_KIB;
                    if (vaddr_start + TWO_MIB > w->vmas[v].end) {
                        pages_to_add = (w->vmas[v].end - vaddr_start) / FOUR_KIB;
                    }
                    
                    for (size_t p = 0; p < pages_to_add; p++) {
                        unsigned long vaddr = vaddr_start + p * FOUR_KIB;
                        batch[batch_size++] = (void*)vaddr;
                        w->pages_attempted++;
                        bitmap_set(&w->bitmaps[v], (vaddr - w->vmas[v].start) / FOUR_KIB);
                        if (batch_size == MP_BATCH) {
                             w->pages_moved += migrate_batch_fast(w->pid, batch, batch_size, w->src_node, w->dst_node);
                             batch_size = 0;
                        }
                    }
                    break;
                }
            }
        }
        
        /* Process batch when full */
        if (batch_size >= MP_BATCH) {
            w->pages_moved += migrate_batch_fast(w->pid, batch, batch_size, w->src_node, w->dst_node);
            batch_size = 0;
        }
    }
    
    // Process any remaining items in the batch
    if (batch_size > 0) {
        w->pages_moved += migrate_batch_fast(w->pid, batch, batch_size, w->src_node, w->dst_node);
    }
    
    free(batch);
    return NULL;
}

/* Worker thread for REMAINED pages */
typedef struct {
    pid_t pid;
    const VMA *vma;
    const VmaBitmap *bitmap;
    size_t start_page_idx;
    size_t end_page_idx;
    int src_node, dst_node;
    size_t pages_moved;
    size_t pages_attempted;
    int tid;
} RemainderWorker;

static void* remainder_worker_thread(void *arg) {
    RemainderWorker *w = (RemainderWorker*)arg;

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(w->tid % sysconf(_SC_NPROCESSORS_ONLN), &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);

    void **batch = malloc(MP_BATCH * sizeof(void*));
    size_t batch_size = 0;

    for (size_t p_idx = w->start_page_idx; p_idx < w->end_page_idx; p_idx++) {
        if (!bitmap_is_set(w->bitmap, p_idx)) {
            unsigned long vaddr = w->vma->start + p_idx * FOUR_KIB;
            batch[batch_size++] = (void*)vaddr;
            w->pages_attempted++;
            
            if (batch_size >= MP_BATCH) {
                w->pages_moved += migrate_batch_fast(w->pid, batch, batch_size, w->src_node, w->dst_node);
                batch_size = 0;
            }
        }
    }

    if (batch_size > 0) {
        w->pages_moved += migrate_batch_fast(w->pid, batch, batch_size, w->src_node, w->dst_node);
    }

    free(batch);
    return NULL;
}


int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <pid> <shm_file> [src dst]\n", argv[0]);
        return 1;
    }
    
    pid_t pid = atoi(argv[1]);
    const char *file = argv[2];
    int src_node = (argc >= 4) ? atoi(argv[3]) : 2;
    int dst_node = (argc >= 5) ? atoi(argv[4]) : 0;
    
    printf("=== Hot & Remained Data Promotion ===\n");
    printf("PID: %d\n", pid);
    printf("SHM: %s\n", file);
    printf("Migration: node %d → node %d\n\n", src_node, dst_node);
    
    /* Find VMAs */
    VMA *vmas;
    int nvmas = find_vmas(pid, file, &vmas);
    if (nvmas <= 0) {
        fprintf(stderr, "No VMAs found\n");
        return 1;
    }
    
    printf("Found %d VMA(s):\n", nvmas);
    for (int i = 0; i < nvmas; i++) {
        printf("  [%d] %016lx-%016lx (%.1f GB) pgoff=0x%lx\n",
               i, vmas[i].start, vmas[i].end,
               (vmas[i].end - vmas[i].start) / (1024.0 * 1024.0 * 1024.0),
               vmas[i].pgoff_bytes);
    }

    /* Initialize bitmaps for each VMA */
    VmaBitmap *bitmaps = malloc(nvmas * sizeof(VmaBitmap));
    for (int i = 0; i < nvmas; i++) {
        size_t num_pages = (vmas[i].end - vmas[i].start) / FOUR_KIB;
        bitmap_init(&bitmaps[i], num_pages);
    }
    
    /* Load hot windows */
    uint64_t *hot_windows;
    size_t n_hot = load_hot_windows(file, &hot_windows);
    
    if (n_hot > 0) {
        printf("\n--- Phase 1: Migrating %zu Hot Windows (%.1f GB potential) ---\n",
               n_hot, (double)(n_hot * TWO_MIB) / (1024.0 * 1024.0 * 1024.0));
        
        DedupSet *dedup_sets = malloc(NTHREADS * sizeof(DedupSet));
        for (int i = 0; i < NTHREADS; i++) {
            dedup_init(&dedup_sets[i], (n_hot / NTHREADS) + 100);
        }
        
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);
        
        pthread_t *threads = malloc(NTHREADS * sizeof(pthread_t));
        Worker *workers = malloc(NTHREADS * sizeof(Worker));
        
        size_t per_thread = (n_hot + NTHREADS - 1) / NTHREADS;
        
        for (int i = 0; i < NTHREADS; i++) {
            size_t start_idx = i * per_thread;
            size_t end_idx = start_idx + per_thread;
            if (end_idx > n_hot) end_idx = n_hot;
            
            workers[i] = (Worker){
                .pid = pid, .vmas = vmas, .bitmaps = bitmaps, .nvmas = nvmas,
                .hot_windows = hot_windows, .start_idx = start_idx, .end_idx = end_idx,
                .src_node = src_node, .dst_node = dst_node, .dedup = &dedup_sets[i],
                .pages_moved = 0, .pages_attempted = 0, .tid = i
            };
            pthread_create(&threads[i], NULL, worker_thread, &workers[i]);
        }
        
        size_t total_moved = 0, total_attempted = 0;
        for (int i = 0; i < NTHREADS; i++) {
            pthread_join(threads[i], NULL);
            total_moved += workers[i].pages_moved;
            total_attempted += workers[i].pages_attempted;
        }
        
        clock_gettime(CLOCK_MONOTONIC, &end);
        double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
        
        printf("Hot Phase Time: %.3f seconds\n", elapsed);
        printf("Hot Pages Moved: %zu (%.2f GB)\n", total_moved, total_moved * 4096.0 / (1e9));
        if (elapsed > 0) {
            printf("Hot Bandwidth: %.2f GB/s\n", (total_moved * 4096.0 / (1e9)) / elapsed);
        }
        printf("Hot Success Rate: %.1f%%\n", total_attempted > 0 ? (100.0 * total_moved / total_attempted) : 0);
        
        for (int i = 0; i < NTHREADS; i++) dedup_free(&dedup_sets[i]);
        free(dedup_sets);
        free(hot_windows);
        free(threads);
        free(workers);
    } else {
        printf("\nNo hot windows found. Skipping Phase 1.\n");
    }

    /* --- Phase 2: Migrate Remained Pages --- */
    printf("\n--- Phase 2: Migrating Remained Pages ---\n");
    
    struct timespec start_rem, end_rem;
    clock_gettime(CLOCK_MONOTONIC, &start_rem);
    

    pthread_t *rem_threads = malloc(REM_NTHREADS * sizeof(pthread_t));
    RemainderWorker *rem_workers = malloc(REM_NTHREADS * sizeof(RemainderWorker));
    size_t total_rem_moved = 0, total_rem_attempted = 0;

    for (int i = 0; i < nvmas; i++) {
        size_t total_pages_in_vma = (vmas[i].end - vmas[i].start) / FOUR_KIB;
        printf("Scanning VMA[%d] with %zu pages...\n", i, total_pages_in_vma);

        size_t pages_per_thread = (total_pages_in_vma + REM_NTHREADS - 1) / REM_NTHREADS;
        if (pages_per_thread == 0) continue;

        for (int t = 0; t < REM_NTHREADS; t++) {
            size_t start_p_idx = t * pages_per_thread;
            size_t end_p_idx = start_p_idx + pages_per_thread;
            if (start_p_idx >= total_pages_in_vma) continue;
            if (end_p_idx > total_pages_in_vma) end_p_idx = total_pages_in_vma;

            rem_workers[t] = (RemainderWorker) {
                .pid = pid, .vma = &vmas[i], .bitmap = &bitmaps[i],
                .start_page_idx = start_p_idx, .end_page_idx = end_p_idx,
                .src_node = src_node, .dst_node = dst_node,
                .pages_moved = 0, .pages_attempted = 0, .tid = t
            };
            pthread_create(&rem_threads[t], NULL, remainder_worker_thread, &rem_workers[t]);
        }

        for (int t = 0; t < REM_NTHREADS; t++) {
            size_t start_p_idx = t * pages_per_thread;
            if (start_p_idx >= total_pages_in_vma) continue;
            pthread_join(rem_threads[t], NULL);
            total_rem_moved += rem_workers[t].pages_moved;
            total_rem_attempted += rem_workers[t].pages_attempted;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end_rem);
    double elapsed_rem = (end_rem.tv_sec - start_rem.tv_sec) + (end_rem.tv_nsec - start_rem.tv_nsec) / 1e9;

    printf("\n=== Final Results ===\n");
    printf("Remained Phase Time: %.3f seconds\n", elapsed_rem);
    printf("Remained Pages Moved: %zu (%.2f GB)\n", total_rem_moved, total_rem_moved * 4096.0 / (1e9));
    if (elapsed_rem > 0) {
        printf("Remained Bandwidth: %.2f GB/s\n", (total_rem_moved * 4096.0 / (1e9)) / elapsed_rem);
    }
    printf("Remained Success Rate: %.1f%%\n", total_rem_attempted > 0 ? (100.0 * total_rem_moved / total_rem_attempted) : 0);

    /* Cleanup */
    for (int i = 0; i < nvmas; i++) {
        bitmap_free(&bitmaps[i]);
    }
    free(bitmaps);
    free(rem_threads);
    free(rem_workers);
    free(vmas);
    
    return 0;
}

