---
seq: 0100
crumb-id: NAT-A4
milestone: M4
gate: "[dry]"
reseed-class: "none"
deps: [NAT-A3]
sources:
  - "docs/design/backend-a4-encoder.md:1-60"                           # arm64 encoder + Mach-O
  - "docs/design/plano-mestre-0.3.1-implementacao.md:285"              # M4 NAT-A4 row
  - "src/backend/encode_arm64.tks:1338"                                # encode_module
  - "src/backend/objfile_macho.tks:495"                                # emit_macho
  - "docs/design/plano-mestre-0.3.1-implementacao.md:395-396"          # ld resolves libc/kernel as externs
---

# 0100 · NAT-A4 — arm64 encoder + Mach-O object + link via system `ld`

> arm64 machine-code encoding (`encode_arm64.tks`): bit-exact `MInst`→bytes over the allocated `MFunc`,
> a Mach-O object writer (`objfile_macho.tks`), and linking the object via the system `ld` (the runtime
> resolved the normal way — `ld` binds libc/kernel symbols as undefined externals).

## Goal

The arm64 encoder + object-emit leg: consume A3's fully-allocated `MModule` and encode each `MInst` to
bit-exact AArch64 bytes (`encode_module`, `encode_arm64.tks:1338`), collect symbols + relocations, and
write a Mach-O object (`emit_macho`, `objfile_macho.tks:495`) that the system `ld` links into a runnable
binary — the runtime resolved the normal way (`ld` binds libc/kernel externals; proven `afdb1fd8`). This
is the FIRST leg that produces real machine bytes, closing the arm64/macOS native vertical. It is
design-ahead `[dry]` against A3's DECLARED `MModule`; both `encode_module` and `emit_macho` already exist —
A4 closes coverage to A3's full allocated `MInst` set and makes the object deterministic. **Byte-preserving**
on the C route (the object feeds only the native link, never the emitted C); reseed-class `none`.

## Where

- `src/backend/encode_arm64.tks:1338` `encode_module` — `MInst`→AArch64 bytes; close coverage to A3's full
  allocated set + emit the page-hi/page-lo/call/abs64 relocation records (`MRelocKind`, `minst.tks:9`).
- `src/backend/encode_arm64_consts.tks` — the fixed encoding tables (opcode fields) the encoder indexes.
- `src/backend/objfile_macho.tks:495` `emit_macho` — the Mach-O object writer over `EncodedModule` +
  `DwarfSink`; determinism audit (no timestamp, stable symbol/section order).
- `src/build/project.tks:1611-1612` `emit_native_arm64` — already dispatches `encode_module`→`emit_macho`
  per the fused per-function scoped-region path (`encode_lfunc_in_region_*`); confirm the link step invokes
  the system `ld` with the runtime as undefined externals.

## How

1. **Encode every allocated `MInst` bit-exact** (`encode_arm64.tks:1338`): a per-`MInst` match emitting the
   AArch64 word(s), reading the fixed encoding tables. `ADRP`/`ADD` emit `PageHi`/`PageLo` relocs; `BL`
   emits `Call`; a rodata/global address emits `Abs64`. Each reloc record carries `(offset, sym, kind,
   sect)`.
2. **Write the Mach-O object** (`emit_macho`, `objfile_macho.tks:495`): header + `__text`/`__const`
   sections + symbol table (visibility→symbol mapping is RM-C15's concern; here confirm the writer emits
   the symbols the encoder produced) + relocation entries + DWARF (`DwarfSink`).
3. **Determinism** (audit for RM-C16 forward): no timestamp, stable symbol/section ordering, no absolute
   path — the object must reproduce byte-for-byte. Record any non-determinism as a fix HERE, not deferred.
4. **Link via system `ld`**: the emitted `.o` links with the system linker; the runtime (libc/kernel) is
   resolved as undefined externals (`plano-mestre:395-396`) — no C toolchain needed to COMPILE, only the
   OS linker to LINK.

```teko
/**
 * encode_module — encode an allocated arm64 MModule to bit-exact AArch64 machine bytes: each MInst emits
 * its word(s) from the fixed encoding tables, address materializations emit PageHi/PageLo/Call/Abs64
 * relocation records keyed by MRelocKind, and rodata/globals are laid out for emit_macho. Deterministic
 * output (no timestamp, stable ordering) so the object reproduces byte-for-byte (RM-C16 fixpoint).
 *
 * @param abi  the AAPCS64 descriptor (for call/return encoding conventions)
 * @param m    the fully-allocated MModule (every MReg physical)
 * @return     the encoded module (text/rodata/symbols/relocs), or an error on an unencodable MInst
 * @throws     when an allocated MInst has no encoding (an internal invariant break)
 * @since 0.3.1
 */
pub fn encode_module(abi: AbiDescriptor, m: MModule): EncodedModule | error
```

## Rulings & laws

- **Teko-only:** `src/backend/*.tks`; no C twin. The runtime is Teko-lowered + linked by `ld`.
- **W15 full Javadoc** on `encode_module`/`emit_macho` + helpers; flatten; no `//`.
- **`ld` links the runtime as externals** (`plano-mestre:395-396`, proven `afdb1fd8`): no C compiler in the
  path — the OS linker joins the binary.
- **Determinism now** (RM-C16 precondition): no timestamp / stable ordering / no abs path in the object —
  a fix here, not a deferred TODO.
- **Backend native testing removed (owner 2026-08-18):** the arm64/macOS CI leg exercises the encoder +
  Mach-O writer.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  `[dry]` — no emitted-byte change on the C route; sweep `.tkt` after any encoding-table/signature change.

## Fixtures

none — the fixpoint self-build exercises this. The compiler encodes its OWN corpus to arm64 Mach-O and the
arm64/macOS native CI leg links + runs it (owner: "backend native — REMOVER; o CI exercita"). The
determinism is proven by RM-C16's object-reproducibility fixpoint, not a standalone fixture.

## Gate

`[dry]` — compile + scoped `.tkr` + trivial fixpoint (no emitted-byte change; the object feeds only the
native link). "Green" = `encode_module` covers A3's full allocated `MInst` set bit-exact, `emit_macho`
writes a deterministic object the system `ld` links + runs on arm64/macOS, and the C-route build is
byte-identical. Reseed-class: `none`.

## Deps

`NAT-A3` — verbatim from 000-INDEX (the encoder consumes A3's allocated `MModule`).

## Done when

`encode_module` emits bit-exact AArch64 bytes + relocations for A3's full allocated set, `emit_macho`
writes a deterministic Mach-O object the system `ld` links into a runnable arm64/macOS binary (runtime as
externals), and the `[dry]` build is byte-identical.
