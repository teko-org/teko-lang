---
seq: 0105
crumb-id: RM-C15
milestone: M4
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [RM-C12, NAT-A4, NAT-B1, NAT-B3, NAT-AARCH, NAT-XL]
sources:
  - "docs/design/reducao-memoria-arrays-0.3.1.md:311-320"              # C15 terminal native per-unit .o
  - "docs/design/reducao-memoria-arrays-0.3.1.md:403-426"              # visibility → symbol table
  - "docs/design/plano-mestre-0.3.1-implementacao.md:290"              # M4 RM-C15 row
  - "docs/design/plano-mestre-0.3.1-implementacao.md:382-396"          # per-unit pipeline + symbol mapping
  - "src/build/project.tks:1633"                                       # encode_lfunc_in_region (per-unit dump)
---

# 0105 · RM-C15 — terminal native: per-unit `.o` (the unit region dropped = the dump)

> The terminal native emit: for each namespace, `lower → isel → regalloc → encode → emit_elf/macho/coff`
> produces ONE `.o` on disk and the unit's arena region is DROPPED (the `.o` IS the memory dump — Eixo C
> realized on the native route). Visibility → object symbol table: `exp`+`pub` = GLOBAL, private = local.

## Goal

The endgame of RM Eixo C: the native route emits a linkable object PER UNIT (namespace), so only ONE
unit's memory lives at a time — the per-unit region is dropped the instant its `.o` is written (the `.o`
is the dump; the memory-ceiling win). This composes the NAT legs (A4 Mach-O, B1 ELF, B3 COFF, AARCH
aarch64-ELF, XL target resolution) behind the per-unit pipeline C12 fused on the C route ("saída abstrata
de C12 = `.o` aqui"). The load-bearing new rule is the **visibility → object symbol table mapping**:
`exp`+`pub` → GLOBAL symbol (reaches `ld` for cross-unit link), private → `static`/local (never enters the
table). The object's symbol table IS the internal-FFI (RM-C11) on the linkage plane — `pub` reaches `ld`
WITHOUT leaking into the `.tkh` (only `exp` does). The internal linker resolves cross-unit symbols; the OS
`ld` (or `objfile_ar`) joins the final binary; the runtime links as undefined externals. It is a
**byte-mover** on the native route (it produces the terminal artifact) driving a **fixpoint-rebuild** — but
the fixpoint criterion is still `gen2.c==gen3.c` HERE (the C route stays the crutch); RM-C16 migrates the
criterion to native-object-reproducible.

## Where

- `src/build/project.tks:1598` `emit_native` + `:1633` `encode_lfunc_in_region_*` / `:1654`
  `encode_module_fused_*` — the per-FUNCTION scoped-region encode already exists (the region-drop-per-
  function dump); RM-C15 LIFTS the granularity to per-UNIT (namespace): each namespace lowers→isel→
  regalloc→encode→emits ITS `.o`, then `region_drop`s the unit region before the next.
- `src/backend/objfile_elf.tks` / `objfile_macho.tks` / `objfile_coff.tks` — the symbol-table emission:
  map each symbol's visibility to GLOBAL (`exp`+`pub`) or local/`static` (private).
- `src/backend/objfile_ar.tks:128` `emit_ar_archive` + `global_symbol_names` (`:147`) — the archive/link
  join that gathers the per-unit `.o`s; confirm it reads the GLOBAL symbols the mapping produces.
- The internal-FFI link table (RM-C11) — the cross-unit symbol resolution; its table IS the object symbol
  table on the linkage plane (globals = `exp`+`pub`).

## How

1. **Per-unit pipeline** (`emit_native`): iterate namespaces in deterministic order; for each,
   `lower_program`/`lower_item` its bodies → `select_module*` → `regalloc_module*` → `encode_module*` →
   `emit_elf`/`emit_macho`/`emit_coff` → write `<unit>.o` → `region_drop` the unit region. Only one unit's
   corpus lives at a time (the residual-memory win of Eixo C on the native route).

```teko
/**
 * emit_unit_object — lower→isel→regalloc→encode→emit ONE namespace to a linkable .o on disk, then drop the
 * unit's arena region (the .o IS the memory dump — Eixo C's per-unit ceiling on the native route). Symbol
 * visibility maps to the object symbol table: exp+pub → GLOBAL (reaches ld for cross-unit link), private →
 * local/static (never enters the table). The internal-FFI (RM-C11) resolves cross-unit symbols; the OS ld
 * / objfile_ar joins the final binary.
 *
 * @param unit    the namespace's lowered LModule + its link decls
 * @param target  the resolved NativeTarget (selects emit_elf/macho/coff via TargetRow.objfmt)
 * @param out_dir the object output directory
 * @return the written .o path, or an error on an emit/encode failure
 * @throws when the unit fails to encode or the object cannot be written
 * @since 0.3.1
 */
fn emit_unit_object(unit: LUnit, target: NativeTarget, out_dir: str): str | error
```

2. **Visibility → symbol table** (the load-bearing mapping): for each defined symbol, emit GLOBAL binding
   when the decl is `exp` or `pub`, LOCAL/`static` when private (private never enters the table). This is
   the SAME `exp`+`pub`=global rule the internal-FFI (RM-C11) uses — the object symbol table is that FFI on
   the linkage plane. `pub` reaches `ld` but NOT the `.tkh` (only `exp` reaches the header — orthogonal
   tables).
3. **Join the objects**: the internal linker resolves cross-unit references (the RM-C11 table); the OS `ld`
   (or `objfile_ar`, `objfile_ar.tks:128`) joins the per-unit `.o`s + the runtime externals into the final
   binary.
4. **Precondition**: RM-C12 (fused per-unit emit) must be green on the C route first — C15 lifts C12's
   abstract per-unit output to a real `.o`.
5. **Fixpoint on the C route still**: the criterion here is `gen2.c==gen3.c` (the C route is the crutch);
   the native object is produced + linked + run by the native CI legs. RM-C16 migrates the criterion.

## Rulings & laws

- **Teko-only:** `src/build/*.tks` + `src/backend/*.tks`; no C twin. Runtime linked by `ld` as externals.
- **W15 full Javadoc** on `emit_unit_object` + the symbol-mapping helpers; flatten; no `//`.
- **`teko.c` é muleta; endgame é binário linkável por `ld` sem compilador C (owner 2026-08-19):** the
  per-unit `.o` IS the terminal; the unit→object→disk→free makes the per-unit dump natural.
- **Visibility mapping (RM-C15 / reducao §403-426):** `exp`+`pub`=GLOBAL, private=local/`static`; the
  object symbol table = the internal-FFI on the linkage plane; `pub` reaches `ld` without leaking to
  `.tkh`.
- **C-route crutch stays** until RM-C16 proves the object reproduces — do NOT remove the C route here.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744` (the per-unit dump is the
  MECHANISM that keeps the native build under the cap — a blown guard is a root-cause fix); commit each
  green step; `[fixpoint]` — `gen2.c==gen3.c` byte-identical (C route); sweep `.tkt` after signature
  changes.

## Fixtures

none — the fixpoint self-build exercises this. The compiler emits ITS OWN per-unit `.o`s and the four
native CI legs link + run the joined binary; the visibility→symbol mapping is exercised by every cross-unit
`pub` call the compiler makes at scale (owner: "backend native — REMOVER; o CI exercita"). A wrong mapping
surfaces as an unresolved/duplicate symbol at link, not a missed fixture.

## Gate

`[fixpoint]` — build gen2 (C route) + scoped regression + `gen2.c==gen3.c` byte-identity, AND the native
legs emit per-unit `.o`s that the OS `ld`/`objfile_ar` join into a runnable binary. "Green" = each
namespace emits one `.o` with the unit region dropped, `exp`+`pub` symbols are GLOBAL and private are
local, the joined native binary runs on all four legs, and the C-route fixpoint is byte-identical.
Reseed-class: `fixpoint-rebuild`.

## Deps

`RM-C12, NAT-A4, NAT-B1, NAT-B3, NAT-AARCH, NAT-XL` — verbatim from 000-INDEX (the fused per-unit C emit +
every native encoder/objfile leg + target resolution).

## Done when

Each namespace emits ONE linkable `.o` with its arena region dropped (the `.o` is the dump), `exp`+`pub`
symbols are GLOBAL and private are local/`static`, the internal-FFI + OS `ld`/`objfile_ar` join a runnable
native binary on all four legs, and the `gen2.c==gen3.c` fixpoint stays byte-identical.
