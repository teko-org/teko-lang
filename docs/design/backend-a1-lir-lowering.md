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

## 6.1 F1b — the SAME-CLASS merge-drop residuals F1's review surfaced (#389, 2026-07-13)

F1 fixed the plain-`if c { x = 5 }` case, but its adversarial review found three MORE arm-merge sites
that dropped an enclosing scalar the SAME way — each a latent SILENT miscompile (own-backend != C).
Two are TRACTABLE and fixed here; the third needs a scope-aware `lenv` rework and is routed to the
architect.

**Item 1 — defer-write-propagation (FIXED).** A `defer { s = s + 100 }` inside an if/match STATEMENT
arm reassigns the enclosing `mut s`, but the arm's closing jump-args were sampled from its env BEFORE
`close_arm_replaying_defers` replayed the defer (which mutates `s`), so the merge received the
PRE-defer `s`. Proof: `mut s=1; match x { 1 => { defer { s = s+100 } }; _ => {} }; exit(s)` → C 101,
own (pre-F1b) 1. Fix: `close_arm_replaying_defers` now takes the reassigned `names` and re-samples
their jump-args from the POST-`replay_defers` env, appended AFTER the arm's PRE-replay `value_args`
(the value block-arg stays pre-defer — a VALUE arm's merge value must remain the pre-defer tail, per
the `lwt_defer_inside_*_value_arm` goldens). The `TDeferStmt` descent F1 removed from the
reassigned-scalar scan (a half-fix that would have minted a stale param) is RE-ADDED, now a real fix
because the args are sampled post-replay. Fixture: `own_defer_arm_write_propagates` (exit 101).

**Item 2 — value-if / value-match RHS reassignment (FIXED).** `if outer { let y = if inner { x = 5;
x } else { 0 } }; exit(x)` dropped `x = 5` at the OUTER statement merge: the reassigned-scalar scan
had no `TBinding` case and did not descend a value-position if/match RHS, so the outer merge never
saw `x` (the inner value-if threads `x` through its OWN merge, but the outer merge past the binding
must carry it too). Proof: C 5, own (pre-F1b) 1. Fix: `collect_reassigned_stmt` gains a `TBinding`
case that descends the binding's value-position `if`/`match` RHS (`collect_reassigned_value_expr`);
the shadow-exclusion `collect_bound_stmt` gains the symmetric RHS descent so a shadow inside the same
RHS is still excluded. Fixture: `own_value_if_rhs_reassign` (exit 5).

**Item 3 — across-all-arms over-exclusion (ROUTED TO ARCHITECT).** F1's shadow-exclusion guard is
across-ALL-arms: if a name is legitimately reassigned as the enclosing scalar in one arm BUT shadowed
in another, it is excluded for BOTH, so the legitimate arm reverts to the drop. Proof: `mut x=1;
match k { A => { x = 5 }; _ => { mut x=99; x=x+1 } }; exit(x)` → on the A path C 5, own 1.

