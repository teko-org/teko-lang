---
seq: 0097
crumb-id: NAT-A1
milestone: M4
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [RM-C5]
sources:
  - "docs/design/backend-a1-lir-lowering.md:46-238"                    # the honest-stop frontier + sub-PRs
  - "docs/design/plano-mestre-0.3.1-implementacao.md:282"              # M4 NAT-A1 row
  - "docs/design/plano-mestre-0.3.1-implementacao.md:382-389"          # per-unit native pipeline
  - "src/lir/lower.tks:6014"                                           # lower_program
  - "src/lir/lower.tks:6416"                                           # lower_item
---

# 0097 · NAT-A1 — close the LIR lowering coverage frontier

> Close the TAST→LIR lowering frontier: every construct the checker produces lowers (no honest-stop
> catch-all reachable by a real node) — new `LOp` cases (memory/aggregate/rodata/indirect), `LModule`
> growth (rodata/globals/layouts), fat pointers, and full control flow (`if`/`loop`/`defer`/`match`).

## Goal

The native route's first leg: the compiler's own `lower_program`/`lower_item` (`lower.tks:6014`/`:6416`)
must lower EVERY construct in `src/` to LIR, so the isel/regalloc/encode/emit tail (NAT-A2..A4/B1/B3) has
a complete `LModule` to consume. The frontier is the set of honest-stops the single-block register-only
bring-up left: control flow (`if`/`loop`/`break`/`continue`/`defer`/`match`), memory/aggregates
(`LAlloca`/`LFieldAddr`/`LLoad`/`LStore` + `LStructLayout`), rodata + `str`/`slice` fat-pointers
(`LRodata`/`LGlobal`/`LGlobalAddr`, two-VReg threading), interface dispatch + closures (`LCallIndirect`/
`LFuncAddr`). This is **byte-preserving on the C route** (lowering is additive to `src/`; the emitted C
artifact does not change — the LIR is in-memory only, consumed by isel, no `.tkb`/wire concern) and it
drives a **fixpoint-rebuild** (the compiler rebuilds on itself; byte-identity holds because no emitted
artifact moves). `LOp`/`LModule` growth is the explicitly-sanctioned N2 growth — the FROZEN closure
covers `LType`/`LBinOp`/`LUnOp` (the scalar contract), which A1 does not touch.

## Where

- `src/lir/lower.tks` — the frontier closers: the `lower_expr` catch-all and the `lower_stmt` catch-all
  are the two sites every new construct must clear (`backend-a1:50-51`). Extend `lower_expr`/`lower_stmt`
  dispatch, add `lower_compare`/`lower_if_expr`/`lower_loop`/`lower_defer`/`lower_match`/`lower_call`
  iface+closure arms; `LowerCtx` gains a block allocator + loop-target stack + defer stack.
- `src/lir/lir.tks` — extend `LOp` with `LAlloca | LFieldAddr | LLoad | LStore | LGlobalAddr | LFuncAddr |
  LCallIndirect` + their `*_inst` builders; grow `LModule` to `{ funcs; rodata; globals; layouts }`; add
  `LRodata`/`LGlobal`/`LStructLayout` + `layout_of` (declared-order natural alignment).
- `src/lir/lir_print.tks` — one print case per new `LOp` + the rodata/global/layout module header
  (the deterministic dump spine).
- `src/lir/lower.tks:6014` `lower_program` / `:6416` `lower_item` — the entry points that must return a
  complete `LModule` with no reachable honest-stop.

Every new `LOp` case extends three sites in lockstep: `lir_print.print_op`, the lowering, and (where the
oracle survives) its case — but the differential oracles are RETIRED (`backend-a1` NOTA); the frontier is
now proven by the self-build + the native CI legs, not a standalone oracle.

## How

Close the frontier in the dependency order the source doc fixes (`backend-a1:230-242`): control-flow core
→ memory/aggregates → rodata/fat-pointers → {match, iface-dispatch → closures}; loop/defer hangs off
control-flow core.

