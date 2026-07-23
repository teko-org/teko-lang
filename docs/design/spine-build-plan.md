# Spine BUILD plan — the bounded points-to/uniqueness fact + the three L2 gates (issue #331)

**Status:** BUILD PLAN, doc-only. Sub-branch of `remodel/memory-unsafe-backend` (#329).
Consumes the #330 recon verdict (`docs/design/spine-layer-or-replace.md`, MERGED into #329):
**LAYER — additive `fn_spine` query beside the untouched `fn_escaping_vars` name set.**

This document turns that verdict into an ORDERED, gate-able crumb sequence. It writes NO product
code; every snippet below is already in W15 full-Javadoc style so the implementer copies it verbatim.

**Line anchors in this doc are re-anchored to the CURRENT `remodel/memory-unsafe-backend` tree**
(the recon cited pre-remodel lines; the remodel branch shifted them). The authoritative sites, as of
this plan:

| Gate | Recon cite | **CURRENT (remodel) site** | Verdict |
|---|---|---|---|
| R5 — ref bound to a local (binding) | `typer.tks:2450` | **`src/checker/typer.tks:2808`** (`type_binding`) | RELAX (candidate) |
| R5 — ref bound to a local (assign) | `typer.tks:2553` | **`src/checker/typer.tks:2911`** (`type_assign`) | RELAX (candidate) |
| — ref stored in a field | `typer.tks:2512` | **`src/checker/typer.tks:2870`** (`type_field_assign`) | RELAX (narrow, guarded) |
| R3 — function returns a ref | `typer.tks:3169` | **`src/checker/typer.tks:3456`** AND **`:3527`** | **KEEP FOREVER** |
| `mem::free` affine target | `typer.tks:404` | **`src/checker/typer.tks:418`** (`type_mem_free`) | RELAX (new `us=unique` check) |
| R4 — ref as collection element | `resolve.tks:1168` | **`src/checker/resolve.tks:1187`** | KEEP |
| R4 — ref as variant member | `resolve.tks:1185` | **`src/checker/resolve.tks:1204`** | KEEP |
| R4 — ref as fn-type param | `resolve.tks:1213` | **`src/checker/resolve.tks:1232`** | KEEP |
| R2 — ref nullable | `resolve.tks:1233` | **`src/checker/resolve.tks:1252`** | KEEP |
| B — ref as generic arg (inferred) | `resolve.tks:1113` | **`src/checker/resolve.tks:1132`** | KEEP |
| B — ref as generic arg (written) | `resolve.tks:1403` | **`src/checker/resolve.tks:1422`** | KEEP |
| — `Ref<Ref<T>>` | — | **`src/checker/resolve.tks:1405`** | KEEP |

> **Implementer note:** re-grep before touching. The messages are stable strings; grep by message
> text (`"cannot be bound to a local"`, `"cannot be stored in a field"`, `"cannot return a
> reference"`, `"cannot be stored in a struct/variant/collection"`, `"cannot be nullable"`) — the
> string is the durable anchor, the line number is not.

---

## 0. Prior art this BUILD sits on (do not re-derive)

Issue **#336** already shipped, on this branch, a **LOCAL, per-binding consume-or-fail dataflow** for
`#must_free` (`src/checker/typer.tks:2222` `must_free_consumed_on_all_paths` and helpers
`:2266` `stmt_diverges_standalone`, `:2295` `stmt_consumes_must_free`, `:2318` `defer_frees`,
`:2334` `texpr_is_must_free_var`, `:2350` `texpr_is_mem_free_call`). Its own doc-comment
(`typer.tks:2185`, HONEST BOUNDS) names FOUR gaps it cannot close and hands them to **`#331`** by
number:

1. the **ALIASED-FREE UAF** (`y = x; free(x); use(y)`) — **L2b of this plan**;
2. a `#must_free` binding inside a `loop { }` body (never walked);
3. a param consumed by passing it to a callee (needs move-tracking) — **L2c of this plan**;
4. `mut h = make(); h = make()` (reassign-before-free leak).

The spine's `us` (unique-vs-shared) axis is the fact that closes (1) and (3). This BUILD **reuses**
`#336`'s `texpr_is_mem_free_call` / `texpr_is_must_free_var` recognizers verbatim (they already
recognize the `mem::free(name)` and bare-`name` shapes the affine check needs). It does **not**
rewrite `must_free_consumed_on_all_paths`; the spine is a *separate* query the affine gate consults.

The **`#337` ADOPT** feature also already merged (`d047c62`). Its `escape.tks` arms
(`src/checker/escape.tks:316,543,741`) just recurse into the adopter body
(`TAdoptStmt as a => mark_block(a.body, false, acc)`) and its own issue text flags the deferred hole:
*"escape's per-block walk is exactly where a future 'these bindings are region-confined to the
adopter' fact could be recorded — but that is OUT OF SCOPE here."* **§5.1 below closes that hole**
with the spine's `pt` (points-to) axis: an object allocated inside an `adopt { }` block is
`pt = adopter(region_id)`, and a `Ref` that escapes the adopter's lexical brace while its referent is
`pt = adopter` is the ADOPT escape-UAF — now a spine-provable REJECT.

---

## 1. L1 — `fn_spine(f)`: the fact (the load-bearing crumb)

### 1.1 Where it lives

