# B1 / N3 — x86-64 SysV ABI + ELF object emission (crumb plan)

**Status:** DESIGN (doc-only). Sub-PR of the 0.3 own-AOT-backend wave. Issue **#386**. Branch
`fix/issue-386-x8664` (the B1 mini-umbrella; base = the umbrella `remodel/backend-build` carrying
A1→A4 + #443, seed `teko 0.3.0.6-beta`). Base for the design:
`docs/design/own-backend-architecture.md` §2.1 (target #3, x86-64 SysV ELF) + §3.3/§3.4/§3.5,
`docs/design/backend-a4-encoder.md` (the proven encoder/object/differential pattern this MIRRORS),
grounded in the merged code (`src/backend/{minst,isel_arm64,regalloc,abi_aapcs64,encode_arm64,
objfile_macho,minst_interp}.tks`, `src/build/project.tks`, `scripts/diff_c_own.sh`).

> This is a PLAN. It brings the **SECOND ISA** into the own AOT backend: x86-64 SysV on Linux ELF.
> It proves the pipeline's target-independence claims (`own-backend-architecture.md` §2.2 — "N3–N5
> reuse the whole LIR + lowering + regalloc frame and swap only isel/encoder/objfile") and feeds
> **E1** (the ELF static linker, whose musl-static lane is the zero-toolchain goal). It does NOT
> retire the C backend, does NOT add the `--backend`/`--target` flags (D1/#390 owns them — B1 extends
> the temporary env seam with `TEKO_TARGET`), and does NOT build the own linker (Phase E/#226 — B1
> links via `cc`-as-linker on Linux). The hard tail (SSE float, data globals, argv forwarding) is
> scoped forward as **named** honest-stops for 0.3.1, mirroring A4's discipline exactly.

---

## 0. TL;DR — the recommended shape

- **IR decision (the flagged tension) — PARALLEL IR, resolved law-first, NO HALT.** A new
  `minst_x86.tks` (`MInstX86` variant + x86 opcode enums + `RelocKindX86`, plus `MFuncX86`/
  `MBlockX86`/`MModuleX86`) that **reuses the ISA-neutral** `MReg`/`MRegClass`/`MCond`/`MMem`/
  `vreg`/`preg`/`AbiDescriptor` and the **register-abstract linear-scan core** (`linear_scan`/
  `candidate_pool`/`ScanResult`/`assign_lookup`) verbatim. Generalizing `MInst` LOSES: it blasts
  three frozen total-matches (`minst_interp`, `encode_arm64`'s `encode_inst_word`, `regalloc`'s
  `inst_regs`/`map_minst_regs`) and risks the shipped A4 differential — the opposite of
  smallest-blast (M.1) and of a settled, byte-tested reference (M.5). Parallel IR is exactly what
  `own-backend-architecture.md` §3.3 prescribes ("per-ISA modules, LIR → MInst keyed by target").
  (§2.)
- **Two-address strategy (THE x86 structural difference) — the copy is inserted at ISEL.** For
  `r = a OP b`, `isel_x86_64` emits `mov dst, a` then the read-modify-write `OP dst, b`, modeling
  the RMW op's `dst` as **both a def and a use** — exactly the shape arm64's `MMovK` already carries
  (`inst_regs` at `regalloc.tks:421` reports `ir_def_use(x.dst, one_reg(x.dst))`). The shared
  regalloc handles it UNTOUCHED; the two-address knowledge stays entirely inside the ISA that has it.
  A regalloc-level operand tie is REJECTED (bleeds an x86 concept into the shared core). (§4.2.)
- **Variable-length strategy — rel32-ALWAYS, so encode stays single-pass + patch.** Every
  intra-function jump is `Jcc rel32` (6 bytes) / `JMP rel32` (5 bytes); every call is `CALL rel32`
  (5 bytes). Each instruction's size is fully determined at emit time → **no branch relaxation /
  iteration**. The encoder builds a `[]byte` per function while recording each block's byte-offset,
  then a patch pass writes the 4-byte `rel32` fields — structurally identical to A4's word-patch,
  just byte-granular. (§6.2.)
- **Sub-slice order (each independently gate-able, smallest-safe-step):**
  `B1-1` abi-sysv64 + regalloc-core-neutrality proof → `B1-2` `minst_x86` IR → `B1-3` isel-x86
  (two-address + fixed-reg div + rip-lea + SysV args) → `B1-4` regalloc-x86 shell (reuse the scan
  core) → `B1-5` encoder core (variable-length + goldens) → `B1-6` branch layout + rel32 fixups +
  relocs → `B1-7` ELF64 object writer → `B1-8` target dispatch + the linux-x86_64 differential lane
  (**#386 CLOSES here**).
- **Differential placement — LINUX runs, macOS byte-tests.** The executing `C-native == own-native`
  differential runs on the **linux-x86_64** runner (the `native.yml` lane that can RUN ELF
  artifacts). On the macOS-arm64 dev host the encoder goldens (byte-vectors, assembler-cross-checked
  with `clang -c -target x86_64-linux-gnu`) run machine-free in the unit gate, and `llvm-readobj`/
  `llvm-objdump` (cross-format on macOS) round-trips the emitted `.o`; execution honest-skips.
- **No genuine HALT.** Every decision resolves law-first and is recorded (§10). One DRY tension (the
  per-ISA `compute_intervals` walk) is resolved as law-consistent per-ISA mirroring and a future
  extraction is REPORTED up (§10, R-1), not turned into a blocker.

---

## 1. The assessed starting point — what B1 REUSES vs what it PARALLELS

B1 is the first proof that the A-slice architecture is target-independent. The grounding audit,
file by file:

### 1.1 REUSED VERBATIM (ISA-neutral — no copy, no change; same `teko::backend` namespace)

| Symbol(s) | Home | Why neutral |
|---|---|---|
| `MRegClass = enum { GPR; FPR }` | `minst.tks:11` | x86 GPR + XMM map onto GPR/FPR |
| `MReg { id; reg_class; is_phys }`, `vreg`, `preg` | `minst.tks:22,874,886` | an abstract register id; x86 ISA numbers fit |
| `MMem { base; offset }` | `minst.tks:32` | base+disp; x86 index/scale is a deferred extension (§6.4) |
| `MCond = enum { EQ;NE;LT;LE;GT;GE;LO;LS;HI;HS }` | `minst.tks:52` | abstract condition codes → x86 `Jcc`/`SETcc` suffixes 1:1 |
| `AbiDescriptor` + `arg_reg`/`allocatable_pool`/`is_caller_saved`/`is_callee_saved`/`spill_scratch` | `abi_aapcs64.tks:14,170,187,199,214,231` | pure register-file descriptor; B1 clones the PARTITION only |
| `LiveInterval`/`IntervalSet`/`ScanResult`/`RegAssignment`/`InReg`/`Spilled`/`AssignLookup`/`SubstEntry` | `regalloc.tks:449,484,936,902,919,1291,1383` | register-abstract; never mention an `MInst` case |
| `linear_scan`/`candidate_pool`/`assign_lookup` | `regalloc.tks:1245,1110,1361` | operate on `IntervalSet`, not `MInst` — the HARD algorithm, single-sourced |
| `Symbol { name; defined; local; sect; offset }` | `encode_arm64.tks:1787` | a generic object-symbol record; ELF binds it the same way |
| `emit_u32_le` | `encode_arm64.tks:175` (pub) | byte-buffer LE primitive |
| `emit_u64_le`/`emit_str_bytes`/`pad_to_mult`/`build_strtab`/`StrTab` | `objfile_macho.tks:11,31,83,119,99` | byte-table helpers (non-pub but SAME-ns visible — proven by `align_up` reuse) |
| `align_up` | `encode_arm64.tks:804` (non-pub, same-ns) | offset rounding |

> **Namespace fact (load-bearing).** `objfile_macho.tks:323` calls the **non-`pub`** `align_up`
> defined in `encode_arm64.tks:804`. Therefore in Teko `pub` = cross-NAMESPACE export and a bare
> `fn` = namespace-private (visible to every file in `teko::backend`). B1's new files live in the
> SAME `teko::backend` namespace, so they reuse all of the above with **no `use` and no re-export** —
> and, per the known silent-bare-name-collision gotcha, they MUST NOT re-declare any of these names.
> Every parallel x86 type/fn therefore carries an `X86`/`_x86` suffix (§1.3).

### 1.2 arm64-SPECIFIC — the parts B1 PARALLELS (never reuses, never generalizes)

| Symbol(s) | Home | Why arm64-only |
|---|---|---|
| the 29 `MInst` cases + `MInst` variant | `minst.tks:106..787` | `MMovZ`/`MMovK`/`MMovN`/`MAdrp`/`MAddLo`/`MCSet`/`MMSub` are pure AArch64 |
| `MAluOp`/`MFAluOp`/`MExtOp`/`MCvtOp`/`MMemSize`/`MRelocKind` | `minst.tks:62..97,42` | AArch64 mnemonics + `{PageHi;PageLo;Call}` relocs |
| `MFunc`/`MBlock`/`MModule` | `minst.tks:796,823,847` | embed `[]MInst` (arm64) |
| `inst_regs`/`map_minst_regs`/`rewrite_inst` + the `MFunc`-walking `compute_intervals`/`rewrite_*`/`regalloc_func`/`regalloc_module`/`all_physical` | `regalloc.tks:407,1464,1622,…` | total matches over the 29-case `MInst` |
| `encode_inst_word`/`encode_func`/`encode_module`/`compute_frame_layout`/`emit_prologue`/`emit_epilogue`/`FrameLayout`/`EncWord`/`Reloc`/`EncodedFunc`/`EncodedModule` | `encode_arm64.tks` | fixed-4-byte-word encoder; `EncWord` is a `u32`; `Reloc.kind: MRelocKind` |
| `minst_interp` | `minst_interp.tks` | total match over arm64 `MInst` semantics |
| `emit_macho` + `MachoLayout` | `objfile_macho.tks` | Mach-O container |
| `select_module` | `isel_arm64.tks:1866` | AArch64 selection (three-address, ADRP+ADD, MOVZ/MOVK chains) |

### 1.3 The naming convention (silent-collision-safe)

Every parallel x86 declaration lives in `teko::backend` with an explicit suffix so it CANNOT collide
with the frozen arm64 name (the bare-name-collision gotcha is silent — no diagnostic). The set:
`MInstX86` (variant) with per-case structs `MMovX86`/`MAluRmX86`/`MAluImmX86`/`MLeaRipX86`/… ;
`MAluOpX86`/`RelocKindX86` ; `MFuncX86`/`MBlockX86`/`MModuleX86` ; `select_module_x86` ;
`inst_regs_x86`/`map_minst_x86`/`rewrite_inst_x86`/`regalloc_func_x86`/`regalloc_module_x86` ;
`encode_module_x86`/`RelocX86`/`EncodedFuncX86`/`EncodedModuleX86`/`FrameLayoutX86` ; `emit_elf`.
The neutral reuse (`MReg`, `linear_scan`, `Symbol`, `emit_u32_le`, …) stays bare.

---

## 2. The IR decision — parallel `minst_x86` (law-first, NO HALT)

The task flags this as a potential HALT ("if generalizing MInst vs parallel IR has no clear
winner"). It has a **clear winner**; recorded, not opened.

**Chosen: a parallel `minst_x86.tks` IR.**

- **M.1 (small/orthogonal) + smallest blast radius.** Generalizing `MInst` to span both ISAs forces
  new cases (or an abstract re-shape) into a variant that FOUR frozen total-matches exhaustively
  cover: `minst_interp` (`minst_interp.tks`), `encode_inst_word` (`encode_arm64.tks:1416`),
  `inst_regs` (`regalloc.tks:407`), `map_minst_regs` (`regalloc.tks:1464`). Every one would gain
  dead x86 arms or break — churning the shipped, byte-tested A4 reference and risking its
  differential. The parallel IR touches ZERO frozen code (it only ADDS files + reuses the neutral
  surface). Blast radius: new files only.
- **M.5 (one way to do it) is preserved, not violated.** The parallel IR is not a "second way to do
  arm64" — it is the ONE way to do x86, mirroring how `isel_arm64` is the one way to select arm64.
  The genuinely-shared, single-sourced asset — the linear-scan ALGORITHM and the `AbiDescriptor`
  contract — stays single-sourced (§5).
- **Grounded in the architecture of record.** `own-backend-architecture.md` §3.3 already fixes this:
  "Per-ISA modules, `LIR → MInst` keyed by target." §3.4: "Target-independent core parameterized by
  the per-ISA register file." B1 is the first realization of that split, not a new decision.

**The residual DRY cost** (the per-ISA `compute_intervals`/`rewrite_*` MFunc-walk is mirrored, not
shared) is real and is resolved law-consistently in §5 + REPORTED up in §10 (R-1) — it is the same
per-ISA mirroring `isel`/`encode` already embody, not a blocker.

---

## 3. `abi_sysv64.tks` — the SysV register file (B1-1)

The whole cost of a new ABI is cloning `AbiDescriptor`'s partition (`abi_aapcs64.tks:8-10`). SysV
AMD64: integer args `RDI,RSI,RDX,RCX,R8,R9`; FP args `XMM0..XMM7`; integer/FP return `RAX`/`XMM0`;
callee-saved `RBX,RBP,R12,R13,R14,R15`; everything else caller-saved. Register-id convention (the
`MReg` GPR numbering B1 adopts, matching the ELF/DWARF encoding order is NOT required — only internal
consistency between isel pins, the descriptor, and the encoder's ModRM field is): use the
**instruction-encoding order** `RAX=0,RCX=1,RDX=2,RBX=3,RSP=4,RBP=5,RSI=6,RDI=7,R8=8..R15=15` so
`enc_reg` is the identity (mirrors arm64's `enc_reg(r) = r.id`, `encode_arm64.tks:255`). FPR ids
`XMM0=0..XMM15=15`.

```teko
/**
 * sysv64 — the concrete System V AMD64 (x86-64 Linux/ELF) register-file
 * descriptor: RDI/RSI/RDX/RCX/R8/R9 integer arguments (index 0 = RAX result),
 * XMM0..XMM7 float arguments (index 0 = XMM0 result), RBX/RBP/R12..R15 callee-
 * saved, everything else caller-saved. The allocatable pools list caller-saved
 * first (scratch-cheap short values before a callee-save), excluding RSP/RBP
 * (the frame registers) and the reserved spill scratch. Register ids follow the
 * x86 instruction-encoding order (RAX=0,RCX=1,RDX=2,RBX=3,RSP=4,RBP=5,RSI=6,
 * RDI=7,R8..R15=8..15), so the encoder's ModRM/REG field is the id directly.
 * Clones `aapcs64`'s SHAPE with a SysV partition (Phase B, §2.2 of the
 * architecture doc).
 *
 * @return AbiDescriptor  the SysV AMD64 register file
 */
pub fn sysv64() -> AbiDescriptor {
    /*
     * gpr_arg      = [7(RDI),6(RSI),2(RDX),1(RCX),8(R8),9(R9)]  (index 0 = RAX(0) as result)
     * NOTE the result register is RAX(0), distinct from arg[0]=RDI(7): SysV's
     * result reg is NOT its first arg reg (unlike AAPCS64 where both are x0).
     * The descriptor's `gpr_arg[0]` is used by `arg_reg` for BOTH; §4.5 pins the
     * result to RAX(0) explicitly at the return site, so `gpr_arg` here lists the
     * ARGUMENT sequence and the isel result-pin names RAX directly.
     */
    …
}
```

> **Decision D-A (recorded).** SysV's **result register (RAX) differs from its first argument
> register (RDI)** — unlike AAPCS64 where both are `x0` and `arg_reg(...,0)` doubles as the result.
> `arg_reg`/`allocatable_pool` are UNCHANGED (they already take the class + index). The result pin is
> not read from `gpr_arg[0]`; isel pins `RAX`(0) at the return/`ret_reg` site directly (§4.5). This
> is a one-line isel difference, NOT an `AbiDescriptor` shape change — the descriptor stays the whole
> cost of the ABI (M.1). Recorded, not open.

**Spill scratch.** SysV has no dedicated IP0/IP1. Reserve `R10`,`R11` (caller-saved, never
argument/callee-saved) as `gpr_spill_scratch` (sized to `MMSub`'s max-three-simultaneous-uses is an
arm64 concern; x86's widest spilled-use instruction is the two-address RMW `OP dst,src` with the
`src` reload + the `dst` reload/store — two scratch, so `[R10,R11]` suffices; §5.3). `fpr_spill_scratch`
= `XMM14`,`XMM15`. `spill_slot_bytes`/`spill_slot_align` = 8.

**Regalloc-core-neutrality proof (the B1-1 gate).** B1-1 ships `abi_sysv64.tks` + a unit test that
drives the SHARED `linear_scan` over a hand-built `IntervalSet` under `sysv64()` and asserts: (a) the
allocatable order is caller-saved-first; (b) a call-crossing interval avoids caller-saved (RAX/RCX/…)
and lands on a callee-saved (RBX/R12/…); (c) `is_callee_saved(sysv64(), preg(3/*RBX*/,GPR))` is true
and `preg(0/*RAX*/,GPR)` is false. This proves the core carries no AAPCS64 assumption BEFORE any x86
instruction exists — the smallest possible first step.

---

## 4. `isel_x86_64.tks` — selection + the two-address strategy (B1-3)

Mirrors `isel_arm64.tks`'s structure exactly: a `SelCtxX86` (in-progress `MFuncX86`, the `class_of`
side-table `VInfoTable` — REUSED, it is neutral, `isel_arm64.tks:31`), the RPO block interplay with
#443 reachability, and a total per-case walk over the LIR. Four x86-specific selection shapes:

### 4.1 The `MInstX86` alphabet (declared in `minst_x86.tks`, B1-2)

The load-bearing subset for the integer/control/memory corpus (the FP family is the `B1-fp` stop):

| LIR | `MInstX86` case | x86-64 form |
|---|---|---|
| `LConstInt` | `MMovImmX86 { dst; imm64; wide }` | `mov r32/r64, imm` — or `movabs r64, imm64` for a 64-bit literal |
| `LBin` add/sub/and/or/xor | `MMovX86`(copy) + `MAluRmX86 { op; dst; src; wide }` | two-address `mov dst,a` ; `add/sub/and/or/xor dst,b` (§4.2) |
| `LBin` mul | `MMovX86` + `MImulX86 { dst; src; wide }` | two-operand `imul dst, src` (two-address, no fixed reg) |
| `LBin` sdiv/udiv/srem/urem | `MDivSeqX86` (fixed-reg sequence) | pin RAX; `cqo`/`xor edx,edx`; `idiv/div src`; result RAX(quot)/RDX(rem) (§4.4) |
| `LBin` shifts | `MShiftX86 { op; dst; wide }` (count in CL) | `mov cl, b` ; `shl/shr/sar dst, cl` |
| `LBin` compares (value) | `MCmpX86 { a; b; wide }` + `MSetccX86 { dst; cond }` | `cmp a,b` ; `setcc dst8` ; `movzx dst, dst8` |
| `LBin` compares (fused) | `MCmpX86` + `MJccX86 { cond; target }` | `cmp a,b` ; `jcc block` (A2-3 fusion analog) |
| `INeg`/`INot` | `MMovX86` + `MNegX86`/`MNotX86 { dst; wide }` | `neg dst` / `not dst` (two-address unary) |
| `LLoad`/`LStore` | `MLoadX86`/`MStoreX86 { size; dst/src; mem }` | `mov reg, [base+disp]` / `mov [base+disp], reg` |
| `LAlloca` | `MFrameAddrX86 { dst; slot }` | `lea dst, [rsp+off]` (resolved via `FrameLayoutX86`) |
| `LGlobalAddr`/`LFuncAddr` (value) | `MLeaRipX86 { dst; sym; kind }` | **ONE** `lea dst, [rip+disp32]` + one reloc (§4.3) |
| `LJump` | `MJmpX86 { target }` | `jmp block` |
| `LCall` | `MCallX86 { sym; uses; ret_reg; has_result; variadic }` | `call sym` + reloc |
| `LCallIndirect` | `MCallIndX86 { target; uses; ret_reg; has_result }` | `call reg` |
| `LRet` | `MRetX86 { }` | epilogue + `ret` (value pre-pinned to RAX/XMM0) |
| `IToF`/`FToI`/`FToF`/float ALU/`FNeg` | — | **named honest-stop `B1-fp`** (SSE, 0.3.1) |

### 4.2 The two-address strategy — COPY AT ISEL (the recommended resolution)

x86 `add dst, src` computes `dst = dst + src`, destroying `dst`; arm64 `add dst, a, b` is
three-address. The isel bridges the gap by **inserting a copy** so the destination starts life
holding the left operand:

```teko
/**
 * select_alu_x86 — the two-address `LBin` selection shared by every integer
 * arithmetic/bitwise x86 ALU case. x86's `OP dst, src` is destructive
 * (`dst = dst OP src`), so a three-address `r = a OP b` lowers to a COPY of `a`
 * into the result register followed by the read-modify-write op: `mov dst, a` ;
 * `OP dst, b`. The RMW op reports `dst` as BOTH a def and a use (§5.2), exactly
 * the shape arm64's `MMovK` already carries — so the shared regalloc handles it
 * unchanged. The extra copy is an honest cost (M.4) a later coalescer removes;
 * correctness-first inserts it whenever `dst != a` (a same-register self-op
 * skips it). The whole two-address concept lives HERE, in the ISA that has it —
 * never in the shared allocator (a regalloc operand-tie is rejected, §10 D-B).
 *
 * @param SelCtxX86 ctx0  the context to extend
 * @param lir::LInst inst  the enclosing instruction (its result VReg)
 * @param MAluOpX86 op  the x86 ALU mnemonic (Add/Sub/And/Or/Xor)
 * @param u32 a  the left-operand VReg (copied into dst)
 * @param u32 b  the right-operand VReg (the RMW source)
 * @param bool wide  true for the 64-bit r64 form, false for the 32-bit r32 form
 * @return SelCtxX86  the advanced context
 */
fn select_alu_x86(ctx0: SelCtxX86, inst: lir::LInst, op: MAluOpX86, a: u32, b: u32, wide: bool) -> SelCtxX86 {
    let dst = vreg(inst.result, MRegClass::GPR)
    let ctx1 = selctx_x86_set_vinfo(ctx0, inst.result, MRegClass::GPR, wide)
    let ctx2 = selctx_x86_emit(ctx1, mov_x86(wide, dst, vreg(a, MRegClass::GPR)))
    selctx_x86_emit(ctx2, alu_rm_x86(op, wide, dst, vreg(b, MRegClass::GPR)))
}
```

Rejected alternatives (recorded §10 D-B): a **regalloc operand-tie constraint** (bleeds a
target-specific concept into the shared scan core — bigger blast, violates the descriptor-only ABI
contract) and a **two-address normalization pre-pass** (an extra module for what one isel line does).
The copy-at-isel mirrors `select_rem`'s existing two-instruction idiom (`isel_arm64.tks:619`) — a
proven pattern in the codebase.

### 4.3 Address materialization COLLAPSES to one instruction

The arm64 `ADRP`+`ADD` pair (two relocs `PageHi`/`PageLo`, `minst.tks:611,632`) becomes a **single**
`lea dst, [rip + disp32]` with **one** `R_X86_64_PC32`/`PLT32` reloc on x86-64. `MLeaRipX86` carries
`{dst; sym; kind}` and lowers to one `EncInst` + one `RelocX86` — a genuine simplification the ELF
writer inherits (§7.3).

### 4.4 Division/remainder — the divide models as a call (corrected B1-4 contract)

x86 `idiv src` divides `RDX:RAX` by `src`, quotient→RAX, remainder→RDX. isel emits ONE `MDivSeqX86`
macro carrying the sequence's operands (`dst`, dividend `a`, divisor `b`); the encoder (B1-5) expands
it to `mov rax, a` ; `cqo` (signed) or `xor edx,edx` (unsigned) ; `idiv/div b` ; then `mov dst, rax`
(quot) or `mov dst, rdx` (rem). At **regalloc** time the stream therefore holds only the macro — there
are NO explicit RAX/RDX pin instructions to build `is_fixed` `LiveInterval`s from, and the frozen
`InstRegs` (`regalloc.tks:260`) carries a single `def_reg` and no clobber list, so the RAX + RDX
clobbers cannot be spelled as two fixed defs. So `inst_regs_x86(MDivSeqX86)` reports
**`ir_call(dst, true, [a, b])` — `is_call = true`** (the corrected contract, overriding an earlier
"fixed RAX/RDX intervals" wording; from the B1-3 adversarial review). Modelling the divide as a call
makes every value live ACROSS it avoid the whole caller-saved set (RAX + RDX included, via
`candidate_pool`, `regalloc.tks:1110`); and the DIVISOR `b` is itself protected because its last use
IS the divide point and A3's `crosses_call` uses the `(start, end]` boundary (end == the call point
counts as crossing), so `b` also lands off RAX/RDX. Sound, mildly pessimistic (a value that could
have reused a caller-saved register the divide does not actually clobber is conservatively pushed to a
callee-save); a fixed-clobber refinement is a named **0.3.1** item.

### 4.5 SysV argument/return lowering

`arg_reg(sysv64(), class, index)` gives RDI/RSI/… (int) and XMM0..7 (float) — the descriptor mirror
already exists (`abi_aapcs64.tks:170`, neutral). The **result** pin is `preg(0/*RAX*/, GPR)` /
`preg(0/*XMM0*/, FPR)` planted at the `LRet`/`MCallX86.ret_reg` site (D-A). Args past the register
window are a `B1-args`-adjacent stack-arg stop shared with A4's `A4-args` posture (the corpus stays
within the register window).

---

## 5. `regalloc_x86.tks` — the thin x86 shell over the SHARED scan core (B1-4)

The register allocator splits cleanly into **the algorithm (shared, single-sourced)** and **the
MInst-facing adapter (per-ISA, mirrored)** — exactly the isel/encode per-ISA split.

### 5.1 What is SHARED (called, never copied)

`linear_scan(abi, ivals)` (`regalloc.tks:1245`), `candidate_pool` (`:1110`), `assign_lookup`
(`:1361`), `LiveInterval`/`IntervalSet`/`ScanResult`/`SubstEntry` — all operate on `IntervalSet`
(register-abstract), never on an `MInst` case. B1-4 makes `linear_scan` `pub` within the namespace
if it is not already reachable, otherwise adds one thin same-ns entry; **no algorithm is re-written**.
This is the DRY-critical, subtle-eviction code — it stays single-sourced (M.5).

### 5.2 What is MIRRORED (per-ISA adapter, small + mechanical)

`inst_regs_x86(inst: MInstX86) -> InstRegs` — a total match over `MInstX86` producing the neutral
`InstRegs` (`regalloc.tks:260`, reused). The two-address RMW op reports `ir_def_use(dst, [dst, src])`
(the def is also the first use — the `MMovK` precedent at `regalloc.tks:421`); `MDivSeqX86` reports
the RAX/RDX defs+uses; `MCallX86` reports `ir_call`. `map_minst_x86`/`compute_intervals_x86`/
`rewrite_inst_x86`/`rewrite_block_x86`/`rewrite_func_x86`/`regalloc_func_x86`/`regalloc_module_x86`
mirror `regalloc.tks`'s `MFunc`-walking shells over `MFuncX86`/`MBlockX86`. These are mechanical
mirrors (the same kind of per-ISA duplication `isel`/`encode` already are), NOT a re-implementation of
the scan. The DRY residue is REPORTED up (§10 R-1).

### 5.3 Spill rewrite

`rewrite_inst_x86` mirrors `rewrite_inst` (`regalloc.tks:1622`): reload spilled uses into
`spill_scratch(sysv64(), GPR, nth)` (R10/R11), substitute, store a spilled def. x86 CAN fold a
reload into the memory operand of an RMW op (`add dst, [rsp+off]`) — a later optimization; the MVP
uses the same reload-to-scratch shape as arm64 (honest, uniform). FPR spill = the inherited
`A3-fpr-spill` stop (`detect_fpr_spill` analog).

---

## 6. `encode_x86_64.tks` — the variable-length encoder (B1-5 / B1-6)

### 6.1 The buffer model — `[]byte` per function + byte-offset fixups

arm64 built `[]u32` (whole-word patches, `encode_arm64.tks` §2.1). x86 is variable-length, so the
per-function buffer is `[]byte` and a fixup records the byte offset of the rel32 field:

```teko
/**
 * EncInstX86 — one x86 instruction's encoding outcome: its variable-length
 * byte string plus optional pending fixups. A position-independent instruction
 * resolves to just `bytes`. An intra-function branch carries a `rel32` fixup —
 * the byte offset WITHIN `bytes` of the 4-byte little-endian displacement field
 * and the destination machine-block id — patched once every block's byte offset
 * is known (§6.2). A symbol reference carries a `RelocX86` (the byte offset of
 * its rel32/abs field + the target symbol + kind).
 *
 * @since #386 B1-5
 */
pub type EncInstX86 = struct {
    /**
     * bytes — the instruction's encoded bytes (1..15), in order.
     */
    bytes: []byte
    /**
     * has_branch — true iff a `rel32` block displacement must be patched in.
     */
    has_branch: bool
    /**
     * branch_field_off — the byte offset within `bytes` of the 4-byte rel32
     * field (meaningful when `has_branch`).
     */
    branch_field_off: u32
    /**
     * target — the destination machine-block id (meaningful when `has_branch`).
     */
    target: u32
    /**
     * has_reloc — true iff this instruction owes a symbol `RelocX86`.
     */
    has_reloc: bool
    /**
     * reloc_field_off — the byte offset within `bytes` of the relocated field.
     */
    reloc_field_off: u32
    /**
     * reloc_sym — the relocation's target symbol (meaningful when `has_reloc`).
     */
    reloc_sym: str
    /**
     * reloc_kind — the relocation kind (meaningful when `has_reloc`).
     */
    reloc_kind: RelocKindX86
}

/**
 * encode_inst_x86 — encode one fully-physical `MInstX86` to its `EncInstX86`
 * (§6.3): a total per-case match over the `MInstX86` variant, mirroring
 * `encode_arm64.tks`'s `encode_inst_word`. `MFrameAddrX86` is resolved through
 * the supplied `FrameLayoutX86`; `MRetX86` returns the bare `ret` byte (the
 * epilogue is prepended by `encode_func_x86`). The FP family returns the NAMED
 * honest-stop `B1-fp` (0.3.1); the x86 immediate/frame forms are natively wide
 * (imm32 / movabs imm64 / sub rsp,imm32), so `B1-bigimm`/`B1-bigframe` are
 * near-unreachable and named only for symmetry (§9).
 *
 * @param FrameLayoutX86 layout  the function's frame layout (for MFrameAddrX86)
 * @param MInstX86 inst  the instruction to encode (all operands physical)
 * @return EncInstX86 | error  the encoding, or a named honest-stop
 */
fn encode_inst_x86(layout: FrameLayoutX86, inst: MInstX86) -> EncInstX86 | error { … }
```

### 6.2 Branch layout — rel32-ALWAYS (single emit pass + patch pass, no relaxation)

Because every jump/call uses the fixed-width rel32 form, each instruction's byte length is known at
emit time — so `encode_func_x86` needs NO iterative relaxation, structurally identical to A4-3:

1. **Emit pass** — prepend the prologue bytes; walk `f.blocks` in RPO order recording each block's
   **starting byte-offset** in a `block_byte_offset[block_id]` map; encode each instruction via
   `encode_inst_x86`, appending its `bytes` (branches carry `has_branch`+`branch_field_off`+`target`;
   symbol refs push a `RelocX86` at `current_byte_offset + reloc_field_off`); expand `MRetX86` to
   epilogue+`ret`, `MFrameAddrX86` to the resolved `lea`.
2. **Patch pass** — for each recorded branch fixup, compute
   `disp = block_byte_offset[target] − (patch_site_byte_offset + 4)` (x86 rel is relative to the
   NEXT instruction, i.e. the END of the 4-byte field) and write it as a signed little-endian
   `u32` into the four bytes at `patch_site_byte_offset`. Our functions are tiny, so the ±2 GB rel32
   range is never a concern (an overflow is an internal-invariant honest-stop, never expected).

Symbol relocations (`call`/`lea rip`) need no patch — the field stays zero and the `RelocX86`
(rebased at module assembly) drives the linker with `r_addend = -4` (§7.3).

### 6.3 The per-case encoding table (assembler cross-checked)

Every encoding below is the operand-zeroed template; the ModRM/REX/imm fields OR/append in. **The
implementer MUST re-derive each against `clang -c -target x86_64-linux-gnu` + `objdump -d` (or
`llvm-mc -triple=x86_64 --show-encoding`) — the table is the spec, the assembler is the oracle**, the
exact discipline A4 used. Two worked pins: `mov eax, 42` = `B8 2A 00 00 00`; `ret` = `C3`.

| `MInstX86` | x86-64 | bytes (template) | notes |
|---|---|---|---|
| `MMovImmX86` (imm32, r64) | `mov r64,imm32`(signext) | `REX.W(48) C7 /0 id` | or `mov r32,imm32` = `B8+rd id` (no REX) |
| `MMovImmX86` (imm64) | `movabs r64,imm64` | `REX.W(48) B8+rd io` | 10 bytes; covers the arm64 MOVZ/MOVK-chain case |
| `MMovX86` (reg→reg) | `mov r/m64,r64` | `REX.W 89 /r` | ModRM mod=11 |
| `MAluRmX86` Add/Sub/And/Or/Xor | `add/sub/… r/m64,r64` | `REX.W 01/29/21/09/31 /r` | two-address (§4.2) |
| `MImulX86` | `imul r64,r/m64` | `REX.W 0F AF /r` | two-operand, two-address |
| `MDivSeqX86` idiv/div | `cqo`(48 99)/`xor edx,edx`(31 D2) ; `idiv/div r/m64`(REX.W F7 /7 or /6) | multi-byte sequence, RAX/RDX pins |
| `MShiftX86` shl/shr/sar | `shl/shr/sar r/m64,cl` | `REX.W D3 /4//5//7` | count pre-moved to CL |
| `MNegX86`/`MNotX86` | `neg/not r/m64` | `REX.W F7 /3 or /2` | two-address unary |
| `MCmpX86` | `cmp r/m64,r64` | `REX.W 39 /r` | sets flags |
| `MSetccX86` | `setcc r/m8` + `movzx r32,r/m8` | `0F 90+cc /0` ; `0F B6 /r` | boolean materialize |
| `MLoadX86`/`MStoreX86` | `mov r64,[b+disp]` / `mov [b+disp],r64` | `REX.W 8B /r` / `REX.W 89 /r` | ModRM+disp8/disp32 (+SIB when base=RSP=4) |
| `MFrameAddrX86` | `lea r64,[rsp+off]` | `REX.W 8D /r` + SIB(24) + disp | base=RSP forces a SIB byte (`B1-sib`, always emitted) |
| `MLeaRipX86` | `lea r64,[rip+disp32]` (+reloc) | `REX.W 8D /r` mod=00 rm=101 + `id`(0) | one `R_X86_64_PC32`/`PLT32` |
| `MJmpX86` | `jmp rel32` | `E9 cd` | rel32 patched (§6.2) |
| `MJccX86` | `jcc rel32` | `0F 80+cc cd` | rel32 patched |
| `MCallX86` | `call rel32` (+reloc) | `E8 cd` | `R_X86_64_PLT32`, addend −4 |
| `MCallIndX86` | `call r/m64` | `FF /2` | |
| `MRetX86` | (epilogue) + `ret` | `C3` | epilogue prepended (§6.5) |
| FP family | ADDSD/CVTSI2SD/… | — | **named honest-stop `B1-fp`** (0.3.1) |

> **REX prefix + SIB traps (bake in).** (a) Any 64-bit-operand op needs `REX.W`(0x48); accessing
> R8..R15 needs `REX.B`/`REX.R`/`REX.X` extension bits — `enc_reg` returns the low 3 bits + the high
> bit routes into REX. (b) A memory operand with **base = RSP (id 4)** ALWAYS needs a SIB byte
> (ModRM rm=100 means "SIB follows"), so `MFrameAddrX86`/spill loads off RSP emit SIB `0x24`
> unconditionally — this is the single most common x86 encoding mistake; the golden vectors pin it.
> (c) `movzx`/`setcc` touch 8-bit registers — SIL/DIL/BPL/SPL need a REX prefix to address; the
> corpus's booleans land in RAX-class regs where the low-byte is `AL` (no REX needed), but the
> encoder must emit REX when the setcc destination is R8..R15 or SIL..DIL.

### 6.4 Memory addressing scope

`MMem { base; offset }` (reused) covers base+disp32 — enough for frame slots and field access. x86's
index*scale form is a **named deferral** (`B1-memindex`) — the N2 lowering emits only base+constant
offsets (arm64 folds the same way, `minst.tks:32`). Load-store size/sign mirrors arm64's `MMemSize`
via an x86 movzx/movsx choice.

### 6.5 Frame — SysV prologue/epilogue (mirrors A4-2's re-derived save-set)

`compute_frame_layout_x86(abi, f) -> FrameLayoutX86` mirrors `compute_frame_layout`
(`encode_arm64.tks:1079`): re-derive the callee-saved save-set by scanning the physical `MFuncX86`
(`inst_regs_x86` + `is_callee_saved`, the D-A/A4 §3.1 precedent — `MFuncX86` carries no
`used_callee_saved`), lay out slots honoring each `LAlloca` alignment, size the frame to 16 (SysV
16-byte alignment at call sites). **Prologue** (framed): `push rbp`(55) ; `mov rbp,rsp`(48 89 E5) ;
`sub rsp,#N`(48 81 EC id / 48 83 EC ib) ; one `push` per saved callee-saved reg. **Epilogue** (LIFO):
`pop` each ; `leave`(C9) (or `mov rsp,rbp;pop rbp`) ; `ret`(C3). The `exit(n)` keystone `main` is
**frameless** (a non-returning `call tk_exit`, no `MRetX86`) → `size=0`, empty prologue — the first
end-to-end binary needs only B1-5's core + B1-6's `call` reloc + B1-7. The 128-byte **red zone** is
IGNORED (an optimization; a deferral note `B1-redzone`, safe to skip — we always reserve frame).

---

## 7. `objfile_elf.tks` — the ELF64 relocatable writer (B1-7)

Mirrors `objfile_macho.tks`'s shape (a `compute_*_layout` pass then ordered emit), consuming an
ISA-agnostic `EncodedModuleX86` (the writer holds no x86 knowledge — E1 reuses it). Reuses the
same-ns byte helpers (`emit_u32_le`/`emit_u64_le`/`emit_str_bytes`/`pad_to_mult`/`build_strtab`).

### 7.1 The ELF64 object layout (bit-exact — the ground-truth table)

An `ET_REL` (relocatable) object for `EM_X86_64`. All little-endian. Sections in file order:
`[Ehdr][.text][.rodata][.symtab][.strtab][.shstrtab][.rela.text][section-header table]`;
`e_shoff` points at the SHT at the end.

**Elf64_Ehdr (64 bytes):**

| field | bytes | value |
|---|---|---|
| `e_ident[0..4]` | 4 | `7F 45 4C 46` (`\x7fELF`) |
| `e_ident[EI_CLASS]` | 1 | `2` (ELFCLASS64) |
| `e_ident[EI_DATA]` | 1 | `1` (ELFDATA2LSB) |
| `e_ident[EI_VERSION]` | 1 | `1` (EV_CURRENT) |
| `e_ident[EI_OSABI]` | 1 | `0` (ELFOSABI_SYSV) |
| `e_ident[8..16]` | 8 | `0` (padding) |
| `e_type` | 2 | `1` (ET_REL) |
| `e_machine` | 2 | `62` (EM_X86_64) |
| `e_version` | 4 | `1` |
| `e_entry` | 8 | `0` |
| `e_phoff` | 8 | `0` (no program headers in a relocatable) |
| `e_shoff` | 8 | file offset of the SHT (computed) |
| `e_flags` | 4 | `0` |
| `e_ehsize` | 2 | `64` |
| `e_phentsize`/`e_phnum` | 2+2 | `0`/`0` |
| `e_shentsize` | 2 | `64` |
| `e_shnum` | 2 | `7` (null,.text,.rodata,.symtab,.strtab,.shstrtab,.rela.text) |
| `e_shstrndx` | 2 | index of `.shstrtab` (`5`) |

**Elf64_Shdr (64 bytes each), the 7 sections:**

| idx | name | `sh_type` | `sh_flags` | `sh_link` | `sh_info` | `sh_addralign` | `sh_entsize` |
|---|---|---|---|---|---|---|---|
| 0 | `""` (null) | `0` SHT_NULL | 0 | 0 | 0 | 0 | 0 |
| 1 | `.text` | `1` PROGBITS | `0x6` ALLOC\|EXECINSTR | 0 | 0 | 16 | 0 |
| 2 | `.rodata` | `1` PROGBITS | `0x2` ALLOC | 0 | 0 | 8 | 0 |
| 3 | `.symtab` | `2` SYMTAB | 0 | idx(.strtab)=4 | **first-global idx** | 8 | 24 |
| 4 | `.strtab` | `3` STRTAB | 0 | 0 | 0 | 1 | 0 |
| 5 | `.shstrtab` | `3` STRTAB | 0 | 0 | 0 | 1 | 0 |
| 6 | `.rela.text` | `4` RELA | `0x40` INFO_LINK | idx(.symtab)=3 | idx(.text)=1 | 8 | 24 |

Each `Shdr` also carries `sh_name` (offset into `.shstrtab`), `sh_addr`=0 (relocatable), `sh_offset`
(file offset of the section data), `sh_size`.

**Elf64_Sym (24 bytes each):** `st_name`(4, offset into `.strtab`), `st_info`(1,
`(bind<<4)|type`), `st_other`(1, 0), `st_shndx`(2), `st_value`(8), `st_size`(8, 0 for the MVP).
- Index 0 = the reserved **null symbol** (all zero).
- **A local `.rodata` section symbol** (`st_info = (STB_LOCAL(0)<<4)|STT_SECTION(3) = 0x03`,
  `st_shndx = 2`, `st_value = 0`) — rodata relocations target THIS with an addend (§7.3), the ELF
  idiom that dodges per-string symbols + duplicate-symbol clashes (the A4-4 "local rodata symbols"
  finding, ELF-native form).
- Defined functions: `st_info = (STB_GLOBAL(1)<<4)|STT_FUNC(2) = 0x12`, `st_shndx = 1` (.text),
  `st_value = section-relative byte offset` (**NOT** rebased — ELF `st_value` in a relocatable is
  section-relative + `st_shndx` names the section, unlike Mach-O's absolute `n_value`; the A4-4
  `n_value`/section-addr bug is INAPPLICABLE here, and stating so is itself the requirement so no one
  ports the Mach-O rebase by reflex).
- Undefined (`tk_exit`, `tk_set_args`, `tk_region_*`): `st_info = 0x10` (GLOBAL\|NOTYPE),
  `st_shndx = 0` (SHN_UNDEF), `st_value = 0`.
- **Ordering (load-bearing):** local symbols FIRST (null, `.rodata` section sym), then global
  (functions), then undefined — and `.symtab`'s `sh_info` = **the index of the first non-local
  symbol** (ELF requires it; the analog of Mach-O's `iextdefsym`, the dysymtab ordering
  `objfile_macho.tks:160`). `count_local`/`count_defined` (`objfile_macho.tks:141,160`) port directly.

### 7.2 Relocations the corpus needs (Elf64_Rela, 24 bytes: `r_offset`(8), `r_info`(8), `r_addend`(8))

`r_info = (symbol_index << 32) | type`. `r_offset` = the `.text`-section-relative byte offset of the
patched field (module-rebased from the per-function offset, the A4-3 "reloc rebase" finding — ports
directly).

| `RelocKindX86` | source | ELF type | value | `r_addend` |
|---|---|---|---|---|
| `Plt32` | `MCallX86` (`call rel32`) | `R_X86_64_PLT32` | `4` | `-4` |
| `Pc32` | `MLeaRipX86` (`lea [rip+d]`) | `R_X86_64_PC32` | `2` | `-4` (+ rodata offset when targeting the `.rodata` section symbol) |
| `Abs64` | data global (deferred `B1-globals`) | `R_X86_64_64` | `1` | absolute |

`Plt32` resolves to a plain PC-relative call when there is no PLT (static link) — the correct choice
for both the `cc`-bootstrap and the future E1 static linker. A rodata `lea` uses `Pc32` against the
`.rodata` **section symbol** with `r_addend = rodata_byte_offset − 4`.

### 7.3 The writer entry

```teko
/**
 * emit_elf — assemble an x86-64 `ET_REL` ELF64 object from an
 * `EncodedModuleX86`: the `Elf64_Ehdr` + the seven section headers
 * (null/.text/.rodata/.symtab/.strtab/.shstrtab/.rela.text) + the section
 * images + the `Elf64_Sym` table (local-then-global-then-undef, `.rodata` as a
 * STT_SECTION symbol) + the `Elf64_Rela` table (PLT32/PC32, `.text`-relative
 * `r_offset`, addend −4). Enough for `cc`/`ld` to link into a runnable
 * executable against the C-built `teko_rt` (D-2 option 1 bootstrap). The writer
 * is ISA-agnostic (consumes only `EncodedModuleX86`'s byte images + symbols +
 * relocs), so E1 (the ELF static linker) reuses its shape. Data globals + GOT
 * relocs are the `B1-globals` stop, raised earlier in `encode_module_x86`.
 *
 * @param EncodedModuleX86 enc  the section images + symbols + relocations
 * @return []byte  the ELF64 object file bytes
 */
pub fn emit_elf(enc: EncodedModuleX86) -> []byte { … }
```

### 7.4 Baked A4-4 review findings (as ELF requirements)

- **Symbol value model (the F1 analog, INVERTED).** ELF `st_value` in `ET_REL` is section-relative +
  `st_shndx`, NOT absolute — do NOT port Mach-O's `section_addr + offset` rebase
  (`objfile_macho.tks:536`). Stating the inversion is the requirement.
- **Reloc rebase.** `r_offset` is `.text`-relative; `encode_module_x86` rebases per-function offsets
  by each function's `.text` base (the A4-3 rebase, `encode_arm64.tks:2126` analog).
- **Local rodata.** rodata is addressed via the local `.rodata` STT_SECTION symbol + addend (the ELF
  form of "rodata symbols are file-local"), so two objects never clash at the E1 link.
- **`sh_info` first-global.** `.symtab.sh_info` must equal the first non-local index; `.rela.text`'s
  `sh_link`→`.symtab`, `sh_info`→`.text` — an off-by-one here makes `ld` reject the object (the
  A4-4 dysymtab-range discipline).

---

## 8. Target dispatch + the differential lane (B1-8, the KEYSTONE)

### 8.1 `emit_native` gains a target switch (additive, FIXPOINT-preserving)

`emit_native` (`project.tks:664`) is currently hardwired to `select_module`/`aapcs64`/
`encode_module`/`emit_macho`. B1-8 adds a `TEKO_TARGET` env seam (mirroring the `TEKO_BACKEND` seam,
`project.tks:627`), superseded later by D1/#390's real `--target` flag:

```teko
/**
 * native_target — which own-backend target the temporary `TEKO_TARGET` seam
 * selects: `x86_64-linux` when the env var is exactly that (OS-based per ruling #390; `x86_64-elf` accepted as deprecated alias), else the default
 * `arm64-macho` (the A4 reference). OFF/absent → the arm64 path, so the default
 * build stays byte-identical (FIXPOINT) and the arm64 differential is untouched.
 * D1/#390 supersedes this with the real `--target` manifest flag.
 *
 * @return NativeTarget  the selected own-backend target
 */
fn native_target() -> NativeTarget {
    match teko::env::var("TEKO_TARGET") { str as v => if v == "x86_64-linux" { NativeTarget::X8664Elf } else { NativeTarget::Arm64Macho }; error => NativeTarget::Arm64Macho }
}
```

`emit_native` branches on `native_target()`: the `Arm64Macho` arm is the EXISTING body verbatim (so
the arm64 default output is byte-for-byte unchanged → fixpoint holds); the `X8664Elf` arm runs
`select_module_x86 → regalloc_module_x86(sysv64()) → encode_module_x86(sysv64()) → emit_elf`, writes
`<stem>.o`, and `link_object`s it (the SAME `build_cc_argv` machinery, `project.tks:503` — on a
linux-x86_64 host `cc` links the ELF against `teko_rt.c`/`assert.c`). The exclusive success marker
stays `"(own backend)"` (the harness's F1(a) guard, `diff_c_own.sh:60`).

> **Cross-host honesty.** On the macOS-arm64 dev host, selecting `x86_64-linux` PRODUCES a valid ELF
> `.o` but `cc` cannot LINK an x86 ELF there — so `link_object` fails. The harness (§8.2) therefore
> only EXECUTES the x86 lane on linux-x86_64; on macOS it byte-tests the `.o` (llvm-readobj is
> cross-format) and honest-skips the link/run. `emit_native`'s x86 arm still writes the `.o` before
> the link attempt, so the object is always available for the byte-test.

### 8.2 The differential — linux runs, macOS byte-tests

Extend `scripts/diff_c_own.sh` (today macOS-arm64-only, `:51`) with a **linux-x86_64 lane**: when
`uname -s -m` is `Linux x86_64`, build each corpus fixture twice (`TEKO_BACKEND=native` for the C
side unset, and `TEKO_BACKEND=native TEKO_TARGET=x86_64-linux` for the own side), assert
`C-native exit == own-native exit` + identical stdout, and run an ELF well-formedness check
(`scripts/check_elf.sh` — new, mirroring `check_macho.sh`: `readelf -h/-S/-s/-r` + `objdump -d`
sanity + `ld -r` accept). The same F1 guards apply: the own build MUST print `"(own backend)"`, and
the `<stem>.o` MUST exist. `native.yml` already carries a `linux-x86_64` leg in the
`diff VM==native` matrix (`:176`) — B1-8 adds the `diff_c_own.sh` invocation to it.

**Validation matrix (which runs where):**

| check | linux-x86_64 (CI) | macOS-arm64 (dev + CI) |
|---|---|---|
| encoder golden byte-vectors (`clang -c -target x86_64-linux-gnu` cross-assembler oracle) | ✓ unit gate | ✓ unit gate (clang cross-targets on macOS) |
| `emit_elf` header/symtab/rela golden bytes | ✓ | ✓ (machine-free) |
| `.o` well-formedness (`llvm-readobj`/`readelf`/`objdump`) | ✓ | ✓ (llvm tools are cross-format) |
| **executing `C==own` differential** | ✓ (runs the ELF) | honest-skip (cannot run x86) |
| `check_macho`/arm64 differential | honest-skip | ✓ (unchanged) |

### 8.3 The interp oracle — NO parallel `minst_x86_interp` for the MVP (grounded)

`minst_interp` is a total match over arm64 `MInst` semantics (`minst_interp.tks:10-17`) — it does
NOT generalize to `MInstX86` (different instructions: two-address RMW, SETcc, CQO, fixed-reg div).
A full parallel `minst_x86_interp` is therefore **deferred, not built for B1**, because its role —
a PRE-machine oracle catching lowering/isel bugs before bytes — is covered three ways for x86 without
it: (a) the **target-independent LIR interp** (`interp_lmodule`, `own-backend-architecture.md` §2.3,
the "fourth oracle") validates the program semantics pre-isel, SHARED across targets; (b) the
**assembler-cross-checked golden byte-vectors** validate each encoding; (c) the **executing
linux-x86_64 differential** validates end-to-end on a runner that actually runs the artifact — the
very thing arm64 lacked (its differential is macOS-only, which is WHY A4 needed the interp). So the
x86 pipeline is at least as well-oracled as arm64 was, minus a large parallel interp. `minst_x86_interp`
is a named future add (`B1-interp`, optional), REPORTED not blocking (§10 R-2).

---

## 9. Honest-stops — which slice each lives behind (0.3.1 completion vs later)

| stop | slice | what it defers | closed by |
|---|---|---|---|
| `B1-fp` | B1-5 (`encode_inst_x86`) | SSE float ops/conversions (ADDSD/CVTSI2SD/UCOMISD/…) | 0.3.1 (mirrors `A4-fp`) |
| `B1-args` | B1-3/B1-8 | stack args past the 6-int/8-fp window + `tk_set_args` argv forwarding | 0.3.1 (mirrors `A4-args`) |
| `B1-globals` | B1-7 | `.data`/`.bss` + `R_X86_64_64` data relocs — N2 lowers no `LGlobal` | when lowering emits globals |
| `B1-memindex` | B1-5 | index*scale addressing (only base+disp needed) | when lowering folds indexed addr |
| `B1-bigimm` | B1-5 | (near-N/A — `movabs` covers imm64; named for symmetry only) | — |
| `B1-bigframe` | B1-6 | (near-N/A — `sub rsp,imm32` covers 2 GB; named for symmetry) | — |
| `B1-redzone` | B1-6 | the 128-byte SysV red-zone leaf optimization (we always reserve) | an optimization pass |
| `B1-interp` | (oracle) | a parallel `minst_x86_interp` (LIR-interp + goldens + real diff cover it) | optional (§10 R-2) |
| FPR spill | — inherited | `detect_fpr_spill` analog errors before encode (A3's `A3-fpr-spill`) | A3's follow-up |
| `loop` back-edge | — inherited | regalloc honest-stops (`A3-loop`) before encode | A3's `A3-loop` |
| i128 register-pair ops | — inherited | isel never emits them (rides A2's i128 route) | A2's i128 route |
| PE/COFF, riscv64, wasm | later clusters | other targets | B2/B3 (#387/#388), C1 (#389) |
| `--backend`/`--target` flags | later cluster | the real manifest flags (B1 uses env seams) | D1/#390 |
| own ELF linker (drop `cc`) | later cluster | the ELF static linker | Phase E1/#226 |

Each stop is a NAMED error the pipeline surfaces; a fixture reaching one stops IDENTICALLY on the own
side and is compared at the stop, never at a fabricated value (M.3, the A4 §6 precedent).

---

## 10. Deferrals + recorded decisions (law-first — NO HALT) + reported findings

**D-A · SysV result register ≠ first argument register.** RAX(result) vs RDI(arg0) — unlike AAPCS64's
shared x0. Resolved by pinning RAX at the return/`ret_reg` site in isel (§4.5); `AbiDescriptor` shape
unchanged (M.1 — the descriptor stays the whole ABI cost). Recorded.

**D-B · Two-address handled at ISEL (copy insertion), not in regalloc.** §4.2. Law-first (M.1/M.5):
the target-specific two-address concept stays in the ISA module; the shared scan core stays
descriptor-only and untouched; the RMW-def-and-use shape reuses the proven `MMovK` precedent
(`regalloc.tks:421`). A regalloc operand-tie (bigger blast, concept-leak) and a normalization pre-pass
(extra module) are rejected. Recorded.

**D-C · Parallel `minst_x86` IR, not a generalized `MInst`.** §2. Law-first (M.1 smallest-blast + M.5
one-way): generalizing churns four frozen total-matches and risks the shipped A4 differential; the
parallel IR is additive and is exactly the architecture-of-record's per-ISA split
(`own-backend-architecture.md` §3.3). Recorded, not open — **this is the flagged potential-HALT, and
it has a clear winner, so it does not HALT.**

**D-D · `TEKO_TARGET` env seam (not the `--target` flag).** Mirrors A4's `TEKO_BACKEND` seam (D-B of
A4). Additive env read; default → arm64 (fixpoint-preserving); D1/#390 supersedes. Recorded.

**D-E · rel32-always (no branch relaxation).** §6.2. Law-first (M.1 — the simplest total encoder;
M.4 — a few bytes of honest overhead vs an iterative relaxation pass). Recorded.

**D-F · `cc`-as-linker on Linux (D-2 option 1 bootstrap, inherited).** B1 links the ELF `.o` via `cc`
against the C-built `teko_rt`; toolchain independence on ELF arrives at E1/#226 (the ELF static
linker), NOT at #386 — recorded so no one claims independence prematurely (M.3, the A4 D-C precedent).

**Reported findings (adjacent — NOT turned into issues here; folded/sequenced by the integrator):**

- **R-1 · The per-ISA `compute_intervals`/`rewrite_*` MFunc-walk is mirrored, not shared.** B1
  duplicates the thin interval-building/rewrite WALK (the SUBTLE scan/eviction algorithm stays
  single-sourced, §5.1). This is the same per-ISA mirroring `isel`/`encode` already embody. A future
  extraction — a target-independent interval builder consuming a decomposed `[]InstRegs` + block
  structure, once the compiler has the generic-over-instruction-type capability to express it without
  the silent-collision hazard — would single-source the walk too. REPORTED for the integrator to
  sequence (likely a 0.3.1 DRY-sweep companion), not a B1 blocker.
- **R-2 · No parallel `minst_x86_interp`.** §8.3 — the LIR interp + goldens + the real linux
  differential cover the oracle role; a parallel interp is an optional later add. REPORTED.
- **R-3 · `own_print_exit` (LIR builtin-call surface).** The KNOWN-STOP `diff_c_own.sh:128` (`_println`
  vs `_tk_println`) is a LIR-lowering gap (LIR's builtin table is narrower than codegen's), shared
  across ALL targets — it will KNOWN-STOP identically on the x86 lane. Already a reported finding;
  B1 inherits it unchanged (no new work).

---

## 11. Regression fixtures + the gate

### 11.1 Golden byte-vector unit tests (`encode_x86_64_test.tkt`) — engine-agnostic, assembler-oracled

- **Per-`MInstX86` encodings** (B1-5): `mov eax,42 → B8 2A 00 00 00`; `mov rax,rbx (48 89 D8)`;
  `add rax,rbx (48 01 D8)`; `sub`/`and`/`or`/`xor`; `imul rax,rbx (48 0F AF C3)`;
  the two-address `r = a+b → mov dst,a ; add dst,b` pair; `cmp` + `setl al` + `movzx`; the
  `idiv` sequence (`mov rax,a ; cqo ; idiv b ; mov dst,rax`); `lea rax,[rsp+off]` **with the SIB
  0x24** (the RSP-base trap); `ret → C3`; each FP case → the `B1-fp` stop. **Each vector is
  re-derived against `clang -c -target x86_64-linux-gnu` + `objdump -d`** (the table is spec, the
  assembler is oracle).
- **Frame** (B1-6): a spill fixture (tiny 2-GPR test descriptor forcing a spill) → the prologue
  `push rbp ; mov rbp,rsp ; sub rsp,#N`, the `MFrameAddrX86 → lea reg,[rsp+off]`, the epilogue
  `leave ; ret`; `compute_frame_layout_x86` offsets asserted directly; a frameless `main` →
  `size=0`, empty prologue.
- **Branch/reloc** (B1-6): a two-block `if` → the `jcc rel32` displacement patched to the byte
  distance; an `MLeaRipX86` → placeholder `id=0` + one `RelocX86{Pc32}`; an `MCallX86 tk_exit` →
  `E8 00 00 00 00` + one `RelocX86{Plt32,"tk_exit"}` with addend −4.
- **ELF** (B1-7, `objfile_elf_test.tkt`): `Ehdr` magic/class/machine bytes; a one-function module's
  `.symtab` has `main` global-defined (`st_shndx=1`, section-relative `st_value`), the `.rodata`
  STT_SECTION local symbol, and `tk_exit` undefined; `.rela.text` has one PLT32; `sh_info` =
  first-global index; `e_shnum`/`e_shstrndx` correct.

### 11.2 End-to-end differential fixtures — the SAME corpus A4 uses (`examples/regressions/own_*`)

B1 adds NO new fixtures; it adds the x86 own-native column to the existing corpus
(`diff_c_own.sh:95`):

| fixture | program | expected exit | VM | C-native | own-arm64 | **own-x86 (new)** |
|---|---|---|---|---|---|---|
| `own_exit_zero` | `exit(0)` | 0 | ✓ | ✓ | ✓ | **linux-run** |
| `own_exit_code` | `exit(42)` | 42 | ✓ | ✓ | ✓ | **linux-run** |
| `own_arith_exit` | `exit(6 * 7)` | 42 | ✓ | ✓ | ✓ | **linux-run** (two-address add path) |
| `own_sub_exit` | `exit(100 - 58)` | 42 | ✓ | ✓ | ✓ | **linux-run** |
| `own_if_exit` | `if 5 > 3 { exit(1) } else { exit(2) }` | 1 | ✓ | ✓ | ✓ | **linux-run** (jcc rel32 path) |
| `own_match_exit` | `match k { 0 => exit(7); _ => exit(9) }` | (per k) | ✓ | ✓ | ✓ | **linux-run** |
| `own_print_exit` | `println` then `exit` | — | ✓ | ✓ | KSTOP | **KSTOP** (R-3, inherited) |

The exit(n)-scoped corpus is exactly the frameless-`main`/interp-validated subset (the A4 §9.2
convention); a trailing-value framed `main` rides the same `MRetX86`+prologue path the frame goldens
exercise (the A4 §8 caveat carries over — no NEW divergence).

### 11.3 Ritual + coverage posture

- **Every B1-N** owes the full ritual: **both-engine gate** (native `teko . -o bin` AND `teko test .`
  VM), **paranoid**, **FIXPOINT** (the arm64/default output is byte-unchanged — B1 is purely
  additive files + the `TEKO_TARGET` branch whose default arm is verbatim), and **100% coverage on
  its new code** (definition-of-done). The encoder is highly branchy (per-`MInstX86`, per-width,
  per-cond, REX/SIB) — cover every encoded case + every honest-stop arm via goldens; a genuinely
  unreachable arm is justified in the PR.
- **VM-gotcha watch** (dense byte-work, the A4 §9.3 list carries over): (a) build buffers via
  `teko::list::push(buf, (x & 0xFF) to byte)` — never widen a `byte` mid-expression; (b) do all
  bit-slicing in `u32`/`u64`, narrow to `byte` only at the last step; (c) no `x = match {…return}` —
  use `let x = match {…}` then act; (d) a NEGATIVE `to u32`/`to u8` **panics** — rel32 displacement
  math and `r_addend = -4` must use MODULAR arithmetic (compute the two's-complement bit pattern in
  `u32`/`u64`, never cast a negative `i64` straight to `u32`); (e) **`flags` is a reserved keyword** —
  name section-flag locals `sh_flags`/`sect_flags` (the `emit_section` precedent,
  `objfile_macho.tks:386`); (f) struct-literals span lines with a leading-field-per-line layout (the
  `sysv64()`/`AbiDescriptor` shape).
- **Bootstrap-seed lane.** B1 uses ONLY existing syntax (new `.tks` files + one additive dispatch
  branch), so the stable-seed lane parses all of `src/` — no staged-syntax hazard (the wave
  stable-seed gotcha does not bite).

### 11.4 Ritual points (where the FULL gate must pass)

- After **B1-1** (abi + core-neutrality proof): full both-engine gate + the neutrality unit test
  green — this is the cheapest possible proof the shared core carries no AAPCS64 assumption.
- After **B1-5** (encoder core) and **B1-6** (branch/reloc): full both-engine gate + the
  assembler-cross-checked goldens green (the riskiest bit-work).
- After **B1-7** (ELF writer): full gate + `readelf`/`llvm-readobj` round-trip green on the emitted
  `.o` (runs on any host — machine-free).
- The **KEYSTONE full ritual at B1-8**: the whole gate — both engines + fixpoint + the **new
  linux-x86_64 C-vs-own leg green** (executing the ELF) + the macOS byte-test lane green. **#386
  CLOSES here.**

---

## 12. The sub-slice decomposition (ordered, each gate-able)

```
A4 (done, #385) ─▶ B1-1 ─▶ B1-2 ─▶ B1-3 ─▶ B1-4 ─▶ B1-5 ─▶ B1-6 ─▶ B1-7 ─▶ B1-8   [#386 closes]
                   abi +    x86 IR   isel-x86  regalloc  encoder  branch/  ELF64    target-dispatch
                   core-    (MInstX86) two-addr  x86      core     rel32    writer   + linux diff
                   proof             + div+lea  shell    (goldens) fixups
```

- **B1-1 · `abi_sysv64.tks` + core-neutrality proof** — `sysv64()` descriptor; a unit test driving
  the SHARED `linear_scan` under it (§3). **Proven by:** the neutrality test + descriptor asserts.
  No x86 instruction yet.
- **B1-2 · `minst_x86.tks`** — the `MInstX86` variant + x86 opcode/reloc enums +
  `MFuncX86`/`MBlockX86`/`MModuleX86` + builders + printer, reusing the neutral `MReg`/`MCond`/`MMem`
  (§4.1). **Proven by:** printer golden tests + type surface.
- **B1-3 · `isel_x86_64.tks`** — `select_module_x86`: two-address copy-at-isel (§4.2), fixed-reg
  div/rem (§4.4), single `lea rip` addressing (§4.3), SysV arg/result lowering (§4.5). **Proven by:**
  golden `MInstX86`-dump + the shared LIR-interp equivalence over the corpus subset.
- **B1-4 · `regalloc_x86.tks`** — `inst_regs_x86`/`map_minst_x86`/`rewrite_inst_x86`/
  `regalloc_func_x86`/`regalloc_module_x86` calling the shared scan core (§5). **Proven by:**
  regalloc-x86 tests (no live-range overlap in a physreg) + `all_physical_x86` + a spill fixture.
- **B1-5 · `encode_x86_64.tks` core** — `EncInstX86`, `encode_inst_x86` for the integer/control/
  memory cases (§6.3), REX/SIB handling, the `B1-fp` stop. **Proven by:** per-`MInstX86` goldens,
  assembler-cross-checked. No e2e.
- **B1-6 · branch layout + rel32 fixups + relocs** — block byte-offset recording + the rel32 patch
  pass (§6.2), `RelocX86` collection, `encode_func_x86`/`encode_module_x86` (concat + rebase +
  symbol table), frame prologue/epilogue (§6.5). **Proven by:** two-block `jcc` displacement +
  `call`/`lea` reloc-record goldens. No object.
- **B1-7 · `objfile_elf.tks`** — `emit_elf` (Ehdr + 7 Shdrs + section images + symtab + rela),
  baking the §7.4 findings; the `B1-globals` stop. **Proven by:** header/symtab/rela goldens +
  `readelf`/`llvm-readobj` well-formedness (machine-free, any host).
- **B1-8 · target dispatch + differential (KEYSTONE)** — `native_target()` + the `emit_native`
  `X8664Elf` arm (§8.1), `scripts/check_elf.sh`, the `diff_c_own.sh` linux-x86_64 lane (§8.2),
  `native.yml` wiring. **Proven by:** `own-native == C-native` exit codes over the corpus on
  linux-x86_64 (executing the ELF) + the macOS byte-test lane. **#386 CLOSES.**

**Files:** new `src/backend/abi_sysv64.tks`, `minst_x86.tks`, `isel_x86_64.tks`, `regalloc_x86.tks`,
`encode_x86_64.tks`, `objfile_elf.tks`, and their `*_test.tkt`; new `scripts/check_elf.sh`; touched
`src/build/project.tks` (`native_target` + the `emit_native` x86 arm), `scripts/diff_c_own.sh` (the
linux-x86_64 lane), `.github/workflows/native.yml` (the diff wiring). **Reuses unchanged:**
`lir::lower_program`, the neutral `minst` surface (`MReg`/`MRegClass`/`MCond`/`MMem`/`vreg`/`preg`),
the whole `AbiDescriptor` surface, the register-abstract scan core
(`linear_scan`/`candidate_pool`/`assign_lookup`/`LiveInterval`/`IntervalSet`/`ScanResult`/`SubstEntry`),
`Symbol`, and the byte-buffer helpers (`emit_u32_le`/`emit_u64_le`/`emit_str_bytes`/`pad_to_mult`/
`build_strtab`/`align_up`).

---

## Appendix · SSE FP encodings (for the `B1-fp` fast-follow)

SysV passes/returns floats in XMM. Base forms (double, `F2` prefix; single uses `F3`): ADDSD
`F2 0F 58 /r`, SUBSD `F2 0F 5C /r`, MULSD `F2 0F 59 /r`, DIVSD `F2 0F 5E /r`; UCOMISD `66 0F 2E /r`;
CVTSI2SD `F2 REX.W 0F 2A /r`, CVTTSD2SI `F2 REX.W 0F 2C /r`, CVTSD2SS `F2 0F 5A /r`, CVTSS2SD
`F3 0F 5A /r`; MOVSD(reg) `F2 0F 10 /r`; XORPD (for FNEG via sign-bit mask) `66 0F 57 /r`. The
implementer re-derives each against `clang -c -target x86_64-linux-gnu` before shipping the
fast-follow, exactly as the `A4-fp` appendix prescribes.
```
