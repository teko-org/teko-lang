---
section: design
created: 2026-08-02
branch: cargo/0.3.1.0-modelo-build-arq (de origin/fix/union)
owner-diagnostic: 2026-08-02 ("o build e SEQUENCIAL; ha tempo escuro; o delivery e MAIOR que a soma dos lead times")
owner-precision: 2026-08-02 ("o wall entre INICIO e FIM do COMANDO e MAIOR que o tempo que o compilador registra de si mesmo")
owner-correction: 2026-08-02 ("nao e wall == soma das fases, e sim wall <= soma das fases, porque queremos PARALELIZAR; travar em igualdade agora falhara depois")
status: DESENHO (arquiteto so escreve docs). Nenhuma linha de produto nesta carga.
depende-de: docs/design/journaling-de-corrida-0.3.1.md (JOURNAL, outro agente), docs/design/build-observability-plan.md (fases stderr), docs/design/al-wave-emit-throughput.md (throughput do emissor)
---

# O modelo de build 0.3.1 — o tempo escuro e a paralelizacao

> *"Nosso modelo de build hoje e SEQUENCIAL. Ha muito tempo que NAO e registrado; quando soma os
> tempos e compara com o elapsed, NAO BATEM — o delivery e MAIOR que a soma dos lead times."* — dono
>
> *"Se marcar o tempo entre INICIO e FIM do COMANDO de build, este e MAIOR que o tempo registrado
> pelo compilador."* — dono, precisao 2026-08-02

Este documento e PLANO. Todo snippet ja vem em estilo W15 (Javadoc em cada declaracao, funcoes
achatadas). O implementador copia verbatim. Regra do dono seguida: **proposta, nao contra-argumento;
alarme so com arquivo:linha.**

A tese, numa frase: **o relogio de fase que existe hoje e honesto mas cobre menos de metade do
caminho — o backend nativo, o `cc`/link externo e o ciclo de vida do processo estao TODOS fora de
qualquer fase — e por isso o wall externo do comando e sempre maior que a soma das linhas que o
compilador imprime.** A lei que fecha isso e o pre-requisito do build paralelo.

---

## 0. A base de medicao e a lei, antes de tudo

A precisao do dono move a linha de base: **a verdade e o wall EXTERNO do comando** (`time ./teko
build <dir>`), nao o start-to-finish interno do compilador. Tudo abaixo compara esse wall externo
com o que o compilador reporta de si.

**A LEI (forma correta — uma DESIGUALDADE, correcao do dono 2026-08-02):**

```
wall_externo(comando)  <=  Σ fases_nomeadas          (incluindo a fase "process" de ciclo de vida)
```

Nao e igualdade, e **`<=`**, porque queremos PARALELIZAR — travar em `==` agora falha no dia das
threads. Leia a lei nos dois regimes:

- **SEQUENCIAL (sem sobreposicao):** vale com IGUALDADE. Cada fase acontece em seu proprio pedaco do
  relogio; a soma dos lead times E o wall. E o regime de HOJE.
- **PARALELO (fases sobrepostas):** vale com `<` ESTRITO. Trabalho concorrente conta uma vez na SOMA
  (dois lead times) mas acontece no MESMO relogio (um so wall), logo `Σfases > wall`. A soma exceder o
  wall e o **sinal de que houve paralelismo** — e sucesso, nao defeito.

Duas quantidades derivam da lei, e cada uma tem um significado fixo:

```
dark    = max(0, wall − Σfases)     -- tempo NAO-ATRIBUIDO (wall fora de toda fase). DEVE ser 0.
overlap = max(0, Σfases − wall)     -- GANHO DE PARALELISMO (lead times concorrentes). >= 0, informativo.
```

- **`dark` e o unico defeito.** `dark > 0` significa que existe wall que nenhuma fase nomeia — o tempo
  escuro do dono. A lei exige `dark == 0`: zero tempo sem nome, inclusive o ciclo de vida (que vira a
  fase NOMEADA `process`, nao um buraco).
