# Context-directed literal typing — design (part A: the capability)

Status: design (read-only; no product code in this doc).
Owner ruling (2026-07-20): *"every literal with a `lit to T` cast must be corrected; the
compiler must know the type a literal is applied to, with no unnecessary cast."*
Scope of THIS doc: **(A) the capability** — a literal adopts the type of its APPLICATION SITE
so `lit to T` is never *required*. (A) is additive and lands in seed 0.3.0.28. **(B) the cleanup**
(remove the ~1441 redundant `NNN to T` casts) depends on a seed that already has (A) and is a
NEXT-BUMP (0.3.0.29) item — sketched only, at the end.

---

## 1. Grounding in the Laws

- **M.3 (honesty).** A cast asserts a conversion. When a literal already IS the site's type,
  `lit to T` asserts a conversion that never happens — the code *lies* that a widen/narrow occurs.
  The owner ruling names this: the redundant cast must go, and it can only go once the literal can
  take its type from context. (A) supplies the honesty precondition.
- **M.1 (fail early).** Preserved verbatim. An out-of-range constant literal in an annotated slot
  still errors (`annotated_literal_ok` / `value_fits`), and an out-of-range *constant cast* already
  errors at `const_range_check` (typer.tks:1286). There is therefore **no "constant truncation cast"
  to preserve**: `256 to u8` is *already a compile error today*, not a sanctioned truncation
  (verified empirically). Truncation semantics live only on RUNTIME values, which are not literals,
  so (A) never touches them.
- **M.2 (no guessing).** The **no-context default is preserved** — a literal with no application
  site stays `i64` / `f64` (typer.tks:13). (A) adds propagation, not inference.
- **M.5 (one source of truth).** The audit's central finding is a **duplicated, divergent adoption
  predicate**. (A) is largely the act of collapsing that duplication to one path.

---

## 2. Audit map — what exists, and where it fails

### 2.1 How a numeric literal is typed today
- `type_number` (typer.tks:13) gives every integer literal the DEFAULT `i64` and every float `f64`.
  The value rides an `i128` carrier (`TNumber.value`, parsed by `lit_int`, parse_lit.tks:8), so the
  *magnitude* is never lost at parse time — only the *type* is a placeholder.
- The width the literal really wants is decided later, at the application site, by one of two
  mechanisms:
  - `literal_adopts(e, to)` (typer.tks:3646) — the **TExpr-side** predicate. Uses `cast_kind`
    (typer.tks:1263, which maps `Byte -> U8`) + `value_fits` (typer.tks:1235) / `float_fits`
    (typer.tks:1254). **Recurses through `TIfExpr` / `TMatchExpr` arms and `TArrayLit` elements.**
  - `retype_literal(e, to)` (typer.tks:3718) — the retyping twin that REBUILDS the node at `to`.
    **Recurses only into `TArrayLit` elements**; every other shape (including `TIfExpr` /
    `TMatchExpr`) is retyped *outer-node-only*.
  - `annotated_literal_ok(value, ann)` (typer.tks:1310) — a SECOND, **parser-Expr-side** predicate
    used only by the two annotated-declaration sites. It accepts only `parser::Number`,
    `parser::Unary(Minus, Number)`, and `parser::ArrayLit` value shapes, and within those only a
    `Prim` annotation.

### 2.2 The propagation choke point
`type_value_expected(e, expected, …)` (typer.tks:3183) is the intended "type a value against a
known target" entry. Today it threads `expected` for exactly three shapes: `StructLit`, `Lambda`,
and an `ArrayLit` whose expected element is an interface/trait. **Every other shape falls through to
`type_expr` with NO expected type** (typer.tks:3191), so the literal is born `i64`/`f64` and can only
be fixed *after the fact* by the caller's `literal_adopts`/`retype_literal`.

### 2.3 Where adoption already works (application sites that DO thread or adopt-after)
Confirmed by build probes against the 0.3.0.27 seed:
- call arg → param  (adopt-after: typer.tks:1196–1197; lambda/iface thread at 1131–1135)
- struct-literal field (threads at 1983; adopts at 2011)
- `teko::list::push` element / `with_cap` arg (typer.tks:411, 426)
- binary / compare operand cross-adoption (typer.tks:279–280, 340–344)
- `if` / `match` *branch-to-branch* cross-adoption (typer.tks:3097–3102, 3149–3151)
- return value (typer.tks:3746, 3790)
- simple / ref-deref / field assignment (typer.tks:3430+3435, 3364, 3385)

