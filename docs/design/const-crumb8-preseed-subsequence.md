# #594 — Crumb 8 pre-seed-bump sub-sequence (c8b … c8e)

Architect design-ahead, READ-ONLY (no product code here). Closes the four reviewer
findings on `remodel/constants` tip `dcd5fb5` **before SEED BUMP #1**. Owner rule
(2026-07-16): every real failure is fixed NOW — nothing deferred, no "merge-OK with
follow-up". Source of truth: `docs/design/const-module-level-plan.md` (§4.9, D4–D6,
Crumb 8 ~L953); `DECISION_LOG.md` D40.

All line anchors below were re-confirmed against `dcd5fb5`.

## The keystone insight (why c8b and c8c are ONE resolution model)

`type_var` (`typer.tks:44`) already stamps every reference with the namespace the
type-checker resolved it to: `TVar { name; is_func; func_ns = b.ns }` where `b` is the
binding `lookup_binding` chose (tail-first / last-match / `is_const`-aware). A bare
project-const reference therefore already carries `func_ns = the project const's
declaring namespace`; a dep-only const carries the dep's module namespace.

The inliner ignores that stamp: `scalar_const_init` (`consteval.tks:566`) matches on
NAME ALONE and returns the FIRST hit, while `collect_module_consts`
(`consteval.tks:584`) walks `all_items` which `type_program_with_deps_pre_mono`
(`typer.tks:4777-4791`) built **dep-first, project-last**. So the two resolvers
disagree by construction: typer picks project `P`, inliner picks dep `P`.

**Fix, law-first:** make the inliner key on `(name, namespace)` and match `v.func_ns`.
Because the typer already recorded the correct namespace in `func_ns`, the inliner's
resolution becomes IDENTICAL to `lookup_binding`'s **by construction** — not by a
re-derived heuristic. This single mechanism resolves BOTH findings:

- **c8b (finding 1):** a bare `TVar` now substitutes the const whose `(name, ns)`
  equals `(v.name, v.func_ns)` → project `P` shadows dep `P`.
- **c8c (finding 2):** a namespace-qualified value `m1::P` lowers to the SAME node
  shape — `TVar { name = "P"; func_ns = "m1" }` — and the same keyed inliner
  substitutes the dep's `P`. No separate "qualified substitution" path is needed.

**Fixpoint safety:** in the single-project self-build every const's `func_ns` equals
its own map namespace, so `(name, ns)` is an exact self-match and the substitution is
unchanged → gen1==gen2 preserved. The keying only ever *disambiguates* a real
cross-module duplicate, which cannot arise in a single project.

---

## Crumb c8b — namespace-key the scalar inliner (finding 1) — CORE, SERIAL, FIRST

Owns PR #612 / branch `fix/issue-594-c8b-const-collision` (today an empty placeholder
commit — nothing implemented yet).

### Files / functions touched (all `src/checker/consteval.tks`)
- `type ScalarConstMap` (`:553`) — add a third parallel list `nss: []str`.
- `scalar_const_init` (`:566`) — add `nss` + a `ns` query param; match `names[i]==name
  && nss[i]==ns`.
- `build_scalar_map` (`:609`) — push `cd.namespace` into `nss` alongside `cd.name`.
- `inline_rw_var` (`:730`) — pass `v.func_ns` as the query ns.
- The `inline_rw_*` family (`inline_rw_expr/exprs/fspec/compare/match/arm/interp/array`,
  `:627-836`) + `rewrite_function`/`rewrite_const_init`/`inline_place_item`/
  `inline_scalar_const_item` — thread the new `nss` list.

### RECOMMENDED shape (W15 "toque = limpa"): carry the map, not parallel arrays
Since every `inline_rw_*` signature must change anyway to gain `nss`, replace the
`(names: []str, inits: []TExpr)` parameter PAIR with a single `smap: ScalarConstMap`
carrier across the whole family. Net arity drops (1 param, not 3). Signatures:

```teko
/**
 * ScalarConstMap — the crumb-5/8b substitution table: three PARALLEL lists over every
 * scalar module-level const (dependency-ordered, initializers collapsed). A reference
 * is rewritten by a `(name, namespace)` lookup — the SAME key `lookup_binding` resolved
 * to (`func_ns` on the `TVar`), so the inliner and the type-checker never diverge
 * (#594 crumb 8b; the c8-reviewer's dep/project collision).
 *
 * @since #594
 */
type ScalarConstMap = struct { names: []str; nss: []str; inits: []TExpr }

/**
 * scalar_const_init — the collapsed initializer for the scalar const named `name`
 * declared in namespace `ns`, or null when no scalar module-level const matches BOTH
 * (a local, a function, an aggregate const, or a same-name const in a DIFFERENT module).
 * Keying on `(name, ns)` makes the inliner agree with `lookup_binding`'s namespace-aware,
 * tail-first resolution: a project const shadows a dep const of the same bare name
 * (#594 crumb 8b).
 *
 * @param map   the scalar substitution table (parallel names/nss/inits)
 * @param name  the referenced name
 * @param ns    the namespace the type-checker resolved the reference to (`TVar.func_ns`)
 * @return      the matching const's collapsed initializer, or null
 * @since #594
 */
fn scalar_const_init(map: ScalarConstMap, name: str, ns: str) -> TExpr?

/**
 * inline_rw_var — rewrite a reference `TVar`: replaced by a clone of the collapsed
 * initializer of the scalar module-level const whose `(name, namespace)` equals
 * `(v.name, v.func_ns)`; any other `TVar` (a local, a param, a fn-value, a
 * different-module same-name const) passes through unchanged (#594 crumb 5/8b).
 *
 * @param v    the reference node
 * @param e    the original expression (returned when `v` is not a matching scalar const)
 * @param map  the scalar substitution table
 * @return     the substituted initializer, or `e` unchanged
 * @since #594
 */
fn inline_rw_var(v: TVar, e: TExpr, map: ScalarConstMap) -> TExpr
```

### Fixtures
- **Unit (checker), NEW** — the dep↔project collision that the E2E harness cannot yet
  reach (dep is an in-memory `TProgram`, mirroring the existing crumb-8 unit tests):
  build a dep `TProgram` with `pub const P: i64 = 7` in namespace `m1`; build a project
  `TProgram` with its own `const P: i64 = 9` (namespace `""`) and a fn `main` returning
  `P`; run `type_program_with_deps` + `inline_consts`; assert `main` inlines **9**, not
  7. Add the symmetric case where the project has NO `P` and references the dep's `P`
  bare → inlines **7** (proves the fallback still surfaces the dep const).
- **both-engine (E2E)** — deferred to c8d (needs the harness capability); the c8d
  fixture is the end-to-end proof of this same shadowing.

### Gate
Full ritual on the c8b sub-PR: both engines + `.tkt` + **fixpoint gen1==gen2** + 100%
delta coverage on the touched `inline_rw_*` arms + native `EXPECT_COMPILE_FAIL` leg
(green once c8e lands; if c8e not yet merged, run the new unit test + the existing
positive const fixtures).

---

## Crumb c8c — namespace-qualified VALUE path `m1::P` (finding 2, issue #613) — CORE, SERIAL, AFTER c8b

Depends on c8b: the qualified value lowers to a `TVar` carrying `func_ns`, which only
the c8b-keyed inliner substitutes correctly.

### Decisions (law-first — recorded for DECISION_LOG)
- **(a) Generalize to ALL value bindings (const + fn-value), not const-only.** Issue
  #613 states the gap is general (namespace-qualified value refs exist for neither
  const nor fn-value; only `m::f()` CALLS resolve). The lookup is identical for both;
  `is_func`/`func_ns` already distinguish a fn-value (→ `tk_closure` mangle) from a
  const/local at lowering. Generalizing closes #613 100% (issues são 100%) at zero
  extra machinery. **DECIDED: generalize.**
- **(b) Precedence vs the existing `Type::Member` arm:** try the TYPE-qualifier arms
  first (member-const `m1::T::Q`, enum/flags `E::M`), fall through to the
  NAMESPACE-value arm ONLY when the qualifier does not resolve as a type. A 2-segment
  `A::B` has `A` as either a type or a namespace (disjoint in practice — a type and a
  namespace sharing a name is a pre-existing identity collision, out of scope).
  Backward-compatible, smallest safe step.
