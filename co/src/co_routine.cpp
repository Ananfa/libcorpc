/*
* Tencent is pleased to support the open source community by making Libco available.

* Copyright (C) 2014 THL A29 Limited, a Tencent company. All rights reserved.
*
* Licensed under the Apache License, Version 2.0 (the "License"); 
* you may not use this file except in compliance with the License. 
* You may obtain a copy of the License at
*
*	http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, 
* software distributed under the License is distributed on an "AS IS" BASIS, 
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
* See the License for the specific language governing permissions and 
* limitations under the License.
*/

#include "co_routine.h"
#include "co_routine_inner.h"
#include "co_epoll.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string>
#include <map>

#include <poll.h>
#include <sys/time.h>
#include <errno.h>

#include <assert.h>

#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <limits.h>
#include <execinfo.h>

#ifdef STACK_PROTECT
	#include <sys/mman.h>
#endif

extern "C"
{
	extern void coctx_swap( coctx_t *,coctx_t* ) asm("coctx_swap");
};
using namespace std;
stCoRoutine_t *GetCurrCo( stCoRoutineEnv_t *env );
struct stCoEpoll_t;

struct stCoRoutineEnv_t
{
	stCoRoutine_t *pCallStack[ 128 ];
	int iCallStackSize;
	stCoEpoll_t *pEpoll;

	//for copy stack log lastco and nextco
	stCoRoutine_t* pending_co;
	stCoRoutine_t* occupy_co;
};

void co_log_err( const char *fmt,... )
{
    va_list arg;
    va_start(arg, fmt);
    
    vfprintf(stdout, fmt, arg);
    
    va_end(arg);
}


#if defined( __LIBCO_RDTSCP__) 
static unsigned long long counter(void)
{
	register uint32_t lo, hi;
	register unsigned long long o;
	__asm__ __volatile__ (
			"rdtscp" : "=a"(lo), "=d"(hi)::"%rcx"
			);
	o = hi;
	o <<= 32;
	return (o | lo);

}
static unsigned long long getCpuKhz()
{
	FILE *fp = fopen("/proc/cpuinfo","r");
	if(!fp) return 1;
	char buf[4096] = {0};
	fread(buf,1,sizeof(buf),fp);
	fclose(fp);

	char *lp = strstr(buf,"cpu MHz");
	if(!lp) return 1;
	lp += strlen("cpu MHz");
	while(*lp == ' ' || *lp == '\t' || *lp == ':')
	{
		++lp;
	}

	double mhz = atof(lp);
	unsigned long long u = (unsigned long long)(mhz * 1000);
	return u;
}
#endif

static unsigned long long GetTickMS()
{
#if defined( __LIBCO_RDTSCP__) 
	static uint32_t khz = getCpuKhz();
	return counter() / khz;
#else
	struct timeval now = { 0 };
	gettimeofday( &now,NULL );
	unsigned long long u = now.tv_sec;
	u *= 1000;
	u += now.tv_usec / 1000;
	return u;
#endif
}

pid_t GetPid()
{
    static __thread pid_t pid = 0;
    static __thread pid_t tid = 0;
    if( !pid || !tid || pid != getpid() )
    {
        pid = getpid();
#if defined( __APPLE__ )
        tid = ::pthread_mach_thread_np(::pthread_self());
		//tid = syscall( SYS_gettid );
		if( -1 == (long)tid )
		{
			tid = pid;
		}
#elif defined( __FreeBSD__ )
		syscall(SYS_thr_self, &tid);
		if( tid < 0 )
		{
			tid = pid;
		}
#else 
        tid = syscall( __NR_gettid );
#endif

    }
    return tid;

}
/*
static pid_t GetPid()
{
	char **p = (char**)pthread_self();
	return p ? *(pid_t*)(p + 18) : getpid();
}
*/
template <class T,class TLink>
void RemoveFromLink(T *ap)
{
	TLink *lst = ap->pLink;
	if(!lst) return ;
	assert( lst->head && lst->tail );

	if( ap == lst->head )
	{
		lst->head = ap->pNext;
		if(lst->head)
		{
			lst->head->pPrev = NULL;
		}
	}
	else
	{
		if(ap->pPrev)
		{
			ap->pPrev->pNext = ap->pNext;
		}
	}

	if( ap == lst->tail )
	{
		lst->tail = ap->pPrev;
		if(lst->tail)
		{
			lst->tail->pNext = NULL;
		}
	}
	else
	{
		ap->pNext->pPrev = ap->pPrev;
	}

	ap->pPrev = ap->pNext = NULL;
	ap->pLink = NULL;
}

template <class TNode,class TLink>
void inline AddTail(TLink*apLink,TNode *ap)
{
	if( ap->pLink )
	{
		return ;
	}
	if(apLink->tail)
	{
		apLink->tail->pNext = (TNode*)ap;
		ap->pNext = NULL;
		ap->pPrev = apLink->tail;
		apLink->tail = ap;
	}
	else
	{
		apLink->head = apLink->tail = ap;
		ap->pNext = ap->pPrev = NULL;
	}
	ap->pLink = apLink;
}

template <class TNode,class TLink>
void inline PopHead( TLink*apLink )
{
	if( !apLink->head ) 
	{
		return ;
	}
	TNode *lp = apLink->head;
	if( apLink->head == apLink->tail )
	{
		apLink->head = apLink->tail = NULL;
	}
	else
	{
		apLink->head = apLink->head->pNext;
	}

	lp->pPrev = lp->pNext = NULL;
	lp->pLink = NULL;

	if( apLink->head )
	{
		apLink->head->pPrev = NULL;
	}
}

