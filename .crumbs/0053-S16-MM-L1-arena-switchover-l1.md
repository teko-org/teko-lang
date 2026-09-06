---
seq: 0053
crumb-id: S16-MM-L1
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [S16-MM-pool]
sources:
  - "docs/design/plano-s16-arena-mmap.md:240-290"   # §4 the switch-over (ABI-mirror, codegen-retarget)
  - "docs/design/plano-s16-arena-mmap.md:292-318"   # §5 L0/L1 of the ladder
  - "docs/design/plano-s16-arena-mmap.md:351-382"   # §7 sub-crumb E (L0 then L2)
---

# 0053 · S16-MM-L1 — arena switch-over L1: Teko-over-mmap arena behind unchanged call-sites (shim indirection)

> Introduce the `cg_arena_sym` indirection (L0 refactor emitting the SAME `tk_*` strings, byte-inert) and
> land the Teko arena behind it as add-alongside — codegen STILL calls the C `tk_region_alloc`, but the
> single-point retarget is now in place so the L2 flip (`0054`) is a one-line table change.

## Goal

The arena backs the compiler's OWN runtime, so the flip is a deliberate multi-step ladder, not a single
shot. This crumb lands the L0+L1 steps: (L0) route the ~15 emitted arena symbol literals in codegen
through ONE constant table (`cg_arena_sym(kind)`) that today returns the SAME `tk_region_alloc`… strings
— a pure refactor whose only emit effect is the ACHADO-A temp-ID shift (behavior-inert), so it is a clean
reseed; (L1) the Teko-over-mmap arena (core + META-POOL, S16-MM-pool) sits behind the table as
add-alongside, ABI-mirrored to the C call surface (`region_alloc(r: ptr, n: u64): ptr` ↔ `void*
tk_region_alloc(void*, uint64_t)`), but codegen STILL resolves the table to the C symbols. The compiler
therefore still RUNS on the C arena — memory behavior unchanged, low risk. Byte-mover for the emitted
`teko.c`? The L0 indirection shifts temp IDs (a real emit delta) → `fixpoint-rebuild` reseed, but the
arena the compiler uses is unchanged. This de-risks the load-bearing L2 flip to a one-line change.

## Where

- `src/codegen/codegen.tks:255` — `cg_arena_sym(kind: CgArenaSym): str` — the single indirection; L0
  emits the SAME `tk_*` strings via `cg_arena_c_sym` (`:282`); L1 keeps the C provider selected.
- `src/codegen/codegen.tks:278` — `cg_arena_teko_sym` — the Teko-arena mangled symbols, present but NOT
  selected yet.
- `src/runtime/arena.tks` — the ABI-mirrored `exp` arena fns (`region_alloc`/`region_new`/`region_drop`/
  … over `ptr`+`u64`), authored in the allocation-free dialect; landed but unwired.

## How

1. **L0 — the sym-table indirection (inert reseed).** Route every emitted arena symbol literal in
   codegen through `cg_arena_sym(kind)`, which returns the SAME `tk_region_alloc`… strings via
   `cg_arena_c_sym`. This is a pure refactor: the emitted `teko.c` is textually identical MODULO the
   ACHADO-A temp-ID shift (adding the indirection fn). Clean reseed.

```teko
/**
 * cg_arena_sym — resolve the emitted C symbol for an arena operation through ONE indirection table,
 * so the switch-over (L2, `0054`) is a one-line provider flip, not ~80 call-site edits
 * (`plano-s16-arena-mmap §4.2` option b). L0/L1 select the C provider (`cg_arena_c_sym` → the SAME
 * `tk_region_alloc`… strings the codegen emitted before), so the arena the compiler runs on is
 * unchanged; L2 flips this to `cg_arena_teko_sym`.
 * @param kind  the arena operation (RegionAlloc, RegionNew, RegionDrop, …)
 * @return      the emitted C symbol name for `kind`
 * @since §16
 */
fn cg_arena_sym(kind: CgArenaSym): str
```

2. **L1 — the Teko arena ABI-mirrored, add-alongside.** The Teko arena fns are authored to signature
   -mirror the C ABI (`region_alloc(r: ptr, n: u64): ptr`, etc.), so the ~80 emitted call-site literals
   need NO textual change when the provider flips — only the DEFINITION provider changes. Inside each
   body: `ptr_word(r)` → the `u64` the arithmetic uses; `word_ptr(addr)` → the returned `ptr`.
3. **Still the C provider.** `cg_arena_sym` continues to return the C symbols; the compiler still runs on
   the C arena. The Teko arena is COMPILED into the self-image but UNUSED by the corpus (proven by the
   S16-MM-pool `arena_mmap` regression on the C leg). Low-risk reseed.
4. **Fixpoint.** The L0 indirection shifts temp IDs → `gen1 ≠ gen0`; converge `gen2 == gen3`
   byte-identical. Because the arena behavior is unchanged, the ONLY delta is the temp-ID shift — verify
   the emitted `teko.c` is otherwise identical byte-for-byte (a real behavior delta here would be a bug).

## Rulings & laws

- **Teko-only:** codegen indirection + arena fns in `.tks`; the maintained-C exception is only the P2
  CONTROL seam; the C arena symbols are DELETED later (L3 / RM-C9), not here.
- **W15 full Javadoc** on `cg_arena_sym` and the ABI-mirror arena fns; flatten; no inline `//`.
- **Codegen-retarget over symbol-alias (`§4.2`):** the single-table indirection is the ratified route;
  the symbol-alias route is REJECTED (fragile; Defect #4 name-capture on `arena_push`/`arena_pop`/
  `arena_commit`). The flip also RETIRES those builtin injections at L2, closing Defect #4.
- **Ladder discipline (`§5`):** L0 (inert) then L1 (add-alongside) BEFORE L2 (flip); the C arena stays
  as the fallback until L2 is proven — do NOT delete C symbols here.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit L0 and L1 as
  separate green steps; reseed ONLY at the fixpoint; `gen2==gen3` byte-identical; sweep `.tkt`/`.tkr`
  after the indirection.

## Fixtures

none — the fixpoint self-build exercises this. L0 is a pure emit-refactor whose correctness IS
`gen2==gen3` byte-identity (modulo the sanctioned temp-ID shift); L1's Teko arena is proven by the
S16-MM-pool `arena_mmap` regression (already landed). No new isolated `.tkr` adds coverage the self-build
+ that regression lack.

## Gate

`[fixpoint]` — build gen2 (`--no-verify --release`, `TEKO_BACKEND=c`, `TEKO_CC=clang`,
`ulimit -v 6815744`) + scoped regression (incl. `arena_mmap`) + `gen2==gen3` byte-identity. "Green" =
`cg_arena_sym` routes every arena symbol, the emitted `teko.c` is byte-identical except the temp-ID
shift, the compiler still runs on the C arena, MEM_PARANOID exits 0, and the 3-gen ladder converges.
Reseed-class: `fixpoint-rebuild`.

## Deps

`S16-MM-pool`.

## Done when

`cg_arena_sym` is the single arena-symbol indirection selecting the C provider, the ABI-mirrored Teko
arena is compiled add-alongside (unwired), the compiler still runs on the C arena, and `gen2==gen3`
holds (modulo the temp-ID shift) — leaving the L2 flip a one-line table change.
