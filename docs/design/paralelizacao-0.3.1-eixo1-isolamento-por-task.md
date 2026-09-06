---
section: design
created: 2026-08-02
source: ruling do dono ("paralelizar testes e codegen; tem a ver com memória — isolamento de task/arena por unidade paralela"), docs/design/backend-memoria-por-funcao-0.3.1.md (isolação por função — a base), docs/design/concorrencia-adiantada-s8.md §8 (os hazards enumerados), docs/design/journaling-de-corrida-0.3.1.md (segmento-por-escritor + releitura), src/runtime/teko_rt.c (a árvore de singletons)
status: DESENHO — nenhuma linha de produto escrita nesta carga. Eixo 1 (isolamento de teste) é executável hoje sobre a árvore; a per-task-ificação é C mantida (exceção ao congelamento) + wiring .tks.
branch: cargo/0.3.1.0-paralelizacao-arq (de origin/fix/union)
---

# Paralelização — Eixo 1: isolamento REAL de teste por `tk_task`

Arquiteto, 2026-08-02. Documento de DESENHO. `src/runtime/teko_rt.{c,h}` é a EXCEÇÃO explícita ao
congelamento do C (C mantida); todo o resto é `.tks`. Regra do dono honrada: proposta, não
contra-argumento; alarme só com arquivo:linha.

---

## 0. O que já está aterrado (medido na árvore) — e por que muda o problema

A nota histórica em `src/runtime/teko_rt.c:2260-2265` diz, textual:

> *"Parallelism for an in-process suite cannot be threads: the arena, the coverage sinks and this very
> channel are process-wide singletons, and making them thread-local would be a far larger change than
> the parallelism is worth. It is PROCESSES — the driver runs the SAME test binary N times, each with
> `TEKO_TEST_SHARD=<i>/<n>` …"*

**Essa premissa caiu em duas frentes já aterradas.** A campanha F1 moveu TODA a disciplina de memória
para uma `tk_task` (`teko_rt.c:1210-1234`): `regs`, `root`, `free_bins`, `arena_marks`/`arena_msp`,
`cur_regions`/`cur_rsp` (C1 backend-memoria), `push_cache`, a captura de teste (`test_jb`/`test_depth`/
`test_how`/`test_code`) e a pilha de atribuição de cobertura (`fn_stack`/`fn_sp`/`fn_cap`). O acessor
`tk_task_current()` (`teko_rt.c:1240-1245`) é `_Thread_local` — **cada thread já tem a sua task, e a
arena já não vaza entre fluxos.** O que resta process-global é uma lista FINITA e enumerável (§1), e é
exatamente ela que este eixo torna per-task.

**A flakiness que o dono nomeia** — a divisão round-robin em 4 shards reembaralha o ordinal e EXPÕE
vazamento de estado global ENTRE testes — **não é resolvida por process-sharding**: o embaralho só
muda QUAL teste roda depois de qual DENTRO de um processo, e um teste cujo verdito depende de resíduo
deixado por outro passa ou falha conforme o agrupamento. A cura não é "resetar cada singleton" (o
vazamento de HOJE É precisamente um reset esquecido); é **rodar cada `#test` num estado per-task limpo
por construção**, de modo que não exista resíduo a expor.

---

## 1. Os singletons process-wide, com arquivo:linha — as três categorias

Enumeração completa das variáveis de escopo de ficheiro mutáveis em `src/runtime/teko_rt.c`,
classificadas pela ação que cada uma exige.

### 1a. Categoria A — JÁ per-task (FEITO em F1/C1; nenhum trabalho)

Todas em `struct tk_task` (`teko_rt.c:1210-1234`), lidas pelos `#define` de costura (`:1250-1272`):
`regs`, `root`, `free_bins`/`free_large` + contabilidade, `arena_marks`/`arena_msp`,
`cur_regions`/`cur_rsp`, `push_cache`, `test_jb`/`test_depth`/`test_how`/`test_code`,
`fn_stack`/`fn_sp`/`fn_cap`. **A arena e a captura de teste já são hermeticamente per-task.**

### 1b. Categoria B — process-global POR NECESSIDADE (fica; tornar concurrency-safe)

