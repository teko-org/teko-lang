---
section: design
created: 2026-07-27
source: ruling do owner ("tem que adiantar"), TEKO_MASTER_PLAN.md §Phase 10 S8, docs/memory/teko-laws-digest.md (escada da morte do C), docs/design/gate-sem-c-0.3.0.31.md, docs/design/romaneio-morte-do-c-031.md, docs/design/star-ref-and-ffi-0.3.1.md §4.4
status: DESENHO — nenhuma linha de produto escrita nesta carga; C1–C8 executáveis hoje, C9–C13 com dependência declarada
branch: cargo/20-concorrencia-adiantada (de ci/0.3.1-lanes-e-seeds @ a9808f0)
---

# Concorrência adiantada — S8 puxada do CAPSTONE para agora

> *"Tem que adiantar, e já bati nessa tecla centenas de vezes não à toa. Isso até permite que
> possamos executar a compilação em mais threads e agilizar os tempos de codegen e demais coisas
> como os testes (até os regressivos)."* — owner, 2026-07-27

Este documento existe porque a lei do projeto — issues-100% / no-deferral, `docs/memory/teko-laws-digest.md` —
determina que **peça futura exigida por uma falha real é adiantada**, e porque duas falhas reais,
medidas e registradas, exigem S8 agora:

1. **O degrau 0 da escada da morte do C está travado no gate de teste.** Três dos quatro sítios de
   emissão de C vivem no caminho de teste (`run_native_gate`, `run_analyzer`,
   `build_regression_cov_exe`), e o critério do owner é binário: quando gen1 compilar gen2 e ainda
   houver qualquer emissão de C — analisador, teste ou `teko.c` — foi feito errado. O desenho que o
   owner deu para o gate sem C **é um ISOLATE por `#test`**, com handler próprio de exit e panic.
   Sem o isolate não há esse gate.
2. **Um `#test` que falha mata a suíte inteira.** Medido no CI: `teko: deliberate panic: assertion
   failed: str_contains` → exit 134, e nenhum teste posterior roda. É a patologia do
   `all-diagnostics` reencarnada no runner — um erro escondendo todos os outros. O isolamento por
   isolate não é conforto: é o que faz o gate **relatar tudo**.

O documento responde, em ordem: o invariante de ordenação está satisfeito? qual é o chão sem C?
qual é a superfície? como o panic é capturado? quem sintetiza o `main`? como o determinismo é
garantido e por qual teste é afirmado? qual a ordem dos ganhos? e o que pode dar errado em silêncio?

---

## 1. O invariante de ordenação — parcialmente satisfeito, e o buraco não é o que o invariante nomeia

`TEKO_MASTER_PLAN.md`:252 fixa: *"single-task arenas+ref (S1–S3) before concurrency (S8)"*. O
estado medido, da própria linha 250 do plano:

| estágio | estado | consequência para S8 |
|---|---|---|
| **S1** arena primitive + root region | ✅ | o chão de alocação existe |
| **S2** scope regions + escape check | 🔶 **per-fn ✅ · block-arm ⬜** | a checagem de escape por comparação de profundidade existe; a granularidade fina não |
| **S3** `ref` (mutable-target only) | ✅ (entregue como `Ref<T>` em MEM-1) | ok |

**Resposta direta: o invariante está satisfeito para o propósito que ele enuncia, e mesmo assim S8
está bloqueada — por um pré-requisito que o invariante não nomeou.**

O invariante foi escrito para garantir que a disciplina de região *dentro de uma tarefa* estivesse
resolvida antes de haver várias tarefas. Ela está: regiões por função, escape por profundidade,
campanha de memória #148 fechada, fixpoint restaurado. O que ele não previu é que a arena de hoje é
**uma só, do processo inteiro, e sem alça**:

```c
// src/runtime/teko_rt.h:152-156
tk_region *tk_region_root(void);   // the process root region (lazy; never dropped in S1)
void       tk_arena_push(void);    // save the root region's current bump position
void       tk_arena_pop(void);     // free every root-region chunk allocated since the matching push
```

`tk_arena_push` e `tk_arena_pop` **não recebem parâmetro**. Elas empilham e desempilham marcas numa
pilha global sobre a região raiz do processo. E é exatamente isso que o gate faz hoje ao redor de
cada teste (`emit_test_call`: `arena_push` · … · `arena_pop`). Duas threads fazendo isso ao mesmo
tempo corrompem a pilha de marcas: o `pop` da thread A libera o que a thread B ainda está usando.

Portanto o pré-requisito real de S8 não é "S2 block-arm" — é **região raiz por tarefa**. Ele é o
crumb C4 abaixo, é caro, e não é opcional. O bloco de S2 (`block-arm`) continua devido por mérito
próprio, mas **não** é o que trava S8; dizer o contrário seria adiar S8 atrás da peça errada.

Achado adjacente, REPORTADO e não convertido em issue por mim: o registro de DI
(`tk_region_register`/`tk_region_lookup`, `teko_rt.h`:162-166) também é global e por região; um
`#singleton` resolvido concorrentemente em duas tarefas é uma segunda corrida da mesma família. S5
(DI lifetimes → arenas) está ⬜ e herdará esse problema inteiro.

---

## 2. O chão sem uma linha de C — a pergunta que decide a viabilidade

### 2.1 O precedente é sólido e o mecanismo é o certo

`examples/probes/arena_bottom/src/bottom.tks` atinge a libc **direto**:

```teko
pub extern fn c_aligned_alloc(alignment: u64, size: u64): u64 = "aligned_alloc"
```

Isso não é escrever C. É Teko **declarando um símbolo estrangeiro verbatim** e deixando o linker
resolver. No caminho nativo o mecanismo está inteiro e provado (`afdb1fd8`): `call_symbol` consulta
`find_extern_symbol` (`src/lir/lower.tks`:1369), o símbolo entra como `SHN_UNDEF`, e o
`R_X86_64_PLT32` já é emitido. Nada disso passa por `cc` como *compilador* — só como *linker*, que o
ruling preserva explicitamente.

### 2.2 O que as APIs de thread pedem, e o que `extern fn` já entrega

```c
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg);
int pthread_join(pthread_t thread, void **retval);
void pthread_exit(void *retval);
pthread_t pthread_self(void);
```

Item a item, contra o que o `extern fn` de hoje expressa:

| exigência da assinatura | expressável hoje? | por quê |
|---|---|---|
| `pthread_t` (opaco, 8 bytes em todo alvo 64-bit) | **sim** — `u64` | idêntico ao que `arena_bottom` já faz com endereços; ABI-fiel, ambos chegam no registrador inteiro |
| `pthread_attr_t *` passado como `NULL` | **sim** — `u64` com valor `0` | nenhum acesso, só travessia |
| `void *arg` (contexto) | **sim** — `ptr<byte>` de `teko::mem::buf_ptr`, ou `u64` | `buf_ptr(len): ptr<byte>` é builtin do checker (`src/checker/scope.tks`:366) |
| ler o `pthread_t` que `create` escreveu, para passar por VALOR ao `join` | **sim** — `teko::mem::bytes_from_ptr(p, 8)` + remontagem por deslocamento | builtin (`scope.tks`:638); não exige deref de `ptr<T>`, que não existe |
| seleção por sistema operacional (pthread × Win32) | **sim** — `#os("linux")` / `#os("windows")` | `os_guard` + `prune_os` (`src/build/project.tks`:110) já podam por alvo, em tempo de build, sem pré-processador |
| **`void *(*start)(void *)` — o ENDEREÇO de uma função Teko** | **NÃO** | é o único buraco |

