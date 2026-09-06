---
seq: 0063
crumb-id: RT-L5
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [RT-L1, S16-SYNC]
sources:
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:69-72"      # §1.2 interning / task / names / coverage families
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:112-118"    # §2.1 L5 = interning/task/names/coverage, process state over L1
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:259-268"    # §4.2 cross-thread → program region (names seam)
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:320"        # §5 F6 — port interning/task/names/coverage
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:198,401"    # tk_cov_dump(char*) ABI hole (R6, owner decision)
  - "owner ruling 2026-08-19 — cov_dump_s(str) ratified (R6 close, 0% C law)"
---

# 0063 · RT-L5 — runtime C→Teko L5: interning / task / names / coverage (process state over L1)

> Close the L5 layer: string interning, per-task roots, the cross-thread `names` handle table, and coverage
> instrumentation run in Teko as process state over the L1 arena — `names` residing in the program region.

## Goal

L5 is **process state over L1** (`migracao…` §2.1): the subsystems that hold long-lived process structure. The
families only in `teko_rt.c` (`migracao…` §1.2): interning — `tk_intern_find`/`_dup`/`_get`/`_put`/`_reset`
(`teko_rt.c:1285-1355`, a hash-map over arena); task isolation — `tk_task_current`/`_begin`/`_end`/`_reset`/
`_reset_transient` (`teko_rt.c:1217-2101`, per-task roots); `names` (F4 handles/channels) —
`tk_names_open`/`_lookup`/`_close`/`_status`/`_grow`/`_take`/`_forget`/`_live_count`/`_capacity`/`_cell_*`
(`teko_rt.c:1939-2073`, generational handle table); and coverage — `tk_cov_reset`/`_mark`/`_enter`/`_leave`/
`_branch(_at/_hit)`/`_line(_at/_hit)`/`_dump`/`_merge`/… (`teko_rt.c:3532-3762`). RT-L5 migrates each to Teko
over L1, with one residence rule that matters: **`names` resides in the PROGRAM region** (`tk_region_program()`)
because it is the seat of `chan`/`wait_group`, the only legitimate program-residents (`migracao…` §4.2) —
anticipating `chan`'s arrival. The coverage `dump` closes the one ABI hole: `tk_cov_dump(const char*)` took a
`char*`, not a `tk_str` (`migracao…` R6) — superseded by `cov_dump_s(str)` (symmetric to
`cov_merge`), the `char*` pun rejected by M.3. Byte-preserving for existing programs (fixpoint guards
existing-case residence; `migracao…` R8); a `fixpoint-rebuild` swap, no teaching reseed.

**BLOCKED (design-ahead, honest).** Behind the **native fixpoint closing** (`migracao…` banner), its deps
**RT-L1** (the arena all four subsystems allocate through) and **S16-SYNC** (the cross-platform sync ABI the
cross-thread `names` cell needs). The `cov_dump_s(str)` surface is ratified by owner ruling 2026-08-19
(R6 closed, 0% C law). This doc designs all four subsystems, the `names` program-residence, the `cov_dump_s`
contract, and the fixtures; what stays blocked is the fixpoint completion.

## Where

- `src/runtime/teko_rt.c:1285-1355` — interning (`tk_intern_*`) — MIGRATE to a Teko hash-map over L1.
- `src/runtime/teko_rt.c:1217-2101` — task isolation (`tk_task_*`) — MIGRATE per-task roots + transient reset
  over L1.
- `src/runtime/teko_rt.c:1939-2073` — `names` (`tk_names_*`) — MIGRATE the generational handle table; its cell
  resides in `tk_region_program()` (`teko_rt.h:265`), the cross-thread program region (`migracao…` §4.2).
- `src/runtime/teko_rt.c:3532-3762` — coverage (`tk_cov_*`) — MIGRATE the instrumentation; `tk_cov_dump(char*)`
  is superseded by `cov_dump_s(str)` (the ABI hole, R6).
- `src/runtime/teko_rt.tks` — home of the migrated interning/task/names/coverage bodies.
- NEW decl: `cov_dump_s(path: str)` (symmetric to `cov_merge`) — see How §4.
- NO new user-facing surface beyond `cov_dump_s`: the subsystem names pre-exist.

## How

1. **Migrate interning to Teko** — an FNV-1a hash-map over L1 (the arena), `find`/`dup`/`get`/`put`/`reset`
   bumping table + entries into the intern region; deterministic iteration (sort keys) so a serialized artifact
   stays byte-stable (feeds the RM-C13 determinism, `reducao…` R6).
2. **Migrate task isolation** — `task_current`/`begin`/`end`/`reset`/`reset_transient`: per-task root frames
   over L1; the transient reset drops a task's transient region without touching program residence.
3. **Migrate `names` into the PROGRAM region** (`migracao…` §4.2). The handle-table cell allocates in
   `tk_region_program()` — the region "owned by NO task, survives the task's exit". `names` is the ONLY runtime
   subsystem whose residence is program BY DESIGN, because it is the seat of `chan`/`wait_group` (`modelo…`
   §2). The cross-thread cell uses the S16-SYNC ABI (futex/ulock/WaitOnAddress) for its generational
   compare-and-set. This ANTICIPATES `chan` (no surface yet, `spine.tks:1555`): when `chan` arrives, the seam
   already resides right.
