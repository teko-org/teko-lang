---
section: design
created: 2026-08-10
status: DESENHO — especificação única e consolidada do modelo de arena (0.3.1). Fonte da verdade.
        Consolida e SUPERSEDE, no que houver conflito, os docs de arena anteriores
        (ast-computed-arena-assessment, arena-por-escopo, modelo-de-memoria-por-escopo,
        transicao-move-on-return, backend-memoria-por-funcao, o-profiler, port-memoria,
        arena-em-teko). Onde um anterior detalha um mecanismo aqui só nomeado, ele permanece
        vigente COMO REFERÊNCIA; onde diverge desta, esta vence.
source: ruling do dono 2026-08-10 ("um documento único que especifica 100% de como a arena deve se
        comportar, incluindo multi-threading") + ast-computed-arena-assessment-0.3.1.md (as três
        ideias ratificadas) + paralelizacao-0.3.1-eixo2 + concorrencia-isolate-spawn-chan-0.3.1.md
---

# Arena — especificação única e completa (0.3.1)

> **Este documento é a fonte única da verdade do modelo de memória do teko.** Ele diz, em um só
> lugar, como uma arena nasce, cresce, morre, o que ela garante, o que ela NÃO garante (e quem
> garante no lugar dela), como um valor que escapa é colocado, e como tudo isso se comporta sob
> múltiplas threads. Os exemplos usam a superfície 0.3.1 (`var`, retorno com `:`) definida no Doc 2.

---

## 0. O princípio governante e o escopo native-only

### 0.1 Segurança de memória — onde ela mora (regra do dono)

> *"A segurança não está no tipo da variável, está na capacidade das arenas."* — dono

A segurança de memória do teko é a soma de **três eixos ortogonais**, e **nenhum deles é uma keyword
de variável** (`let`/`mut`/`var` não são invariantes de segurança — são intenção):

| hazard | quem garante | nível |
|---|---|---|
| **Use-after-free (tempo de vida)** | a arena: vida da arena ⊇ escopo do uso | arena (tempo de vida) |
| **Overflow (capacidade)** | a arena: piso pré-calculado + lista de chunks que nunca estoura | arena (capacidade) |
| **Aliasing (duas escritas vivas na mesma célula)** | exclusividade de fluxo (F1, `borrow.tks`) — análise de fluxo, independente de keyword | controle de fluxo |

A frase precisa: **segurança = tempo-de-vida + capacidade da arena (UAF, overflow) + exclusividade F1
por fluxo (aliasing)**. Remover `let`/`mut` não remove nenhuma dessas garantias, porque nenhuma delas
vinha da keyword (verificação completa: `ast-computed-arena-assessment-0.3.1.md` §4.8).

### 0.2 O destino: native-only, sem emitir nem depender de C

O teko **não emite C nem depende de um compilador C**. A rota C (emitir `teko.c`, invocar `cc`) é
**andaime de bootstrap**, não destino — sai quando o backend nativo se auto-hospeda. O único resíduo
de C admitido é:

- **FFI nativo com link dinâmico** para bibliotecas de sistema em **Apple e Windows** (libSystem,
  `kernel32`, etc.) — chamada nativa por símbolo dinâmico, **nunca** a emissão de um arquivo `.c`
  local que exija um compilador C.

Consequência para este documento: **o modelo de arena aqui é o modelo do backend NATIVO.** A
"preservação de bytes para a rota C" que dominou o período de bootstrap era uma restrição de
transição; a invariante permanente é a **auto-consistência do backend nativo** — o fixpoint
`gen2 == gen3` (o compilador nativo se reconstrói ao mesmo byte).

---

## 1. A região — a mecânica física

### 1.1 Uma região é uma LISTA de chunks, não um buffer que realoca

`tk_region_alloc` (`teko_rt.c:2034`) faz **bump** dentro do chunk-cabeça; quando o pedido não cabe, ele
**PREPENDA um chunk novo** de `max(pedido, TK_REGION_DEFAULT_CHUNK)` (`teko_rt.c:2061`;
`TK_REGION_DEFAULT_CHUNK = 64 KiB`, `teko_rt.h:155`). **Crescer uma região NUNCA copia os bytes dela** —
apenas encadeia outro chunk. Logo:

- Não há "realocação" no nível da região. A capacidade cresce por encadeamento, O(1) amortizado,
  sem cópia.
- Um chunk nunca estoura: se o pedido não cabe, encadeia-se outro. **Overflow no nível da região é
  estruturalmente impossível.**

### 1.2 O copy-grow O(n²) vive na SLICE, não na região

A cópia que custa vive uma camada abaixo, na slice: `tk_slice_push_r` (`teko_rt.c:1272`) aloca um buffer
NOVO maior, `memcpy` do velho→novo, e **abandona o velho dentro da região**. Com reclaim 0%, cada
metade-abandonada da duplicação fica residente até a região dropar — é a origem do pico ~1.8 GB
(`al1-proof-report.md`: 1138 MB de copy-grow medidos).

A cura in-place já existe como primitiva: `tk_slice_grow_inplace` (`teko_rt.h:1287`, o append "Modelo A"
/ AL3) **acrescenta sem abandonar quando há capacidade**. É o `push(ref x, v)` ref-push. Presente, mas
ainda não é o caminho de emissão pervasivo.

**Distinção decisiva:** o piso da AST (§3) mira a demanda de chunk da região; o 1.8 GB mira a slice, e é
domínio do AL3 `grow_inplace`, não do piso.

### 1.3 O retrato de retenção (medido)

| quantidade | valor | onde |
|---|---|---|
| root (nunca liberado) | **1926 MB** | raiz da task, dropada só na saída do processo |
| razão de reclaim | **0.0 %** | nada dropado no meio do build |
| scoped (liberado no drop) | **0.0 MB** | escopos abrem região, não liberam nada útil |

**A parede é retenção, não iteração.** Os três mecanismos do modelo (piso, elisão, DPS) atacam
retenção e throughput; o DPS ataca também **correção** (fronteira de retorno).

---

## 2. Ciclo de vida — a árvore de regiões

### 2.1 Abrir, entrar, sair, dropar

A disciplina de região é uma árvore. Cada nó:

```
child = region_new(parent)   // filha com chunks SEPARADOS do pai
region_enter(child)          // todo bump-alloc subsequente cai nesta região
... trabalho ...             // aloca livremente
region_leave()               // volta o topo da pilha ao pai
region_drop(child)           // larga TODOS os chunks da filha de uma vez — O(1)
```

- **`region_drop` é atômico e total:** larga todo o conteúdo da filha numa tacada. Não há free
  por-objeto; a granularidade de liberação é a região.
- **Escopo léxico = região.** Um bloco abre uma região ao entrar e a dropa ao sair. A vida de tudo que
  ele alocou é exatamente a vida do escopo.
- **A raiz da task** (`tk_region_root`) é dropada só na saída do processo — é onde a retenção-root mora.

### 2.2 A pré-condição de segurança do drop total

`region_drop` só é seguro porque **nada vivo fora da região aponta para dentro dela**. Toda a disciplina
de escape (§6) existe para manter essa invariante: um valor que precisa sobreviver ao drop **não pode
nascer na região que vai dropar** — tem de nascer numa região que o cobre. É exatamente aqui que o
`arena-por-escopo` (a passada falsificada) quebrou: fez bulk-drop de uma região enquanto um alias vivo
ainda a referenciava → UAF. **A lição, gravada:** nunca dropar região com referência viva pendurada; a
colocação correta (§3, §5, §6) é o que garante isso por construção.

---

## 3. O piso da arena — pré-cálculo estático a partir da AST (Ideia 1)

> *"cada arena tem seu piso pré-calculado em tempo de montagem da AST… reduzindo realocação."* — dono

### 3.1 O que é

