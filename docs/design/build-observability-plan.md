# Build-Observability — Design Plan (silent inter-phase gaps)

Branch base `remodel/constants`, HEAD `a5fc3f8`. Escopo: fechar os *gaps silenciosos*
das TRANSIÇÕES do build (front-end → testes → backend), adicionar verbosidade à CLI,
avaliar cache `.tkc`, heartbeat e paridade C/native. Observabilidade é **só stderr**;
não altera bytes (.c/.o/binário) nem o FIXPOINT.

> Nota de método: este documento é PLANO. Todo snippet já vem em estilo W15
> (doc-comments Javadoc, funções pequenas). O implementador copia verbatim no fork.

---

## A. PROVAS (função + linhas exatas + por que é silencioso)

Fonte de verdade lida: `src/build/progress.tks`, `src/build/project.tks`,
`src/build/assemble.tks`, `main.tks` (o dispatch da CLI é self-hosted, está na RAIZ
do projeto, não em `src/`).

### Raízes (confirmadas)

- **R1 — em não-TTY, `phase_begin` NÃO emite START.**
  `src/build/progress.tks:192-195`:
  ```
  pub fn phase_begin(label: str, initial: str, visible: bool) -> Phase {
      if visible { teko::io::eprint(mid_line(label, initial)) }
      Phase { label = label; start = now_ns(); visible = visible }
  }
  ```
  `visible = report && tty`. Em CI (`output_is_tty` retorna `false` por `CI`/`TERM`,
  `progress.tks:28-35`), `visible=false` → `phase_begin` imprime NADA. A fase só aparece
  no SETTLE (`phase_end_ok`, `progress.tks:218-221`), impresso DEPOIS que a fase termina.
  Logo toda fase é invisível ENQUANTO roda. **CONFIRMADA.**

- **R2 — trechos inteiros sem fase (ou com fase suprimida).**
  Gap 1 não tem nenhum `phase_begin`; Gap 2 roda o front-end com `report=false`.
  **CONFIRMADA** (detalhe por gap abaixo).

### Gap 1 — front → test (CONFIRMADO; minha localização estava correta)

Função `run_native_gate` — `src/build/project.tks:1524-1543`. NÃO existe nenhum
`phase_begin` na função inteira. Passos silenciosos, em ordem:
- `codegen::tk_emit_c_test(prog, true)` — `project.tks:1531` (codegen do corpus inteiro,
  monolítico — `tk_emit_c_mode`, `codegen/codegen.tks:8532`, vários loops sobre
  `prog.items`, sem callback).
- `teko::io::write_file(cfile, csrc)` — `project.tks:1533`.
- `run_cc(cfile, binp, m, prog, 0)` — `project.tks:1534` → `teko::process::run` de um `cc`
  compilando um `.c` multi-MB (o TU de teste do corpus inteiro).

Por que é silencioso: zero fases; a primeira saída só surge quando o BINÁRIO de teste roda
e imprime. Chamadores: `run_gate_native` (`project.tks:1582`) e `test_project`
(`project.tks:1859`) — nenhum passa `tty`.

### Gap 2 — test → backend (CONFIRMADO; minha localização estava correta)

Função `release_build_of` — `src/build/project.tks:1557-1560`:
```
let rel = match frontend_body(false, true, tty) { Frontend as f => f; error as e => return fail(dir, e.message) }
```
`frontend_body(include_tests=false, quiet=true, tty)` — `project.tks:318`. Com `quiet=true`:
- banner suprimido — `if !quiet` em `project.tks:345`;
- `assemble_sel(files, include_tests, !quiet=false, tty)` — `project.tks:352`; dentro,
  `visible = report && tty = false` (`assemble.tks:148`) e os settles estão sob
  `if report` (`assemble.tks:191-194`) → lexer/parser NÃO emitem nada;
- `checked_program_of(selected, dep_prog, !quiet=false, tty)` — `project.tks:363`; dentro,
  `visible=false` (`project.tks:271`) e todos os settles sob `if report`
  (`project.tks:283, 294`) → checker/monomorph NÃO emitem nada.

Por que é silencioso: o front-end INTEIRO (lex + parse + load-deps + checker + monomorph +
inline_consts) roda re-executado em modo quiet. Nenhuma linha até o backend começar a
assentar suas fases.

### Gap 3 — front → backend, testes desligados (CONFIRMADO; é a manifestação da R1)

Função `backend` — `src/build/project.tks:842-868`. Caminho sem testes:
`compile_project_g` (`project.tks:1646`) → `build_no_tests_present` (`project.tks:1649,1617`)
→ `codegen_and_report` → `backend`. Ali:
- `phase_begin("codegen", …, tty)` — `project.tks:842`; em não-TTY não imprime START (R1);
  `tk_emit_c` roda invisível; settle só em `project.tks:850`.
- idem `emit C` (`project.tks:857/862`) e `cc` (`project.tks:863/868`).

O gap percebido "entre front→backend" = o intervalo entre o settle de `monomorph`
(última linha do front-end, `project.tks:294`) e o settle de `codegen`
(`project.tks:850`), durante o qual `tk_emit_c` roda sem START. É R1 pura.

### Estimativa de qual fase domina o tempo (do código)

- **`cc` externo domina** (Gap 1 e Gap 3): `run_cc`/`link_object` chamam
  `teko::process::run` de um `cc` compilando o `.c` gerado do corpus inteiro (MBs). O
  diagnóstico já citado em `build_cc_argv` (`project.tks:619-622`) mede o self-host em
  ~129–168 s, boa parte no `cc`. É o maior consumidor isolado e é EXTERNO/bloqueante.
- **checker + monomorph** (Gap 2): re-front-end completo; `type_program_with_deps_pre_mono`
  + `monomorphize` sobre todos os itens. Dezenas de segundos no self-host.
- **codegen (`tk_emit_c`)** (Gap 1/Gap 3): significativo, monolítico, mas < `cc`.

Prioridade de observabilidade daí: **um START-line antes do `cc` é o conserto de maior
valor** (a fase mais longa e a que NÃO dá para heartbeat — ver §B4).

---

## B. DESIGN (decisões para os 5 pontos)

### Crítica ao rascunho `report-3state`

Incorporar: o enum `Report = { Tty; Plain; Quiet }` e `report_mode`; `phase_begin` emitindo
START em `Plain`; troca de `phase.visible: bool` por `phase.mode: Report`; instrumentação
dos Gaps 1 e 2. O rascunho está CORRETO e é o núcleo pure-stderr.

Descartar/ajustar: (a) o rascunho resolve `report_mode(quiet, tty)` com 2 eixos; o design
final tem 3 eixos (verbosity, quiet-pass, tty) — estender o resolvedor, não substituir. (b)
o bracket `rebuild` do Gap 2 fica, mas é PALIATIVO; a raiz do Gap 2 NÃO é resolvida por
`.tkc` (ver prova §B2) — fica observabilidade + heartbeat. (c) faltam CLI-flag, heartbeat e
paridade native — crumbs D–H.

### B1. VERBOSIDADE na CLI

**Conflito de nome (PROVADO):** `-v` já é `--version` — `main.tks:40`. NÃO usar `-v` para
verbose.

**Escala (Verbosity):** `quiet(0) < normal(1) < verbose(2) < debug(3)`.
- `quiet`  — só erros e a linha final `built …`; nenhuma linha de fase.
- `normal` — comportamento atual: fases + settle (o default).
- `verbose`— fases + heartbeats das fases longas (§B4) + tamanhos/contagens extra.
- `debug`  — verbose + sub-passos (lower/isel/regalloc/encode nomeados no native; dump de
  deps carregadas; tempos por sub-passo).

**Flags:** `--verbosity=<quiet|normal|verbose|debug>` (forma canônica), com atalhos
`--quiet`/`-q` (=quiet) e `--verbose`/`-V` (=verbose; `-V` maiúsculo por causa do `-v`
tomado). A última flag na linha vence (mesmo padrão de `opt_level_of`, `project.tks:581`).

**Dois eixos ORTOGONAIS:** `Verbosity` (QUANTO detalhe) × render `Tty|Plain` (COMO desenha).
`--no-tty`/`CI`/`TERM` decidem SÓ `Tty` vs `Plain` (via `output_is_tty`); NUNCA mexem na
`Verbosity`. Precedência final:

1. `Verbosity` = `verbosity_of(args)` (explícito > default `normal`).
2. render = `output_is_tty(no_tty)` → `Tty`|`Plain` (no_tty > `CI` > `TERM`, já existente).
3. `Report` de cada fase = `report_mode_v(verbosity, quiet_pass, tty)`:
   - `quiet_pass` (o retry já reportado) → `Quiet` (vence tudo);
   - `verbosity==quiet` → `Quiet`;
   - senão `tty` ? `Tty` : `Plain`.
   (`verbose`/`debug` mantêm `Tty`/`Plain` e LIGAM heartbeats — bit separado, ver ReportCfg.)

**Onde parsear:** novo helper em `project.tks` (mesmo lugar de `no_tty_of`,
`project.tks:1724`), e consumo em `main.tks` (linhas 71-88 e 102-108). Para não explodir
assinaturas, empacotar os eixos num struct e passá-lo no lugar do atual `tty: bool`:

```
/**
 * The build's observability configuration — the two orthogonal axes every phase
 * reads: HOW MUCH to say (`verbosity`) and HOW to render (`tty` = in-place `\r`
 * updates vs plain START+settle lines). Replaces the bare `tty: bool` threaded
 * through the build so a new axis (heartbeats, debug sub-steps) is one field, not a
 * new positional parameter on a dozen functions (SOLID/OCP).
 *
 * @field verbosity  how much per-phase detail to emit (`quiet`..`debug`)
 * @field tty        render in-place `\r` updates (a real terminal) vs plain lines
 */
pub type ReportCfg = struct {
    verbosity: Verbosity
    tty: bool
}

/**
 * The build output verbosity axis (orthogonal to the `Tty|Plain` render axis).
 *
 * @see report_mode_v
 */
pub type Verbosity = enum { Quiet; Normal; Verbose; Debug }

/**
 * Resolve a phase's render `Report` from the two axes plus the pass's own
 * already-reported flag. `quiet_pass` (a retry whose lines were already printed)
 * always wins as `Quiet`; a `Quiet` verbosity also silences phase chatter; else a
 * terminal renders `Tty` and a non-interactive destination renders `Plain`.
 *
 * @param verbosity   the CLI verbosity level
 * @param quiet_pass  this pass already reported (suppress everything)
 * @param tty         a real terminal (in-place `\r` updates)
 * @return            the resolved `Report` mode for `phase_begin`
 */
pub fn report_mode_v(verbosity: Verbosity, quiet_pass: bool, tty: bool) -> Report {
    if quiet_pass { return Report::Quiet }
    if verbosity == Verbosity::Quiet { return Report::Quiet }
    if tty { Report::Tty } else { Report::Plain }
}

/**
 * Whether phase HEARTBEATS (periodic progress on a long monolithic phase — the
 * checker re-run, codegen) should be emitted: only at `Verbose`/`Debug`, and never
 * on a `Tty` (the `\r` counter already animates there) nor on an already-reported
 * `quiet_pass`.
 *
 * @param cfg         the build's observability configuration
 * @param quiet_pass  this pass already reported (no heartbeat)
 * @return            true iff a Plain heartbeat line should be emitted
 */
pub fn heartbeat_on(cfg: ReportCfg, quiet_pass: bool) -> bool {
    if quiet_pass { return false }
    if cfg.tty { return false }
    cfg.verbosity == Verbosity::Verbose || cfg.verbosity == Verbosity::Debug
}
```

```
/**
 * verbosity_of — resolve the CLI verbosity level from the build flags. The last
 * matching flag wins (mirrors `opt_level_of`). Recognised spellings:
 * `--verbosity=<quiet|normal|verbose|debug>`, `--quiet`/`-q` (Quiet),
 * `--verbose`/`-V` (Verbose). Absent → `Normal` (today's behaviour). `-v` is NOT
 * accepted here — it is `--version` (main.tks).
 *
 * @param args  the full CLI argument vector
 * @return      the resolved `Verbosity` (Normal by default)
 */
fn verbosity_of(args: []str) -> Verbosity {
    mut i: u64 = 0
    mut v = Verbosity::Normal
    loop {
        if i >= args.len { break }
        let a = args[i]
        if a == "--quiet" || a == "-q" { v = Verbosity::Quiet }
        else if a == "--verbose" || a == "-V" { v = Verbosity::Verbose }
        else if a == "--verbosity=quiet" { v = Verbosity::Quiet }
        else if a == "--verbosity=normal" { v = Verbosity::Normal }
        else if a == "--verbosity=verbose" { v = Verbosity::Verbose }
        else if a == "--verbosity=debug" { v = Verbosity::Debug }
        i = i + 1
    }
    v
}
```

`project_arg_of` (`project.tks:1747`) precisa pular os novos tokens (`--quiet`,`-q`,
`--verbose`,`-V`,`--verbosity=*`), senão viram "projeto". `help.tks` ganha as linhas de
ajuda (golden do help muda — ver §D).

### B2. CACHE `.tkc` — PROVA DE (NÃO-)EQUIVALÊNCIA e recomendação

**Codec:** reusar `emit::serialize_program` (`emit/tkb_frame.tks:424`) /
`emit::deserialize_program` (`emit/tkb_read.tks:993`) VERBATIM. Os bytes são o frame
`TKB\0` + `u32=3` + `u64 FNV-1a` + corpo (round-trip já testado, `emit/tkb_test.tkt`). O
`.tkc` é um ARQUIVO/ROLE distinto (artefato de pipeline), NÃO um pacote — a semântica do
`.tkb` (árvore exportada dentro de um `.tkl`) fica intacta porque a separação é de PAPEL e
de EXTENSÃO, não de formato. Scaffolding:

```
/**
 * write_tkc — serialize a checked program to a `<out>/<stem>.tkc` PIPELINE cache
 * file. Distinct ROLE from a `.tkb` (which is a package's exported tree inside a
 * `.tkl`): a `.tkc` is a build-internal, OPTIONAL, reproducible artifact — it never
 * enters a `.tkl`, never enters the FIXPOINT, and never changes the emitted binary.
 * The byte codec is shared with `.tkb` on purpose (`serialize_program`), so no new
 * serializer is introduced. Best-effort: a write error is non-fatal.
 *
 * @param path  the `.tkc` output path
 * @param prog  the checked, monomorphized program to cache
 * @return      null on success, else the write error
 */
fn write_tkc(path: str, prog: checker::TProgram) -> void | error {
    teko::io::write_file_bytes(path, emit::serialize_program(prog))
}

/**
 * read_tkc — load a checked program from a `.tkc` pipeline cache. Inverse of
 * `write_tkc`; delegates to `emit::deserialize_program`. Returns an error (not a
 * panic) on a missing/corrupt/foreign cache so the caller can fall back to a full
 * re-front-end — a `.tkc` is an OPTIONAL accelerator, never a source of truth.
 *
 * @param path  the `.tkc` input path
 * @return      the cached program, or an error to fall back on
 */
fn read_tkc(path: str) -> checker::TProgram | error {
    let raw = match teko::io::read_file_bytes(path) { []byte as b => b; error as e => return e }
    emit::deserialize_program(raw)
}
```

**A questão delicada — equivalência para o Gap 2 (PROVA DE REFUTAÇÃO):**

O binário release hoje vem de `strip_tests(rel.prog)` onde
`rel = frontend_body(false, true, tty)` — ou seja, `monomorphize` roda sobre os itens
SEM testes. Para o `.tkc` atacar o Gap 2 pela raiz, o cache teria que vir do pass 1 do
gate, que é `frontend_body(true, …)` — COM testes. A pergunta é se

  `strip_tests( monomorphize(itens_com_testes) )` == `monomorphize(itens_sem_testes)`

**Isto é FALSO em geral. Contraexemplo (monomorfização dirigida por teste):**
seja `foo<T>` uma função genérica não-test e um `#test` que chama `foo<TSóDeTeste>`.
- `monomorphize(com testes)` cria a instância `foo$TSóDeTeste` (item NÃO-test, pois é
  instância de uma fn não-test).
- `strip_tests` remove só itens com `is_test` (`project.tks:1437`), NÃO remove a instância.
- `monomorphize(sem testes)` NUNCA cria `foo$TSóDeTeste`.
→ conjuntos de itens diferentes → C emitido diferente → **FIXPOINT QUEBRA**.

Segundo contraexemplo (procedência): um `.tkt` pode conter uma função auxiliar NÃO-`#test`.
`strip_tests` a preserva no caminho com-testes, mas ela nem existe no caminho sem-testes
(o arquivo `.tkt` é pulado inteiro, `assemble.tks:157-160`). Mesma divergência.

Não existe pass ANTERIOR à divergência que produza barato o programa sem-testes: a única
forma de obtê-lo é rodar `monomorphize(sem testes)` — que É exatamente o trabalho do Gap 2.
Um cache cross-invocação (keyed por hash de fonte) não ajuda o self-test de CI (checkout
limpo → cache sempre frio) e adiciona risco de invalidação imperfeita → bytes errados →
fixpoint. 

