---
seq: 0021
crumb-id: COL-F0a
milestone: M1
gate: "[dry]"
reseed-class: "(folds R1)"
deps: [RM-C2]
sources:
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:104-159"   # FASE 0 teaching items 1-2
  - "docs/design/colecoes-remodelagem-backing-fixo-0.3.1.md:120-151"     # §0 anchor-form + of_len law
  - "docs/design/colecoes-remodelagem-backing-fixo-0.3.1.md:482-522"     # §2.7 place/read/write + bucket contract
---

# 0021 · COL-F0a — `of_len` + `place`/`read`/`write` + `bucket` fixed-backing intrinsics

> `of_len` + `place`/`read`/`write` + `bucket` fixed-backing intrinsics — the VALUE regime the whole
> collection library rests on.

## Goal

Teach the compiler the first slice of the FASE-0 collection surface: the fixed-array anchor form
(`of_len<T>(n): []T` = one `memset`-zero pass + the `count` watermark idiom) and the VALUE-regime
intrinsics `place<T>(v): *T` / `read<T>(p): T` / `write<T>(p, x)` plus the **bucket** mark (reassign /
remove = mark-dead-only; the physical byte leaves only at the region bulk-drop, the SEALED life model
`mudancas-superficie-0.3.1.md:1742` item C). This is the substrate every growable collection is remodelled
onto (the pointer-index model §2, the chunk-chain base Q1): a value is written ONCE into the collection's
bump and never recopied by a grow — the pointer index grows, not the values, which is what closes the v1
value-copy leak (§0.5). Purely ADDITIVE and INERT: nothing existing is removed (the old
`empty`/`push`/`with_cap`/`grow_inplace` at `typer.tks:805-844` keep working — their removal is COL-F2,
`0093`, M3), and `src/` does not yet call the new intrinsics, so a `[dry]` build is byte-identical. Its
seed folds into SM-R1 (`0030`) — it is NOT a separate reseed.

## Where

- `src/checker/typer.tks:805-844` — the `teko::list` builtin arm (`empty`/`push`/`with_cap`/`grow_inplace`
  today) — ADD the type rules for `of_len`/`place`/`read`/`write` beside it; touch NOTHING in the existing
  four arms (their removal is COL-F2).
- `src/checker/scope.tks:265` — `byte_ptr_signature` neighbourhood (`builtin_fn`) — register the new
  intrinsic signatures next to `buf_ptr`/`byte_ptr`/`word_ptr`.
- `src/codegen/codegen.tks:3339` — `emit_slice_of_len` (the sized-array `[n]T = []` materializer already
  exists) — reuse it for `of_len<T>(n)`; ADD `emit_place`/`emit_read`/`emit_write` special emitters in the
  `teko::mem` emitter block (the `codegen.tks:2589` `emit_byte_ptr` neighbourhood).
- `src/lir/lower.tks:1376` — `lower_addr_of_place` — `place`/`read`/`write` reuse the addr-of-index
  machinery; the native leg HONEST-STOPS in `lower_call`'s terminal `_ =>` (Doc-2 terminal), like the
  existing `ptr_word`/`word_ptr`.
- `src/runtime/arena.tks:685,696` — `region_alloc`/`region_drop` — `place` bumps the collection region;
  the bucket mark reclaims nothing mid-region (crumb-D intact).

NEW: no new module; all four intrinsics are builtin calls registered in the existing checker/codegen
tables (no new surface syntax).

## How

1. **Confirm the `of_len` anchor form.** The sized-array literal `var xs: [n]T = []` already lowers through
   `emit_slice_of_len` (`codegen.tks:3339`) as one `memset`-zero pass, and `TSliceOfLen` exists in the
   checker (`comptime_expand.tks:170`, `comptime_fold.tks:899`). COL-F0a exposes it as the named builtin
   `of_len<T>(n)` so the library reads uniformly; it teaches the ADD side only (the REMOVE of
   `empty`/`push`/`with_cap`/`grow_inplace` is COL-F2, `0093`).

```teko
/**
 * of_len — allocate a FIXED `[]T` of exactly `n` zero-filled slots in one `memset` pass (the anchor form
 * CLAUDE.md legislates: header `{ptr, len}`, no `cap`, no tag). Presence is tracked by the caller's `count`
 * watermark (`count <= n`), never by growth: the array never resizes in place. Backs the pointer-index and
 * chunk-chain models — a value written by `place` lives here once and is never recopied by a grow.
 *
 * @param n  the exact number of slots (known at this allocation, per F1 fixed-arrays)
 * @return   a fixed `[]T` of `n` zero-filled slots
 * @since 0.3.1
 */
fn of_len<T>(n: u64): []T
```

2. **Add the VALUE-regime intrinsics** (`place`/`read`/`write`), registered in `scope.tks::builtin_fn`
   beside `byte_ptr` and emitted in the `teko::mem` block:

```teko
/**
 * place — write the VALUE `v` ONCE into the collection's bump region and return a stable pointer to it.
 * The value never moves again: a grow copies POINTERS (the index), never the value, so no dead value-array
 * is stranded in the bump (the v1 leak §0.5 cannot form). Only VALUE-struct-large uses it; a scalar that
 * fits a word is stored inline in the index; an OBJECT (`class`) is never placed — it lives in its own
 * per-object region and the collection stores only its pointer.
 *
 * @param v  the value to write once into the collection's bump
 * @return   a stable pointer to the placed value (valid until the collection's region drops)
 * @since 0.3.1
 */
fn place<T>(v: T): *T

/**
 * read — dereference `p` and copy the VALUE out (the read-by-copy the owner ruled: a `get` never aliases
 * the collection's internal storage for a VALUE element). The returned copy is independent — mutating it
 * does not touch the slot.
 *
 * @param p  a pointer previously returned by `place`
 * @return   an independent copy of the pointed-to value
 * @since 0.3.1
 */
fn read<T>(p: *T): T

/**
 * write — the VALUE `set(i, x)`: for a scalar-inline element overwrite the slot in place (a clean memory
 * overwrite, nothing to reclaim); for a VALUE-struct-large element `place` the new value and mark the old
 * slot DEAD (a bucket), reclaimed only at the region bulk-drop. NEVER a mid-region free (crumb-D intact).
 *
 * @param p  the slot pointer to overwrite
 * @param x  the new value
 * @since 0.3.1
 */
fn write<T>(p: *T, x: T)
```

3. **Legislate the bucket mark (no primitive).** Reassign / `remove` / discard of a VALUE slot is
   mark-dead-ONLY — it lowers the watermark (or shifts pointers); there is NO per-entry free. The physical
   byte leaves only at `region_drop` of the collection's region (the SEALED item C,
   `mudancas-superficie-0.3.1.md:1742`). The sole exception is `pop`, whose RETURN travels to the caller by
   normal DPS/move-on-return while the vacated slot becomes a bucket (`return→DPS; slot→bucket`).
4. **Native leg = honest-stop.** `place`/`read`/`write` reuse `lower_addr_of_place` on the C leg; the
   native leg falls into `lower_call`'s terminal `_ =>` "not yet lowered (N2)", exactly as
   `ptr_word`/`word_ptr` do — the native lowering is Doc-2 terminal, addressed by the NAT-* wave (M4).
5. **Remove nothing; stay inert.** The four legacy list builtins (`typer.tks:805-844`) are UNTOUCHED; the
   new intrinsics have no `src/` caller yet (FASE 1, M2, is the first consumer), so a `[dry]` build is
   byte-identical.

## Rulings & laws

- **Teko-only:** checker/codegen/lir `.tks`; the arena is pure Teko (`region_alloc`/`region_drop`); NO
  `teko_rt.c` (the maintained-C seed is untouched — no new atomic needed here).
- **W15 full Javadoc** on every new intrinsic (pub + private); flatten/extract; no inline `//`.
- **F1 fixed-arrays (SEALED):** an array is FIXED — `of_len` is a fresh fixed allocation, never a resize;
  "capacity" lives only in the wrapper's `count` watermark.
- **Bucket / no mid-region reclaim (SEALED `:1742` item C):** reassign/remove = mark-only; physical free at
  the region bulk-drop; the bump arena gains NO mid-region reclaim (crumb-D intact).
- **NO PUSHES / ZERO dynamic growth (CLAUDE.md):** the VALUE regime writes each value once by index; no
  growth primitive is introduced.
- **Additive/inert:** removes nothing (COL-F2 is the expurgo); no `src/` caller yet → byte-identical.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  reseed only at SM-R1 (`0030`).

## Fixtures

The self-build FIXPOINT exercises `of_len`/`place`/the class-holder path as soon as FASE 1 (`0077` COL-Q9)
migrates the compiler's own `Map`/`List` — so NO happy-path fixture for the VALUE regime. The one path the
compiler never walks itself (a large VALUE-struct that must be `place`d, not inlined) needs an isolated
oracle:

| fixture | asserts | expected |
|---|---|---|
| `of_len_zero_fill` | `of_len<i64>(n)` returns `n` zero-filled slots; index-assign + `count` watermark; no growth | 0 |
| `value_place_once` | a large value-struct is `place`d once; a forced grow copies pointers only (a `place`-counter == element count, values never recopied) | 0 |
| `value_slot_bucket` | reassign/remove of M VALUE slots does ZERO mid-region reclaim; the bump falls only at the region drop; token == M | 0 |

## Gate

`[dry]` — compile + the three fixtures + fixpoint (byte-identical; intrinsics inert until FASE 1 adopts
them). "Green" = `of_len`/`place`/`read`/`write` type-check and emit on the C leg, the bucket mark reclaims
nothing mid-region, the four legacy list builtins still compile, `[dry]` build byte-identical.
Reseed-class: `(folds R1)`.

## Deps

`RM-C2` (the `mem::copy` index-join primitive + count→`[total]byte=[]`→copy idiom the VALUE materialization
reuses).

## Done when

`of_len<T>(n)` + `place`/`read`/`write` + the bucket mark are registered in the checker and emit on the C
leg (native = honest-stop), the four legacy list builtins are untouched, the fixtures pass, and a `[dry]`
build is byte-identical (intrinsics inert until COL-Q9 adopts them).
