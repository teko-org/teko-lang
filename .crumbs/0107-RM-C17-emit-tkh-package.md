---
seq: 0107
crumb-id: RM-C17
milestone: M4
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [RM-C16]
sources:
  - "docs/design/reducao-memoria-arrays-0.3.1.md:329-333"              # C17 emit + package the compiler's .tkh
  - "docs/design/plano-mestre-0.3.1-implementacao.md:292"              # M4 RM-C17 row
  - "src/emit/tkh.tks:217"                                             # emit_tkh
  - "src/emit/header.tks:144"                                          # emit_tkh call site
---

# 0107 · RM-C17 — emit + package the compiler's own `.tkh` (ship binary + `.tkh`)

> The final compiler build (C route OR native) ALSO emits the compiler's own `.tkh` (its aggregated `exp`
> surface) and the package ships binary + `.tkh`. Backend-independent (interface of types). Only `exp`
> embarks — the internal-FFI (`exp`+`pub`) is transitional and NEVER enters the `.tkh` (R8).

## Goal

The compiler ships its own type-interface header so a dev can get IDE intellisense against the compiler and
link/extend it. The build (native, after RM-C16 removed the C route) emits the compiler's `.tkh` — the
AGGREGATED `exp` surface via `emit_tkh` (`tkh.tks:217`) — alongside the binary, and the package delivers
both. It is backend-INDEPENDENT (a `.tkh` is an interface of types, identical whichever route emitted the
binary). The visibility law is strict: ONLY `exp` reaches the `.tkh` (the user interface); the internal-FFI
(`exp`+`pub`, the cross-unit link table of RM-C11/RM-C15) is a TRANSITIONAL build artifact that is
discarded after the link and NEVER embarks in the `.tkh` (R8). It has no memory impact; it enables
next-version features. It is **byte-preserving** (additive emission alongside the binary) driving a
**fixpoint-rebuild** (byte-identity: the `.tkh` is deterministic — aggregated `exp` in a stable order — so
the rebuild reproduces it).

## Where

- `src/emit/tkh.tks:217` `emit_tkh` — the `.tkh` writer over the aggregated `exp` `Header`; confirm it
  aggregates the compiler's OWN `exp` surface (the whole compiler-as-library) for this build.
- `src/emit/header.tks:144` — the `emit_tkh(header)` call site; wire it into the FINAL compiler build so
  the compiler's `.tkh` is emitted next to the binary.
- `src/build/project.tks` — the packaging step: ship binary + `.tkh` together (the package manifest).
- The internal-FFI link table (RM-C11/C15) — CONFIRM it is discarded after the link and does NOT feed
  `emit_tkh` (only `exp` reaches the `.tkh`; `pub` stays internal).

## How

1. **Aggregate the compiler's `exp` surface** for its own build: the same `emit_tkh` (`tkh.tks:217`) the
   compiler runs for a user project, applied to the compiler-as-library — the `Header` carries only `exp`
   declarations (per `tast.tks` M.4: only `exp` reaches the header).

```teko
/**
 * emit_compiler_tkh — emit the compiler's OWN .tkh: the aggregated exp surface of the compiler-as-library,
 * backend-independent (identical whichever route emitted the binary). Only exp declarations embark; the
 * internal-FFI (exp+pub) link table is a transitional build artifact discarded after the link and NEVER
 * written to the .tkh (R8). Deterministic (stable declaration order) so the rebuild reproduces it byte-
 * for-byte (fixpoint).
 *
 * @param compiler_exports  the compiler's aggregated exp Header
 * @param out_path          the .tkh output path shipped beside the binary
 * @return the written .tkh path, or an error on a write failure
 * @throws when the .tkh cannot be written
 * @since 0.3.1
 */
fn emit_compiler_tkh(compiler_exports: Header, out_path: str): str | error
```

2. **Emit it in the final build** (`header.tks:144` wiring): the native build produces the binary AND calls
   `emit_compiler_tkh` — the `.tkh` is backend-independent, so it is identical whether the C route (pre-C16)
   or the native route emitted the binary.
3. **Package binary + `.tkh`**: the package delivers both artifacts.
4. **Enforce the visibility boundary**: only `exp` embarks; the internal-FFI (`exp`+`pub`) is discarded
   after the link (RM-C15's object symbol table exposes `pub` to `ld`, but `pub` NEVER reaches the `.tkh` —
   orthogonal tables). Confirm `emit_tkh` reads only `exp`.
5. **Fixpoint**: the `.tkh` is deterministic (aggregated `exp` in stable order), so the rebuild reproduces
   it; `gen2==gen3` holds (now native-object + `.tkh` reproducibility).

## Rulings & laws

- **Teko-only:** `src/emit/*.tks` + build; no C twin.
- **W15 full Javadoc** on `emit_compiler_tkh` + helpers; flatten; no `//`.
- **A build emite o `.tkh` junto com o binário (owner 2026-08-19):** the final build ships binary + `.tkh`;
  backend-independent; serves next-version IDE/linking features.
- **Só `exp` entra no `.tkh` (visibility law, `tast.tks` M.4 + R8):** the internal-FFI (`exp`+`pub`) is
  transitional and does NOT embark — `pub` reaches `ld` but not the header.
- **Deterministic `.tkh`:** stable declaration order so the fixpoint reproduces it.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  `[fixpoint]` — `gen2==gen3` byte-identical (native object + `.tkh`); sweep `.tkt`/`.tkr` after the emit
  wiring.

## Fixtures

none — the fixpoint self-build exercises this. The compiler emits its OWN `.tkh` on every self-build, and
the native fixpoint reproduces it byte-for-byte; a non-deterministic `.tkh` surfaces as a `gen2!=gen3`
break. The `.tkh` shape is already covered by `read_tkh` round-trips the self-build exercises.

## Gate

`[fixpoint]` — build gen2 + scoped regression + `gen2==gen3` byte-identity (native object + the emitted
`.tkh`). "Green" = the final build emits the compiler's `.tkh` (aggregated `exp` only, internal-FFI
excluded) beside the binary, the package ships both, and the rebuild reproduces the `.tkh` byte-for-byte.
Reseed-class: `fixpoint-rebuild`.

## Deps

`RM-C16` — verbatim from 000-INDEX (the native route + C-route removal; the `.tkh` ships from the native
build).

## Done when

The final compiler build emits the compiler's own `.tkh` (aggregated `exp` surface, internal-FFI excluded,
`pub` never embarked) beside the binary, the package ships binary + `.tkh`, and the `gen2==gen3` fixpoint
reproduces the `.tkh` byte-for-byte.
