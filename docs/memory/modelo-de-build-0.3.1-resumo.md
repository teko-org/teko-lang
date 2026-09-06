---
section: memory
created: 2026-08-02
plano: docs/design/modelo-de-build-0.3.1.md
branch: cargo/0.3.1.0-modelo-build-arq (de origin/fix/union)
---

# Resumo — modelo de build 0.3.1 (tempo escuro + paralelizacao)

## O achado central
O relogio de fase (`progress.tks`) e honesto mas cobre menos de metade do caminho. O **wall EXTERNO do
comando** (`time ./teko build`) e sempre maior que a soma auto-reportada porque tres regioes estao FORA
de qualquer fase: o backend NATIVO, o `cc`/link EXTERNO, e o CICLO DE VIDA do processo. Nao existe
TOTAL auto-reportado — a "soma" e feita a olho, somando as linhas impressas.

## Reconciliacao do "0.0s cada" (10.478s de wall)
Para uma sonda de 2000 instrucoes, `lower/isel/regalloc/encode` sao genuinamente ~0. O wall vive no
`cc`/link externo (`link_object`, project.tks:2735, sem fase) + ciclo de vida. Casa exato com a
precisao do dono: custo real FORA do que o compilador conta. Para o self-host (17.7MB) o EMISSOR
domina (AL-wave, 88 KB/s), dentro de encode/objfile que tambem sao mudos no native. Ortogonais.

## Provas arquivo:linha (o mapa do escuro)
- Backend native sem fase: `project.tks:2640-2645,2672-2677,2587,2612,2729-2741`.
- `cc`/link native nao cercado: `project.tks:2735`.
- 1o relogio so em `assemble.tks:216`; startup/parse-flags antes disso em `main.tks:37-92` (escuro).
- Sem total no exit (`main.tks` nao tem BuildClock de topo).
- Dupla contagem: `assemble.tks:216-217` abre lexer E parser no mesmo instante, assentam juntos 263-264.
- Atribuicao mentirosa `.tkr`: `regression.tks:2474-2478` (1a fila paga `child_ns` inteiro; demais
  `phase_zero()`); `child_ns=t_waited-t_spawn` em `regression.tks:211`; reuso `fresh=false` em 2121/2178.
- Neto `cc` da regressao dentro de `compile`: `regr_timing.tks:21-23` (ja documentado).

## A LEI (item 2 — DESIGUALDADE, correcao do dono 2026-08-02)
`wall_externo <= Σ fases_nomeadas` (a fase `process` de ciclo de vida incluida). NAO e igualdade:
travar em `==` quebraria no dia do paralelismo. Duas quantidades de sinal fixo:
`dark = max(0, wall−Σfases)` = tempo escuro, DEVE ser 0 (unico defeito); `overlap = max(0, Σfases−wall)`
= ganho de paralelismo, `>=0`, informativo (NAO e defeito). Sequencial: dark=0, overlap=0. Paralelo:
dark=0, overlap>0. Codigo: `BuildClock` (topo em main.tks), `PhaseLedger`+`ledger_record`+
`ledger_total_ns`+`ledger_reconcile` (falha SO em `dark>0`, devolve `Reconciliation{dark,overlap}`; a
regra antiga "balde negativo = defeito" foi REMOVIDA — era o que falharia depois). Alimenta o JOURNAL:
fase = `Record{kind="phase"}`, via `append`/`fold` — contrato contra a forma DECLARADA (dep bloqueado).

## Paralelizacao (item 3)
- Paralelizavel HOJE (por PROCESSO, sem threads): `.tkr`/projetos e shards do gate via `run_pool`
  (ja em producao) e `run_gate_sharded` (project.tks:3723).
- Paralelizavel mas BLOQUEADO (threads): map por-funcao isel/regalloc/encode (dado ja independente;
  fork_join de concorrencia-adiantada-s8 §3.2). Barreira = montagem do objeto (tabela de simbolos +
  layout de secoes) + link + ORDEM do fixpoint.
- Modelo de concorrencia: default = SO concede (falta primitivo — grep de nproc = VAZIO; hoje
  `TEST_JOBS_DEFAULT=4` hardcoded, project.tks:3658). Proposto `tk_nproc` (seed C mantido) +
  `teko::env::nproc()` + `build_jobs()` (pin via `TEKO_JOBS`, precedente `test_jobs`/`regr_jobs_of_default`).
- REGRA: medir antes de paralelizar. A lei (M1-M4) e pre-requisito duro da paralelizacao (P0-P3).

## Crumbs
M1 (fase native) · M2 (BuildClock topo) · M3 (PhaseLedger+reconcile) · M4 (fim da dupla contagem
lexer/parser + atribuicao .tkr honesta) · M5 (timing→journal, BLOQUEADO) · P0 (nproc+build_jobs) ·
P1 (regressao por run_pool) · P2 (gate sob build_jobs) · P3 (map por-funcao, BLOQUEADO por threads).

## Colisoes nomeadas
`codegen.tks`/`lower.tks`/`regalloc.tks`/`isel_*`/`encode_*` tem agentes vivos — M1 so ADICIONA
`phase_begin` em project.tks, nao toca a logica. M4 mexe so na aritmetica de RowTiming, nao no
`run_pool` (que tem o fix do Windows em regr_group.tks). P3 e so desenho.

## Tensao de lei resolvida
Fixtures NAO afirmam ns (proibido: "afirma o que FAZ, nao o numero"). Afirmam invariantes estruturais
(presenca/ordem de label, `dark==0`, `overlap>=0`, igualdade de veredicto serial-vs-paralelo). Sem HALT.

## Ritual
Apos M1 (stderr de todo build native), M2 (main.tks), M4 (aritmetica da regressao), P0 (seed C novo →
fixpoint pesado), P1 (igualdade de veredicto). Todas as fases sao stderr-only, fixpoint preservado por
construcao.
