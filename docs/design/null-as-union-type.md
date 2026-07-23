# Null as a Union Type — `T | null` unifying `Optional` into `Variant`

> STATUS: **RATIFIED (owner, 2026-07-19).** Every decision below is owner-ruled
> and converged; see the decision log (§9). This document is now the
> implementation basis for the wave.
> Author: architect (design-ahead). Owner: schivei. Date ratified: 2026-07-19.
> No product `.tks` changed and no version bumped BY THIS DOCUMENT — it is the
> plan; the wave's crumbs (§8) do the code changes under their own gates.
>
> Ratified decision set (all owner, 2026-07-19): (1) MODEL — fold `Optional` into
> `Variant`, add the unit type `Null` (1 byte, zero-fill) as a first-class `Type`
> case; nullability = `T | null`, no separate former. (2) SUGAR = B, ZERO —
> delete `T?`/`error?`/`?.`/`??`/`?.m()` and the checker `Optional`; keep the
> `null` value + `NullPattern`. (3) NARROWING — universal, TWO forms only (`match`
> and `if x == null`/`!= null`). (4) REPRESENTATION — three-class (niche /
> inline-tag ≤8B / box-in-arena ≥16B), the box dial = A + hatch. (5) `#inline` —
> KEPT as a GENERAL sum-representation directive (attribute spelling). (6) REFS —
> R2 relaxed with the narrowing invariant; `ref x: T | null` ≡ `Ref<T | null>` vs
> `Ref<T> | null`. (7) MIGRATION — 77 `error?` → `null | error`, null-first
> canonical order.

---

## 0. The owner's proposal (verbatim, PT-BR)

> "Sobre nullable, ainda não concordo com tipos nullables. Penso que ou um valor
> é de um tipo ou é nulo, logo, uma variável poderia ser definida como:
> `mut valor: i32 | null = null`; ou seja, null seria valor E tipo de tamanho
> fixo de 1 byte preenchido com zero `0b00000000`, nada mais."

Reading: **eliminate the special nullability former `Optional` (`T?`) and model
absence as an ordinary union `T | null`, where `null` is a first-class unit
type — 1 byte, zero-fill — that participates in unions like any other member**,
unifying with the `Variant` former that already carries `T | error`
(errors-as-values).

---

## 1. Confirmed current state (file:line)

Every claim below was read from the tree at authoring time.

- **Two formers, not one.** `src/checker/type.tks:71` declares
  `pub type Optional = struct { inner: Type }` as a built-in unary former,
  a case *distinct* from `pub type Variant = struct { members: []Type }`
  (`type.tks:73`) inside `pub type Type = variant … | Optional | … | Variant | …`
  (`type.tks:93`). **There is no `Null` type today.**
