---
section: design
created: 2026-07-29
status: BRIEFING DE IMPLEMENTAÇÃO — auto-contido; não é preciso ler o desenho para executar
desenho: docs/design/harness-de-testes-gerado.md (o porquê de tudo o que está aqui)
branch: cargo/0.3.1.0-harness-gerado
---

# Briefing — harness de testes gerado, FATIA 1 (rota C primeiro)

> **RULING DO DONO (2026-07-29), literal — é de PRIORIDADE, não de desenho:**
>
> *"Por isso que precisa adiantar o trabalho de executar testes em paralelo (mesmo que tenha que
> primeiro corrigir a emissão em C / até pq pode quebrar Windows, Mac e wasm), além de implementar em
> Teko nativo."*

> **RULING COMPLEMENTAR (R14, 2026-07-29):** *"1. Compilação"* — a marca "isto é um teste" é de
> **tempo de COMPILAÇÃO**, não um argumento em tempo de execução. Consequência directa para este
> briefing: o sinalizador que o **passo 3** introduz não é só "modo gate" — **é também o interruptor
> da bifurcação de `panic`/`exit` que chega mais tarde.** Ver §A.4.1. A ordem dos cinco passos **não
> muda**; muda o que o passo 3 tem de deixar bem nomeado.

**O antecedente do "por isso", porque é ele que justifica a promoção:** mediu-se que o comportamento
do `defer` sob `panic` **não é testável hoje, por construção** — não existe expect-panic no framework
unitário e um panic mata o binário de testes inteiro; não pode ir para `regressor.tkr` porque
partiria o projecto de regressão do próprio compilador; e um projecto de regressão dedicado gastaria
a 11.ª entrada contra um tecto de 10 que o dono adiou para a .32. **Ninguém podia ter apanhado aquele
bug porque não havia onde escrever o teste que o apanharia.** Este harness é o que fecha isso: com a
captura a INTERCEPTAR em vez de abortar, um `#test` passa a poder afirmar panic sem matar a suíte.

---

## A — A PRIMEIRA FATIA EXECUTÁVEL HOJE

### A.0 Porque a ordem do dono não é só prudência — é mecanicamente obrigatória

O desenho listava "o gate obedecer ao backend" como migalha 1. **Sob esta ordem ela deixa de poder
ser a primeira, e não por risco: por dependência.** Hoje o `main` que chama os `#test` **só existe
dentro do emissor de C** (`emit_test_main`/`emit_test_call`, `src/codegen/codegen.tks`). Mandar o
gate para a rota nativa antes de existir um `main` de gate independente do emissor não troca C por
nativo — **troca gate por nenhum gate**.

Logo a ordem correcta é: **primeiro nasce o `main` sintetizado e a rota C passa a consumi-LO** (é o
"corrigir a emissão em C" do ruling); só depois o backend pode ser escolhido. A razão de risco que o
dono deu — *"pode quebrar Windows, Mac e wasm"* — aponta para o mesmo sítio (§C); a cauda wasm
dessa citação saiu da árvore por ordem dele em 2026-07-30, o risco de Windows e macOS fica.

### A.1 A fatia, por ordem

| # | passo | entrega | ficheiros |
|---|---|---|---|
| **1** | builtin `flush_out` | expõe o `tk_flush_out` que o runtime já tem | `src/checker/scope.tks`, `src/lir/lower.tks`, `src/codegen/codegen.tks` |
| **2** | namespace reservado `teko::__gate` + regra de prefixo `__` | o sítio onde o sintetizador põe o que fonte nenhuma pode chamar | `src/checker/resolve.tks` (ou `scope.tks`), `src/lir/lower.tks` |
| **3** | **`src/build/gate.tks` — o `main` SERIAL sintetizado** | o coração da fatia; AST tipada → AST tipada | **NOVO** `src/build/gate.tks` + 2 linhas em `src/lir/lower.tks` |
| **4** | **a rota C consome o MESMO `main`** | `emit_test_main`/`emit_test_call` saem; a entrada passa a ser o sintetizador | `src/codegen/codegen.tks`, `src/build/project.tks` |
| **5** | **P2 — a prova byte-idêntica** | o portão da fatia: o gate na rota C tem de produzir a MESMA saída de hoje | `scripts/gate_main_golden.sh` (**NOVO**) |

