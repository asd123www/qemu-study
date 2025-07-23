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

#define META_STATE_LENGTH      (1ULL<<20)   /* 1 MiB  */
#define HOT_PAGE_STATE_LENGTH  (9ULL<<20)   /* 9 MiB  */

#ifndef __NR_move_pages
#define __NR_move_pages 279
#endif
long move_pages(int pid, unsigned long cnt, void **pages,
                       const int *nodes, int *status, int flags)
{
    return syscall(__NR_move_pages, pid, cnt, pages, nodes, status, flags);
}

struct vma { unsigned long start, end; size_t step; };

/* stride (4 KiB vs 2 MiB) detection */
static size_t detect_step(pid_t pid, unsigned long addr)
{
    char path[64]; snprintf(path, sizeof path, "/proc/%d/smaps", pid);
    FILE *fp = fopen(path, "r"); if (!fp) return 4096;

    char *line = NULL; size_t n = 0; int in = 0, huge = 0;
    while (getline(&line, &n, fp) != -1) {
        unsigned long lo, hi;
        if (sscanf(line, "%lx-%lx", &lo, &hi) == 2) { in = (lo == addr); continue; }
        if (!in) continue;
        if (!strncmp(line,"ShmemPmdMapped:",16)||!strncmp(line,"ShmemHugePages:",16)){
            unsigned long v; if (sscanf(strchr(line,':'),": %lu",&v)==1 && v){ huge=1; break;}
        }
        if (line[0]=='\n') break;
    }
    free(line); fclose(fp);
    return huge ? 2*1024*1024ULL : 4096ULL;
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

/* hot-list loader */
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

/* migrate hot pages – returns # migrated */
static size_t migrate_hot(pid_t pid, struct vma *v,
                          const uint64_t *hot, size_t n,
                          int slow,int fast)
{
    if(!n) return 0;
    void **addr=malloc(n*sizeof(void*)); int *st=malloc(n*sizeof(int));
    size_t want=0;
    for(size_t i=0;i<n;i++){
        uint64_t off=hot[i];
        if(off < v->end - v->start) addr[want++]=(void*)(v->start+off);
    }
    if(!want){free(addr);free(st);return 0;}
    if(move_pages(pid,want,addr,NULL,st,0)<0){perror("move_pages hot q");goto out;}
    size_t need=0;
    for(size_t i=0;i<want;i++) if(st[i]==slow) addr[need++]=addr[i];
    if(need){
        int *dst=malloc(need*sizeof(int));
        for(size_t i=0;i<need;i++) dst[i]=fast;
        if(move_pages(pid,need,addr,dst,st,MPOL_MF_MOVE|MPOL_MF_MOVE_ALL)<0)
            perror("move_pages hot m");
        free(dst);
    }
out:   free(addr); free(st); return need;
}

/* sweep – returns # migrated, sets remaining */
static size_t sweep(pid_t pid, struct vma *v,int slow,int fast,size_t *remain)
{
    size_t np=(v->end-v->start)/v->step;
    void **a=malloc(np*sizeof(void*)); int *st=malloc(np*sizeof(int));
    for(size_t i=0;i<np;i++) a[i]=(void*)(v->start+i*v->step);
    if(move_pages(pid,np,a,NULL,st,0)<0){perror("move_pages q");goto out;}

    size_t need=0;
    for(size_t i=0;i<np;i++){
        if(st[i]==slow){a[need++]=a[i]; (*remain)++;}
    }
    if(need){
        int *dst=malloc(need*sizeof(int));
        for(size_t i=0;i<need;i++) dst[i]=fast;
        if(move_pages(pid,need,a,dst,st,MPOL_MF_MOVE|MPOL_MF_MOVE_ALL)<0)
            perror("move_pages m");
        free(dst);
    }
out:   free(a); free(st); return need;
}

static int detect_node(pid_t pid,unsigned long addr)
{
    void *p=(void*)addr; int st;
    return move_pages(pid,1,&p,NULL,&st,0)<0?-1:st;
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

    printf("Promoting %d VMA(s) node %d → %d (stride %zu B)\n",
           nv,slow,fast,v[0].step);

    size_t iter=0;
    while(1){
        uint64_t *hot=NULL; size_t nhot=load_hot_list(file,&hot);
        size_t hot_mig=0, sweep_mig=0, remaining=0;

        if(nhot){
            for(int i=0;i<nv;i++)
                hot_mig+=migrate_hot(pid,&v[i],hot,nhot,slow,fast);
            free(hot);
        }

        for(int i=0;i<nv;i++)
            sweep_mig+=sweep(pid,&v[i],slow,fast,&remaining);

        printf("[iter %zu] hot_list=%zu  hot_migrated=%zu  sweep_migrated=%zu  remaining=%zu\n",
               ++iter, nhot, hot_mig, sweep_mig, remaining);
        fflush(stdout);
        if((sweep_mig + hot_mig) == 0) break;


        if(!remaining) break;
        usleep(20000); /* 20 ms */
    }
    puts("Done.");
    free(v);
    return 0;
}