template <class TNode,class TLink>
void inline Join( TLink*apLink,TLink *apOther )
{
	//printf("apOther %p\n",apOther);
	//if( !apOther->head )
	//{
	//	return ;
	//}
	TNode *lp = apOther->head;
	while( lp )
	{
		lp->pLink = apLink;
		lp = lp->pNext;
	}
	lp = apOther->head;
	if(apLink->tail)
	{
		apLink->tail->pNext = (TNode*)lp;
		lp->pPrev = apLink->tail;
		apLink->tail = apOther->tail;
	}
	else
	{
		apLink->head = apOther->head;
		apLink->tail = apOther->tail;
	}

	apOther->head = apOther->tail = NULL;
}

/////////////////for copy stack //////////////////////////
stStackMem_t* co_alloc_stackmem(unsigned int stack_size)
{
#ifdef STACK_PROTECT
	static uint64_t pagesize = sysconf(_SC_PAGE_SIZE);
	static uint64_t mask = pagesize - 1;
#endif

	stStackMem_t* stack_mem = (stStackMem_t*)malloc(sizeof(stStackMem_t));
	stack_mem->occupy_co= NULL;
	stack_mem->stack_size = stack_size;
#ifdef STACK_PROTECT
	// 加入保护页，堆栈溢出时能第一时间报错
	// 保护页处于内存块低地址空间，分配的低地址空间的第一个完整页作为保护页
	// 会有一定内存空间被浪费
	stack_mem->origin = (char*)malloc(stack_size + pagesize * 2);
	void *protect_start = (void *)((uint64_t)(stack_mem->origin + pagesize) & ~mask);
	stack_mem->stack_buffer = (char*)protect_start + pagesize;
	if (mprotect(protect_start, pagesize, PROT_NONE) == -1) {
		perror("mprotect");
        exit(0);
    }

#else
	stack_mem->stack_buffer = (char*)malloc(stack_size);
#endif
	stack_mem->stack_bp = stack_mem->stack_buffer + stack_size;
	return stack_mem;
}

stShareStack_t* co_alloc_sharestack(int count, int stack_size)
{
	stShareStack_t* share_stack = (stShareStack_t*)malloc(sizeof(stShareStack_t));
	share_stack->alloc_idx = 0;
	share_stack->stack_size = stack_size;

	//alloc stack array
	share_stack->count = count;
	stStackMem_t** stack_array = (stStackMem_t**)calloc(count, sizeof(stStackMem_t*));
	for (int i = 0; i < count; i++)
	{
		stack_array[i] = co_alloc_stackmem(stack_size);
	}
	share_stack->stack_array = stack_array;
	return share_stack;
}

static stStackMem_t* co_get_stackmem(stShareStack_t* share_stack)
{
	if (!share_stack)
	{
		return NULL;
	}
	int idx = share_stack->alloc_idx % share_stack->count;
	share_stack->alloc_idx++;

	return share_stack->stack_array[idx];
}


// ----------------------------------------------------------------------------
struct stTimeoutItemLink_t;
struct stTimeoutItem_t;
struct stCoEpoll_t
{
	int iEpollFd;
	static const int _EPOLL_SIZE = 1024 * 10;

	struct stTimeout_t *pTimeout;

	struct stTimeoutItemLink_t *pstTimeoutList;

    struct stTimeoutItemLink_t *pstNoTimeoutList;

	struct stTimeoutItemLink_t *pstActiveList;
    
	co_epoll_res *result; 

};

typedef void (*OnPreparePfn_t)( stTimeoutItem_t *,struct epoll_event &ev, stTimeoutItemLink_t *active );
typedef void (*OnProcessPfn_t)( stTimeoutItem_t *);

struct stTimeoutItem_t
{
	stTimeoutItem_t *pPrev;
	stTimeoutItem_t *pNext;
	stTimeoutItemLink_t *pLink;

	unsigned long long ullExpireTime;

	OnPreparePfn_t pfnPrepare;
	OnProcessPfn_t pfnProcess;

	void *pArg; // routine 
	bool bTimeout;
};

struct stTimeoutItemLink_t
{
	stTimeoutItem_t *head;
	stTimeoutItem_t *tail;
};

struct stTimeout_t
{
    stTimeoutItemLink_t *pItems;
    int iItemSize;
    uint64_t *pFlags;
    int iFlagSize;
    unsigned long long ullStart;
    long long llStartIdx;
};

stTimeout_t *AllocTimeout( int iSize )
{
    assert((iSize & (iSize-1))==0);
    if (iSize & (iSize-1)) {
        co_log_err("CO_ERR: AllocTimeout line %d iSize is not power of 2\n", __LINE__);
        return NULL;
    } else if (iSize < 1024) {
        co_log_err("CO_ERR: AllocTimeout line %d iSize is too small\n", __LINE__);
        return NULL;
    }
    
	stTimeout_t *lp = (stTimeout_t*)calloc( 1,sizeof(stTimeout_t) );	

	lp->iItemSize = iSize;
	lp->pItems = (stTimeoutItemLink_t*)calloc( 1,sizeof(stTimeoutItemLink_t) * lp->iItemSize );
    lp->iFlagSize = iSize >> 6;
    lp->pFlags = (uint64_t*)calloc( 1, 8 * lp->iFlagSize);
	lp->ullStart = GetTickMS();
	lp->llStartIdx = 0;

	return lp;
}

void FreeTimeout( stTimeout_t *apTimeout )
{
	free( apTimeout->pItems );
	free ( apTimeout );
}