**RECOMENDAÇÃO (law-first):** a Restrição INVIOLÁVEL "carregar o cache deve produzir bytes
idênticos / não quebrar o fixpoint" tem precedência sobre o requisito 2/3 (que é um MEIO; o
FIM é fechar a mudez do Gap 2). Portanto **NÃO cachear `.tkc` no caminho default do Gap 2.**
O owner previu essa saída explicitamente ("se não for provável com segurança, recomende NÃO
usar cache"). O Gap 2 é fechado por observabilidade (bracket `rebuild`) + heartbeat (§B4).

O `.tkc` é entregue como SCAFFOLDING (write_tkc/read_tkc + testes de round-trip), FORA do
fixpoint: uso seguro = emitir `.tkc` DEPOIS de `strip_tests(rel.prog)` como artefato
inspecionável / semente de um futuro incremental atrás de flag OFF-by-default. Sem fiação no
`release_build_of` default. (Reportado ao owner: a eliminação real do Gap 2 pela raiz é uma
reestruturação do gate — produzir o programa sem-testes UMA vez e derivar a visão de teste
por cima — que toca o checker e é maior que "observabilidade"; fora do escopo desta issue.)

### B3. Gap 2 concreto — ELIMINAR o re-front-end (decisão do owner)

O owner REJEITOU o bracket paliativo ("não há sentido em recarregar o front"). A análise
completa das abordagens (a)/(b)/(c) está na seção **"Gap 2 — eliminação do re-front-end"**
abaixo. Resumo: **(a) reusar o AST parseado é PROVÁVEL e seguro (ship); (b) prune pós-mono é
REFUTADO (quebra o fixpoint por reordenação de instâncias compartilhadas); (c) reusar o check
é o maior ganho mas depende de uma invariância do checker validada só pelo gate de fixpoint do
CI.** O bracket sai; entra o reuso de AST (a) + observabilidade honesta (o check+mono de
release vira trabalho REPORTADO de verdade, não um "retry quiet").

### B4. HEARTBEAT — viabilidade honesta

- **Checker (Gap 2 re-front-end):** VIÁVEL. O checker já expõe `ProgressFn`/`on_item`
  (`checker_tick_fn`, `project.tks:247`; `type_program_with_deps_pre_mono` recebe `on_item`,
  `project.tks:276`). Hoje no re-front-end quiet o tick é suprimido. Solução: um
  `heartbeat ProgressFn` que, em `Plain`+`verbose/debug`, emite uma linha a cada N itens (ou
  a cada ~1 s por `now_ns`), independente do `report` do pass. Passar esse callback ao
  `checked_program_of` mesmo quando `report=false`. Barato, stderr-only.
- **Codegen (`tk_emit_c`):** VIÁVEL com custo moderado. Hoje `tk_emit_c(prog)` →
  `tk_emit_c_mode(prog0, mode)` (`codegen/codegen.tks:8516,8532`) NÃO tem callback; itera
  `prog.items` em vários loops (o pesado é a emissão de corpos). Adicionar um `ProgressFn`
  OPCIONAL ao `tk_emit_c_mode` e tickar no loop de corpos. **Byte-identidade preservada** —
  o callback só escreve stderr, não toca `b: []byte`. Compat: manter `tk_emit_c(prog)`
  chamando com no-op; nova entrada reportante:
  ```
  /**
   * tk_emit_c_report — the reporting sibling of `tk_emit_c`: emit the Program-mode C
   * translation unit while ticking `on_item(done, total)` per top-level item, so a
   * non-TTY build can heartbeat the long monolithic codegen phase. The EMITTED BYTES
   * are byte-identical to `tk_emit_c(prog)` — `on_item` writes only stderr and never
   * touches the output buffer (the FIXPOINT is untouched).
   *
   * @param prog0    the checked, monomorphized program
   * @param on_item  the per-item progress hook (a no-op recovers `tk_emit_c`)
   * @return         the emitted C source, or the first codegen error
   */
  pub fn tk_emit_c_report(prog0: checker::TProgram, on_item: checker::ProgressFn) -> str | error
  ```
- **`cc` (processo externo):** **INVIÁVEL heartbeat DURANTE o `cc`.** `teko::process::run`
  (`project.tks:704,744`) é SÍNCRONO/bloqueante e teko NÃO tem threads nem spawn assíncrono
  com poll. Não dá para bater enquanto o `cc` roda. Honesto: a ÚNICA mitigação é a
  START-line em `Plain` ANTES da chamada bloqueante ("cc <binp> …") — que já é o conserto de
  maior valor, pois `cc` é a fase dominante (§A). Registrar como limitação; async de
  processo é trabalho futuro fora de escopo.

### B5. PARIDADE C / native

Hoje `backend` instrumenta só o caminho C (`project.tks:842-868`); `emit_native`
(`project.tks:1061`) e as caudas `emit_native_*` (`1093/1119/1149/1177/1252`) +
`finish_native_object` (`project.tks:1204`) NÃO têm fase nenhuma. Ao aposentar o C, a
observabilidade sumiria. Paridade proposta (mapeando às fases C `codegen`/`emit C`/`cc`):

- `emit_native` recebe `cfg: ReportCfg`. `lower_program` → fase `lower`.
- cada cauda: `select/regalloc/encode` → uma fase `codegen (native)` (em `debug`, três
  sub-fases nomeadas `isel`/`regalloc`/`encode`).
- `finish_native_object`: `write_file_bytes(.o)` → fase `emit obj`; `link_object` → fase
  `link` (a START-line antes do `cc`-linker, mesmo motivo do `cc`). `emit_native_wasm`:
  `emit obj`→ `emit wasm`, sem `link`.

Regra: mesmas labels/mesma forma que o caminho C, para o leitor de log não distinguir o
backend pela ausência de output. Nenhuma dessas fases altera os bytes do `.o`/binário.

---

## Gap 2 — eliminação do re-front-end (aprofundamento, decisão do owner)

### Fatos do código que governam a prova

1. **O FIXPOINT só restringe o programa de RELEASE.** `gen2.c==gen3.c` compara o C do
   BINÁRIO de release (`mono(sem_testes)` → `tk_emit_c` → o `teko`). O programa do GATE
   (`<name>-tktest.c`, `run_native_gate`, `project.tks:1524-1543`) é DESCARTÁVEL: compila,
   roda, some. **Os bytes do gate NÃO entram no fixpoint.** Isto abre espaço: o gate pode
   ter qualquer forma/ordem contanto que rode os testes.
2. **Ordem da saída de `monomorphize`** (`checker/monomorph.tks:1147-1249`):
   - Fase 1 (1164-1193): percorre `prog.items` EM ORDEM; itens não-genéricos vão para
     `kept` nessa ordem; templates genéricos são DROPADOS; instâncias-semente entram na
     `queue` na ordem de descoberta (ordem de item).
   - Fase 2 (1199-1233): consome `queue` em FIFO (`qi` de 0), estampa cada instância e a
     ANEXA a `kept` — logo a POSIÇÃO de uma instância em `kept` = quando é descoberta na
     fila.
   → **A ordem final de `kept` é sensível à ordem de descoberta.**
3. **`assemble_sel` itera a MESMA lista `files` para `include_tests` true/false**
   (`assemble.tks:145-190`); a ÚNICA diferença é o skip `if ends_with(.tkt) && !include_tests`
   (linha 157). Itens não-`.tkt` são produzidos em ordem/conteúdo IDÊNTICOS nos dois modos.
4. **`parser::Item` carrega `.file`** (`parser/ast.tks:411`) — dá para filtrar por
   PROCEDÊNCIA de arquivo com exatidão. `TFunction` também carrega `file`
   (`monomorph.tks:1175`).
5. **`#test` NÃO são roots de alcançabilidade** (`reachability.tks:193-206`): `entry_roots`
   coleta só statements soltos (virtual-main) e funções `exp`. Uma `#test` não é raiz e
   nenhum código de produção a chama.

### (a) Reusar o AST PARSEADO — **PROVÁVEL, seguro. RECOMENDADO (baseline).**

Ideia: separar PARSE de CHECK+MONO. Parsear a árvore UMA vez (com `.tkt`), derivar release e
gate da MESMA árvore:
- `parsed_all = assemble_sel(files, include_tests=true, …)` (parse único).
- `parsed_prod = filter_tkt(parsed_all)` — remove itens cujo `.file` termina em `.tkt`.
- gate    = `check+mono(parsed_all)`   (hoje = `fe.prog` do pass 1).
- release = `check+mono(parsed_prod)`  (hoje = `frontend_body(false, true, …).prog`).

**Prova de fixpoint.** Por (Fato 3+4), `filter_tkt(parsed_all)` é a lista de `parser::Item`
não-`.tkt` na MESMA ordem/conteúdo que `assemble_sel(files, include_tests=false)`. Ou seja,
`parsed_prod` ≡ o programa parseado que o `release_build_of` de hoje re-parseia. Como
`check+mono` é função determinística da entrada, `check+mono(parsed_prod)` produz o MESMO
`TProgram` que hoje → `tk_emit_c` idêntico → **binário de release byte-idêntico. FIXPOINT
PRESERVADO.** Nenhuma suposição sobre o checker; só o determinismo do lex/parse (já assumido
em todo o pipeline).

**Ganho.** Elimina re-lex + re-parse do corpus inteiro no Gap 2. Pelo shape do código, o
front-end custa lex + parse + **check (dominante)** + mono; (a) remove a fatia lex+parse
(estimo ~20-40% do Gap 2, dependente do corpus — não mensurável sem profiling, honesto).
Ainda roda check+mono de produção duas vezes.

**Custo/risco.** Baixo/estrutural-leve. Refatorar `frontend_body` em `frontend_parse`
(manifest→discover→assemble→`parser::Program`+`Manifest`) e `frontend_check`
(prune_os→load_deps→`checked_program_of`→`Frontend`). Um novo `filter_tkt(parser::Program)`.
Rewire `compile_project_g`/`run_gate`/`release_build_of`.

**Bônus de observabilidade:** o check+mono de release deixa de ser um "retry quiet já
reportado" e passa a ser trabalho LEGÍTIMO e distinto (produção-apenas) → pode REPORTAR
`checker`/`monomorph` de verdade (suprimindo só o banner duplicado). O Gap 2 deixa de ser
mudo SEM bracket artificial.

### (b) Prune por alcançabilidade pós-mono — **REFUTADO. Não usar.**

Ideia: derivar `mono(sem_testes)` de `mono(com_testes)` removendo itens/instâncias
alcançáveis SÓ de entry points de teste (em vez do `strip_tests` que só tira `is_test`).

Há walker (`close_reachable`/`entry_roots`, `reachability.tks`), e `#test` não são roots
(Fato 5) — então em tese daria pra identificar itens test-only. MAS **a prova de
byte-identidade FALHA por ORDEM** (Fato 2):

**Contraexemplo concreto (instância compartilhada reordenada).**
- Produção: `main` chama `foo<Bar>` no item de posição 10.
- Teste `aaa_test.tkt` (ordena antes no walk) tem `#test` que também chama `foo<Bar>`,
  descoberto na posição 0.
- `mono(com_testes)`: a `queue` recebe `foo$Bar` PRIMEIRO (descoberta pelo teste) → estampada
  cedo → aparece cedo em `kept`.
- `mono(sem_testes)`: `foo$Bar` é descoberta só por `main` → estampada na posição de `main`.
- `foo$Bar` é COMPARTILHADA (alcançável da produção), então o prune a MANTÉM — porém na
  POSIÇÃO ERRADA. `tk_emit_c` emite funções em ordem de item → **C em ordem diferente →
  bytes diferentes → FIXPOINT QUEBRA.**

O prune conserta o CONJUNTO mas não a ORDEM das instâncias compartilhadas, que a presença de
testes já perturbou ANTES do prune. Refutado. (O contraexemplo 1 do §B2 — instância só de
teste — o prune resolveria; o de ORDEM não.)

### (c) Reusar o CHECK (filtrar itens tipados antes de um mono de produção) — **maior ganho, seguro SÓ sob invariância validada pelo gate de fixpoint do CI.**

Ideia (c-final): checar a árvore COMPLETA (produção+testes) UMA vez → `pre_all` (PreMono).
- gate    = `mono(pre_all.prog)` (com testes; descartável).
- release = `mono(filter_tkt(pre_all.prog), table)` — mono sobre itens tipados de PRODUÇÃO só.

**Por que NÃO recai no (b):** aqui filtramos ANTES do mono. O mono de release constrói sua
`queue` SÓ dos corpos de produção, em ordem de produção → mesma ordem de
`mono(check(sem_testes))`. O problema de ordem do (b) não ocorre.

**O que resta provar (invariância do checker):** que os ITENS TIPADOS DE PRODUÇÃO
(pré-mono) são idênticos quer o check tenha visto os testes ou não. Argumento a favor:
namespaces isolam testes; corpos de produção não referenciam testes (Fato 5); resolução de
nome em corpos de produção resolve para alvos de produção mesmo com assinaturas de teste
extras no ambiente. Riscos residuais NÃO elimináveis estaticamente por mim:
- resolução global de overload/trait cruzando namespaces (um impl de teste mudaria a escolha
  num corpo de produção?);
- a `table` passada ao mono é dobrada COM testes; entradas de tipo-decl de teste extras
  poderiam, por colisão de nome, mudar o stamping de uma instância de produção.

Ambos são "quase certamente seguros" mas não os PROVO no papel. **Como a validação é só no CI
(seed local não builda), o gate de fixpoint É o validador exato:** se
`gen2.c==gen3.c` E `gen2.c` continua idêntico ao `gen2.c` pré-mudança, a invariância vale.
Recomendação: implementar (c-final) como crumb SEPARADO, gated no fixpoint — se regredir o
golden do `gen2.c` no CI, reverter para (a).

**Ganho.** Elimina re-lex+re-parse+re-check de produção; sobra só um segundo `mono`. Remove a
fatia dominante (check) → estimo ~60-70% do Gap 2. É o que melhor atende "não recarregar o
front".

**Custo/risco.** Estrutural + risco de fixpoint (invariância). Precisa `filter_tkt` em nível
de `TItem` (procedência em TFunction/TypeDecl/TStatement/ConstDecl — auditar que todos
carregam `file`; TFunction carrega, `monomorph.tks:1175`).

### Veredito

1. **(a) preserva o fixpoint COM PROVA** (determinismo de parse + Fatos 3/4) — SHIP.
2. **(b) REFUTADO** (contraexemplo de ordem).
3. **(c-final)** é o maior ganho e o que o owner quer, mas a segurança do fixpoint depende de
   invariância do checker que só o gate de fixpoint do CI confirma — SHIP como follow-up
   gated, com reversão automática para (a) se o golden regredir.

Sequência: (a) primeiro (ganho garantido, mudez do Gap 2 resolvida honestamente), depois
(c-final) atrás do gate de fixpoint. NENHUM caso força manter o bracket — ele foi
substituído por (a).

---

## Proposta do owner — C base compartilhado + delta de teste (.h + link)

Forma refinada (evoluiu de "`#include` do .c" → "`teko.h` + compilação separada + link"):
emitir o C de PRODUÇÃO uma vez (`teko.c` + `teko.h`); o TU de teste passa a `#include
"teko.h"` e conter SÓ testes + delta + test-main (TU minúsculo); produção compila para um
`teko.o` LINKÁVEL; binário de teste = `teko.o(instrumentado) + teko_test.o` linkados;
diferenças de cobertura por macro (`-DTEKO_COV`).

### Fatos do código (confirmados)

- **Privados JÁ são EXTERNAL + mangled-únicos.** `emit_function_sig` (`codegen.tks:6393-6419`)
  NÃO emite `static` — só `extern` para protótipo foreign (6396) e `__attribute__((noinline))`
  para auto-recursiva (6395). Todo símbolo é `teko__<ns>__<name>` (#49,
  `cb_fn_name`/`mangle_type_name`, globalmente único). **→ a tag `TK_LINKAGE static/vazio` do
  owner é DESNECESSÁRIA hoje: não há função file-static para expor. O `teko.h` só precisa dos
  protótipos (o pass de protótipos já existe em `tk_emit_c`, ~`codegen.tks:8630`) e os testes
  linkam contra `teko.o` sem nenhuma mudança de linkage. Zero colisão (mangling único).**
- **Sem `__LINE__`/`__FILE__` no C emitido** (grep vazio em `codegen.tks`). Release sem `-g`
  (`build_cc_argv` debug=false). **→ macro de cobertura VAZIA deixa os MESMOS tokens
  pós-preprocessor → mesmo código de máquina → `teko.o` de release BYTE-IDÊNTICO ao de hoje.**
- **Cobertura é INLINE e indexada por ÍNDICE de item.** `CgMode={Program;TestPlain;TestCov}`
  (`codegen.tks:50`); `CovCtx` (65-70) carrega `fn_idx` = índice do fn de produção em
  `prog.items`; `emit_cov_line/branch` (105-134) emitem `tk_cov_line_at(fn_idx,line)` só se
  `ctx.on`; prólogo `tk_cov_mark(cov_idx)` (`emit_function_cov`, 6649-6652); `Program`/
  `TestPlain` usam `cov_off()` → 0 marcas → byte-idêntico.
- **A caminhada de floors usa `fe.prog` = `mono(com_testes)`** (`project.tks:1488/1492/1507`).
- **Empírico** (main 0.3.0.21, seed 0.3.0.16, CI=1): front-end ~49s; depois **bloco silencioso
  único de codegen+write+cc >240s e contando** (dominante). O split codegen-vs-cc **continua
  não medido** — sem instrumentação de fase, codegen→write→cc é um bloco mudo só. **O crumb 1
  (START/settle em `codegen`/`emit C`/`cc`) é a FERRAMENTA de medição desse split.**

### 1. Visibilidade

Resolvida por construção (fato 1): funções já external+únicas; `teko.h` = os type-decls + o
pass de protótipos extraídos para header. Delta chama privados de produção livremente (são
external). A `TK_LINKAGE` é futuro-proofing barato (expandir p/ nada hoje) caso um dia o
codegen emita alguma função `static` — mas NÃO é necessária agora. Ressalva: file-statics que
EXISTEM (`tk_build_meta` `codegen.tks:299`; literais static em corpos) ficam dentro do
`teko.o` e não são referenciados entre TUs → sem problema.

### 2. Contabilidade honesta do CC

No CI fresh-build o `cc` NÃO diminui: `teko.o` (release, macros vazias) + `teko_cov.o`
(produção instrumentada) + `teko_test.o` (delta minúsculo) → produção ainda passa 2× pelo cc,
IGUAL a hoje (release.c + test.c-que-contém-produção). O que GANHA:
  (i) **codegen do TU de teste vira o DELTA** (não re-emite a produção inteira) — a produção é
      emitida 1× (base) em vez de 2× → **~metade do codegen de produção**, o ganho real SE
      codegen dominar;
  (ii) fonte única do C de produção (manutenção);
  (iii) incrementalidade dev-local (reusar `teko.o` se `teko.c`+defines não mudam) — INÚTIL no
      CI fresh (frio) e marginal no dev (editar produção regenera `teko.c`);
  (iv) paralelismo dos dois cc — `teko::process::run` é SÍNCRONO, sem spawn async → só futuro.
Quantificação: front-end ~49s vs bloco codegen+cc >240s. Se codegen ≈ metade do bloco, a
proposta economiza um passe de produção (~dezenas–>100s). **Depende do split que o crumb 1
mede.**

### 3. Cobertura (o crux) — release byte-idêntico CONFIRMADO, com um NÓ de indexação

Release `teko.o` = `cc teko.c` com `TK_COV_*` vazias → tokens idênticos aos de hoje → objeto
byte-idêntico (fato 2). Gate `teko_cov.o` = `cc teko.c -DTEKO_COV` → marcas ativas.
**NÓ de integração:** `cov_idx` = índice do fn em `prog.items`. A base `teko.c` que gera o
`teko.o` de release DEVE vir do programa de release (`mono(sem_testes)`) para o objeto ser
byte-idêntico — então os `cov_idx` da base são índices de `sem_testes`. Mas a caminhada de
floors hoje usa `mono(com_testes)` (`project.tks:1488`). **Os índices DIVERGEM** (itens de
teste intercalados deslocam os índices absolutos). A proposta EXIGE alinhar a base de
indexação da cobertura: ou (a) a caminhada de floors passa a usar o programa base
(`sem_testes` — o que semanticamente até faz mais sentido: cobertura mede PRODUÇÃO), ou (b)
re-chavear a cobertura por NOME MANGLED em vez de índice (refactor maior). É custo real, não
trivial — sinalizado.

### 4. Delta

`delta = mono(com_testes) ∖ mono(sem_testes)` por nome mangled = `#test` + helpers de `.tkt` +
instâncias genéricas só-de-teste (`foo<TSóDeTeste>`). Vive em `teko_test.c`, chama produção via
protótipos de `teko.h` + link contra `teko.o`. **Funciona SÓ porque produção é external+única
(fato 1);** sem o header/link, TUs separados não se enxergariam. O set-diff é determinístico
(conjunto); ORDEM é irrelevante (gate é descartável, fora do fixpoint). Risco menor: corpo de
`foo<TSóDeTeste>` que referencie um file-static de produção — não ocorre no codegen atual
(literais são compound literals/local static), mas registrar.

### 5. Alinhamento 0.4 (sobrevive à aposentadoria do C)

O `.h`/`#define` é a REALIZAÇÃO C de um conceito backend-agnóstico:
**emitir o INTERMEDIÁRIO de produção uma vez, produzir o objeto DUAS vezes parametrizado por
cobertura (on/off), e linkar o delta como objeto separado via símbolos external únicos.**
- C: intermediário = texto `teko.c`; toggle = `-DTEKO_COV`; dois objetos via cc; link.
- Native (0.4): intermediário = LIR (`lower_program`); toggle = flag de cov no LIR→máquina;
  dois `.o` (limpo p/ release, instrumentado p/ gate); `link_object` já lida com `.o` external
  (`finish_native_object`/`link_object`, `project.tks:1204/743`). Os símbolos mangled únicos
  (fato 1) são o que faz o link cross-objeto funcionar nos DOIS backends.
- **Ressalva honesta:** hoje o gate SEMPRE usa o TU C (`run_native_gate`→`tk_emit_c_test`→
  `run_cc`, `project.tks:1531/1534`), MESMO com `--backend=native`. Então para um release
  NATIVE não há `teko.c` compartilhado entre release(.o native) e gate(.c) → o
  compartilhamento C-#define **só beneficia o release C** (o caminho do fixpoint hoje). Em 0.4,
  com gate também native, o conceito mapeia p/ dois `.o` native.

### 6. Veredito revisado + encaixe

- É uma **restruturação de CODEGEN — ONDA PRÓPRIA pós-observabilidade**, NÃO um crumb de
  stderr. **Não bloqueia** os crumbs só-stderr (1-3) nem os de front-end (3/4b).
- **Depende** de ter `mono(com)` E `mono(sem)` — que os crumbs 3/4b já produzem. Encaixa
  DEPOIS deles (é dedup de codegen, ortogonal à dedup de front-end; os ganhos EMPILHAM).
- **Payoff condicionado ao split codegen-vs-cc que o crumb 1 mede.** Dado o bloco >240s, é
  plausível que codegen seja grande → alto valor; mas se o cc dominar, a proposta **não reduz
  cc** (2× produção continua) e o custo estrutural (macros no emissor + re-baseline do golden
  `gen2.c` + nó de indexação da cobertura) não compensa.
- **Fixpoint:** o TEXTO de `teko.c` muda (marcas-macro + `#define` no topo → novo golden + bump
  de seed — mudança ESTRUTURAL do artefato, ritual pesado); o OBJETO/binário de release fica
  byte-idêntico (fato 2). Ritual = gate de fixpoint do CI valida o novo golden.
- **Recomendação:** (i) landar crumb 1 e MEDIR o split; (ii) só então decidir. Se codegen
  dominar, agendar como onda de codegen pós-observabilidade (independente do const wave), como
  ponte natural para o modelo de dois-objetos do 0.4. Se cc dominar, DESPRIORIZAR (ganho pequeno
  perto do custo/risco) e focar em front-end (3/4b) + observabilidade.

---

## Onda AL — alocação & throughput do build (NÃO confundir com a Fase E) (produtor 22× mais lento que o cc)

Dados do owner (CI real, fork, macOS, crumbs 1-3): `emit test` = 17.7 MB em 201.1s
(**~88 KB/s**); `cc test` compila os MESMOS 17.7 MB em 9.0s → o consumidor é **22× mais
rápido que o produtor**. Local: codegen release 8 MB em 124.3s (~65 KB/s). Pico 1.8 GB para
8-17 MB de texto. O gargalo é o EMISSOR, e vale para C E native.

### Provas de código (hipótese "concat imutável → quadrático": REFINADA)

1. **Caminho quente C `cb`** (`codegen.tks:148-150`) → `teko::mem::append_fo` → runtime
   `tk_append_bytes_fo` (`teko_rt.c:2178`). O crescimento é **GEOMÉTRICO** (`cap = len < 4 ? 8
   : len*2`, `teko_rt.c:2192`) COM fast-path in-place via um **cache GLOBAL de ponteiros**
   `tk_push_cache` (`teko_rt.c:2092`, `TK_PUSH_HASH_SIZE = 1<<16 = 65536` slots single-probe,
   hash do ponteiro `teko_rt.c:2093-2095`). **→ NÃO é quadrático INGÊNUO** (a hipótese literal
   é refutada; o #148 fez trabalho pesado aqui).
   **PORÉM a amortização é FRÁGIL:** o in-place só ocorre se o witness (ptr+len+esz+region+gen)
   bater (`teko_rt.c:2180-2189`), e o re-cache após um copy-grow só acontece se o slot estiver
   vazio OU o incumbente for MENOR (`teko_rt.c:2205`, despejo por tamanho). Se o buffer de
   saída multi-MB (o MAIOR do processo) colidir de hash com outro buffer vivo grande (ex.: as
   listas do TAST de `mono(com_testes)`) que ocupe seu slot, ele **NÃO é cacheado** → o próximo
   `cb` falha o witness → **copy-grow COMPLETO de len bytes** → e pode não recachear →
   **CADA append vira uma cópia total = O(n²).** É a mesma classe do "11.5 GB fix / clobber
   2300×" que os comentários #148 descrevem (`teko_rt.c:2155-2160`).
   **Aritmética confirma superlinearidade:** 17.7 MB em ~1-2M fragmentos a ~1µs/append (call +
   ponte statement-expr + probe de 5 campos) daria ~1-2s; **201s ⇒ ~100-200× ⇒ a cauda
   quadrática (tempestade de copy-grow) está ATIVA e domina.** O pico 1.8 GB é consistente com
   o churn de copy-grows na free-list + o TAST de `mono(com_testes)` residente (honesto: o pico
   é dos DOIS, não só do emissor).

2. **Writers native — PIORES** (owner: "o mesmo vale para native"). `objfile_macho.tks:12-19`,
   `encode_*`, e o codec `tkb_buf.tks:6-13` constroem bytes **UM BYTE POR VEZ** via
   `teko::list::push` → `tk_slice_push` (`teko_rt.c:2216`, região ROOT, **SEM free-old**) →
   mesma fragilidade de cache + overhead MÁXIMO (uma chamada de função POR BYTE) + TODOS os
   degraus geométricos retidos no root (churn de memória sem free). Pior que o caminho C.

3. **`concat`/interp** (`codegen.tks:2319`, `$"..."`) dobram via `tk_str_concat`
   (`teko_rt.c:139`) — cada fold aloca buffer fresco e COPIA o acumulado → O(k²) em peças por
   chamada (k pequeno por chamada, mas pervasivo). Não é o dominante, mas soma.

4. **RawBuf NÃO é builder** (`mem/unsafe/rawbuf.tks`): só `rawbuf_alloc(len)` (FIXO, sem
   capacidade extra), `rawbuf_read`→`[]byte` (cópia), `rawbuf_len`. **Falta:** um campo `cap`,
   `reserve`, append amortizado, e um `to_str`/`to_bytes` barato (view sem cópia). Um builder
   real exige API unsafe nova (ou primitivo de runtime).

### Design (onda "emitter throughput", comportamento-preservador)

Substituir a amortização dependente-de-cache-global por um builder que **CARREGA a própria
capacidade** (sem cache compartilhado, sem witness, sem inanição) → amortizado-O(1) GARANTIDO
e uma chamada por fragmento sem probe. **Mesmos bytes emitidos** (a ordem/conteúdo dos appends
é idêntica) → fixpoint intacto.

- **AL1 — MEDIR (risco zero, sem mudança de produto).** A infra #148 já tem os contadores:
  `tk_obs_miss[why]`/`tk_obs_miss_big[why]` (`teko_rt.c:2132-2138`) classificam POR QUE o
  in-place falhou (0=sem ptr, 1=ptr colidiu/despejado, 2=len, 3=cap exausto, 4=outro) e
  `tk_obs_push2` atribui os grows caros. Rodar um gate com obs ligada CONFIRMA se a tempestade
  é dominada por `why==1` (colisão/despejo — a inanição do cache) vs `why==3` (cap real). Usar
  também o crumb 1 (START/settle em `codegen`/`emit C`/`cc`) para separar codegen-vs-cc. É a
  ferramenta que decide o tamanho do ganho.

- **AL2 — Robustez do runtime (seed C mantido, byte-preservador) — ALAVANCA PRINCIPAL.**
  Garantir que os poucos buffers multi-MB (a saída do emissor / writers) mantenham
  amortizado-O(1) INDEPENDENTE do cache global: p.ex. um registro dedicado para builders
  grandes, OU capacidade carregada de forma robusta, de modo que `tk_append_bytes_fo`/
  `tk_slice_push` NUNCA degradem para copy-grow total repetido do buffer grande.
  `src/runtime/teko_rt.c` é o **seed C MANTIDO** (edição permitida pela lei). Bytes de saída
  INALTERADOS → **fixpoint preservado (gen2.c==gen3.c + `cmp` local)**. Se a tempestade for
  confirmada (AL1), esta é a alavanca de ORDEM DE GRANDEZA (ver ganho abaixo). Guarda: gate de
  659 testes + TEKO_MEM_PARANOID + fixpoint.

- **AL3 — Builder explícito (mais fundo, opcional se AL2 bastar).** Introduzir um valor
  `Builder` `{ptr,len,cap}` na superfície (`teko::mem`), SAFE (backed por primitivo de runtime,
  NÃO derivado de RawBuf — evita a contágio unsafe U2/#333), que baixa para um append de
  runtime robusto (sem cache global, sem probe por fragmento). Threadar pelos `cb`/`cb_byte`/
  `cb_i64`… do emissor (`[]byte`→`Builder`) e pelos `write_u*`/`list::push` quentes dos writers
  native. Remove o CONSTANTE por-fragmento residual (probe de 5 campos + ponte). Diff mecânico
  GRANDE (assinaturas `buf: []byte` em centenas de emit fns); bytes preservados por construção.

- **AL4 — Paridade native.** Aplicar AL2/E3 aos `objfile_*`/`encode_*`/`tkb_buf`
  (`list::push` byte-a-byte → builder/append robusto). Fecha o "mesmo vale para native" do
  owner; `cmp` dos goldens de `.o`/`.wasm` como ritual.

### Ganho honesto

- **SE a tempestade (why==1) domina** (o que os 88 KB/s ⇒ superlinear indicam), AL2 restaura
  amortizado-linear real: 17.7 MB ≈ 35 MB de memcpy + overhead por fragmento. Mesmo a 50-100
  MB/s efetivos (com overhead de call sob o seed), ~0.2-0.4s de cópia ⇒ **de 201s para poucos
  segundos ⇒ ~50-100×.** Pico de memória (#148): remover o churn de copy-grow derruba a
  free-list inchada ⇒ pico cai em direção ao piso (TAST do mono + 1 buffer de saída ~2× final).
- **SE o constante por-fragmento domina** (call+probe), AL2 ajuda menos e AL3 (remover o probe)
  dá ~2-5×. **AL1 decide qual.** Os 88 KB/s pesam fortemente para o primeiro caso.

### Composição e PRIORIDADE (codegen já provado 85-93% do custo)

1. **THROUGHPUT (AL1 medir → AL2 runtime) — PRIORIDADE MÁXIMA.** Maior ROI, MENOR risco
   (byte-preservador, runtime-only), vale para C E native, ataca direto os 85-93%. Se E2
   restaurar MB/s, 17.7 MB viram ~segundos e o problema some.
- **base+delta** (seção anterior): reduz o VOLUME emitido; é ORTOGONAL ao throughput (emitir
   menos × emitir mais rápido MULTIPLICAM). Mas é estrutural + re-baseline do golden `gen2.c` +
   nó de indexação de cobertura + só C-release → **DEPOIS do throughput** (e possivelmente
   DESNECESSÁRIO se AL2 já traz MB/s: 17.7 MB a 5 MB/s = 3.5s).
- **heartbeat (crumb 5)** é OBSERVABILIDADE, não velocidade: o crumb 1 (START/settle) já
   des-muta a fase; o heartbeat fino é nice-to-have de MENOR urgência quando o emit já é rápido.
- Ordem recomendada: **AL1 → AL2 (headline) → medir de novo → (se preciso) AL3/E4 → base+delta só
   se ainda valer.** Tudo INDEPENDENTE do const wave (não toca const-eval).

### Riscos

- **E2** edita `teko_rt.c` (seed mantido): PERIGO = mudar bytes de saída. Mitiga: o append é
  byte-idêntico (só a bookkeeping de capacidade muda); ritual = fixpoint `gen2.c==gen3.c` +
  `cmp` local + gate 659 + `TEKO_MEM_PARANOID` (a rede do #148).
- **E3** (`[]byte`→`Builder`): churn de assinaturas + risco de um alias romper o decreto de
  cadeia LINEAR (o mesmo invariante que `append_fo`/`push_fo` já exigem). Evitar contágio unsafe
  (Builder SAFE, não RawBuf). Regressão por crumb: fixpoint + `cmp`.
- **E4** native: muda memória/perf dos writers, não os bytes; `cmp` dos goldens `.o`/`.wasm`.
- Todos independentes do const wave.

---

## C. SEQUÊNCIA DE CRUMBS (menor risco → maior)

Legenda risco: **baixo** = só stderr; **estrutural** = assinatura/CLI/codegen. Todos
independentes do const wave (nenhum toca const-eval); ordem respeita o seed (só usa recursos
já no seed: enums+`==`, structs, closures/`ProgressFn` já usados).

### Crumb 1 — Report 3-state (baixo) — FECHA Gap 3 e torna tudo visível em Plain
- Arquivos: `src/build/progress.tks`, `src/build/assemble.tks`, `src/build/project.tks`
  (`checked_program_of`, `backend`).
- Novos: `pub type Report = enum { Tty; Plain; Quiet }`; `report_mode(quiet,tty)` (transitório,
  substituído por `report_mode_v` no crumb 6); `Phase.mode: Report`; `phase_begin` emite START
  em `Plain` via `eprintln(mid_line)`; `phase_tick` só em `Tty`; `phase_end_*` no-op em `Quiet`.
- É o rascunho `report-3state` MENOS o bracket do Gap 2 (crumb 3).
- Regressão `.tkt`: `src/build/progress_test.tkt` — `report_mode_resolves_the_three_states`
  (já no rascunho). Ponto de gate: `cli_flags_test.sh`-style — build de um fixture com
  `CI=1` deve conter, em stderr, a START-line `codegen …` ANTES do settle (prova de que
  Plain anuncia).
- Risco: baixo. Independente do const wave: sim.

### Crumb 2 — Instrumentar Gap 1 (baixo) — FECHA Gap 1
- Arquivos: `src/build/project.tks` (`run_native_gate` ganha fases `emit test`/`cc test`;
  assinatura recebe `cfg`/`tty`; ajustar chamadores `run_gate_native:1582`,
  `test_project:1859`).
- Regressão: fixture com `#test`, `CI=1`, `teko test` → stderr contém `cc test` START.
- Risco: baixo. Independe do const wave.

### Crumb 3 — Gap 2: reusar o AST parseado (abordagem (a)) — ELIMINA o re-lex/re-parse + mata a mudez
- SUBSTITUI o bracket paliativo (rejeitado pelo owner).
- Arquivos: `src/build/project.tks` (split de `frontend_body` em `frontend_parse` +
  `frontend_check`; novo `filter_tkt(parser::Program) -> parser::Program`; rewire de
  `compile_project_g`/`run_gate`/`run_gate_native`/`release_build_of` para parsear UMA vez e
  derivar gate=`check(parsed_all)` e release=`check(filter_tkt(parsed_all))`).
- Assinaturas novas:
  - `fn frontend_parse(include_tests: bool, cfg: ReportCfg) -> ParsedFront | error`
    (`ParsedFront = struct { parsed: parser::Program; manifest: Manifest }`).
  - `fn frontend_check(pf: ParsedFront, cfg: ReportCfg) -> Frontend | error`.
  - `fn filter_tkt(p: parser::Program) -> parser::Program` (dropa itens com `.file`
    terminando em `.tkt`; procedência exata, `ast.tks:411`).
- **Prova de fixpoint** (§"Gap 2", (a)): `filter_tkt(parse(with tests))` ≡ `parse(without
  tests)` (Fatos 3/4) → release byte-idêntico. Ponto ritual: **gate de fixpoint do CI**
  (`gen2.c==gen3.c` e golden do `gen2.c` inalterado).
- Observabilidade: o `frontend_check` de release passa a REPORTAR `checker`/`monomorph` de
  verdade (não mais quiet), suprimindo só o banner duplicado. Regressão `.tkt` + build gated
  com `CI=1` → stderr mostra a fase `checker` da produção entre os testes e o backend.
- Risco: estrutural-leve; fixpoint PROVADO. Independe do const wave.

### Crumb 4 — Heartbeat do checker/mono (estrutural leve)
- Arquivos: `src/build/project.tks` (`checked_program_of` aceita heartbeat `ProgressFn`;
  emite só se `heartbeat_on(cfg, quiet_pass)`).
- Novo: `checker_heartbeat_fn(phase, cfg) -> checker::ProgressFn` (tick por N itens / ~1 s).
- Após o crumb 3, o check+mono de release já reporta; o heartbeat cobre corpos longos em
  `Plain`+`verbose`.
- Regressão: `--verbose`+`CI=1` → múltiplas linhas de heartbeat de `checker`.
- Risco: estrutural (assinatura). Pode usar bool provisório e migrar a `ReportCfg` no crumb 6.
  Independe do const wave.

### Crumb 4b — Gap 2 agressivo: reusar o CHECK (abordagem (c-final)) — FOLLOW-UP gated no fixpoint
- Só entra se o gate de fixpoint do CI confirmar a invariância (§"Gap 2", (c)).
- Arquivos: `src/build/project.tks` — checar a árvore completa UMA vez (`pre_all`), gate=
  `mono(pre_all.prog)`, release=`mono(filter_tkt_titem(pre_all.prog), table)`.
- Novo: `filter_tkt_titem(prog: checker::TProgram) -> checker::TProgram` (procedência em
  nível de `TItem`; auditar `file` em TypeDecl/TStatement/ConstDecl).
- **Risco de FIXPOINT (invariância do checker) — reversão automática para o estado do crumb 3
  se o golden do `gen2.c` regredir no CI.** Ponto ritual: gate de fixpoint.
- Ganho: remove a fatia dominante (check) do Gap 2. Independe do const wave.

### Crumb 5 — Heartbeat do codegen (estrutural, toca codegen)
- Arquivos: `src/codegen/codegen.tks` (`tk_emit_c_report` + `on_item` no loop de corpos de
  `tk_emit_c_mode`; `tk_emit_c` vira wrapper no-op), `src/build/project.tks` (`backend` passa
  o hook).
- **PROVA de byte-identidade obrigatória**: o `on_item` não toca `b`; golden `gen2.c==gen3.c`
  intacto. Regressão: além do `.tkt`, o gate de FIXPOINT do CI é o ponto ritual.
- Risco: estrutural (codegen). Independe do const wave.

### Crumb 6 — CLI verbosidade (estrutural, CLI+help)
- Arquivos: `main.tks`, `src/build/project.tks` (`Verbosity`, `verbosity_of`, `ReportCfg`,
  `report_mode_v`, `heartbeat_on`; `project_arg_of` pula novos tokens; trocar o thread
  `tty:bool`→`cfg:ReportCfg` em `compile_project_g`, `frontend_body`, `checked_program_of`,
  `backend`, `run_native_gate`, `release_build_of`, `codegen_and_report`, etc.),
  `src/build/help.tks`.
- Regressão: `scripts/cli_flags_test.sh` estende — `--verbosity=quiet` silencia fases;
  `--verbose` liga heartbeat; `-q`/`-V` idem; `-v` continua `--version`. Golden do help.
- Risco: estrutural (assinaturas em massa + golden do help). Independe do const wave.

### Crumb 7 — Paridade native (estrutural, muitas caudas)
- Arquivos: `src/build/project.tks` (`emit_native` + 5 caudas + `finish_native_object`
  recebem `cfg`; fases `lower`/`codegen (native)`/`emit obj`/`link`; wasm: `emit wasm`).
- **Sem mudar bytes** do `.o`/binário/`.wasm`. Regressão: `diff_c_own.sh`/`native_regressions.sh`
  como ponto ritual; um `.tkt` de fixture native com `CI=1` mostrando START `link …`.
- Risco: estrutural (mecânico). Independe do const wave.

### Crumb 8 — `.tkc` scaffolding (baixo, FORA do fixpoint)
- Arquivos: novo `src/build/tkc.tks` (`write_tkc`/`read_tkc` — doc deixando explícito que
  NÃO entra no `release_build_of` default), `src/build/tkc_test.tkt`.
- Uso seguro: emitir `.tkc` de `strip_tests(rel.prog)` após o build, atrás de flag
  `--emit-tkc` OFF-by-default (inspeção/futuro incremental). NÃO no caminho do fixpoint.
- Regressão: round-trip `deserialize(serialize)==prog` (espelha `emit/tkb_test.tkt`);
  garantir que build default NÃO produz `.tkc` (fixpoint intacto).
- Risco: baixo PORQUE não fiado no default. Independe do const wave.

### Onda própria (pós-observabilidade, NÃO um crumb de stderr)
- **Throughput do emissor (AL1→AL2→AL3/AL4):** ver seção "Onda: throughput do emissor".
  **PRIORIDADE MÁXIMA das ondas** — byte-preservador, vale C E native, ataca os 85-93% do
  custo. AL1 (medir com obs #148 + crumb 1) → AL2 (robustez do runtime, seed C mantido,
  potencial ~50-100×) → AL3/E4 (builder explícito + writers native). Ritual: fixpoint + `cmp`.
- **C base compartilhado + delta (.h + link):** ver seção "Proposta do owner". Dedup de
  CODEGEN (produção emitida 1×), ortogonal a 3/4b (dedup de front-end) e ao throughput (emitir
  menos × mais rápido). Gated na medição do crumb 1; **DEPOIS do throughput** (pode ficar
  desnecessário se AL2 trouxer MB/s). Custo: macros de cov + re-baseline do golden `gen2.c` + nó
  de indexação da cobertura. Ponte para o modelo dois-objetos do 0.4.

**Pontos rituais (gate cheio deve passar):** fim do crumb 5 (fixpoint após tocar codegen),
fim do crumb 6 (CLI + goldens de help), fim do crumb 7 (`diff_c_own`/native regressions),
fim do crumb 8 (provar que default não emite `.tkc`).

---

## D. RISCOS (o que pode quebrar fixpoint/goldens e como evitar)

1. **`.tkc` no caminho default quebraria o fixpoint** (PROVADO §B2: divergência de
   monomorfização/procedência). Evitar: NÃO fiar no `release_build_of`; `.tkc` fica OFF-by-
   default (crumb 8). Este é o trade-off ruim que o owner pediu para eu sinalizar.
2. **Heartbeat do codegen tocando bytes** (crumb 5). Evitar: `on_item` só stderr, nunca
   toca `b: []byte`; ponto ritual = gate de fixpoint `gen2.c==gen3.c`.
3. **START-lines em Plain mudando goldens de output de TESTE** — se algum golden do CI
   compara stderr do build literalmente, as novas linhas quebram. Evitar: auditar
   `scripts/*_test.sh` que capturam ERR (ex.: `cli_flags_test.sh` compara stderr do help,
   não do build) e ajustar asserts para `grep -qF` (substring), nunca igualdade total.
4. **Golden do `--help`** muda no crumb 6 (novas flags). Evitar: atualizar help + seu teste
   no mesmo crumb (squash).
5. **`-v` acidental para verbose** quebraria `--version` (`main.tks:40`) e
   `cli_flags_test.sh`. Evitar: `-V`/`--verbose`, nunca `-v` (§B1).
6. **Paridade native alterando bytes** (crumb 7) se uma fase reordenar/relocar chamadas.
   Evitar: fases envolvem as chamadas existentes sem mudar ordem/args; ponto ritual
   `diff_c_own.sh`.
7. **Validação só no CI** (seed local 0.3.0.16-beta pré-const não builda a fonte). Todo crumb
   traz `.tkt` + depende do gate do CI para o teste real; não confiar em build local.

## Blocked / pendências reportadas (não viram issues novas)
- Eliminação do Gap 2 pela RAIZ agora está EM ESCOPO: (a) reuso de AST parseado (provado,
  crumb 3) + (c-final) reuso de check (crumb 4b, gated no fixpoint). O bracket paliativo foi
  DESCARTADO por decisão do owner.
- (c-final) depende de uma INVARIÂNCIA do checker (itens tipados de produção idênticos com/sem
  testes) que só o gate de fixpoint do CI confirma — não provável estaticamente aqui.
- Heartbeat DURANTE o `cc`/linker externo exige spawn assíncrono + poll (inexistente) —
  REPORTADO; mitigação é a START-line pré-`cc`.
- Checagem INCREMENTAL de testes sobre uma base de produção já checada (pra evitar o 2º mono
  de produção no gate) esbarra no mecanismo de dep, que trata a base como pré-monomorfizada e
  não instancia genéricos com novos argumentos de tipo — exigiria cirurgia no checker, fora de
  escopo. REPORTADO.

---

## VALIDAÇÃO EMPÍRICA A/B (2026-07-18, container Linux, gcc, CI=1)

Bancada: fonte da `main` 0.3.0.21 em duas cópias limpas (P1/P2); compilador C1 =
main sem patch, C2 = main + crumbs 1-3, ambos buildados pelo seed 0.3.0.16.

### Baseline (C1 × P1, pipeline atual): 595.3s, ~90% em silêncio
- `monomorph ✓` @29.5s → **406.4s de silêncio** → primeiro `test … ok` (Gap 1);
- último teste → **131.6s de silêncio** → settle `codegen 4499/4499 121.5s ✓`
  (Gap 2 ≈ 10s de re-front-end quiet + Gap 3 = codegen sem START);
- `emit C ✓` → 13.9s mudo → `cc ✓`.

### Com crumbs 1-3 (C2 × P2): 600.6s (custo ≈ zero) — nenhum trecho anônimo
- Toda fase anuncia no START (Plain) e assenta com tempo+✓: `emit test` 382.1s,
  `cc test` 26.9s, release `checker 4460/4460` 6.2s (REPORTADO — antes mudo;
  4460 vs 7484 itens do gate = o `filter_tkt` em ação), `monomorph 0/0` 2.8s,
  `codegen` 124.3s, `cc` 12.5s.
- **O split que faltava, medido pelas próprias fases**: `emit test` (codegen do
  TU de teste) = 382s vs `cc test` = 27s → **codegen ≈ 93% do Gap 1** — a
  dominância do codegen está confirmada em produção (reforça o crumb 5 e a
  onda base+delta). O silêncio restante é INTERNO a fases anunciadas
  (heartbeat, crumbs 4-6).

### Byte-identidade (fixpoint): ✅
`cmp P1/bin/teko.c P2/bin/teko.c` → **IDÊNTICOS (8.473.049 bytes)**; o TU de
teste também. A prova estática da abordagem (a) confirmada empiricamente.

### Escada local de seeds (bootstrap sem download)
O seed 0.3.0.16 NÃO compila a umbrella (const em uso no fonte desde o crumb 9),
mas a cadeia local replica o CI sem nenhum seed externo:
`0.3.0.16 (binário) → main/0.3.0.21 (pulo direto) → 8521a1b/0.3.0.22 (bump #1,
suporte a const, fonte ainda sem uso) → aa32910/0.3.0.23 (bump #2) → branch`.
Cada degrau `teko build . --no-verify` (~3min); degraus preservados como
binários (`seeds/teko-0.3.0.NN`). Procedimento canônico para agents validarem
a umbrella localmente.

### Nota de nomenclatura e ordem (ruling do owner, 2026-07-18)

Esta onda chamava-se "E1-E4" em rascunhos — COLIDIA com a **Fase E** do projeto
(o LINKER PRÓPRIO, issues/PRs já fechados na antecipação da main). São coisas
distintas: a Fase E entrega independência de toolchain e NÃO resolve o
problema de alocação — pelo contrário, um linker construído sobre os padrões
atuais (rebind de `push`, alocações por byte) HERDA o trap. Lei de ordem:
**a onda AL precede a Fase E como pré-requisito.**

### AL5 — lifetime por fase do pipeline (a terceira perna, ruling do owner)

Além do append amortizado (AL2/AL3), o PIPELINE retém tudo até o fim (região
root): tokens vivos após o parse, AST viva após o check, TAST viva após o
codegen, e as gerações velhas de cada array crescido — o pico de ~1.8 GB é
esse rastro. AL5 = regiões por estágio do build: ao entrar na fase N+1, os
intermediários da fase N morrem (o modelo de um pipeline de dados real:
consome, produz, LIBERA). Reduz pico de memória e pressão de alocador; o
desenho fino (fronteiras de região × o que atravessa fases, ex.: interns de
string) é o design-ahead da onda.

### Critério de aceitação (ruling do owner)

Codebase de mesmo tamanho: **teko builda mais rápido que rustc.** Baseline
2026-07-18 (CI real): emit test 17.6 MB — 201s (macOS) / 307s (Windows) /
520s (ubuntu); gate completo 5-12 min por OS. Meta pós-AL: segundos de teko +
o tempo do cc.
