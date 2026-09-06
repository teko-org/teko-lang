# O modelo geral de memória POR ESCOPO da linguagem Teko — 0.3.1

Arquiteto, 2026-08-02. Ramo `cargo/0.3.1.0-memoria-modelo-arq` (de `origin/fix/union`).
Documento de DESENHO — nenhuma linha de produto. `bootstrap/teko.c` é SAÍDA.
Regra do dono honrada: **proposta com arquivo:linha, não contra-argumento; alarme só se provado.**

Este é o modelo de memória de EXECUÇÃO de programas Teko — vale em AMBOS os backends (C e nativo).
Não é o scratch do compilador (isso é `backend-memoria-por-funcao-0.3.1.md`, trabalho paralelo com
que este design COORDENA — ver §12).

---

## 0. A diretriz do dono que redefine o alvo (invariante central)

> *"Só deve vazar para root aquilo que precisamos que seja wide como: `wait_group` e `chan<>`;
> talvez uma outra feature que precise operar cross-threading."*

Isto DEMOLE a estratégia de escape atual. Hoje o modelo é M.1/M.5 (`escape.tks:9-12`):

> *"When the analysis cannot PROVE an allocation is frame-local it is treated as ESCAPING (a leak
> is safe; a use-after-free is a vulnerability)."*

E o reticulado de residência confirma-o na letra (`spine.tks:88`):

> *"`⊤` — the allocation site is unknown / joined-over. **Treated as `PtRoot` (the safe leak)**."*

**O `PtTop → PtRoot` como refúgio de incerteza está PROIBIDO como default.** A nova regra de
residência é DETERMINÍSTICA e MÍNIMA — UMA regra estrutural + UMA anotação (correção do dono,
2026-08-02):

> **Toda variável morre no fim do seu escopo LÉXICO.** "Escopo" aqui são os CINCO: bloco nu `{}`,
> corpo de `loop` (por iteração), braço de `if`/`else`, braço de `when`/match, corpo de `fn` (§4) —
> tratados idênticos. Para cruzar escopos: DESCENDO (para um callee/cadeia inferior) o caller REPASSA
> por argumento — nenhuma anotação; SUBINDO (`return`) MOVE-se para a arena do caller — nenhuma
> anotação. Para escapar de TODO escopo há exatamente duas origens: `#singleton` (declarado) OU ser
> cross-thread por natureza (`chan`/`wait_group`).

