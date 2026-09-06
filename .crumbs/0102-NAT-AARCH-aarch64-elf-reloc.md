---
seq: 0102
crumb-id: NAT-AARCH
milestone: M4
gate: "[dry]"
reseed-class: "none"
deps: [NAT-A4, NAT-B1]
sources:
  - "docs/design/plano-mestre-0.3.1-implementacao.md:287"              # M4 NAT-AARCH row
  - "docs/design/plano-mestre-0.3.1-implementacao.md:387-389"          # reloc key (MRelocKind,width,addend)
  - "src/backend/objfile_elf.tks:799"                                  # elf_reloc_type_arm64 (keyed by kind alone)
  - "src/backend/objfile_elf.tks:808"                                  # arm64_reloc_reqs
  - "src/backend/minst.tks:9"                                          # MRelocKind
---

# 0102 · NAT-AARCH — aarch64-ELF relocation map + writer decls

> The aarch64-on-ELF vertical (arm64/linux): an aarch64-ELF relocation map keyed by
> `(MRelocKind, width, addend)` — NOT `MRelocKind` alone — plus the writer declarations, closing the
> reproduced link-and-fail bug where a bare-kind key emitted the wrong relocation type.

## Goal

arm64/linux joins the target set: the same arm64 encoder (NAT-A4) feeds the ELF writer (NAT-B1's
`emit_elf`) instead of Mach-O, via `emit_elf_arm64` (already dispatched at `project.tks:1616`). The one
genuine gap is the relocation MAP: today `elf_reloc_type_arm64` (`objfile_elf.tks:799`) is keyed by
`MRelocKind` ALONE and hard-codes `addend = 0` (`arm64_reloc_reqs:814`). The reproduced link-and-fail bug
is that the correct aarch64-ELF relocation type depends on `(kind, width, addend)` — the same `MRelocKind`
maps to different `R_AARCH64_*` types by operand width/addend. This crumb WIDENS the map key to the
triple, so the emitted `.rela` entries carry the right type + addend and the system `ld` resolves them.
Design-ahead `[dry]` against NAT-A4/B1's DECLARED encoder/writer; **byte-preserving** on the C route;
reseed-class `none`.

## Where

- `src/backend/objfile_elf.tks:799` `elf_reloc_type_arm64` — WIDEN the key from `MRelocKind` to
  `(MRelocKind, width, addend)`; return the correct `R_AARCH64_*` per the triple.
- `src/backend/objfile_elf.tks:808` `arm64_reloc_reqs` / `:820` `arm64_rodata_reloc_reqs` — thread the
  reloc `width` + `addend` from the encoder's reloc record into `ElfRelocReq` (today `addend = 0` is a
  hard-code, the bug).
- `src/backend/minst.tks:9` `MRelocKind` — confirm the encoder's reloc record carries `width` + `addend`
  alongside `kind` (extend `RelocX86`/the arm64 reloc record if the width/addend fields are absent).
- `src/build/project.tks:1615-1616` `emit_native_arm64_linux` — already routes `encode_module`→
  `emit_elf_arm64`; confirm the ELF writer path uses the widened map.

## How

1. **Widen the reloc key** (`elf_reloc_type_arm64:799`): a match over `(MRelocKind, width, addend)`
   selecting the correct `R_AARCH64_*`. The existing arms (`PageHi→ADR_PREL_PG_HI21`,
   `PageLo→ADD_ABS_LO12_NC`, `Call→CALL26`, `Abs64→ABS64`) stay for their canonical width/addend; the
   widening disambiguates the cases where the same kind at a different width/addend needs a different type.

```teko
/**
 * elf_reloc_type_arm64 — the aarch64-ELF relocation type for a reloc site keyed by the TRIPLE
 * (MRelocKind, width, addend), not MRelocKind alone: the same kind at a different operand width or addend
 * resolves to a different R_AARCH64_* type (the reproduced link-and-fail bug where a bare-kind key emitted
 * the wrong type). The single source the ELF writer reads for every arm64 .rela entry.
 *
 * @param kind    the relocation kind the encoder recorded
 * @param width   the relocated field width in bits (the disambiguator the bare key dropped)
 * @param addend  the reloc addend (threaded into the .rela entry, no longer hard-coded 0)
 * @return        the R_AARCH64_* relocation type for this site
 * @since 0.3.1
 */
fn elf_reloc_type_arm64(kind: MRelocKind, width: u32, addend: i64): u32
```

2. **Thread width + addend** (`arm64_reloc_reqs:808`): read the encoder's reloc `width`/`addend` into
   `ElfRelocReq { offset; sym; rtype = elf_reloc_type_arm64(kind, width, addend); addend }` — replacing the
   `addend = 0` hard-code that dropped the addend.
3. **Confirm the encoder records the triple**: if the arm64 reloc record carries only `kind`, extend it
   with `width`/`addend` (a minimal `minst.tks` reloc-record widening; in-memory only, no wire concern).
4. **Writer decls**: the aarch64-ELF path reuses `emit_elf`'s machinery with `e_machine = EM_AARCH64`
   (`objfile_elf.tks:789`) and the widened map — the writer is the SAME as B1's, differing only in machine
   + reloc map.

## Rulings & laws

- **Teko-only:** `src/backend/*.tks`; no C twin. Reloc records are in-memory (no wire format).
- **W15 full Javadoc** on the widened `elf_reloc_type_arm64` + the reloc-req builders; flatten; no `//`.
- **Reloc key is the TRIPLE** (`plano-mestre:387-389`): `(MRelocKind, width, addend)` — the recorded fix
  for the link-and-fail bug; a reviewer must not "simplify" it back to the bare kind.
- **Reuse `emit_elf`** (NAT-B1's writer) — AARCH differs only in `e_machine` + the reloc map, it does not
  fork the ELF writer.
- **Backend native testing removed (owner 2026-08-18):** the arm64/linux CI leg exercises this.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  `[dry]` — no emitted-byte change on the C route; sweep `.tkt` after the reloc-record widening.

## Fixtures

none — the fixpoint self-build exercises this. The compiler compiles ITSELF to arm64/linux ELF and the
arm64/linux native CI leg links + runs it; a wrong reloc type surfaces as a link failure in the produce
leg, not a missed fixture (owner: "backend native — REMOVER; o CI exercita").

## Gate

`[dry]` — compile + scoped `.tkr` + trivial fixpoint (no emitted-byte change; the object feeds only the
native link). "Green" = `elf_reloc_type_arm64` is keyed by `(MRelocKind, width, addend)` with the addend
threaded, the aarch64-ELF object links + runs on arm64/linux, and the C-route build is byte-identical.
Reseed-class: `none`.

## Deps

`NAT-A4, NAT-B1` — verbatim from 000-INDEX (the arm64 encoder + the ELF writer both land first).

## Done when

The aarch64-ELF relocation map is keyed by `(MRelocKind, width, addend)`, the addend is threaded (no
`addend = 0` hard-code), the arm64/linux ELF object links into a runnable binary via the system `ld`, and
the `[dry]` build is byte-identical.
