# A2 / N2b — arm64 instruction selection (crumb plan)

**Status:** DESIGN (doc-only). Sub-PR of the 0.3 own-AOT-backend wave (umbrella
`remodel/backend-build`). Issue **#383**. Branch `fix/issue-383-isel-arm64`. Base for the
design: `docs/design/own-backend-architecture.md` §3.3 (isel) and §2 (AAPCS64), grounded in
the merged A1 code (`src/lir/lir.tks`, `lir_interp.tks`, `lir_print.tks`, PR #396 on the
umbrella).

> This is a PLAN. It designs the machine-IR (`MInst`) + printer and the AArch64 instruction
> selector (`LIR -> MInst`), decomposes A2 into ordered, independently gate-able sub-sub-PRs,
> and specifies the verification (golden MInst-dump + isel-over-interp equivalence). A2 emits
> **no register allocation** (A3) and **no machine bytes** (A4): `MInst` stays on virtual
> registers over an abstract operand model, and the selected form is proven by re-interpreting
> it against the LIR interpreter's exit code.

---

## 1. The assessed starting point (the real A1 shape isel consumes)

A1 (#382) closed the lowering frontier. The `LOp` variant the selector must cover is now the
FULL sixteen-case set (`src/lir/lir.tks`):

```
LConstInt · LConstFloat · LBin · LUn · LCall · LParam · LRet · LJump · LBranch
LAlloca · LFieldAddr · LLoad · LStore · LGlobalAddr · LCallIndirect · LFuncAddr
```

with the closed `LBinOp` (arithmetic `IAdd..IRemU`, bitwise `IAnd/IOr/IXor`, shifts
`IShl/IShrS/IShrU`, integer compares `ICmpEq..ICmpGeU`, float `FAdd..FDiv`, float compares
`FCmpEq..FCmpGe`) and `LUnOp` (`INeg/INot/FNeg`, extends `ZExt/SExt/Trunc`, conversions
`IToF/FToI/FToF`). Three grounding facts fix the isel's shape:

1. **Signedness is on the opcode, widths on `LType`** (`lir.tks:13-17,25-35`). `IDivS` vs
   `IDivU` picks `SDIV` vs `UDIV`; `ICmpLtS` vs `ICmpLtU` picks the signed vs unsigned
   condition code. The selector reads op + result-type and needs no re-analysis — a flat
   per-op match, exactly the strategy §3.3 fixes.

2. **`LType` is already the machine class** (`lir.tks:18-23`): `I8..I128`, `F16/F32/F64`,
   `Ptr`, `Void`. `str`/`slice`/`Ref`/`ptr`/`Named` all collapsed to `Ptr` at lowering, so
   isel never sees a semantic type — every operand is a width + a GPR/FPR class.

3. **There is no fat SSA value and no fat `LOp`.** This is the load-bearing correction to the
   recon's abstract "fat-pointer two-register sequences" (§3.3). In A1 a `str`/`slice` is
   carried as **two independent scalar VRegs** — a `Ptr` VReg and an `I64` length VReg —
   threaded through a side-table (`lower.tks` `lenv_bind_fat`/`lenv_lookup_fat`, `LFatLookup`
   `:64-79`); a string literal is `LGlobalAddr` (the ptr) plus a separate `LConstInt` (the
   len). Fat values are **never** promoted through block-args (`lower.tks` `promote_env`:
   "one block-arg per SCALAR (non-fat) name … a FAT binding is left UNPROMOTED"). **Therefore
   isel needs no fat opcode**: each half selects as an ordinary scalar; the "two-register
   sequence" surfaces only as (a) two adjacent AAPCS64 argument-register occupancies when a
   fat pair is passed to a call, and (b) two ordinary `MMov`s if both halves cross a merge.
   §5.1 records this as a resolved decision.

### 1.1 The verification oracle A2 must match

The LIR interpreter (`lir_interp.tks`) is an **exit-code oracle**, not a byte-layout verifier
(`:93-97`): an address IS a memory-cell index (one cell per byte-granular address,
`MemStore`), `alloca` bumps a cursor, rodata is seeded one-cell-per-byte before entry
(`seed_rodata`), and a function pointer is a **synthetic negative sentinel** `-(index+1)`
resolved on demand (`func_slot_addr`/`resolve_func_addr`, `:238-286`). A2's isel-over-interp
harness re-interprets the SELECTED `MInst` form over this SAME abstract model and asserts the
same exit code (§4). Byte-accurate layout is A4's job (the C-vs-own differential); A2 stays at
the exit-code oracle so it needs no encoder.

---

## 2. The `MInst` machine-IR (new `src/backend/minst.tks`)

`MInst` is a thin, AArch64-keyed machine-instruction IR: a variant of instruction-shaped
structs, mirroring `LOp`'s shape so the printer and the isel-over-interp harness are flat
per-case matches (the A1 `lir_print`/`lir_interp` precedent). It carries **virtual registers**
that A3 colors and **physical-register pins** for the ABI; it holds no encoded bytes.

### 2.1 The operand model

One register type carries a physical flag so it appears uniformly in every operand and in a
memory base:

```teko
/**
 * MRegClass — the AArch64 register file a value lives in: general-purpose
 * (x/w registers, the SP/LR) or floating-point/SIMD (v/d/s/h registers).
 * A VReg's class is fixed by its defining op (an integer op defines a GPR,
 * a float op an FPR), so `class_of` (the isel side-table) never guesses.
 *
 * @since #383 A2-1
 */
pub type MRegClass = enum { GPR; FPR }

/**
 * MReg — one register operand: a virtual register (`is_phys=false`, `id` an
 * abstract number the A3 allocator colors) or a physical register
 * (`is_phys=true`, `id` an ISA number — x0..x30, SP=31 in the GPR class;
 * v0..v31 in the FPR class). Physical pins carry the AAPCS64 constraints
 * (argument/return registers) isel plants and A3 coalesces.
 *
 * @since #383 A2-1
 */
pub type MReg = struct { id: u32; class: MRegClass; is_phys: bool }

/**
 * MMem — a base-plus-displacement address operand: `[base, #offset]`. A
 * folded `LFieldAddr` rides in `offset` (a compile-time byte constant); the
 * plain case is `offset=0`. The A4 encoder maps `offset` to the scaled/
 * unscaled immediate form; A3 may rewrite `base` to a frame register.
 *
 * @since #383 A2-4
 */
pub type MMem = struct { base: MReg; offset: i32 }

/**
 * MRelocKind — which relocation an address-materialization instruction owes
 * the A4 encoder: the ADRP page (`PageHi`), the ADD/LDR low-12 page offset
 * (`PageLo`), or a direct branch target (`Call`). A2 records the kind + the
 * already-final symbol; A4 emits the actual relocation entry.
 *
 * @since #383 A2-4
 */
pub type MRelocKind = enum { PageHi; PageLo; Call }
```

Condition codes map the compare opcodes (signed `LT/LE/GT/GE`, unsigned `LO/LS/HI/HS`,
`EQ/NE`) directly onto AArch64 `b.cond`/`cset` conditions:

```teko
/**
 * MCond — an AArch64 condition code, chosen from an integer/float compare
 * opcode. The unsigned lane (`LO/LS/HI/HS`) is picked for the `*U` compares,
 * the signed lane (`LT/LE/GT/GE`) for the `*S` compares — signedness rides
 * the LIR opcode, so the mapping is total and mechanical.
 *
 * @since #383 A2-3
 */
pub type MCond = enum { EQ; NE; LT; LE; GT; GE; LO; LS; HI; HS }
```

### 2.2 The opcode subset (`MInst` cases)

The subset is the minimum that covers A1's op set (M.1 small/orthogonal). Each case names its
AArch64 mnemonic; `wide` = 64-bit X-register form vs 32-bit W-register form; `dbl` = f64 vs
f32 (f16 promotes, §5.4).

| `MInst` case | AArch64 | Covers |
|---|---|---|
| `MAlu{op,wide,dst,a,b}` | ADD/SUB/MUL/SDIV/UDIV/AND/ORR/EOR/LSL/ASR/LSR | `IAdd..IMul`, `IDivS/U`, `IAnd/IOr/IXor`, `IShl/IShrS/IShrU` |
| `MAluImm{op,wide,dst,a,imm}` | ADD/SUB/AND/…#imm | folded `LFieldAddr`, add/sub-by-const peephole |
| `MMSub{wide,dst,a,b,c}` | MSUB (`c-a*b`) | `IRemS/IRemU` (SDIV/UDIV then MSUB) |
| `MNeg{wide,dst,a}` | NEG | `INeg` |
| `MMvn{wide,dst,a}` | MVN | `INot` |
| `MFAlu{op,dbl,dst,a,b}` | FADD/FSUB/FMUL/FDIV | `FAdd..FDiv` |
| `MFNeg{dbl,dst,a}` | FNEG | `FNeg` |
| `MCmp{wide,a,b}` / `MCmpImm{wide,a,imm}` | CMP (SUBS xzr) | integer compares, cond-vs-0 |
| `MFCmp{dbl,a,b}` | FCMP | float compares |
| `MCSet{wide,dst,cond}` | CSET | a compare used as a VALUE (not fused into a branch) |
| `MMovZ/MMovK/MMovN{wide,dst,imm16,shift}` | MOVZ/MOVK/MOVN | `LConstInt` materialization (16-bit lanes) |
| `MFMov{dbl,dst,src}` | FMOV | `LConstFloat` (GPR bits -> FPR), reg-reg FP move, class crossings |
| `MMov{wide,dst,src}` | ORR xzr (reg move) | `Trunc` (W-view), reg-reg move, edge moves |
| `MExt{op,dst,a}` | SXTB/SXTH/SXTW · UXTB/UXTH/UXTW | narrow `SExt`/`ZExt` |
| `MCvt{op,dst,a}` | SCVTF/UCVTF · FCVTZS/FCVTZU · FCVT | `IToF`/`FToI`/`FToF` |
| `MLoad{size,sign,dst,mem}` | LDR/LDRB/LDRH/LDRSW/LDRSB/LDRSH | `LLoad` (size by `ty`) |
| `MStore{size,src,mem}` | STR/STRB/STRH | `LStore` |
| `MFrameAddr{dst,slot}` | (abstract) ADD dst, FP/SP, #slotoff | `LAlloca` (slot address; A3 resolves offset) |
| `MAdrp{dst,sym,kind}` + `MAddLo{dst,a,sym,kind}` | ADRP + ADD | `LGlobalAddr` (data reloc), `LFuncAddr` (text reloc) |
| `MBranch{target}` | B | `LJump` (after edge moves) |
| `MBranchCond{cond,target}` | B.cond | fused compare taken-edge |
| `MCbz{wide,zero,a,target}` | CBZ/CBNZ | non-fused `LBranch` (test the cond reg) |
| `MCall{sym,uses,ret_reg,has_result,variadic}` | BL | `LCall` (after arg pins) |
| `MCallIndirect{target,uses,ret_reg,has_result}` | BLR | `LCallIndirect` |
| `MRet` | RET | `LRet` (value already pinned to x0/v0) |

`MAluOp`/`MFAluOp`/`MExtOp`/`MCvtOp`/`MMemSize` are small closed enums (the arithmetic,
extend, convert, and access-size selectors). `MInst` is the variant over the cases above; a
`MBlock` mirrors `LBlock` (`id`, `params: []MReg`, `insts: []MInst`); an `MFunc` mirrors
`LFunc` (`symbol`, `blocks`, plus a `frame` slot table `LAlloca` grows and A3 finalizes); an
`MModule` carries the `MFunc`s + the rodata/globals/layout tables passed THROUGH unchanged
from `LModule` (A4 emits them; isel does not touch their bytes).

### 2.3 Where A3 and A4 plug in

- **A3 (regalloc)** consumes the virtual `MReg`s, the physical pins (arg/return/`MCall.uses`),
  and the `MFrameAddr` slot table: it colors the vregs, resolves slot -> FP/SP offset, spills,
  and coalesces the pin-`MMov`s into physical registers. A2 leaves every non-pin `MReg`
  virtual.
- **A4 (encoder + Mach-O)** consumes the concrete `MInst` opcodes: it picks scaled/unscaled
  immediate forms, the FMOV-immediate / single-MOVZ peepholes, resolves `MBranch*` target
  offsets, and emits the `MAdrp`/`MAddLo`/`MCall` relocations from their `sym`+`kind`.

---

## 3. The instruction selector (new `src/backend/isel_arm64.tks`)

`select_module(m: LModule) -> MModule` walks each `LFunc`, threading a `SelCtx` that carries
the in-progress `MFunc`, the current block, the **`class_of` side-table** (VReg -> `MRegClass`,
populated at each op's result so argument/return classification is exact), and a scratch-VReg
allocator (fresh virtual ids past `LFunc.next_vreg`). Selection is a flat per-`LOp` match; the
mapping:

### 3.1 Constants, arithmetic, unary, moves

- **`LConstInt`** -> `materialize_int`: the minimal MOVZ + MOVK-per-nonzero-lane sequence
  (MOVN for a value denser as its inverse). `wide` from the result `LType`.
- **`LConstFloat`** -> materialize the `bits` (u64) into a scratch GPR via `materialize_int`,
  then `MFMov` GPR -> FPR `dst`. (A4 folds encodable immediates to FMOV #imm.)
- **`LBin` arithmetic/bitwise/shift** -> one `MAlu` (`IAdd`->ADD, `ISub`->SUB, `IMul`->MUL,
  `IDivS`->SDIV, `IDivU`->UDIV, `IAnd/IOr/IXor`->AND/ORR/EOR, `IShl`->LSL, `IShrS`->ASR,
  `IShrU`->LSR). **`IRemS`/`IRemU`** -> `MAlu` SDIV/UDIV into a scratch quotient, then
  `MMSub` (`rem = a - q*b`).
- **`LBin` float** -> one `MFAlu` (`FAdd`->FADD …).
- **`LBin` compare** (`ICmp*`/`FCmp*`) -> when the value is consumed by a `LBranch` and by
  nothing else, it is **fused** (§3.2) and emits nothing here; otherwise `MCmp`/`MFCmp` then
  `MCSet dst, cond` materializing the 0/1 result. `cond` from the opcode's `MCond`.
- **`LUn`** -> `INeg`->`MNeg`; `INot`->`MMvn`; `FNeg`->`MFNeg`; `Trunc`->`MMov` (W-view write
  zeroes the upper half); `ZExt` from a narrow width -> `MExt` UXTB/UXTH (32->64 zero-extend is
  the implicit W-write, an `MMov`); `SExt` -> `MExt` SXTB/SXTH/SXTW; `IToF`->`MCvt`
  SCVTF/UCVTF (signedness from the source type); `FToI`->`MCvt` FCVTZS/FCVTZU; `FToF`->`MCvt`
  FCVT.
- **`LParam`** -> at function entry, `MMov` from the pinned AAPCS64 argument register for
  `index` (x0..x7 GPR / v0..v7 FPR, by the param's class) into the param VReg. Stack-passed
  params (index past 8 in a class, §3.5) become an `MLoad` from the incoming-args frame area.

### 3.2 Control flow — jumps, branches, edge moves

Block-args are the SSA-lite merge; A1 promotes **only scalar values, at most one per merge**
(`promote_env`), so edge moves are a single `MMov` in the common case — but the selector emits
the general **parallel-copy** (a scratch-register cycle-break) so a future multi-arg merge is
correct by construction.

- **`LJump{target,args}`** -> emit the parallel-copy of `args` into `target`'s block params,
  then `MBranch{target}`.
- **`LBranch{cond,t,t_args,f,f_args}`** -> the two edges carry (possibly different) block-args,
  and an AArch64 conditional branch cannot carry moves, so **split each edge into its own
  machine block**: `t_edge` performs the taken-edge moves then `MBranch t`; `f_edge` performs
  the else-edge moves then `MBranch f`. The branch itself is either
  - **fused**: when `cond` is defined by an `ICmp*`/`FCmp*` `LBin` whose only use is this
    branch, emit `MCmp/MFCmp a,b` then `MBranchCond{cond, t_edge}` and fall through to
    `f_edge` — no CSET, no separate test; or
  - **generic**: `MCbz{zero=false, a=cond, target=t_edge}` (CBNZ on the boolean) then fall
    through to `f_edge`.

  The empty-arg fast path skips the edge blocks and branches straight to `t`/`f`.

### 3.3 Memory + aggregates

- **`LAlloca{bytes,align}`** -> reserve a frame slot (grow the `MFunc.frame` table) and emit
  `MFrameAddr{dst, slot}`; A3 resolves `slot` to an FP/SP displacement.
- **`LFieldAddr{base,offset}`** -> `MAluImm ADD dst, base, #offset` (a Ptr VReg). The
  fold-into-`MMem` peephole is deferred to A4 so A2's golden dumps stay 1:1 and predictable
  (M.1).
- **`LLoad{addr,ty}`** -> `MLoad{size, sign, dst, MMem{addr,0}}` — size B/H/W/X (or FPR S/D)
  from `ty`; loads narrower than the register zero/sign-extend per the value's producing op
  (a `Trunc`/`ZExt` chain already fixed the class).
- **`LStore{addr,value,ty}`** -> `MStore{size, value, MMem{addr,0}}`.
- **`LGlobalAddr{symbol}`** -> `MAdrp{dst, symbol, PageHi}` + `MAddLo{dst, dst, symbol,
  PageLo}` (a **data** relocation pair).
- **`LFuncAddr{symbol}`** -> the same ADRP + ADD pair, but with the `Call`/text relocation kind
  so A4 emits a text-section reloc (`lir.tks` `LFuncAddr` doc fixes this distinction).

### 3.4 Calls — direct, indirect, AAPCS64

- **`LCall{symbol,args,variadic}`** -> classify each arg VReg by `class_of` and lower it to its
  AAPCS64 slot (§3.5): a pin-`MMov` into the arg physical register, or an `MStore` to the
  outgoing-args frame area for the overflow/variadic tail. Then `MCall{symbol, uses=<the pinned
  physregs, for A3 liveness>, ret_reg, has_result, variadic}`. If `has_result`, `MMov` from x0
  (or v0 for an FPR result) into the result VReg.
- **`LCallIndirect{target,args,variadic}`** -> identical arg lowering, then `MCallIndirect{
  target, uses, ret_reg, has_result}` (BLR through the `target` Ptr VReg). This is the
  vtable/closure path: A1 already emitted the `LLoad` of the slot and prepends the receiver
  `data` as the first arg (`lir.tks` `LCallIndirect` doc), so isel treats `target` as an
  ordinary Ptr VReg.
- **`LRet{has_value,value}`** -> if `has_value`, `MMov value -> x0`/`v0` (pinned by the
  function's return class), then `MRet`; else `MRet`.

### 3.5 AAPCS64 argument assignment (the fat pair and variadic quirks)

The selector runs the AAPCS64 classifier: GPR args fill x0..x7, FPR args fill v0..v7,
independently; a class's overflow spills to the stack in declaration order. Two grounded
notes:

- **Fat pairs need no special case.** A `str`/`slice` reaches a call as two independent scalar
  VRegs (a `Ptr` and an `I64`, §1 fact 3); they occupy two consecutive GPR arg slots
  naturally — the AAPCS64 "small aggregate in two registers" outcome falls out of ordinary
  per-VReg assignment. No fat opcode, no fat move.
- **Darwin arm64 variadic tail goes on the stack.** On the reference target (arm64 Mach-O),
  variadic arguments past the fixed arity are passed on the **stack**, not in registers. When
  `LCall.variadic`, the selector routes every argument past the callee's fixed arity to
  outgoing stack slots (`MStore`), not to `v*`/`x*`. This matters for the `tk_*` printf-shaped
  runtime calls in the corpus.

---

## 4. Verification — golden dumps + isel-over-interp equivalence

Two legs, both machine-byte-free (A2 emits nothing to link):

**(1) Golden MInst-dump.** A new deterministic printer `minst_print` (in `minst.tks`, the
`lir_print` precedent: one instr per line, `%N`/`%pXX` for virtual/physical regs, `#Mn` for
machine blocks, `@sym` for symbols) renders `MModule` to stable text. Unit tests
(`isel_arm64_test.tkt`) build a small `LModule` by hand, run `select_module`, and assert
`str_contains` over the dump — the machine-IR's version of the LIR diff spine.

**(2) isel-over-interp equivalence.** A new `MInst` interpreter
(`src/backend/minst_interp.tks`) re-interprets the SELECTED form over the **same abstract model
as `lir_interp`** — a `MemStore` (cell-per-address), rodata seeded before entry, the synthetic
negative func-address sentinel for `MCallIndirect`, physical arg registers x0..x7/v0..v7 as
their own register slots. For each fixture the harness asserts:

```
minst_interp(select_module(m), "main", args)  ==  interp_lmodule(m, "main", args)
```

i.e. the selected form, re-run, produces the LIR interpreter's exit code. The fixture corpus
is A1's own interp fixtures (`lir_interp_test.tkt` F1-F5 and the control/memory/call cases A1
added) driven through isel — so equivalence is checked against a KNOWN-GOOD oracle for exactly
the programs A1 proved lower correctly. A float or i128-heavy op the oracle honest-stops on is
skipped identically on both sides (the harness compares the honest-stop, not a fabricated
value).

The `MInst` interpreter is a permanent A2-scoped oracle (it retires when A4's real
C-vs-own byte differential subsumes it), mirroring how `lir_interp` is the permanent N1 oracle.

---

## 5. The sub-sub-PR decomposition (ordered, each gate-able)

A2 is XL, so it drains as five sub-sub-PRs on `fix/issue-383-isel-arm64`, in order. Each owes
the full ritual (gate both engines · paranoid · fixpoint) and 100% coverage on its new code;
each names the fixtures that PROVE it. No machine bytes in any of them.

### A2-1 · `MInst` types + printer
- **Scope:** the machine-IR — `MRegClass`/`MReg`/`MMem`/`MRelocKind`/`MCond`, the `MAluOp`/
  `MFAluOp`/`MExtOp`/`MCvtOp`/`MMemSize` enums, the `MInst` variant (§2.2), `MBlock`/`MFunc`/
  `MModule`, the constructor helpers (the `*_inst` precedent), and the deterministic
  `minst_print`.
- **Files:** new `src/backend/minst.tks`; tests `src/backend/minst_test.tkt`.
- **Deps:** A1 (merged).
- **Proven by:** golden-dump unit tests — hand-built `MInst`s render to the exact expected
  text (every opcode case, both `wide` forms, physical + virtual regs, the reloc kinds).

### A2-2 · isel core (const · arith · unary · move · param · ret) + the equivalence harness
- **Scope:** `select_module`/`SelCtx`/`class_of`, `materialize_int`, and the §3.1 mappings for
  `LConstInt`/`LConstFloat`/`LBin`(arith/bitwise/shift)/`LUn`/`LParam`/`LRet`. **The
  `minst_interp` harness lands here** (the whole equivalence method, §4), scoped to the
  straight-line integer subset.
- **Files:** new `src/backend/isel_arm64.tks`, `src/backend/minst_interp.tks`; tests
  `src/backend/isel_arm64_test.tkt`, `src/backend/minst_interp_test.tkt`.
- **Deps:** A2-1.
- **Proven by:** golden-dump (a two-param `add`, a const+ret, a rem-via-MSUB, a negate) +
  isel-over-interp on A1's F1-F5 straight-line fixtures (exit code == `interp_lmodule`).

### A2-3 · control flow (compares → cmp+b.cond/cbz · jumps · block-arg edge moves)
- **Scope:** the §3.2 lowering — `LJump`, `LBranch`, the compare-fusion peephole, the parallel-
  copy edge moves + critical-edge splitting, and the value-form `MCmp`+`MCSet`/`MFCmp` for a
  compare consumed as a value.
- **Files:** `src/backend/isel_arm64.tks` (+ its harness gains `MBranch*`/`MCbz`/`MCmp`
  execution), tests in `isel_arm64_test.tkt`.
- **Deps:** A2-2.
- **Proven by:** golden-dump (a fused `if a < b`, a generic CBNZ branch, a merge with one
  block-arg → single edge `MMov`) + isel-over-interp on A1's `if`/`match`/`loop` fixtures
  (exit-code parity through the interp).

### A2-4 · memory + aggregates + rodata addressing
- **Scope:** the §3.3 lowering — `LAlloca` (frame slot + `MFrameAddr`), `LFieldAddr`
  (`MAluImm` ADD), `LLoad`/`LStore` (sized), `LGlobalAddr`/`LFuncAddr` (ADRP+ADD reloc pairs),
  and passing `LModule.rodata`/`globals`/`layouts` through to `MModule` unchanged.
- **Files:** `src/backend/isel_arm64.tks` (+ harness gains alloca/load/store/global-addr over
  the `MemStore` model, mirroring `lir_interp`'s seeded rodata + synthetic func addresses),
  tests in `isel_arm64_test.tkt`.
- **Deps:** A2-2 (independent of A2-3; may drain in parallel, sequenced after for a clean
  single lane).
- **Proven by:** golden-dump (an alloca+field_addr+store+load round-trip, a `global_addr @sym`
  ADRP/ADD pair) + isel-over-interp on A1's struct/rodata fixtures — exit code matches, and the
  synthetic func-address sentinel resolves identically on both sides.

### A2-5 · calls direct + indirect + AAPCS64 argument lowering
- **Scope:** the §3.4/§3.5 lowering — `LCall` (arg pins + `MCall` + result move), `LCallIndirect`
  (BLR through the loaded slot), the AAPCS64 classifier (x0-x7/v0-v7 + stack overflow), the
  Darwin-variadic-on-stack rule, and the fat-pair-as-two-scalars confirmation (a golden fixture
  passing a `str` proves the two adjacent GPR slots).
- **Files:** `src/backend/isel_arm64.tks` (+ harness gains `MCall`/`MCallIndirect` over the
  physical-arg-register model + the func-address sentinel for BLR), tests in
  `isel_arm64_test.tkt`.
- **Deps:** A2-4 (needs `global_addr`/`func_addr` + memory for the vtable/closure indirect
  path).
- **Proven by:** golden-dump (a direct `tk_exit(%0)` BL with x0 pin, an indirect vtable call, a
  9-arg call spilling to the stack, a variadic call) + isel-over-interp on A1's call / interface-
  dispatch / closure fixtures. **A2 CLOSES here.**

### Ordering
```
A1 (done) ─▶ A2-1 ─▶ A2-2 ─▶ A2-3
                         └──▶ A2-4 ─▶ A2-5   [#383 closes]
```

---

## 6. Recorded decisions and law tensions (law-first)

Each is framed against the Constitution laws (M.0 self-host · M.1 small/orthogonal · M.2
deterministic · M.3 honest · M.4 no-hidden-cost · M.5 one-way). None is an unresolvable HALT;
one (§6.3) is flagged for an owner confirmation because it touches the D-2 runtime-link fork.

### 6.1 Fat pointers are two scalars, not a fat opcode (resolved)
The recon's "fat-pointer two-register sequences" (§3.3) is, against the real A1 shape, **two
independent scalar VRegs** with no fat `LOp` and no fat SSA value (§1 fact 3). isel adds NO fat
opcode; the two-register outcome falls out of ordinary per-VReg selection + AAPCS64 assignment.
This is the M.1/M.5 answer (no redundant machinery, one way to move a value) and is the honest
reading of the code (M.3). Recorded, not open.

### 6.2 The `MInst` interpreter mirrors `lir_interp`'s abstract model (resolved)
A2 verifies at the **exit-code** oracle, not byte layout (byte accuracy is A4's C-vs-own
differential). The `minst_interp` therefore reuses `lir_interp`'s cell-per-address `MemStore`,
seeded rodata, and synthetic negative func-address sentinel, so `minst_interp(select(m)) ==
interp_lmodule(m)` is a like-for-like comparison. Building a second, byte-accurate interpreter
now would be M.4 hidden cost with no added signal before A4. Resolved.

### 6.3 i128 multiply/divide/remainder lower via runtime helpers (recommended — owner confirm)
AArch64 GPRs are 64-bit; `I128` add/sub/bitwise/shift/compare lower **inline** via register
pairs (ADDS/ADC, SUBS/SBC, and paired logic) — a bounded, honest sequence isel emits directly.
But `I128` **multiply/divide/remainder** are large multi-instruction idioms (or a long-multiply
+ Knuth division). **Recommendation:** lower `I128` mul/div/rem to a **runtime-helper call** (an
`MCall` to `tk_mul_i128`/`tk_divmod_i128`-shaped symbols in `teko_rt`), exactly as the C
backend already relies on runtime helpers for wide arithmetic (`tk_mul_u16` et al.). Law-first:
M.3 (honest — a named helper, not a silently-wrong inline sequence), M.1 (isel stays small), and
it is consistent with own-backend recon **D-2**'s bootstrap answer (link the C-built `teko_rt`
until E1). **Flagged for the owner only because it adds helper symbols to `teko_rt` and couples
to D-2**: confirm the helper route is acceptable vs demanding a fully-inline i128 mul/div in A2.
Not a HALT — the recommendation passes all Laws; A2-2 can land the inline i128 add/sub/bitwise
subset and gate the mul/div/rem helper stubs behind a named honest-stop if the confirmation
lags, then close it in A2-5's follow-on without reworking the walk.

### 6.4 f16 arithmetic promotes to f32 (resolved)
`F16` values load/store natively (H-registers) but arithmetic promotes via FCVT to S, computes
in f32, and FCVT-narrows back — the portable AAPCS64 baseline (no FEAT_FP16 assumption). If a
later target guarantees native FP16, it is a pure A4 peephole. M.4-honest (the promotion is
visible in the dump), M.2-deterministic. Resolved.

### 6.5 `LFieldAddr`/rodata fold is deferred to A4 (resolved)
isel emits the un-folded form (`MAluImm` ADD then `MLoad [base,0]`) so A2's golden dumps are
1:1 with the LIR and independently reviewable; the base+offset addressing-mode fold and the
MOVZ/FMOV-immediate peepholes are A4's encoder-side concern. M.1 (A2 does one thing:
selection), M.2 (predictable dumps). Resolved.

### 6.6 No genuine unresolved tension
Every point above resolves law-first. §6.3 is a confirmation touching an already-open fork
(D-2), not a new tension; the plan proceeds on its recommendation.
