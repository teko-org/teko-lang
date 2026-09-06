---
section: design
created: 2026-08-03
source: ruling do owner ("isolate/spawn: SIM, ratificado" + pedido de avaliação async/await como
  açúcar, 2026-08-03), docs/design/concorrencia-adiantada-s8.md (L0/L1, branch cargo/20-concorrencia-adiantada,
  já em árvore), docs/design/journaling-de-corrida-0.3.1.md §16–§29 (branch
  cargo/0.3.1.0-journal-testparallel-close, NÃO nesta árvore ainda), docs/medicoes/canais-nativos-3-plataformas.md
  (mesma branch, commit 58c76fa3), TEKO_MASTER_PLAN.md §S8/ASYNC (:659) e §GATE (:674),
  examples/regressions/bulk/src/q200_arena_size_directive_ok/body.tks (o precedente `#arena_size`),
  src/codegen/codegen.tks:8828-8842
status: DESENHO — nenhuma linha de produto proposta nesta carga; só linguagem/semântica, para o
  consumidor journaling-de-corrida
branch: cargo/0.3.1.0-concurrency-design
---

# `isolate` / `spawn` / `chan` — desenho de linguagem, e a avaliação `async`/`await`

> *"tem que adiantar... isso até permite que possamos executar a compilação em mais threads"* —
> owner, 2026-07-27 (a carga que produziu `docs/design/concorrencia-adiantada-s8.md`)
>
> *"isolate/spawn: SIM — é o modelo de tarefas ISOLADAS, ratificado... resolve a tensão que o
> produto apontou: o canônico com isolate/spawn está CERTO, não conflita."* — owner, 2026-08-03
>
> *"avaliar: faz sentido ter async/await (o `Intent<T>`) como açúcar para threads NÃO-ISOLADAS?"*
> — owner, 2026-08-03

## 0. O que este documento é, e o que não é

**É** desenho de linguagem: onde `isolate`/`spawn`/`chan` entram (ou não) no lexer e no parser, a
semântica de isolamento que os sustenta, o mapeamento de `chan` sobre o transporte nativo do SO por
plataforma, a atomicidade de `push`/`pop`, o fecho por `waitgroup`, e a avaliação pedida de
`async`/`await`/`Intent<T>` como açúcar. **Não é** implementação — nenhuma linha de C, de Teko, de
lexer ou de parser é escrita ou alterada aqui. O consumidor concreto é o
`teko::journal`/fan-in de `docs/design/journaling-de-corrida-0.3.1.md` (branch
`cargo/0.3.1.0-journal-testparallel-close`, ainda não mesclada nesta árvore) — a superfície de
`chan` que o dono deu na tarefa **já é o desenho fechado desse documento**, e este texto a promove
de "o que o journal precisa" para "a primitiva de linguagem", sem reabrir o que já foi medido lá.

Este documento segue o protocolo `counter-argue`: pesquisei o histórico antes de escrever uma
linha, e a §1 é esse registro — inclusive de uma tensão real entre duas rulings do dono que **ele
próprio já resolveu** nesta tarefa, e que eu confirmo, não invento.

---

## 1. O histórico pesquisado — a tensão, e por que ela se dissolve

### 1.1 As duas rulings, lado a lado

**Ruling A — 2026-06-30, `S8/ASYNC` (`TEKO_MASTER_PLAN.md:659`):** unifica async(I/O) e
concorrência(threads) sob `Intent<T>`/`Intent`. *"**ONLY 2 NEW KEYWORDS: `async` + `await`**
(everything else = namespace TYPES)... **NO `scope`**... **NO `spawn`** (calling an async fn
returns the Intent). CPU parallelism = lib fn `teko::threading::run(()=>…): Intent<T>`."* A linha
seguinte (`:661`) crava a supressão: *"~~W13~~ SUPERSEDED... Old concurrency design (`scope{}`/
`spawn`/`WaitGroup`/M:N scheduler) is REPLACED: NO `scope`/`spawn`."*

**Ruling B — 2026-07-27, `concorrencia-adiantada-s8.md`:** o gate de teste precisa de threads de SO
**agora**, e desenha `teko::isolate` (L1) com `Isolate`, `spawn(entry, ctx, lane): Isolate |
error`, `join`, `fork_join`, `hardware_parallelism` — como **biblioteca**, deixando as **cinco
palavras-chave** `scope{}`/`spawn`/`channel<T>`/`send`/`recv` **reservadas e não congeladas**
(`TEKO_MASTER_PLAN.md:262`, citado por aquele documento).

**A leitura ingênua vê contradição:** A diz "NO spawn"; B constrói uma função chamada `spawn`. **A
leitura correta, que é a do dono nesta tarefa, é que não há contradição nenhuma — porque as duas
frases falam de coisas diferentes.** O "NO spawn" de A é sobre uma **palavra-chave de sintaxe**: A
não introduz um token `spawn` no lexer, nem uma forma de statement `spawn <expr>`. O `spawn` de B
é, e sempre foi, **uma função de biblioteca** — `pub unsafe fn spawn(entry, ctx, lane): Isolate |
error` é uma assinatura comum, chamável como qualquer outra, sem nenhuma palavra reservada nova. A
própria B nunca propôs um token: ela cita `TEKO_MASTER_PLAN.md:262` exatamente para dizer que as
**palavras-chave** continuam congeladas, e entrega a **capacidade** por biblioteca enquanto isso.

**E há uma segunda peça, medida agora e não antes:** `examples/regressions/bulk/src/
q200_arena_size_directive_ok/body.tks` e `src/codegen/codegen.tks:8828-8842` documentam, já
em produto, **a ruling de fronteira de isolate do próprio dono, datada 2026-07-27** — o mesmo dia
de B: *"the owner's ruling on the execution-unit model (2026-07-27) is that every isolate — the
language's future unit of concurrent execution, a **heap-isolated boundary, NOT a suspension point
like async/await's coroutine** — is born with its OWN root, 'como se fosse outro programa'"*. Esta
frase, já na árvore antes desta tarefa, é a prova de que o próprio dono **já distinguia** isolate
(fronteira de heap) de async/await (ponto de suspensão) no mesmo dia em que ratificou B. A tensão
que "o produto apontou" nunca existiu na cabeça de quem decidiu — só na leitura que confundiu
"função de biblioteca chamada `spawn`" com "palavra reservada `spawn`". **Confirmo a ruling de hoje
como a formalização correta de uma distinção que a árvore já continha.**

