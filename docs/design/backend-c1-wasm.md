# C1 / N6a+N6b — WebAssembly (WASI + Browser): the stackifier + linear-memory RT (crumb plan)

**Status:** DESIGN (doc-only). Sub-PR of the 0.3 own-AOT-backend wave. Issue **#389** (closes the
Wasm stub **#224**). Branch `fix/issue-389-wasm` (base = the `remodel/backend-build` umbrella carrying
A1–A4 + B1–B3). Base for the design: `docs/design/own-backend-architecture.md` §3.3/§4 (Phase C),
`docs/design/backend-a4-encoder.md` (the object-writer + differential keystone this MIRRORS),
`docs/design/backend-b1-x8664.md` §1 (the REUSE-vs-PARALLEL discipline), grounded in the merged code
(`src/lir/lir.tks`, `src/lir/lower.tks`, `src/build/project.tks`, `src/backend/objfile_*.tks`,
`scripts/diff_c_own.sh`, `src/runtime/teko_rt.{c,h}`).

> This is a PLAN. WebAssembly is **the highest step of the own-backend staircase**: it is a
> **stack machine with no registers**, so the shared neutral register allocator
> (`regalloc.tks` linear-scan / candidate-pool) **does not apply** — C1 swaps an ENTIRE pipeline
> stage. It is the fire-test that the architecture generalizes past register machines. C1 consumes the
> **target-independent LIR** (A1/#382) DIRECTLY, replaces `isel + regalloc` with a **stackifier**
> (`src/backend/stackify.tks`), maps the LIR CFG to **structured** wasm control flow, emits a
> self-describing **module** (`src/backend/objfile_wasm.tks`, `emit_wasm`, paralleling
> `emit_macho`/`emit_elf`/`emit_coff`), maps the arena/`teko_rt` onto a single **linear memory**, and
> runs the `exit(n)`/`print` corpus under **WASI**. Because Wasm is a whole module (not a relocatable
> `.o` linked by `cc`), the "link" is different (a Wasm module linker `wasm-ld`, or `zig cc
> -target wasm32-wasi` as its driver). The differential is **execution equivalence** (own-wasm exit /
> stdout == C-path wasm), NOT byte-identity — the codegen differs by construction, exactly like the
> riscv64/qemu lane. The hard tail (SIMD/threads/multi-value/reference-types/GC/exceptions, i128, the
> full-teko_rt browser port, the own Wasm linker) is scoped forward as **named** honest-stops.

---

## 0. TL;DR — the recommended shape

- **The pipeline fork (the load-bearing structural fact).** C1 depends on **A1 only** (the LIR); it
  does **not** touch A2/A3. The register targets run `LIR → isel_<isa> → regalloc → encode_<isa> →
  objfile_<fmt> → cc-link`. Wasm runs `LIR → stackify → emit_wasm → wasm-ld`. The **stackifier
  subsumes both isel and regalloc**: Wasm's "registers" are its **locals** (unlimited, typed), so
  register *allocation* is trivial (one local per LIR VReg) and there is no register *pressure* to
  scan — the whole `regalloc.tks` stage evaporates. C1 runs **in parallel with Phase B** (it shares no
  per-ISA code with B; it shares only the LIR upstream and the object-writer + differential *pattern*).

- **Sub-slice order (each independently gate-able, smallest-safe-step):**
  `C1-1` module model + LEB128 + `emit_wasm` section framing (empty-`main` module, golden bytes +
  `wasm-tools validate`) →
  `C1-2` the stackifier — VReg→local model + **straight-line** LIR→wasm lowering (const/bin/un/param/
  call/ret/load/store/alloca/fieldaddr/globaladdr; naive `local.get`/compute/`local.set`) →
  `C1-3` **structured control flow** — the CFGStackify region-tree pass (`block`/`loop`/`if` + `br`/
  `br_if`/`br_table` by label depth; block-args → edge local-copies; the `C1-irreducible` guard) →
  `C1-4` linear memory + rodata/data section + the WASI runtime **import** set (map `teko_rt`) →
  `C1-5` relocatable-object linking metadata + `emit_native_wasm` wiring + `wasm-ld` link (the first
  END-TO-END module; `NativeTarget::Wasm32Wasi` + `--target` routing) →
  `C1-6` the **differential lane** — own-wasm == C-wasm execution equivalence under `wasmtime`
  (**N6a done**) →
  `C1-7` the **Browser JS-import variant** — the same emitted module with JS-provided imports + the
  `extensions/web` glue (**N6b done; #389 CLOSES here**).

- **Stackifier model (risk #1).** MVP = **one wasm local per LIR VReg**, plus one per block-param —
  the always-correct "ExplicitLocals" baseline (LLVM's term). Every instruction is `local.get a;
  local.get b; <op>; local.set r`; the operand stack is transiently balanced to **zero between
  instructions**. Block-args cross edges as **local copies** (`local.set param, local.get arg` before
  the `br`), never as branch-operands — this sidesteps **multi-value** entirely. The RegStackify
  *optimization* (threading single-use, same-block values through the operand stack to elide
  `local.set`/`local.get` pairs) is a **named honest-stop** `C1-regstackify-opt` (0.3.1), NOT MVP.

- **Structured-control decision (risk #2 — the biggest; §3).** Confirmed law-first: the "only loop"
  law (no `goto`; only `loop` + `if`/`break`/`continue`) makes **every A1-lowered CFG reducible and
  single-entry** — verified against `lower_loop`/`lower_if`/`lower_match` (each builds
  single-header natural loops / diamonds / guard-chains). **Decision: reconstruct the region tree with
  a scoped CFGStackify pass** (RPO + dominators + natural-loop forest → `block`/`loop`/`if` scope
  placement + `br` label-depth), the *disciplined realization of "trust the structure"* — it does NOT
  fragile-ly trust block-creation order, and it does NOT build the relooper's irreducible-CFG
  node-splitting machinery (which "only loop" makes dead code). An irreducible SCC (unreachable from
  valid Teko) → the **named honest-stop `C1-irreducible`**, not a silent miscompile.

- **Runtime + link decision (risk #4).** **WASI**, mirroring the C path. Bootstrap = **link the
  clang/wasi-sdk-built C `teko_rt` object + wasi-libc via `wasm-ld`** (the exact analog of A4's D-2
  option-1 `cc`-as-linker bootstrap); the own backend emits a **relocatable** wasm object. Own Wasm
  linker + lowering `teko_rt.tks` through the own backend is Phase E territory — **toolchain
  independence is only fully achieved there, not at #389** (recorded, M.3).

- **Differential (risk #5).** own-wasm vs **C-path wasm** (`zig cc -target wasm32-wasi` on the C twin),
  both run under **`wasmtime`**; **byte-identity NOT expected** → assert **exit code + stdout** equal
  (the riscv/qemu precedent). Oracles: `wasm-tools validate` (or `wasm-validate`) for well-formedness,
  `wasm2wat` for debugging, `wasmtime` to execute. A `have_tool` gate honest-skips the lane with a
  named reason when the oracles are absent.

- **No genuine HALT.** Every fork resolves law-first and is recorded (§11). Two items are flagged for
  an **owner ruling but do not block the design** (§11, D-W1 relocatable-object-vs-self-contained
  bootstrap; D-W2 N6b scope depth); one adjacent finding is **REPORTED up**, not turned into an issue
  (§11, R-1: the `teko::io::println` corpus needs `fd_write`, a wider runtime surface than
  `exit(n)`-only — sequencing note).

---

## 1. The assessed starting point — what C1 consumes, REUSES, and PARALLELS

C1 consumes `lir::lower_program(prog) -> LModule | error` (`src/lir/lower.tks`) DIRECTLY — the same
entry `emit_native_arm64`/`_x86`/`_riscv`/`_win` call before handing to isel. The `LModule`
(`lir.tks:155`) is `{ funcs: []LFunc; rodata: []LRodata; globals: []LGlobal; layouts: []LStructLayout }`.
Each `LFunc` (`lir.tks:141`) is `{ symbol; n_params; param_types: []LType; ret_type: LType; blocks:
[]LBlock; next_vreg }`; each `LBlock` (`lir.tks:136`) is `{ id; params: []u32; insts: []LInst }`; each
`LInst` (`lir.tks:131`) is `{ result; has_result; op: LOp; line; col }`. The op alphabet `LOp`
(`lir.tks:126`) is the closed 16-case variant `LConstInt | LConstFloat | LBin | LUn | LCall | LParam |
LRet | LJump | LBranch | LAlloca | LFieldAddr | LLoad | LStore | LGlobalAddr | LCallIndirect |
LFuncAddr`. **This is the stackifier's whole input alphabet** — a total per-case match over 16 cases,
the same "no case may be missed" discipline the register encoders owe over `MInst`.

### 1.1 The machine-class map — `LType` → wasm value type

`LType` (`lir.tks:18`) is the sole type surface. The wasm value-type mapping (wasm32):

| `LType` | wasm valtype | note |
|---|---|---|
| `I8`, `I16`, `I32` | `i32` (0x7F) | sub-32 ints live in an `i32`; width matters only at load/store + at signed compare/shift (masking/sign-extend) |
| `I64` | `i64` (0x7E) | |
| `I128` | — | **named honest-stop `C1-i128`** — no native wasm i128; isel-for-registers already never emits i128 arith (rides the inherited `A2` i128 route), so this is reached only by a genuine i128 program |
| `F16` | — | **named honest-stop `C1-f16`** — no wasm f16 (matches the register targets' `f16` gap) |
| `F32` | `f32` (0x7D) | |
| `F64` | `f64` (0x7C) | |
| `Ptr` | `i32` (0x7F) | wasm32 linear-memory address — every address-shaped Teko type (str/slice/Ref/ptr/Named/Optional/Variant/Func) already collapsed to `Ptr` at `ltype_of` (`lower.tks`), so fat-pointers are two `i32` locals, exactly as they are two registers |
| `Void` | (no valtype) | a valueless instruction leaves the operand stack empty |

`wasm_valtype(t: LType) -> u8 | error` is the one seam; the two honest-stops (`C1-i128`, `C1-f16`)
surface here.

### 1.2 REUSED verbatim (target-neutral — no copy, no change)

- **`lir::lower_program`** — the whole front half (TAST → LIR), unchanged. C1 adds no lowering.
- **The `LModule`/`LFunc`/`LBlock`/`LInst`/`LOp` type surface** — read-only input.
- **The object-writer PATTERN** — `objfile_wasm.tks` parallels `objfile_macho.tks`/`objfile_elf.tks`/
  `objfile_coff.tks` (a per-format module writer producing `[]byte`); the LEB128 byte idiom parallels
  A4's `emit_u32_le` little-endian `teko::list::push(buf, (x & 0xFF) to byte)`.
- **The differential PATTERN** — `scripts/diff_c_own.sh` gains a wasm lane exactly as B1/B2/B3 added
  the linux/riscv/windows lanes (own build behind the `TEKO_BACKEND=native` seam + `TEKO_TARGET`,
  the `"(own backend)"` success marker, an oracle-availability honest-skip).
- **The `examples/regressions/own_*` corpus** — the SAME six fixtures the register lanes diff
  (`own_exit_zero`, `own_exit_code`, `own_arith_exit`, `own_sub_exit`, `own_if_exit`, `own_match_exit`)
  plus `own_print_exit`, re-run under wasmtime. No new corpus programs for the MVP.
- **The target-dispatch SEAM** — `NativeTarget` (`project.tks:704`) + `target_from_name`
  (`project.tks:719`) + `emit_native`'s `match` (`project.tks:786`) + the `TEKO_BACKEND`/`TEKO_TARGET`
  env seams (D1/#390's real `--target` supersedes them).

### 1.3 PARALLELED (wasm-specific — never reuses, never generalizes)

- **`src/backend/stackify.tks`** — REPLACES `isel_<isa>.tks` + `regalloc.tks` + `abi_<abi>.tks` +
  `encode_<isa>.tks` in one file: LIR → a structured wasm function (`WasmFunc`). Owns risks #1 + #2.
- **`src/backend/objfile_wasm.tks`** — REPLACES the `objfile_<fmt>.tks` family: LEB128 + section
  framing + `emit_wasm`. Owns risk #3.
- **The runtime binding** — the WASI import set + (N6b) the browser JS glue. Owns risk #4.
- **The link tail** — `finish_wasm_module` + `link_wasm` (wasm-ld) REPLACE `finish_native_object` +
  `link_object` (cc). Owns the "link is different" fact.

### 1.4 The naming convention (silent-collision-safe)

Per the fase-3 gotcha (a bare name colliding with a builtin gives NO diagnostic), every C1 type/fn is
homed under the `teko::backend` namespace and prefixed `Wasm`/`wasm_`/`stackify_`/`emit_wasm_` so no
name shadows a `minst`/`encode`/`objfile` twin. `WasmModule` ≠ `EncodedModule`, `WasmFunc` ≠
`EncodedFunc`, `emit_wasm` ≠ `emit_macho`.

---

## 2. The pipeline swap — where wasm forks, and why regalloc evaporates

```
register targets (A2–B3):  LIR ─isel_<isa>─▶ MInst ─regalloc─▶ MInst' ─encode_<isa>─▶ bytes ─objfile─▶ .o ─cc─▶ exe
wasm (C1):                 LIR ─────────────── stackify ───────────────▶ WasmFunc ─emit_wasm─▶ .wasm.o ─wasm-ld─▶ .wasm
                                (region-structuring + local-alloc + instruction lowering, ALL in stackify)
```

**Why the neutral regalloc does not apply (the fire-test).** Linear-scan register allocation exists to
map an unbounded set of virtual registers onto a *bounded* physical register file under an ABI, spilling
to the stack under pressure. Wasm has **no register file and no pressure**: locals are unbounded and
typed. So "allocation" degenerates to a **total function** `VReg → local index` (plus one local per
block-param), and there is nothing to scan, no spill slots, no callee-saved set, no ABI register
partition. The A3/B* machinery (`compute_intervals`, `candidate_pool`, `all_physical`, the frame
layout, AAPCS64/SysV/Win64/LP64D descriptors) is **entirely absent** from the wasm path. This is the
concrete sense in which C1 proves the architecture generalizes past register machines: the LIR contract
(SSA-lite, block-args, closed opcodes, `Ptr`-collapsed types) is enough to drive a stack machine with a
strictly *simpler* backend than the register targets — the complexity moves from `regalloc` into the
**control-flow structuring** (§3), which the register targets get for free from arbitrary jumps.

The stackifier is therefore three concerns in one pass-group inside `stackify.tks`:
1. **Region structuring** (§3, risk #2) — CFG → `block`/`loop`/`if` scope tree + `br` label depths.
2. **Local allocation** (§4, risk #1) — `VReg → local index`, `block-param → local index`, typed decl.
3. **Instruction lowering** (§4) — `LOp` → wasm opcodes, `local.get`/compute/`local.set`.

---

## 3. Risk #2 — STRUCTURED CONTROL FLOW (the biggest risk; deepest treatment)

Wasm has **no arbitrary jumps**. Control is `block`/`loop`/`if`…`end` scopes with `br`/`br_if`/
`br_table N` that branch to the `N`-th enclosing labelled scope (a `br` out of a `block` jumps to its
`end`; a `br` to a `loop` jumps to its *start* — the back-edge). The LIR CFG is `LJump{target,args}` /
`LBranch{cond, t_target,t_args, f_target,f_args}` / `LRet` terminators over `LBlock`s — arbitrary edges.
Mapping arbitrary edges to structured scopes is the classic Wasm-backend problem (LLVM's
`WebAssemblyCFGStackify`; Emscripten's Relooper; Ramsey's "Beyond Relooper" / Stackifier).

### 3.1 The reducibility proof — grounded in the "only loop" law

The frontend has **no `goto`**: the only control constructs are `loop` (+ `break`/`continue`) and
`if`/`match` (the "only loop" law + "no match-on-bool"). Grounding this in the A1 lowering:

- **`lower_loop`** (`lower.tks:1853`) builds a **single-header natural loop**: one `headb` (a merge
  block whose block-params are the loop-carried env scalars — the loop phis), a single entry edge
  `jump headb(entry_args)` from before the loop, a single back-edge from `close_loop_body` to `headb`
  (the `continue` target), and one `exitb` (the `break` target). One entry, one latch region → the loop
  is **reducible**.
- **`lower_if_value`/`lower_if_stmt`** (`lower.tks:1959`/`:1942`) build a **diamond**: `branch cond →
  thenb / elseb`, each `jump mergeb(args)`. Single-entry, single-merge → reducible.
- **`lower_match_*`** (`lower.tks:2763`+) build a **guard/arm chain**: each arm is a guard block
  branching to its body or the next guard, bodies `jump` the merge. A forward-only cascade → reducible.
- **`lower_bool_and`** (`lower.tks`) builds the same diamond for `&&` short-circuit.
- **`break`/`continue`** (`lower.tks:1756`/`:1776`) emit `jump exitb`/`jump headb` — structured edges
  to the enclosing loop's exit/head, never arbitrary.

There is **no frontend construct that can emit an irreducible (multi-entry) loop** — the LIR CFG is
always reducible and single-entry. (This is a *stronger* guarantee than LLVM's general backend needs,
which is why C1 does NOT need the irreducible-CFG fixup those relooper/Stackify implementations carry.)

### 3.2 The decision — a scoped CFGStackify (reconstruct the region tree), NOT a blind trust of order

The issue frames two options: **(A)** trust the structure + reconstruct the region tree, or **(B)**
implement a relooper/Stackify. **The recommended answer is a hybrid that is the disciplined form of
(A):** reconstruct the region tree with a **scoped CFGStackify pass** — the LLVM CFGStackify placement
algorithm SPECIALIZED to reducible CFGs (so the node-splitting / irreducible-fixup half is replaced by
an honest-stop). Rationale, law-first:

- **Do NOT blind-trust block-creation order (rejects naive-A).** A1 appends blocks in AST-recursion
  order, which *happens* to be close to a structured order, but nothing in the LIR contract PROMISES it
  (a future lowering refactor could reorder), and relying on it is an unchecked invariant (M.2/M.3
  tension). The pass instead DERIVES structure from the CFG edges, which ARE the contract.
- **Do NOT build the relooper's irreducible machinery (rejects full-B).** Node-splitting / label
  variables exist to handle irreducible CFGs; §3.1 proves those cannot occur, so that code would be
  permanently dead (M.1: smallest orthogonal; a backend must not carry unreachable complexity).
- **The middle path (chosen):** compute the region tree from `{RPO order, dominator tree, natural-loop
  forest}` over the reducible CFG, place `block`/`loop`/`if` scopes, and lower each edge to a `br` at
  the computed label depth. This is *exactly* "trust the structure" — the structure (reducibility) is
  what lets the region reconstruction be a simple, total, one-pass placement instead of a general
  relooper — while remaining robust to block ordering (M.2) and honest about the one impossible case.

### 3.2.1 Prior-art validation (a 6-toolchain survey, folded in at C1-3)

Before C1-3 landed, the integrator surveyed Zig, LLVM, Emscripten/Relooper, Go, Ramsey's "Beyond
Relooper" (ICFP'22), and the WASI-vs-Browser split — confirming the CFGStackify bet above, and
explaining *why* it is the right one for THIS codebase specifically:

- **The decisive analog: Zig (own backend, source is already goto-free).** Zig's
  `src/codegen/wasm/CodeGen.zig` has **no relooper, no CFGStackify, no region-tree rebuild** — its IR
  (AIR) arrives at the backend **already structured** (`block`/`loop`/`cond_br`/`br`/`repeat`), because
  ZIR→Sema preserve the source's own goto-free structure end to end. The backend is a recursive walk
  with one `block_depth` counter; branch resolution is `br (block_depth - recorded_depth)` arithmetic.
  Irreducibility literally cannot arise, so Zig carries no tripwire for it at all.
- **Why C1 cannot take Zig's shortcut.** Zig skips CFGStackify because its IR stays structured
  end-to-end. **The LIR is FLAT on purpose** (`LFunc.blocks` + `LJump`/`LBranch`, SSA-lite) — shared by
  the 4 register targets, whose isel + linear-scan regalloc need a linearized CFG. This puts C1 in
  LLVM's situation (flat IR → `WebAssemblyCFGStackify` + `FixIrreducibleControlFlow`), not Zig's.
  Ramsey's dominator-based structuring (ICFP'22) is the correct, clean algorithm for a flat *reducible*
  CFG — exactly the scoped CFGStackify this section specifies.
- **The refinement considered and REJECTED: a Zig-like lowering side-table.** `lower_loop`/
  `lower_if_*`/`lower_match_*` already know their own loop headers/diamonds at lowering time; the
  backend could just trust a side-table instead of recomputing dominators. Rejected: (1) it would touch
  the SHARED `lower.tks`, risking byte-identity on the 4 shipped native targets plus W15/coverage churn;
  (2) it creates a desync hazard — the side-table would need to survive any future block-level transform
  between lowering and this backend; (3) it dilutes the C1 architectural proof ("the LIR alone drives the
  stack machine — wasm touches nothing upstream"). A well-understood dominator pass is worth more than
  that coupling.
- **Go's giant dispatch loop — the anti-pattern.** Go compiles goroutine bodies to one big loop plus a
  `br_table` dispatching on a "PC" variable — universal (accepts irreducible input) but slow (every edge
  pays the dispatch), justified there only by goroutine stack-switching. Wrong for Teko: it would erase
  the whole point of emitting structured control flow.
- **WASI vs Browser: zero control-flow difference.** `block`/`loop`/`br`/`br_table` are core-spec and
  host-agnostic; no surveyed toolchain branches its CFG lowering by host. The WASI/Browser split (C1-4
  onward) is entirely an imports/runtime-boundary concern, never a control-flow one.

**Verdict:** no plan change — §3.2's scoped CFGStackify is confirmed, now with the empirical grounding
above (PR #403 ratification, 2026-07-11).

### 3.3 The algorithm (specialized CFGStackify)

The pass (`stackify_control` in `stackify.tks`) mirrors LLVM's marker-placement approach:

1. **RPO** — a reverse-post-order over the CFG from the entry block (the same numbering #443 gives
   regalloc, recomputed locally here — C1 does not depend on #443 because it never enters regalloc).
   RPO lays blocks so every forward edge goes to a later block and every back-edge to an earlier one.
2. **Dominators + loop forest** — the immediate-dominator tree (Cooper-Harvey-Kennedy over RPO) and the
   natural loops (a back-edge `b→h` where `h dom b`; the loop body = blocks that reach `b` without
   passing `h`). §3.1 guarantees each header has exactly one such region.
3. **Scope placement** — for each loop, wrap its header..last-body span in a `loop … end` (back-edges
   `br` to it = `continue`); for each merge block that is the target of a forward branch spanning code,
   open a `block … end` so a forward `br` can reach its `end`; render each `LBranch` diamond as
   `if … else … end`. The placement is a stack discipline over the RPO sequence: opening scopes are
   pushed as blocks are entered, `end`s emitted as their spans close, and a **label-depth map**
   `block_id → scope-stack-depth` is maintained.
4. **Edge lowering** — each terminator becomes:
   - `LRet{has_value,value}` → (push value local if any) `return` (or fall through the function `end`).
   - `LJump{target,args}` → copy `args` into `target`'s param-locals (§4.3), then `br depth(target)`
     (omitted when the target is the natural fallthrough — the very next emitted block).
   - `LBranch{cond, t,t_args, f,f_args}` → the diamond: `local.get cond; if …then-edge… else
     …false-edge… end`, OR, when it is a loop back-edge / forward exit, `local.get cond; br_if
     depth(target)` with the other edge as fallthrough.
   - A dense integer `match` MAY lower to `br_table` (jump table) — an **optimization**, deferred as
     `C1-brtable-opt`; the MVP renders `match` as the if/else guard-chain the LIR already gives it
     (correct, just larger). `br_table` is only a size/speed win, never a correctness need.

### 3.4 The honest-stop — `C1-irreducible`

If loop-forest detection ever finds a **multi-entry SCC** (an irreducible cycle — a header with two
in-loop predecessors from outside, impossible per §3.1), `stackify_control` returns the NAMED error
`"C1-irreducible: irreducible control flow (multi-entry loop) — unreachable from structured Teko
source; report to the compiler team"`. This is a belt-and-suspenders guard: it should be **statically
unreachable** from valid Teko, and its unreachability is justified in the PR (the one coverage arm that
may legitimately go uncovered, per the 100%-coverage ruling's "unreachable arm justified in the PR"
exception). It exists so that a future lowering bug that DID emit an irreducible CFG stops honestly
rather than silently miscompiling — the M.3 discipline.

### 3.5 Key type signatures (risk #2)

```teko
/**
 * WasmScope — one open structured-control scope during CFGStackify placement:
 * its kind (`block`/`loop`/`if`) and, for a `block`/`loop`, the LIR block whose
 * boundary the scope's label targets (a `br` to that block resolves to this
 * scope's relative depth). The pass keeps a stack of these; `br` depth is the
 * distance from the branch site down to the matching scope.
 *
 * @since #389 C1-3
 */
pub type WasmScope = struct {
    /**
     * kind — 0 = `block` (label at its `end`, forward exit), 1 = `loop`
     * (label at its start, back-edge/continue), 2 = `if` (structural diamond,
     * not a `br` target).
     */
    kind: u8
    /**
     * label_block — the LIR block id this scope's label targets (meaningful for
     * `block`/`loop`); a `br` to this block resolves to this scope's depth.
     */
    label_block: u32
}

/**
 * StructuredFunc — a function's control flow reconstructed as a flat, ordered
 * stream of structured-control MARKERS + straight-line body chunks: the region
 * tree flattened to the `block`/`loop`/`if`/`else`/`end`/`br`/`br_if` sequence
 * the code section emits, with each LIR block's instructions spliced in at its
 * RPO position. Produced by `stackify_control`; consumed by the instruction
 * lowerer (§4), which fills each block's body with `local.get`/compute/
 * `local.set`. The label-depth map is resolved during placement, so the body
 * lowerer never re-derives scope nesting.
 *
 * @since #389 C1-3
 */
pub type StructuredFunc = struct {
    /**
     * markers — the ordered control markers + block-body anchors (an opcode
     * tag stream: OpenBlock/OpenLoop/OpenIf/Else/End/Br(depth)/BrIf(depth)/
     * BlockBody(block_id)/Return), the region tree in emission order.
     */
    markers: []WasmMarker
    /**
     * loop_result_type — reserved: every scope is typed `[] -> []` (void) in
     * the MVP (block-args cross as locals, §4.3, NOT as branch operands), so
     * multi-value never appears. Carried for the `C1-multivalue-opt` follow-up.
     */
    void_scopes: bool
}

/**
 * stackify_control — reconstruct one `LFunc`'s reducible CFG into a
 * `StructuredFunc`: RPO the blocks, build the dominator tree + natural-loop
 * forest, place `block`/`loop`/`if` scopes, and resolve every `LJump`/`LBranch`/
 * `LRet` edge to a `br`/`br_if`/`return` at its label depth (§3.3). Returns the
 * NAMED honest-stop `C1-irreducible` if a multi-entry loop is found (unreachable
 * from structured Teko, §3.4).
 *
 * @param f the lowered function (its `blocks` a reducible, single-entry CFG)
 * @return the flattened structured-control marker stream, or a named honest-stop
 */
pub fn stackify_control(f: lir::LFunc) -> StructuredFunc | error { … }
```

---

## 4. Risk #1 — THE STACKIFIER (operand-stack vs locals) + instruction lowering

### 4.1 The model — one local per VReg (the always-correct baseline)

Wasm locals are **unlimited and typed** — they function exactly as the LIR's virtual registers do. The
MVP maps **every LIR VReg to one wasm local** (of its `LType`'s valtype), plus one local per
block-param, plus the function's parameters (which are locals 0..n_params-1 by the wasm calling
convention). Register *allocation* is thus a total function with no failure mode:

- Function params → locals `0 .. n_params-1` (wasm ABI: params are the leading locals).
- Every VReg that is a `result` of an inst, and every block-param VReg → a fresh local index `≥
  n_params`, typed by the defining instruction's `LType`.
- The `code` section's per-function **local declarations** are the run-length-compressed list of these
  local types after the params (wasm groups locals as `(count, valtype)` runs).

Each instruction lowers to a transiently-balanced operand-stack sequence that **starts and ends with an
empty operand stack** (the MVP invariant that makes correctness obvious):

```
LBin{op=IAdd, a, b} defining r  →  local.get $a ; local.get $b ; i32.add ; local.set $r
LConstInt{val} defining r       →  i32.const val ; local.set $r     (i64.const for I64)
LUn{op=INeg, a} defining r       →  i32.const 0 ; local.get $a ; i32.sub ; local.set $r
LCall{sym,args} defining r       →  (local.get $arg)* ; call $sym ; local.set $r  (drop if void)
LLoad{addr,ty} defining r        →  local.get $addr ; <ty>.load align,off ; local.set $r
LStore{addr,value,ty}            →  local.get $addr ; local.get $value ; <ty>.store align,off
LParam{index} defining r         →  local.get $index ; local.set $r   (or alias directly)
```

This is LLVM's **ExplicitLocals** baseline — every value materialized through a local, the operand
stack used only within one instruction. It is correct for ANY DAG of instructions with zero analysis.

### 4.2 The RegStackify optimization is DEFERRED (named)

LLVM's `RegStackify` pass THREADS single-use values through the operand stack (a value defined and used
once, in the same block, with no interfering side-effect between, needs no local — it stays on the
stack) to elide `local.set`/`local.get` pairs. This is a **pure size/speed optimization** with a
non-trivial safety analysis (interference, ordering). It is **NOT MVP** — the honest-stop
`C1-regstackify-opt` (0.3.1). The MVP's larger-but-correct code validates + runs identically; the
differential (execution equivalence) is indifferent to code size.

### 4.3 Block-args cross edges as LOCAL COPIES (sidesteps multi-value)

The SSA-lite block-args are phi nodes; the register targets eliminate them as edge copies into
registers. The wasm path does the same into **locals**: before a `br`/`br_if`/`if` edge to `target`,
emit `local.get $arg_i ; local.set $param_i` for each of `target`'s params (evaluated
right-order-safe via a temp when a param feeds another on the same edge — the standard parallel-copy
sequentialization, shared conceptually with regalloc's edge copies). Consequently **no value ever
crosses a `br` on the operand stack**, so every wasm scope is typed `[] -> []` (void) and **multi-value
is never used** — the `C1-multivalue-opt` honest-stop keeps it that way for the MVP.

### 4.4 The load-bearing opcode table (the ground truth; wat2wasm-cross-checked)

Every wasm opcode below is the byte the code section emits. **The implementer MUST re-derive each
against the WebAssembly core spec + `wat2wasm`/`wasm-tools parse` (round-trip a `.wat` and disassemble
with `wasm2wat`) — the table is the spec, the assembler is the oracle** (the A4/B1 discipline). Width
selects the `i32`/`i64` family; signedness on the opcode selects `_s`/`_u`.

| `LOp` / case | wasm instr(s) | opcode(s) | slice |
|---|---|---|---|
| `LConstInt` (I8/16/32/Ptr) | `i32.const` (LEB128 signed) | `0x41` | C1-2 |
| `LConstInt` (I64) | `i64.const` | `0x42` | C1-2 |
| `LConstFloat` (F32/F64) | `f32.const`/`f64.const` (raw IEEE) | `0x43`/`0x44` | C1-2 |
| `LBin IAdd/ISub/IMul` (i32) | `i32.add/sub/mul` | `0x6A/0x6B/0x6C` | C1-2 |
| `LBin IDivS/IDivU/IRemS/IRemU` (i32) | `i32.div_s/div_u/rem_s/rem_u` | `0x6D/0x6E/0x6F/0x70` | C1-2 |
| `LBin IAnd/IOr/IXor` (i32) | `i32.and/or/xor` | `0x71/0x72/0x73` | C1-2 |
| `LBin IShl/IShrS/IShrU` (i32) | `i32.shl/shr_s/shr_u` | `0x74/0x75/0x76` | C1-2 |
| `LBin ICmpEq/Ne` (i32) | `i32.eq/ne` | `0x46/0x47` | C1-2 |
| `LBin ICmpLtS/LtU/…/GeU` (i32) | `i32.lt_s/lt_u/gt_s/gt_u/le_s/le_u/ge_s/ge_u` | `0x48..0x4F` | C1-2 |
| i64 family | `i64.*` (add `0x7C`…, cmp `0x51`…) | (parallel row) | C1-2 |
| `LBin FAdd/FSub/FMul/FDiv` (f64) | `f64.add/sub/mul/div` | `0xA0/0xA1/0xA2/0xA3` | C1-2 |
| `LBin FCmpEq/…/Ge` (f64) | `f64.eq/ne/lt/gt/le/ge` | `0x61..0x66` | C1-2 |
| `LUn INeg` | `i32.const 0; …; i32.sub` (no wasm ineg) | `0x41 0x6B` | C1-2 |
| `LUn INot` | `i32.const -1; …; i32.xor` | `0x41 0x73` | C1-2 |
| `LUn FNeg` | `f64.neg` | `0x9A` | C1-2 |
| `LUn ZExt/SExt/Trunc` | `i32.wrap_i64` / `i64.extend_i32_s/_u` / masks | `0xA7/0xAC/0xAD` | C1-2 |
| `LUn IToF/FToI/FToF` | `f64.convert_i*_s/_u`, `i*.trunc_f*`, `f32.demote/f64.promote` | `0xB7…/0xAA…/0xB6/0xBB` | C1-2 |
| `LParam` | `local.get index` | `0x20` | C1-2 |
| `LCall` | `(local.get arg)*; call funcidx` | `0x10` | C1-2 |
| `LCallIndirect` | `(local.get arg)*; local.get tgt; call_indirect typeidx,0` | `0x11` | C1-2 |
| `LFuncAddr` | `i32.const tableidx` (table slot) | `0x41` | C1-2 |
| `LAlloca` | shadow-stack bump (SP global `-=` size; §5.2) | `0x23/0x24 + i32.sub` | C1-2 |
| `LFieldAddr` | `local.get base; i32.const off; i32.add` | `0x6A` | C1-2 |
| `LLoad` | `<ty>.load align,offset` | `0x28/0x29/0x2A/0x2B` + subwidth | C1-2 |
| `LStore` | `<ty>.store align,offset` | `0x36/0x37/0x38/0x39` + subwidth | C1-2 |
| `LGlobalAddr` | `i32.const dataaddr` (rodata offset) | `0x41` | C1-4 |
| `LRet`/`LJump`/`LBranch` | `return`/`br`/`br_if`/`if`/`else`/`end` | `0x0F/0x0C/0x0D/0x04/0x05/0x0B` | C1-3 |

Two honest-stops live in the lowerer: `C1-i128` (any `I128` operand) and `C1-f16` (any `F16`) — §1.1.

### 4.5 Key type signatures (risk #1)

```teko
/**
 * WasmFunc — one function lowered to a wasm code-section entry: the function's
 * declared locals (the run-length `(count, valtype)` groups AFTER the params),
 * its type signature index (into the module type section), and its instruction
 * byte stream (the structured-control markers of §3 with each block's body
 * spliced in, §4.1). Produced by `stackify_func`; concatenated into the code
 * section by `emit_wasm` (§5).
 *
 * @since #389 C1-2
 */
pub type WasmFunc = struct {
    /**
     * type_index — the index into the module's type (function-signature)
     * section for this function's `(params) -> (results)` shape.
     */
    type_index: u32
    /**
     * local_groups — the non-param locals as run-length `(count, valtype)`
     * pairs, in local-index order (the code-entry local declarations).
     */
    local_groups: []WasmLocalGroup
    /**
     * body — the function body bytes: the structured-control + straight-line
     * instruction stream, terminated by the code-entry `end` (0x0B).
     */
    body: []byte
    /**
     * relocs — the function-body-relative symbol relocations (call funcidx,
     * data addr) the linking section rebases (§5.4); empty for a fully
     * self-contained function (only runtime imports produce them in the MVP).
     */
    relocs: []WasmReloc
}

/**
 * WasmLocalGroup — a run of consecutive locals of one valtype in a code entry's
 * local declarations (wasm compresses locals as `(count, valtype)` runs).
 *
 * @since #389 C1-2
 */
pub type WasmLocalGroup = struct {
    /**
     * count — how many consecutive locals share this valtype.
     */
    count: u32
    /**
     * valtype — the shared wasm value type byte (0x7F i32 / 0x7E i64 /
     * 0x7D f32 / 0x7C f64).
     */
    valtype: u8
}

/**
 * stackify_func — lower one `LFunc` to a `WasmFunc`: structure its control flow
 * (`stackify_control`, §3), allocate one local per VReg + block-param (§4.1),
 * and emit the body by splicing each block's straight-line instruction lowering
 * (§4.4) into the marker stream, with block-args crossing edges as local copies
 * (§4.3). Any per-instruction honest-stop (`C1-i128`/`C1-f16`) or the
 * `C1-irreducible` control stop propagates out unchanged.
 *
 * @param mod_types the module type-section builder (to intern this signature)
 * @param f the lowered function
 * @return the wasm code entry, or a named honest-stop
 */
pub fn stackify_func(mod_types: WasmTypeTable, f: lir::LFunc) -> WasmFunc | error { … }
```

---

## 5. Risk #3 — THE WASM BINARY FORMAT (`src/backend/objfile_wasm.tks`, `emit_wasm`)

### 5.1 LEB128 — the encoding primitive (parallels A4's `emit_u32_le`)

Wasm integers are **LEB128** (unsigned + signed, variable-length). Two primitives anchor the writer,
built on the same `teko::list::push(buf, (x & 0x7F) …)` byte idiom A4 uses for little-endian words:

```teko
/**
 * emit_uleb128 — append `v` to `buf` as unsigned LEB128 (7 bits per byte, low
 * group first, high bit = "more follow"): the wasm size/index/count encoding
 * primitive, paralleling A4's `emit_u32_le` (`backend-a4-encoder.md` §2.1). All
 * arithmetic stays in u64; only the final `& 0x7F` / `| 0x80` narrows to byte
 * (the VM byte-width anchoring gotcha — never widen a byte mid-expression).
 *
 * @param buf the buffer to extend
 * @param v the value to encode
 * @return buf followed by v's ULEB128 bytes
 */
pub fn emit_uleb128(buf: []byte, v: u64) -> []byte { … }

/**
 * emit_sleb128 — append `v` to `buf` as signed LEB128 (the sign bit propagated
 * into the final group per the LEB128 signed rule): the wasm `i32.const`/
 * `i64.const` immediate + signed-field encoding. Distinct from `emit_uleb128`
 * because the termination test is sign-aware (a value and its arithmetic-shift
 * both settle to all-0s or all-1s).
 *
 * @param buf the buffer to extend
 * @param v the signed value to encode
 * @return buf followed by v's SLEB128 bytes
 */
pub fn emit_sleb128(buf: []byte, v: i64) -> []byte { … }
```

### 5.2 The module layout (bit-exact — the ground-truth ordering)

`emit_wasm` writes the 8-byte preamble then the sections **in ascending id order** (the wasm spec
requires it for the known sections); each section is `id:u8 ++ uleb128(size) ++ payload`:

- **Preamble** — magic `0x00 0x61 0x73 0x6D` (`"\0asm"`) ++ version `0x01 0x00 0x00 0x00`.
- **Type (1)** — the interned `(params)->(results)` function signatures (`0x60` form byte + param
  valtypes + result valtypes). The MVP has ≤1 result (no multi-value, §4.3).
- **Import (2)** — the runtime functions the module needs but does not define: each `(module_name,
  field_name, kind=func, typeidx)`. In the WASI/bootstrap path this is empty when the C `teko_rt` is
  linked in as a *defining* object (wasm-ld resolves `tk_*`); in the self-contained/Browser path it
  imports `tk_exit`/`tk_print`/… from the `"env"` module (§7, §9).
- **Function (3)** — one `typeidx` per defined function (the type index each code entry uses).
- **Table (4)** — a single `funcref` table sized to the `LFuncAddr`/vtable slot count (present only
  when the program takes function addresses / does indirect calls; empty otherwise).
- **Memory (5)** — a single linear memory `{min, max?}` in 64 KiB pages (the MVP requests a fixed
  initial size covering rodata + the shadow stack + the arena's first chunk; growth via `memory.grow`
  rides `tk_region_alloc`, §6).
- **Global (6)** — the **shadow-stack pointer** global (`SP`, mutable `i32`, init = top of the reserved
  stack region) that `LAlloca` bumps (§4.4); plus any `LGlobal` (none in the N2 subset →
  `C1-globals` honest-stop, mirroring A4's `A4-globals`).
- **Export (7)** — `_start` (WASI entry) / `main` and (Browser) the memory, per §9.
- **Start (8)** — omitted; WASI uses the `_start` export convention, not the start section.
- **Element (9)** — the funcref table's initializer (the `LFuncAddr` slot → funcidx map), when a table
  exists.
- **Code (10)** — one entry per function: `uleb128(entry_size) ++ local_decls ++ body_bytes`
  (`WasmFunc.local_groups` then `WasmFunc.body`). This is the load-bearing section.
- **Data (11)** — the rodata: one active data segment per `LRodata` (or one merged segment) placing the
  interned string bytes at their assigned linear-memory offsets (`LGlobalAddr` resolves to these).

### 5.3 The writer entry

```teko
/**
 * WasmModule — the whole program lowered to wasm: the interned type table, the
 * runtime imports, the defined functions (each a `WasmFunc`), the linear-memory
 * plan (rodata bytes + their offsets, the shadow-stack + arena reservation),
 * the funcref table, and the export set. `emit_wasm` serializes it to a module
 * (or a relocatable object, §5.4). The wasm analog of `EncodedModule`, but
 * self-describing — there is no separate symbol table / section-relative reloc
 * model; wasm references are funcidx/dataaddr, resolved by index or (across the
 * runtime boundary) by the linking section.
 *
 * @since #389 C1-1
 */
pub type WasmModule = struct {
    /**
     * types — the interned function signatures (the type section).
     */
    types: []WasmFuncType
    /**
     * imports — the runtime functions imported from the host (WASI/env); empty
     * when `teko_rt` is linked as a defining object.
     */
    imports: []WasmImport
    /**
     * funcs — the defined functions, in module order (the function + code
     * sections).
     */
    funcs: []WasmFunc
    /**
     * data — the rodata segments: interned bytes + their linear-memory offsets
     * (the data section; `LGlobalAddr` resolves here).
     */
    data: []WasmDataSegment
    /**
     * mem_min_pages — the initial linear-memory size in 64 KiB pages (covers
     * rodata + shadow stack + the arena's first chunk).
     */
    mem_min_pages: u32
    /**
     * exports — the module exports (`_start`/`main`; Browser also memory).
     */
    exports: []WasmExport
}

/**
 * emit_wasm — serialize a `WasmModule` to wasm module bytes: the 8-byte
 * preamble then every non-empty section in ascending id order (§5.2), each
 * framed `id ++ uleb128(size) ++ payload`. Parallels `emit_macho`/`emit_elf`/
 * `emit_coff` (a per-format module writer over the encoded program), but emits
 * a self-contained MODULE, not a relocatable `.o` linked by `cc`. When
 * `relocatable` is set it additionally appends the `linking` + `reloc.CODE`/
 * `reloc.DATA` custom sections `wasm-ld` consumes (§5.4).
 *
 * @param m the lowered wasm module
 * @param relocatable emit the linking metadata for `wasm-ld` (the bootstrap
 *                    link path, §5.4/§6) vs a final self-contained module
 * @return the wasm module (or relocatable object) bytes
 */
pub fn emit_wasm(m: WasmModule, relocatable: bool) -> []byte { … }
```

### 5.4 Relocatable object vs self-contained module (the "link is different" fact)

Wasm is a self-contained module, so "linking" has two honest realizations:

- **Relocatable object + `wasm-ld` (RECOMMENDED bootstrap, §6, §11 D-W1).** `emit_wasm(m, true)`
  appends the **`linking` custom section** (a `WASM_SYMBOL_TABLE` naming each defined/imported function
  + data symbol, and `WASM_SEGMENT_INFO`) and the **`reloc.CODE`/`reloc.DATA`** custom sections
  (`R_WASM_FUNCTION_INDEX_LEB` for calls, `R_WASM_MEMORY_ADDR_*` for data refs). `wasm-ld` stitches the
  own object + the clang/wasi-sdk-built `teko_rt` object + wasi-libc into a final module — the FAITHFUL
  mirror of A4's `.o` + `cc` bootstrap (the same "own code as a relocatable object, foreign runtime
  linked in" shape). This is in-family with the ELF/Mach-O/COFF relocation work the register targets
  already do.
- **Self-contained module + imports (the ALTERNATIVE; §11 D-W1).** `emit_wasm(m, false)` emits a final
  module that IMPORTS `tk_*` from `"env"`; a host (WASI shim / JS glue) supplies them. Simpler (no
  linking metadata) but needs a runtime PROVIDER — the full C `teko_rt` is not trivially importable
  without either wasm-ld or a JS reimplementation (the M.1 sin, §7). Used only for the **Browser** N6b
  path where the JS glue IS the provider (§9).

Both are behind the same `emit_wasm` flag, so the choice is a link-tail decision, not an encoder fork.

---

## 6. Risk #4 — LINEAR MEMORY + `teko_rt` (the WASI binding)

### 6.1 The single linear memory maps the whole address space

Wasm has ONE linear memory (a flat `i32`-addressed byte array). The compiler's address model already
collapses every address-shaped type to `Ptr` (an `i32` in wasm32), so pointers ARE linear-memory
offsets with no translation. The memory is laid out low→high:

1. **rodata** (interned string bytes, the data section) at a fixed low base.
2. the **shadow stack** — a reserved region a mutable `SP` global bumps DOWN; `LAlloca{bytes,align}`
   does `SP = align_down(SP - bytes, align); yield SP` (§4.4). This models the native stack frame the
   register targets get from the hardware SP; wasm has no addressable native stack, so allocas MUST use
   linear memory (the standard clang wasm shadow-stack).
3. the **arena** (`tk_region_*`) grows above the shadow stack via `memory.grow`.

`memory.grow` is the M.4-honest cost: the arena's OOM/grow path calls it instead of `mmap` (wasi-libc's
`malloc`/`sbrk` already do this under the hood when `teko_rt` is linked, §6.2 — so for the bootstrap
path the compiler emits NO explicit `memory.grow`; wasi-libc owns it).

### 6.2 The runtime — WASI, mirroring the C path

`teko_rt.c` makes ~131 libc calls (measured: `malloc`/`free`/`memcpy`/`fprintf`/`fwrite`/`write`/
`read`/`open`/`exit`/… — §measured in the C source). **All of these are provided by wasi-libc** (the
wasi-sdk libc): `exit`→`__wasi_proc_exit`, `write`/`fwrite`→`fd_write`, `malloc`/`free`→dlmalloc over
`memory.grow`, etc. So the recommended runtime binding is: **compile `src/runtime/teko_rt.c` +
`assert.c` to a wasm object with `clang --target=wasm32-wasi` (wasi-sdk) — or `zig cc -target
wasm32-wasi` — and link it in via `wasm-ld`** (§5.4). This is D-2 option-1 for wasm: **the SAME C
runtime, unchanged**, riding the foreign toolchain during bootstrap; no second runtime, no divergence
(M.1). The own-emitted module's `tk_exit`/`tk_print`/`tk_region_*` calls become
`R_WASM_FUNCTION_INDEX_LEB` relocations `wasm-ld` resolves against that object.

- **`exit(n)`** → `tk_exit(n)` → wasi `proc_exit(n)` → wasmtime returns exit code `n`. Direct
  differential signal.
- **`teko::io::println(s)`** → `tk_println(s)` → wasi `fd_write(1, …)` → wasmtime forwards to stdout.
  Direct differential signal (stdout compare).

The Browser variant (N6b) has no WASI; its JS glue provides the imports (§9), and for the MVP corpus
reimplements only the handful the corpus touches (`tk_exit`→set exit var, `tk_print`→`console.log`) —
the FULL browser `teko_rt` (all ~131 calls, via a wasm-compiled `teko_rt` + JS syscall shims) is the
`C1-browser-rt` honest-stop.

### 6.3 Reported finding R-1 (sequencing, not a new issue)

The `own_print_exit` fixture needs `fd_write` (a wider runtime surface than the `exit(n)`-only
frameless first-light). This is not a divergence — it just means the wasm first-light corpus should
START with `own_exit_zero`/`own_exit_code` (pure `proc_exit`) and add the `print` fixtures once the
`fd_write` path is linked (C1-4/C1-5). REPORTED so the integrator sequences the corpus growth, not
turned into a new issue.

---

## 7. Risk #5 — THE DIFFERENTIAL (execution equivalence, not byte-identity)

### 7.1 The two sides + the oracle

- **C-path wasm (trusted):** the existing C backend emits `<stem>.c`; `zig cc -target wasm32-wasi
  <stem>.c teko_rt.c assert.c -o <stem>.c.wasm` produces the C-side module. (zig is a locally
  available cross cc — memory: "zig, Docker+qemu".)
- **own-path wasm:** `TEKO_BACKEND=native TEKO_TARGET=wasm32-wasi teko build <fixture> -o <ownbin>` →
  `emit_native_wasm` → `<stem>.wasm`.
- **Run both under `wasmtime`** (`wasmtime run <mod.wasm>`), capture exit code + stdout, assert equal.
  **Byte-identity is NOT expected** (different codegen) — this is the riscv64/qemu lane's model
  (execution equivalence), NOT the arm64/x86 goldens' byte model.

### 7.2 Oracles + availability gate (the new lane in `diff_c_own.sh`)

The wasm lane is selected by `TEKO_DIFF_TARGET=wasm32-wasi` (explicit, like the riscv lane — the CI
host is not a wasm host) and honest-skips (exit 0, named reason) when its oracles are absent:

- `wasm-tools validate` (or `wasm-validate` from wabt) — well-formedness of BOTH modules before
  running (catches a malformed own module at the encoder, not as a runtime trap).
- `wasmtime` — the executor for both sides. (`node --experimental-wasi-unstable-preview1` or `wasmer`
  are documented fallbacks; the lane probes `wasmtime` first.)
- `wasm2wat` — for failure diagnostics only (dump the own module on a mismatch).

Availability check pattern (mirrors the riscv lane's `command -v riscv64-linux-gnu-gcc`/`qemu-…`
gates): `command -v wasmtime` AND (`command -v wasm-tools` OR `command -v wasm-validate`) AND `command
-v zig` — any missing → `echo "diff_c_own: skipped — wasm lane needs wasmtime + wasm-tools + zig; not
found on this host"; exit 0`. Local resources for the runner: zig (present), Docker (a
`ghcr.io/webassembly/wasi-sdk` or a `wasmtime` image is the fallback provisioning path). The
`"(own backend)"` success-marker guard (harness blind-spot F1(a)) carries over unchanged — the own
build must print it or the lane FAILS (never silently passes via a C fallback).

### 7.3 The i128/f16/print-before-link honest-skips are IDENTICAL on both sides

A fixture that reaches a named honest-stop (`C1-i128`/`C1-f16`) stops IDENTICALLY on the own side and is
compared at the stop, never at a fabricated value (the A2/A3/A4 §4 precedent). The MVP corpus is the
`exit(n)`/`print` integer/control subset that reaches none of them.

---

## 8. Target dispatch — how wasm slots into the `--target` routing (#390)

The register targets flow through `emit_native` (`project.tks:786`) → `emit_native_<isa>` →
`finish_native_object` (write `.o` + `cc`-link). Wasm ADDS a target and a PARALLEL tail (the `.o` +
`cc` tail does not fit a self-contained module):

```teko
/**
 * NativeTarget — which own-AOT-backend target the temporary `TEKO_TARGET` env
 * seam selects (D1/#390's real `--target` supersedes it). C1 adds the two wasm
 * variants: WASI (a wasmtime/host module) and Browser (a JS-import module).
 * Both share the SAME emitted code — they differ only in the runtime import set
 * + the link/glue tail (§9), so a single `emit_native_wasm` handles both, keyed
 * by the variant.
 */
type NativeTarget = enum { Arm64Macho; X8664Linux; Riscv64Linux; X8664Windows; Wasm32Wasi; Wasm32Browser }

/**
 * emit_native_wasm — the wasm own-backend tail (#389 C1): lower the program to
 * LIR, STACKIFY it (control-structuring + local-alloc + instruction lowering,
 * §3/§4 — NO isel, NO regalloc), assemble a `WasmModule`, emit it as a
 * relocatable object (`emit_wasm(m, true)`), and hand the bytes to
 * `finish_wasm_module` (write `.wasm.o` + `wasm-ld`-link against the C-built
 * `teko_rt` + wasi-libc → `.wasm`, §6.2) — OR, for `Wasm32Browser`, emit a
 * self-contained module + the JS glue (§9). The object-format-agnostic
 * `finish_native_object` does NOT apply (wasm is not a `cc`-linked `.o`), so
 * wasm owns this parallel tail; the `"(own backend)"` marker is preserved so the
 * differential harness keys on it (F1(a)).
 *
 * @param dir the project directory (diagnostics)
 * @param od the slash-stripped output directory (already created)
 * @param stem the output artifact name
 * @param lmod the lowered LIR module
 * @param prog the checked program (`.tsym` emission, extern-reachability)
 * @param m the resolved manifest
 * @param browser true for the `Wasm32Browser` variant (JS-import + glue)
 * @return 0 on a successful build, else the failing status
 */
fn emit_native_wasm(dir: str, od: str, stem: str, lmod: lir::LModule, prog: checker::TProgram, m: Manifest, browser: bool) -> i32 { … }
```

`target_from_name` (`project.tks:719`) gains: `"wasm"`/`"wasm32"`/`"wasm32-wasi"`/`"wasi"` →
`Wasm32Wasi`; `"wasm32-browser"`/`"wasm-browser"`/`"browser"` → `Wasm32Browser`. `emit_native`'s `match`
gains the two arms → `emit_native_wasm(…, browser=false/true)`. This is additive + FIXPOINT-preserving
(the default stays `Arm64Macho`, the whole native path stays behind `TEKO_BACKEND=native`). When D1/#390
lands the real `--target`, wasm routes through it identically — the two enum variants are the seam.

---

## 9. The Browser JS-import variant (N6b) — C1-7

The Browser variant emits the **SAME `WasmModule`** (identical stackifier + code section) with a
DIFFERENT runtime binding:

- `emit_wasm(m, false)` — a self-contained module that IMPORTS the corpus runtime functions from the
  `"env"` module and EXPORTS `main` + `memory`.
- `extensions/web/teko.js` (new) — the JS glue: fetch/instantiate the module, provide the `env`
  imports (`tk_exit`→record exit code + throw a sentinel; `tk_print`/`tk_println`→decode the
  `(ptr,len)` from `memory` and `console.log`), and expose a `run()` returning the exit code. A minimal
  `extensions/web/teko.html` harness loads it for the differential (headless via `node
  --experimental-wasi-unstable-preview1` OR a Playwright/headless-Chrome run — Chrome is a local
  resource).
- The corpus runs the same fixtures; the differential asserts the JS-side exit/stdout equals the C/WASI
  side (§7).

**Honest-stops for N6b:** `C1-browser-rt` — only the corpus's handful of `tk_*` are JS-reimplemented;
the FULL browser `teko_rt` (all ~131 libc-equivalents, e.g. via compiling `teko_rt.c` to a second wasm
module + a JS WASI-shim like `@wasmer/wasi`) is deferred. `C1-browser-fs` — filesystem/`open`/`read`
have no browser analog without a virtual FS; deferred. Both are named, so a browser program touching
them stops honestly.

---

## 10. Honest-stops — which slice each lives behind

| stop | slice | what it defers | closed by |
|---|---|---|---|
| `C1-i128` | C1-2 (`wasm_valtype`) | i128 ops (no wasm i128) — inherited (isel never emits i128) | the i128 route (shared with A2) |
| `C1-f16` | C1-2 | f16 (no wasm f16) — matches the register targets' gap | when a target grows f16 |
| `C1-regstackify-opt` | C1-2 | operand-stack threading of single-use values (size/speed only) | 0.3.1 |
| `C1-irreducible` | C1-3 | irreducible CFG — **unreachable** from structured Teko (§3.4) | never expected; a lowering-bug tripwire |
| `C1-brtable-opt` | C1-3 | dense-`match` → `br_table` jump table (size/speed only) | 0.3.1 |
| `C1-multivalue-opt` | C1-3 | block/branch operand values (multi-value) — block-args cross as locals | 0.3.1 |
| `C1-globals` | C1-4 | `LGlobal` data (N2 lowers none) — mirrors `A4-globals` | when lowering emits globals |
| `C1-browser-rt` | C1-7 | the FULL browser `teko_rt` (all libc calls) — only corpus `tk_*` in JS | later |
| `C1-browser-fs` | C1-7 | browser filesystem (`open`/`read`) — no browser analog | later |
| SIMD | — | `v128` / SIMD proposal — not in the MVP feature set | later wave |
| threads / atomics | — | threads + `atomic.*` proposal — rides #456 threading, post-MVP | #456-aligned |
| reference types (beyond funcref) | — | `externref`/typed refs — not needed by the corpus | later |
| GC proposal | — | wasm-GC structs/arrays — Teko has NO GC (arena/region); never | never (design ruling) |
| exceptions proposal | — | `try`/`catch`/`throw` — Teko errors are values (`T\|error`), not wasm EH | never (design ruling) |
| own wasm linker (drop `wasm-ld`) | Phase E | the own module linker | Phase E/#226 |
| `--target` real flag | later | the manifest flag (C1 uses the `TEKO_TARGET` env seam) | D1/#390 |

Each stop is a NAMED error the pipeline surfaces; a fixture reaching one stops identically on the own
side (M.3, the A2/A3/A4 §4 precedent). Note the last three feature-stops (**GC**, **exceptions**) are
**permanent design rulings**, not deferrals: Teko has no GC (arena/region model) and models errors as
values, so those wasm proposals are never targets — recorded so no one re-opens them.

---

## 11. Deferrals + recorded decisions (law-first — no HALT) + reported findings

**D-W1 · Relocatable object + `wasm-ld` vs self-contained module (RESOLVED law-first; owner ruling
optional).** The WASI bootstrap emits a **relocatable** object and links the C-built `teko_rt` via
`wasm-ld` (§5.4/§6.2). **Law-first (M.1/M.3):** it reuses the SINGLE C runtime unchanged (no second
runtime → M.1) and is the honest analog of A4's `cc`-as-linker bootstrap (M.3 — independence deferred to
Phase E, not falsely claimed now). The alternative (self-contained module importing a JS/host-provided
runtime) is used only where there is no other provider (Browser N6b), because for WASI it would force a
second runtime implementation (the M.1 sin). **Flagged for the owner ONLY as a scope-size call:** if the
owner prefers to defer the relocatable-object linking metadata (the `linking`/`reloc.*` custom sections,
the fiddliest part of C1-5) and ship the MVP via a self-contained module that imports a THIN wasi-shim
for just `proc_exit`+`fd_write` (dropping the full `teko_rt` for the MVP corpus), that trades M.1-purity
for a smaller C1-5 — a legitimate MVP-scoping choice. **Recommendation: relocatable + `wasm-ld` (full
`teko_rt`, M.1-pure).** Not blocking — both are behind the same `emit_wasm(relocatable)` flag.

**D-W2 · N6b (Browser) depth for #389 (RESOLVED law-first; owner ruling optional).** "Issues are 100%"
means #389 delivers BOTH N6a (WASI) and N6b (Browser). N6b shares the ENTIRE upstream (stackifier +
`emit_wasm`); it adds only the import-variant + JS glue + a headless differential (C1-7). **Law-first:**
delivering N6b at the corpus depth (the handful of `tk_*` the `exit(n)`/`print` corpus needs, with
`C1-browser-rt`/`C1-browser-fs` as named stops for the rest) satisfies the issue's proposal while
honoring MVP-first (SIMD/threads/etc. deferred by feature, not by runtime). **Flagged for the owner
ONLY** in case they prefer N6b split to a follow-up issue (it is the one slice with a non-compiler
deliverable — JS glue + a browser CI harness). **Recommendation: keep N6b in #389 at corpus depth.**

**D-W3 · No `#443` dependency (recorded).** C1 recomputes RPO locally (§3.3) and never enters regalloc,
so — unlike A4 — it does **not** require #443. It requires only A1 (the LIR). This is what lets C1 run
in parallel with Phase B (own-backend-architecture §4 ordering).

**D-W4 · The stackifier owns isel+regalloc; NO parallel interp oracle for the MVP (recorded).** The
register targets have `minst_interp` as a pre-machine oracle; wasm's pre-machine oracle is the LIR
interpreter (`lir_interp`, the shared 4th oracle) PLUS `wasm-tools validate` (well-formedness) —
building a wasm interpreter in Teko is out of scope (wasmtime IS the executor). Grounded, not open
(mirrors B1's "no parallel minst_x86_interp for the MVP").

**R-1 · REPORTED up (sequencing, not a new issue).** The `print` fixtures need `fd_write` (a wider
runtime surface than `exit(n)`-only). The wasm first-light corpus should start `exit(n)`-only and add
`print` once `fd_write` is linked (§6.3). Folded into the C1-4/C1-5 sequencing, not spun into an issue.

**No genuine unresolved tension → no HALT.** Every fork resolves law-first with a named follow-up; D-W1
and D-W2 are scope-size calls the owner MAY weigh in on but that the recommendation already settles.

---

## 12. Regression fixtures + the gate

### 12.1 Golden byte/wat unit tests (engine-agnostic, no execution)

- **`src/backend/objfile_wasm_test.tkt`** (C1-1): `emit_uleb128(624485) → [0xE5 0x8E 0x26]`;
  `emit_sleb128(-123456) → [0xC0 0xBB 0x78]`; the preamble bytes `00 61 73 6D 01 00 00 00`; a
  one-function empty-`main` module's section framing (type/function/code/export ids in order); the code
  entry `end` byte `0x0B`.
- **`src/backend/stackify_test.tkt`** (C1-2): per-`LOp` golden instruction bytes —
  `LConstInt{42}→i32.const → 41 2A`; `LBin{IAdd}→ 20 <a> 20 <b> 6A 21 <r>`; `LCall{tk_exit}→ 20 <arg>
  10 <funcidx>`; `LLoad{I32}→ 20 <addr> 28 <align> <off>`; each `I128` case → the `C1-i128` stop.
- **`src/backend/stackify_test.tkt`** (C1-3): a two-block `if` → `if … else … end` with the block-arg
  local-copy; a `loop` → `loop … br 0 … end` with the back-edge `br`; a `match` guard-chain → nested
  `if`; a fabricated irreducible CFG → the `C1-irreducible` stop.
- **Cross-check:** `wasm-tools validate` / `wasm2wat` on the emitted bytes in the C1-1/C1-4/C1-5 gate
  (macOS/linux — cross-format tools, no wasm host needed to VALIDATE; execution is the C1-6 lane).

### 12.2 End-to-end differential fixtures (`examples/regressions/own_*`, driven by `diff_c_own.sh`)

The SAME corpus the register lanes use — re-run under wasmtime:

| fixture | program | expected exit | stdout | added at |
|---|---|---|---|---|
| `own_exit_zero` | `exit(0)` | 0 | — | C1-5 (first light) |
| `own_exit_code` | `exit(42)` | 42 | — | C1-5 |
| `own_arith_exit` | `exit(6 * 7)` | 42 | — | C1-5 |
| `own_sub_exit` | `exit(100 - 58)` | 42 | — | C1-5 |
| `own_if_exit` | `if 5 > 3 { exit(1) } else { exit(2) }` | 1 | — | C1-5 (needs C1-3) |
| `own_match_exit` | `match k { 0 => exit(7); _ => exit(9) }` | per k | — | C1-5 (needs C1-3) |
| `own_print_exit` | `println($"answer={6*7}!"); exit(42)` | 42 | `answer=42!` | C1-6 (needs `fd_write`, R-1) |

The VM + C-native legs already exist (`diff_vm_native.sh`); C1-6 adds the **own-wasm leg** and asserts
`own-wasm exit/stdout == C-wasm exit/stdout` under wasmtime. Browser (C1-7) re-runs the same corpus
through the JS harness.

### 12.3 Ritual + coverage posture

- **Every C1-N** owes the full ritual: **both-engine gate** (native `teko . -o bin` AND `teko test .`
  VM), **paranoid**, **fixpoint**, and **100% coverage on new code** (definition-of-done). The
  stackifier + LEB128 writer are highly branchy (per-`LOp`, per-width, per-scope) — cover every lowered
  case + every honest-stop arm via golden tests; the one genuinely-unreachable arm (`C1-irreducible`)
  is justified in the PR (the coverage-ruling exception).
- **VM-gotcha watch** (dense byte-work, from A4 §9.3): (a) build buffers via `teko::list::push(buf, (x
  & 0x7F) to byte)` — never widen a `byte` mid-expression (byte-width anchoring); (b) all LEB128
  bit-slicing in `u64`/`i64`, narrow to `byte` only at the last step; (c) no `x = match {…return}` — use
  `let x = match {…}` then act (the isel/regalloc VM gotcha); (d) qualify every ns (`teko::backend::…`)
  — a bare name colliding with a builtin gives no diagnostic (fase-3 gotcha).
- **Fixpoint is trivially preserved** through C1-1..C1-4 (new files, unreachable from the default path)
  and C1-5..C1-7 (the wasm path is behind `TEKO_BACKEND=native` + `TEKO_TARGET=wasm32-*`; the default
  stays arm64/C). No `src/` file uses a wasm-only feature, so the stable-seed lane is unaffected.

### 12.4 Ritual points (where the FULL gate must pass)

- After **C1-3** (structured control): full both-engine gate + `wasm-tools validate` on the if/loop/
  match goldens (the riskiest bit — the region reconstruction).
- After **C1-5** (first END-TO-END module): the whole gate + the first `own-wasm == C-wasm` run on the
  wasm lane (`exit(n)` corpus).
- The **KEYSTONE full ritual at C1-6**: the whole gate — both engines + fixpoint + the **new wasm
  differential leg green under wasmtime** (WASI, **N6a done**).
- **C1-7** (Browser, **N6b done**): the JS-harness differential green (headless). **#389 CLOSES here.**

---

## 13. The sub-slice decomposition (ordered, each gate-able)

```
A1 (LIR, done) ─▶ C1-1 ─▶ C1-2 ─▶ C1-3 ─▶ C1-4 ─▶ C1-5 ─▶ C1-6 ─▶ C1-7   [#389 closes]
                  module   stackify  control  linear-  link +   WASI     Browser
                  +LEB128   straight  flow     mem +    emit_    diff     JS-import
                  emit_wasm -line     (region  teko_rt  native_  (N6a)    (N6b)
                  framing   lowering  tree)    WASI     wasm
                            (∥ Phase B — needs only A1; never enters isel/regalloc)
```

- **C1-1 · module model + LEB128 + `emit_wasm` framing** — `src/backend/objfile_wasm.tks`:
  `emit_uleb128`/`emit_sleb128`, `WasmModule`/`WasmFunc`/`WasmFuncType`/`WasmImport`/`WasmExport`/
  `WasmDataSegment`, the preamble + ascending-id section framing, an empty-`main` module. **Proven by:**
  golden byte tests + `wasm-tools validate`. No LIR yet.
- **C1-2 · the stackifier — locals + straight-line lowering** — `src/backend/stackify.tks`:
  `wasm_valtype`, VReg→local allocation, `WasmLocalGroup`, `stackify_func`'s per-`LOp` lowering for the
  straight-line subset (§4.4), the `C1-i128`/`C1-f16`/`C1-regstackify-opt` stops. **Proven by:**
  per-`LOp` golden instruction bytes. No control flow, no execution.
- **C1-3 · structured control flow (the CFGStackify pass)** — `src/backend/stackify.tks`:
  `WasmScope`/`StructuredFunc`/`stackify_control` (RPO + dominators + loop forest → scope placement +
  `br` label depths), block-args as edge local-copies (§4.3), the `C1-irreducible`/`C1-brtable-opt`/
  `C1-multivalue-opt` stops. **Proven by:** if/loop/match golden `.wat` + `wasm-tools validate`. The
  biggest risk (§3). No execution yet.
- **C1-4 · linear memory + rodata/data + WASI import binding** — `objfile_wasm.tks` (memory section +
  data section + shadow-stack `SP` global + `LAlloca` bump) + the WASI runtime import set (§6); the
  `C1-globals` stop. **Proven by:** a rodata/`print` module validates; `LGlobalAddr` resolves to a data
  offset (golden).
- **C1-5 · relocatable object + `wasm-ld` link + `emit_native_wasm` wiring** — `objfile_wasm.tks`
  (`emit_wasm(m, true)` linking + `reloc.*` custom sections), `src/build/project.tks`
  (`emit_native_wasm` + `finish_wasm_module` + `link_wasm`, `NativeTarget::Wasm32Wasi/Browser`,
  `target_from_name`), link via `wasm-ld` against the clang/wasi-sdk `teko_rt` + wasi-libc. **Proven
  by:** the FIRST end-to-end `.wasm` runs `exit(n)` under wasmtime.
- **C1-6 · the differential lane (N6a)** — `scripts/diff_c_own.sh` wasm lane (own-wasm == C-wasm
  execution equivalence under wasmtime; the `wasm-tools`/`wasmtime`/`zig` availability gate + honest-
  skip; the `"(own backend)"` marker guard). **Proven by:** `own-wasm == C-wasm` exit/stdout over the
  corpus. **N6a done.**
- **C1-7 · the Browser JS-import variant (N6b)** — `emit_native_wasm(browser=true)` (self-contained
  module + `env` imports), `extensions/web/teko.js` + `teko.html` glue, the headless JS differential;
  the `C1-browser-rt`/`C1-browser-fs` stops. **Proven by:** the JS-harness differential green.
  **N6b done; #389 CLOSES.**

**Files:** new `src/backend/stackify.tks`, `src/backend/objfile_wasm.tks`,
`src/backend/stackify_test.tkt`, `src/backend/objfile_wasm_test.tkt`, `extensions/web/teko.js`,
`extensions/web/teko.html`; touched `src/build/project.tks` (`NativeTarget` + `target_from_name` +
`emit_native` arm + `emit_native_wasm`/`finish_wasm_module`/`link_wasm`), `scripts/diff_c_own.sh` (the
wasm lane). **Reuses unchanged:** `lir::lower_program`, the whole `lir` type surface, the C
`src/runtime/teko_rt.{c,h}` (compiled to wasm by wasi-sdk), the `examples/regressions/own_*` corpus, the
`TEKO_BACKEND`/`TEKO_TARGET` seams. **NEVER touched (the fire-test):** `isel_*.tks`, `regalloc*.tks`,
`abi_*.tks`, `encode_*.tks`, `minst*.tks` — the wasm path proves the LIR alone suffices to drive a
stack machine.

---

## Appendix · the wasm value-type + section-id quick reference (for the writer)

- **Value types:** `i32` 0x7F, `i64` 0x7E, `f32` 0x7D, `f64` 0x7C; funcref 0x70; functype form 0x60.
- **Section ids:** custom 0, type 1, import 2, function 3, table 4, memory 5, global 6, export 7,
  start 8, element 9, code 10, data 11, datacount 12.
- **Control:** block 0x02, loop 0x03, if 0x04, else 0x05, end 0x0B, br 0x0C, br_if 0x0D, br_table 0x0E,
  return 0x0F, call 0x10, call_indirect 0x11.
- **Vars:** local.get 0x20, local.set 0x21, local.tee 0x22, global.get 0x23, global.set 0x24.
- **Consts:** i32.const 0x41, i64.const 0x42, f32.const 0x43, f64.const 0x44.
- **Linking (relocatable):** the `linking` custom section (version 2) `WASM_SYMBOL_TABLE` (0x08) +
  `WASM_SEGMENT_INFO` (0x05); `reloc.CODE`/`reloc.DATA` with `R_WASM_FUNCTION_INDEX_LEB` (0),
  `R_WASM_MEMORY_ADDR_LEB` (3), `R_WASM_TABLE_INDEX_SLEB` (1). The implementer re-derives each against
  the wasm-tools / LLVM `wasm-ld` object docs + a `clang --target=wasm32-wasi -c` reference object
  (`wasm-tools print`), exactly as A4 re-derives arm64 against `llvm-mc`.
