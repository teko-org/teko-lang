---
seq: 0161
crumb-id: MEM-W6
milestone: M5
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [MEM-W5]
sources:
  - "DECISION_LOG.md:1164"                                            # D130 refinement 6 (root born in _start)
  - "DECISION_LOG.md:1155-1157"                                       # D130 refinement 1 + tangle dissolution
  - ".crumbs/0125-RT-ENTRY-start-abi-per-os.md"                       # the _start entry
  - "src/runtime/arena.tks:291,769-947"                              # ar_control first-touch + ambient region/ret_dest
  - ".crumbs/0034-SM-S6-reball-usize-size.md"                        # the mass position reball
  - ".crumbs/0091"                                                   # SM-S4 FFI→opaque ptr reball
---

# 0161 · MEM-W6 — root born in `_start`→`main` param; remove ambient; reball positions/pointers — RESEED-FINAL

> The terminal sweep. The root region is BORN in `_start` (0125) and passed to `main` as the region
> parameter — `slots==0` is uniform (`main`'s parent is `_start`). This SUBSTITUTES the lazy first-touch
> `ar_control()`. Then REMOVE the ambient machinery: the `_Thread_local` control first-touch,
> `region_enter`/`region_leave`, `ar_cur_*`, retiring `ar_control()`→`region_control(<param>)`. Fold the
> model's slice of the reball (positions→`usize`, raw pointers→`ptr`/`uptr`, `str`↔`[]byte` zero-copy).
> This is the RESEED-FINAL of the campaign.

## Goal

Refinement 6: the root nasce no `_start`, not lazily in `main`. `_start` (0125) opens the root region and
passes it to `main` as the region param — so `main` is not special (its parent is `_start`, like any
scope receives the parent's region). This replaces the `ar_control()` first-touch (`arena.tks:291`).
With the root threaded and `region_control` (MEM-E1) reaching the control from any region, the AMBIENT
dies: `region_enter`/`region_leave` (`arena.tks:769`), `ar_cur_*`, the `_Thread_local` control, and the
already-retired `ret_dest` (MEM-W4). Every `ar_control()` site (all 40 in `arena.tks`, a veneer over
`ar_*_w(control,…)`) reroutes to `region_control(<region-param>)`. Finally, adopt the model's reball: the
memory positions become `usize` (E0a), raw pointer words become `ptr`/`uptr` (E0b), and `str_of_bytes`/
`bytes_of_str` become the zero-copy identity reinterpret (the E0b flagship) — the fat-pointer arena field
is `uptr` (MEM-W5), region params are `ptr`. Byte-preserving on 64-bit → fixpoint-gated.

## Where

- `.crumbs/0125` (`_start`) — open the root region in `_start`; pass it to `main` as the region param.
- `src/runtime/arena.tks:291` — RETIRE the `ar_control()` `_Thread_local` first-touch; `ar_control()`
  callers reroute to `region_control(<param>)` (the 40 sites, veneer over `ar_*_w`).
- `src/runtime/arena.tks:769-947` — REMOVE `region_enter`/`region_leave`/`ar_cur_*` (ambient
  current-stack); `set_ret_dest`/`ret_dest` already retired (MEM-W4).
- `src/codegen/codegen.tks:126-127` — retire the `RegionEnter`/`RegionLeave` CgArenaSym cases.
- `src/`+`.tkt` — the model's reball: memory positions `u64`→`usize`, raw pointer `u64`/`i64`→`ptr`/`uptr`
  (source positions `line`/`col` stay `u32`); `str_of_bytes`/`bytes_of_str`→identity reinterpret.
  (Cross-references the mass reball `0034 SM-S6` / `0091 SM-S4`; the model's slice lands here.)

## How

1. `_start` opens the root region and stores the control in it; passes it to `main` as the region param.
2. Reroute the 40 `ar_control()` sites to `region_control(<region-param>)`; the `_Thread_local` control is
   removed (D130 refinement 1: no thread-local, no global var, no tid-table).
3. Remove `region_enter`/`region_leave`/`ar_cur_*` (ambient current-stack) — the param replaces them.
4. Reball the model's positions/pointers to `usize`/`ptr`/`uptr`; `str_of_bytes`/`bytes_of_str` become
   identity reinterprets (E0b flagship, zero copy).
5. RESEED-FINAL harvest of the whole sweep (W1..W6).

## Rulings & laws

- **DRIFT reconciled (arquiteto 2026-08-27, `embed-vfs-sweep-integration-0.3.1.md §7`):** `wrap`/`unwrap`
  are ALREADY landed as intrinsics (`typer.tks:888` `type_ptr_unwrap`, `:928` `type_ptr_wrap`); `ptr`/
  `uptr` are ALREADY surface newtypes (`marshall.tks:8,16`). This crumb's reball is therefore pure USE
  (mass migration of positions→`usize`, raw words→`ptr`/`uptr`, `str`↔`[]byte` via `wrap`/`unwrap`), NOT
  teaching the intrinsics — supersedes the D132-escalation-1 "wrap/unwrap machinery is W6" expectation.
  Provenance (PV-C1, `0171`) must land BEFORE this reball (it touches the reserved names str/[]byte/ptr).
- **Teko-only + arena-is-Teko (D128):** `arena.tks`/`codegen.tks`/`lower.tks`/`_start`.
- **D130 refinements 1 + 6:** root born in `_start`, region as param, ambient `_Thread_local` REMOVED —
  the tangle (global var, thread-local, tid-table, "arena-terminal RM-C9") DISSOLVED (the model IS the
  arena terminal). Control reached via `region_control` (§3 of the plan; law-first from refinements 2+6).
- **Reball byte-preserving on 64-bit (§7b.5):** `usize==u64`, `ptr`/`uptr` bare word — deterministic,
  `gen2==gen3`. Source positions stay `u32`.
- **W15 full Javadoc; no `//`.** **Safety:** never `teko test .`; subshell `ulimit -v 4718592`;
  `[RITUAL]` gen2==gen3 + MEM_PARANOID/ASan + RSS ratchet (the model's REDUCTION: reclaim 0%→scoped,
  peak DOWN toward `<1 GB`).

## Fixtures

Shadow (scratchpad, MEM-S1): `mem_no_root_leak` (root-unresolved=0 except `main`'s frame), the full
`mem_*` family re-run end-to-end under `TEKO_MEM_PARANOID`; `nm` shows no `_Thread_local` arena control.
Non-versioned. The reball is proven by the fixpoint byte-identity.

## Gate

`[RITUAL]` — **RESEED-FINAL**: build gen2, `gen2==gen3` byte-identity, MEM_PARANOID 0 / self-host under
ASan, per-OS `nm` shows no ambient thread-local control, RSS peak DOWN (the campaign's reduction — reclaim
0%→"regions dropped ≈ scopes", toward `<1 GB`). "Green" = the root is born in `_start`, the ambient is
gone, `ar_control()`→`region_control(param)`, the model's reball landed, the full `mem_*` family passes,
`gen2==gen3`. Reseed-class: `fixpoint-rebuild`.

## Deps

`MEM-W5` (the last flip; the object arena travels, its control reached by `region_control`).

## Done when

The root region is born in `_start` and passed to `main` as the region param, the ambient
`_Thread_local`/`region_enter`/`region_leave`/`ret_dest`/`ar_cur_*` are removed, every `ar_control()`
reroutes to `region_control(<param>)`, the model's positions are `usize` and raw pointers are `ptr`/
`uptr` with `str`↔`[]byte` zero-copy, RESEED-FINAL is `gen2==gen3` under MEM_PARANOID with the RSS peak
down — the per-scope memory model is LIVE and the ambient tangle dissolved.

## Region-type reconciliation (D149 / crumbs 0183-0184)

Operates over the `Region`/`Arena` types (0183/0184). `_start` opens the root `Region` and passes it to `main`
as the region param; the retired `ar_control()` first-touch is `Arena::current()` → replaced by
`region.control()` (a `Region` method returning `Arena`); `region_enter`/`region_leave`/`ar_cur_*` are the
`Arena` ambient current-stack, removed. The reball's arena slice (positions→`usize`, raw words→`ptr`/`uptr`,
`str`↔`[]byte` zero-copy) is **already embodied** by the `Region`/`Arena` `addr: uptr` fields — adopting the
types SUBSUMES that slice of the reball. Provenance (0171) still precedes the str/[]byte reball. See
`docs/design/arena-region-tipo-com-metodos-0.3.1.md §4`.
