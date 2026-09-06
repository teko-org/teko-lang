---
seq: 0036
crumb-id: RM-C1
milestone: M2
gate: "[dry]"
reseed-class: "none"
deps: []
sources:
  - "docs/design/reducao-memoria-arrays-0.3.1.md:228-232"   # C1 — measure/shadow the codegen emit buffer
  - "docs/design/reducao-memoria-arrays-0.3.1.md:110-126"    # the 93% PUSH bill; C3+C4/C5 the main blow
  - "docs/design/reducao-memoria-arrays-0.3.1.md:225-227"    # build-before-remove; 6.5 GiB guard inviolable
---

# 0036 · RM-C1 — measure/shadow the `cb`/`append_fo` emit buffer (baseline; no src change)

> Measure/shadow the `cb`/`append_fo` emit buffer (baseline; no src change) — establish the peak-memory
> baseline the RM-C3 conversion is judged against.

## Goal

Measure, with the `tk_obs` arena-lifetime aggregate, the fraction of the compiler's peak memory attributable
to the codegen C-emit buffer (`cb`/`append_fo` — the single largest runtime consumer, the "93% PUSH" bill,
−3.0 to −3.5 GB once converted). Build an ISOLATED `.tkr` shadow (OUTSIDE the `teko test .` OOM) that emits a
large block of text BOTH ways — the old chained `append_fo`/`cb` idiom vs. the new two-pass
count→`[total]byte=[]`→copy idiom (RM-C2) — and compares the peak. Record the baseline. This crumb changes
NOTHING in `src/` (build-before-remove methodology: measure first, so RM-C3's win is quantified against a
real number, not a guess). It teaches nothing and consumes no seed → `[dry]`, reseed-class `none`.

## Where

- `src/runtime/teko_rt.c:1681-1683` — the `tk_obs` aggregate (`tk_obs_root`/`tk_obs_scoped` site tables,
  env-gated process-wide DIAGNOSTIC) — READ-ONLY here; RM-C1 uses it to attribute peak to the emit buffer.
  This is the MAINTAINED-C seed (the `teko_rt` exception) — not edited, only observed.
- `src/codegen/codegen.tks:128,130,132` — `cb`/`cb_str`/`cb_byte` (the thin emit-buffer append helpers over
  `cb_add`) and `append_fo` (`codegen.tks:2915`) — the buffer whose peak is measured; UNCHANGED here (the
  conversion is RM-C3, `0037`).
- NEW `examples/regressions/rm_c1_emit_shadow/` — the isolated `.tkr` shadow project that emits a large
  block both ways and compares peak (runs OUTSIDE `teko test .`).

NEW: the shadow `.tkr` project only; no `src/` change.

## How

1. **Attribute peak to the emit buffer.** Run the compiler under the `tk_obs` env gate on a large input and
   read `tk_obs_root`/`tk_obs_scoped` to attribute the peak fraction landing in the `cb`/`append_fo` buffer
   (the root-region allocations that are never freed today). Record the number as the baseline.
2. **Build the isolated shadow.** In `examples/regressions/rm_c1_emit_shadow/`, emit a large text block TWO
   ways in one standalone `.tkr`:
   - the OLD way: chained `out = cb(out, piece)` / `append_fo` accumulation (copy-grow — the 93% consumer);
   - the NEW way: count the total length, `var final: [total]byte = []` (zero-fill one pass), copy each piece
     by index (the RM-C2 idiom).
   Compare the transient peak of each. The shadow runs ISOLATED (never `teko test .` — the monomorph leak
   would crash the container).
3. **Record the baseline; change no `src/`.** RM-C1 is the measurement/shadow only (build-before-remove): it
   quantifies the target so RM-C3's conversion is judged against a real peak. No `src/` idiom is converted
   here — that is RM-C3.
4. **Confirm the guard.** The measurement runs under the 6.5 GiB `ulimit -v` cap; a blown guard is a
   root-cause finding to REPORT, never a raised ceiling. The shadow demonstrates the new idiom stays flat
   (non-growing peak) versus the old idiom's copy-grow spike.

## Rulings & laws

- **Teko-only:** the shadow is `.tkr`; `tk_obs` is READ-ONLY (the maintained-C `teko_rt` seed exception —
  observed, not edited); no `src/` change.
- **W15 full Javadoc** on any helper in the shadow project; no inline `//`.
- **Build-before-remove (owner methodology):** measure the baseline FIRST; the conversion (RM-C3) and the
  C-root removals (RM-C8/C9) come after, each proven against this number.
- **NO PUSHES / the guard is inviolable (CLAUDE.md):** the new idiom is non-growing; the 6.5 GiB guard is a
  root-cause fix target, never raised.
- **No reseed, no src change:** RM-C1 teaches nothing and consumes no seed → reseed-class `none`.
- **Safety:** NEVER `teko test .` (the shadow runs isolated); build in a subshell with `ulimit -v 6815744`;
  commit the green step.

## Fixtures

The shadow project IS the fixture (it does not exercise a `src/` path — it is a standalone peak comparison
the self-build never runs):

| fixture | asserts | expected |
|---|---|---|
| `rm_c1_emit_shadow` | emitting a large block via the new count→`[total]byte=[]`→copy idiom has a FLAT (non-growing) peak vs. the old `append_fo` copy-grow spike; the baseline fraction is recorded | 0 |

## Gate

`[dry]` — compile + the shadow fixture + fixpoint (byte-identical; no `src/` change). "Green" = the emit-
buffer peak fraction is attributed and recorded, the shadow shows the new idiom flat vs. the old idiom's
spike, `[dry]` build byte-identical (RM-C1 touches no `src/`). Reseed-class: `none`.

## Deps

`—` (RM-C1 is the baseline measurement; the conversion it feeds is RM-C3, which depends on RM-C2 + SM-R1).

## Done when

The codegen emit-buffer peak fraction is measured via `tk_obs` and recorded as the baseline, the isolated
`rm_c1_emit_shadow` `.tkr` proves the new count→copy idiom stays flat versus the old `append_fo` copy-grow,
`src/` is unchanged, and a `[dry]` build is byte-identical.
