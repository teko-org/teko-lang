---
seq: 0027
crumb-id: S16-MM-wp
milestone: M1
gate: "[dry]"
reseed-class: "(folds R1)"
deps: []
sources:
  - "docs/design/plano-s16-arena-mmap.md:141-167"   # §2.1 word_ptr — inverse of ptr_word
  - "docs/design/plano-s16-arena-mmap.md:169-187"    # §2.2 C-leg load/store gap
  - "docs/design/plano-s16-arena-mmap.md:79-102"     # §1.1 the ptr↔word bridge in the arena core
---

# 0027 · S16-MM-wp — `word_ptr` intrinsic (inverse of `ptr_word`) + C-leg load/store gap fix

> `word_ptr` intrinsic (inverse of `ptr_word`) + C-leg load/store gap fix — the ptr↔integer bridge the
> Teko-over-mmap arena core needs to compile.

## Goal

Close the ptr↔i64 intrinsic gap that blocks the Teko-over-mmap arena core (§16). The arena bumps its cursor
in `u64` and must hand each allocation back AS a `ptr` (so codegen's existing region-handle ABI keeps
compiling), so it needs `word_ptr(w: i64): ptr` — the INVERSE of the already-landed `ptr_word(p: ptr): i64`
(round-trips `word_ptr(ptr_word(p)) == p`). It ALSO fixes the true first blocker: `teko::mem::load_u64` /
`store_u64` (P1) were registered and lowered on the NATIVE leg only, but the arena core is compiled by the
C backend into `teko.c`, so the C leg needs `emit_load_u64`/`emit_store_u64` emitters (`volatile`, for the
same reason the syscall helpers clobber `"memory"`) before the arena core can even COMPILE. Both are
COMPILER INTRINSICS (sanctioned reinterpret carve-outs) — `cast_check` never fights them, the opaque-ptr
law stays intact. Purely ADDITIVE and INERT until the arena switch-over (S16-MM-L1, `0053`, M2) adopts them;
a `[dry]` build is byte-identical. Its seed folds into SM-R1.

## Where

- `src/checker/scope.tks` — NO CURRENT LANDING for `word_ptr` arm (to be added) beside `ptr_word`.
- `src/codegen/codegen.tks` — NO CURRENT LANDING for `emit_word_ptr` (to be added as a special emitter).
- **EXISTING (already landed, native leg only):**
  - `src/checker/scope.tks:664` — `load_u64_signature` registration
  - `src/checker/scope.tks:675` — `store_u64_signature` registration
  - `src/checker/scope.tks:1131-1132` — `load_u64` + `store_u64` intrinsics registered
  - `src/lir/lower.tks:3162-3163` — `is_load_u64_call`/`is_store_u64_call` (native leg, P1)
  - `src/lir/lower.tks:4387-4409` — `lower_load_u64_call` (native lowering)
  - **MISSING (C-leg gap to be filled by this crumb):** C-leg `emit_load_u64`/`emit_store_u64` emitters
    (`volatile` casts for arena-backed load/store); these map to the already-registered native intrinsics.
