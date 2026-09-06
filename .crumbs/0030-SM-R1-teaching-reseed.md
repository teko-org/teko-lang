---
seq: 0030
crumb-id: SM-R1
milestone: M1
gate: "[RITUAL]"
reseed-class: "teaching (1, the only forward one)"
deps: [SM-A1, SM-A2, SM-A3, SM-A4, SM-A5, SM-G1, SM-G2, SM-G3, SM-G4, SM-G5, SM-G6, SM-G7, SM-G8, SM-G9, SM-G10, SM-G11, SM-G12, 9D-T1, RM-C2, COL-F0a, COL-F0b, COL-F0c, COL-F0d, SM-STRU32, IO-2, S16-MM-wp, S16-MM-const, S16-SYNC-const]
sources:
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1196-1199"   # §10 Phase R — the reseed
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1121-1142"   # §9.2 why exactly ONE reseed
  - "docs/design/plano-mestre-0.3.1-implementacao.md:313-333"            # M1 fits ONE teaching reseed
---

# 0030 · SM-R1 — THE ONE forward teaching reseed

> THE ONE forward teaching reseed — capture a seed that DPS-lowers + parses/knows ALL of M1.

## Goal

The HINGE of the whole 0.3.1 wave: harvest a single fresh bootstrap seed (`bootstrap/teko.c`) from the
current source that (1) DPS-lowers its own emitted C (the self-lowering memory-model change SM-A2..A5) and
(2) PARSES and KNOWS every additive M1 surface — the `:`/`var`/`self`/`base`/`static` grammar, the Marshall
opaque `ptr`, DI `service`/`svc`, `size`/`usize`, method + operator + constraint acceptance, inline-union,
`mem::copy`, the collection FASE-0 intrinsics (`of_len`/`place`/`read`/`write`/`retain`/`release`/weak/
`deep_copy`/chunk-node), string-u32 codecs, `byte_ptr`, `word_ptr`+load/store, and the mmap/sync consts.
This is the ONLY forward TEACHING reseed of the campaign: every M1 row above is additive and written in OLD
spelling so the CURRENT seed parses it, they all converge HERE, and the harvested seed becomes the substrate
the M2 source sweeps (SM-S1/S2/S3/S6) and the fixpoint-rebuild swaps stand on. After this, no change needs a
second seed swap — every downstream reseed is a byte-identity fixpoint-rebuild or an M3 expurgo. It is a
`[RITUAL]`: full native ladder + a genuine reseed via `reseed-bootstrap.yml`, drained by cherry-pick (no
PR/merge).

## Where

- `.github/workflows/reseed-bootstrap.yml` — the governed re-harvest (manual `workflow_dispatch`, dispatch
  by ref on the lane): harvests gen1's own emitted C from the current source, self-verifies the C-route
  fixpoint, records provenance against its own `run_id`, leaves a `reseed/teko-c-<run>` branch + artifact.
- `bootstrap/teko.c` — the degrau seed the reseed SWAPS (harvested output, never hand-written).
- `bootstrap/PROVENANCE` — the run-id-backed record `scripts/provenance_gate.sh` validates on the drain.
- `scripts/build_with_seed_fallback.sh` / `scripts/provenance_gate.sh` — the harvest + gate scripts the
  workflow runs.
- ALL M1 `src/` surfaces (`0002`–`0029`) — the accumulated additive+DPS state the harvest captures; SM-R1
  authors NO new `src/` — it is the operational harvest of what the M1 cluster already landed.

NEW: no product `.tks`; this crumb is the reseed operation itself.

## How

1. **Confirm the M1 cluster is fully landed and green.** Every dep (`0002`–`0029`) is committed on the lane,
   each `[dry]`/`[RITUAL]*` gate passed. The DPS memory model (SM-A2..A5) lowers the compiler's own C; the
   additive grammar (SM-G1..G12, 9D-T1) is accepted by the current seed (written in OLD spelling); the
   collection/IO/§16 surfaces are registered and inert. The set is dependency-closed (master-plan §2.313):
   the current seed parses every row, none uses another row's not-yet-seeded surface.

