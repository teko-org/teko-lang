# Onde está a memória do compilador — medido, não suposto

Diagnóstico do dono, 2026-07-31:

> *"o problema com o compilador atualmente são dois (na ordem de uma mesma família já corrigida
> hoje): 1. Arquivos muito grandes… 2. Por serem muito grandes, ao ler um arquivo ele entra inteiro
> na memória (OOM). 3. Deveríamos ter leitura por buffer e não por arquivo completo, descartando
> trechos já lidos e interpretados."*

**A família está certa. O membro não é a leitura de ficheiro — e a aritmética diz isso por 300×.**

## 1. O mecanismo que ele descreve EXISTE

`src/build/assemble.tks:228` lê o ficheiro inteiro para um `str` e entrega-o ao lexer:

```teko
let src = match teko::io::read_file(sf.path) { str as s => s; error as e => return e }
match asm_lex_and_parse(sf.path, src) { … }
```

Confirmado. A questão não é se acontece — é **quanto pesa**.

## 2. Os números, e eles fecham a questão

| grandeza | valor |
|---|---|
| corpus fonte inteiro (142 `.tks`) | **5,5 MB** |
| maior ficheiro (`src/lir/lower.tks`) | **776,9 KB** |
| segundo (`src/codegen/codegen.tks`) | 674,0 KB |
| pico do build (rota C, `--release`) | **1712,2 MB** |
| **o texto fonte, como fração do pico** | **0,29 %** |

Mesmo que **todos** os 142 ficheiros ficassem residentes ao mesmo tempo e nunca fossem libertados,
seriam 5,5 MB de 1712 MB. **Ler por buffer e descartar o já lido pouparia, no melhor caso, 0,3 %.**
Não chega perto da OOM.

## 3. O que a observabilidade de arena diz — e ela já existia

`TEKO_ARENA_OBS=<path>` está no runtime desde antes desta lane e **nunca tinha sido corrida sobre um
build do compilador**. Corrida agora:

```
root (process-lifetime, never freed):     1926.3 MB
scoped (freed at region drop):               0.0 MB
reclaimed by region drops:                   0.0 MB   (11 of 5007 regions dropped)
reclaimed by test-gate rewinds:              0.0 MB
reclaim ratio: 0.0%  (reclaimed / allocated)

=== ROOT-lifetime bytes by call site: 1926.27 MB total ===
   0     1700.5 MB      3484564 allocs
   1       23.3 MB       160686 allocs
   2       20.7 MB        61927 allocs
   …
=== SCOPED-lifetime bytes by call site: 0.00 MB total ===
=== MALLOC str total: 66.4 MB across 2165811 buffers ===
=== CHUNKS: 4996 regions, 21134 chunks, malloc'd cap 1485.8 MB, used 1459.3 MB, tail-waste 26.6 MB ===
```

**Três factos, e cada um sozinho fecha a hipótese da leitura de ficheiro:**

1. **A taxa de recuperação é 0,0 %.** De 1926 MB alocados, praticamente nada volta. **11 de 5007
   regiões** foram largadas.
2. **Um único sítio de chamada tem 1700,5 MB — 88 % do total** — em **3 484 564 alocações** de ~511
   bytes em média. O texto fonte inteiro é 0,3 % do que esse *um* sítio aloca.
3. **A arena com nome próprio ("scoped") tem 0,0 MB.** Tudo vai para a raiz, que só liberta na saída
   do processo.

## 4. A família está certa, e é exatamente a de hoje

O defeito do `verdict_emit` corrigido hoje era: **alocar em ciclo, numa arena que só liberta à saída
do processo**. O pipeline de compilação é **o mesmo padrão, à escala do compilador inteiro** — cada
intermédio (tokens, AST, AST tipada, LIR, texto C) é alocado e **nenhum é libertado** até o processo
sair.

**E o instrumento para o resolver já está construído e não é chamado por ninguém.**
`tk_arena_push`/`tk_arena_pop`/`tk_arena_commit` existem no runtime e estão expostos como builtins
(`src/checker/scope.tks:745`, `src/lir/lower.tks:3885-3887`). Os únicos chamadores são **o portão de
teste** (`#109`, `#469` — `codegen.tks:12124/12129`). **O pipeline de compilação nunca os invoca.**

O `tk_arena_commit` — o par que faz um âmbito que ESCAPA dobrar as suas alocações no nível de baixo
em vez de as perder — está marcado no cabeçalho como *"enabling primitive — staged off; no compiler
source calls this yet"*. É a peça exata que um marco por ficheiro precisaria: **os itens analisados
comprometem-se, o fluxo de tokens descarta-se.**

## 5. O ponto do dono que se mantém, e por mérito próprio