- **`overlap` NAO e defeito — e a metrica de speedup.** Sob threads ele cresce; isso e o que estamos a
  perseguir. Um `ledger_reconcile` que falhasse quando `Σfases > wall` (a antiga regra "balde negativo
  = defeito") e PRECISAMENTE o que quebraria no dia da paralelizacao — por isso foi removido.

Quando o dono ou o CI marcam `time`, o veredicto e: **`dark == 0`** (todo o wall externo esta coberto
por alguma fase, dentro do erro de resolucao do relogio monotonico) e **`overlap`** reportado como o
ganho medido — nunca `wall == Σ`.

---

## 1. ONDE o tempo se perde — provas com arquivo:linha

### 1.1 O relogio de fase EXISTE e e honesto — mas so no caminho C do front+backend

`src/build/progress.tks` tem a maquina completa: `Phase{label,start,mode}` (`progress.tks:85`),
`now_ns()` monotonico (`progress.tks:103`, le `teko::time::span_monotonic_now().ticks`),
`phase_begin`/`phase_end_ok`/`phase_end_fail` (`progress.tks:243/275/291`), e `elapsed_str`
(`progress.tks:157`) que renderiza `(now_ns()-start)` em decimos de segundo. **Nada errado com o
relogio.** O problema e ONDE ele e (nao) instalado.

Instrumentado hoje (caminho C):
- `lexer`/`parser` — `assemble.tks:216-217`;
- `checker`/`monomorph`/`consteval` — `project.tks:328/350/362`;
- `codegen`/`emit C`/`cc` — `project.tks:1860/1875/1884`;
- gate: `emit test`/`cc test` — `project.tks:3552/3563`; regr-cov: `emit rcov`/`cc rcov` —
  `project.tks:5530/5542`; `recheck` — `project.tks:5825`.

Essa e a lista COMPLETA de `phase_begin` do build. Note o que NAO esta nela: o backend nativo
inteiro, o `link` externo, e o ciclo de vida.

### 1.2 O backend NATIVO e integralmente mudo — zero `phase_begin`

`emit_native` (`project.tks:2557`) faz `lower_program(prog)` (`project.tks:2560`) e despacha para as
caudas. NENHUMA cauda tem fase:

- `emit_native_x86` (`project.tks:2640`) encadeia `select_module_x86` (2642) → `regalloc_module_x86`
  (2643) → `encode_module_x86` (2644) → `emit_elf` → `finish_native_object` — **sem um unico
  `phase_begin`**;
- idem `emit_native_win` (`project.tks:2672`), `emit_native_arm64` (`project.tks:2587`),
  `emit_native_arm64_linux` (`project.tks:2612`);
- `finish_native_object` (`project.tks:2729`) faz `write_file_bytes(.o)` (2733), `link_object` (2735)
  e escreve `.tsym` (2738) — **tambem sem fase.**

Ou seja, as seis fases que o dono nomeia (`lower → isel → regalloc → encode → objfile → link`) **nao
tem relogio no caminho de producao.** O `build-observability-plan.md` §B5 ja previa a paridade
native; este documento a torna a LEI, nao um opcional.

### 1.3 A reconciliacao do "0.0s cada" com o wall de 10.478s — e a mesma coisa que a precisao do dono

Na sonda de 2000 instrucoes, `lower/isel/regalloc/encode/objfile/link` reportaram ~0.0s e o processo
levou 10.478s. Leitura arquitetural honesta (nao consegui rodar; deduzo do codigo):

Para um programa de 2000 instrucoes de maquina, `select/regalloc/encode` sao GENUINAMENTE ~0.0s — sao
transformacoes por-funcao de um modulo minusculo. O wall de 10.478s **nao esta nas seis fases** — ele
esta em DOIS lugares que nenhuma delas cerca:

1. **O `cc`/link EXTERNO.** `finish_native_object` chama `link_object` (`project.tks:2735`) que
   spawna um `cc`-como-linker (`teko::process::run`) para ligar o `.o` contra o runtime C
   (`teko_rt`). Um `cc` que compila/linka o runtime + linka o objeto custa segundos, e a fase `link`
   reportou 0.0s **porque a chamada nao esta dentro de nenhum `phase_begin`** — o wall do filho `cc`
   nao e atribuido a fase alguma.
2. **O ciclo de vida do processo** (§1.5).

Isto e EXATAMENTE a precisao do dono: o wall externo (10.478s) e maior que a soma auto-reportada
(~0.0s) porque o custo real vive FORA do que o compilador conta — no filho `cc` e no startup/teardown.
A sonda nao mediu errado; ela mediu as fases certas, que para um programa minusculo sao ~0, e deixou
escuro o que domina. **Crumb 1 (§4) poe uma fase nomeada em volta do `link_object` e o escuro some.**

(Para o self-host de 17.7 MB o quadro muda: ai o EMISSOR domina — 88 KB/s, tempestade de copy-grow
superlinear, ja provado em `al-wave-emit-throughput.md` / `build-observability-plan.md` §AL — e esse
tempo cai dentro de `encode`/`objfile`, que hoje tambem sao mudos no native. Os dois diagnosticos
compoem: instrumentar (este doc) revela ONDE; o AL-wave conserta o emissor. Sao ortogonais.)

### 1.4 O `cc`/link externo: o wall do FILHO nao e atribuido — no caminho C tambem

Nao e so no native. No caminho C, `cc` e uma fase (`project.tks:1884`), mas o que ela mede e o
`teko::process::run` de um `cc` que compila um `.c` multi-MB — o wall do filho INTEIRO cai num unico
settle no fim, sem START-line ate a fase assentar (a raiz R1 de `build-observability-plan.md` §A: em
nao-TTY `phase_begin` nao emite START, so o SETTLE aparece, DEPOIS do filho terminar). O tempo do
`cc` esta contado, mas so aparece no fim; e no native nem contado esta.

`regr_timing.tks:21-23` ja documenta a fronteira exata deste buraco, na fase de regressao:
> *"`compile` — a invocacao do compilador, INCLUINDO o `cc` que ele spawna (compartilham um processo
> filho, logo nao ha split barato aqui; separa-los exige o compilador reportar seu proprio tempo de
> `cc`, o que e mudanca de compilador, nao de runner)."*

Traduzindo: o wall do neto `cc` (o `cc` que o `teko`-filho spawna) e visivel so como parte do
`compile` do filho. Para separa-lo, o COMPILADOR precisa emitir seu proprio tempo de `cc` — que e
precisamente a fase `link`/`cc` nomeada que a LEI exige.

### 1.5 O balde escuro do CICLO DE VIDA — antes do 1o relogio e depois do ultimo

