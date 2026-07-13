# A1 / N2a — closing the LIR lowering coverage frontier (crumb plan)

**Status:** PLAN (doc-only). Sub-PR of the 0.3 own-AOT-backend wave (umbrella
`remodel/backend-build`). Issue **#382**. Branch `fix/issue-382-lir-lowering`, which becomes a
**mini-umbrella**: A1 is XL, so it is drained as an ordered set of sub-sub-PRs based on this branch
itself, each independently gate-able and kept well under the timebox. Base for the design:
`docs/design/own-backend-architecture.md` §3.2 (the N2 coverage frontier) and §4 Phase A.

> The deliverable of A1 is a COMPLETE TAST→LIR lowering (every construct the checker produces)
> proven by the LIR-interpreter oracle (exit-code parity vs the VM over the runnable subset) plus
> golden LIR-dump diffs. A1 emits NO machine code: isel/regalloc/encode/objfile are A2–A4. The value
> of A1 is lowering completeness, validated independently of any target.

---

## 1. The assessed starting point (done vs. todo)

Read fresh from the four N1 files on this branch. N1 (#221) delivered a **single-block,
register-only** lowering; everything past straight-line integer/float scalar code honest-stops.

### 1.1 What N1 already lowers (the closed subset)

Grounded in `src/lir/lower.tks`:

- `lower_expr` dispatch (`:298-308`): `TNumber`, `TVar`, `TBinary`, `TUnary`, `TCast`, `TCall` —
  every other `TExprKind` hits the catch-all honest-stop at `:306`.
- `lower_stmt` dispatch (`:442-449`): `TBinding` (simple-name target only, `:453-465`), `TReturn`,
  `TExprStmt` — every other `TStatement` hits the catch-all at `:447`.
- Arithmetic only: `lbinop_of` (`:144-171`) covers `+ - * / %`; `lunop_of` (`:175-181`) covers unary
  `- !`; numeric `to` casts (`:187-205`). Comparisons/bitwise/shifts are **declared** in `LBinOp`
  (`lir.tks:30-35`) but never emitted.
- Direct calls: `exit`→`tk_exit`, `panic`→`tk_panic_str`, Teko→Teko via the shared mangler
  (`call_symbol` `:426-437`). No other builtin lowers (`:433-434`).
- Function/module structure: one `LFunc` per `TFunction`, params materialized as `LParam` into
  VRegs `0..n_params` (`lower_function` `:554-570`), a synthesized virtual `main` over the loose
  top-level statements (`lower_program`/`lower_virtual_main` `:624-678`).

The LIR interpreter (`lir_interp.tks`) runs exactly this subset: integer const/bin/un/param/ret + a
direct Teko call + `tk_exit`. Floats are lowered but **not run** (`:166`); `LJump`/`LBranch` are
declared but honest-stop at runtime (`:172-173`). The printer (`lir_print.tks`) is already complete
for the whole *declared* op set — it prints `jump`/`branch`/block-args (`:162-174`, `:197-201`) even
though the lowering never yet produces them.

### 1.2 The exact honest-stops A1 must close

| Site | Location | Construct |
|------|----------|-----------|
| `lower_expr` catch-all | `lower.tks:306` | `TIfExpr`, `TMatchExpr`, `TCompare`, `TFieldAccess`, `TSafeFieldAccess`, `TCoalesce`, `TStructInit`, `TIndex`, `TInterp`, `TInExpr`, `TPathExpr`, `TArrayLit`, `TLambda`, `TStrLit`, `TByteLit`, `TCharLit`, `TBoolLit`, `TNullLit` |
| `lower_stmt` catch-all | `lower.tks:447` | `TAssign`, `TLoopStmt`, `TBreakStmt`, `TContinueStmt`, `TDeferStmt`, `TAdoptStmt` |
| `lower_var` fn-as-value | `lower.tks:326` | function-as-value (`TVar.is_func`) |
| `lower_call` closure call | `lower.tks:380` | `TCall.is_closure_call` |
| `lower_call` iface dispatch | `lower.tks:381` | `TCall.is_iface_dispatch` |
| `bind_target_name` destructure | `lower.tks:463` | destructuring `let` |
| `lower_binding` | (n/a) | fine as-is for simple names |

### 1.3 The structural gap N1 left open (the crux of A1's cost)

Two N1 design choices are *not* extended by simply adding `match` arms — they require the walk and
the IR to grow:

1. **Single-block only.** `LowerCtx.block_id` is fixed at 0; `append_inst` always finds block 0
   (`lir.tks:172-183`); `lower_block` stops at the first terminator (`lower.tks:536-547`). Every
   control-flow construct needs **fresh block allocation** and **current-block switching** mid-walk,
   plus **block-args** on merge edges (the SSA-lite value merge). `LFunc.blocks` is already a list and
   `LJump`/`LBranch`/block-params already exist in the types — the infrastructure to *allocate and
   switch* blocks is what is missing.

2. **`LOp` is register-only — it has NO memory / aggregate / rodata / indirect-call ops.** The
   variant is `LConstInt | LConstFloat | LBin | LUn | LCall | LParam | LRet | LJump | LBranch`
   (`lir.tks:78`). There is no load, store, stack-slot, address-of-global, field-address, or
   call-through-a-register. `LModule` is functions-only (`lir.tks:97`, "Structs/globals/rodata enter
   in N2"). **A1 EXTENDS `LOp` and `LModule`** — this is explicitly sanctioned (`lir.tks:15-17`,
   `:96`): the *scalar* enums (`LType`, `LBinOp`, `LUnOp`) are frozen, but the instruction set and the
   module were always going to grow in N2. Each new `LOp` case must extend three sites in lockstep:
   `lir_print.print_op`, `lir_interp.interp_inst`, and (where value-producing) the lowering.

There is **no `.tkb`/serialization concern** for these additions: the LIR is in-memory only (lowered,
then consumed by isel); only the TAST is codec'd. So extending `LOp` touches no wire format.

---

## 2. New LIR shapes A1 introduces

All grounded in the existing enums. `LType`/`LBinOp`/`LUnOp` are **reused unchanged** — comparisons
(`ICmp*`/`FCmp*`), bitwise (`IAnd`/`IOr`/`IXor`), and shifts (`IShl`/`IShrS`/`IShrU`) are already in
`LBinOp` and only need a lowering path. The genuinely new declarations are the memory/aggregate/
indirect ops added to `LOp`, and the `LModule` growth.

### 2.1 New `LOp` cases (added across A1-3, A1-4, A1-6, A1-7)

```teko
/**
 * LAlloca — reserve a stack slot of `bytes` size and `align` alignment, yielding its address
 * as a Ptr VReg. The lowering computes bytes/align from the struct-layout table; the address
 * is the base an aggregate's field stores and loads key off.
 *
 * @since #382 A1-3
 */
pub type LAlloca = struct { bytes: u32; align: u32 }

/**
 * LFieldAddr — the address of a field inside an aggregate: `base + offset`, a Ptr VReg. `offset`
 * comes from the struct-layout table; `base` is an alloca/heap address. Also serves array/slice
 * element addressing (offset = index * stride, folded when the index is constant; a dynamic index
 * multiplies through an IMul first).
 *
 * @since #382 A1-3
 */
pub type LFieldAddr = struct { base: u32; offset: u32 }

/**
 * LLoad — load a value of machine class `ty` from address `addr` (a Ptr VReg), yielding the value.
 *
 * @since #382 A1-3
 */
pub type LLoad = struct { addr: u32; ty: LType }

/**
 * LStore — store `value` (of machine class `ty`) to address `addr` (a Ptr VReg); yields no value.
 *
 * @since #382 A1-3
 */
pub type LStore = struct { addr: u32; value: u32; ty: LType }

/**
 * LGlobalAddr — the address of a named global/rodata symbol (a Ptr VReg). String literals and
 * top-level consts materialize a rodata/global entry in LModule and reference it here; the symbol
 * is already the final linker name (the LIR never re-mangles, mirroring LCall).
 *
 * @since #382 A1-4
 */
pub type LGlobalAddr = struct { symbol: str }

/**
 * LFuncAddr — the address of a named function (a Ptr VReg): a function used as a value, and the
 * function slot of a closure literal. Distinct from LGlobalAddr so isel emits a text-section
 * relocation, not a data one.
 *
 * @since #382 A1-7
 */
pub type LFuncAddr = struct { symbol: str }

/**
 * LCallIndirect — call the function whose address is in `target` (a Ptr VReg) with `args`. The
 * vtable-dispatch path (A1-6) loads the slot address then calls it; the closure path (A1-7) calls
 * the closure's function-pointer field. `has_result`/result live on the enclosing LInst, as for
 * LCall.
 *
 * @since #382 A1-6
 */
pub type LCallIndirect = struct { target: u32; args: []u32; variadic: bool }
```

`LOp` grows to
`… | LAlloca | LFieldAddr | LLoad | LStore | LGlobalAddr | LFuncAddr | LCallIndirect`, and every one
gets an `lir.tks` builder (`alloca_inst`, `load_inst`, …) mirroring the existing `*_inst` helpers, a
`lir_print` case, and an `lir_interp` case.

### 2.2 `LModule` growth (A1-3 / A1-4)

```teko
/**
 * LRodata — one read-only data entry: its already-final symbol + the raw bytes (a string literal's
 * UTF-8, or a const aggregate's initializer image). Referenced by LGlobalAddr.
 *
 * @since #382 A1-4
 */
pub type LRodata = struct { symbol: str; bytes: []byte }

/**
 * LGlobal — one mutable/immutable top-level datum: its symbol, machine class, and initializer image.
 *
 * @since #382 A1-4
 */
pub type LGlobal = struct { symbol: str; ty: LType; bytes: []byte }

/**
 * LStructLayout — the resolved layout of one aggregate: total size, alignment, and each field's
 * byte offset in declared order (natural alignment, no reordering — matches a plain C struct so the
 * A4 C-vs-own differential holds by construction). Keyed by the Named type's canonical name.
 *
 * @since #382 A1-3
 */
pub type LStructLayout = struct { name: str; size: u32; align: u32; field_offsets: []u32 }
```

`LModule` becomes `struct { funcs: []LFunc; rodata: []LRodata; globals: []LGlobal; layouts:
[]LStructLayout }`; `empty_module`/`add_func` and the test builders extend to the new fields (default
empty). **NOTE (seed sequence):** every `.tkt` and every caller that constructs `LModule { funcs = … }`
by name must add the new fields in the SAME sub-sub-PR that widens the struct, or the seed build
breaks — grep `LModule {` before widening.

### 2.3 Fat pointers (A1-4)

A `str`/`slice` value is `{ptr, len}` — mirroring the C backend's fat pointer (`codegen.tks:3796`,
"same `{ptr,len}` layout"). In the register IR a fat pointer is **two VRegs** carried together; the
lowering threads a small `FatVReg { ptr: u32; len: u32 }` result for str/slice-typed nodes, and
whichever consumer needs only the length (e.g. `s.len`) reads `.len` directly, so no memory traffic is
emitted for a length read. Aggregate storage of a fat pointer (a struct field of str type) stores both
words at `offset` and `offset+8`.