Reticulado interno que realiza a regra (granularidade da análise; o utilizador só vê "morre no
escopo | move no return | root"):

| tier (interno) | quem reside aqui | quando morre |
|---|---|---|
| **região do escopo `{}`** (default) | toda variável cujo join de usos não sai da chaveta — efêmera | na aresta de saída da chaveta |
| **região de frame** | caso particular: o `{}` é o corpo da função | na aresta de retorno |
| **arena do CALLER** (move) | um valor retornado — sobe UM nível, transitivamente N | com o escopo do caller que o recebeu |
| **ROOT / wide** | SÓ duas origens: `#singleton` declarado OU cross-thread estrutural | no fim do processo/task |

**Não há `#transient` nem `#scoped` no modelo de VARIÁVEIS** (correção do dono): `#transient` é o
default (tudo é efêmero ao fim do escopo) e `#scoped` é desnecessário (para uma cadeia inferior usar
algo escopado, basta o caller repassar por argumento). Só `#singleton` tem razão de existir para
variáveis. (O DI de CLASSES que usa `#singleton`/`#scoped`/`#transient` fica FORA do âmbito — não se
remove nem se refatora; ver §2a.)

A antiga **task-root como refúgio-de-incerteza deixa de ser um destino de residência.** A task-root
(`tk_region_root()`) sobrevive apenas como o que sempre foi de facto: **o frame do `main`** — o
escopo mais externo da task. **As duas origens legítimas do tier-wide, com a nuance de runtime:**

- **`#singleton`** → `tk_region_root()` (a task-root) — reusa o mapeamento DI JÁ existente
  (`codegen.tks:9564`, `di_scope_expr`: `DiKind::Singleton => "tk_region_root()"`). É a superfície
  DECLARATIVA de residência-root.
- **cross-thread** (`chan`/`wait_group`) → `tk_region_program()`, que "owned by NO task … survives
  both a task's `tk_arena_pop` and that task's exit … (channels, and whatever else must outlive the
  task that created it)" (`teko_rt.h:170-175`). O runtime já aloca os canais lá: `tk_names_cell_open`
  → *"allocated in the program region"* (`teko_rt.h:265`; `teko_rt.c:1842`).

Nuance a reportar (não forçar): num programa mono-task root e programa coincidem em tempo de vida (o
processo); num futuro multi-task, `#singleton` em `tk_region_root()` morre com a task enquanto o
cross-thread em `tk_region_program()` sobrevive-a. Se um `#singleton` precisar de ser genuinamente
cross-task, migra para programa — reconciliação REPORTADA para quando isolates/spawn existirem, não
uma mudança agora.

---

## 1. A prova de que remover o refúgio-root NÃO abre UAF (por estrutura)

O modelo antigo escolheu `PtRoot` sempre-que-em-dúvida porque root vive mais que tudo — uma
sobre-aproximação segura mas cega. Remover esse refúgio parece reabrir o UAF que M.1 fechou. **Não
reabre, e a prova é o próprio reticulado que já está em `spine.tks`.**

**Definição (a regra de residência):** a região onde um valor reside é o **JOIN (menor limite
superior, LUB) de todos os seus sítios de uso**, no reticulado de escopos aninhados ∪ pilha de
chamadas ∪ fronteira de thread. O `spine.tks` já computa esse join: `pt_join` é o LUB
(`spine.tks:495`, *"the points-to join (least upper bound)"*), com ranks `PtFrame`=0 < sítios
concretos=1 < `PtTop`=2 (`spine.tks:445-457`).

**Teorema (não-UAF):** alocar um valor na região = JOIN(usos) nunca é UAF.
**Prova:** o JOIN é, por definição de LUB, ⊒ (vive tanto ou mais que) TODO sítio de uso. Logo cada
uso ocorre DENTRO do tempo de vida da região escolhida. Um UAF exige um uso APÓS a morte da
residência — impossível quando a residência domina todos os usos. ∎

O modelo antigo pegava sempre no TOPO do reticulado (`PtRoot`) — correto mas grosseiro (o leak). O
novo pega no JOIN EXATO — que ainda domina todos os usos (logo continua sem UAF) mas é o mais
apertado possível (logo morre). **A correção (não-UAF) é preservada; só se perde a
sobre-conservadoria (o leak).** O que muda não é a segurança — é a PRECISÃO.

**Por que o JOIN é SEMPRE determinável sem cair no topo-genérico** (a exigência dura do dono — o
fallback "não sei → root" tem de morrer):

1. **Escopo `{}` é sintático.** O dono já o observou (`onde-a-limpeza-por-escopo-falha.md:45`):
   *"O roteamento já é POR DESTINO, não por posição léxica."* O escopo-dono de um local é o menor
   `{}` que contém todos os seus usos — visível no texto no sítio da atribuição.
2. **A travessia de `return` é sempre UM nível de caller.** Um valor que escapa da função sobe para
   quem chamou. Se o caller também o retorna, sobe mais um nível — TRANSITIVAMENTE, um valor
   retornado por N frames é movido N vezes; o seu join é o frame mais alto que o usa. Cada passo é
   um move seguro (o caller vive mais que o callee — §6).
3. **O único join que ultrapassa qualquer frame da pilha é o cross-thread.** Um dado partilhado
   entre threads não pertence a nenhum frame de nenhuma pilha — pertence ao processo. É EXATAMENTE
   o conjunto que o dono nomeou (`chan<>`, `wait_group`) → região de PROGRAMA.

Portanto o reticulado de residência tem exatamente estes topos legítimos: **um frame concreto da
pilha (alcançado por move)** OU **a região de programa (cross-thread)**. Não há terceiro. A
`PtRoot`-como-refúgio-genérico não é um tier — é a marca de uma análise que desistiu.

**O que fazer com um join genuinamente indeterminado (a análise ainda incompleta).** M.1 (nunca UAF)
é absoluto e não-negociável; uma análise incompleta TEM de ter uma saída segura. Mas essa saída
deixa de ser um default silencioso e passa a ser um **DEFEITO MEDIDO**:

- residência de fallback = região de PROGRAMA (⊒ tudo → nunca UAF, mesma segurança do antigo root);
- MAS marcada como `residence = unresolved` e **contada** por `TEKO_ARENA_OBS` (§11). O critério de
  aceitação não é "0 UAF" (isso é dado) — é **"a contagem de `unresolved` tende a zero no corpus
  real; só `chan`/`wait_group` residem em programa por DESIGN"**.
- alternativa law-first ratificável: em vez do fallback-programa, um **honest-stop do checker**
  (rejeitar o programa com "não consigo determinar a residência de `x`; anote ou reduza o escopo")
  — a mesma disciplina de `typer.tks:5404`. Recomendação: honest-stop para o corpus do compilador
  (que é auto-hospedado e queremos residência total), fallback-programa-medido para código de
  utilizador durante a migração. Ver a pergunta ao dono, §13.

**A M.1/M.5 nova, para colar em `escape.tks:9-12` e `spine.tks:88`:**

> A residência de um valor é o JOIN dos seus usos. Root (task-root) NÃO é destino de residência — é o
> frame do `main`. `chan`/`wait_group`/isolate residem na região de PROGRAMA por design. Todo o resto
> MORRE no seu escopo `{}` ou é MOVIDO para o caller. Uma residência que a análise não consiga apertar
> abaixo do topo é um DEFEITO MEDIDO (fallback-programa contado, ou honest-stop), nunca um leak-root
> silencioso aceite. Nunca UAF: a residência = LUB(usos) domina todos os usos por construção.

---

## 2. O conjunto CROSS-THREAD enumerado (as únicas residentes-programa) — arquivo:linha

Procurei chan/channel/wait_group/spawn/thread/task/isolate no checker e runtime. O conjunto:

| feature | estado medido (arquivo:linha) | residência |
|---|---|---|
| **`chan<>`** | superfície de linguagem AINDA NÃO existe (`spine.tks:1555`: *"No channel surface exists yet"*); o SEAM de runtime existe — F4 `tk_names_open(kind, tk_chan*)` (`teko_rt.h:244`), célula em `tk_region_program()` (`teko_rt.h:265`, `teko_rt.c:1842`); o gate de consumo de envio existe (`is_unique_at`, `spine.tks:1562`, contrato L2c `spine.tks:1553`) | **programa** |
| **`wait_group`** | nomeado pelo dono; sem superfície nem símbolo de runtime hoje (zero ocorrências) — FUTURO | **programa** (quando existir) |
| **isolate / `spawn`** | modelo ratificado (`codegen.tks:9579-9587`): cada isolate *"is born with its OWN root, «como se fosse outro programa»"*; `#arena_size` já abre uma raiz-árvore independente (`cg_frame_region_parent_expr → NULL`, `codegen.tks:9599-9601`). Sem `spawn` ainda | **raiz própria** (uma por isolate; NÃO a task-root partilhada) |
| **região de PROGRAMA** | `tk_region_program()` (`teko_rt.h:170-175`, `teko_rt.c:1687-1695`): *"one per process, owned by NO task"* — o assento nomeado para *"channels, and whatever else must outlive the task that created it"* | é o destino |
| **task** (`tk_task`) | `teko_rt.h:165-168`: cada task tem a SUA root; a root morre com a task — por isso o cross-thread NÃO reside na root, reside no programa | — |

**Veredito do conjunto:** hoje o cross-thread é **potencial**, não atual — `chan` e `wait_group` não
têm superfície. Isto é uma OPORTUNIDADE: o modelo de residência entra ANTES de os canais existirem,
então quando `chan`/`wait_group` chegarem já nascem com a residência-programa correta e o gate de
uniqueness (`is_unique_at`) já construído. A lista de residentes-programa é FECHADA e curta: só o que
atravessa thread. Qualquer outra coisa a aparecer em programa é o defeito medido do §1.

### 2a. `#singleton` para VARIÁVEIS — a superfície DECLARATIVA de root (reusa trilho existente)

O dono: *"quando se queira que um determinado ponteiro resida em root, implementar um atributo
`#singleton`."* **Confirmado no código: `#singleton` NÃO está deferido — já está implementado para
DI e JÁ ligado à residência-root.** Não é mecanismo novo; é ABRIR o atributo, hoje só type/class,
para o alvo BINDING.

Pontos de código (o trilho a reusar):

| peça | arquivo:linha |
|---|---|
| enum de lifetime | `ast.tks:384` — `pub type DiKind = enum { None; Singleton; Scoped; Transient }` |
| parse do atributo | `parse_decl.tks:908-944` (já parseia `#singleton`/`#scoped`/`#transient`) |
| mapeamento lifetime→região | `codegen.tks:9564` — `DiKind::Singleton => "tk_region_root()"` (`di_scope_expr`); `di_emit_cached_body` `:9540-9564` (lookup/register na região) |
| monotonicidade por profundidade JÁ provada | `safety-spine.md:464` (*"singleton ≤ scoped ≤ transient by region depth"*), `:472` (check DI-monotónico sobre o grafo de providers) |
| **LIMITE ATUAL a abrir** | `parse_decl.tks:1263` rejeita lifetime antes de `fn` (*"may only precede a `type`, not a function"*); `di.tks:102` (*"applies to a class"*) |

**O design:** `#singleton` num binding = o override explícito de residência-root, alocando/lendo de
`tk_region_root()` pelo MESMO `di_scope_expr` (`codegen.tks:9564`). O `ResidenceTier` de um binding
anotado `#singleton` é forçado a `Root` — curto-circuita o oráculo (não se computa join; a
declaração manda). É a única anotação de residência de variável; o default (sem anotação) é o escopo
`{}` (§1). Root passa a ter EXATAMENTE duas origens, ambas explícitas ou estruturais, nunca um
fallback: `#singleton` (declarado) e cross-thread (`chan`/`wait_group`).

**Reconciliação com o DI de classes (NÃO mudar):** o DI de classes continua a usar as três lifetimes
(`di.tks`, `codegen.tks:9451,9540-9564`) inalterado. A extensão é ADITIVA e cirúrgica: só a
superfície `#singleton` ganha um segundo alvo (binding), reusando `DiKind::Singleton` e o mapeamento
para root. `#scoped`/`#transient` permanecem exclusivos de classes DI e são REJEITADOS num binding
(o default de variável já É o efêmero-de-escopo, então não têm significado adicional). A
monotonicidade `singleton ≤ scoped ≤ transient by region depth` (`safety-spine.md:464`) reforça a
prova de não-UAF do §1: um `#singleton` é o topo (root, maior profundidade-de-vida), logo domina
qualquer uso — nunca UAF, por construção, exatamente como o teorema do LUB.

---

## 3. A unificação C+nativo — onde mora a fronteira, o que migra

### 3a. O achado que decide a fronteira

O **modelo semântico já É partilhado** — mora no checker e é `pub`:

- `escape.tks` — `fn_escaping_vars` (`escape.tks:347`), `binding_is_frame_local` (`:357`),
  `binding_is_block_local` (`:808`, a decisão N-níveis por bloco JÁ existe), `assign_routes_to_frame`
  (`:410`), `assign_frees_old` (`:575`). Todos `pub`; ambos os motores leem (`escape.tks:5-6`:
  *"the codegen reads this"*).
- `spine.tks` — o reticulado `PointsTo`/`Unique`, `pt_join` (`:495`), `is_unique_at` (`:1562`). A
  espinha transitiva.

O que NÃO é partilhado é o **LOWERING do ciclo de vida das regiões**:

- **Rota C** (`codegen.tks` consome a TAST diretamente): já emite `tk_region_new`/`tk_region_drop`
  em texto-C, com a pilha `[]RegionFrame` (`codegen.tks:7955`), `cg_enclosing_region_expr`
  (`:7963`), `want_block` com tecto 64 (`:5412`, `:7432`, `:8178`, `:8799`), drop na aresta de saída
  (`:5462`). **Funciona.**
- **Rota nativa** (`codegen.tks`→TAST→`lower.tks`→LIR→backend): NÃO emite NADA. Prova literal
  (`lower.tks:3390-3393`): *"the enclosing region may not be directly accessible at lowering time for
  the native backend (unlike C codegen, where `cg_enclosing_region_expr` tracks the open region frame
  stack). The native backend routes `buf_ptr` through `tk_region_alloc` with `tk_region_root()`"*.
  **Tudo cai na task-root → nada morre → o self-build nativo OOMa a ~15,8 GB.**

### 3b. A fronteira recomendada (law-first): oráculo no checker, lowering espelhado, primitiva única

Não migramos a rota C para consumir LIR (mudança gigante, fora de âmbito). Subimos a DECISÃO acima
dos dois motores e damos ao nativo o lowering que lhe falta:

1. **O ORÁCULO DE RESIDÊNCIA vive no checker** — `spine.tks` (transitivo) como fonte-de-verdade,
   com `escape.tks` como fast-path conservador. Produz, por função, um **plano de regiões** puro:
   para cada binding, a sua residência (bloco-B / frame / caller / programa); para cada `return`, se
   é move e para onde. É um artefacto do checker que AMBOS os lowerings consomem IDENTICAMENTE — a
   única forma de C e nativo nunca divergirem (`escape.tks:405-409` já avisa: *"they must never
   disagree"*). Nome do artefacto: `ResidencePlan` (§14).

2. **O nativo GANHA o lowering em falta** em `lower.tks`, espelhando o que `codegen.tks` já faz,
   lendo os MESMOS `pub` do checker. Em vez de replicar a pilha `[]RegionFrame` roteada sítio-a-sítio
   (inviável no LIR — milhares de `list::push` implícitos), usa a **primitiva de região-corrente
   `tk_region_enter`/`tk_region_leave`** (a mesma que o trabalho `backend-memoria` adiciona ao
   runtime — COORDENAR, não duplicar, §12): na entrada de um escopo `{}` com locais, emite
   `region_enter(tk_region_new(<corrente>))`; na aresta de saída, `region_leave()` +
   `tk_region_drop`. O `tk_alloc` default passa a mirar a região-corrente, então TODO `list::push`
   implícito do bloco cai lá sem rotear nada.

3. **A primitiva de runtime é ÚNICA e partilhada**: `tk_region_enter(tk_region*)` /
   `tk_region_leave(void)` em `teko_rt.{c,h}` (C MANTIDA — exceção explícita ao congelamento). O
   `backend-memoria` já a define para o scratch do compilador (crumb C1 lá); este modelo REUSA-A
   para as regiões de escopo do programa gerado. Uma primitiva, dois consumidores.

**O que MIGRA de `codegen.tks` para o checker (`spine.tks`/novo `residence.tks`):** a DECISÃO de
abrir região por bloco (hoje o predicado `cg_block_has_block_local` + `want_block` embutidos no
codegen, `:8120`, `:5412`) e a de rotear cada binding — sobem para funções `pub` do checker que
devolvem o `ResidencePlan`. `codegen.tks` passa a LER o plano em vez de re-derivar inline (refactor
opcional; pode manter-se a ler os `pub` atuais na fase 1). `lower.tks` lê o MESMO plano.

**O que NÃO migra:** a emissão concreta (texto-C em `codegen.tks`; instruções LIR em `lower.tks`).
Cada motor lowera o mesmo plano à sua maneira. É o padrão já usado por `binding_is_block_local`
(decisão no checker, emissão em cada motor).

---

## 4. P1 — o escopo léxico: o seletor de N NÍVEIS

**"Escopo `{}`" é TODO escopo léxico, não o bloco nu** (correção do dono, 2026-08-02): *"quando disse
bloco {} não me referi apenas a bloco nu, mas também a loop, if, when, fn."* A regra "toda variável
morre no fim do seu escopo" aplica-se UNIFORMEMENTE aos CINCO tipos:

| escopo léxico | região hoje (rota C) | larga em |
|---|---|---|
| bloco nu `{ }` | `_tkbr` (`codegen.tks:8178-8187`) | aresta de saída do bloco |
| corpo de `loop` | `_tkbr` por iteração (`codegen.tks:8799-8803`) | **aresta de saída de CADA iteração** (`:8810`, *"drop the loop body's region"*) |
| braço de `if`/`else` | `_tkbr` value-arm (`codegen.tks:5440-5462`) | aresta de saída do braço |
| braço de `when`/match | `_tkbr` value-arm (mesma via `emit_branch_value`) | aresta de saída do braço |
| corpo de `fn` | `_tkfr` frame (`codegen.tks:9658-9662`) | aresta de retorno |

**Confirmação da generalização (não são casos à parte):** os cinco já usam o MESMO mecanismo —
`tk_region_new(<corrente>)` na entrada, `tk_region_drop` na aresta de saída, com defers-antes-do-drop.
O `ResidencePlan` associa cada binding ao seu escopo-dono QUALQUER que seja o tipo (o índice do
escopo, não o seu sabor sintático); o lowering trata os cinco idênticos. O loop NÃO é exceção — a
sua "aresta de saída" é per-iteração, então a disciplina região-por-iteração (`emit_loop_while`) É a
regra geral de morte-no-escopo aplicada a um escopo cuja saída ocorre a cada volta. Onde o texto
abaixo diz "bloco", leia "escopo léxico" (os cinco).

**O estado:** o roteamento tem hoje DOIS níveis (`onde-a-limpeza…:49`): moldura da função OU raiz. O
seletor `frame` (`codegen.tks:3726-3733`) escolhe `tk_slice_push` (root) / `tk_slice_push_r(…,frame)`
(moldura) / `tk_slice_push_fo` (linear). A pilha `[]RegionFrame` JÁ dá o pai N-níveis para o auto-box
(`cg_enclosing_region_expr(regions)` devolve a região do bloco interno, `:7963-7965`) — MAS o `frame`
que roteia o crescimento de slice continua a ser passado como `""` mesmo dentro de um bloco com
região aberta (`codegen.tks:5451`, o `emit_stmt` recebe `""`). São **duas portas** e o dono já as
separou (`onde-a-limpeza…:58`): o predicado trava a ABERTURA da região; o seletor `frame` trava o
ROTEAMENTO. A abertura já é N-níveis; o roteamento ainda é 2.

**O design:** o `ResidencePlan` dá, para cada binding, a **região exata do bloco dono** (não a
moldura). O seletor passa a receber o NOME dessa região (a variável `_tkbr<n>` do bloco, já criada
em `codegen.tks:5440-5442`), não `""` nem `_tkfr`. Mecanismo idêntico ao atual — muda o argumento:
`frame` passa a ser "a região-corrente do escopo dono do destino", que na rota C é
`cg_enclosing_region_expr(regions)` e na rota nativa é a região-corrente do `tk_region_enter`. Reusa
a árvore de regiões-filhas do runtime (`tk_region_new(parent)`/`tk_region_drop`, `teko_rt.h:149-152`)
— cada `{}` abre uma filha da corrente; o drop na aresta de saída é o que `codegen.tks:5462` já faz
para os value-arms, generalizado a todo `{}` com locais.

Verdade das três perguntas do dono, P1: um literal/variável declarado num `{}` cujo join de usos NÃO
sai da chaveta reside na região desse `{}` e morre na sua aresta de saída. `binding_is_block_local`
(`escape.tks:808`) JÁ prova a condição (todos os usos dentro de B, não-tail, não-assign-RHS). Falta:
(a) o nativo herdar o lowering; (b) o seletor de crescimento de slice/str usar a região do bloco, não
a moldura; (c) sub-regiões por omissão (§8).

---

## 5. P2 — o MOVE-ON-RETURN: a espinha transitiva, provada

**O estado:** `return` é posição de escape → alocado em root → *"leaked, which is SAFE"*
(`escape.tks:7,11,328`). O move-para-o-caller NÃO existe em backend nenhum. `typer.tks:5404` é o
honest-stop da espinha: *"a reference to a local … cannot escape until the transitive-escape spine
lands"*.

**O design — a residência de um valor retornado é a arena do CALLER, não a root.**

Prova de segurança por estrutura (o oposto do leak-preguiçoso):
- A disciplina de pilha garante que a região de frame do callee é criada DEPOIS da região ativa do
  caller e largada ANTES de o controlo voltar ao caller. Logo `região_caller ⊒ região_callee`
  SEMPRE.
- Mover o valor retornado para a região do caller ANTES de largar a do callee aloca-o numa região
  que vive MAIS que a origem — nunca UAF (é o LUB do §1 aplicado à fronteira de chamada). É o
  INVERSO do UAF: o UAF seria alocar na do callee e usar no caller; o move faz o contrário.

**Como identificar a região-destino do caller no sítio do `return`.** Duas opções ratificáveis:

- **(A) Região-corrente = região do caller (recomendada).** Com `tk_region_enter`/`leave`, a
  região-corrente no momento da chamada É a região ativa do caller. O callee aloca os valores
  destinados ao retorno na região-CORRENTE (a do caller), e os seus locais não-escapantes na sua
  própria filha (`tk_region_enter`'d à entrada, `leave`'d + dropped à saída). O seletor N-níveis do
  §4 estende-se naturalmente através da fronteira de chamada: destino = retorno ⇒ região-corrente
  (caller); destino = local ⇒ região do bloco/frame. **Sem mudança de ABI** — a região-corrente é
  thread-local, não um parâmetro.
- **(B) Cópia-out no sítio da chamada.** O caller, imediatamente após a chamada e antes de largar a
  região temporária do callee, copia (deep, transitivo) o valor retornado da região do callee para a
  sua própria. Custo: uma cópia por retorno não-escalar; precisa de um walk transitivo do valor.
  Menos limpo; fica como plano-B.

**Recomendação law-first: (A).** Reusa a primitiva única `enter`/`leave`, casa com o seletor
N-níveis, não toca a ABI, e o "destino = corrente" é o mesmo mecanismo do escopo `{}` um nível acima.

**O que roteia o quê no move (A):** os valores cujo `PointsTo` é `PtFrame` (nascem no frame do
callee) E cujo join inclui o `return` sobem para `PtParam`-tier (a região do caller). Um valor que já
é `PtParam` (aponta para storage de um parâmetro = já é do caller) permanece — é identidade
pass-down, o caso que `typer.tks:5404` hoje é o ÚNICO que aceita. O move GENERALIZA o pass-down de
"só um `ref` param" para "qualquer valor de frame cujo único escape é o retorno".

**A interação com o alarme UAF do dono:** o move CORRETO elimina o leak SEM abrir UAF porque a
residência (caller) domina todos os usos (o valor deixa de ser usado no callee após o `return`; passa
a ser usado no caller, que é a residência). O leak-preguiçoso atual (root) também dominava — mas
grosseiramente e para sempre. O move troca "domina para sempre (leak)" por "domina exatamente até o
caller acabar (morre)".

Verdade da pergunta 2 do dono: SIM quando (A) estiver ligado e a espinha transitiva
(`spine.tks` como oráculo) determinar que o único escape do valor é o retorno.

---

## 6. A interação MOVE × CROSS-THREAD (retornado E capturado por um `chan`)

O caso duro que o dono pediu. Um valor que é ao mesmo tempo retornado E enviado num `chan`.

**Resolução pelo reticulado + afinidade, tudo já em `spine.tks`:**

1. **Residência = o JOIN dos dois usos.** Um uso é o retorno (tier caller); o outro é
   `chan.send` (tier programa, cross-thread). O LUB de {caller, programa} é **programa** (o
   cross-thread domina). Logo o valor reside na **região de PROGRAMA**, NÃO é movido para o caller.
   O move aplica-se SÓ quando o join é exatamente "um frame acima e nada cross-thread".
2. **A afinidade impede o duplo-vivo.** `spine.tks` tem a disciplina de uniqueness: `chan.send(x)`
   CONSOME `x` e o gate `is_unique_at` (`spine.tks:1562`, contrato L2c `:1553-1556`) REJEITA enviar
   um valor `UsShared`/`UsTop`. Ou seja: se `x` é enviado num canal, `x` é movido para o canal
   (consumido) e NÃO pode continuar vivo para ser retornado — a menos que seja `UsUnique` e o envio
   seja o ÚLTIMO uso. Se o programa tenta retornar E enviar o MESMO `x` ambos vivos, `x` é `UsShared`
   → o `chan.send` é rejeitado no check (não é um leak, não é um UAF — é um erro de compilação
   honesto, a mesma disciplina do `mem::free` afim, `spine.tks:1547-1556`).
3. **Portanto os três desfechos possíveis são todos seguros e nenhum vaza indevidamente:**
   - `x` só retornado → **move para o caller** (§5).
   - `x` só enviado num `chan` → **consumido para a região de programa** (o canal detém-no).
   - `x` retornado E enviado, ambos vivos → **rejeição de compilação** (`UsShared` no gate de send).
   - `x` enviado e depois o HANDLE do canal é retornado → o canal (programa) é a residência; o handle
     retornado é um nome (`tk_names`, `teko_rt.h:244`), não o payload — o move do handle é trivial.

Nenhum caso precisa da task-root. O cross-thread vai para programa por design; o resto move ou morre.

---

## 7. #arena_depth (P4) — DEFAULT = 1 (a granularidade fina), NÃO o tecto-64

**Correção do dono (2026-08-02): `#arena_depth`, por padrão, deve ser 1.** Isto corrige a
recomendação anterior de igualá-lo ao 64. **São DOIS conceitos separados, e o design não os
equipara:**

- **tecto-de-segurança-da-pilha (constante à parte, hoje 64):** `codegen.tks:5412,7432,8178,8799`
  (`regions.len < 64`) + `teko_rt.c:1184` (`TK_ARENA_MARK_MAX 64`). É um LIMITE DE SEGURANÇA da pilha
  de regiões ativas (espelha a pilha fixa de marcas do gémeo C) — para de abrir regiões novas além de
  64 níveis de aninhamento simultâneo, um guarda anti-estouro. Recomendo NOMEÁ-LO
  `TK_REGION_STACK_CAP` (uma fonte), mas ele **não tem nada a ver com `#arena_depth`**.
- **`#arena_depth` (default = 1):** o NÍVEL DE ACHATAMENTO de sub-arenas, na definição do dono
  (`onde-a-limpeza…:112-117`): *"cada escopo ou chamada de função abre uma arena, o depth seria o
  nível de achatamento que ela comportaria … as sub-regiões, arenas dentro de arenas, que popam em
  LIFO, como o defer faz."*

**A semântica de depth=1 (a leitura que bate com a Correção 1):** depth=1 = "cada escopo abre a SUA
própria arena e NÃO achata filhos" = **granularidade MÁXIMA**. Cada um dos cinco escopos léxicos (§4)
materializa a sua sub-arena e morre na sua aresta de saída — exatamente a regra "toda variável morre
no fim do seu escopo". depth=1 É o default fino que a Correção 1 exige. `#arena_depth(N>1)` é o
OVERRIDE que ACHATA N níveis de escopos aninhados numa ÚNICA arena (os N-1 escopos interiores deixam
de materializar arena própria; bumpeiam na do ancestral e morrem todos juntos quando ela popa, LIFO)
— uma otimização opt-in para cortar overhead onde o perfil mostra muitas sub-arenas minúsculas e
curtas.

**Prova de que depth=1 preserva a disciplina região-por-iteração do `emit_loop_while` (pedido do
dono):** sob depth=1, o corpo do loop é um escopo que abre a sua arena e a larga na sua aresta de
saída — que para um loop é a saída de CADA iteração (`codegen.tks:8810`, *"THEN drop the loop body's
region"*). Logo a arena da iteração N é largada antes de a da iteração N+1 abrir: recuperação por
iteração, sem acumular. depth=1 NÃO achata o loop para o fim da função — o achatamento só existe com
N>1. **Carve-out obrigatório:** `#arena_depth(N>1)` NUNCA achata através de uma aresta de retrocesso
de loop — um corpo de loop é SEMPRE uma fronteira de materialização, senão a alocação por iteração
acumularia até ao fim do escopo achatado (o pico que o dono teme). O achatamento coalesce apenas
escopos em linha reta (blocos/if/when aninhados sem back-edge).

**O custo sob depth=1 — barato E fino (o número corrigido):** o que importa para a cauda é o número de
arenas VIVAS SIMULTANEAMENTE, não o total de escopos EXECUTADOS. Sob depth=1 com drop-por-escopo
(Correção 1), as arenas vivas num instante = a profundidade de aninhamento léxico ATIVA nesse ponto
(um punhado, ≤ `TK_REGION_STACK_CAP`), porque cada escopo larga a sua arena na saída ANTES de o
seguinte abrir. **Um loop de 1M iterações tem UMA arena de iteração viva a cada instante, não 1M.**
O número de 2,6 GB de cauda (`onde-a-limpeza…:144-159`) assumia 500k regiões vivas ao mesmo tempo —
o que só acontece se elas NÃO forem largadas (o bug de acumulação atual). Com drop-por-escopo a
cauda cai para `profundidade_de_aninhamento × 5,3 KB` ≈ centenas de KB, não GB. **Portanto depth=1 é
simultaneamente a granularidade fina (Correção 1) E barato** — o custo escala com o aninhamento, não
com a contagem de execuções. `#arena_depth(N>1)` existe para o caso oposto: quando o overhead de
abrir/largar uma arena por escopo (cada primeira alocação puxa um chunk de 64 KiB,
`TK_REGION_DEFAULT_CHUNK`, `teko_rt.h:140`) supera o ganho — aí achatar N níveis reduz mallocs de
chunk, ao custo de adiar a morte dos interiores. `#arena_size(N)` continua a ser o chão-inicial
(realloc), ortogonal ao depth.

**Recomendação de sequência:** default-depth=1 entra COM o modelo (é a granularidade que §4/§8
exigem — não é opcional nem #476). `TK_REGION_STACK_CAP` (o 64) nomeado à parte, sem semântica de
depth. A DIRETIVA declarável `#arena_depth(N>1)` (o achatamento opt-in, #476) é um follow-on de
otimização — NÃO bloqueia o modelo, porque o default 1 já é o comportamento correto e fino. A
pergunta ao dono (§13) deixa de ser "que default" (é 1) e passa a ser só "puxar a diretiva de
override #476 agora ou como fast-follow".

---

## 8. Sub-regiões POR OMISSÃO (o opt-in duplo que cai)

**O estado:** hoje uma região só abre se o predicado dispara (`fn_body_has_frame_local` /
`cg_block_has_block_local`) OU se há `#arena_size` (`codegen.tks:9656`). Opt-in duplo
(`onde-a-limpeza…:107`). Com root-como-refúgio banido e default-depth=1 (§7), o default INVERTE:
**cada um dos cinco escopos léxicos (§4) com pelo menos um local não-escapante abre a sua região**
(depth=1 = cada escopo a sua sub-arena, sem achatar). O predicado `binding_is_block_local` deixa de
ser um opt-in e passa a ser a norma; o que NÃO abre região é só o escopo sem local nenhum
(byte-idêntico, zero custo) ou além do `TK_REGION_STACK_CAP` (o guarda de segurança da pilha, NÃO o
depth). O `#arena_size(N)` passa a AFINAR só o chão-inicial; o `#arena_depth(N>1)` a ACHATAR
sub-arenas — ambos deixam de FORÇAR a abertura (que já é por omissão a depth=1).

---

## 9. `tk_str_concat_r` — o pré-requisito (str irrotável sem ele)

**O estado:** `tk_str_concat` aloca um buffer fresco que o resultado POSSUI (`teko_rt.c:140-143`,
*"a fresh buffer … the result OWNS it"*) — via malloc/root, NUNCA rotado para região nenhuma. Logo
qualquer escopo que PRODUZA um `str` (concat, interpolação) vaza-o: `str` não tem como participar da
morte por escopo. É o único item que nenhuma das outras peças contorna (`onde-a-limpeza…:103,59`).

**O design — a assinatura (teko_rt.{c,h}, C MANTIDA):**

```c
/* tk_str_concat_r — como tk_str_concat, mas o buffer fresco do resultado é bump-alocado em `r`
 * (tk_region_alloc(r, a.len + b.len)) em vez de malloc. O resultado vive em `r` e MORRE quando `r`
 * for largada. `r == tk_region_root()`/programa reproduz o comportamento leak-atual; `r` = uma
 * região de escopo faz o str concatenado morrer com o escopo. */
tk_str tk_str_concat_r(tk_region *r, tk_str a, tk_str b);
```

O codegen/lower da concat e da interpolação rota através de `tk_str_concat_r(<região-corrente>, …)`
quando a residência do `str` resultante é um escopo (o mesmo seletor N-níveis do §4); mantém
`tk_str_concat` (root/programa) quando a residência é programa. É o primeiro item da ordem porque sem
ele o seletor N-níveis não tem para onde rotear um `str`.

---

## 10. Crumbs ordenados (bootstrap-seguros, com colisões e rituais)

O seed é o `teko` lançado anterior; a primitiva de runtime vem antes do seu uso. Colisões marcadas nos
ficheiros quentes: `lower.tks`, `codegen.tks`, `escape.tks`/`spine.tks`, `typer.tks`.

**C0 — [FEITO] commit vazio + push.** Proteção contra restart.

**C1 — Runtime: `tk_str_concat_r` + confirmar `tk_region_enter`/`leave`.** `teko_rt.{c,h}` (C
MANTIDA). Adiciona `tk_str_concat_r` (§9). A primitiva `region_enter`/`leave` é a MESMA do
`backend-memoria` C1 — **NÃO reimplementar**; depender do merge dele ou, se este chegar primeiro,
adicioná-la lá com a assinatura acordada (`docs/design/backend-memoria-por-funcao-0.3.1.md:236-241`).
Builtins Teko em `scope.tks` (espelhar `:740-742`) + mapeamento em `lower.tks` (espelhar `:3981-3982`).
**Colisão:** `lower.tks`, `scope.tks` — aditivo. **Gate:** builda; `teko test .` verde; sem chamadores
⇒ FIXPOINT trivial. Ritual: NÃO.

**C2 — Checker: o oráculo de residência `ResidencePlan`.** Novo `src/checker/residence.tks` (ou
extensão de `spine.tks`): `pub fn residence_plan(f: TFunction): ResidencePlan` que, por binding,
devolve o tier (escopo léxico / frame / caller / root) associando cada binding ao seu escopo-dono
qualquer que seja o sabor (bloco/loop/if/when/fn, §4), usando `pt_join` (`spine.tks:495`) como oráculo
transitivo e `escape.tks` como fast-path; por `return`, se é move. Fixar o default de materialização
`arena_depth = 1` (cada escopo a sua sub-arena; §7) e nomear à parte o guarda de pilha
`TK_REGION_STACK_CAP = 64` (NÃO é depth). **Colisão:** `spine.tks` (agentes de spine) — preferir
ficheiro NOVO `residence.tks` que IMPORTA `spine`, minimizando a colisão. **Gate:** ninguém consome o
plano ainda ⇒ FIXPOINT trivial. Ritual: NÃO.

**C2b — `#singleton` para bindings (abrir o atributo, §2a).** Relaxar a rejeição
lifetime-antes-de-fn/binding: `parse_decl.tks:1263`, threading `di_kind` para o `TBinding` (`ast.tks`),
e no `residence_plan` um binding `#singleton` força `ResidenceTier::Root` (reusa `di_scope_expr →
tk_region_root()`, `codegen.tks:9564`). `#scoped`/`#transient` num binding continuam rejeitados. O DI
de classes NÃO muda. **Colisão:** `parse_decl.tks`, `ast.tks` (aditivos). **Gate:** um `#singleton`
binding aloca em root (fixture `mem_singleton_root`). Ritual: NÃO (atrás de anotação opt-in).

**C3 — Rota C: seletor N-níveis + move-on-return + sub-região por omissão (os 5 escopos).** `codegen.tks`
passa a ler o `ResidencePlan` uniformemente para os cinco escopos léxicos (§4 — bloco/loop/if/when/fn,
já com `_tkbr`/`_tkfr`): o `frame` de crescimento de slice/str usa a região do escopo dono (não a
moldura); `return` de um valor `PtFrame` aloca na região-corrente (caller); `str` concat/interp rota
por `tk_str_concat_r`; sub-região abre por omissão (depth=1) respeitando o `TK_REGION_STACK_CAP`.
**Colisão:** `codegen.tks`
(quente — coordenar com `arena-escopo-local`, que já mexe em `emit_struct_init_framed`/`emit_as_r_in`,
diff em `origin/cargo/0.3.1.0-arena-escopo-local`). **Gate — RITUAL COMPLETO:** gen2 native? NÃO — a
rota C valida-se por `teko test .` verde + FIXPOINT gen2==gen3 (a rota C não altera um byte para os
casos já cobertos; os novos casos têm fixtures novas). Ritual: SIM.

**C4 — Rota NATIVA: herdar o ciclo de vida via `lower.tks`.** `lower.tks` ganha o lowering ausente:
lê o MESMO `ResidencePlan`; na entrada de escopo `{}` com locais emite
`region_enter(tk_region_new(<corrente>))`, na saída `region_leave` + `tk_region_drop`; para o loop, o
enter/leave por iteração (a arena da iteração larga na saída de cada volta, §4/§7 — nunca achatada);
`buf_ptr` e o `tk_alloc` default passam a mirar a região-corrente em vez de `tk_region_root()` fixo
(`lower.tks:3390-3437`); `return` move para a corrente-do-caller. **Colisão:** `lower.tks` (muito
quente — coordenar com `backend-memoria`, `theory/all-legs-native-map`). **Gate — RITUAL COMPLETO:**
buildar gen2 `TEKO_BACKEND=native`; `teko test .` verde; **FIXPOINT gen2==gen3 byte-idêntico**;
`TEKO_ARENA_OBS`: "scoped > 0", regiões largadas ≈ nº de escopos executados, arenas VIVAS ≈
profundidade de aninhamento (não o total executado, §7). Ritual: SIM.

**C5 — Cross-thread → programa (quando `chan`/`wait_group` chegarem).** Rotear o payload de
`chan.send`/`wait_group` para `tk_region_program()`; ligar o gate `is_unique_at` (`spine.tks:1562`)
ao sítio de consumo (contrato L2c, `spine.tks:1553`). Hoje SEM superfície ⇒ este crumb é
scaffolding/honest-stop até a feature existir. **Gate:** o gate de uniqueness rejeita send de
`UsShared`. Ritual: SIM quando a superfície existir.

**C6 — [follow-on] `#arena_depth(N>1)` (#476) como override de ACHATAMENTO.** A diretiva declarável
que ACHATA N níveis de escopos em linha reta numa arena (o default É 1 — cada escopo a sua sub-arena,
entra em C2/C3, NÃO aqui). Carve-out: nunca achata através de back-edge de loop (§7). Não bloqueia
C1–C4. Ritual: SIM.

---

## 11. O portão e a medição

**Portão de correção:** gen2 compilado com `TEKO_BACKEND=native` (NÃO gen1/rota-C — gen1 é base C e
nunca exercita o backend nativo onde o OOM vive). Rotina: (1) buildar gen2 native; (2) `teko test .`
verde; (3) **FIXPOINT gen2==gen3 byte-idêntico** (escopar/mover NÃO pode alterar um byte emitido para
os casos existentes — os novos casos trazem as suas fixtures); (4) diff C-vs-own inalterado.

**Medição de memória:** `TEKO_ARENA_OBS` (o instrumento já existe, `teko_rt.c:1338-1423`: contadores
de regiões criadas/largadas, `tk_obs_scoped`/`tk_obs_drop_bytes`, `reclaimed`). Critério: **"scoped >
0"** e **"regiões largadas ≈ nº de escopos com locais"** (contra o `0.0%` / `11 de 5007` de hoje). E
o critério do §1: **contagem de `residence = unresolved` (fallback-programa) tende a zero** — só
`chan`/`wait_group` residem em programa por design.

**Fixtures — ASSERTAM STDOUT, nunca exit** (correção do dono; formato `.tkr` como
`region_actuator.tkr` do `arena-escopo-local`: `Then stdout pattern = "…"`). Todas sob gen2 native.

| fixture | o que prova | stdout |
|---|---|---|
| `mem_block_dies` | um local só usado dentro de um `{}` reside na região do bloco e morre na saída (churn N ciclos que nete a zero; corrupção = valor errado, não só leak) | valor conhecido |
| `mem_scope_kinds` | os CINCO escopos (bloco/loop/if/when/fn) tratados idênticos — um local em cada tipo morre na sua aresta de saída (Correção 1) | valor conhecido |
| `mem_loop_per_iter` | loop que aloca por iteração sob depth=1: as arenas VIVAS mantêm-se ≈ 1 (não N); `TEKO_ARENA_OBS` mostra pico plano ao longo de 1M iterações (prova a recuperação por iteração, não achatada) | linha de pico |
| `mem_move_return` | um valor construído no callee e retornado é MOVIDO para o caller (usado após a chamada; se ficasse na região do callee seria UAF/valor-corrompido) | valor conhecido |
| `mem_move_transitive` | valor retornado por N frames aninhados (move N vezes; o join é o frame de topo) | valor conhecido |
| `mem_str_scope` | um `str` concatenado num `{}` rota por `tk_str_concat_r` e morre com o escopo | string conhecida |
| `mem_no_root_leak` | corpo com só locais e retornos; `TEKO_ARENA_OBS` mostra scoped>0 e unresolved=0 (nada em root exceto o frame do main) | linha de contagem |
| `mem_singleton_root` | um binding `#singleton` reside em root (lido de novo após o escopo declarante fechar — prova que NÃO morreu com o `{}`) | valor conhecido |
| `mem_chan_program` (quando `chan` existir) | um valor enviado num `chan` reside em programa; enviar um `UsShared` é rejeitado na compilação | string / erro de compilação |
| `mem_fixpoint` | o próprio `src/` (self-build) gen2==gen3 byte-idêntico | — |

---

## 12. Relação com o fix pontual `backend-memoria-por-funcao` — COMPLEMENTAR, ordem definida

O fix pontual (`docs/design/backend-memoria-por-funcao-0.3.1.md`, ramo
`cargo/0.3.1.0-backend-memoria`) funde os 3 passes module-at-a-time do backend
(`select_module`→`regalloc_module`→`encode_module`) para largar o scratch do COMPILADOR (o pico
`LIR + 2×MModule` coexistentes, `backend-memoria…:32-37`).

**Este modelo geral SUBSUME o pontual? NÃO — são COMPLEMENTARES. A prova:**

- O modelo geral limpa por escopo DENTRO de cada função do programa gerado. O self-build nativo é um
  programa Teko, então SIM: as suas funções param de vazar os seus locais para a task-root — deixa de
  OOMar *pela via correta* nesse eixo.
- MAS o pico `2×MModule` do fix pontual é um problema ESTRUTURAL do pipeline do compilador, não
  per-escopo: TODOS os `MInst` de TODAS as funções ficam vivos ao mesmo tempo porque o laço é
  module-at-a-time (`backend-memoria…:11-42`), independentemente de residência por escopo. Mesmo com
  limpeza por escopo perfeita, o compilador ainda constrói todo o `MModule` antes de encodar. A
  limpeza por escopo reduz o working-set POR FUNÇÃO; a fusão reduz o pico CROSS-FUNÇÃO. Eixos
  ortogonais.

**Recomendação de ordem:**
1. **O fix pontual (`backend-memoria`) PRIMEIRO** — é o unblock estrutural, estreito e já desenhado
   para o OOM de 15,8 GB; mata o termo dominante `LIR + 2×MModule`. É também quem adiciona a primitiva
   `tk_region_enter`/`leave` (C1 lá) de que ESTE modelo depende.
2. **O modelo geral (este) SEGUNDO** — a mudança semântica mais profunda (move-on-return, escopo
   N-níveis, residência-programa cross-thread) que também ajuda o self-build e, sobretudo, torna
   a limpeza por escopo o MODELO DA LINGUAGEM (pergunta 3 do dono), não um truque de backend.
3. **Partilham a primitiva única** `tk_region_enter`/`leave` — COORDENAR (uma implementação de
   runtime, dois consumidores). O `phase_begin` do `backend-instr` é o sítio natural para ancorar o
   `enter`/`leave` do fix pontual (`backend-memoria…:287-291`); as regiões de escopo deste modelo
   ancoram no lowering de entrada/saída de `{}`.

---

## 13. Riscos, tensões de lei, e a pergunta ao dono

| risco / tensão | resolução (law-first) |
|---|---|
| **R1 — remover root-refúgio abre UAF** | NÃO: residência = LUB(usos) domina todos os usos por construção (§1, teorema). A segurança (M.1) é preservada; perde-se só a sobre-conservadoria. |
| **R2 — join genuinamente indeterminado (análise incompleta)** | M.1 exige saída segura: fallback = região de PROGRAMA (⊒ tudo, nunca UAF) MAS contado como `unresolved` e conduzido a zero; OU honest-stop do checker. NÃO é leak-root silencioso. |
| **R3 — move-on-return e a ABI congelada** | Opção (A) usa região-corrente thread-local, SEM parâmetro novo → ABI intocada. Sem tensão. |
| **R4 — colisão nos ficheiros quentes** | `codegen.tks`/`lower.tks`/`spine.tks`: minimizada — oráculo em ficheiro NOVO `residence.tks`; C3(C)/C4(nativo) separados; coordenar com `arena-escopo-local` e `backend-memoria`. |
| **R5 — primitiva de runtime toca C congelada** | `teko_rt.{c,h}` é exceção explícita ao congelamento. `enter`/`leave`/`concat_r` aditivos, comportamento-idêntico com pilha vazia/região-root. |
| **R6 — cross-thread ainda não tem superfície** | `chan`/`wait_group` não existem hoje (`spine.tks:1555`). Oportunidade: o modelo entra ANTES, então nascem com residência-programa correta. C5 é scaffolding até lá. |
| **R7 — FIXPOINT quebra (bytes mudam)** | Impossível por estrutura se os casos existentes mantêm residência; os novos trazem fixtures. O FIXPOINT byte-idêntico é o detector. Se quebrar, algo mudou de residência indevidamente — parar e reexaminar. |
| **R8 — `#singleton` para binding refatora o DI de classes** | NÃO: extensão ADITIVA e cirúrgica — só a superfície `#singleton` ganha o alvo binding, reusando `DiKind::Singleton` + `di_scope_expr:9564`. O DI de classes (`di.tks`) fica intocado; `#scoped`/`#transient` de binding rejeitados. Sem tensão. |
| **R9 — depth=1 abre nova tensão?** | NÃO. depth=1 = granularidade máxima = a regra "morre no escopo" (Correção 1) — são a MESMA coisa, não competem. O único risco seria custo (muitas arenas), refutado em §7: as arenas VIVAS = profundidade de aninhamento (pequena), não o total executado; o loop recupera por iteração. A ÚNICA subtileza a implementar corretamente é o carve-out do achatamento N>1 nunca cruzar back-edge de loop — se violado, um loop achatado acumula (o pico do dono). Fixture `mem_loop_per_iter` é o detector. |
| **R10 — confundir `#arena_depth` com o tecto-64** | O erro que a Correção 2 apanhou: são conceitos separados. `TK_REGION_STACK_CAP` (64) = guarda de segurança da pilha de regiões; `#arena_depth` (default 1) = nível de achatamento de sub-arenas. Nomeados à parte no design; nunca a mesma constante. |

**Tensão de lei residual: NENHUMA que force HALT.** Tudo resolve via Constituição/Leis (R11 arenas
lexicais; M.1 nunca-UAF preservado; exceção de runtime C mantida; Teko-only; issue-100%). O modelo é
implementável ADIANTANDO tudo o que não depende de `chan`/`wait_group` (que não existem) — a
residência-escopo e o move-on-return fecham as perguntas 1, 2 e 3 do dono sem a superfície
cross-thread.

**A ÚNICA pergunta ao dono (não-bloqueante).** O default está resolvido pela Correção 2:
`#arena_depth = 1` entra COM o modelo (é a granularidade fina que §4/§8 exigem — não é opcional). A
pergunta que resta é só de âmbito da DIRETIVA de OVERRIDE `#arena_depth(N>1)` (o achatamento opt-in,
#476): entra AGORA ou como fast-follow de otimização? O modelo NÃO depende dela (o default 1 já é o
comportamento correto). Recomendação: fast-follow — a otimização de achatamento só se justifica
depois de o perfil mostrar overhead de sub-arenas, e o carve-out de loop (§7) tem de vir com ela.

---

## 14. Assinaturas Teko que o implementador adiciona (full Javadoc — copiar verbatim)

Formas, não corpos. Estilo Javadoc completo (W15).

```teko
/**
 * ResidenceTier — o tier de residência de um valor, o reticulado que substitui o binário
 * frame-local-vs-root de M.1/M.5. Ordenado do mais apertado ao mais largo: um valor reside no
 * MENOR tier que domina todos os seus usos (o LUB de `spine::pt_join`). `Root` é o tier wide, com
 * EXATAMENTE duas origens legítimas: um binding `#singleton` (declarado, curto-circuita o oráculo →
 * `tk_region_root()`, `codegen.tks:9564`) OU cross-thread estrutural (`chan`/`wait_group` →
 * `tk_region_program()`). `Unresolved` é um DEFEITO MEDIDO (a análise não apertou abaixo do topo),
 * contado por TEKO_ARENA_OBS e conduzido a zero, nunca um leak-root silencioso aceite. NÃO há tier
 * `Scoped`/`Transient` de variável — o default É o escopo `{}` (efêmero) e o move cobre o subir.
 *
 * @since 0.3.1
 */
pub type ResidenceTier = variant Scope | Frame | Caller | Root | Unresolved

/**
 * ResidencePlan — o artefacto PURO que o checker produz por função e que AMBOS os motores (C em
 * `codegen.tks`, nativo em `lower.tks`) consomem IDENTICAMENTE. É a fonte-única-de-verdade que
 * impede C e nativo de divergirem no roteamento (o aviso de `escape.tks:405` — "they must never
 * disagree" — generalizado). Cada binding recebe o seu tier e, quando `Block`, o índice do bloco
 * `{}` dono; cada `return` recebe se é um move e para que tier.
 *
 * @since 0.3.1
 */
pub type ResidencePlan = struct {
    /** o tier de residência de cada binding local, por índice de binding na função. */
    bindings: []BindingResidence
    /** por sítio de `return`, se o valor é movido e o tier de destino. */
    returns: []ReturnResidence
}

/**
 * residence_plan — o ORÁCULO DE RESIDÊNCIA. Computa, para uma função, onde cada valor reside,
 * usando `spine::pt_join` (o LUB transitivo, `spine.tks:495`) como fonte precisa e `escape.tks`
 * como fast-path conservador. É a peça que SOBE a decisão de memória para o checker partilhado, de
 * modo que o backend nativo herde a mesma limpeza que a rota C tem — cada motor apenas LOWERA este
 * plano à sua maneira (texto-C / instruções LIR).
 *
 * @param f  a função tipada a analisar
 * @return   o plano de residência (nunca erro — um join indeterminado vira tier `Unresolved`, um
 *           defeito medido, ou é honest-stopped a montante conforme a política ratificada)
 * @since 0.3.1
 */
pub fn residence_plan(f: checker::TFunction): ResidencePlan

/**
 * region_enter — empilha `child` como região-corrente da task: toda alocação default subsequente
 * (`list::push`, str-concat, auto-box) passa a bump-alocar em `child` até ao `region_leave`
 * correspondente. A MESMA primitiva que o fix pontual `backend-memoria` adiciona (COORDENAR, não
 * duplicar): aqui serve as regiões de escopo `{}` do programa GERADO; lá serve o scratch do
 * COMPILADOR. Reusa `tk_region_new`/`tk_region_drop` (`teko_rt.h:149-152`).
 *
 * @param child  a região-filha (de `tk_region_new(<corrente>)`) que recebe o escopo
 * @return       void
 * @since 0.3.1
 */
pub fn region_enter(child: RegionHandle)

/**
 * region_leave — desempilha a região-corrente: a alocação default volta ao destino anterior (a
 * região do escopo envolvente, ou a corrente-do-caller no retorno). Chamado na aresta de saída de
 * um `{}` ANTES do `tk_region_drop` da filha; e no `return`, a corrente durante a chamada É a
 * região do caller, o que realiza o move-on-return sem parâmetro novo (ABI intocada).
 *
 * @return  void
 * @since 0.3.1
 */
pub fn region_leave()
```

E a assinatura de runtime (C MANTIDA), §9: `tk_str tk_str_concat_r(tk_region *r, tk_str a, tk_str b)`.

Funções existentes que o design TOCA: `escape.tks` `binding_is_block_local`/`fn_escaping_vars`
(passam a alimentar o plano); `spine.tks` `pt_join`/`is_unique_at` (o oráculo transitivo + gate
cross-thread); `codegen.tks` `cg_enclosing_region_expr`/`emit_list_push`/`emit_struct_init_framed`/
`want_block`/`di_scope_expr:9564` (lêem o plano, seletor N-níveis, mapeamento `#singleton`→root);
`lower.tks` `lower_buf_ptr_call` e o lowering de entrada/saída de bloco (ganha o ciclo de vida
ausente); `parse_decl.tks:1263` + `ast.tks:384,560` (abrir `#singleton` para bindings, threading
`di_kind`); `typer.tks:5404` (o honest-stop da espinha transitiva relaxa quando o move aterra).
