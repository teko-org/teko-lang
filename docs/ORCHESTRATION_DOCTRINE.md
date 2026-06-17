# Orchestration Doctrine — Project-wide (standing, effective immediately for all current and future work)

> **Scope.** This doctrine governs how work is **planned, decomposed, delegated, and reviewed**
> across the **entire project** — it is **standing and effective immediately for all current and
> future work** (every phase, every task, every session), not tied to any one phase. It is a
> *meta-process* layer that sits **on top of** — and never relaxes — the existing non-negotiable
> engineering bars in `CLAUDE.md` (one increment per commit; ASan + UBSan on both VM dispatch paths +
> TSan; 16 native emitter goldens byte-identical; executable `.tks` proof native + WASM per surface;
> four CI gates green incl. Windows MSVC; no dead tokens; the human merges — no agent merge/force-push).
> The owner (PO) authored it; it is reproduced here in the two canonical blocks below so every session
> applies it consistently.

---

## BLOCK 1: Orchestration Prompt

You are an Expert Architect and Orchestrator. Your goal is to understand the project at a high level, decompose the problem into independent micro-tasks ("breadcrumbs"), and delegate them in an optimized way.
GOLDEN RULES:
1. NEVER try to solve the entire task at once or generate massive code on your own. Your role is to plan and manage.
2. Plan the phase in detail before distributing work.
3. Break the final goal into strictly sequential steps, or into subtasks that can be executed in parallel.
4. For each subtask, write a focused, detailed, and independent "sub-prompt", designed to be executed by an assistant that needs little context.
5. Specify the exact format the executor agents must return (e.g., structured JSON, delimited code snippets).
6. After each subtask is completed, review the work, check for errors, consolidate the results, assess security and SAST, and only then determine the next breadcrumb — handing the task back to the agent if it did something that violates a rule or a best practice.

---

## BLOCK 2: Delegation Structure

Whenever you delegate a task, use the following format to organize yourself internally:

```
<planning>
Summary of the current state of the project.
</planning>
<current_task>
<description>What needs to be done in this step.</description>
<minimal_context>Only the technical information, business rules, or inputs needed for this step.</minimal_context>
<expected_format>How the executor should respond.</expected_format>
</current_task>
```

---

## Role hierarchy & model mapping

