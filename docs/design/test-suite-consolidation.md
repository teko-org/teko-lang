# Test-suite consolidation audit — teko-lang (v0.3.0.28-beta)

**Status:** AUDIT (design-only, read-only). No branch, no PR, no code change. Owner ruling
2026-07-20: *"avaliar testes que não precisam mais existir, dada a versão que temos e a
quantidade; supomos que dá pra reduzir e ainda manter boa cobertura. O mesmo para
regressivos."*

**Scope:** identify unit tests (`#test` in `*_test.tkt`) and regression fixtures
(`examples/regressions/`) that no longer need to exist, **reducing quantity while keeping
coverage**. This is a CONSERVATIVE audit: the deliverable value is a faster build, a legible
suite, and a smaller surface for the W15 sweep (B)/D39 — **not** cutting coverage. On any
doubt: KEEP.

---

## 0. Executive summary (read this first)

The suite is **mature and well-factored**, not bloated. The evidence:

| Metric | Value | Reading |
| --- | --- | --- |
| Unit tests (`#test`) | 2,133 in 71 `*_test.tkt` files | — |
| Test lines | 45,212 | — |
| Non-test source lines | 93,738 | **test:source = 0.48 : 1** (healthy, not padded) |
| Assert calls | 7,318 | **3.4 asserts/test** (grouped-by-branch, not one-assert-per-test bloat) |
| Regression fixtures | 204 dirs (24 `EXPECT_EXIT`, 35 `EXPECT_COMPILE_FAIL`, 145 positive/differential) | — |

**Verdict on the owner's supposition:** *partially confirmed, and tightly bounded.* Yes, there
is a coverage-preserving reduction available — but it is **low single digits**, not a large
cut. The 3.4 asserts/test ratio and the golden-vector nature of the backend suite (each `#test`
pins distinct encoder/isel/regalloc branches) mean the apparent bigness of "2,133 tests" is
real coverage, not redundancy. The honest reduction that provably keeps coverage is:

- **Safe deletes (subsumed + obsolete):** the entire `src/reprobug/` module — **5 tests + 2
  scaffold `.tks`, ~155 lines** — a guard for a bug in the now-**deleted** VM (`src/vm/vm.c`),
  whose surviving invariant is already covered elsewhere (§4.2).
- **Fixture consolidation (redundant same-branch negatives):** the `discard_*` R-value cluster,
  **4 fixtures → 1**, plus 1–2 `own_exit*` overlaps (§4.5). Net ~**4–5 fixtures**.
- **Consolidate-not-delete opportunities:** a handful of narrow unit clusters (§4.4), which
  reduce **count** but not coverage and only marginally reduce lines.

**Bounded estimate (conservative, coverage-neutral):**

| Target | Reduction | % of category |
| --- | --- | --- |
| Unit tests | ~5–12 tests (reprobug + 2–3 micro-consolidations) | **0.2 %–0.6 %** |
| Test lines | ~250–600 lines | **0.6 %–1.3 %** |
| Regression fixtures | ~4–5 fixtures | **~2 %–2.5 %** |

**The one thing that would unlock a *bigger* safe cut** is not in this repo today: there is **no
per-test coverage attribution**. `bin/teko-tktest.tkcov` is an aggregate binary index (magic
`TKCOV1`) with no test→line/branch map, so subsumption cannot be proven mechanically — only
argued structurally. A `teko test --per-test-cov` (test→line matrix) would let the team retire
provably-subsumed tests in bulk with a machine-checked safety net. **Reported up as an enabling
investment; this audit does not turn it into an issue.**

---

## 1. Method and its honest limits

1. **Map** every `*_test.tkt` by module: test count, line count, asserts/test, and what each
   name-cluster covers.
2. **Cross-reference** with `bin/teko-tktest.tkcov` — which turned out to be an **aggregate**
   coverage index (no per-test attribution). Consequence: no "test X's lines ⊆ test Y's lines"
   can be *proven*; every subsumption claim below is a **structural** argument (same function
   under test, same branch, asserts preserved by the survivor) and defaults to **consolidate
   (asserts preserved)** rather than blind delete.
3. **Group fixtures** by exercised construct (directory-name prefix + `EXPECT_*` marker + the
   harness that owns them) to find same-branch redundancy.
4. **Conservative gate:** a test is a poda candidate ONLY with (a) a named survivor that retains
   the branch, or (b) proof the guarded code is deleted AND the surviving invariant is covered
   elsewhere. Otherwise it STAYS.

**What the `.tkcov` limit means for you:** the numbers in §0 are floors of *certainty*, not
ceilings of *opportunity*. There may be more subsumed tests; we cannot prove it safely today,
so we do not claim it.

---