A AST prova um **limite inferior** da demanda do primeiro chunk de uma região: a soma das alocações de
largura fixa que os statements de um escopo provadamente fazem (literais de struct de layout conhecido,
literais de array de contagem conhecida, sítios de box). Esse piso semeia o presize:

- **A regra do dono (2026-08-10):** o piso de um escopo é o **cálculo estático de quanto ele precisa +
  o cabeçalho da arena** — **NÃO os 64 KiB default de sempre.** `open_frame_region`/`open_native_region`
  passam esse piso a `tk_region_new_sized_u(parent, need + header)`. A região nasce do tamanho da
  necessidade provada, não de um bloco fixo. (Elisão, §4, é o caso limite `need == 0`: sem necessidade,
  sem região.)
- **Para necessidade dinâmica** (a folha aloca, mas o tamanho é fato de runtime): o piso estático semeia
  o caso `Confidence::Thin` do presize do profiler (`#arena_size`, `codegen.tks:9832`), e a região cresce
  além por chunk-list (§1.1); o caminho dinâmico (profiler p99.9) refina quando há amostra.

### 3.2 O que ele garante e o que não

- **Piso é limite INFERIOR.** A região cresce além dele normalmente (§1.1). Nunca há UAF por sub-piso —
  não há como faltar espaço, só encadeia chunk.
- **Sobre-piso é leak-safe** (reservado-não-usado), a mesma assimetria que o custo newsvendor do
  profiler modela (`c_sobra = 1 byte/byte`).
- **O piso NÃO entrega o 1.8 GB.** Esse número vive na slice-cap (§1.2) e é do AL3. O piso entrega
  dezenas de MB (tail-waste, ~26 MB / 21k chunks) + throughput no `posix_memalign`.

### 3.3 Prioridade

Mais baixa das três ideias. **Dobrar no presize do profiler**, não construir como análise separada.

---

## 4. Elisão de arena — não abrir região onde não se aloca (Ideia 2)

> *"pode chegar à conclusão que não há necessidade de uma arena em determinada região, no caso da
> expressão não tocar a arena."* — dono

### 4.1 O que é

Um predicado `scope_touches_arena(body): bool` — verdadeiro sse e só se o escopo contém ao menos um
sítio de alocação routável (`push`/`box`/init-de-struct/lit-de-array/concat-de-str/`tk_alloc`). Quando
falso, `open_native_region`/`open_frame_region` **pulam** `tk_region_new_u`/`enter_u` (e o `drop_u`
pareado) — exatamente como o skip de `bracket_depth > 0` já faz hoje (`lower.tks:1637`), um padrão
provado na mesma função. A pilha de regiões continua balanceada pela mesma simetria.

### 4.2 O que ganha

Com o piso já dimensionado por necessidade (§3), a elisão é o caso **`need == 0`**: um escopo-folha que
não aloca nada (`if x { return a }`, um braço de comparação, um bloco-guarda) não abre região nenhuma —
economiza o **cabeçalho da arena + a maquinaria** `new`/`enter`/`leave`/`drop` no caminho quente. (Antes
do piso por necessidade, cada região pagava 64 KiB mínimo — o profiler mediu o caso brutal: um bloco de
200 bytes custava 64 KiB, perda de 300×; com piso = need+header isso desaparece, e a elisão remove até o
header.) O custo é por região **simultaneamente viva**, limitado pela profundidade, não pela contagem
total.

### 4.3 A regra de ouro: conservador

**Dúvida → NÃO elide.** O predicado é conservador (mesma postura de `escape.tks`: "dúvida → escapa").
Um sítio routável não detectado roteia sua alocação para a região **externa** (leak-safe), **nunca**
UAF — desde que a elisão só remova o wrapper da região, nunca redirecione uma alocação existente.
Compõe limpo com o DPS: um escopo sem alocação não conveia nada, então os brackets de conveyance nunca
disparam dentro dele.

---

## 5. DPS — retorno virtual na arena de quem chama (Ideia 3) — A CHAVE

> *"tratar return sempre como alocação do valor diretamente na arena de quem chamou a função… a função
> chamada teria a referência da arena do chamador e nosso return seria virtual — seta na arena do
> caller e sai."* — dono

### 5.1 O modelo

O callee recebe uma **referência ao destino na arena do caller** e o `return` escreve o valor **ali** e
sai — não há slot de frame local no callee para copiar. Isso é **destination-passing style (DPS)** para
retornos de agregado.

- O destino é alocado pelo caller na sua **região CORRENTE** (`alloc_call_dest`, via
  `region_current_vreg`) — **nunca** uma filha nova que poderia ser dropada por baixo do valor
  retornado (isso reabriria o UAF do arena-por-escopo). É exatamente a região onde o caller hoje
  alocaria o resultado depois de uma cópia.
- `return` vira **virtual**: constrói no destino + emite o `ret` nu. Sem cópia-out.
- **Tail merges que alimentam o retorno** (um `if`/`match` em posição de cauda) escrevem cada braço
  **no MESMO `ret_dest`** — fechando a fronteira tail-merge-into-return numa tacada.

### 5.2 O que ele retira e o que ele preserva

- **Retira `own_returned_value`** (`lower.tks:11715`) — a box post-hoc que hoje copia o agregado para
  storage que sobrevive ao frame, DEPOIS do fato. O DPS constrói-no-lugar-desde-o-início. O
  `own_returned_value` era um remendo retrofitado; o DPS é o modelo.
- **`frame_escape_guard`** (`frame_escape.tks:56`) fica **satisfeito por construção** para funções DPS
  (não há slot de frame para escapar) e **permanece** como rede de inversão para os casos residuais
  (retorno de registrador/closure).
- **`ret_dest = null` = caminho de hoje, byte-idêntico.** O DPS entra SÓ quando um destino é passado.
  Função de valor-registrador (escalar/enum/ponteiro) retorna em registrador, sem destino — DPS nunca se
  aplica.

### 5.3 As assinaturas (copiar verbatim de `ast-computed-arena-assessment-0.3.1.md` §4.2)

```teko
fn fn_returns_aggregate(f: checker::TFunction, ctx: LowerCtx): bool
fn with_ret_dest(ctx: LowerCtx, dest: u32 | null): LowerCtx
fn lower_return_into_dest(ctx: LowerCtx, r: checker::TReturn): LowerStmtOut | error
fn alloc_call_dest(ctx: LowerCtx, callee: checker::TFunction): Lowered
```

Funções existentes tocadas: `lower_return`/`lower_return_fat` (`lower.tks:7245`/`:7278`), `lower_call`
(`:1740`), `lower_block_value`/`lower_match` tail (`:10798`), retirar `own_returned_value` (`:11715`) no
caminho DPS.

### 5.4 DPS subsume `-> ref T`

O retorno por referência (`-> ref T`) é vestigial — **zero funções de produção o usam** (só uma probe e
duas regressões de rejeição). Seu único uso real era o *identity pass-down* (retornar um `ref` PARÂMETRO
inalterado). Sob DPS, todo retorno de agregado já aterrissa na storage do caller, então um retorno
tipado-referência é redundante: o caller já tem o valor onde queria. **DPS subsume todos os casos de
`-> ref T`** → o form é removido (Doc 2). Depois disso, `ref` sobrevive SÓ em **parâmetros**
(caller→callee, borrow write-through, ex. `grow_inplace(ref []T)`), nunca como escape callee→caller. Dois
mecanismos ortogonais: escape por DPS, borrow por F1.

---

## 6. Escape e as quatro fronteiras — onde um valor "sai" da sua região

O deep-root da corrupção nativa é: *agregados são o endereço de um slot de frame por-instrução, e não são
copiados-out de forma confiável nas fronteiras de conveyance.* Há **quatro fronteiras**, e cada uma tem
um dono:

| fronteira | exemplo | dono da cura |
|---|---|---|
| **retorno passando o slot de frame** | `return agregado` | **DPS** (§5) — nasce na arena do caller |
| **tail-merge alimentando retorno** | `return if…`, `match` em cauda | **DPS** (§5) — cada braço no mesmo `ret_dest` |
| **self-append / push em loop** | `push(ref buf, v)` em laço | **AL3** `grow_inplace` (§1.2) — não DPS |
| **acumulador by-address / merge não-cauda** | `ref acc: []str` escrito em loop | **região que sobrevive** (piso/AL3) — colocação, não drop |

**A regra única:** um valor que escapa **nasce na região cuja vida o cobre**. O piso da AST computa a
demanda; o DPS coloca o retorno na arena do caller; o AL3 mantém o append in-place sem abandonar. A
elisão nunca redireciona uma alocação existente (só remove wrapper vazio). **Nenhuma cura envolve dropar
mais cedo** — a falha do arena-por-escopo foi exatamente dropar cedo com alias vivo.

---

## 7. Concorrência — a arena sob isolamento e paralelismo

O teko tem **dois modelos de concorrência**, e a distinção entre eles é de ARENA (recomposto dos docs
`concorrencia-isolate-spawn-chan` 08-03, `paralelizacao-eixo1/eixo2` 08-02, `journaling-de-corrida`
07-30, `concorrencia-adiantada-s8`):

| modelo | superfície | arena |
|---|---|---|
| **`spawn` + `chan`** — paralelismo real | keyword + tipo (Doc 2 §10) | **heap isolado por task** — cada task tem sub-raiz própria (F1); dados só cruzam por **cópia** (via `chan`); `spawn` é fire-and-forget (sem retorno) |
| **`await` + `Intent<T>`** — suspensão | keyword contextual (Doc 2 §10) | **duas fundações** (§7.9): I/O cooperativo vive na arena de quem criou; CPU roda numa corotina de um pool |

O paralelismo de compilação (abaixo) é do **BACKEND pós-lowering**, e o modelo de arena sob threads é
uma extensão direta e provada da disciplina de região por-função.

### 7.1 Escopo: paralelizar o backend, NUNCA o lowering

Paraleliza-se `select`/`regalloc`/`encode` **por função**. O `lower_program` fica **SERIAL** — e isso é
o que desarma a ameaça mais perigosa: os **nomes ordinais** (`.Lstr<n>`, `.Lclofn<n>`) são atribuídos
DURANTE o lowering, na ordem de visita. Paralelizar o lowering renomearia símbolos e mataria o fixpoint.
Quando o fluxo chega ao encode, o `LModule` já está todo baixado com cada nome fixado em ordem serial —
o backend paralelo só CONSOME nomes já fixos.

### 7.2 Região-por-thread (por-lane)

Cada worker corre numa `tk_task` própria (`tk_task_current()` é `_Thread_local`) com root próprio.
Dentro do worker, a disciplina de região reusa-se tal-e-qual:

```
// a lane_region é criada pelo PAI (filha de tk_region_program(), task-agnóstica,
// sobrevive ao tk_task_end do worker) e passada ao worker; o worker só a ENTRA.
scratch = region_new(lane_region)             // filha da região-de-raia
region_enter(scratch)
ef = encode_lfunc_in_region_x86(funcs[i])     // isel/regalloc/encode bump-alocam no scratch
region_leave()
encoded[i] = clone_encoded_into(lane_region, ef)   // COPIA p/ a raia antes de dropar (sobrevive à barreira)
region_drop(scratch)                          // larga o scratch da função
```

- **Dois workers nunca bump-alocam a mesma região** → sem corrida de alocação, sem lock no caminho
  quente. `tk_g_region_gen` é atômico e só afeta a validade do `push_cache`, **nunca os bytes**.
- **Cópia-antes-do-drop:** `clone_encoded_into` copia o resultado para a `lane_region` (parent-visível,
  sobrevive à barreira) antes de largar o scratch. Os `str` de NOME (`Symbol.name`/`RelocX86.sym`)
  **não são copiados** — originam em `prog` (Grupo B, imutável, partilhado, sobrevive a tudo).

### 7.3 MAP paralelo → REDUCE serial ordenado

```
// FASE MAP (paralela): função i → worker (i % lanes); escrita no slot DISJUNTO encoded[i]
parallel for i in 0..funcs.len:
    encoded[i] = encode_lfunc_in_region_x86(funcs[i])
// BARREIRA (join) — o ÚNICO ponto de sincronização
// FASE REDUCE (serial, no pai, em ordem de ÍNDICE, nunca de conclusão):
var mt = empty_module_text_x86()
for i in 0..funcs.len:
    mt = fold_encoded_func_x86(mt, encoded[i])   // absolutiza offsets .text AQUI, serial e ordenado
finish_encoded_module_x86(mt, rodata, globals)
```

### 7.4 A prova de fixpoint sob paralelismo (byte-idêntico)

O objeto emitido é byte-idêntico para qualquer `lanes ≥ 1`. Duas invariantes:

- **(I1) Cada `e[i]` tem o mesmo valor em série e em paralelo.** `encode_lfunc_in_region_x86(funcs[i])` é
  função **pura** de `funcs[i]`: inputs = a `LFunc` + ABI constante; toca só memória per-task; outputs
  (bytes, syms com offsets relativos ao próprio `.text`, relocs) não dependem de outro `funcs[j]` nem de
  estado global mutável (sem interning no backend, `region_gen` irrelevante para bytes, rodata/globals
  lidos só no `finish`).
- **(I2) O fold é aplicado na mesma ordem `0,1,…,n-1`.** O REDUCE itera por ÍNDICE, lê `encoded[i]`,
  absolutiza offsets só aqui. A ordem de CONCLUSÃO dos workers afeta só QUANDO `encoded[i]` é preenchido,
  nunca a ORDEM em que é consumido.

Logo o output é byte-idêntico. **`gen2 == gen3` é preservado E é o próprio detector:** se um byte mudar,
uma das invariantes foi violada (escape para região errada, ou fold em ordem de conclusão) — PARAR e
reexaminar. `lanes = 1` reproduz o serial de hoje byte-a-byte (modo de bisecção).

### 7.5 `nproc` — default concedido pelo SO, cap no máximo do SO

`lanes` default = o paralelismo que o SO CONCEDE ao processo (`hardware_parallelism()`:
`sched_getaffinity` popcount no Linux, máscara de afinidade no Windows, `hw.logicalcpu` no macOS),
sempre `≥ 1`. Override explícito (`TEKO_BUILD_LANES`) clampado a `[1, os_max]`. Nunca sobre-subscreve.
Todo o fork-join fica dentro de UMA fase `phase_begin`/`phase_end` (codegen) → `dark` continua 0; o
overlap encolhe o `wall` da fase.

### 7.6 `spawn`, task e a região imortal do programa (F2)

O isolamento de memória tem **duas camadas**:

1. **Fronteira de região (raiz própria) — JÁ EXISTE:** `#arena_size`/`tk_region_new(NULL)` — pai `NULL`,
   raiz de árvore independente, *"como se fosse outro programa"*. Suporta uma função sozinha na própria
   raiz, sem paralelismo real.
2. **Fronteira de task (raiz própria + thread de SO concorrente) — o pré-requisito bloqueante:**
   `tk_task`/`tk_task_current()`, as globais colapsadas por-task, `tk_arena_push`/`pop` sobre a raiz da
   task chamadora. Necessária para qualquer `spawn` que rode código Teko concorrente. **É esta camada que
   a keyword `spawn` cria** (§7.8): cada `spawn f(args)` nasce uma corotina isolada com sua sub-raiz, os
   argumentos entram **por cópia** nessa raiz, e nada de fora é referenciado por `ref` (§9).

**`spawn` — lança uma função sem retorno como thread.** A regra dura do `spawn` é **uma só: o alvo NÃO
pode ter retorno** (um valor retornado não teria para quem ir — `spawn` é fire-and-forget). Uma função
sem-retorno lançada por `spawn` roda como uma **thread**: sua **própria sub-raiz de arena** (F1), dentro do
processo (compartilha a região de programa F2: registro de `chan`, singletons por thread). Os argumentos
entram **por cópia**, nada de fora é referenciado por `ref` (§9), e a conclusão vem por outros meios (o
fecho do canal, o `WaitGroup`) — nunca pelo próprio `spawn`.