### 2.3 O buraco é UM, é de superfície, e o backend já tem a peça

Não existe hoje nenhuma expressão Teko que renda o endereço nu de uma função. Um nome de função
usado como valor não vira endereço: `lower_var` desvia para `lower_fn_value`
(`src/lir/lower.tks`:1753), que **levanta um thunk env-first e constrói um literal de closure
`{fn, env}`** — um agregado de duas palavras cujo valor é o endereço do *registro*, não o da
função. Passar isso a um parâmetro de ponteiro de função entregaria à `pthread_create` algo que não
é chamável.

Mas o backend **já sabe** produzir o que falta:

```
// src/lir/lir.tks:118-126
/**
 * LFuncAddr — the address of a named function (a Ptr VReg): a function used
 * as a VALUE (`TVar.is_func`) or the callable slot of a closure/thunk literal
 * (`TLambda`). Distinct from `LGlobalAddr` so isel emits a text-section
 * relocation, not a data one.
 */
pub type LFuncAddr = struct { symbol: str }
```

`LFuncAddr` é literalmente "o endereço de uma função nomeada, com relocação de seção de texto". O
isel já o baixa. **O vão é de superfície e de checagem, não de geração de código.**

### 2.4 A forma do fecho — a que a lei já escolheu

Há duas formas possíveis e a lei desempata sem consulta ao owner.

A forma preguiçosa seria um intrínseco `fn_addr(f): u64`. Ela é **proibida pela mesma lei que já
rejeitou o `tk_cov_dump`**: `docs/design/gate-sem-c-0.3.0.31.md` §2.2(e) recusou passar `{ptr,len}`
onde se lê `char*` chamando isso de *"trocadilho de ABI — desonesto sob M.3"*. Um `u64` que é na
verdade um ponteiro de função é o mesmo trocadilho, agravado: qualquer inteiro passaria pela
checagem e viraria um salto.

A forma honesta já está **reservada e ratificada** em `docs/design/star-ref-and-ffi-0.3.1.md`
§4.4 (G3):

> *"**`cabi` fn-pointer callbacks** (G3): only NON-capturing closures/top-level fns coerce
> (env-first ABI dropped; capturing = reject); the own backend **emits a plain C-ABI function and
> takes its address** — native."*

Então o crumb 1 é **essa** peça, e nada além dela: um **tipo de parâmetro `cabi fn(T…): R`,
válido apenas em posição de parâmetro de `extern fn`**, que aceita como argumento **apenas** o nome
nu de uma função de topo não-capturante cujos parâmetros e retorno sejam escalares representáveis
na ABI C. Coerção → `LFuncAddr(mangle_fn_symbol(ns, name))`. Sem thunk, sem env — o `env-first ABI
dropped` do G3 é exatamente o que impede o desenho de depender por acidente do formato do thunk
levantado (que pode mudar e quebraria a entrada de thread em silêncio). O contexto da thread é um
parâmetro declarado da própria função, `ctx: ptr<byte>`, e não uma casa de ambiente implícita.

Deref, chamada através do valor, armazenamento em campo e closures capturantes **continuam
reservados** — nenhuma falha os exige, e o MASTER_PLAN:262 manda não congelar o que não precisa
congelar.

### 2.5 Veredito

**Sim: `pthread_create`/`pthread_join`/`CreateThread` cabem no padrão de `extern fn`, e o chão de
concorrência é alcançável sem uma linha de C.** O que falta no `extern fn` de hoje é exatamente um
item — o tipo de parâmetro `cabi fn(…): …` e a coerção de nome de função para ele — e a peça de
backend que ele precisa (`LFuncAddr`) já existe e já emite relocação de texto.

Corolário que vale registrar porque desfaz um bloqueio herdado: o cabeçalho de
`src/runtime/teko_rt.tks` declara que `print`/`panic` *"bottom out at the host write / the host
abort — the FFI bottom, crumb C1, DEFERRED"*. **Esse adiamento caiu com o `arena_bottom`.**
`write` e `abort` são símbolos de libc como `aligned_alloc` e `free`; o mesmo `extern fn` os
alcança. O que impedia não era o mecanismo, era ninguém ter tentado.

---

## 3. A superfície proposta — três camadas, uma congelada agora

O MASTER_PLAN reserva **cinco** primitivas (`scope{}`, `spawn`, `chan<T>`, `send`, `recv`) e
proíbe congelá-las *"until parser + real duplication data exist"* (:262). O ruling do owner manda
adiantar a concorrência. As duas coisas convivem quando se separa **capacidade** de **açúcar**:

> **A falha que força o adiantamento não pede sintaxe.** O `main` do gate é **sintetizado pelo
> compilador**, não escrito por humano; ele precisa de uma **biblioteca** chamável a partir de AST
> tipada. Idem o codegen paralelo. Portanto adiantar S8 = entregar a **capacidade completa** agora,
> em forma de biblioteca, e congelar as cinco palavras-chave quando existir o dado de duplicação
> que a lei exige — dado que essas mesmas cargas vão **produzir**.

Nada fica devendo: a capacidade é 100%. O que espera é a superfície de conveniência, e espera
**por lei escrita**, não por conveniência minha.

### 3.1 L0 — `teko::thread::sys`, o chão (não é superfície de usuário)

Ligações `extern fn` puras, guardadas por `#os`, contidas no vertical `unsafe` (U2: código seguro
não pode nomear tipo `unsafe`). Um arquivo por família de SO.

