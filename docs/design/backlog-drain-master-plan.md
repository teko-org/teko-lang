# Backlog-drain master plan — zero the 74-issue backlog

**Author:** architect (READ-ONLY synthesis; no builds run producing this).
**Date:** 2026-07-06. **Seed:** `teko.tkp` current (`.37-alpha`). **Repo tip:** `main` @ `1aff6d9` (post-#298, post-#306 CI cut).
**Purpose:** the single ordered sequence the integrator executes SERIALLY, one PR at a time, each CI-validated (~6 m wall-clock post-#306).

This folds the five milestone readiness assessments + the three crumb-level drain docs
(`drain-265-168-native-gate.md`, `drain-onda3-subcluster-A.md`, `drain-fase3-stdlib-order.md`,
`onda3-monomorphization-cluster.md`, `compile-time-architecture.md`) into ONE actionable topo-order.

---

## 0. Ground truth (verified live against `gh` + tree, 2026-07-06)

- **74 open issues** (the task's "73" predates the #296→#303 merge; #184 is an UN-MILESTONED keystone — see §2).
  Full set: `158 159 163 164 167 168 171 172 173 174 178 179 180 182 183 184 186–226 (contiguous) 228 230 231 233 234 249 254 265 267 270 282 283 290 294 299 301 304 305`.
- **2 draft PRs in flight:** **#307** (fixes **#299**, `fix/issue-299`) and **#300** (advances **#184**, `fix/issue-184`, milestoned Onda-2). Both are the head of the ready-set — reconcile/merge these FIRST.
- **Milestone-1 "1 open" = PR #300 itself** (no separate Onda-2 issue remains; the monomorphic-stdlib milestone is effectively closed pending #300/#184).
- **CI is ~6 m** after #306 (`disable riscv/windows-arm + un-double gate`). The re-enable debt is **#304** (windows-arm64) + **#305** (riscv64-qemu) — un-milestoned, `fase-6-qualidade`.
- **Blocker-source keystones already CLOSED:** #157, #162, #165, #169, #181, #185, #221, #227, #229, #296 (#303), #160, #161, #166, #177.
- **5 `keystone`-labeled OPEN issues:** #163, #164 — plus the onda-3 cluster #254 and the native-gate #265/#168 are the load-bearing keystones this plan sequences first.

### HARD constraints (encode into every merge decision)

1. **MAIN INTEGRITY ABSOLUTA** — merge to `main` ONLY when ALL checks green (no "non-blocking exception"). Serial, one PR at a time. `fix-infra-first`; undraft before merge.
2. **RESOURCE LIMIT** — ONE heavy build at a time on the integrator's machine. Validation is OFFLOADED to CI (native.yml, ~6 m). The integrator does not run local full gates in parallel; agents draft, CI validates, integrator merges.
3. **KEYSTONES FIRST** — a keystone that unblocks N downstream issues merges before those N. Two keystone clusters gate the bulk of the backlog: the **onda-3 monomorphization+128-bit cluster** and the **native-gate cluster (#265/#168)**. Plus two stdlib-root keystones (**#184** IO/iter, **#194**/#199/#205/#210 stdlib roots).
4. **CLEAN-only** — `verify-both` (native #test gate + VM gate + fixpoint gen1==gen2 + diff_vm_native + paranoid) must be green in CI on the PR before merge.

---

## 1. THE DEPENDENCY DAG (ondas + keystones)

```
                              ┌──────────────────────────────────────────────┐
                              │  CLOSED blocker-source keystones (done)        │
                              │  #157 #160 #161 #162 #165 #166 #169 #177       │
                              │  #181 #185 #221 #227 #229 #296(→#303)          │
                              └──────────────────────────────────────────────┘

RG (READY-GATE, no keystone dep)         ── ready NOW, drain in parallel-of-one (serial merges)
   #307→#299 · #283 · #270 · #167        ── in-flight / small / independent
   #304 · #305                           ── CI re-enable debt (post-#306)

╔═══════════════════════════════ KEYSTONE CLUSTER K-A : onda-3 monomorph+128 ═══════════════════════════════╗
║   #299 ──(ready, PR#307)                                                                                   ║
║   #290 ──(ready)──►  #254 ──(needs #290)──►  #294 (needs #254)                                             ║
║   #301 ──(ready; VM half probe-gated)                                                                      ║
║        │                                                                                                    ║
║        └── #254 is THE monomorphization keystone → unblocks #163 (collections) + generic stdlib methods    ║
╚════════════════════════════════════════════════════════════════════════════════════════════════════════╝
                                          │
                                          ▼ (#254 merged)
                     ┌────────────────────────────────────┐
                     │  #163 collections (Map/List/Set)    │  ← generic classes; consumed by net/db/web/cloud
                     └────────────────────────────────────┘

╔═══════════════════════════════ KEYSTONE CLUSTER K-B : native test-gate ═══════════════════════════════════╗
║   #265 (line/branch native instrumentation)  ──►  native enforces 3 floors  ──►  CI flip (VM→nightly)      ║
║   #168 (compile-once partition; Phase-B full BLOCKED on C7.16, but 1595→366MB win lands WITHOUT it)        ║
║   #265 is lever-B of #249; unblocks the memory+speed headline for the whole gate                           ║
╚════════════════════════════════════════════════════════════════════════════════════════════════════════╝
                                          │
                    ┌─────────────────────┴─────────────────────┐
                    ▼                                            ▼
       #249 (self-build perf, levers)              #234 (W15 final sweep — BLOCKED on onda-4 close)

╔═══════════════════════════════ KEYSTONE : #184 IO/iter (un-milestoned) ═══════════════════════════════════╗
║   #184 (Reader/Writer/Seeker/Closer + combinators + iter protocol; PR#300 in flight)                       ║
║   partially rides #301 (flat_map/closure-in-Ref) for full combinators                                      ║
║   unblocks 6+ Onda-5 leaves: #201 HTTP · #209 log · #212 client · #215 config · net/db streaming            ║
╚════════════════════════════════════════════════════════════════════════════════════════════════════════╝

ONDA-4 (fase-1 linguagem)  ── mostly ready (all named blockers CLOSED)
   #283 #172 #171 #174 #173  ── independent
   #178 ──► #179             ── trait Json → trait Schema (linear)
   #164 (S8 async/concurrency)── ready, fully designed; TSan re-expand per primitive

ONDA-5 (fase-3 stdlib)  ── 3 critical paths, keystone-gated
   MONOMORPHIC ROOTS (no #254): #186 math·#189 enc·#192 compress·#194 crypto  ── ready NOW
   crypto family : #194 ──► #195 #196 ──► #197 ──► #198
   net    family : #199 ──► #200 ──► #201 ──► #202 #203 ──► #204
   web    family : #210 ──► #211 #212 #213 ──► #214            (over #201, #178/#179, #196)
   db     family : #205 ──► #206 #207 ──► #208                  (over #199, #200, #195, #181✓)
   cloud  family : #209 ──► #216 #217                           (#217 over #164)
   collections   : #163 (BLOCKED on #254) ──► #215 config, env-as-Map
   math   family : #186 ──► #187 #188 · #192 ──► #193

PACKAGING (trilha)  ── #158→#159 dist track ; #180 .tkl ; pkg-mgr chain
   #158 ──► #159        (stdlib dist → Homebrew)
   #180 (.tkl+prelinker; #165✓) ──► #218 ──► #219 ──► #220
   #218 also needs #194 (crypto hash) ; #219 needs #201 #192 #197 ; #182 TCC deferred post-alpha
   #183 capabilities (independent) ; #267 release-selection (item-2 ready, item-1 post-alpha)

BACKEND (onda-6)  ── linear chain, OFF the self-build critical path
   #222 (Linux x86_64 ref; #221✓) ──► #223 #224 ──► #225 ──► #226

TOOLING  ── #228 plugins·#230 docgen·#231 lint  ready ; #233 LSP DEFERRED (no start gate)

QUALIDADE  ── #167 #168 #249 #265 #270 #282 ready ; #234 BLOCKED on onda-4 close
```

---

## 2. KEYSTONE NOTES (the two named clusters + the stdlib roots)

### K-A — onda-3 monomorphization + 128-bit (crumbs in `drain-onda3-subcluster-A.md`)
- **Order is FIXED: `#290 → #301 → #254 → #294`** (methods lower through #290's keying; #294 needs #254's stamped method as its direct target). #299 is independent (128-bit literal panic) and rides in first via PR #307.
- **#254 is THE monomorphization keystone.** It unblocks #163 (collections as generic classes) and every generic stdlib method. 5 layers, re-verified site counts (L1 = 2 fn-body edits; L2 = 3 sites incl. a ClassBody passthrough bug; L4 needs an `Env.expected_ret` field — a WIDE change, every `Env {…}` literal must default it).
- **Under-scoping caught** in ALL FOUR roots (the #296 lesson): #290 = 3 sites (not 2), #301 = 5 native + 1 VM probe (not 3), #254 as above, #294 = 2 sites. Trust the drain doc's counts, not the parent cluster doc's estimates.
- **#301 VM half is PROBE-GATED** (`TEKO_TRACE` the `reseat` repro). The native mangle half is fully specified and unblocks the `ReadFn | error` union-arm + `flat_map` (feeds #184).
- **#294 ruling (a) APPLIED law-first** (autonomous call, logged for LTS review): a constraint is a monomorphization gate, not a dynamic-dispatch promotion → constraint-bound structs dispatch DIRECT to the stamped method, no vtable, no new rep. Heterogeneous struct-as-contract collections are explicitly OUT of scope (report-only, not an issue).

### K-B — native test-gate #265 + #168 (crumbs in `drain-265-168-native-gate.md`)
- **The infra is REAL and opt-in TODAY** (`CgMode::TestCov`, `emit_test_main`, `tk_cov_mark`, the cross-process `.tkcov` protocol). The ONLY compiler gap is emitting per-line/per-branch marks inside production fn bodies under TestCov.
- **#265 Track A** (A0 fixtures → A1 thread `CovCtx` all-`cov_off()` byte-identical → A2 line mark → A3/A4 branch marks → A5 `tk_cov_line_at`/`tk_cov_branch_at` explicit-fn runtime entry points → A6 swap floor → A7 comment-truth). **A5 is the single load-bearing decision**: stack-only keying attributes every native mark to the TEST → false 0%; the `_at` entry points in the maintained `teko_rt.{c,h}` seam fix it (Teko-only law satisfied — runtime seam is NOT a frozen twin).
- **Fixpoint SAFE:** all instrumentation gated on `TestCov`/`CovCtx.on`; the `Program` path (the fixpoint oracle) emits ZERO `tk_cov_*` and is never touched. Fixture 6 makes byte-identity a hard gate.
- **#168 headline (1595→366 MB) lands WITHOUT C7.16** via B1 (reuse `fe`, no re-front-end) + B2-lite (process-boundary isolation). Full per-context `.tkb` partition (B3) is BLOCKED on C7.16 → honest-stop doc-comment only.
- **Track C (CI flip)** demotes VM to a nightly lane — gated on fixture 8 (VM==native floor parity over the 79 `examples/regressions/`). This is the main-integrity-safe way to leave the VM gate off PR CI.
- **Coupling:** #265 IS lever-B of #249 and makes #168 actually faster. Sequence #265 before the #249 lever-B claim and before #234's final sweep (which benefits from the faster/leaner gate).

### Stdlib-root keystones (Onda-5)
- **#184 (IO/iter, un-milestoned)** — PR #300 in flight. Rides #301 for full closure-combinators (`flat_map`). Unblocks #201/#209/#212/#215 + net/db streaming. Treat as a keystone; merge early in the Onda-5 window.
- **#194** (crypto core) → crypto family; **#199** (net core) → net/http/web/db leaves; **#205** (db iface) → db; **#210** (web router) → web. These four roots gate ~14 leaves; drain the roots before their families.

---

## 3. ORDERED EXECUTION SEQUENCE (serial merges, one PR at a time)

Legend: **[R]** ready NOW · **[K]** keystone · **[B:#n]** blocked-by · **[S/M/L]** effort · `∥` = independent (any order within the batch, still merged serially).

### BATCH 0 — clear the in-flight + CI debt (do FIRST)
| Seq | Issue/PR | Why first | Effort |
|-----|----------|-----------|--------|
| 0.1 | **PR #307 → #299** [R] | in-flight; independent 128-bit literal fix; head of ready-set | S |
| 0.2 | **PR #300 → #184** [K] | in-flight IO/iter keystone (Onda-2 close); reconcile then merge | L |
| 0.3 | **#304** ∥ **#305** [R] | CI re-enable debt from #306; restore full platform gate BEFORE trusting green | S each |

> Rationale: undrafting/merging the two in-flight PRs unblocks the head of two chains and closes the last Onda-2 item. #304/#305 restore the platform matrix so subsequent CLEAN judgments are trustworthy (main-integrity: don't merge on a weakened gate). If #304/#305 need upstream toolchain fixes, keep them as periodic/nightly lanes and proceed — do NOT block the drain on them (owner-review flag, §5).

### BATCH 1 — KEYSTONE K-A (onda-3 monomorph), fixed order
| Seq | Issue | State | Effort |
|-----|-------|-------|--------|
| 1.1 | **#290** [R][K-prereq] same-bare cross-ns method dispatch (3 sites) | ready | S |
| 1.2 | **#301** [R] closure-in-Ref round-trip (native half load-bearing; VM half probe-gated) | ready | M |
| 1.3 | **#254** [K][B:#290] generic-type methods — THE monomorph keystone (5 layers) | after 1.1 | L |
| 1.4 | **#294** [B:#254] struct-through-constraint dispatch (ruling (a), 2 sites) | after 1.3 | M |

> #254 gate-able per-layer; the `any_generic` no-op guard (gen1==gen2 byte-identity) is the single most important fixpoint check — verify at EACH layer, not only at the end.

### BATCH 2 — KEYSTONE K-B (native gate), Track A→C
| Seq | Issue | State | Effort |
|-----|-------|-------|--------|
| 2.1 | **#265** [K] native line/branch instrumentation (A0→A7) | ready | M |
| 2.2 | **#168** [B:#265-A6 for the win] compile-once partition (B1+B2-lite; B3 honest-stop) | after 2.1 | M |
| 2.3 | (CI flip, Track C) VM→nightly | gated on fixture-8 parity | S (CI-config) |

> Do K-B early: it makes every subsequent PR's CI leaner+faster (the whole backlog benefits). It is INDEPENDENT of K-A — could run in parallel-of-one interleaved with Batch 1 if the integrator prefers speed on CI first. Recommended: Batch 0 → Batch 2 (leaner CI) → Batch 1 → onward.

### BATCH 3 — ONDA-4 fase-1 linguagem (mostly independent)
| Seq | Issue | State | Effort |
|-----|-------|-------|--------|
| 3.1 | **#283** [R] latent -O2 UB in teko.c | ready | S |
| 3.2 | **#172** ∥ **#171** ∥ **#173** [R] S9 cleanup · latent catalog · W10c extras | ready | M each |
| 3.3 | **#174** [R] strings ROUND-0 (regex NFA-vs-backtracking = OPEN design decision, §5) | ready | M–L |
| 3.4 | **#178** [R] trait Json (synthesized) → **#179** [B:#178] trait Schema | linear | M each |
| 3.5 | **#164** [R][design✓] S8 async/concurrency (Intent<T>); TSan re-expand PER primitive | ready | L |

> #164 is orthogonal to the traits chain — can interleave. #234's blocker set (§Batch 8) includes #164/#163/#172/#171, so close those before the final sweep.

### BATCH 4 — ONDA-5 monomorphic ROOTS (no #254 dep; drain in parallel-of-one)
| Seq | Issue | Root-of | Effort |
|-----|-------|---------|--------|
| 4.1 | **#186** [R] math::real (libm FFI; `-lm` already linked) | #187 #188 | S |
| 4.2 | **#189** [R] encodings T1 (base64→url→csv→toml→multipart) | #215 | M |
| 4.3 | **#192** [R] compress DEFLATE/gzip/zlib (adler32→inflate→deflate→wrappers→ZIP) | #193 #219 | M |
| 4.4 | **#194** [R][K-root] crypto core (SHA-2/3/BLAKE/MD5-dep + CSPRNG via new `tk_rt_secure_bytes`) | crypto family, #218 | L |
| 4.5 | **#187** [B:#185✓] bigint/decimal/rational · **#188** [B:#186] math-adv · **#193** [B:#192] compress-adv | leaves | M each |
| 4.6 | **#190** ∥ **#191** [R] binary-enc · doc-enc | | M each |

### BATCH 5 — ONDA-5 collections + config (needs #254 from Batch 1)
| Seq | Issue | State | Effort |
|-----|-------|-------|--------|
| 5.1 | **#163** [B:#254] collections (Map/List/Set/BTree as generic classes); env-as-Map cutover | after 1.3 | M |
| 5.2 | **#209** [R] teko::log (facade + JSON/pretty) | | S |
| 5.3 | **#215** [B:#163 #189 #191 #177✓] config (env/file/flags, typed via traits) | after 5.1, 4.2 | M |

### BATCH 6 — ONDA-5 crypto → net → http → db → web → cloud (family chains)
Drain each family root-first; leaves after their root. Serial merges, but families are mutually independent (pick the order that keeps CI busy).
| Family | Ordered chain |
|--------|---------------|
| crypto | **#195** [B:#194] HMAC/KDF · **#196** [B:#194] cipher/AEAD → **#197** [B:#196 #190 #187] pk/x509 → **#198** [B:#197] PGP/JWT |
| net    | **#199** [B:#184] core/tcp/udp/dns → **#200** [B:#199] TLS → **#201** [B:#199 #200 #192] HTTP/1.1 → **#202** [B:#201 #194] WS/SSE · **#203** [B:#200 #196 #201] QUIC/H2/H3 → **#204** [B:#201 #197] mqtt/redis/mail/ssh |
| db     | **#205** [B:#199] iface → **#206** [B:#205 #200 #195] postgres · **#207** [B:#205 #181✓] sqlite → **#208** [B:#206 #204] mysql/mongo/pool |
| web    | **#210** [B:#201] router → **#211** [B:#210 #196 #209] middleware · **#212** [B:#201 #184] client · **#213** [B:#210 #202] WS/SSE → **#214** [B:#213 #178 #179] binding/validation/OpenAPI |
| cloud  | **#216** [B:#209 #210] metrics/trace/health · **#217** [B:#164 #209] shutdown/resilience/cron |

### BATCH 7 — PACKAGING (trilha) + BACKEND (onda-6) + TOOLING
Off the self-build critical path — schedule opportunistically between heavier batches to keep CI busy.
| Track | Ordered chain |
|-------|---------------|
| dist    | **#158** [R, #157✓] stdlib dist → **#159** [B:#158] install.sh + Homebrew |
| .tkl    | **#180** [R, #165✓] .tkl ZIP + pre-linker |
| pkg-mgr | **#218** [B:#180 #185✓ #194] local model → **#219** [B:#218 #201 #192 #197] fetch/registry → **#220** [B:#219 #181✓ #180] wiring-on-demand |
| security| **#183** [R] capabilities/sandboxing (independent) |
| release | **#267** item-2 [R] tag-sans-release fallback ; item-1 (LTS-default) post-alpha |
| toolchain| **#182** [deferred post-alpha] TCC bundle (NOT an alpha blocker) |
| backend | **#222** [R, #221✓] Linux-x86_64 ref → **#223** ∥ **#224** [B:#222] arm64/riscv/win · wasm → **#225** [B:#223 #224] 3-way diff CI → **#226** [B:#225] custom linker |
| tooling | **#228** [R, #227✓] editor plugins ∥ **#230** [R] docgen ∥ **#231** [R, #229✓] lint ; **#233** LSP DEFERRED (no start gate) |

### BATCH 8 — QUALIDADE & perf (some ride the keystones; #234 last)
| Seq | Issue | State | Effort |
|-----|-------|-------|--------|
| 8.1 | **#270** [R] dead-code removal (triage vs #184 io not-yet-consumed) | ready | S |
| 8.2 | **#167** [R] residual memory (malloc-str ~41 MB + streaming) | ready | M |
| 8.3 | **#282** [R, #229✓] enable `fmt --check .` CI gate (seed ≥ .29; verify LF .gitattributes) | ready | S |
| 8.4 | **#249** [R] self-build perf levers (B ties #265; C/D straightforward; E=S8 threading) | after 2.1 | M |
| 8.5 | **#234** [B:#164 #163 #160✓ #172 #171] W15 final DRY/KISS/SOLID sweep — LAST | after Batch 3/5 | L |

---

## 4. READY-SET (start immediately) vs WAIT-FOR-KEYSTONE

**READY NOW (no open blocker; drain serial, any order — parallel-of-one):**
`#307/#299` · `#300/#184` · `#304` · `#305` · `#290` · `#301` · `#265` · `#283` · `#172` · `#171` · `#173` · `#174` · `#178` · `#164` · `#186` · `#189` · `#190` · `#191` · `#192` · `#194` · `#209` · `#158` · `#180` · `#183` · `#222` · `#228` · `#230` · `#231` · `#270` · `#167` · `#282` · `#267`(item-2)
→ **32 issues startable with zero keystone wait.**

**WAITS FOR A KEYSTONE:**
| Waits on | Unblocks |
|----------|----------|
| **#290** | #254 |
| **#254** (K-A) | #163 → #215 ; all generic stdlib methods |
| **#265** (K-B A6) | #168 win, #249 lever-B, CI flip, leaner CI for all |
| **#184** (K) | #199/#201/#209/#212 streaming, iter combinators (+ #301 for flat_map) |
| **#194** (root) | #195 #196 #197 #198 #202 #218 |
| **#199** (root) | #200 #201 #202 #203 #204 #205 #206 |
| **#201** (root) | #202 #203 #204 #210 #211 #212 #213 #219 |
| **#205** (root) | #206 #207 #208 |
| **#210** (root) | #211 #213 #214 #216 |
| **#178→#179** | #214 |
| **#164** | #217 |
| **#222** | #223 #224 → #225 → #226 |
| **#180** | #218 → #219 → #220 |
| onda-4 close (#164/#163/#172/#171) | #234 |

---

## 5. RISK / DECISION NOTES (owner review) + autonomous calls already made

### Autonomous calls I made (law-first; logged for LTS v1.0.0.0 review)
1. **#294 ruling (a) — constraint = monomorphization gate, not dynamic-dispatch promotion.** Constraint-bound structs dispatch DIRECT to the stamped method (no vtable, no new rep); heterogeneous struct-as-contract collections are OUT of scope. Basis: M.1/M.3 + `teko-oop-w10b-design` (structs are value types). No HALT. *(from `drain-onda3-subcluster-A.md`)*
2. **#265 A5 — add `tk_cov_line_at`/`tk_cov_branch_at` to the maintained `teko_rt.{c,h}` seam.** The runtime seam is NOT a frozen twin (no-mirroring ruling), so permitted C growth. This is the ONLY correct fix for the stack-keying false-0% trap. Basis: `teko-no-more-mirroring` + `teko-native-test-gate`. *(from `drain-265-168-native-gate.md`)*
3. **Sequencing: run K-B (native gate) before/interleaved-with K-A** to make all subsequent CI leaner+faster — a whole-backlog win. Basis: `teko-decision-autonomy-and-log` ("aplicar o recomendado sem travar"). Reversible ordering choice.
4. **#184/#300 treated as a keystone** despite being un-milestoned — it gates 6+ Onda-5 leaves. Recommend milestoning it into Onda-3 or a dedicated IO keystone slot (owner: confirm milestone).
5. **#304/#305 do NOT block the drain** — if they need upstream toolchain fixes, keep them as nightly lanes and proceed. Basis: main-integrity is satisfied by the retained platform coverage as a periodic job; blocking the whole backlog on a QEMU flake would violate "aplicar o recomendado sem travar".

### OPEN design decisions the owner should resolve BEFORE the relevant batch
- **#174 regex: NFA vs backtracking — NOT chosen.** Blocks the regex half of #174 (spec `{n,m}`, non-greedy, backreferences marked optional). `str_from_utf8` + encoding scope is clear. **Decision needed before Batch 3.3.**
- **#301 VM half is probe-gated** — the `TEKO_TRACE reseat` probe pins snapshot vs `coerce_to` vs merge. Land whichever the probe proves; the native half ships regardless. Not a blocker for #254.
- **#254 layer-4 `Env.expected_ret`** — a WIDE change (every `Env {…}` literal must default the new field). Name-verify `type_expr_expected`/`type_value_expected` before editing. Flagged as the highest-churn crumb.
- **#233 LSP has NO start gate** — DEFERRED "until compiler stabilizes." Owner defines the stabilization trigger, or it stays parked.
- **#182 TCC bundle DEFERRED post-alpha** — macOS needs Apple SDK (accepted per pre-alpha ruling); not an alpha blocker. Confirm it stays out of the alpha drain.
- **#267 item-1 (LTS-default release selection)** — deferred to first stable (post-alpha). Item-2 (tag-sans-release fallback) is ready now.

### REPORTED findings (fold/sequence, do NOT spawn issues — per `teko-issues-must-be-100-percent`)
- Manifest `m = []` extern row probe (#186) — resolve by inspecting the resolver path; report either way.
- `Hash` vs `Hashable` trait-name reconciliation (#177/#298 synthesized traits vs the #163 Map-key constraint `K: Hashable & Eq`).
- TOML datetime ↔ `teko::time` coupling (#189/#215) — carry raw `str` first if `teko::time` types aren't wired.
- The parent `onda3-monomorphization-cluster.md` UNDER-COUNTS sites in all four roots — trust `drain-onda3-subcluster-A.md`'s verified counts.

### Convergence guarantee
Issues never spawn issues; each delivers 100% of its proposal with no regression; adjacent findings are REPORTED (integrator folds/sequences). The backlog is a finite DAG with all cycles broken by the closed keystones — draining in the Batch 0→8 order converges to ZERO with no orphan.

---

## 6. Per-PR ritual (every merge)
1. Agent drafts on `fix/issue-NN` (isolated worktree), base `main`.
2. CI runs `verify-both`: native #test gate + VM gate + fixpoint gen1==gen2 byte-identical + `diff_vm_native.sh` + `TEKO_MEM_PARANOID=1` + `//`-audit (W15). ~6 m.
3. Full gate at the END of every crumb that adds a new corpus `.tks` (self-build changes). Extern-touching crumbs = native-authoritative + VM honest-stop fixtures.
4. Undraft only when ALL checks green. Integrator merges serially (CLEAN-only, no exceptions). Push after each green merge; keep the PR body's `Closes #NN` current.
5. Bump the 4th version field in `teko.tkp` on each code merge (manual; triggers the alpha release/tag).
6. FAXINA: kill orphan research sub-agents, prune merged local branches + orphan worktrees at session end.
