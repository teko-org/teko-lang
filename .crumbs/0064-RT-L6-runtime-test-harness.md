---
seq: 0064
crumb-id: RT-L6
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [RT-L5, S16-PANIC]
sources:
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:73-74"      # §1.2 harness (setjmp/longjmp) + backtrace/crash families
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:191-201"    # §3.2 the irreducible-until-Fase-E core (setjmp, signal, backtrace)
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:320"        # §5 F6 — harness + assert + backtrace, owner decision on setjmp
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:398,406-413"# R3 (setjmp tension) + law-first recommendation
  - "docs/design/plano-s16-expurgo-libc-completo.md:246,327"       # FASE8 test/crash — setjmp TENSION, backend intrinsic
  - "owner ruling 2026-08-19"                                       # capture_panic backend intrinsic ratified (R3 close)
---

# 0064 · RT-L6 — runtime C→Teko L6: test harness + assert + backtrace (setjmp/longjmp + signal)

> Close the L6 layer: the test harness, the assert integration, and the crash/backtrace handler run in Teko —
> resolving the one irreducible tension (non-local capture) with a backend intrinsic, not a C shim.

## Goal

L6 is the deepest, most OS-entangled layer: the **test harness + assert + backtrace/crash** (`migracao…`
§2.1). The families only in `teko_rt.c` (`migracao…` §1.2): the harness — `tk_test_begin`/`_end`/`_run`/
`_report`/`_summary`/`_shard_take`/`_scope`/`_capture_*`, `tk_assert_scenario_*`, `tk_chan_*`
(`teko_rt.c:2199-2516`); and backtrace/crash — `tk_tsym_load`/`_resolve`/`tk_backtrace`/`tk_rt_crash_handler`
(`teko_rt.c:60-137`). The assert value-comparison bodies already migrated at **S16-PANIC** (`0057`); L6
integrates them into the harness. The crash handler's BODY migrates to Teko; its INSTALLATION (`signal`/
`sigaction`) and `backtrace`/`backtrace_symbols_fd` are `extern fn` — migrable, low priority (`migracao…`
§3.2). The one TRUE tension is `tk_test_run`'s **setjmp/longjmp** (`teko_rt.c:2304-2330`): it captures a panic
without killing the suite, and Teko has NO non-local-control surface (`migracao…` §3.2, R3). Byte-preserving
for existing programs (fixpoint guards existing-case residence; `migracao…` R8); a `fixpoint-rebuild` swap, no
teaching reseed.

**RESOLVED (owner ratified 2026-08-19).** Behind the **native fixpoint closing** (`migracao…`
banner), its deps **RT-L5** (task/names/coverage the harness drives) and **S16-PANIC** (the migrated assert).
R3 (setjmp/longjmp) is CLOSED by owner ruling (2026-08-19): the backend `capture_panic` intrinsic is the
law-first resolution — the backend already owns the stack at the crash handler; it leaves NO C residue and
invents no language surface only the harness uses. The minimal C setjmp shim fallback is NOT taken.

## Where

- `src/runtime/teko_rt.c:2199-2516` — the harness (`tk_test_*`, `tk_assert_scenario_*`, `tk_chan_*`) — MIGRATE
  the run/report/shard/scope/capture bodies to Teko over L5 (task/coverage); C bodies go DEAD.
- `src/runtime/teko_rt.c:2304-2330` — the setjmp/longjmp panic-capture in `tk_test_run` — the R3 tension; the
  capture becomes a backend intrinsic (design §3), or a minimal C setjmp shim bridges (honest fallback).
- `src/runtime/teko_rt.c:60-137` — `tk_tsym_load`/`_resolve`/`tk_backtrace`/`tk_rt_crash_handler` — MIGRATE the
  body to Teko; installation (`signal`/`sigaction`) + `backtrace`/`backtrace_symbols_fd` stay `extern fn`.
- `src/assert/assert.tks` — the scenario hooks (`scenario_prefix_rt`/`scenario_set_rt`/`scenario_ok_rt`,
  extern to `teko_rt`) — migrate their state onto the L5 task-scenario state; the assert compare bodies are
  already Teko (S16-PANIC).
- `src/runtime/teko_rt.tks` — home of the migrated harness/backtrace bodies.
- NEW decl (draft, owner-gated): a backend non-local-capture intrinsic — see How §2.

## How

1. **Migrate the harness bodies to Teko** over L5: `test_begin`/`end`/`run`/`report`/`summary`/`shard_take`/
   `scope`/`capture_*` drive the task/coverage state (L5); the scenario name state (`scenario_set`/`ok`/
   `prefix`) rides the L5 task-scenario state, finishing the assert integration begun at S16-PANIC.
2. **Resolve R3 law-first — a backend capture intrinsic** (`migracao…` R3 recommendation, `plano-s16-expurgo…`
   :327). `tk_test_run` must catch a panic without killing the suite. The recommended close: the OWN BACKEND
   gains a non-local-capture intrinsic (it already owns the stack in the crash handler — stack unwind + PC
   restore), leaving NO C residue and inventing no language surface only the harness uses. The DRAFT contract
   the implementer copies verbatim once ratified:

```teko
/**
 * capture_panic — run `body` and, if it panics, catch the panic and return its message instead of unwinding
 * past this frame (the harness guard that keeps one failing case from killing the suite). Realised by a
 * BACKEND intrinsic (stack unwind + PC restore), not a language-level control surface and not a C setjmp shim
 * (`migracao…` R3 — the law-first close: no C residue, no harness-only language feature). RATIFIED
 * (owner ruling 2026-08-19).
 *
 * @param body  the scenario body to run under capture
 * @return      null if `body` completed, or the panic message if it panicked
 * @since 0.3.1
 */
pub fn capture_panic(body: func<null>): str | null
```

3. **Migrate the crash handler body to Teko; keep installation extern** (`migracao…` §3.2). `crash_handler`'s
   Teko body formats the backtrace; `signal`/`sigaction` install it and `backtrace`/`backtrace_symbols_fd`
   resolve as `extern fn` (the platform, not C-we-keep).
4. **The bridge, if the intrinsic is not ratified** (`migracao…` R3 (c)): a MINIMAL C setjmp shim survives
   FASE8 as the last measured residue — the ONLY C that outlives the L6 migration, retired the moment the
   intrinsic lands. This is the honest fallback, NOT the recommendation.
5. **The link is the normal program link** (`migracao…` §2.2) for everything except the (bridge) setjmp shim;
   `signal`/`backtrace` resolve as undefined externals.
6. **Fixpoint byte-identity + the harness runs native.** `gen2==gen3` byte-identical; the harness runs through
   the NATIVE path; coverage is non-zero; panic capture preserves the suite (`migracao…` §5 F6 proof).

Reused (do NOT redeclare): the migrated assert family (S16-PANIC), the L5 task/coverage/scenario state,
`region_alloc` (L1), the `signal`/`backtrace` externs.

## Rulings & laws

- **Teko-only:** L6 bodies land in `src/runtime/teko_rt.tks`; the maintained-C exception is the BRIDGE the
  campaign retires (`migracao…` §1.4/R1). The `teko_rt.c` harness/backtrace C goes DEAD; deletion is `0095`
  RM-C9 (M3), EXCEPT a minimal setjmp shim if R3 is unratified (the last residue, retired on ratification).
- **W15 full Javadoc** on every touched declaration (including `capture_panic`); flatten/extract; no inline `//`.
- **Removals = clean expurgo, NO tombstone:** the harness/backtrace C is removed cleanly at M3; the setjmp
  bridge (if any) is a NAMED, measured residue, not a tombstone.
- **M.1 fail-loud (`migracao…` R3):** the panic-capture guard is REQUIRED — a suite that cannot catch a
  scenario panic without aborting is not fail-loud-correct. The guard is the backend intrinsic (recommended)
  or the bridge shim (fallback).
- **Law-first close (`migracao…` R3, §8):** R3 is CLOSED by owner ruling 2026-08-19. The backend capture
  intrinsic is the ratified resolution that obeys all laws (Teko-only via a backend intrinsic, no
  harness-only language surface, no C residue). The minimal C setjmp shim fallback is NOT taken.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744` cap — a blown guard is a
  root-cause fix, never a raised ceiling; commit each green step; **reseed ONLY at a [RITUAL]** — this is
  `fixpoint-rebuild`, no teaching seed harvested; fixpoint `gen2==gen3` byte-identical; sweep `.tkt`/`.tkr`
  after the `capture_panic` signature lands.

## RESOLVED (R3 — non-local panic capture)

**R3 CLOSED.** Owner ruling 2026-08-19 ratified the backend `capture_panic` intrinsic as the law-first
resolution — it obeys every law and leaves no C residue. The minimal C setjmp shim fallback is NOT taken.
The tension is resolved; no HALT remains.

## Fixtures

the harness/crash path is not self-build-exercised (the gate never runs `teko test .`) — isolated oracles:

| fixture | asserts | expected |
|---|---|---|
| `l6_capture_panic` | a scenario that panics is CAUGHT; the suite continues and reports the failure (guard preserves the suite) | `0` |
| `l6_harness_native_run` | the harness runs a two-scenario suite through the NATIVE path; coverage non-zero | `0` |
| `l6_backtrace_format` | a forced fault produces a formatted backtrace via the migrated handler (installation extern) | `0` |
| `l6_assert_integrated` | `assert::eq_i64` inside a scenario reports through the migrated harness scenario name | `0` |

## Gate

`[fixpoint]` — build gen2 + the scoped fixtures + `gen2==gen3` byte-identity. "Green" = the harness + assert
integration + crash handler run in Teko, panic capture preserves the suite (via the ratified backend intrinsic),
coverage is non-zero on the native path, and the emitted `teko.c` is byte-identical to before the swap. R3 is
CLOSED by owner ruling 2026-08-19. **Reseed-class:** `fixpoint-rebuild` (core-consumes; teaches nothing; no
reseed harvested).

## Deps

`RT-L5` (`0063` — task/names/coverage the harness drives), `S16-PANIC` (`0057` — the migrated assert
compare+panic the harness reports through).

## Done when

the test harness, assert integration, and crash/backtrace run in Teko with panic capture preserving the suite
(backend intrinsic ratified, or the named setjmp bridge in place), coverage non-zero on the native path, the
fixtures exit `0`, a `[fixpoint]` build is `gen2==gen3` byte-identical, and R3 is either ruled or explicitly
relayed to the owner.
