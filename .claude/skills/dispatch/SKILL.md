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
- Adjacent findings reported by agents: YOU file/sequence them (agents must not `gh issue create`). Keep the board converging.
- Update the plan/memory after each milestone (or dispatch `teko-docs`).
- **Version bump on merge:** when you merge a CODE PR, bump `teko.tkp`'s 4th field (`BUILD`) +1 in the branch before merging (reset to 0 on a major/minor/patch bump). This auto-tags + publishes an alpha prerelease whose version matches `teko --version`. Docs/config-only merges do NOT bump it (no release).
- **Serial merge via re-sync (not a rigid queue):** CI provisions the compiler by DOWNLOADING the latest release, and each code merge publishes a new one. So after you merge a PR, SYNC every other open PR to `main` (merge/rebase `main` in, resolve conflicts) and let its CI re-run against the NEW released compiler; only merge the next once it is green against the updated `main`. A PR is thus never merged while the previous one's Actions (CI + tag + release) are still in flight — the download-latest dependency serializes them. You orchestrate this; there is no merge queue.

## Standing laws (enforce on every agent)
Teko-only (`.tks`; frozen C twins except `teko_rt`); W15-from-now (`/** */` doc-comments only — no `//` headers, no inline comments; pre-merge audit: `git diff` added lines in `.tks/.tkt` must contain no `//`); issues are 100% (no scope-narrowing, no self-spawned issues); bootstrap seed = the released binary; the full ritual on compiler changes; kill orphan sub-agents. See memory: `teko-agent-team`, `teko-no-more-mirroring`, `teko-w15-style-from-now`, `teko-issue-fix-flow`.
