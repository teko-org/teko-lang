# TEKO_ROADMAP_INDEPENDENCE — projeto, testes e independência (pós-primeiro-binário)

> Sucede/!acompanha o `TEKO_ROADMAP_BINARY.md` (o primeiro binário via transpile-para-C, M0/M1).
> Enquanto o BINARY prova a *pipeline* (read→lex→parse→check→emit-C→cc→exec), este roadmap reajusta o
> que falta para a Teko ser uma toolchain **de verdade**: compilação **por projeto** (`.tkp`), um
> **framework de testes** com cobertura mínima de release, e o caminho à **independência** (FFI + backend
> próprio + self-hosting). Ordem = **M.4** (cada fase repousa na anterior). `src/` é canônico.

> **Falha grave que motiva este roadmap.** Hoje `teko <file.tks>` compila **um arquivo isolado** e escolhe
> o parser por *basename* (`src/driver.c`: `tk_compile`/`basename_of`). Isso contradiz o cânone: a Teko
> **compila o projeto inteiro** descrito por um `.tkp`. *(REBOOT_PLAN §2.6, l.758: "O compilador lê o `.tkp`,
> descobre os arquivos e compila o projeto inteiro de uma vez — vê todos os arquivos antes de resolver
> referências. Consequências: sem headers, sem ordem de declaração…".)* Corrigir isso é a **Fase A**, e
> é pré-requisito de quase tudo o que é real (resolução cross-namespace, regra do `main.tks`, dependências).

---

## Eixo A — Compilação por projeto (`.tkp`)

**Cânone:** o `.tkp` é **TOML**, declara `name` (raiz canônica), `artifact` (executable/library), `source`
(raiz do código, invisível nos namespaces), `[dependencies]`, `[aliases]` *(TEKO_LEGISLATION §208–214)*.
Namespace = **diretório**; arquivos no mesmo dir **agregam** numa namespace; `src/` é a raiz `teko::`
*(LEGISLATION §181–188; REBOOT_PLAN §2.6, l.753–756)*. A regra do `main.tks` (executável **exige**,
biblioteca **proíbe**) já existe em Teko: `src/build/tkp_rule.tks` (`check_main_file_rule`).

| # | Entrega | Lei | Estado |
|---|---|---|---|
| A1 | **Leitor `.tkp` (TOML)** — parser TOML mínimo → `Manifest { name, artifact, source, deps, aliases }`. Lido ANTES de qualquer Teko (LEGISLATION: "standalone simple parser"). | M.3 | falta (B1b no BINARY) |
| A2 | **Enumeração do file-set + mapa de namespaces** — varrer a raiz `source`, descobrir `.tks`/`.tkt`, mapear `src/lexer/lexer.tks → teko::lexer`. | M.4 | falta (precisa de `teko::fs` dir-listing — ver Eixo C) |
| A3 | **Modelo de programa multi-arquivo** — agregar TODOS os arquivos num único `tk_program` antes de resolver; visibilidade física (privado=dir, `pub`=projeto, `exp`=outros projetos). | M.1 | parcial (o checker já tipa um `tk_program`; falta a agregação cross-arquivo) |
| A4 | **Regra do `main.tks` fiada** — chamar `check_main_file_rule(artifact, has_main)` a partir do `artifact` do manifesto (NÃO do basename). Aposentar a heurística de basename no driver. | M.3 | falta (a regra existe; falta a fiação) |
| A5 | **Driver por projeto** — `teko build [projdir]` lê o `.tkp` da raiz; sem `.tkp`, erro honesto. (Compilar um `.tks` avulso deixa de ser a entrada normal.) | M.2 | falta |
| A6 | **Pacotes + pré-linker** (deps) — carregar `.tkh`+`.tkb` das dependências e fundir as árvores-tipadas ANTES do codegen (pré-link estático, não FFI — LEGISLATION §215–226). | M.1 | **diferido** (evolução; o codec `.tkb`/`.tkh` já existe) |

> **Resultado do Eixo A:** `teko build` sobre um diretório com `.tkp` compila o projeto inteiro, com
> resolução cross-namespace e a regra do `main.tks` aplicada a partir do artefato. A1–A5 são a semente;
> A6 (deps) é evolução.

### Artefatos & saída (legislador)

**A saída é um DIRETÓRIO, não um arquivo** — um build pode gerar **vários arquivos** (binário nativo, o
`.c` intermediário do transpile, o `.tsym` de símbolos do Eixo E, e, no `pack`, o pacote `.tkh`+`.tkb`).
Portanto:
- **`-o <dir>`** define o **diretório destino** (NUNCA um arquivo único — a saída é multi-arquivo).
- **Sem `-o`:** default = um diretório de saída na raiz do projeto. **A definir** (recomendo `target/`;
  evitar `build/` — colide com o CMake do bootstrap). *(Decisão aberta — ver Decisões.)*
- O diretório é criado se não existir; o nome dos artefatos vem do `name` do manifesto.

**`teko pack <projdir>` — empacotar uma biblioteca.** Quando o `.tkp` é `artifact = library`, `pack`
emite o **pacote Teko**: hoje definido como **`.tkh` (interface) + `.tkb` (árvore tipada / IL)**
*(LEGISLATION §215–226)*. **O que mais vai no pacote além de `.tkh`/`.tkb` é PONTO DE DISCUSSÃO** —
abrir quando A/B/C/D/E estiverem concluídos. Candidatos a debater: metadados/versão do manifesto,
checksum/assinatura (defesa do registry — REBOOT_PLAN l.986), o `.tsym` (símbolos, Eixo E), licença,
lock de dependências. **Nada decidido — só nomeado.**

---

## Eixo B — Framework de testes + portão de cobertura

**Cânone + legislação do usuário:** `.tkt` = testes **junto** ao `.tks`, mesma namespace (enxergam o
privado), marcados por `#test`, compilados no perfil de teste *(REBOOT_PLAN §2 rev.8, l.129–131;
LEGISLATION §186–187)*. Perfis: **debug=VM**, **release=nativo**, **CI roda release** como portão de
qualidade *(REBOOT_PLAN §2.21, l.964–977)*. "P22 Testing + coverage" está listado como **PULL EARLY**
*(REBOOT_PLAN l.1140)* — mas **não há doutrina de cobertura ainda**.

**Legislado pelo usuário (a registrar na LEGISLATION):**
1. **`assert` NÃO é keyword.** *(Já verdade — não há `Assert` no `token.tks`; `assert` é `Ident`. Zero
   mudança no lexer.)*
2. As ferramentas de teste vivem em **namespace próprio**: `teko::assert::is_true | is_false |
   str_contains | …` (e correlatos), injetadas como stdlib (não-importadas, raiz `teko::`).
3. Testes rodam **isolados** (`teko test`). Para produzir **release**, **havendo `.tkt`**, eles **devem
   ser executados** e a Teko impõe um threshold de cobertura (default 80%, configurável, piso 10%).
4. **Testes NÃO são artefatos (legislador).** Um `.tkt` é lexado/parseado/checado como o resto, mas é
   **executado durante o build, no VM/interpretador, sobre a árvore tipada — ANTES de qualquer emissão
   (`.tkb`/`.tkh`) ou codegen nativo** — e depois descartado. **Tests nunca viram `.tkb` nem binário**
   (consistente com "`.tkt` ignorado no release"). Ordem: read→lex→parse→check →
   **[se há `.tkt`: VM roda os testes + mede cobertura → portão]** → emit/codegen. O portão **precede** a
   emissão. → **É por isso que o VM (Eixo D) entra agora**: ele é o motor de execução dos perfis
   **test/debug** (Stage-1 — CONSTITUTION §237–245). O runner/cobertura/portão (B3–B5) são realizados
   **sobre o VM** (ver Eixo D: D2/D3/D4).

| # | Entrega | Lei | Estado |
|---|---|---|---|
| B1 | **Namespace `teko::assert`** — `is_true(bool)`, `is_false(bool)`, `str_contains(hay,needle)` injetados; falha → `teko::panic`. (`equals`/`not_equals`/`is_error`/`is_ok` precisam de genéricos/tipos-resultado — deferidos.) | M.1/M.3 | **✓ feito** |
| B2 | **Migrar os `.tkt`** do `assert` solto → `teko::assert::*`. | M.3 | **✓ feito** (646 asserts em 8 `.tkt`; só `assert_test.tkt` resta) |
| B3 | **Coletor + runner `#test`** → realizado como **D2** (roda no VM, não em nativo). `teko test`. | M.1 | → Eixo D |
| B4 | **Cobertura por LINHA** → realizado como **D3** (o VM conta linhas executadas; `.tkt` fora do denominador). | M.3 | → Eixo D |
| B5 | **Portão pré-emissão** → realizado como **D4** (threshold configurável, piso 10%, default 80%; release executa obrigatório, debug/test permite `--no-test`; gate **antes** de emit/codegen). | M.1 | → Eixo D |

> **Resultado do Eixo B + D:** `teko test` roda a suíte no **VM** (cobertura por linha medida); `teko build
> --release` é barrado, **antes de emitir/codegen**, por testes verdes + cobertura ≥ threshold; debug/test
> permite `--no-test`. `teko::assert` é a superfície canônica; `assert`-solto saiu dos `.tkt`.

---

## Eixo C — Independência + FFI

**Cânone:** FFI = **um opcode** (`OP_CALL_EXTERN`/`OP_SYSCALL`) que os emitters baixam para a convenção da
plataforma — o "fundo OS/FFI" sobre o qual **IO, arena, rede, tempo, threads** viram lib *(REBOOT_PLAN
§7.2, l.1167–1170; `src/core.tks` "a one true intrinsic")*. Teko↔Teko é **pré-link estático de árvore
tipada**, NÃO FFI; **FFI é só para código estrangeiro** na fronteira insegura *(LEGISLATION §215–226)*.
`ptr`/`uptr` são **opacos, só-transporte** *(LEGISLATION §235–240)*. A semente **inclui FFI/syscall** (ler
fonte, escrever saída, `exit`) *(REBOOT_PLAN l.1104–1105)*; a **forma do `extern`** é decisão aberta
*(l.1186–1187)*. Três estágios: **`.tkb` VM → AOT-nativo (LTS, é o que ships) → bare-metal**
*(CONSTITUTION §237–245)*; backend **próprio, sem LLVM** *(REBOOT_PLAN l.974–976)*; **bootstrap em 4
pontos** *(REBOOT_PLAN l.1075–1094)*.

| # | Entrega | Lei | Estado |
|---|---|---|---|
| C1 | **Primitiva FFI/`extern`** — declarar função externa + marshalling na fronteira; `void*`/`ptr` opacos. **Forma sintática = decisão pendente.** | M.0/M.1 | **falta (legislar a forma)** |
| C2 | **Superfícies host sobre FFI** — `teko::env::args`, `teko::exit`, `teko::panic`, `teko::io` (slurp: `read_file`/`write_file`/`write_err`, LEGISLATION §270–282) + **listagem de diretório** (para o Eixo A2) + **process/exec** (invocar `cc` na fase transpile). Destrava `driver.tks`/`main.tks`. | M.4 | parcial (só `read_file` na semente C; resto falta) |
| C3 | **Backend nativo próprio** — emitir direto ao metal + linker próprio, aposentando o `cc` do host (o transpile-para-C é degrau, não o design final). **Plano completo em `TEKO_ROADMAP_NATIVE_BACKEND.md`**; tracked como **ROUND N** (independente/paralelo) em `TEKO_MASTER_PLAN.md` (matriz Linux x86_64/arm64/riscv64 + macOS arm64 + Windows x86_64/arm64 + **Wasm nos dois ambientes (WASI e Browser)**; linker do sistema no M1 (incl. `wasm-ld`), linker próprio como eixo L1–L4 futuro, Wasm fora do L1–L4 por já ter `wasm-ld`). | M.0 | **agendado** (plano fechado 2026-07-01, Wasm/WASI incluído no mesmo dia, escopo estendido a Wasm/Browser no mesmo dia; execução ainda não iniciada) |
| C4 | **Self-hosting** — materializar `codegen.tks` + `driver.tks`; rodar o ciclo de 4 pontos (semente-C → compilador-Teko ger.1 → ger.2==ger.3 bit-a-bit + corretude diferencial); **aposentar o C**. | M.4 | **diferido** (trilha longa) |
| C5 | **Capabilities / sandboxing / auditoria de superfície** (`exp`/`extern`/syscall). | M.1 | **evolução** |

### Backlog nomeado (já levantado, alimenta a independência)
genéricos + constraints · ponteiros de função / `use`-capture / `inject` (DI) · métodos/OOP (override pós-genéricos) · `flags` · pacotes/pré-linker (A6) · concorrência (`ref`+escape) · crypto/TLS (libs finas sobre FFI, **sem** TLS/HTTP nativo). *(Citações: REBOOT_PLAN §3–7, l.1104–1170; TEKO_HISTORY backlog.)* *(O "modo VM" saiu do backlog — promovido ao Eixo D, pois os testes exigem o VM agora.)*

---

## Eixo D — VM / interpretador (perfil test/debug)

**Por que agora:** os testes (Eixo B) **executam no VM sobre a árvore tipada, antes de emit/codegen**, e são
descartados (não viram `.tkb`/binário). O VM é o motor do **sub-perfil de teste** — o **Stage-1** da doutrina
*(CONSTITUTION §237–245: `.tkb`/IL interpretado, o degrau de bootstrap)*.

> **Sub-perfil de teste (legislador):** é uma **fase de VM comum a todos os perfis**, não exclusiva de
> debug/test. **release TAMBÉM roda os testes** — executa o sub-perfil de teste **na VM** (o portão) e só
> **então** segue o pipeline para o codegen nativo. Resumo: **debug** = roda no VM (+ sub-perfil de teste);
> **test** (`teko test`) = só o sub-perfil de teste, isolado; **release** = **sub-perfil de teste na VM →
> portão → codegen nativo**. `--no-test` pula a fase em debug/test, **nunca em release** (havendo `.tkt`).

O VM interpreta a **árvore tipada (`tast`)** diretamente — para testes NÃO há passo de serialização `.tkb`
(o `.tkb` é só para pacotes/deps, A6). O mesmo VM é o motor que o release usa para o portão antes de emitir.

| # | Entrega | Lei | Estado |
|---|---|---|---|
| D1 | **Interpretador da árvore tipada** — avalia `TExpr`/`TStatement`/`TFunction` sobre um modelo de valor (int/bool/`str`/`list`), incluindo os builtins injetados (`print`/`println`/`teko::assert::*`) e os pânicos (÷0/cast/OOB → `teko::panic`). NÃO serializa `.tkb`. | M.0 | **novo** — *pronto (consome `tast`, que existe)* |
| D2 | **Runner sobre o VM** (≙ B3) — coleta `#test`, executa cada uma no VM **junto do código da sua namespace** (multi-arquivo via A3), reporta nome/contagem/pass-fail, **exit ≠0** em falha. `teko test`. | M.1 | dep: D1, A3 |
| D3 | **Cobertura no VM** (≙ B4) — o VM conta **linhas** executadas durante os `#test`; % sobre o código de produção (`.tkt` fora do denominador). | M.3 | dep: D2 |
| D4 | **Portão pré-emissão** (≙ B5) — o driver roda o gate-VM (testes + cobertura) **ANTES** de emit/codegen; release barra em falha/cobertura<threshold (configurável, piso 10%, default 80%); debug/test permite `--no-test`. | M.1 | dep: D3, A1 (threshold no `.tkp`) |

> **Resultado do Eixo D:** o VM existe como motor test/debug; `teko test` roda a suíte interpretada; o
> portão de release passa pelo VM antes de emitir qualquer artefato. O mesmo VM é o degrau para o
> dev-loop interpretado (Stage-1) e para a futura validação diferencial do self-hosting (C4).

---

## Eixo E — símbolos de debug (`.tsym`) + diagnósticos file:line + stack-trace

**Por quê (legislador):** precisamos de **geração de arquivo de símbolos** para evoluir — suporte a
**debugger**, e para **erros/pânicos coletarem stack-trace + file/line**. Cânone: `teko::Error` é um
struct fixo com **`message` + `file` + `line` (compile-time)**; **stack-trace salvo via `.tsym`** (debug
symbols) *(REBOOT_PLAN l.113–114; §"sem stack trace salvo via `.tsym`")*. Hoje `tk_error` carrega só
`message` (sem posição); tokens têm span de texto mas a posição (linha/coluna) não é fiada até os nós.

| # | Entrega | Lei | Estado |
|---|---|---|---|
| E1 | **Posição na pipeline** — fiar `{ file, line, col }` do lexer → tokens → parser → AST → `tast` (cada nó sabe sua origem). | M.3 | **novo** |
| E2 | **`teko::Error` + pânicos com file:line** — `tk_error`/`Error` ganham `file`+`line`; pânicos (÷0/cast/OOB/assert) imprimem `arquivo:linha` (no VM e no nativo). | M.1/M.3 | **novo** (dep E1) |
| E3 | **Emissão `.tsym`** — um arquivo de símbolos (mapa símbolo↔file:line, frames) emitido junto do artefato, para debugger e stack-trace de release. | M.4 | **novo** (dep E1) |
| E4 | **Stack-trace** — frames de chamada carregam origem; pânico imprime a pilha (VM direto; nativo via `.tsym`). | M.1 | **novo** (dep E2, E3) |

> **Resultado do Eixo E:** erros e pânicos apontam `arquivo:linha`; um `.tsym` acompanha o build para
> debugger e stack-trace. Base para o tooling de evolução (language server, depurador).

---

## Correções pendentes (legislador apontou)

- **[A5! projeto-só]** A entrada é **somente projeto** (`.tkp`) — **não há compilação single-file**.
  `teko build|run|test <projdir>` (e `teko <dir>` = build). Remover o fallback single-file do `main.c`
  (`tk_compile`/`tk_run` de arquivo avulso saem da CLI; viram internos da pipeline de projeto). *(Doutrina:
  REBOOT_PLAN §2.6 — compila o projeto, não o arquivo.)*
- **[A5b mirror dos mains]** `main.tks` e `main.c` **desalinharam** — re-espelhar: ambos exprimem a entrada
  **projeto-só** + subcomandos (`build`/`run`/`test`), semanticamente equivalentes (§2.20).
- **[B1! aderência do `teko::assert`]** `src/assert/` precisa do **par co-localizado** `assert.{tks,c,h}`
  (hoje só `assert.tks`; a impl C está em `teko_rt.c`/`vm.c`/`scope.c`). Mover a realização C dos
  `teko::assert::*` para `src/assert/assert.{c,h}` (a casa da namespace; como os testes rodam no **VM**, é o
  VM/assert que a consome) — fechar a brecha de par.

---

## Decisões — ratificadas e abertas

**Ratificadas (legislador):**
- **Sequência:** sem ordem rígida — tudo **segmentado em breadcrumbs** com dependências; agentes pegam crumbs prontos (deps satisfeitas). Ver `## Breadcrumbs`.
- **`teko::assert`:** superfície aprovada — `is_true`, `is_false`, `equals`, `not_equals`, `str_contains`, `is_error`, `is_ok`.
- **Cobertura:** métrica = **linha**; `.tkt` fora do denominador.
- **Portão:** threshold **configurável no `.tkp`, piso 10%, default 80%**; release com `.tkt` **executa obrigatório** (sem `--no-test`); debug/test permite `--no-test`.

**Abertas (a legislar quando o crumb chegar):**
- **F — Forma do `extern`/FFI (C1):** sintaxe da declaração externa + marshalling (`marshall`?) + conversão de `ptr`. *(Cânone em aberto — REBOOT_PLAN l.1186.)* → crumb **C1.0 (legislação)** bloqueia C1.1+.
- ~~`teko build` por projeto (A5)~~ — **resolvido:** entrada projeto-só; `.tks` avulso é rejeitado (não há modo single-file).
- **Diretório de saída default (sem `-o`):** `target/` (recomendado) · `out/` · outro? *(`build/` não — colide com o CMake do bootstrap.)* `-o` é sempre um **diretório**. → crumb **A7**.
- **Conteúdo do pacote (`pack`) além de `.tkh`/`.tkb`:** metadados/versão · checksum/assinatura · `.tsym` · licença · lock de deps? **Discussão diferida** (abrir após A/B/C/D/E). → crumb **A8**.

---

## Caminho crítico
**A1–A5 (projeto/`.tkp`)** → **B1–B3 (assert namespace + runner)** → **C1–C2 (FFI + superfícies host)** →
**B4–B5 (cobertura + portão release)** → **C3 (backend próprio)** → **C4 (self-hosting)**.
A6 (deps/pré-linker) e C5 (capabilities) são evolução. O Eixo A destrava o Eixo C2 (que precisa de `fs`),
que por sua vez destrava `driver.tks`/`main.tks` (self-hosting). F3-pânicos (BINARY) corre em paralelo.

---

## Breadcrumbs (segmentação para agentes)

> Cada crumb = **uma unidade de agente / um sub-PR**. Formato: **[ID] título** — *deps* · *lei* · *par
> (arquivos)* → o quê + **Aceite**. Convenção: todo crumb de código entrega o **par** (`.tks` + semente
> `.c`/`.h`) + `.tkt`. Agentes **rascunham**, eu integro; tensão → **HALT → tribunal**. Um agente pega
> qualquer crumb cujas deps estejam satisfeitas.

### Eixo A — projeto/`.tkp`
- **[A1] ✓ Leitor TOML** — `src/build/manifest.{tks,c,h}` + `manifest_test.tkt`. Feito.
- **[A2] ✓ File-set + mapa de namespaces** — `src/build/discover.{tks,c,h}` + test. Feito (dirent contido; `src/lexer/lexer.tks → teko::lexer`, `src/core.tks → teko`).
- **[A3] Programa multi-arquivo** — deps: A2 · M.1 · par: `src/build/assemble.{tks,c,h}` + test
  > Lexar+parsear cada arquivo, agregar num `tk_program` cross-namespace; visibilidade (privado=dir/`pub`=projeto/`exp`=outros). **Aceite:** projeto de 2+ namespaces compila como um; referência cross-namespace resolve.
- **[A4] Regra do `main.tks` fiada** — deps: A1, A3 · M.3 · par: `driver` + reuso `tkp_rule.tks`
  > `artifact` do manifesto → `check_main_file_rule`; remove o basename-heurístico. **Aceite:** exec sem main → erro; lib com main → erro; válidos passam.
- **[A5] Driver por projeto** — deps: A1–A4 · M.2 · par: `main.{tks,c}` + `driver`
  > `teko build [projdir]` lê o `.tkp`; sem `.tkp` → erro. **Decidir:** `.tks` avulso → `teko check <file>` ou removido. **Aceite:** `teko build` compila o repo; uso atualizado.
- **[A7] Saída em diretório + `-o <dir>`** — deps: A5 · M.2 · par: `driver` + `src/build/output.{tks,c,h}`
  > Default = `<projroot>/target/` (a decidir); `-o <dir>` força o diretório destino (nunca arquivo único — saída multi-arquivo: binário + `.c` + `.tsym` + pacote). Cria o dir; nomeia pelo `name` do manifesto. **Aceite:** `teko build` põe os artefatos em `target/`; `-o /x` redireciona; nome do binário = `name`.
- **[A8] `teko pack` + formato de pacote** — deps: A5, A6, codec `.tkh`/`.tkb` · M.3 · **discussão diferida** · par: `src/build/pack.{tks,c,h}`
  > `teko pack <projdir>` (library) → `.tkh`+`.tkb` no diretório de saída. **Conteúdo além de `.tkh`/`.tkb` = ponto de discussão** (metadados/versão, checksum/assinatura, `.tsym`, licença, lock) — abrir quando os demais eixos fecharem. **Aceite (futuro):** uma lib empacota e outro projeto consome via `[dependencies]` (A6).
- **[A6] Deps + pré-linker** — deps: A5, A8, codec `.tkb`/`.tkh` (existe) · M.1 · **evolução** · par: `src/build/prelink.{tks,c,h}`
  > Carrega `.tkh`+`.tkb` das deps, funde árvores tipadas antes do codegen. **Aceite (futuro):** projeto+1 dep-lib linka estático e checa junto.

### Eixo B — testes/cobertura
- **[B1] ✓ `teko::assert`** — par: `src/assert/assert.tks` + injeção (`scope`) + `runtime/teko_rt`. Feito: `is_true`/`is_false`/`str_contains` (resto deferido — genéricos).
- **[B2] ✓ Migrar os `.tkt`** — feito: 646 asserts em 8 `.tkt` → `teko::assert::*` (só `assert_test.tkt` resta, fecha com D2).
- **B3/B4/B5 → realizados no Eixo D** (runner/cobertura/portão rodam **no VM**, não em nativo). Ver D2/D3/D4.

### Eixo D — VM / interpretador (test/debug)
- **[D1] Interpretador da árvore tipada** — deps: — · M.0 · par: `src/vm/vm.{tks,c,h}` + `vm_test.tkt`
  > Avalia `TExpr`/`TStatement`/`TFunction` sobre um modelo de valor (int/bool/`str`/`list`), incluindo os builtins injetados (`print`/`println`/`teko::assert::*`) e os pânicos (÷0/cast → `teko::panic`). **NÃO** serializa `.tkb`. **Aceite:** interpreta `return 6*7`→42, `print("x")`, `teko::assert::is_true(true/false)` (passa/paniqueia), sem tocar codegen.
- **[D2] Runner `#test` no VM** (≙ B3) — deps: D1, A3 · M.1 · par: `src/build/testrun.{tks,c,h}` + `driver` (`teko test`)
  > Coleta `#test`, interpreta cada uma no VM **junto do código da namespace** (multi-arquivo via A3), reporta, exit≠0 em falha. **Aceite:** `teko test` roda a suíte do repo no VM; falha injetada → exit≠0 + relatório. (Fecha o `assert_test.tkt`.)
- **[D3] Cobertura por linha no VM** (≙ B4) — deps: D2 · M.3 · par: `src/build/coverage.{tks,c,h}` (contadores no VM)
  > O VM conta linhas executadas; % sobre produção (`.tkt` fora do denominador). **Aceite:** relatório determinístico para o repo.
- **[D4] Portão pré-emissão** (≙ B5) — deps: D3, A1 (threshold), A5 (driver) · M.1 · par: `driver` (`build --release`/`--no-test`)
  > Gate-VM (testes + cobertura) **antes** de emit/codegen; release barra em falha/cobertura<threshold; debug/test permite `--no-test`. **Aceite:** abaixo do threshold ou teste vermelho → release barrado antes de emitir; `--no-test` recusado em release.

### Eixo C — independência/FFI
- **[C1.0] LEGISLAR `extern`/FFI** — deps: — · M.0/M.1 · par: `TEKO_LEGISLATION.md`
  > Fixar a forma do `extern`/syscall (declaração, marshalling, `ptr`). **Tribunal.** **Aceite:** cláusula na LEGISLATION; desbloqueia C1.1.
- **[C1.1] Primitiva `extern`** — deps: C1.0 · M.0 · par: `src/codegen` + `runtime`
  > Baixa `extern` para a convenção da plataforma (opcode único). **Aceite:** um `extern` mínimo chama o host e retorna.
- **[C2a] `teko::env` + `exit`** — deps: C1.1 · M.4 · par: `src/env/env.{tks,c,h}` + test → `args()`, `exit(n)`.
- **[C2b] `teko::io` (slurp)** — deps: C1.1 · M.4 · par: `src/io/io.{tks,c,h}` + test → `read_file`/`write_file`/`write_err`.
- **[C2c] `teko::fs` (dir-list)** — deps: C1.1 · M.4 · par: `src/fs/fs.{tks,c,h}` + test → `list_dir` (par Teko de A2).
- **[C2d] `teko::process` (exec)** — deps: C1.1 · M.4 · par: `src/process/process.{tks,c,h}` + test → invocar `cc`.
  > **Aceite (cada C2x):** roda sobre FFI; `.tkt` cobre feliz + erro.
- **[C3] Backend nativo próprio** — deps: C1.1 · M.0 · **agendado** · par: `src/codegen/native/*` (lir/isel/regalloc/enc/obj + `stackify_wasm`/`obj_wasm` + `native_emit`, `src/runtime` inalterado como link target)
  > Emite direto ao metal (+ Wasm), aposenta o `cc` (eventualmente). Plano completo (matriz de alvos, arquitetura de
  > camadas, milestones N1–N8, linker próprio L1–L4 diferido, Wasm via `wasm-ld`) em `TEKO_ROADMAP_NATIVE_BACKEND.md`.
  > **Aceite (M1):** os 6 alvos de CI (`.github/workflows/native.yml`) + Wasm/WASI + Wasm/Browser (2 jobs novos)
  > rodam a suite via objeto nativo, todos os motores concordando (VM==native-C==native-obj, ambos Wasm inclusos;
  > testes de `fs`/`process` no Browser verificam o honest-stop, não rodam de fato).
- **[C4] Self-hosting** — deps: A5, B5, C2*, M1, M2 · M.4 · **diferido**
  > Ciclo 4 pontos (semente-C → Teko ger.1 → ger.2==ger.3 + corretude diferencial); aposenta C. **Aceite (futuro):** ger.2==ger.3 bit-a-bit.
- **[C5] Capabilities/sandboxing** — deps: C1.1 · M.1 · **evolução** — auditoria de superfície `exp`/`extern`/syscall.

### Materialização dos pares C-only (auditoria) — alimenta C4
- **[M1] ✓ `codegen.tks`** — `src/codegen/codegen.tks` espelha `codegen_c.c` 1:1 (incl. str/print + guards F3). Feito.
- **[M2] `driver.tks`** — deps: A5, C2* · M.4 · **bloqueado** em C2 · par: `src/driver.tks`
  > Original Teko do driver (precisa de `fs`/`process`/`io`/`env`/`exit`). **Aceite:** espelha o driver.

### Cross-ref (BINARY)
- **[F3-pânicos] ✓** — `codegen_c.c` + `runtime/teko_rt`: guards ÷0 + conversão impossível. Feito (overflow deferido → build profiles).

### Estado
- **✓ Feitos:** A1, A2, A3, B1, B2, M1, F3-pânicos, D1 (+ integração VM: `teko run`).
- **▶ Prontos agora (deps satisfeitas):** **D2** (runner `#test` no VM — destrava D3/D4, torna os 646 asserts executáveis) · **A4** (regra do main pelo artefato) · **A5!**+**A5b** (projeto-só + mirror dos mains — correções) · **B1!** (aderência do `teko::assert`) · **E1** (posição na pipeline) · **C1.0** (legislar `extern` — você).
- **Em seguida:** A4 → A5! → A5b (Eixo A/entrada); D2 → D3 → D4 (testes no VM); E1 → E2 → {E3,E4} (símbolos/diagnósticos); C1.0 → C1.1 → C2* (FFI/host).
- **Correções a fechar (legislador):** A5! (remover single-file), A5b (mirror mains), B1! (par `assert.{c,h}`).