int AddTimeout( stTimeout_t *apTimeout,stTimeoutItem_t *apItem ,unsigned long long allNow )
{
	if( apTimeout->ullStart == 0 )
	{
		apTimeout->ullStart = allNow;
		apTimeout->llStartIdx = 0;
	}
    
	if( allNow < apTimeout->ullStart )
	{
		co_log_err("CO_ERR: AddTimeout line %d allNow %llu < apTimeout->ullStart %llu\n",
					__LINE__,allNow,apTimeout->ullStart);

		return __LINE__;
	}
    
	if( apItem->ullExpireTime < allNow )
	{
		co_log_err("CO_ERR: AddTimeout line %d apItem->ullExpireTime %llu allNow %llu apTimeout->ullStart %llu\n",
					__LINE__,apItem->ullExpireTime,allNow,apTimeout->ullStart);

		return __LINE__;
	}
    
	unsigned long long diff = apItem->ullExpireTime - apTimeout->ullStart;

	if( diff >= (unsigned long long)apTimeout->iItemSize )
	{
		diff = apTimeout->iItemSize - 1;
		//co_log_err("CO_ERR: AddTimeout line %d diff %d\n",
		//			__LINE__,diff);

		//return __LINE__;
	}
    
    int idx = ( apTimeout->llStartIdx + diff ) & (apTimeout->iItemSize - 1);
	AddTail( apTimeout->pItems + idx , apItem );
    
    // set flag
    apTimeout->pFlags[idx >> 6] |= uint64_t(1) << (idx & 63);

	return 0;
}

inline void TakeAllTimeout( stCoEpoll_t *ctx,unsigned long long allNow,stTimeoutItemLink_t *apResult )
{
    stTimeout_t *apTimeout = ctx->pTimeout;
	if( apTimeout->ullStart == 0 )
	{
		apTimeout->ullStart = allNow;
		apTimeout->llStartIdx = 0;
	}

	if( allNow < apTimeout->ullStart )
	{
        co_log_err("CO_ERR: TakeAllTimeout line %d allNow %llu < apTimeout->ullStart %llu\n",
                   __LINE__,allNow,apTimeout->ullStart);
        
		return ;
	}
    
	int cnt = allNow - apTimeout->ullStart + 1;
	if( cnt > apTimeout->iItemSize )
	{
		cnt = apTimeout->iItemSize;
	}
    
	if( cnt < 0 )
	{
		return;
	}
    
	for( int i = 0; i<cnt; i++)
	{
		int idx = ( apTimeout->llStartIdx + i) & (apTimeout->iItemSize - 1);
        
        if ((apTimeout->pItems + idx)->head) {
            // 清flag
            apTimeout->pFlags[idx >> 6] &= ~(uint64_t(1) << (idx & 63));
            
            Join<stTimeoutItem_t,stTimeoutItemLink_t>( apResult,apTimeout->pItems + idx  );
        }
	}
    
	apTimeout->ullStart = allNow;
	apTimeout->llStartIdx = (apTimeout->llStartIdx + cnt - 1) & (apTimeout->iItemSize - 1);
}

inline int GetNextTimeoutInASecond( stTimeout_t *apTimeout, unsigned long long allNow)
{
    // 以8毫秒为粒度来判断（刚好是一字节）
    if( apTimeout->ullStart == 0 )
    {
        apTimeout->ullStart = allNow;
        apTimeout->llStartIdx = 0;
    }
    
    if( allNow < apTimeout->ullStart )
    {
        co_log_err("CO_ERR: GetNextTimeout line %d allNow %llu < apTimeout->ullStart %llu\n",
                   __LINE__,allNow,apTimeout->ullStart);
        
        return 1024;
    }
    
    uint64_t startIdx = apTimeout->llStartIdx >> 6;
    int startFlagIdx = apTimeout->llStartIdx & 63;
    int shiftRightCnt = startFlagIdx & 0xFFFFFFF8; // 8对齐
    uint64_t startMask = int64_t(0x8000000000000000) >> (63 - startFlagIdx);
    uint64_t startFlag = (apTimeout->pFlags[startIdx] & startMask) >> shiftRightCnt;
    if (startFlag) {
        // 再按字节判断
        if (startFlag & int64_t(0x00000000000000FF)) {
            return 8;
        } else if (startFlag & int64_t(0x000000000000FF00)) {
            return 16;
        } else if (startFlag & int64_t(0x0000000000FF0000)) {
            return 24;
        } else if (startFlag & int64_t(0x00000000FF000000)) {
            return 32;
        } else if (startFlag & int64_t(0x000000FF00000000)) {
            return 40;
        } else if (startFlag & int64_t(0x0000FF0000000000)) {
            return 48;
        } else if (startFlag & int64_t(0x00FF000000000000)) {
            return 56;
        } else {
            return 64;
        }
    }
    
    int ret = 64 - shiftRightCnt;
    // 注意：这里假定数组表示的时间远大于1秒，endIdx不会追上startIdx
    long long llendIdx = (apTimeout->llStartIdx + 1000) & (apTimeout->iItemSize - 1);
    int64_t endIdx = llendIdx >> 6;
    int moldMask = apTimeout->iFlagSize - 1; // 因为apTimeout->iFlagSize是2的n次方
    for (int i = (startIdx + 1) & moldMask; i != endIdx; i = (i + 1) & moldMask) {
        uint64_t flag = apTimeout->pFlags[i];
        
        if (flag) {
            if (flag & int64_t(0x00000000000000FF)) {
                ret += 8;
            } else if (flag & int64_t(0x000000000000FF00)) {
                ret += 16;
            } else if (flag & int64_t(0x0000000000FF0000)) {
                ret += 24;
            } else if (flag & int64_t(0x00000000FF000000)) {
                ret += 32;
            } else if (flag & int64_t(0x000000FF00000000)) {
                ret += 40;
            } else if (flag & int64_t(0x0000FF0000000000)) {
                ret += 48;
            } else if (flag & int64_t(0x00FF000000000000)) {
                ret += 56;
            } else {
                ret += 64;
            }
            return ret;
        }
        
        ret += 64;
    }
    
    int endFlagIdx = llendIdx & 63;
    uint64_t endMask = (uint64_t(0x1) << (endFlagIdx + 1)) - 1; //~(int64_t(0x8000000000000000) >> (63 - endFlagIdx));
    uint64_t endFlag = apTimeout->pFlags[endIdx] & endMask;
    if (endFlag) {
        if (endFlag & int64_t(0x00000000000000FF)) {
            ret += 8;
        } else if (endFlag & int64_t(0x000000000000FF00)) {
            ret += 16;
        } else if (endFlag & int64_t(0x0000000000FF0000)) {
            ret += 24;
        } else if (endFlag & int64_t(0x00000000FF000000)) {
            ret += 32;
        } else if (endFlag & int64_t(0x000000FF00000000)) {
            ret += 40;
        } else if (endFlag & int64_t(0x0000FF0000000000)) {
            ret += 48;
        } else if (endFlag & int64_t(0x00FF000000000000)) {
            ret += 56;
        } else {
            ret += 64;
        }
    }
    
    return ret;
}

