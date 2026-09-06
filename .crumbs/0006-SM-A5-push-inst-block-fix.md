---
seq: 0006
crumb-id: SM-A5
milestone: M1
gate: "[RITUAL]*"
reseed-class: "(folds R1)"
deps: ["SM-P1"]
sources:
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1166-1167"# §10 Phase A — A5
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1245-1247"# §11 push_inst_block is NOT DPS
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1332-1339"# §14 R1 — A5 must be green pre-R1
---

# 0006 · SM-A5 — `push_inst_block` self-append point-fix

> `push_inst_block` self-append point-fix (AL3 `grow_inplace` boundary; NOT DPS).

## Goal

The THIRD native fixpoint blocker, `push_inst_block`, is NOT a DPS case — it is the **self-append /
materialize-at-append boundary** (the AL3 `grow_inplace` seam where a slice header is grown in place
while an inst block appends to itself). This crumb is its SEPARATE point-fix (SM-P1 confirms the facet).
It MUST land and prove green BEFORE the reseed (R1) so the seed does not lock in a still-crashing native
chain (§14 R1). It is a targeted correctness fix at the self-append site, not a redesign; it moves bytes
only at that site; its seed folds into SM-R1.

## Where

- `push_inst_block` (grep `src/lir/lower.tks` / `src/codegen/codegen.tks` for the self-append inst-block
  emitter) — the crash site; fix the self-append / grow-in-place boundary so the header and the appended
  block do not alias-corrupt.
- The AL3 `tk_slice_grow_inplace` boundary (`lower.tks`, the `grow_inplace` lowering) — the seam
  `push_inst_block` rides; confirm the header update and the append order do not overlap.
- SM-P1 probe `probes/dps_pin_push_inst_block` — the minimal repro this crumb turns to exit `0`.

## How

1. **Confirm the facet from SM-P1.** `push_inst_block` pins to the self-append/AL3 boundary (not the
   return facet). If SM-P1 disconfirmed, re-pin before fixing — the fix targets the exact aliasing the
   objdump showed.
2. **Fix the self-append boundary.** At the `push_inst_block` site, the crash is the slice header being
   grown in place while the block appends to its own backing (the AL3 `grow_inplace` seam). Apply the
   point-fix per the CLAUDE.md array laws: a self-append that grows is a `push`/`grow_inplace`-class
   copy-grow — it must be replaced by the known-size idiom (pre-alloc the exact total, write by index),
   OR the header/append order corrected so the grown backing is committed before the header retargets.
   Root-cause the aliasing; do NOT band-aid with a larger arena.
3. **Prove the repro is green.** `probes/dps_pin_push_inst_block` must go from native crash to exit `0`.
4. **Rebuild the native ladder + fixpoint.** `gen2 == gen3` holds; the fix is a deterministic
   correctness change at one site.
5. **Land BEFORE R1.** This crumb's green native chain is a precondition of the reseed — the seed must
   not carry a crashing self-append path.

No NEW public Teko surface — this is a targeted fix at an existing emitter.

## Rulings & laws

- **Teko-only:** `src/lir`/`src/codegen` edit; NO `teko_rt.c` patch — the slice-grow machinery is dead
  code to REMOVE, not to patch in C (CLAUDE.md "NADA em `teko_rt.c` PRO EXPURGO"). If a self-append must
  be replaced, it becomes the known-size Teko idiom.
- **Array laws (CLAUDE.md):** self-append/`grow_inplace` is `push`-class and PROHIBITED; the fix uses
  known-size pre-alloc + index-write, never a growth primitive. No `ulimit` raise — root-cause the aliasing.
- **W15:** any touched declaration keeps full Javadoc.
- **Safety:** NEVER `teko test .`; native ladder + fixpoint in a subshell with `ulimit -v 6815744`;
  commit the green step; SEED at SM-R1, not here (`[RITUAL]*`, `(folds R1)`).
- **§14 R1:** A5 MUST be green pre-R1 (so the reseed does not lock in a crashing native chain).

## Fixtures

The self-build emits inst blocks throughout, so the fixpoint exercises the fixed path; the crash-repro
oracle is the SM-P1 probe promoted to a kept oracle:

| fixture | asserts | expected |
|---|---|---|
| `probes/dps_pin_push_inst_block` | the self-append inst block that crashed native now runs | 0 |

## Gate

`[RITUAL]*` — full native ladder + `gen2==gen3` + the `push_inst_block` native crash is exit `0`.
"Green" = the third native blocker is closed, the fixpoint holds byte-identical, and the fix is a
known-size idiom (no growth primitive). Reseed-class: `(folds R1)`. **Must be green before R1.**

## Deps

`SM-P1` (pins `push_inst_block` to the self-append facet).

## Done when

The `push_inst_block` self-append crash is fixed (known-size idiom, no `grow_inplace`), its native repro
is exit `0`, the native ladder is green with `gen2==gen3`, and it is proven pre-R1 so the reseed carries
no crashing self-append path.
