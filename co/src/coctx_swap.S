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

.globl coctx_swap
#if !defined( __APPLE__ ) && !defined( __FreeBSD__ )
.type  coctx_swap, @function
#endif
coctx_swap:

#if defined(__i386__)
	leal 4(%esp), %edx // sp
	movl 4(%esp), %eax // param1
	movl %edx, 28(%eax)
	movl %ebp, 24(%eax)
	movl %esi, 20(%eax)
	movl %edi, 16(%eax)
	movl %ebx, 4(%eax)
	movl 0(%esp), %edx // ret func addr
	movl %edx, 0(%eax)
	
	movl 8(%esp), %eax // param2
	movl 4(%eax), %ebx
	movl 16(%eax), %edi
	movl 20(%eax), %esi
	movl 24(%eax), %ebp
	movl 28(%eax), %esp

    jmp *(%eax)

#elif defined(__x86_64__)
	leaq 8(%rsp), %rax       // rsp + 8 -> rax
    movq 0(%rsp), %rdx
	movq %rax, 104(%rdi)
	movq %rbx, 96(%rdi)
	movq %rdx, 72(%rdi)     // ret func addr
	movq %rbp, 48(%rdi)
	movq %r12, 24(%rdi)
	movq %r13, 16(%rdi)
	movq %r14, 8(%rdi)
	movq %r15, 0(%rdi)
	movq %rsi, 64(%rdi)     // param
	movq %rdi, 56(%rdi)     // param
	
	movq 0(%rsi), %r15
	movq 8(%rsi), %r14
	movq 16(%rsi), %r13
	movq 24(%rsi), %r12
	movq 48(%rsi), %rbp
	movq 72(%rsi), %rax     // ret func addr
	movq 96(%rsi), %rbx
	movq 104(%rsi), %rsp
	movq 56(%rsi), %rdi     // param
	movq 64(%rsi), %rsi     // param

    jmp *%rax

#endif
