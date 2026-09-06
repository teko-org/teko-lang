---
section: design
created: 2026-07-29
status: DESENHO — nenhuma linha de produto escrita nesta carga
branch: cargo/0.3.1.0-harness-gerado (de remodel/0.3.1.0-linux-native-2 @ ecc0b44)
reconcilia: docs/memory/parallel-test-harness-0.3.2.md, docs/design/concorrencia-adiantada-s8.md,
            docs/design/gate-sem-c-0.3.0.31.md
---

# O harness de testes GERADO — unitários em threads, regressivos em processos

## 0. Os rulings do dono, literais — o desenho não pode divergir deles

**R1 (2026-07-29) — a divisão em duas paralelizações:**

> *"O que o compilador precisa aprender com testes, tanto unitários como regressivos é criar em tempo
> de compilação uma main para produzir um binário executável em teko, essa main deve disparar em
> threads isoladas cada uma das funções #test em paralelo, enquanto que, para as regressões, deve
> executar em paralelo novos processos (dado que o binário dos regressivos já existe)."*

**R2 (2026-07-29) — zero C, e a lane fecha vermelha:**

> *"esse é outro que não pode emitir C quando for compilar nativo, se ao final de uma sessão de testes
> houver um C gerado, tem que fechar vermelha a lane."*

**R3 (2026-07-29) — a via 1 para o veredicto atravessar a fronteira:**

> *"indicar pra ele um stderr, stdin e stdout customizado"*

**R4 (2026-07-29) — a via 2:**

> *"2. implementar canais `chan<T>`"*

**R5 (2026-07-29) — a atribuição de cada via, e o requisito que nasce dela:**

> *"Para os regressivos, que usarão processos, a primeira opção é a certa, para os testes unitários
> que estão no mesmo processo, a segunda seria a certa com uma mudança para capturar exit e panic
> quando executar testes unitários."*

**R0 (2026-07-28), que R3 generaliza e que fica INTEIRO:**

> *"podemos criar um canal de saída próprio, faz sentido: stdout, stderr e um terceiro, que quando
> roda direto, usa o stderr, mas que quando dizemos o canal, ele escreve em outro local."*

**R6 (2026-07-28), a lei de posse entre tarefas:**

> *"sem ref em threads, não só async/await, mas em isolation principalmente"*

**R7 (2026-07-29) — o estado de hoje, e as DUAS primitivas novas:**

> *"exit e panic hoje saem limpo, a única captura seria um 'catch' global que apenas permite tratar
> panico, mas sem interromper o panico, bem como não existe captura de saida, logo, são duas
> primitivas novas mas que devem ser mantidas apenas para rodar os testes e conhecidas somente pelo
> compilador."*

**R9 (2026-07-29) — a grafia, e a hipótese de recovery:**

> *"1. Grafia, prefira `chan<T>` e combina com os outros tipos que são curtos. 2. precisa corrigir,
> ensinar o native"*

> *"a não ser que implementemos a mesma tática de recovery do Go para capturar pânicos"* (era
> HIPÓTESE; foi avaliada em §6.11 e **decidida por R10, abaixo**)

**R12 (2026-07-29) — a regra das CINCO SAÍDAS, e o veredicto de bug. Literal:**

> *"defer, sim, basta olhar o código atual, rodarão pq independem de thread, estão ligadas ao escopo.
> o panic executa após o defer (ao menos deveria, precisa olhar na emissão em C como é construída, e
> o repo tem o teko.c da última versão pra aferir)."*

> *"O que, sinceramente, deveria acontecer, `panic`, `exit`, `return`, `break` e `continue` deveriam
> disparar o defer do escopo sempre, se não estão, temos BUG"*

**Verificado: três dos cinco funcionam, `panic` e `exit` estão PARTIDOS. Diagnóstico em §18.**

**R10 (2026-07-29) — O RULING QUE FECHA A CAPTURA. Literal:**

> *"Certo, não gosto do recover, mas podemos o ter somente para testes (capturar aborts/panic sem
> executá-los), o exit ainda acredito ser necessário, mas, tem uma nuance aqui.*
>
> *se ao compilar um teste, informar que se trata de teste, pode bifurcar as funções globais de exit
> e panic (mantendo as diretas de os intactas), assim consegue capturar somente quando rodar em teste
> com um argumento que só o própio compilador conhece."*

**R13 (2026-07-29) — PROMOÇÃO DE PRIORIDADE, e a ordem. Literal:**

> *"Por isso que precisa adiantar o trabalho de executar testes em paralelo (mesmo que tenha que
> primeiro corrigir a emissão em C / até pq pode quebrar Windows, Mac e wasm), além de implementar em
> Teko nativo."*

O *"por isso"* tem antecedente medido: o comportamento do `defer` sob `panic` (§18) **não era
testável por construção** — não há expect-panic no framework unitário, um panic mata o binário de
testes, e um projecto de regressão dedicado estouraria o tecto de 10 que o dono adiou para a .32.
**Ninguém podia ter apanhado aquele bug porque não havia onde escrever o teste que o apanharia.**

O que R13 muda, e são duas coisas:

1. **A ORDEM.** A migalha 5 (a rota C consome o MESMO `main` sintetizado) sobe de consequência a
   **PRÉ-REQUISITO**. A razão do dono é de risco (macOS/Windows vivem na rota C); e há uma segunda,
   mecânica, que o desenho não tinha explicitado: **o `main` que chama os `#test` só existe hoje
   dentro do emissor de C**, logo mandar o gate para a rota nativa antes de existir um `main`
   independente do emissor não troca C por nativo — troca gate por NENHUM gate. A ordem do dono é
   obrigatória, não apenas prudente.
2. **A PRIORIDADE.** Deixa de ser trabalho a registar e passa a andar **em paralelo com a escada de
   degraus**.

**A fatia executável, o mapa de colisões e o risco por plataforma estão em
`docs/design/harness-briefing-fatia-1.md`** — briefing auto-contido, entregável a um implementador
que não leu este documento.

**R14 (2026-07-29) — A NUANCE DE R10, FECHADA. Literal:**

> *"1. Compilação"* — em resposta à pergunta de §6.11.9 (a marca "isto é um teste" é de tempo de
> COMPILAÇÃO ou de EXECUÇÃO?), e *"Vou na sua recomendação em todos os casos"*.

**Fica a opção (A): marca de TEMPO DE COMPILAÇÃO.** Consequências em §6.11.9.

**R11 (2026-07-29) — `cancel`, e é de OUTRA CATEGORIA: PÚBLICA. Literal:**

> *"o que podemos pensar, e isso vai valer lá na frente quando tivermos async/await, uma função
> global `cancel(error | null)`.*
>
> *O que ela faz?*
>
> *Em uma thread: cancela a thread com uma mensagem de erro.*
>
> *Em um processo: causa panico*
>
> *Sem informar nada (null):*
> *Em thread apenas cancela sem motivo*
> *Em processo causa exit(1)*
>
> *E com isso consegue formar um rulling que até pode (e deve) ser utilizado por outros
> desenvolvedores para interromper todo um fluxo de thread sem derrubar a aplicação/processo."*

Desenhada em **§17**. **NÃO é para implementar nesta lane** — é desenho a registar.

**R8 (2026-07-28), a solução já nomeada para o gate sem C, e que R1/R5/R7 confirmam um ano depois**
(`docs/design/concorrencia-adiantada-s8.md`, §introdução):

> *"O desenho que o owner deu para o gate sem C **é um ISOLATE por `#test`**, com handler próprio de
> exit e panic."*

Deste conjunto sai a tabela que governa tudo o que segue e que não é negociável:

| metade | unidade de execução | via do veredicto | transporte |
|---|---|---|---|
| **unitários (`#test`)** | THREAD no MESMO processo | **via 2** — `chan<T>` | valor TIPADO, sem serializar |
| **regressivos** | PROCESSO novo | **via 1** — descritores próprios | ficheiros que o pai nomeia |

---

## 1. O degrau em que isto cabe, e porquê

| peça | degrau | argumento |
|---|---|---|
| o gate passar a OBEDECER ao backend resolvido (§2) | **0.3.1.0** | é o defeito que a própria lane descobriu; Lei 4 (defeito revelado pela tarefa é corrigido agora, não vira issue) |
| `main` de gate SINTETIZADA, serial, consumida pelas DUAS rotas (§3) | **0.3.1.1** | não precisa de primitiva nova; é o que torna a prova de equivalência ESTRUTURAL |
| metade de PROCESSOS: `spawn`/`wait` + descritores, morte do andaime de `sh` (§5) | **0.3.1.2** | precisa de runtime C mantido, não de primitiva de linguagem |
| metade de THREADS: `cabi fn`, chão de thread, raiz de arena por thread, captura de panic/exit, `chan<T>` (§6) | **0.3.2** | `chan<T>` é SUPERFÍCIE DE LINGUAGEM nova; é carga da linguagem, não do módulo de build |

**A restrição de calendário que ninguém pode perder: a prova de equivalência EXPIRA.** A rota C morre
na 0.3.1.4 e é ela o oráculo (`docs/memory/0.3.1.0-linux-native-first-stop.md`, A REGRA DO ORÁCULO).
Toda a prova diferencial de §10 tem de ser EXECUTADA E ARQUIVADA antes da 0.3.1.4; depois disso não há
com que comparar. Se a metade de threads escorregar para lá da 0.3.1.4, ela nasce sem oráculo — e
então o desenho tem de ser reordenado, não a prova dispensada.

A fórmula de versão fica INTACTA: `A.B.C.D` + `suffix`, o quarto campo é o número de build que o
integrador incrementa. Nada aqui lhe toca.

---

## 2. O defeito que força a carga — e ele é de UMA linha que não existe

`run_native_gate` (`src/build/project.tks`:2818) emite o corpus `#test` como C e chama `run_cc`
**incondicionalmente**. Não lê `TEKO_BACKEND`. O resolvedor existe ao lado dele
(`c_backend_selected`, `project.tks`:1551) e o gate nunca o consulta.

Consequência medida, e registada em `docs/memory/0.3.1.0-linux-native-first-stop.md`: **1030 testes
verdes atravessam a rota C**, e uma miscompilação exclusiva do nativo (a variante DECLARADA a entrar
sempre no primeiro braço) passou por baixo deles sem tocar em nada. O `fixpoint` também não a vê, por
construção — compara dois binários do MESMO compilador errado.

Portanto: **o gate que não lê o backend não é uma ineficiência, é um portão cego.** É a peça 1 e é a
única do documento que não depende de primitiva nenhuma.

E é dela que sai o cumprimento de R2. O gate passa a despachar:

- `Backend::Native` → `.o` + `link_object`, **zero `.c` escrito**;
- `Backend::C` (só sob `TEKO_BACKEND=c`, o seletor em retirada) → o caminho de hoje, que existe
  APENAS como oráculo até 0.3.1.4.

O default é nativo, logo o caminho por omissão não emite C — que é exactamente o que R2 pede.

---

## 3. Descoberta dos `#test` e onde a geração entra no compilador

### 3.1 Como se descobre

Pela mesma travessia que `has_tests` (`project.tks`:2981) já faz: `prog.items`, arm
`checker::TFunction`, campo `is_test`. Nada de tabela nova, nada de registo em tempo de execução.

**O ÍNDICE DO TESTE É O ÍNDICE DO ITEM EM `prog.items`, e isso é contrato.** É o mesmo `idx` que
`cov_enter(idx)` usa hoje, portanto o sintetizador **não insere nem remove itens** — só ACRESCENTA um
`main` no fim. Se inserisse, a atribuição de cobertura do filho deixaria de casar com a do pai, em
silêncio.

Essa ordem é também a ordem do RELATÓRIO (§8). Ela é estável porque a descoberta de fontes já é
ordenada (vagão 20) e porque o sintetizador preserva a ordem de `prog.items` verbatim.

### 3.2 Onde entra

Módulo novo **`src/build/gate.tks`** — o mesmo nome que `concorrencia-adiantada-s8.md` §5.1 já
reservou, para não haver dois. É um transform de **AST TIPADA para AST TIPADA**, a correr **depois de
check + monomorph** e **antes do lowering**, e o seu produto é consumido pelas DUAS rotas.

```teko
/**
 * GateShape — a forma do `main` que o sintetizador produz.
 *
 * Existe porque a metade de threads chega numa versão posterior à metade serial, e as duas têm de
 * sair do MESMO sintetizador: uma forma escolhida por dado é uma peça; duas formas escritas à mão
 * são duas peças que divergem.
 *
 * @since 0.3.1.1
 */
pub type GateShape = enum {
    /** Um `main` que chama cada `#test` em sequência, no fio principal. A forma de arranque. */
    Serial
    /** Um `main` que lança cada `#test` numa thread e drena os veredictos de um `chan<TestVerdict>`. */
    Threaded
}

/**
 * GatePlan — tudo o que o `main` gerado precisa de saber e que NÃO vem do programa.
 *
 * NENHUM CAMPO DEPENDE DA MÁQUINA QUE COMPILA, e isso é uma exigência do fixpoint, não uma
 * preferência (§8.1). O grau de paralelismo e os caminhos de canal são lidos pelo `main` gerado em
 * TEMPO DE EXECUÇÃO, do ambiente; se fossem cozidos aqui como literais, o mesmo fonte compilado num
 * host de 4 núcleos e noutro de 64 emitiria bytes diferentes e `gen2 == gen3` partiria por uma razão
 * que não tem nada a ver com o compilador.
 *
 * @since 0.3.1.1
 */
pub type GatePlan = struct {
    /** a forma do `main` a sintetizar; decidida por POLÍTICA do compilador, nunca pelo ambiente. */
    shape: GateShape
}

/**
 * gate_program — devolve `prog` com um `main` SINTETIZADO que corre cada `#test`.
 *
 * O `main` sintetizado é a ÚNICA descrição do que um gate faz, e é por isso que ele é produzido aqui
 * e não no emissor: as duas rotas de código (a própria e a C, esta até 0.3.1.4) consomem o MESMO
 * `main`, logo uma divergência de veredicto entre rotas é, por construção, uma divergência de GERAÇÃO
 * DE CÓDIGO e nunca de harness. É essa a metade estrutural da prova de §10.
 *
 * Os itens de `prog` são preservados um a um, na ordem em que chegaram: o índice de um `#test` em
 * `prog.items` É o seu índice de cobertura (`cov_enter`) e o seu índice de relatório. Inserir ou
 * remover um item desalinharia as duas atribuições em silêncio.
 *
 * @param prog  o programa já verificado e monomorfizado, COM os `#test` dentro
 * @param plan  a forma do `main`, o grau de paralelismo e os caminhos dos canais
 * @return      o mesmo programa com um `main` acrescentado no fim
 * @throws      quando o programa não declara nenhum `#test`, ou já traz um `main` de gate
 * @since 0.3.1.1
 */
pub fn gate_program(prog: checker::TProgram, plan: GatePlan): checker::TProgram | error

/**
 * gate_test_indices — os índices, em `prog.items`, de cada função `#test`, por ordem crescente.
 *
 * Extraída do sintetizador porque é a ÚNICA definição de "quais são os testes e por que ordem": o
 * relatório do pai, a contagem de esperados (§6.6) e a atribuição de cobertura leem-na toda daqui.
 *
 * @param prog  o programa verificado
 * @return      os índices dos `#test`, crescentes; vazio quando não há nenhum
 * @since 0.3.1.1
 */
pub fn gate_test_indices(prog: checker::TProgram): []u64
```

Dois ajustes de lowering acompanham, ambos já nomeados por `gate-sem-c-0.3.0.31.md` §F2:

- `lower_item_function` (`src/lir/lower.tks`:8186) faz hoje `if f.is_test { return … }` — descarta
  todo corpo de teste. Sob modo gate deixa de descartar. **Consequência a dizer em voz alta: o
  backend próprio NUNCA foi exercitado sobre corpos de teste.** É risco não medido e é a primeira
  sonda da sequência (migalha 0).
- `lower_virtual_main` (`src/lir/lower.tks`:8218) sintetiza `main` a partir das statements soltas;
  sob modo gate, as soltas são descartadas e o `main` vem do sintetizador.

### 3.3 O emissor de C consome o MESMO `main`

`tk_emit_c_test(prog, cov)` deixa de ter um `main` próprio: `emit_test_main`/`emit_test_call`
(`codegen.tks`:10504/10593) são APAGADOS e a entrada passa a ser
`tk_emit_c_mode(gate_program(prog, plan), CgMode::Program)`.

É esta troca que torna a prova de §10 estrutural em vez de empírica. E é ela que respeita o ruling do
dono sobre o oráculo — *"se a versão C passa em um teste que falha nativo, tem que se apoiar no
emissor/codegen"*: quando as duas rotas correm o mesmo `main`, a única variável que sobra é o
`src/codegen/codegen.tks` contra o `src/lir` + `src/backend`, que é exactamente onde ele mandou
apoiar-se.

---

## 4. A LEI DA CASA — uma só, em duas materializações

As duas vias do dono são a mesma lei escrita em dois suportes:

> **A atribuição de um veredicto é pela CASA, nunca pelo FLUXO.** Quem executa nunca imprime; quem
> imprime nunca executa. A casa é identificada pelo ÍNDICE do teste, e o relatório é montado pelo pai
> percorrendo os índices por ordem, depois da barreira.

| | via 1 (processos) | via 2 (threads) |
|---|---|---|
| a casa é | um ficheiro por filho, cujo caminho o pai escolheu | uma posição `[]TestVerdict` indexada por `v.index` |
| o que atravessa | bytes num descritor | um valor de `TestVerdict` COPIADO para a região do recetor |
| a barreira é | `wait` do processo | `join` da thread |
| a ordem do relatório | índice da linha de regressão | `v.index`, nunca a ordem de chegada |

Isto **reconcilia** `docs/memory/parallel-test-harness-0.3.2.md` em vez de o substituir. O que aquele
documento chamou "canal de veredito com fallback para stderr" é a via 1 inteira, e continua válido
palavra por palavra — para os REGRESSIVOS. O que ele resolveu com processos para os unitários é
substituído por threads + `chan<T>`, por ruling directo (R1, R5). As duas correcções ao documento
antigo, e são só duas:

1. *"This language has no threads"* (§ "Why a PATH") deixa de ser premissa: R1 manda criá-las. O
   argumento do PATH continua a valer na via 1 pelos OUTROS dois motivos que ele próprio dá —
   sobrevive ao filho morrer a meio, e continua lá depois.
2. A auto-reexecução (`argv[1]` como selector) **está fora por ESCOLHA, e já não por impedimento.**
   Quando este desenho foi escrito ela era impossível — um binário do backend próprio que lesse a
   linha de comando **nem sequer linkava** (§16, medido). **Esse defeito foi corrigido**
   (`cargo/0.3.1.0-args-native`, já no vagão principal): o `main` nativo recebe `argc`/`argv` e chama
   `tk_set_args`. **O impedimento caiu; a exclusão mantém-se**, agora sustentada só por R1 (threads
   para os unitários).

   **A porta fica identificada:** no dia em que o `args()` nativo funcionar, a auto-reexecução volta a
   estar em cima da mesa, e é uma alternativa REAL à forma escolhida — um binário que se relança a si
   próprio com um selector dá isolamento por PROCESSO por teste, sem `cabi fn`, sem chão de thread e
   sem `chan<T>`. **Não a ressuscito agora** por duas razões: R1 manda threads para os unitários, e o
   desenho não deve depender de uma correcção que está a ser feita ao lado. Mas quem reabrir isto tem
   de saber que a única coisa que a matou foi um defeito em vias de ser corrigido, e não um argumento.
   Nada em §5 ou §6 muda se ela voltar; o que muda é a lista de primitivas bloqueadas, que encolhe.

---

## 5. VIA 1 — os regressivos, processos com descritores próprios

### 5.1 Quem cria os descritores, onde vivem, quem os fecha

O **pai** — o processo do runner — cria os quatro caminhos de uma linha de regressão ANTES de lançar
o filho, e é ele o dono de todos:

```
<prefix>.in     stdin  do filho   (escrito pelo pai antes do spawn)
<prefix>.out    stdout do filho
<prefix>.err    stderr do filho
<prefix>.chan   o TERCEIRO canal (R0/R3), só quando o pai o nomeia
```

**Onde vive o descritor, já que as arenas fecham ao fim do escopo:** não vive numa arena nenhuma. O
que a arena guarda é o *caminho* (um `str`), e o caminho é um valor do pai, criado antes do spawn e
vivo até à colheita. O descritor propriamente dito é uma alça do SISTEMA OPERATIVO, aberta dentro do
`spawn` e fechada por ele: o pai abre, duplica para os slots do filho, e fecha a sua cópia
imediatamente a seguir ao spawn — o filho fica com as dele. Nada atravessa a fronteira de arena
porque nada de arena atravessa a fronteira de processo.

**Quem fecha:** o `spawn` fecha as cópias do pai (senão o `.out` nunca vê EOF e um leitor futuro
bloqueia); o filho fecha as dele ao terminar, por acção do SO; o pai lê os ficheiros DEPOIS do `wait`.
Ninguém drena nada em concorrência, que é a propriedade que dispensa `select`/`poll` — e é o segundo
motivo, ainda de pé, para o canal ser um PATH e não um fd.

### 5.2 A superfície que falta, nomeada

Hoje `src/process/process.tks` tem exactamente duas funções, ambas SÍNCRONAS
(`run`, `run_quiet`), e nenhuma aceita redirecção escolhida pelo chamador — `run_quiet` redirige para
o dispositivo nulo, mas por dentro (`tk_rt_run_quiet`, `teko_rt.c`:1808, `_dup`/`_dup2` à volta do
`_spawnvp`). `src/io/io.tks` só tem ficheiro inteiro (`read_file`/`write_file`) e os quatro
escritores de stream padrão. **Não existe nenhuma peça de redirecção por processo filho exposta a
Teko.** É primitiva em falta e é a primeira da lista de §12.

```teko
/**
 * ProcHandle — uma alça de processo filho lançado e ainda não colhido.
 *
 * Nunca é um pid nu: no Windows o que se colhe é uma HANDLE e no POSIX um pid, e um `i64` cru
 * chamado `pid` em ambos seria mentira num deles. `TK_RT_SPAWN_FAILED` (-1) continua a ser o único
 * valor negativo e continua a significar "nenhum filho chegou a correr".
 *
 * @since 0.3.1.2
 */
