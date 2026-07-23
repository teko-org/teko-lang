# `ref` / `Ref<T>` → pointers (`*T` / `&x` / `*p`) — deep feasibility analysis

> **Status: ANALYSIS + RECOMMENDATION for the owner to decide.** Author: architect
> (decision-support). Umbrella: `remodel/emit-throughput` (62d59a1), branch
> `feat/0.3.0.30-regressives-c0-c2`. This document does NOT touch product code and
> is NOT a crumb sequence — it is the reasoned answer to the dev feedback that
> teko's reference model is "more complicated and too verbose," and whether teko
> should replace `ref`/`Ref<T>` with C/Zig/Go-style pointers.
>
> **Headline recommendation up front:** DO NOT replace teko's reference *semantics*
> with C/Zig/Go pointer semantics — that discards the language's entire
> constitutional pitch (no-GC, arena-safe, static-checked). The verbosity complaint
> is real but is a **surface/spelling** problem, and it already has a **ratified,
> unbuilt** fix: the transparent `ref` model (`docs/design/ref-transparent-model.md`,
> owner-ratified 2026-07-11/13). Build that first. IF the owner specifically wants
> pointer *spelling* (`*T`), it is admissible ONLY as a pure rename of the **safe**
> reference (spine kept, runtime unchanged — `Ref<T>` is already a bare `T*`), and it
> must be reconciled with the existing `ptr<T>` **unsafe** tier so the safe/unsafe
> boundary the whole memory model rests on is not blurred. Either way this is a
> 0.3.1 "Safety Wave" surface decision — **too big for 0.3.0.30** (null-union C3–C7 is
> mid-flight on this very surface).

---

## 1. The AS-BUILT model (read from the tree, not assumed)

### 1.1 The decisive fact: teko's reference is ALREADY a pointer at runtime

The entire "is this even a big change?" question collapses on one finding. At the C
codegen level teko's safe reference is *already* the exact C pointer triad:

| Teko surface (today) | Lowers to (C backend) | Cite |
|---|---|---|
| type `Ref<T>` | bare `<T> *` (a raw C pointer, no wrapper, no tag) | `codegen.tks:1032`, `codegen.tks:1867` |
| `&x` (safe borrow, AL wave F1) | `&x` (C address-of) | `codegen.tks:6171-6177` |
| `r.value` (deref) | `(*(r))` | `codegen.tks:3157-3164` |
| call-site pass of a `mut` local to a `Ref<T>` param | implicit `&x` auto-ref | `codegen.tks:3086-3091` |

So **teko already IS "pointers" physically.** `Reference` is not a fat/checked
reference at runtime; it is `T*`. The auto-ref at call sites even makes `&`
*implicit* already. What distinguishes teko from C is **not** the representation —
it is the **static safety layer** (§1.3) plus the **surface spelling** (`Ref<T>` +
`.value` instead of `*T` + `*p`).

**Consequence that governs the whole analysis:** switching to pointers cleanly
factors into two INDEPENDENT changes, and they must not be conflated:

- **(S) Spelling** — `Ref<T>` → `*T`, `.value` → `*p`, `&x` → `&x` (already there).
  Pure surface. Zero runtime change. Zero safety change.
- **(M) Meaning/semantics** — drop the spine + escape gates + arena-lifetime rules
  and let `*T` be an unchecked C/Zig pointer. This is the part that would destroy
  the language thesis.

The dev's complaint is entirely about (S). Nothing about (S) requires (M).

### 1.2 Two surfaces exist, and only ONE is what the dev is complaining about

**As-built today (the 0.3.0.30 tree):** the compiler's own corpus uses the
**explicit `Ref<T>` + `.value`** surface. Verified: `ref x:` (the transparent
binding modifier) appears **0 times** in `src/`. The transparent-`ref` model is
**designed, owner-ratified, and UNBUILT.**

**Already-ratified but unbuilt (`ref-transparent-model.md`):** the transparent
`ref` model replaces `Ref<T>`/`.value` with:
- `ref x: T` as the binding modifier (no angle brackets),
- `ref` as the type-position spelling (`r: ref T`, `[]ref T`) — `Ref<>` becomes
  compiler-internal only,
- **type-directed auto-deref** — `ref T` "acts as `T`", so `.value` **disappears
  entirely** (`ref-transparent-model.md` §1, §4).

