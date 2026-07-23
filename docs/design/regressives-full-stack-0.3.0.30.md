# 0.3.0.30 HEADLINE — REGRESSORS BY ARTIFACT-KIND × TARGET × FFI (integrated plan)

Umbrella: `remodel/emit-throughput`. Owner reframe (2026-07-23). DESIGN ONLY; this doc is the
ordered crumb sequence. Companion: `docs/design/tkr-regression-format.md` (the `.tkr` Gherkin
format; taxonomy corrected). Sibling: `docs/design/kill-c-pull-forward-0.3.0.30.md` (the
own-native `.o`/`.a` + system-`ld` + FFI crumbs KP1–KP17 this doc gates the regressors behind).

## 0. The reframe (owner 2026-07-23) — WHY this replaces the oracle-type partition

The prior plan partitioned the curated regressors by ORACLE TYPE
(`rt_behavior` / `host_cli_io` / `compile_fail_diag`). That is **DISCARDED**. The compiler is a
native EXECUTABLE, so R0 already IS the regressor of the "exe native" artifact kind AND of the
whole LANGUAGE. The curated regressors must therefore cover only what R0 STRUCTURALLY cannot
exercise — the matrix of **artifact kinds, targets, and FFI** — never language features
(those live in R0 + checker `#test`s). Concretely:

- runtime-operand law and host/CLI/IO are exercised NATURALLY by the consumer exes (the
  `binary` / `interop` regressors) — no dedicated project.
- compile-fail DIAGNOSTICS become CHECKER `#test`s (stronger than a diagnostic-less regressor)
  — REPORTED UP (the architect does not open issues).

"3 é teórico; de fato não vejo mais do que 10 (incluindo o próprio compilador)." — owner.

## 1. Taxonomy (corrected — see companion doc §0)

`.tkb` = Teko Binary codec (`src/emit/tkb_*`, FROZEN/UNTOUCHED). `.tkr` = Gherkin regressor
(single parser in `src/build/tkr.tks`). `.tkp` = manifest. `.tkl` = portable package.

Consolidation (retained by the owner):
- MIGRATE the Gherkin logic from `src/build/tkb.tks` INTO `src/build/tkr.tks` (the single
  `.tkr` parser). DELETE `src/build/tkb.tks` + `src/build/tkb_test.tkt`. The test becomes
  `src/build/tkr_test.tkt`.
- DELETE the old on-disk TOML `.tkr` file format: the `parse_tkr` TOML reader + its
  `TkrBuild`/`TkrExpectAccum`/`tkr_split_kv`/`tkr_open_header`/`tkr_apply_*` helpers go. The
  `Tkr` struct + the check layer (`check_run`/`check_compile_fail`/`match_stream`/
  `resolve_stream`/`tkr_expect_for`/`TkrExpect`/`TkrGolden`) STAY as the frozen lowering target.
- RENAME `examples/regressions/rt_behavior/rt_behavior.tkb` → `rt_behavior.tkr` (same Gherkin).

## 2. THE GATING MODEL — regressors run under GEN1, not the seed (owner 2026-07-23)

**The gate for a regressor is NOT "can the seed do it?" — it is "does GEN1 (the compiler built
from the umbrella/PR sources) have the capability at the point the regressor is authored?"**

The regression phase fires in `./bin/teko test .`, where `./bin/teko` = gen1 (the seed compiled
the umbrella sources into gen1). The seed 0.3.0.29 only needs to build gen1; it does NOT need the
capability the regressor exercises. Therefore:

- Since **.30 pulls own-native `.o`/`.a` emission + link via the system `ld` INTO .30**
  (`kill-c-pull-forward-0.3.0.30.md` §2/§3, crumbs KP16/KP17), the **static / shared / pack /
  interop** regressors are authorable **WITHIN .30**, LIVE right after the crumb that adds the
  emission to gen1 — NOT `.31`-gated. They run under gen1 (which HAS the emission), even though
  the 0.3.0.29 seed cannot emit `.a`/`.o`.
