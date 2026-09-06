# 0.3.1 — `regressor.tkr` AS THE PRIMARY REGRESSION BANK + DEATH OF THE REGRESSION SCRIPTS

> decidido em 2026-07-24; hoje ela só tem a metade `coff`.

**DECISIONS CLOSED — ratified by the owner 2026-07-24 (D1-D9 all sealed at ruling level; the
Feature, D7 = DELETE the drained dirs). Default triage principle going forward, owner's literal
words: "sempre prefira dobrar e remover diretórios drenados."**

DESIGN ONLY (doc). Owner-closed direction (2026-07-24): the `.30` order was executed
INCOMPLETELY — three failures the owner named. There is no counter-argument; the direction
is fixed and this document is the COMO (the executable plan), not the whether.

Umbrella: post-`drop-128` (`0.3.1`). Companion history read first:
`docs/design/regressives-full-stack-0.3.0.30.md` (the `.30` model),
`docs/design/tkb-regression-format.md` (the Gherkin grammar — `.tkr` is its carrier now),
`docs/design/kill-c-pull-forward-0.3.0.30.md` §5 (the FFI test-lane suite),
`docs/design/null-union-corpus-pivot-0.3.0.30.md` (the null-union matrix seed).

Architect does NOT write product code and does NOT edit `.github`/`scripts` — §7 is the exact
delta the owner/integrator applies there. All Teko code snippets below are copy-paste-ready in
full W15 Javadoc; implementers copy them verbatim.

---

## 0. The three failures (verified in-tree on `origin/main`, 2026-07-24)

1. **`regressor.tkr` is DECLARATIVE, never executed.** The repo-root `regressor.tkr`
   (`git show origin/main:regressor.tkr`) is four `# verified-by:` doc scenarios with NO
   `When` verb. The runner honest-SKIPS it via `dir_is_self_project` (`src/build/regression.tks`
   @origin/main ~L653) and the `TEKO_IN_REGRESSION` sentinel. It documents a contract; it is
   not the system-under-test compiling anything.
2. **231 fixture dirs, 6 executable specs.** `examples/regressions/` holds 231 directories;
   only 6 carry a `.tkr` (`nullfield_variant_{plain,boxed,nested,recursive,writeback}`,
   `rt_behavior`). The remaining ~225 are marker-file fixtures (39 `EXPECT_COMPILE_FAIL`,
   46 `EXPECT_EXIT`, plus plain no-marker) consumed ONLY by the shell harnesses — never by the
   native runner.
