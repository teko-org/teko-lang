---
section: design
created: 2026-07-30
source: ruling do dono ("sem meio termos, sem faz isso e depois completa"), ruling do dono ("journaling"), ADENDO do dono 2026-07-30 ("colocar um sumario ... que aponta todos os erros, skips e sucessos, bem como a cobertura medida" — §13), docs/design/concorrencia-adiantada-s8.md §3.2 (fork_join — atribuicao estatica de raia), src/build/regression.tks (run_pool / ProcSpec / o terceiro canal), src/runtime/teko_rt.c:1214 (o despejo periodico que ja sobrevive a SIGKILL), cargo/0.3.1.0-testes-paralelos-canais (teko::test::scoped + a guarda textual)
status: DESENHO — nenhuma linha de produto escrita nesta carga; C1–C9 executaveis hoje, nenhum crumb depende de capacidade inexistente
branch: cargo/0.3.1.0-arq-concorrencia (de remodel/0.3.1.0-linux-native-2 @ 44e39eb)
---

# Journaling de corrida — o controlo de concorrencia que faltou ao paralelismo

> *"Entao, mas atender apenas o caso 1 nao resolve a sobreposicao de arquivo por concorrencia."* — dono
>
> *"Ja nomearia esse 'encerramento elegante' com um nome que o liga muito bem: `journaling`."* — dono

O paralelismo de testes esta medido (4,22x; 20,674 s -> 4,904 s) e o `.tkcov` foi isolado. Este
documento fecha o resto, e fecha-o **com um mecanismo so**. Nao ha fase 2: os dez crumbs abaixo sao
todos executaveis sobre a arvore de hoje, e nenhum espera capacidade que nao exista.

**§13 e o adendo do dono (2026-07-30):** o sumario final sobre as duas fases — erros, skips e
sucessos nomeados, mais a cobertura medida. Ele nao e um enfeite pregado no fim: e a leitura humana
do mesmo `fold`, e por isso sobrevive a morte da corrida que o produziu.

---

## 1. O modelo, numa frase, e porque ha um so

**Toda a escrita de uma corrida vai para um *journal*: um registo append-only, carimbado com a
identidade da corrida, segmentado por escritor. A sumarizacao nao junta nada no fim — ela *rele*.**

Tres propriedades, e cada uma mata um defeito:

| propriedade | mata |
|---|---|
| **segmento por escritor** (ninguem partilha destino) | a sobreposicao — sem lock, porque nao ha escritor partilhado |
| **carimbo de corrida** (o segmento nomeia a corrida a que pertence) | o lixo de corrida anterior com ar de valido |
| **append-only + sumarizacao por releitura** | a perda por morte abrupta — nao ha nada por fundir que se perca, ja estava escrito |

### 1.1 Porque isto colapsa os dois substratos do dono

O dono descreveu dois mecanismos: um **acumulador com thread de sincronizacao** para threads, e um
**tunel com consumidor** ou **ficheiro por processo** para processos. Eles nao sao dois mecanismos:
sao o mesmo, com dois armazens de segmento.

A lei que os une ja esta ratificada nesta casa. `docs/design/concorrencia-adiantada-s8.md` §3.2 fixa
para `fork_join`: *"a atribuicao de indices a raias e ESTATICA (item `i` pertence a raia `i % lanes`),
portanto reprodutivel; cada raia escreve APENAS nas casas dos seus proprios indices, portanto sem
escrita compartilhada; ... Quem le o resultado e o chamador, depois da barreira."*

Palavra por palavra, isso e um journal. "As casas dos seus proprios indices" e o segmento. "Depois
da barreira" e a releitura. O sharding de processos de hoje ja e o mesmo desenho com o segmento em
disco em vez de em memoria. **Portanto nao ha duas mecanicas a propor: ha uma, e ela ja foi
ratificada para threads antes de threads existirem.**

O unico ponto do desenho do dono que **recuso**, e digo-o com a razao em vez de o contornar: a
*"thread de sincronizacao"*. Uma thread que recolhe as entradas dos outros e, por definicao, um
destino partilhado — precisa de lock, serializa exactamente o que foi paralelizado, e (o pior) deixa
o estado a viver **dentro dela**, logo um `SIGKILL` leva-o. Com um segmento por escritor, a
recolha e uma *leitura* apos a barreira, e uma leitura nao coordena com ninguem. O acumulador do dono
existe; o que nao existe e um escritor central para ele.

### 1.2 O que muda de forma quando as threads chegarem: nada

`Journal` carrega um `seg: u64` **opaco** — hoje um descritor de ficheiro, amanha um indice de laje
por raia. O formato do registo, a funcao `append`, a funcao `fold` e o consumidor sao os mesmos. A
troca de substrato e **uma funcao**: `journal_open_rt`. Isto nao e uma fase adiada — threads nao
existem em Teko hoje (todos os acertos de "thread" na arvore sao a palavra *threaded* em
doc-comments do parser), logo implementar um sumidouro de raia e impossivel, nao adiado. O que este
desenho garante e que a chegada delas **nao muda uma linha** do que os crumbs C1–C9 escrevem.

### 1.3 O precedente que ja esta em casa, e onde ele nao serve

`src/runtime/teko_rt.c:1214` ja diz, por escrito, a ideia inteira:

> *"Dumped periodically (every 512 MB — survives a SIGKILL under memory pressure) and at process exit"*

**Serve como precedente da CADENCIA** — escrever durante o voo, e nao no fim, e a unica coisa que
sobrevive ao matador mal-educado — e o desenho herda-a. **Nao serve como mecanismo**, por tres
defeitos medidos que o journal corrige:

1. `tk_obs_dump` faz `fopen(tk_obs_path, "w")` (`teko_rt.c:1294`) — **reescrita total**, nao append.
   Uma morte no meio da reescrita deixa um ficheiro truncado que **parece** valido.
2. `tk_obs_path` e `"/tmp/teko_arena_obs.txt"` (`teko_rt.c:1256`) — **fixo e sem carimbo**. Dois
   processos concorrentes do mesmo host atropelam-se, e o de ontem passa por o de hoje. E o defeito
   deste documento, dentro do proprio precedente.
3. O despejo final vive em `tk_regions_free_all` (`teko_rt.c:1499`), que o **manipulador de crash
   nao chama** — `tk_rt_crash_handler` faz `_Exit(128+sig)` directo (`teko_rt.c:119`). Um SIGSEGV ja
   perde o mapa final hoje.

Reuso a cadencia, reuso o lugar (o mesmo choke point), e substituo `fopen("w")` por append carimbado.

---

## 2. A superficie, em Teko real

### 2.1 O que muda no `.tkt` de quem escreve testes hoje: **nada**

`teko::test::scoped("...")` — que a lane `cargo/0.3.1.0-testes-paralelos-canais` ja fez aterrar —
mantem a grafia e mantem o sentido. O que muda e **onde** o caminho aterra: passa de um sufixo de
identidade para um caminho enraizado na corrida **e** na identidade. Zero chamadas mudam de forma;
a garantia sobe de "dois testes nao colidem" para "dois testes nao colidem, e nenhuma corrida ve o
lixo de outra".

### 2.2 `teko::journal` — o modulo novo

```teko
/**
 * Journal — o segmento append-only que UM escritor possui e mais ninguem.
 *
 * O `seg` e opaco de proposito: hoje e um descritor de ficheiro aberto em `O_APPEND`, e quando as
 * raias de `teko::isolate` chegarem sera o indice de uma laje por raia. O formato do registo, o
 * `append` e o `fold` nao mudam com a troca — e essa invariancia e o motivo de o campo nao ter tipo
 * concreto na superficie.
 *
 * @since 0.3.1
 */
pub type Journal = struct {
    /** o identificador da corrida a que este segmento pertence (`run_id`). */
    run: str
    /** a identidade do escritor — `s<i>` para uma shard do gate, `p<i>` para um filho do pool. */
    writer: str
    /** o sumidouro aberto: descritor hoje, laje quando houver raias. */
    seg: u64
}

/**
 * Record — um registo lido de volta pelo sumarizador.
 *
 * @since 0.3.1
 */
pub type Record = struct {
    /** a corrida que o escreveu; um registo de outra corrida e DESCARTADO pelo `fold`. */
    run: str
    /** quem o escreveu. */
    writer: str
    /** a especie do registo (`begin`, `ok`, `fail`, `cov`, `end`, `stop`, `crash`). */
    kind: str
    /** o corpo do registo, sem quebras de linha (o emissor escapa-as). */
    payload: str
}

/**
 * run_id — o identificador desta corrida, herdado do orquestrador ou criado agora.
 *
 * NOMEAR A CORRIDA E O QUE MATA O LIXO CALADO. Um segmento orfao de ontem vive noutra raiz e carrega
 * outro carimbo, logo e identificavel em vez de silenciosamente fundido — o modo de falha que ja nos
 * mordeu, e o unico da lista que nao dava sinal nenhum.
 *
 * @return o identificador da corrida (`<ns monotonico>-<pid>`), estavel dentro do processo
 * @since 0.3.1
 */
pub fn run_id(): str

/**
 * run_root — o directorio que esta corrida possui, e o UNICO que ela pode criar.
 *
 * @return `bin/.tkrun/<run_id()>`
 * @since 0.3.1
 */
pub fn run_root(): str

/**
 * open — abrir o segmento de `writer` nesta corrida.
 *
 * @param writer  a identidade do escritor; dois escritores vivos nunca a repetem
 * @return        o segmento aberto
 * @throws        quando o segmento nao pode ser criado (permissao, disco cheio)
 * @since 0.3.1
 */
pub fn open(writer: str): Journal | error

/**
 * append — acrescentar UM registo ao segmento de `j`.
 *
 * O PONTO DE DURABILIDADE E ESTE, e esta nomeado: uma unica chamada `write(2)` sobre um descritor
 * aberto em `O_APPEND`, sem buffer de espaco de utilizador. O registo esta no kernel quando `append`
 * retorna, logo um `SIGKILL` do processo nao o perde — so uma queda da maquina o perderia, e essa
 * fronteira esta em §5.
 *
 * @param j        o segmento a acrescentar
 * @param kind     a especie do registo
 * @param payload  o corpo, sem quebras de linha
 * @throws         quando a escrita falha (ENOSPC, EIO) — e a falha e PEGAJOSA (§4, modo 5)
 * @since 0.3.1
 */
pub fn append(j: Journal, kind: str, payload: str): null | error

/**
 * fold — reler TODOS os segmentos de `run` sob `root` e devolver os registos em ordem de segmento.
 *
 * TRES REGRAS DE HIGIENE, e cada uma corresponde a um modo de falha nomeado em §4:
 *
 * 1. um registo cuja corrida nao e `run` e descartado (lixo de outra corrida);
 * 2. a ultima linha de um segmento que nao termina em `\n` e descartada (escrita rasgada);
 * 3. um segmento sem registo `end` e devolvido com um registo `end` sintetico de especie
 *    `incomplete` — o escritor morreu, e a corrida TEM de o saber em vez de o arredondar.
 *
 * @param root  a raiz da corrida (`run_root()`, ou uma raiz antiga numa releitura)
 * @param run   o carimbo que um registo tem de trazer para contar
 * @return      os registos, ordenados por nome de segmento e depois por ordem de escrita
 * @since 0.3.1
 */
pub fn fold(root: str, run: str): []Record

/**
 * scratch — o caminho que `base` tem NESTA corrida e para ESTE escritor.
 *
 * ESTE E O COMPOSITOR UNICO. "Quais caminhos estao isolados" responde-se lendo os chamadores desta
 * funcao, nao greando literais — que foi exactamente como os caminhos fixos passaram despercebidos.
 *
 * @param base  o caminho que se escreveria a mao
 * @return      `<run_root()>/<writer>/<escopo>/<base>`, com os directorios ja criados
 * @since 0.3.1
 */
pub fn scratch(base: str): str

/**
 * sweep — remover as raizes de corrida sob `bin/.tkrun/` que nao sejam `keep`.
 *
 * A LIMPEZA E DA CORRIDA SEGUINTE, NUNCA DA PROPRIA. Uma corrida que limpasse ao sair perderia a
 * limpeza no unico caso em que ela importa (morte abrupta), e perderia com ela a prova de que morreu.
 * Varrer na abertura tem a propriedade oposta: um `SIGKILL` deixa a raiz *de proposito*, e a corrida
 * seguinte varre-a depois de a poder relatar.
 *
 * @param keep  a raiz a preservar (a da corrida corrente)
 * @return      quantas raizes foram removidas
 * @since 0.3.1
 */
pub fn sweep(keep: str): u64
```

### 2.3 Os fundos de runtime (C mantido — `src/runtime/teko_rt.{c,h}`)

```c
// tk_journal_open — abrir/criar `path` em O_WRONLY|O_APPEND|O_CREAT e devolver o descritor cru.
// SEM stdio: um FILE* traz um buffer de espaco de utilizador, e um buffer de espaco de utilizador
// morre com o processo — que e o unico momento em que este ficheiro tinha valor.
int64_t tk_journal_open(tk_str path);
// tk_journal_append — escrever `rec` inteiro no descritor `seg`, repetindo em escrita curta.
// Devolve 0, ou o errno. O registo e limitado a TK_JOURNAL_REC_MAX bytes.
int32_t tk_journal_append(int64_t seg, tk_str rec);
// tk_journal_note — a versao ASYNC-SIGNAL-SAFE, para uso DENTRO de um manipulador de sinal:
// escreve um buffer pre-formatado no descritor ja aberto, sem alocar e sem formatar.
void tk_journal_note(int sig);
// tk_rt_rename — renomear atomicamente dentro do mesmo directorio (rename(2) / MoveFileExW com
// MOVEFILE_REPLACE_EXISTING). O unico primitivo que falta para publicar um ficheiro INTEIRO ou
// nenhum; usado pelos artefactos que nao sao append (o `.tkcov` de uma shard, o `cobertura.xml`).
int32_t tk_rt_rename(tk_str from, tk_str to);
```

`tk_rt_rename` e a segunda metade da disciplina: **o que e append vai por append; o que e um
artefacto inteiro escreve-se num temporario do proprio escritor e publica-se por `rename`.** Nenhum
leitor ve nunca um ficheiro meio-escrito. Sao dois verbos, nao dois modelos: ambos dizem "o escritor
so publica o que esta completo".

### 2.4 Como um teste emite, e como o orquestrador agrega

Do lado de quem escreve testes — **nada muda**:

```teko
#test
/**
 * a_test_that_writes_scratch — um teste que precisa de um ficheiro so seu.
 *
 * A GRAFIA E A MESMA DE HOJE. `teko::test::scoped` continua a ser a chamada; o que ela devolve passa
 * a estar enraizado na corrida, e por isso duas corridas tambem deixam de se ver.
 */
fn a_test_that_writes_scratch() {
    let p = teko::test::scoped("probe")
    match teko::io::write_file(p, "payload") { error => { }; null => { } }
    teko::assert::eq_str(match teko::io::read_file(p) { str as s => s; error => "" }, "payload")
}
```

Do lado do orquestrador, `run_gate_sharded` deixa de "fundir no fim" e passa a **reler**:

```teko
/**
 * gate_summary — o veredicto do gate, RELIDO do journal em vez de acumulado em memoria.
 *
 * PORQUE E UMA RELEITURA E NAO UMA FUSAO. Uma fusao vive na memoria do orquestrador e morre com ele;
 * este `fold` e uma consulta pura sobre bytes que ja estao em disco, logo o mesmo resultado sai de um
 * `teko test --replay <run>` depois de o orquestrador ter sido morto por OOM. Foi o que aconteceu
 * duas vezes hoje (`rc=137`), e nas duas o agregado teria sido perdido em silencio.
 *
 * @param root  a raiz desta corrida
 * @param run   o carimbo desta corrida
 * @param jobs  quantas shards deviam ter escrito um segmento
 * @return      o veredicto agregado, com as shards incompletas nomeadas
 */
fn gate_summary(root: str, run: str, jobs: u64): GateVerdict {
    let recs = teko::journal::fold(root, run)
    let v = gate_fold_records(recs)
    if v.segments < jobs { return gate_verdict_missing(v, jobs) }
    v
}
```

E a fusao de cobertura passa a ser **falivel**, que hoje nao e:

```teko
/**
 * merge_shard_coverage — fundir o despejo de cobertura de cada shard, e PARAR quando um falta.
 *
 * O DEFEITO QUE ISTO FECHA E MEDIDO NA ARVORE DE HOJE. `coverage::cov_merge` devolve um `bool` que a
 * versao actual descarta, e nada remove `<base><i>.tkcov` entre corridas — logo uma shard que morra
 * antes de despejar deixa no lugar o ficheiro da corrida ANTERIOR, e as fasquias sao calculadas sobre
 * ele. Um ficheiro em falta e um facto sobre a corrida, nao um zero.
 *
 * @param base  o prefixo de rascunho das shards
 * @param jobs  quantas shards correram
 * @throws      quando alguma shard nao deixou despejo — a corrida para, com a shard nomeada
 */
fn merge_shard_coverage(base: str, jobs: u64): null | error {
    mut i: u64 = 0
    loop {
        if i >= jobs { break }
        let p = teko::str::concat(base, $"{i}", ".tkcov")
        if !coverage::cov_merge(p) { return error { message = $"teko: shard {i} left no coverage dump ({p})" } }
        i = i + 1
    }
    null
}
```

---

## 3. O inventario completo dos caminhos fixos, e a regra unica

Contei-os eu, com o comando abaixo, e o total esta dito. **A tabela do briefing conta *literais*; esta
conta *escritas*, e as duas nao coincidem.**

```
# literais de caminho no corpus .tkt
grep -rhoE '"(bin|out)/[^"]*|"\.[A-Za-z][^"]*|"/tmp/[^"]*' --include='*.tkt' src | sort | uniq -c | sort -rn
# quem escreve, no corpus inteiro
grep -rn 'teko::io::write_file\|teko::fs::mkdir\|spawn_redirected\|mkdir_p\|rm_rf' --include='*.tks' --include='*.tkt' src
```

### 3.1 A descoberta que reordena a lista: `bin/teko` **nao e escrito por nenhum teste**

As nove utilizacoes de `"bin/teko"` estao todas em `src/build/project_binary_path_test.tkt`, e todas
sao **comparacoes** — `bin_path_for(NativeTarget::X8664Linux) == "bin/teko"`,
`artifact_path_for_target("bin/teko", "x86_64-windows") == ...`. O teste afirma o que o compositor de
caminhos devolve; nao toca no disco. O mesmo vale para toda a familia:

| literal | usos | escritas | onde |
|---|---:|---:|---|
| `bin/teko` | 9 | **0** | `project_binary_path_test.tkt:49,50,51,99,100,101,102,132,133` |
| `bin/teko.o` | 3 | **0** | idem :115,116,117 |
| `bin/teko.exe` | 1 | **0** | idem :34 |
| `out/x` | 2 | **0** | `project_test.tkt:1293,1295` |
| `out/x.o` | 1 | **0** | idem :1294 |
| `bin/x` | 2 | **0** | `regression_test.tkt:335,341` (elemento de argv, comparado) |
| `out/` | 1 | **0** | `project_test.tkt:1293` |
| **subtotal** | **22** | **0** | |

**Resposta directa a pergunta do dono ("o que acontece ao `bin/teko` que nove testes escrevem"):
nada, porque nenhum o escreve.** Sob a regra unica esses literais permanecem intocados, e a guarda
G1 tem de os deixar passar — o que a inversao dela ja afirma
(`sg_line_violates("teko::assert::eq_str(bin_path_for(t), \"bin/teko\")")` tem de dar `false`).

O `bin/teko` **e** escrito — pela *build*, como artefacto (`<od>/<stem>`). Isso e §11.

### 3.2 Classe A — literais que um `.tkt` entrega a um escritor (14 familias, 19 usos)

| # | caminho | usos | quem escreve | testemunha |
|---:|---|---:|---|---|
| 1 | `bin/pt-probe` | 2 | `spawn_redirected` (`project.tks:1502`) | `project_test.tkt:1169,1173` |
| 2 | `bin/pt-probe.err` | (derivado) | idem :1502 | idem |
| 3 | `bin/pt-probe-sh` | 2 | idem | `project_test.tkt:1226,1227` |
| 4 | `bin/pt-probe-sh.err` | (derivado) | idem | idem |
| 5 | `bin/.regr-work/c4_test_probe` | 1 | `tkr_compile_fail_artifact` | `regression_test.tkt:424` |
| 6 | `bin/.regr-work/c4_test_probe2` | 1 | idem | `regression_test.tkt:441` |
| 7 | `.regr-pool-test` | 1 | `spawn_spec` (+`.in/.out/.err/.chan` por filho) | `regression_test.tkt:967` |
| 8 | `.regr-pool-order-test` | 1 | idem | :994 |
| 9 | `.regr-pool-crash-test` | 1 | idem | :1043 |
| 10 | `.regr-pool-determinism-test` | 1 | idem | :1066 |
| 11 | `.regr-pool-verdict-test` | 1 | idem | :1096 |
| 12 | `.regr-pool-verdict-crash-test` | 1 | idem | :1114 |
| 13 | `/tmp/.regr-verdict-path-test.chan` | 2 | `set_var` + `verdict_emit` | `process_test.tkt:9,10` |
| 14 | `/tmp/.regr-verdict-emit-test.chan` | 1 | `verdict_emit` | `process_test.tkt:22` |

### 3.3 Classe B — compostos pelo produto durante a corrida (14 familias, **0 literais em `.tkt`**)

Um `grep` no corpus `.tkt` encontra **zero** destes. Foi por isso que a auditoria por literais nao
podia estar completa — e e por isso que a guarda G2 (§7.2) existe.

| # | caminho | onde |
|---:|---|---|
| 15 | `bin/.regr-work` (`REGR_WORK_DIR`) | `regression.tks:1996` |
| 16 | `bin/.regr-work/.toolquery` + `.in/.out/.err/.chan` | `regression.tks:474` |
| 17 | `<prefix>.proj/` + `teko.tkp`, `main.tks`, `src/regr_decls.tks`, `bin/<nome>` | `regression.tks:1408,1431` |
| 18 | `<prefix>.compile` + `.in/.out/.err/.chan` | `regression.tks:1420` |
| 19 | `<prefix>.run` `.dep_compile` `.depout` `.consumer` `.dep` `.cwd` `.wellformed` | `regression.tks:2482,1505,1499,2292,2294,2347,941` |
| 20 | `<prefixo absoluto>.tkcov` | `regression.tks:2017` |
| 21 | `<binary>.ccprobe.c` | `project.tks:557` |
| 22 | `<binary>.ccmachine` | `project.tks:1472` |
| 23 | `<binary>.Info.plist` | `project.tks:1460` |
| 24 | `.teko-per-commit.diff` | `project.tks:5172` |
| 25 | `<od>/.<stem>-shard/<i>` + `.in/.out/.err/.chan/.tkcov` | `run_gate_sharded` (lane dos canais) |
| 26 | o `covfile` do gate (`TEKO_TKCOV`) | `project.tks:3554` |
| 27 | **`/tmp/teko_arena_obs.txt`** | `teko_rt.c:1256` — fixo, sem carimbo, partilhado por TODO o host |
| 28 | `<out_dir>/cobertura.xml` | `cobertura_path_of` |

**TOTAL: 28 familias de caminho escritas durante uma corrida** (14 de classe A + 14 de classe B),
mais 22 usos de literais que nao escrevem nada. O item 27 e o pior de todos e nenhuma auditoria de
`.tkt` o encontraria: dois `teko test` no mesmo host, em worktrees diferentes, atropelam-se nele.

### 3.4 A regra unica

> **Todo o caminho que uma corrida escreve e `teko::journal::scratch(base)`. Sem excepcao por
> recurso, sem lista.** Um caminho que nao passe por ai ou nao e escrito (§3.1) ou e um artefacto
> declarado da build (§11).

As 28 familias migram por substituicao mecanica do compositor. As 22 comparacoes nao mudam.

---

## 4. Os modos de falha, por caminho de saida

> **REORDENADO POR §14 (ruling do dono, 2026-07-30).** Em **modo teste** as quatro primeiras linhas
> desta tabela **deixam de ocorrer**: `panic` e `exit` sao CAPTURADOS e nao emitem syscall de saida,
> logo nao ha `atexit` saltado, nao ha shard morta por sua propria mao, e a tabela reduz-se a **duas
> classes** — o programa termina (sempre elegante) ou algo de FORA o mata. A tabela abaixo fica como
> esta porque continua a valer **fora** do modo teste (modo `Program`, o compilador em uso real) e
> para a classe externa. Le-a com §14 ao lado.

O dono pediu a tabela por **caminho de saida**, porque a casa ja tem a nocao de que caminhos saltam
que limpeza (`tk_panic` chama `tk_regions_free_all()` a mao porque `abort()` salta o `atexit`,
`teko_rt.c:1660`). Aqui esta, e a ultima coluna e o argumento inteiro:

| caminho de saida | `atexit` corre? | `tk_regions_free_all`? | o journal perde |
|---|:--:|:--:|---|
| retorno normal do `main` | sim | sim (atexit tardio) | **nada** |
| `exit(n)` / `tk_exit` | sim | sim (explicito) | **nada** |
| `panic` / `abort` | **nao** | sim (explicito, W9.3b) | **nada** |
| sinal de crash (SEGV/BUS/ILL/FPE) | **nao** (`_Exit`) | **NAO** (buraco existente) | **nada** |
| sinal educado (INT/TERM/HUP/QUIT) | hoje: nao ha manipulador | nao | **nada** (+ um registo `stop`, C5) |
| `SIGKILL` (OOM killer) | nao | nao | **nada** |

A coluna e uniforme porque o journal **nunca depende de um passo final**. E disso que vem o nome.

E os modos de falha nomeados no briefing, cada um com o que acontece:

| modo | o que acontece |
|---|---|
| **shard que morre a meio** | o segmento tem tudo ate a morte; falta o registo `end`; o `fold` sintetiza `incomplete` e a corrida **falha nomeando a shard**. Hoje: funde silenciosamente o `.tkcov` da corrida anterior. |
| **escrita parcial** | a ultima linha nao termina em `\n` e o `fold` descarta-a. Um `write` curto e repetido em ciclo; registos sao limitados a 4 KiB e `O_APPEND` torna o deslocamento atomico, logo dois escritores nunca se entrelacam no mesmo segmento (e alias nunca partilham segmento). |
| **`SIGKILL` do orquestrador** | nada por fundir se perde. A raiz da corrida fica em disco com o carimbo; `teko test --replay <run>` reconstroi o veredicto inteiro (C9). |
| **`SIGKILL` de um shard** | igual a "shard que morre a meio". |
| **disco cheio** | `append` devolve `error` com o `errno`; a falha e **pegajosa** — o primeiro `append` falhado marca o journal e o processo sai nao-zero com `teko: journal write failed`. Um journal que nao pode ser escrito **nao pode ser saltado em silencio**: e a lei da guarda aplicada ao proprio instrumento. |
| **lixo de corrida anterior com ar de valido** | estruturalmente inalcancavel: outra raiz, outro carimbo. O `fold` nunca le fora de `run_root(run)` **e** descarta registo cujo `run` difere — duas barreiras, porque esta e a que mordeu. |
| **cancelamento de CI / Ctrl-C / timeout** | C5: `stop <sig>` no journal, depois `_Exit(128+sig)`. O pai le "cancelada", nunca "passou". |

---

## 5. A durabilidade: o ponto nomeado, e quanto custa

**O ponto de durabilidade e a chamada `write(2)` em `tk_journal_append`, sobre um descritor
`O_APPEND` sem stdio.** Quando `append` retorna, o registo esta no kernel.

Isso significa uma fronteira exacta, e digo-a em vez de a insinuar:

* **sobrevive**: `SIGKILL`, `abort`, `_Exit`, OOM killer, sinal de crash, qualquer morte do processo;
* **nao sobrevive**: queda da maquina ou falta de energia antes de o kernel esvaziar.

A fronteira certa e essa, porque **o OOM killer mata o processo, nao o kernel**. Foi o OOM killer que
nos matou duas vezes hoje.

### 5.1 O custo, com numeros

Base: ~1167 `#test` na arvore (`grep -rc '^#test' --include='*.tkt' src`), ~3 registos por teste
(`begin`, veredicto, e o `cov`/`end` de shard) => **~3500 registos por corrida**.

| opcao | custo por registo | custo por corrida | % do gate paralelo (4,904 s) |
|---|---:|---:|---:|
| `write(2)` sem `fsync` (**escolhida**) | ~2 us | **~7 ms** | **0,14 %** |
| `fsync` por registo (`TEKO_JOURNAL_FSYNC=1`) | ~500 us (ext4 tipico) | ~1,75 s | ~36 % |
| o que ha hoje: `verdict_emit` (le tudo + escreve tudo) | O(n) por registo | ~490 MB de I/O em O(n^2) | nao medido, e nao trivial |

A terceira linha nao e uma estimativa da forma nova — e a **forma actual**:
`verdict_emit` (`process.tks:215-220`) faz `read_file` do canal inteiro e `write_file` do canal
inteiro **por registo**. Com 3500 registos de ~80 bytes isso da ~490 MB de trafego. Trocar para
append nao e so mais seguro; e a unica das tres que nao cresce em quadrado.

### 5.2 O `.tkcov` NAO tem essa forma — e onde ele tem, e noutro sitio

**Dito explicitamente para ninguem abrir o C1 a procura de um append de cobertura que nao existe, e
"consertar" o que ja esta certo.** O `O(n^2)` de §5.1 e do **`verdict_emit`**, nao do `.tkcov`. O
ficheiro de cobertura **acumula em memoria e despeja UMA vez**: `tk_cov_dump` e um
`fopen(path, "wb")` que percorre as tabelas e escreve tres seccoes (`teko_rt.c:2698-2717`), e o
`fread` esta do lado do **agregador** (`tk_cov_merge`, `:2731`) — exactamente onde deve estar. A
conclusao do dono mantem-se e ja e o comportamento da cobertura: **ler so no fim, para agregar,
sumarizar e gerar o XML.** O que o C1 traz de novo e pôr o CANAL DE VEREDICTO nessa mesma forma.

Os tres defeitos que partiam a cobertura sao de outra natureza, e dois ja estao fechados hoje: shards
a partilhar o caminho (identidade do escritor, C2/C3); uma shard que panica nunca chegar ao despejo
(**C0**, a captura); e o ficheiro da corrida anterior a flutuar (**C4**).

**MAS — e fui medir os TRES sumidouros e nao so o despejo — a intuicao do dono acerta noutro lugar, e
acerta em cheio.** *"Ao invés de apenas apendar, está lendo para saber o que fazer"* descreve
literalmente **dois dos tres sumidouros**, uma camada abaixo do ficheiro: eles releem o **vector em
memoria** para decidir se inserem.

| sumidouro | dedup por acerto | custo | fonte |
|---|---|---|---|
| **linhas** | conjunto de endereçamento aberto (`tk_line_insert_packed`) | **O(1)** amortizado | `teko_rt.c:2646-2652` |
| **funcoes** | **varrimento linear do vector inteiro** (`tk_cov_mark`) | **O(distintas)** por acerto | `teko_rt.c:2541-2554` |
| **ramos** | **varrimento linear do vector inteiro** (`tk_covb_add`) | **O(distintos)** por acerto | `teko_rt.c:2590-2599` |

E a frequencia e o pior da historia: `tk_cov_mark(cov_idx)` e um **prologo de entrada de funcao**
(`emit_function_cov`, `codegen.tks:9601`) e `tk_cov_branch_at` corre em **cada execucao de ramo**
(`codegen.tks:8303`) — nao uma vez por linha distinta, mas uma vez por *passagem*. O lado da consulta
tem a mesma forma: `tk_cov_branch_hit` (`teko_rt.c:2613`) tambem varre linearmente, e a caminhada das
fasquias chama-o por ramo.

