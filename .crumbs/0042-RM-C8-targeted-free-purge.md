---
seq: 0042
crumb-id: RM-C8
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [RM-C7]
sources:
  - "docs/design/reducao-memoria-arrays-0.3.1.md:267-270"   # §6 crumb C8
  - "docs/design/reducao-memoria-arrays-0.3.1.md:135-182"   # §5.1 region_free + §5.2 purge-on-reassign
  - "docs/design/reducao-memoria-arrays-0.3.1.md:606-617"   # §8 R1/R2 (soundness of the purge)
---

# 0042 · RM-C8 — targeted free + purge-on-reassign (`region_free`; `assign_frees_old`; `CgArenaSym::RegionFree`)

> Expose `exp fn region_free(p, bytes)`, generalize the checker's `assign_frees_old` predicate to
> "reassignment whose old value is an arena-owned backing", add `CgArenaSym::RegionFree`, and emit an
> EAGER free of the previous backing in `emit_assign` — the backstop for the reassigned-accumulator that
> a scope-drop would leave peaking.

## Goal

Arena-per-scope (RM-C6) is the clean route for the transient, but it does NOT help the pathological case:
an accumulator array reassigned MANY times inside a long-lived scope (`a = <new backing>` in a loop),
where waiting for the scope drop still peaks — each old backing lives until the whole scope ends. This
crumb adds the TARGETED backstop: `region_free(p, bytes)` returns a backing to the arena free-list for
immediate reuse (the internal `ar_free_block` already parks with ceil/floor bins), and codegen emits an
eager purge of the PREVIOUS backing on a qualifying reassignment, captured before the overwrite. The
soundness gate is the checker predicate: it frees ONLY when the old backing is a FRESH arena allocation
OWNED by the variable — never a sub-slice, a `str`↔`[]byte` reinterpret, a literal, or a borrowed
parameter (freeing any of those corrupts a neighbor/alias, R2). Byte-mover for the emitted `teko.c`? YES
— it inserts the capture-old/free-eager block into `emit_assign` for qualifying reassignments → a real
emit delta → `fixpoint-rebuild` reseed. Effect: closes the reassigned-accumulator peak (robustness),
holding the peak ≤1.0 GB.

## Where

- `src/runtime/arena.tks` — NEW `exp fn region_free(p: ptr, bytes: u64)` over the existing internal
  `ar_free_block(ar_control(), ptr_word(p) to u64, bytes)` (park with ceil/floor bins; no `teko_rt.c`).
- `src/checker/*` — `assign_frees_old(fn_body, a)` — GENERALIZE from today's `TCall`/`list::push` `@fo`
  case to the predicate "the reassignment's old value is a fresh arena allocation owned by the variable".
- `src/codegen/codegen.tks:6192` — `emit_assign` — emit, BEFORE the store, the capture-old + eager-free
  block for a qualifying reassignment.
- `src/codegen/codegen.tks:250` — `CgArenaSym` — NEW variant `RegionFree` routed by `cg_arena_teko_sym`
  to the Teko `region_free` symbol (and `cg_arena_c_sym` to its C twin during transition).

## How

1. **Expose `region_free`.** The internal `ar_free_block` already parks a backing into the free-list
   (ceil on take / floor on park + paranoid mode). Wrap it (`reducao §5.1`):

```teko
/**
 * region_free — return an array backing to the Teko arena's free-list for immediate reuse. Operates by
 * `load`/`store` over raw mmap memory via `ar_free_block` — never `tk_slice_*`, never `teko_rt.c`. A
 * null pointer is a no-op. The eager purge-on-reassign (RM-C8) calls THIS, and only for a backing the
 * checker proved the variable owns (a fresh arena allocation — not a sub-slice, reinterpret, literal,
 * or borrowed param; freeing those would corrupt an alias, `reducao §8 R2`).
 *
 * @param p      the base pointer of the backing to free (0 = no-op)
 * @param bytes  the backing size in bytes (`len * sizeof(elem)`)
 * @return       void
 * @since 0.3.1
 */
exp fn region_free(p: ptr, bytes: u64)
```

2. **Generalize the predicate.** `assign_frees_old` today fires only for a value that is a
   `TCall`/`list::push` (the `@fo` arm). Broaden it to the load-bearing predicate: the reassignment
   qualifies IFF the OLD backing is (i) a fresh arena allocation (ii) OWNED by the variable — explicitly
   NOT a literal, NOT a sub-slice `s[a..b]`, NOT a `str`↔`[]byte` reinterpret (same memory), NOT a
   borrowed parameter. Conservative: on any doubt, DO NOT free (the scope drop reclaims it later).

