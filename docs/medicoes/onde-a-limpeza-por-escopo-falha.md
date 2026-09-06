# A limpeza automática por escopo — o meu contra-exemplo caiu, e o que ficou no lugar

Pedido do dono, 2026-07-31: *"E eu sigo esperando um exemplo real de onde o que eu propus falha."*

Justo. As medições anteriores mostravam o `adopt` a não libertar — isso é evidência de que **a
implementação de hoje não faz nada**, não de que **a proposta esteja errada**. São perguntas
diferentes e eu tinha respondido à errada.

A regra proposta: *"sempre que um bloco finaliza, executa-se os defers ali pendentes e depois limpa a
memória para os itens órfãos naquela região."*

## Primeiro: a ordem que ele descreveu JÁ está implementada, literalmente

`src/codegen/codegen.tks`, `emit_loop_while` — o comentário do próprio código:

> *"…fire the defers first (**so a defer body can still read this iteration's block-locals**), THEN
> drop the loop body's region."*

E a região do corpo do laço é *"a fresh region every iteration … and **dropped at every exit edge**"*.
**Defers primeiro, limpeza depois, por aresta de saída.** A regra dele não é uma proposta nova: é o
que o emissor já faz — para as ligações que passam o predicado.

## O contra-exemplo que eu trouxe — E QUE NÃO SE SUSTENTA

Eu apontei o `emit_block` (`codegen.tks:8120`): `mut out = buf` fora do laço, e dentro
`out = emit_stmt(out, …)` a devolver um `[]byte` novo a cada iteração. Argumentei que o drop da
região do corpo do laço libertaria o acumulador.

**O dono refutou: *"out pertence à arena da função e não do laço"*. Ele tem razão, e a linha que o
prova é `codegen.tks:3726`:**

> *"the routing selector rides **`frame`**: `""` → root `tk_slice_push`; `"@fo"` → the free-old-on-grow
> `tk_slice_push_fo`; anything else → `tk_slice_push_r` **growing into that frame region variable**"*

E a emissão confirma: `if frame.len > 0 && !is_fo { cb(out, ", "); cb(out, frame) }`. **O que se passa
ao alocador é a região de MOLDURA — nunca a do bloco.** O crescimento do `out` vai para a arena da
função. O drop da região do laço não lhe toca.

E o caso simétrico que eu levantei — um nome que nasce dentro do laço e cujo armazenamento sai por
`push` para uma lista de fora — **falha pela mesma razão**: o alvo é a lista exterior, e o
`tk_slice_push_r` roteia para a moldura dela.

## O que a refutação REVELA, e é melhor do que o que eu tinha escrito

**O roteamento já é POR DESTINO, não por posição léxica.** Eu vinha a dizer que a pergunta certa era
*"onde o armazenamento foi alojado e para onde flui, não onde o nome foi ligado"* — e o emissor **já
responde assim**. Ele não olha para onde a instrução executa; olha para de quem é o destino.

O que existe hoje é esse roteamento com **dois níveis**: moldura da função, ou raiz. O modelo do dono
é o **mesmo seletor com N níveis** — a região do bloco dono do destino. Não é mecanismo novo nem
análise nova.

Isso reposiciona três coisas que eu tinha colado como se fossem uma:

| o que eu disse | o que a refutação corrige |
|---|---|
| a espinha transitiva é pré-requisito | **para o acumulador, não é** — o destino é sintaticamente conhecido no sítio da atribuição. Continua necessária para destino que é campo guardado ou retorno (`typer.tks:5404`) |
| o predicado `cg_same_named_struct` bloqueia | ele trava a **abertura** da região, não o **roteamento**. São duas portas e eu colei-as |
| `tk_str_concat_r` não existe | **este mantém-se, e sobe para primeiro** — sem ele, `str` não tem como ser roteada para região nenhuma |



---

## O modelo completo do dono, e o estado medido de cada peça

> *"Era para existir sub-regions (por padrão) e aí entrariam as tags `#arena_size` e `#arena_depth`,
> que adotariam com tamanho e profundidade."*

| peça | estado medido |
|---|---|
| **árvore de regiões com profundidade** | **EXISTE** — `tk_region_new(parent)` cria filha de `parent`, e `tk_region_drop_subtree` varre a cadeia `->parent` (`teko_rt.h:149`, `:152`) |
| **`#arena_size(N)`** | **EXISTE, parseado E consumido** — `ParsedAttributes.has_arena_size`/`arena_size` (`parser/result.tks:34`), rejeitado a 0 no parse |
| **`#arena_depth`** | **NÃO EXISTE** — zero ocorrências na árvore. É o **#476**, fora do âmbito do SW12, com pergunta ao dono em aberto no plano |
| **sub-regiões POR OMISSÃO** | **NÃO** — a região só abre se o predicado disparar **ou** se houver `#arena_size` |

### E o `#arena_size` faz mais do que dimensionar

`cg_frame_region_parent_expr` (`codegen.tks:9569`):

```teko
if f.has_arena_size { "NULL" } else { "tk_region_root()" }
```

Uma função com `#arena_size` ganha uma **raiz de árvore INDEPENDENTE**, não filha da raiz do
processo. O comentário chama-lhe contrato e liga-o ao modelo de isolados: *"a function that declares
its own arena is its OWN isolation boundary"*.

E — o ponto que mais interessa a este debate — `:9625`:

```teko
let want_frame = fn_body_has_frame_local(escaping, fbody) || f.has_arena_size
```

**`#arena_size` já FORÇA a abertura da região, contornando o predicado `Named`.** A porta que eu
descrevi como fechada tem uma chave, e ela está construída.

E a fonte diz também o que ninguém a usa: *"additive by ruling — **`src/` does not adopt
`#arena_size`**"*. Mesma história do `adopt`: construído, consumido pelo emissor, zero utilizadores.

### O que falta, então, na ordem que a medição sugere

1. **`tk_str_concat_r`** — sem ele, `str` é irrotável para qualquer região. É o único item que
   nenhuma das outras peças contorna.
2. **O seletor `frame` passar de dois níveis a N** — a região do bloco dono do destino, em vez da
   moldura da função. O mecanismo é o mesmo; muda o que se passa como argumento.
3. **Sub-regiões por omissão** — hoje é opt-in duplo (predicado **ou** `#arena_size`).
4. **`#arena_depth` (#476)** — não existe, e o plano tem a pergunta em aberto ao dono desde o SW12.

---

## O `#arena_depth` — a definição do dono, e o número que a justifica

> *"cada escopo ou chamada de função abre uma arena, o depth seria o nível de achatamento que ela
> comportaria, enquanto size define o chão inicial da arena para reduzir o realloc. As sub regiões,
> arenas dentro de arenas, que popam em LIFO, como o defer faz."*

### Primeiro: a pergunta que o plano tem em aberto NÃO é semântica

O dono disse *"nem sei qual é a pergunta"*. É de **âmbito**, não de significado, e está em
`docs/design/wave-0.3.1-plan.md`:

> *"**REPORTED gap (not a new issue):** #479/#480 reference `#arena_depth` (**#476**), which is NOT in
> this task's explicit scope list (it sits in the master-plan 0.3.2 bucket). Either (a) pull #476 into
> SW12 as crumb 12.0 … or (b) ship #479/#480 size-only and honest-stop the depth suggestion until
> #476 lands. **Recommend (a)** — the profiler is incoherent suggesting a directive that does not
> exist."*

**Pergunta: o `#476` entra no SW12 agora, ou o profiler sai sem a sugestão de profundidade?** Nada
mais. E a definição que o dono acaba de dar torna (a) mais forte, porque o `depth` deixa de ser um
número solto e passa a ser o controlo de um custo medido — o de baixo.

### O número: abrir uma região custa 64 KiB, não 64 bytes

`teko_rt.h:140`:

```c
#define TK_REGION_DEFAULT_CHUNK (64u * 1024u)   // default chunk payload (bytes)
```

A `struct tk_region` são sete campos (~56–64 bytes). **Mas a primeira alocação dentro dela puxa um
chunk de 64 KiB.** Uma região vazia é barata; uma região *usada* custa 64 KiB de chão.

**E é isto que o `#arena_depth` controla.** Medido no build real do compilador:

```
CHUNKS: 4996 regions, 21134 chunks, malloc'd cap 1485.8 MB, used 1459.3 MB, tail-waste 26.6 MB
```

* 21134 chunks / 4996 regiões ≈ **4,2 chunks por região**
* 1485,8 MB / 21134 chunks ≈ **70,3 KB por chunk** — coerente com os 64 KiB de omissão mais os
  sobredimensionados
* **desperdício de cauda: 26,6 MB** para 4996 regiões ≈ **5,3 KB perdidos por região**

**A aritmética que justifica o achatamento:** hoje são ~5000 regiões. Uma região *por escopo* num
compilador com 5941 itens e laços aninhados não são 5000 — são centenas de milhares. **A 5,3 KB de
cauda desperdiçada por região, 500 000 regiões dariam ~2,6 GB só de cauda** — antes de contar um único
byte útil. O `depth` é o que impede a sub-região por omissão de trocar um problema de memória por
outro maior.

### E o achatamento já existe, sem nome e fixado a 64

Duas ocorrências independentes, ambas com o mesmo tecto:

| sítio | o que limita |
|---|---|
| `codegen.tks:5412, 7425, 8156, 8768` | `want_block = cg_block_has_block_local(…) && **regions.len < 64**` — a partir de 64 níveis a região **não abre**, e o comentário chama-lhe *"a safe leak"* |
| `teko_rt.c:1184` | `#define TK_ARENA_MARK_MAX 64` — a profundidade da pilha de checkpoints por tarefa |

**O achatamento por profundidade já está implementado como constante mágica, em dois sítios, com o
mesmo valor 64 e sem ninguém lhe chamar `depth`.** O `#arena_depth` não introduz um conceito novo:
**dá nome, e controlo por declaração, a um tecto que já existe fixado.**

### O LIFO que ele descreve também já lá está

`teko_rt.c:1724`: *"The root region is a **LIFO chunk-list** (`tk_region_alloc` PREPENDS new chunks),
so a checkpoint = …"*. E o `tk_arena_push`/`tk_arena_pop` é exatamente a disciplina LIFO de marcas que
ele compara ao `defer` — e o `defer` corre LIFO por ruling verificado (`ast.tks:339`).

**As três peças do modelo dele — sub-regiões aninhadas, pop em LIFO, e um tecto de profundidade —
existem no runtime. O que não existe é a superfície que as declara e a rota que as usa por omissão.**