*Assessment.* A PER-ARM fix — thread the name for ALL arms, but have a SHADOWING arm supply the
ENCLOSING binding's PRE-branch VReg (skipping its own shadow) instead of excluding the name globally
— is *partially* tractable and strictly non-regressing (it fixes the shadow-ONLY arm and leaves the
shadow arm's own merge-arg exactly as today). BUT it is NOT a complete fix: an arm that reassigns the
enclosing scalar BEFORE shadowing it (`_ => { x = 3; mut x = 99; … }`) needs the enclosing's LAST
pre-shadow VReg, which an append-only newest-first `lenv` cannot recover without scope markers — that
residual stays a silent stale-read. It also INVERTS F1's shadow-exclusion model (Finding 2), changing
the currently-GREEN shadow goldens (`lwt_if_stmt_shadowed_scalar_excluded_from_merge`,
`own_if_mut_shadow_no_leak`) from zero-param to threaded merges, and bloats the per-arm arg threading
that items 1+2 already grow. A COMPLETE, sound fix requires a scope-aware `lenv` (the reviewer's own
judgment). Rather than land an incomplete fix that destabilizes the merge model alongside items 1+2,
item 3 is reported for architect routing.

## 6.2 F1b item 3 — the across-all-arms over-exclusion: the enclosing-identity fix (#389, 2026-07-13)

**Status:** DESIGN (doc-only), owner-ruled fix-now before #389 closes. Branch
`design/389-f1b-item3-across-arms` off `fix/issue-389-c1-8-keystone@a1c8572`; implementation PRs land
into `fix/issue-389-c1-8-keystone`. This is the LAST same-class merge-drop residual; it eliminates the
`_ => { x = 3; mut x = 99; … }` reassign-then-shadow hard case the F1b review flagged unrecoverable.

### 6.2.1 Root, restated as an IDENTITY problem

Item 3 is not really "over-exclusion" — that is the symptom. The root is that F1's merge threading
keys each arm's jump-arg by NAME, resolved newest-first (`jump_args_for_names` → `lenv_lookup`). When
an arm shadows a threaded name (`mut x = 99`, or a pattern `A as x`), newest-first resolves the arm's
closing jump-arg to the SHADOW's VReg, not the enclosing scalar's — a miscompile. F1 defended against
this the only way a name-keyed model can: EXCLUDE any name shadowed in ANY arm
(`collect_bound_stmts`/`collect_match_pattern_bound_names`/`exclude_names`). That exclusion is
across-ALL-arms and coarse: a name legitimately reassigned as the ENCLOSING scalar in one arm but
shadowed in another is excluded for BOTH, reverting the legitimate arm to the merge-drop.

The clean fix keys each arm's jump-arg by the enclosing binding's IDENTITY, not its name. Then a
shadow is simply a different identity the merge ignores — no exclusion is needed at all, and the
threading decision can safely OVER-approximate (thread every reassigned pre-branch scalar), because a
name no arm reassigns-at-enclosing threads its pre-branch VReg on every edge — a no-op phi, never a
miscompile.

### 6.2.2 Chosen mechanism (evaluated against the three routed options)

The routing offered (a) scope-DEPTH markers on `lenv`, (b) a per-arm pre-branch env SNAPSHOT, and
(c) binding-IDENTITY keys. The chosen mechanism is **(c), realized as pre-branch-INDEX identity made
recoverable by reassign-in-place** — the smallest change that is also COMPLETE:

1. **Reassign-in-place (SSA-lite reassignment updates the enclosing binding's current VReg, instead of
   appending a duplicate `lenv` entry).** `lower_assign_simple` is the ONLY reassignment site (the
   `=` path at `lower.tks:4325`, the `op=` path at `:4335`); every other `ctx_bind`/`lenv_bind`
   (`lower_binding` `:1659`, the pattern binds `:3375/:3403/:3476`, lambda `:1499/:1529`, promote/rebind
   `:1787/:2355`, params `:4516`) is a FRESH lexical binding and correctly keeps appending. Switching
   only that one site so `x = e` REBINDS the newest `x` entry's VReg in place — rather than pushing a
   second `x` entry — is **behavior-preserving for every existing lookup**: `lenv_lookup`/
   `lenv_lookup_fat` are newest-first, and after an in-place update the newest `x` is at its original
   index carrying the new VReg, so every read returns the SAME VReg it does today. The ONLY structural
   effect is that a reassigned scalar no longer leaves a DUPLICATE `lenv` entry. That matters solely to
   the two functions that read `lenv` POSITIONALLY: `promote_env` (loop heads) allocates one param per
   scalar ENTRY, and index-keyed arm args (below) read a specific slot. `add_merge_scalar_params` is
   per-NAME (deduped), so `if`/`match` merges are unaffected regardless.

2. **Index-identity: each threaded name's merge-arg per arm is read from the arm-end env at the name's
   PRE-BRANCH INDEX**, not by newest-first name. The pre-branch env (`ctx.env` at the merge site) holds
   each threaded enclosing scalar exactly once, at a fixed index `j`. Every arm is lowered starting from
   `ctx.env` and thereafter only APPENDS (shadows, pattern binds, `let`/`mut`) or UPDATES-IN-PLACE
   (reassignments) — it never shrinks or reorders indices `0..pre_branch_len`. So `arm_env.vregs[j]` is
   always the enclosing binding's current value at the arm's close: the reassigned VReg if the arm
   reassigned the enclosing scalar, the untouched pre-branch VReg if it only shadowed it (the shadow
   sits at a HIGHER index the read ignores), and correct even THROUGH a nested inner merge (which
   rebuilds the env in order via `rebind_scalars`, preserving index `j` while updating its VReg to the
   inner merge param).

3. **Drop the coarse exclusion; over-thread safely.** `reassigned_scalars` loses its
   `!contains_str(bound, …)` term; `reassigned_scalars_for_match` collapses to
   `reassigned_scalars(match_arm_stmts(arms), env)` (no pattern exclusion). A name is threaded whenever
   ANY arm has a simple `TAssign` to it AND it is a pre-branch scalar. Index-identity makes this
   provably safe (§6.2.1). The shadow-exclusion helpers become dead and are removed:
   `collect_bound_stmts`/`collect_bound_stmt`/`collect_bound_value_expr`/`collect_bound_expr_stmt`/
   `collect_bound_if`/`collect_bound_match`, `pattern_bound_names`/`collect_alt_pattern_names`/
   `collect_match_pattern_bound_names`, `bind_target_names`, `exclude_names`, and `append_all_str`
   if no other caller remains (grep before deleting; `append_all_u32` stays — item 1 uses it).

**Why not (a) scope-depth or (b) snapshot.** (a) needs a NEW `lenv` field (a depth per binding) AND a
threaded depth counter, and still cannot by itself distinguish "reassigned the enclosing x" from "only
shadowed x" without reassign-in-place — depth alone tells you WHERE a binding lives, not whether the
enclosing one's VALUE moved. (b) a pre-branch snapshot recovers the pre-branch value for a shadowing
arm but CANNOT recover the enclosing scalar's LAST pre-shadow value in the reassign-THEN-shadow arm
(`x = 3; mut x = 99; …`) — it would thread the pre-branch value (wrong) where the answer is `3`. Only
tracking the enclosing binding's evolving value THROUGH the shadow (reassign-in-place) solves the hard
case, and index-identity is the read side of exactly that. (c) as a stable-ID FIELD would also work but
costs a new `ids: []u32` slice on `LEnv` plus id-preservation plumbing through `rebind_scalars`/
`promote_env`; index-identity needs neither — the pre-branch position IS the stable id, for free.

### 6.2.3 The per-arm value-selection rule

For a threaded name `n` at pre-branch index `j`, each arm supplies `arm_env.vregs[j]`. That single rule
yields, by construction:

| The arm… | `arm_env.vregs[j]` holds | threads | correct? |
|----------|--------------------------|---------|----------|
| reassigns the enclosing `n` (no shadow) | the reassigned VReg | new value | yes |
| only shadows `n` (`mut`/pattern) | the untouched pre-branch VReg (shadow is at a higher index) | pre-branch | yes |
| does neither | the untouched pre-branch VReg | pre-branch | yes |
| reassigns enclosing `n`, THEN shadows (`n = 3; mut n = 99; …`) | the LAST pre-shadow reassigned VReg (`3`) — the shadow appended a new index; the later `n = …` in-place-updates the SHADOW's index, never `j` | `3` | **yes — the hard case** |
| shadows `n`, THEN reassigns the shadow (`mut n = 99; n = n+1`) | the untouched pre-branch VReg | pre-branch | yes |

The reassign-then-shadow hard case is ELIMINATED, not a residual: `n = 3` in-place-updates index `j`
to VReg(3); `mut n = 99` appends `n` at a new index `k > j`; the later shadow reassignment updates `k`,
leaving `j` at VReg(3); the merge reads `j` → `3`. This is precisely the C behavior (the enclosing
`n`'s last value before it goes out of view is `3`).

### 6.2.4 Exact type / signature changes (all Teko, full-Javadoc; implementer copies verbatim)

`LEnv` is UNCHANGED (no new field). New/changed function shapes:

```teko
/**
 * lenv_reassign — rebind the NEWEST binding of `name` to `vreg` IN PLACE (the
 * SSA-lite reassignment update), preserving that slot's fat-pointer side-table
 * (`has_len`/`len_vregs`) and, crucially, its POSITION — so the binding keeps
 * its pre-branch INDEX and the merge machinery can read the enclosing scalar's
 * current value at that fixed index regardless of any shadow appended after it
 * (#389 F1b item 3). Behaviour-preserving for every `lenv_lookup`: the newest
 * `name` is still at its original index, now carrying `vreg`, so a name-keyed
 * newest-first read returns the SAME VReg it would after the old append — the
 * only difference is that no DUPLICATE `name` entry is left behind. Falls back
 * to `lenv_bind` when `name` is unbound (a checker invariant break — an assign
 * to an undeclared local never reaches lowering).
 *
 * @param LEnv env  the env whose newest `name` binding is updated
 * @param str name  the reassigned local's name
 * @param u32 vreg  the reassignment's fresh result VReg
 * @return LEnv  `env` with the newest `name` slot's VReg replaced in place
 */
pub fn lenv_reassign(env: LEnv, name: str, vreg: u32) -> LEnv { … }
```

`lower_assign_simple` (`lower.tks:4322`) swaps its two `ctx_bind` calls for a `ctx_reassign` wrapper
over `lenv_reassign` (both the `=` path `:4325` and the `op=` path `:4335`). `lower_binding` and the
pattern binds are UNTOUCHED (they must keep appending — that IS the shadow).

```teko
/**
 * merge_scalar_indices — the pre-branch INDEX of each threaded name in `env`
 * (its newest occurrence), in `names` order — computed ONCE at a merge site
 * from the PRE-BRANCH env so every arm reads its enclosing scalar's value from
 * the same fixed slot, immune to any shadow the arm appends (#389 F1b item 3).
 *
 * @param LEnv env  the pre-branch env (each threaded name occurs once)
 * @param []str names  the threaded scalar names, in merge-param order
 * @return []u64  each name's pre-branch index, in the SAME order as `names`
 */
fn merge_scalar_indices(env: LEnv, names: []str) -> []u64 { … }

/**
 * arm_scalar_args — one jump-arg per threaded scalar, read from `arm_env` at the
 * fixed pre-branch `indices` — the ENCLOSING binding's current value at the
 * arm's close (reassigned if the arm reassigned it, pre-branch if the arm only
 * shadowed it), REPLACING `jump_args_for_names`'s newest-first name lookup for
 * if/match arm merges (#389 F1b item 3). Loops keep `jump_args_for_names` (their
 * back-edge threads ALL promoted scalars by name, no enclosing-vs-shadow split).
 *
 * @param LEnv arm_env  the arm-end env (indices 0..pre_branch_len unchanged in position)
 * @param []u64 indices  the threaded scalars' pre-branch indices, in param order
 * @return []u32  each index's current VReg, in the SAME order as `indices`
 */
fn arm_scalar_args(arm_env: LEnv, indices: []u64) -> []u32 { … }
```

`close_arm_replaying_defers` (`:2697`) swaps its `names: []str` param for `indices: []u64` and computes
`append_all_u32(value_args, arm_scalar_args(replayed.env, indices))` — still POST-`replay_defers`, so
item 1's defer-write propagation composes unchanged (a `defer { s = s + 100 }` in-place-updates `s` at
its index; the post-replay index read sees the post-defer value). The four arm-lowering fns
(`lower_if_stmt` `:2782`, `lower_if_value` `:2811`, `lower_match_arm_stmt` `:3691`,
`lower_match_arm_value` `:3585`) compute `let indices = merge_scalar_indices(ctx.env, names)` alongside
`names` and pass `indices` down; the statement/value merge fallthroughs (`lower_match_stmt` `:3754`,
`lower_match_value` `:3657`) swap `jump_args_for_names(chain.env, names)` for
`arm_scalar_args(chain.env, indices)` (the fallthrough env restored to pre-branch preserves the same
indices). `reassigned_scalars` (`:2257`) drops its `bound`/exclusion term;
`reassigned_scalars_for_match` (`:2307`) becomes a one-liner delegating to it.

### 6.2.5 Composition with F1 / F1b (traced, and the regressions that stay green)

- **F1's own fixtures stay green.** `own_if_reassign_exit` (`x = 5` at top level): index-in-place, arm
  reads index 0 → `%5`, else → `%0`; identical single-param merge (golden
  `lwt_if_stmt_reassigned_scalar_threads_a_merge_param` UNCHANGED). `own_if_mut_shadow_no_leak`
  (`mut x=99; x=x+1`, pure shadow): now threaded, but the then arm's `x=x+1` updates the SHADOW's index,
  index 0 stays `%0`, both edges carry `%0`, `ret` reads the merge param = `%0` → exit 1 UNCHANGED (the
  golden `lwt_if_stmt_shadowed_scalar_excluded_from_merge` MOVES from zero-param to a no-op one-param
  merge — an internal-golden update, exit code identical). `own_match_pattern_binding_no_collision`
  (Circle path, `Circle as x` empty body): index 0 untouched → merge carries pre-branch → exit 1
  UNCHANGED; the previously-latent Square path (`Square => { x = 5 }`) is now CORRECT (was silently
  dropped) — proven by a new fixture.
- **F1b item 1 (defer) composes.** `own_defer_arm_write_propagates`: `arm_scalar_args` reads POST-replay,
  the defer's in-place update to `s` is seen → exit 101 UNCHANGED.
- **F1b item 2 (value-if RHS) composes.** `own_value_if_rhs_reassign` and its golden
  `lwt_if_stmt_value_if_rhs_reassign_threads_the_outer_merge` are byte-identical under the new mechanism
  (traced: inner value-if threads `x` through its own merge → outer-arm index 0 = inner merge param
  `%9` → outer merge `jump #B3(%9)`; the F1b NIT — `let x = 99` shadowing inside the RHS — is fixed as a
  CONSEQUENCE, since the enclosing `x = 7` reassigns index 0 while the inner `let x` lives at a higher
  index). This is the exact shape the review flagged; it needs no separate handling.
- **`lwt_if_stmt_non_mutating_arm_keeps_zero_param_merge` stays green** (no assign → `names` empty →
  zero-param merge, byte-identical).

### 6.2.6 Regression fixtures (own == C, both VM-oracle exit and own-native/own-wasm binary)

New `examples/regressions/` binary fixtures (kind = "binary", `scripts/validate_wasm_own.sh`,
own-native/own-wasm exit == C-native exit):

1. `own_match_arm_reassign_vs_shadow` — shape (1). `mut x = 1; match k { 1 => { x = 5 }; _ => { mut x =
   99; x = x + 1 } }; exit(x)`. Two variants: `k = 1` → exit **5** (the arm that reassigns the enclosing
   `x`, the previously-dropped path), `k = 0` → exit **1** (the shadow-only arm threads pre-branch).
2. `own_if_value_rhs_shadow_then_outer_reassign` — shape (2), the F1b NIT. `mut x = 1; if 1 == 1 { let y
   = if 1 == 1 { let x = 99; x } else { 0 }; x = 7 }; exit(x + y - y)` → exit **7** (inner value-if
   RHS shadows `x` via `let x = 99`; the outer arm's `x = 7` reassigns the enclosing `x`; pre-fix this
   dropped `x = 7`, giving 1).
3. `own_match_reassign_then_shadow` — the hard case. `mut x = 1; match k { 1 => { x = 3; mut x = 99; x =
   x + 1 }; _ => { } }; exit(x)`, `k = 1` → exit **3** (the enclosing `x`'s last value is `3`, before
   the shadow; own must equal C = 3, NOT the shadow's 100 and NOT the pre-branch 1).

Golden LIR-dump fixtures in `src/lir/lower_test.tkt`:

- UPDATE `lwt_if_stmt_shadowed_scalar_excluded_from_merge` → `…_threads_pre_branch_value`: assert the
  merge now carries an `x` param whose every incoming edge is the pre-branch VReg (`jump #B3(%0)` on
  both), and the read past the merge is the merge param (`ret %4`) — value still `1`.
- UPDATE `lwt_match_stmt_pattern_binding_excludes_one_name_but_still_threads_another` →
  `…_threads_both_the_reassigned_and_the_pattern_shadowed_name`: assert TWO scalar params (`x`, `y`);
  the pattern arm (`A as x`, empty) carries pre-branch `x` and pre-branch `y`; the wildcard arm carries
  reassigned `x` and reassigned `y` (this update fixes the latent wildcard-path miscompile the old
  golden enshrined).
- ADD `lwt_if_stmt_reassign_then_shadow_threads_the_pre_shadow_enclosing_value`: the hard case at LIR
  level — the merge `x` param's arm edge carries the `x = 3` VReg, never the `mut x = 99`/`x = x + 1`
  shadow VRegs.
- ADD `lwt_match_arm_reassign_vs_shadow_threads_per_arm`: arm A's closing jump carries the reassigned
  VReg; the shadow arm's closing jump carries the pre-branch VReg — per-arm selection, one param.

### 6.2.7 Ritual points

Doc-only design PR: no ritual gate beyond a clean build (it emits no code). The IMPLEMENTATION carries
the gate. Split into two crumbs, each a ritual point (full gate — VM gate · paranoid · differential
(LIR-interp-vs-VM over the runnable subset AND own-vs-C binary for the new regressions) · fixpoint) at
its merge into `fix/issue-389-c1-8-keystone`:

- **Crumb 1 (reassign-in-place, pure refactor):** add `lenv_reassign`; switch `lower_assign_simple`'s
  two sites to it. Nothing else changes — exclusion and name-keyed jump-args stay. Gate proves the
  ENTIRE existing golden + regression suite is byte-identical (no golden depended on reassignment
  leaving a duplicate `lenv` entry — confirmed by inspection: the only positional reader that could
  shrink is `promote_env`, and no existing golden has a reassign-then-nested-`loop` shape). This is the
  bisection anchor: if anything moves, it moves HERE, in isolation.
- **Crumb 2 (index-identity threading + drop exclusion):** add `merge_scalar_indices`/`arm_scalar_args`;
  thread `indices` through `close_arm_replaying_defers` and the four arm-lowering fns + the two
  fallthroughs; drop the exclusion term in `reassigned_scalars`/`reassigned_scalars_for_match`; remove
  the now-dead shadow-exclusion helpers; update the two goldens; add the two new goldens; add the three
  binary regressions. Gate proves item 3 fixed with own == C on all three shapes and no F1/F1b
  regression.

### 6.2.8 FIXPOINT safety, residual, law check

- **FIXPOINT-safe.** `lower.tks` is own-backend-only (TAST → in-memory LIR consumed by isel; no `.tkb`/
  wire format — §1.3, §5.4). The change is deterministic, so the self-hosted rebuild is stable; the
  existing GREEN corpus's emitted LIR is byte-identical except the two intentionally-updated goldens
  (whose exit-code behavior is unchanged). Fixpoint (self-compilation stability) holds.
- **Residual: NONE.** The reassign-then-shadow hard case is eliminated (§6.2.3). Over-threading a
  pure-shadow name is a no-op phi, never a miscompile. No same-class merge-drop shape remains open.
- **Law check (no HALT).** M.1/M.3 honesty preserved (every construct still lowers or honest-stops;
  `lenv_reassign` falls back safely). Smallest-safe-steps satisfied by the two-crumb split. Full-Javadoc
  on every new/changed declaration. Seed-safe: no new language feature, no wire change, `LEnv` shape
  unchanged. Nothing HALTs — this closes item 3 and, with it, the F1 keystone's last core defect.
