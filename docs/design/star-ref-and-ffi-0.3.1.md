# The `ref` keyword + unsafe raw pointers + FFI — EXECUTABLE PLAN (0.3.0.30 build → 0.3.1 swap)

> **Status:** DESIGN-AHEAD, doc-only. **NOT implemented — the owner ratifies this plan before code.**
> **The owner has DECIDED** (this is no longer an option comparison): the safe reference is the
> **`ref` KEYWORD**; **`Ref<T>` becomes compiler-internal and invisible at the surface**; the raw
> pointer sigils **`*` / `&` return to being EXCLUSIVE to `unsafe`** raw pointers. This document is the
> **executable crumb sequence** that builds that decision under the same **accept(.30)→adopt(.31)**
> discipline the null-union work uses: **0.3.0.30 BUILDS the whole new base (surface + backend),
> seed-safe, coexisting with the old surface** (so it rides into the `0.3.0.30` seed); **0.3.1 SWAPS
> the whole corpus** onto the new pattern using the `0.3.0.30` seed, then repurposes `&` and removes
> the old forms.
>
> **The ratified semantic model is `docs/design/ref-transparent-model.md`** (tripartite law, R1–R11,
> the A1–A6 spine, never-null, depth-cap-2, `Ref<T?>` transparency). This plan does NOT re-derive it —
> it **executes** it. Companions read: `docs/design/marshall-spec.md` (the `ref`↔`ptr` boundary,
> C0–C8), `docs/design/memory-unsafe-backend-remodel.md` (`unsafe`-by-type §2), `docs/design/
> null-union-c3-c7-0.3.0.30.md` (the accept→adopt seed dance this mirrors), `docs/design/
> wave-0.3.1-plan.md` (SW3/SW4/SW8 hooks). Verified against `src/` at authoring time.
>
> **The build gate for every product-touching crumb (call it GATE-G):**
> `teko build . --no-verify --release && ./bin/teko test .` **+ the self-host fixpoint**
> (`gen1 == gen2 == gen3`) **+ `diff_vm_native`**. Doc/fixture-only crumbs skip the fixpoint.

---

## 0. The as-built state this plan is anchored on (verified in `src/`)

| fact | today | cite |
|---|---|---|
| **safe reference type** | `Ref<T>` (generic) → internal `Reference{inner}` | `resolve.tks` (`Ref<T>`→`Reference`) |
| **reference deref** | `.value` (`AssignKind::RefDeref`) → C `(*p)` | `ast.tks:117`, `codegen.tks` |
| **safe borrow / pass-by-ref** | **`&x` = a SAFE Borrow** (AL-wave-F1), used across the WHOLE corpus | `ast.tks:60`, `parse_expr.tks:471-477` |
| **the `ref` keyword** | **does not exist** — no lexer token, `BindKind = enum{Let;Mut;Const}`, 0 binder uses | `ast.tks:101` |
| **`*` as an operator** | **NOT an operator today — the glyph is FREE** (only multiply at the binary level) | `parse_expr.tks:517` |
| **raw pointer internals** | `Ptr{inner:Type?}`, `Uptr{}`, `unsafe` modifier all EXIST internally | `type.tks:102`, `resolve.tks:1050` |
| **surface `ptr<T>`/`uptr`** | **partial** — `ptr<T>` resolves; unsafe-gating is pointee-conditional; `uptr` not unsafe-carrying; no deref/arith operators | `resolve.tks:1057`, `:981` |
| **runtime rep** | `Reference` AND `Ptr` both lower to bare `T*` — Marshall is ~free | `codegen.tks` |

Two facts drive the whole sequencing:

1. **The reference already exists** (`Ref<T>` + `.value` + `&`/auto-ref) and self-hosts today. This
   plan changes its **grafia** to the `ref` keyword and moves `Ref<T>` out of sight — a surface build
   over a settled runtime (codegen `Reference → T*` is untouched).
2. **`&` cannot mean two things at once.** Today `&x` is the SAFE borrow used everywhere in `src/`.
   The decision makes `&` an **unsafe raw address-of**. Those are incompatible, so **the `&`
   repurposing MUST wait until the corpus no longer uses `&x` as a borrow — i.e. .31**, after the
   swap. This is the single load-bearing ordering constraint in the plan (§2, tension T1).

