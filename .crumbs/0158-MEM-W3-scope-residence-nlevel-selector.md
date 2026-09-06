---
seq: 0158
crumb-id: MEM-W3
milestone: M5
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [MEM-W2]
sources:
  - "docs/design/modelo-de-memoria-por-escopo-0.3.1.md:254-299"       # §4 the 5 scopes, N-level selector
  - "docs/design/transicao-move-on-return-e-seletor-n-niveis-0.3.1.md:132-166" # §2 R_decl selector
  - "DECISION_LOG.md:1165"                                            # D130 refinement 5 (fixed child array, loop reuse)
  - "src/checker/residence.tks"                                       # the Scope tier + binding_is_block_local
  - "src/codegen/codegen.tks"                                         # _tkbr per-scope + emit_list_push frame
  - "src/lir/lower.tks"                                               # native per-scope lifecycle (absent today)
---

# 0158 · MEM-W3 — scope residence + N-level selector; fixed child-region array reused in loops (PARANOID)

> Consume the plan's `Scope` tier: a region per lexical scope (the 5 — block/loop/if/when/fn, treated
> identically), each local dying at its scope edge; the N-level selector routes an accumulator's growth
> to its DECLARING scope's region (not the innermost). D130 refinement 5: the live child regions are a
> FIXED compile-time array (peak = nesting depth ≤ `TK_REGION_STACK_CAP=64`), the slot zeroed on reclaim,
> a LOOP REUSING the same slot (close previous before opening next → flat peak). NO pool/refcount/GC, NO
> dynamic list. This is the dangerous flip that makes memory die per-scope — PARANOID-gated.

## Goal

The model's core (modelo §4): every lexical scope with a non-escaping local opens its region (sized by
`MEM-W2`, elided by `MEM-W1` when `slots==0`) and drops it at the scope edge — per ITERATION for a loop.
The N-level selector (transição §2): an accumulator declared in scope B but grown in an inner loop routes
its growth to B's region (R_decl), never the iteration region (else it dies per-iteration → UAF). The
child-region tracking is the SIMPLE fixed array of refinement 5: `[TK_REGION_STACK_CAP]ptr`, indexed by
nesting depth, slot zeroed on drop, a loop reusing its depth slot (previous closed before next opens) →
the live count is the nesting depth, flat. The native route (`lower.tks`) GAINS the per-scope lifecycle
it lacks entirely today.

## Where

- `src/codegen/codegen.tks` — the `_tkbr`/`_tkfr` open/close already exist; drive them from the plan's
  `Scope` tier (not `want_block`); `emit_list_push`'s `frame` becomes the DECLARING scope's region
  (`scope_region_of`), not `_tkfr`.
- `src/lir/lower.tks` — ADD `open_native_region`/`close_native_region` per scope (mirror `_tkbr`), the
  fixed `region_stack` array in `LowerCtx`, `native_scope_region_of` for the selector; loop = per-iter
  open/close reusing the slot.
- `src/checker/residence.tks` — the `Scope` tier + `binding_is_block_local` already computed; the
  `scope_region_of` name→region map is a deterministic TAST walk both engines share.

## How

1. Both engines walk the TAST in the SAME pre-order, opening a region per `Scope`-tier scope into the
   fixed depth-indexed array; drop at the scope edge (loop: per iteration, reusing the depth slot).
2. Register a `Scope`-tier accumulator's `name → declaring region`; a self-append of `name` routes growth
   there (`scope_region_of` / `native_scope_region_of`); fallback to the frame region (leak-safe) where
   R_decl does not resolve.
3. Fixed array (refinement 5): `[TK_REGION_STACK_CAP]ptr`, zero the slot on reclaim, loop reuses the
   slot — NO dynamic list, NO pool/refcount/GC. Peak live = nesting depth.

## Rulings & laws

- **Teko-only:** `codegen.tks`/`lower.tks`; the region primitives are the maintained-Teko arena (D128).
- **D130 refinement 5:** SIMPLE fixed array, loop-slot reuse — do NOT invent pool/refcount/GC; do NOT use
  a dynamic `push` list; do NOT skip the loop reuse.
- **Non-UAF by LUB:** R_decl dominates every use of the accumulator (all lexically within it) → the
  buffer lives as long as all uses, dies at R_decl's edge (modelo §1 theorem).
- **The 5 scopes identical:** block/loop/if/when/fn use the same open/close; the loop's edge is
  per-iteration.
- **W15 full Javadoc; no `//`.** **Safety:** never `teko test .`; subshell `ulimit -v 4718592`;
  `[RITUAL]` gen2==gen3 + **MEM_PARANOID/ASan** (the UAF detector for this flip) + RSS ratchet.

## Fixtures

Shadow (scratchpad, MEM-S1): `mem_block_dies`, `mem_scope_kinds`, `mem_loop_per_iter` (FLAT peak over 1M
iters — the refinement-5 proof), `mem_str_scope`, `mem_accum_return` (an accumulator that ESCAPES via
return must NOT be scope-classified — the over-classification detector). Non-versioned.

## Gate

`[RITUAL]` — gen2==gen3 + **MEM_PARANOID 0 / self-host under ASan** + `TEKO_ARENA_OBS` scoped>0, live
regions ≈ nesting depth (NOT total executed), loop peak FLAT + RSS ratchet DOWN. "Green" = each of the 5
scopes drops its region at its edge, the selector routes to R_decl, the loop reuses its slot with flat
peak, `gen2==gen3`, MEM_PARANOID clean. Reseed-class: `fixpoint-rebuild` (sweep; if ASan surfaces a UAF,
its own harvest per the expurgo iterative-reseed law).

## Deps

`MEM-W2` (sized regions to open).

## Done when

Every lexical scope drops its region at its edge (loop per-iteration), the N-level selector routes an
accumulator to its declaring region, the live child regions are a fixed depth-array reused in loops
(flat peak), the native route has the lifecycle, and the ritual gate is green under MEM_PARANOID/ASan.

## Region-type reconciliation (D149 / crumbs 0183-0184)

Operates over the `Region` type (0183). The fixed depth-array of live child regions is
`[TK_REGION_STACK_CAP]Region` (was `[…]ptr`); `open_native_region`/`close_native_region` become
`parent.child_sized(region_slots)` / `region.drop()`; `scope_region_of` / `native_scope_region_of` return a
`Region`; the N-level selector routes an accumulator's growth to its declaring `Region`. No loose `region_*`
over `u64`. See `docs/design/arena-region-tipo-com-metodos-0.3.1.md §4`.
