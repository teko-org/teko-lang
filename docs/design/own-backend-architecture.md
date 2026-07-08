# Own AOT backend + M-linker — architecture, requirements, and phasing (recon)

**Status:** RECON (doc-only). Sub-PR of the 0.2 wave umbrella (#374, branch
`remodel/own-aot-backend`). Issue **#339**. Base for the design:
`docs/design/memory-unsafe-backend-remodel.md` §4.

> This is a RECON: the deliverable is this architecture + requirements doc plus a phased
> issue-map. It does NOT build the backend — it settles the loose fase-4/Onda-6 stubs
> (#222–#226) into code-grounded implementation issues. Spawning the GitHub issues from §4
> is a follow-up for the integrator; the map here is the plan of record.

---

## 1. Why (the north-star)

The remodel (`memory-unsafe-backend-remodel.md` §4) ratifies building **Teko's own AOT
backend + linker** to leave C-codegen and the external `cc`/linker behind. Three motives, in
priority order:

1. **Toolchain independence.** Today the production path is `codegen::tk_emit_c(prog)` →
   write `<stem>.c` → `run_cc` shells out to `zig cc`/`clang` (`src/build/project.tks:515`,
   `:527`, `:397`), then the system linker produces the executable. Every release depends on a
   foreign C compiler *and* a foreign linker. The own backend + **M-linker** removes both, so
   `teko` is a single self-contained toolchain — the precondition for the bare-metal / OS-in-Teko
   direction (a compiler that needs `cc` cannot bootstrap a kernel).
2. **Cross-compile speed.** The honest verdict is already quantified in
   `docs/design/compile-time-architecture.md` §5: the own backend **wins big on cross-compile**
   (`:438-455` — the current cross path re-runs `zig cc` **six times**, once per Linux artifact;
   the own backend lowers the front-end **once** and emits N cheap machine-code lowerings), and
   wins on independence. It wins only **~2.4% on a local self-build** (`:429-436`; `cc` is 0.69s of
   the ~28s self-build — the cost is the VM gate + coverage, not the C compiler) and **~nothing on
   qemu** (`:457-462`). This doc records that verdict so no one over-attributes local speed to this
   epic: **the win is independence + cross-compile, not single-host wall-clock.**
3. **It obsoletes a live bug class.** The generated `teko.c` carries latent UB that
   `zig cc -O1/-O2` miscompiles (`#283`; `scripts/cross_compile_linux.sh` pins `-O0` to dodge it,
   and a residual x86_64 UB at `src/checker/match.tks:228` still forces `-O0`). No C means no
   `-O2` UB and no `-O0` tax — `memory-unsafe-backend-remodel.md:176-177` records this as
   "obsoleted by the own-backend".

The VM's fate rides along (`memory-unsafe-backend-remodel.md` §4): the noisy VM-vs-native
differential **migrates** to **C-backend-vs-own-backend** (both native, C-backend trusted and
self-hosting) — a *stronger* oracle that validates the new backend exactly when it is born; the
C backend then retires when the own path is trusted. This recon scopes exactly that migration and
the pipeline that makes it real, grounded in the `src/lir/` work N1 (#221) already shipped.

---

## 2. Requirements

### 2.1 Target ISAs / ABIs / object formats

The parity set is anchored to the **current release matrix** (`.github/workflows/release*.yml`;
`scripts/cross_compile_linux.sh`) so the own backend ships exactly the targets already promised:

| # | ISA | ABI | Object format | Where it ships today |
|---|-----|-----|---------------|----------------------|
| 1 | arm64 (AArch64) | AAPCS64 | Mach-O | `macos-arm64` release; dev host |
| 2 | arm64 (AArch64) | AAPCS64 (Linux) | ELF | Linux arm64 glibc + musl |
| 3 | x86-64 | SysV | ELF | Linux x86_64 glibc + musl |
| 4 | x86-64 | Win64 | PE/COFF | `windows-x86_64` release |
| 5 | arm64 | Win64 (ARM64) | PE/COFF | `windows-arm64` release |
| 6 | riscv64 | LP64D | ELF | Linux riscv64 glibc + musl |
| 7 | wasm32 | WASI | Wasm | new (N6a) |
| 8 | wasm32 | Browser (JS-import RT) | Wasm | new (N6b) |

**Three object formats** the backend must emit: **ELF** (Linux, riscv64), **Mach-O**
(macOS/arm64), **PE/COFF** (Windows). Wasm is its own container. The Mach-O emitter must not
regress the `__TEXT,__info_plist` section `run_cc` writes today (`src/build/project.tks:429-437`,
plist assembled at `:444` — Finder/`mdls`/`Get Info` metadata; plain text, no XML metacharacters).

### 2.2 Reference / parity target

**The reference target is arm64 Mach-O (target #1)** — the dev host (Apple Silicon) and the CI
lane with the most RAM (macOS runs the coverage/gate per the native-test-gate ruling). N2 (#222)
reaches full type/control parity with `tk_emit_c` on this one target *first*; N3–N5 (#223) are
ports that reuse the whole LIR + lowering + regalloc frame and swap only isel/encoder/objfile.

### 2.3 The differential gate (VM == C-native == own-native)

The correctness bar is a **3-way differential** built on the four oracles that already exist:

- **VM** (`teko run`) — the language oracle (retiring, but kept until C-vs-own is trusted).
- **C-native** — `tk_emit_c` → `run_cc` → binary (the trusted, self-hosting production path).
- **own-native** — the new backend's binary.
- **LIR interpreter** (`src/lir/lir_interp.tks`) — the "FOURTH oracle" (`:1-8`): runs the LIR
  subset directly, no `cc`/QEMU, validating the *lowering* independently of any machine code.

The gate grows in two moves: (a) N2 births **C-native == own-native** at the reference target,
diffing exit code + stdout over `examples/regressions/` (the corpus `scripts/diff_vm_native.sh`
already drives — extend it with an own-backend leg, `:1-25`); (b) N8 (#225) makes it 3-way across
all targets. The LIR-dump (`src/lir/lir_print.tks`, deterministic, floats as exact IEEE bits,
`:1-7`) is the *pre-machine* diff spine — a lowering divergence is caught before any encoder runs.

### 2.4 C-backend retire criteria

`tk_emit_c` retires only when **all** hold (decision collected at N8, #225): (1) own-native
passes the full 3-way gate on every target for K consecutive green releases; (2) the own backend
self-hosts (gen1==gen2==gen3 byte-identical through the own path, the fixpoint bar); (3) the
M-linker (#226) removes the last external-linker dependency, so no target silently falls back to
`cc`. Until then C-native stays the trusted side of the differential. **Do not retire the VM
differential before C-vs-own is in place** (`memory-unsafe-backend-remodel.md:187-189`).

### 2.5 DWARF / debug scope

`LInst` already carries `line`/`col` (`src/lir/lir.tks:80-83`) propagated from the `TExpr`, feeding
the `.tsym` map (`codegen::tk_emit_tsym`, written beside the binary at `project.tks:532`) and future
debug info. **Debug scope is staged, not day-one:** N2–N6 emit line-table-quality info only (enough
for the native stack-trace the `.tsym` already backs); full DWARF (ELF/Mach-O) and PDB (PE/COFF)
are an explicit later decision collected at N8 (#225), gated behind `--g`. The MVP ships symbol
names + line/col, not variable-location expressions.

---

## 3. Architecture

### 3.1 The pipeline and where N1 stopped

```
TAST  ──lower──▶  LIR  ──isel──▶  MInst  ──regalloc──▶  MInst'  ──encode──▶  object bytes  ──M-linker──▶  executable
(checker)        (SSA-lite)      (per-ISA)  (linear-scan)         (per-ISA)     (ELF/Mach-O/PE)   (#226)
                     │
                     └──lir_print (diff spine) · lir_interp (4th oracle)
```

**N1 (#221, CLOSED) already delivers the front half:**

- **`src/lir/lir.tks`** — the LINEAR IR: SSA-lite with explicit **block-args** (NOT phi, NOT a
  stack machine, `:6-9`), 3-address, closed operator enums. `LType` (`:18-23`) is the machine
  class (`I8..I128`, `F16/F32/F64`, `Ptr`, `Void`); **signedness lives on the opcode**
  (`IDivS`/`IDivU`, `LBinOp` `:30-35`), never on the type — exactly like x86. Symbols arrive
  **already mangled** (`:10-11`, `LCall.symbol`); the LIR never re-mangles. `LModule`/`LFunc`/
  `LBlock`/`LInst` at `:80-97`. The comparison/bitwise/shift opcodes and `LJump`/`LBranch` are
  **declared now, first exercised at N2** (`:70-75`).
- **`src/lir/lower.tks`** — TAST → LIR for the **N1 reference subset** (`:5-16`): int/float
  literals, arithmetic/unary, locals, `to` casts, direct calls (Teko-Teko + `exit`/`panic`),
  `let`, `return`, expr-statements. It mirrors the SHAPE of `emit_expr_ctx`/`emit_stmt` in the C
  backend — same recursion, different result (append `LInst` + return a `VReg` instead of appending
  text). Everything else **honest-stops with a NAMED error** — this is the N2 frontier (§3.2).
- **`src/lir/lir_print.tks`** — the deterministic textual LIR printer (`:1-7`): the diff spine.
- **`src/lir/lir_interp.tks`** — the LIR interpreter oracle (`:1-8`): the fourth oracle.

The seam where "the semantic type dies and the machine begins" is `ltype_of`
(`src/lir/lower.tks:48-58`): every address-shaped type (`str`/`slice`/`Ref`/`ptr`/`Named`/
`Optional`/`Variant`/`Func`) collapses to `Ptr`.

### 3.2 The N2 coverage frontier (the honest-stops to close)

The current lowering hard-errors — by design, M.1/M.3 (`src/lir/lower.tks:15-16`) — on everything
past the N1 subset. These are the exact sites N2 fills, one case at a time, without rewriting the
walk:

- `lower_expr` catch-all `_ =>` (`:306`) — every non-{number,var,binary,unary,cast,call} expr.
- `lower_stmt` catch-all `_ =>` (`:447`) — every non-{binding,return,exprstmt} statement (`if`,
  `match`, `loop`, `defer`, ...).
- `lower_call` **interface dispatch** (`:381`, "interface dispatch not yet lowered (N2)") — **the
  reviewer's "hard-errors on iface dispatch"; it is the documented coverage boundary, not a bug.**
- `lower_call` **closure call** (`:380`).
- `lower_var` **function-as-value** (`:326`).

N2 also grows `LModule` past functions-only (`lir.tks:96` "Structs/globals/rodata enter in N2"):
**string/float literals need a rodata section**, aggregates need struct layout, and top-level
consts need globals. The LIR interpreter and printer grow one case in lockstep with each closed
honest-stop (their scope notes at `lir_interp.tks:9-13`, `lir_print.tks:1-7`).

### 3.3 Instruction selection (isel)

Per-ISA modules, `LIR → MInst` (a thin machine-instruction IR keyed by target). N1's design note
(`lir.tks:6-9`) fixes the strategy: **a register IR serves both register targets (linear-scan) and
Wasm (its own stackify)**. isel is a per-block tree/peephole match over `LOp`; because signedness is
on the opcode (`IDivS`/`IDivU`) and widths are on `LType`, the selector reads the op+type and needs
no re-analysis. Proposed home: `src/backend/isel_<isa>.tks` (`isel_arm64.tks` first). Fat-pointer
ops (str/slice as `{ptr,len}`) are the first N2 additions that isel must lower to two-register
sequences.

### 3.4 Register allocation

**Linear-scan over the SSA-lite block-args form** (`lir.tks:6-9`): each value is defined once, merges
pass block-args on the edge — so live ranges are contiguous and phi-elimination is a copy on the
edge, not a separate pass. Target-independent core (`src/backend/regalloc.tks`) parameterized by the
per-ISA register file + ABI (caller/callee-saved, argument registers, the red zone). AAPCS64 /
SysV / Win64 / LP64D each contribute a small register-file descriptor; the allocator itself is shared.

### 3.5 Object emission

Per-format emitters, `MInst' → object bytes`: `objfile_elf.tks`, `objfile_macho.tks`,
`objfile_coff.tks`. Each writes sections (`.text`, `.rodata`, `.data`, `.bss`), a symbol table, and
**relocations** for calls/data-refs the M-linker resolves. The Mach-O emitter must reproduce the
`__TEXT,__info_plist` section (`project.tks:429-444`). During N2–N6 the objects are handed to the
**system linker** (`cc`-as-linker) so a runnable binary exists before the M-linker lands; #226 then
swaps in the own linker and removes that last external dependency.

### 3.6 The M-linker

A minimal static linker per format (#226, `src/link/`): read the emitted objects (+ the runtime
object, §5.2), **resolve relocations**, lay out sections, write the executable header (ELF `Ehdr`+
`Phdr`, Mach-O load commands, PE headers). Static-first; **dynamic linking is optional/deferred**
(`memory-unsafe-backend-remodel.md` §4; the musl artifacts are already fully static, so static
covers the promised matrix). It shares the reachability model with the packaging pre-linker
(#180/#220) — the same "which symbols are live" walk.

### 3.7 Slotting beside the C-emit path (the backend-selection flag)

The selection point is **`backend(...)` in `src/build/project.tks:465`** (and the twin
`backend(...)` in `src/driver.tks:172`). Today the `Artifact::Binary` arm unconditionally calls
`codegen::tk_emit_c` → `run_cc` (`:515-529`). N7 (#225) adds a `--backend={c,native}` flag threaded
through the `Manifest`/argv parse (`project.tks:901-977` already skips `-o`/`--no-test`) into
`backend()`, which dispatches:

```teko
/**
 * emit_binary — produce the native executable for a checked program, selecting the
 * code path from the resolved backend choice. The C path is the trusted default until
 * the own path clears the 3-way differential (#225); `native` opts into the own AOT
 * backend + M-linker.
 *
 * @param dir the project directory, for diagnostics
 * @param stem the output artifact stem
 * @param out_dir the resolved output directory
 * @param prog the checked, typed program
 * @param m the resolved manifest, carrying the backend selection
 * @return the process exit status (0 on success)
 */
fn emit_binary(dir: str, stem: str, out_dir: str, prog: checker::TProgram, m: Manifest) -> i32 {
    if m.backend == Backend::Native { return emit_native(dir, stem, out_dir, prog, m) }
    emit_c_native(dir, stem, out_dir, prog, m)
}
```

`emit_c_native` is the current body of `backend()` (lines `:515-534`, extracted verbatim);
`emit_native` runs `lir::lower` → isel → regalloc → encode → objfile → M-linker. The default stays
`Backend::C` until retirement (§2.4), so the flag is purely additive and FIXPOINT-preserving until
flipped.

---

## 4. Phasing — the issue-map

This settles the fase-4/Onda-6 stubs (#222 N2, #223 N3–N5, #224 Wasm, #225 N7+N8, #226 M-linker)
into ordered, code-grounded implementation issues re-homed into the 0.2 wave. **These are a PLAN;
the integrator spawns the GitHub issues.** Every compiler-touching issue owes the full ritual
(gate both engines · paranoid · differential · parity · fixpoint). Verification names the gate leg
that PROVES each issue.

### Phase A — reference-target machine backend (settles #222)

**A1 · N2a — close the lowering coverage frontier (control + fat-pointer + aggregates)**
- Scope: close the honest-stops in §3.2 — `if`/`match`/`loop`/`defer` (`LJump`/`LBranch` +
  block-args), interface dispatch (vtable load + indirect call), closures + function-as-value,
  struct/slice/str fat-pointer lowering, and `LModule` rodata/globals/struct-layout. Grow
  `lir_interp` + `lir_print` one case per feature.
- Depends on: N1 (#221, CLOSED).
- Files: `src/lir/lower.tks`, `src/lir/lir.tks` (LModule rodata/globals), `src/lir/lir_interp.tks`,
  `src/lir/lir_print.tks`; tests `src/lir/lower_test.tkt`, `lir_interp_test.tkt`, `lir_test.tkt`.
- Verifies via: **LIR-interpreter oracle** (exit code parity vs VM over the subset) + golden
  LIR-dump diff. No machine code yet — proves lowering completeness independently.

**A2 · N2b — arm64 instruction selection**
- Scope: `LIR → MInst` for AArch64; peephole/tree match over the closed `LOp` set; fat-pointer
  two-register sequences.
- Depends on: A1.
- Files: new `src/backend/minst.tks` (machine-IR types + printer), `src/backend/isel_arm64.tks`;
  tests `src/backend/isel_arm64_test.tkt`.
- Verifies via: golden MInst-dump tests + isel-over-interp equivalence (selected form re-interpreted
  matches the LIR interp).

**A3 · N2c — linear-scan register allocation (shared core + AAPCS64 descriptor)**
- Scope: target-independent linear-scan over block-args; AAPCS64 register file + ABI descriptor
  (arg regs x0–x7/v0–v7, callee-saved, spill slots).
- Depends on: A2.
- Files: new `src/backend/regalloc.tks`, `src/backend/abi_aapcs64.tks`; tests `regalloc_test.tkt`.
- Verifies via: `regalloc_test.tkt` (no live-range overlap in a physical reg) + post-alloc
  interp-equivalence.

**A4 · N2d — arm64 encoder + Mach-O object + link via system ld (births C-vs-own differential)**
- Scope: `MInst' → AArch64 machine bytes`; Mach-O object emitter (sections, symtab, relocations,
  `__TEXT,__info_plist` parity); link via `cc`-as-linker; wire `emit_native` behind the (still
  unflagged) native path for the reference target only.
- Depends on: A3; the runtime-link decision (§5.2).
- Files: new `src/backend/encode_arm64.tks`, `src/backend/objfile_macho.tks`,
  `src/build/project.tks` (`emit_native` skeleton); tests + `examples/regressions/` reused as the
  differential corpus.
- Verifies via: **C-native == own-native** exit-code + stdout diff over `examples/regressions/` at
  arm64 Mach-O (the differential's birth). #222 CLOSES here.

### Phase B — the remaining native targets (settles #223, one sub-PR per ABI family)

**B1 · N3 — x86-64 SysV + ELF (Linux)**
- Scope: `isel_x86_64.tks`, `abi_sysv.tks`, `encode_x86_64.tks`, `objfile_elf.tks`. Reuses A1/A3
  core whole.
- Depends on: A4.
- Files: new `src/backend/isel_x86_64.tks`, `abi_sysv.tks`, `encode_x86_64.tks`,
  `src/backend/objfile_elf.tks`; tests per module.
- Verifies via: C-native == own-native on Linux x86_64 (glibc + musl) in CI; replaces one `zig cc`
  leg of `cross_compile_linux.sh`.

**B2 · N4 — riscv64 LP64D + ELF**
- Scope: `isel_riscv64.tks`, `abi_lp64d.tks`, `encode_riscv64.tks` (reuse `objfile_elf.tks`).
- Depends on: B1 (shares the ELF emitter).
- Files: new `src/backend/isel_riscv64.tks`, `abi_lp64d.tks`, `encode_riscv64.tks`.
- Verifies via: C-native == own-native on riscv64 (qemu leg) — this is the family the current
  toolchain fights hardest (`cross_compile_linux.sh` notes the incomplete riscv64 glibc), so it is
  the clearest independence win.

**B3 · N5 — Windows x86_64 + arm64 (Win64 ABI + PE/COFF)**
- Scope: `abi_win64.tks` (shadow space, differing arg regs), `objfile_coff.tks`; reuse the
  x86_64/arm64 encoders from A4/B1.
- Depends on: B1 (x86_64 encoder), A4 (arm64 encoder).
- Files: new `src/backend/abi_win64.tks`, `src/backend/objfile_coff.tks`.
- Verifies via: C-native == own-native on `windows-x86_64` + `windows-arm64` release lanes.

### Phase C — Wasm (settles #224)

**C1 · N6a + N6b — Wasm (WASI + Browser), the stackifier + JS-import runtime**
- Scope: the register-IR → Wasm **stackify** pass (`lir.tks:6-9` reserves this route);
  `objfile_wasm.tks` module/section writer; WASI RT binding (N6a) + the Browser JS-import variant of
  `teko_rt` + JS glue (N6b). This is AOT (distinct from the interpreted `teko run` dev/WASM path).
- Depends on: A1 (the target-independent LIR) — NOT A2/A3 (Wasm has its own selection/no regalloc),
  so C1 can run in parallel with Phase B.
- Files: new `src/backend/stackify.tks`, `src/backend/objfile_wasm.tks`, a Wasm variant of
  `src/runtime/teko_rt.tks` + `extensions/`/`web` glue.
- Verifies via: C-native (or VM) == own-wasm under a wasmtime/node harness over the corpus.

### Phase D — 3-way gate + backend flag (settles #225)

**D1 · N7 — the `--backend={c,native}` selection flag**
- Scope: thread the flag through argv parse + `Manifest` into `backend()`/`emit_binary` (§3.7);
  default `Backend::C` (additive, FIXPOINT-preserving).
- Depends on: A4 (needs a working native path to select).
- Files: `src/build/project.tks` (parse + `emit_binary`/`emit_native`), `src/driver.tks`,
  `src/build/manifest.tks` (`Backend` field); tests in `project` + `manifest` `.tkt`.
- Verifies via: `manifest`/`project` unit tests + fixpoint unchanged (flag defaults to C).

**D2 · N8 — the 3-way differential CI (VM == C-native == own-native) + retire/DWARF decisions**
- Scope: extend `scripts/diff_vm_native.sh` with the own-backend leg across all 8 targets; collect
  the **C-RETIRE** criteria evidence (§2.4) and the **DWARF/PDB** scope decision (§2.5) as owner
  forks. Does NOT retire anything.
- Depends on: B1, B2, B3, C1 (all targets present).
- Files: `scripts/diff_vm_native.sh`, `.github/workflows/*.yml` (matrix legs), `DECISION_LOG.md`.
- Verifies via: the 3-way gate green on every target for the retirement-clock to start. #225 CLOSES
  here.

### Phase E — the M-linker (settles #226)

**E1 · L1 — ELF static linker**
- Scope: read own objects + runtime object, resolve relocations, lay out sections, write ELF
  `Ehdr`/`Phdr`; replace `cc`-as-linker on the ELF targets.
- Depends on: B1/B2 (ELF objects to link).
- Files: new `src/link/linker.tks` (format-independent core: reachability, symbol resolution),
  `src/link/link_elf.tks`; tests.
- Verifies via: own-linked ELF binary passes the 3-way gate on Linux x86_64/arm64/riscv64 — the
  first target with **zero** external toolchain.

**E2 · L2 — Mach-O + PE/COFF static linkers**
- Scope: `link_macho.tks` (load commands, `__info_plist` carry-through), `link_coff.tks`.
- Depends on: E1 (shared core), A4/B3 (Mach-O/COFF objects).
- Files: new `src/link/link_macho.tks`, `src/link/link_coff.tks`.
- Verifies via: own-linked binaries pass the 3-way gate on macOS + Windows — **full toolchain
  independence achieved** (the §2.4 retire precondition #3).

**E3 · L3–L4 — dynamic linking (optional/deferred)**
- Scope: dynamic-section / import-table support if a shared-library artifact needs it. Deferred —
  the static path covers the promised matrix (musl artifacts are already fully static).
- Depends on: E2.
- Files: extends `src/link/link_*.tks`.
- Verifies via: a dynamic-link smoke fixture; kept out of the core gate until a real consumer needs it.

### Ordering summary

```
#221 (done) ─▶ A1 ─▶ A2 ─▶ A3 ─▶ A4 [#222]
                                  ├─▶ B1 ─▶ B2
                                  │        └─▶ B3   [#223]
                                  └─▶ C1           [#224]  (∥ Phase B, needs only A1)
                       B1,B2,B3,C1 ─▶ D2 ; A4 ─▶ D1   [#225]
                                B1/B2 ─▶ E1 ─▶ E2 ─▶ E3 [#226]
```

Critical path to the first fully-independent binary: **A1 → A2 → A3 → A4 → B1 → E1**.

---

## 5. Open decisions

Each is framed against the Constitution laws (M.0 self-hosting · M.1 small/orthogonal ·
M.2 deterministic · M.3 honest · M.4 no-hidden-cost · M.5 one-way-to-do-it). None is an
unresolvable tension that HALTs this recon; each has a law-first recommendation and a point where
the owner's ruling is collected. Flagged for an explicit ruling: **D-1 (REPL), D-2 (runtime link),
D-3 (C-retire timing), D-4 (reference target).**

### D-1 · The REPL's fate (owner fork, from remodel §4)
An interactive REPL is naturally an interpreter; on an AOT-only world it becomes either
compile-and-run-per-line or a retained minimal tree-walker. **Law tension:** keeping the VM alive
just for the REPL violates M.1 (two evaluators for one language) and M.5 (two ways to run code);
compile-per-line honors both but pays latency (M.4 visible cost, acceptable for an interactive
tool). **Recommendation:** compile-and-run-per-line on the own backend; retire the VM entirely. Not
blocking (the REPL is not on the backend critical path) — collected at D2/#225 alongside VM
retirement.

### D-2 · The runtime-link path (blocks A4 — genuine fork)
`teko_rt` must reach the final binary. Three options:
1. **Keep `src/runtime/teko_rt.c` compiled by `cc`, link its `.o`.** Simplest, but keeps a `cc`
   dependency alive — defeats the independence goal at exactly the last mile (M.3 tension: "own
   backend" that still needs `cc` is not honest independence).
2. **Lower `src/runtime/teko_rt.tks` through the own backend.** The `.tks` runtime asset already
   exists. This is the M.0-pure answer (the runtime rides the same pipeline), but the runtime uses
   the lowest-level ops (raw memory, syscalls) that stress the backend earliest.
3. **Ship a precompiled `teko_rt.o` per target** in the toolchain bundle. Independence at
   distribution time, but a per-target prebuilt blob is an M.3 half-truth and a maintenance tax.
**Recommendation:** option **(1) for bootstrap** (A4–B3 link the C-built runtime object so the
backend can be validated fast), converging to option **(2)** once E1 lands and the own backend can
lower `teko_rt.tks` itself — that is the point where independence becomes real (§2.4 precondition
#3). Owner ruling wanted on whether (2) is a hard exit criterion or (3) is acceptable for some
target.

### D-3 · C-backend retire timing + DWARF depth (owner fork, collected at N8/#225)
When exactly to delete `tk_emit_c`, and how much debug info to emit. **Law tension:** M.0
(self-hosting must never break) argues for a long overlap where C-native stays the trusted
differential side; M.5 (one way) argues against carrying two backends indefinitely. **Recommendation:**
retire on the §2.4 criteria (K green releases + own self-host fixpoint + M-linker landed), not a
date; ship line-table debug info first, full DWARF/PDB behind `--g` as a later increment. Collected
at D2/#225, not decided here.

### D-4 · Reference target — arm64 Mach-O vs x86_64 ELF
§2.2 picks **arm64 Mach-O** (dev host + highest-RAM CI lane). **Counter-tension:** x86_64 ELF is the
majority of the release matrix and the historical UB battleground, so parity there arguably matters
more. **Recommendation:** keep arm64 Mach-O as the N2 reference (fastest local iteration → shortest
feedback loop, honoring M.4 for the developer), because N3 (x86_64) reuses the entire shared core and
follows immediately. If the owner prefers x86_64-first, only A2/A4's target parameter changes — the
plan is target-agnostic by construction.

### D-5 · M-linker vs system-linker sequencing (resolved law-first, recorded)
The stubs place #226 last (depends on #225), yet a runnable own-backend binary needs *a* linker at
A4. **Resolution (no HALT):** N2–N6 link via `cc`-as-linker (the system linker), so the backend is
validated before the M-linker exists; #226 then removes that last external dependency. This means
**"toolchain independence" is only fully achieved at E1/E2, not at #222** — recorded here so no one
claims independence prematurely (M.3). This is a sequencing decision, not an open question.