**Ficheiros de 776,9 KB e 674,0 KB são violação de boa prática, e isso é verdade independentemente da
memória.** O W15 já manda dividir ficheiros grandes em módulos coesos com a mesma superfície pública.
O que muda com esta medição é só o **motivo**: divide-se por legibilidade e manutenção, não para
resolver a OOM — porque não resolve.

## 6. O que ficou POR medir, e é o próximo passo

**O sítio de chamada 0 não está identificado.** O `dladdr` devolveu `?` para todos os 90 sítios,
porque o binário do compilador não exporta os símbolos estáticos. Sem isso sabe-se *quanto* (1700,5
MB, 3,48 M alocações, ~511 B cada) e não se sabe *quem*.

Fechar isso precisa de uma de duas coisas — e nenhuma foi feita:
* religar o compilador com `-rdynamic` (ou não-estático) só para a corrida de medição; ou
* fazer o despejo imprimir o **endereço de retorno cru** em vez de só o índice, e resolvê-lo com
  `addr2line` contra o binário.

**Até lá, qualquer atribuição de causa a uma fase concreta é palpite.** O que está provado é a
estrutura — 0,0 % de recuperação, 88 % num sítio — não o nome do sítio.

---

## 7. A limpeza por escopo — R11, e EU ERREI ao chamar-lhe `adopt`

Adendo do dono, 2026-07-31, e a correção dele à minha primeira resposta:

> *"não é adopt, é bem mais antigo, era um pré requisito que eu havia descrito em detalhes sobre a
> arena e pq ela em modo safe é automática. E um dos requisitos é que, sempre que um bloco finaliza,
> executa-se os defers ali pendentes e depois limpa a memória para os itens órfãos naquela região."*

**Ele tem razão, e o documento existe.** Eu apontei o `adopt` porque foi o que encontrei primeiro no
codegen; o `adopt` é a camada **L4**, a do *bulk-drop de ciclos*, opt-in. O que ele descreve é a
camada **L0**, a base — e ela é anterior, é automática, e tem regra numerada.

### A fonte: R11, no modelo de referências

`docs/design/ref-transparent-model.md` (PR #499, commit `959ad7c9`) consolida as regras R1–R11:

> **R11. Sem GC; arenas lexicais.** *Uma `ref` é válida só enquanto viva a arena que segura o seu
> alvo.*

E é citada como o modelo em vigor noutro sítio, com o nome que o dono usa:
`const-module-level-plan.md:78` — *"model is arena-per-scope (ref-transparent model, R11: 'sem GC;
arenas lexicais')"*.

`memory-unsafe-backend-remodel.md` põe-na como camada zero da tabela do modelo híbrido:

| camada | papel | estado |
|---|---|---|
| **L0 — bump-arena region tree** | *"the invisible default for ~95% of code; **per-scope**, per-request region"* | **BUILT** — custo *"none (leak-to-root is sound)"* |
| L4 — `adopt` | bulk-reclaim de um nó cíclico, opt-in | SCAFFOLDING |

**Eu li a L4 e respondi a L0.** São camadas diferentes com propósitos diferentes.

### E a ordem que ele nomeia está escrita, e é essa

*"executa-se os defers ali pendentes e depois limpa a memória"* — `backend-a1-lir-lowering.md:210`:

> *"**defer** lowers by **replaying the deferred body (LIFO) at every scope exit** — before each
> `return`, `break`, `continue`, and at normal fall-off"*

Os `defer` correm **à saída do bloco, antes de qualquer aresta de saída**. A limpeza da região vem
**depois** — que é exatamente a ordem que ele descreve, e a única que pode funcionar: um `defer` que
toque em algo alocado no bloco tem de correr **enquanto** esse algo ainda existe.

### Então porque é que não acontece? A resposta está medida, e tem uma linha de estado

`docs/design/concorrencia-adiantada-s8.md`:

| estágio | estado |
|---|---|
| **S1** arena primitive + root region | ✅ |
| **S2** scope regions + escape check | 🔶 **per-fn ✅ · block-arm ⬜** |
| **S3** `ref` (mutable-target only) | ✅ |

**`block-arm ⬜`.** A granularidade que o exemplo do dono precisa — a região do braço de um `if` — é a
metade que não foi construída. E a L0 diz-se **BUILT** com o custo *"none (**leak-to-root is
sound**)"*: **é essa cláusula que torna a implementação de hoje "correta" enquanto não recupera
nada.** Vazar para a raiz não produz um dangling, logo é sólido — e é por isso que ninguém tropeça
nele. Custa memória, não correção.

E é exatamente o que o número diz:

```
11 of 5007 regions dropped
scoped (freed at region drop):  0.0 MB
root (never freed):          1926.3 MB
reclaim ratio:                  0.0 %
```

**A semântica está especificada (R11), a ordem está especificada (defers, depois limpeza), a camada
diz-se construída — e a metade por bloco está por fazer, com estado registado desde antes desta
lane.** O compilador não está a desobedecer ao desenho: está a usar a válvula de escape que o desenho
lhe deu.

