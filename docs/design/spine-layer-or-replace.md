# Spine LAYER-vs-REPLACE recon — `escape.tks` audit (issue #330)

**Status:** RECON DECISION, doc-only. Sub-branch of `remodel/memory-unsafe-backend` (#329).
Load-bearing gate for L1/L2 of the memory remodel (`docs/design/memory-unsafe-backend-remodel.md:40,47-49,60-64`).

---

## VERDICT (one line)

**LAYER** — a bounded, one-function + one-hop points-to/uniqueness lattice can be layered *beside*
the existing per-binding-name `[]str` set as a **new, additive query**, because the current pass is
already a single-pass structural walk over `TExpr`/`TStatement` (`escape.tks:91-317`) whose only fact
is a **name set** used monotonically ("when unsure, escaping" — `escape.tks:11-12,33`); the name set
stays untouched and remains the sound over-approximation, while the spine adds a *finer* proof that can
only ever *shrink* the set of programs the L2 gates reject. The name set is soundly one projection of a
richer per-cell fact: the current pass drops all cell identity (`escape.tks:14`) and never runs a
second pass (`escape.tks:284`), so there is nothing to *replace* — the spine is strictly more
information, not a different representation.

**Deciding cite:** `escape.tks:14` ("no points-to graph — DEFERRED to a later, finer step") explicitly
frames the spine as a *later, finer* addition, not a rewrite; and `escape.tks:11-12` ("intentionally
INCOMPLETE — it frees only a provable subset") means every consumer already tolerates the name set
being a *conservative superset* of what truly escapes. A layer that proves *more* frame-locality can
only make consumers route more tightly — never less soundly.

**Important scope correction (honest ceiling):** LAYER is feasible as the *representation* decision.
It does **NOT** mean the spine ships all four guarantees cheaply. The stored-borrow guarantee (L2 gate
#1, relaxing `typer.tks:2512`) requires the lattice to carry a **lifetime/outlives** ordering that the
current pass has zero machinery for, and the one-hop bound (§2) makes the *returned-borrow* case
(`typer.tks:3169`) **permanently un-provable** and therefore a **keep-forever REJECT**, not a relax.
So the verdict is **LAYER, with a hard scope**: the spine can relax the *storable-into-a-local-arena*
and *self-append/`mem::free`-affinity* cases; it **cannot** relax returned borrows; and stored borrows
into fields relax **only** under a one-hop outlives proof, everything else stays REJECTED.

---

## §2c.1 — LAYER or REPLACE?

**Answer: LAYER.** The bounded lattice is added as a *new* per-function query alongside the untouched
`fn_escaping_vars` name set. `mark_*` is **not** replaced.

Reasons, each cited:

1. **The fact is already a monotone over-approximation, not a precise set.** `escape.tks:9-12` states
   soundness is absolute and the analysis is "intentionally INCOMPLETE — it frees only a provable
   subset." Consumers therefore already accept: `name ∈ escaping` may be a false positive (over-marked
   but safe). A layer that proves a *subset* of those false positives are actually frame-local is
   purely additive — it removes conservatism without adding unsoundness. Nothing in the consumers
   assumes the set is precise.

2. **The representation is a name set with no cell identity — so there is nothing to migrate.**
   `escape.tks:14` ("no points-to graph — DEFERRED"), `escape.tks:43-57` (`str_set_*` — membership by
   name string). The name set is one projection (`cell ↦ name`, collapsing all cells of a name to the
   name) of a richer per-cell fact. Because the projection is total and lossy in the *safe* direction
   (collapsing loses precision, never soundness), the finer fact refines the name set without changing
   its type or its callers.

3. **Single-pass, no fixpoint is a *precision* choice, not a *representation* barrier.**
   `escape.tks:283-285` ("conservatively the name may itself later escape — we do not run a second
   fixpoint pass") means `let y = x` over-marks `x`. A REPLACE would re-run `mark_*` as a fixpoint over
   abstract locations; a LAYER instead runs a *separate* bounded fixpoint (§2) whose result only ever
   *clears* a name the current pass over-marked. The current pass keeps producing its superset; the
   spine produces the refinement; the L2 gates read the refinement, codegen keeps reading the name set.

4. **The one-depth call arm (`escape.tks:124-131`) is the exact seam the layer refines.** Today every
   call argument is marked escaping unconditionally ("a call may retain its argument", `escape.tks:20,123`).
   The spine adds a *one-hop* interprocedural summary (does the callee's *declared* signature retain the
   arg?) — a new query, not a rewrite of the arm. The arm's blanket `true` remains the fallback when the
   summary is absent or the hop budget is spent.

**Why not REPLACE.** Replacing `mark_*` with a fixpoint over abstract locations would (a) rewrite a
shared checker function both codegen engines read at `codegen.tks:6277`, forcing a full-gate +
independent-review change (§ "downstream build classification"); (b) risk *reducing* the escaping set
below the sound floor if the fixpoint has any bug, converting a leak (safe) into a UAF (vulnerability),
violating M.1/M.5 (`escape.tks:9-11`); and (c) buy nothing the layer does not — the layer already
produces the finer fact the L2 gates need. REPLACE is strictly more dangerous for zero additional power.

---

## §2c.2 — The bounded fact: a sketch

### Abstract domain

A **cell** is one of:
- a **binding** — a `let`/`mut`/`const` local or a parameter, keyed by name (reusing the same name key
  as `str_set_*`, so the name-set projection is `cells ↦ names`);
- a **one-hop field path** — `x.f` where `x` is a bare local (a `TVar`, per `expr_is_bare_var`
  `escape.tks:78-83`) and `f` is a single field. **Exactly one hop.** `a.b.c` is *not* a cell — it
  collapses to the conservative "escapes" state, matching the current pass's treatment of non-bare
  receivers (`escape.tks:26-33,169-178`);
- a **parameter slot** — a `Reference`-typed parameter (the only place a `Ref` may live today,
  `typer.tks:2446-2450` R5), which is the *referent anchor* the outlives proof needs.

### Lattice edges (the fact carried per cell)

Three orthogonal facts, each a small finite lattice:

1. **points-to** `pt(cell) ⊆ {frame, root, param(i), ⊤}` — where the cell's value is allocated.
   `⊤` = unknown ⇒ treat as `root` (the safe leak). Join = set union, capped at `⊤`.
2. **borrowed-from** `bf(cell) ∈ {none, param(i), local(name), ⊤}` — for a `Reference` cell, which
   referent it borrows. `⊤` = borrowed-from-unknown ⇒ the outlives proof fails ⇒ REJECT. Because a ref
   is parameter-only today (`typer.tks:2446`), `bf` is initially always `param(i)` or `none`.
3. **unique-vs-shared** `us(cell) ∈ {unique, shared, ⊤}` — whether the cell is the sole live handle to
   its target. `let y = x` on a non-scalar sets `us(x) := shared` and `us(y) := shared` (join to
   `shared`). `⊤` = unknown ⇒ `shared` (the safe direction: a shared cell may not be `free`d — the
   `mem::free` affine gate). Join: `unique ⊔ shared = shared`.

The product lattice is `pt × bf × us`, height ≤ 3 per axis ⇒ **finite, fixed-height**.

### Why one function + one hop is the honest bound

- **One function:** interprocedural precision needs a call graph + a summary fixpoint over it, which is
  a whole-program analysis — exactly the "whole-program borrow checker" the design *rules out* as taxing
  the 95% with no memory problem (`memory-unsafe-backend-remodel.md:57-58`). The one-depth call arm
  (`escape.tks:124`) already draws this line; the spine keeps it, refining a call only by the callee's
  **declared** signature (a `Ref` param that the callee's return type cannot surface — a *local*,
  syntactic check on the callee decl, no transitive summary). Anything deeper is un-bounded.
- **One hop:** the current pass's *only* precision refinement is a scalar field off a bare local — one
  hop (`escape.tks:169-178`). A second hop (`a.b.c`) crosses a boxed heap pointer (`escape.tks:27-30`)
  whose lifetime the name set cannot name, so it is already collapsed to escaping. Matching that bound
  means the lattice's field-path cells terminate at depth 1 — a `⊤` beyond one hop. This keeps the cell
  universe **finite per function** (bindings + their direct fields), which is what makes the fixpoint
  terminate.

### Where the fixpoint runs, the join, and termination

- **Where:** a new `pub fn fn_spine(f: TFunction) -> Spine` beside `fn_escaping_vars` (`escape.tks:323`),
  reading the same `f.body`. It runs a **per-function** worklist fixpoint over the finite cell universe
  of `f` (its bindings + one-hop field paths + `Reference` params). It does **not** touch `mark_*`.
- **The join** is the componentwise product-lattice join above (union for `pt`, `none < param(i)/local < ⊤`
  for `bf`, `unique < shared < ⊤` for `us`). Monotone by construction (every transfer function only
  moves a cell *up* the lattice — never down).
- **Termination:** the cell set is finite (bindings of `f` + their one-hop fields, both bounded by the
  syntactic size of `f`). Each cell's fact is a fixed-height (≤3) lattice element. The transfer
  functions are monotone (facts only climb). A monotone function over a finite product of fixed-height
  lattices reaches a fixpoint in ≤ `|cells| × 3` iterations. This is the *second* fixpoint pass that
  `escape.tks:284` explicitly declined for the name set — the spine adds it *for the finer fact only*,
  where its cost is bounded and its result is used solely to *relax*, never to *un-mark* the name set.

### The name-set projection (the layer contract)

`fn_escaping_vars(f)` stays **byte-identical**. The spine's relationship to it is the invariant:

> for every binding name `n`, if the spine proves `pt(n) = frame ∧ us(n) = unique ∧ bf`-outlives-holds,
> then `n`'s allocation is frame-safe **even if** `n ∈ fn_escaping_vars(f)` (the name-set over-marked it).

The L2 gates read the spine; codegen region routing keeps reading the name set unchanged (§2c.3). The
two never disagree unsafely because the spine only ever certifies a *subset* of what the name set
conservatively rejected.

---

## §2c.3 — Where it plugs in (per consumer)

### Codegen region-routing consumers (`codegen.tks`) — **SAME name-set projection, UNCHANGED**

| Consumer | Site (current) | Plug-in |
|---|---|---|
| `fn_escaping_vars(f)` | `codegen.tks:6277` | **Unchanged.** Codegen keeps reading the name set. |
| `binding_is_frame_local` | `codegen.tks:5564,6072` | **Unchanged** (reads `escaping` name set). |
| `binding_is_frame_local_slice` | `codegen.tks:6075` | **Unchanged.** |
| `binding_is_block_local` | `codegen.tks:5555` (also referenced `:5274,:5108`) | **Unchanged.** |
| `assign_routes_to_frame` | `codegen.tks:5623` | **Unchanged.** |
| `assign_frees_old` | `codegen.tks:5612` | **Unchanged.** |

Region routing is a *performance* refinement (frame vs root); its safe default is leak-to-root. The
spine does **not** touch it in the first build — the spine's job is the L2 *rejection* gates, not codegen
routing. (A later build *could* let codegen frame-route a spine-proven-unique cell, but that is a
separate, additive step and out of scope here.)

### The `Ref` escape gates — **NEW query `ref_target_outlives(borrow, referent)`**, per gate

These are total type-level rejections (`resolve.tks`/`typer.tks`), not flow analysis. The spine adds a
new query; each gate is then classified **relax** (guard the rejection with the spine proof) or **keep**
(unconditional forever).

| Gate | Site (re-anchored, current) | Rule | Spine verdict |
|---|---|---|---|
| R4 — ref as collection element | `resolve.tks:1168` | ref cannot be a slice/collection element | **KEEP.** A collection outlives the frame and has no single referent anchor; one-hop cannot bound its lifetime. |
| R4 — ref as variant member | `resolve.tks:1185` | ref cannot be a variant member | **KEEP.** Same — a variant escapes structurally; no anchor. |
| R4 — ref as function-type parameter | `resolve.tks:1213` | ref cannot be a fn-type param | **KEEP.** Passing a ref through a fn value crosses the one-function bound (the callee is opaque). |
| R2 — ref nullable | `resolve.tks:1233` | `Ref<T>?` rejected (a ref is never null) | **KEEP.** Nullability is orthogonal to lifetime; this is a value-model rule, not a spine hole. |
| B — ref as inferred/written generic arg | `resolve.tks:1113` (inferred), `resolve.tks:1403` (written) | ref cannot be a generic type argument | **KEEP.** Stamping a ref into a generic field/return escapes; no anchor survives monomorphization. |
| R5 — ref bound to a local | `typer.tks:2450` (binding), `typer.tks:2553` (assign) | ref cannot be bound to a local (parameter-only) | **RELAX (candidate).** The spine can bind a ref to a local **iff** `ref_target_outlives(local, referent)` — the referent (a `param(i)` anchor via `bf`) outlives the local's block. This is the *primary* spine win: local borrows within one function. |
| — ref stored in a field | `typer.tks:2512` | ref cannot be stored in a field | **RELAX (candidate, guarded)** — **ONLY** when the enclosing struct/class is itself a frame-local, spine-proven `pt=frame`, `us=unique` cell whose lifetime the one-hop path bounds. Otherwise **KEEP**. This is the stored-borrow guarantee (L2 gate #1) and it is the *narrowest* relaxation. |
| R3 — function returns a ref | `typer.tks:3098,3169` | a function cannot return a reference (pass-down only) | **KEEP FOREVER.** The one-function bound makes the caller's frame invisible; a returned borrow's referent is un-nameable. See §2c.4(b). *(This gate was not in the issue's §2b list; the recon adds it because it is the hard REJECT that fixes the one-hop bound.)* |

`ref_target_outlives(borrow, referent)` is the single new query the relaxable gates consult; it reads
`bf` (which referent) and `pt`/`us` (the referent's region and uniqueness) from the spine.

---

## §2c.4 — The unsoundness map (the three hard cases)

Each case: can the bounded fact PROVE it (relax the gate) or must it REJECT (keep the gate)?

### (a) Aliased value — `let y = x; …`

- **Today:** `escape.tks:283-285` over-marks `x` escaping outright (no fixpoint). Sound but imprecise.
- **Spine:** **CAN PROVE (partially), and where it cannot, MUST reject safely.** The `us` axis tracks
  this exactly: `let y = x` on a non-scalar sets `us(x) := shared`, `us(y) := shared` (join). The spine
  then knows `x` is no longer unique. For the *frame-locality* use (region routing) this is irrelevant —
  aliasing does not make a frame-local value escape as long as both aliases die in the frame; the spine
  can keep both frame-local when `pt(x)=pt(y)=frame`. For the *`mem::free` affine* use, `us=shared`
  ⇒ `free(x)` is **REJECTED** (freeing a shared cell is the aliased-UAF the design flags as the one real
  gap, `memory-unsafe-backend-remodel.md:24-25`). **Verdict: PROVE unique ⇒ relax; else REJECT.**
- **Gate tied to:** the `mem::free` affine gate (L2 gate #2, downstream `S1_use_list_import`/`mem::free`)
  and the local-binding gate `typer.tks:2450`.

### (b) Returned borrow — a `Ref` flowing into a `TReturn`

- **Today:** rejected unconditionally at `typer.tks:3098,3169` (R3 — "a function cannot return a
  reference (pass-down only)"). The `mark_*` pass would separately mark the returned value escaping
  (`escape.tks:304-305`), but the *type gate* rejects it before flow ever matters.
- **Spine:** **MUST REJECT — permanently.** The one-function bound (§2) means the caller's frame, into
  which the returned ref's referent would have to outlive, is **invisible** to a per-function analysis.
  There is no anchor cell for "the caller's stack slot," so `bf` is unconstrainable and the outlives
  proof is un-formulable, not merely unproven. This is the case that *fixes* the one-hop / one-function
  bound as honest: the spine cannot see across the return edge, so R3 stays a blanket KEEP forever.
- **Gate tied to:** `typer.tks:3169` (R3). **KEEP FOREVER.**

### (c) Field-stored borrow — a ref written into a struct/class field

- **Today:** rejected unconditionally at `typer.tks:2512` ("a reference cannot be stored in a field
  (parameter-only in this version)").
- **Spine:** **CAN PROVE only in the narrowest one-hop case; else REJECT.** The spine relaxes this **iff**
  all hold: (i) the enclosing object is a bare-local, spine-proven `pt=frame` cell (one hop —
  `owner.field`, matching `escape.tks:78-83,169-178`); (ii) the stored ref's referent (`bf`) is a
  `param(i)` or a local whose block **encloses** the object's block (`ref_target_outlives`); (iii) the
  object is `us=unique` (no alias can outlive it carrying the dangling ref). If the object escapes
  (`pt=root/⊤`), or the field path exceeds one hop, or the referent is a narrower-scoped local, the
  outlives proof fails ⇒ **REJECT** (gate stays). This is the stored-borrow guarantee and it is
  deliberately tiny: only a unique, frame-local, one-hop container holding a borrow of something that
  outlives it.
- **Gate tied to:** `typer.tks:2512`. **RELAX under proof; KEEP otherwise.**

**Summary of the map:** (a) relax-if-unique / reject-if-shared; (b) **reject forever**; (c)
relax-under-one-hop-outlives / reject otherwise. Two of three hard cases are dominated by REJECT — which
is why the verdict is "LAYER, with a hard scope," not "LAYER, ship everything."

---

## §2c.5 — Feasibility verdict

**FEASIBLE as a LAYER — but the shipped guarantee is narrow and honest, not free.**

- The lattice **can** be built as an additive per-function fixpoint over a finite cell universe (§2),
  soundly beside the untouched name set (`escape.tks:11-12,14,284`). The representation question ("layer
  or replace") resolves cleanly to LAYER.
- **What ships:** local ref-to-local binding within one function (`typer.tks:2450` relaxed under
  `ref_target_outlives`); `mem::free` affine safety keyed on `us=unique` (the one real native gap,
  `memory-unsafe-backend-remodel.md:24-25`); and the narrow one-hop stored-borrow (`typer.tks:2512`
  relaxed only for unique frame-local one-hop containers).
- **What does NOT ship, ever, under one-function+one-hop:** returned borrows (`typer.tks:3169` R3 —
  the caller frame is invisible); refs in collections/variants/generics/fn-params (`resolve.tks:1168,1185,1213,1113,1403`
  — no single referent anchor); nullable refs (`resolve.tks:1233` — orthogonal value rule). These
  blanket gates **stay unconditional**.

**Fallback (if the BUILD proves the lattice unsound in practice):** the blanket gates stay exactly as
today (`resolve.tks:1168,1185,1213,1233,1113` and `typer.tks:2450,2512,3169`), and **stored borrows,
`mem::free` on possibly-aliased targets, and channel-send-move do NOT ship.** This is the honest ceiling
of `memory-unsafe-backend-remodel.md:47-49`: 0 cliffs closed until the spine is *proven*, and pretending
the fact is free is the only dishonest option. The recon does not pretend it is free — it scopes the win
to three narrow relaxations and names two whole classes (returned + collection-stored borrows) as
permanent REJECTs.

---

## Required output — downstream BUILD (#331) classification

**SHARED-CODEGEN-TOUCHING is the wrong label; the correct label is ADDITIVE-BUT-GATE-TOUCHING.**

Precisely:

- The spine query itself (`fn_spine`, `ref_target_outlives`) is **ADDITIVE** — a new function beside the
  untouched `fn_escaping_vars`. It changes **no** existing `mark_*` code and **no** codegen routing site
  (`codegen.tks:5555,5564,5623,6072,6075,6277` all keep reading the unchanged name set). By that part
  alone, fixpoint (gen1==gen2) is preserved: adding a *new* pure query does not change generated C.
- **HOWEVER**, the BUILD *also* relaxes three type gates (`typer.tks:2450,2512`; guarded `resolve`/`typer`
  sites) so that programs previously **rejected** now **compile**. That changes the accepted language and
  therefore the emitted output for newly-accepted programs. Those relaxations are read by the checker and
  gate what codegen ever sees. **Relaxing a gate is a shared-checker change** even though it adds no
  codegen branch.

**Therefore the BUILD (#331) requires the FULL shared-checker discipline:** full gate (C + self-host +
native) + FIXPOINT (gen1==gen2 byte-identical) + `diff_vm_native` (`scripts/diff_vm_native.sh`,
VM==native on every regression) + **independent review**. The build is **NOT** trivially additive —
the query is additive, but the gate relaxations it enables are shared-checker-touching. This recon
records that requirement so #331 inherits it (issue #330 §5 "the downstream build is NOT additive").

---

## Required output — regression fixtures the BUILD will need

Under `examples/regressions/`, following the `examples/regressions/mem_free/` pattern
(`mem_free.tkp` with `name`/`source`/`[artifact] kind = "binary"`; a `main.tks`; a `src/<mod>/work.tks`;
exit code is the observable, VM==native asserted by `scripts/diff_vm_native.sh`). Each is named with the
gate it exercises:

1. **`stored_borrow_outlives_referent`** (MUST STAY REJECTED — the counter-example).
   A `Ref` stored into a field of an object that **outlives** the referent (e.g. a frame-local struct
   holds a borrow of a narrower-scoped local, or the container escapes to root). Expected: the compiler
   **REJECTS** it with the `typer.tks:2512` message ("a reference cannot be stored in a field …"). This
   fixture proves the spine's stored-borrow relaxation did **not** over-accept. Exit: non-zero compile
   error (build fails, no binary); it is a *compile-reject* fixture, not a run fixture — assert the
   `teko build`/`teko run` both fail with the escape-gate diagnostic.

2. **`stored_borrow_sound`** (MUST BE ACCEPTED — the positive case the spine unlocks).
   A `Ref` bound to / stored under a unique, frame-local, one-hop container whose referent is a
   parameter that provably outlives it. Expected: **compiles**, runs, VM exit == native exit == a fixed
   sentinel (e.g. reads through the borrow and returns a known sum, mirroring the `mem_free` exit-code
   pattern). This proves the spine actually relaxed `typer.tks:2450/2512` for the sound shape.

3. **`ref_returned_rejected`** (MUST STAY REJECTED — the permanent-KEEP guard).
   A function that returns a `Ref`. Expected: **REJECTED** with `typer.tks:3169` ("a function cannot
   return a reference (pass-down only)"). Guards §2c.4(b) — the one-function bound must never relax this.

4. **`free_aliased_rejected`** (MUST STAY REJECTED — the `us=shared` affine guard).
   `let y = xs; teko::mem::free(xs)` where `xs` is aliased by `y`. Expected: **REJECTED** by the affine
   `free` gate because the spine proves `us(xs)=shared`. Guards §2c.4(a) — the one real native UAF gap
   (`memory-unsafe-backend-remodel.md:24-25`) must not be freed when aliased. Pairs with the existing
   `examples/regressions/mem_free/` (which frees a *unique* slice and must keep PASSING).

5. **`ref_in_collection_rejected`** (MUST STAY REJECTED — the KEEP-forever anchor).
   A `[]Ref<T>` or a variant carrying a `Ref`. Expected: **REJECTED** at `resolve.tks:1168`/`:1185`.
   Guards that the blanket collection/variant gates were **not** touched by the BUILD.

Fixtures 1, 3, 4, 5 are **compile-reject** fixtures (the observable is "build fails with the named
diagnostic"); fixture 2 is a **run** fixture (VM exit == native exit == sentinel). All follow the
`mem_free` project layout.

---

## Verification (doc-only)

This change is a single `.md` file. `git diff --stat origin/remodel/memory-unsafe-backend -- '*.tks' '*.tkt'`
is EMPTY (confirmed). Doc-only ⇒ fixpoint (gen1==gen2) preserved by construction, no `.tks` delta;
`diff_vm_native` unaffected. No full build gate is required for this recon.

---

## No unresolved HALT

No genuine design fork remained that the code + Laws could not resolve. The one judgment call — whether
the returned-borrow case (§2c.4b) is "unproven" or "un-formulable" — is resolved by the Laws: the design
rules out whole-program analysis (`memory-unsafe-backend-remodel.md:57-58`), and a per-function analysis
*cannot name* the caller frame, so R3 (`typer.tks:3169`) is a permanent KEEP, not a deferred relax. This
is stated as a scope limit in the verdict, not hidden.
