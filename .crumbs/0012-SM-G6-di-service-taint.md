---
seq: 0012
crumb-id: SM-G6
milestone: M1
gate: "[dry]"
reseed-class: "(folds R1)"
deps: []
sources:
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:484-799"  # §7 DI service/svc (all)
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1179-1180"# §10 Phase G — G6
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1108"     # §9.1 program_uses_di-gated
---

# 0012 · SM-G6 — DI `service`/`svc` escape-taint + string-key (finish taint)

> DI `service`/`svc` escape-taint + string-key (Part A banked; finish taint).

## Goal

DI Part A (the `service`/`svc`/`ServiceLifetime` keyword surface + registry) is already BANKED in the
seed (TC-0, `0a246dfe`). This crumb FINISHES the DI feature: the STRICT service-escape taint/flow rule
(`service_taint.tks`), string-key registration (flip the `di_key_rejected` rejection to acceptance), and
the transitive-interface conflict rule. A service VALUE can NEVER be stored in a field, passed as a
param, or returned — ONLY `svc<T>()` and `static ctor()` may PRODUCE one. This auto-closes the Marshall
escape (a service can't be a param → can't reach `__wrap`/`__unwrap`, SM-G5 §6.1a) and is why services
need no lifetime annotation at USE sites. The trusted DI backend is EXEMPT (it holds instances by
pointer) but arena-bounded, so no UAF. Byte-preserving until used (`program_uses_di`-gated;
`src/` uses no DI); its seed folds into SM-R1.

## Where

- `src/checker/service_taint.tks` — NEW module: `is_service_tainted` + `check_no_service_escape`.
- `src/checker/di.tks:16` — `DiProvider` — widen to carry `iface`, `key`, `kind`, `impl_name`,
  `ctor_symbol`, `service_id` (the (interface, key) row shape, §7.2).
- `src/checker/di.tks:98` — `register_item_providers` — read `is_service` + the lifetime keyword.
- `src/checker/di.tks:116` — `register_over_implements` — register under the DIRECT interface AND every
  transitive ancestor (`interface_ancestry`); the per-(iface,key) duplicate check fires the conflict.
- `src/checker/di.tks:258` — `di_key_rejected` — FLIP the string-key rejection into acceptance (the AST
  already carries `has_di_key`/key fields inert, `di.tks:9`).
- `src/checker/di.tks:134` — `choose_factory` — REPLACED by "the static named `ctor`" (mandatory, no-arg;
  a `service` without `static ctor(): self` is a hard error).
- `src/checker/di.tks:36` — `program_uses_di` — the existing gate that keeps a no-DI program byte-identical.
- `src/checker/di.tks:368/373` — `tk_region_register`/`tk_region_lookup`/`di_type_id` — REUSE for the
  scoped registry + `service_id`; the ONLY candidate `teko_rt` addition is `tk_region_parent` (add ONLY
  if not already exposed — a one-line read accessor, behavior-identical).

## How

1. **Widen the table row** (`DiProvider`, `di.tks:16`) to the (interface, key) shape:

```teko
/**
 * DiProvider — one row of the compile-time service table: a single (interface, key) slot a concrete
 * service fills. A service registers ONE row per interface it transitively satisfies, so two services
 * under sibling children of one ancestor both emit an ancestor row — turning `svc<Ancestor>()` into a
 * conflict. `service_id` (`di_type_id(impl_name)`) is the runtime identity emitted as a constant at each
 * substituted call site to key the root singleton slot and the per-arena scoped registry.
 *
 * @field iface        canonical name of a satisfied interface (direct OR transitive ancestor)
 * @field key          the optional string key ("" = unkeyed) that partitions the table
 * @field kind         the lifetime: Singleton | Scoped | Transient
 * @field impl_name    canonical name of the concrete `service` type
 * @field ctor_symbol  the emitted symbol of the service's `static ctor` (real code)
 * @field service_id   di_type_id(impl_name) — root-slot / scoped-registry key
 * @since 0.3.1
 */
pub type DiProvider = struct { iface: str; key: str; kind: parser::DiKind; impl_name: str; ctor_symbol: str; service_id: u64 }
```

2. **Transitive ancestry + conflict** (`register_over_implements`, `di.tks:116`): register each provider
   under its interface AND every transitive ancestor (`interface_ancestry`). Resolution: `T` (+ key)
   resolves IFF EXACTLY ONE row matches `(iface == canonical(T), key == given)`; zero → "no service
   provides T"; ≥2 → "ambiguous: N services satisfy T". The duplicate REGISTRATION under one (iface, key)
   is the error, independent of use.

```teko
/**
 * interface_ancestry — the full set of interface canonical names a service provider satisfies: the
 * interface it specializes plus every interface that one transitively extends. Registering under ALL of
 * these is what lets `svc<Ancestor>()` resolve — and what makes two providers of sibling children of one
 * ancestor a COMPILE conflict on that shared ancestor.
 *
 * @param iface  the canonical interface name a service specializes
 * @param table  the collected type table (for the `extends` edges)
 * @return       the canonical names of `iface` and all its transitive interface ancestors
 * @since 0.3.1
 */
fn interface_ancestry(iface: str, table: TypeTable): []str
```

3. **String-key acceptance** (`di.tks:258`): flip `di_key_rejected` into acceptance. Registration syntax
   `service <lifetime>(key <string-literal>) <Interface>`; resolution `svc<T>(key "fast")` looks up the
   `(iface, key)` slot, `svc<T>()` the `(iface, "")` unkeyed slot. Two providers of one interface with
   DIFFERENT keys are NOT a conflict (distinct slots); SAME key (incl. both unkeyed) IS. A strict superset
   of core DI: with no keys, the table and every diagnostic are byte-identical to Part A.
