# The Safety Spine — escape/lifetime analysis that makes `Ref<T>` genuinely safe (0.3.1)

**Status:** DESIGN-AHEAD, doc-only. The KEYSTONE of the language thesis ("no-GC safety without a
ceremony borrow-checker"). L1 of the remodel (`memory-unsafe-backend-remodel.md:40` — "the whole
safety bet"), today *unbuilt, research-grade*. This document is the executable design for the
**0.3.1 "Safety Wave"**; it must be ready when 0.3 (the backend) closes.

**Consumes (the sources):**
- `docs/design/ref-transparent-model.md` (branch `docs/ref-transparent-model`, PR #499) — the surface
  spec: the binding×aliasing matrix, `ref`, type-directed auto-deref, the bidirectional desugar,
  R1–R11, the **6 amendments A1–A6** (= the spine requirements), the 7 open cases.
- `docs/design/spine-layer-or-replace.md` (#330 recon, MERGED) — the LAYER verdict (spine is a new
  `fn_spine` query BESIDE the untouched `fn_escaping_vars` name set) and its **hard scope** (one
  function + one hop; returned/collection borrows KEEP-FOREVER).
- `docs/design/spine-build-plan.md` (#331 BUILD) — the shipped/planned L1 lattice + L2a/L2b/L2c gates.
- `src/checker/escape.tks` (one-depth, no points-to) + `src/checker/spine.tks` (the #331 one-hop
  lattice) — the substrate this extends.
- `docs/design/memory-unsafe-backend-remodel.md` (§1 the hybrid model; §5/§5a the `mem::free`/`#must_free`
  bounds the spine must close).
- Issue-mother **#498**; companion **#497** (`-> void` removal).

**Standing scope discipline (lean-knobs):** the analysis is MANDATORY and default-on; `adopt` and
explicit lifetime annotations enter **only** when dogfooding PROVES them necessary — this document
does NOT design a pile of theoretical knobs. It finds the ONE knob the compiler's own corpus forces
(§B2-slice-1b), and stops there.

---

## 0. TL;DR — the thesis verdict up front (read this first)

> **The light spine is VIABLE for the 95%, and the compiler's own PIPELINE passes reject-conservative
> trivially — but the compiler's own STDLIB (the iterator/stream idiom in `src/iter` + `src/io`, 28
> functions) forces exactly ONE step beyond the #330/#331 "one-function, one-hop" ceiling: a one-hop
> `return-borrows-which-params` summary (lifetime-elision-LITE). It is NOT the full Rust borrow
> checker, NOT whole-program points-to, and NOT open research — but it IS real machinery the recon
> explicitly declined. Without it the reject-conservative v1 either over-rejects the entire stdlib
> (self-host FAILS) or under-rejects a real user UAF (thesis-safety FAILS). With it, the whole
> compiler passes AND the idiom is sound.**

Full ranked assessment in **§C (Thesis Risk Assessment)**. The one-line answer to the owner's two
questions:

1. **Does the compiler pass reject-conservative?** *Pipeline: yes, trivially (borrow-down mutable
   accumulators only). Stdlib: only with the param-root closure rule + the borrow summary of
   §B2-1b; a naive blanket reject BREAKS it.*
2. **Light spine or heavy machine?** *Light for the dominant pattern; ONE lean-knob (borrow summary,
   inferred-or-annotated) for the closure-over-borrow idiom. No heavy machine needed. No genuine HALT.*

---

# BLOCK 1 — the transparent `ref` surface (Phase 1 of 0.3.1)

The migration from the current `.value`-based `Ref<T>` to the transparent model. This precedes the
spine (Phase 2); the analysis is type-driven and works on either surface, so Block 2 does NOT block on
Block 1 — but the surface migration must land first for the ergonomics the wave advertises.

## B1.1 The matrix and the `ref` modifier (from #498 §2–§3)

Two orthogonal axes — `mut` (write) × `Ref<T>` in the type (aliasing). 2×2, three legal + one illegal:

| | value `T` | reference `Ref<T>` |
|---|---|---|
| **`let`** | frozen immutable snapshot | **ILLEGAL** (no immutable reference) |
| **`mut`** | mutable local value (not a pointer) | the alias (write-through) |

`ref x: T` is the **3rd binding modifier**, sugar for `mut x: Ref<T>`. It is inherently alias-mutable
(no `let ref`/`mut ref`). The current state: `BindKind = enum { Let; Mut; Const }` (`src/parser/ast.tks`)
and the lexer maps `let`/`mut`/`const` (`src/lexer/lexer.tks`). `ref` is a **contextual** keyword (Teko
already does this for `from`/`params`/`trait`/`type`/`to`/`in`/`self`/`base`) so a namespace segment
named `ref` stays legal.

## B1.2 The bidirectional desugar + type-directed auto-deref (from #498 §4)

- **Encapsulate** (value → Ref, at init/attach): `ref x: T = <value>` wraps the value into a Ref (R5,
  copy into the arena). This is the direction the earlier `= Ref<[]T>` draft got wrong — the RHS is the
  **value**, not a Ref.
- **Peel** (Ref → value, at use): using `ref x` in a value context auto-derefs, **type-directed** —
  strips EXACTLY as many `Ref` layers as the expected type at the use-site demands, never "all the way"
  blindly. Against a free generic `U`, zero peels (the whole Ref is passed — this is why generics stay
  sound).
- **Marshall** (ptr ↔ Ref) is the ONLY explicit crossing — the `unsafe`/FFI boundary. Spelling is
  **open case #1** (§C2); it blocks the FFI surface, NOT the analysis.

## B1.3 Migrating the compiler's own `Ref` usage — the mechanical hazard

The compiler reads/writes `Ref<T>` almost exclusively as **borrow-down mutable accumulators** via
`.value` (e.g. `escape.tks` `acc.value = str_set_add(acc.value, name)`; `typer.tks` `lam_collect_*`;
`codegen.tks` `cg_lift_*`). Under transparency these become:

- read `acc.value` → `acc` (auto-deref to `[]str`);
- write `acc.value = X` → `acc = X` (write-through, R4).

**Hazard — `.value` is overloaded.** `.value` is BOTH the Ref-deref sigil AND a legitimate struct field
name (`IntIndexed { index = …; value = v }` in `int_iter.tks`; many non-Ref `.value` fields — a raw
grep counts 652 `.value` across `src/`, the large majority NOT Ref). A blind `sed` is unsound. The
migration MUST be **type-checker-driven**: only a `.value` whose receiver resolves to a `Reference` type
is rewritten. Recommended path (respects the **stable-seed gotcha** — src/ cannot use `ref` until the
umbrella ships a seed that parses it):

1. Add `ref` (contextual keyword + `BindKind::Ref`) and transparent auto-deref to the compiler; keep
   `.value` as a **deprecated-but-accepted alias** on `Reference` (parses old + new). No src/ change yet.
2. Ship a seed carrying `ref` + transparency.
3. Migrate `src/` `.value`-on-Ref → transparent, `Ref<T>` binding → `ref` where it is a borrow-down.
   The 28 `src/iter`+`src/io` closure functions and the ~10 pipeline accumulators are the surface.
4. Remove the deprecated `.value`-on-Ref acceptance. Fixpoint + `diff_vm_native` at each step.

## B1.4 `Ref<Ref<T>>` — currently rejected, must open

`resolve.tks` (`"a reference cannot target another reference (\`Ref<Ref<T>>\` is invalid)"`) rejects
nesting today. The transparent model (#498 §3, open case #4) makes `Ref<Ref<T>>` **legal** (the `ref`
modifier gives the outermost layer, the type gives the inner). This gate must relax to `KEEP the reject
only for the value-model cases the spine still forbids` — but nesting per se becomes legal. This is a
Block-1 surface change gated on **open case #5** (type-directed auto-deref soundness — §C2) because the
peeling rule is what makes nested refs addressable without a sigil.

## B1.5 `#497` (`-> void`) co-migration

`-> void` has **8** surface occurrences in `src/` (the internal `Void` type stays). `fn` without `-> T`
returns nothing. Small, mechanical, independent of the spine; sequence it with the Block-1 surface pass.

---

# BLOCK 2 — the spine (A1–A6) as an implementable crumb sequence

## B2.0 Relationship to the existing #330/#331 spine — EXTEND, do not restart

The #331 spine (`src/checker/spine.tks`) already ships the **one-function, one-hop** lattice: `Cell`
(`name`/`field`/`is_ref_param`), the three axes `PointsTo`/`BorrowedFrom`/`Unique`, `fn_spine`, and the
two queries `ref_target_outlives` / `is_unique_at`, plus the capture sweep for laundered aliased-free.
The #330 recon's **hard scope** declared refs-in-collections/variants/closures a **KEEP-FOREVER reject**
because "the one-hop bound cannot bound their lifetime."

**A1–A6 push exactly one notch past that ceiling.** They do NOT rewrite the lattice; they add:
- a **type-directed transitive reachability** walk (A1-detect) so a Ref hidden inside a returned
  aggregate/closure is SEEN (today `escape.tks` is one-depth and MISSES it — the §7 soundness hole);
- a **one-hop `return-borrows-params` summary** (A1-interproc) so the reachable-Ref-that-roots-at-a-param
  is admitted and the reachable-Ref-that-roots-at-a-local is rejected AT THE CALLER;
- four local guards (A2/A3/A4/A5) and one interprocedural ownership rule (A6).

The LAYER contract stays: `fn_escaping_vars` remains byte-identical; every new fact is a `spine.tks`
query that only ever CERTIFIES-a-subset or REJECTS-more. A reviewer diffs `escape.tks` and sees zero
change (except the shared reachability helper, which is additive and pure).

**The enforcement law (from #498 §9):** *reject-what-you-can't-prove.* Early versions are SAFE (reject
what they cannot prove), permissive over time. This is the project honest-stop applied to the memory
checker.

## B2.1 The abstract-domain extension (what A1 actually needs)

Two new facts, both bounded and terminating — NEITHER is a points-to graph, region inference with
constraint solving, or a whole-program borrow checker:

### (a) `type_reaches_ref` — the type-directed reachability predicate (finite, pure)

```
/**
 * Does the type `t` TRANSITIVELY reach a `Reference` — through struct/class fields, collection
 * elements, variant members, tuple/optional inners, or a closure's captured-env type — such that a
 * by-value copy of a `t` would copy a POINTER (the §7 soundness hole: copying a `Holder { r: Ref<int> }`
 * copies `r`, not the pointee)? This is A1's DETECTION core: a purely type-structural walk, NOT a
 * points-to analysis. Nominal recursion is bounded by `visited` (a type visited more than once contributes
 * no new Ref edge), so the walk terminates; the type universe is finite per compilation.
 *
 * @param t        the resolved type under test
 * @param visited  the nominal type names already entered on this path (recursion guard)
 * @return         true iff a by-value copy of `t` carries at least one live `Reference` edge
 * @see            docs/design/ref-transparent-model.md §7 (the single root soundness hole)
 * @since          0.3.1.0-beta (#498 A1)
 */
pub fn type_reaches_ref(t: Type, visited: []str) -> bool { /* struct/collection/variant/closure walk */ }
```

This is Go-style escape reachability, not Andersen/Steensgaard. It answers "could this value smuggle a
borrow out," which is all the reject direction needs.

### (b) `BorrowSummary` — the one-hop interprocedural lifetime fact (the lean-knob)

```
/**
 * The per-function BORROW SUMMARY: which of `f`'s parameters the RETURN VALUE (transitively) borrows.
 * This is lifetime-elision-LITE — the ONE fact past the #330 one-hop escape ceiling that the compiler's
 * own iterator/stream stdlib forces (28 functions return a closure capturing a `Ref` parameter). It is
 * NOT a named-lifetime system and NOT whole-program: it is a single bitmask per function, computed from
 * `f`'s body in one pass, consumed exactly ONE hop deep at each call site.
 *
 * INFERENCE (v1, conservative): a param index `i` is in `returns_borrow_of` iff some value in an
 * escaping/return position of `f` transitively captures/contains parameter `i` (a `Ref` param read into
 * a returned closure env, a returned aggregate whose field aliases param `i`, or a returned call whose
 * own summary borrows an argument that is param `i`). Unknown/recursive/higher-order-opaque ⇒ the
 * WHOLE-return-is-borrowed fallback (`returns_all = true`), the safe over-reject direction.
 *
 * @field returns_borrow_of  the 0-based parameter indices the return value borrows
 * @field returns_all        true ⇒ un-summarizable: treat the return as borrowing every param (over-reject)
 * @see   §B2-slice-1b; docs/design/spine-layer-or-replace.md §2c.4 (the one-hop bound)
 * @since 0.3.1.0-beta (#498 A1 interprocedural)
 */
pub type BorrowSummary = struct { returns_borrow_of: []u32; returns_all: bool }

/**
 * Compute `f`'s borrow summary. Computed BOTTOM-UP over the (acyclic in the compiler corpus) borrow
 * call-graph so a callee's summary is available when a caller is summarized; a back-edge (recursion)
 * falls back to `returns_all` (a single monotone widening — no full fixpoint needed, and the safe
 * direction). Reads `f.params` + `f.body` + the already-computed summaries of the callees `f` returns.
 *
 * @param f          the typed function
 * @param callee_sum  a query giving the BorrowSummary of an already-summarized callee (or `returns_all`)
 * @return            the borrow summary of `f`
 * @since 0.3.1.0-beta (#498 A1 interprocedural)
 */
pub fn fn_borrow_summary(f: TFunction, callee_sum: BorrowSummaryEnv) -> BorrowSummary { /* one forward pass */ }
```

**Why this is bounded, not a whole-program borrow checker:** one bit-set per function; a single
bottom-up pass; recursion widens to `returns_all` (never iterated to a least fixpoint); higher-order
opacity widens to `returns_all`. The COST is over-rejection on the widened cases, never unsoundness and
never non-termination. The compiler corpus's iterator/stream functions form an ACYCLIC borrow graph
(each combinator wraps a strictly-smaller source), so they all summarize precisely — the widening never
fires on the dogfood corpus (verify this in slice 1b; see §C1 risk R2).

## B2.2 The crumb sequence — cheap → expensive

Each slice is independently gate-able (full gate + FIXPOINT gen1==gen2 + `diff_vm_native` +
independent review — the #330 `ADDITIVE-BUT-GATE-TOUCHING` discipline, because each RELAXES or TIGHTENS
the accepted language). Ordered so the seed never uses a feature it cannot yet parse (all of A1–A6 are
checker-only; no new surface except Block 1's `ref`, so the stable-seed constraint binds only Block 1).

### Slice 0a — A4: `ref` definite-assignment [LOCAL, cheapest]

**Rule (#498 A4):** `mut x: Ref<T>` / `ref x: T` REQUIRES an initializer at the declaration
(C++-style definite-assignment). R3 only bars `let Ref`; a `ref` without init + R4 (write-through never
rebinds) = a deref of garbage on the first `=`.

**Algorithm:** layer on the existing `src/checker/initanalysis.tks` (already a definite-assignment
dataflow). At a `TBinding` whose declared/inferred type is a `Reference` and whose `value` is absent
(or is the `null`/uninit sentinel), REJECT.

**Signature (implementer copies verbatim):**
```
/**
 * (A4) A `ref`/`mut Ref<T>` binding MUST carry an initializer — an uninitialized reference + R4's
 * "assignment is write-through, never rebind" would deref garbage at the first `=`. Enforced at the
 * binding site (definite-assignment), reusing `initanalysis.tks`'s dataflow.
 *
 * @param b  the binding under check
 * @return   true iff `b` is a reference binding WITHOUT a valid initializer (⇒ REJECT)
 * @since    0.3.1.0-beta (#498 A4)
 */
fn ref_binding_missing_init(b: parser::Binding) -> bool { /* is-ref-typed ∧ no init */ }
```

**Fixtures:** `ref_uninit_rejected` (COMPILE-REJECT, both engines, non-zero) · `ref_init_ok`
(RUN, VM==native, sentinel 0).

**Honest-stop:** none — this is total and local. **Dogfooding exposure:** ZERO (the compiler declares
no `ref` local; refs are param-only today). A4 is future-proofing the surface.

### Slice 0b — A3: lvalue/rvalue precedence at a `ref` parameter [LOCAL]

**Rule (#498 A3):** a non-Ref **lvalue** argument (a bare var / an all-mut access-path — an addressable
place) to a `ref` parameter is **R7 borrow-down** (alias the caller's storage). A non-Ref **rvalue /
temporary** (a literal, a call result, an operator expression) to a `ref` parameter is **R5 copy** (into
the callee's arena, non-escaping). This resolves the genuine-escape-vs-copy-alias-seam.

**Algorithm:** a local syntactic classifier on the argument expression. Teko has no formal lvalue
category today, so introduce a predicate (NOT a type-system change):

```
/**
 * (A3) Is `e` an LVALUE — an addressable place the caller owns (a bare `TVar`, or a field/index path
 * every segment of which is addressable)? An lvalue arg to a `ref` param is a borrow-down (R7 alias);
 * a non-lvalue (rvalue/temporary — literal, call result, operator) is a copy-into-callee (R5),
 * non-escaping. The distinction is purely syntactic + one type lookup per segment; NOT dataflow.
 *
 * @param e  the argument expression
 * @return   true iff `e` denotes an addressable place (⇒ R7 borrow-down); false ⇒ R5 copy
 * @since    0.3.1.0-beta (#498 A3)
 */
fn arg_is_lvalue(e: TExpr) -> bool { /* TVar | (TFieldAccess/TIndex over an lvalue) */ }
```

**Fixtures:** `refparam_lvalue_aliases` (RUN — pass a `mut` local, callee writes through, caller
observes the mutation; VM==native, sentinel encodes the written value) · `refparam_rvalue_copies` (RUN
— pass a temporary, callee's write does NOT escape; sentinel encodes the unchanged caller state).

**Honest-stop:** an access-path whose addressability the classifier cannot confirm ⇒ treat as **rvalue
(copy)** — the safe direction (copy never dangles; it only costs an arena copy). **Dogfooding exposure:**
the pipeline passes `mut` locals (lvalues) to `Ref` params — already the R7 path; A3 formalizes it.
No corpus regression.

### Slice 1 — A1-detect: transitive field-sensitive reachable-Ref escape REJECT [the monster's tractable half]

**Rule (#498 A1):** materialize/verify **every reachable `Ref` edge** inside an aggregate/collection/
closure at a return/store that crosses an arena — not just the top scalar (copy-on-attach dissolves only
the scalar/leaf case; a `Ref` hidden inside a struct/collection/closure copies the POINTER — the §7
hole). The escape checker becomes **field-sensitive + transitive**.

**The v1 enforcement is DETECT-and-REJECT, not materialize.** The MATERIALIZE direction (deep-copy every
reachable Ref into the destination arena) is DEFERRED and, for transparency, is arguably ill-posed:
deep-copying a `Ref` breaks its aliasing contract (writes through the original stop being seen). So v1
does the always-sound thing — SEE the reachable Ref and REJECT the escape; the transparent deep-copy is
a later, opt-in, per-type decision (§C1 risk, NOT shipped in v1).

**Algorithm:** in the escape walk (the `mark_*` family or a new parallel walk in `spine.tks` that leaves
`escape.tks` byte-identical), at every ESCAPING position (return value, binding/assign RHS that outlives,
struct-init field, collection element, closure capture), if the value's TYPE satisfies
`type_reaches_ref` AND the reachable Ref does not provably root at something that outlives the escape
target, REJECT. The reachability walk is the §B2.1(a) predicate; the "roots at" test consults the spine's
`bf`/`pt` for the top-level and the borrow summary for call results.

```
/**
 * (A1) The transitive reachable-`Ref` escape gate: at an escaping position, a value whose type
 * `type_reaches_ref` and whose reachable borrow does NOT provably outlive the escape target is REJECTED.
 * Closes the §7 soundness hole (Ref-in-struct-field, Ref-in-returned-collection, Ref-captured-in-escaping-
 * closure) that the one-depth `escape.tks` misses. DETECT-and-REJECT — never a silent deep-copy.
 *
 * @param val   the escaping expression
 * @param s     the enclosing function's spine
 * @param sum   the enclosing function's borrow summary (for call-result roots)
 * @return      true iff `val` may carry a reachable Ref past its safe lifetime (⇒ REJECT)
 * @since       0.3.1.0-beta (#498 A1)
 */
fn escaping_value_leaks_ref(val: TExpr, s: Spine, sum: BorrowSummary) -> bool { /* reachability + root test */ }
```

**Fixtures:**
- `leak_ref_in_struct_field` (COMPILE-REJECT) — the §7 example `return Holder { r = local }`; both
  engines non-zero with the A1 diagnostic. THE hole made a static error.
- `leak_ref_in_returned_collection` (COMPILE-REJECT) — `[]Ref<T>` built from locals, returned.
- `sound_ref_struct_frame_local` (RUN) — a `Holder { r = <param-rooted borrow> }` used within the frame,
  never returned; VM==native sentinel. Proves A1 does not over-reject the frame-local case.

**Honest-stop:** anything the reachability walk cannot resolve (an opaque generic `U` before
monomorphization, a `.tkb`-imported type whose Ref-reachability is not serialized) ⇒ treat as
`reaches_ref = true` (over-reject). Deep-copy materialize ⇒ DEFERRED. **Dogfooding exposure:** the
compiler stores NO Ref in a struct field and returns NO Ref-bearing collection (verified: zero
`Ref`-typed fields, zero `[]Ref` in `src/`), so A1-detect fires on the pipeline NOWHERE — EXCEPT on the
closure-capture case, which slice 1b resolves.

### Slice 1b — A1-interproc: the `return-borrows-params` summary [the lean-knob; unblocks the stdlib SOUNDLY]

**This is the load-bearing slice for dogfooding.** The 28 `src/iter`+`src/io` functions return a CLOSURE
capturing a `Ref<Cursor>` parameter (e.g. `over_array(xs, cur: Ref<ArrayCursor>) -> IntIter` returns
`() => { … cur.value … }`). A1-detect (slice 1) sees "an escaping closure whose capture reaches a Ref"
and would REJECT — breaking self-host on `int_iter.tks` line 1.

The idiom is **sound**: the captured `cur` is a PARAM, whose referent is caller-owned and outlives the
frame (the peel-law: the returned closure carries a borrow of something ≥ the caller). Slice 1b makes the
analysis prove it, in two moves:

1. **Local admission rule:** an escaping closure/aggregate is ADMITTED when every reachable Ref it carries
   roots at a PARAM (`bf = BfParam`, `pt = PtParam`) — the referent outlives the frame. This alone lets
   `over_array` et al. COMPILE.
2. **Caller-side soundness (the summary):** `fn_borrow_summary(over_array)` records `returns_borrow_of =
   {xs, cur}`. At a caller, when an argument fed to a summarized borrowed param is a **local** (not a
   param), the call RESULT is treated as borrowing that local; if the result then ESCAPES the caller,
   REJECT. This catches the genuine UAF `fn make() -> IntIter { mut c = Cursor{}; over_array(xs, c) }`
   (feeds a LOCAL `c`, escapes the closure — `c` dies, the closure dangles).

```
/**
 * (A1 interproc) Caller-side check: does the RESULT of `call` borrow a caller LOCAL (via a summarized
 * borrowed parameter fed a local argument) AND then flow to an escaping position? If so the returned
 * borrow outlives its referent ⇒ REJECT (the iterator-over-a-local-that-escapes UAF). If every
 * summarized-borrowed arg is itself a param (or an outliving place), the result is param-rooted ⇒ ADMIT.
 *
 * @param call   the call expression in an escaping position
 * @param s      the enclosing function's spine
 * @param sum    the CALLEE's borrow summary
 * @return       true iff the escaping call-result carries a borrow of a caller local (⇒ REJECT)
 * @since        0.3.1.0-beta (#498 A1 interprocedural)
 */
fn borrowed_result_escapes(call: TCall, s: Spine, sum: BorrowSummary) -> bool { /* map summary onto args */ }
```

**Fixtures:**
- `iter_over_param_ref_ok` (RUN) — `over_array`-shaped: take `Ref<Cursor>` + slice PARAMS, return a
  closure, caller drives it within the frame; VM==native, sentinel = sum of yielded elements. Proves the
  stdlib idiom COMPILES.
- `iter_over_local_escapes_rejected` (COMPILE-REJECT) — the `make()` UAF: feed a LOCAL cursor, return the
  closure; both engines non-zero with the A1-interproc diagnostic. Proves soundness for users.
- `iter_combinator_chain_ok` (RUN) — `take(skip(over_array(xs, c1), c2), c3)`-shaped nesting, exercising
  the ACYCLIC bottom-up summary composition; VM==native. Proves the summary composes without widening.

**Honest-stop / KNOWN BOUNDS (must be documented in `spine.tks`):**
- **Higher-order opacity:** a combinator that takes an `IntIter` (a closure) as a param and RE-wraps it
  (`take(src: IntIter, cur: Ref<TakeCursor>)`) borrows through a FUNCTION VALUE. The summary treats the
  incoming closure param conservatively: its captured borrows are opaque, so if the combinator's return
  captures the `src` closure, the summary widens `src`'s contribution to `returns_all` UNLESS `src` is
  itself a param (then it stays param-rooted — the actual corpus case). **Verify (slice-1b acceptance):
  the corpus combinators keep `src` as a param and never store it in a local before returning — if true,
  no widening fires and they summarize precisely.** (§C1 R2.)
- **Recursion:** a self-borrowing recursive function widens to `returns_all` (safe over-reject). None in
  the corpus.
- **`.tkb` boundary:** the borrow summary is NOT yet serialized in the `.tkb` codec (mirrors the
  `di_kind`/`must_free` gaps in `memory-unsafe-backend-remodel.md:238`) — so a cross-`.tkl`-package call
  to a borrowed-return function widens to `returns_all` until the codec carries the summary. Documented
  follow-up; module-local analysis is exact.

**Dogfooding exposure:** THIS slice is what makes the 28 stdlib functions pass. It is the mandatory
minimum for self-host under the transitive escape gate.

### Slice 2 — A2: borrow-down is NON-ESCAPING [consumes slice 1b]

**Rule (#498 A2):** a borrow-down `Ref` flows only DOWNWARD; storing it upward (an out-param `mut`, a
struct field, a collection, a closure that escapes) is a compile error.

**Algorithm:** with slices 1+1b in place this is largely SUBSUMED — A2 is the same reachable-Ref escape
gate applied to the specific "store a borrowed ref into an outliving place" sites: the `type_field_assign`
/ collection-push / out-param-write sites already in `typer.tks`/`resolve.tks`. A2 = "at those sites, if
the stored value reaches a Ref whose root does NOT outlive the container, REJECT," reusing
`escaping_value_leaks_ref`. The one NEW site is the **escaping closure capture of a borrow-down local**
(distinct from a param): a `let r = <param-ref>; () => { r … }` returned is admitted (param-rooted), but
capturing a narrower local ref is rejected.

**Fixtures:** `borrow_stored_in_field_rejected` (COMPILE-REJECT) · `borrow_in_escaping_closure_rejected`
(COMPILE-REJECT) · `borrow_down_used_locally_ok` (RUN — the accumulator idiom: pass `mut acc` down,
mutate `.value`, read `acc` after; VM==native — THE compiler's own pattern).

**Honest-stop:** the closure body modeling in the spine is shallow (the #331 `spine.tks` does not descend
into lambda bodies — documented in `spine-build-plan.md` "KNOWN BOUND"); A2 relies on the CAPTURE LIST
type (which reaches-ref or not), not on lambda-body flow, so the shallow model suffices. **Dogfooding
exposure:** `borrow_down_used_locally_ok` is the pipeline's exact pattern — MUST keep passing (regression
floor).

### Slice 3 — A5: composite borrow-down path all-mut + R10 [LOCAL]

**Rule (#498 A5):** borrow-down of a place `p` only if EVERY segment of the access-path is `mut`
(root binding mut AND each field mut). Write-through via a borrowed `ref` is an EXTERNAL write (subject
to R10 — `a.field = v` on a `let a` panics; external field writes require a `mut` binding). A path rooted
in a `let` is never borrow-eligible.

**Algorithm:** a per-access-path syntactic walk consulting the binding kind of the root and the `mut`-ness
of each field along the path:

```
/**
 * (A5) Is the access-path `place` borrow-eligible — is EVERY segment mutable (the root binding is `mut`
 * AND each traversed field is declared mutable)? A borrow-down aliases the caller's storage; if any
 * segment is immutable the write-through would violate that segment's immutability (R10). A `let`-rooted
 * path is never eligible.
 *
 * @param place  the argument access-path
 * @param env    the type/binding environment (root kind + per-field mutability)
 * @return       true iff every segment is mutable (⇒ borrow-eligible)
 * @since        0.3.1.0-beta (#498 A5)
 */
fn access_path_all_mut(place: TExpr, env: Env) -> bool { /* root kind + field mut chain */ }
```

**Fixtures:** `borrow_immutable_path_rejected` (COMPILE-REJECT — borrow a field off a `let`-rooted path)
· `borrow_all_mut_path_ok` (RUN) · `external_field_write_on_let_rejected` (COMPILE-REJECT — R10).

**Honest-stop:** a path segment whose mutability cannot be resolved ⇒ treat as immutable (reject). R10's
"mutating method on a `let a`" is **open case #7** — recommendation: require `mut` (else `let` is not a
real firewall); confirm with owner (§C2). **Dogfooding exposure:** the pipeline borrows only bare `mut`
locals (root-mut, no path) — trivially eligible.

### Slice 4 — A6: free-requires-ownership + DI monotonic [interprocedural ownership + DI]

**Rule (#498 A6):** `mem::free` (and every consuming free) is ILLEGAL via a non-owning `ref`
(borrow-down / non-owning); the residual gets a 2nd class — "free of storage aliased by a live Ref" —
tracked interprocedurally. `#wire` runs a lifetime-MONOTONICITY check (a holder-L Ref may bind only a
provider ≥L; `singleton ≤ scoped ≤ transient` by region depth) OR is desugared BEFORE the escape pass.

**Algorithm — two independent parts:**
1. **free-requires-ownership:** extend the L2b `is_unique_at` gate at `type_mem_free` (`typer.tks`) with an
   OWNERSHIP predicate: the freed target must be `us = UsUnique` AND NOT a borrow (`bf = BfNone`, i.e. an
   owned local, not a borrowed-down ref). Freeing a borrow-down ref frees the caller's live storage
   (`defer { mem::free(r) }` on a borrowed `r` — the §7 residual). This reuses the existing spine axes;
   no new lattice.
2. **DI-monotonic:** a check over the `#wire`-generated provider graph — a `#singleton` holder's `Ref`
   field to a `#scoped` provider dangles when the request region drops. Model each lifetime as a region
   DEPTH (`singleton=0 ≤ scoped=1 ≤ transient=2`); a holder at depth d may hold a Ref to a provider at
   depth ≤ d only. Run this on the DI graph BEFORE (or as) the escape pass.

```
/**
 * (A6) May the target of a consuming free (`mem::free`, channel-send-move) be consumed HERE — is it the
 * SOLE live handle (`us = UsUnique`) AND an OWNED local (not a borrowed-down `ref`, `bf = BfNone`)?
 * Freeing a borrow frees the caller's still-live storage (the "free of Ref-aliased storage" residual).
 *
 * @param s     the enclosing function's spine
 * @param name  the freed target's binding name
 * @return      true iff `name` is provably unique AND owned (⇒ free is legal)
 * @since       0.3.1.0-beta (#498 A6)
 */
fn free_target_is_owned(s: Spine, name: str) -> bool { /* is_unique_at ∧ bf = BfNone */ }
```

**Fixtures:** `free_borrowed_ref_rejected` (COMPILE-REJECT — `defer { mem::free(r) }` on a `ref` param)
· `free_owned_unique_ok` (RUN — the existing `mem_free` regression, exit 67, must keep passing) ·
`di_singleton_holds_scoped_rejected` (COMPILE-REJECT — the DI cross-lifetime dangle) ·
`di_monotonic_wire_ok` (RUN).

**Honest-stop:** the DI-monotonic check needs the `#wire` provider graph; if `#wire` desugars to plain
constructors BEFORE the escape pass, A6-DI is subsumed by A1 (the generated field-store becomes a normal
Ref-in-field escape) — prefer that (less new code). **Dogfooding exposure:** ZERO — the compiler calls no
`mem::free` in its own logic and uses no `#wire`/DI. A6 is a user-facing guarantee with no corpus stress
(so it CANNOT be validated by self-host — its fixtures are its only proof; call this out in review).

## B2.3 Sub-PR order (into `remodel/backend-build`)

| PR | Slice | Ships | Gate |
|---|---|---|---|
| SP-0 | Block-1 surface | `ref` keyword + transparent auto-deref + deprecated `.value` alias + `-> void` (#497) | full + fixpoint + diff; stable-seed dance |
| SP-1 | 1a `type_reaches_ref` + 1b `BorrowSummary` (PURE) | the two new facts, consulted nowhere yet | fixpoint trivially holds (pure query) |
| SP-2 | A4 + A3 | definite-assignment + lvalue/rvalue | full + fixpoint + diff |
| SP-3 | A1-detect + A1-interproc | the transitive escape gate + caller-side summary | full + fixpoint + diff + **corpus must self-host** (the load-bearing gate) |
| SP-4 | A2 | borrow-down non-escaping | full + fixpoint + diff |
| SP-5 | A5 | composite path all-mut + R10 | full + fixpoint + diff |
| SP-6 | A6 | free-ownership + DI-monotonic | full + fixpoint + diff |

**RITUAL POINTS (the full gate MUST pass — C-gate + self-host + native + FIXPOINT gen1==gen2 +
`diff_vm_native` + 100%-new-code coverage + independent review):**
- **SP-0** (surface change touches the parser + checker — shared).
- **SP-3** (the transitive gate — this is where self-host either survives or reveals the iterator
  collision; it is the single most important ritual point in the wave).
- **SP-4, SP-5, SP-6** (each relaxes/tightens the accepted language — shared-checker discipline per #330).
- SP-1 (pure) and SP-2 (A4/A3, no corpus exposure) need the gate but are low-risk.

---

# BLOCK C — THESIS RISK ASSESSMENT (the crown deliverable, ranked)

The owner's mandate: test the theory EARLY because it MIGHT NOT WORK. Below is the honest map of where
the theory breaks at DESIGN level, ranked by threat to the thesis.

## C1. Ranked risks to the thesis

### R1 — [CERTAIN, HIGH] The naive reject-conservative BREAKS self-host on the stdlib iterator idiom.
**Finding.** `src/iter` + `src/io` (28 functions) implement the ENTIRE iteration/streaming layer as
"take a `Ref<Cursor>` param, return a CLOSURE that captures it" (Teko has no `while`/`for`; iterators
ARE closures over a caller-owned cursor Ref). Examples: `over_array`, `range`, `once`, `take`, `skip`,
`enumerate`, `chain` (int_iter); `over_strs`, `over_bytes` (str/byte iter); `mem_read_fn`,
`buffered_read_fn`, `limit_read_fn`, `zip_writer`, `zip_reader` (io/stream). `teko.tkp` has
`source = "src"` → ALL of these compile during self-host → the spine runs on every one.

The shipped/planned spine plan (`spine-build-plan.md` §2.4 **skeptic-amendment-1**, "MUST ship in PR-2")
says: *"in `type_lambda` REJECT any capture whose captured type satisfies `type_contains_ref`."* Applied
literally, that rejects all 28 → **self-host FAILS at `int_iter.tks:20`.** This is a concrete, ALREADY-
LATENT collision between the spine plan and the dogfood corpus.

**Why it's only HIGH, not fatal:** the idiom is SOUND (captured Ref roots at a PARAM). The fix is
slice 1b's param-root admission rule + borrow summary. **Recommended resolution (law-first):** the
blanket amendment-1 reject is UNSOUND-BY-OVER-REJECTION against the corpus; refine it to "reject a
closure capture whose reachable Ref does NOT root at a param," and add the caller-side summary so the
narrower-local case is still caught. Passes-all-Laws (safe + compiles the corpus) ⇒ this refinement wins.

### R2 — [HIGH] The borrow summary is a whole-program-FLAVORED fact; higher-order combinators are the stress.
**Finding.** The summary is bounded (one bitmask, one bottom-up pass, widen-on-recursion). But the
iterator COMBINATORS (`take(src: IntIter, cur)`, `skip`, `chain`, `enumerate`) take a CLOSURE as a param
and re-wrap it — borrow flows THROUGH a function value, the hardest interprocedural case. If a combinator
stored its `src` closure in a local before returning, the summary would widen to `returns_all` and
over-reject a legitimate combinator. **The corpus appears to keep `src` a param captured directly into
the returned closure (no intervening local), so no widening fires — but this MUST be verified in slice
1b acceptance, function by function.** If even one combinator defeats the precise summary, the fallback
is over-rejection (self-host breaks) → that combinator needs a rewrite OR the lean-knob annotation
(§C1 R1 resolution). **Threat:** if MANY corpus patterns defeat the summary, the "light" analysis is not
light — you are inferring lifetimes through higher-order code, which trends toward the heavy machine.
**Recommended de-risk:** slice 1b's FIRST task is to run `fn_borrow_summary` over `src/iter` + `src/io`
and confirm every function summarizes precisely (no `returns_all` widening). Do this in the PURE PR
(SP-1) BEFORE any gate relaxes — it is a pure query, so it can be dumped and inspected with zero risk.
This is the single cheapest early test of the thesis.

### R3 — [MEDIUM] No immutable borrow (`&T`) → forced arena copies on hot read-only paths.
**Finding (open case #4).** The matrix is deliberately 2×2 — there is no read-only borrow. Passing a
large `let`-owned value to a function that only READS it forces a copy (R5/R8 copy-into-arena). The
compiler passes large TAST/AST nodes by value constantly. Today those are cheap (structural sharing via
the arena + by-value structs are pointers under the hood), but the transparent model's "attach copies"
semantics could turn some of these into deep copies. **Threat:** ergonomics/perf leak, NOT soundness —
but a perf regression on the compiler's own hot path is a real cost the thesis "safety is free" claim
must not hide. **Recommended resolution:** ACCEPT the 2×2 for v1 (the spec's recommendation), but
INSTRUMENT — measure arena-copy volume on self-build before/after Block 1. Introduce a read-only view
ONLY if PGO shows hot large copies (a lean-knob proven by dogfooding, not designed up front). Flag to
owner as a WATCH item, not a blocker.

### R4 — [MEDIUM] Type-directed auto-deref soundness is UNVERIFIED and interacts with A1.
**Finding (open case #5).** The peeling rule strips "exactly as many Ref layers as the expected type
demands." In a STORE/escaping context the expected type could be `Ref<T>` (one peel of a `Ref<Ref<T>>`),
which would STORE a mid-level ref — potentially the very Ref-in-container escape A1 forbids. The peeling
and the escape gate MUST be co-designed: the peel that produces a `Ref` value feeds A1's reachability.
**Threat:** a type-directed peel could re-open a closed hole. **Recommended resolution:** run the
dedicated mini-soundness pass the spec (#498 open case #5) already calls for, BEFORE cementing the
peeling rule — specifically, enumerate every context where peeling yields a `Ref`-typed (not fully-peeled
`T`) value and confirm each such value flows only to a `ref`/`mut Ref` slot A1 also guards. This gates
Block-1's `Ref<Ref<T>>` opening (B1.4). Cheap to check on paper; do it in the SP-0 design review.

### R5 — [LOW, but genuinely open-research if pursued] Transparent deep-copy-on-attach; A6 DI-monotonic.
**Finding.** Two pieces are the actual research edge — and BOTH are correctly DEFERRED, so they do not
threaten v1:
- **A1 MATERIALIZE (transparent deep-copy of reachable Refs on attach)** is ill-posed for transparency
  (deep-copying a Ref silently breaks its aliasing contract). v1 REJECTS instead — always sound. Only if
  a future wave wants "copy a struct-with-Refs into a `let` and have it Just Work" does this become open
  research. **Do NOT promise it.**
- **A6 DI-monotonic** across `#wire`-generated graphs is the one genuinely interprocedural ownership
  analysis, but it has ZERO dogfooding exposure (the compiler uses no DI) and can be subsumed by A1 if
  `#wire` desugars before the escape pass. **Recommended:** desugar-first; do not build a bespoke DI
  lifetime solver in v1.

## C2. Which of the 7 open cases BLOCK implementation

| # | Open case | Blocks? | Verdict |
|---|---|---|---|
| 1 | Marshall spelling | **Blocks Block-1 FFI surface only** | The `ptr`↔`Ref` crossing can't be spelled — but the ANALYSIS (Block 2) never needs it. Ship the spine against the current `.value` surface; add Marshall as a late, small slice. NON-blocking for the spine. |
| 2 | `let → mut` specific cases | No | Leave `let ↛ mut` as-is (spec default); revisit if dogfooding demands. |
| 3 | `ref` rebind | No | Recommendation: fixed binding (C++-style, no rebind via `=`); A4 assumes it. Confirm with owner but non-blocking (the safe default is "no rebind"). |
| 4 | No immutable borrow (`&T`) | No (but R3 WATCH) | Accept 2×2; instrument perf. |
| 5 | Auto-deref soundness | **Gates Block-1 `Ref<Ref<T>>` opening** | Run the mini-soundness pass before cementing peeling (R4). Blocks the NESTED-ref surface, not the spine core. |
| 6 | `Ref` as struct field | No | Covered by A1 (transitive) + A5 (path). Confirm DI/adopted-type interaction in A6. |
| 7 | Mutating method on `let a` | No | Recommendation: require `mut` (else `let` is not a firewall). Confirm with owner; non-blocking (the safe default is "require mut"). |

**Net:** NO open case blocks the spine analysis (A1–A6 crumbs). Two gate BLOCK-1 surface pieces (Marshall
spelling → FFI crossing; auto-deref soundness → `Ref<Ref<T>>`). The spine proceeds against today's
surface regardless.

## C3. Is A1 (transitive escape) tractable? — YES for the shipped subset; the "monster" is half-lamb.

- **The DETECT/REJECT direction is a KNOWN, CORRECT, TERMINATING algorithm:** a type-directed transitive
  reachability walk (`type_reaches_ref`) — finite (the type universe is finite; nominal recursion bounded
  by a visited-set) — plus a one-hop borrow summary (a bitmask, one bottom-up pass, widen-on-recursion).
  This is **Go-style escape analysis + lifetime-elision-LITE**, NOT Andersen/Steensgaard points-to, NOT
  region inference with constraint solving, NOT a whole-program borrow checker. No fixpoint over abstract
  locations; the only fixpoint is the existing per-function `us`/`pt` join (already shipped) plus a single
  monotone widening on the summary. Tractable.
- **The MATERIALIZE direction (transparent deep-copy) is the ONLY research-open piece — and v1 does NOT
  ship it** (it rejects instead). So A1-as-shipped is tractable; A1-as-dreamed (deep-copy transparency)
  is deferred and should stay deferred.
- **Verdict:** A1 is NOT open research for reject-conservative. It IS more than the #330 one-hop ceiling
  (the transitive reachability + the summary), so the recon's "collections KEEP-FOREVER" softens to
  "collections/closures ADMITTED when param-rooted, REJECTED otherwise" — a real capability gain, at the
  cost of the one lean-knob (R1/R2).

## C4. The bottom line — where the thesis holds and where it bends

- **HOLDS (the 95%):** the compiler's own PIPELINE (lexer/parser/checker/codegen/emit/build) uses `Ref`
  ONLY as borrow-down mutable accumulators (`acc: Ref<[]str>` — pass a `mut` local down, mutate `.value`
  in place, read after). Zero Ref-in-field, zero Ref-return, zero Ref-in-collection, zero `mem::free`,
  zero `adopt`, zero DI in the pipeline. It passes reject-conservative TRIVIALLY (A2 happy path). The
  thesis "no-GC safety without ceremony" is TRUE for the dominant pattern, PROVEN by the load-bearing
  corpus.
- **BENDS (the iterator/closure-over-borrow idiom):** the stdlib's 28 closure-returning functions force
  ONE step past the one-hop ceiling — the `return-borrows-params` summary. This is the lean-knob the
  mandate says to add only when dogfooding forces it, and the stdlib forces it. It is tractable and
  bounded, but it is NOT free: it is the seed of a (deliberately minimal) lifetime system.
- **DOES NOT need the heavy machine:** no named-lifetime variables, no whole-program points-to, no borrow
  checker over the 95%. If slice 1b's summary survives the higher-order combinators (R2 — the ONE thing
  to test first, cheaply, in the pure PR), the light spine is confirmed viable. If it does NOT survive,
  the pivot is an EXPLICIT `borrows(params)` return annotation on the ~28 stdlib functions (still light —
  the annotation IS the summary, checked locally) rather than escalating to whole-program inference.

**Recommended first move when 0.3 closes:** ship SP-1 (the PURE `type_reaches_ref` + `BorrowSummary`)
and DUMP the summary over `src/iter`+`src/io`. That single, zero-risk, pure-query experiment settles R2
— the whole thesis — before one gate is touched. Test the theory where it is cheapest to kill.

---

## No unresolved HALT

Every fork resolves law-first (passes-all-Laws wins): the amendment-1 blanket reject is refined to the
param-root rule (safe AND compiles the corpus); the interprocedural piece is scoped to a one-hop
bounded summary with a safe widening (never unsound, never non-terminating); the MATERIALIZE and DI
research edges are DEFERRED behind sound rejects. The two owner-confirmations flagged (open cases #3
`ref` rebind, #7 mutating-method-on-`let`) both have a SAFE default (no-rebind; require-mut) the design
adopts pending confirmation — neither blocks. **No genuine tension requires the owner before
implementation begins.** The single most valuable early action (a pure-query dump of the borrow summary
over the stdlib) is called out so the thesis is tested at minimum cost.

---

*Sources: `docs/design/ref-transparent-model.md` (#499), `docs/design/spine-layer-or-replace.md` (#330),
`docs/design/spine-build-plan.md` (#331), `src/checker/escape.tks`, `src/checker/spine.tks`,
`docs/design/memory-unsafe-backend-remodel.md`. Corpus audit: `src/iter/*`, `src/io/*`, `teko.tkp`.
Issue-mother #498; companion #497.*