### 1.2 O precedente de método: minimizar tokens novos é a norma do projeto, não uma preferência minha

`TEKO_MASTER_PLAN.md:674` (o GATE do lexer) já tomou esta decisão, para uma onda de sintaxe muito
maior (OOP + DI + async completos): *"`class abstract virtual override intern flags async await`
are **CONTEXTUAL** (recognized by the PARSER in position...) → **NO lexer reservation, zero corpus
breakage**"*. Mesmo `async`/`await` — as ÚNICAS duas palavras que a Ruling A message anuncia como
"NEW KEYWORDS" — **não são tokens reservados do lexer**; são identificadores comuns que o parser
reconhece por posição (`async fn`, dentro de corpo `async`). Isto não é um detalhe: é o método
inteiro do projeto para introduzir sintaxe nova, e é o que evita repetir o que `self`/`base`
custaram (~270 usos existentes que forçaram a forma contextual, `:674`).

### 1.3 O que meço agora, e que a §3.3 de B não tinha: dado de colisão real para `isolate`/`spawn`/`chan`

B cita a exigência de `MASTER_PLAN:262`: não congelar sem *"dado de duplicação real"*. Eu meço esse
dado nesta árvore, porque ele ainda não tinha sido produzido para estas três palavras especificamente:

```sh
grep -rEon '\bspawn\b'   src/ examples/   # 7 acertos — nenhum é um identificador Teko chamado `spawn`
grep -rEon '\bchan\b'    src/ examples/   # 1 acerto — src/checker/spine.tks:1553, PROSA num doc-comment
grep -rEon '\bisolate\b' src/ examples/   # 4 acertos — todos PROSA em doc-comments (o precedente da §1.1)
```

**Resultado: zero identificadores Teko hoje se chamam `isolate`, `spawn` ou `chan`.** Os acertos de
`spawn` são prosa em comentários C (`teko_rt.c`, `win32_compat.h`) e doc-comments Teko sobre
`teko::process::spawn` (lançamento de **processo**, não de tarefa — nota de vocabulário na §4.4). O
de `chan` é um doc-comment em `spine.tks:1553` que já **antecipa** um `chan.send(x)` futuro (§5.4
abaixo). **A diferença para `self`/`base` é total: aqui a reserva não quebraria um corpus, porque
não há corpus a quebrar.** Isto muda o cálculo de custo de tornar as três palavras tokens reais — mas,
como a §2 argumenta, não muda a recomendação, porque o motivo de manter contextual não é medo de
colisão: é não fechar uma porta de sintaxe antes de ela ter um segundo uso que a justifique.

---

## 2. Os tokens — onde entram (ou não) no lexer/parser

### 2.1 O mecanismo existente, para referência exata

`src/lexer/lexer.tks:332` (`keyword_kind`) é uma cadeia de `if text == "..." { return TokenKind::X }`
— cada entrada é um token **sempre** reservado (`"loop"` → `TokenKind::Loop`, `:344`; `"defer"` →
`TokenKind::Defer`, `:348`). É aqui que uma palavra reservada de verdade entraria, e o comentário da
própria função nomeia a alternativa: *"CONTEXTUAL keywords — significant only in specific syntactic
positions"* (`:322`) — essas **não** entram em `keyword_kind`; ficam `TokenKind::Ident` no lexer e o
**parser** as reconhece por texto, na posição certa, como já faz `parse_decl.tks:58`
(`tokens[p].text == "params"`) e `:389` (`tokens[p].text == "from"`).

### 2.2 A decisão: nenhuma das três precisa de reconhecimento algum, hoje

A superfície inteira que o journal exige — repetida abaixo — é **cem por cento chamada de função e
anotação de tipo genérico já legais**:

```teko
let c: u64 = teko::threads::chan_bounded(1024)      // chamada comum
let tx = teko::threads::chan_writer(c)               // chamada comum
match tx.push(rec) { error as e => ...; null => ... } // método comum, já suportado (structs com fn)
match teko::threads::chan_pop(c) { ... }             // chamada comum
defer teko::threads::chan_close(c)                   // `defer` JÁ EXISTE (lexer.tks:348)
loop teko::threads::chan_is_open(c) { ... }          // `loop <cond> { }` JÁ EXISTE e compila
                                                      //   (medido, journaling-de-corrida §19.3)
```

Nenhuma linha acima usa uma palavra reservada nova. `chan<Rec>` é um tipo genérico como qualquer
outro (`Tx<Rec>`, `Rx<Rec>` ou um único `Chan<Rec>` com dois acessores — a forma exata é §5). `spawn`
é uma função (`teko::isolate::spawn(entry, ctx, lane): Isolate | error`, já assinada em B). `join`,
`fork_join`, `hardware_parallelism` idem. **Portanto: zero tokens novos no lexer, zero entradas novas
no parser, para toda a superfície que este documento existe para suportar.**

Isto não é uma lacuna do desenho — é a mesma decisão que `async`/`await` tomaram (§1.2), aplicada a
uma superfície que, ao contrário de `async`/`await`, **nem precisa de reconhecimento contextual**:
`async fn` muda a forma como o corpo é compilado (precisa de o parser saber que está dentro de um
`async`); `spawn(f, ctx, lane)` não muda nada na gramática — é uma chamada com um tipo de retorno
`Isolate | error`, tratada pelo checker como qualquer `extern fn`/`fn` fallível.

### 2.3 O que FICA reservado, e por quê — a mesma razão que B já deu