A **new module `src/checker/spine.tks`** (namespace `teko::checker`), beside `escape.tks`. NOT an
extension of `escape.tks`: the recon's LAYER verdict is that `mark_*` and `fn_escaping_vars` stay
**byte-identical**, and a separate module keeps the additive boundary auditable (the reviewer can
diff `escape.tks` and see zero change). `spine.tks` `use`s `teko::parser`/`teko::lexer` exactly as
`escape.tks` does, and reads the same `TFunction.body` (`src/checker/tast.tks:108`).

### 1.2 The cell universe (finite, per-function)

A **cell** is one of three shapes, keyed so the name-set projection `cell ↦ name` is total:

```
/**
 * A CELL in the spine's abstract domain — the unit the points-to / borrowed-from /
 * unique-vs-shared facts attach to. The universe is FINITE per function (bindings + their
 * one-hop fields + `Reference` params), which is what makes the worklist fixpoint terminate.
 * The `name`/`field` pair projects to the `escape.tks` name set by dropping `field` (a
 * one-hop `x.f` cell projects to the name `x`), so `fn_escaping_vars` stays byte-identical.
 *
 * @see docs/design/spine-layer-or-replace.md §2c.2 (the abstract domain)
 */
pub type Cell = struct {
    /** the binding / parameter name — the same key `str_set_*` uses (escape.tks:43). */
    name: str
    /** a single field off `name` for a one-hop path (`x.f`), else "" for a bare binding/param. EXACTLY one hop; `a.b.c` is never a cell (it collapses to `⊤`). */
    field: str
    /** true iff this cell is a `Reference`-typed parameter — the referent ANCHOR the outlives proof needs (typer.tks:2808 R5: a ref is parameter-only today). */
    is_ref_param: bool
}
```

The universe is built by one syntactic walk of `f.params` (each `Reference` param → an `is_ref_param`
cell; every param → a bare cell) plus `f.body` (each `TBinding.target` `SimpleName` → a bare cell;
each one-hop `x.f` field-read/write where `x` is a bare local → an `x.f` cell). Bounded by the
syntactic size of `f`.

### 1.3 The three lattice axes (each finite, fixed-height ≤ 3)

```
/**
 * WHERE a cell's value is allocated. Join = set-union capped at Top; Top ⇒ treat as Root (the
 * safe leak). `Adopter` carries the lexical `adopt { }` region id (§5.1 — the ADOPT escape fact).
 */
pub type PointsTo = variant PtFrame | PtRoot | PtParam | PtAdopter | PtTop
/**
 * For a `Reference` cell, WHICH referent it borrows. `⊤` (BfTop) ⇒ referent unknown ⇒ the
 * outlives proof FAILS ⇒ REJECT. A ref is parameter-only today (typer.tks:2808), so `bf` is
 * initially `BfParam(i)` or `BfNone`.
 */
pub type BorrowedFrom = variant BfNone | BfParam | BfLocal | BfTop
/**
 * Whether a cell is the SOLE live handle to its target. `let y = x` on a non-scalar joins both
 * to `UsShared`. `⊤` (UsTop) ⇒ `Shared` (the safe direction: a shared cell may not be `free`d).
 */
pub type Unique = variant UsUnique | UsShared | UsTop
```

`PtParam`/`BfParam`/`BfLocal` carry an identifier payload (the param index `i` as `u32`, or the
local name as `str`); `PtAdopter` carries the region id (`u32`). Encode the payload as a struct
field on each variant member's named type per the no-variant-fork ruling (a `Reference`-typed
member is itself forbidden — these payloads are plain `u32`/`str`, never refs). The product lattice
`PtFrame..PtTop × BfNone..BfTop × UsUnique..UsTop` has height ≤ 3 per axis ⇒ **finite, fixed-height**.

### 1.4 The `Spine` result + the monotone worklist fixpoint

```
/**
 * The per-cell fact for one function — parallel arrays keyed by cell index (Teko has no Map in
 * the compiler corpus; `escape.tks` uses the same parallel-`[]str` idiom). `fn_spine` returns
 * this; the L2 gates query it via `ref_target_outlives` / `is_unique_at`.
 */
pub type Spine = struct {
    cells: []Cell
    pt: []PointsTo       // parallel to cells
    bf: []BorrowedFrom   // parallel to cells
    us: []Unique         // parallel to cells
}

/**
 * The spine fact for one function — a bounded, per-function points-to/uniqueness lattice layered
 * BESIDE `fn_escaping_vars` (the name set stays byte-identical; this only ever CERTIFIES a subset
 * of what the name set over-marked). Builds the finite cell universe of `f`, then runs a monotone
 * worklist fixpoint over it: every transfer function only moves a cell UP the lattice, so it
 * terminates in ≤ |cells| × 3 iterations (§2c.2 termination). Reads `f.params` + `f.body`; touches
 * NO `mark_*` code.
 *
 * @param f  the typed function to analyze
 * @return   the per-cell points-to / borrowed-from / unique-vs-shared fact for `f`
 * @see      docs/design/spine-layer-or-replace.md §2c.2
 * @since    0.1.0.0-beta (#331 L1)
 */
pub fn fn_spine(f: TFunction) -> Spine { /* build universe → seed → worklist-join to fixpoint */ }
```

**Seeding** (the initial climb-floor): every bare cell starts `pt = PtFrame`, `bf = BfNone`,
`us = UsUnique`. Every `Reference` param cell starts `bf = BfParam(i)` (its own slot),
`pt = PtParam`. **Transfer functions** (monotone — climb only):

