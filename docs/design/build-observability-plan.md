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