```teko
/**
 * Cria uma thread de SO 1:1 que começa a executar `entry` recebendo `ctx`.
 *
 * O identificador de thread é escrito por referência em `tid_out`, que deve apontar para pelo
 * menos 8 bytes vivos (o `pthread_t` de todo alvo POSIX 64-bit cabe em 8). O bloco é lido de volta
 * com `teko::mem::bytes_from_ptr` porque `join` recebe o identificador por VALOR — nenhuma outra
 * primitiva da API devolve o `pthread_t`, e inventar deref de `ptr<T>` para isto seria superfície
 * nova sem falha que a exija.
 *
 * @param tid_out  destino de 8 bytes onde o `pthread_t` é escrito
 * @param attr     atributos da thread; 0 seleciona os padrões da plataforma
 * @param entry    a rotina de entrada, com a ABI C `void *(*)(void *)`
 * @param ctx      o contexto opaco entregue a `entry` como único argumento
 * @return         0 em sucesso, o `errno` da plataforma caso contrário
 * @since S8
 */
#os("linux")
pub unsafe extern fn sys_thread_create(
    tid_out: ptr<byte>,
    attr: u64,
    entry: cabi fn(ptr<byte>): ptr<byte>,
    ctx: ptr<byte>
): i32 = "pthread_create"

/**
 * Bloqueia até a thread `tid` terminar e recolhe o valor com que ela terminou.
 *
 * @param tid         o identificador devolvido por `sys_thread_create`, lido de volta do seu bloco
 * @param retval_out  destino de 8 bytes para o valor de retorno da thread; 0 descarta
 * @return            0 em sucesso, o `errno` da plataforma caso contrário
 * @since S8
 */
#os("linux")
pub unsafe extern fn sys_thread_join(tid: u64, retval_out: u64): i32 = "pthread_join"

/**
 * Termina a thread chamadora imediatamente, devolvendo `retval` a quem a esperar.
 *
 * É esta a saída — e não `abort` — que o handler de panic de uma raia usa: ela mata a raia sem
 * derrubar o processo, e é o que permite ao gate relatar TODOS os testes em vez do primeiro que
 * falha.
 *
 * @param retval  o valor entregue a quem chamar `sys_thread_join`
 * @return        nada; a chamada não retorna
 * @since S8
 */
#os("linux")
pub unsafe extern fn sys_thread_exit(retval: u64) = "pthread_exit"

/**
 * O identificador da thread chamadora — a chave com que uma raia se reconhece na tabela de raias.
 *
 * @return  o `pthread_t` da thread corrente
 * @since S8
 */
#os("linux")
pub unsafe extern fn sys_thread_self(): u64 = "pthread_self"
```

O espelho `#os("windows")` liga `CreateThread` / `WaitForSingleObject` / `ExitThread` /
`GetCurrentThreadId` com a mesma forma. `#os("macos")` reusa o corpo POSIX.

### 3.2 L1 — `teko::isolate`, a capacidade completa

```teko
/**
 * Uma tarefa em voo: a alça do SO e a fatia de memória onde ela deposita o seu veredito.
 *
 * `unsafe type` porque carrega representação crua (U2, contenção por tipo): código seguro não a
 * nomeia, e o que sai daqui para o mundo seguro sai por cópia de valor.
 *
 * @since S8
 */
pub unsafe type Isolate = struct {
    /** O identificador de thread do SO, lido de volta do bloco que `sys_thread_create` escreveu. */
    handle: u64

    /** O índice da raia — a casa, e apenas a casa, em que esta tarefa pode escrever. */
    lane: u64
}

/**
 * Lança `entry` numa thread de SO 1:1, entregando-lhe `ctx`, e devolve a alça para esperá-la.
 *
 * A tarefa recebe o índice `lane` por convenção de contrato, não por mecanismo: `ctx` aponta para
 * o registro que o chamador montou, e a primeira palavra desse registro É o índice. A regra que
 * torna o conjunto determinístico está em `fork_join` e não aqui.
 *
 * @param entry  a rotina de entrada C-ABI, obtida por coerção de uma função de topo não-capturante
 * @param ctx    o registro de contexto da raia, vivo até o `join` correspondente
 * @param lane   o índice desta raia no plano de trabalho
 * @return       a alça da tarefa, ou o erro da plataforma quando a criação falha
 * @throws       quando o SO recusa criar a thread (limite de threads, memória)
 * @since S8
 */
pub unsafe fn spawn(entry: cabi fn(ptr<byte>): ptr<byte>, ctx: ptr<byte>, lane: u64): Isolate | error

/**
 * Espera `t` terminar. Depois deste retorno, tudo que a tarefa escreveu está visível ao chamador.
 *
 * O `join` é a ÚNICA barreira de memória do modelo: nenhuma leitura do que uma raia escreveu é
 * legítima antes dele. Não há leitura concorrente sancionada, e por isso não há necessidade de
 * ordenação de memória explícita na v1.
 *
 * @param t  a tarefa a esperar
 * @return   nada em sucesso
 * @throws   quando o SO recusa a junção (alça inválida, junção dupla)
 * @since S8
 */
pub unsafe fn join(t: Isolate): null | error

/**
 * Executa `count` itens de trabalho em até `lanes` threads e retorna quando o último terminar.
 *
 * ESTA É A PRIMITIVA QUE O PROJETO USA. Ela existe para tornar o determinismo estrutural em vez de
 * disciplinar: a atribuição de índices a raias é ESTÁTICA (item `i` pertence à raia `i % lanes`),
 * portanto reprodutível; cada raia escreve APENAS nas casas dos seus próprios índices, portanto
 * sem escrita compartilhada; e nenhuma raia produz saída, portanto sem entrelaçamento. Quem lê o
 * resultado é o chamador, depois da barreira, em ordem de índice.
 *
 * Não há roubo de trabalho na v1, e a ausência é deliberada: um índice roubado torna o escalonamento
 * dependente de tempo, e um escalonamento dependente de tempo não reproduz a falha que ele causou.
 *
 * @param count  quantos itens de trabalho existem
 * @param lanes  quantas threads usar; 0 pede `hardware_parallelism()`
 * @param entry  a rotina de raia C-ABI; recebe o registro de contexto da sua raia
 * @param ctx    o registro base de contexto, com espaço para `lanes` cabeçalhos de raia
 * @return       quantas raias foram efetivamente criadas
 * @throws       quando alguma raia não pôde ser criada; as já criadas são esperadas antes do erro
 * @since S8
 */
pub unsafe fn fork_join(count: u64, lanes: u64, entry: cabi fn(ptr<byte>): ptr<byte>, ctx: ptr<byte>): u64 | error

/**
 * Quantas threads o host consegue executar de fato em paralelo.
 *
 * @return  o número de processadores online, nunca menor que 1
 * @since S8
 */
pub fn hardware_parallelism(): u64
```

### 3.3 L2 — as cinco palavras-chave, RESERVADAS

> **GRAFIA ACTUALIZADA (ruling do dono, 2026-07-29):** a primitiva de canal escreve-se **`chan<T>`**
> — a forma curta, por coerência com os outros tipos curtos da linguagem. Este documento foi escrito
> com `channel<T>` e o token foi actualizado nesta secção; **a SUBSTÂNCIA não mudou uma vírgula.** A
> objecção abaixo (o canal é a única das cinco que ameaça o determinismo) continua a valer como
> argumento registado, e é respondida — não apagada — em
> `docs/design/harness-de-testes-gerado.md` §6.10, onde a decisão do dono de a adoptar para o
> harness convive com o perigo que esta secção nomeou.

`scope { }` / `spawn` / `chan<T>` / `send` / `recv` → `T | error` permanecem reservadas, com a
forma já registrada no MASTER_PLAN e a decisão de 1:1 OS threads primeiro **já honrada por L0/L1**
(M:N vira um backing sob a mesma superfície, sem mudança de assinatura). O dado de duplicação que a
lei exige para congelá-las é produzido pelas cargas C9–C13: quando o gate, o codegen e o regressor
estiverem todos escritos contra L1, o padrão repetido entre eles É o dado, e a sintaxe se desenha
sobre ele em vez de sobre suposição.

