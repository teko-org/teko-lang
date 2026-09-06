---
seq: 0039
crumb-id: RM-C5
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [RM-C4]
sources:
  - "docs/design/reducao-memoria-arrays-0.3.1.md:252-255"   # §6 crumb C5
  - "docs/design/reducao-memoria-arrays-0.3.1.md:246-250"   # §6 crumb C4 — the 4 naturezas classification
  - "docs/design/reducao-memoria-arrays-0.3.1.md:27-38"     # §1 Eixo A (kill the growth)
  - "docs/design/reducao-memoria-arrays-0.3.1.md:107-129"   # §4 mem_model — the C4/C5 drop
---

# 0039 · RM-C5 — convert lir + backend + parser + codegen residual (LEnv/LowerCtx parallel arrays)

> Convert `src/lir/*` (877 push sites, incl. the 6 parallel arrays of `LEnv`/`LowerCtx`), `src/backend/*`
> (633), `src/parser/*` (293), and the codegen residual (163) from copy-grow `push` to the four
> exact-size natures (MAP / PARSE / FILTER / BUFFER). At the end of this crumb, NO `src/` site calls the
> growth path anymore — the last leg of Eixo A.

## Goal

Eixo A ("kill the growth") converts every `teko::list::push` copy-grow site in the compiler to
exact-size pre-allocation + write-by-index, killing the 20.3 M amortized copy-grows that leak into the
never-freed `root` arena (93% of the ~6.2 GB build peak). RM-C3 did the codegen emit buffer (the 93%
single consumer) and RM-C4 did `src/checker/*` + `src/build/*`. This crumb (C5) finishes the residual:
`src/lir/*` — most notably the six PARALLEL ARRAYS inside `LEnv` (`names`/`vregs`/`len_vregs`/`has_len`/
`is_slot`/`is_scalar_slot`/`slot_ltype`) and the `LowerCtx` growth — plus `src/backend/*`,
`src/parser/*`, and the ~163 leftover `src/codegen` sites RM-C3 did not touch. Byte-mover for the
emitted `teko.c`? NO — the conversion is layout-PRESERVING (same `{ptr,len}`, pure zero-fill, no tag):
the emitted program is byte-identical; only the allocator footprint changes. It drives a
`fixpoint-rebuild` reseed because it touches the compiler's own lowering source (the core consumes the
swap), but teaches nothing new. This is the crumb that gets the peak under the ≤1.5 GB target together
with C4 (mem_model: C4/C5 land ~1.2–1.3 GB).

## Where

- `src/lir/lower.tks:5` — `LEnv` (the 7 parallel arrays `names`/`vregs`/`len_vregs`/`has_len`/`is_slot`/
  `is_scalar_slot`/`slot_ltype`) and its builders `lenv_bind`/`lenv_bind_fat`/`lenv_bind_fat_slot`/
  `lenv_prefix` (`:31`,`:40`,`:49`,`:15`) — each rebuilds every array by push; convert each to a MAP
  `of_len(env.len + 1)` copy-then-set, so a bind is one exact-size allocation, not seven copy-grows.
- `src/lir/*` residual push sites (877 total) — classify each into MAP / PARSE / FILTER / BUFFER and
  convert per that nature.
- `src/backend/*` (633 sites) — same four natures (isel/regalloc/encoder scratch vectors).
- `src/parser/*` (293 sites) — token/decl/statement accumulators; mostly PARSE (two-pass) and MAP.
- `src/codegen/*` residual (163 sites RM-C3 left) — the non-emit-buffer scratch.

No NEW `exp`/`pub` surface: this is a mechanical rewrite of existing internal helpers. The one new kind
of helper is a private exact-size builder per aggregate that today grows (illustrated below).

## How

The four natures (owner classification, `reducao §6 C4`) and their exact-size rewrite:

1. **MAP** (`out.push(f(src[i]))` for every `i`): the output length is EXACTLY `src.len`. Rewrite to
   `var out: [src.len]T = []` then `out[i] = f(src[i])` by index. The `LEnv` builders are the flagship
   MAP case — a bind produces an env whose every array is `old.len + 1`.