- `let y = x` / `y = x` where `x` non-scalar → `us(x) ⊔= UsShared`, `us(y) ⊔= UsShared` (alias).
- a value allocated inside an `adopt { }` block → `pt ⊔= PtAdopter(region_id)` (§5.1).
- a `Reference` binding to a local `r = &referent` → `bf(r) := BfLocal(referent-name)` (once L2a
  relaxes R5); the referent's own `pt`/`us` are read by `ref_target_outlives`.
- anything the analysis cannot name → join to `⊤` on the relevant axis (the safe direction).

**The climbing axes join componentwise** — `pt` (union of allocation sites capped at `PtTop`) and
`us` (`UsUnique < UsShared < UsTop`) are monotone-join-climbed to a fixpoint. **`bf` does NOT join:**
it is SINGLE-ASSIGNMENT — a `Reference` cell is seeded once (`BfParam(i)`/`BfNone`, refs being
parameter-only today) and, once L2a relaxes R5, written once more by DEFINITE ASSIGNMENT (`:=`) at
the ref-bind's one referent site — never merged, because a ref names exactly one referent
syntactically. The `BfNone < BfParam/BfLocal < BfTop` ordering exists for the outlives predicate's
variant reads (`ref_target_outlives`), not for a join. **Termination:** finite cell set × fixed-height
lattice × monotone transfers on the two climbing axes ⇒ fixpoint in ≤ `|cells| × 3` iterations
(the `× 3` is the product-lattice height bound; `spine-layer-or-replace.md` §2c.2 "termination").

### 1.5 The two queries the L2 gates consume

```
/**
 * Does the referent of a borrow OUTLIVE the borrow — i.e. may `borrow` (a `Reference` cell) be
 * stored/bound at `site` without dangling? True iff the referent (`bf(borrow)`) is a `BfParam`
 * (a param outlives every local — the caller owns it) OR a `BfLocal` whose block ENCLOSES the
 * borrow's block, AND the referent is not itself `pt = PtAdopter` escaping its adopter (§5.1).
 * `bf = BfTop` ⇒ referent unknown ⇒ FALSE (REJECT). Conservative: unknown ⇒ does-not-outlive.
 *
 * @param s        the function's spine
 * @param borrow   the cell index of the `Reference` being stored/bound
 * @param referent the cell index of the referent (or a sentinel when un-nameable)
 * @return         true iff the borrow provably does not outlive its referent
 */
pub fn ref_target_outlives(s: Spine, borrow: u32, referent: u32) -> bool { /* read bf, pt of referent */ }

/**
 * Is `binding` the SOLE live handle at `site` — `us(binding) = UsUnique`? The affine `mem::free`
 * / channel-send-move gate consults this: a `UsShared`/`UsTop` cell may NOT be consumed (the
 * aliased-free UAF, remodel §5 / #336 gap 1). Conservative: `⊤` ⇒ shared ⇒ FALSE.
 *
 * @param s        the function's spine
 * @param binding  the cell index of the consumed binding
 * @return         true iff `binding` is provably unique (safe to free/move)
 */
pub fn is_unique_at(s: Spine, binding: u32) -> bool { /* us(binding) == UsUnique */ }
```

`is_unique_at` does not need a distinct per-`site` value in the first build: the fixpoint joins
`UsShared` in for **any** aliasing read anywhere in `f`, so a cell that is ever aliased is
`UsShared` at the fixpoint — the conservative (over-reject) direction. A later increment may add
flow-sensitivity (unique-until-first-alias); out of scope here and documented as the honest tax.

---

## 2. L2a — Ref-storability gate (relax R5 binding/assign; narrow-relax field-store)

### 2.1 The exact sites and rule

| Site | Current rule | Change |
|---|---|---|
| `typer.tks:2808` (`type_binding`) | `if type_contains_ref(bound) { return error {…"bound to a local"…} }` | **Guard:** reject **unless** the RHS is a borrow whose spine cell satisfies `ref_target_outlives`. |
| `typer.tks:2911` (`type_assign`) | same message (assign twin) | **Guard:** same. |
| `typer.tks:2870` (`type_field_assign`) | `if type_contains_ref(field_t) \|\| type_contains_ref(v.type) { return error {…"stored in a field"…} }` | **Narrow-guard:** relax ONLY when the enclosing object is a bare-local `pt = PtFrame`, `us = UsUnique` one-hop cell AND `ref_target_outlives(referent)` holds; else KEEP. |

**Precise rule (all must hold to relax):** (i) the borrow's referent is a `BfParam` or an enclosing
`BfLocal` (`ref_target_outlives` = true); (ii) for the field-store, the container cell is
`pt = PtFrame ∧ us = UsUnique` and the path is exactly one hop (`owner.field`, matching
`escape.tks:78` `expr_is_bare_var`); (iii) the referent is not `pt = PtAdopter` escaping its adopter
brace (§5.1). If ANY fails → keep the original rejection with the **unchanged diagnostic string**
(over-rejection is conservative-safe).

### 2.2 What stays KEEP (unconditional, untouched by L2a)

`resolve.tks:1187` (collection element), `:1204` (variant member), `:1232` (fn-type param), `:1252`
(nullable), `:1132`/`:1422` (generic arg), `:1405` (`Ref<Ref<T>>`). The recon proves each has **no
single referent anchor** the one-hop bound can name. L2a touches NONE of these — the reviewer must
confirm `git diff` shows zero change in `resolve.tks`.

