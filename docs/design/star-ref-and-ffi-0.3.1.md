# `*`-sugar references + the FFI/reverse-FFI surface (design 0.3.1 — the Security/FFI wave)

> **Status:** DESIGN-AHEAD, doc-only. **NOT implemented — the owner reviews before any code.**
> Wave: **0.3.1 "Security/FFI"** (this is the security/FFI wave, NOT `0.3.0.30`). This document
> **SUPERSEDES THE SPELLING** (not the safety model) of `docs/design/ref-transparent-model.md`: the
> owner has ratified the direction — **remove the (planned) surface `ref` keyword, make `Ref<T>` a
> compiler-internal type, and replace the reference's grafia with a `*`-before-the-identifier sugar.**
> The entire soundness apparatus (the tripartite law, R1–R11, the A1–A6 spine, arena/R11 lifetimes,
> never-null, `Ref<T?>` transparency) is **carried over unchanged** — only the surface grafia of the
> already-existing reference changes.
>
> **Read for this design (all read at authoring time):** `docs/design/ref-transparent-model.md`
> (the ratified model + tripartite law + A1–A6 + §4.2 null-transparency), `docs/design/marshall-spec.md`
> (the `ptr`↔`ref` boundary, C0–C8), `docs/design/memory-unsafe-backend-remodel.md` (`unsafe`-by-type
> §2, the memory ladder), `docs/design/wave-0.3.1-plan.md` (the sub-wave/seed chain — SW3/SW4 are the
> hooks this doc re-spells), the **`ref-to-pointers-analysis-0.3.x.md`** feasibility analysis
> (recovered from commit `20d98fe`; it is the decision-support the owner has now ruled on), and the
> current surfaces in `src/` (`parse_type.tks`, `parse_decl.tks`, `parse_expr.tks`, `ast.tks`,
> `checker/resolve.tks`, `checker/type.tks`, `checker/typer.tks`, `codegen/codegen.tks`,
> `emit/header.tks`, `build/manifest.tks`, `teko.tkp`).
>
> **Governing laws cited:** M.0 (operators map to metal; keywords are precious), M.1 (fail early),
> M.2 (explicit direction), M.3 (honest — the safe/unsafe boundary is greppable and never hidden),
> M.4 (parser/checker cost), M.5 (you see the copy). Teko-only (2026-07-04), W15+FULL-JAVADOC
> (2026-07-05), stable-seed bootstrap law.

---

## 0. The as-built baseline (read from the tree — the load-bearing facts)

Two facts govern the whole design; both were verified in `src/`.

1. **The safe reference is ALREADY a bare `T*` at runtime.** `checker::Reference{inner}` lowers to a
   raw C `T *` (`codegen.tks` `Reference → T*`), `&x` lowers to C `&x`, `.value` lowers to `(*p)`,
   and a call-site pass of a live `mut` local to a `Reference` param is an implicit `&x` auto-ref.
   **So this whole change is surface-only.** No runtime, ABI, or codegen-representation change is
   implied by Part A — the spine, escape gates, and `Reference → T*` lowering are untouched.

2. **The reference EXISTS today — as `Ref<T>` — and has for ~10 versions; only its GRAFIA is open.**
   Be precise about what is and is not built, because it decides the fallback framing (A.9):
   - **There is no `ref` KEYWORD, and there never was.** The lexer has no `ref` token; `BindKind` in
     `ast.tks` is `enum { Let; Mut; Const }` (no `Ref` arm); `ref x:` / `ref t` as a binder or param
     appears **0 times** in `src/`. The transparent `ref`-keyword surface of
     `ref-transparent-model.md` is **designed but UNBUILT** — it would be **new construction**.
   - **But the REFERENCE itself is fully built and in daily use** — it is the generic type **`Ref<T>`**
     (**114 uses in `src/`**, owner-verified) + **`.value`** (the deref) + **`&`/auto-ref** (the
     borrow). This is the reference the compiler self-hosts on today.
   The consequence that governs A.9: **the only existing reference artifact is `Ref<T>` + `.value` +
   `&`.** BOTH candidate surfaces — (A) the `*name` sugar of this doc, and (B) the transparent
   `ref`-keyword of ref-transparent-model — are **equivalent-cost NEW CONSTRUCTION of a fresh grafia
   over that already-existing `Ref<T>`.** Neither "keeps an existing keyword" (there is none) and
   neither invents the reference (it already exists). **The real decision is purely the GRAFIA of the
   reference we already have** — `Ref<T>`/`.value` → `*name` (A) vs `Ref<T>`/`.value` → `ref`-keyword
   (B). Everything below Part A is grafia + lowering of that one settled semantic object.

The current pointer/unsafe surfaces (all confirmed present):

| surface | internal | notes |
|---|---|---|
| `Ref<T>` (generic type) + `.value` | `Reference{inner}` | the reference IN USE today (114 sites); never-null (R2); `Ref<T> \| null` niche merged (`resolve.tks:1503`) |
| `&x` (prefix) | `Borrow` → `Reference{T}` | AL-wave-F1 SAFE mutable borrow; lowers to C `&x` |
| `ptr<T>` / `ptr<void>` | `Ptr{inner:Type?}` | raw address; `unsafe`-carrying only when pointee unsafe (today) |
| `uptr` | `Uptr{}` | opaque word (carries `Arena.region`, D35) |
| `unsafe fn` / `unsafe type` | `is_unsafe` | declaration modifier, contagious by composition (§2 remodel) |
| `extern fn f(..) -> T = "sym" [from "lib"]` | `TFunction{is_extern,c_symbol}` | foreign fn, no body, raw C symbol, no mangling |
| `extern type Name` | `ExternBody{}` | opaque handle → `void *` |
| `params xs: []T` | trailing homogeneous variadic | Teko's own variadic; desugars to a slice |
| `[extern.libs.<os>]` in `teko.tkp` | link knobs | per-OS `-l` libraries |

---

# PART A — the `*`-sugar reference model

## A.1 The one rule that makes it unambiguous: `*` is disambiguated by POSITION, decided in the PARSER