**A remodelacao que o dono pede e real, e UMA so forma de funcao, e ela ja existe tres linhas acima no
mesmo ficheiro** — `tk_line_insert_packed`. Funcoes e ramos passam a usar o mesmo conjunto de
endereçamento aberto que as linhas ja usam; o despejo, o formato do ficheiro e o agregador **nao mudam
uma linha**.

**O que eu NAO medi, e digo-o em vez de estimar:** quantos acertos dinamicos uma corrida instrumentada
faz. Sem esse numero o custo real e uma forma conhecida com magnitude desconhecida, e eu nao penduro
um numero inventado num documento que o dono compara com o que ja tem. **A medicao e o primeiro passo
do trabalho**, e se ela desmentir a forma, e a medicao que fica.

**Achado adjacente: REPORTADO, nao convertido em issue por mim.** Nao pertence ao C1 (que e o canal de
veredicto) e nao e pre-requisito de nenhum crumb C0–C9 — a cobertura fica CORRECTA com o C0/C2/C3/C4
tal como estao. E uma questao de CUSTO, nao de correccao, e a decisao de a puxar e do dono.

**O `TestAnalyze` foi verificado tambem** (era o caminho que faltava auditar): ele despeja **um
ficheiro por teste** em `$TEKO_ANALYZE_DIR/<idx>.tkcov` (`emit_test_main_analyze`,
`codegen.tks:12043`) — N escritas, mas cada uma continua a ser um despejo unico de um conjunto em
memoria, sem reler nada para decidir. Ja e endereçado pela identidade do escritor (o indice do teste),
o que confirma o modelo em vez de o contrariar. Herda porem o defeito 3: o directorio e partilhado
entre corridas e um `<idx>.tkcov` obsoleto seria lido como desta corrida — o **C2** cobre-o pela raiz
carimbada. O `ProgramCov` despeja por `atexit` para `$TEKO_TKCOV` (`codegen.tks:11955`), forma unica,
sem releitura.

**Decisao: `write(2)` sempre, `fsync` nunca por omissao, com o botao `TEKO_JOURNAL_FSYNC=1` para
quem quiser durabilidade de queda de maquina.** O botao tem de trazer a sua medicao real no crumb
que o aterra (o numero acima e um tipico de ext4, nao uma medicao desta arvore) — e se a medicao
desmentir a estimativa, e a medicao que fica.

---

## 6. Os sinais educados — o braco que falta

Medido: `tk_rt_install_crash_handler` (`teko_rt.c:120-127`) instala `SIGSEGV`, `SIGBUS`, `SIGILL`,
`SIGFPE` — os de *"o programa partiu-se"*. **Nenhum dos de "alguem pediu para parares"** esta
tratado: `SIGINT`, `SIGTERM`, `SIGHUP`, `SIGQUIT`.

```c
// tk_rt_stop_handler — o braco EDUCADO: alguem pediu para parar (Ctrl-C, timeout de CI,
// cancelamento). Escreve UM registo `stop <sig>` no segmento ja aberto e sai com 128+sig.
//
// ASYNC-SIGNAL-SAFE POR CONSTRUCAO, e e por isso que o descritor e mantido aberto: dentro de um
// manipulador nao se pode alocar nem formatar, so `write(2)` sobre um descritor que ja existe e um
// buffer que ja esta pronto. Este e o motivo tecnico de tk_journal_open ser chamado na abertura da
// corrida e nao no primeiro registo.
static void tk_rt_stop_handler(int sig) {
    tk_journal_note(sig);
    _Exit(128 + sig);
}
```

O mesmo `tk_journal_note` entra em `tk_rt_crash_handler`, o que **fecha de passagem o buraco
existente** do despejo de arena (§1.3, defeito 3).

**Fronteira, dita e nao insinuada:** `SIGKILL` e `SIGSTOP` nao sao capturaveis (POSIX). O
encerramento elegante **nao** salva do OOM killer. Os dois mecanismos sao complementares e ambos sao
obrigatorios: **sinais para os matadores educados, journal para o mal-educado.** Fazer so um e o
meio-termo proibido.

Windows: `signal(SIGINT/SIGTERM)` mais `SetConsoleCtrlHandler` para `CTRL_CLOSE_EVENT`, sob o mesmo
`#ifdef _WIN32` que `tk_win32_spawnvp` ja usa.

---

## 7. A guarda, e como se inverte

Sao duas, porque ha duas coisas verdadeiras a manter, e **cada uma tem de poder falhar**.

### 7.1 G1 — textual: todo o literal de rascunho passa pelo compositor

Ja aterrou na lane `cargo/0.3.1.0-testes-paralelos-canais`
(`src/test/scratch_guard_test.tkt`), com inversao. Este desenho **reusa-a** e alarga-a em dois
pontos:

1. o varrimento passa de `.tkt` para `.tkt` **e** `.tks` — porque as 14 familias de classe B nao tem
   literal em `.tkt` nenhum e a guarda actual e cega para elas;
2. o compositor que ela exige passa a ser `teko::journal::scratch(` (e `teko::test::scoped(`, que
   delega nele).

Inversao: a que ja existe, mais duas linhas plantadas de classe B.

### 7.2 G2 — observacional: a raiz da corrida e a UNICA coisa que a corrida cria

G1 e uma guarda de **grafia**. Um caminho composto em tempo de execucao — de um `.tkr`, de uma
variavel de ambiente, de uma concatenacao — escapa-lhe por construcao. G2 nao le codigo, olha o disco:

```teko
#test
/**
 * jg_a_run_creates_nothing_outside_its_root — A GUARDA QUE NAO SE DEIXA ENGANAR PELA GRAFIA.
 *
 * Tira uma fotografia das raizes escrivies (`bin/`, `out/`, `/tmp`), corre uma corrida de journal
 * completa, e tira outra. Todo o caminho novo tem de estar dentro de `run_root()`. Um caminho
 * composto em tempo de execucao — que e como as 14 familias de classe B nascem — cai aqui e nao cai
 * em G1.
 *
 * NOMEIA O CAMINHO, NUNCA UM NUMERO. Um numero nao se pode agir.
 *
 * @throws quando a corrida criou algo fora da sua raiz
 */
fn jg_a_run_creates_nothing_outside_its_root() {
    let before = jg_snapshot()
    teko::assert::gt_i64(before.len to i64, 0)
    let root = jg_run_a_probe_run()
    let stray = jg_first_outside(jg_snapshot(), before, root)
    teko::assert::is_absent(teko::str::concat("stray path (", stray, ")"), stray.len > 0)
}

#test
/**
 * jg_the_guard_rejects_a_planted_stray — A INVERSAO, EM TRES BRACOS, E NENHUM E OPCIONAL.
 *
 * Tres vezes hoje uma guarda deu ZERO numa arvore que continuava partida, porque era CEGA em vez de
 * tolerante. Por isso a inversao nao afirma so "apanha o plantado": afirma tambem que o INSTRUMENTO
 * esta vivo. Uma fotografia vazia nunca podia diferir de nada, e uma guarda que nao pode falhar e
 * decoracao.
 *
 * 1. um caminho plantado FORA da raiz e nomeado;
 * 2. um caminho novo DENTRO da raiz nao e nomeado (a guarda e tolerante, nao paranoica);
 * 3. a fotografia de base nao e vazia (a guarda esta viva).
 */
fn jg_the_guard_rejects_a_planted_stray() {
    let base = jg_snapshot_of(jg_fixture_before())
    teko::assert::gt_i64(base.len to i64, 0)
    teko::assert::eq_str(jg_first_outside(jg_fixture_after_stray(), base, "bin/.tkrun/r1"), "bin/.planted-stray")
    teko::assert::eq_str(jg_first_outside(jg_fixture_after_clean(), base, "bin/.tkrun/r1"), "")
}
```

**Como se inverte a minha guarda, em uma frase:** planta-se `bin/.planted-stray`, e G2 tem de o
nomear; remove-se a fotografia de base, e o braco 3 tem de falhar. As duas direccoes estao afirmadas
por testes, o que e a diferenca entre uma guarda e um enfeite.

---

## 8. A prova por colisao forcada

Nao provo "corrigi" com 1155 testes verdes — verde numa particao diz que **naquela** particao nao
colidiram. Provo com uma colisao **forcada e deterministica**, no mesmo padrao da prova por reversao
que fechou o degrau 29.

O material ja esta todo na arvore: `run_pool` corre filhos concorrentes, `sh_argv` monta-os
(`regression_test.tkt:967` ja o faz), e o rendezvous e feito com ficheiros marcadores.

```teko
#test
/**
 * jc_the_same_base_in_two_processes_loses_a_write — O ANTES: dois escritores, um caminho, e uma
 * escrita que desaparece. Deterministico, nao provavel.
 *
 * O RENDEZVOUS E O QUE O TORNA DETERMINISTICO. Sem ele a colisao e uma corrida que pode nao
 * acontecer, e uma prova que pode nao acontecer nao e prova. Com ele os dois filhos ficam
 * garantidamente dentro da janela ao mesmo tempo: cada um anuncia-se, espera pelo anuncio do outro,
 * e so entao escreve. O ultimo a escrever fica com o ficheiro inteiro, e o primeiro le de volta bytes
 * que nao sao os seus.
 *
 * ISTO E O DEFEITO QUE O DONO NOMEOU, reproduzido em vez de argumentado — e fica no corpus para
 * sempre, porque uma prova que so se corre uma vez nao guarda nada.
 *
 * @throws quando os dois escritores partilham um caminho e AINDA ASSIM sobrevivem os dois (o que
 *         significaria que esta prova deixou de provar o que diz)
 */
fn jc_the_same_base_in_two_processes_loses_a_write() {
    let w = teko::test::scoped("collide")
    let caps = run_pool(jc_two_children_sharing(w), 2)
    teko::assert::eq_u64(caps.len, 2)
    teko::assert::is_true(jc_one_of_them_read_back_foreign_bytes(caps))
}

#test
/**
 * jc_journal_scratch_keeps_both_writes — O DEPOIS, contra o mesmo rendezvous e a mesma janela: os
 * dois filhos derivam o caminho da sua IDENTIDADE, e os dois sobrevivem inteiros.
 *
 * @throws quando algum dos dois nao le de volta exactamente o que escreveu
 */
fn jc_journal_scratch_keeps_both_writes() {
    let w = teko::test::scoped("noncollide")
    let caps = run_pool(jc_two_children_scoped(w), 2)
    teko::assert::eq_u64(caps.len, 2)
    teko::assert::str_contains(caps[0].chan_text, "intact")
    teko::assert::str_contains(caps[1].chan_text, "intact")
}

#test
/**
 * jc_round_robin_puts_neighbours_in_different_shards — o agravante, afirmado como PROPRIEDADE em vez
 * de suposto.
 *
 * O split e round-robin por ordinal (escolha certa: o custo por teste e desigual), logo dois `#test`
 * ADJACENTES caem em shards diferentes — e adjacentes sao exactamente os que partilham caminho,
 * porque vivem no mesmo ficheiro e na mesma familia. Sem esta linha, a prova acima seria sobre dois
 * processos quaisquer; com ela, e sobre o par que o corpus real produz.
 */
fn jc_round_robin_puts_neighbours_in_different_shards() {
    teko::assert::ne_i64(shard_of_ordinal(7, 4) to i64, shard_of_ordinal(8, 4) to i64)
}
```

O par `jc_the_same_base_*` / `jc_journal_scratch_*` e o **ANTES e o DEPOIS na mesma corrida**. Nao
exige desligar o mecanismo por variavel de ambiente (um botao global desses seria uma maneira nova de
partir a arvore); o braco "antes" simplesmente escreve o literal partilhado directamente nos filhos
`sh`, que e o que o corpus fazia.

---

## 9. `run_regression_sources` — o que isto destranca, e o que nao

**Destranca, e digo porque.** `run_regression_sources` (`regression.tks:2988-3020`) e serial por um
motivo estrutural, nao por preguica: todos os `.tkr` partilham `REGR_WORK_DIR`, e
`merge_regression_coverage` varre essa **unica** raiz. Enraizar o rascunho por escritor remove
exactamente esse acoplamento: cada filho tem a sua sub-arvore e o seu `.tkcov`, e a cobertura da fase
passa a ser o `fold` dos segmentos. A partir dai, repartir os ficheiros `.tkr` por `run_pool` e um
crumb pequeno — `run_pool`, o carimbo de shard e o sumarizador ja existem (C9).

**E o que NAO destranca, dito com a mesma clareza.** Os 620 s de `own_native` sao **um** `.tkr`.
Reparticao por FICHEIRO nao o divide. Reparticao por LINHA dividi-lo-ia, mas as linhas partilham o
artefacto construido (cache de build por processo), logo cada shard reconstruiria. O ganho depende
de quanto dos 620 s e build e quanto e linhas — e **esse numero eu nao tenho**. C9 mede-o primeiro e
aborta se a build dominar; medir antes de paralelizar e a mesma disciplina que o cabecalho do
`regressor.tkr` ja impoe ao numero de builds (*"numero de build se MEDE na lane fail-closed, nao se
deriva por adicao"*). A perna Windows de 15-20 min tem a mesma forma e a mesma medicao pendente.

---

## 10. O custo, em crumbs

Cada crumb e independentemente fechavel e cada um entrega algo sozinho.

| # | crumb | toca | ganho |
|---:|---|---|---|
| **C1** | `teko::journal` (modulo) + fundos `tk_journal_open/append/note` + `tk_rt_rename`; `teko::process::verdict_emit` passa a delegar em `append` | `src/journal/journal.tks` (novo), `src/runtime/teko_rt.{c,h}`, `src/process/process.tks`, `src/checker/scope.tks` (injeccao do namespace) | o sumidouro duravel existe; `verdict_emit` deixa de ser O(n^2) (~490 MB -> ~280 KB de I/O) |
| **C2** | identidade da corrida: `run_id`/`run_root`/`scratch`/`sweep`, `TEKO_RUN` herdado por `spawn_spec`; `teko::test::scoped` re-apontada; ferrolho da raiz | `src/journal/journal.tks`, `src/test/test.tks`, `src/build/regression.tks` | lixo de corrida anterior fica estruturalmente inalcancavel; duas corridas no mesmo worktree deixam de partilhar rascunho |
| **C3** | migrar as **28 familias** (§3.2, §3.3) para `scratch`, incluindo `/tmp/teko_arena_obs.txt` | `regression.tks`, `project.tks`, `teko_rt.c`, 4 `.tkt` | a regra passa a ser total; a auditoria passa a ser "ler os chamadores de um compositor" |
| **C4** | `fold` + `summarize` + `render_summary` (§13); `merge_shard_coverage` falivel; shard sem `end` = falha nomeada | `project.tks`, `src/journal/summary.tks` (novo) | **fecha o defeito medido**: um `.tkcov` em falta deixa de flutuar sobre o da corrida anterior — e passa a ser VISTO, no sumario |
| ~~C4b~~ | **DISSOLVIDO NO C4 por §14.** Existia para reconstruir o que a morte levou; com a captura o arnes chega sempre ao fim e o tally e um contador. Os registos por teste ficam (valem para o `--replay` depois de morte EXTERNA), mas deixam de ser a fonte do numero | — | — |
| **C5** | braco dos sinais educados (INT/TERM/HUP/QUIT) + `tk_journal_note` no manipulador de crash | `teko_rt.c` | cancelamento de CI e Ctrl-C deixam de ser mudos; fecha o buraco existente do despejo de arena no crash |
| **C6** | G1 alargada a `.tks`, compositor unico, inversao estendida | `src/test/scratch_guard_test.tkt` | reincidencia por grafia fica bloqueada nas 28 familias, nao nas 14 visiveis |
| **C7** | G2 observacional + inversao de tres bracos (incl. vivacidade do instrumento) | `src/journal/journal_guard_test.tkt` (novo) | reincidencia por caminho composto em execucao fica bloqueada; a guarda pode falhar, e a inversao prova-o |
| **C8** | a prova por colisao forcada (3 `#test` + 2 filhos `sh` com rendezvous) | `src/journal/journal_collision_test.tkt` (novo) | "corrigi" vira "provei", com o ANTES na mesma corrida |
| **C9** | `teko test --replay <run>` (o mesmo sumario, depois do facto) + o aviso de corrida nao-sumarizada no `sweep` + fase de regressao repartida por `run_pool`, com a medicao build-vs-linhas de §9 | `project.tks`, `regression.tks` | um OOM deixa de perder o veredicto — a corrida seguinte **sumariza a morta** (§13.5); a fase de regressao entra no mesmo mecanismo |

**Pontos de ritual (gate completo obrigatorio):** depois de **C0** (§14 — muda o arnes de TODOS os
1167 `#test`), depois de **C2** (muda caminho em toda a arvore),
depois de **C4b** (muda o arnes emitido, logo muda TODA a fase unitaria), depois de **C8** (a prova
tem de correr verde e o braco "antes" tem de continuar a falhar quando o mecanismo e removido),
depois de **C9**.

**O custo do adendo, dito em separado para ser auditavel:** o sumario **nao** cabe inteiro no C4.
`summarize`/`render_summary` sao um `fold` puro e cabem (C4 cresce ~1 unidade); a releitura cabe no
C9 (que ja era o comando `--replay`). O que **nao** cabia e o crumb novo **C4b**: hoje a fase
unitaria nao emite nada alem de `test <label> ... ` / `ok` para stdout, e um sumario nao pode ser
lido do proprio stdout sem parsing fragil nem distinguir "morreu" de "nunca correu" (§13.2).
**Total: 9 -> 10 crumbs, um novo, dois crescidos.**

**E o ruling do dono (§14) devolve o que o adendo custou:** entra o **C0** (a captura, e vai a
frente de tudo) e o **C4b dissolve-se** nele, porque o tally deixa de precisar de reconstrucao.
**10 -> 10.** Ordem final: **C0** · C1 · C2 · C3 · C4 · C5 · C6 · C7 · C8 · C9.

**Ordem e semente:** nenhum crumb usa funcionalidade de linguagem ausente da semente. Os fundos novos
sao `extern fn` sobre `teko_rt.{c,h}` — C mantido, excepcao explicita ao congelamento. O unico custo
de infra-estrutura e a injeccao do namespace `teko::journal` em `src/checker/scope.tks`, com
precedente directo e recentissimo: a lane dos canais fez o mesmo para `teko::test` (+106 linhas).

---

## 11. O que esta solucao **NAO** cobre

Dito na primeira pessoa, com a razao, e nao como alarme:

1. **Queda da maquina ou falta de energia.** O ponto de durabilidade e o kernel, nao o prato. O botao
   `TEKO_JOURNAL_FSYNC=1` existe e o seu custo esta estimado em §5.1; por omissao esta desligado
   porque paga 36 % do gate por um risco que nao e o nosso (o nosso e o OOM killer).
2. **`SIGKILL` e `SIGSTOP` nao sao capturaveis.** Nenhum encerramento elegante os apanha; e por isso
   que o journal existe **alem** do braco de sinais, e nao em vez dele. (§14 corta a outra metade:
   o que o programa faz a SI PROPRIO — `panic`/`exit` — deixa de o matar em modo teste, e so esta
   linha, a externa, sobra.)
3. **O artefacto `bin/teko`.** Ele e escrito — pela build, como saida declarada (`<od>/<stem>`), nao
   como rascunho. Duas builds simultaneas para o mesmo `-o` atropelam-se, e isso continua verdade.
   C2 mitiga-o com um ferrolho na raiz da corrida que **recusa** a segunda com uma mensagem nomeada,
   mas nao o torna concorrente: dois artefactos no mesmo caminho sao um erro de invocacao, nao uma
   corrida a resolver.
4. **Sistemas de ficheiros de rede.** A atomicidade de deslocamento do `O_APPEND` nao vale em NFS.
   Nenhuma perna nossa constroi sobre NFS; se alguma vier a construir, a garantia degrada e tem de
   ser re-afirmada nesse contexto.
5. **O sumidouro de raia.** A superficie esta desenhada e o `Journal.seg` ja e opaco para o acomodar,
   mas nao ha threads em Teko hoje — isto e capacidade inexistente, nao fase adiada. Quando
   `teko::isolate` (S8/C4, raiz por tarefa) aterrar, muda **uma** funcao: `journal_open_rt`.

---

## 12. Riscos e tensoes de lei

| risco | resolucao |
|---|---|
| C3 toca 28 familias e a lane dos canais esta em merge forward | C3 e mecanico e o compositor e unico; a lane ja aterrou `teko::test::scoped` e este desenho **delega nela** em vez de a substituir — o trabalho dela nao se perde, ganha uma raiz |
| o descritor aberto para o manipulador de sinal e estado global | e a **unica** forma de o manipulador ser async-signal-safe; sem descritor pre-aberto nao ha `write(2)` legal dentro do manipulador, e sem `write(2)` nao ha braco educado |
| `sweep` na abertura apaga a raiz de uma corrida que ainda corre noutro processo | o ferrolho de C2 nomeia a raiz viva; `sweep` salta qualquer raiz com ferrolho valido |
| a estimativa de `fsync` (~500 us) e tipica, nao medida nesta arvore | C5 traz a medicao; se ela desmentir a estimativa, e a medicao que fica — nunca o contrario |

**Tensao de lei resolvida em primeiro lugar:** o meu briefing pede fixtures de regressao expressas
como *"inputs -> expected native exit codes"*. O ruling do dono de hoje proibe-o: *"um teste afirma o
que deve FAZER, nao o numero com que sai"*. Passa o ruling. Toda a fixture de regressao deste desenho
afirma-se por `Then stdout pattern = "scenario <nome>: ok"`, na forma que
`examples/regressions/own_native/src/scenario.tks` ja canonizou, e vive num canal **existente** —
nenhum canal novo, porque um canal novo custa uma build e o alvo do dono e dez.

**Nenhuma tensao por resolver. Nao ha HALT.**

---

## 13. O sumario final — adendo do dono, 2026-07-30

> *"Colocar um sumario (no fim de todos os testes, unitarios e regressivos) que aponta todos os
> erros, skips e sucessos, bem como a cobertura medida."*

O desenho acima nao o resolvia. Resolvia o `gate_summary` da fase **unitaria** (§2.4) e deixava a
fase de regressao a reportar-se sozinha, que e exactamente a separacao que o adendo fecha. Esta
seccao acrescenta-o, e acrescenta-o **como consequencia do journal e nao ao lado dele**.

### 13.1 Porque isto e uma releitura e nao um acumulador

As duas fases escrevem na **mesma raiz de corrida**. Logo nao ha nada por juntar: o sumario das duas
e um `fold` sobre uma raiz, e o `fold` ja existe (C4). A fase de cada registo deriva do prefixo do
escritor (`s<i>` = shard unitaria, `r<i>` = filho de regressao, `m` = orquestrador) — **zero
canalizacao nova**.

E dai vem a propriedade que o adendo torna valiosa em vez de bonita: **um sumario que so existe se a
corrida chegar ao fim e o sumario que falta exactamente quando e preciso.** Este existe depois do
facto (§13.5).

### 13.2 O buraco que o adendo desenterra, e e maior do que parecia

Fui ver o que a fase unitaria reporta hoje. **Nao reporta nada.** `emit_test_main`
(`src/codegen/codegen.tks:11990-12017`) chama cada `#test`, cada um imprime `test <label> ... ` e
`ok`, e o `main` faz `return 0`. Nao ha linha de total, nao ha contagem, nao ha coluna de skip.

E e **fail-fast**: uma assercao falhada faz `panic` -> `abort`, o processo morre, e **todos os testes
seguintes nunca correm, nem sao contados nem nomeados**. O leitor ve um `test X ... ` pendurado sem
`ok` e mais nada. Nao ha como saber quantos ficaram por correr.

Por isso o C4b existe e por isso ele **nao cabia** no C4: sem um registo `plan` (quantos testes esta
shard ia correr) e um `begin` por teste, o sumario nao pode dizer `never-ran`, e `never-ran` e
precisamente o numero que hoje esta invisivel. Sob sharding a coisa melhora sozinha — as outras tres
shards acabam — mas so o journal permite dizer **qual** shard morreu, **em que teste**, e **quantos**
ficaram por correr.

> Nao parseio o meu proprio stdout para isto. Um sumario derivado das linhas `test ... ok` seria
> fragil (o teste imprime o que quiser no meio) e, sobretudo, **nao distingue "morreu" de "nunca
> correu"** — que e a unica distincao que o dono precisa de ler.

### 13.3 A superficie

```teko
/**
 * Finding — UM achado nomeado do sumario: uma falha, um skip, ou um escritor que morreu.
 *
 * NOMEADO, NUNCA CONTADO. Um numero nao se pode agir: "1 skipped" nao diz qual fila nem porque, e a
 * lei desta casa e que um skip no CI do proprio compilador E falha (`scripts/no_skips_gate.sh`). Uma
 * lei sem instrumento e uma lei que ninguem aplica.
 *
 * @since 0.3.1
 */
pub type Finding = struct {
    /** `unit` ou `regression`, derivado do prefixo do escritor. */
    phase: str
    /** que escritor o produziu (`s2`, `r0`, `m`). */
    writer: str
    /** o que ele nomeia: o `#test`, ou `<ficheiro>.tkr[<cenario> linha <n>]`. */
    label: str
    /** porque — a razao honesta que o produtor ja escreve hoje, transportada em vez de resumida. */
    reason: str
}

/**
 * RunSummary — o veredicto INTEIRO de uma corrida: as duas fases, os achados nomeados, a cobertura.
 *
 * @since 0.3.1
 */
pub type RunSummary = struct {
    /** o carimbo da corrida que este sumario descreve. */
    run: str
    /** testes/linhas que passaram, por fase e no total (`unit`, `regression`, `total`). */
    passed: PhaseTally
    /** os que falharam. */
    failed: PhaseTally
    /** os que foram saltados por capacidade ausente. */
    skipped: PhaseTally
    /** os que NUNCA correram porque o escritor morreu antes de lá chegar (`plan` menos o resto). */
    never_ran: PhaseTally
    /** cada falha, nomeada. */
    fails: []Finding
    /** cada skip, nomeado COM a razao. */
    skips: []Finding
    /** cada escritor sem registo `end`, com o ultimo teste que chegou a comecar. */
    dead: []Finding
    /** funcoes/linhas/ramos medidos, do fold dos despejos desta corrida. */
    cov: CovTriple
    /** quantos despejos de cobertura eram esperados e nao apareceram (§2.4). */
    cov_missing: u64
}

/**
 * summarize — o sumario das DUAS fases, como funcao pura sobre os registos.
 *
 * PURA DE PROPOSITO: e o que permite testa-la sobre registos sinteticos (§13.6) sem correr uma
 * suite, e e o que permite ao `--replay` produzir byte-a-byte o mesmo bloco depois do facto.
 *
 * @param recs  os registos que `fold` devolveu
 * @return      o veredicto agregado
 * @since 0.3.1
 */
pub fn summarize(recs: []Record): RunSummary

/**
 * render_summary — o bloco que o humano le, em texto estavel.
 *
 * ESTAVEL PORQUE E LIDO POR UM SCRIPT TAMBEM. `scripts/no_skips_gate.sh` reconstitui hoje os skips
 * por tres padroes de `grep` diferentes, um dos quais (`regression ok ... 2 scenario row(s)
 * skipped`) existe so porque um skip pode esconder-se atras de um `ok`. Com uma seccao `SKIPPED`
 * nomeada, um skip nunca se esconde e o script colapsa para UM padrao.
 *
 * @param s  o veredicto a renderizar
 * @return   o bloco, com quebras de linha, terminado pela linha de fecho
 * @since 0.3.1
 */
pub fn render_summary(s: RunSummary): str
```

### 13.4 O bloco, como o dono o vai ler

```
teko: ===== summary (run 1753891200123456789-48213) =====
teko:   unit          1167 passed     0 failed    0 skipped    0 never-ran
teko:   regression       8 passed     1 failed    2 skipped    0 never-ran
teko:   TOTAL         1175 passed     1 failed    2 skipped    0 never-ran
teko:
teko:   FAILED (1):
teko:     regression  r0  own_native.tkr[own_defer_arm_write_propagates]: exit 1, expected stdout pattern absent
teko:
teko:   SKIPPED (2) — SKIPPED IS FAILURE IN THIS PROJECT'S OWN CI:
teko:     regression  r0  own_native.tkr[x86_64-windows row 3]: x86_64-w64-mingw32-gcc (the cross-linker for target x86_64-windows) not found on PATH
teko:     regression  r0  own_native.tkr[x86_64-windows row 7]: x86_64-w64-mingw32-gcc (the cross-linker for target x86_64-windows) not found on PATH
teko:
teko:   COVERAGE (4 shard dumps + 63 regression dumps merged, 0 missing):
teko:     functions   92% (4811/5219)    floor 90   ok
teko:     lines       88% (61233/69582)  floor 85   ok
teko:     branches    74% (18220/24486)  floor 70   ok
teko: ===== 1 failed, 2 skipped — run FAILED =====
```

Quatro coisas que este bloco faz e que hoje ninguem faz:

1. **Soma as duas fases.** A linha `TOTAL` nao existe em lado nenhum da arvore.
2. **Nomeia cada skip com a sua razao.** O `... (the cross-linker for target ...) not found on PATH` que
   viaja pelas pernas do CI ja e produzido — `regression.tks:611` — e ja e impresso em linha
   (`regr_row_skip_line`, `regression.tks:2593`). O que faltava era ele **aparecer no fim**, junto,
   em vez de estar espalhado a 4000 linhas de transcricao do sitio onde o leitor olha.
3. **Mostra a cobertura medida e as fasquias lado a lado**, incluindo `missing` — o numero do defeito
   que C4 fecha passa a ser visto, em vez de calculado em silencio.
4. **Tem uma linha de fecho.** "O sumario chegou a ser impresso?" passa a ser respondivel; e um `grep`
   por `===== .* run ` que hoje nao tem alvo.

**O que este sumario NAO muda: a politica.** Nomear um skip nao o transforma em falha. A separacao ja
esta decidida e esta certa (`scripts/no_skips_gate.sh`: *"a skipped row is a legitimate outcome of
the LANGUAGE ... o que NAO lhes pertence e o NOSSO CI reportar verde sobre linhas que nunca
executou"*), e `REGRESSION_REQUIRE_TOOLS=1` continua a ser o botao fail-closed. O sumario da a lei o
**instrumento**; quem decide continua a ser a lane.

### 13.5 A corrida morta tambem e sumarizada

Duas mortes por OOM hoje (`rc=137`), em duas maquinas. Nesses dois casos o orquestrador nunca chegou
a imprimir bloco nenhum. Com o journal:

* os segmentos estao em disco, carimbados;
* `teko test --replay <run>` imprime **o mesmo bloco**, com `dead: s2 (morreu em <ultimo teste>)` e
  `never-ran: 291`;
* e o `sweep` da corrida SEGUINTE (C2), antes de varrer, ve uma raiz sem sumario e diz uma linha:

```
teko: warning: run 1753891200123456789-48213 ended without a summary (2 failures, 1 writer dead) — `teko test --replay 1753891200123456789-48213`
```

**Uma corrida morta passa a ser sumarizada pela seguinte.** E isso nao e um extra do adendo: e a
razao pela qual `sweep` varre na abertura e nao no fecho, que ja estava fixada no `@return` de
`teko::journal::sweep` (§2.2) antes de o adendo existir.

### 13.6 A guarda do sumario — e ela tambem tem de poder falhar

Um sumario e um instrumento, e um instrumento cego e a patologia desta lane (tres vezes hoje). Por
isso o `summarize` puro tem inversao de tres bracos, e o terceiro e o que importa:

```teko
#test
/**
 * js_the_summary_names_every_finding — a inversao: alimentado com uma falha, dois skips e um
 * escritor morto, o sumario tem de nomear os quatro. Um sumario que os contasse sem os nomear
 * passaria neste teste se ele afirmasse numeros; por isso afirma NOMES.
 *
 * @throws quando algum achado plantado nao aparece no bloco renderizado
 */