- `src/runtime/arena.tks:688,693,705,709,722,726,746` — the arena's `word_ptr(… to i64)` call-sites and
  word-load/store sites (the intrinsics' primary consumers once this crumb lands).

NEW: no new module; `word_ptr` intrinsic + `load_u64`/`store_u64` C-leg emitters in the existing checker +
codegen tables. The native signatures and lowering for `load_u64`/`store_u64` already exist.

## How

1. **`word_ptr` — the inverse bridge** (registered at `scope.tks:531`):

```teko
/**
 * word_ptr — bridge an integer word back to an opaque `ptr`, the INVERSE of `ptr_word` (round-trips
 * `word_ptr(ptr_word(p)) == p`). The arena bumps its cursor in `u64`, converts `u64 -> i64` at the boundary
 * (a sanctioned Prim cast), and hands each allocation back AS a `ptr` so codegen's region-handle ABI keeps
 * compiling. A COMPILER INTRINSIC (no surface `u64 -> ptr` cast) — the sanctioned reinterpret carve-out, so
 * `cast_check` never consults `cast_kind` and the opaque-ptr law (`ptr_opaque_error`) stays intact. The
 * opaque result widens to any `ptr<T>` at the use-site via `ptr_widens_to_opaque`. Native leg is Doc-2
 * terminal (honest-stop), exactly as `ptr_word`/`ref_word`.
 *
 * @param w  the integer word to reinterpret as a pointer
 * @return   the opaque `ptr` whose bit-pattern is `w`
 * @since 0.3.1
 */
fn word_ptr(w: i64): ptr
```

2. **Add C-leg emitters for `load_u64`/`store_u64`** (the gap that blocks C-compilation of arena core).
   The `load_u64`/`store_u64` intrinsics ARE registered (scope.tks:664,675,1131-1132) and lowered natively
   (lower.tks:3162-3163, 4387-4409), BUT the C-leg has no emitters yet. The arena core reads back words a
   syscall (mmap) or a sibling store just wrote, over a raw address the optimiser cannot alias-analyze — so
   the emitters use `volatile` (the same reason the syscall helpers clobber `"memory"`). `store_u64` emits as
   a void-expr statement. Add `emit_load_u64`/`emit_store_u64` as special emitters in the `teko::mem` block,
   dispatched by bare last segment. This is the HARD PREREQUISITE — the `arena_teko` reference impl ran
   native-only, which is why the C-leg gap was invisible.
3. **Native leg = honest-stop.** `word_ptr`'s native lowering falls into `lower_call`'s terminal `_ =>` "not
   yet lowered (N2)", exactly as `ptr_word`/`ref_word`; the load/store native lowerings (`LLoad`/`LStore`,
   P1) already exist. The native leg is Doc-2 terminal (NAT-*, M4).
4. **Param type `i64` (mirrors `ptr_word`).** `word_ptr` takes `i64` to mirror `ptr_word`'s `i64` return
   symmetrically; the arena does its arithmetic in `u64` and converts `u64 -> i64` (a sanctioned `Prim`
   cast) at the boundary. Placement in `teko::sys` (next to `ptr_word`) per the `f64_bits` precedent (a
   reinterpret intrinsic need not live in `mem`).
5. **Stay inert.** The arena core adopts `word_ptr`/`load_u64`/`store_u64` at the switch-over (S16-MM-L1,
   `0053`); until then a `[dry]` build is byte-identical.

## Rulings & laws

- **Teko-only:** checker/codegen `.tks`; the C-leg emitters are codegen `.tks` (they emit into `teko.c`);
  no `teko_rt.c` change.
- **W15 full Javadoc** on `word_ptr` and the load/store emitters; flatten/extract; no inline `//`.
- **Opaque-ptr law (`ptr_opaque_error`) intact:** `word_ptr` is a COMPILER INTRINSIC (no surface `u64→ptr`
  cast); result widens via `ptr_widens_to_opaque`, exactly as `ptr_word`/`ref_word`/`f64_bits`.
- **`volatile` on load/store (§2.2):** the arena reads back words mmap/a sibling store just wrote over a
  raw un-aliasable address; the optimiser must not elide/reorder.
- **Additive/inert:** no arena-core caller until S16-MM-L1 → byte-identical.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  reseed only at SM-R1 (`0030`).

## Fixtures

The arena core is not yet on `word_ptr`/`load_u64`/`store_u64` (S16-MM-L1 is the first consumer), so the
round-trip + volatile-load/store paths are NOT self-exercised — isolated oracles required:

| fixture | asserts | expected |
|---|---|---|
| `word_ptr_roundtrip` | `word_ptr(ptr_word(p)) == p` for an arena-shaped pointer; the bit-pattern survives the round-trip | 0 |
| `load_store_u64_volatile` | `store_u64(addr, v)` then `load_u64(addr) == v` over a raw mmap-returned address (C leg); the store is not elided | 0 |

## Gate

`[dry]` — compile + the two fixtures + fixpoint (byte-identical; intrinsics inert until S16-MM-L1). "Green"
= `word_ptr` type-checks and emits the pointer cast on the C leg, `load_u64`/`store_u64` emit volatile C,
`word_ptr(ptr_word(p)) == p`, native leg honest-stops, `[dry]` build byte-identical. Reseed-class:
`(folds R1)`.

## Deps

`—`

## Done when

`word_ptr(w: i64): ptr` and the C-leg `emit_load_u64`/`emit_store_u64` emitters exist (opaque-ptr law
intact, `volatile`), the round-trip `word_ptr(ptr_word(p)) == p` holds, native leg honest-stops, the
fixtures pass, and a `[dry]` build is byte-identical (inert until S16-MM-L1 adopts them).