O primeiro `now_ns()` do build so e chamado quando a primeira fase comeca (`lexer`,
`assemble.tks:216`), que ja e DEPOIS de: `exec` do processo, o loader dinamico, o init de runtime +
arena (`teko_rt`), `teko::env::args()` (`main.tks:37`), o dispatch de subcomando
(`main.tks:38-99`), o parse de flags (`out_dir_of`/`project_arg_of`/`opt_level_of`/`no_tty_of`/
`backend_of`, `main.tks:75-92`), `project_dir_of` e o `manifest`/`discover` (que rodam ANTES do
`assemble_sel`). O ultimo `phase_end` assenta e depois vem: flush de stdout/stderr, teardown de arena
(`tk_regions_free_all`), `atexit`, e o `_exit`. **Nada disso esta em fase nenhuma.** E `main.tks` nao
tem nenhum relogio de topo — nao existe um `time_command_start` no `exec` nem um total no `exit`.

**Nao existe TOTAL auto-reportado.** Cada fase assenta sua propria linha; ninguem soma. A "soma" que o
dono compara com o `time` e feita a OLHO, somando as linhas impressas — e por construcao ela exclui o
native mudo, o link externo e o balde de ciclo de vida. Dai `delivery > Σ lead_times`, sempre.

### 1.6 lexer e parser: DUAS fases, UM intervalo (dupla contagem)

`assemble.tks:216-217` abre `lex_phase` E `parse_phase` no MESMO instante, antes de qualquer trabalho;
`asm_lex_and_parse` (`assemble.tks:235`) faz lex+parse JUNTOS por arquivo; ambas assentam no mesmo
ponto (`assemble.tks:263-264`). Consequencia: a linha "lexer" e a linha "parser" reportam o MESMO
intervalo (lex+parse+merge). **Somar as duas conta o front-parse em DOBRO** — e cria um `overlap`
FANTASMA (§0): `Σfases − wall` cresce sem que exista concorrencia nenhuma, apenas trabalho sequencial
instrumentado por dois relogios sobrepostos. Isso CORROMPE a metrica de speedup — `overlap` deve medir
paralelismo REAL, nao dupla-instrumentacao. A LEI corrigida exige que `overlap` so venha de concorrencia
verdadeira: aqui a cura e uma fase `parse` UNICA (ou dois relogios sequenciais reais), para que
sequencial de `overlap == 0` como manda o regime sem sobreposicao.

### 1.7 A atribuicao mentirosa por `.tkr`: a primeira fila paga tudo

Provado em `regression.tks:2474-2478`:
```
let build_times = if art.fresh {
    PhaseTimes { harness_ns = art.comp.harness_ns; compile_ns = art.comp.child_ns; run_ns = 0; builds = 1 }
} else {
    phase_zero()
}
```
A primeira fila que alcanca o slot de cache (`fresh=true`) recebe `compile_ns = art.comp.child_ns` — o
wall INTEIRO do build-filho (`child_ns = t_waited - t_spawn`, `regression.tks:211`). Toda fila
seguinte reusa o artefato (`tkr_standalone_artifact` retorna `fresh=false`, `regression.tks:2121`;
`tkr_ensure_built` no-op quando `done`, `regression.tks:2178`) e recebe `phase_zero()` — **aparece de
graca.** O cache esta CORRETO (nao rebuildar e o certo); a MEDICAO e que mente: atribui um build
compartilhado por N cenarios inteiramente ao primeiro. A honestidade e distribuir/nomear o custo
compartilhado, nao escondê-lo no cenario 0.

### 1.8 Sumario das fontes de descompasso (o mapa do escuro)

| # | buraco | arquivo:linha | classe |
|---:|---|---|---|
| A | backend nativo sem fase | `project.tks:2640-2645,2672-2677,2587,2612,2729-2741` | fase ausente |
| B | `link_object`/`cc` externo nao cercado por fase (native) | `project.tks:2735` | wall de filho nao atribuido |
| C | `cc` do caminho C so aparece no settle (sem START) | `project.tks:1884`; raiz R1 `progress.tks` | START tarde demais |
| D | ciclo de vida (startup antes do 1o relogio) | `main.tks:37-92`; 1o relogio `assemble.tks:216` | relogio comeca tarde |
| E | teardown/flush/exit (depois do ultimo relogio) | `main.tks` (sem total no exit) | relogio para cedo |
| F | sem TOTAL auto-reportado | `project.tks` (nenhum agregador) | soma so a olho |
| G | lexer+parser dupla contagem | `assemble.tks:216-217,263-264` | fases sobrepostas |
| H | `.tkr`: 1a fila paga o build todo | `regression.tks:2474-2478,211,2121,2178` | atribuicao mentirosa |
| I | neto `cc` da regressao dentro de `compile` | `regr_timing.tks:21-23` | filho nao separado |

---

## 2. A LEI em codigo: o razao de fases e a desigualdade que sobrevive ao paralelismo

### 2.1 A equacao (uma desigualdade), e o balde nomeado

```
wall_externo  <=  fase(process.startup)
                + Σ fases_de_trabalho   (parse, load-deps, checker, monomorph, consteval,
                                         lower, isel, regalloc, encode, objfile, link, ...)
                + fase(process.teardown)
```