pub type ProcHandle = struct {
    /** o valor opaco que o host devolveu; negativo significa que nenhum filho arrancou. */
    raw: i64
}

/**
 * spawn_redirected — lança `argv` SEM esperar por ele, com os três descritores padrão apontados aos
 * caminhos que o chamador escolheu.
 *
 * É esta função que materializa o ruling R3: o pai nomeia o destino, o filho escreve lá. Um caminho
 * vazio herda o descritor do pai, que é o que faz um `spawn_redirected` sem redirecção nenhuma ter
 * exactamente o comportamento de `run` sem a espera.
 *
 * `dir` é a directoria de trabalho do FILHO (o que o antigo `cd <dir> && ` fazia por shell) e `env`
 * são tokens `K=V` que se juntam ao ambiente herdado, sem tocar no ambiente do pai — a mesma
 * propriedade que `env_prefix_cmd` garantia por citação de shell.
 *
 * @param argv      o vector de argumentos; `argv[0]` é o executável
 * @param dir       a directoria de trabalho do filho ("" = a do pai)
 * @param env       tokens `K=V` acrescentados ao ambiente do filho ([] = herdar apenas)
 * @param in_path   o ficheiro ligado ao stdin do filho ("" = herdar)
 * @param out_path  o ficheiro ligado ao stdout do filho ("" = herdar)
 * @param err_path  o ficheiro ligado ao stderr do filho ("" = herdar)
 * @return          a alça do filho, ou uma alça com `raw` negativo quando nenhum filho arrancou
 * @since 0.3.1.2
 */
pub fn spawn_redirected(argv: []str, dir: str, env: []str, in_path: str, out_path: str, err_path: str): ProcHandle

/**
 * wait_one — bloqueia até `h` terminar e devolve o estado com que terminou.
 *
 * O estado é o do FILHO, com a mesma leitura que `run` já documenta: 0–255 para quem saiu, 128+signo
 * para quem foi morto por sinal no POSIX, e o código de saída no Windows, que não tem sinais. Um
 * número nunca identifica um panic — o panic identifica-se na sua linha de stderr
 * (`TK_PANIC_MARKER`).
 *
 * @param h  a alça devolvida por `spawn_redirected`
 * @return   o estado do filho, ou `TK_RT_SPAWN_FAILED` quando a alça nunca correspondeu a um filho
 * @since 0.3.1.2
 */
pub fn wait_one(h: ProcHandle): i32
```

O chão em C fica em `src/runtime/teko_rt.c` — o **C mantido**, a excepção explícita da lei Teko-only,
e o mesmo sítio onde `tk_rt_run` já faz `fork`/`execvp`/`waitpid` numa chamada só. Partir em dois
**não acrescenta uma syscall**: separa a espera.

### 5.3 Linux, macOS e Windows — o que muda e o que não

| | POSIX (Linux, macOS) | Windows |
|---|---|---|
| lançar | `fork` + `dup2` dos três fds + `execvp` (ou `posix_spawn` com `file_actions_adddup2`) | `CreateProcess` com `STARTUPINFO.hStdInput/hStdOutput/hStdError` e `bInheritHandle` |
| colher | `waitpid` | `WaitForSingleObject` + `GetExitCodeProcess` |
| **quantos slots de descritor há** | três, por convenção | **três, por estrutura** — `STARTUPINFO` tem exactamente esses campos |
| o terceiro canal | um PATH em `TEKO_VERDICT_CHANNEL` | o MESMO path, na mesma variável |

**Não existe um quarto stream padrão em nenhuma das duas plataformas**, e é por isso que o terceiro
canal é um CAMINHO e não um descritor: o `--status-fd` do GnuPG e o
`AnonymousPipeServerStream.GetClientHandleAsString()` do .NET são a mesma ideia com fichas
diferentes — o pai nomeia o destino e diz-lho por fora. Um caminho é a ficha que ambas as plataformas
já sabem ler, sobrevive ao filho morrer a meio, e fica lá para ser lido depois. (Correcção honesta ao
que o dono lembrou: o .NET não tem um quarto stream; tem uma maneira de PASSAR a alça. A forma
portável é sempre "o pai nomeia".)

**O fallback de R0 fica inteiro e é o que evita que o protocolo seja obrigatório:** sem
`TEKO_VERDICT_CHANNEL` no ambiente, o veredicto vai para **stderr**. Corre-se um binário de teste à
mão e vê-se o veredicto, sem flag nenhuma.

### 5.4 O que morre, e é muito

`sh_squote`, `sh_join`, `to_sh_path`, `windows_path_to_sh`, `windows_drive_to_mount`,
`captured_sh_cmd`, `cd_prefix_cmd`, `env_prefix_cmd`, `regr_batch_script`, `regr_batch_rc`,
`regr_rc_of_text`, `REGR_RC_SUFFIX`, `REGR_BATCH_UNREADABLE_RC` — a camada inteira de shell
(`src/build/regression.tks`:180–450). Existia porque o `sh` era o escalonador; com um lançador
próprio, deixa de haver `sh` para citar.

**E o ficheiro `.rc` morre com ela, por um motivo que é preciso dizer para não voltar:**
`parallel-test-harness-0.3.2.md` listou "o canal de veredito" como perigo nº1 porque um filho que
chama `exit(3)` legitimamente é indistinguível de um que falhou com 3. **Esse perigo é da metade
UNITÁRIA, não da regressiva:** numa regressão o código de saída ESPERADO é o contrato escrito no
`.tkr`, portanto o estado de saída É o veredicto e não há ambiguidade nenhuma a resolver. O `.rc`
existia por outra razão, puramente de shell — o estado de um filho em segundo plano não se lê do `$?`
depois do `wait`. Com `wait_one` a devolver o estado, o ficheiro não tem função.

O `.chan` sobrevive à morte do `.rc` para o que ele é realmente bom: **transportar o que um código de
saída não cabe** — a linha de panic, o índice em voo quando o filho morreu, e a imagem de cobertura.

### 5.5 O pool

```teko
/**
 * ProcPool — o lançador limitado da metade de regressões: mantém no máximo `jobs` filhos em voo,
 * colhe-os por ordem de ÍNDICE e devolve uma captura por linha, na ordem em que as linhas foram
 * dadas.
 *
 * A ordem de colheita não é a ordem de término, de propósito: ordenar por término é ordenar por
 * tempo, e uma saída ordenada por tempo não reproduz a corrida que a produziu.
 *
 * @since 0.3.1.2
 */
pub type ProcPool = struct {
    /** quantos filhos podem estar em voo ao mesmo tempo. */
    jobs: u64
    /** as alças em voo, indexadas pela linha a que pertencem. */
    inflight: []ProcHandle
}

/**
 * run_pool — corre `specs` em processos paralelos, no máximo `jobs` ao mesmo tempo, e devolve uma
 * captura por linha, na ORDEM DAS LINHAS.
 *
 * Substitui `run_captured_batch` inteira, com a mesma assinatura de intenção e sem uma linha de
 * shell. A janela é DESLIZANTE (colhe-se um, lança-se o seguinte), e não uma barreira a cada `jobs`
 * como o script de `sh` era obrigado a ser por falta de um `wait -n` portável: a cauda de cada lote
 * deixa de ser desperdiçada.
 *
 * @param specs  o que correr, uma entrada por linha de regressão
 * @param jobs   quantos filhos podem estar em voo (`regr_jobs()`)
 * @return       uma captura por linha, na ordem de `specs`
 * @since 0.3.1.2
 */
pub fn run_pool(specs: []ProcSpec, jobs: u64): []CapResult
```

`CapResult` fica com a forma que tem hoje (`exit`/`stdout`/`stderr`/`harness_ns`/`child_ns`/`cmd`), e
GANHA uma medição honesta que hoje não existe: com um `spawn` e um `wait` por filho, `child_ns` deixa
de ser a divisão do relógio do lote por N (`regr_batch_capture`, que documenta essa divisão como
partilha e não como medição) e passa a ser o intervalo real daquele filho.

---

## 6. VIA 2 — os unitários, threads no mesmo processo, `chan<T>`

### 6.1 A assinatura

Três palavras novas na linguagem, e apenas três. Ficam registadas contra o
`TEKO_MASTER_PLAN.md`:262, que proíbe congelar as cinco primitivas reservadas *"until parser + real
duplication data exist"*: **o dado existe agora e é o ruling R4 do dono**, que nomeia `chan<T>`
directamente e nomeia o caso de uso. `scope{}` e a forma de `spawn` como palavra-chave continuam
reservadas — nada aqui as exige.

```teko
/**
 * chan — um canal tipado de capacidade fixa entre threads do mesmo processo.
 *
 * O canal é criado pelo RECETOR e as suas casas vivem na região do recetor (§6.3). Um `send` COPIA
 * o valor para dentro dessa região; nenhum ponteiro para a região de quem envia atravessa a
 * fronteira, o que é o que torna o canal compatível com a lei R6 (sem `ref` entre threads) e com
 * arenas que fecham ao fim do escopo.
 *
 * @param T  o tipo transportado; tem de ser COPIÁVEL POR VALOR (§6.3)
 * @since 0.3.2
 */
pub type chan<T>

/**
 * chan_new — cria um canal com `cap` casas, na região de quem chama.
 *
 * Quem chama é o RECETOR, e essa não é uma convenção: as casas ficam na região do chamador, portanto
 * o canal não pode sobreviver a quem o criou, e quem o criou é quem vai drená-lo.
 *
 * `cap` é FINITA e obrigatória. Um canal sem limite trocaria um bloqueio visível por um crescimento
 * invisível de memória, que é o modo de falha que este projecto menos consegue ver.
 *
 * @param cap  quantas casas o canal tem; tem de ser maior que zero
 * @return     o canal, ou o erro quando `cap` é zero ou a região não consegue alojá-lo
 * @throws     quando `cap` é zero
 * @since 0.3.2
 */
pub fn chan_new<T>(cap: u64): chan<T> | error

/**
 * send — copia `value` para a próxima casa livre do canal e publica-a.
 *
 * BLOQUEIA enquanto não houver casa livre. A cópia acontece ANTES da publicação, e a publicação é um
 * passo único: um emissor que morra a meio da cópia não deixa uma casa meio-escrita visível ao
 * recetor — deixa uma casa não publicada, que é indistinguível de nunca ter enviado. É esta ordem
 * que faz a escolha de posse aguentar o panic (§6.5).
 *
 * @param c      o canal
 * @param value  o valor a transportar; é COPIADO, nunca referenciado
 * @return       nada em sucesso
 * @throws       quando o canal já foi fechado
 * @since 0.3.2
 */
pub fn send<T>(c: chan<T>, value: T): null | error

/**
 * recv — retira o valor publicado mais antigo, bloqueando enquanto não houver nenhum.
 *
 * Devolve `null` quando o canal está FECHADO e vazio — o "acabou" que distingue um canal esgotado de
 * um canal que ainda vai receber (§6.4). Devolve `error` só quando o canal nunca poderá progredir.
 *
 * @param c  o canal
 * @return   o valor mais antigo, ou `null` quando o canal está fechado e vazio
 * @since 0.3.2
 */
pub fn recv<T>(c: chan<T>): T | null

/**
 * chan_close — declara que nenhum `send` mais acontecerá neste canal.
 *
 * Fecha-o o RECETOR, e só ele (§6.4). Um `send` num canal fechado é `error`, nunca silêncio.
 *
 * @param c  o canal
 * @return   nada
 * @since 0.3.2
 */
pub fn chan_close<T>(c: chan<T>)
```

### 6.2 O tipo transportado — o veredicto de um teste é um VALOR, não texto

```teko
/**
 * TestState — o que aconteceu a um `#test`, como facto e não como texto.
 *
 * Os estados são distintos NA ORIGEM, não por inspecção da mensagem: `Failed` é depositado por
 * `teko::assert`, `Panicked` pelo choke point de panic e `Exited` pelo de saída (§6.5). Distinguir
 * um do outro por prefixo de mensagem seria um trocadilho que parte no dia em que alguém escrever um
 * `panic("assertion failed: …")` à mão.
 *
 * @since 0.3.2
 */
pub type TestState = enum {
    /** o corpo do teste retornou normalmente. */
    Ok
    /** uma asserção de `teko::assert` falhou. */
    Failed
    /** o corpo entrou em `panic` (deliberado ou de guarda de runtime). */
    Panicked
    /** o corpo chamou `exit(code)`; `code` fica em `TestVerdict.code`. */
    Exited
    /** a thread desapareceu sem depositar nada — o caso silencioso, e é VERMELHO (§6.6). */
    Vanished
    /** o teste foi planeado e nunca chegou a arrancar (falha de criação de thread). */
    NotRun
}

/**
 * TestVerdict — tudo o que o pai precisa de saber sobre UM teste, num valor auto-contido.
 *
 * Auto-contido é a palavra que carrega o desenho: cada campo é um escalar ou um `str`, e o `send`
 * copia-os todos em profundidade para a região do recetor. Não há aqui nem uma referência, nem um
 * agregado aninhado, nem um fecho — porque nenhum deles sobreviveria ao fecho da arena da thread
 * emissora.
 *
 * @since 0.3.2
 */
pub type TestVerdict = struct {
    /** o índice do `#test` em `prog.items` — a CASA, e a chave de ordenação do relatório. */
    index: u64
    /** o nome qualificado do teste, tal como o rótulo o imprime. */
    name: str
    /** o que aconteceu. */
    state: TestState
    /** o código de `exit`, quando `state` é `Exited`; 0 nos restantes. */
    code: i32
    /** a mensagem da asserção ou do panic; "" quando o teste passou. */
    message: str
    /** o sítio (ficheiro:linha:coluna) que produziu a mensagem; "" quando não há. */
    site: str
    /** quanto tempo o corpo demorou, em nanossegundos monotónicos. */
    elapsed_ns: i64
    /** o que o corpo escreveu no seu stdout, capturado e replicado pelo pai por ordem. */
    out_text: str
    /** o que o corpo escreveu no seu stderr. */
    err_text: str
}
```

### 6.3 A DECISÃO CENTRAL — o `send` COPIA para a região do RECETOR

Três formas possíveis. A lei desempata sem consultar ninguém.

| forma | veredicto |
|---|---|
| **o canal guarda um ponteiro para o valor na arena do EMISSOR** | **REJEITADA.** As arenas fecham ao fim do escopo; a thread que envia morre logo a seguir; o recetor lê memória libertada. Sob panic é pior ainda: a arena fecha ANTES de o recetor chegar lá. |
| **o `send` MOVE o valor** | **REJEITADA.** Mover exige provar que o emissor não volta a tocar no valor — análise de posse linear que não existe. O `Spine` (`src/checker/spine.tks`) é hoje uma query pura que *"nothing consumes for a decision yet"*. Construir posse linear para entregar um harness é inverter a ordem do trabalho. E não sobrevive ao panic: um emissor que morre a meio de um move deixa um valor que nem é dele nem é do canal. |
| **o canal tem casas na região do RECETOR e o `send` COPIA para lá** | **ESCOLHIDA.** |

Os quatro argumentos, por ordem de força:

1. **Aguenta o panic, que é o teste que o coordenador impôs.** A cópia termina antes da publicação, e
   a publicação é um passo. Uma thread morta a meio de um `send` deixa uma casa não publicada; uma
   morta depois deixa um veredicto completo em memória que NÃO é a dela. Em nenhum dos dois casos o
   fecho da arena da thread toca no que o recetor vai ler.
2. **Não inventa lei de posse nova.** A lei já existe e é a mesma que a via 1 usa: *o que atravessa
   uma fronteira, atravessa por CÓPIA para memória que o outro lado já possui.* Uma lei, duas vias.
3. **Respeita R6 literalmente.** Nenhuma `ref` atravessa. O que a thread recebe é o canal, cujo
   interior ela nunca nomeia: ela chama `send` e o `send` copia.
4. **Torna a capacidade uma decisão de MEMÓRIA e não de sorte.** `cap` casas × `sizeof(T)` + o texto
   copiado é um limite superior que o pai escolheu.

**O preço, dito em voz alta:** `T` tem de ser COPIÁVEL POR VALOR — escalares, `str`, e structs
planas desses. Fecho, interface, referência e agregado com ponteiro interior são **recusa de
compilação**, não cópia rasa silenciosa. Uma cópia rasa de um fecho aliasaria o ambiente capturado, e
esse ambiente está na arena que vai fechar. É uma paragem honesta na checagem, com a lista de tipos
recusados escrita no diagnóstico.

**Quanto texto se copia:** `message`/`site`/`out_text`/`err_text` são `str`, portanto de comprimento
variável. A cópia é limitada por `TEST_TEXT_CAP` (um `const`, não um número mágico) e o excesso é
truncado com uma marca visível — porque um veredicto que faz o canal crescer sem limite é um harness
que morre de memória a relatar falhas, que é o pior momento possível para morrer.

### 6.4 Bloqueio, capacidade, fecho

- **Capacidade:** `cap = lanes + 1`. Uma casa por thread em voo, mais uma, para que uma thread nunca
  bloqueie no `send` à espera do pai — o pai drena entre lançamentos. `cap` finita é obrigatória
  (§6.1).
- **Bloqueio:** `send` bloqueia com o canal cheio; `recv` bloqueia com o canal vazio e não fechado.
- **Quem fecha:** o **RECETOR**, e só ele, depois de ter feito `join` a todas as threads que lançou.
  Nessa altura, por definição, não pode existir mais nenhum emissor. Um emissor a fechar o canal seria
  a corrida clássica ("quem fecha por último?") e aqui ela nem se põe, porque o recetor é único e a
  barreira é dele.
- **Como o recetor distingue "acabou" de "ainda vem mais":** NÃO é pelo fecho, e esta é a parte que
  interessa. O recetor sabe quantos testes despachou; ele conta. `recv` devolver `null` (canal fechado
  e vazio) é a confirmação, não a fonte. A fonte é o **balanço de esperados contra recebidos** (§6.6).
- **Se o recetor morrer antes de ler:** o processo inteiro morreu, portanto não há ninguém a quem
  responder. É o caso de §6.7, e resolve-se um nível acima, na fronteira de PROCESSO.
- **Se um emissor morrer antes de enviar:** §6.6. É o caso perigoso e tem resposta própria.

### 6.5 A CAPTURA DE `exit` E DE `panic` — as DUAS PRIMITIVAS NOVAS (R5, R7)

#### 6.5.1 Como está hoje, medido — e o `catch` global NÃO serve

```
src/runtime/teko_rt.h:611   _Noreturn void tk_panic(const char *msg);
src/runtime/teko_rt.h:614   _Noreturn void tk_panic_str(tk_str msg);
src/runtime/teko_rt.h:615   _Noreturn void tk_exit(int32_t code);
src/lir/lower.tks (call_symbol)   "panic" -> "tk_panic_str"   ·   "exit" -> "tk_exit"
```

`tk_panic_str` escreve `teko: deliberate panic: <msg>` em stderr, imprime backtrace e chama `abort()`
— SIGABRT, exit 134. `tk_exit` faz `tk_regions_free_all` e sai. **Os dois são `_Noreturn` e os dois
matam o PROCESSO.** É exactamente o que R7 diz por medição do dono — *"exit e panic hoje saem
limpo"* — e é o que `concorrencia-adiantada-s8.md` §4.1 já tinha registado.

**O "catch global" de hoje, localizado e julgado.** Ele existe e é `tk_rt_crash_handler`
(`src/runtime/teko_rt.c`:116), instalado por um `__attribute__((constructor))` sobre `SIGSEGV`,
`SIGBUS`, `SIGILL` e `SIGFPE`:

```c
static void tk_rt_crash_handler(int sig) {
    fputs("\nteko: FATAL signal — a generated program crashed (M.1).\n", stderr);
    tk_backtrace();
    _Exit(128 + sig);   // async-signal-safe
}
```

**Ele TRATA e não INTERROMPE** — imprime, e depois `_Exit`. É a descrição de R7 palavra por palavra,
e é por isso que **não pode ser a base**: observar não é conter. O mecanismo novo nasce **ao lado**
dele (o `crash_handler` continua a ser a última rede, para a falta de hardware que nenhum handler
Teko alcança, §6.6), e nunca por cima.

**Captura de saída não existe de todo.** Não há antecedente nenhum: `tk_exit` só sai.

#### 6.5.2 As duas primitivas — nome, assinatura, semântica

R7 fixa que são **duas primitivas novas**, e o desenho entrega exactamente duas CAPACIDADES:

| # | primitiva | o que contém |
|---|---|---|
| **P-A** | **captura de panic** | um `panic` levantado dentro de um corpo de teste guardado deposita `{mensagem, sítio}` e termina **a thread**, não o processo |
| **P-B** | **captura de saída** | um `exit(code)` levantado dentro de um corpo de teste guardado deposita `{code}` e termina **a thread**, não o processo |

**As duas são ARMADAS PELO MESMO PAR, e essa é uma decisão com argumento.** Um corpo de teste está
guardado ou não está; armá-las separadamente não tem caso de uso, e dois pares dobram as maneiras de
deixar um deles por desarmar — que é precisamente a falha que
`concorrencia-adiantada-s8.md` §4.2 já nomeou para `guard_lane`/`unguard_lane` (*"uma raia que retorna
sem desguardar deixa entrada morta na tabela, o que a próxima varredura leria como veredicto de outra
raia"*) e para a qual já reservou a fixture `thread_lane_unguard_pairs`. **Se o dono quis dizer dois
pontos de armar separados, é uma linha de mudança** — a semântica das duas capacidades não muda.

O par é o `guard_lane`/`unguard_lane` daquele desenho, com o nome ajustado ao que ele agora faz (não
guarda uma raia longa: guarda UM teste, porque R1 manda uma thread por `#test`):