All of these route through `literal_adopts`, which is why `takesbyte(0x41)`, `S { b = 255 }`,
`fn f() -> byte { 0x41 }`, `x == 0` (x: u32), and `takes128(9223372036854775808)` all compile
**with no cast today**.

### 2.4 THE GAP — the sites that force a `to T` (enumerated, file:line)
Two declaration sites use the weaker `annotated_literal_ok` twin instead of `literal_adopts`:

- **`type_binding`** — typer.tks:3281 (`match annotated_literal_ok(b.value, at)`).
- **`type_const_decl`** — typer.tks:4712 (identical pattern).

Because `annotated_literal_ok` (typer.tks:1310) is narrower than `literal_adopts`, these two sites
reject inputs that every other site accepts. Confirmed failing on the seed, each currently forcing a
cast:

| # | source that FAILS today (compiles everywhere else) | root cause | law |
|---|---|---|---|
| G1 | `let x: byte = 0x41` / `mut r: byte = 0` / `const B: byte = 5` | `annotated_literal_ok` matches only `Prim`; the `Byte` type case falls to `_ => "value type does not match annotation"` (typer.tks:1334, 1354, 1376) | forces `0x41 to byte` — the 1937 `to byte` casts |
| G2 | `let x: u32 = if c { 1 } else { 0 }` / `= match k { … }` | `annotated_literal_ok` matches only `Number`/`Unary`/`ArrayLit`; `IfExpr`/`MatchExpr` fall to `_ => error` (typer.tks:1378) — even though `literal_adopts` handles them | forces a `to T` on each arm |
| G3 | `let xs: []byte = [0x1, 0x2]` / `[]byte = [ if c {1} else {2} ]` | array recursion in `annotated_literal_ok` inherits G1 (element `byte`) and G2 (element `if`/`match`) | forces `[…] to []byte` or per-element casts |

Note the asymmetry that makes these *pure gaps* and not design choices: `takesbyte(0x41)` (arg),
`S { b = 0x41 }` (field), and `fn f() -> byte { 0x41 }` (return) **all compile**, because they use
`literal_adopts`; only the *annotated binding/const of the same literal* fails.

### 2.5 A latent MISCOMPILE surfaced by the audit (report-up + fixed by A)
`retype_literal` recursing outer-node-only (typer.tks:3718) is not just incomplete for the annotated
path — it silently miscompiles the *assign* path, which already reaches it. Probe:

```
mut d = 0 to i128
d = if true { 9223372036854775808 } else { 0 }   // 2^63
_ = (d == 9223372036854775808)                    // observed FALSE
```

The assign adopts (`literal_adopts` says the `if` fits `i128`) then `retype_literal` sets only the
outer `TIfExpr.type = i128`, leaving the branch leaf `9223372036854775808` typed `i64` — a value that
does not fit `i64`. The branch is emitted at the wrong width and the equality is false (measured exit
code 9 where 7 was expected). This is a **currently-miscompiled program**, so fixing it does not
break fixpoint (see §4). (A)'s propagation fixes it by construction: the branch is born `i128`.

### 2.6 Edge cases catalogued
- **No context.** `let x = 9223372036854775808` (no annotation) → `i64` default, unchanged (M.2).
  (Aside, adjacent finding — an *unannotated* over-`i64` binding is NOT range-checked today; it binds
  `i64` with an out-of-range value. Out of scope for (A); reported up, not actioned.)
- **Negative literals.** `-N` is `parser::Unary(Minus, Number)`; `annotated_literal_ok` handles it
  today (typer.tks:1338), including the "negative may not annotate an unsigned type" rejection
  (typer.tks:1349). (A) must carry this shape into the unified path unchanged.
- **Cast target == expected.** `let x: u32 = 5 to u32` — the cast is redundant but still valid
  (verified); (A) only makes it UNNECESSARY, never illegal. It stays a no-op when target == expected.
- **Truncating cast.** `256 to u8` is already an M.1 error (§1); (A) leaves it an error.
- **Overflow / range.** `let x: u8 = 256` still fails early (verified); preserved.

---

## 3. Design of (A) — one propagation rule, one adoption predicate

### 3.1 The rule
> **A literal is typed BY its application site.** `type_value_expected(e, expected, …)` is the single
> expected-directed typer: it BORNS a literal (and every literal reachable through a value-carrying
> composite — `if`, `match`, array) at `expected` whenever the literal fits `expected`, falling back
> to `type_expr` (the no-context default) otherwise. The post-site `literal_adopts`/`retype_literal`
> become a satisfied safety-net rather than the mechanism of record.