### O que isto muda no que eu tinha escrito

O instrumento continua a ser o mesmo — `tk_arena_push`/`pop` para o bloco que não escapa,
`tk_arena_commit` para o que escapa, e nenhum tem chamador no pipeline. O que muda é **de quem é a
dívida**: não é uma funcionalidade opt-in que ninguém usou, é a **metade por bloco do S2**, com
estado `⬜` registado, e o bloqueio nomeado no `typer.tks:5404` (a espinha de escape transitivo).


---

## 8. Porque a região por bloco quase nunca abre — a porta é `Named`, e só

Ampliação do dono, 2026-07-31:

> *"Não apenas `if`s, mas os braços de match, loop, bloco escopado, etc. … em teko, [o bloco escopado]
> permite controlar a memória sem precisar de `unsafe`."*

Fui medir quantos construtos abrem região hoje, e **é mais do que eu tinha dito na secção 7.** Cinco
sítios do `codegen.tks` empurram um `RegionFrame`:

| sítio | construto | condição |
|---|---|---|
| `:7427` | **braço de valor** (`if`/`match` em posição de valor) | `cg_block_has_block_local(…)` **e** `regions.len < 64` |
| `:8166` | `emit_block_region` | idem |
| `:8773` | **corpo de `loop`** (`is_loop = true`) | idem |
| `:9107` | `emit_adopt` | **incondicional** — é o seu propósito |
| `:5442` | função com local de moldura | `fn_body_has_frame_local` |

**Ou seja: os braços de `match`, os braços de valor do `if` e os corpos de `loop` JÁ estão ligados.**
A máquina está lá. O que decide se ela abre é um predicado — e é aí que está a resposta.

### A porta, e ela admite uma forma de tipo só

`cg_block_has_block_local` (`:8100`) percorre as ligações do bloco e devolve `true` na primeira que
satisfaça **duas** condições:

```teko
if cg_same_named_struct(b.bound, b.value.type) && checker::binding_is_block_local(escaping, fn_body, block, is_value, b) { return true }
```

E `cg_same_named_struct` (`:9340`) é:

```teko
match a { checker::Named as na => match b { checker::Named as nb => str_eq(na.name, nb.name); _ => false }; _ => false }
```

**Só passa uma ligação cujos dois lados sejam `Named` com o MESMO nome.** Um `str`, um `[]T`, uma
lista, um inteiro — **nada disso é `Named`**, logo nada disso abre região, e tudo cai na raiz.

### E é exatamente isso que o número de 0,0 % significa

O compilador aloca, de longe, `str` e `[]T`: **66,4 MB de buffers de `str` em 2 165 811 alocações**, e
o sítio de topo com **3 484 564 alocações**. **Nenhuma dessas formas passa a porta.** As 11 regiões
largadas de 5007 são as poucas ligações de struct nomeada que qualificaram.

**A correcção à secção 7 é esta:** não é *"a granularidade por bloco não foi construída"* nem *"é
opt-in e ninguém usou"*. É **construída, ligada aos construtos certos, e fechada por um predicado que
admite uma forma de tipo só**. O comentário do próprio código diz o alcance por escrito
(`:9351`): *"MEM Step 1 routes only top-level bindings; sub-scope bindings stay on root in this first
cut"* — **primeiro corte**, escrito como tal.

### O bloco nu `{ }` NÃO existe hoje, e é a parte nova da proposta

```teko
pub type Statement = variant Binding | Assign | Return | LoopStmt | BreakStmt | ContinueStmt | ExprStmt | DeferStmt | AdoptStmt
```

`src/parser/ast.tks:358`. **Não há forma de statement para um bloco nu.** O único bloco com escopo
próprio que é um statement é o `AdoptStmt`. Logo a proposta do dono tem duas metades com custos muito
diferentes:

* **alargar o predicado** — os construtos já lá estão; muda-se quem passa a porta;
* **acrescentar o bloco nu** — é gramática nova (lexer não muda, parser e AST mudam).

### E o argumento do dono sobre o que o bloco nu VALE aqui

> *"em outras linguagens, apenas limita ou permite a reescrita de variáveis dentro de um constructo com
> o mesmo nome de outra fora, mas em teko, permite controlar a memória sem precisar de `unsafe`."*

**Está certo, e a tabela do modelo híbrido dá-lhe a razão pelo contrário:** hoje quem quer controlo
explícito de libertação desce a `#must_free`/`mem::free` (L2c) ou a `unsafe` (L5). Um bloco nu com
região própria dá **a mesma capacidade na camada L0** — sem anotação, sem `unsafe`, e com a garantia
que a R11 já enuncia. É a diferença entre *"o dev pede para libertar"* e *"o dev diz onde acaba o
tempo de vida"*.