- **(c) Interaction with `use`:** `use m1::P` already surfaces `P` bare (via
  `seed_dep_consts`); the qualified `m1::P` is an alternate spelling of the same
  binding. Both must resolve after this crumb; they do — same `ValBinding`.

### Files / functions touched
- `src/checker/scope.tks` — NEW `lookup_value_in_ns` (namespace-FILTERED, unlike the
  name-only `lookup_binding:139`).
- `src/checker/typer.tks` — `type_path_expr` (`:2392`): drop `ref_ns: str`, take
  `env: Env` (so it can look up value bindings + read `env.cur_ns`); add the
  namespace-value fallback arm on the `resolve_named` miss (`:2403`). **Single call
  site** at `typer.tks:2471` (`type_path_expr(pe, table, env.cur_ns)` →
  `type_path_expr(pe, table, env)`).
- No inliner change — c8b's `(name, ns)` keying already substitutes the emitted
  `TVar { func_ns = "m1" }`.

```teko
/**
 * lookup_value_in_ns — the value binding named `name` declared in namespace `ns`, or an
 * error when no such binding exists. Unlike `lookup_binding` (name-only, tail-first),
 * this filters by BOTH name and declaring namespace — the resolver for a
 * namespace-qualified value spelling `ns::name` (#594 crumb 8c, issue #613). Scans the
 * mutable bindings then the sealed globals (a dep's exported const/fn is a global).
 *
 * @param env   the type-checking environment
 * @param name  the value's bare name (the path's last segment)
 * @param ns    the qualifier namespace (the path minus its last segment)
 * @return      the matching binding, or a located "unknown value: ns::name" error
 * @since #594
 */
fn lookup_value_in_ns(env: Env, name: str, ns: str) -> ValBinding | error

/**
 * type_path_expr — type a `Type::Member` / `ns::value` two-segment path used as a VALUE.
 * Resolution order: (1) member const `Owner::NAME` when the leading path resolves to a
 * struct/class/abstract type (walks the base chain); (2) enum/flags member `E::M`;
 * (3) namespace-qualified value `ns::NAME` — a `pub`/`exp` module const or a top-level
 * fn-value in module `ns` — reached only when the qualifier is NOT a type. The
 * namespace arm emits `TVar { name = NAME; is_func = <Func?>; func_ns = ns }`, the same
 * node a bare reference produces, so the c8b-keyed scalar inliner substitutes a
 * qualified const identically and a qualified fn-value lowers to its `tk_closure`
 * mangle (#594 crumb 8c, issue #613).
 *
 * @param pe     the parsed path expression
 * @param table  the type table
 * @param env    the type-checking environment (for the value-binding lookup + cur_ns)
 * @return       the typed value reference, or a located error
 * @since #594
 */
fn type_path_expr(pe: parser::PathExpr, table: TypeTable, env: Env) -> TExpr | error
```

### Fixtures
- **Unit (checker), NEW** — a dep `TProgram` (`m1`) with `pub const P: i64 = 5` +
  `pub fn twice(x: i64) -> i64`; a project referencing `m1::P` (value) and `m1::twice`
  (fn-value bound to a local then called); assert both type + `m1::P` inlines to 5.
- **both-engine (E2E)** — the c8d fixture's consumer uses the `m1::P` spelling (see
  c8d); this is where the qualified path is proven through the whole pipeline.
- **EXPECT_COMPILE_FAIL, NEW** — `m1::PRIV` where `m1`'s `PRIV` is `private` → the
  cross-module visibility error (proves the namespace arm still enforces D4 visibility);
  exercised by the c8e leg.

### Gate
Full ritual on the c8c sub-PR; both engines + fixpoint + 100% delta coverage. The new
`EXPECT_COMPILE_FAIL` fixture requires c8e's leg to be asserted in CI.

---

## Crumb c8d — E2E cross-module both-engine fixture + harness dep-provisioning (finding 3) — HARNESS, PARALLEL to c8b/c8c

