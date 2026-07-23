# Native test-gate keystone — #265 (line/branch cov) + #168 (compile-once partition)

Status: DESIGN (architect). Implementation crumbs. Companion to
`docs/design/compile-time-architecture.md` §2.1/Lever-B and memory
`teko-native-test-gate` (owner standing ruling 2026-07-06: the `#test` gate NEVER runs on
the VM — it compiles to a NATIVE binary that knows the product-under-test + its access
points; VM stays dev/debug/WASM only). This is THE shared-villain kill: it drops the
compiler's gate-time peak from 1595 MB → ~366 MB (the child runs in its own process) AND
the 15.6 s VM interpret → a sub-second native run. Both axes bank it once.

## 0. What already exists (do NOT rebuild)

Verified in-tree — the infrastructure is REAL and opt-in today:

- **Codegen modes.** `CgMode = enum { Program; TestPlain; TestCov }` (`codegen.tks:50`).
  `tk_emit_c_mode(prog0, mode)` (`codegen.tks:7324`) emits types/prototypes/bodies
  IDENTICALLY across modes; only `main()` differs (`emit_mode_main`, `codegen.tks:7546`).
  `Program` = the historical virtual-main → **byte-identical → fixpoint intact**.
- **Native gate main.** `emit_test_main` (`codegen.tks:7605`) emits a `main()` that calls
  each `#test` in order (fail-fast, M.1); under `TestCov` it brackets with
  `tk_cov_reset/branch_reset/branches_on/line_reset/lines_on` and, per test,
  `tk_cov_enter(idx)` / `tk_cov_leave` (`emit_test_call`, `codegen.tks:7666`), then dumps
  via `getenv("TEKO_TKCOV")` → `tk_cov_dump`.
- **Function-level marks.** `emit_function_mode` (`codegen.tks:7659`) → `emit_function_cov`
  (`codegen.tks:5895`) emits `tk_cov_mark(cov_idx)` on entry of every PRODUCTION fn (skips
  `is_test` + `teko::vm`), keyed by `fn_items_index` (`codegen.tks:7674`). `cov_idx < 0` ⇒
  no prologue ⇒ byte-identical (Program/TestPlain).
- **Runtime sinks (all present, cross-process).** `teko_rt.{h,c}`:
  `tk_cov_line(line)` / `tk_cov_branch(line,col,outcome)` key by the CURRENT fn on a
  realloc-backed fn-stack (`tk_cov_enter/leave`, `teko_rt.c:1667/1678`); packed ids
  `tk_line_id(fn,line)` (`teko_rt.c:1710`) and `tk_branch_id(fn,line,col,outcome)`
  (`teko_rt.c:1659`); `tk_cov_dump`/`tk_cov_merge` speak the `TKCOV1` `.tkcov` protocol
  (`teko_rt.c:1768/1801`) — the SAME protocol #168 uses.
- **Orchestration.** `run_native_gate` (`project.tks:734`) emits the TestCov TU → `run_cc`
  → run child → `vm::cov_merge(covfile)` (`vm.tks:3983` → `tk_cov_merge`). `run_gate`
  (`project.tks:807`) dispatches `native_gate ? run_gate_native : run_gate_vm`. CLI/env
  plumbing `--native-gate` / `TEKO_NATIVE_GATE=1` (`native_gate_of`, `project.tks:904`) done.
- **Engine-independent floors.** `line_coverage`/`branch_coverage`/`functions_coverage`
  (`vm.tks:4051/4085/4050`) statically walk each production fn body (`cov_walk_block`,
  `vm.tks:3866`) and query `tk_cov_line_hit(i, line)` / `tk_cov_branch_hit(i,line,col,o)`
  where **`i` = the prog.items index**. They read the runtime sinks — they DO NOT care which
  engine populated them.

## 1. THE gap (the entire compiler work item)

The native binary populates the FUNCTION sink (`tk_cov_mark`) but NOT the LINE and BRANCH
sinks INSIDE production fn bodies. The VM populates them at three AST sites:

- `eval_expr` head: `teko::cov_line(e.line)` on EVERY evaluated expr (`vm.tks:2157`).
- `exec_if`: `cov_branch(e.line,e.col,0)` then-taken / `(…,1)` else-taken (`vm.tks:3697/3701`).
- `exec_match` / value-match: `cov_branch(e.line,e.col,i)` for arm `i` (`vm.tks:2082/2121`).

So `native_function_floor` (`project.tks:719`) enforces ONLY the function floor; line+branch
floors are left to the VM lane. **Closing #265 = emit those `tk_cov_line`/`tk_cov_branch`
calls in the TestCov codegen so native enforces ALL THREE floors → the VM lane can leave PR CI.**

### 1.1 THE load-bearing keying subtlety (get this right or the floor reads garbage)

`tk_cov_line`/`tk_cov_branch` attribute to `tk_fn_stack[sp-1]` — the fn most recently
`tk_cov_enter`'d. The STATIC floor walk queries with `fn = prog.items index of the production
fn`. TODAY the native gate only enters the **TEST's** index at the test call
(`emit_test_call` cov branch). That is CORRECT for the VM (whose `eval_call` does
`cov_enter(h.idx)` for the CALLED production fn too — `vm.tks:3165/3256/3594`), but the native
path never enters the production fn. **Therefore native line/branch marks would all attribute
to the TEST's index, and every production fn would read as 0% line/branch — a false floor.**

**Resolution (mirror the VM exactly):** every PRODUCTION fn body, under TestCov, must be
bracketed `tk_cov_enter(<own items-index>)` … `tk_cov_leave()` around its body, so its interior
`tk_cov_line`/`tk_cov_branch` marks key on ITS index (matching the static walk's `i`). The VM
does this in `eval_call`/`call_value` (enter on entry, leave on return, `vm.tks:3165/3256/3594`).
The `tk_cov_mark(idx)` prologue `emit_function_cov` already emits gives us the index for free.
`tk_cov_leave` must fire on EVERY exit edge (returns + fall-through). Simpler, equivalent, and
exit-edge-proof: **do NOT use enter/leave inside bodies; instead emit the fn's own index as an
explicit argument** — see N2 (the recommended shape avoids the exit-edge problem entirely).

## 2. Crumb sequence (smallest safe, each gate-able)

Two independent tracks: **Track A = #265 line/branch instrumentation** (compiler, unblocks
line/branch floors on native); **Track B = #168 compile-once partition** (compiler+CI, unblocks
the sub-second win). A is the correctness prerequisite for demoting the VM lane; B is the speed
prerequisite for making native the DEFAULT. Do A first (it is the memory+correctness keystone),
then B (the speed keystone), then the CI flip (QW-1a).

### Track A — #265 line/branch native instrumentation

**A0 — Regression fixtures FIRST (design-ahead, compiles today).** Add fixtures that pin the
current byte-identity and the future line/branch parity (see §4). No code — they gate every
later crumb.

**A1 — Thread a `cov: bool` through the body-emission chain.** The instrumentation must be emitted
ONLY under TestCov and must be ABSENT (byte-identical) otherwise. The codegen holds no
module-mutable state (functional twin), so `cov` is THREADED. Rather than touch all 74 `emit_*`
fns, add ONE parameter to the minimal spanning set that reaches every expr/branch chokepoint:
`emit_function_cov` already knows it (call it with `cov = cov_idx >= 0`). Introduce a tiny record
so the thread is one value, not a bool viral through 30 signatures:

```teko
/**
 * CovCtx — the coverage-instrumentation context threaded through TestCov body emission (#265).
 *
 * Carries whether line/branch `tk_cov_*` calls are emitted and the OWNING production fn's
 * prog.items index (so interior line/branch marks key on the same `fn` the static floor walk
 * queries — `line_coverage`/`branch_coverage` in vm.tks use `i to u64`). `on == false` is the
 * default for every non-TestCov path → NO `tk_cov_*` emitted → byte-identical output → the
 * gen1==gen2 fixpoint is untouched.
 *
 * @see emit_function_cov  the entry that constructs a live CovCtx (cov_idx >= 0)
 * @see line_coverage  the static walk whose `fn` index this MUST match
 * @since #265
 */
