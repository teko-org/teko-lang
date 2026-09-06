---
seq: 0020
crumb-id: RM-C2
milestone: M1
gate: "[dry]"
reseed-class: "(folds R1)"
deps: []
sources:
  - "docs/design/reducao-memoria-arrays-0.3.1.md:234-237"  # C2 crumb
  - "docs/design/reducao-memoria-arrays-0.3.1.md:213-219"  # §5.5 transcribe: mem::copy in Teko
  - "docs/design/reducao-memoria-arrays-0.3.1.md:41-84"    # §2 the foundation already in df63f88c
---

# 0020 · RM-C2 — `mem::copy(dst,at,src)` index-join primitive + the count→`[total]byte=[]`→copy idiom

> `mem::copy(dst,at,src)` index-join primitive + the "count→`[total]byte=[]`→copy" idiom.

## Goal

Provide, in PURE Teko (no `teko_rt.c`, no `from "teko_rt"`), the index-join copy primitive
`mem::copy(dst, at, src)` — a bounded, non-growing `dst[at+i] = src[i]` loop — and establish the
canonical NO-PUSH output idiom it anchors: "count the total → allocate `var final: [total]byte = []`
(zero-fill, one pass) → copy each piece by index." This is the ADDITIVE foundation the codegen emit-buffer
conversion (RM-C3, `0037`, M2) builds on: every `cb`/`append_fo` chain becomes a spread-literal `b"…"` +
`..str` accumulation with `total` and an index-materialized `[total]byte`. It removes NOTHING and adds no
growth primitive (arrays never grow — CLAUDE.md NO PUSHES). It is the M1 seed of the memory campaign; its
seed folds into SM-R1.

## Where

- `src/runtime/arena.tks` (the pure-Teko mem/arena module) — ADD `exp fn copy(dst: ref []byte, at: u64,
  src: []byte)` — a `dst[at+i] = src[i]` loop, no growth, routed over the arena-backed `[]byte`
  (`{ptr, len}`); NO `from "teko_rt"`.
- Codegen/idiom sites (documented, not yet converted here) — the "count → `[total]byte=[]` → copy" idiom
  RM-C3 adopts: `var final: [total]byte = []` (zero-fill `memset` inline, already emitted) + index copy.
- `[n]T = []` array-of-runtime-size — already emits `memset`-zero inline (§5.5); no transcription needed.

## How

1. **Add the index-join copy primitive** (pure Teko):

```teko
/**
 * copy — index-join copy of `src` into `dst` starting at offset `at`: `dst[at + i] = src[i]` for every
 * byte of `src`, with NO growth (the caller sized `dst` to hold the total). The array-append-free
 * building block of the no-push output idiom — count the total length, allocate `var final: [total]byte
 * = []` (zero-fill, one pass), then `copy` each piece into its exact index range. Operates over the
 * arena-backed `[]byte` `{ptr, len}`; issues NO `tk_slice_push*`, and no `from "teko_rt"` — it is a bare
 * bounded loop the compiler lowers directly.
 *
 * @param dst  the destination byte array, already sized to hold `at + src.len` (a `ref []byte`, a
 *             position-pointer only — never grown or reassigned, per the array law)
 * @param at   the starting index in `dst` to copy into
 * @param src  the source bytes to copy
 * @since 0.3.1
 */
exp fn copy(dst: ref []byte, at: u64, src: []byte)
```

2. **Document the count→alloc→copy idiom** the primitive anchors (the one RM-C3 adopts): each output
   piece is a spread-literal with `b"…"` for the literals and `..str` direct for the dynamics
   (`outs[i] = [..b"#define TK_ARENA_", ..suffix, b' ', ..sym, b'\n']`); accumulate `total`; allocate
   `var final: [total]byte = []`; copy by index (`copy(final, k, piece); k += piece.len`). The compiler
   may const-fold the fully-known pieces to a pure literal.
3. **No growth, no `teko_rt`.** `copy` issues no `tk_slice_push*`; `[n]T = []` already `memset`-zeroes
   inline (§5.5). The `ref []byte` is a POSITION-POINTER only (CLAUDE.md) — `copy` writes in place; it
   never grows or reassigns `dst`.
4. **Remove nothing.** RM-C2 is purely additive (the C-root removals are RM-C8/RM-C9, M2/M3). Seed it so
   the sweep consumers (RM-C3+) can use the new idiom against a seed that already knows `copy`.
5. **Confirm byte-neutrality.** `src/` does not yet call `copy` (RM-C3 is the first consumer), so a `[dry]`
   build is byte-identical; the primitive is inert until adopted.

## Rulings & laws

- **Teko-only, NO `teko_rt.c`:** `copy` is pure Teko over the arena-backed `[]byte`; NO new `from
  "teko_rt"` (CLAUDE.md "NADA em teko_rt.c PRO EXPURGO", §5.5 "mem::copy em Teko").
- **W15 full Javadoc** on `copy` (it is `exp` — stdlib surface); no `//`.
- **NO PUSHES / ZERO dynamic growth (CLAUDE.md):** arrays never grow; `copy` is a bounded index write,
  not a growth primitive. `ref []byte` = position-pointer only (never grown/reassigned).
- **`exp` = user value (CLAUDE.md):** `copy` is a genuinely useful stdlib primitive → `exp`.
- **Additive:** removes nothing; the C-root purge is RM-C8/C9.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step.

## Fixtures

`src/` does not yet call `copy` (RM-C3 is the first consumer), so its behavior is NOT self-exercised — one
isolated behavior oracle (and a memory-shadow the RM-C1/RM-C3 campaign reuses):

| fixture | asserts | expected |
|---|---|---|
| `mem_copy_index_join` | `copy(dst, at, src)` writes `src` into `dst[at..]` with no growth; result bytes correct | 0 |
| `count_alloc_copy_idiom` | the count→`[total]byte=[]`→copy idiom builds a large buffer with a flat, non-growing peak | 0 |

## Gate

`[dry]` — compile + the two fixtures + fixpoint (byte-identical; `copy` inert until adopted). "Green" =
`copy` performs the bounded index-join with no growth and no `teko_rt` reference, the idiom builds a
buffer flat, `[dry]` build byte-identical. Reseed-class: `(folds R1)`.

## Deps

`—`

## Done when

`exp fn copy(dst: ref []byte, at: u64, src: []byte)` exists in pure Teko (no `from "teko_rt"`) as a
bounded non-growing index-join, the count→`[total]byte=[]`→copy idiom is documented and proven flat, the
two fixtures pass, and a `[dry]` build is byte-identical (primitive inert until RM-C3 adopts it).