The lexer already emits `Star` for `*` (multiplicative op / today's nowhere-in-safe deref). **The
lexer is unchanged and makes NO decision.** The *parser* decides which `*` it is, from the grammar
position it is standing in. There are exactly two positions, and they never overlap:

- **`*` at a DEFINITION-IDENTIFIER slot ⇒ the safe reference-sugar marker** (background type
  `Reference{T}`, spine-tracked, never a raw address). A "definition-identifier slot" is precisely:
  1. the **function name** slot — `fn *name(...)` (the fn RETURNS a reference),
  2. a **parameter name** slot — `*t: T` (a by-reference parameter),
  3. a **binding name** slot after `mut` — `mut *t: T` (a mutable reference binding).
- **`*` at the START of a TYPE ⇒ a REAL RAW POINTER `*T`** (unsafe; requires `unsafe` context). A
  "type position" is precisely: after `:` in a param/field/binding annotation, after `->` in a
  return, a generic argument, a slice element, or a union member — i.e. every place
  `parse_type_primary` runs.

The decisive property: **a definition-identifier slot and a type position are never the same place
in the grammar.** In `*t: *T` the first `*` binds the *name* `t` (safe by-ref param) and the second
`*` opens the *type* `*T` (raw-pointer pointee, forcing the enclosing fn to be `unsafe`). The parser
is already position-aware here — `parse_params` knows it is reading a NAME (then `:` then a
`parse_type`), and `parse_type_primary` knows it is reading a TYPE. No lookahead, no lexer hack, no
contextual-keyword table: the discriminator is **which recursive-descent function is on the stack.**

This is the exact resolution the `ref-to-pointers-analysis` §5 flagged as "the load-bearing surface
tension" (`*T`-safe-vs-`ptr<T>`-unsafe): **position, not appearance, is the discriminator.** `*name`
(a binder) is always safe; `*Type` (a type) is always the raw-pointer, always unsafe. They are
spelled with the same glyph but can never be confused because they can never occur in the same slot.

### A.1.1 Grammar deltas (additive; no new token)

| production | today | new (additive) |
|---|---|---|
| `parse_function` name | `fn NAME` | `fn [*] NAME` — an optional leading `Star` before the fn name sets `returns_ref` |
| `parse_params` param name | `NAME : type` | `[*] NAME : type` — an optional leading `Star` before the param name sets `by_ref` |
| binding (`parse_stmt` mut) | `mut NAME [: type] = e` | `mut [*] NAME [: type] = e` — leading `Star` sets `by_ref`; **illegal after `let`/`const`** |
| `parse_type_primary` | slice / fn-type / null / named | **+ a leading `Star` ⇒ `PtrType{pointee}`** (a real raw pointer; recurses so `**T` = `*(*T)`) |
| use sites | — | **no change** — reads/writes of a `*`-bound name are bare `t` (auto-deref, A.6) |

`BindKind` grows one arm: `enum { Let; Mut; Const; Ref }` (or, equivalently, a `by_ref: bool` flag on
`Binding`/`Param`/`FnDecl`; a flag is cheaper and avoids a second `Mut`-vs-`Ref` split since a
`*`-binding is *inherently* mutable — see A.4). Recommendation: **a `by_ref: bool` flag**, because a
`*name` is never `let`, so the tripartite `mut`-vs-`ref` distinction lives on the same `Mut` binder
plus the flag, not on a new `BindKind`.

`ast.tks` gets a new type node `PtrType{ pointee: TypeExpr }` in the `TypeExpr` variant, resolved by
the checker to `Ptr{inner}` (the SAME internal `Ptr` the current `ptr<T>` resolves to — §B.1
refines the two spellings). No new `Type` variant is needed on the checker side.

## A.2 The transform table (verbatim owner intent, made total)

```
fn func<T>(ref t: T | null) -> ref T2 | null { … }      // OLD (ref-transparent-model spelling)
fn *func<T>(*t: T | null) -> T2 | null { … }            // NEW  (the `*` on the name = "returns a ref";
                                                        //       the `*` on the param name = "by-ref")

ref t: T | null                                          // OLD (a mut ref binding)
mut *t : T | null                                        // NEW  (`mut` unlocks the binding; `*` makes it a ref)
```

Note the RETURN: `fn *func(...) -> T2 | null` — the `*` on the **name** carries "this fn returns a
reference"; the return TYPE is written `T2 | null` **without** a sigil (the reference-ness is on the
declared name, not repeated on the return type). This is the direct reading of owner-rule 1: **`*`
goes before the identifier being DEFINED** — and the fn's own name is the thing it defines.

## A.3 The definition-site vs type-position table (the complete disambiguation)

| written | slot | meaning | safe? |
|---|---|---|---|
| `fn *f(...)` | fn-name | `f` returns a reference (background `Reference{Ret}`) | SAFE |
| `*t: T` (in a param list) | param-name | `t` is a by-reference parameter | SAFE |
| `mut *t: T = e` | binding-name (after `mut`) | `t` is a mutable reference binding | SAFE |
| `let *t …` / `const *t …` | binding-name (after `let`/`const`) | **COMPILE ERROR** — no immutable reference (tripartite; A.4) | — |
| `x: *T` | type (after `:`) | `x` is a value of raw-pointer type `*T` | **UNSAFE** (requires `unsafe` ctx) |
| `-> *T` | type (after `->`) | returns a raw pointer | **UNSAFE** |
| `[]*T`, `Map<K, *V>` | type (container/generic arg) | container of raw pointers | **UNSAFE** |
| `*t: *T` | name `*t` + type `*T` | by-ref param whose pointee is a raw pointer | mixed: the `*t` is safe, the `*T` forces `unsafe fn` |
| `**t` (name) | binding/param name | **COMPILE ERROR** — a name carries at most ONE `*` (A.5) | — |
| `**T` (type) | type | `*(*T)` = raw pointer-to-pointer (C `T**`) | **UNSAFE** (raw depth is uncapped, ref-model §3 ruling 2) |

## A.4 Checker binding rules — the tripartite law, mapped onto `let` / `mut *` / `*`

The tripartite law of `ref-transparent-model.md` §2/§6-R1 is carried over **verbatim in meaning**;
only its surface changes. The 2×2 matrix (binding × aliasing) becomes:

| | value `T` | reference (background `Reference{T}`) |
|---|---|---|
| **`let x: T`** | snapshot, deep-immutable | **`let *x` — ILLEGAL** (no immutable reference) |
| **`mut x: T`** | mutable local value (not a pointer, aliases nothing) | **`mut *x: T`** — the alias (write-through) |

Rules the checker enforces (each maps 1:1 onto an existing R-rule):

- **R1/tripartite:** `let` protects (deep-immutable value); `mut` unlocks the binding + interior;
  `*` unlocks the value at the pointee (aliasing). `let *x`/`const *x` = compile error ("a reference
  is inherently mutable; there is no immutable reference — use `let x: T` for an immutable snapshot").
- **R3 (mandatory-mut + never-null):** `*x` is inherently mutable — there is no `let *`/`mut *` split
  beyond "`mut` is the binder a `*` rides on." A `*`-binding is never null (A.7); `*x: T?` is the
  *pointee*-optional form (`Reference{T | Null}`), never a nullable reference.
- **R4 (write-through, never rebind):** `t = v` on a `*`-bound `t` writes `v` **through** the alias;
  the pointer is fixed for the binding's life. Rebind is impossible in the surface (use
  `teko::marshall::swap` for a value exchange, marshall-spec §5.6).
- **R5 (copy-on-attach):** `mut *t: T = <value>` materializes a copy of the value in the destination
  arena and `t` aliases the copy (the bidirectional desugar, ref-model §4.1 — unchanged).
- **A4 (definite-assignment):** `mut *t: T` **requires an initializer** at the declaration (a `*`
  binding with `+ R4` and no init would deref garbage on the first `=`). No uninitialized `*`-binding.
- **A1–A2/A5/A6 (the spine):** unchanged — they key off `checker::Reference`, which the `*`-sugar
  still produces. The escape/borrow/free gates do not see the spelling change at all.

**Binding lowering:** `mut *t: T` ⇒ a `Binding` whose bound type resolves to `Reference{T}` with
`by_ref = true`; `fn *f(...) -> R` ⇒ the fn's `return_type` resolves to `Reference{R}` and the
returned expression is subject to R6 (materialize-in-caller-arena) + A1 (transitive escape); `*t: T`
param ⇒ a `Param` whose resolved type is `Reference{T}` with `by_ref = true` (R7 borrow-down under
A2/A3/A5). **None of these introduce a new internal type — they all produce `Reference{..}`, the
type the spine already understands.**

## A.5 Nesting — `**x`, and how depth-cap-2 is reached without a nesting sigil

Owner-rule 4: a name has **at most one `*`**; nesting is eliminated by construction.

- **`**x` at any definition-identifier slot ⇒ COMPILE ERROR** — message: *"a reference binder carries
  at most one `*`; a reference-to-a-reference is produced by a `*`-returning function, not by a
  doubled sigil."* This makes `Reference{Reference{..}}` **unspellable as a binder**, so the
  user can never *write* `Ref<Ref<T>>` directly (owner-rule 4 satisfied structurally).
