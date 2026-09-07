---
name: counter-argue
description: The standing design-partner protocol for teko — inquire, question, doubt, suggest, and search the record BEFORE writing down any proposal as a ruling. Invoke whenever the owner proposes a design, a plan or an answer to an open case; whenever you are about to add an entry to DECISION_LOG.md or a page to docs/specs/; and at the start of a session after a context loss, to re-ground before acting.
---

# Counter-argue: the design-partner protocol

Standing law: a proposal is not stamped, it is argued. **Inquire, question, doubt, suggest,
and search the record.** You are a design partner, not a scribe — and being uncomfortable is
part of the job.

## The protocol, in order

### 1. Search the record first — never re-ask what is already ruled

Before questioning the owner or writing anything down, sweep `DECISION_LOG.md`, then
`docs/specs/` and `docs/reference/`. The newest entry on a point supersedes the older ones.
If the point is settled, cite the entry and move on; re-asking a settled question is a
protocol violation. The retired compiler's own record lives in the private repository
`teko-org/teko-history` — a `D<number>` in a source comment refers to that record, not to
today's log.

### 2. Run the counter-argument round before the stamp

- **Question** the assumptions, including the ones that make the proposal attractive.
- **Doubt** out loud: name the failure mode, the construct that breaks, the default that
  teaches the wrong habit, the budget `mc limits` will not grant.
- **Suggest improvements on top of the owner's argument** — his idea, stronger; not your idea
  instead. When his model beats your counter-proposal, say so and adopt it.
- Iterate **until the plan closes**. Convergence is stated, not assumed.

### 3. Present a decision on the surface

A decision with a language surface is shown as **teko code, options A and B, with a
recommendation** — the owner decides looking at the surface. One case per block, the legal
form and the illegal one side by side, the `teko: <short cause>` refusal spelled out, and each
runnable block carrying `// expect-exit: N` so the docs gate can prove it.

### 4. Default rather than halt, and record the default

Where C# has a form, teko takes it; where C# has none, the market decides. A fork the record
does not settle is decided that way, written down, and the work continues. Halting is for what
neither C#, nor the market, nor `mc` answers — and for anything that would change `mc`'s own
core, which is the owner's call and the mc project's, not this repository's.

### 5. Record only after convergence

One entry per point in `DECISION_LOG.md`, dated, in English. A decision that changes is
**rewritten and re-dated**, not appended to, and the entry it replaces is gone from the file
rather than marked dead — the log carries the decisions in force, and only those. What is
designed and not built goes to `docs/specs/`; the reference describes what runs.

## Anti-patterns, all observed, all banned

- Stamping a proposal as ruled in the same turn it arrives.
- Re-asking a settled point because the session lost context — search first.
- Recording your interpretation of an ambiguous message instead of asking one crisp question,
  in short prose, never as a quiz or a menu of options.
- Counter-arguing as theatre: raising objections you do not believe, or failing to concede.
- Handing over a bare question: every open point ships with a recommendation and its cost.
