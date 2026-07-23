# Wave 0.3.1 — "Onda de Segurança" — impact-ordered sub-wave decomposition + seed chain

> **Status:** DESIGN-AHEAD, doc-only. Owner-ordered 2026-07-13. Decomposes the (large) 0.3.1 wave
> into IMPACT-ordered sub-waves where **each sub-wave merge cuts an intermediate seed
> (`0.3.1.N-beta`) that dogfoods the next** (the D36 per-merge seed chain applied to a full wave).
> Owner mandate: *"essa será bem grande e precisa de subdivisões por itens de maior impacto gerando
> versões para seed da próxima."*
>
> **Sources (read for this plan):** `TEKO_MASTER_PLAN.md` WAVE 0.3 ROADMAP + header wave rulings;
> `docs/design/ref-transparent-model.md` (2026-07-13 rulings — tripartite law, mandatory `ref`
> modifier, `ref` grafia in every type position with `Ref<>` compiler-internal + depth cap 2,
> never-null refs, `Ref<T?>` transparency + no-narrowing, cases A–F); `docs/design/marshall-spec.md`
> (operand law, wrapper law, arrays §5.7, crumbs C0–C8); `docs/design/safety-spine.md` (PR #507,
> SP-0..SP-6); `docs/design/vm-retirement.md` (context); `DECISION_LOG.md` **D36** (seed chain),
> **D33** (metaprogramming post-1.0).
>
> **This plan writes NO product code and does NOT touch `teko.tkp`.** It is the sequencing contract;
> the per-item type signatures / bodies live in the source design docs cited per sub-wave.

---

## 0. Reconciliation with the master-plan 0.3.1 / 0.3.2 split (REPORTED, not a HALT)

The `TEKO_MASTER_PLAN.md` WAVE 0.3 ROADMAP section currently splits the line into **0.3.1
(Safety spine)** and **0.3.2 (Perf & Tooling)**, listing `#453/#476/#475/#479/#480/#456/#442/#471/
#472/#473/#477/#461` as *"Moved from 0.3.1"* into 0.3.2. The owner's 2026-07-13 ordering (this task)
**re-absorbs those items as the LATER sub-waves of a single, large 0.3.1** and puts the resource /
runtime / threading infrastructure **first** (as the wave opener). These are not in conflict: the
same items ship, in the same relative order; the only open question is the **version label** on the
perf/tooling sub-waves.