### 2.4 Control-flow shapes (A1-1, A1-2, A1-5)

- **if-expression** lowers to: eval `cond` → `LBranch cond, #Bthen(), #Belse()`; each arm lowers into
  its block and ends `LJump #Bmerge(%v)`; `#Bmerge(%r)` takes one block-arg `%r` which IS the
  if-expr's result VReg. An if-**statement** (no value) merges with zero block-args.
- **loop** lowers to: `LJump #Bhead()`; `#Bhead` is the loop body's entry; `break`→`LJump #Bexit()`,
  `continue`→`LJump #Bhead()`. `LowerCtx` gains a **loop-target stack** (`[]LoopTargets{head, exit}`)
  so a nested/labelled break/continue resolves to the right blocks.
- **defer** lowers by **replaying the deferred body (LIFO) at every scope exit** — before each
  `return`, `break`, `continue`, and at normal fall-off (mirrors the "defer fires at every block exit"
  ruling). A `LowerCtx` **defer stack** holds the pending `[]TStatement` bodies; the exit-lowering
  helper inlines them newest-first. No new op — defer is pure re-lowering of statements.
- **match** lowers subject once, then a linear chain of test-blocks: each arm is
  `LBranch <test>, #Barm_i(), #Bnext_i()`; a `when` guard adds a second branch inside the arm block;
  wildcard/binding arms are an unconditional `LJump`. Scalar patterns (int/bool/enum-ordinal via
  `TPathExpr.value`) test with an `ICmpEq`; variant/string patterns test on a loaded tag/compare
  (needs the memory ops from A1-3/A1-4 — the reason match is sequenced after them).