`scope{}` / `spawn` (como **statement/prefixo de sintaxe**, não como nome de função) / `channel<T>`
(como sintaxe dedicada) / `send` / `recv` continuam **reservados e não tokenizados**, herdando
integralmente o argumento de B (§3.3 daquele documento) e de `MASTER_PLAN:262`: não há ainda **dois**
usos reais divergindo o bastante para desenhar a sintaxe por cima deles — há **um** (`chan<Rec>` do
journal). Uma palavra-chave desenhada sobre uma amostra de um é o mesmo erro que a `bare-name-probe-
family` já catalogou para símbolos: parecer generalidade e ser, na verdade, o formato do primeiro
caso que apareceu.

### 2.4 Onde a sintaxe ENTRARIA, se e quando um segundo uso a exigir — registrado para não redescobrir

Não construo nada disto. Registro a costura, porque é barato e poupa uma redescoberta:

| forma de açúcar | onde entraria | contextual, como |
|---|---|---|
| `spawn <call-expr>` (lança e devolve `Isolate`) | `parse_decl.tks` ou `parse_expr.tks`, como prefixo de expressão | `tokens[p].text == "spawn"` seguido de uma chamada — o mesmo padrão de `"from"` |
| `isolate { ... }` (bloco com fronteira de arena própria) | `parse_stmt.tks`, como forma de bloco | `tokens[p].text == "isolate"` antes de `{` — o mesmo padrão de `virtual fn` |
| `chan<T>` com sintaxe de canal dedicada (`c <- v`, `<- c`) | novos operadores, não cabem no padrão de palavra contextual | exigiria tokens novos de verdade (`<-`), e é a peça mais cara — **não a proponho** |

A primeira linha é a que mais tensiona com a superfície `spawn orquestrar(c)` que a missão desta
tarefa usa como prosa ilustrativa — e vale nomear a tensão em vez de escondê-la (§4.3).

---

## 3. `isolate` — a semântica de isolamento

### 3.1 O que já está ratificado e em produto, ANTES desta tarefa

O achado mais importante desta pesquisa: **o conceito `isolate` já tem uma ruling de fronteira do
dono, datada 2026-07-27, e já tem uma primeira materialização em produto** — `#arena_size(N)`
(crumb `#453`, `src/codegen/codegen.tks:8828-8842`, `examples/regressions/bulk/src/
q200_arena_size_directive_ok/`). A função que declara `#arena_size` abre sua região com
`tk_region_new(NULL)` — pai `NULL`, **raiz de árvore independente**, nunca filho da raiz de
processo (`tk_region_root()`). O doc-comment do próprio codegen nomeia a regra geral: *"every
isolate ... is born with its OWN root, 'como se fosse outro programa'"*.

**O que isto entrega, e o que não entrega:** entrega a mecânica de raiz-independente (uma região
`tk_region_new(NULL)` já existe e já é exercitada por um crumb fechado). **Não entrega** thread, não
entrega escalonamento, não entrega passagem de mensagem — o próprio doc-comment o diz: *"no
scheduling, no message-passing, no thread"*. É a fundação de memória de um isolate, sem o isolate.

### 3.2 O que falta, e é exatamente o que B (`concorrencia-adiantada-s8.md`) já mediu como bloqueante

`tk_arena_push`/`tk_arena_pop` (`src/runtime/teko_rt.h:155-156`, **inalterado nesta árvore**, medido
nesta tarefa) continuam sem parâmetro — pilha global sobre a raiz de **processo**. `#arena_size`
contorna isto **estaticamente**: uma função com o atributo ganha sua PRÓPRIA raiz de uma vez, no
início; não há push/pop concorrente disputando a mesma pilha porque não há duas THREADS tocando essa
função ao mesmo tempo hoje. **Duas raias de verdade, cada uma com sua chamada a push/pop sobre a
pilha global, corrompem-se** — o mesmo achado §8.1 de B, ainda válido, ainda não pago.

**Portanto a semântica de isolamento que este documento fixa é em DUAS CAMADAS, e a distinção
importa para o dono decidir onde investir:**

1. **Fronteira de região (raiz própria) — JÁ EXISTE**, via `#arena_size`/`tk_region_new(NULL)`.
   Suporta hoje: uma função rodando SOZINHA em sua própria raiz (sem paralelismo real).
2. **Fronteira de tarefa (raiz própria + thread de SO concorrente)** — **NÃO EXISTE**. É o crumb
   **C-A/F1** de B: `tk_task`/`tk_task_current()`, as 12 famílias de globais colapsadas por tarefa,
   `tk_arena_push`/`pop` operando sobre a raiz da tarefa CHAMADORA. **Pré-requisito bloqueante para
   qualquer `spawn` que rode código Teko concorrentemente.**

### 3.3 A região do programa (F2) — onde o `chan` mora, e por que isto não é opcional

B (§F2) mediu que, depois de F1 partir a raiz única em N raízes de tarefa, **não sobra nenhuma
"raiz de processo" para um recurso singleton morar** — cada tarefa morre e sua raiz esvazia com ela.
Um `chan` — que por definição do dono (§18.1 de journaling-de-corrida, abaixo) é criado UMA vez pela
`main` e sobrevive a todas as tarefas que o usam — precisa de uma região **imortal, separada de
qualquer raiz de tarefa**. F2 é essa região, e é **pré-requisito de F1**, não um adicional: sem ela,
o primeiro `chan_bounded` depois de F1 aterrar não teria onde nascer.

### 3.4 IDs, não ponteiros — a regra do dono, e por que ela é a única correta aqui

> *"Abriu um canal? Tem que retornar a ref do canal... a main abre o canal e passa para a thread do
> orquestrador um id pra ele buscar a ref do canal somente leitura e para os handlers passa o id pra
> eles buscarem a ref de escrita."* — owner, citado em journaling-de-corrida §19.1

A razão não é estilo, é memória: depois de F1, um ponteiro para a arena de OUTRA tarefa pendura no
instante em que essa tarefa rebobina seu próprio `arena_pop`. **Um `u64` não é um ponteiro — é um
NOME**, resolvido por consulta a um registro processo-inteiro (F3) a cada uso, nunca cacheado. A
prova que fecha isto (medida no integrador, journaling-de-corrida §19.3-§19.4): `ref x = <expr>`
compila e produz **cópia**, sem diagnóstico, nas duas rotas do compilador hoje — um handle que
guardasse estado dentro de uma cópia veria um retrato congelado e nunca notaria um fecho feito por
outro caminho. **A garantia não pode vir de `ref`; tem de vir do tipo do handle não guardar nada.**

```teko
/**
 * Regra do handle de tarefa/canal — vale para TODO id que atravesse fronteira de tarefa.
 *
 * Um handle carrega o id e mais nada. Zero observáveis em cache. Todo predicado é uma
 * CHAMADA que consulta o registro pelo id — nunca um campo lido do próprio handle.
 *
 * Por quê: uma cópia do handle (que `ref` produz hoje, sem diagnóstico) é inofensiva POR
 * CONSTRUÇÃO só se copiar um nome não envelhecer o nome. Um handle com estado em cache
 * quebra essa garantia silenciosamente — é o defeito medido em journaling-de-corrida §19.4.
 *
 * @since 0.3.1
 */
pub type Isolate = struct { handle: u64 }   // já assinado em concorrencia-adiantada-s8.md §3.2
pub type ChanId  = u64                       // o `c` que a main abre e distribui por nome
```

Isto fecha, por construção e sem depender de nenhuma capacidade de `ref` que não exista hoje
(`ref mut`/write-through **não existem**, medido, e este desenho não precisa deles — journaling-de-
corrida §19.5), a pergunta que B tinha deixado aberta sobre DI/`Ref` entre tarefas: **para o `chan`
especificamente**, a pergunta não se responde — **desaparece**, porque não há `Ref` nenhum
atravessando a fronteira, só um `u64`. Para qualquer OUTRO `#singleton` que uma tarefa resolva e
segure por `Ref`, a pergunta continua aberta e é do dono (B §8.9, journaling-de-corrida §19.2) —
**não decido aqui, e nomeio explicitamente para não ser lida como decidida**.

---

## 4. `spawn` — a primitiva de lançamento, e uma tensão que nomeio

### 4.1 A assinatura fechada, herdada de B sem alteração

```teko
pub unsafe fn spawn(entry: cabi fn(ptr<byte>): ptr<byte>, ctx: ptr<byte>, lane: u64): Isolate | error
pub unsafe fn join(t: Isolate): null | error
pub unsafe fn fork_join(count: u64, lanes: u64, entry: cabi fn(ptr<byte>): ptr<byte>, ctx: ptr<byte>): u64 | error
pub fn hardware_parallelism(): u64
```

`join` é a **única barreira de memória** do modelo v1 (B §3.2): nenhuma leitura do que uma raia
escreveu é legítima antes dele, e por isso não há necessidade de ordenação de memória explícita —
decisão que este documento herda sem reabrir.

### 4.2 O bloqueio real, medido nesta pesquisa e em B: `cabi fn(T…): R` não é token deste lexer

Medido (§20 de B, reconfirmado independentemente em journaling-de-corrida §20 sobre a MESMA busca):
`cabi` tem **zero** acertos em `src/lexer/` e `src/parser/`. **A assinatura acima não compila hoje.**
Não é um detalhe — é o único vão de superfície que falta para `spawn` sair de "assinatura desenhada"
para "função chamável": um tipo de parâmetro `cabi fn(T…): R`, válido só em posição de parâmetro
de função `unsafe`/`extern`, que aceita como argumento apenas o nome nu de uma função de topo
não-capturante (B §2.4, forma já reservada em `docs/design/star-ref-and-ffi-0.3.1.md` §4.4 G3). O
backend já emite o que falta (`LFuncAddr`, `src/lir/lir.tks:118-126`, medido); o vão é só de
front-end.

**Isto não bloqueia o consumidor journal hoje** — e é preciso dizer por quê, para não soar como
contradição: o fan-in do journal (B/journaling-de-corrida §16) foi construído deliberadamente **sem**
`teko::isolate::spawn`, com um handler que é uma função C do runtime que nunca chama código Teko
(journaling-de-corrida §16.8: *"Não é `teko::isolate::spawn`... não há superfície de threads em
Teko no fim disto"*). O `chan` que o journal expõe (§5 abaixo) é a primitiva de **dados**; `spawn`
é a primitiva de **execução Teko concorrente**, e o journal não precisou da segunda para entregar a
primeira. Os dois evoluem em paralelo, sem um bloquear o outro — mas quem quiser um dia rodar
código Teko de verdade dentro de uma raia do journal (não apenas C) esbarra neste vão primeiro.

### 4.3 A tensão que nomeio: `spawn orquestrar(c)` (a prosa da missão) não é a assinatura acima

A missão que originou este documento escreve, como ilustração de uso, `spawn orquestrar(c)` /
`spawn drenar(c, filho)` — uma chamada direta, sem `ctx: ptr<byte>` nem `cabi fn`. **Isto não é a
mesma coisa que a assinatura de §4.1 permite hoje, e a diferença não é cosmética:** `spawn(entry,
ctx, lane)` pede um ponteiro de função C-ABI e um blob de contexto já montado; `spawn orquestrar(c)`
pede que o COMPILADOR monte o `ctx` a partir dos argumentos de uma chamada comum e sintetize o
trampolim `cabi` sozinho. A primeira é o que B desenhou e é **construível com o vão de §4.2
fechado**; a segunda é açúcar de chamada que ainda não tem desenho — precisaria do compilador
inferir o layout do registro de contexto a partir da assinatura de `orquestrar`/`drenar`, o que é
trabalho de checker+codegen novo, não coberto por nenhum crumb existente.

**Não escondo a tensão, e não a resolvo fabricando sintaxe.** Para o consumidor journal, que tem
call sites **fixos, conhecidos, de funções de topo não-capturantes** (`orchestrate`, `drain_into`),
um adaptador `cabi` escrito à mão por call site é suficiente e barato — duas ou três funções, não
uma feature de linguagem. **Recomendo tratar `spawn <call-expr>` como açúcar FUTURO, registrado na
§2.4, e não como parte do que este documento fecha** — exatamente a mesma disciplina que impediu
`chan<T>` de virar sintaxe sobre uma amostra de um caso (§2.3).

---

## 5. `chan<T>` — a superfície que o journal fechou, promovida a primitiva de linguagem

### 5.1 A forma, MPSC, fixada pelo dono e sem alteração aqui

> *"`chan<T>` é fan-in, vários escritores, um leitor... para ter um fan-out 'N:M' teria que ser
> outro tipo de estrutura."* — owner, 2026-07-30, citado em journaling-de-corrida §18

```teko
/** Tx — o extremo de escrita. Copiável: os N escritores são a metade que pode ser múltipla. */
pub type Tx = struct { raw: u64 }

/** Rx — o extremo de leitura. UM só; um segundo `pop` de outra tarefa é erro de runtime,
 *  nomeado, nunca corrida silenciosa (journaling-de-corrida §18.2). */
pub type Rx = struct { raw: u64 }

pub fn chan_bounded(cap: u64): u64          // abre um canal LIMITADO; devolve o id (§3.4)
pub fn chan_unbounded(): u64          // idem, sem limite — ver ressalva §5.2
pub fn chan_writer(id: u64): Tx | error   // NotAProducer/Closed se o id não serve
pub fn chan_reader(id: u64): Rx | error   // NotAReader se já há um leitor tomado
pub fn chan_close(id: u64): null         // fecho do lado da main (o backstop, §8.2)
pub fn chan_is_open(id: u64): bool         // consulta ao registro, NUNCA cacheado (§3.4)
```

### 5.2 `chan_unbounded` — uma ressalva que preciso registrar, não decidir

Todo o desenho medido em B e em journaling-de-corrida (§15.7, §16.5) é sobre canais **limitados**:
*"um canal sem limite tem a memória como único travão, e o projeto morreu de OOM duas vezes"*. A
missão desta tarefa pede `chan_unbounded()` como parte da superfície exigida, e eu **não** encontro,
em nenhum dos dois documentos-fonte, uma medição ou um caso de uso que o exija — o journal inteiro
usa `chan_bounded`. **Registro a função na superfície porque foi pedida, mas sinalizo que ela
reabre exatamente o risco que a lei "limitado, com contrapressão" existe para fechar**, e devolvo a
pergunta ao dono na §10 em vez de decidir por ele.

### 5.3 A atomicidade de `push`/`pop` — por que é о transporte do SO que a entrega de graça

> *"`tx.push(rec): error | null` ATÓMICO (nada de 'posso escrever?' + push = TOCTOU)"*

A prova de que isto **não é uma promessa**, é uma propriedade medida do transporte escolhido na §6:
`sendto`/`WriteFile` sobre um datagrama são uma ÚNICA chamada de sistema — não há janela entre "há
espaço?" e "escrever" porque não há duas chamadas. O padrão que o TOCTOU teme (`is_full()` seguido
de `push()`) nunca é escrito, porque a API não o expõe: só existe `push`, que ATÓMICA e
internamente decide aceitar ou devolver `Full`. O mesmo para `pop`: uma única `recvfrom`/`ReadFile`
não-bloqueante decide, sem consulta prévia, entre devolver um registo, `null` (nada agora — medido
como `EAGAIN`/erro equivalente), ou fechado (leitura devolveu 0, medido).

```teko
pub fn push(tx: Tx, rec: Rec): error | null   // ATÓMICO — 1 chamada de sistema, §6
pub fn pop(rx: Rx): Rec | error | null // idem — `null` é "nada AGORA", não é fecho
```

### 5.4 O que fica reservado, e o consult site que já existe e não é usado ainda

`src/checker/spine.tks:1553` já documenta, **antes desta tarefa**, um consult site para um futuro
`chan.send(x)`: *"a `UsShared`/`UsTop` cell may NOT be consumed... a future `chan.send(x)` consume
site MUST call `is_unique_at(...)` and REJECT a shared/`⊤` target"*. **Isto é rejeição em
COMPILAÇÃO de um segundo leitor/dono compartilhado — e não está ligado a nada hoje** (journaling-de-
corrida §18.2 mediu o mesmo: o reticulado existe, não é consultado em lugar nenhum). A garantia
efetiva de MPSC, hoje, é de **runtime**: a primeira tarefa a chamar `chan_reader`/`pop` é registada
como dona; uma segunda tarefa que tente panica com mensagem nomeada (`"chan<T> is MPSC"`). **Ligar
a espinha é o upgrade natural, fora do escopo desta tarefa** (seria também fora do escopo do
journal) — registro aqui para não se perder pela segunda vez.

---

## 6. O mapeamento `chan` → transporte nativo, por plataforma

### 6.1 A medição que SUPERSEDE a escolha anterior do próprio journal — dito às claras

`journaling-de-corrida-0.3.1.md` §29.2 (datado antes da medição de 3 plataformas) tinha escolhido
**`AF_UNIX SOCK_STREAM`** (POSIX) / named pipe `FILE_FLAG_OVERLAPPED` (Windows), com um
enquadramento por prefixo de comprimento por cima, porque `STREAM` era o único testado numa caixa
Linux. **`docs/medicoes/canais-nativos-3-plataformas.md`** (mesma branch, corrida de CI
`30593502637`, commit `58c76fa3`) mediu as **três** plataformas de verdade e desfez essa escolha:

> *"O macOS não tem `SOCK_SEQPACKET` em `AF_UNIX` (`errno 43`). O que sobrevive à interseção é o
> `SOCK_DGRAM`."*

**A medição que a missão desta tarefa me dá já é esta segunda — mais recente, mais larga — e este
documento a adota como a correta, superseding §29.2 explicitamente.** A tabela final medida:

| | fan-in por nome (1 fd/handle no leitor) | N threads, 1 descritor partilhado |
|---|---|---|
| Linux | `AF_UNIX SOCK_DGRAM` | `SOCK_STREAM`, 0 rasgados em 2000 |
| macOS | `AF_UNIX SOCK_DGRAM` | `SOCK_STREAM`, 0 rasgados em 2000 |
| Windows | **mailslot** | pipe nomeado, 0 rasgados em 2000 |