**Só depois desta fatia** é que fazem sentido: o gate obedecer ao backend, a catraca de zero-C, e a
prova diferencial rota×rota.

### A.2 Passo 1 — builtin `flush_out`

**Porquê:** `abort()` não faz flush do `FILE*` com buffer. Medido: um `println` executado antes de um
panic **não aparece**. Sem `flush_out`, o rótulo `test X ... ` de um teste que entra em panic
perde-se, e o diagnóstico morre com o veredicto. `tk_flush_out` já existe em `src/runtime/teko_rt.h`;
falta só ser builtin do checker.

- `src/checker/scope.tks`, em `builtin_fn`: uma entrada `flush_out` com `params = []`, `ret = void`.
  Copiar a forma de `arena_push` (mesma assinatura vazia).
- `src/lir/lower.tks`, em `builtin_io_symbol`: `if last == "flush_out" { return "tk_flush_out" }`.
- `src/codegen/codegen.tks`, na cadeia de builtins bare (onde está `arena_push` → `tk_arena_push`):
  `flush_out` → `tk_flush_out`.

**Fixture:** `flush_out_survives_panic` — um programa que faz `println("MARK")`, `flush_out()`,
`panic("x")`. Espera-se **134 e `MARK` presente em stdout**. Sem a chamada a `flush_out`, `MARK`
desaparece — é essa a prova.

### A.3 Passo 2 — o namespace reservado

**Porquê:** o sintetizador precisa de chamar funções que **nenhum `.tks` pode chamar** (ruling: as
primitivas de teste são *"conhecidas somente pelo compilador"*). O precedente existente faz o
contrário — `builtin_fn` resolve pelo ÚLTIMO segmento, portanto todo builtin injectado é chamável por
nome nu, e o doc-comment de `call_symbol` já regista o preço que isso custou.

Três camadas, nenhuma é convenção:

1. **Não entram em `builtin_fn`.** Um `.tks` que as escreva recebe `unknown function`, mecanicamente.
2. **O caminho de origem é `teko::__gate`**, um namespace **sem módulo**. O sintetizador não passa
   pela resolução de nomes: constrói o `checker::TCall` já resolvido, com `call_ns = "teko::__gate"`.
   `call_symbol` (`src/lir/lower.tks`) ganha uma arm para esse namespace.
3. **Regra de prefixo reservado:** um segmento de caminho começado por `__`, escrito em SOURCE, é
   recusado com diagnóstico próprio. Sem ela, alguém que crie `src/__gate/` colide em silêncio.

**Fixtures:** `gate_reserved_namespace_rejected` (um `.tks` com `use teko::__gate` → 1, com o
diagnóstico do prefixo) e `gate_primitive_not_callable_from_source` (um `.tks` que chama uma função
do namespace → 1, `unknown function`).

### A.4 Passo 3 — `src/build/gate.tks`, o `main` sintetizado

**O quê:** um transform de **AST TIPADA para AST TIPADA**, a correr **depois de check + monomorph** e
**antes do lowering**, cujo produto é consumido pelas DUAS rotas de código.

**A descoberta dos testes** é a mesma travessia que `has_tests` (`src/build/project.tks`) já faz:
`prog.items`, arm `checker::TFunction`, campo `is_test`.

> **CONTRATO QUE NÃO PODE SER QUEBRADO:** o índice do teste é o índice do item em `prog.items`, e é o
> mesmo `idx` que `cov_enter(idx)` usa. **O sintetizador não insere nem remove itens** — só acrescenta
> um `main` no fim. Se inserir, a atribuição de cobertura do filho deixa de casar com a do pai, **em
> silêncio**.