## 2. Suite map (by module)

### 2.1 Backend (`src/backend/`) — 23 files, ~945 tests, ~22.9k lines — golden-vector core

| File | Tests | Lines | What it pins |
| --- | --- | --- | --- |
| `stackify_test.tkt` | 168 | 3,628 | wasm stackification / valtype / mem-op lowering |
| `encode_arm64_test.tkt` | 70 | 1,940 | AArch64 instruction encodings (hex golden) |
| `encode_x86_64_test.tkt` | 65 | 1,519 | x86-64 encodings (hex golden) |
| `isel_x86_64_test.tkt` | 62 | 1,005 | x86-64 instruction selection |
| `isel_riscv_test.tkt` | 57 | 903 | RISC-V isel |
| `encode_riscv_test.tkt` | 51 | 1,385 | RISC-V encodings |
| `regalloc_test.tkt` / `_x86` / `_riscv` / `_match` | 42/33/30/3 | — | register allocation |
| `isel_arm64_test.tkt` | 38 | 753 | AArch64 isel |
| `minst_interp_test.tkt` / `minst_*` | 35/14/10/8 | — | MInst interpreter **oracle** (active, not the retired VM) |
| `objfile_*` (elf/macho/coff/wasm/elf_riscv) | 71 total | — | object-file emission per format |
| `abi_*` (sysv64/win64/riscv64) | 32 total | — | ABI lowering |

**Poda posture:** **KEEP essentially all.** Each `#test` here is a distinct golden vector (a
unique byte string / a distinct isel pattern / a distinct spill scenario). They are already
grouped 3–6 asserts per test by opcode family (e.g. `ex_encodes_every_condition_code`
enumerates all `setcc` codes in ONE test). Consolidating further would MERGE distinct branches
and *lose* the per-branch failure locality that makes an encoder bug bisectable. These are the
**single coverers** of the own-native backend — off-limits.

### 2.2 Checker (`src/checker/`) — 8 files, 495 tests, ~11.2k lines — invariant/law core

| File | Tests | Lines |
| --- | --- | --- |
| `checker_test.tkt` | 280 | 5,157 |
| `spine_test.tkt` | 73 | 1,561 |
| `comptime_fold_test.tkt` | 48 | 1,239 |
| `borrow_test.tkt` | 44 | 1,076 |
| `consteval_test.tkt` | 22 | 630 |
| `generics_test.tkt` / `closures_test.tkt` / `checker_const_test.tkt` | 20/12/16 | — |

**Poda posture:** **KEEP.** These encode the type-system, borrow, safety-spine, and const/comptime
**invariants** — the Constitution/Laws surface. Clusters like the 17 `must_free_*` and 9
`mem_ref_*` tests each pin a distinct rejection/acceptance branch of the memory-safety spine.
Off-limits per the "law/invariant tests stay" rule (§6).

### 2.3 Front-end + LIR — parser 126, lexer 19, lower 117, lir 23, lir_interp 36, codegen 42

**Poda posture:** **KEEP.** `parser_test.tkt`'s 20 `rejects_malformed_*` and 13 discard tests are
the unit-level coverage that *backs* the negative fixtures (§4.5) — they are the reason the
fixtures can be thinned. `lir_interp_test.tkt` is the **LIR interpreter oracle** (issue #221,
the lowering-independent side of the agreement) — active, not the retired VM.

### 2.4 Stdlib / encoding / tooling — the long tail

`fmt` 42, `math` 31+40, `json` 59, `csv` 18, `url` 18, `base64` 13, `regex` 26, `iter` 17,
`io/stream` 25, `crypto` 16, `compress` 13, `collections` 15, `build/*` ~85 total, plus small
modules (`text` 3, `time` 4, `list` 3, `assert` 4, `mem/unsafe/rawbuf` 3).

