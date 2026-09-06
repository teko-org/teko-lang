---
seq: 0003
crumb-id: SM-A2
milestone: M1
gate: "[RITUAL]*"
reseed-class: "(folds R1)"
deps: ["SM-A1"]
sources:
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:62-77"    # §1.1 DPS keystone
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:894-913"  # §8 self↔DPS convergence
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1159-1161"# §10 Phase A — A2
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1112-1119"# §9.1 DPS moves bytes, gen2==gen3
  - "ast-computed-arena-assessment-0.3.1.md:§4.2"                     # verbatim fn shapes (copy source)
---

# 0003 · SM-A2 — DPS ABI + `lower_return_into_dest` + `alloc_call_dest`

> DPS ABI + `lower_return_into_dest` + `alloc_call_dest` (dest-passed only; `ret_dest=null` = today).

## Goal

THE KEYSTONE (§1.1). A callee allocates its return DIRECTLY into the caller's arena via a synthetic
destination (`alloc_call_dest`); `return` becomes VIRTUAL — write-into-dest + exit, no copy-out. This
closes the RETURN and TAIL-MERGE-INTO-RETURN conveyance boundaries (≈ 2 of the 3 native fixpoint
blockers if SM-P1 pinned them to the return facet), retires the copy-out box SM-A1 measured, and buys
the memory win. **Entered ONLY when a destination is passed** — the ABI carries `ret_dest`, and
`ret_dest == null` reproduces today's path BYTE-IDENTICALLY, so the change is opt-in per call and safe
to land incrementally. DPS is the ONE byte-mover that drives the seed (§9.1): `gen1 ≠ gen2` is expected
(the native return ABI changes), `gen2 == gen3` is INVIOLABLE (deterministic lowering). Its seed is
harvested once at SM-R1 (`0030`), not here — hence reseed-class `(folds R1)`.

## Where

Type/fn shapes are specified VERBATIM in `ast-computed-arena-assessment-0.3.1.md §4.2` — **copy from
there, do not re-author** (`fn_returns_aggregate`, `with_ret_dest`, `lower_return_into_dest`,
`alloc_call_dest`, one hash/shape project-wide). Existing fns touched:

- `src/lir/lower.tks:7245` — `lower_return` — route to `lower_return_into_dest` when `ret_dest != null`.
- `src/lir/lower.tks:7278` — `lower_return_fat` — same, for fat (aggregate) returns.
- `src/lir/lower.tks:1740` — `lower_call` — reserve `alloc_call_dest` in the caller's current region and
  pass it as the hidden `ret_dest` argument for aggregate-returning callees.
- `src/lir/lower.tks:10798` — `lower_block_value` / `lower_match` tail — each tail arm lowers into the
  SHARED `ret_dest` (the tail-merge conveyance the pin targets).
- `src/lir/lower.tks:1594` — `region_current_vreg` — the caller region `alloc_call_dest` allocates into.
- `src/lir/lower.tks:11715` — `own_returned_value` — becomes dead ON THE DPS PATH (retired in SM-A3;
  here it is simply bypassed when `ret_dest != null`).

## How

