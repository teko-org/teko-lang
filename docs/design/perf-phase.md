# Perf phase — design record

Status: RECORD (no product code changed). Date: 2026-07-14.
Base: `origin/remodel/backend-build` (tip `b9006f9`).
Companion: `docs/design/perf-frontend-scoping.md` (the arena-scoping design + crumbs).

This is the perf-phase design record: what the symbolized diagnostic found, the
ranked fixes with DURABLE-vs-interim attribution, and the owner rulings that
frame the phase.

## 1. Diagnostic results (attribution)

A symbolized perf run over the self-host build attributed cost as:

* **Front-end + emit = 132 s / 91%** of wall time — the dominant cost. This is the
  lex -> parse -> reconcile -> check -> monomorph -> codegen pipeline plus C emit.
* **`cc` ~11 s — EXONERATED.** The host C compiler is not the bottleneck; do not
  chase it.
* **Native (own) backend HONEST-STOPS** at
  `native backend N1: 'text' is not a fat-pointer local` — it **cannot self-host
  yet**, so it cannot replace the C backend on the critical path. See §4 (blocker).
* **Named churn culprits:** unpre-sized list-builders (`teko::list::empty` +
  copy-on-`push` growth) and repeated type-name string construction (no interning)
  — high allocation/chunk churn.
* **Arena: 0.0% reclaim.** The front-end allocates everything into a single
  process-lifetime root region — **471 MB, 5007 chunks, no per-file/per-pass
  rewind** (`tk_alloc` -> `tk_region_root`, `src/runtime/teko_rt.c:1228`). Peak RSS
  ~**1.77 GB** (full gate) / **657 MB** (`--no-verify`).

## 2. The four ranked fixes

| # | Fix | Kind | Notes |
|---|---|---|---|
| 1 | **Arena scoping** (per-file / per-pass / cross-pass rewind) | **DURABLE** | The biggest durable structural win. Designed in `perf-frontend-scoping.md`. Safe boundary today = the throwaway gate pass (halves the verify-path peak); per-pass sub-regions deferred behind proofs. |
| 2 | **Pre-size list-builders** (grow lists with a known/estimated capacity instead of empty + copy-on-push) | **DURABLE** | Cuts realloc/chunk churn across every pass. Concurrent work (separate implementer). |
| 3 | **Intern type-names** (dedupe repeated type-name string construction) | **DURABLE** | Cuts both allocation and comparison cost. Concurrent work. **Coordination:** intern storage must live OUTSIDE the arena rewind span (see `perf-frontend-scoping.md` §6 TOP RISK). |
| 4 | **`-O2` interim** (optimize the bootstrap compiler build) | **INTERIM** | A stop-gap speed-up of the *compiler binary itself*, not a structural fix to the algorithm/allocator. Superseded by the optimization-axis work (owner ruling (b)/(c)). |

DURABLE = fixes the structure (allocator lifetime, churn, redundant work) and
survives the backend/optimization changes. INTERIM = a temporary lever that a
later, principled change replaces.

## 3. Owner rulings (2026-07-14)

* **(a) `owner 2026-07-14`** — the perf targets **self-host + tests < 3 min, with
  coverage < 5 min** apply to the **UNOPTIMIZED fast dev/CI path** (not to an
  optimized/PGO release build).
* **(b) `owner 2026-07-14`** — **optimization is an axis**, not a mode switch:
  * **unoptimized** = fast to *produce* (the dev/CI default);
  * **optimized** = release;
  * **PGO** = heaviest, release-only.
* **(c) `owner 2026-07-14`** — the **own linker (Phase E) is to be built WITH
  optimization-level flags from day one** (O0 / O1 / O2 + a later PGO stage), not
  bolted on after.
* **(d) `owner 2026-07-14`** — tests are to be **isolated/parallelized onto a
  thread**, not removed.
* **(e) `owner 2026-07-14` — PGO is the capstone optimizer; this phase only
  UNBLOCKS LATENCY.** The manual fixes here (`-O2` seed, `with_cap`, arena
  scoping) exist to make the compiler usable NOW. The genuine, durable
  optimization is **PGO**: a real profile of the *compiler's own execution* that,
  where the profiler shows a bottleneck, will **increase arena region size/depth**
  or **turn hot points `unsafe`** (skip checks that are hot AND provably safe) —
  data-driven, "almost like an SQL query analyzer suggesting and applying
  bottleneck fixes." Ties to ruling (c): the own backend/linker carries the PGO
  stage from day one.
* **(f) `owner 2026-07-14` — keep `emit`/`concat` AS-IS; do NOT stream the emit to
  disk and do NOT change `concat`.** Today's bottleneck is `malloc` (small-string
  churn); once the C backend dies it **migrates to arena-resize**, and PGO (ruling
  e) tunes that genuinely. Streaming the ~8 MB emit to a file would trim only the
  emit peak (~8-16 MB of a 0.5-1.8 GB peak) and is not worth the churn.
* **(g) `owner 2026-07-14` — the string interner is NOT wired.** The enabling PR
  ships the interner *intrinsic* staged-off (harmless, available), but the hot
  path (`cg_variant_typename_str`) is **NOT routed through it** — it is exactly the
  `malloc`/string churn PGO owns (ruling e/f), and a review found the current
  interner returns permanent libc-heap copies that could *raise* peak RSS vs the
  reclaimable arena it replaces. The manual apply-work is **`with_cap` + arena
  scoping ONLY**.