---

## 3. The sub-sub-PR decomposition (ordered)

Each row is a PR **based on `fix/issue-382-lir-lowering`**, drained in order to preserve the seed
sequence; each is independently gate-able (its own `.tkt` fixtures + golden dump) and sized under the
timebox. `LIR-interp` and `lir_print` grow one case per closed honest-stop, in the same PR.

| # | Title | Depends | One-line scope |
|---|-------|---------|----------------|
| **A1-1** | control-flow core: multi-block + comparisons + `if` | — | block alloc/switch infra + block-args, `TCompare`, `if`-expr/`if`-stmt via `LBranch`/`LJump` |
| **A1-2** | `loop` / `break` / `continue` / `defer` | A1-1 | loop-target stack, break/continue jumps, defer-replay at every scope exit |
| **A1-3** | memory + aggregates + scalar leaves | A1-1 | new `LAlloca`/`LFieldAddr`/`LLoad`/`LStore` + interp memory model + `LStructLayout`; `TStructInit`/`TFieldAccess`/`TIndex`/`TArrayLit`/`TAssign`; `TByteLit`/`TCharLit`/`TBoolLit`/`TNullLit`/`TPathExpr` |
| **A1-4** | rodata + `str`/`slice` fat-pointers + interpolation | A1-3 | `LRodata`/`LGlobal`/`LGlobalAddr`, `TStrLit`, fat-pointer two-VReg threading, `.len`, `TInterp`, `TInExpr`, `TSafeFieldAccess`/`TCoalesce` |
| **A1-5** | `match` (all arms) | A1-1, A1-4 | full `TMatchExpr`/match-stmt: scalar + variant-tag + string arms, `when` guards, binding patterns |
| **A1-6** | interface dispatch (vtable load + indirect call) | A1-3 | `LCallIndirect`; load vtable slot from the receiver fat pointer, indirect call (`TCall.is_iface_dispatch`) |
| **A1-7** | closures + function-as-value | A1-3, A1-6 | `LFuncAddr` + closure `{fn, env}` literal; `TLambda`, `TVar.is_func`, `TCall.is_closure_call` via `LCallIndirect` |