*(Não há um construto de isolamento tipo-processo na linguagem, e é de propósito: uma thread no mesmo
processo ainda pode corromper e já só fala por canais do SO — teria os custos de um processo sem o ganho.
Quem precisa de isolação de processo real usa **outro binário**, fora do escopo da linguagem.)*

```teko
fn tarefa() {                          // função comum SEM retorno — SEM id (resolve por chave)
    var tx = svc<Tx<i32>>("res")       // extremo de escrita, pela chave constante
    tx.send(processar())
    tx.close()                         // o produtor fecha
    tx.done()                          // done() pelo próprio handle (o ctx é transient, inalcançável aqui)
}

fn main() {
    var ctx = chan<i32>::make<OsChan<i32>>("res", 64)   // devolve o ctx (WaitGroup manual)
    ctx.add(1)                         // add() manual
    spawn tarefa()                     // thread fire-and-forget SEM id
    ctx.wait()                         // espera o done()
    // spawn soma(a, b)                 // ERRO: soma tem retorno — spawn não aceita
}
```

**A região do programa (F2):** depois que F1 parte a raiz única em N raízes de task, **não sobra raiz de
processo** para um singleton morar — cada task morre e sua raiz esvazia. Um `chan`, criado UMA vez pela
`main` e que sobrevive a todas as tasks, precisa de uma região **imortal, separada de qualquer raiz de
task**. F2 é essa região, e é **pré-requisito de F1**, não adicional.

### 7.7 NOMES, não ponteiros — a regra do dono para tudo que cruza fronteira de task

> *"a main abre o canal e o consumidor o resolve; toda implementação é um `service`, e a chave é uma
> constante — o worker resolve o `Tx` por `svc<Tx<T>>(\"chave\")`, sem receber id nenhum."* — dono

A razão é memória: **um ponteiro para a arena de OUTRA task pendura no instante em que essa task rebobina
seu `arena_pop`.** O que cruza a fronteira nunca é um ponteiro — é um **NOME**. E o nome do canal é a sua
**chave constante** (§7.8): resolvida **em comp-time** por `svc<Tx<T>>("chave")`, um intrínseco DI (§8) —
não há nem sequer um `u64` de runtime a passar, nem registro consultado por handle. É a forma mais forte da
regra: o "nome" é uma constante conhecida na compilação, e a instância do canal vive na raiz do programa
(F2) sob essa chave, então nada envelhece e **nada precisa reconstruir tipo** ao cruzar um `spawn`.

```teko
// O "nome" do canal é a CHAVE constante — não um id de runtime:
var ctx = chan<i32>::make<OsChan<i32>>("res", 64)   // a main cria e nomeia o canal; recebe o ctx (WaitGroup)
var rx  = svc<Rx<i32>>("res")                        // AMBOS os extremos por chave, em comp-time (sem id):
var tx  = svc<Tx<i32>>("res")                        // leitor e escritor resolvidos por svc
```

### 7.8 `chan<T>` — MPSC (fan-in), a primitiva de dados

`chan<T>` é um **tipo genérico**, aberto pela fábrica estática `make`, cujo parâmetro `bounds` tem
**valor default = 1** (usa a superfície nova de uma vez: genérico, `static fn`, `usize`, e **parâmetro com
default**). Tudo é operado pela **chave constante** do canal (a regra "NOMES, não ponteiros", §7.7):

```teko
// O TRANSPORTE é um contrato — a interface que qualquer canal satisfaz; TODO transporte é um `service singleton`:
type IChannelKind<T> = interface {
    fn init(key: str)   // método PRÉVIO: inicializa o transporte ligado à CHAVE constante do canal
    fn send(v: T)       // entrega uma mensagem
    fn recv(): T        // retira uma mensagem
    fn end()            // fecho + dreno
}

// make: cria o canal, chama K.init(key), registra o serviço na RAIZ DO PROGRAMA (F2) sob a CHAVE constante,
// e devolve o CONTEXTO (ctx) — o WaitGroup MANUAL do canal (add/done/wait, coordenados pelo usuário) + fecho.
// K é um `service singleton` que satisfaz a interface (senão: erro de compilação):
static fn chan<T>::make<K: service singleton & IChannelKind<T>>(key: str, bounds: usize = 1): Ctx | error
                                                   // error: conflito de chave (runtime) ou falha do K.init (abrir o transporte)
// checar existência antes de resolver (evita o panic do svc de chave ausente):
fn has_svc<T: service>(key: str | null = null): bool

// Os extremos são clamados por svc, pela MESMA chave constante (svc = comp-time, §8):
var rx  = svc<Rx<T>>("chave")                      // LEITOR único — resolvido por chave
var tx  = svc<Tx<T>>("chave")                      // ESCRITOR (N) — resolvido por chave (nada de id no spawn)

fn Tx<T>::send(v: T): null                         // devolve null; a propriedade tx.closed diz se fechou (drenado)
fn Tx<T>::close()                                  // o PRODUTOR fecha: invoca o end() do transporte (fecha + drena)
fn Rx<T>::pop(): T | Closed                        // recebe; o erro ESPECÍFICO `Closed` quando encerrado e drenado

// WaitGroup MANUAL — add/done disponíveis TAMBÉM nos handles (o `ctx` é transient, o worker não o alcança):
fn Tx<T>::add()                                    // um PRODUTOR se registra pelo SEU handle (incrementa o ctx)
fn Tx<T>::done()                                   // um PRODUTOR sinaliza fim pelo SEU handle (decrementa o ctx)
fn Rx<T>::add()                                    // um CONSUMIDOR se registra pelo SEU handle
fn Rx<T>::done()                                   // um CONSUMIDOR sinaliza fim pelo SEU handle
fn ctx::add(n: usize)                              // o CRIADOR registra N tasks (antes do spawn = race-free)
fn ctx::wait()                                     // o criador bloqueia até o contador zerar
fn ctx::close()                                    // fecho de reserva do canal (invoca end())
```

- **Resolução por CHAVE (não id nem ponteiro) — ruling do dono.** `make` e `svc` usam a MESMA chave; o
  **caso comum é a chave CONSTANTE** (literal/`const`), que torna `svc<Tx<T>>("chave")` um **intrínseco de
  comp-time** (§8), substituído inline — **sem id em runtime, sem vtable, sem dispatch dinâmico**. (Uma chave
  **variável** de runtime também é possível, com um custo — ver o próximo bullet.) **Elimina passar id no
  `spawn`**: o worker resolve o `Tx` pela chave. `make<K: service singleton & IChannelKind<T>>` — o transporte
  é um `service` que satisfaz a interface, e o `make` só precisa do tipo `K` + a chave.
