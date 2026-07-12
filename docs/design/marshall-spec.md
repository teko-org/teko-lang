# Marshall — the maximal safe↔unsafe pointer boundary (design 0.3.1)

> **Status:** DESIGN-AHEAD, doc-only. **NOT implemented.** Companion + downstream of
> `docs/design/ref-transparent-model.md` (the ratified `Ref<T>`/`ref` model, §5 "Marshall & a
> fronteira `Ref`/`Ptr`") and `docs/design/memory-unsafe-backend-remodel.md` (unsafe-by-TYPE, §2).
> Issue-mãe: **#498**. Owner rulings 2026-07-11..13 PIN the parts marked **[PINNED]** below — treated
> here as law. The one genuinely open axis (spelling) is resolved law-first with a recommendation;
> everything else is drafted against the *declared* shapes of the (still-unbuilt) transparent `Ref`
> redesign and the (partly-built) U2 unsafe-containment gate, so the implementer resumes in minutes
> when those dependencies close.
>
> **Blocked-on (called out per-crumb in §11):** the transparent `Ref<T>` redesign (auto-deref §4 of
> the ref model — unbuilt) gates only the two boundary-crossing ops (`wrap`/`unwrap`). Everything
> else — the ptr/uptr unsafe-gating, the raw-pointer operators, `swap`, and every fixture — is
> UNBLOCKED and can land against today's checker.

This document specifies **Marshall**: the single explicit, `unsafe`-gated boundary between the
**safe reference world** (`Ref<T>`, transparent, spine-tracked) and the **raw pointer world**
(`ptr<T>`/`uptr`, unsafe-by-type, dev-owned). It is the maximal surface for operating on pointers
across that boundary, per the owner's "the maximum possible to operate with pointers between
safe↔unsafe" mandate.

---

## 1. Intent

The surface `ref T` (internally `Reference{T}`) is the safe, transparent reference; it never appears as a raw address and it is protected
by the arena/region model (R11) and the spine (A1–A6). `ptr<T>` is the raw C address — nullable,
untracked, and (per **[PINNED]** ruling 4) **`unsafe`-only**. Between them there must be **exactly
one, explicit, greppable crossing** — never an implicit coercion. That crossing is Marshall.

Marshall exists to satisfy three laws at once:

- **M.3 (honesty):** the safe/unsafe boundary is a real, visible thing, not a hidden trapdoor.
  Crossing it is spelled out; the raw pointer is never conjured behind the reader's back.
- **M.1 (fail early):** the one invariant a raw pointer can violate on the way *into* the safe world
  — `Ref<T>` is never null (R2) — is checked at the crossing and **panics** on violation, rather
  than producing a poisoned `Ref` that segfaults later.
- **M.2 (explicitness):** the *direction* of every crossing is visible at the use site (distinct
  named operations, never one overloaded verb), so the reader never reconstructs intent from
  context.

What Marshall is **NOT**: it is not the `valor ↔ Ref` desugar (that is implicit and safe, §4.1 of
the ref model), and it is not a value cast (`to`/`as`). It is a *boundary crossing between two
memory-safety regimes*, and it is modelled as such.

---

## 2. The boundary map — what is, and is not, Marshall

Three boundaries exist in the memory model. Only ONE is Marshall.

| boundary | mechanism | safety | who owns it |
|---|---|---|---|
| **valor ↔ ref** | implicit **bidirectional desugar** (§4.1 ref model): encapsulate on init/attach (R5, copy into arena), descásca on use (type-directed auto-deref §4) | SAFE, invisible | the desugar; the spine |
| **ptr ↔ ref** | **MARSHALL** — explicit, named, `unsafe`-gated (`wrap`/`unwrap`, this doc) | UNSAFE crossing; null-checked in | the dev, at the crossing |
| **value ↔ value** exchange | `teko::marshall::swap<T>` **[PINNED]** — write-through both refs; never touches a ptr | **SAFE** | the spine (R4 write-through) |

**[PINNED] ruling 2 — `swap` is a VALUE swap, and it is SAFE.** `swap` lives in the `teko::marshall`
namespace for discoverability (it is the sanctioned "exchange" primitive, adjacent to rebind
questions), but it is **not Marshall proper**: it never crosses into `ptr`. It swaps the *pointed-at
values* of two `Ref<T>` (write-through both ways, R4), so a target-rebind is impossible outside
`Ptr`/`unsafe` (R4: write-through never rebinds). See §5.6.

**[PINNED] ruling 3 — Marshall covers ONLY `ptr ↔ Ref`.** The `valor ↔ Ref` boundary is the implicit
desugar (§4.1), NOT Marshall. The `Ref → let` copy (R8) is the *descásca* direction of that desugar,
not a Marshall.

The **event-horizon framing** (the single mental model): `Ref<T>` is *inside* the spine's
jurisdiction; `ptr<T>` is *outside* it. `unwrap` steps OUT (the spine can no longer help you);
`wrap` steps back IN (the compiler re-asserts the one invariant it can check — non-null — and trusts
you for the rest). Everything in §5 is a corollary of that one picture.

---

## 3. The two worlds, in the checker's vocabulary

For the implementer, the meta-types (from `src/checker/type.tks`):

- **`Reference { inner: Type }`** — surface `Ref<T>`. Never null (R2). C-repr = bare `T *`. The safe
  side. Auto-deref is type-directed (§4 ref model). **Depends on the transparent redesign** (today it
  is `.value`-based; the redesign makes it transparent).
- **`Ptr { inner: Type? }`** — surface `ptr<T>`. `inner = null` is the opaque `ptr` (≡ `ptr<void>` ≡
  `*void`). Nullable at runtime (it is a raw address; 0 is a legal value). C-repr = `T *` (or
  `void *` for opaque). The unsafe side.
- **`Uptr { }`** — surface `uptr`. Opaque word-size unsigned; the raw address carried as an integer
  (D35 precedent: `Arena.region: uptr` holds a raw `tk_region *`). The ptr↔integer bridge lives here.

**Representation identity that makes Marshall nearly free:** `Reference{T}` and `Ptr{T}` lower to the
*same* C type (`T *`). `wrap`/`unwrap` therefore emit **no bit-level conversion** — the entire cost
of Marshall is (a) a compile-time type check and (b) a single runtime null-compare on `wrap`. This is
the load-bearing fact behind every "at what cost" answer in §5.

---

## 4. The unsafe gate — `ptr<T>` and `uptr` are unsafe-only [PINNED ruling 4]

Today `ptr<T>` is unsafe-*carrying* only when its pointee is unsafe (`src/checker/resolve.tks:971`,
`unsafe_carrying_at`'s `Ptr` arm recurses into `p.inner`), and `uptr` is not unsafe-carrying at all
(the `_ => false` arm). Ruling 4 makes **the raw address itself** the unsafe thing, regardless of
pointee. The gate change (see crumb C0, §11):

- `Ptr` becomes **unconditionally** unsafe-carrying (a `ptr<int>` in a safe signature/field/local is
  the same error as naming any `unsafe type`).
- `Uptr` becomes unsafe-carrying (a bare `uptr` in safe code is rejected).

Consequence: the raw-pointer FFI primitives already shipped in `teko::mem`
(`as_ptr`/`as_cstr`/`str_from_cstr`/`bytes_from_ptr`/`buf_ptr`) must migrate to **`unsafe fn`**
(their signatures name `ptr`). Their only current stdlib callers are the already-`unsafe`
`rawbuf_alloc`/`rawbuf_read` (`src/mem/unsafe/rawbuf.tks`), so the blast radius is small — but this
is a real migration, tracked as crumb C0. This is exactly the containment M.3 wants: *the raw
pointer is honestly unsafe everywhere it appears.*

---

## 5. The full API surface

Every op below lives in the stdlib namespace **`teko::marshall`** (spelling ratified in §6). Each is
given as a copy-paste-ready, full-Javadoc Teko signature the implementer adds verbatim. All ops that
name `ptr`/`uptr` are `unsafe fn` by the §4 gate; `swap` is the sole SAFE member.

**The OPERAND LAW [PINNED, owner 2026-07-13]: only `ref` or `ptr` enter Marshall.** Every operand of
every `teko::marshall` op must already be reference/pointer-typed (`ref T`, `ptr<T>`, `uptr`). Bare
values — including `mut` lvalues — are compile errors at a marshall call site; **no implicit
borrow-down (R7/A3) occurs at the Marshall door** (the addressing must pre-exist and be visible —
the case-C principle). Full statement + fixtures in §5.6.

### 5.0 Module skeleton (compiles today; the honest-stops mark the blocked bodies)

```teko
/**
 * `teko::marshall` — the explicit, `unsafe`-gated boundary between the safe reference world
 * (`Ref<T>`) and the raw pointer world (`ptr<T>`/`uptr`). Per `docs/design/marshall-spec.md`
 * (issue #498), this namespace is the ONLY sanctioned `ptr <-> Ref` crossing: `wrap` (raw in,
 * null-checked, may panic) and `unwrap` (Ref out, infallible, `unsafe`), plus the ptr/uptr bridge,
 * the raw-pointer helpers, and the SAFE value-`swap`. It builds NO new allocator and adds NO
 * grammar: every op is a stdlib generic fn over the `Ptr`/`Reference`/`Uptr` type-cases the checker
 * already knows, mirroring the `teko::mem::unsafe::{RawBuf, Owned}` pattern.
 *
 * @see docs/design/marshall-spec.md
 * @see docs/design/ref-transparent-model.md §5
 * @since 0.3.1
 */
```

### 5.1 `wrap` — raw pointer INTO a safe reference (`ptr<T> -> ref T`) [checked, may panic]

The sanctioned crossing OUT of the raw world: takes a raw address the dev asserts is live and hands
back a spine-tracked `ref T` (underlying type `Reference{T}`). **Safety class:** `unsafe fn` (names `ptr<T>`); the *result* is a
safe `ref T` that legally crosses back to safe code (§2 boundary rule: a safe type may leave an
`unsafe fn`).

**Checks on entry (the owner's "which are feasible and at what cost"):**

| check | class | cost | verdict |
|---|---|---|---|
| **type compatibility** (`ptr<T>` → `Ref<T>` same `T`; opaque `ptr` needs an explicit target `T`) | **STATIC** (compile) | zero | **DONE at compile time.** A `ptr<int>` wrapped as `Ref<str>` is a *compile error* (`marshall_wrap_type_mismatch_rejected`), never a runtime panic. An opaque `ptr` (inner=null) can only be wrapped by naming `T` at the call: `teko::marshall::wrap<int>(p)`. |
| **non-null** (R2: `Ref<T>` is never null) | **RUNTIME → panic** | one compare + branch | **REQUIRED (M.1).** A null raw pointer wrapped into a `Ref` would silently violate the never-null invariant and segfault on first deref. `wrap` panics on null instead. |
| **region membership** (is the pointee in a live arena?) | — | O(regions) tree walk, and *still unsound* (cannot see a freed-but-mapped address — the aliased-UAF the spine cannot track either) | **NOT checked in release.** This is the residual risk `unsafe` owns (memory-unsafe doc §2c: "unsafe contains the residual"). Best-effort tree-containment check under `TEKO_MEM_PARANOID` only (crumb C8), never a release-path guard. Documented as an *honest non-check*, not a silent one. |

```teko
/**
 * Wraps a raw `ptr<T>` the caller asserts points at a LIVE `T` into a safe, spine-tracked
 * `ref T` — the sanctioned crossing OUT of the raw pointer world. The pointee type `T` is
 * checked against the pointer's element type at COMPILE time (a mismatch is a compile error,
 * not a panic); an opaque `ptr` requires `T` be named explicitly at the call. The ONE runtime
 * guard is the R2 non-null invariant: a null `p` PANICS (M.1), because a null `Ref<T>` cannot
 * exist. Region/liveness is NOT verified — that residual is the `unsafe` the caller owns; the
 * returned `Ref<T>` re-enters spine jurisdiction (A1-A6) from the wrap point on.
 *
 * @param ptr<T> p  a raw, non-null address the caller asserts points at a live `T`
 * @return ref T  a safe reference aliasing `*p`
 * @throws panic  if `p` is null (the R2 never-null invariant would be violated)
 * @example ref node: Node = teko::marshall::wrap<Node>(raw_node_ptr)  // result is ref Node
 * @since 0.3.1
 */
pub unsafe fn wrap<T>(p: ptr<T>) -> ref T {
    // BLOCKED on the transparent ref redesign (§4 ref model). Body: null-panic guard, then
    // reinterpret p as the bare `T *` a Reference{T} already is (zero-cost, §3).
}
```

### 5.2 `unwrap` — safe reference OUT to a raw pointer (`ref T -> ptr<T>`) [unsafe-only]

The crossing INTO the raw world: extracts the raw address a `ref T` holds. **Safety class:**
`unsafe fn`; the *result* is a `ptr<T>`, an unsafe type, so it can be **named only inside unsafe
context** — safe code cannot hold the result (`marshall_unwrap_in_safe_rejected`).

**Checks:** **NONE, and none possible.** At the instant of extraction a `ref T` is always non-null
and correctly typed, so the `ptr<T>` is trivially valid *now*. The danger is entirely downstream
(the raw ptr outliving the Ref's arena) — that is the spine's event horizon, owned by `unsafe`.
Honesty demands we state: `unwrap` is *infallible at the crossing and dangerous forever after*.

```teko
/**
 * Extracts the raw `ptr<T>` a `ref T` holds — the crossing INTO the raw pointer world. The
 * extraction is infallible: a `Ref<T>` is always non-null and correctly typed, so the yielded
 * `ptr<T>` is valid at the instant of the call. It is `unsafe fn` and yields an `unsafe` type,
 * so safe code cannot hold the result. From this point the spine (A1-A6) no longer tracks the
 * address: keeping `p` past the arena that owns `r`'s pointee is a use-after-free the caller,
 * not the compiler, must prevent.
 *
 * @param ref T r  a safe reference
 * @return ptr<T>  the raw, non-null address `r` aliases
 * @example let raw: ptr<Node> = teko::marshall::unwrap(node_ref)  // result is ptr<Node>
 * @since 0.3.1
 */
pub unsafe fn unwrap<T>(r: Ref<T>) -> ptr<T> {
    // BLOCKED on the transparent ref redesign. Body: reinterpret the Reference{T}'s bare
    // `T *` as ptr<T> (zero-cost, §3). No guard.
}
```

### 5.3 Null handling — `ptr<T>` is C-nullable; `ptr<T>?` is FORBIDDEN

**Owner asked: is `Ptr` nullable? `Ptr<T>` vs `Ptr<T>?`.** Recommendation, law-first:

- A raw `ptr<T>` **is nullable by construction** — it is a raw address, and 0 is a legal bit-pattern.
  This is honest C semantics (M.3): a raw pointer that pretended it could never be null would be a
  lie. `wrap` is the gate that turns "maybe-null raw address" into "never-null `ref T`".
- **`ptr<T>?` (Teko's `?`-Optional layered over a raw ptr) is a category error and is REJECTED**
  (`marshall_ptr_optional_rejected`). `?`/`Optional` is a *safe-world* construct with a defined
  sentinel discipline; stacking it on a raw address pretends a safety the raw pointer does not have
  (M.3), and it is redundant — `ptr<T>` already encodes "possibly null". Inside unsafe code the dev
  tests null with the explicit helpers below, not with `?`/`match`.

```teko
/**
 * The null raw pointer of element type `T` (the address 0). The only sanctioned way to spell a
 * null `ptr<T>` — never a bare `0` cast. `unsafe fn` because it yields a raw pointer.
 *
 * @return ptr<T>  a null `ptr<T>` (address 0)
 * @since 0.3.1
 */
pub unsafe fn null<T>() -> ptr<T> { /* codegen: (T *)0 */ }

/**
 * Tests whether a raw `ptr<T>` is null (address 0) — the explicit null check that replaces the
 * forbidden `ptr<T>?`/`match`. `unsafe fn` because its parameter names a raw pointer.
 *
 * @param ptr<T> p  the raw pointer to test
 * @return bool  true iff `p` is the null pointer
 * @since 0.3.1
 */
pub unsafe fn is_null<T>(p: ptr<T>) -> bool { /* codegen: p == (T *)0 */ }
```

### 5.4 ptr ↔ integer bridge — `to_uptr` / `from_uptr` (D35 precedent)

The `uptr` word (already the carrier for `Arena.region`) is the ptr↔integer bridge. There is **no
`ptr<T> <-> uptr` cast in the checker today** (`src/mem/unsafe/rawbuf.tks:16` records its absence);
Marshall's maximalist scope adds it, as explicit `unsafe fn`s — **not** as a `to`/`as` cast. Reasons
(feed the §6 matrix): a ptr↔int reinterpret is exactly the "Rust cast shadow" M.3 forbids for `as`,
and it is not a value conversion (`to`). Modelling it as named marshall fns keeps it honest and
greppable. `uptr`-to-`uN`/`iN` is ordinary integer transport (uptr is an unsigned word); the bridge
fns only cross the *pointer/word* line.

```teko
/**
 * Reinterprets a raw `ptr<T>` as its address, as an opaque word-size `uptr` — the ptr->integer
 * half of the bridge (D35: the shape `Arena` already uses to carry a `tk_region *`). It is a bit
 * reinterpret, NOT a `to`/`as` value conversion; hence a named `unsafe fn`, never a cast.
 *
 * @param ptr<T> p  the raw pointer whose address to take
 * @return uptr  `p`'s address as an opaque word
 * @since 0.3.1
 */
pub unsafe fn to_uptr<T>(p: ptr<T>) -> uptr { /* codegen: (uintptr_t)p */ }

/**
 * Reinterprets an opaque `uptr` address as a raw `ptr<T>` — the integer->ptr half. The element
 * type `T` is named at the call (there is nothing in a bare word to infer it from). It is a bit
 * reinterpret, NOT a `to`/`as` value conversion.
 *
 * @param uptr u  an address as an opaque word
 * @return ptr<T>  the address reinterpreted as a `ptr<T>`
 * @example let p: ptr<Node> = teko::marshall::from_uptr<Node>(word)
 * @since 0.3.1
 */
pub unsafe fn from_uptr<T>(u: uptr) -> ptr<T> { /* codegen: (T *)u */ }
```

### 5.5 Raw-pointer OPERATORS and the sigils `*` / `&` / `->`

M.0 is generous with operators (they map to metal — pointer arithmetic *is* a machine capability),
rigorous with keywords. So raw-pointer work is expressed with **operators**, all **unsafe-gated**
(legal only inside an `unsafe fn` / `unsafe type` body — the same gate that lets a body *name*
`ptr<T>`). The approved sigils from the #498 thread:

| form | meaning | notes |
|---|---|---|
| `*p` | **deref** — read/write the pointee `*p = v` / `let x = *p` | unsafe; `p: ptr<T>` → `T` lvalue |
| `&x` | **address-of** — raw `ptr<T>` of an lvalue `x` | unsafe; the raw escape hatch (e.g. `&buf` for a C API). `&` in SAFE code stays banned (the closures rule) |
| `p->field` | **arrow** — deref-and-member, sugar for `(*p).field` | unsafe; C ergonomics for struct pointers |
| `p + n` / `p - n` | **pointer arithmetic** — advance/retreat by `n` elements (`n * sizeof(T)` bytes) | unsafe; M.0 operators-to-metal |
| `p - q` | **pointer difference** → element count (`i64`) | unsafe; both operands same `ptr<T>` |
| `p[n]` | **indexed deref** — sugar for `*(p + n)` | unsafe |

`&` (address-of) and `unwrap` are complementary, not redundant: `&x` takes the address of a raw
lvalue *local*; `unwrap` extracts the address a *safe `Ref<T>`* holds. Both are `unsafe`; document
both. All six are compile errors in safe context (`marshall_ptr_arith_in_safe_rejected`).

**Parser note:** `*`/`&`/`-` already lex as operators; the work is *checker-side gating* (reject in
non-unsafe context) + *codegen* (they already map to the C forms). `->` is the one possibly-new
surface token; if the lexer lacks a distinct arrow, it desugars to `(*p).field` in the parser
(crumb C2). No new keyword — M.0-clean.

### 5.6 `swap` — the SAFE value exchange [PINNED ruling 2]

Not Marshall proper (never touches `ptr`); the sole SAFE member of the namespace. Swaps the
*pointed-at values* of two `ref T` via R4 write-through both ways — a target-rebind is structurally
impossible (R4). Runs on the VM **and** native (it is safe), so its fixture is a *differential*
oracle, unlike every unsafe fixture (native-only). Costs three `T`-copies (temp + two writes);
acceptable, and honest about the copy (M.5 "you see the copy").

```teko
/**
 * Swaps the VALUES two references point at — write-through both (R4), never a target-rebind
 * (rebinding a reference is impossible outside `Ptr`/`unsafe`). The SAFE, sanctioned exchange
 * primitive: it stays entirely inside the safe reference world (it names no `ptr`), so it is a
 * plain `pub fn`, runs on the VM and native alike, and is subject to no unsafe gate. After the
 * call `a` holds `b`'s former value and vice-versa; the two references still alias the same two
 * storage slots they did before.
 *
 * @param T a  the first reference (write-through target — must ALREADY be a reference)
 * @param T b  the second reference (write-through target — must ALREADY be a reference)
 * @example ref rx: i64 = x
 * @example ref ry: i64 = y
 * @example teko::marshall::swap(rx, ry)   // operands are ref i64 — pre-existing, visible addressing
 * @since 0.3.1
 */
pub fn swap<T>(ref a: T, ref b: T) {
    let t: T = a        // descásca-copy (R8): snapshot a's value
    a = b               // write-through a (R4): a's slot := b's value
    b = t               // write-through b (R4): b's slot := old a
}
```

**[PINNED] ruling (owner 2026-07-13) — the Marshall OPERAND LAW: only `ref` or `ptr` enter
`teko::marshall`.** Every op in this namespace accepts ONLY reference/pointer-typed operands
(`ref T`, `ptr<T>`, `uptr`). A bare VALUE — including a `mut` lvalue — is a compile error at a
marshall call site: **no implicit borrow-down (R7/A3) happens at the Marshall door.** The addressing
must pre-exist and be visible (the case-C principle: `ref` marks addressing); Marshall never
manufactures a reference from a value silently. Ordinary functions with `ref` params keep R7
borrow-down as usual — the restriction is specific to the boundary namespace. So: passing a
`let`-bound value is rejected (`marshall_swap_on_let_rejected`), and passing a `mut`-bound VALUE is
equally rejected (`marshall_swap_on_mut_value_rejected`) — the caller borrows explicitly first
(`ref rx: i64 = x`). This body compiles against **today's** `.value` Ref too (it is plain assignment
+ one local), so `swap` is UNBLOCKED (crumb C3).

### 5.7 Arrays of pointers and references [RATIFIED, owner 2026-07-13]

**The safe side — `[]ref T` (slice of references, a VALUE whose elements alias):**

- **Element access** is transparent (type-directed peel): `xs[i]` in a value context reads THROUGH
  the element's reference; **element assignment is WRITE-THROUGH, never rebind** (R4 extended to
  elements — re-pointing an element means rebuilding the slice).
- **Attaching a VALUE** to a ref collection (literal element, `push`) = **R5 copy-on-attach** — the
  value is materialized in the arena and the element aliases the COPY (the Marshall operand law does
  NOT apply here; `[]ref T` is ordinary safe-world code).
- **Nullability (Case D applied):** `[]ref T?` ✓ (elements alias optional VALUES) · `([]ref T)?` ✓
  (the `?` sits on the slice/value layer) · a `?` on any ref layer ✗ at any depth.
- **A slice-of-refs FIELD carries no modifier** (`d: []ref T`): the field is a VALUE whose type
  shows the refs — the `ref` modifier marks "this declaration IS a reference", which a slice is not.
- **Escape (A1):** a ref collection is a ref-carrying aggregate — local use ✓; crossing arenas
  (return/store) ✗ conservative until the spine proves cases (this was literally the
  "collection-return" hole of the adversarial soundness pass).
- The three shapes stay distinct: `ref xs: []T` (alias of the WHOLE array) ≠ `[]ref T` (array OF
  aliases) ≠ `ref xs: []ref T` (alias of an array of aliases — two depth-1 chains, inside the cap).

**The raw side — `ptr` IS the array (C MODE, owner):** `[]ptr<T>` exists (an unsafe type by Ptr
contagion, §4) as a Teko slice whose elements are raw pointers — elements are raw-REBINDABLE
(`ps[0] = ps[1]` is legal; the raw world has no R4). But the raw world's native array idiom is the
C one: **a base `ptr<T>` + indexing/arithmetic (`p[i]`, `p + n`, §5.5) — the pointer IS the array.**
Consequently **`ptr<[]T>` is REJECTED**: a Teko slice is a fat value (`ptr+len`), not a C type; a
raw pointer to one would be a category mixup — the raw world carries base+len separately (the
RawBuf pattern is exactly that answer). `ptr<ptr<T>>` stays legal (C `T**` interop; unsafe depth is
unbounded, per the cap ruling).

**Crossing in bulk:** `[]ptr<T>` and `[]ref T` share an **identical C-representation** (both arrays
of `T *`), so a bulk crossing is a null-scan one way and a no-op reinterpret the other. v1 ships
only the **element-wise** primitives (§5.1/§5.2 applied per element); a bulk
`wrap_slice`/`unwrap_slice` convenience is deferred and built ON them (it must null-panic each
element on the way in — no cheaper than the loop). Documented so the implementer does not invent a
"cheap" unchecked bulk crossing that would smuggle a null past R2.

```teko
/**
 * Wraps every raw pointer in a `[]ptr<T>` into a `[]ref T`, null-checking each element (R2).
 * DEFERRED to a follow-up: it is pure convenience over per-element `wrap` (§5.1) and no cheaper
 * (each element still null-panics). Listed here so the contract is fixed: it is `unsafe fn`
 * (names `[]ptr<T>`) and PANICS on the first null element.
 *
 * @param []ptr<T> ps  a slice of raw pointers, each asserted live and non-null
 * @return []ref T  a safe slice of references, one per input element
 * @throws panic  on the first null element
 * @since 0.3.1
 */
pub unsafe fn wrap_slice<T>(ps: []ptr<T>) -> []ref T { /* deferred; loop of wrap */ }
```

---

## 6. Spelling decision matrix + recommendation [resolves ref-model §10.1]

The one genuinely open axis. Three candidate spellings:

- **(A) keyword** — `marshall(x)`, a contextual keyword, direction inferred from the arg's type.
- **(B) namespace fns** — `teko::marshall::wrap` / `::unwrap` / …, distinct names, stdlib generics.
- **(C) cast form** — `p as ref<T>` / `r as ptr<T>`, reusing the `as` operator.

| criterion (law) | (A) keyword | (B) `teko::marshall::*` fns | (C) `as` cast |
|---|---|---|---|
| **greppability** (M.3) | ok (`marshall`) | **best** (`marshall::`, per-op names) | poor (`as` is everywhere) |
| **direction visible / no overload** (M.2, M.5) | **fails** — one `marshall` for both directions IS overload; direction hidden in arg type | **best** — `wrap`≠`unwrap`, one name one fn | ok (target type shows direction) |
| **no cast-lie** (M.3) | ok | ok | **fails** — a ptr↔Ref reinterpret spelled `as` is exactly "the Rust cast shadow" M.3 forbids |
| **keyword sparsity** (M.0, M.5) | **fails** — adds a keyword that unlocks no metal | **best** — zero keyword | ok (reuses `as`) |
| **parser/checker cost** (M.4) | highest (contextual kw + type-directed dispatch) | **zero grammar** (stdlib fns + generics exist) | moderate (`as ptr<T>`/`as ref<T>` target forms) |
| **precedent consistency** | weak | **strong** — mirrors the *shipped* `teko::mem` FFI primitives (`as_ptr`/`bytes_from_ptr`) AND the OWNER-PINNED `teko::marshall::swap` already in this namespace | weak — `to`=checked value conv, `as`=lossy-forbidden; Marshall is neither |
| **unsafe-gating ergonomics** | same for all (naming `ptr` gates) | naturally an `unsafe fn` per op — honest | same |

**RECOMMENDATION: (B) `teko::marshall::wrap` / `::unwrap` / `::to_uptr` / `::from_uptr` / `::null`
/ `::is_null` / `::swap`.** It is the *only* candidate that passes every law: M.5 (distinct names,
no overload), M.0 (no keyword), M.3 (no cast-lie, maximally greppable), M.4 (zero grammar → ships
first, no parser dependency). It is precedent-locked from two sides: the owner has *already* placed
`swap` at `teko::marshall::swap`, and the existing FFI primitives already live as `teko::mem::*`
functions. The §4.1 sketch's `marshall(x)` keyword was illustrative shorthand; ratify the fn form.

(The raw-pointer *operators* of §5.5 are orthogonal to this choice — they are M.0 operators, not the
crossing verb, and stay operators under any option.)

---

## 7. Interaction with unsafe-by-type, sigils, arenas/regions (R11), and the spine (A1–A6)

- **Unsafe-by-type (§4):** Marshall is the *only* sanctioned producer/consumer of `ptr`/`uptr`
  across the boundary. `wrap`'s output (safe `ref T`) is the one thing that may leave an `unsafe
  fn` per the memory-unsafe §2 boundary rule (alongside a by-value safe copy and `Owned<T>`).
  `unwrap`'s output (`ptr<T>`) may not — it stays inside unsafe context by the containment gate.
- **Sigils (§5.5):** the raw operators are gated by the *same* unsafe context check that lets a body
  name `ptr<T>` — no separate mechanism. One gate, uniformly enforced.
- **Arenas/regions & R11:** `unwrap` is the spine's **event horizon**. A `ptr<T>` obtained from a
  `ref T` into an arena is valid only while that arena lives (R11); the spine stops tracking at the
  `unwrap`, so keeping the ptr past the arena is a UAF the dev owns. This composes exactly with the
  `unsafe #must_free` Arena (memory-unsafe §2c): `region_alloc` already yields a `ptr<T>` into a
  manual region; Marshall is the disciplined way to *lift* such a ptr into a temporary `ref T` for
  a safe helper and drop back — never storing the `ref` past the region (that store is rejected by
  A1/A2 on the safe side, and the raw ptr's lifetime is `unsafe`-owned on the other).
- **The spine (A1–A6):** `wrap` re-enters spine jurisdiction — the produced `ref T` is subject to
  A1 (transitive escape), A2 (borrow-down non-escaping), A4 (definite-assignment), A5 (path), A6
  (free needs ownership). The spine **cannot** verify the wrapped pointee is live (it came from
  outside) — the null check is the only compiler guard; lifetime correctness is the dev's asserted
  contract. This is the honest seam (M.3): **`wrap` is where dev-asserted lifetime becomes
  compiler-tracked lifetime, and the compiler cannot verify the handoff — only non-null.** The doc
  must not pretend `wrap` launders an unsafe pointer into a safe one; it launders the *type*, not
  the *lifetime*.
- **`swap` (§5.6)** touches none of this — it is pure safe write-through, fully inside A1–A6.

---

## 8. FFI patterns cookbook

**The FFI WRAPPER LAW [PINNED, owner 2026-07-13]: a wrapper receives `ref` or `ptr` — data never
crosses BY VALUE inside a wrapper.** The caller owns the data and either LENDS it (`ref`,
write-through) or hands the RAW ADDRESS (`ptr`). Copies and conversions are EXPLICIT at the call
site, never hidden inside the wrapper body (M.5: you see the copy). Scalar RESULTS may still cross
out by value (a safe value legally leaves an `unsafe fn`); the law is about payloads — strings,
buffers, aggregates. Corollary of the §5 operand law, extended from the marshall ops to the whole
boundary zone.

**8.1 C string APIs — a THIN raw shim; conversions live at the caller:**
```teko
/**
 * The thin shim over the C `char*` API: pointers in, pointer out, NOTHING copied inside.
 * @param ptr<byte> c_name  a NUL-terminated C string the caller prepared
 * @return ptr<byte>  the C API's own result pointer (ownership per the C API's contract)
 */
pub unsafe fn greet_raw(c_name: ptr<byte>) -> ptr<byte> {
    c_api_greet(c_name)
}

// the CALLER makes every copy visibly, at the call site:
unsafe fn caller(name: str) -> str {
    let c_in: ptr<byte> = teko::mem::as_cstr(name)     // VISIBLE copy #1 (NUL-terminated)
    let c_out: ptr<byte> = greet_raw(c_in)
    teko::mem::str_from_cstr(c_out)                    // VISIBLE copy #2 (back to safe str)
}
```

**8.2 Buffer fill — the wrapper BORROWS the caller's buffer (write-through, zero copies):**
```teko
/**
 * Hands the CALLER's buffer to a C fill routine — write-through via the borrowed `ref`; the
 * wrapper allocates nothing and copies nothing. The caller sizes and owns the buffer.
 * @param []byte buf  the caller's buffer (borrowed; filled in place)
 */
pub unsafe fn fill(ref buf: []byte) {
    c_api_fill(teko::mem::buf_ptr(buf), buf.len)       // raw address of the BORROWED storage
}
```

**8.3 Struct pointer lift via Marshall** (the model pattern — it RECEIVES the pointer):
```teko
/**
 * Wraps a raw `ptr<Node>` from a C API into a `ref Node`, runs a SAFE helper over it, and
 * returns a by-value scalar summary (legal: scalar out). Shows `wrap` as the ptr->Ref lift.
 * @param ptr<Node> raw  a raw, non-null node pointer from C
 * @return i64  a value computed by the safe helper
 */
pub unsafe fn summarize(raw: ptr<Node>) -> i64 {
    ref n: Node = teko::marshall::wrap<Node>(raw)      // null-panics if raw is null; result is ref Node
    node_weight(n)                                     // a plain SAFE fn taking ref Node
}
```

**8.4 Handles are TYPED POINTERS, not words.** Holding a raw address as a `uptr` word in a field
loses the type and the reader's pointer-visibility — per the wrapper law, an unsafe struct holds
the `ptr<T>` itself:
```teko
unsafe type CApiSession = struct {
    h: ptr<CApiCtx>       // typed, visible, greppable — never `h: uptr`
}
```
The `to_uptr`/`from_uptr` bridge (§5.4) remains for genuine WORD-transport seams (an ABI field that
is an integer, a callback context squeezed through a C `void*`-as-word API) — transport, not
storage. **Follow-up flagged:** the shipped `Arena.region: uptr` (D35) predates this law; re-tag to
a typed opaque ptr when the 0.3.1 marshall crumbs land (tracked with C1).

---

## 9. Regression fixtures (named like the `mem_free`/`unsafe_*` family)

Each fixture is its own standalone project under `examples/regressions/<name>/` (a fixture cannot
`use` the compiler's own stdlib, so it carries a **local copy** of the shape under test — the same
convention as `arena_manual_ok`/`unsafe_rawbuf_roundtrip`). REJECT fixtures carry an
`EXPECT_COMPILE_FAIL` marker file; ACCEPT/panic fixtures are exit-code oracles.

**ACCEPT — round-trip / exit-code oracles:**

| fixture | oracle | exercises |
|---|---|---|
| `marshall_swap_values` | **VM + native** (swap is SAFE) | §5.6 write-through both ways; exit = f(swapped values) |
| `marshall_wrap_unwrap_roundtrip` | native-only | `unwrap` then `wrap` an identity; exit = value read back through the round-tripped Ref |
| `marshall_uptr_roundtrip` | native-only | §5.4 `to_uptr`→`from_uptr` identity; exit = deref of the rebuilt ptr |
| `marshall_ptr_arith_index` | native-only | §5.5 `p + n`, `p[n]`, `*p`, `&x` inside `unsafe fn`; exit = summed elements |
| `marshall_ffi_cstr_roundtrip` | native-only | §8.1 `as_cstr`→`str_from_cstr` through a local C stub |
| `marshall_slice_wrap` | native-only | §5.7 element-wise `[]ptr<T>`↔`[]ref T` |

**REJECT — `EXPECT_COMPILE_FAIL`:**

| fixture | rejects |
|---|---|
| `marshall_ptr_in_safe_fn_rejected` | naming `ptr<int>` in a NON-`unsafe` fn signature (§4 gate) |
| `marshall_ptr_field_in_safe_struct_rejected` | a `ptr<T>` field in a non-`unsafe` struct (contagion) |
| `marshall_uptr_in_safe_rejected` | a bare `uptr` in safe code (§4 gate) |
| `marshall_unwrap_in_safe_rejected` | holding `unwrap`'s `ptr<T>` result in safe context (§5.2) |
| `marshall_ptr_arith_in_safe_rejected` | `*p` / `&x` / `p + 1` in a safe fn (§5.5) |
| `marshall_ptr_optional_rejected` | `ptr<T>?` — the forbidden `?`-over-ptr (§5.3) |
| `marshall_wrap_type_mismatch_rejected` | `wrap<str>` of a `ptr<int>` — the STATIC type check (§5.1) |
| `marshall_swap_on_let_rejected` | `swap` on a `let`-bound arg (needs `ref` write-through, §5.6) |
| `marshall_swap_on_mut_value_rejected` | `swap` on a `mut` VALUE lvalue — the operand law: no implicit borrow-down at the Marshall door (§5.6) |

**PANIC — native, non-zero exit:**

| fixture | panic |
|---|---|
| `marshall_wrap_null_panics` | `wrap` of a null `ptr<T>` PANICS (R2/M.1, §5.1); expect the Teko panic/abort exit (same convention as the ÷0 / conversion-panic fixtures), and unchanged under `TEKO_MEM_PARANOID=1` |

---

## 10. Open questions for the owner (each with a recommendation — never a bare question)

1. **`uptr` in safe code.** §4 makes `uptr` unsafe-only (it carries raw addresses). If a program
   wants a genuine word-size *integer* with no pointer meaning, that is `u64`/a `usize`-like type,
   not `uptr`. **Recommendation:** keep `uptr` unsafe-only (it exists to transport addresses); do NOT
   overload it as a general word integer. If a safe word-size int is later wanted, name it
   separately. *(Adopt unless the owner already treats `uptr` as a public safe integer.)*

2. **`->` arrow as surface token vs parser desugar.** §5.5. **Recommendation:** desugar `p->field`
   to `(*p).field` in the parser (no new lexer token, M.0-clean) unless a distinct arrow token
   already exists. Ship the operators `*`/`&`/`+`/`-`/`[]` first; `->` is pure ergonomics on top.

3. **`unwrap` from a *borrowed-down* `Ref` (A2 interaction).** Should `unwrap` of a non-escaping
   borrow-down `Ref` (A2) be allowed? It produces a raw ptr that *by definition* could escape A2's
   downward-only rule. **Recommendation:** ALLOW it (unsafe already owns escape past this point;
   forbidding it would cripple the maximalist FFI use of borrowed params) but document loudly that
   `unwrap` *exits* A2's guarantee — the crossing is the whole point. *(This is the honest-stop:
   permit, but never pretend the ptr is still A2-bound.)*

4. **`wrap` region-membership under `TEKO_MEM_PARANOID`.** §5.1 defers a best-effort tree-containment
   check to paranoid mode (crumb C8). **Recommendation:** implement it as a *warning-free* debug
   abort only if the address is provably outside *every* live region (a conservative check that never
   false-positives on a valid address); if it cannot be made false-positive-free cheaply, ship
   *nothing* in paranoid mode rather than a noisy check. Do not gate the release path on it ever.

5. **Bulk `wrap_slice`/`unwrap_slice` timing.** §5.7 defers them. **Recommendation:** ship only the
   element-wise primitives in 0.3.1; add the bulk helpers when a real FFI corpus needs them, built on
   the primitives (they are convenience, not new capability — M.5).

6. **`Owned<T>` vs `wrap`/`unwrap` for resource transfer.** The memory-unsafe doc names `Owned<T>` as
   the move-only handle crossing out of unsafe. Where a raw *resource* (not just an address) must
   cross, is that `Owned<ptr<T>>` or a `wrap`ped `Ref`? **Recommendation:** keep them distinct —
   `wrap`/`unwrap` cross the *pointer/reference type* line; `Owned<T>` crosses the *ownership/move*
   line. A resource handed to safe code as owned uses `Owned<T>`; a borrowed address lifted for a
   safe helper uses `wrap`. Do not merge them.

---

## 11. Crumb sequence for implementation (0.3.1)

Smallest safe steps, each independently gate-able. **UNBLOCKED** crumbs land against today's checker;
**BLOCKED** crumbs wait on the transparent `Ref<T>` redesign (§4 ref model, #498 core).

- **C0 — unsafe-gate `Ptr`/`Uptr` [UNBLOCKED].** `src/checker/resolve.tks` `unsafe_carrying_at`:
  `Ptr as p => true` (unconditional), add `Uptr => true`. Migrate the `teko::mem` FFI primitives
  (`as_ptr`/`as_cstr`/`str_from_cstr`/`bytes_from_ptr`/`buf_ptr`) to `unsafe fn`; audit call sites
  (only `rawbuf_*`, already unsafe). Fixtures: `marshall_ptr_in_safe_fn_rejected`,
  `marshall_ptr_field_in_safe_struct_rejected`, `marshall_uptr_in_safe_rejected`,
  `marshall_ptr_optional_rejected`. **Ritual: full gate** (this touches the containment gate — the
  keystone of §4).

- **C1 — the `teko::marshall` module + ptr/uptr bridge [UNBLOCKED].** New `src/marshall/marshall.tks`
  (module skeleton §5.0) with `null`/`is_null`/`to_uptr`/`from_uptr` (all `unsafe fn`, zero grammar,
  codegen is the trivial reinterprets of §5.4/§5.3). Fixture: `marshall_uptr_roundtrip`.

- **C2 — raw-pointer operators [UNBLOCKED].** Checker: gate `*`/`&`/`->`/`+`/`-`/`[]` on `ptr<T>` to
  unsafe context. Parser: `->` desugar (open q 2). Codegen: they already map to the C forms. Fixtures:
  `marshall_ptr_arith_index`, `marshall_ptr_arith_in_safe_rejected`.

- **C3 — `swap` [UNBLOCKED].** Add `teko::marshall::swap<T>` (§5.6; compiles against today's Ref).
  Fixtures: `marshall_swap_values` (VM+native differential), `marshall_swap_on_let_rejected`.
  **Ritual: full gate** (first SAFE Marshall member; the safe surface is now complete).

- **C4 — `wrap` [BLOCKED on transparent ref].** Add `teko::marshall::wrap<T>` (§5.1): static
  type-check + runtime null-panic + zero-cost reinterpret. Fixtures: `marshall_wrap_unwrap_roundtrip`
  (with C5), `marshall_wrap_null_panics`, `marshall_wrap_type_mismatch_rejected`.

- **C5 — `unwrap` [BLOCKED on transparent ref].** Add `teko::marshall::unwrap<T>` (§5.2). Fixtures:
  `marshall_unwrap_in_safe_rejected`, and complete `marshall_wrap_unwrap_roundtrip`.
  **Ritual: full gate** (the ptr↔Ref boundary is now complete — the core of the issue).

- **C6 — FFI cookbook fixtures [UNBLOCKED except 8.3].** `marshall_ffi_cstr_roundtrip` (8.1),
  buffer-fill (8.2) — both land after C0. §8.3 (`summarize`) lands with C4.

- **C7 — slice marshalling [BLOCKED with C4/C5].** Element-wise `wrap_slice`/`unwrap_slice` (§5.7).
  Fixture: `marshall_slice_wrap`.

- **C8 — paranoid region-membership best-effort [UNBLOCKED, optional].** §5.1 / open q 4; a
  `TEKO_MEM_PARANOID`-only conservative containment check. Never on the release path.

**Sequencing note (stable seed):** C0–C3 + C6 + C8 use only features already in the current seed
(unsafe modifier, generics, operators), so they land immediately. C4/C5/C7 wait on the transparent
`ref` redesign; their fixtures and signatures (above) are authored now so they resume in minutes
when that dependency closes.

---

*Companion: `docs/design/ref-transparent-model.md` (§5, §10.1). Base: `docs/design/memory-unsafe-backend-remodel.md` (§2, §2c). Discussion: #498.*
