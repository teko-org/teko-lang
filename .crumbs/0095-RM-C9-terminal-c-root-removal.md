---
seq: 0095
crumb-id: RM-C9
milestone: M3
gate: "[RITUAL]"
reseed-class: "expurgo"
deps: [RT-L1, RT-L2, RT-L3, RT-L4, RT-L5, RT-L6, RT-L7, S16-MM-wp, S16-MM-const, S16-MM-pool, S16-SYNC-const]
sources:
  - "docs/design/reducao-memoria-arrays-0.3.1.md:335-340"              # C9 terminal C-root removal
  - "docs/design/plano-mestre-0.3.1-implementacao.md:265"              # M3 RM-C9 row
  - "src/runtime/teko_rt.c:2320"                                       # tk_g_arena_control slot
  - "docs/design/arena-especificacao-unica-0.3.1.md:60-73"            # region mechanics
---

# 0095 · RM-C9 — TERMINAL C-root removal + transcribe arena control slot to Teko

> The E4 (part 1) C-death: after ALL runtime legs (RT-L*) and §16 memory/sync constants are in Teko,
> remove the dead C roots the self-compile enumerates and TRANSCRIBE the last arena control shim — the
> `.bss`/`MAP_FIXED` control slot (`tk_g_arena_control`) — into Teko, zeroing `from "teko_rt"` on the
> array/arena path. NO tombstone.

## Goal

This is the TERMINAL runtime-C removal: the last step where the compiler still reaches into
`teko_rt.c` for arena control. By this seq every RT-L* leg (the libc→Teko transcriptions) and the §16
mmap/sync constants live in Teko, so the only residual C the arena path uses is the per-task control slot
`tk_g_arena_control` (`teko_rt.c:2320-2332`) — a `_Thread_local` word holding the current region pointer,
reachable in C via a `.bss` slot / `MAP_FIXED` mapping. This crumb TRANSCRIBES that slot to Teko (a Teko
`.bss`-backed word / fixed-address mapping over the already-Teko `mmap`/syscall arena) and REMOVES the
dead C roots the self-compile flags. It is a **byte-mover** expurgo driving E4's first reseed. It is the
RUNTIME half of "no C" (§16 removes the runtime dependency); the COMPILE half is M4's C-route removal
(RM-C16). Both must close for "no C" to be literally true. No tombstone — removed C symbols simply cease
to exist; nothing references them.

## Where

- `src/runtime/teko_rt.c:2320-2332` — `tk_g_arena_control` (`_Thread_local uint64_t`) + its getter/setter
  (`:2326`/`:2332`) — TRANSCRIBE to Teko: a Teko module owning the control word over the Teko arena
  (`src/runtime/arena.tks`), backed by a `.bss` slot / fixed mapping, and REMOVE the C original.
- `src/runtime/arena.tks` — the Teko arena (already complete: `region_alloc`/`region_new`/`region_drop`/
  `region_enter`/`region_leave`/`region_current` over `mmap`/syscall) — ADD the transcribed control-slot
  accessor here (the Teko owner of what `tk_g_arena_control` held).
- `src/build/project.tks:1619-1629` — the `extern fn region_* … from "teko_rt"` decls used by the fused
  native emit — REPOINT to the Teko arena module (drop the `from "teko_rt"` on the arena path).
- `src/runtime/teko_rt.c` — the dead C roots the self-compile enumerates after the transcription
  (region-control helpers with no remaining caller) — REMOVE. What STAYS in maintained-C: the
  `teko_rt.{c,h}` runtime seed the §16 non-array paths still need until S16-SWEEP (0096) fully closes.

## How

1. **Transcribe the control slot** (`teko_rt.c:2320` → `arena.tks`): a Teko word owning the current-region
   pointer, backed by a `.bss` slot / `MAP_FIXED` fixed mapping over the Teko arena, exposing the same
   get/set the C `tk_g_arena_control` did. Per-task semantics preserved (the `_Thread_local` becomes a
   task-local Teko slot — the arena's task model already exists).

```teko
/**
 * arena_control_get — the current region pointer for the calling task, read from the transcribed
 * `.bss`/fixed-mapping control slot (the Teko owner of the retired C `tk_g_arena_control`). Zero when no
 * region is entered (the program root is used).
 *
 * @return the current region handle, or 0 when none is entered
 * @since 0.3.1
 */
pub fn arena_control_get(): u64

/**
 * arena_control_set — publish `region` as the calling task's current region into the transcribed control
 * slot. Paired with `arena_control_get` by `region_enter`/`region_leave`; the slot is task-local so two
 * workers never race on it (the arena's per-task discipline, unchanged by the transcription).
 *
 * @param region  the region handle to make current for this task
 * @since 0.3.1
 */
pub fn arena_control_set(region: u64)
```

2. **Repoint the arena externs** (`project.tks:1619-1629`) from `from "teko_rt"` to the Teko arena
   module — the array/arena path now has ZERO `from "teko_rt"`.
3. **Let the self-compile enumerate the dead C roots** (build-first, step 5): remove the transcribed C
   originals, seed, and the self-compile ERRS at any residual C reference — each error is a removal site.
   Remove → seed → repeat.
4. **NO tombstone:** removed C symbols cease to exist; no diagnostic references them (the user never knew
   `tk_g_arena_control` existed).
5. **Message pass + reseed ITERATIVELY** (owner: unified message pass over the integrated tree BEFORE the
   single reseed) until `gen2==gen3` byte-identical.

## Rulings & laws

- **Teko-only + maintained-C exception:** the arena control transcription is C→Teko (`nada em teko_rt.c
  pro expurgo` — the array/arena machinery is REMOVED from C, moved to Teko). `teko_rt.{c,h}` remains the
  maintained-C seed for the non-array §16 tail until 0096 closes it.
- **W15 full Javadoc** on the transcribed Teko accessors; removed C decls carry no doc.
- **Removals = clean expurgo, NO tombstone:** dead C roots simply vanish (`plano-mestre:253-257`).
- **§16 sem atalhos:** the control slot is transcribed as a REAL Teko `.bss`/fixed-mapping implementation
  over the Teko arena, never a degraded shim (owner: se existe em C, existe em Teko).
- **Safety:** NEVER `teko test .`; `ulimit -v 6815744` per build; commit each green sweep; reseed ONLY at
  this [RITUAL]; E4-part-1 harvest at `gen2==gen3`; sweep `.tkt`/`.tkr` after signature changes.

## Fixtures

none — the fixpoint self-build exercises this. The compiler's own arena usage (every region enter/leave/
alloc during a self-build) drives the transcribed control slot at scale; a divergence surfaces as a
`gen2!=gen3` fixpoint break, not a missed fixture.

## Gate

`[RITUAL]` — full native ladder + a genuine expurgo reseed (E4, part 1). "Green" = the arena control slot
runs in Teko, the array/arena path has zero `from "teko_rt"`, the enumerated dead C roots are removed, and
`gen2==gen3` byte-identical after the E4 harvest. Reseed-class: `expurgo`.

## Deps

`ALL RT-L*` (runtime legs transcribed) + `S16-*` (§16 mmap/sync constants) — verbatim from 000-INDEX.
This is the LAST of E4's two members' first half; S16-SWEEP (0096) follows.

## Done when

The arena control slot is transcribed to Teko (`.bss`/fixed mapping over the Teko arena), the array/arena
path no longer references `teko_rt`, the enumerated dead C roots are removed with no tombstone, and the
E4 reseed lands `gen2==gen3` byte-identical.
