---
section: design
created: 2026-08-02
status: DESENHO DE IMPLEMENTAÇÃO — reconcilia o desenho de `journaling-de-corrida-0.3.1.md` (C0–C9)
  com o que JÁ aterrou em `fix/union` e com o que está em voo (isolamento C1 drenado, isolamento C4
  em andamento). Nenhuma linha de produto escrita nesta carga. Teko-only; o único C tocado é
  `src/runtime/teko_rt.{c,h}` (exceção mantida). ZERO `.tkp` novo.
branch: cargo/0.3.1.0-journal-reconciliada (de fix/union @ 82d47dbf)
reconcilia:
  - docs/design/journaling-de-corrida-0.3.1.md            (o desenho: C0..C9, §2.2 superfície, §13 sumário, §14 captura)
  - docs/design/isolamento-builds-teste-sem-reload-ast-0.3.1.md  (C1 drenado; C4 sitemap em voo)
  - src/runtime/teko_rt.c  (C0 captura JÁ landed; alvo dos fundos do journal)
  - src/build/project.tks  (TEST_STAGE_REGR landed; merge_shard_coverage; report stage)
  - src/process/process.tks (verdict_emit JÁ delega em append_file)
prior-art: /home/user/wt-journal (cargo/0.3.1.0-journal-impl) — journal C1/C2/C4 escritos sobre base
  ANTIGA (pré-fix/union, pré-captura); reaproveitável como referência, NÃO merge-forward direto.
---

# Implementação reconciliada do journaling de corrida (0.3.1)

Arquiteto, 2026-08-02. Documento de DESENHO DE IMPLEMENTAÇÃO. O dono cobrou: o journaling está
**desenhado** (`journaling-de-corrida-0.3.1.md`, 4195 linhas, C0–C9) mas **não implementado**, e sem
agente a finalizá-lo. Entretanto duas peças ad-hoc do harness (isolamento C1/C4) aterraram ou estão
em voo e **tocam exatamente o transporte de cobertura que o journal reclama**. Este documento
reconcilia os dois desenhos, mede o que já existe, e entrega a **sequência de crumbs executável** do
que falta — cada crumb behavior-preserving (fixpoint byte-idêntico + `teko test` verde + cobertura
numericamente idêntica), com as dependências marcadas.

> **Nota de numeração, dita já para não confundir.** Há DUAS numerações C na casa:
> - **journal C0–C9** = os crumbs de `journaling-de-corrida-0.3.1.md` §10/§14.
> - **isolamento C1–C5** = os crumbs de `isolamento-builds-teste-sem-reload-ast-0.3.1.md` §3.
>
> O "C1/C4 ad-hoc" do briefing é o **isolamento** C1 (TEST_STAGE_REGR, drenado) e C4 (sitemap, em
> voo). Este documento renumera os crumbs de IMPLEMENTAÇÃO do journal com prefixo **J** (J0–J9) para
> nunca colidir com nenhuma das duas.

---

## 1. Estado real, medido — feito vs. falta

Auditei `fix/union @ 82d47dbf`. Três coisas do desenho do journal **já aterraram** (uma delas
inteira), duas estão **em voo por fora** (isolamento), e o módulo `teko::journal` propriamente **não
existe**.

### 1.1 O que JÁ está feito (e o desenho ainda não sabia)

| peça do desenho | estado | evidência (arquivo:linha) |
|---|---|---|
| **journal C0 — a captura em modo teste (§14)** | **FEITO, wired em todo o codegen** | `teko_rt.c:2330 tk_test_run` (setjmp interno), chokepoints em `tk_panic` (`:2572`) e `tk_exit` (`:2612`); `emit_test_call` já emite `tk_test_report(tk_test_run(&fn))` (`codegen.tks:12317`); `emit_test_main` termina em `tk_test_summary(); return tk_test_any_failed()?1:0` (`codegen.tks:12258`); guarda `src/test/capture_test.tkt` |
| **journal C0 — superfície Teko `run_capturing`** | **PLACEHOLDER (DESIGN-AHEAD)** | `src/test/test.tks:94` usa o shim `capture_probe` porque `cabi fn` (o tipo ponteiro-de-função C-ABI) ainda não é token do lexer; o `run_capturing(body: cabi fn())` de §14.3 espera-o |
| **journal C1 — `verdict_emit` deixa de ser O(n²)** | **JÁ RESOLVIDO por outra via** | `process.tks:396 verdict_emit` já faz `teko::io::append_file`, não mais o `read_file`+`write_file` por registo que §5.1 descrevia. O defeito quadrático **não existe mais**; falta só re-enraizar o canal na corrida (J2) |
| **agregação de cobertura POR FRAGMENTO** | **FEITO** | `coverage::cov_merge` → `teko_rt.c tk_cov_merge`; `merge_regression_coverage` (`regression.tks`); `merge_shard_coverage` (`project.tks:3887`) — todos dobram dumps de disco. É o "transporte de cobertura" do journal já realizado como fragmentos (isolamento §1.1c) |
| **`teko::test::scoped` (sufixo de escopo)** | **FEITO PARCIAL** | `test.tks:53 scoped(base)=concat(base, scope())` — a lane dos canais. É um SUFIXO por teste, **não** um caminho enraizado na corrida. Journal C2 re-enraíza-o |
| **isolamento C1 — `TEST_STAGE_REGR`** | **FEITO, MERGED** | `project.tks:5741 TEST_STAGE_REGR`, `:5794` dispatch, `:5839` `run_test_stage(TEST_STAGE_REGR,...)`, `:5860 test_stage_regr`. A tier de regressão saiu do driver (cura do OOM) |
| **`scratch_collision_test.tkt`** | **EXISTE (parcial)** | `src/test/scratch_collision_test.tkt` — material de C8 (prova por colisão) da lane dos canais |

**Consequência de reconciliação nº 1:** o journal C0 e o antigo journal C4b **já estão resolvidos**.
A captura landed; o tally já é um contador (`tk_test_summary`, `teko_rt.c:2403`), não uma
reconstrução. O `never-ran` auto-infligido já é 0 por construção. Portanto **o C0 sai da fila de
implementação** (resta só substituir o shim `capture_probe` por `run_capturing` quando `cabi fn`
aterrar — DESIGN-AHEAD, J0).

### 1.2 O que está EM VOO por fora (isolamento)

| peça | estado | branch | relação com o journal |
|---|---|---|---|
| **isolamento C1 (TEST_STAGE_REGR)** | drenado em `fix/union` | (merged `f1c0ca8a`) | entrega a MESMA isolação que o journal C9 queria para a fase de regressão, mas por **re-invocação de processo por env-stage**, não por `fold` de journal. A cura do OOM **já está entregue** — ver §3.3 |
| **isolamento C4 (sitemap)** | **anti-restart commit só; trabalho real NÃO começou** | `cargo/0.3.1.0-test-report-sitemap @ 6226e91d` | vai emitir `cov_emit_sitemap`/`cov_cobertura_from_sitemap` para o report renderizar SEM `recheck_frontend`/`cov_cobertura(fe.prog)`. É o **transporte de FATO de cobertura** do journal — ver §4 (o encaixe) |

### 1.3 O que FALTA (o journal propriamente)

Nada disto existe em `fix/union`:

| falta | crumb-journal original | onde nasce |
|---|---|---|
| o módulo `teko::journal` (`run_id`/`run_root`/`open`/`append`/`fold`/`scratch`/`sweep`) | C1+C2+C4 §2.2 | `src/journal/journal.tks` (novo) |
| os fundos de runtime `tk_journal_open`/`_append`/`_note` + `tk_rt_rename` | C1 §2.3 | `teko_rt.{c,h}` |
| a injeção do namespace `teko::journal` | C1 §10 | `src/checker/scope.tks` |
| a identidade de corrida (re-enraizar `scoped`, `TEKO_RUN` herdado, ferrolho da raiz) | C2 | `journal.tks`, `test.tks`, `regression.tks` |
| a migração das **28 famílias** de caminho para `scratch` (incl. `/tmp/teko_arena_obs.txt`) | C3 | `regression.tks`, `project.tks`, `teko_rt.c` |
| `summarize`/`render_summary`/`RunSummary`/`Finding` + `merge_shard_coverage` falível + `cov_missing` | C4 §13 | `src/journal/summary.tks` (novo), `project.tks:3887` |
| os sinais educados (INT/TERM/HUP/QUIT) + `tk_journal_note` no crash handler | C5 §6 | `teko_rt.c` |
| a durabilidade nomeada (`write(2)` O_APPEND, botão `TEKO_JOURNAL_FSYNC`) | §5 | `teko_rt.c` |
| G1 alargada a `.tks` (compositor único) | C6 | `scratch_guard_test.tkt` |
| G2 observacional (a raiz é a única coisa criada) | C7 | `journal_guard_test.tkt` (novo) |
| a prova por colisão forçada, completa | C8 | `scratch_collision_test.tkt` (existe parcial) |
| `teko test --replay` + aviso de corrida não-sumarizada no `sweep` | C9 | `project.tks`, `regression.tks` |

### 1.4 Prior art reaproveitável — `wt-journal`

Existe uma tentativa PARADA em `/home/user/wt-journal` (`cargo/0.3.1.0-journal-impl`) que escreveu
journal C1/C2/C4 (`src/journal/journal.tks`, `summary.tks`, `summary_test.tkt`) **sobre base antiga**
(pré-`fix/union`, pré-captura, commits `5cb941f8`/`f10086f8`/`3c9ea69a`). **REPORTADO como referência,
não como merge-forward.** A superfície dele divergiu do desenho (`open_at(path,run,writer)` em vez de
`open(writer)`; fundos extra `tk_journal_close`/`tk_rt_monotonic_ns`/`tk_rt_pid`/`tk_rt_rmtree`/
`tk_rt_pid_alive`). O implementer deve **copiar as bodies úteis** (o `fold_segment`, o `sweep` com
verificação de pid vivo, o `run_id` monotónico+pid) mas **re-derivar contra a superfície de §2.2** e
contra a árvore de hoje (onde a captura já landed). Não fazer o cherry-pick cego — a base mudou por
baixo.

---

## 2. Mapa de reconciliação — journal C0–C9 × realidade

| journal | descrição (§) | reconciliação | vira crumb-J |
|---|---|---|---|
| **C0** | captura em modo teste (§14) | **FEITO** (§1.1). Só a superfície `run_capturing` é DESIGN-AHEAD (`cabi fn`) | **J0** (só o swap do shim) |
| **C1** | `teko::journal` + fundos + `verdict_emit` delega | módulo NÃO existe; fundos NÃO existem; `verdict_emit` **já** delega em `append_file` (falta re-enraizar) | **J1** |
| **C2** | identidade de corrida; `scoped` re-apontada; ferrolho | NÃO existe; `scoped` é sufixo hoje | **J2** |
| **C3** | migrar 28 famílias para `scratch` (incl. obs.txt) | NÃO feito | **J3** |
| **C4** | `fold`+`summarize`+`render_summary`; merge falível; shard-sem-`end`=falha nomeada | NÃO feito. **O tally já é contador (C0)**, então encolhe: `summarize` é fold puro; a **renderização de cobertura** cruza-se com isolamento-C4 (§4) | **J4** |
| ~~C4b~~ | reconstruir o que a morte levou | **DISSOLVIDO** — a captura já o dispensou (§14.4). Fora da fila | — |
| **C5** | sinais educados + `tk_journal_note` no crash | NÃO feito (só SEGV/BUS/ILL/FPE existem, `teko_rt.c:120`) | **J5** |
| **C6** | G1 alargada a `.tks` | NÃO feito | **J6** |
| **C7** | G2 observacional | NÃO feito | **J7** |
| **C8** | prova por colisão forçada | `scratch_collision_test.tkt` existe parcial | **J8** |
| **C9** | `--replay` + aviso no sweep + regressão repartida por `run_pool` | a **cura do OOM já veio por isolamento-C1** (§3.3); resta `--replay` + aviso; a repartição por `run_pool` **rebaixa-se a opcional/medido** | **J9** |

**Resumo do delta:** dos 10 crumbs do journal, **1 está feito (C0)**, **1 dissolvido (C4b)**, e **8
faltam** (J1–J9, sendo J0 o remate DESIGN-AHEAD de C0). Um dos 8 (J4) encaixa-se com isolamento-C4;
um (J9) tem metade do escopo já entregue por isolamento-C1.

---

## 3. Onde as duas mecânicas se sobrepõem — e a fronteira que as separa

O desenho do journal assumiu **uma raiz de corrida** por onde tudo flui (vereditos unitários,
vereditos de regressão, cobertura) e uma sumarização por `fold`. O isolamento construiu **outra
mecânica** para o mesmo fim: staging por re-invocação de processo (env-slots) e cobertura por dumps
`.tkcov` por fragmento + sitemap serializado. **As duas cruzam-se em exatamente um ponto: o
transporte de cobertura.** Fora dele, são ortogonais e compõem-se.

### 3.1 A fronteira, dita numa frase

> **O journal transporta o QUE ACONTECEU na corrida (registos append-only, carimbados): begin/ok/
> fail/skip/dead. O sitemap+`.tkcov` transportam a COBERTURA MEDIDA (site-id→ficheiro:linha + hits).
> O sumário (§13) é a JUNÇÃO dos dois — ele *lê* ambos, não *possui* nenhum.**

O journal **não reescreve** `cov_cobertura`; consome o que o subsistema de cobertura já funde. O
isolamento-C4 **não inventa** um canal de veredicto; continua a usar os env-slots de rc. Cada um fica
com o seu verbo.

### 3.2 Colisão em `teko_rt.c` — M2 e o 5º gap

O briefing avisa que M2 e o 5º gap também tocam `teko_rt.c`. Medi as três regiões e **não se
sobrepõem**:

| workstream | região em `teko_rt.c` | símbolos |
|---|---|---|
| **M2** (move-on-return, `fechamento-seguro-m2`) | ~1160–1863 (regiões/pilha) + codegen brackets | `tk_region_enter`/`tk_region_leave` (`:1855`/`:1861`) |
| **5º gap** (slice/box, `migracao-runtime-c-para-teko`) | ~3762–3995 | `tk_slice_push`/`_push_fo`/`tk_slice_elem_box`/`tk_mem_copy`/… |
| **journal (J1/J5)** | símbolos NOVOS + o instalador de sinal (`:120`) + o crash handler (`:119`) | `tk_journal_open`/`_append`/`_note`, `tk_rt_rename`, `tk_rt_stop_handler` |

**Recomendação de sequência anti-colisão:**
1. Os fundos do journal são **símbolos novos** — não editam nenhuma função que M2 ou o 5º gap tocam.
   O ÚNICO ponto partilhado seria o crash handler / instalador de sinal (`teko_rt.c:119-127`) para o
   J5 acrescentar `tk_journal_note` e os handlers educados — e **nem M2 nem o 5º gap tocam ali**.
   Portanto J1/J5 são **collision-free** com M2/5º gap por construção.
2. O risco residual é o **conflito de cauda** (todos apensam ao mesmo ficheiro). Mitigação: o
   implementer apensa os fundos do journal num **bloco delimitado** (um `// ===== journal (§2.3) =====`
   e o fecho), e rebaseia depois de M2/5º gap aterrarem as suas edições de região/slice. Como não há
   sobreposição semântica, o rebase é mecânico.
3. **Não sequenciar J1/J5 atrás de M2 nem do 5º gap.** Eles são independentes; o custo de os atrasar
   é maior que o custo do rebase de cauda.

### 3.3 Colisão em `project.tks` — isolamento-C1 (feito) e isolamento-C4 (em voo)

- **isolamento-C1** já mexeu no dispatch de `test_project` e em `test_drive` (`:5794`, `:5839`). O
  journal J9 (repartição da regressão) tocaria as MESMAS funções — mas J9 rebaixa a repartição a
  opcional (§3.4), então **não colide**: J9 só acrescenta `--replay` (um novo stage/flag) e o aviso
  no `sweep`.
- **isolamento-C4** vai reescrever o **report stage** (`project.tks:3965 cov_cobertura(fe.prog)` →
  `cov_cobertura_from_sitemap`) e `coverage.tks`. O journal J4 toca `merge_shard_coverage`
  (`project.tks:3887`, função DIFERENTE) e acrescenta o módulo de sumário (ficheiro novo). **As duas
  funções são distintas**, mas J4 **consome** o output de C4 (o sitemap) para a linha COVERAGE do
  bloco §13.4 depois da morte. Logo: **J4 espera isolamento-C4 drenar** (dependência, §5).

### 3.4 O que isolamento-C1 já entregou do journal C9

O journal C9 queria "a fase de regressão entra no mesmo mecanismo" (repartida por `run_pool`, fold dos
segmentos) **para curar o OOM**. O OOM **já está curado** por isolamento-C1 (a tier saiu do driver por
env-staging). Portanto a repartição por `run_pool` do journal C9 **perde a sua urgência**: passa a ser
um ganho de LATÊNCIA (paralelizar a fase), não de correção. E o próprio journal §9 já diz que essa
repartição **exige medição build-vs-linhas primeiro e aborta se a build dominar**. **Recomendação:**
J9 entrega `--replay` + o aviso no `sweep` (baratos, fecham o buraco do OOM-perde-veredicto); a
repartição por `run_pool` fica **fora deste plano**, atrás da medição de §9, como trabalho separado.

---

## 4. O encaixe do C4 (isolamento) — recomendação

**Pergunta do briefing:** o isolamento-C4 vira o crumb-de-cobertura do journal, ou fica point-fix e o
journal absorve-o depois?

**Recomendação: isolamento-C4 fica POINT-FIX na sua própria lane; o journal J4 ABSORVE o output dele,
não o trabalho.** Três razões, law-first:

1. **Verbos diferentes (§3.1).** O sitemap responde "site-id→ficheiro:linha" — um **artefacto de
   build** publicado por `rename` (o próprio `tk_rt_rename` que o journal §2.3 pede!). O journal
   responde "o que aconteceu na corrida" — **registos append-only**. Fundir os dois num crumb misturava
   dois mecanismos, e a lei da casa (journal §1: "um mecanismo só") é contra misturar. Cada um é um
   verbo do MESMO princípio ("o escritor só publica o que está completo", §2.3): append para o fluxo,
   rename para o artefacto inteiro.
2. **Já está em voo com dono-agente.** isolamento-C4 tem branch e anti-restart commit. Puxá-lo para
   dentro do journal agora colidiria em `coverage.tks`/report-stage e travava as duas frentes. O
   journal **espera-o drenar** e consome o `.tksites` que ele publica.
3. **O benefício do journal sobre cobertura é PEQUENO e não é a renderização.** O que o journal
   acrescenta é a **falibilidade** (`merge_shard_coverage` que PARA e NOMEIA a shard sem dump, §2.4) e
   o **`cov_missing`** na linha COVERAGE do sumário (§13.4). Isso senta-se EM CIMA da agregação por
   fragmento que já existe e do sitemap que C4 constrói — não os reescreve.

**A costura, desenhada:**

```
isolamento-C4 (lane própria)            journal J4 (este plano)
────────────────────────────           ─────────────────────────
cov_emit_sitemap(prog): .tksites      merge_shard_coverage(base,jobs): null|error   [falível]
cov_cobertura_from_sitemap(sitemap)     summarize(fold(root,run)): RunSummary
  (report renderiza sem recheck)          .cov      = CovTriple lido dos .tkcov já fundidos
                                          .cov_missing = quantos dumps esperados faltaram
                                        render_summary(RunSummary): o bloco §13.4
                                          (a linha COVERAGE cita .tksites p/ nomear ficheiro:linha
                                           das falhas, DEPOIS da morte, via --replay)
```

**Onde o journal NÃO pode duplicar C4:** não reescrever `cov_cobertura`, não reparsear a AST, não
tocar o report-stage de render. O journal J4 lê o `.tksites` como **input opaco** (uma tabela plana),
exatamente como o report de C4 o lê. Se C4 mudar o formato do `.tksites`, é um contrato entre C4 e J4
— e J4 deve escrever-se contra a **forma declarada** de C4 (a assinatura `cov_emit_sitemap` de
isolamento §2.4), como scaffolding DESIGN-AHEAD, honest-stop enquanto C4 não drena.

---

## 5. Sequência ORDENADA de crumbs de implementação (J0–J9)

Ponto ritual = **gate completo**: gen2 nativo verde + `teko test .` verde + **FIXPOINT gen2==gen3
byte-idêntico** + **cobertura numericamente idêntica** (os dumps movem de caminho mas dobram para os
mesmos números). Semente: nenhuma superfície de linguagem nova além de `cabi fn` (só o J0, que é
DESIGN-AHEAD e espera-a); tudo o resto usa `const`/`fn`/`struct`/`enum`/`match`/`extern fn`/`env` —
todos na semente.

| J | crumb | depende de | ritual | colisão |
|---|---|---|---|---|
| **J0** | swap `capture_probe`→`run_capturing(body: cabi fn())` | `cabi fn` no lexer (BLOQUEADO) | sim | `test.tks` |
| **J1** | módulo `teko::journal` + fundos `tk_journal_open/_append/_note` + `tk_rt_rename`; injeção de namespace; `verdict_emit` re-enraíza | — (independente) | **sim** | `teko_rt.c` (novo bloco), `scope.tks`, `process.tks` |
| **J2** | identidade de corrida: `run_id/run_root/scratch/sweep`; `scoped`→`journal::scratch`; `TEKO_RUN` herdado; ferrolho | J1 | **sim** (muda caminhos) | `journal.tks`, `test.tks`, `regression.tks` |
| **J3** | migrar 28 famílias para `scratch` (+`obs.txt`) | J2 | **sim** (caminhos tree-wide) | `regression.tks`, `project.tks`, `teko_rt.c`, 4 `.tkt` |
| **J4** | `summarize/render_summary/RunSummary/Finding` + `merge_shard_coverage` falível + `cov_missing` | J1 (fold); **isolamento-C4 drenado** (sitemap) | **sim** | `summary.tks` (novo), `project.tks:3887` |
| **J5** | sinais educados (INT/TERM/HUP/QUIT) + `tk_journal_note` no crash handler | J1 | **sim** | `teko_rt.c:119-127` (collision-free c/ M2, 5º gap) |
| **J6** | G1 alargada a `.tks`, compositor único, inversão estendida | J3 | não | `scratch_guard_test.tkt` |
| **J7** | G2 observacional + inversão de 3 braços | J3 | não | `journal_guard_test.tkt` (novo) |
| **J8** | prova por colisão forçada, completa (ANTES+DEPOIS na mesma corrida) | J2 | **sim** | `scratch_collision_test.tkt` (existe) |
| **J9** | `teko test --replay <run>` + aviso de corrida não-sumarizada no `sweep` | J4, J2 | **sim** | `project.tks`, `regression.tks` |

**Ordem obrigatória:** **J1 primeiro** (funda o módulo e os fundos; é independente e desbloqueia tudo).
Depois **J2** (identidade — muda caminhos, exige fixpoint). Depois **J3** (migração — o maior toque de
caminhos). **J5** pode correr em paralelo com J2/J3 (só toca `teko_rt.c`, região disjunta). **J4**
espera J1 **e** isolamento-C4 drenar. **J6/J7/J8** depois de J3 (a regra já é total). **J9** por último
(precisa de J4 e J2). **J0** entra quando `cabi fn` aterrar, ortogonal a tudo.

### 5.1 J1 — o módulo e os fundos (o alicerce)

Cria `src/journal/journal.tks` com a superfície de §2.2. Os fundos de runtime (§2.3), num bloco novo
e delimitado de `teko_rt.{c,h}`. Injeta `teko::journal` em `src/checker/scope.tks` (precedente direto:
a lane dos canais fez o mesmo para `teko::test`, +106 linhas).

Assinaturas que o implementer adiciona (copiar verbatim; full-Javadoc):

```teko
/**
 * open_seg_rt — o fundo cru: abre/cria `path` em O_WRONLY|O_APPEND|O_CREAT e devolve o descritor.
 *
 * SEM stdio, de propósito. Um `FILE*` traz um buffer de espaço de utilizador, e um buffer de espaço
 * de utilizador morre com o processo — que é o único momento em que este ficheiro tinha valor
 * (§1.3). O descritor cru põe o registo no kernel a cada `append`.
 *
 * @param path  o caminho do segmento, já enraizado na corrida
 * @return      o descritor (>= 0), ou o `-errno` numa falha de abertura
 * @since 0.3.1
 */
extern fn open_seg_rt(path: str): i64 = "tk_journal_open" from "teko_rt"

/**
 * append_seg_rt — escreve `rec` inteiro no descritor `seg`, repetindo numa escrita curta.
 *
 * O PONTO DE DURABILIDADE É ESTE (§5): uma `write(2)` sobre um descritor O_APPEND sem buffer de
 * utilizador, atómica no deslocamento. Quando devolve 0, o registo está no kernel — sobrevive a
 * SIGKILL, abort, _Exit, OOM. Só uma queda da máquina o perderia (a fronteira de §5, e o botão
 * `TEKO_JOURNAL_FSYNC=1` cobre-a com o custo medido no crumb que o aterra).
 *
 * @param seg  o descritor aberto por `open_seg_rt`
 * @param rec  o registo já formatado, terminado em `\n`, <= TK_JOURNAL_REC_MAX bytes
 * @return     0, ou o `errno` da escrita (ENOSPC/EIO) — e a falha é PEGAJOSA no chamador (§4)
 * @since 0.3.1
 */
extern fn append_seg_rt(seg: i64, rec: str): i32 = "tk_journal_append" from "teko_rt"

/**
 * rename_rt — renomeia atomicamente dentro do mesmo diretório (rename(2)/MoveFileExW REPLACE_EXISTING).
 *
 * A SEGUNDA METADE DA DISCIPLINA (§2.3): o que é append vai por append; o que é artefacto inteiro
 * escreve-se num temporário do próprio escritor e publica-se por `rename`. Nenhum leitor vê um
 * ficheiro meio-escrito. É o mesmo primitivo que o `.tksites` de isolamento-C4 usa para publicar.
 *
 * @param from  o temporário do escritor
 * @param to    o caminho publicado
 * @return      0, ou o `errno`
 * @since 0.3.1
 */
extern fn rename_rt(from: str, to: str): i32 = "tk_rt_rename" from "teko_rt"

/**
 * open — abrir o segmento de `writer` nesta corrida (§2.2).
 *
 * @param writer  a identidade do escritor; dois escritores vivos nunca a repetem (`s<i>`/`p<i>`/`m`)
 * @return        o segmento aberto
 * @throws        quando o segmento não pode ser criado (permissão, disco cheio)
 * @since 0.3.1
 */
pub fn open(writer: str): Journal | error

/**
 * append — acrescentar UM registo ao segmento de `j`, com a falha PEGAJOSA de §4 (modo 5): o primeiro
 * `append` falhado marca o journal e o processo sai não-zero com `teko: journal write failed`. Um
 * journal que não pode ser escrito não pode ser saltado em silêncio — é a lei da guarda aplicada ao
 * próprio instrumento.
 *
 * @param j        o segmento
 * @param kind     a espécie (`begin`/`ok`/`fail`/`skip`/`cov`/`end`/`stop`/`crash`)
 * @param payload  o corpo, sem quebras de linha (o emissor escapa-as)
 * @throws         quando a escrita falha
 * @since 0.3.1
 */
pub fn append(j: Journal, kind: str, payload: str): null | error
```

Os `type Journal`/`type Record` e as bodies de `run_id`/`run_root`/`fold`/`scratch`/`sweep` copiam de
§2.2 e reaproveitam as bodies do prior art `wt-journal` (§1.4), re-derivadas contra esta superfície.
**`verdict_emit` (`process.tks:396`) já delega em `append_file`;** J1 troca o alvo pelo segmento do
escritor (via `journal::append`), preservando o comportamento observável (o canal continua a receber
as mesmas linhas).

**Fixtures J1 (unit, `src/journal/journal_test.tkt` — NÃO é `.tkp`, é teste da própria suite):**
- `j_append_then_fold_returns_the_record` — abre um segmento, apensa `ok probe`, `fold` devolve-o.
- `j_a_record_of_another_run_is_discarded` — planta um registo com outro carimbo; `fold` descarta-o
  (§2.2 regra 1).
- `j_a_torn_last_line_is_dropped` — um segmento cuja última linha não acaba em `\n`; `fold` descarta-a
  (regra 2).
- `j_a_segment_without_end_yields_incomplete` — segmento sem `end`; `fold` sintetiza `incomplete`
  (regra 3).

Behavior-preserving: os fundos são símbolos novos; nada os lê exceto os testes novos. **Ritual: sim**
(toca `scope.tks` e `teko_rt.c`).

### 5.2 J2 — identidade de corrida, e o re-enraizamento de `scoped`

Bodies de `run_id()` (`<ns monotónico>-<pid>`), `run_root()` (`bin/.tkrun/<run_id>`), `scratch(base)`
(`<run_root>/<writer>/<escopo>/<base>`, dirs criados), `sweep(keep)` (varre `bin/.tkrun/` menos `keep`,
saltando raízes com ferrolho vivo). `teko::test::scoped` (`test.tks:53`) passa de
`concat(base, scope())` (sufixo) para `teko::journal::scratch(base)` (enraizado). `TEKO_RUN` herdado
por `spawn_spec` (`regression.tks`) para que o filho reuse o `run_id` do pai. Ferrolho na raiz que
**recusa** uma segunda corrida no mesmo caminho com mensagem nomeada (§11.3).

**Fixture J2:** reusa `scratch_collision_test.tkt` (já existe) — dois filhos no mesmo `run_pool`
derivam caminhos da sua identidade e sobrevivem inteiros (o DEPOIS de §8). ZERO `.tkp` novo.

Behavior-preserving mas **muda caminhos** → **fixpoint obrigatório** e **cobertura numericamente
idêntica** (os `.tkcov` movem para dentro da raiz mas dobram para os mesmos números). **Ritual: sim.**

### 5.3 J3 — a migração das 28 famílias

Substituição mecânica: todo caminho escrito passa por `journal::scratch`. Inclui `/tmp/teko_arena_obs.txt`
(`teko_rt.c:1256`, o pior — fixo, sem carimbo, partilhado por todo o host) que ganha um caminho
carimbado. As 22 comparações de §3.1 **não mudam** (não escrevem). Ler os chamadores do compositor
único é a nova auditoria.

**Prova:** as fixtures existentes de `own_native` continuam a casar o seu stdout (nenhum canal novo).
**Ritual: sim** (caminhos tree-wide; fixpoint + cobertura idêntica).

### 5.4 J4 — o sumário, e a costura com isolamento-C4

`RunSummary`/`Finding`/`PhaseTally`/`CovTriple` (§13.3), `summarize(recs: []Record): RunSummary`
(fold puro), `render_summary(s: RunSummary): str` (o bloco §13.4). `merge_shard_coverage`
(`project.tks:3887`) passa de `-> null` (descarta) para `-> null | error` que PARA e NOMEIA a shard
sem dump (§2.4), alimentando `cov_missing`.

**Reconciliação com o tally já-contador (C0):** `tk_test_summary` (`teko_rt.c:2403`) já imprime o
bloco UNITÁRIO por contador. O `render_summary` do journal é o bloco CRUZADO (unit+regressão+cobertura+
mortos), por `fold`, que sobrevive à morte. **Recomendação de fronteira:** manter `tk_test_summary` C
como a contribuição rápida da shard unitária (emite os seus `begin`/`ok`/`fail` para o journal); o
`render_summary` Teko é o **único bloco cruzado**, emitido pelo orquestrador (driver/report stage).
Evita dois sumários a competir. (Esta é a única tensão de reconciliação genuína — ver §6.)

**Dependência dura:** **espera isolamento-C4 drenar** para (a) não colidir no report-stage e (b)
consumir o `.tksites` na linha COVERAGE. Enquanto C4 não drena, J4 escreve-se DESIGN-AHEAD contra a
assinatura declarada `cov_emit_sitemap`/`cov_cobertura_from_sitemap` (isolamento §2.4), com honest-stop
na leitura do sitemap. **Ritual: sim.**

**Fixture J4:** o cenário de §13.7 em `own_native` (reusa canal existente):
`journal_summary_names_a_planted_skip` — monta registos sintéticos com uma falha e um skip, renderiza,
afirma pelo NOME (`Then stdout pattern = "scenario journal_summary_names_a_planted_skip: ok"`). Mais os
3 `#test` de §13.6 (`js_the_summary_names_every_finding`, `js_an_empty_run_is_a_FAILURE_not_a_green`,
`js_a_dead_writer_cannot_read_as_passed`) em `src/journal/summary_test.tkt`.

### 5.5 J5 — os sinais educados

`tk_rt_stop_handler` (INT/TERM/HUP/QUIT → `tk_journal_note(sig)` + `_Exit(128+sig)`, §6) e
`tk_journal_note` no `tk_rt_crash_handler` (fecha de passagem o buraco do despejo de arena no crash,
§1.3 defeito 3). Tudo em `teko_rt.c`, região disjunta de M2/5º gap (§3.2). **Collision-free.**

**Fixture J5:** um cenário `own_native` que envia SIGTERM a um filho e afirma que o journal ganhou um
registo `stop` (via `run_pool` + marcador, o material de §8 já na árvore). **Ritual: sim.**

### 5.6 J6/J7/J8 — as guardas e a prova

- **J6:** G1 (`scratch_guard_test.tkt`, já landed) alargada de `.tkt` para `.tkt`+`.tks` (as 14
  famílias de classe B não têm literal em `.tkt`); o compositor exigido passa a `teko::journal::scratch(`.
  Inversão: a existente + 2 linhas plantadas de classe B.
- **J7:** G2 observacional (`journal_guard_test.tkt`, novo) — fotografa `bin/`/`out/`/`/tmp`, corre uma
  corrida, e todo caminho novo tem de estar dentro de `run_root()`. Inversão de 3 braços (incl.
  vivacidade do instrumento, §7.2).
- **J8:** completa `scratch_collision_test.tkt` (existe parcial) com o par ANTES/DEPOIS na mesma
  corrida (§8) + o `jc_round_robin_puts_neighbours_in_different_shards`. **Ritual: sim** (a prova tem
  de correr verde e o braço ANTES tem de continuar a falhar quando o mecanismo é removido).

J6/J7 não são ritual (só acrescentam guardas). J8 é ritual.

### 5.7 J9 — `--replay` + aviso no sweep

`teko test --replay <run>` re-imprime o bloco §13.4 de uma raiz carimbada (o mesmo `fold`+`summarize`+
`render_summary` de J4, sobre uma raiz antiga). O `sweep`, antes de varrer, vê uma raiz sem sumário e
diz a linha de aviso (§13.5). **A repartição da regressão por `run_pool` fica FORA deste plano** (§3.4:
o OOM já está curado; a repartição exige a medição build-vs-linhas de §9). Depende de J4 e J2.
**Ritual: sim.**

---

## 6. Riscos e tensões de lei

| risco / tensão | resolução |
|---|---|
| **dois sumários** (o `tk_test_summary` C já-landed × o `render_summary` Teko de J4) | RESOLVIDO em §5.4: o C fica a contribuição da shard unitária; o Teko é o único bloco cruzado, emitido pelo orquestrador. Não é HALT — a lei "um mecanismo só" (§1) decide: o cruzado é o autoritativo porque sobrevive à morte |
| **J4 bloqueado em isolamento-C4** | DESIGN-AHEAD: J4 escreve-se contra a assinatura declarada `cov_emit_sitemap` (isolamento §2.4), honest-stop na leitura do `.tksites`. Resume em minutos quando C4 drena. O resto de J4 (`summarize`/`render_summary`/merge falível) NÃO depende de C4 e pode aterrar já |
| **colisão de cauda em `teko_rt.c`** (M2, 5º gap, journal) | §3.2: regiões disjuntas; journal apensa em bloco delimitado e rebaseia. Sem sobreposição semântica → rebase mecânico. **Não** sequenciar J1/J5 atrás de M2/5º gap |
| **`cabi fn` ausente bloqueia a superfície `run_capturing`** (J0) | a captura JÁ funciona pelo shim `capture_probe` (C0 landed); J0 é só o swap cosmético quando `cabi fn` aterrar. Nada a jusante espera J0 |
| **cobertura numericamente idêntica sob J2/J3** (caminhos movem) | os `.tkcov` movem de caminho mas o `fold`/`cov_merge` dá a UNIÃO dos mesmos dumps → mesmos números. O gate ritual (cobertura idêntica) é o detector |
| **fixture por exit-code** (o meu formato-padrão) × ruling do dono | **JÁ RESOLVIDO no journal §12**: o dono baniu "afirma o número com que sai". Toda fixture deste plano afirma por `Then stdout pattern = "scenario <nome>: ok"` em `own_native`, canal EXISTENTE. ZERO `.tkp` novo |
| **prior art `wt-journal` sobre base antiga** | §1.4: copiar bodies, re-derivar superfície; NÃO merge-forward cego (a base mudou — a captura landed por baixo) |

**Tensão de lei genuína?** Uma só, e é resolvível law-first sem HALT: os **dois sumários**. A lei "um
mecanismo só" (journal §1) e o ruling "sem meio-termos" ditam um bloco cruzado autoritativo; o
`tk_test_summary` C rebaixa-se a produtor de registos da shard. **Nenhum HALT.**

---

## 7. Fixtures de regressão (todas em canais/projetos EXISTENTES — ZERO `.tkp` novo)

Reafirmadas por stdout-pattern (ruling do dono, journal §12), nunca por exit-code:

| fixture | onde vive | afirma |
|---|---|---|
| `j_append_then_fold_*` (4 `#test`) | `src/journal/journal_test.tkt` (suite própria) | as 3 regras de higiene do `fold` (§2.2) |
| `scratch_collision` ANTES/DEPOIS + round-robin | `src/test/scratch_collision_test.tkt` (existe) | a colisão forçada (§8) — o defeito do dono reproduzido e curado na mesma corrida |
| `journal_summary_names_a_planted_skip` | cenário em `own_native.tkr` | o bloco §13.4 nomeia falha+skip (`scenario ...: ok`) |
| `js_*` (3 `#test`) | `src/journal/summary_test.tkt` | a inversão de 3 braços do sumário (§13.6), incl. vivacidade |
| `jg_*` (2 `#test`) | `src/journal/journal_guard_test.tkt` | G2 observacional + inversão (§7.2) |
| G1 alargada + 2 plantadas | `src/test/scratch_guard_test.tkt` (existe) | o compositor único cobre classe A e B |
| stop-signal ganha registo | cenário em `own_native.tkr` | J5 — SIGTERM deixa um `stop` no journal |

---

## 8. O que este plano NÃO faz (dito na primeira pessoa)

1. **Não implementa** — é desenho. Nenhuma linha de produto escrita nesta carga.
2. **Não puxa isolamento-C4 para dentro do journal** (§4) — ele fica point-fix; o journal consome o
   `.tksites`.
3. **Não reparte a fase de regressão por `run_pool`** (§3.4) — o OOM já está curado por isolamento-C1;
   a repartição fica atrás da medição build-vs-linhas de §9, como trabalho separado.
4. **Não faz bump, não abre PR** (ruling do briefing).
5. **Não converte achados adjacentes em issues** — o custo O(distintas) do `tk_cov_mark`/`tk_covb_add`
   (journal §5.2, varrimento linear vs. o conjunto de endereçamento aberto que as linhas já usam)
   fica **REPORTADO ao dono**, não puxado por mim. É questão de CUSTO, não de correção, e não é
   pré-requisito de nenhum crumb J.

---

## 9. Resumo executável (a ordem que o implementer segue)

```
J1  módulo teko::journal + fundos (independente)          [ritual]  ── começa aqui
 │
 ├─ J2  identidade de corrida; scoped re-enraizada        [ritual, fixpoint]
 │   │
 │   ├─ J3  migrar 28 famílias para scratch               [ritual, fixpoint, cov idêntica]
 │   │   ├─ J6  G1 alargada a .tks
 │   │   └─ J7  G2 observacional
 │   └─ J8  prova por colisão completa                    [ritual]
 │
 ├─ J5  sinais educados (paralelo; só teko_rt.c)          [ritual, collision-free]
 │
 └─ J4  summarize/render + merge falível + cov_missing    [ritual; ESPERA isolamento-C4 drenar]
     └─ J9  --replay + aviso no sweep                     [ritual]

J0  swap capture_probe→run_capturing   [DESIGN-AHEAD: espera `cabi fn` no lexer; ortogonal]
```

**Feito (fora da fila):** journal C0 (captura) landed; journal C4b dissolvido; agregação de cobertura
por fragmento; isolamento-C1 (cura do OOM). **Falta:** J1–J9 (J0 quando `cabi fn` aterrar).
