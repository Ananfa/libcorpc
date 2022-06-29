#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <pthread.h>

struct mem_align {
    void *origin_start;  // for free
    void *start;         // data addr start, align page size
    void *end;           // data addr end,   align page size
    void *origin_end;
};

int malloc_align_page(size_t memsize, struct mem_align *mem)
{
    if (memsize == 0 || mem == NULL)
        return -1;

    memset(mem, 0, sizeof(*mem));
    long pagesize = sysconf(_SC_PAGE_SIZE);
    if (pagesize == -1) {
        perror("sysconf err");
        return -1;
    }
    printf("pagesize %#x\n", pagesize);

    size_t datasize = memsize + pagesize * 2;
    mem->origin_start = malloc(datasize);
    if(mem->origin_start == NULL)
        return -1;
    mem->origin_end = mem->origin_start + datasize;

    long mask = pagesize - 1;
    mem->start = (void *)((long)(mem->origin_start + pagesize) & ~mask);
    long pagenum = memsize / pagesize + 1;
    mem->end = mem->start + pagesize * pagenum;
    return 0;
}

int main()
{
    int ret;
    struct mem_align mem;

    size_t stacksize;
    pthread_attr_t thread_attr;
    pthread_attr_init(&thread_attr);
    ret = pthread_attr_getstacksize(&thread_attr,&stacksize);
    if (ret != 0) {
        return ret;
    }

    printf("stack size %d\n", stacksize);

    ret = malloc_align_page(1024, &mem);
    if (ret != 0) {
        return ret;
    }

    printf("malloc mem: %#x - %#x\n", mem.origin_start, mem.origin_end);
    printf("align mem: %#x - %#x\n", mem.start, mem.end);

    ret = mprotect(mem.start, (size_t)(mem.end) - (size_t)(mem.start), PROT_READ);
    if (ret == -1) {
        perror("mportect");
        return ret;
    }

    sleep(600); // 让进程先挂起不退出，看/proc/[pid]/maps里的地址空间对应页的权限
    free(mem.origin_start);
    return 0;
}