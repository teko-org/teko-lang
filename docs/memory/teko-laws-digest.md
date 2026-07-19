---
section: language-law
created: 2026-07-13
source: TEKO_CONSTITUTION.md, TEKO_LEGISLATION.md, TEKO_MASTER_PLAN.md (wave constraints)
---

# Teko Laws Digest

**Teko-only (ruling 2026-07-04):** compiler source canonical in `.tks`; frozen C bootstrap (`0.0.1.3-bootstrap`) archived; C runtime (`teko_rt.c`) maintained.

**Twins retired (2026-07-13, #524):** VM interpreter, REPL, C bootstrap all retired; native AOT sole engine.

**W15-from-now:** code quality (doc-comments only, no inline; flatten, extract; cyclomatic <N) applied continuously, not deferred.

**Issues-100% + NO-DEFERRAL (reforçado 2026-07-16):** every open item ships complete; no deferrers ("future wave"); tensions → law-first ruling + tribunal. **Toda falha achada (mesmo antiga, mesmo que "não bloqueie") é resolvida AGORA, in-wave — "não bloqueia" não é desculpa (é falha de design/desleixo). Se o fix precisa de peça planejada para o futuro (0.4/pós-1.0), a peça é ADIANTADA agora.** "follow-up / não bloqueia / workaround-em-vez-de-fix / completar-depois" para uma falha real = PROIBIDO. Recorte de roadmap (feature futura não-começada que nenhuma falha exige) continua ok — até uma falha exigi-la.

**Memory-is-rules-only (governança 2026-07-16):** memória = SÓ conjuntos de regras/design-rulings/convenções; definição de agente = regras + como-agir; skill = superpoderes (+regras). Um ACHADO que precisa ser resolvido (bug/gap/limitação) NÃO vive em memória → valida se já foi feito → senão vira ISSUE (`bug`) rastreada. Ao migrar p/ issue, REMOVE a nota da memória/skill/agente (o detalhe técnico vai no corpo da issue).

**Resolve-in-same-task / don't-ask (2026-07-13):** an error found now is fixed now; a future-planned piece the task needs is pulled forward. This is LAW-decided, not an owner call — never ask the owner to choose fix-now-vs-defer or whether a disproportionate rework is "in scope" (asking is itself the violation). Owner-decision tensions = product taste or law-vs-law ONLY.

**100%-coverage-on-delta:** new/altered code covers all branches + lines; arm inalcanzaable only if listed with reason.

**Main-integrity/never-merge-on-snapshot:** all checks `completed + success` before merge; `gh pr merge`, not direct push.

**DRY-last:** the whole-codebase DRY refactor is final phase; every other item lands first.

**Metaprogramming-out-of-LTS:** comptime/macros deferred to post-`1.0.0.0`; traits (structural derive) stay.

**STS-before-LTS (2026-07-13):** sequential-task-structure ruling stabilizes before LTS lockdown; waves solve independently.