This matters enormously for the owner's question: **the two ergonomic flanks the
dev names ("verbose", "complicated") are precisely the two flanks the ratified
transparent model already closes** — no `.value` (verbosity), and no deref-sigil
precedence war (the "Lisp of parentheses" it explicitly calls out, §1). A pointer
switch is being proposed against a baseline that teko has *already decided to
abandon*; measured against the ratified target, most of the verbosity is gone
without any pointer decision.

### 1.3 The safety-spine — what actually makes `Ref<T>` more than a raw pointer

The difference between teko's `T*` and C's `T*` is 100% static, in two layers:

- **Resolve-time escape gates** (`resolve.tks`): a `Reference` may not be stored in
  a struct/variant/collection (`resolve.tks:1524-1528`), may not be a generic type
  argument (`:1404`), may not be a function-type parameter (`:1599`), may not target
  another reference (`Ref<Ref<T>>`, `:1773`), and (bare) may not be nullable
  (`:1616-1619`, now relaxed to the `Ref<T> | null` niche form — §4).
- **The inferred spine** (`spine.tks`): a bounded per-function points-to/uniqueness
  lattice — `Cell` × `PointsTo` (`PtFrame|PtRoot|PtParam|PtAdopter|PtTop`) ×
  `BorrowedFrom` (`BfNone|BfParam|BfLocal|BfTop`) × `Unique`
  (`UsUnique|UsShared|UsTop`), with `is_unique_at` / `ref_target_outlives`. This is
  the machinery that keeps a borrow from outliving its referent, gates `mem::free`
  to unique-owned targets, and seeds `BfLocal` at each `&x` site
  (`spine.tks:1411-1436`). It is L1 of the memory remodel — "the whole safety bet"
  (`memory-unsafe-backend-remodel.md:40`).
- **The arena model** (`memory-unsafe-backend-remodel.md`): no-GC, bump-arena region
  tree, `adopt` for cyclic bulk-drop, `unsafe`-by-type + `ptr<T>` as the raw floor.
  R11: "a `ref` is valid only while the arena holding its target is live."

`adopt`, `#must_free`, and the `unsafe`/`ptr<T>` tier are all **orthogonal to the
reference spelling** — they key off the arena tree and nominal `unsafe` marking, not
off whether the safe reference is spelled `Ref<T>` or `*T`.

### 1.4 The unsafe raw-pointer tier ALREADY EXISTS — `ptr<T>`

Critically for any "add pointers" proposal: teko **already has a raw pointer type**,
`ptr<T>` (`Ptr{inner}` internally), reserved for `unsafe`/FFI/Marshall
(`typer.tks:505-520` region-alloc returns `ptr<T>`; `resolve.tks:723-728`
opaque-ptr degrade; `marshall-spec` is the `ptr`↔`ref` crossing). `src/` has 34
`unsafe fn`/`unsafe type`/`unsafe #` sites. So teko's memory ladder is already
**two-tier**: `Ref<T>` (safe, spine-checked, lowers to `T*`) above `ptr<T>` (unsafe,
raw, `unsafe`-only). **A `*T` pointer spelling would land on top of a slot that is
already occupied.** This is the single sharpest design constraint on any pointer
proposal (§5, §7).

---

## 2. The verbosity evidence (quoted + counted)

### 2.1 Counts (grep, `src/` unless noted)

| Signal | Count | Note |
|---|---|---|
| `Ref<` occurrences in `src/**/*.tks` | **96** | the surface under complaint |
| `Ref<` across the whole repo (incl. docs/tests) | **373** in **69 files** | |
| distinct `Ref<XxxCursor>` shapes in `src/iter` + `src/io` | **~15** | the iterator idiom |
| `ref x:` transparent binding modifier in `src/` | **0** | ratified surface UNBUILT |
| `.value` in `src/**/*.tks` (all fields) | **641** | the Ref-deref subset is a minority; most are ordinary struct fields (`IntIndexed.value`, etc.) — a blind rename is unsound (`safety-spine.md` B1.3 warns 652 `.value`, majority non-Ref) |
| `&`-prefix borrow lines in `src/` (incl. doc) | ~38 | real first-class `&x` sites are few; the call-path auto-ref makes `&` mostly implicit |
| `unsafe fn`/`type`/`#` in `src/` | 34 | the existing `ptr<T>`/`unsafe` tier |