`process.startup` e `process.teardown` sao o balde de ciclo de vida NOMEADO — a fase `process`. Nao ha
residuo anonimo: todo o wall externo esta coberto por ALGUMA fase, logo `dark == 0`. A folga
`overlap = Σfases − wall` e `>= 0` e cresce com o paralelismo (§0); HOJE, sequencial, ela e ~0 e a
desigualdade colapsa em igualdade. O implementador NAO deve travar em `==`.

### 2.2 O relogio de topo: cercar o comando inteiro em `main.tks`

O unico ponto que ve o wall externo real e o `main`. Marca-se `now_ns()` na PRIMEIRA linha e um total
no `exit`. A diferenca entre esse wall e a soma das fases de trabalho E o balde de ciclo de vida — e
ele passa a ser IMPRESSO, nao inferido.

```teko
/**
 * BuildClock — o relogio de topo do COMANDO, o unico que ve o wall externo real.
 *
 * O `start` e marcado na primeira instrucao de `main` (o mais cedo que Teko pode observar; o custo de
 * `exec`+loader ANTES disso e inatingivel de dentro do processo e fica na conta do CI que roda `time`,
 * nomeado como `process.exec` na tabela de reconciliacao). O wall de topo (`now_ns()` de fecho − `start`)
 * e o `wall_externo` da LEI (§2.1): ele tem de estar INTEIRAMENTE coberto por fases (`dark == 0`), com
 * o balde `process` (startup Teko-observavel + teardown) nomeando a fatia que nao e trabalho — impresso,
 * nunca inferido. Sob paralelismo a soma das fases EXCEDE este wall (`overlap`), e isso e esperado.
 *
 * @field start  leitura monotonica na primeira instrucao de main (ns)
 * @since 0.3.1
 */
pub type BuildClock = struct {
    start: i64
}

/**
 * build_clock_start — abrir o relogio de topo. Chamado como PRIMEIRA instrucao util de `main`, antes
 * de `teko::env::args()`, para que o parse de flags e o dispatch de subcomando caiam DENTRO do balde
 * `process.startup` em vez de escaparem para o escuro.
 *
 * @return o relogio aberto
 * @since 0.3.1
 */
pub fn build_clock_start(): BuildClock {
    BuildClock { start = teko::build::now_ns_pub() }
}
```

### 2.3 O razao de fases: um registro append-only que alimenta o JOURNAL

A LEI so fecha se CADA fase depositar seu par (label, ns) num razao unico, e o total for a soma desse
razao mais o balde. O razao e o mesmo `fold` do JOURNAL (`journaling-de-corrida-0.3.1.md`): uma fase e
um `Record{kind="phase", payload="<label> <ns>"}`. Zero canalizacao nova — o JOURNAL ja e o sumidouro
duravel; timing de fase e mais uma especie de registro.

```teko
/**
 * PhaseLedger — o razao das fases de UMA corrida de build: pares (label, ns) em ordem de assentamento.
 *
 * A INVARIANTE E A LEI (DESIGUALDADE, §2.1): `wall_externo <= ledger_total_ns(l)`, com a fase `process`
 * ja no razao. O UNICO defeito e `dark = max(0, wall − Σfases) > 0` — wall que nenhuma fase nomeia (o
 * tempo escuro). A folga `overlap = max(0, Σfases − wall)` NAO e defeito: e o ganho de paralelismo, `>= 0`
 * e informativo. `ledger_reconcile` afirma `dark == 0` e devolve `overlap`; as fixtures F1/F2 o forcam.
 *
 * @field labels  o nome de cada fase, em ordem de assentamento
 * @field ns      a duracao de cada fase (paralela a `labels`), em ns monotonicos
 * @since 0.3.1
 */
pub type PhaseLedger = struct {
    labels: []str
    ns: []i64
}

/**
 * ledger_record — anexar uma fase assentada ao razao. Chamado por `phase_end_ok`/`phase_end_fail`
 * (progress.tks), de modo que instrumentar uma fase e depositar no razao sejam UMA acao, nao duas que
 * se podem esquecer uma da outra.
 *
 * @param l      o razao corrente
 * @param label  o nome da fase que acaba de assentar
 * @param ns     sua duracao medida (`now_ns() - phase.start`)
 * @return       o razao com a fase anexada
 * @since 0.3.1
 */
pub fn ledger_record(l: PhaseLedger, label: str, ns: i64): PhaseLedger {
    PhaseLedger {
        labels = teko::list::push(l.labels, label)
        ns = teko::list::push(l.ns, ns)
    }
}

/**
 * ledger_total_ns — a soma de todas as fases do razao.
 *
 * @param l  o razao
 * @return   a soma das duracoes, em ns
 * @since 0.3.1
 */
pub fn ledger_total_ns(l: PhaseLedger): i64 {
    mut acc: i64 = 0
    mut i: u64 = 0
    loop {
        if i >= l.ns.len { break }
        acc = acc + l.ns[i]
        i = i + 1
    }
    acc
}

/**
 * Reconciliation — o veredicto da LEI decomposto nas suas duas quantidades de sinal fixo (§0):
 * `dark` (wall nao-atribuido, DEVE ser 0) e `overlap` (ganho de paralelismo, `>= 0`, informativo).
 *
 * @field dark     `max(0, wall − Σfases)` — tempo escuro; o defeito a eliminar.
 * @field overlap  `max(0, Σfases − wall)` — sobreposicao de fases concorrentes; o speedup medido.
 * @since 0.3.1
 */
pub type Reconciliation = struct {
    dark: i64
    overlap: i64
}

/**
 * ledger_reconcile — o veredicto da LEI (§2.1), que e uma DESIGUALDADE e nao uma igualdade. Devolve a
 * decomposicao `dark`/`overlap` do wall de topo contra a soma das fases. Falha SOMENTE quando ha tempo
 * escuro (`dark > 0` — wall que nenhuma fase nomeia); NUNCA falha quando a soma das fases excede o wall
 * (`Σfases > wall`), porque isso e o GANHO DE PARALELISMO e travar nele quebraria no dia das threads —
 * era exatamente a regra "balde negativo = defeito" da versao anterior, agora REMOVIDA. Sequencial:
 * `dark == 0` e `overlap == 0`. Paralelo: `dark == 0` e `overlap > 0`.
 *
 * @param l     o razao das fases (a fase `process` de ciclo de vida ja incluida)
 * @param wall  o wall externo (`now_ns() de fecho − BuildClock.start`)
 * @return      a decomposicao com `dark == 0`, ou um error quando ha tempo escuro (`dark > 0`)
 * @since 0.3.1
 */
pub fn ledger_reconcile(l: PhaseLedger, wall: i64): Reconciliation | error {
    let total = ledger_total_ns(l)
    let dark = if wall > total { wall - total } else { 0 }
    if dark > 0 { return error { message = $"teko: {dark}ns of wall belongs to no phase (dark time)" } }
    Reconciliation { dark = 0; overlap = total - wall }
}
```

