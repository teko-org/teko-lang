# D2 / N8 — the C-vs-own differential across all targets, the C-RETIRE ladder, and the DWARF/PDB scope fork

**Status:** DESIGN-AHEAD (doc-only). Sub-PR of the own-AOT-backend epic (#339), Phase D.
Issue **#391** (D2/N8), branch `fix/issue-391-3way-gate`; this proposal authored on
`design/391-d2-proposal`, base `remodel/backend-build`. **Closes the design owed by #391; closes #225's
decision-collection duty.** This doc writes NO product code and RETIRES nothing — it (1) designs the
implementable-now differential-completeness work, (2) frames the C-RETIRE ladder as tracked,
release-over-release evidence for the OWNER to rule, and (3) presents the DWARF/PDB scope decision as a
crisp OWNER FORK.

> **Three buckets, kept separate throughout.** Every item below is tagged:
> **[NOW]** — implementable immediately against merged code;
> **[RULING]** — needs an owner decision (a fork, not a default);
> **[DEFERRED]** — blocked until the E-cluster M-linker (#392/#393/#394/#226) or a not-yet-landed target
> leg (wasm-browser N6b, windows-arm64 execution).

---

## 0. Two facts the parent doc and the issue predate — reconcile FIRST

`own-backend-architecture.md` §2.3/§2.4/§2.5 and its §4 D2/N8 block, and the #391 issue body, both
describe a **3-way `VM == C-native == own-native`** gate extended from `scripts/diff_vm_native.sh`. Two
things landed since that were written; this proposal is reconciled to today's tree, not the doc's:

**Fact 1 — the VM is RETIRED (D37, #524/#548, owner-ratified 2026-07-12/13).** `src/vm/` is deleted,
the C bootstrap mirror is gone, and native AOT is the sole execution engine (`vm-retirement.md` §0;
`DECISION_LOG.md` D37). **There is no VM leg.** `scripts/diff_vm_native.sh` was gutted and renamed to
`scripts/native_regressions.sh` (VM-retirement crumb 1; the surviving #64 CWD/`rt_dir` regression). The
"3-way" of #391 is therefore now a **2-way `C-native == own-native`** differential. The LIR interpreter
(`src/lir/lir_interp.tks`) remains a *pre-machine* lowering oracle inside `teko test`, but it is not a
leg of the executing differential and is out of scope here.

**Fact 2 — the executing differential ALREADY EXISTS and passes.** The C-vs-own differential was born
at the A4-5 keystone (#385) as **`scripts/diff_c_own.sh`** — it builds each `examples/regressions/`
fixture TWICE with the same self-hosted `teko` (C-native then own-native), asserts identical exit code +
stdout, and runs a host-tool object well-formedness check (`check_macho.sh`/`check_elf.sh`/
`check_coff.sh`) on the emitted `.o` (`diff_c_own.sh:1-27`). It carries the `"(own backend)"` success
marker guard against a silent env-seam no-op (`diff_c_own.sh:28-39`, `:322-327`) and a `KNOWN_STOP` list
for fixtures the own path honest-stops on (`:210-248`). **The differential is not built from scratch by
#391; #391 COMPLETES it** across the remaining targets and collects the retire/DWARF decisions.

**Net restatement of #391's real scope** (what this doc designs):
1. **[NOW]** close the target-coverage gap of the *existing* 2-way differential toward all 8 §2.1
   targets, and name the CI ritual leg that proves it;
2. **[NOW/RULING]** reconcile the differential scripts' backend-selection seam (`TEKO_BACKEND` env →
   the D1 `--backend` flag) with the pending default-inversion ruling;
3. **[RULING]** frame the C-RETIRE ladder (§2.4) as tracked evidence — owner rules the thresholds;
4. **[RULING/DEFERRED]** present the DWARF/PDB scope fork (§2.5) — owner rules the depth.

---

## 1. The 8-target parity set and what the differential covers TODAY

The parity set is fixed by `own-backend-architecture.md` §2.1 (anchored to the release matrix). Coverage
below is read from `scripts/diff_c_own.sh`'s host branches (`:99-152`), `scripts/validate_wasm_own.sh`,
and the CI wiring in `.github/workflows/native.yml` (`gen1-checks` matrix `:216-233`; the riscv job
`:156-196`; the wasm keystone `:352-358`) and `sanitizers.yml` (windows-selfhost `:306-312`).

| # | Target (ISA / ABI / format) | Executing own==C leg today | Where it runs | Bucket |
|---|-----------------------------|----------------------------|---------------|--------|
| 1 | arm64 / AAPCS64 / **Mach-O** (macOS) | **YES** (native exec) | `native.yml` `gen1-checks` `macos-arm64` → `diff_c_own.sh` (`:121-125`) | [NOW] covered |
| 2 | arm64 / AAPCS64-Linux / **ELF** (glibc+musl) | **NO** | — no `diff_c_own.sh` branch, no CI leg | **[NOW] addable** (§2.1) |
| 3 | x86-64 / SysV / **ELF** (Linux) | **YES** (native exec) | `native.yml` `gen1-checks` `linux-x86_64` → `diff_c_own.sh` (`:126-130`) | [NOW] covered |
| 4 | x86-64 / Win64 / **PE/COFF** (Windows) | **YES** (native exec, no emulator) | `sanitizers.yml` windows-selfhost → `diff_c_own.sh` (`:131-148`) | [NOW] covered |
| 5 | arm64 / Win64 / **PE/COFF** (Windows) | **NO** (no windows-arm64 runner) | — encoder+COFF emit exist (B3), no executing host | **[DEFERRED-exec] / [NOW-structural]** (§2.2) |
| 6 | riscv64 / LP64D / **ELF** (Linux) | **YES** (qemu-riscv64) | `native.yml` riscv job → `TEKO_DIFF_TARGET=riscv64-linux diff_c_own.sh` (`:99-120`, `:191-196`) | [NOW] covered |
| 7 | wasm32 / WASI / **Wasm** | **YES** (wasmtime) | `native.yml` `gen1-checks` `linux-x86_64` → `validate_wasm_own.sh` (`:352-358`, keystone #389 C1-8c) | [NOW] covered |
| 8 | wasm32 / Browser (JS-import RT) / **Wasm** | **NO** (WASI only; N6b RT not landed) | — `validate_wasm_own.sh` targets `wasm32-wasi` only (`:237-238`) | **[DEFERRED]** until N6b browser RT + a node/headless harness |

**Score today: 5 of 8 targets have an EXECUTING own==C differential.** The three gaps: #2 arm64-Linux
ELF (addable now), #5 arm64-Windows PE/COFF (executing blocked on a runner; structural now), #8
wasm-browser (blocked on the N6b runtime). No fabricated coverage is claimed for the gaps (M.3).

---

## 2. The differential-completeness design — [NOW] implementable

### 2.1 [NOW] Add target #2 — arm64-Linux ELF via qemu, mirroring the riscv lane

The cleanest closable gap. `diff_c_own.sh` already has the full cross-execution machinery for a
non-host ISA: the riscv lane selects by `TEKO_DIFF_TARGET`, cross-links with a `*-linux-gnu-gcc`
(`TEKO_CC_ENV`), runs both binaries under a `RUN_WRAP` emulator, and cross-checks the ELF with the
target binutils (`OBJ_CHECK_ENV`) — see `diff_c_own.sh:99-120`. An arm64-Linux leg is the **same shape
with different tokens**:

- select on `TEKO_DIFF_TARGET=arm64-linux` (an explicit-select lane, like riscv — the CI host is
  x86_64, so `uname` must NOT pick it);
- `OWN_TARGET_ENV="TEKO_TARGET=arm64-linux"` (the own backend's target seam, `project.tks:721-727`);
- `TEKO_CC_ENV="TEKO_CC=aarch64-linux-gnu-gcc"` (the cross-linker);
- `RUN_WRAP="qemu-aarch64-static"` (the executing emulator);
- `OBJ_CHECK="$script_dir/scripts/check_elf.sh"` with `OBJ_CHECK_ENV="ELF_TOOLCHAIN=aarch64-linux-gnu- ELF_MACHINE=AArch64"`;
- honest-skip (exit 0) when the cross-gcc / qemu are absent, exactly as the riscv lane does
  (`diff_c_own.sh:106-113`).

CI wiring: a new `build-test / linux-arm64 (qemu own==C differential)` job in `native.yml` mirroring the
riscv job (`:156-196`) — `apt-get install gcc-aarch64-linux-gnu qemu-user-static`, then
`TEKO_DIFF_TARGET=arm64-linux ./scripts/diff_c_own.sh` on gen1. **This is a script-only + workflow-only
change — no compiler `.tks` touched** (the arm64 encoder + `objfile_elf.tks` already exist from A4/B1;
`objfile_elf.tks` is ISA-agnostic and B2 already proved the ELF emitter carries a second ISA). This
takes the executing score to **6 of 8**.

### 2.2 [NOW-structural / DEFERRED-exec] Target #5 — arm64-Windows PE/COFF

The own backend already emits arm64 machine code (A4) and PE/COFF objects (B3, `objfile_coff.tks`), so a
**structural** leg is buildable now: cross-emit the `.o` under `TEKO_TARGET=arm64-windows` and run
`check_coff.sh` (llvm-readobj/llvm-objdump) on it — the same object cross-check the x86-64 Windows lane
runs (`diff_c_own.sh:144-148`). What is NOT available is an **executing** leg: GitHub offers no
windows-arm64 runner, and there is no in-CI arm64-Windows emulator equivalent to qemu-user for PE. So
target #5's exit-code equality is [DEFERRED-exec] until a windows-arm64 runner exists; the structural
object-validity check is [NOW]. **Recommendation (law-first, M.3):** add the structural leg now (it is
honest, cheap, and catches encoder/COFF regressions), and record in the tracking table (§3) that #5's
*executing* equality is UNPROVEN — so the retire clock (§3) cannot count #5 as green until execution
lands. This keeps "all 8 targets green" honest rather than counting a structural pass as an execution
pass.

### 2.3 [DEFERRED] Target #8 — wasm32 Browser

`validate_wasm_own.sh` covers `wasm32-wasi` only (`:237-238`); the browser variant (N6b) needs the
JS-import runtime (`teko_rt` browser variant + JS glue) and a node/headless harness, neither landed. No
[NOW] work is possible beyond noting the gap. The WASI leg (#7) is the wasm proxy until N6b closes.

### 2.4 [RULING-adjacent] The backend-selection seam: `TEKO_BACKEND` env → `--backend` flag

**Today's reality.** `diff_c_own.sh` selects the backend by the ENV seam: the C leg does
`unset TEKO_BACKEND` (`:296`), the own leg sets `TEKO_BACKEND=native $OWN_TARGET_ENV` (`:306`). The D1
`--backend={c,native}` flag landed at 0.3.0.21 (#404): `backend_of` (`project.tks:900-914`) reads the
flag with the `TEKO_BACKEND` env as a *lower-precedence fallback* (`native_backend_selected`,
`project.tks:882-884`; the fallback line `:913`), and `Manifest.backend` defaults to `Backend::C`
(`manifest.tks:379`), threaded via `with_backend` (`manifest.tks:393`) into the `emit_binary` dispatch
(`project.tks:1777`). So **both seams work today**; the env seam is still live.

**The reconciliation.** The **default-inversion ruling (owner 2026-07-14)**, recorded in
`docs/design/backend-default-inversion.md` (rescued onto the living-docs branch, PR #572), already
OWNS the migration: its crumb **S5 / §2.4** specifies migrating `diff_c_own.sh:296` from
`unset TEKO_BACKEND` to `--backend=c`, and `:306` from `TEKO_BACKEND=native` to `--backend=native`
(keeping `$OWN_TARGET_ENV`/`TEKO_TARGET`, which is orthogonal target selection, and keeping the
`"(own backend)"` marker assertion). That ruling *retires the `TEKO_BACKEND` env seam entirely*
(default-inversion crumb S1, M.5 "one selector"). **D2 must NOT duplicate or pre-empt that migration.**

**Recommendation (law-first, avoids a double-migration and a merge race):**
- **The env→flag migration of the differential scripts is owned by the default-inversion PR (S5), not
  by D2.** D2's new arm64-Linux leg (§2.1) should be written in the SAME seam the script currently uses
  so it does not fork the migration: if D2 lands before default-inversion, use the env seam
  (`OWN_TARGET_ENV` + `TEKO_BACKEND=native`) exactly as the riscv lane does; the S5 migration then
  rewrites all legs uniformly in one place. If default-inversion lands first, D2's new leg uses
  `--backend=native` from the start.
- **The CI matrix legs** (`.github/workflows/native.yml`) select the backend *inside* `diff_c_own.sh`,
  not at the workflow level — the workflow only sets `TEKO_DIFF_TARGET` + installs the toolchain. So no
  workflow-level backend-flag change is needed for the differential; the flag migration is entirely
  inside the scripts (S5's concern). **This is the answer to "should the CI matrix migrate to
  `--backend`":** the differential scripts migrate (via S5); the workflow legs do not select a backend
  and need no change beyond adding the new target job (§2.1).

This is [RULING-adjacent] only in sequencing (who lands the migration); the differential design itself
is [NOW] and seam-agnostic.

### 2.5 [NOW] The ritual leg that PROVES the differential

The gate leg is: **the `own==C differential` step green on every wired target lane, on gen1, per PR** —
concretely the `diff_c_own.sh` step in `native.yml` `gen1-checks` (macOS-arm64, linux-x86_64), the
riscv job, the new arm64-Linux job (§2.1), the windows-selfhost `diff_c_own.sh` (x86-64 PE/COFF), and
the `validate_wasm_own.sh` keystone (wasm32-wasi). All must be REQUIRED (not honest-skipped) on their
provisioned hosts — the wasm leg already flips its skip to a hard failure via `REQUIRE_WASM_ENGINE=1`
(`native.yml:352-358`); the new arm64-Linux leg must likewise be REQUIRED once qemu+cross-gcc are
provisioned. **This green-on-every-wired-target state, sustained release-over-release, is the input the
retire clock reads (§3) — the differential does not itself retire anything.**

---

## 3. The C-RETIRE ladder — [RULING], evidence COLLECTED not decided

`own-backend-architecture.md` §2.4 states `tk_emit_c` retires only when ALL THREE hold; the
default-inversion doc §4 already reframes these as a five-rung pin-removal ladder (R0–R5). D2's duty
(from #391) is to make the evidence for those conditions **tracked release-over-release** and to surface
the OPEN thresholds for the owner. **D2 decides none of these.**

### 3.1 The three §2.4 retire conditions, restated against today's tree

1. **K consecutive green releases of the full differential across all targets.** Today: 5/8 targets have
   an executing leg (§1); #2 is addable now (→6/8), #5 executing and #8 are blocked. So condition 1
   cannot even *start* counting until §2.1 lands (#2), #5 gets an executing host, and #8 (N6b) lands.
   **`K` is unspecified — an OWNER FORK (§6 Q1).**
2. **Own-path self-host fixpoint gen1==gen2==gen3 byte-identical through the own backend.** Today the
   fixpoint is proven on the **C** path (`release.yml` gen chain `cmp gen2/teko.c out/teko.c`); the own
   backend does not self-host the whole compiler yet (default-inversion §1: it links via system
   `cc`/cross-gcc, and the own corpus `diff_c_own.sh` is a *subset* of the language the compiler uses).
   This is the bar the default-inversion `--backend=c` pin-list (26 sites) makes explicit and keeps on C
   until met (default-inversion §0, R3).
3. **The M-linker removes the last `cc` dependency (#226).** **[DEFERRED]** — no `src/link/` exists; the
   E-cluster (#392 ELF linker / #393 Mach-O+COFF linker / #394 dynamic) is **not landed** (verified: no
   `src/link/` in tree, no #392/#393/#394 in the merge log; the branches `fix/issue-392/393/394-*` are
   unmerged). Until E1/E2 land, every target still links via `cc`-as-linker (`own-backend-architecture.md`
   §3.5, D-5), so condition 3 is structurally unmeetable and retire is impossible regardless of §1/§2.

### 3.2 [NOW] How evidence is COLLECTED — a release-over-release tracking table

The mechanism this proposal recommends (implementable now, no compiler code): a **C-RETIRE ladder
tracking table** maintained in `DECISION_LOG.md` (the repo's decision-of-record home, consistent with
D37's provenance style), updated by the integrator at each `-beta` seed cut. One row per release, one
column per retire condition, so the owner reads the clock at a glance and rules `K` against real data.

Proposed table shape (the integrator fills it release-over-release):

| Seed | Targets green (exec) | Fixpoint on own path? | M-linker? | Consecutive-green streak | Notes |
|------|----------------------|-----------------------|-----------|--------------------------|-------|
| 0.3.0.21 | 5/8 (#1,#3,#4,#6,#7) | no (C only) | no (E-cluster unlanded) | 0 | D1 flag landed; #2 addable, #5 exec-blocked, #8 N6b-blocked |
| … | … | … | … | … | … |

Rules that make the streak honest (M.3):
- a target counts green ONLY with an **executing** own==C leg (structural-only #5 does NOT count — §2.2);
- the streak RESETS on any target's differential red, or on any own-backend regression the differential
  catches;
- the streak cannot exceed the number of green targets (a 5/8 release can never satisfy an "all-8"
  threshold — the ladder is gated on §1 completeness first).

The R0–R5 pin-removal ladder of `backend-default-inversion.md` §4 is the *sibling* view (which
`--backend=c` pin drops when): R3 (self-host builds) drops on condition 2; R4 (cross-emit) on Phase B+E;
R5 (delete `tk_emit_c`) is the terminal state = all conditions met. **D2's tracking table feeds the
owner's R-rung rulings; it does not pull any rung.**

### 3.3 [RULING] The open thresholds (do NOT decide here) — see §6 Q1–Q3

- **Q1 — what is `K`?** The number of consecutive green releases across all 8 executing targets before
  condition 1 is satisfied.
- **Q2 — order of the default→native inversion vs retire.** `backend-default-inversion.md` (owner
  2026-07-14) flips the *default* to native while pinning C on self-host/release sites; retire (deleting
  `tk_emit_c`) is a later, separate event. Does the owner confirm the inversion **precedes** retire (the
  doc's model: default flips first on the pin-list, retire follows when the fixpoint is green), or should
  they be coupled?
- **Q3 — order of retire vs the M-linker.** §2.4 condition 3 makes retire depend on #226. Confirm retire
  is strictly AFTER E1/E2 (no target silently falling back to `cc`), i.e. R5 is terminal.

---

## 4. The DWARF / PDB scope decision — [RULING] fork, do NOT decide

`own-backend-architecture.md` §2.5 stages debug info: N2–N6 emit **line-table quality only** (symbol
names + line/col, backing the native stack trace via the `.tsym` map `codegen::tk_emit_tsym`,
`project.tks`), with full DWARF/PDB "an explicit later decision collected at N8, gated behind `--g`." The
`LInst.line/col` fields (`src/lir/lir.tks:80-83`) already carry the source positions the richer info
would consume. The VM-retirement work also introduced a `-g`/debug seam intent in `build_cc_argv`
(`vm-retirement.md` §2.2) for the `teko run` native-debug profile — so a `--g`/`-g` axis already has a
home to hang from. D2 collects the decision; it does not rule.

### 4.1 The fork, as two crisp options

**Option A — MVP: stay at line-table / `.tsym` quality (RECOMMEND as the default rung).**
- Emit: symbol names + `line/col` from `LInst`, fed to the `.tsym` map (already written beside the
  binary). No variable-location expressions, no DWARF `.debug_info`/`.debug_line` DIE tree, no PDB.
- **Cost:** ~zero new backend work — the data already exists (`lir.tks:80-83`); the native stack trace
  the `.tsym` backs is already the shipped debug surface.
- **Benefit:** honest stack traces (function + file:line) on panic; matches the C path's current debug
  fidelity (the C path also does not emit rich DWARF by default). Sufficient for the 0.3.x safety-spine
  wave.
- **Loss:** no source-level stepping in gdb/lldb/WinDbg over own-backend binaries; no
  variable-inspection; a debugger sees stripped-ish objects with a symbol table only.

**Option B — full DWARF (ELF/Mach-O) + PDB (PE/COFF) behind `--g`.**
- Emit, when `--g` is passed: DWARF `.debug_info` + `.debug_line` + `.debug_abbrev` (ELF/Mach-O) and a
  PDB / CodeView stream (PE/COFF), **including variable-location expressions** (DWARF `DW_OP_*`
  location lists mapping each `VReg`/spill slot to a register or frame offset per PC-range).
- **Cost:** LARGE and per-format. Variable-location expressions require the register allocator
  (`src/backend/regalloc.tks`) to *emit a location map* (VReg → phys-reg/stack-slot per live range) that
  the DWARF/PDB writer consumes — a new cross-cutting output from regalloc that does not exist today.
  Three new emitters (DWARF for ELF+Mach-O sharing a core, PDB/CodeView for COFF, a genuinely different
  container). Each is a differential surface of its own (a debugger consuming malformed DWARF is a new
  failure class).
- **Benefit:** first-class source debugging of own-backend binaries — stepping, breakpoints, variable
  inspection — the story a systems language eventually owes.
- **Gated on:** the M-linker (E-cluster) laying out `.debug_*` sections / the PDB stream correctly, and
  regalloc's location-map output. So Option B is **[DEFERRED]** in practice — it cannot ship complete
  until Phase E anyway.

### 4.2 Recommendation (law-first, not a ruling)

M.4 (no hidden cost) and M.1 (small/orthogonal) favor **Option A now, Option B behind `--g` as a later
increment** — ship line-table quality with the safety-spine wave, design variable-location DWARF/PDB as
its own post-E-cluster issue. This mirrors the §2.5 staging and the D-3 recommendation in
`own-backend-architecture.md` §5. **But the depth, the timing, and whether `--g` full DWARF is in the
0.3.x LTS scope at all are the OWNER's to rule (§6 Q4).**

---

## 5. Risks + law tensions

1. **"Issues are 100%" vs three uncoverable targets (#5 exec, #8, and the M-linker gate).** #391's
   proposal is "extend the differential across all 8 targets," but #5-executing, #8, and condition-3
   retire are structurally blocked by unlanded deps. **Resolution (M.3):** D2 delivers 100% of the
   *designable + now-implementable* scope (the arm64-Linux leg → 6/8 executing, the structural #5 leg,
   the tracking table, both forks framed) and states the residue as explicitly [DEFERRED] with its
   blocking dep named. The differential-completeness *work item* is honestly "6/8 executing + 1
   structural + 1 deferred," not a fabricated 8/8. This is the DESIGN-AHEAD mandate, not a shortfall.
2. **Double-migration of the env seam.** If D2 migrated `diff_c_own.sh` to `--backend` independently of
   default-inversion's S5, two PRs would edit the same lines → a merge race + a possible window where the
   default flipped but the differential's C leg was mis-pinned (a release-breaking blind spot, per
   default-inversion §5). **Resolution (§2.4):** D2 does NOT touch the seam; S5 owns it. D2's new leg
   uses whichever seam is live when it lands.
3. **Counting a structural pass as an execution pass (#5).** Would falsely advance the retire clock.
   **Resolution (§3.2):** the tracking table counts a target green ONLY with an executing leg; #5 stays
   uncounted until a windows-arm64 executing host exists.
4. **DWARF/PDB scope creep into the safety-spine wave.** Full DWARF+PDB is a multi-issue epic that could
   swallow the wave. **Resolution (§4.2):** recommend Option A now; Option B is a post-E-cluster issue —
   but this is the owner's fork (Q4), not decided here.

**No HALT.** Every tension resolves law-first; the only genuinely open items are the owner FORKS below,
which are *decisions to collect* (exactly #391/#225's charter), not blocking tensions.

---

## 6. Owner forks — RULED (2026-07-15)

Concrete, numbered questions for the owner to rule. Q1–Q3 are the C-RETIRE ladder; Q4 is the DWARF/PDB
scope.

**Owner ruling 2026-07-15 (approved with the integrator's recommendations):**
- **Q1 → `K` = 3.** Three consecutive green beta releases (executing own==C across every wired target) satisfy §2.4 condition 1; the tracking table (§3.2) reports the streak.
- **Q2 → CONFIRMED.** The default flips to `Backend::Native` first (the 26-site `--backend=c` pin-list holds self-host/release/fixpoint on C); `tk_emit_c` retire (R5) is a strictly later event gated on the own-path fixpoint. Inversion precedes retire, decoupled.
- **Q3 → CONFIRMED.** Retire is strictly after the M-linker (E-cluster #392/#393/#394/#226) removes the last `cc`-as-linker on every target — no silent `cc` fallback at retire.
- **Q4 → Option A now.** 0.3.x ships line-table/`.tsym` only. Option B (full DWARF+PDB behind `--g` with variable-location expressions) is deferred to a dedicated post-E-cluster issue; the scope/timing of full `--g` is decided when that issue opens (it does not block 0.3.x).

The original questions are preserved below for the record.

1. **Q1 — `K`: how many consecutive green releases** (full executing own==C differential across all 8
   targets) satisfy §2.4 condition 1 before the retire clock is considered "run out"? (e.g. 3, 5, a
   whole minor wave.) The tracking table (§3.2) will report the streak; `K` is the threshold it is read
   against.
2. **Q2 — inversion vs retire ordering.** Confirm the `backend-default-inversion.md` model: the default
   flips to `Backend::Native` first (with the 26-site `--backend=c` pin-list holding the self-host /
   release / fixpoint chain on C), and `tk_emit_c` retire (R5) is a strictly LATER event gated on the
   own self-host fixpoint — i.e. inversion **precedes** retire, they are not coupled. Yes / adjust?
3. **Q3 — retire vs M-linker ordering.** Confirm `tk_emit_c` retire is strictly AFTER the M-linker
   (E-cluster #392/#393/#394/#226) removes the last `cc`-as-linker dependency on every target (§2.4
   condition 3) — no target may silently fall back to `cc` at retire. Yes / adjust?
4. **Q4 — DWARF/PDB scope + `--g`.** Which rung ships, and when:
   - **A (MVP):** line-table / `.tsym` quality only (symbol names + line/col; native stack trace), no
     variable locations — the recommended default for the 0.3.x wave; OR
   - **B (full):** DWARF (ELF/Mach-O) + PDB (PE/COFF) behind `--g`, INCLUDING variable-location
     expressions (a new regalloc location-map output + three format emitters, gated on the M-linker) —
     as a post-E-cluster issue?
   And: is full `--g` DWARF/PDB **in scope for the 0.3.x LTS** at all, or explicitly deferred past it?

---

## 7. What remains BLOCKED (the DESIGN-AHEAD residue)

- **[DEFERRED] Target #8 wasm-browser** — blocked on the N6b browser JS-import runtime + a node/headless
  differential harness (neither landed). WASI (#7) is the wasm proxy meanwhile.
- **[DEFERRED] Target #5 arm64-Windows executing equality** — blocked on a windows-arm64 CI runner /
  PE emulator. The structural object-validity leg is [NOW].
- **[DEFERRED] §2.4 condition 3 (M-linker) and the R3–R5 pin drops** — blocked on the E-cluster
  (#392/#393/#394/#226), unlanded (no `src/link/`). Retire cannot complete before it.
- **[DEFERRED] Option B DWARF/PDB with variable locations** — blocked on regalloc's location-map output
  + the M-linker's `.debug_*`/PDB section layout; and on Q4.

Everything else — the arm64-Linux executing leg (§2.1), the structural arm64-Windows leg (§2.2), the
retire-ladder tracking table (§3.2), the ritual-leg definition (§2.5), and both owner forks (§6) — is
designed here and implementable the moment the owner rules Q1–Q4. The implementer resumes in minutes:
the differential already exists and passes on 5 targets; the residue is target legs + a tracking table +
four rulings, not new backend machinery.