**`chan<T>` merece nota própria, porque a análise mudou o desenho.** Nenhum dos três ganhos que
o owner nomeou precisa de canal: gate, codegen e regressor são todos **fork-join sobre um intervalo
de índices, com escrita disjunta e leitura após barreira**. Canal é a primitiva de comunicação
*durante* a execução, e comunicação durante a execução é precisamente o que introduz ordem
dependente de tempo. Congelar `chan<T>` agora seria congelar a peça que os casos reais não usam
— e a única que ameaça o determinismo. Fica reservada com uma razão escrita, não por omissão.

---

## 4. O handler de exit e panic por raia

### 4.1 O que acontece hoje

`panic(msg)` baixa para `tk_panic_str` (`src/lir/lower.tks`:1216), que escreve
`teko: deliberate panic: <msg>` em stderr, imprime backtrace e chama `abort()` — SIGABRT, exit 134.
`exit(code)` baixa para `tk_exit`, que faz `tk_regions_free_all` e sai. **Ambos matam o processo.**
Numa suíte paralela isso é fatal em dois sentidos: mata as outras raias e faz o gate esconder todos
os defeitos menos um.

### 4.2 O que muda — e por que não é uma linha de C

A tentação é editar `teko_rt.c` para consultar uma flag por thread. **Está proibido e, mais
importante, é a direção errada:** o romaneio já condena `teko_rt.c` à morte, e `tk_panic_str` está
entre os símbolos que o caminho nativo nomeia (10 dos 156). O movimento certo não é remendar o C —
é **retargetar a tabela**.

`call_symbol` mapeia builtin → símbolo por tabela literal:

```
// src/lir/lower.tks:1215-1216
if last == "exit"  { return "tk_exit" }
if last == "panic" { return "tk_panic_str" }
```

O crumb C3 troca esses dois destinos por funções **em Teko** (`src/runtime/teko_rt.tks`), que
implementam o comportamento guardado e só tocam o host, no caminho não-guardado, por `extern fn`
para `write` e `abort` — o mesmo padrão de `arena_bottom`. Resultado: o handler de panic passa a
ser Teko, `tk_panic_str` fica órfão e sai junto com o resto do C. **Zero C novo, e o romaneio
avança em vez de ser contornado.**

```teko
/**
 * O ponto único de falha deliberada do programa (M.1): escreve a linha canônica de panic e
 * termina — o processo inteiro quando não há raia guardada, apenas a raia quando há.
 *
 * O comportamento não-guardado é BYTE-IDÊNTICO ao do runtime em C, marcador incluso
 * (`teko: deliberate panic: `), porque o exit 134 é contrato afirmado por fixture e por golden de
 * regressão; trocar o mecanismo sem trocar a saída é a única forma honesta de fazer esta migração.
 *
 * @param msg  a mensagem da guarda que disparou
 * @return     nada; a chamada não retorna nem para a raia nem para o processo
 * @since S8
 */
pub fn panic(msg: str)

/**
 * Registra a raia chamadora como guardada, ligando o seu identificador de thread à casa de
 * veredito `slot`. Enquanto guardada, um `panic` ou um `exit` dentro dela deposita o veredito em
 * `slot` e encerra APENAS a thread, via `sys_thread_exit`.
 *
 * O reconhecimento é por `sys_thread_self()` contra a tabela de raias, e não por armazenamento
 * local de thread: a tabela tem no máximo `lanes` entradas — uma dezena — e uma varredura linear
 * sobre ela custa menos que introduzir TLS na linguagem para um caso que ainda não mediu precisar.
 * `pthread_setspecific` fica como otimização com gatilho de medição, não como requisito.
 *
 * @param slot  a casa de veredito desta raia, dentro do bloco de resultados do plano
 * @return      nada
 * @since S8
 */
pub unsafe fn guard_lane(slot: ptr<byte>)

/**
 * Desfaz `guard_lane` para a thread chamadora — o caminho normal de saída de uma raia que terminou
 * sem panic. Uma raia que retorna sem desguardar deixa entrada morta na tabela, o que a próxima
 * varredura leria como veredito de outra raia; por isso o par é obrigatório e a fixture
 * `thread_lane_unguard_pairs` o afirma.
 *
 * @return  nada
 * @since S8
 */
pub unsafe fn unguard_lane()
```

### 4.3 Como a saída é atribuída ao teste certo

**Pela casa, nunca pelo fluxo.** Esta é a regra e é o que torna a atribuição imune a paralelismo:

- Uma raia **nunca imprime**. Ela preenche `resultado[i]` — veredito, código, e a mensagem, se
  houver — para cada índice `i` que lhe pertence.
- O pai imprime **depois da barreira**, percorrendo `0..count` em ordem. A saída é a mesma com 1
  raia e com 16, byte a byte, porque a ordem de impressão não é a ordem de execução.
- O rótulo `test <ns::name> ... ` deixa de ser impresso *antes* do teste (como hoje, para
  sobreviver ao abort) e passa a ser impresso *com* o veredito. Isso só é possível porque o
  veredito agora sempre existe — o que é a diferença entre um gate que aborta e um gate que relata.

### 4.4 O que este desenho NÃO captura, dito em voz alta

Uma falta de hardware — SIGSEGV, SIGBUS, divisão inteira por zero no nível da CPU — mata o processo
antes de qualquer handler Teko. Capturá-la exigiria `sigaction` com um handler C-ABI (que o `cabi
fn` do crumb C1 até torna expressável) executando trabalho async-signal-safe em Teko, e isso é um
perigo maior do que o problema.

A saída honesta, e ela custa nada no caminho feliz: o plano de raias grava o registro **"raia k
iniciou o índice i"** no arquivo de resultados **antes** de chamar o teste. Se o processo morre, o
pai lê o arquivo, sabe exatamente quais índices estavam em voo, e **reexecuta apenas esses,
serialmente, com `lanes = 1`**. Cada um recebe então o seu próprio veredito. Custo pago só quando
há queda; "relata tudo" preservado; nenhum handler de sinal.

---

## 5. O `main` sintetizado, e o `#test` como referência estática

### 5.1 Quem gera, em que fase

Um módulo novo, `src/build/gate.tks`, sintetiza o gate em **AST TIPADA** (`checker::TProgram` →
`checker::TProgram`), rodando **depois de check + monomorph e antes do lowering de LIR**. É o F2 do
`gate-sem-c-0.3.0.31.md`, com o corpo do `main` trocado de sequencial para fork-join.

Dois ajustes no lowering acompanham:

- `lower_item_function` (`src/lir/lower.tks`:5655) hoje faz `if f.is_test { return … }` — descarta
  todo corpo de teste. Sob modo gate, ele deixa de descartar. **Consequência que precisa ser dita:
  o backend próprio nunca foi exercitado sobre corpos de teste. É risco não medido, e é o primeiro
  a medir** (crumb C0).
- `lower_virtual_main` (:5684) sintetiza `main` a partir das statements soltas; sob modo gate, as
  soltas são descartadas e o `main` vem das statements sintetizadas.