```teko
/**
 * GateShape — a forma do `main` que o sintetizador produz.
 *
 * Existe porque a forma paralela chega numa versão posterior à serial, e as duas têm de sair do
 * MESMO sintetizador: uma forma escolhida por dado é uma peça; duas formas escritas à mão são duas
 * peças que divergem.
 *
 * @since 0.3.1.1
 */
pub type GateShape = enum {
    /** Um `main` que chama cada `#test` em sequência, no fio principal. A forma desta fatia. */
    Serial
    /** Um `main` que lança cada `#test` numa thread e drena os veredictos. NÃO é desta fatia. */
    Threaded
}

/**
 * GatePlan — tudo o que o `main` gerado precisa de saber e que NÃO vem do programa.
 *
 * NENHUM CAMPO PODE DEPENDER DA MÁQUINA QUE COMPILA, e isso é exigência do fixpoint, não
 * preferência: se o grau de paralelismo fosse cozido aqui como literal, o mesmo fonte compilado num
 * host de 4 núcleos e noutro de 64 emitiria bytes diferentes e `gen2 == gen3` partiria por uma razão
 * que nada tem a ver com o compilador. Tudo o que varia com a máquina é lido em TEMPO DE EXECUÇÃO.
 *
 * @since 0.3.1.1
 */
pub type GatePlan = struct {
    /** a forma do `main` a sintetizar; política do compilador, nunca do ambiente. */
    shape: GateShape
}

/**
 * gate_test_indices — os índices, em `prog.items`, de cada função `#test`, por ordem crescente.
 *
 * Extraída do sintetizador porque é a ÚNICA definição de "quais são os testes e por que ordem": o
 * relatório, a contagem de esperados e a atribuição de cobertura leem-na toda daqui.
 *
 * @param prog  o programa verificado
 * @return      os índices dos `#test`, crescentes; vazio quando não há nenhum
 * @since 0.3.1.1
 */
pub fn gate_test_indices(prog: checker::TProgram): []u64

/**
 * gate_program — devolve `prog` com um `main` SINTETIZADO que corre cada `#test`.
 *
 * O `main` sintetizado é a ÚNICA descrição do que um gate faz, e é por isso que é produzido aqui e
 * não no emissor: as duas rotas de código consomem o MESMO `main`, logo uma divergência de veredicto
 * entre rotas é, por construção, uma divergência de GERAÇÃO DE CÓDIGO e nunca do harness.
 *
 * Os itens de `prog` são preservados um a um, na ordem em que chegaram (ver o contrato do índice).
 *
 * @param prog  o programa já verificado e monomorfizado, COM os `#test` dentro
 * @param plan  a forma do `main`
 * @return      o mesmo programa com um `main` acrescentado no fim
 * @throws      quando o programa não declara nenhum `#test`, ou já traz um `main` de gate
 * @since 0.3.1.1
 */
pub fn gate_program(prog: checker::TProgram, plan: GatePlan): checker::TProgram | error
```

**O corpo que o `main` sintetizado tem de produzir** — é o contrato de hoje, na ordem exacta, e P2
compara-o byte a byte:

```
tk_cov_reset · tk_cov_branch_reset · tk_cov_branches_on(true) · tk_cov_line_reset · tk_cov_lines_on(true)
para cada #test, por índice crescente:
    arena_push()
    print("test <ns::name> ... ") · flush_out()
    cov_enter(idx) · <chamada directa ao teste> · cov_leave()
    println("ok")
    arena_pop()
tk_cov_branches_on(false) · tk_cov_lines_on(false) · <dump da cobertura>
```

**Dois ajustes no lowering**, e são duas linhas:

- `src/lir/lower.tks`, `lower_item_function`: hoje faz `if f.is_test { return … }` — **descarta todo
  corpo de teste**. Sob modo gate, deixa de descartar.
  > **AVISO A QUEM IMPLEMENTA:** o backend próprio **nunca** foi exercitado sobre corpos de teste.
  > É risco não medido. Nesta fatia isso não morde (a rota C é que compila), mas não presumas que
  > morde zero quando a fatia seguinte chegar.
- `src/lir/lower.tks`, `lower_virtual_main`: sob modo gate, as statements soltas são descartadas e o
  `main` vem do sintetizador.

**Fixtures:** `gate_test_index_is_item_index` (um projecto com `#test` intercalados com funções de
produção; a atribuição de cobertura de cada teste casa com o índice do item) e
`gate_main_not_in_coverage_denominator` (a percentagem de cobertura de um projecto é idêntica antes e
depois de o `main` sintetizado passar a existir — ele **não** é função de produção e tem de ser
excluído do denominador por proveniência, como `strip_tests` já exclui os `#test`).