### 2.3 Implementation shape

`type_binding`/`type_assign`/`type_field_assign` each already receive `env`/`table`; they must also
receive (or lazily compute once per function) the current function's `Spine`. **Threading:** compute
`fn_spine(f)` ONCE at the top of the per-function typing entry and thread it down (an added parameter
on the three gate functions, or a field on an existing per-function context struct — implementer
picks the lower-churn option; both are shared-checker-touching so both need the full gate). The gate
body becomes an early-return guard (W15 flatten):

```
/**
 * (MEM Step 0, R5 — RELAXED under the spine, #331 L2a) A `Reference` may be bound to a local IFF
 * the spine proves its referent outlives the binding; otherwise the local could carry a dangling
 * borrow past the referent's death. Over-rejection is conservative-safe (a spine `⊤` ⇒ reject).
 *
 * @param bound  the binding's resolved type
 * @param s      the enclosing function's spine
 * @param borrow the cell index of the bound `Reference` (sentinel when the RHS is not a borrow)
 * @param ref_ce the cell index of the referent
 * @return       true iff the ref-bind is sound and the gate should ADMIT it
 */
fn ref_bind_is_sound(bound: Type, s: Spine, borrow: u32, ref_ce: u32) -> bool {
    if !type_contains_ref(bound) { return true }
    ref_target_outlives(s, borrow, ref_ce)
}
```

### 2.4 IMPLEMENTATION-PINNED design (2026-07-07, workflow-derived + adversarially verified — supersedes the shapes in §2.1–2.3 where they differ)

A design workflow (4 readers · 2 proposals · synthesis · 4 skeptics) pinned the exact L2a. Two prior
drafts failed here — one deferred (post-pass that never admitted: `ref_target_outlives(idx, idx)`),
one was reverted for committing a binary. This subsection is the authoritative recipe.

**THE LANGUAGE FACT that reshapes everything — refs are param-only AND there is no address-of.**
`Ref<T>` is `Reference{inner}` (type.tks:82), created ONLY by resolving the generic `Ref<T>`; there is
**no `&`/take-address operator** in the surface. Grep-confirmed, the ONLY way a `Reference`-typed value
appears in an expression is a bare `TVar` reading a **`Ref<T>` parameter** (typer.tks:44) — or that
TVar forwarded to another ref param. `r.value` reads `T` (strips the ref); a fn cannot return a ref (R3
KEEP); auto-ref keeps the value `T`-typed (the `&` lives only at the C boundary). ⇒ the only "ref-to-
local" is `let r = p` / `mut r = p; r = q` aliasing a ref **parameter**. **`bf = BfLocal` is UNREACHABLE
today** (it needs `&local`); the "bf writer" is a **bf-INHERIT** writer (`bf[storage] := bf[source]`,
always `BfParam`). The `BfLocal`/`BfTop` arms of `ref_target_outlives` stay unchanged L1 code (no
new-code coverage debt; cover the unreachable arms via a fabricated-`Spine` unit test).

