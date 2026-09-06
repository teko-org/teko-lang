---
seq: 0054
crumb-id: S16-MM-L2
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [S16-MM-L1]
sources:
  - "docs/design/plano-s16-arena-mmap.md:292-334"   # §5 L2 (FLIP) + memory risk
  - "docs/design/plano-s16-arena-mmap.md:264-289"   # §4.2/§4.3 provider flip + circularity
  - "docs/design/plano-s16-arena-mmap.md:371-382"   # §7 sub-crumb E (L2 flip)
  - "docs/design/plano-s16-arena-mmap.md:407-434"   # §8 fixpoint non-convergence risk
---

# 0054 · S16-MM-L2 — arena switch-over L2: compiler's own runtime IS the Teko-over-mmap arena (load-bearing)

> Flip `cg_arena_sym` to the Teko arena's mangled symbols and RETIRE the `arena_push`/`arena_pop`/
> `arena_commit` builtin injections — now the compiler's own runtime memory management IS the
> Teko-over-mmap arena. The load-bearing reseed: expect a CLEAN 3-gen fixpoint (`tc1==tc2==tc3`).

## Goal

L2 is THE flip: `cg_arena_sym` retargets from the C symbols (`tk_region_alloc`…) to the Teko arena's
namespaced mangled symbols, and the `arena_push`/`arena_pop`/`arena_commit` builtin injections are
retired (closing Defect #4 permanently). Now every allocation the compiler makes while compiling ITSELF
goes through the Teko-over-mmap arena + META-POOL. The circularity is re-confirmed safe: the arena's own
bodies are the allocation-free dialect (only mmap/load/store/word_ptr/ptr_word), so none re-enters the
arena — `region_root()` lazily mmaps the CONTROL block + first region via the META-POOL through syscalls
and raw word writes. Byte-mover for the emitted `teko.c`? The provider flip changes the emitted arena
symbols (a real emit delta) → `fixpoint-rebuild` reseed — but allocation is BEHAVIOR-TRANSPARENT (same
distinct-pointer, same bump/rewind semantics), so the emitted TEXT depends only on compiler LOGIC, not
on which correct allocator backs it: expect a CLEAN 3-gen fixpoint `tc1==tc2==tc3`. This is the most
dangerous ritual in §16 (a subtly-wrong arena corrupts every emitted program incl. the compiler).

## Where

- `src/codegen/codegen.tks:255` — `cg_arena_sym` — flip the provider from `cg_arena_c_sym` (`:282`) to
  `cg_arena_teko_sym` (`:278`): a one-line table change (the L1 indirection made it so).
- `src/checker/scope.tks` — RETIRE the `arena_push`/`arena_pop`/`arena_commit` builtin injections (they
  are superseded by the namespaced Teko arena fns; the retirement closes the name-capture Defect #4).
- `src/runtime/arena.tks` — the Teko arena (core + META-POOL, S16-MM-pool) is now the LIVE provider.

## How

1. **Flip the table** (`§4.2` option b): `cg_arena_sym` returns `cg_arena_teko_sym(kind)` — the Teko
   arena fns' mangled symbols (namespaced, e.g. `teko::rt::arena::region_alloc`). The ~80 emitted
   call-site literals are unchanged (ABI-mirrored in L1); only the resolved symbol changes.

```teko
/**
 * cg_arena_sym — resolve the emitted arena symbol; L2 selects the Teko-over-mmap provider
 * (`cg_arena_teko_sym`), so the compiler's own runtime IS the Teko arena (`plano-s16-arena-mmap §5`
 * L2). The namespaced Teko names do NOT collide with the retired `arena_push`/`arena_pop`/`arena_commit`
 * builtin last-segments (Defect #4 closed). The ABI-mirror (L1) keeps every call-site literal unchanged
 * — this flip is a one-line provider change.
 * @param kind  the arena operation
 * @return      the emitted Teko arena mangled symbol for `kind`
 * @since §16
 */
fn cg_arena_sym(kind: CgArenaSym): str
```

2. **Retire the builtin injections.** Remove the `arena_push`/`arena_pop`/`arena_commit` builtin arms
   from `scope.tks` (now provided by the namespaced Teko fns) — this is a clean expurgo of those three
   injection arms (no tombstone diagnostic), closing Defect #4.
3. **Re-confirm the circularity at the flip** (`§4.3`): the Teko arena bodies are the allocation-free
   dialect, so no `tk_region_alloc`/Teko `region_alloc` appears in the arena's own emitted body; it
   bootstraps via mmap + raw word writes. Verified-shape by the `arena_mmap` regression.
4. **The fixpoint expectation is CLEAN 3-gen** (`§5` L2): gen0 (C-arena binary) emits tc1.c
   (routes-to-Teko-arena); tc1 (Teko-arena binary) emits tc2.c; if the Teko arena is byte-behavior
   -identical, `tc1.c == tc2.c == tc3.c`. Gate HARD on this. If a subtle divergence makes `tc1 ≠ tc2`,
   that is a BUG in the arena surfaced by the fixpoint — HALT and fix; do NOT reseed a non-converged
   arena (a subtly-wrong arena corrupts every emitted program).
5. **Memory risk is top severity** (`§5.1`): MEM_PARANOID already peaks ~5.6 GB against the 6 GB cap. The
   META-POOL reuse (S16-MM-pool) must hold — a header leak or one-mmap-per-header blows the cap. Do NOT
   run MEM_PARANOID and a parallel build together at L2; drop `-g`, one build at a time. A blown guard is
   a root-cause fix (the arena), never a raised ceiling.

## Rulings & laws

- **Teko-only:** the flip + retirement in `.tks`; the C arena symbols remain (as fallback) until L3
  (RM-C9), deleted there, not here.
- **W15 full Javadoc** on `cg_arena_sym`; flatten; no inline `//`.
- **Removals = clean expurgo:** retiring the `arena_push`/`arena_pop`/`arena_commit` builtin arms is a
  clean removal of the injection arms — NO tombstone diagnostic.
- **Fixpoint non-convergence (`§8`):** the 3-gen fixpoint IS the detector; HALT + fix, never reseed a
  non-converged arena; the C arena stays as the fallback until L2 is proven (deletion is L3).
- **P2 seam (maintained-C exception):** the CONTROL word crosses via `tk_arena_control_get/set`
  (`arena.tks:1-3`) — the standing-law `teko_rt.c` exception, no new module-mutable-word surface.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  reseed ONLY at this [RITUAL]-grade fixpoint; `tc1==tc2==tc3` byte-identical; sweep `.tkt`/`.tkr` after
  the flip + builtin retirement.

## Fixtures

none — the fixpoint self-build exercises this. Once flipped, the compiler ALLOCATES EVERYTHING through
the Teko arena while compiling itself, so the 3-gen `tc1==tc2==tc3` byte-identity + a clean MEM_PARANOID
tree + the full native ladder ARE the regression (the most exhaustive exercise possible). The
`arena_mmap` isolated regression (S16-MM-pool) remains as the unit-level correctness oracle.

## Gate

`[fixpoint]` — build the full 3-gen ladder (`--no-verify --release`, `TEKO_BACKEND=c`, `TEKO_CC=clang`,
`ulimit -v 6815744`) expecting `tc1==tc2==tc3` byte-identical + MEM_PARANOID exit 0 + full tree +
`provenance_gate.sh` PASS. This is the RITUAL POINT (`§5` L2 / `§8` ritual 2 — the most dangerous
reseed). "Green" = the compiler's runtime IS the Teko arena, the 3-gen fixpoint is byte-identical, the
memory peak stays under the 6.5 GiB guard, and Defect #4 is closed. Reseed-class: `fixpoint-rebuild`.

## Deps

`S16-MM-L1`.

## Done when

`cg_arena_sym` selects the Teko-over-mmap provider, the `arena_push`/`arena_pop`/`arena_commit` builtin
injections are retired, the compiler self-builds on the Teko arena with `tc1==tc2==tc3` byte-identical,
MEM_PARANOID is clean under the guard, and no arena divergence remains.
