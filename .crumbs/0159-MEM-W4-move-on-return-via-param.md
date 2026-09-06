---
seq: 0159
crumb-id: MEM-W4
milestone: M5
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [MEM-W3]
sources:
  - "docs/design/modelo-de-memoria-por-escopo-0.3.1.md:302-351"       # §5 move-on-return
  - "docs/design/ast-computed-arena-assessment-0.3.1.md:234-323"      # Idea 3 DPS
  - "DECISION_LOG.md:1155"                                            # D130 refinement 1 (param, NOT ambient ret_dest)
  - "src/checker/residence.tks:126-142"                               # plan_return / ReturnResidence (is_move)
  - "src/runtime/arena.tks:941-947"                                   # ambient set_ret_dest/ret_dest (to RETIRE)
---

# 0159 · MEM-W4 — move-on-return via the region PARAMETER; retire ambient `ret_dest` (PARANOID)

> The move flip, done the D130 way: a returned value is built DIRECTLY into the caller's region — which
> arrives as the implicit region PARAMETER (MEM-E4), NOT the ambient `ret_dest`/current-stack. Consume
> `ReturnResidence.is_move`: a `Caller`-tier return allocates into the received region param; the callee's
> own region (a child of the param) drops on the way out. The ambient `set_ret_dest`/`ret_dest`
> (`arena.tks:941`) is RETIRED. The dangerous flip (leak→move) — PARANOID-gated.

## Goal

Refinement 1 rejects the ambient conveyance. The region param IS the caller's region = the return
destination (the unification: one param, not a separate `ret_dest`). At a `Caller`-tier / `is_move`
return (`plan_return`, `residence.tks:126`), the value is constructed into the region param (the caller's
region), so it lives with the caller and dies with the caller's scope — the move. The callee opens its
own child region from the param for its non-escaping locals (MEM-W3), dropped at the edge. Transitive:
N frames each build their return into their received param, so a value bubbles to the top consumer's
region. Non-UAF: the param = the caller's region ⊒ the callee's frame ⊒ every use (stack discipline +
LUB). The ambient `ret_dest` machinery is removed.

## Where

- `src/codegen/codegen.tks` / `src/lir/lower.tks` — at a `Caller`-tier return, construct into
  `region_from_param(ctx)` (the caller's region), not the frame; the return then flips from
  root/ambient-leak to the caller's region.
- `src/runtime/arena.tks:941-947` — RETIRE `set_ret_dest`/`ret_dest` (ambient); the param carries it.
- `src/codegen/codegen.tks:134-135` — the `RetDestSet`/`RetDestGet` CgArenaSym cases retire with the
  ambient path.

## How

1. Consume `ReturnResidence`: `is_move`/`Caller` → build the return value into the region param.
2. The callee's child region (from the param, MEM-W3) holds non-escaping locals, dropped at the edge;
   the moved value is already in the param, so it survives.
3. Retire `set_ret_dest`/`ret_dest` + their CgArenaSym cases.
4. Prove transitive move (N frames bubble to the top consumer's region).

## Rulings & laws

- **Teko-only:** `codegen.tks`/`lower.tks`/`arena.tks`.
- **D130 refinement 1:** the move destination is the region PARAMETER, NOT ambient `ret_dest`/
  current-stack. The ambient dies here.
- **Non-UAF by LUB (§5):** the param (caller's region) ⊒ the callee frame ⊒ all uses; the move allocates
  where the value is used and dies with the caller — the inverse of UAF.
- **W15 full Javadoc; no `//`.** **Safety:** never `teko test .`; subshell `ulimit -v 4718592`;
  `[RITUAL]` gen2==gen3 + **MEM_PARANOID/ASan** (the move is the highest-risk flip) + RSS ratchet.

## Fixtures

Shadow (scratchpad, MEM-S1): `mem_move_return`, `mem_move_transitive`, `mem_no_root_leak` (scoped>0,
unresolved=0), `mem_service_root` (a `service singleton` binding survives its declaring `{}` — NOT
moved/dropped). Non-versioned. The UAF regressor is the self-host under ASan.

## Gate

`[RITUAL]` — gen2==gen3 + **MEM_PARANOID 0 / self-host under ASan** + `TEKO_ARENA_OBS` regions-dropped ≈
scopes-with-locals, root-unresolved=0 except `main`'s frame + RSS ratchet DOWN. "Green" = a returned
value is built into the caller's region param, the ambient `ret_dest` is gone, transitive returns bubble
correctly, `gen2==gen3`, MEM_PARANOID clean. Reseed-class: `fixpoint-rebuild` (sweep; own harvest if ASan
surfaces a UAF).

## Deps

`MEM-W3` (the per-scope regions the callee's child opens from the param).

## Done when

A `Caller`-tier return is constructed into the received region param (the caller's region), the callee's
child region drops without stranding it, transitive N-frame returns bubble to the top consumer, the
ambient `set_ret_dest`/`ret_dest` is retired, and the ritual gate is green under MEM_PARANOID/ASan.

## Region-type reconciliation (D149 / crumbs 0183-0184)

Operates over the `Region` type (0183). The region param is a `Region`; a `Caller`-tier / `is_move` return is
built into `param.alloc(...)`; `region_from_param(ctx)` returns a `Region`. The retired
`set_ret_dest`/`ret_dest` are the ambient slots of the `Arena` type — their retirement composes with the
`Arena` migration (0184). See `docs/design/arena-region-tipo-com-metodos-0.3.1.md §4`.
