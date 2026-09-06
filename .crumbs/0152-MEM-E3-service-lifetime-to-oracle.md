---
seq: 0152
crumb-id: MEM-E3
milestone: M5
gate: "[dry]"
reseed-class: "fixpoint-rebuild"
deps: []
sources:
  - "docs/design/modelo-de-memoria-por-escopo-0.3.1.md:156-188"       # §2a the declarative root surface
  - "docs/design/plano-secao7-di-service-svc.md"                      # the service surface (landed)
  - "DECISION_LOG.md:1161"                                            # D130 refinement 7 (service, not #singleton)
  - "src/checker/residence.tks:35-87"                                 # residence_tier / is_singleton stub (line 83)
  - "src/checker/resolve.tks:884-902"                                 # is_service_name / service_lifetime_of / lifetime_eq
  - "src/codegen/codegen.tks:3618-3644"                               # svc_scope_expr (Singleton→root already)
---

# 0152 · MEM-E3 — checker: wire `service <lifetime>` to the residence oracle (unstub `is_singleton`)

> Close D130 refinement 7: root comes from `service singleton` (the LANDED surface), NOT a `#singleton`
> binding attribute. The residence oracle stubs `is_singleton = false` (`residence.tks:83`); thread the
> `TypeTable` into `residence_plan` and derive the singleton fact from the binding's service lifetime.
> Additive enrichment of the plan output — the plan still has ZERO consumers ⇒ byte-identical.

## Goal

The `service <lifetime>` surface is fully landed (`parse_decl.tks:760`, `ast.tks:212`, checker
`is_service_name`/`service_lifetime_of`/`lifetime_eq` at `resolve.tks:884-902`, `codegen.tks:3618`
already routes a `service singleton` CTOR to `tk_region_root()`). What is MISSING is the oracle link: a
BINDING whose type is a `service singleton` must reside in `Root`, but `residence_plan` hardcodes
`is_singleton = false`. This crumb threads the `TypeTable` into `residence_plan`/`plan_binding` and sets
`is_singleton` from `service_lifetime_of(binding_type) == Singleton`. `scoped`/`transient` on a binding
map to the ordinary scope/frame residence (the default already IS ephemeral-by-scope — no extra meaning).
No consumer of the plan yet ⇒ byte-identical.

## Where

- `src/checker/residence.tks:49` — `residence_plan(f: TFunction, table: TypeTable)` (thread the table).
- `src/checker/residence.tks:81-87` `plan_binding` — replace `var is_singleton = false` with
  `binding_is_service_singleton(b, table)` (resolve the binding's type name → `is_service_name` +
  `service_lifetime_of == Singleton`).
- `src/checker/resolve.tks:884-902` — REUSE `is_service_name`/`service_lifetime_of`/`lifetime_eq`
  (no new mechanism).

## How

```teko
/**
 * binding_is_service_singleton — true when binding `b`'s declared/inferred type is a `service singleton`
 * (`type N = service singleton { … }`), the DECLARATIVE root residence (D130 refinement 7). Reuses the
 * landed `is_service_name` + `service_lifetime_of` (`resolve.tks:884-902`) — the same trail the service
 * CTOR routing already uses (`codegen.tks:3618`). A `service scoped`/`transient` binding is NOT root:
 * it takes the ordinary scope/frame residence (the default is already ephemeral-by-scope). This is the
 * ONLY root origin for a binding; the other is cross-thread (`chan`/`wait_group`), which has no surface
 * yet.
 *
 * @param b      the typed binding
 * @param table  the checker type table (to resolve the type name to its service lifetime)
 * @return       true iff `b`'s type is a `service singleton`
 * @since 0.3.1
 */
fn binding_is_service_singleton(b: TBinding, table: TypeTable): bool
```

1. Thread `table` through `residence_plan` → `plan_block_bindings` → `plan_binding`.
2. `is_singleton = binding_is_service_singleton(b, table)` (was hardcoded false, `residence.tks:83`).
3. Confirm the plan still has no consumer ⇒ byte-identical `[dry]`.

## Rulings & laws

- **Teko-only:** `src/checker/*.tks`; reuses the landed service trail — NO change to the service surface
  or the class DI.
- **D130 refinement 7:** root = `service singleton`, NOT `#singleton` binding (which does not exist).
- **Additive/inert:** the plan is still unconsumed → byte-identical.
- **W15 full Javadoc; no `//`.** **Safety:** never `teko test .`; subshell `ulimit -v 4718592`.

## Fixtures

none — the plan is unconsumed; byte-identity by the fixpoint. The root residence is exercised by the
`mem_service_root` shadow fixture once `MEM-W3`/`MEM-W4` consume the plan.

## Gate

`[dry]` — compile + fixpoint (byte-identical; plan unconsumed). "Green" = `residence_plan` takes the
table, a `service singleton` binding computes `Root`, `[dry]` byte-identical. Reseed-class:
`fixpoint-rebuild` (folds into RESEED-1 of `MEM-E5`).

## Deps

`—` (batches with the other teaching crumbs; the service surface is already landed).

## Done when

`residence_plan` threads the `TypeTable`, `is_singleton` is derived from `service singleton` (the stub at
`residence.tks:83` gone), `scoped`/`transient` bindings take scope/frame residence, no consumer yet, and
`[dry]` is byte-identical.