- **REGISTRO (compilador) vs MATERIALIZAÇÃO (`make`) — dois modos de chave, ruling do dono.** `Ctx`/`Rx<T>`/
  `Tx<T>` **não** são serviços do usuário — são handles que o compilador registra e o `make` materializa. O
  registro tem **duas fases**, e o modo é escolhido pela **forma da chave**:
  - **Chave CONSTANTE (literal/`const`) — registro ESTÁTICO, resolução INLINE (o default, custo zero).** O
    compilador pareia cada `svc<…>("literal")` ao `make` da mesma constante, **conhece o `K`**, monomorfiza
    `Ctx`/`Rx`/`Tx` + as ops do transporte, e reserva o slot `(chan<T>, "literal") → (Ctx, Rx, Tx)` **inline**.
    O `make` só **MATERIALIZA** (chama `K.init`, abre o transporte, põe a instância no slot). **Sem lookup em
    runtime.** O conflito (mesmo `chan<T>` sob a mesma chave em dois `make`) é **erro de compilação**.
  - **Chave VARIÁVEL (`str` de runtime) — PRÉ-REGISTRO (comp-time) + FINALIZAÇÃO (runtime).** Para o caso em
    que a chave **não** é conhecida na compilação (um canal por conexão, por request, por usuário), o
    compilador **pré-registra a FORMA** — monomorfiza `(chan<T>, K)`: os handles por `T` e um **registro de
    ops** do transporte (`send`/`recv`/`end`). Mas a ligação **chave→instância** é **finalizada em runtime**
    pelo `make`, que insere a instância + o registro de ops num **registro processo-inteiro chaveado pela
    string** (em F2). O `svc<Rx<T>>(var_key)` vira então um **lookup em runtime** pela string, e as ops de
    `Rx`/`Tx` são uma **chamada indireta** (o ponteiro de função do registro de ops) — **não** vtable de
    interface, **não** o Round 3. **Custo:** um lookup por resolução + uma indireção por op. Conflito
    (mesma chave viva em dois `make`) sai como **`error`** — por isso `make` devolve **`Ctx | error`** (o
    `error` também cobre a falha do `K.init`, p.ex. abrir o socket). **`svc` de chave não encontrada = PANIC
    (ruling do dono)** — o `svc` continua infalível no tipo (devolve `T`), a chave ausente é `panic` (como um
    acesso fora de faixa). Para checar **antes** de resolver há **`has_svc<T: service>(key): bool`** — o dev
    faz o check e evita o panic.
  - **A diferença é se o `K` é conhecido no sítio do `svc`:** chave constante → `K` conhecido → inline; chave
    variável → `K` **apagado** no sítio (o key é variável, não pareável a um `make` específico) → indireção
    pelo registro de ops. Nos dois casos o **tipo `T` é estático** e **não há dispatch dinâmico de interface**.
- **`make` devolve o `ctx`; AMBOS os extremos por `svc` (ruling do dono).** `make(key, bounds = 1): ctx`
  cria o canal e devolve o **contexto** — o **WaitGroup** do canal e um fecho de reserva (`ctx.close()`).
  Os dois extremos são clamados por chave: `svc<Rx<T>>(key)` (leitor), `svc<Tx<T>>(key)` (N escritores); o
  `ctx` é o retorno do `make` (fica com o criador). O worker sinaliza fim pelo **seu handle** (`tx.done()`/`rx.done()`), não pelo ctx transient. Fan-in MPSC: **1 leitor**, **N escritores**.
- **O WaitGroup é MANUAL, e `add`/`done` estão disponíveis também nos handles (ruling do dono — não bloquear o
  dev).** A contagem é **do usuário**, nunca automática (nem no `spawn`, nem na saída da task). Como o `ctx` é
  **transient** (na região do criador) e um worker **não o alcança**, mas **sempre segura o seu `tx`/`rx`**
  (estável, F2), tanto o **`add`** quanto o **`done`** ficam **também nos handles**: `tx.add()`/`tx.done()`
  (produtor) e `rx.add()`/`rx.done()` (consumidor). O criador ainda tem `ctx.add(n)` e `ctx.wait()`. **Caveat
  (não trava):** se o dev adiciona **depois** do `spawn` (via handle) em vez de **antes** (via `ctx.add`), a
  corrida "add-depois-do-spawn" é **responsabilidade dele** — o `ctx.add(n)` antes do `spawn` é o caminho
  race-free, mas o dev **pode** coordenar como quiser.
- **Fechar é responsabilidade do PRODUTOR (ruling do dono).** O **`Tx` tem o `close()`** — quem sabe que não
  há mais mensagens é o produtor, então é ele que fecha (`tx.close()` → `end()`; convenção Go). Com N
  produtores, o usuário coordena por `ctx.add` + `tx.done()`/`rx.done()` + `ctx.wait()` e fecha-se **uma vez** (idempotente). O fecho também
  fica **no `ctx`** (`ctx.close()`) como reserva, **caso precise**. O **consumidor (`Rx`) não fecha** — faz `pop()` até
  receber o erro **específico `Closed`** (encerrado + drenado; distinto de um `error` de transporte). Ambos
  observam: `Rx` pelo `Closed`, `Tx` pela propriedade `tx.closed`.
- **`bounds` conta MENSAGENS, não bytes.** O teto limita quantas mensagens ficam em voo; o **tamanho de
  cada mensagem é responsabilidade do dev** — se ele enviar uma única mensagem de 10 GB, é por conta dele.
- **O transporte `K` é um `service singleton & IChannelKind<T>` extensível (ruling do dono).** O `make` chama
  `K.init(key)` (o método prévio), registra o canal sob a chave, devolve o `ctx`. Built-ins e plugues:
  - **`OsChan<T>`** (default): transporte do **SO** — `SOCK_DGRAM` no Linux/macOS, **mailslot** no Windows.
  - **`MemChan<T>`**: fila **em-processo** (anel na região imortal F2, §7.6), **sem syscall** — mais rápida.
  - **do dev**: `KafkaChan<T>`, `RabbitChan<T>`, `WsChan<T>`, `HttpChan<T>`, `UdpChan<T>`, `RpcChan<T>` —
    qualquer `service singleton` que implemente `init`/`send`/`recv`/`end`. Native-only: código Teko falando o
    protocolo, ou **link dinâmico FFI** a uma lib de sistema — nunca um `.c` local.
- **`singleton` garante UMA instância; o `make` a coloca em F2 — a exceção do canal.** O `singleton` do
  constraint garante **instância única** (é o que proíbe um transporte `transient`, que abriria um socket
  novo por resolução). Um singleton comum resolvido por `svc<T>()` (sem chave) vive na raiz da THREAD (§8);
  mas o canal é registrado pelo `make` sob uma **chave**, para comunicação ENTRE tasks, então essa instância
  única vive na **raiz do PROGRAMA (F2)** — por isso `svc<Rx<T>>("chave")`/`svc<Tx<T>>("chave")` de
  **qualquer thread** resolvem o MESMO canal. Config **por valor**, nunca um `ref` cruzando task (UAF, §9).
- **O `ctx` É O DONO do lifetime do canal — transient (ruling do dono, ratificado).** A instância vive em F2, mas
  **quem a possui é o `ctx`**: o `ctx` é **transient** (vive na região de quem chamou `make`). Quando o
  `ctx` cai (a região do criador sai de escopo), ele **cascateia o teardown**: `end()` do transporte →
  **desregistra a chave** de F2 → **libera** a entrada (o `service` singleton + `Rx` + `Tx`) de F2. Uma
  cascata só: o canal, o serviço e os dois extremos morrem **juntos com o `ctx`**.
- **O UAF é fechado por CONSTRUÇÃO — sem `mem::free` manual.** Duas coisas o garantem:
  1. **`ctx.wait()` é a barreira.** Ele bloqueia até TODAS as tasks (produtores/consumidores) terminarem;
     o criador chama `ctx.wait()` **antes** de deixar a região cair. Logo, no instante do teardown, **não há
     nenhum `Rx`/`Tx` vivo em uso** — é a mesma pré-condição do drop total (§2.2): nada vivo aponta para o
     que vai cair. (Há serviços que dependem de `Rx`/`Tx` finalizarem — o `ctx.wait()` é exatamente esse
     ponto de espera.)
  2. **Resolução por CHAVE, não ponteiro cacheado (§7.7).** Um worker nunca segura um ponteiro para o canal;
     resolve `svc<…>("chave")`. Depois do teardown a chave está desregistrada, então uma resolução tardia
     **falha** (não há serviço) em vez de pendurar. Não há borrow cruzando fronteira que envelheça.