- **Depth 2 still exists internally**, reached only by *composition*, exactly as ref-model §8's law
  demands: `fn *a<T>(*b: T) -> T` where the callee returns its by-ref param produces a `Reference`
  whose value is itself a `Reference` at the type level (`T**`). The **depth cap 2** (ref-model §3)
  is enforced by the checker on the *resolved* `Reference` depth, NOT on a count of surface `*`s
  (there is at most one surface `*` per name). Consecutive-ref depth > 2 = compile error; containers
  reset the chain (ref-model §3 refinement 1) — carried over verbatim.
- **`**T` at a TYPE position is NOT this error** — it is a raw pointer-to-pointer (`*(*T)`), legal in
  `unsafe` and *uncapped* (raw depth is unbounded, ref-model §3 refinement 2). The two `**` cases are
  distinguished by the same positional rule as the single `*` (A.1).

## A.6 Use sites stay bare — automatic, transparent desugar (owner-rule 6)

No `*` at USE sites. You write `t`, never `*t`, to read/use the value. The type-directed auto-deref
(ref-model §4) is carried over verbatim: a use of a `*`-bound `t` in a value context peels exactly the
`Reference` layers the use-site type demands; against a free generic `U` it peels zero and passes the
whole reference. `.value` **disappears from the surface entirely** (this is the verbosity fix the dev
asked for). Assignment `t = v` is R4 write-through. The `over_array` idiom becomes:

```teko
/**
 * Advances an array cursor by one and yields the current element, or `null` at the end.
 * `*cur` is a by-reference parameter (background `Reference{ArrayCursor}`); every use of
 * `cur` auto-derefs (no `.value`), and `cur = …` writes THROUGH the alias (R4).
 *
 * @param []i64 xs         the backing array (borrowed by value; not mutated)
 * @param ArrayCursor *cur  the cursor, passed by reference (write-through advance)
 * @return i64 | null       the current element, or `null` when the cursor is exhausted
 * @since 0.3.1
 */
fn over_array(xs: []i64, *cur: ArrayCursor) -> i64 | null {
    if cur.pos >= xs.len { return null }        // auto-deref — no `.value`
    let v = xs[cur.pos]
    cur = ArrayCursor { pos = cur.pos + 1 }      // write-through (R4)
    v
}
```

## A.7 Never-null, and the reconciliation with the merged C5 `Ref<T> | null` work (owner-rule 5)

**The rule:** no reference is ever null; only its *value* can be. In the backend there is **never**
`Reference | Null`, only `Reference{ inner: T | Null }` — the spine (`T*`) is always non-null; the
pointee slot may be a null-union. Surface: `mut *t: T | null` (or `-> T | null` on a `*`-returning
fn) means **`Reference{ T | Null }`** — a non-null reference to a nullable value.

**What is currently merged (the C5 work), and exactly what must change:**

- `resolve.tks:1503`/`:1619` and the null-union niche (`cg_union_niche_member`) currently admit
  **`Ref<T> | null`** as a two-member union: the `Reference` is the sole non-null sibling of `Null`,
  and it **niche-fills to a bare `T*` where `NULL` means the `null` member** — i.e. the *pointer
  itself* carries the absence. `type.tks:102` lists `Reference` and `Null` as sibling `Type` arms;
  `typer.tks` flow-narrows a `Ref<T> | null` local (a local pointer can't be aliased, so the narrow
  is sound *for a local*).

- **Under this model that reading is WRONG at the surface and must be re-pointed.** The change set:
  1. **The surface `Reference | Null` union is REJECTED** everywhere (it already is for the *bare*
     nullable-ref direction, `resolve.tks:1619` "a reference cannot be nullable"; extend that reject
     to the two-member niche form too). The only surface null-with-reference is `*t: T | null` =
     `Reference{ T | Null }` — the `?`/`| null` sits on the **inner**, never on the `Reference`.
     Diagnostic: *"a reference always exists once declared — put the `| null` on the value
     (`*t: T | null` ≡ `Reference{T | Null}`), not on the reference."* (ref-model §4.2, verbatim.)
  2. **The niche machinery MOVES DOWN one level.** The `X | null → bare X` niche that C5 built for
     `Ref<T> | null` (pointer-is-null = absent) is **retargeted** to the *pointee*: when the inner
     `T | Null` is itself reference/pointer-shaped it keeps the niche (NULL-in-the-slot = the `null`
     value); the reference spine `T*` above it is unconditionally non-null. The niche codegen arm is
     retained but re-classified — it fires for the *inner* nullable, not for a top-level
     `Reference | Null` (which no longer exists).
  3. **The C5 flow-narrowing is RESTRICTED.** C5's narrowing on a `Ref<T> | null` local was sound
     because a *local pointer* can't be aliased between check and use. Under this model the null lives
     on the **pointee**, and ref-model §4.2's law applies: **NO flow-sensitive narrowing through a
     reference** (another alias may null the slot between the check and the use). So the C5 narrowing
     memo must be **disabled across a reference boundary** — each access re-reads presence via
     `?.`/`??`/`match` (one extra load, no new layout). It **stays** for genuinely-local, non-ref
     `T | null` values (which is the majority of the C5 call sites and where it is sound).

- **Lowering is unchanged in bytes:** `Reference{T | Null}` is still a bare non-null `T*` pointing at
  a slot of type `T | Null`; the slot's representation is whatever the null-union rep already chose
  for `T | Null`. **No new rep path** — this is a re-classification of *which* value the existing
  niche describes, not a new niche.

**Flag for review (precise):** the files that change are `resolve.tks` (reject two-member
`Reference | Null`; keep `Reference{T|Null}`), `typer.tks` (restrict the flow-narrow memo to non-ref
optionals), and the null-union niche classifier (retarget the `Reference` niche arm from
top-level-union to pointee). `type.tks:102`'s `Reference`/`Null` sibling arms stay (they are needed
for `T | Null` *inners*), but `Reference` may no longer appear as a *direct* sibling of `Null`.

## A.8 How `&x` (today's safe address-of / mut-by-reference) is spelled now

Today `&x` (AL-wave-F1 `Borrow`) is the SAFE mutable-borrow surfacing the implicit auto-ref. Under
this model:

- **Safe by-reference passing needs NO sigil at all.** You pass `x` to a `*param`; the auto-borrow
  (R7/A3 borrow-down) happens automatically — the same implicit `&x` auto-ref the codegen already
  emits. So the *safe* meaning of `&x` is **retired from the surface**: `f(x)` where `f` declares
  `*p: T` is the whole story (call sites are bare — consistent with owner-rule 6).
- **`&x` is REASSIGNED to the UNSAFE raw address-of** — `&x` yields `*T` (a raw pointer) and is legal
  **only in `unsafe` context**, exactly as `marshall-spec.md` §5.5 already wants (`&x` = raw
  `ptr<T>`/`*T` of an lvalue; banned in safe code). This *removes* the AL-wave-F1 safe borrow (its
  job is now done by auto-borrow) and frees `&` to mean the one thing every C/Zig programmer expects.
- **Consequence for `src/`:** the `&`-prefix sites in the corpus (mostly already implicit via
  auto-ref) must be audited during adoption (A.10): a safe `&x` becomes a bare `x` passed to a
  `*param`; any genuine raw address-of moves inside an `unsafe fn`. Because most `&` are already
  implicit at call sites, the blast radius is small (analysis §2.1).

## A.9 The fallback evaluation — `*name`-sugar vs a NEW transparent `ref`-keyword (owner asked; recommend one)

**Framing first (the fact that decides this): both options are NEW CONSTRUCTION of equivalent cost
over the SAME already-existing reference.** The reference is built and in daily use as `Ref<T>` +
`.value` + `&` (114 `Ref<` sites, §0 fact 2). There is **no `ref` keyword to "keep"** — it never
existed. So the choice is NOT "keep vs replace"; it is **which fresh grafia to build over the
existing `Ref<T>`:**

