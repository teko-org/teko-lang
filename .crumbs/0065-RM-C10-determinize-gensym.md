---
seq: 0065
crumb-id: RM-C10
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [RM-C5]
sources:
  - "docs/design/reducao-memoria-arrays-0.3.1.md:274-278"          # C10 — determinize gensym (pre-condition of Eixo C)
  - "docs/design/reducao-memoria-arrays-0.3.1.md:504-517"          # R4 — buf.len-derived names break the streaming fixpoint
  - "docs/design/reducao-memoria-arrays-0.3.1.md:627-634"          # C10 is the mandatory pre-condition of C11/C12
---

# 0065 · RM-C10 — determinize gensym (drop `buf.len`-derived temp names → global counter)

> Replace every temporary name derived from the emit buffer's length with a deterministic global counter, so
> the per-unit streaming pipeline (C11/C12) cannot make `teko.c` diverge — the mandatory pre-condition of Eixo C.

## Goal

Today many codegen temporaries take their name from the CURRENT emit-buffer length: `$"_teq{buf.len}"`,
`$"_sb{buf.len}"`/`_si{buf.len+1}`/`_sp{buf.len+2}`/`_sl{buf.len+3}`, `$"_wp{buf.len}"`/`_wc{buf.len}`,
`$"_gi{buf.len}"`, `$"_ac{buf.len}"`, `$"_sfc{buf.len}"`, `$"_rb{buf.len}"`, `$"_cs/_ci/_ca{buf.len}"`, plus
`fresh_named(prefix, buf.len)` (`codegen.tks:815,2265,2305,2505-2508,2541-2542,2568,2608,2633,2655,2665,2674,
2720,2770-2772,2798,…`) and `fresh_tmp_name(buf.len)` (`codegen.tks:3840`). This is a **load-bearing latent
bug** (`reducao…` R4): the moment emission becomes per-unit streaming (C11/C12), the buffer length at a given
site changes with unit ordering/boundaries, temp names shift, and `teko.c` DIVERGES → the fixpoint breaks. This
crumb **determinizes gensym**: every temporary derives from a deterministic global counter (or a
`(namespace, fn_idx, seq)` triple), NOT the buffer length. No streaming yet — a pure mechanical rename of
temporaries. It is **byte-preserving by requirement**: `teko.c` must come out BYTE-IDENTICAL (validate
byte-by-byte) — the rename is chosen so the emitted names are stable, not so they change. It drives no teaching
reseed; a `fixpoint-rebuild` swap the core consumes. It BLOCKS C11.

Not blocked by any open dependency (its dep RM-C5 landed the lir/backend residual conversion); this is
executable design.

## Where

- `src/codegen/codegen.tks:3840` — `fresh_tmp_name(buf_len: i64)` — retarget to draw from the global counter,
  not `buf.len`.
- `src/codegen/codegen.tks:3932` — `fresh_named(prefix: str, n: i64)` — every call site passing `buf.len to
  i64` (`:815,2305,2608,2633,2720,2798,…`) passes the counter instead.
- `src/codegen/codegen.tks:2265,2505-2508,2541-2542,2568,2655,2665,2674,2770-2772` — the inline
  `$"_x{buf.len}"` temp-name sites — replace `buf.len` with a `next_temp()` call (and the `+1/+2/+3` offset
  groups with consecutive counter draws).
- NEW private fn: `next_temp()` (the deterministic counter draw) + its module-level counter state — see How §2.
- Any implicit non-determinism (`map`/`hashset` iteration order, global state) touched by the rename — audit per
  `reducao…` §R4 (512-514): same input → same `.tkb` → same `teko.c`.

## How

1. **Choose the deterministic key.** Two law-clean options (`reducao…` C10): a monotonic GLOBAL counter reset
   at codegen start, or a `(namespace, fn_idx, seq)` triple. The global counter is simplest and stable IF the
   emission order is deterministic (it is — namespaces + items iterate in a fixed order). The triple is more
   robust under future reordering. DECISION: a global counter, reset per codegen run, incremented at each
   draw — because the current whole-program emission order is already deterministic and the counter reproduces
   the EXACT sequence of names `buf.len` produced today (validated byte-identical). The W15 surface:

```teko
/**
 * next_temp — draw the next deterministic temporary ordinal from the codegen-run global counter. Replaces the
 * `buf.len`-derived naming that couples a temp's name to the emit buffer's byte length — the coupling that
 * would make per-unit streaming (C11/C12) shift names and diverge `teko.c`, breaking the fixpoint
 * (`reducao…` R4). The counter is reset at the start of each codegen run and produces the SAME name sequence
 * the buffer length produces today, so `teko.c` stays byte-identical.
 *
 * @return  the next temporary ordinal, monotonic within a codegen run
 * @since 0.3.1
 */
fn next_temp(): i64
```

2. **Add the module-level counter + reset.** A single mutable ordinal, reset at codegen entry (`codegen.tks`'s
   top-level emit entry). Every temp draw calls `next_temp()`; the `+1/+2/+3` offset groups (`_sb`/`_si`/`_sp`/
   `_sl` at `:2505-2508`) become four consecutive `next_temp()` draws preserving their relative order.
3. **Mechanical rename, byte-identical.** Replace each `buf.len`(`±k`) in a temp-name site with the counter
   draw. Because the counter reproduces the today-sequence, the emitted `teko.c` is byte-for-byte the same —
   VALIDATE byte-by-byte (`reducao…` C10). If a byte shifts, the counter draw order does not match the old
   `buf.len` order at some site — fix the draw order, do not accept the shift.
4. **Audit residual non-determinism** (`reducao…` §R4 512-514): the rename must not leave any name depending on
   `map`/`hashset` iteration order or implicit global state — same input must give the same `.tkb` and the same
   `teko.c`.
5. **Fixpoint proves it.** `gen2==gen3` byte-identical AND the pre-rename `teko.c` == post-rename `teko.c`
   (the mechanical-rename invariant).

## Rulings & laws

- **Teko-only:** the rename lands in `src/codegen/codegen.tks` (`.tks`); no C twin.
- **W15 full Javadoc** on `next_temp` and any touched helper; flatten/extract; no inline `//`.
- **Removals = clean expurgo, NO tombstone:** removes no surface (temporaries are internal); it is a rename, not
  a removal.
- **Determinism law (`reducao…` R4/R6):** the artifact must be reproducible — same input → same `.tkb` → same
  `teko.c`. `buf.len`-derived names violate this under streaming; the counter restores it. This is the
  MANDATORY pre-condition of C11/C12 (`reducao…` 627-634).
- **Byte-preservation requirement (`reducao…` C10):** `teko.c` must be byte-identical after the rename —
  validated byte-by-byte, not merely fixpoint-stable.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744` cap — a blown guard is a
  root-cause fix, never a raised ceiling; commit each green step; **reseed ONLY at a [RITUAL]** — this is
  `fixpoint-rebuild`, no teaching seed harvested; fixpoint `gen2==gen3` byte-identical; sweep `.tkt`/`.tkr`
  after the signature change.

## Fixtures

none — the fixpoint self-build exercises this. (Gensym runs on every codegen path; the byte-identity
validation — pre-rename `teko.c` == post-rename `teko.c`, then `gen2==gen3` — IS the regression. No isolated
oracle can localise a temp-name shift better than the whole-program byte diff.)

## Gate

`[fixpoint]` — build gen2 + the byte-identity check (pre-rename `teko.c` == post-rename `teko.c`) + `gen2==gen3`
byte-identity. "Green" = every temporary draws from the deterministic counter, no name derives from `buf.len`,
and the emitted `teko.c` is byte-for-byte unchanged by the rename. **Reseed-class:** `fixpoint-rebuild`
(core-consumes; teaches nothing; no reseed harvested).

## Deps

`RM-C5` (`0039` — the lir+backend+parser+codegen residual conversion; the codegen this rename edits sits on it).

## Done when

every codegen temporary derives from a deterministic global counter (never `buf.len`), the emitted `teko.c` is
byte-identical to before the rename (validated byte-by-byte), C11 is unblocked, and a `[fixpoint]` build is
`gen2==gen3` byte-identical.
