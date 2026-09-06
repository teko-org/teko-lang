---
seq: 0101
crumb-id: NAT-B1
milestone: M4
gate: "[dry]"
reseed-class: "none"
deps: [NAT-A3]
sources:
  - "docs/design/backend-b1-x8664.md:1-60"                             # x86-64 SysV + ELF
  - "docs/design/plano-mestre-0.3.1-implementacao.md:286"              # M4 NAT-B1 row
  - "src/backend/isel_x86_64.tks:803"                                  # select_module_x86
  - "src/backend/regalloc_x86.tks:471"                                 # regalloc_module_x86
  - "src/backend/encode_x86_64.tks:984"                                # encode_module_x86
  - "src/backend/objfile_elf.tks:776"                                  # emit_elf
---

# 0101 · NAT-B1 — x86-64 SysV ABI + ELF object

> The x86-64/linux vertical: SysV64 ABI descriptor + x86 machine-IR (`minst_x86`) + x86 isel
> (`isel_x86_64`) + x86 regalloc (`regalloc_x86`) + x86 encoder (`encode_x86_64`), emitting an ELF object
> (`emit_elf`, the writer reused across arches) linked by the system `ld`.

## Goal

The x86-64/linux native leg — the arch the self-build's primary CI legs run on. It reuses A1's
target-independent `LModule` and A3's target-independent linear-scan allocator (parameterized by the
SysV64 descriptor, `abi_sysv64.tks`), adding the x86-specific isel/regalloc/encode tail
(`select_module_x86` `:803`, `regalloc_module_x86` `:471`, `encode_module_x86` `:984`) and reusing the ELF
writer (`emit_elf`, `objfile_elf.tks:776`). All four already exist — B1 closes their coverage to A1's full
`LOp` set and makes the ELF object deterministic. Design-ahead `[dry]` against A3's DECLARED shape;
**byte-preserving** on the C route; reseed-class `none`.

## Where

- `src/backend/abi_sysv64.tks` — the SysV64 `AbiDescriptor` (arg registers, caller/callee-saved, spill
  scratch) parameterizing A3's allocator for x86-64.
- `src/backend/minst_x86.tks` — the x86 machine-IR (`MInstX86`/`MFuncX86`/`RelocX86`) the x86 tail uses.
- `src/backend/isel_x86_64.tks:803` `select_module_x86` — `LIR → MInstX86` for the full `LOp` set.
- `src/backend/regalloc_x86.tks:471` `regalloc_module_x86` — linear scan over `MFuncX86` with the SysV64
  descriptor (the SAME allocation strategy as A3, x86 register file).
- `src/backend/encode_x86_64.tks:984` `encode_module_x86` — `MInstX86`→x86-64 bytes + `RelocX86` records.
- `src/backend/objfile_elf.tks:776` `emit_elf` — the ELF writer (`EncodedModuleX86` + `DwarfSink`),
  reused; determinism audit.

## How

1. **SysV64 descriptor** (`abi_sysv64.tks`): arg registers (`rdi,rsi,rdx,rcx,r8,r9` GPR / `xmm0..7` FPR),
   caller/callee-saved sets, spill scratch — the descriptor A3's allocator consumes for x86-64.
2. **Cover the full `LOp` set in x86 isel** (`select_module_x86:803`): each `LOp` to `MInstX86` with SysV64
   arg/result/ret pins; fat values as two scalars (same as arm64). Address materializations emit `RelocX86`
   (PC-relative for text, absolute/`R_X86_64_64` for rodata).
3. **x86 regalloc** (`regalloc_module_x86:471`): the same linear-scan body over the x86 register file +
   SysV64 descriptor.
4. **x86 encoder** (`encode_module_x86:984`): `MInstX86`→bytes, ModRM/SIB/REX prefixes from the fixed x86
   encoding consts (`encode_x86_consts.tks`), `RelocX86` records collected.
5. **ELF object** (`emit_elf:776`): the reused writer emits `.text`/`.rodata` + symbols + `x86_reloc_reqs`
   + DWARF. Determinism: no timestamp, stable symbol/section order, no abs path (RM-C16 precondition).
6. **Link via system `ld`**: runtime as undefined externals; no C compiler in the path.

```teko
/**
 * encode_module_x86 — encode an allocated x86-64 MModuleX86 to machine bytes: each MInstX86 emits its
 * ModRM/SIB/REX-prefixed encoding from the fixed x86 tables, address sites emit RelocX86 records (PC-rel
 * for text, absolute for rodata), rodata/globals laid out for emit_elf. Deterministic (no timestamp,
 * stable ordering) so the ELF object reproduces byte-for-byte (RM-C16 fixpoint).
 *
 * @param abi  the SysV64 descriptor
 * @param m    the fully-allocated MModuleX86 (every MReg physical)
 * @return     the encoded module (text/rodata/symbols/relocs), or an error on an unencodable MInstX86
 * @throws     when an allocated MInstX86 has no encoding (an internal invariant break)
 * @since 0.3.1
 */
pub fn encode_module_x86(abi: AbiDescriptor, m: MModuleX86): EncodedModuleX86 | error
```

## Rulings & laws

- **Teko-only:** `src/backend/*.tks`; no C twin. Runtime linked by `ld` as externals.
- **W15 full Javadoc** on every new/touched `select_*`/`regalloc_*`/`encode_*`/`emit_elf` fn; flatten; no
  `//`.
- **Reuse A3's allocator + the ELF writer** — B1 swaps the descriptor + the x86 encoder, it does NOT fork
  the allocator or the object writer.
- **Determinism now** (RM-C16 precondition): the ELF object reproduces byte-for-byte.
- **Backend native testing removed (owner 2026-08-18):** the x86-64/linux CI leg exercises this vertical.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  `[dry]` — no emitted-byte change on the C route; sweep `.tkt` after any descriptor/encoding change.

## Fixtures

none — the fixpoint self-build exercises this. The compiler compiles ITSELF to x86-64 ELF and the
x86-64/linux native CI leg links + runs it (owner: "backend native — REMOVER; o CI exercita"). RM-C16's
object-reproducibility fixpoint proves the determinism.

## Gate

`[dry]` — compile + scoped `.tkr` + trivial fixpoint (no emitted-byte change; the object feeds only the
native link). "Green" = the x86 isel/regalloc/encode tail covers A1's full `LOp` set, `emit_elf` writes a
deterministic ELF the system `ld` links + runs on x86-64/linux, and the C-route build is byte-identical.
Reseed-class: `none`.

## Deps

`NAT-A3` — verbatim from 000-INDEX (reuses A3's allocator; the SysV64 descriptor parameterizes it).

## Done when

The SysV64 descriptor + x86 isel/regalloc/encoder cover A1's full `LOp` set, `emit_elf` writes a
deterministic ELF the system `ld` links into a runnable x86-64/linux binary (runtime as externals), and
the `[dry]` build is byte-identical.