### 2.2 The actual verbose sites (quoted)

The iterator/stream idiom is where `Ref<T>` + `.value` is densest —
`src/iter/int_iter.tks`:

```teko
pub fn over_array(xs: []i64, cur: Ref<ArrayCursor>) -> IntIter {
    // ...
    if cur.value.pos >= xs.len { return null }
    let v = xs[cur.value.pos]
    cur.value = ArrayCursor { pos = cur.value.pos + 1 }
    // ...
}
```

Four `cur.value` in five lines — this is the dev's "verbose." Under the **already
ratified** transparent model (`ref-transparent-model.md` §4), the same reads:

```teko
pub fn over_array(xs: []i64, cur: ref ArrayCursor) -> IntIter {
    if cur.pos >= xs.len { return null }        // auto-deref: `.value` gone
    let v = xs[cur.pos]
    cur = ArrayCursor { pos = cur.pos + 1 }      // write-through, still R4
}
```

The `.value` noise vanishes and `Ref<ArrayCursor>` becomes `ref ArrayCursor`. This
is the verbosity fix already on the books.

### 2.3 What is genuinely NOT verbosity — it is the safety tax

The `Ref<T>` escape ceremony the dev may also be feeling (`a reference cannot be
stored in a struct/variant/collection`, `Ref<Ref<T>>` rejected, the spine's
reject-conservative honest-stops) is **not spelling** — it is the static safety
guarantee. No spelling change removes it, and a pointer model that *did* remove it
would be removing the language's reason to exist (§3). Distinguishing these two is
the crux of the recommendation.

---

## 3. Candidate pointer models and their safety trade-offs

### 3.1 C-style raw `*T` (unchecked)

- **Ergonomics:** `*T` / `&x` / `*p`. Familiar, terse.
- **Safety LOST:** everything in §1.3. No escape gate (a `*T` freely stored in a
  struct/collection/returned — the exact §7 soundness hole `ref-transparent-model.md`
  documents becomes *unchecked*), no `is_unique_at` guard on `mem::free` (the aliased
  UAF the whole remodel exists to close, `memory-unsafe-backend-remodel.md:24`, goes
  wide open), no arena-lifetime binding. Dangling-by-accident becomes normal.
- **Verdict: REJECT as the safe default.** This is model (M) — it deletes the thesis.
  It is exactly what teko *already offers behind `unsafe`* via `ptr<T>`; making it the
  default surface would demote the whole language to C. **Constitutional conflict —
  the one genuine HALT-level tension (§8).**

### 3.2 Zig-style `*T` / `?*T` / `[*]T`

- **Ergonomics:** explicit, honest sigils; `?*T` optional-pointer, `[*]T`
  many-pointer. Ergonomic and much *safer-feeling* than C (no implicit decay,
  explicit optionality).
- **Safety:** still fundamentally **unchecked for lifetimes/aliasing** — Zig has no
  borrow/escape analysis; a `*T` can outlive its pointee. Zig leans on allocators +
  defer + programmer discipline, not a static spine. Adopting Zig *semantics* still
  loses teko's escape gates and spine.
- **The one genuinely useful import:** `?*T` (optional pointer as a NULL niche) is
  **exactly** what teko's null-union already produces for `Ref<T> | null` (§4) — teko
  arrived at the Zig optional-pointer niche independently and already ratified it.
- **Verdict: REJECT the semantics; teko already has the one good idea (`?*T` niche).**

### 3.3 Go-style `*T` (GC-backed)

- **Not applicable.** Go's `*T` is safe *only because a tracing GC keeps the pointee
  alive* — escape analysis in Go is a performance optimization, not a safety
  mechanism; anything that might outlive its frame is heap-allocated and the GC
  reclaims it. Teko is **constitutionally no-GC** (`memory-unsafe-backend-remodel.md`
  §1: "No GC — ruled out"; a region-GC "does not stay minimal"). Without a GC, Go's
  model provides **no** safety. Reject as inapplicable — note it only to close it.

### 3.4 "Safe pointer" — pointer SPELLING over the retained spine (the only viable candidate)

- **Idea:** change only (S). `*T` becomes the surface name for today's `Ref<T>` (=
  `ref T`); `&x` stays the address-of (already present); `*p` becomes the deref (or is
  elided by transparent auto-deref). The **spine, escape gates, arena-lifetime rules,
  copy-on-attach, never-null bare ref, `adopt`, and the `ptr<T>` unsafe tier are ALL
  retained unchanged.** Runtime is already `T*`, so codegen is untouched.