**Poda posture:** **KEEP**, one investigated **false positive** resolved: `src/list/list_test.tkt`
(3 tests) and `src/collections/list_test.tkt` (7 tests) are NOT duplicates — `teko::list` is the
built-in value-thread array module; `teko::collections::List<T>` is the reference-semantic class
(issue #163). Different modules, different code, both retained.

---

## 3. What is deliberately OUT of scope (the untouchables)

1. **Golden-vector backend tests** (§2.1) — single coverers of encoder/isel/regalloc/objfile
   branches.
2. **Law/invariant tests** — the checker/borrow/spine/const suites (§2.2).
3. **Oracles** — `lir_interp_test.tkt`, `minst_interp_test.tkt` (differential-agreement engines,
   not the retired VM).
4. **The unit backing of every fixture** — parser/lexer rejection tests. Thinning a fixture is
   safe ONLY because these keep the branch covered.
5. **Owner-pinned negatives** — every `EXPECT_COMPILE_FAIL` created by a dated owner ruling
   (§4.5 tension).

---

## 4. Poda candidates, by category

### 4.1 Category 1 — duplicate (two tests, same branch, same fn)

**Found: essentially none.** The one plausible cross-module duplicate (`list` vs
`collections::list`) was investigated and is a false positive (§2.4). The suite's 3.4
asserts/test and construct-grouped naming leave little literal duplication. **Reduction from
this category: ~0.**

### 4.2 Category 2 — obsolete / transitional (**the primary finding**)

**`src/reprobug/` — DELETE the whole module.** 5 tests + `reprobug.tks` + `inner/inner.tks`,
**~155 lines total.**

- **Evidence of obsolescence:** the module's own doc-comment states it is a "MINIMAL REPRO +
  regression guard for a confirmed VM state-corruption bug" in `find_function` **in
  `src/vm/vm.c` AND `src/vm/vm.tks`**. The VM was retired 100 % (issue #524,
  `docs/design/vm-retirement.md`, owner-ratified 2026-07-12: *"tudo que precisar pra remover
  100 % a VM"*). `src/vm/` is gone; the code the guard protects **no longer exists**.
- **Retention-of-coverage argument (the surviving invariant IS covered):** the only
  *language-level* behaviour the tests assert — that same-named functions in different
  namespaces (`teko::reprobug::probe(u64)` vs `teko::reprobug::inner::probe(str)`) resolve
  within their own namespace and do NOT cross-resolve — is independently covered by (a)
  namespace-resolution tests in `src/checker/checker_test.tkt`, and (b) the end-to-end fixture
  `examples/regressions/di_same_name_cross_ns/`. Deleting `reprobug` removes zero unique
  coverage.
- **Recommendation: DELETE** the module (test + the two scaffold `.tks`). This is the single
  cleanest win: it removes a whole namespace whose sole reason to exist retired with the VM,
  shrinking both the corpus (faster fixpoint) and the sweep surface.

**Adjacent (hygiene, NOT poda — reported up):** ~10 surviving `*_test.tkt` files still carry
stale comment-framing that references "the VM" / `src/vm` (e.g. `checker_test.tkt`,
`generics_test.tkt`, `closures_test.tkt`, `encode_x86_64_test.tkt`, `lexer_test.tkt`,
`manifest_test.tkt`). The *tests* are valid; only the prose is stale. This is a doc-sync/W15
refresh item — it does not reduce test count and is not part of this poda.

### 4.3 Category 3 — subsumed (narrow test fully inside a broader later one)

Covered by 4.2 (reprobug's invariant subsumed by checker + `di_same_name_cross_ns`). No other
subsumption can be **proven** without per-test coverage attribution (§1 limit). Per the
conservative gate, no further Category-3 deletion is claimed.

### 4.4 Category 4 — excessive granularity → **consolidate (do not delete)**

The suite is already well-grouped, so genuine N→1 opportunities are scarce and the payoff is
**count, not coverage**. Candidates where the survivor provably covers the same cases:

- **Backend encode/isel/fmt:** already table-grouped (`ex_encodes_every_condition_code`,
  `binary_operators_get_one_space` with 3 asserts). **Do NOT touch** — further merging hurts
  bisect-locality and legibility, which is the opposite of the owner's "legível" goal.
- **`own_exit*` e2e fixtures** (`own_exit_code`, `own_exit_zero`, `own_arith_exit`,
  `own_sub_exit`): overlapping exit-emission paths. `own_exit_zero` (zero) vs `own_exit_code`
  (non-zero) is a REAL branch — keep both; `own_arith_exit` vs `own_sub_exit` differ only by
  opcode already unit-covered in isel — **consolidate to 1**, net **−1 fixture**.

**Recommendation:** consolidate the `own_arith`/`own_sub` overlap only; leave all in-file unit
clusters as-is. **Rule: where poda = consolidate, the survivor must retain every assert** — so
these are asserts-preserving merges, not deletions.

### 4.5 Category 5 — redundant regression fixtures (same construct/exit, no new angle)

**The `discard_*` R-value cluster.** Four `EXPECT_COMPILE_FAIL` fixtures —
`discard_arith_read` (`_ + 1`), `discard_bare_read` (`y = _`), `discard_call_arg`
(`f("a", _)`), `discard_return_read` (`return _`) — all exercise the **identical branch**:
`parse_atom` rejecting `_` as an R-value ("`_` is a write-only discard and cannot be read").
Only the surrounding syntactic context differs.

- **Retention-of-coverage argument:** the contextual variety is **already unit-covered** —
  `parser_test.tkt` carries 13 discard tests hitting `_`-as-R-value across contexts. The
  fixtures add only the e2e "the whole build fails" assurance, which **one** representative
  provides. Keeping `discard_bare_read` (the minimal `y = _`) preserves the e2e wiring proof.
- **The other distinct discard branches STAY** (they are NOT the same branch):
  `discard_double_underscore` (`read_underscore`, **lexer**), `discard_as_alias` /
  `discard_let_binding` / `discard_typed_target` (`parse_bind_target`, distinct paths),
  `discard_param_plain_fn` (checker semantic), `underscore_as_x_rejected` (`_ as x` parser).
- **Recommendation: consolidate 4 → 1**, net **−3 fixtures** — **but see the tension in §5.**

No other fixture cluster is redundant: the per-backend matrices (`own_*` vs `wasm_*` vs
`native_lir_*`) exercise **different backends** (distinct code paths — not redundant with each
other), and the `member_const_*` (8) / `trait_derive_*` (5) / `comptime_fold_*` (5) families
each pin a distinct construct or an owner-ruled acceptance/rejection.

---

## 5. Risks and law tensions

**Tension T1 — owner-pinned negatives vs the reduce mandate (the discard cluster, §4.5).**
The four discard fixtures were each created by a **dated owner ruling (2026-07-15)** as explicit
negative regression pins. The 2026-07-20 ruling says "reduce". These two owner acts touch the
same artifacts. Law-first reading: a later general ruling ("reduce, keep coverage") does not
silently revoke an earlier *specific* ruling that named each fixture. **Recommended resolution:**
treat §4.5 as a **proposal to the owner**, not an auto-delete — present the 4→1 consolidation
(with the parser-unit-coverage retention argument) and let the owner confirm the merge of *their
own* pins. Coverage is unaffected either way, so this is safe to defer. **No HALT** — the audit's
job (surface + argue) is done; the decision to un-pin is the owner's.

**Risk R1 — no per-test coverage attribution (§1).** Every "subsumed" claim rests on structural
argument, not a machine-checked test→line map. Mitigation already applied: the ONLY outright
deletes proposed (§4.2 reprobug) rest on *deleted guarded code* + *named external coverers*, the
strongest possible non-mechanical evidence. Everything softer is "consolidate, asserts
preserved," which cannot lose coverage.

**Risk R2 — floor proximity.** Floors are functions=50, lines=50, branches=49 (`teko.tkp
[coverage]`), and CONTRIBUTING mandates 100 % on NEW-code deltas. The proposed reductions remove
tests whose branches are covered elsewhere, so measured coverage should be **~unchanged**.
Mitigation ritual: §7.

---

## 6. What stays OUT of the poda (the coverers we refuse to touch)

- Every **single coverer** of a backend encode/isel/regalloc/objfile/abi branch (§2.1).
- Every **law/invariant** test in checker/borrow/spine/const/comptime (§2.2).
- The **oracles** (`lir_interp`, `minst_interp`).
- The **unit backing** of the fixtures (parser/lexer rejection tests) — the reason a fixture can
  be thinned at all.
- Every **owner-pinned** negative fixture not proven same-branch-redundant.

---

## 7. Ritual points (where the full gate must re-pass for any executed poda)

Any future PR that *acts* on this audit (out of scope here — this is design-only) must, per
CONTRIBUTING §2–3:

1. Run `./bin/teko test .` (native gate) with coverage enforced — assert functions/lines/branches
   stay **at or above** the pre-poda measured values, not merely above the floors.
2. Run the four regression harnesses green: `scripts/compile_fail_regressions.sh`,
   `scripts/positive_regressions.sh`, `scripts/native_regressions.sh`,
   `scripts/crossmodule_regressions.sh`.
3. Re-verify the **self-host fixpoint** (gen-2 == gen-1) — deleting `src/reprobug/` changes the
   corpus, so the fixpoint must be re-baselined in the same PR.
4. Sequence per crumb: (a) delete `src/reprobug/` [biggest, cleanest]; (b) fixture consolidations
   [owner-confirmed]; each independently gate-able.

---

## 8. Bottom line

The owner's supposition holds **only weakly**: the suite CAN shrink while keeping coverage, but
the coverage-safe amount is **~0.2–0.6 % of tests and ~2 % of fixtures**, dominated by one clean
obsolete-module delete (`src/reprobug/`, a retired-VM guard) plus one owner-pinned fixture
consolidation. The suite's real character is **mature and lean** (0.48 test:source line ratio,
3.4 asserts/test, golden-vector backend), not padded. The larger legibility win is **hygiene**
(stale VM comment-framing) and the larger *future* win is **per-test coverage tooling** — both
reported up, neither a coverage cut.
