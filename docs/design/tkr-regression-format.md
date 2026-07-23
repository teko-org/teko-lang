# The `.tkr` Gherkin regression format (0.3.0.30)

Status: RATIFIED (owner ruling 2026-07-23). This SUPERSEDES and REPLACES the mis-named
`tkb-regression-format.md` (deleted): that doc put the Gherkin grammar under the `.tkb`
extension, which COLLIDES with the Teko Binary codec. The canonical extension taxonomy is
now fixed (see §0).

## 0. Extension taxonomy (canonical — owner 2026-07-23)

| ext | meaning | lives in | status |
|---|---|---|---|
| `.tkp` | project manifest (TOML) | every project root | unchanged |
| `.tkb` | **Teko Binary** — portable serialized-module codec; part of the `.tkl` package (ZIP of `.tkh`+`.tkb`+`.tsym`, `docs/BUILDING.md:92`) | `src/emit/tkb_{buf,frame,read,write}.tks` + `src/emit/tkb_test.tkt` | **FROZEN / UNTOUCHED** — it is the binary format, NOT a test format |
| `.tkr` | **Gherkin regressor** — the BDD spec (Feature / Scenario / Scenario Outline / Examples) declaring what to expect of a regressor project. A project may hold N `.tkr` files, and each file may hold N features (§1.1). | `src/build/tkr.tks` (the single parser) + `src/build/tkr_test.tkt` | canonical authoring format |
| `.tkl` | portable package (ZIP of `.tkh`+`.tkb`+`.tsym`) — the artifact a `kind = "package"` project emits | build output | unchanged |
| `.tkh` | exported interface (type signatures + docs) | inside a `.tkl` | unchanged |
| `.tsym` | symbol map | build output / inside a `.tkl` | unchanged |

The prior architecture put the Gherkin grammar in `src/build/tkb.tks` (WRONG — the `tkb`
name belongs to the binary codec in `src/emit`) and left `.tkr` as a compact TOML form.
Both are corrected here: the Gherkin grammar IS `.tkr`; there is NO on-disk TOML `.tkr`.

## 1. What `.tkr` is

`.tkr` is "Teko-native Gherkin": a Behaviour-Driven regressor spec using the Gherkin step
KEYWORDS (`Feature`, `Background`, `Scenario`, `Scenario Outline`, `Examples`, `Given`,
`When`, `Then`, `And`, `But`) with **TOML-value right-hand sides** read by the EXISTING
manifest value lexers (`mf_read_quoted` / `mf_read_array` / `mf_read_int`,
`src/build/manifest.tks`) verbatim. There is ZERO new value-lexing.

A regressor project is a directory with a single `.tkp` entry point plus one OR MORE `.tkr`
files (§1.1). The `When` verb classifies HOW the project is exercised (§4) — which, under the
artifact-kind reframe (`regressives-full-stack-0.3.0.30.md`), is the axis that matters: the
regressor surface partitions by ARTIFACT KIND × TARGET × FFI, not by language feature.

### 1.1 File granularity — free (owner 2026-07-23, #40)

The `.tkr`↔project relation is NOT 1:1; granularity is the developer's choice:

- **N files per project.** A project directory may hold ANY number of `.tkr` files (e.g.
  `arith.tkr`, `bitwise.tkr`, `io.tkr` side by side). Discovery scans and runs ALL of them
  (§10), not just the first — the project's verdict is the aggregate over every scenario of
  every `.tkr` it holds.
- **N features per file.** A single `.tkr` may hold MULTIPLE `Feature:` blocks (each opening a
  fresh feature) and, within each, any number of `Scenario` / `Scenario Outline`. `parse_tkr`
  returns a `[]TkrFeature` (a file is a LIST of features), not a single feature.
- **Both valid, no coercion.** A fat `.tkr` with everything, or many small `.tkr`s per concern
  — both are accepted; nothing forces one shape. The two axes compose freely: N files × N
  features/file × N scenarios/feature.

This changes only the FILE granularity inside a regressor project; it does NOT change the
taxonomy or the curated set of 10 regressors (`regressives-full-stack-0.3.0.30.md` §4).

### 1.2 The frozen in-memory target (`Tkr` + the check layer)

Parsing lowers Gherkin to the **frozen** in-memory model — reused verbatim, never a
serialized file:

- `Tkr` / `TkrKind` / `TkrMatch` / `TkrMatchMode` / `TkrExpect` / `TkrGolden` (types), and
- the check layer `check_run` / `check_compile_fail` / `match_stream` / `resolve_stream` /
  `tkr_expect_for` / `RegrOutcome` (verdict).