### 2.4 Ligacao ao JOURNAL (o outro agente implementa o sumidouro)

O timing de fase alimenta o MESMO journal: em `phase_end_*` (progress.tks), alem do razao em memoria,
emite-se `teko::journal::append(j, "phase", $"{label} {ns}")`. O sumario final
(`journaling-de-corrida-0.3.1.md` §13) passa a incluir uma seccao de timing lida por `fold` — o mesmo
mecanismo, sem acumulador novo. **Contrato contra a forma DECLARADA do journal** (`Journal`,
`append`, `fold`, `Record`): se o journal ainda nao aterrou, o razao em memoria (§2.3) funciona
sozinho e o `append` e um ponto de fiacao de UMA linha quando o dep fechar. Isto e a parte
DESIGN-AHEAD: nao depende da API bloqueada.

---

## 3. O BUILD PARALELO

### 3.1 Medir antes de paralelizar — o item 2 e pre-requisito DURO

Nao se paraleliza o que nao se mede. Enquanto o razao (§2) nao fechar `dark == 0` (todo o wall
atribuido a alguma fase), nao se sabe se o gargalo e codegen (paralelizavel por-funcao), o `cc`/link
externo (barreira serial), ou o ciclo de vida (nao-paralelizavel). E `overlap` so vira metrica de
speedup confiavel DEPOIS que a dupla-contagem fantasma (§1.6) e curada — senao ele mede
instrumentacao, nao concorrencia. O `regressor.tkr` ja impoe essa disciplina ao numero de builds
(*"numero de build se MEDE na lane fail-closed, nao se deriva por adicao"*). Aplica-se identica aqui.

### 3.2 O que E paralelizavel (por camada)

1. **Por-FUNCAO no backend nativo — embarracosamente paralelo, mas exige threads.** O dado ja e
   independente por construcao: `lower_program` produz um `LModule` de `LFunction` independentes;
   `select_module_x86` (`project.tks:2642`), `regalloc_module_x86` (2643) e `encode_module_x86` (2644)
   sao mapas por-funcao — cada funcao vira `MInst` sem olhar as outras. `isel/regalloc/encode` sao o
   caso de livro de `fork_join` (`concorrencia-adiantada-s8.md` §3.2: atribuicao estatica de raia,
   item `i` na raia `i%lanes`, sem escrita compartilhada, junta depois da barreira). **BLOQUEADO por
   ausencia de threads em Teko** (§3.5) — desenho pronto, fiacao adiada.
2. **Por-PROJETO / por-`.tkr` — paralelizavel HOJE via `run_pool`.** `run_pool` (`regression.tks`,
   ja em producao) roda filhos `teko` concorrentes com colheita do mais-antigo-em-voo e wall REAL por
   filho (`harvest_spec`, `regression.tks:201-214`). A fase de regressao e projetos independentes sao
   spawn de processos — nao precisam de threads. `journaling-de-corrida-0.3.1.md` §9 ja destranca isto:
   enraizar o rascunho por escritor remove o acoplamento do `REGR_WORK_DIR` unico.
3. **Por-SHARD do gate unitario — ja paralelo.** `run_gate_sharded` (`project.tks:3723`) roda `jobs`
   shards concorrentes do binario de teste (`test_jobs()`, `project.tks:3674`). Precedente vivo.
4. **Os dois `cc` (release + gate) em paralelo** — desejavel, mas `teko::process::run` e SINCRONO
   (`build-observability-plan.md` §B4/§5): sem spawn assincrono com poll, nao da. BLOQUEADO.

### 3.3 As BARREIRAS (o que NAO paraleliza, por lei)

- **O link.** `link_object`/`run_cc` sao um passo unico contra o runtime; a imagem final e serial. E o
  join point.