---

## 1. The ratified surface (what the corpus will read after .31)

Executes `ref-transparent-model.md`; the owner's specific rules, verbatim:

- **`Ref<T>` is compiler-internal and INVISIBLE at the surface.** The surface never spells `Ref<T>` or
  `.value` again; the checker still uses `Reference{T}` as the internal type.
- **The `ref` keyword is the safe reference**, in four positions:
  - variable binding — `mut ref r: T = …` (a mutable reference binding); **`let` cannot hold a
    reference** — `let ref …` is illegal (tripartite law; a reference is inherently mutable).
  - parameter — `fn a(ref b: T)` (by-reference param, R7 borrow-down).
  - return — `fn a(...) -> ref T` (returns a reference, R6 materialize-in-caller-arena).
  - call-site pass-by-reference — **`a(ref x)`**, which **REPLACES `&x`** for "pass this mutable
    lvalue by reference."
- **Use is transparent** — no `.value`; a `ref`-bound name auto-derefs (type-directed, ref-model §4);
  `r = v` is R4 write-through.
- **`*` and `&` are EXCLUSIVE to `unsafe` raw pointers**: `*p` = deref of a `ptr<T>`, `&x` = raw
  address-of yielding `ptr<T>` — both legal only in `unsafe` context. Raw-pointer support (`ptr<T>`,
  `uptr`, `*`-deref, `&`-address-of, arithmetic) must exist/complete.

```teko
/**
 * Advances an array cursor by one and yields the current element, or `null` at the end.
 * `ref cur` is a by-reference parameter (internal `Reference{ArrayCursor}`); every use of
 * `cur` auto-derefs (no `.value`), and `cur = …` writes THROUGH the alias (R4). This is the
 * post-.31 spelling of the `over_array` idiom (was `cur: Ref<ArrayCursor>` + `.value`).
 *
 * @param []i64 xs           the backing array (borrowed by value; not mutated)
 * @param ref ArrayCursor cur  the cursor, passed by reference (write-through advance)
 * @return i64 | null         the current element, or `null` when the cursor is exhausted
 * @since 0.3.1
 */
fn over_array(xs: []i64, ref cur: ArrayCursor) -> i64 | null {
    if cur.pos >= xs.len { return null }        // auto-deref — no `.value`
    let v = xs[cur.pos]
    cur = ArrayCursor { pos = cur.pos + 1 }      // write-through (R4)
    v
}
```

---

## 2. The two-phase strategy + the critical `&` sequencing nuance

**Phase .30 (0.3.0.30) — BUILD, seed-safe, COEXIST.** Add every new surface (the `ref` keyword in all
four positions, `*`-deref, complete `ptr<T>`/`uptr` unsafe support) and its backend, **while the old
surface (`Ref<T>`, `.value`, safe-`&x`) keeps working unchanged.** `src/` does NOT adopt any new
surface in .30, so **seed `0.3.0.29` still builds gen1** (the corpus is on the old spelling). The .30
merge cuts the `0.3.0.30` seed, which now *understands* the new surface. **`&` is NOT touched in .30 —
it stays the safe borrow.**

**Phase .31 (0.3.1) — SWAP the corpus, then repurpose `&`, then remove the old forms.** Using the
`0.3.0.30` seed (which parses the new surface), migrate `src/`: `Ref<T>`→`ref`, `.value`→transparent,
`&x`(borrow)→`ref x`. Only **after** the corpus no longer uses `&x` as a borrow: repurpose `&`→unsafe
address-of, hide `Ref<T>` from the surface, and remove `.value`/old-`Ref<T>`/safe-`&x` acceptance.

**THE NUANCE (tension T1, load-bearing):** `&` is a safe borrow in the whole corpus today. If .30
repurposed `&`→unsafe-address-of, the seed-build of the *current* corpus (which uses `&x` as a borrow)
would break — the `0.3.0.29` seed and the .30 corpus both still mean "safe borrow." **Therefore `&`'s
meaning is FROZEN through .30 and only flips in .31, AFTER the corpus swaps `&x`→`ref x`.** In .30 the
unsafe address-of that raw-pointer support needs is provided by the **existing FFI primitives**
(`teko::mem::as_ptr`/`buf_ptr`, `region_alloc` → `ptr<T>`) and Marshall `unwrap`, **NOT** by `&`. The
`&`→pointer flip is the very last .31 crumb.