- **Safety:** **fully preserved** — this is a rename, not a semantic change. The spine
  runs identically; `escaping_value_leaks_ref`, `is_unique_at`, `ref_target_outlives`
  don't care whether the thing is spelled `Ref<T>` or `*T`.
- **Cost:** parser + type-renderer + diagnostics churn, the whole-corpus rename, and —
  the real design cost — **reconciling `*T` (safe) against `ptr<T>` (unsafe)** so two
  pointer spellings don't confuse the safe/unsafe boundary (§5).
- **Verdict: the ONLY candidate that passes all Laws.** But note it is *strictly
  worse than the already-ratified transparent-`ref` model* on one axis (below), which
  is why the recommendation is "transparent-ref first, pointer-spelling only if the
  owner still wants it."

### 3.5 Why the transparent-`ref` model beats pointer-spelling head-to-head

Both (3.4) and the ratified transparent model (`ref-transparent-model.md`) keep the
spine and kill `.value`. The difference:

| Axis | transparent `ref` (ratified) | pointer-spelling `*T` |
|---|---|---|
| Deref sigil | **none** (type-directed auto-deref) | `*p` — reintroduces a deref sigil competing on precedence (the "Lisp of parentheses" §1 explicitly rejects), unless auto-deref is *also* kept — in which case `*p` is redundant |
| Collision with `ptr<T>` | none (`ptr` stays the unsafe raw spelling; `ref` the safe) | **direct** — `*T` vs `ptr<T>` both read as "pointer"; blurs safe/unsafe |
| Owner status | **ratified** (2026-07-11/13), designed, 6 amendments closed | un-designed; would re-open a closed decision |
| Nullable form | `ref x: T \| null` ≡ `Ref<T\|null>` (D4, already ruled) | would need a fresh `?*T`/`*T?` precedence ruling |
| `&x` address-of | already exists (AL wave F1) | already exists — no gain |

Pointer-spelling's only advantage is *C/Zig familiarity*; it *loses* on the deref
sigil and the `ptr<T>` collision, and it *discards ratified work*.

---

## 4. NULL-union interaction — the pointer model SUBSUMES nothing new here

The just-landed null-union work (`null-as-union-type.md`,
`null-union-c3-c7-0.3.0.30.md`) already delivers the pointer-model's headline null
story:

- **`Ref<T> | null` niche-fills to a bare `T*` with `NULL` meaning `null`**
  (`null-union-c3-c7` C3, `cg_union_niche_member`). That is *exactly* Zig's `?*T` /
  a nullable C pointer — one word, `0` = absent — **already ratified and being
  implemented right now**, on the C3–C7 crumb line.
- **Bare `Ref<T>` stays never-null** (R2 kept, D3); `Ref<T> | null` is the ONLY
  nullable reference and the UNIVERSAL narrowing invariant (§3f/§3d of the base) is
  the fence that keeps the bare-ref promise sound (must narrow before deref).

**So the answer to "does a pointer model subsume part of the null-union work?" is:
NO — it is the reverse. The null-union work already IS the nullable-pointer model for
reference types** ("a nullable pointer IS `T | null` for reference types" is
literally what C3 ships). A pointer switch would be *renaming a niche that already
exists*, gaining nothing.

**Does the ongoing null-union corpus pivot (`null-union-c3-c7-0.3.0.30.md`) need to
account for a pointer model? NO — and it must NOT be perturbed.** Its risk R-1
already ruled that representation (niche/inline/box) is a purely physical dial
orthogonal to the reference's semantic category and spelling. Whether the safe
reference is later spelled `Ref<T>`, `ref T`, or `*T`, the niche `X | null → bare X`
rule is spelling-independent. **Recommendation: leave the null-union pivot exactly as
sequenced; a future spelling change is a pure surface rename layered on top, and the
`cg_union_niche_member` niche authority already anticipates the `Reference` member.**
The only forward-compat note worth recording: if `*T` is ever adopted, `*T` and
`ptr<T>` must BOTH map to the same niche classifier arm they already have
(`Reference`/`Ptr` are both niche members in C3) — no new rep path.

---

