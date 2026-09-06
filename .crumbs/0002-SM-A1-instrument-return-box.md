---
seq: 0002
crumb-id: SM-A1
milestone: M1
gate: "[dry]"
reseed-class: "none"
deps: ["SM-P1"]
sources:
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:62-77"    # §1.1 DPS keystone (the box A1 measures)
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1158"     # §10 Phase A — A1
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:86-90"    # §1.3 arena floor / profiler seed
---

# 0002 · SM-A1 — instrument the return-box volume

> Instrument return-box volume (assessment D0) — measure the copy-out the DPS keystone deletes.

## Goal

Before DPS (SM-A2) deletes the copy-out return box, MEASURE it. This crumb adds an observability probe
(assessment D0) that reports the **volume of return-box copy-out** — how many bytes the current
`own_returned_value` retrofit (`lower.tks:11715`) copies OUT of the callee frame at each aggregate
return, and the aggregate return count. It is the baseline against which SM-A2's copy-elision win is
proven, and it feeds the arena floor's profiler seed (§1.3, `Confidence::Thin`). It changes no lowering
decision — it only measures — so it is byte-preserving (the instrumentation is behind the existing
`tk_obs` observability gate, inert on a normal build), reseed-class `none`.

## Where

- `src/codegen/codegen.tks:9832` — the profiler's existing `#arena_size` presize path; the return-box
  volume is recorded alongside it as the `Confidence::Thin` seed input (§1.3).
- `src/lir/lower.tks:11715` — `own_returned_value` (the box whose copy-out volume is counted at
  emit time; instrument, do not yet retire — retirement is SM-A3).
- `src/lir/lower.tks:7245`/`:7278` — `lower_return`/`lower_return_fat` — the aggregate-return sites the
  counter increments.
- The `tk_obs` observability channel (the same profiler that reports `tk_slice_push_r` = 93% of the
  peak) — add the return-box counters to its report; no new runtime FFI.

## How

1. **Add a return-box observability counter** gated behind the existing `tk_obs` channel (inert on a
   normal build, exactly like the push-volume counter). At each aggregate return lowering
   (`lower_return`/`lower_return_fat`, `lower.tks:7245`/`:7278`), when the retrofit box
   `own_returned_value` is emitted, add the box byte-size and bump an aggregate-return count.

```teko
/**
 * obs_return_box_record — accumulate one aggregate return-box copy-out into the profiler baseline
 * (assessment D0). Records `bytes` (the size of the value copied OUT of the callee frame by
 * `own_returned_value`) and increments the aggregate-return count, so the pre-DPS copy-out volume is
 * measured before SM-A2 deletes it. Inert unless the `tk_obs` channel is active — a normal build
 * emits nothing and is byte-identical.
 *
 * @param bytes  the size in bytes of the return value copied out of the callee frame
 * @since 0.3.1
 */
fn obs_return_box_record(bytes: u64)
```

2. **Wire it into the profiler seed:** the accumulated volume + count feed the `#arena_size` presize
   path (`codegen.tks:9832`) as the `Confidence::Thin` seed (§1.3), so the arena floor can start at a
   measured lower bound rather than zero. This is a READ into the existing seed, not a new analysis.
3. **Emit the baseline report** on the `tk_obs` channel: total return-box bytes, aggregate-return
   count, and per-function top offenders — the numbers SM-A2 must drive toward zero.
4. **Confirm byte-neutrality:** the counter and report are behind the `tk_obs` gate; a normal (non-obs)
   build emits the identical C/native. This is the `[dry]` proof.

## Rulings & laws

- **Teko-only:** the instrumentation is `.tks` (`codegen.tks`/`lower.tks`); no C twin edit — the
  counter piggybacks the existing `tk_obs` channel.
- **W15 full Javadoc** on `obs_return_box_record` and any helper; no inline `//`.
- **Measure-before-move:** this crumb ONLY measures; it does not change any lowering decision (SM-A3
  retires the box). Owner methodology "shadow/baseline before the conversion" (CLAUDE.md expurgo
  methodology step 2) applied to the DPS return box.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  no reseed (`[dry]`, reseed-class `none`).

## Fixtures

`none — the fixpoint self-build exercises this`. The compiler returns aggregates throughout its own
source, so the self-build drives every `lower_return`/`lower_return_fat` path; the counter is verified
by the profiler report on the self-build, not by a synthetic `.tkr`. (The instrumentation is observability,
not a language capability with an error branch.)

## Gate

`[dry]` — compile + trivial fixpoint (no emitted-byte change on a normal build) + the `tk_obs` report
shows a non-zero return-box baseline. "Green" = normal build byte-identical AND the baseline numbers are
recorded for SM-A2 to beat. Reseed-class: `none`.

## Deps

`SM-P1` (the pin decides whether the box SM-A1 measures is the one SM-A2 elides on the return facet).

## Done when

A `tk_obs`-gated build reports the total return-box copy-out volume + aggregate-return count, the
profiler `#arena_size` seed reads it as `Confidence::Thin`, and a normal build is byte-identical.
