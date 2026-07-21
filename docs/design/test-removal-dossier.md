# Test-removal dossier — teko-lang (v0.3.0.28-beta)

**Status:** DOSSIER (design-only, read-only). No branch, no PR, **nothing removed** — this is a
reference catalogue for a FUTURE decision. Companion to
`docs/design/test-suite-consolidation.md` (the audit); this file is the tiered, per-candidate
catalogue the owner asked for.

**Owner maturity lens (2026-07-20):** *coverage does not prove quality in immutable/concrete
code; it matters while you BUILD the feature, not after.* Applied here: a test's value shifts
over its life. During construction it is a **coverage instrument** (does the new branch work?).
After the code freezes (a settled ISA encoder, a shipped stdlib fn) that same test becomes a
**regression guard** — and a regression guard is *removable-with-safety* only if some OTHER live
check would catch the same regression. The bar stays **SAFE**: a test leaves only when its
branch is provably held elsewhere, or its guarded code is provably gone.

**The honesty constraint that shapes every tier:** there is **no per-test coverage
attribution** in the repo today. `bin/teko-tktest.tkcov` is an aggregate binary index (magic
`TKCOV1`) with no test→line/branch map. So subsumption is **argued**, never **proven**, except
where the guarded code is deleted outright. That single fact is what separates T1 (proven) from
T2 (safe-only-if-confirmed).

---

## 0. Tier summary (the numbers up front)

| Tier | Meaning | Unit tests | Fixtures | Lines | Removable status |
| --- | --- | --- | --- | --- | --- |
| **T1** | Safe NOW, proven | ~5 | ~4 | ~250–400 | **Actionable** (one owner-un-pin gate on the fixtures) |
| **T2** | Safe ONLY if confirmed by per-test attribution | 0 realized (pool ≈ 110–190 golden vectors flagged for investigation) | 0 | 0 today | **Blocked on the lever** (§4) |
| **T3** | KEEP — sole-guards, law/invariant, oracles, owner-pinned | untouched | untouched | — | **Off-limits** |

**Bottom line:**
- **Only-T1 scenario (do today):** remove ~5 unit tests + ~4 fixtures + ~250–400 lines →
  **~0.2 % of tests, ~2 % of fixtures.** Small, clean, safe.
- **T1 + T2-with-attribution scenario (after the lever ships):** the T2 investigation pool is
  ~110–190 backend golden vectors; the *safely-thinnable* subset inside it is **unknown until
  measured** (honest: could be near-zero, could be ~30–60). The value of the lever is turning
  that "unknown" into a machine-checked number — and giving D4 a permanent dead-test tooth.

The suite is **mature and lean** (0.48 test:source line ratio, 3.4 asserts/test). This dossier
does not find a large safe cut; it finds one clean delete, a fixture consolidation, and a
well-bounded *future* opportunity gated on tooling.

---

## 1. TIER 1 — Safe NOW (proven)

Each entry: file · what it covers · who else covers it (retention) · verdict + reason.

### T1.1 — `src/reprobug/` (whole module) — OBSOLETE + SUBSUMED — **DELETE**

| Field | Value |
| --- | --- |
| Files | `src/reprobug/reprobug_test.tkt` (5 `#test`), `src/reprobug/reprobug.tks`, `src/reprobug/inner/inner.tks` |
| Size | ~155 lines total (test + 2 scaffold `.tks`) |
| `#test` names | `own_probe_resolves_within_its_own_namespace`, `inner_probe_resolves_within_its_own_namespace`, `build_items_no_early_return_has_three`, `build_items_early_return_has_two`, `holder_field_not_corrupted_by_later_pushes` |

- **What it covered:** a confirmed **VM** state-corruption bug — `find_function` in
  `src/vm/vm.c` **AND** `src/vm/vm.tks` matched a call target by the callee's bare last path
  segment globally, cross-resolving same-named functions across namespaces and corrupting VM
  state. The module is a minimal repro + guard for that VM defect.