- **(A) `*name`-sugar** (this doc) — build the `*`-before-the-identifier grafia; `Ref<T>` becomes
  compiler-internal.
- **(B) a transparent `ref`-KEYWORD, built from scratch** (per `ref-transparent-model.md`) — build a
  new contextual `ref` keyword + `ref` type-grafia, hiding `Ref<T>` as compiler-internal. The owner's
  stated fallback ("keep the `ref` keyword but hide the `Ref<T>` type; then `&x` becomes `ref x`")
  is, factually, **this construction** — there is no keyword to keep, so B is "CONSTRUIR a `ref`
  keyword," and in B the mut-by-reference `&x` is spelled `ref x`.

Both keep `Ref<T>` + `.value`-lowering + the spine unchanged; both delete surface `.value`; both are
the same-sized parser+checker build. The only existing artifact to keep-or-swap is the `Ref<T>` +
`.value` + `&` grafia. Comparison, law-first:

| criterion (law) | (A) `*name`-sugar (build) | (B) new transparent `ref`-keyword (build) |
|---|---|---|
| **verbosity / ergonomics** (owner's goal) | **best** — one glyph, no keyword, no `.value`; `*t`, `mut *t`, `fn *f` | good — `ref t`, `ref x`, but a word per binder is 2–3 chars more |
| **teachability** (M.3 honest) | **mixed** — `*name` (safe ref) vs `*Type` (raw ptr) is a *subtle* same-glyph/different-position rule a newcomer can misread as "everything with `*` is a pointer" | **best** — `ref` reads as "reference"; `*T` is unambiguously "raw pointer"; two distinct words, zero overload |
| **grammar ambiguity** (M.4) | resolved by strict position (A.1), but `*` is heavily overloaded (multiply, deref, raw-ptr, ref-binder) — the *parser* is clean, the *reader* carries the load | **best** — `ref` is a fresh contextual keyword; `*` means only "raw pointer / deref"; no reader overload |
| **`*T`-vs-`ptr<T>` collision** (§5 of the analysis) | **resolved by construction** — `*name` (binder, safe) can never occupy a type slot, so it never collides with `*T`/`ptr<T>` | resolved differently — `ref` (safe) and `ptr`/`*T` (unsafe) are distinct words; also clean |
| **keyword sparsity** (M.0) | **best** — zero new keyword | costs one contextual keyword (`ref`) — cheap (Teko already does contextual kw) |
| **build cost** | new parser *positions* (fn-name `*`, param-name `*`, binder `*`) + type-position `*T` | new contextual keyword + `ref` type-grafia; comparable checker surface, spec'd in ref-model |
| **owner status** | the ratified DIRECTION | the documented fallback (build-from-scratch) |

**RECOMMENDATION: build (A) `*name`-sugar — but gate the surface behind a teachability + soundness
review (an §10.4-style pass) before it cements.** (A) is ergonomically strongest and, more
importantly, it *dissolves* the load-bearing `*T`-vs-`ptr<T>` collision **by construction** (a safe
binder and a raw type can never occupy the same slot), which is the single sharpest design constraint
the analysis identified. Its one real cost is **teachability of the overloaded `*`** (M.3): a reader
must internalize "`*` before a *name being defined* = safe reference; `*` before a *type* = raw
pointer." That is a documentation + diagnostics problem, not a grammar problem, and it is fully
mitigable:

1. **Diagnostics carry the rule.** Any `*` misuse (`let *x`, `**x`, `*T` in safe context) emits the
   positional explanation, so the compiler *teaches* the rule at the point of confusion.
2. **The review gate.** Run a short teachability/soundness pass (mirroring ref-model §10.4 auto-deref)
   BEFORE cementing the parser positions; if the overload proves too costly, **build (B) instead** —
   the fallback is fully law-passing and already designed in ref-model, so the escape hatch is cheap
   (and, because both are fresh construction over the same `Ref<T>`, switching before build starts
   costs nothing already sunk).

If the owner weights teachability over ergonomics, **(B) is the honest second choice** and loses on
nothing but verbosity. Both pass all Laws; (A) wins on ergonomics + collision-dissolution, (B) wins
on teachability. **The owner ratifies.** (No HALT — both options are law-passing new-construction over
the existing reference; this is a preference call the owner owns.)

## A.10 Part-A crumb sequence (re-spells wave-0.3.1-plan SW3/SW4; seed-sequenced)

Every crumb is additive-then-adopt under the stable-seed law: the parser/checker ACCEPT the new
surface in seed `N` (with `Ref<T>`/`.value` kept as a deprecated-but-accepted alias), `src/` adopts
in `N+1`, the alias is removed last. **This is the linchpin hop** (analysis §6.3, wave-plan R2) — the
single highest stranding risk; never remove the old acceptance in the same sub-wave that adds the new.

| crumb | size | touches | notes |
|---|---|---|---|
| **A0** teachability/soundness review of the `*` overload (gates A2) | L (design) | design + a parser/checker unit harness | the §10.4-style gate; verdict = build-(A) **or** build-(B) instead (A.9) |
| **A1** lexer/AST: `PtrType{pointee}` type node + `by_ref` flag on `Param`/`Binding`/`FnDecl` | S | `ast.tks` | additive; no new token |
| **A2** parser: accept leading `*` at fn-name / param-name / `mut`-binder slots, and `*` at type-primary start | M | `parse_decl.tks`, `parse_stmt.tks`, `parse_type.tks` | gated by A0; `let *`/`const *`/`**name` = parse error |
| **A3** checker: `*name` binders resolve to `Reference{..}` (`by_ref`); `*T` resolves to `Ptr{inner}` + unsafe-gate; tripartite `let *` reject; A4 definite-assignment on `*`-binders | M | `checker/resolve.tks`, `checker/typer.tks` | the spine/escape gates are UNCHANGED (they read `Reference`) |
| **A4** depth-cap-2 on resolved `Reference` depth + `**name` reject + `**T` uncapped-in-unsafe | S | `checker/resolve.tks` | carries ref-model §3 verbatim onto the resolved depth |
| **A5** never-null re-point: reject surface `Reference \| Null`; retarget the C5 niche + restrict the flow-narrow memo (A.7) | M | `resolve.tks`, `typer.tks`, null-union niche classifier | **the C5 reconciliation** — review-heavy |
| **A6** auto-deref + bare use sites (type-directed peel; `.value` deprecated-but-accepted) | L | `checker/typer.tks`, `codegen.tks` | carries ref-model §4; `.value` alias kept for the seed dance |
| **A7** `&x` → unsafe-only raw address-of (retire the AL-wave-F1 safe borrow; auto-borrow replaces it) | S | `parse_expr.tks` (Borrow), `checker`, `codegen` | `&x` now yields `*T`, unsafe-gated (marshall-spec §5.5) |
| **A8** ADOPT in `src/`: migrate `Ref<T>`/`.value`/safe-`&x` → `*name`/bare-use; remove the `.value` + old-`Ref<T>` acceptance | L | `src/**` (114 `Ref<`, the Ref-subset of `.value`, the `&` sites) | fixpoint + `diff_vm_native` at each file; honest-stop (keep old form on a stubborn file) until green, THEN remove the alias |

**Seed-sequencing:** A1–A7 land in one seed with BOTH spellings accepted (`0.3.1.N-beta`); A8 dogfoods
that seed and only then drops the alias (`0.3.1.(N+1)-beta`). The corpus must never use `*name` before
the seed can parse it — the deprecated-alias step is what makes the hop safe.

## A.11 Part-A regression fixtures (inputs → exit, VM & native)

**ACCEPT (VM==native unless noted):**
- `star_param_by_ref_ok` — `fn f(*t: T)` writes through; caller's `mut` local mutated → RUN, VM==native.
- `star_fn_returns_ref_ok` — `fn *g() -> T` returns a caller-arena-materialized ref (R6) → RUN, VM==native.
- `star_mut_binding_ok` — `mut *t: T = v; t = w` write-through → RUN, VM==native.
- `star_use_site_bare_ok` — a `*`-bound name used with NO sigil, `.value`-free → RUN, VM==native.
- `star_optional_pointee_ok` — `mut *t: T | null = …`; `?.`/`??`/`match` peel one level → RUN, VM==native.
- `star_autoderef_free_generic_ok` — `f<U>(t)` with `t` doubly-referenced passes the whole ref → RUN.

**REJECT (`EXPECT_COMPILE_FAIL`):**
- `star_let_binding_rejected` — `let *x: T` (no immutable reference; tripartite).
- `star_double_name_rejected` — `**t` at a binder slot ("at most one `*`").
- `star_depth_cap3_rejected` — a composition producing consecutive-ref depth 3.
- `star_ptr_type_in_safe_rejected` — `x: *T` / `-> *T` in a NON-`unsafe` fn (raw pointer needs unsafe).
- `star_nullable_reference_rejected` — a surface `*t`-union that would form `Reference | Null` ("put the `| null` on the value").
- `star_uninit_ref_rejected` — `mut *t: T` with no initializer (A4 definite-assignment).
- `amp_in_safe_rejected` — `&x` in a safe fn (now unsafe-only raw address-of).

**Ritual points (Part A):** A0 is a design gate (verdict before A2 cements); the **A1–A7 seed cut** is
a full gate + the stable-seed dance (both spellings accepted); the **A8 adoption** is a full gate +
**self-host critical** (the corpus must build on `*name` with `.value` removed — the load-bearing
proof, exactly the SW4 ritual).

---

# PART B — the safe⇆unsafe boundary + FFI completeness (deep re-evaluation)

## B.0 The refined pointer ladder — three distinct pointer-ish things, cleanly separated

The task instructs that **`ptr<T>` is OPAQUE** and models nullable FFI pointers as `ptr<T> | null`
and a void pointer as `uptr | null`. This is a **refinement of `marshall-spec.md`**, which treated
`ptr<T>` as directly derefable (its §5.5 raw operators act on `ptr<T>`). Reconciled, the ladder is:

| rung | spelling | derefable? | nullable? | role |
|---|---|---|---|---|
| safe reference | `*name` (background `Reference{T}`) | via auto-deref (safe) | never (A.7) | the spine-tracked safe world |
| **transparent raw pointer** | `*T` (type position) | **yes** — `*p`, `p+n`, `p[i]`, `p->f` (marshall-spec §5.5) | yes (raw addr; 0 legal) | unsafe pointer math on a KNOWN pointee layout |
| **opaque FFI pointer** | `ptr<T>` | **NO** — must convert first | via `ptr<T> | null` | the C-boundary token; layout not manipulated in Teko |
| opaque void pointer | `uptr` | no | via `uptr | null` | address-as-word; `void *` / handle transport |

**The refinement (flag vs marshall-spec):** marshall-spec's raw *operators* (`*p`/`&`/`->`/`+`/`-`/`[]`)
bind to the **transparent `*T`**, not to the opaque `ptr<T>`. `ptr<T>` becomes the **opaque boundary
handle** — you cannot deref it; you convert `ptr<T>` (after a null check) to `*T` to do pointer math,
or `wrap` it into a safe `*name`. This is a clean division of labor: `*T` = "I know the layout and
will compute"; `ptr<T>` = "an FFI address I only pass around." Both are `unsafe`-carrying
unconditionally (marshall-spec §4 / crumb C0). **`marshall-spec.md` §5.5 must be re-pointed at `*T`;
its `wrap`/`unwrap` produce/consume `*name`/`*T`, and the *boundary* with C speaks `ptr<T> | null`.**

## B.1 safe ⇆ unsafe conversion via opaque `ptr<T>` (the boundary rules)

The conversions, and where `unsafe` is mandatory:

| from → to | mechanism | safe? |
|---|---|---|
| safe `*name` (`Reference{T}`) → `*T` (transparent raw) | `teko::marshall::unwrap` (marshall-spec §5.2) | `unsafe fn`; result `*T` cannot be held in safe code |
| `*T` (or null-checked `ptr<T>`) → safe `*name` | `teko::marshall::wrap` (marshall-spec §5.1) | `unsafe fn`; null-PANICS (R2/M.1); result `*name` re-enters the spine |
| `ptr<T> \| null` → `*T` | explicit null test (`match`/`??`) then reinterpret | `unsafe`; the null test is the gate |
| `*T` → `ptr<T>` | reinterpret (same `T*` runtime) | `unsafe`; free (§3 marshall-spec) |
| `ptr<T>` ↔ `uptr` | `to_uptr`/`from_uptr` (marshall-spec §5.4) | `unsafe fn` |
| a safe VALUE crossing OUT of an `unsafe fn` | by-value copy / `Owned<T>` (remodel §2) | safe result may leave; raw types may not |

**What stays safe:** a safe reference is passed to a `*param`, mutated write-through, and a scalar
value crosses back out — all without the caller writing `unsafe`, as long as no raw type is *named*
in safe code. **Where `unsafe` is mandatory:** naming `*T`, `ptr<T>`, or `uptr` in a signature, field,
or local; calling any raw operator; any `marshall` op that names a raw type. The opaque `ptr<T>` is
the ONLY pointer the extern boundary exposes to a wrapper (§B.4), and it is nullable-by-union
(`ptr<T> | null`), never by `?` (marshall-spec §5.3: `ptr<T>?` is a category error, rejected).

## B.2 C macros over FFI

Two directions, and they are NOT symmetric.

### B.2.1 USING an existing C macro via FFI

A C macro has **no linker symbol**, so `extern fn f = "SYM"` (which binds a symbol) cannot reach it.
Surface — an additive macro form of `extern`:

```teko
/**
 * Binds a C preprocessor macro as if it were a typed function. The dev ASSERTS the signature
 * (a macro is untyped); the checker type-checks calls against the asserted shape, but lowering
 * EXPANDS the macro textually (C-transpile backend) rather than emitting a symbol call. The
 * header that defines the macro must be named so it is in scope at expansion.
 *
 * @param u32 x  the value passed to the macro
 * @return u32   the asserted result type
 * @since 0.3.1
 */
extern macro fn htonl(x: u32) -> u32 = "htonl" from header "arpa/inet.h"
```

- **Surface:** `extern macro fn NAME(params) -> R = "MACRO_NAME" from header "h.h"`. `macro` is a
  contextual keyword after `extern`; `from header "…"` is a new `[extern]`-adjacent knob (§B.4)
  requesting an `#include`.
- **Type-checking:** the asserted signature is checked like any `extern fn` (params/return resolved,
  callers checked). The checker does NOT verify the macro *is* type-safe (it can't — macros aren't
  typed); the dev owns that assertion (M.3 honest: the assertion is greppable at the `extern macro`).
- **Lowering (the tension):** on the **C-transpile backend**, emit the macro name at the call site
  (with the `#include`) — free. On the **own AOT backend there is NO C preprocessor** — a C macro is
  fundamentally unreachable without a C toolchain. **Law tension (toolchain-independence vs C-macro
  FFI):** resolve honest-stop — `extern macro` compiles on the C-transpile backend and on a
  `cc`-compiled shim object (a generated one-line real wrapper the C compiler expands and exports as
  a symbol), and is an **honest compile error on the pure own-backend** ("a C macro requires a C
  toolchain; provide a linkable wrapper symbol via `extern fn`"). Recommended default: **generate a
  real wrapper function** (`static inline` promoted to an exported symbol) so a macro becomes an
  ordinary `extern fn` at link time — this makes it work uniformly *where a C compiler is available*
  and degrades honestly where it is not.

### B.2.2 CREATING a C macro callable from Teko

This is a reverse-FFI concern (§B.5): when Teko *exports* to C, the generated C header (§B.5) may emit
small `#define` wrappers for exported constants / trivial inlines so a C consumer gets macro
ergonomics. **Recommendation:** do NOT let Teko author arbitrary C macro *bodies* (that is injecting C
text, which the own-backend cannot honor and which violates M.3 honesty about what Teko emits). Limit
"created" macros to **generated `#define`s for exported `const`s** in the C header (a mechanical,
backend-independent emission). Anything richer is deferred (flagged, not an owner HALT).

## B.3 Variadics (`…`)

### B.3.1 Calling C variadic functions

```teko
/**
 * The C `printf`, bound as a variadic extern. The `...` tail marks the C-ABI variadic zone:
 * each call-site trailing argument is passed per the C calling convention (default argument
 * promotions apply — `f32`->`f64`, sub-`int` integers->`int`). Only FFI-safe scalar/pointer
 * arguments may fill the variadic tail.
 *
 * @param byte *fmt  a NUL-terminated C format string
 * @return i32       the C return
 * @since 0.3.1
 */
unsafe extern fn printf(fmt: *byte, ...) -> i32 = "printf"
```

- **Surface:** a literal `...` token as the LAST parameter of an `extern fn` (distinct from Teko's
  homogeneous `params xs: []T`, which stays the Teko idiom — see B.3.2). `...` is only legal on
  `extern` fns and only in trailing position.
- **Type-checking:** fixed params check normally; each variadic-tail argument must be an FFI-safe
  scalar or pointer (no closures, no safe refs, no aggregates unless `#repr("c")` §B.4). The checker
  applies the **default argument promotions** so the call matches the C ABI.
- **ABI lowering:** on the **C-transpile backend** the call is emitted verbatim (the C compiler
  handles varargs). On the **own AOT backend** this is a real per-target crumb — implement the
  platform varargs ABI (SysV AMD64: integer args in GPRs then stack, floats in XMM + `AL` = #vector
  regs; AArch64/Windows have their own rules; promotions applied). This is the honest cost of
  toolchain independence and is sequenced as a backend crumb (B.10), not free.

### B.3.2 Declaring Teko-side variadics for FFI

**Recommendation: do NOT expose Teko-authored C-ABI variadic functions in 0.3.1.** Teko's variadic is
`params xs: []T` (homogeneous, desugars to a slice) — a *better* idiom that a C caller cannot consume
directly anyway (it is a fat slice, not a C `va_list`). Exporting a Teko fn *as* a C variadic would
require Teko-side `va_list` handling and per-target unwinding of the varargs ABI for no real corpus
need. **Honest-stop:** a Teko fn exported to C (§B.5) takes fixed params (or an explicit
`count + *T` pair — the C idiom); `va_list` consumption is deferred and flagged.

## B.4 FFI completeness audit — gaps and proposals

The current surface (`extern fn`, `extern type`, `ptr<T>`, `uptr`, `unsafe`, `[extern.libs.<os>]`,
`params []T`) covers **one-symbol-at-a-time libc-shaped C**. Real C integration needs more. Gaps and
proposals:

**G1 — Whole-library / header-set linking (not symbol-by-symbol).** Today each foreign symbol needs
its own `extern fn = "sym"`. Propose a **grouped extern block** + a **header include knob**:

```teko
/**
 * Groups a set of foreign declarations that share a library and header set. `from lib` supplies
 * the `-l` name (per-OS resolution still via `[extern.libs.<os>]`); `from header` requests the
 * `#include`s the C-transpile backend and any generated shim need. Each inner declaration is an
 * ordinary `extern fn`/`extern type`/`extern macro` with the group's lib/header defaulted in.
 *
 * @since 0.3.1
 */
extern from lib "sqlite3" from header "sqlite3.h" {
    type Sqlite3            // opaque handle -> void *
    fn open(path: *byte, out: **Sqlite3) -> i32 = "sqlite3_open"
    fn close(db: *Sqlite3) -> i32 = "sqlite3_close"
    macro fn version() -> *byte = "sqlite3_libversion"
}
```

Plus a manifest knob `[extern.headers.<os>]` (mirroring `[extern.libs.<os>]`) listing includes. A
full **`teko bindgen`** (generate `extern` blocks from a C header) is the ideal but is a **tool**, out
of 0.3.1 scope — flagged as the follow-on that this grouped surface is the manual precursor to.

**G2 — struct/union layout compatibility.** Teko structs are Teko-laid-out. For a C struct crossing
by value (issue #502, already SW9) or by `*T`, field order/padding/alignment must match C. Propose a
**`#repr("c")`** decoration on a `struct`/`extern type` (reusing the `#`-decorator machinery,
remodel §3) that pins C layout; and an **`extern union`** body (C untagged union) for FFI. `#repr("c")`
is required for any aggregate that crosses the boundary by value or by pointer; a non-`#repr` struct
crossing FFI is a compile error (M.3: no silent layout assumption).

**G3 — callback function pointers.** Passing a Teko fn to C as a `R (*)(args)` callback. Teko fn
types `(A,B)->R` exist, but capturing closures carry an **env-first ABI** (`void*` prepended,
codegen `R(void*, params)`) that is NOT a C function pointer. Propose an FFI-safe fn-pointer:

```teko
/**
 * A C-ABI function pointer type: NO environment, plain `R (*)(A, B)`. Only a NON-capturing
 * closure or a top-level fn may be coerced to a `cabi`-fn pointer; a capturing closure is a
 * compile error (its env cannot ride the C ABI). The `void *user_data` idiom is expressed as
 * an explicit trailing `*T`/`uptr` param, per the C convention.
 *
 * @since 0.3.1
 */
extern type Comparator = cabi (a: *byte, b: *byte) -> i32
```

Rule: only env-free callables coerce to a `cabi` pointer; the checker rejects a capturing closure at
the coercion site (M.3 — the env would be lost silently otherwise). The context/user-data pattern is
an explicit `*T`/`uptr` param, never a hidden env.

**G4 — calling convention / symbol attributes.** Windows `stdcall`, weak symbols, symbol versioning,
`thread_local` FFI state, `errno`. Propose: a `#[extern]`-family attribute for calling convention
(`#cconv("stdcall")`) and weak linkage (`#weak`), defaulted to the platform C default; `errno` exposed
as a `teko::ffi::errno()` accessor (thread-local read via `teko_rt`). These are **lower priority**;
recommend shipping cconv + `errno` in 0.3.1 and flagging weak/versioning as follow-ons.

**G5 — the opaque-handle gap in the header path.** `emit/header.tks:91` honest-stops on exporting an
`extern type` in the `.tkh`. Reverse-FFI (§B.5) needs this closed for the C-header emitter.

## B.5 Reverse FFI — exporting Teko to other languages (a first-class owner ask)

How does C (or another language) call INTO a Teko-compiled artifact? Five pieces:

**RF1 — the artifact kind.** `build/manifest.tks` already parses `kind = "binary" | "static" |
"shared" | "package"` (→ `Artifact::{Binary,Static,Shared,Package}`). `static`/`shared` today produce
**Teko** libraries (Teko-mangled symbols + `.tkh`). Add a **C-ABI library artifact** — either a new
`kind = "clib"` **or** an orthogonal knob `[artifact] abi = "c"` on `static`/`shared`. Recommend the
**`abi = "c"` knob** (composes with `static`/`shared` = a C-callable `.a` or `.so`/`.dylib`), so the
same fn set can ship as both a Teko lib and a C lib.

**RF2 — the export marker.** A Teko fn is exported to C with a **stable, unmangled C symbol** and the
**C ABI** (no env param, no namespace mangling), with an FFI-safe signature only. Propose a
`#[export("c_symbol")]` attribute (or a `cexp fn` visibility) that:
- emits the fn under the given C symbol (no `teko::` mangling — reuse the `extern`-fn no-mangle path
  in `codegen.tks:7515`),
- forbids non-FFI-safe params/returns (no capturing closures, no bare safe `*name` crossing as-is —
  a safe ref crosses as `*T`/`ptr<T>`; values cross by copy with `#repr("c")` for aggregates),
- forbids Teko `params []T` variadics in the exported signature (B.3.2).

```teko
/**
 * Exported to C as the stable symbol `teko_add`. C ABI, no name mangling, no environment.
 * Only FFI-safe scalar/pointer/`#repr("c")` aggregate params and returns are permitted.
 *
 * @param i64 a  first addend
 * @param i64 b  second addend
 * @return i64   the sum
 * @since 0.3.1
 */
#[export("teko_add")]
pub fn add(a: i64, b: i64) -> i64 { a + b }
```

**RF3 — the generated C header.** `emit/header.tks` emits a `.tkh` (Teko header) today. Add a
**C-header emitter** (`emit_c_header`) that, for every `#[export]` fn (and `#repr("c")` type it
references), writes a real `.h` with C prototypes + `#define`s for exported consts (B.2.2). This
closes the `extern type` header gap (G5) for the export direction. `teko build` on a `abi="c"`
artifact emits the `.h` alongside the library.