The central capability (serialize → `.tkl` → deserialize → `seed_from_dep` → inline →
lower → run) has NO end-to-end proof; the unit tests only prove isolated legs.
`examples/regressions/` never provisions `packages/`, so a dep-consuming fixture cannot
build under `native_regressions.sh` / `sanitizers.yml` today. This crumb ADDS that
harness capability (the "adiantar" work) + the fixture.

### Harness capability (NEW)
`teko build <pkg> -o <out>` with a `kind = "package"` `.tkp` already emits
`<out>/<name>-<version>.tkl` (`project.tks:800-831`), and `load_dep_program`
(`project.tks:143`) consumes `packages/<name>-*.tkl`. A new driver (NEW
`scripts/crossmodule_regressions.sh`, or a function inside `native_regressions.sh`)
performs, for BOTH engines (`$TEKO` seed and `./bin/teko` self-hosted):

1. build the dep project (`.../dep/`, `kind = package`) into a temp dir → `m1-*.tkl`;
2. copy the `.tkl` into the consumer's `packages/`;
3. `teko build <consumer>` → binary;
4. run the binary, assert `exit == EXPECT_EXIT`.

The fixture ships WITHOUT a committed `packages/` (the harness provisions it and cleans
up); a committed `packages/` would be a stale binary blob in the tree.

### Fixture layout (NEW) — `examples/regressions/const_crossmodule_inline/`
```
const_crossmodule_inline/
  dep/            m1.tkp (kind=package, name=m1)  +  src/consts.tks
                    (pub const P: i64 = 20; exp const Q: i64 = 22)
  consumer/       const_crossmodule_inline.tkp (kind=binary, [dependencies] m1)
                    +  src/main.tks
  EXPECT_EXIT     42
  README-driver   (how the harness builds dep→packages→consumer)
```
- **Phase 1 spelling (lands with c8d, does NOT need c8c):** the consumer references the
  dep consts by BARE name (surfaced by `seed_dep_consts`) — `P + Q` → exit 42. Proves
  the full serialize→seed→inline→run chain on both engines.
- **Phase 2 spelling (added AFTER c8c merges):** switch the consumer to the qualified
  idiom `m1::P + m1::Q` → still exit 42. Proves the c8c namespace-value path end-to-end.

### Order dependency
- The harness capability + Phase-1 fixture is INDEPENDENT of c8b/c8c (bare spelling
  works on the tip) → can be built in parallel.
- Phase-2 (the `m1::P` spelling assertion) depends on **c8c**.
- c8d does NOT depend on c8e (different leg: c8d builds a positive dep fixture; c8e
  asserts negative fixtures).

### Gate
Wire `crossmodule_regressions.sh` into a REQUIRED leg (the same `gen1 checks` job that
runs `native_regressions.sh`, native.yml `:289-293`). Full ritual; both engines proven
by the driver running twice.

---

## Crumb c8e — assert `EXPECT_COMPILE_FAIL` in CI (finding 4, issue #610) — HARNESS, PARALLEL