4. **Migrate coverage + close the ABI hole** (`migracao…` R6). The instrumentation (`mark`/`enter`/`leave`/
   `branch`/`line`) migrates to Teko over L1. `tk_cov_dump(const char*)` — the one contract symbol whose param
   is `char*` not `tk_str` — is superseded by `cov_dump_s(str)`, symmetric to `cov_merge`; the `tk_str→char*`
   pun is M.3-rejected. The W15 contract the implementer copies verbatim:

```teko
/**
 * cov_dump_s — write the accumulated coverage profile to the file at `path`. Supersedes the C
 * `tk_cov_dump(const char*)`, the sole gate-contract symbol whose parameter was a raw `char*` rather than a
 * `tk_str` (`migracao…` R6 / `gate-sem-c` §4.2). Symmetric to `cov_merge(str)`; the `tk_str→char*` pun is
 * rejected by M.3 — a `str` is a fat pointer, never reinterpreted as a C string.
 *
 * @param path  the destination file path for the coverage dump
 * @return      void, or an error if the dump cannot be written
 * @throws      when the file cannot be created/written
 * @since 0.3.1
 */
pub fn cov_dump_s(path: str): null | error
```

5. **The link is the normal program link** (`migracao…` §2.2): the four subsystems are `exp fn` Teko compiled
   into the program's object; the sync bottom resolves via the S16-SYNC ABI.
6. **Fixpoint byte-identity + residence.** `gen2==gen3` byte-identical proves the interning/task/coverage
   residence did not shift; the `names` program-residence is the one deliberate wide residence (legitimate,
   `migracao…` §4.2).

Reused (do NOT redeclare): `region_alloc`/`region_program`/`region_drop_subtree` (L1), the S16-SYNC
futex/ulock/WaitOnAddress ABI, `cov_merge(str)` (the existing symmetric surface).

## Rulings & laws

- **Teko-only:** L5 bodies land in `src/runtime/teko_rt.tks`; the maintained-C exception is the BRIDGE the
  campaign retires (`migracao…` §1.4/R1). The `teko_rt.c` interning/task/names/coverage C goes DEAD; deletion
  is `0095` RM-C9 (M3).
- **W15 full Javadoc** on every touched declaration (including `cov_dump_s`); flatten/extract; no inline `//`.
- **Removals = clean expurgo, NO tombstone:** `tk_cov_dump(char*)` is SUPERSEDED by `cov_dump_s`; when the C is
  removed (M3) there is no tombstone diagnostic — the new surface simply replaces it.
- **No ABI pun (M.3, `migracao…` R6):** `cov_dump_s(str)` replaces the `char*` hole; `str→char*` is rejected.
- **Residence law (`modelo…` §2, `migracao…` §4.2):** `names` resides in the program region (cross-thread,
  seat of `chan`/`wait_group`) — a legitimate wide residence; interning/task/coverage reside in scope/task
  regions, never leaking to root.
- **R6 CLOSED:** `cov_dump_s(str)` — ratified by owner ruling 2026-08-19 — the coverage dump migrates to
  the `str`-typed surface (no C residue, 0% C law); the ABI hole is closed. The surface passes M.3 + is
  symmetric to `cov_merge`.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744` cap — a blown guard is a
  root-cause fix, never a raised ceiling; commit each green step; **reseed ONLY at a [RITUAL]** — this is
  `fixpoint-rebuild`, no teaching seed harvested; fixpoint `gen2==gen3` byte-identical; sweep `.tkt`/`.tkr`
  after the `cov_dump_s` signature lands.

## Fixtures

interning/task/names/coverage hold process state not fully self-build-exercised — isolated oracles for the
cross-thread `names` cell and the coverage dump:

| fixture | asserts | expected |
|---|---|---|
| `l5_intern_dedup` | interning the same string twice returns the same handle; distinct strings distinct handles | `0` |
| `l5_task_transient_reset` | a task's transient region is dropped by `reset_transient` while program residence survives | `0` |
| `l5_names_cross_thread` | a handle opened in one task is looked up from another (program-region cell, S16-SYNC CAS) | `0` |
| `l5_cov_dump_s` | `cov_dump_s(path)` writes a non-empty profile; no `char*` symbol on the path | `0` |

## Gate

`[fixpoint]` — build gen2 + the scoped fixtures + `gen2==gen3` byte-identity. "Green" = interning/task/coverage
run in Teko over L1, `names` resides in the program region (cross-thread via S16-SYNC), `cov_dump_s(str)`
replaces the `char*` hole (R6 CLOSED), no `tk_intern_*`/`tk_names_*`/`tk_cov_*` C symbol is on the path, and the emitted
`teko.c` is byte-identical to before the swap. **Reseed-class:** `fixpoint-rebuild` (core-consumes; teaches
nothing; no reseed harvested).

## Deps

`RT-L1` (`0059` — the arena all four subsystems allocate through), `S16-SYNC` (`0056` — the cross-platform
sync ABI the cross-thread `names` cell's generational CAS needs).

## Done when

interning/task/names/coverage run in Teko over L1 with `names` in the program region (cross-thread via
S16-SYNC) and `cov_dump_s(str)` replacing the `char*` ABI hole, no C symbol for these is on the path, the
fixtures exit `0`, and a `[fixpoint]` build is `gen2==gen3` byte-identical.
