---
seq: 0033
crumb-id: SM-S3
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [SM-R1]
sources:
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1205-1206"   # §10 Phase S — S3
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:199-258"     # §5 self/base/static grammar+checker
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:250-258"     # §5.4 byte-preservation
---

# 0033 · SM-S3 — sweep methods to `self`/`base`/`static`; remove loose-receiver parse

> Sweep methods to `self`/`base`/`static`; remove the loose-receiver parse + `allow_untyped_first` — rewrite
> the ~89 receiver sites to the synthetic-`self` form and delete the old loose-receiver acceptance.

## Goal

AUDIT/VERIFY-ONLY (already applied in src): confirm that the method-receiver migration is complete — every
method in `src/` has been rewritten to the new form with a synthetic `self` receiver (no source-named receiver
param), `base` for the superclass upcast, and the `static` modifier for receiver-less methods. Confirm that
the old loose-receiver parse path and the `allow_untyped_first` acceptance have been DELETE'd (they were kept
accepting through the additive G3 crumb, `0009`). The synthetic-`self` machinery is already in place
(`inject_synthetic_receiver`, `consume_static_modifier`, `parse_decl.tks` ~196-221, 398, 430, 585). Byte-
preserving: `params[0]` is still the untyped receiver with its `type_ann` rewritten to the struct name before
codegen (§5.4), so the emitted C is byte-identical — the fixpoint proves it. This is the LOAD-BEARING sweep
of Phase S (the biggest mechanical rewrite; the byte-identity gate is what proves a corpus-wide method rewrite
survives). This crumb is a verify-only audit; the work is DONE in src.

## Where

- `src/**/*.tks` — the ~89 method receiver sites — rewrite each explicit loose receiver to the synthetic
  `self` form; add `static` on the receiver-less methods; use `base` for superclass upcalls.
- `src/parser/parse_decl.tks:34` — `parse_params` `allow_untyped_first` — REMOVED (the additive window that
  accepted BOTH old loose receiver AND synthetic `self`); after the sweep nothing needs the loose form.
- `src/parser/parse_decl.tks:157,173` — `consume_static_modifier` / `inject_synthetic_receiver` — KEPT
  (the synthetic-`self` machinery is now the ONLY path).
- `src/checker/typer.tks:3110-3128` — the synthetic `base: <Base> = <self upcast>` prepend — now
  unconditional for `has_base` methods.
- `src/codegen/synth.tks:199,314,365,418,475` — the method emitters — now emit `name="self"` + `is_static`
  directly (byte-neutral: `params[0]` receiver `type_ann` still rewritten to `Named{struct_name}` before
  codegen).
- `src/checker/collect.tks:459` — `synthesize_mret_structs` — UNCHANGED (multi-return synthesis is
  orthogonal to the receiver form).
- `src/**/*.tkt` — the method-declaration expectation corpus — rewritten to the swept form in lockstep.

NEW: no new surface; a source rewrite + a clean removal of the loose-receiver acceptance.

## How

**VERIFY-ONLY audit (work already done):** No fresh sweep is needed — the receivers are already rewritten and
the acceptance is already removed.

1. **Confirm ~89 receiver sites are rewritten.** Audit `src/` to confirm every method uses the synthetic `self`
   form (no explicit loose receiver param), receiver-less methods are marked `static`, and superclass upcasts
   use `base`. `self` and `base` remain contextual identifiers (not reserved keywords, except `static` which IS
   reserved).
2. **Confirm `allow_untyped_first` is removed (clean expurgo).** Verify that `allow_untyped_first` acceptance
   (`parse_decl.tks:34`) and the loose-receiver parse fallback are gone — a clean expurgo with NO tombstone
   diagnostic. The synthetic-`self` path is the only declaration method.
3. **Verify the emit is byte-neutral.** Confirm that `params[0]` remains the untyped receiver whose `type_ann`
   is rewritten to `Named{struct_name}` before codegen (§5.4); the method emitters (`synth.tks:199…475`)
   emit `name="self"` + `is_static` directly. Verify that the invariants `is_instance`, `method_sig_matches`,
   `is_static_method` are preserved.
4. **Byte-identity is the proof.** Build gen2 on the SM-R1 seed, run the scoped regression, prove
   `gen2==gen3` byte-identical — the proof the corpus-wide method rewrite changed no semantics.
5. **This is the load-bearing sweep.** The largest mechanical rewrite (~89 sites + `synth.tks`); the fixpoint
   byte-identity is the gate that matters most — a single mis-rewritten receiver would break `gen2==gen3`.

## Rulings & laws

- **Teko-only:** source `.tks`/`.tkt` rewrite + parser removal `.tks`; no C twin.
- **W15 full Javadoc** unaffected; no inline `//` introduced.
- **Byte-preserving (§5.4):** `params[0]` receiver + `type_ann`-rewrite keeps the emit byte-identical; the
  fixpoint proves it.
- **`base` NOT reserved (§14 R2):** it stays a contextual identifier (live production local name); only
  `static` is a reserved keyword; `self` is contextual.
- **Removals = clean expurgo, NO tombstone (CLAUDE.md):** `allow_untyped_first` + the loose-receiver parse
  are deleted cleanly once the source is swept.
- **Safety:** NEVER `teko test .`; build gen2 in a subshell with `ulimit -v 6815744`; commit each green
  batch; NO reseed (fixpoint-rebuild); fixpoint `gen2==gen3`; sweep `.tkt`/`.tkr` in lockstep.

## Fixtures

`none — the fixpoint self-build exercises this`. The sweep rewrites the compiler's own ~89 method receivers
and removes the loose-receiver acceptance; building gen2 byte-identical IS the exercise. A mis-rewritten
receiver breaks `gen2==gen3` directly; the loose-receiver-now-rejected behavior is a removal the build
catches.

## Gate

`[fixpoint]` — build gen2 on the SM-R1 seed + scoped regression + `gen2==gen3` byte-identity. "Green" =
audit confirms every method in `src/` + `.tkt` uses the synthetic `self` / `base` / `static` form (sweep
complete + byte-identical), `allow_untyped_first` + the loose-receiver parse are cleanly removed (no
tombstone), the emit is byte-neutral, the build is byte-identical (`gen2==gen3`). Reseed-class:
`fixpoint-rebuild` (this crumb is verify-only).

## Deps

`SM-R1` (the seed must accept the synthetic-`self` form before the ~89 sites are swept to it).

## Done when

Audit confirms every method in `src/` + `.tkt` uses synthetic `self` / `base` / `static` (sweep complete +
byte-identical), `allow_untyped_first` and the loose-receiver parse are cleanly expurgated (no tombstone),
the emit invariants hold, and gen2 on the SM-R1 seed is byte-identical (`gen2==gen3`). The sweep is
DONE-verified.