Concretely, `type_value_expected` gains arms so that it, not the caller, threads `expected` down:

- **`Number` / `Unary(Minus, Number)`** — if `expected` is a numeric `Prim` OR `Byte` (via
  `cast_kind`) that the value fits (`value_fits` / `float_fits`), produce the leaf typed `expected`.
  Reuse `annotated_literal_ok`'s existing float-vs-int and negative-vs-unsigned *rejection* messages
  (M.1 fail-early is unchanged). Otherwise fall through to `type_expr` (default).
- **`IfExpr` / `MatchExpr`** — type each branch/arm *value* via `type_value_expected(_, expected, …)`
  recursively, so branch leaves are born `expected`; join as today. (This is the miscompile fix.)
- **`ArrayLit`** — generalize the existing interface-only path (typer.tks:3219) to ALL element
  types: type each element via `type_value_expected(_, elem, …)`.
- **`StructLit` / `Lambda`** — unchanged.
- **else** — `type_expr` (unchanged default).

### 3.2 The minimal change to complete it at every site
- **Retire the divergent twin.** `type_binding` (typer.tks:3281) and `type_const_decl`
  (typer.tks:4712) already type their value through `type_value_expected` (typer.tks:3253, 4709);
  once §3.1 borns the literal at `at`, the follow-up `assignable_to` is already true and the
  `annotated_literal_ok` branch is dead. Replace that branch's *predicate* with the shared
  `literal_adopts` + `retype_literal` (the exact pair `type_assign` already uses at typer.tks:3430,
  3435). `annotated_literal_ok` is then reused ONLY as the source of the M.1 range/kind error
  messages inside the `Number` arm of §3.1 — one source of truth (M.5), no behavioural twin.
- **Thread expected at the call-arg site** for `if`/`match`/array args too: extend the
  `type_value_expected` fast-path guard at typer.tks:1131 (today `is_lam && pt_is_func`, or
  `iface_arr_target`) to also fire when the arg is an `IfExpr`/`MatchExpr`/`ArrayLit` and the param
  is a fitting numeric/`byte`/slice type. This closes the same miscompile class for args as §2.5
  closed for assign.
- **Complete `retype_literal`** (typer.tks:3718) to recurse into `TIfExpr`/`TMatchExpr` arm trailing
  values, mirroring its `TArrayLit` recursion. This is the belt-and-suspenders correctness fix so any
  adopt-after path (assign, return, field) that keeps using `literal_adopts` cannot leave a stale
  inner leaf. (Directly closes §2.5.)

Backends are UNTOUCHED. The typed-tree *shape* is unchanged; (A) only causes more literals to be
born at their narrow width — exactly the shape codegen already receives today from the adopt-after
sites (empirically: narrow-typed literal leaves at args/fields/returns compile and run correctly on
both backends now). So this is a checker-local change, primarily one file (`src/checker/typer.tks`).

---

## 4. Fixpoint safety (why (A) is byte-preserving for today's corpus)

The self-host gate requires the compiler to reproduce its own `.tkb`/binary byte-for-byte. (A) is
byte-preserving because:

1. **Same fit rules, same result node.** Borning a literal at `expected` uses the *same*
   `cast_kind`/`value_fits`/`float_fits` predicates the adopt-after path uses today. For any literal
   that compiles today, the FINAL typed node (kind + `i128` value + resolved `.type`) is identical
   whether the type arrives by born-at-site or by adopt-after. The `.tkb` serializes that final node,
   so the emitted bytes are identical.
2. **Only two behavioural deltas, both outside the fixpoint set.**
   - *Newly-accepted programs* (G1/G2/G3): these do NOT compile today, so there are no existing bytes
     to preserve — purely additive.
   - *Newly-corrected programs* (§2.5 miscompile): these compile today but to WRONG bytes; a program
     that relied on the wrong bytes was never fixpoint-valid. The compiler's own source must not
     contain such a construct (it would be a latent bug); a pre-flight grep for
     `= (if|match) …` value-bindings/assigns of over-`i64` literals confirms the corpus is clean, so
     the correction changes no in-corpus output.
3. **No default drift.** The no-context path (`type_number`) is unchanged; a literal with no
   application site is still `i64`/`f64` (M.2).