- **A tabela de simbolos global e o layout de secoes.** `emit_elf`/`emit_coff` montam UM objeto com a
  tabela de simbolos e o layout de secoes globais — depende de TODAS as funcoes ja encodadas. E a
  barreira natural apos o map por-funcao (§3.2.1): funcoes em paralelo, montagem do objeto em serie.
- **A ORDEM deterministica do FIXPOINT.** O binario de release tem de ser byte-identico entre gen1 e
  gen2. Qualquer paralelizacao tem de produzir a MESMA ordem de emissao — dai a atribuicao de raia ser
  ESTATICA e a juncao REORDENAR para a ordem de item antes de emitir (o mesmo cuidado de ordem que
  refutou a abordagem (b) do Gap 2 em `build-observability-plan.md`). A barreira preserva o fixpoint;
  o paralelismo vive so ANTES dela.

### 3.4 O modelo de concorrencia (o dono ja fixou)

Default = o que o SO concede; o dev DEVE poder fixar o numero de threads. Precedente: `test_jobs()`
(`project.tks:3674`) le `TEKO_TEST_JOBS`, com parser compartilhado `regr_jobs_of_default`
(`regression.tks:269`: vazio/nao-numerico/zero → fallback). **Mas o default de hoje e um `4`
HARDCODED** (`TEST_JOBS_DEFAULT`, `project.tks:3658`), NAO "o que o SO concede". E nao existe primitivo
de contagem de CPU (grep de `nproc`/`_SC_NPROCESSORS`/`available_parallelism` = vazio na arvore).

Proposta (DESIGN-AHEAD, seed C MANTIDO — `teko_rt.{c,h}` e excecao explicita ao congelamento):

```teko
/**
 * build_jobs — quantas raias de trabalho paralelo esta corrida pode ter em voo.
 *
 * O DEFAULT E O QUE O SO CONCEDE (`teko::env::nproc()`), nao um numero cravado — o dono fixou "o SO
 * concede por default, o dev PODE fixar". Um `TEKO_JOBS` explicito vence (12-Factor), com a mesma
 * gramatica de `test_jobs`: vazio/nao-numerico/zero → o default do SO. Uma casa de 1 CPU, ou um SO que
 * nao sabe contar, cai em 1 — nunca em 0, que seria uma barreira disfarcada de paralelismo.
 *
 * @return o numero efetivo de raias (>= 1)
 * @since 0.3.1
 */
fn build_jobs(): u64 {
    let dflt = teko::env::nproc()
    match teko::env::var("TEKO_JOBS") { str as v => regr_jobs_of_default(v, dflt); error => dflt }
}
```

O primitivo que falta, contra a forma DECLARADA (nao ha impl hoje; e o unico fundo novo):

```teko
/**
 * nproc — quantos processadores logicos o SO concede a este processo AGORA.
 *
 * O DEFAULT DO PARALELISMO. Baixa para `sysconf(_SC_NPROCESSORS_ONLN)` no POSIX e
 * `GetActiveProcessorCount(ALL)` no Windows (o mesmo `#ifdef _WIN32` que `tk_win32_spawnvp` ja usa).
 * Nunca devolve 0: um SO que responde 0 ou falha e tratado como 1 CPU, porque uma corrida tem sempre
 * pelo menos a raia que a executa.
 *
 * @return o numero de CPUs logicas concedidas (>= 1)
 * @since 0.3.1
 */