```teko
/**
 * assign_frees_old — decide whether reassigning `a` may eagerly free its previous backing (RM-C8). TRUE
 * only when the old value is a fresh arena allocation the variable OWNS; FALSE for a sub-slice,
 * a `str`↔`[]byte` reinterpret, a literal, or a borrowed parameter — freeing any of those would corrupt
 * an alias/neighbor (`reducao §8 R2`, the plan's single most careful point). Conservative by design: on
 * doubt, returns FALSE and lets the scope drop reclaim the backing.
 *
 * @param fn_body  the enclosing function body (ownership/flow facts)
 * @param a        the assignment being lowered
 * @return         true iff the old backing may be eagerly purged
 * @since 0.3.1
 */
fn assign_frees_old(fn_body: []@TStatement(), a: checker::TAssign): bool
```

3. **Emit the eager purge** in `emit_assign` (`:6192`), capturing the old backing BEFORE the overwrite
   (correct sequence), then storing the new backing, then freeing the captured old via the Teko symbol
   `CgArenaSym::RegionFree`:

```
{ ptr _old = <lvalue>.ptr; uint64_t _oldn = <lvalue>.len * sizeof(<elem>);
  <lvalue> = <new backing>;
  teko::mem::region_free(_old, _oldn); }
```

4. **Add `CgArenaSym::RegionFree`.** Route it through `cg_arena_teko_sym` to the mangled Teko
   `region_free` symbol (and `cg_arena_c_sym` to its C twin while both exist) — zero reference to
   `tk_slice_push_*`.
5. **Fixpoint delta.** The emitted `teko.c` gains the capture/free block for qualifying reassignments →
   `gen1 ≠ gen0`, converge `gen2 == gen3`. A predicate that freed a sub-slice/reinterpret would corrupt
   emit and diverge — the fixpoint + MEM_PARANOID are the detectors; HALT and tighten the predicate, do
   not loosen the gate.

## Rulings & laws

- **Teko-only:** `region_free` in `src/runtime/arena.tks` (Teko over mmap), predicate in `src/checker`,
  emission in `src/codegen`; C twins frozen.
- **W15 full Javadoc** on `region_free`/`assign_frees_old`/`emit_assign`; flatten; no inline `//`.
- **Owner R1 (`reducao §8`):** prefer scope-drop; emit purge-on-reassign ONLY where the checker proves
  the variable owns the backing — do NOT turn every `a = <new>` into a free.
- **Owner R2 (soundness, `reducao §5.2`/`§8`):** eager free of a sub-slice / reinterpret / literal /
  borrowed param corrupts an alias — the predicate MUST reject them; conservative-on-doubt is law.
- **Ownership (CLAUDE.md):** the array variable OWNS its backing; UAF after an eager purge is the dev's
  responsibility in the use-design, not the backend's — the backend only frees a proven-owned backing.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  reseed ONLY at the fixpoint; `gen2==gen3` byte-identical; sweep `.tkt`/`.tkr` after the `CgArenaSym`
  and `emit_assign` changes.

## Fixtures

| fixture | asserts | expected |
|---|---|---|
| `region_free_reuse` | `region_free(p, n)` then a fresh `[n]byte = []` reuses the SAME base address (free-list reuse, not new mmap) | 0 |
| `purge_on_reassign_owned` | an owned accumulator reassigned in a loop; the old backing address is reused each turn (bounded footprint) | 0 |
| `purge_skips_subslice` | reassigning a variable whose old value is a sub-slice `s[a..b]` does NOT free (the parent `s` stays valid; read-back intact) | 0 |
| `purge_skips_reinterpret` | reassigning over a `str`↔`[]byte` reinterpret does NOT free (aliased memory intact) | 0 |

These paths are NOT self-build-exercised: the compiler's own reassignments are dominated by the
conservative FALSE case, and the free-list REUSE + the three reject arms are the load-bearing soundness
that the self-build does not force — isolated oracles required.

## Gate

`[fixpoint]` — build gen2 (`--no-verify --release`, `TEKO_BACKEND=c`, `TEKO_CC=clang`,
`ulimit -v 6815744`) + the fixtures + `gen2==gen3` byte-identity + MEM_PARANOID exit 0. "Green" =
`region_free` reuses the free-list, the eager purge fires ONLY for proven-owned backings (the three
reject fixtures compile-and-run intact), the 3-gen ladder converges, and the reassigned-accumulator peak
is closed (≤1.0 GB). Reseed-class: `fixpoint-rebuild`.

## Deps

`RM-C7`.

## Done when

`exp fn region_free` is exposed over `ar_free_block`, `assign_frees_old` fires only for proven-owned
arena backings, `emit_assign` emits the capture-old/eager-free block via `CgArenaSym::RegionFree`, the
four fixtures pass, `gen2==gen3` holds, MEM_PARANOID is clean, and the peak stays ≤1.0 GB.