### 5.2 Por que isso é referência ESTÁTICA e não AST pendurada

A pergunta do owner — *"faz com que os testes unitários façam uma referência estática ao código e
permite a execução limpa e sem ficar com a AST do código pendurada na memória"* — tem resposta
mecânica:

O gate sintetizado contém, para cada `#test`, um `checker::TCall` cujo callee resolve para o símbolo
manglado do teste. No lowering isso vira `LCall @<símbolo>` — uma referência de **link**. A AST é
consumida pelo lowering e descartada; o binário do gate carrega **símbolos**, não nós. É exatamente
o oposto do que `run_native_gate(dir, out_dir, prog: checker::TProgram, m, tty)` faz hoje, que é
entregar o `prog` inteiro ao emissor e mantê-lo vivo enquanto o gate roda.

### 5.3 Uma raia, não um isolate por teste

O desenho entrega **uma** função `cabi` de raia, não uma por teste:

```teko
/**
 * O corpo de uma raia do gate: guarda-se, percorre os índices que lhe pertencem, despacha cada um
 * para o símbolo do seu `#test`, e deposita o veredito na casa correspondente.
 *
 * O despacho é um `match` sobre o índice gerado com um braço por `#test`, e cada braço é uma
 * chamada DIRETA ao símbolo do teste. É isso que faz o binário do gate referenciar os testes
 * estaticamente: não há tabela de ponteiros, não há AST, há `call @<símbolo>`.
 *
 * Esta função é SINTETIZADA por `src/build/gate.tks`; ela não é escrita à mão em lugar nenhum, e a
 * assinatura aqui existe para fixar o contrato que o sintetizador deve produzir.
 *
 * @param ctx  o cabeçalho de raia: índice da raia, passo, contagem e base do bloco de vereditos
 * @return     0; o valor de retorno da thread não carrega informação — o veredito vai pela casa
 * @since S8
 */
