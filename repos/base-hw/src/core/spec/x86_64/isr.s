.data
.global _isr_array
_isr_array:

.text

.macro _isr_entry
	.align 4, 0x90
1:	.data
	.quad 1b
	.previous
.endm

.macro _exception_with_code vector
	_isr_entry
	push $\vector
	jmp _dispatch_interrupt
.endm

.macro _exception vector
	_isr_entry
	push $0
	push $\vector
	jmp _dispatch_interrupt
.endm

/* interrupt dispatcher */
_dispatch_interrupt:
	push %r15
	push %r14
	push %r13
	push %r12
	push %r11
	push %r10
	push %r9
	push %r8
	push %rbp
	push %rsi
	push %rdi
	push %rdx
	push %rcx
	push %rbx
	push %rax
	mov %cr2, %rdi
	push %rdi
	mov %rsp, %rdi
	call dump_interrupt_info

_exception				0
_exception				1
_exception				2
_exception				3
_exception				4
_exception				5
_exception				6
_exception				7
_exception_with_code 	8
_exception				9
_exception_with_code	10
_exception_with_code	11
_exception_with_code	12
_exception_with_code	13
_exception_with_code	14
_exception				15
_exception				16
_exception_with_code	17
_exception				18
_exception				19

.set vec, 20
.rept 236
_exception				vec
.set vec, vec + 1
.endr