fn js_the_summary_names_every_finding() {
    let block = teko::journal::render_summary(teko::journal::summarize(js_planted_records()))
    teko::assert::str_contains(block, "own_native.tkr[x86_64-windows row 3]")
    teko::assert::str_contains(block, "not found on PATH")
    teko::assert::str_contains(block, "js_planted_failure")
    teko::assert::str_contains(block, "never-ran")
}

#test
/**
 * js_an_empty_run_is_a_FAILURE_not_a_green — O BRACO DE VIVACIDADE, e e o unico indispensavel.
 *
 * Zero registos significa que a corrida nao produziu nada, e um sumario que renderize "0 failed" com
 * ar de verde sobre zero registos e exactamente a guarda cega que deu ZERO tres vezes hoje numa
 * arvore que continuava a parar. Um instrumento que nao pode falhar e decoracao — inclusive este.
 *
 * @throws quando um sumario vazio nao se declara falhado
 */
fn js_an_empty_run_is_a_FAILURE_not_a_green() {
    let block = teko::journal::render_summary(teko::journal::summarize(teko::list::empty()))
    teko::assert::str_contains(block, "no records — run produced nothing")
    teko::assert::str_absent(block, "run PASSED")
}

#test
/**
 * js_a_dead_writer_cannot_read_as_passed — a shard que morre a meio nao pode arredondar para verde,
 * e a contagem de `never-ran` tem de ser a diferenca para o `plan` que ela declarou.
 *
 * @throws quando um `plan` de 300 com 9 vereditos nao rende 291 nunca-corridos
 */
fn js_a_dead_writer_cannot_read_as_passed() {
    let s = teko::journal::summarize(js_records_of_a_dead_shard(300, 9))
    teko::assert::eq_u64(s.never_ran.total, 291)
    teko::assert::eq_u64(s.dead.len, 1)
    teko::assert::str_absent(teko::journal::render_summary(s), "run PASSED")
}
```

### 13.7 Fixture de regressao

Um cenario no canal **existente** (`own_native`) — nenhum canal novo, nenhuma build nova:

```
  Scenario: journal_summary_names_a_planted_skip
    Given args = ["--replay-fixture"]
    When built and run
    Then stdout pattern = "scenario journal_summary_names_a_planted_skip: ok"
```

O caso monta registos sinteticos com uma falha e um skip, renderiza, e afirma pelo NOME (nunca pelo
codigo de saida — ruling do dono de hoje). A prova de que o bloco sobrevive a morte da corrida e o
`#test` `js_a_dead_writer_cannot_read_as_passed` acima, que nao precisa de matar processo nenhum
porque `summarize` e pura.

### 13.8 O que o sumario NAO cobre

1. **Nao muda a politica de skip** (§13.4) — nomeia, nao decide.
2. ~~**Nao torna a fase unitaria tolerante a falhas.** O `panic` continua a matar a shard.~~
   **RISCADO PELO RULING DO DONO (§14).** Isto era o defeito, nao um limite. Com a captura o `panic`
   deixa de matar a shard, o tally deixa de ser reconstrucao e `never-ran` passa a 0 por construcao no
   caso auto-infligido. O que continua fora de alcance e apenas o **isolamento** entre testes
   (`teko::isolate`, S8) — um limite estreito, nao uma desculpa para deixar a suite morrer.
3. **Nao inclui a cobertura de uma shard cujo despejo falta.** Ela e reportada como `missing`, e a
   corrida falha (C4). Estimar o que falta seria inventar o numero que a fasquia julga.

---

## 14. A captura em modo teste — ruling do dono, 2026-07-30

> *"E PARA CAPTURAR E SAIR ELEGANTE ANTES SEM ENVIAR SYSCALL DE SAIDA QUANDO COMPILAR TESTES... Uma
> coisa e um aborto externo, de fora do programa, outra coisa e o proprio programa sair, e para sair
> ele tem que ser deterministico."*

**A critica e minha e e justa.** Escrevi em §11.2 que *"o `panic` continua a matar a shard"* e
tratei o fail-fast como dado. Nao e dado: **e o defeito**. O meu `C4b` — journalizar `plan`/`begin`
para poder calcular `never-ran` — existia inteiro para **reconstruir o que a morte levou**. A morte
nao devia acontecer.

### 14.1 A lei, e onde ela corta

| | quem manda | o que se faz |
|---|---|---|
| o **proprio programa** sai (`panic`, `exit`, assercao falhada) | nos | **captura-se**, deterministico, **sem syscall de saida** em modo teste |
| aborto **externo** (SIGKILL do OOM, SO, hardware, energia) | ninguem nosso | **convive-se** — o journal, o `--replay`, o varrimento |

O journal **nao deixa de fazer falta**: a segunda linha continua a existir e foi ela que nos matou
duas vezes hoje. O que muda e o **peso**: o journal deixa de ser a defesa principal e passa a ser a
defesa do que nao controlamos.

### 14.2 O mecanismo, e porque o salto vive no RUNTIME e nao no codigo emitido

A captura e um `setjmp`/`longjmp` — e **inteiro dentro de `teko_rt.c`**. O codigo emitido nunca
nomeia `setjmp`.

Isso nao e estetica. `setjmp` tem exigencias que nenhum gerador de codigo geral quer honrar (a moldura
que o chamou tem de continuar viva; locais modificados entre o `setjmp` e o `longjmp` tem de ser
`volatile`). Metendo-o no runtime, **o que o backend proprio tem de saber fazer reduz-se a uma coisa:
tomar o endereco de uma funcao de topo e passa-lo**. Isso e um `lea`/`adr` de um simbolo que o
backend ja emite para chamar. Nomeio-o como requisito da escada da morte do C para nao ser surpresa.

```c
// teko_rt.h — como um #test terminou. TRES resultados, nenhum deles um estado do processo.
#define TK_TEST_OK        0   /* o corpo retornou */
#define TK_TEST_PANICKED  1   /* panic / assercao falhada / panic implicito (div0, oob, cast) */
#define TK_TEST_EXITED    2   /* o corpo chamou exit(n); `code` traz o n */
typedef struct { int32_t how; int32_t code; } tk_test_end;

// tk_test_run — correr UM corpo de #test capturando a saida que ele proprio provoque.
//
// O SALTO VIVE AQUI, e e por isso que o codigo gerado nao precisa de saber que ele existe: o
// backend proprio so tem de saber tomar o endereco de `body`. `body` e `volatile` porque um
// parametro modificado (ou apenas mantido) entre o setjmp e o longjmp e indeterminado sem isso —
// e a unica exigencia do setjmp que nao se pode delegar.
tk_test_end tk_test_run(void (* volatile body)(void));
```

```c
// teko_rt.c
static jmp_buf tk_test_jb;
static volatile sig_atomic_t tk_test_capturing = 0;
static volatile int32_t tk_test_how = TK_TEST_OK, tk_test_code = 0;

tk_test_end tk_test_run(void (* volatile body)(void)) {
    tk_test_how = TK_TEST_OK; tk_test_code = 0;
    tk_test_capturing = 1;
    if (setjmp(tk_test_jb) == 0) { body(); }
    tk_test_capturing = 0;
    tk_test_end e; e.how = (int32_t)tk_test_how; e.code = (int32_t)tk_test_code; return e;
}
```

E os **dois** pontos de estrangulamento, que ja existiam e nao ganham chamadores novos:

```c
_Noreturn void tk_panic_str(tk_str msg) {
    if (tk_test_capturing) {                       // MODO TESTE: para o teste, nao para o processo
        tk_test_note(TK_PANIC_MARKER, msg);        // no canal DESTE teste, nunca no fluxo partilhado
        tk_test_how = TK_TEST_PANICKED;
        tk_test_capturing = 0;
        longjmp(tk_test_jb, 1);
    }
    fputs(TK_PANIC_MARKER, stderr); fwrite(msg.ptr, 1, msg.len, stderr); fputc('\n', stderr);
    tk_backtrace();
    tk_regions_free_all();                         // (W9.3b) abort() salta o atexit
    abort();
}

_Noreturn void tk_exit(int32_t code) {
    if (tk_test_capturing) {
        tk_test_how = TK_TEST_EXITED; tk_test_code = code;
        tk_test_capturing = 0;
        longjmp(tk_test_jb, 1);
    }
    tk_regions_free_all(); exit(tk_exit_status(code));
}
```

Cinco propriedades, e cada uma foi verificada na arvore antes de eu a escrever:

1. **Nenhuma assinatura muda.** `_Noreturn` continua honesto: `longjmp` nao retorna. Zero chamadores
   tocados, zero mudanca no codegen dos sitios de chamada.
2. **Cobre os panics implicitos.** `tk_panic_div0`, `tk_panic_oob` e `tk_panic_cast`
   (`teko_rt.c:1890-1900`) desaguam todos em `tk_panic`, e as assercoes desaguam em `tk_assert_fail`
   -> `tk_panic` (`src/assert/assert.c:57,69`). Um so ponto, ja partilhado.