4. **Revalidate is not newly violated.** `revalidate.tks:49` asserts a `TNumber` leaf's type equals
   the default `i64`/`f64`. `retype_literal` ALREADY violates this at the adopt sites; revalidate does
   not descend into bindings/args yet (only binary/unary/compare/cast operands — where the
   other-operand adoption already retypes). (A) adds no new reachable violation. **Law tension noted
   for later:** when deep `validate_statement` lands (the deferred §VI gap), rule :49 must be relaxed
   from "== default" to "a `Prim`/`Byte` the value fits". That relaxation is NOT part of (A); it is
   flagged so the future crumb does not regress (A). Recommended resolution: relax :49 to a fit-check,
   citing this doc.

---

## 5. Crumb sequence for (A) (each gate-able)

Each crumb keeps the seed green (`teko build` + full `.tkt` gate) and is byte-preserving per §4.
All snippets land in full-Javadoc style; implementers copy verbatim.

**Crumb A1 — complete `retype_literal` for `if`/`match` (correctness first).**
Extend `retype_literal` (typer.tks:3718) with `TIfExpr`/`TMatchExpr` arms that rebuild each
non-diverging branch/arm trailing value via `retype_literal(_, to)`. No new call sites. Gate: the
§2.5 assign probe returns the correct value on BOTH backends.

**Crumb A2 — born-at-site for scalar & negative literals in `type_value_expected`.**
Add the `Number` and `Unary(Minus, Number)` arms (§3.1). Reuse `annotated_literal_ok`'s reject
messages for M.1. Gate: G1 (`let x: byte = 0x41`, `const B: byte = 5`) compiles; `let x: u8 = 256`
still fails early; `let x: i8 = -5` compiles; `let x: u8 = -1` still fails.

**Crumb A3 — born-at-site for `if`/`match` values in `type_value_expected`.**
Add the `IfExpr`/`MatchExpr` arms (§3.1). Gate: G2 (`let x: u32 = if c {1} else {0}`,
`= match k {…}`) compiles and round-trips over-`i64` values correctly.

**Crumb A4 — generalize the `ArrayLit` expected path to all element types.**
Lift the interface-only guard (typer.tks:3187) so any `[]E` expected threads `elem` per element via
`type_value_expected`. Gate: G3 (`let xs: []byte = [0x1, 0x2]`, `[]byte = [ if c {1} else {2} ]`)
compiles; `let xs: []u8 = [1, 300, 3]` still fails early per-element.

**Crumb A5 — unify the binding/const predicate.**
In `type_binding` (typer.tks:3281) and `type_const_decl` (typer.tks:4712) replace the
`annotated_literal_ok` branch with the shared `literal_adopts` + `retype_literal` pair. Gate: no
regression across the whole `.tkt` suite; the two sites now behave identically to `type_assign`.

**Crumb A6 — thread expected for `if`/`match`/array CALL ARGS.**
Extend the fast-path guard at typer.tks:1131. Gate: `takesu32(if c {big} else {0})` round-trips.

**Crumb A7 — ritual gate + regression fixtures (see §6).**
Add the fixtures; run the FULL gate (`.tkt` + native + VM differential + fixpoint self-compile). This
is the ritual point that ratifies (A) into 0.3.0.28.

### Ritual points (where the full gate must pass)
- End of **A1** (correctness landing — no miscompile may ship).
- End of **A5** (the capability is complete for declarations).
- **A7** — the closing ritual: full `.tkt`, VM↔native differential, and a clean fixpoint
  self-compile (the seed at 0.3.0.28 must reproduce itself byte-for-byte). This is the gate that
  authorizes the seed bump and unblocks (B).

---

## 6. Regression fixtures (inputs → expected exit, VM and native)

Add as `examples/regressions/*` (trailing top-level expr = exit code; must match on `teko run` (VM)
and `teko build` + execute (native)). Positive fixtures assert the literal compiles WITHOUT a cast
AND yields the right value; negative fixtures assert the diagnostic still fires.

Positive (compile-without-cast + value round-trip):
- `lit_byte_binding` — `let x: byte = 0x41` → exit `0x41` (65). No `to byte`.
- `lit_byte_slice` — `let xs: []byte = [0x1, 0x2]`, exit `xs[0] + xs[1]` (3). No cast.
- `lit_if_u32_binding` — `let x: u32 = if true { 7 } else { 9 }` → exit 7. No cast on arms.
- `lit_match_u32_binding` — `let x: u32 = match 1 { 1 => 7; _ => 9 }` → exit 7.
- `lit_i128_if_assign_2p63` — the §2.5 program; `d == 2^63` → exit 7 (guards the miscompile fix).
- `lit_const_byte` — `const B: byte = 5` referenced in a value slot → exit 5.
- `lit_arg_if_over_i64` — `takes128(if true { 2^63 } else { 0 })` compared == 2^63 → exit 7.

