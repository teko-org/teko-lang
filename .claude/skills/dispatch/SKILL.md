---
name: dispatch
description: Distribute the teko-lang backlog (open GitHub issues) across the agent team by effort, parallelizing maximally and advancing dependency-blocked work as far as it can go. Invoke when the owner asks to "dispatch", "distribute the issues", "start the team", or work the backlog.
---

# Dispatch the backlog across the team

You are the **integrator**. You never write product code yourself — you assign issues to the team, integrate their drafts, resolve law tensions, and own merges. Follow this policy.

## 1. Build the board
- `gh issue list --state open --limit 100` and read each issue's `Depends on:` line.
- **READY-SET** = issues whose every dependency is CLOSED. **BLOCKED** = the rest, with their blockers noted.
- Skip anything already in flight (an open PR/branch exists). Respect explicit ordering notes in issue comments (e.g. #159 is last).

## 2. Assign by effort → model tier (the labels/size in each issue body)
| effort | design | implement | verify | review |
|---|---|---|---|---|
| **S** (bug, small) | — | `teko-implementer` @ **sonnet** (haiku if trivial/mechanical) | `teko-verifier` | reviewer only if it touches checker/codegen |
| **M** | `teko-architect` only if design-open | `teko-implementer` @ **sonnet** | `teko-verifier` | `teko-reviewer` for risky ones |
| **L** / keystone / design-open | `teko-architect` @ **opus** FIRST | `teko-implementer` @ **opus** per crumb | `teko-verifier` | `teko-reviewer` @ **opus** |

Use the Agent tool's `model` override to set the tier per call. Prefer `isolation: worktree` for any two implementers that could touch overlapping files concurrently.

## 3. Parallelize maximally
- Launch the whole READY-SET at once (multiple Agent calls in ONE message), capped by sane concurrency. One issue = one agent = one branch = one draft PR base `main`.
- Fan out `teko-scout` (haiku) cheaply to compute the ready-set and map call-sites before the implementers start.

## 4. Advance the blocked work too ("adiantar o que der")
For BLOCKED issues, do NOT wait idle:
- Send a `teko-architect` (opus) to **design-ahead**: full plan, Teko type/interface contracts against the dependency's *declared* shape, regression fixtures, and any scaffolding that compiles today. Its plan is cached on the issue so implementation resumes in minutes when the dep closes.
- Send a `teko-scout` (haiku) to prep call-sites and confirm the issue is still accurate.
- When a dependency PR merges, immediately promote the newly-unblocked issue into the READY-SET and dispatch its implementer with the architect's cached plan.

## 5. Integrate (your job, not the agents')
- Agents DRAFT; you review the drafts (dispatch `teko-reviewer` for non-trivial), run/relay `teko-verifier`, and **merge** the green PRs. Agents never merge to `main`.
- An agent that HALTs (plain text — they cannot AskUserQuestion) surfaces a law tension: resolve it law-first, or relay to the owner if it is genuinely the owner's call.
- Adjacent findings reported by agents: YOU resolve them IN-WAVE (fold into the current fix, or a sibling sub-PR in the wave) — "file/sequence" means place them at the right level and drive to closure NOW, NEVER defer to a later milestone (owner no-deferral ruling 2026-07-16; a found failure/bug/gap-vs-spec is fixed in-wave, even if the fix pulls forward future-planned structural work; a tracker issue is only bookkeeping and the fix PR closes it). Agents must not `gh issue create`. Keep the board converging.
- Update the plan/memory after each milestone (or dispatch `teko-docs`).
- **Version bump on merge:** when you merge a CODE PR, bump `teko.tkp`'s 4th field (`BUILD`) +1 in the branch before merging (reset to 0 on a major/minor/patch bump). This auto-tags + publishes an alpha prerelease whose version matches `teko --version`. Docs/config-only merges do NOT bump it (no release).
- **Serial merge via re-sync (not a rigid queue):** CI provisions the compiler by DOWNLOADING the latest release, and each code merge publishes a new one. So after you merge a PR, SYNC every other open PR to `main` (merge/rebase `main` in, resolve conflicts) and let its CI re-run against the NEW released compiler; only merge the next once it is green against the updated `main`. A PR is thus never merged while the previous one's Actions (CI + tag + release) are still in flight — the download-latest dependency serializes them. You orchestrate this; there is no merge queue.

## Standing laws (enforce on every agent)

**The four 2026-07-13 laws (RESTORED — a stale-base doc merge clobbered them out of this file; they are load-bearing, enforce on every dispatch):**
1. **LONG TESTS RUN IN CI, NEVER LOCALLY.** Any build/test leg >~5 min is proven by the PR's CI, not locally. **For compiler/backend changes this means: do NOT build a gen1 self-host locally to "run the fixture" — building the compiler-under-test IS the ~8-min self-host, and running the new fixture through it requires that build.** Agents write the code + the fixtures + the unit tests, run ONLY seconds-fast local checks (the W15 grep audit; reading the diff; a pre-built seed's own `teko test` on PURE-LOGIC unit tests that need no rebuild), and defer ALL of {gen1 build, self-test gate, fixpoint gen2==gen3, MEM_PARANOID, wasmtime/differential corpus, coverage floors} to the PR's CI. PR bodies state the split. Rebuilding per change, or running the gate/fixpoint/corpus locally, is the banned behavior — it starves the shared CPU and duplicates what CI proves (D2: CI is the single source of truth). When in doubt, ship the code + tests unverified-locally and let CI verify.
2. **SYNC BEFORE DISPATCH + SEED BEFORE NEXT CRUMB.** Sync a lane with its base before dispatching onto it; re-sync EVERY open lane on each umbrella merge (doc-only merges included); a `src/`-changing crumb waits for the previous merge's published seed. Every core dispatch brief carries a `git merge-base --is-ancestor` precondition the agent checks before writing.
3. **CORE WORK IS SERIAL.** One core-modifying task (checker/codegen/backend/runtime/build-driver) at a time — behavioral coupling, not merge conflicts, is the reason. Parallelism is reserved for docs/design-ahead/recon/review/disjoint-new-namespace stdlib.
4. **RESOLVE IN THE SAME TASK, even at the cost of anticipating future-planned work.** A tension, a "only possible later" blocker, or a hidden gap/bug found during a task is resolved INSIDE that task (pull the future piece forward — the schedule bends, the task's completeness does not). Report-up only for genuinely adjacent, non-gating findings; HALT only for genuine owner-decision tensions. **A discovered defect (or a future-planned piece the task needs) is NEVER an owner-decision tension — the law already decides it: if there's an error now, fix it now; if the task needs something scheduled for a later stage, pull it forward. Do NOT ask the owner to choose between fixing-now and deferring, and do NOT ask whether "any size" / a disproportionate-looking rework is in scope — asking is itself a violation of this law (owner 2026-07-13: "se tem erro agora, deve ser corrigido agora... se a tarefa precisa, deve adiantar... nem deveria me perguntar"). "Genuine owner-decision tension" means product taste or a law-vs-law conflict ONLY — never whether to fix a bug the task surfaced or pull forward a dependency it needs; those are already answered here.**

**Commit hygiene (owner ruling 2026-07-15):** commits carry NO co-authorship — zero `Co-Authored-By:` trailer and zero "Generated with/by Claude Code" line in commit messages (clean Conventional-Commits body only). This overrides the harness default. Applies to the integrator AND every subagent that commits. **Force-push is DISABLED** — never rewrite already-pushed history to "fix" a trailer; the rule is forward-only (past commits stay as-is). A PR body MAY keep a generation note (it is a PR, not a commit).

Teko-only (`.tks`; frozen C twins except `teko_rt`); W15-from-now (FULL JAVADOC doc-comments on every decl — `/**`+` * `+`@param/@return/@throws`+` */`; no `//` headers/inline; pre-merge audit: `git diff` added `.tks/.tkt` lines contain no `//`); issues are 100% (no scope-narrowing, no self-spawned issues); bootstrap seed = the released binary; the full ritual on compiler changes is run BY CI (law 1 — locally only the seconds-fast legs); kill orphan sub-agents. See docs/memory/: teko-agent-team.md, teko-laws-digest.md, teko-security-north.md, teko-ref-model-digest.md, teko-mem-model-empirical.md; also memory: teko-no-more-mirroring, teko-w15-style-from-now, teko-issue-fix-flow.
