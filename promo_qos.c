#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <inttypes.h>
#include <numaif.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef __NR_migrate_pages
#define __NR_migrate_pages 256
#endif
#ifndef __NR_move_pages
#define __NR_move_pages 279
#endif

static long sys_migrate_pages(pid_t pid, unsigned long maxnode,
                              const unsigned long *old_nodes,
                              const unsigned long *new_nodes)
{ return syscall(__NR_migrate_pages, pid, maxnode, old_nodes, new_nodes); }

static long sys_move_pages(int pid, unsigned long cnt, void **pages,
                           const int *nodes, int *status, int flags)
{ return syscall(__NR_move_pages, pid, cnt, pages, nodes, status, flags); }

struct vma { unsigned long start, end; size_t step; };

static size_t detect_step(pid_t pid, unsigned long addr) {
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
    return huge ? (size_t)(2*1024*1024ULL) : 4096ULL;
}

static int find_vmas(pid_t pid, const char *needle, struct vma **out) {
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

static int detect_node(pid_t pid,unsigned long addr){
    void *p=(void*)addr; int st;
    return sys_move_pages(pid,1,&p,NULL,&st,0)<0?-1:st;
}

static void build_masks(unsigned long maxnode, int from_node, int to_node,
                        unsigned long **from, unsigned long **to, size_t *words_out)
{
    const size_t UBITS = sizeof(unsigned long)*8;
    size_t words = (maxnode + UBITS - 1) / UBITS; if (!words) words = 1;
    unsigned long *oldm = (unsigned long*)calloc(words, sizeof(unsigned long));
    unsigned long *newm = (unsigned long*)calloc(words, sizeof(unsigned long));
    oldm[from_node/UBITS] |= 1UL << (from_node % UBITS);
    newm[to_node/UBITS]   |= 1UL << (to_node   % UBITS);
    *from = oldm; *to = newm; *words_out = words;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr,"Usage: %s <pid> [file] [slow fast]\n", argv[0]);
        return 1;
    }
    pid_t pid = atoi(argv[1]);
    const char *file = (argc>=3 && strncmp(argv[2],"--",2)!=0) ? argv[2] : "/dev/shm/my_shared_memory";

    int argi = (file && strncmp(file,"--",2)==0) ? 2 : 3;
    int have_nodes = 0, slow=-1, fast=0;
    if (argc >= argi+2 && strncmp(argv[argi],"--",2)!=0 && strncmp(argv[argi+1],"--",2)!=0) {
        slow = atoi(argv[argi]); fast = atoi(argv[argi+1]); have_nodes = 1;
    }

    struct vma *v = NULL; int nv = find_vmas(pid, file, &v);
    if (nv <= 0) { fprintf(stderr, "No mapping of %s in %d\n", file, pid); return 1; }

    if (!have_nodes) {
        slow = detect_node(pid, v[0].start);
        if (slow < 0) { perror("detect_node"); free(v); return 1; }
        fast = 0;
    }

    size_t thp_vmas = 0; unsigned long long bytes = 0, thp_bytes = 0;
    for (int i=0;i<nv;i++){
        bytes += (unsigned long long)(v[i].end - v[i].start);
        if (v[i].step == 2*1024*1024ULL) { thp_vmas++; thp_bytes += (unsigned long long)(v[i].end - v[i].start); }
    }

    if (thp_bytes == 0) {
        fprintf(stderr,
            "Refusing to migrate: pool is 4 KiB-backed (no THP). Migrating ~%llu MiB with 4K pages\n"
            "will trigger millions of PTE updates and IPI/TLB shootdowns and will take ~10s on stock kernels.\n"
            "Make the pool THP-backed first, then re-run (expect ~5–10× faster).\n"
            "Hints:\n"
            "  1) remount /dev/shm with THP:  mount -o remount,huge=always /dev/shm  (then recreate the shm file)\n"
            "  2) or move the pool to hugetlbfs (2M/1G) and remap both apps.\n",
            bytes >> 20);
        free(v);
        return 2;
    }

    /* If we’re here, at least some VMAs are THP-backed: proceed with migrate_pages */
    int numnodes = 64; /* cheap fallback */
    unsigned long *from=NULL,*to=NULL; size_t words=0;
    build_masks((unsigned long)numnodes, slow, fast, &from, &to, &words);

    printf("THP-backed promote: pid=%d  %d -> %d  THP_bytes=%.2f GiB / total=%.2f GiB\n",
           pid, slow, fast, (double)thp_bytes/ (1024.0*1024.0*1024.0),
           (double)bytes/ (1024.0*1024.0*1024.0));

    long not_moved = sys_migrate_pages(pid, (unsigned long)numnodes, from, to);
    if (not_moved < 0) perror("migrate_pages");
    else printf("migrate_pages: %ld pages not moved\n", not_moved);

    free(from); free(to); free(v);
    return 0;
}


