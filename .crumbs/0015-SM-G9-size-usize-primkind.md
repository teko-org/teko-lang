---
seq: 0015
crumb-id: SM-G9
milestone: M1
gate: "[dry]"
reseed-class: "(folds R1)"
deps: []
sources:
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:802-891"  # §7b size/usize + reballing
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1186-1188"# §10 Phase G — G9
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1109"     # §9.1 byte-preserving 64-bit
---

# 0015 · SM-G9 — add `size`/`usize` to `PrimKind` + prim predicates + prim→machine-type table

> Add `size`/`usize` to `PrimKind` + prim predicates + prim→machine-type table (inert).

## Goal

Add `size` (signed machine word) and `usize` (unsigned machine word), pointer-sized and
TARGET-DEPENDENT (32 vs 64-bit), as the natural integer types for the arena/slice/pointer machinery.
This is the ADDITIVE half: add `Size`/`Usize` to `PrimKind`, the prim predicates, and the lowering
prim→machine-type table (`Usize`/`Size` => `i64` on 64-bit). INERT until used — `src/` still says `u64`,
so byte-identical. On the 64-bit fixpoint targets `usize == u64` and `size == i64` BIT-FOR-BIT, so a
future `u64→usize` reball lowers to the IDENTICAL emitted bytes → `gen2==gen3` holds. The mass source
reball is SM-S6 (`0034`, M2). `usize`/`size` are DISTINCT KINDS from `uptr`/`ptr` (a size/count vs an
address). Its seed folds into SM-R1.

## Where

- `src/lexer/lexer.tks:315` — `size`/`usize` are NEW primitive TYPE NAMES (resolved as `PrimKind` cases,
  NOT reserved keywords — exactly as `u64` is a type-name identifier today). No keyword collision, no
  grammar change beyond the type table.
- `src/checker/type.tks:11` — `PrimKind = enum { … U8; U16; U32; U64; … }` — add `Size` and `Usize`.
- `src/checker/type.tks:34` — `prim_is_int` → true for both.
- `src/checker/type.tks:50` — `prim_is_signed` → true for `Size`, false for `Usize`.
- `src/checker/type.tks:63` — `prim_width` → the TARGET word width (64 on the fixpoint targets, 32 on a
  32-bit target) — the one place target-dependence enters the type.
- `src/checker/type.tks:104` — `Uptr` (Marshall address word); `:177` — `Uptr` same-kind-only equality —
  KEEP; add `Size`/`Usize` as ordinary int-family prims that `to`-convert with the other ints.
- `src/lir/lower.tks` / `src/codegen/codegen.tks` — the prim→machine-type table gains `Usize => i64`,
  `Size => i64` on a 64-bit target (§7b.5).

## How

1. **Add the prim members** (`type.tks:11`): `Size`, `Usize`.
2. **Wire the predicates:** `prim_is_int(Size|Usize) = true`; `prim_is_signed(Size) = true`,
   `prim_is_signed(Usize) = false`; `prim_width(Usize|Size) = <target word width>`.

```teko
/**
 * prim_width — the bit width of a primitive, target-parameterized for the machine-word kinds: `Size` and
 * `Usize` return the TARGET word width (64 on the x86_64/arm64 fixpoint targets, 32 on a 32-bit target).
 * This is the ONLY place target-dependence enters the type; every other prim has a fixed width. On the
 * 64-bit fixpoint targets `Usize`/`Size` share `U64`/`I64`'s width bit-for-bit, which is why a
 * `u64`→`usize` reball is byte-preserving on the fixpoint.
 *
 * @param k       the primitive kind
 * @param target  the compilation target (for the machine-word width)
 * @return        the bit width of `k` on `target`
 * @since 0.3.1
 */
fn prim_width(k: PrimKind, target: Target): u32
```

3. **Wire the lowering table** (`lower.tks`/`codegen.tks`): `Usize => i64`, `Size => i64` on 64-bit — the
   SAME machine type `U64`/`I64` emit, so a position rewritten `u64→usize` (SM-S6) lowers to the identical
   bytes.
4. **Keep the kinds distinct from `uptr`** (§7b.2): `usize`/`size` = integer measure/position;
   `uptr`/`ptr` = address (Marshall). `usize <-> u64`/`u32` is ordinary `to`-cast integer transport;
   `uptr <-> usize` is the ptr↔word BRIDGE (a Marshall op, `to_uptr`/`from_uptr` territory), NOT a plain
   `to`-cast — so a `usize` index never silently becomes an address and vice-versa. Keep `Uptr`
   same-kind-only equality (`type.tks:177`).
5. **Confirm inert.** `src/` still says `u64` → byte-identical `[dry]` build. The reball (SM-S6) is what
   later adopts `usize`, fixpoint-gated.

## Rulings & laws

- **Teko-only:** lexer/checker/lir/codegen `.tks`; no C twin.
- **W15 full Javadoc** on `prim_width` and the touched predicates; no `//`.
- **`usize`/`size` are type NAMES, not keywords** (§7b.1): resolved as `PrimKind` cases like `u64` — no
  keyword collision.
- **Distinct kinds (§7b.2):** the ptr↔word crossing is a Marshall boundary, not a value conversion —
  never implicit.
- **Byte-preserving on 64-bit (§9.1):** `Usize`/`Size` lower to the same machine type as `U64`/`I64`;
  32-bit is not a fixpoint target (a separate cross-compile property, not a fixpoint gate).
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step.

## Fixtures

`src/` still uses `u64` (reball is SM-S6), so the `usize`/`size` type-check and the kind-distinctness
reject are NOT self-exercised — isolated oracles required:

| fixture | asserts | expected |
|---|---|---|
| `usize_len_index` | `xs.len: usize`, `xs[i: usize]` type-check; `usize` lowers = `u64` on 64-bit | 0 |
| `usize_uptr_not_implicit` | a `usize` used where a `uptr` is expected (and vice-versa) is rejected | EXPECT_COMPILE_FAIL |

## Gate

`[dry]` — compile + the two fixtures + fixpoint (byte-identical; `src/` still `u64`). "Green" =
`size`/`usize` resolve as prims, the predicates + width are correct, the ptr↔word crossing is rejected as
implicit, `usize` lowers identically to `u64` on 64-bit, `[dry]` build byte-identical. Reseed-class:
`(folds R1)`.

## Deps

`—`

## Done when

`Size`/`Usize` are `PrimKind` members with correct `prim_is_int`/`prim_is_signed`/`prim_width` and a
`=> i64` lowering on 64-bit, `usize`/`size` resolve as type names, the ptr↔word crossing is rejected as
implicit, the two fixtures pass, and a `[dry]` build is byte-identical (`src/` still `u64`).