#### A.4.1 O SINALIZADOR — nomeia-o bem, porque ele vale mais do que parece

Os dois ajustes acima precisam de um modo que chegue do topo até ao lowering. **Esse modo é também a
marca "isto é um teste" que R14 acaba de fixar**, e que mais tarde decide se `panic`/`exit` bifurcam
para a captura. **É UM sinalizador, não dois.**

O precedente exacto já está no ficheiro e deve ser copiado em vez de inventado — `flat_symbols`:

```teko
pub fn lower_program(prog: checker::TProgram, flat_symbols: bool = false): LModule | error
```

parâmetro de topo com omissão, carregado em `LowerCtx` (`flat_symbols: bool`) e reproduzido em cada
construtor de contexto. A marca faz a mesma viagem:

| camada | papel |
|---|---|
| `src/build/project.tks` | decide que ESTE build é o do gate e passa a marca |
| `src/build/gate.tks` | sintetiza o `main`; a marca viaja AO LADO, não dentro do `GatePlan` — o `GatePlan` descreve a FORMA do `main`, não o modo do compilador |
| `lower_program` | recebe-a como parâmetro de topo, à imagem de `flat_symbols` |
| `LowerCtx` | carrega-a; cada construtor de contexto reproduz o campo |
| `lower_item_function` / `lower_virtual_main` | consomem-na **neste passo** |
| `call_symbol` | consome-a **mais tarde**, para decidir se bifurca `panic`/`exit` |

> **INSTRUÇÃO EXPLÍCITA:** dá-lhe um nome de primeira classe e um doc-comment que diga que ele é
> TAMBÉM o interruptor da bifurcação. **Não um `bool` anónimo chamado `is_gate` enfiado a meio da
> lista de parâmetros.** Se este passo introduzir um sinalizador anónimo, a carga da bifurcação
> inventa um segundo, e passam a existir duas respostas para "isto é um teste?" — que é a doença que
> este repositório já pagou várias vezes.

**Fixture que o fixa desde já:** `nontest_binary_is_byte_identical` — o mesmo projecto SEM `#test`,
compilado antes e depois desta fatia, com os **binários** comparados byte a byte. Com a marca de
compilação, um binário que não é de teste não leva nada de novo, logo a identidade não é uma promessa
a cumprir: **é uma consequência de o código não existir.** Escreve-a agora, mesmo que hoje passe
trivialmente — é ela que protege a garantia quando a bifurcação chegar.

### A.5 Passo 4 — a rota C consome o MESMO `main`

`tk_emit_c_test(prog, cov)` deixa de ter `main` próprio:

- **APAGAR** `emit_test_main` e `emit_test_call` (`src/codegen/codegen.tks`).
- A entrada passa a ser, em espírito, `tk_emit_c_mode(gate_program(prog, plan), CgMode::Program)`.
- `src/build/project.tks`: `run_native_gate` chama o sintetizador antes de emitir.

> **ATÓMICO, e isto é a instrução mais importante do passo:** **substituir, nunca apagar-primeiro.**
> Enquanto `emit_test_main` existir e o sintetizador não estiver ligado, o gate funciona. No instante
> em que se apagar sem ligar, **macOS e Windows ficam sem gate** (§C). O commit tem de trocar os dois
> lados de uma vez.

### A.6 Passo 5 — P2, o portão da fatia

`scripts/gate_main_golden.sh`: constrói o gate na **rota C** com o `main` sintetizado e compara o seu
stdout, **byte a byte**, com o do `main` emitido à mão (capturado ANTES da fatia começar).