cabi fn __tk_gate_lane(ctx: ptr<byte>): ptr<byte>
```

Uma raia por núcleo, cada uma varrendo `i = lane, lane + lanes, lane + 2*lanes, …`. Isso mantém
**uma** função `cabi` (portanto uma coerção, portanto uma superfície mínima em C1), dá controle
direto do grau de paralelismo, e faz de cada teste uma chamada estática dentro do `match`.

---

## 6. Determinismo — o requisito inegociável

`docs/design/bare-name-probe-family.md` deixou a regra: *"determinismo não é só um requisito do
fixpoint — é um instrumento de diagnóstico. Tornar uma ordem estável não conserta a dependência de
ordem; ela a torna reproduzível, e só então testável."* Paralelizar é o inverso: **desestabiliza**
ordens que hoje estão estáveis por acidente. Cada uma precisa ser tornada estável **por construção**
antes, não depois.

### 6.1 O runner — determinismo estrutural

Garantido por três regras, nenhuma delas disciplina de programador:

1. **Atribuição estática.** Índice `i` → raia `i % lanes`. Sem roubo de trabalho. A mesma execução
   com as mesmas raias produz o mesmo escalonamento, o que é o que permite reproduzir uma queda.
2. **Escrita disjunta.** Raia `k` escreve somente em `resultado[i]` para os seus `i`. Não há
   acumulador compartilhado, nem contador, nem lista com append.
3. **Saída só no pai, em ordem de índice, depois da barreira.** Nenhuma raia chama `print`.

**Afirmado por:** `thread_result_order_stable` — a mesma suíte com `lanes = 1`, `lanes = 4` e
`lanes = 16` produz stdout **byte-idêntico**; comparação por diff, exit 0 exigido nos três.

### 6.2 O codegen — a ameaça real, nomeada

O fixpoint `gen2 == gen3` é byte-idêntico. Paralelizar o codegen sem tocar em mais nada **quebra o
fixpoint**, e a causa não é entrelaçamento — é **nomenclatura ordinal**.

`lower_program` (`src/lir/lower.tks`:5484-5492) atravessa os itens do programa em série carregando
duas tabelas **cumulativas**:

```teko
mut lifted: []LFunc = teko::list::empty()
loop {
    let step = match lower_item(m, loose, prog.items[i], layouts, enums, lifted, externs, flat_symbols) { … }
    m = step.module
    loose = step.loose
    lifted = step.lifted
    i++
}
```

E as entradas dessas tabelas são nomeadas pelo **ordinal de inserção**:

```teko
fn rodata_symbol(index: u64): str { teko::str::concat(".Lstr", teko::u64_to_str(index)) }     // :4616
fn lift_thunk_symbol(id: u64): str { teko::str::concat(".Lclofn", teko::u64_to_str(id)) }      // :1619
```

Ou seja: **`.Lstr7` não significa nada; significa "o sétimo literal que este build encontrou"**.
Trocar a ordem de visita — e é isso que paralelizar faz — renomeia símbolos, muda relocações, muda
bytes, mata o fixpoint. E o diagnóstico apontaria para um nome de símbolo, nunca para a causa.

É a **mesma família** da sonda por nome nu: um identificador cujo significado depende da ordem de
descoberta. Hoje a ordenação de fontes (vagão 20) mascara o problema tornando a ordem estável —
mas estável **por um sort externo**, não por construção.

**A correção, que vale por si mesma:** nomear por conteúdo e por dono em vez de por ordinal.
`.Lstr` passa a ser endereçado pelo conteúdo dos bytes internados; `.Lclofn` passa a ser
`<símbolo-do-dono>__clo<n>`, com `n` ordinal **dentro do dono** — que é per-função e portanto
independente da ordem entre funções. Feito isso, cada item pode ser baixado numa tabela **privada**,
e a junção é concatenação em ordem de índice com deduplicação — determinística por construção, não
por convenção.

**Afirmado por dois testes, e os dois são necessários:**

- `fixpoint_lanes_invariant` — construir o mesmo projeto com `TEKO_LANES=1` e `TEKO_LANES=8` e
  comparar o **objeto** byte a byte. Exit 0 apenas se idênticos.
- `symbol_order_invariant` — construir o mesmo projeto duas vezes com a descoberta de fontes em
  ordens diferentes e comparar o objeto. **Este teste falha hoje**, antes de qualquer paralelismo,
  e essa falha é o achado: ele mede a dependência de ordem que o sort esconde. Escrevê-lo é o
  primeiro passo do crumb C11, e ele deve ser escrito mesmo que passe — se passar, a premissa cai
  e isso também é resultado (mesma disciplina do PONTO ABERTO 2 do `bare-name-probe-family.md`).

### 6.3 O que NÃO paralelizar, por decisão

O **front-end** (lex/parse/check/monomorph) fica serial na v1. A tabela de tipos com namespace
(#109) e a resolução de instâncias genéricas têm dependências de ordem de registro documentadas e
**ainda abertas** (`bare-name-probe-family.md`, pontos 1 e 2). Paralelizar sobre uma tabela cuja
dependência de ordem é conhecida e não fechada é fabricar intermitência exatamente onde o projeto
já sabe que há defeito. O front-end volta à mesa quando os dois pontos abertos fecharem.

---

## 7. A ordem dos ganhos, por valor medido sobre risco

Os três ganhos que o owner nomeou, ordenados — e um deles muda de lugar por causa de uma medição
que **falta**.

### 1º — o gate de teste. Valor alto, risco baixo, e é o único que desbloqueia.

É o único dos três que não é otimização: é o **degrau 0**. Três dos quatro sítios de emissão de C
estão nele, e o critério do owner é binário. Além disso entrega, de graça, o fim do "um erro
esconde todos" — que é ganho de diagnóstico, não de tempo.

Risco baixo por construção: o binário de gate é descartável, a saída tem golden, e a comparação
`lanes=1` × `lanes=N` é uma prova completa e barata.

### 2º — os regressivos. Valor alto e medido, risco baixo, mas a alavanca não é thread.

Medição já registrada (`teko-laws-digest.md`): **318 builds em 8m33s = 1,61 s por build**, custo
**fixo**, com fontes de no máximo 9 linhas; e os builds são **mutuamente independentes**. Um pool de
processos limitado sobre `teko::process::run` colhe isso — e **não precisa de concorrência na
linguagem**, porque cada build já é um processo.

A alavanca maior, porém, é anterior: o colapso em canais (`cargo/20-regressor-canais`) leva os 318
para um punhado, porque *variação de fonte não exige projeto separado — só variação de configuração
de build exige build separado*. Reduzir o denominador vale mais que dividir o numerador, e não tem
risco de determinismo nenhum. Portanto: **canais primeiro, pool de processos depois, thread nunca**
(aqui).

A ordem do trem já decidiu *"medir na .31, reestruturar na .32"* para o runner, justamente porque
paralelizar a captura mexe em ordem de saída. Este desenho não a contradiz; ele diz **qual** é a
regra que torna a reestruturação segura: a mesma da §6.1 — a captura vai para uma casa por cenário,
e o relatório é montado pelo pai em ordem de índice.

### 3º — o codegen. Valor NÃO ATRIBUÍDO, risco o mais alto dos três.

Os números que existem: `codegen` custa **271,2 s no x86_64** contra **88,1 s no arm64** na mesma
carga; a escada inteira é 726 s × 369 s.

**Esse 271,2 s não pode ser otimizado antes de ser atribuído, e há uma hipótese concreta de que
paralelizar o codegen do Teko não compre nada dele.** `docs/design/tempo-de-build-paridade-por-host.md`
(item T-4) registra que **`-O2` é superlinear no tamanho da unidade de tradução**. A fase chamada
"codegen" hoje inclui a compilação, por `cc -O2`, de uma TU única e enorme de C emitido. Se a maior
parte dos 271,2 s for `cc`, então **matar o emissor de C deleta o custo** e o paralelismo interno do
Teko não tem o que colher ali. Um fator 3,1× entre hosts na mesma carga de trabalho é, ele próprio,
sinal de que o dominante é algo com comportamento não-linear — não uma diferença de 3× de clock.

**A medição que falta, e como obtê-la:** cronometrar o caminho nativo por fase separadamente —
`lower_program`, isel, regalloc, encode, objfile — nos dois hosts, no mesmo projeto. A infraestrutura
de observabilidade já existe e é onde encaixar (`phase_begin`/`phase_end_ok` em
`src/build/project.tks`, o mesmo par que já reporta `emit test` e `cc test`). Sem essa atribuição, o
crumb C12 não deve ser executado: paralelizar contra o fixpoint para colher um ganho que talvez
esteja em outro processo é o pior negócio dos três.

---

## 8. O que pode dar errado em SILÊNCIO

Seção obrigatória porque divergência silenciosa é o modo de falha desta casa. Cada item traz o
**sintoma que NÃO aparece** e a guarda.

**8.1 — A arena global. O pior, e é bloqueante.**
`tk_arena_push()`/`tk_arena_pop()` não recebem alça: empilham marcas sobre a região raiz do
processo (`teko_rt.h`:152-156), e o gate faz push/pop ao redor de **cada** teste. Duas raias fazendo
isso concorrentemente rebobinam o ponteiro de bump uma sobre a outra: a alocação seguinte devolve o
mesmo endereço às duas, e elas escrevem uma por cima da outra.
**O sintoma é nenhum.** Não há free, não há acesso fora de bloco vivo — o próprio cabeçalho do
runtime registra que *"Arena reuse is invisible to ASan"*, e o `mem-paranoid` é o único oráculo que
vê reuso de arena. Um teste passa com valor errado e ninguém sabe.
**Guarda:** região raiz por tarefa (crumb C4) é **pré-requisito**, não seguimento. Nenhum crumb
posterior a C4 pode pousar antes dele.

**8.2 — A tabela de internamento é global.**
`tk_intern_get`/`tk_intern_put`/`tk_intern_reset` (`teko_rt.h`:292-299) são uma tabela de processo,
com `tk_intern_reset` como fronteira de passada. Dois `put` concorrentes rasgam a tabela; o `get`
devolve um valor **plausível e errado** — mesma forma da família da sonda nua.
**Guarda:** tabela por raia com junção pelo pai, ou internamento desligado nas raias. A escolha é do
crumb C12 e precisa de medição de custo antes.

**8.3 — Nomes ordinais de símbolo.**
`.Lstr<n>` / `.Lclofn<n>` (§6.2). Sintoma: o fixpoint quebra e o diff aponta um nome de símbolo, que
não é a causa. Guarda: C11, mais o `symbol_order_invariant`.

**8.4 — Globais de diagnóstico do runtime.**
`teko_rt.h`:613 registra que `tk_to_*` grava linha/coluna em **globais** que `tk_panic_cast` lê. Duas
raias falhando um cast concorrentemente reportam a posição uma da outra. O veredito estaria certo (o
teste falhou), a **posição** estaria errada — e é a posição que a pessoa vai depurar.
**Guarda:** essas posições migram para o registro da raia junto com o handler (crumb C7).

**8.5 — Os sinks de cobertura são globais.**
`cov_enter(idx)` / `cov_leave()` marcam "o teste corrente" num sink de processo. Sob paralelismo a
atribuição de linhas e branches ao teste vira sorteio. Sintoma: percentuais que variam entre
execuções e pisos de cobertura que oscilam sem que o código mude.
**Guarda:** sink por raia + junção do pai, e — a guarda barata — comparar o relatório de cobertura
com `lanes=1` e com `lanes=N`; qualquer diferença é bug, não ruído.

**8.6 — Dependência entre testes, hoje mascarada pela execução serial.**
A suíte roda em série desde sempre. Se algum teste depende de outro através da arena, da tabela de
internamento ou dos sinks, paralelizar torna a falha **intermitente** — o pior formato possível.
**Guarda, e é barata:** ANTES de qualquer paralelismo, rodar a suíte serialmente em **ordem
invertida** e em uma ordem embaralhada com semente fixa. O que quebrar ali é dependência real, e
teria sido descoberta como flake três meses depois. É o crumb C8, e ele pode falhar — falhar é o
resultado útil.

**8.7 — Entrelaçamento de saída.**
`tk_print` escreve num `FILE*` compartilhado. Duas raias imprimindo o rótulo `test X ... ` produzem
lixo que o golden pega **às vezes**. Guarda: a regra "raia não imprime" da §6.1 — estrutural, não
disciplinar.

**8.8 — `errno` e o retorno de `pthread_create`.**
`pthread_create` **não** define `errno`; devolve o código de erro. Ignorar o retorno numa raia que
falhou ao criar produz um `join` sobre alça inválida, cujo comportamento é indefinido e cujo sintoma
típico é travamento sob carga, não erro. Guarda: `spawn` devolve `Isolate | error` e o `error` é
verificado no sítio — afirmado por fixture com limite de threads artificialmente baixo.

**8.9 — Registro de DI por região.**
`tk_region_register`/`tk_region_lookup` (`teko_rt.h`:162-166) resolvem `#singleton` subindo a cadeia
de pais. Com raiz por tarefa, duas tarefas materializam **dois** singletons; com raiz compartilhada,
elas corrompem a tabela. Nenhuma das duas é o que o autor de um `#singleton` espera.
**REPORTADO, não resolvido aqui:** S5 (DI lifetimes → arenas) está ⬜ e este é um requisito novo
para ele. Até S5, a regra do gate é que nenhum `#test` guardado resolve DI — e isso precisa ser uma
**checagem**, não uma nota.