O journal precisa da coluna **fan-in por nome** (N escritores/tarefas, cada um abre seu próprio
descritor pelo nome do canal — é exatamente o modelo de `chan_writer(id)` da §5.1): `SOCK_DGRAM`
nos dois POSIX, **mailslot** no Windows. Nenhuma das três plataformas perde silenciosamente quando
cheio — `EAGAIN` (Linux), `ENOBUFS` (macOS), erro equivalente medido no Windows — que é exatamente
a propriedade que a §5.3 precisa e que a lei do dono exige (*"cheio recusa com erro, não perde"*).

### 6.2 Por que `DGRAM`, e não `SEQPACKET`, é a base — e por que isso NÃO é uma derrota

`SEQPACKET` preserva fronteira de mensagem igual a `DGRAM` nos dois lugares onde existe — mas só
existe em Linux (`errno 43` no macOS, medido). **Um único enquadramento, em toda a parte**, vale mais
que usar a primitiva mais fina onde ela existe e outra onde não existe — a mesma razão pela qual
journaling-de-corrida §29.4 já tinha recusado ramificar por SO no enquadramento. `DGRAM` preserva
fronteira nas DUAS plataformas POSIX (medido, canais-nativos §1), e o mailslot do Windows também
preserva fronteira por construção (`PIPE_TYPE_MESSAGE`/mailslot, medido). **As três plataformas
convergem no MESMO enquadramento sem precisar de STREAM + prefixo de comprimento nenhures** — o que
é estritamente melhor do que a escolha de §29.2, não um recuo.

### 6.3 O que o SO garante de graça, e o que o `chan<T>` de linguagem deve afirmar (não reimplementar)

| requisito do `chan<T>` | dado pelo SO | medido |
|---|---|---|
| **limitado** | o teto do datagrama/mailslot é dimensionável na criação | `nMaxMessageSize`/`SO_SNDBUF` respeitado, com o CONCEDIDO relido, nunca o pedido assumido (a armadilha: macOS concede exato, Linux duplica) |
| **contrapressão sem perda silenciosa** | escrita não-bloqueante recusa | `EAGAIN`/`ENOBUFS`/erro Windows — nunca aceita-e-descarta |
| **fecho por produtor** | contagem de referências do descritor pelo próprio kernel | leitura só devolve 0 depois do ÚLTIMO produtor fechar — a "definição subtil de `is_open`" que o journal teve de escrever à mão (§25.4 daquele doc) é o comportamento por OMISSÃO do transporte |
| **N escritores, 1 leitor, sem corrupção** | cada escritor abre seu próprio descritor pelo nome | 2000/2000 registos entregues, 0 corrompidos, nas quatro pernas medidas |
| **fronteira de mensagem** | `DGRAM`/mailslot | preservada; `STREAM` cola (medido: 3+5 bytes → 8) |

**A regra de desenho que sai disto:** a camada de linguagem NÃO reimplementa limite, contrapressão
ou contagem de fechos — ela **pede uma vez, na abertura** (`ensure_capacity`, já desenhado em
journaling-de-corrida §29.10) e depois **confia no transporte** para o resto. Reimplementar por
cima seria o mesmo erro que a §5.1 evita ao não expor `is_full()`.

### 6.4 O que fica sem medição, e digo de qual lado

Broadcast de rede do mailslot (limite 424 bytes) e N produtores em **processos** separados (as
sondas usaram threads do mesmo processo) continuam fora desta medição — nenhum dos dois é exigido
pelo journal hoje, e journaling-de-corrida (§5, canais-nativos §5) já os lista como não cobertos.
Não finjo cobertura que não existe.

---

## 7. `WaitGroup` e o fecho — o que a missão pede e que nenhuma fonte fechou por completo

### 7.1 O que já está fechado (herdado sem alteração)

`join(t: Isolate): null | error` e `fork_join(count, lanes, entry, ctx): u64 | error` (§4.1)
cobrem o caso de **contagem estática conhecida no lançamento** — o caso do gate de teste em B. O
`defer` já existe como palavra reservada (`lexer.tks:348`) e o padrão *main é dona, `defer
chan_close(c)`* não pede nada novo: é uma chamada comum dentro de um `defer` comum.

### 7.2 O que falta: esperar N tarefas cuja contagem cresce em tempo de execução

O cenário do journal (`orchestrate` + um handler `drain_into` por tubo de cada filho) tem uma
contagem **conhecida no lançamento** (2 tubos por filho × jobs), então **tecnicamente** já cabe em
`fork_join` ou em um array de `Isolate` + laço de `join`. Mas a missão pede `waitgroup` como parte
nomeada da superfície, e nem B nem journaling-de-corrida desenham essa API — só o velho `W13`
(superseded) a mencionava (`add`/`done`/`wait`). **Não está fechado por nenhuma fonte; desenho aqui,
como INTEGRATOR-PINNED, veto aberto:**

```teko
/**
 * WaitGroup — um contador de tarefas em voo, pela MESMA disciplina de handle-por-id de §3.4:
 * o handle não guarda nada, todo predicado consulta o registro (F3).
 *
 * Por que não basta `join` num array: `add`/`done` permitem que o número de tarefas em voo
 * cresça DEPOIS do lançamento inicial (um handler que por sua vez lança outro) — o caso que
 * `fork_join`, de contagem estática, não cobre. Onde a contagem é estática no lançamento,
 * `fork_join`/`join` continuam a forma mais simples e são preferíveis a isto.
 *
 * @since 0.3.1 (integrator-pinned — não fechado por nenhuma ruling do dono; ver §10)
 */
pub fn wg_open(): u64            // abre um contador, devolve o id
pub fn wg_add(wg: u64, n: u64): null | error    // soma n tarefas esperadas
pub fn wg_done(wg: u64): null | error    // uma tarefa terminou
pub fn wg_wait(wg: u64): null | error    // bloqueia até a contagem chegar a zero
```

Construído sobre o MESMO registro F3 que já sustenta `chan` — nenhuma máquina de segurança de
memória nova, só um contador com mutex+condvar guardado pelo mesmo padrão de id.

### 7.3 A ordem de fecho — normal vs. backstop, sem novidade sobre journaling-de-corrida

