# B2 / N4 — riscv64 (RV64GC/LP64D) + ELF object emission (crumb plan)

**Status:** DESIGN (doc-only). Sub-PR of the 0.3 own-AOT-backend wave. Issue **#387**. Mini-umbrella
`fix/issue-387-riscv64` (#401; base = the umbrella `remodel/backend-build` carrying A1→A4 + #443 + B1,
seed `teko 0.3.0.9-beta`). Base for the design: `docs/design/backend-b1-x8664.md` (the proven
crumb/encoder/object/differential pattern this MIRRORS), `docs/design/own-backend-architecture.md`
§2.1 (target #4, riscv64 LP64D ELF) + §3.3/§3.4/§3.5, grounded in the merged code
(`src/backend/{minst,minst_x86,isel_arm64,isel_x86_64,regalloc,regalloc_x86,abi_aapcs64,abi_sysv64,
encode_arm64,encode_x86_64,objfile_macho,objfile_elf,minst_interp}.tks`, `src/build/project.tks`,
`scripts/diff_c_own.sh`, `.github/workflows/native.yml`).

> This is a PLAN. It brings the **THIRD ISA** into the own AOT backend: riscv64 RV64G on Linux ELF.
> It is the second reuse of the target-independent core (B1 was the first), and the FIRST reuse of a
> whole object writer — the B1 ELF writer is REUSED WHOLESALE (only `e_machine`, `e_flags` and the
> RISC-V relocation types differ), which is the headline B2 saving. It does NOT retire the C backend,
> does NOT add the `--backend`/`--target` flags (D1/#390 owns them — B2 extends the temporary env
> seam with `TEKO_TARGET=riscv64-linux`), and does NOT build the own linker (Phase E/#226 — B2 links
> via `riscv64-linux-gnu-gcc`-as-linker on CI). The hard tail (F/D float, data globals, argv
> forwarding, 64-bit immediate/frame materialization) is scoped forward as **named** honest-stops for
> 0.3.1, mirroring A4/B1's discipline exactly.

---

## 0. TL;DR — the recommended shape

- **RISC-V is the EASY ISA where it counts — three-address, load-store, fixed-32-bit — so B2 reuses
  the arm64 SHAPES, not the x86 ones.** RISC-V `add rd, rs1, rs2` is three-address (like AArch64,
  UNLIKE x86's destructive two-address), so isel needs NO copy-at-def hack (B1-3's cost vanishes);
  `div`/`rem`/`divu`/`remu` are NATIVE M-extension instructions (SIMPLER than both x86's fixed-reg
  `idiv` macro AND arm64's `SDIV`+`MSUB` remainder — RISC-V's `rem` is one instruction); the encoding
  is FIXED 32-bit (like arm64's `[]u32` word model, UNLIKE x86's variable-length), so the encoder +
  branch-fixup mirror A4/arm64's word-patch, not B1's byte-granular patch. The one place RISC-V is
  HARDER than x86 is the **bit-permuted B/J branch immediates** and the **two-instruction PC-relative
  address pair** (`auipc`+`addi` with a PAIRED `PCREL_HI20`/`PCREL_LO12_I` relocation) — the two
  RISC-V-specific sharp details, §6.3 + §7.2.
- **Object writer — REUSED WHOLESALE via a minimal, behavior-preserving generalization (the flagged
  tension, resolved law-first, NO HALT).** The B1 `objfile_elf.tks` writer is already 90% ISA-agnostic
  (it consumes byte images + a neutral `Symbol` list + relocations). B2 extracts its body into a
  neutral `emit_elf_object(ElfObject) -> []byte` core parameterized on **three** facts —
  `e_machine`, `e_flags`, and the ALREADY-RESOLVED numeric relocation type — plus one structural
  addition (emit file-local `.text` label symbols, which x86 never produces so B1's bytes are
  unchanged). B1's `emit_elf` becomes a 5-line adapter (`ElfObject{e_machine=62,e_flags=0,…}` →
  `emit_elf_object`) whose byte output is **byte-identical** to today's (B1's goldens are the
  guardrail); B2's `emit_elf_riscv` is the symmetric adapter (`e_machine=243`, `e_flags=0x0004`). The
  RISC-V-specific PAIR-reloc + label-synthesis complexity lives entirely in the RISC-V ENCODER, never
  in the writer — so the writer stays ISA-agnostic and E1 (the ELF static linker) still reuses it.
  Parallel-writer duplication is REJECTED (it contradicts the reuse-wholesale mandate and M.5). (§7.)