| singleton | arquivo:linha | estado | ação |
|---|---|---|---|
| `tk_g_region_gen` | `teko_rt.c:1275` | contador de geração de região | **JÁ atômico** (`__atomic_add_fetch`, `:1281`); ruling `:1180-1187` fixa que DEVE ser único entre tasks (senão `push_cache` false-hit cruza tasks). Nenhum trabalho. |
| `tk_obs_*` (mapa de tempo de vida da arena) | `teko_rt.c:1356-1387` | agregado de DIAGNÓSTICO, env-gated, off por default | ruling `:1186`: *"needs a lock when tasks actually run concurrently"*. Uma corrida ali custa um histograma errado, nunca um free errado. **Guarda barata: um mutex único em `tk_obs_*`** ligado só quando `tk_obs_on == 1`. |
| `tk_g_program` / `tk_g_program_regs` | `teko_rt.c:1651-1652` | a região de PROGRAMA (F2), dona de nenhuma task, partilhada | init preguiçoso NÃO sincronizado (`tk_region_program`, `:1651` corpo). **Guarda: inicializar EAGER antes do fork** (o pai toca `tk_region_program()` uma vez antes de lançar raias), ou um `once`-guard. |
| `tk_g_names` (slots de nome de DI) | `teko_rt.c:1772` | tabela de `#singleton` na região de programa | §8.9 do concorrencia-doc; ligado a S5 (DI lifetimes). **REPORTADO, não resolvido aqui**: a regra do gate é que nenhum `#test` isolado resolve DI até S5 — e isso é uma CHECAGEM, não uma nota. |

### 1c. Categoria C — process-global, TOCADA por corpo-de-teste/gate, a per-task-ificar (o alvo do Eixo 1)

Estes são os que vazam entre testes sob reembaralho. Cada um migra para `tk_task`, espelhando F1.

| # | singleton | arquivo:linha | família | quem toca |
|---|---|---|---|---|
| C-1 | `tk_intern_table[TK_INTERN_BUCKETS]` | `teko_rt.c:804` (+ `tk_intern_reset` `:854`) | tabela de internamento | builtins `intern_get`/`intern_put`/`intern_reset` (`scope.tks:743-747`); corpos de teste |
| C-2 | `tk_cov_ids` / `tk_cov_n` / `tk_cov_cap` | `teko_rt.c:3366-3368` | sink de cobertura de sítio | `tk_cov_mark`; `tk_cov_reset` `:3369` |
| C-3 | `tk_covb_ids`/`tk_covb_n`/`tk_covb_cap`/`tk_covb_on` | `teko_rt.c:3395-3398` | sink de cobertura de ramo | `cov_branch`; `tk_cov_branch_reset` `:3407` |
| C-4 | `tk_line_ids`/`tk_line_cap`/`tk_line_n`/`tk_lines_on` | `teko_rt.c:3452-3455` | sink de cobertura de linha | `cov_line`; `tk_cov_line_reset` `:3473` |
| C-5 | `_tk_cast_loc_line` / `_tk_cast_loc_col` | `teko_rt.c:2469-2470` (GLOBAL exportado, não-static) | posição de diagnóstico de cast | `tk_to_*` escrevem, `tk_panic_cast` lê (§8.4) |
| C-6 | `tk_test_ran`/`passed`/`failed`/`exited` | `teko_rt.c:2143-2146` | tally de teste | `tk_test_*`; `tk_test_any_failed` `:2256` |
| C-7 | `tk_test_probe_last_code` | `teko_rt.c:2220` | último código sondado | captura de teste |
| C-8 | `tk_scope_buf` / `tk_scope_len` | `teko_rt.c:2311-2312` | rótulo de escopo do teste corrente | `scope()` (`test.tks:53`) |
| C-9 | `tk_scen_name`/`tk_scen_len`/`tk_scen_prefix` | `teko_rt.c:2336-2338` | rótulo de cenário corrente | derivação de path por cenário |
| C-10 | `tk_chan_out`/`tk_chan_err`/`tk_chan_label`/`tk_chan_label_len`/`tk_chan_open` | `teko_rt.c:2036-2040` | CAPTURA de stdout/stderr do teste | `tk_print` sob captura |
| C-11 | `tk_rt_stdin_eof_flag` | `teko_rt.c:2535` | estado de EOF de stdin | leitura de stdin em teste |
| C-12 | `tk_rt_fd_stage`/`tk_rt_fd_staged`/`tk_rt_fd_taken` | `teko_rt.c:3182-3186` | staging de fd (pipe) | io de teste |

**O que NÃO migra:** `tk_shard_index`/`tk_shard_count`/`tk_shard_seen` (`teko_rt.c:2269-2271`) — é a
IDENTIDADE do shard, uma por processo por construção; permanece process-global e correta.

---

## 2. O mecanismo — per-task-ificação centralizada, e o RESET num sítio só

### 2a. Espelhar F1: mover a Categoria C para `tk_task`, com costura por `#define`

Cada campo da Categoria C vira um membro de `struct tk_task` e ganha um `#define` de costura ao lado
dos 11 de F1 (`teko_rt.c:1250-1272`), de modo que **os ~call-sites mantêm o texto exato** — F1 já
provou que isto é uma mudança de ONDE o estado vive, não de quem o toca. As tabelas grandes (intern,
sinks de cobertura) mantêm o seu buffer `malloc`'d através de resets (é o que `tk_cov_reset` já faz:
*"keep the buffer; just forget the marks"*, `:3369`); só o count/marcas zeram.

### 2b. A peça-chave: `tk_task_reset` — UM sítio zera TODO o estado efêmero per-task

O vazamento de hoje é um reset esquecido. A cura estrutural é **centralizar**: uma função

```c
void tk_task_reset(tk_task *t);   // teko_rt.c (C mantida)
```

que zera, num sítio só, TODO o estado efêmero per-task de um teste: as marcas de arena (via o
`tk_arena_pop` até à base), os counts dos sinks de cobertura (C-2/3/4), a tabela de internamento
(C-1), as posições de cast (C-5), o tally e o probe (C-6/7), os rótulos (C-8/9), a captura (C-10) e o
estado de stdin/fd (C-11/12). **Invariante de lei:** adicionar um novo singleton per-task no futuro
obriga a acrescentar UMA linha a `tk_task_reset` — e nenhum call-site pode esquecê-lo, porque há um só.
É a mesma disciplina de "um ponto único" que `bare-name-probe-family.md` prescreve para dependência de
ordem: torná-la impossível por construção, não por vigilância.

### 2c. O gate roda cada `#test` num estado limpo por `tk_task_reset`

O runner do gate chama `tk_task_reset(tk_task_current())` na ENTRADA de cada `#test` (e a captura de
resultado, que já é per-task via `test_jb`, deposita o veredito). Resultado: **cada teste é hermético,
qualquer que seja o shard ou a ordem** — o reembaralho não pode expor resíduo porque não há resíduo.

### 2d. Custo de tamanho, e por que o pool de `lanes` tasks o limita

`sizeof(tk_task)` é dominado por `push_cache` (`TK_PUSH_HASH_SIZE = 1<<16` entradas ≈ vários MB). Uma
task NOVA por teste seria churn de MBs × milhares de testes — inaceitável. Por isso a unidade de
ISOLAMENTO CONCORRENTE é a **raia** (`lanes` ≈ nproc, uma dezena), não o teste: um POOL de `lanes`
tasks reusadas, cada teste corre numa task do pool RESETADA por `tk_task_reset` (o buffer fica, o
estado zera). Cross-lane isolation vem do armazenamento per-task; within-lane hermeticidade vem do
reset centralizado. Custo de memória: `lanes × sizeof(tk_task)` — uma dezena de tasks, não milhares.

---

## 3. O fold determinístico — modelo de journal (releitura, não junção)

Paralelizar não pode mudar o stdout. A regra (concorrencia-doc §6.1, journaling-doc §1) é
inegociável e já ratificada:

1. **Escrita disjunta.** Raia `k` escreve APENAS no segmento do seu próprio índice (o journal
   segmentado por escritor). Sem acumulador partilhado.
2. **Sumarização por RELEITURA, não por junção.** Cobertura → cada raia despeja o seu `.tkcov`
   (o segmento já isolado pela journaling-doc); o pai FUNDE por releitura (`merge_shard_coverage`,
   `project.tks:3763` já é essa forma). Tally (C-6) → per-task, o pai relê e soma após a barreira.
3. **Saída só no pai, em ordem de ÍNDICE, depois da barreira.** Nenhuma raia chama `print`. O rótulo
   `test <ns::name> …` sai COM o veredito, no pai, em ordem `0..count`. **A saída é byte-idêntica com
   1 raia e com N** — a ordem de impressão não é a ordem de execução.

**Teste = STDOUT, nunca exit.** Nenhuma fixture deste eixo asserta exit-code; o veredito vai pelo
stdout do pai. É o que torna a atribuição imune ao paralelismo: pela casa, nunca pelo fluxo.

---

## 4. Assinaturas Teko/C que o implementador adiciona (full Javadoc — copiar verbatim)

```teko
/**
 * task_reset — devolve a task corrente ao estado efêmero-limpo de "antes de qualquer teste":
 * zera, num sítio só, TODO o estado per-task que um `#test` pode sujar — marcas de arena, counts
 * dos sinks de cobertura, tabela de internamento, posições de diagnóstico de cast, tally, rótulos
 * de escopo/cenário, captura de stdout/stderr e estado de stdin/fd. O buffer `malloc`'d de cada
 * tabela é PRESERVADO (só o count/marcas zeram), para que o reset não seja uma realocação.
 *
 * É o ponto ÚNICO de reset: um novo singleton per-task adicionado no futuro é resetado aqui e em
 * lugar nenhum mais, de modo que nenhum call-site possa esquecê-lo — a invariante que torna o
 * vazamento de estado entre testes impossível por construção, não por vigilância.
 *
 * @return void
 * @since 0.3.1
 */
extern fn task_reset() = "tk_task_reset" from "teko_rt"
```

O binding `tk_task_reset(tk_task*)` recebe a task; o wrapper Teko (`task_reset`) passa
`tk_task_current()` internamente (mesma costura de `tk_arena_push`). Builtin em `src/checker/scope.tks`
(espelhar `arena_push`, `:740-742`); mapeamento em `src/lir/lower.tks` (espelhar `:3981-3982`, ao lado
das entradas de `arena_*`).

---

## 5. Fixtures de regressão (input → verdito por STDOUT; NUNCA exit)

Todas afirmam por **stdout comparado** (a lei "teste = stdout"), sob gen2 nativo.

| fixture | forma | verdito esperado (stdout) |
|---|---|---|
| `iso_intern_no_leak` | teste A interna `"x"`; teste B (que NÃO interna) lê `intern_get("x")` e imprime miss/hit | B imprime MISS nas duas ordens (A→B e B→A); diff vazio entre ordens |
| `iso_cast_loc_no_cross` | dois testes falham um cast em linhas diferentes; captura a posição reportada | cada teste nomeia a SUA linha, nunca a do outro, em qualquer agrupamento de shard |
| `iso_cov_lanes_invariant` | mesma suíte com `lanes=1`, `4`, `16`; relatório de cobertura comparado | idêntico nas três (releitura por casa) |
| `iso_stdout_lanes_invariant` | mesma suíte com `lanes=1`, `4`, `16`; stdout comparado por diff | byte-idêntico nas três |
| `iso_reverse_and_shuffle` | suíte serial em ordem invertida e embaralhada com semente fixa | mesmo conjunto de veriditos; qualquer diferença = dependência real (o achado útil) |
| `iso_scope_label_no_cross` | dois testes com `scope()` distintos derivam paths; concorrentes | cada path deriva do SEU escopo; sem cruzamento |
| `iso_capture_no_cross` | dois testes capturam stdout; a raia A não vê o que B imprimiu | captura de cada teste contém só o seu próprio output |

`iso_reverse_and_shuffle` PODE falhar hoje — e falhar é o resultado: mede a dependência que o sort de
fontes mascara. Escrever mesmo que passe (disciplina do PONTO ABERTO de `bare-name-probe-family.md`).

---

## 6. Crumbs ordenados (com colisões e rituais)

Ponto ritual = gate completo: gen2 nativo + `teko test .` verde + FIXPOINT gen2==gen3 + stdout dos
`iso_*_lanes_invariant` byte-idêntico entre `lanes`.

| # | crumb | entrega | colisão | ritual |
|---|---|---|---|---|
| **E1-C0** | **[FEITO]** commit vazio + push | proteção contra restart | — | não |
| **E1-C1** | **Mover Categoria C para `tk_task`** (`teko_rt.c/h`, C mantida) — campos + `#define` de costura ao lado dos 11 de F1 | isolamento cross-lane por armazenamento per-task; comportamento single-task byte-idêntico (topo/task única inalterada) | `teko_rt.{c,h}` (C mantida, exceção); ADITIVO | **sim** (é mudança de disciplina de memória) |
| **E1-C2** | **`tk_task_reset` + builtin `task_reset`** | o reset centralizado num sítio; builtin `scope.tks` + mapeamento `lower.tks` (ADITIVO, ao lado de `arena_*`) | `lower.tks`/`scope.tks` (quentes) — edição de duas linhas, aditiva | **sim** |
| **E1-C3** | **Gate roda cada `#test` sob `task_reset`** | hermeticidade por teste; mata o vazamento reembaralho-exposto na raiz | `src/build/project.tks` (o runner do gate, `native_gate_run:3716`) | **sim** |
| **E1-C4** | **Concurrency-safe da Categoria B** | mutex único em `tk_obs_*` (só sob `tk_obs_on`); init EAGER de `tk_region_program` antes do fork; checagem "`#test` isolado não resolve DI" (§1b, ligado a S5) | `teko_rt.c` (C mantida); `project.tks` | **sim** |
| **E1-C5** | **Fold por journal + saída só no pai** | cobertura/tally por casa (per-task), releitura pelo pai; raia não imprime; rótulo com veredito em ordem de índice | `project.tks` (runner); coordena com journaling-doc | **sim** |
| **E1-C6** | **nproc: default OS-granted, cap OS max** | `TEST_JOBS_DEFAULT`/`REGR_JOBS_DEFAULT` deixam o literal `4` (`project.tks:3783`) e passam a `hardware_parallelism()` capado ao OS max; env override clampa a `[1, os_max]` | `project.tks` | **sim** |

**Dependência de raia em thread real:** E1-C3/C5 em MODO THREAD dependem das primitivas S8
(`fork_join`/`isolate`, DESIGN-only — §7). **Mas E1-C1/C2/C4 e o modo PROCESS-shard de E1-C3/C5 são
executáveis HOJE** e já entregam o valor central: a per-task-ificação + o reset centralizado matam o
vazamento reembaralho-exposto MESMO sob process-sharding (cada teste roda hermético). O modo thread é
um backing sob a mesma superfície quando S8 fechar.

---

## 7. O que continua BLOQUEADO, explicitamente (DESIGN-AHEAD)

- **Modo THREAD in-process** depende de `fork_join`/`isolate`/`thread::sys`/`cabi fn`
  (`concorrencia-adiantada-s8.md` §3, `journaling-de-corrida-0.3.1.md`) — **DESIGN-only, não
  aterrado** (`grep fork_join src = vazio`). Este eixo entrega tudo o que NÃO depende disso: a
  per-task-ificação (C mantida), o `tk_task_reset`, o wiring do gate sob process-shard, e as fixtures.
  Quando as primitivas fecharem, trocar o backing de process→thread é a mesma superfície.
- **DI per-task (`tk_g_names`, C-1b)** herda o problema de S5 (DI lifetimes → arenas, ⬜). Aqui só a
  CHECAGEM "`#test` isolado não resolve DI". REPORTADO, não convertido em issue.

---

## 8. Riscos e tensões de lei — com resolução

| risco / tensão | resolução (law-first) |
|---|---|
| **R1 — reset esquecido reaparece** (a causa do vazamento de hoje) | `tk_task_reset` é o ponto ÚNICO; a lei do doc-comment obriga a nova per-task-ificação a passar por ele. Impossível por construção. |
| **R2 — tamanho da task × lanes** | pool de `lanes`≈nproc tasks reusadas; buffer preservado no reset. Custo `lanes × sizeof(tk_task)`, uma dezena, não milhares. |
| **R3 — toca C congelada** | `teko_rt.{c,h}` é a exceção explícita (C mantida); as migrações espelham F1, aditivas, single-task byte-idêntico. Sem tensão. |
| **R4 — stdout muda sob paralelismo** | raia não imprime; pai imprime em ordem de índice após barreira; `iso_stdout_lanes_invariant` é o detector. Teste = stdout, nunca exit. |
| **R5 — Categoria B corre** | `tk_g_region_gen` já atômico; `tk_obs_*` com lock só sob observação; programa init eager. Nenhuma corre um free. |
| **R6 — modo thread bloqueado** | DESIGN-AHEAD: tudo o que não depende de S8 entregue hoje; process-shard já colhe o valor central; thread é backing futuro. |

**Nenhuma tensão de lei genuína permanece.** Sem HALT. O único item herdado é S5 (DI per-task),
REPORTADO.