2. **MANDATORY PRE-FLIGHT RE-EVALUATION (reavaliação no momento do crumb).** At the MOMENT this crumb
   executes, audit the actual state of each budgeted teaching item. If ANY surface is already landed
   (S1/S2/S3 sweeps, intrinsic definitions, semantic layers), mark it DONE and teach ONLY the residual.
   If teaching is still genuinely needed, it MUST be done in this round ("se precisa ensinar, tem que ser
   feito"); do not defer to a later reseed.
3. **Dispatch `reseed-bootstrap.yml` on the lane ref.** It harvests gen1's own emitted C, cc-compiles it to
   a compiler, rebuilds the tip, and checks the tip's re-emitted `teko.c` is byte-identical to the harvested
   C (a C-route FIXPOINT of the seed). A seed that does not self-reproduce is REFUSED, red, never proposed.
4. **The green criterion is the C-route fixpoint** (the harvested C reproduces itself down the C route). DPS
   is byte-preserving for the C route, so the harvest passes; the DEEPER native `gen2==gen3` is proven by
   the drained lane's own full matrix — which is correct, because the reseed EXISTS to unblock that native
   fixpoint (§9.2).
5. **Drain by CHERRY-PICK, no PR/merge.** Branch protection admits no bypass; the workflow leaves a
   `reseed/teko-c-<run>` branch (ONE commit: `bootstrap/teko.c` + `PROVENANCE`) + an artifact. The
   integrator collects that run-id-backed commit and cherry-picks it into the lane — the lane's own
   100%-green promotion is the gate. `provenance_gate.sh` PASSES because the record names a REAL run id
   matching the file sha.
6. **After the reseed, the seed knows all of M1.** The M2 source sweeps (SM-S1/S2/S3/S6, `0031`–`0034`) can
   now rewrite `src/` to the new spelling against a seed that ALREADY accepts it; the fixpoint-rebuild swaps
   (RM-C3+, IO-6+, S16-*) core-consume it. This is the single teaching harvest for the entire M1 surface —
   COL-F0a..d fold in here too (NOT a separate collection reseed; the whole program's surface teaches once).

## Rulings & laws

- **Teko-only:** no `src/` change here; the C twins stay FROZEN — the reseed harvests OUTPUT, it does not
  edit the bootstrap by hand.
- **Reseed ONLY at a `[RITUAL]` (CLAUDE.md):** this IS the ritual point; it is the one forward teaching
  reseed of the campaign (the ≤4 expurgo reseeds are M3; everything else is fixpoint-rebuild/none).
- **Governed harvest (provenance gate):** a seed swap requires a complete `PROVENANCE` naming a REAL CI run
  id (never the `unrecovered` sentinel), the measured commit, and a matching sha256; the workflow IS that
  run, so the record is honest by construction.
- **Drain = cherry-pick, no PR (branch protection):** the lane's own 100%-green promotion is the gate; never
  push to a protected branch, never merge.
- **Fixpoint `gen2==gen3` byte-identical:** proven by the drained lane's native matrix (not a precondition
  of the harvest, which unblocks it).
- **Safety:** NEVER `teko test .`; the native ladder builds in a subshell with `ulimit -v 6815744` — a blown
  guard is a root-cause fix, never a raised ceiling; sweep `.tkt`/`.tkr` after the AST/signature additions
  before the harvest.

## Fixtures

`none — the fixpoint self-build exercises this`. SM-R1 mints no new behavior; it captures a seed. Every M1
surface's own oracle already landed with its `[dry]` crumb (`0002`–`0029`), and the harvest's correctness is
the C-route self-reproduce + the drained lane's native `gen2==gen3` — not a new `.tkr`.

## Gate

`[RITUAL]` — the full native ladder (genB→gen2→gen3) + a genuine reseed. "Green" = the harvested seed
self-reproduces down the C route (C-route fixpoint), `PROVENANCE` names this run's id with a matching
sha256, and the drained lane proves native `gen2==gen3` byte-identity across the full matrix. Reseed-class:
**teaching (1, the only forward one)** — the single teaching harvest of the whole M1 surface.

## Deps

ALL M1 rows: `SM-A1`,`SM-A2`,`SM-A3`,`SM-A4`,`SM-A5`,`SM-G1`,`SM-G2`,`SM-G3`,`SM-G4`,`SM-G5`,`SM-G6`,
`SM-G7`,`SM-G8`,`SM-G9`,`SM-G10`,`SM-G11`,`SM-G12`,`9D-T1`,`RM-C2`,`COL-F0a`,`COL-F0b`,`COL-F0c`,`COL-F0d`,
`SM-STRU32`,`IO-2`,`S16-MM-wp`,`S16-MM-const`,`S16-SYNC-const` (the whole M1 additive + DPS cluster).

## Done when

`reseed-bootstrap.yml` has harvested a seed that DPS-lowers its own C AND parses/knows every M1 surface, the
seed self-reproduces down the C route with a run-id-backed `PROVENANCE`, the commit is drained by
cherry-pick, and the lane proves native `gen2==gen3` — so the M2 sweeps and fixpoint-rebuilds can stand on a
seed that already accepts the new grammar.