```teko
/**
 * gate_guard_begin — arma, para a thread chamadora, a captura de panic (P-A) e a captura de saída
 * (P-B) do teste `index`.
 *
 * Enquanto armado, um `panic` ou um `exit` dentro deste corpo NÃO mata o processo: deposita o motivo
 * na linha desta thread na TABELA DE GUARDAS e termina a thread. Fora de qualquer guarda, `panic` e
 * `exit` comportam-se EXACTAMENTE como hoje, byte a byte — o exit 134 e a linha `TK_PANIC_MARKER`
 * são contrato afirmado por golden, e trocar o mecanismo sem trocar a saída é a única forma honesta
 * de fazer esta migração.
 *
 * O reconhecimento é por `sys_thread_self()` contra a tabela, e NÃO por armazenamento local de
 * thread: a linguagem não tem TLS, a tabela tem no máximo `lanes` linhas — uma dezena — e uma
 * varredura linear sobre ela custa menos do que introduzir TLS na linguagem para um caso que ainda
 * não mediu precisar. É a mesma escolha, e pelo mesmo motivo, que
 * `concorrencia-adiantada-s8.md` §4.2 já tinha ratificado.
 *
 * @param index  o índice do `#test` que esta thread vai correr — a CASA do veredicto
 * @return       nada
 * @since 0.3.2
 */
fn gate_guard_begin(index: u64)

/**
 * gate_guard_end — desarma a guarda da thread chamadora: o caminho normal de saída de um teste que
 * terminou sem panic e sem `exit`.
 *
 * O par é OBRIGATÓRIO. Uma thread que retorna sem desarmar deixa uma linha morta na tabela, e a
 * varredura seguinte leria essa linha como o veredicto de outra thread. Afirmado por
 * `thread_lane_unguard_pairs`.
 *
 * @return  nada
 * @since 0.3.2
 */
fn gate_guard_end()
```

**O que a guarda deposita, e onde.** A linha da tabela é `{thread_id, index, state, code, message,
site}` de largura FIXA, e a **tabela vive na região do PAI**, alojada antes de a primeira thread
nascer. Não pode viver na região da thread que morre — essa arena vai fechar. A ordem é obrigatória e
está no doc-comment: **copiar para a linha da tabela → fechar a região da thread → terminar a
thread.** Invertida, o `str` da mensagem é lido de memória já libertada.

#### 6.5.3 Onde vivem — Teko, com zero C novo

Três sítios possíveis, e a lei escolhe:

| sítio | veredicto |
|---|---|
| **na `main` gerada** | **IMPOSSÍVEL.** A `main` é quem CHAMA; `panic` não retorna. Não há ponto de retoma. |
| **em `teko_rt.c`, remendando `tk_panic_str`/`tk_exit`** | **REJEITADO**, e não por mim: `concorrencia-adiantada-s8.md` §4.2 já o recusou — *"é a direção errada: o romaneio já condena `teko_rt.c` à morte"*. E R7 diz que estas primitivas *"nascem no caminho nativo"*. |
| **em Teko, `src/runtime/teko_rt.tks`, alcançadas por uma arm de `call_symbol`** | **É AQUI.** É o mecanismo do desenho anterior, inteiro. |

Concretamente, o que `concorrencia-adiantada-s8.md` §4.2 já desenhou e que este documento **adopta
sem alterar**: `call_symbol` (`src/lir/lower.tks`) mapeia builtin → símbolo por tabela literal —

```
if last == "exit"  { return "tk_exit" }
if last == "panic" { return "tk_panic_str" }
```

— e esses dois destinos passam a ser funções **em Teko**, que implementam o comportamento guardado e
só tocam o host, no caminho NÃO-guardado, por `extern fn` para `write` e `abort` (o padrão que
`examples/probes/arena_bottom` provou). Resultado: o handler passa a ser Teko, `tk_panic_str` fica
órfão e sai junto com o resto do C. **Zero C novo, e o romaneio avança em vez de ser contornado.**

Divergência que registo por honestidade: a §6.5.2 da PRIMEIRA redacção deste documento propunha
remendar `teko_rt.c` com `_Thread_local`. **Estava errada e foi substituída** — divergia do desenho
anterior por desconhecimento, não por razão, e R7 (*"nascem no caminho nativo"*) fecha a questão.
O que sobrevive daquela análise é só o achado de §6.7 sobre a CLASSE DE ARMAZENAMENTO da raiz de
arena, que é outro assunto e continua a ser uma medição por fazer.

#### 6.5.4 "Conhecidas somente pelo compilador" — o mecanismo, não a intenção

Esta é a metade de R7 mais fácil de violar por acidente, e por isso é a que leva mais texto.

**O precedente existe e é EXACTAMENTE o oposto do que precisamos — e é preciso dizê-lo.** O
compilador já injecta funções que nenhum `.tks` declara: `builtin_fn` (`src/checker/scope.tks`:503+)
resolve `print`, `cov_enter`, `arena_push`, `intern_get`, … **pelo ÚLTIMO SEGMENTO do caminho**, o
que significa que qualquer `.tks` pode chamá-las por nome nu. Não são secretas; são globais sem
declaração. O doc-comment de `call_symbol` regista o preço que isso já custou: *"a user function whose
bare name happened to match a builtin (`arena_push`, `one_byte`, …) was silently emitted as the
RUNTIME symbol"*. **Portanto seguir esse precedente violaria R7.** Segui-lo seria pôr as duas
primitivas ao alcance de qualquer ficheiro.

O mecanismo que cumpre R7 tem três camadas, e nenhuma é uma convenção:

1. **Não entram em `builtin_fn`.** Um `.tks` que escreva `gate_guard_begin(0)` recebe
   `unknown function` do checker, pela mesma via por que qualquer nome inexistente o recebe. Isto é
   mecânico: a ausência de uma linha numa tabela.
2. **O seu caminho de origem é um NAMESPACE RESERVADO SEM MÓDULO — `teko::__gate`.** O sintetizador
   não passa pela resolução de nomes: constrói o `checker::TCall` já resolvido, com
   `call_ns = "teko::__gate"`, e `call_symbol` ganha uma arm para esse namespace. Um `.tks` não
   consegue produzir esse nó: não existe `src/__gate/`, portanto `use teko::__gate` não resolve.
3. **E uma REGRA DE PREFIXO RESERVADO no checker fecha o buraco que a camada 2 deixaria:** um
   segmento de caminho que comece por `__` é recusado em SOURCE, com diagnóstico próprio. Sem esta
   regra, alguém que criasse `src/__gate/` colidiria com o namespace sintetizado — e colidiria em
   silêncio, que é o modo de falha desta casa.

**O que não pode vazar, item a item:**

| superfície | vaza? | porquê |
|---|---|---|
| mensagens de erro do checker | **não** | o checker nunca as resolve, portanto nunca as nomeia |
| traço de panic (`.tsym`) | **não, POR CONSTRUÇÃO** | são emitidas SEM entrada na tabela `.tsym`; `tk_tsym_resolve` (`teko_rt.c`) só acrescenta um nome Teko quando o conhece, e não conhece estas. Um traço mostra o frame do runtime, nunca um nome Teko que não existe em ficheiro nenhum |
| cobertura | **não, e este é o que quase vaza** | a `main` sintetizada e as guardas NÃO são funções de produção. Têm de ser excluídas do DENOMINADOR (`count_prod_fns`, `src/coverage/coverage.tks`:557) por PROVENIÊNCIA, do mesmo modo que `strip_tests`/`filter_tkt` já excluem os `#test`. Sem isso, a percentagem de cobertura muda no dia em que o gate pousa, sem que uma linha de código de produção tenha mudado — e muda em silêncio |
| `teko --help`, documentação gerada, `.tsym` de release | **não** | só existem sob `GateShape`, isto é, só no binário de gate; o binário de release nunca as contém |

#### 6.5.5 `panic` × asserção falhada — distinguidos NA ORIGEM

Hoje `teko::assert::is_true` chama `panic("assertion failed: is_true")` (`src/assert/assert.tks`:21),
portanto uma asserção falhada É um panic e a única diferença é o texto. **Distinguir por texto é um
trocadilho** e parte no dia em que alguém escrever esse prefixo à mão.

A correcção é de UMA linha por asserção: `teko::assert::*` passa a chamar um choke point próprio.

```teko
/**
 * assert_fail — o ponto único por onde uma asserção falhada termina o teste corrente.
 *
 * Distinto de `panic` DE PROPÓSITO e na origem: uma asserção falhada é `TestState::Failed`, um panic
 * é `TestState::Panicked`, e o pai nunca precisa de inspeccionar a mensagem para saber qual foi.
 *
 * Fora de um teste guardado, o comportamento é byte-idêntico ao `panic` de hoje — a mesma linha
 * `TK_PANIC_MARKER`, o mesmo exit 134 — porque essa saída é contrato afirmado por golden.
 *
 * @param message  a mensagem da asserção que falhou
 * @param site     o ficheiro:linha:coluna da asserção
 * @return         nada; a chamada não retorna nem para o teste nem para o processo
 * @since 0.3.2
 */
pub fn assert_fail(message: str, site: str)
```

#### 6.5.6 `exit` dentro de um teste — o RECORD distingue, a POLÍTICA decide

O dono nomeou a diferença: *"o panic é uma falha do teste, o `exit` pode ser o teste a testar
precisamente que algo sai com um código."* O desenho separa as duas perguntas:

- **O REGISTO distingue sempre:** `state = Exited`, `code = <o código>`. Nunca se perde qual foi.
- **A POLÍTICA, hoje, é VERMELHA.** O contrato de um `#test` é "passa se retornar normalmente"; um
  teste que termina o processo não retornou. Verde por omissão seria transformar um acidente
  (código sob teste que chama `exit`) num teste que passa sem ninguém reparar.
- **O gancho para o inverter fica NOMEADO e não desenhado:** um atributo `#test(expect_exit = N)`
  é a superfície natural para um teste que quer AFIRMAR uma saída. Não é inventado aqui porque
  nenhuma falha medida o exige ainda, e o `MASTER_PLAN` manda não congelar o que não precisa.

### 6.6 A thread que morre sem escrever nada — o caso perigoso

Depois de capturados o panic e o `exit`, sobra o desaparecimento puro: SIGSEGV, SIGBUS, divisão
inteira por zero ao nível da CPU, `abort()` de dentro da libc. Nenhum handler Teko corre antes disso.

**A regra, e ela é estrutural e não depende de temporizador nenhum:**

> O pai sabe, ANTES de lançar seja o que for, o conjunto exacto de índices que vai despachar
> (`gate_test_indices`). Ele aloja `[]TestVerdict` com uma casa por índice, todas inicializadas a
> `TestState::Vanished`. Cada veredicto drenado do canal SUBSTITUI a casa do seu `index`. Quando a
> barreira fecha, **toda a casa que continua `Vanished` é VERMELHA e é impressa como tal, nomeando o
> teste.**

Silêncio não pode contar como verde, e aqui não conta: o verde exige uma escrita positiva. A
contagem esperados-contra-recebidos é a mesma regra dita por outras palavras, e o relatório imprime as
duas (`N testes, M veredictos recebidos`) porque um desencontro é ele próprio um diagnóstico.

**Porque não há temporizador:** um `recv` com timeout transforma um teste lento numa falha
intermitente, que é o formato de falha que este projecto menos consegue depurar. O limite é o `join`:
o recetor não drena para sempre, drena até ter feito `join` a todas as threads que lançou. Uma thread
que trava para o `join` — e isso é um bloqueio real, não um silêncio, com o binário todo parado, o
que é visível. O limite de tempo pertence ao passo de CI que corre o binário, não à primitiva.

**E a recuperação do que se perdeu, que é o que a via 1 traz de volta para dentro da via 2:** antes de
lançar o índice `i`, o `main` gerado escreve a linha *"iniciei `i`"* no **canal de veredicto**
(§5.1/§5.3 — um caminho, um ficheiro, o mesmo mecanismo de R0). Se o processo INTEIRO morrer, o pai —
que é o processo do compilador, um nível acima — lê o ficheiro, sabe exactamente que índices estavam
em voo, e **reexecuta apenas esses, com `lanes = 1`**. Cada um recebe então o seu veredicto próprio.
Custo pago só quando há queda; "relata tudo" preservado; nenhum handler de sinal.

### 6.7 As arenas, sob panic

Três regiões, três donos, e a distinção é o que faz o desenho aguentar:

| região | dono | quando fecha | o que lá vive |
|---|---|---|---|
| a do PAI (o fio principal) | o processo | no fim do gate | o `[]TestVerdict`, as casas do canal, as áreas de estágio |
| a de CADA THREAD | a thread | quando a thread termina, normal OU por panic | tudo o que o corpo do teste alojou |
| — | — | — | — |

Hoje a raiz é uma só e sem alça: `static tk_region *tk_g_root` (`teko_rt.c`:940), e `tk_arena_push`/
`tk_arena_pop` empilham marcas num vector global (`static tk_arena_mark tk_arena_marks[64]`,
`teko_rt.c`:1341). Duas threads a fazer isso concorrentemente rebobinam o ponteiro de bump uma sobre a
outra, **e o sintoma é NENHUM** — o próprio cabeçalho do runtime regista que reuso de arena é
invisível ao ASan. É o perigo silencioso nº1 de `concorrencia-adiantada-s8.md` §8.1.

**Achado desta carga, e ele estreita muito o pré-requisito.** Aquele documento classificou "raiz de
região por tarefa" como crumb caro e bloqueante (C4), presumindo um redesenho de API. A leitura do
código diz outra coisa: `tk_g_root`, `tk_g_regs`, `tk_arena_marks` e o índice de marca são
**variáveis com classe de armazenamento estática**, e as assinaturas de `tk_region_root()`,
`tk_arena_push()` e `tk_arena_pop()` **não têm parâmetro nenhum a acrescentar** — passam a ser por
thread mudando a CLASSE DE ARMAZENAMENTO (`_Thread_local` no POSIX, `__declspec(thread)` no MSVC).
Nenhum chamador muda. **Isto é uma hipótese a MEDIR (migalha 0d), não um facto**, porque a lista de
globais do runtime é maior do que estas quatro (a tabela de internamento, as posições de diagnóstico
de cast, os sinks de cobertura) e cada uma tem de ser julgada por si. Mas se se confirmar, o
pré-requisito mais caro do desenho anterior custa uma linha por variável.

**Quem fecha a arena de uma thread que entrou em panic:** o guarda, na ordem de §6.5.3 — copiar o
motivo para o estágio do PAI, depois `tk_regions_free_all` **daquela thread**, depois terminar a
thread. E o veredicto que já foi para o canal sobrevive porque **nunca esteve nessa região**: está
nas casas do canal, que são do recetor. Era este o teste que a escolha de §6.3 tinha de passar.

### 6.8 As duas vias coexistem — e não, a leitura simples não se aguenta inteira

A leitura do coordenador — *descritores para regressões, `chan<T>` para unitários* — está **certa
como atribuição** e é agora ruling (R5). O que ela não cobre, e é preciso dizer:

**A metade unitária precisa das DUAS.** `chan<T>` transporta o veredicto entre THREADS; mas o binário
de gate é ele próprio um processo filho do compilador, e a fronteira compilador↔binário-de-gate é uma
fronteira de PROCESSO. Por ela atravessam: o registo "iniciei o índice i" que salva a corrida quando o
processo morre (§6.6), a imagem de cobertura (§7) e o veredicto final. Essa travessia é via 1.

Portanto: **`chan<T>` é o transporte de dentro do binário de gate; os descritores são o transporte
entre o compilador e o binário de gate.** Não competem, encaixam. A regressão usa via 1 nas duas
fronteiras porque só tem uma.

### 6.9 O harness nasce na via 1 e migra para a via 2 sem se escrever duas vezes?

**Sim, e é essa a razão de `GateShape` existir (§3.2).**

- **`GateShape::Serial`** (0.3.1.1) não usa canal nenhum: um só fio, os veredictos vão para o mesmo
  `[]TestVerdict` indexado por `index`, e o relatório é impresso pela MESMA função.
- **`GateShape::Threaded`** (0.3.2) troca o sítio onde a casa é preenchida — em vez de o corpo do
  `main` escrever directamente, uma thread envia e o `main` drena — e **mais nada muda**: o mesmo
  `TestVerdict`, o mesmo `[]TestVerdict`, o mesmo relatório, a mesma ordenação, o mesmo balanço de
  esperados.

A condição para isto se aguentar é escrever a metade serial **contra o tipo `TestVerdict` desde o
primeiro dia**, e nunca contra um `print` directo. É a única disciplina que a migalha 4 tem de
respeitar, e é o que impede o harness de ser escrito duas vezes.

Aviso de forma, medido: **não usar uma interface para abstrair o sumidouro.** O caminho nativo tem uma
paragem conhecida e por fechar em `fat-pointer interface-dispatch result not yet lowered`. O
sintetizador escolhe a forma; não há despacho dinâmico nenhum.

### 6.10 A OBJECÇÃO ANTERIOR AO CANAL — citada, e respondida

*(A citação abaixo está VERBATIM, com a grafia `channel<T>` que o documento original usava. A grafia
foi decidida depois — é `chan<T>`, §6.12 — e citar o original com o token de hoje seria falsificar a
citação. É a mesma disciplina com que §17 cola a saída do linker sem a limpar.)*

`concorrencia-adiantada-s8.md` §3.3 deixou o canal deliberadamente DE FORA, e o argumento é
concreto. Literal:

> *"**`channel<T>` merece nota própria, porque a análise mudou o desenho.** Nenhum dos três ganhos que
> o owner nomeou precisa de canal: gate, codegen e regressor são todos **fork-join sobre um intervalo
> de índices, com escrita disjunta e leitura após barreira**. Canal é a primitiva de comunicação
> *durante* a execução, e comunicação durante a execução é precisamente o que introduz ordem
> dependente de tempo. Congelar `channel<T>` agora seria congelar a peça que os casos reais não usam
> — e a única que ameaça o determinismo. Fica reservada com uma razão escrita, não por omissão."*

**O dono decidiu o contrário (R4, R5) e a decisão dele vale.** Mas a objecção não desaparece por
haver ruling: ela nomeia um perigo REAL — *ordem dependente de tempo* — e o documento tem de dizer
como é que ele não morde. A resposta tem três partes e nenhuma é disciplina de programador.

**(a) O canal transporta, não ordena.** A ordem em que `recv` devolve valores É a ordem do
escalonador e é não-determinística. O desenho nunca a usa: o recetor drena para
`verdicts[v.index]` — a CASA — e imprime percorrendo `0..count`. É a regra 4 de §8. **A saída é a
mesma com 1 thread e com 16 porque a ordem de impressão não é a ordem de execução**, exactamente o
mesmo argumento com que §6.1 daquele documento defendeu o fork-join.

**(b) O canal não é "comunicação DURANTE a execução".** A objecção visa o padrão em que raias
conversam entre si e uma decisão depende de quem falou primeiro. Aqui não há nada disso: os
emissores nunca leem do canal, nunca se veem uns aos outros, e o único recetor só age **depois da
barreira de `join`**. É fork-join com um transporte diferente — a topologia é a mesma que a objecção
aprovou; só a casa deixou de ser um endereço cru e passou a ser um valor tipado.

**(c) O que o canal COMPRA e que a casa crua não compra**, e é por isso que a troca não é neutra:
com escrita disjunta em memória partilhada, o veredicto de um teste que entra em `panic` teria de ser
escrito **pela thread moribunda** num bloco do pai, o que obriga a raciocinar sobre uma escrita a
meio de uma morte. Com o canal, a publicação é um passo único e anterior à morte (§6.3): ou o
veredicto foi publicado inteiro, ou não foi publicado — e "não foi publicado" tem resposta própria e
vermelha (§6.6). **A escolha do dono é a que aguenta o panic melhor**, e essa é a prova real, não a
elegância.

**O que continua verdade da objecção, e fica como guarda:** um `chan<T>` cuja ordem de `recv` fosse
usada para QUALQUER decisão — ordenar saída, atribuir cobertura, escolher o primeiro erro a reportar
— partiria o fixpoint e os goldens. Por isso a regra é escrita como proibição e não como conselho:
**a ordem de chegada ao `recv` não pode influenciar nenhum byte de saída.** É afirmada por
`gate_lanes_output_identical` e por `gate_coverage_lanes_identical`.

### 6.11 O recovery do Go — MEDIDO, e DECIDIDO: fica FORA. A captura é INTERCEPÇÃO (R10).

> *"a não ser que implementemos a mesma tática de recovery do Go para capturar pânicos"* — dono,
> 2026-07-29. Era hipótese; foi medida (§6.11.1–§6.11.5) e **decidida por R10** (§6.11.6–§6.11.10).
>
> **O veredicto, em uma linha:** *"não gosto do recover, mas podemos o ter somente para testes
> (capturar aborts/panic **sem executá-los**)"* — e "sem executá-los" é o que dispensa o
> desenrolador por inteiro. A medição abaixo fica porque é ela que sustenta o veredicto: sem ela,
> "não vale a pena" seria opinião.