```
timeline of the `&` glyph and the `Ref<T>`/`ref` grafia:

 seed 0.3.0.29 ──build──▶ .30 corpus (OLD surface) ──merge──▶ seed 0.3.0.30
   &x = safe borrow          &x = safe borrow                    PARSES BOTH:
   Ref<T>/.value             Ref<T>/.value  (unchanged)          ref-kw + *deref + ptr<T>
   (no ref kw)               + ref-kw/​*deref/​ptr<T> ACCEPTED       ALSO ACCEPTED, unused in src/
                                                                          │
 seed 0.3.0.30 ──build──▶ .31 corpus swap ─────────────────────────────┘
   swap Ref<T>→ref, .value→transparent, &x→ref x   (still &=borrow until swap done)
   THEN: & → unsafe address-of · hide Ref<T> · remove .value/old-Ref<T>/safe-&x
```

---

# PART A — the `ref` keyword (safe reference)

## 3. Phase .30 — BUILD the `ref` surface + backend (seed-safe, coexisting)

Every crumb below is **additive**: the parser/checker/codegen ACCEPT the new form; `src/` does not use
it, so the `0.3.0.29` seed builds the .30 corpus unchanged. GATE-G at each product-touching crumb.

| crumb | size | touches (product?) | what to build | seed-safety |
|---|---|---|---|---|
| **A30.0** | L (design/verify) | no (design) | the §10.4-style auto-deref soundness pass on type-directed peeling + depth-cap-2 (ref-model open case #4/#5) — the verdict GATES A30.3 before it cements | doc-only |
| **A30.1** | S | **yes** (lexer) | `ref` as a **contextual keyword** (like `from`/`params`/`unsafe`, via `is_name_at`) — so an identifier named `ref` still parses; only the binder/param/return/call positions trigger it | additive token; `src/` uses no `ref` keyword → seed-safe |
| **A30.2** | M | **yes** (parser/AST) | `ref` in the 4 positions: binding `mut ref r: T`, param `fn a(ref b: T)`, return `-> ref T`, call-arg `a(ref x)`; `ref` grafia in type positions (fields/generic-args/slices/nesting); add `by_ref: bool` on `Param`/`Binding`/`FnDecl` + a `RefArg` expr node for `ref x` | additive productions; `src/` unaffected → seed-safe |
| **A30.3** | M | **yes** (checker) | `ref`-binders resolve to internal `Reference{T}`; **`let ref` reject** (tripartite); transparent type-directed auto-deref (no `.value` needed on a `ref`-bound name); never-null; depth-cap-2 on resolved `Reference` depth; A4 definite-assignment on `ref` binders; `a(ref x)` = borrow-down (R7/A3, spine A2/A5). The A1–A6 spine is UNCHANGED (it already keys off `Reference`). | gated by A30.0; coexists with `Ref<T>`/`.value` acceptance → seed-safe |
| **A30.4** | S | **yes** (codegen/lower) | `ref` bindings/params/returns lower to the EXISTING `Reference → T*` path; `a(ref x)` lowers to the EXISTING implicit auto-ref (`&x` at the C level) — **reuse, no new lowering** | reuse of built codegen → seed-safe |
| **A30.5** | S | **yes** (coexistence) | keep `Ref<T>` + `.value` + safe-`&x` FULLY WORKING alongside `ref` (the accept-seed contract); a `ref`-bound name and a `Ref<T>`+`.value` value are two spellings of the same internal `Reference` | both surfaces accepted → the whole point of the .30 seed |
| **A30.6** | S | no (fixtures) | the .30 accept-fixtures (§9) — new surface RUNs, old surface unchanged | doc/fixture-only |

**A30 ritual:** A30.0 is a design gate (verdict before A30.3 cements the peel). A30.1–A30.5 each ride
GATE-G. The .30 **seed cut** (merge) is a full gate + confirms BOTH spellings parse and `src/` still
builds on the `0.3.0.29` seed (it must — `src/` is untouched).

**Null reconciliation carried in A30.3 (the merged C5 `Ref<T> | null` work):** the surface
`ref r: T | null` means **`Reference{ T | Null }`** (non-null spine, nullable pointee), never
`Reference | Null`. The C5 pointer-is-null niche is **retargeted to the pointee** and the C5
flow-narrow memo is **disabled across a reference boundary** (ref-model §4.2: another alias may null
the slot; re-read via `?.`/`??`/`match`). It stays for genuinely-local non-ref `T | null`. This is a
checker-side reclassification in `resolve.tks`/`typer.tks` + the null-union niche classifier — **no
new rep path** (still a bare `T*` at a `T|Null` slot). Build it in .30 additively (it only fires for
the new `ref` surface; the old `Ref<T>|null` niche keeps working until .31 removes the surface).

## 4. Phase .31 — SWAP the corpus onto `ref`, then hide `Ref<T>` + remove old forms

Uses the `0.3.0.30` seed (which parses `ref`). GATE-G at each product-touching crumb; the corpus swap
is the load-bearing self-host proof.

| crumb | size | touches | what to do | ordering |
|---|---|---|---|---|
| **A31.1** | L | **yes** (`src/**`) | migrate `Ref<T>`→`ref` (binding/type grafia) and `.value`→transparent across `src/` (the 114 `Ref<` sites + the Ref-subset of `.value`); fixpoint + `diff_vm_native` at EACH file; honest-stop (keep old form on a stubborn file) until green | first |
| **A31.2** | M | **yes** (`src/**`) | swap every safe `&x` (borrow) → `ref x` at call sites (the AL-F1 sites); `&` STILL means borrow here — this is a pure spelling migration, not the repurposing | after A31.1 |
| **A31.3** | S | **yes** (checker/codegen) | **hide `Ref<T>` from the surface** (reject `Ref<T>`/`.value` in user code — internal `Reference` only); remove the deprecated `.value`-on-`Reference` acceptance; remove the safe-`&x` **borrow** acceptance | **only after** A31.1+A31.2 green (corpus no longer uses any old form) |
| **A31.4** | M | **yes** (parser/checker/codegen) | **REPURPOSE `&` → unsafe raw address-of** (`&x` yields `ptr<T>`, unsafe-only, marshall-spec §5.5) — now that A31.3 removed the safe-`&x` meaning and nothing in the corpus uses it as a borrow | **LAST** — the T1 flip |
| **A31.5** | S | no (fixtures) | .31 swap-fixtures + the negative that `Ref<T>`/`.value`/safe-`&x` are now rejected | doc/fixture-only |

**A31 ritual:** A31.1 is **self-host critical** (the corpus must build on `ref` with `.value` gone —
the SW4 keystone). A31.3/A31.4 are full gates (they remove/repurpose shared surface). A31.4 is the
single most delicate flip — its gate proves no residual `&x`-as-borrow survives.

---

# PART B — unsafe raw pointers + FFI completeness

`*` and `&` are the raw-pointer operators; `ptr<T>`/`uptr` are the raw types (all `unsafe`-only). The
runtime rep is shared with `Reference` (`T*`), so Marshall is ~free (marshall-spec §3).

## 5. Phase .30 — BUILD `*`-deref + complete `ptr<T>`/`uptr` (the glyph `*` is free today)

`&`-address-of is **NOT** built here (T1 — `&` stays safe-borrow through .30); raw address-of in .30
comes from the existing FFI primitives + Marshall `unwrap`. GATE-G at each product-touching crumb.

| crumb | size | touches | what to build | seed-safety |
|---|---|---|---|---|
| **B30.1** | M | **yes** (parser/checker/codegen) | `*p` **prefix deref operator** on a `ptr<T>`, **unsafe-gated** (glyph free today, `parse_expr.tks:517`); parser adds `*` prefix; checker rejects in safe ctx; codegen `(*p)` | additive; `src/` uses no `*p` → seed-safe |
| **B30.2** | M | **yes** (checker/codegen) | **complete `ptr<T>`/`uptr` unsafe** (marshall C0): `Ptr` unconditionally unsafe-carrying, `Uptr` unsafe-carrying; nullable-by-union `ptr<T> \| null` / `uptr \| null` (reject `ptr<T>?`); pointer arithmetic `p+n`/`p-n`/`p[i]`/`p->f` gated unsafe; migrate `teko::mem` FFI prims to `unsafe fn` | additive; only `rawbuf_*` (already unsafe) call them → seed-safe |
| **B30.3** | M | **yes** (`src/marshall/*`) | the `teko::marshall` module: `null`/`is_null`/`to_uptr`/`from_uptr` (unsafe), `swap` (safe), and **`wrap`/`unwrap`** (`ref`↔`ptr<T>`) — buildable now because the internal `Reference` exists; the surface says `ref T`, not `Ref<T>` | new module, additive → seed-safe |
| **B30.4** | S | no (fixtures) | the marshall/pointer accept-fixtures (§9) | doc/fixture-only |

**Reconciliation flagged to the owner (marshall-spec §5.5):** raw-pointer operators bind to `ptr<T>`
(as marshall-spec already has them); there is **no `*T` type** (an earlier draft floated one — it is
dropped: `*` is an operator only, the raw type is `ptr<T>`). `ptr<T>` is the raw unsafe pointer,
derefable via `*p` inside `unsafe`, nullable via union. This is consistent with the original
marshall-spec (no re-point needed). **REPORTED** so the owner knows the `*T`-type idea is dropped.

## 6. Phase .31 — repurpose `&` + FFI completeness (the security wave proper)

The `&`→address-of flip is **A31.4** (Part A, last). The remaining FFI-completeness surfaces are
additive and land after the corpus swap; each rides GATE-G. (Deep rationale/signatures for each were
drafted in the prior FFI audit; here they are the executable .31 cluster.)

| crumb | size | touches | what to build |
|---|---|---|---|
| **B31.0** | (=A31.4) | parser/checker/codegen | `&x` → unsafe raw address-of yielding `ptr<T>` (the T1 flip; sequenced in Part A) |
| **B31.1** | M | parser/checker/codegen/manifest | **`extern macro fn … = "M" from header "h"`** — bind a C macro (no linker symbol); type-check the asserted signature; C-transpile expands textually; own-backend honest-stops OR generates a `cc`-compiled wrapper symbol; `[extern.headers.<os>]` manifest knob |
| **B31.2** | M | parser/checker/codegen | **C-ABI variadics `...`** on `extern fn` (trailing, extern-only); default argument promotions; C-backend emits verbatim; own-backend varargs ABI is B31.7 |
| **B31.3** | M | parser/manifest | **grouped `extern from lib "L" from header "h" { … }`** block — link a whole library/header set, not one symbol at a time; a `teko bindgen` tool is the flagged follow-on |
| **B31.4** | M | parser/checker/codegen | **`#repr("c")`** struct layout + **`extern union`** — C layout compatibility for aggregates crossing by value (couples #502) or pointer; a non-`#repr` aggregate crossing FFI = compile error |
| **B31.5** | M | parse_type/checker/codegen | **`cabi` fn-pointer** callbacks — only NON-capturing closures / top-level fns coerce (env-first ABI dropped); a capturing closure at the coercion site = compile error; user-data is an explicit `ptr<T>`/`uptr` param |
| **B31.6** | S–M | parser/codegen/teko_rt | **`#cconv("stdcall")` + `teko::ffi::errno()`** (calling convention + thread-local errno read); weak/versioning flagged follow-on |
| **B31.7** | L | backend | **own-backend variadic ABI** (SysV/AArch64/Windows) — the honest cost of toolchain independence; sequences with the SW13 backend debts; C-transpile gives B31.2 for free until then |
| **B31.8** | M | manifest/checker/codegen | **reverse-FFI export:** `[artifact] abi = "c"` knob (on `static`/`shared`) + **`#[export("c_symbol")]`** (stable unmangled C symbol, C ABI, FFI-safe signature gate — no capturing closures, no bare `ref` crossing as-is; a `ref` is `unwrap`ped to `ptr<T>` at the boundary; aggregates need `#repr("c")`; no Teko `params []T` variadic export) |
| **B31.9** | M | emit/header/teko_rt | reverse-FFI **C-header emitter** (`emit_c_header` — real `.h` with C prototypes + `#define`s for exported consts; closes the `extern type` header honest-stop `header.tks:91`) + the **`teko_rt_init`/`teko_rt_shutdown`** runtime-init contract (guarded auto-init fallback; arena stays process-root) + panic-must-not-unwind-into-C (an `#[export]` fn that can panic returns `… | error`) |

**Reverse-FFI export marker (implementer copies verbatim):**

```teko
/**
 * Exported to C as the stable symbol `teko_add`. C ABI, no name mangling, no environment.
 * Only FFI-safe scalar / `ptr<T>` / `#repr("c")`-aggregate params and returns are permitted;
 * a bare `ref` is `unwrap`ped to `ptr<T>` at the boundary, a capturing closure is rejected,
 * and a `params []T` Teko variadic may not be exported (B31.8). The host calls `teko_rt_init`
 * once before any exported call (or relies on the guarded auto-init).
 *
 * @param i64 a  first addend
 * @param i64 b  second addend
 * @return i64   the sum
 * @since 0.3.1
 */
#[export("teko_add")]
pub fn add(a: i64, b: i64) -> i64 { a + b }
```

---

## 7. Gate & fixpoint discipline (every product-touching crumb)

**GATE-G = `teko build . --no-verify --release && ./bin/teko test .` + fixpoint (`gen1==gen2==gen3`)
+ `diff_vm_native`.** Notes:

- **.30 crumbs:** because `src/` does NOT adopt the new surface in .30, the `0.3.0.29` seed builds
  gen1, gen1 builds gen2, gen2 builds gen3 — **fixpoint holds trivially** (the corpus is byte-identical
  across the new-surface addition; it simply doesn't use it). A .30 crumb that perturbs `.tkb` output
  for the *unchanged* corpus is a bug (the addition must be inert until adopted).
- **.31 crumbs:** the seed is `0.3.0.30`. Each corpus-swap crumb must hold fixpoint on the NEW
  spelling (`0.3.0.30` seed builds the swapped gen1; gen1 builds gen2; equal). `diff_vm_native` proves
  the swap changed spelling, not behavior. A31.1 and A31.4 are the two crumbs where a break strands
  the chain — migrate file-by-file with honest-stop.
- **Design/fixture-only crumbs** (A30.0, A30.6, A31.5, B30.4) skip the fixpoint.

## 8. Seed-safety notes (everything in .30 must be buildable by seed `0.3.0.29`)

- **The binding rule:** `src/` must use NO new surface in .30. Every .30 crumb is a parser/checker/
  codegen ADDITION that the compiler ACCEPTS but the compiler's own code does not WRITE. The corpus
  stays on `Ref<T>`/`.value`/safe-`&x` through the entire .30 phase; adoption is 100% deferred to .31.
- **`ref` is CONTEXTUAL** (A30.1) — a namespace segment / identifier named `ref` stays legal, and no
  existing token is reserved away. Verify no `src/` identifier collision before merge (contextual
  keyword, so even a collision is inert unless in a binder/param/return/call slot).
- **`*`-deref (B30.1) uses a free glyph** — `*` is not a prefix operator today, so adding the prefix
  deref cannot change the meaning of any existing `src/` expression (multiply stays at the binary
  level). Seed-safe by construction.
- **`&` is UNTOUCHED in .30** (T1) — its safe-borrow meaning is frozen so the `0.3.0.29` seed and the
  .30 corpus agree. The flip is A31.4, after the corpus stops using `&x` as a borrow.
- **`ptr<T>`/`uptr` completion (B30.2)** only tightens *unsafe-carrying* + adds operators used only in
  `unsafe`; the FFI prims migrate to `unsafe fn` but their only callers (`rawbuf_*`) are already
  unsafe — blast radius audited, seed-safe.
- **The .30 merge cuts the `0.3.0.30` seed** carrying the full new surface; **.31 dogfoods it.** Never
  remove an old form in .30 (that is a .31 crumb) — removing before the corpus swaps would strand the
  seed chain (the null-union §0.1 seed-sequencing law, mirrored).

## 9. Regression fixtures (inputs → exit; VM & native)

**.30 accept-fixtures (both surfaces live):**
- `ref_binding_ok` — `mut ref r: T = v; r = w` write-through → RUN, VM==native.
- `ref_param_ok` — `fn f(ref b: T)` mutates caller's `mut` lvalue via `f(ref x)` → RUN, VM==native.
- `ref_return_ok` — `fn g(...) -> ref T` (R6 caller-arena materialize) → RUN, VM==native.
- `ref_use_transparent_ok` — a `ref`-bound name used with NO `.value` → RUN, VM==native.
- `ref_optional_pointee_ok` — `mut ref r: T | null`; `?.`/`??`/`match` peel one level → RUN.
- `old_surface_still_ok` — `Ref<T>` + `.value` + `&x` UNCHANGED (coexistence proof) → RUN, VM==native.
- `let_ref_rejected` — `let ref r: T` → **EXPECT_COMPILE_FAIL** (tripartite).
- `ref_uninit_rejected` — `mut ref r: T` no init → **EXPECT_COMPILE_FAIL** (A4).
- `ref_depth_cap3_rejected` — consecutive-ref depth 3 → **EXPECT_COMPILE_FAIL**.
- `ptr_deref_unsafe_ok` / `ptr_deref_in_safe_rejected` — `*p` in unsafe RUNs / in safe FAILS.
- `ptr_optional_question_rejected` — `ptr<T>?` → **EXPECT_COMPILE_FAIL** (nullable is `ptr<T> | null`).
- `marshall_swap_values` — SAFE `swap` → RUN, **VM==native**; `marshall_wrap_null_panics` — PANIC.

**.31 swap-fixtures (old forms now gone):**
- `ref_selfhost_ok` — the `over_array`/iterator idiom compiles on `ref` → RUN (self-host proof).
- `amp_is_unsafe_addressof_ok` — `&x` in `unsafe` yields `ptr<T>` → RUN (native); `amp_in_safe_rejected` — `&x` in safe → **EXPECT_COMPILE_FAIL** (the flip).
- `refT_surface_rejected` — `Ref<T>` / `.value` in user code → **EXPECT_COMPILE_FAIL** (hidden).
- FFI cluster: `extern_macro_htonl`, `variadic_printf`, `grouped_extern_block`, `repr_c_struct_byval`,
  `cabi_callback_noncapturing` (+ `_capturing_rejected`), `ffi_errno_read`,
  `revffi_export_add`, `revffi_c_header_emitted`, `revffi_rt_init_contract` — RUN;
  `revffi_export_nonffisafe_rejected`, `revffi_export_teko_variadic_rejected` — COMPILE_FAIL.

## 10. Law / Constitution tensions (called out; resolved law-first)

- **T1 [CRITICAL, ordering] — `&` cannot be safe-borrow AND unsafe-address-of at once.** The corpus
  uses `&x` as a borrow everywhere. **Resolved:** freeze `&`'s meaning through .30; flip to
  address-of only in **A31.4**, after A31.1/A31.2 swap `&x`→`ref x` and A31.3 removes the safe-`&x`
  acceptance. Raw address-of in .30 uses the existing FFI prims / Marshall `unwrap`, not `&`. This is
  the single non-negotiable ordering edge. Not a HALT.
- **T2 [HIGH, stranding] — the corpus swap (A31.1) is self-host-critical.** If a `src/` pattern breaks
  under the `ref` checker, no swapped gen1 builds. **Resolved:** file-by-file migration with GATE-G +
  honest-stop (keep the old form on a stubborn file until resolved); remove old acceptance (A31.3)
  only when 100% green. Mirrors null-union R2. Not a HALT.
- **T3 [MEDIUM] — auto-deref soundness (depth-cap-2 peel) unverified.** **Resolved:** A30.0 runs the
  §10.4-style soundness pass BEFORE A30.3 cements the peel; if it fails, narrow the nested-ref opening
  (honest-stop) rather than cement an unsound peel. Not a HALT.
- **T4 [MEDIUM] — the merged C5 `Ref<T> | null` niche re-interpretation.** **Resolved law-first**
  (ref-model §4.2): the pointer-is-null niche retargets to the pointee and the flow-narrow memo is
  disabled across ref boundaries; built additively in A30.3, old niche removed in A31.3. Review-heavy,
  not a HALT.
- **T5 [MEDIUM] — C-macro FFI vs toolchain-independence** (own-backend has no C preprocessor, B31.1).
  **Resolved honest-stop:** `extern macro` works on the C-transpile backend + a generated
  `cc`-compiled wrapper symbol; honest compile error on the *pure* own-backend. Not a HALT.
- **T6 [MEDIUM] — C-ABI variadics on the own-backend** (B31.2/B31.7). **Resolved:** C-transpile ships
  now; own-backend varargs ABI is a real backend crumb (B31.7, with SW13). Not a HALT.
- **T7 [LOW] — reverse-FFI runtime init vs no-implicit-global** (B31.9). **Resolved:** explicit
  `teko_rt_init/shutdown` + guarded auto-init; arena stays process-root (remodel §0). Not a HALT.
- **T8 [LOW] — panics must not unwind into a C host** (B31.9). **Resolved:** the export boundary
  catches Teko panics; a panicking `#[export]` fn returns `… | error`. Not a HALT.

**No genuine unresolved tension → NO HALT.** REPORTED for owner awareness: the `*T`-type idea is
dropped (§B.5 — `*` is an operator, the raw type is `ptr<T>`); and the marshall-spec `swap`/`wrap`/
`unwrap` signatures now spell `ref T` (not `Ref<T>`) at the surface — a companion-doc spelling edit.

---

## Appendix — file/impact map (absolute paths)

- **.30 build — Part A (`ref`):** `/home/user/teko-lang/src/lexer/lexer.tks` (contextual `ref`),
  `/home/user/teko-lang/src/parser/ast.tks` (`by_ref` flag, `RefArg`),
  `/home/user/teko-lang/src/parser/parse_decl.tks` (`ref` param/return),
  `/home/user/teko-lang/src/parser/parse_stmt.tks` (`mut ref` binder),
  `/home/user/teko-lang/src/parser/parse_type.tks` (`ref` grafia in type positions),
  `/home/user/teko-lang/src/parser/parse_expr.tks` (`a(ref x)` call arg),
  `/home/user/teko-lang/src/checker/resolve.tks` (`ref`→`Reference`, `let ref` reject, depth cap,
  reject surface `Reference|Null`), `/home/user/teko-lang/src/checker/typer.tks` (auto-deref peel,
  C5 flow-narrow restriction), `/home/user/teko-lang/src/codegen/codegen.tks` (reuse `Reference→T*`).
- **.30 build — Part B (pointers):** `/home/user/teko-lang/src/parser/parse_expr.tks` (`*p` prefix
  deref), `/home/user/teko-lang/src/checker/resolve.tks` (`unsafe_carrying_at` `Ptr`/`Uptr`; ptr
  arith gates), `/home/user/teko-lang/src/codegen/codegen.tks` (`(*p)`, arith), new
  `/home/user/teko-lang/src/marshall/marshall.tks` (wrap/unwrap/swap/bridge).
- **.31 swap:** `/home/user/teko-lang/src/**` (114 `Ref<`, `.value` Ref-subset, `&x` borrow sites);
  `/home/user/teko-lang/src/parser/parse_expr.tks` + `checker` + `codegen` for the `&`→address-of flip;
  `resolve.tks` to hide `Ref<T>` from the surface.
- **.31 FFI completeness:** `/home/user/teko-lang/src/parser/parse_decl.tks` (`extern macro`, `...`,
  grouped `extern`), `/home/user/teko-lang/src/build/manifest.tks` (`[extern.headers]`, `abi="c"`),
  `/home/user/teko-lang/src/emit/header.tks` (C-header emitter, close `header.tks:91`),
  `/home/user/teko-lang/src/runtime/teko_rt.{c,h}` (maintained-C: errno read, `teko_rt_init/shutdown`),
  `/home/user/teko-lang/src/backend/*` (own-backend varargs ABI).
- **Companion designs:** `/home/user/teko-lang/docs/design/ref-transparent-model.md` (the ratified
  semantic model — EXECUTED here), `/home/user/teko-lang/docs/design/marshall-spec.md` (C0–C8;
  signatures now spell `ref T`), `/home/user/teko-lang/docs/design/memory-unsafe-backend-remodel.md`,
  `/home/user/teko-lang/docs/design/null-union-c3-c7-0.3.0.30.md` (the accept→adopt seed dance),
  `/home/user/teko-lang/docs/design/wave-0.3.1-plan.md` (SW3/SW4/SW8 hooks).

*This document is design-ahead and doc-only. No product code is changed. The owner ratifies this plan
before implementation.*