- Likewise **FFI export/import** land in .30 behind their KP crumbs (KP10/KP11 reverse-FFI;
  KP7/KP12/KP13 import), so the `ffi_export`/`ffi_import` regressors are **.30-LIVE**, aligned to
  the kill-C FFI lane (`kill-c-pull-forward-0.3.0.30.md` §5).
- The **only** ordering constraint is WITHIN .30: a regressor exercising a capability goes LIVE
  **after** the KP crumb that adds it to gen1 (else gen1, built from pre-emission sources, fails
  the regressor). Sequenced inside .30 behind the enabling crumb — never pushed to .31.
- Only what genuinely does NOT land in .30 defers to .31 (today: `wasm_browser`, pending the
  browser JS-runtime slice — stated explicitly below).

**Verified emission status TODAY (pre-KP, under the current C-backed gen1), from
`src/build/project.tks`:**
  - `binary` (exe) — YES (default path).
  - `package` → `.tkl` — YES (C7.12, `project.tks:907`; a CODEC path, backend-independent).
  - `static` → `.a` — HONEST STOP today (`project.tks:902`); becomes YES in gen1 at **KP16**.
  - `shared` → `.so`/`.dylib`/`.dll` — HONEST STOP today (`project.tks:905`); YES at **KP16/KP17**.
  - `wasm32-wasi` — build emits `.wasm` today (`validate_wasm_own.sh`); the RUN oracle needs a
    wasmtime harness added to the runner (crumb C7 below, own to this workstream).
  - `wasm32-browser` — module emits; JS-runtime wiring is "a later slice" → `.31` unless it lands.
  - FFI (`extern`, `abi="c"`) — no front-end today; lands in gen1 at KP7–KP15 (all .30).

## 3. Ground truth (verified in-tree, 2026-07-23)

- Runner + `Tkr` model + check layer in `src/build/regression.tks` + `src/build/tkr.tks`.
  `project.tks::test_project` calls `run_regression_phase(exe, m)`; the BUILD gate does NOT
  (`--no-verify` → `build_ungated`). `[tests] regression` fires EXCLUSIVELY in `teko test .`.
- `teko.tkp` already declares `[tests] regression = ["examples/regressions"]` + `gate = true`.
- Anti-recursion: `TEKO_IN_REGRESSION` sentinel (`regression.tks:459`) + the structural belt
  (each regressor is `teko build`-ed → `build_ungated`, no nested test phase).