- **O "free" é a reclamação POR-ENTRADA de F2, disciplinada pela arena (nova capacidade).** Como o serviço
  vive em **F2 (imortal)** e F2 não faz bulk-drop, o teardown do `ctx` precisa **remover só a entrada daquele
  canal** de F2 — um `free` direcionado, **disparado pelo drop do `ctx` transient**, não um `mem::free` do
  usuário. É a **única capacidade nova de arena** que isto exige: F2 ganha um **free-list/slab** para
  entradas de canal/journal, reclamáveis **individualmente** (o resto de F2 continua imortal). É a resposta
  ao trio "UAF + lifetime + free": o *lifetime* é do `ctx`, o *free* é a reclamação da entrada de F2 no drop
  do `ctx`, e o *UAF* é fechado pelo `ctx.wait()` + resolução-por-chave.
- **Conflito de chave = erro de compilação** (§7 Doc 2, política de DI): o mesmo `chan<T>` sob a mesma chave
  duas vezes é **erro em comp-time**; chaves distintas coexistem, nunca "último vence".
- **Conformidade de interface ESTÁTICA, não dispatch dinâmico do Round 3.** Tudo resolve por `(tipo, chave
  constante)` em comp-time; o `send`/`recv`/`end` do transporte é **monomorfizado**. Pré-requisito é só a
  conformidade estática de interface (que já existe) + o `service` DI por chave — **não** o Round 3.

`spawn` é uma **keyword de corotina** (não uma função) — estilo Go: `spawn f(args)` lança `f` numa corotina
**isolada** (sub-raiz própria, §7.6). Os argumentos vão **por cópia**, **nenhum retorno é esperado**, e
**não há `join`** — a sincronização é pelo `chan` (resultados) e por `WaitGroup` (esperar N terminarem).

**bounded — produtor rápido, consumidor lento.** O teto de 64 **mensagens** faz o produtor **esperar**
quando há 64 pendentes (contrapressão): a fila em voo nunca passa de 64, não cresce sem limite se o
consumidor atrasa.

```teko
fn gerar() {                                       // função sem retorno — SEM id: resolve por chave
    var tx = svc<Tx<Pedido>>("pedidos")            // extremo de escrita, pela chave constante
    for p in pedidos() {
        tx.send(p)              // 64 pendentes? ESPERA o consumidor — a fila não acumula
    }
    tx.close()                 // o PRODUTOR fecha → o pop do consumidor passará a devolver `Closed`
    tx.done()                  // done() pelo próprio handle (o ctx é transient, inalcançável no worker)
}

fn main() {
    var ctx = chan<Pedido>::make<OsChan<Pedido>>("pedidos", 64)   // cria em F2 sob "pedidos"; devolve o ctx
    ctx.add(1)                       // add() MANUAL — 1 task a esperar (coordenação do usuário)
    spawn gerar()                    // Go-style: dispara e segue (sem join) — SEM id
    var rx = svc<Rx<Pedido>>("pedidos")   // o LEITOR também por svc, pela chave
    loop {
        var m = rx.pop()
        match m {
            Pedido => processar_lento(m),   // lento; o produtor não corre à frente dele
            Closed => break                 // erro específico: encerrado e drenado — a sincronização, não há join
        }
    }
    ctx.wait()                       // bloqueia até o done() da task
}
```

**unbounded — enfileirar um lote fixo ANTES de existir consumidor.** Bounded daria **deadlock** (ninguém
drena enquanto a `main` enfileira). O `bounds = 0` não bloqueia, e é seguro porque o total é **conhecido e
pequeno**.

```teko
fn main() {
    var ctx = chan<Tarefa>::make<OsChan<Tarefa>>("tarefas", 0)   // unbounded; devolve o ctx
    var tx  = svc<Tx<Tarefa>>("tarefas")                          // escritor pela chave
    for tarefa in lote_fixo() {      // ex.: 12 tarefas conhecidas — total pequeno e finito
        tx.send(tarefa)              // nunca bloqueia; não há "cheio" para travar a main
    }
    tx.close()                       // o produtor fecha
    ctx.add(4)                       // MANUAL: 4 workers a esperar
    var i = 0
    loop while i < 4 { spawn worker(); i = i + 1 }   // cada worker: lê por svc<Rx<Tarefa>>, e faz rx.done() ao terminar
    ctx.wait()                       // bloqueia até os 4 done()
}
```

Não há `join` no modelo de corotina: a barreira de memória é o **fecho do canal** (o `pop` devolver o erro
específico `Closed` após `end()`) e/ou o `ctx.wait()` (WaitGroup).

### 7.9 `await` — alarga o retorno para `Intent<T>` (sem necessidade de `async`)

**Não há necessidade de uma keyword `async`, e a função NÃO declara `Intent`.** Uma função retorna o seu tipo normal
(`fn calc(x, y): i32`); é o **`await` — um PREFIXO de ligação/atribuição** — que ALARGA o retorno para
`Intent<T>`. Ao contrário de outras linguagens (que **estreitam** a assinatura para `Task<T>`/`Promise<T>`),
aqui a assinatura fica limpa e o **alargamento** acontece só onde se espera (não há `await` inline numa
expressão — liga-se primeiro):

```teko
fn calc(x: i32, y: i32): i32 { x + y }

await var a = calc(1, 2)       // a : Intent<i32> — o `await` PREFIXA a ligação; o retorno vai para .value
if a.canceled {
    println("canceled")
} else {
    println($"r = {a.value}")
}
```

`await f(args)` **suspende** a tarefa corrente (cooperativa, **cede o controle sem bloquear a thread**),
executa `f`, e o retorno de `f` **cai no campo `.value` de um `Intent<T>` criado no caller** (é por isso
que o Intent nasce na arena do caller). O `Intent<T>` carrega o desfecho: **`.value: T`** (o retorno) e
**`.canceled: bool`** (a task foi cancelada). É o oposto do `spawn` (fire-and-forget, sem retorno): no
`await` se GARANTE a execução, por suspensão.

**Várias tasks — atribuição múltipla (Doc 2 §9.3), sem `when_all`/`when_any` nem `await` de array.** O
`await` prefixa uma ligação múltipla e cada alvo vira um `Intent`, todas esperadas:

```teko
await var a, b, c = fa(), fb(), fc()   // a, b, c : Intent<…> — todas esperadas em paralelo
```

Como **não há throwing** (cancelada ou não, a task sempre executa até um desfecho), esperar todas é seguro;
inspeciona-se cada `Intent`. Isso torna `when_all`/`when_any` e o `await` de array desnecessários. O dev
nunca escreve `Intent` num retorno — só o `await` o produz.

**Descartar o retorno — `await _ = f()`.** Às vezes se quer a **garantia de execução** do `await` (esperar
`f` completar, por suspensão) sem **capturar** o desfecho — o equivalente a esperar uma `Task` em C# sem
guardar o resultado. Como o `await` é prefixo de uma ligação, o descarte usa o alvo `_` (o mesmo `_` do
resto da linguagem): liga, espera, e **não materializa** o `Intent`.

```teko
await _ = liberar_cache()      // espera completar; nenhum Intent capturado (nem .value nem .canceled)
await _, _ = fa(), fb()        // espera as duas; descarta ambos os desfechos
await _, x = fa(), fb()        // descarta o 1º, captura o 2º em x : Intent<…>
```

O `_` não abre variável nem arena: o compilador esperando o desfecho e o descartando na hora, sem alocar o
`Intent` no caller. Difere do `spawn` (fire-and-forget, **não** espera): `await _ = f()` **espera**, só não
guarda.

**`cancel()` — uma função global, como `panic`, mas ciente de suspensão.** Há uma **marcação de execução
suspensa** (o runtime sabe se a tarefa corrente está sob um `await`). `cancel()`:
- **em suspensão** → **cancela o `Intent`** corrente: marca `.canceled = true` e escreve a razão em
  `.failure` (`error | null`). O `await` retoma com esse desfecho — o chamador vê `.canceled` e reage.
- **fora de suspensão** (nada a cancelar) → **dispara um `panic`**, igualzinho ao `panic()` de hoje.