Byte-preservation / no-regression (must behave EXACTLY as today):
- `lit_redundant_cast_still_ok` — `let x: u32 = 5 to u32` → exit 5 (redundant cast stays legal).
- `lit_default_no_context` — `let x = 5` (i64 default) unchanged.

Negative (diagnostic must still fire — checker `.tkt` cases, not exit-code fixtures):
- `let x: u8 = 256` → "literal out of range for the annotated type (M.1 — fail early)".
- `let x: u8 = -1` → "a negative literal cannot be annotated as an unsigned type (M.1)".
- `let xs: []u8 = [1, 300, 3]` → per-element range error.
- `256 to u8` → "constant out of range for the cast target (M.1 — fail early)" (truncation stays an
  error; A must NOT relax it).
- A float into an int slot (`let x: i32 = 1.5`) and an int into a float slot (`let x: f32 = 1`) →
  the existing kind-mismatch messages, unchanged.

Also add unit `.tkt` cases mirroring the existing `retype_literal_recurses_into_array_elements`
(checker_test.tkt:558): a `retype_literal_recurses_into_if_arms` and `_into_match_arms` asserting the
inner branch leaf is retyped (guards A1 against future erosion).

---

## 7. Risks & law tensions (with recommended resolution)

- **R1 — revalidate.tks:49 vs born-narrow leaves.** Discussed in §4.4. Not triggered by (A) today;
  flagged so the deferred deep-revalidation crumb relaxes :49 to a fit-check rather than regressing
  (A). Recommended: relax :49 when `validate_statement` lands. No action in (A).
- **R2 — the miscompile fix changes output.** By §4.2 the only changed output is for a
  currently-miscompiled (non-fixpoint-valid) construct; a pre-flight corpus grep confirms the seed
  contains none, so fixpoint holds. Land A1 FIRST and gate before anything additive.
- **R3 — double-typing composites.** Threading `expected` into `if`/`match`/array means the composite
  is typed once with expected (not typed-then-retyped). Ensure the branch-to-branch cross-adoption
  (typer.tks:3097, 3149) still runs and is idempotent when a branch is already `expected` (it is:
  `literal_adopts(x, x.type)` via `type_eq` short-circuit). No tension, but a test asserts idempotence.
- **R4 — `annotated_literal_ok` reuse vs retirement.** Recommended: keep it as the *message* source
  inside §3.1's `Number` arm (its float/negative/range diagnostics are load-bearing and tested at
  checker_test.tkt:546–548), but remove it as a *decision* predicate at the two declaration sites
  (A5). This honors M.5 without discarding the tested error text.
- **No genuine unresolved tension → no HALT.** The owner ruling + M.3/M.1/M.5 fully determine (A);
  every edge resolves law-first above.

---

## 8. (B) — next-bump note (bootstrap sequencing; NOT designed here)

Once 0.3.0.28 seeds (A), a source file with a bare `lit` in an application slot compiles. **Only
then** may (B) delete the ~1441 redundant `NNN to T` casts (heaviest: `to byte` and `to u32`),
because a de-casted source needs a SEED that already understands context-typed literals. (B) is
therefore mechanical + gated on the new seed and lands at 0.3.0.29. Deletion is limited to casts where
the target == the site's expected type (redundant); a cast that changes the type (a genuine widen/
narrow, or a runtime-value truncation) is preserved. (B) is out of scope for this doc beyond this
sequencing note.

---

### Appendix — key anchors (src/checker/)
- `type_number` typer.tks:13 · `value_fits` :1235 · `float_fits` :1254 · `cast_kind` :1263 ·
  `const_range_check` :1286 · `annotated_literal_ok` :1310 · `type_call` arg path :1116–1206 ·
  struct field :1980–2017 · `type_value_expected` :3183 · `type_array_lit_expected` :3219 ·
  `type_binding` :3272 (annotated_literal_ok at :3281) · `type_if` cross-adopt :3095 ·
  `type_match` join :3149 · assign adopt :3430/:3435 · `literal_adopts` :3646 · `retype_literal`
  :3718 · `type_const_decl` :4707 (annotated_literal_ok at :4712).
- `widens_into` resolve.tks:748/:814 (no integer widening — i64↛i128, so adoption, not widening,
  types cross-width literals).
- `revalidate` TNumber default invariant revalidate.tks:49.
- `lit_int` (i128 carrier) parse_lit.tks:8.
