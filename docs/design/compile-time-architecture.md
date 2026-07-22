# Compile-time / CI wall-clock architecture

Status: DESIGN (architect). Companion to the memory review in
`memory/teko-arena-lifetime-observability.md` (the *memory* axis) and
`memory/teko-selfbuild-perf-diagnosis.md` (issue #249, the *time* axis this doc
supersedes-and-extends). This doc is the compile-**time** analog: it grounds the CI
wall-clock, names the shared villain (the VM test-gate), sequences the fixes, and
evaluates the owner's own-backend+linker hypothesis per path.

> **The one-line answer.** The 16m screenshot has a NEW cause since it was taken:
> `--native-gate` (#265) is now an always-on SECOND gate on every `build-test` job,
> so each platform runs the whole `#test` corpus **twice** (VM + native). That, not
> riscv-qemu, is today's critical path (**windows-arm64 ≈ 16m**). Making the native
> gate the SOLE gate (drop the duplicate VM lane, keep VM as a periodic/dev lane) +
> weakening riscv to smoke takes 16m → **~5-6m** with zero compiler work. Everything
> else (own backend, parallelism) is strategic, not the immediate lever.

---

## 0. TL;DR sequencing

| # | Change | Layer | Wall-clock saved | Risk | Blocks on |
|---|--------|-------|------------------|------|-----------|
| **QW-1** | Native gate becomes the SOLE build-test gate (drop the duplicate VM step); VM gate → 1 periodic lane | CI-config | **~8-9m** (biggest) | LOW-MED | #265 line/branch cov (have fn cov) |
| **QW-2** | riscv64-qemu → SMOKE (build + ~30-test arch subset, drop `--coverage`) | CI-config | ~7m off the riscv job | MED (owner ruling) | — |
| **QW-3** | release-cross-smoke → x86_64-glibc only on PR; full 6-target on nightly/release | CI-config | ~3m | LOW | — |
| **QW-4** | Fixpoint (gen1==gen2) is release-only, never on PR | CI-config | (already true) | — | — |
| **B (#265 full)** | Native gate with line+branch coverage → retire VM gate as default everywhere | compiler | folds QW-1's residual | MED | #265 cov instrumentation |
| **C** | Coverage zero-cost when off (`tk_cov_*`/`tk_obs_enabled` out of the hot loop) | compiler | ~1-2s/gen | LOW | — |
| **D** | VM Env O(n)→hash (dev-lane throughput) + already-fixed lexer | compiler | dev-only after B | LOW | — |
| **E** | Parallelism across files/functions | compiler | 2-4× the ~17s front-end | HIGH (determinism) | S8 threading |
| **Strategic** | Own backend + M-linker | compiler | cross-compile + independence (NOT self-build/qemu) | fase-4 | #222/#223/#225/#226 |

**Projected PR wall-clock after QW-1..QW-3: ~5-6m** (down from 16m), no compiler change.
**After B+C: ~4m.** Own-backend is a fase-4 strategic play, not in this number.

---

## 1. Attribution map (grounded)

### 1.1 Per-job timings — the CURRENT critical path (run 28763999356, PR #296, native.yml, success)

```
   5s  changes
 973s  build-test / windows-arm64          ← CRITICAL PATH (~16m)
 535s  build-test / linux-riscv64 (qemu)
 332s  build-test / linux-arm64
 309s  build-test / linux-x86_64
 260s  diff VM==native / ubuntu-latest
 254s  release cross-compile smoke (zig)
 215s  build-test / windows-x86_64
 184s  build-test / macos-arm64
 120s  diff VM==native / macos-latest
   3s  CI gate
```
Wall-clock = **988s (~16m28s)**. Consistent across the last 3 PRs (988s / 972s / 954s),
critical path = **windows-arm64** every time.

**★ The screenshot is stale.** The owner's 16m31s / riscv-8m55s screenshot was taken
before `--native-gate` (#265) was wired as an always-on step into every `build-test`
job. It is NO LONGER the shape of the pipeline. Today riscv is 535s (not 535s critical);
the critical path is windows-arm64 at ~16m because it runs BOTH gates.

### 1.2 What blew up windows-arm64 — the native gate is a SECOND full gate

`build-test` step breakdown (run 28763999356):

```
build-test / windows-arm64  (973s)
   359s  Test gate (VM)                      ← teko . -o bin          (VM interprets 863 #test)
   591s  Native test gate (opt-in)           ← teko . -o gen1-native --native-gate
build-test / linux-x86_64   (309s)
   121s  Test gate (VM)
   181s  Native test gate (opt-in)
build-test / macos-arm64    (184s)
    75s  Test gate (VM)
   100s  Native test gate (opt-in)
```

Both steps do a FULL self-build of the corpus + run all **863 `#test`** functions. The
native gate additionally **emits + cc-compiles** a test-profile TU (`run_native_gate`,
`src/build/project.tks:733`) before running it — so it is not cheaper than the VM gate
on a cold corpus; it is comparable-or-worse per run, AND it is ADDITIVE (a second gate,
not a replacement). native.yml:78-90 runs both back-to-back. **That doubling is the
regression.** windows-arm64 is the worst because ARM Windows runners are the slowest in
the matrix and it pays the doubling on the slowest hardware.

The native gate was added (per its own doc-comment, native.yml:88 and #265) as an *opt-in
correctness lane* — "it caught 3 latent native miscompiles pre-merge". Good signal, wrong
placement: a correctness lane does not need to be the DEFAULT on all 5 platforms on every
PR while ALSO paying the VM gate.

### 1.3 riscv64-qemu step breakdown (535s — the ex-critical-path)

```
build-test / linux-riscv64 (qemu)  (535s)
    19s  Install cross-compiler + QEMU + host clang
    48s  Emit the current compiler's C (gen-1, gate skipped)   ← teko . -o gen1 --no-verify (native x86_64)
     7s  Cross-compile gen-1's teko.c for riscv64 (static)
   452s  Run the gate under QEMU user-mode                       ← qemu-riscv64-static ./teko-riscv test . --coverage
```

**85% of the riscv job (452s) is RUNNING the full `test . --coverage` under emulation.**
Emulated riscv is ~10× native; the same gate is ~45s native. This is the job the owner
screenshotted at 8m55s (it varies 452-535s with runner load). native.yml:143.

### 1.4 release-cross-smoke (254s) — a release concern on every PR

```
release cross-compile smoke (zig)  (254s)
     6s  Install zig
    64s  Emit teko.c (one gen, no gate)          ← teko . -o out --no-verify (native)
   114s  Cross-compile all six Linux targets      ← 6× zig cc over teko.c (compile-only)
    66s  Run the x86_64-glibc cross artifact       ← miscompile guard
```
`cross_compile_linux.sh` builds 6 targets: {x86_64,arm64,riscv64} × {glibc,musl}
(scripts/cross_compile_linux.sh:86-91). ~19s/target average via `zig cc`. This is pure
release-risk insurance running on every source PR.

### 1.5 diff-VM==native (260s / 120s) — legitimately needed, but re-builds gen1

```
diff VM==native / ubuntu-latest  (260s)
   126s  Build gen1 (seed compiles PR corpus → bin/teko)   ← a THIRD full self-build
    93s  gen1 self-test (bin/teko test .)                   ← a THIRD full VM gate
    23s  Differential harness (VM==native over 79 fixtures)
```
This job self-builds AGAIN and runs the VM gate AGAIN. The 23s differential harness (the
actual unique value — VM==native over `examples/regressions/` 79 fixtures) is a small tail
on a 237s build+gate prefix.

### 1.6 Compiler-intrinsic cost — the ~28s/gen decomposition (from #249, sample-based)

| Phase | Cost | Nature | file:line hotspot |
|---|---|---|---|
| Front-end (lex/parse/check/mono) of 880-item corpus | ~17s | native, single-thread | churn (below) |
| **VM test-gate (interpret 863 `#test`)** | **~15.6s** | native exe running the VM interpreter | `vm::run_tests_cov` `src/vm/vm.tks:3946` |
| coverage bookkeeping (even OFF) | ~1-2s | hot-loop `tk_cov_*` | `tk_cov_branch/line/mark`, `tk_obs_enabled` |
| `cc` on emitted teko.c (102k lines) | **0.69s (2.4%)** | host cc | NOT a bottleneck |

Measured splits (#249): `build --no-verify` = 17.7s (load+codegen+cc, no tests) vs `build`
gated = 33.2s → **the VM interpret of the test bodies alone = 15.6s = 47% of a gated build.**

**The named hotspots inside those phases:**
- **VM Env is O(n) per read AND O(n²) per frame.** `Venv.slots: []Slot` scanned newest-first
  (`find`, `src/vm/vm.tks:238`), and `define` (`vm.tks:261`) grows it via a functional
  `teko::list::push` rebuild. Every variable read is a linear scan; a frame with k bindings
  and k reads is O(k²). The `cells` store (`vm.tks:228`) is ALSO a functional rebuild on
  every `Ref` write (its own doc-comment: "O(n) ... fine for a test VM"). This is why
  interpreting 863 tests is slow — it is not one villain, it is O(n²) env in the hot loop.
- **The O(N²) lexer is ALREADY FIXED.** `src/lexer/lexer.tks:637` — "each byte crossed once
  → replacing the old O(N²) stamping." The monotonic-cursor rewrite landed. The #249 note
  listing "the O(N²) lexer" as a live lever is stale; do NOT re-attack it.
- **alloc/copy churn** — `tk_slice_push_fo/_r` + `_platform_memmove` (heaviest leaf in the
  #249 sample) — this is the SAME churn the memory review attacked. It is now much reduced
  (R3b right-size first rung, memory doc), but the residual per-`push` memcpy is the shared
  cost of both memory and time.

### 1.7 Separating CI-config cost from compiler-intrinsic cost

- **CI-config cost (removable by workflow edits, no compiler change):**
  the duplicate native gate (~half of every build-test job), qemu emulation (452s), 6-target
  cross-compile on PR (114s), diff-job re-building gen1 (126s+93s), 5-platform matrix breadth,
  seed provisioning (2-6s, negligible). **This is ~60% of the 16m wall-clock and it is the
  cheap win.**
- **Compiler-intrinsic cost (needs compiler work):** the ~28s/gen — dominated by the VM
  test-gate (15.6s) and front-end churn (~17s). `cc` is 0.7s and irrelevant.

---

## 2. The shared-villain thesis: the VM test-gate

**One mechanism drives BOTH the memory balloon AND the compile-time balloon: interpreting
the 863 `#test` functions in the functional-env VM.**

- **Memory:** `memory/teko-arena-lifetime-observability.md` (2026-07-05 re-measurement):
  codegen-only build = 366 MB; the FULL gate = **1595 MB**. The +1229 MB delta is "the VM
  interpreting the `#test` with env functional O(n)-rebuild". The compiler self-reports
  `peak 1521.7 MB`. The gate is ~77% of peak memory.
- **Time:** #249: the VM interpret of test bodies = 15.6s = 47% of a gated self-build; and
  in CI it is paid TWICE (VM gate + native gate) plus a THIRD time in diff-vm-native.

Both memory notes and both time notes converge on the SAME sentence: **"o fix próprio é #265
(gate nativo) / #168 (test-split)."** The native subprocess has no functional-env cost (it
runs compiled machine code with real stack frames and mutable locals), and process isolation
lets the OS reclaim between contexts. #265 is therefore the single highest-leverage compiler
fix for both axes.

**Quantified leverage of #265 (native gate replacing VM gate as the SOLE default):**
- Memory: 1595 MB → ~366 MB + the native runner's own footprint (a separate process, so the
  compiler's own peak drops to the codegen-only 366 MB; the child test binary is small). Net:
  the gate stops inflating the COMPILER's peak entirely. This is the ~930 MB the #249 note and
  the memory note both attribute to gate+coverage.
- Time (steady state, AFTER #168 partitioning so the native gate compiles the test TU ONCE):
  the 15.6s VM interpret → a native run of ~sub-second (native exec is ~100× the VM). The
  `run_native_gate` doc-comment (`src/build/project.tks:731`) states this goal verbatim:
  "Replaces the 15.6s VM interpret ... with a sub-second native run."

**Why the native gate is SLOWER today (the paradox to resolve).** `run_native_gate`
(`project.tks:733`) currently, per invocation: `tk_emit_c_test` the whole `#test` corpus →
`run_cc` it → run it → merge `.tkcov`. The emit+cc of a fresh test TU is NOT free on a cold
corpus, and it happens IN ADDITION to (not instead of) the front-end work already done for the
build. So on a single PR gate it is comparable to the VM gate, not 100× faster — hence the
owner's 2026-07-05 ruling "opt-in, not default." The sub-second win only materializes once
#168 splits the harness so the test TU is compiled ONCE and the run is the only per-gate cost.

### 2.1 #265 designed concretely enough to sequence

The infrastructure EXISTS (opt-in) — this section is the ratification path to make it the
default and reclaim the win.

**a) How `#test` compiles to a native binary.** `codegen::tk_emit_c_test(prog, cov_on)`
(called at `project.tks:740`) emits a C TU whose `main()` calls each `#test` in order,
fail-fast: a failed assert panics → non-zero exit (M.1). This TU is cc-compiled with the same
`run_cc` the build uses and executed as a child process (`teko::process::run`, `project.tks:748`).

**b) How coverage is collected natively.** The child sets `TEKO_TKCOV=<path>` and, on exit,
dumps its marks to a `.tkcov` file; the parent merges via `vm::cov_merge(covfile)`
(`project.tks:750`). The engine-independent static walk (`vm::*_coverage_pct`) then reads the
merged sinks exactly as it read the VM's in-process marks. **GAP (the reason it is not yet
the default):** native instrumentation today covers **FUNCTION coverage only**
(`native_function_floor`, `project.tks:793`); LINE and BRANCH floors are NOT enforced on the
native path (per its doc-comment: "native has no per-expression instrumentation yet"). To make
the native gate the sole gate we must emit per-line/per-branch `tk_cov_mark` calls in the
test-profile codegen (the codegen prologue at `codegen.tks:5892` already knows the
`TestCov` profile and `cov_idx`). THIS is the concrete work item that closes #265 to default.

**c) What the gate runner becomes.** `run_gate` (`project.tks:807`) already dispatches:
`native_gate ? run_gate_native : run_gate_vm`. Making native the default is a one-line flip of
the dispatch PLUS the line/branch instrumentation above PLUS the #168 compile-once split so it
is actually faster. The CLI/env plumbing (`--native-gate` / `TEKO_NATIVE_GATE=1`,
`native_gate_of` `project.tks:903`) is done.

**d) Does the VM gate stay?** YES — as a fallback/dev/WASM lane, NOT the default:
- The VM=dev/WASM, native=production ruling (`memory/teko-no-gc-vm-role.md`) says native is
  authoritative on correctness; the native gate is MORE production-representative.
- The VM gate remains the WASM test lane (#224) — the WASM build embeds the VM anyway, so the
  VM lane IS the WASM test path (#249 note).
- `diff-vm-native` already proves VM==native on the 79 `examples/regressions/` fixtures, so
  divergence is caught structurally; the VM lane need only run PERIODICALLY (nightly), not on
  every PR.

**Sequencing:** #265-line/branch-cov (compiler) → #168 compile-once split (compiler) →
flip native to default + demote VM to nightly (CI-config). QW-1 below realizes the CI-config
half IMMEDIATELY (drop the duplicate) even before the sub-second win lands.

---

## 3. CI-config quick wins (low-risk, high-impact, no compiler change)

Ranked by wall-clock saved vs risk. Each states the exact native.yml edit, the correctness
tradeoff, and the main-integrity law it respects (`memory/teko-main-integrity-absolute.md`:
never weaken the gate so a real regression slips).

### QW-1 — Native gate is the SOLE build-test gate; VM gate → periodic lane  ★ BIGGEST WIN

**The regression fix.** Today build-test runs BOTH "Test gate (VM)" (native.yml:78-80) and
"Native test gate" (native.yml:88-90). Pick ONE per PR.

Two options, owner's call (see HALT-1):
- **1a (recommended once #265 line/branch cov lands):** drop the VM step, keep only
  `teko . -o gen1-native --native-gate`. Saves the VM gate on all 5 platforms
  (75-359s each). windows-arm64: 973s → ~600s.
- **1b (available TODAY, before #265 cov):** drop the *native* step from build-test (keep VM),
  and run the native gate on ONE platform (linux-x86_64) as the correctness lane it was
  designed to be. windows-arm64: 973s → ~360s; linux-x86_64 keeps both. This immediately
  un-doubles 4 of 5 platforms with ZERO compiler dependency.

**Tradeoff:** 1b keeps the full VM floors (fn+line+branch) everywhere and the native
correctness signal on x86_64 — strictly no gate weakening. 1a needs #265 line/branch cov first
or it drops line/branch floors on 4 platforms (unacceptable under main-integrity until cov lands).
**Recommend 1b NOW, 1a after #265 cov.**

**Law:** main-integrity preserved — the gate still runs fully on every platform (1b) or with
equal-or-stronger native floors (1a-after-cov). Saves **~8-9m off the critical path** (1b).

### QW-2 — riscv64-qemu → SMOKE (build + minimal arch subset, drop `--coverage`)  ★ HALT-2

**Edit** native.yml:143:
```yaml
# was: qemu-riscv64-static ./teko-riscv test . --coverage      (452s)
- name: Run an ARCH-CORRECTNESS smoke under QEMU (not the full gate)
  run: qemu-riscv64-static ./teko-riscv test . --test-filter arch --no-coverage
```
(needs a `--test-filter <tag>` CLI on `test`, or a dedicated `tests/arch/` project the smoke
points at — the arch subset = endianness, `__int128`/u128, alignment, struct layout, the
handful of tests that actually exercise arch-divergent codegen.)

**Rationale (grounded).** The FULL functional gate already runs on 5 native platforms incl.
linux-x86_64 and linux-arm64. riscv is emulated — its unique job is proving **arch-correctness**
(endianness / int128 / alignment / calling convention), NOT re-proving language semantics that
x86_64/arm64 already proved. Running the full 863-test `--coverage` gate through a ~10× emulator
is 452s of mostly-redundant work. A ~30-test arch smoke is ~15-30s emulated.

**Tradeoff:** we stop catching a hypothetical riscv-ONLY semantic bug that is NOT arch-related.
This is low-probability (the codegen is arch-parametric C; a semantic bug would show on all
targets) but non-zero. Saves **~7m off the riscv job** (which is currently the 2nd longest).

**Law tension → HALT-2 (owner ruling needed).** native.yml:316-323 documents riscv as BLOCKING
under the main-integrity law ("green means green everywhere"). Weakening it to smoke is a
deliberate scope reduction of a BLOCKING gate. Per the standing "never weaken the gate to the
point a real regression slips" law, this needs the owner's explicit sign-off. **See HALT.**

### QW-3 — release-cross-smoke → x86_64-glibc only on PR; full 6-target on nightly/release

**Edit** the cross-compile step (native.yml:263-264 / `cross_compile_linux.sh`) to build ONLY
`linux-x86_64-glibc` on `pull_request`, and add a `schedule:` (nightly) + keep the full
6-target run in `release.yml` (which already does it). The x86_64-glibc artifact is the one
that gets RUN (the miscompile guard, native.yml:272) — the other 5 are compile-only insurance.

**Tradeoff:** a zig-version/target break on arm64/riscv/musl surfaces on the nightly instead of
the PR. Since release.yml re-runs the full 6-target build at tag time and the nightly catches it
within a day, a broken cross-compile still cannot reach a published release. Saves **~3m**
(114s cross + part of the 64s emit). Low risk — the RUN guard (the actual miscompile catcher)
stays on every PR.

**Law:** release integrity preserved — the full matrix still gates the actual release; only the
PR-time redundancy moves to nightly.

### QW-4 — Fixpoint is release-only (already the case; document + guard)

The 2-gen fixpoint (gen1.c==gen2.c byte-identity) is NOT run on PR native.yml — PR CI builds
gen1 once per platform and runs its gate. Good. Keep it that way; the fixpoint belongs to
`release.yml` (and the #249 open question of whether gen-3 is even needed — a separate
soundness proof). No PR change; this row exists to state the invariant so a future edit does
not accidentally add a fixpoint chain to PR CI.

### QW-5 (opt) — collapse diff-vm-native's redundant gen1 build

diff-vm-native rebuilds gen1 (126s) + runs the VM gate (93s) that build-test already ran. If
build-test uploaded gen1 as an artifact, diff-vm-native could `download-artifact` it and skip
straight to the 23s differential harness. Saves ~200s on that job (not critical-path, but frees
a runner). MED complexity (artifact plumbing across jobs); lower priority than QW-1..3.

### Projected wall-clock after QW-1(1b) + QW-2 + QW-3

```
windows-arm64  973 → ~360   (QW-1b un-doubles)
riscv-qemu     535 → ~90    (QW-2 smoke)
cross-smoke    254 → ~130   (QW-3 x86_64-only)
linux-x86_64   309 → ~305   (keeps both — the correctness lane)
new critical path ≈ linux-x86_64 or windows-arm64 ~360s → ~6m
```
**16m28s → ~5-6m with ZERO compiler change, no gate weakening beyond the riscv-smoke ruling.**

---

## 4. Compiler-speed architecture (28s/gen → ≤15s → lower)

The self-build itself. Ordered levers; every determinism hazard flagged (the fixpoint
gen1.c==gen2.c byte-identity must survive EVERY change).

### Lever B — Native test-gate (the keystone; §2.1). 15.6s → sub-second AFTER #168 split.
The compiler half of #265: emit per-line/per-branch `tk_cov_mark` in the test-profile codegen
(so native enforces all three floors) + #168 compile-once-run-per-context so the test TU is
built once. This alone gets a gated self-build from ~33s toward ~18s (the front-end residual).
**Determinism:** the native runner's coverage merge (`vm::cov_merge`) is order-independent
(dedup'd sinks); no fixpoint hazard — the fixpoint compares teko.c (codegen output), which the
gate engine does not touch.

### Lever C — Coverage zero-cost when off. ~1-2s/gen.
`tk_cov_branch/line/mark` and `tk_obs_enabled` fire in the hot eval loop even when coverage is
OFF (#249 sample: ~510 samples on `tk_obs_enabled` alone). Gate them behind a compile-time-off
fast path (a single hoisted boolean check at loop entry, or codegen that omits the calls when
the profile is not TestCov). **Determinism:** none — pure hot-path removal, output unchanged.

### Lever D — VM Env O(n)→O(1) (dev-lane throughput only, after B).
`Venv.slots` linear scan (`vm.tks:238`) + functional `define` rebuild (`vm.tks:261`) is O(n²)
per frame. Once B makes native the default gate, the VM is dev/WASM-only, so this is
throughput-for-debugging, not gate-critical. Fix: a name→slot index (hash) layered over the
slot list, or a scope-stack of small frames instead of one flat growing list. **The lexer O(N²)
is ALREADY FIXED (`lexer.tks:637`) — do not re-attack it.** **Determinism:** VM-only, does not
touch codegen output → no fixpoint hazard.

### Lever E — Parallelism across files/functions (the ~17s front-end).  ★ determinism-critical
The compiler is single-threaded (#249: user≈real). The front-end is the largest remaining cost
after B. Parallelizable? Analysis of the barrier:
- **Lex + parse** are per-file embarrassingly parallel (each `.tks` → tokens → AST independently).
  **BARRIER:** file DISCOVERY order. The whole `memory/teko-arena-lifetime-observability.md`
  saga is readdir-order bugs; parallel parse must preserve a DETERMINISTIC file order (sort by
  path before dispatch, join in that order) or the merged TProgram item order changes → codegen
  emission order changes → **fixpoint breaks**.
- **Check** is NOT trivially parallel — the type table is program-global (cross-file resolution,
  `type_table_find`, R0-R5 name rules). A parallel checker needs a two-phase collect (parallel
  per-file symbol collection into a shared, order-stable table) then parallel body-check. The
  Env-seal already splits collect/type (`memory` note: "two-segment Env, scope::seal() at the
  collect→type boundary") — that boundary is the natural parallel barrier.
- **Mono + codegen** emit into ordered buffers; parallel emission must write to per-unit buffers
  concatenated in a DETERMINISTIC order.

**Determinism hazards to gate EVERY parallel change against (fixpoint = the oracle):**
1. **readdir/discovery order** — sort paths canonically before any parallel dispatch.
2. **gensym counters** — a shared mutable gensym counter is a data race AND an order-dependence.
   Parallel emission must use per-unit gensym namespaces (unit-prefixed) or a post-pass
   renumber in deterministic order. This is EXACTLY the "cosmetic gensym numbering" artifact the
   memory note already tracks at the C→self-host transition — parallelism would make it a
   correctness bug, not cosmetic.
3. **emission order** — parallel codegen buffers concatenated by canonical unit order, never by
   completion order.
4. **shared arena** — the arena runtime (`teko_rt.c`) is not thread-safe (bump allocator, global
   free-list, push-cache with `region_gen`). Parallelism needs per-thread arenas (Zig-style) or
   a lock — interacts with the whole S2 memory design. **This couples E to the memory work.**

**Blocker:** E depends on S8 threading (the language has no concurrency primitives yet — the
compiler cannot spawn what the language cannot express, and the corpus must not use a feature
not in its seed). So E is a LATE lever. **Do B+C+D first; E rides S8.** Target: B+C alone should
hit ≤15s local (33s − 15.6s gate + coverage-off ≈ 16s, then C shaves more). E is the path
below 15s.

### Incremental compilation — feasibility given the arena model.
Incremental (recompile only changed files, cache typed trees) is FEASIBLE in principle but
FIGHTS three current invariants: (1) the arena never frees, so a cached typed tree pins its whole
region — incremental caching wants per-unit droppable regions (the S2 design). (2) The `.tkb`
codec re-derives call_ns and drops generics on load (memory note) — a cache round-trip through
`.tkb` is a re-typing risk today. (3) Cross-file resolution (global type table) means a change in
one file can invalidate resolution in another — needs a dependency graph. **Verdict:** incremental
is a fase-5+ item, downstream of S2 regions (droppable per-unit arenas) and a fidelity-complete
`.tkb` codec (C7.16). Not a near-term lever; noted for completeness. For CI specifically,
incremental is LESS valuable than for local dev because CI always builds from a clean checkout.

---

## 5. Own backend + linker as a compile-time lever (owner hypothesis)

The owner's hypothesis: "we can only meaningfully improve times with our OWN backend and linker"
(teko → native machine code directly + an M-linker, replacing teko → C → host cc → host ld).
Evaluated rigorously PER PATH, reconciled with the #249 fact that host `cc` is 0.7s / 2.4%.

### Path 1 — Local self-build (28s/gen): own-backend helps ~2.4%. **NOT the lever.**
A native backend replaces the `cc` step. Measured: `cc` on the 102k-line emitted teko.c =
**0.69s = 2.4%** of a gen (#249, decisive). The 97% is INSIDE the teko process (VM gate 15.6s +
front-end churn 17s), which a native backend does NOT touch — you still lex/parse/check/mono the
same corpus, and you still run the gate. **Honest verdict to the owner: for local self-build,
own-backend is NOT the lever — #265 (native gate) and parallelism (E) are.** An own-backend at
`-O0` might even be SLOWER end-to-end than `cc -O0` until it is well-tuned, because `cc` is a
mature, fast `-O0` code generator; the win there is control/independence, not raw speed.

### Path 2 — CI cross-compile (254s on every PR): own-backend WINS BIG. ★ the genuine win.
Today: emit ONE teko.c, then **6 separate `zig cc` invocations** (one per Linux target,
`cross_compile_linux.sh:86-91`) = 114s + the 64s emit + zig provisioning. Each `zig cc` re-parses
and re-lowers the SAME 102k-line teko.c for a different target.

An own-backend with built-in target codegen emits ALL targets from ONE front-end pass — the
front-end (lex/parse/check/mono, ~17s, done ONCE) feeds N target emitters that share everything
up to instruction selection. Instead of 6× (full C-compile of 102k lines), it is 1× front-end +
6× (backend-only lowering, cheap). Plus an own M-linker removes the external zig/cc/ld dependency
entirely (no `Install zig` step, no glibc-pin gymnastics, no `-fno-sanitize=undefined` zig quirks).

**Plausible saving (estimate, flagged as estimate):** the 6× C-compile (114s) collapses toward
1× front-end (already amortized) + 6× native lowering. If native lowering is comparable to `cc`'s
codegen-only phase (~0.5s/target), the cross-compile step goes 114s → ~5-10s. Cross-smoke job:
254s → ~80s (dominated by the one emit + the run-guard). **This is the path where own-backend
genuinely wins — plus it deletes toolchain-provisioning from CI** (zig/cc/ld). Aligns with the
toolchain-independence memory (`teko-prealpha-release-and-toolchain-independence.md`: bundle-TCC
was the stopgap; own-backend is the real answer).

### Path 3 — qemu-riscv (452s): own-backend helps ~NOTHING. **Do not over-attribute.**
The 452s is RUNNING the gate under EMULATION, not compiling. An own-backend changes how the
riscv binary is PRODUCED (cross-emit instead of cross-cc), saving the ~48s emit + 7s cross-cc —
but the 452s of emulated EXECUTION is unchanged: the emulator runs whatever machine code you
give it at the same ~10× slowdown. **Honest verdict: own-backend does NOT fix qemu.** Only
QW-2 (smoke-only) or real riscv hardware (self-hosted runner) helps the qemu cost.

### Roadmap position — a LARGE, LONG, STRATEGIC play (fase-4), not a fast fix.
Own-backend+linker is fase-4: #222 (reference-target type/control coverage), #223 (arm64 /
riscv64 / windows targets), #225 (3-way differential gate + backend-selection flag), #226
(M-linker: ELF → Mach-O → PE/COFF). It aligns with the bare-metal/OS north-star
(`memory/teko-bare-metal-os-db-vision.md`: "native backend = capability not purity"). It is
delivered over a phase, gated by a 3-way differential (VM == C-backend == native-backend) so it
never regresses correctness.

**Honest verdict, front-and-center for the owner:** own-backend+linker helps **cross-compile
speed + toolchain independence A LOT** (Path 2, and the north-star), and **self-build + qemu
VERY LITTLE** (Paths 1, 3). It is the STRATEGIC lever, NOT the immediate 16m→5-6m fix. The
immediate fix is QW-1..QW-3 (CI-config) + #265 (native gate). Sequence own-backend AFTER those
land — it is a fase-4 capability, not a CI-speed patch.

---

## 6. Dependency DAG + ordering

```
NOW (CI-config, no compiler dep, days):
  QW-1b (un-double the gate)  ──┐
  QW-2  (riscv smoke) [HALT-2]  ├──►  16m → ~5-6m PR wall-clock
  QW-3  (cross PR→nightly)    ──┘
  QW-4  (fixpoint stays release-only — invariant guard)

KEYSTONE (compiler, weeks) — the shared-villain kill:
  #265 line/branch cov instrumentation  ──►  #168 compile-once split  ──►  native gate = DEFAULT
     │                                                                        │
     └── also fixes the MEMORY balloon (1595→366 MB, same villain) ◄──────────┘
     then QW-1a (drop VM step on all platforms; VM → nightly/WASM lane)

COMPILER SPEED (after keystone):
  Lever C (coverage zero-cost off)  — independent, do anytime, LOW risk
  Lever D (VM Env O(n)→hash)         — dev-lane only after native gate default
  Lever E (parallelism)              — BLOCKED on S8 threading; determinism-gated by fixpoint

STRATEGIC (fase-4, long):
  Own backend + linker (#222→#223→#225→#226)
     wins: cross-compile (Path 2) + toolchain independence + bare-metal north-star
     does NOT win: self-build (Path 1) or qemu (Path 3)
```

**What unblocks what:** QW-1..3 are independent and immediate. #265-cov unblocks native-gate-
as-default (QW-1a) which unblocks demoting the VM gate to nightly. #168 unblocks the sub-second
native gate (turns #265 from correctness-lane into speed-win). E is blocked on S8.

**Interaction with the memory work (same villain):** #265 is ALSO the memory keystone — closing
it drops the compiler's gate-time peak from 1595 MB to the 366 MB codegen floor (the child runs
in its own process). Do #265 once; both axes bank it. The remaining memory work (arena thrift
to get codegen-only 366→300 MB, R3b-style right-sizing on the new #177/#184/#185 modules) is
INDEPENDENT of compile-time and tracked in the memory note.

**Interaction with onda-3 (unrelated — do NOT block on it):** the monomorphization-cluster
keystone (`docs/design/onda3-monomorphization-cluster.md`) is a correctness/feature axis, not a
speed axis. It shares no code path with the gate or CI config. Sequence independently.

---

## 7. HALTs (owner rulings needed — genuine design/law forks)

**HALT-1 (QW-1 form).** Two ways to un-double the gate:
- 1b (recommended NOW): drop the *native* step from 4 platforms, keep VM everywhere + native
  on x86_64 as the correctness lane. No compiler dep, no gate weakening, immediate ~8m saving.
- 1a (after #265 line/branch cov): drop the *VM* step everywhere, native becomes sole gate,
  VM → nightly/WASM. Requires the cov instrumentation first.
Recommend 1b now, 1a after #265-cov. **Confirm the phasing?** (Not a blocker — 1b is
law-clean and I recommend proceeding; this HALT only flags that 1a needs #265-cov to not drop
line/branch floors.)

**HALT-2 (riscv smoke — the real law fork).** QW-2 weakens the riscv64-qemu gate from the FULL
`test . --coverage` to an arch-correctness SMOKE. native.yml:316 documents riscv as BLOCKING
under the main-integrity law ("green means green everywhere"). The full functional gate already
runs on 5 native platforms; riscv's unique value is arch-correctness (endianness/int128/
alignment), which a ~30-test smoke covers. **Weakening riscv to smoke saves ~7m but is a
deliberate scope reduction of a BLOCKING gate — acceptable to the owner?** Per the "never weaken
the gate to where a real regression slips" law, this is the one change I will NOT assume;
it needs an explicit ruling. If NO, riscv stays full and the wall-clock floor is ~9m instead of
~5-6m (still a huge win from QW-1 alone).

Everything else (QW-1b, QW-3, QW-4, Levers B/C/D, the own-backend verdict) is law-clean and I
recommend proceeding without a ruling.
