# Coverage gate — delta-coverage as a required status check (#353)

Design-only. No CI job or product `.tks` is authored here. This doc plus the crumb
sequence at the end is what an implementer follows. Scope: turn the owner's standing
law **"100% coverage on all NEW/CHANGED code"** (issue #353, "D32") from a
human-read number into a **required GitHub status check named exactly `Coverage gate`**.

Base branch: `remodel/memory-unsafe-backend` (sub-branch of #329). CI-only issue —
no fixpoint impact by construction.

---

## 1. Ground truth — what `teko test . --coverage` actually emits

Recon against a real `cobertura.xml` produced by the current corpus (7400 lines-valid,
6579 branches-valid) and the emitter at `src/vm/vm.tks:3901-3970` (`cov_cobertura`,
C twin `src/vm/vm.c:3147`). CI path: `native.yml` runs `teko . -o bin` as the native
gate (`teko test`'s `--coverage` writes `<cwd>/cobertura.xml`; the build/native path
writes `<out>/cobertura.xml` — see `src/driver.c:921-923,999-1000`).

Real header + a real uncovered class (verbatim):

```xml
<?xml version='1.0' ?>
<coverage line-rate='0.501' branch-rate='0.487' lines-covered='3710' lines-valid='7400'
          branches-covered='3208' branches-valid='6579' complexity='0' version='teko' timestamp='0'>
  <sources><source>.</source></sources>
  <packages>
    <package name='teko' line-rate='0.501' branch-rate='0.487' complexity='0'>
      <classes>
      <class name='teko::compress::crc32_row' filename='src/compress/compress.tks'
             line-rate='0.000' branch-rate='0.000' complexity='0'>
        <methods><method name='crc32_row' signature='()V' line-rate='0.000' branch-rate='0.000'>
          <lines><line number='16' hits='0'/></lines></method></methods>
        <lines>
          <line number='16' hits='0'/>
          <line number='17' hits='0'/>
          <line number='18' hits='0'/>
          <line number='42' hits='0' branch='true' condition-coverage='0% (0/2)'/>
        </lines>
      </class>
```

Load-bearing facts the gate depends on (each verified against the real file):

1. **`filename=` is REPO-RELATIVE** (`src/compress/compress.tks`). It joins directly
   to `git diff` paths — no path munging. `<sources><source>.</source>` confirms
   root-relative.
2. **Per-line hit counts exist**: `<line number='N' hits='H'/>`. `H>=1` ⇒ covered.
3. **Only EXECUTABLE lines appear.** The `<lines>` list is produced by a STATIC AST
   walk (`cov_walk_block`, `src/vm/vm.tks:3867`) that enumerates only lines hosting an
   evaluated expression. Blank lines, doc-comment lines, and bare declarations are
   **never emitted**. This is the single most important primitive: the report is
   already the "executable line" oracle. The diff script does NOT need its own
   executable/non-executable classifier — it intersects changed lines with the report's
   line set, and any changed line absent from the report is non-executable → not required.
4. **Branch lines are annotated** (`branch='true' condition-coverage='P% (t/total)'`).
   The delta gate is a **line** gate (per acceptance criterion 1, "executable line …
   uncovered"), so it keys on `hits`. Branch-delta is out of scope for v1 (see §7).
5. **One `<class>` per function**; `filename` repeats across the classes of one file.
   A source line can therefore appear in MULTIPLE classes (e.g. two fns, or a lambda
   nested in a fn). The join must **OR `hits` across every class for that
   `filename:line`** — a line is covered if ANY class reports `hits>=1`.
6. **Scope is `src/**` ONLY.** Verified: 789 classes, all `filename='src/...'`; ZERO
   `examples/` classes. `examples/regressions/**` fixtures compile as separate
   sub-projects and are NOT in `teko test .` coverage. A PR touching only
   `examples/**`, docs, or CI is therefore a no-op for this gate (passes cleanly, same
   as docs-only).
7. **Test functions are EXCLUDED from the report.** `cov_cobertura` skips
   `f.is_test` (and `f.namespace == "teko::vm"`). So `.tkt` files contribute only their
   NON-test helper fns as classes (109 `.tkt` classes exist). Consequence for the diff:
   a changed line inside a `#test` body is **absent from the report** → treated as
   non-executable → not required. That is correct: `#test` bodies are the tests
   themselves, not "new code to be covered." No special-casing needed; the
   report-intersection handles it for free.

---

## 2. Delta computation — joining the report to the PR's changed lines

### 2.1 The changed-line set

```
git diff --unified=0 <merge-base>...HEAD -- 'src/**/*.tks' 'src/**/*.tkt'
```

- `<merge-base>` = `git merge-base origin/<base-branch> HEAD` (the true PR fork point;
  the `A...B` three-dot form already diffs against the merge-base, but we compute it
  explicitly for the `git diff --unified=0 $MB HEAD` form so renames resolve against the
  fork tree). In Actions, checkout with `fetch-depth: 0` so the merge-base is present.
- `--unified=0` ⇒ each hunk header `@@ -a,c +x,y @@` gives exactly the ADDED/CHANGED
  line span `[x, x+y-1]` in the NEW file. We collect only `+` sides (added/changed);
  pure deletions contribute no new executable line and are ignored.
- Path scope `src/**` matches fact #6. `.tkt` included (fact #7 makes test lines drop
  out safely via the report intersection).

Hunk parse (grounded on a real hunk from this repo):
```
+++ b/src/io/stream.tks
@@ -429,0 +430,24 @@ pub fn flush_write(...) -> u64 | error {
```
⇒ file `src/io/stream.tks`, changed new-lines `430..453`. A `+x` with no `,y` means
`y=1` (single line). A `+++ /dev/null` (file deleted) contributes nothing.

### 2.2 Renames

`git diff --find-renames` (default on in recent git) emits `rename from/to` with a
`@@` block only for the changed hunks of the renamed file, under the NEW path. Because
the changed-line set is keyed on the NEW `+++ b/<path>` and the cobertura `filename` is
also the new path, renames join correctly with no extra handling. A pure rename with no
content change produces no `@@ +` hunk ⇒ no required lines. (A rename+edit yields the
edited lines only — exactly right.)

### 2.3 The join (the gate's core)

```
required = { (file, line) : (file,line) ∈ changed-set  AND  (file,line) ∈ report-line-set }
missed   = { (file, line) ∈ required : OR-of-hits over all classes for (file,line) == 0 }
PASS ⟺ missed == ∅  (after exclusions, §3)
```

- `changed-set ∩ report-line-set` is the executable-changed set (fact #3: absence from
  the report ⇒ non-executable/doc/blank/decl/test ⇒ not required).
- `missed` prints each `file:line` (criterion 1) with the fn/class name for context.
- Empty `changed-set` (docs-only / examples-only / no `src` `.tks`|`.tkt` delta) ⇒
  `missed == ∅` ⇒ PASS cleanly (criterion 3). No report is even needed in that case;
  the job may short-circuit before building (see §5 fast-path).

### 2.4 Robustness notes for the implementer

- Build the report-line map as `dict[file] -> dict[line] -> max(hits seen)` in ONE pass
  over `<class>`/`<line>`, honoring the multi-class OR (fact #5).
- A changed line that lands on a `branch='true'` line is still keyed on `hits` only —
  covering the line (hits>=1) satisfies the line gate even if one branch outcome is
  untaken. (Branch-delta deferred, §7.)
- CRLF/LF: `.gitattributes` already pins `.tks`/`.tkt` to LF (native.yml fmt-gate note),
  so line numbers are byte-stable across runners.

---

## 3. THE DESIGN FORK — exclusion mechanism for justified-unreachable arms

The tension: D32 carves out "justified-unreachable" defensive arms (a `match` `_ =>`
that a soundness argument proves cannot execute) with a one-line justification. But
**W15 forbids inline `//` in function bodies** — so `// coverage: unreachable` on the
arm is ILLEGAL under our style law. The marker must live somewhere W15 permits AND stay
visible/reviewable (M.3-honest).

### Options evaluated (grounded in what `--coverage` emits + W15 + M.3)

**A. Doc-comment tag `@uncovered <line-hint> <reason>` on the enclosing decl.**
Legal under W15 (doc-comments are the sanctioned comment form). The diff script parses
the fn's `/** */` for the tag and drops those lines from `required`.
- Cost: **coarse and fragile.** The report is per-LINE but a doc-comment sits on the
  DECL, not the arm. A `<line-hint>` inside the doc is a hand-maintained line number
  that silently rots when the body shifts (every edit above the arm invalidates it),
  and there is no compiler check that the hint points at a real unreachable line. It
  also invites over-exclusion (one tag, whole fn). Rejected as primary.

**B. Reviewed `coverage-exclude` manifest** (`coverage-exclude.tks`-adjacent data file:
`path`, `line` or `line-range`, `reason`), diffed and reviewed like code.
- Legal (it is data, not a body comment — W15 doesn't reach it). Visible + reviewable
  in the PR diff (M.3 satisfied: the justification is a required column). Machine-join
  is trivial: subtract its `(file,line)` set from `required`.
- Cost: **the exclusion lives far from the code** it excludes (a reader of the arm
  doesn't see it's excluded), and line numbers rot on edits — but the manifest is
  itself diffed, so a rotting entry surfaces as a stale line in review, and a
  newly-uncovered line that the stale entry no longer covers **re-reddens the gate**
  (fail-safe: staleness makes the gate STRICTER, never silently permissive). This is
  the key M.3 property option A lacks.

**C. Compiler structural dead-arm detection via `--coverage`.**
Recon verdict: **the compiler does NOT emit structural unreachability today.** The
cobertura walk (`cov_walk_block`) enumerates every arm as an executable line
unconditionally; there is no reachability analysis that could stamp an arm
"structurally unreachable" and drop it from `lines-valid`. `never`/bottom-type
reasoning (memory: `teko-never-bottom-type`) exists for `exit`/`panic` divergence but is
not wired to coverage. Building it is a **compiler change** — explicitly out of scope
for a CI-only issue (#353 "Touches CI only … no `.tks` compiler change") and would
break the fixpoint-by-construction promise. **C is the RIGHT long-term answer but is a
separate issue.** Report up, do not fold.

### VERDICT: **Option B — the reviewed `coverage-exclude` manifest.**

Why B wins law-first:
- **W15-legal without ambiguity.** It is a data file, not a function body, so the
  "no inline `//`" law never applies. (A is also legal but couples the marker to a
  rotting line-hint with no fail-safe.)
- **M.3-honest + reviewable BY CONSTRUCTION.** Every exclusion is a diff-visible row
  with a mandatory `reason` column; adding one is a reviewed change exactly like adding
  code. The reviewer sees the justification in the same PR that introduces the
  unreachable arm. (D32's "one-line justification, visible" maps 1:1 to a manifest row.)
- **Fail-safe staleness.** Because the manifest is keyed on `(file, line)` and the gate
  only ever SUBTRACTS matched lines, a stale entry (line moved) stops matching → the now-
  uncovered arm re-reddens the gate. Staleness can only make the gate stricter, never
  silently exempt real uncovered code. This is the property A cannot offer (A's line-hint
  drift silently widens the exemption to whatever line the number now points at).
- **Trivial, testable join.** `required -= manifest-set` is one set-subtraction; no
  new parser, no dependence on doc-comment shape.

Manifest format (grounded — plain TSV-in-repo, reviewed like code):

```
# coverage-exclude — justified-unreachable executable lines (D32 carve-out).
# One row per line. Columns: file<TAB>line<TAB>reason
# The gate SUBTRACTS matched (file,line) from the required set. A stale row (line moved)
# simply stops matching and the now-uncovered line RE-REDDENS the gate (fail-safe).
src/parse/parse_expr.tks	412	defensive _=> after exhaustive TokenKind match; kind is closed enum, arm is soundness-only
```

Location: repo root `coverage-exclude` (or `.github/coverage-exclude`). No `.tks`
extension — it is not compiled; it is CI data.

**Cost stated honestly (B's residual weakness):** the exclusion is not co-located with
the arm, so a reader of `parse_expr.tks:412` must consult the manifest to learn the arm
is exempt. Mitigation available at implementer discretion (NOT required for #353): the
arm MAY carry a Javadoc `@see coverage-exclude` on its enclosing fn as a human
breadcrumb — that is a legal doc-comment and purely advisory (the gate ignores it). This
gives B most of A's locality without A's rotting machine-hint.

**No owner HALT.** The fork resolves cleanly law-first: B is the only option that is
simultaneously W15-legal, M.3-visible, fail-safe on staleness, and CI-only. C (compiler
dead-arm detection) is the superior end-state and is reported up as a **follow-up
issue candidate** for the integrator to sequence — NOT created here (issues-are-100%).

---

## 4. The diff-coverage helper — shell/python vs Teko

**Recommendation: a `python3` script `scripts/coverage_diff.py`.** Reasoning:

- **No recursion trap.** A Teko helper would itself be `src/**/*.tks` under `teko test .`
  and thus subject to its OWN gate — a helper that is 100%-covered-of-itself is a
  self-reference that complicates the very first green run (the gate gating its own
  introduction). A script under `scripts/` is out of the `src/**` scope (fact #6) and
  carries no coverage obligation.
- **XML + git-diff parsing is exactly stdlib territory** (`xml.etree.ElementTree`,
  `subprocess`, `re`). `python3` is present on all three GitHub runners.
- W15 governs `.tks`; a `scripts/*.py` is CI tooling, not corpus — W15 does not reach it
  (same status as `ci_provision_teko.sh`). It still gets normal review.
- Shell-only was considered and rejected: robust Cobertura XML parsing in pure `sh`/awk
  is brittle (nested attributes, multi-class OR). Python is the pragmatic floor.

Contract (the implementer writes this):
```
scripts/coverage_diff.py --report cobertura.xml --base-ref <ref> [--exclude coverage-exclude]
  exit 0  → no uncovered changed executable line (PASS)
  exit 1  → >=1 uncovered changed executable line; prints "UNCOVERED src/x.tks:NN  (fn foo)"
  exit 2  → tool error (missing report, bad git state) — treated as FAIL by CI
```

---

## 5. CI wiring — the `Coverage gate` job

**Own file `.github/workflows/coverage-gate.yml`** (isolated from `native.yml` so a
coverage-gate change never re-triggers the whole native matrix, and the check name is
unambiguous). One Linux runner is enough (coverage marks are engine-deterministic;
no per-platform variance in line hits).

```yaml
name: Coverage gate
on:
  pull_request:
    branches: [main, 'remodel/**']
jobs:
  coverage-gate:
    name: Coverage gate            # <-- the check context is the JOB NAME; must be EXACTLY this
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with: { fetch-depth: 0 }   # merge-base must be reachable
      # Fast-path: no src/**/*.tks|*.tkt delta -> pass without building (docs/examples/CI-only PR).
      - id: delta
        run: |
          MB=$(git merge-base origin/${{ github.base_ref }} HEAD)
          if git diff --unified=0 "$MB"...HEAD -- 'src/**/*.tks' 'src/**/*.tkt' | grep -q '^@@'; then
            echo "has_delta=true" >> "$GITHUB_OUTPUT"
          else
            echo "has_delta=false" >> "$GITHUB_OUTPUT"
          fi
      - name: Provision teko from the latest release   # released seed, NOT ./build/teko (#352)
        if: steps.delta.outputs.has_delta == 'true'
        env: { GH_TOKEN: '${{ github.token }}' }
        run: ./scripts/ci_provision_teko.sh linux-x86_64
      - name: Coverage report
        if: steps.delta.outputs.has_delta == 'true'
        run: teko test . --coverage        # writes ./cobertura.xml
      - name: Delta-coverage gate
        if: steps.delta.outputs.has_delta == 'true'
        run: python3 scripts/coverage_diff.py --report cobertura.xml
             --base-ref "origin/${{ github.base_ref }}" --exclude coverage-exclude
```

- **Check name `Coverage gate`** = the job `name:`. GitHub publishes the check under
  that string; the ruleset requires that exact context (criterion 5).
- **Released seed per #352**: uses `ci_provision_teko.sh` (the same provisioner
  `native.yml` uses), NOT `./build/teko` (the frozen C twin that #285/#352 showed can't
  parse current syntax). `teko` on PATH after provisioning.
- **Docs-only / no-`.tks` PR passes cleanly** because every build/report/gate step is
  `if: has_delta == 'true'`. A no-delta PR runs only checkout + the cheap `git diff` and
  reports GREEN (criterion 3). This also keeps the check fast on the common docs PR.
- `teko test . --coverage` writes `cobertura.xml` at cwd (`driver.c:1000`), which the
  script reads. If the native `teko . -o bin` path is preferred for parity with the
  native gate, swap to `teko . -o bin --coverage` and point `--report bin/cobertura.xml`
  — both are valid; `teko test` is lighter (no binary emit) and sufficient for the marks.

---

## 6. Sequencing constraint (CRITICAL — the never-posting-check trap)

A required status check that NEVER posts blocks **every** merge. Therefore the ruleset
edit is the LAST crumb and is gated on a live green observation:

1. Merge the workflow + script (the check starts POSTING on PRs, but is not yet required).
2. Open/observe a real PR where `Coverage gate` runs and is GREEN.
3. ONLY THEN add `{context: "Coverage gate"}` to ruleset **17650105** (`All Green`,
   currently requires `CI gate`, `Sanitizer gate`, `SAST gate` — verified via
   `gh api repos/{owner}/{repo}/rulesets/17650105`).

```
gh api repos/{owner}/{repo}/rulesets/17650105 --jq '.rules[]|select(.type=="required_status_checks")'
# then PATCH required_status_checks adding {context:"Coverage gate", integration_id:<Actions app id>}
```
Do NOT reorder. Adding to the ruleset before step 2 is the documented footgun in the
issue and would wedge the repo.

---

## 7. Out of scope / reported up (issues-are-100%, no new issues created here)

- **Branch-delta gate** (require both outcomes of a changed `if`/`match` line). The report
  carries `condition-coverage` per line, so it is a natural v2, but criterion 1 says
  "executable LINE uncovered" — v1 is a line gate. Reported as a follow-up candidate.
- **Compiler structural dead-arm detection** (fork option C) — the superior long-term
  exclusion mechanism; a compiler change, separate issue. Reported up.
- Historical branch-cov FLOORS in `native.yml` are UNTOUCHED (additive gate; issue
  out-of-scope explicitly).
- `.c` twin coverage — frozen, excluded (already absent from the report).

---

## 8. Acceptance criteria → concrete test steps

| # | Criterion | Test step |
|---|-----------|-----------|
| 1 | Uncovered new `.tks` line ⇒ FAIL naming `file:line` | Scratch branch off this PR: add a `pub fn` in `src/**` with a body line no `#test` exercises. Push; observe `Coverage gate` RED printing `UNCOVERED src/…:NN`. (RED PROOF — log it, then delete the branch.) |
| 2 | All new/changed lines covered ⇒ PASS | On the same fn add a `#test` hitting every body line; re-push; observe GREEN. |
| 3 | Docs-only PR passes cleanly | A PR editing only `docs/**`/`*.md`: `has_delta=false` fast-path ⇒ GREEN without building. |
| 4 | Justified-unreachable arm excluded, justification visible | Add a defensive `_ =>` arm + a `coverage-exclude` row (`file<TAB>line<TAB>reason`); gate GREEN; the reason is diff-visible in the PR (M.3). Remove the row ⇒ gate RED (proves the exclusion is load-bearing, not a no-op). |
| 5 | Check name exactly `Coverage gate`; added to ruleset only after live-green | §6 sequence: merge → observe green on a live PR → PATCH ruleset 17650105. |

Dogfood: this very PR's helper is under `scripts/` (out of `src/**` scope), so the gate
does not gate its own introduction — no self-reference deadlock.

---

## 9. CRUMB SEQUENCE (implementer follows in order; each independently gate-able)

- **C1 — `scripts/coverage_diff.py`.** Parse cobertura (`xml.etree`), build
  `file -> line -> max hits` with the multi-class OR (§2.4). Parse
  `git diff --unified=0 $(git merge-base $base HEAD)...HEAD -- 'src/**/*.tks' 'src/**/*.tkt'`
  into `file -> {added lines}`. `required = changed ∩ report-lines`; subtract
  `coverage-exclude`; `missed = {r : OR-hits==0}`. Print each `UNCOVERED file:line (fn)`;
  exit 0/1/2 per §4. Gate: unit-test locally against the checked-in `cobertura.xml`
  (inject a fake diff) — no CI needed yet.

- **C2 — `coverage-exclude` file** at repo root with the header block from §3 and zero
  data rows (or one real row if a genuine unreachable arm exists in the base). Gate:
  `coverage_diff.py --exclude coverage-exclude` still runs clean.

- **C3 — `.github/workflows/coverage-gate.yml`** per §5: `pull_request` into
  `[main, remodel/**]`, job `name: Coverage gate`, `fetch-depth: 0`, has-delta fast-path,
  released-seed provision, `teko test . --coverage`, run the script. Gate: workflow lints
  (`actionlint` if available); pushes on a PR.

- **C4 — dogfood green.** Open the design→impl PR (base `remodel/memory-unsafe-backend`).
  Confirm `Coverage gate` runs and is GREEN on the PR's own delta (script is under
  `scripts/`, out of scope, so no self-gate). Criterion 3 also covered if the PR is
  CI-only.

- **C5 — RED proof (criterion 1).** Scratch branch: add an uncovered `src/**` fn line;
  push; capture the RED `Coverage gate` log with the `file:line`. Delete the branch.

- **C6 — exclusion proof (criterion 4).** Scratch: uncovered defensive arm + a
  `coverage-exclude` row ⇒ GREEN; remove row ⇒ RED. Log both.

- **C7 (RITUAL / FINAL) — require in ruleset.** ONLY after C4 green observed on a live
  PR: `gh api` PATCH ruleset **17650105** adding `{context: "Coverage gate"}` to
  `required_status_checks`. This is the ritual point — the whole gate must be observed
  green BEFORE this crumb, never before (§6). Verify a subsequent PR now shows
  `Coverage gate` as REQUIRED.

**Ritual points:** C4 (gate observed green on a live PR) and C7 (ruleset flip — the
irreversible-if-wrong step; a never-posting required check wedges the repo).

**Risks + law tensions:**
- *Never-posting required check* → mitigated by the C7-last sequencing (§6). HIGH if
  reordered; the crumb order enforces it.
- *W15 vs D32 exclusion marker* → resolved: manifest (option B) is data, not a body
  comment. No tension remains.
- *Self-gate recursion* → avoided by keeping the helper in `scripts/` (out of `src/**`).
- *Report path drift* (`teko test` cwd vs `teko . -o bin`) → the workflow pins the
  command and the `--report` path together; if the native-parity path is chosen, both
  move as a pair (§5 note).