3. **Nao muda a semantica de `defer`.** Medido, e ao contrario do que eu supunha: o replay dos
   `defer` acontece **no sitio de chamada, ANTES** da chamada divergente — e o que o cenario
   `defer_cascade_exit` prova (`regressor.tkr:228`: *"its own argument is read AFTER the replay, so
   the exit code itself is the LIFO proof"*), e `defer_not_duplicated_on_diverging_call` guarda o
   outro lado. Quando o controlo chega a `tk_exit`/`tk_panic_str`, os `defer` **ja correram**. O
   `longjmp` nao salta nenhum. O limite residual documentado (um panic IMPLICITO nao tem sitio de
   chamada, logo o seu `defer` nao corre — *contrato, nao lacuna*) mantem-se identico.
4. **A arena passa a rebobinar de verdade.** `tk_arena_push`/`tk_arena_pop` envolvem cada teste
   (`emit_test_call`); hoje um teste que panica **nunca chega ao pop** porque o processo morre. Com
   a captura o pop e sempre alcancado.
5. **A mensagem passa a ser atribuida ao teste certo.** O comentario de `tk_flush_out`
   (`teko_rt.c:1610-1620`) narra uma investigacao inteira perdida por isto: *"an abort was attributed
   to a spine test ~66 tests before the real one"*. Sob captura a mensagem entra no canal **deste**
   teste. A ma atribuicao deixa de ser possivel.

### 14.3 A superficie em Teko

```teko
/**
 * TestHow — como um `#test` terminou. Um resultado do TESTE, nunca um estado do processo.
 *
 * @since 0.3.1
 */
pub type TestHow = enum { Ok; Panicked; Exited }

/**
 * TestEnd — o fim de um `#test`: como acabou e, quando `Exited`, com que valor.
 *
 * O VALOR VIVE AQUI E NAO NO PROCESSO, e e essa a diferenca inteira. `exit(7)` dentro de um `#test`
 * e um FACTO SOBRE O TESTE que o arnes relata e continua; `exit(7)` num programa e o contrato do
 * programa com quem o invocou, e esse fica intocado (§14.5).
 *
 * @since 0.3.1
 */
pub type TestEnd = struct {
    /** como o corpo terminou. */
    how: TestHow
    /** o valor de `exit(n)` quando `how` e `Exited`; 0 nos outros casos. */
    code: i32
}

/**
 * run_capturing — correr `body` capturando qualquer saida que ele proprio provoque, e devolver o
 * controlo ao arnes SEM syscall de saida.
 *
 * DETERMINISTICO NOS TRES EIXOS que o dono exigiu: quem captura sabe QUAL teste terminou (o arnes
 * chamou-o e nao largou o rotulo), COMO terminou (`TestHow`) e, no caso de `exit`, COM QUE VALOR.
 * Nao ha corrida, nao ha sinal, nao ha ordem por descobrir: e um retorno de funcao.
 *
 * @param body  o corpo do `#test`, passado por endereco
 * @return      como o corpo terminou
 * @since 0.3.1
 */
pub extern fn run_capturing(body: cabi fn()): TestEnd = "tk_test_run" from "teko_rt"
```

### 14.4 O arnes, e o que ele deixa de precisar

`emit_test_call` (`src/codegen/codegen.tks:12079`) passa de

```c
tk_arena_push(); tk_print("test <label> ... "); tk_flush_out(); <fn>(); tk_println("ok"); tk_arena_pop();
```

para

```c
tk_arena_push(); tk_test_begin("<label>");
tk_test_report(tk_test_run(&<fn>));      /* imprime o veredicto E soma o tally E journaliza */
tk_test_end(); tk_arena_pop();
```

e `emit_test_main` (`:11990`) deixa de terminar em `return 0;` cego:

```c
tk_cov_branches_on(false); tk_cov_lines_on(false);
{ const char *dp = getenv("TEKO_TKCOV"); if (dp) tk_cov_dump(dp); }
tk_test_summary();                       /* o bloco de §13, agora natural */
return tk_test_any_failed() ? 1 : 0;
```

**E aqui e que o adendo de §13 se reordena inteiro:**

* o **tally deixa de ser reconstrucao e passa a ser um contador**. O arnes chega sempre ao fim, logo
  `passed`/`failed`/`exited` sao somas triviais;
* **`never-ran` passa a ser 0 por construcao** no caso auto-infligido. Continua a existir como coluna,
  mas so a classe EXTERNA a pode tornar nao-zero — que e exactamente onde ela devia ter estado;
* o **`.tkcov` passa a ser despejado sempre**. Hoje uma shard que panica nunca chega ao `tk_cov_dump`
  no fim do `main`. Era metade do defeito que o C4 tinha de DETECTAR; com a captura, um despejo em
  falta so pode vir de morte externa — e o C4 continua a detecta-lo, agora sobre um caso raro em vez
  de sobre o caso comum;
* o **`C4b` dissolve-se**. Ele existia para reconstruir o que a morte levou. Os registos por teste
  continuam a ser escritos (valem para o `--replay` depois de morte externa), mas deixam de ser a
  fonte do tally — passam a ser confirmacao, e o crumb encolhe para dentro do C4.

### 14.5 O que NAO e capturado, e a fronteira e estrutural e nao um interruptor

A captura e de **modo teste**, palavra do dono: *"quando compilar testes"*. E o modo ja existe:
`CgMode = enum { Program; TestPlain; TestCov; TestAnalyze; ProgramCov }`
(`src/codegen/codegen.tks:59`). `tk_test_run` e emitido **so** pelos tres perfis `Test*`; `Program` e
`ProgramCov` nunca o emitem, logo **nunca poem `tk_test_capturing` a 1**, logo `tk_panic` e `tk_exit`
comportam-se byte a byte como hoje.

**A fronteira e estrutural: nao ha bandeira que a possa desligar por engano, porque a unica coisa que
liga a captura e codigo que so o modo teste emite.**

Por isso as filas cujo contrato **E** o codigo de saida continuam intactas: `Given source` +
`When built and run` + `Then exit = N` compila em modo `Program`. Os alvos cruzados 42/210/7 e o par
oraculo do degrau 30 (cinco filas, medidas pelo integrador linha a linha) medem o observavel que
existem para medir, e capturar ali apagava-o. `defer_cascade_exit` (`Then exit = 235`,
`regressor.tkr:228,234`) e o caso mais afiado disto — o codigo de saida **e** a prova LIFO — e
continua a sair pela syscall.

### 14.6 A guarda, e a inversao por reversao

Tres bracos, e o segundo e o que o dono pediu por nome.

```teko
#test
/**
 * tc_a_test_that_exits_is_reported_and_the_suite_survives — o caso que hoje mata a suite.
 *
 * `exit(7)` no meio de um corpo tem de voltar como `Exited { code = 7 }`, sem syscall, e o arnes tem
 * de continuar. Sem o mecanismo este `#test` nao FALHA: o processo desaparece e nenhum dos seguintes
 * chega a ser reportado — que e a diferenca entre um teste vermelho e uma suite muda.
 *
 * @throws quando a saida nao volta capturada com o seu valor
 */
fn tc_a_test_that_exits_is_reported_and_the_suite_survives() {
    let e = teko::test::run_capturing(tc_body_that_exits_7)
    teko::assert::is_true(e.how == TestHow::Exited)
    teko::assert::eq_i64(e.code to i64, 7)
}

#test
/**
 * tc_the_suite_reached_this_test — A PROVA DE QUE A SUITE SOBREVIVEU, e ela tem de ser um teste
 * SEPARADO e POSTERIOR.
 *
 * Um teste nao pode afirmar credivelmente que a suite lhe sobreviveu; so o SEGUINTE o pode. Se a
 * captura regredir, este nunca e reportado — e um teste que nao aparece no sumario e uma falha que o
 * §13 nomeia (`never-ran`), nunca um verde.
 */
fn tc_the_suite_reached_this_test() {
    teko::assert::is_true(tc_previous_test_was_recorded())
}

#test
/**
 * tc_a_panicking_body_comes_back_too — o outro estrangulamento, incluindo o panic IMPLICITO.
 *
 * @throws quando um panic explicito ou uma divisao por zero nao voltam como `Panicked`
 */
fn tc_a_panicking_body_comes_back_too() {
    teko::assert::is_true(teko::test::run_capturing(tc_body_that_panics).how == TestHow::Panicked)
    teko::assert::is_true(teko::test::run_capturing(tc_body_that_divides_by_zero).how == TestHow::Panicked)
}
```

**A inversao e por REVERSAO, e sem interruptor** — o mesmo padrao que fechou o degrau 29. Um botao
`TEKO_TEST_CAPTURE=0` seria uma maneira nova de partir a arvore; a reversao honesta ja existe na
linguagem, e chama-se modo `Program`. Duas filas no canal **existente** `own_native`, sobre a **mesma
fonte**:

```
  Scenario: capture_off_in_program_mode_still_exits_for_real
    Given source = "cases/capture_exit_7.tks"
    When built and run
    Then exit = 7

  Scenario: capture_on_in_test_mode_reports_and_continues
    Given source = "cases/capture_exit_7.tks"
    Given args = ["--as-test"]
    When built and run
    Then stdout pattern = "scenario capture_on_in_test_mode_reports_and_continues: ok"
```

A primeira fila **e** a inversao: prova que o mecanismo esta desligado fora do modo teste — logo, que
ele esta LIGADO dentro dele por decisao e nao por acidente, e que a lei de §14.5 e observavel. A
segunda prova o relato e a continuacao. **A guarda pode falhar nos dois sentidos**, que e a unica
forma de nao ser decoracao.

### 14.7 O crumb, e ele vai a frente

**C0 — a captura.** `tk_test_run` + os dois estrangulamentos + `tk_test_report`/`tk_test_summary`/
`tk_test_any_failed` em `teko_rt.{c,h}`; `emit_test_call`/`emit_test_main` em
`src/codegen/codegen.tks`; `teko::test::run_capturing` em `src/test/test.tks`; a guarda e as duas
filas de reversao.

**Vai antes de tudo o resto**, e nao so porque o dono disse *"imediatamente, a dor e latente"*: vai
antes porque **encolhe os crumbs que vem depois**. O C4b dissolve-se no C4, o `never-ran` deixa de
precisar de reconstrucao, o `.tkcov` passa a ser despejado sempre e o C4 deixa de perseguir o caso
comum. **Total: 10 -> 10 crumbs.** O adendo de §13 tinha custado um; o ruling do dono devolve-o.

Ordem final: **C0** · C1 · C2 · C3 · C4 (com §13 e o antigo C4b dentro) · C5 · C6 · C7 · C8 · C9.

**Ponto de ritual novo, e obrigatorio: depois do C0.** Ele muda o arnes de **todos** os 1167 `#test`.

### 14.8 O que a captura NAO cobre

1. **Aborto externo.** `SIGKILL`, o OOM killer, um ecra azul, falta de energia. Palavra do dono:
   *"o software nao tem que lidar com isso, tem que conviver e fazer o melhor para se proteger"* — e
   e para isso que o journal (C1–C9) continua a existir, agora no seu lugar certo.
2. **Sinal de crash dentro de um teste** (SIGSEGV por memoria `unsafe`). `tk_rt_crash_handler` corre
   em contexto de sinal e um `longjmp` de la e formalmente indefinido. Fica como aborto externo: o
   manipulador journaliza (C5) e o processo morre. E a fronteira certa — um SIGSEGV nao e o programa
   a sair, e o programa partido.
3. **Recursos nao-memoria que um teste que panica tenha aberto** (descritores, ficheiros). A arena
   rebobina (§14.2, ponto 4); descritores nao. Nomeado como limite conhecido, nao escondido.
4. **Isolamento entre testes.** A captura faz a suite CONTINUAR; nao faz cada teste correr numa
   moldura sua. Estado global que um teste corrompeu antes de panicar continua corrompido para o
   seguinte — isso e `teko::isolate` (S8), e continua a nao existir. A diferenca em relacao ao que eu
   escrevi antes e que agora isto e um limite estreito e nomeado, em vez da desculpa para deixar a
   suite morrer.

---

## 15. O tunel e um TUBO, e o laco que o drena — directiva do dono, 2026-07-30

> *"Para processos eu ja falei, e direcionar um stdout e stderr virtual para o processo, por isso um
> orquestrador, para ler estes canais e apendar. Algo burro mas extremamente eficiente e eficaz."*
>
> *"Para o caso de 'espera' e `loop {}`, sempre lendo a saida enquanto ela estiver aberta... ha muita
> coisa simples e ja ao nosso alcance que resolve o paradigma sem criar um monstro."*

### 15.1 O tunel ja existe — e esta feito de FICHEIROS. Parte do §3 nao migra: DESAPARECE.

`spawn_redirected(argv, dir, env, in_path, out_path, err_path)` recebe **caminhos**. Medido em
`teko_rt.c:2336-2350`: o PAI abre os tres com `open(..., O_WRONLY|O_CREAT|O_TRUNC)`, faz `fork`, e o
FILHO faz `dup2` deles sobre os seus proprios fluxos antes do `execvp`.

Dai vem uma fatia inteira do inventario de §3: o trio `.in`/`.out`/`.err` **por filho lancado**. Ele
nasce num sitio so — `spawn_spec` (`regression.tks:182-186`) — e por isso **morre num sitio so**.

**Correccao ao §3, e e uma subtraccao e nao uma migracao:** com tubos, o par `.out`/`.err` de cada
filho **deixa de existir**. Nao ganha carimbo, nao ganha raiz, nao ganha nada — o **descritor E o
segmento**. Isso apaga o par em todas as familias de captura de §3.2/§3.3 de uma vez (as seis
`.regr-pool-*` — as 56 entradas que o integrador contou —, as duas `pt-probe`, as duas
`c4_test_probe`, `.toolquery`, `.compile`, `.run`, `.dep_compile`, `.wellformed`, e a base de shard).

**O `.in` FICA, e e escolha e nao esquecimento.** Um tubo de stdin traz a segunda direccao do
impasse de §15.3: o pai a escrever mais do que o buffer para um filho que ainda nao le. O
`stdin_data` e conhecido de antemao e escreve-se uma vez; um ficheiro nunca bloqueia. Trocar dois
ficheiros por filho e ficar com uma direccao de impasse em vez de duas e o negocio certo, e e
exactamente o *"algo burro mas extremamente eficiente"*.

O `.chan` tambem fica: e o TERCEIRO canal, e ha exactamente tres fluxos herdaveis em POSIX e em
Windows. Ele e o segmento de journal (§2.2) e sempre foi essa a sua razao.

### 15.2 A peca que falta e UMA: `pipe`

Medido: `fork` e `dup2` estao la (`teko_rt.c:2344-2350`), `_dup2` do lado Windows (`:2253`).
**`pipe` nao existe em lado nenhum do runtime.** E a unica peca nova, mais `CreatePipe` do outro lado.

```c
// tk_rt_spawn_piped — como tk_rt_spawn_redirected, mas para cada fluxo cujo caminho venha VAZIO
// cria um tubo em vez de abrir um ficheiro. Devolve o pid; os descritores do lado do PAI ficam na
// tabela de filhos vivos, lidos por tk_rt_child_fd.
//
// A TABELA E PROCESSO-LOCAL E E O ORQUESTRADOR QUE O DONO DESCREVE. Ela e o unico estado partilhado
// que isto acrescenta, e quando as raias chegarem protege-se com UM mutex — exclusao DENTRO do
// processo, segmento por escritor FORA dele, que e a regra que este documento ja segue.
int64_t tk_rt_spawn_piped(tk_str payload);
// tk_rt_child_fd — o descritor de leitura do fluxo `which` (1 = stdout, 2 = stderr) do filho `pid`.
int64_t tk_rt_child_fd(int64_t pid, int32_t which);
// tk_rt_wait_readable — bloquear ate que ALGUM dos `n` descritores tenha bytes ou EOF, ou ate
// `timeout_ms`. Devolve o indice do primeiro pronto, -1 no timeout. poll(2) / WaitForMultipleObjects.
//
// ESTE E O `loop {}` DO DONO, e e o que torna a espera uma LEITURA em vez de uma paragem.
int64_t tk_rt_wait_readable(tk_str fds_packed, int32_t n, int32_t timeout_ms);
// tk_rt_read_fd — ler ate `cap` bytes de `fd`. 0 = EOF, negativo = erro.
int64_t tk_rt_read_fd(int64_t fd, tk_str into, int64_t cap);
```

### 15.3 O PERIGO, e e o unico, e trocar ficheiros por tubos INTRODU-LO

Um ficheiro nunca bloqueia quem escreve. **Um tubo bloqueia.** O buffer e finito (tipicamente 64 KiB
no Linux), e os nossos filhos imprimem muito: um `teko test` de shard despeja a transcricao inteira.

Portanto: **um filho que encha o tubo PARA. Um pai parado no `wait_one` nunca le. Impasse.**

Isto **nao pode acontecer hoje** — os destinos sao ficheiros. **A troca para tubos e que o cria**, e
digo-o assim em vez de vender o tubo como um ganho sem contrapartida: o tubo troca dois ficheiros por
filho por uma **disciplina de drenagem obrigatoria**. A disciplina e uma linha de invariante:

> **NUNCA `wait_one` antes de EOF nos dois tubos do filho.** Espera-se a LEITURA, nunca o processo.

```teko
/**
 * pump — o laco do orquestrador: drenar TODOS os filhos vivos enquanto algum tubo estiver aberto, e
 * so depois recolher os estados.
 *
 * A ORDEM E O INVARIANTE INTEIRO, e ela e ao contrario do que parece natural: NAO se espera o filho
 * e depois se le o que ele deixou. Espera-se pela LEITURA. Um filho que encha o buffer do tubo fica
 * bloqueado no seu `write`, e um pai parado em `wait_one` nunca chega a esvaziar o buffer que o
 * desbloquearia — impasse dos dois lados, sem nada que o resolva.
 *
 * E O MESMO LACO QUE DA A AGREGACAO INCREMENTAL: cada bloco lido e apendado ao segmento do seu
 * escritor no momento em que chega, logo uma morte externa do orquestrador deixa em disco tudo o que
 * chegou ate ali. Uma peca, dois problemas.
 *
 * @param hs     os filhos em voo
 * @param sinks  o segmento de journal de cada filho, na mesma ordem
 * @param deadline_ms  quanto tempo o laco pode ficar sem progresso antes de desistir; 0 = sem limite
 * @return       o estado de saida de cada filho, em ordem de indice (nunca de chegada)
 */
pub fn pump(hs: []ProcHandle, sinks: []Journal, deadline_ms: i32): []i32
```

### 15.4 O `ProcHandle` ganha os descritores e o evento de fecho

*"Melhorar a estrutura do `Process` com evento de fecha"* e **estender uma struct**, nao inventar um
paradigma:

```teko
/**
 * ProcHandle — o filho em voo: a referencia do SO, os tubos por onde ele fala, e se ja se calou.
 *
 * @since 0.3.1.2
 */
pub type ProcHandle = struct {
    /** a referencia opaca do host (pid em POSIX, um HANDLE em Windows). */
    raw: i64
    /** o descritor de leitura do stdout do filho; -1 quando esse fluxo foi para um ficheiro. */
    out: i64
    /** o descritor de leitura do stderr do filho; -1 quando foi para um ficheiro. */
    err: i64
    /**
     * O EVENTO DE FECHO: verdadeiro quando os dois tubos deram EOF.
     *
     * E ESTA a condicao que autoriza o `wait_one`, e nao o inverso. Enquanto for falso, `wait_one`
     * pode ficar a espera de um filho que esta ele proprio a espera do pai (§15.3).
     */
    closed: bool
}
```

`wait_one` mantem a assinatura e ganha uma pre-condicao documentada. Nenhum chamador de hoje quebra:
um `ProcHandle` com `out = -1` e `err = -1` e exactamente o comportamento actual.

### 15.5 A prova por impasse forcado

Nao provo com um filho que imprime pouco — isso passa nas duas ordens. Provo com um filho que
**enche o buffer**, e o braco "antes" tem de bater no prazo.

```teko
#test
/**
 * pp_a_child_that_fills_the_pipe_is_drained — o filho escreve 200 000 bytes, mais do triplo do
 * buffer tipico de 64 KiB, e o laco recupera-os INTEIROS.
 *
 * Um filho que imprima pouco passa nas duas ordens e nao prova nada; e por isso que este escreve mais
 * do que o buffer. A afirmacao e sobre o TAMANHO recuperado: um impasse trunca, um laco correcto nao.
 *
 * @throws quando algum byte se perde ou o laco nao termina dentro do prazo
 */
fn pp_a_child_that_fills_the_pipe_is_drained() {
    let seg = pp_segment_of(pp_run_drained(pp_child_writing(200000), 5000))
    teko::assert::eq_u64(seg.len, 200000)
}

#test
/**
 * pp_waiting_before_draining_deadlocks — A INVERSAO, e ela tem de PODER falhar e nao pode PENDURAR.
 *
 * A ordem errada — `wait_one` primeiro, ler depois — nao "corre mais devagar": nao termina. Um teste
 * que pendura nao e um teste, logo a inversao corre com PRAZO e afirma que o prazo foi atingido. Sem
 * este braco a guarda nao prova nada, e tres guardas cegas ja nos deram ZERO hoje.
 *
 * @throws quando a ordem errada TERMINA — o que significaria que esta prova deixou de provar
 */
fn pp_waiting_before_draining_deadlocks() {
    teko::assert::is_true(pp_run_wait_first(pp_child_writing(200000), 5000).timed_out)
    teko::assert::is_false(pp_run_drained(pp_child_writing(200000), 5000).timed_out)
}
```

### 15.6 O `verdict_emit`: o destino passa a PARAMETRO, e o desviante e o ficheiro

O integrador tem razao na leitura: os dois destinos ja la estao, mas como um `or` — e **o ramo do
`stderr` ja apenda** (`eprintln` escreve num fluxo). O desviante e o ramo do ficheiro, que le tudo e
reescreve tudo (`process.tks:215-220`). A correccao nao e acrescentar um destino: e **pôr o ramo do
ficheiro a apendar como o do `stderr` ja apenda**, e entao o destino deixa de ser um `or` e passa a
ser um argumento.

```teko
/**
 * Sink — para onde um registo vai. Um PARAMETRO, nunca um `or` decidido dentro do emissor.
 *
 * @since 0.3.1
 */
pub type Sink = enum { Stderr; Segment }

/**
 * emit_to — apendar UM registo ao destino nomeado.
 *
 * OS DOIS RAMOS APENDAM, que e o que o `or` de hoje ja fazia de um lado e nao do outro. O destino ser
 * argumento e o que permite ao orquestrador escrever no segmento do FILHO que acabou de ler, em vez
 * de cada processo adivinhar sozinho onde escreve.
 *
 * @param sink  para onde
 * @param seg   o segmento, quando `sink` e `Segment`
 * @param line  o registo, sem quebra de linha
 * @throws      quando a escrita falha (§4, modo 5 — a falha e pegajosa)
 */
pub fn emit_to(sink: Sink, seg: Journal, line: str): null | error
```

### 15.7 `chan<T>`: o primeiro consumidor real, registado e NAO construido

> **RECTIFICADO POR §16 (correccao do dono, 2026-07-30).** O requisito 1 desta tabela esta
> **REFUTADO** — eu desenhei um laco central a esperar em `2 x jobs` origens, e a forma e FAN-IN: um
> tubo por constructo, um handler por filho, e o orquestrador com um `recv` so. A tabela fica com a
> linha riscada para o erro ser legivel; a lista corrigida esta em §16.4.

`docs/design/concorrencia-adiantada-s8.md` §3.3 reservou `chan<T>` e recusou congela-lo com uma razao
explicita: *"Congelar `chan<T>` agora seria congelar a peca que os casos reais nao usam."*

**Agora ha um caso real, e e o `pump`.** Construo-o com tubo e descritor, **nao** com `chan<T>` —
usar `chan<T>` hoje obrigava a construir a primitiva primeiro, e isso e o monstro que o dono nao
quer. Mas registo aqui **a forma que o uso mediu**, que e a evidencia que o S8 pediu antes de
congelar:

| requisito medido no `pump` | porque, e o que reprova |
|---|---|
| ~~**espera multi-origem**~~ **REFUTADO (§16)** | eu escrevi que um `recv` de uma origem so nao exprimia o caso. Exprime — a multiplicidade vive nos HANDLERS, cada um bloqueado no SEU tubo, e o orquestrador tem uma origem so. A peca que eu dava como o requisito dificil **nao existe**, e com ela cai o `poll(2)`/`WaitForMultipleObjects` e a assimetria do Windows que eu declarara por medir |
| **fecho distinguivel de vazio** | `recv` tem de responder "fechado" diferente de "nada agora", porque e o fecho que autoriza o `wait_one` (§15.4). `-> T \| closed` |
| **limitado, com contrapressao** | o tubo do SO da-a de graca: quem escreve bloqueia. Um `chan<T>` ilimitado tem a memoria como unico limite — e nos morremos de OOM duas vezes hoje. **Limitado, por omissao** |
| **ordem por origem, nao global** | o relatorio e por indice de filho (§determinismo do `run_pool`). O canal so tem de preservar a ordem DENTRO de cada origem; uma ordem global seria uma promessa mais cara e inutil |

Quatro requisitos vindos de necessidade, nenhum de antecipacao. **Nao construo nenhum aqui.**

### 15.8 O custo, e o que isto muda nos crumbs

**Nao acrescenta crumb.** O tubo e o laco sao a implementacao do **C1** (o sumidouro do escritor) e do
**C4** (a releitura, que passa a ser drenagem incremental). O que muda e o CONTEUDO:

* **C1** ganha `tk_rt_spawn_piped`/`tk_rt_child_fd`/`tk_rt_wait_readable`/`tk_rt_read_fd`, o
  `ProcHandle` estendido e o `emit_to`;
* **C3** ENCOLHE: o par `.out`/`.err` de cada familia de captura nao migra — some;
* **C7** ganha a prova por impasse forcado de §15.5;
* **C0** fica como esta.

**Total: 10 crumbs, sem alteracao.**

### 15.9 O que isto NAO cobre

1. **O `stdin` continua ficheiro**, por decisao (§15.1) — uma direccao de impasse em vez de duas.
2. **Um filho que feche stdout/stderr e continue a correr** da EOF sem ter saido. O laco entao espera
   no `wait_one`, correctamente, e sem nada por ler. Nao e impasse; e um filho lento.
3. **`chan<T>` nao e construido aqui**, so medido (§15.7).
4. **Windows nao esta medido.** `CreatePipe` + `WaitForMultipleObjects` sao a forma equivalente e
   estao declarados, mas eu nao os corri. A perna Windows tem de trazer a sua propria medicao — e
   dizer que "deve funcionar igual" seria a suposicao que este documento inteiro existe para nao
   fazer.

---

## 16. FAN-IN: um tubo por constructo — correccao do dono, 2026-07-30

> *"Nao e 'Um' tubo, mas um por constructo, e isso e o pq de ter um orquestrador e operar com
> `chan<>`, cada processo recebe um handler que le a saida e grava no canal, o orquestrador le do
> canal e apenda no arquivo."*

**O erro e meu e e de forma.** A §15.3 desenhou um **laco central** a esperar em `2 x jobs` origens
com `poll(2)`. A forma e **fan-in**:

```
filho 1 ──tubo──▶ handler 1 ─┐
filho 2 ──tubo──▶ handler 2 ─┼──▶ chan ──▶ orquestrador ──▶ apenda no segmento
filho N ──tubo──▶ handler N ─┘
```

Um tubo **por constructo**, um handler **por filho** que drena o SEU tubo e grava no canal, e o
orquestrador com **um `recv` so**.

### 16.1 O que a correccao apaga do meu desenho

| eu tinha | a forma do dono |
|---|---|
| `tk_rt_wait_readable` (`poll(2)` / `WaitForMultipleObjects`) | **nao existe.** Cada handler faz um `read(2)` bloqueante no seu tubo |
| um laco central com `2 x jobs` descritores | **um `recv`.** A multiplicidade vive nos produtores |
| "Windows precisa de forma equivalente e nao esta medido" | **desaparece.** `ReadFile` bloqueante e simetrico com `read(2)`; nao ha `WaitForMultipleObjects` |
| o invariante da drenagem no laco central | **no handler.** Mesmo invariante, outra casa |

**A parte que eu dava como o requisito dificil evapora-se.** Isso e o sinal de que a forma esta certa:
a que eu propus precisava da primitiva mais assimetrica entre os dois SO, e esta nao precisa dela.

### 16.2 A DEPENDENCIA, dita e nao contornada

**A forma do dono exige `spawn` e `chan<T>`. Nenhuma das duas existe.** Sao duas das cinco primitivas
RESERVADAS de `docs/design/concorrencia-adiantada-s8.md` §3.3, e nao ha por onde fugir: um handler que
bloqueia num `read` precisa de **contexto de execucao proprio**, e um processo por handler seria
absurdo (dobrava a contagem de processos para drenar tubos).

E o S8 nomeia o que trava a primitiva GERAL, medido: `tk_arena_push(void)` / `tk_arena_pop(void)`
(`teko_rt.h:156-157`) **nao recebem parametro** — empilham marcas numa pilha global sobre a regiao
raiz do processo. Duas raias a faze-lo corrompem-se. O pre-requisito real da S8 e **regiao raiz por
tarefa**, e o proprio S8 classifica-o: *"e caro e toca a disciplina de memoria inteira"*.

### 16.3 Porque esse bloqueio NAO trava ESTE caso — e e a medicao que o desbloqueia

**O corpo de um handler de dreno nao aloca da arena.** Ele le bytes de um descritor e empurra-os para
um canal. Verificado: `tk_str_concat` e a familia de strings usam **`malloc`**, nao a arena
(`teko_rt.c:139`; o proprio comentario de observabilidade chama-lhes *"dark matter ... outside the
arena"*), e `malloc` e seguro em threads nas duas libc que usamos.

Portanto o **minimo viavel** nao e a primitiva geral. E isto:

> **O corpo do handler vive no RUNTIME, em C, e nunca chama codigo Teko.** Zero risco de arena, zero
> mudanca no codegen, zero mudanca no verificador.

```c
// tk_chan — a fila MPSC de REGISTOS que o fan-in precisa: N produtores, UM consumidor, limitada.
//
// REGISTOS E NAO UM FLUXO DE BYTES, e esta e a descoberta desta forma: se o canal transportasse
// bytes, dois filhos entrelacavam-se num fluxo so e a ATRIBUICAO perdia-se — que e exactamente o
// defeito que este documento inteiro existe para fechar, reencarnado uma camada acima. Cada registo
// traz o seu escritor.
typedef struct tk_chan tk_chan;
// tk_chan_open — abrir um canal com `cap_bytes` de folga. LIMITADO por lei (§15.7): um canal sem
// limite tem a memoria como unico travao, e nos morremos de OOM duas vezes no dia deste desenho.
tk_chan *tk_chan_open(uint64_t cap_bytes, uint32_t producers);
// tk_chan_recv — bloquear ate haver um registo. Devolve 0 no registo, 1 quando TODOS os produtores
// fecharam. A distincao entre "vazio agora" e "fechado" e o que autoriza o wait_one (§15.4).
int32_t tk_chan_recv(tk_chan *c, tk_str *writer_out, tk_str *bytes_out);
// tk_rt_spawn_drainer — um handler: uma thread cujo corpo e ESTA funcao C — `read(fd)` ate EOF,
// empurrando cada bloco para `c` com o rotulo `writer`, e no fim baixa a contagem de produtores.
//
// NAO E `teko::isolate::spawn`. Nao corre codigo Teko, nao toca na arena, nao tem valor de retorno
// que o mundo seguro veja. E o fundo sobre o qual `spawn` VAI assentar, e nao a primitiva.
int32_t tk_rt_spawn_drainer(int64_t fd, tk_chan *c, tk_str writer);
```

O custo, em linhas de C mantido, e para ser comparado com o que ja ha:

| peca | POSIX | Windows | forma |
|---|---|---|---|
| `tk_chan` (anel + mutex + duas variaveis de condicao + contagem de produtores) | ~120 | ~120 | `pthread_mutex`/`pthread_cond` · `CRITICAL_SECTION`/`CONDITION_VARIABLE` |
| `tk_rt_spawn_drainer` | ~40 | ~40 | `pthread_create` · `CreateThread` |
| `tk_rt_spawn_piped` + `tk_rt_child_fd` (§15.2) | ~50 | ~50 | `pipe` · `CreatePipe` |

**~210 linhas de C mantido, simetricas entre os dois SO** — contra *"caro e toca a disciplina de
memoria inteira"* da regiao raiz por tarefa, que este caminho **nao precisa de tocar**.

### 16.4 A superficie em Teko, e o orquestrador com UM `recv`

```teko
/**
 * Chan — a fila de registos por onde os handlers falam com o orquestrador.
 *
 * @since 0.3.1
 */
pub type Chan = struct {
    /** a referencia opaca do runtime. */
    raw: i64
}

/**
 * ChanMsg — um registo tirado do canal: de QUEM veio e o que ele disse.
 *
 * O ROTULO VIAJA COM OS BYTES, nunca ao lado. Um canal de bytes puros entrelacava dois filhos num
 * fluxo so e perdia a atribuicao — o mesmo defeito deste documento, uma camada acima.
 *
 * @since 0.3.1
 */
pub type ChanMsg = struct {
    /** a identidade do escritor (`s2`, `r0`) — a mesma chave do segmento de journal (§2.2). */
    writer: str
    /** o bloco de bytes que o handler leu, na ordem em que o leu. */
    bytes: str
}

/**
 * drain_into — pôr um handler a drenar cada tubo de `h` para `c`.
 *
 * UM POR CONSTRUCTO, e e por isso que isto recebe UM `ProcHandle` e nao a lista: a multiplicidade
 * vive aqui, num handler por fluxo, e nao no consumidor.
 *
 * @param h       o filho cujos tubos vao ser drenados
 * @param c       o canal para onde os blocos vao
 * @param writer  a identidade com que os blocos deste filho sao rotulados
 * @throws        quando o handler nao pode ser criado (limite de threads)
 * @since 0.3.1
 */
pub fn drain_into(h: ProcHandle, c: Chan, writer: str): null | error

/**
 * recv — tirar o proximo registo do canal, bloqueando ate haver um.
 *
 * UMA ORIGEM SO, e e a rectificacao de §15.7: eu tinha escrito que o caso exigia espera
 * multi-origem. Nao exige. Cada handler bloqueia no SEU tubo; aqui chega um `recv` — e com ele cai o
 * `poll(2)`, cai o `WaitForMultipleObjects` e cai a assimetria de Windows que eu declarara por medir.
 *
 * @param c  o canal
 * @return   o proximo registo, ou `closed` quando TODOS os produtores fecharam
 * @since 0.3.1
 */
pub fn recv(c: Chan): ChanMsg | closed
```

E o orquestrador inteiro, que e o ponto do *"algo burro mas extremamente eficiente"*:

```teko
/**
 * orchestrate — o consumidor: um `recv`, um `append`, ate todos os produtores fecharem.
 *
 * O INVARIANTE DA DRENAGEM MUDOU DE CASA, NAO DESAPARECEU (§15.3). Ele vive agora no handler, que le
 * o seu tubo ate EOF e so entao baixa a contagem. Aqui a regra e a irma dele, e e a UNICA coisa que
 * este laco tem de respeitar:
 *
 *   **o orquestrador nunca bloqueia em nada que nao seja `recv`.**
 *
 * Se ele parasse num `wait_one` a meio, os handlers encheriam o canal limitado e parariam — o mesmo
 * impasse de §15.3, uma camada acima. Por isso o `wait_one` de cada filho corre DEPOIS de `closed`,
 * e nesse ponto o filho ja fechou os seus fluxos: a espera passa a ser limitada pela saida dele, e
 * nunca pela leitura do pai.
 *
 * @param c      o canal onde todos os handlers falam
 * @param sinks  o segmento de journal de cada escritor
 * @return       quantos registos foram apendados
 */
fn orchestrate(c: Chan, sinks: []Journal): u64 {
    mut n: u64 = 0
    loop {
        match recv(c) {
            ChanMsg as m => {
                match teko::journal::append(sink_of(sinks, m.writer), "out", m.bytes) { error as e => return n; null => { } }
                n = n + 1
            }
            closed => break
        }
    }
    n
}
```

### 16.5 Os requisitos, CORRIGIDOS — o que este uso mede sobre `chan<T>`

| requisito | estado | o que o uso mediu |
|---|---|---|
| ~~espera multi-origem~~ | **REFUTADO** | o consumidor precisa de `recv` de origem UNICA; a multiplicidade e dos produtores |
| **fan-in de N produtores para 1 consumidor** | **NOVO, e e o requisito central** | e a forma inteira. Um `chan<T>` que so admita um produtor nao serve |
| **fecho POR PRODUTOR** | confirmado, e afinado | nao chega saber "fechado": e preciso que o canal conte os produtores e so diga `closed` quando o ultimo sair. E o que autoriza o `wait_one` sem impasse |
| **limitado, com contrapressao** | confirmado, e agora e OBRIGATORIO | com fan-in, N produtores enchem mais depressa do que um consumidor esvazia. Ilimitado = OOM, e nos morremos disso duas vezes |
| **ordem por origem, nao global** | confirmado, e agora e ESTRUTURAL | um handler por origem empurra na ordem em que leu; a ordem por origem sai de graca e a global continua a nao fazer falta |
| **registos, nao fluxo de bytes** | **NOVO** | sem o rotulo a viajar com os bytes, o fan-in entrelaca dois filhos e perde a atribuicao — o defeito deste documento, uma camada acima |

Seis linhas, **duas novas e uma refutada**, todas vindas de um caso real. O S8 recusou congelar
`chan<T>` porque *"seria congelar a peca que os casos reais nao usam"*. **O caso real chegou.** Isto e
a diferenca entre congelar por antecipacao e congelar por necessidade medida — e continuo a **nao**
congelar: registo a forma, construo o fundo minimo, e a primitiva geral e do dono.

### 16.6 A prova, com o segundo braco que a forma nova exige

A prova de §15.5 mantem-se (um filho que escreve 200 000 bytes, mais do triplo do buffer) e **ganha o
braco do consumidor**, porque o fan-in traz um segundo impasse possivel:

```teko
#test
/**
 * fi_n_children_fan_in_without_interleaving — N filhos a escrever muito ao mesmo tempo, e cada
 * segmento recebe os SEUS bytes inteiros e por ordem.
 *
 * O que isto apanha e o que um canal de bytes puros faria: os blocos chegam entrelacados no tempo
 * (e devem — sao concorrentes), mas o rotulo separa-os, e a ordem DENTRO de cada escritor tem de ser
 * a ordem de leitura.
 *
 * @throws quando algum segmento perde bytes ou os recebe fora de ordem
 */
fn fi_n_children_fan_in_without_interleaving() {
    let segs = fi_run_fan_in(4, 200000, 5000)
    teko::assert::eq_u64(fi_total_bytes(segs), 800000)
    teko::assert::is_true(fi_each_writer_is_in_read_order(segs))
}

#test
/**
 * fi_an_orchestrator_that_waits_mid_loop_deadlocks — O SEGUNDO BRACO, e ele so existe por causa do
 * fan-in: um canal LIMITADO com N produtores para num consumidor que se distrai.
 *
 * A inversao de §15.5 provava o lado do handler. Esta prova o lado do consumidor: um `wait_one` a
 * meio do laco enche o canal, para todos os handlers, e nada avanca. Corre com PRAZO, porque um
 * teste que pendura nao e um teste.
 *
 * @throws quando a ordem errada TERMINA — o que significaria que esta prova deixou de provar
 */
fn fi_an_orchestrator_that_waits_mid_loop_deadlocks() {
    teko::assert::is_true(fi_run_waiting_mid_loop(4, 200000, 5000).timed_out)
    teko::assert::is_false(fi_run_fan_in(4, 200000, 5000).timed_out)
}
```

### 16.7 O custo em crumbs, e o prazo

**Continua sem crumb novo.** O **C1** ja era "o sumidouro do escritor"; passa a incluir o `tk_chan`, o
handler de dreno e os tubos. O **C3** encolhe (§15.1). O **C7** ganha os dois bracos de impasse. O
**C0** fica.

**Total: 10 crumbs.** E o C1 cresce em conteudo — ~210 linhas de C mantido, simetricas entre POSIX e
Windows — sem tocar na regiao raiz por tarefa, que continua a ser a divida da S8 e continua a nao ser
paga aqui.

Prazo do dono: *"imediatamente, a dor e latente"*. O C0 e o C1 entram ja.

### 16.8 O que isto NAO e, dito para nao ser lido a mais

1. **Nao e `teko::isolate::spawn`.** O handler e uma thread cujo corpo e uma funcao C do runtime que
   nunca chama codigo Teko. Nao ha superficie de threads em Teko no fim disto.
2. **Nao e `chan<T>`.** E `tk_chan`: um canal de REGISTOS DE BYTES, sem tipo generico, interno ao
   runtime. Quando `chan<T>` for construido, este e o fundo sobre que assenta — nao e trabalho
   deitado fora, mas tambem nao e a primitiva.
3. **Nao resolve a regiao raiz por tarefa.** Contorna-a, e o contorno tem uma condicao verificavel: o
   corpo do handler nao aloca da arena. Se algum dia ele precisar de correr codigo Teko, a condicao
   cai e a divida da S8 volta inteira.
4. **Windows continua sem medicao minha.** A forma agora e simetrica (thread + leitura bloqueante dos
   dois lados), o que remove a assimetria que eu tinha declarado — mas simetrico no papel nao e
   medido, e a perna Windows tem de trazer a sua propria medicao.

---

## 17. A arena por tarefa — PRE-REQUISITO, nao divida — ruling do dono, 2026-07-30

> *"Mas ai nao resolve nada, o processo precisa funcionar tanto nativo quanto em C, sem excecoes."*

**A minha saida de emergencia esta invalidada, e a razao esta na arvore.** Escrevi em §16.3 que o
corpo do handler *"vive no runtime, em C, e nunca chama codigo Teko"*. `feat/issue-runtime-em-teko`
esta viva e `docs/design/romaneio-morte-do-c-031.md` lista `src/runtime/teko_rt.c` entre os ficheiros
que MORREM. As minhas ~210 linhas iam para dentro de um ficheiro com morte planeada, e a condicao que
suspendia a divida **cai por construcao** no dia em que o runtime for Teko. Nao e risco: e o objectivo
declarado desta lane. **Isso e o meio-termo que o dono proibiu.**

### 17.1 Primeiro a medicao, porque "caro" nao e um numero — e eu repeti um adjectivo

Classifiquei a arena por tarefa como *"cara e toca a disciplina de memoria inteira"*. **Essa frase e
do `concorrencia-adiantada-s8.md` e eu repeti-a sem a medir.** Fui medir. E menor do que o adjectivo,
e esta concentrada num ficheiro so.

**As variaveis que uma segunda tarefa corrompe** (todas em `src/runtime/teko_rt.c`):

| # | variavel | linha | porque corrompe |
|---:|---|---:|---|
| 1 | `tk_g_regs` | 1149 | registo intrusivo de regioes vivas; duas tarefas a prepender perdem nos |
| 2 | `tk_g_root` | 1150 | a propria regiao raiz — o comentario ja diz *"single-threaded seed (S8 revisit)"* |
| 3 | `tk_arena_marks[64]` | 1551 | a pilha de marcas: o `pop` de A liberta o que B usa |
| 4 | `tk_arena_msp` | 1552 | o topo dessa pilha |
| 5 | `tk_push_cache[]` | 2773 | cache de cauda viva, chaveada por regiao+geracao — um falso acerto escreve em memoria alheia |
| 6 | `tk_free_bins[]` | 1168 | bins da lista livre |
| 7 | `tk_free_large` | 1169 | lista livre grande |
| 8 | `tk_free_parked_bytes` + `_reused_bytes` + `_reused_count` | 1170 | contadores da lista livre |

**A que NAO pode ser por tarefa, e e a armadilha do "poe tudo em thread-local":**

| # | variavel | linha | porque |
|---:|---|---:|---|
| 9 | `tk_g_region_gen` | 1151 | a geracao tem de ser unica **entre tarefas**, nao dentro de cada uma. Por tarefa, duas tarefas emitem a mesma geracao e o `tk_push_cache` volta a falsear. **Fica global e passa a atomica.** |

**As tres familias com a mesma patologia, fora da arena:**

| # | familia | linha | nota |
|---:|---|---:|---|
| 10 | `tk_fn_stack`/`tk_fn_sp`/`tk_fn_cap` | 2568-2570 | pilha de atribuicao de cobertura — por tarefa |
| 11 | tabelas `tk_obs_*` | 1236-1254 | observabilidade — por tarefa, ou atomicas |
| 12 | `tk_test_jb` / `tk_test_capturing` (§14) | C0 | **por tarefa, obrigatoriamente**: um `panic` numa tarefa que saltasse para a moldura de outra e pior do que o `abort` que substitui |

**TOTAL: 12 familias de variavel, num ficheiro so.**

**E o numero que decide o custo e o outro — o que NAO muda:**

| simbolo | referencias | muda? |
|---|---:|---|
| `tk_alloc` | 54 | **nao** |
| `tk_region_alloc` | 41 | **nao** |
| `tk_region_new` | 35 | **nao** |
| `tk_region_drop` | 34 | **nao** |
| `tk_region_root` | 27 | **nao** |
| `tk_arena_pop` | 20 | **nao** |
| `tk_regions_free_all` | 17 | **nao** |
| `tk_arena_push` | 12 | **nao** |
| `tk_region_register` / `tk_region_lookup` | 19 | **nao** |

**~259 referencias, e nenhuma muda de forma** — porque as assinaturas ficam identicas e passam a ler
`tk_task_current()` por dentro. E por isso que as 64 referencias em `src/codegen/codegen.tks` (que sao
literais de C emitido) tambem nao mudam: o C emitido continua a dizer `tk_arena_push();`.

**A conta honesta: 12 declaracoes movem-se, ~259 chamadas ficam.** O custo esta concentrado, nao
espalhado — o contrario do que o adjectivo que eu repeti dizia.

### 17.2 A pergunta central: uma tarefa com raiz propria numa linguagem sem tarefas

`_Thread_local` resolve o C de hoje e **nao** resolve o nativo. A resposta que serve as duas
encarnacoes nao e uma tecnica — e uma **costura**:

> **Todo o estado da tabela de §17.1 colapsa num `tk_task`, e ha UM acessor: `tk_task_current()`.
> A costura entre as duas encarnacoes e essa funcao, e so essa.**

```c
// tk_task — TODO o estado que uma tarefa nao partilha: a sua regiao raiz, a sua pilha de marcas, a
// sua cache de cauda, as suas listas livres, o seu buffer de captura (§14).
//
// UMA STRUCT E NAO DOZE VARIAVEIS, e e isso que torna a costura possivel: com doze globais haveria
// doze acessos a portar; com uma, ha um.
typedef struct tk_task tk_task;
// tk_task_current — a tarefa que corre AGORA. A UNICA linha do runtime que sabe o que e uma thread.
tk_task *tk_task_current(void);
```

E as duas encarnacoes da mesma funcao:

| encarnacao | implementacao | custo de backend |
|---|---|---|
| **C de hoje** | `static _Thread_local tk_task *tk_g_task;` — uma linha (C11, e a arvore ja usa `_Noreturn`) | nenhum |
| **Teko nativo de amanha** | `pthread_getspecific` / `TlsGetValue`, ligados por `extern fn` — a FFI que ja existe | **nenhum** |

**E aqui esta a resposta a objeccao, e ela e o ponto inteiro:** a versao nativa **nao precisa de
nenhuma capacidade nova do backend proprio.** `pthread_getspecific` e uma chamada C ordinaria a um
simbolo externo — exactamente o que `extern fn` ja emite hoje, dezenas de vezes. Nao ha relocacao de
TLS por inventar, nao ha registo reservado, nao ha modelo de armazenamento por escolher.

E se um dia se quiser o acesso mais rapido (relocacoes TLS reais no emissor ELF/COFF), **troca-se uma
funcao** e nenhuma das ~259 chamadas sabe. Isso e a diferenca entre uma costura e uma saida de
emergencia: a costura tem duas implementacoes hoje e admite uma terceira amanha; a saida tinha uma so
e expirava.

**O custo que eu NAO medi, e digo-o:** quanto custa um `pthread_getspecific` por `tk_alloc` nesta
arvore. A mitigacao esta desenhada — `tk_alloc_in(tk_task *, size)` com `tk_alloc` a ser o invocador
de uma consulta so, e os ciclos quentes (`tk_slice_push`) a buscarem uma vez — mas **o numero e a
primeira coisa do crumb**, e se ele desmentir a forma, e a medicao que fica.

### 17.3 A resposta a pergunta de ordenacao, e ela e SIM, trava

**O fan-in NAO pode entrar antes da arena por tarefa.** Digo-o em vez de arranjar outra condicao.

A razao e estrutural e sobrevive ao romaneio: um handler de dreno, **assim que o runtime for Teko**,
aloca — qualquer codigo Teko aloca. E mesmo hoje, o empurrao de um bloco para o `tk_chan` copia bytes,
e uma copia que passe por `tk_alloc` toca a raiz partilhada. Fazer o fan-in primeiro e construir por
cima de uma suposicao com data de validade.

**Portanto entra um crumb, e ele e um PRE-REQUISITO:**

**C-A — a arena por tarefa.** As 12 familias de §17.1 colapsam em `tk_task`; `tk_g_region_gen` fica
global e passa a atomica; `tk_task_current()` com as duas encarnacoes; a guarda de §17.4.

**Ordem: C0 · C-A · C1 · C2 · C3 · C4 · C5 · C6 · C7 · C8 · C9. Total: 10 -> 11 crumbs.**

O **C0 continua a poder ir a frente**: a captura corre no arnes de uma shard que ainda e
single-threaded, e nao ha handler nenhum antes do C1. Mas o C-A **tem de mover `tk_test_jb` para
dentro do `tk_task`** quando aterrar, e isso esta na sua lista (familia 12) para nao se perder.

**Ponto de ritual: depois do C-A.** Ele mexe na disciplina de memoria do compilador inteiro, e o
fixpoint (gen1 == gen2) e o unico juiz que serve.

### 17.4 A guarda, e a regra que sobrevive

Mesmo com arena por tarefa sobra **uma** regra, e portanto sobra uma guarda: **nenhuma tarefa
rebobina ou liberta a regiao de outra.** Ela tem de poder falhar.

```teko
#test
/**
 * ta_two_tasks_do_not_share_a_root — duas tarefas alocam, uma rebobina, e os bytes da outra ficam
 * INTACTOS.
 *
 * @throws quando a rebobinagem de uma tarefa toca a canaria da outra
 */
fn ta_two_tasks_do_not_share_a_root() {
    let r = ta_run_two_tasks_with_canaries()
    teko::assert::is_true(r.other_canary_intact)
}

#test
/**
 * ta_the_guard_is_not_vacuous — O BRACO DE VIVACIDADE, e sem ele os outros dois nao valem nada.
 *
 * Se `tk_task_current()` devolvesse a MESMA tarefa as duas raias, o teste acima passaria por nao
 * haver duas coisas para corromper — verde sobre um instrumento cego, que e a patologia que ja nos
 * deu ZERO tres vezes nesta lane. Por isso afirma-se primeiro que as raizes sao DIFERENTES.
 *
 * @throws quando as duas tarefas observam a mesma raiz, ou a mesma geracao
 */
fn ta_the_guard_is_not_vacuous() {
    let r = ta_run_two_tasks_with_canaries()
    teko::assert::ne_i64(r.root_a, r.root_b)
    teko::assert::is_true(ta_all_generations_distinct(r.gens))
}

#test
/**
 * ta_a_shared_root_corrupts — A INVERSAO POR REVERSAO: a mesma canaria contra uma raiz PARTILHADA
 * (a forma de hoje) tem de ser destruida.
 *
 * Sem este braco a guarda so diz "nao vi problema", que e o que uma guarda cega tambem diz.
 *
 * @throws quando a forma partilhada NAO corrompe — o que significaria que esta prova nao prova
 */
fn ta_a_shared_root_corrupts() {
    teko::assert::is_false(ta_run_two_tasks_on_one_root().other_canary_intact)
}
```

A geracao tem a sua propria afirmacao (`ta_all_generations_distinct`) porque e a variavel que **fica**
global: duas tarefas a criar regioes em ciclo nunca podem observar a mesma geracao, e um incremento
nao-atomico colide de forma reprodutivel com duas raias e 100 000 voltas.

### 17.5 O que continua por resolver, e nao escondo

1. **`pthread_getspecific` por alocacao nao esta medido** (§17.2). E o primeiro passo do C-A.
2. **`tk_region_register`/`tk_region_lookup` (DI) sao por regiao**, logo seguem a raiz para dentro do
   `tk_task` — mas o S8 ja tinha reportado que um `#singleton` resolvido em duas tarefas e uma segunda
   corrida da mesma familia. Com a raiz por tarefa, dois `#singleton` passam a ser **duas instancias**,
   e isso e uma mudanca de SEMANTICA de DI, nao so de seguranca. **REPORTADO, nao decidido por mim:**
   e uma escolha do dono se um `#singleton` e por processo ou por tarefa.
3. **Windows continua sem medicao minha.** `TlsGetValue` e a forma simetrica de `pthread_getspecific`
   e a costura nao muda, mas simetrico no papel nao e medido.
4. **A arena por tarefa nao da threads a Teko.** Ela remove o que travava a S8; `spawn` e `chan<T>`
   continuam por construir, e o §16 continua a construir apenas o fundo minimo.

---

## 18. `chan<T>` e MPSC — a fronteira, fixada pelo dono (2026-07-30)

> *"`chan<T>` e fan-in, varios escritores, um leitor. Para ter um fan-out 'N:M', teria que ser outro
> tipo de estrutura, que ate pode operar sobre um `chan<T>` mas que precisaria de uma segunda
> estrutura capaz de broadcasting e multiplas copias."*

**Lei do desenho: `chan<T>` e MPSC. N escritores, UM leitor. Nao e MPMC, nao e difusao.**

### 18.1 Como o TIPO o impoe — e a resposta nao e "por convencao"

O tipo parte-se em dois, e os dois extremos nao sao o mesmo valor:

```teko
/**
 * Tx — o extremo de ESCRITA de um canal. Copiavel de proposito: os N escritores sao a metade que
 * pode ser multipla.
 *
 * @since 0.3.1
 */
pub type Tx = struct { raw: i64 }

/**
 * Rx — o extremo de LEITURA. UM, e a unicidade e a razao de ele existir como tipo separado.
 *
 * PORQUE UM TIPO E NAO UMA REGRA: com um `chan<T>` unico, "nao chames `recv` em duas tarefas" e uma
 * frase num comentario, e uma frase nao falha. Com o extremo separado, ter dois leitores exige ter
 * dois `Rx` — e passa a ser um ACTO explicito que o §18.2 faz falhar, em vez de um descuido que
 * ninguem ve.
 *
 * @since 0.3.1
 */
pub type Rx = struct { raw: i64 }

/**
 * open — abrir um canal e devolver os seus dois extremos.
 *
 * @param cap_bytes  a folga do anel; LIMITADO por lei (§15.7/§16.5)
 * @param producers  quantos escritores vao existir (o `recv` so diz `closed` quando o ultimo sair)
 * @return           o extremo de escrita (copiavel) e o de leitura (unico)
 * @throws           quando o canal nao pode ser criado
 * @since 0.3.1
 */
pub fn open(cap_bytes: u64, producers: u32): ChanEnds | error

/** send — empurrar um registo rotulado. N escritores, sem coordenacao entre eles. */
pub fn send(t: Tx, writer: str, bytes: str): null | error

/** recv — tirar o proximo registo. UM leitor; um segundo PANICA (§18.2). */
pub fn recv(r: Rx): ChanMsg | closed
```

### 18.2 Erro de verificador, erro de runtime, ou corrida silenciosa? — **runtime, hoje; e nunca silenciosa**

Fui medir antes de prometer, porque foi a §17 que me ensinou a nao repetir adjectivo emprestado.

| camada | estado MEDIDO | o que apanha |
|---|---|---|
| **tipo separado (`Tx`/`Rx`)** | desenhavel hoje | obriga a que dois leitores sejam um acto explicito: copiar o `Rx` |
| **verificador** | **NAO disponivel hoje.** A rede de unicidade existe — `spine.tks` tem o reticulado `UsUnique < UsShared < UsTop` — mas **nao e consultada em lado nenhum**; o proprio `borrow.tks` diz do seu par: *"Consulted NOWHERE yet ... fixpoint is preserved by construction"* | (nada, hoje) |
| **runtime** | **disponivel, e e o que garante** | tudo o resto, incluindo o que chegue por FFI ou pelo runtime em C |

**A garantia efectiva e de RUNTIME e e deterministica:** `tk_chan` grava a identidade da tarefa que
fez o **primeiro** `recv`. Um `recv` vindo de outra tarefa **panica** com a mensagem nomeada
(`teko: chan has a second reader — chan<T> is MPSC`). Nao ha corrida silenciosa: ou e o dono do
`Rx` a ler, ou o programa para e diz porque.

E ela **pode falhar**, que e a unica coisa que a distingue de decoracao:

```teko
#test
/**
 * ch_a_second_reader_panics — o segundo leitor tem de PARAR o programa, com a fronteira nomeada.
 *
 * @throws quando um segundo `recv` de outra tarefa NAO panica — o que significaria que a fronteira
 *         do dono e um comentario
 */
fn ch_a_second_reader_panics() {
    teko::assert::str_contains(ch_run_two_readers().panic_text, "chan<T> is MPSC")
}

#test
/**
 * ch_the_guard_is_not_vacuous — vivacidade: o canal com UM leitor tem de entregar os registos dos
 * DOIS escritores. Sem isto, um canal que panicasse sempre passaria o teste acima.
 */
fn ch_the_guard_is_not_vacuous() {
    teko::assert::eq_u64(ch_run_two_writers_one_reader().received, 2)
}
```

**O que NAO prometo:** rejeicao em compilacao. Ela e alcancavel — o consult site seria o reticulado de
`spine.tks`, um `Rx` que ele junte a `UsShared` e um erro — mas **ligar a espinha e trabalho que nao
esta nos 11 crumbs** e nao o contrabandeio para dentro deles. Fica **REPORTADO** como o upgrade
natural: quando a espinha for consultada, esta regra e um dos seus primeiros clientes, e o painel de
runtime continua como rede para o que vier por FFI.

### 18.3 O meu fan-in ja e estritamente MPSC — verificado, e o unico ponto duvidoso nomeado

Percorri o desenho a procura de um segundo leitor. **Nao ha.**

* §16.4 `orchestrate` — o **unico** `recv` do documento;
* §16.4 `drain_into` — so `send`, e ha **2 por filho** (stdout e stderr), logo `2N` escritores para
  `N` filhos e **1** leitor;
* §2.4 `gate_summary` e §13 `summarize` — leem **segmentos de journal em disco**, nunca o canal;
* `pump` (§15.3) era a forma central que a §16 rectificou; ela nao lia canal nenhum, porque nessa
  versao nao havia canal.

**O ponto que merecia ser nomeado e a invariante do consumidor, e ela ja implicava isto sem o dizer:**
*"o orquestrador nunca bloqueia em nada que nao seja `recv`"* (§16.4). Um segundo leitor seria uma
segunda tarefa bloqueada em `recv` — proibida por essa linha antes de a lei do dono existir. A lei do
dono torna explicito o que a invariante ja exigia.

### 18.4 A segunda estrutura (N:M) — NOMEADA, nao desenhada, e com a divida precisa

O dono disse que o N:M pode assentar sobre um `chan<T>` mas exige difusao e **multiplas copias**. A
divida nao e a difusao: e a **posse**.

**A diferenca essencial, em uma linha:** num MPSC cada registo e consumido **uma** vez, logo nao ha
contagem de referencias e nao ha pergunta de tempo de vida. Numa difusao o registo e consumido **N**
vezes, e e *por isso* — nao por gosto — que ela precisa de uma regra de posse.

**E com a raiz por tarefa (C-A, §17) a pergunta fica afiada:** um valor alocado na raiz da tarefa A e
um ponteiro pendurado no instante em que A rebobina. Portanto uma difusao **nao pode transportar
referencias para arenas alheias**. Quem pegar nisto tem de decidir, e ainda ninguem decidiu:

| pergunta | opcoes conhecidas, nenhuma escolhida |
|---|---|
| **quem e dono do valor difundido** | (i) ninguem — o runtime guarda-o numa area `malloc`ada e cada receptor **copia para a SUA arena ao receber** (e a forma que o `tk_chan` de §16 ja usa para bytes); (ii) uma regiao partilhada com contagem de referencias; (iii) valor imutavel com prova de escape de que sobrevive a todos os receptores |
| **quando morre** | (i) na ultima copia feita — precisa de saber quantos receptores ha; (ii) na contagem a chegar a zero; (iii) no fecho do ultimo receptor |
| **o que acontece a um receptor lento** | com contrapressao, ele trava os outros; sem ela, a memoria cresce. O MPSC resolve-o com um anel limitado; a difusao tem de o resolver **por receptor** |

**Nao construo nada disto, e nao e preciso para os 11 crumbs.** Fica registado com detalhe suficiente
para nao ser redescoberto: a difusao e um problema de **tempo de vida atraves de fronteiras de
arena**, e so se torna respondivel **depois** do C-A, porque e o C-A que cria as fronteiras.

### 18.5 O que isto muda nos crumbs

**Nada.** Os 11 crumbs ficam como estao — o fan-in ja era MPSC (§18.3), e a fronteira do dono
confirma-o em vez de o alterar. A unica adicao e dentro do C1: o `Tx`/`Rx` separados e o painel de
segundo-leitor, que sao ~15 linhas do `tk_chan` que aquele crumb ja constroi.

---

## 19. O canal e um HANDLE, nao um valor — e o `ref` de hoje nao o afecta

### 19.1 O modelo de posse do dono, e porque resolve mais do que parece

> *"Abriu um canal? Tem que retornar a ref do canal... a main abre o canal e passa para a thread do
> orquestrador um id pra ele buscar a ref do canal somente leitura e para os handlers passa o id pra
> eles buscarem a ref de escrita."*

**Concordo, e a razao e mais forte do que a conveniencia: o que atravessa a fronteira de tarefa passa
a ser um `u64`, e um `u64` nao e um ponteiro — e um NOME.** Depois do C-A (§17) cada tarefa tem a sua
raiz, e um ponteiro para a arena de outra tarefa pendura no instante em que ela rebobina. **Um nome
nao pendura.** O modelo do dono nao contorna a fronteira de arena: nao a atravessa.

O `Tx`/`Rx` da §18 nao desaparece — muda de estatuto. Deixa de ser um valor transportado e passa a ser
**que ref o id devolve**: `get_channel_writer(id)` para os handlers, `get_channel_reader(id)` para o
orquestrador. A separacao dos dois extremos (a lei MPSC de §18) e agora imposta pelo **acessor que se
chama**, o que e mais forte do que impo-la pelo valor que se transporta.

E o canal e singleton **por definicao**: um so registo, na arena do programa, com a `main` como dona.

### 19.2 A pergunta do reticulado de DI — medida, e a resposta tem duas metades

**Primeiro uma correccao de facto.** `di_singleton_holds_scoped_rejected` **nao existe no corpus**.
Procurei-o na arvore inteira: aparece em `docs/design/safety-spine.md:493` e em
`docs/design/wave-0.3.1-plan.md:332` — e este ultimo di-lo como **planeado**
(*"→ EXPECT_COMPILE_FAIL"*). E `src/checker/di.tks` (383 linhas) **nao tem verificacao de
profundidade**: `depth`, `cross-lifetime` e `monoton` nao ocorrem la. **O reticulado
`singleton=0 ≤ scoped=1 ≤ transient=2` esta ESPECIFICADO, nao construido** — a mesma forma que a
espinha de §18.2.

**Agora a resposta, sobre o reticulado tal como esta especificado: a raiz por tarefa NAO cabe numa
profundidade existente, e tambem NAO e uma profundidade nova. E um EIXO novo.**

O reticulado ordena **quanto tempo** um valor vive: `singleton` no exterior, `transient` no interior.
Duas raizes de tarefa nao se ordenam assim — sao **irmas**. Ambas estao a profundidade 0 na sua
propria cadeia e nenhuma contem a outra. O reticulado e uma cadeia; as raizes por tarefa fazem dele
uma **floresta**. A consequencia e exacta: o teste `d_holder >= d_provider` deixa de bastar, e passa a
precisar de `mesma_tarefa(holder, provider) && d_holder >= d_provider` — porque **segurar entre
tarefas e inseguro em TODA a profundidade**, inclusive 0 com 0.

**E e por isso que a pergunta que estava aberta ao dono fecha-se — mas nao por caber:** fecha-se
porque o modelo de handle **elimina o caso**. Nao ha `Ref` entre tarefas para classificar, porque o
que atravessa e um `u64`. O canal e um `#singleton` de processo e as tarefas alcancam-no por NOME.

**Fica REPORTADO, nao decidido por mim:** qualquer OUTRO `#singleton` que uma tarefa resolva e segure
por `Ref` continua a ser a pergunta aberta que o S8 ja tinha reportado. O modelo de handle fecha-a
para o canal; nao a fecha para a DI em geral.

### 19.3 A medicao do `ref` — e a resposta e a primeira: handle pequeno que consulta sempre

A medicao do integrador e o cerne: `ref x = <expr>` **compila e e uma COPIA**, nas duas rotas, sem um
unico diagnostico. Se a condicao de paragem lesse estado guardado dentro do handle, o `defer` da
`main` fecharia o canal e **o laco nao terminava**.

**Resposta directa: e a primeira. O handle e pequeno, copiavel, e NAO GUARDA NADA.** Ja era a forma da
§18 (`Rx = struct { raw: i64 }`), mas era um acaso — **passa a ser requisito**:

> **REQUISITO DO HANDLE DE CANAL: ele carrega o id e mais nada. Zero observaveis em cache. Todo o
> predicado e uma CHAMADA que consulta o registo pelo id.**
>
> Sob este requisito, uma copia do handle e inofensiva **por construcao**: copiar um nome nao envelhece
> o nome. O desenho nao depende de `ref` ser um alias verdadeiro — e nao depende de `ref` de todo.

E a forma que eu proponho para o laco vai um passo mais longe, e elimina o handle da condicao:

```teko
/**
 * orchestrate — o consumidor: le e apenda enquanto o canal estiver aberto.
 *
 * A CONDICAO LE O `id`, NUNCA UM HANDLE. Medido nesta arvore: `ref x = <expr>` compila e produz uma
 * COPIA, sem diagnostico, nas duas rotas. Um laco condicionado num campo de um handle copiado leria
 * um retrato congelado e nunca terminaria. Um `u64` nao tem esse problema porque nao guarda estado:
 * a pergunta vai ao registo, todas as voltas.
 *
 * @param c  o id do canal que a `main` abriu e de que e dona
 * @return   quantos registos foram apendados
 */
fn orchestrate(c: u64): u64 {
    mut n: u64 = 0
    loop teko::threads::chan_is_open(c) {
        match teko::threads::chan_recv(c) {
            ChanMsg as m => { n = n + 1; append_to_segment(m) }
            closed => break
        }
    }
    n
}
```

`loop <cond> { }` **existe e compila** (medido pelo integrador com a semente `0.3.0.31-beta`), logo a
forma do esboco do dono e real. **Nao ha crumb novo**: isto e uma restricao sobre a superficie que o
C1 ja constroi.

**E o fecho tem uma ordem, dita para nao ser descoberta em producao:** o `closed` do `recv` (contagem
de produtores a zero, §16.5) e a terminacao NORMAL; o `defer` da `main` a fechar o canal e o
**backstop** para o caso de o orquestrador ter de ser mandado parar antes disso. Os dois existem e nao
sao alternativos.

### 19.4 A guarda, e ela e a medicao do integrador transformada em regressao permanente

```teko
#test
/**
 * hc_a_copied_handle_still_sees_the_close — o `exit 1` medido, virado do avesso: uma COPIA do handle
 * tem de ver o fecho feito por outro caminho.
 *
 * E este teste que torna o requisito de §19.3 verificavel em vez de prometido.
 *
 * @throws quando a copia continua a dizer "aberto" depois de o canal fechar
 */
fn hc_a_copied_handle_still_sees_the_close() {
    let c = hc_open()
    let copy = teko::threads::get_channel_reader(c)
    hc_close(c)
    teko::assert::is_false(hc_reader_is_open(copy))
}

#test
/**
 * hc_a_caching_handle_fails_this — A INVERSAO, e e a medicao do integrador posta no corpus para
 * sempre: um handle que GUARDE o estado nao ve o fecho.
 *
 * Sem este braco a guarda acima diz apenas "nao vi problema", que e o que uma guarda cega tambem diz.
 *
 * @throws quando o handle com cache VE o fecho — o que significaria que esta prova nao prova nada
 */
fn hc_a_caching_handle_fails_this() {
    let c = hc_open()
    let stale = hc_caching_reader_of(c)
    hc_close(c)
    teko::assert::is_true(hc_cached_open_flag(stale))
}

#test
/**
 * hc_the_guard_is_not_vacuous — vivacidade: antes do fecho, o handle copiado tem de dizer ABERTO.
 * Um `is_open` que respondesse sempre false passaria os dois testes acima.
 */
fn hc_the_guard_is_not_vacuous() {
    let c = hc_open()
    teko::assert::is_true(hc_reader_is_open(teko::threads::get_channel_reader(c)))
}
```

### 19.5 O que NAO medi

1. **Nao corri a semente eu proprio.** As seis linhas da tabela do `ref` sao medicao do integrador, com
   controlo e as duas rotas a concordarem; aceito-as como facto e nao as re-derivo, mas nao sao minhas.
2. **`ref mut` / write-through nao existem** (medido por ele). O meu desenho **nao precisa deles** — e
   e por isso que §19.3 tira o handle da condicao em vez de pedir a capacidade. Se um dia alguem
   quiser o `ch.is_open()` do esboco literal, aí precisa de alias verdadeiro, e **isso** seria um crumb
   novo. Com o `u64` na condicao, nao e.
3. **O reticulado de DI nao esta construido** (§19.2), logo a analise de eixo que fiz e sobre a
   especificacao. Quando ele for construido, a linha `mesma_tarefa(...)` tem de entrar com ele.

---

## 20. A FUNDACAO — o que mais precisa haver, e o que as medicoes mudaram

Verifiquei na arvore os dois achados que mais pesavam, porque um deles podia obrigar-me a reescrever:

* **`lower.tks`, `lower_item_function`: `if f.is_test { return ... }`** — o corpo de um `#test` **nunca
  chega ao LIR**, logo nunca chega ao backend proprio. E `project.tks` emite o portao por
  `codegen::tk_emit_c_test` (`:3440`, `:4496`, `:4697`). **Confirmado: o arnes e os corpos de teste sao
  C, nas duas rotas.**
* **`cabi` nao e token deste lexer** (zero acertos em `src/lexer/` e `src/parser/`). **Confirmado: a
  superficie que escrevi em §14.3 nao e escrevivel.**

Nenhum dos dois me obriga a reescrever. **Os dois encolhem o C0** (§20.2).

### 20.1 O que mais precisa haver na fundacao — lista ORDENADA

Cada item: **o que e** · **porque e pre-requisito** (nao "seria bom") · **como se prova que ficou
feito**. Um item so entra aqui se algo dos 11 crumbs ficar **incorrecto** sem ele.

---

**F1 · `tk_task` e `tk_task_current()` — a arena por tarefa** *(= o crumb C-A, §17)*

*O que:* as 12 familias de variavel de §17.1 colapsam numa struct por tarefa; `tk_g_region_gen` fica
global e atomica; um acessor com duas encarnacoes (`_Thread_local` em C, `pthread_getspecific`/
`TlsGetValue` por `extern fn` no nativo).

*Porque e pre-requisito:* todo o resto aloca. Sem isto, o `pop` de uma tarefa liberta o que outra usa
— e o proprio `teko_rt.c:1150` ja o diz por escrito: *"single-threaded seed (S8 revisit)"*.

*Prova:* duas tarefas com canarias; a rebobinagem de uma nao toca a outra · **vivacidade**: as duas
raizes tem de ser DIFERENTES e as geracoes distintas · **reversao**: a mesma canaria sobre raiz
partilhada tem de ser destruida.

---

**F2 · A REGIAO DO PROGRAMA, separada das raizes de tarefa** *(novo — sai de F1)*

*O que:* uma regiao processo-inteira, imortal, distinta de qualquer raiz de tarefa. Hoje `tk_g_root`
faz os dois papeis ao mesmo tempo; o F1 parte-o e **a metade partilhada tem de passar a existir com
nome proprio**.

*Porque e pre-requisito:* a anotacao 2 do dono diz *"todo canal reside na arena do programa ou na
spine — e singleton"*. Depois do F1 **nao ha "arena do programa"** — ha N raizes de tarefa e mais
nada. Um canal alojado numa delas morre quando essa tarefa sai. **Este item e o que impede o canal de
nascer no sitio errado**, e nao existia em nenhum dos 11 crumbs porque so aparece quando o F1 parte a
raiz.

*Prova:* um valor alocado na regiao do programa sobrevive ao `tk_arena_pop` **e** a saida da tarefa que
o alocou · **reversao**: o mesmo valor numa raiz de tarefa nao sobrevive.

---

**F3 · O registo de nomes no runtime: `u64` → recurso, atras de `extern fn`**

*O que:* uma tabela processo-inteira que resolve um id para o recurso, com `open` a devolver o id e
`lookup` a validar.

*Porque e pre-requisito:* **medido, e por duas vias independentes** — nao ha estado mutavel ao nivel do
modulo (`mut REG = ...` fora de funcao da *"expected a declaration"*), e `check_ref_return_passdown` so
admite devolver um dos proprios parametros `ref`, com a nota de que *"a stored field cannot escape
until the transitive-escape spine lands"*. **Nao ha onde por o registo em Teko hoje.** Ele tem de
viver no runtime.

*Prova:* dois `open` devolvem ids diferentes · um id obsoleto devolve **erro**, nunca comportamento
indefinido · um handle COPIADO ve o fecho (§19.4, com o braco de reversao do handle que guarda estado).

---

**F4 · `tk_chan` MPSC: `Tx`/`Rx` por acessor, contagem de produtores, painel de segundo-leitor**

*Porque e pre-requisito:* e o sumidouro que o fan-in escreve e o orquestrador le. A lei MPSC do dono
so e imponivel se o segundo leitor **falhar**, e hoje o verificador nao o pode fazer (§18.2, medido: o
reticulado da espinha existe e **nao e consultado em lado nenhum**).

*Prova:* §18.2 — um segundo `recv` de outra tarefa panica com a fronteira nomeada · vivacidade: com um
leitor, os registos dos dois escritores chegam.

---

**F5 · `pipe` + o handler de dreno**

*Porque e pre-requisito:* `fork` e `dup2` existem; **`pipe` nao existe em lado nenhum do runtime**
(medido, §15.2). Sem ele o tunel continua a ser ficheiros e o par `.out`/`.err` nao desaparece.

*Prova:* §15.5 e §16.6 — um filho que escreve 200 000 bytes e drenado inteiro · **as duas inversoes
com PRAZO**: esperar antes de drenar tem de atingir o prazo, e o consumidor que bloqueia a meio do
laco tambem.

---

**F6 · A captura em modo teste** *(= C0, §14)* — **pode ir a frente de tudo, e agora custa menos**

*Porque e pre-requisito:* sem ela o `panic` mata a shard, leva os que faltavam e salta o despejo de
cobertura — e todo o §13 vira reconstrucao do que a morte levou.

*Prova:* um `#test` que chama `exit(7)` e reportado como tal **e o `#test` seguinte corre** (a prova
tem de ser um teste POSTERIOR e separado) · reversao: a mesma fonte em modo `Program` sai 7 a serio.

---

**NAO sao fundacao — nomeados para ninguem os ir procurar:**

| peca | porque NAO e pre-requisito nosso |
|---|---|
| coercao de ponteiro-de-funcao no front-end + o token `cabi` | o arnes e **C emitido** (medido). `tk_test_run(&fn)` e uma chamada C escrita pelo emissor, nao por Teko. Isto e fundacao do `spawn` **geral**, nao da nossa |
| `select_func_addr` / relocacao de simbolo | **ja existe nos dois backends** (medido) — nao era degrau nenhum |
| ligar a espinha e o reticulado de DI | o painel de runtime do F4 cobre a lei MPSC hoje. Ligar a espinha e o **upgrade** de §18.2, e continua fora dos 11 crumbs |

### 20.2 O que muda nos 11 crumbs

**Nada cresce. O C0 encolhe duas vezes, e ambas por medicao.**

1. **`cabi` nao existe ⇒ retiro a superficie Teko de §14.3.** `teko::test::run_capturing` e o
   `cabi fn()` **saem do desenho**. Nao sao substituidos: **nao sao precisos**. O arnes e C emitido,
   logo `tk_test_run(&<fn>)` e uma linha que `emit_test_call` escreve. E a prova tambem nao precisa
   deles — o `#test` que prova a captura **chama `exit(7)` directamente**, e quem o captura e o arnes.
2. **Os `#test` nao passam pelo backend proprio ⇒ o C0 nao toca no backend.** Toca em
   `emit_test_call`/`emit_test_main` (`codegen.tks`) e em `teko_rt.{c,h}`. **Zero trabalho de
   backend**, e a fronteira estrutural de §14.5 fica **mais** forte, nao menos: ha um emissor so.
3. **A rota de classe funcionar hoje** nao muda crumb nenhum — muda a escolha de §20.3, e la a
   justificacao.

**Total: 11 crumbs, e o C0 e menor do que quando o escrevi.**

**O alarme que isto cria, e posso prova-lo:** a captura passa a estar provada **na rota C apenas**,
porque e a unica por onde um corpo de teste passa (`lower.tks`, `lower_item_function`). No dia em que
os corpos de teste baixarem para LIR, um `longjmp` que atravesse molduras nativas **tem de ser
reprovado** — o backend proprio nunca viu um. Nao e um problema hoje; e uma linha que tem de viajar
com o degrau que mudar aquele `if f.is_test { return ... }`.

### 20.3 Classe ou handle `u64`? — **handle**, e o criterio nao e preferencia

Fico com **uma**, como pedido: **handle `u64` + acessor** (§19).

**O criterio, e e uma propriedade e nao um gosto:** *um ponteiro obsoleto e comportamento indefinido;
um id obsoleto e um erro verificavel.* Num desenho cujo assunto inteiro e sobreviver a morte abrupta e
a concorrencia, o modo de falha que se pode **relatar** ganha ao que nao se pode.

**E a vantagem da rota de classe — "funciona hoje, sem esperar pela espinha" — nao se aplica a ESTE
objecto, e a razao sai do meu proprio F2.** *"Objecto e ponteiro"* esta medido e e verdade; mas o
canal tem de viver na regiao do programa, e **depois do F1 essa regiao ainda nao existe** (e o F2 que
a cria). Logo a rota de classe tambem nao esta pronta hoje para o canal — nao esta pronta por outro
motivo, mais fundo, e que so aparece quando se olha para a arena por tarefa.

**O que fica da rota de classe, porque a medicao do integrador nao se desperdica:** o **handle** pode
ser uma classe. O estado vive no registo do runtime (F3); o handle carrega o id e mais nada
(requisito de §19.3). Assim o aliasing medido — que e real — nao e preciso para a correccao, e o
handle pode ganhar metodos (`c.close()`) por ergonomia sem que nada dependa disso.

### 20.4 O que preciso do dono — e a resposta e "de nenhum lado"

A escrita **directa** em campo de classe sob `let` esta com o dono e nao a decido.

**O meu desenho nao depende dela, e digo de que lado precisaria se dependesse:** o handle **nao tem
campos mutaveis** — carrega um id imutavel. Todo o efeito passa por chamada ao registo. Logo:

* se o dono **proibir** a escrita directa: o desenho nao muda;
* se o dono **permitir**: o desenho nao muda.

E quando o handle for uma classe com metodos, a mutacao que ele usa e **por metodo** — exactamente a
forma que a anotacao 6 do dono ja legitimou (*"quando o metodo de uma classe realiza mutacao na
propria classe, e isso e desejado"*). **Nao encosto a decisao do dono a nenhuma parede.**

**Duas coisas que ficam REPORTADAS e nao decididas por mim:**

1. `ref p: T` como parametro **recusa escrita** (B.21, medido), e a lei 5 do dono diz que **`ref` e
   mutavel por definicao**. As duas afirmacoes colidem. Nao e do meu desenho — ele nao usa `ref` —
   mas e uma divergencia entre a lei e a arvore, e quem a resolver deve saber que existe.
2. `ref r = c` sem anotacao **compila e copia, sem diagnostico** (medido). O meu desenho e imune por
   construcao (§19.3), mas qualquer outro que use `ref` nao e.

---

## 21. `#arch("…")` — o irmao do `#os`, e a fronteira que impede que ele esconda um teste mau

### 21.1 O desenho

**Simetria total com o `#os`**, porque a maquinaria toda ja existe e o custo esta em copiar uma forma
provada: campo `arch_guard` ao lado de `os_guard` (`ast.tks:400`), leitura em `parse_decl.tks:890`,
serializacao nos tres sitios do `.tkb` (`tkb_write:521`, `tkb_read:727`, `tkb_frame:301`), e o filtro
em `prune_os`, que passa a `prune_platform(program, tos, tarch)`.

**Composicao: E, nunca OU.** Um item com `#os("linux") #arch("arm64")` existe **so** em linux-arm64.
Nao ha forma de exprimir "ou", e a ausencia e deliberada: um `ou` num filtro de compilacao e a porta
por onde entra o item que existe em duas plataformas e nao foi provado em nenhuma.

**O `match` cresce? Digo o que medi.** `prune_os` (`project.tks:124`) trata **so**
`parser::Function`; `parse_decl.tks:1292` recusa `#os` em qualquer outra posicao. **Recomendo manter
`#arch` tambem so em funcao**, e a razao nao e preguica: um `const` ou um `type` que difere por
arquitectura e exprimivel por **uma funcao** guardada (`fn word_size(): u64`), e essa forma tem
ainda a vantagem de o valor divergente ter um NOME. Se o dono quiser `#arch` em `type`/`const`, o
`match` de `prune_os` cresce com um braco por especie de item, mais o espelho na recusa de posicao —
**e a serializacao do `.tkb` ja nao chega, porque um `type` podado muda a tabela de tipos**. Digo-o
para a decisao ser tomada com o custo a vista, e nao a decido.

### 21.2 O vocabulario, e o erro escondido que EU consigo provar hoje

**O `#os` de hoje aceita qualquer string e compara-a por igualdade:**

```teko
parser::Function as f => { if f.os_guard != "" && f.os_guard != tos { keep = false } }
```

Logo `#os("Linux")` — L maiusculo — **remove a funcao em TODAS as plataformas, em silencio**. Nao e
hipotese: e a leitura directa da linha 124. Um filtro mal escrito nao falha; **desaparece com o
codigo**.

**E a casa ja tem a regra certa, aplicada a outra coisa.** `unsupported_target_error`
(`project.tks:1961`) existe precisamente para *"um `TEKO_TARGET` que `target_from_name` nao
reconhece... um erro de COMPILACAO, nunca um mis-lower silencioso"*, e **lista o conjunto aceite**.

**Proposta: `#arch` valida contra um vocabulario FECHADO em tempo de compilacao, e o `#os` passa a
validar tambem.** O vocabulario ja e canonico e esta fixado no runtime (`teko_rt.c:2418-2429`):
**`"arm64"`** (nao `aarch64`) e **`"x86_64"`** (nao `amd64`); os SO saem do mesmo par que
`NativeTarget` ja enumera (`macos`, `linux`, `windows`).

*Prova:* `#arch("x86-64")` e um **erro de compilacao** que nomeia o valor e lista o conjunto aceite ·
**reversao**: `#arch("x86_64")` compila e o item sobrevive na plataforma certa · **vivacidade**: o
mesmo item **desaparece** na outra, senao o filtro nao esta a filtrar.

### 21.3 O cenario `.tkr` que nao deve CORRER — a forma honesta

`Then on "<os-arch>"` **sobrepoe expectativas**; nao impede execucao. `skip` e falha por lei. Falta a
terceira forma, e ela ja existe na casa — no `#os`:

> **`Given platforms = ["linux-x86_64", "macos-arm64"]`. Numa plataforma fora da lista o cenario NAO E
> SALTADO: ele NAO EXISTE nesta corrida** — exactamente como `prune_os` remove um item do programa e
> ninguem lhe chama skip.

A distincao e de significado e nao de palavra: **um skip diz "nao consegui"; um nao-aplicavel diz
"isto nao existe aqui".** O primeiro e falha por lei porque esconde trabalho por fazer; o segundo e a
mesma operacao que a compilacao condicional ja faz, e por isso e legitimo pela mesma razao.

O sumario de §13 ganha uma coluna `not-applicable`, **separada de `skipped`** — juntar as duas seria
reintroduzir o skip com outro nome.

**A guarda que impede isto de virar esconderijo, e e a metade que faz a proposta valer:**

> **Um cenario nao-aplicavel em TODAS as pernas da matriz e uma FALHA.**

Verificavel por agregacao: a uniao dos `Given platforms` de cada cenario tem de intersectar pelo menos
uma perna da matriz declarada. Um cenario que nao corre em lado nenhum e cobertura falsa com selo de
qualidade — e e exactamente o que uma lista de plataformas facilita se nada a vigiar.

*Prova:* um cenario com `Given platforms = ["plan9-vax"]` faz a guarda **falhar** nomeando-o ·
vivacidade: um cenario com uma plataforma real da matriz passa.

### 21.4 A pergunta contra a proposta — e a resposta e que o caso `xat_` NAO merece `#arch`

Pediste-me para a fazer, e ela e a parte util.

**A fronteira, em uma linha:**

> **`#arch` responde "este codigo EXISTE aqui?". Nunca responde "este codigo COMPORTA-SE de outra
> maneira aqui?".**

| classe | merece | porque |
|---|---|---|
| codigo cuja **existencia** e especifica da arquitectura — uma ligacao FFI, um intrinseco, um numero de chamada de sistema, um registo com nome de plataforma | **`#arch`** | noutra arquitectura ele nao compila nem liga. Nao ha resultado para comparar |
| teste cujo **resultado** difere por hospedeiro | **corrigir** | um resultado que muda com o hospedeiro significa que o hospedeiro e uma **entrada nao nomeada**. Nomeia-se a entrada e o teste volta a ser universal |

**E o `xat_` cai do segundo lado.** Nao encontrei
`xat_sysv_call_args_keep_independent_per_file_counting` na arvore ao topo — digo-o em vez de fingir
que li o corpo — mas o **nome** e a familia dizem o suficiente, e a familia esta ca
(`src/backend/isel_x86_64_test.tkt`): sao testes de **seleccao de instrucao** que constroem um
`lir::LFunc` sintetico e afirmam o que o classificador SysV faz com ele. Isso e uma **funcao pura da
entrada**. A mesma entrada tem de dar a mesma saida num Mac, num arm64 e num Windows — o classificador
SysV nao muda de opiniao consoante quem o corre.

**Hipotese, e ela e verificavel em vez de opinada:** se ele passa **so** em linux-x86_64, o alvo esta
a ser apanhado do **hospedeiro** em vez de ser dito pelo teste. Num macOS arm64 o codigo exercitado nao
e o SysV — e o AAPCS64; num Windows x86_64 e o Win64. **O teste nao e dependente de arquitectura: e um
teste que se esqueceu de nomear o seu alvo.**

*Como se decide sem discutir:* passa-se o alvo **explicitamente** ao que esta sob teste. Se passar a
verde nas quatro pernas, era isso — e um `#arch` ali teria **escondido** o defeito e comprado cobertura
falsa nas tres plataformas onde ele falha. Se continuar vermelho com o alvo explicito, entao ha uma
dependencia real do hospedeiro, e **essa** merece ser nomeada — mas nomeada no sitio certo, que e a
dependencia, nao o teste.

**A guarda que torna isto lei em vez de conselho:**

> **`#arch` (e `#os`) num `#test` sao PROIBIDOS**, salvo entrada numa lista de excepcoes com razao
> escrita.

Um `#test` sobre um backend de alvo cruzado e precisamente o que tem de correr em **todo** o lado: e
para isso que ele existe. *Prova:* um `#test` com `#arch` fora da lista faz a guarda falhar nomeando o
teste · vivacidade: uma funcao **nao**-`#test` com `#arch` passa.

### 21.5 Porque nenhum dos dois mecanismos tem utilizadores — opiniao MEDIDA

`#os` tem zero usos e `Then on` tambem. Nao acho que a superficie seja indescobrivel: **acho que o
corpus que precisaria dela ainda nao e Teko.**

Medido: a divisao POSIX/Windows do projecto vive toda em `teko_rt.c`, e la ela ja se escreve
`#ifdef _WIN32` — ha dezenas (`:2243` *"No fork/dup2 on Windows"*, `:2262`, `:30`). **O `#os` foi
construido para uma divisao do lado Teko que ainda nao existe, porque o runtime ainda e C.**

Isso faz uma previsao falsificavel: **`#os` ganha os primeiros utilizadores exactamente quando
`feat/issue-runtime-em-teko` aterrar** — e a §16/§17 deste documento ja precisam dele (o
`tk_task_current` tem duas encarnacoes de plataforma, o `pipe` tem duas). Se o runtime aterrar em Teko
e `#os` continuar com zero usos, a minha explicacao esta errada e a superficie e que e o problema.

### 21.6 O custo

**Nao toca em nenhum dos 11 crumbs nem na fundacao F1–F6.** E ortogonal: compilacao condicional e
seleccao de regressivos. As unicas duas linhas de contacto: o sumario de §13 ganha a coluna
`not-applicable` (dentro do **C4**), e o `#os` validado ajuda o F1/F5, que sao os primeiros a precisar
de duas encarnacoes por plataforma.

**Um crumb**, com quatro pecas e as suas guardas: `arch_guard` no parser/AST/`.tkb`;
`prune_platform`; a validacao de vocabulario fechado para `#arch` **e** `#os`; e do lado dos
regressivos o `Given platforms` com a guarda do "nao-aplicavel em toda a matriz". As tres inversoes
de §21.2, §21.3 e §21.4 entram com ele.

---

## 22. O caso que motivava o `#arch` era um teste mal escrito — e isso torna a §21 melhor

**A proposta fica de pe.** O dono pediu o `#arch`, o `#os` existe e esta orfao, e a peca vale por si.
O que muda e a resposta a pergunta 3: **deixa de ser principio e passa a ser exemplo trabalhado**, e
e o melhor que este documento vai ter — um **falso positivo perfeito**.

### 22.1 O que era, medido — e uma tabela ANTERIOR desta seccao estava errada

> **CORRECCAO (2026-07-30).** Uma versao anterior desta seccao dizia que os dois testes falhavam em
> tres pernas e passavam em linux-x86_64, e explicava a assimetria pelo `SIGABRT` a truncar shards
> diferentes. **A assimetria NUNCA EXISTIU.** Fica registada, e nao apagada, porque um documento que
> corrige em silencio e a mesma doenca que ele descreve.

**O que se mede de facto:**

| perna | `xat_` vermelhos | unitarios relatados |
|---|---:|---:|
| windows-x86_64, **pre**-C0 | **2** | 638 |
| macos-arm64, **pre**-C0 | **2** | 638 |
| linux-arm64-glibc, **pre**-C0 | **2** | 638 |
| macos-arm64, **com** C0 | **2** | **1144** |
| linux-arm64-glibc, **com** C0 | **2** | **1144** |

**Os dois falharam sempre, em todas as pernas, antes e depois do C0.** Nao houve escalonamento a
produzir contagens diferentes.

**Porque o defeito era universal, e isto e o que ja estava certo:**

* o texto esperado, `"fmov.d %pf0, %0"`, era uma **quimera** — mnemonica do ramo FPR com a grafia do
  ramo GPR. `reg_ref_x86` marca o ficheiro em **qualquer** operando FPR e `pin_reg_args_x86` constroi
  destino e origem da **mesma** decisao, logo **nenhuma seleccao pode emitir aquele texto**, em
  hospedeiro nenhum. O certo e `fmov.d %pf0, %f0` — e esta agora na arvore
  (`isel_x86_64_test.tkt:226,895,918`);
* a primeira asserçao dos **dois** testes era byte-identica, logo um vermelho com o gemeo verde era
  impossivel em qualquer hospedeiro. **Isto ja dizia que a assimetria era impossivel**, e eu escrevi-o
  ao lado de uma tabela que a afirmava — devia ter parado ali.

**De onde veio a assimetria imaginaria:** de uma expressao de busca. Contava-se
`test teko::[a-z_:]+ \.\.\. FAILED`, e `xat_win64_call_args_number_both_files_by_shared_position`
tem **digitos**. A classe `[a-z_:]` nunca o casa. **Medi a exposicao desse padrao na arvore: dos 52
`xat_` de `isel_x86_64_test.tkt`, 13 tem digitos no nome — um quarto da familia, invisivel ao
contador.**

**E o que aconteceu e pior do que o que eu tinha escrito, e serve melhor esta seccao:** o defeito nao
estava no instrumento a truncar. Estava **na leitura**, com o padrao errado — e a diferenca inventada
viajou como pista principal.

**A conclusao que interessa fica INTACTA, e agora e mais afiada: um `#arch` admitido com base em
"falha ali e aqui nao" teria sido admitido com base numa diferenca IMAGINARIA.** Nao e preciso que o
relatorio esteja truncado para a evidencia ser falsa — basta que quem a le conte com o padrao errado.
O conserto foram **4 literais de string e zero linhas de `.tks`**, e a inversao da 2 vermelhos → 0.

### 22.2 A pre-condicao, e ela e ordenacao e nao conselho

> **`#arch` (e `#os`, e o `Given platforms` de §21.3) so sao admissiveis sobre uma suite que NAO
> ABORTA.**

Uma suite que aborta ao primeiro vermelho nao relata semantica: relata **ate onde chegou**. Sem o C0
esta suite parava nos **638** e ninguem podia sequer **contar** quantos vermelhos havia — logo nao
havia base para admitir nem para recusar um `#arch`.

**E o papel do C0 aqui e mais modesto e mais util do que eu lhe tinha atribuido: ele nao revelou uma
assimetria — revelou que NAO HAVIA NENHUMA.** Com ele, os dois vermelhos aparecem em todas as pernas
com a suite inteira (1144) por tras, e a hipotese de plataforma morre por falta de assimetria em vez
de por argumento.

**Correccao ao meu proprio §21.6, que dizia que isto nao tocava em crumb nenhum: toca. O crumb do
`#arch` DEPENDE do C0**, e a dependencia e desta natureza — sem o C0, o instrumento que decidiria se
um `#arch` e justificado esta cego. Ordem: **C0 antes de `#arch`, sempre.**

### 22.3 Que evidencia justifica um `#arch` — tres provas, todas baratas, todas falsificaveis

*"Falha noutro hospedeiro"* **nao chega**, e este caso e a razao: foi exactamente o que ele parecia.
Um autor que queira `#arch` apresenta **as tres**, e a terceira e verificavel por maquina.

**E1 — A falha sobrevive ao relatorio, E A LEITURA NAO PERDE CASOS.** Duas metades, e a segunda foi
comprada com sangue nesta seccao:

*(a) a suite corre ate ao fim* (C0). Uma diferenca observada sobre uma suite que abortou e uma
diferenca de escalonamento, nao de semantica.

*(b) a contagem e feita com um padrao que nao perde casos.* O `#arch` deste documento quase foi
justificado por uma assimetria que so existia porque o contador usava `[a-z_:]` e um quarto da familia
tem digitos no nome (§22.1). **Quem apresenta E1 apresenta o padrao com que contou**, e ele tem de
casar os nomes que existem — verificavel: o total contado tem de bater com o total declarado pela
suite.

*Este caso morre aqui, e nas duas metades:* com contagem completa e padrao correcto, os dois testes
sao vermelhos em **todas** as pernas.

**Um filtro de plataforma e tao bom quanto a leitura que o justifica — e a leitura falha por razoes
que nao sao do relatorio.**

**E2 — A expectativa e alcancavel.** O autor nomeia a **linha** que produz o valor esperado no
hospedeiro onde passa. Nao uma crenca: um sitio. *Este caso morre aqui tambem:* nenhum caminho emite
`%pf0, %0`. Uma quimera e inalcancavel em **todo** o lado e so **parece** dependente do hospedeiro
quando o relatorio esta truncado.

**E3 — A divergencia esta no codigo sob teste, nao na entrada do teste.** O autor nomeia a **leitura
do hospedeiro** — que chamada consulta `teko::os()`, `teko::arch()`, `TEKO_TARGET` ou um alvo por
omissao. **Se nao houver leitura, nao ha dependencia de arquitectura, e acabou.**

**E o E3 e mecanico, o que o torna guarda em vez de ritual.** Varri o corpus `.tkt` inteiro por
`host_default_target` / `host_target_for_os` / `TEKO_TARGET`: **4 ficheiros, todos em `src/build/`,
nenhum em `src/backend/`.** Um teste de seleccao constroi um `lir::LFunc` **sintetico** — a entrada e
independente do hospedeiro por construcao, logo um resultado dependente do hospedeiro **e impossivel**
a menos que o codigo sob teste leia o hospedeiro. Ele nao le.

> **A guarda: `#arch`/`#os` num `#test` de um modulo que NAO LE o hospedeiro e recusado
> automaticamente.** Nao ha julgamento humano no caminho — a pergunta e "este modulo consulta a
> plataforma?", e a resposta e um `grep` que ja corri.

*Prova:* um `#arch` plantado num `#test` de `src/backend/` faz a guarda **falhar** nomeando o teste ·
**vivacidade**: o mesmo `#arch` num `#test` de `src/build/` (que le o hospedeiro) **passa** para a via
das excepcoes, com E1 e E2 por apresentar. Se a guarda recusasse os dois, era cega — e ja tivemos tres
dessas.

### 22.4 O que este caso me ensinou sobre a proposta

A §21.4 dizia que `#arch` responde *"este codigo EXISTE aqui?"* e nunca *"comporta-se de outra maneira
aqui?"*. **Mantem-se, e agora tem um caso a sustenta-la em vez de so uma frase.** O acrescento e mais
duro do que o principio: **a evidencia que um autor traz para pedir um `#arch` e, quase sempre, a
mesma evidencia que um teste partido produz.** Por isso a admissao nao pode assentar no sintoma
(*"falha ali"*) — tem de assentar em E2 (o caminho existe) e E3 (a leitura existe), que um teste
partido **nao consegue apresentar**.

E deixo o dado a descoberto, porque e ele que fecha o arco — corrigido, e o corrigido diz mais: **o
C0 nao tornou o defeito visivel; tornou-o CONTAVEL.** Sem ele a suite parava nos 638 e a pergunta
"quantos vermelhos ha, e sao os mesmos em todas as pernas?" nao tinha resposta possivel. Com ele a
resposta e imediata e mata a hipotese de plataforma: **2, e os mesmos, em todas.**

**A captura nao paga so a fiabilidade do relatorio — paga a possibilidade de FAZER a pergunta que
distingue um defeito de um hospedeiro.** E §22.1 acrescenta a outra metade, que nao e do C0 e e minha:
**a pergunta so vale se quem a responde contar com o padrao certo.**

---

## 23. `push -> error | null` — a lei do dono, e o buraco que ela abre no meu argumento

> *"Ao fazer push em um canal, retorna um `error | null`, nulo se sucesso, error dizendo o pq foi
> negado o push (o guarda do bounded)."*

Duas formas (**bounded** / **unbounded**), **sem panico**, **sem predicado**. E o idioma nao alarga
nada: `-> error | null` tem **78** usos em `src/` (mais 13 na ordem inversa) — contei.

### 23.1 O argumento a favor e mais forte do que a simplicidade: o predicado do C# e TOCTOU

Entre o *"esta livre?"* e o `push`, outro produtor ocupa a vaga — e num MPSC ha N produtores **por
construcao**, logo a corrida nao e um azar, e o caso normal. **Um `push` que devolve o veredicto e
atomico: a pergunta e a accao sao a mesma operacao.** A forma do dono nao e so mais leve; **elimina
uma corrida que o modelo do predicado obriga o utilizador a gerir a mao.**

**E esta propriedade e a que eu tenho de nao estragar em §23.2.** Qualquer coisa que eu acrescente
tem de manter o `push` como **unica autoridade**.

### 23.2 O buraco no meu argumento, admitido — e a reformulacao

Eu escrevi que a contrapressao vinha de **quem escreve bloquear**, e que os handlers *"encheriam o
canal limitado e parariam"*. **Com um `push` que devolve `error`, nao param.** O integrador tem razao:
o meu argumento de ausencia-de-impasse assentava num bloqueio que a lei do dono remove.

**Descartar esta fora de questao** — nao perder saida e a razao de ser do journaling inteiro. Logo o
handler **repete**. E a reformulacao e esta, e ela **e mais forte do que a que substitui**:

```teko
/**
 * drain_one — o ciclo de UM handler: ler o seu tubo e entregar cada bloco ao canal.
 *
 * A RECUSA NAO E UM ERRO DO HANDLER: e a contrapressao a falar. Um `Full` significa que o consumidor
 * esta atrasado, e a resposta certa e NAO LER MAIS — o bloco fica na mao, o tubo enche, e o filho
 * bloqueia no seu proprio `write`. A contrapressao chega ao produtor real na mesma, so que agora
 * atravessa uma decisao VISIVEL em Teko em vez de morrer dentro de uma primitiva bloqueante.
 *
 * SO `Full` SE REPETE (§23.4). As outras quatro razoes sao terminais, e repetir uma delas era um ciclo
 * infinito — e por isso que a razao tem de ser enumeravel e nao uma frase.
 *
 * @param fd  o descritor de leitura do tubo deste handler
 * @param c   o id do canal
 * @param w   a identidade com que os blocos deste handler sao rotulados
 * @return    quantos blocos entregou
 */
fn drain_one(fd: i64, c: u64, w: str): u64 {
    mut n: u64 = 0
    loop {
        let blk = teko::threads::read_block(fd)
        if blk.len == 0 { break }
        loop {
            match teko::threads::chan_push(c, w, blk) {
                null => break
                error as e => {
                    if !chan_refusal_is_full(e) { return n }
                    match teko::threads::chan_await_space(c, AWAIT_MS) { error => { }; null => { } }
                }
            }
        }
        n = n + 1
    }
    n
}
```

**A cadeia de ausencia-de-impasse, reformulada, e a conclusao MANTEM-SE:**

| antes (bloqueio) | agora (recusa + repeticao) |
|---|---|
| canal cheio ⇒ `push` bloqueia | canal cheio ⇒ `push` devolve `Full` |
| handler parado dentro do `push` | handler **nao le mais do tubo** |
| — | tubo enche ⇒ **filho bloqueia no seu `write`** |
| a invariante: o orquestrador nunca bloqueia fora do `recv` | **a mesma, sem uma virgula mudada** |

A contrapressao chega ao mesmo sitio pela mesma razao; o que mudou foi **onde ela e decidida**. E o
ganho e real: antes ela morria dentro de uma primitiva do SO, agora e uma linha de Teko que se pode
ler, medir e interromper.

**O `chan_await_space` NAO e o predicado do C#, e a distincao e a que salva a lei do dono:** ele nao
responde *"posso escrever?"* — ele **dorme ate algo ter mudado**. O `push` que vem a seguir **pode ser
recusado na mesma** (outro produtor ganhou a corrida) e o ciclo trata disso. **O `push` continua a ser
a unica autoridade; o `await` e so uma dica que evita queimar CPU.** Sem ele, oito handlers em espera
activa; com ele, oito adormecidos. Um `await` que fosse garantia seria o TOCTOU de volta.

**O que NAO medi:** o custo da espera activa se o `await` nao existisse, e o valor certo de
`AWAIT_MS`. Sao medicoes do crumb, nao adjectivos meus.

### 23.3 `unbounded`: admissivel em que casos? — **em nenhum dos nossos**

Escrevi que o ilimitado *"tem a memoria como unico travao"*, e nos morremos de OOM duas vezes no dia
deste desenho.

**O criterio, e nao e "porque sim":** o ilimitado e admissivel quando o **volume total e conhecido
antes do primeiro push**. Se for, a memoria tem um tecto que nao depende do ritmo do consumidor.

**Aplicado ao nosso uso: nenhum canal qualifica.** Todos transportam **saida de processo**, cujo
volume nao e conhecivel a priori — a transcricao de uma shard depende de quantos testes falham. Logo
**todos os nossos canais sao bounded**, e isso e decisao, nao omissao. As duas formas existem porque o
dono as pediu e servem quem escreve programas em Teko; o **compilador** usa uma so.

**O que faria mudar:** um canal que transportasse apenas os registos de sumario (§13 — um `plan` e um
`end` por escritor, total `2 x jobs`, conhecido antes de comecar). Esse qualificaria — e e tao pequeno
que nao precisa de canal nenhum. Deixo o criterio escrito para nao ser re-derivado.

### 23.4 As razoes de recusa — fechadas por construcao, no espirito do `grep` e nao do julgamento

O integrador pediu para eu guardar a forma da guarda de §22 (*"um `grep`, nao um julgamento"*). Aplico-a
aqui: **as razoes nao sao prosa, sao um conjunto fechado, e cada uma corresponde a um estado que o
`tk_chan` JA tem.**

| razao | estado que a produz | repetivel? |
|---|---|---|
| **`Full`** | anel na capacidade (**so** possivel em bounded) | **SIM** — e a unica |
| **`Closed`** | a `main` fechou o canal (§19) | nao — deliberado |
| **`NoReader`** | o `Rx` nunca foi tomado, ou a tarefa leitora morreu | nao — e uma falha, e distinta de `Closed` de proposito |
| **`Oversize`** | o registo excede a capacidade TOTAL do anel, logo **nunca** cabe | nao — e repeti-la era o ciclo infinito |
| **`NotAProducer`** | o id nao nomeia um produtor registado (id obsoleto, §19/F3) | nao |

**O `Oversize` e o que justifica a enumeracao inteira.** Sem ele, um handler com um bloco maior do que
o anel repete `Full` para sempre — um impasse **novo**, criado pela propria lei que remove o bloqueio.
Distinguir *"agora nao cabe"* de *"nunca cabe"* nao e requinte: e o que impede o ciclo de §23.2 de ser
um ciclo infinito. **Um `error` que dissesse so "negado" produzia esse impasse**, e e por isso que a
letra da lei do dono — *"error dizendo o **pq**"* — e carga util e nao cortesia.

**A guarda, e ela e mecanica:** **toda a razao tem de ter um teste que a PRODUZA.** Uma razao que
nenhum teste consegue construir esta morta ou e inalcancavel — e nos dois casos a enumeracao esta a
mentir. Nao ha julgamento: conta-se um teste por variante.

*Prova:* cinco testes, um por razao, cada um a construir o estado e a afirmar a razao devolvida ·
**vivacidade**: um `push` normal devolve `null`, senao um canal que recusasse sempre passaria os cinco
· **inversao**: um handler alimentado com `Oversize` **termina** (e nao repete), e o mesmo handler com
`Full` **repete**.

### 23.5 O custo

**Nenhum crumb novo.** Isto e a superficie do `tk_chan`, que o **C1** ja constroi (F4 em §20.1). A
lei do dono **retira** trabalho: sai o caminho de bloqueio dentro do `push`, entra um `enum` de cinco
variantes e um ciclo de repeticao de sete linhas. **O `chan_await_space` e a unica adicao**, e existe
para nao queimar CPU, nao para dar garantias.

---

## 24. O registo de UMA LINHA — e a minha conta do `unbounded` estava certa pela razao errada

> *"O numero de escritas e previsivel (de acordo com a quantidade de testes e regressivos)...
> padronizar uma estrutura single-line de saida, e quem vai escrever precisa formatar no padrao
> esperado e identificar quem escreveu... um contrato entre quem executa e quem escreve, alem de
> padronizar o tipo T em `chan<T>`."*

### 24.1 A conta refeita — o meu argumento de §23.3 CAI

Eu escrevi que nenhum canal nosso qualificava para `unbounded` porque *"o volume depende de quantos
testes falham"*. **Com registo padronizado isso deixa de ser verdade, e o dono tem razao:** cada
escrita ganha tecto, o numero de escritas sai de uma contagem que existe antes de correr, e
`volume = registos x tecto` fica conhecido. **O que era imprevisivel era o texto livre. Deixa de haver
texto livre.**

E fui medir o factor que faltava — **quantos registos por teste**:

| | medido |
|---|---|
| sitios de impressao em todo o corpus `.tkt` | **3**, em **2** ficheiros (de **81**) |
| logo, registos por `#test` | **1** (o veredicto), salvo tres excepcoes |
| veredictos unitarios | ~1167 |
| linhas de regressao | ~1155 |
| **total conhecido antes de correr** | **~2300 registos** |

**A conta do dono verifica-se do lado unitario.** A previsibilidade e real.

**E mesmo assim fico com `bounded` — por outra razao, e e esta que quero registada:**

> **O volume previsto e uma propriedade do caminho FELIZ. O canal existe para sobreviver ao infeliz.**

Os ~2300 registos sao o que uma corrida **correcta** produz. Um teste com um ciclo a imprimir produz
uma infinidade deles — e esse teste e precisamente o defeito que se quer encontrar. **Com `unbounded`,
esse defeito vira um OOM; com `bounded`, vira contrapressao e um relatorio.** Nos morremos de OOM duas
vezes no dia deste desenho, e nenhuma delas estava no plano.

E ha uma segunda metade, que a medicao tambem mostra: **o lado dos regressivos nao ganha tecto com
esta lei.** Um filho de regressao e uma build inteira do compilador, e o que sai do seu stdout sao
diagnosticos em texto livre. O contrato padroniza o que o **arnes** emite; nao padroniza o que uma
build de terceiros imprime. **Logo a previsibilidade vale para o trafego de veredicto e nao vale para
o trafego de saida de processo** — e o canal transporta os dois.

**Conclusao inalterada, argumento substituido: todos os nossos canais sao `bounded`.** As duas formas
existem porque o dono as pediu e servem quem escreve Teko; o compilador usa uma so.

### 24.2 O registo, e porque cada campo existe

Cinco campos. **Um campo que nao sirva a agregacao, o sumario ou a atribuicao nao entra**, e digo o
que ficou de fora e porque.

```teko
/**
 * Rec — UM registo de uma linha: o tipo unico que atravessa todos os canais deste desenho.
 *
 * UMA LINHA E O CONTRATO, e a razao e o fan-in: N handlers a entregar texto livre entrelacam-se e a
 * atribuicao perde-se (§16.5). Com um registo por linha e o escritor la dentro, o entrelacamento
 * deixa de ser possivel — nao por disciplina, por forma.
 *
 * @since 0.3.1
 */
pub type Rec = struct {
    /** quem escreveu (`s2`, `r0`) — a MESMA chave do segmento de journal (§2.2). Sem ela o fan-in
        nao sabe a que segmento apendar: e o campo que a §18.5 exigia e nao tinha forma. */
    writer: str
    /** ordinal monotonico POR ESCRITOR. Da a ordem por origem (§16.5) e torna uma perda
        DETECTAVEL — um salto na sequencia e uma falha nomeavel em vez de um silencio. */
    seq: u64
    /** a especie: `begin`/`ok`/`fail`/`skip`/`out`/`end`/`cont`. E por ela que o sumario de §13
        separa passados, falhados, saltados e nao-corridos sem interpretar texto. */
    kind: str
    /** o que o registo nomeia: o `#test` ou `<ficheiro>.tkr[<cenario>]`. E o que torna um achado
        NOMEADO em vez de contado (§13.3). */
    subject: str
    /** o corpo, com quebras de linha escapadas e limitado por `REC_MAX`. Uma linha que exceda parte
        em registos `cont` (§24.5). */
    payload: str
}
```

**O que NAO entra, e porque:** um **carimbo de tempo**. Ele convida a ordenar globalmente, e a §16.5
decidiu explicitamente que a ordem e **por origem e nao global** — uma ordem global seria uma promessa
mais cara e inutil, e um campo que a torna tentadora e um campo que a vai produzir.

### 24.3 O `T` — a primitiva fica generica, o nosso uso e que e instanciado

**`chan<T>` continua generico como superficie de linguagem.** Quem escreve Teko escolhe o seu `T`; a
primitiva nao ganha um tipo privilegiado, e um canal especializado no compilador seria a linguagem a
servir-se a si propria a frente do utilizador.

**O que se padroniza e o NOSSO uso: todos os canais deste desenho sao `chan<Rec>`.** A distincao
importa porque a §23 fixou o `push` como `-> error | null` para a primitiva inteira; nada disso muda
por o nosso `T` ser um so.

### 24.4 O contrato — imposto no FUNDO DE EMISSAO, nao por disciplina

Um contrato que so falhe em execucao, depois de o registo ja ter sido mal escrito, e convencao. Este
falha **antes**, e a razao e estrutural:

> **Um programa gerado so alcanca o stdout por `tk_print`/`tk_println`/`tk_write` — ha um fundo so.**
> Logo o registo nao e formatado por quem escreve: e **embrulhado** por quem emite.

Um `#test` que chame `teko::io::println("ola")` nao produz uma linha solta: produz um `Rec` com
`kind = "out"`, o `writer` e o `subject` do teste que corre (o canal por teste ja os tem,
`tk_test_begin`) e `payload = "ola"`. **Quem escreve nao pode violar o formato porque nunca o
escreve.** E o *"algo burro mas extremamente eficiente"* aplicado ao contrato.

*E a guarda e um `grep`, no espirito de §22:* **nada no programa gerado escreve no stdout fora
daqueles tres fundos.** Verificavel por varredura do C emitido; um `fputs(..., stdout)` novo no
codegen falha a guarda. *Vivacidade:* os tres fundos legitimos **passam**, senao a guarda estaria a
proibir a unica via que existe.

**O que NAO medi:** o custo do embrulho por linha impressa. E irrelevante no corpus de hoje — **3
sitios de impressao em 81 ficheiros** — mas o numero pertence ao crumb, nao a minha opiniao.

### 24.5 O `Oversize` deixa de existir — e o que sobra a proteger e melhor

O integrador tem razao: com registo limitado por contrato, um `Oversize` deixaria de ser condicao de
**ritmo** e passaria a **violacao de contrato**. **Mas a resposta certa nao e trata-lo melhor: e
elimina-lo.**

Uma linha que exceda `REC_MAX` **parte-se em registos `cont`** — o primeiro traz o inicio, os
seguintes trazem `kind = "cont"` com o mesmo `writer`/`seq`-base, e o sumarizador recompoe. **Partir
preserva a saida; truncar perde-a, e nao perder saida e a premissa deste documento inteiro.**

Com isso, **nenhum registo pode ser maior do que o anel**, e a razao `Oversize` do §23.4 **nao tem
como ser produzida**. Pela minha propria guarda de la — *"toda a razao tem de ter um teste que a
PRODUZA"* — ela **sai da enumeracao**. Ficam **quatro**: `Full` (a unica repetivel), `Closed`,
`NoReader`, `NotAProducer`.

**E o que sobra a proteger nao desaparece, muda de sitio e fica mais barato:** passa a ser uma
**invariante de arranque** — `capacidade_do_anel >= REC_MAX` — afirmada **uma vez**, e nao um ramo
avaliado em cada `push`. Uma verificacao de O(1) por processo em vez de O(1) por escrita.

**A minha conclusao de §23.4 mantem-se no que importa e melhora no resto:** eu disse que era preciso
distinguir *"agora nao cabe"* de *"nunca cabe"* para o ciclo de repeticao nao ser infinito. Continua
verdade — **e a lei do dono resolve-o por construcao em vez de por enumeracao**, que e melhor. O ciclo
de §23.2 so pode ver `Full`, e `Full` e sempre transitorio porque o consumidor esta vivo (a invariante
do consumidor, §16.4).

### 24.6 O custo

**Nenhum crumb novo, e o C1 encolhe:** sai um ramo de recusa e o seu teste, entra um `Rec` de cinco
campos, o embrulho no fundo de emissao e a divisao em `cont`. A §18.5 tinha registado *"registos, nao
fluxo de bytes"* como requisito **sem forma**; esta seccao e a forma, e vem do dono.

---

## 25. `pop -> T | error | null` — a simetria que eu so tinha de um lado

> *"O pop do canal, assim como o push, deve ser atomico, logo `ch.pop()` deve retornar
> `T | error | null`, onde o error e nosso checked e **null quer dizer que nada foi lido**."*

### 25.1 O que eu tinha, dito com exactidao

Fui reler-me antes de responder. `docs/.../journaling` §16.4 e §18.1:

```teko
/** recv — tirar o proximo registo do canal, BLOQUEANDO ate haver um. */
pub fn recv(c: Chan): ChanMsg | closed
```

**Duas saidas, e bloqueante.** Portanto: **nao tinha o defeito TOCTOU** — nunca houve um `is_empty()`
seguido de um `pop`, e um `recv` bloqueante e atomico por construcao. **Mas tambem nao tinha a forma da
lei**, e a razao nao e a atomicidade: e que **a lei do dono converte o `pop` de BLOQUEANTE em
NAO-BLOQUEANTE**, e e isso que parte a forma de duas saidas.

Um `recv` bloqueante nao precisa de dizer *"nada agora"* — ele espera. Um `pop` nao-bloqueante tem de o
dizer, e `null` e a palavra. **A leitura do integrador (`null => break`) estava errada sob a lei nova, e
estaria certa sob a minha antiga se `closed` se chamasse `null`** — que e exactamente a confusao que a
lei elimina ao separar *"nada agora"* de *"acabou"*.

E a lei e melhor do que o que eu tinha, por uma razao que nao e simetria estetica: **um `recv`
bloqueante da ao consumidor uma maneira de parar que nao e o `recv`** — ele fica preso la dentro. A
invariante de §16.4 dizia *"o orquestrador nunca bloqueia em nada que nao seja `recv`"*; com um `pop`
nao-bloqueante, **o orquestrador nao bloqueia em nada, ponto**, e a invariante deixa de ter excepcao.

### 25.2 A forma final

```teko
/**
 * pop — tirar o proximo registo, ATOMICAMENTE e sem bloquear.
 *
 * TRES SAIDAS, E A DO MEIO E A QUE FALTAVA. Um `is_empty()` seguido de um `pop` e TOCTOU — a mesma
 * corrida que o `push` de §23 elimina, na outra ponta. A pergunta e a accao sao a mesma operacao.
 *
 * `null` NAO E FECHO. E "agora nao havia nada", e o laco continua: um consumidor que chegue a frente
 * do produtor ve `null` dezenas de vezes numa corrida saudavel. Quem decide a terminacao e
 * `is_open` (§25.4), nunca esta funcao.
 *
 * @param c  o id do canal (§19 — o que atravessa a fronteira de tarefa e um nome)
 * @return   o registo, `null` quando nada havia, ou o checked da casa
 * @throws   quando o id nao nomeia um canal, ou quem chama nao e o leitor registado (§25.3)
 * @since 0.3.1
 */
pub fn pop(c: u64): Rec | error | null
```

E o orquestrador, na forma final — **as duas pecas que ja existiam, agora usadas em conjunto**:

```teko
/**
 * orchestrate — o consumidor: gira enquanto o canal estiver aberto, apenda o que vier.
 *
 * O `null` MANTEM O LACO A GIRAR; o fecho tira-o de la. Sair no `null` seria terminar uma corrida
 * VIVA so porque o consumidor foi mais rapido do que o produtor — e o defeito que esta seccao
 * corrige.
 *
 * @param c      o id do canal
 * @param sinks  o segmento de journal de cada escritor
 * @return       quantos registos foram apendados
 */
fn orchestrate(c: u64, sinks: []Journal): u64 {
    mut n: u64 = 0
    loop teko::threads::chan_is_open(c) {
        match teko::threads::chan_pop(c) {
            Rec as r => { append_to_segment(sinks, r); n = n + 1 }
            null => { match teko::threads::chan_await_record(c, AWAIT_MS) { error => { }; null => { } } }
            error as e => { report_reader_fault(e); break }
        }
    }
    n
}
```

### 25.3 As razoes de `error` na LEITURA — sao duas, e a assimetria com a escrita tem explicacao

| razao | estado que a produz | repetivel? |
|---|---|---|
| **`NoSuchChannel`** | o id nao nomeia um canal vivo (id obsoleto, §19/F3) | nao — terminal |
| **`NotAReader`** | quem chama nao e o leitor registado — a fronteira MPSC de §18 | nao — terminal |

**Duas, contra quatro do lado da escrita, e a assimetria nao e descuido:** o escritor disputa um
**recurso que flutua** (espaco), logo tem uma razao *repetivel* (`Full`). O leitor nao disputa nada —
ou ha registo ou nao ha, e **"nao ha" ja e o `null`**. A condicao transitoria da leitura esta
codificada na saida do meio, e por isso nao aparece como erro. **Nenhuma razao de leitura e
repetivel**, e o `break` do laco em §25.2 e por isso correcto e nao pessimista.

**E isto corrige a minha propria §18.2, por coerencia com a lei do §23.** La eu fiz o segundo leitor
**panicar**. A lei do dono e *"sem panico: devolve o veredicto"*, e nao ha razao para o `push` a
obedecer e o `pop` nao. **`NotAReader` passa a ser `error`, e o panico sai.** A garantia nao enfraquece
— continua a ser de runtime, deterministica e nomeada; muda o mensageiro.

*Guarda, a minha de §23.4 aplicada aqui:* **toda a razao tem de ter um teste que a PRODUZA** — dois
testes, um por razao · **vivacidade**: um `pop` normal devolve `Rec` e um canal vazio devolve `null`,
senao um canal que errasse sempre passaria os dois.

### 25.4 A terminacao sem o `null` — e a definicao de `is_open` que a torna correcta

O `closed` de §16 **desaparece do `pop`**. Nao vira `error`: *"todos os produtores acabaram"* e o fim
NORMAL, e por o caminho feliz num ramo de erro seria mentir sobre o que aconteceu. Ele **vira uma
propriedade do canal**, observada por `is_open` — que e onde o dono ja o tinha posto.

**E ha aqui uma armadilha que a pergunta destapa, e ela e perda de dados.** Se `is_open` fosse
simplesmente *"ha produtores vivos"*, o ultimo produtor a terminar poria a condicao a falso **com
registos ainda no anel**, e o laco sairia deixando-os por ler. Portanto:

> **`is_open(c)` = `produtores_vivos > 0` **OU** `registos_no_anel > 0`.**

Assim ele fica falso **exactamente** quando o ultimo produtor acabou **e** tudo o que ele escreveu ja
foi consumido — nem antes (perda), nem depois (impasse).

**A terminacao prova-se, e a prova e curta:** os produtores sao finitos (2 por filho, §16.4); cada um
decrementa a contagem **uma** vez, ao ver EOF no seu tubo; o EOF e garantido porque o filho ou sai ou e
morto, e nos dois casos o tubo fecha; e o anel drena porque o consumidor so sai quando ele esta vazio.
**Logo a condicao vai a falso e o laco termina.**

*Prova:* um canal com registos por ler **e** zero produtores vivos ainda diz **aberto** · so depois do
ultimo `pop` diz fechado · **inversao**: com `is_open` definido apenas por produtores, o mesmo cenario
**perde** registos — e e essa a versao que tem de falhar o teste.

### 25.5 A espera do lado da leitura — irma, e com a mesma disciplina

Sim, precisa. Com `null` a significar *"nada agora"*, o laco gira em vazio e queima CPU. O irmao do
`chan_await_space` e o `chan_await_record(c, timeout_ms)`, e **leva a mesma regra: e uma DICA, nunca uma
garantia.**

**E aqui tenho de ser honesto contra o meu proprio argumento.** Do lado da escrita, o `await` nao podia
ser garantia porque ha N produtores e outro pode ganhar a vaga. **Do lado da leitura ha UM leitor**, e
ninguem lhe tira o registo — tecnicamente o `await` **poderia** ser garantia. Nao deve ser, por duas
razoes que consigo defender:

1. **o `null` deixaria de ocorrer no caminho normal**, e uma saida que nunca ocorre e uma saida que
   ninguem testa — e o dia em que ela ocorrer sera o dia em que ninguem sabe o que faz;
2. **a garantia quebra em silencio no N:M.** A §18.4 ja regista a segunda estrutura com multiplos
   consumidores; nesse dia, um `await` que fosse garantia passaria a mentir sem que uma linha mudasse.

**Custo de a manter dica: um ramo.** Custo de a tornar garantia: uma promessa que o proprio documento
ja preve quebrar. **O `pop` continua a ser a unica autoridade nas duas pontas.**

**O que NAO medi:** o valor certo de `AWAIT_MS` e o custo da espera activa sem o `await`. Sao medicoes
do crumb.

### 25.6 O que muda no F4

**Superficie, nao crumb.** O F4 (§20.1) ja constroi o `tk_chan`; isto fixa-lhe a forma das duas pontas.
O saldo e negativo em trabalho: **sai** o caminho bloqueante do `recv` e **sai** o panico do segundo
leitor; **entra** a saida `null`, o `chan_await_record` e a definicao de `is_open` com as duas parcelas.
**11 crumbs, sem alteracao.**

---

## 26. O `Rec` binario, com assercoes e cobertura — e a Lei 3 fa-lo ENCOLHER

As tres leis condicionam-se, e a ordem de leitura importa: a **Lei 3** tira o texto livre do canal, e
so por causa disso a **Lei 2** cabe la dentro sem estourar o tecto que a **Lei 1** precisa de ter.

### 26.1 Verificacoes que me foram pedidas — duas confirmadas, uma REFUTADA

| afirmacao | veredicto |
|---|---|
| `write_u32` desloca bytes ⇒ little-endian fixo, independente do hospedeiro | **confirmado** (`tkb_buf.tks:8-15`) |
| 24 funcoes de assercao distintas, volume concentrado em tres | **confirmado**: **24** distintas, **4185** chamadas, e `is_true` **3237** + `str_contains` **574** + `is_false` **311** = **4122**, ou seja **98,5 %** |
| *"o `.tkb` nao tem magic nem versao"* | **REFUTADO** |

**O `.tkb` TEM os dois, e o precedente e melhor do que se pensava.** `tkb_read.tks:1007` e `:1032`
devolvem `"not a .tkb (bad magic)"`; `:1010` e `:1035` comparam `TKB_EXPR_VERSION` e
`TKB_PROGRAM_VERSION` e **a mensagem de recusa diz o que fazer** (*"pre-0.3.1 artifacts ... must be
rebuilt with the current compiler"*); e `:1016`/`:1041` verificam um hash. **Magic + versao + hash +
remedio.** Nao ha nada a inventar: o `.tkj` copia esta forma.

### 26.2 A Lei 3 aplicada primeiro — e o `Rec` ENCOLHE

Eu escrevi em §24.4 que um `#test` a chamar `println("ola")` produzia um `Rec` de `kind = "out"`
embrulhado pelo fundo de emissao. **Sai, e a lei esta certa: sao dois fluxos e nao um.**

E ha um sitio onde por o texto livre que ja existe e ja foi construido para isto — o **terceiro canal**
(`VERDICT_CHANNEL_ENV`, §15.1), que ficou no desenho precisamente porque *"ha exactamente tres fluxos
herdaveis em POSIX e em Windows"*. Logo:

| fluxo | por onde | atribuicao |
|---|---|---|
| **texto livre** (prints, diagnosticos de build) | **stdout/stderr** do filho → o handler reencaminha para o stdout/stderr do orquestrador | por **prefixo** `out\|`/`err\|`, ja medido em uso |
| **registos estruturados** (assercao, cobertura, veredicto) | **o terceiro canal** → `chan<Rec>` → segmento `.tkj` | por **campo** `writer`, dentro do registo |

**E dai vem o encolhimento, em cascata:**

1. **sai a variante `out`/`err`** do `Rec`;
2. o que sobra no canal e **diagnostico**, nao **produto** — e por isso **a variante `cont` de §24.5
   dissolve-se**: eu tinha escolhido *partir* em vez de *truncar* porque perder saida violava a
   premissa. Com o produto fora do canal, o que la viaja e um `expected`/`got` que se localiza nos
   primeiros bytes. **Passa a cap com marca explicita `truncated`**, e a razao da inversao esta dita:
   truncar o produto era perda, truncar um diagnostico e um corte assinalado;
3. **`REC_MAX` deixa de ter de acomodar texto arbitrario**, que era a condicao que o integrador
   nomeou;
4. e a previsibilidade de §24.1 **volta a ser funcao da estrutura**: um ciclo patologico a imprimir
   inunda o **stdout**, que tem contrapressao do SO, e nao o anel.

**O custo declarado, e nao escondido:** as duas metades passam a ter garantias **diferentes**. Entre
**processos** a saida livre e atribuida por prefixo. Entre **raias**, com `print` directo para um
stdout partilhado, **N raias entrelacam-se e a atribuicao perde-se**. E defensavel — o texto livre de
uma raia e diagnostico humano, e o que precisa de atribuicao (veredicto, assercao, cobertura) viaja
pelo canal com o `writer` dentro — mas **e decisao, nao consequencia**, e fica escrita como tal.

### 26.3 A forma do `Rec` — variante por especie

```teko
/**
 * Rec — um registo do journal. VARIANTE e nao struct plana: um `assert` e um `cov` nao tem campos em
 * comum alem do cabecalho, e uma struct plana pagaria os dois em cada registo.
 *
 * SEM `out`/`err` (Lei 3): texto livre nao entra no tunel.
 *
 * @since 0.3.1
 */
pub type Rec = variant RecAssert | RecCov | RecVerdict

/**
 * RecHead — o que TODO o registo carrega, e nada mais entra aqui.
 *
 * @since 0.3.1
 */
pub type RecHead = struct {
    /** quem escreveu — a mesma chave do segmento (§2.2) e o que torna o fan-in atribuivel. */
    writer: str
    /** ordinal monotonico POR ESCRITOR: da a ordem por origem (§16.5) e torna uma perda DETECTAVEL. */
    seq: u64
    /** o que o registo nomeia: o `#test` ou `<ficheiro>.tkr[<cenario>]`. */
    subject: str
}

/**
 * RecAssert — UMA assercao: o que tocou, o que era esperado, o que foi medido.
 *
 * ISTO E O QUE FALTAVA, e o custo de nao o ter esta medido: a fixture `ref_mutable_binder` pontuava
 * SETE assercoes em sete bits do codigo de saida, e quando falhou o que se soube foi `exit 99,
 * expected 127` — bits descodificados a mao. Pior: os 7 bits so cobriam escritas de CAMPO, logo a
 * fixture nao via a reatribuicao de valor inteiro, e o referente escalar dava SIGSEGV enquanto ela
 * devolvia um numero plausivel. **Um numero que aliasa direccoes nao medidas e pior que um teste em
 * falta.** Com esta variante, cada assercao e uma linha com nome, esperado e medido.
 *
 * @since 0.3.1
 */
pub type RecAssert = struct {
    /** o cabecalho comum. */
    head: RecHead
    /** qual das 24 (`is_true`, `str_contains`, …) — a especie decide como `expected`/`got` se leem. */
    kind: str
    /** `<ficheiro>:<linha>:<coluna>` do sitio de chamada, injectado pelo codegen (§26.5). */
    site: str
    /** o texto do argumento, tal como escrito na fonte — o "o que tocou". */
    expr: str
    /** o que era esperado, ja renderizado. */
    expected: str
    /** o que foi medido. */
    got: str
    /** verdadeiro quando `expected`/`got` foram cortados em `REC_MAX` — corte ASSINALADO, nunca mudo. */
    truncated: bool
}

/**
 * RecCov — cobertura, LINEAR a medida que entra (a lei do dono sobre o `.tkcov`, §5.2).
 *
 * COM UM CONSUMIDOR SO, A SOBREPOSICAO POR CONCORRENCIA DEIXA DE TER COMO ACONTECER — que era o
 * defeito com que este documento comecou. O agregador le no fim, como ele ja tinha fixado.
 *
 * @since 0.3.1
 */
pub type RecCov = struct {
    /** o cabecalho comum. */
    head: RecHead
    /** que sumidouro: `fn` / `line` / `branch`. */
    sink: str
    /** os identificadores empacotados que este registo acrescenta (o formato do `.tkcov`, §5.2). */
    ids: []u64
}

/**
 * RecVerdict — o veredicto MEIO-PRONTO de um caso: decidido na origem, somado no fim.
 *
 * "MEIO-PRONTO" E O CONTRATO: quem correu o caso ja sabe se ele passou, falhou ou saiu — e essa
 * decisao viaja decidida. O que o sumarizador NAO recebe pronto esta em §26.6, e e exactamente o que
 * so ele pode fazer.
 *
 * @since 0.3.1
 */
pub type RecVerdict = struct {
    /** o cabecalho comum. */
    head: RecHead
    /** `ok` / `fail` / `exited` / `skip` / `not-applicable` (§21.3). */
    outcome: str
    /** o valor de `exit(n)` quando `outcome` e `exited` (§14); 0 nos outros. */
    code: i32
    /** quantas assercoes este caso correu — permite detectar um caso que morreu a meio. */
    asserts: u64
    /** nanossegundos de parede do caso. */
    ns: u64
}
```

### 26.4 O enquadramento binario, e porque ele e o argumento mais forte

**O argumento do enquadramento e melhor do que o da velocidade, e e o que defende a lei.** Um registo
de uma linha em texto tem de responder ao que acontece quando a carga contem uma quebra de linha — **e
contem**: `expected`/`got` de um `str_contains` sobre um diagnostico de compilador tem-nas as dezenas
(e o `COMPILE_FAIL_HEAD_LINES` existe precisamente para as cortar). Ou se escapa, pagando custo e bugs
**nas duas pontas**, ou se quebra a invariante de uma linha e **o entrelacamento volta**. **Com quadro
prefixado por comprimento, a carga e opaca e o conteudo nao pode corromper o enquadramento.**

```
cabecalho do ficheiro (uma vez):
  magic     "TKJ1"        4 bytes
  format    u32           a versao do FORMATO — governa a leitura
  toolchain u32 u32 u32   a versao do compilador que escreveu — governa a INTERPRETACAO (§26.4.1)

quadro (repetido, append-only):
  len   u32               bytes da carga que se segue
  kind  u8                1=assert 2=cov 3=verdict
  carga len bytes         opaca ao enquadramento
```

Tudo little-endian por `write_u32` (`tkb_buf.tks:8-15`), **sem hash de ficheiro**: um `.tkj` cresce por
append e um hash total exigiria reescreve-lo. **Um quadro rasgado no fim detecta-se por `len` maior do
que o que resta** — o analogo append-only da regra de §4 (*"a ultima linha sem `\n` e descartada"*),
e a mesma resposta ao mesmo modo de falha.

#### 26.4.1 A travessia entre maquinas — e a descoberta que ela obriga

O cenario do dono — *"CI falhou mas guardou o journal, baixo e inspecciono na minha maquina"* —
**e LEITURA, nao execucao nem transpile**. Um journal e o registo do que aconteceu, nao um programa.
Confirmo a leitura do integrador.

**E ele obriga a uma distincao que eu nao tinha: nem tudo no `.tkj` e igualmente portavel.**

| conteudo | portavel entre versoes? |
|---|---|
| `RecAssert`, `RecVerdict` | **sim** — sao auto-descritivos, texto e numeros |
| `RecCov` | **NAO** |

**A razao esta medida na arvore**, no comentario do proprio protocolo `.tkcov`: *"the coverage id is
the prog.items index in BOTH processes (they share the same TProgram)"*. **Um identificador de
cobertura e um indice para um `TProgram` — e dois compiladores diferentes tem `TProgram` diferentes.**

Logo a regra do leitor, e ela e precisa em vez de binaria: **`format` diferente ⇒ recusa total, com a
mensagem no estilo do `.tkb` (dizer o remedio). `toolchain` diferente ⇒ assercoes e veredictos
renderizam-se; a cobertura e RECUSADA com a razao nomeada.** Renderizar cobertura de outra versao
seria interpretar indices para uma tabela que nao se tem — o erro escondido classico.

**A fronteira de conversao:** o `.tkj` em disco e **sempre** binario; o orquestrador desserializa,
imprime o humano e reescreve o binario no segmento. E **`--replay` le o binario** — e a mesma leitura
que o `teko journal <path>.tkj -o saida.log` faz, com dois frontends sobre **um** leitor.

### 26.5 A superficie de assercao — 4185 sitios, ZERO tocados

**As tres de maior volume sao 98,5 % das chamadas** (3237 + 574 + 311 de 4185). Se mudar a assinatura
delas custasse tocar nos sitios, isto era um mes.

**Nao custa, e a razao ja esta a funcionar na arvore ao lado.** `teko::assert::*` sao **builtins
injectados** (`scope.tks`), logo **o codegen e que lowera a chamada**. Ele ja faz exactamente isto para
outra coisa: `emit_cov_line` emite `tk_cov_line_at(<fn_idx>, <line>)` — **a posicao e injectada pelo
compilador, nao escrita pelo autor**.

Portanto: `teko::assert::is_true(c)` lowera para `teko__assert__is_true(c, <site_id>)`, onde `site_id`
indexa uma tabela estatica que o codegen emite com `<ficheiro:linha:coluna>` e **o texto do argumento
tal como escrito**. **Os 4185 sitios de chamada nao mudam um caracter.** O que muda: uma tabela nova no
emissor, o parametro extra nas 24 assinaturas do seed C, e o `RecAssert` a ser emitido em vez de
(apenas) o panico.

**Isto e a diferenca entre um crumb e um mes, e o precedente esta no mesmo ficheiro.**

*Guarda:* o `expr` de um `RecAssert` tem de **casar com a fonte** naquele `site` — verificavel lendo o
ficheiro na posicao dada · **vivacidade**: duas assercoes na mesma linha tem `site` **diferentes**
(coluna), senao a tabela esta a colapsar sitios.

### 26.6 Quem agrega o que

| chega **pronto** no `Rec` | o sumarizador ainda **calcula** |
|---|---|
| o veredicto de cada caso (`outcome`, `code`) | as somas por fase e o TOTAL (§13) |
| cada assercao com `expected`/`got`/`site` | a lista de achados NOMEADOS, ordenada |
| os ids de cobertura por sumidouro | a **uniao** entre escritores — so o pai a pode fazer |
| a contagem de assercoes e o tempo por caso | as **fasquias**: exigem a caminhada estatica sobre o `TProgram`, que so o pai tem |
| — | `never-ran` = `plan` − recebidos (§13.2) |

**A regra: o sumarizador nao RE-DECIDE nada.** Ele soma, une e renderiza. Um veredicto que chegasse
por decidir poria a decisao longe de quem tem os factos — que e o defeito do `exit 99` da
`ref_mutable_binder`, uma camada acima.

### 26.7 O custo

**Nenhum crumb novo, e o saldo e negativo.** Toca no **C1** (o enquadramento e o `Rec`), no **C4** (o
sumarizador recebe mais pronto e calcula menos) e acrescenta o subcomando `teko journal` ao **C9**, que
ja era o `--replay` — **dois frontends, um leitor**. **Nao toca em F1–F6**: e superficie do que o F4 ja
constroi. E **sai** trabalho: a variante `cont`, o embrulho de `out`/`err` no fundo de emissao, e a
necessidade de `REC_MAX` acomodar texto arbitrario. **11 crumbs.**

---

## 27. Assercoes esperadas × executadas — e o terceiro modo de um teste ser verde sem afirmar nada

> *"Hoje nao temos visibilidade de quantas assercoes deveriam ocorrer... nada passa pelo invisivel ao
> gate... podem ser condicionais... ao menos o numero de assertividades executadas sao passiveis de
> medicao."*

### 27.1 Metade ja existe, e e a metade cara — confirmado na fonte

`src/checker/test_assert.tks` (172 linhas) ja anda o corpo de um `#test` e devolve
`AssertStats { total, folded }`. **E a descida ja ve os construtos condicionais todos** — medido em
`assert_stats_stmt` e `assert_stats_expr`:

| construto | ja descido |
|---|---|
| `TIfExpr` (then + else) | `assert_stats_if` |
| `TMatchExpr` (todos os arcos) | `assert_stats_match` |
| `TLoopStmt` (init + body) | `assert_stats_stmt:82` |
| `TDeferStmt` | `:83` |
| `TAdoptStmt` | `:84` |

**A estrutura esta la; falta o PARAMETRO.** Distinguir obrigatoria de fluxo e passar uma profundidade
condicional pela recursao que ja existe — **nao e analise nova, e um argumento a mais**.

**E ha uma lacuna que tenho de nomear, medida:** `is_bool_assert_call` conta **so** `is_true` e
`is_false` (`:164-172`), e exclui `str_contains` **de proposito** (*"takes two string arguments, not a
bool predicate"*). Isso e correcto para o problema que o modulo resolve (dobra de predicado), e
**errado para este**: as 574 chamadas de `str_contains` seriam invisiveis ao esperado. **Para esta lei,
a contagem tem de ser das 24, nao das duas.**

### 27.2 Obrigatoria vs fluxo — computavel SEM heuristica, e o terceiro caso existe

**Confirmo a leitura do integrador**, e ela e exacta: *obrigatoria* = a assercao esta na raiz do corpo;
*fluxo* = aninhada em qualquer construto condicional. Nao ha heuristica: e uma propriedade do caminho
sintactico, e o `fold` ja o percorre.

**Duas afinacoes e um caso que ele nao viu:**

1. **`loop` e sempre fluxo.** Um `loop <cond>` pode correr zero vezes — o corpo nunca e garantido.
2. **`defer` classifica-se pelo SITIO DO `defer`, nao pelo corpo dele.** Um `defer` na raiz dispara na
   queda do fim do corpo (§14.2 — o replay acontece nos arcos de saida), logo **as suas assercoes sao
   obrigatorias**; um `defer` dentro de um `if` e fluxo. E a mesma regra aplicada uma linha acima, e
   isso e bom: a classificacao continua a ser uma so.
3. **O TERCEIRO CASO, e e o que ele pediu para eu nomear: uma assercao na raiz DEPOIS de um `return`
   condicional.** Sintacticamente esta na raiz — profundidade zero — e a minha regra chamar-lhe-ia
   obrigatoria. Mas um `if x { return }` antes dela pode salta-la, e o portao acusaria um teste sadio.
   **Tambem e computavel sem heuristica, mas nao por profundidade: e uma propriedade do BLOCO.**

> **Regra completa: uma assercao e OBRIGATORIA quando esta a profundidade condicional zero E nenhum
> `return`/`break`/`continue` condicional a precede no seu bloco. Todas as outras sao FLUXO.**

### 27.3 A regra do portao — e melhoro-a, porque contar nao chega

A proposta era *"toda a obrigatoria tem de executar; as de fluxo sao contadas e relatadas"*. **A
direccao esta certa; a metrica e fraca.** Uma contagem bate com identidades trocadas: um teste com duas
obrigatorias que executa uma delas duas vezes (num `loop`) da 2 = 2 e passa.

**E o remedio ja esta pago pela §26.5:** cada `RecAssert` traz o seu `site_id`. Logo:

> **O CONJUNTO dos `site_id` obrigatorios tem de estar CONTIDO no conjunto dos `site_id` que
> chegaram. As de fluxo sao contadas e relatadas, nunca exigidas.**

Contencao de conjuntos, nao cardinalidade. Custa o mesmo — o `site_id` ja viaja — e nao se deixa
enganar por repeticao.

*Prova:* um teste com duas obrigatorias, uma delas num ramo morto, **falha** nomeando o `site` que nao
chegou · **vivacidade**: um teste com duas obrigatorias executadas passa · **inversao**: com a regra por
cardinalidade, o teste que executa uma duas vezes **passa** — e e essa versao que tem de falhar o teste
da guarda.

### 27.4 O terceiro modo de ser verde sem afirmar nada — e ele e MEDIDO

O integrador perguntou se as duas leis juntas fecham as duas maneiras. **Fecham duas, e ha uma
terceira, e nao e a que se esperava.**

| # | modo | quem apanha |
|---:|---|---|
| 1 | a assercao **nao pode falhar** (predicado dobra em constante) | `test_assert.tks` hoje — `MISLEADING`/`FOUNDATIONAL` |
| 2 | a assercao **nunca foi tentada** (ramo morto) | **esta lei** |
| 3 | a assercao **e invisivel a analise** | **ninguem** |

**O terceiro, medido:** varri os 1042 `#test` do corpus. **102 (9,8 %) nao tem uma unica
`teko::assert::` DIRECTA no corpo.**

**E digo ja o que este numero NAO e, para nao repetir o erro da §22:** nao sao 102 testes que nao
afirmam nada. Fui verificar um — `comptime_fold_test.tkt:131` — e ele chama `cf_int_is(...)` e
`cf_bool_is(...)`, auxiliares locais que afirmam la dentro. **O numero mede INVISIBILIDADE A ANALISE,
nao ausencia de assercoes.**

**E e por isso que e um achado e nao um alarme:** `test_assert.tks` anda o corpo do `#test` e **nao
segue chamadas**. Para esses 102, `total = 0` — logo a regra `FOUNDATIONAL` (que exige `total >= 1`) ja
os ignora hoje, **e a lei nova acusaria zero obrigatorias onde ha tres**. Duas analises cegas ao mesmo
sitio.

> **Consequencia de desenho, e nao e opcional: a contagem estatica tem de seguir pelo menos UM salto
> para auxiliares locais do proprio ficheiro de teste. Sem isso, a lei produz falsos negativos em
> 9,8 % do corpus** — e um portao que nao ve um decimo dos testes e o *"invisivel ao gate"* que a lei
> existe para acabar.

Um salto chega para o corpus de hoje (o auxiliar afirma directamente); a profundidade e um parametro,
e **se um dia um auxiliar chamar outro, a medicao dira** — o portao relata quantos testes ficaram com
`total = 0` depois do salto, e esse numero e ele proprio a guarda.

### 27.5 As regressoes — mesmo regime para as assercoes, outro para os passos

O `.tkr` tem **duas camadas**, e so uma entra neste regime:

* **as `teko::assert::*` DENTRO da fixture** — mesmo regime, sem excepcao. A fixture liga o mesmo
  runtime e usa os mesmos builtins; o esperado calcula-se da fonte dela no momento em que o arnes a
  compila, que e o momento em que o arnes ja a tem nas maos;
* **os passos `Then` do `.tkr`** — sao expectativas da LINHA, de outra granularidade, e ja sao
  contaveis por si. Ficam onde estao.

**E a `ref_mutable_binder` de hoje e o caso trabalhado desta lei.** Sete assercoes em sete bits do
codigo de saida; quando tres cairam, `exit 99, expected 127` nao dizia se elas **falharam** ou **nunca
correram** — e a diferenca era o diagnostico inteiro. Sob esta lei: a fixture emite sete `RecAssert`
com `site_id`; o arnes sabe quais sao obrigatorias; e as tres em falta sao relatadas como **NAO
EXECUTADAS**, nomeadas pelo sitio, e distintas das falhadas. **O `expected`/`got` de §26.3 diz o resto,
e ninguem descodifica bits a mao.**

### 27.6 O custo

**Nenhum crumb novo. Nao toca F1–F6.** Toca em:

* `src/checker/test_assert.tks` — o parametro de contexto (obrigatoria/fluxo), o alargamento das 2
  assercoes para as 24, e o salto de um nivel para auxiliares locais;
* **C4** — o sumarizador passa a comparar dois conjuntos de `site_id` em vez de contar;
* **C1** — nada: o `site_id` ja viaja por §26.5.

**11 crumbs.** E o mais barato dos tres pedacos ja esta escrito: a descida por `if`/`match`/`loop`/
`defer`/`adopt` existe e esta provada em producao.

---

## 28. Duas especies, nao tres — e a forma anterior fazia a cobertura EXPLODIR

### 28.1 A sintaxe, confirmada na arvore antes de escrita

**Uma `variant` em Teko e uniao de tipos NOMEADOS.** Nao ha forma anonima — varri `src/` e nao existe
um unico `variant {`. O precedente e uniforme:

```
pub type Statement   = variant Binding | Assign | Return | LoopStmt | ... | AdoptStmt   (ast.tks:343)
pub type BindTarget  = variant SimpleName | DestructurePattern                          (ast.tks:265)
pub type Pattern     = variant LiteralPattern | RangePattern | ... | NullPattern        (pattern.tks:32)
```

### 28.2 O dimensionamento — o dono apanhou um desastre, e esta medido

A forma anterior disparava **uma mensagem por evento**. Para a cobertura isso significa **um registo
por LINHA EXECUTADA**: `emit_cov_line` emite `tk_cov_line_at(<fn_idx>, <line>)` em cada linha
instrumentada (§5.2). **O meu proprio numero de §24.1 — ~2300 registos — e essa forma nao podiam
coexistir**, e eu escrevi os dois sem os ligar. O dono ligou.

**A regra dele e a que a arvore ja pratica:** acumular em tabelas e despejar **uma vez** — que e
exactamente o que `tk_cov_dump` faz (`teko_rt.c:2698`) e o que a §5.2 mediu como **ja correcto**. A
cobertura **nao vira trafego de canal**.

### 28.3 A forma

```teko
/**
 * Veredict — o veredicto de UM facto: de quem e, e como correu.
 *
 * NAO E UMA ESPECIE DE REGISTO — e um campo. Foi isto o "meio-pronto": o veredicto nasce COLADO ao
 * facto que o produziu, no sitio onde os dados estao, em vez de viajar a parte e ter de ser
 * reconciliado. A regra de §26.6 — "o sumarizador nao RE-DECIDE nada" — fica mais forte: ele nem
 * sequer tem de emparelhar.
 *
 * @since 0.3.1
 */
pub type Veredict = struct {
    /** o caso a que este facto pertence (indice na tabela de sujeitos que o codegen ja emite). */
    subject: u32
    /** como correu: 0 ok · 1 falhou · 2 saiu (`exit(n)`, §14) · 3 saltado · 4 nao-aplicavel (§21.3). */
    how: u8
}

/**
 * Assert — UMA assercao executada: onde, se passou, o esperado, o medido, e o seu veredicto.
 *
 * `site` E RESERVADO NO ZERO (§28.4): `site == 0` nao nomeia uma assercao, nomeia o SUJEITO — e o
 * registo com que o arnes diz "este caso terminou, e foi assim". E a unica adicao minha a forma do
 * dono, e existe porque um caso que saia por `exit(7)` nao tem assercao que carregue o seu desfecho.
 *
 * @since 0.3.1
 */
pub type Assert = struct {
    /** o sitio de chamada (a tabela que o codegen emite, §26.5); 0 = o proprio sujeito. */
    site: u32
    /** passou ou nao. */
    ok: bool
    /** o esperado, opaco ao enquadramento (§26.4). */
    expected: []byte
    /** o medido. */
    got: []byte
    /** o veredicto deste facto. */
    veredict: Veredict
}

/**
 * Cov — o marcador de que o despejo de cobertura deste escritor esta COMPLETO.
 *
 * VAZIO, E O VAZIO E O PONTO. A cobertura nao viaja pelo canal: ela acumula nas tabelas e despeja
 * uma vez, exactamente como ja faz. Este registo nao transporta identificadores — transporta o FACTO
 * de o despejo existir, UM por escritor e nao um por linha.
 *
 * E TEM UM TRABALHO REAL: e ele que torna um despejo em falta detectavel. O defeito medido em §2.4 —
 * uma shard que morre antes de despejar deixa no lugar o ficheiro da corrida ANTERIOR, e as fasquias
 * sao calculadas sobre ele — fecha-se aqui: sem `Cov` de um escritor, o sumarizador sabe que aquele
 * despejo nao e desta corrida.
 *
 * @since 0.3.1
 */
pub type Cov = struct { }

/**
 * RecBody — o corpo de um registo. DUAS especies, nao tres.
 *
 * @since 0.3.1
 */
pub type RecBody = variant Assert | Cov

/**
 * Rec — um registo do journal.
 *
 * @since 0.3.1
 */
pub type Rec = struct {
    /** quem escreveu (indice, nao string — §26.2 dizia `str`; um `u32` e mais barato e o nome vive
        no cabecalho do ficheiro, escrito uma vez). */
    writer: u32
    /** ordinal monotonico por escritor: ordem por origem (§16.5) e perda DETECTAVEL. */
    seq: u64
    /** o corpo. */
    body: RecBody
}
```

### 28.4 O buraco que a forma de duas especies abre, e o menor conserto

**Com o veredicto dentro da assercao, um caso que NAO chega a nenhuma assercao nao emite registo
nenhum** — e um `exit(7)` capturado pelo C0 (§14) e exactamente esse caso. Sem remedio, o desfecho
mais interessante do C0 fica sem transporte.

**O menor conserto, e e por isso que nao acrescento uma terceira especie: `site == 0` e RESERVADO
para o sujeito.** Um registo com `site = 0` diz *"o caso `subject` terminou, e `how` diz como"*. Uma
constante reservada, documentada, contra uma especie inteira — e a forma do dono fica intacta.

**E o resto do desfecho e DERIVADO, nao transportado**, o que e melhor: pela §27, o arnes conhece o
conjunto de `site_id` **obrigatorios** de cada caso; o sumarizador compara-o com os que chegaram.
Passou = todos os obrigatorios chegaram e todos com `ok`. Falhou = algum com `ok = false`. **Nao
executado = obrigatorio que nao chegou** — que e precisamente a distincao que faltou na
`ref_mutable_binder` (§27.5).

### 28.5 A conta refeita — e a decisao `bounded` fica mais forte

| | antes (§24.1) | agora |
|---|---:|---:|
| registos de veredicto | ~2300 (um por caso) | **0** — o veredicto e campo |
| registos de assercao | 0 | **~4173** (um por assercao EXECUTADA) |
| registos de sujeito (`site = 0`) | — | ~2300 |
| registos de cobertura | (indefinido — teria sido **por linha executada**) | **1 por escritor** (~8) |
| **total** | ~2300 | **~6500** |

Medi tambem o factor que quebra a previsibilidade, e **ele mudou de natureza**: uma assercao dentro de
um `loop` executa N vezes. **Estimativa GROSSEIRA — e digo-o porque o meu varredor conta chaveta por
indentacao e nao por sintaxe: ~29 dos 4173 sitios estao dentro de um `loop`.** Poucos, mas nao zero.

**E isso torna o argumento de §24.1 melhor:** eu tinha justificado `bounded` dizendo que *"o volume
previsto e uma propriedade do caminho feliz"* e ilustrado com um teste patologico a imprimir. **Ja nao
preciso do patologico:** um teste **sadio** que afirme dentro de um ciclo de mil voltas emite mil
registos, e nada nisso e defeito. **A imprevisibilidade e agora legitima, e `bounded` deixa de ser uma
defesa contra o erro para passar a ser a forma correcta para o uso normal.** Decisao inalterada,
argumento mais forte.

### 28.6 A portabilidade — e uma simplificacao real

**Confirmo, e e melhor do que eu tinha.** A §26.4.1 dizia que o `RecCov` **nao** atravessava versoes,
porque um identificador de cobertura e um indice para um `TProgram` e dois compiladores tem `TProgram`
diferentes. **Com o `Cov` vazio, o `.tkj` deixa de transportar um unico identificador de cobertura.**

| ficheiro | portabilidade |
|---|---|
| **`.tkj`** | **INTEIRAMENTE portavel** — so `site`, `subject`, `ok`, `expected`, `got`, `how` |
| `.tkcov` | herda a restricao, e **ja a tinha**: e o formato cujos ids sao indices de `TProgram` |

Cai a regra composta que eu tinha desenhado (*"`toolchain` diferente ⇒ assercoes renderizam, cobertura
e recusada"*). **Fica uma regra simples: `format` diferente ⇒ recusa com remedio, no estilo do `.tkb`
(§26.1). `toolchain` diferente ⇒ le-se tudo.** O `site` e o `subject` sao indices para tabelas que o
proprio `.tkj` carrega no cabecalho, nao para um `TProgram` externo — e e isso que os torna portaveis.

**O cenario do dono — *"CI falhou, baixo o journal e inspecciono ca"* — passa a funcionar sem
excepcoes.** A restricao nao desapareceu: **mudou para o ficheiro que sempre a teve.**

### 28.7 O custo

**Nenhum crumb novo. Nao toca F1–F6.** O saldo continua negativo: **sai** uma especie de registo
(`RecVerdict`), **sai** o `writer: str` (passa a `u32` com a tabela no cabecalho), **sai** a regra
composta de portabilidade, e **sai** — antes de ter chegado a existir — a cobertura como trafego de
canal. **Entra** uma constante reservada (`site == 0`). **11 crumbs.**

---

## 29. O transporte proprio — e a peca que falta e a mais fina de todas

> *"encontramos o tunel para o trafego de dados binarios para o journal, tanto entre threads quanto
> processos, sem redesignar um stdout / stderr"*
>
> *"ate canais `chan<T>` poderiam se beneficiar dessa arquitetura, ja existe, so precisa de um
> 'acucar'."*

### 29.1 A tensao e real, mas nao onde foi vista — e o conflito esta DENTRO do meu texto

O integrador poe a tensao assim: o fan-in redirecciona o stdout do filho para um tubo, logo os `print`
do filho nao chegam ao stdout real. **A forma estrita esta refutada pelo meu proprio texto**, e cito a
§26.2, que e posterior ao fan-in e manda sobre ele:

> | **texto livre** (prints, diagnosticos de build) | **stdout/stderr** do filho → o handler
> **reencaminha para o stdout/stderr do orquestrador** | por **prefixo** `out|`/`err|` |

Ha um reenvio nomeado. Os `print` chegam a um stdout de verdade — o do orquestrador.

**Mas ha um conflito, e e meu, entre duas seccoes minhas.** O `orchestrate` de §16.4 nao reenvia nada:

```
match teko::journal::append(sink_of(sinks, m.writer), "out", m.bytes) { ... }
```

Ele **apenda o texto livre ao segmento** e nao escreve uma linha no stdout. Isso e exactamente o que
a §26.2 proibiu dez seccoes depois (*"sai a variante `out`/`err` do `Rec`"*), e eu nunca voltei atras
para corrigir o codigo de §16.4. **A §26.2 manda; o bloco de §16.4 fica superado e e corrigido aqui**
(§29.9). O integrador viu um conflito verdadeiro; o que ele nao viu e que os dois lados do conflito
sao meus.

**E fui medir a perna que faltava, a do buffering, porque era a unica que o reenvio nao consertaria.**
Um programa que escreve um byte sem quebra de linha e sai por `_exit` (sem descarga de stdio):

| destino do stdout | o byte chegou? |
|---|---|
| tubo | **nao** (0 bytes) |
| ficheiro | **nao** (0 bytes) |

**Identicos.** O filho de hoje **ja nao e um terminal** — os destinos ja sao ficheiros —, logo o tubo
**nao introduz** nenhuma diferenca de buffering que o ficheiro ja nao tivesse. **Essa perna da tensao
nao existe, e nao a levanto.** Pela mesma disciplina nao levanto a perna do `isatty`: varri `src/` e
ha **zero** ramos sobre `isatty` (o unico acerto e um doc-comment em `src/build/progress.tks:24` a
dizer que a costura esta fora de ambito). **Um alarme que nao consigo produzir nao e um alarme.**

O que a heranca muda e o **contrario**, e e ganho: com os tres fluxos herdados o filho passa a poder
ser um terminal, o que hoje nunca e.

### 29.2 O transporte, escolhido — e o criterio e uma lei que este documento ja tem

**POSIX: `AF_UNIX` `SOCK_STREAM`. Windows: `\\.\pipe\` com `FILE_FLAG_OVERLAPPED`.** Uma so, e o
criterio nao e preferencia:

> **O transporte nao pode criar um artefacto no sistema de ficheiros.** E a §7.2 — *a raiz da corrida
> e a UNICA coisa que a corrida cria* — e e a lei inteira pela qual o F4 existe.

O `AF_UNIX` de Windows **falha esse criterio**: nao tem espaco de nomes abstracto e nao tem
`socketpair`, logo exige um **caminho real** e devolve ao directorio de trabalho o ficheiro que
acabamos de gastar um crumb a remover. O `\\.\pipe\` vive num espaco de nomes do kernel: **nao poe
nada em disco.** O desempate (ser esperavel, §29.8) vem depois e concorda.

Medido nesta caixa, Linux:

| | medido |
|---|---|
| `AF_UNIX` abstracto (NUL inicial): `bind`+`listen`+`connect`+`accept` | **OK**, e `stat` do nome **nao acha nada em disco** |
| `AF_UNIX` por caminho: `bind` | cria inode `S_ISSOCK`, **0 bytes** |
| `unlink` depois do `listen`, e `connect` a seguir | **ENOENT** — o truque de apagar cedo **nao existe** |
| capacidade de `sun_path` | **108 bytes** |

**Correccao a conta dos 2 `.chan`, e nao repito adjectivo que nao medi.** Nao "somem" em todo o lado:

| hospedeiro | o que sobra |
|---|---|
| Linux | **nada** — espaco de nomes abstracto, medido |
| Windows | **nada** — `\\.\pipe\` e espaco de nomes do kernel |
| macOS | **um inode de socket de 0 bytes** — nao ha abstracto la (**conhecimento, nao medido nesta caixa**) |

E o de macOS nasce **dentro da raiz da corrida**, que e a unica coisa que a corrida cria e que morre
inteira. **A guarda de §7.2 fica valida sem uma linha de alteracao.**

**E o transporte e nomeado, nao passado — e e dai que vem a economia toda.** O `VERDICT_CHANNEL_ENV`
ja carrega um **caminho** (`src/process/process.tks:358`) e o filho ja o le
(`verdict_channel_path`, `:366`). O que muda e o que esta na ponta do nome, nao o mecanismo. Logo:

* **`spawn_redirected_fds` nao ganha um quarto encaixe** — a assinatura de hoje fica intacta;
* o `_O_NOINHERIT` / `FD_CLOEXEC` que o F5 poe de proposito (`teko_rt.c:2855-2858`) **fica como esta**;
* nao ha heranca de descritor a desenhar, logo nao ha o que a §29.11 recusa.

### 29.3 Threads e processos: um `chan<Rec>`, dois bracos — e o socket e ADAPTADOR

**Nao e o mesmo transporte, e sao duas encarnacoes do mesmo `chan<Rec>`.** Entre raias do mesmo
processo um socket serializa, entra no kernel, copia duas vezes e volta, para entregar um registo que
ja estava no mesmo espaco de enderecamento. O `tk_chan` em memoria basta e continua a bastar.

**O que faz dos dois uma peca so e o QUADRO, e ele ja esta desenhado.** O registo e embrulhado no
**fundo de emissao** (§24.4), logo ja e o quadro prefixado por comprimento de §26.4 **antes** de
chegar a qualquer dos bracos:

```
produtor-raia    ──────────────── quadro ─────────────────▶ tk_chan (anel) ──▶ orquestrador
produtor-processo ── quadro ─▶ socket ─▶ leitor ─▶ quadro ─▶ tk_chan (anel) ──▶ orquestrador
```

**Um quadro, um leitor de quadros, um anel.** O braco de socket nao e um segundo canal: e um
**adaptador** que enche o mesmo anel. Zero serializacao extra na raia, porque o quadro ja existia.

**O custo da diferenca, medido e nao presumido** (nesta caixa, 6500 registos de 80 B, um produtor):

| forma | total | por registo |
|---|---:|---:|
| `socketpair` `AF_UNIX` | **5,966 ms** | **0,92 us** |
| anel com `pthread_mutex` + `cond` | **2,152 ms** | **0,33 us** |
| **razao** | **2,77x** | **delta de 3,8 ms na corrida inteira** |

**3,8 ms.** Contra os 36 % do gate que o `fsync` de §5.1 custa, e ruido — mas agora e um numero e nao
uma suposicao. **O que NAO medi:** N produtores em disputa sobre o mesmo socket. Isto e 1:1.

### 29.4 O acucar tem um limite, e e o enquadramento — o meu prefixo sobrevive

Medido nesta caixa, duas escritas de 3 e 5 bytes:

| forma | leituras |
|---|---|
| `SOCK_SEQPACKET` | **3 e 5** — fronteira preservada |
| `SOCK_DGRAM` | **3 e 5** — fronteira preservada |
| `SOCK_STREAM` | **8** — **colou** |

O subconjunto portatil e `STREAM` (macOS sem `SEQPACKET` em `AF_UNIX`, `AF_UNIX` de Windows so
`STREAM` — **as duas de conhecimento, nao medidas nesta caixa, e nao consegui desmentir nenhuma**).
Logo **o prefixo de comprimento de §26.4 continua a fazer falta**, e o integrador tem razao no que
isso significa: nao se perde trabalho, muda o **estatuto** — deixa de ser invencao minha e passa a ser
a camada fina que falta ao `STREAM`.

**E nao ramifico em `SEQPACKET` mesmo onde ele existe.** Dois enquadramentos sao dois leitores, e o
segundo nunca corre no CI que temos. **Um enquadramento, em toda a parte** — a mesma razao pela qual
a §28.6 recusou a regra composta de portabilidade.

### 29.5 O que o SO da de graca, e o que ele nao da

| requisito que desenhei a mao | o SO da | medido nesta caixa |
|---|---|---|
| **limitado** (`bounded`) | o buffer do socket **e** o tecto | `SO_SNDBUF` = **212992** por omissao |
| **contrapressao** | escrita bloqueia ou devolve `EAGAIN` | escrita nao-bloqueante parou aos **180224** bytes com `EAGAIN` |
| **espera com prazo exacto** | `poll` com deadline | `poll(50 ms)` num socket ocioso voltou aos **50,13 ms** |
| **N escritores, um leitor** | varios descritores para o mesmo par | 4 `dup` da ponta de escrita: o leitor viu **os 8 bytes dos quatro** |
| **fecho POR PRODUTOR** | **contagem de referencias do descritor** | o `read` so devolveu **0 depois de os QUATRO `dup` fecharem** |
| **fronteira de mensagem** | **nao** no subconjunto portatil | §29.4 |
| **ordem entre produtores** | nao — e §16.5 nunca a pediu | — |

A linha do **fecho por produtor** e a mais valiosa: era o requisito mais afiado do F4 (*"e preciso que
o canal conte os produtores e so diga `closed` quando o ultimo sair"*, §16.5) e **o kernel ja o faz**.

**E ha uma armadilha medida no numero do tecto:** o `SO_SNDBUF` **relatado** e 212992 mas a escrita
parou aos **180224** — o util e ~15 % menor do que o anunciado. Um pedido de 8 MiB **foi concedido**
(`getsockopt` releu 8388608), e note-se que `/proc/sys/net/core/wmem_max` e 4194304, ou seja o
`AF_UNIX` **nao foi limitado por ele** nesta caixa.

### 29.6 As razoes de recusa — a enumeracao fica INTEIRA, e a fronteira do integrador esta metade certa

O dono manda: *"os enumerados podem permanecer, seriam flags da mensagem transportada"*. **Cumpro-o
inteiro, e os dois argumentos do integrador sao os certos** — a grafia de `EAGAIN`/`EPIPE` varia entre
hospedeiros e uma flag nossa e identica em toda a parte (a mesma logica que tornou o `.tkj` portavel,
§28.6); e a guarda *"toda a razao tem de ter um teste que a produza"* so sobrevive se quem produz a
razao for **codigo nosso**, porque uma razao produzida pelo kernel nem sempre se consegue provocar.

**Mas a tabela do integrador poe `Closed` e `NoReader` como flags transportadas, e ai ela quebra — por
DIRECCAO.** Sao dois factos diferentes com o mesmo nome:

| facto | sentido em que teria de viajar | veredicto |
|---|---|---|
| **o PRODUTOR acabou** ("nao ha mais registos") | produtor → consumidor | **flag transportada**, e e a **unica** |
| **o CONSUMIDOR sumiu** (`Closed`/`NoReader` como recusa de `push`) | **consumidor → produtor** | **nao pode ser flag** neste canal |

Uma flag no sentido produtor→consumidor **nunca** pode dizer ao produtor que o leitor morreu. Poe-la
la exigia **um canal de volta** — e a pergunta do integrador (*"nao inventes um canal de volta sem o
nomear como tal"*) e a pergunta certa. **A resposta e que nao invento nenhum, e nao preciso:**

> **A razao que teria de viajar para tras e entregue pela FALHA DA PROPRIA ESCRITA.** Medido nesta
> caixa: escrever num socket cujo leitor fechou devolve **`EPIPE`**. Isso nao e uma mensagem e nao e
> um canal — e o `push` a falhar, no sitio onde o `push` ja devolve.

Logo a tabela final, e cada razao ganha um lugar declarado em vez de um balde:

| razao | onde vive | porque, e como se produz |
|---|---|---|
| **`Fin`** (o produtor acabou) | **FLAG TRANSPORTADA** — a unica | sentido certo. Redundante com a contagem de descritores (§29.5) **de proposito**: a flag e a grafia **portatil** do que o kernel ja sabe |
| **`Full`** | **retorno LOCAL** | a razao do integrador e a melhor linha da tabela dele: *se o canal esta cheio, nao ha espaco para a mensagem que diria "estou cheio"*. Produz-se enchendo (medido: `EAGAIN` aos 180224 B) |
| **`Closed`** | **retorno LOCAL** | o leitor registado saiu: `EPIPE` na escrita (medido). Produz-se fechando o leitor |
| **`NoReader`** | **retorno LOCAL, e muda de FASE** | ninguem chegou a escutar: `ECONNREFUSED` **no `connect`**, nao no `push`. Distinguivel de `Closed` por construcao — outra chamada, outro momento. Produz-se nao abrindo o orquestrador |
| **`NotAProducer`** | **retorno LOCAL** | id que o registo do F3 nao conhece, verificado **antes** de qualquer envio. Produz-se com um id obsoleto |
| ~~`Oversize`~~ | — | **ja tinha morrido em §24.5**; nao ressuscita |

**Cinco razoes, cinco testes, nenhuma perdida.** A forma do dono aplica-se onde a direccao a permite; e
onde nao permite, a razao ja era local por natureza — que era a intuicao do integrador, so que o corte
nao e `Full`/`NotAProducer` contra as outras duas: **e uma contra quatro.**

### 29.7 O `pop` de tres saidas mapeia limpo — e o SO oferece a correccao de §25.4 de graca

| saida de `pop` | o socket | medido |
|---|---|---|
| **`Rec`** | leitura devolveu bytes | — |
| **`null`** ("nada agora") | leitura nao-bloqueante devolveu **`EAGAIN`** | **sim** |
| **fechado** (que e `is_open`, nao saida de `pop`) | leitura devolveu **0** | **sim** |
| **`error`** | qualquer outro `errno` | — |

**Confirma, e ha um bonus que e a evidencia mais forte a favor do acucar.** A §25.4 nomeia uma
armadilha de perda de dados: se `is_open` fosse so *"ha produtores vivos"*, o ultimo produtor a sair
punha a condicao a falso **com registos ainda no anel**. **O socket ja resolve isso**: medido, o `read`
so devolveu `0` **depois** de os quatro escritores fecharem **e** o buffer estar drenado. A definicao
correcta de `is_open` que eu tive de construir a mao **e o comportamento por omissao do transporte.**

### 29.8 A assimetria de 2 ms fecha — de um lado, e digo de qual

**Fecha no caminho do journal.** `\\.\pipe\` com `FILE_FLAG_OVERLAPPED` e um objecto **esperavel**,
com prazo exacto; do lado POSIX o prazo ja e exacto (medido: `poll(50 ms)` → 50,13 ms). O braco que
sondeia **desaparece do transporte**.

**Nao fecha no caminho da captura.** O tubo anonimo continua anonimo, e o ciclo de
`Sleep(TK_RT_PIPE_POLL_INTERVAL_MS)` de `teko_rt.c:2907-2915` sobrevive exactamente enquanto a captura
de texto livre sobreviver (§29.9). **E nao promovo nada:** o comentario na arvore diz *"UNVERIFIED on a
real Windows host"* e continua a dizer, porque **este repositorio nao tem executor Windows** e o meu
Windows continua por medir.

### 29.9 O F5 NAO fica obsoleto — muda de papel, e digo a condicao exacta em que morreria

Sem rodeios, porque foi pedido tres vezes: **o `pipe` do F5 nao fica sem utilizador, e tem dois.**

**Medido, e sao consumidores reais:**

| consumidor da saida livre capturada | medido |
|---|---|
| linhas `.tkr` que afirmam sobre `stdout`/`stderr` | **192** |
| quem le os ficheiros de volta | `src/build/regression.tks:204-205` (`spec.prefix ~ ".out"` / `".err"`) |

O que muda e a **obrigacao**, e e uma inversao de omissao:

| | antes (§15/§16) | com transporte proprio |
|---|---|---|
| os tres fluxos do filho | **redireccionados**, sempre | **herdados e intactos**, por omissao |
| o journal | pelo stdout redireccionado | **pelo transporte proprio** |
| a saida livre | drenada e reenviada, sempre | vai **directa** ao terminal do orquestrador |
| capturar a saida livre | obrigatorio | **opcional, por filho** — e e `spawn_redirected_fds` com uma ponta de `pipe()`, que **ja aterrou hoje** |

**Nenhuma linha do F5 se apaga.** O que se apaga e um handler por fluxo por filho no caminho do
journal, e o bloco `orchestrate` de §16.4 (§29.1), que deixa de apendar texto livre ao segmento porque
o texto livre deixa de passar por ele.

**E a condicao em que o F5 ficaria mesmo obsoleto, dita para o dono poder decidir depois:** no dia em
que a atribuicao de texto livre a um filho deixar de ser exigida, o tubo anonimo perde o ultimo
utilizador e sai inteiro. **Hoje ela e exigida 192 vezes**, logo hoje nao sai.

### 29.10 O `tk_chan` (F4) ENCOLHE, nao desaparece — e a invariante muda de dono

Nao desaparece porque o braco de raias continua a precisar de um anel em memoria (§29.3): o buffer do
socket so e o anel na perna **entre processos**. O que sai do F4:

| do F4 sai | para onde |
|---|---|
| a contagem de produtores | contagem de referencias do descritor (medido, §29.5) |
| o limite e a contrapressao (perna de processo) | `SO_SNDBUF` + `EAGAIN` (medido) |
| a espera com prazo | `poll` / objecto esperavel (medido) |
| a definicao subtil de `is_open` (§25.4) | comportamento por omissao do `read` (medido, §29.7) |
| o quadro | **nao sai** — §29.4 |

**E a invariante de arranque `capacidade_do_anel >= REC_MAX` (§26.3) muda de forma, nao de existencia.**
Na perna de socket ela passa a ser sobre o `SO_SNDBUF`, que e **configuravel e com tecto do sistema** —
e a medicao de §29.5 diz exactamente como a afirmar sem mentir:

> **Pedir, reler, e afirmar sobre o que foi CONCEDIDO, nunca sobre o que foi pedido** — e sobre o
> **util**, nao sobre o **relatado**: o relatado foi 212992 e o util 180224, ~15 % abaixo.

```teko
/**
 * ensure_capacity — garantir que o transporte aceita um registo do tamanho maximo, e falhar no
 * ARRANQUE quando nao aceita.
 *
 * PEDIR NAO E TER, e essa e a diferenca inteira entre esta funcao e um `setsockopt` solto. O
 * hospedeiro pode conceder menos do que se pediu, e o que ele relata depois ainda por cima
 * sobrestima o utilizavel — medido: `SO_SNDBUF` relatado 212992, escrita a parar em 180224. Por isso
 * esta funcao releva o concedido e afirma sobre a fraccao util, uma vez, na abertura.
 *
 * @param t        o transporte acabado de abrir
 * @param rec_max  o maior registo que o protocolo admite (§26.3)
 * @return         nada quando cabe
 * @throws         quando o concedido nao acomoda `rec_max`, com o pedido e o concedido na mensagem
 * @since 0.3.1
 */
pub fn ensure_capacity(t: Transport, rec_max: u64): null | error
```

### 29.11 O `SCM_RIGHTS` nao faz falta a este desenho — e sai do caminho

**Nao faz falta, e a razao e estrutural e nao um contorno:** o transporte e **nomeado, nao passado**
(§29.2). O filho **liga-se** a um nome que le do ambiente; ninguem entrega um descritor a ninguem.
Sem entrega de descritor nao ha `SCM_RIGHTS`, nao ha `DuplicateHandle`, nao ha quarto encaixe no
`spawn`, e nao ha o `CLOEXEC` a desligar.

A unica coisa que passar descritores compraria e entregar um descritor vivo a um processo **que ja
corre e que nao lancamos**. **Nao fazemos isso em lado nenhum**: todo o filho nosso e lancado por nos
e recebe o ambiente. **Capacidade para outro dia, nao lacuna deste desenho** — e fica registada aqui
para nao voltar a ser um pedido perdido, que e o que aconteceu ao pedido original de pipes e unix
socks (varrido: **zero** ocorrencias em `src/` e `docs/` antes desta seccao).

### 29.12 O custo — nenhum crumb novo, e o saldo e negativo

| | efeito |
|---|---|
| **F4** | **encolhe** (§29.10) — quatro pecas passam ao SO |
| **F5** | **muda de papel**, nao encolhe (§29.9) |
| **F1, F2, F3, F6** | intactos |
| **C1** | absorve o transporte: `tk_journal_open` passa a abrir um socket/pipe nomeado em vez de um ficheiro. E o mesmo sumidouro, com outra ponta |
| **C3** | **encolhe outra vez**: o `.chan` junta-se ao `.out`/`.err` que ja tinham saido |
| **§16.4** | o bloco `orchestrate` e **corrigido** (§29.1): deixa de apendar texto livre |
| **total** | **11 crumbs** |

**Ponto de ritual acrescentado: depois do C1** — o transporte muda o que **todo** o filho escreve, e
uma regressao ai nao se ve num teste unitario.

**Regressoes a acrescentar** (todas com saida nativa, todas falsificaveis):

| fixture | entrada | esperado |
|---|---|---|
| `tr_inherited_streams_reach_the_terminal` | filho que so faz `print`, sem captura | **0**, e o byte aparece no stdout do pai |
| `tr_journal_travels_with_streams_inherited` | filho que emite 3 registos e imprime | **0**, 3 quadros no `.tkj`, e o texto **fora** dele |
| `tr_run_root_is_still_the_only_thing_created` | uma corrida inteira | **0**, e a §7.2 conta **zero** entradas novas fora da raiz |
| `tr_frame_survives_stream_coalescing` | dois registos escritos em duas escritas | **0** — o prefixo separa o que o `STREAM` colou |
| `tr_push_full_is_local` | encher o transporte | **0**, razao `Full`, **local** |
| `tr_push_closed_is_local` | fechar o leitor e escrever | **0**, razao `Closed`, **local** |
| `tr_open_with_no_reader_is_NoReader` | ligar sem orquestrador | **0**, razao `NoReader`, **no `connect`** |
| `tr_fin_flag_travels` | produtor a fechar | **0** — a flag chega **e** a contagem de descritores concorda |
| `tr_capacity_asserted_on_granted_not_requested` | pedir mais do que o hospedeiro concede | **1**, com pedido e concedido na mensagem |
| `tr_free_text_capture_still_attributes` | filho capturado por `pipe` | **0** — as 192 afirmacoes `.tkr` continuam a poder ser feitas |

### 29.13 O que isto NAO resolve

1. **O transporte nao e durabilidade.** Um socket nao sobrevive ao processo. A §5 fica **inteira**: o
   `.tkj` em disco continua a ser o que sobrevive ao OOM, e o socket e so como o registo la chega.
2. **Windows continua por medir por mim**, e a §29.8 recusa-se a promover o que a arvore ja marca como
   nao verificado.
3. **macOS sem `SEQPACKET` e `AF_UNIX` de Windows so `STREAM`** sao **conhecimento, nao medicao desta
   caixa** — e nao os desmenti. O desenho nao depende de nenhum dos dois estar certo, porque escolhe
   o subconjunto `STREAM` de qualquer maneira (§29.4).
4. **N produtores em disputa sobre um socket nao esta medido** (§29.3 e 1:1).
5. **A atribuicao de texto livre entre RAIAS continua perdida**, exactamente como a §26.2 ja o
   declarara. O transporte nao toca nisso: N raias a imprimir para um stdout partilhado entrelacam-se,
   e o que precisa de atribuicao viaja pelo canal com o escritor dentro.
6. **A ordem entre o texto livre e os registos deixa de ser observavel numa so leitura.** Sao dois
   destinos de verdade, agora sem um ponto onde se cruzem — que e o preco de nao redesignar o
   stdout/stderr, e o dono pediu-o por nome.
