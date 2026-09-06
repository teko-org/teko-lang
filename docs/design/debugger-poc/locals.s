# docs/design/debugger-poc/locals.s — the LOCALS/variables structural proof (D2.x).
#
# mini.s/multi.s proved a backtrace (function -> file:line). locals.s proves the NEXT layer: the
# DIE tree that lets a debugger NAME and LOCATE a function's parameters and locals. It carries the
# EXACT three-attribute shape src/backend/dwarf.tks emits (debug_abbrev_locals / debug_info_locals):
#
#   * DW_TAG_base_type   "i32" (DW_ATE_signed, byte_size 4) — the DW_AT_type target.
#   * DW_TAG_subprogram  "add" WITH children and DW_AT_frame_base = DW_OP_reg6 (rbp).
#   * DW_TAG_formal_parameter a / b, DW_TAG_variable s, each DW_AT_location = DW_OP_fbreg <sleb>.
#   * DW_TAG_lexical_block re-binding s (the shadow), with its own DW_OP_fbreg offset.
#
# THE FBREG OFFSETS AND THE FRAME BASE ARE PLACEHOLDERS. The code below happens to store the params
# at exactly -20/-24 and the local at -4 so the tree is self-consistent to read, but the WRITER does
# not know those numbers yet: the real offsets are compute_frame_layout's slots and the real frame
# base is the prologue-dependent register+offset, both wired in a later crumb (the codegen hook is
# FLAGGED, not taken). Until then `gdb print a` is not promised — the STRUCTURE is what this proves.
#
# The proof is gdb-INDEPENDENT: `readelf --debug-dump=info locals.o` decodes the whole tree. Run the
# assertions with d2_locals_harness.sh. There is no .debug_line here on purpose — the line table is
# multi.s's job; this file isolates the variable DIEs.
	.text
	.globl	add
	.type	add, @function
add:
	pushq	%rbp
	movq	%rsp, %rbp
	movl	%edi, -20(%rbp)
	movl	%esi, -24(%rbp)
	movl	-20(%rbp), %eax
	addl	-24(%rbp), %eax
	movl	%eax, -4(%rbp)
	movl	-4(%rbp), %eax
	popq	%rbp
	ret
	.size	add, .-add

	.section	.debug_abbrev,"",@progbits
	.byte	0x01, 0x11, 0x01
	.byte	0x25, 0x08
	.byte	0x13, 0x05
	.byte	0x03, 0x08
	.byte	0x1b, 0x08
	.byte	0x11, 0x01
	.byte	0x12, 0x07
	.byte	0x10, 0x17
	.byte	0x00, 0x00
	.byte	0x02, 0x24, 0x00
	.byte	0x03, 0x08
	.byte	0x3e, 0x0b
	.byte	0x0b, 0x0b
	.byte	0x00, 0x00
	.byte	0x03, 0x2e, 0x01
	.byte	0x03, 0x08
	.byte	0x3a, 0x0d
	.byte	0x3b, 0x05
	.byte	0x11, 0x01
	.byte	0x12, 0x07
	.byte	0x40, 0x18
	.byte	0x3f, 0x19
	.byte	0x00, 0x00
	.byte	0x04, 0x05, 0x00
	.byte	0x03, 0x08
	.byte	0x49, 0x13
	.byte	0x02, 0x18
	.byte	0x00, 0x00
	.byte	0x05, 0x34, 0x00
	.byte	0x03, 0x08
	.byte	0x49, 0x13
	.byte	0x02, 0x18
	.byte	0x00, 0x00
	.byte	0x06, 0x0b, 0x01
	.byte	0x00, 0x00
	.byte	0x00

	.section	.debug_info,"",@progbits
.Lcu_start:
	.long	.Lcu_end - .Lcu_after_len
.Lcu_after_len:
	.short	0x0004
	.long	0
	.byte	0x08
	.byte	0x01
	.ascii	"teko 0.3.1"
	.byte	0x00
	.short	0x000c
	.ascii	"hello.tks"
	.byte	0x00
	.ascii	"."
	.byte	0x00
	.quad	add
	.byte	0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	.long	0
.Ltype_i32:
	.byte	0x02
	.ascii	"i32"
	.byte	0x00
	.byte	0x05
	.byte	0x04
	.byte	0x03
	.ascii	"add"
	.byte	0x00
	.byte	0x01
	.short	29
	.quad	add
	.byte	0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	.byte	0x01
	.byte	0x56
	.byte	0x04
	.ascii	"a"
	.byte	0x00
	.long	.Ltype_i32 - .Lcu_start
	.byte	0x02
	.byte	0x91
	.sleb128	-20
	.byte	0x04
	.ascii	"b"
	.byte	0x00
	.long	.Ltype_i32 - .Lcu_start
	.byte	0x02
	.byte	0x91
	.sleb128	-24
	.byte	0x05
	.ascii	"s"
	.byte	0x00
	.long	.Ltype_i32 - .Lcu_start
	.byte	0x02
	.byte	0x91
	.sleb128	-4
	.byte	0x06
	.byte	0x05
	.ascii	"s"
	.byte	0x00
	.long	.Ltype_i32 - .Lcu_start
	.byte	0x02
	.byte	0x91
	.sleb128	-8
	.byte	0x00
	.byte	0x00
	.byte	0x00
.Lcu_end:
