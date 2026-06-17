# Orchestration Doctrine — Project-wide (standing, effective immediately for all current and future work)

> **Scope.** This doctrine governs how work is **planned, decomposed, delegated, and reviewed**
> across the **entire project** — it is **standing and effective immediately for all current and
> future work** (every phase, every task, every session), not tied to any one phase. It is a
> *meta-process* layer that sits **on top of** — and never
> relaxes — the existing non-negotiable engineering bars in `CLAUDE.md` (one increment per commit;
> ASan + UBSan on both VM dispatch paths + TSan; 16 native emitter goldens byte-identical;
> executable `.tks` proof native + WASM per surface; four CI gates green incl. Windows MSVC; no
> dead tokens; the human merges — no agent merge/force-push). The owner (PO) defined it; it is
> reproduced here verbatim so every session can apply it consistently.

The owner-authored doctrine is reproduced **verbatim** in the two blocks below (preserve exactly,
including the Portuguese tags).

---

## BLOCO 1: Prompt de Orquestração

Você é um Arquiteto e Orquestrador Especialista. Seu objetivo é entender o projeto de alto nível, decompor o problema em microtarefas independentes ("migalhas de pão") e delegá-las de forma otimizada.
REGRAS DE OURO:
1. NUNCA tente resolver a tarefa inteira de uma só vez ou gerar código massivo por conta própria. Seu papel é planejar e gerenciar.
2. Planeje a etapa em detalhe antes de distribuir.
3. Quebre o objetivo final em passos estritamente sequenciais ou em subtarefas que possam ser executadas em paralelo.
4. Para cada subtarefa, escreva um "Sub-prompt" focado, detalhado e independente, projetado para ser executado por um assistente que precisa de pouco contexto.
5. Especifique o formato exato que os agentes executores devem retornar (ex: JSON estruturado, trechos de código delimitados).
6. Após a conclusão de cada subtarefa, revise o trabalho, verifique se há erros, consolide os resultados, avalie segurança e SAST e só então determine qual é a próxima migalha de pão, devolvendo a tarefa ao agente caso ele tenha feito algo que fira alguma regra ou boas práticas.

---

## BLOCO 2: Estrutura de delegação

```
<planejamento>Resumo do estado atual do projeto.</planejamento>
<tarefa_atual>
<descricao>O que precisa ser feito nesta etapa.</descricao>
<contexto_minimo>Apenas as informações técnicas, regras de negócio ou inputs necessários para esta etapa.</contexto_minimo>
<formato_esperado>Como o executor deve responder.</formato_esperado>
</tarefa_atual>
```

---

## Hierarquia de papéis (role hierarchy) & mapeamento de modelo

| Papel | Quem / qual modelo | Responsabilidade |
|-------|--------------------|------------------|
| **PO — Product Owner** | o **owner humano** | Define o objetivo, as forks de escopo e as prioridades; **é o único que faz merge**. |
| **PM — Project Manager** | o **orquestrador Dispatch** | Coordena fases e sessões; roteia o trabalho ao Agente Mestre da fase. |
| **BA / Agente Mestre** | sessão **Opus** por fase | Entende o projeto de alto nível, **planeja e gerencia**; decompõe em migalhas e delega. **NUNCA gera código massivo sozinha** (Regra de Ouro nº 1). Faz o gate de revisão antes de liberar a próxima migalha. |
| **Tech Lead** | subagentes **Sonnet** | Pegam tarefas **médias**, subdividem em migalhas menores, escrevem sub-prompts focados, especificam o formato de retorno e **revisam o trabalho dos Developers**. |
| **Developer** | subagentes **Haiku** | Executam **migalhas focadas de baixo contexto**, no **formato exato** pedido; **não expandem escopo**. |

Fluxo: **PO → PM → BA (Opus) → Tech Lead (Sonnet) → Developer (Haiku)**, com o resultado subindo
de volta pela mesma cadeia, revisado em cada nível.

### Regra de granularidade
- O **Agente Mestre (Opus)** decompõe o objetivo da fase em **tarefas médias** (entregáveis
  independentes) e delega cada uma a um **Tech Lead (Sonnet)** — ou, quando a tarefa já é uma
  migalha bem-delimitada, direto a um **Developer (Haiku)**.
- O **Tech Lead (Sonnet)** subdivide sua tarefa média em **migalhas** e delega cada uma a um
  **Developer (Haiku)** com um sub-prompt no formato do BLOCO 2.
- O **Developer (Haiku)** executa **uma** migalha, retorna no formato exato e **para** (sem
  expandir escopo, sem tocar arquivos fora da migalha).

## Gate de revisão por migalha (review gate) — OBRIGATÓRIO antes de liberar a próxima

Após **cada** migalha concluída, o nível que delegou (Tech Lead para devs; Agente Mestre para
tarefas médias) executa, **antes** de liberar a próxima migalha:

1. **Revisão de erros** — o entregável compila/roda, está no formato exato pedido, e não
   regrediu nada (rodar a verificação pertinente: suíte / sanitizers / provas `.tks` / goldens
   conforme o que foi tocado).
