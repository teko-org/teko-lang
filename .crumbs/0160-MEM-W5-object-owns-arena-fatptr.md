---
seq: 0160
crumb-id: MEM-W5
milestone: M5
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [MEM-W4]
sources:
  - "DECISION_LOG.md:1156"                                            # D130 refinement 2 (object owns its arena, travels)
  - "docs/design/modelo-de-memoria-por-escopo-0.3.1.md:§0"           # the residence model
  - "src/lir/lower.tks"                                              # fat pointer / aggregate rep
  - "src/codegen/codegen.tks"                                        # C aggregate rep
---

# 0160 · MEM-W5 — object owns its own arena (the fat pointer carries the arena `uptr`, travels with it)

> D130 refinement 2: an object is DONO of its own arena — the fat pointer carries the arena pointer (a
> `uptr`), its members allocate in that arena, and the arena TRAVELS with the object. This is the piece
> that gives threads for free (each receives its region by param → the `CLONE_SETTLS`/`%fs`/D127 blocker
> dissolves). The heaviest, most novel flip — sequenced last, in-plan (not deferred to 0.3.2).

## Goal

Refinement 2: the object carries its arena. The fat pointer of an object gains an arena handle field
(`uptr`, MEM-E0a/E0b — NOT a raw `u64`); the object's members are allocated in THAT arena; when the
object moves (returned via MEM-W4, stored, passed), the arena handle travels with it, so the members stay
valid wherever the object goes. This makes an object self-contained w.r.t. its storage — the basis for
cross-thread objects (an object handed to a spawned task carries its arena; no shared thread-local
control), dissolving the D127 transport blocker.

## Where

- `src/lir/lower.tks` / `src/codegen/codegen.tks` — the aggregate/fat-pointer representation gains the
  arena handle field (`uptr`); member allocation routes to the object's arena; the handle is threaded on
  construction and travels on move/return/store.
- `src/runtime/arena.tks` — the object's arena is a region (the maintained-Teko arena); `region_control`
  (MEM-E1) reaches the control from it (composes with MEM-W6).

## How

1. Extend the object fat pointer with an arena handle field (`uptr`).
2. On object construction, open/assign its arena; member allocations target it.
3. On move/return/store, the handle travels with the object (it is part of the value).
4. Compose with MEM-W4 (the object moves into the caller's region; its arena handle moves with it) and
   MEM-W6 (`region_control` reaches the control via the object's arena → root).

## Rulings & laws

- **Teko-only:** `codegen.tks`/`lower.tks`/`arena.tks`.
- **D130 refinement 2:** the fat pointer carries the arena as `uptr` (NOT `u64` — E0a/E0b); the arena
  travels with the object. This is the thread-dissolution basis (D127).
- **In-plan, not deferred:** the heaviest flip, sequenced LAST in this plan (0.3.1), not pushed to 0.3.2
  (the teach-now/use-later law keeps it in-plan).
- **Non-UAF:** the arena's lifetime ⊇ the object's uses because the arena travels WITH the object — the
  object cannot outlive its own arena.
- **W15 full Javadoc; no `//`.** **Safety:** never `teko test .`; subshell `ulimit -v 4718592`;
  `[RITUAL]` gen2==gen3 + MEM_PARANOID/ASan + RSS ratchet.

## Fixtures

Shadow (scratchpad): an object returned across scopes whose members are read at the top caller (the
arena traveled with it); a churn program that must net to a known fingerprint (corruption = wrong value).
Non-versioned. The UAF regressor is the self-host under ASan.

## Gate

`[RITUAL]` — gen2==gen3 + MEM_PARANOID 0 / ASan + RSS ratchet. "Green" = the object fat pointer carries
its arena `uptr`, members allocate there, the arena travels on move/return, an object's members are valid
at the top caller, `gen2==gen3`, MEM_PARANOID clean. Reseed-class: `fixpoint-rebuild` (sweep).

## Deps

`MEM-W4` (the move that the object's arena travels through).

## Done when

An object's fat pointer carries its arena handle (`uptr`), its members allocate in that arena, the arena
travels with the object on move/return/store, member reads at the top caller are valid, and the ritual
gate is green under MEM_PARANOID/ASan — the D127 thread-transport blocker dissolved.

## Region-type reconciliation (D149 / crumbs 0183-0184) — THIS CRUMB IS THE CORE

W5 is LITERALLY "a região vira tipo fat" (D149). The arena handle the fat pointer carries **IS a `Region`**
(the `addr: uptr` field of the `Region` type, 0183) — D130 refinement 2's fat-region == the `Region` type.
Member allocation = `object.region().alloc(...)`; the handle travels because it is the `Region` value inside
the fat pointer. This stops being "add a raw `u64` field" and becomes "the object carries a `Region`". The
`Region` type (0183) is the vehicle this crumb was always describing; each spawned task receives its `Region`
by param → the D127 blocker dissolves. See `docs/design/arena-region-tipo-com-metodos-0.3.1.md §4`.
