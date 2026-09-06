---
seq: 0111
crumb-id: D1-T4
milestone: M5
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [RM-C16]
sources:
  - "docs/design/mudancas-superficie-0.3.1.md:1660"                    # Doc-2 literal/const dedup
  - "docs/design/plano-mestre-0.3.1-implementacao.md:304"              # M5 D1-T4 row
  - "docs/design/backend-a1-lir-lowering.md:162-186"                   # LRodata / LGlobal (the intern point)
---

# 0111 · D1-T4 — static literal / folded-constant dedup

> Emit each distinct string literal / folded constant ONCE in rodata and reference it everywhere it recurs,
> instead of emitting a fresh rodata entry per occurrence — a smaller binary. Optimization of the emitter;
> a perf/binary-size fixpoint, no capability.

## Goal

Doc-2:1660: today (and at A1-4's `LRodata` interning) a string literal or a folded constant can be emitted
as a rodata entry per OCCURRENCE. This crumb DEDUPES: each distinct literal/folded constant is emitted once
under a single canonical symbol, and every reference (`LGlobalAddr`) points at that one entry — a strictly
smaller binary with no behavioral change (rodata is immutable; sharing a byte-identical constant is
semantically transparent). It composes with A1-4's `LRodata`/`LGlobal` (the intern point): the dedup keys
by the constant's bytes (+ machine type for a folded constant). It is the native route's binary-size tune;
NO capability. It is a **byte-mover** on the native route (fewer rodata entries) driving a
**fixpoint-rebuild** — byte-identity holds because the dedup is a deterministic function of the constant
set with a canonical symbol order.

## Where

- `src/lir/lir.tks` — `LRodata`/`LGlobal` + `add_rodata`/`add_global` (A1-4) — INTERN by content: a
  content-keyed table so a repeated literal reuses the existing symbol instead of appending a new entry.
- `src/lir/lower.tks` — the `TStrLit`→rodata materialization + folded-constant sites (`lower_const.tks`) —
  route through the interning `add_rodata`/`add_global` so `LGlobalAddr` references the canonical symbol.
- `src/codegen/codegen.tks` — the C-route rodata emission (while the C route still exists pre-final): mirror
  the dedup so the C and native artifacts agree (or, post-RM-C16, native only).

## How

1. **Content-keyed intern table** in the `LModule` builder: `add_rodata(bytes)` returns the EXISTING
   symbol when `bytes` already interned, else mints a fresh canonical symbol and appends. A folded constant
   keys by `(bytes, LType)`.

```teko
/**
 * intern_rodata — return the canonical rodata symbol for `bytes`, emitting a NEW entry only on first sight
 * and reusing the existing symbol on every recurrence (static literal / folded-constant dedup → a smaller
 * binary). Rodata is immutable, so sharing a byte-identical constant is semantically transparent. The
 * symbol order is canonical (first-emission order) so the native object reproduces byte-for-byte (fixpoint).
 *
 * @param m      the LModule being built
 * @param bytes  the constant's raw bytes (a string literal's UTF-8 or a folded constant's image)
 * @return the (module, canonical symbol) pair — the symbol is shared across all occurrences
 * @since 0.3.1
 */
fn intern_rodata(m: LModule, bytes: []byte): { module: LModule; symbol: str }
```

2. **Route every literal/folded-constant site through it** (`lower.tks` `TStrLit`, `lower_const.tks`
   folded constants): `LGlobalAddr` references the interned symbol; a recurring `"…"` no longer mints a
   duplicate rodata entry.
3. **Canonical symbol order**: mint symbols in first-emission (deterministic walk) order so the native
   object reproduces — the dedup must not introduce ordering non-determinism (RM-C16 constraint).
4. **Behavioral transparency**: rodata is immutable; two byte-identical constants sharing one entry cannot
   be observed apart (no address-identity semantics on constants). Confirm no site relies on distinct
   addresses for equal constants.
5. **Fixpoint**: the interned set + canonical order are deterministic; the native object reproduces;
   `gen2==gen3`.

## Rulings & laws

- **Teko-only:** `src/lir/*.tks` + `src/codegen/*.tks`; no C twin.
- **W15 full Javadoc** on `intern_rodata` + the interning `add_rodata`/`add_global`; flatten; no `//`.
- **Canonical symbol order (RM-C16):** first-emission order, deterministic — the dedup must not break
  native-object reproducibility.
- **Rodata immutable → dedup is transparent:** no address-identity semantics on constants.
- **Optimization only — no capability:** a smaller binary, same behavior.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  `[fixpoint]` — native-object-reproducible `gen2==gen3`; sweep `.tkt` after the interning signature change.

## Fixtures

none — the fixpoint self-build exercises this. The compiler emits thousands of recurring literals compiling
itself (keywords, format strings, symbol prefixes); the dedup is measured by the binary-size delta the
native fixpoint tracks, and its determinism is proven by native-object reproduction. No isolated `.tkr`
reaches the rodata-interning surface.

## Gate

`[fixpoint]` — build gen2 + scoped regression + `gen2==gen3` native-object byte-identity. "Green" = each
distinct literal/folded constant emits ONE rodata entry referenced everywhere it recurs, the binary
shrinks, the symbol order is canonical, and the native object reproduces. Reseed-class: `fixpoint-rebuild`.

## Deps

`RM-C16` — verbatim from 000-INDEX (the native route the emitter tune targets).

## Done when

Each distinct string literal / folded constant is emitted once under a canonical symbol and shared across
all occurrences, the binary is smaller, and the native-object `gen2==gen3` fixpoint reproduces.
