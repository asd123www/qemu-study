#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <numaif.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#define META_STATE_LENGTH      (1ULL << 20)   /* 1 MiB */
#define HOT_PAGE_STATE_LENGTH  (9ULL << 20)   /* 9 MiB */
#define PAGE_BYTES             4096           /* 4 KiB */
#define WINDOW_US              20000          /* 20 ms */

#ifndef __NR_move_pages
#define __NR_move_pages 279
#endif
long move_pages(int pid, unsigned long cnt, void **pages,
                       const int *nodes, int *status, int flags)
{
    return syscall(__NR_move_pages, pid, cnt, pages, nodes, status, flags);
}

/* ───────── VMA descriptor with sweep cursor ───────── */
struct vma {
    unsigned long start, end;
    size_t        step;        /* 4 KiB or 2 MiB */
    size_t        cursor;      /* how many “units” already swept */
};

#define UNIT_PAGES(v) ((v)->step / PAGE_BYTES)

/* ───────── detect 4 KiB vs 2 MiB stride ──────────── */
static size_t detect_step(pid_t pid, unsigned long addr)
{
    char p[64]; snprintf(p, sizeof p, "/proc/%d/smaps", pid);
    FILE *fp = fopen(p, "r"); if (!fp) return PAGE_BYTES;

    char *line = NULL; size_t n = 0; int in = 0, huge = 0;
    while (getline(&line, &n, fp) != -1) {
        unsigned long lo, hi;
        if (sscanf(line, "%lx-%lx", &lo, &hi) == 2) { in = (lo == addr); continue; }
        if (!in) continue;
        if (!strncmp(line, "ShmemPmdMapped:", 16) ||
            !strncmp(line, "ShmemHugePages:", 16)) {
            unsigned long v; if (sscanf(strchr(line, ':'), ": %lu", &v) == 1 && v)
                { huge = 1; break; }
        }
        if (line[0] == '\n') break;
    }
    free(line); fclose(fp);
    return huge ? 2 * 1024 * 1024ULL : PAGE_BYTES;
}

/* ───────── collect VMAs that map the shm file ─────── */
static int find_vmas(pid_t pid, const char *needle, struct vma **out)
{
    char m[64]; snprintf(m, sizeof m, "/proc/%d/maps", pid);
    FILE *fp = fopen(m, "r"); if (!fp) { perror("maps"); return -1; }

    size_t cap = 4, cnt = 0; struct vma *v = malloc(cap * sizeof *v);
    char line[512];
    while (fgets(line, sizeof line, fp)) {
        if (!strstr(line, needle)) continue;
        unsigned long s, e; if (sscanf(line, "%lx-%lx", &s, &e) != 2) continue;
        if (cnt == cap) { cap <<= 1; v = realloc(v, cap * sizeof *v); }
        v[cnt].start  = s;
        v[cnt].end    = e;
        v[cnt].step   = detect_step(pid, s);
        v[cnt].cursor = 0;
        cnt++;
    }
    fclose(fp);
    if (!cnt) { free(v); return 0; }
    *out = v; return (int)cnt;
}

/* ───────── load hot-page offsets from QEMU ────────── */
static size_t load_hot_list(const char *file, uint64_t **out)
{
    int fd = open(file, O_RDONLY); if (fd < 0) return 0;
    void *m = mmap(NULL, META_STATE_LENGTH + HOT_PAGE_STATE_LENGTH,
                   PROT_READ, MAP_SHARED, fd, 0); close(fd);
    if (m == MAP_FAILED) return 0;

    uint8_t  *base = (uint8_t *)m + META_STATE_LENGTH;
    uint32_t  cnt  = *(uint32_t *)base;
    if (!cnt) { munmap(m, META_STATE_LENGTH + HOT_PAGE_STATE_LENGTH); return 0; }

    uint64_t *l = malloc(cnt * sizeof(uint64_t));
    memcpy(l, base + sizeof(uint32_t), cnt * sizeof(uint64_t));
    munmap(m, META_STATE_LENGTH + HOT_PAGE_STATE_LENGTH);
    *out = l; return cnt;
}