É um só verbo — "aborta isto" — que degrada de cancelamento cooperativo (quando há um `Intent`) para panic
(quando não há), sem o dev precisar saber em qual contexto está.

A arena difere conforme a fundação (I/O vs CPU), dita e não escondida (`concorrencia-isolate-spawn-chan` §8):

**O que o `Intent` carrega, e por que o dado cruza por cópia (regra do dono):**
- **`Intent<T>`** (genérico) é **criado na arena do caller** (quem faz o `await`) e carrega o desfecho:
  **`.value: T`** (a CÓPIA do retorno de `f`), **`.canceled: bool`**, e **`.failure: error | null`** (a
  razão do cancelamento). Erro-de-negócio fica no `.value` (se `f` retorna `T | error`); cancelamento é
  `.canceled` + `.failure`, no nível da task. Ele é **alimentado pela retomada**:
  quando o trabalho completa, a suspensão **escreve a cópia de `T` em `.value`** — **nunca uma referência**
  à arena da raia/continuação que produziu o valor (essa pode ter rebobinado). É o "dados cruzam só por
  cópia" entre tasks, e é destino-na-arena-do-caller, como o DPS (§5).
- **`Intent`** (não-genérico) é o desfecho de esperar uma **função SEM retorno**: só `.canceled`, sem
  `.value`. (`Intent` vs `Intent<T>` = o mesmo nome com aridade genérica distinta — é overload de TIPO,
  Doc 2 §9.)
- **`ref` NÃO cruza a fronteira de MT/async** (regra do dono, preservação de UAF): nem como argumento de
  `spawn`/`chan`/uma função esperada por `await`, nem embutido num genérico (`<ref T>` é rejeitado, §9). Um borrow que cruzasse
  penduraria quando a arena do outro lado dropasse. O que cruza é cópia (valor) ou id (`u64`), nunca borrow.

- **I/O cooperativo (uma só raia mutando a arena por vez):** o `Intent` de I/O **vive dentro do
  bloco/arena de quem o criou** — não tem arena própria, não cria thread de SO. `await` suspende a
  tarefa lógica sem bloquear a thread; um reator (`epoll`/`kqueue`/`IOCP`) por thread executora retoma.
  **Custo de arena: zero** — não precisa de F1, porque com uma só raia a garantia de F1 (ninguém
  rebobina a arena de outro) já vale de graça. Compõe com o `spawn`: uma task que faça I/O roda seu laço
  cooperativo na SUA arena.
- **CPU (`await f()` de trabalho pesado):** o corpo roda numa **corotina isolada de um pool** pré-aquecido
  (F1); o `await` **suspende** a tarefa que espera (sem bloquear a thread) até o `Intent<T>` ser alimentado
  ao completar, e então recolhe o resultado. Herda a arena-por-task inteira. **Não é um terceiro modelo de
  arena "leve".** (Diferente de `spawn`, que é fire-and-forget sem retorno.)
- **O que NÃO existe:** thread de verdade rodando Teko que **compartilha arena sem F1**. Custaria o mesmo
  (precisa de F1 para ser seguro) e entregaria menos — é o bug que F1 existe para fechar
  (`arena_push`/`pop` de duas raias sobre a mesma pilha se corrompem, sintoma nenhum).

### 7.10 Journaling — a faceta de arena da durabilidade

O journal segue **exatamente o mesmo modelo dos canais** (ruling do dono): o **sink** é um
`service singleton & IJournalKind` (default `FileJournal`), o `make<K: service singleton & IJournalKind>(key,
roll, fmt): ctx` recebe a **chave constante** e devolve o **ctx**, e o escritor é clamado por `svc<Jw>(key)`
(Doc 2 §10.4). O serviço **reside na raiz do programa** (F2, §7.6) — como o `chan`, porque um journal precisa
sobreviver a todas as tasks. Tem faceta de arena por dois pontos:

- **Segmento por escritor = a mesma disciplina de região-por-raia.** Cada escritor possui seu segmento e
  mais ninguém (sem lock, sem destino partilhado) — é o `encoded[i]` disjunto do §7.3 aplicado à
  durabilidade. O campo `Journal.seg` é opaco: um descritor de ficheiro hoje, um **índice de laje por
  raia** quando as threads chegarem — a troca é uma função (`journal_open_rt`), e **nada mais muda**.
- **A durabilidade mora FORA do buffer que morre com o processo** — o mesmo princípio da arena: estado
  que precisa sobreviver não pode viver onde vai ser reclamado. `append` é um `write(2)` em `O_APPEND`
  **sem buffer de userspace**: o registro está no kernel quando `append` retorna, então um `SIGKILL` não
  o perde (só uma queda de máquina perderia). Um buffer de userspace (um `FILE*` com stdio) morreria com
  o processo — exatamente o momento em que o ficheiro tinha valor. A sumarização **relê** (`fold`), não
  funde: nada por fundir que se perca, já estava escrito.
- **Rolling — um journal cresce, então rotaciona.** É aqui que os 10 GB moram (não no canal): o dev
  configura a **política de rolling** (por tamanho, por data, ou custom) e a **formatação** no
  `journal::make` (Doc 2 §10.4). O `append` verifica a política e rotaciona por `rename` atômico antes de
  escrever quando ela dispara; o `fold` relê todos os ficheiros do segmento em ordem, transparente ao
  rolling. O serviço do sink vive na raiz do programa (F2); os ficheiros vivem em disco.

---

## 8. DI — tempos de vida SÃO tempos de vida de arena

A injeção de dependência mapeia diretamente na árvore de regiões (detalhe pleno no Doc 2 / §7 do master
de superfície):

| lifetime | região | resolução |
|---|---|---|
| **singleton** | raiz (root) — once-guard, uma instância | slot na raiz |
| **scoped** | caminhada de ancestralidade de arena — a instância da arena-escopo do call-site | walk até a arena-escopo |
| **transient** | região corrente — instância nova a cada resolução | alloc na região atual |

- `svc<T: service>(key: str | null = null): T` é um **intrínseco de comp-time** (sem ABI; o compilador
  substitui o call-site inline por código por-lifetime). Não é uma chamada de runtime. O constraint
  `T: service` (Doc 2 §9.2b) garante que só serviços se resolvem; a `key` é opcional (sem chave = por tipo;
  com chave constante = por nome, ex.: `svc<Tx<T>>("chave")` do canal, §7.8).
- **Regra de escape:** um valor de serviço **nunca** é armazenado em campo, passado como argumento, ou
  retornado em código de usuário (`a.b = svc<S>()`, `fun(svc<S>())`, `return svc<S>()` são rejeitados). O
  backend é isento dessa proibição, mas os valores que ele segura permanecem **arena-bounded** (vivem na
  arena, dropam com ela) — é o caso especial das funções de background do backend sobre a arena.

**DI sob threads — a regra do dono (2026-08-10).** Cada thread tem uma **sub-raiz** própria (§7.6, F1), e
a resolução de DI é **da thread, não do programa** — justamente para não ressincronizar entre threads:

| lifetime | sob thread |
|---|---|
| **singleton** | vive na **raiz da THREAD**, não na raiz do programa — cada thread tem seu singleton, sem partilha nem lock cross-thread |
| **scoped** | resolve contra a arena-escopo **dentro da sub-raiz da thread** (fecha o ponto que estava aberto) |
| **transient** | região corrente da thread |

Isso mantém a disciplina "dados só cruzam por cópia/nome entre threads": um serviço resolvido numa thread
não é visível a outra por referência — se precisar atravessar, atravessa como valor por `chan`, nunca o
`Ref` do serviço.