- 217 dirs under `examples/regressions/`; `const_crossmodule_inline` is the dep/consumer pair.
- The kill-C FFI lane (`kill-c-pull-forward-0.3.0.30.md` §5) already scopes FFI fixtures under
  `examples/regressions/ffi/<name>/` — but names them `.tkb` and cites the DELETED
  `tkb-regression-format.md`. That is the OLD taxonomy; it must be updated to `.tkr` + the new
  grammar (incl. the `backend`/`pending` Given nouns, §7). REPORTED UP (that doc is another
  agent's territory; the taxonomy fix propagates there).

## 4. The regressor set (≤10 incl. the compiler) — FINAL, gated per KP crumb

Partition axis: **artifact kind × target × FFI × interop.** "Live at" = the crumb after which
gen1 has the capability and the regressor is authored. All except `wasm_browser` are **.30**.

| # | regressor | artifact / target | uniquely asserts (R0 cannot) | live at (gen1 capability) |
|---|---|---|---|---|
| R0 | **the compiler** | `binary` (exe native) | whole language + exe kind; fixpoint gen1==gen2; own==C/own==wasm differentials | existing lanes (§5); declared by `regressor.tkr` |
| 1 | **`rt_behavior`** (renamed) | `binary` (exe) | an ARBITRARY built program's observable exit/stdout over fold-opaque argv operands; runner first-light self-proof | **.30 C2** (builds under gen1 today) |
| 2 | **`pack`** | `package` → `.tkl` | the package artifact kind emits a well-formed `.tkl` | **.30 C3** (`.tkl` codec exists today) |
| 3 | **`interop`** | `binary` consuming #2 (+ later #4/#5) | binary↔package interoperability end-to-end (build dep → provision → consume → run) | **.30 C4** (package-interop today); static/shared deps join at KP16/KP17 |
| 4 | **`static`** | `static` → `.a` | the static-library artifact kind + a consumer linking it | **.30, after KP16** (own-native `.a` archive writer) → crumb C-S1 |
| 5 | **`shared`** | `shared` → `.so`/`.dylib`/`.dll` | the shared-library artifact kind + a consumer linking it | **.30, after KP16+KP17** (`.o` + system-`ld -shared`) → crumb C-S2 |
| 6 | **`wasm_wasi`** | `wasm32-wasi` | a wasm module RUN under wasmtime (exit/stdout) via `teko test` | **.30 C7** (build exists today; this crumb adds the runner wasmtime harness) |
| 7 | **`wasm_browser`** | `wasm32-browser` | a browser-target module under a headless JS runtime | **.31** unless the browser JS-runtime slice lands in .30; then .30 behind it |
| 8 | **`ffi_export`** | `binary`/`shared` `abi="c"` | reverse-FFI: teko exposes a C ABI (`.h`, `ns_name`/`= "SYM"`) a C driver links | **.30, after KP10+KP11 (+ KP16/KP17 link)** → crumb C-F1 |
| 9 | **`ffi_import`** | `binary` + `extern fn/type` | teko calls a C library through `extern` | **.30, after KP7+KP12+KP13 (+ system-`ld`)** → crumb C-F2 |

**Nine regressors + R0 = ten entry points, all .30 except `wasm_browser`.** Four are LIVE
immediately under today's gen1 (R0, `rt_behavior`, `pack`, `interop`-package); the rest go LIVE
inside .30 the moment their KP crumb lands in gen1 — each DESIGNED now (companion §9) so the
implementer wires them in minutes.

### 4.1 Why not fewer / more
- Not fewer: each asserts a DISTINCT artifact-kind capability R0 alone does not. `rt_behavior`
  doubles as the runner's live self-proof.
- Not more: `interop` (#3) is the "quarto que compõe a disposição" — it is where #2/#4/#5 prove
  interoperation and grows as the library kinds land, rather than spawning new entry points.

### 4.2 The FFI regressors ARE the kill-C §5 lane
`ffi_export`/`ffi_import` (#8/#9) are the SAME fixtures the kill-C doc §5 scopes under
`examples/regressions/ffi/<name>/`, RE-EXPRESSED in the corrected `.tkr` grammar (with
`Given backend = "own"` and `Given pending = "<KP-crumb>"` skipped-green until the crumb lands).
This design does not duplicate that suite; it OWNS the taxonomy correction (`.tkb`→`.tkr`) and
the grammar the suite authors against.

## 5. R0 — the compiler as the root regressor (the .29 debt)

R0 is the EXISTING self-host + `teko test .` + fixpoint + backend-differential lanes, NAMED and
COUNTED as the compiler-regressor via a **root `.tkr`**.

- **File: `regressor.tkr` at the repository root.** Distinct from `teko.tkp`; a `.tkr` at the
  root is unambiguous and greppable. NOT under `examples/regressions/`, so discovery does not
  pick it up as an ordinary regressor.
- **Declarative + NON-RECURSIVE.** `regressor.tkr` DECLARES the contract; the runner does NOT
  execute it. The runner treats the repo-root self-project as a no-op: `TEKO_IN_REGRESSION`
  already prevents a `teko test .` re-entry, and discovery is scoped to the
  `[tests] regression` sources (`examples/regressions`), which excludes the root. A self-entry
  would be circular. The EXISTING lanes VERIFY the contract; `regressor.tkr` is its DECLARATION.
- **Contract (documentation Scenarios, each with a `# verified-by:` lane comment):**
  1. `Feature: R0 — the compiler is its own full-stack regressor (exe native).`
  2. `Scenario: the compiler compiles the whole language` — seed builds gen1 (`teko . -o bin`).
  3. `Scenario: the compiler passes its own gate` — `teko test .` exit 0 (600+ `#test`s + D4).
  4. `Scenario: the compiler reaches a byte-identical fixpoint` — **gen1 == gen2, NEVER gen3.**
  5. `Scenario: the emitted code agrees across backends` — own==C (`diff_c_own.sh`) and
     own==wasm (`validate_wasm_own.sh`).

## 6. Re-mapping the 217 dirs

- **SUBSUMED by R0 (drop as regressors; dirs may stay as corpus):** every language-feature
  fixture — the compiler source USES the feature and/or its `#test` suite covers it. The bulk.
- **FOLD into `interop`:** `const_crossmodule_inline` (dep/consumer) — its build-dep→`.tkl`
  →consume→run shape IS the package-interop regressor. Retire `crossmodule_regressions.sh` once
  `interop` is green (§8).
- **FOLD into `rt_behavior`:** fixed-literal arithmetic fixtures (`own_arith_exit`,
  `own_sub_exit`, `char_ops`) become Outline ROWS over argv operands (already the shape of
  `rt_behavior.tkr`); then dropped as standalone regressors. Their DIRS stay where referenced by
  `diff_c_own.sh` / `validate_wasm_own.sh` (backend corpora) — do NOT delete.
- **→ CHECKER `#test` (REPORTED UP):** compile-fail rejection fixtures with a unique diagnostic.
  A diagnostic-less regressor passes on ANY build failure (weak); the rejection is a checker
  concern. Where not yet a checker `#test`, it should become one — stronger than a regressor.
- **KEEP as corpus (NOT regressors, NOT deleted):** dirs referenced by `diff_c_own.sh`,
  `validate_wasm_own.sh`, and the sanitizers.yml build-all loops.
- **DROP (delete with the harness):** failure-only compile-fail dirs with no diagnostic pin and
  no corpus role (their only consumer was the retired shell harness).

## 7. `examples/regression-fixture/` (the runner's failure-reporting self-test)

The two TOML `test.tkr` demos (`exit_ok` pass / `exit_bad` FAIL) proved the runner REPORTS a
mismatch. Since the on-disk TOML `.tkr` format is DELETED and a genuinely-failing regressor
cannot live under a green `teko test .` gate, this coverage MOVES to an in-memory checker `#test`
in `src/build/tkr_test.tkt` (`check_run` over a mismatching `CapResult` → failing `RegrOutcome`;
over a matching one → passing). Deterministic, gate-safe, stronger. DELETE
`examples/regression-fixture/`.

## 8. CI-shrink SPEC (owner applies `.github`/`scripts`)

The architect does NOT edit `.github`/`scripts`; this is the exact delta, applied at C6 AFTER the
live regressors (R0 + `rt_behavior` + `pack` + `interop`) are green under `teko test .`.

### 8.1 DELETE (scripts that run regressors INDIVIDUALLY instead of via `teko test`)
- `scripts/compile_fail_regressions.sh` — its diagnostic pins → checker `#test`s (§6, REPORTED
  UP) BEFORE deletion; the rest were failure-only.
- `scripts/positive_regressions.sh` — SUBSUMED by R0 (`EXPECT_EXIT` positives).
- `scripts/native_regressions.sh` — the `#64` CWD-build guard folds into a regressor (§8.3).
- `scripts/crossmodule_regressions.sh` — folds into the `interop` regressor (§6).

### 8.2 REMOVE these steps from `.github/workflows/native.yml` (`gen1-checks` job)
- `native.yml:282-286` — "CWD build regression check (issue #64, on gen1)" → `native_regressions.sh`
- `native.yml:294-298` — "EXPECT_COMPILE_FAIL regression check (issue #610, on gen1)" → `compile_fail_regressions.sh`
- `native.yml:308-312` — "EXPECT_EXIT positive const regression check (issue #594 8f, on gen1)" → `positive_regressions.sh`
- `native.yml:321-325` — "E2E cross-module const regression check (issue #594 8d, on gen1)" → `crossmodule_regressions.sh`

### 8.3 The `#64` CWD-build guard — FOLD, don't drop
`native_regressions.sh` checks `cd <project> && teko build .` (CWD-relative resolution, #64/#66)
— a distinct INVOCATION shape the by-PATH runner does not reproduce. FOLD it into a `rt_behavior`
(or `interop`) `Scenario` whose runner mode chdir's into the regressor dir before `teko build .`.
If that mode is not added this wave, KEEP `native_regressions.sh` + its step and note the debt —
do NOT silently drop the #64 coverage.

### 8.4 KEEP (backend differentials — NOT "individual regressor" runners)
`scripts/diff_c_own.sh`, `scripts/validate_wasm_own.sh` stay (own==C, own==wasm differentials);
their corpus dirs stay too (§6).

### 8.5 R0 needs NO new lane
R0 is the existing self-host + `teko test .` + fixpoint + differentials; only NAMED here (via
`regressor.tkr`). No workflow edit required for R0.

## 9. Crumb sequence (ordered; `[RITUAL]` = fixpoint gen1==gen2 byte-identical + `teko test .` green on all 3 OSes)

Every crumb builds under the previous seed and uses only str/list/match/struct/enum/loop (all in
the 0.3.0.29 seed). C0–C7 are this workstream's crumbs; the C-S*/C-F* regressor crumbs are
ordered INSIDE .30 immediately AFTER their enabling kill-C KP crumb lands in gen1.

### C0 — Docs + R0 declaration (this wave's design deliverable)
Files: `docs/design/tkr-regression-format.md` (done), this doc (done), NEW `regressor.tkr` at the
repo root (§5). Gate: light. NOT `[RITUAL]`.

### C1 — Consolidate the Gherkin parser into `.tkr` (the rename/merge)
Files: `src/build/tkr.tks` (RECEIVES the migrated Gherkin parser + lowering + Outline expand;
LOSES the TOML `parse_tkr` + helpers), DELETE `src/build/tkb.tks`, DELETE
`src/build/tkb_test.tkt`, NEW/renamed `src/build/tkr_test.tkt`, `src/build/regression.tks`
(discovery + dispatch + the root self-project guard).

Renames (Gherkin `Tkb*`/`tkb_*` → `Tkr*`/`tkr_*`, avoiding collision with the frozen
`Tkr`/`TkrKind`/… which STAY): `TkbPhase`→`TkrPhase`, `TkbStep`→`TkrStep`, `TkbRow`→`TkrRow`,
`TkbExamples`→`TkrExamples`, `TkbScenario`→`TkrScenario`, `TkbFeature`→`TkrFeature`;
`parse_feature`→`parse_tkr` (REPLACES the deleted TOML `parse_tkr`); `run_one_tkb`→`run_one_tkr`;
every `tkb_*` helper → `tkr_*`. Discovery collapses to `.tkr`-only (drop the dual-format
`SpecRef.is_tkb`) AND scans ALL `.tkr` in a dir, not the first (owner 2026-07-23, #40 — free
file granularity; companion §10): `dir_first_tkr` → `dir_tkr_files(dir) -> []str`, and
`parse_tkr` returns a `[]TkrFeature` (a file may hold multiple `Feature:` blocks).

Signature shapes (full Javadoc; bodies migrate from the current `tkb.tks`):

```teko
/**
 * TkrPhase — the Gherkin step PHASE: `Given` (inputs), `When` (the entry verb classifying the
 * exercise mode), or `Then` (the expected observable output). `And`/`But` inherit the preceding
 * step's phase and never appear as a distinct value here.
 *
 * @since 0.3.0.30
 */
pub type TkrPhase = enum { Given; When; Then }

/**
 * TkrFeature — one parsed `Feature:` block of a `.tkr` file: the `name` free text and the
 * `scenarios` (Background already folded into each scenario's steps). A file may hold MULTIPLE
 * features (`parse_tkr` returns `[]TkrFeature`); a project may hold multiple `.tkr` files — the
 * file↔project relation is free (companion §1.1, #40).
 *
 * @since 0.3.0.30
 */
pub type TkrFeature = struct {
    /** the Feature free-text label. */
    name: str
    /** the scenarios, each carrying the Background Given steps. */
    scenarios: []TkrScenario
}

/**
 * parse_tkr — read a `.tkr` (Gherkin) source into its `[]TkrFeature` (one per `Feature:` block;
 * a file with no explicit `Feature:` is one implicit feature), or fail honestly (M.3). Reuses the
 * manifest value lexers (`mf_read_quoted`/`mf_read_array`/`mf_read_int`) verbatim for every
 * right-hand side; introduces only keyword-line + Examples-table + HEX-docstring parsing.
 * REPLACES the deleted TOML `parse_tkr`.
 *
 * @param src  the `.tkr` file contents
 * @return     the parsed features, or an `error` on any malformed line / unknown noun-phrase
 * @throws     on a malformed keyword line, an unknown noun-phrase, or a malformed value
 * @since 0.3.0.30
 */
fn parse_tkr(src: str) -> []TkrFeature | error { /* migrate the tkb.tks body; loop features */ }

/**
 * dir_tkr_files — the basenames of ALL `*.tkr` files in `dir` (sorted for a stable report),
 * empty when `dir` is unreadable or holds none. A directory IS a regressor iff this is non-empty.
 * REPLACES the first-only `dir_first_tkr`/`dir_first_spec` (owner #40 — a project may hold N
 * `.tkr` files, all run).
 *
 * @param dir  the directory to inspect
 * @return     every `.tkr` basename in `dir` (sorted), or an empty list
 * @since 0.3.0.30
 */
fn dir_tkr_files(dir: str) -> []str { /* list_dir, filter .tkr, sort */ }

/**
 * run_one_regressor — exercise a regressor DIRECTORY: for EVERY `.tkr` in `dir_tkr_files(dir)`,
 * parse (→ `[]TkrFeature`) and run every feature's every scenario, dispatching on
 * `(m.artifact, When-verb)` (companion §4): `built and run` (exe), `packaged` (`.tkl` +
 * `artifact exists`), `linked and run` (interop), `built as static/shared library`, `run on
 * "<target>"` (wasm), `exports/imports c abi` (FFI), `compilation fails` (per-source). A verb
 * whose capability is not in THIS gen1 is an HONEST STOP (or `Given pending`-skipped-green). The
 * verdict folds the FIRST failure across all files/features/scenarios, else pass. Each file's
 * scratch prefix embeds its basename so captures never collide.
 *
 * @param exe       the absolute path to this compiler binary (gen1)
 * @param regr_dir  the regressor project directory
 * @param prefix    the scratch-file prefix for this regressor
 * @return          the aggregate verdict over every `.tkr` in the directory
 * @since 0.3.0.30
 */
fn run_one_regressor(exe: str, regr_dir: str, prefix: str) -> RegrOutcome { /* iterate + fold */ }
```

Root self-project guard in `run_regression_sources`: a discovered dir resolving to the repo root
(the self-project holding `regressor.tkr`) is SKIPPED (honest skip: `regression skip <root>
(self-project — verified by the self-host lanes)`). `[RITUAL]`.

### C2 — `rt_behavior` renamed + first light (LIVE .30, today's gen1)
`git mv examples/regressions/rt_behavior/rt_behavior.tkb rt_behavior.tkr`. Confirm discovery runs
it via `teko test .`. Inert→live transition + runner self-proof. Resolve tree-cleanliness (R2).
`[RITUAL]` — 3 OSes green + duration delta measured.

### C3 — `pack` regressor (LIVE .30, `.tkl` codec exists today)
NEW `examples/regressions/pack/` (`pack.tkp` `[artifact] kind = "package"` + a small `src` of
`exp` items + `pack.tkr` `When packaged` / `Then artifact exists`). Add the `packaged` verb + the
`artifact exists` oracle to `run_one_tkr` (locate `<out>/<name>-<version>.tkl`). `[RITUAL]`.

### C4 — `interop` regressor (LIVE .30, package-interop today; folds `const_crossmodule_inline`)
NEW `examples/regressions/interop/` (a `dep/` package + a `consumer/` exe + `interop.tkr` with
`Background: Given dependency "dep"` + `When linked and run` + `Then exit`/`stdout`). Add the
`dependency` Background noun + `linked and run` mode to `run_one_tkr`: build each dep to its
artifact (`.tkl` today), provision a scratch `packages/`, build the consumer, run it. `[RITUAL]`.

### C5 — args passthrough (runner ergonomics)
`manifest.tks` (`Manifest.test_regression_args` — `[tests] args`), `project.tks` (`--` tail in
the `test` subcommand), `regression.tks` (append after each spec's `args`). Precedence:
`spec.args ++ m.test_regression_args ++ passthrough` (fold-opaque operands ALWAYS first).
Recommend BOTH; if one, the `--` tail (no manifest surface). `[RITUAL]`.

### C6 — CI-shrink + harness-retirement SPEC (owner applies §8)
BEFORE deletion: the compile-fail diagnostic pins are raised as checker `#test`s (REPORTED UP)
and the `#64` CWD guard is folded (§8.3) or explicitly kept. `[RITUAL final for the spine]`.

### C7 — `wasm_wasi` regressor (LIVE .30 — this crumb OWNS the enabling capability)
Add `run_wasm_captured(module, args, stdin, prefix)` to `regression.tks` (invoke `wasmtime run`,
mirroring `validate_wasm_own.sh`; same `CapResult` shape as `run_captured`). NEW
`examples/regressions/wasm_wasi/` (`Given targets = ["wasm32-wasi"]` + `When run on
"wasm32-wasi"` + `Then exit`/`stdout`). The wasm BUILD exists today; this adds the RUN oracle.
`[RITUAL]`.

### C-S1 — `static` regressor (LIVE .30, ordered AFTER kill-C KP16)
Once gen1 emits `.a` (KP16, own-native archive writer): NEW `examples/regressions/static/`
(`kind = "static"` + `static.tkr` `When built as static library` / `Then artifact exists`) and
extend `interop` with `Given dependency "static-dep"` linking the `.a` via system `ld`.
`[RITUAL]`.

### C-S2 — `shared` regressor (LIVE .30, ordered AFTER kill-C KP16+KP17)
Once gen1 emits `.o` + links `-shared` via system `ld` (KP16/KP17): NEW
`examples/regressions/shared/` and extend `interop` to consume the shared lib. `[RITUAL]`.

### C-F1 — `ffi_export` regressor (LIVE .30, ordered AFTER kill-C KP10+KP11, link via KP16/KP17)
Once gen1 does `exp fn` C-ABI export (KP10) + `.h` emission (KP11): the reverse-FFI fixtures of
kill-C §5.2.B under `examples/regressions/ffi/` in `.tkr` form (`When exports c abi`; a
`consumer.c` compiled by host `cc`, linked with the Teko `.o`/`.a` by the system `ld`, run +
asserted). `[RITUAL]`.

### C-F2 — `ffi_import` regressor (LIVE .30, ordered AFTER kill-C KP7+KP12+KP13, system-`ld` link)
Once gen1 lowers `extern fn` calls (KP7) + macro resolver (KP12) + vararg ABI (KP13): the
using-C fixtures of kill-C §5.2.A (`When imports c abi`, `Given backend = "own"`,
`Given pending = "<KP>"` skipped-green until each lands). `[RITUAL]`.

### (deferred) `wasm_browser` — `.31` unless the browser JS-runtime slice lands in .30
If the browser runtime + a headless driver land in .30, author `examples/regressions/wasm_browser/`
behind that crumb (same shape as C7 with a headless harness). Otherwise it is the sole `.31`
regressor.

## 10. Risks / tensions / what to report up

- **R1 (perf).** Live set is tiny: `rt_behavior` (~1 compile + ~15 runs), `pack` (1 build),
  `interop` (2 builds + 1 run), later `wasm_wasi`/`static`/`shared`/FFI a handful each. R0 is the
  existing gate (zero new cost). MEASURE at each `[RITUAL]`. No HALT.
- **R2 (tree cleanliness).** `compile_regressive` writes `examples/regressions/<name>/bin/` (and
  the interop scratch `packages/`) into the tree, perturbing the fixpoint byte-identity check.
  RESOLUTION (C2): gitignore `examples/regressions/**/bin/` + `**/packages/` (a repo edit the
  implementer applies, NOT `.github`/`scripts`). Preferred alt: redirect output to
  `bin/.regr-work/<sanitized>/`.
- **R3 (compile-fail diagnostics → checker `#test`).** REPORTED UP: the diagnostic pins must
  become checker `#test`s BEFORE `compile_fail_regressions.sh` is deleted (C6), else the wording
  coverage is lost.
- **R4 (#64 CWD guard).** Decision (§8.3): fold into a regressor CWD-compile mode, or KEEP
  `native_regressions.sh`. Recommend fold; if not this wave, KEEP + note the debt.
- **R5 (kill-C taxonomy drift — REPORTED UP).** `kill-c-pull-forward-0.3.0.30.md` §5 and its
  crumb table still say `.tkb` and cite the DELETED `tkb-regression-format.md`. The FFI lane must
  author against `.tkr` + this grammar (incl. the `backend`/`pending` Given nouns). That doc is
  another agent's territory; the taxonomy correction propagates there — routed to the owner.
- **R6 (root `.tkr` recursion).** `regressor.tkr` is DECLARATIVE and runner-SKIPPED; the
  `TEKO_IN_REGRESSION` sentinel is the backstop. Non-circular.
- **R7 (ordering-within-.30, the owner's one caveat).** Each of static/shared/ffi goes LIVE only
  AFTER its KP crumb lands in gen1 (C-S1 after KP16, C-S2 after KP16/KP17, C-F1 after KP10/KP11,
  C-F2 after KP7/KP12/KP13). Landing a regressor before its KP crumb would fail gen1 (built from
  pre-capability sources). Sequenced inside .30 behind the enabler — never pushed to .31.

## 11. Decisions taken (ratify with the owner)

1. **Final set = 10 (R0 + 9), all .30 except `wasm_browser`.** LIVE-today: R0, `rt_behavior`,
   `pack`, `interop` (package). LIVE-later-in-.30 behind a KP crumb: `wasm_wasi` (C7), `static`
   (KP16), `shared` (KP16/KP17), `ffi_export` (KP10/KP11), `ffi_import` (KP7/KP12/KP13).
   `wasm_browser` → .31 unless the browser slice lands.
2. **Gating model = gen1 capability, not seed capability** (owner 2026-07-23): regressors run
   under `./bin/teko` = gen1; the gate is "does gen1 have the capability at authoring," so
   static/shared/pack/interop/FFI are .30, ordered after their kill-C KP crumb.
3. **Root file name = `regressor.tkr`** (repo root, declarative, runner-skipped).
4. **`rt_behavior` RETAINED** (renamed to `.tkr`) as the exe/runner self-proof + R0's
   observable-program complement.
5. **`interop` is package-first** (folds `const_crossmodule_inline`), extended to static/shared
   as they land — one entry point, not three.
6. **`examples/regression-fixture/` DELETED**, its failure-reporting coverage → an in-memory
   `tkr_test.tkt` `#test`.
7. **compile-fail diagnostics → checker `#test`s** (REPORTED UP), not a curated regressor.
8. **FFI regressors ARE the kill-C §5 lane** re-expressed in `.tkr`; that doc's `.tkb` naming +
   its citation of the deleted format doc must be corrected (REPORTED UP).
9. **Free file granularity** (owner 2026-07-23, #40): a project may hold N `.tkr` files and a
   `.tkr` may hold N features — discovery scans/runs ALL (`dir_first_tkr` → `dir_tkr_files`,
   `parse_tkr` → `[]TkrFeature`). Fat-or-many is the developer's choice; the taxonomy and the
   set of 10 are unchanged (only the file granularity INSIDE a project).

No genuine LAW TENSION surfaced: Teko-only (`.tks`/`.tkt`/`.tkr`), W15 full-Javadoc (all new
signatures), M.3 honest-stops (parser errors + gated verbs + `Given pending` skips), fixpoint
gen1==gen2 (never gen3), TEST-lane / no-gen2 / anti-recursion boundaries all hold. No HALT.