/* ───────── migrate a batch of “units” (4 KiB or THP) ─ */
static size_t migrate_units(pid_t pid, void **addr, size_t units,
                            int slow, int fast,
                            size_t unit_pages, size_t *budget_pg)
{
    if (!units || !(*budget_pg)) return 0;
    size_t max_units = *budget_pg / unit_pages;
    if (!max_units) return 0;
    if (units > max_units) units = max_units;

    int *st = malloc(units * sizeof(int));
    if (move_pages(pid, units, addr, NULL, st, 0) < 0) {
        perror("move_pages query"); free(st); return 0;
    }
    size_t need = 0;
    for (size_t i = 0; i < units; i++) if (st[i] == slow) addr[need++] = addr[i];

    size_t moved_pg = 0;
    if (need) {
        int *dst = malloc(need * sizeof(int));
        for (size_t i = 0; i < need; i++) dst[i] = fast;
        if (move_pages(pid, need, addr, dst, st,
                       MPOL_MF_MOVE | MPOL_MF_MOVE_ALL) < 0)
            perror("move_pages migrate");
        free(dst);
        moved_pg      = need * unit_pages;
        *budget_pg   -= moved_pg;
    }
    free(st);
    return moved_pg;
}

/* ───────── migrate hot pages ──────────────────────── */
static size_t migrate_hot(pid_t pid, struct vma *v,
                          const uint64_t *hot, size_t nhot,
                          int slow, int fast, size_t *budget_pg)
{
    if (!nhot) return 0;

    size_t step       = v->step;
    size_t unit_pages = UNIT_PAGES(v);

    /* collect & dedup addresses */
    void **addr = malloc(nhot * sizeof(void *));
    size_t units = 0;

    if (step == PAGE_BYTES) {                   /* 4 KiB */
        for (size_t i = 0; i < nhot; i++)
            addr[units++] = (void *)(v->start + hot[i]);
    } else {                                    /* 2 MiB huge pages */
        uint64_t *bases = malloc(nhot * sizeof(uint64_t));
        for (size_t i = 0; i < nhot; i++)
            bases[i] = (hot[i] / step) * step;
        /* sort & uniq */
        qsort(bases, nhot, sizeof(uint64_t),
              (int(*)(const void*,const void*))strcmp);
        for (size_t i = 0; i < nhot; i++)
            if (i == 0 || bases[i] != bases[i-1])
                addr[units++] = (void *)(v->start + bases[i]);
        free(bases);
    }

    size_t moved = migrate_units(pid, addr, units,
                                 slow, fast, unit_pages, budget_pg);
    free(addr);
    return moved;
}

/* ───── sweep cold pages (cursor, budget-aware, no skipping) ─────────── */
static size_t sweep(pid_t pid, struct vma *v,
                    int slow, int fast, size_t *budget_pg)
{
    if (!(*budget_pg)) return 0;

    const size_t step        = v->step;                 /* 4 KiB or 2 MiB */
    const size_t unit_pages  = UNIT_PAGES(v);
    const size_t total_units = (v->end - v->start) / step;

    size_t moved_pg = 0;

    /* keep taking small slices until the window budget is exhausted
     * or the VMA is finished.  slice = min(128 units, remaining units)   */
    while (*budget_pg && v->cursor < total_units) {
        size_t avail_units = total_units - v->cursor;
        size_t slice_units = avail_units < 128 ? avail_units : 128;

        /* shrink slice if it won’t fit in remaining budget,
         * but never drop below 1 unit so we always make progress        */
        size_t max_units = (*budget_pg / unit_pages);
        if (!max_units) max_units = 1;
        if (slice_units > max_units) slice_units = max_units;

        void **addr = malloc(slice_units * sizeof(void *));
        for (size_t i = 0; i < slice_units; i++)
            addr[i] = (void *)(v->start + (v->cursor + i) * step);

        /* migrate_units() debits *budget_pg only for pages that really move */
        moved_pg        += migrate_units(pid, addr, slice_units,
                                         slow, fast, unit_pages, budget_pg);
        v->cursor       += slice_units;       /* always advance past slice   */
        free(addr);
    }
    return moved_pg;
}