**A ideia é boa e o encaixe aparente é melhor do que parece.** Em Go, `recover()` só funciona dentro
de uma função DIFERIDA, e o Teko **já tem `defer`** — é o único bloco com escopo que a linguagem tem
(não há `try`, não há `catch`, não há `scope`). Logo a táctica não pediria palavra-chave nova: o bloco
que trata já existe e o `recover` seria uma função, o que casa com R7 ("conhecidas somente pelo
compilador"). E casa com o modelo de teste do próprio Go — cada teste corre na sua goroutine e o
framework faz `recover`, que é literalmente o problema deste documento.

Por isso a hipótese merecia medição em vez de opinião. Foi medida.

#### 6.11.1 Os `defer` correm quando há panic? — A PRIMEIRA MEDIÇÃO ESTAVA CONFUNDIDA

**Fica registada com o erro à vista, e não apagada**, porque uma medição errada com a razão do erro
escrita vale mais do que silêncio — e evita que o próximo agente refaça a sonda pelo mesmo caminho.

A primeira sonda marcava os pontos com `println` (**stdout, BUFFERIZADO**):

| caso | rota C | nativo |
|---|---|---|
| `fail = false` (controlo) | `BEFORE` · `DEFER RAN` · exit **7** | idem |
| `fail = true` | só a linha de panic · exit **134** — **nem `BEFORE` nem `DEFER RAN`** | idem |

E no mesmo relatório registei, como achado lateral, que **`abort()` não faz flush do `FILE*`** — por
isso o `BEFORE`, que comprovadamente CORREU, também não apareceu.

**Esse achado destrói a conclusão principal, e eu não o apliquei a ela.** Se o `abort()` engole a
saída de código que comprovadamente correu, a ausência de `DEFER RAN` **não distingue** "o defer não
correu" de "o defer correu e a linha morreu no buffer". **Medi a saída, não a execução.**

#### 6.11.1b A REMEDIÇÃO — instrumento que sobrevive ao `abort()`, e o C emitido

O instrumento certo já estava à mão na própria sonda: a linha de panic APARECIA, e ela sai por
`ewrite` → **stderr, não-bufferizado**. Sonda reescrita com `eprintln` nos dois pontos:

```teko
pub fn body(fail: bool): i32 {
    defer { eprintln("DEFER RAN") }
    eprintln("BEFORE")
    if fail { panic("probe") }
    7
}
```

```
=== COM panic, marcas por STDERR, rota C ===
BEFORE
teko: deliberate panic: probe
--- exit=134

=== COM panic, marcas por STDERR, NATIVO ===
BEFORE
teko: deliberate panic: probe
--- exit=134

=== CONTROLO (sem panic), rota C ===
BEFORE
DEFER RAN
--- exit=7
```

**Agora o confundidor está removido:** `BEFORE` sai por stderr e APARECE, provando que o canal
sobrevive ao `abort()`; `DEFER RAN` sai pelo MESMO canal e está genuinamente AUSENTE. **O `defer`
não correu.** A conclusão anterior estava certa; a evidência é que não estava.

**E a prova documental, que dispensa sondas — o C EMITIDO.** (Leitura de artefacto, não geração: a
sonda foi compilada em scratchpad e o seu `.c` LIDO; nada gerado foi commitado. `bootstrap/teko.c`
não serve para esta pergunta por um facto que vale a pena registar — **o compilador não usa `defer`
em lado nenhum do seu próprio código**: `grep 'defer {' src/**/*.tks` só devolve menções em
comentários e no AST.)

```c
int32_t teko_deferprobe__body(bool fail) {
    tk_eprintln((tk_str) { (const tk_byte *)"BEFORE", 6 });
    if (fail) {
        tk_panic_str((tk_str) { (const tk_byte *)"probe", 5 });
    } tk_eprintln((tk_str) { (const tk_byte *)"DEFER RAN", 9 });
    return ((int64_t)7ULL);
}
```

**Está tudo aqui, em cinco linhas.** O corpo do `defer` **É emitido** — inline, na SAÍDA LÉXICA (a
queda pelo fim), imediatamente antes do `return`. E está **DEPOIS** do `if (fail) { tk_panic_str(...)
}`. Como `tk_panic_str` é `_Noreturn`, no caminho de panic o controlo nunca chega lá. Não é que o
`defer` não exista: **é que o panic salta por cima dele por nunca regressar.**

O emissor é `emit_defers` (`src/codegen/codegen.tks`), chamado nas saídas léxicas — e **em nenhum
sítio no caminho de uma chamada divergente**.

#### 6.11.1c O QUE O DONO TEM DE CERTO, e o que a medição corrige

> *"defer, sim, basta olhar o código atual, rodarão pq independem de thread, estão ligadas ao escopo.
> o panic executa após o defer (ao menos deveria …)"* — dono, 2026-07-29

| afirmação | veredicto |
|---|---|
| *"independem de thread"* | **CERTO.** O `defer` é resolvido em tempo de compilação; não há estado de thread envolvido. Nada no desenho de threads o afecta — e isto é load-bearing para `cancel` (§17.4) |
| *"estão ligadas ao escopo"* | **CERTO, e é literalmente o que o C mostra** — o corpo aparece na saída léxica do escopo |
| *"o panic executa após o defer"* | **NÃO é o que acontece.** O C emite o corpo do `defer` DEPOIS do sítio da chamada de panic, e o panic não regressa. O `defer` fica inalcançável nesse caminho |
| *"ao menos deveria"* | **pergunta legítima; a resposta é PARCIAL — §6.11.1d** |

#### 6.11.1d "Deveria"? — **SIM, e é BUG.** O dono decidiu; o diagnóstico está em §18

O dono respondeu à sua própria pergunta no mesmo dia:

> *"O que, sinceramente, deveria acontecer, `panic`, `exit`, `return`, `break` e `continue` deveriam
> disparar o defer do escopo sempre, se não estão, temos BUG"*

Verifiquei os cinco: **três funcionam, dois estão partidos**, e os dois partidos são exactamente os
que são CHAMADAS em vez de palavras-chave. **A matriz medida, a causa no código e a sequência de
correcção estão em §18.**

*(Uma posição minha que retiro: eu tinha recomendado NÃO mexer no `panic`, argumentando que correr os
`defer` só na forma explícita seria "meio-limpo". Esse argumento é fraco aqui e está retirado em
§18.4 — quando o processo morre, o SO reclama o que ficou por fechar. Ele só vale quando o processo
SOBREVIVE, que é o caso do `cancel`, não este.)*

#### 6.11.1e O achado lateral, que sobrevive intacto e agora está bem fundado

`abort()` **não faz flush do `FILE*` com buffer** — provado pelo contraste directo entre as duas
sondas: com `println` (stdout) o `BEFORE` desaparece; com `eprintln` (stderr) aparece. Isto **mede** a
necessidade do builtin `flush_out` (migalha 3), que até aqui era nota herdada de
`gate-sem-c-0.3.0.31.md` §2.2(d): sem ele, tudo o que um teste escreveu em stdout antes de entrar em
panic **perde-se**, e o diagnóstico morre com o veredicto.

#### 6.11.2 Porque não corre — o `defer` do Teko é ESTÁTICO e LÉXICO, e o panic não é uma saída léxica

**A formulação certa, que a primeira redacção não tinha:** não é que o Teko "não tenha `defer` sob
panic". É que **o `defer` só conhece SAÍDAS LÉXICAS, e uma chamada divergente não é uma delas.** O
corpo está lá, emitido; o panic é que nunca regressa para o alcançar.

`replay_defers` (`src/lir/lower.tks`) di-lo no próprio doc-comment:

> *"fire every deferred body … by lowering each body's statements straight-line into the CURRENT
> block … its OWN `.defers` stack is READ (**not shrunk — firing is a symbolic replay over
> already-lowered source structure, not a runtime pop**)"*

Ou seja: **os corpos de `defer` são INLINE em cada saída LÉXICA de escopo, em tempo de compilação.**
O backend C faz o mesmo (`emit_defers`, `codegen.tks`), e o C de §6.11.1b mostra-o linha a linha.
E há três confirmações independentes de que o modelo é deliberadamente estático:

- **não existe registo de `defer` no runtime** — `grep -in defer src/runtime/teko_rt.{c,h}` devolve
  três ocorrências e as três são a palavra inglesa "deferred" em comentários sobre outras coisas;
- **o checker PROÍBE `return`/`break`/`continue`/`defer` dentro de um corpo de `defer`**
  (`typer.tks`), precisamente para que o replay simbólico nunca reentre;
- `panic` é hoje uma função **Teko** (`src/runtime/teko_rt.tks`) cujo corpo é
  `ewrite(marcador ~ msg)` seguido de `rt_abort()` — `extern fn ... = "abort" from "c"`. Escreve e
  aborta. Não há sítio por onde um desenrolamento pudesse acontecer.

#### 6.11.3 As duas vias, lado a lado

| | **recovery à Go** | **guarda por thread (§6.5)** |
|---|---|---|
| **o que é preciso construir** | (1) **pilha de `defer` em TEMPO DE EXECUÇÃO** — cada `defer` passa a EMPILHAR um registo chamável em vez de ser inline, o que obriga o corpo do `defer` a virar um valor chamável com ambiente capturado (isto é, depende de `cabi fn`/thunks, que também não existem); (2) **um desenrolador** — ou DWARF/`.eh_frame` + CFI emitidos pelo backend, ou `setjmp`/`longjmp` com um buffer por frame guardado; (3) **um protocolo de retoma** — o frame que faz `recover` tem de RETORNAR normalmente a partir de um ponto de desenrolamento arbitrário, o que o backend tem de saber sintetizar | (1) o par `gate_guard_begin`/`gate_guard_end`; (2) a tabela de guardas na região do pai; (3) uma bifurcação em `panic`/`exit`. O "desenrolamento" é `sys_thread_exit` — **fornecido pela plataforma** |
| **o backend emite CFI hoje?** | **NÃO.** O próprio linker o diz na sonda de §16: `binn/argvprobe.o: missing .note.GNU-stack section implies executable stack`. Um backend que ainda não emite `.note.GNU-stack` está longe de emitir `.eh_frame` correcto | irrelevante — não precisa |
| **muda a superfície visível ao utilizador?** | **MUITO.** `defer` deixa de ser gratuito (passa a custar um push por execução); a semântica de `defer` sob panic muda para toda a gente; `recover()` passa a ser capacidade da linguagem, usável por qualquer `.tks`; um programa passa a poder ser observado parcialmente desenrolado | **NADA.** Fora de uma guarda, `panic` e `exit` são byte a byte o que são hoje. O par está num namespace que fonte nenhuma alcança (§6.5.4) |
| **emite C?** | **nenhuma das duas emite C** — mas a via Go quase de certeza acrescenta C ou uma dependência externa: `setjmp`/`longjmp` são C de runtime, e a alternativa DWARF traz `libunwind` para a linha de link | zero C novo (`call_symbol` → funções Teko, `concorrencia-adiantada-s8.md` §4.2) |
| **determinismo / fixpoint** | **o pior dos dois, e por larga margem.** Passar os `defer` a runtime muda os bytes emitidos de **toda a função com `defer` no fonte do compilador** — e o compilador é escrito em `defer`. Mais: tabelas `.eh_frame` são superfície NOVA de fixpoint, com ordenação e endereços próprios. Um `gen2 != gen3` daí sairia com um diff que aponta uma tabela de unwind | não toca em código de produção nenhum: só a `main` sintetizada e as duas funções de guarda existem no binário de gate |
| **fica capacidade permanente?** | **SIM** — e é o único argumento genuinamente forte a favor | **NÃO.** Serve os testes e mais nada |

#### 6.11.4 O `exit` continua a ser primitiva SEPARADA, mesmo adoptando o Go inteiro

É fácil assumir que o recovery resolve os dois e ele resolve **um**. Em Go, `os.Exit` **termina o
processo imediatamente e nenhum `defer` corre** — está na documentação da própria função, e é por isso
que o `testing` de Go não consegue reportar um teste que chame `os.Exit`. Portanto:

> Mesmo com recovery à Go implementado por inteiro, **P-B (captura de `exit`) continua a ter de ser
> construída à parte**, exactamente como está desenhada em §6.5.2. A hipótese do dono cobre P-A e não
> cobre P-B.

#### 6.11.5 O que a via da guarda já dá do Go, sem construir o Go

Esta é a razão que decide, e é uma observação sobre a TOPOLOGIA, não sobre esforço.

O que o `testing` do Go **observa** — *um teste que entra em panic não mata a suíte; o framework
apanha-o, atribui-lhe o veredicto, e os outros continuam* — vem de DUAS peças: o `recover` **e** o
facto de cada teste correr na sua própria goroutine. R1 manda exactamente a segunda: **uma thread por
`#test`**. E com uma thread por teste, **a fronteira de desenrolamento de que o harness precisa é de
UM frame: a própria thread.** Terminar a thread já é um desenrolamento, e quem o implementa é a
plataforma (`pthread_exit` corre os cleanup handlers; `ExitThread` o equivalente).

Ou seja: **o harness fica com a propriedade observável do modelo de teste do Go sem construir a
maquinaria geral do Go**, porque a granularidade de isolamento que o dono já escolheu (thread por
teste) coincide com a granularidade de recuperação de que o harness precisa. O `recover` do Go existe
para recuperar NO MEIO de uma pilha e continuar; o harness nunca quer continuar — quer terminar o
teste e reportá-lo.

#### 6.11.6 A TENSÃO — RESOLVIDA POR R10, e a resolução é melhor do que a recomendação

A tensão levantada era: R7 quer as primitivas *"apenas para rodar os testes"*, e desenrolamento não se
esconde do utilizador. **R10 fechou-a, e não escolhendo um dos dois lados que eu apresentei — mudando
a pergunta.** A frase que o faz é esta:

> *"capturar aborts/panic **sem executá-los**"*

**Isso não é recuperar. É INTERCEPTAR — e a diferença dispensa o desenrolador inteiro.** Recuperar é
apanhar um panic em voo e continuar a partir de um frame no meio da pilha; para isso é preciso
desenrolar. Interceptar é o panic guardado **nunca chegar ao `abort()`**: deposita o veredicto e
termina a raia. **Não há nada para desenrolar, porque a coisa que desenrolaria nunca acontece.**

O dono escolheu interceptar. As três razões de custo que a §6.11.3 mediu deixam de estar em jogo:
não há pilha de `defer` em runtime, não há CFI/`.eh_frame`, não há protocolo de retoma. A semântica
de `defer` para o utilizador **não muda uma vírgula**, e `recover` não passa a existir como palavra
da linguagem.

| | o que R10 mantém | o que R10 apaga |
|---|---|---|
| R7 (*"só para testes, só o compilador conhece"*) | **INTACTO** | — |
| a via da guarda por thread (§6.5) | **é a via escolhida** | — |
| pilha de `defer` em runtime | — | **fora** |
| desenrolador (DWARF/CFI ou setjmp) | — | **fora** |
| protocolo de retoma | — | **fora** |
| `recover()` como capacidade da linguagem | — | **fora** |
| mudança na semântica de `defer` | — | **fora** |
| P-B (captura de `exit`) | **confirmado necessário e SEPARADO** (§6.11.4) | — |

#### 6.11.7 O RULING, e o que ele acrescenta ao desenho de §6.5

**Guarda por thread. Recovery à Go FORA.** A recomendação que esta secção fazia foi ratificada, e R10
traz uma peça a mais que §6.5 não tinha — a **condição** e a **fronteira**:

> *"se ao compilar um teste, informar que se trata de teste, pode bifurcar as funções globais de exit
> e panic (mantendo as diretas de os intactas)"*

Ou seja: a bifurcação **não é incondicional**. Ela só existe quando o compilador SABE que está a
compilar um teste, e as chamadas DIRECTAS ao SO ficam por tocar. As duas metades estão em §6.11.8 e
§6.11.9, e a segunda tem de estar escrita com nomes — senão alguém bifurca a errada.

**O ganho lateral, e não é pequeno:** com a bifurcação condicionada à compilação de um teste, a
exigência de §6.5.2 ("fora de uma guarda, o comportamento é byte a byte o de hoje") deixa de ser
**disciplina** e passa a ser **estrutura**: num binário que não é de teste, o ramo guardado **não é
emitido**. A fixture `gate_unguarded_panic_is_unchanged` ganha por isso uma forma mais forte — não é
só a SAÍDA que tem de ser idêntica, é o **BINÁRIO**. Está em §12.

#### 6.11.8 A FRONTEIRA — o que bifurca e o que fica intacto

**O que BIFURCA: as duas funções GLOBAIS, e o compilador já tem essa fronteira desenhada e nomeada.**

`texpr_diverges` (`src/checker/typer.tks`) define-a em uma linha, e o comentário por cima dela usa as
palavras do dono antes de ele as escrever — *"global builtins panic/exit, unqualified"*:

```teko
TCall as c => c.callee.segments.len == 1 && (c.callee.segments[0].name == "panic" || c.callee.segments[0].name == "exit")
```

`codegen.tks` tem a mesma linha. **Esta é a definição de "função global" para este efeito**, e não é
inventada aqui: um caminho de UM segmento chamado `panic` ou `exit`. Adoptá-la significa que a
bifurcação e a análise de divergência nunca podem discordar sobre o que é uma chamada global.

**Herdam a bifurcação DE GRAÇA, e é isso que se quer** — a família de guardas de runtime, porque
todas chamam `panic` (`src/runtime/teko_rt.tks`):

| guarda | dispara em |
|---|---|
| `panic_div0` | divisão por zero |
| `panic_oob` / `panic_oob_at` | índice fora de limites |
| `panic_cast` | conversão impossível |
| `panic_overflow` | overflow inteiro |

Um `#test` que faça um acesso fora de limites recebe assim um VEREDICTO em vez de matar a suíte —
sem uma linha de trabalho extra, porque a bifurcação está no `panic` que todas elas atravessam.

**O que FICA INTACTO — as directas do SO. Nomeadas, uma a uma:**

| # | sítio | o que é | porque NUNCA bifurca |
|---|---|---|---|
| 1 | `rt_abort()` — `exp extern fn rt_abort() = "abort" from "c"` (`src/runtime/teko_rt.tks`) | o `abort` da libc, declarado verbatim | é o FUNDO. Se bifurcasse, a bifurcação não teria fundo nenhum e o panic não-guardado deixaria de abortar |
| 2 | o builtin injectado `abort` (`src/checker/scope.tks`, *"teko::abort — host abort FFI bottom"*) | a mesma coisa, pela via do builtin | quem escreve `abort()` pediu o abort do host, não o panic do Teko. Bifurcá-lo seria mudar o significado de uma chamada directa |
| 3 | `exit(code)` da libc, dentro de `tk_exit` (`src/runtime/teko_rt.c`) | a saída real do processo | é o FUNDO de P-B, pela mesma razão de (1) |
| 4 | `_exit(127)` no filho do `fork` após `execvp` falhar (`teko_rt.c`, dois sítios) | a saída do filho que não conseguiu executar | corre **noutro processo**, entre `fork` e `exec`, onde só é legal chamar funções async-signal-safe. Uma bifurcação aqui escreveria numa tabela de guardas que pertence ao PAI |
| 5 | `_Exit(128 + sig)` em `tk_rt_crash_handler` (`teko_rt.c`) | o "catch" global de §6.5.1 | corre dentro de um handler de SINAL. `_Exit` é async-signal-safe; quase tudo o resto não é. É a última rede, e uma rede que chama código de utilizador deixa de ser rede |
| 6 | `abort()` na macro de OOM (`src/runtime/teko_rt.h`) | falha de alocação | não há memória para depositar veredicto nenhum |

**A regra que resume as seis, para não ser preciso decorar a tabela:** bifurca-se o que o PROGRAMA
TEKO chama pelo nome global; não se toca no que o RUNTIME chama para terminar de facto. A fronteira é
entre a superfície da linguagem e o fundo de FFI — que é exactamente a fronteira que
`concorrencia-adiantada-s8.md` §4.2 já usa quando diz que o `panic` em Teko *"só toca o host, no
caminho não-guardado, por `extern fn` para `write` e `abort`"*.

#### 6.11.9 A NUANCE — FECHADA por R14: marca de TEMPO DE COMPILAÇÃO

R10 tinha duas metades a apontar para tempos diferentes (*"ao **compilar** um teste, informar que se
trata de teste"* × *"capturar somente quando **rodar** … com um argumento"*). Apresentei as duas
leituras; **o dono respondeu `"1. Compilação"` (R14). Fica a (A).**

**As duas leituras, e o que a escolha compra:**

| | **(A) marca de COMPILAÇÃO — ESCOLHIDA** | **(B) argumento em EXECUÇÃO — não escolhida** |
|---|---|---|
| o que é | o binário de gate é compilado COM a bifurcação; um binário normal é compilado SEM ela | o binário traria os DOIS caminhos e escolheria por um argumento secreto |
| binário que não é de teste | **byte-idêntico ao de hoje — o ramo guardado nem é emitido** | não mudaria, mas o ramo estaria lá |
| custo em execução | **zero** | um teste de argumento por `panic`/`exit` |
| como se esconde do utilizador | **estruturalmente: não há código para chamar** | por o argumento ser secreto — mais fraco: o ramo existe e o binário pode ser invocado à mão |
| fixpoint | não sente nada | não sentiria, desde que o argumento não influenciasse bytes (§8.1) |

**O que a escolha promove de preferência a desenho oficial:** a garantia de que *"fora de uma guarda,
o comportamento é byte a byte o de hoje"* (§6.5.2) **deixa de ser disciplina e passa a ser
estrutura**. Não é uma promessa que alguém tem de cumprir linha a linha — é uma consequência de o
código não existir. **O portão dessa garantia é a fixture `nontest_binary_is_byte_identical`, que
compara BINÁRIOS e não saída** (§12). É a forma forte, e só (A) a permite.

**(B) fica REGISTADA como não escolhida, e com uma actualização que importa a quem a reabrir:** a
razão de ela estar em desvantagem incluía um BLOQUEIO — dependia de `teko::env::args()`, que §16
mediu **nem linkar** no backend nativo (`undefined reference to 'teko_args'`). **Esse bloqueio CAIU:**
o vagão `cargo/0.3.1.0-args-native` aterrou no vagão principal — o `main` sintetizado passou a receber
`argc`/`argv` e a chamar `tk_set_args`, e `args` foi resolvido no `call_symbol`. **Portanto (B)
deixou de estar bloqueada e continua não escolhida**, agora só pelos seus méritos (contenção mais
fraca, custo em execução não-nulo). Quem a reabrir tem de saber as duas coisas: que o impedimento
técnico desapareceu, e que a escolha não mudou por isso.

**O que NÃO muda com a decisão:** a fronteira de §6.11.8, a tabela de guardas, o par
`gate_guard_begin`/`gate_guard_end`, e tudo em §6.1–§6.10. A nuance decidia ONDE está o interruptor,
não o que ele liga.

#### 6.11.9b ONDE VIVE a marca, e como se propaga — deixa de ser primitiva por decidir

A marca era a primitiva bloqueada **5d** ("não existe seam nenhuma"). Com R14 passa a ser **trabalho
nomeado**, e o achado é que **não precisa de maquinaria nova**: o passo 3 da fatia 1 já tem de levar
um modo até ao lowering, porque `lower_item_function` tem de parar de descartar `is_test` e
`lower_virtual_main` tem de tomar as statements sintetizadas. **A marca É esse mesmo modo.**

O precedente exacto já existe no ficheiro e deve ser copiado em vez de inventado — `flat_symbols`:

```teko
pub fn lower_program(prog: checker::TProgram, flat_symbols: bool = false): LModule | error
```

um parâmetro de topo com omissão, carregado em `LowerCtx` (`flat_symbols: bool`) e reproduzido em
cada construtor de contexto. A marca de teste faz a mesma viagem, pelo mesmo caminho:

| camada | o que carrega |
|---|---|
| `src/build/project.tks` | decide que ESTE build é o do gate e passa a marca |
| `src/build/gate.tks` | sintetiza o `main`; a marca viaja ao lado, não dentro do `GatePlan` (o `GatePlan` descreve a FORMA do `main`, não o modo do compilador) |
| `lower_program` | recebe-a como parâmetro de topo, à imagem de `flat_symbols` |
| `LowerCtx` | carrega-a, e cada construtor de contexto reproduz o campo |
| `lower_item_function` / `lower_virtual_main` | consomem-na já no passo 3 da fatia 1 |
| `call_symbol` | consome-a mais tarde (migalha 14) para decidir se bifurca `panic`/`exit` |

**A instrução que fica para quem implementar o passo 3:** dar ao sinalizador um nome de primeira
classe e um doc-comment que diga que ele é TAMBÉM o interruptor da bifurcação — **não um `bool`
ad-hoc chamado `is_gate`**. Se o passo 3 introduzir um sinalizador anónimo, a migalha 14 inventa um
segundo, e passam a existir duas respostas para "isto é um teste?" — que é a doença que este
repositório já pagou várias vezes.

#### 6.11.10 O que esta decisão APAGA do plano

Honestamente: **do plano de migalhas, nada** — porque a §6.11 recomendou contra o desenrolador desde
a primeira redacção e nenhuma migalha de 11 a 17 foi escrita para o servir. Não vou fabricar uma
supressão para parecer que houve.

O que a decisão apaga é um **RISCO** e um **ramo de futuro**, e vale registá-los porque estavam
mesmo em cima da mesa:

- morre a hipótese de a metade de threads arrastar consigo um vagão de linguagem (defers em runtime +
  desenrolador + `recover` + re-especificação de `defer`) — que era o cenário em que o harness deixava
  de ser harness;
- morre a variante da migalha 13b em que `panic`/`exit` em Teko teriam de suportar retoma. Ficam a ser
  o que §6.5.3 já desenhava: escrever e terminar.

O que a decisão **ACRESCENTA** ao plano está em §6.11.8 (a fronteira, que agora é obrigatória e
tem de estar escrita no código) e na migalha 14, que ganha a condição de compilação.

**O gatilho de reabertura mantém-se, e agora com um dono explícito para a frase:** o dono disse *"não
gosto do recover"*. Se algum dia a linguagem quiser recuperação de erro VISÍVEL AO UTILIZADOR, o
desenrolador passa a ser necessário por mérito próprio — e nesse dia **o harness deve ser RE-ASSENTE
sobre ele, não duplicado**: `gate_guard_begin`/`gate_guard_end` viram um `defer` + `recover`
sintetizados pelo mesmo `src/build/gate.tks`, e a tabela de guardas desaparece. §6.5 é
deliberadamente pequeno o suficiente para ser deitado fora nesse dia sem lamentar.

### 6.12 O NOME — RESOLVIDO. É `chan<T>`, por ruling e por coerência.

A pergunta que este documento devolveu ao dono foi respondida no mesmo dia.

**R9 (2026-07-29), literal:** *"Grafia, prefira `chan<T>` e combina com os outros tipos que são
curtos."*

A razão dele é de COERÊNCIA, e vale registá-la porque generaliza para além deste caso: a linguagem
nomeia os seus tipos curtos (`str`, `i32`, `u64`, `ptr`, `uptr`) e `channel<T>` seria o membro
comprido de uma família curta. `chan<T>` fica.

**As três fontes divergentes foram CORRIGIDAS nesta carga** — porque o argumento que levantou a
pergunta era exactamente este, e deixá-las vivas depois do ruling seria cometer o defeito que apontei:

| ficheiro | o que mudou |
|---|---|
| `TEKO_MASTER_PLAN.md`:260 e :560 | `channel<T>` → `chan<T>`, e a linha de RESERVA ganhou a nota datada de que só a GRAFIA ficou decidida — a palavra continua reservada |
| `docs/design/concorrencia-adiantada-s8.md` §3.3 (e §3 introdutório) | token actualizado, com um bloco de citação a dizer que a SUBSTÂNCIA não mudou |
| `docs/memory/teko-laws-digest.md` (regra 3 do modelo de corrotina) | **não estava na lista que me foi dada — encontrei-a a varrer, e corrigi-a pelo mesmo critério** |

**O que NÃO foi tocado, e é deliberado:** a objecção de `concorrencia-adiantada-s8.md` §3.3 — *"a
única que ameaça o determinismo"* — fica intacta, palavra por palavra. Ela é anterior, continua
correcta como argumento, e o dono ter escolhido o contrário para o harness não a invalida: invalida-a
só se o perigo que ela nomeia não for contido, e §6.10 mostra como é. **As duas coexistem: a objecção
e a resposta.** Suavizá-la porque o ruling foi noutro sentido seria reescrever a história para que ela
concordasse com o presente — que é a forma mais silenciosa de perder um argumento válido.

---

## 7. Cobertura sob paralelismo

Factos: `cov_enter(idx)`/`cov_leave()` marcam "o teste corrente" num sink de PROCESSO
(`tk_cov_ids`/`tk_cov_n`/`tk_cov_cap`, `teko_rt.c`:1977-1979); `cov_mark`, `cov_line_at` e
`cov_branch_at` escrevem tabelas globais. Sob threads paralelas, a atribuição de uma linha ao teste
que a executou passa a ser um sorteio.

**A decisão, e ela é possível porque a pergunta do PISO não é a pergunta da ATRIBUIÇÃO:**

1. As três métricas de piso (funções/linhas/ramos) são a **UNIÃO** sobre a corrida inteira. União é
   independente de ordem e é monótona. Logo: **os sinks passam a ser POR THREAD** (a mesma mudança de
   classe de armazenamento de §6.7) e o pai funde-os depois da barreira, por **ORDEM DE ÍNDICE**, com
   união. `coverage::coverage_pct`/`line_coverage_pct`/`branch_coverage_pct` leem os sinks fundidos
   sem uma linha de mudança.
2. A fusão por ordem de índice não é necessária para a união ser correcta (é comutativa) — é para que
   qualquer consumidor futuro que NÃO seja comutativo não introduza intermitência sem ninguém reparar.
3. **A atribuição POR TESTE melhora, não piora.** O sink de uma thread É a cobertura de um teste, o
   que é exactamente o que `--per-test-cov` hoje obtém reexecutando um binário por teste. Essa
   melhoria não é reclamada nesta carga: `--per-test-cov` fica no caminho actual até a migalha 15.
4. **O depósito para o pai**: `tk_cov_dump(const char *)` continua sem forma chamável honesta de Teko
   (o `char*` × `tk_str`, `gate-sem-c-0.3.0.31.md` §4.2(e)). **A tensão dissolve-se e não se decide:**
   a imagem dos sinks fundidos passa a ser escrita **pelo `main` gerado, em Teko**, no canal de
   veredicto (§5.3) — `teko::io::write_file`, que já existe e já baixa nativamente. Não há `char*`
   nenhum a alcançar. A decisão que `gate-sem-c` elevou ao dono deixa de ser necessária, o que é o
   mesmo desfecho que `concorrencia-adiantada-s8.md` §10.1 previu.
5. **A guarda, e é barata:** o relatório de cobertura com `lanes=1` e com `lanes=N` tem de ser
   BYTE-IDÊNTICO. Qualquer diferença é defeito, não ruído. É fixture.

---

## 8. Determinismo

Quatro regras, todas estruturais — nenhuma é disciplina de quem escreve testes.

1. **Índice estático.** O índice de um teste é a sua posição em `prog.items`. Não há roubo de
   trabalho, não há reordenação por duração, não há afinidade.
2. **Escrita disjunta.** Cada teste escreve numa casa que é só sua. Não há acumulador partilhado, nem
   contador, nem lista com append concorrente.
3. **Quem executa não imprime.** O corpo de um teste escreve para o seu próprio `out_text`/`err_text`
   (capturados) e nunca para o stdout do processo. O pai imprime, depois da barreira, percorrendo
   `0..count`.
4. **O canal é transporte, não ordem.** A ordem de chegada ao `recv` é a ordem do escalonador. O
   recetor drena para `verdicts[v.index]` e ordena pelo índice. **Nunca imprimir na ordem de
   chegada** — é a única forma de a saída ser a mesma com 1 thread e com 16.

Para a via 1 a regra é a que já existe: o relatório é dobrado por índice de linha depois de todos os
filhos serem colhidos, que é o que `run_captured_batch` já faz e que `run_pool` herda.

**Afirmado por:** `gate_lanes_output_identical` — a mesma suíte com `lanes` 1, 4 e 16, stdout
comparado por diff entre as três; exit 0 nas três e diffs vazios.

### 8.1 O FIXPOINT — o que estas peças lhe devem

As duas primitivas novas (§6.5) e o sintetizador (§3) passam a estar **dentro do compilador que se
reconstrói a si próprio**, portanto entram em `gen2 == gen3` byte-idêntico. Três exigências, e a
primeira já apanhou um erro na primeira redacção deste documento:

1. **NENHUM valor lido do ambiente pode influenciar bytes emitidos.** `TEKO_TEST_LANES` e
   `TEKO_REGR_JOBS` são lidos pelo `main` gerado **em tempo de execução** e nunca cozidos como
   literais em `gate_program`. A primeira versão de `GatePlan` levava um campo `lanes: u64`; com ele,
   o mesmo fonte compilado num host de 4 núcleos e noutro de 64 emitiria bytes DIFERENTES, e
   `gen2 == gen3` partiria por uma razão que não tem nada a ver com o compilador — e o diff apontaria
   um número, nunca a causa. É a mesma família dos nomes ordinais de símbolo que
   `concorrencia-adiantada-s8.md` §6.2 catalogou. **Corrigido: `GatePlan` só carrega a FORMA, e a
   forma é política do compilador.**
2. **A tabela de guardas tem largura decidida em execução**, pelo mesmo motivo. Ela é alojada pelo
   `main` gerado com `lanes` linhas; `lanes` vem do ambiente; o CÓDIGO que a aloja é o mesmo em todo
   o host.
3. **O sintetizador é uma função pura da AST tipada.** Não lê relógio, não lê ambiente, não lê
   sistema de ficheiros, e percorre `prog.items` na ordem em que os recebe. Duas execuções sobre a
   mesma árvore produzem o mesmo `main`, item a item.

**E o aviso que o fixpoint não dá:** ele compara dois binários do MESMO compilador, portanto **é cego
a uma miscompilação determinística** — foi exactamente assim que a variante DECLARADA sobreviveu a
todos os portões. Um fixpoint verde não é prova nenhuma de que os veredictos estão certos; quem prova
isso é §10, e só até 0.3.1.4.

---

## 9. `TEKO_REGR_JOBS` e o grau de paralelismo — decidido onde

**Decidido no BINÁRIO, lido do AMBIENTE, nunca de argv.** Não é preferência: um binário do backend
próprio vê `teko::env::args()` vazio (§4), portanto argv não é um canal disponível. O ambiente é
também o que o doc-comment de `REGR_JOBS_ENV` já argumenta (*12-Factor: a concorrência da máquina é
assunto da máquina*).

**Dois botões, porque os dois recursos limitantes são diferentes** — e é este o argumento, não
"dois porque são duas metades":

| botão | metade | omissão | recurso limitante |
|---|---|---|---|
| `TEKO_REGR_JOBS` | processos (regressões) | **4, inalterado** | **MEMÓRIA** — cada filho é um `teko build` inteiro com a sua arena; o vagão mediu 1,3 GB de pico |
| `TEKO_TEST_LANES` | threads (unitários) | `hardware_parallelism()` | **NÚCLEOS** — cada teste é curto e a sua arena é pequena |

`TEKO_REGR_JOBS` **mantém nome, valor de omissão e significado**, porque é um botão publicado que
pernas de CI já definem; renomeá-lo partiria configurações que ninguém está a rever nesta carga. O
que muda é só QUEM o lê: deixa de ser "quantos `&` antes de um `wait`" e passa a ser "quantos filhos o
pool mantém em voo".

```teko
/**
 * PARALLELISM_ENV_REGR — a chave de ambiente do grau de paralelismo da metade de PROCESSOS.
 */
const PARALLELISM_ENV_REGR: str = "TEKO_REGR_JOBS"

/**
 * PARALLELISM_ENV_LANES — a chave de ambiente do grau de paralelismo da metade de THREADS.
 */
const PARALLELISM_ENV_LANES: str = "TEKO_TEST_LANES"

/**
 * parallelism_of — o grau de paralelismo escrito em `raw`, ou `fallback` para tudo o que não seja um
 * decimal positivo.
 *
 * Uma ÚNICA função de leitura para os dois botões, para que um valor malformado tenha exactamente um
 * comportamento em todo o projecto. Um valor malformado é um erro de CONFIGURAÇÃO, e a leitura segura
 * de um é "quem escreveu isto não queria dizer zero".
 *
 * @param raw       o valor tal como está no ambiente
 * @param fallback  o valor a usar quando `raw` é vazio, não-numérico ou zero
 * @return          o grau efectivo, nunca zero
 * @since 0.3.1.2
 */
fn parallelism_of(raw: str, fallback: u64): u64

/**
 * hardware_parallelism — quantas threads o host consegue executar de facto em paralelo.
 *
 * @return  o número de processadores em linha, nunca menor que 1
 * @since 0.3.2
 */
pub fn hardware_parallelism(): u64
```

---

## 10. A PROVA DE EQUIVALÊNCIA — e ela é parte do desenho, não um seguimento

O dono fixou a rota C como referência de comportamento. A prova de que o harness novo dá **exactamente
os mesmos veredictos** tem quatro camadas, e a **ordem entre elas é obrigatória**: cada uma isola uma
variável, e trocá-las torna qualquer diferença ambígua.

| # | camada | o que prova | como |
|---|---|---|---|
| **P1** | **estrutural** | o harness não é uma variável | um só sintetizador (`gate_program`), duas rotas de código. A diferença entre rotas é, por construção, só a geração de código |
| **P2** | **golden, antes×depois, MESMA rota** | o sintetizador não mudou nada | o stdout do binário de gate na rota C, com o `main` gerado, **byte-idêntico** ao do `main` emitido à mão de hoje |
| **P3** | **diferencial, rota×rota, SERIAL** | o nativo concorda com o oráculo | a mesma suíte com `TEKO_BACKEND=c` e sem ele, ambos `lanes=1`; linhas de veredicto byte-idênticas |
| **P4** | **serial×paralelo, MESMA rota** | o paralelismo não mudou veredicto nenhum | `lanes` 1, 4, 16; stdout e relatório de cobertura byte-idênticos |

**A ferramenta:** `scripts/gate_route_equivalence.sh`, que corre os dois lados, normaliza só o que é
legitimamente variável (durações), compara linha a linha e **nomeia o PRIMEIRO teste divergente**.
"O gate falhou" não é um resultado utilizável; "o teste `lvt_x` deu ok em C e Failed em nativo" é.

**A regra de arbitragem já existe e não é inventada aqui** — A REGRA DO ORÁCULO
(`docs/memory/0.3.1.0-linux-native-first-stop.md`): *enquanto a rota C existir, qualquer divergência
de COMPORTAMENTO entre nativo e C é um defeito do nativo até prova em contrário.* E a excepção
medida também fica: o oráculo é um prior forte, não escritura — há já um caso registado em que o
nativo está certo e a rota C recusa (`variant Box | null`).

**A prova é PERECÍVEL.** A rota C morre na 0.3.1.4. P2 e P3 têm de ser executadas e o seu resultado
arquivado antes disso. Depois, P4 é a única que sobrevive — e P4 sozinha não prova equivalência com
coisa nenhuma, só consistência interna.

---

## 11. A SEQUÊNCIA DE MIGALHAS

**Ponto ritual** = onde o gate completo tem de passar: `teko test .` verde + fixpoint `gen2 == gen3`
byte-idêntico + `sh scripts/no_emitted_c.sh`. Migalhas de sonda e de fixture saltam o fixpoint.

| # | migalha | entrega | ritual |
|---|---|---|---|
| **0** | **SONDAR, não construir** | (a) o backend próprio baixa um corpo de `#test`? (`lower.tks`:8186 descarta-os — nunca foi exercitado); (b) um `extern fn` com DOIS parâmetros `[]str` mais quatro `str` baixa nativamente? (o `spawn_redirected` de §5.2 depende disso); (c) `teko::io::write_file` baixa nativamente no binário de gate?; (d) **`_Thread_local` em `tk_g_root`/`tk_g_regs`/`tk_arena_marks` basta, ou a API precisa de alça?** (§6.7) — mede, não presumas | não |
| **1** | **o gate OBEDECE ao backend** | `run_native_gate` → `run_test_gate`, despacho por `Backend`: nativo → `.o` + `link_object`, zero `.c`; `c` → o caminho de hoje, só sob `TEKO_BACKEND=c`. **É a peça que fecha R2 e não depende de nada** | **sim** |
| **2** | **a catraca de zero C dentro do compilador** | o gate regista os caminhos que escreveu; se algum termina em `.c` com backend nativo, a sessão FALHA. `no_emitted_c.sh` fica como rede exterior, com baseline VAZIA sob nativo | **sim** |
| **3** | **builtin `flush_out`** | `tk_flush_out` existe no runtime (`teko_rt.h`:183) e não é builtin do checker; o `main` gerado consome-o (o rótulo tem de sair do buffer antes do corpo) | **sim** |
| **3b** | **o namespace reservado `teko::__gate` e a regra de prefixo `__`** | a arm de `call_symbol` para o namespace sintetizado; a recusa em SOURCE de qualquer segmento começado por `__`, com diagnóstico próprio. **Sem isto, "conhecidas somente pelo compilador" (R7) é intenção e não mecanismo** (§6.5.4) | **sim** |
| **4** | **`src/build/gate.tks` — o `main` SERIAL sintetizado** | `gate_program`/`gate_test_indices`/`GatePlan`/`TestVerdict`/`TestState`; `lower_item_function` deixa de descartar `is_test` sob modo gate; `lower_virtual_main` toma as statements sintetizadas; a `main` sintetizada é excluída do DENOMINADOR de cobertura por proveniência. **Escrito contra `TestVerdict` desde já (§6.9)**; `GatePlan` NÃO carrega `lanes` (§8.1) | **sim** |
| **5** | **a rota C consome o MESMO `main`** | `emit_test_main`/`emit_test_call` APAGADOS; `tk_emit_c_test` = `tk_emit_c_mode(gate_program(...))`. **P2 é o portão desta migalha** | **sim** |
| **6** | **P3 — o diferencial rota×rota, serial** | `scripts/gate_route_equivalence.sh` + o resultado arquivado. Pode FALHAR, e falhar é o produto: cada divergência é um defeito do nativo com endereço | não |
| **7** | **`spawn_redirected` + `wait_one`** | a divisão de `tk_rt_run` em `src/runtime/teko_rt.c`; `ProcHandle`; os três descritores por caminho; POSIX e Win32 | **sim** |
| **8** | **`run_pool` — a metade de regressões** | janela deslizante, colheita por índice, `child_ns` real. `run_captured_batch` reescrita por dentro, mesma assinatura de intenção | **sim** |
| **9** | **a morte do andaime de shell** | os treze símbolos de §5.4 apagados, `.rc` incluído. É o passo que o `parallel-test-harness-0.3.2.md` prometeu | **sim** |
| **10** | **o terceiro canal, com fallback** | `TEKO_VERDICT_CHANNEL`; sem ele, o veredicto vai para stderr (R0). Um registo por linha, com etiqueta de tipo (veredicto · início-de-índice · imagem de cobertura) | **sim** |
| **11** | **`cabi fn(T…): R` em parâmetro de `extern fn`** | coerção do nome nu de uma função de topo não-capturante para `LFuncAddr`; recusa de capturante, genérica, método e tipo não representável em ABI C. Já desenhado em `concorrencia-adiantada-s8.md` C1 — **não redesenhar** | **sim** |
| **12** | **o chão de thread** | `pthread_create`/`join`/`exit`/`self` e os gémeos Win32 como `extern fn` sob `#os` | **sim** |
| **13** | **raiz de arena e sinks de cobertura POR THREAD** | o que a migalha 0(d) tiver medido. **BLOQUEANTE:** nenhuma migalha posterior pousa antes desta | **sim** |
| **13b** | **`panic`/`exit` reimplementados em TEKO** | `call_symbol` deixa de apontar a `tk_panic_str`/`tk_exit` e passa a apontar a funções Teko em `src/runtime/teko_rt.tks`, cujo fundo NÃO-guardado é `extern fn` para `write`/`abort`. **Zero C novo.** Saída byte-idêntica, exit 134 preservado — é a pré-condição das duas primitivas e é `concorrencia-adiantada-s8.md` C3 verbatim | **sim** |
| **14** | **as DUAS PRIMITIVAS — captura de `panic` (P-A) e de `exit` (P-B)** | a bifurcação **CONDICIONADA à compilação de um teste** (R10) das duas funções GLOBAIS `panic`/`exit` — as de UM segmento, a fronteira que `texpr_diverges` já define; as SEIS directas do SO de §6.11.8 ficam intactas; `gate_guard_begin`/`gate_guard_end` no namespace reservado; a tabela de guardas na região do pai, varrida por `sys_thread_self()`; `teko::assert::assert_fail`. Num binário que NÃO é de teste o ramo guardado não é emitido — a identidade com o de hoje passa a ser estrutural | **sim** |
| **15** | **`chan<T>`** | `chan_new`/`send`/`recv`/`chan_close`; a cópia para a região do recetor; a recusa de compilação para `T` não copiável | **sim** |
| **16** | **`GateShape::Threaded`** | o `main` gerado lança uma thread por teste, no máximo `lanes` em voo, drena o canal, ordena por índice, imprime. Balanço de esperados; `Vanished` é vermelho | **sim** |
| **17** | **P4 + a reexecução serial pós-queda** | `lanes` 1/4/16 byte-idênticos, cobertura incluída; e a política de reexecutar com `lanes=1` os índices que o canal diz que estavam em voo quando o processo morreu | **sim** |

**Onde a paragem é dura:** as migalhas 1 a 10 são executáveis HOJE — nenhuma precisa de primitiva de
linguagem que não exista. As migalhas 11 a 17 dependem de `cabi fn`, que não existe. E **correr o
binário de gate do PRÓPRIO compilador pela rota nativa depende do auto-build nativo**, que hoje para
no degrau 8 (`MInst`, variante DECLARADA em `push_box_bytes`). Isso **não bloqueia** as migalhas
1 a 17: todas se provam em projectos de sonda (`examples/probes/`, `examples/regressions/own_native/`),
que é o veículo que os degraus 3 a 7 já validaram. O que bloqueia é só o gate do compilador sobre si
próprio, e esse desbloqueia-se no vagão do backend, não aqui.

---

## 12. Fixtures de regressão

Todas no caminho nativo, um par entrada → código de saída. (Não há segundo motor: o corpus corre no
nativo, e a rota C só aparece nas fixtures de EQUIVALÊNCIA, que a nomeiam explicitamente.)

| fixture | forma | esperado |
|---|---|---|
| `gate_honours_backend_native` | `teko test .` num projecto de sonda com `TEKO_BACKEND` não definido; contar `.c` no fim | 0 **e** zero `.c` |
| `gate_emits_c_only_under_c_backend` | o mesmo com `TEKO_BACKEND=c` | 0 **e** exactamente o `.c` do gate |
| `gate_zero_c_ratchet_bites` | forçar um `.c` a existir sob nativo no fim da sessão | não-zero, nomeando o ficheiro |
| `gate_main_synthesis_is_byte_identical` | stdout do gate com `main` gerado × com o `main` emitido à mão (P2) | 0 e diff vazio |
| `gate_route_equivalence_serial` | a mesma suíte nas duas rotas, `lanes=1` (P3) | 0 e diff vazio nas linhas de veredicto |
| `gate_test_index_is_item_index` | um projecto com `#test` intercalados com funções de produção; a atribuição de cobertura de cada teste tem de casar com o índice do item | 0 |
| `gate_reports_all_failures` | três `#test` a falhar; hoje o gate nomeia **um** | não-zero **e** os três nomeados |
| `gate_panic_is_not_assert` | um teste que falha uma asserção e outro que faz `panic("assertion failed: x")` à mão | não-zero, **e** o primeiro `Failed`, o segundo `Panicked` |
| `gate_exit_inside_test_is_a_verdict` | um teste que chama `exit(3)` | não-zero, **e** os OUTROS testes têm veredicto, **e** o registo diz `Exited code=3` |
| `gate_vanished_is_red` | um teste que faz um acesso inválido (morte sem escrever nada) | não-zero, **e** o teste é nomeado como `Vanished`, **e** os outros são reportados |
| `gate_lanes_output_identical` | a mesma suíte com `lanes` 1, 4, 16 (P4) | 0 nas três **e** diffs vazios |
| `gate_coverage_lanes_identical` | o relatório de cobertura com `lanes` 1 e N | 0 **e** byte-idênticos |
| `gate_unguarded_panic_is_unchanged` | um `panic` FORA de um teste guardado | 134, **e** a linha `TK_PANIC_MARKER` byte-idêntica à de hoje |
| `gate_unguarded_exit_is_unchanged` | um `exit(7)` FORA de um teste guardado | 7, saída byte-idêntica à de hoje |
| `nontest_binary_is_byte_identical` | o MESMO projecto sem `#test`, compilado antes e depois de a migalha 14 aterrar; **binários** comparados byte a byte. **É O PORTÃO da garantia de R14** — com a marca de COMPILAÇÃO, um binário que não é de teste não leva o ramo guardado, logo a identidade não é uma promessa a cumprir: é uma consequência de o código não existir (§6.11.9) | 0 só se idênticos |
| `os_direct_abort_never_bifurcates` | um `#test` guardado que chama `abort()` (o builtin injectado, a directa nº2 de §6.11.8) em vez de `panic` | o processo aborta de facto — a captura NÃO o apanha, e é isso que se afirma |
| `runtime_guards_inherit_the_bifurcation` | um `#test` guardado com índice fora de limites (`panic_oob`) e outro com divisão por zero (`panic_div0`) | não-zero, **e** os dois com VEREDICTO nomeado, **e** os restantes testes reportados |
| `thread_lane_unguard_pairs` | uma thread que retorna sem `gate_guard_end` deixa linha morta na tabela | 1 (detectado, nomeando a linha) |
| `gate_primitive_not_callable_from_source` | um `.tks` que escreve `gate_guard_begin(0)` | 1 (`unknown function`) |
| `gate_reserved_namespace_rejected` | um `.tks` com `use teko::__gate` ou um `src/__gate/` | 1 (segmento reservado, diagnóstico próprio) |
| `gate_main_not_in_coverage_denominator` | percentagem de cobertura de um projecto ANTES e DEPOIS de o gate sintetizado pousar | idênticas |
| `gate_primitives_absent_from_tsym` | traço de panic de um binário de gate | 134 **e** nenhum nome `__gate` no traço |
| `fixpoint_is_lane_count_blind` | construir o mesmo projecto com `TEKO_TEST_LANES=1` e `=64`, comparar os OBJECTOS byte a byte (§8.1) | 0 só se idênticos |
| `chan_send_copies_the_value` | o emissor muda a sua cópia depois do `send`; o recetor não vê a mudança | 0 |
| `chan_survives_sender_panic` | três emissores, o do meio entra em panic depois de enviar; o veredicto enviado chega intacto | 0 |
| `chan_rejects_noncopyable` | `chan<T>` com `T` contendo um fecho | 1 (erro de compilação, nomeando o campo) |
| `chan_close_then_recv_is_null` | canal fechado e vazio | 0 |
| `chan_send_on_closed_is_error` | `send` depois de `chan_close` | 0 (o `error` é tratado, não é silêncio) |
| `arena_per_thread_isolation` | cada thread grava um padrão conhecido, faz `arena_pop` e relê; qualquer cruzamento falha | 0 |
| `spawn_redirected_three_streams` | um filho que lê stdin, escreve em stdout e em stderr, com os três apontados a ficheiros do pai | 0 **e** os três ficheiros com o conteúdo esperado |
| `spawn_redirected_inherits_when_empty` | caminhos vazios herdam os descritores do pai | 0 |
| `spawn_failure_is_negative` | `spawn_redirected` de um executável inexistente | `raw` negativo, **sem** travamento |
| `pool_order_is_row_order` | linhas com durações deliberadamente invertidas | 0 **e** o relatório na ordem das linhas |
| `pool_child_crash_does_not_stop_the_run` | uma linha cujo filho morre por sinal | não-zero **e** as outras linhas com veredicto |
| `verdict_channel_defaults_to_stderr` | binário corrido à mão, sem `TEKO_VERDICT_CHANNEL` | 0 **e** o veredicto em stderr |
| `verdict_channel_survives_child_death` | o filho morre a meio; o que chegou ao canal fica no disco | não-zero **e** os índices em voo legíveis |
| `regr_jobs_env_still_honoured` | `TEKO_REGR_JOBS=1` e `=8` no pool novo | 0 nas duas **e** relatórios idênticos |
| `test_lanes_env_honoured` | `TEKO_TEST_LANES=1` e `=8` | 0 nas duas **e** stdout idêntico |

---

## 13. O que está BLOQUEADO por primitiva em falta — a lista, explícita

| # | primitiva | estado medido | quem bloqueia |
|---|---|---|---|
| 1 | **`cabi fn(T…): R`** em parâmetro de `extern fn`, com coerção do nome de função | **NÃO EXISTE.** Um nome de função como valor vira closure `{fn, env}` (`lower_fn_value`), não um endereço. `LFuncAddr` já existe e o isel já o baixa nas duas arquitecturas | migalhas 11-17, ou seja **toda a metade de threads** |
| 2 | **chão de thread** (`pthread_create`/`join`/`exit`/`self`, gémeos Win32) | não existe; `src/` não tem `thread` nem `isolate` | 12-17 |
| 3 | **raiz de região e pilha de marcas POR THREAD** | `tk_g_root`/`tk_g_regs`/`tk_arena_marks` são estáticos de processo. **Hipótese de custo baixo (classe de armazenamento) por MEDIR** | 13-17 |
| 4 | **sinks de cobertura por thread + fusão** | `tk_cov_ids`/`tk_cov_n`/`tk_cov_cap` são de processo | 13, 16 |
| 5 | **captura de `panic` (P-A) e captura de `exit` (P-B)** | `tk_panic_str`/`tk_exit` são `_Noreturn` e matam o processo; o único "catch" que existe (`tk_rt_crash_handler`) trata sem interromper. **DUAS primitivas novas (R7), sem antecedente para P-B.** Por R10 são INTERCEPÇÃO, não recuperação — logo **não exigem desenrolador** | 14, 16 |
| 5d | ~~a marca "isto é um teste"~~ — **JÁ NÃO É PRIMITIVA BLOQUEADA (R14)** | continua a não existir seam (`CgMode::TestCov`/`TestPlain` são do EMISSOR de C, não do lowering), mas deixou de ser pergunta: é **trabalho nomeado**, com o precedente `flat_symbols` a copiar, e **viaja no mesmo sinalizador que o passo 3 da fatia 1 já tem de levar** (§6.11.9b) | — |
| ~~5e~~ | ~~o argumento em tempo de EXECUÇÃO~~ | **RETIRADA**: a opção (B) não foi escolhida (R14). E o bloqueio que a penalizava CAIU — `cargo/0.3.1.0-args-native` aterrou, o `main` nativo recebe `argc`/`argv` e chama `tk_set_args`. Registado em §6.11.9 para quem a reabrir | — |
| 5b | **`panic`/`exit` em Teko** (pré-condição de 5) | `call_symbol` aponta hoje a `tk_panic_str`/`tk_exit`; o fundo `write`/`abort` por `extern fn` está desenhado e não escrito | 13b, 14 |
| 5c | **namespace reservado + regra de prefixo `__`** | não existe; `builtin_fn` resolve por ÚLTIMO SEGMENTO, o que torna todo builtin injectado publicamente chamável — o oposto do que R7 exige | 3b, 14 |
| 6 | **`chan<T>`** e as quatro funções | palavra não reservada no lexer; superfície de linguagem nova. Grafia RESOLVIDA por ruling (§6.12) e as três fontes divergentes corrigidas | 15-16 |
| 7 | **`spawn`/`wait` separados + redirecção por caminho** | `teko::process` só tem `run`/`run_quiet`, síncronos, sem redirecção escolhida pelo chamador | 7-10 |
| 8 | **builtin `flush_out`** | `tk_flush_out` existe no runtime, não é builtin do checker | 3-4 |
| 9 | **`extern fn` com dois parâmetros `[]str`** baixado nativamente | por MEDIR (migalha 0b). Se não baixar, a saída é uma assinatura por caminho em vez de um vector | 7 |
| 10 | **`tk_set_args` no `main` nativo** | `lower_virtual_main` cria `main` com ZERO parâmetros; `args()` é vazio em todo o binário nativo | **não bloqueia este desenho** — nenhuma peça aqui usa argv. Bloqueia o CLI nativo, e é do vagão do backend. **REPORTADO** |
| 11 | **auto-build nativo do compilador** | para no degrau 8 (`MInst` em `push_box_bytes`) | só o gate do COMPILADOR sobre si próprio. As 17 migalhas provam-se em sondas |

---

## 14. O QUE FICOU POR DECIDIR, e porquê

Secção obrigatória. Cada item diz porque não foi decidido aqui — e nenhum deles é "depende".

1. **A política de um `#test` que chama `exit` com um código esperado.** O REGISTO distingue sempre
   (§6.5.5); a POLÍTICA é vermelha hoje. O atributo `#test(expect_exit = N)` é a superfície natural e
   **não é desenhada aqui** porque nenhuma falha medida a exige — o `MASTER_PLAN` manda não congelar o
   que não precisa de ser congelado. Fica NOMEADA para não ser reinventada com outro nome.
2. **`scope { }` e `spawn` como palavras-chave.** Continuam reservadas. R4 nomeou `chan<T>` e só
   `chan<T>`; adiantar as outras duas seria congelar superfície que nenhum ruling pediu.
2b. ~~A GRAFIA~~ — **FECHADA (R9, 2026-07-29): é `chan<T>`**, e as três fontes divergentes foram
   corrigidas nesta carga (§6.12). Deixada aqui riscada, e não apagada, porque era a única pergunta
   que este documento devolveu ao dono e o registo de que foi respondida vale mais do que a linha
   limpa.
2c. **Se as duas primitivas se armam com UM par ou com DOIS** (§6.5.2). R7 diz "duas primitivas";
   o desenho entrega duas CAPACIDADES armadas por um par, com o argumento de que dois pares dobram as
   maneiras de deixar um por desarmar. Se o dono quis dois pontos de armar, é uma linha.
2f. **O BUG das cinco saídas (§18)** não é decisão nenhuma — é correcção, com dono próprio, e não é
   desta lane. O que fica em aberto é só a SEQUÊNCIA: corrigi-lo antes ou depois da migalha 14. Recomendo
   ANTES — a migalha 14 bifurca `panic`/`exit`, e bifurcar uma função cujo contrato de `defer` está
   partido é assentar a captura sobre um defeito.
2e. **`cancel` — as três decisões de §17.5**, que são do dono e não desta lane: a forma de `defer`
   (§17.4.3, recomendo (1) na v1 e NUNCA (2)), se se avança já para a propagação estática, e a
   sequência. O que NÃO está em aberto é §17.4.5: `cancel` não pode herdar a isenção de `#must_free`,
   sob pena de introduzir uma fuga silenciosa e repetida num processo que sobrevive.
2d. ~~A NUANCE DE R10~~ — **FECHADA (R14, 2026-07-29): marca de tempo de COMPILAÇÃO** (§6.11.9).
   Deixada riscada e não apagada porque era a última pergunta em aberto deste documento, e o registo
   de que foi respondida vale mais do que a linha limpa. **Não sobra nenhuma pergunta minha ao
   dono**; o que resta em aberto (2c, 2e) é escolha dele sobre superfície futura, não bloqueio.
3. **Se `chan<T>` deve suportar múltiplos recetores.** O harness tem exactamente um. Um canal
   multi-recetor precisa de uma disciplina de fecho diferente (§6.4) e não há caso que o exija.
   Recusado por ausência de necessidade, não por dificuldade.
4. **Roubo de trabalho entre threads.** Deliberadamente ausente (§8): um índice roubado torna o
   escalonamento dependente do tempo, e um escalonamento dependente do tempo não reproduz a falha que
   causou. Volta à mesa com uma MEDIÇÃO de desequilíbrio, não com uma intuição.
5. **Quanto do runtime tem de passar a ser por thread além das quatro variáveis de §6.7.** A tabela de
   internamento (`tk_intern_*`), as posições de diagnóstico de cast (`teko_rt.h`:613) e os sinks de
   cobertura são, cada um, um julgamento próprio. A migalha 0(d) mede; o desenho não adivinha.
6. **A ordem exacta dos bytes da imagem de cobertura escrita em Teko** (§7.4). Tem de ser idêntica à
   que `tk_cov_dump` produz hoje, e isso é um golden a escrever na migalha 16, não uma decisão.
7. **A segunda metade de `--per-test-cov`.** A atribuição por teste melhora de graça com sinks por
   thread, mas reclamá-la exige mudar o que o relatório afirma. Fica para depois da migalha 16, e é
   ganho, não dívida.

---

## 15. Riscos e tensões de lei

### 15.1 Tensões, resolvidas law-first

| tensão | resolução |
|---|---|
| **"ZERO emissão de C" × "a rota C é a referência de comportamento até 0.3.1.4"** | R2 diz *"não pode emitir C **quando for compilar nativo**"*. Logo: sob nativo (o default) zero `.c`, e a catraca fecha a lane a vermelho; a rota C sobrevive **só** sob `TEKO_BACKEND=c`, o seletor já declarado em retirada, e **só** para servir P2/P3. As duas leis passam sem excepção nenhuma. |
| **Lei Teko-only × a captura de panic** | **RESOLVIDA SEM TOCAR EM C**: as duas primitivas nascem em Teko (`src/runtime/teko_rt.tks`), alcançadas por uma arm de `call_symbol`, com o fundo não-guardado em `extern fn` para `write`/`abort` — o mecanismo de `concorrencia-adiantada-s8.md` §4.2, adoptado verbatim. R7 (*"nascem no caminho nativo"*) confirma-o. A primeira redacção deste documento propunha remendar `teko_rt.c`; estava errada e está substituída (§6.5.3). |
| **R7 "conhecidas somente pelo compilador" × o precedente dos builtins injectados** | O precedente (`builtin_fn`, resolução por último segmento) faz EXACTAMENTE o contrário — torna tudo chamável por nome nu, e o doc-comment de `call_symbol` já regista o preço. Seguir o precedente violaria R7, logo **abre-se mecanismo novo** (namespace reservado + regra de prefixo `__`), e o dono tem de o saber: é peso a mais, e é peso obrigatório (§6.5.4). **R10 acrescenta a contenção MAIS FORTE das quatro** — com a bifurcação condicionada à compilação de um teste, num binário normal o ramo guardado nem existe. |
| **R7 (só para testes) × a hipótese de recovery à Go (capacidade permanente)** | **DISSOLVIDA por R10**, e não por eu ter escolhido um lado: *"capturar aborts/panic **sem executá-los**"* é INTERCEPÇÃO, não recuperação, e intercepção não precisa de desenrolar nada. R7 fica intacto e o desenrolador sai (§6.11.6). |
| **`MASTER_PLAN`:262 (não congelar as cinco primitivas sem dado de duplicação) × entregar `chan<T>`** | O dado existe e é R4: o dono nomeou `chan<T>` e nomeou o caso. Congela-se **uma** das cinco, a que foi nomeada; as outras quatro ficam reservadas. |
| **issues-100% × a metade de threads estar bloqueada por `cabi fn`** | Nada fica devendo no DESENHO: as 17 migalhas cobrem a proposta inteira e as fixtures existem para todas. O que está bloqueado é EXECUÇÃO, e está nomeado com endereço (§13). |
| **R1 diz "cada `#test` numa thread isolada" × economia de threads** | Cumprido literalmente: **uma thread por teste**, com no máximo `lanes` em voo. A alternativa (uma thread longa por raia, a varrer `i % lanes`) foi REJEITADA porque um panic mataria a raia e levaria consigo a cauda de testes dela — o oposto do que R1 pede. Uma criação de thread custa dezenas de microssegundos; mil testes pagam dezenas de milissegundos, que é ruído ao lado de uma suíte. |

### 15.2 Riscos, com mitigação

| risco | mitigação |
|---|---|
| Corpos de `#test` **nunca** passaram pelo backend próprio (`lower.tks`:8186 descarta-os) | migalha 0(a) mede ANTES de a migalha 4 depender disso |
| `_Thread_local` pode não bastar para a raiz de arena | migalha 0(d) mede; se não bastar, o custo sobe para o `C4` do desenho anterior e a migalha 13 vira vagão |
| Uma cópia rasa de `TestVerdict` aliasar um `str` da arena que fecha | a recusa de `T` não copiável é de CHECAGEM, não de convenção; `chan_rejects_noncopyable` afirma-a |
| A ordem `copiar → fechar arena → terminar thread` ser invertida por quem implementar | está escrita no doc-comment do guarda e é afirmada por `chan_survives_sender_panic` |
| Um `#test` que imprime interleavar a saída | regra 3 de §8: quem executa não imprime. O corpo escreve para o seu `out_text`, que o pai replica por ordem |
| Dependência entre testes, hoje mascarada pela execução serial | a suíte corre em série desde sempre. **ANTES de ligar o paralelismo**, correr a suíte em ordem INVERTIDA. O que partir ali é dependência real, e teria aparecido como intermitência três meses depois |
| A prova de equivalência expirar com a rota C | §10: P2 e P3 têm data limite (0.3.1.4) e o resultado é ARQUIVADO, não só executado |

### 15.3 Achados adjacentes — REPORTADOS, não convertidos em issue

1. **`teko::env::args()` é vazio em todo o binário do backend próprio** (`lower_virtual_main` cria
   `main` sem parâmetros; `tk_set_args` não é chamado em `src/lir` nem em `src/backend`). Não afecta
   este desenho, e torna impossível qualquer CLI nativo.
2. **`regr_batch_capture` atribui a cada linha uma FATIA do relógio do lote, não uma medição.** O
   próprio doc-comment o diz. Com `spawn`/`wait` a medição por filho passa a ser real — ganho lateral
   da migalha 8.
3. **`tk_cov_dump` recebe `const char *` enquanto o irmão `tk_cov_merge` recebe `tk_str`** — a
   assimetria que gerou a decisão elevada em `gate-sem-c-0.3.0.31.md` §4.2. Este desenho contorna-a
   escrevendo a imagem em Teko; a assimetria fica lá, e alguém há-de tropeçar nela outra vez.
4. **`REGR_GROUP_MIN_MEMBERS` e o colapso em canais** (`regr_group.tks`) reduzem o DENOMINADOR de
   linhas de regressão. `concorrencia-adiantada-s8.md` §7 argumenta que reduzir o denominador vale
   mais do que dividir o numerador. Continua verdade, e é ortogonal a esta carga.

---

## 16. DEGRAU DESCOBERTO — um binário nativo NÃO LÊ a sua linha de comando. E é pior do que "vazio".

Isto começou como nota de rodapé deste desenho (§4: *"o `main` nativo é `new_func("main", 0, …)`"*) e
foi medido a pedido do coordenador. **A medição é pior do que a leitura estática dizia, e isto é um
degrau da lane, não um rodapé.**

### 17.1 O método

`.gen1` construído de `bootstrap/teko.c` (20 s). Projecto de sonda `argvprobe` com `main` virtual
(`exit(probe())`), compilado nas DUAS rotas, corrido com três argumentos.

```teko
pub fn probe(): i32 {
    let a = teko::env::args()
    if a.len == 0 { return 0 }
    if a.len == 1 { return 1 }
    if a.len == 4 { return 4 }
    9
}
```

### 17.2 A saída, colada

```
=== C ROUTE ===
teko: .: built binc/argvprobe
$ ./binc/argvprobe one two three
  C   exit=4 (esperado 4: argv0 + 3)

=== NATIVE ROUTE ===
teko: .: cc failed to link the own-backend object
  NAT exit=127
```

E a causa, com a linha do linker:

```
/usr/bin/ld: warning: binn/argvprobe.o: missing .note.GNU-stack section implies executable stack
/usr/bin/ld: binn/argvprobe.o: in function `teko_argvprobe__probe':
(.text+0x12): undefined reference to `teko_args'
collect2: error: ld returned 1 exit status
```

**Resposta directa à pergunta: NÃO.** Um binário construído com o backend nativo não lê argumentos —
e não é que os leia vazios: **ele nem chega a existir**. O programa não LINKA. Um `teko` construído
nativamente não pode receber `. -o out --release` porque não é produzível de todo.

### 17.3 O achado a mais — a paragem honesta é CONTORNADA pela grafia qualificada

A mesma sonda, com o nome escrito NU em vez de qualificado:

```
=== BARE args() — NATIVE ===
teko: .: native backend N1: builtin `args` not yet lowered (N2) [in `argvprobe::probe`]
=== BARE args() — C ===
teko: .: built binbc/argvprobe   ->   exit=4
```

| grafia | nativo | rota C |
|---|---|---|
| `args()` — **1 segmento** | **paragem honesta** `builtin 'args' not yet lowered (N2)` | funciona, exit 4 |
| `teko::env::args()` — **3 segmentos** | **`undefined reference to 'teko_args'`, no LINKER** | funciona, exit 4 |

A causa é uma linha de `call_symbol` (`src/lir/lower.tks`):

```teko
if segs.len == 1 {
    return error { message = teko::str::concat("native backend N1: builtin `", last, "` not yet lowered (N2)") }
}
```

**A guarda só cobre a grafia NUA.** Um builtin de host escrito qualificado atravessa-a, não é achado
por `find_extern_symbol` (num projecto de utilizador `args` é builtin INJECTADO, não `extern fn`
declarado — a declaração `pub extern fn args(): []str = "tk_rt_args"` vive em `src/env/env.tks`, que
é fonte do COMPILADOR e não do projecto), e cai em `mangle_fn_symbol("", "args")` → `teko_args`, que
símbolo nenhum define.

**A família é maior do que `args`.** Todo o `host_surface_fn` (`src/checker/scope.tks`) tem a mesma
exposição: `read_file`, `write_file`, `list_dir`, `run`, `run_quiet`, `cwd`, `chdir`, `var`,
`set_var`, … Cada um escrito qualificado e não baixado pelo nativo é um símbolo indefinido em vez de
uma paragem com endereço. É exactamente a doença que o doc-comment de `call_symbol` já descreve para
o caso simétrico — *"it would silently fall through to `mangle_fn_symbol(...)` and target a symbol no
function ever defines (a link-time failure at best)"* — e que ali foi fechada só para a família
`teko::list::*`.

### 17.4 Porque é que isto é um DEGRAU da lane e não uma nota deste desenho

- **O compilador é um CLI.** O objectivo declarado da lane é *"as 4 pernas Linux geram NATIVE"* e o
  critério do dono é o gen2 sozinho compilar e correr um hello world. Um gen2 que não lê a sua própria
  linha de comando não recebe `.`, nem `-o`, nem `--release`: não há invocação possível.
- **Não é o degrau 8.** O degrau 8 (`MInst` em `push_box_bytes`) é uma paragem de LOWERING; esta é uma
  falha de LINK e vem depois. As duas são independentes e ambas estão no caminho do gen2.
- **Não bloqueia este desenho** e continua a não bloquear: nenhuma peça do harness usa argv — é por
  isso que a auto-reexecução por `argv[1]` do desenho de memória foi descartada (§4) e que os graus de
  paralelismo vêm do AMBIENTE (§9). O harness está desenhado à volta deste buraco de propósito.

### 17.5 O que fecha o degrau, em duas metades separáveis

1. **`main` com `argc`/`argv` + `tk_set_args`.** `lower_virtual_main` cria `new_func("main", 0, [],
   I32)`; tem de criar a assinatura de duas entradas e chamar `tk_set_args(argc, argv)` antes do corpo
   — que é literalmente o que `codegen.tks` já faz na rota C (`b = cb(b, "    tk_set_args(argc,
   argv);\n")`). O símbolo `tk_rt_args` já existe no runtime e já é alcançável por `extern fn`.
2. **A guarda da paragem honesta deixa de olhar para `segs.len`.** Um callee com `call_ns` vazio que
   não é builtin conhecido, não é `assert`-seed e não é família de lista **é uma paragem honesta,
   qualificado ou nu**. Vale por si, independentemente de (1): converte uma classe inteira de
   símbolos indefinidos em diagnósticos com endereço.

**REPORTADO, ACEITE E JÁ CORRIGIDO.** O dono aceitou (2026-07-29: *"precisa corrigir, ensinar o
native"*) e o vagão `cargo/0.3.1.0-args-native` **aterrou no vagão principal**: o `main` sintetizado
passou a receber `argc`/`argv` e a chamar `tk_set_args`, e `args` foi resolvido no `call_symbol`.
**A medição abaixo fica como o registo do defeito e da sua prova** — não como estado presente. A
correcção não foi desta carga. Nada em
`src/lir` nem em `src/backend` é tocado aqui, para as duas cargas não colidirem. As fixtures que o
afirmariam, deixadas para essa carga: `native_main_reads_argv` (exit 4 com três argumentos, nas duas
rotas) e `qualified_host_builtin_stops_honestly` (a forma qualificada pára com a mesma mensagem que a
nua, em vez de falhar no linker).

**Consequências para ESTE desenho, e são duas:**

1. A exclusão da auto-reexecução por `argv[1]` (§4) era CONDICIONAL e **a condição caiu**: `args()`
   funciona. Continua não escolhida — R1 manda threads para os unitários — mas quem a reabrir já não
   encontra impedimento técnico, só a decisão.
2. A opção (B) de §6.11.9 (marca por argumento em execução) **deixou de estar bloqueada pelo mesmo
   motivo**, e continua não escolhida por R14. As duas coisas estão registadas lá.

---

## 17. `cancel(error | null)` — a primitiva PÚBLICA (R11). Desenho, para outra versão.

**MUDANÇA DE CATEGORIA, e é a primeira coisa a dizer.** As duas primitivas de R7 são invisíveis,
só-testes, *"conhecidas somente pelo compilador"*. **`cancel` é o oposto**: o dono escreve-o em letra
— *"pode (e deve) ser utilizado por outros desenvolvedores"*. Portanto **R7 não se lhe aplica**, o
mecanismo de contenção de §6.5.4 não se lhe aplica, e o padrão de qualidade sobe: o que passa numa
primitiva escondida não passa numa palavra que toda a gente vai escrever.

### 18.1 A matriz

|  | **em thread (raia guardada)** | **em processo (não guardado)** |
|---|---|---|
| `cancel(e)` | cancela a raia, com `e` como motivo | **panic**, com `e` como mensagem |
| `cancel(null)` | cancela a raia, sem motivo | **`exit(1)`** |

```teko
/**
 * cancel — interrompe o FLUXO corrente: a raia, quando há uma; o processo, quando não há.
 *
 * É a saída VOLUNTÁRIA e é PÚBLICA — ao contrário da captura de `panic`/`exit` (§6.5), que é
 * involuntária e só existe sob compilação de teste. As duas terminam a raia pelo MESMO caminho e
 * depositam veredicto pelo MESMO canal; a diferença é quem as chama e porquê.
 *
 * O comportamento depende de haver ou não uma raia guardada para a thread chamadora, e essa é a
 * ÚNICA condição — não há modo, não há flag, não há variável de ambiente:
 *
 * - **numa raia**, com `reason` presente, a raia termina e `reason` vai no veredicto; com `reason`
 *   nulo, a raia termina sem motivo registado. O PROCESSO SOBREVIVE, que é o ponto inteiro.
 * - **fora de uma raia**, com `reason` presente é um `panic` com a mensagem de `reason`; com
 *   `reason` nulo é `exit(1)`. A assimetria é deliberada e está justificada em §17.8.
 *
 * @param reason  o motivo do cancelamento, ou `null` para cancelar sem motivo
 * @return        nada; a chamada não retorna
 * @since (a decidir — ver §17.10)
 */
pub fn cancel(reason: error | null)
```

### 18.2 Como é que `cancel` sabe onde está? **É a MESMA tabela. Verificado, não presumido.**

A pergunta é: existe uma noção em runtime de "sou uma raia?". Existe, e é exactamente a tabela de
guardas de §6.5.2 — `gate_guard_begin` escreve a linha, `sys_thread_self()` procura-a,
`gate_guard_end` limpa-a. `cancel` faz **a mesma varredura**: linha encontrada → caminho de raia;
linha ausente → caminho de processo.

**Não precisa de tabela nova, nem de campo novo, nem de estado novo.** A linha já carrega
`{thread_id, index, state, code, message, site}`; `cancel` escreve `state` e `message` e mais nada.

**Isto é um argumento forte a favor do desenho inteiro, e vale dizê-lo:** a mesma máquina serve a
captura INVOLUNTÁRIA (um teste que entra em panic sem querer) e a saída VOLUNTÁRIA (`cancel`). Duas
peças que pareciam independentes partilham o único estado de runtime que ambas precisam. Se
precisassem de tabelas diferentes, seria sinal de que uma delas estava mal desenhada.

**Uma consequência que tem de ser dita:** hoje a tabela só é POPULADA sob compilação de teste (R10).
Se `cancel` for pública e usável fora de testes, **a tabela tem de existir sempre que houver raias** —
isto é, deixa de ser artefacto de teste e passa a ser parte do runtime de concorrência. Isso não a
torna maior nem mais cara (uma linha por raia viva, varredura linear sobre uma dezena), mas move-a de
sítio no mapa: sai de "andaime de gate" para "estado de tarefa". A bifurcação de R7 continua condicional
à compilação de teste; a TABELA passa a ser condicional a haver concorrência.

### 18.3 `cancel` como a CARA PÚBLICA da guarda — aguenta-se, e diverge em um ponto

A simetria é real:

| | captura (R7) | `cancel` (R11) |
|---|---|---|
| quem dispara | o código, sem querer | o programador, de propósito |
| visibilidade | invisível, só o compilador | **pública** |
| existe fora de teste? | não | **sim** |
| como termina a raia | deposita e termina | **igual** |
| por onde vai o veredicto | tabela de guardas → `chan<T>` → casa do índice | **igual** |
| estado depositado | `Panicked` / `Exited` | **`Cancelled`** — estado NOVO (§17.4) |

**Onde DIVERGE, e é o único sítio:** a captura é um efeito colateral de uma terminação que já ia
acontecer; `cancel` é uma chamada que o compilador VÊ no sítio. Essa diferença parece pequena e é o
eixo de toda a §17.4 — é ela que permite a `cancel` correr `defer` que um `panic` nunca poderá correr.

`TestState` ganha portanto um membro, e a razão é a mesma que separou `Failed` de `Panicked`: distinguir
na ORIGEM em vez de por inspecção de texto.

```teko
    /** o fluxo foi interrompido por uma chamada explícita a `cancel` — voluntário, não uma falha. */
    Cancelled
```

**Política:** num `#test`, `Cancelled` é **vermelho** pela mesma regra que `Exited` (§6.5.6) — o
contrato de um teste é retornar normalmente. Fora de um teste, `cancel` não tem política nenhuma: é
controlo de fluxo do programa.

### 18.4 A PERGUNTA DURA — os `defer` correm quando uma raia é cancelada?

#### 18.4.1 O que a maquinaria já dá, medido no código

Sob `panic` **não correm** — medido em §6.11.1, nas duas rotas. A causa é que `panic` não tem sítio de
chamada garantido: uma divisão por zero, um índice fora de limites ou um `abort` da libc entram no
caminho de terminação sem que o compilador saiba onde.

**`cancel` é outra coisa: é SEMPRE uma chamada explícita, e o compilador vê-a.** E a maquinaria de
saída léxica que já existe é geral — `replay_defers(ctx, 0)` é invocada em **onze** sítios distintos
de `src/lir/lower.tks`, um por cada construção que o compilador reconhece como saída de escopo:

| sítio | construção |
|---|---|
| `lower_return` | `return` |
| `close_lambda_body` / fecho do corpo de função | queda pelo fim |
| `lower_break` / `lower_continue` / saída de laço | `break`, `continue` |
| braço de `match` com valor | a cauda do braço |
| cauda do virtual-main (três sítios) | fim do programa |

**Conclusão da investigação: sim, mecanicamente `cancel` pode correr `defer` — e é grátis.** Lowerá-lo
é copiar `lower_return` linha por linha: `replay_defers(ctx, 0)` e depois o terminador. Nenhuma peça
nova, nenhum desenrolador, nenhuma tabela.

#### 18.4.2 O LIMITE, e é ele que decide se a primitiva é usável ou uma armadilha

`ctx.defers` é, pelo doc-comment de `LowerCtx`, *"the **CURRENT FUNCTION'S** pending-defer stack …
**reset per function/lambda body**"*.

Portanto `replay_defers(ctx, 0)` corre os `defer` **do frame onde `cancel` está escrito, e só desse**.

```
raia -> task()          defer { close(f) }      <- NÃO corre
          -> step()     defer { unlock(m) }     <- NÃO corre
             -> leaf()  defer { close(g) }      <- corre
                cancel(e)
```

**Sem desenrolamento, nenhuma primitiva consegue correr os `defer` dos frames chamadores. É um facto
da arquitectura, não uma escolha.** E o dono acabou de tirar o desenrolador da mesa (R10), com razões
que continuam válidas.

#### 18.4.3 As três formas possíveis, e porque a do meio é a pior

| forma | o que promete | veredicto |
|---|---|---|
| **(1) nenhum `defer` corre** | nada — igual a `exit`/`panic` hoje | previsível e consistente. Não mente |
| **(2) só os do frame de `cancel`** | limpa "alguma coisa" | **A PIOR DAS TRÊS.** Parece que limpa, limpa só um bocado, e o programador deixa de verificar. Meia-limpeza é mais perigosa que nenhuma, porque desliga a atenção sem desligar o problema |
| **(3) todos os `defer` até à entrada da raia** | o que um programador espera | é o certo, e exige propagação — ver §17.4.5 |

**Recomendação: (1) numa v1, NUNCA (2)**, e (3) como destino declarado. A consistência é o argumento:
hoje `exit` e `panic` não correm `defer` nenhum; `cancel` a correr *alguns* introduziria três
comportamentos diferentes para três terminações, e ninguém decora isso certo.

#### 18.4.4 O que MITIGA (1), e é preciso dizer para a recomendação não parecer resignação

**A memória não é o problema — a arena resolve-a por inteiro.** Uma raia cancelada tem a sua região
libertada na íntegra (§17.6). Nenhum `defer` é necessário para memória, ao contrário de uma linguagem
com `malloc`/`free` manuais, onde (1) seria inaceitável.

O que fica exposto são **alças do host**: ficheiros, sockets, locks. Hoje a superfície é pequena
(`teko::io` trabalha por ficheiro inteiro, sem alça persistente). **Mas está a crescer, e o projecto já
tem o vocabulário:** `#must_free` — e é aí que está o achado a seguir.

#### 18.4.5 O ACHADO QUE MUDA O PESO DA DECISÃO — `#must_free` já trata divergência como segura, e para `cancel` isso é FALSO

`src/checker/typer.tks` verifica que toda a alça `#must_free` é consumida antes do fim do escopo. E o
seu doc-comment diz, textualmente:

> *"A path that DIVERGES (`break`/`continue`/a trailing **`panic`/`exit`** call) before reaching the
> block's end **never drops the value, so it needs no consume**"*

E nomeia o idioma canónico: *"`mut h = make(); defer { mem::free(h) }`"* — **exactamente a construção
que `cancel` não correria.**

**Porque é que "divergência é segura" é VERDADE para `panic`/`exit` e FALSO para `cancel`:**

| | `panic` / `exit` | `cancel` numa raia |
|---|---|---|
| o que morre | **o processo inteiro** | **só a raia** |
| quem recupera a alça vazada | **o sistema operativo**, ao fechar o processo | **ninguém** |
| a fuga acumula? | não — acontece uma vez e o processo acaba | **SIM — uma por raia cancelada, para sempre** |

Ou seja: se `cancel` for acrescentada ao conjunto de divergência sem mais nada, **o verificador de
`#must_free` deixa de exigir consumo num caminho que agora vaza de verdade** — e vaza em silêncio,
repetidamente, num processo que sobrevive. **Isto é uma regressão de segurança de memória introduzida
por uma primitiva de conveniência**, e é o tipo de coisa que só se vê antes de a escrever.

**Portanto, seja qual for a forma escolhida em §17.4.3, uma coisa não é opcional:**

> **`cancel` NÃO pode entrar no conjunto de divergência de `#must_free` com o mesmo estatuto de
> `panic`/`exit`.** Ou o verificador passa a EXIGIR que toda a alça `#must_free` viva seja consumida
> antes de um `cancel` alcançável (paragem honesta, na compilação), ou a forma (3) é construída para
> que o `defer` canónico corra mesmo. **Silêncio não é opção.**

Isto é também o argumento mais forte a favor de (3) — e a via para lá, sem desenrolador, é
**propagação estática**: uma função que pode cancelar declara-o na assinatura (como um efeito), e em
cada sítio de chamada dessa função o compilador emite "se cancelou → replay dos MEUS defers →
propaga". É desenrolamento feito em tempo de COMPILAÇÃO, com zero maquinaria de runtime — o custo é um
teste por chamada e superfície de assinatura. **Não a desenho aqui** (é sistema de efeitos, e é
vagão), mas registo que é o caminho, e que **async/await vai precisar de propagação de efeitos de
qualquer maneira** — o que faz de (3) um investimento partilhado e não um imposto de `cancel`.

### 18.5 O que fica por decidir, e é do dono

1. **Qual das três formas de §17.4.3.** A minha recomendação é (1) na v1 com (3) declarada como
   destino; nunca (2).
2. **Se se avança já para (3)**, isso é um vagão de sistema de efeitos e deve ser sequenciado com
   async/await, não com o harness.
3. Seja qual for: **§17.4.5 não é negociável** — `cancel` não pode herdar a isenção de `#must_free`.

### 18.6 A arena de uma raia cancelada, e a mensagem que a atravessa

A mesma resposta que aguentou o panic (§6.7), e é preciso verificar que aguenta este caso — aguenta,
pela mesma razão:

- **Quem fecha:** o guarda, na ordem obrigatória — **copiar o motivo para a linha da tabela (que é da
  região do PAI) → libertar a região da raia → terminar a thread.** Invertida, o `str` do motivo é lido
  de memória já libertada.
- **A mensagem sobrevive** porque **nunca esteve na região da raia**: a linha da tabela é do pai, e o
  `TestVerdict` que segue para o `chan<T>` é **copiado para a região do RECETOR** (§6.3). É exactamente
  o caso que aquela decisão foi desenhada para aguentar, agora com um segundo cliente.
- **O `error` que `cancel` recebe** é um valor da raia — o seu campo `message` é um `str` na região da
  raia. É copiado como todos os outros, com o mesmo limite `TEST_TEXT_CAP` e a mesma truncagem visível.

**Verificação feita: a decisão de §6.3 aguenta `cancel` sem uma alteração.** Se não aguentasse, seria
sinal de que aguentava o panic por acidente.

### 18.7 `cancel(e)` num processo faz panic — logo herda a dependência do `flush_out`

§6.11.1 mediu: sob `abort()` o `stdout` com buffer **não é descarregado** — o `BEFORE` da sonda, que
foi executado, não apareceu. `cancel(e)` fora de uma raia É um panic, portanto **a mensagem de um
`cancel` num processo pode perder-se exactamente da mesma maneira**, e junto com ela tudo o que o
programa tinha escrito antes.

**Dependência nomeada:** o builtin `flush_out` (migalha 3) é pré-requisito de `cancel` ser diagnosticável
fora de uma raia. Para uma primitiva pública isto pesa mais do que para o gate: um programador que
chama `cancel(error { message = "..." })` e não vê a mensagem conclui que a primitiva não funciona.

### 18.8 A assimetria `panic` × `exit(1)` — e ela faz sentido

`cancel(e)` num processo → **panic**; `cancel(null)` → **`exit(1)`**. Parece arbitrário e não é:

- **com motivo, há o que dizer.** Um `panic` escreve a linha `TK_PANIC_MARKER` com a mensagem e imprime
  traço. Um `exit(1)` é mudo — deitaria fora o `error` que o chamador se deu ao trabalho de construir.
- **sem motivo, não há o que dizer.** Um `panic` com mensagem vazia produziria uma linha de panic oca e
  um traço de pilha para um evento que o programa pediu de propósito. `exit(1)` é a saída honesta:
  terminou, sem sucesso, sem história.
- **e os códigos distinguem-se**: 134 (SIGABRT) para "houve um motivo", 1 para "não houve". Um script
  que chama o programa consegue separá-los sem ler texto nenhum.

A regra por trás, que generaliza: **o canal de saída é escolhido pela existência de informação, não
pela gravidade.** Havendo informação, usa-se o caminho que a transporta.

### 18.9 A ponte para async/await — o que `cancel` já compromete e o que deixa aberto

O dono deu esta razão para a peça existir: *"isso vai valer lá na frente quando tivermos
async/await"*. Sem desenhar async aqui, o que a forma de `cancel` **já compromete**:

1. **O cancelamento é do FLUXO, não de um objecto.** `cancel` não recebe alça de tarefa — cancela o
   fluxo corrente. Isso exclui, por construção, um `t.cancel()` de fora sobre uma tarefa alheia:
   cancelar é sempre algo que o próprio fluxo faz a si mesmo. É coerente com R6 (*"sem ref em threads
   … em isolation principalmente"*): cancelar de fora exigiria uma alça partilhada e mutável, que é
   precisamente o que R6 proíbe.
2. **O motivo é um `error`, portanto atravessa por CÓPIA** (§17.6), como tudo o resto.
3. **O comportamento é decidido pelo CONTEXTO** (há raia? não há?), não por um modo. Uma tarefa `async`
   será mais um contexto na mesma tabela.

O que fica **ABERTO**, e é honesto listá-lo em vez de fingir que a peça o resolve:

- **`cancel` numa tarefa SUSPENSA.** Uma tarefa parada num `await` não está a executar, logo não há
  frame onde a chamada aconteça. Cancelá-la é uma operação de FORA, e §17.9(1) acabou de excluir a
  forma de fora. **Falta a peça, e ela não é `cancel`** — é um cancelamento cooperativo (a tarefa
  observa um pedido no ponto de retoma e chama `cancel` ela própria). Nomeio-o; não o desenho.
- **Propagação por uma árvore de tarefas.** Cancelar um pai cancela os filhos? `scope { }`, reservada,
  é o sítio natural para essa resposta.
- **`cancel` dentro de um `defer`.** Reentrância; o checker já proíbe `return`/`break`/`continue`/`defer`
  dentro de um `defer` e este é o mesmo tipo de pergunta.
- **Se `cancel(null)` numa raia é distinguível de a raia terminar normalmente.** No desenho de §17.3 é:
  `Cancelled` × `Ok`. Numa tarefa `async`, quem lê essa diferença ainda não existe.

### 18.10 Em que versão cabe

**Não é 0.3.1.x e não é o harness.** `cancel` depende de haver raias (migalhas 12-16) e a sua metade
interessante — a de thread — não existe antes delas. E a decisão de §17.4.3, se for (3), é um vagão de
sistema de efeitos.

| peça | degrau |
|---|---|
| a tabela de guardas que `cancel` reutiliza | **0.3.2** (migalha 14, já planeada) |
| `cancel` com a matriz de §17.1, forma (1) | **0.3.2**, depois da migalha 16 — é pequena, uma vez que a tabela exista |
| a regra de `#must_free` de §17.4.5 | **junto com `cancel`, obrigatoriamente** — nunca depois |
| a forma (3) por propagação estática | **com async/await**, não antes |

**Fixtures que a afirmariam, deixadas escritas para quem a implementar:**

| fixture | forma | esperado |
|---|---|---|
| `cancel_in_lane_reports_reason` | uma raia chama `cancel(error{...})`; as outras completam | não-zero, **e** a raia com `Cancelled` + o motivo, **e** as outras reportadas |
| `cancel_in_lane_null_has_no_reason` | `cancel(null)` numa raia | `Cancelled` com motivo vazio, processo vivo |
| `cancel_outside_lane_with_reason_panics` | `cancel(error{...})` num programa sem raias | 134 **e** a linha `TK_PANIC_MARKER` com a mensagem |
| `cancel_outside_lane_null_exits_one` | `cancel(null)` num programa sem raias | **1**, sem linha de panic |
| `cancel_process_survives_lane_death` | N raias, metade cancela | o processo devolve veredicto para as N |
| `cancel_must_free_is_not_exempt` | uma alça `#must_free` viva num caminho que alcança `cancel` | 1 (erro de compilação) — §17.4.5 |
| `cancel_defer_contract` | um `defer` no frame de `cancel` e outro num frame chamador | **o que a forma escolhida em §17.4.3 prometer, e nada mais** |

---

## 18. BUG CONFIRMADO — `panic` e `exit` não disparam o `defer` do escopo

> *"O que, sinceramente, deveria acontecer, `panic`, `exit`, `return`, `break` e `continue` deveriam
> disparar o defer do escopo sempre, se não estão, temos BUG"* — dono, 2026-07-29

**Verificado. Três dos cinco funcionam; dois estão partidos. É bug, e a correcção é LIGAR
informação que o compilador já calcula — não construir maquinaria nova.**

**NÃO corrigido aqui** (sou o arquitecto). Diagnóstico completo + sequência para quem corrigir.

### 18.1 A MATRIZ MEDIDA — cinco saídas × dois escopos × duas rotas

Sondas por **`eprintln`/stderr** (não-bufferizado — o instrumento que sobrevive ao `abort()`; a
sonda anterior por `println` estava confundida, §6.11.1).

| saída | `defer` no MESMO escopo | `defer` em escopo EXTERIOR | rota C | nativo |
|---|---|---|---|---|
| `return` | ✅ **corre** | ✅ **corre** | ✓ | ✓ |
| `break` | ✅ **corre** (o do corpo do laço) | ✅ **corre** (o da função) | ✓ | ✓ |
| `continue` | ✅ **corre** (uma vez por iteração) | ✅ **corre** (o da função) | ✓ | ✓ |
| **`panic`** | ❌ **NÃO CORRE** | ❌ **NÃO CORRE** | ✗ | ✗ |
| **`exit`** | ❌ **NÃO CORRE** | ❌ **NÃO CORRE** | ✗ | ✗ |

Saída literal, rota C:

```
== RETURN ==            == BREAK ==                  == CONTINUE ==
  in f_return             in loop (break)              continue taken
D:return-fn             D:break-inloop               D:continue-inloop
                          after loop (break)         D:continue-inloop
                        D:break-outerfn              D:continue-outerfn
```
```
== PANIC ==                          == EXIT ==
  in inner                             in inner
teko: deliberate panic: boom         --- exit=5
--- exit=134
                     (nem D:inner-fn nem D:outer-fn, em nenhum dos dois)
```

Nativo: **idêntico nos cinco**. Não é defeito de rota — é do modelo partilhado.

### 18.2 A CAUSA — a hipótese estrutural, confirmada no código

**`return`, `break` e `continue` são PALAVRAS-CHAVE; `panic` e `exit` são CHAMADAS.** A maquinaria de
replay só conhece as palavras-chave.

Os **dez** sítios de `replay_defers` em `src/lir/lower.tks` dividem-se em exactamente dois grupos, e
não há um terceiro:

| grupo | sítios | disparado por |
|---|---|---|
| **por PALAVRA-CHAVE** | `lower_return`, `lower_break`, `lower_continue` | `return` / `break` / `continue` |
| **por QUEDA LÉXICA** | fecho de corpo de função/lambda, saída de laço, braço de `match`, três caudas de virtual-main | chegar ao fim do escopo |
| **por CHAMADA DIVERGENTE** | **NENHUM** | — |

E o C emitido mostra a consequência exacta (sonda compilada em scratchpad e **lida**; nada gerado foi
commitado):

```c
int32_t teko_deferprobe__body(bool fail) {
    tk_eprintln((tk_str) { (const tk_byte *)"BEFORE", 6 });
    if (fail) {
        tk_panic_str((tk_str) { (const tk_byte *)"probe", 5 });   /* _Noreturn — nunca regressa */
    } tk_eprintln((tk_str) { (const tk_byte *)"DEFER RAN", 9 });  /* o defer, na QUEDA LÉXICA */
    return ((int64_t)7ULL);
}
```

O corpo do `defer` **é emitido** — mas na queda léxica, **depois** do sítio da chamada. Como
`tk_panic_str` é `_Noreturn`, o controlo nunca lá chega. **Não é `defer` em falta: é uma saída não
reconhecida como saída.**

*(`bootstrap/teko.c` não serve de prova aqui, e o facto merece registo: **o compilador não usa `defer`
em lado nenhum do seu próprio código** — `grep 'defer {' src/**/*.tks` só devolve menções em
comentários e no AST. Não há no artefacto versionado nenhuma composição `defer`+`panic` para ler.)*

### 18.3 A CORREÇÃO É LIGAR, NÃO CONSTRUIR — a informação já está calculada

O compilador **já sabe** que aquelas duas chamadas são saídas. `src/checker/typer.tks`:

```teko
// global builtins panic/exit, unqualified.
fn texpr_diverges(e: TExpr): bool {
    match e.kind {
        TCall as c => c.callee.segments.len == 1 && (c.callee.segments[0].name == "panic" || c.callee.segments[0].name == "exit")
```

O facto existe, está computado, e é a **mesma fronteira** que R10 escolheu para a bifurcação
(§6.11.8). **O `replay_defers` simplesmente não o consome.** Ligar as duas coisas é a correcção
inteira — nenhum desenrolador, nenhuma pilha de runtime, nenhuma estrutura nova.

### 18.4 O LIMITE QUE A CORREÇÃO **NÃO** REMOVE — e é obrigatório dizê-lo

`ctx.defers` é, pelo doc-comment de `LowerCtx`, *"the **CURRENT FUNCTION'S** pending-defer stack …
reset per function/lambda body"*. Logo a correcção restaura os `defer` **do frame onde a saída está
ESCRITA**, e só desse.

```
probe()   defer { A }                  <- continua a NÃO correr
  -> inner()   defer { B }             <- passa a correr  ✅
       panic("boom")
```

**Consequência que ninguém pode ignorar:** `teko::assert::is_true` chama `panic` **dentro de si**.
Depois da correcção, um `#test` com `defer` que falhe uma asserção **continua** a não correr o seu
`defer` — o panic acontece no frame do `assert`, não no do teste. Correr frames chamadores exige
desenrolamento, que R10 tirou da mesa por razões que continuam válidas.

**A assimetria residual, e porque é ACEITÁVEL aqui** (revendo o meu próprio argumento de §6.11.1d):

| | corre `defer` depois da correcção? |
|---|---|
| `exit(n)` — sempre chamada explícita | **SIM, sem excepção** |
| `panic("...")` explícito | **SIM**, no frame onde está escrito |
| panic IMPLÍCITO — div/0, índice fora de limites, cast, overflow, OOM | **NÃO** — não há sítio de chamada que o compilador veja |

Eu tinha argumentado que meia-limpeza é pior do que nenhuma. **Para `panic`/`exit` esse argumento é
FRACO e retiro-o**, por um motivo concreto: nos dois casos **o processo morre**, portanto o SO
reclama tudo o que ficou por fechar. A meia-limpeza só é armadilha quando o processo SOBREVIVE — que
é o caso do `cancel` (§17.4.5), não este. O argumento estava certo; estava aplicado ao caso errado.

### 18.5 A SEQUÊNCIA PARA QUEM CORRIGIR

| # | passo | nota |
|---|---|---|
| **1** | **Fixtures PRIMEIRO, a falhar.** Os cinco casos × dois escopos, marcados por **`eprintln`/stderr** — nunca `println`: `abort()` não faz flush e a sonda mente (§6.11.1) | as três de palavra-chave passam já; as duas de chamada falham. É o produto do passo |
| **2** | **Expor o predicado.** `texpr_diverges` vive no checker; o lowering precisa da mesma pergunta. Exportar, ou espelhar a fronteira num sítio único partilhado — **nunca duplicar a lista de nomes** | duas listas divergem; é a doença deste repositório |
| **3** | **Ligar no LIR.** Em `lower_stmt`, uma statement de expressão cuja chamada diverge → `replay_defers(ctx, 0)` **ANTES** de baixar a chamada | é o corpo de `lower_return` sem o valor de retorno |
| **4** | **Ligar no emissor C.** O mesmo em `emit_stmt`/`emit_defers` (`src/codegen/codegen.tks`) | **as duas rotas têm de mudar juntas** — divergirem seria pior que o bug |
| **5** | **A queda léxica não pode replicar duas vezes.** Hoje o corpo é emitido na queda; se a chamada passar a replicar antes, verificar os guardas `block_terminated`/`tail.terminated` para não haver dupla emissão | duplicar um `defer` que liberta memória é pior que não o correr |
| **6** | **Fixpoint.** O compilador não usa `defer`, portanto os seus próprios bytes **não devem mudar**. `gen2 == gen3` byte-idêntico é o portão | se mudarem, alguma coisa a mais mexeu |
| **7** | **`#must_free`.** O verificador isenta caminhos divergentes (§17.4.5). Com `defer` a correr antes do `panic`/`exit`, o idioma canónico `defer { mem::free(h) }` passa a ser honrado — **confirmar que a isenção continua correcta e não passou a esconder outra coisa** | |

**Fixtures nomeadas:** `defer_runs_on_return`, `defer_runs_on_break`, `defer_runs_on_continue`,
`defer_runs_on_explicit_panic`, `defer_runs_on_exit`, `defer_outer_scope_on_each_exit`,
`defer_not_duplicated_on_fallthrough`, `defer_implicit_panic_documented` (afirma o limite de §18.4,
para que a assimetria seja CONTRATO e não surpresa).

### 18.6 CONSEQUÊNCIAS NO RESTO DO DOCUMENTO

**§6.11 (recovery à Go).** A recomendação contra o desenrolador **mantém-se**, e o dono já a ratificou
por mérito próprio (R10: interceptar, não recuperar). **Mas o argumento muda e não o defendo com a
medição furada:** já não é *"os `defer` não correm, logo o modelo é estático e distante do Go"*. É
*"os `defer` correm em três das cinco saídas e vão correr nas cinco depois desta correcção — e mesmo
assim isso NÃO é o recovery do Go, porque continua a ser por frame e o Go desenrola a pilha inteira"*.
A distância ao Go é **frames**, não `defer`.

**§17 (`cancel`).** A pergunta dura **dissolve-se em parte, e só em parte** — e digo-o assim porque
overclaimar aqui seria repetir o erro da primeira medição:

| caso | depois da correcção |
|---|---|
| `cancel` escrito no frame de entrada da raia | **os `defer` desse frame correm.** Sem armadilha, sem trabalho extra — `cancel` entra como a **sexta saída** e herda o comportamento |
| `cancel` escrito N frames abaixo | os `defer` dos frames chamadores **continuam a não correr** |

Portanto a forma **(2)** de §17.4.3 — "só os do frame do `cancel`" — deixa de ser uma escolha
esquisita e passa a ser **a consequência natural e consistente da regra dos cinco**: toda a saída
corre os `defer` do seu escopo. **Revejo a recomendação de §17.4.3:** com o bug corrigido, a v1 de
`cancel` deve ser a forma (2) — não porque meia-limpeza seja boa, mas porque passa a ser a MESMA
semântica que `return`, `break`, `continue`, `panic` e `exit` têm, e uma sexta saída com regra
própria é que seria a armadilha.

**O que NÃO muda:** §17.4.5 continua inteiro e continua a ser o ponto não-negociável — `cancel` não
pode herdar a isenção de `#must_free`, porque no caso dele **o processo sobrevive** e a fuga acumula.

---

## 19. Como este documento se verifica

Toda afirmação de código acima é reproduzível com a árvore em mãos:

```sh
grep -n 'fn run_native_gate' -A 34 src/build/project.tks       # emite C incondicionalmente: §2
grep -n 'TEKO_BACKEND_C_VALUE\|fn c_backend_selected' src/build/project.tks   # o resolvedor que o gate não consulta: §2
grep -n 'if f.is_test' src/lir/lower.tks                       # os corpos de teste são descartados: §3.2
grep -n 'new_func("main", 0' src/lir/lower.tks                 # main sem argv: §4
grep -rn 'tk_set_args' src/lir src/backend                     # zero ocorrências: §4
grep -n 'pub extern fn run' src/process/process.tks            # só síncrono, sem redirecção: §5.2
grep -n 'fn regr_batch_script\|fn sh_squote\|fn to_sh_path' src/build/regression.tks   # o andaime que morre: §5.4
grep -n '_Noreturn void tk_panic\|_Noreturn void tk_exit' src/runtime/teko_rt.h        # matam o processo: §6.5.1
grep -n 'tk_g_root\|tk_g_regs\|tk_arena_marks' src/runtime/teko_rt.c                   # estáticos de processo: §6.7
grep -n 'tk_cov_ids\|tk_cov_n \|tk_cov_cap' src/runtime/teko_rt.c                      # sinks de processo: §7
grep -n 'REGR_JOBS_DEFAULT\|REGR_JOBS_ENV' src/build/regression.tks                    # o botão que fica: §9
grep -n 'panic("assertion failed' src/assert/assert.tks                                # asserção É panic hoje: §6.5.5
sed -n '116,128p' src/runtime/teko_rt.c                                                # o "catch" global que trata e NÃO interrompe: §6.5.1
grep -n 'fn builtin_fn' -A 6 src/checker/scope.tks                                     # builtins resolvidos por ÚLTIMO SEGMENTO: §6.5.4
grep -n 'bare name happened to match a builtin' src/lir/lower.tks                      # o preço já pago por isso: §6.5.4
grep -n 'fn count_prod_fns' src/coverage/coverage.tks                                  # o denominador de cobertura a proteger: §6.5.4
grep -n 'fn texpr_diverges' -B 2 -A 4 src/checker/typer.tks                            # a fronteira "global panic/exit, unqualified" JA existe: §6.11.8
grep -n 'rt_abort\|"abort" from "c"' src/runtime/teko_rt.tks                           # directa do SO nº1: §6.11.8
grep -n 'name == "abort"' src/checker/scope.tks                                        # directa nº2 (o builtin injectado): §6.11.8
grep -n 'void tk_exit\|_exit(127)\|_Exit(128' src/runtime/teko_rt.c                    # directas nº3, nº4, nº5: §6.11.8
grep -n 'panic_div0\|panic_oob\|panic_cast\|panic_overflow' src/runtime/teko_rt.tks    # as guardas que herdam a bifurcacao: §6.11.8
grep -n 'replay_defers' src/lir/lower.tks                                             # ONZE saidas lexicas — a maquinaria que `cancel` pode reusar: §17.4.1
grep -n "CURRENT FUNCTION'S pending-defer" src/lir/lower.tks                          # o LIMITE: a pilha e por FUNCAO: §17.4.2
grep -n 'never drops the value, so it needs no consume' src/checker/typer.tks         # `#must_free` isenta divergencia — FALSO para cancel: §17.4.5
grep -n 'fn lower_return\|fn lower_break\|fn lower_continue' src/lir/lower.tks         # os TRES sitios por PALAVRA-CHAVE: §18.2
grep -rn 'defer {' --include=*.tks src/ | grep -v '\*'                                 # VAZIO: o compilador nao usa defer: §18.2
```

Se alguma dessas leituras divergir do que está escrito aqui, **o documento está errado e deve ser
corrigido antes de ser seguido** — não contornado.