static int CoRoutineFunc( stCoRoutine_t *co,void * )
{
	if( co->pfn )
	{
		co->pfn( co->arg );
	}
	co->cEnd = 1;

	stCoRoutineEnv_t *env = co->env;

	co_yield_env( env );

	return 0;
}

struct stCoRoutine_t *co_create_env( stCoRoutineEnv_t * env, const stCoRoutineAttr_t* attr,
		pfn_co_routine_t pfn,void *arg )
{

	stCoRoutineAttr_t at;
	if( attr )
	{
		memcpy( &at,attr,sizeof(at) );
	}
	if( at.stack_size <= 0 )
	{
		at.stack_size = 128 * 1024;
	}
	else if( at.stack_size > 1024 * 1024 * 8 )
	{
		at.stack_size = 1024 * 1024 * 8;
	}

	if( at.stack_size & 0xFFF ) 
	{
		at.stack_size &= ~0xFFF;
		at.stack_size += 0x1000;
	}

	stCoRoutine_t *lp = (stCoRoutine_t*)malloc( sizeof(stCoRoutine_t) );
	
	memset( lp,0,(long)(sizeof(stCoRoutine_t))); 


	lp->env = env;
	lp->pfn = pfn;
	lp->arg = arg;

	stStackMem_t* stack_mem = NULL;
	if( at.share_stack )
	{
		stack_mem = co_get_stackmem( at.share_stack);
		at.stack_size = at.share_stack->stack_size;
	}
	else
	{
		stack_mem = co_alloc_stackmem(at.stack_size);
	}
	lp->stack_mem = stack_mem;

	lp->ctx.ss_sp = stack_mem->stack_buffer;
	lp->ctx.ss_size = at.stack_size;

	lp->cStart = 0;
	lp->cEnd = 0;
	lp->cIsMain = 0;
	lp->cEnableSysHook = 0;
	lp->cIsShareStack = at.share_stack != NULL;

	lp->save_size = 0;
	lp->save_buffer = NULL;

	return lp;
}

int co_create( stCoRoutine_t **ppco,const stCoRoutineAttr_t *attr,pfn_co_routine_t pfn,void *arg )
{
	if( !co_get_curr_thread_env() ) 
	{
		co_init_curr_thread_env();
	}
	stCoRoutine_t *co = co_create_env( co_get_curr_thread_env(), attr, pfn,arg );
	*ppco = co;

	return 0;
}

void co_free( stCoRoutine_t *co )
{
    if (!co->cIsShareStack) 
    {
#ifdef STACK_PROTECT
    	free(co->stack_mem->origin);
#else
        free(co->stack_mem->stack_buffer);
#endif
        free(co->stack_mem);
    } else {
        // fix by lxk here

        if (co->stack_mem->occupy_co == co) {
            co->stack_mem->occupy_co = NULL;
        }
        
        if (co->save_buffer)
        {
            free(co->save_buffer), co->save_buffer = NULL;
        }
    }
    free( co );
}

void co_release( stCoRoutine_t *co )
{
    co_free( co );
}

void co_swap(stCoRoutine_t* curr, stCoRoutine_t* pending_co);

void co_resume( stCoRoutine_t *co )
{
	stCoRoutineEnv_t *env = co->env;
    assert(env == co_self()->env);
    
	stCoRoutine_t *lpCurrRoutine = env->pCallStack[ env->iCallStackSize - 1 ];
	if( !co->cStart )
	{
		coctx_make( &co->ctx,(coctx_pfn_t)CoRoutineFunc,co,0 );
		co->cStart = 1;
	}
	env->pCallStack[ env->iCallStackSize++ ] = co;
	co_swap( lpCurrRoutine, co );


}

void ActiveProcess( stTimeoutItem_t * ap )
{
	stCoRoutine_t *co = (stCoRoutine_t*)ap->pArg;
	co_resume( co );

	free(ap);
}

void co_activate( stCoRoutine_t *co ) {
	stCoRoutineEnv_t *env = co->env;
	assert(env == co_self()->env);

	stTimeoutItem_t* item = (stTimeoutItem_t*)calloc(1, sizeof(stTimeoutItem_t));
	item->pfnProcess = ActiveProcess;
	item->pArg = co;

	AddTail( env->pEpoll->pstActiveList, item );
}

void co_yield_env( stCoRoutineEnv_t *env )
{
	
	stCoRoutine_t *last = env->pCallStack[ env->iCallStackSize - 2 ];
	stCoRoutine_t *curr = env->pCallStack[ env->iCallStackSize - 1 ];

	env->iCallStackSize--;

	co_swap( curr, last);
}

