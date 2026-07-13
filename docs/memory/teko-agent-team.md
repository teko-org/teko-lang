---
section: operations
created: 2026-07-13
source: TEKO_MASTER_PLAN.md, DECISION_LOG.md (wave structure, D34 rulesets), .claude/skills/dispatch/SKILL.md
---

# Teko Agent Team Operating Model

**Roles:** scout (recon), architect (design), implementer (code), verifier (gate), reviewer (QA), docs (memory).

**Workflow:** agents draft on `fix/issue-*`, `docs/*`, `recon/*`, `remodel/*` branches; integrator merges via umbrella PR + sub-PR flow within a wave. Each wave (`0.X`) gets W15 sweep + doc-sync pre-launch.

**The FOUR Laws (2026-07-13):** (1) CI-only long tests; (2) sync-before-dispatch + seed-before-crumb; (3) core-is-serial (one fixpoint at a time); (4) resolve-in-same-task (no deferred drift).

**Counter-argue protocol:** design tensions resolved law-first via TEKO_CONSTITUTION.md, TEKO_LEGISLATION.md; tribunal (owner arbitrates) on law conflict.