type CovCtx = struct {
    /** whether to emit `tk_cov_line`/`tk_cov_branch` (true only under CgMode::TestCov). */
    on: bool
    /** the owning production fn's `prog.items` index — the `fn` key interior marks attribute to. */
    fn_idx: u64
}

/**
 * cov_off — the no-instrumentation CovCtx (every Program/TestPlain path and every ordinary caller).
 *
 * @return a CovCtx with `on = false` → emit_cov_line/emit_cov_branch emit ZERO bytes.
 * @since #265
 */
fn cov_off() -> CovCtx { CovCtx { on = false; fn_idx = 0 } }
```

Thread `CovCtx` as the LAST parameter of the body-emission spine only:
`emit_block_tail`, `emit_stmt`, `emit_exprstmt_tail`, `emit_if_stmt`, `emit_match_stmt`,
`emit_expr_ctx`, `emit_if_value`, `emit_match_value`, `emit_arm_value`, `emit_branch_value`,
`emit_block_region`. Every current call-site passes `cov_off()` (mechanical, byte-identical).
`emit_function_cov` passes a live `CovCtx { on = cov_idx >= 0; fn_idx = cov_idx to u64 }`.

**A2 — Emit `tk_cov_line` at the expression-statement / binding chokepoint (NOT per sub-expr).**
The VM marks on EVERY `eval_expr`; naively mirroring that is a mark per sub-expression, which
bloats the TU and is unnecessary — the floor walk (`cov_walk_expr`, `vm.tks:3781`) collects the
DISTINCT set of `e.line` values reachable in the body, so a line counts as covered if ANY expr on
it ran. Emitting one `tk_cov_line(<line>)` at the START of each emitted STATEMENT (binding / assign
/ return / expr-stmt / loop-header / if-cond / match-subject) covers the same distinct-line set the
walk enumerates, because `cov_walk_stmt`/`cov_walk_expr` add the statement's own line first.

```teko
/**
 * emit_cov_line — under an active CovCtx, prepend a `tk_cov_line(<line>)` mark keyed on the owning
 * fn (via the fn-stack the TestCov main already set with tk_cov_enter). No-op when `ctx.on` is
 * false (byte-identical) or `line == 0` (unpositioned — the runtime tk_cov_line also guards 0).
 *
 * @param buf   the emission buffer
 * @param ctx   the coverage context (on/off + owning fn index)
 * @param line  the source line of the statement being emitted (0 = skip)
 * @param indent the current C indentation
 * @return the buffer with the mark appended, or `buf` unchanged when off
 * @since #265
 */