- **Deliberate orthogonality.** A variant member may NOT be optional: the
  doctrinal note at `type.tks:63-65`, enforced in `resolve.tks:1387-1389`
  ("a variant member may not be nullable (`T?`) — use `T | …` and mark the whole
  type `?`"). `T??` collapses to `T?` (`resolve.tks:1439`). `void?` is illegal
  (`resolve.tks:1437`).
- **Never-null reference (R2).** `Reference` (`type.tks:90`) is never null:
  `resolve.tks:1438` rejects `Ref<T>?`, and `resolve.tks:1390` *separately*
  rejects a `Reference` as any variant/struct/collection member ("a reference
  cannot be stored…"). So `Ref<T> | null` is **already doubly-blocked**.
- **Runtime rep of `T?` (native C backend).** `codegen.tks:1068-1074`:
  each distinct inner maps to `tk_opt_<mangle>` = `struct { bool present;
  <innerC> value; }`. The `bool present` (1 byte) IS the discriminant the owner
  describes. Optional-of-value is a tagged struct.
- **Runtime rep of `A | B` (native C backend).** `codegen.tks:7237-7300`:
  `tk_u_<keys>` = `struct { tk_tag_<keys> tag; union { … } as; }` where the tag
  is a C `enum` (`typedef enum … tk_tag_<keys>`). **A C enum is int-width — 4
  bytes** on the LP64 targets. This asymmetry (optional's 1-byte `bool` vs
  variant's 4-byte `enum`) is the crux of the representation analysis (§6).
- **`null` literal lowering (LIR).** `lower.tks:4310-4314` `lower_null_lit`:
  `null -> const_int 0` (a zero word). The VM/LIR path carries `null` as a zero
  slot; the native C backend carries an optional as the `tk_opt` struct. There is
  **no** pointer niche-optimization in the C backend today (see §6).
- **`error?` is the pervasive Result idiom.** 77 declarations in `src/` return
  `-> error?` ("ok = no error present; !ok = error present"), e.g.
  `typer.tks:1276 cast_check`, `collect.tks:911 check_conformance`. Modelled as
  `error?` *precisely because* `void` cannot be a value/member (`type.tks:57-60`)
  and there is no `null` type. Under the proposal this becomes `null | error`.
- **Sentinels.** A bare `null` literal today carries the SENTINEL optional
  `Optional{inner=Void}` (`type.tks:128-135`), which `type_eq` treats as equal to
  ANY concrete optional. `type_has_sentinel` (`resolve.tks:1188-1194`) and
  `type_has_void_sentinel` (`resolve.tks:1293-1300`) each carry an `Optional`
  arm. The empty-collection sentinel (`Slice{Void}`, e.g. `list::empty()`) is a
  SEPARATE mechanism and is *not* touched by this proposal.
- **Operators.** `ast.tks`: `NullLit` (§16), `SafeFieldAccess` `x?.f` (§46),
  `Coalesce` `x ?? y` (§47), `SafeMethodCall` `x?.m()` (§49), `NullPattern`.
  `parse_type.tks:49-53` builds `OptionalType` from a postfix `?`.
- **Monomorphization touch-points.** `subst_type` (`type.tks:1269-1289`) and
  `unify`/`type_has_void_sentinel` (`resolve.tks:1293-1310`) each carry an
  `Optional` arm.

Every listed claim is confirmed. Nothing in the seed contradicts the owner's
framing; the disagreement is purely about *representation cost* and *how much
sugar to keep*, both resolvable below.

---

## 2. Steelman of the owner's model (before any objection)

The proposal is **coherent, KISS-positive, and in two places strictly BETTER
than today**. Stated at its strongest:

1. **One sum former, not two.** Teko already has a general sum type (`Variant`).
   Nullability is just the special case `T | null`. Carrying a *second*
   dedicated former (`Optional`) plus its own mangling (`tk_opt_`), its own
   sentinel arm, its own resolve rules and its own three operators is
   redundant machinery. Collapsing to one former deletes a whole parallel track.
2. **`error?` → `null | error` is more honest.** The success case of a fallible
   void operation is currently spelled `error?` where "no error present" means
   success — but the *value* of success has no name. Under the proposal success
   is literally the value `null : Null`. The `void`/`null` pair becomes crisp:
   **`void` = "returns nothing to bind"; `null` = "a present, 1-byte, bindable
   unit value meaning absence."**
3. **Optional-of-value is ALREADY tagged.** `tk_opt_i32` is
   `{bool present; int32_t value}`. The owner's "1 byte discriminant, zero-fill"
   is *exactly* the `present` byte that exists today. So for value payloads, the
   union model adds **zero** runtime cost — it is the same struct, renamed.
4. **It removes a real footgun.** The current split forces users to learn *two*
   ways to say "or nothing": `T?` for null, `T | error` for errors, with a
   compiler error if you cross them (`resolve.tks:1389`). One former = one mental
   model, aligned with errors-as-values.
5. **Two concrete size WINS are available** (see §6): niche-filled
   `ptr<T> | null` / `ClassRef | null` drop from 16 → 8 bytes, and niche-filled
   `null | error` drops from **24 → 16 bytes**. Since `error?` is the single most
   common fallible signature in the compiler, this is a throughput win exactly
   where the owner is most sensitive.

6. **Pointer/reference nullability becomes HONEST IN THE TYPE (owner-endorsed,
   2026-07-19).** Under the union model a bare `ptr<T>` / `Ref<T>` is a
   trustworthy never-null handle, and `ptr<T> | null` / `Ref<T> | null` is the
   *only* nullable form — and it is explicit at the type level. This abolishes
   the "null pointer you couldn't trust": the type now tells you exactly when a
   handle may be absent, and the checker forces you to narrow before deref (§3d).
   The niche representation delivers this honesty at ZERO size cost — the nullable
   handle is one word, `null` = zero handle. This is the owner's variant-sizing
   goal met literally for the handle classes.

**Verdict on the steelman:** the model *wins* on parsimony and honesty, and can
win on size. It does NOT win on the literal claim "null, nada mais" — see §6.1:
`null` alone is 1 byte, but `i32 | null` is unavoidably `discriminant + i32`
because `0` is a valid `i32`. The owner's "1 byte" is the discriminant, not the
whole value. That is the single factual correction; everything else survives.

---

## 3. Surface design (A/B, one decision per block)

### 3(a) `null` as a type-atom

**LEGAL (proposed):**

```
mut valor: i32 | null = null        // Null widens into i32 | null
let x: i32 | null | null = null     // idempotent — dedup to i32 | null
fn find(k: str) -> Node | null      // absence as an ordinary member
let r: str | null | error = fetch() // triple union, no special-casing
```

**Set-semantics.** Union membership is a SET: `null` appears at most once;
`i32 | null | null` ≡ `i32 | null` via the existing dedup in `union_collect`
(`resolve.tks:1215`, `type_eq`). No new machinery — `Null == Null` is one
`type_eq` arm.

**`null` standalone** is `Null` — a 1-byte zero-fill unit (the owner's exact
spec). Its only inhabitant is `null`. It is a legal binding type
(`let n: null = null`), unlike `void`.

### 3(b) `?.` / `??` / postfix `T?` — **CLOSED: OPTION B — NO SUGAR AT ALL (owner, 2026-07-19)**

The owner ruled Option B: **remove every nullability-specific syntactic form.**
This is the maximally-KISS end-state — nullability IS union-handling, with no
exceptions and no parallel operators.

**DELETED (5 forms):**
- the type spelling `T?` — the parser's `OptionalType` former is removed WHOLE
  (`parse_type.tks:49-53`);
- the type spelling `error?` (a special case of the above);
- the operator `?.` — `SafeFieldAccess` (`ast.tks:46`);
- the operator `??` — `Coalesce` (`ast.tks:47`);
- the operator `?.m()` — `SafeMethodCall` (`ast.tks:49`);
- and, in the checker, the `Optional` former itself (`type.tks:71`, from the
  `Type` variant `type.tks:93`). Nullability is ONLY `T | null` carried by
  `Variant`.

**KEPT (2):** the VALUE `null` (`NullLit`, `ast.tks:16`) and the `NullPattern`
(so `match x { null => … }` works). These are the atoms of the union model, not
sugar.

**Consequence:** the ONLY ways to consume a `T | null` are `match` and the
`if x == null` / `if x != null` flow-narrow (§3f). There is no `?.`/`??`
shortcut. Every existing corpus/stdlib `?.`/`??`/`T?`/`error?` site is rewritten
to `match`/`if` + explicit `T | null` (the wave's largest churn — §7, §8 Crumb 6).

### 3(c) `error?` → explicit `null | error`; canonical member order

`error?` is DELETED as a spelling; the 77 stdlib `-> error?` signatures become
explicit `-> null | error` (mechanical rewrite, §8 Crumb 6). Semantics unchanged:
`null` = success, `error` = failure.

**Canonical member order (`null | error`, not `error | null`) — a REQUIRED design
point.** Two facts collide today, so a written union must be normalized to ONE
canonical form:

- Inline-variant C mangling is built in SOURCE ORDER (`codegen.tks:1197`
  `cg_variant_typename_str`): `null | error` → `tk_u_null_error`,
  `error | null` → `tk_u_error_null` — two DIFFERENT typedefs for one type.
- `type_eq` for `Variant` uses order-sensitive `types_eq` (`type.tks:140,152`),
  so `null | error` and `error | null` currently compare UNEQUAL.

Resolution (minimal + behavior-preserving): **normalize `null` to a fixed
canonical position at resolve time — recommend `null` FIRST** (`null | error`,
`null | i32`), matching the owner's spelling and the "absent/discriminant case
first" reading. Normalizing only the `null` member's position leaves every
existing null-free `T | error` mangle untouched (zero regression), while
guaranteeing `null | error` and `error | null` resolve to ONE type + ONE mangled
typedef. Runs in the same `union_collect` dedup pass (`resolve.tks:1199`) — an
**S** add. (Checker-internal; users may still WRITE either order.)

**Canonical member order (`null | error`, not `error | null`) — a REQUIRED
design point.** The mechanical sugar rule `T? ≡ T | null` would put `null` last
(`error | null`), but the CANONICAL Teko spelling is `null | error` (owner's
honesty framing, and the errors-as-values convention that error is the trailing
"failure" arm). These denote the SAME type only if union member order is
normalized, because two facts collide today:

- Inline-variant C mangling is built in SOURCE ORDER (`codegen.tks:1197`
  `cg_variant_typename_str`), so `null | error` → `tk_u_null_error` but
  `error | null` → `tk_u_error_null` — two DIFFERENT typedefs for one type.
- `type_eq` for `Variant` uses order-sensitive `types_eq` (`type.tks:140,152`),
  so `null | error` and `error | null` currently compare UNEQUAL.

Resolution (recommend, minimal + behavior-preserving): **normalize `null` to a
fixed canonical position at resolve time** — recommend `null` FIRST in the
2-member absence form so the canonical render is `null | error` / `null | i32`
(matches the owner's spelling and the "discriminant/absent case first" reading).
Normalizing only the `null` member's position leaves every existing
null-free `T | error` mangle untouched (zero regression), while guaranteeing
`T?`, `error?`, `null | error`, and `error | null` all resolve to ONE canonical
type + ONE mangled typedef. This normalization runs in the same `union_collect`
dedup pass (`resolve.tks:1199`) that already flattens/dedups members — an **S**
add. (This is a checker-internal canonicalization; users may still WRITE either
order.)

### 3(d) Nullable references — **R2 RELAXED (owner, 2026-07-19): `Ref<T> | null` now ALLOWED**

The owner relaxed R2. The new law:

- **BARE `Ref<T>` stays never-null** — the R2 trust survives for the bare form.
  A plain `ref`/`Ref<T>` is guaranteed to point at a live target; code may deref
  it with no null check. This is the confidence the ref-model depends on.
- **`Ref<T> | null` is the ONLY nullable reference, and it is EXPLICIT.** A
  nullable ref must be spelled at type level as the union. This makes ref
  nullability *honest in the type* (the same honesty win the owner endorsed for
  pointers, §2.6 below).
- Consequently `resolve.tks:1438` (Optional-over-Reference) is REPLACED, and the
  `Reference`-as-variant-member rejection at `resolve.tks:1390` must be RELAXED to
  admit the two-member `Ref<T> | null` shape specifically (while still rejecting
  a bare `Reference` as a *stored* struct/collection member — R4 storability is a
  separate concern from nullability). `ptr<T> | null` is likewise allowed and,
  like `Ref<T> | null`, niche-fills to a bare nullable handle (§6.5) — the
  variant-sizing free lunch: **null = zero handle, cost ZERO**.

**THE NARROWING INVARIANT — UNIVERSAL (owner ruling, 2026-07-19).** The invariant
is NOT special to ref/ptr; it is the general union rule. See §3(f); the ref case
is one instance. Restated for ref: a deref / `.value` / auto-deref of a
`Ref<T> | null` is rejected until narrowed to the bare `Ref<T>` arm.

```
ref r: Node | null = maybe_node()   // Ref<Node> | null
let v = r.value                     // ILLEGAL — "cannot deref a possibly-null reference; narrow with `match` or `if x != null` first"
match r {
    null => handle_absent()
    Node as n => use(n.value)       // LEGAL — narrowed to bare Ref<Node>, R2 holds
}
```

**Safety-leak proof (why the invariant is load-bearing).** Suppose the checker
did NOT require narrowing and allowed `r.value` directly on a `Ref<T> | null`.
The runtime rep is niche-filled: `null` is the zero handle. Then `r.value`
lowers to a load through a zero pointer — an unguarded NULL dereference — exactly
the "untrustworthy null pointer" the owner's honesty argument exists to abolish.
Worse, it would silently VOID the bare-`Ref<T>` guarantee: if a possibly-null ref
could be dereffed as if bare, then "bare ref is never-null" would no longer be a
property the compiler enforces — a caller could launder a nullable ref into a
context expecting a bare ref by dereffing. The invariant is precisely the fence
that keeps the *bare* form's R2 promise sound: the type says `| null`, so you
MUST narrow before you may deref; once narrowed the type IS bare `Ref<T>` and R2
applies with full force. Without the fence, R2 leaks.

### 3(e) `ref` precedence — RESOLVED by owner (2026-07-19): `ref x: T | null` ≡ `Ref<T | null>`

The owner ruled the precedence (confirming the coordinator's recommendation, in
his own spelling): **`ref` is a binding modifier applied to the ENTIRE type**, so
`ref x: T | null` ≡ `Ref<T | null>` — a PRESENT, never-null ref TO a nullable
value. This is NOT ambiguous: by definition `ref` wraps the whole (already-parsed)
type. Two DISTINCT, coexisting spellings result, and both are legal:

| Spelling | Equivalent type | Handle | Referent | You narrow… |
|---|---|---|---|---|
| `ref x: T \| null` | `Ref<T \| null>` | always valid (never-null) | may be `null` | the REFERENT (`x.value : T \| null` → narrow to `T`) |
| `y: Ref<T> \| null` | `Ref<T> \| null` | may be `null` | (only reachable once handle non-null) | the HANDLE (narrow to bare `Ref<T>`, then deref) |

- `ref x: T | null` ≡ `Ref<T | null>`: the reference itself is guaranteed live
  (bare-ref R2 holds — deref `x.value` is always safe), but what it yields is
  `T | null`, which you then narrow like any union before using as `T`.
- `y: Ref<T> | null`: the HANDLE may be absent; you narrow the handle to bare
  `Ref<T>` (§3d) before you may deref at all; the referent is then a plain `T`.

Parser rule: `ref` in a binding-modifier position consumes the modifier, parses
the type-expression to completion (including any `|` union), then wraps it as
`Ref<…>`. `ref` NEVER participates in union precedence, so `ref x: T | null` has
exactly ONE parse (`Ref<T | null>`). A type-level `ref T | null` (using `ref` as a
type operator) is a parse error: **"`ref` is a binding modifier, not a type
operator; write `Ref<T | null>` for a live ref to a nullable value, or
`Ref<T> | null` for a nullable ref."**

### 3(f) UNIVERSAL narrowing invariant — the whole rail is the EXISTING union rail (owner ruling, 2026-07-19)

The owner ruled that the narrowing invariant is **universal to every sum type**,
not special to ref/ptr:

> A value of type `T | null` (indeed any union) may be used AS `T` ONLY after it
> has been NARROWED to that member. Touching the payload of the union without
> narrowing is a compile error — the SAME error as touching a `T | error` without
> matching it.

The **TWO** narrowing forms the owner accepts under Option B (nothing more — `?.`
is DELETED and is no longer a narrowing form):

1. **Exhaustive `match`** — `match x { T as t => …; null => … }`; each arm sees
   the narrowed member type.
2. **Equality-guard flow-narrowing** — `if x == null { … } else { /* x : T */ }`
   and symmetrically `if x != null { /* x : T */ } else { … }`. In the branch
   where `x` is statically known non-null, its type flow-narrows to `T`.

Checker requirement to document (two narrowing sites): `x` narrows to its
non-null member (a) inside each `match` arm; (b) in the `else` of `if x == null`
and the `then` of `if x != null`. Any deref / field-access / method-call /
value-use that consumes the union payload OUTSIDE a narrowed scope is rejected
with the same diagnostic family as an unmatched `T | error`.

**Incremental-cost proof (why this adds NO new machinery — the owner's key
point).** The checker ALREADY implements every piece for `T | error`:

- **Exhaustiveness / arm-narrowing for `match`** already exists — `match.tks`
  drives `T | error` matches and binds each arm to its member type (`as`
  binding). Adding `null` as a member means the `null` arm binds `Null` and the
  `T` arm binds `T` through the *same* code path; the only delta is that `Null`
  is a member the exhaustiveness checker must see covered — one more member in an
  existing coverage set, not a new algorithm.
- **"May not use a union as its member" rejection** already exists — the checker
  already forbids using a `T | error` value where a `T` is required until it is
  destructured; the deref/field/method guards reuse that "is this a bare member
  or still a sum?" test. `T | null` is the identical test with `null` in the
  member set.
- **Flow-narrowing on an equality guard** is the ONLY genuinely incremental
  piece, and it is small and local: recognize `x == null` / `x != null` in an
  `if` condition and, in the corresponding branch, replace `x`'s static type with
  the union minus `null`. This is a single condition-shape recognizer plus a
  scoped type-environment override — bounded, one function, no new global pass.
  (Under Option B this is the ONLY addition; the deleted `?.`/`??` lowering paths
  are REMOVED code, not added.)

So treating null-handling AS union-handling is **the same rail**: it reuses
`match` exhaustiveness, arm binding, and the sum-not-a-member guard verbatim, and
adds only a localized equality-guard flow-narrower. This is the cheapest possible
implementation shape and is exactly why the owner prefers it over a bespoke
optional-nullability subsystem. Cost estimate: the flow-narrower is an **S** add
in `typer.tks`; everything else is arms folded into existing `Variant` handling.

---

## 4. The illegal/legal error-surface table

| Form | Today | Proposed | Message |
|---|---|---|---|
| `i32 \| null` | illegal (no `Null`) | **legal** | — |
| `i32 \| null \| null` | illegal | **legal**, dedup to `i32 \| null` | — |
| `T?` (type spelling) | legal | **DELETED** (Option B) | "`T?` is gone — write `T \| null`" |
| `error?` (type spelling) | legal | **DELETED** (Option B) | "`error?` is gone — write `null \| error`" |
| `x?.f`, `x ?? y`, `x?.m()` | legal | **DELETED** (Option B) | "`?.`/`??` are gone — use `match` / `if x != null`" |
| `Ref<T> \| null` (nullable ref, handle may be null) | illegal | **LEGAL** (owner relaxed R2; niche handle) | — |
| `ref x: T \| null` ≡ `Ref<T \| null>` (live ref, nullable referent) | illegal | **LEGAL** | — |
| bare `Ref<T>` | never-null | **never-null (R2 kept for bare)** | — |
| USE of any `T \| null` (or any union) payload as `T` before narrowing | n/a | **illegal (UNIVERSAL narrowing invariant, §3f)** | same diagnostic family as an unmatched `T \| error`: "value may be `null`; narrow with `match` / `if x == null` first" |
| `ref T \| null` using `ref` as a TYPE operator | n/a | **parse error** | "`ref` is a binding modifier, not a type operator; write `Ref<T \| null>` for a live ref to a nullable value, or `Ref<T> \| null` for a nullable ref" |
| `void \| null` | illegal | **illegal** | "`void` is not a value (M.3)" |
| `null` (standalone binding) | illegal | **legal**, type `null`, 1 byte | — |

---

## 5. Interactions

- **Sentinel / generic inference.** Bare `null` stops being the polymorphic
  `Optional{inner=Void}` sentinel (`type.tks:128`) and becomes the CONCRETE type
  `Null`. Assignability is then ordinary union widening ("`Null` widens into any
  union whose members include `Null`"), which is *simpler* than the sentinel
  dance. The `Optional` arms in `type_has_sentinel` (`resolve.tks:1191`) and
  `type_has_void_sentinel` (`resolve.tks:1297`) are DELETED. The empty-collection
  sentinel (`Slice{Void}`) is untouched.
- **`void`/`null` pair.** `void` stays return-only (M.3). `null` is the value
  that `void` never was. `error?`-as-success finally has a named value.
- **Idempotence / collapse.** Handled by union set-semantics
  (`union_collect` dedup). `T??` → `T | null | null` → `T | null` for free.
- **Monomorphization.** `subst_type` and `unify` lose their `Optional` arm; the
  existing `Variant` arms already handle members structurally. `Null` is a leaf
  (`_ => t` in `subst_type`), like `Byte`/`Str`.
- **Borrow / spine.** A stored borrow of a `T | null` field is a borrow of a
  union field — the existing variant-field borrow rules apply; the
  optional-specific paths in `borrow.tks`/`spine.tks` fold into them.
- **FFI boundary.** `ptr<T> | null` niche-fills to a bare C pointer (NULL = null)
  — the clean FFI shape. `i32 | null` marshals as the tagged struct (same as
  today's `tk_opt_i32`; no native C analog — unchanged).

---

## 6. Representation validation (the crux — the owner is throughput-sensitive)

### 6.1 `i32 | null` CANNOT be "i32 with 0 = null"

`0` is a valid `i32`. There is no bit pattern of a 32-bit integer free to mean
"absent". Therefore `i32 | null` **must be tagged**: `discriminant + i32 payload`.
The owner's "1 byte, `0b00000000`" is correct for a *standalone* `null` (the
`Null` type genuinely is 1 byte), and it is correct as the *discriminant* of the
union — but the i32 payload still occupies its 4 bytes. "null, nada mais" holds
for the atom, not for the union carrying a real payload.

### 6.2 Exact layouts (LP64: `bool`/`enum`, `int`=4, pointer=8, `tk_str`={ptr,len}=16)

| Type | Today | Naive "2-member Variant" (4-byte enum tag) | Proposed unified former (uint8 tag + niche) |
|---|---|---|---|
| `i8 \| null` | `tk_opt_i8` `{bool;int8}` = **2** | `{enum(4);int8}` = **8** ✗ regress | `{u8;int8}` = **2** ✓ |
| `i32 \| null` | `tk_opt_i32` `{bool;int32}` = **8** | `{enum(4);int32}` = **8** | `{u8;int32}` = **8** ✓ |
| `i64 \| null` | `tk_opt_i64` `{bool;int64}` = **16** | `{enum(4);int64}` = **16** | `{u8;int64}` = **16** ✓ |
| `ptr<T> \| null` | (not emittable today¹) | `{enum(4);ptr}` = **16** | niche `T*`, NULL=null = **8** ✓ WIN |
| `ClassRef \| null` | `tk_opt_C` `{bool;C*}` = **16** | `{enum(4);C*}` = **16** | niche `C*`, NULL=null = **8** ✓ WIN |
| `null \| error` (`error?`) | `tk_opt_error` `{bool;tk_str}` = **24** | `{enum(4);tk_str}` = **24** | niche `tk_str.ptr==NULL` = **16** ✓ WIN |

¹ `codegen.tks:1082-1109` `cg_opt_mangle` has NO `Ptr` arm — `ptr<T>?` currently
falls through to "optional/slice inner type not yet supported". So there is **no
existing pointer niche to regress**; the union model is free to ADD one.

**Niche safety proofs.** A `ClassRef`/`ptr` object pointer is never address 0
(M.1: allocation panics on OOM, never returns NULL — `teko_rt.h:115`). A live
`error`'s `tk_str.ptr` is never NULL (empty strings use a 1-byte buffer —
`teko_rt.h:299`). So NULL is a sound "not present"/"not error" niche in all three
pointer-shaped cases.

### 6.3 Representation verdict

**No regression — and a net WIN — IF AND ONLY IF the unified former is built with
two properties the naive "just reuse `Variant`" path lacks:**

1. **1-byte discriminant** (`uint8_t`, the owner's literal `0b00000000`) instead
   of the current 4-byte `enum` tag, for the non-niche tagged case. This restores
   parity with today's `bool present` for small scalars (else `i8|null` regresses
   2→8).
2. **Niche-filling** for members with a spare bit pattern (any single pointer- or
   `tk_str`-shaped member alongside `null`). This IMPROVES three pervasive shapes
   over today: `ClassRef|null` 16→8, `ptr<T>|null` new-8, `error?` **24→16**.

A **naive big-bang** that spells `T | null` as a plain `tk_u_` with the existing
4-byte enum tag **DOES regress** every small-scalar and pointer optional and is
therefore unacceptable. The recommendation (§8) sequences the niche/tag work
BEFORE routing `T?` through the unified former, so no intermediate build ships a
regression.

### 6.4 VARIANT-SIZING — "pay for the active member, not the max" (owner constraint, 2026-07-19)

The owner sharpened the requirement beyond "no regression": a `u128 | null` that
is currently `null` should cost ~1 byte, not 17 (nor the 32 that alignment
actually forces). This is **variant-sizing** (pay for the ACTIVE member). It is
achievable for some type classes and physically impossible for others; the honest
analysis, and where the dial lives, follows.

**Fact 1 — an inline fixed slot cannot shrink.** A stack local or a struct field
has a fixed layout chosen at compile time. If `mut valor: u128 | null` must be
able to hold a `u128` after a later `valor = big`, the slot must reserve room for
the `u128` *for its whole lifetime*. There is no "1 byte while null, 16 bytes
after assignment" for an inline binding without dynamic stack resizing (which
Teko does not and should not do). **So variant-sizing REQUIRES an indirection: a
niche or a box (handle).** This is a property of statically-sized frames, not a
Teko limitation.

**Fact 2 — niche is the free variant-sizing.** For a member with a spare
bit-pattern (`ptr`, `ClassRef`, `Reference`-shaped, a range-restricted int, an
enum with a free tag value), `X | null` is ONE word: the value itself, with `0`
(NULL) meaning `null`. Zero extra bytes, zero indirection, zero heap. For these
classes the owner's goal is met *exactly*: the `null` case costs nothing beyond
the word already spent on `X`.

**Fact 3 — saturating value types have NO free niche.** `i32`, `u128`, `f64`,
`bool` — every bit pattern is a valid value, so there is no spare code for
`null`. Only two representations exist, and they are a DIAL:

- **(a) INLINE TAG** — `{uint8 tag; payload}`, max-sized, fast (no indirection,
  no allocation). This is today's `tk_opt_` shape. `u128 | null` = 32 bytes
  (tag + 15 pad + 16), always, regardless of the active member. **Not
  variant-sized.**
- **(b) BOX** — the slot is a pointer (8 bytes); `null` = NULL pointer (0 heap),
  non-null = pointer to a heap/arena cell holding the payload. **Variant-sized in
  the HEAP dimension** (null allocates nothing), but the slot is still a pointer,
  and every non-null access pays one allocation + one load (indirection).

**Fact 4 — the tripartite-model "contradiction" is not one.** The ref-model
digest calls optionals "reference category, capability-passing", yet today's
codegen emits `tk_opt_i32` inline by-value. These are ORTHOGONAL axes:
"reference category" is the *semantic* role (a nullable place that sinks to a
value on unwrap — how it copies and passes), while inline-vs-box is the
*physical* layout. A small optional can be semantically reference-category and
physically inline. The variant-sizing decision is purely physical and does not
disturb the semantic category. So there is no contradiction to resolve — only a
physical dial to set per size class.

**Fact 5 — arena makes the box allocation cheap, but not free of indirection.**
Teko's arena means the box's backing cell is a bump-pointer allocation (near-free
vs `malloc`), and it is bulk-freed with the region. This DEMOLISHES the
allocation objection to boxing. What arena does NOT remove: (i) the pointer
indirection — one extra load per read of a non-null boxed value; (ii) cache
locality loss — the payload is off in the arena, not in the stack frame/struct.
For a hot `u128` in a tight loop, inline (payload in-frame, often in registers)
still beats a pointer chase. So arena moves the threshold DOWN (boxing viable for
more types) but does not make boxing the unconditional default for speed-critical
large scalars.

### 6.5 The numeric threshold and the per-class recommendation

Boxing SAVES `stack_bytes(inline) − 8` bytes of slot and adds one arena
bump-alloc + one load per non-null access. It only makes size sense when the
inline slot exceeds one pointer:

| Payload | inline-tag slot | box slot | slot bytes saved by boxing | verdict |
|---|---|---|---|---|
| `i8`..`i32`, `bool`, `char`, `byte` (≤4B) | ≤8 | 8 | ≤0 | **INLINE — boxing never wins** |
| `i64`/`u64`/`f64` (8B) | 16 | 8 | 8 | inline (tie/faster); box only if null-dominated |
| `u128`/`i128` (16B) | 32 | 8 | 24 | **DIAL — box-in-arena when size matters** |
| large by-value struct (>16B) | payload+align | 8 | large | **BOX-in-arena default** |
| `ptr`/`ClassRef`/`Reference`-shaped | 8 (niche) | — | — | **NICHE — 1 word, 0 overhead** |

**Threshold: boxing begins to win at payload ≥ 16 bytes** (i.e. `u128`/`i128`
and larger aggregates). Below one pointer (8B) it is strictly worse (no bytes
saved, pure added cost); at exactly one word it is a wash on size and a loss on
speed. Recommendation per class:

- **Pointer / class / reference-shaped → NICHE.** Free variant-sizing. Do this
  unconditionally (§6.2 already shows the 16→8 win).
- **Saturating scalar ≤ 8 bytes → INLINE TAG** (`{uint8 tag; payload}`). Fast,
  predictable, and the `null` costs exactly the owner's 1 discriminant byte on
  top of a payload slot that must exist anyway.
- **Saturating payload ≥ 16 bytes → BOX-IN-ARENA** (default), with an escape hatch
  to force inline for a proven hot path. The slot is one pointer; `null` = NULL =
  zero heap; non-null = one arena cell. This is where the owner's `u128 | null`
  lives: **8-byte slot, zero heap when null.**

**CLOSED (owner, 2026-07-19): "vou na recomendação A + hatch."** The dial is
settled: for a saturating payload ≥ 16 bytes the DEFAULT representation is
box-in-arena (8-byte handle, zero heap when `null`, arena bump-alloc when
present), and an EXPLICIT ESCAPE HATCH (`#inline`) forces inline-tag on a
proven-hot binding (always max-size, but zero-indirection reads). §6.7 defines
`#inline` as a GENERAL sum-representation directive (not null-only) — RATIFIED
(owner, D10).

### 6.6 Does this satisfy the owner's "1 byte when null"? — honest gap

Partly, and the residual gap must be reported plainly:

- **Niche classes (ptr/class/ref):** YES in spirit — the `null` case costs ZERO
  extra bytes (the word is the value's own word). Not literally "1 byte", but
  "no byte" — better.
- **Small saturating scalars:** the null-ness costs exactly **1 byte** (the
  discriminant), matching the owner's `0b00000000`. But the payload slot is
  always reserved, so the WHOLE binding is `payload + 1`, never 1 total.
- **`u128 | null` (the owner's example):** literal "1 byte when null" is
  **physically impossible for a mutable inline binding** — the slot must be able
  to become a `u128`. The best achievable is **8 bytes (a pointer) + zero heap
  when null** via box-in-arena. So there is an HONEST GAP: a mutable
  `u128 | null` local occupies 8 bytes (boxed) or 32 bytes (inline) even while
  its value is `null`; it is never 1 byte.

The reason is fundamental: a statically-compiled native frame reserves the space
a binding might need over its lifetime. The mental model where "null costs 1
byte" holds LITERALLY in the VM (uniform boxed word slots — `lower.tks:4310`
already carries `null` as a zero word), and holds in the HEAP dimension natively
(box-in-arena: null allocates nothing). It cannot hold for a native inline slot
without dynamic stack resizing. **Recommendation to the owner: adopt "pay for the
active member's HEAP footprint, not the max" as the precise, achievable form of
variant-sizing — niche where free, box-in-arena above 16 bytes — and accept an
8-byte handle (not 1 byte) as the irreducible cost of a mutable large-scalar
slot.**

### 6.7 The `#inline` hatch — a SUM-REPRESENTATION directive, NOT null-only (owner's scope question, 2026-07-19)

> The owner approved the hatch but challenged its scope: *"apenas para nulls é
> muita arrogância."* He is right. `#inline` is NOT a null feature. It is a
> **representation directive for ANY sum type (`Variant`)** whose default would be
> box-in-arena — `T | null`, `T | error`, `A | B | C`, alike. Nullability is just
> the case that motivated it; the directive is general.

**Real scope.** `#inline` forces the INLINE-TAG representation (in-frame /
embedded, max-size, zero-indirection reads) instead of the default box-in-arena,
for a binding or field whose type is an ELIGIBLE sum. Eligible = a `Variant`
whose non-null/active members are SATURATING (no niche) and whose max payload is
≥ 16 bytes — i.e. exactly the sums that box by default.

```
#inline mut hot: u128 | null = null         // absence-heavy sum, hot path — force inline
#inline result: BigVal | error = compute()  // SUCCESS-heavy sum — the T is the hot path; inline it
type Particle = struct {
    #inline pos: Vec4 | error                // per-FIELD: embed inline in the struct layout
    vel: Vec4 | error                        // default: boxed handle (8B) in the struct
}
```

**Chosen spelling: a `#`-attribute on the binding/field** (over a type modifier
`inline u128 | null` or a use-site cast). Rationale: representation is a property
of STORAGE, so it belongs on the declaration, not the type — a type modifier
would make `inline S` and `S` the same TYPE with different rep, forcing `type_eq`
to ignore it (the confusion the attribute avoids); a use-site cast cannot move
where a slot lives. The `#`-attribute rides the decl, leaves `type_eq`/mangle
untouched, reuses the existing `#test`-family lexical channel (no new grammar),
and applies uniformly to a local AND a struct field.

**Eligibility rule + error (three ineligible classes, never a silent no-op):**

```
#inline mut a: i32 | null = null   // ERROR — sum payload < 16B; already inline
#inline mut p: Node | null = ...   // ERROR — niche-able sum (ptr/ref/class handle); already optimal
#inline rec: Tree | null = ...     // ERROR — a self-recursive sum MUST box (inline would be infinite-size)
#inline mut n: i32 = 0             // ERROR — not a sum type
```

Message: **"`#inline` is a sum-representation directive for a boxed-by-default
`Variant` with a saturating payload ≥ 16 bytes (e.g. `u128 | null`,
`BigVal | error`); `<T>` is <already inline (payload < 16B) | a niche handle
(ptr/ref/class) | self-recursive (must box) | not a sum> — remove `#inline`."**
(The recursive case is a HARD ineligibility: an inline self-recursive sum has no
finite size, so box is mandatory, not a default.)

**The default nuance the owner flagged — absence-heavy vs success-heavy.**
Box-in-arena is the CORRECT default for ABSENCE-heavy sums (`T | null` where
`null` is common and the big payload is rare — the boxed slot stays a NULL
handle most of the time, zero heap). But for SUCCESS-heavy sums (`BigVal | error`
where the happy path is the large `T` and errors are rare), boxing puts one
indirection on the HOT path — the opposite of what you want. Two ways to handle
this:
- **(i) `#inline` covers it (RECOMMEND for this wave).** Keep ONE simple default
  (box-in-arena above 16B, purely size-driven) and let the author flip the
  success-heavy hot cases with `#inline`. Predictable, no heuristics, one rule to
  learn. The valve is exactly the success-heavy case.
- **(ii) A representation-sensitive default** that inspects "which member is the
  hot/common one." Rejected for now: the compiler cannot know call-site frequency
  without profile data; any static guess (e.g. "error member ⇒ treat as
  success-heavy ⇒ inline") is a hidden heuristic that surprises users and couples
  layout to member identity. Simpler to keep the size-only default and expose the
  knob.

So: **default = size-driven box-in-arena; `#inline` is the single, explicit valve
for BOTH the absence-heavy-but-hot and the success-heavy cases.** This is why the
directive must be general, not null-only — the owner's instinct is correct.

**Local vs struct-field interaction.**
- **Local binding:** `#inline` makes the local's slot the inline-tag struct in the
  frame (max-size, no indirection). The frame simply reserves the larger slot.
- **Struct field:** `#inline` embeds the inline-tag struct in the aggregate's
  layout instead of an 8-byte handle — per-field, changing the struct's
  size/offsets: a deliberate ABI choice for a hot, cache-sensitive aggregate.
  Non-`#inline` fields stay boxed handles (compact when the rare member
  dominates). Both are honored by the SAME codegen member-representation switch;
  the attribute only flips that one field's default.

**FUTURE generalization (RECORDED, NOT scoped for this wave).** The same
storage-attribute channel could later carry `#inline` / `#boxed` to control the
boxing of LARGE VALUE types generally (not just sum members) — e.g. `#boxed` a
big by-value struct field to keep an aggregate compact, or `#inline` a normally-
boxed one. This is a clean generalization of the representation-directive idea,
but it is a SEPARATE feature with its own design; it is noted here only so the
attribute is designed with room to grow, and is explicitly OUT OF SCOPE now.

---

## 7. Blast radius (file-by-file, S/M/L)

Recalculated for **Option B**. The dominant ADD is that `Type` gains a case
`Null`, so every exhaustive `match` over `checker::Type` gains an arm; the
dominant DELETE is the removal of `Optional` + 3 operators + `OptionalType`, which
REMOVES arms/paths and partly offsets the adds (net wash on several files). `+` =
net addition, `−` = net deletion, `±` = both (offsetting).

| Layer | File | Change (Option B) | Size |
|---|---|---|---|
| Parser | `parser/ast.tks` | `NullLit`/`NullPattern` stay; DELETE `SafeFieldAccess`/`Coalesce`/`SafeMethodCall` nodes from `ExprKind` | − S |
| Parser | `parser/parse_type.tks` | DELETE `OptionalType` former + postfix `?`; `null` becomes a type atom | − S |
| Parser | `parser/parse_expr.tks` | DELETE `?.`/`??`/`?.m()` parse paths | − S |
| Parser | `parser/parse_pattern.tks` | `NullPattern` stays (unchanged) | S |
| Checker | `checker/type.tks` | ADD `Null` case + arms in `type_eq`/`subst_type`/`type_mangle`/every `match t`; DELETE `Optional` case + its arms | ± **L** |
| Checker | `checker/resolve.tks` | ADD `null` union member + canonical `null`-order (§3c); DELETE `OptionalType` resolve, Optional-over-Reference, sentinel arms | ± **L** |
| Checker | `checker/typer.tks` | ADD `Null` to assignable/`emit_as`/`widens_into`/`type_join`; ADD the equality-guard flow-narrower (§3f, the one genuinely new piece) | + **M** |
| Checker | `checker/match.tks` | `NullPattern` over a union `null` member (folds into existing variant match) | S |
| Checker | `checker/borrow.tks`,`spine.tks`,`revalidate.tks`,`check_modules.tks` | DELETE Optional arms; they fold into existing Variant arms | ± M (total) |
| LIR | `lir/lower.tks` | ADD `null` lit → `Null` value; DELETE `lower_coalesce`/`lower_safe_field_access`/`lower_safe_method_call` | ± M |
| Codegen | `codegen/codegen.tks` | ADD `tk_null`, uint8 tag + niche-filling + box-in-arena (≥16B) + `#inline` attr; DELETE the `tk_opt_*` former paths (folded into the unified `Variant`) | ± **L** |
| Backends | native LIR isel / VM value model | `Null` value (1-byte / zero slot); niche-aware loads; box-in-arena + `#inline` | + M |
| Stdlib + corpus | 77 `-> error?` + all `T?`/`?.`/`??`/`?.m()` | REQUIRED mechanical rewrite → `null \| error` / `T \| null` + `match`/`if` (THE biggest churn) | + **L** |
| Tests | `tests/`, corpus fixtures | new regression fixtures (§8); delete operator fixtures | + M |

**Wave size (Option B):** three ± **L** files (`type.tks`, `resolve.tks`,
`codegen.tks`, each simultaneously adding `Null` and deleting `Optional` — the
deletion meaningfully OFFSETS the addition, so these are lighter than a pure-add
L) + one + **L** (the corpus/stdlib rewrite, the real cost) + ~5 M. Net vs the
earlier sugar-keeping estimate: the compiler-internal wave is SMALLER (Option B
deletes 5 forms + 1 former — removed code offsets much of the `Null` add), but the
one-time MIGRATION cost is LARGER (every `?.`/`??`/`T?`/`error?` site rewritten,
not just left as sugar). Overall still one L-sized wave; the migration `L` is
mechanical and fixture-guarded (§8 Crumb 6), so it is churn, not risk.

---

## 8. Crumb plan — FINAL, RATIFIED SEQUENCE (8 gate-able crumbs)

Each crumb is independently gate-able. RITUAL types: **fixpoint** = self-host
gen1==gen2; **size probe** = the native throughput gate (a `sizeof`/heap probe
in emitted C asserting the §6.2/§6.5 targets — required at every crumb that
changes representation). The order guarantees **no intermediate build regresses
layout or breaks the fixpoint**: the representation (C3/C4) lands BEFORE any corpus
value uses `T | null` (C5); the desugar bridge (C5) makes `T?` ≡ `T | null` BEFORE
the corpus is rewritten (C6); the forms are deleted only once dead (C7). The
function shapes are contracts the implementer copies verbatim (full Javadoc).

**Crumb 1 — add the `Null` type case (inert). S. RITUAL: fixpoint.
BEHAVIOR-PRESERVING (no bytes change).** Add the case to `Type` (`type.tks:93`)
plus an arm in every exhaustive `match` over `checker::Type` (type_eq /
subst_type / type_mangle / codegen / VM / backends). Nothing produces `Null` yet.

```
/**
 * The `null` unit type — the sole member of its own domain, a 1-byte zero-fill
 * value (`0b00000000`). Distinct from `void` (return-only, never a value): a
 * `Null` IS a bindable value meaning "absent". Ratified (owner, 2026-07-19).
 *
 * @since null-as-union
 */
pub type Null = struct { }
```

Fixture: `t/null_type_inert.tks` — a file that never mentions `null` compiles
byte-identically to pre-crumb (gen1==gen2).

**Crumb 2 — union accepts a `null` member + null-first canonical order. S.
BEHAVIOR-PRESERVING.** In resolve variant-member validation (`resolve.tks:1387`)
admit `Null` (keep rejecting `void`; relax the `Reference` rejection to admit the
two-member `Ref<T> | null` shape, D3, while still rejecting a bare stored
`Reference`, R4). Normalize `null` to the canonical FIRST position in the
`union_collect` dedup pass (`resolve.tks:1199`) so `null | error` ≡ `error | null`
resolve to one type + one mangle (§3c). Nothing produces such a union yet.

```
/**
 * union_normalize_null — canonicalize a union's member order so `null`, if
 * present, is FIRST (all other members keep source order). Runs inside the
 * dedup fold; guarantees `null | error` and `error | null` share one resolved
 * type and one mangled typedef.
 *
 * @param members the deduped union members (source order)
 * @return the members with any `Null` moved to index 0
 */
fn union_normalize_null(members: []checker::Type) -> []checker::Type
```

Fixture: `t/union_null_member.tks` — `let x: i32 | null = 0` / `= null` both
check; `i32 | null | null` and `error | null` both resolve to the canonical form.

**Crumb 3 — codegen niche + inline-tag classes; `tk_null`. L. BYTES-CHANGING
(new union shapes only — no corpus value uses them yet, so corpus output is
unchanged; justified: establishes the non-regressing representation before any
use). RITUAL: size probe + fixpoint.** Emit `tk_null` (1-byte). Teach the unified
`Variant` former: (a) a `uint8_t` discriminant for the tagged case (replaces the
4-byte enum tag for null-bearing unions), (b) NICHE-filling for a two-member
`X | null` whose `X` is ptr/`Ref`/class/`error`-shaped (emit `X` bare, NULL =
`null`, no tag). Fills the `cg_opt_mangle` ptr gap.

```
/**
 * cg_union_niche_member — for a two-member union `X | null`, return `X` when it
 * carries a spare null bit-pattern (class/ptr/`Ref` pointer, or `error`/`str`
 * `{ptr,len}` whose `ptr` is never NULL for a live value) so codegen emits `X`
 * bare with NULL meaning `null` (no tag word); returns `null` when the union
 * must be tagged (a saturating payload).
 *
 * @param v the resolved union type
 * @return the niche-carrying member, or `null` if the union must be tagged
 * @throws error on an internally malformed member type
 */
fn cg_union_niche_member(v: checker::Variant) -> checker::Type | null | error
```

Size probe `t/repr_niche.tks`: `ClassRef|null`==8, `Ref<T>|null`==8, `ptr<T>|null`
==8, `null|error`==16 (vs legacy `tk_opt_error` 24 — a WIN), `i8|null`==2,
`i32|null`==8, `i64|null`==16. Must show no regression vs recorded `tk_opt_*`.

**Crumb 4 — box-in-arena for saturating ≥16B + the `#inline` general directive.
L. BYTES-CHANGING (new shapes / opt-in). RITUAL: size probe.** Default-box the
saturating ≥16B sums (8-byte handle, NULL = null = zero heap, arena bump-alloc on
non-null). Add the `#inline` binding/field attribute (§6.7) — a GENERAL
sum-representation directive: parse it on the `#` channel, check eligibility, flip
that binding/field's codegen from box to inline-tag.

```
/**
 * inline_attr_eligible — validate a `#inline` attribute: OK only when `t` is a
 * boxed-by-default sum (a `Variant` with a saturating max payload ≥ 16 bytes).
 * Rejects a non-sum, a niche-able sum (ptr/ref/class handle — already optimal),
 * a self-recursive sum (MUST box — inline is infinite-size), and a sum whose
 * payload is < 16 bytes (already inline). Ratified general scope (owner).
 *
 * @param t the resolved type the attribute is applied to
 * @param table the type table (recursion + size resolution)
 * @return null when `#inline` is legal; an error naming the ineligible class
 */
fn inline_attr_eligible(t: checker::Type, table: TypeTable) -> error?
```

Size probe `t/repr_box.tks`: `u128|null` slot ==8 (boxed) with zero heap for the
`null` case; `#inline`-annotated `u128|null` ==32 inline; a `#inline` on an
ineligible type is the exact §6.7 compile error.

**Crumb 5 — the STRUCTURAL PIVOT: `null` literal → `Null`, universal narrowing,
and DESUGAR the legacy surface to the union model. L. BYTES-CHANGING for the
corpus (unified rail; e.g. former `error?` niche 24→16) but BEHAVIOR-PRESERVING
(identical observable semantics). RITUAL: fixpoint + size probe.** (a) Type the
`null` literal as `Null` (retire the `Optional{Void}` sentinel for `NullLit`); add
`Null`-widening. (b) Enforce the universal narrowing invariant (§3f) — using a
union payload as its member is rejected until narrowed by `match` or the
equality-guard; this reuses the existing `T | error` sum-not-a-member guard, the
only new code being the flow-narrower. (c) DESUGAR `T?`→`T | null`,
`error?`→`null | error` at resolve, and `?.`/`??`/`?.m()`→`match`/`if` at
lowering, so the ENTIRE existing corpus runs on the unified rail with unchanged
source and unchanged behavior.

```
/**
 * null_widens_into — a bare `null` (type `Null`) is assignable into a target
 * that is `Null` itself or a union listing `null` among its members (set
 * membership, order-independent).
 * @param target the resolved destination type
 * @param table  the type table (to expand a named-variant target)
 * @return true iff a `Null` value is assignable into `target`
 */
fn null_widens_into(target: checker::Type, table: TypeTable) -> bool

/**
 * narrow_on_eq_guard — the one genuinely new checker piece (§3f): if `cond` is
 * `x == null` / `x != null`, return the branch-scoped type overrides for `x`
 * (the union MINUS `null` in the known-non-null branch), else the env unchanged.
 * The other narrowing form (`match` arms) reuses existing variant machinery.
 * @param cond the resolved `if` condition
 * @param env  the current flow type-environment
 * @return the (then-env, else-env) overrides, or the unchanged env
 */
fn narrow_on_eq_guard(cond: checker::TExpr, env: FlowEnv) -> BranchEnvs
```

Fixtures: `t/null_narrow_match.tks` + `t/null_narrow_ifguard.tks` (bare payload
use rejected; narrowed use compiles), `t/ref_null_narrow.tks` (`r.value` on
`Ref<T> | null` rejected until narrowed), `t/desugar_equiv.tks` (a `T?` fn and its
`T | null` twin emit identical C). Size probe: former `error?` sites now 16 B.

**Crumb 6 — mechanical corpus + stdlib source rewrite. L (the biggest churn).
BEHAVIOR-PRESERVING and BYTE-IDENTICAL (C5 made the forms semantically identical,
so the rewrite changes source spelling only, not emitted bytes). RITUAL: fixpoint
+ byte-identical corpus rebuild.** Rewrite every site: `T?`→`T | null`,
`error?`→`null | error` (the 77 signatures), `x?.f`/`x ?? y`/`x?.m()`→explicit
`match`/`if x != null`. After this crumb no source uses the legacy forms.

Fixture: `t/error_union_migration.tks` — a representative `null | error` fn
round-trips success (`null`) and failure (`error`) on both engines; and the whole
corpus rebuild is byte-identical to its C5 output.

**Crumb 7 — DELETE the dead surface + the `Optional` former. M (net deletion).
BEHAVIOR-PRESERVING and BYTE-IDENTICAL (no users remain after C6).
RITUAL: fixpoint.** Remove `OptionalType` (`parse_type.tks:49-53`),
`SafeFieldAccess`/`Coalesce`/`SafeMethodCall` (`ast.tks:46-49`) + their parse and
lowering paths, the desugar bridge from C5, and the checker `Optional` case
(`type.tks:71,93`) with its `type_eq`/`subst_type`/mangle arms and
`resolve.tks:1438`. Keep `NullLit` + `NullPattern`. This is REMOVED code that
offsets the C1 additions (§7).

Fixture: grep-gate `t/no_optional_former.tks` — the tokens `?`-postfix-type,
`?.`, `??` no longer parse (each is the §4 parse error); the corpus still builds.

**Crumb 8 — sentinel cleanup, Javadoc audit, final gate. S. BEHAVIOR-PRESERVING.
RITUAL: full gate + fixpoint (final ratified end-state).** Confirm the
`Optional{Void}` narrowing/inference sentinel is fully gone (its arms left with
the case in C7) and that the SEPARATE empty-collection `Slice{Void}` sentinel is
UNTOUCHED (`t/empty_slice_sentinel.tks` still infers). Full-Javadoc audit of every
declaration touched across C1-C7 (W15 law). Whole-suite gate, VM + native.

---

## 9. Ratified decision log (all owner, 2026-07-19)

### The ratified position

**Adopt the proposal, staged per §8, Option B (NO SUGAR — delete `T?`/`error?`/
`?.`/`??`/`?.m()` and the `Optional` former; nullability is only `T | null` via
`Variant`), with the unified former built as a THREE-CLASS representation (§6.5):
niche for pointer/class/ref (1 word, 0 overhead); inline `uint8`-tag for
saturating scalars ≤ 8 bytes; box-in-arena for saturating payloads ≥ 16 bytes
(with the general `#inline` directive as the hot-path valve, §6.7).** The model is
more parsimonious (Option B deletes 5 syntactic forms + 1 former — maximally
KISS), more honest (`error?`→`null | error`), a net size WIN on the three most
common absence shapes (pointer/class/`error?`: 16→8, new-8, 24→16), and — via
box-in-arena — delivers the owner's variant-sizing for `u128 | null` in the only
form physics allows (8-byte handle, zero heap when null). The two factual
corrections the owner must accept: (1) §6.1 — `i32 | null` is `discriminant +
i32`, not one byte; and (2) §6.6 — a mutable `u128 | null` inline slot can be
8 bytes (boxed) or 32 (inline) but never 1 byte, because a native frame reserves
lifetime-max space; "1 byte when null" holds literally only in the VM and, in
spirit, as "zero heap when null" natively. R2 is now RELAXED (owner): bare
`Ref<T>` stays never-null, `Ref<T> | null` is the explicit nullable form, and the
mandatory NARROWING INVARIANT (§3d) keeps the bare-ref promise sound. The sentinel
machine SIMPLIFIES.

### Owner decisions already recorded (2026-07-19)

- **D2 RESOLVED — pointer nullability is honest in the type** (§2.6): bare
  `ptr<T>` = trustworthy never-null; `ptr<T> | null` = explicit nullable. Endorsed
  as a key argument FOR.
- **D3 RESOLVED — R2 relaxed for the union form** (§3d): `Ref<T> | null` is now
  legal; bare `Ref<T>` stays never-null.
- **D4 RESOLVED — `ref` precedence** (§3e): `ref x: T | null` ≡ `Ref<T | null>`
  (a live never-null ref TO a nullable value), DISTINCT from and coexisting with
  `y: Ref<T> | null` (a nullable ref whose handle may be null). `ref` is a binding
  modifier over the whole type; no ambiguity.
- **D5 RESOLVED — the narrowing invariant is UNIVERSAL** (§3f): any `T | null`
  (any union) must be narrowed before use as `T`; under Option B, TWO forms
  accepted (`if x == null`/`!= null` flow-narrowing, exhaustive `match` — `?.` is
  deleted). Owner's key point: this is the EXISTING `T | error` rail — incremental
  cost is a single localized equality-guard flow-narrower (an **S** add),
  everything else folds into existing variant handling. Proven in §3f.
- **D8 RESOLVED — the box dial is A + hatch** (§6.5, owner "vou na recomendação A
  + hatch"): saturating payload ≥ 16 bytes defaults to BOX-IN-ARENA (8-byte
  handle, zero heap when null), with an EXPLICIT hatch to force inline-tag on a
  hot binding.
- **D9 RESOLVED — SUGAR = Option B (NO SUGAR AT ALL)** (§3b, owner 2026-07-19):
  DELETE the type spellings `T?`/`error?`, the operators `?.`/`??`/`?.m()`, and
  the checker `Optional` former. KEEP the `null` value and `NullPattern`.
  Narrowing is `match` + `if x == null`/`!= null` only. This SUPERSEDES the former
  D1/D6/D7 (operators/`T?`-sugar) — all now DELETED, not kept. The
  canonical-order normalization (`null` first, §3c) is independent and stays.
  Consequence: the `error?`→`null | error` + operator rewrite is REQUIRED and is
  the wave's biggest churn (§8 Crumb 6).
- **D10 RESOLVED — KEEP `#inline` as a GENERAL sum-representation directive**
  (§6.7, owner confirmed 2026-07-19). Not null-only: it forces inline-tag over the
  default box-in-arena for ANY eligible `Variant` (saturating payload ≥ 16 B) —
  `T | null`, `T | error`, `A | B | C`. Spelling = the `#`-attribute on the
  binding/field (Option A: reuses the `#test` channel, leaves `type_eq`/mangle
  untouched). Ineligible-application is a compile error (non-sum / already-niche /
  self-recursive / payload < 16 B). Future `#inline`/`#boxed` on large value types
  is RECORDED as a separate feature, out of scope for this wave.

### No open decisions remain

All decisions D2-D10 are owner-ratified. The design is CONVERGED; §8 is the
implementation basis. Adjacent findings encountered during implementation are
REPORTED up, not turned into scope here.

### Law tensions (resolved law-first; none HALT)

- **M.3 (`void` is not a value).** Untouched — `null` is the value, `void` stays
  return-only. The proposal actually *sharpens* the void/null boundary. No
  tension.
- **R2 (references never null) — RELAXED by owner (D3).** Bare `Ref<T>` stays
  never-null; `Ref<T> | null` is the explicit nullable form; the UNIVERSAL
  narrowing invariant (§3f) restores soundness (deref only in a narrowed scope).
  No leak — proven in §3d.
- **Throughput doctrine.** The niche/tag requirement in §6.3 is a HARD gate, not
  a preference: any crumb that would ship the 4-byte-enum-tag intermediate is
  forbidden. Encoded as the size-probe rituals at §8 Crumbs 3, 4, 5. No unresolved
  tension — the sequencing removes it. Box dial CLOSED (D8), sugar CLOSED
  (D9: Option B), `#inline` CLOSED (D10).

RATIFIED — no unresolved tension, no HALT. §8 is the implementation basis.