void co_yield_ct()
{
	co_yield_env( co_get_curr_thread_env() );
}
void co_yield( stCoRoutine_t *co )
{
	co_yield_env( co->env );
}

#ifdef CHECK_MAX_STACK > 0
void print_stacktrace()
{
    int size = 16;
    void * array[16];
    int stack_num = backtrace(array, size);
    char ** stacktrace = backtrace_symbols(array, stack_num);
    for (int i = 0; i < stack_num; ++i)
    {
        co_log_err("%s\n", stacktrace[i]);
    }
    free(stacktrace);
}

thread_local uint32_t _t_max_stack_size(0);
void check_stack_size(stCoRoutine_t* co) {
// 注意：主协程不会进行栈拷贝，因此不需要校验主协程的栈大小
	if (co->cIsMain == 1) {
		return;
	}

	int len = co->stack_mem->stack_bp - co->stack_sp;

	if (len > _t_max_stack_size) {
		_t_max_stack_size = len;
		co_log_err("CO_DEBUG: ============= max stack size %lu\n", _t_max_stack_size);
		if (len > CHECK_MAX_STACK) {
			print_stacktrace();
		}
	}
}
#endif

thread_local uint32_t _t_max_malloc_size(0);
void save_stack_buffer(stCoRoutine_t* occupy_co)
{
	assert(occupy_co->cIsMain == 0);

	///copy out
	int len = occupy_co->stack_mem->stack_bp - occupy_co->stack_sp;
	if (len > _t_max_malloc_size) {
		_t_max_malloc_size = len;
	}

#if CHECK_MAX_STACK > 0
	if (len > CHECK_MAX_STACK) {
		co_log_err("CO_WARN: ============= malloc size %lu\n", len);
	}
#endif

	if (occupy_co->save_buffer)
	{
		//free(occupy_co->save_buffer), occupy_co->save_buffer = NULL;
		free(occupy_co->save_buffer);
	}

	occupy_co->save_buffer = (char*)malloc(len); //malloc buf;
	occupy_co->save_size = len;

	memcpy(occupy_co->save_buffer, occupy_co->stack_sp, len);
}

void co_swap(stCoRoutine_t* curr, stCoRoutine_t* pending_co)
{
 	stCoRoutineEnv_t* env = co_get_curr_thread_env();

	//get curr stack sp
	char c;
	curr->stack_sp= &c;

#ifdef CHECK_MAX_STACK > 0
		check_stack_size(curr);
#endif

	if (!pending_co->cIsShareStack)
	{
		env->pending_co = NULL;
		env->occupy_co = NULL;
	}
	else 
	{
		env->pending_co = pending_co;
		//get last occupy co on the same stack mem
		stCoRoutine_t* occupy_co = pending_co->stack_mem->occupy_co;
		//set pending co to occupy thest stack mem;
		pending_co->stack_mem->occupy_co = pending_co;

		env->occupy_co = occupy_co;
		if (occupy_co && occupy_co != pending_co)
		{
			// change by lxk, 对已结束的协程就不进行栈拷贝了
			if (occupy_co->cEnd) { // 协程已结束，清理缓存栈数据
				// 清理协程缓存栈
				if (occupy_co->save_buffer)
				{
					free(occupy_co->save_buffer), occupy_co->save_buffer = NULL;
				}
			} else {
				save_stack_buffer(occupy_co);
			}
		}
	}

    assert(pending_co->env);
    
	//swap context
	coctx_swap(&(curr->ctx),&(pending_co->ctx) ); // 注意：执行这一句切换后本函数中所有变量的值都没意义了，需要重新赋值
    
	//stack buffer may be overwrite, so get again;
	stCoRoutineEnv_t* curr_env = co_get_curr_thread_env();
	stCoRoutine_t* update_occupy_co =  curr_env->occupy_co;
	stCoRoutine_t* update_pending_co = curr_env->pending_co;
	
    // fix by lxk here
    // before: if (update_occupy_co && update_pending_co && update_occupy_co != update_pending_co)
	if (update_pending_co && update_occupy_co != update_pending_co)
	{
		//resume stack buffer
		if (update_pending_co->save_buffer && update_pending_co->save_size > 0)
		{
#ifdef CHECK_MAX_STACK > 0
			if (update_pending_co->save_size > CHECK_MAX_STACK) {
				co_log_err("CO_WARN: ============= resuming stack size %lu\n", update_pending_co->save_size);
			}
#endif

			// 注意：执行这句memcpy后，函数中所有变量都可能被覆盖掉了，因为编译优化函数内变量在栈上的位置顺序是不定的，即stack_sp的位置不一定在所有函数变量的前面
			memcpy(update_pending_co->stack_sp, update_pending_co->save_buffer, update_pending_co->save_size); 

#ifdef CHECK_MAX_STACK > 0
			// 下面两个问题的原因可能是因为编译器的编译优化调整了函数内变量在栈上的布局顺序导致的
			//assert(update_pending_co == curr); // 问题一： 这里为什么能false? 
			//if (update_pending_co->save_size > CHECK_MAX_STACK) {
			//	co_log_err("CO_DEBUG: ============= resume stack size %lu\n", update_pending_co->save_size); // 问题二：这里打印的大小值明显大得离谱，为什么？
			//	print_stacktrace();
			//}
			stCoRoutineEnv_t* curr_env1 = co_get_curr_thread_env();
			stCoRoutine_t* update_pending_co1 = curr_env1->pending_co;
			// assert(update_pending_co1 == curr); // 问题三： 这里为什么还是false? 调试发现参数curr的地址值比变量c的地址值小，因此栈恢复时不会恢复curr值，其他函数内的局部变量地址也都比变量c的地址小，因此都没有被恢复
			if (update_pending_co1->save_size > CHECK_MAX_STACK) {
				co_log_err("CO_WARN: ============= resumed stack size %lu\n", update_pending_co1->save_size);
				print_stacktrace();
			}
#endif
		}
	}
}

