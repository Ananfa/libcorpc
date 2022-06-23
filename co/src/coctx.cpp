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

#include "coctx.h"
#include <string.h>

#if defined(__i386__)

//#define ESP 0
//#define EIP 1
//#define EAX 2
//#define ECX 3

//----- --------
// 32 bit
// | regs[0]: ret |
// | regs[1]: ebx |
// | regs[2]: ecx |
// | regs[3]: edx |
// | regs[4]: edi |
// | regs[5]: esi |
// | regs[6]: ebp |
// | regs[7]: eax |  = esp
enum
{
	kEIP = 0,
	kESP = 7,
};

#elif defined(__x86_64__)

//#define RSP 0
//#define RIP 1
//#define RBX 2
//#define RDI 3
//#define RSI 4
//
//#define RBP 5
//#define R12 6
//#define R13 7
//#define R14 8
//#define R15 9
//#define RDX 10
//#define RCX 11
//#define R8 12
//#define R9 13

// -----------
// 64 bit
//low | regs[0]: r15 |
//    | regs[1]: r14 |
//    | regs[2]: r13 |
//    | regs[3]: r12 |
//    | regs[4]: r9  |
//    | regs[5]: r8  | 
//    | regs[6]: rbp |
//    | regs[7]: rdi |
//    | regs[8]: rsi |
//    | regs[9]: ret |  //ret func addr
//    | regs[10]: rdx |
//    | regs[11]: rcx | 
//    | regs[12]: rbx |
//hig | regs[13]: rsp |
enum
{
	kRDI = 7,
	kRSI = 8,
	kRETAddr = 9,
	kRSP = 13,
};

#elif defined(__aarch64__)

// -----------
// 64bit
//low | regs[0]: x31(lr) |
//    | regs[1]: x30(sp) |
//    | regs[2]: x0 |
//    | regs[3]: x1 |
//----| regs[X]: x2 |
//----| regs[X]: x3 |
//----| regs[X]: x4 |
//----| regs[X]: x5 |
//----| regs[X]: x6 |
//----| regs[X]: x7 |
//    | regs[4]: x19 |
//    | regs[5]: x20 |
//    | regs[6]: x21 |
//    | regs[7]: x22 |
//    | regs[8]: x23 |
//    | regs[9]: x24 |
//    | regs[10]: x25 |
//    | regs[11]: x26 |
//    | regs[12]: x27 |
//    | regs[13]: x28 |
//hig | regs[14]: x29(fp) |
enum
{
	kLR = 0,
	kSP = 1,
	kX0 = 2,
	kX1 = 3,
	kFP = 14,
};

#endif

//64 bit
extern "C"
{
	extern void coctx_swap( coctx_t *,coctx_t* ) asm("coctx_swap");
};

int coctx_init( coctx_t *ctx )
{
	memset( ctx,0,sizeof(*ctx));
	return 0;
}

#if defined(__i386__)

int coctx_make( coctx_t *ctx,coctx_pfn_t pfn,const void *s,const void *s1 )
{
	//make room for coctx_param
	char *sp = ctx->ss_sp + ctx->ss_size - sizeof(coctx_param_t);
	sp = (char*)((unsigned long)sp & -16L);

	coctx_param_t* param = (coctx_param_t*)sp ;
	param->s1 = s;
	param->s2 = s1;

	memset(ctx->regs, 0, sizeof(ctx->regs));

	// 注意：这里sp减去一个指针大小的空间是为了设置返回地址，在coctx_swap汇编函数用ret方式返回时跳转到指定返回地址中
	//      目前因coctx_swap汇编函数中直接用了jmp指令跳转，所以没有设置返回地址。但这里还需要保留返回地址数据位置，因为后面接着是参数数据，不然解析参数就错位了
	ctx->regs[ kESP ] = (char*)(sp) - sizeof(void*);
	ctx->regs[ kEIP ] = (char*)pfn;

	return 0;
}

#elif defined(__x86_64__)

int coctx_make( coctx_t *ctx,coctx_pfn_t pfn,const void *s,const void *s1 )
{
	char *sp = ctx->ss_sp + ctx->ss_size;
	sp = (char*) ((unsigned long)sp & -16LL  );

	memset(ctx->regs, 0, sizeof(ctx->regs));

	// 注意：这里减8是一个指针大小空间，原先是用于设置返回地址，在coctx_swap汇编函数用ret方式返回时跳转到指定的返回地址中
	//      目前因coctx_swap汇编函数中直接用了jmp指令跳转，所以没有设置返回地址。这里还是保留返回地址的栈空间（兼容规范）
	ctx->regs[ kRSP ] = sp - 8;

	ctx->regs[ kRETAddr] = (char*)pfn;

	ctx->regs[ kRDI ] = (char*)s;
	ctx->regs[ kRSI ] = (char*)s1;
	return 0;
}

#elif defined(__aarch64__)

int coctx_make( coctx_t *ctx,coctx_pfn_t pfn,const void *s,const void *s1 )
{
	char *sp = ctx->ss_sp + ctx->ss_size/* - sizeof(coctx_param_t)*/;
	sp = (char*) ((unsigned long)sp & -16LL  );

	// 注意：这里不需要设置栈中的LR和FP字段，因为coctx_swap.S中用的是br x30方式跳转并且跳转位置取值是regs中的kLR
	//coctx_param_t* param = (coctx_param_t*)sp;
	//param->fp = (void*)sp;
	//param->lr = (void*)pfn;

	memset(ctx->regs, 0, sizeof(ctx->regs));

	ctx->regs[ kFP ] = sp/* - 16*/;
	ctx->regs[ kSP ] = sp/* - 16*/; // FP和LR在栈中的保留

	ctx->regs[ kLR] = (char*)pfn;

	// 注意：两个协程指针参数必须设置，切换时用于恢复参数，跳转到上面的pfn（CoRoutineFunc）时需要两个协程指针参数（其实这里s1应该可以不设置，因为CoRoutineFunc中只用一个协程指针）
	ctx->regs[ kX0 ] = (char*)s;
	ctx->regs[ kX1 ] = (char*)s1;
	return 0;
}

#endif

