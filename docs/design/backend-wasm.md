# C1 / N6a+N6b — Wasm backend: stackifier + register-bypass (WASI + Browser) (crumb plan)

**Status:** DESIGN (doc-only). Sub-PR of the 0.3 own-AOT-backend wave (umbrella
`remodel/backend-build`). Issue **#389** (C1 settles the old `#224` Wasm stub). Branch
`design/389-backend-wasm` (base `remodel/backend-build`, already carrying A1's LIR). Base for the
design: `docs/design/own-backend-architecture.md` §4 Phase C (`:311-321` — C1 depends on A1, NOT
A2/A3) + §3.3/§3.4 (the per-ISA / register-file split), the LIR contract's reserved Wasm route
(`src/lir/lir.tks:6-9`, "a register IR serves both register targets (linear-scan) and Wasm (its own
stackify)"), the labeled-loop lowering (`docs/design/loop-extensible.md` §1, #517; break/continue
#520), grounded in the merged A1 code (`src/lir/lir.tks`, `src/lir/lower.tks`) and the isel
edge-move shape it reuses (`src/backend/isel_arm64.tks:1337`).

> This is a PLAN. It designs the **third differential target and the first non-register backend**:
> the register-IR → Wasm **stackifier** (`src/backend/stackify.tks`) that reconstructs structured
> `block`/`loop`/`if`/`br`/`br_if` from A1's unstructured, block-arg LIR CFG, and the module writer
> (`src/backend/objfile_wasm.tks`) that emits the wasm binary sections. C1 **bypasses A2 isel and A3
> regalloc entirely** (Fork = Option A, owner-ratified 2026-07-12): it consumes `LModule` directly,
> maps every LIR VReg to a wasm local, and lets the loading engine (V8 TurboFan / wasmtime Cranelift)
> JIT the real register allocation. It emits **no** physical registers, **no** spill slots, **no**
> two-pass branch-displacement patcher (a wasm `br` is a relative label depth, not a PC displacement).
> Arena stays in linear memory (**no WasmGC**), identical to native. C1 does **not** add the
> `--backend` flag (D1/#390 owns it — C1 uses the temporary `TEKO_BACKEND=wasm` env seam), does
> **not** build an own wasm linker (the module IS the container; the WASI runtime links via
> `wasm-ld`, the cc-as-linker analogue), and does **not** widen A1's lowering. The hard tail (i128,
> f16, SIMD) is scoped forward as **named** honest-stops, each behind a specific slice.

---

## 0. TL;DR — the recommended shape

- **Sub-slice order (each independently gate-able, smallest-safe-step):**
  `C1-1` value-type model + VReg→local map + `objfile_wasm` module skeleton (emit a trivial
  `exit(n)` function) → `C1-2` straight-line lowering, **ALL-LOCALS baseline** (no value-stack opt)
  → `C1-3` the **stackifier core** (RPO + dominators + the labeling rule + `if`/`else` baseline +
  forward merges) → `C1-4` **loops** (natural-loop extents + break/continue/labeled-break) → `C1-5`
  the **value-stack peephole** (single-use/same-block/LIFO/no-barrier drops `local.set/get`) → `C1-6`
  **runtime modes + the panic-hook trap** (WASI ABI interop + the Teko-native capability-import
  firewall) → `C1-7` **wasm32 i64→i32 checked narrowing** (+ the wasm64 switch) → `C1-8` `emit_native`
  target-dispatch + **the own-wasm == C-native == interp differential** (the keystone; **#389 CLOSES
  here**).
- **Register-bypass (the fork).** No A2/A3. Every LIR VReg → a wasm local (`local.get`/`local.set`);
  every `LBlock.param` (SSA-lite merge) → a local; on each control edge a **parallel-copy** of source
  locals into target-param locals (mirroring isel's `emit_edge_moves`, `isel_arm64.tks:1337`). So
  every `block`/`loop`/`if` has a **VOID signature** and the operand stack is **empty at every label
  boundary** — this single choice kills the classic loop-result-arity and cross-edge-stack bugs.
- **Stackifier.** Reducible LIR CFG (guaranteed — no goto; break/continue target enclosing loops) →
  dominator-based reconstruction (Ramsey "Beyond Relooper", ICFP 2022, specialized to reducible
  input: zero node duplication, zero dispatch var, no `FixIrreducibleControlFlow`, no Relooper
  Multiple). Loop header → `loop`, forward merge/loop-exit → `block`, two-way branch into dominated
  successors → `if`/`else`, back edge → `br` to `loop` (continue), break → `br` to the wrapping
  `block` end.
- **★★ Two load-bearing correctness points (§4.3, §4.7 — a future implementer MUST NOT get these
  wrong):** (1) a block needs a `block` label **iff it has an incoming FORWARD edge that is not the
  RPO fall-through edge** (NOT "≥2 forward preds") — AND `if`/`else` is the **baseline** for two-way
  branches whose successors the branch dominates (arms inline, no label); loop-exit/break targets get
  a `block` **unconditionally**. (2) a loop's `end` closes after the **natural-loop body** (blocks
  that can reach the back-edge source), **NOT** the dominance subtree (which swallows the exit +
  post-loop code when the loop is entered unconditionally).
- **No genuine HALT.** Every decision is resolved law-first and recorded (§11): consume LIR directly
  (M.1), block-args as locals not block signatures (M.1/M.2), i128/f16 as named stops (M.3),
  `wasm-ld` as the cc-as-linker analogue (M.4, superseded by no external linker being needed for a
  self-contained module). One adjacent finding (the extensible-loop continue-runs-the-step property
  living in the desugar, invisible to wasm validation) is **REPORTED up** (§11.5), not resolved here.

---

## 1. The assessed starting point (what C1 consumes / REUSES vs PARALLELS)

C1 is the first backend that proves the LIR contract is *target-independent past the register model*:
it takes A1's `LModule` and produces bytes without ever building an `MFunc`. The grounding audit,
file by file.

### 1.1 REUSED VERBATIM (LIR contract, the #443 RPO algorithm, the isel edge-move shape)

| Symbol(s) | Home | Why reused |
|---|---|---|
| `LModule`/`LFunc`/`LBlock`/`LInst`/`LOp` + every op case | `lir.tks:126-155` | the whole input alphabet; C1 is a total match over `LOp` |
| `LType`/`LBinOp`/`LUnOp` (the frozen scalar enums) | `lir.tks:18-41` | machine classes + opcodes map 1:1 onto wasm value types + opcodes (§3.2, §4-appendix) |
| `LJump`/`LBranch`/`LRet` (the closed terminator set) | `lir.tks:67-75` | the CFG C1 stackifies — **no** `br_table`, no computed jump, so reducibility is structural (§4.1) |
| `LBlock.params` + positional block-args on `LJump`/`LBranch` edges | `lir.tks:136`, `:71-75` | the SSA-lite merge C1 lowers to a per-edge parallel-copy into locals (§4.6) |
| `layout_of`/`layout_of_sized`/`ltype_size`/`ltype_align`/`align_up` | `lir.tks:455-545` | struct/alloca layout — declared-order natural alignment, so own-wasm agrees with C-native by construction |
| `emit_u32_le` | `encode_arm64.tks:175` (pub) | the little-endian byte-buffer primitive for the fixed 8-byte wasm magic+version header |
| `align_up` (byte-buffer sense) | `encode_arm64.tks` (same-ns) | section/data padding |
| the **#443 RPO algorithm + reducibility theorem** | `regalloc.tks:163-177` (`rpo_block_order`) + `docs/design/backend-443-rpo-numbering.md:105-109` | "retreating edge == back edge" is exact on Teko's reducible CFG — C1's back-edge detection *is* this theorem (§4.2) |
| the **parallel-copy edge-move shape** | `isel_arm64.tks:1337` (`emit_edge_moves`) + `:1404` (`resolve_edge`) | block-args → locals on each edge, cycle-broken by a temp; C1 mirrors the shape over locals not MRegs (§4.6) |

> **Namespace fact (load-bearing, from B1's audit).** In Teko `pub` = cross-NAMESPACE export and a
> bare `fn` = namespace-private (visible to every file in `teko::backend`). C1's two new files live in
> the SAME `teko::backend` namespace, so they reuse `emit_u32_le`, `align_up`, and the `lir::*`
> contract with no `use` and no re-export — and, per the silent-bare-name-collision gotcha, they MUST
> NOT re-declare any of those names. Every new wasm declaration therefore carries a `Wasm`/`W`/`wasm`
> marker (§1.3).

### 1.2 What C1 PARALLELS (its own stackify + module writer; bypasses A2/A3/A4 encoders)

| The register pipeline does | C1 does instead | Why it cannot reuse |
|---|---|---|
| `rpo_block_order(MFunc)` (`regalloc.tks:173`) | `rpo_block_order_wasm(LFunc)` (§4.2) | same algorithm + #443 theorem, over `LFunc` terminators (`LJump`/`LBranch`/`LRet`), not `MFunc`'s `MInst`s — exactly as B1 has its own `rpo_block_order_x86(MFuncX86)`, `regalloc_x86.tks:226` |
| A2 `select_module` → physical-shaped `MInst` | `stackify_func` lowers `LOp` straight to wasm opcodes | wasm has no register operands to select for; the engine JITs selection at load |
| A3 `regalloc_module` (linear-scan, spills, pins) | **nothing** — VReg → local, one linear pass (§3) | wasm has no finite register file; the engine owns allocation (§3.1) |
| A4 `encode_arm64` fixed-4-byte words + two-pass branch displacement patch | LEB128 opcode stream + relative label depths (§4.4, §9) | a wasm `br` is a scope-relative label immediate, not a PC-relative displacement — nothing to patch |
| `emit_macho` / `emit_elf` / `emit_coff` + `cc`/`ld` link | `emit_wasm_module` → a self-contained module that IMPORTS its runtime; WASI links `teko_rt.wasm` via `wasm-ld` (§9, §11.4) | the wasm module IS the container; there is no relocatable-object + external-linker step for a single-module emit |

### 1.3 The naming convention (silent-collision-safe)

Every new wasm declaration lives in `teko::backend` with an explicit marker so it CANNOT collide with
a frozen A-slice name (the bare-name-collision gotcha is silent — no diagnostic). The set:
`WasmValType`/`WasmFunc`/`WasmImport`/`WasmSectionId` (types) ; `WScopeKind`/`WScope` (the
stackifier's scope-stack frame) ; `wasm_valtype_of`/`stackify_func`/`rpo_block_order_wasm`/
`dominators_wasm`/`natural_loop_body`/`needs_block_label`/`emit_edge_copies`/`br_depth` (functions,
`src/backend/stackify.tks`) ; `emit_wasm_module`/`emit_uleb`/`emit_sleb`/`emit_wasm_section`
(`src/backend/objfile_wasm.tks`). The neutral reuse (`emit_u32_le`, `align_up`, `lir::*`) stays bare.

---

## 2. The IR decision — consume LIR directly, arena in linear memory, NO WasmGC (law-first, NO HALT)

The task could pose a HALT ("wasm value model vs the register IR"). It has a clear winner; recorded,
not opened. Three decisions, each ratified 2026-07-12 by the owner and re-derived law-first here.

**A · Consume `LModule` directly — the Fork is Option A (bypass A2 isel + A3 regalloc).**
`own-backend-architecture.md:317` already fixes it: "Wasm depends on A1, NOT A2/A3 (Wasm has its own
selection/no regalloc)." wasm has no finite register file — values live on an unbounded operand stack
+ unbounded indexed locals, and the consuming engine's JIT (V8 TurboFan, wasmtime Cranelift) performs
the REAL register allocation + spilling at load time against the actual host ISA. Running our own
linear-scan first is wasted work the engine redoes and cannot improve on. **M.1 (small/orthogonal) +
M.4 (no hidden cost):** the shortest correct path is LIR → wasm; A2/A3 add nothing a wasm consumer
keeps. The parallel `minst_x86`/regalloc mirroring B1 pays (a target with registers) buys C1 nothing.

**B · Arena in linear memory — NO WasmGC.** The Teko memory model is arena/region in a flat address
space (no GC); linear memory reproduces it byte-for-byte, so `LStructLayout`/`LAlloca`/`LLoad`/
`LStore` lower unchanged and the C-vs-own differential holds by construction. WasmGC would fork the
memory model, require GC types the native backends do not have, and break layout parity. **M.5 (one
way to do it):** one memory model across all backends. WasmGC is out of scope (Appendix, "if ever").

**C · wasm32 default, uniform 64-bit `ptr<>`.** wasm32 (32-bit address space, ≤4 GB, universal
engine support) is the default; wasm64 only for a >4 GB module. `ptr<>` is **64-bit uniform** across
every backend (`ltype_size(Ptr) = 8`, `lir.tks:465`); on wasm32 the backend **narrows i64→i32 CHECKED
at every memory op and PANICS on overflow (address ≥ 2^32)** (§6) — the address model stays 64-bit in
the IR, the check enforces the 32-bit reality. wasm64 flips `Ptr → i64` with no narrowing.

---

## 3. The register-bypass — LIR VReg → wasm local (A3 skipped, engine JITs the real allocation)

### 3.1 Why no regalloc — the engine owns register allocation at load time

wasm's abstract machine has **unbounded** indexed locals and an **unbounded** operand stack; the
finite host register file is invisible to the module. The engine's baseline+optimizing JIT allocates
real registers and inserts real spills when it compiles the module for the host — *after* we ship it.
So C1 emits the pre-regalloc register-IR directly: no physical `MReg`, no callee-saved set, no spill
slots, no register prologue/epilogue, no branch-displacement patcher. This is the direct payoff of
Fork Option A: the entire A3 surface (`compute_intervals`/`linear_scan`/`rewrite_func`, the `A3-loop`
back-edge honest-stop) is simply not on the wasm path.

### 3.2 The VReg → local mapping + wasm value types (LType lowering)

The whole "allocation" is one linear pass over `LFunc`:

- **Params ARE locals `0..n_params-1`** (typed by `LFunc.param_types`). `LParam{index}` → `local.get
  index`.
- **Every other VReg that survives past its producing instruction, and every `LBlock.params` entry
  (block-arg / SSA-lite merge param)**, gets a declared local at index `n_params + k`. Build a
  `vreg_to_local: []u32` and a parallel `locals: []WasmValType` (the declared-locals vector of the
  function body). Because LIR is SSA-lite (single def per VReg, `lir.tks:7`), the map is a trivial
  dense assignment — no interference graph, no coloring.

```teko
/**
 * WasmValType — the four wasm value types a Teko machine class maps onto: i32
 * (I8/I16/I32 and, on wasm32, Ptr), i64 (I64 and, on wasm64, Ptr), f32 (F32),
 * f64 (F64). I128 and F16 have NO native wasm value type and route through the
 * named honest-stops `C1-wideint` / `C1-f16` (§10); `Void` produces no local.
 * The enum closes now so the mapping widens EXACTLY ONCE when i128-as-a-pair
 * lands (the `lir.tks:16-17` "close the enum now so N2 does not re-version it"
 * discipline, mirrored here for the wasm codomain).
 *
 * @since #389 C1-1
 */
pub type WasmValType = enum { WI32; WI64; WF32; WF64 }

/**
 * wasm_valtype_of — the wasm value type of one LIR machine class under the
 * target pointer width `ptr64` (false = wasm32, `Ptr -> WI32`; true = wasm64,
 * `Ptr -> WI64`). I8/I16/I32 -> WI32; I64 -> WI64; F32 -> WF32; F64 -> WF64.
 * I128 raises `C1-wideint` and F16 raises `C1-f16` (both NAMED honest-stops,
 * §10); `Void` is never asked (callers skip a Void-typed VReg — it holds no
 * value, so it is allocated no local).
 *
 * @param LType t  the machine class to map
 * @param bool ptr64  the target pointer width (false = wasm32)
 * @return WasmValType | error  the wasm value type, or a named honest-stop
 */
pub fn wasm_valtype_of(t: lir::LType, ptr64: bool) -> WasmValType | error { … }
```

The scalar opcode mapping (exercised from C1-2) is 1:1 and listed in the Appendix: `LBin`/`LUn` →
`i32.add`/`i64.mul`/`f64.div`/`i32.lt_s`/… (signedness is in the opcode, `lir.tks:14`, exactly like
wasm's `_s`/`_u` suffixes); `LConstInt`/`LConstFloat` → `i32.const`/`i64.const`/`f64.const`;
`LLoad`/`LStore` → `i32.load`/`i64.store8`/… by width; `LCall`/`LCallIndirect` → `call`/
`call_indirect`; `LGlobalAddr`/`LFuncAddr` → an `i32.const` of the data/table offset (see §9 for the
function-table indirection).

### 3.3 Params, block-args → locals, and the linear-memory shadow stack for LAlloca

**Block-args → locals, NOT wasm block signatures (the register-bypass's clean payoff).** Rather than
thread LIR positional block-args through wasm `block`/`loop` param+result signatures (fragile with
multiple predecessors and loop back-edge arity), every `LBlock.param` is just a declared local. On
each control edge, before the `br`, the edge's positional args emit as a **parallel-copy** of source
locals into target-param locals (`local.get src; local.set dst`), breaking cycles with a temp local —
mirroring isel's `emit_edge_moves` (§4.6). Consequence, stated once and relied on everywhere: **every
`loop`/`block`/`if` is emitted with a VOID signature and the operand stack is EMPTY across every
branch/label boundary.** The stackifier therefore never has to match wasm block value types.

**LAlloca / the shadow stack.** wasm locals are not addressable, but `LFieldAddr`/`LLoad`/`LStore`
need real addresses. So `LAlloca` reserves space on a linear-memory **shadow stack**: a module global
`$sp` (i32 on wasm32 / i64 on wasm64) bumped DOWN by `(bytes, aligned)` at function entry, restored at
every exit; the alloca's address VReg = the bumped `$sp` value (an ordinary value, subject to the
value-stack rule §5). This is the wasm analogue of A4's `A4-frame`, but only for stack SLOTS — never
for register spills (there are none). The arena and rodata stay in the same linear memory, identical
to native. **$sp reservation math must account for the eventual i128-as-a-pair widening** (a 16-byte
slot once `C1-wideint` lands); the layout table (`layout_of_sized`, `lir.tks:530`) already sizes
i128 fields at 16 bytes, so the alloca math is correct today and needs no revisit (§11.3).

---

## 4. The stackifier — reducible LIR CFG → structured `block`/`loop`/`if`/`br`/`br_if`

`src/backend/stackify.tks`. The one genuinely new pass with no analogue in the register pipeline: it
turns A1's unstructured `LBlock` CFG + block-args back into structured wasm control flow.

### 4.1 Why the CFG is reducible (no goto; the theorem) + why NOT Relooper/Binaryen

Teko's frontend emits ONLY structured `loop`/`if`/`break`/`continue` with no goto and no
computed jump, so the LIR CFG is **reducible** — confirmed structurally, not assumed:

- The complete LIR terminator set is `LRet | LJump | LBranch` (`lir.tks:67-75`) — no `br_table`, no
  indirect control terminator.
- Every loop is created only by `lower_loop` (`lower.tks:1853`), which gives each loop exactly ONE
  header block reached from outside by exactly ONE forward entry edge, so the header dominates the
  body by construction (single-entry natural loop).
- The only back-edge sources are `lower_loop`'s `close_loop_body` fall-through and `lower_continue`,
  both targeting their own loop's header; `lower_break` targets the loop's exit. The checker
  (`check_labels`, `typer.tks`) proves every break/continue names a **lexically enclosing** loop
  (#520), so no edge can jump INTO the middle of a loop or into a sibling loop — no loop can acquire a
  second entry.
- This is the same reducibility `backend-443-rpo-numbering.md:105-109` records ("retreating edge ==
  back edge" is exact *because* the CFG is reducible; Teko "cannot express an irreducible CFG today").

Because the input is reducible, C1 uses **dominator-based reconstruction** (Ramsey "Beyond Relooper",
ICFP 2022) specialized to reducible input: **zero node duplication, zero dispatch var, no
`FixIrreducibleControlFlow`, no Relooper Multiple, no block sorting.** We do NOT use Binaryen's
Relooper — it rediscovers structure we already have and can duplicate blocks / introduce a label
variable, i.e. strictly more cost and code for zero benefit on reducible input. (If a *future*
optimization pass ever flattened structured loops into a raw basic-block CFG, the same dominator
recursion still applies, duplication-free; a marker is left in §11.5, mirroring #443's future-note.)

### 4.2 The analysis — RPO layout (#443) + dominators + back-edge & merge marking

```teko
/**
 * rpo_block_order_wasm — the reverse-post-order block-id sequence of the LIR
 * function `f`, entry `#B0` first (a post-order DFS over each block's
 * terminator successors — `LJump.target`, `LBranch.t_target`/`f_target`, `LRet`
 * none — then reversed). The SAME #443 algorithm `rpo_block_order` runs over
 * `MFunc` (`regalloc.tks:173`) and `rpo_block_order_x86` over `MFuncX86`
 * (`regalloc_x86.tks:226`); this parallels it over `LFunc`. This layout makes
 * every FORWARD edge point downward (earlier -> later) and every retreating
 * edge point upward — and on Teko's reducible CFG (§4.1) "retreating == back
 * edge" is EXACT, the theorem #443 already relies on.
 *
 * @param lir::LFunc f  the reducible LIR function
 * @return []u32  the block ids in reverse post-order, `#B0` first
 */
fn rpo_block_order_wasm(f: lir::LFunc) -> []u32 { … }
```

- **Dominators** — Cooper-Harvey-Kennedy iterative over the RPO (cheap; `dominators_wasm(f, rpo) ->
  []u32` yielding the idom array).
- **Back edge** `u -> v` iff `v` precedes `u` in RPO AND `v` dominates `u`. Its target `v` is a **loop
  header** (the `#Bhead` of `lower_loop`: single forward entry + back edges only).
- **Loop-header set** = targets of any back edge. **Forward-merge / loop-exit set** = the blocks that
  need a `block` label per the labeling rule below (§4.3).

### 4.3 Scope placement — loop headers → `loop`, forward merges/exits → `block`

**★★ CORRECTNESS FIX 1 — the labeling rule (a miscompile-class point; the naive "≥2 forward preds"
rule is WRONG).** A block needs a wasm `block` label **iff it has any incoming FORWARD edge that is
NOT the RPO fall-through edge.** The *fall-through edge* into block `B` is the single edge from `B`'s
immediate RPO-predecessor when that predecessor's terminator continues into `B` with no `br` (an
`LJump` to `B` where `B` is next in RPO, or the not-taken side of an `LBranch` whose taken side is the
`br_if` and whose not-taken target is `B`). Every OTHER forward edge into `B` is a `br`/`br_if` that
must jump over intervening code, so `B` must carry a label the `br` can name. This one principle
subsumes both cases the naive rule drops: (i) an if/else / match-chain arm block reached by a
`br_if`, and (ii) a single-predecessor loop-exit / break target. The naive "≥2 forward predecessors"
rule leaves those unlabeled, so their `br` references a nonexistent label = **invalid module** — it
breaks `if c { A } else { B }` when both arms diverge, `break`-inside-`if`-inside-`loop`, and match
test-chains. Do not use predecessor count; use the fall-through test.

```teko
/**
 * needs_block_label — the labeling rule (CORRECTNESS FIX 1): true iff block
 * `b` must be wrapped in a wasm `block` so a forward `br`/`br_if` can name it.
 * `b` needs a label iff at least one FORWARD edge into `b` is NOT the RPO
 * fall-through edge — i.e. `b` is reached by some `br`/`br_if`, not only by
 * falling out of its immediate RPO predecessor. This is INDEPENDENT of the
 * predecessor COUNT (the discarded naive "≥2 forward preds" rule mislabels
 * single-predecessor loop-exit targets and if/else arm blocks). A loop-exit /
 * break target ALWAYS satisfies this (its break edge is a `br` out of the loop
 * scope, never a fall-through), so it is labeled unconditionally (FIX 1c).
 * Blocks emitted INLINE inside an `if`/`else` (§4.7) are reached only by
 * structural fall-through and are never asked here.
 *
 * @param lir::LFunc f  the function
 * @param []u32 rpo  the RPO block order (§4.2)
 * @param []u32 idom  the dominator tree (§4.2)
 * @param u32 b  the block id under test
 * @return bool  true iff `b` needs a `block` label
 */
fn needs_block_label(f: lir::LFunc, rpo: []u32, idom: []u32, b: u32) -> bool { … }
```

**Placement.** For each loop header `H`: open `loop $cont_H` immediately BEFORE `H`; its `end` closes
after the natural-loop body (FIX 2, below). For each labeled forward-target `M`: open `block $join_M`
whose `end` sits immediately BEFORE `M`, and whose START is placed at the nearest position that
dominates ALL of `M`'s forward predecessors (its idom's scope). Co-located openings are sorted so
wider scopes wrap narrower ones (a loop outside its inner joins); proper nesting is guaranteed on
reducible + RPO input (the Peterson-Kasami-Tokura structurability result), and the wider-wraps-narrower
sort is derived from, and tied to, the loop-body extent of FIX 2 (§11.2 records the argument).

**★★ CORRECTNESS FIX 2 — a loop's extent is the NATURAL-LOOP BODY, not the dominance subtree (a
miscompile-class point).** The naive "close the loop `end` after the last RPO block dominated by `H`"
is WRONG: when a loop is entered UNCONDITIONALLY, the exit block AND all post-loop code are also
dominated by `H` (the dominance extent strictly exceeds the loop-body extent), so `loop..end` would
SWALLOW the exit and everything after it — pushing the break-target block INSIDE the loop and
mis-scoping every following construct. The loop's contiguous RPO extent is instead bounded by the
maximum RPO index among **back-edge sources** (blocks that can reach a back edge to `H`), i.e. the
**natural loop body**. The `block $break` that wraps the `loop` closes just PAST that extent, so a
`break` lands correctly at the exit outside the loop.

```teko
/**
 * natural_loop_body — the natural loop of header `header` (CORRECTNESS FIX 2):
 * the set of blocks that can reach a back-edge SOURCE for `header` without
 * leaving through `header` — computed by a reverse reachability from every
 * back-edge source, bounded by `header`, plus `header` itself. This is STRICTLY
 * SMALLER than `header`'s dominance subtree when the loop is entered
 * unconditionally (the exit block + post-loop code are dominated by `header`
 * but are NOT in the natural body). The loop's `loop..end` closes after the
 * maximum RPO index in this set; the wrapping `block $break` closes just past
 * it, so the loop exit sits OUTSIDE the loop scope.
 *
 * @param lir::LFunc f  the function
 * @param []u32 rpo  the RPO block order (§4.2)
 * @param []u32 idom  the dominator tree (for the back-edge test, §4.2)
 * @param u32 header  the loop header block id
 * @return []u32  the natural-loop body block ids (a subset of `f.blocks`)
 */
fn natural_loop_body(f: lir::LFunc, rpo: []u32, idom: []u32, header: u32) -> []u32 { … }
```

### 4.4 Emission with a scope stack + relative `br` depths

Walk the blocks in RPO layout with a SCOPE STACK of frames:

```teko
/**
 * WScopeKind — the three structured wasm control scopes the stackifier opens:
 * `WLoop` (a `loop` — its label re-enters at the START, the continue/back-edge
 * target), `WBlock` (a `block` — its label exits at the END, the break /
 * forward-merge target), and `WIf` (a two-way branch whose arms emit INLINE,
 * §4.7 — it owns no jump target). The kind fixes whether a `br` to this frame
 * means "re-iterate" (loop start) or "fall past" (block/if end).
 *
 * @since #389 C1-3
 */
pub type WScopeKind = enum { WLoop; WBlock; WIf }

/**
 * WScope — one entry of the emission-time scope stack: which structured scope
 * it is, and the LIR block id it is keyed to (a loop's header for `WLoop`, the
 * merge/exit block it wraps for `WBlock`; `WIf` carries the branch block id for
 * diagnostics only). A `br` to a target block resolves to the relative DEPTH of
 * the matching frame from the stack top (§4.4).
 *
 * @since #389 C1-3
 */
pub type WScope = struct { kind: WScopeKind; block_id: u32 }
```

For each block `B` in RPO order: **(a)** pop + `end` every scope whose close position is here;
**(b)** push + emit `loop`/`block` (VOID signature, §3.3) for every scope opening here; **(c)** emit
`B`'s straight-line instructions (§5 value-stack rule); **(d)** the terminator:

- `LRet` → the return value (if any) is already the trailing value on the stack or a `local.get`;
  emit `return`. **`return` needs no label** — it unwinds all scopes.
- `LJump T` → emit `T`'s edge parallel-copy into locals (§4.6), THEN: if `T` is a back edge → `br
  $cont_T` (continue/iterate); else if `T` is the immediately-next RPO block AND no scope boundary
  intervenes → **fall through** (no instruction); else → `br $join_T` (forward).
- `LBranch cond, T-edge, F-edge` → **the `if`/`else` baseline (§4.7)** when both successors are
  dominated by `B`; otherwise the `br_if` form: push `cond` (the fused compare stays on the stack, no
  local — like isel `is_fused_compare`, `isel_arm64.tks:1229`), then `br_if $dest_T` for the taken
  edge + fall-through/`br $dest_F` for the other, each edge doing its parallel-copy immediately before
  its `br`. A `br_if` edge that must run copies is split into its own edge-block (a tiny wrapper
  `block`) exactly as isel's `resolve_edge`/`emit_edge_moves` splits a copy-carrying conditional edge
  — and that synthetic edge-block is itself a labeled scope, covered by the same labeling rule (FIX 1)
  and the same wider-wraps-narrower sort (§4.3).

```teko
/**
 * br_depth — the relative label immediate for a `br`/`br_if` to the block
 * `target_id`, given the current `stack` (top = innermost). The immediate is
 * `(stack.len - 1 - index_of_matching_frame)`, so the innermost scope is depth
 * 0. A loop-header target resolves to its `WLoop` frame (a `br` there re-enters
 * the loop START = continue); a merge / loop-exit target resolves to its
 * `WBlock` frame (a `br` there jumps PAST the block END = fall/break). Depths
 * fall straight out of the scope stack; a multi-level labeled break is just a
 * larger depth (the frame sits further down the stack).
 *
 * @param []WScope stack  the active scope stack (top last)
 * @param u32 target_id  the destination LIR block id
 * @return u32 | error  the relative `br` depth, or an internal-invariant stop
 *                      if no matching frame is on the stack (unreachable on a
 *                      correctly labeled, reducible function)
 */
fn br_depth(stack: []WScope, target_id: u32) -> u32 | error { … }
```

### 4.5 break / continue / labeled break (#520) → `br` to the right scope

Break/continue are already in the LIR as `LJump` to the loop's exit (`lower_break`, `lower.tks:1756`)
/ head (`lower_continue`, `:1776`). So the stackifier needs NO break/continue-specific code — they are
ordinary `LJump`s the terminator rule (§4.4-d) already handles: a jump to a loop header is a back edge
→ `br $cont` (continue = loop START); a jump to a loop exit targets that loop's wrapping `block`
→ `br $break` (break = block END, past the loop). Labeled break/continue (#520): the LIR already
resolved the label to the enclosing loop's blocks (`find_loop_target`, `lower.tks:1732`,
checker-verified lexically enclosing), so the target block id is unambiguous and its frame sits at a
well-defined stack depth — a **multi-level `br N`** with `N` = the number of enclosing scopes to step
out (`br_depth`, §4.4). Following the prior-art shape, an outer `A: loop` containing an inner `B: loop`
with `break A` in `B`'s body resolves to: `loop $B_cont`(0), `block $B_break`(1), `loop $A_cont`(2),
`block $A_break`(3) → `break A` = `br 3`. No arbitrary jump ever arises.

**Extensible-loop step-then-recheck (#517) needs NO special wasm handling.** The frontend already
desugars the range/3-part step to the TOP of the body behind a `_first` guard (`loop-extensible.md`
§5.2-5.3), so `continue` = `br $cont` lands right after the loop label where the guarded step + the
condition test (`br_if $break` on `!cond`) both live; for-each heads carry the `_it()` pull in the
`match` subject at the top of the body, so `continue` re-pulls (advances) for free. The single back
edge gives C-`for` `continue` semantics automatically — the "backends unchanged" property the LIR
desugar guarantees. (This is a desugar property invisible to wasm validation; §11.5 REPORTS it up so
the differential exit-code corpus keeps a `continue`-runs-the-step fixture, §12.)

### 4.6 Block-args on edges → parallel-copy into locals (not wasm merge types)

Each control edge carries positional block-args matching the target `LBlock.params`. Before the `br`
(or, for a fall-through edge, before falling into the target), emit the edge's args as a
**parallel-copy** of source-VReg locals into the target-param locals — `local.get src; local.set dst`
per position, breaking a copy cycle with one temp local. This mirrors isel's `emit_edge_moves`
(`isel_arm64.tks:1337`) verbatim in shape (over locals, not MRegs).

```teko
/**
 * emit_edge_copies — the per-edge parallel-copy of block-args into the target
 * block's parameter locals, mirroring isel's `emit_edge_moves`
 * (`isel_arm64.tks:1337`) over wasm locals: for each position `i`, copy the
 * source VReg's local into the target `LBlock.params[i]`'s local. A copy CYCLE
 * (a permutation among the param locals) is broken with one temp local, exactly
 * as `emit_edge_moves` breaks it. Because every merge routes through locals,
 * the operand stack is EMPTY at the edge and every scope has a VOID signature
 * (§3.3) — the stackifier never matches wasm block value types.
 *
 * @param lir::LFunc f  the function (for the target's params + the VReg->local map)
 * @param []u32 vreg_to_local  the VReg -> local index map (§3.2)
 * @param u32 target  the destination block id
 * @param []u32 args  the edge's positional block-args (source VRegs)
 * @return []byte  the emitted `local.get`/`local.set` copy sequence
 */
fn emit_edge_copies(f: lir::LFunc, vreg_to_local: []u32, target: u32, args: []u32) -> []byte { … }
```

### 4.7 The `if`/`else` baseline for two-way branches (LOAD-BEARING, not an optional peephole)

**★★ This section is CORRECTNESS FIX 1(b) — it is the BASELINE, not the "optional recovery" the
skeleton mislabeled.** When an `LBranch`'s two successors are both dominated by the branch block `B`
(the common case: a plain `if`/`else` diamond, an `if c { return }; rest`, a match test-chain, or both
arms diverging), emit a real `(if cond (then <T-arm inline>) (else <F-arm inline>))` — the arm blocks
emit INLINE inside the `then`/`else`, so they need **no label** and the merge that follows is reached
by structural fall-through out of the `end` (also no label). This is what makes the diamond
well-formed: without it, the naive baseline's `br_if $dest_T` names an arm block that the labeling
rule never labeled (a single-predecessor plain block that is neither a loop header nor a ≥2-pred
merge), producing an invalid module — exactly the miscompile the adversarial pass caught for cases
"both arms diverge", "break inside if inside loop", and "match with many arms". A nested match chain
emits as nested `if`/`else` with ZERO extra labels.

The `br_if` + `block` form (§4.4-d) remains the mechanism for edges the `if`/`else` cannot absorb: a
back edge (→ `br $cont`, continue), a break/loop-exit edge (→ `br $break`), and any forward merge that
is not a clean branch-dominated diamond (reached from outside the diamond) — those targets carry
labels per FIX 1. Together, `if`/`else`-as-baseline + the fall-through labeling rule cover every
branch shape the reducible LIR can produce, with the minimum number of labels.

---

## 5. The value-stack-vs-locals rule (the value stackify optimization)

Distinct from the CFG stackifier: how a *value* travels from its producer to its consumer.

**BASELINE (correct, verbose — its own slice C1-2).** Put EVERY VReg in a local: each def → `local.set`
(or `local.tee`), each use → `local.get`. Always correct; the engine JIT folds most redundant set/get
pairs away at load time. This is shipped and differentially verified FIRST, so the whole backend is
proven correct before any value-stack cleverness (M.2 — a proven baseline under the peephole).

**OPTIMIZATION (the peephole — slice C1-5).** A value `V` produced by instruction `I` may STAY ON THE
OPERAND STACK (no local; its consumer reads it directly) iff ALL hold:

1. **SINGLE-USE** — `V` has exactly one use (SSA-lite gives single-def; count uses). Multi-use → local.
2. **SAME-BLOCK** — producer and consumer are in the same `LBlock` (values never cross a branch/label
   boundary on the stack — the stack is empty at every edge, §3.3/§4.6; cross-block values are
   block-args = locals).
3. **LIFO / TREE-SHAPED** — between def and use, `V` is the top-of-stack still-pending value at the
   point of use (pending values consumed in strict reverse order of production — an expression tree).
   If an intervening produced-and-pending value would break LIFO, force `V` to a local.
4. **NO INTERVENING HAZARD** — if `V` is a load result, no store to a possibly-aliasing address and no
   call (`LCall`/`LCallIndirect`, treated as full barriers) occurs between `I` and the use; otherwise
   memory ordering forces `V` to a local before the barrier.

When all four hold: emit `I`'s operands, emit `I`, leave the result on the stack (no `local.set`); the
consumer just consumes it (`local.tee` in the rare case it must also persist). Otherwise `local.set` at
def, `local.get` at use.

**FORCED-LOCAL (never stack):** block-args / `LBlock.params`; function params (locals `0..n-1`); any
multi-use VReg; any value live across a call or an aliasing store; any value whose def/use straddle a
scope boundary. **ALWAYS-STACK special case:** an `LBranch` `cond` VReg that is single-use by the
branch and in-block feeds `br_if` directly off the stack (the fused compare — mirrors
`is_fused_compare`, `isel_arm64.tks:1229`), never materialized; likewise an `LFieldAddr` base feeding
an immediately-consumed `LLoad`/`LStore` address chain.

C1-5 is **purely additive over a proven baseline**: its gate is a byte-golden SHRINK vs the C1-2
output AND *identical exit codes* over the whole corpus (the differential is unchanged).

---

## 6. wasm32 / wasm64 — the 64-bit `ptr<>` model + i64→i32 CHECKED narrowing (panic on ≥ 2^32)

`ptr<>` is **64-bit uniform** in the LIR (`ltype_size(Ptr) = 8`, `lir.tks:465`) — a single memory
model across every backend. On **wasm32** (the default), a wasm linear-memory access takes an i32
address, so at EVERY memory op the backend narrows the 64-bit address value to i32 **CHECKED**: emit
the address as i64, test `addr >= 0x1_0000_0000` (2^32), and on overflow **PANIC** via the panic-hook
trap (§8) rather than silently wrapping (`i32.wrap_i64` truncates — never emitted un-guarded for an
address). This keeps the IR's 64-bit address model honest while enforcing the 32-bit reality (M.3 —
fail loud, do not silently wrap). On **wasm64** (`memory64`, selected only for a >4 GB module) `Ptr →
i64` and the narrowing check is elided (the address is already 64-bit). The switch is a single
`ptr64: bool` threaded from the manifest/target triple through `wasm_valtype_of` (§3.2) and the memory
lowering; every memory-op site consults it. Non-address i64 values (ordinary `i64` arithmetic) are
never narrowed — only address operands feeding `*.load`/`*.store`.

The checked narrow is a small, named lowering step (`C1-7`), fixture-driven both ways: an overflow
fixture panics via the hook, a large-pointer fixture runs on wasm64. This is the one place the uniform
64-bit `ptr<>` meets the wasm32 address width, and it is a CHECK, not a truncation.

---

## 7. Runtime modes — WASI ABI interop vs Teko-native capability imports (the firewall)

A Teko wasm module runs in one of two modes, decided by the loading contract — NOT by two different
compilers (the same `emit_wasm_module` emits both; the mode is which import namespace the module
declares + which host wires it). Both are ratified 2026-07-12. **Consumption-side design:** the Teko-native capability-import mode is **Model 1** (#530) and the WASI-interop mode is **Model 2** (#535); see those issues for runtime capability semantics and permission boundaries.

**(interop) Standard WASI ABI.** When the module is consumed by another language / a standard host,
it speaks the WASI preview1 ABI: it exports `_start` (or the named function exports) and imports the
WASI functions it uses from `wasi_snapshot_preview1` (`fd_write`, `proc_exit`, …). `tk_exit` → the
`proc_exit` import; the runtime surface (`teko_rt`) is provided by linking `teko_rt.wasm` (§9, §11.4).
This is the mode the C1-8 differential runs under wasmtime.

**(Teko-native) Capability imports = the firewall.** When loaded by a Teko host, the module's IMPORTS
ARE its capabilities, wired by the loading contract: an fs capability, an io capability, a clock
capability, each a host-supplied import function. **No import = no capability** — a module that was
never handed an fs import literally cannot touch a filesystem, because the wasm import is the only way
in and the host chose not to provide it. This is the firewall: capability is granted by wiring, not by
ambient authority, and it cannot be forged from inside the sandbox. The **browser** variant (N6b) is
this mode with JS-import glue: `env`/`io`/`exit` are wired by the JS host; `fs`/`process` have NO
browser equivalent, so those imports are simply not wired and the corresponding builtins **honest-stop
at runtime** (a browser sandbox has no VFS — none is invented, M.3). The WASI mode is the same firewall
with WASI as the standard capability set.

---

## 8. The panic-hook trap (export side) — host import event then trap (M.1 fail-loud)

A Teko panic in a wasm guest must surface a STRUCTURED panic to the host and then stop — it cannot
recover (M.1). So `tk_panic_str`/`tk_panic` lower to: **(1)** call a host `panic` IMPORT (an event) —
`panic(code, msg_ptr, msg_len)` — that lets the host observe + report the structured panic (message,
line:col from the `.tsym`, exit intent); **(2)** then execute `unreachable`, which **traps** the wasm
instance. Observe + report, then trap — never recover. This mirrors native's `tk_panic` (which prints
a backtrace then aborts) within wasm's model: the host hook is the wasm-legal way to get the structured
panic OUT before the trap makes the instance unusable (M.3 — the panic is not swallowed by a bare
`unreachable`). In WASI mode the `panic` import can map to a `fd_write` of the message + `proc_exit`;
in the Teko-native/browser mode it is a host-wired capability (a JS `onPanic` in the browser). The
import is declared unconditionally (it is a fail-loud requirement, not an optional capability); a host
that declines to wire it gets the default trap with no structured surface (still fail-loud, just
un-annotated).

---

## 9. The module writer — `objfile_wasm.tks` (type/import/func/table/memory/global/export/code sections)

A single self-contained wasm module (`\0asm` magic `0x6D736100` + version `1`, emitted via
`emit_u32_le` twice), then the sections in canonical order. wasm uses **LEB128** for all counts,
indices, and immediates (unlike the fixed-width native encoders), so C1 adds `emit_uleb`/`emit_sleb`
(new — the one encoding primitive that is not reused from the native path).

| section | id | contents |
|---|---|---|
| Type | 1 | the distinct function signatures (params+results as `WasmValType` vectors) `func`/`import` reference by index |
| Import | 2 | the runtime + capability imports (§7) + the `panic` hook (§8): `(module, name, kind)` triples |
| Function | 3 | per defined function, its Type-section index |
| Table | 4 | one `funcref` table for `call_indirect` (the `LCallIndirect`/vtable path, `lir.tks:104`) |
| Memory | 5 | one linear memory (min pages = rodata + shadow-stack + arena reserve; the arena grows it via `memory.grow`) |
| Global | 6 | `$sp` (the mutable shadow-stack pointer, §3.3) + any `LGlobal` |
| Export | 7 | `_start`/named exports (§7) + `memory` |
| Elem | 9 | the `funcref` table initializer (function addresses for indirect calls) |
| Code | 10 | per function: the LEB128 body size + the local-run RLE (`WasmFunc.locals`) + the stackifier's instruction bytes + the implicit `end` |
| Data | 11 | the rodata image (`LModule.rodata`, interned string/const bytes) at its linear-memory offset |

```teko
/**
 * WasmFunc — one function lowered to a wasm Code-section entry: its declared
 * locals vector (the VReg->local allocation BEYOND the params, §3.2 — RLE'd
 * into local-runs by the writer) and its structured instruction bytes (the
 * stackifier's output, §4, terminated by the implicit `end`). The Type/Function
 * sections index it by position; the Code section prefixes its LEB128 body size.
 *
 * @since #389 C1-1
 */
pub type WasmFunc = struct { locals: []WasmValType; code: []byte }

/**
 * emit_wasm_module — assemble a complete wasm binary from an `LModule`: the
 * magic + version header, then the Type/Import/Function/Table/Memory/Global/
 * Export/Elem/Code/Data sections in canonical order (§9). Maps every VReg to a
 * local (§3.2), stackifies every function's CFG (`stackify_func`, §4), interns
 * the rodata into the Data section, reserves the shadow-stack + arena in linear
 * memory, and declares the runtime/capability imports (§7) + the `panic` hook
 * (§8). Emits a SELF-CONTAINED module that IMPORTS its runtime — the WASI path
 * links `teko_rt.wasm` via `wasm-ld` (§11.4), the browser path binds it via JS
 * glue (§7). Any function's named honest-stop (`C1-wideint`/`C1-f16`)
 * propagates out unchanged, so an unsupported fixture stops IDENTICALLY on the
 * own-wasm side and the differential compares at the stop (the A4/§10 precedent).
 *
 * @param lir::LModule m  the lowered program
 * @param bool ptr64  the target pointer width (false = wasm32)
 * @return []byte | error  the wasm module bytes, or a named honest-stop
 */
pub fn emit_wasm_module(m: lir::LModule, ptr64: bool) -> []byte | error { … }
```

Testability: golden byte assertions on the fixed header prefix (magic/version) and a tiny
one-function module's section layout, plus a **host-tool well-formedness check** in the gate —
`wasm-tools validate` (or `wasmtime` load) accepts the module, and `wasmtime`/`node` runs it to the
expected exit code. The encoder slices (C1-1..C1-5) can byte-golden with no engine; the engine leg
lands at C1-8.

---

## 10. Honest-stops — which slice each lives behind (0.3.1 completion vs later)

| stop | slice it lives behind | what it defers | closed by |
|---|---|---|---|
| `C1-wideint` | C1-1 (`wasm_valtype_of`) | i128 value type (as an i64 pair, or memory-backed) — the enum closes now, so the map widens once (§11.3) | 0.3.1 (Appendix) |
| `C1-f16` | C1-1 (`wasm_valtype_of`) | f16 value type (no native wasm type) | 0.3.1 (Appendix) |
| `C1-fp` | C1-2 | FP-op parity check against the wasm FP opcodes (regular; Appendix table) — folded into C1-2 as it is 1:1 | 0.3.1 if any gap found |
| `C1-simd` | later cluster | `v128` / SIMD lowering | later (not on the LTS path) |
| `C1-bigmem` | C1-7 | wasm32 address ≥ 2^32 → PANIC (the checked narrow, §6); a genuine >4 GB module needs the wasm64 switch | 0.3.1 (wasm64 arm) |
| `C1-wasm64-scope` | C1-7 | the wasm64 arm's own SUPPORTED subset today: no `LAlloca` (the shadow-stack `$sp`/`LFieldAddr`/`LAlloca` sites still compute in `i32` unconditionally), no rodata (`LGlobalAddr` likewise), no `Ptr`-typed signature — `wasm_assemble_program`'s own scope guard fails loud (M.3) rather than emit an invalid `memory64` module; a wasm64 program calling `print`/`panic` also fails cleanly (`wasm_resolve_call`'s existing "unresolved call symbol" error) since the `fd_write`-based io/panic trampolines — WASI preview1's own `i32`-address-only ABI, a genuine ecosystem wall against `memory64` — are omitted for `ptr64` | a follow-up threads `ptr64` through `LFieldAddr`/`LAlloca`/`LGlobalAddr`/`$sp` |
| browser `fs`/`process` | C1-6 | no VFS/process in a browser sandbox — the import is not wired, the builtin honest-stops at runtime (§7) | never (no equivalent; M.3) |
| WasmGC | — | GC value types — arena-in-linear-memory is THE model (§2-B) | out of scope (Appendix, "if ever") |
| own wasm linker | — | none needed — the module is self-contained; WASI links `teko_rt.wasm` via `wasm-ld` (§11.4) | Phase E is ELF/Mach-O/COFF only |
| same LIR subset as A1 | — (inherited) | C1 widens NO lowering; a construct A1 honest-stops, C1 never sees | A1's own slices (#382) |

Each stop is a NAMED error the pipeline surfaces; a fixture reaching one stops identically on the
own-wasm side and is compared at the stop, never at a fabricated value (M.3, the A4 §6 precedent).

---

## 11. Deferrals + recorded decisions (law-first — NO HALT)

### 11.1 No A2 wasm-isel — lower LIR ops directly to wasm opcodes (resolved)
The `LOp` → wasm-opcode map is 1:1 and register-free (§3.2, Appendix), so a separate wasm "isel" IR
would be dead structure. **Law-first (M.1 + M.4):** `stackify_func` emits opcodes inline as it walks;
no `minst_wasm` parallel IR (unlike B1's `minst_x86`, which pays for a target WITH registers — wasm
has none, so the cost buys nothing). Recorded, not open.

### 11.2 Block-args as locals, not block signatures (resolved)
Threading LIR positional block-args through wasm `block`/`loop` param+result signatures is fragile
with multiple predecessors and loop back-edge arity. **Law-first (M.1/M.2):** every block-arg is a
local; each edge is a parallel-copy (§4.6); every scope is VOID-signature with an empty operand stack
at every boundary. This is the register-bypass's clean payoff and it makes the stackifier's
correctness argument local (no cross-edge stack shape to prove). It also grounds the FIX-2-tied
wider-wraps-narrower sort (§4.3): because the stack is empty at every boundary, co-located scope
openings can be ordered purely by their close positions (a loop's natural-body extent strictly
contains its inner joins' extents), with no operand-stack constraint to satisfy. Recorded, not open.

### 11.3 i128 / f16 representation (named: `C1-wideint` / `C1-f16`)
wasm has no i128 or f16 value type. **Law-first (M.3):** `wasm_valtype_of` raises a NAMED honest-stop
for each; a fixture reaching one stops identically own-side and native-side. Not exercised by the
`exit(n)` corpus (which is i32/i64/f64). The `WasmValType` enum closes now, so the map widens EXACTLY
once when i128-as-a-pair lands; the `$sp` shadow-stack math (§3.3) already sizes i128 slots at 16 bytes
via `layout_of_sized` (`lir.tks:530`), so no alloca revisit is needed. Recorded, not open.

### 11.4 `wasm-ld` as the cc-as-linker analogue (resolved)
Native links the C-built `teko_rt.o` via `cc` (D-2 option 1). The wasm analogue: the WASI path links
the own module against `teko_rt.wasm` (the C runtime compiled to `wasm32-wasi` by clang) via
`wasm-ld`; the browser path binds the runtime via JS-import glue (§7). **Law-first (M.4):** `wasm-ld`
is a visible, honest bootstrap linker (bundled with the LLVM toolchain), superseded by nothing — a
single-module emit that IMPORTS all its capabilities needs no external linker at all, and Phase E's
own-linker is ELF/Mach-O/COFF only (a wasm module IS its container). Recorded, not open.

### 11.5 Extensible-loop continue-runs-the-step is a DESUGAR property (REPORTED up, not resolved here)
The #517 range/3-part `continue`-runs-the-step-then-rechecks semantics live entirely in the
parser desugar (the `_first`-guarded step at the body TOP, `loop-extensible.md` §5.2-5.3), which is
INVISIBLE to wasm validation — a `br $cont` that lands at the loop head runs the step for free, but
only because the desugar put the step there. A wasm-valid module can therefore be SEMANTICALLY wrong if
the desugar regresses. **This is not a C1 decision** — it is a property of `lower.tks`/the parser that
C1 relies on. REPORTED up so the differential exit-code corpus keeps a `continue`-runs-the-step fixture
(§12) as the only thing that would catch a desugar regression. The future-irreducibility marker (§4.1)
is likewise recorded here: if a later optimization pass flattens structured loops into a raw
basic-block CFG, the same dominator recursion still applies duplication-free — no relooper is ever
needed on reducible input.

### 11.6 No genuine unresolved tension
Every deferral above is a scope boundary resolved law-first with a named follow-up. The keystone bar
(§12.4) is fully provable on the acyclic + reducible, non-i128, integer/control, `exit(n)`-terminated
subset A1's interp already runs. **C1 does not HALT.**

---

## 12. Regression fixtures + the gate

### 12.1 Golden wasm-vector unit tests (`src/backend/stackify_test.tkt`, `objfile_wasm_test.tkt`) — engine-agnostic

- **Value-type + local map** (C1-1): `wasm_valtype_of(I32,false) = WI32`; `I64 = WI64`; `Ptr,false =
  WI32`; `Ptr,true = WI64`; `I128 → C1-wideint`; `F16 → C1-f16`; a 2-param function's `vreg_to_local`
  places params at 0..1 and the first value at 2.
- **Module skeleton** (C1-1): the `\0asm`+version header bytes; a one-function `exit(42)` module's
  section-id order (Type=1, Import=2, Function=3, …, Code=10); the `proc_exit` import triple.
- **Stackifier core** (C1-3): a plain `if c { A } else { B }` → `(if (then …)(else …))` with NO arm
  labels + a fall-through merge; `if c { return }; rest` → the same, `rest` unlabeled; a 3-arm match
  → nested `if`/`else`, zero extra labels; a forward merge reached from OUTSIDE a diamond → a labeled
  `block` with the `br` depth asserted. **The labeling-rule fixtures (FIX 1) are the load-bearing
  ones** — assert that an if/else's arm blocks and merge carry NO label, and that a merge reached by a
  non-fall-through `br` DOES.
- **Loops** (C1-4): a counter `loop` → `block $break { loop $cont { br_if $break; …; br $cont } }`
  with the `end`s closing after the **natural-loop body** (FIX 2) — assert the exit block is OUTSIDE
  the `loop` scope; a nested labeled `break OUTER`/`continue OUTER` → the multi-level `br N` depth
  asserted; a loop with a single break (single-predecessor exit) → the exit is still labeled (FIX 1c).
- **Edge copies** (C1-3/4): a merge with a block-arg → the `local.get src; local.set dst` copy before
  the `br`; a 2-way copy cycle → the temp-local break (mirrors `emit_edge_moves`).
- **Value-stack peephole** (C1-5): a single-use in-block LIFO value → left on the stack (no
  `local.set`); a multi-use value → `local.set`+`local.get`; a load across a call → forced local.

### 12.2 End-to-end differential fixtures (`examples/regressions/`, driven by the wasm leg)

| fixture | program (shape) | expected exit | VM | C-native | own-wasm |
|---|---|---|---|---|---|
| `wasm_exit_zero` | `exit(0)` | 0 | ✓ | ✓ | **new** |
| `wasm_exit_code` | `exit(42)` | 42 | ✓ | ✓ | **new** |
| `wasm_arith_exit` | `exit(6 * 7)` | 42 | ✓ | ✓ | **new** |
| `wasm_if_exit` | `if 5 > 3 { exit(1) } else { exit(2) }` | 1 | ✓ | ✓ | **new** |
| `wasm_if_both_diverge` | `if k > 0 { exit(1) } else { exit(2) }` (no merge) | (per k) | ✓ | ✓ | **new** (FIX 1) |
| `wasm_match_exit` | `match k { 0 => exit(7); _ => exit(9) }` | (per k) | ✓ | ✓ | **new** (FIX 1) |
| `wasm_loop_count` | `loop mut i in 0..n { s += i }; exit(s)` | (per n) | ✓ | ✓ | **new** (FIX 2) |
| `wasm_break_in_if` | `loop { if done { break }; … }; exit(r)` | (per r) | ✓ | ✓ | **new** (FIX 1c) |
| `wasm_labeled_break` | `OUTER: loop { loop { break OUTER } }; exit(r)` | (per r) | ✓ | ✓ | **new** (#520) |
| `wasm_continue_step` | `loop mut i in 0..n { if odd(i) { continue }; s += i }; exit(s)` | (per n) | ✓ | ✓ | **new** (§11.5 — proves continue runs the step) |
| `wasm_panic_hook` | `x ?? panic("boom")` on a null | (trap) | ✓ | ✓ | **new** (§8 — host hook observes the structured panic, THEN traps) |

All exit-code (no stdout) at first; a `print`-then-`exit` fixture is added once `LCall` to the string
runtime rides the WASI `fd_write` import (C1-6). The `wasm_if_both_diverge` / `wasm_match_exit` /
`wasm_break_in_if` / `wasm_loop_count` fixtures are the **direct regression tests for the two
correctness fixes** — an implementer who reverts to "≥2 forward preds" (FIX 1) or the dominance-subtree
loop extent (FIX 2) fails to produce a valid module for them.

### 12.3 Ritual posture — RIGHT-SIZED (CI is the gate; dono ruling 2026-07-10)
- **Every C1-N** owes the full ritual on the primary lane: the **both-engine gate** (native `teko . -o
  bin` AND `teko test .` VM), **paranoid**, **fixpoint**, and **100% coverage on its new code**
  (definition-of-done). The stackifier is branchy (per-block, per-terminator, per-scope) — cover every
  labeling-rule arm, every scope-kind, every honest-stop arm via golden tests; a genuinely unreachable
  arm is justified in the PR. The wasm differential leg (§12.2) is a wasmtime/node lane, honest-skipped
  where no wasm engine is present (a named reason, mirroring A4's macOS-only diff skip).
- **VM-gotcha watch** (dense byte-work): (a) build buffers via `teko::list::push(buf, (x & 0xFF) to
  byte)` — never widen a `byte` mid-expression (byte-width anchoring); (b) LEB128 in `u32`/`u64`
  arithmetic, narrow to `byte` only at the last step; (c) no `x = match {…return}` — use `let x =
  match {…}` then act (the isel/regalloc VM gotcha); (d) the scope stack + `br_depth` math in `u32`.
- **Fixpoint is trivially preserved** through C1-1..C1-7 (new files, no reachable call from the default
  path) and through C1-8 (the wasm path is behind the `TEKO_BACKEND=wasm` env seam; default stays C).

### 12.4 Ritual points (where the FULL gate must pass)
- After **C1-3** (the stackifier core, the FIX-1 labeling rule) and **C1-4** (loops, the FIX-2
  natural-loop extent): full both-engine gate + golden goldens green (these are the riskiest CFG-shape
  bits — the two correctness fixes live here).
- The **KEYSTONE full ritual at C1-8**: the whole gate — both engines + fixpoint + the **new own-wasm
  == C-native (== interp) differential leg green under wasmtime** — must pass. **#389 CLOSES here.**

---

## 13. The sub-slice decomposition (ordered, each gate-able)

```
A1 (done) ─▶ C1-1 ─▶ C1-2 ─▶ C1-3 ─▶ C1-4 ─▶ C1-5 ─▶ C1-6 ─▶ C1-7 ─▶ C1-8   [#389 closes]
             valtype   all-locals  stackify   loops    value-   runtime   wasm32    emit_native
             + local   straight-   core       nat-loop  stack    modes +   i64→i32   + differential
             + module   line       (FIX 1)   (FIX 2)   peephole  trap      narrow    (wasmtime)
```

- **C1-1 · value-type model + VReg→local map + `objfile_wasm` skeleton** — `src/backend/objfile_wasm.tks`
  (magic/version + Type/Function/Export/Code sections, `emit_uleb`/`emit_sleb`), `src/backend/stackify.tks`
  (`WasmValType`, `wasm_valtype_of`, the VReg→local map only). Emit a trivial `exit(n)` function (params
  as locals, one block, a const + the `proc_exit` import). **Proven by:** golden module bytes decode
  under `wasm-tools validate` + `wasmtime` runs `exit(n)`. **Reuses unchanged:** `lir.tks`, `LModule`,
  `emit_u32_le`.
- **C1-2 · straight-line lowering + memory ops, ALL-LOCALS baseline** — `LConstInt`/`LConstFloat`,
  `LBin`/`LUn` (arith+bitwise+shift+compare → wasm opcodes, Appendix), `LCall`/`LCallIndirect`,
  `LParam`, `LLoad`/`LStore`/`LFieldAddr`/`LGlobalAddr`/`LFuncAddr`, `LAlloca` via the shadow-stack
  global `$sp`. No value-stack opt (every VReg a local). **Proven by:** the A1 single-block corpus
  exit codes == C-native == interp. **Reuses:** A1 lowering output verbatim.
- **C1-3 · the STACKIFIER core (FIX 1)** — `rpo_block_order_wasm` (#443 over `LFunc`), `dominators_wasm`,
  back-edge/label marking (`needs_block_label` — the fall-through labeling rule), scope placement,
  the scope-stack emission + `br_depth`, `LJump`/`LBranch` → `br`/`br_if`, **`if`/`else` as the
  baseline** for branch-dominated diamonds, block-args → local parallel-copy (`emit_edge_copies`),
  copy-carrying conditional edges split into edge-blocks. **Proven by:** the if/match forward-diamond
  corpus (incl. both-arms-diverge) exit codes match + the labeling-rule golden asserts. **Reuses:** the
  #443 RPO algorithm, the isel `emit_edge_moves` shape.
- **C1-4 · LOOPS (FIX 2)** — loop headers → `loop $cont`, back edges → `br` continue, `#Bexit` → the
  wrapping `block $break` closing after the **natural-loop body** (`natural_loop_body`),
  break/continue/labeled-break (#520) → `br` to the correct scope at its computed depth. **Proven by:**
  the loop corpus — counters, while/range/3-part/for-each desugars, nested + labeled break/continue,
  the continue-runs-the-step fixture — exit codes match + the exit-outside-loop golden assert.
  **Reuses:** `lower_loop`'s head/exit/block-arg structure.
- **C1-5 · the value-stack OPTIMIZATION** — the single-use + same-block + LIFO + no-barrier peephole
  drops `local.set/get` pairs (`local.tee` where persist); the fused compare → `br_if` off the stack.
  **Proven by:** a byte-golden SHRINK vs the C1-2 baseline AND identical exit codes over the full
  corpus (differential unchanged). Purely additive over a proven baseline.
- **C1-6 · runtime modes + trap** — the WASI ABI export path (interop, `proc_exit`/`fd_write` imports)
  AND the Teko-native capability-import firewall (no import = no capability); the `panic` host IMPORT
  surfaces the structured panic then `unreachable`/traps (M.1). **Proven by:** a `wasmtime` WASI run;
  a host-hook fixture observes the structured panic then traps (observe+report, not recover).
- **C1-7 · wasm32 i64→i32 CHECKED narrowing** — the checked narrow at every memory op (PANIC on
  address ≥ 2^32, via the §8 hook) + the wasm64 switch for >4 GB modules; uniform 64-bit `ptr<>`.
  **Proven by:** an overflow fixture panics via the hook; a large-pointer fixture runs on wasm64.
- **C1-8 · `emit_native` TARGET-DISPATCH arm + the DIFFERENTIAL (KEYSTONE)** — the `TEKO_BACKEND=wasm`
  seam + the `wasm32-wasi` target triple in `src/build/project.tks` (an additive, FIXPOINT-preserving
  target arm), the `wasm-ld` link of `teko_rt.wasm` (§11.4), the wasm leg of `diff_vm_native.sh` wiring
  **own-wasm(wasmtime) == C-native == interp** over the corpus (honest-skipped where no wasm engine is
  present). **Proven by:** the whole gate — both engines + fixpoint + the new differential leg — green.
  **#389 CLOSES.**

**Files:** new `src/backend/stackify.tks`, `src/backend/objfile_wasm.tks`,
`src/backend/stackify_test.tkt`, `src/backend/objfile_wasm_test.tkt`, a wasm variant of
`src/runtime/teko_rt` (WASI/browser build) + `extensions/`/`web` JS glue (N6b), the wasm leg of
`scripts/diff_vm_native.sh`, `examples/regressions/wasm_*`; touched `src/build/project.tks`
(`emit_native` wasm arm + the `TEKO_BACKEND=wasm` env seam). **Reuses unchanged:**
`lir::lower_program`, the whole `lir` type surface, `encode_arm64::emit_u32_le` (pub), `align_up`, the
#443 RPO algorithm, the isel `emit_edge_moves` parallel-copy shape.

---

## Appendix · deferred fast-follows (i128 pair-lowering, f16, SIMD, WasmGC-if-ever) + the scalar opcode map

**The scalar `LOp` → wasm opcode map (C1-2, 1:1 — the ground truth for the straight-line lowering).**
Signedness is in the opcode (`lir.tks:14`), matching wasm's `_s`/`_u` suffixes. `<w>` = `i32` for
I8/I16/I32/Ptr@wasm32, `i64` for I64/Ptr@wasm64; `<f>` = `f32`/`f64`.

| `LBinOp` | wasm | | `LBinOp` | wasm |
|---|---|---|---|---|
| `IAdd`/`ISub`/`IMul` | `<w>.add`/`.sub`/`.mul` | | `ICmpEq`/`ICmpNe` | `<w>.eq`/`.ne` |
| `IDivS`/`IDivU` | `<w>.div_s`/`.div_u` | | `ICmpLtS`/`ICmpLtU` | `<w>.lt_s`/`.lt_u` |
| `IRemS`/`IRemU` | `<w>.rem_s`/`.rem_u` | | `ICmpLeS`/`ICmpLeU` | `<w>.le_s`/`.le_u` |
| `IAnd`/`IOr`/`IXor` | `<w>.and`/`.or`/`.xor` | | `ICmpGtS`/`ICmpGtU` | `<w>.gt_s`/`.gt_u` |
| `IShl`/`IShrS`/`IShrU` | `<w>.shl`/`.shr_s`/`.shr_u` | | `ICmpGeS`/`ICmpGeU` | `<w>.ge_s`/`.ge_u` |
| `FAdd`/`FSub`/`FMul`/`FDiv` | `<f>.add`/`.sub`/`.mul`/`.div` | | `FCmpEq`..`FCmpGe` | `<f>.eq`/`.ne`/`.lt`/`.le`/`.gt`/`.ge` |

`LUnOp`: `INeg` → `<w>.sub` from 0 (or `i64.sub`); `INot` → `<w>.xor` with -1; `FNeg` → `<f>.neg`;
`ZExt`/`SExt` → `i64.extend_i32_u`/`_s`; `Trunc` → `i32.wrap_i64`; `IToF`/`FToI` →
`<f>.convert_<w>_s`/`_u` and `<w>.trunc_<f>_s`/`_u`; `FToF` → `f64.promote_f32`/`f32.demote_f64`.
`LConstInt` → `<w>.const`; `LConstFloat` → `<f>.const` (the IEEE bits, `lir.tks:49`); `LLoad`/`LStore`
→ `<w>.load`/`.store` (+ `8`/`16`/`32`+`_s`/`_u` by width). This table is regular and 1:1, which is
why C1 needs no wasm-isel IR (§11.1).

**Deferred fast-follows.** `C1-wideint` (i128 as an i64 pair — two locals per i128 value, the arith
expanded to carry-chains, memory as two i64 slots) and `C1-f16` (f16 as an i32-carried bit pattern
+ software convert) widen `WasmValType`/`wasm_valtype_of` once (§11.3). `C1-simd` (`v128`) is a later
cluster, off the LTS path. WasmGC is out of scope — the arena-in-linear-memory model (§2-B) is THE
memory model; a WasmGC re-target would fork it and is revisited only "if ever". Each fast-follow is
mechanical: the enum is closed, the map widens at one site, and a fixture that reaches the stop today
compares at the stop on both sides.

---

## 14. C1-8 sub-decomposition (the keystone destrincha) — grounded on the SHIPPED lane

> **Scope of this section.** A sub-slicing of the last crumb, produced **after** C1-1..C1-7 landed
> and merged (`fix/issue-389-wasm`, head `c0cad13`). It is grounded on the CODE now on the lane, not on
> the pre-implementation §13 sketch — and that grounding materially **shrinks and re-shapes** C1-8. The
> single most important finding: **the emit_native wasm dispatch arm, the whole `wasm_assemble_program
> → emit_wasm → write .wasm` pipeline, the self-contained WASI module, and the own-wasm==C-native
> differential harness (`scripts/validate_wasm_own.sh`) ALL already landed as part of C1-5/C1-6/C1-7**
> (the harness's own header reads "issue #389 C1-5/C1-6/C1-7"; `emit_native_wasm`'s doc reads "#389
> C1-5, reworked … the wasm64 switch at C1-7"). C1-8 therefore adds **NO compiler code** — it is a
> corpus-broadening + CI-gating + fixpoint-confirming + doc-reconciling crumb that turns the already-
> working artifact into a ratified, gated keystone and CLOSES #389.
>
> **SUPERSEDED (2026-07-14, owner "fix fully, any size"):** This pre-implementation finding was
> falsified during grounding on execution. Closing #389 honestly required **SIX real own-backend
> compiler fixes** (see §14.0.1). The overall structure stands; the details are reconciled below.

### 14.0.1 Reconciliation — what C1-8 actually cost

The pre-implementation claim "C1-8 adds NO compiler code" was falsified during grounding on execution.
Closing #389 honestly required **SIX real own-backend compiler fixes**, each separately proven
(own==C differential + wasm-under-wasmtime + fixpoint):
- **F1** (#561) — LIR if/match merge-scalar-drop: a `mut` scalar reassigned in an arm was dropped at
  the merge. `src/lir/lower.tks`.
- **F1c** (#562) — `!` (logical-not) lowered to bitwise `INot` (nonzero for both bool values) → every
  `if !cond` mis-branched on both own backends; now `ICmpEq(operand,0)`. The range-`for` desugar was
  the first own-backend code to use `!`. `src/lir/lower.tks`.
- **F2** (#563) — own-native regalloc §6.2 loop-liveness extension across all three allocators
  (arm64/x86/riscv): removed the loop honest-stop, widen header-live intervals to the latch.
  own-native now compiles loops. `src/backend/regalloc*.tks`.
- **F1b** (#564) — defer-write-propagation (statement-arm scalar sampled post-`replay_defers`) +
  value-if/match RHS reassignment threading. `src/lir/lower.tks`.
- **item 3** (#566) — scope-aware `lenv` (reassign-in-place + index-identity), replacing F1's coarse
  across-all-arms shadow-exclusion; residual NONE; fat-local reassign falls back to append
  (honest-stop preserved). `src/lir/lower.tks`.

**Misdiagnosis note:** The "stackifier gap" hypothesis for the loop failure was a MISDIAGNOSIS (the
stackifier was correct; the root was the `!`-lowering bug, F1c).

**Now green:** The loop fixtures (`wasm_loop_count`→6, `wasm_continue_step`→6) now pass under wasmtime,
and the own==C native differential now compiles loops on all three native backends.

### 14.0 Reality reconciliation — three §-supersedes (law-first, recorded, NO HALT)

The §13 sketch predates both the implementation and two ratified laws it now collides with. Each
tension resolves law-first to the newer/ratified fact; none is a genuine unresolved tension, so none
HALTs.

- **S-1 · The seam is `TEKO_BACKEND=native` + `TEKO_TARGET=wasm32-wasi`, NOT a new `TEKO_BACKEND=wasm`
  (supersedes §0/§13's "temporary `TEKO_BACKEND=wasm` env seam").** The shipped path reuses the
  EXISTING own-backend seam (`native_backend_selected`, `project.tks:719`, `TEKO_BACKEND=="native"`)
  plus the `TEKO_TARGET` triple resolver (`target_from_name`, `project.tks:763` — `wasm32-wasi`/`wasm`/
  `wasi` → `NativeTarget::Wasm32Wasi`, `wasm64-wasi` → `Wasm64Wasi`, `wasm32-browser` → `Wasm32Browser`)
  and a new arm in `emit_native`'s target `match` (`project.tks:870-872` →
  `emit_native_wasm(…, browser, ptr64)`). **Law-first (M.5 — one way to do it):** the own backend
  already HAD a target-dispatch seam; a second parallel `TEKO_BACKEND=wasm` env var would be a
  redundant selector for a state (`which own target`) `TEKO_TARGET` already names. Reusing it is
  strictly better for FIXPOINT (S-4/§14.Q4 — zero new default-path branches). D1/#390's real
  `--backend`/`--target` flag supersedes BOTH env seams later.
- **S-2 · There is NO `wasm-ld`, NO relocatable object, and NO `teko_rt.c` wasm port (supersedes §11.4
  and the §13 "wasm variant of `src/runtime/teko_rt`" file line).** `objfile_wasm.tks` emits a FINAL,
  self-contained module whose ENTIRE import section is the two WASI preview1 functions `proc_exit` +
  `fd_write` (`wasm_wasi_import_set`, `objfile_wasm.tks:482+`). Everything the corpus calls is
  **synthesized as wasm functions inside the module**: `tk_exit` resolves DIRECTLY to the `proc_exit`
  import (`stackify.tks:4655`); `tk_write`/`tk_ewrite`/`tk_println`/`tk_panic_str` are synthesized
  fd_write trampolines over a fixed linear-memory scratch region (`wasm_io_iov_base_addr`/
  `wasm_io_nwritten_addr`/`wasm_io_scratch_bytes`, `stackify.tks:4463-4526`); `_start` is synthesized
  (`stackify.tks:4710+`); the arena/rodata live in the module's own linear memory
  (`wasm_program_memory_plan`, arena sizing deferred to #453's `#arena_size`). **Law-first (M.4 — the
  cheapest honest path):** a single-module emit that imports all its capabilities needs no external
  linker AT ALL; §11.4's "`wasm-ld` links `teko_rt.wasm`" was the cc-as-linker analogue for a
  relocatable-object shape that the ratified §1.2 self-contained-module decision never produced. §11.4
  itself already flagged this ("superseded by no external linker being needed"). The §8 host `panic`
  IMPORT is likewise superseded by the synthesized `tk_panic_str` fd_write-then-`unreachable`
  trampoline (still fail-loud: observe via fd_write to the host, then trap).
- **S-3 · The keystone differential is own-wasm(wasmtime) == C-native ONLY — there is no `interp`/VM
  leg (supersedes §12/§12.4's "== interp" and the §12.3 "both-engine gate", and the §13 "wasm leg of
  `scripts/diff_vm_native.sh`").** The VM was **retired (issue #524, `docs/design/vm-retirement.md`)**;
  `scripts/diff_vm_native.sh` no longer exists on the lane (only `native_regressions.sh`'s CWD check
  survived it), and the test gate runs **natively only** (#265, `run_gate_native`, `project.tks:1355`).
  **Law-first:** the later, ratified #524/#265 win over the pre-#524 §12 sketch. C-native is the single
  trusted oracle, and the three-way "own-wasm == C-native == interp" collapses to the two-way
  "own-wasm == C-native"; transitivity through `diff_c_own.sh` (C-native == own-**native**, register
  targets) still yields the full own-wasm == C-native == own-native chain over the shared `own_*`
  fixtures, so nothing is lost. The keystone harness is `scripts/validate_wasm_own.sh` (already on the
  lane), not a wasm leg of a retired script.

### 14.Q · The four de-risking questions, resolved against the shipped lane

- **Q1 — the teko_rt→wasm port (was flagged "highest risk"): RESOLVED — there is NO port; it is off
  the critical path entirely.** `teko_rt.c`/`.h` are never compiled to, linked into, or imported by a
  wasm module. Its host dependencies (`mmap`/`sbrk` arena, `write`/`read`, `exit`, `clock`, libc,
  threads) are all IRRELEVANT to the wasm path because the wasm backend **re-implements the exact
  subset the corpus needs as synthesized wasm**, not as ported C: arena/rodata = the module's own
  linear memory (a fixed `mem_min_pages` reserve today; `memory.grow` + real `#arena_size` sizing is
  #453, NOT a C1-8 blocker for the exit/print corpus); `exit` = `tk_exit`→`proc_exit` import; `write`
  = the `fd_write` trampolines; `clock`/`read`/threads = never lowered, so structurally unreachable
  (the §7 capability firewall: no import ⇒ no capability). The **smallest working subset** for the
  exit-code + stdout corpus is therefore exactly {`proc_exit`, one `fd_write` trampoline family, the
  linear-memory scratch+arena reserve, the synthesized `_start`} — and it is **already built and
  differentially green** (C1-6/C1-7). **Load-bearing (must keep working):** those synthesized
  trampolines + the linear-memory layout. **Scoped honest-stops (already NAMED, M.3):** `C1-globals`
  (top-level `LGlobal` data, `stackify.tks:4459`), `C1-i128`/`C1-f16`/`C1-simd`, `C1-wasm64-scope`
  (the alloca-free/rodata-free/`Ptr`-signature-free wasm64 subset), and browser `fs`/`process`. None
  of these is exercised by the C1-8 keystone corpus.
- **Q2 — the wasm-ld link (§11.4): RESOLVED — `wasm-ld` is NOT in the path at all.** `objfile_wasm`
  emits a FINAL module (imports + synthesized `_start`), never a relocatable object with a linking
  section; the C1-1..C1-7 emit was always a single self-contained module, and C1-8 keeps it that way
  (S-2). There is no object-file+linker step to reconcile — the emitted bytes ARE the runnable
  artifact. wasm-ld/wasi-libc availability is therefore **NOT** a C1-8 dependency (this removes the
  §13-implied wasi-libc-in-CI blocker entirely).
- **Q3 — the differential corpus: the keystone uses `validate_wasm_own.sh`'s OWN corpus, a
  purpose-built superset of the shared `own_*` fixtures, NOT `diff_c_own.sh`'s verbatim.** Today
  `validate_wasm_own`'s `CORPUS` = the six shared exit/arith/control fixtures `own_exit_zero`/
  `own_exit_code`/`own_arith_exit`/`own_sub_exit`/`own_if_exit`/`own_match_exit` (the SAME dirs
  `diff_c_own` compares C-vs-own-native, giving the transitive chain) **plus** wasm-specific proof
  fixtures `wasm_print_exit` (a string LITERAL, dodging the `own_print_exit` interpolation N2 gap that
  is a KNOWN_STOP in `diff_c_own`), `wasm_panic_hook` (`trap`-kind), `wasm_defer_arm_scope`
  (`print`-kind stdout), and `wasm64_arith_exit` (`wasm64-wasi` target). It deliberately does NOT
  reuse `diff_c_own`'s corpus verbatim (it swaps the print fixture and adds trap/defer/wasm64). **The
  gap C1-8b closes:** the §12.2 FIX-1/FIX-2 *engine-level* loop/control fixtures (`wasm_if_both_diverge`,
  `wasm_loop_count`, `wasm_break_in_if`, `wasm_labeled_break`, `wasm_continue_step`) are proven today
  only at the GOLDEN byte level (`stackify_test.tkt`) and at C-vs-own-**native** level (`diff_c_own`
  over the `loop_*` dirs) — NOT executing under a real wasm engine. The keystone must prove FIX-1/FIX-2
  run correctly under wasmtime == C-native, so C1-8b adds those five as `wasm_*` exit-code fixtures to
  `validate_wasm_own`'s corpus.
- **Q4 — FIXPOINT preservation: the default (C) path is byte-identical because the wasm arm is
  UNREACHABLE without the env seam.** `emit_native` is called ONLY inside the
  `if native_backend_selected()` gate (`project.tks:686`, `native_backend_selected` = `TEKO_BACKEND
  == "native"`, `:719`); the wasm arm is nested one level deeper, inside `emit_native`'s
  `match native_target()` and only for `TEKO_TARGET ∈ {wasm32-wasi, wasm64-wasi, wasm32-browser}`
  (`:865-872`). With NO env set (the compiler compiling itself, and every default user build),
  `native_backend_selected()` is `false`, `emit_native` is never entered, and not one byte of the C
  codegen path changes — so **gen1==gen2 holds by construction** (the fixpoint gate in
  `release.yml`/`sanitizers.yml` is untouched). This is the S-1 dividend: because the seam is the
  pre-existing `TEKO_BACKEND=native` + `TEKO_TARGET` and NOT a new env var, C1-8 adds **zero** new
  branches to the default path.

### 14.1 The ordered sub-crumb sequence (each independently gate-able)

```
C1-7 (done) ─▶ C1-8a ─▶ C1-8b ─▶ C1-8c ─▶ C1-8d   [#389 CLOSES]
               doc/     engine    CI wasm   fixpoint +
               decision loop/ctl  gate +    full-gate
               record   fixtures  wasmtime  keystone
               (this §) (+corpus) provision ritual + close
```

- **C1-8a · doc + decision record (design-only; this §14).** Ratify S-1/S-2/S-3 in the doc so the
  code and the spec agree before the gate lands. **Files touched:** `docs/design/backend-wasm.md`
  (this section). **Seams/signatures added:** none (design-only). **Regression fixtures:** none.
  **Ritual point:** doc review on the design PR; no gate. **Serial, FIRST** (it is the map the rest
  follow; it also honestly records that C1-8 ships no compiler code).

- **C1-8b · the FIX-1/FIX-2 engine-level differential fixtures (+ corpus wiring).** Add the five
  §12.2 control-flow fixtures as `examples/regressions/wasm_*` projects, each a tiny exit-code program
  whose value the stackifier's FIX-1 (labeling) / FIX-2 (natural-loop extent) must reconstruct
  correctly to produce a valid module, then append them (with `KIND=exit`, `TARGET=wasm32-wasi`) to
  `validate_wasm_own.sh`'s parallel `CORPUS`/`KIND`/`TARGET` arrays. **Files touched:**
  `examples/regressions/wasm_if_both_diverge/`, `.../wasm_loop_count/`, `.../wasm_break_in_if/`,
  `.../wasm_labeled_break/`, `.../wasm_continue_step/` (each `<name>.tkp` + `src/main.tks`);
  `scripts/validate_wasm_own.sh` (the three arrays only — no logic change). **Seams/signatures
  added:** none in Teko (product code already lowers these shapes; these are fixtures). The fixture
  program shapes (copy verbatim; each is a whole `src/main.tks` — a trailing loose `exit(n)` IS the
  program's exit code):

```teko
/**
 * wasm_if_both_diverge — FIX-1(b) proof: an `if`/`else` whose BOTH arms diverge
 * (each `exit`s) has no merge block; the stackifier must emit inline `then`/`else`
 * arms with NO arm label (a naive "≥2 forward preds" rule mislabels the arm blocks
 * and produces an invalid module). Exit code is the taken arm's constant, compared
 * own-wasm(wasmtime) == C-native.
 *
 * @return never  the process exits from whichever arm runs (k is a compile-time 1)
 */
let k = 1
if k > 0 { exit(1) } else { exit(2) }
```

```teko
/**
 * wasm_loop_count — FIX-2 proof: a counting `loop` entered UNCONDITIONALLY. The
 * loop's `end` must close after the NATURAL-LOOP BODY, not the (larger) dominance
 * subtree; otherwise the exit block is swallowed inside the `loop` and the module
 * is mis-scoped. `sum(0..4) = 6`, compared own-wasm(wasmtime) == C-native.
 *
 * @return never  exits with the accumulated sum
 */
mut s = 0
loop mut i in 0..4 { s = s + i }
exit(s)
```

```teko
/**
 * wasm_break_in_if — FIX-1(c) proof: a `break` nested inside an `if` inside a
 * `loop`. The single-predecessor loop-exit target must be labeled UNCONDITIONALLY
 * (its break edge is a `br` out of the loop scope, never a fall-through), so the
 * `br $break` names a live label. Exits with the counter value at break.
 *
 * @return never  exits with the loop counter captured at the break
 */
mut r = 0
loop {
    if r >= 3 { break }
    r = r + 1
}
exit(r)
```

```teko
/**
 * wasm_labeled_break — #520 proof: a labeled `break OUTER` from an inner loop
 * resolves to a MULTI-LEVEL `br N` at the correct scope depth (outer loop's
 * wrapping `block`). Exits with the value set before the labeled break.
 *
 * @return never  exits with the sentinel set just before `break OUTER`
 */
mut r = 0
OUTER: loop {
    loop {
        r = 7
        break OUTER
    }
}
exit(r)
```

```teko
/**
 * wasm_continue_step — §11.5 proof: the extensible-loop `continue` must RUN THE
 * STEP (the desugar places the guarded step at the body top); a `br $cont` that
 * lands at the loop head advances the counter for free. Skips odd i, sums evens
 * of 0..6 → 0+2+4 = 6. A desugar regression (step not run on continue) would
 * infinite-loop or mis-sum — the ONLY thing that catches it (backends are
 * invisible to this property).
 *
 * @return never  exits with the sum of the even counters
 */
mut s = 0
loop mut i in 0..6 {
    if i % 2 == 1 { continue }
    s = s + i
}
exit(s)
```

  **Regression fixtures → expected exit (own-wasm(wasmtime) == C-native, both legs):**
  `wasm_if_both_diverge`→`1`; `wasm_loop_count`→`6`; `wasm_break_in_if`→`3`; `wasm_labeled_break`→`7`;
  `wasm_continue_step`→`6`. Each also passes `wasm-validate` (structural, honest-skipped when WABT
  absent). **Ritual point:** `scripts/validate_wasm_own.sh` green LOCALLY over the broadened corpus
  with wasmtime present — this is the gate that proves the two correctness fixes execute on a real
  engine. **Serial after C1-8a** (touches the corpus arrays the next crumb gates in CI).

- **C1-8c · the CI wasm differential leg (THE KEYSTONE RITUAL POINT).** Add a job/step to
  `.github/workflows/native.yml` (the Linux x86-64 `gen1 checks` lane, beside the existing
  `C-vs-own differential` step at `:301`) that **provisions wasmtime** (pinned official tarball) **and
  WABT** (`wabt` via apt, for `wasm-validate`), builds gen1 (the PR compiler, which carries
  `emit_native_wasm`), and runs `./scripts/validate_wasm_own.sh`. The harness already HONEST-SKIPS
  (exit 0, named reason) per-tool when a tool is absent (`validate_wasm_own.sh:55-69`), and already
  keys on `emit_native_wasm`'s exclusive `"(own backend)"` marker + the on-disk `.wasm` (its F1(a)
  blind-spot guard), so the CI step is a thin provision-then-invoke — mirroring `diff_c_own`'s
  host-gated invocation and A4's macOS-only differential skip. **Files touched:**
  `.github/workflows/native.yml` (one install step + one run step; optionally the same in
  `sanitizers.yml`). **Seams/signatures added:** none. **Regression fixtures:** the whole
  `validate_wasm_own` corpus (from C1-8b) now runs in CI. **Ritual point:** THIS IS THE KEYSTONE — the
  wasm differential leg green in CI. **Serial after C1-8b**, and **blocked on the wasmtime/WABT
  provisioning decision** (§14.2).

- **C1-8d · fixpoint + full-gate keystone ritual + close #389 (reached-pending-close).** Confirm the whole ritual on the lane:
  the native test gate (#265, `run_gate_native`) green, `diff_c_own.sh` green (C-native == own-native,
  register targets), the new `validate_wasm_own` wasm leg green (own-wasm == C-native), and
  **fixpoint gen1==gen2 byte-identical** (Q4 — the wasm arm is unreachable on the default path, so
  this is preserved by construction; re-run the `release.yml`/`sanitizers.yml` fixpoint gate to prove
  it). Verify **100% coverage on C1-8's new code** — which is fixtures + CI YAML + this doc, carrying
  NO new Teko product lines, so the obligation is discharged by the already-covered `emit_native_wasm`/
  `wasm_assemble_program` landed under C1-5/6/7 (call this out in the PR). **Files touched:** none (a
  verification crumb; a CHANGELOG/issue-close note only). **Ritual point:** the FULL gate — native
  gate + both differentials + fixpoint — green; **#389 CLOSES here.** **Serial, LAST.**

### 14.2 Named blocker + design-ahead

- **BLOCKER B-1 · wasmtime (and WABT) availability in CI.** Today NO workflow provisions wasmtime or
  `wasm-validate` (audited: `native.yml`/`sanitizers.yml`/`release.yml` install only cc/clang/zig/
  qemu/riscv-gcc). Wired as-is, C1-8c's leg would HONEST-SKIP (exit 0) forever and the keystone would
  close #389 on a *vacuously* green leg — not a real gate. **What unblocks it:** add a wasmtime install
  step (the official release tarball is deterministic and offline-cacheable, the same pattern
  `native.yml`'s zig step already uses; WABT via `apt-get install -y wabt`). This is a genuine external
  dependency but **resolvable, not a HALT** — it is the load-bearing decision of C1-8c. **Design-ahead
  (everything that does NOT need B-1 is deliverable now):** C1-8a (this doc), C1-8b (the fixtures +
  corpus arrays — they gate on a *local* wasmtime, which the implementer has), and the *authoring* of
  C1-8c's YAML against `validate_wasm_own.sh`'s already-frozen interface are all buildable today; only
  C1-8c's *green CI run* waits on the provisioning step actually fetching wasmtime. **Fallback if CI
  cannot fetch wasmtime** (proxy/network): run the wasm leg in the release lane or a dedicated runner
  that has it, and keep the honest-skip with its named reason in the default lane so the gate is
  transparently deferred, never silently faked (M.3).

### 14.3 Dispatch order + parallelism

- **Order:** `C1-8a → C1-8b → C1-8c → C1-8d`, essentially **serial** — b feeds c feeds d, and a is the
  map they follow.
- **Parallelism:** the only safe overlap is authoring b and c at once (disjoint files:
  `examples/regressions/wasm_*` + `scripts/validate_wasm_own.sh` arrays for b, `.github/workflows/*`
  for c), since c's YAML can be written against `validate_wasm_own`'s frozen interface before b's
  fixtures are green — but c cannot be MERGED/validated until b's corpus passes locally, so the
  effective merge order stays b-before-c.
- **No serial-core contention:** unusually for a keystone, **C1-8 touches NO `.tks` compiler code** (the
  emitter, the dispatch arm, and the harness all landed in C1-5/6/7). There is no core-file serial
  constraint at all — the entire crumb is fixtures + one CI step + doc + a verification pass. This is
  the direct, honest consequence of the §14.0 finding, and it is why C1-8 is the lowest-risk crumb of
  the whole C1 lane despite being the one that closes #389.