**RF4 — the runtime init contract.** A Teko `.so`/`.a` needs the `teko_rt` arena machinery
initialized before any exported fn allocates. A C host cannot rely on Teko's `main` running. Propose
an explicit, exported init/teardown pair emitted for every `abi="c"` artifact:

```c
void teko_rt_init(void);      /* the C host calls this once before any teko_* call */
void teko_rt_shutdown(void);  /* optional: releases the root arena */
```

with a **guarded auto-init fallback** (first exported call inits if the host didn't) so the honest
default is "it just works," and the explicit pair is there for hosts that manage lifecycle. This
respects no-implicit-global (the arena is process-root, exactly the compiler's own leak-to-root
model, remodel §0) while giving the C host a real handle.

**RF5 — FFI-safe boundary on exports.** The same restriction as callbacks (G3): what leaves a Teko
export is a by-value safe copy, a `*T`/`ptr<T>` (unsafe on the Teko side, a plain `T*` to C), a
`#repr("c")` aggregate, or an `Owned<T>` handle (as an opaque `void *` to C). A safe `*name`
(`Reference`) does not cross as-is — it is `unwrap`ped to `*T` at the export boundary. Panics must
NOT unwind into C — an exported fn catches Teko panics at the boundary and returns an error code /
aborts per the artifact's declared panic policy (flag: reuse the `error?`/exit-code convention; a
`#[export]` fn that can panic should return `... | error`).

