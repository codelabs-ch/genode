/*
 * \brief  Transition between kernel/userland, and secure/non-secure world
 * \author Martin Stein
 * \author Stefan Kalkowski
 * \date   2011-11-15
 */

/*
 * Copyright (C) 2011-2013 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

/***************************************************
 ** Constant values that are pretty commonly used **
 ***************************************************/

/* alignment constraints */
.set MIN_PAGE_SIZE_LOG2, 12

/* size of pointer to CPU context */
.set CONTEXT_PTR_SIZE, 1 * 8

/* globally mapped buffer storage */
.set BUFFER_SIZE, 6 * 8

/* offsets of the member variables in a CPU context */
.set SP_OFFSET,     1 * 8
.set R8_OFFSET,     2 * 8
.set RAX_OFFSET,   10 * 8
.set SS_OFFSET,    17 * 8
.set FLAGS_OFFSET, 18 * 8
.set CR3_OFFSET,   20 * 8

.section .text
	/* space for a copy of the kernel context */
	.p2align 2
	.global _mt_master_context_begin
	_mt_master_context_begin:

	/* space must be at least as large as 'Cpu_state' */
	.space 21*8

	.global _mt_master_context_end
	_mt_master_context_end:

	/* space for a client context-pointer per CPU */
	.p2align 2
	.global _mt_client_context_ptr
	_mt_client_context_ptr:
	.space CONTEXT_PTR_SIZE

	/* a globally mapped buffer per CPU */
	.p2align 2
	.global _mt_buffer
	_mt_buffer:
	.space BUFFER_SIZE

	/*
	 * Page aligned base of mode transition code.
	 *
	 * This position independent code switches between a kernel context and a
	 * user context and thereby between their address spaces. Due to the latter
	 * it must be mapped executable to the same region in every address space.
	 * To enable such switching, the kernel context must be stored within this
	 * region, thus one should map it solely accessable for privileged modes.
	 */
	.p2align MIN_PAGE_SIZE_LOG2
	.global _mt_begin
	_mt_begin:
	1: jmp 1b

	/*
	 * On user exceptions the CPU has to jump to one of the following
	 * seven entry vectors to switch to a kernel context.
	 */
	.global _mt_kernel_entry_pic
	_mt_kernel_entry_pic:
	1: jmp 1b

	.global _mt_user_entry_pic
	_mt_user_entry_pic:

	/* Prepare stack frame in mt buffer (Intel SDM Vol. 3A, figure 6-8) */
	mov _mt_client_context_ptr, %rax
	mov $_mt_buffer+BUFFER_SIZE, %rsp
	pushq $0x23
	pushq SP_OFFSET(%rax)

	/* Set I/O privilege level to 3 */
	orq $0x3000, FLAGS_OFFSET(%rax)
	btrq $9, FLAGS_OFFSET(%rax) /* XXX: Drop once interrupt handling is done */
	pushq FLAGS_OFFSET(%rax)

	pushq $0x1b
	pushq (%rax)

	/* Restore segment registers */
	mov $0x23, %rbx
	mov %rbx, %ds
	mov %rbx, %es
	mov %rbx, %fs
	mov %rbx, %gs

	1: jmp 1b

	.global _mon_kernel_entry
	_mon_kernel_entry:
	1: jmp 1b

	.global _mt_vm_entry_pic
	_mt_vm_entry_pic:
	1: jmp 1b

	/* end of the mode transition code */
	.global _mt_end
	_mt_end:
	1: jmp 1b