**O transporte de canal (§7.8) é DI por CHAVE CONSTANTE.** O DI resolve por `svc<T>("chave")` — tipo +
chave, ambos em **comp-time**. O transporte do `chan` é exatamente isso: `make<K: service singleton & IChannelKind<T>>`
exige que K seja um **`service`**, e a instância vive na **raiz do programa (F2)** sob a **chave constante**
do canal. **`Tx`/`Rx` clamam o serviço por `svc<Tx<T>>("chave")`**, resolvido inline em comp-time — não há
id de runtime nem service-locator. É por isso que o worker não recebe id no `spawn` e **nada reconstrói o
tipo**: a chave constante + o tipo já dizem tudo em compilação; a instância (monomorfizada no `make`) vive
em F2 até o `close`. É a **exceção de lifetime**: o serviço do canal é raiz-de-PROGRAMA (F2), não raiz-de-
thread, porque é a primitiva de comunicação ENTRE tasks.

---

## 9. Invariantes de segurança e casos de borda (checklist)

1. **UAF:** vida da arena ⊇ escopo ⊇ todo uso. Um valor que escapa nasce na região que o cobre (DPS pro
   retorno; piso/AL3 pros acumuladores). Nunca dropar região com referência viva pendurada.
2. **Overflow:** piso pré-calculado + crescimento por chunk-list (§1.1) — chunk nunca estoura, encadeia.
3. **Aliasing:** exclusividade F1 por fluxo (`borrow.tks`), independente de keyword. O destino DPS é
   **single-writer-por-construção do controle de fluxo** (o caller sintetiza UM destino, passa a UM
   callee, não lê até o retorno; o callee escreve uma vez por caminho de retorno) — não vem de `let`.
4. **Conservadorismo:** dúvida → escapa / não elide / roteia para a região externa (leak-safe, nunca
   UAF).
5. **Sem drop prematuro:** o DPS não dropa nada sob o callee; ele redireciona ONDE o callee escreve. A
   falha do arena-por-escopo (bulk-drop com alias vivo) não recorre.
6. **Threads:** cada worker na sua região; resultado copiado para a lane-region antes do drop; nomes em
   `prog` imutável; fold em ordem de índice. **Nomes** entre tasks (chave constante do canal), nunca ponteiros.
7. **`ref` não cruza fronteira de concorrência** (regra do dono): `spawn`/`chan`/uma função esperada por `await` **rejeitam**
   `ref` como parâmetro/valor, e **`<ref T>` é proibido em genérico** — um borrow que atravessasse uma
   task/continuação penduraria quando a arena do outro lado dropasse. O que cruza é cópia (valor) ou **nome**
   (a chave constante do canal). É a mesma raiz do "NOMES, não ponteiros" (§7.7), estendida a todo borrow.

---

## 10. Exemplos (superfície 0.3.1)

**(a) Retorno virtual — o valor nasce na arena de quem chama:**
```teko
type Point = struct { x: i64, y: i64 }

fn make(cond: bool): Point {          // retorno de agregado → recebe destino do caller (DPS)
    return if cond { Point { x: 1, y: 2 } } else { Point { x: 3, y: 4 } }
    // cada braço do `if` em cauda escreve DIRETO no ret_dest do caller — sem slot de frame, sem cópia
}

fn use_it() {
    var p = make(true)                // `p` já é a storage do destino; nada é copiado para cá
    print(p.x)                        // lê da arena de `use_it`, que vive ⊇ este uso
}
```

**(b) Elisão — braço sem alocação não abre região:**
```teko
fn classify(n: i64): i64 {
    if n < 0 { return -1 }            // braço-folha sem alocação → NENHUMA região aberta (elidida)
    var buf = [n, n * 2]              // este escopo aloca → região aberta normalmente
    return buf[1]
}
```

**(c) Append in-place — AL3, não DPS:**
```teko
fn collect(xs: []i64): []i64 {
    var out = []                      // slice
    for x in xs {
        push(ref out, x * 2)          // grow_inplace: acrescenta sem abandonar quando há cap (AL3)
    }
    return out                        // o retorno do agregado ainda é DPS (nasce na arena do caller)
}
```

**(d) Threads — canal por chave constante, nunca ponteiro nem id:**
```teko
fn pipeline() {
    var ctx = chan<Msg>::make<OsChan<Msg>>("pipe", 1024)  // main cria o canal (serviço em F2), recebe o ctx
    var rx  = svc<Rx<Msg>>("pipe")    // leitor por chave
    ctx.add(1)                        // MANUAL
    spawn handler()                   // SEM id — o handler resolve tx/ctx por chave
    // dentro do handler: var w = svc<Tx<Msg>>("pipe"); … ; w.done()   // done() pelo handle; nunca um &canal (penduraria)
    ctx.wait()                        // bloqueia até o done()
}
```

---

## 11. Decisões — resolvidas pelo dono (2026-08-10) e o que resta

**Resolvidas (incorporadas acima):**
- **Piso = necessidade estática + cabeçalho da arena**, não 64 KiB default (§3). Elisão = o caso limite
  `need == 0` (§4).
- **`chan_unbounded`: PERMITIDO** — responsabilidade do dev, não da linguagem (§7.8).
- **Transporte do canal = `service singleton & IChannelKind<T>` extensível** (`interface { fn init(key); fn send(T);
  fn recv(): T; fn end() }`); built-ins `OsChan` (default) / `MemChan`, e o dev pluga Kafka/Rabbit/RPC/UDP/
  WS/HTTP. **Totalmente estático — DI por CHAVE CONSTANTE:** `make<K: service singleton & IChannelKind<T>>(key,
  bounds): ctx` cria e devolve o **ctx** (WaitGroup + fecho de reserva); **ambos** os extremos por chave —
  `svc<Rx<T>>("chave")` e `svc<Tx<T>>("chave")` (comp-time, inline). O serviço vive na **raiz do programa
  (F2)** (exceção de lifetime — não raiz-de-thread — por ser comunicação entre tasks), da abertura ao fecho.
  **Elimina passar id no `spawn`.** Fechar = do **produtor** (`tx.close()`; reserva em `ctx.close()`);
  `Rx::pop(): T | Closed` (erro específico `Closed` = encerrado); `Tx::send(): null` + `tx.closed`. Sem dispatch dinâmico do Round 3 (§7.8).
- **Lifetime do canal = o `ctx` (transient); o drop do `ctx` cascateia o teardown** (`end()` + desregistra a
  chave + libera a entrada de F2). **UAF fechado por construção** (`ctx.wait()` barreira + resolução por
  chave). **Nova capacidade de arena exigida:** F2 ganha **reclamação por-entrada** (free-list/slab) para
  canais/journals — um `free` direcionado disparado pelo drop do `ctx`, **não** `mem::free` manual (§7.8).
- **DI sob threads:** cada thread tem sub-raiz; **singleton vive na raiz da THREAD** (não do programa),
  scoped na sub-raiz da thread — sem ressincronizar (§8).
- **Concorrência: `spawn` (keyword, dispara função sem retorno como thread), `chan<T>`, `await`.** Não há
  **necessidade de `async`** — o `await` prefixo já alarga o retorno para `Intent<T>` por suspensão, então
  marcar a função seria redundante. Não há **necessidade de um `isolate`** — uma thread no mesmo processo
  ainda pode corromper e só fala por canais do SO; quem precisa de isolação real usa outro binário (§7.6/§7.9).
- **`Intent<T>`**: `.value`/`.canceled`/`.failure`; várias tasks por **atribuição múltipla**
  (`await var a, b = fa(), fb()`), sem `when_all`/`when_any`; **`cancel()`** global cancela o Intent ou dá
  panic (§7.9).
- **`ref` não cruza fronteira de concorrência; `<ref T>` proibido em genérico** — preserva UAF (§9).

**Nada a decidir na arena — as decisões estão fechadas.** A regra de elisão é direta: **`need == 0` → não
abre arena** (§4); `need > 0` → arena do tamanho `need + cabeçalho` (§3). O único "desconhecido" é
*quanto* cada mecanismo economiza em MB no total — isso se **mede depois** de implementar e **não muda o
desenho**. A superfície também está fechada (Doc 2 §12).