fn emit_cov_line(buf: []byte, ctx: CovCtx, line: u32, indent: str) -> []byte {
    if !ctx.on { return buf }
    if line == 0 { return buf }
    cb(cb_u128_digits(cb(cb(buf, indent), "tk_cov_line("), line to u128), ");\n")
}
```

Call it at the head of `emit_stmt` (once, on `s`'s line) — that is the single statement
chokepoint, so ONE edit covers bindings/assigns/returns/expr-stmts/loops. Verify the line field
name on each `TStatement` case (`b.line`/`a.line`/`r.line`/`x.line`/`l.line`) — the checker
carries a `line` on each; the VM reads `e.line` off the expr, so prefer the statement's primary
expr line where a stmt has no own line, matching `cov_walk_stmt` (which recurses into the expr).

**A3 — Emit `tk_cov_branch` at the `if` chokepoints (statement + value + tail forms).**
Three emission sites, each mirroring one VM `cov_branch` call:

- `emit_if_stmt` (`codegen.tks:5288`): after `if (<cond>) {\n`, emit `tk_cov_branch(<line>,<col>,0);`
  at the top of the then-block; in the `else {` (synthesize an `else` even when `!has_else` so the
  outcome-1 mark exists — the VM marks outcome 1 on the SKIP path too, `vm.tks:3701`) emit
  `tk_cov_branch(<line>,<col>,1);`. **Static-walk parity:** `cov_addsite(w, e.line, e.col, 2)` —
  2 outcomes always, so a bare `if` with no else STILL has a total of 2; without the synthesized
  else mark, outcome 1 can never be hit → the branch floor is unreachable at 100%. This is the
  ONE place the codegen must add an `else` it did not have. Guard it under `ctx.on` so non-cov
  output is byte-identical.
- `emit_if_value` (`codegen.tks:3191`) + `emit_exprstmt_tail`'s TIfExpr arm (`codegen.tks:5177`):
  same two marks at the head of each branch. The tail form already emits an explicit `else` when
  `has_else`; when `!has_else` it delegates to `emit_if_stmt`, so covered by the above.
- Use `e.line` / `e.col` off the `TExpr` node (the `if`'s own position), matching
  `cov_addsite(w0, e.line, e.col, 2)`.

```teko
/**
 * emit_cov_branch — under an active CovCtx, emit one `tk_cov_branch(line, col, outcome)` mark for a
 * taken branch outcome (the VM's exec_if/exec_match cov_branch twin). No-op when off → byte-identical.
 *
 * @param buf     the emission buffer
 * @param ctx     the coverage context
 * @param line    the branch site's source line (the if/match expr's `e.line`)
 * @param col     the branch site's column (`e.col`) — part of the packed branch id
 * @param outcome 0/1 for if (then/else), arm index for match
 * @param indent  the C indentation of the branch body
 * @return the buffer with the mark appended, or `buf` unchanged when off
 * @since #265
 */
fn emit_cov_branch(buf: []byte, ctx: CovCtx, line: u32, col: u32, outcome: u64, indent: str) -> []byte {
    if !ctx.on { return buf }
    mut b = cb(cb(buf, indent), "tk_cov_branch(")
    b = cb(cb_u128_digits(b, line to u128), ", ")
    b = cb(cb_u128_digits(b, col to u128), ", ")
    cb(cb_u128_digits(b, outcome to u128), ");\n")
}
```

**A4 — Emit `tk_cov_branch` at the `match` chokepoints (statement + value + tail forms).**
`emit_match_stmt` (`codegen.tks:4701`), `emit_match_value` (`codegen.tks:4547`), `emit_match_tail`
(`codegen.tks:4616`). Each lowers arms to an if/else chain (or switch). At the TOP of arm `i`'s
body emit `tk_cov_branch(e.line, e.col, i)`. `n = m.arms.len` outcomes — matches
`cov_addsite(w0, e.line, e.col, m.arms.len)` (`vm.tks:3796`). The subject is evaluated once
before the chain (its own line already marked by A2's statement mark on the enclosing stmt).

**A5 — Bracket production fn bodies with the fn-stack so interior marks key correctly.**
Per §1.1: the interior `tk_cov_line`/`tk_cov_branch` marks read `tk_fn_stack[sp-1]`. The TestCov
`main` enters only the TEST index. Fix: in `emit_function_cov`, under an active CovCtx, wrap the
body with `tk_cov_enter(<fn_idx>);` right after the existing `tk_cov_mark(<fn_idx>);` prologue and
ensure `tk_cov_leave();` fires on EVERY exit edge. Because tracking every C `return` edge is
fragile (the body has many), use the RAII-free alternative that the runtime already supports:

- **Recommended (exit-edge-proof):** do NOT use enter/leave for production bodies. Instead pass
  `fn_idx` in the mark itself. The runtime `tk_cov_line(line)` / `tk_cov_branch(line,col,outcome)`
  currently take NO fn argument (they read the stack). Add TWO sibling runtime entry points to the
  MAINTAINED runtime (`teko_rt.{c,h}` — permitted C, runtime seam, NOT a frozen twin):
  `tk_cov_line_at(uint64_t fn, uint32_t line)` and
  `tk_cov_branch_at(uint64_t fn, uint32_t line, uint32_t col, uint64_t outcome)` that take the fn
  index EXPLICITLY (delegating to the same `tk_line_id`/`tk_branch_id` packing, bypassing the
  stack). Codegen (A2–A4) emits `_at` with `ctx.fn_idx`. This removes ALL exit-edge bookkeeping —
  no enter/leave inside bodies, no leave-on-every-return. Declare in `teko_rt.h`, define next to
  `tk_cov_line`/`tk_cov_branch`, expose as `teko::cov_line_at`/`teko::cov_branch_at` builtins in
  `codegen.tks:2075`-area (the same `else if last == "cov_line"` dispatch block). The VM continues
  to use the stack-based `cov_line`/`cov_branch` unchanged (it has a live enter/leave via eval_call).
- **Alternative (no runtime change, more fragile):** enter/leave brackets on entry + before every
  return + at fall-through. Rejected as default — the body has too many return edges (variant
  wraps, diverging arms, defers) to bracket reliably; a single missed leave corrupts the stack for
  the rest of the run. Note as the fallback if adding runtime entry points is vetoed.

**A6 — Wire the native floors to line/branch.** In `project.tks`, replace `native_function_floor`
(`project.tks:719`) with the full three-floor `coverage_gate_result` (`project.tks:700`) — which
already reads `vm::line_coverage_pct`/`branch_coverage_pct`/`coverage_pct` off the merged sinks.
After A2–A5 the native run populates all three sinks, so `coverage_gate_result` (the VM path's own
floor fn) works VERBATIM on the native path. `run_gate_native` (`project.tks:791`) calls
`coverage_gate_result(dir, fe)` instead of `native_function_floor(dir, fe)`. This is the crumb
that CLOSES #265 (native enforces all three floors). Update the `native_function_floor` doc-comment
region — it becomes dead; delete it.

**A7 — Update `run_gate_native`/`run_native_gate` doc-comments** to state the three floors are now
native, and drop the "line/branch floors are NOT enforced here" honest-stop language
(`project.tks:783-790`, `project.tks:727-733`). No behavior change beyond A6; comment-truth only.

### Track B — #168 compile-once partition (the sub-second win)

**B0 — Blocked-dependency note.** #168 Phase B (per-context test binaries as SEPARATE processes,
`.tkb` as the load artifact) depends on **C7.16** (program-level `.tkb` codec — today expr-only).
Do NOT design against a `.tkb` load path that does not exist. What is UNBLOCKED and delivers the
#265 speed win WITHOUT C7.16:

**B1 — Emit the test TU ONCE per gate, not per-invocation (Phase A, unblocked).** Today
`run_native_gate` emits + `run_cc`'s the TestCov TU on every gate call. Within a single
`compile_project_g` there is exactly one gate call, so the per-invocation cost is already once;
the DOUBLING the compile-time doc names is at the CI level (VM gate + native gate both run). The
compiler-side "compile once" that matters: the TestCov TU shares the SAME front-end product (`fe.prog`)
as the release build — the front-end (lex/parse/check/mono, ~17 s) is ALREADY done once and reused
(`run_gate_native` takes `fe`). The native gate's only ADDED cost over `--no-verify` is
`tk_emit_c_test` + `run_cc` + run + merge. Confirm `fe.prog` is threaded (it is) — no crumb needed
beyond documenting that Phase A "compile once" = "reuse `fe`, do not re-front-end", which the code
already does. The DEAD weight is the SECOND cc of a near-identical TU (release TU vs test TU differ
only in `main`): acceptable — one extra cc of 102k lines = 0.7 s (#249). **Phase A is effectively
already satisfied; the crumb is to DOCUMENT that and NOT to add a re-front-end.**

**B2 — Per-process isolation for the MEMORY win (Phase B-lite, unblocked without C7.16).** The
memory balloon (1595 MB) is the VM interpreting tests IN-PROCESS. The native gate already runs the
tests in a CHILD process (`teko::process::run`, `project.tks:748`) → the compiler's own peak drops
to the codegen-only 366 MB the moment the VM gate is no longer the default. This is Phase B's core
benefit (OS reclaims between contexts) achieved by process boundary ALONE, without needing
`.tkb`-per-context. The full Phase B (one process PER test-context for even lower child peak) needs
C7.16 and is deferred; the child test binary's own peak is small (it runs compiled code with real
stack frames, no functional-env), so B2-lite already lands the 1595→366 win. **Crumb: none in the
compiler — this is a CONSEQUENCE of A6 + the QW-1a CI flip; document it as the memory deliverable.**

**B3 — (BLOCKED on C7.16) Phase B full — per-context `.tkb` test binaries.** Design-ahead only:
when C7.16 lands, partition `#test` by context (namespace/file), emit + cc one test TU per context,
run each as a separate child (the OS reclaims between them → child peak < 1 GB even for a corpus
that grows past today's). The `.tkcov` merge already unions across processes, so N child dumps merge
into one sink set for the floor walk — the protocol is ALREADY multi-process-correct. Leave a
doc-comment honest-stop in `run_native_gate` naming C7.16 as the blocker. Do NOT implement.

### Track C — the CI flip (QW-1a, CI-config, after A6 lands)

**C1 — Make native the DEFAULT gate; demote VM to a periodic lane.** After A6 (native enforces all
three floors), `run_gate` (`project.tks:807`) default flips: `compile_project_g` passes
`native_gate = true` by default (or `native_gate_of` defaults true unless `--vm-gate` opts out).
`native.yml`: drop the "Test gate (VM)" step (`native.yml:82`) from the 5 build-test platforms;
keep ONE nightly/periodic lane running `teko test .` (the VM) over the full corpus for VM regression
(#265 governance: the VM lane stays as a SEPARATE CI job, not per-PR). `diff-vm-native`
(`native.yml`) keeps proving VM==native on the 79 `examples/regressions/` fixtures. This realizes
the 16m→~5-6m + the memory win. **This is CI-config, gate-able independently, LOW risk once A6
proves the three native floors match the VM floors on the fixtures (§4 A-parity fixture).**

## 3. Type signatures / function shapes (what the implementer adds)

New (codegen.tks): `type CovCtx = struct { on: bool; fn_idx: u64 }`; `fn cov_off() -> CovCtx`;
`fn emit_cov_line(buf: []byte, ctx: CovCtx, line: u32, indent: str) -> []byte`;
`fn emit_cov_branch(buf: []byte, ctx: CovCtx, line: u32, col: u32, outcome: u64, indent: str) -> []byte`.
New (teko_rt.h/.c — MAINTAINED runtime seam, permitted C): `void tk_cov_line_at(uint64_t fn, uint32_t line);`
`void tk_cov_branch_at(uint64_t fn, uint32_t line, uint32_t col, uint64_t outcome);` (delegate to the
existing `tk_line_id`/`tk_branch_id` packing, bypassing the fn-stack).
New builtins (codegen.tks dispatch ~`:2075`): `cov_line_at` → `tk_cov_line_at`,
`cov_branch_at` → `tk_cov_branch_at`.

Touched (codegen.tks): thread `CovCtx` through `emit_block_tail`, `emit_stmt`, `emit_exprstmt_tail`,
`emit_if_stmt`, `emit_match_stmt`, `emit_expr_ctx`, `emit_if_value`, `emit_match_value`,
`emit_arm_value`, `emit_branch_value`, `emit_block_region`; construct a live one in
`emit_function_cov` (`:5895`); inject calls per A2–A5.
Touched (project.tks): `run_gate_native` (`:791`) calls `coverage_gate_result` not
`native_function_floor`; delete `native_function_floor` (`:719`); doc-comment truth in
`run_native_gate` (`:734`). Default flip in `compile_project_g`/`native_gate_of` (Track C).
Untouched: `line_coverage`/`branch_coverage`/`functions_coverage`/`cov_walk_*`/`cov_merge`
(engine-independent — they already work once the sinks are populated). The VM's own
`cov_line`/`cov_branch` marking (`vm.tks:2157/3697/…`) is UNTOUCHED (VM lane unchanged).

## 4. Regression fixtures (inputs → expected exit codes)

Add under `examples/regressions/` (the `diff-vm-native` harness set) + `.tkt` gate fixtures. Each
runs on BOTH engines during the transition (the diff harness stays), and the NATIVE exit is the
authority per the VM=dev/native=production ruling.

1. **cov_line_native_parity** — a fn with N distinct executable lines, a `#test` exercising a
   subset; assert `line_coverage_pct(native-merged) == line_coverage_pct(vm)`. Native exit 0.
2. **cov_branch_if_native** — a fn with an `if`/`else` where only the then-branch is tested; the
   branch floor must count 1/2 taken on BOTH engines (proves the synthesized-else outcome-1 mark
   and the A3 site). Under-floor ⇒ nonzero exit (floor failure); at-floor ⇒ 0.
3. **cov_branch_if_no_else_native** — a bare `if` (no else) fully tested on the then-side only;
   MUST read 1/2 (not 1/1) — pins the "2 outcomes always" static-walk parity and the synthesized
   else mark. This is the fixture that catches the §1.1/A3 trap.
4. **cov_branch_match_native** — a `match` with 3 arms, 2 tested; branch floor 2/3 on both engines.
5. **cov_fn_idx_attribution** — TWO production fns on the SAME source line numbers in DIFFERENT
   files (cross-file line collision, the #109 family), each with a branch; assert the native marks
   attribute to the correct `fn_idx` (not conflated). Proves A5 (`_at` explicit-fn keying). This is
   the fixture that would FAIL under the naive stack-only approach where all marks key on the test.
6. **cov_off_byte_identity** — build the SAME project with `Program` mode (a normal `teko . -o out`)
   BEFORE and AFTER the patch; the emitted `teko.c` must be byte-identical (no `tk_cov_*` leaked
   into non-TestCov output). This is the FIXPOINT guard as a fixture (see §5).
7. **native_all_three_floors** — a project whose `[coverage]` floors (functions/lines/branches) are
   all set; a passing test set exits 0 on `teko . -o out --native-gate`; dropping one test below a
   floor exits nonzero with the matching floor message. Proves A6 (native enforces all three).
8. **A-parity (the Track-C unblock gate)** — over the 79 `examples/regressions/` fixtures, assert
   `{functions,lines,branches}_coverage_pct` are EQUAL VM-vs-native. This is the fixture the CI flip
   (C1) waits on: only when native==VM on all three floors across the corpus may the VM lane leave PR.

## 5. Ritual points (where the FULL gate must pass) + fixpoint survival

- **After A1** (thread `CovCtx`, all call-sites `cov_off()`): FULL gate. This crumb MUST be
  byte-identical (fixture 6) — it only adds an unused parameter. Gate proves no accidental drift.
- **After A5** (all instrumentation + `_at` keying): FULL gate on a project WITH `--native-gate`;
  the native run must populate line/branch sinks and the floors must MATCH the VM (fixtures 1–5).
- **After A6** (native enforces three floors): FULL gate; `run_gate_native` now three-floor. This is
  the #265-closing ritual.
- **Before C1** (CI flip): fixture 8 (A-parity) GREEN across the 79 fixtures — the mandatory
  precondition to demote the VM lane (main-integrity: never drop a floor that catches a regression).

**Fixpoint (gen1.c==gen2.c byte-identity) survival — the non-negotiable invariant.** The fixpoint
compares the `Program`-mode emission (the release TU). ALL #265 instrumentation is gated on
`CgMode::TestCov` / `CovCtx.on` — the `Program` path constructs `cov_off()` everywhere, so it emits
ZERO `tk_cov_*` bytes → the release `teko.c` is byte-for-byte unchanged → **the fixpoint is
structurally untouched.** Fixture 6 makes this a REGRESSION-GATED fact, not a hope. The `_at` runtime
additions (A5) are new SYMBOLS in `teko_rt.c`; they are referenced only by the TestCov TU, never by
the `Program` TU, so they do not appear in the release emission and cannot perturb the fixpoint. The
native gate's coverage MERGE (`vm::cov_merge`) is order-independent (dedup'd id sets) → no ordering
hazard. **No crumb in this plan touches `emit_program_main` or the `Program` branch of
`emit_mode_main` — that is the fixpoint oracle and it stays frozen.**

**Coverage-floor survival.** The floors are computed by the SAME engine-independent static walk
(`line_coverage`/`branch_coverage`) reading the SAME packed ids. Native populates them with the SAME
`tk_line_id`/`tk_branch_id` packing the VM uses (A2–A5 emit the same `(fn,line)` / `(fn,line,col,
outcome)` tuples the VM marks). Fixture 8 proves EQUALITY before the VM lane is demoted → the floor
bar cannot silently drop. The `[coverage]` floor VALUES in `teko.tkp` are unchanged.

## 6. Risks + law tensions (recommended resolution)

- **R1 (keying, HIGH) — interior marks attributing to the wrong fn.** The stack-only approach
  (§1.1) makes every native line/branch mark key on the TEST, not the production fn → false 0%.
  **Resolution: A5's `tk_cov_line_at`/`tk_cov_branch_at` explicit-fn entry points** (permitted
  runtime C, `teko_rt.{c,h}` is the maintained seam, NOT a frozen twin — Teko-only law satisfied).
  Fixture 5 is the guard. This is the single most important design decision in the plan.
- **R2 (byte-identity, MED) — instrumentation leaking into `Program` output.** A stray unguarded
  emit breaks the fixpoint. **Resolution: `CovCtx.on` gates EVERY emit; fixture 6 is a hard gate
  after A1 and after A5.** Recommend running fixture 6 as the FIRST check in the ritual.
- **R3 (branch totals, MED) — `if` without `else`.** The static walk counts 2 outcomes for every
  `if`; codegen must emit the outcome-1 mark on the skip path even when the source has no `else`
  (A3). Missing it makes the branch floor unreachable at 100% → a false floor FAILURE (build breaks)
  or, worse, a lowered floor to compensate (regression). **Resolution: A3 synthesizes the else mark
  under `ctx.on`; fixture 3 pins 1/2.**
- **R4 (double cc cost, LOW).** The native gate cc's a second TU (test) alongside the release TU.
  #249 measures cc at 0.7 s / 2.4% — negligible; the net is still a massive win (kills the 15.6 s VM
  interpret). Documented in B1; no action.
- **R5 (law tension — main-integrity vs demoting the VM lane).** `teko-main-integrity-absolute`:
  never weaken the gate so a regression slips. Demoting the VM lane (C1) is safe ONLY if native
  enforces EQUAL floors. **Resolution: fixture 8 (A-parity across 79 fixtures) is the mandatory
  precondition; the VM lane stays as a nightly job (#265 governance) so VM regressions are still
  caught. No genuine tension remains — the laws RESOLVE it (equal-or-stronger floors + retained VM
  nightly).** Not a HALT.
- **R6 (C7.16 dependency — #168 Phase B).** Full per-context `.tkb` partition is BLOCKED on C7.16.
  **Resolution: B1+B2-lite (process-boundary isolation) deliver the 1595→366 memory win and the
  speed win WITHOUT C7.16; B3 is design-ahead only with an honest-stop doc-comment.** No blocker for
  the keystone's headline deliverables.

**No HALT.** Every fork resolves law-first: the runtime seam is maintained C (Teko-only satisfied),
the fixpoint is structurally protected (Program path frozen), the floors are equal-or-stronger
(main-integrity satisfied), and the #168 blocker is routed around for the headline wins. Proceed.

## 7. What remains BLOCKED (explicit)

- **#168 Phase B (full per-context `.tkb` test binaries)** — BLOCKED on **C7.16** (program-level
  `.tkb` codec). The keystone's memory+speed headline (1595→366 MB, 15.6 s→sub-second) lands WITHOUT
  it via A6 + B1 + B2-lite + C1. B3 is scaffolded as an honest-stop doc-comment only.
- Everything in Tracks A and C, plus B1/B2, is UNBLOCKED and implementable today against the
  declared shapes above.
