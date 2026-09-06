---
section: design
created: 2026-08-02
status: DESENHO — nenhuma linha de produto escrita nesta carga. Executável hoje (nativo, TEKO_BACKEND=native); parte C4 é DESIGN-AHEAD (bloqueado na forma serializada de site-map de cobertura).
branch: fix/union (PR #102, base remodel/0.3.1.0-linux-native-2)
reconcilia: docs/design/harness-de-testes-gerado.md, docs/design/paralelizacao-0.3.1-eixo1-isolamento-por-task.md, docs/design/regressor-principal-0.3.1.md, src/build/project.tks, src/build/regression.tks, src/build/regr_group.tks
---

# Isolamento 100% dos builds de teste — sem recarregar AST (0.3.1)

Arquiteto, 2026-08-02. Documento de DESENHO. Teko-only; o único C tocado seria `src/runtime/teko_rt.{c,h}`
(exceção mantida) e NÃO é preciso nesta carga. Regra do dono honrada: proposta law-first, alarme só com
arquivo:linha.

## 0. O ruling do dono, literal — e o que ele custa

> *"Os builds de testes deveriam ser 100% isolados, ao invés de recarregar AST."*

E os invioláveis que emolduram o redesenho (do briefing):
- **1 build por `.tkp`, o `.tkr` é referência.** Um `.tkp` gera EXATAMENTE 1 build; vários `.tkr` que
  referenciam o MESMO `.tkp` no mesmo diretório são camadas de cenário, não builds separados.
- Variação em SOURCE → um FILE separado DENTRO de um projeto (um build). Só variação em BUILD
  CONFIGURATION (`Given target`/`Given env`) ganha build separado. Cross-target/panic preservados.
- Foco NATIVO; o gate roda gen2 nativo. Determinismo/fixpoint intocados.

**Sintoma medido (CI macOS-arm64, head 7dd96223):** os testes PASSAM
(`regressions 17 run, 0 skipped, 0 failed (70 builds, 256.2s)`), imprime `recheck ✓`, e SÓ ENTÃO o job
morre com `exit code 137` (OOM). O estouro é DEPOIS do veredicto verde, no fim do `recheck` — não
durante os builds.

---

## 1. Diagnóstico — com arquivo:linha. E onde o briefing estava desatualizado.

O briefing pediu "confirme e aprofunde, não confie cego". Confirmei — e a árvore já resolveu DOIS dos
três suspeitos do briefing. O terceiro é o real, e a causa do 137 é uma **coincidência de dois picos**
que nenhum dos dois desenhos anteriores previu.

### 1.1 O que o briefing supôs e que JÁ ESTÁ RESOLVIDO na árvore (correção honesta)

**(a) "o runner-pai segura `fe.prog` vivo para o merge de cobertura entre builds".** NÃO é mais verdade
para o driver. O staging 0.3.1.0 (`test_project`, `project.tks:5777`) já quebrou `teko test` em três
processos re-invocados via `TEST_STAGE_ENV` (`project.tks:5713`):
- `test_stage_build` (`project.tks:5870`) segura `fe.prog`, emite o gate + o compilador-regrcov, e
  **RETORNA — o processo sai, o SO recupera a arena** (`project.tks:5862` doc).
- `test_stage_report` (`project.tks:5906`) RE-checa a fonte e lê a cobertura contra `fe.prog`, e sai.
- `test_drive` (`project.tks:5811`) — o processo que o usuário lançou — segura apenas `m: Manifest`,
  nunca `fe.prog`.

**(b) "a cópia instrumentada por regressor segura AST".** Não. `build_regression_cov_exe`
(`project.tks:5648`) constrói UMA cópia instrumentada do compilador (`tk_emit_c_cov` + `run_cc`) no
STAGE DE BUILD, que sai. Os regressores compilam SNIPPETS através dessa cópia via
`teko::process::spawn_redirected` (`regression.tks:201` `harvest_spec`), cada compile num sub-processo
isolado — memória do build já é reclamada por-build.

**(c) O merge de cobertura JÁ É por fragmento, não por AST.** `cov_merge` (`coverage.tks:427` →
`teko_rt.c:3718 tk_cov_merge`) faz UNION de um dump `.tkcov` serializado nas sinks do processo.
`merge_regression_coverage` (`regression.tks:2033`) e `merge_shard_coverage` (`project.tks:3887`)
apenas dobram dumps de disco. Nenhum precisa da AST para AGREGAR.

Portanto: o mecanismo "cada sub-processo emite seu fragmento serializado; o pai só funde fragmentos"
que o briefing pede para cobertura **já existe**. O ponto (2b) do briefing está, nessa parte, cumprido.

### 1.2 Onde a memória REALMENTE acumula — a tier roda no processo DRIVER

`test_drive` (`project.tks:5811`) chama, EM LINHA, no seu próprio processo:

```
project.tks:5818   let trc     = native_gate_run(...)          // roda o binário do gate (filho leve)
project.tks:5819   let regr_rc = drive_regression_tier(exe, m) // <<< A TIER INTEIRA, NO DRIVER
project.tks:5821   run_test_report_stage(exe, trc, regr_rc)    // spawna o report (recheck ~3GB)
```

`drive_regression_tier` (`project.tks:5835`) → `run_regression_phase` (`regression.tks:3007`) →
`run_regression_sources` (`regression.tks:2954`). **Tudo isto executa no processo `test_drive`.** E o
alocador de regiões do Teko **só devolve memória ao SO na terminação do processo** — está escrito, com
o número, em `project.tks:5757`: *"since the region allocator only releases at termination, its ~3 GB
stayed resident through every later step."*

Logo, cada byte que a tier aloca no driver fica RESIDENTE até o driver sair — e a tier aloca muito, sem
teto, ao longo dos 70 builds e das ~200+ execuções de cenário:

| o que acumula | arquivo:linha | por quê cresce sem teto |
|---|---|---|
| `RegrGroups` (todos os `RegrGroupMember.source` + todo `RegrGroupBuild` com seu `CapResult`) | `regression.tks:2958` (`regr_build_groups`) | criado UMA vez e passado a CADA `run_one_tkr`; vive a corrida inteira |
| `[]TkrFeature` por `.tkr` (a "AST" do DSL de regressão) | `regression.tks:2778` (`parse_tkr`) | um parse por ficheiro; nada é liberado entre ficheiros |
| `CapResult` de cada cenário (stdout/stderr/chan LIDOS DE VOLTA do disco) | `regression.tks:204-206` (`harvest_spec`) | cada `read_file` do `.out`/`.err`/`.chan` do filho vira `str` retido |
| `RegrBuilt.srcs` — todo `RegrSrcBuild.comp` (um `CapResult` cheio) por build standalone | `regression.tks:2126` (`regr_src_remember`) | uma entrada por `Given source` distinto por `.tkr` |
| `rows`/`phase` — todo `RowTiming` de toda linha | `regression.tks:2781`,`2976` | concatenado, nunca podado |

Nenhum destes é a AST do PROJETO (isso o staging já moveu). É o **agregado da orquestração**: 70
capturas de build + centenas de capturas de cenário, cada uma lida de volta para a arena do driver e
nunca liberada.

### 1.3 A CAUSA do exit 137 — a coincidência de dois picos

O `run_pool` já roda cada build num filho isolado (`regression.tks:362`), mas o PAI acumula o `[]CapResult`
(`regression.tks:364`, `380`). Some isso a §1.2 e a ordem do log explica o crash EXATAMENTE:

1. `run_regression_sources` termina no driver e imprime `regressions 17 run … (70 builds)`. Neste ponto
   o driver carrega, RESIDENTE, o agregado inteiro da tier (§1.2) — e nunca vai liberá-lo, porque o
   alocador de regiões não devolve mid-process.
2. O driver então spawna o report stage (`project.tks:5821`) e **BLOQUEIA em `wait`** — o driver
   continua vivo, com todo o agregado da tier residente.
3. O report stage faz `recheck_frontend` (`project.tks:5948` → `frontend_body(true,true,false)`): um
   RE-parse + RE-check do COMPILADOR INTEIRO (auto-hospedado) — o pico ~3GB que `project.tks:5885`
   documenta. Imprime `recheck ✓`.
4. Neste instante coexistem, residentes: **[agregado-da-tier no driver] + [recheck ~3GB no report
   filho]**. No runner macOS-arm64 (teto de RAM apertado) a SOMA cruza o teto → OOM killer → 137,
   logo após `recheck ✓`.

**É uma coincidência de dois pesos-pesados que nenhum dos dois desenhos anteriores previu:** o staging
isolou o build e o report um do outro, mas deixou a TIER no driver; e o driver segura o agregado da
tier VIVO por cima do report porque é o pai que espera. Os dois picos se sobrepõem no `wait`.

### 1.4 O "recarregar AST" que o dono nomeia — a segunda face

Há UM recarregamento de AST genuíno e caro: `recheck_frontend` (`project.tks:5948`) re-parseia e
re-checa o compilador inteiro no report stage, com o único fim de ter `fe.prog` para RENDERIZAR o
relatório de cobertura — `coverage::cov_cobertura(fe.prog)` (`coverage.tks:383`),
`coverage_pct/line/branch(fe.prog)`. A AGREGAÇÃO da cobertura é por fragmento (§1.1c); mas o RELATÓRIO
(mapear site-id → ficheiro:linha) é definido como um WALK do programa tipado. É por isso, e só por
isso, que o report recarrega a AST. É a face que o ruling do dono ataca: *"100% isolados, ao invés de
recarregar AST"*. Removê-la é C4 (§2.4), e é a única parte DESIGN-AHEAD.

---

## 2. A arquitetura — quatro processos, cada build 100% isolado, sem picos coincidentes

Uma lei, aplicada em três lugares: **memória de teste atravessa uma fronteira de PROCESSO, e nada
sobrevive à fronteira exceto um veredicto pequeno num env-slot ou um fragmento serializado num
ficheiro.** É a mesma lei da casa que `paralelizacao-…-eixo1` §3 e `harness-de-testes-gerado` §4 já
ratificaram; aqui ela fecha o buraco que restou: a tier no driver.

### 2.1 C1 (a cura do 137) — a QUARTA etapa isolada: `TEST_STAGE_REGR`

O staging já tem `build`/`report` re-invocados. A tier vira a terceira etapa re-invocada, de modo que
o driver não a hospede. **O rc-plumbing já existe** — `TEST_REGR_RC_ENV` (`project.tks:5742`) já é lido
pelo report (`project.tks:5908`); hoje o driver só o preenche em linha. C1 troca a chamada em-linha por
um spawn.

Depois de C1, o driver: spawna `build` (sai) → roda o binário do gate (filho leve) → **spawna `regr`
(que roda a tier INTEIRA e SAI — o SO recupera todas as capturas)** → spawna `report` (o pico do
recheck, sozinho). **Nenhum par de pesos-pesados coexiste.** Quando o report recheca, o driver segura
só o `Manifest`. O pico cai de `[tier] + [recheck]` para `max([tier], [recheck])`. É a cura direta e é
BEHAVIOR-PRESERVING: os mesmos filhos, as mesmas capturas, os mesmos veriditos, a mesma ordem de log
(a tier já imprime tudo antes de retornar).

Assinaturas que o implementer adiciona (full-Javadoc, copiar verbatim):

```teko
/**
 * TEST_STAGE_REGR — a etapa que RODA os regressivos: a `[tests] regression` inteira num processo
 * próprio, re-invocado, para que a sua acumulação (as capturas de 70 builds e de cada cenário lidas
 * de volta do disco — a arena que o alocador de regiões só devolve na saída) MORRA com este processo
 * antes que o report stage abra o seu pico de recheck. Sem esta fronteira o driver segura o agregado
 * da tier VIVO enquanto espera o report recarregar a AST do compilador, e os dois picos coincidem
 * (a causa medida do exit 137 no CI macOS-arm64).
 *
 * @since 0.3.1
 */
const TEST_STAGE_REGR: str = "regr"

/**
 * test_stage_regr — STAGE 2: entra no projeto, lê o SEU manifesto sozinho (nunca a AST verificada) e
 * roda a tier de regressão com o compilador que o build stage deixou. Depois RETORNA — o processo
 * sai, o SO recupera cada captura que a tier leu de volta. É `drive_regression_tier` de hoje, movida
 * para o seu próprio processo.
 *
 * @param dir  o diretório do projeto
 * @return     0 quando todo regressor passou (ou a tier é consultiva), 1 numa falha gated
 * @throws     a mensagem do manifesto quando o diretório não tem um `.tkp` legível único
 * @since 0.3.1
 */
fn test_stage_regr(dir: str): i32 {
    ensure_rt_dir_abs()
    let exe = self_exe_path()
    let m = match enter_project_manifest(dir) { Manifest as x => x; error as e => return fail(dir, e.message) }
    drive_regression_tier(exe, m)
}
```

E os dois pontos de costura (edições mínimas, aditivas):
- `test_project` (`project.tks:5777`): acrescentar `if stage == TEST_STAGE_REGR { return test_stage_regr(dir) }`
  ao dispatch, ao lado de `build`/`report`.
- `test_drive` (`project.tks:5811`): trocar `let regr_rc = drive_regression_tier(exe, m)`
  (`:5819`) por `let regr_rc = run_test_stage(TEST_STAGE_REGR, exe)` (o mesmo helper que já spawna
  `build`, `project.tks:5958 run_test_stage`). O `native_gate_run` (`:5818`) permanece no driver — é
  só rodar um binário e é leve (a sua cobertura funde no report, não aqui).

> **Nota de sequência do gate:** hoje o driver, entre build e regr, roda `native_gate_run` (o binário
> do gate) e passa `trc` ao report. Isso não muda. O `run_test_stage` já sabe propagar o rc via env; o
> report já lê `TEST_REGR_RC_ENV`. C1 não inventa protocolo — reusa o que build/report usam.

### 2.2 C2 (limitar o pico da própria etapa regr) — chunk re-invocado por lote

C1 tira a coincidência, mas a etapa `regr` sozinha ainda acumula ao longo de ~20 builds (§3 mostra que
a fold já derruba 70→~20; ver §5). Se o pico de UMA etapa `regr` ainda for alto num runner apertado, o
mesmo remédio do dono aplica-se recursivamente: **a etapa `regr` re-invoca a si mesma por LOTE de `.tkr`,
e o SO recupera a memória a cada lote.** É a forma que `compile_regressive` já prova para um release
(`project.tks:5759`).

Esta é uma **segunda linha de defesa, condicional à medição**: com a fold de §5 (70→~20) e a tier já
fora do driver, o pico de uma etapa `regr` é ~1/4 do que o driver hospedava mais o recheck. C2 só é
necessário se a medição pós-C1 ainda mostrar folga insuficiente. Marcado como DESIGN-AHEAD-CONDICIONAL:
desenhado, mas só implementar se C1+C5 não bastarem.

```teko
/**
 * REGR_CHUNK_ENV — o env-slot que diz a uma etapa `regr` re-invocada QUAL fatia da lista de `.tkr`
 * correr (`"<from>:<count>"`), para que o pico de memória de um lote MORRA no fim do lote em vez de
 * somar ao longo de toda a lista. Ausente = correr a lista inteira (o caminho de hoje).
 *
 * @since 0.3.1
 */
const REGR_CHUNK_ENV: str = "TEKO_REGR_CHUNK"

/**
 * regr_chunk_size — quantos `.tkr` uma etapa `regr` corre antes de re-invocar-se para o próximo lote.
 * Derivado do OS-granted parallelism (não um literal), pela mesma razão que os pools seguem o hardware
 * (`os_max`, `regression.tks:254`): um lote maior que a folga de RAM do runner é exatamente o modo de
 * falha que este chunk existe para remover.
 *
 * @return o tamanho do lote, >= 1
 * @since 0.3.1
 */
fn regr_chunk_size(): u64
```

### 2.3 C3 — a fold "1 build por `.tkp`" já existe; o residual e o alvo

Confirmação importante contra o briefing: **a fold que colapsa `Given source` num build por
configuração JÁ ESTÁ IMPLEMENTADA** — `regr_group.tks`, ruling do dono 2026-07-25 (`regr_group.tks:4`).
`regr_row_source` (`regr_group.tks:486`) dobra tanto `source_inline` quanto `Given source = "<file>"`
(o dono baniu inline em `.tkr`, então TODA fonte de `.tkr` é ficheiro e a fold entende ficheiro). Cada
snippet ganha o SEU namespace por diretório (`src/m<k>/body.tks`, `regr_group.tks:21`), que é a resposta
da própria linguagem à colisão de nome nu. **A "violação um-build-por-arquivo" que o briefing descreve
não está mais no caminho da fold** — está nos rows que a fold EXCLUI por construção, e são exatamente os
que o dono manda preservar:

- `CompileFail` (`regr_group.tks:501`) — um build de grupo que contém um compile-fail nunca buildaria;
  fica solo. Inevitável.
- `dep_dir`/`in_place`/`check_wellformed`/cross-target-skip (`regr_group.tks:502-504`) — cada um muda
  o QUE é checado (o artefato do grupo não é o do cenário); fica solo. É variação de BUILD
  CONFIGURATION, que o dono manda dar build separado.
- `snippet_stmts_have_return` (`regr_group.tks:507`) — um `return` de topo vira exit-code no virtual
  main e não cabe no `entry()` do grupo. Solo.
- O PACKING greedy first-fit (`regr_group_for_row`, `regr_group.tks:735`): uma configuração cujos rows
  declaram o mesmo nome nu de topo é partida em N grupos (N builds) para não criar a colisão que os
  bugs #284/#290 ainda produzem fora de DI. Cada grupo é um build.
- `.tkr` baseado em `.tkp`-em-disco (`RegrBuilt`, `regression.tks:2168`) — 1 build por ficheiro `.tkr`,
  já partilhado entre cenários do mesmo ficheiro.

Ou seja: **o alvo de 70→~20 NÃO é reescrever a fold** (ela já colapsa as fontes por configuração); é
(a) registrar-e-consolidar os órfãos sob "1 build por tkp / tkr é referência" (§5), e (b) confirmar por
medição que o residual de builds-solo é irredutível (compile-fail, cross-target, in-place) — esses são
o que o dono EXPLICITAMENTE manda preservar. C3 é, portanto, uma tarefa de CORPUS + medição, não de
motor. O motor está certo.

### 2.4 C4 (DESIGN-AHEAD) — cobertura por site-map serializado, sem recarregar AST

A última face de "ao invés de recarregar AST": o report stage recheca o compilador inteiro só para
mapear site-ids → ficheiro:linha ao renderizar o relatório. A cura law-first: o build stage — que JÁ
tem `fe.prog` tipado — emite, ao lado do gate, um **SITE-MAP serializado** (`bin/<name>.tksites`):
uma tabela `site-id → (ficheiro, linha, coluna, fn-index)`. O report stage lê o site-map + os dumps
`.tkcov` fundidos e renderiza a cobertura por JUNÇÃO de duas tabelas planas — **sem `frontend_body`,
sem re-parse, sem re-check.** O recheck ~3GB do report desaparece; o report passa a ser um processo
leve de fusão de ficheiros.

**Por que é DESIGN-AHEAD:** exige uma forma serializada estável do site-map e uma reescrita de
`cov_cobertura`/`coverage_pct`/`line`/`branch` (`coverage.tks:383+`) para consumir a tabela em vez de
`prog`. É a maior peça, toca a superfície de cobertura, e o seu benefício (remover o pico do report) é
SECUNDÁRIO ao 137 uma vez que C1 já separou os picos. Entrega C1+C3+C5 primeiro (fecham o OOM); C4
remove o último recarregamento de AST quando a forma do site-map estiver ratificada.

Assinatura declarada contra a qual o implementer já pode escrever o scaffolding hoje:

```teko
/**
 * cov_emit_sitemap — serializa o mapa site-id → ficheiro:linha:coluna:fn do programa tipado `prog`
 * num blob que o report stage lê SEM re-parsear a fonte. É o que permite renderizar a cobertura por
 * junção de duas tabelas planas (o site-map + os dumps `.tkcov` fundidos) em vez de um walk da AST —
 * a remoção do último "recarregar AST" que o ruling do dono nomeia.
 *
 * Emitido pelo build stage, que JÁ tem `prog` tipado, ao lado do gate; o report stage nunca mais
 * precisa da AST para relatar.
 *
 * @param prog  o programa verificado (o mesmo `fe.prog` do build stage)
 * @return      o blob serializado do site-map, ou o erro de escrita
 * @since 0.3.1  (DESIGN-AHEAD — bloqueado na ratificação da forma serializada)
 */
fn cov_emit_sitemap(prog: checker::TProgram): str | error

/**
 * cov_cobertura_from_sitemap — renderiza o relatório Cobertura a partir do site-map serializado e das
 * sinks já fundidas, sem tocar na AST. O gêmeo sem-recheck de `cov_cobertura` (`coverage.tks:383`).
 *
 * @param sitemap  o blob que `cov_emit_sitemap` produziu
 * @return         o XML Cobertura
 * @since 0.3.1  (DESIGN-AHEAD)
 */
fn cov_cobertura_from_sitemap(sitemap: str): str | error
```

### 2.5 C5 — a captura não sobrevive ao veredicto (poda do agregado)

Ortogonal a C1 e barato: dentro da etapa `regr`, o `[]CapResult` que `run_pool` devolve
(`regression.tks:388`) e o `RegrBuilt.srcs` (`regression.tks:2191`) só precisam do EXIT e de um veredicto
booleano depois de comparados; o stdout/stderr cru de um build que já passou não é lido de novo. Hoje
são retidos inteiros. C5 substitui o `CapResult` retido por um `CapVerdict` enxuto (exit + ok + um
resumo truncado só quando FALHA) assim que a comparação termina, cortando o texto dos builds VERDES do
agregado. Não muda veredicto nenhum; corta a maior massa (os stdout/stderr verdes). Sob o alocador que
não devolve mid-process isto não "libera", mas REDUZ o high-water da etapa `regr` — soma-se a C1/C2.

```teko
/**
 * CapVerdict — o que resta de um `CapResult` DEPOIS que o cenário foi julgado: o exit, o veredicto e,
 * SÓ numa falha, o texto que a explica (truncado a `CAP_FAIL_TEXT_CAP`). Um build verde não guarda
 * stdout/stderr nenhum — reter o texto de milhares de builds que passaram é precisamente a massa que
 * infla o high-water da etapa de regressão.
 *
 * @since 0.3.1
 */
type CapVerdict = struct {
    /** o exit status observado do filho. */
    exit: i32
    /** true quando o cenário casou o esperado. */
    ok: bool
    /** a explicação da falha, truncada; "" quando `ok`. */
    fail_text: str
}
```

---

## 3. Sequência ORDENADA de crumbs

Ponto ritual = gate completo: **gen2 nativo verde + `teko test .` verde + FIXPOINT gen2==gen3 + a nova
etapa não muda o log byte-a-byte** (`teko test .` produz a MESMA sequência de linhas antes e depois de
C1 — a tier já imprimia tudo antes de retornar).

| # | crumb | entrega | muda contagem de builds? | colisão | ritual |
|---|---|---|---|---|---|
| **C0** | commit vazio + push (proteção anti-restart) | — | não | — | não |
| **C1** | `TEST_STAGE_REGR` + `test_stage_regr` + costura em `test_project`/`test_drive` | **A CURA do 137**: tier fora do driver; picos deixam de coincidir | não (behavior-preserving) | `project.tks` (dispatch + `test_drive`), aditivo | **sim** |
| **C3** | consolidar os 6 órfãos `mem_*` de run num projeto `mem` compartilhado; registrar no `teko.tkp` | +1 build (não +6); ver §5 | **SIM** (corpus) | `teko.tkp`, `examples/regressions/mem/**` | **sim** |
| **C5** | `CapVerdict` — podar o texto dos builds verdes do agregado da etapa `regr` | reduz high-water da etapa `regr` | não | `regression.tks` (`run_pool`/`RegrBuilt`), quente | **sim** |
| **C2** | chunk re-invocado por lote (`REGR_CHUNK_ENV`/`regr_chunk_size`) — **CONDICIONAL à medição pós-C1/C5** | limita o pico da etapa `regr` a um lote | não | `regression.tks`/`project.tks` | **sim** |
| **C4** | site-map serializado + `cov_*_from_sitemap`; report deixa de rechecar | remove o último recarregar-AST | não | `coverage.tks`, `project.tks` (report) — **DESIGN-AHEAD** | **sim** |

Ordem obrigatória: **C1 primeiro** (é a cura, e é independente). C3/C5 em paralelo com C1. C2 só se a
medição pós-C1+C5 mostrar folga insuficiente. C4 por último (maior, secundário ao OOM).

Seed: nenhuma superfície de linguagem nova; `const`, `fn`, `struct`, `enum`, `match`, `env::var/set_var`,
`spawn_redirected` — todos no seed. Sequenciamento de bootstrap trivial.

---

## 4. Fixtures de regressão (input → exit nativo esperado)

Assertos por STDOUT/exit sob gen2 nativo. A propriedade central (isolamento de memória) não é asserível
num `.tkr`, então divide-se em (a) um GUARD estrutural que a torna impossível-por-construção e (b)
fixtures comportamentais que provam que C1 não mudou veredicto nenhum.

| fixture | forma | verdito esperado |
|---|---|---|
| `driver_no_inline_tier` (`.tkt` guard, estilo `fixture_guard_test.tkt`) | varre `src/build/project.tks`: o corpo de `test_drive` NÃO referencia `run_regression_phase`/`run_regression_sources` (só via `TEST_STAGE_REGR`) | falha nomeando ficheiro:linha se a tier voltar ao driver — o guard que trava a regressão do 137 |
| `regr_stage_roundtrip` (`.tkr`) | `teko test` num projeto-mini com 1 regressor trivial e `TEST_STAGE_ENV=regr` | a etapa `regr` sozinha corre o regressor e sai 0; stdout = a linha de sumário |
| `test_verdict_stable_across_staging` (`.tkr`) | o MESMO projeto-mini com regressor que passa e outro que falha | veredicto e ordem de log idênticos a antes de C1 (o gate byte-a-byte) |
| `mem_family_consolidated` (§5) | o projeto `mem` consolidado com os 6 `.tkr` de referência | cada cenário casa o SEU stdout (`"9"`, `"arena freed"`, `"b len=5 sum=10"`, `"region_new ok"`, `"root-anchor"`, `"321"`); 1 build total |
| `regr_chunk_boundary` (`.tkr`, só se C2) | `TEKO_REGR_CHUNK="0:1"` numa lista de 3 `.tkr` | corre só o `.tkr` 0; re-invoca para 1 e 2; sumário idêntico ao da corrida sem chunk |

O gate ritual completo (`teko test .` verde + fixpoint) é o detector de que nenhum veredicto mudou.

---

## 5. Os `.tkr` órfãos — lista e efeito na contagem

25 `.tkr` no disco; 17 registrados no `teko.tkp` (linha 57, `[tests] regression`) — casa com o
`17 run` do CI. **8 órfãos**, todos projetos completos (`.tkp` + `main.tks` + `src/`):

| órfão | kind (`When`) | sob "1 build por tkp / tkr é referência" |
|---|---|---|
| `mem_block_dies` | built and run (`"9"`) | consolida no projeto `mem` |
| `mem_free_arena_ok` | built and run (`"arena freed"`) | consolida no projeto `mem` |
| `mem_free_slice_ok` | built and run (`"b len=5 sum=10"`) | consolida no projeto `mem` |
| `mem_region_new_ok` | built and run (`"region_new ok"`) | consolida no projeto `mem` |
| `mem_singleton_root` | built and run (`"root-anchor"`, nativo) | consolida no projeto `mem` |
| `mem_str_scope` | built and run (`"321"`) | consolida no projeto `mem` |
| `mem_free_arena_leak` | **compilation fails** (`"is dropped without being freed"`) | **fica solo** (compile-fail não agrupa, `regr_group.tks:501`) |
| `seed_literal_arm` | built and run (`"123"`, nativo) | fica solo (concern C-vs-nativo próprio; réplica de seed) |

**Efeito na contagem, honesto:**
- Registrar os 8 como projetos-independentes-de-hoje seria **+8 builds** (cada `.tkp` = 1 build) — o
  OPOSTO do que o dono quer. Não fazer isso.
- Sob o modelo do dono ("tkr é referência"): os 6 `mem_*` de run viram 6 ficheiros-fonte (cada um no
  seu namespace por diretório) DENTRO de um único projeto `examples/regressions/mem/mem.tkp`, com 6
  `.tkr` de referência que buildam o MESMO `.tkp`. **6 `.tkr` → 1 build.** Os `main.tks`/`src` de cada
  órfão migram para `src/<caso>/` do projeto `mem`; cada `.tkr` referencia `mem.tkp` e afirma o seu
  próprio `Then stdout pattern`.
- `mem_free_arena_leak` (compile-fail) → +1 build solo (inevitável).
- `seed_literal_arm` → +1 build solo.

Saldo de registrar os 8 sob o modelo do dono: **+3 builds** (1 projeto `mem` + 2 solos), cobrindo 8
regressões que hoje não correm no gate — em vez de +8 se registrados ingênuos. É a demonstração viva de
"1 build por tkp": 6 tkr, 1 build.

---

## 6. Contagem de builds projetada

Confirmação medida do briefing: o `70 builds` de hoje NÃO vem de um-build-por-`Given source` (a fold de
`regr_group` já colapsa fontes por configuração desde 2026-07-25). Vem de: builds-de-grupo (por
configuração, partidos por colisão de nome nu no packing) + builds-solo irredutíveis (compile-fail,
cross-target, in-place, well-formed) + `.tkr` de projeto (1 cada). O `own_native.tkr` sozinho traz o
differential nomeado por 4 configurações (`own_differential.tks` sob 4 alvos = 4 builds legítimos, é
variação de BUILD CONFIGURATION que o dono manda preservar).

| eixo | efeito na contagem |
|---|---|
| C1 (etapa isolada) | **0** — behavior-preserving; mesmos builds, outro processo |
| C5 (poda de captura) | **0** — mesmos builds, menos memória retida |
| C3 (consolidar `mem`) | **+3** (registra 8 regressões órfãs como 1 projeto + 2 solos) |
| a fold de grupo (já em vigor) | mantém o colapso fonte→configuração |

**Projeção:** a contagem NÃO cai para ~20 por reescrita de motor — o motor já dobra as fontes. O
"~20" só se materializa se, ALÉM da fold, o corpus for reorganizado sob "1 build por tkp": consolidar
famílias hoje espalhadas em muitos `.tkp` (o `mem` é o primeiro exemplo; o próximo candidato medido é
qualquer conjunto de `.tkr` que hoje tenha um `.tkp` por caso e possa virar referência sobre um `.tkp`
compartilhado). **A cura do 137 (C1) não depende dessa queda** — ela remove a coincidência de picos
independentemente da contagem. A queda de contagem é um ganho de tempo/memória adicional, medido por
corpus, não uma pré-condição do fix.

Recomendação honesta ao dono: tratar "70→~20" como META DE CORPUS (consolidação incremental de famílias
`.tkr` sob `.tkp` compartilhado, começando por `mem`), separada da cura de OOM (C1), que é código e é
imediata.

---

## 7. Riscos e tensões de lei — com resolução (law-first)

| risco / tensão | resolução |
|---|---|
| **R1 — a tier reverte ao driver num refactor futuro** (a regressão do 137) | o guard `driver_no_inline_tier` (§4) torna impossível-por-construção: `test_drive` não pode referenciar a tier senão via `TEST_STAGE_REGR`. Falha nomeando linha. |
| **R2 — ordem do log muda com a etapa extra** | a tier já imprime TUDO (sumário, timings) antes de `drive_regression_tier` retornar; movê-la para um filho que herda stdout preserva a ordem. `run_test_stage` já flusha stdout antes do spawn (`project.tks:5961`). `test_verdict_stable_across_staging` é o detector. |
| **R3 — a etapa `regr` sozinha ainda estoura num runner apertado** | C2 (chunk re-invocado) é a segunda linha, condicional à medição. C5 reduz o high-water por poda. Se ainda faltar, o mesmo remédio recursivo do dono aplica-se sem novo desenho. |
| **R4 — registrar órfãos infla a contagem** | sob "1 build por tkp" a família `mem` é 1 build, não 6; compile-fail e seed ficam solo por lei (não por escolha). Saldo +3, não +8. §5. |
| **R5 — C4 toca a superfície de cobertura** | C4 é DESIGN-AHEAD e SECUNDÁRIO: C1 já separa os picos, então o recheck do report deixa de coincidir com o agregado da tier. C4 remove o recheck por completo quando a forma do site-map estiver ratificada; até lá o report recheca sozinho, num processo que sai. Sem tensão de calendário. |
| **R6 — determinismo/fixpoint** | nenhuma etapa nova toca o codegen; `run_pool` já ordena por índice, nunca por término (`regression.tks:352`). O binário emitido é idêntico; fixpoint intocado. |
| **R7 — Teko-only** | zero C nesta carga (C1/C3/C5 são `.tks` + corpus). C4, se e quando, também é `.tks` (coverage). Sem exceção de congelamento acionada. |

**Nenhuma tensão de lei genuína permanece. Sem HALT.**

---

## 8. O que fica BLOQUEADO / DESIGN-AHEAD, explícito

- **C4 (site-map serializado)** está bloqueado na RATIFICAÇÃO da forma serializada do site-map (o
  layout `site-id → ficheiro:linha:coluna:fn`) e na reescrita de `coverage.tks:383+` para consumi-la.
  As assinaturas de §2.4 estão declaradas contra a forma esperada; o implementer pode escrever o
  scaffolding (o módulo, os doc-comments, os honest-stops) que compila hoje e resume em minutos quando
  a forma fechar. **NÃO é pré-condição da cura do 137.**
- **C2 (chunk)** é CONDICIONAL: só implementar se a medição pós-C1+C5 mostrar que o pico de uma etapa
  `regr` ainda cruza o teto do runner. Desenhado, pronto, não obrigatório.
- **A meta de corpus 70→~20** é incremental e medida por família; §6 entrega o primeiro passo (`mem`) e
  o critério (`.tkp` por caso → `.tkp` compartilhado com `.tkr` de referência). Reportado ao dono como
  trilho separado, não convertido em issue por mim.

Achado adjacente REPORTADO (não vira issue por mim): os bugs #284/#290 (resolução por nome nu fora de
DI — synth de trait estrutural, `const` de topo, `fn`-como-valor) são a razão de o packing de grupo
partir uma configuração em N builds (`regr_group.tks:52-69`). Fechá-los reduziria o packing e, com ele,
a contagem de builds de grupo — mas é trabalho de COMPILADOR, reportado para cima.