3. **The regression shell scripts are still alive** and wired into `native.yml`:
   `compile_fail_regressions.sh`, `positive_regressions.sh`, `native_regressions.sh`,
   `crossmodule_regressions.sh`, `diff_c_own.sh` (gen1-checks
   L286-390; removed in 0.3.1
   lane run regressions NATIVELY; `regressives-full-stack-0.3.0.30.md` §4 instead KEPT four of
   them ("distinct invocation / backend differential"). That divergence is failure #3.

The `.30` original order (owner): reduce regressors to a FEW per
`artifact-kind × target × FFI`, **≤10 including the compiler**; delete the `.sh` scripts and the
individual CI lanes; ONLY the test lane runs regressions, NATIVELY. This document executes THAT.

### 0.1 The regressive-primacy hierarchy — RULING (owner, 2026-07-24)

> **"prefira regressivo a teste unitário: regressivo da Teko (`regressor.tkr`) > regressivos
> [de projeto] > tkt; prefira matar testes tkt do que testes regressivos."**

Binding order of preference when a behavior is asserted in more than one place:
**`regressor.tkr` (the Teko-self regressive) > project-regressives > `.tkt` unit tests.** When a
`.tkt` and a regressive DUPLICATE an assertion, the `.tkt` dies, not the regressive. This
INVERTS the `.30`-era triage (which preferred killing regressives and routing rejections to
checker `#test`s). Consequences threaded through this doc: the §5 "die by duplication with
`.tkt`" group is REVERSED — those fixtures LIVE as regressive scenarios and the duplicated
`.tkt` die (§5, §5.2); the diagnostic-less rejections become diagnostic-PINNED regressive
scenarios, not `#test`s (§3 F1). The one honest coupling this creates — MEASURED coverage —
is resolved WITHIN the ruling in §5.1 (a new runner crumb), not against it.

**Ground truth confirmed:** on `origin/main`, `.tkr` IS already the Gherkin BDD format
(`Feature`/`Scenario`/`Scenario Outline`/`Given`/`When`/`Then`/`Examples`), parsed to
`[]TkrFeature` and lowered to the frozen `Tkr` + `check_run`/`check_compile_fail` verdict layer.
The `.tkb` extension of the `.30` doc has already retired into `.tkr` (D1 below is therefore a
confirmation, not a change). `teko.tkp` today declares `regression = ["examples/regressions"]`
(directory + discovery) — the exact form the owner's 2026-07-24 ruling kills (§1).

---

## 1. FOUNDATION — manifest becomes an explicit ordered FILE-PATH list (owner ruling 2026-07-24)

The load-bearing change; everything else is built on it. `[tests] regression` stops being a
directory that the compiler walks and becomes an **explicit, ordered list of relative `.tkr`
file paths**:

```toml
[tests]
regression = [
  "regressor.tkr",
  "examples/regressions/crossmodule/crossmodule.tkr",
  "examples/regressions/ffi_import/ffi_import.tkr",
  "examples/regressions/ffi_export/ffi_export.tkr",
  "examples/regressions/lib_static/lib_static.tkr",
  "examples/regressions/lib_shared/lib_shared.tkr",
  "examples/regressions/cwd_build/cwd_build.tkr",
]
gate = true
```

Consequences (all ratified by the owner; documented, not re-litigated):

- **No globs, no directory walk, no hierarchical `.tkr` search.** Execution order = list order:
  deterministic, auditable in the manifest, diffable.
- **These functions DIE** in `src/build/regression.tks`: `discover_source`, `dir_tkr_files`,
  `dir_first_tkr`/`dir_first_spec` (if present), `tkr_basename_lt`, `tkr_insert_sorted`,
  `dir_is_self_project`. `run_one_regressor(dir)` (walks a dir's `.tkr` set) collapses into a
  direct `run_one_tkr(file)` per listed path.
- **Side effect the owner flagged:** the pending follow-up "swap `dir_tkr_files`'s insertion
  sort (`tkr_basename_lt`/`tkr_insert_sorted`) for `teko::sort::sort_str`" is now MOOT — the
  function is deleted, order is the manifest's. (`teko::sort::sort_str` does not exist in-tree;
  it is never needed for this.)
- **Honest validation (M.3):** a listed path that does not exist is a MANIFEST ERROR — the
  phase halts non-zero with the offending path named. NEVER a silent skip. (Old behavior: a
  non-directory source yielded `[]` — a silent no-op. That is the exact failure mode the owner
  is closing.)
- **The `Manifest` field is renamed** `test_regression_dirs` → `test_regression_files` (its
  type stays `[]str`) so the schema reads honestly. Touch sites:
  `manifest.tks` (field + the `regression` key reader + both `Manifest{…}` constructors),
  `project.tks` (`test_project` guard `fe.manifest.test_regression_files.len > 0`),
  `regression.tks` (`run_regression_sources` iterates files, not sources).

Field doc-comment (copy-paste):

```teko
    /**
     * test_regression_files — `[tests] regression` — the EXPLICIT, ORDERED list of `.tkr`
     * regressor FILE paths (relative to the project root) the `teko test` lane runs, in list
     * order. There is NO directory discovery and NO glob: each entry names ONE `.tkr` file.
     * `[]` when the `[tests]` section (or the key) is absent → the regression tier is OFF. A
     * listed path that does not resolve to a readable file is a MANIFEST ERROR (M.3), never a
     * silent skip.
     *
     * @since 0.3.1
     */
    test_regression_files: []str
```

New core loop (copy-paste — replaces the discover-and-walk body of `run_regression_sources`):

```teko
/**
 * run_regression_sources — the CORE regression run (no anti-recursion sentinel): run every
 * `.tkr` file named in `m.test_regression_files`, IN LIST ORDER, on the host lane, printing a
 * report line and tallying failures. Each entry is a FILE path (relative to the project root);
 * its parent directory is the regressor project dir (`regr_dir`) a `built and run` scenario
 * compiles. A listed path that does not resolve to a readable file is a MANIFEST ERROR (M.3):
 * the run stops non-zero with the path named — never a silent skip. Returns 1 when any
 * regressor failed (or a path was missing) AND `m.test_gate`, else 0. Called directly by tests;
 * the `teko test` driver wraps it in `run_regression_phase`.
 *
 * @param exe  the absolute path to this compiler binary (compiles each regressor / snippet)
 * @param m    the resolved manifest (its `test_regression_files`/`test_gate` drive the run)
 * @return     0 when the tier passes (or is advisory), 1 when a failure/missing-path gates it
 * @since 0.3.1
 */
fn run_regression_sources(exe: str, m: Manifest): i32 {
    let work = "bin/.regr-work"
    match mkdir_p(teko::str::concat(work, "/")) { error => { }; null => { } }
    mut fails: u64 = 0
    mut si: u64 = 0
    loop {
        if si >= m.test_regression_files.len { break }
        let path = m.test_regression_files[si]
        match teko::io::read_file(path) {
            error => {
                teko::io::println($"teko: regression FAIL {path} — listed regressor file does not exist (M.3)")
                fails = fails + 1
                si = si + 1
            }
            str => {
                let oc = run_one_tkr(exe, dirname_of(path), basename_of(path), work ~ "/" ~ regr_sanitize(path))
                report_regressive(path, oc)
                if !oc.ok && !oc.skipped { fails = fails + 1 }
                si = si + 1
            }
        }
    }
    teko::io::println($"teko: regressions {m.test_regression_files.len} run, {fails} failed")
    if fails > 0 && m.test_gate { return 1 }
    0
}
```

`basename_of`/`dirname_of` already exist in the module (`dirname_of` is used by
`run_captured`); add `basename_of` if absent (trivial, mirror of `dirname_of`).

### 1.1 Anti-recursion in the new world (the root `regressor.tkr` now EXECUTES)

`dir_is_self_project` was a DISCOVERY heuristic (skip a dir holding `regressor.tkr`). With the
root file listed EXPLICITLY and EXECUTING, the guard is no longer "skip"; it becomes a
STRUCTURAL property plus the sentinel backstop:

- **Structural:** every `regressor.tkr` scenario is an INLINE-source or explicit-`source`
  compile (§2a) — the runner NEVER builds the containing directory's `.tkp` for it. Since the
  root's containing dir IS the compiler project, a `built and run` scenario at the root with NO
  inline/explicit source would try to rebuild the compiler — so the runner REFUSES it
  (`guard_no_project_build_at_root`, honest error), forcing self-project scenarios to be
  snippet-scoped. The compiler-as-SUT compiles SNIPPETS, never itself.
- **Declarative meta-scenarios stay skip.** The four existing `# verified-by:` scenarios
  (self-host build, own gate, gen1==gen2 fixpoint, own==C) CANNOT execute without
  recursion; they remain verb-less and are honest-SKIPPED as living documentation (§2c).
- **Backstop unchanged:** `TEKO_IN_REGRESSION` in `run_regression_phase` still refuses a nested
  `teko test .` re-entry. The runner only ever `teko build`s a snippet/project — never
  `teko test`s — so a recursion is structurally impossible; the sentinel is the belt.

```teko
/**
 * guard_no_project_build_at_root — REFUSE a scenario that would compile the project ROOT itself
 * (the compiler) as a regressor. A root-listed `regressor.tkr` scenario MUST be snippet-scoped
 * (inline `source` / explicit `source` file); a `built and run` with no source at `regr_dir ==
 * "."`/"" would rebuild the compiler and recurse. Returns an honest error in that case, else
 * null. The structural half of the self-project anti-recursion guard (§1.1); the
 * `TEKO_IN_REGRESSION` sentinel is the backstop.
 *
 * @param regr_dir  the regressor directory (dirname of the listed `.tkr`)
 * @param t         the lowered scenario
 * @return          an error when the scenario would build the project root, else null
 * @throws          when `regr_dir` is the project root and the scenario has no inline/explicit source
 * @since 0.3.1
 */
fn guard_no_project_build_at_root(regr_dir: str, t: Tkr): null | error {
    let at_root = regr_dir == "." || regr_dir.len == 0
    let has_src = t.source_inline.len > 0 || t.source_file.len > 0
    if at_root && t.kind != TkrKind::CompileFail && !has_src {
        return error { message = "regressor.tkr scenario would rebuild the compiler root — use an inline `source` snippet" }
    }
    null
}
```

---

## 2. Runner extensions (crumbs, with copy-paste signatures)

The bank needs four capabilities the runner lacks today. Each is an independent, gate-able
crumb (§6). None needs a new runtime symbol — env injection and the qemu run-wrappers all
ride the existing `sh -c` command string and `teko::process::run`/`teko::io`/`teko::fs`.

### 2a. Inline source — a snippet in the `.tkr` itself (no on-disk project)

New Gherkin nouns: `Given source inline = """<teko>"""` (a docstring holding a whole `.tks`
program) and the already-specced `Given source = "cases/<f>.tks"` (an explicit standalone file,
relative to `regr_dir`). Both compile ONE standalone source with gen1 (`teko <path> -o bin
--no-verify`), never the dir's `.tkp`. Inline is the workhorse of `regressor.tkr`: it lets the
compiler be the SUT over hundreds of tiny programs with ZERO on-disk project per case.

New `Tkr` fields (additive — `Tkr` + the check layer stay frozen contract, this only adds
inputs the lowering fills):

```teko
    /** an inline Teko source program (from `Given source inline`); "" when the scenario builds a dir `.tkp` or an explicit `source` file. */
    source_inline: str
    /** an explicit standalone source file, relative to `regr_dir` (from `Given source`); "" when none. */
    source_file: str
```

```teko
/**
 * compile_inline_source — build a SINGLE inline Teko program `src_text` with gen1 (`exe`),
 * writing it to a scratch `<prefix>.src.tks` first, then `teko <that> -o bin --no-verify` under
 * `env`, capturing the build's exit/stderr (the stderr carries the diagnostic a `compilation
 * fails` inline scenario pins). No on-disk project is touched — this is the compiler-as-SUT over
 * a snippet (§1.1). The scratch file's directory is created if missing.
 *
 * @param exe       the absolute path to this compiler binary (gen1)
 * @param src_text  the inline `.tks` program to compile
 * @param env       per-scenario child env (`K=V` tokens; §2b) — e.g. `TEKO_BACKEND=native`
 * @param prefix    the scratch-file prefix for this compile
 * @return          the captured build outcome (+ the produced binary path in `stdout`-adjacent scratch)
 * @since 0.3.1
 */
fn compile_inline_source(exe: str, src_text: str, env: []str, prefix: str): CapResult {
    let srcp = prefix ~ ".src.tks"
    match mkdir_p(teko::str::concat(dirname_of(srcp), "/")) { error => { }; null => { } }
    match teko::io::write_file(srcp, src_text) { error => { }; null => { } }
    mut argv: []str = teko::list::push(teko::list::empty(), exe)
    argv = teko::list::push(argv, srcp)
    argv = teko::list::push(argv, "-o")
    argv = teko::list::push(argv, prefix ~ ".bin")
    argv = teko::list::push(argv, "--no-verify")
    run_captured_env(argv, "", env, prefix ~ ".compile")
}
```

The single-file compile path (`teko <file> -o bin --no-verify`) is already proven — the `.30`
`compile_regressive_source` used exactly this shape; `compile_inline_source` only adds the
"write the snippet first" step.

### 2b. Per-scenario ENV injection — `Given backend` / `Given env` (the own==C lever)

New nouns: `Given backend = "own"` (sugar → `TEKO_BACKEND=native`), `Given backend = "c"`
(the default; explicit for a paired differential row), and the general
`Given env = ["TEKO_TARGET=x86_64-windows", "FOO=bar"]`. Env applies to BOTH the compile and the
run. This is what lets the own-vs-C BACKEND differential be EXPRESSED as
scenarios: an `Examples` table over `backend` runs the SAME snippet under each backend and
asserts the SAME absolute expected exit/stdout — if own passes N and C passes N, then own==C
TRANSITIVELY, with no bespoke diff harness.

New `Tkr` fields:

```teko
    /** per-scenario child env (`K=V` tokens) applied to compile AND run; from `Given env`/`Given backend`. [] when none. */
    env: []str
```

Env injection rides the existing `sh -c` string — prepend `env K=V …` to the command; no new
runtime symbol, no parent-process `set_var` leak:

```teko
/**
 * run_captured_env — `run_captured` with a per-child environment: run `argv` under `env`
 * (`K=V` tokens) with `stdin_data` on its stdin, capturing exit/stdout/stderr via the same
 * `/bin/sh -c` redirection. Implemented by prefixing the command with `env K=V …` (each token
 * single-quoted), so the environment reaches ONLY the child — the runner's own env is untouched
 * (no `teko::env::set_var` leak across scenarios). `env == []` is byte-identical to the old
 * `run_captured`.
 *
 * @param argv        the command to run (argv[0] is the executable)
 * @param stdin_data  bytes to feed the child's stdin
 * @param env         the child environment as `K=V` tokens ([] = inherit only)
 * @param prefix      the scratch-file path prefix (its parent directory is created)
 * @return            the captured exit/stdout/stderr
 * @since 0.3.1
 */
fn run_captured_env(argv: []str, stdin_data: str, env: []str, prefix: str): CapResult {
    mut cmd = ""
    mut k: u64 = 0
    if env.len > 0 { cmd = "env" }
    loop {
        if k >= env.len { break }
        cmd = cmd ~ " " ~ sh_squote(env[k])
        k = k + 1
    }
    if env.len > 0 { cmd = cmd ~ " " }
    // … the remainder is the existing run_captured body (sh_join(argv) + redirections). …
    run_captured_cmd_prefixed(cmd, argv, stdin_data, prefix)
}
```

(`run_captured` becomes `run_captured_env(argv, stdin, [], prefix)`; extract the shared
redirection body into `run_captured_cmd_prefixed` to keep both flat and cyclomatic-low.)

### 2c. Verb-less scenarios are honest-SKIPPED (living documentation)

A scenario with NO `When` entry verb is a DECLARATIVE doc scenario (the four R0
`# verified-by:` meta-scenarios). It lowers to a skip with the `# verified-by:` note as its
reason — reported as `skip`, never a pass or fail. This is how the self-host / fixpoint /
own==C meta-contract stays IN `regressor.tkr` as documentation while the executable language
matrix lives alongside it.

```teko
/**
 * tkb_scenario_is_declarative — true when a scenario has NO `When` entry verb: it is a
 * documentation-only meta-scenario (an R0 `# verified-by:` contract) and is honest-SKIPPED,
 * never executed. Lets `regressor.tkr` carry the self-host/fixpoint contract as living
 * documentation beside the executable language matrix (§1.1).
 *
 * @param sc  the parsed scenario
 * @return    true when the scenario declares no `When` verb
 * @since 0.3.1
 */
fn tkb_scenario_is_declarative(sc: TkbScenario): bool { sc_when_verb(sc).len == 0 }
```

### 2d. What the six scripts do, mapped one-by-one (runner capability OR death by duplication)

| script | what it does today | fate |
|---|---|---|
| `compile_fail_regressions.sh` | 39 `EXPECT_COMPILE_FAIL` dirs: build, assert exit≠0, optional `EXPECT_STDERR:` substring | **DIES.** ALL 39 rejections → inline `When compilation fails` + `Then diagnostic` scenarios in `regressor.tkr` F1 (§3). Under the 0.1 inversion the ~33 formerly-diagnostic-less ones are NOT dropped: each gets its EMITTED diagnostic captured and PINNED (a stronger oracle than the old exit-only marker OR a checker `#test`). Any duplicating checker `.tkt` rejection test then dies (§5.2). |
| `positive_regressions.sh` | 46 single-project `EXPECT_EXIT`: build + run, assert exit | **DIES.** Each language/const fixture → an inline `When built and run` + `Then exit` scenario in the relevant `regressor.tkr` Feature (§3). |
| `native_regressions.sh` | #64 `cd <proj> && teko build .` CWD-relative runtime shape, on seed + gen1 | **DIES; capability moves to the runner** as a new entry verb `When built in place` (compile with CWD == `regr_dir`, `teko build .`). Kept as ONE project-regressor (`cwd_build.tkr`, §4) — a distinct INVOCATION shape, not a language feature. |
| `crossmodule_regressions.sh` | dep→`.tkl`→provision `packages/`→build consumer→run | **DIES; capability moves to the runner** as `Background: Given dependency "<subdir>"` (§2e). Kept as one project-regressor (`crossmodule.tkr`). The heaviest new runner crumb. |

### 2e. Dependency provisioning — `Background: Given dependency "<subdir>"`

The crossmodule capability the runner lacks: build a `kind=package` sub-project to a `.tkl`,
drop it into a scratch `packages/`, then compile the consumer against it. Exactly what
`crossmodule_regressions.sh` does in bash, now native.

```teko
/**
 * provision_dependency — build the `kind = "package"` sub-project at `regr_dir/<dep_subdir>`
 * with gen1 (`exe`) to a `.tkl`, then copy that `.tkl` into `scratch_packages/` (created if
 * missing) so a subsequent consumer compile resolves it. The committed fixture tree is never
 * written — the `.tkl` and the consumer build live under scratch. Mirrors
 * `crossmodule_regressions.sh`'s build-dep → provision step. Honest error on a missing dep
 * manifest / a failed dep build / no `.tkl` produced (M.3).
 *
 * @param exe               the absolute path to this compiler binary (gen1)
 * @param dep_dir           the dependency sub-project directory (`regr_dir/<dep_subdir>`)
 * @param scratch_packages  the scratch `packages/` dir to provision the built `.tkl` into
 * @param prefix            the scratch-file prefix for the dep build
 * @return                  the provisioned `.tkl` path, or an error
 * @throws                  on a missing/malformed dep manifest, a dep build failure, or no `.tkl`
 * @since 0.3.1
 */
fn provision_dependency(exe: str, dep_dir: str, scratch_packages: str, prefix: str): str | error { /* crumb C4 */ }
```

### 2f. `Then object well-formed` — the surviving leaf tools

`check_elf.sh` / `check_macho.sh` / `check_coff.sh` encode real host-toolchain knowledge
(`readelf`/`otool`/`llvm-readobj`, `ld -r`) that is not worth reimplementing in Teko and is not
orchestration. They SURVIVE as leaf tools INVOKED BY a scenario. A `Then object well-formed`
verb picks the checker from the effective target and shells it against the emitted `.o`; an
absent host tool is an honest per-scenario skip (never a fabricated pass).

```teko
/**
 * check_object_wellformed — post-build cross-check the emitted `<name>.o` with the host
 * toolchain leaf tool for the effective target: `scripts/check_elf.sh` (ELF),
 * `scripts/check_macho.sh` (Mach-O), `scripts/check_coff.sh` (PE/COFF). A `Then object
 * well-formed` scenario runs this after a `backend = "own"` build. A rejected object is a FAIL;
 * an absent tool is an honest SKIP with a named reason (never a pass). These three scripts are
 * the ONLY `scripts/*` that survive the regression-script cull — leaf tools, not orchestration.
 *
 * @param objp    the emitted object path (`<oout>/<name>.o`)
 * @param target  the effective os-arch (selects elf/macho/coff)
 * @return        the verdict (ok / FAIL / skip-when-tool-absent)
 * @since 0.3.1
 */
fn check_object_wellformed(objp: str, target: str): RegrOutcome { /* crumb C3 */ }
```

### 2g. Target selection — `Given target`, honest os-arch skip

A `Given target = "<os-arch>"` sets `TEKO_TARGET` in `env`. When the target's own toolchain is
absent the scenario honest-SKIPS with a named reason and the os-arch in the label (so a macOS dev
host skipping the qemu lane is honest, not a false green). On a provisioned CI lane,
`REGRESSION_REQUIRE_TOOLS` turns the skip into a hard failure (§7).

---

## 3. The primary bank — `regressor.tkr` populated

`regressor.tkr` becomes the compiler-as-SUT bank: Features by area, almost entirely INLINE-source
scenarios (`Given source inline` + `When built and run` / `When compilation fails`). One file,
listed FIRST in `teko.tkp`. Feature breakdown with scenario-count estimates:

| Feature | content | est. scenarios |
|---|---|---|
| **F0 meta (declarative)** | the four R0 `# verified-by:` contracts (self-host build, own gate, gen1==gen2 fixpoint, own==C) — verb-less, honest-SKIPPED (§2c) | 4 (skip) |
| **F1 diagnostics / syntax** | ALL 39 `EXPECT_COMPILE_FAIL` rejections as inline `When compilation fails` + `Then diagnostic` — the ~6 already-pinnable PLUS the ~33 formerly "diagnostic-less" ones, each now with its EMITTED diagnostic ASSERTED (§5.2 inversion: a pinned regressive is stronger than the old exit-only marker AND than a checker `#test`) | ~39 |
| **F2 semantics / types** | the language corpus subsumed from the plain + positive `EXPECT_EXIT` dirs, grouped: traits/derive (~10), classes/oop (~8), generics/monomorph (~6), match/patterns (~6), closures/lambda (~4), loops/labels (~5), str/char ops (~4), arena/ownership (~5), misc (~10) | ~50 |
| **F3 capabilities — NULL-UNION MATRIX** | the COMPLETE `shapes × recursion × FFI` matrix (§3.1) — the 4th incomplete pass must not exist | ~22 |
| **F4 consts / comptime** | `comptime_fold_*` (5), `const_agg_*` (3), `member_const_*` (5), `const_scalar_inline`/`const_pub_export_survives` | ~12 |
| **F6 rt_behavior** | the runtime-operand law: op-family `Scenario Outline`s over fold-opaque argv (arith/bitwise/shift-masked/compare/concat/foreach/`ids[i]=v`) — folds the existing `rt_behavior.tkr` | ~12 |
| **F7 host_cli_io** | observable host surface: env/stdin/argv (incl. a `--` tail) + a file-IO roundtrip + one HEX-docstring byte golden | ~6 |

`regressor.tkr` total ≈ **163 scenarios across 8 Features** (4 skipped meta + ~159 executable;
F1 grew from ~6 to ~39 under the 0.1 inversion — the ~33 formerly-dropped rejections now live
here diagnostic-pinned), each a compiled-and-run (or compiled-and-rejected) snippet — the
compiler exercising the whole language on ~159 real tiny programs per `teko test .`, on every CI
OS, NATIVELY.

### 3.1 The NULL-UNION matrix (complete — F3)

The existing 5 `nullfield_variant_*` `.tkr` cover only part of the space; the owner's mandate is
the COMPLETE matrix so a fourth partial pass cannot happen. Enumerated (each an inline
`built and run` unless marked compile-fail):

- **shapes × value:** `scalar` (`u64 | null`), `struct` (`Point | null`), `class` (`Node | null`),
  `slice element` (`[]T` where `T = X | null`), `nested` (`(A | null) | null` collapsed / a
  field-of-a-field), `interface` (`Iface | null`), `base/widen` (`Base | null` narrowed from a
  derived). — 7
- **operation × null:** construct-null, construct-present, `match` narrow (both arms), writeback
  (mutate a `… | null` field), forward-un-narrowed (return a whole `… | null`). — 5, applied to
  the load-bearing shapes.
- **recursion:** self-referential linked-list walk (`cur.next` where `next: Node | null`),
  boxed recursive variant (folds `nullfield_variant_recursive`). — 2
- **FFI:** `ptr<T> | null` null test, `uptr | null` null test (from
  `kill-c-pull-forward-0.3.0.30.md` §5.2 `ptr_null_union`, `backend = "own"`). — 2
- **compile-fail (the strict boundary):** `ptr<T>?` rejected, `void*` deref rejected,
  bare-`null`-into-non-nullable rejected. — 3

≈ **22 scenarios.** The 5 existing `nullfield_variant_*.tkr` FOLD in here as inline scenarios
(their dirs die, §5).

---

## 4. The project-regressors (≤10 including the compiler)

INLINE snippets cover everything expressible as one standalone `.tks`. What CANNOT be a single
snippet — multi-module, a real artifact kind, an FFI C-consumer, a distinct invocation shape —
stays as a small on-disk project-regressor, listed by path. The **≤10** set (owner's
`artifact-kind × target × FFI`):

1. **`regressor.tkr`** — the compiler / native `binary` kind / all backends (own, C,
2. **`crossmodule.tkr`** — `package` (`.tkl`) artifact + multi-module: dep→`.tkl`→consumer via
   `Background: Given dependency` (§2e). Folds the 1 `dep/consumer` fixture.
3. **`ffi_import.tkr`** — FFI-in: `extern fn … = "SYM" from "lib"` + libc link + a C stub
   (`kill-c` §5.2 A). `backend = "own"`, system-`ld` link.
4. **`ffi_export.tkr`** — reverse-FFI (FFI-out): `exp fn … = "SYM"` + `consumer.c` compiled by
   host `cc`, linked by system `ld`, run + assert (`kill-c` §5.2 B). Includes `.h` emission.
5. **`lib_static.tkr`** — `kind = "static"` (`.a`) artifact built + consumed.
6. **`lib_shared.tkr`** — `kind = "shared"` (`.so`/`.dylib`/`.dll`) artifact built + linked.
7. **`cwd_build.tkr`** — the #64 `cd <proj> && teko build .` CWD-relative invocation shape
   (`When built in place`, §2d). Folds `native_regressions.sh`.
   (§2g) kept separate from `regressor.tkr` because its host tooling and honest-skip surface are
   distinct. (ALTERNATIVE: fold into F5 — recommended AGAINST, to keep `regressor.tkr` toolchain-free.)

+ os-arch routing) rather than becoming projects #9/#10 — recommended, keeps the set tight.
(ALTERNATIVE if the owner wants them isolated: `` (#9) + `coff_target.tkr` (#10),
still ≤10.)

FFI fixtures that ARE single-snippet (`extern_macro_const_flag`, `ptr_deref_index_arrow`,
`ptr_arith_addr_of`, the `*_rejected` compile-fails) fold into `regressor.tkr` F3/F5; only the
link-and-C-consumer cases need the `ffi_import`/`ffi_export` projects.

---

## 5. Consolidation / removal map (231 dirs → mechanical triage)

Under the 0.1 inversion NO fixture "dies by duplication with `.tkt`" — a fixture is a
REGRESSIVE, which now outranks the unit test. Every fixture either becomes an inline scenario
(a) or a project-regressor (b); the duplicating `.tkt` is what dies (§5.2). Per-directory triage
criterion (mechanical, ordered — first match wins):

1. `dep/` + `consumer/` pair → **(b) project-regressor** `crossmodule.tkr` (absorbs the fixture).
2. FFI needing a C stub / `consumer.c` / link (`extern_fn_libc_call`, `repr_c_struct_byval`,
   `cabi_callback_*`, `revffi_*`, `variadic_printf`) → **(b)** `ffi_import`/`ffi_export`.
3. `own_*` (backend-differential corpus, ~18) → **(a) inline** F5 `backend`-`Examples` scenario.
5. ANY `EXPECT_COMPILE_FAIL` (all 39) → **(a) inline** F1 `compilation fails` + `diagnostic`.
   The ~6 already-pinnable keep their message; the ~33 formerly-"diagnostic-less" get their
   EMITTED diagnostic captured and PINNED (0.1 inversion — a diagnostic-pinned regressive is
   STRONGER than the old exit-only marker AND than a checker `#test`; §5.2). None is dropped.
6. single-project `EXPECT_EXIT` / plain no-marker, pure language feature, self-contained (~150)
   → **(a) inline** F2/F3/F4 `built and run` + `exit` scenario.
7. `kind = static/shared` artifact fixture → **(b)** `lib_static`/`lib_shared`.
8. the #64 CWD fixture (`char_ops` today) → **(b)** `cwd_build.tkr`.

Approximate counts over the 231 (revised under 0.1):

| group | fate | count |
|---|---|---|
| (a) inline scenarios in `regressor.tkr` (F1-F7) | language / const / null-union / own-backend / rt / ALL 39 diag-pinned rejections | ~213 |
| (b) survive as one of the ≤10 project-regressors | crossmodule (1) + ffi (build 2) + lib_static/shared (build 2) + cwd_build (1) | ~6 dirs consolidated into 6 projects |
| (c) die by duplication | **none** — regressives outrank `.tkt`; the `.tkt` die instead (§5.2) | 0 |

Net: **231 dirs → 1 `regressor.tkr` (~159 executable scenarios) + 7 project-regressors + 0
dropped.** After migration the drained `(a)` fixture dirs are DELETED (their snippets now live
inline). See R5 (§8) for the sanitizer-corpus coupling.

### 5.1 Coverage verdict — regressions do NOT feed the metric today (RESOLVE within the ruling)

The owner's honest-verification question: does the delta / floor coverage metric count
REGRESSION execution, or only `.tkt`? Traced in-tree — **only `.tkt` (`#test`) execution feeds
it; regression execution contributes ZERO today.** The mechanism:

- Coverage is a STATIC WALK of the assembled compiler program `fe.prog`
  (`coverage::coverage_pct` / `line_coverage_pct` / `branch_coverage_pct`), the covered set
  filled by RUNTIME marks — `cov_mark`/`cov_enter`/`cov_branch` host side-channels
  (`src/checker/scope.tks` ~461-470) emitted INTO the program and merged from a covfile.
- The `#test` path emits an INSTRUMENTED test binary (`codegen::tk_emit_c_test(prog, true)`),
  builds it, sets `TEKO_TKCOV=<covfile>`, RUNS it, then `coverage::cov_merge(covfile)`
  (`src/build/project.tks` ~1749-1753 for the gate run; ~2711-2716 for per-test attribution).
  Executing a `#test` marks every COMPILER-SOURCE line it calls — that is the whole coverage
  signal.
- The regression phase runs INSIDE that test binary, but a regressor compiles its snippet by
  SHELLING an EXTERNAL compiler — `self_exe_path()` (`src/build/regression.tks` ~487) invoked
  `teko <file> -o bin --no-verify` (release, UN-instrumented; `compile_regressive` /
  `compile_regressive_source` argv). That child process writes no `TEKO_TKCOV` and is never
  `cov_merge`d, so the compiler code paths its snippet exercises are NOT marked in `fe.prog`.

**Consequence:** a `.tkt` is currently the ONLY thing that marks the compiler-source lines it
exercises. Naively deleting it under 0.1 would DROP measured coverage below the D4 floors
(`coverage_floor_misses`, project.tks ~1629) — the gate would fail. So the ruling cannot be
executed as-is without first making regressions COUNT.

**Resolution WITHIN the ruling (new runner crumb, sequenced BEFORE the `.tkt` sweep):** make
regression execution contribute to `fe.prog` coverage, reusing the EXISTING seam
(`TEKO_TKCOV` env + `coverage::cov_merge`) — no new coverage machinery:

- **Cov-A (recommended).** In the `teko test` lane, build the regression `exe` as a
  COVERAGE-INSTRUMENTED compiler (the same `tk_emit_c_test(prog, true)` instrumentation the
  test binary already carries), and give each snippet-compile its OWN `TEKO_TKCOV=<covfile-i>`;
  after the phase, `coverage::cov_merge` every child covfile into `fe.prog` BEFORE
  `coverage_floor_misses` reads the percentages. Then every regressor compile marks the exact
  compiler-source lines its construct exercises — regressions become first-class measured
  coverage, and a `.tkt` deleted in favor of an equivalent regressive is coverage-NEUTRAL.
- **Cov-B (alternative, not recommended).** Compile snippets IN-PROCESS (the instrumented test
  binary calls the compiler pipeline directly, marks automatically), still shelling only the RUN
  for exit/stdout. More invasive: global-state/arena reuse + weaker error isolation across
  snippets. Rejected in favor of Cov-A's process isolation.

This is crumb **C-cov** (§6), a HARD PREREQUISITE of the `.tkt` sweep. The sweep never runs
until regressions feed coverage, so the floors hold throughout and the ruling is honored without
a measured-coverage regression. (Reweighting/lowering the floors is explicitly NOT chosen — it
would HIDE real coverage rather than move it onto the preferred oracle.)

### 5.2 The `.tkt` sweep — what dies (themes, not an exhaustive list)

`.tkt` inventory in-tree: **74 files, ~2230 `#test` functions.** The sweep kills a `#test`
ONLY when (i) an equivalent `regressor.tkr` / project scenario asserts the SAME observable
behavior, AND (ii) after C-cov, the compiler lines that `#test` covered are re-covered by that
scenario's instrumented compile. Both conditions are mechanical and checkable per-`#test`.

Themes that DIE (duplicated by the bank — an OBSERVABLE end-to-end regressive equivalent exists):

- **checker rejections / accepts** (`src/checker/*.tkt`, 9 files): the reject-this-construct and
  accept-this-construct `#test`s whose verdict is now a diagnostic-pinned F1 scenario or an
  F2/F3 built-and-run — the largest single block.
- **codegen / emit exit-behavior** (`src/codegen`, `src/emit`, some `src/build`): "this program
  lowers and exits N" `#test`s → F2/F5 `built and run`.
- **comptime / const fold** → F4; **null-union representation** → the F3 matrix;
  **own-native exit** subset of `src/backend/*.tkt` → F5 `backend`-Examples.

Themes that STAY (NO regressive equivalent — they assert INTERNAL function outputs, not
observable program behavior; the ruling's "prefer killing `.tkt`" applies to DUPLICATES, not to
these):

- **backend encoder / regalloc / isel BYTE goldens** (`src/backend`, 23 files;
  `src/lir`, 3) — a regressive asserts exit/stdout equality (own==C), never the exact emitted
  bytes; the goldens are irreplaceable.
- **pure-library units** (`math`, `crypto`, `compress`, `collections`, `encoding/*`, `text`,
  `regex`, `fmt`, `io`, `list`, `iter`, `time`, `url/json/csv/base64` ≈ 18 files) — pure
  functions with no compiler-as-SUT path.
- **infra** (`assert`, `runtime`, and the `manifest`/`project`/`regression-runner` `#test`s in
  `src/build`) — they test the harness itself.

**Estimated sweep magnitude:** on the order of **~8-12 `.tkt` files substantially thinned or
emptied** (the checker-rejection, codegen/emit-exit, comptime, null-union, and own-native-exit
blocks), roughly **~300-500 `#test` functions retired** of the ~2230 — i.e. the observable-
behavior fraction, NOT the byte-golden / pure-lib / infra majority. The exact per-`#test` cut is
mechanical (conditions i+ii above), performed during the sweep crumb, file-by-file, each removal
gated by C-cov keeping the floors green. This is an ESTIMATE by theme, as requested — not a
committed line-item list.

---

## 6. Crumb sequence (runner first, populate second, remove last)

`[RITUAL]` = full gate: seed→gen1 + `./bin/teko test .` green on all three CI OSes AND the
tests.yml duration delta measured (R1). Each crumb builds under the previous seed and uses only
`str`/`list`/`match`/`struct`/`enum`/`loop` + the already-shipped `.tkr` Gherkin parser — all in
the seed; nothing sequences ahead of its seed feature.

- **C0 — FOUNDATION: manifest = explicit file list.** `manifest.tks` (rename
  `test_regression_dirs` → `test_regression_files`, list-of-files semantics, both constructors),
  `project.tks` (guard rename), `regression.tks` (new `run_regression_sources` §1; DELETE
  `discover_source`/`dir_tkr_files`/`tkr_basename_lt`/`tkr_insert_sorted`/`dir_first_tkr`/
  `dir_is_self_project`; `run_one_regressor`→direct `run_one_tkr`; add `basename_of` if absent).
  Re-list the 6 existing `.tkr` by path in `teko.tkp`. Honest-error on a missing path. `[RITUAL]`
  — schema change goes live. **DONE (feat/0.3.1-regressor-runner, wave A).**
- **C1 — Runner: inline source + declarative-skip.** `Given source inline`/`Given source`
  nouns → `source_inline`/`source_file`; `compile_inline_source` (§2a); `guard_no_project_build_at_root`
  (§1.1); `tkb_scenario_is_declarative` (§2c). `tkr_test.tkt` parity units. `[RITUAL]`
  **DONE (feat/0.3.1-regressor-runner, wave A) — with a correction the doc's §2a snippet missed:
  `teko` REJECTS a bare `.tks` file at the CLI (`main.tks`'s "a lone .tks is not a unit" law), so
  `compile_inline_source`/`compile_standalone_source` both synthesize a SCRATCH project (a minimal
  `.tkp` + the snippet at its root `main.tks`, the C7.9 main-file rule) rather than passing the
  snippet path directly; the pre-existing `compile_regressive_source` (0.3.0.30) carried the same
  never-exercised bug (no live `.tkr` used `Given source` yet) — fixed in the same crumb, not
  deferred.**
- **C2 — Runner: env / backend injection.** `Given backend`/`Given env` → `Tkr.env`;
  `run_captured_env` + `run_captured_cmd_prefixed` refactor (§2b). Unit: env reaches the child,
  own==C parity over one snippet. `[RITUAL]` **DONE (feat/0.3.1-regressor-runner, wave A).**
- **C3 — Runner: target + run-wrapper + leaf-tool verbs.** `Given target` → `env`+wrapper
  (`resolve_run_wrapper`, §2g); `When built in place` (#64 CWD shape); `Then object well-formed`
  (`check_object_wellformed`, §2f). Honest os-arch skips.
  `[RITUAL]` **DONE (feat/0.3.1-regressor-runner, wave A).**
- **C4 — Runner: dependency provisioning.** `Background: Given dependency "<subdir>"` →
  `provision_dependency` (§2e). The heaviest runner crumb. `[RITUAL]`
  **DONE (feat/0.3.1-regressor-runner, wave A) — implemented as `Given dependency = "<subdir>"`
  (the `=`-form, consistent with every other `Given` noun in this grammar) rather than the
  bare-verb shape sketched above.**
- **C5 — POPULATE `regressor.tkr`** (F0-F7, §3, incl. the complete null-union matrix §3.1). List
  it FIRST in `teko.tkp`. First light of the compiler-as-SUT bank. `[RITUAL]` — measure the
  duration delta (R1).
- **C6 — POPULATE the project-regressors** (§4): author `crossmodule`, `ffi_import`, `ffi_export`,
  `lib_static`, `lib_shared`, `cwd_build`; list each by path. Fold the source
  fixtures in. `[RITUAL]`
- **C-cov — Runner CONTRIBUTES to coverage (0.1 prerequisite of the sweep).** Cov-A (§5.1): the
  `teko test` lane builds the regression `exe` coverage-instrumented, each snippet-compile gets
  its own `TEKO_TKCOV`, and `coverage::cov_merge` folds every child covfile into `fe.prog`
  before `coverage_floor_misses`. Touch sites: `project.tks` (the `test_project` regression-phase
  call site + the merge, reusing ~1749-1753's seam), `regression.tks` (per-scenario covfile env).
  Unit: a regressive that compiles construct X marks X's compiler-source lines; the floor
  percentages RISE with the phase on. `[RITUAL]` — REQUIRED before C8; measure that regressions
  now move the floors.
- **C8 — `.tkt` SWEEP (0.1 inversion) + drained-dir deletion.** Per §5.2, delete the duplicated
  `#test`s (checker rejections, codegen/emit-exit, comptime, null-union, own-native-exit) file by
  file, EACH removal gated on the D4 floors staying green (now held by the regressives via C-cov).
  Delete the drained `(a)` fixture dirs (their snippets live inline). `[RITUAL]`
- **C9 — REMOVAL (owner/integrator applies §7).** Delete the 6 orchestration scripts; strip the
  regression steps from `native.yml`; add the backend-toolchain provisioning + fail-closed flag to
  `tests.yml`; resolve R5. `[RITUAL final]` — confirm the bank covers every retired harness's
  verdict with no coverage loss.

**Chain fit (`.31`, post-`drop-128`):** C0-C4 (the runner) depend on NOTHING outside the seed and
land immediately. C-cov reuses the shipped `TEKO_TKCOV`/`cov_merge` seam — no blocked API. The
`.tkt` sweep (C8) is strictly AFTER C-cov so the floors never dip. C5-C6's F5/FFI scenarios that
need an unlanded own-backend crumb ride
`Given pending = "<crumb>"` (skip-green) until KP7-KP15 (`kill-c-pull-forward`) close — the bank
grows into maturity without blocking. Null-union F3 depends only on the already-merged
null-union C3-C7. No blocked API remains for the runner or the bank skeleton.

---

## 7. CI delta (owner/integrator applies; architect does NOT edit `.github`/`scripts`)

**`native.yml` — REMOVE** (all now inside `teko test .`): the gen1-checks steps "CWD build
regression (#64)" (`native_regressions.sh`), "EXPECT_COMPILE_FAIL (#610)"
(`compile_fail_regressions.sh`), "EXPECT_EXIT positive (#594 8f)" (`positive_regressions.sh`),
"E2E cross-module (#594 8d)" (`crossmodule_regressions.sh`), "C-vs-own differential"
(`diff_c_own.sh`), and the removed in 0.3.1
`diff_c_own.sh` invocation. `native.yml` keeps only the non-regression CLI smokes
(`cli_flags_test.sh`, `fmt_cli_test.sh`, `region_drop_subtree_test.sh`) and the
cross-compile-linux smoke.

**`tests.yml` — becomes the SOLE regression executor.** `./bin/teko test .` now runs the
regression phase (because `teko.tkp` declares `[tests] regression = […]`). ADD, on the lanes that
can host them, provisioning for the backend toolchains the folded differentials need —
the qemu emulator for the cross legs, plus the windows lane runs the coff
differential NATIVELY.
Set a fail-closed env (a `REGRESSION_REQUIRE_TOOLS=1`, mirroring the retired
fail-closed) on the provisioned lanes so a broken provisioning step is a HARD failure,
not a silently-green honest-skip (D6/R2).

**Surviving `scripts/*`:** `check_elf.sh`, `check_macho.sh`, `check_coff.sh` — leaf tools the
`Then object well-formed` verb shells (§2f). **DELETED after C7 green:** `compile_fail_regressions.sh`,
`positive_regressions.sh`, `native_regressions.sh`, `crossmodule_regressions.sh`,
`diff_c_own.sh`.

**`sanitizers.yml`** build-all loops (`for proj in examples/regressions/*/`) currently ASan/
paranoid-build EVERY fixture dir — a SEPARATE corpus concern from regressions. Deleting the
drained dirs (§5) shrinks that corpus; see R5 for the ratifiable resolution.

---

## 8. Risks / law tensions


  `teko test .`
  add engine startup. Windows's 90-min cap (tests.yml already generous) is the pressure point.
  Mitigate: honest-skip where a tool is absent; provision heavy toolchains only on the lanes that
  afford them; MEASURE the delta at every `[RITUAL]` (C5/C6/C-cov). Not a HALT — the pressure is
  bounded (snippet compiles, not full builds).
- **R2 (honest-skip erosion).** If a CI lane silently lacks `qemu`, the F5 scenarios
  skip and coverage drops vs the old fail-closed gate. RESOLUTION: the
  `REGRESSION_REQUIRE_TOOLS=1` fail-closed flag on provisioned lanes (§7/D6). A dev laptop keeps
  the honest-skip default.
- **R3 (tree cleanliness).** Inline snippets compile into `bin/.regr-work/` (already the runner's
  scratch); project-regressors build `-o bin` (already `.gitignore`d via
  `examples/regressions/**/bin/`). No fixpoint/byte-identity perturbation.
- **R4 (self-project recursion).** The root `regressor.tkr` now EXECUTES. Guarded structurally
  (`guard_no_project_build_at_root`, §1.1) + the `TEKO_IN_REGRESSION` sentinel + the fact the
  runner only ever `teko build`s a snippet, never `teko test`s. Ratified as D3.
- **R5 (sanitizer corpus breadth).** `sanitizers.yml` builds EVERY `examples/regressions/*/` dir
  under ASan/paranoid. Deleting the drained language dirs (§5) shrinks that corpus.
  SEALED (D7, owner 2026-07-24): the drained dirs are DELETED and the sanitizer corpus becomes
  the ≤10 project-regressors (they build real artifacts under ASan); the inline snippets already
  run under gen1 in `teko test .`, and their ASan value is marginal (tiny exit(n) programs). Per
  the owner principle *"sempre prefira dobrar e remover diretórios drenados."* Not a law tension.
- **R6 (own-backend maturity).** F5/FFI scenarios that need an unlanded KP crumb ride
  `Given pending` skip-green (§6 chain fit). No false green: a pending scenario reports skip.
- **R7 (measured coverage under the `.tkt` sweep — 0.1).** Regressions do NOT feed the coverage
  metric today (§5.1), so deleting `.tkt` naively drops measured coverage below the D4 floors.
  RESOLUTION: crumb C-cov (Cov-A) makes regressions contribute via the existing
  `TEKO_TKCOV`/`cov_merge` seam, and the `.tkt` sweep (C8) is strictly AFTER it, each deletion
  gated on the floors staying green. NOT resolved by lowering floors (that hides coverage). Not a
  HALT — the seam already exists; C-cov only wires the regression phase into it.

No genuine LAW TENSION surfaced. Teko-only (runner + bank are `.tkr`/`.tks`/`.tkt`; the surviving
`check_*.sh` are pre-existing host-toolchain leaf tools, not new product code — the C-twin freeze
is about the compiler, not host object-inspectors). W15 full-Javadoc on every signature above.
M.3 honest errors (missing manifest path, missing dep, absent tool → named skip/error). The
owner's TEST-lane-only / native-only / ≤10 / kill-the-scripts boundaries are all satisfied. **No
HALT required.**

---

## 9. Decisions to ratify (explicit)

- **D1** — `.tkr` (Gherkin BDD) is THE single regressive format; the `.tkb` extension and the
  compact-TOML `.tkr` are retired into it. (CONFIRMATION — already `origin/main` state.)
- **D2** — `[tests] regression` = an explicit, ordered list of relative `.tkr` FILE paths; no
  globs, no directory walk; execution order = list order; a missing path is a MANIFEST ERROR
  (M.3), never a silent skip. (Owner ruling 2026-07-24 — document + implement.)
- **D3** — the root `regressor.tkr` EXECUTES via inline/explicit-source snippets;
  `dir_is_self_project` dies; anti-recursion = `guard_no_project_build_at_root` + the sentinel;
  the 4 meta-scenarios stay declarative-skip.
- **D4** — the 6 orchestration scripts DIE; `check_{elf,macho,coff}.sh` + external
  `qemu` SURVIVES as a scenario-invoked leaf tool.
- **D5** (SEALED — owner 2026-07-24) — the ≤10 set = `regressor.tkr` + `{crossmodule, ffi_import,
  backends Feature F5 (env/target + os-arch routing), NOT isolated as their own projects.
- **D6** — `tests.yml` is the sole regression executor and PROVISIONS the backend toolchains with
  a `REGRESSION_REQUIRE_TOOLS` fail-closed on those lanes; `native.yml` drops every regression
  step.
- **D7** (SEALED — owner 2026-07-24) — the drained `(a)` fixture dirs are DELETED (NOT retained
  as sanitizer corpus); the sanitizer corpus becomes the ≤10 project-regressors. This instances
  the owner's default triage principle: **"sempre prefira dobrar e remover diretórios drenados"**
  — fold into a regressive and delete the drained directory (applies to future triages of the
  same nature).
- **D8** — the regressive-primacy hierarchy (owner ruling 2026-07-24, §0.1):
  **`regressor.tkr` > project-regressives > `.tkt`**; a `.tkt` duplicating a regressive DIES, not
  the regressive. Diagnostic-less rejections become diagnostic-PINNED regressive scenarios (F1),
  not `#test`s. (Ratify + implement.)
- **D9** — regression execution MUST feed the coverage metric (crumb C-cov / Cov-A, §5.1) BEFORE
  any `.tkt` is swept; the floors are held by the regressives, never lowered. (Ratify + implement
  — the honest-verification resolution WITHIN D8, not against it.)

**ALL of D1-D9 are CLOSED at ruling level (owner 2026-07-24).** D1 is a confirmation of the
`origin/main` state; D2/D3/D4/D6/D8/D9 follow directly from the owner's closed direction; D5/D7
delete the drained dirs), under the default principle *"sempre prefira dobrar e remover
diretórios drenados."* No open decision, no alternative pending, no HALT — the doc is
implementation-ready.