4. **The escape taint/flow rule** (`service_taint.tks`, NEW):

```teko
/**
 * is_service_tainted — true iff a value's origin is a `svc<T>()` call or a `service` type's `static ctor`
 * return. A tainted value may be USED (methods called, `self`-mutated) but may NEVER ESCAPE its producing
 * scope. Taint propagates through direct aliasing (a `var s = svc<T>()` is tainted); a service cannot be
 * laundered — accepting it as a param is itself the forbidden escape, so no fn accepts-and-returns it.
 *
 * @param e    the typed expression under flow analysis
 * @param env  the checker environment (for binding origins)
 * @return     true iff `e` carries service taint
 * @since 0.3.1
 */
fn is_service_tainted(e: TExpr, env: Env): bool

/**
 * check_no_service_escape — reject every escape of a service-tainted value: a field store, an argument
 * (even an interface-typed param), a return, or an aggregate-literal element. Keys on TAINT ORIGIN, not
 * the declared type, so interface generalization (`a.serv: AnInterface = svc<Impl>()`) cannot bypass it.
 * Runs over USER-authored bodies only; the `svc`-substituted code and ctor plumbing are synthesized
 * AFTER this pass, so the trusted backend is exempt by construction (no flag needed).
 *
 * @param stmt  the typed statement
 * @param env   the checker environment
 * @return      null when no service value escapes, else the located escape diagnostic
 * @throws      on a field store / param pass / return / aggregate-bind of a service-tainted value
 * @since 0.3.1
 */
fn check_no_service_escape(stmt: TStatement, env: Env): null | error
```

5. **Mandatory ctor** (`choose_factory`→ the static `ctor`, `di.tks:134`): a `service` without
   `static ctor(): self` is a hard error. The ctor is emitted with a hidden destination region (DPS, §1.1
   applied to the ctor return); the substituted `svc` site passes root (singleton) / current (transient)
   / current-on-miss (scoped) as the dest. (The `svc` inline-expansion per lifetime is Part-A/lowering
   territory — this crumb finishes taint + key + conflict; the expansion machinery is banked.)
6. **Exemption is structural.** `check_no_service_escape` runs over user bodies only; substitution and
   the registry primitives (`tk_region_register`/`tk_region_lookup`/`tk_region_parent`) are synthesized
   post-flow or are runtime builtins — outside the checked corpus, no exemption FLAG needed. Backend-held
   pointers are arena-bounded (singleton in ROOT, scoped shares its region's lifetime, transient never
   stored) — no UAF.
7. **Confirm byte-neutrality.** `program_uses_di` (`di.tks:36`) gates the whole pass; `src/` uses no DI →
   byte-identical `[dry]` build.

## Rulings & laws

- **Teko-only:** checker `.tks` (`di.tks`, new `service_taint.tks`); the only candidate C addition is
  `tk_region_parent` (add ONLY if absent — behavior-identical read accessor, within runtime exception).
- **W15 full Javadoc** on `DiProvider`, `interface_ancestry`, `is_service_tainted`,
  `check_no_service_escape`; no `//`.
- **Escape rule keys on TAINT ORIGIN (owner STRICT, §7.4):** interface generalization cannot bypass it;
  the trusted backend is exempt-but-arena-bounded.
- **ONE hash project-wide:** `service_id` = `di_type_id` — reuse.
- **Byte-preserving until used (§9.1):** `program_uses_di`-gated; does NOT drive the reseed.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step.

## Fixtures

`src/` uses no DI, so NONE of the DI accept/reject paths are self-exercised — isolated oracles required:

| fixture | asserts | expected |
|---|---|---|
| `svc_singleton_once` | two `svc<S>()` of a singleton return the same instance | 0 |
| `svc_transient_always_new` | two `svc<S>()` of a transient return distinct instances | 0 |
| `svc_scoped_ancestry_reuse` | a singleton consuming a scoped dep gets the call-site scoped instance | 0 |
| `svc_conflict_ancestor` | owner's A/B/C/D/E: `svc<A>()` is a compile conflict | EXPECT_COMPILE_FAIL |
| `svc_string_key_disambiguates` | two keyed providers of one iface; `svc<I>(key "x")` resolves | 0 |
| `service_escape_field_rejected` | `a.b = svc<S>()` (field store) rejected | EXPECT_COMPILE_FAIL |
| `service_escape_param_rejected` | `fun(svc<S>())` rejected, even interface-typed param | EXPECT_COMPILE_FAIL |
| `service_escape_return_rejected` | returning a service value rejected | EXPECT_COMPILE_FAIL |
| `service_ctor_mandatory` | a `service` without `static ctor(): self` is rejected | EXPECT_COMPILE_FAIL |

## Gate

`[dry]` — compile + the nine fixtures + fixpoint (byte-identical; `program_uses_di`-gated). "Green" =
taint blocks every escape channel keyed on origin, string keys partition the table, the transitive
ancestor conflict fires, the mandatory-ctor rule fires, `[dry]` build byte-identical. Reseed-class:
`(folds R1)`.

## Deps

`—` (Part A registry is banked in the seed; SM-G5's Marshall service block relies on this escape rule but
is not a build dependency).

## Done when

`check_no_service_escape` rejects field-store/param/return/aggregate escapes keyed on taint origin,
string-key registration partitions the (iface, key) table, the transitive-ancestor conflict + mandatory-
ctor rules fire, all nine fixtures pass, and a `[dry]` build is byte-identical (no-DI program unchanged).
