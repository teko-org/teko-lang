---
seq: 0148
crumb-id: MEM-E0a
milestone: M5
gate: "[dry]"
reseed-class: "fixpoint-rebuild"
deps: []
sources:
  - ".crumbs/0015-SM-G9-size-usize-primkind.md"                        # the recovered design (size/usize prim)
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:802-891"   # §7b machine-word ints + reball
  - "DECISION_LOG.md:1166"                                             # D130 addendum — isize/usize rename
  - "src/checker/type.tks:2-37"                                        # PrimKind already has Size/Usize; prim_width hardcodes 64
---

# 0148 · MEM-E0a — `isize`/`usize` arch-word size/index types (recover 0015, rename + target-param)

> Recover the deliberated `size`/`usize` design (`0015-SM-G9`), applying the D130 correction: the SIGNED
> machine word is named **`isize`** (NOT `size` — `size` collides with a widely-used identifier), and
> `prim_width` becomes TARGET-parameterized (64 now, 32 on a 32-bit target). Inert/byte-identical until
> the reball adopts it. NOT new design — recovery + rename.

## Goal

`isize` (signed machine word) / `usize` (unsigned machine word), pointer-sized and TARGET-DEPENDENT, are
the natural integer types for the arena/slice/pointer machinery — array sizes, slot counts, indices,
offsets. **State (verified):** `PrimKind` ALREADY carries `Size; Usize` (`type.tks:7`) with
`prim_is_int`/`prim_is_signed` wired, but (a) the signed member is spelled `size`, which the owner flagged
as a collision, and (b) `prim_width` HARDCODES `64` for both (`type.tks:37`) — no target-dependence. This
crumb (i) renames the surface `size`→`isize` (internal `Size`→`Isize`), (ii) recovers `0015`'s
`prim_width(k, target)` so `Usize`/`Isize` return the TARGET word width. On the 64-bit fixpoint targets
`usize==u64`/`isize==i64` bit-for-bit, so it lowers to IDENTICAL bytes → byte-identical `[dry]`; `src/`
still says `u64` (the mass reball is `0034 SM-S6` / `MEM-W6`), so nothing is self-exercised yet.

## Where

- `src/lexer/lexer.tks` — `isize`/`usize` are NEW primitive TYPE NAMES (resolved as `PrimKind` cases like
  `u64`, NOT reserved keywords). Retire any `size` type-name spelling; `size` stays a free identifier.
- `src/checker/type.tks:7` — rename `Size`→`Isize` in `PrimKind`; keep `Usize`.
- `src/checker/type.tks:18,26,31-37` — `prim_is_int(Isize|Usize)=true`; `prim_is_signed(Isize)=true`,
  `(Usize)=false`; `prim_width` → target word width (the ONE target-dependence port).
- `src/checker/type.tks:177` region — keep `Uptr` same-kind-only equality; `Isize`/`Usize` are ordinary
  int-family prims that `to`-convert with the other ints; `uptr↔usize` is NOT a plain `to`-cast (E0b).
- `src/lir/lower.tks` / `src/codegen/codegen.tks` — the prim→machine-type table: `Usize => i64`,
  `Isize => i64` on a 64-bit target (same emit as `U64`/`I64`).
- `src/checker/resolve.tks` (`builtin_type`/type-name resolution) — map the names `isize`/`usize`.

## How

1. Rename `Size`→`Isize` tree-wide (internal enum + every `match` arm); the surface name is `isize`.
2. Target-parameterize `prim_width`:

```teko
/**
 * prim_width — the bit width of a primitive, target-parameterized for the machine-word kinds: `Isize`
 * and `Usize` return the TARGET word width (64 on the x86_64/arm64 fixpoint targets, 32 on a 32-bit
 * target). This is the ONLY place target-dependence enters the type; every other prim has a fixed
 * width. On the 64-bit fixpoint targets `Usize`/`Isize` share `U64`/`I64`'s width bit-for-bit, so a
 * `u64`→`usize` reball is byte-preserving on the fixpoint.
 *
 * @param k       the primitive kind
 * @param target  the compilation target (its machine-word width)
 * @return        the bit width of `k` on `target`
 * @since 0.3.1
 */
pub fn prim_width(k: PrimKind, target: checker::Target): u32
```

3. Wire the lowering table: `Usize => i64`, `Isize => i64` on 64-bit — identical emit to `u64`/`i64`.
4. Keep the kinds distinct from `uptr` (§7b.2): `usize`/`isize` = measure/position; `uptr`/`ptr` =
   address. `usize↔u64` is `to`-cast; `uptr↔usize` is a Marshall bridge, never implicit (E0b).
5. Confirm inert: `src/` still says `u64` → byte-identical `[dry]`.

## Rulings & laws

- **Teko-only:** lexer/checker/lir/codegen `.tks`; no C twin.
- **Recover, do NOT invent:** `0015`'s design verbatim except the `size`→`isize` rename + the
  `prim_width(k,target)` target-parameterization (both D130-directed).
- **`isize`/`usize` are type NAMES, not keywords;** `size` remains a free identifier (the collision the
  rename resolves).
- **Byte-preserving on 64-bit (§7b.5):** `Usize`/`Isize` lower to the same machine type as `U64`/`I64`.
- **W15 full Javadoc; no `//`.** **Safety:** never `teko test .`; subshell `ulimit -v 4718592`.

## Fixtures

`src/` still uses `u64` (reball is later), so these are isolated oracles (a rejection oracle is allowed):

| fixture | asserts | expected |
|---|---|---|
| `usize_len_index` | `xs.len: usize`, `xs[i: usize]` type-check; `usize` lowers = `u64` on 64-bit | 0 |
| `isize_signed_delta` | `isize` is signed, holds a negative offset; lowers = `i64` on 64-bit | 0 |
| `usize_uptr_not_implicit` | a `usize` where a `uptr` is expected (and vice-versa) is REJECTED | EXPECT_COMPILE_FAIL |

## Gate

`[dry]` — compile + the three oracles + fixpoint (byte-identical; `src/` still `u64`). "Green" =
`isize`/`usize` resolve as prims, predicates+width correct, `prim_width` reads the target, the ptr↔word
crossing is rejected as implicit, `usize`/`isize` lower identically to `u64`/`i64` on 64-bit, `[dry]`
byte-identical. Reseed-class: `fixpoint-rebuild` (folds into the RESEED-1 harvest of `MEM-E5`).

## Deps

`—` (independent teaching leaf; batches with E0b/E1/E2/E3).

## Done when

`Isize`/`Usize` are `PrimKind` members with correct predicates and a target-parameterized `prim_width`
(`=> i64` on 64-bit), `isize`/`usize` resolve as type names, `size` stays a free identifier, the ptr↔word
crossing is rejected as implicit, the oracles pass, and a `[dry]` build is byte-identical.