Consequence for this phase: the DURABLE manual fixes narrow to **`with_cap`
(pre-size hot list-builders) + arena scoping (Boundary A)**, which target the
unoptimized dev/CI path ruling (a) budgets. `-O2` (fix #4) is an interim CI
stopgap subsumed by the optimization axis of rulings (b)/(c) — and is itself
gated on the emitted-C UB **#283** (`zig -O2` miscompile) before the Linux/CI seed
can be `-O2`. The interner (fix #3) is dropped from manual wiring per ruling (g).
All heavier string/`malloc`/arena-resize tuning is deferred to PGO (ruling e).

### Bootstrap sequencing (seed-chain)
The manual fixes need compiler intrinsics the released seed lacks (`with_cap`,
`tk_arena_commit`), so they cannot be *used* in `src/**` until a seed that HAS
them is released. Order: **enabling PR** (intrinsics staged off, proven via
gen1-built fixtures) -> **seed refresh** -> **apply-PRs** (`with_cap` at the hot
sites; arena Boundary-A rewind). This is the standard per-merge seed chain.

## 4. Native honest-stop — blocker for the default -> native inversion

The own/native backend currently HONEST-STOPS at
`native backend N1: 'text' is not a fat-pointer local`. Until it self-hosts, the
native backend **cannot become the default** (the default -> native inversion is
BLOCKED on this stop). The C backend stays the critical-path backend for now; the
perf work above targets the front-end + C-emit path that every backend shares.

## 5. Regression fixtures (for the arena-scoping implementation)

VM and native exit codes for the fixtures the `arena_commit` / gate-rewind crumbs
add (see `perf-frontend-scoping.md` §5, Crumbs 1-2):

| Fixture | Input | Expected (native) | Expected (VM) |
|---|---|---|---|
| `arena_commit_keeps` | push -> build a list -> `teko::arena_commit()` -> read the list back | exit 0 (data survives the commit) | n/a — `arena_*` are host side-channel builtins the VM does not run (mirrors `Arena`'s native-only exercise); no `#test` |
| `arena_pop_frees` | push -> build a list -> `teko::arena_pop()` under `TEKO_ARENA_OBS` | exit 0 + obs dump shows the span reclaimed (`rewind_bytes > 0`) | n/a (native-only, as above) |
| full self-host gate (ritual) | the compiler's own build under the gate | exit 0, byte-identical FIXPOINT (`gen2.c == gen3.c`) preserved; `report_peak` verify-path peak DROPS vs baseline | — |

The gate-rewind crumb has **no dedicated positive fixture** beyond the full gate
itself (it is exercised on every verify build); its safety proof is the **FULL
gate under ASan** (`ci/heavy-sanitizers-on-main-only`), which must stay green —
that is the use-after-free proof for Boundary A.

## 6. Ritual points

* **R1** — after the `arena_commit` primitive lands (Crumb 1): full self-host gate
  green + the two `arena_*` fixtures. No caller yet -> zero behaviour change.
* **R2** — after the gate-pass rewind lands (Crumb 2): full self-host gate green,
  **under ASan**, FIXPOINT preserved, verify-path peak measurably reduced. This is
  the load-bearing ritual — it proves no use-after-free across the rewind AND that
  the intern-storage coordination (§2 #3 / `perf-frontend-scoping.md` §6) holds.
* **R3** (deferred) — per-pass sub-region scoping (Boundary B): one pass per crumb,
  each behind its own aliasing proof + ASan gate.

## 7. What remains blocked / deferred

* **Boundary B** (per-pass scratch sub-regions) and **Boundary C** (token/AST
  rewind) are deferred behind the concurrent list/string work and per-pass
  aliasing proofs — see `perf-frontend-scoping.md` §3.
* The **default -> native inversion** is blocked on the native honest-stop (§4).
* The **optimization axis** (rulings (b)/(c)) and **test thread isolation**
  (ruling (d)) are separate phases; this record only fixes their relationship to
  the four perf fixes.
* **Test-execution remodel — a SECOND bottleneck, deferred (owner 2026-07-14:
  "vamos ter que pensar … pois é outro gargalo", not now).** Beyond ruling (d)'s
  thread isolation, the *shape* of how the suite runs is itself a latency source:
  the self-hosted `teko test .` re-runs the whole corpus serially, and it was
  heavy enough to blow the Windows self-host cap — so that lane now drops test
  execution and only builds gen1 + a `--version` smoke (`sanitizers.yml`,
  `ci-gates.md`), with the authoritative suite on the Linux/macOS lanes. When we
  return to it: parallelize/isolate the run onto a thread (ruling (d)), stop
  carrying the test AST alongside the source load, and re-add the Windows test run
  once the compiler-speed work brings the build time down. Sequenced **after** the
  four perf fixes + the -O2 seed land; no design committed here yet.
* **Post-linker: per-OS ABI / platform optimization — a SEPARATE round, deferred
  (owner 2026-07-15).** Owner hypothesis: we are not yet exploiting the best ABI each
  OS can provide to maximize performance; grounded in the observed per-OS divergence
  in the test/build times (historically macOS fastest, then Ubuntu, then Windows —
  the gap has since shifted). Because teko owns its native backend (instruction
  selection, register allocation, calling conventions) **and** its own linker, it
  CONTROLS the ABI it emits — so post-Phase-E there is a real lever: per-target
  calling conventions (SysV / Win64 / AAPCS64), TLS model, syscall-vs-libc, allocator
  / page size, alignment & vectorization ABIs. **Caveat (integrator-pinned):** part of
  the *current* divergence is measurement artifact, not ABI — different CI runner
  hardware (macOS Apple-silicon vs slow Windows runners) and the fact that the
  *shipping* path is still the C backend via the host `cc` (each OS's `cc` makes its
  own ABI choices), NOT the own native path (which honest-stops today). So this
  optimization targets the OWN native path once it is complete and IS the shipping
  path — genuinely **post-linker (Phase E+)**, and it feeds / is fed by PGO (the ABI
  shape is an input to the profiler). No design committed here yet.
