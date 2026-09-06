---
seq: 0155
crumb-id: MEM-S1
milestone: M5
gate: "[dry]"
reseed-class: "none"
deps: [MEM-E5]
sources:
  - "docs/design/modelo-de-memoria-por-escopo-0.3.1.md:544-573"       # §11 the mem_* fixtures
  - "docs/design/plano-fiacao-modelo-memoria-por-escopo-0.3.1.md:§6"  # the shadow fixture list
  - "DECISION_LOG.md:1152-1165"                                       # D130 (reclaim 0%→scoped is the proof) + D117 shadow
---

# 0155 · MEM-S1 — SHADOW validation in the scratchpad (the `mem_*` proofs, NOT versioned)

> Before the tree-wide sweep, prove EACH dangerous flip in isolation. Write the `mem_*` programs as
> STANDALONE scratchpad artifacts (D117 — NOT versioned fixtures), build gen2, run them with the flip's
> consumption toggled on LOCALLY, and correct the flip design against the observed behavior. This
> de-risks the single RESEED-FINAL: the sweep flips land already proven. NO reseed (scratchpad only).

## Goal

The owner's methodology: teach → RESEED-1 → **SHADOW + corrections** → sweep → RESEED-FINAL. This crumb
is the SHADOW. Each `mem_*` program (design-doc §6) is the minimal proof of one flip; a program is
corrupted-not-just-leaked when its stdout fingerprint is WRONG (churn N cycles that must net to a known
value). The regressor of UAF is the self-host under `TEKO_MEM_PARANOID`/ASan, not a versioned `.tkr`.
Corrections found here feed the exact `MEM-W*` they belong to.

## Where

- Scratchpad only: `/tmp/claude-*/scratchpad/mem_*` — standalone `.tks` programs + a runner. NOTHING
  versioned (D117; the mass-test law forbids affirmative `.tkr`/`.tkt` for self-exercised paths).

## How

Build gen2; for each flip, toggle its consumption locally and run the matching program:

| shadow program | proves (flip) |
|---|---|
| `mem_block_dies` | a `{}`-local dies at the block edge (scope residence, W3) |
| `mem_scope_kinds` | the 5 scopes treated identically (W3) |
| `mem_loop_per_iter` | the fixed child-array REUSES the slot; peak FLAT over 1M iters (W3, refinement 5) |
| `mem_move_return` | a callee-built value MOVES into the caller's region param (W4) |
| `mem_move_transitive` | N-frame return bubbles to the top consumer (W4) |
| `mem_str_scope` | a scoped `str` dies with its `{}` (W3) |
| `mem_no_root_leak` | scoped>0, unresolved=0 (W4/W6) |
| `mem_service_root` | a `service singleton` binding survives its declaring `{}` (W3/W4) |
| `mem_elide_leaf` | a `slots==0` fn opens NO arena; parent region passes through (W1) |
| `mem_ptr_bytes_zerocopy` | a `str`↔`[]byte` reinterpret shares the ptr (E0b flagship, no copy) |

For each: baseline with the current compiler, then evolve the flip until the fingerprint is exact and
`TEKO_MEM_PARANOID` is clean. Record corrections in the target `MEM-W*` crumb.

## Rulings & laws

- **D117 — shadow in the scratchpad, NOT versioned.** The mass-test law: no affirmative `.tkr`/`.tkt`
  for what the self-build exercises; the UAF regressor is the self-host under ASan.
- **Corruption = wrong fingerprint, not just leak** — the programs churn and must net to a known value.
- **No reseed:** scratchpad only. **Safety:** never `teko test .`; subshell `ulimit -v 4718592`.

## Fixtures

The `mem_*` programs ARE the fixtures, but they are SCRATCHPAD (non-versioned). Zero versioned artifact.

## Gate

`[dry]` — the scratchpad programs build and run; each flip's fingerprint is exact under
`TEKO_MEM_PARANOID`. No reseed. "Green" = every flip is proven in isolation and its corrections are
recorded in the target `MEM-W*`. Reseed-class: `none`.

## Deps

`MEM-E5` (RESEED-1 done — the teaching cluster is in the seed so the flips can be toggled on).

## Done when

Every `mem_*` flip is proven in the scratchpad under `TEKO_MEM_PARANOID`, corrections are folded into the
matching `MEM-W*`, and nothing is versioned.