**PHASE ORDERING (why §2.3's "thread fn_spine into the gates" is INFEASIBLE):** `fn_spine` needs a typed
`TFunction`; `type_binding`/`type_assign` run mid-typing on `parser::` nodes before any `TFunction`
exists. So the consumer is a **post-typing pass** `check_ref_storability(tf) -> null | error`, run ONCE
per function after the body is assembled (`type_function` after `check_must_free`, ~typer.tks:3700, over
the `TFunction` built at :3701; `type_method` over the assembled body ~:3635). Build `fn_spine(tf)` ONCE
from the ACTUAL `fixed_params` (so `is_ref_param`/`seed_bf` yield `BfParam` — a stripped shell seeds
`BfNone` and silently under-admits, a likely cause of the draft's "never admitted").

**THE (borrow, referent) PAIRING — the exact bug the reverted draft had.** For each ref-to-local site the
gate calls `ref_target_outlives(spine, borrow = cell_of(target) to u32, referent = cell_of(source) to u32)`
where **target** = the LHS storage local (its `bf` was single-assigned to `BfParam` by the inherit
writer) and **source** = the RHS bare-`TVar` name (`borrow_source_name`, reuse the `alias_src_name` shape,
spine.tks:938). da848b6 passed `(idx, idx)` — reading the referent's own `bf` (`BfNone`) — so it never
admitted. `cell_of == -1` wraps to an out-of-range `u32` the guards reject ⇒ conservative reject.

**THE bf-INHERIT WRITER (spine.tks).** Replace `bf = seed_bf(cells)` in `fn_spine` (:963) with
`bf = bf_transfer_block(f.body, cells, seed_bf(cells))`. Add a `bf_transfer_{block,stmt,expr,match,binding}`
family mirroring the `us` transfer family (:821-895) + a `set_bf` helper (copy `set_us` :794 with the
`BorrowedFrom` element). It recurses into loop/defer/adopt bodies AND if/match arm bodies **exactly like
`add_block_cells`** (a missed arm ⇒ a ref-to-local nested there types through unvalidated). It fires ONLY
on `TBinding` (never `TAssign`) whose `target` is `SimpleName`, `type_contains_ref(bound)`, and `value.kind`
is a bare `TVar` whose source cell exists → `bf[dst] := bf[src]`. Single-assignment is PRESERVED (a local
is the target of ≤1 binding; params are seeded, never targets). A chain `let a=p; let b=a` inherits
transitively in the forward sweep (no back-edges).

**INLINE-GATE RELAX (required — the post-pass alone relaxes nothing, since the inline gate rejects before
a TBinding/TAssign exists).** At typer.tks:2900 narrow to: `match bound { Reference => { /* bare ref —
defer to the post-pass */ } _ => if type_contains_ref(bound) { return error{…UNCHANGED string…} } }`.
Identically at the assign twin (:3006), deferring only when BOTH `tb.type` and `v.type` are a bare
`Reference`. A `Slice<Ref>`/`Ref?` bound STILL rejects inline with its original message (KEEP — no anchor).
The `.value` RefDeref path (:2989) returns earlier and stays exempt. The post-pass reject emits the EXACT
existing string `"a reference cannot be bound to a local (parameter-only in this version)"` (no fixture drift).

**CASE MATRIX (admit / reject):**
- `let r = p` (bare ref param) → **ADMIT**. `bf[r]=BfParam` ⇒ `referent_not_escaping_adopter(p)` (pt[p]=PtParam) ⇒ true. The primary win + positive RUN fixture.
- `let a=p; let b=a` (transitive chain) → **ADMIT** on the inherited `BfParam` tag.
- `let r = if c {p} else {q}` (non-bare RHS) → **REJECT** (double-safe: writer skips ⇒ bf[r]=BfNone; and `borrow_source_name=""` ⇒ referent=-1). The negative EXPECT_COMPILE_FAIL fixture.
- `let xs = [p,p]` (Slice<Reference>) → **REJECT** inline (nested-ref bound, no anchor).
- `mut r=p; r=q` → **ADMIT** (gate reads the single-assigned `bf[r]=BfParam` + a fresh per-site referent `q`).
- `p.value = 7` (RefDeref write-through) → **exempt/unchanged** (stores T, not a ref).
- `b.slot = <ref>` (store a ref into a field) → **REJECT / KEEP the inline gate byte-identical**. This is **VACUOUS**: no struct/class field can be DECLARED `Ref`-typed (collect.tks:1496+/resolve.tks:1373+), so `field_t` is never a `Reference` — wiring a spine narrow-relax here would be dead, uncoverable code. **DEFER §2.1(ii) entirely.**

**★ SKEPTIC AMENDMENT 1 (UNSOUND — MUST ship in PR-2): closure-capture is an escape route the rationale
missed.** `fn leak(p: Ref<i64>) -> (fn()->i64) { let r = p; () => { r.value } }` — admitting `let r = p`
lets a closure capture `r` into its heap env and RETURN it, carrying a ref to the caller's (now-dead)
object past the frame → UAF. The "R3/R4/R2/field-store = complete escape set" claim is FALSE. **Fix:** in
`type_lambda` (typer.tks:~158-163 capture loop) REJECT any capture whose captured type satisfies
`type_contains_ref` — a `Reference` may not cross a function boundary through a closure env (same law as
R3/R4). Add a fixture: a ref-local captured in an escaping lambda → COMPILE_FAIL. (Verify whether a ref
PARAM capture is already rejected today; either way, this gate must hold once `let r = p` is admitted.)

**★ SKEPTIC AMENDMENT 2 (OVER-REJECT — fix for correctness): a ref-chain link is not a data referent.**
The adopter guard checks `pt[referent]`, but for a chain link `let b = a` the `referent` = `a` is itself a
ref-local whose `pt[a]` records where the POINTER `a` is stored (possibly an adopt region), NOT where the
data lives → a sound chain in an adopt block over-rejects. **Fix:** in the post-pass, when the referent
cell is itself a `Reference` (`is_ref_param` OR `bf[referent] != BfNone`), skip the pt-adopter check — the
inherited `BfParam` tag already proves param-rooted.

**PR-2 SHIPS:** (1) the bf-inherit writer + `set_bf` in spine.tks; (2) the two inline-gate narrows
(typer.tks:2900/:3006); (3) `check_ref_storability(tf)` post-pass wired in `type_function`/`type_method`,
with amendment-2's chain-link skip; (4) the closure-capture ref-reject in `type_lambda` (amendment 1);
(5) fixtures — `stored_borrow_sound` reshaped as a ref-to-local **BIND** (take `Ref<i64>`, `let r = p`,
read AND write `r.value`, return a sentinel; VM==native) + a negative un-nameable-RHS EXPECT_COMPILE_FAIL
(added to BOTH `diff_vm_native.sh` COMPILE_FAIL[] and the `sanitizers.yml` skip loop) + a
closure-capture-of-ref-local EXPECT_COMPILE_FAIL + a fabricated-`Spine` unit test covering the
`BfNone`/`BfTop`/`PtAdopter`/cell-not-found reject arms (100%-new-code) + a ref-to-local inside a match arm
AND a loop body (recursion fidelity). **PR-2 DEFERS:** the field-store relax (vacuous), the literal
`BfLocal` writer + `&local` surface, adopter-escape enforcement at the store site (needs per-cell scope
data `Cell` lacks), L2b/L2c. **Reviewer gate:** `git diff` shows ZERO change in `resolve.tks`, `collect.tks`,
and the field-store gate (typer.tks:2965).

**KNOWN BOUND — lambda bodies are NOT gated (sound today; adversarial-review finding 2).** Both the bf
writer (`bf_transfer_*`) and the gate (`check_ref_storability_*`) descend into `if`/`match`/`loop`/`defer`/
`adopt` bodies exactly like `add_stmt_cells`, but NEITHER descends into a lambda body — the spine's cell
universe (`add_expr_cells`) does not model closures. So a ref-to-local inside a `() => { … }` is admitted
UNGATED, including the un-nameable `let r = if c {p} else {q}` that is rejected at the top level (an
asymmetry). This is SOUND under param-only refs: every admitted lambda-body ref-to-local still roots at a
caller-owned `Ref<T>` parameter and is valid within the frame; the only escape (capturing a ref into a
returned closure env) is the PRE-EXISTING closure-capture UAF (filed separately), which needs no `let r = p`
(a direct `p` capture already leaks). Consistent gating of lambda bodies waits for the spine to model
closures — do it together with the closure-capture escape fix (its natural escape-fact provider).

**Soundness (why the shipped subset is sound):** the only ref value is a `Ref<T>` param read; its referent
is caller-owned and strictly outlives every callee local; R3/R4/R2/field-store stay unconditional KEEP AND
closure-capture now KEEPs ref captures (amendment 1) ⇒ an admitted ref-to-local can only be read, deref'd,
or forwarded DOWN another ref param — it can never escape the frame. Every deferred piece is vacuous or
unconstructable, so deferring cannot under-reject; the shipped subset over-rejects at worst.

---

## 3. L2b — affine `free(x)` (`us = unique` at `type_mem_free`)

### 3.1 The site and rule

`type_mem_free` at **`typer.tks:418`**. Today it checks the target is a `mut` heap variable
(`Slice`/class) — it has NO uniqueness check. **Add:** after resolving the target `vname`
(`typer.tks:429`, the `TVar` name), require `is_unique_at(spine, cell_of(vname))`. If the cell is
`UsShared`/`UsTop` (aliased), REJECT:

```
/**
 * (#331 L2b) The affine `mem::free` guard: the freed target must be the SOLE live handle
 * (`us = unique`) at this point. A shared target (`let y = x; free(x)`) would leave `y`
 * dangling — the ALIASED-FREE UAF the design flags as the one real native gap (remodel §5 /
 * #336 HONEST-BOUNDS gap 1), caught today by NEITHER static analysis NOR ASan (only
 * TEKO_MEM_PARANOID). This closes it statically.
 *
 * @throws error  "cannot free `<name>` — it is aliased here (…); free requires the sole live
 *                 handle (spine `us` = shared)" when the target is not provably unique
 */
```

Exact new guard inside `type_mem_free`, after the `is_heap` check
(`typer.tks:438`), before building the void `TCall`:

```
if !is_unique_at(spine, cell_of(vname, spine)) {
    return error { message = $"cannot free `{vname}` — it is aliased (another binding still holds it); `mem::free` requires the sole live handle" }
}
```

`type_mem_free` currently has signature `(c, env, table)` — it must gain the function's `Spine` (same
threading as L2a). The existing `#336` recognizers (`texpr_is_mem_free_call`, `texpr_is_must_free_var`)
are unaffected; L2b is a NEW check at the `free` site, not a change to the consume dataflow.

### 3.2 Interaction with the existing `mem_free` run fixture

`examples/regressions/mem_free/` frees a **unique** `xs` (built, summed, freed, rebuilt — no alias) →
`us(xs) = UsUnique` → **still PASSES** (exit 67). This is the regression floor: L2b must not reject
the sound unique-free. The new `free_aliased_rejected` fixture (§6) is its negative twin.

### 3.3 The capture sweep — closing the LAUNDERED aliased free (option b, 2026-07-07)

The base L2b gate above closes the **direct** aliased free (`let y = x; free(x)`) because the L1 `us`
transfer marks a direct `let y = x` alias shared. But a bare `let y = x` is not the only way to make a
second live handle. A pre-merge adversarial review (teko-reviewer, PR #366) found three **laundered**
alias classes the direct transfer misses, each a real, trivially-constructible aliased-free UAF the
gate would silently ADMIT:

1. **container-store** — `ys = teko::list::push(ys, xs); free(xs)` (`xs`'s buffer is now also held by
   `ys[i]`);
2. **call-return launder** — `let y = passthrough(xs); free(xs)` (the callee returns/keeps `xs`);
3. **field-extract** — `let y = obj.data; free(obj)` (`y` aliases `obj`'s non-scalar interior).

The owner ruling (2026-07-07) chose to **close the full class now** (option b) rather than narrow the
claim. The fix is a second monotone pass, `capture_block`, layered BESIDE the direct-alias
`transfer_block` in `fn_spine` (`spine.tks`) — it does **not** modify `escape.tks` (the #330 LAYER
verdict: `fn_escaping_vars` stays byte-identical). A non-scalar bare local joins `us = shared` when it:

- is passed as an argument to a **non-`free`** call (covers 1 & 2), **except** when the enclosing
  statement re-binds the call result to that same local (`xs = push(xs, …)` — the self-thread, so the
  builder-then-`free` idiom is not over-rejected); or
- is stored into a struct field (`{ f = xs }`); or
- has a **non-scalar** interior extracted (`let y = obj.data` — a SCALAR field read like `xs.len`
  copies a stack member and is NOT an alias, so the base stays unique).

**Three soundness guards** make this safe against over-reject: (a) the self-thread exclusion keeps
`xs = push(xs, item); free(xs)` passing (`mem_free` fixture); (b) `is_mem_free_call` skips a
`teko::mem::free` call so the consume never marks its own target shared (else every `free` would
reject itself); (c) the scalar-field discriminator keeps `.len`-before-`free` unique. Validated: the
gen-2 self-build accepts the whole compiler corpus (no over-reject), gen1==gen2 byte-identical
(fixpoint held), and the two new negative fixtures (`free_captured_by_container_rejected`,
`free_field_extract_rejected`) reject with the aliased-free diagnostic.

**Accepted cost (option b):** the call-argument rule is argument-blind to the callee's actual
behavior, so a `free(x)` after passing `x` to a *read-only* function is now conservatively rejected
(the sound over-reject direction — §1.4 "cannot name → ⊤"). The compiler corpus does not hit this,
and a caller can always `free` before the read-only call.

---

## 4. L2c — channel-send-move (semantics only)

The channel SURFACE is a later wave (remodel §3, out of scope of #331). What lands here is the
**consume semantics**: a channel-send is an affine MOVE — the same `is_unique_at` check as `mem::free`.
Since no channel-send syntax exists yet, L2c ships as:

1. the `is_unique_at` query itself (already built in L1) — the single reusable affine primitive;
2. a **doc-comment contract** in `spine.tks` stating that a future `chan.send(x)` consume site MUST
   call `is_unique_at(spine, cell_of(x))` and REJECT a shared/`⊤` target, identically to L2b — "one
   analysis, four guarantees" (issue §2 L2c);
3. NO surface, NO new gate site, NO fixture (there is no syntax to exercise). This is an
   honest-stop scaffold: the primitive exists and is documented; the wiring waits on the channel wave.

**This is the one part that is intentionally semantics-only.** It is not INFEASIBLE — it is
deliberately scoped to the primitive because the consume site does not exist yet. Flagged explicitly
so the implementer does not hunt for a channel AST that isn't there.

---

## 5. Sequencing — the sub-PR order into #329

Each sub-PR is independently gate-able (full gate + FIXPOINT gen1==gen2 + `diff_vm_native` +
independent review — the recon's `ADDITIVE-BUT-GATE-TOUCHING` classification):

1. **PR-1 — L1 `spine.tks` (the fact, PURE).** Adds `src/checker/spine.tks` with `Cell`/`PointsTo`/
   `BorrowedFrom`/`Unique`/`Spine`/`fn_spine`/`ref_target_outlives`/`is_unique_at`. **No gate site
   changes yet** — the query is computed but consulted nowhere. Because it emits no C and relaxes no
   gate, **FIXPOINT is preserved by construction** (a new pure query changes no generated output).
   Gate: full gate confirms the new module compiles on both engines; fixpoint trivially holds. This
   PR is the safe landing of the whole lattice, reviewable in isolation. Ships `stored_borrow_sound`
   ACCEPT is NOT yet true here (the gate still rejects) — so no positive fixture lands in PR-1; only
   the negative KEEP-forever fixtures (`ref_returned_rejected`, `ref_in_collection_rejected`) land
   here as guards that PR-1 changed nothing.

2. **PR-2 — L2a Ref-storability.** Threads the spine into `type_binding`/`type_assign`/
   `type_field_assign` and relaxes R5 + narrow field-store. NOW `stored_borrow_sound` ACCEPTS and
   `stored_borrow_outlives_referent` still REJECTS. Ships fixtures 1 + 2. This is the shared-checker
   change that alters the accepted language ⇒ full discipline.

3. **PR-3 — L2b/L2c affine.** Threads the spine into `type_mem_free`; adds the `is_unique_at` guard;
   adds the L2c doc-contract. Ships `free_aliased_rejected`; the existing `mem_free` fixture is the
   passing unique-free floor. L2c adds no fixture (no surface).

### 5.1 Where the ADOPT escape-UAF footgun (#337's deferred hole) is closed

`#337` merged with `escape.tks:316` recursing plainly into `adopt` bodies and its issue text naming
the region-confinement fact as deferred. The spine's `pt = PtAdopter(region_id)` axis closes it:

- an object allocated lexically inside an `adopt { }` block is seeded `pt = PtAdopter(id)`;
- `ref_target_outlives` returns **false** for a borrow whose referent is `pt = PtAdopter(id)` when the
  borrow's storage cell outlives that adopter's lexical brace (the referent is bulk-dropped at the
  brace; a borrow surviving it dangles) — so L2a's field-store/bind relaxation **cannot** admit a
  ref-into-adopter-escaping-store.

**Scope honesty:** the full adopter-escape fact needs the spine to know each cell's enclosing
`adopt` region id. The MINIMUM that ships in #331 is the `PtAdopter` axis + the `ref_target_outlives`
false-for-escaping-adopter rule; recording the precise region id per allocation is a one-hop
extension of the same walk `escape.tks:316` already does. If the region-id threading proves heavier
than the one-function budget allows, the **safe fallback** is: any referent inside ANY `adopt { }`
block is treated `pt = PtAdopter(⊤)` and a borrow of it stored outside the block is REJECTED
(conservative — over-rejects nested-adopter cases, never under-rejects). This keeps the footgun
closed even in the degenerate case. Documented here so the implementer ships the safe fallback if the
precise id proves costly, rather than shipping an under-approximation.

---

## 6. Fixtures + failure map

Under `examples/regressions/`, following the `mem_free` / `must_free_leak` layout (a `<name>.tkp`
with `name`/`source = "src"`/`[artifact] kind = "binary"`; a `main.tks`; a `src/<mod>/…tks`). Negative
fixtures carry an **`EXPECT_COMPILE_FAIL`** marker file AND must be added to BOTH harness consumers:
`scripts/diff_vm_native.sh`'s `COMPILE_FAIL=(…)` array (`diff_vm_native.sh:84`) AND relied on by
`.github/workflows/sanitizers.yml`'s skip loop (`sanitizers.yml:121`, which skips any dir containing
the marker). **Keep the two in sync** (the `must_free_leak` marker file says so verbatim).

| # | Fixture | Kind | Observable | Lands in | Guards |
|---|---|---|---|---|---|
| 1 | `stored_borrow_outlives_referent` | **COMPILE-REJECT** (`EXPECT_COMPILE_FAIL`) | `teko build` AND `teko run` both exit non-zero with the `typer.tks:2870` "stored in a field" diagnostic | PR-2 | spine's field-store relax did NOT over-accept a container-outlives-referent case |
| 2 | `stored_borrow_sound` | **RUN** | VM exit == native exit == fixed sentinel (reads through the borrow, returns a known sum, mirror `mem_free`'s exit-code shape) | PR-2 | spine actually RELAXED R5/field-store for the unique frame-local one-hop shape |
| 3 | `ref_returned_rejected` | **COMPILE-REJECT** (`EXPECT_COMPILE_FAIL`) | both engines reject with `typer.tks:3456`/`:3527` "cannot return a reference (pass-down only)" | PR-1 | R3 KEEP-FOREVER (§2c.4b) — the one-function bound must never relax this |
| 4 | `free_aliased_rejected` | **COMPILE-REJECT** (`EXPECT_COMPILE_FAIL`) | both engines reject with the L2b "aliased … sole live handle" diagnostic | PR-3 | the `us = shared` affine guard — the one real native UAF gap (remodel §5) |
| 5 | `ref_in_collection_rejected` | **COMPILE-REJECT** (`EXPECT_COMPILE_FAIL`) | both engines reject with `resolve.tks:1187`/`:1204` "stored in a struct/variant/collection" | PR-1 | the KEEP-forever collection/variant anchor — BUILD touched neither |

Fixture 4 PAIRS with the existing `examples/regressions/mem_free/` (unique free → must keep PASSING
at exit 67). Fixtures 3 + 5 land in **PR-1** as pre-existing-behavior guards (they already reject
today; PR-1 must not change that — proving the pure query is inert). Fixtures 1 + 2 land in **PR-2**
(the relaxation). Fixture 4 lands in **PR-3** (the affine `free`).

---

## 7. The honest ceiling — what ships vs what stays REJECTED forever

**Ships (the three narrow spine wins):**
- **local ref-to-local binding** within one function (`typer.tks:2808`/`:2911` R5 relaxed under
  `ref_target_outlives`);
- **`mem::free` affine safety** keyed on `us = unique` (`typer.tks:418`) — closes the one real native
  gap (`y = x; free(x); use(y)`), invisible today to ASan (only `TEKO_MEM_PARANOID` catches it);
- **narrow one-hop stored borrow** (`typer.tks:2870`) — relaxed ONLY for a unique, frame-local,
  one-hop container whose referent provably outlives it;
- **the affine primitive `is_unique_at`** reused as the channel-send-move consume check (semantics
  only; no surface — §4).

**Stays REJECTED forever (blanket gates untouched):**
- **returned borrows** — `typer.tks:3456`/`:3527` (R3). The one-function bound makes the caller frame
  un-nameable; `bf` is unconstrainable, the outlives proof un-FORMULABLE (not merely unproven).
  **KEEP FOREVER.**
- **refs in collections / variants / generics / fn-params** — `resolve.tks:1187`, `:1204`, `:1232`,
  `:1132`, `:1422`, `:1405`. No single referent anchor survives; the one-hop bound cannot bound their
  lifetime. **KEEP.**
- **nullable refs** — `resolve.tks:1252` (R2). Orthogonal value-model rule, not a lifetime hole.
  **KEEP.**

**Feasibility:** FEASIBLE as a LAYER, per the recon. No part is INFEASIBLE. The single semantics-only
part (L2c channel-send-move) is deliberately scoped to the affine primitive because the channel
surface does not exist yet — it is a documented honest-stop, not a gap. The ADOPT escape-UAF fact
(§5.1) is feasible with a documented safe-fallback if precise region-id threading exceeds the
one-function budget.

---

## Verification (doc-only)

This change is a single `.md` file. `git diff --stat origin/remodel/memory-unsafe-backend -- '*.tks'
'*.tkt'` MUST be EMPTY. Doc-only ⇒ fixpoint (gen1==gen2) preserved by construction, no `.tks`
delta; `diff_vm_native` unaffected. No full build gate is required for this plan.

## No unresolved HALT

Every design fork resolved from the #330 verdict + the code:
- **Where the fact lives** (new `spine.tks` vs extend `escape.tks`): resolved to a NEW module by the
  LAYER verdict's byte-identical-name-set invariant (auditability).
- **`is_unique_at` flow-sensitivity**: resolved to the conservative any-alias-anywhere ⇒ shared
  reading, per the design's accepted over-rejection tax; flow-sensitive refinement deferred, named.
- **ADOPT region-id precision**: resolved to ship the `PtAdopter` axis with a documented safe fallback
  (`PtAdopter(⊤)` reject-on-escape) if precise ids exceed budget — never an under-approximation.

No genuine tension remains for the owner.
