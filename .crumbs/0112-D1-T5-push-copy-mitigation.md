---
seq: 0112
crumb-id: D1-T5
milestone: M5
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [COL-F2]
sources:
  - "docs/design/mudancas-superficie-0.3.1.md:1664"                    # Doc-2 push/copy-grow mitigation
  - "docs/design/plano-mestre-0.3.1-implementacao.md:305"              # M5 D1-T5 row
  - "docs/design/reducao-memoria-arrays-0.3.1.md:27-38"                # Eixo A — the four natures
  - "docs/design/arena-especificacao-unica-0.3.1.md:74-83"            # copy-grow lives in the slice
---

# 0112 · D1-T5 — push/copy-grow mitigation via known-size arrays

> The final push/copy-grow mitigation: confirm every remaining allocation is a KNOWN-SIZE array (the four
> natures — MAP / PARSE-SCAN / FILTER / OUTPUT-BUFFER), the copy-grow class fully eliminated. The
> perf-fixpoint capstone of the memory campaign. **Judgment call flagged: the `with_cap` pre-capacity lever
> named in the Doc-2 row was REMOVED by COL-F2 — the law-first resolution is known-size-only.**

## Goal

Doc-2:1664 named "push/copy-grow mitigation via known-size arrays + `with_cap` pre-capacity (levers already
exist)". By this seq COL-F2 (the E2 expurgo, this crumb's dep) has DELETED the entire dynamic-growth surface
— `push`/`empty`/`with_cap`/`grow_inplace` and the C `tk_slice_push_r` machinery. So the `with_cap`
pre-capacity lever the Doc-2 row named **no longer exists**, superseded by the owner's "zero dynamic growth"
law (2026-08-18). This crumb is therefore the FINAL perf-fixpoint CAPSTONE of the memory campaign under the
current law: verify the copy-grow class is fully gone and every remaining allocation is a known-size array
(exact-count pre-allocation + index-write / two-pass count-then-fill / widen-then-cut / spread-literal
output-buffer), then measure the peak-memory landing (target the ≤1.5 GiB trajectory). It is
**byte-preserving** where the conversions already landed (COL-F2 forced them) and a **fixpoint-rebuild**
(the native object reproduces). No capability, no new surface — a confirmation + measurement fixpoint.

### Judgment call (flagged, law-first resolution — no HALT)

The Doc-2 goal-cell lists `with_cap` as a lever, but this crumb's own dep (COL-F2) removed `with_cap` under
the owner's "NO PUSHES + zero dynamic growth" law (`with_cap`/`grow_inplace` banned as the copy-grow class).
**Law tension:** the Doc-2 row (pre-dating the ban) vs the standing zero-growth law. **Resolution
(law-first):** the zero-growth law WINS (it is the newer, harder owner ruling, and the whole memory campaign
exists to remove exactly this class). D1-T5 is scoped as **known-size-only**: NO `with_cap` reintroduction;
the mitigation is the known-size idiom the expurgo already forced. Recorded so the implementer does not
re-add `with_cap` to satisfy the stale row.

## Where

- `src/checker/*` / `src/lir/*` / `src/codegen/*` / `src/collections/*` — CONFIRM zero remaining copy-grow
  sites: grep the whole tree for any residual dynamic-growth pattern (a `loop { xs = … append … }` shape) —
  every one must already be a known-size conversion by COL-F2. A residual is a COL-F2 gap, REPORTED up.
- `src/codegen/codegen.tks:3167` `emit_slice_of_len` — the known-size backing (`[n]T = []` zero-fill) the
  four natures target; confirm every producer routes through it.
- The profiler hooks (`tk_obs`) — measure the final peak (the `tk_slice_push_r` line must read ~0 MB).

## How

