	.file	1 "hello.tks"
	.text

# lvl4: FRAMELESS LEAF — exactly what frame_is_framed_x86 == false emits: no prologue at all.
	.globl	lvl4
	.type	lvl4, @function
lvl4:
	.loc 1 30
	movl	$7, %eax
	ret
	.size	lvl4, .-lvl4

# lvl3: FRAMED with callee-saved pushes AFTER `mov rbp,rsp`, then `sub rsp` — our exact ordering.
	.globl	lvl3
	.type	lvl3, @function
lvl3:
	.loc 1 41
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	%rbx
	pushq	%r12
	subq	$32, %rsp
	.loc 1 42
	call	lvl4
	movl	%eax, -4(%rbp)
	.loc 1 43
	movl	-4(%rbp), %eax
	addq	$32, %rsp
	popq	%r12
	popq	%rbx
	leave
	ret
	.size	lvl3, .-lvl3

# lvl2: FRAMELESS *CALLER* — the only theoretical hole: calls but never returns
# (func_any_ret_x86 false -> call_align false -> frameless). rsp untouched by us.
	.globl	lvl2
	.type	lvl2, @function
lvl2:
	.loc 1 20
	call	lvl3
	.loc 1 21
	movl	%eax, %edi
	call	exitwrap
	hlt
	.size	lvl2, .-lvl2

	.globl	exitwrap
	.type	exitwrap, @function
exitwrap:
	.loc 1 12
	pushq	%rbp
	movq	%rsp, %rbp
	call	exit
	.size	exitwrap, .-exitwrap

# lvl1: FRAMED, zero saved regs, zero slots, framed only by call_align.
	.globl	lvl1
	.type	lvl1, @function
lvl1:
	.loc 1 16
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$0, %rsp
	.loc 1 17
	call	lvl2
	leave
	ret
	.size	lvl1, .-lvl1

	.globl	main
	.type	main, @function
main:
	.loc 1 40
	pushq	%rbp
	movq	%rsp, %rbp
	.loc 1 41
	call	lvl1
	.loc 1 42
	xorl	%eax, %eax
	leave
	ret
	.size	main, .-main