Pipeline: **parse `.tkr` (Gherkin) → `[]Tkr` (structs) → check.** The lowering is the only
new code; the `Tkr` model and the verdict layer are contract-frozen. There is no TOML `.tkr`
on disk; the compact `key = value` form that used to be the file format is now ONLY the shape
of the in-memory `Tkr`.

## 2. Grammar (line-oriented, minimal — M.5)

A `.tkr` file is a sequence of lines. Leading whitespace is insignificant. A line is one of:

- blank / `#`-comment — skipped (M.2).
- `Feature: <free text>` — opens a feature; the free text is documentation only. A file MAY
  contain multiple `Feature:` blocks (each closes the previous and opens a new one), so
  `parse_tkr` yields a `[]TkrFeature` (§1.1). A file with no explicit `Feature:` is one implicit
  feature carrying its scenarios.
- `Background:` — opens a step block whose `Given` steps are PREPENDED to every scenario
  (shared inputs: common `args`, `stdin`, `targets`, and interop `dependency` declarations).
- `Scenario: <free text>` — opens a single-run scenario.
- `Scenario Outline: <free text>` — a parametrised scenario; MUST be followed (after its
  steps) by an `Examples:` block.
- `Examples:` — opens the parameter table. The FIRST non-blank `| … |` line after it is the
  header row; each subsequent `| … |` line is one parameter row.
- `<Keyword> <noun-phrase> = <toml-value>` — a step. `<Keyword>` is
  `Given`/`When`/`Then`/`And`/`But`. `And`/`But` inherit the phase (Given/When/Then) of the
  preceding step. The noun-phrase (§3) selects WHICH `Tkr` field the value sets.
- A `When` entry-verb line has NO `= value` (§4) — it classifies HOW the project is
  exercised.

Placeholders `<name>` inside a Scenario Outline step value are substituted from the matching
Examples column BEFORE the value lexers see the line (§5).

## 3. Noun-phrases (Given inputs / Then expectations)

Given (inputs):
- `args` = `[ "…", … ]` → `Tkr.args` (fold-opaque argv operands, §7)
- `stdin` = `"…"` → `Tkr.stdin` (literal bytes)
- `stdin bytes` = `"""<hex>"""` → `Tkr.stdin` decoded from a HEX docstring (§8)
- `targets` = `[ "…" ]` / `"all"` → `Tkr.targets`
- `dependency` = `"<subdir>"` → an interop dependency the runner builds to its artifact and
  provisions before the consumer compile (§9; interop regressor). Background-only.
- `source` = `"cases/<f>.tks"` → the standalone failing source a `compilation fails`
  scenario compiles (negative path only, §5.1).
- `backend` = `"own"` / `"c"` → force the build backend (the FFI lane requires
  `backend = "own"`; a fixture passing only via the C emitter is a regression). Background-safe.
- `pending` = `"<KP-crumb>"` → mark a scenario skipped-GREEN until the named enabling crumb
  lands in gen1 (M.3-honest: an unbuilt capability is skipped, never faked). Used by the FFI /
  library-kind regressors that go live inside .30 behind their kill-C KP crumb.

When (entry verbs — classify the exercise mode, §4).

Then (expectations):
- `exit` = `<int>` → `Tkr.exit`
- `stdout` / `stdout bytes` / `stdout sha256` → `Tkr.stdout` (Literal / HEX-Literal / Sha256)
- `stderr` / `stderr sha256` → `Tkr.stderr`
- `artifact sha256` = `"<hex>"` (with an `on "<os-arch>"` prefix) → a `[golden.<os-arch>]`
  byte golden (`TkrGolden`)
- `artifact exists` (valueless) → assert the declared artifact FILE was produced (the
  build-only oracle for library/package kinds that have no run phase)
- `diagnostic` = `"…"` → `Tkr.diagnostic` (required stderr substring for `compilation fails`)

Unknown noun-phrases are an HONEST error (M.3) — a `.tkr` phrase set is closed and curated.

## 4. `When` entry verbs — the artifact-kind exercise modes

The `When` verb is the CORE of the artifact-kind reframe: it selects how the runner exercises
the project, keyed to the project's `[artifact] kind`. **Regressors run under GEN1 (the compiler
built from the umbrella sources), not the seed** — so "live at" is the .30 crumb after which GEN1
has the capability, NOT whether the 0.3.0.29 seed has it. Since .30 pulls own-native `.o`/`.a` +
system-`ld` + FFI into gen1 (`kill-c-pull-forward-0.3.0.30.md`), every verb below is a .30 verb;
each is implemented in the runner only once its capability is in gen1 (M.3 honest-stop / `Given
pending` skip otherwise):

