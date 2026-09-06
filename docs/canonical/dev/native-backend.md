# The Native Backend

Teko's own AOT backend emits **object-file bytes directly** — no host `cc`, no textual C
intermediate — for every platform the toolchain ships. `teko build` lowers the checked program
(the TAST) straight to a native object and links it against the pre-built `teko_rt` runtime
object using the platform linker.

## Target matrix

| OS | Arch | Object format | ABI / calling convention |
|---|---|---|---|
| Linux | x86_64 | ELF64 | System V AMD64 |
| Linux | arm64 | ELF64 | AAPCS64 |
| macOS | arm64 (Apple Silicon) | Mach-O 64 | AAPCS64 (Apple variant — `x18` reserved) |
| Windows | x86_64 | PE/COFF | Microsoft x64 calling convention |

Each row is a distinct instruction-selector + encoder + object-writer combination — the
architectures diverge in registers/ABI, the object formats diverge in section/relocation/symbol
layout. The design deliberately factors this as **one shared IR + N thin per-target encoders**,
not N independent backends: everything from LIR lowering through register allocation is shared
code, and only instruction selection, byte encoding, and object writing are per-target.

(WebAssembly is intentionally out of the tree today — an earlier partial implementation was
removed wholesale by ruling rather than kept as a known-incomplete stub, on the basis that a
half-working target that *looks* supported is worse than no target; wasm returns as a
from-scratch design when its time comes, not as a patch on the removed code.)

## Pipeline stages (mirrors `src/backend/`)

| Stage | Files (`src/backend/`) | Responsibility |
|---|---|---|
| **LIR** | (`src/lir/lower.tks`) | TAST → architecture-agnostic low-level IR — see `architecture-pipeline.md` |
| **Instruction selection** | `isel_x86_64.tks`, `isel_arm64.tks` | LIR → concrete per-architecture instructions, via tree-pattern tiling ("maximal munch") |
| **Register allocation** | `regalloc.tks`, `regalloc_x86.tks` | Linear-scan allocation over LIR virtual registers, shared machinery across architectures |
| **ABI lowering** | `abi_sysv64.tks`, `abi_win64.tks`, `abi_aapcs64.tks` | Per-target calling-convention detail: argument classification (registers vs. stack slots), the SysV/Win64 register-count difference, the AAPCS64 Apple variant |
| **Encoding** | `encode_x86_64.tks` (+`_consts.tks`), `encode_arm64.tks` (+`_consts.tks`) | Concrete instruction → machine bytes + relocations. arm64 is fixed-width (4 bytes/instruction, table-driven); x86_64 is variable-width (REX/ModRM/SIB prefixes) |
| **Machine instructions** | `minst.tks`, `minst_x86.tks` | The per-architecture machine-instruction representation isel emits and encoding consumes |
| **Object writers** | `objfile_elf.tks`, `objfile_macho.tks`, `objfile_coff.tks` | Bytes + symbol table + relocations → the target's native object-file format |
| **Archive writers** | `objfile_ar.tks`, `objfile_ar_macho.tks`, `objfile_ar_coff.tks` | Static-library (`.a`/`.lib`) archive containers per platform, for the `static` artifact kind |
| **Debug info** | `dwarf.tks` | DWARF line/info/abbrev emission — see `debugger.md` |

## Relocation and linkage against `teko_rt`

`teko_rt.c` is compiled **once per target** (a `teko_rt.o` per OS/architecture, a build
artifact, not something generated per-program). The object the native backend emits for a
user's program references `teko_rt` symbols (`tk_alloc`, `tk_region_new`, `tk_panic_div0`, …)
as **external, unresolved symbols** with the platform's normal relocation kind
(`R_X86_64_PLT32` / `R_AARCH64_CALL26` / the COFF equivalent) — exactly the shape `cc` already
produces when linking generated C against the same runtime object. No change to `teko_rt`'s
ABI is required for this: the native backend generates the same calling-convention sequence C
implicitly did.

Teko-to-Teko calls (a function defined in the same program) use the same
namespace-qualified mangling the C path already established
(`teko::checker::type_eq` → `teko__checker__type_eq`) — the native backend inherits the
mangler rather than reinventing it.

## Linking

The system linker (`ld`/`ld64`/`lld-link`, invoked via the platform's `cc`/`clang` driver) is
the production linking path across every target — this isolates the highest-value, most
correctness-sensitive work (code generation) from the lowest-urgency work (linking), which the
project treats as a solved problem the system already does well.

An own, from-scratch linker is a deliberately later-staged, forward-looking axis (a minimal
static ELF linker first, extended to Mach-O and PE/COFF, dynamic linking evaluated last if it
is ever needed at all) — it exists to eventually retire the dependency on the host toolchain
entirely, once every native target is otherwise solid; it does not gate anything documented
above it. Until the own linker lands, cross-compiling to a target whose native linker cannot
run on the build host (e.g., producing a Windows PE from a Linux build host) stops at "emit and
validate the object," rather than reaching for a foreign-platform toolchain (mingw) as a
workaround — cross-compiling is a fully first-class target once every host builds its own
targets natively; it is never achieved by depending on another platform's cross-toolchain in
the interim.

## Verification

Every native-backend claim is checked against an independent oracle, not against itself:
**the transpile-to-C path** — the same TAST, lowered to C and compiled by the host `cc`,
run and compared byte-for-byte / exit-code-for-exit-code against the native object's
behavior on the same inputs.

A native-backend regression is therefore always distinguishable from a front-end regression:
if the C path disagrees with the native object on a program both were built from, the bug is
in encoding/selection/register-allocation, not in the checker or the lowering that feeds both
paths.

## Debug information

`dwarf.tks` emits real DWARF (`.debug_abbrev`/`.debug_info`/`.debug_line`) alongside the object,
sufficient for a standard debugger (or Teko's own `tdb`, see `debugger.md`) to map machine
addresses back to `file:line` and unwind a call stack — this is deliberately a thin "line +
frame" layer, not a full variable/type DWARF surface; richer debug info is a later increment
layered on the same emission path, not a redesign of it.