1. **Control-flow core** — block alloc/switch infra + block-args (the SSA-lite merge), `TCompare`→
   `ICmp*`/`FCmp*`, `if`-expr/`if`-stmt via `LBranch`/`LJump`. Merges thread reassigned scalars by
   pre-branch INDEX identity (the #389 F1b keystone, `backend-a1:545-795`) — the merge carries one param
   per enclosing scalar an arm re-assigns, read at its fixed pre-branch index, immune to shadows.
2. **loop/break/continue/defer** — loop-target stack; defer REPLAYS the deferred body LIFO at every scope
   exit (before each `return`/`break`/`continue` and at fall-off); no new op.
3. **memory + aggregates** — `LAlloca`/`LFieldAddr`/`LLoad`/`LStore` + `LStructLayout` (declared-order,
   natural per-field alignment, NO reordering — matches a plain C struct so the layout agrees with the C
   route by construction). Scalar leaves (`TByteLit`/`TCharLit`/`TBoolLit`/`TNullLit`/`TPathExpr`).

```teko
/**
 * layout_of — the resolved layout of one aggregate: total size, alignment, and each field's byte offset
 * in DECLARED order with natural per-field alignment and NO reordering, so the own-backend layout matches
 * a plain C struct (the C-vs-own layouts agree by construction). Keyed by the Named type's canonical name.
 *
 * @param name   the aggregate's canonical name
 * @param table  the checker type table supplying field types/widths
 * @return       the size/align/field-offset layout, or an error on an unresolved field type
 * @throws       when a field type has no known machine width
 * @since 0.3.1
 */
fn layout_of(name: str, table: checker::TypeTable): LStructLayout | error
```

4. **rodata + str/slice fat-pointers** — `LRodata`/`LGlobal`/`LGlobalAddr`; a `str`/`slice` is TWO VRegs
   (`{ptr,len}`) threaded via a `FatVReg` side-table, never promoted through block-args; `.len` reads the
   len VReg directly (no memory traffic). Aggregate storage of a fat pointer stores both words at `offset`
   and `offset+8` (`{ptr@0, len@8}`).
5. **match** — subject once, per-arm test chain (`ICmpEq` scalar / loaded-tag variant / runtime-compare
   string), `when` guards, binding patterns, merge for the expr result.
6. **iface dispatch + closures** — `LCallIndirect` (load vtable slot from the receiver fat pointer, indirect
   call) + `LFuncAddr` (function-as-value, closure `{fn, env}` literal). Symbols are the FINAL linker names
   (the LIR never re-mangles), matching the C symbols so the differential holds.
7. **Confirm no reachable catch-all**: after the frontier closes, `lower_expr`/`lower_stmt` catch-alls are
   only reachable by an internal invariant break, never by a checker-produced node — a final review, not a
   fixture.
8. **Rebuild + fixpoint**: the lowering is additive; the emitted C artifact is unchanged; `gen2==gen3`
   holds. **Seed sequence:** every `.tkt`/caller constructing `LModule { funcs = … }` by name MUST add the
   new fields in the SAME step that widens the struct — grep `LModule {` before widening or the seed build
   breaks.

## Rulings & laws

- **Teko-only:** `src/lir/*.tks`; no C twin. LIR is in-memory only — no `.tkb`/wire concern.
- **W15 full Javadoc** on every new `LOp`/type/fn; flatten with early-return guards; no `//`.
- **`LOp`/`LModule` growth is SANCTIONED** (the N2 growth, `lir.tks:15-17,:96`), NOT a frozen-enum breach
  — the frozen closure is `LType`/`LBinOp`/`LUnOp`, untouched here.
- **Layout parity:** declared-order natural alignment, no reordering, so own == C layout by construction
  (`backend-a1:422-430`).
- **Merge-threading keystone (#389 F1b):** reassigned scalars threaded by pre-branch index identity —
  over-thread safely, no coarse shadow exclusion (`backend-a1:545-795`).
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit each closed
  sub-frontier; this is a `[fixpoint]` — `gen2==gen3` byte-identical (no emitted artifact moves); sweep
  `.tkt` after each `LModule`/`LOp` widening.

## Fixtures

none — the fixpoint self-build exercises this. The compiler lowers its OWN full corpus (every construct
in `src/` — control flow, aggregates, str/slice, match, dispatch, closures) to LIR on the native route;
the native CI legs run that LIR. Per the owner ruling "backend native — REMOVER: o CI exercita" and the
retirement of the differential oracles (`backend-a1` NOTA), the frontier is proven by the self-build +
the native CI legs, not by standalone `.tkr`/golden fixtures.

## Gate

`[fixpoint]` — build gen2 + scoped regression + `gen2==gen3` byte-identity (the lowering is additive; the
emitted C artifact does not change). "Green" = no `lower_expr`/`lower_stmt` catch-all is reachable by a
checker-produced node, the full corpus lowers to a complete `LModule`, and the rebuild is byte-identical.
Reseed-class: `fixpoint-rebuild`.

## Deps

`RM-C5` — verbatim from 000-INDEX (the memory-reduction leg the LIR frontier rests on).

## Done when

Every construct the checker produces lowers to LIR (no reachable honest-stop catch-all), `LOp`/`LModule`
carry the memory/aggregate/rodata/indirect shapes, fat pointers + full control flow lower, and the
`gen2==gen3` rebuild is byte-identical.