//int poll(struct pollfd fds[], nfds_t nfds, int timeout);
// { fd,events,revents }
struct stPollItem_t ;
struct stPoll_t : public stTimeoutItem_t 
{
	struct pollfd *fds;
	nfds_t nfds; // typedef unsigned long int nfds_t;

	stPollItem_t *pPollItems;

	int iAllEventDetach;

	int iEpollFd;

	int iRaiseCnt;
};

struct stPollItem_t : public stTimeoutItem_t
{
	struct pollfd *pSelf;
	stPoll_t *pPoll;

	struct epoll_event stEvent;
};

/*
 *   EPOLLPRI 		POLLPRI    // There is urgent data to read.  
 *   EPOLLMSG 		POLLMSG
 *
 *   				POLLREMOVE
 *   				POLLRDHUP
 *   				POLLNVAL
 *
 * */
static uint32_t PollEvent2Epoll( short events )
{
	uint32_t e = 0;	
	if( events & POLLIN ) 	e |= EPOLLIN;
	if( events & POLLOUT )  e |= EPOLLOUT;
	if( events & POLLHUP ) 	e |= EPOLLHUP;
	if( events & POLLERR )	e |= EPOLLERR;
	if( events & POLLRDNORM ) e |= EPOLLRDNORM;
	if( events & POLLWRNORM ) e |= EPOLLWRNORM;
	return e;
}

static short EpollEvent2Poll( uint32_t events )
{
	short e = 0;	
	if( events & EPOLLIN ) 	e |= POLLIN;
	if( events & EPOLLOUT ) e |= POLLOUT;
	if( events & EPOLLHUP ) e |= POLLHUP;
	if( events & EPOLLERR ) e |= POLLERR;
	if( events & EPOLLRDNORM ) e |= POLLRDNORM;
	if( events & EPOLLWRNORM ) e |= POLLWRNORM;
	return e;
}

//static stCoRoutineEnv_t* g_arrCoEnvPerThread[ 204800 ] = { 0 };
static __thread stCoRoutineEnv_t* g_coRoutineEnv = nullptr;
void co_init_curr_thread_env()
{
    g_coRoutineEnv = (stCoRoutineEnv_t*)calloc( 1,sizeof(stCoRoutineEnv_t) );

	g_coRoutineEnv->iCallStackSize = 0;
	struct stCoRoutine_t *self = co_create_env( g_coRoutineEnv, NULL, NULL,NULL );
	self->cIsMain = 1;

	g_coRoutineEnv->pending_co = NULL;
	g_coRoutineEnv->occupy_co = NULL;

	coctx_init( &self->ctx );

	g_coRoutineEnv->pCallStack[ g_coRoutineEnv->iCallStackSize++ ] = self;

	stCoEpoll_t *ev = AllocEpoll();
	SetEpoll( g_coRoutineEnv,ev );
}

stCoRoutineEnv_t *co_get_curr_thread_env()
{
    return g_coRoutineEnv;
}

void OnPollProcessEvent( stTimeoutItem_t * ap )
{
	stCoRoutine_t *co = (stCoRoutine_t*)ap->pArg;
	co_resume( co );
}

void OnPollPreparePfn( stTimeoutItem_t * ap,struct epoll_event &e,stTimeoutItemLink_t *active )
{
	stPollItem_t *lp = (stPollItem_t *)ap;
	lp->pSelf->revents = EpollEvent2Poll( e.events );


	stPoll_t *pPoll = lp->pPoll;
	pPoll->iRaiseCnt++;

	if( !pPoll->iAllEventDetach )
	{
		pPoll->iAllEventDetach = 1;

		RemoveFromLink<stTimeoutItem_t,stTimeoutItemLink_t>( pPoll );

		AddTail( active,pPoll );

	}
}

// change by lxk here
void co_eventloop( stCoEpoll_t *ctx, pfn_co_eventloop_t pfn, void *arg )
{
	if( !ctx->result )
	{
		ctx->result =  co_epoll_res_alloc( stCoEpoll_t::_EPOLL_SIZE );
	}
	co_epoll_res *result = ctx->result;

	for(;;)
	{
        // 由于co_epoll_wait的系统消耗比较大，应尽量减少调用频率，可以根据下一超时时间点来设置waittime
        // 但由于超时链数据结构限制，需要遍历数组来找到下一超时事件，最坏情况下（链空的时候）需要遍历整个数组（60*1000个）元素
        // 方案：只查最近的1秒内的超时事件，增加超时位图数据结构来提高查找效率，而且以8毫秒为粒度（刚好是位图中的一字节数据）
        int waittime = GetNextTimeoutInASecond(ctx->pTimeout, GetTickMS());
        
		int ret = co_epoll_wait( ctx->iEpollFd,result,stCoEpoll_t::_EPOLL_SIZE, waittime );
        
		stTimeoutItemLink_t *active = (ctx->pstActiveList);
		stTimeoutItemLink_t *timeout = (ctx->pstTimeoutList);

		memset( timeout,0,sizeof(stTimeoutItemLink_t) );

		for(int i=0;i<ret;i++)
		{
			stTimeoutItem_t *item = (stTimeoutItem_t*)result->events[i].data.ptr;
			if( item->pfnPrepare )
			{
				item->pfnPrepare( item,result->events[i],active );
			}
			else
			{
				AddTail( active,item );
			}
		}

		unsigned long long now = GetTickMS();
		TakeAllTimeout( ctx,now,timeout );

		stTimeoutItem_t *lp = timeout->head;
        
		while( lp )
		{
			//printf("raise timeout %p\n",lp);
			lp->bTimeout = true;
			lp = lp->pNext;
		}

        if (timeout->head) {
            Join<stTimeoutItem_t,stTimeoutItemLink_t>( active,timeout );
        }

		lp = active->head;
		while( lp )
		{
			PopHead<stTimeoutItem_t,stTimeoutItemLink_t>( active );
            if (lp->bTimeout && now < lp->ullExpireTime) 
			{
				int ret = AddTimeout(ctx->pTimeout, lp, now);
				if (!ret) 
				{
					lp->bTimeout = false;
					lp = active->head;
					continue;
				}
			}
			if( lp->pfnProcess )
			{
				lp->pfnProcess( lp );
			}

			lp = active->head;
		}
		if( pfn )
		{
			if( -1 == pfn( arg ) )
			{
				break;
			}
		}

	}
}