- **Why obsolete (proven):** the VM was retired **100 %** (issue #524,
  `docs/design/vm-retirement.md`; owner-ratified 2026-07-12 *"tudo que precisar pra remover
  100 % a VM"*). `src/vm/` and both twins are DELETED. **The guarded code no longer exists** —
  the strongest possible obsolescence proof.
- **Retention of the surviving invariant (proven):** the only *language-level* behaviour the
  tests assert — same-named fns in different namespaces resolve within their own namespace and
  do NOT cross-resolve — is independently held by (a) namespace-resolution tests in
  `src/checker/checker_test.tkt`, and (b) the end-to-end fixture
  `examples/regressions/di_same_name_cross_ns/`. The `build_items_*` / `holder_field_*` tests
  assert early-return and list-mutation behaviour that the checker + `lir/lower_test.tkt` +
  `list` suites already cover. Deleting `reprobug` removes **zero unique coverage.**
- **Verdict: T1 — DELETE the module.** Removes a whole namespace whose sole reason to exist
  retired with the VM. Shrinks the corpus (faster fixpoint) and the W15/D39 sweep surface.
  Ritual: re-baseline the self-host fixpoint in the same PR (corpus change).

### T1.2 — `discard_*` R-value fixture cluster — SUBSUMED (same branch) — **CONSOLIDATE 4 → 1**

| Field | Value |
| --- | --- |
| Fixtures | `discard_arith_read` (`_ + 1`), `discard_bare_read` (`y = _`), `discard_call_arg` (`f("a", _)`), `discard_return_read` (`return _`) |
| Shared branch | `parse_atom` rejecting `_` as an R-value ("`_` is a write-only discard and cannot be read") |

- **What they cover:** four `EXPECT_COMPILE_FAIL` fixtures hitting the **identical** parser
  branch; only the surrounding syntactic context differs.
- **Retention (proven):** `src/parser/parser_test.tkt` carries **13 discard unit tests** that
  already exercise `_`-as-R-value across contexts. The fixtures add only the e2e "the whole
  build fails" assurance — which ONE representative (`discard_bare_read`, the minimal `y = _`)
  fully provides. Keeping it preserves the e2e wiring proof; the other three add no branch.
- **Sibling fixtures that STAY (distinct branches, NOT part of this cluster):**
  `discard_double_underscore` (`read_underscore`, **lexer**), `discard_as_alias` /
  `discard_let_binding` / `discard_typed_target` (`parse_bind_target`, distinct paths),
  `discard_param_plain_fn` (checker semantic), `underscore_as_x_rejected` (`_ as x`).
- **Verdict: T1 for coverage — CONSOLIDATE 4 → 1, net −3 fixtures. GATE:** each was created by
  a **dated owner ruling (2026-07-15)** as an explicit pin. Coverage retention is proven, but
  merging the owner's own pins is the owner's call — present, do not auto-delete (tension T1 in
  the audit §5).

### T1.3 — `own_arith_exit` / `own_sub_exit` differential overlap — **CONSOLIDATE 2 → 1 (−1 fixture)**

- **What they cover:** two entries in the `diff_c_own.sh` 15-program corpus, differing only by
  ALU opcode (add vs sub) — a difference already pinned at unit level by
  `src/backend/isel_x86_64_test.tkt` / `encode_x86_64_test.tkt` (`ex_encodes_every_two_address_alu_op`).
- **Retention:** the executing own==C differential still proves the arith path end-to-end with
  one representative; the opcode variety is unit-covered. `own_exit_zero` (zero exit) vs
  `own_exit_code` (non-zero) is a REAL branch — **both stay.**
- **Verdict: T1 — consolidate the arith/sub overlap only, −1 fixture.**

### T1.4 — Duplicates — **NONE FOUND**

Investigated and cleared: `src/list/list_test.tkt` (3) vs `src/collections/list_test.tkt` (7)
are NOT duplicates — `teko::list` is the built-in value-thread array; `teko::collections::List<T>`
is the reference-semantic class (issue #163). Different modules, different code. **No removal.**

**T1 total: ~5 unit tests (reprobug) + ~4 fixtures (3 discard + 1 own) + ~250–400 lines.**

---

## 2. TIER 2 — Safe ONLY if confirmed by per-test attribution

This is the **immutable/concrete surface** the owner's lens points at: the backend golden-vector
suite over a **frozen ISA**. Its construction-time coverage job is done; its remaining job is
regression-guarding. The question per vector is therefore: *would some OTHER live check catch a
regression in this opcode's encoding?* Two live checks exist:

1. **Fixpoint** (`gen2 == gen1`, byte-identity): any change to an opcode the compiler emits
   *while compiling itself* changes the emitted bytes → fixpoint breaks. Strong, but it proves
   **stability**, not **correctness** (a wrong-but-stable encoding survives), and it does NOT
   localize WHICH opcode broke.
2. **Executing differentials** — the ONLY correctness-by-execution checks:
   - `scripts/diff_c_own.sh` — own-native == C-native over **15** `own_*` programs
     (exit(n)/control-flow/arith/match/loop/defer scoped).
   - `scripts/validate_wasm_own.sh` — own-wasm (wasmtime) == C-native over **9** `wasm_*`
     programs.

### 2.1 The T2-eligible surface (backend golden vectors)

| Family | Files | Tests | Role | T2-eligible? |
| --- | --- | --- | --- | --- |
| encode | `encode_{arm64,riscv,x86_64}_test.tkt` | 186 | hex golden per opcode | yes (surface) |
| isel | `isel_{arm64,riscv,x86_64}_test.tkt` | 157 | instruction selection | yes (surface) |
| regalloc | `regalloc_test.tkt` + `_x86`/`_riscv`/`_match` | 108 | spill/allocation | yes (surface) |
| stackify | `stackify_test.tkt` | 168 | wasm stackification/valtype/mem-op | yes (surface) |
| objfile | `objfile_{coff,elf,elf_riscv,macho,wasm}_test.tkt` | 71 | object-file bytes per format | yes (surface) |
| abi | `abi_{sysv64,win64,riscv64}_test.tkt` | 32 | ABI lowering | yes (surface) |
| minst (structural) | `minst_test.tkt` + `_x86`/`_riscv` | 32 | MInst shape/builders | yes (surface) |
| **T2-eligible subtotal** | | **754** | | |
| minst_interp (ORACLE) | `minst_interp_test.tkt` | 35 | differential oracle | **NO → T3** |

### 2.2 The honest split inside the 754

For each family, a vector is a **T2 investigation candidate** only if its opcode is BOTH (a) on
the self-compile path (fixpoint-guarded) OR in the 15+9 executing-diff corpus, AND (b) not the
sole localizer of that opcode. Everything else is a **sole-guard → T3**.

- **Sole-guards (the majority) → T3, KEEP.** The executing-diff corpus (15+9 programs) touches a
  NARROW opcode subset: integer add/sub/mul, compare, conditional branch, exit, a little
  print/defer/match/loop. The encoders pin FAR more — division sequences, every shift-by-CL,
  `setcc` for every condition code, `movabs`/sign-extended immediates, FPR/XMM ops, every
  memory addressing form (disp8/disp32/SIB/RIP), REX-extended high registers, `neg`/`not`,
  per-format relocation kinds. **No executing differential exercises these**, and fixpoint only
  guards the ones the self-compiler happens to emit — and even then only as a non-localizing
  byte check. These vectors are the **primary correctness oracle** for the frozen ISA and stay.
  Rough family estimate: **~75–85 %** of the 754 are sole-guards.
- **T2 candidates (the minority): ~110–190 vectors.** These are the arith/compare/branch/exit
  encodings that the 15+9 executing programs DO run end-to-end, where the golden vector is a
  belt-and-suspenders second guard. They are *plausibly* thinnable — but with two caveats that
  forbid deletion today:
  1. **No proof.** Without per-test attribution we cannot show a given vector's lines/branches
     are actually re-hit by an executing program. The mapping is argued from opcode names, not
     measured.
  2. **Residual diagnostic value.** Even a differential-covered vector localizes a break to one
     instruction; the differential only says "program N exits wrong." Retiring it trades
     bisect-locality for count.

### 2.3 T2 verdict

**T2 realized today = 0.** Nothing here is removable-with-safety under the current toolchain: the
subsumption is argued, not proven, and the guarded code is very much alive (the ISA is frozen,
not deleted). T2 is a **catalogued investigation surface** of ~110–190 vectors that becomes
actionable the moment per-test attribution exists (§4). Honest label: **"safe-if-confirmed,"
not "safe-proven."**

---

## 3. TIER 3 — KEEP (off-limits)

| Group | Count | Why it stays |
| --- | --- | --- |
| Backend sole-guards | ~570–640 of the 754 golden vectors | ONLY oracle for a rare/unique opcode the differentials never run; frozen-ISA correctness |
| Oracles | `lir_interp_test.tkt` (36) + `minst_interp_test.tkt` (35) | differential-agreement engines (issue #221), not the retired VM |
| Law/invariant | checker 280 + spine 73 + borrow 44 + comptime_fold 48 + consteval 22 + const 16 + generics 20 + closures 12 (≈495) | encode the type/borrow/safety-spine/const CONTRACTS — the Constitution/Laws surface |
| Fixture unit-backing | parser 126 (incl. 20 `rejects_malformed_*`, 13 discard) + lexer 19 | the reason any fixture can be thinned; delete these and the fixtures lose their branch |
| Owner-pinned negatives | the 35 `EXPECT_COMPILE_FAIL` minus the T1.2 cluster | each fixed by a dated owner ruling; not proven same-branch-redundant |
| Stdlib/tooling | fmt 42, json 59, math 71, io/stream 25, regex 26, build ~85, etc. | each pins a distinct stdlib/tooling branch; already grouped 3.4 asserts/test; merging hurts legibility (the owner's own goal) |

---

## 4. THE LEVER — `teko test --per-test-cov` (approved as dev-time; full spec §7)

**What it is:** extend the test runner to attribute coverage **per test** — emit, alongside the
aggregate `.tkcov`, a test→(lines, branches) matrix. Equivalently, run each `#test` with a
fresh coverage counter and record the delta.

**What it converts:**
- **T2 from argued → proven.** With the matrix, "vector V's lines ⊆ the union of executing-diff
  programs' lines" becomes a **machine-checked query**, not a name-based guess. Every one of the
  ~110–190 T2 candidates gets a definitive yes/no. The *safely-removable* subset stops being
  "unknown" and becomes a measured list.
- **D4 gains a permanent tooth.** Today the coverage floors (functions=50/lines=50/branches=49)
  are aggregate gates. Per-test attribution lets the gate also flag **dead tests** (a `#test`
  whose covered lines are a strict subset of another's) and **zero-marginal tests**
  continuously — so the suite cannot silently re-accrete redundancy after this cleanup. The
  audit's whole "can't prove subsumption" limitation dissolves.

**Estimated gain (honest, bounded):**
- Enables a *measured* T2 harvest. If attribution confirms even **30 %** of the ~110–190 pool is
  genuinely subsumed by an executing differential, that is **~35–60 golden vectors** safely
  retirable — with a machine-checked safety net, not an argument.
- Ongoing: automatic dead-test detection on every `teko test` → the suite stays lean by
  construction, shrinking future W15/D39 sweep surface permanently.
- Secondary: enables *targeted* differential expansion — the matrix shows exactly which opcodes
  have NO executing coverage (the true sole-guards), guiding where a new `diff_c_own` program
  adds the most safety.

**Cost/placement:** a runner feature (`src/coverage/` + the test driver), gated behind a flag so
the default fast path is unchanged. **Reported up as the enabling investment; this dossier does
not open an issue for it.**

---

## 5. Scenarios (removable-with-safety, quantified)

| Scenario | Unit tests | Fixtures | Lines | % tests | % fixtures | Precondition |
| --- | --- | --- | --- | --- | --- | --- |
| **Only-T1 (today)** | ~5 | ~4 | ~250–400 | ~0.2 % | ~2 % | owner un-pins T1.2 |
| **T1 + measured T2** | ~5 + ~35–60 vectors | ~4 | ~1,200–2,000 | ~2 %–3 % | ~2 % | `--per-test-cov` ships + confirms |
| **Aggressive (rejected)** | — | — | — | — | — | would sacrifice frozen-ISA correctness oracle → violates SAFE bar |

**Verdict on the owner's maturity lens:** correct in principle — the frozen backend's coverage
tests HAVE aged from instruments into guards, and *some* guards are redundant. But "redundant"
is only **safe once measured**. Today the proven-safe removal is small (T1); the larger, still
conservative harvest (T2) is real but **gated on one tool** (`--per-test-cov`), which is the
highest-leverage next move because it also gives D4 a permanent anti-bloat tooth.

---

## 6. Floor recalculation — `teko.tkp [coverage]` (owner refinement 2026-07-20)

**The mechanic (why removing "absolute-code" tests moves the number):** `teko test` measures
line/branch/function coverage **during the test run**. The frozen backend's correctness is proven
by *other* gates — the self-host **fixpoint** (gen2==gen1 byte-identity), the **own==C
differential** (`diff_c_own.sh`, 15 programs, executed), the **own-wasm differential**
(`validate_wasm_own.sh`, 9 programs, wasmtime), and the **host-tool object checks**
(`check_elf`/`check_macho`/`check_coff`) — but those harnesses are **NOT** part of the
`teko test` coverage run. So when a golden vector is retired, its backend **source lines stay in
the denominator** (the code is frozen, not deleted) while the **numerator drops** → measured
coverage % falls, even though the lines are still proven at runtime by the differential/fixpoint.

The owner's framing is exactly right and must be stated in the manifest: **this is not loosening
the gate — it is ceasing to count unit-coverage over ABSOLUTE code that a superior gate already
proves.** The floors are re-scoped to measure the **live/mutable** surface, where coverage still
means "did we test the new branch?".

### 6.1 Backend frozen surface (the code that goes to differential-only proof)

| Frozen family (source) | ~Lines | Superior gate that proves it (in lieu of unit coverage) |
| --- | --- | --- |
| `encode_{arm64,riscv,x86_64}.tks` (+`_consts`) | ~6,700 | fixpoint (self-emitted opcode bytes) + own==C differential (executes) + host object check |
| `isel_{arm64,riscv,x86_64}.tks` | ~4,300 | fixpoint + own==C differential |
| `regalloc*.tks` | ~3,800 | fixpoint + own==C differential |
| `stackify.tks` | ~5,480 | wasm-validate (WABT) + wasmtime differential |
| `objfile_*.tks` | ~3,650 | host toolchain (`readelf`/`otool`/`llvm-readobj`/`ld -r`) + differential |
| `abi_*.tks` (in isel/minst) + `minst*.tks` | ~7,900 | own==C differential (calling convention proven by executed calls) + LIR/MInst interp oracle |
| **Backend frozen subtotal** | **31,850 (34 % of the 93,738-line source)** | |

### 6.2 Per-scenario floor impact and recommendation

> **Inputs the team must read before finalizing:** the ACTUAL current measured coverage from
> `./bin/teko test .` (functions/lines/branches %). The floors below are expressed as a **model**
> keyed to that measured value `M` and the removal's coverage delta `Δ`; plug in the real `M`.
> The current floors (50/50/49) are set with margin *below* today's `M` (the gate passes), so the
> question is always "does `M − Δ` still clear the floor?", not "does the floor equal `M`?".

**Scenario T1 (do today): floors UNCHANGED — 50 / 50 / 49.**
- `reprobug` removes test **and** its source scaffold together (~155 lines both sides of the
  ratio) → net coverage impact ≈ **0**.
- The fixture consolidations (T1.2/T1.3) run in the **regression harnesses**, not the
  `teko test` coverage run → **zero** effect on the measured %.
- **Proven: T1 needs no floor change.** (This is a clean, safe finding — act on T1 without
  touching `[coverage]`.)

**Scenario T1 + measured-T2 (after the lever confirms ~35–60 removable vectors): floors LIKELY
NEED A SMALL DROP — model below.**
- Estimated coverage delta from retiring ~35–60 backend vectors (each uniquely covering
  ~15–40 encoder/isel source lines that no *remaining unit test* re-hits, only the differential):
  - **Lines:** ~800–1,500 backend lines shift to differential-only → **Δ_lines ≈ 0.9–1.6 pts**
    absolute on the whole-source denominator.
  - **Branches:** encoder dispatch is branch-dense → **Δ_branches ≈ 1.5–2.5 pts**.
  - **Functions:** most encoder fns are hit by many vectors, so few fall out →
    **Δ_functions ≈ 0.5–1.0 pt**.
- **Two ways to keep the gate honest — choose one:**

  **Mechanism A — lower the global floors (the owner's stated ask).**
  Set each floor to `floor(M − Δ − margin)` with a small `margin` of ~2 pts. If, and only if,
  the current margin `(M − floor)` is thinner than `Δ + 2`, the floors must drop by `Δ`. Concrete
  recommendation, to be confirmed against measured `M`:
  - functions: **50 → 49** (drop 1)
  - lines: **50 → 48** (drop 2)
  - branches: **49 → 46** (drop 3)

  These are the *maximum* drops the modelled Δ justifies; if the measured `M` shows the current
  50/49 already clear `M − Δ` with margin (very possible — a mature self-hosted compiler usually
  runs its coverage well above a 50 floor), then **no drop is needed even for T2**, and the floors
  stay. Do NOT drop floors speculatively — drop only what the post-removal measurement requires.

  **Mechanism B — exclude the frozen backend from the coverage denominator (RECOMMENDED, more
  honestly matches "measure live code").**
  Add a `[coverage] exclude` list (or reuse the existing self-exclusion guard mechanism the
  coverage subsystem already applies to itself) naming the frozen backend source
  (`src/backend/{encode,isel,regalloc,stackify,objfile,abi,minst}*.tks`). Then the numerator AND
  denominator both drop by the frozen surface → the measured % now reflects **only the live code**
  and is ~**unchanged by any backend-vector removal** (the excluded lines never counted). With B,
  the floors can stay HIGH — even **rise** — because they now measure the surface where coverage
  is a real quality signal. Recommended live-only starting floors, to finalize against the
  re-measured live-only `M_live`: **functions=60, lines=60, branches=55** (higher than today
  precisely because the hard-to-unit-cover frozen ISA is no longer diluting the number).

- **Recommendation: Mechanism B.** It is literally what "floors measure live/mutable code, not
  frozen" means; it makes any future backend-vector retirement floor-neutral (no re-tuning
  churn); and it turns the coverage number back into a meaningful signal instead of a diluted
  average. Mechanism A is the acceptable fallback if an exclusion list is judged too heavy — but
  A re-introduces the exact dilution the owner is trying to remove, and needs re-tuning on every
  future harvest. Under B, the code left deliberately WITHOUT unit coverage is the entire §6.1
  frozen surface — safe because each family's superior gate (fixpoint / executed differential /
  host object check) proves it at runtime, which unit coverage never did.

---

## 7. Spec — `teko test --per-test-cov` (dev-time tool, NOT CI) — crumb for 0.3.0.29

**Owner ruling 2026-07-20:** APPROVED to build, as a **development-time** utility. Explicitly
**out of the CI gate** (in CI the aggregate `TKCOV1` floor gate suffices — concurred: per-test
attribution is a poda/authoring aid, not a release gate).

### 7.1 What it emits

- Today: `teko test` writes `bin/teko-tktest.tkcov` — an **aggregate** binary index (magic
  `TKCOV1`) recording which lines/branches the WHOLE suite hit. No test→line map.
- With `--per-test-cov`: additionally emit a **per-test attribution matrix** — for each `#test`,
  the set of (function, line, branch-outcome) it covers **in isolation**. Serialized as a
  sibling artifact (e.g. `teko-tktest.pertest.tkcov` + an optional human `--cov-report=text|json`
  dump). Default `teko test` is byte-for-byte unchanged; the matrix is produced only under the
  flag.

### 7.2 How it guides the poda (T2 argued → T2 proven)

- **Subsumption query:** a test `A` is safely removable when `covered(A) ⊆ ⋃ covered(others)`
  — i.e. every line/branch `A` touches is also touched by some retained test. With the matrix
  this is a **machine-checked set operation**, not a name-based argument. The entire §2 T2 pool
  (~110–190 vectors) gets a definitive per-vector yes/no.
- **Differential-aware variant:** cross the matrix with the executing-differential corpus to
  flag vectors whose unique lines are ALSO run by `diff_c_own`/`validate_wasm_own` at runtime —
  these become removable under Mechanism B (excluded surface) with a proof, not a guess.
- **D4 permanent tooth:** run under the flag in a periodic dev check to flag any NEW test whose
  covered set is a strict subset of an existing one → the suite cannot silently re-accrete
  redundancy after the cleanup. (Dev-time only; the CI gate stays aggregate.)

### 7.3 Scope, design, and size

- **Flag:** `teko test --per-test-cov [--cov-report=<fmt>]`. Dev path only; **no CI wiring**, no
  change to the release gate, zero CI cost.
- **Reuses existing machinery:** the coverage subsystem (`src/coverage/coverage.tks`, 578 lines —
  `CovWalk`/`BranchSite`/`CovCount`, `functions/line/branch_coverage`, the `teko::cov_distinct()`
  runtime hit-set) is already engine-independent (relocated out of the VM per #524). Per-test
  attribution = **snapshot the runtime hit-set before each `#test`, run it, snapshot after, diff**
  → that test's isolated covered set; accumulate into the matrix.
- **New work (estimate):**
  1. runner: per-test snapshot/restore around each `#test` invocation (the driver already
     iterates them for pass/fail) — small.
  2. a matrix accumulator + serializer (mirror the `TKCOV1` writer shape) — moderate.
  3. the `--per-test-cov`/`--cov-report` flags + a subsumption-query reporter — small.
  - **Rough size: ~300–600 new source lines** across `src/coverage/` + the test driver + arg
    parsing. **One crumb, independently gate-able, 100 % coverage on the new delta**, behind the
    flag so the default remains untouched.
- **Estimated payoff:** converts the ~110–190-vector T2 pool from "argued" to a measured
  removable list (if ~30 % confirm subsumed → **~35–60 vectors** safely retirable *with proof*),
  plus ongoing dead-test detection that keeps the suite lean and shrinks the W15/D39 sweep
  surface permanently.