Critical path: **A1-1 → A1-3 → A1-4 → {A1-5, A1-6 → A1-7}**. A1-2 hangs off A1-1 and can land any time
after it. When A1-7 merges, the branch's honest-stops are all closed and the mini-umbrella
`fix/issue-382-lir-lowering` merges into `remodel/backend-build`, closing A1 and unblocking A2.

### A1-1 · control-flow core

- **Touches:** `lir.tks` (add `alloc_block(f) -> {func, block_id}`, and make `LowerCtx`/`ctx_append`
  block-aware — already parametric on `block_id`, so mainly a block-allocator + a helper to *start*
  emitting into a new block); `lower.tks` (add `lower_compare`, `lower_if_expr`, dispatch `TIfExpr`
  in `lower_expr`; teach `lower_block` that a branch/jump also terminates); `lir_interp.tks`
  (multi-block execution: `interp_block` must follow `LJump`/`LBranch` to the target block, binding
  block-args into the target's param VRegs; add comparison arms to `eval_bin`); `lir_print.tks`
  (already complete — verify block-arg printing).
- **New shapes:** block-args as the SSA-lite merge (§2.4); `lower_compare` maps a `TCompare`
  (`first` + `rest` chain) to `ICmp*`/`FCmp*` picked by operand `PrimKind` + token, ANDing multi-term
  chains.
- **Interp cases added:** `LBranch` (read cond, pass block-args, jump), `LJump` (pass block-args,
  jump), `ICmpEq`…`ICmpGeU`/`FCmp*` in `eval_bin`. The interp gains a block-lookup + a bounded
  block-step loop (guard against a cycle with a step budget, like the depth guard).
- **Fixtures:** `lower_test.tkt` — golden dump of `if a < b { return a } return b` (assert the
  `branch`/`jump`/`#B` structure + the merge block-arg). `lir_interp_test.tkt` — F6 `exit(if x<y {x} else {y})` parity; F7 a comparison chain `a<b<c`. Golden LIR-dump fixture for the if.
- **Verifies via:** interp exit-code parity for F6/F7 + golden if-dump diff.

### A1-2 · loop / break / continue / defer

- **Touches:** `lower.tks` (`LowerCtx` gains `loop_targets: []LoopTargets` and `defers: [][]TStatement`;
  `lower_loop`, `lower_break`, `lower_continue`, `lower_defer`; an `exit_scope` helper that replays
  defers before a terminator; dispatch the four `TStatement`s in `lower_stmt`); `lir_interp.tks`
  (nothing new structurally — loops are just jumps the multi-block executor already follows once A1-1
  lands; keep the step budget). `lir_print.tks` unchanged.
- **New shapes:** loop-target stack + defer stack (§2.4). Labelled break/continue resolve by scanning
  the stack for the matching label (empty label = innermost).
- **Interp cases added:** none (jumps reuse A1-1); add a fixture-driven loop-termination step budget
  if not already present.
- **Fixtures:** `lower_test.tkt` — golden dump of a counting loop with `break`; a `defer` that runs
  before `return`. `lir_interp_test.tkt` — F8 `let s=0; loop { ...; break }; exit(s)` sums to a known
  code; F9 a `defer`-ordered side effect observable through the returned value.
- **Verifies via:** interp parity for F8/F9 + golden loop/defer dump.

### A1-3 · memory + aggregates + scalar leaves

- **Touches:** `lir.tks` (add `LAlloca`/`LFieldAddr`/`LLoad`/`LStore` to `LOp` + builders; widen
  `LModule` with `layouts`; add `LStructLayout` + a `layout_of(name, checker types)` computing
  declared-order natural-alignment offsets); `lower.tks` (dispatch `TStructInit`, `TFieldAccess`,
  `TIndex`, `TArrayLit`, `TAssign`, and the scalar leaves `TByteLit`/`TCharLit`/`TBoolLit`/`TNullLit`/
  `TPathExpr`); `lir_interp.tks` (a **memory model** — see §5.1 decision: a growable `[]i128` cell
  store keyed by address, addresses are cell indices; `LAlloca` reserves cells, `LStore`/`LLoad`
  write/read a cell, `LFieldAddr` is base+offset arithmetic over cell indices); `lir_print.tks` (print
  the four new ops + a module rodata/layout header).
- **New shapes:** §2.1 memory ops, §2.2 `LStructLayout`. Scalar leaves are trivial: `TBoolLit`/
  `TByteLit`/`TCharLit`→`LConstInt` (char = its codepoint scalar), `TNullLit`→a null Ptr const,
  `TPathExpr`→`LConstInt` of the resolved `.value`.
- **Interp cases added:** `LAlloca`, `LFieldAddr`, `LLoad`, `LStore` over the cell store.
- **Fixtures:** `lower_test.tkt` — golden dump of `struct{a;b}` init + field read; `arr[i]` index.
  `lir_interp_test.tkt` — F10 `let p = Point{x=3;y=4}; exit(p.x + p.y)` → 7; F11
  `let a = [10,20,30]; exit(a[1])` → 20; F12 a field mutation via `TAssign`.
- **Verifies via:** interp parity F10–F12 + golden aggregate dump. `lir_test.tkt` gains a
  `layout_of` unit test (offsets/size/align for a mixed-width struct).

### A1-4 · rodata + str/slice fat-pointers + interpolation

- **Touches:** `lir.tks` (add `LGlobalAddr` to `LOp`; add `LRodata`/`LGlobal` + `add_rodata`/
  `add_global`; widen `LModule`); `lower.tks` (`TStrLit`→intern rodata + materialize `{ptr,len}`;
  fat-pointer threading `FatVReg`; `.len` field read short-circuits to the len VReg; `TInterp`→a
  sequence of runtime formatter calls building a string; `TInExpr`; `TSafeFieldAccess`/`TCoalesce`→a
  null-check branch reusing A1-1); `lir_interp.tks` (a rodata byte table; `LGlobalAddr` yields a
  rodata address; string ops the fixtures need — keep fixtures exit-code-observable via `.len`/byte
  index so no `tk_println` is required, §4); `lir_print.tks` (rodata section printing + `LGlobalAddr`).
- **New shapes:** §2.2 rodata/globals, §2.3 fat pointers.
- **Interp cases added:** `LGlobalAddr` (rodata address), byte-load from rodata (reuse `LLoad`).
- **Fixtures:** `lower_test.tkt` — golden dump of `"hi"` (rodata entry + `{ptr,len}`); a `??` coalesce.
  `lir_interp_test.tkt` — F13 `exit("hello".len)` → 5; F14 `exit("abc"[0] to i32)` → 97; F15 a `??`
  fallback returning a known code.
- **Verifies via:** interp parity F13–F15 + golden rodata/str dump.

### A1-5 · match (all arms)

- **Touches:** `lower.tks` (`lower_match`: subject once, per-arm test chain, `when` guards, binding
  patterns bind the subject/payload VReg into the arm env, merge block for the expr result; handles
  scalar arms via `ICmpEq`, variant arms via a tag load + payload field addresses, string arms via a
  runtime compare); `lir_interp.tks` (nothing new structurally — reuses branches + loads); the
  existing `lwt_honest_stop_on_match_expr` test (`lower_test.tkt:243-249`) **flips** from asserting
  the honest-stop to asserting the lowered dump.
- **New shapes:** the match test-chain (§2.4). Variant tag offset comes from `LStructLayout`
  (the tag is field 0 of a variant's runtime image).
- **Interp cases added:** none (composition of A1-1 branches + A1-3/A1-4 loads).
- **Fixtures:** `lower_test.tkt` — golden dump of a 3-arm enum match with a wildcard; a variant match
  binding a payload. `lir_interp_test.tkt` — F16 `match e { A=>1; B=>2; _=>0 }` fed to `exit`; F17 a
  `when`-guarded arm; F18 a variant-payload arm returning the payload.
- **Verifies via:** interp parity F16–F18 + golden match dump + the flipped honest-stop test.

### A1-6 · interface dispatch (vtable load + indirect call)

- **Touches:** `lir.tks` (add `LCallIndirect` to `LOp` + builder); `lower.tks` (`lower_call` iface arm
  `:381`: the receiver `args[0]` is the interface fat pointer `{data, vtable}`; load the vtable ptr
  (field 1), load slot `iface_slot` (`LFieldAddr` base=vtable, offset=`iface_slot*8`; `LLoad` Ptr),
  then `LCallIndirect` with `data` prepended to the remaining args — mirrors the C backend's
  `value.vtable[slot](args)`, `codegen.tks:2058-2118`); `lir_interp.tks` (`LCallIndirect`: resolve the
  target VReg to a function via a vtable model — see §5.2; recurse like `LCall`); `lir_print.tks`
  (`LCallIndirect` as `call.indirect %t(args)`).
- **New shapes:** §2.1 `LCallIndirect`; the vtable is an rodata array of `LFuncAddr` slots
  (symbol `tk_vt_<Class>_<Base>`, matching the C symbol so A4's differential holds).
- **Interp cases added:** `LCallIndirect`; a vtable/function-pointer model (an address→symbol map
  populated when a vtable rodata entry is built).
- **Fixtures:** `lower_test.tkt` — golden dump of an interface-method call (vtable load + indirect
  call). `lir_interp_test.tkt` — F19 a two-impl interface dispatched dynamically, each returning a
  distinct exit code.
- **Verifies via:** interp parity F19 + golden iface-dispatch dump.

### A1-7 · closures + function-as-value

- **Touches:** `lir.tks` (add `LFuncAddr` to `LOp` + builder); `lower.tks` (`lower_var` fn-as-value
  arm `:326`: a bare fn → a closure literal `{fn = LFuncAddr sym, env = null}`; `TLambda` → the lifted
  fn's `LFuncAddr` + an env aggregate of the captures (`LAlloca` + field stores, reusing A1-3);
  `lower_call` closure arm `:380`: load the `fn` field, `LCallIndirect` passing the `env` — mirrors
  the C backend's `((R(*)(A,B))f.fn)(args)`); `lir_interp.tks` (`LFuncAddr` yields a function address
  the vtable/fn-pointer model resolves; closure call recurses like A1-6); `lir_print.tks`
  (`LFuncAddr` as `funcaddr @sym`).
- **New shapes:** §2.1 `LFuncAddr`; closure aggregate `{fn, env}` (layout via A1-3).
- **Interp cases added:** `LFuncAddr` (reuses A1-6's fn-pointer model).
- **Fixtures:** `lower_test.tkt` — golden dump of `let f = add; f(2,3)` and a capturing lambda.
  `lir_interp_test.tkt` — F20 a function-as-value called indirectly → known code; F21 a lambda
  capturing a local, called, returning the captured value.
- **Verifies via:** interp parity F20/F21 + golden closure dump. On merge, all honest-stops are
  closed — assert (in a final review, not a new test) that `lower_expr`/`lower_stmt` catch-alls are now
  only reachable by internal invariant breaks, not by any checker-produced node.

---

## 4. Verification method (overall)

A1 has **no machine code**, so correctness is proven two ways, both cheap and host-only:

1. **The LIR-interpreter oracle — exit-code parity vs the VM.** Each sub-sub-PR adds `iwt_*` fixtures
   (`lir_interp_test.tkt`) that build the construct as LIR (by hand or via the lowering) and assert
   the exit code `interp_lmodule` produces equals what the VM produces for the equivalent Teko source.
   **All A1 fixtures must be exit-code-observable** — `exit(n)`, a returned value, or `.len`/byte-index
   into a constant — because the interp honest-stops on `tk_println` and the other non-subset runtime
   calls (`lir_interp.tks:270-271`). Do NOT write print-based fixtures for A1.

2. **Golden LIR-dump diffs.** `lir_print.print_lmodule` is the deterministic diff spine. Each PR adds
   a golden dump (checked into the test as the expected string, the pattern `lower_test.tkt` already
   uses via `str_contains`, escalated to full-text equality for the new multi-block shapes so a stray
   block/edge is caught). A lowering divergence is caught here before any encoder exists.

New golden fixtures are needed for: the `if` merge-block shape (A1-1), the loop/defer replay (A1-2),
the aggregate alloca/field layout (A1-3), the rodata + fat-pointer form (A1-4), the match test-chain
(A1-5), the vtable-load + indirect-call sequence (A1-6), and the closure `{fn,env}` form (A1-7).

The full ritual gate (VM gate · paranoid · differential · fixpoint) runs at each sub-sub-PR's merge —
but note the differential leg here is **only** the LIR-interp-vs-VM leg; the C-native-vs-own-native
differential is born later, at A4. Fixpoint must hold because the lowering is additive to `src/` and
does not change any existing emitted artifact.

**Ritual points (where the full gate must pass):** each of A1-1 … A1-7 at its own PR merge into the
mini-umbrella; then once more at the mini-umbrella `fix/issue-382-lir-lowering` → `remodel/backend-build`
merge (A1 close), which is the seed-advancing event.

---

## 5. Recorded decisions and tensions (law-first)

None HALTs. Each is resolved against the Constitution laws and recorded so the implementer proceeds
without re-litigating.

### 5.1 The interpreter's memory model (resolved)
**Tension:** the N1 interp is pure-register (i128 cells). Running struct/slice/str fixtures needs a
memory abstraction. A byte-accurate flat `[]byte` heap (option A) is faithful to the machine but heavy;
a value-cell store (option B — address = cell index, each cell an i128, plus a separate rodata byte
table) is far simpler. **Resolution (law-first):** option **B**. The interp is an ORACLE over exit
codes, not a layout verifier (M.3 — its honest job is lowering-correctness, not ABI); real byte layout
is validated by the C-vs-own differential at A4. B keeps the oracle small and deterministic (M.1/M.2).
The `LStructLayout` offsets are still computed byte-accurately (declared-order natural alignment) so
A2+ inherits a real layout — only the *interp's* execution uses cell indices.

### 5.2 The interp's function-pointer / vtable model (resolved)
**Tension:** `LCallIndirect`/`LFuncAddr` need the interp to turn an address back into a callable. A
real machine uses a code address; the interp has none. **Resolution:** the interp keeps an
address→symbol map, populated when a vtable rodata array or an `LFuncAddr` is materialized; `LFuncAddr`
yields a synthetic address, `LCallIndirect` resolves it through the map and recurses via the existing
`find_lfunc`/`interp_call`. Deterministic (M.2), no machine code (honest for A1's scope).

### 5.3 Struct layout: who owns offsets (resolved, note for A4)
The C backend **never computed offsets** — it emitted C structs and let `cc` lay them out
(`codegen.tks:7209`). The own backend must compute them itself; A1-3's `layout_of` is genuinely new
code. **Resolution:** lay out in **declared field order with natural per-field alignment, no
reordering** — this matches a plain C struct, so when the A4 differential compares C-native to
own-native the layouts agree by construction. This is a design constraint on A1-3 (record it), not an
open question. Variant runtime image = tag (field 0) + payload; fat pointer = `{ptr@0, len@8}`;
interface/closure value = `{data/fn@0, vtable/env@8}` — all mirroring the C backend's fat-pointer
convention (`codegen.tks:549-574`).

### 5.4 `LOp` extension is sanctioned, not a re-version (resolved)
Extending `LOp`/`LModule` is the explicitly-planned N2 growth (`lir.tks:15-17`, `:96`), not a breach of
the "closed enums" design — that closure covers `LType`/`LBinOp`/`LUnOp` (the scalar contract), which
A1 does **not** touch. No `.tkb`/wire concern exists (LIR is in-memory only). Recorded so a reviewer
does not flag the new `LOp` cases as a frozen-enum violation.

### 5.5 No genuine unresolved tension
Every construct in §1.2 has a concrete lowering above; every new op has a print + interp + fixture
plan; the sequencing respects the memory-before-match / memory-before-dispatch dependencies. **A1 does
not HALT.**

## 6. Merge blocks must thread scalar RE-ASSIGNMENTS (#389 A3-loop root, 2026-07-13)

**Pinned root of the range-`for` back-edge miscompile.** The bug reported as "the range-loop
back-edge fails to thread the counter increment" is NOT in either backend's edge-copy emission —
it is an A1 **merge-lowering** defect shared by both own backends (own-native inherits it too; it is
merely MASKED there by the §6.2 loop honest-stop firing first). A `mut` scalar RE-ASSIGNED inside an
`if`/`match` arm (`lower_assign_simple` rebinds `env[name]` to a fresh SSA-lite VReg) is **dropped at
the merge**: `lower_if_stmt`/`lower_if_value`/`lower_match_*` allocate the merge block with only the
value block-arg (zero for a statement `if`), and rebuild the post-merge context from the PRE-branch
`ctx.env`. The arm-local rebind never reaches a merge block-param, so after the merge the name reads
its stale pre-branch VReg.

The range-`for` desugar `loop mut i in a..b { … }` makes this load-bearing: the per-iteration STEP
is emitted as a guarded `if !first { i = i + 1 }` at the body top (so `continue` re-runs it). The
increment IS computed, but the `if`-merge drops the `i` rebind, so the loop back-edge
(`close_loop_body` → `jump_args_for_names(env, names)`) carries the stale header `i` — the counter
freezes. Manual loops with a TOP-LEVEL `i = i + 1` are unaffected (the rebind is in the body env the
back-edge reads). Proof (own-wasm, wasmtime): `if 1==1 { x=5 }; exit(x)` → exit 0 (C-oracle 5);
`loop { …; if 1==1 { i = i+1 } }` → infinite loop (frozen `i`); the same increment top-level → exit 6.

**Fix (A1, one site-family, backend-agnostic).** At each statement-bearing merge — `lower_if_stmt`,
`lower_if_value`, and the `match` statement/value merges — thread the scalars an arm re-assigns,
mirroring `promote_env`/`jump_args_for_names` (the SAME machinery loop headers already use):

```teko
/**
 * reassigned_scalars — the enclosing-scope SCALAR names some arm of this
 * branch re-assigns (`TAssign`, `AssignKind::Simple`), so the merge must
 * carry them as block-params. A static pre-scan of the arm statement blocks
 * against `env` — computed BEFORE lowering, so the merge params can be
 * allocated up front and each arm's closing jump can supply them. An arm
 * with no such assignment contributes nothing, so an `if`/`match` that
 * mutates no enclosing scalar keeps its CURRENT zero-scalar-param merge
 * (no LIR change, no regression risk for the existing acyclic corpus).
 *
 * @param []checker::TStatement arms  every arm's statement block, flattened
 * @param LEnv env  the pre-branch scalar environment (the promotable names)
 * @return []str  the distinct enclosing scalar names re-assigned in any arm
 */
fn reassigned_scalars(arms: []checker::TStatement, env: LEnv) -> []str { … }
```

Each merge then adds one block-param per reassigned name (after any existing value param), every
arm's closing jump appends `jump_args_for_names(arm_env, names)`, and the post-merge env maps each
reassigned name to its new merge param. **No backend change is required:** the wasm stackifier's
`emit_edge_copies` (§4.3 parallel-copy) and the native isel's `emit_edge_moves` already thread
multi-param FORWARD merges (this is why the loop-carried accumulator `s` — a top-level rebind — has
always threaded correctly). A forward merge param has an ordinary forward interval, so it does NOT
trip the §6.2 loop honest-stop.

**Minimality vs. promote-all.** Promoting EVERY enclosing scalar at every merge (as loop headers do)
is also correct but adds redundant self-copies and register pressure to every `if`, changing the LIR
of the existing GREEN acyclic corpus (regression surface). The reassigned-only pre-scan keeps every
mutation-free `if`/`match` byte-identical and touches only branches that actually mutate — the
smallest correct, lowest-regression fix. **This is the load-bearing keystone fix: with it, own-wasm
== C-native on `wasm_loop_count`/`wasm_continue_step` (exit 6) with ZERO stackifier change.**
