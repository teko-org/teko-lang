# CI memory/UB gating — two tiers (light seed-gate on lanes, heavy audit on main)

**Status:** ratified (owner ruling 2026-07-14). Supersedes the prior arrangement where the
full ASan+UBSan(+LSan-by-name) default-dispatch lane ran on *every* lane PR.

## Problem

The heavy sanitizer lane (`asan-default` in `.github/workflows/sanitizers.yml`) rebuilds gen1
under `-fsanitize=address,undefined` through a `cc` wrapper and then runs the whole self-host
gate plus the regressions corpus under ASan shadow memory. That is a genuine, valuable UB audit —
it surfaced and drove root fixes for the #291 trait-vtable function-pointer mismatch, the
`tk_mul_u16` signed-overflow, and the systemic `__int128` arena under-alignment. But it costs
~24 min of wall-clock and heavy runner memory, and it ran on **every** lane PR into `remodel/**`.

Owner question that started this: *"Pq os gates de ASan e UBSan são tão lentos e pesados?"* —
followed by the key realization that the heavy lane's value is a **main/release-boundary audit**,
not a per-lane-PR gate, especially now that the frozen C mirror is retired (#524/#548) and there
is no second implementation to differentially catch these UBs.

## Decision

Split memory/UB gating into two tiers with **distinct, stable required-check names** so the
branch rulesets can require the right tier on the right branch.

### LIGHT gate — the SEED protector — runs on EVERY lane PR

Job `mem-paranoid` (check name **`Memory paranoid (native self-host)`**), aggregated by the
**`Sanitizer gate`** check.

- Builds gen1 **natively** (no clang, no sanitizer flags) — the seed compiles the PR sources.
- Runs the self-host gate and native-builds the regressions corpus with **`TEKO_MEM_PARANOID=1`**.
- `TEKO_MEM_PARANOID` is teko's **own** arena oracle (`src/runtime/teko_rt.c`, #148 Level-2): a
  runtime `getenv` that **poisons every freed arena block and never reuses it**, so any
  use-after-free / arena-reuse-after-free in the compiler's own C aborts loud.
- Cost ≈ one native self-host. This is what a lane PR must pass so the **seed** it feeds the next
  crumb is memory-clean. `tsan` (build + smoke) and `windows-selfhost` (one gate run) stay in this
  tier — already right-sized, unchanged.

### HEAVY gate — the MAIN audit — runs ONLY on merge to main

Job `asan-default` (check name **`ASan+UBSan+LSan / default dispatch`**), aggregated by the
**`Heavy sanitizer gate (main)`** check.

- Body unchanged (the proven ASan+UBSan native-path audit; LSan off-by-construction, as its long
  in-file comment explains).
- **Trigger changed** to main-only:
  `if: needs.changes.outputs.run == 'true' && (github.base_ref == 'main' || github.ref == 'refs/heads/main')`.
  It runs as a **required check on PRs into `main`** (the umbrella → main integration merge — the
  seed/release boundary) and **re-runs on push to `main`** as a post-merge audit.
- On lane/remodel PRs and remodel pushes it is **skipped** — a skipped dependency is neither
  `failure` nor `cancelled`, so the `Heavy sanitizer gate (main)` aggregator passes harmlessly
  there and blocks nothing.

**Single trigger = merge to main.** No nightly, no cron schedule, no auto-issue — the owner
explicitly rejected the daily-00h variant in favor of *"Único gatilho passa a ser merge na main"*.

## Why this is safe

- The seed a lane PR produces is still gated for memory correctness on **every** lane PR, by
  teko's own arena oracle. A use-after-free cannot reach the seed silently.
- The heavy ASan/UBSan audit of the emitted C + runtime seam still runs — at the exact boundary
  where main is about to become a release (umbrella → main), and again on the merge commit. Any
  UB is caught before it ships in a released seed.
- The two tiers have separate aggregator checks, so the rulesets express the intent directly and
  no required check ever "hangs" a branch by never reporting (the classic failure mode when a
  required job stops running on a branch).

## Required rulesets (apply manually — no ruleset API is exposed to CI)

Branch rulesets are **not** changed by this PR (GitHub does not expose a ruleset/branch-protection
mutation to the workflow token here). Apply these by hand after merge:

| Branch pattern        | Required checks to REQUIRE                     | Required checks to REMOVE                          |
|-----------------------|-----------------------------------------------|----------------------------------------------------|
| `remodel/**`, lanes   | `Sanitizer gate` (light)                       | any direct requirement on `ASan+UBSan+LSan / default dispatch`; and, if present, the old single required check should now point at `Sanitizer gate` (which is the light aggregator under the new design) |
| `main`                | `Heavy sanitizer gate (main)` **and** `Sanitizer gate` | — |

Notes:
- `Sanitizer gate` keeps its name but now aggregates the **light** tier — so a lane ruleset that
  already required `Sanitizer gate` stays green with no rename; it simply no longer waits on the
  heavy lane.
- On `main`, require **both** aggregators: `Heavy sanitizer gate (main)` enforces the ASan/UBSan
  audit, and `Sanitizer gate` keeps the light suite (mem-paranoid + tsan + windows) required there
  too.
- Do **not** require the raw job names (`ASan+UBSan+LSan / default dispatch`, `Memory paranoid …`)
  directly on lanes — require the aggregators, which are `if: always()` and therefore always report.
