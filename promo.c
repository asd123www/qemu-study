/*
 * promo.c – hot‑page promoter that handles tmpfs + 2 MB huge pages
 *
 * The helper drains a tmpfs‑backed checkpoint (e.g. /dev/shm/my_shared_memory)
 * from a slow NUMA node (CXL / remote) into local DRAM.  If the mapping already
 * contains PMD‑mapped shmem THP (2 MB pages) the code walks in 2 MB strides so
 * each move_pages() call migrates a whole huge page. Otherwise it walks in
 * 4 kB steps.
 *
 * BUILD
 *   # glibc ships the move_pages wrapper
 *   gcc -O2 -Wall -lnuma -o promo promo.c
 *
 *   # older glibc: header declares move_pages() but lib is missing the symbol
 *   gcc -O2 -Wall -lnuma -DMOVE_PAGES_SYSCALL -o promo promo.c
 *
 * RUN
 *   sudo ./promo <pid> [file=/dev/shm/my_shared_memory] [slow fast]
 *       pid   – target process (e.g. QEMU PID)
 *       file  – file path to look for inside /proc/PID/maps (default above)
 *       slow  – NUMA node to migrate *from*  (auto‑detected if omitted)
 *       fast  – NUMA node to migrate *to*    (default 0)
 *
 * Requires root or CAP_SYS_NICE to migrate pages of another process.
 * Works on Linux ≥ 4.14 (tested on 5.19 + 6.9).
 */

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

/* ---------------- raw‑syscall wrapper if glibc/libnuma lacks move_pages */
#ifdef MOVE_PAGES_SYSCALL
#ifndef __NR_move_pages
#define __NR_move_pages 279               /* x86‑64 & arm64 */
#endif
long move_pages(int pid, unsigned long count, void **pages,
               const int *nodes, int *status, int flags) {
    return syscall(__NR_move_pages, pid, count, pages, nodes, status, flags);
}
#endif

struct vma { unsigned long start, end; size_t step; };

/* Detect whether this tmpfs VMA already has PMD‑mapped shmem THP.
 * We look for ShmemPmdMapped: or ShmemHugePages: > 0 in the smaps stanza. */
static size_t detect_step(pid_t pid, unsigned long addr) {
    char path[64]; snprintf(path, sizeof path, "/proc/%d/smaps", pid);
    FILE *fp = fopen(path, "r"); if (!fp) return 4096;

    char *line = NULL; size_t n = 0; int in_target = 0, huge = 0;
    while (getline(&line, &n, fp) != -1) {
        unsigned long lo, hi;
        if (sscanf(line, "%lx-%lx", &lo, &hi) == 2) { /* new VMA header */
            in_target = (lo == addr);
            continue;
        }
        if (!in_target) continue;

        if (!strncmp(line, "ShmemPmdMapped:", 16) ||
            !strncmp(line, "ShmemHugePages:", 16)) {
            unsigned long val;
            if (sscanf(strchr(line, ':'), ": %lu", &val) == 1 && val) {
                huge = 1; break;            /* ≥1 huge page present */
            }
        }
        if (line[0] == '\n') break;        /* blank line = next VMA */
    }
    free(line); fclose(fp);
    return huge ? 2*1024*1024ULL : 4096ULL;
}

/* Collect every VMA that maps the given file path */
static int find_vmas(pid_t pid, const char *needle, struct vma **out) {
    char maps[64]; snprintf(maps, sizeof maps, "/proc/%d/maps", pid);
    FILE *fp = fopen(maps, "r"); if (!fp) { perror("maps"); return -1; }

    size_t cap = 4, cnt = 0; struct vma *arr = malloc(cap * sizeof *arr);
    char line[512];
    while (fgets(line, sizeof line, fp)) {
        if (!strstr(line, needle)) continue;
        unsigned long s, e; if (sscanf(line, "%lx-%lx", &s, &e) != 2) continue;
        if (cnt == cap) { cap <<= 1; arr = realloc(arr, cap * sizeof *arr); }
        arr[cnt].start = s; arr[cnt].end = e; arr[cnt].step = detect_step(pid, s);
        cnt++;
    }
    fclose(fp);
    if (!cnt) { free(arr); return 0; }
    *out = arr; return (int)cnt;
}

static void migrate_vma(pid_t pid, struct vma *v, int slow, int fast,
                        size_t *remaining) {
    size_t np = (v->end - v->start) / v->step;
    void **addr  = malloc(np * sizeof(void*));
    int  *status = malloc(np * sizeof(int));

    for (size_t i = 0; i < np; i++) addr[i] = (void*)(v->start + i * v->step);

    if (move_pages(pid, np, addr, NULL, status, 0) < 0) {
        perror("move_pages query");
        goto out;
    }

    size_t need = 0;
    for (size_t i = 0; i < np; i++) {
        if (status[i] == slow) {
            addr[need++] = addr[i];
            (*remaining)++;                 /* count still‑slow pages */
        }
    }

    if (need) {
        int *dst = malloc(need * sizeof(int));
        for (size_t i = 0; i < need; i++) dst[i] = fast;
        if (move_pages(pid, need, addr, dst, status,
                       MPOL_MF_MOVE | MPOL_MF_MOVE_ALL) < 0)
            perror("move_pages migrate");
        free(dst);
    }
out:
    free(addr); free(status);
}

static int detect_node(pid_t pid, unsigned long addr) {
    void *p = (void*)addr; int st;
    return move_pages(pid, 1, &p, NULL, &st, 0) < 0 ? -1 : st;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <pid> [file] [slow fast]\n", argv[0]);
        return 1;
    }

    pid_t pid = atoi(argv[1]);
    const char *file = (argc >= 3) ? argv[2] : "/dev/shm/my_shared_memory";

    struct vma *vmas; int nv = find_vmas(pid, file, &vmas);
    if (nv <= 0) {
        fprintf(stderr, "No mapping of %s in pid %d\n", file, pid);
        return 1;
    }

    int slow, fast = 0;
    if (argc >= 5) { slow = atoi(argv[3]); fast = atoi(argv[4]); }
    else { slow = detect_node(pid, vmas[0].start); if (slow < 0) { perror("detect_node"); return 1; } }

    printf("Promoting %d VMA(s) from node %d → %d (stride %zu bytes)\n",
           nv, slow, fast, vmas[0].step);

    while (1) {
        size_t remaining = 0;
        for (int i = 0; i < nv; i++) migrate_vma(pid, &vmas[i], slow, fast, &remaining);
        printf("remaining=%zu pages on node %d\n", remaining, slow);
        fflush(stdout);
        if (!remaining) break;
        usleep(20000);                           /* 20 ms window */
    }

    printf("All pages on node %d migrated to %d. Done.\n", slow, fast);
    free(vmas);
    return 0;
}