---

## 9. A sequência de crumbs

Cada crumb entrega algo isoladamente gate-ável. **Ponto ritual** = onde o gate completo tem de
passar: `teko build . --no-verify --release && ./bin/teko test .` + fixpoint `gen1 == gen2`
byte-idêntico + `scripts/no_emitted_c.sh`. Crumbs de desenho e de fixture pulam o fixpoint.

**Nota de forma, importante:** a pauta pedia fixtures "motor legado e nativo". **O motor legado foi aposentado em
2026-07-13 (#524)** — AOT nativo é o único motor. Toda fixture abaixo tem um único par
entrada → exit code, no caminho nativo. Registrar isso é obrigação; entregar um par que não existe
mais seria pior.

| # | crumb | entrega | desbloqueia | ritual |
|---|---|---|---|---|
| **C0** | **Sondar, não construir** | (a) `extern fn` aceita `ptr<byte>` em parâmetro e baixa nativamente? (b) `buf_ptr`/`bytes_from_ptr` baixam nativamente — **medido: NÃO**, a tabela de `call_symbol` (`lower.tks`:1215-1267) tem io, cov e arena, e nada mais; (c) o backend próprio baixa um corpo de `#test`? (nunca foi exercitado, `lower.tks`:5655); (d) **atribuir os 271,2 s** por fase, nos dois hosts | tudo; C12 **depende** de (d) | sem fixpoint |
| **C1** | **`cabi fn(T…): R` em parâmetro de `extern fn`** | tipo em posição de parâmetro; coerção de nome nu de função de topo não-capturante → `LFuncAddr(mangle_fn_symbol(ns, name))`; rejeição de capturante, de genérica, de método e de tipo não representável em ABI C | **todo o resto**; é o único vão do chão | **sim** |
| **C2** | **`buf_ptr`/`bytes_from_ptr` nativos** | duas entradas na tabela de `call_symbol` → `tk_region_alloc`/`tk_bytes_from_ptr`; espelha exatamente o que F3 fez para `cov_*`/`arena_*`; zero C novo | casas de resultado e leitura de `pthread_t` no caminho nativo | **sim** |
| **C3** | **`panic`/`exit` em Teko** | retargetar `call_symbol` de `tk_panic_str`/`tk_exit` para `teko::rt::panic`/`teko::rt::exit`, em Teko, com bottom por `extern fn` para `write`/`abort`. Saída não-guardada **byte-idêntica**, exit 134 preservado | C7; e fecha o *"FFI bottom, crumb C1, DEFERRED"* de `teko_rt.tks` | **sim** |
| **C4** | **Região raiz por tarefa** | `arena_push`/`pop`/`commit` passam a operar sobre a raiz da tarefa chamadora. É a peça de §8.1 e é **bloqueante** | C5 em diante | **sim** |
| **C5** | **`teko::thread::sys`** | ligações `extern fn` guardadas por `#os` (POSIX + Win32) | C6 | **sim** |
| **C6** | **`teko::isolate` (L1)** | `Isolate`, `spawn`, `join`, `fork_join`, `hardware_parallelism`; a lei do fork-join determinístico escrita no doc-comment de `fork_join` | C7, C10, C12 | **sim** |
| **C7** | **Raia guardada** | `guard_lane`/`unguard_lane`; `panic`/`exit` guardados depositam veredito e chamam `sys_thread_exit`; posições de diagnóstico migram para o registro da raia (§8.4) | C10 | **sim** |
| **C8** | **Sonda de dependência entre testes** | suíte serial em ordem invertida e em ordem embaralhada com semente. **Pode falhar — é o objetivo** | C10, com confiança em vez de esperança | sem fixpoint |
| **C9** | **`main` de gate sintetizado, `lanes = 1`** | `src/build/gate.tks`; `lower_item_function` para de descartar `is_test` sob modo gate; `run_native_gate` emite `.o` + `link_object`. Vereditos e exit code **idênticos** aos de hoje | C10; e o degrau 0 | **sim** |
| **C10** | **Gate paralelo** | `lanes > 1`; pai imprime em ordem de índice; política de reexecução serial dos índices em voo após queda fatal (§4.4) | o baseline VAZIO de `no_emitted_c.sh` | **sim** |
| **C11** | **Matar os nomes ordinais** | `.Lstr` por conteúdo; `.Lclofn` por dono + ordinal interno; tabelas privadas por item com junção ordenada | C12 | **sim** |
| **C12** | **Lowering paralelo atrás de `TEKO_LANES`, padrão 1** | só depois de C0(d) atribuir os 271,2 s | o ganho de codegen, se ele existir onde se supõe | **sim** |
| **C13** | **Pool de processos no regressor** | limitado sobre `teko::process::run`, captura por casa, relatório montado em ordem pelo pai. **Depois** do colapso em canais | o ganho de regressivo | **sim** |

### 9.1 Fixtures de regressão

| fixture | forma | exit esperado |
|---|---|---|
| `cabi_qsort_callback` | passa o endereço de uma comparadora de topo a `qsort` da libc e imprime o resultado ordenado. **Prova o crumb C1 inteiro sem uma única thread** — é a prova mais barata que existe e deve ser a primeira | 0 |
| `cabi_capturing_rejected` | lambda capturante em posição `cabi` | 1 (erro de compilação, canal `diagnostics`) |
| `cabi_generic_rejected` | função genérica em posição `cabi` | 1 (erro de compilação) |
| `cabi_nonabi_type_rejected` | função com parâmetro `str` (fat pointer) em posição `cabi` | 1 (erro de compilação) |
| `thread_spawn_join_one` | uma raia escreve um valor conhecido na sua casa; o pai lê depois do `join` | 0 |
| `thread_result_order_stable` | mesma carga com `lanes` 1, 4, 16; stdout comparado por diff entre as três | 0 nas três **e** diffs vazios |
| `thread_panic_isolated` | três raias, a do meio dá panic; as outras duas completam | não-zero, **e** stdout nomeia os três vereditos |
| `thread_exit_isolated` | uma raia chama `exit(3)`; vira veredito, não morte de processo | não-zero, **e** stdout nomeia todos |
| `thread_spawn_error_checked` | limite de threads baixado até `spawn` falhar; o `error` é tratado | não-zero, com mensagem, **sem** travamento |
| `arena_per_lane_isolation` | cada raia grava um padrão conhecido, faz `arena_pop` e relê; qualquer cruzamento falha | 0 |
| `thread_lane_unguard_pairs` | raia que retorna sem `unguard_lane` deve ser detectada | 1 |
| `gate_all_failures_reported` | um `.tkt` com três `#test` falhando; hoje o gate nomeia **um** | não-zero, **e** os três nomeados |
| `fixpoint_lanes_invariant` | build com `TEKO_LANES=1` e `TEKO_LANES=8`, objetos comparados byte a byte | 0 só se idênticos |
| `symbol_order_invariant` | mesmo projeto, duas ordens de descoberta, objetos comparados byte a byte. **Escrever mesmo que passe** | 0 só se idênticos |
| `suite_reverse_order` | suíte serial em ordem invertida | 0 |

---

## 10. Riscos, tensões de lei, e o que continua bloqueado

### 10.1 A tensão que se dissolve — e é o achado mais útil deste documento

`gate-sem-c-0.3.0.31.md` §4.2 elevou ao owner uma decisão: `tk_cov_dump(const char *)` não tem forma
chamável de Teko, e as três saídas listadas eram (1) superfície nova em C, (2) um trocadilho de ABI
rejeitado por M.3, (3) adiar a cobertura nativa. **O crumb C3 abre uma quarta, que não estava na
mesa e é melhor que as três:** quando `panic`/`exit`/`cov_dump` passam a ser funções **Teko** cujo
bottom é `extern fn` para símbolos de libc, não existe mais um `char*` do runtime C a alcançar — a
função escreve o arquivo ela mesma. **A decisão do owner em §4.2 deixa de ser necessária.** Não é
uma tensão resolvida por escolha; é uma tensão dissolvida por mudança de mecanismo, e é exatamente
o que o precedente do `arena_bottom` habilitou.

### 10.2 Tensão real, resolvida law-first: adiantar S8 × "reserved" do MASTER_PLAN

`no-deferral` manda adiantar a peça que a falha exige. MASTER_PLAN:262 proíbe congelar as cinco
primitivas antes de existirem dados de duplicação. **Passa nas duas** a leitura da §3: a falha exige
**capacidade**, não sintaxe; o `main` do gate é sintetizado, não escrito. L0+L1 entregam a capacidade
inteira, agora, incluindo o "1:1 OS threads primeiro" que o plano fixou. As palavras-chave congelam
quando o dado existir — e são estas cargas que o produzem. Nenhuma vírgula do issue fica devendo.

### 10.3 Riscos, com mitigação

| risco | mitigação |
|---|---|
| Corpos de `#test` **nunca** passaram pelo backend próprio (`lower.tks`:5655 os descarta) | C0(c) mede antes de C9 depender disso |
| Os 271,2 s podem ser majoritariamente `cc -O2`, e nesse caso C12 não compra nada | C0(d) atribui; C12 **não executa** sem a atribuição |
| `ptr<byte>` em parâmetro de `extern fn` pode não baixar nativamente | C0(a); se não baixar, o fallback é o idioma `u64` que `arena_bottom` já provou |
| `pthread_t` cabe em 8 bytes em todo alvo suportado, mas isso é premissa | afirmada por fixture que compara `sizeof` observado com 8 no início da suíte, em cada alvo |
| C4 (raiz por tarefa) é caro e toca a disciplina de memória inteira | é a mesma carga do arena em Teko (`docs/design/arena-em-teko.md`); alinhar as duas em vez de duplicar |

### 10.4 O que continua BLOQUEADO, explicitamente

- **C9 e C10 dependem do vão N2 do backend próprio.** `gate-sem-c-0.3.0.31.md` §2.3 mediu: o backend
  próprio não compila o programa do compilador — nem um programa de cinco linhas com interpolação —
  e o inventário estático são **53 paradas honestas distintas**, com a família de ponto flutuante
  inteira dentro. O binário do gate **é** o programa do compilador mais um `main`. Enquanto o N2 não
  fechar, C9/C10 não pousam. **C0 a C8 não dependem disso** e são executáveis hoje, em programas de
  sonda (`examples/probes/`), que é exatamente o veículo que o `arena_bottom` validou.
- **C13 depende do colapso em canais** (`cargo/20-regressor-canais`), pela §7.
- **C12 depende de C0(d)**, por decisão, não por mecanismo.

### 10.5 Achados adjacentes — REPORTADOS, não convertidos em issue

1. O registro de DI por região (`tk_region_register`/`tk_region_lookup`) é global e ganha requisito
   novo com tarefas; S5 está ⬜ e herda o problema inteiro (§8.9).
2. `.Lstr<n>`/`.Lclofn<n>` são dependência de ordem **hoje**, antes de qualquer paralelismo, e são
   membros da família da sonda por nome nu que `bare-name-probe-family.md` cataloga (§6.2).
3. As posições de diagnóstico de cast em globais de processo (`teko_rt.h`:613) misatribuem sob
   concorrência (§8.4).
4. Corpos de `#test` nunca foram lowerados pelo caminho nativo (`lower.tks`:5655) — cobertura de
   backend com um buraco do tamanho da suíte.

---

## 11. Como este documento se verifica

Toda afirmação de código acima é reproduzível com a árvore em mãos:

```sh
grep -n 'tk_arena_push\|tk_arena_pop\|tk_region_root' src/runtime/teko_rt.h    # sem alça: §1, §8.1
sed -n '118,126p'   src/lir/lir.tks                                            # LFuncAddr existe: §2.3
sed -n '1753,1765p' src/lir/lower.tks                                          # fn-as-value vira closure, não endereço: §2.3
sed -n '4616,4618p' src/lir/lower.tks                                          # .Lstr<ordinal>: §6.2
sed -n '1619,1621p' src/lir/lower.tks                                          # .Lclofn<ordinal>: §6.2
sed -n '5484,5492p' src/lir/lower.tks                                          # tabelas cumulativas em série: §6.2
sed -n '1215,1267p' src/lir/lower.tks                                          # a tabela inteira de builtins nativos: C0(b), C2, C3
grep -n 'if f.is_test' src/lir/lower.tks                                       # o descarte dos testes: §5.1, §10.5
grep -n 'buf_ptr_signature\|bytes_from_ptr' src/checker/scope.tks              # os builtins de bloco cru existem: §2.2
```

Se alguma dessas leituras divergir do que está escrito aqui, **o documento está errado e deve ser
corrigido antes de ser seguido** — não contornado.