A terminação normal é o `closed` de `pop` (contagem de produtores chegou a zero, garantida pelo
próprio kernel, §6.3); o `defer chan_close(c)` da `main` é o **backstop** para quando o orquestrador
precisa ser mandado parar antes disso. Os dois coexistem, não são alternativos — herdado sem
alteração de journaling-de-corrida §19.3.

---

## 8. A avaliação pedida: `async`/`await`/`Intent<T>` como açúcar para threads NÃO-ISOLADAS

### 8.1 A pergunta, tomada ao pé da letra, tem duas leituras — e elas não custam o mesmo

"Threads não-isoladas" pode significar duas coisas bem diferentes, e a resposta certa depende de
qual:

**Leitura 1 — concorrência SEM paralelismo real (cooperativa, um só mutador da arena por vez).**
`await` suspende a tarefa lógica corrente sem bloquear a thread de SO; quem retoma é o MESMO
executor, mais tarde, quando o I/O completar. Não há duas threads tocando a mesma arena ao mesmo
tempo — há uma só, alternando entre continuações. **Isto não precisa de F1 (arena por tarefa)
NENHUMA**, porque a garantia que F1 compra (nenhuma raia rebobina a arena de outra) já vale de
graça quando só existe uma raia. É exatamente a forma original de `C10.S10` no `TEKO_MASTER_PLAN.md`
(pré-S8/ASYNC): *"coroutine-based non-blocking I/O... Teko runtime event loop (epoll/kqueue/IOCP)"*.

**Leitura 2 — o "não-isolada" é literal: `Intent<T>` roda em cima de uma OS thread de verdade que
COMPARTILHA a arena de quem a lançou, sem a fronteira de F1/F2.** Esta leitura **reabre exatamente
o bug que o F1 inteiro existe para fechar** (B §8.1, §17): duas raias fazendo `arena_push`/`pop`
sobre a mesma pilha global corrompem-se, **e o sintoma é nenhum** — B já mediu isto e chamou-o "o
pior, e é bloqueante". Açucarar por cima de threads que compartilham arena sem F1 não economiza
trabalho nenhum: ou o `Intent<T>` de CPU precisa da MESMA fundação de arena-por-tarefa que `isolate`
precisa, ou ele é inseguro. **Não há atalho aqui — dourar a pílula com sintaxe não muda a física da
memória.**

### 8.2 O que a árvore já tinha decidido, e que responde a pergunta por baixo dela

`S8/ASYNC` (§1.1) já tinha, no MESMO dia da ruling que unificou tudo, duas frases que **antecipam
exatamente esta distinção sem a nomear**: *"NO `scope`` (blocks already are arenas → arena drop
joins/cancels pending Intents)"* — ou seja, um `Intent` de I/O vive DENTRO do bloco/arena de quem o
criou, não tem arena própria — **isto é a Leitura 1**. E *"CPU parallelism = lib fn
`teko::threading::run(()=>…): Intent<T>`"* — uma função de biblioteca que devolve `Intent<T>` para
trabalho de CPU é, estruturalmente, **a mesma forma de `spawn`+`join`** que B desenhou meses depois,
só que com um nome e um tipo de retorno diferentes. **A árvore já continha a resposta: `Intent<T>`
nunca foi UM modelo — sempre foram dois, disfarçados do mesmo nome de tipo.**

### 8.3 A avaliação, e a dúvida que registro em voz alta (o "incômodo" que o protocolo pede)

**Duvido de unificar os dois sob o MESMO tipo `Intent<T>` sem qualificação.** Se `Intent<T>` de I/O
(Leitura 1, cooperativo, zero custo de arena) e `Intent<T>` de CPU (que precisa da fundação inteira
de F1) forem o mesmo tipo com a mesma sintaxe `await`, um programador vendo `await trabalho_pesado()`
não tem como saber, olhando o call site, se está esperando uma continuação no MESMO thread (barato,
sempre seguro) ou uma raia de SO de verdade (cara, e insegura sem F1). Isto é o mesmo tipo de
armadilha que o projeto já baniu uma vez — o `char*`/`{ptr,len}` "trocadilho de ABI" que
`gate-sem-c-0.3.0.31.md` §2.2(e) rejeitou por ser "desonesto sob M.3": duas coisas com custo e
garantia diferentes, uma fachada igual.

### 8.4 A recomendação

**Sim, faz sentido ter `async`/`await` — mas como açúcar sobre DUAS fundações diferentes, nomeadas
como diferentes, não como um único "não-isolado" indiferenciado:**

1. **`async`/`await` para I/O (Leitura 1) — SIM, é o caso bom para açúcar, e é BARATO em memória:**
   zero arenas novas, zero threads de SO novas, um reator (`epoll`/`kqueue`/`IOCP`) por thread
   executora. Compõe com `isolate` por construção: cada isolate que quiser fazer I/O assíncrono roda
   o SEU PRÓPRIO laço cooperativo dentro da SUA PRÓPRIA arena — os dois modelos empilham em vez de
   competir. **Custo real:** ainda é uma feature GRANDE e NÃO iniciada (nenhum reator, nenhuma
   transformação de corpo `async fn` em máquina de estados, no lexer ou no codegen hoje) — não é
   sugar barato de escrever, só é seguro por barato de manter.

2. **`async`/`await` para CPU (a forma `teko::threading::run(...): Intent<T>` do `S8/ASYNC`) —
   SIM, mas SÓ como açúcar LITERAL sobre `isolate`/`spawn`/`join` (§4), nunca sobre um terceiro
   modelo de arena "leve". `async fn trabalho_pesado()` desaçucararia para: lançar o corpo num
   worker de um pool de isolates pré-aquecido (evitando o custo de `pthread_create` por chamada,
   que B já tinha citado como o próximo risco a medir), e `await` desaçucararia para o `join`
   correspondente. **Isto herda de graça toda a segurança de memória que F1 já paga** — não cria
   uma segunda dívida.

3. **O que eu recomendo NÃO construir:** um terceiro modelo — threads de verdade que rodam código
   Teko compartilhando arena SEM a fronteira de F1, disfarçado de "leve" porque tem sintaxe de
   `async`. Ele custa o MESMO que `isolate` (precisa de F1 de qualquer forma, para ser seguro) e
   entrega MENOS garantia (sem o handle-por-id de §3.4, sem o `chan` como único canal de dados) —
   é estritamente pior nas duas medidas.