1. **Verify the copy-grow class is gone** (the campaign's exit criterion): grep for any dynamic-growth shape
   tree-wide; the `tk_slice_push_r` machinery is deleted (COL-F2), so a residual would be a compile error
   or a re-introduced pattern — either is a COL-F2 gap REPORTED up, not patched here.
2. **Confirm the four natures everywhere** (§Eixo A): every allocation is MAP (`[]T` of `src.len` +
   index-write), PARSE-SCAN (two passes: count then fill), FILTER (widen to upper bound, fill a `count`,
   cut `slice[0..count]`), or OUTPUT-BUFFER (spread-literal `[..b"…", ..dyn]` + copy-by-index). No lever
   grows a buffer.
3. **Do NOT reintroduce `with_cap`** (the flagged judgment call): the pre-capacity lever is removed under
   the zero-growth law; known-size is the only mitigation.
4. **Measure the landing**: the peak-memory trajectory target is ≤1.5 GiB (from the +6 GB pre-campaign
   peak); the native build must fit well under the 6.5 GiB guard. Record the profiler reading.

```teko
/**
 * assert_no_copy_grow — a build-time audit (over the lowered corpus) that no allocation site uses a
 * dynamic-growth shape: every array is known-size (exact-count pre-allocation + index-write, two-pass
 * count-then-fill, widen-then-cut, or spread-literal output). The copy-grow class (push/grow_inplace/
 * with_cap) is gone post-COL-F2; this is the campaign's exit assertion, NOT a with_cap reintroduction.
 *
 * @param prog  the lowered program
 * @return ok, or the first residual dynamic-growth site (a COL-F2 gap to report up, not patch here)
 * @throws when a dynamic-growth shape survives (an expurgo gap)
 * @since 0.3.1
 */
fn assert_no_copy_grow(prog: lir::LModule): error?
```

5. **Fixpoint**: the corpus is already known-size (COL-F2 forced it); the native object reproduces;
   `gen2==gen3`.

## Rulings & laws

- **Teko-only:** confirmation + measurement over `.tks`; no C twin (the C growth machinery is deleted).
- **W15 full Javadoc** on any survivor touched; flatten; no `//`.
- **NO PUSHES + zero dynamic growth (owner 2026-08-18) WINS over the stale Doc-2 `with_cap` lever:** the
  mitigation is known-size-only; `with_cap` is NOT reintroduced (the flagged judgment call).
- **The four natures (Eixo A):** MAP / PARSE-SCAN / FILTER / OUTPUT-BUFFER — every site is calculable, none
  impossible.
- **Peak target ≤1.5 GiB; guard 6.5 GiB inviolable:** a blown guard is a root-cause fix, never a raised
  ceiling.
- **Adjacent residual = REPORT up, not a new issue:** a surviving copy-grow site is a COL-F2 gap reported,
  not patched here.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  `[fixpoint]` — native-object-reproducible `gen2==gen3`; sweep `.tkt` after any audit hook.

## Fixtures

none — the fixpoint self-build exercises this. The compiler IS the largest known-size-array consumer; the
mitigation is measured by the profiler peak (`tk_slice_push_r` ~0 MB) + the native fixpoint reproducing. A
residual copy-grow surfaces as a peak-memory regression or an `assert_no_copy_grow` stop, not a missed
`.tkr`.

## Gate

`[fixpoint]` — build gen2 + scoped regression + `gen2==gen3` native-object byte-identity. "Green" = zero
copy-grow sites remain (every allocation is known-size, `with_cap` NOT reintroduced), the peak memory lands
on the ≤1.5 GiB trajectory well under the 6.5 GiB guard, and the native object reproduces. Reseed-class:
`fixpoint-rebuild`. This is the final crumb of the 0.3.1 wave.

## Deps

`COL-F2` — verbatim from 000-INDEX (the expurgo that deleted the copy-grow surface + `with_cap`; this crumb
confirms the known-size landing under the zero-growth law).

## Done when

The copy-grow class is confirmed fully gone, every allocation is a known-size array (the four natures,
`with_cap` NOT reintroduced per the flagged law-first resolution), the peak memory lands on the ≤1.5 GiB
trajectory under the guard, and the native-object `gen2==gen3` fixpoint reproduces — closing the memory
campaign and the 0.3.1 wave.
