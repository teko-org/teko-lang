# A3 / N2c — linear-scan register allocation (crumb plan)

**Status:** DESIGN (doc-only). Sub-PR of the 0.3 own-AOT-backend wave (umbrella
`remodel/backend-build`). Issue **#384**. Branch `fix/issue-384-regalloc` (the A3 mini-umbrella;
PR #398, base `remodel/backend-build`, already carrying A1+A2). Base for the design:
`docs/design/own-backend-architecture.md` §3.4 (regalloc) and its pipeline diagram (`:118-123`),
grounded in the merged A2 code (`src/backend/minst.tks`, `isel_arm64.tks`, `minst_interp.tks`, PR
#397) and A1's LIR (`src/lir/lir.tks`).

> This is a PLAN. It designs the target-independent **linear-scan register allocator** over A2's
> machine IR plus the **AAPCS64 ABI descriptor** that parameterizes it, decomposes A3 into ordered,
> independently gate-able sub-sub-PRs, and specifies the verification (the no-overlap invariant test
> + post-alloc interp-equivalence + golden dumps). A3 emits **no machine bytes** (A4) and does **no
> prologue/epilogue emission** (deferred, §6.1): it consumes an `MFunc` carrying virtual `MReg`s +
> ABI pins and produces an `MFunc` in which every `MReg` is physical (a colored register, or a
> reserved scratch fed by a spill load/store), the pins honored, no two overlapping live ranges
> sharing a physical register. The selected form re-interpreted after allocation still yields the
> same exit code.

---

## 1. The assessed starting point (the real A2 shape the allocator consumes)

A2 (#383) closed instruction selection. Every value in the selected `MFunc` is an `MReg`
(`src/backend/minst.tks:22`):

```
pub type MReg = struct { id: u32; reg_class: MRegClass; is_phys: bool }
```

`is_phys=false` is a **virtual** register the allocator colors; `is_phys=true` is a **physical
pin** isel already planted at an ABI boundary (`reg_class` is `GPR`/`FPR`, `minst.tks:11`). The
allocator's whole job is: rewrite every `is_phys=false` `MReg`, across every `MInst` field, to a
physical register (or a spill slot with load/store), honoring the pins, so no two overlapping live
ranges share a physical register. Four grounding facts fix the allocator's shape.

### 1.1 The def/use model per `MInst` (the load-bearing table)

Every `MInst` case (`minst.tks:787`, the 29-case variant) has a fixed set of **def** operands
(registers it writes) and **use** operands (registers it reads). This table IS the liveness input;
the allocator reads it and nothing else about an instruction's semantics. `d` = defines,
`u` = uses; `mem.base` is the `MMem` base register (`minst.tks:32`).

| `MInst` case | def | use | note |
|---|---|---|---|
| `MAlu{dst,a,b}` | `dst` | `a`, `b` | |
| `MAluImm{dst,a}` | `dst` | `a` | |
| `MMSub{dst,a,b,c}` | `dst` | `a`, `b`, `c` | **the only 3-use case** — sizes the scratch pool (§3.4) |
| `MNeg{dst,a}` / `MMvn{dst,a}` | `dst` | `a` | |
| `MFAlu{dst,a,b}` | `dst` | `a`, `b` | FPR class |
| `MFNeg{dst,a}` | `dst` | `a` | FPR class |
| `MCmp{a,b}` / `MFCmp{a,b}` | — | `a`, `b` | writes the flags (a non-register def, §1.2) |
| `MCmpImm{a}` | — | `a` | writes the flags |
| `MCSet{dst}` | `dst` | — | reads the flags |
| `MMovZ{dst}` / `MMovN{dst}` | `dst` | — | fresh materialization lane |
| `MMovK{dst}` | `dst` | **`dst`** | **read-modify-write**: merges a lane into `dst` (`minst.tks:406`) — `dst` is BOTH use and def |
| `MFMov{dst,src}` / `MMov{dst,src}` | `dst` | `src` | class-crossing (`MFMov`) or GPR↔GPR (`MMov`) |
| `MExt{dst,a}` / `MCvt{dst,a}` | `dst` | `a` | |
| `MLoad{dst,mem}` | `dst` | `mem.base` | base==dst is legal (§1.4) |
| `MStore{src,mem}` | — | `src`, `mem.base` | |
| `MFrameAddr{dst}` | `dst` | — | slot address; A3 finalizes the slot (§3.5) |
| `MAdrp{dst}` | `dst` | — | reloc; `sym` is not a register |
| `MAddLo{dst,a}` | `dst` | `a` | |
| `MBranch` / `MBranchCond` | — | — | terminator; `MBranchCond` reads flags |
| `MCbz{a}` | — | `a` | terminator |
| `MCall{uses,ret_reg,has_result}` | `ret_reg` iff `has_result` | every reg in `uses` | **clobbers caller-saved** (§1.3); `uses` are physical pins |
| `MCallIndirect{target,uses,ret_reg,has_result}` | `ret_reg` iff `has_result` | `target` + every reg in `uses` | **clobbers caller-saved** |
| `MRet` | — | the ABI result register (§1.2) | value pre-pinned to x0/v0 by a preceding move |

Two subtleties the table encodes and the implementer MUST honor:

- **`MMovK` is read-modify-write** (`minst.tks:406`, "keeping the other lanes"). Its `dst` is a use
  *and* a def. It is only ever emitted by `materialize_int` as a follow-up lane on the SAME fresh
  vreg a preceding `MMovZ`/`MMovN` first defined, so it never opens a new interval — but for
  last-use extension and for spill expansion it counts as a use of `dst`.
- **`MRet` carries no operand** (`minst.tks:777`): the returned value was already pinned into x0/v0
  by a preceding `MMov`/`MFMov` (`select_ret`, `isel_arm64.tks:1051-1060`). For liveness the pin's
  physical interval must reach the `MRet`; §1.2 gives the rule that keeps it alive.

### 1.2 Block-args are the SSA-lite merge; a value's interval is [def, last-use]

The LIR is SSA-lite with explicit **block-args** (`lir.tks:6-9`): each value is defined once, and a
merge passes block-args on the edge — so `phi` never exists and a value's live range is the
contiguous span from its single def to its last use across the linear instruction numbering. A2
already lowered every LIR merge to **edge moves** — `select_jump`/`select_branch` emit the
parallel-copy into the target block's `params` and split critical edges into their own machine
blocks (`isel_arm64.tks`, the A2-3 lowering; `MBlock.params`, `minst.tks:796-813`). Therefore by
the time the allocator runs, **every block-arg is already an ordinary vreg written by an edge
`MMov`**: the allocator treats `MBlock.params` and edge moves as plain defs/uses and needs no
phi machinery. Liveness is single-def, so an interval is exactly `[def-point, last-use-point]` over
the flattened numbering (§3.1) — with two carve-outs:

- **Flags** (`MCmp`/`MFCmp`/`MCmpImm` set them; `MCSet`/`MBranchCond` read them) are NOT registers
  and are NOT allocated. A2 always emits the compare and its consumer adjacently within one block
  (the fusion invariant, `minst_interp.tks:251-255` records the "single pending slot" model), so
  the allocator never has to keep two flag-defs live at once and simply ignores flags.
- **A pinned physical def with no later register use** (the `select_ret` `MMov preg(0), value` then
  `MRet`, `isel_arm64.tks:1056-1059`) has its fixed interval **extended to the block terminator**,
  so a result pin lives to the `RET` and the allocator will not reuse x0/v0 before the return.

### 1.3 The physical pins are pre-colored intervals; `MCall` clobbers caller-saved

isel plants physical registers (`preg(...)`, `minst.tks:886`) at exactly the AAPCS64 boundaries.
These are FIXED points the allocator must respect — it never re-colors them, it treats each as a
pre-colored interval that **blocks that physical register over its span**:

- **argument pins** — `select_param` (`isel_arm64.tks:1020-1039`) emits `MMov(param_vreg,
  preg(index))` at entry: `preg(index)` is live `[block-start, that move]` (a fixed *use* interval).
- **return pin** — `select_ret` (`isel_arm64.tks:1051-1060`): `preg(0)` is live `[that move, RET]`.
- **call-arg pins** — `pin_args` (`isel_arm64.tks:1620-1646`) emits `MMov(preg(idx), arg_vreg)`
  before the `MCall`, and `MCall.uses` lists exactly those pinned physregs (`minst.tks:716-740`):
  each `preg(idx)` is live `[its setup move, the call]`.
- **call-result pin** — `select_call_result` (`isel_arm64.tks:1660-1667`): `preg(0)` is live
  `[the call, the result move]`.

**`MCall`/`MCallIndirect` clobber every caller-saved register** (AAPCS64): a virtual value whose
interval *contains* a call instruction point cannot occupy a caller-saved register — it must be
callee-saved or spilled. The allocator models each call point as a clobber of the whole
caller-saved set for that class (§3.3). Because the arg-reg pins are themselves caller-saved and
sit right at the call, a value crossing the call is already forced off them by the clobber; a value
merely *between* a pin-setup move and the call is forced off that one reg by the fixed-interval
overlap. Both filters compose.

### 1.4 Spill slots reuse `MFunc.frame`; the interp already runs the physical form

`MFunc.frame: []lir::LAlloca` (`minst.tks:837`) is A2-4's frame-slot table — one `LAlloca{bytes,
align}` (`lir.tks:80`) per selected `LAlloca`, grown by `append_frame` (`minst.tks:1338`). A3
**reuses this table for spill slots**: each spilled vreg gets a fresh `LAlloca` slot appended, and
the spill store/reload are ordinary `MFrameAddr` + `MStore`/`MLoad` against that slot. This buys
the allocator a free oracle: the `minst_interp` (`minst_interp.tks`) already models

- four physical/virtual register files (`MRegFile`, `minst_interp.tks:259`) — a fully-colored
  (`is_phys=true`) function runs through the physical vectors with no interp change;
- entry args seeded into physical GPR arg registers (`seed_params`, `:395`) and results read from
  x0 (`interp_ret`/`interp_call`, `:802`/`:901`);
- `MFrameAddr(slot)` → a stable per-slot address (`frame_slot_addr`, `:843`) and `MStore`/`MLoad`
  round-tripping through it (`:867`/`:882`), with **base==dst legal** (`interp_load` reads the base
  before writing the dst, `:868-871`) — so the one-register reload micro-sequence `frame_addr sv,#k;
  ldr sv,[sv,#0]` interprets correctly.

**Consequence: A3 needs ZERO new interp cases.** The post-alloc equivalence oracle is
`minst_interp(regalloc(sel)) == minst_interp(sel)` over the runnable (`tk_exit`-terminated) subset,
reusing the A2 interpreter verbatim (§4). The one gap is honest and named: `minst_interp` only
*executes* `tk_exit` calls — user-function/`tk_*` calls are golden-dump-only until the interp
recurses into callees (`minst_interp.tks:901-908`), so it does **not** model caller-saved
clobbering at a real call. The cross-call clobber correctness is therefore proven by (a) the
structural no-overlap + call-clobber invariant tests (§4.1) and (b) A4's real C-vs-own byte
differential — never claimed from the interp (M.3).

---

## 2. The AAPCS64 ABI descriptor + register-file model (`src/backend/abi_aapcs64.tks`)

The core is **target-independent, parameterized by a descriptor** (`own-backend-architecture.md`
§3.4): AAPCS64 / SysV / Win64 / LP64D each contribute a small register-file descriptor; the
allocator is shared. A3 ships the AAPCS64 descriptor; the descriptor TYPE lives beside the core so
Phase B adds `abi_sysv.tks` etc. by cloning it.

### 2.1 The descriptor type

```teko
/**
 * AbiDescriptor — the target register-file + calling-convention facts the
 * shared linear-scan core reads: the argument/result registers, the
 * caller/callee-saved partition per register class, the ordered allocatable
 * pool the scan draws from, and the reserved spill-scratch registers the
 * rewrite reloads through. Register ids are ISA numbers in the `MReg`
 * convention (x0..x30/SP=31 for GPR, v0..v31 for FPR — `minst.tks:22`). Every
 * id list is iterated in its stored order, so allocation is deterministic
 * (M.2) and cloning this struct with a different partition is the whole cost
 * of a new ABI (Phase B).
 *
 * @since #384 A3-1
 */
pub type AbiDescriptor = struct {
    /**
     * gpr_arg — the GPR argument/return registers, in order (AAPCS64 x0..x7).
     * Index 0 is also the GPR result register.
     */
    gpr_arg: []u32
    /**
     * fpr_arg — the FPR argument/return registers, in order (AAPCS64 v0..v7).
     * Index 0 is also the FPR result register.
     */
    fpr_arg: []u32
    /**
     * gpr_allocatable — the GPR pool the scan assigns from, caller-saved
     * first then callee-saved (so short values take scratch-cheap caller-saved
     * before forcing a callee-save), excluding FP/LR/SP/the platform register
     * and the spill scratch (AAPCS64: x0..x15 then x19..x28, minus x15..x17).
     */
    gpr_allocatable: []u32
    /**
     * fpr_allocatable — the FPR pool the scan assigns from, caller-saved first
     * then callee-saved, excluding the spill scratch (AAPCS64: v0..v7, v16..v29
     * then v8..v15, minus v30..v31).
     */
    fpr_allocatable: []u32
    /**
     * gpr_caller_saved — the GPR set a call clobbers (AAPCS64 x0..x18); a
     * value live across a call must avoid these (§1.3).
     */
    gpr_caller_saved: []u32
    /**
     * fpr_caller_saved — the FPR set a call clobbers (AAPCS64 v0..v7, v16..v31;
     * only the low 64 bits of v8..v15 are callee-saved).
     */
    fpr_caller_saved: []u32
    /**
     * gpr_spill_scratch — GPR registers reserved OUT of the allocatable pool
     * for the spill reload/store micro-sequences (AAPCS64 x15,x16,x17 — IP0/IP1
     * plus one, all caller-saved so reserving them costs no prologue save).
     * Sized to the max simultaneous spilled operands of one instruction
     * (MMSub's three uses, §1.1).
     */
    gpr_spill_scratch: []u32
    /**
     * fpr_spill_scratch — FPR registers reserved for FPR spill reload/store
     * (AAPCS64 v30,v31 — max two simultaneous FPR uses, MFAlu).
     */
    fpr_spill_scratch: []u32
    /**
     * spill_slot_bytes — the byte size of one spill slot (8: a whole X or D
     * register; the value is stored full-width, never masked, matching the
     * size-agnostic interp §1.4).
     */
    spill_slot_bytes: u32
    /**
     * spill_slot_align — the alignment of one spill slot (8).
     */
    spill_slot_align: u32
}
```

### 2.2 The descriptor value + queries

```teko
/**
 * aapcs64 — the concrete AAPCS64 (AArch64) register-file descriptor: x0..x7/
 * v0..v7 arguments, x19..x28 + v8..v15(low) callee-saved, x0..x18 + v0..v7 +
 * v16..v31 caller-saved, x15..x17/v30..v31 reserved spill scratch. The
 * reference-target descriptor (`own-backend-architecture.md` §2.2); Phase B
 * clones its shape with a different partition.
 *
 * @return AbiDescriptor  the AAPCS64 register file
 */
pub fn aapcs64() -> AbiDescriptor { … }

/**
 * arg_reg — the physical argument/result register for per-class position
 * `index` (x0..x7 / v0..v7), or `ok=false` past the 8-register window — the
 * descriptor mirror of isel's `param_slot` (`isel_arm64.tks:1000`).
 *
 * @param AbiDescriptor abi  the register file
 * @param MRegClass reg_class  which register file the argument lives in
 * @param u32 index  the per-class argument position
 * @return ArgReg  the resolved physical register, or `ok=false`
 */
pub fn arg_reg(abi: AbiDescriptor, reg_class: MRegClass, index: u32) -> ArgReg { … }

/**
 * allocatable_pool — the ordered allocatable register ids for `reg_class`
 * (GPR: `gpr_allocatable`; FPR: `fpr_allocatable`), the pool the linear-scan
 * core draws a free register from, in its deterministic try-order.
 *
 * @param AbiDescriptor abi  the register file
 * @param MRegClass reg_class  which register file to enumerate
 * @return []u32  the ordered allocatable ISA register ids
 */
pub fn allocatable_pool(abi: AbiDescriptor, reg_class: MRegClass) -> []u32 { … }

/**
 * is_caller_saved — whether physical register `r` is clobbered by a call, so
 * a value live across a call point may not occupy it (§1.3).
 *
 * @param AbiDescriptor abi  the register file
 * @param MReg r  a physical register (is_phys must be true)
 * @return bool  true iff `r` is caller-saved in `r`'s class
 */
pub fn is_caller_saved(abi: AbiDescriptor, r: MReg) -> bool { … }

/**
 * is_callee_saved — whether physical register `r` is preserved across a call,
 * so a value live across a call point may safely occupy it (the complement of
 * `is_caller_saved` over the class's saved registers; FP/LR/SP are neither
 * allocatable nor reported here).
 *
 * @param AbiDescriptor abi  the register file
 * @param MReg r  a physical register (is_phys must be true)
 * @return bool  true iff `r` is callee-saved in `r`'s class
 */
pub fn is_callee_saved(abi: AbiDescriptor, r: MReg) -> bool { … }

/**
 * spill_scratch — the `nth` reserved spill-scratch register of `reg_class`
 * (GPR: `gpr_spill_scratch`; FPR: `fpr_spill_scratch`), the physical register
 * the rewrite reloads a spilled operand into (§3.4).
 *
 * @param AbiDescriptor abi  the register file
 * @param MRegClass reg_class  which register file the scratch lives in
 * @param u32 nth  the scratch index (0-based, < the class's reserved count)
 * @return MReg  the physical scratch register
 */
pub fn spill_scratch(abi: AbiDescriptor, reg_class: MRegClass, nth: u32) -> MReg { … }
```

`ArgReg` is the `param_slot`-shaped `struct { ok: bool; reg: MReg }` (`isel_arm64.tks:983-990`).

---

## 3. The allocator (`src/backend/regalloc.tks`, target-independent core)

`regalloc_module(abi, m: MModule) -> MModule | error` walks each `MFunc` and rewrites it in place.
Per function the pipeline is four passes: **number → intervals → scan → rewrite** — mirroring how
`select_module` walks (`isel_arm64.tks:1866`) and threading functional state (no mutable module).

### 3.1 Pass 1 — linear numbering (`number_insts`)

Flatten the function's instruction stream into one linear sequence in block order (blocks are
already in selection/topological order, `select_all_blocks`, `isel_arm64.tks:1824-1840`), assigning
each `(block, inst)` a strictly increasing **program point** `u32`. Block boundaries carry no gap —
edge moves already live inside the split edge blocks (§1.2), so a plain block-order flattening gives
correct linear liveness for the acyclic control flow A2 emits.

```teko
/**
 * NumberedInst — one instruction paired with its linear program point and the
 * block it belongs to (so the rewrite can re-emit into the right MBlock).
 *
 * @since #384 A3-2
 */
pub type NumberedInst = struct { point: u32; block_id: u32; inst: MInst }

/**
 * number_insts — flatten `f`'s blocks into one linearly-numbered instruction
 * stream in block order, the coordinate system every later pass indexes
 * liveness by (§3.1).
 *
 * @param MFunc f  the selected function
 * @return []NumberedInst  the flattened, numbered stream
 */
fn number_insts(f: MFunc) -> []NumberedInst { … }
```

> **Loop back-edges are out of A3's first cut and are a NAMED honest-stop (§6.2).** A2's control
> flow is the acyclic `if`/`match` lowering; a `loop` back-edge makes a value's live range
> non-contiguous over a strict linear numbering (a use precedes its dominating def in program-point
> order), which linear-scan handles only with live-range holes / interval union. A3-2 detects a
> back-edge (a branch/jump target whose id ≤ the current block id) and honest-stops with a named
> error, deferring loop-body allocation to the A3 loop follow-up.
>
> **§6.2 RESOLVED (#389 A3-loop pull-forward, owner ruling 2026-07-13 "fix fully now, any size").**
> The honest-stop's own wording — "needs live-range holes over the strict linear numbering" —
> **overstates the CORRECTNESS requirement.** True lifetime holes (interval range-lists so a value
> live across a loop but unused in a sub-region can share its register in the hole) are a
> *quality* optimisation (fewer spills), NOT a correctness prerequisite. The **smallest correct
> approach is loop-liveness extension**: after the naive `[firstDef, lastUse]` intervals are
> computed, for every natural loop `[header_pt, latch_pt]` (each retreating edge `has_back_edge`
> already finds), **extend any interval that is live at the loop header to reach at least
> `latch_pt`** — the classic "a value live at a loop header is live across the whole loop body"
> fixup. This closes the ONE real gap in the contiguous model: a value defined before/at the
> header and last-*used* mid-body would otherwise have its interval truncated at that mid-body
> use, letting a later-in-body value steal its register and clobber it for the next iteration.
> Extension is conservative (never underestimates liveness) and needs NO new interval
> representation — `LiveInterval` stays a single `[start, end]`; only `end` grows, and
> `crosses_call` is recomputed over the widened range. Because the A1 frontend threads EVERY
> loop-carried scalar through a loop-header block-param (`promote_env`, `lower.tks`) — whose
> destructed edge-move gives it an entry-edge def (low point) AND a back-edge def/use (high
> point) — its naive interval *already* spans `[entry, latch]`; the extension is therefore a
> no-op for today's integer-loop corpus but is the correctness insurance that lets the honest-stop
> be removed for ALL loop shapes (loop-invariants held in a reg across the body, alloca base
> pointers, future frontends). **True lifetime holes remain a deferred QUALITY follow-up (a
> separate named crumb), never a keystone blocker.**

### 3.2 Pass 2 — live-interval computation (`compute_intervals`)

Walk the numbered stream once, driven by the §1.1 def/use table. For each **virtual** `MReg`:
record its first def point as `start` and its last use point as `end` (`MMovK`'s `dst` counts as a
use). For each **physical** pin: build a fixed interval `[def, last-use]` blocking that physreg;
apply the §1.2 terminator-extension rule to a result pin. Record every `MCall`/`MCallIndirect`
point as a **call point**. Mark a virtual interval `crosses_call` iff a call point lies in
`(start, end]`.

```teko
/**
 * InstRegs — the def/use decomposition of one MInst (§1.1): its optional
 * defined register, its used registers, and whether it is a call (clobbering
 * the caller-saved set). The single source of truth both interval computation
 * and the rewrite read, so the def/use table lives in ONE place (M.5).
 *
 * @since #384 A3-2
 */
pub type InstRegs = struct { has_def: bool; def_reg: MReg; uses: []MReg; is_call: bool }

/**
 * inst_regs — decompose one MInst into its def/use registers per §1.1. A flat
 * per-case match over the 29-case MInst variant (`minst.tks:787`), mirroring
 * the `print_minst` dispatch shape (`minst.tks:1868`); MMovK reports `dst` in
 * BOTH `def_reg` and `uses` (read-modify-write), MCall/MCallIndirect set
 * `is_call` and report `ret_reg`/`uses`.
 *
 * @param MInst inst  the instruction to decompose
 * @return InstRegs  its def/use decomposition
 */
pub fn inst_regs(inst: MInst) -> InstRegs { … }

/**
 * LiveInterval — one value's live range over the linear numbering: the vreg
 * (or, for a fixed interval, the physreg) it belongs to, its class, its
 * [start,end] program-point span, whether it is a pre-colored physical pin,
 * and whether it spans a call point (so it must avoid caller-saved, §1.3).
 *
 * @since #384 A3-2
 */
pub type LiveInterval = struct {
    /**
     * reg_id — the virtual id being colored, or (for a fixed interval) the
     * physical ISA register id this interval blocks.
     */
    reg_id: u32
    /**
     * reg_class — which register file the value lives in.
     */
    reg_class: MRegClass
    /**
     * start — the program point of the value's single def (fixed intervals:
     * the pin's def point).
     */
    start: u32
    /**
     * end — the program point of the value's last use (fixed intervals: the
     * pin's last use, extended to the block terminator for a result pin, §1.2).
     */
    end: u32
    /**
     * is_fixed — true for a pre-colored physical pin the allocator must not
     * re-color, only avoid.
     */
    is_fixed: bool
    /**
     * crosses_call — true iff a call point lies in (start, end]; such an
     * interval must be assigned a callee-saved register or spilled.
     */
    crosses_call: bool
}

/**
 * IntervalSet — the computed intervals for one function: the virtual
 * intervals to color (start-sorted at scan time) and the fixed physical
 * intervals that block registers, plus the call points.
 *
 * @since #384 A3-2
 */
pub type IntervalSet = struct { virt: []LiveInterval; fixed: []LiveInterval; call_points: []u32 }

/**
 * compute_intervals — the §3.2 liveness pass: one walk of the numbered
 * stream building every virtual value's [def,last-use] interval, the fixed
 * pin intervals, and the call-point list.
 *
 * @param []NumberedInst stream  the linearly-numbered instruction stream
 * @return IntervalSet  the virtual + fixed intervals and call points
 */
fn compute_intervals(stream: []NumberedInst) -> IntervalSet { … }
```

### 3.3 Pass 3 — the linear-scan core (`linear_scan`)

Classic Poletto–Sarkar linear scan, extended for fixed intervals + call clobbers. Sort the virtual
intervals by `start` (tie-break by `reg_id` for determinism, M.2). Maintain an `active` set of
currently-assigned virtual intervals ordered by `end`. For each virtual interval `V` in start order:

1. **Expire** — remove from `active` every interval whose `end < V.start`, freeing its physreg.
2. **Candidate pool** — from `allocatable_pool(abi, V.reg_class)`, in its fixed order, keep only
   registers that are: (a) not currently held by an `active` interval; (b) free of any `fixed`
   interval overlapping `[V.start, V.end]`; and (c) if `V.crosses_call`, **not caller-saved**
   (`is_caller_saved`).
3. **Assign or spill** — if the candidate pool is nonempty, assign `V` the first candidate and add
   it to `active`. Otherwise **spill the furthest-next-use**: compare `V.end` against the maximum
   `end` in `active` (restricted to the same class); the loser (`V` itself, or the active interval
   with the largest `end`) is marked `Spilled` and the winner keeps/takes the freed register.

```teko
/**
 * RegAssignment — the outcome for one virtual register: a physical register
 * (`InReg`) or a frame spill slot (`Spilled`). The scan produces one per
 * virtual interval; the rewrite (§3.4) consumes it.
 *
 * @since #384 A3-3
 */
pub type RegAssignment = variant InReg | Spilled

/**
 * InReg — the virtual register was colored to physical ISA register `phys`.
 *
 * @since #384 A3-3
 */
pub type InReg = struct { vreg_id: u32; phys: u32 }

/**
 * Spilled — the virtual register was spilled to frame slot `slot` (an
 * `MFunc.frame` index the rewrite reserves, §3.5).
 *
 * @since #384 A3-3
 */
pub type Spilled = struct { vreg_id: u32; slot: u32 }

/**
 * ScanResult — the whole function's allocation: one RegAssignment per virtual
 * register (positionally by vreg id) plus the set of callee-saved physregs the
 * scan actually used (A4's prologue save-set, recorded now, emitted later §6.1).
 *
 * @since #384 A3-3
 */
pub type ScanResult = struct { assignments: []RegAssignment; used_callee_saved: []u32 }

/**
 * linear_scan — the §3.3 allocator: assign every virtual interval a physical
 * register or a spill decision, honoring fixed pin intervals and call
 * clobbers, spilling the furthest-next-use when no register is free. Sorts by
 * (start, reg_id) so the result is deterministic (M.2).
 *
 * @param AbiDescriptor abi  the register file to allocate from
 * @param IntervalSet ivals  the virtual + fixed intervals and call points
 * @return ScanResult  the per-vreg assignment + the callee-saved save-set
 */
fn linear_scan(abi: AbiDescriptor, ivals: IntervalSet) -> ScanResult { … }
```

### 3.4 Pass 4 — the rewrite (`rewrite_func`)

Rewrite every `MInst`, mapping each `MReg` operand through the assignment and expanding spills:

- **`InReg` operand** → replace the virtual `MReg` with `preg(phys, reg_class)`.
- **physical operand** (a pin, `is_phys=true`) → passed through unchanged.
- **`Spilled` use** → immediately BEFORE the instruction, emit the one-register reload
  `frame_addr sv,#slot; ldr sv,[sv,#0]` where `sv = spill_scratch(abi, class, nth)` (§1.4, base==dst
  legal); substitute `sv` for that operand. `nth` is bumped per distinct spilled use of the
  instruction (max 3, MMSub — §1.1 sizes the pool).
- **`Spilled` def** → compute into a scratch `sv = spill_scratch(abi, class, 0)`, then immediately
  AFTER the instruction emit `frame_addr sv2,#slot; str sv,[sv2,#0]` (a def-scratch distinct from
  its own store-address scratch, drawn from the same reserved pool).

```teko
/**
 * map_minst_regs — a copy of `inst` with every MReg field replaced by `f(reg)`
 * (a flat per-case reconstruction over the 29-case variant, `minst.tks:787`,
 * mirroring `print_minst`'s dispatch). MCall/MCallIndirect map their `uses`
 * element-wise and their `ret_reg`. The single point where the machine-IR's
 * register-field shape is walked for rewriting — no case may be missed (a
 * missed case leaks a virtual register into the output, caught by the §4.1
 * all-physical invariant test).
 *
 * @param MInst inst  the instruction to rewrite
 * @param fn(MReg)->MReg f  the per-register substitution
 * @return MInst  the rewritten instruction
 */
fn map_minst_regs(inst: MInst, f: fn(MReg) -> MReg) -> MInst { … }

/**
 * rewrite_inst — expand one numbered instruction into its final physical
 * sequence: the spill reloads for its spilled uses, the register-substituted
 * instruction (`map_minst_regs`), and the spill store for a spilled def (§3.4).
 * Non-spilled operands map straight to their `InReg` physreg; physical pins
 * pass through.
 *
 * @param AbiDescriptor abi  the register file (for spill scratch)
 * @param ScanResult sr  the allocation
 * @param MInst inst  the instruction to rewrite
 * @return []MInst  the reloads ++ the rewritten instruction ++ the store
 */
fn rewrite_inst(abi: AbiDescriptor, sr: ScanResult, inst: MInst) -> []MInst { … }

/**
 * rewrite_func — the §3.4 pass: rebuild every block from its rewritten
 * instruction sequences and finalize the frame (§3.5), yielding an MFunc whose
 * every MReg is physical.
 *
 * @param AbiDescriptor abi  the register file
 * @param MFunc f  the selected function
 * @param ScanResult sr  the allocation
 * @return MFunc  the fully-colored function
 */
fn rewrite_func(abi: AbiDescriptor, f: MFunc, sr: ScanResult) -> MFunc { … }
```

### 3.5 Frame finalization + the module driver

Each `Spilled` vreg reserves a fresh slot by appending an `LAlloca{bytes = abi.spill_slot_bytes;
align = abi.spill_slot_align}` to `MFunc.frame` (`append_frame`, `minst.tks:1338`); the `Spilled.slot`
is that entry's index. The interp resolves each slot to a distinct stable address
(`frame_slot_addr`, `minst_interp.tks:843`), so a store→reload round-trips (§1.4). The
byte-offset-from-FP/SP resolution of `MFrameAddr` and the prologue/epilogue that reserves the frame
and saves `used_callee_saved` are A4's (deferred, §6.1) — A3 finalizes the *slot table*, not the
stack-pointer arithmetic.

```teko
/**
 * regalloc_func — allocate one function end-to-end: number, compute
 * intervals, scan, rewrite. The unit the module driver maps over.
 *
 * @param AbiDescriptor abi  the register file
 * @param MFunc f  the selected function (virtual MRegs + ABI pins)
 * @return MFunc | error  the fully-colored function, or a NAMED honest-stop
 *                         (a loop back-edge §6.2, or an internal invariant)
 */
pub fn regalloc_func(abi: AbiDescriptor, f: MFunc) -> MFunc | error { … }

/**
 * regalloc_module — allocate every function of `m`, carrying the rodata/
 * globals/layouts tables through unchanged (A3 touches only registers +
 * frame, exactly as `select_module` carries them, `isel_arm64.tks:1866`).
 *
 * @param AbiDescriptor abi  the register file to allocate from
 * @param MModule m  the selected module
 * @return MModule | error  the colored module, or the first function's error
 */
pub fn regalloc_module(abi: AbiDescriptor, m: MModule) -> MModule | error { … }
```

---

## 4. Verification — the no-overlap invariant + post-alloc interp-equivalence

Two legs, both machine-byte-free (A3 emits nothing to link), mirroring how A2 pairs a structural
golden check with the interp oracle.

### 4.1 The structural invariants (the no-overlap + pin + all-physical tests)

Over the colored `MFunc`, a checker (in `regalloc_test.tkt`, or a small pub helper `verify_alloc`
in `regalloc.tks` the test drives) asserts:

1. **All-physical** — every `MReg` in every `MInst` field has `is_phys=true` (no virtual register
   survived; catches a missed `map_minst_regs` case).
2. **No overlap** — recompute intervals over the colored form; assert no two *distinct*-value
   intervals that overlap share a physical register id in the same class. This is the core
   correctness property (`own-backend-architecture.md` §3.4, "no two overlapping live ranges sharing
   a physical register") and the direct A3 oracle for the linear scan.
3. **Pins honored** — every original physical pin (arg/ret/`MCall.uses`) still holds its ISA
   register in the output (the allocator did not steal x0 from a `tk_exit` code, etc.).
4. **Call-clobber** — no colored value whose interval crosses a call point sits in a caller-saved
   register (the §1.3 property the interp cannot see, §1.4).

### 4.2 The post-alloc interp-equivalence (the semantics oracle)

Reusing the A2 `minst_interp` verbatim (§1.4), assert for each runnable fixture:

```
minst_interp(regalloc_module(aapcs64(), select_module(m)), "main", args)
    == minst_interp(select_module(m), "main", args)
```

i.e. allocation only renames registers / adds spill moves, never changes semantics — the exit code
is preserved. Because A2 already proved `minst_interp(select_module(m)) == interp_lmodule(m)`, this
transitively pins the colored form to the LIR oracle. The fixture corpus is A2's own runnable
fixtures (the straight-line, control-flow, and memory programs that terminate in `tk_exit`), driven
through regalloc — a KNOWN-GOOD oracle for exactly the programs A2 proved select correctly. A
fixture reaching a `minst_interp` honest-stop (a real user call, §1.4) is compared at the
honest-stop identically on both sides, never at a fabricated value (the A2 §4 precedent).

### 4.3 Golden allocation dump

The existing `minst_print` (`minst.tks:1950`) renders the colored `MFunc` — every operand now
`%pN`. Unit tests build a small `MFunc` by hand (virtual regs + a couple of pins, or a
force-a-spill fixture with more simultaneously-live values than allocatable registers via a
tiny test-only descriptor), run `regalloc_func`, and `str_contains`-assert the physical registers +
the spill `frame_addr`/`ldr`/`str` sequences — the allocator's version of the diff spine. A
**tiny test descriptor** (e.g. 2 allocatable GPRs) makes spilling reachable and 100%-covered on
small inputs without needing a 30-live-value program.

---

## 5. The sub-sub-PR decomposition (ordered, each gate-able)

A3 is XL, so it drains as four sub-sub-PRs on `fix/issue-384-regalloc`, in order. Each owes the full
ritual — the **both-engine gate** (native `teko . -o bin` AND `teko test .` VM, per the hard lesson
that the diff-VM==native lane runs the VM; watch the `x = match{…return}` VM gotcha — use the
`let`-then-assign shape, `isel_arm64.tks:1831-1835`) · paranoid · fixpoint — and **100% coverage on
its new code**; each names the fixtures that PROVE it. No machine bytes in any of them.

### A3-1 · the AAPCS64 ABI descriptor + register-file model
- **Scope:** new `src/backend/abi_aapcs64.tks` — the `AbiDescriptor` type (§2.1), the `aapcs64()`
  value, and the queries `arg_reg`/`allocatable_pool`/`is_caller_saved`/`is_callee_saved`/
  `spill_scratch` (§2.2), plus the `ArgReg` result shape.
- **Files:** new `src/backend/abi_aapcs64.tks`; tests `src/backend/abi_aapcs64_test.tkt`.
- **Deps:** A2 (merged — needs `MReg`/`MRegClass`/`preg`).
- **Proven by:** unit tests — `arg_reg(GPR,0..7)` = x0..x7 and `ok=false` at 8; `arg_reg(FPR,..)` =
  v0..v7; the allocatable pools exclude FP/LR/SP/x18 + the scratch and list caller-saved first;
  `is_caller_saved` true for x0..x18/v0..v7/v16..v31 and false for x19..x28/v8..v15;
  `spill_scratch(GPR,0..2)`=x15..x17, `(FPR,0..1)`=v30..v31. Pure data — no engine divergence
  possible, but gated both engines for the ritual.

### A3-2 · linear numbering + live-interval computation
- **Scope:** new `src/backend/regalloc.tks` seeded with `NumberedInst`/`number_insts` (§3.1),
  `InstRegs`/`inst_regs` (the §1.1 def/use table — the single source of truth), `LiveInterval`/
  `IntervalSet`/`compute_intervals` (§3.2, including fixed pin intervals, the result-pin terminator
  extension, call-point marking, `crosses_call`), and the loop back-edge NAMED honest-stop (§6.2).
  No allocation yet.
- **Files:** `src/backend/regalloc.tks`; tests `src/backend/regalloc_test.tkt`.
- **Deps:** A2 (merged). Independent of A3-1 (interval computation reads no ABI), but sequenced
  after it for a single clean lane.
- **Proven by:** golden interval-dump assertions on hand-built `MFunc`s — a straight-line add's two
  param vregs get `[entry, use]` intervals; a `select_ret` result pin's fixed x0 interval reaches
  the `MRet`; an `MMovK` follow-up extends its dst's interval; a call fixture marks the caller's
  live value `crosses_call` and lists the call point; `MMSub` reports three uses. A back-edge
  fixture hits the honest-stop.

### A3-3 · the linear-scan core (active set · expire · assign-or-spill honoring pins + clobbers)
- **Scope:** `RegAssignment`/`InReg`/`Spilled`/`ScanResult` (§3.3) and `linear_scan` — the
  start-sorted scan, the active set + expire-old, the fixed-interval + call-clobber candidate
  filter, and the furthest-next-use spill heuristic. Deterministic (sort tie-break by `reg_id`).
  Produces the assignment; no rewrite yet.
- **Files:** `src/backend/regalloc.tks` (+ the scan); tests in `regalloc_test.tkt`.
- **Deps:** A3-1 (the pool/caller-saved queries), A3-2 (the intervals).
- **Proven by:** unit tests on crafted `IntervalSet`s — two disjoint intervals reuse one register;
  two overlapping intervals get distinct registers; a `crosses_call` interval is assigned a
  callee-saved register (never a caller-saved one, even when caller-saved are free); a value
  overlapping an arg-pin's fixed interval avoids that reg; forcing more simultaneously-live values
  than a **tiny test descriptor**'s pool spills the furthest-next-use. The **no-overlap invariant
  check** (§4.1.2) is asserted directly on `linear_scan`'s output.

### A3-4 · the rewrite pass + frame finalization + post-alloc interp-equivalence
- **Scope:** `map_minst_regs` (the total 29-case register-field walk), `rewrite_inst` (spill
  reload/store expansion, §3.4), `rewrite_func` + frame finalization (§3.5), `regalloc_func`, and
  `regalloc_module`. The **all-physical + no-overlap + pins-honored + call-clobber** structural
  checks (§4.1) and the **post-alloc interp-equivalence harness** (§4.2) land here, reusing the A2
  `minst_interp` verbatim (zero new interp cases, §1.4).
- **Files:** `src/backend/regalloc.tks` (+ the rewrite); tests `src/backend/regalloc_test.tkt` (+ a
  `regalloc_interp_test.tkt` for the equivalence leg, or fold into `regalloc_test.tkt`).
- **Deps:** A3-3.
- **Proven by:** golden colored-dump (a two-param add → all `%pN`, a spilled value → the
  `frame_addr`/`ldr`/`str` sequence via the tiny descriptor) + the four structural invariants over
  the colored form + `minst_interp(regalloc(sel)) == minst_interp(sel)` on A2's runnable
  straight-line/control/memory/`tk_exit` fixtures. **A3 CLOSES here.**

### Ordering
```
A2 (done) ─▶ A3-1 ─▶ A3-2 ─▶ A3-3 ─▶ A3-4   [#384 closes]
```
A3-1 and A3-2 are independent (the ABI descriptor vs the ABI-free interval computation) and could
interleave, but the single-lane sequence above keeps one heavy build at a time (the Mac's
one-build-at-a-time law) and gives the scan (A3-3) both inputs already merged.

---

## 6. Deferrals + recorded decisions (law-first)

Each is framed against the Constitution laws (M.0 self-host · M.1 small/orthogonal · M.2
deterministic · M.3 honest · M.4 no-hidden-cost · M.5 one-way). None is an unresolvable HALT.

### 6.1 Prologue/epilogue + FP/SP frame arithmetic are DEFERRED to A4 (named: A4-frame)
A3 finalizes the *frame slot table* (`MFunc.frame` grows one entry per spill) and records the
`used_callee_saved` save-set, but it does **not** emit the prologue/epilogue (STP/LDP of the
callee-saved registers, `SUB sp, sp, #framesize`) or resolve `MFrameAddr`'s slot→FP/SP byte offset.
That is A4's job: `MFrameAddr` is defined as "resolved once A3 finalizes the frame layout"
(`minst.tks:594-603`) — A3 delivers the *layout* (slot count + save-set), A4 does the stack-pointer
arithmetic and the actual STP/LDP encoding alongside the Mach-O frame. **Law-first:** the interp
models registers + slots directly and needs no stack pointer (§1.4), so the A3 gate is complete
without prologue emission (M.4 — no hidden cost added, the frame data is honestly recorded for A4);
splitting stack-pointer materialization to the encoder keeps A3 doing one thing (M.1). Named A4
follow-up: **A4-frame** (prologue/epilogue + `MFrameAddr` offset resolution + save/restore of
`ScanResult.used_callee_saved`).

### 6.2 Loop back-edges are a NAMED honest-stop in A3-2 (named: A3-loop)
A2's runnable control flow is the acyclic `if`/`match` lowering; a `loop` back-edge makes a live
range non-contiguous over a strict linear numbering (a use at a program point *before* the
dominating def), which basic linear-scan mishandles. A3 detects a real back-edge as a **retreating
edge in RPO** (`pos(target) ≤ pos(source)`) and honest-stops with a named error rather than
mis-allocate (M.3). **Law-first:** an honest-stop that names the deferral beats a silently-wrong
allocation; the acyclic subset is fully allocated (all `if`/`match` nesting), only true `loop`
back-edges stop.

> **✅ RESOLVED by #443 (RPO position back-edge test), 2026-07-09.** The earlier `target id ≤ block
> id` proxy was only a true back-edge test when block ids were topological, which A1 lowering does
> NOT guarantee (`lower_if_value` allocates the merge block before the arm bodies, so a nested arm's
> tail gets a higher id than the merge it targets → a backward-in-id but forward-in-CFG `LJump`). The
> proxy therefore false-positived on acyclic nested control flow AND the strict linear numbering was
> itself inverted for such blocks. **Fix (#443):** `regalloc_func` computes an RPO block order once
> (`rpo_block_order`) and threads it — `number_insts` flattens in RPO (so every def precedes its uses;
> the inversion is gone) and `has_back_edge` tests RPO position (so acyclic nesting has no retreating
> edge and allocates, while a real `loop` latch→header still retreats and honest-stops). Renumber, not
> reorder: block ids + `MFunc.blocks` order + branch targets are untouched, so the interp is provably
> unaffected. See `docs/design/backend-443-rpo-numbering.md`.

> **✅ REFINED by #385 A4-6 (dead-block reachability filter), 2026-07-10.** #443 fixed the acyclic
> false-positive for the REACHABLE blocks, but did not cover a second acyclic false-positive it did not
> yet exercise: a `match` whose final arm is IRREFUTABLE (a `_` wildcard) lowers its last pattern test
> as an UNCONDITIONAL jump into that arm's body, so the defensive-fallback block `lower_match_value`
> emits past the last arm has NO predecessor — it is UNREACHABLE. `rpo_block_order`'s `append_unvisited`
> still numbers it (keeping the numbering total, so its dead vregs are not dropped), placing it LAST;
> its own forward jump to the merge block therefore RETREATS by RPO position (`pos(merge) ≤ pos(dead)`)
> and `has_back_edge` false-tripped on a pure acyclic DAG (the A4-5 `own_match_exit` fixture's
> KNOWN_STOP). **Fix (A4-6):** `has_back_edge` (and `compute_intervals`/`regalloc_func`) now thread the
> entry-reachable block set (`reachable_blocks`, the DFS-visited set) and consider a retreating edge a
> back-edge ONLY when its SOURCE block is reachable — a dead block cannot form a loop the allocator must
> model. Real `loop` latch→header edges retreat between two reachable blocks and still honest-stop.

Named follow-up: **A3-loop** — live-range holes / interval union over back-edges (the
standard linear-scan-with-holes extension), landed when A1/A2 exercise `loop` end-to-end through the
interp. This is stated up front so no one claims A3 covers loops.

> **✅ LANDED by #389 F2 (loop-liveness extension), 2026-07-13.** The honest-stop is REMOVED across
> all three regalloc backends (`regalloc.tks`/`regalloc_x86.tks`/`regalloc_riscv.tks`): each
> `compute_intervals*` now describes the loops via `natural_loops*` (one `NaturalLoop{header_pt,
> latch_pt}` per retreating edge — the §6.2 replacement for the bare `has_back_edge` bool) and
> widens every header-live interval over its loop body with the shared, register-abstract
> `extend_over_loops` (only `end` grows; `crosses_call` recomputed; `LiveInterval` stays a single
> `[start,end]`). The x86 lane is the executing linux-x86_64 own==C differential and the riscv lane
> runs under QEMU, so the extension is mirrored to all three — own-native now compiles range-`for`
> (and every) loop and matches C (`own_loop_range_native`, `own_loop_nested_range`,
> `scripts/diff_c_own.sh`). True lifetime holes remain the deferred QUALITY-only follow-up.

### 6.3 Spilling is "spill-everywhere via reserved scratch," the simple correct first cut (named: A3-splitting)
The furthest-next-use heuristic + a fixed reserved scratch pool (x15..x17 GPR, v30..v31 FPR) reloading
each spilled operand right before its use is the smallest **correct** allocator on a load/store ISA
(ARM64 can't operate on memory operands). It costs code quality (a reserved-scratch reservation
shrinks the allocatable pool by 3 GPR / 2 FPR — all caller-saved, so **zero prologue cost**; and
reload-per-use duplicates loads). **Law-first:** correct-and-simple-and-deterministic (M.1/M.2/M.3)
before fast; the reservation is a visible, honest cost recorded in the descriptor (M.4), tunable
per-target. Named follow-up: **A3-splitting** — linear-scan with interval splitting / second-chance
that reloads into a normally-allocated register and removes the scratch reservation. Not needed for
the A4 differential to be correct (only for it to be *lean*), so it sequences after the backend is
proven end-to-end.

### 6.4 i128 register-pair values ride A2's existing i128 deferral (no new A3 decision)
A2 already honest-stops i128 parameters/materialization (`select_param`, `isel_arm64.tks:1025-1027`;
isel §6.3, the `tk_*_i128` helper route). Since A2 never hands A3 a raw i128 register-pair value,
A3 inherits that boundary unchanged — an i128 lowered to helper calls presents to A3 as ordinary
64-bit GPR values (the helper's args/result). No separate A3 pair-allocation is designed; when the
i128-inline route lands in isel, its two-GPR values are just two ordinary GPR intervals here (M.1 —
the allocator already handles them). Recorded, not open.

### 6.5 No genuine unresolved tension
Every deferral above is a scope boundary resolved law-first with a named follow-up, not a HALT. The
allocator's correctness bar (§4) is fully provable on the acyclic, non-i128, `tk_exit`-terminated
subset A2 runs today, so A3 ships a complete, gated deliverable and the deferrals are honestly
scoped forward.