void OnCoroutineEvent( stTimeoutItem_t * ap )
{
	stCoRoutine_t *co = (stCoRoutine_t*)ap->pArg;
	co_resume( co );
}

stCoEpoll_t *AllocEpoll()
{
	stCoEpoll_t *ctx = (stCoEpoll_t*)calloc( 1,sizeof(stCoEpoll_t) );

	ctx->iEpollFd = co_epoll_create( stCoEpoll_t::_EPOLL_SIZE );
	ctx->pTimeout = AllocTimeout( 0x10000 ); // 方便用位操作代替乘除法和去模操作
	
	ctx->pstActiveList = (stTimeoutItemLink_t*)calloc( 1,sizeof(stTimeoutItemLink_t) );
	ctx->pstTimeoutList = (stTimeoutItemLink_t*)calloc( 1,sizeof(stTimeoutItemLink_t) );
    ctx->pstNoTimeoutList = (stTimeoutItemLink_t*)calloc( 1,sizeof(stTimeoutItemLink_t) );

	return ctx;
}

void FreeEpoll( stCoEpoll_t *ctx )
{
	if( ctx )
	{
		free( ctx->pstActiveList );
		free( ctx->pstTimeoutList );
        free( ctx->pstNoTimeoutList );
		FreeTimeout( ctx->pTimeout );
		co_epoll_res_free( ctx->result );
	}
	free( ctx );
}

stCoRoutine_t *GetCurrCo( stCoRoutineEnv_t *env )
{
	return env->pCallStack[ env->iCallStackSize - 1 ];
}

stCoRoutine_t *GetCurrThreadCo( )
{
	stCoRoutineEnv_t *env = co_get_curr_thread_env();
	if( !env ) return 0;
	return GetCurrCo(env);
}

typedef int (*poll_pfn_t)(struct pollfd fds[], nfds_t nfds, int timeout);
int co_poll_inner( stCoEpoll_t *ctx,struct pollfd fds[], nfds_t nfds, int timeout, poll_pfn_t pollfunc)
{
    if (timeout == 0)
	{
		return pollfunc(fds, nfds, timeout);
	}
	
	int epfd = ctx->iEpollFd;
	stCoRoutine_t* self = co_self();

	//1.struct change
	stPoll_t& arg = *((stPoll_t*)malloc(sizeof(stPoll_t)));
	memset( &arg,0,sizeof(arg) );

	arg.iEpollFd = epfd;
	arg.fds = (pollfd*)calloc(nfds, sizeof(pollfd));
	arg.nfds = nfds;

	stPollItem_t arr[2];
	if( nfds < sizeof(arr) / sizeof(arr[0]) && !self->cIsShareStack)
	{
		arg.pPollItems = arr;
	}	
	else
	{
		arg.pPollItems = (stPollItem_t*)malloc( nfds * sizeof( stPollItem_t ) );
	}
	memset( arg.pPollItems,0,nfds * sizeof(stPollItem_t) );

	arg.pfnProcess = OnPollProcessEvent;
	arg.pArg = GetCurrCo( co_get_curr_thread_env() );
	
	
	//2. add epoll
	for(nfds_t i=0;i<nfds;i++)
	{
		arg.pPollItems[i].pSelf = arg.fds + i;
		arg.pPollItems[i].pPoll = &arg;

		arg.pPollItems[i].pfnPrepare = OnPollPreparePfn;
		struct epoll_event &ev = arg.pPollItems[i].stEvent;

		if( fds[i].fd > -1 )
		{
			ev.data.ptr = arg.pPollItems + i;
			ev.events = PollEvent2Epoll( fds[i].events );

			int ret = co_epoll_ctl( epfd,EPOLL_CTL_ADD, fds[i].fd, &ev );
			if (ret < 0 && errno == EPERM && nfds == 1 && pollfunc != NULL)
			{
				if( arg.pPollItems != arr )
				{
					free( arg.pPollItems );
					arg.pPollItems = NULL;
				}
				free(arg.fds);
				free(&arg);
				return pollfunc(fds, nfds, timeout);
			}
		}
		//if fail,the timeout would work
	}

	//3.add timeout
    int iRaiseCnt = 0;
    if (timeout > 0)
    {
        unsigned long long now = GetTickMS();
        arg.ullExpireTime = now + timeout;
        int ret = AddTimeout( ctx->pTimeout,&arg,now );
        if( ret != 0 )
        {
            co_log_err("CO_ERR: AddTimeout ret %d now %lld timeout %d arg.ullExpireTime %lld\n",
                       ret,now,timeout,arg.ullExpireTime);
            errno = EINVAL;
            iRaiseCnt = -1;
            
        }
        else
        {
            co_yield_env( co_get_curr_thread_env() );
            iRaiseCnt = arg.iRaiseCnt;
        }
    }
    else
    {
        AddTail( ctx->pstNoTimeoutList , &arg );
        
        co_yield_env( co_get_curr_thread_env() );
        iRaiseCnt = arg.iRaiseCnt;
    }

    {
        assert(arg.pLink == NULL);
		//clear epoll status and memory
		RemoveFromLink<stTimeoutItem_t,stTimeoutItemLink_t>( &arg );
        
		for(nfds_t i = 0;i < nfds;i++)
		{
			int fd = fds[i].fd;
			if( fd > -1 )
			{
				co_epoll_ctl( epfd,EPOLL_CTL_DEL,fd,&arg.pPollItems[i].stEvent );
			}
			fds[i].revents = arg.fds[i].revents;
		}


		if( arg.pPollItems != arr )
		{
			free( arg.pPollItems );
			arg.pPollItems = NULL;
		}

		free(arg.fds);
		free(&arg);
	}

	return iRaiseCnt;
}

