---
seq: 0185
crumb-id: FP-0
milestone: M5
gate: "[dry]"
reseed-class: "none"
deps: [MEM-ARENA-MIGRATE]
sources:
  - "docs/design/fold-and-prune-por-passo-0.3.1.md:§1"
  - "docs/design/projecao-ast-entre-fases-0.3.1.md:572"        # PJ-1 baseline
  - "docs/design/ast-computed-arena-assessment-0.3.1.md:87"    # reclaim 0,0 %
  - "src/build/project.tks:224"                                # report_phase_rss
---

# 0185 · FP-0 — instrumentar a retenção por-fase + a fatia de posições (baseline)

> Mede ANTES de podar: por fase, ÁRVORE viva vs scaffolding reclamável, e a fatia residente de
> `line`/`col`. Sem mudança de comportamento — só números para dirigir FP-1..FP-6.

## Goal

Estabelecer o baseline que confirma "onde está o peso" (doc §1) e ordena qual poda tira mais (doc §1
tabela). Estende `report_phase_rss` (já chamado após checker/monomorph/consteval, `project.tks:224/236/248`)
para separar, por fase, o working-set reclamável (scaffolding do checker + árvore da fase anterior) da
árvore que sobrevive, e para medir a fatia de `line`/`col` residente (o alvo do `.tsym`). Byte-preservante
(só instrumentação sob env-guard, não muda o `teko.c`).

## Where

- `src/build/project.tks:224,236,248` — `report_phase_rss("checker"/"monomorph"/"consteval")` — estender
  para reportar sub-linhas (árvore-viva vs scaffolding) sob a MESMA env-guard `TEKO_PHASE_RSS` já
  existente. NÃO alterar o caminho quente sem a env.
- `src/build/project.tks` (novo helper privado) — `phase_tree_bytes(prog)`: soma bounded dos nós vivos
  de um `TProgram` (walk determinístico, sem `push`), reportada só quando a env está ligada.

## How

1. Sob `TEKO_PHASE_RSS`, após cada `report_phase_rss`, emitir uma sub-linha
   `teko: phase <name>: tree <MB> · scaffold <MB> · pos <MB>` onde `tree` = `phase_tree_bytes(prog)`,
   `scaffold` = RSS-atual − tree, `pos` = contagem-de-nós-com-posição × 8 B.
2. `phase_tree_bytes` é um walk read-only em ordem fixa de campo (determinístico R6), retornando `u64`;
   NÃO aloca array dinâmico (acumula em escalar).
3. Nenhuma mudança fora da env-guard → build seco default byte-idêntico.

```teko
/**
 * phase_tree_bytes — soma bounded dos bytes dos nós vivos de um TProgram, para a contabilidade de
 * retenção por-fase (só sob TEKO_PHASE_RSS). Walk read-only, ordem fixa de campo, sem alocar.
 *
 * @param prog  o programa tipado da fase corrente
 * @return      o total estimado de bytes da árvore viva desta fase
 * @since 0.3.1
 */
fn phase_tree_bytes(prog: checker::TProgram): u64
```

## Rulings & laws

- **Teko-only (.tks); zero C (D148).** **Byte-preservante:** só instrumentação sob env-guard.
- **Fork protocol (2026-08-19):** nenhum fork — é medição pura.
- **W15 full Javadoc; no `//`.** **Safety:** never `teko test .`; subshell `ulimit -v 4718592`;
  `[dry]` = compila + fixpoint trivial (nenhuma mudança de byte emitido).
- **Ratchet D68:** não aplicável (não muda o pico) — este crumb MEDE o pico.

## Fixtures

none — the fixpoint self-build exercises this (a instrumentação roda no próprio self-build).

## Gate

`[dry]` — build compila, `teko.c` byte-idêntico ao anterior (instrumentação sob env não altera emissão),
baseline registrado no retorno. Reseed-class: `none`.

## Deps

`MEM-ARENA-MIGRATE` (0184 — a superfície de método estabilizada).

## Done when

O build seco sob `TEKO_PHASE_RSS` reporta, por fase, `tree/scaffold/pos` em MB, e o baseline está
registrado; o `teko.c` default segue byte-idêntico.