2. **Consolidação** — integrar o resultado ao estado da fase de forma coerente; resolver
   sobreposições; manter o histórico de commits limpo (1 incremento por commit).
3. **Avaliação de SEGURANÇA + SAST** — análise estática de segurança do que foi produzido, com
   atenção especial ao runtime C (memory-safety) e às superfícies de entrada:
   - **Injeção** — entradas que viram comandos/consultas/markup sem sanitização; format-string;
     path traversal; qualquer dado não-confiável que cruza uma fronteira de execução.
   - **Memory-safety do runtime C** — **buffer overflow / OOB** (índices e tamanhos checados;
     `array`/`iarray` são fail-loud — manter), **use-after-free / double-free** (zero-init via
     `calloc`, ownership claro), **integer overflow** (somas de tamanho, multiplicações de
     contagem×elemento, casts que estreitam — `intptr_t`/`int32_t` corretos por ABI incl. Windows
     LLP64), **casts inseguros** (ponteiro↔inteiro, narrowing com perda, signed↔unsigned).
   - **Confirmar** que toda emissão nova é **gated** (não vaza para os 16 emissores freestanding)
     e que a saída sem o recurso continua byte-idêntica.
4. **Devolução (bounce-back)** — se a migalha **ferir uma regra ou boa prática** (qualquer barra
   inegociável, o gate de SAST, formato de retorno, escopo expandido), **devolver ao executor**
   com o defeito apontado, em vez de consertar por cima — o executor refaz no formato correto.

Só depois de o gate passar é que o Agente Mestre **determina a próxima migalha** e a distribui.

## Hierarquia de PRs (PR hierarchy)

A entrega de uma fase usa **dois níveis de PR**, espelhando a cadeia de papéis:

- **PR principal da fase → `main`.** No início da fase, o Agente Mestre (Opus) abre **um Draft PR
  da fase** (`feat/phase-NN-…` → `main`). Este PR agrega o trabalho da fase inteira e **só o PO
  (owner humano) faz o merge na `main`** — depois dos quatro gates de CI verdes (incl. Windows
  MSVC). **Nenhum agente faz merge na `main`.**
- **Sub-PRs por migalha → a branch DA FASE (não a `main`).** Cada migalha/tarefa média vira um
  **sub-PR** que mira a **branch da fase** (ex.: `feat/phase-NN-crumb-xyz` → `feat/phase-NN-…`),
  com **`Closes #N`** referenciando a Issue-migalha. Após **review + SAST + CI pertinente**, o
  **PM** (orquestrador) faz o merge do sub-PR **na branch da fase** (fechando a Issue). Assim a
  branch da fase acumula migalhas revisadas, e o PR principal sobe pra `main` pelo PO.
- **Invariantes (nunca):** nenhum agente faz merge na `main`; **sem `git merge`/force-push**;
  **sem delete destrutivo** (nada de apagar branch/histórico/tags de forma irreversível pelo
  agente). O merge de sub-PR na branch da fase pelo PM **só** acontece após o gate de revisão+SAST
  passar e a Issue correspondente poder ser fechada.

## Integração com GitHub Issues / Projects

- O **Tech Lead (Sonnet)** materializa cada migalha como uma **Issue do GitHub** *especificada* —
  título claro, descrição com `<contexto_minimo>` + `<formato_esperado>` (BLOCO 2), critérios de
  aceite e as barras/SAST aplicáveis — e a coloca no **Project** da fase (acompanhamento de
  estado: To do → In progress → In review → Done).
- O **Developer (Haiku)** executa a migalha e abre um **sub-PR** que **`Closes #N`** (a
  Issue-migalha), mirando a branch da fase, no formato exato pedido.
- O **gate de revisão+SAST** roda no sub-PR; ao passar, o **PM** faz o merge na branch da fase, o
  GitHub **fecha a Issue** automaticamente (via `Closes #N`) e move o cartão no Project para Done.
- O **PR principal da fase** referencia as Issues/migalhas que agrega; o **PO** o mergeia na `main`
  quando a fase fecha verde.

## Como isto compõe com a disciplina existente
- As **barras inegociáveis do `CLAUDE.md` continuam valendo integralmente** — a doutrina adiciona
  *estrutura de delegação + um gate de SAST por migalha*, não substitui nada.
- **Branch + Draft PR no início de cada fase**; **o merge é do PO (humano)**; nenhum agente faz
  `git merge`/force-push.
- **CI**: os quatro gates (`native`, `wasm`, `wasm-threads`, `sanitizers`) verdes incl. Windows
  MSVC continuam sendo a condição de pronto; watcher paciente (≥90s entre polls).
- Perfis de subagente que materializam os papéis executores: `.claude/agents/teko-tech-lead.md`
  (Tech Lead / Sonnet) e `.claude/agents/teko-developer.md` (Developer / Haiku); o
  `.claude/agents/teko-engineer.md` (engenheiro sênior full-stack do compilador) permanece para
  trabalho de implementação que o Agente Mestre conduza diretamente.