int	co_poll( stCoEpoll_t *ctx,struct pollfd fds[], nfds_t nfds, int timeout_ms )
{
	return co_poll_inner(ctx, fds, nfds, timeout_ms, NULL);
}

void SetEpoll( stCoRoutineEnv_t *env,stCoEpoll_t *ev )
{
	env->pEpoll = ev;
}

stCoEpoll_t *co_get_epoll_ct()
{
	if( !co_get_curr_thread_env() )
	{
		co_init_curr_thread_env();
	}
	return co_get_curr_thread_env()->pEpoll;
}

struct stHookPThreadSpec_t
{
	stCoRoutine_t *co;
	void *value;

	enum 
	{
		size = 1024
	};
};

void *co_getspecific(pthread_key_t key)
{
	stCoRoutine_t *co = GetCurrThreadCo();
	if( !co || co->cIsMain )
	{
		return pthread_getspecific( key );
	}
	return co->aSpec[ key ].value;
}

int co_setspecific(pthread_key_t key, const void *value)
{
	stCoRoutine_t *co = GetCurrThreadCo();
	if( !co || co->cIsMain )
	{
		return pthread_setspecific( key,value );
	}
	co->aSpec[ key ].value = (void*)value;
	return 0;
}

void co_disable_hook_sys()
{
	stCoRoutine_t *co = GetCurrThreadCo();
	if( co )
	{
		co->cEnableSysHook = 0;
	}
}

static bool g_start_hook = false;
bool co_is_enable_sys_hook()
{
    if (!g_start_hook) {
        return false;
    }
    
	stCoRoutine_t *co = GetCurrThreadCo();
	return ( co && co->cEnableSysHook );
}

void co_start_hook()
{
    g_start_hook = true;
}

stCoRoutine_t *co_self()
{
	return GetCurrThreadCo();
}

//co cond
struct stCoCond_t;
struct stCoCondItem_t 
{
	stCoCondItem_t *pPrev;
	stCoCondItem_t *pNext;
	stCoCond_t *pLink;

	stTimeoutItem_t timeout;
};
struct stCoCond_t
{
	stCoCondItem_t *head;
	stCoCondItem_t *tail;
};
static void OnSignalProcessEvent( stTimeoutItem_t * ap )
{
	stCoRoutine_t *co = (stCoRoutine_t*)ap->pArg;
	co_resume( co );
}

stCoCondItem_t *co_cond_pop( stCoCond_t *link );
int co_cond_signal( stCoCond_t *si )
{
	stCoCondItem_t * sp = co_cond_pop( si );
	if( !sp ) 
	{
		return 0;
	}
	RemoveFromLink<stTimeoutItem_t,stTimeoutItemLink_t>( &sp->timeout );

	AddTail( co_get_curr_thread_env()->pEpoll->pstActiveList,&sp->timeout );

	return 0;
}

int co_cond_broadcast( stCoCond_t *si )
{
	for(;;)
	{
		stCoCondItem_t * sp = co_cond_pop( si );
		if( !sp ) return 0;

		RemoveFromLink<stTimeoutItem_t,stTimeoutItemLink_t>( &sp->timeout );

		AddTail( co_get_curr_thread_env()->pEpoll->pstActiveList,&sp->timeout );
	}

	return 0;
}

int co_cond_timedwait( stCoCond_t *link,int ms )
{
	stCoCondItem_t* psi = (stCoCondItem_t*)calloc(1, sizeof(stCoCondItem_t));
	psi->timeout.pArg = GetCurrThreadCo();
	psi->timeout.pfnProcess = OnSignalProcessEvent;

	if( ms > 0 )
	{
		unsigned long long now = GetTickMS();
		psi->timeout.ullExpireTime = now + ms;

		int ret = AddTimeout( co_get_curr_thread_env()->pEpoll->pTimeout,&psi->timeout,now );
		if( ret != 0 )
		{
			free(psi);
			return ret;
		}
	}
	AddTail( link, psi);

	co_yield_ct();

	RemoveFromLink<stCoCondItem_t,stCoCond_t>( psi );
	free(psi);

	return 0;
}

stCoCond_t *co_cond_alloc()
{
	return (stCoCond_t*)calloc( 1,sizeof(stCoCond_t) );
}

int co_cond_free( stCoCond_t * cc )
{
	free( cc );
	return 0;
}

stCoCondItem_t *co_cond_pop( stCoCond_t *link )
{
	stCoCondItem_t *p = link->head;
	if( p )
	{
		PopHead<stCoCondItem_t,stCoCond_t>( link );
	}
	return p;
}
