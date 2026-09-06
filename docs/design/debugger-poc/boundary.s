	.file	1 "hello.tks"
	.text

# boundary.s — F0.3 of docs/design/tdb-proposta-0.3.1.md (Fase 0, section 3.0): the third harness
# fixture, the one F0.1 (mini.s) and F0.2's assertion machine did not cover — a backtrace that
# crosses a PLATFORM LIBRARY frame in the MIDDLE of the stack, not just at the bottom (main's own
# libc entry, which mini.s/adv.s already show incidentally).
#
# Six frames, breakpoint in the deepest (`leaf`):
#   #0 leaf   (ours, hello.tks)      -- called BY the comparator libc invokes
#   #1 b      (ours, hello.tks)
#   #2 cmp    (ours, hello.tks)      -- the qsort(3) comparator callback
#   #3 qsort  (glibc, NO hello.tks)  -- THE BOUNDARY: a real platform-library frame, no Teko debug
#                                       info at all, sandwiched between two of our own frames
#   #4 a      (ours, hello.tks)      -- calls qsort with `cmp` as the comparator
#   #5 main   (ours, hello.tks)
#
# What this fixture exists to prove (the d0_1_harness.sh assertion): the unwind STOPS at the
# boundary with a correctly-named library frame instead of INVENTING a `hello.tks` frame for
# `qsort`'s own body, and RESUMES correctly on our side once qsort calls back into `cmp`. A
# debugger that misreads a foreign frame's prologue could either fabricate a bogus Teko frame or
# drop the chain below the boundary; either failure is silent and exactly the class of bug this
# fixture is built to catch before `tdb` ever exists to be blamed for it.

	.section .rodata
arr:
	.long	2
	.long	1

	.text
	.globl	leaf
	.type	leaf, @function
leaf:
	.loc 1 30
	movl	$0, %eax
	ret
	.size	leaf, .-leaf

	.globl	b
	.type	b, @function
b:
	.loc 1 41
	pushq	%rbp
	movq	%rsp, %rbp
	.loc 1 42
	call	leaf
	leave
	ret
	.size	b, .-b

# cmp — the qsort(3) comparator: `int cmp(const void *pa, const void *pb)`, SysV `%rdi`/`%rsi`.
# Called BY glibc's qsort, so this frame's caller is the boundary itself.
	.globl	cmp
	.type	cmp, @function
cmp:
	.loc 1 20
	pushq	%rbp
	movq	%rsp, %rbp
	.loc 1 21
	call	b
	.loc 1 22
	movl	(%rdi), %eax
	subl	(%rsi), %eax
	leave
	ret
	.size	cmp, .-cmp

# a — calls glibc's qsort(base, nmemb, size, cmp) over a two-element array, so qsort calls `cmp`
# at least once and the boundary frame is guaranteed to appear in `leaf`'s backtrace.
	.globl	a
	.type	a, @function
a:
	.loc 1 16
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$16, %rsp
	leaq	arr(%rip), %rdi
	movq	$2, %rsi
	movq	$4, %rdx
	leaq	cmp(%rip), %rcx
	.loc 1 17
	call	qsort@PLT
	leave
	ret
	.size	a, .-a

	.globl	main
	.type	main, @function
main:
	.loc 1 40
	pushq	%rbp
	movq	%rsp, %rbp
	.loc 1 41
	call	a
	xorl	%eax, %eax
	leave
	ret
	.size	main, .-main