| verb | artifact kind | oracle | live at (gen1 capability) |
|---|---|---|---|
| `built and run` | `binary` (exe) | exit + stdout/stderr of the run child | now (default path) |
| `compiled` | any | build exit 0 + declared streams (no run) | now |
| `compilation fails` | (source) | build fails + `diagnostic` substring | now |
| `it traps` (as `Then it traps`) | `binary` | non-zero exit only | now |
| `packaged` | `package` → `.tkl` | build exit 0 + `artifact exists` (the `.tkl`) | now (`project.tks` C7.12 emits `.tkl` — a codec path) |
| `built as static library` | `static` → `.a` | build exit 0 + `artifact exists` | .30 after **KP16** (own-native `.a` archive writer) |
| `built as shared library` | `shared` → `.so`/`.dylib`/`.dll` | build exit 0 + `artifact exists` | .30 after **KP16+KP17** (`.o` + system-`ld -shared`) |
| `linked and run` | `binary` consuming `Given dependency` artifacts | exit + stdout of the consumer run (interop) | package-dep: now; static/shared-dep: .30 after KP16/KP17 |
| `run on "<target>"` | wasm target | wasmtime/host exit + stdout | wasm32-wasi: .30 once the runner wasmtime harness lands (§9); wasm-browser: .31 unless the browser slice lands |
| `exports c abi` | `binary`/`shared` with `[artifact] abi = "c"` | `.h` emitted + `ns_name`/`= "SYM"` flat symbol; a C driver links + runs | .30 after **KP10+KP11** (+KP16/KP17 link) |
| `imports c abi` | `binary` with `extern fn/type` | links a C lib + runs; observable result | .30 after **KP7+KP12+KP13** (+ system-`ld`) |

The runner dispatches on `(m.artifact, When-verb)`: it reads the regressor's `.tkp`
(`parse_manifest`) for the kind, locates the produced artifact (`bin/<name>` for exe,
`<out>/<name>-<version>.tkl` for a package, `<out>/lib<name>.{a,so,…}` for a lib), and applies
the verb's oracle. A verb whose capability is not yet in GEN1 is an HONEST STOP / `Given
pending` skip (never a fake pass) — the corresponding regressor project is authored in the crumb
ordered right AFTER its enabling KP crumb (crumb sequence in
`regressives-full-stack-0.3.0.30.md` §9). Only what genuinely does not land in .30 defers to .31.

## 5. Scenario Outline + Examples (parametrised cases)

A `Scenario Outline` carries `<placeholder>` tokens inside its step values; its `Examples:`
table has a header row naming the placeholders and one data row per case. Lowering: for each
row, every `<name>` in every step value is textually replaced by that row's column value, the
substituted steps are lowered to a `Tkr`, and the project is exercised per row. For a `binary`
project the `.tkp` is compiled ONCE and RUN N times (one run per row); N rows ⇒ 1 compile + N
runs. Substitution is purely textual and happens BEFORE the value lexers see the line.

### 5.1 Compile-fail Outlines (the negative path — compile-per-scenario)

A `compilation fails` scenario has NO run phase and NO shared artifact: each scenario compiles
its own `Given source = "cases/<f>.tks"` standalone and asserts the build fails (+ the
`Then diagnostic` substring). NOTE (reframe): the reframe routes MOST compile-fail diagnostics
to CHECKER `#test`s (stronger than a diagnostic-less regressor) — REPORTED UP. The
`compilation fails` verb is RETAINED in the grammar for the residual diagnostics that are best
pinned as an end-to-end build, but it is no longer a standalone curated project by default.

## 6. Per-target routing: the `on "<os-arch>"` prefix

A `Then` step may be prefixed `on "<os-arch>"` to route the expectation into a
`[expect.<os-arch>]` override (`TkrExpect`) or a `[golden.<os-arch>]` byte golden
(`TkrGolden`), for legitimate per-target divergence (e.g. a wasm trap exit differing from a
native abort). Resolution at run time is the existing `tkr_expect_for` / `resolve_stream`.

## 7. The runtime-operand law

Every `Given args`/`stdin` value is a FOLD-OPAQUE runtime operand: it arrives through the
process boundary (argv/stdin) and is unknown at compile time, so the compiler CANNOT const-fold
the operation under test. The emitted code must actually perform the work at run time and the
regressor observes the real result. Under the reframe this law is exercised NATURALLY by the
consumer exes (the `binary` / `interop` regressors), not by a dedicated "runtime behaviour"
project.

## 8. HEX docstrings (`"""48 89 e5"""`) — control-byte goldens