pub fn nproc(): u64
```
```c
// tk_nproc — o numero de CPUs logicas concedidas ao processo. POSIX: sysconf(_SC_NPROCESSORS_ONLN).
// Windows: GetActiveProcessorCount(ALL_PROCESSOR_GROUPS). Piso 1 (nunca 0). Sem alocacao.
uint64_t tk_nproc(void);
```

### 3.5 O que fica BLOQUEADO (design-ahead honesto)

- **Threads em Teko nao existem** (`journaling-de-corrida-0.3.1.md` §1.2: todos os "thread" na arvore
  sao a palavra em doc-comments). Logo o paralelismo POR-FUNCAO dentro de UM compile (§3.2.1) esta
  BLOQUEADO — o desenho (map por-funcao → barreira na montagem do objeto) esta pronto e nao muda quando
  `teko::isolate`/`fork_join` (S8/C4) aterrar; muda a fiacao do map, nao a forma.
- **`teko::process::run` e sincrono** — dois `cc` em paralelo e o poll de filho estao BLOQUEADOS ate um
  spawn assincrono existir.
- **O paralelismo entregavel HOJE e por-PROCESSO** (§3.2.2/3.2.3): projetos, `.tkr` e shards do gate
  via `run_pool` — nenhum precisa de threads. E onde o ganho e colhivel ja, e onde `build_jobs`/`nproc`
  se aplicam primeiro.

---

## 4. A ORDEM em crumbs — com colisoes nomeadas

Cada crumb e independentemente fechavel (gate-able) e entrega algo sozinho. **MEDIR e o pre-requisito
de PARALELIZAR** — M1..M4 (a lei) vem antes de P1..P3 (o paralelismo).

| # | crumb | toca | colisao nomeada | ganho |
|---:|---|---|---|---|
| **M1** | Fase native: `phase_begin`/`end` em `lower`/`isel`/`regalloc`/`encode`/`objfile`/`link`, cercando `link_object` (o `cc` externo) | `project.tks` (`emit_native*`, `finish_native_object`) | `codegen.tks`/`lower.tks`/`regalloc.tks` **tem/tiveram agentes vivos** — M1 so ADICIONA chamadas em `project.tks`, nao toca a logica desses modulos | o backend deixa de ser mudo; o `cc`/link native passa a ter numero |
| **M2** | `BuildClock` de topo em `main.tks` + total no `exit`; balde `process.startup`/`teardown` | `main.tks`, `progress.tks`, `project.tks` | `main.tks` e raiz, sem agente de produto; baixo risco | o wall externo passa a ser impresso; o balde de ciclo de vida deixa de ser escuro |
| **M3** | `PhaseLedger` + `ledger_reconcile` (a LEI-desigualdade checavel: `dark==0`/`overlap>=0`); `phase_end_*` deposita no razao | `progress.tks`, `project.tks` | isolado em `progress.tks` | `wall <= Σfases` com `dark==0` vira afirmavel; `overlap` vira metrica de speedup |
| **M4** | Consertar as distorcoes: lexer/parser em UMA fase `parse` (fim da dupla contagem); atribuicao `.tkr` distribui o build compartilhado (nomeia, nao esconde) | `assemble.tks:216-264`, `regr_timing.tks`, `regression.tks:2474-2478` | `regression.tks` — o **pool tem o fix do Windows** (`regr_group.tks`/pool). M4 mexe SO na aritmetica de `RowTiming`/`PhaseTimes`, nao no `run_pool`/spawn — coordenar rebase, nao sobrepor | soma para de dobrar e de mentir |
| **M5** | Ligar timing ao JOURNAL: `phase_end_*` emite `append(j,"phase",...)`; sumario le por `fold` | `progress.tks`, `src/journal/*` (do outro agente) | **depende do JOURNAL** (agente vivo) — contra a forma DECLARADA; fiacao de 1 linha quando fechar | timing sobrevive a morte da corrida; sumario unico |
| **P0** | `tk_nproc`/`teko::env::nproc()` (seed C mantido) + `build_jobs()` (default = SO concede) | `teko_rt.{c,h}`, `src/env/*`, `project.tks` | seed C e excecao ao congelamento; sem colisao de agente | o default de paralelismo passa a ser real, e o pin do dev existe |
| **P1** | Fase de regressao por `run_pool` (projetos/`.tkr` independentes), com a medicao build-vs-linhas de `journaling §9` | `regression.tks`, `project.tks` | **pool + fix do Windows** (`regr_group.tks`) — reusar `run_pool`, nao forka-lo | paralelismo entregavel HOJE, por processo |
| **P2** | Gate unitario: confirmar/generalizar `run_gate_sharded` sob `build_jobs` (default SO) em vez de `TEST_JOBS_DEFAULT=4` | `project.tks:3652-3733` | isolado | default honesto no gate |
| **P3** | (DESIGN-AHEAD, BLOQUEADO) map por-funcao `isel/regalloc/encode` → barreira na montagem do objeto, sob `fork_join` | `project.tks`, `src/backend/*` | `regalloc.tks`/`isel_*`/`encode_*` **tem agentes vivos**; ADEMAIS bloqueado por threads — so desenho, nenhuma linha | paralelismo intra-compile quando threads aterrarem |

**Sequencia:** M1 · M2 · M3 · M4 · (M5 quando o journal fechar) · P0 · P1 · P2 · (P3 bloqueado).
Semente: nenhum crumb usa feature de linguagem ausente da semente; o unico fundo novo e `tk_nproc`
(seed C mantido).

---

## 5. Fixtures de regressao (afirmam COMPORTAMENTO, nao numero de saida)

Nota de lei (resolvida abaixo em §7): o dono proibiu fixtures que afirmam "o numero com que sai".
Timing e nao-determinista — NENHUMA fixture afirma um valor de ns. Elas afirmam INVARIANTES estruturais
do razao, que sao deterministas.

| # | fixture | afirma | crumb |
|---:|---|---|---|
| **F1** | um build com N fases conhecidas deposita N labels no razao, na ordem de assentamento | `l.labels == ["parse","checker",...,"link"]` (conjunto/ordem), nao os ns | M3 |
| **F2** | `ledger_reconcile(l, wall)`: com `wall > Σns` (tempo escuro) devolve ERROR (`dark>0`); com `wall == Σns` devolve `dark=0,overlap=0` (sequencial); com `wall < Σns` (fases concorrentes) devolve `dark=0,overlap=Σns−wall` SEM error (paralelismo NAO e defeito) | `dark==0` obrigatorio; `overlap>=0` aceito | M3 |
| **F3** | uma fase native e nomeada no razao (o backend deixou de ser mudo): build native minusculo → `labels` contem `"link"` com ns cercando o `cc` | presenca da fase, nao o ns | M1 |
| **F4** | lexer+parser: apos M4, existe UMA fase `parse`, nao duas com o mesmo intervalo | `labels` tem `"parse"` e NAO `"lexer"`+`"parser"` sobrepostos | M4 |
| **F5** | `.tkr`: dois cenarios reusando um build compartilhado NAO atribuem `compile_ns` inteiro ao cenario 0 — a soma dos `compile_ns` das filas == 1 build, distribuido/nomeado, nao concentrado | a distribuicao, via `PhaseTimes` agregado | M4 |
| **F6** | `build_jobs()` com `TEKO_JOBS=3` → 3; vazio → `nproc()` (>=1); `TEKO_JOBS=0` → `nproc()` | o pin e o default do SO | P0 |
| **F7** | `nproc()` >= 1 sempre (piso), em host de 1 CPU simulado | o piso | P0 |
| **F8** | P1: a fase de regressao por `run_pool` produz o MESMO veredicto (ok/skip/fail por cenario) que a serial — o wall muda, as linhas nao (a lei do `run_pool`) | igualdade de veredicto | P1 |

Formato: no canal EXISTENTE do corpus (`Then stdout pattern = "..."`, forma de
`examples/regressions/own_native/src/scenario.tks`), sem canal novo (um canal novo custa um build).

---

## 6. Pontos de ritual (gate completo obrigatorio)

- **Depois de M1** — muda a saida stderr de TODO build native (novas linhas de fase); confirmar
  fixpoint (as fases sao stderr-only, nao tocam bytes do `.o`/binario — mas o ritual valida).
- **Depois de M2** — toca `main.tks` (o entry point de todo comando); gate completo.
- **Depois de M4** — muda a aritmetica de timing da fase de regressao INTEIRA; gate + conferir
  `regr_timing` nao regride veredicto.
- **Depois de P0** — seed C novo (`tk_nproc`); ritual pesado (fixpoint `gen1.c==gen2.c` + `cmp` local),
  pois toca `teko_rt.{c,h}`.
- **Depois de P1** — muda a fase de regressao para paralela; gate + prova de igualdade de veredicto
  serial-vs-paralelo (F8).

Todas as fases sao stderr-only e NAO alteram bytes de saida (`.o`/binario/`.c`) — o fixpoint e
preservado por construcao; o ritual e a rede, nao a expectativa de quebra.

---

## 7. Riscos e tensoes de lei

| risco | resolucao (law-first) |
|---|---|
| fixture de timing afirmaria um ns (proibido pelo dono: "afirma o que FAZ, nao o numero") | TODAS as fixtures (§5) afirmam INVARIANTES estruturais (presenca/ordem de label, `dark==0`, `overlap>=0`, igualdade de veredicto), nunca um valor de ns. Tensao RESOLVIDA. |
| a LEI travar em `wall == Σfases` quebraria no dia do paralelismo (correcao do dono 2026-08-02) | a LEI e uma DESIGUALDADE `wall <= Σfases` (§0/§2.1); `ledger_reconcile` falha SO em `dark>0`, nunca em `overlap>0`. A regra "balde negativo = defeito" foi REMOVIDA — era precisamente o que falharia depois. Tensao RESOLVIDA. |
| M1/M4/P3 tocam modulos com agentes vivos (`codegen`,`lower`,`regalloc`,`isel_*`,`encode_*`,`regr_group`/pool) | M1 so ADICIONA `phase_begin` em `project.tks` (nao a logica dos modulos); M4 mexe so na aritmetica de `RowTiming` (nao no `run_pool`); P3 e so desenho (bloqueado). Coordenar rebase, nao sobrepor. |
| balde de ciclo de vida absorve tempo que DEVIA ser fase (esconde um buraco novo dentro de "process") | `ledger_reconcile` torna o balde VISIVEL e impresso; se ele crescer, e um sinal de fase faltante a nomear, nao um lixao — a lei "zero tempo sem nome" aplica ao proprio balde (quando uma fatia do balde for identificada, vira fase). |
| paralelismo quebra o fixpoint por ordem | barreira preserva a ordem de item antes de emitir (§3.3); paralelismo vive so ANTES da montagem do objeto. O mesmo cuidado de ordem que refutou (b) do Gap 2. |
| medir custa tempo (o razao/journal adiciona I/O) | o razao e em memoria (ns por push); o `append` ao journal e §5 do journaling: ~2us/registro, <0.2% do gate. Medir nao move o gargalo. |
| `tk_nproc` retorna 0 ou falha | piso 1 por construcao (nunca 0), documentado no fundo C. |

**Nenhuma tensao por resolver. Nao ha HALT.** A unica tensao real (fixture-de-numero) e resolvida
pela forma estrutural das fixtures.

---

## 8. O que fica BLOQUEADO e o que ja compila hoje (mandato "adiantar o que der")

**Compila/entrega HOJE (nao depende de dep bloqueado):** M1, M2, M3, M4 (a lei inteira e as
distorcoes), P0 (`tk_nproc`/`build_jobs`), P1, P2 (paralelismo por-processo via `run_pool`/shards).

**BLOQUEADO, so desenho entregue:**
- **M5** (timing→journal) — depende do modulo `teko::journal` (outro agente). Contrato contra a forma
  DECLARADA pronto; fiacao de 1 linha (`append`) quando fechar. O razao em memoria (§2.3) ja fecha a
  lei SEM o journal.
- **P3** (paralelismo intra-compile por-funcao) — depende de threads em Teko (`teko::isolate`/
  `fork_join`, S8/C4), que NAO existem. Desenho pronto (map por-funcao → barreira na montagem do
  objeto); nao muda de forma quando threads aterrarem.
- **Dois `cc` em paralelo** — depende de spawn assincrono (`teko::process::run` e sincrono hoje).

A parte NAO-bloqueada e a lei completa (o pre-requisito) mais o paralelismo por-processo — que ja
colhe ganho. O implementador retoma o bloqueado em minutos quando o dep fechar.