**Recommendation (law-first, no HALT):** keep the whole body under the `0.3.1.N-beta` seed chain (one
uninterrupted self-hosting corpus is simpler and matches the owner's "one big wave" framing); if the
owner later wants a hard 0.3.1/0.3.2 boundary, cut it cleanly **after SW8 (Marshall)** — that is the
natural safety/perf seam (SW1–SW8 = safety+infra, SW9–SW13 = debts+perf+tooling). Either labelling
delivers identical work in identical order. **REPORTED for the owner; the plan below is label-agnostic.**

---

## 1. The seed-chain mechanism (D36 applied to a full wave)

Per **D36**, every merge into the umbrella publishes a compiler seed and the next merge dogfoods it.
Applied here, **each sub-wave merge bumps `0.3.1.N` and cuts `0.3.1.N-beta`**; the next sub-wave's CI
seeds from that beta (`ci_provision` beta-channel). The build of tag `0.3.1.N-beta` itself resolves
"newest beta" to `N-1` — the self-hosting chain. The **stable-seed lane discipline** is the binding
constraint on ordering:

> **No sub-wave may use, in `src/`, a surface the previous seed cannot parse.**

Two consequences drive the whole layout:

1. **Additive-then-adopt.** A new *grammar* surface (`ref` grafia, `#arena_size`, grouped attributes,
   postfix `!`, `_` discard, flags) is introduced **additively** in sub-wave `N` (parser+checker
   ACCEPT it; `src/` does NOT use it yet, so the `N-1` seed still builds gen1). The seed cut at `N`
   now carries the surface. Only sub-wave `≥ N+1` may ADOPT it in `src/`.
2. **The `ref` grafia is the linchpin.** Because the compiler's own `src/iter`+`src/io` (28 closures)
   and pipeline accumulators MUST migrate to the new spelling, the grafia-accept seed (SW3) has to
   exist **before** the grafia-adoption sub-wave (SW4). This is the single most delicate seed hop in
   the wave — see §5 (Critical path) and §7 (Risks).

Ritual vocabulary used below:
- **Full gate** = C-gate + self-host + native + **fixpoint gen1==gen2** + `diff_vm_native` +
  100%-new-code coverage + independent review. **Every sub-wave merge is a full gate** (it cuts a
  seed — a bad seed strands the chain).
- **Fast-path** = a low-exposure sub-PR *inside* a sub-wave whose fixpoint holds trivially (pure
  query, no corpus exposure); it still rides the sub-wave's merge gate but needs no dedicated ritual.
- **Seed-cut moment** = the umbrella merge of the sub-wave.

---

## 2. Sub-wave list (IMPACT-ordered) — one line each

| SW | Seed cut | Body (impact rationale) | Scope |
|----|----------|-------------------------|-------|
| **SW1** | `0.3.1.1-beta` | **Resource & Arena Foundation** — establishes the resource/arena vocabulary every later sizing + container-aware decision consumes | #453 `#arena_size(N)` + `[resources]` manifest (`max_memory`/`max_cpu` u8 %, container-aware effective RAM) |
| **SW2** | `0.3.1.2-beta` | **Thread-Safe Runtime & Parallel Front-End** — unblocks parallelism that ACCELERATES every later gate (esp. the large spine self-hosts) | G8 thread-safe `teko_rt` → #456 `teko::threading`+`teko::sync` (container-aware CPU, affinity `error?`) → triggers #449 front-end multi-threading |
| **SW3** | `0.3.1.3-beta` | **Ref Grafia Surface + Auto-Deref Soundness (SP-0 pt1)** — THE LINCHPIN SEED: the new spelling becomes parseable before any `src/` adopts it | §10.4 auto-deref adversarial soundness workflow FIRST → `ref` keyword + grafia in every type position + `Ref<>` compiler-internal + depth cap 2 + never-null + `.value` deprecated-but-accepted alias |
| **SW4** | `0.3.1.4-beta` | **Ref Grafia Adoption + `-> void` Removal (SP-0 pt2 + #497)** — dogfoods SW3's seed; the compiler now self-hosts on the new spelling exclusively | migrate `src/` `.value`→transparent + `Ref<T>`→`ref` (28 iter/io closures + ~10 accumulators); remove `.value` acceptance; #497 drop `-> void` (57 sites) |
| **SW5** | `0.3.1.5-beta` | **Spine Facts + Local Guards (SP-1 + SP-2)** — low-exposure groundwork the keystone needs | SP-1 `type_reaches_ref` + `BorrowSummary` (PURE) · SP-2 A4 definite-assignment + A3 lvalue/rvalue |
| **SW6** | `0.3.1.6-beta` | **Transitive Escape Gate (SP-3 / A1) — KEYSTONE** — "the whole safety bet"; corpus self-host is the load-bearing proof | A1-detect (type-directed transitive reachability) + A1-interproc (param-root borrow summary); + #530 WASI×arena×`#singleton` design seed (doc) |
| **SW7** | `0.3.1.7-beta` | **Spine Completion (SP-4 + SP-5 + SP-6)** — tightens the accepted language to the final safe set | A2 borrow-down non-escaping · A5 composite-path all-mut + R10 · A6 free-ownership + DI-monotonic |
| **SW8** | `0.3.1.8-beta` | **Marshall (C0–C8)** — completes the `ptr`↔`ref` unsafe boundary (the spine's sanctioned escape hatch) | C0 unsafe-gate `Ptr`/`Uptr` · C1 module+bridge · C2 raw-ptr ops · C3 `swap` · C4 `wrap` · C5 `unwrap` · C6 FFI cookbook · C7 slice marshalling · C8 paranoid membership |
| **SW9** | `0.3.1.9-beta` | **Correctness Debts** — removes latent bugs blocking #395 close; largely independent → high parallelism | #501 · #502 · #503 · #519 · #412 · #495 · #301 · #283 |
| **SW10** | `0.3.1.10-beta` | **Ruled Surface Set** — new user-facing surface (all additive) | #525 postfix `!` + diverging `??` · #526 own-backend `_ as x`/for-each · #527 `_` discard-only · #517 ref-for-each follow-on · #532 flags bitwise + masked `~` + `~` position rule · #477 grouped attributes · #461 static slices |
| **SW11** | `0.3.1.11-beta` | **Test Infrastructure Cluster** — implemented together per the shared subprocess model | #442 glob/single selector · #471 `#test_panic`/`#test_exit` · #472 parallel + `#serial_group` · #473 `teko::process` (`run_captured`/`spawn`/`wait`/`run_pool`) |
| **SW12** | `0.3.1.12-beta` | **Profiling, Coverage & PGO** — perf tooling built on SW1's arena vocab | #475 coverage-as-disk-signal · #479 `teko profile` · #480 PGO arena pre-sizing |
| **SW13** | `0.3.1.13-beta` | **Backend Completion Debts** — closes #395 → 0.3.1 GA | #222 i128 register-pair · FPR-spill · has_back_edge (#443 remnants) |

The last seed (`0.3.1.13-beta`) is promoted to the **`0.3.1.0` release**; **#395 stays OPEN until SW13
is 100%** (the backend epic is a patch of the 0.3 line, per the header ruling).

---

## 3. Per-sub-wave detail

Sizes: **S** = ≤ ~1 day / single file; **M** = a few files, one subsystem; **L** = cross-cutting /
checker-core / multi-engine.

### SW1 — Resource & Arena Foundation → `0.3.1.1-beta`

**Impact:** first because the `[resources]` numbers (`max_cpu` → thread-pool sizing; container-aware
effective RAM → arena sizing) are the inputs G8/#456 (SW2) and #479/#480 (SW12) consume; landing the
vocabulary once avoids re-plumbing it later.

| Crumb | Size | Touches | Notes |
|-------|------|---------|-------|
| 1.1 `[resources]` manifest section — parse `max_memory`/`max_cpu` as `u8` percents; container-aware effective RAM/CPU resolution (cgroup-aware host query in `teko_rt`) | M | manifest parser (`project.tks`), `teko_rt.{c,h}` (maintained-C, cgroup read) | additive; validate 0–100 range → `error` on out-of-range |
| 1.2 `#arena_size(N)` attribute — parse + checker (non-`unsafe`, per-function incl. child scopes) | M | lexer (attr already `#`-family), `checker`, `codegen` (arena pre-size at region open) | additive surface; `src/` does NOT adopt yet (kept additive so the `0.2.x`/`0.3.0` seed still builds gen1) |
| 1.3 fixtures | S | `examples/regressions/` | see below |

**Parallelizes:** 1.1 ⟂ 1.2 (manifest vs attribute — disjoint files). 1.3 after both.
**Ritual:** full gate at merge (seed cut).
**Fixtures owed** (input → exit, VM & native):
- `arena_size_directive_ok` — a fn with `#arena_size(4096)` allocating < N → RUN, exit = f(payload), **VM==native**.
- `resources_manifest_parsed` — `.tkp` with `[resources] max_memory=50 max_cpu=25` builds + a probe prints the resolved effective bytes → RUN.
- `resources_percent_out_of_range` — `max_cpu=250` → **EXPECT_COMPILE_FAIL** (u8 %, 0–100).
- `arena_size_on_unsafe_rejected` — `#arena_size` on an `unsafe`-only path → **EXPECT_COMPILE_FAIL** (directive is non-unsafe by ruling).

**Seed-cut moment:** merge of SW1 → `0.3.1.1-beta` carries the resource/arena vocabulary.

---

### SW2 — Thread-Safe Runtime & Parallel Front-End → `0.3.1.2-beta`

**Impact:** the thread-safe runtime is the precondition for both the threading library AND the
parallel front-end; #449 then makes self-host faster, which pays back across the LARGE spine
sub-waves (SW3–SW7). This is why concurrency infra precedes the spine.

| Crumb | Size | Touches | Notes |
|-------|------|---------|-------|
| 2.1 **G8** — thread-safe `teko_rt`: reentrant allocator/region ops, no shared mutable global without a lock/atomic | L | `teko_rt.{c,h}` (maintained-C exception) | precondition for 2.2/2.3; audit `tk_alloc`/`tk_region_*`/`tk_set_args` for TLS/atomics |
| 2.2 **#456** `teko::threading` + `teko::sync` — container-aware CPU count (from SW1 `max_cpu`), `spawn`/`join`, mutex/once; affinity failure surfaces as `error?` | M | new `src/threading/*.tks`, `src/sync/*.tks`, `teko_rt` FFI seam | affinity `error?` (never panic) per ruling |
| 2.3 **#449** front-end multi-threading — parallelize per-file parse/check where the shared-checker discipline (#330) allows | L | `driver.tks`, `checker` (must stay deterministic + fixpoint-stable) | internal; NO new surface |
| 2.4 fixtures | S | regressions + `teko test` | |

**Parallelizes:** 2.2 ⟂ 2.3 once 2.1 lands (both depend on G8). 2.1 is the gate.
**Ritual:** full gate at merge; **extra**: run the fixpoint gate under the parallel front-end (2.3) to
prove determinism (gen1==gen2 must hold with threads on).
**Fixtures owed:**
- `threading_spawn_join` — spawn N workers summing a shared-via-`sync` counter → RUN, deterministic exit, **VM==native** (VM may serialize; result identical).
- `sync_mutex_once` — `once` runs body exactly once under contention → RUN.
- `threading_affinity_error` — request an invalid core → returns `error`, handled → RUN (no panic).
- `parallel_frontend_determinism` — build the corpus with `TEKO_FRONTEND_THREADS>1` → identical `.tkb` bytes to the serial build (fixpoint).

**Seed-cut moment:** merge → `0.3.1.2-beta` carries thread-safe runtime + threading; later sub-waves'
CI can now parallelize.

**Risk flag:** the parallel front-end (2.3) MUST NOT perturb `.tkb` byte-output or the fixpoint gate
breaks and the entire downstream seed chain stalls. Keep 2.3 behind an env flag defaulted OFF until
`parallel_frontend_determinism` is green on all platforms; the seed itself may build serially.

---

### SW3 — Ref Grafia Surface + Auto-Deref Soundness (SP-0 pt1) → `0.3.1.3-beta` — **THE LINCHPIN SEED**

**Impact:** this seed is the one every subsequent spine sub-wave and the `src/` migration depend on.
It must ship the new spelling as ACCEPTED (parser+checker) while keeping the old `.value` path alive,
so SW4 can migrate `src/` incrementally under fixpoint+diff. **The §10.4 auto-deref adversarial
soundness workflow runs FIRST — before the checker cements the peeling rule** (ref-model open case
#4/#5; safety-spine R4).

| Crumb | Size | Touches | Notes |
|-------|------|---------|-------|
| 3.0 **§10.4 auto-deref soundness workflow** — mini soundness pass on type-directed peeling; ratify the peeling rule + the depth-cap-2 opening; produce the pass-or-narrow verdict BEFORE any surface cements | L (design/verify) | design artifact (adversarial agents) + a checker unit harness | **gates 3.2**; if it fails, narrow the opening (honest-stop) rather than cement an unsound peel |
| 3.1 `ref` contextual keyword + `BindKind::Ref` | M | lexer, parser, `ast` | additive |
| 3.2 transparent type-directed auto-deref (§4) + `ref` grafia in EVERY type position (fields, generic args, slices, returns, nesting) + `Ref<>` becomes compiler-internal name for `Reference{T}` | L | `checker/resolve.tks`, `checker/expr.tks`, `typer` | gated by 3.0; peel EXACTLY the layers the use-site type demands |
| 3.3 depth cap 2 (consecutive `ref` levels; containers reset the chain; safe-world only) + never-null (`Ref<…>?` is a compile error everywhere; `Ref<T?>` transparency, no flow-narrowing through `ref`) | M | `checker/resolve.tks` (relax the old `Ref<Ref<T>>` reject to KEEP-only-value-model cases; add cap + null rules) | ref-model §3/§4.2 |
| 3.4 `.value`-on-`Reference` kept as **deprecated-but-accepted** alias (parses old + new) | S | `checker`, `codegen` | the stable-seed dance step 1 — DO NOT remove yet |
| 3.5 fixtures | S | regressions | negatives compile-time; positives VM==native |

**Parallelizes:** 3.1 ⟂ (3.3 null/cap rules can be drafted in parallel) but 3.2 depends on 3.0's
verdict and 3.1. 3.4 last.
**Ritual:** **full gate** (surface change touches parser+checker, shared) + the stable-seed dance:
ship the seed with BOTH spellings accepted; `src/` unchanged (additive → the `0.3.1.2-beta` seed still
builds gen1).
**Type/interface contract (implementer copies verbatim; full Javadoc):**

```teko
/**
 * Peels exactly the number of `Reference` layers the expected type at a use-site
 * demands (type-directed auto-deref, ref-model §4). NEVER peels blindly to the leaf;
 * against a free generic `U` it peels zero and passes the whole `Reference`.
 *
 * @param expr     the checked expression node carrying its inferred `Reference{..}` type
 * @param expected the type the use-site imposes (may itself be `Reference{..}` or a free var)
 * @return the peeled expression node retyped to `expected`, or `error` when the demanded
 *         depth exceeds the value's ref-depth or violates the depth-cap-2 / never-null rules
 * @throws returns `error` (never panics) so the caller reports a checker diagnostic
 * @see docs/design/ref-transparent-model.md §4, §4.2 (null transparency), §3 (depth cap 2)
 * @since 0.3.1.3-beta
 */
fn autoderef_to(expr: Expr, expected: Type) -> Expr | error
```

**Fixtures owed:**
- `ref_grafia_field_ok` — `struct H { r: ref int }` used transparently → RUN, VM==native.
- `ref_depth_cap2_ok` — `ref x: ref T` (depth 2) → RUN; `ref_depth_cap3_rejected` — `ref x: ref ref T` → **EXPECT_COMPILE_FAIL** ("exceeds cap").
- `ref_optional_transparency_ok` — `ref x: T? = null`; `?.`/`??`/`match` peel one level → RUN, VM==native.
- `ref_of_optional_ref_rejected` — `Ref<T>?` in any position → **EXPECT_COMPILE_FAIL** ("uma referência sempre existe...").
- `ref_value_deprecated_alias_ok` — `.value` on a `Reference` still parses (deprecation, not removal) → RUN.
- `autoderef_free_generic_no_peel` — `f<U>(x)` with `x: Ref<Ref<T>>` passes the whole ref → RUN, VM==native.

**Seed-cut moment:** merge → `0.3.1.3-beta` — the new spelling is now parseable by the seed. **This is
the seed SW4 dogfoods.**

---

### SW4 — Ref Grafia Adoption + `-> void` Removal (SP-0 pt2 + #497) → `0.3.1.4-beta`

**Impact:** the compiler now self-hosts on the transparent model exclusively — the ergonomic payoff
and the precondition for the spine facts (SP-1 consumes `ref`-typed src). Highest stranding risk in
the wave (see §7).

| Crumb | Size | Touches | Notes |
|-------|------|---------|-------|
| 4.1 migrate `src/` `.value`-on-`Ref` → transparent; `Ref<T>` binding → `ref` modifier where it is a borrow-down (the 28 `src/iter`+`src/io` closures + ~10 pipeline accumulators) | L | `src/iter/*`, `src/io/*`, checker helpers | fixpoint + `diff_vm_native` at EACH file; if a file's pattern breaks, keep `.value` for that file (honest-stop) and resolve before removing the alias |
| 4.2 adopt `ref` grafia in type positions across `src/` (fields, returns, slices) where already a reference | M | `src/**` | mechanical; guided by 3.2 |
| 4.3 remove deprecated `.value`-on-`Reference` acceptance (stable-seed dance step 3) | S | `checker`, `codegen` | ONLY after 4.1/4.2 green |
| 4.4 **#497** drop `-> void` (57 sites; internal `Void` type stays; `fn` w/o `-> T` returns nothing) | M | parser, checker, codegen, VM, `src/**` (57), `.tkt` (2) | independent-of-spine, mechanical; safe (the seed already parses fn-without-return) |
| 4.5 fixtures | S | regressions | |

**Parallelizes:** 4.4 (#497) ⟂ 4.1/4.2 (disjoint). 4.3 strictly after 4.1+4.2.
**Ritual:** **full gate** — self-host is the critical proof; the corpus must build clean on `ref`
spelling with the `.value` path removed.
**Fixtures owed:**
- `void_return_removed_ok` — `fn f() { ... }` (no `-> void`) → RUN, VM==native.
- `iter_closure_ref_selfhost` — the `over_array`/`range` idiom compiles under the new spelling → RUN.
- (regression) existing `optionals` (exit 6) + `match_pattern_bindings` (exit 5) unchanged, VM==native.

**Seed-cut moment:** merge → `0.3.1.4-beta` — compiler self-hosts on the transparent model; `.value`
gone.

---

### SW5 — Spine Facts + Local Guards (SP-1 + SP-2) → `0.3.1.5-beta`

**Impact:** low-exposure groundwork the keystone needs; ships the two PURE facts (consulted nowhere)
+ the two cheap local guards. Do the pure facts FIRST so they can be dumped and inspected with zero
risk (safety-spine C-recommendation).

| Crumb | Size | Touches | Notes |
|-------|------|---------|-------|
| 5.1 **SP-1a** `type_reaches_ref` (transitive, terminating; NOT a points-to graph) | M | `src/checker/spine.tks` | PURE query, consulted nowhere yet |
| 5.2 **SP-1b** `BorrowSummary` (one-hop `return-borrows-params` bitmask; widen-on-recursion) | M | `spine.tks` | PURE; **fast-path** (fixpoint trivially holds) |
| 5.3 **SP-2 / A4** `ref x: T` requires init (definite-assignment) | S | `checker` | closes uninit-Ref |
| 5.4 **SP-2 / A3** lvalue → param `ref` = R7 alias; rvalue/temporary → param `ref` = R5 copy | M | `checker/expr.tks` | closes genuine-escape/copy-alias seam |
| 5.5 fixtures | S | regressions | |

**Parallelizes:** 5.1 ⟂ 5.2 (independent facts); 5.3 ⟂ 5.4 (independent guards). All four ⟂ each other.
**Ritual:** full gate at merge; SP-1a/1b are the fast-path (no corpus exposure — nothing consumes the
facts yet).
**Fixtures owed:**
- `ref_uninit_rejected` — `ref x: T` with no initializer → **EXPECT_COMPILE_FAIL** (A4).
- `ref_param_rvalue_copies` — passing a temporary to a `ref` param mutates a callee-local copy → RUN, VM==native.
- `ref_param_lvalue_aliases` — passing a live `mut` lvalue write-through mutates the caller → RUN, VM==native.

**Seed-cut moment:** `0.3.1.5-beta` — facts present, guards active; the accepted language narrows only
on uninit-Ref + rvalue/lvalue.

---

### SW6 — Transitive Escape Gate (SP-3 / A1) — **KEYSTONE** → `0.3.1.6-beta`

**Impact:** THE whole safety bet. Closes the single root soundness hole (a `Ref` hidden inside a
returned aggregate/collection/closure). **Corpus self-host is the load-bearing proof** — this is where
the theory survives or reveals the iterator collision (safety-spine R1, CERTAIN/HIGH). Single most
important ritual point in the wave.

| Crumb | Size | Touches | Notes |
|-------|------|---------|-------|
| 6.1 **A1-detect** — type-directed transitive reachability: mark a store/return that carries a reachable `Ref` across an arena boundary | L | `spine.tks` (additive helper), `escape.tks` reads it (byte-identical `fn_escaping_vars`) | consumes 5.1 `type_reaches_ref` |
| 6.2 **A1-interproc** — caller-side `BorrowSummary`: admit reachable-Ref rooted at a PARAM; reject rooted at a LOCAL, AT THE CALLER | L | `spine.tks`, `checker/typer` | consumes 5.2; **resolves R1** (see §7) |
| 6.3 **#530 WASI×arena×`#singleton` design seed** — doc-only design-ahead: map program-root arena (`#singleton`) onto WASI linear memory using the arena-addressing vocabulary this sub-wave cements | S (doc) | `docs/design/` | NO code; attaches here because A1 cements the arena-escape/addressing model (alt: SW3) |
| 6.4 fixtures | M | regressions | the soundness proof set |

**Parallelizes:** 6.1 ⟂ 6.3 (doc); 6.2 depends on 6.1. Keep the pair as sequential sub-PRs.
**Ritual:** **full gate + `corpus must self-host`** — the load-bearing gate. Independent review
mandatory. If self-host breaks on a legitimate combinator, apply the R1 resolution (param-root
refinement), NOT a blanket reject.
**Type/interface contract (the crown refinement — full Javadoc):**

```teko
/**
 * Admits a closure/aggregate/collection whose transitively-reachable `Ref` roots at a
 * PARAMETER (sound: the caller owns the storage), and rejects one that roots at a LOCAL
 * (unsound: the local's arena dies with the callee). Replaces the blanket "reject any
 * capture whose type contains a Ref" — that over-rejection breaks the 28 `src/iter`+`src/io`
 * iterator closures during self-host (safety-spine R1).
 *
 * @param place   the returned/stored access-path whose reachable Ref set is being judged
 * @param roots   the borrow-summary roots for `place` (param-rooted vs local-rooted)
 * @return `ok` when every reachable Ref roots at a live-past-the-frame param; otherwise
 *         `error` carrying the A1-interproc diagnostic (which local, which edge)
 * @throws returns `error` (never panics); the checker turns it into a compile diagnostic
 * @see docs/design/safety-spine.md §C1 R1 (the iterator-idiom resolution), §B2.1
 * @see docs/design/ref-transparent-model.md §8 A1, §7 (the single root hole)
 * @since 0.3.1.6-beta
 */
fn a1_escape_ok(place: AccessPath, roots: BorrowRoots) -> Ok | error
```

**Fixtures owed (the soundness proof set):**
- `ref_in_struct_escape_rejected` — `struct Holder { r: ref int }` returned with a local-rooted ref → **EXPECT_COMPILE_FAIL** (the §7 UAF).
- `ref_in_returned_slice_rejected` — `[]ref T` whose elements root at a local → **EXPECT_COMPILE_FAIL**.
- `closure_captures_escaping_local_ref_rejected` — closure capturing a local `ref` that escapes → **EXPECT_COMPILE_FAIL** (with the A1-interproc diagnostic).
- `iter_closure_param_root_ok` — the `over_array` param-rooted capture → RUN, VM==native (proves the corpus idiom survives).
- (gate) **corpus self-hosts**; fixpoint gen1==gen2.

**Seed-cut moment:** `0.3.1.6-beta` — the transitive gate is live; the safety thesis is proven on the
corpus.

---

### SW7 — Spine Completion (SP-4 + SP-5 + SP-6) → `0.3.1.7-beta`

**Impact:** brings the accepted language to its final safe set; each amendment tightens/relaxes shared
checker state (shared-checker discipline per #330 — each is its own ritual sub-PR).

| Crumb | Size | Touches | Notes |
|-------|------|---------|-------|
| 7.1 **SP-4 / A2** borrow-down is NON-escaping (a borrow-down `ref` flows only down; storing it up = error) | M | `spine.tks`, `checker` | closes collection-store, closure-two-hop |
| 7.2 **SP-5 / A5** composite path all-mut + R10 (borrow-elegible only if every path segment is `mut`; write-through via borrowed ref is external write) | M | `checker/expr.tks` | closes referenceable-path |
| 7.3 **SP-6 / A6** free-ownership (`mem::free` via non-owning `ref` = illegal) + DI-monotonic (`#wire` lifetime monotonicity, or desugar-before-escape) | L | `checker`, `#wire` desugar | **prefer desugar-before-escape** so A6-DI is subsumed by A1 (less new code) |
| 7.4 fixtures | M | regressions | |

**Parallelizes:** 7.1 ⟂ 7.2 (different guards); 7.3 after (it may reuse A1 via desugar).
**Ritual:** full gate; each of SP-4/5/6 is a shared-checker discipline ritual sub-PR.
**Fixtures owed:**
- `borrow_down_stored_up_rejected` (A2) → **EXPECT_COMPILE_FAIL**.
- `borrow_path_let_segment_rejected` (A5) → **EXPECT_COMPILE_FAIL**.
- `free_borrowed_ref_rejected` (A6) → **EXPECT_COMPILE_FAIL**; `free_owned_unique_ok` — the existing `mem_free` regression (exit 67) still passes.
- `di_singleton_holds_scoped_rejected` (A6-DI) → **EXPECT_COMPILE_FAIL**; `di_monotonic_wire_ok` → RUN.

**Seed-cut moment:** `0.3.1.7-beta` — the spine is complete.
**Risk flag:** **A6 has ZERO corpus/dogfood exposure** (the compiler calls no `mem::free`, uses no
`#wire`) — its fixtures are its ONLY proof; call this out in review (safety-spine B2.2).

---

### SW8 — Marshall (C0–C8) → `0.3.1.8-beta`

**Impact:** completes the `ptr`↔`ref` unsafe boundary — the spine's sanctioned escape hatch. Placed
AFTER the transparent-ref core so ALL crumbs (incl. the previously-BLOCKED C4/C5/C7) use the final
model; no double migration. Signatures/bodies: `docs/design/marshall-spec.md`.

| Crumb | Size | Touches | Notes | Marshall status |
|-------|------|---------|-------|-----------------|
| C0 unsafe-gate `Ptr`/`Uptr` + migrate `teko::mem` FFI prims to `unsafe fn` | M | `checker/resolve.tks` `unsafe_carrying_at` | **Ritual: full gate** (touches the containment gate) | was UNBLOCKED |
| C1 `teko::marshall` module + `null`/`is_null`/`to_uptr`/`from_uptr` | S | new `src/marshall/marshall.tks` | | was UNBLOCKED |
| C2 raw-ptr operators `*`/`&`/`->`/`+`/`-`/`[]` gated to unsafe | M | checker, parser (`->` desugar) | | was UNBLOCKED |
| C3 `swap<T>(ref a: T, ref b: T)` — SAFE value exchange | S | `src/marshall` | **Ritual: full gate** (first SAFE member) | was UNBLOCKED |
| C4 `wrap<T>` — static check + null-panic + reinterpret | M | `src/marshall`, checker | | needed transparent ref (now available) |
| C5 `unwrap<T>` | M | `src/marshall`, checker | **Ritual: full gate** (ptr↔Ref boundary complete) | needed transparent ref |
| C6 FFI cookbook fixtures | S | regressions | | UNBLOCKED (8.3 rides C4) |
| C7 slice marshalling `wrap_slice`/`unwrap_slice` (§5.7) | M | `src/marshall`, codegen | | needed transparent ref |
| C8 paranoid region-membership best-effort (`TEKO_MEM_PARANOID` only) | S | `teko_rt` | never on release path | UNBLOCKED, optional |

**Parallelizes:** C0→(C1,C2 after C0); C3 after C1; C4→C5→C7 chain; C6 after C0/C4; C8 independent.
**Ritual:** full gate at C0, C3, C5 (per marshall-spec); the sub-wave merge is itself a full gate.
**Fixtures owed** (from marshall-spec §): `marshall_ptr_in_safe_fn_rejected`,
`marshall_ptr_field_in_safe_struct_rejected`, `marshall_uptr_in_safe_rejected`,
`marshall_ptr_optional_rejected` (all COMPILE_FAIL) · `marshall_uptr_roundtrip` (RUN) ·
`marshall_ptr_arith_index` (RUN, native) / `marshall_ptr_arith_in_safe_rejected` (COMPILE_FAIL) ·
`marshall_swap_values` (RUN, **VM+native**) / `marshall_swap_on_let_rejected` /
`marshall_swap_on_mut_value_rejected` (COMPILE_FAIL) · `marshall_wrap_unwrap_roundtrip`,
`marshall_wrap_null_panics`, `marshall_wrap_type_mismatch_rejected`, `marshall_unwrap_in_safe_rejected` ·
`marshall_ffi_cstr_roundtrip`, buffer-fill · `marshall_slice_wrap` (native-only).

**Seed-cut moment:** `0.3.1.8-beta` — the unsafe boundary is complete. **This is the natural
safety/perf seam if the owner wants a 0.3.1|0.3.2 label boundary (§0).**

---

### SW9 — Correctness Debts → `0.3.1.9-beta`

**Impact:** removes latent bugs blocking the #395 close; the items are largely independent → the most
parallelizable sub-wave.

| Crumb | Size | Item | Notes |
|-------|------|------|-------|
| 9.1 | S | **#501** literal int > i64::MAX into unsigned casts (checker+codegen parity, both engines) | |
| 9.2 | S | **#502** extern params accept `struct`/`enum` by value (FFI completeness, C7.1a carve-out) | |
| 9.3 | S | **#503** `ptr<void>` in params emitting invalid C | couples to SW8 Marshall ptr surface — sequence after C0 |
| 9.4 | S–M | **#519** | correctness debt (per task scope) |
| 9.5 | S–M | **#412** | |
| 9.6 | S–M | **#495** | |
| 9.7 | S–M | **#301** | |
| 9.8 | S–M | **#283** | |

**Parallelizes:** all eight are independent bug-fixes → fan out; only #503 has a soft edge to SW8.
**Ritual:** full gate at merge; each fix ships its own before/after regression (VM==native).
**Fixtures owed:** one differential regression per item (input that reproduces → expected exit /
COMPILE_FAIL, VM==native), e.g. `int_over_i64max_unsigned_cast`, `extern_struct_byval`,
`ptr_void_param_valid_c`, plus one named fixture per #519/#412/#495/#301/#283 mirroring its report.
**Seed-cut moment:** `0.3.1.9-beta`.

---

### SW10 — Ruled Surface Set → `0.3.1.10-beta`

**Impact:** new user-facing surface. All additive (introduce-accept only; `src/` need not adopt) →
one seed at merge. Sequence-sensitive only in that each new grammar must be additive before any later
`src/` adoption.

| Crumb | Size | Item | Notes |
|-------|------|------|-------|
| 10.1 | M | **#525** postfix `!` + diverging `??` | new operator surface |
| 10.2 | M | **#526** own-backend `_ as x` / for-each | depends on own AOT backend (0.3.0) |
| 10.3 | S | **#527** `_` discard-only | narrows `_` semantics |
| 10.4 | M | **#517** ref-for-each follow-on | depends on the ref model (SW4+) and #526 for-each |
| 10.5 | M | **#532** flags bitwise + masked `~` + the `~` string/numeric position rule | |
| 10.6 | S | **#477** grouped attributes `#[a, b(1,2)]` — pure desugar into stacked decorators | |
| 10.7 | M | **#461** static slice initialization — literal/const-rooted push-chains to rodata | |

**Parallelizes:** mostly independent; 10.4 (#517) after 10.2 (#526 for-each). 10.6 (#477) is pure
desugar (S).
**Ritual:** full gate; each new surface ships a differential fixture (VM==native) + a negative.
**Fixtures owed:** `postfix_bang_diverging_qq` (RUN + a COMPILE_FAIL for misuse) · `foreach_backend_ok`
(RUN, native) · `discard_underscore_only` (`_` read → COMPILE_FAIL) · `ref_foreach_ok` (RUN,
VM==native) · `flags_bitwise_masked_not` (RUN) · `grouped_attrs_desugar` (RUN, equals stacked form) ·
`static_slice_rodata` (RUN, native).
**Seed-cut moment:** `0.3.1.10-beta`.

---

### SW11 — Test Infrastructure Cluster → `0.3.1.11-beta`

**Impact:** #442/#471/#472/#473 implemented TOGETHER (shared subprocess model). Depends on SW2 (G8 +
threading) for parallelism and process surface.

| Crumb | Size | Item | Notes |
|-------|------|------|-------|
| 11.1 | M | **#473** `teko::process` expanded — `run_captured`/`spawn`/`wait`/`run_pool` | the subprocess substrate the others share; anticipates 0.4 process surface |
| 11.2 | M | **#442** glob selector / single test or test-scope selector (VM+native gate) | on 11.1 |
| 11.3 | S | **#471** `#test_panic("msg")` + `#test_exit(N)` — assert divergence without interrupting the run | on 11.1 |
| 11.4 | M | **#472** parallel tests by default (per-process) + `#serial_group("name")` | on 11.1 + SW2 threading |

**Parallelizes:** 11.2 ⟂ 11.3 ⟂ 11.4 once 11.1 lands.
**Ritual:** full gate; the runner change itself must keep the existing gate green (VM==native across
the whole suite under the new parallel runner).
**Fixtures owed:** `process_run_captured` (RUN, captures stdout+exit) · `test_selector_glob` (selects a
subset, both engines) · `test_panic_asserted` / `test_exit_asserted` (a diverging test PASSES) ·
`parallel_tests_serial_group` (a `#serial_group` suite runs non-parallel; result deterministic).
**Seed-cut moment:** `0.3.1.11-beta`.

---

### SW12 — Profiling, Coverage & PGO → `0.3.1.12-beta`

**Impact:** perf tooling built on SW1's arena vocab; closes the "suggest→auto-size" loop.

| Crumb | Size | Item | Notes |
|-------|------|------|-------|
| 12.1 | M | **#475** coverage as disk signal only (zero in-memory alloc during run); XML/Cobertura transform after | decouples coverage from memory |
| 12.2 | M | **#479** `teko profile` (static + dynamic) suggesting `#arena_size`/`#arena_depth` | consumes SW1 `#arena_size` |
| 12.3 | M | **#480** PGO arena pre-sizing — pré-linker auto-assigns size+depth from the profiler report; manual > PGO > default | consumes 12.2 |

**Parallelizes:** 12.1 ⟂ 12.2; 12.3 after 12.2.
**Ritual:** full gate.
**Fixtures owed:** `coverage_disk_signal_no_alloc` (run under a paranoid alloc-counter → zero in-run
coverage allocs) · `profile_suggests_arena_size` (profiler emits a size suggestion for a hot fn) ·
`pgo_autosize_precedence` (manual `#arena_size` overrides the PGO report).
**Seed-cut moment:** `0.3.1.12-beta`.
**REPORTED gap (not a new issue):** #479/#480 reference **`#arena_depth` (#476)**, which is NOT in this
task's explicit scope list (it sits in the master-plan 0.3.2 bucket). Either (a) pull #476 into SW12
as crumb 12.0 (it is the sibling directive #479/#480 suggest+auto-assign), or (b) ship #479/#480
size-only and honest-stop the depth suggestion until #476 lands. **Recommend (a)** — the profiler is
incoherent suggesting a directive that does not exist. Flagged UP for the owner's ruling.

---

### SW13 — Backend Completion Debts → `0.3.1.13-beta` → **0.3.1.0 GA**

**Impact:** closes the AOT backend to 100% → **#395 closes** → the wave releases.

| Crumb | Size | Item | Notes |
|-------|------|------|-------|
| 13.1 | L | **#222** i128 register-pair | native codegen |
| 13.2 | M | **FPR-spill** | float register spill |
| 13.3 | M | **has_back_edge (#443 remnants)** | `loop` back-edge completion |

**Parallelizes:** 13.1 ⟂ 13.2 ⟂ 13.3 (distinct backend passes).
**Ritual:** full gate + the final wave W15 sweep + doc-sync pre-launch (header ruling: each wave gets
its own W15 sweep before launch).
**Fixtures owed:** `i128_regpair_arith` (RUN, native, VM==native) · `fpr_spill_pressure` (many live
floats → correct spill, RUN native) · `loop_back_edge_native` (an extensible `loop` recovery case,
RUN native).
**Seed-cut moment:** `0.3.1.13-beta` → promote to **`0.3.1.0` release**; #395 → closed.

---

## 4. Dependency edges (summary)

```
SW1 ──▶ SW2 ──▶ SW3 ──▶ SW4 ──▶ SW5 ──▶ SW6 ──▶ SW7 ──▶ SW8 ──▶ SW9 ─▶ SW10 ─▶ SW11 ─▶ SW12 ─▶ SW13
 │       │       │(LINCHPIN     │       │(KEYSTONE      │(escape       │        │       │(needs   │
 │       │        seed)          │        self-host)     hatch)         │        │        SW2)     │
 │       └─ G8 gates #456,#449   └─ §10.4 gates 3.2      └─ A6 zero-   (parallel bugs)   #476 dep? │
 └─ [resources] feeds #456 CPU + #479/#480 sizing        dogfood                                    └─ #395 close
```

- **Hard logical edges:** SW1→SW2 (resources→CPU/RAM); SW3→SW4 (grafia seed→adoption); SW5→SW6 (facts→A1); SW6→SW7 (A1→A2/A5/A6 build on it); transparent-ref (SW4) → Marshall C4/C5/C7 (SW8); SW2 → SW11 (threading→parallel tests); SW1 → SW12 (#arena_size→profiler/PGO).
- **Seed-chain edges (D36):** EVERY arrow above is ALSO a seed edge — the chain serializes all merges even where logic would allow reordering. SW9–SW12 are logically near-independent and could be **reordered by the owner** without breaking anything except the version numbering.

---

## 5. Critical path

**The seed chain makes the critical path the full linear spine-through-Marshall run:**

```
SW1 → SW2 → SW3 → SW4 → SW5 → SW6 → SW7 → SW8
      (▲ #449 speeds all of SW3–SW13 once landed)
```

with **two non-negotiable hops**:

1. **SW3 → SW4 (the grafia hop).** SW3's `0.3.1.3-beta` MUST carry BOTH spellings so SW4 can migrate
   `src/` under fixpoint+diff. If SW3 seeds only the new spelling, SW4 cannot bootstrap; if SW4 fails
   self-host, the chain STRANDS (no `0.3.1.4-beta` → all of SW5–SW13 blocked). This is the single
   highest stranding risk.
2. **SW6 (the A1 keystone).** "The whole safety bet." Self-host is the proof; the R1 iterator-idiom
   collision is CERTAIN and must be met with the param-root refinement (§7), not a blanket reject.

Everything after SW8 (SW9–SW13) is logically loosely-coupled and gated only by the seed discipline;
its impact-ordering is a preference, not a hard chain.

---

## 6. What can DESIGN-AHEAD now (while 0.3.0 finishes)

All of the following need NO blocked API and can be authored/prebuilt against DECLARED shapes today:

- **Fixtures — ALL of them.** Every fixture named above (inputs + expected exit/COMPILE_FAIL, VM &
  native) can be written now; they compile-fail or run against the honest-stop until the feature lands.
- **The §10.4 auto-deref soundness workflow (SW3 crumb 3.0)** — a paper/adversarial pass; run it NOW
  so SW3 opens with a ratified peeling verdict (it GATES the surface, so front-loading removes the
  SW3 critical-path risk).
- **SP-1 pure facts (SW5 5.1/5.2)** — `type_reaches_ref` + `BorrowSummary` are pure queries consulted
  nowhere; they can be written and unit-dumped against today's checker.
- **Marshall C0–C3 + C6 + C8 bodies** — UNBLOCKED against today's checker (marshall-spec); the CODE can
  be written now (in today's `Ref<T>` spelling) even though its MERGE is sequenced into SW8; or land
  its unblocked half early if the owner prefers hardening the unsafe boundary sooner.
- **Correctness-debt fixes (SW9 #501/#502/#503)** — independent of the spine; buildable now.
- **The test-cluster subprocess model (SW11)** — designable now; needs SW2 to merge.
- **Module skeletons + honest-stops that compile today:** `src/marshall/marshall.tks` skeleton (§5.0),
  `src/threading/*`/`src/sync/*` skeletons — all with full Javadoc + honest-stops, so implementers
  resume in minutes.

---

## 7. Risks + law tensions (ranked)

### R1 — [CERTAIN, HIGH] The naive reject breaks self-host on the stdlib iterator idiom (SW6).
`src/iter` + `src/io` implement the ENTIRE iteration layer as "take a `ref Cursor` param, return a
closure that captures it" (28 functions). A blanket "reject any capture whose type contains a Ref"
(the literal skeptic-amendment-1) rejects all 28 → **self-host FAILS**. **Resolution (law-first,
passes-all-Laws):** refine to *"reject a closure capture whose reachable Ref does NOT root at a
param,"* + the caller-side `BorrowSummary` (SW6 crumb 6.2 / `a1_escape_ok`). The idiom is SOUND
(param-rooted); the refinement is SAFE and compiles the corpus ⇒ it wins. **Place the §10.4 soundness
workflow BEFORE the checker cements** (crumb 3.0, gating SW3's 3.2).

### R2 — [HIGH] Grafia-hop stranding (SW3→SW4). The single point that can freeze the whole chain.
If `0.3.1.3-beta` does not carry BOTH spellings, or if SW4's `src/` migration hits a pattern the new
checker rejects, no `0.3.1.4-beta` is cut and SW5–SW13 are all blocked. **Mitigation:** SW3 ships the
seed with `.value` deprecated-but-accepted (crumb 3.4); SW4 migrates file-by-file with fixpoint+diff,
using the **honest-stop** (keep `.value` on a stubborn file) until resolved, and only then removes the
alias (crumb 4.3). Never remove the old acceptance in the same sub-wave that adds the new adoption.

### R3 — [MEDIUM] Auto-deref soundness is UNVERIFIED and interacts with A1 (SW3/SW6).
Type-directed peeling (§4) + the `Ref<Ref<T>>` opening (depth cap 2) are new rules; a bad peel could
re-open a closed hole. **Resolution:** the §10.4 mini-soundness pass (ref-model open case #4/#5) runs
in the SW3 design review BEFORE cementing peeling; it GATES the nested-ref surface, not the spine
core. If it does not pass cleanly, NARROW the opening (honest-stop) — reject nested refs until proven,
rather than cement an unsound peel.

### R4 — [MEDIUM] A6 has ZERO dogfood exposure (SW7). The compiler calls no `mem::free`, uses no
`#wire`/DI, so self-host CANNOT validate A6 — its fixtures are its ONLY proof. **Resolution:** treat
the A6 fixtures as load-bearing (independent review of each), and prefer `#wire`-desugar-before-escape
so A6-DI is subsumed by A1 (less bespoke code, more shared coverage).

### R5 — [MEDIUM] Parallel front-end must not perturb the fixpoint (SW2). If #449 changes `.tkb`
byte-output or breaks gen1==gen2, the seed chain stalls. **Resolution:** keep #449 behind an
env flag defaulted OFF; the SEED builds serially; land `parallel_frontend_determinism` green on all
platforms before flipping the default. Determinism is a Law (differential equivalence) — reject any
scheduling that observably reorders output.

### R6 — [LOW] Honest-stop posture across SW5–SW7 (the enforcement law). Early spine versions are SAFE
(reject-what-you-can't-prove) and OVER-reject; they relax as the analysis matures. This is the project
honest-stop applied to the memory checker (#498 §9) and is a FEATURE, not a defect — call it out in
each SW5–SW7 review so an over-rejection is not mistaken for a bug.

### Law tensions (resolved, no HALT)
- **0.3.1/0.3.2 label vs one-big-0.3.1 (§0)** — resolved by keeping one seed chain and offering the
  SW8 seam as the optional boundary. REPORTED; not a HALT (same work, same order).
- **#476 (`#arena_depth`) referenced by #479/#480 but out of scope (SW12)** — resolved by recommending
  #476 be pulled into SW12 as crumb 12.0 (the profiler cannot coherently suggest a non-existent
  directive). REPORTED UP for the owner's ruling; not a HALT.

**No genuine unresolved tension remains → no HALT.** The two REPORTED items above are for the owner's
awareness; the plan is executable as written.

---

## 8. Ritual-point summary

| Sub-wave | Ritual | Why |
|----------|--------|-----|
| SW1–SW13 (every merge) | **full gate** | each cuts a seed; a bad seed strands the chain |
| SW3 | full gate + stable-seed dance (BOTH spellings) | linchpin seed |
| SW4 | full gate + **self-host critical** | `.value` removed; corpus on new spelling |
| SW6 | full gate + **corpus must self-host** + independent review | the A1 keystone — the load-bearing ritual |
| SW7 | full gate, each of A2/A5/A6 a shared-checker sub-PR ritual | tightens accepted language; A6 review-heavy (no dogfood) |
| SW8 | full gate at C0, C3, C5 | containment gate / first SAFE member / ptr↔Ref boundary |
| SW13 | full gate + final W15 sweep + doc-sync | wave launch |
| SW5 (SP-1a/1b) | **fast-path** inside the merge | pure queries, no corpus exposure |

---

*Companion designs: `docs/design/ref-transparent-model.md` (the model + A1–A6 + §10.4 open case),
`docs/design/marshall-spec.md` (C0–C8), `docs/design/safety-spine.md` (SP-0..SP-6 + thesis risks),
`docs/design/vm-retirement.md` (context). Governing: `DECISION_LOG.md` D36 (seed chain), D33
(metaprogramming post-1.0). Wave epic: #395 (stays OPEN until SW13 = 100%).*
