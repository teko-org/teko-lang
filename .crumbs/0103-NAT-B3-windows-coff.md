---
seq: 0103
crumb-id: NAT-B3
milestone: M4
gate: "[dry]"
reseed-class: "none"
deps: [NAT-B1]
sources:
  - "docs/design/backend-b3-windows-coff.md:1-60"                      # Win64 ABI + PE/COFF
  - "docs/design/plano-mestre-0.3.1-implementacao.md:288"              # M4 NAT-B3 row
  - "src/backend/abi_win64.tks:1"                                      # Win64 descriptor
  - "src/backend/objfile_coff.tks:350"                                 # emit_coff
  - "src/backend/regalloc_x86.tks:471"                                 # x86 regalloc reused
---

# 0103 · NAT-B3 — Windows x86_64 (Win64 ABI + PE/COFF)

> The x86_64/windows vertical: the Win64 ABI descriptor (with the `shadow_space` field) parameterizing
> A3's allocator + B1's x86 encoder, emitting a PE/COFF object (`emit_coff`) linked by the platform linker.

## Goal

x86_64/windows joins the target set by reusing B1's x86 isel/regalloc/encoder with the Win64 descriptor
(`abi_win64.tks`) swapped in, and a PE/COFF object writer (`emit_coff`, `objfile_coff.tks:350`) in place of
ELF. Win64 differs from SysV64 in the argument registers (`rcx,rdx,r8,r9` + `xmm0..3`), the caller/callee-
saved split, and crucially the **`shadow_space`** — the 32-byte home-space the caller must reserve above
the return address for the callee to spill the register args. This crumb adds the `shadow_space` descriptor
field + its prologue reservation, and the COFF writer. Design-ahead `[dry]` against B1's DECLARED encoder;
`emit_coff` already exists — B3 closes coverage + determinism. **Byte-preserving** on the C route;
reseed-class `none`.

## Where

- `src/backend/abi_win64.tks:1` — the Win64 `AbiDescriptor`: arg registers, caller/callee-saved sets,
  spill scratch, AND the `shadow_space: u32` field (the 32-byte home space) B1's SysV64 descriptor lacks.
- `src/backend/regalloc_x86.tks:471` `regalloc_module_x86` — reused with the Win64 descriptor (the SAME
  allocator body; only the descriptor differs).
- `src/backend/encode_x86_64.tks` — reused; the prologue must reserve `shadow_space` for every call frame
  that makes a call (a Win64 requirement).
- `src/backend/objfile_coff.tks:350` `emit_coff` — the PE/COFF object writer (`EncodedModuleX86`);
  determinism audit (no timestamp, stable symbol/section order).
- `src/build/project.tks:1605` `emit_native_win` — the Win64 dispatch arm; confirm it routes the x86 tail
  with the Win64 descriptor → `emit_coff`.

## How

1. **Add the `shadow_space` descriptor field** (`abi_win64.tks:1`): a `shadow_space: u32 = 32` on
   `AbiDescriptor` (the SysV64/AAPCS64 descriptors set it to 0). The allocator/encoder read it; a non-Win64
   descriptor's 0 keeps the existing frames byte-identical.

```teko
/**
 * WIN64 — the Windows x86_64 ABI descriptor: arg registers (rcx, rdx, r8, r9 GPR / xmm0..3 FPR), the
 * caller/callee-saved split, spill scratch, and shadow_space = 32 (the home space the caller reserves
 * above the return address for the callee to spill its register args — the field SysV64/AAPCS64 set to 0).
 * Parameterizes A3's allocator and B1's x86 encoder unchanged; only the descriptor differs.
 *
 * @since 0.3.1
 */
pub const WIN64: AbiDescriptor
```

2. **Reserve `shadow_space` in the prologue** (`encode_x86_64.tks`): every frame that makes a call reserves
   `shadow_space` bytes above the return address (Win64 requirement); a leaf that makes no call may omit it.
   This is the only encoder delta vs SysV64.
3. **Reuse the x86 isel/regalloc/encoder** with `WIN64`: `select_module_x86`/`regalloc_module_x86`/
   `encode_module_x86` take the descriptor — B3 swaps the descriptor, not the bodies.
4. **PE/COFF object** (`emit_coff:350`): COFF header + sections + symbol table + relocations (the
   `emit_coff_*` helpers already exist, `objfile_coff.tks:35-350`). Determinism: no timestamp, stable
   ordering (RM-C16 precondition).
5. **Link via the platform linker** (`link.exe`/`lld-link`): runtime (`kernel32`/ucrt) resolved as
   undefined externals; no C compiler in the path.

## Rulings & laws

- **Teko-only:** `src/backend/*.tks`; no C twin. Runtime linked as externals.
- **W15 full Javadoc** on `WIN64` + the `shadow_space`-aware prologue + `emit_coff` touches; flatten; no
  `//`.
- **`shadow_space` on the descriptor** (`backend-b3`): SysV64/AAPCS64 keep 0 so their frames stay
  byte-identical — record so a reviewer does not apply the reservation universally.
- **Reuse B1's x86 tail + A3's allocator** — B3 swaps the descriptor + the object writer, it does not fork
  isel/regalloc/encode.
- **Determinism now** (RM-C16 precondition): the COFF object reproduces byte-for-byte.
- **Backend native testing removed (owner 2026-08-18):** the x86_64/windows CI leg exercises this.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  `[dry]` — no emitted-byte change on the C route; sweep `.tkt` after the descriptor widening.

## Fixtures

none — the fixpoint self-build exercises this. The compiler compiles ITSELF to x86_64/windows PE/COFF and
the windows native CI leg links + runs it (owner: "backend native — REMOVER; o CI exercita"). RM-C16's
object-reproducibility fixpoint proves the determinism.

## Gate

`[dry]` — compile + scoped `.tkr` + trivial fixpoint (no emitted-byte change; the object feeds only the
native link). "Green" = the `WIN64` descriptor (with `shadow_space`) drives the reused x86 tail,
`emit_coff` writes a deterministic PE/COFF the platform linker links + runs on x86_64/windows, and the
C-route build is byte-identical. Reseed-class: `none`.

## Deps

`NAT-B1` — verbatim from 000-INDEX (reuses B1's x86 isel/regalloc/encoder + descriptor mechanism).

## Done when

The Win64 descriptor with `shadow_space` parameterizes the reused x86 tail, the prologue reserves the home
space, `emit_coff` writes a deterministic PE/COFF the platform linker links into a runnable x86_64/windows
binary (runtime as externals), and the `[dry]` build is byte-identical.