**Isto é o produto da fatia.** Se P2 não for byte-idêntico, o sintetizador mudou comportamento e a
fatia não fechou — não se avança para a escolha de backend com uma diferença por explicar.

**Capturar o golden ANTES de tocar em `codegen.tks`.** Depois de apagar `emit_test_main` não há com
que comparar.

---

## B — MAPA DE COLISÕES

| ficheiro | quem lá está | migalhas minhas que lhe tocam | estado |
|---|---|---|---|
| `src/lir/lower.tks` | **`args-native`** + **`degrau-10`** | 1 (`flush_out`), 2 (namespace), 3 (2 linhas de modo gate) | 🔴 **QUENTE — dois agentes** |
| `src/codegen/codegen.tks` | **`defer-nas-saidas`** | 1 (`flush_out`), 4 (apagar `emit_test_main`) | 🔴 **QUENTE** |
| `src/checker/typer.tks` | **`defer-nas-saidas`** | nenhuma nesta fatia | 🟡 evitar |
| `src/build/project.tks` | **`args-native`** | 4 (ligar o sintetizador) | 🟡 **um agente** |
| `src/backend/**` | **`degrau-10`** | nenhuma | 🟢 |
| `src/checker/scope.tks` | ninguém | 1 (`flush_out`) | 🟢 **LIVRE** |
| `src/checker/resolve.tks` | ninguém | 2 (regra de prefixo) | 🟢 **LIVRE** |
| **`src/build/gate.tks`** (NOVO) | ninguém | 3 | 🟢 **LIVRE por construção** |
| `src/process/process.tks` | ninguém | 7 | 🟢 **LIVRE** |
| `src/runtime/teko_rt.{c,h}` | ninguém | 7 | 🟢 **LIVRE** |
| `src/build/regression.tks` | ninguém | 8, 9, 10 | 🟢 **LIVRE** |
| `src/build/regr_group.tks` | ninguém | 9 | 🟢 **LIVRE** |
| `scripts/**` | ninguém | 5, 6 | 🟢 **LIVRE** |

### B.1 O achado que muda o que deve arrancar já

**A fatia 1 é a mais colidida das duas metades; a metade de PROCESSOS não colide com nada.**

- **Livres, arrancam já, zero colisão:** migalhas **7, 8, 9, 10** — a metade de regressões
  (`spawn_redirected`/`wait_one` no runtime + `run_pool` + a morte do andaime de shell + o terceiro
  canal). Tocam em `src/process/`, `src/runtime/`, `src/build/regression.tks`,
  `src/build/regr_group.tks`. **Nenhum desses ficheiros tem outro agente.**
- **Colidem, esperam sequenciamento:** os passos 1, 2, 3 e 4 desta fatia, todos por causa de
  `lower.tks` e `codegen.tks`.

### B.2 Sequenciamento recomendado

1. **`defer-nas-saidas` fecha primeiro.** É pequeno (ligar um predicado que já existe), e liberta
   `codegen.tks` e `typer.tks`. **E é pré-requisito conceptual do harness:** a fatia bifurca
   `panic`/`exit` mais à frente, e bifurcar uma função cujo contrato de `defer` está partido é assentar
   a captura sobre um defeito.
2. **Em paralelo, sem esperar por ninguém:** migalhas 7→8→9→10.
3. **`args-native` e `degrau-10`** libertam `lower.tks`. Enquanto não libertarem, os passos 1–3 desta
   fatia ficam em fila.
4. **Mitigação, se o coordenador quiser arrancar a fatia 1 antes disso:** os toques em `lower.tks`
   são **cirúrgicos e enumeráveis** — uma linha em `builtin_io_symbol`, uma arm em `call_symbol`, e
   duas linhas de modo gate. Podem ser o ÚLTIMO commit da fatia, com os passos 2 e 3 a construírem
   `gate.tks` (ficheiro novo, zero colisão) primeiro.

---

## C — O RISCO DE WINDOWS E macOS, NOMEADO

### C.1 O estado medido

`scripts/ci_producer_matrix.sh`:

| perna | backend do fixpoint |
|---|---|
| `linux-x86_64-glibc`, `linux-x86_64-musl`, `linux-arm64-glibc`, `linux-arm64-musl` | **native** |
| **`macos-arm64`** | **c** |
| **`windows-x86_64`** | **c** |

### C.2 O que exactamente parte, item a item

| # | o que parte | quando |
|---|---|---|
| 1 | **macOS e Windows ficam SEM GATE** | se `emit_test_main`/`emit_test_call` forem apagados antes de o sintetizador estar ligado à rota C. Essas duas pernas correm o gate pela rota C; sem `main` de teste, o binário de gate não tem o que chamar |
| 2 | **macOS e Windows morrem no build do gate** | se o gate passar a escolher o backend antes de a rota C consumir o sintetizador — cairiam na rota nativa, cujos vãos nessas pernas são conhecidos e por fechar: `B1-fp` (a família de vírgula flutuante no x86-64), `B1-args` (passagem por pilha no Windows), o `TimeDateStamp` do PE (que já partiu o fixpoint de Windows na 0.3.0.31) |
| 3 | **a atribuição de cobertura desalinha em silêncio** | se o sintetizador inserir ou remover itens de `prog.items`. Não é específico de plataforma, mas é o único desta lista que **não dá erro** — dá números errados |

### C.3 O teste que prova ANTES de partir

**P2, corrido POR PERNA na matriz de CI — não só localmente.** É a diferença entre "passou na minha
máquina" e "as seis pernas concordam".

| teste | o que afirma | onde corre |
|---|---|---|
| `gate_main_synthesis_is_byte_identical` | o stdout do gate com o `main` sintetizado é byte-idêntico ao do `main` emitido à mão | **as seis pernas**, rota C |
| `gate_backend_resolution_per_leg` | `backend_of()` resolve `Backend::C` em `macos-arm64` e `windows-x86_64`, e `Native` nas quatro Linux | pré-voo, barato, antes de qualquer emissão |
| `gate_main_not_in_coverage_denominator` | a percentagem de cobertura não muda com a chegada do `main` sintetizado | apanha (3), que é o único silencioso |

**A ordem de execução importa:** `gate_backend_resolution_per_leg` é pré-voo e custa milissegundos —
corre primeiro. Se ele falhar, P2 nas seis pernas não vale nada, porque estaria a medir a rota errada.

### C.5 Nota de contexto — o `args()` nativo JÁ foi corrigido

Quando o desenho foi escrito, um binário do backend nativo que lesse a linha de comando **nem
linkava** (`undefined reference to 'teko_args'`). **Isso foi corrigido** — `cargo/0.3.1.0-args-native`
aterrou no vagão principal: o `main` sintetizado recebe `argc`/`argv` e chama `tk_set_args`, e `args`
foi resolvido no `call_symbol`.

**Porque interessa a esta fatia, embora ela não use argv:** o `main` de gate desta fatia é sintetizado
e **não lê argumentos** — nada aqui depende da correcção. O que a correcção muda é que **deixa de
haver um impedimento** a duas alternativas que ficaram registadas como não escolhidas (a
auto-reexecução por selector em `argv[1]`, e a marca por argumento em tempo de execução, que R14
rejeitou a favor da marca de compilação). Nenhuma das duas volta à mesa nesta fatia; ficam aqui só
para que ninguém as reabra a pensar que ainda estão bloqueadas — não estão, apenas não foram
escolhidas.

**Cuidado de colisão que daí resulta:** `cargo/0.3.1.0-args-native` tocou em `src/lir/lower.tks` e
`src/build/project.tks`. Se já aterrou, esses dois ficheiros ficaram **mais livres** do que o mapa de
§B indicava — confirma com o coordenador antes de assumir.

### C.4 A regra que resume

> **Nenhum commit desta fatia pode deixar a árvore num estado em que `macos-arm64` ou
> `windows-x86_64` não tenham gate.** O passo 4 é atómico por essa razão, e P2 nas seis pernas é o
> que o verifica.
