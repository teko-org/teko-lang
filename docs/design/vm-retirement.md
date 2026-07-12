# VM retirement — sunset the tree-walking interpreter, native becomes the sole engine (crumb plan)

**Status:** DESIGN (doc-only). Sub-PR of the 0.3.1 (Safety spine) wave. Issue **#524**
(owner-ratified 2026-07-12: *"carimba a aprovação do INSS; serviu bem, honras sejam dadas, mas
já está velha e caducando"*). Branch `docs/vm-retirement`, base `remodel/backend-build`, opened as a
DRAFT PR. This is a **KEYSTONE removal**: after it, `teko` has ONE execution engine — native
AOT (own backend + the C backend) — and the VM==native differential is gone.

> **Owner principle (ratified 2026-07-12): 100% VM removal.** *"Se está aposentado, não se mantém
> na ativa; tudo que precisar pra remover 100% a VM deverá ser feito."* Nothing VM stays active in
> any form. The §10 micro-decisions are therefore **RATIFIED**, not defaults: the REPL is retired
> (VM-only, no native repoint without a JIT), the C-bootstrap is removed, `--vm-gate` is removed
> outright, `teko test` keeps its name over the native engine, and **`teko run` is REPOINTED to a
> native debug build-and-execute** (like `cargo run` / `go run`) — the command survives, only the VM
> machinery behind it goes.

> This is a PLAN. It designs the sunset of `src/vm/` and the inversion of the gate split
> (memory `teko-native-test-gate` / `teko-verify-both-with-test-gate` — native was the DEFAULT
> gate since #168/#265; this makes it the ONLY one). It grounds every crumb in the merged code:
> `src/vm/vm.tks`, `src/build/project.tks`, `src/repl/repl.tks`, `src/driver.tks`,
> `src/codegen/codegen.tks`, `main.tks`, `.github/workflows/native.yml`,
> `.github/workflows/sanitizers.yml`, `scripts/diff_vm_native.sh`, `CMakeLists.txt`.

---

## 0. TL;DR

- **Load-bearing UX decision — BOTH `teko test` and `teko run` survive, repointed to NATIVE.**
  - `teko test <dir>` keeps its contract (run the `.tkt` suite, report pass/fail + coverage, emit NO
    release binary) but drives it through the already-shipping `run_native_gate` (#265) instead of
    `vm::run_tests_cov` — a thin wrapper over the native gate's test-run leg.
  - `teko run <dir>` keeps its contract (build-and-execute the project, propagate the program's exit
    code) but does it as a **native DEBUG build + exec** (`cargo run` / `go run` shape) instead of
    VM-eval: front-end → codegen → link with the debug profile (`-g`, assertions on, no optimization)
    to a scratch dir → run the produced binary. Only the VM eval path (`vm::run`) is removed; the
    COMMAND stays. Rejected for `teko test`: removing it in favour of `teko . -o bin` (that also builds
    a release binary, and CI's `./bin/teko test .` self-test would need repointing for no benefit).
- **Crumb count: 5**, dependency-ordered so each is independently gate-able and only ONE self-host
  re-baseline is in flight at a time:
  1. **CI-first** — delete the `vm-gate-nightly` lane + the VM==native differential step + gut
     `diff_vm_native.sh`'s VM side. Stops the false-failures immediately (no `src/` change → no
     re-baseline). *This is the smallest unblock.*
  2. **Driver repoint** — `teko test` → native gate; `teko run` → native-debug build-and-exec; delete
     the VM eval path (`run_gate_vm`, the `vm::run`/`vm::run_tests*` callers) + `--vm-gate`; delete the
     dead legacy mirror `src/driver.tks`. (re-baseline #1)
  3. **Retire the REPL** — remove `src/repl/` + the `repl` subcommand (its only consumer of the VM
     *interpreter*, and a VM-only dev tool). (re-baseline #2)
  4. **Move coverage out + delete `src/vm/`** — relocate the engine-independent coverage subsystem to
     a new `teko::coverage` namespace, retarget the self-exclusion guards, then delete all of
     `src/vm/` + the frozen C twins + the vestigial C-bootstrap. (re-baseline #3)
  5. **Cleanup + final fixpoint** — stale VM comments, DECISION_LOG, MASTER_PLAN marks, coverage-floor
     re-check, final `gen2 == gen3` verify.
- **Shared-code hazards found (2):** (a) the **coverage subsystem** (`cov_cobertura`, `cov_merge`,
  `functions_coverage`, `line_coverage`, `branch_coverage`, `*_coverage_pct`, and their
  `CovWalk`/`BranchSite`/`CovCount` helpers) lives in `vm.tks` but is consumed by the NATIVE gate —
  it **moves**, it is not deleted; (b) the **REPL** is a deep consumer of the VM interpreter types —
  it is retired *with* the VM (micro-decision M1, RATIFIED = retire).
- **No HALT.** Both hazards resolve cleanly law-first. No non-VM type is trapped in `src/vm/` that
  cannot move.

---

## 1. What actually depends on the VM (grounded map)

Enumerated by grepping `use teko::vm` / `vm::` across `src/**/*.tks` and `**/*.tkt`, and the
subcommand dispatch in `main.tks`.

### 1.1 The VM source
- `src/vm/vm.tks` (4251 lines, namespace `teko::vm`) — the tree-walking interpreter + the coverage
  subsystem (two concerns in one file; see §4).
- `src/vm/vm.c` + `src/vm/vm.h` — the **frozen** C twins (bootstrap seed; not maintained).
- `src/vm/vm_test.tkt` — the interpreter's own `.tkt` suite (tests `teko::vm`, which is excluded from
  coverage — see §4.3).

### 1.2 The consumers (every non-VM importer)

| Importer | `vm::` symbols used | Kind | Fate |
|---|---|---|---|
| `src/build/project.tks` | `coverage_pct`, `line_coverage_pct`, `branch_coverage_pct`, `cov_cobertura`, `cov_merge` | **shared coverage** | repoint to `teko::coverage` (§4) |
| `src/build/project.tks` | `run`, `run_tests`, `run_tests_cov` | VM-only | delete the callers (crumb 2) |
| `src/repl/repl.tks` | `Venv`, `Value`, `StructVal`, `OptVal`, `Normal`, `VmReturn`, `Exit`, `Stepped`, `exec_stmt`, `fresh_env`, `is_plain_expr_stmt`, `run` | **deep interpreter** | retire the REPL (crumb 3, M1) |
| `src/driver.tks` | `run` | dead legacy mirror | delete `src/driver.tks` (crumb 2) |
| `src/repl/repl_test.tkt` | REPL's own suite | test | delete with the REPL |
| `src/vm/vm_test.tkt` | interpreter suite | test | delete with `src/vm/` |

Nothing in `checker`, `codegen`, `lexer`, `parser`, `backend`, `lir`, `emit`, or the stdlib imports
`teko::vm`. The blast radius is exactly these files.

### 1.3 The dispatch (`main.tks`)
`teko run` → `teko::build::run_project` → `vm::run` (**repointed to native-debug**, §2.2). `teko test` →
`teko::build::test_project` → `vm::run_tests_cov` (**repointed to native**, §2.1). `teko repl` →
`teko::repl::run_cli` (VM interpreter — retired). `teko build` / `teko . -o bin` → `compile_project_g`,
whose D4 gate is NATIVE by default (`run_native_gate`) and only opts back to the VM under `--vm-gate` /
`TEKO_VM_GATE=1` (**the opt-out is removed outright**, §2/M3).

### 1.4 CI + scripts
- `.github/workflows/native.yml`:
  - `diff-vm-native` job → step *"Differential harness (VM == native …)"* runs `scripts/diff_vm_native.sh`
    (the false-failure source: it runs `teko run` = VM). The same job's *"gen1 self-test"* runs
    `./bin/teko test .` (VM today; becomes native automatically once crumb 2 repoints `teko test`).
  - `vm-gate-nightly` job → `./bin/teko test .` (VM, scheduled).
  - `gate` job → `needs:`/checks reference `diff-vm-native`.
- `.github/workflows/sanitizers.yml` — no VM job *runs*; several comments cite the VM lane's per-test
  arena/LSan rewind as rationale (stale after retirement → crumb 5 doc-sync).
- `scripts/diff_vm_native.sh` — the VM==native harness + a native-only CWD-build-regression check (#64)
  worth keeping.
- `scripts/diff_c_own.sh` — the **own==C differential** (the replacement cross-check; unchanged).
- `CMakeLists.txt` — `TEKO_VM_SOURCES = src/vm/vm.c` in the `teko_bootstrap` lib; `add_executable(teko …)`
  links `main.c` + `src/driver.c` (which `#include "vm/vm.h"`). **CI never invokes cmake** — the C
  bootstrap `build/teko` is vestigial (the seed is the downloaded release via
  `scripts/ci_provision_teko.sh`).

---

## 2. What `teko test` and `teko run` become (the load-bearing decisions)

Both subcommands survive; only the VM engine behind them is replaced by native AOT. `teko build`
stays the RELEASE build; `teko run` is its DEBUG-flavored build-and-execute sibling; `teko test` is
the native test gate without a release artifact.

### 2.1 `teko test` → native test-run

**Decision: keep `teko test`, repoint it to a native test-run.** It becomes a thin wrapper over
`run_native_gate` (which already emits a test-profile C TU whose `main()` calls each `#test`,
compiles it with the build's `run_cc`, runs the child fail-fast, and merges the child's `.tkcov`).
`teko test` differs from `teko . -o bin` only in that it does NOT rebuild the release program.

`src/build/project.tks::test_project` today:

```teko
fn test_project(dir: str, gen_cov: bool) -> i32 {
    let fe = match project_frontend_sel(dir, true) { Frontend as x => x; error as e => return fail(dir, e.message) }
    let trc = if gen_cov { vm::run_tests_cov(fe.prog, true, true, "cobertura.xml") } else { vm::run_tests_cov(fe.prog, true, false, "") }
    if trc != 0 { return trc }
    coverage_gate_result(dir, fe)
}
```

post-retirement (the implementer copies this verbatim):

```teko
/**
 * The `teko test <dir>` entry (D2, post-VM): assemble WITH the `.tkt` tests, then run every
 * `#test` NATIVELY via the #265 native gate (emit a test-profile C TU, compile with `run_cc`,
 * run the child fail-fast — a failed assert panics with a non-zero exit, M.1), and enforce the
 * D4 coverage floors. Unlike `teko build`, it emits NO release binary. `--coverage` writes a
 * Cobertura report to `<cwd>/cobertura.xml` (the project root — `teko test` has no `-o`).
 *
 * @param dir      the project directory (already the cwd; the front-end chdir'd in)
 * @param gen_cov  `--coverage` — write the Cobertura report next to the project root
 * @return         0 when every test passes and the floors hold, else the failing status
 */
fn test_project(dir: str, gen_cov: bool) -> i32 {
    let fe = match project_frontend_sel(dir, true) { Frontend as x => x; error as e => return fail(dir, e.message) }
    let trc = run_native_gate(dir, "bin", fe.prog, fe.manifest)
    if trc != 0 { return trc }
    if gen_cov {
        let xml = coverage::cov_cobertura(fe.prog)
        match teko::io::write_file("cobertura.xml", xml) { error as e => return fail(dir, e.message); null => { } }
        teko::io::println("teko: wrote coverage report cobertura.xml")
    }
    coverage_gate_result(dir, fe)
}
```

Notes for the implementer:
- `run_native_gate(dir, out_dir, prog, m)` is unchanged and already self-contained (it does NOT do a
  release rebuild — that lives in `run_gate_native` *after* it). `teko test` reuses it directly; no new
  function is needed. The `"bin"` scratch dir holds the transient `<name>-tktest{.c,,.tkcov}`.
- `coverage_gate_result` and `run_native_gate` already call the coverage functions; after crumb 4 those
  resolve to `teko::coverage::*` (see §4). In crumb 2 they are still `vm::*` (vm.tks is still present),
  so crumb 2 changes only the *engine*, not the coverage home — keeping crumb 2 small.

**CI impact:** `./bin/teko test .` (the `diff-vm-native` gen1 self-test at native.yml:264, and the
retired `vm-gate-nightly` line) becomes a native run with no workflow edit — the subcommand is the same,
the engine underneath changed.

### 2.2 `teko run` → native DEBUG build-and-execute (repointed, not dropped)

**Decision: keep `teko run`, repoint it to a native debug build + exec** (the `cargo run` / `go run`
shape). It preserves the exact exit-code contract the VM path had — the program's own exit
(`teko::exit(n)` → n; a panic → non-zero) — but produces it by RUNNING a native binary instead of
tree-walking. Like `go run`, it builds-and-runs the PROGRAM; it does NOT run the `.tkt` gate (that is
`teko test`) and does NOT run the D4 coverage floors.

`src/build/project.tks::run_project` today:

```teko
fn run_project(dir: str) -> i32 {
    let fe = match project_frontend(dir) { Frontend as x => x; error as e => return fail(dir, e.message) }
    vm::run(fe.prog)
}
```

post-retirement (the implementer copies this verbatim):

```teko
/**
 * The `teko run <dir>` entry (post-VM): build the project with the DEBUG profile and EXECUTE the
 * resulting native binary — the `cargo run` / `go run` shape. Same front-end as `teko build` but
 * WITHOUT the `.tkt` tests (a run, not a gate) and WITHOUT the D4 coverage floors: this is the fast
 * debug-iteration command. The debug profile links with `-g` (DWARF for the E4 native stack trace,
 * fed by the `.tsym` map), assertions ON, and no optimization (see build_cc_argv's `debug` seam).
 * The binary is built to a scratch dir, then run; the returned status is the PROGRAM's own exit code
 * (teko::exit(n) -> n, a panic -> non-zero) — the exact contract the retired VM `run` path had.
 *
 * @param dir  the project directory (already the cwd; the front-end chdir'd in)
 * @return     the executed program's exit code, or a non-zero build/diagnostic status
 */
fn run_project(dir: str) -> i32 {
    let fe = match project_frontend(dir) { Frontend as x => x; error as e => return fail(dir, e.message) }
    let stem = fe.manifest.name
    let bc = build_debug_and_report(dir, stem, "target/debug", fe.prog, fe.manifest)
    if bc != 0 { return bc }
    let binp = teko::str::concat("target/debug/", stem)
    let argv = teko::list::push(teko::list::empty(), binp)
    teko::process::run(argv)
}
```

Supporting shape the implementer adds — a debug seam threaded through the shared link line, so the C
and own backends both honor it without forking `build_cc_argv`:

```teko
/**
 * build_cc_argv gains a `debug` toggle: when true it appends the DEBUG profile flags to the shared
 * link line — `-g` (emit DWARF so the E4 native stack trace resolves against the `.tsym` map) — and
 * leaves optimization OFF (the pipeline's default is already `-O0`) and assertions ON (assert.c is
 * always linked; nothing defines NDEBUG). When false the line is byte-identical to today's build, so
 * the FIXPOINT and every release artifact are unchanged.
 *
 * @param input   the input path (a generated `.c` or an own-backend `.o`)
 * @param binary  the output native executable path
 * @param m       the resolved manifest
 * @param prog    the checked program (extern-reachability)
 * @param debug   append the debug profile flags (`-g`); false = today's byte-identical release line
 * @return        the full `cc` argument vector, ready for teko::process::run
 */
fn build_cc_argv(input: str, binary: str, m: Manifest, prog: checker::TProgram, debug: bool) -> []str
```

`run_cc` / `link_object` pass `debug=false` (release, byte-identical); a new `build_debug_and_report`
mirrors `codegen_and_report` but calls `build_cc_argv(..., true)` and writes the `.tsym` beside the
binary (debug metadata is exactly what the debug profile wants). Notes for the implementer:

- **No optimization split exists today.** `build_cc_argv` currently passes no `-O`/`-g`/`-DNDEBUG`, so
  today's single profile is already unoptimized with assertions live. The debug profile therefore adds
  only `-g`; the "release vs debug" distinction is `-g` presence (+ the `.tsym`), nothing more, until a
  real optimization pass is designed (a separate, later concern — NOT this issue).
- **Scratch dir:** build to `target/debug/<stem>` (created if absent), consistent with the `cargo`/`go`
  target-dir convention, so `teko run` never clobbers `bin/` (the release output). Passing `-o
  target/debug` keeps run artifacts out of the release path.
- **Backend parity:** `teko run` respects `TEKO_BACKEND=native` (the own-AOT seam) exactly as `teko
  build` does — `build_debug_and_report` branches on `native_backend_selected()` the same way
  `codegen_and_report` does, so the debug run works on both backends and feeds the own==C differential.
- **Exit-code fidelity:** the returned status is the child program's, propagated unchanged — a faithful
  repoint of the VM `run` contract (only the engine changed).

**CI impact:** `scripts/diff_vm_native.sh` used `teko run` for the VM side of the differential; crumb 1
removes that (the own==C differential is the replacement). No CI step depends on `teko run` producing VM
output afterward.

---

## 3. What is lost, and why it is acceptable

The VM==native differential (`scripts/diff_vm_native.sh`, issue #54) cross-ran every
`examples/regressions/*` project through both engines and asserted identical exit code + stdout. Its
unique coverage was: **a frontend/semantics divergence where native silently miscompiles but the VM
aborts (or vice-versa)** — e.g. #517 Finding 1 (`_ as x` bound null into a non-null `T`; VM aborted
134, native exited 5). Retiring it loses that specific "two independent lowerings of the same checked
tree disagree" oracle.

Why acceptable (owner-accepted 2026-07-12; the honest scoreboard from the issue):

1. **The differential's remaining value is now net-negative on the imminent work.** G1 gensym (#521)
   and G2 mangle (#522) are *native-codegen* changes; their correct cross-check is **own==C**
   (`diff_c_own.sh`), not VM==native — the VM shares the front-end with native but not the codegen, so
   it adds little protection there while still costing false-failures.
2. **The VM is now a *source* of false failures.** The wrapper-descent bug (VM-only: `match` on a
   single-field wrapper — memory `teko-vm-wrapper-descent-bug`) fails *correct* code and already forced
   a workaround that blocked #520. That is the VM obstructing, not the differential saving.
3. **The VM cannot run the surface that matters now.** `extern` (project_frontend / fs / net / os) is
   VM-refused by design (C7.1a) — SP-1 and the PM PoC both hit this. The differential already carries a
   growing `NATIVE_ONLY` allow-list for exactly this reason.
4. **The replacements are real and already in CI:**
   - **own==C differential** (`diff_c_own.sh`) — two *independent native lowerings* of the same tree
     disagree ⇒ the exact "divergence oracle" role, now inside the native world where it matters.
   - **the native gate** (`teko . -o bin`) — the whole `.tkt` corpus with all three coverage floors,
     asserted, every PR.
   - **adversarial review + the PoC round #523** — the human/structural net that caught #517 Finding 1
     in the first place (the *checker* was unsound; the fix is a checker fix, independent of which
     engine surfaced it).
   - **self-host fixpoint** — `gen2 == gen3` byte-identity is itself a whole-compiler differential
     (the compiler compiling the compiler) that no interpreter provides.
5. **The regression bar is preserved by fixtures, not by the interpreter.** The `_ as x` class of bug is
   caught going forward by an *asserted* native fixture (see §7), not by an engine that happens to abort
   — which is the more honest, more portable guard anyway (M.1).

Net: the differential's genuine catches were checker/soundness bugs that a checker fix + an asserted
fixture cover; its running cost (false-failures + double-maintenance of every feature in two engines) is
removed. **Retire.**

---

## 4. Shared-code hazard: the coverage subsystem (MOVE, do not delete)

### 4.1 The trap
`vm.tks` carries an **engine-independent** coverage subsystem that the NATIVE gate depends on. It does
NOT interpret — it statically walks the checked `TProgram` and reads the runtime `teko::cov_*` marks
(which the native child process populates and `cov_merge` unions in). Deleting `src/vm/` naively would
delete the native gate's coverage machinery.

Symbols to move (contiguous block, `vm.tks` ~lines 3779–4251, minus the two VM-only runners):

| Symbol | Signature | Exported? |
|---|---|---|
| `BranchSite` | `type BranchSite = struct { line: u32; col: u32; n: u64 }` | private |
| `CovWalk` | `type CovWalk = struct { sites: []BranchSite; lines: []u32 }` | private |
| `CovCount` | `type CovCount = struct { cov: u64; val: u64 }` | private |
| `LineBr` | (branch tally per line) | private |
| `cov_walk_empty`, `cov_addline`, `cov_addsite`, `cov_walk_expr`, `cov_walk_stmt`, `cov_walk_block` | static walkers | private |
| `cov_pct`, `cov_rate_str`, `cov_line_branch`, `cov_fn_line_tally`, `cov_fn_branch_tally`, `cov_emit_lines`, `cov_emit_class`, `count_prod_fns` | tally/emit helpers | private |
| `functions_coverage`, `line_coverage`, `branch_coverage` | `(prog: checker::TProgram) -> CovCount` | `exp` |
| `coverage_pct`, `line_coverage_pct`, `branch_coverage_pct` | `(prog: checker::TProgram) -> u64` | `exp` |
| `cov_cobertura` | `(prog: checker::TProgram) -> str` | `exp` |
| `cov_merge` | `exp extern fn cov_merge(path: str) -> bool = "tk_cov_merge" from "teko_rt"` | `exp` |

Left behind in `vm.tks` and **deleted** with it (VM-only): `run`, `run_tests`, `run_tests_cov` (the two
runners) + the entire interpreter (`eval_expr`, `exec_block`, `exec_stmt`, `pat_match`, `Venv`, `Value`,
`Flow`, …).

### 4.2 Where it moves
**New file `src/coverage/coverage.tks`, namespace `teko::coverage`.** A dedicated namespace (not folded
into `teko::build`) is *required*, not cosmetic — see §4.3. `src/build/project.tks` adds
`use teko::coverage` and the 5 call sites become:

- `vm::coverage_pct` → `coverage::coverage_pct` (×2: `report_native_coverage`, `coverage_gate_result`)
- `vm::line_coverage_pct` → `coverage::line_coverage_pct` (×2)
- `vm::branch_coverage_pct` → `coverage::branch_coverage_pct` (×2)
- `vm::cov_cobertura` → `coverage::cov_cobertura` (`run_gate_native` XML; new `test_project` XML)
- `vm::cov_merge` → `coverage::cov_merge` (`run_native_gate`)

This is additive syntax only (structs, `exp fn`, `loop`, `match`, `teko::mem::append_fo`) — all already
in the stable seed, so the **stable-seed lane gotcha** (memory `teko-wave-stable-seed-lane-gotcha`) does
not bite. `discover` auto-picks up the new `src/coverage/` directory as `teko::coverage`, consistent
with every other `src/<dir>` namespace.

### 4.3 The self-exclusion invariant (must be preserved)
The coverage code today excludes the `teko::vm` namespace from measurement in **five** places:

- `src/codegen/codegen.tks:8534` — `if f.is_test || f.namespace == "teko::vm" { return emit_function(...) }`
  (under `TestCov`, VM fns skip the `tk_cov_mark` prologue → not counted).
- `vm.tks` guards in `cov_cobertura`, `line_coverage`, `branch_coverage`, `count_prod_fns` —
  `if !f.is_test && f.namespace != "teko::vm"`.

The rationale (verbatim from `count_prod_fns`): those functions "implement the NATIVE VM and are never
reached via find_function during interpretation, so counting them would permanently depress coverage."

Once the coverage code moves to `teko::coverage`, the same self-exclusion must apply to *it* — the
report machinery should not measure itself (e.g. `cov_cobertura` only runs under `--coverage`, so
counting it would sink the floors on a plain build). **Retarget all five guards `"teko::vm"` →
`"teko::coverage"`.** This is byte-identity-safe for every surviving function:

- The coverage functions **were part of `teko::vm`** (they lived in `vm.tks`), so they already skipped
  the prologue / were excluded. After the move they still skip / are excluded — identical treatment.
- The rest of `teko::vm` (the interpreter) is deleted, so it is measured neither before nor after.

This is why the neutral home must be its own namespace: if coverage moved into `teko::build`, the guard
`!= "teko::build"` would wrongly exclude `project.tks`/`manifest.tks`/`discover.tks` from the floors.

### 4.4 The REPL hazard (deep interpreter consumer)
`src/repl/repl.tks` (600 lines, namespace `teko::repl`) is not a shallow consumer — it *is* an
interpreter front-end. It persists a `vm::Venv`, runs each typed line via `vm::exec_stmt`, and reads
back `vm::Value`/`vm::StructVal`/`vm::OptVal`. A native AOT compiler has no line-by-line eval without a
JIT or a compile-per-line loop (both out of scope for #524). Since the REPL is *defined* as "an
interactive session over the VM" (DT3) and is a dev tool, not part of the language contract, it is
retired with the VM (micro-decision **M1**, RATIFIED = retire; §10). This is the correct order: the REPL
must be removed **before** `src/vm/` is deleted, or the compiler will not typecheck.

---

## 5. Ordered crumb sequence

Each crumb: files, the ritual leg, and what proves it. `src/`-changing crumbs (2, 3, 4) each carry ONE
self-host fixpoint re-baseline, sequenced strictly one-at-a-time (§6).

### Crumb 1 — CI-first: stop the false-failures (no `src/` change)
**Files:** `.github/workflows/native.yml`, `scripts/diff_vm_native.sh`.
- Delete the `vm-gate-nightly` job (native.yml ~353–384).
- Delete the *"Differential harness (VM == native …)"* step from `diff-vm-native` (native.yml ~278–282).
- Rename the `diff-vm-native` job → `gen1-checks` (it keeps: gen1 build, gen1 self-test, CLI-flags, fmt,
  `region_drop_subtree_test`, and the **own==C differential**). Update the `gate` job's
  `needs:`/`DIFF_RESULT` block (native.yml 390, 399–425) to the new job name.
- `scripts/diff_vm_native.sh`: gut the VM legs — drop the per-fixture `teko run` comparison and the
  `EXPECTED_FAIL`/`NATIVE_ONLY` VM machinery; keep the native-only CWD-build-regression check (#64).
  Rename → `scripts/native_regressions.sh` and repoint the (renamed) CI step, OR fold the CWD check into
  `diff_c_own.sh` and delete the script. Default: rename + keep the CWD check (smallest, preserves #64).
- **Ritual leg:** CI-only. The per-PR `gate` must go green with the own==C differential + native gate,
  and no VM step. **Proves:** the wrapper-descent false-failure path is gone from CI; the replacement
  cross-checks still gate.

### Crumb 2 — driver repoint (`teko test` → native gate; `teko run` → native-debug; drop the VM eval path + `--vm-gate`)
**Files:** `src/build/project.tks`, `main.tks`, delete `src/driver.tks`.
- `test_project` → native (the verbatim body in §2.1).
- `run_project` → native-debug build-and-exec (the verbatim body in §2.2); add the `debug` seam to
  `build_cc_argv` + the new `build_debug_and_report`. The `teko run` COMMAND and its `main.tks` arm
  STAY; only the `vm::run` eval call is replaced.
- Delete `run_gate_vm`, `vm_gate_requested` (the VM eval + gate path); simplify `native_gate_of`
  and collapse `run_gate` to always-native:

  ```teko
  /**
   * run_gate — the D4 test-gate body: run every `#test` NATIVELY (#265, the sole engine after
   * VM retirement), then enforce the three coverage floors and rebuild the release program.
   *
   * @param dir      the project directory (diagnostics)
   * @param out_dir  the output directory (default "bin")
   * @param gen_cov  `--coverage` — write the Cobertura report
   * @param fe       the front-end result assembled WITH the `.tkt` tests
   * @return         0 when the gate passes and the release build succeeds, else the failing status
   */
  fn run_gate(dir: str, out_dir: str, gen_cov: bool, fe: Frontend) -> i32 {
      run_gate_native(dir, out_dir, gen_cov, fe)
  }
  ```

  Update `compile_project_g`'s signature to drop the now-inert `native_gate` parameter (it is always
  native), threading through to `run_gate`. `main.tks`'s `compile_project_g(...)` call sites drop the
  `native_gate_of(args)` argument. **Remove `--vm-gate` outright** (M3, ratified): drop it from
  `project_arg_of`'s flag-skip list and delete `native_gate_of` — the flag is gone, not inert
  (`TEKO_VM_GATE` is likewise no longer read). A stray `--vm-gate` now falls through as an unknown token
  (the standard "not a project" path), which is the honest signal that the VM is gone.
- `main.tks`: KEEP the `if cmd == "run"` arm (it now calls the native-debug `run_project`); rewrite the
  `teko run …` help line to "build (debug) and run the project's native binary"; remove the `--vm-gate …`
  help line. Leave the `repl` arm for crumb 3.
- Delete `src/driver.tks` — a dead legacy C-twin mirror (its `run`/`compile`/`backend`/`compile_project`
  are called by NOTHING; superseded by `src/build/project.tks` + `main.tks`; it `use teko::vm` only for
  its dead `run`). Removing it also drops dead uncovered functions → coverage floors improve.
- **Ritual leg:** full native gate + a `teko run` exec smoke + fixpoint. **Proves:** `teko test .` runs
  native and passes; `teko run <proj>` builds a debug binary and executes it, propagating the program's
  exit code (e.g. `teko::exit(5)` → 5); `--help` advertises `run` as a native debug-run and no longer
  lists `--vm-gate`; `gen2 == gen3`.

### Crumb 3 — retire the REPL
**Files:** delete `src/repl/repl.tks`, `src/repl/repl_test.tkt`; `main.tks`.
- `main.tks`: remove the `if cmd == "repl"` arm + `teko::repl::run_cli` call, and the `teko repl …` help
  line.
- **Ritual leg:** full native gate + fixpoint. **Proves:** `teko repl` is honestly rejected (exit 2);
  no `teko::repl`/`teko::vm` import remains outside `src/vm/` + `project.tks`'s coverage calls; `gen2 ==
  gen3`.

### Crumb 4 — move coverage out, delete `src/vm/`
**Files:** create `src/coverage/coverage.tks`; edit `src/codegen/codegen.tks`, `src/build/project.tks`,
`CMakeLists.txt`; delete `src/vm/{vm.tks,vm.c,vm.h,vm_test.tkt}`; delete `src/driver.c`, `src/driver.h`,
`main.c` (frozen C twins retired with the exe).
- Create `src/coverage/coverage.tks` (namespace `teko::coverage`) with the §4.1 block, moved verbatim,
  with the four guards retargeted to `"teko::coverage"`. Every declaration carries full Javadoc (W15);
  the moved doc-comments are already Javadoc — keep them, adjusting `teko::vm` prose references.
- `src/codegen/codegen.tks:8534`: retarget `"teko::vm"` → `"teko::coverage"` (byte-identity-safe, §4.3).
- `src/build/project.tks`: `use teko::coverage`; drop `use teko::vm`; repoint the 5 coverage call sites
  (§4.2). No `vm::` reference remains.
- Delete `src/vm/` (all four files).
- `CMakeLists.txt`: remove `TEKO_VM_SOURCES` (`src/vm/vm.c`) from `teko_bootstrap`; add
  `src/coverage/coverage.c`? — **no**: the C twins are frozen and NOT regenerated for new Teko files
  (Teko-only law). `coverage.tks` has no C twin (like `src/regex/regex.tks`, `src/fmt/fmt.tks`). Retire
  the vestigial C-bootstrap `teko` executable target (`add_executable(teko main.c src/driver.c …)` +
  its `target_*`), since `main.c`/`src/driver.c` `#include "vm/vm.h"` and the seed is now the downloaded
  release (CI never invokes cmake). Keep the `teko_bootstrap` STATIC lib (minus vm.c) as the frozen
  front-end compile-regression guard. See M2 for the conservative alternative.
- **Ritual leg:** full native gate + own==C differential + fixpoint + coverage-floor re-check.
  **Proves:** `src/vm/` is gone; `teko . -o bin` self-build is green; the three coverage floors still
  pass (the move is self-excluded, §4.3); the `--coverage` Cobertura report is byte-identical to the
  pre-move report on the same corpus; `gen2 == gen3`.

### Crumb 5 — cleanup + final fixpoint + docs
**Files:** `.github/workflows/sanitizers.yml`, `.github/workflows/native.yml`, `DECISION_LOG.md`,
`TEKO_MASTER_PLAN.md`, `README.md`/`CONTRIBUTING.md` (if they mention `teko run`/`teko repl`/the VM).
- Remove stale VM-lane comments/rationale (sanitizers.yml per-test-rewind note; native.yml VM prose).
- DECISION_LOG entry (D-number, the retirement + M1/M2/M3 defaults, base = issue #524 owner ratification).
- MASTER_PLAN: mark the VM interpreter + REPL rows retired; note `teko run`/`teko test` are now native
  (`teko run` = native debug build-and-exec); native is the sole engine.
- **Ritual leg:** the FULL gate (this is the closing ritual point). **Proves:** the whole wave is green,
  documentation matches reality, `gen2 == gen3` one final time.

---

## 6. Byte-identity / fixpoint re-baseline sequencing

Removing `vm.tks` (~1400 functions) + `repl.tks` + `driver.tks` shrinks the compiler source, so the
emitted C for the whole compiler changes and the self-host fixpoint (memory
`selfhost-byte-identity-broken`: normalized `gen1 == gen2`, byte-identical `gen2 == gen3`) re-baselines.

Per the re-baseline-ordering law, only ONE re-baseline may be in flight at a time and it must NOT overlap
another issue's:
- **Land the whole of #524 AFTER the loop #520 merges** (its re-baseline settles on the current VM-gated
  CI, exactly as the issue sequences) **and BEFORE G1 gensym (#521) / G2 mangle (#522) begin** (so the
  VM is gone before their native-codegen re-baselines start).
- **Within #524**, crumbs 2, 3, 4 each re-baseline sequentially (they are ordered; crumb N's fixpoint
  verify completes before crumb N+1 begins). Crumbs 1 and 5 touch no `src/` code → no re-baseline.

Determinism is preserved (the compiler is deterministic; removing modules only changes item indices /
gensym counters uniformly), so each `gen2 == gen3` holds. The coverage move keeps the emitted C
byte-identical for surviving functions (§4.3), so the only source-driven diff is the *removal* of the VM
functions — a clean shrink, not a churn.

---

## 7. Regression fixtures to add (native-only — there is no VM to compare against)

All exit codes are the NATIVE binary's; the VM column is intentionally gone.

| Fixture | Input | Expected | Guards |
|---|---|---|---|
| `examples/regressions/wrapper_match_descent/` | a `match` on a single-field wrapper (`Ref`/`Ptr`/`Optional`/`Slice`) that the VM's wrapper-descent bug mis-handled — the shape that blocked #520 | builds + runs, `exit(N)` for a fixed `N`, **asserted** | proves the retired VM's false-failure is now a *pass* natively |
| `examples/regressions/nonnull_bind_asserted/` | the `_ as x` binding class (#517 Finding 1) with an **assertion** that the bound value is the real payload, not null/0 | build fails at the checker (once the checker fix lands) OR runs and the assert holds | replaces the differential's abort-oracle with an asserted native guard |
| `scripts/native_regressions.sh` (or `diff_c_own.sh`) | `teko run examples/regressions/exit_success_path` | builds a debug binary under `target/debug/` and EXECUTES it; exit == the program's own (e.g. 5) | proves `teko run` is native-debug build-and-exec, not VM, not removed |
| CLI (in `scripts/cli_flags_test.sh`) | `teko repl` | exit 2 (M1 = retire) | proves the REPL retired |
| CLI (in `scripts/cli_flags_test.sh`) | `teko --help` | stdout/stderr does NOT contain `repl` or `--vm-gate` (negative grep); DOES contain `run` (as a native debug-run), `test`, `build`, `fmt` | proves the banner matches reality |
| gate (existing) | `teko . -o bin` self-build | exit 0; functions/lines/branches floors pass | proves the coverage move preserved the floors |
| gate (existing) | `teko . -o bin --coverage` | `cobertura.xml` byte-identical to pre-move on the same corpus | proves `cov_cobertura` moved cleanly |
| differential (existing) | `scripts/diff_c_own.sh` over the `exit(n)` corpus | own-native exit == C-native exit | the replacement cross-check still gates |

The two new `examples/regressions/*` projects each need a `.tkp` + a `main.tks` that `teko::exit(N)`s
with an asserted value (the corpus's standard exit(n) shape).

---

## 8. Ritual points (where the full gate must pass)

- **After crumb 1** — CI green with the VM lanes removed and own==C + native gate intact (CI-only ritual;
  no fixpoint).
- **After crumb 2** — full native gate + `teko run` rejection + fixpoint `gen2 == gen3`.
- **After crumb 3** — full native gate + `teko repl` rejection + fixpoint.
- **After crumb 4** — full native gate + own==C differential + coverage-floor re-check +
  `--coverage` byte-identity + fixpoint. **This is the keystone ritual** (the VM is gone here).
- **After crumb 5** — the FULL closing gate; docs match reality; final fixpoint.

Per memory `teko-ci-perf-model`: one native test-gate on the primary platform, the rest compile-only;
each `fix/issue-524-*` sub-PR runs CI; wait for aggregate AllGreen + a fresh seed before the next crumb.

---

## 9. Risks + law tensions (with recommended resolution)

1. **REPL is a hard interpreter consumer (LAW TENSION: "issues are 100%" vs "the REPL needs an
   interpreter").** Resolution (owner-ratified 100%-VM-removal principle): the REPL is *defined* as "a
   session over the VM" and is a dev tool, not a language-contract surface. "Native is the sole engine"
   ⇒ the interpreter goes ⇒ its only front-end goes with it (it cannot repoint to native without a JIT).
   Retiring it does not regress the language. Folded into this issue (not spun off — memory
   `teko-issues-must-be-100-percent`). **Not a HALT** (clean, and now ratified — M1).
2. **Coverage-floor regression from the move.** Mitigated by retargeting the self-exclusion guards to
   `"teko::coverage"` (§4.3) — the coverage machinery stays excluded from its own floors, exactly as the
   VM was. Crumb 4's ritual re-checks all three floors + Cobertura byte-identity.
3. **C-bootstrap reproducibility.** Retiring the C-bootstrap `teko` exe drops the "rebuild from pure C"
   path. Mitigated: the seed has been the downloaded release since the beta wave (memory
   `teko-integrator-seed-discipline`, `teko-beta-seed-mechanism`); CI never invokes cmake. The frozen
   front-end twins remain in the `teko_bootstrap` lib as a compile-regression guard. Conservative
   alternative in M2.
4. **Re-baseline collision with #520/#521/#522.** Mitigated by the strict window in §6 (land after #520
   merges, before G1/G2 start; sequential within the issue).
5. **`--vm-gate` / `TEKO_VM_GATE=1` in external scripts.** Removed OUTRIGHT (M3, ratified — nothing VM
   stays active, not even an inert flag). A CI/dev script still passing `--vm-gate` now sees it fall
   through as an unknown token (the standard "not a project" diagnostic), which is the honest signal the
   VM is gone. In-tree: the retirement removes every `--vm-gate`/`TEKO_VM_GATE` reference from
   `native.yml`/scripts in crumb 1, so nothing in the repo depends on it.
6. **`vm_test.tkt` coverage contribution.** `vm_test.tkt` tested `teko::vm` (excluded from coverage), so
   deleting it removes only self-excluded tests — no other module loses coverage. Verified at crumb 4's
   floor re-check.

**No HALT condition is met.** The only non-VM code trapped in `src/vm/` is the coverage subsystem, which
moves cleanly to `teko::coverage` (§4). No widely-depended-on type is stuck.

---

## 10. Micro-decisions — RATIFIED (owner 2026-07-12)

All five collapse to ONE owner principle: **100% VM removal** — *"se está aposentado, não se mantém na
ativa; tudo que precisar pra remover 100% a VM deverá ser feito."* Nothing VM stays active in any form.
These are ratified, not defaults awaiting a decision; the implementer executes them as written.

- **M1 — Retire the REPL with the VM: RATIFIED = RETIRE.** The REPL is a VM-interpreter front-end (DT3,
  a dev tool) and cannot repoint to native without a JIT. Remove `src/repl/` + the `repl` subcommand +
  its help line. *Reversible:* a future native/JIT REPL is a fresh issue.
- **M2 — Retire the C-bootstrap: RATIFIED = REMOVE.** Drop `add_executable(teko …)` (`main.c` +
  `src/driver.c`, which `#include "vm/vm.h"`) + `TEKO_VM_SOURCES` from CMake, and delete `src/vm/`'s C
  twins. The exe is vestigial (seed = downloaded release; CI never invokes cmake). The `teko_bootstrap`
  lib stays (minus vm.c) as the frozen front-end compile-regression guard. *Reversible:* git.
- **M3 — `--vm-gate` / `TEKO_VM_GATE`: RATIFIED = REMOVE OUTRIGHT.** Not inert, not deprecated-then-
  dropped — gone in this issue. Delete the flag from `project_arg_of`, delete `native_gate_of` /
  `vm_gate_requested`, and strip every `--vm-gate`/`TEKO_VM_GATE` mention from `native.yml`/scripts/help.
- **M4 — `teko test`: RATIFIED = KEEP, native.** The name and contract survive over the #265 native gate
  (§2.1). (Rejected: removing it in favour of `teko . -o bin`.)
- **M5 — `teko run`: RATIFIED = KEEP, native-debug.** The command survives, repointed to a native DEBUG
  build-and-execute (`cargo run` / `go run` shape; §2.2). Only `vm::run` behind it is removed. The
  exit-code contract is preserved; the debug profile adds `-g` + the `.tsym` to today's already-
  unoptimized, assertions-on build.

---

## 11. What remains blocked

Nothing. #524 depends on no open API — the native gate (#265), the own==C differential (#385/#386), and
the downloaded-seed mechanism are all merged. The only sequencing constraint is temporal (§6): land after
#520 merges, before #521/#522 begin. This plan compiles against today's code; the implementer executes
crumbs 1→5 in order the moment #520 is in.
