---
seq: 0011
crumb-id: SM-G5
milestone: M1
gate: "[dry]"
reseed-class: "(folds R1)"
deps: []
sources:
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:259-357"  # §6 Marshall opaque ptr
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1177-1178"# §10 Phase G — G5
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1106"     # §9.1 byte-preserving until adopted
---

# 0011 · SM-G5 — Marshall opaque `ptr`/`uptr` + `__wrap`/`__unwrap` + tag runtime (inert)

> Marshall opaque `ptr`/`uptr` + `__wrap`/`__unwrap` + tag runtime (inert).

## Goal

Replace the generic `ptr<T>` family with a single OPAQUE non-generic `ptr`/`uptr`, plus two methods:
`[u]ptr.__wrap<T>(): T | error | null` (FALLIBLE checked cast IN — safe via arena-liveness + type-tag,
NO `unsafe`) and `[u]ptr.__unwrap<T>(): T` (INFALLIBLE raw pointer exposure OUT). The opaque pointer is
STRUCTURALLY INCAPABLE of arithmetic (no element width), so this decision ALREADY kills `p+n`/`p[n]`.
The tag lives in the ARENA ALLOCATION HEADER (not the pointer word), so `ptr`/`uptr` stay a bare
machine word (C-repr `void*`/`uintptr_t`) — no fat pointer, no ABI change. The tag-runtime path is INERT
(byte-identical) until a `__wrap` exists to read it. Byte-preserving until adopted; `src/`'s FFI
migration to opaque `ptr` is SM-S4 (`0091`, M3). Its seed folds into SM-R1.

## Where

- `src/checker/type.tks` — remove `Ptr { inner: Type? }` + `Uptr {}` generic handling; replace with two
  ATOMIC types `Ptr` (opaque) and `Uptr`. `type.tks:104` `Uptr` word; `:177` `Uptr` same-kind-only equality.
- `src/checker/resolve.tks:971` — delete the `unsafe_carrying_at` `Ptr`-recurses-into-inner arm (an
  opaque `ptr` carries no pointee to recurse into).
- `src/checker/scope.tks` — `__wrap<T>`/`__unwrap<T>` resolved as METHODS on `ptr`/`uptr` (receiver form
  `[u]ptr.__wrap<T>()`); `T` a REQUIRED explicit type argument. `__wrap` result `T | error | null`;
  `__unwrap` result `T`.
- `src/lir/lower.tks` — `__wrap<T>` lowers to null-test → `tk_region_lookup(addr)` → header-tag compare →
  branch value/null/error; `__unwrap<T>` lowers to a bare reinterpret (zero instructions beyond the type
  change).
- `src/runtime/teko_rt.c` — ADDITIVE twin `tk_region_alloc_tagged(r, n, tag)` (within the runtime
  exception; byte-identical when the tag path is unused). `tk_region_alloc` (`teko_rt.c:2034`) is the
  existing route; the tag is a `u64` header slot.
- `src/checker/di.tks:373` — `di_type_id` (FNV-1a of the canonical type name) — REUSE this exact
  derivation for the `type_tag` (ONE hash function project-wide).

## How

1. **Make `ptr`/`uptr` atomic + opaque.** Remove the generic `Ptr{inner}` / `Uptr{}` handling in
   `type.tks`; two atomic types remain. Delete the `resolve.tks:971` recurse-into-inner arm. The
   monomorphization surface shrinks (no `Ptr<T>` instantiations). Confirm the checker rejects `p+n`/`p[n]`
   on an opaque `ptr` with a clear "opaque pointer has no arithmetic" diagnostic (arithmetic is
   structurally impossible — no element width).
2. **Add the tag to the arena header.** A routable object allocation records a `u64 type_tag`
   (`di_type_id` of the canonical name, `di.tks:373` — reuse verbatim) in a per-object header slot via
   the additive runtime twin `tk_region_alloc_tagged(r, n, tag)`. `ptr`/`uptr` stay bare words; the tag
   is fetched from the header. Inert (byte-identical) until a `__wrap` reads it.
3. **Add the methods.** `__wrap<T>`/`__unwrap<T>` are methods on `ptr`/`uptr`, `T` explicit:

```teko
/**
 * ptr_wrap — the FALLIBLE checked re-entry of an opaque address into the typed world (`p.__wrap<T>()`).
 * Three dynamic checks, in order, all UB-free (no `unsafe`): (1) address 0 → `null`; (2) the address
 * falls inside a LIVE region of the region tree (`tk_region_lookup`) — a dropped-region address → error
 * ("pointer's arena is no longer live"); (3) the header's `type_tag == di_type_id(T)` → the `T` value,
 * else error ("pointer tags type X, wrapped as T"). A foreign (non-teko-arena) address is not found →
 * error — the honest refusal to vouch for memory teko does not own.
 *
 * @param p  the opaque pointer to re-enter
 * @return   the `T` value, `null` (address 0), or `error` (dead arena / tag mismatch / foreign)
 * @throws   on a dead-arena or tag-mismatch address (surfaced as `error`, never UB)
 * @since 0.3.1
 */
fn ptr_wrap<T>(p: ptr): T | error | null

/**
 * ptr_unwrap — the INFALLIBLE raw exposure OUT (`p.__unwrap<T>()`): a pure reinterpret of the address as
 * a `T`-typed value, NO check (Ref and Ptr are the same C type — zero instructions beyond the type
 * change). Makes no safety claim; its downstream C use is governed by the `extern` contract, and its
 * arena residence is tracked by the ordinary escape story (A1), not an `unsafe` trapdoor.
 *
 * @param p  the opaque pointer to expose
 * @return   the address reinterpreted as `T`
 * @since 0.3.1
 */
fn ptr_unwrap<T>(p: ptr): T
```

4. **Lower them.** `__wrap<T>` → null-test → `tk_region_lookup(addr)` → header-tag compare → branch to
   value/null/error. `__unwrap<T>` → bare reinterpret.
5. **Service seam (§6.1a):** user code CANNOT `__wrap`/`__unwrap` a service (the service-escape rule,
   SM-G6, forbids a service reaching a Marshall operand — the block is a CONSEQUENCE of the DI escape
   rule, not a second mechanism). The trusted backend is exempt but arena-bounded. No user-reachable
   `service↔ptr` surface — nothing to add here beyond relying on SM-G6.
6. **Confirm byte-neutrality.** Removing generic `Ptr<T>` changes bytes only for source that USES generic
   pointers; the tag path is inert until a `__wrap` reads it. `src/` FFI still uses the old raw path
   until SM-S4. `[dry]` build byte-identical.

## Rulings & laws

- **Teko-only:** checker/lir `.tks`; the ONE C touch is the additive runtime twin
  `tk_region_alloc_tagged` (within the maintained-C runtime exception — `teko_rt.c` is allowed for
  memory correctness; byte-identical when the tag path is unused).
- **W15 full Javadoc** on `ptr_wrap`/`ptr_unwrap` and helpers; no `//`.
- **ONE hash project-wide:** the `type_tag` reuses `di_type_id` (`di.tks:373`) — do not mint a second
  hash.
- **Safe-by-arena, not by assertion (§6.1):** `__wrap`'s three checks are dynamic and surface
  `null`/`error` (never UB, never an uncatchable panic) — so `__wrap` needs no `unsafe` context.
- **Byte-preserving until adopted (§9.1):** the tag path is inert; this crumb does NOT drive the reseed
  by itself — it rides the source sweep (SM-S4).
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step.

## Fixtures

The self-build does not yet call `__wrap`/`__unwrap` (FFI migrates in SM-S4), so the tag/wrap branches
are NOT self-exercised — isolated oracles required for each dynamic outcome:

| fixture | asserts | expected |
|---|---|---|
| `marshall_wrap_tag_ok` | `p.__wrap<T>()` on a live tagged arena ptr returns the value | 0 |
| `marshall_wrap_tag_mismatch` | `p.__wrap<Other>()` returns `error` (dynamic tag check) | 0 (error branch) |
| `marshall_wrap_dead_arena` | `__wrap` of a dropped-region address returns `error` | 0 (error branch) |
| `marshall_wrap_null` | `__wrap` of address 0 returns `null` | 0 (null branch) |
| `marshall_unwrap_infallible` | `p.__unwrap<T>()` exposes the value, no check | 0 |
| `opaque_ptr_no_arithmetic` | `p + 1` / `p[0]` on an opaque `ptr` rejected ("no arithmetic") | EXPECT_COMPILE_FAIL |

## Gate

`[dry]` — compile + the six fixtures + fixpoint (byte-identical; tag path inert). "Green" = `ptr`/`uptr`
are atomic + opaque, `__wrap`/`__unwrap` type and lower, the four dynamic wrap outcomes are correct,
arithmetic is rejected, `[dry]` build byte-identical. Reseed-class: `(folds R1)`.

## Deps

`—` (the service-escape interaction relies on SM-G6 but is not a build dependency — the escape rule
blocks a service reaching a Marshall operand independently).

## Done when

`ptr`/`uptr` are opaque atomic types (no generic family, no arithmetic), `__wrap<T>` performs the
null/arena-liveness/tag checks surfaced as `T | error | null`, `__unwrap<T>` is a bare reinterpret, the
tag rides the arena header (inert until read), the six fixtures pass, and a `[dry]` build is byte-identical.