## B.6 Part-B crumb sequence (extends wave-0.3.1-plan SW8 into a full FFI sub-wave; seed-sequenced)

All extern-surface additions are additive-accept-then-adopt; most are seed-safe (the compiler's own
`src/` uses little FFI, so the seed can carry the surface without `src/` adopting it).

| crumb | size | touches | blocked? |
|---|---|---|---|
| **B0** refine `ptr<T>` → OPAQUE; move raw operators (`*p`/`&`/`->`/`+`/`-`/`[]`) to transparent `*T`; `ptr<T> \| null` / `uptr \| null` nullable-by-union | M | `checker/resolve.tks`, `parse_type.tks`, `codegen.tks` | needs Part A `*T` (A2/A3); reconciles marshall-spec §5.5 |
| **B1** `wrap`/`unwrap` re-pointed to `*name`↔`*T`; boundary speaks `ptr<T> \| null` | M | `src/marshall/*`, checker | needs Part A auto-deref (A6) — the marshall C4/C5 crumbs, re-spelled |
| **B2** `extern macro fn … from header "h"` — surface + type-check + C-expand / shim-generate; own-backend honest-stop | M | `parse_decl.tks`, `checker`, `codegen`, `[extern.headers]` in `manifest.tks` | seed-safe (additive) |
| **B3** C-ABI variadic `...` on `extern fn` — surface + promotions + C-backend emit | M | `parse_decl.tks`, `checker`, `codegen` | seed-safe; own-backend ABI is B10 |
| **B4** grouped `extern from lib/header { … }` block + `[extern.headers.<os>]` manifest knob | M | `parse_decl.tks`, `manifest.tks` | seed-safe (additive) |
| **B5** `#repr("c")` struct layout + `extern union` — layout pinning for by-value/pointer FFI aggregates | M | `parser` (decorator), `checker`, `codegen` layout | couples #502 (SW9 extern struct by-value) |
| **B6** `cabi` fn-pointer type + non-capturing coercion rule (callbacks) | M | `parse_type.tks`, `checker`, `codegen` (drop env ABI) | seed-safe |
| **B7** `#cconv(..)` + `teko::ffi::errno()` (calling convention + errno); weak/versioning flagged follow-on | S–M | `parser`, `codegen`, `teko_rt` (maintained-C errno read) | seed-safe |
| **B8** reverse-FFI: `[artifact] abi = "c"` + `#[export("sym")]` marker + FFI-safe export gate | M | `manifest.tks`, `checker`, `codegen` (no-mangle path) | additive |
| **B9** reverse-FFI: C-header emitter (`emit_c_header`) + close the `extern type` header gap (G5) + `teko_rt_init/shutdown` contract | M | `emit/header.tks`, `teko_rt.{c,h}` (maintained-C init/teardown) | on B8 |
| **B10** own-backend variadic ABI (SysV/AArch64/Windows varargs) — the honest cost of toolchain independence | L | `src/backend/*`, `lir/lower.tks` | on B3; a real backend crumb (sequences with SW13 backend debts) |