1. **Add the DPS ABI leg.** Aggregate-returning functions gain a hidden trailing destination parameter
   `ret_dest` (a pointer into the caller's current region). `ret_dest == null` = the legacy path
   (today's copy-out), so every non-DPS call is byte-identical.

```teko
/**
 * alloc_call_dest — reserve, in the CALLER's current region (`region_current_vreg`), the destination an
 * aggregate-returning callee writes its result INTO, and yield the vreg holding that destination
 * pointer. Passed as the hidden `ret_dest` trailing argument of the call; `null` when the callee does
 * not return an aggregate (the legacy copy-out path, byte-identical). This is the caller-arena virtual
 * return: the value is BORN in the caller's arena, never copied out of the callee frame.
 *
 * @param ctx   the lowering context (holds the current region)
 * @param ty    the aggregate return type whose size/align the destination is reserved for
 * @return      the vreg holding the destination pointer, or null when no destination is needed
 * @since 0.3.1
 */
fn alloc_call_dest(ctx: LowerCtx, ty: Type): LVreg | null

/**
 * lower_return_into_dest — lower a `return <e>` VIRTUALLY when a `ret_dest` is in scope: evaluate `<e>`
 * writing its aggregate result directly through `ret_dest` (no `own_returned_value` box, no copy-out),
 * then exit the frame. A tail `match`/`if` merge lowers EACH arm into the SAME `ret_dest`, closing the
 * tail-merge conveyance boundary. When `ret_dest == null` this is never entered — `lower_return` keeps
 * today's path.
 *
 * @param ctx       the lowering context
 * @param e         the returned expression
 * @param ret_dest  the caller-passed destination pointer vreg
 * @since 0.3.1
 */
fn lower_return_into_dest(ctx: LowerCtx, e: TExpr, ret_dest: LVreg)
```

2. **Gate the call path.** In `lower_call` (`:1740`), when `fn_returns_aggregate(callee)` (the §4.2
   predicate), reserve `alloc_call_dest` and thread it as `ret_dest`; else pass `null`.
3. **Route the return path.** In `lower_return`/`lower_return_fat`, when `ret_dest != null` delegate to
   `lower_return_into_dest`; else keep the existing box path unchanged.
4. **Thread the tail merges.** In `lower_block_value`/`lower_match` tail (`:10798`), each arm targets the
   shared `ret_dest` so a value returned through a tail `match`/`if` is born in the caller's arena — the
   construction that closes `type_match`/`frame_sweep_inst` IF the pin confirmed the return facet.
5. **Do NOT yet retire `own_returned_value`** — that is SM-A3. Here it is only bypassed on the DPS path.
   `frame_escape_guard` (`frame_escape.tks:56`) is satisfied BY CONSTRUCTION on the DPS path (the value
   is already in the caller's arena) and stays as the inversion net.
6. **`self` convergence (§8):** the receiver `params[0]` already lowers by-address into the caller's
   region (`region_current_vreg`), structurally identical to `alloc_call_dest` — no extra codegen; a
   `self`-mutating method IS a DPS write through the receiver channel. Note the shared discipline; no
   separate work.
7. **Prove the byte-move is deterministic:** rebuild the native ladder (genB→gen2→gen3). `gen1 ≠ gen2`
   is expected; **`gen2 == gen3` must hold** — that is the ritual gate.

## Rulings & laws

- **Teko-only:** all edits in `src/lir/lower.tks`; no C twin. The DPS ABI is emitted through the normal
  codegen; the runtime is untouched.
- **W15 full Javadoc** on `alloc_call_dest`, `lower_return_into_dest`, and every helper (pub + private).
- **`ret_dest == null` = today** (umbrella A2): the legacy path stays byte-identical, so the change is
  opt-in per call and the non-aggregate corpus does not move.
- **Safety:** NEVER `teko test .`; native ladder + fixpoint build in a subshell with `ulimit -v 6815744`
  (INVIOLABLE — the DPS win must not raise the ceiling); commit each green step; **the SEED is harvested
  once at SM-R1 (`0030`), NOT here** — this crumb carries the dev native-ladder ritual but mints no
  reseed (`[RITUAL]*`, `(folds R1)`); sweep `.tkt`/`.tkr` for any signature shift.
- **§9.1** (umbrella:1112-1119): DPS moves bytes deterministically; `gen2==gen3` is the invariant.
- **§14 R1:** SM-A2 is committed only AFTER SM-P1 confirms (or the memory-only fork is accepted).

## Fixtures

The self-build exercises DPS on every aggregate return, but the ERROR/CORRECTNESS oracles the fixpoint
cannot assert (exit-code correctness of tricky return shapes, inversion failure) are NOT self-exercised
as oracles — carry these from §12/assessment:

| fixture | asserts | expected |
|---|---|---|
| `dps_aggregate_return_value_correct` | an aggregate returned via DPS carries the correct value | 0 |
| `dps_variant_match_return` | a variant returned through a tail `match` is correct under DPS | 0 |
| `dps_frameset_if_return` | a `FrameSet` returned through an `if` merge is correct under DPS | 0 |
| `dps_no_frame_escape` | `frame_escape_guard` stays satisfied on the DPS path | 0 |
| `dps_caller_dest_not_dropped` | the caller destination outlives the callee frame (inversion) | 0 / inversion fails |
| `dps_dest_single_writer` | the DPS destination is single-writer by construction (inversion) | inversion fails |

## Gate

`[RITUAL]*` — full native ladder (genB→gen2→gen3) + `gen2==gen3` byte-identity. "Green" = `gen1 ≠ gen2`
(DPS changed the emit), `gen2 == gen3` holds, the SM-A1 return-box baseline drops toward zero, and (if
the pin confirmed) `type_match`+`frame_sweep_inst` native crashes turn to exit `0`. Reseed-class:
`(folds R1)` — no reseed minted here; harvested at SM-R1.

## Deps

`SM-A1` (the measured baseline SM-A2 must beat). Precondition: SM-P1 pin decides fixpoint-fix vs memory-only.

**SM-P1 verdict (resolved `0001` @ `4f775e8d`, `docs/memory/0.3.1-native-p1-pin-type-match-frame-sweep.md`): DECOUPLE / memory-only.**
`type_match` = RETURN/TAIL-MERGE facet → SM-A2 CLOSES it by construction (build the tail-merge-into-`ret_dest`
path). `frame_sweep_inst` = PAYLOAD-BIND facet and `push_inst_block` = self-append → NOT closed by DPS; SM-A2/A3
are memory + return-correctness only, and the native fixpoint DECOUPLES to the point-fix family (SM-A5 +
root-map grind). SM-A2 is NOT the sole native-fixpoint fix. NOTE: at `4f775e8d` the native route cannot build
gen2 at all (broad regression: `serialize_const` `_ =>` `src/lir/lower_const.tks:350`; `syscall6`/`ar_mmap`), so
the `gen2==gen3` ritual gate here is unreachable until that frontier is cleared — do not commit the R1 reseed
while native gen2 cannot be produced.

## Done when

Aggregate returns lower into a caller-passed `ret_dest` (with `ret_dest == null` reproducing today
byte-identically), the native ladder is green with `gen2==gen3`, and the return-box copy-out volume is
driven down from the SM-A1 baseline.
