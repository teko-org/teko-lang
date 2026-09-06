---
seq: 0034
crumb-id: SM-S6
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [SM-R1, SM-G9]
sources:
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1213-1216"   # §10 Phase S — S6
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:836-880"     # §7b.3-7b.6 reball scope + byte-preservation
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:855-864"     # §7b.4 convergence with slice/native rep
---

# 0034 · SM-S6 — reball memory/collection positions `u64` → `usize`/`size`

> Reball memory/collection positions `u64` → `usize`/`size` (source pos stays `u32`) — make the type say
> what the metal already does, byte-preserving on 64-bit.

## Goal

Mechanically rewrite every MEMORY/machine position in `src/` + `.tkt` from `u64` to `usize` (and `size`
for a signed delta) now that G9 (`0015`) added `Usize`/`Size` to `PrimKind` with the lowering table
`Usize => i64`, `Size => i64` on 64-bit. The targets: slice `.len`/`.cap`, array/collection indices, byte
offsets, arena offsets, `tk_region_alloc` sizes, the slice header `{ptr, len, cap}`, and the DPS/AL3
machinery (`alloc_call_dest` sizes, DPS destination offsets, `scope_touches_arena` counts,
`tk_slice_grow_inplace`) — so the whole arena/DPS/slice machinery speaks ONE machine-word type. SOURCE
positions (`line`/`col` `u32`, token/byte-span offsets) STAY `u32` — they are diagnostic coordinates, not
metal measures. Byte-preserving on the 64-bit fixpoint targets (`usize == u64`, `size == i64`, bit-for-bit;
the lowering emits the identical machine type), so it rides the source sweep and needs NO separate reseed —
`fixpoint-rebuild`. This is the unifying move that lets the paused #112 native-slice rep work resume against
a single position type instead of a hard-coded `u64`.

## Where

- `src/**/*.tks` — every memory/collection `u64` position → `usize` (or `size` for a signed delta): slice
  `.len`/`.cap` fields, index/offset params + locals, `of_len`/`place`/arena-size args, loop counters that
  INDEX memory, every `to u64` cast ON A POSITION → `to usize`.
- `src/checker/typer.tks` / `src/lir/lower.tks` / `src/codegen/codegen.tks` — the slice header + the
  prim→machine-type table (G9's `Usize => i64`, `Size => i64` on 64-bit) — confirm `Usize`/`Size` lower to
  the SAME LIR/C integer type as `U64`/`I64` (the byte-preservation proof point).
- `src/parser/ast.tks` (`line: u32`/`col: u32`, `TExpr`, every decl) — SOURCE positions UNCHANGED (they are
  diagnostic coordinates, target-independent).
- `src/**/*.tkt` — the expectation corpus — memory positions rewritten in lockstep; source positions
  untouched.

NEW: no new surface; a discriminated mechanical mass rewrite gated by 64-bit byte-identity.

## How

1. **Apply the discriminator (§7b.3).** For every `u64`, classify: MEASURES or INDEXES memory/a collection
   → `usize` (or `size` for a signed delta, e.g. a pointer difference); LOCATES a point in source text
   (`line`/`col`, token/byte-span offsets) → unchanged `u32`; a DOMAIN value (a hash, a numeric literal's
   value, a timestamp) → unchanged `u64` (it is not a position). Only the FIRST class reballs.
2. **Reball the slice header + DPS/AL3 machinery (§7b.4).** Slice `.len`/`.cap`, `alloc_call_dest` sizes,
   the DPS destination offsets, `tk_region_alloc`'s length, `scope_touches_arena`'s counts, and
   `tk_slice_grow_inplace` all become `usize` — one machine-word type across the whole memory model. This is
   the tie the owner named to the #112 native rep: the header now speaks `usize` rather than a hard-coded
   `u64`.
3. **Keep source positions `u32` (§7b.3).** `line`/`col` and byte-in-file spans do NOT reball — a 32-bit
   build does not have shorter source files; making them target-dependent would be meaningless. Leave every
   `ast.tks` `line: u32`/`col: u32` untouched.
4. **Byte-identity is the gate (§7b.5).** On the 64-bit fixpoint targets `prim_width(Usize) == 64` and the
   lowering emits the SAME `i64`/`u64` it emits for `u64` today, so `u64 → usize` on a position changes the
   checker's type but lowers to IDENTICAL bytes → `gen2==gen3` holds. 32-bit (`usize == u32`) is NOT a
   fixpoint target and never enters the comparison (its correctness is a separate cross-compile property).
5. **Sweep in batches; commit each green.** Rewrite by directory, each fixpoint-green and committed; sweep
   `.tkt`/`.tkr` in lockstep (a signature/type change mandates a corpus sweep).

## Rulings & laws

- **Teko-only:** source `.tks`/`.tkt` rewrite; the prim table is `.tks` (from G9); no C twin.
- **W15 full Javadoc** unaffected; no inline `//` introduced.
- **Byte-preserving on 64-bit (§7b.5):** `Usize`/`Size` lower to the same machine type as `U64`/`I64`; the
  fixpoint proves it. The reballing needs NO separate reseed (rides the sweep).
- **Source positions stay `u32` (§7b.3):** diagnostic coordinates are target-independent — do NOT reball
  `line`/`col`/spans.
- **Discriminator is explicit (§7b.3):** measure/index memory → `usize`; locate source → `u32`; domain
  value → unchanged.
- **Safety:** NEVER `teko test .`; build gen2 in a subshell with `ulimit -v 6815744`; commit each green
  batch; NO reseed (fixpoint-rebuild); fixpoint `gen2==gen3`; sweep `.tkt`/`.tkr` in lockstep.

## Fixtures

`none — the fixpoint self-build exercises this`. The reball rewrites the compiler's own memory positions;
building gen2 on the SM-R1 seed and proving `gen2==gen3` byte-identical (on 64-bit `usize == u64`) IS the
exercise and the byte-preservation proof. A mis-classified domain value or source position that got reballed
would break the build or the byte-identity, caught by the fixpoint.

## Gate

`[fixpoint]` — build gen2 on the SM-R1 seed + scoped regression + `gen2==gen3` byte-identity. "Green" =
every memory/collection position in `src/` + `.tkt` is `usize`/`size`, source positions stay `u32`, the
prim table lowers `Usize`/`Size` identically to `U64`/`I64` on 64-bit, the build is byte-identical
(`gen2==gen3`). Reseed-class: `fixpoint-rebuild`.

## Deps

`SM-R1` (the seed must know `usize`/`size`) and `SM-G9` (which added `Usize`/`Size` to `PrimKind` + the
lowering table the reball rewrites onto).

## Done when

Every memory/collection position in `src/` + `.tkt` is `usize` (or `size` for signed deltas), source
positions remain `u32`, `Usize`/`Size` lower identically to `U64`/`I64` on the 64-bit targets, and gen2 on
the SM-R1 seed is byte-identical (`gen2==gen3`).