**Ritual points (Part B):** B0/B1 ride the SW8 Marshall gate (the containment gate + the ptr↔ref
boundary — full gate at each). B5 (`#repr("c")`) and B8/B9 (reverse-FFI) are full gates (they change
what crosses the boundary / what the artifact exposes). B10 is a full gate + the backend fixpoint.

## B.7 Part-B regression fixtures (inputs → exit; native unless the op is safe)

**ACCEPT / round-trip (native-only unless noted):**
- `ffi_opaque_ptr_roundtrip` — `ptr<T> | null` from a C stub, null-checked, `wrap`ped to `*name` → RUN.
- `ffi_star_ptr_arith` — `*T` deref/arith (`*p`, `p+n`, `p[i]`, `p->f`) inside `unsafe fn` → RUN.
- `ffi_extern_macro_htonl` — `extern macro fn htonl` expands and round-trips a value → RUN (C-backend).
- `ffi_variadic_printf` — `unsafe extern fn printf(fmt, ...)` prints, exit = return → RUN (C-backend).
- `ffi_grouped_extern_block` — a `extern from lib/header { … }` group links + calls → RUN.
- `ffi_repr_c_struct_byval` — a `#repr("c")` struct passed by value to a C stub → RUN.
- `ffi_callback_noncapturing` — a non-capturing closure coerced to a `cabi` pointer, called by C → RUN.
- `ffi_errno_read` — `teko::ffi::errno()` after a failing C call returns the code → RUN.
- `revffi_export_add` — `#[export("teko_add")] add`; a C driver links the `abi="c"` lib and calls it → RUN.
- `revffi_c_header_emitted` — `teko build` emits a `.h` whose prototype compiles under a C compiler → RUN.
- `revffi_rt_init_contract` — a C host calls `teko_rt_init` then an exported fn that allocates → RUN.