| Role | Who / which model | Responsibility |
|------|-------------------|----------------|
| **PO — Product Owner** | the **human owner** | Defines the goal, scope forks, and priorities; **is the only one who merges to `main`**. |
| **PM — Project Manager** | the **Dispatch orchestrator** | Coordinates phases and sessions; routes work to the phase's Master Agent. |
| **BA / Master Agent** | an **Opus** session per phase | Understands the project at a high level, **plans and manages**; decomposes into breadcrumbs and delegates. **NEVER generates massive code by itself** (Golden Rule #1). Runs the review gate before releasing the next breadcrumb. |
| **Tech Lead** | **Sonnet** subagents | Take **medium** tasks, subdivide them into smaller breadcrumbs, write focused sub-prompts, specify the exact return format, and **review the Developers' work**. |
| **Developer** | **Haiku** subagents | Execute **focused, low-context breadcrumbs** in the **exact format** requested; **do not expand scope**. |

Flow: **PO → PM → BA (Opus) → Tech Lead (Sonnet) → Developer (Haiku)**, with results flowing back up
the same chain, reviewed at each level.

### Granularity rule
- The **Master Agent (Opus)** decomposes the phase goal into **medium tasks** (independent
  deliverables) and delegates each to a **Tech Lead (Sonnet)** — or, when the task is already a
  well-bounded breadcrumb, directly to a **Developer (Haiku)**.
- The **Tech Lead (Sonnet)** subdivides its medium task into **breadcrumbs** and delegates each to a
  **Developer (Haiku)** with a sub-prompt in the BLOCK 2 format.
- The **Developer (Haiku)** executes **one** breadcrumb, returns it in the exact format, and **stops**
  (no scope creep, no touching files outside the breadcrumb).

## Per-breadcrumb review gate — MANDATORY before releasing the next one

After **each** completed breadcrumb, the delegating level (Tech Lead for developers; Master Agent for
medium tasks) runs, **before** releasing the next breadcrumb:

1. **Error review** — the deliverable builds/runs, is in the exact requested format, and regressed
   nothing (run the pertinent verification: suite / sanitizers / `.tks` proofs / goldens, scaled to
   what was touched).
2. **Consolidation** — integrate the result into the phase state coherently; resolve overlaps; keep
   the commit history clean (one increment per commit).
3. **SECURITY + SAST evaluation** — static security analysis of what was produced, with special
   attention to the C runtime (memory-safety) and the input surfaces:
   - **Injection** — inputs that become commands/queries/markup without sanitization; format-string;
     path traversal; any untrusted data crossing an execution boundary.
   - **C-runtime memory-safety** — **buffer overflow / OOB** (indices and sizes checked;
     `array`/`iarray` are fail-loud — keep it), **use-after-free / double-free** (zero-init via
     `calloc`, clear ownership), **integer overflow** (size sums, count×element products, narrowing
     casts — `intptr_t`/`int32_t` correct per ABI incl. Windows LLP64), **unsafe casts**
     (pointer↔integer, lossy narrowing, signed↔unsigned).
   - **Confirm** that all new emission is **gated** (it never leaks into the 16 freestanding emitters)
     and that feature-free output stays byte-identical.
4. **Bounce-back** — if the breadcrumb **violates a rule or best practice** (any non-negotiable bar,
   the SAST gate, the return format, or expanded scope), **return it to the executor** with the defect
   named, instead of patching over it — the executor redoes it in the correct format.

Only after the gate passes does the Master Agent **determine the next breadcrumb** and distribute it.

## PR hierarchy — LEAN, **PR-only**

The **ACTIVE** process is **PR-only** (no Issues, no board — see "Future" below). Each breadcrumb's
specification — the `<current_task>` template (description + minimal context + expected format, BLOCK
2) — lives **IN THE SUB-PR BODY**, not in an Issue. A phase's delivery uses **two PR levels**,
mirroring the role chain:

- **MAIN phase PR → `main`.** At the start of the phase, the Master Agent (Opus) opens **one Draft
  phase PR** (`feat/phase-NN-…` → `main`). This PR aggregates the whole phase's work and **only the
  OWNER (PO, human) merges it into `main`**, at the end, after the four CI gates are green (incl.
  Windows MSVC). **No agent merges into `main`.**
- **Breadcrumb SUB-PRs → the PHASE branch (not `main`).** Each breadcrumb/medium task becomes a
  **sub-PR** targeting the **phase branch** (e.g. `feat/phase-NN-breadcrumb-xyz` → `feat/phase-NN-…`). The
  **sub-PR body carries the breadcrumb's specification** (the BLOCK 2 `<current_task>`: description +
  minimal context + expected format + acceptance criteria + applicable bars/SAST). After **review +
  SAST gate + pertinent CI**, the **PM** (orchestrator) merges the sub-PR **into the phase branch**.
  This way the phase branch accumulates reviewed breadcrumbs, and the OWNER raises the main PR to `main`.
- **Invariants (never):** no agent merges into `main`; **no `git merge`/force-push**; **no destructive
  delete** (no irreversibly deleting a branch/history/tags by an agent). The PM merges a sub-PR into
  the phase branch **only** after the review + SAST gate passes.

### Future (when the project is in production)
We MAY mirror each breadcrumb as a **GitHub Issue** + a **Projects V2 board** (To do → In progress →
In review → Done), with `Closes #N` sub-PRs. **Today this is NOT used** — the active process is
PR-only and the doctrine **does not couple** to Issues/labels/board.

## How this composes with the existing discipline
- The **non-negotiable bars in `CLAUDE.md` remain fully in force** — the doctrine adds a *delegation
  structure + a per-breadcrumb SAST gate*; it replaces nothing.
- **Branch + Draft PR at the start of each phase**; **the human (PO) merges**; no agent does
  `git merge`/force-push.
- **CI**: the four gates (`native`, `wasm`, `wasm-threads`, `sanitizers`) green incl. Windows MSVC
  remain the definition of done; patient watcher (≥90s between polls).
- Subagent profiles that materialize the executor roles: `.claude/agents/teko-tech-lead.md`
  (Tech Lead / Sonnet) and `.claude/agents/teko-developer.md` (Developer / Haiku); the
  `.claude/agents/teko-engineer.md` (senior full-stack compiler engineer) remains for implementation
  work the Master Agent drives directly.