`scripts/diff_vm_native.sh` (which once asserted rejection) was retired with the VM lane
(#524/#395); `native_regressions.sh` only smokes ONE positive fixture;
`sanitizers.yml:99,191` SKIPS every `EXPECT_COMPILE_FAIL` dir. ~30 negative fixtures
(incl. crumb-7/8 `member_const_*_rejected` and the new c8c visibility fixture) pass
green UNEXERCISED.

### Harness (NEW) — `scripts/compile_fail_regressions.sh`
Iterate `examples/regressions/*/`; for each dir containing `EXPECT_COMPILE_FAIL`:
- run `teko build <dir>` (both engines: `$TEKO` and `./bin/teko`);
- assert **exit ≠ 0** (build must FAIL);
- when the fixture ships an optional `EXPECT_COMPILE_FAIL` file with a non-empty marker
  line, assert the marker substring appears in stderr (per-fixture message pin — the
  file is already present at every negative fixture; today it is empty/sentinel, so the
  marker check is opt-in and back-compatible).

Failure to build (exit 0) or a missing marker = leg failure.

### Files
- NEW `scripts/compile_fail_regressions.sh`.
- `.github/workflows/native.yml` — add a REQUIRED step in `gen1 checks`
  (peer to `:289-293`) running it on gen1.
- Optionally seed markers into the crumb-7/8 negative fixtures' `EXPECT_COMPILE_FAIL`
  files (message pins) — additive, no fixture behavior change.

### Order dependency
INDEPENDENT of c8b/c8c/c8d — pure scripting over existing negative fixtures. It is the
leg that makes c8c's NEW visibility `EXPECT_COMPILE_FAIL` actually assert, so c8c's
rejection fixture is only PROVEN once c8e is merged (c8c can merge first; c8e
retroactively covers it).

### Gate
The script itself is the gate artifact; its first green run over all ~30 negative
fixtures is the acceptance. Full ritual on the sub-PR.

---

## Ordering, seriality, and dispatch

```
CORE (serial — both touch the inliner/typer resolution model):
   c8b  ──►  c8c
   (finding 1)  (finding 2)     [c8c emits the TVar that c8b's keyed inliner consumes]

HARNESS (parallel to CORE and to each other):
   c8d  (finding 3 — E2E + dep provisioning)   Phase-2 assertion waits on c8c
   c8e  (finding 4 — EXPECT_COMPILE_FAIL leg)   makes c8c's negative fixture assert
```

Prerequisite matrix:
- c8c ⟵ c8b (hard: shared resolution model).
- c8d Phase-2 spelling ⟵ c8c (fixture spelling only; Phase-1 is independent).
- c8c's negative fixture is only ASSERTED once c8e's leg exists (c8c may merge first).
- c8d and c8e are independent of each other and of the CORE crumbs' Phase-1 parts.

Recommended dispatch order (respecting one heavy build at a time, core serial):
1. **c8b** (finish PR #612) — the correctness bug; unblocks c8c. CORE.
2. **c8e** in parallel with c8b — pure harness, no core contention; immediately starts
   exercising the existing crumb-7/8 negative fixtures.
3. **c8d** (harness capability + Phase-1 fixture) in parallel — no core contention.
4. **c8c** — after c8b merges. CORE.
5. **c8d Phase-2** — flip the consumer fixture to `m1::P` after c8c merges (tiny follow
   commit on the c8d fixture); **c8c's negative fixture now asserted by c8e**.
6. Re-confirm all four legs green on `remodel/constants` → **SEED BUMP #1**.

## Risks / law tensions
- **R1 — namespace-identity collision (dep ns == project ns == the same string, incl.
  `""`):** `(name, ns)` keying cannot disambiguate two consts sharing name AND ns. This
  requires a dep packed with the project's exact namespace (or both empty) — a
  pre-existing module-identity problem the whole compiler already assumes away (one
  namespace = one module). Recommended resolution: OUT OF SCOPE for c8b; a same-ns
  cross-module duplicate is a distinct diagnostic (module-identity), not a const bug. A
  real `.tkl` always carries its module path as namespace (`dep_item_ns`), so a project
  without a namespace decl (`""`) still keys distinctly from a dep. NOTE, do not fix
  here.
- **R2 — `func_ns` reliability:** the fix rests on `type_var` stamping `func_ns = b.ns`
  for a const (`typer.tks:51`, unconditional). Verified present at `dcd5fb5`. If any
  future path synthesizes a const-reference `TVar` with `func_ns = ""` while the const's
  map namespace is non-empty, the substitution would MISS (a compile error, not a silent
  miscompile — safe-fail). c8b's unit test pins the happy path; add an assertion that a
  same-ns bare ref still inlines (guards regressions of the stamp).
- **R3 — harness dep-provisioning cleanup:** c8d writes into a fixture's `packages/` at
  build time; the driver MUST clean it (and never commit it) so the working tree stays
  clean and fixpoint/byte-identity is unaffected. Design the driver to build the dep
  into a `mktemp -d` and copy in, `rm -rf` on exit (mirror `native_regressions.sh`'s
  tmpdir discipline).

## HALT (genuine design tensions requiring the owner)
NONE. All four findings resolve law-first (smallest safe step, passes-all-laws,
backward-compatible, fixpoint-preserving). The one product-taste call — c8c generalizing
to fn-values — is resolved by "issues são 100%" (#613 names fn-values explicitly) at zero
extra machinery. No lei-vs-lei tension remains; no deferral.