## 5. The `*T` vs `ptr<T>` collision — the load-bearing surface tension

This is the design problem a pointer proposal *must* solve, and it is the reason the
change is non-trivial even as a pure rename:

- Today: `ref`/`Ref<T>` = **safe** (spine-checked, safe world); `ptr<T>` = **unsafe**
  (raw, `unsafe`-only, Marshall boundary). The safe/unsafe split is **the spine of
  the memory model** (`memory-unsafe-backend-remodel.md` §2: unsafe is a total,
  grepable, assumed property).
- A `*T` spelling reads to every C/Zig/Rust programmer as "raw pointer" — i.e. as
  the *unsafe* thing. But the proposal wants `*T` to be the *safe* reference. This
  **inverts the intuition** the unsafe tier depends on: a dev seeing `*T` would assume
  C semantics (no guarantees) when in fact the spine is enforcing safety — or, worse,
  would assume the spine is off when writing `*T` and be surprised by escape rejects.
- Three ways out, all with costs:
  1. **`*T` = safe reference, keep `ptr<T>` unsafe.** Two pointer spellings; the
     safe one wears the "dangerous-looking" sigil. Confusing but codegen-cheap.
  2. **`*T` = safe, rename `ptr<T>` → something louder (`raw<T>`/`unsafe *T`).**
     Cleaner mental model, but churns the FFI/Marshall surface too.
  3. **Unify `*T` for both, distinguished by `unsafe` context.** DANGEROUS — makes
     the single most safety-critical distinction *contextual* rather than *spelled*,
     directly against the "unsafe is a grepable total property" law. **Reject.**

There is no free reconciliation. The transparent-`ref` model sidesteps this entirely
by keeping `ref` (safe) and `ptr` (unsafe) as *distinct words* — which is a further
reason to prefer it.

---

## 6. Migration cost, blast radius, seed-sequencing

### 6.1 Blast radius (either transparent-`ref` or `*T` spelling)

- **Parser:** a new type-spelling (`*T` or the already-planned `ref` grafia) + a
  deref form. `&x` borrow already parses (AL wave F1). Contextual-keyword discipline
  applies (`memory-unsafe-backend-remodel.md` §3).
- **Checker:** `resolve.tks` type-spelling → `Reference` mapping; `type_render`
  diagnostics; the `.value` → auto-deref path in `typer.tks`. **The spine and escape
  gates are UNTOUCHED** (they key off `checker::Reference`, not the surface).
- **Codegen:** **untouched** — `Reference → T*`, `&x → &x`, deref `→ (*p)` already
  exist. This is the single biggest de-risk: no runtime/ABI change.
- **Corpus:** 96 `Ref<` sites + the Ref-subset of 641 `.value` (type-checker-driven
  rewrite ONLY — `safety-spine.md` B1.3: a blind `sed` on `.value` is unsound because
  most `.value` are ordinary fields).

### 6.2 Single wave or staged? — STAGED, and it is the transparent-ref migration

This is not a big-bang. `ref-transparent-model.md` §11 and `safety-spine.md` B1.3
already specify the exact staged, stable-seed-safe dance:

1. Add the new spelling + auto-deref; keep `.value`-on-`Ref` as a **deprecated but
   accepted alias** (parser accepts both). No `src/` change yet.
2. Ship a seed carrying the new spelling + transparency.
3. Migrate `src/` (type-checker-driven), fixpoint + `diff_vm_native` at each step.
4. Remove the deprecated `.value`-on-`Ref` acceptance.

A `*T` pointer spelling would ride the *same* four-step dance (add `*T` as an accepted
alias of `Ref<T>` → seed → migrate → drop `Ref<T>`), i.e. it is *strictly more work
than* transparent-ref because it *adds* the `ptr<T>` reconciliation (§5) on top.

### 6.3 Seed-sequencing implication (the bootstrap gotcha)