- **Sub-slice order (each independently gate-able, mirroring B1's eight):**
  `B2-1` abi-riscv64 + regalloc-core-neutrality proof → `B2-2` `minst_riscv` IR + printer →
  `B2-3` isel-riscv (three-address ALU + native div/rem + fused compare-branch + the `auipc`+`addi`
  address pair + LP64D args) → `B2-4` regalloc-riscv shell (reuse the neutral scan core) →
  `B2-5` encoder core (6 formats R/I/S/B/U/J + the bit-scrambled B/J immediates + assembler-verified
  goldens) → `B2-6` frame + fixed-32 word-patch branch fixup + the CALL / PCREL_HI20+LO12 relocs →
  `B2-7` **ELF writer REUSE** (generalize `emit_elf` → `emit_elf_object`; `emit_elf_riscv` adapter) →
  `B2-8` target dispatch (`TEKO_TARGET=riscv64-linux`) + the qemu-riscv64 C-vs-own differential lane
  (**#387 CLOSES here**).
- **Differential placement — CI runs under QEMU, macOS byte-tests.** The host is macOS-arm64 (cannot
  run riscv64 ELF), so the executing `C-riscv == own-riscv` differential runs on the linux-x86_64 CI
  runner under `qemu-riscv64-static` (the existing `riscv64-qemu` lane, re-scoped to a FAST tiny-corpus
  diff instead of the disabled full `test --coverage` run that was 8-9m). Locally on macOS the encoder
  goldens (byte-vectors, assembler-cross-checked with `clang --target=riscv64-linux-gnu -march=rv64g`)
  run machine-free, `llvm-readobj`/`readelf` round-trips the emitted `.o` cross-format, and execution
  honest-skips.
- **No genuine HALT.** Every decision resolves law-first and is recorded (§10). The one flagged
  tension — the `emit_elf` generalization — is CLEAN (three params + one neutral writer gap-fill,
  B1's byte output preserved), so it does not HALT. The residual DRY tensions (per-ISA interval-walk
  mirroring, per-ISA word-encoder shell) are the SAME per-ISA mirroring `isel`/`encode` already embody
  and are REPORTED up (§10 R-1/R-2), not turned into blockers.

---

## 1. The assessed starting point — what B2 REUSES vs what it PARALLELS

B2 is the SECOND proof the A-slice core is target-independent, and the FIRST proof a whole object
format writer is target-agnostic. The grounding audit, file by file:

### 1.1 REUSED VERBATIM (ISA-neutral — no copy, no change; same `teko::backend` namespace)

| Symbol(s) | Home | Why neutral |
|---|---|---|
| `MRegClass = enum { GPR; FPR }` | `minst.tks:11` | RISC-V x-regs + f-regs map onto GPR/FPR |
| `MReg { id; reg_class; is_phys }`, `vreg`, `preg` | `minst.tks:22,874,886` | an abstract register id; RISC-V x0..x31/f0..f31 fit |
| `MMem { base; offset }` | `minst.tks:32` | base+disp12; RISC-V has NO index*scale form at all — base+imm12 is the ONLY addressing mode |
| `MCond = enum { EQ;NE;LT;LE;GT;GE;LO;LS;HI;HS }` | `minst.tks:52` | abstract condition codes → RISC-V `beq/bne/blt/bge/bltu/bgeu` (GT/LE/HI/LS via operand swap, §4.4) |
| `AbiDescriptor` + `arg_reg`/`allocatable_pool`/`is_caller_saved`/`is_callee_saved`/`spill_scratch` | `abi_aapcs64.tks:14,170,187,199,214,231` | pure register-file descriptor; B2 clones the PARTITION only |
| `LiveInterval`/`IntervalSet`/`ScanResult`/`RegAssignment`/`InReg`/`Spilled`/`AssignLookup`/`SubstEntry` | `regalloc.tks:449,484,936,902,919,1291,1383` | register-abstract; never mention an `MInst` case |
| `linear_scan`/`candidate_pool`/`assign_lookup` | `regalloc.tks:1245,1110,1361` | operate on `IntervalSet`, not `MInst` — the HARD algorithm, single-sourced |
| `Symbol { name; defined; local; sect; offset }` | `encode_arm64.tks:1787` | a generic object-symbol record; ELF binds it the same way (B2 uses `sect=1` locals for pcrel labels, §7.2) |
| `twos_field` | `encode_arm64.tks:1581` (non-pub, same-ns) | the negative→two's-complement field encoder the B/J scramble reuses (§6.3) — NEVER casts a negative through `to u32` |
| `emit_u32_le` | `encode_arm64.tks:175` (pub) | the fixed-32 word / LE-field emit primitive |
| `emit_u64_le`/`emit_str_bytes`/`pad_to_mult`/`build_strtab` | `objfile_macho.tks:11,31,83,119` | byte-table helpers (same-ns visible) |
| `align_up` | `encode_arm64.tks:804` (non-pub, same-ns) | offset rounding |
| **the WHOLE ELF writer BODY** — `build_elf_strtab`/`ElfStrtab`/`ElfSym`/`elf_first_global_index`/`elf_symbol_index`/`ElfLayout`/`compute_elf_layout`/`emit_elf_shdr(s)`/`emit_elf_one_sym`/`emit_elf_symtab`/`emit_elf_relas` | `objfile_elf.tks` | **REUSED** — B2-7 extracts the neutral `emit_elf_object` core these already compose; only `e_machine`/`e_flags`/reloc-type differ (§7) |

> **Namespace fact (load-bearing, carried from B1).** `pub` = cross-NAMESPACE export; a bare `fn` =
> namespace-private (visible to every file in `teko::backend`). B2's new files live in the SAME
> `teko::backend` namespace, so they reuse all of the above with **no `use` and no re-export** — and,
> per the silent-bare-name-collision gotcha (no diagnostic), they MUST NOT re-declare any of these
> names. Every parallel RISC-V type/fn therefore carries a `Riscv`/`_riscv` suffix (§1.3).

### 1.2 arm64/x86-SPECIFIC — the parts B2 PARALLELS (never reuses, never generalizes)

| Symbol(s) | Home | Why not reusable |
|---|---|---|
| the 29 `MInst` cases + the 20 `MInstX86` cases | `minst.tks`/`minst_x86.tks` | per-ISA mnemonics (`MAdrp`/`MMovK`; `MLeaRipX86`/`MDivSeqX86`) |
| `MAluOp`/`MRelocKind`/`MAluOpX86`/`RelocKindX86` | `minst.tks:62,42`/`minst_x86.tks:13,47` | AArch64 `{PageHi;PageLo;Call}` / x86 `{Plt32;Pc32;Abs64}` relocs |
| `EncWord`/`Reloc`/`FrameLayout` (arm64) ; `EncInstX86`/`RelocX86`/`FrameLayoutX86` (x86) | `encode_arm64.tks`/`encode_x86_64.tks` | `EncWord.reloc_kind: MRelocKind` is arm64-typed; `EncInstX86` is byte-granular — B2 needs a fixed-32 word model with RISC-V reloc kinds |
| `select_module`/`select_module_x86` | `isel_arm64.tks:1866`/`isel_x86_64.tks` | per-ISA selection |
| `inst_regs`/`inst_regs_x86` + the `MFunc`/`MFuncX86`-walking `regalloc_*` shells | `regalloc.tks`/`regalloc_x86.tks` | total matches over the per-ISA instruction variant |
| `minst_interp` | `minst_interp.tks` | total match over arm64 `MInst` semantics |
| `emit_macho` | `objfile_macho.tks` | Mach-O container (B2 targets ELF) |

### 1.3 The naming convention (silent-collision-safe)

Every parallel RISC-V declaration lives in `teko::backend` with an explicit suffix so it CANNOT
collide with the frozen arm64/x86 names. The set: `MInstRiscv` (variant) with per-case structs
`MAluRRiscv`/`MAluIRiscv`/`MLiRiscv`/`MLuiRiscv`/`MAuipcRiscv`/`MLoadRiscv`/`MStoreRiscv`/
`MSltRiscv`/`MBranchRiscv`/`MJalRiscv`/`MJalrRiscv`/`MCallRiscv`/`MCallIndRiscv`/`MLlaRiscv`/
`MFrameAddrRiscv`/`MRetRiscv` ; `MAluOpRiscv`/`RelocKindRiscv` ; `MFuncRiscv`/`MBlockRiscv`/
`MModuleRiscv` ; `select_module_riscv` ; `inst_regs_riscv`/`map_minst_riscv`/`rewrite_inst_riscv`/
`regalloc_func_riscv`/`regalloc_module_riscv` ; `encode_module_riscv`/`EncWordRiscv`/`RelocRiscv`/
`EncodedFuncRiscv`/`EncodedModuleRiscv`/`FrameLayoutRiscv`/`BlockOffsetRiscv`/`BranchFixupRiscv` ;
`emit_elf_riscv`/`riscv_reloc_type`. The neutral reuse (`MReg`, `linear_scan`, `Symbol`,
`twos_field`, `emit_u32_le`, the ELF writer body, `emit_elf_object`) stays bare/`pub`.

---

## 2. The IR decision — parallel `minst_riscv` (law-first, NO HALT — settled by B1's D-C)

B1 already settled this (its D-C): a parallel per-ISA IR, not a generalized `MInst`. B2 inherits the
ruling verbatim. **Chosen: a parallel `minst_riscv.tks` IR.** Generalizing `MInst` to span three ISAs
would churn the FIVE frozen total-matches (`minst_interp`, `encode_inst_word`, `encode_inst_x86`,
`inst_regs`, `inst_regs_x86`) and risk the two shipped differentials (A4 + B1) — the opposite of
smallest-blast (M.1). The parallel IR touches ZERO frozen code (it only ADDS files + reuses the
neutral surface), and it is exactly the architecture-of-record's per-ISA split
(`own-backend-architecture.md` §3.3). Recorded (§10 D-C), not open.

---

## 3. `abi_riscv64.tks` — the LP64D register file (B2-1)

The whole cost of a new ABI is cloning `AbiDescriptor`'s partition. **RISC-V LP64D:** integer args
`a0..a7` (`x10..x17`), integer result `a0`/`a1`, callee-saved `sp`/`s0..s11` (`x2`, `x8..x9`,
`x18..x27`) + `ra` (`x1`, special), FP args `fa0..fa7` (`f10..f17`), FP result `fa0`, FP callee-saved
`fs0..fs11`. Register-id convention: use the **RISC-V register numbers directly** — GPR `x0=0..x31=31`,
FPR `f0=0..f31=31` — so the encoder's `rd`/`rs1`/`rs2` fields are the id (mirrors arm64's
`enc_reg(r)=r.id`).

> **Decision D-A (recorded).** UNLIKE x86 (whose result `RAX` differs from arg0 `RDI`, B1's D-A),
> RISC-V's integer result `a0` **IS** `gpr_arg[0]` (like AAPCS64's `x0`). So B2 needs **no** result-pin
> divergence — `arg_reg(riscv64_lp64d(), GPR, 0)` doubles as the result register, and isel pins it at
> the return/`ret_reg` site the same way arm64 does. RISC-V is the SIMPLER ABI here.

**Reserved (never allocated):** `x0` (the hardwired zero register — used explicitly for `li`/`neg`/
`jal x0`/`ret`, never assigned), `x1` (`ra` — reserved for the call/link register), `x2` (`sp`),
`x3` (`gp`), `x4` (`tp`), `x8` (`s0/fp` — the frame pointer), plus the spill scratch.

**Spill scratch.** Reserve `t4,t5,t6` (`x29,x30,x31`) as `gpr_spill_scratch` — all caller-saved
temporaries (reserving them costs no prologue save), sized to the max simultaneous spilled operands of
one instruction (an R-type with both sources AND the def spilled = two source reloads + one def
scratch, mirroring arm64's three-scratch `MMSub` rationale). `fpr_spill_scratch` = `ft10,ft11`
(`f30,f31`). `spill_slot_bytes`/`spill_slot_align` = 8.

```teko
/**
 * riscv64_lp64d — the concrete RISC-V RV64G LP64D (riscv64 Linux/ELF) register-
 * file descriptor: a0..a7 (x10..x17) integer arguments (index 0 = a0, which is
 * ALSO the integer result — D-A, unlike x86), fa0..fa7 (f10..f17) float
 * arguments (index 0 = fa0, the float result), s0..s11 (x8..x9, x18..x27)
 * callee-saved plus ra (x1) and sp (x2), everything else caller-saved. The
 * allocatable pools list caller-saved first (scratch-cheap short values before a
 * callee-save prologue spill), excluding x0(zero)/x1(ra)/x2(sp)/x3(gp)/x4(tp)/
 * x8(fp) — the hardwired/reserved/frame registers — and the reserved spill
 * scratch t4/t5/t6 (GPR) / ft10/ft11 (FPR). Register ids are the RISC-V register
 * numbers (x0..x31, f0..f31), so the encoder's rd/rs1/rs2 fields are the id
 * directly. Clones aapcs64's SHAPE with an LP64D partition — the whole cost of a
 * new ABI (Phase B, §2.2 of the architecture doc).
 *
 * @return AbiDescriptor  the RISC-V LP64D register file
 */
pub fn riscv64_lp64d() -> AbiDescriptor {
    AbiDescriptor {
        gpr_arg = riscv_gpr_arg_seq()
        fpr_arg = push_range(teko::list::empty(), 10, 17)
        gpr_allocatable = riscv_gpr_allocatable()
        fpr_allocatable = riscv_fpr_allocatable()
        gpr_caller_saved = riscv_gpr_caller_saved()
        fpr_caller_saved = riscv_fpr_caller_saved()
        gpr_spill_scratch = push_range(teko::list::empty(), 29, 31)
        fpr_spill_scratch = push_range(teko::list::empty(), 30, 31)
        spill_slot_bytes = 8
        spill_slot_align = 8
    }
}
```

The pool helpers mirror `abi_sysv64.tks`'s `sysv_gpr_arg_seq`/`sysv_gpr_allocatable`/
`sysv_gpr_caller_saved` shape exactly:
- `riscv_gpr_arg_seq()` → `[10,11,12,13,14,15,16,17]` (a0..a7; a0 doubles as result).
- `riscv_gpr_allocatable()` → caller-saved first `t0..t2` (5,6,7), `a0..a7` (10..17), `t3` (28) —
  then callee-saved `s1` (9), `s2..s11` (18..27). Excludes t4..t6 (spill scratch), x0..x4, x8 (fp).
- `riscv_gpr_caller_saved()` → `ra`(1), `t0..t2`(5,6,7), `a0..a7`(10..17), `t3..t6`(28..31) — the
  full call-clobber set (INCLUDING the spill scratch, which is caller-saved too).
- `riscv_fpr_allocatable()` → `ft0..ft7`(0..7), `fa0..fa7`(10..17), `ft8..ft9`(28,29) — then
  `fs0..fs1`(8,9), `fs2..fs11`(18..27). Excludes ft10/ft11 (spill scratch).
- `riscv_fpr_caller_saved()` → `ft0..ft7`(0..7), `fa0..fa7`(10..17), `ft8..ft11`(28..31).

**Regalloc-core-neutrality proof (the B2-1 gate).** B2-1 ships `abi_riscv64.tks` + a unit test that
drives the SHARED `linear_scan` over a hand-built `IntervalSet` under `riscv64_lp64d()` and asserts:
(a) the allocatable order is caller-saved-first (`t0` before `s1`); (b) a call-crossing interval
avoids caller-saved (`a0`/`t0`/…) and lands on a callee-saved (`s1`/`s2`/…); (c)
`is_callee_saved(riscv64_lp64d(), preg(9/*s1*/,GPR))` is true and `preg(10/*a0*/,GPR)` is false; (d)
`x0`/`ra`/`sp`/`fp` are never returned by the pool. This proves the core carries no AAPCS64/SysV
assumption BEFORE any RISC-V instruction exists — the smallest possible first step.

---

## 4. `isel_riscv.tks` — selection (B2-3), the arm64-shaped three-address path

RISC-V is three-address load-store like AArch64, so `isel_riscv.tks` mirrors `isel_arm64.tks`'s
structure — NOT `isel_x86_64.tks`'s two-address structure. It reuses the neutral `SelCtx` shape
(rename `SelCtxRiscv`), the `VInfoTable` `class_of` side-table (neutral, `isel_arm64.tks:31`), the
RPO block walk with #443 reachability, and a total per-case walk over the LIR.

### 4.1 The `MInstRiscv` alphabet (declared in `minst_riscv.tks`, B2-2)

The load-bearing subset for the integer/control/memory corpus (the F/D family is the `B2-fp` stop):

| LIR | `MInstRiscv` case | RV64 form |
|---|---|---|
| `LConstInt` (small) | `MLiRiscv { dst; imm }` | `addi rd, x0, imm12` (fits ±2048) |
| `LConstInt` (32-bit) | `MLuiRiscv`+`MAluIRiscv` | `lui rd, hi20` ; `addi rd, rd, lo12` |
| `LConstInt` (64-bit) | — | **named honest-stop `B2-bigimm`** (multi-instruction `li`; §9) |
| `LBin` add/sub/and/or/xor/shl/shr | `MAluRRiscv { op; dst; a; b; wide }` | three-address `add/sub/and/or/xor/sll/srl/sra rd, rs1, rs2` (or the `*w` RV64 32-bit form when `!wide`, §4.2) |
| `LBin` mul | `MAluRRiscv`(Mul) | `mul rd, rs1, rs2` (M-ext, ONE instruction — §4.3) |
| `LBin` sdiv/udiv/srem/urem | `MAluRRiscv`(Div/Divu/Rem/Remu) | `div/divu/rem/remu rd, rs1, rs2` (M-ext, ONE instruction each — §4.3) |
| `LBin` compare (value) | `MSltRiscv` (+ fixups) | `slt/sltu` (+ `xori`/`sltiu`/`sub` per condition — §4.4) |
| `LBin` compare (fused) | `MBranchRiscv { cond; a; b; target }` | `beq/bne/blt/bge/bltu/bgeu rs1, rs2, block` — ONE instruction (§4.4) |
| `INeg`/`INot` | `MAluRRiscv`(Sub, rs1=x0) / `MAluIRiscv`(Xori, −1) | `neg rd, rs = sub rd, x0, rs` ; `not rd, rs = xori rd, rs, -1` |
| `LLoad`/`LStore` | `MLoadRiscv`/`MStoreRiscv { size; dst/src; mem }` | `ld/lw/lh/lb/lbu/lhu/lwu rd, off(rs1)` / `sd/sw/sh/sb rs2, off(rs1)` |
| `LAlloca` | `MFrameAddrRiscv { dst; slot }` | `addi rd, sp, off` (resolved via `FrameLayoutRiscv`) |
| `LGlobalAddr`/`LFuncAddr` (value) | `MLlaRiscv { dst; sym }` | **TWO** insns `auipc rd,%pcrel_hi ; addi rd,rd,%pcrel_lo` + the PAIR reloc (§4.5, §7.2) |
| `LJump` | `MJalRiscv { target }` | `jal x0, block` (J-type, link discarded — §6.3) |
| `LCall` | `MCallRiscv { sym; uses; ret_reg; has_result; variadic }` | `auipc ra,0 ; jalr ra,0(ra)` + ONE `R_RISCV_CALL_PLT` reloc (§4.5) |
| `LCallIndirect` | `MCallIndRiscv { target; uses; ret_reg; has_result }` | `jalr ra, 0(rs)` |
| `LRet` | `MRetRiscv { }` | epilogue + `jalr x0, 0(ra)` (`ret`; value pre-pinned to a0/fa0) |
| `IToF`/`FToI`/`FToF`/float ALU/`FNeg` | — | **named honest-stop `B2-fp`** (F/D extension, 0.3.1) |

### 4.2 Three-address ALU — NO copy (the arm64 shape, not the x86 one)

`select_alu_riscv` mirrors `isel_arm64.tks:599`'s `select_alu` ONE-instruction shape verbatim — the
copy-at-def B1-3 needed for x86's destructive two-address is simply absent. The only RISC-V twist is
the 32-bit width form: where arm64 flips `sf_bit`, RV64 picks a DIFFERENT opcode (`OP-32`=0x3B
`addw/subw/sllw/srlw/sraw/mulw/divw/divuw/remw/remuw`) for the 32-bit result, so `wide` selects the
base (`OP`=0x33) vs the `*w` form at ENCODE time (the `MAluRRiscv` carries `wide`).

```teko
/**
 * select_alu_riscv — the register-register `MAluR` selection shared by every
 * integer arithmetic/bitwise/shift/mul/div/rem `LBinOp` case. RISC-V is
 * three-address (`op rd, rs1, rs2`), so — UNLIKE x86 (B1-3's copy-at-def) — the
 * result register is written directly with NO preparatory copy; this is the
 * arm64 select_alu shape (isel_arm64.tks:599), not the x86 one. `wide` selects
 * the RV64 width at encode (the base OP form for 64-bit, the OP-32 `*w` form for
 * 32-bit), so it is recorded on the instruction (not resolved here).
 *
 * @param SelCtxRiscv ctx0  the context to extend
 * @param lir::LInst inst  the enclosing instruction (its result VReg)
 * @param MAluOpRiscv op  which RISC-V ALU mnemonic to emit
 * @param u32 a  the first-operand VReg (rs1)
 * @param u32 b  the second-operand VReg (rs2)
 * @param bool wide  true for the 64-bit base form, false for the 32-bit `*w` form
 * @return SelCtxRiscv  the advanced context
 */
fn select_alu_riscv(ctx0: SelCtxRiscv, inst: lir::LInst, op: MAluOpRiscv, a: u32, b: u32, wide: bool) -> SelCtxRiscv {
    let dst = vreg(inst.result, MRegClass::GPR)
    let ctx1 = selctx_riscv_set_vinfo(ctx0, inst.result, MRegClass::GPR, wide)
    selctx_riscv_emit(ctx1, alu_r_riscv(op, wide, dst, vreg(a, MRegClass::GPR), vreg(b, MRegClass::GPR)))
}
```

### 4.3 Division/remainder COLLAPSES to one instruction (RISC-V's biggest simplification)

x86 needs an `MDivSeqX86` fixed-reg `RAX:RDX` macro (B1 §4.4, modelled as a call); arm64 needs
`SDIV`+`MSUB` for the remainder (`isel_arm64.tks:619`, `select_rem`). RISC-V has NATIVE M-extension
`div`/`divu`/`rem`/`remu rd, rs1, rs2` — every one is a **single** three-address instruction with NO
fixed register and NO clobber. So `select_rem_riscv` is just `select_alu_riscv` with `Rem`/`Remu`:

```teko
/**
 * select_rem_riscv — `IRemS`/`IRemU`'s selection: a SINGLE native `rem`/`remu`
 * instruction. RISC-V's M-extension provides remainder directly (unlike arm64,
 * which synthesizes it as SDIV+MSUB — select_rem at isel_arm64.tks:619 — and
 * unlike x86's fixed-reg idiv sequence), so this is the same one-instruction path
 * as any other ALU op. No scratch register, no clobber, no call model.
 *
 * @param SelCtxRiscv ctx0  the context to extend
 * @param lir::LInst inst  the enclosing instruction (its result VReg)
 * @param MAluOpRiscv op  `Rem` (signed) or `Remu` (unsigned)
 * @param u32 a  the dividend VReg (rs1)
 * @param u32 b  the divisor VReg (rs2)
 * @param bool wide  true for the 64-bit `rem`, false for the 32-bit `remw`
 * @return SelCtxRiscv  the advanced context
 */
fn select_rem_riscv(ctx0: SelCtxRiscv, inst: lir::LInst, op: MAluOpRiscv, a: u32, b: u32, wide: bool) -> SelCtxRiscv {
    select_alu_riscv(ctx0, inst, op, a, b, wide)
}
```

### 4.4 Compares — value form is a small sequence; branch form is ONE instruction

RISC-V has no `setcc` and no flags register; `slt`/`sltu` compute only strict-less-than. The two uses
diverge:

- **Value form** (`select_icmp_riscv`, mirrors `select_icmp` but per-condition): materialize a 0/1
  boolean via a short `slt`/`sub`/`xori`/`sltiu` sequence keyed on `MCond` —
  `LT(s)→slt rd,a,b` ; `LO(u)→sltu rd,a,b` ; `GT→slt rd,b,a` (swap) ; `HI→sltu rd,b,a` (swap) ;
  `GE→slt rd,a,b ; xori rd,rd,1` ; `HS→sltu rd,a,b ; xori rd,rd,1` ; `LE→slt rd,b,a ; xori rd,rd,1` ;
  `LS→sltu rd,b,a ; xori rd,rd,1` ; `EQ→sub rd,a,b ; sltiu rd,rd,1` ; `NE→sub rd,a,b ; sltu rd,x0,rd`.
- **Branch form** (`select_branch_riscv`) — RISC-V branches ARE compare-and-branch, so a fused
  compare+conditional-branch maps to a **single** B-type instruction (SIMPLER than x86's `cmp`+`jcc`
  AND arm64's `cmp`+`b.cond`). The `MCond`→mnemonic map with operand-swap:
  `EQ→beq` ; `NE→bne` ; `LT→blt` ; `GE→bge` ; `LO→bltu` ; `HS→bgeu` ; and the swap cases
  `GT→blt(b,a)` ; `LE→bge(b,a)` ; `HI→bltu(b,a)` ; `LS→bgeu(b,a)`. This mirrors A2-3's compare-fusion
  (`isel_arm64.tks:699`, `is_fused_compare`), reusing the neutral fusion analysis, but emits one
  `MBranchRiscv` instead of a `cmp`+conditional pair.

### 4.5 Address materialization + calls — the two-instruction PC-relative forms

- **`MLlaRiscv` (rodata/global address, the arm64-`ADRP`+`ADD` / x86-`lea rip` analog)** lowers to a
  **PAIR** `auipc rd, %pcrel_hi(sym) ; addi rd, rd, %pcrel_lo(label)` with a PAIRED relocation
  (`R_RISCV_PCREL_HI20` on the `auipc` + `R_RISCV_PCREL_LO12_I` on the `addi`) and a synthesized local
  `.text` label at the `auipc` — the sharp RISC-V detail, fully specified in §7.2. isel emits one
  `MLlaRiscv{dst; sym}`; the encoder (B2-6) expands it to the two instructions + the two relocs + the
  label. (NOT exercised by the closing exit(n) corpus — unit-goldened only, like B1's `MLeaRipX86`.)
- **`MCallRiscv`** lowers to `auipc ra, 0 ; jalr ra, 0(ra)` with a **single** `R_RISCV_CALL_PLT`
  relocation on the `auipc` (the linker treats the pair as a call macro and fills BOTH instructions
  from the one reloc — NO local label needed, unlike the `MLlaRiscv` pair). This IS exercised
  (`exit(n)` calls `tk_exit`).
- **LP64D argument/return lowering.** `arg_reg(riscv64_lp64d(), class, index)` gives `a0..a7` (int) /
  `fa0..fa7` (float); the result pin is `preg(10/*a0*/, GPR)` / `preg(10/*fa0*/, FPR)` planted at the
  `LRet`/`MCallRiscv.ret_reg` site — and because `a0 == gpr_arg[0]` (D-A), this is the SAME code path
  as arm64 (no x86-style result/arg0 divergence). Args past `a0..a7`/`fa0..fa7` are the `B2-args` stop
  (the corpus stays within the register window).

---

## 5. `regalloc_riscv.tks` — the thin RISC-V shell over the SHARED scan core (B2-4)

Identical split to B1-4: **the algorithm is shared, single-sourced**; **the MInst-facing adapter is
per-ISA, mirrored**.

### 5.1 SHARED (called, never copied)

`linear_scan`/`candidate_pool`/`assign_lookup` + `LiveInterval`/`IntervalSet`/`ScanResult`/
`SubstEntry` — all register-abstract (never mention an `MInst` case). B2-4 calls them UNCHANGED; the
subtle-eviction algorithm stays single-sourced (M.5).

### 5.2 MIRRORED (per-ISA adapter, small + mechanical)

`inst_regs_riscv(inst: MInstRiscv) -> InstRegs` — a total match over `MInstRiscv` producing the
neutral `InstRegs` (`regalloc.tks:260`, reused). RISC-V's three-address ALU reports the CLEAN
`ir_def_use(dst, [a, b])` (def is NOT also a use — no two-address tie, so SIMPLER than x86's RMW
`ir_def_use(dst, [dst, src])`); native `div`/`rem` report the same clean shape (no fixed-reg/call
model — SIMPLER than x86's `MDivSeqX86` `ir_call`); `MCallRiscv`/`MCallIndRiscv` report `ir_call`;
`MLiRiscv`/`MLuiRiscv`/`MFrameAddrRiscv`/`MLlaRiscv` report a plain def; `MBranchRiscv`/`MStoreRiscv`
report uses-only (no def). `map_minst_riscv`/`compute_intervals_riscv`/`rewrite_inst_riscv`/
`rewrite_block_riscv`/`rewrite_func_riscv`/`regalloc_func_riscv`/`regalloc_module_riscv` mirror the
`regalloc.tks`/`regalloc_x86.tks` `MFunc`-walking shells over `MFuncRiscv`/`MBlockRiscv`. These are
mechanical mirrors (the same per-ISA duplication `isel`/`encode` already are), NOT a re-implementation
of the scan (§10 R-1).

### 5.3 Spill rewrite

`rewrite_inst_riscv` mirrors `rewrite_inst` (`regalloc.tks:1622`): reload spilled uses via
`ld` into `spill_scratch(riscv64_lp64d(), GPR, nth)` (t4/t5/t6), substitute, `sd` a spilled def. The
reload/store slot address is `off(sp)` (base+imm12) — within the `B2-bigframe` window (§9). FPR spill =
the inherited `A3-fpr-spill` stop.

---

## 6. `encode_riscv.tks` — the fixed-32 word encoder (B2-5 / B2-6), the arm64 model

### 6.1 The buffer model — `[]u32` words + word-offset fixups (the A4/arm64 model, NOT B1's bytes)

RISC-V's fixed 32-bit instructions are exactly arm64's world: B2 reuses A4's `[]u32`-word,
whole-word-patch structure (`encode_arm64.tks` §2.1) — NOT B1's byte-granular `[]byte` patch. B2
parallels the arm64 encoder TYPES (its `EncWord.reloc_kind: MRelocKind` is arm64-typed) as
`EncWordRiscv`, but mirrors the SHAPE:

```teko
/**
 * EncWordRiscv — one RISC-V instruction's encoding outcome: its base 32-bit word
 * plus an optional pending fixup. A position-independent instruction resolves to
 * just `word`. An intra-function branch carries a branch fixup — its target
 * machine-block id and which displacement field (0=B-type imm[12:1], 1=J-type
 * imm[20:1]) the A4-style patch pass scrambles in once every block's word offset
 * is known (§6.3). A symbol reference carries a `RelocRiscv` (which
 * `RelocKindRiscv` this word owes). The RISC-V analog of arm64's `EncWord`
 * (byte-identical STRUCTURE, RISC-V reloc kinds) — NOT the byte-granular x86
 * `EncInstX86`.
 *
 * @since #387 B2-5
 */
pub type EncWordRiscv = struct {
    /**
     * word — the base 32-bit encoding (register/funct fields filled; branch/reloc
     * immediate fields zero until patched).
     */
    word: u32
    /**
     * has_branch — true iff an intra-function displacement must be scrambled in.
     */
    has_branch: bool
    /**
     * branch_form — which displacement field the patch pass fills: 0=B-type
     * imm[12:1] (±4 KiB), 1=J-type imm[20:1] (±1 MiB). Sizes the range check.
     */
    branch_form: u32
    /**
     * target — the destination machine-block id (meaningful when `has_branch`).
     */
    target: u32
    /**
     * has_reloc — true iff this word owes a symbol `RelocRiscv`.
     */
    has_reloc: bool
    /**
     * reloc_sym — the relocation's target symbol name (meaningful when
     * `has_reloc`).
     */
    reloc_sym: str
    /**
     * reloc_kind — which RISC-V relocation this word owes (Call/PcRelHi20/
     * PcRelLo12/…).
     */
    reloc_kind: RelocKindRiscv
}
```

### 6.2 The `RelocKindRiscv` enum + the numeric map

```teko
/**
 * RelocKindRiscv — which RISC-V ELF relocation an address-materialization, call,
 * or (future) cross-section branch owes. `Call` is the auipc+jalr call macro
 * (R_RISCV_CALL_PLT, resolves to a plain PC-relative call under a static link —
 * correct for both the cc bootstrap and the future E1 linker). `PcRelHi20`/
 * `PcRelLo12` are the PAIRED data-address relocations (§7.2). `Branch`/`Jal` name
 * the cross-section branch relocations for symmetry/future far-branch relaxation;
 * the MVP patches every intra-function branch DIRECTLY (§6.3, mirroring arm64,
 * which emits no reloc for B/B.cond), so a B2 object never carries one. `Abs64`
 * is the future data-global path (R_RISCV_64), behind the `B2-globals` stop.
 *
 * @since #387 B2-5
 */
pub type RelocKindRiscv = enum { Call; PcRelHi20; PcRelLo12; Branch; Jal; Abs64 }

/**
 * riscv_reloc_type — the numeric `R_RISCV_*` relocation type for a
 * `RelocKindRiscv`, fed to the neutral ELF writer (§7): R_RISCV_CALL_PLT(19),
 * R_RISCV_PCREL_HI20(23), R_RISCV_PCREL_LO12_I(24), R_RISCV_BRANCH(16),
 * R_RISCV_JAL(17), R_RISCV_64(2). `Branch`/`Jal`/`Abs64` are unreached by the MVP
 * (intra-function branches patch directly; globals hit `B2-globals`) and are
 * covered by a direct unit test on hand-built input.
 *
 * @param RelocKindRiscv kind  the relocation kind
 * @return u32  the `Elf64_Rela` `r_info` type field
 */
fn riscv_reloc_type(kind: RelocKindRiscv) -> u32 {
    match kind {
        Call => 19 to u32
        PcRelHi20 => 23 to u32
        PcRelLo12 => 24 to u32
        Branch => 16 to u32
        Jal => 17 to u32
        Abs64 => 2 to u32
    }
}
```

### 6.3 Branch layout + the bit-scrambled B/J immediates (the sharp encoder detail)

The word model mirrors A4-3 exactly (single emit pass recording each block's WORD offset, then a patch
pass), with ONE RISC-V-specific twist: the displacement is **byte-relative** (not word-relative like
arm64) and the immediate is **bit-permuted** across the instruction word. The scramble is the single
riskiest bit-work in B2 — **assembler-verify EVERY branch offset**.

1. **Emit pass** — prepend the prologue words; walk `f.blocks` in RPO order recording each block's
   starting WORD offset in a `BlockOffsetRiscv` map; encode each instruction via
   `encode_inst_word_riscv`; expand `MRetRiscv`→epilogue+`ret`, `MFrameAddrRiscv`→`addi rd,sp,off`,
   `MLlaRiscv`→the auipc+addi pair (+ the two relocs + the synthesized label, §7.2), `MCallRiscv`→the
   auipc+jalr pair (+ the one `Call` reloc).
2. **Patch pass** — for each branch fixup compute `disp_bytes = (block_word_offset[target] − here) * 4`
   (RISC-V branch offsets are byte displacements from the branch itself), range-check, then SCRAMBLE
   into the B-type or J-type field. The scramble REUSES the neutral `twos_field` (`encode_arm64.tks:1581`,
   which never casts a negative through `to u32`) to get the two's-complement bits, then scatters:

```teko
/**
 * btype_scramble — scatter a 13-bit two's-complement branch displacement into the
 * RISC-V B-type immediate fields (the notorious bit-permutation): imm[12]→word[31],
 * imm[10:5]→word[30:25], imm[4:1]→word[11:8], imm[11]→word[7]. `v13` is
 * `twos_field(disp_bytes, 13)` — the sign-extended byte displacement (imm[0] is
 * always 0 for a 2-byte-aligned target). All arithmetic stays in `u32`; the
 * negative case is already folded into `v13`'s two's-complement bits, so no
 * panicking negative cast occurs. Assembler-verify against
 * `clang --target=riscv64 -march=rv64g` + `objdump -d`.
 *
 * @param u32 v13  the 13-bit two's-complement displacement (`twos_field(.,13)`)
 * @return u32  the OR-mask contributing the scrambled immediate to the B-type word
 */
fn btype_scramble(v13: u32) -> u32 {
    let b12 = (v13 >> (12 to u32)) & (0x1 to u32)
    let b11 = (v13 >> (11 to u32)) & (0x1 to u32)
    let b10_5 = (v13 >> (5 to u32)) & (0x3F to u32)
    let b4_1 = (v13 >> (1 to u32)) & (0xF to u32)
    (b12 << (31 to u32)) | (b10_5 << (25 to u32)) | (b4_1 << (8 to u32)) | (b11 << (7 to u32))
}

/**
 * jtype_scramble — scatter a 21-bit two's-complement jump displacement into the
 * RISC-V J-type immediate fields (`jal`): imm[20]→word[31], imm[10:1]→word[30:21],
 * imm[11]→word[20], imm[19:12]→word[19:12]. `v21` is `twos_field(disp_bytes, 21)`
 * (imm[0] always 0). Same `u32`-only, no-negative-cast discipline as
 * `btype_scramble`. Assembler-verify every offset.
 *
 * @param u32 v21  the 21-bit two's-complement displacement (`twos_field(.,21)`)
 * @return u32  the OR-mask contributing the scrambled immediate to the J-type word
 */
fn jtype_scramble(v21: u32) -> u32 {
    let j20 = (v21 >> (20 to u32)) & (0x1 to u32)
    let j10_1 = (v21 >> (1 to u32)) & (0x3FF to u32)
    let j11 = (v21 >> (11 to u32)) & (0x1 to u32)
    let j19_12 = (v21 >> (12 to u32)) & (0xFF to u32)
    (j20 << (31 to u32)) | (j10_1 << (21 to u32)) | (j11 << (20 to u32)) | (j19_12 << (12 to u32))
}
```

Range checks: B-type fits when `disp_bytes ∈ [−4096, 4094]` (13-bit signed, even); J-type when
`disp_bytes ∈ [−1048576, 1048574]` (21-bit signed, even). Overflow raises the `B2-farbranch`
honest-stop (internal invariant — real functions are tiny, mirrors `A4-farbranch`).

### 6.4 The per-format encoding table (assembler cross-checked)

The encoder handles all **six** RISC-V formats. Each row is the operand-zeroed template; the
`rd`/`rs1`/`rs2`/`funct3`/`funct7`/imm fields OR in. **The implementer MUST re-derive each against
`clang --target=riscv64-linux-gnu -march=rv64g -c` + `objdump -d` (or `llvm-mc -triple=riscv64
--show-encoding`) — the table is the spec, the assembler is the oracle**, the exact A4/B1 discipline.
Two worked pins: `addi a0, x0, 42` = `0x02A00513`; `ret` (`jalr x0, 0(ra)`) = `0x00008067`.

| format | fields (MSB→LSB) | opcode | corpus members |
|---|---|---|---|
| **R** | `funct7[31:25] rs2[24:20] rs1[19:15] funct3[14:12] rd[11:7] opcode[6:0]` | OP=0x33 / OP-32=0x3B | add(f7=0x00,f3=0) sub(f7=0x20,f3=0) sll(f3=1) slt(f3=2) sltu(f3=3) xor(f3=4) srl(f7=0,f3=5) sra(f7=0x20,f3=5) or(f3=6) and(f3=7) ; M-ext f7=0x01: mul(f3=0) div(f3=4) divu(f3=5) rem(f3=6) remu(f3=7) ; RV64 `*w` on 0x3B |
| **I** | `imm[31:20] rs1[19:15] funct3[14:12] rd[11:7] opcode[6:0]` | OP-IMM=0x13 / LOAD=0x03 / JALR=0x67 | addi(f3=0) xori(f3=4) sltiu(f3=3) slli/srli/srai(shamt in imm) ; ld(f3=3) lw(f3=2) lb(f3=0) lbu(f3=4) lh(f3=1) lhu(f3=5) lwu(f3=6) ; jalr(f3=0) |
| **S** | `imm[11:5][31:25] rs2[24:20] rs1[19:15] funct3[14:12] imm[4:0][11:7] opcode[6:0]` | STORE=0x23 | sd(f3=3) sw(f3=2) sh(f3=1) sb(f3=0) — the imm is SPLIT across two fields (§6.3-style, but constant) |
| **B** | `imm[12|10:5][31:25] rs2 rs1 funct3 imm[4:1|11][11:7] opcode` | BRANCH=0x63 | beq(f3=0) bne(f3=1) blt(f3=4) bge(f3=5) bltu(f3=6) bgeu(f3=7) — the SCRAMBLED immediate (§6.3) |
| **U** | `imm[31:12][31:12] rd[11:7] opcode[6:0]` | LUI=0x37 / AUIPC=0x17 | lui, auipc — imm placed directly, no scramble |
| **J** | `imm[20|10:1|11|19:12][31:12] rd[11:7] opcode[6:0]` | JAL=0x6F | jal — the SCRAMBLED immediate (§6.3) |

> **RISC-V encoding traps (bake in).** (a) The B-type and J-type immediates are the ONLY genuinely
> bit-permuted encodings — golden-pin every one. (b) S-type also splits its immediate (imm[11:5] /
> imm[4:0]) but into two CONTIGUOUS runs (no permutation) — still a common mistake, pin the frame
> store/load goldens. (c) `x0` is the hardwired zero register — `li small`=`addi rd,x0,imm`,
> `neg`=`sub rd,x0,rs`, `jal x0`=discard-link jump, `ret`=`jalr x0,0(ra)`; the encoder emits id 0 for
> these rs1/rd fields. (d) shift-immediate `slli/srli/srai` on RV64 uses a 6-bit shamt (imm[5:0]) with
> funct bits in imm[11:6] — distinct from the 5-bit RV32 form.

### 6.5 Frame — LP64D prologue/epilogue (mirrors A4-2/B1's re-derived save-set)

`compute_frame_layout_riscv(abi, f) -> FrameLayoutRiscv` mirrors `compute_frame_layout`
(`encode_arm64.tks:1079`): re-derive the callee-saved save-set by SCANNING the physical `MFuncRiscv`
(`inst_regs_riscv` + `is_callee_saved` — `MFuncRiscv` carries no `used_callee_saved`, the A4/B1
precedent), lay out slots honoring each `LAlloca` alignment, size the frame to **16** (RISC-V ABI
16-byte stack alignment). **Prologue** (framed): `addi sp, sp, -N` ; `sd ra, (N−8)(sp)` ;
`sd s0, (N−16)(sp)` ; `addi s0, sp, N` (set frame pointer) ; one `sd` per used callee-saved reg.
**Epilogue** (LIFO): `ld` each saved reg ; `ld s0` ; `ld ra` ; `addi sp, sp, N` ; `jalr x0, 0(ra)`
(`ret`). The `exit(n)` keystone `main` is **frameless** (a non-returning `call tk_exit`, no
`MRetRiscv`) → `size=0`, empty prologue — the first end-to-end binary needs only B2-5's core + the
`li`/`MLiRiscv` for the exit-code arg + B2-6's `MCallRiscv` `Call` reloc + B2-7. A frame larger than
the `addi` imm12 window (±2048) is the `B2-bigframe` stop (§9) — a REAL RISC-V limit (imm12 is only
12-bit), not the near-N/A x86 `B1-bigframe` (imm32 covers 2 GB).

---

## 7. ELF writer REUSE — the neutral `emit_elf_object` core (B2-7)

**This is the headline B2 saving: NO new object writer.** `objfile_elf.tks`'s writer body is already
ISA-agnostic (Ehdr, seven section headers, symtab, strtab/shstrtab, rela) except for THREE bindings:
the hardwired `e_machine = 62` (`emit_elf_header:548`), the hardwired `e_flags = 0` (`:553`), and the
`elf_reloc_type(RelocKindX86)` map (`:259`). B2-7 generalizes those three, adds one neutral structural
capability (local `.text` label symbols), and leaves the writer's byte layout untouched.

### 7.1 The minimal generalization (the resolved tension, §10 D-B)

Introduce a neutral input value + a neutral relocation request, and extract the writer body:

```teko
/**
 * ElfRelocReq — one ALREADY-RESOLVED relocation the neutral ELF writer turns
 * into an `Elf64_Rela`: the `.text`-relative field offset (`r_offset`), the
 * target symbol NAME (resolved to an index by the writer), the NUMERIC
 * `R_<arch>_*` type (each per-ISA encoder maps its own `RelocKind*` → the number,
 * so the writer holds no ISA knowledge), and the addend. This is the ISA-agnostic
 * form of `RelocX86`/`RelocRiscv` — the per-ISA reloc's `kind` is already lowered
 * to `rtype`.
 *
 * @since #387 B2-7
 */
pub type ElfRelocReq = struct {
    /** offset — the patched field's `.text`-section-relative byte offset. */
    offset: u32
    /** sym — the target symbol's final name (function, extern, rodata datum, or a `.text` pcrel label). */
    sym: str
    /** rtype — the numeric `R_<arch>_*` relocation type (per-ISA-mapped). */
    rtype: u32
    /** addend — the `r_addend`; a rodata target folds its `.rodata` offset onto this base. */
    addend: i64
}

/**
 * ElfObject — the ISA-agnostic input to the shared ELF64 writer: the machine id
 * (`e_machine`: EM_X86_64=62, EM_RISCV=243), the ABI flags (`e_flags`: 0 for
 * x86-64, EF_RISCV_FLOAT_ABI_DOUBLE=0x0004 for RV64 LP64D), the `.text`/`.rodata`
 * section images, the neutral `Symbol` list (local-then-global-then-undefined,
 * now INCLUDING per-pcrel-pair `.text` label locals — §7.2), and the resolved
 * `ElfRelocReq` rows. Both `emit_elf` (x86) and `emit_elf_riscv` build this and
 * call `emit_elf_object`; the writer is ISA-agnostic, so E1 (the ELF static
 * linker) reuses it unchanged.
 *
 * @since #387 B2-7
 */
pub type ElfObject = struct {
    /** e_machine — the ELF `e_machine` (62=x86-64, 243=riscv). */
    e_machine: u32
    /** e_flags — the ELF `e_flags` (0=x86-64, 0x0004=RV64 LP64D hard-float). */
    e_flags: u32
    /** text — the `.text` section image. */
    text: []byte
    /** rodata — the `.rodata` section image. */
    rodata: []byte
    /** symbols — the neutral symbol list (locals incl. `.text` pcrel labels, then globals, then undefined). */
    symbols: []Symbol
    /** relocs — the resolved relocation requests, `.text`-relative, in emission order. */
    relocs: []ElfRelocReq
}

/**
 * emit_elf_object — the ISA-AGNOSTIC ELF64 `ET_REL` writer (the extracted B1
 * body): the `Elf64_Ehdr` (with the supplied `e_machine`/`e_flags`) + the seven
 * section headers + the section images + the `Elf64_Sym` table (local-then-global,
 * the `.rodata` `STT_SECTION` symbol AND any `.text` pcrel-label locals) + the
 * `Elf64_Rela` table (`r_info = (idx<<32)|rtype`). No ISA knowledge remains past
 * this boundary — the per-ISA `emit_elf`/`emit_elf_riscv` adapters supply the
 * machine, flags, and numeric reloc types. E1 reuses this verbatim.
 *
 * @param ElfObject obj  the machine id + flags + section images + symbols + relocs
 * @return []byte  the ELF64 object file bytes
 */
pub fn emit_elf_object(obj: ElfObject) -> []byte { … }
```

`emit_elf`'s current body (the `elf_build_symbols`/`build_elf_strtab`/`elf_build_relas`/
`compute_elf_layout`/emit sequence, `objfile_elf.tks:698`) MOVES into `emit_elf_object`, with
`emit_elf_header` taking `e_machine`/`e_flags` parameters and `elf_build_relas` consuming pre-resolved
`ElfRelocReq` rows (the rodata-hit addend-folding logic is unchanged — it folds `.rodata` offset onto
whatever base addend the request carries). B1's `emit_elf` becomes a thin, behavior-preserving adapter:

```teko
/**
 * emit_elf — the x86-64 ELF adapter (B1's entry, now delegating): map the
 * `EncodedModuleX86` to a neutral `ElfObject` (`e_machine`=62, `e_flags`=0, each
 * `RelocX86` lowered to an `ElfRelocReq` via `elf_reloc_type`) and hand it to the
 * shared `emit_elf_object`. The byte output is IDENTICAL to B1's pre-B2 writer
 * (B1's goldens are the guardrail — they must stay byte-for-byte green), so the
 * x86 differential is untouched.
 *
 * @param EncodedModuleX86 enc  the x86 section images + symbols + relocations
 * @return []byte  the ELF64 object file bytes
 */
pub fn emit_elf(enc: EncodedModuleX86) -> []byte {
    emit_elf_object(ElfObject {
        e_machine = 62 to u32
        e_flags = 0 to u32
        text = enc.text
        rodata = enc.rodata
        symbols = enc.symbols
        relocs = x86_reloc_reqs(enc)
    })
}
```

and B2's `emit_elf_riscv` is the symmetric adapter (`e_machine=243`, `e_flags=0x0004`, each
`RelocRiscv` lowered via `riscv_reloc_type`).

### 7.2 The PAIR relocation + the synthesized `.text` label (the ONE structural writer addition)

The sharp RISC-V-vs-x86 reloc difference: an x86 rodata address is ONE `R_X86_64_PC32` reloc; a RISC-V
rodata address is a **PAIR** — `R_RISCV_PCREL_HI20` on the `auipc` (targeting the real symbol) +
`R_RISCV_PCREL_LO12_I` on the `addi`, whose `r_info` symbol is **NOT the real target** but a
**file-local label defined at the `auipc`'s address** (the linker re-finds the HI20 reloc at that
label to recompute the pairing). So the RISC-V ENCODER (`encode_module_riscv`), when lowering an
`MLlaRiscv`, does THREE things the x86 encoder never does:

1. emit `auipc rd, 0` at word offset `P_hi`, record HI20 `RelocRiscv{offset=P_hi, sym=<target>,
   kind=PcRelHi20, addend=<rodata offset or 0>}`;
2. **synthesize a local label** `Symbol{name=".Lpcrel_hiN", defined=true, local=true, sect=1(.text),
   offset=P_hi}` (N a per-module counter) and add it to the module symbol list;
3. emit `addi rd, rd, 0` at word offset `P_lo`, record LO12 `RelocRiscv{offset=P_lo,
   sym=".Lpcrel_hiN", kind=PcRelLo12, addend=0}`.

All three are module-rebased at assembly exactly like B1's reloc/offset rebase. The PAIRING complexity
lives ENTIRELY in the encoder — the writer stays neutral. The writer's ONLY change is to emit
file-local `.text` labels: `elf_build_symbols` gains a pass emitting each `defined && local && sect==1`
`Symbol` as `STB_LOCAL|STT_NOTYPE` (info=0x00, `st_shndx=1`, section-relative `st_value`). Because x86
NEVER produces a `sect==1` local (it has only `sect==2` rodata locals + globals + undefined), B1's
symbol table — and thus its bytes — are UNCHANGED (the guardrail). `elf_first_global_index` already
finds the boundary by bind field; `elf_symbol_index` already resolves the LO12's `.Lpcrel_hiN` by
name. The whole pair mechanism therefore drops into the existing writer with one neutral pass.

> **Call reloc — SIMPLER, no label.** `R_RISCV_CALL_PLT` on the `auipc` of `auipc ra,0 ; jalr ra,0(ra)`
> is a call MACRO reloc: the linker fills BOTH instructions from the single reloc, targeting the callee
> directly with addend 0 — NO synthesized label (unlike the `MLlaRiscv` data pair). The `exit(n)`
> corpus exercises only this call path; the `MLlaRiscv` HI20/LO12 pair is unit-goldened but not hit
> e2e (no rodata address in the exit-only corpus, exactly like B1's `MLeaRipX86`).

### 7.3 Baked B1 §7.4 findings (ELF requirements, unchanged)

Section-relative `st_value` (NOT the Mach-O absolute rebase); `.text`-relative `r_offset`; local
rodata via the `.rodata` `STT_SECTION` symbol + addend; `.symtab.sh_info` = first-global index. All
inherited from the reused writer body — B2 adds only the `.text`-label locals (which slot in among the
locals, so `sh_info` still counts all locals correctly).

### 7.4 The RISC-V-specific relocations the corpus needs

| `RelocKindRiscv` | source | ELF type | value | `r_addend` | label? |
|---|---|---|---|---|---|
| `Call` | `MCallRiscv` (auipc+jalr) | `R_RISCV_CALL_PLT` | 19 | 0 | no |
| `PcRelHi20` | `MLlaRiscv` auipc | `R_RISCV_PCREL_HI20` | 23 | rodata offset / 0 | — (targets real sym) |
| `PcRelLo12` | `MLlaRiscv` addi | `R_RISCV_PCREL_LO12_I` | 24 | 0 | YES → `.Lpcrel_hiN` |
| `Branch`/`Jal` | (cross-section — MVP patches intra-fn directly) | `R_RISCV_BRANCH`/`_JAL` | 16/17 | 0 | no |
| `Abs64` | data global (`B2-globals` stop) | `R_RISCV_64` | 2 | absolute | no |

> **`R_RISCV_RELAX` OMITTED (named stop `B2-relax`).** The linker-relaxation hint reloc (that lets `ld`
> relax `auipc`+`jalr`→`jal`) is an OPTIMIZATION — omitting it yields correct, slightly-larger code.
> The MVP emits no `R_RISCV_RELAX`; a fast-follow adds it. Correctness unaffected.

---

## 8. Target dispatch + the qemu differential (B2-8, the KEYSTONE)

### 8.1 `emit_native` gains a third target arm (additive, FIXPOINT-preserving)

`NativeTarget` (`project.tks:642`) gains a `Riscv64Linux` case; `target_from_name` (`:655`) maps
`"riscv64-linux"` → `Riscv64Linux`; `emit_native`'s `match` (`:715`) gains the arm dispatching to a
new `emit_native_riscv` tail (mirroring `emit_native_x86:764`):

```teko
/**
 * emit_native_riscv — the riscv64/ELF own-backend tail (#387 B2): select RISC-V
 * `MInstRiscv` from the lowered module, allocate registers under the LP64D
 * register file (`riscv64_lp64d`), encode to fixed-32 RISC-V machine words, wrap
 * them in an `ET_REL` ELF64 object (`emit_elf_riscv` → the shared
 * `emit_elf_object`), and hand the bytes to `finish_native_object` (write `.o` +
 * cc-link + `.tsym` + success marker). Reached only via
 * `TEKO_TARGET=riscv64-linux`; the emitted `.o` is a riscv64 object, so the
 * executing C-vs-own differential runs on the linux-x86_64 CI runner under
 * `qemu-riscv64-static` with a riscv64 cross-linker (§8.2), while a macOS host
 * still writes the `.o` for the byte / well-formedness cross-check (`llvm-readobj`
 * is cross-format) even though no riscv cross-linker/emulator runs there.
 *
 * @param dir   the project directory (for diagnostics)
 * @param od    the slash-stripped output directory (already created)
 * @param stem  the output artifact name (the manifest `name`)
 * @param lmod  the target-independent lowered module
 * @param prog  the checked program (extern-reachability, `.tsym`)
 * @param m     the resolved manifest (link knobs)
 * @return      0 on a successful build+link, else the failing status
 */
fn emit_native_riscv(dir: str, od: str, stem: str, lmod: teko::lir::LModule, prog: checker::TProgram, m: Manifest) -> i32 {
    let sel = match teko::backend::select_module_riscv(lmod) { teko::backend::MModuleRiscv as x => x; error as e => return fail(dir, e.message) }
    let col = match teko::backend::regalloc_module_riscv(teko::backend::riscv64_lp64d(), sel) { teko::backend::MModuleRiscv as x => x; error as e => return fail(dir, e.message) }
    let enc = match teko::backend::encode_module_riscv(teko::backend::riscv64_lp64d(), col) { teko::backend::EncodedModuleRiscv as x => x; error as e => return fail(dir, e.message) }
    finish_native_object(dir, od, stem, teko::backend::emit_elf_riscv(enc), prog, m)
}
```

The `Arm64Macho` and `X8664Linux` arms are UNCHANGED (verbatim A4/B1 bodies), so the default build
stays byte-identical (FIXPOINT) and both existing differentials are untouched. `finish_native_object`
is reused verbatim — the exclusive `"(own backend)"` marker (the F1(a) harness guard) is unchanged.

> **The cross-link seam (D-D).** On the linux-x86_64 CI host the own `.o` is a riscv64 object, so the
> host `cc` cannot link it (exactly B1's macOS-can't-link-x86 posture). B2-8 selects the RISC-V
> cross-linker in `build_cc_argv` (`project.tks:420`): when `TEKO_TARGET=riscv64-linux` and no manifest
> `cc` override is set, default the compiler to `riscv64-linux-gnu-gcc` (overridable via a new `TEKO_CC`
> env seam, mirroring `TEKO_BACKEND`/`TEKO_TARGET`). `build_cc_argv` already passes `teko_rt.c` +
> `assert.c` as inputs, so the cross-gcc compiles the C runtime FOR riscv and links it with the own
> `.o` into a qemu-runnable executable — and prints `"(own backend)"`. On macOS (no cross-linker) the
> link honest-skips like B1's x86 lane: the `.o` is written for byte-testing, execution honest-skips.

### 8.2 The differential — CI runs under QEMU, macOS byte-tests

The host is macOS-arm64 (cannot run riscv64 ELF). The executing `C-riscv == own-riscv` differential
runs on the linux-x86_64 CI runner under `qemu-riscv64-static`, re-using the `riscv64-qemu` lane
(`native.yml:117`, currently `if: false` because the OLD full `test . --coverage` run under qemu was
8-9m). B2 re-scopes that lane to a FAST tiny-corpus differential (the `own_*` corpus only, seconds not
minutes) and re-enables it:

- The lane already installs `gcc-riscv64-linux-gnu qemu-user-static clang` (`:138`) — no new deps.
- Extend `scripts/diff_c_own.sh` with a riscv mode selected by an explicit `TEKO_DIFF_TARGET=riscv64-linux`
  env (NOT `uname`, which reports x86_64 on the CI host): it sets `OWN_TARGET_ENV="TEKO_TARGET=riscv64-linux"`,
  `TEKO_CC=riscv64-linux-gnu-gcc`, wraps both the C-side and own-side executions in `RUN_WRAP="qemu-riscv64-static"`,
  and points `OBJ_CHECK` at `check_elf.sh` invoked with the riscv `readelf`/`objdump`. It reuses the
  corpus loop + the KNOWN-STOP logic verbatim (DRY over a duplicated riscv script). Per fixture:
  build the C-riscv binary (default C backend + `TEKO_CC` cross-gcc), build the own-riscv binary
  (`TEKO_BACKEND=native TEKO_TARGET=riscv64-linux` + cross-gcc-as-linker), run BOTH under
  `qemu-riscv64-static`, assert identical exit code + stdout.
- The same F1 guards apply: the own build MUST print `"(own backend)"`, the `<stem>.o` MUST exist.
- The `native.yml` gate job already treats `riscv64-qemu` as BLOCKING (`:389`) — flipping it back on
  makes a red riscv gate the merge (main-integrity law).

Locally on macOS: encoder goldens (byte-vectors, assembler-cross-checked with
`clang --target=riscv64-linux-gnu -march=rv64g -c` + `objdump -d`) run machine-free in the unit gate;
`llvm-readobj`/`readelf` round-trips the emitted `.o` cross-format; execution honest-skips (no riscv
emulator on the dev host).

**Validation matrix (which runs where):**

| check | linux-x86_64 CI (qemu) | macOS-arm64 (dev + CI) |
|---|---|---|
| encoder golden word-vectors (`clang --target=riscv64 -march=rv64g` oracle) | ✓ unit gate | ✓ unit gate (clang cross-targets) |
| `emit_elf_riscv` header/symtab/rela golden bytes | ✓ | ✓ (machine-free) |
| `.o` well-formedness (`readelf`/`llvm-readobj`/`objdump`) | ✓ (riscv binutils) | ✓ (llvm cross-format) |
| B1 `emit_elf` bytes byte-identical (the writer-reuse guardrail) | ✓ | ✓ |
| **executing `C-riscv == own-riscv` differential** | ✓ (qemu-riscv64-static) | honest-skip (no riscv emulator) |
| arm64 Mach-O + x86-64 ELF differentials | unchanged | unchanged |

### 8.3 The interp oracle — NO parallel `minst_riscv_interp` for the MVP (grounded, B1's R-2)

As in B1: `minst_interp` is a total match over arm64 `MInst` semantics — it does NOT generalize. A
full parallel `minst_riscv_interp` is DEFERRED, because the oracle role is covered three ways without
it: (a) the target-independent LIR interp validates program semantics pre-isel (shared); (b) the
assembler-cross-checked golden word-vectors validate each encoding; (c) the executing qemu differential
validates end-to-end on a runner that actually runs the artifact. `minst_riscv_interp` is a named
optional future add (`B2-interp`), REPORTED not blocking (§10 R-2).

---

## 9. Honest-stops — which slice each lives behind (0.3.1 completion vs later)

| stop | slice | what it defers | closed by |
|---|---|---|---|
| `B2-fp` | B2-5 (`encode_inst_word_riscv`)/B2-3 | F/D float ops + the LP64D `fa*` float-arg path (fadd.d/fcvt/fmv/…) | 0.3.1 (mirrors `A4-fp`/`B1-fp`) |
| `B2-args` | B2-3/B2-8 | stack args past a0-a7 / fa0-fa7 + `tk_set_args` argv forwarding | 0.3.1 (mirrors `B1-args`) |
| `B2-globals` | B2-6/B2-7 | `.data`/`.bss` + `R_RISCV_64` data relocs — N2 lowers no `LGlobal` | when lowering emits globals |
| `B2-bigimm` | B2-3/B2-5 | 64-bit immediate materialization beyond `lui`+`addi` (multi-instruction `li`) — a REAL RISC-V limit (not the near-N/A x86 movabs) | when a corpus needs a 64-bit literal |
| `B2-bigframe` | B2-6 | a frame > the `addi` imm12 window (±2048) — a REAL RISC-V limit (imm12), needs a materialized offset | when a function needs a large frame |
| `B2-farbranch` | B2-6 | B-type ±4 KiB / J-type ±1 MiB overflow (internal invariant — real functions tiny) | (mirrors `A4-farbranch`) |
| `B2-relax` | B2-6/B2-7 | the `R_RISCV_RELAX` linker-relaxation hint (an optimization; correctness unaffected) | a relaxation fast-follow |
| `B2-interp` | (oracle) | a parallel `minst_riscv_interp` (LIR-interp + goldens + real qemu diff cover it) | optional (§10 R-2) |
| FPR spill | — inherited | `detect_fpr_spill` analog errors before encode (A3's `A3-fpr-spill`) | A3's follow-up |
| `loop` back-edge | — inherited | regalloc honest-stops (`A3-loop`) before encode | A3's `A3-loop` |
| i128 register-pair ops | — inherited | isel never emits them (rides A2's i128 route) | A2's i128 route |
| PE/COFF, wasm | later clusters | other targets | C1 (#389), later |
| `--backend`/`--target` flags | later cluster | the real manifest flags (B2 uses env seams) | D1/#390 |
| own ELF linker (drop cross-`gcc`) | later cluster | the ELF static linker | Phase E1/#226 |

Each stop is a NAMED error the pipeline surfaces; a fixture reaching one stops IDENTICALLY on the own
side and is compared at the stop, never at a fabricated value (M.3, the A4/B1 precedent).

> **RISC-V note.** `B2-bigimm` and `B2-bigframe` are MORE real than their x86 twins (`B1-bigimm`/
> `B1-bigframe` were near-N/A because x86 `movabs`/`imm32` cover the range): RISC-V's `addi` immediate
> is only 12-bit and its constant materialization is genuinely multi-instruction, so these stops guard
> a real boundary the moment a corpus exceeds it. The exit(n) corpus stays well inside both (`exit`
> codes fit imm12; `main` is frameless).
>
> **No `B2-memindex`.** RISC-V has NO index*scale addressing at all — `MMem{base;offset}` (base+imm12)
> is the complete addressing model, so B1's `B1-memindex` stop has no RISC-V analog.

---

## 10. Deferrals + recorded decisions (law-first — NO HALT) + reported findings

**D-A · RISC-V result register = first argument register (`a0`).** UNLIKE x86's `RAX≠RDI` split (B1's
D-A), RISC-V's `a0` is both `gpr_arg[0]` AND the integer result — like AAPCS64's `x0`. So B2 needs no
result-pin divergence; the return/`ret_reg` path is the arm64 one. `AbiDescriptor` shape unchanged.
Recorded — RISC-V is the simpler ABI.

**D-B · ELF writer REUSED via a minimal neutral generalization, NOT a parallel writer.** §7. The
flagged tension. Resolved law-first: (M.1 smallest-blast + M.5 one-way + the reuse-wholesale mandate)
extract the B1 writer body into `emit_elf_object(ElfObject)` parameterized on `e_machine` + `e_flags` +
numeric reloc type, plus ONE neutral gap-fill (emit `.text`-label locals). B1's `emit_elf` becomes a
behavior-preserving 5-line adapter whose bytes are IDENTICAL (B1 goldens are the guardrail); B2's
`emit_elf_riscv` is the symmetric adapter. The RISC-V PAIR-reloc + label-synthesis lives in the RISC-V
ENCODER, never the writer, so the writer stays ISA-agnostic and E1 reuses it. A **parallel
`emit_elf_riscv` writer** is REJECTED (it duplicates the whole writer — contradicting the
reuse-wholesale mandate and M.5). **This is the flagged potential-HALT, and it has a clear winner
(three params + one neutral pass, all guarded by B1's byte-goldens), so it does NOT HALT.** Recorded.

**D-C · Parallel `minst_riscv` IR (inherited from B1's D-C).** §2. Generalizing `MInst` across three
ISAs churns five frozen total-matches and risks two shipped differentials; the parallel IR is additive
and is the architecture-of-record's per-ISA split. Recorded.

**D-D · Cross-linker via `TEKO_TARGET`-derived cc + a `TEKO_CC` seam.** §8.1. `TEKO_TARGET=riscv64-linux`
defaults `build_cc_argv`'s compiler to `riscv64-linux-gnu-gcc` (overridable by a new `TEKO_CC` env seam,
mirroring the other seams), so the own riscv `.o` cross-links + prints the marker on the x86 CI host;
D1/#390's real `--target` supersedes it. Additive, default-preserving (empty `TEKO_TARGET` → host cc).
Recorded.

**D-E · `TEKO_TARGET=riscv64-linux` env seam (not the `--target` flag).** Mirrors B1's D-D / A4's D-B.
Additive `NativeTarget::Riscv64Linux` + `target_from_name` entry; default → arm64 (fixpoint-preserving);
D1/#390 supersedes. Recorded.

**D-F · Intra-function branches patched DIRECTLY (no reloc), like arm64.** §6.3. RISC-V B/J
displacements are computed + scrambled by the encoder's patch pass; `R_RISCV_BRANCH`/`R_RISCV_JAL` are
named in `RelocKindRiscv` for future cross-section/far branches but the MVP never emits them. Law-first
(M.1 — the simplest total encoder). Recorded.

**D-G · cross-`gcc`-as-linker on CI (D-2 option 1 bootstrap, inherited).** B2 links the riscv `.o` via
`riscv64-linux-gnu-gcc` against the C-built `teko_rt`; toolchain independence on riscv arrives at
E1/#226 (the ELF static linker), NOT at #387 — recorded so no one claims independence prematurely (M.3,
the A4/B1 precedent).

**Reported findings (adjacent — NOT turned into issues here; folded/sequenced by the integrator):**

- **R-1 · The per-ISA `compute_intervals`/`rewrite_*` MFunc-walk AND the fixed-32 word-encoder shell
  are mirrored, not shared.** B2 (like B1) duplicates the thin interval-building/rewrite WALK and now
  ALSO the `[]u32`-word encode/patch shell (which arm64 + riscv share in STRUCTURE but not in type,
  because `EncWord.reloc_kind` is per-ISA). The subtle scan/eviction algorithm stays single-sourced
  (§5.1), and the negative→field encoder `twos_field` IS single-sourced (§6.3). A future extraction —
  a target-independent interval builder + a generic fixed-width word-patch pass over a decomposed
  `[]InstRegs`/`[]EncWordNeutral`, once the compiler can express generic-over-instruction-type without
  the silent-collision hazard — would single-source both. REPORTED for the integrator to sequence
  (a 0.3.1 DRY-sweep companion, likely with B1's R-1), not a B2 blocker.
- **R-2 · No parallel `minst_riscv_interp`.** §8.3 — the LIR interp + goldens + the real qemu
  differential cover the oracle role; a parallel interp is an optional later add. REPORTED.
- **R-3 · `own_print_exit` (LIR builtin-call surface).** The KNOWN-STOP (`diff_c_own.sh:134`, `println`
  vs `tk_println`) is a LIR-lowering gap shared across ALL targets — it KNOWN-STOPs identically on the
  riscv lane (the cross-`ld` rejects the undefined `println`). Already a reported finding; B2 inherits
  it unchanged (no new work).
- **R-4 · The `emit_elf_header`/`elf_reloc_type` signatures change (writer generalization touches
  frozen B1 code).** §7.1 — extracting `emit_elf_object` edits `objfile_elf.tks` (a shared, recently
  frozen file). The touch is behavior-preserving (B1's byte-goldens must stay green), the W15
  "touch = clean" posture applies, and it is the DRY-correct single-writer the reuse mandate demands.
  REPORTED so the integrator expects the `objfile_elf.tks` diff in the B2-7 sub-PR.

---

## 11. Regression fixtures + the gate

### 11.1 Golden word-vector unit tests (`encode_riscv_test.tkt`) — engine-agnostic, assembler-oracled

- **Per-`MInstRiscv` encodings** (B2-5): `addi a0,x0,42 → 0x02A00513`; `add a0,a1,a2`; `sub`/`and`/
  `or`/`xor`/`sll`/`srl`/`sra`; `mul a0,a1,a2`; `div`/`divu`/`rem a0,a1,a2` (the native one-instruction
  path); the `*w` RV64 32-bit forms (`addw`/`subw`/`mulw`/`remw`); the compare sequences per `MCond`
  (`slt`+`xori`, `sub`+`sltiu`); `ld a0,off(sp)` / `sd a1,off(sp)` (the S-type SPLIT immediate);
  `lui`/`auipc`; `ret → 0x00008067`; each F/D case → the `B2-fp` stop. **Each vector re-derived
  against `clang --target=riscv64-linux-gnu -march=rv64g -c` + `objdump -d`** (table is spec, assembler
  is oracle).
- **The bit-scrambled branches** (B2-6, THE sharp test): a two-block `if` → the `beq`/`blt` B-type
  displacement scrambled per §6.3, byte-checked against the assembler; an `MJalRiscv` back/forward jump
  → the J-type scramble; NEGATIVE displacements (backward branch) exercise the `twos_field` two's-complement
  path with NO panicking negative cast.
- **Frame** (B2-6): a spill fixture (tiny 2-GPR test descriptor forcing a spill) → the prologue
  `addi sp,sp,-N ; sd ra,(N-8)(sp) ; sd s0,(N-16)(sp) ; addi s0,sp,N`, the `MFrameAddrRiscv →
  addi rd,sp,off`, the epilogue `ld…; addi sp,sp,N; jalr x0,0(ra)`; `compute_frame_layout_riscv`
  offsets asserted directly; a frameless `main` → `size=0`, empty prologue.
- **Reloc records** (B2-6): an `MCallRiscv tk_exit` → `auipc ra,0 ; jalr ra,0(ra)` + ONE
  `RelocRiscv{Call,"tk_exit"}` on the auipc, addend 0; an `MLlaRiscv` → `auipc rd,0 ; addi rd,rd,0` +
  the PAIR `RelocRiscv{PcRelHi20,<sym>}` + `RelocRiscv{PcRelLo12,".Lpcrel_hi0"}` + the synthesized
  `Symbol{".Lpcrel_hi0", local, sect=1, offset=auipc_off}`.

### 11.2 ELF writer tests (`objfile_elf_test.tkt` — extended, reuse guardrail)

- **B1 byte-identity guardrail** (B2-7): every EXISTING `objfile_elf_test.tkt` x86 golden re-runs
  UNCHANGED and byte-identical after `emit_elf` becomes the adapter — the proof the extraction is
  behavior-preserving.
- **RISC-V goldens** (B2-7): a one-function riscv module's `Ehdr` has `e_machine=243`, `e_flags=0x0004`;
  `.symtab` has `main` global-defined (`st_shndx=1`, section-relative `st_value`), the `.Lpcrel_hi0`
  LOCAL `.text` label (when an `MLlaRiscv` is present), and `tk_exit` undefined; `.rela.text` has one
  `R_RISCV_CALL_PLT` (and, for the pair fixture, `PCREL_HI20`+`PCREL_LO12_I` targeting the label);
  `sh_info` = first-global index (counting the pcrel-label local); `e_shnum`/`e_shstrndx` correct.

### 11.3 End-to-end differential fixtures — the SAME corpus A4/B1 use (`examples/regressions/own_*`)

B2 adds NO new fixtures; it adds the riscv own-native column to the existing corpus (`diff_c_own.sh:112`):

| fixture | program | expected exit | VM | C-native | own-arm64 | own-x86 | **own-riscv (new)** |
|---|---|---|---|---|---|---|---|
| `own_exit_zero` | `exit(0)` | 0 | ✓ | ✓ | ✓ | ✓ | **qemu-run** |
| `own_exit_code` | `exit(42)` | 42 | ✓ | ✓ | ✓ | ✓ | **qemu-run** (`addi a0,x0,42` + call) |
| `own_arith_exit` | `exit(6 * 7)` | 42 | ✓ | ✓ | ✓ | ✓ | **qemu-run** (`mul` path) |
| `own_sub_exit` | `exit(100 - 58)` | 42 | ✓ | ✓ | ✓ | ✓ | **qemu-run** |
| `own_if_exit` | `if 5 > 3 { exit(1) } else { exit(2) }` | 1 | ✓ | ✓ | ✓ | ✓ | **qemu-run** (fused B-type branch) |
| `own_match_exit` | `match k { 0 => exit(7); _ => exit(9) }` | (per k) | ✓ | ✓ | ✓ | ✓ | **qemu-run** |
| `own_print_exit` | `println` then `exit` | — | ✓ | ✓ | KSTOP | KSTOP | **KSTOP** (R-3, inherited) |

The exit(n)-scoped corpus is the frameless-`main`/interp-validated subset (the A4 §9.2 convention); a
trailing-value framed `main` rides the same `MRetRiscv`+prologue path the frame goldens exercise.

### 11.4 Ritual posture — RIGHT-SIZED (dono ruling 2026-07-10; CI is the gate)

- **CI is the gate.** The FULL gate (both engines + FIXPOINT + paranoid + the qemu differential) runs
  in CI at the ritual points. Local per-slice verification is PROPORTIONATE — the default-path-unchanged
  proof + the slice's own goldens/tests + (for the writer-reuse slice) B1's ELF goldens re-green
  byte-identical. **NO local 3-gen-fixpoint or paranoid for the mechanical slices** — CI does it.
- **FIXPOINT is structural, not re-proven locally per slice.** B2 is purely ADDITIVE files + one
  `NativeTarget` arm whose default (arm64) path is verbatim, so the default output is byte-unchanged by
  construction; CI's fixpoint leg confirms it once.
- **100% coverage on new code (definition-of-done).** The encoder is highly branchy (per-`MInstRiscv`,
  per-width base-vs-`*w`, per-`MCond` compare/branch, the six formats, the B/J scramble) — cover every
  encoded case + every honest-stop arm via goldens; a genuinely unreachable arm is justified in the PR.
- **VM-gotcha watch** (dense word/byte-work, the A4/B1 list carries over): (a) build words in `u32`,
  narrow to `byte` only at the last LE-emit step; (b) do all bit-slicing/scramble in `u32`; (c) no
  `x = match {…return}` — use `let x = match {…}` then act; (d) a NEGATIVE `to u32`/`to u8` **panics** —
  the B/J displacement scramble MUST route through `twos_field` (never cast a negative `i64` to `u32`);
  (e) `flags` is a reserved keyword — the `e_flags` field/local is fine as `e_flags`/`sh_flags`; (f)
  struct-literals span lines with a leading-field-per-line layout.
- **Bootstrap-seed lane.** B2 uses ONLY existing syntax (new `.tks` files + additive dispatch + a
  behavior-preserving writer refactor), so the stable-seed lane parses all of `src/` — no staged-syntax
  hazard.

### 11.5 Ritual points (where the FULL CI gate must pass)

- After **B2-1** (abi + core-neutrality proof): full CI gate + the neutrality unit test green — the
  cheapest proof the shared core carries no AAPCS64/SysV assumption.
- After **B2-5** (encoder core) and **B2-6** (branch/frame/reloc): full CI gate + the
  assembler-cross-checked goldens green — **the bit-scrambled B/J immediates are the riskiest bit-work,
  every offset assembler-verified**.
- After **B2-7** (ELF writer reuse): full CI gate + B1's `emit_elf` goldens byte-identical (the reuse
  guardrail) + `readelf`/`llvm-readobj` round-trip green on the emitted riscv `.o` (any host,
  machine-free).
- The **KEYSTONE full CI ritual at B2-8**: the whole gate — both engines + fixpoint + the **re-enabled
  qemu-riscv64 C-vs-own leg green** (executing the riscv ELF under emulation) + the macOS byte-test
  lane green + both prior differentials (arm64 Mach-O, x86-64 ELF) unchanged. **#387 CLOSES here.**

---

## 12. Integration + merge strategy (dono ruling 2026-07-10)

From B2 on, sub-sub-PRs **MERGE** into the mini-umbrella (not squash — preserve the B2-1…B2-8 slice
history), and the mini-umbrella **SQUASHES** into the umbrella:

- Each `B2-N` is a sub-sub-PR on `fix/issue-387-<slug>-N` (worktree-isolated) with base the **B2
  mini-umbrella branch** (`fix/issue-387-riscv64`, tracked by **#401**). When green it **merges**
  (merge-commit, history preserved) into the mini-umbrella.
- The mini-umbrella **#401** accumulates B2-1…B2-8; when the KEYSTONE CI ritual is green it **squashes**
  into the umbrella `remodel/backend-build` (one squashed commit per mini-umbrella, the umbrella BUILD
  field bumped per merge, seed re-downloaded after AllGreen + Build-Release green).
- The integrator (not the architect) drives the squashes; the verifier runs the gate; agents draft,
  never merge.

---

## 13. The sub-slice decomposition (ordered, each gate-able)

```
B1 (done, #386) ─▶ B2-1 ─▶ B2-2 ─▶ B2-3 ─▶ B2-4 ─▶ B2-5 ─▶ B2-6 ─▶ B2-7 ─▶ B2-8   [#387 closes]
                   abi +    riscv IR  isel     regalloc  encoder  frame/   ELF      target-dispatch
                   core-   (MInstRiscv) 3-addr  riscv    (6 fmts, branch/  writer   + qemu diff
                   proof              +native   shell    B/J      reloc    REUSE
                                      div/rem            scramble pair)
```

- **B2-1 · `abi_riscv64.tks` + core-neutrality proof** — `riscv64_lp64d()` descriptor + the pool
  helpers; a unit test driving the SHARED `linear_scan` under it (§3). **Proven by:** the neutrality
  test + descriptor asserts. No RISC-V instruction yet.
- **B2-2 · `minst_riscv.tks`** — the `MInstRiscv` variant + `MAluOpRiscv`/`RelocKindRiscv` +
  `MFuncRiscv`/`MBlockRiscv`/`MModuleRiscv` + builders + printer, reusing the neutral `MReg`/`MCond`/
  `MMem` (§4.1). **Proven by:** printer golden tests + type surface.
- **B2-3 · `isel_riscv.tks`** — `select_module_riscv`: three-address ALU (§4.2, the arm64 shape),
  native one-instruction div/rem (§4.3), the per-`MCond` compare value-sequence + the single-instruction
  fused branch (§4.4), the `auipc`+`addi` `MLlaRiscv` address pair + the `MCallRiscv` pair (§4.5), LP64D
  arg/result (D-A). **Proven by:** golden `MInstRiscv`-dump + the shared LIR-interp equivalence over the
  corpus subset.
- **B2-4 · `regalloc_riscv.tks`** — `inst_regs_riscv`/`map_minst_riscv`/`rewrite_inst_riscv`/
  `regalloc_func_riscv`/`regalloc_module_riscv` calling the shared scan core (§5). **Proven by:**
  regalloc-riscv tests (no live-range overlap in a physreg) + `all_physical_riscv` + a spill fixture.
- **B2-5 · `encode_riscv.tks` core** — `EncWordRiscv`, `RelocKindRiscv`, `riscv_reloc_type`,
  `encode_inst_word_riscv` for the R/I/S/U integer/memory cases (§6.4), the `B2-fp` stop. **Proven by:**
  per-`MInstRiscv` word goldens, assembler-cross-checked. No e2e.
- **B2-6 · frame + fixed-32 branch fixup + relocs** — `compute_frame_layout_riscv` + prologue/epilogue
  (§6.5), block word-offset recording + the B/J scramble patch pass (§6.3, `btype_scramble`/
  `jtype_scramble` via `twos_field`), `RelocRiscv` collection (the `Call` reloc + the `MLlaRiscv`
  HI20/LO12 pair + the synthesized `.text` label, §7.2), `encode_func_riscv`/`encode_module_riscv`
  (concat + rebase + symbol table). **Proven by:** two-block branch scramble goldens + the reloc-record
  + label goldens. No object.
- **B2-7 · ELF writer REUSE** — extract `emit_elf_object(ElfObject)` + `ElfRelocReq` from
  `objfile_elf.tks` (§7.1), the `.text`-label-local pass (§7.2), the `emit_elf` adapter (byte-identical),
  the `emit_elf_riscv` adapter, the `B2-globals` stop. **Proven by:** B1 x86 goldens byte-identical +
  RISC-V header/symtab/rela goldens + `readelf`/`llvm-readobj` well-formedness (machine-free, any host).
- **B2-8 · target dispatch + qemu differential (KEYSTONE)** — `NativeTarget::Riscv64Linux` +
  `target_from_name` + `emit_native_riscv` (§8.1), the `TEKO_CC`/cross-linker seam (D-D), the
  re-enabled + re-scoped `riscv64-qemu` lane running `diff_c_own.sh --target riscv64-linux` under
  `qemu-riscv64-static` (§8.2). **Proven by:** `own-riscv == C-riscv` exit codes over the corpus under
  qemu + the macOS byte-test lane. **#387 CLOSES.**

**Files:** new `src/backend/abi_riscv64.tks`, `minst_riscv.tks`, `isel_riscv.tks`, `regalloc_riscv.tks`,
`encode_riscv.tks`, and their `*_test.tkt`; touched `src/backend/objfile_elf.tks` (extract
`emit_elf_object` + `ElfObject`/`ElfRelocReq` + the `.text`-label pass + the `emit_elf` adapter — B1
bytes preserved), `src/build/project.tks` (`NativeTarget::Riscv64Linux` + `target_from_name` +
`emit_native_riscv` + the `TEKO_CC`/cross-linker seam), `scripts/diff_c_own.sh` (the riscv qemu mode),
`.github/workflows/native.yml` (re-enable + re-scope the `riscv64-qemu` lane). **Reuses unchanged:**
`lir::lower_program`, the neutral `minst` surface (`MReg`/`MRegClass`/`MCond`/`MMem`/`vreg`/`preg`), the
whole `AbiDescriptor` surface, the register-abstract scan core
(`linear_scan`/`candidate_pool`/`assign_lookup`/`LiveInterval`/`IntervalSet`/`ScanResult`/`SubstEntry`),
`Symbol`, `twos_field`, the byte-buffer helpers (`emit_u32_le`/`emit_u64_le`/`emit_str_bytes`/
`pad_to_mult`/`build_strtab`/`align_up`), and — the headline — the WHOLE ELF writer body via
`emit_elf_object`.

---

## Appendix · F/D float encodings (for the `B2-fp` fast-follow)

RV64 LP64D passes/returns floats in `fa0..fa7` (`f10..f17`). Base OP-FP (opcode 0x53) forms (`.d`
double, funct7 low bit selects fmt): FADD.D `funct7=0x01`, FSUB.D `0x05`, FMUL.D `0x09`, FDIV.D `0x0D`;
FEQ.D/FLT.D/FLE.D `funct7=0x51` (funct3 2/1/0); FCVT.D.L (i64→f64) `funct7=0x69`, FCVT.L.D (f64→i64)
`funct7=0x61`; FCVT.S.D `0x20`, FCVT.D.S `0x21`; FMV.D / FSGNJ.D (for FNEG via FSGNJN.D) `funct7=0x11`;
FLD/FSD (loads/stores) opcode 0x07/0x27. The implementer re-derives each against
`clang --target=riscv64-linux-gnu -march=rv64g -c` before shipping the fast-follow, exactly as the
`A4-fp`/`B1-fp` appendices prescribe.