A triple-quoted docstring holds whitespace-tolerant hexadecimal: all ASCII whitespace between
the `"""` fences is ignored; the remaining nibble-pairs decode to raw bytes. This carries
CONTROL bytes (`\n`, `\0`, opcodes) LITERALLY, closing the `.tkr` newline-in-quoted-literal
gap. Reconciliation: a HEX docstring for bytes you READ and reason about (short binary records,
opcode sequences); `sha256` for bytes you only FIX and never read (large/opaque streams,
artifacts).

## 9. Design-ahead contracts for the .30-gated exercise modes

Declared now so the implementer wires each in minutes the moment its enabling kill-C KP crumb
lands in gen1. All are .30 (ordered inside .30 behind their KP crumb — companion §9), NOT .31,
because .30 pulls the enabling capabilities into gen1:

- **Library artifacts (`static`/`shared`).** Oracle = build exit 0 + `artifact exists`. Live
  after **KP16** (`.a`) / **KP16+KP17** (`.so`/`.dylib`/`.dll` via system-`ld -shared`).
- **Interop (`linked and run` + `Background: Given dependency "<dep>"`).** The runner builds each
  dependency subdir to its artifact (`.tkl` now; `.a`/`.so` at KP16/KP17), provisions a scratch
  `packages/` (mirroring `crossmodule_regressions.sh`), builds the consumer, runs it.
  Package-interop is live NOW; static/shared-interop joins at KP16/KP17.
- **wasm run-oracle.** A `run_wasm_captured(module, args, stdin, prefix)` invoking `wasmtime run`
  (mirroring `validate_wasm_own.sh`), same `CapResult` shape as `run_captured`. The wasm32-wasi
  BUILD exists in gen1 today; this workstream's own crumb (companion C7) adds the RUN oracle —
  live in .30. wasm-browser additionally needs the JS-import runtime + a headless driver → .31
  unless that slice lands in .30.
- **FFI (import/export) = the kill-C §5 lane.** `imports c abi` / `exports c abi` verbs,
  `[artifact] abi = "c"`, `extern fn/type`, `.h` emission, the `ns_name`/`= "SYM"` flat-symbol
  convention, `Given backend = "own"`, and `Given pending = "<KP>"` skips. These ARE the fixtures
  `kill-c-pull-forward-0.3.0.30.md` §5 scopes under `examples/regressions/ffi/<name>/` (that doc
  still uses the OLD `.tkb` name + cites the deleted format doc — corrected to `.tkr` here;
  REPORTED UP). Live in .30 after KP10/KP11 (export) and KP7/KP12/KP13 (import).

## 10. Discovery — all `.tkr` in a project (owner 2026-07-23, #40)

Discovery scans a regressor directory for EVERY `.tkr` file (not the first) and runs each; a
project's verdict is the aggregate over all of them. The signature shift from the single-spec
model:

- `dir_tkr_files(dir) -> []str` — the basenames of ALL `*.tkr` in `dir` (replaces the
  first-only `dir_first_tkr`/`dir_first_spec`; a dir IS a regressor iff this is non-empty).
- `discover_source(source) -> []str` — unchanged in shape (regressor directories), but a dir
  qualifies when `dir_tkr_files(dir).len > 0`.
- `run_one_regressor(exe, dir, prefix) -> RegrOutcome` — iterates `dir_tkr_files(dir)`, parses
  each with `parse_tkr` (→ `[]TkrFeature`), runs every feature's every scenario, and folds the
  verdicts (FIRST failure wins, else pass). The per-file scratch prefix includes the `.tkr`
  basename so captures never collide across files.

`parse_tkr` returns `[]TkrFeature` (a file is a list of features, §1.1); a single-feature file
is the one-element case. Ordering is deterministic (sorted basenames, then source order within a
file) so a failure's report line is stable.

## 11. Meta-tests (`src/build/tkr_test.tkt`)

The consolidated parser's `#test`s assert: parse round-trips; a file with MULTIPLE `Feature:`
blocks yields a `[]TkrFeature` of the right length; a directory with MULTIPLE `.tkr` files is
discovered and run in full; a Scenario lowers to the expected `Tkr`; an Outline expands to the
expected `[]Tkr`; a HEX docstring decodes control bytes; the verdict PARITY between a lowered
scenario and a hand-built `Tkr` (identical `RegrOutcome`); and the runner-reports-FAIL self-test
(an in-memory `check_run` over a mismatching `CapResult` returns a failing `RegrOutcome`) — the
coverage the deleted `examples/regression-fixture/` demo used to carry, now a deterministic,
gate-safe checker test.