**Em uma linha: `isolate`/`spawn`/`chan` é a fundação de PARALELISMO real (memória isolada, dados só
cruzam por cópia via `chan` ou valor de retorno de `join`); `async`/`await`/`Intent<T>` é açúcar —
de duas fundações diferentes, e o desenho deve continuar a dizer qual é qual em vez de escondê-las
atrás do mesmo nome de tipo.** Isto não é uma terceira posição nova: é a costura entre a Ruling A
(`S8/ASYNC`, que já continha as duas metades sem as separar) e a Ruling B (`isolate`), que a ruling
de hoje do dono já apontou como não-conflitantes — só precisavam de ter a distinção dita, não
inventada.

---

## 9. Decisões para o dono

Cada linha: a pergunta, minha recomendação (com o porquê), e o que muda se a resposta for a outra.

| # | pergunta | recomendação | se a resposta for a outra |
|---|---|---|---|
| D1 | `isolate`/`spawn`/`chan` viram tokens reservados no lexer, ou continuam funções de biblioteca sem reconhecimento sintático? | **Continuam biblioteca, zero tokens** (§2.2) — a superfície inteira do journal já é chamada de função; a mesma escolha que `async`/`await` já fizeram (contextual, `:674`) mesmo tendo virado "keywords" na prosa da ruling | se SIM: entrada em `keyword_kind` (`lexer.tks:332`) para os três; risco de colisão é **zero, medido** (§1.3) — não seria um erro técnico, só uma sintaxe fechada sobre amostra de um caso |
| D2 | `spawn <call-expr>` como açúcar de chamada (a forma da prosa da missão) — construir agora ou registrar para depois? | **Registrar para depois** (§4.3) — o journal não precisa dele (call sites fixos, adaptador `cabi` manual é suficiente); construí-lo agora seria sintaxe sobre um caso não medido em duplicação | se AGORA: abre trabalho novo de checker+codegen (síntese de `ctx` + trampolim `cabi`), fora dos 11 crumbs que journaling-de-corrida já fechou |
| D3 | `chan_unbounded()` entra na superfície mesmo sem caso de uso medido, ou sai até haver um? | **Sai, ou fica sinalizada como não recomendada** (§5.2) — nenhuma fonte mede um caso que a exija, e ela reabre o risco de OOM que "limitado por lei" existe para fechar | se FICA: precisa de sua própria invariante de segurança (um teto implícito de algum tipo), ainda não desenhada |
| D4 | `WaitGroup` (`wg_open`/`add`/`done`/`wait`) — a forma da §7.2 serve, ou o dono prefere que os casos de contagem dinâmica usem só `Isolate[]` + laço de `join`? | **A forma da §7.2**, mas é a peça menos apoiada em medição deste documento — nenhuma fonte a fechou | se `Isolate[]` + laço basta: `WaitGroup` sai da superfície, menos uma API para manter |
| D5 | `async`/`await`/`Intent<T>` — aceitar a separação de DUAS fundações (I/O cooperativo vs. açúcar-sobre-isolate para CPU) da §8.4, ou manter os dois sob uma única `Intent<T>` indiferenciada como o `S8/ASYNC` original escreveu? | **Separar, e dizer no tipo ou no doc-comment qual fundação um dado `async fn` usa** — a alternativa é a "fachada igual, custo diferente" que o projeto já rejeitou uma vez para ABI (§8.3) | se manter indiferenciado: aceitar que `await` pode, sem aviso no call site, custar tanto quanto um `spawn` de tarefa isolada — decisão legítima, mas deveria ser deliberada, não um acidente de nomenclatura |
| D6 | Qual namespace: `teko::isolate` (B) para `Isolate`/`spawn`/`join`, e `teko::threads` (missão) para `chan_*` — dois namespaces, ou um só? | **Um só, `teko::threads`**, com `Isolate` e `chan_*` lado a lado — são a MESMA camada de concorrência (thread de SO), e `teko::isolate` como nome sugere um módulo maior do que uma struct e quatro funções | se dois namespaces: nenhum custo técnico, só documentar a fronteira exata entre os dois |

---

## 10. Como este documento se verifica

```sh
# zero identificadores Teko chamados isolate/spawn/chan hoje — a base do D1
grep -rEon '\bspawn\b'   src/ examples/
grep -rEon '\bchan\b'    src/ examples/
grep -rEon '\bisolate\b' src/ examples/

# a fronteira de #arena_size já em produto, e a citação exata da ruling 2026-07-27
sed -n '1,15p'      examples/regressions/bulk/src/q200_arena_size_directive_ok/body.tks
sed -n '8828,8842p' src/codegen/codegen.tks

# tk_arena_push/pop ainda sem parâmetro — F1 não aterrou
sed -n '150,166p' src/runtime/teko_rt.h

# o mecanismo de keyword contextual já usado por `params`/`from`
sed -n '55,60p'   src/parser/parse_decl.tks
sed -n '386,392p' src/parser/parse_decl.tks

# o consult site de unicidade já escrito e não ligado, para chan.send
sed -n '1548,1558p' src/checker/spine.tks

# a ruling S8/ASYNC e a supersessão de W13 — a base da §1.1
grep -n 'S8/ASYNC\|~~W13~~\|GATE' TEKO_MASTER_PLAN.md
```

`docs/design/concorrencia-adiantada-s8.md` está nesta árvore (verificável direto). `docs/design/
journaling-de-corrida-0.3.1.md` e `docs/medicoes/canais-nativos-3-plataformas.md` estão na branch
`cargo/0.3.1.0-journal-testparallel-close` (commit de topo `58c76fa3` para o segundo) — não nesta
árvore ainda; `git show cargo/0.3.1.0-journal-testparallel-close:<path>` reproduz as citações.

Se alguma leitura acima divergir do que está no repositório, **este documento está errado e deve
ser corrigido antes de ser seguido** — não contornado.