```teko
/**
 * lenv_bind — bind `name` to scalar vreg `vreg`, returning an env grown by exactly one slot. MAP
 * nature (RM-C5): the seven parallel arrays are each materialized at the known final length
 * `env.names.len + 1` via `[n]T = []` + write-by-index, replacing seven `teko::list::push` copy-grows
 * (each of which abandoned its previous backing in `root`). Layout-preserving: the produced `LEnv` is
 * value-identical to the push-built one, so the emitted `teko.c` is byte-identical.
 *
 * @param env   the environment to extend
 * @param name  the binding name
 * @param vreg  the scalar virtual register the name resolves to
 * @return      a new `LEnv` one slot longer, every array exact-sized
 * @since 0.3.1
 */
pub fn lenv_bind(env: LEnv, name: str, vreg: u32): LEnv
```

2. **PARSE** (a loop whose output length is not known up front — the parser): TWO PASSES — pass 1
   counts the exact element total, pass 2 allocates `[total]T = []` and fills by index. No growth
   between passes. Applies to `src/parser/*` statement/decl vectors.
3. **FILTER** (`if pred(src[i]) { out.push(src[i]) }`): allocate the MAXIMUM (`of_len(src.len)`), fill
   by a running `count`, then cut `out[0..count]` at the end (the cut is a zero-copy sub-slice, no
   growth). Applies to isel/regalloc live-set scratch in `src/backend/*`.
4. **BUFFER** (byte accumulation): the RM-C3 spread-literal idiom (`b"…"` + `..str` + count →
   `[total]byte = []` by index) for any residual byte builder in `src/codegen/*`.

5. **Sequence module-by-module, fixpoint after each.** Convert `src/lir` first (the `LEnv`/`LowerCtx`
   arrays are the densest), then `src/backend`, then `src/parser`, then the codegen residual. After each
   module: `[fixpoint]` scoped rebuild — the emitted `teko.c` must be byte-identical (a divergence means
   a nature was misclassified, e.g. a FILTER whose cut length was computed wrong). Do NOT batch all four
   modules into one reseed; one module = one gate.
6. **The peak-guard is the proof.** Build in a subshell under `ulimit -v 6815744` (6.5 GiB); the peak
   MUST fall (C4/C5 → ~1.2–1.3 GB per mem_model). A blown guard is a root-cause fix (a nature left
   growing), never a raised ceiling.

## Rulings & laws

- **Teko-only:** all conversions in `.tks` (`src/lir`,`src/backend`,`src/parser`,`src/codegen`); C twins frozen.
- **W15 full Javadoc** on every rewritten builder (pub + private); flatten early-return; no inline `//`.
- **Layout-preserving (owner, `reducao §7`):** same `{ptr,len}`, pure zero-fill, no tag → the emitted
  `teko.c` is byte-identical; the migration is scaled-green (new idiom coexists with old during the
  sweep, module by module).
- **Eixo A first (`reducao §8` decisão-em-aberto):** kill the push before the Eixo-C restructure that
  touches the same modules — this crumb is the factual pre-req of RM-C10+.
- **No removal here:** the growth ROOTS (`tk_slice_push*`, `append_fo`, …) are removed only at RM-C9
  (`0095`), AFTER every `src/` caller is converted — the "build/convert before removing the root"
  methodology. This crumb leaves the roots in place.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit each converted
  module as its own green step; reseed ONLY at the module fixpoint; fixpoint `gen2==gen3` byte-identical;
  sweep `.tkt`/`.tkr` after the `LEnv`/`LowerCtx` signature-touching edits.

## Fixtures

none — the fixpoint self-build exercises this. The compiler compiling ITSELF drives every converted
`src/lir`/`src/backend`/`src/parser`/`src/codegen` path; the byte-identity `gen2==gen3` proof plus the
falling peak-guard ARE the regression. No isolated `.tkr` adds coverage the self-build lacks.

## Gate

`[fixpoint]` — build gen2 (`--no-verify --release`, `TEKO_BACKEND=c`, `TEKO_CC=clang`,
`ulimit -v 6815744`) + scoped regression + `gen2==gen3` byte-identity, per converted module. "Green" =
every module's emitted `teko.c` is byte-identical to before the conversion AND the build peak falls
toward ~1.2–1.3 GB (Eixo A complete: no `src/` site calls the growth path). Reseed-class:
`fixpoint-rebuild`.

## Deps

`RM-C4`.

## Done when

`src/lir` (incl. the `LEnv`/`LowerCtx` parallel arrays), `src/backend`, `src/parser`, and the codegen
residual carry ZERO copy-grow `push` sites (all four natures converted), the emitted `teko.c` is
byte-identical (`gen2==gen3`), and the build peak has dropped to ~1.2–1.3 GB under the 6.5 GiB guard.