**REJECT (`EXPECT_COMPILE_FAIL`):**
- `ffi_opaque_ptr_deref_rejected` — dereferencing an opaque `ptr<T>` without converting to `*T` (B0).
- `ffi_ptr_optional_question_rejected` — `ptr<T>?` (`?`-over-ptr; nullable must be `ptr<T> | null`, marshall §5.3).
- `ffi_variadic_nonextern_rejected` — `...` on a non-`extern` fn.
- `ffi_variadic_unsafe_payload_rejected` — a closure / safe-ref in a C variadic tail.
- `ffi_repr_c_missing_rejected` — a non-`#repr("c")` aggregate crossing FFI by value.
- `ffi_callback_capturing_rejected` — a CAPTURING closure coerced to a `cabi` pointer (env would be lost).
- `revffi_export_nonffisafe_rejected` — `#[export]` on a fn with a capturing-closure param / bare safe-ref return.
- `revffi_export_teko_variadic_rejected` — `#[export]` on a `params []T` fn (B.3.2).

**PANIC (native, non-zero exit):**
- `ffi_wrap_null_panics` — `wrap` of a null `ptr<T>` panics (R2/M.1; carried from marshall-spec).

## B.8 Seed-sequencing summary (Part A + Part B)

- **Part A is the linchpin seed** (A1–A7 accept, A8 adopt) — it re-spells the SW3/SW4 hop of
  `wave-0.3.1-plan.md`. Nothing in `src/` may use `*name` until the accept-seed ships; the
  `.value`/`Ref<T>` deprecated-alias carries the corpus across the hop.
- **Part B B0/B1** depend on Part A (`*T` + auto-deref) — they are the SW8 Marshall crumbs, re-spelled
  (marshall's C4/C5/C7 were already BLOCKED on the transparent ref; now they consume the `*`-model).
- **B2/B3/B4/B5/B6/B7/B8/B9 are additive and seed-safe** — the seed carries the surface; `src/` need
  not adopt (the compiler is a `kind="binary"`, links only libc, and needs none of the new FFI to
  self-host). They can land in any order after B0, gated only by the seed chain.
- **B10 (own-backend varargs)** sequences with the backend-completion debts (SW13); the C-transpile
  backend gives B3 for free in the interim (honest-stop on the own-backend until B10).

## B.9 Law / Constitution tensions (called out; resolved law-first, one flagged for the owner)

1. **`*` overload teachability (M.3) — Part A.** The same glyph is multiply / deref / raw-pointer /
   ref-binder, disambiguated by position. **Resolved:** strict positional grammar (A.1) + teaching
   diagnostics + the A0 review gate; build the new `ref`-keyword instead (A.9-B) if the review finds
   the overload too costly. *Preference call — owner ratifies A vs B; not a HALT (both are law-passing
   new construction over the same existing `Ref<T>`).*
2. **C-macro FFI vs toolchain-independence (Teko-only / own-backend has no C preprocessor) — B2.**
   **Resolved honest-stop:** `extern macro` works on the C-transpile backend and via a generated
   `cc`-compiled wrapper symbol (so it degrades to an ordinary `extern fn` at link); it is an honest
   compile error on the *pure* own-backend. Not a HALT.
3. **C-ABI variadics vs own-backend — B3/B10.** The own-backend must implement the platform varargs
   ABI itself (no free ride). **Resolved:** C-transpile ships B3 now; own-backend varargs is a real
   backend crumb (B10) sequenced with SW13. Not a HALT.
4. **`ptr<T>` opacity refines marshall-spec (§5.5 derefable ptr) — B0.** The raw operators move to
   `*T`; `ptr<T>` becomes opaque. **Resolved:** documented re-point of marshall-spec §5.5; the two
   spellings share the `T*` runtime so it is a checker-side reclassification, not a rep change.
   **REPORTED to the owner** as an amendment to marshall-spec (a companion-doc edit, not a new issue).
5. **The merged C5 `Ref<T> | null` niche must be re-interpreted — A.7 / A5.** The pointer-is-null
   niche is retargeted to the pointee and the flow-narrow memo is restricted across ref boundaries.
   **Resolved law-first** (ref-model §4.2 is the governing ruling); review-heavy but not a HALT.
6. **Reverse-FFI runtime init vs no-implicit-global — RF4/B9.** **Resolved:** an explicit
   `teko_rt_init/shutdown` pair + a guarded auto-init fallback; the arena stays process-root
   (leak-to-root, remodel §0). Not a HALT.
7. **Panics must not unwind into a C host — RF5.** **Resolved:** the export boundary catches Teko
   panics and returns an error / aborts per the artifact's panic policy; a panicking `#[export]` fn
   should return `… | error`. Not a HALT.

**No genuine unresolved tension remains → NO HALT.** Two items are REPORTED for the owner's awareness:
the marshall-spec §5.5 re-point (tension 4) and the A vs B spelling ratification (tension 1). Both are
preference/companion-edit calls the owner owns; the plan is executable as written under either
resolution.

---

## Appendix — file/impact map (absolute paths)

- Grammar (Part A): `/home/user/teko-lang/src/parser/parse_decl.tks` (fn-name `*`, param-name `*`,
  `extern macro`, `...`, grouped `extern`), `/home/user/teko-lang/src/parser/parse_stmt.tks`
  (`mut *` binder), `/home/user/teko-lang/src/parser/parse_type.tks` (`*T` type-primary, `cabi`),
  `/home/user/teko-lang/src/parser/parse_expr.tks` (`&x` → unsafe raw address-of),
  `/home/user/teko-lang/src/parser/ast.tks` (`PtrType`, `by_ref` flag).
- Checker: `/home/user/teko-lang/src/checker/resolve.tks` (`*name`→`Reference`, `*T`→`Ptr`,
  unsafe-gate, depth cap, reject surface `Reference|Null`, `unsafe_carrying_at`),
  `/home/user/teko-lang/src/checker/typer.tks` (auto-deref peel, restrict C5 flow-narrow),
  `/home/user/teko-lang/src/checker/type.tks` (the `Type` variant — `Reference` no longer a direct
  `Null` sibling).
- Codegen / emit: `/home/user/teko-lang/src/codegen/codegen.tks` (auto-deref, `*T` ops, extern
  no-mangle, `#[export]`, variadics), `/home/user/teko-lang/src/emit/header.tks` (C-header emitter,
  close the `extern type` header gap).
- Manifest / runtime: `/home/user/teko-lang/src/build/manifest.tks` (`[extern.headers]`, `abi="c"`),
  `/home/user/teko-lang/src/runtime/teko_rt.{c,h}` (maintained-C: errno read, `teko_rt_init/shutdown`),
  `/home/user/teko-lang/teko.tkp` (`[extern]` knobs — the manifest example surface).
- Companion designs: `/home/user/teko-lang/docs/design/ref-transparent-model.md` (the safety model —
  carried over), `/home/user/teko-lang/docs/design/marshall-spec.md` (§5.5 re-point flagged),
  `/home/user/teko-lang/docs/design/memory-unsafe-backend-remodel.md` (`unsafe`-by-type),
  `/home/user/teko-lang/docs/design/wave-0.3.1-plan.md` (SW3/SW4 re-spelled, SW8 extended).

*This document is design-ahead and doc-only. No product code is changed. The reference already exists
as `Ref<T>`; both surface options (A `*name`, B a new transparent `ref` keyword) are equal-cost fresh
grafia over it. The owner reviews and ratifies (A vs B spelling; the marshall-spec §5.5 re-point)
before implementation.*