/* ───────── detect NUMA node of first page ─────────── */
static int detect_node(pid_t pid, unsigned long addr)
{
    void *p = (void *)addr; int st;
    return move_pages(pid, 1, &p, NULL, &st, 0) < 0 ? -1 : st;
}

/* ====================================================================== */
int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr,
            "Usage: %s <pid> [file] [slow fast] [rate_MBps]\n", argv[0]);
        return 1;
    }

    pid_t pid        = atoi(argv[1]);
    const char *file = (argc >= 3) ? argv[2] : "/dev/shm/my_shared_memory";

    struct vma *vmas; int nv = find_vmas(pid, file, &vmas);
    if (nv <= 0) { fprintf(stderr, "No mapping of shm file\n"); return 1; }

    int slow, fast = 0;
    int rate_idx = 3;
    if (argc >= 5) { slow = atoi(argv[3]); fast = atoi(argv[4]); rate_idx = 5; }
    else { slow = detect_node(pid, vmas[0].start); if (slow < 0) { perror("detect_node"); return 1; } }

    size_t pages_per_win = SIZE_MAX;
    if (argc >= rate_idx + 1) {
        double mbps = atof(argv[rate_idx]);
        double bytes = mbps * 1024 * 1024 * (WINDOW_US / 1e6);
        pages_per_win = (size_t)(bytes / PAGE_BYTES);
        if (!pages_per_win) pages_per_win = 1;
        printf("Rate limit: %.2f MB/s ≈ %zu pages/20 ms\n", mbps, pages_per_win);
    } else puts("Rate limit: unlimited");

    printf("Promoting %d VMA(s) node %d → %d\n", nv, slow, fast);

    size_t iter = 0;
    while (1) {
        size_t budget_pg = (iter == 0) ? SIZE_MAX : pages_per_win;

        uint64_t *hot = NULL; size_t nhot = load_hot_list(file, &hot);

        size_t hot_pg = 0, cold_pg = 0;
        if (nhot) {
            for (int i = 0; i < nv && budget_pg; i++)
                hot_pg += migrate_hot(pid, &vmas[i],
                                      hot, nhot, slow, fast, &budget_pg);
            free(hot);
        }

        if (iter > 0 && budget_pg) {
            for (int i = 0; i < nv && budget_pg; i++)
                cold_pg += sweep(pid, &vmas[i], slow, fast, &budget_pg);
        }

        double moved_MB = (hot_pg + cold_pg) * PAGE_BYTES / (1024.0 * 1024.0);
//        printf("[Iter %zu] Hot=%zu pg  Cold=%zu pg  Data=%.2f MB  Budget used=%zu/%s\n",
//               iter, hot_pg, cold_pg, moved_MB,
//               (iter == 0 ? hot_pg + cold_pg : pages_per_win - budget_pg),
//               (iter == 0 ? "∞" : argv[rate_idx]));
//        fflush(stdout);

        /* exit when every VMA cursor reached its end and no new hot pages */
        int done = 1;
        for (int i = 0; i < nv; i++) {
            size_t total_units = (vmas[i].end - vmas[i].start) / vmas[i].step;
            if (vmas[i].cursor < total_units) { done = 0; break; }
        }
        if((hot_pg + cold_pg) == 0)
            break;
        if (done && nhot == 0) break;

        usleep(WINDOW_US);
        iter++;
    }

    puts("Migration complete.");
    free(vmas);
    return 0;
}