**Yes — the compiler's own use of the new spelling needs a new seed.** The stable
seed must be able to *parse* `*T`/`ref T` before `src/` may *use* it
(`ref-transparent-model.md` §11 "Gotcha stable-seed"). That is precisely why the
deprecated-alias step exists: the parser accepts both forms across one seed
generation, `src/` migrates, then the old form is dropped. The corpus must never use
a spelling the seed cannot parse (standing bootstrap law). This is identical to the
null-union seed-sequencing already in flight (`null-union-c3-c7` §0.1 "Seed-sequencing
law").

### 6.4 Sizing verdict — too big for 0.3.0.30

0.3.0.30 is mid-wave on **this exact surface**: null-union C3–C7 is actively
rewriting the reference/optional surface (77 `error?` sites, `Ref<T> | null` niche,
the narrowing invariant). Opening a parallel `Ref → *T` rename would collide on
`resolve.tks`, `codegen.tks`, `type.tks`, and the whole corpus, and would fight the
in-flight fixpoint gates. **Do not.** The natural home is the **0.3.1 "Safety Wave,"
Block 1 (surface)** — `safety-spine.md` already scopes the transparent-`ref` surface
migration there, alongside `-> void` removal (#497). Any pointer-spelling decision
belongs in that same Block 1, folded into the one surface migration — never a second
parallel one.

---

## 7. Ergonomics: BEFORE vs AFTER on real corpus, with teko wrinkles

`src/iter/int_iter.tks` `over_array` (real):

```teko
// TODAY (as-built, the dev's complaint)
pub fn over_array(xs: []i64, cur: Ref<ArrayCursor>) -> IntIter {
    if cur.value.pos >= xs.len { return null }
    cur.value = ArrayCursor { pos = cur.value.pos + 1 }
}

// RATIFIED transparent `ref` (already on the books, kills the verbosity)
pub fn over_array(xs: []i64, cur: ref ArrayCursor) -> IntIter {
    if cur.pos >= xs.len { return null }
    cur = ArrayCursor { pos = cur.pos + 1 }
}

// POINTER-SPELLING `*T` (the proposal) — with EXPLICIT deref
pub fn over_array(xs: []i64, cur: *ArrayCursor) -> IntIter {
    if (*cur).pos >= xs.len { return null }     // `*p` deref sigil is BACK
    *cur = ArrayCursor { pos = (*cur).pos + 1 }
}
```

The pointer-with-explicit-deref form is **more verbose than the ratified transparent
model**, not less — `(*cur).pos` is noisier than `cur.pos`. The pointer form only
matches transparent-ref if it *also* adopts auto-deref, at which point the `*` sigil
is redundant and you've reinvented `ref` with a scarier spelling.

**Teko-specific wrinkles a pointer model stumbles on:**

- **Arena lifetimes (R11):** a `*T` invites the C mental model "this pointer is
  valid until freed." Teko's is "valid while the arena holding the target is live."
  The `ref`/`Ref` spelling carries no false promise; `*T` actively mis-signals.
- **Value-vs-reference categories (tripartite law):** teko has value semantics with
  reference *categories* (`teko-ref-model-digest.md`) — `let` snapshots, `mut` local
  value, `ref` alias. `*T` collapses the reference into "pointer," losing the crisp
  binding-modifier reading (`let`/`mut`/`ref` on the binding, not a type sigil).
- **Copy-on-attach (R5) / never-null (R2):** `ref x: T = <value>` *wraps a value*
  (copy-into-arena); `*p = &value` in C is aliasing. The pointer spelling would
  invite the aliasing reading and undercut copy-on-attach.
- **The `ptr<T>` collision (§5).**

---

## 8. Safety verdict + the one HALT-level tension

- **Keep the spine, keep the thesis:** any admissible change is (S) spelling only.
  The minimum that MUST be kept: the escape gates (`resolve.tks` Reference rules), the
  spine (`is_unique_at`, `ref_target_outlives`, `bf`/`pt`/`us`), arena lexical
  lifetimes (R11), copy-on-attach (R5), never-null bare ref (R2 + the null-union
  narrowing fence), and the `unsafe`/`ptr<T>` raw tier + `adopt` (both survive
  untouched — they are spelling-independent).
- **The genuine HALT-level tension** is narrow and worth stating plainly: **if the
  owner's intent is to replace teko's reference with *raw, unchecked* C/Zig pointer
  *semantics* as the default safe surface, that fundamentally conflicts with the
  Constitution** (no-GC + arena-safe + static-checked is teko's reason to exist; the
  memory remodel ruled out GC and RC precisely to keep this). You cannot have
  "C-style unchecked pointers as the default" AND "teko's safety pitch." **Resolved
  law-first: the safety thesis is constitutional, so pointer *semantics*-replacement
  is rejected; only pointer *spelling* over the retained spine is admissible.** No
  owner HALT is required to make *that* call — the Laws decide it. The HALT flag is
  only tripped if the owner explicitly wants the unchecked-semantics reading; in that
  case this is a Constitution-level decision, not an architecture one, and it stops
  here for the owner.

---

## 9. RECOMMENDATION

1. **Do NOT switch to C/Zig/Go pointer *semantics*.** It deletes the language thesis;
   Go's model needs a GC teko constitutionally forbids; C/Zig raw pointers are what
   `ptr<T>` already provides behind `unsafe`. (§3.1–3.3, §8.)

2. **Address the dev's verbosity complaint by BUILDING the already-ratified
   transparent-`ref` model** (`ref-transparent-model.md`, owner-ratified
   2026-07-11/13). It kills `.value` (the verbosity) and the deref-precedence war
   (the "complicated"), keeps the spine, keeps `ref`/`ptr` as distinct safe/unsafe
   words, and its nullable form is already ruled (D4). This is the highest-value,
   lowest-risk move and is *already decided* — it just needs implementation in the
   0.3.1 Safety Wave, Block 1. (§1.2, §3.5, §7.)

3. **IF the owner specifically wants pointer *spelling* (`*T`)** despite (2): admit it
   ONLY as a pure rename of the **safe** reference — `*T` ≡ today's `Ref<T>`/`ref T`,
   spine unchanged, runtime already `T*`, codegen untouched — AND resolve the
   `*T`-vs-`ptr<T>` collision explicitly (§5; recommended sub-option: keep `ptr<T>`
   unsafe, do NOT contextualize `*` by `unsafe`). Note it is *strictly more work than*
   (2) and slightly *more verbose* unless it also adopts auto-deref (at which point the
   `*` sigil is redundant). Fold it into the SAME Block-1 surface migration — never a
   parallel wave.

4. **Wave:** NOT 0.3.0.30 (null-union C3–C7 owns this surface mid-flight; §6.4).
   Target **0.3.1 "Safety Wave," Block 1 (surface)**, where the transparent-`ref`
   migration + `-> void` (#497) already live. One surface migration, staged with the
   four-step deprecated-alias/seed dance (§6.2–6.3).

5. **Null-union:** leave `null-union-c3-c7-0.3.0.30.md` exactly as sequenced. It
   already delivers the nullable-pointer story (`Ref<T> | null` niche = bare `T*`,
   NULL=null); a pointer model subsumes nothing there. Only forward-compat note: any
   future `*T` must reuse the existing `cg_union_niche_member` `Reference`/`Ptr` niche
   arm — no new rep path. (§4.)

6. **What is kept from the safety-spine in every admissible path:** ALL of it — the
   escape gates, the spine lattice, arena lifetimes, copy-on-attach, never-null bare
   ref, `adopt`, and the `unsafe`/`ptr<T>` tier. The change is surface-only or it is
   rejected.

**HALT flag:** raised ONLY if the owner's intent is raw *unchecked* pointer semantics
as the default (§8) — that is a Constitution-level call. If the intent is ergonomics
(as the dev feedback reads), no HALT: build transparent-`ref`; pointer-spelling is an
optional, later, surface-only rename with a known `ptr<T>` reconciliation cost.

---

*Sources (all read from the tree at authoring time): `src/codegen/codegen.tks`
(1032, 1867, 3086-3091, 3157-3164, 6171-6177 — the `Reference → T*` / `&x` / `(*p)`
lowering), `src/checker/spine.tks` (the lattice + `is_unique_at`/`ref_target_outlives`),
`src/checker/resolve.tks` (the escape gates, 1404/1524/1599/1616/1773),
`src/checker/typer.tks` (`type_borrow_expr` 2565, `ref_borrow_or_error` 4882,
`ptr<T>` region-alloc 505), `src/iter/int_iter.tks` (the corpus idiom),
`docs/design/ref-transparent-model.md`, `docs/design/safety-spine.md`,
`docs/design/memory-unsafe-backend-remodel.md`, `docs/design/null-as-union-type.md`,
`docs/design/null-union-c3-c7-0.3.0.30.md`, `docs/memory/teko-ref-model-digest.md`.
Grep counts: `Ref<` 96 in `src/` (373 / 69 files repo-wide), `.value` 641, `ref x:`
0, `unsafe` decls 34.*
