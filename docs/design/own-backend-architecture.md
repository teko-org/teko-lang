# Own AOT backend + M-linker — architecture, requirements, and phasing (recon)

**Status:** RECON (doc-only, WIP). Sub-PR of the 0.2 wave umbrella (#374, branch
`remodel/own-aot-backend`). Issue **#339**. Base for the design: `docs/design/memory-unsafe-backend-remodel.md` §4.

> This is a RECON: the deliverable is this architecture + requirements doc plus a phased
> issue-map. It does NOT build the backend — it settles the loose fase-4/Onda-6 stubs
> (#222–#226) into code-grounded implementation issues.

## 1. Why (the north-star)
_(to fill: toolchain independence — drop external `cc`/linker; cross-compile speed; the
bare-metal / OS-in-Teko direction. Consumes remodel §4.)_

## 2. Requirements
_(to fill: target ISAs/ABIs (arm64 AAPCS64, x86-64 SysV + Win64, riscv64), object formats
(ELF/Mach-O/PE-COFF), the reference target for parity, the C-backend retire criteria,
DWARF/debug scope, the 3-way differential gate.)_

## 3. Architecture
_(to fill: IR/lowering seam from the typed AST; instruction selection; register allocation;
object emission; the M-linker (relocations, sections, dynamic optional); how it slots beside
the existing C-emit path behind a backend-selection flag.)_

## 4. Phasing — the issue-map
_(to fill: turn #222 (N2 type/control coverage), #223 (N3–N5 targets), #224 (Wasm),
#225 (N7+N8 3-way gate + backend flag), #226 (M-linker) into ordered, code-grounded
implementation issues re-homed into the 0.2 wave.)_

## 5. Open decisions
_(to fill.)_
