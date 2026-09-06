---
seq: 0037
crumb-id: RM-C3
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [RM-C2, SM-R1]
sources:
  - "docs/design/reducao-memoria-arrays-0.3.1.md:239-244"   # C3 — convert the codegen emit buffer
  - "docs/design/reducao-memoria-arrays-0.3.1.md:110-126"    # the 93% PUSH bill; largest single drop
  - "docs/design/reducao-memoria-arrays-0.3.1.md:225-227"    # build-before-remove; fixpoint each converted file
---

# 0037 · RM-C3 — convert codegen emit buffer to spread-literal `b"…"` + `..str` index-materialize (the 93%)

> Convert the codegen emit buffer (`cb`/`append_fo`) to spread-literal `b"…"` + `..str` index-materialize —
> the single largest memory drop of the whole campaign.

## Goal

Rewrite the codegen's C emission from the chained-append idiom `out = cb(out, …)` / `append_fo` (copy-grow —
the 93% peak-memory consumer RM-C1 baselined, −3.0 to −3.5 GB) to the no-push idiom RM-C2 anchors: each
emitted piece is a SPREAD-LITERAL — `b"…"` for the literal bytes, `..str` for the dynamic parts —
accumulating a `total` length and materializing `var final: [total]byte = []` (one zero-fill pass) filled by
index (`copy(final, k, piece); k += piece.len`). The dense targets are the `emit_*` functions of
`codegen.tks`; `cb`/`cb_str`/`cb_byte` become thin shims over the new idiom until they vanish. This is the
LARGEST single drop of the memory campaign (the goal ≤1.5 GB is reached at C4/C5, but C3 is the main blow).
It core-consumes the SM-R1 seed and the RM-C2 `copy` primitive, rebuilds the compiler on them, and teaches
nothing new → `fixpoint-rebuild`. Fixpoint per converted file (byte-identity is the proof each `emit_*`
rewrite preserves the emitted C exactly).

## Where

- `src/codegen/codegen.tks:128,130,132` — `cb`/`cb_str`/`cb_byte` — become THIN shims over the spread-literal
  idiom (they route to `copy`/index-materialize until the `emit_*` callers no longer need them, then vanish).
- `src/codegen/codegen.tks:2522,2915,7919` and every `emit_*` — the chained `out = cb(out, …)` /
  `append_fo`/`push_fo` accumulations — rewritten to spread-literal pieces + `total` + `[total]byte`
  index-materialize. This is the dense bulk of the conversion.
- `src/runtime/arena.tks` — `copy(dst, at, src)` (RM-C2, `0020`) — the index-join primitive each piece is
  copied through; UNCHANGED (already seeded).
- `src/codegen/codegen.tks:5706` — the `list::push`/`with_cap`/`mem::push_fo` recognition in the emit path —
  updated as the `emit_*` callers drop the growth idiom.

NEW: no new surface; a byte-preserving rewrite of the codegen emit idiom over the RM-C2 primitive.

## How

1. **Convert each `emit_*` to spread-literal pieces.** Replace `out = cb(out, "#define TK_ARENA_"); out =
   cb(out, sym); out = cb(out, "\n")` chains with a single spread-literal piece
   `[..b"#define TK_ARENA_", ..sym, b'\n']` — `b"…"` for the constant bytes, `..str` for the dynamic
   segments. The compiler may const-fold a fully-known piece to a pure literal.
2. **Accumulate `total`, materialize once, copy by index.** For a group of pieces: sum each piece's length
   into `total`, allocate `var final: [total]byte = []` (one zero-fill pass, `emit_slice_of_len` inline),
   then `copy(final, k, piece); k += piece.len` for each (the RM-C2 count→alloc→copy idiom). No growth, no
   `append_fo`, no dead buffer stranded in the root region.
3. **Shrink `cb`/`cb_str`/`cb_byte` to shims, then delete.** Keep them as thin wrappers over the new idiom
   while callers migrate, so the conversion can land file-by-file; once no `emit_*` calls them they are
   removed (clean expurgo, no tombstone) — but the full growth-primitive removal is COL-F2/RM-C-later; here
   they simply stop being called.
4. **Fixpoint per converted file.** After each `emit_*`/file conversion, build gen2 on the SM-R1 seed and
   prove `gen2==gen3` byte-identical — the emitted C must be UNCHANGED (the rewrite is a memory idiom swap,
   not an output change). Commit each green file; sweep `.tkt`/`.tkr` if any signature moved.
5. **Watch the peak drop.** Against the RM-C1 baseline, confirm the emit-buffer peak fraction collapses
   (−3.0 to −3.5 GB) while `gen2==gen3` holds — the win is real only if byte-identity survives.

## Rulings & laws

- **Teko-only:** codegen `.tks` over the pure-Teko `copy` (RM-C2); NO `teko_rt.c` (the maintained-C seed is
  untouched — `copy` is pure Teko).
- **W15 full Javadoc** on every rewritten/new `emit_*` helper; flatten/extract to cut the append chains; no
  inline `//`.
- **NO PUSHES / ZERO dynamic growth (CLAUDE.md):** the emit buffer becomes count→`[total]byte=[]`→copy; no
  growth primitive survives in the emit path.
- **Byte-preserving:** the emitted C is IDENTICAL — the fixpoint `gen2==gen3` per file is the proof.
- **Build-before-remove:** the conversion lands and proves green BEFORE any growth-primitive removal
  (COL-F2, `0093`, M3); RM-C3 removes nothing structurally, it swaps the idiom.
- **Safety:** NEVER `teko test .`; build gen2 in a subshell with `ulimit -v 6815744` — the drop is the point,
  a blown guard is a root-cause fix; commit each green file; NO reseed (fixpoint-rebuild); fixpoint
  `gen2==gen3`; sweep `.tkt`/`.tkr` on any signature move.

## Fixtures

`none — the fixpoint self-build exercises this`. RM-C3 rewrites the codegen's OWN emit path; building gen2 on
the SM-R1 seed and proving `gen2==gen3` byte-identical per converted file IS the exercise (the emitted C must
not change). The peak-drop is measured against the RM-C1 baseline, not a new `.tkr`.

## Gate

`[fixpoint]` — build gen2 on the SM-R1 seed + scoped regression + `gen2==gen3` byte-identity, per converted
file. "Green" = every `emit_*` accumulates via spread-literal + `total` + `[total]byte` index-materialize
(no `append_fo` copy-grow), the emitted C is byte-identical (`gen2==gen3`), and the emit-buffer peak drops
against the RM-C1 baseline. Reseed-class: `fixpoint-rebuild`.

## Deps

`RM-C2` (the `copy` index-join primitive + count→`[total]byte=[]`→copy idiom) and `SM-R1` (the seed that
knows the idiom + `of_len`).

## Done when

The codegen emit buffer is fully converted to spread-literal `b"…"` + `..str` + `[total]byte` index-
materialize over `copy`, `cb`/`append_fo` no longer copy-grow, every converted file is byte-identical
(`gen2==gen3`), and the emit-buffer peak has dropped against the RM-C1 baseline.
