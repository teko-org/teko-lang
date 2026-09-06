# O profiler como AFINADOR do modelo de arenas — proposta (SW12: #476 · #475 · #479 · #480)

> **Ruling do dono, 2026-07-31:** *"a pergunta é uma nota para ser respondida não por mim, mas pelo
> profiler… e o trabalho dele seria fazer tunning usando `arena_size` e `arena_depth`, para melhorar
> o controle da memória em regiões safe."*

Isto fecha a pergunta que o plano deixou em aberto (`wave-0.3.1-plan.md:464`), e fecha-a por
construção em vez de por escolha: **se o profiler é o afinador, a diretiva que ele afina tem de
existir.** O `#476` entra no SW12 como **crumb 12.0**. Não é a opção (a) do plano a ganhar a votação;
é a opção (b) a deixar de ser coerente — um afinador sem um dos dois botões não afina, mede.

Este documento é uma **proposta**. Os alarmes que traz são só os que consigo **produzir**, e cada um
vem com a medição que o produz. Onde não medi, digo que não medi.

**Portão:** nada disto se implementa até o teko ser 100% nativo (`tdb`). Isto é desenho; a ordem de
entrada é do integrador.

---

## 0. Onde a árvore contradiz o que me foi dado — três correções, todas para melhor

Foi-me pedido que dissesse. Digo, e as três **fortalecem** a proposta em vez de a enfraquecerem.

### 0.1 O `sítio de chamada 0` não é um índice de tabela — é um POSTO no ranking

`tk_obs_dump_table` (`src/runtime/teko_rt.c:1380-1398`) faz um top-30 não-destrutivo e imprime `k` —
o **posto**, `0..29` — não o índice do sítio:

```c
for (int k = 0; k < 30; k += 1) { … fprintf(fp, "  %2d  %9.1f MB  %11llu allocs  %s\n", k, …); }
```

Logo *"sítio de chamada 0"* lê-se **"o maior consumidor"**, não *"a entrada 0 da tabela"*. A conclusão
do briefing não muda (é o maior, com 88 %); muda o que se procura quando se for atrás dele.

### 0.2 O endereço de retorno **já é capturado**. O que falta é imprimi-lo

O briefing pergunta *"que superfície mínima exige? (endereço de retorno cru + `addr2line`? uma tabela
de sítios emitida em compilação? `-rdynamic`?)"* — e a resposta começa mais perto do que parecia:

```c
typedef struct { void *ra; unsigned long long bytes, count; } tk_obs_site;   // teko_rt.c:1332
```

O `ra` **está lá**, gravado por `__builtin_return_address(0)`. O `?` não vem de não se ter o
endereço: vem de `dladdr` devolver `dli_sname == NULL` para símbolos estáticos, e do despejo imprimir
só esse nome. **O endereço nunca é impresso.** Isto muda o custo do §5 de "instrumentação nova" para
"um `fprintf`".

### 0.3 Os ~2,6 GB de cauda são um limite de **PICO SIMULTÂNEO**, não uma expectativa

A aritmética do briefing — *"a 5,3 KB por região, 500 000 regiões dariam ~2,6 GB só de cauda"* — está
**certa para o mundo de hoje**, onde 11 de 5007 regiões são largadas: uma região que nunca larga paga
a cauda para sempre. Mas o mundo que a proposta constrói é aquele em que a região **larga na aresta de
saída**, e aí a cauda só se paga **enquanto a região está viva**. O número de regiões vivas ao mesmo
tempo é limitado pela **profundidade**, não pela contagem total.

**Isto não enfraquece o `depth`: muda a moeda em que ele é pago, e a moeda nova também está medida.**

| regime | o que o `depth` limita |
|---|---|
| hoje (nada larga) | **bytes residentes** — cauda × regiões criadas, o cálculo do briefing |
| com sub-regiões a largar | **bytes residentes** ≤ `depth × 64 KiB × cadeias ativas` … |
| … e o resto migra para | **agitação do alocador** — um `posix_memalign` de 64 KiB por região USADA, e um `free` por drop |

O `depth` deixa de ser *"o que impede um estouro de 2,6 GB"* e passa a ser **a taxa de câmbio entre
bytes residentes e agitação do alocador**. Os dois lados são mensuráveis, e **o profiler tem de
reportar os dois**, senão sugere um número otimizando metade de uma balança.

---

## 1. O que o profiler MEDE — e o que cada modo NÃO pode responder

O `#479` pede estático **e** dinâmico. São instrumentos diferentes com poderes diferentes, e a
proposta nomeia a fronteira em vez de a diluir.

### 1.1 O modo ESTÁTICO — uma caminhada sobre o `TProgram`

O precedente existe e é literal: `src/coverage/coverage.tks` já é uma caminhada estática sobre o
`TProgram` que enumera sítios de ramo, linhas e funções (`cov_walk_expr`, `cov_walk_stmt`,
`count_prod_fns`). O modo estático do profiler é **a mesma caminhada, com outro somatório**.

| pergunta | o estático responde? |
|---|---|
| que declarações abrem região, e a que profundidade léxica | **sim** — é sintaxe |
| qual a profundidade léxica MÁXIMA de cada função | **sim** |
| quantas ligações de cada bloco são roteáveis para região | **sim** — o predicado é `cg_same_named_struct` + `binding_is_block_local` |
| que sítios alocam `str` (hoje irroteável, §6) | **sim** |
| **quantos bytes cada região vai realmente conter** | **NÃO** — depende da entrada |
| **quantas vezes cada função é chamada** | **NÃO** |
| **que profundidade é atingida em execução** | **NÃO** — recursão e chamadas indiretas não são léxicas |

**O estático dá o DENOMINADOR, nunca o numerador.** Ele responde *"quantos sítios existem e quais
poderiam ser afinados"*; nunca *"qual deles vale a pena"*. Um profiler que sugerisse `#arena_size` só
com o estático estaria a inventar o número — exatamente o que a ruling de 2026-07-13 proíbe.

### 1.2 O modo DINÂMICO — por região, por sítio de abertura

Sete grandezas por sítio, e **cada uma existe porque um dos dois botões a consome**. Uma grandeza que
não alimente `size`, `depth` ou uma recusa nomeada não entra.

| grandeza | quem a consome |
|---|---|
| **aberturas** (quantas vezes a região deste sítio abriu) | denominador de tudo; distingue quente de frio |
| **marca-d'água por abertura** (histograma log2, 32 baldes) | **`size`** — é a distribuição de que sai o quantil (§2.1) |
| **chunks por abertura** | **`size`** — quantos `malloc` extra o chão não evitou |
| **cauda desperdiçada** (`cap - used`) | **`size`** e **`depth`** — o custo do excesso e o custo de abrir |
| **profundidade atingida** (nível na árvore de regiões no momento da abertura) | **`depth`** — é o eixo do achatamento |
| **bytes recuperados no drop** | **`depth`** — uma região que recupera menos do que o seu chão não paga o próprio chão |
| **bytes irroteáveis** (`str`/`malloc`, `tk_obs_mstr`) | a **recusa nomeada** (§6) |

O histograma log2 de 32 baldes é uma escolha, e a razão é o requisito do `#475` levado a sério:
**tabela de tamanho fixo, zero alocação em corrida, e o quantil sai dela sem guardar amostras.**

### 1.3 A oitava grandeza, que não era minha e tem consumidor nomeado — o R3 da spine

`docs/design/safety-spine.md:566-576`, risco **R3 [MEDIUM]**:

> *"No immutable borrow (`&T`) → forced arena copies on hot read-only paths… **INSTRUMENT** — measure
> arena-copy volume on self-build before/after Block 1. Introduce a read-only view **ONLY if PGO
> shows hot large copies**."*

A spine aceita conscientemente uma perda e **delega a decisão de a corrigir a este documento**. Logo o
profiler mede também:

> **volume de cópia para arena, por sítio, com o tamanho do que é copiado.**

E digo já a parte desagradável: **essa medição não pode vir de um endereço de retorno.** Uma cópia de
struct é emitida em linha pelo codegen; ela não atravessa função de runtime nenhuma, logo não há `ra`
para a ver. **A única via é a tabela de sítios de compilação** — a mesma do §5. As duas necessidades
convergem na mesma peça, o que é o argumento mais forte que ela tem.

**O limiar que faria a `&T` valer a pena**, proposto e falsificável pela primeira corrida:

> bytes copiados atribuíveis a **parâmetros só-lidos** > **10 % dos bytes alocados totais**, **e**
> concentrados em **≤ 20 sítios**.

As duas metades são necessárias e a segunda é a que importa: `&T` é uma capacidade de linguagem, com
custo de superfície, de checker e de espinha. Se os 10 % estiverem espalhados por 500 sítios, o ganho
por sítio é ruído e a resposta certa é não a construir. **Se a primeira corrida desmentir o 10 %, é a
corrida que fica** — o número é a minha proposta, não uma medição.

---

## 2. Como o profiler ATRIBUI `#arena_size` e `#arena_depth` — o coração

### 2.1 `size` — o chão que evita realloc, e a estatística sai de um custo, não de um gosto

**A pergunta "máximo? percentil?" tem resposta derivável, e ela não é nenhuma das duas por
preferência.** É um problema de jornaleiro (*newsvendor*): há um custo de ficar aquém e um custo de
passar além, eles são **assimétricos**, e o ótimo de uma distribuição sob custos assimétricos é
**sempre um quantil**, cujo valor sai da razão dos custos:

```
q* = c_sub / (c_sub + c_sobra)
```

**Ambos os custos estão medidos nesta árvore.**

* **`c_sobra` — passar além.** Um byte a mais no chão é um byte reservado que não se usa. E há um
  agravante escrito na própria fonte, `cg_emit_arena_presize` (`codegen.tks:9573-9596`): não existe
  primitiva de *reservar sem consumir*, logo a chamada de pré-dimensionamento **consome** os `N`
  bytes que reserva. O excesso paga-se **duas vezes** — uma no `malloc`, outra na contabilidade do
  chunk. Ainda assim, em bytes: **`c_sobra` = 1 byte por byte de excesso.**
* **`c_sub` — ficar aquém.** Um chunk extra. Medido no build real: `26,6 MB de cauda / 21 134 chunks`
  = **1,32 KB de cauda desperdiçada por chunk**, mais um `posix_memalign` que eu **não** medi em tempo
  (e por isso não o conto — a conta abaixo é um limite INFERIOR do quantil).

```
q* = 1320 / (1320 + 1) = 0,99924
```

**O quantil é p99,9 — praticamente o máximo, e é por isso que a intuição "usa o máximo" quase
acerta.** Mas *"quase"* é o ponto: com 3,48 M alocações num só sítio, uma cauda pesada é certa, e é
exatamente a última milésima que contém a invocação patológica cujo chão seria desastroso replicar em
todas as outras. **A regra é "o máximo menos a cauda extrema", e o profiler diz qual foi o corte.**

> **`size` = o balde `p99,9` do histograma de marca-d'água por abertura, arredondado para cima ao
> balde, e a saída IMPRIME a distribuição — não só o número.**

E três guardas, cada uma com a razão:

1. **`size < 64 KiB` não se sugere.** `TK_REGION_DEFAULT_CHUNK` já é 64 KiB (`teko_rt.h:140`) e
   `tk_region_alloc` dimensiona o primeiro chunk a `max(pedido, 64 KiB)`. Uma sugestão abaixo do chão
   é uma diretiva que não faz nada — pior que nenhuma, porque parece que faz.
2. **Menos de `N` aberturas ⇒ `Confidence::Thin`, e a sugestão sai marcada.** Um quantil p99,9 sobre
   12 amostras é o máximo com outro nome.
3. **Um sítio dominado por bytes irroteáveis ⇒ recusa nomeada, nunca sugestão** (§6).

### 2.2 `depth` — o achatamento, e o custo está medido

Definição do dono: *"o depth seria o nível de achatamento que ela comportaria"*. Além do nível `d`, o
escopo **não abre região** — as suas alocações vão para a região envolvente mais próxima. É
exatamente o que `regions.len < 64` já faz hoje em quatro sítios (`codegen.tks:5412`, `:7425`,
`:8156`, `:8768`), com o comentário a chamar-lhe *"a safe leak"*, e a que `TK_ARENA_MARK_MAX 64`
(`teko_rt.c:1184`) faz eco no runtime. **`#arena_depth` não introduz conceito: dá nome, e controlo por
declaração, a um tecto que já existe fixado a 64 em três sítios independentes.**

**O critério é um ponto de equilíbrio por nível, e ele é brutal — é o achado que mais muda o
desenho:**

> Uma região **usada** custa **64 KiB** de chão (`TK_REGION_DEFAULT_CHUNK`), porque a sua primeira
> alocação puxa um chunk inteiro. Logo **uma região só se paga a si própria se recuperar mais do que
> o seu chão.**

Uma região de bloco que segure 200 bytes custa 64 KiB para segurar 200 bytes — **uma perda de 300×**.
Isto diz uma coisa que precisa de ser dita alto: **sub-regiões por padrão em TODO o escopo é
inacessível com o chão de hoje, e o `depth` é o que a torna acessível — não uma afinação fina.**

> **`depth` = o nível `d` mais profundo tal que, em todo o nível ≤ `d`, a média de bytes recuperados
> no drop excede o custo do chão daquele nível.**

Isto é diretamente calculável a partir de duas das sete grandezas do §1.2 (*bytes recuperados no
drop*, *profundidade atingida*), e produz um número **por declaração**, que é a granularidade a que a
diretiva se aplica.

**E daqui sai o achado adjacente que REPORTO e não converto em issue:** o critério acima também
dimensiona o *outro* botão que ninguém declarou — **`TK_REGION_DEFAULT_CHUNK` é uma constante única
para regiões de todas as profundidades**, e uma região profunda quase nunca precisa de 64 KiB. Um chão
que encolhesse com a profundidade baixaria o ponto de equilíbrio e deixaria o `depth` subir. **Isto é
custo, não correção, não pertence a nenhum dos quatro crumbs, e a decisão de o puxar é do dono.**

### 2.3 A precedência — `manual > PGO > omissão`, e ela resolve uma tensão de lei real

**Há uma tensão, ela é literal, e está escrita em dois sítios da árvore** (`ast.tks:439`,
`parse_decl.tks:931`):

> *"O compilador nunca INFERE este número (ruling do dono, 2026-07-13): um `teko profile` (#479)
> posterior pode SUGERIR um, nunca atribuí-lo."*

E o `#480` diz *"o pré-linker atribui automaticamente tamanho+profundidade a partir do relatório"*.

**Resolução law-first, e ela passa nas duas leis sem torcer nenhuma:**

| camada | quem escreve | o que a lei diz |
|---|---|---|
| **manual** — `#arena_size(N)` / `#arena_depth(N)` na fonte | **só o humano** | intacta: o literal é sempre escrito por uma pessoa |
| **PGO** — `.tkprof` passado ao build | o profiler, num artefato **à parte** | não é a diretiva; é o **valor por padrão** para quem não a tem |
| **omissão** | o compilador | `TK_REGION_DEFAULT_CHUNK` e o tecto de hoje |

**O `#480` nunca escreve na fonte, e nunca produz uma diretiva.** Ele substitui o *default*, e a
diretiva — quando existe — ganha-lhe. A ruling de 2026-07-13 fala do **número que a diretiva
carrega**; o PGO não toca nesse número. As duas leis compõem-se, e a precedência que o plano já
escreveu (*"manual > PGO > omissão"*) é exatamente o enunciado dessa composição.

**Consequência que fixo aqui:** o profiler **imprime a diretiva como texto para o humano colar**, e o
pré-linker **lê o `.tkprof`**. Nenhum dos dois reescreve `.tks`. Um profiler que reescrevesse fonte
violaria a ruling e, pior, tornaria o corpus não reproduzível a partir do que está no git.

**E o `.tkprof` entra por bandeira explícita, `--pgo <arquivo>`, nunca por descoberta automática no
diretório do projeto.** A razão é um defeito já nomeado e já pago: um arquivo de corrida anterior a
flutuar e a ser lido como se fosse desta corrida (`journaling-de-corrida-0.3.1.md` §5.2, defeito 3).
Um build tem de dizer o que leu. **Descoberta automática é um portão que não gateia.**

### 2.4 O `#arena_depth`, na grafia que a árvore obriga

Espelha `parse_arena_size_arg` (`parse_decl.tks:934-943`) linha a linha — porque a forma já foi
ratificada e divergir dela seria inventar uma segunda gramática para o mesmo tipo de argumento.

```teko
/**
 * parse_arena_depth_arg — ler o argumento `(N)` de `#arena_depth(N)`. `pos` está no `#`.
 *
 * MESMA FORMA de `parse_arena_size_arg`, e pela mesma razão: `N` é um literal inteiro nu, escrito
 * pelo programador, nunca uma expressão e nunca um número que o compilador calcule (ruling do dono,
 * 2026-07-13). O `0` é recusado — achatar até zero é não abrir região nenhuma, e isso já é o
 * comportamento padrão de hoje, não uma diretiva.
 *
 * @param tokens  o fluxo de tokens do módulo
 * @param pos     o índice do `#` que abre o atributo
 * @return        o valor lido e o índice de retoma
 * @throws        quando falta o `(`, o literal não é um inteiro positivo, ou falta o `)`
 */
fn parse_arena_depth_arg(tokens: []lexer::Token, pos: u64): ParsedU64 | error {
    if !is_kind_at(tokens, pos + 2, lexer::TokenKind::LParen) { return err_at(tokens, pos + 2, "expected '(' after `#arena_depth`") }
    if !is_kind_at(tokens, pos + 3, lexer::TokenKind::Number) { return err_at(tokens, pos + 3, "expected a positive nesting depth in `#arena_depth(N)`") }
    let lit = tokens[pos + 3].text
    let n = match lit_int(lit) { NumInt as v => v; error as e => return err_at(tokens, pos + 3, e.message) }
    if n.neg || n.mag == (0 to u64) { return err_at(tokens, pos + 3, "`#arena_depth(N)` requires a positive nesting depth") }
    if !is_kind_at(tokens, pos + 4, lexer::TokenKind::RParen) { return err_at(tokens, pos + 4, "expected ')' to close `#arena_depth(…)`") }
    ParsedU64 { value = n.mag; next = pos + 5 }
}
```

Os campos gémeos em `ParsedAttributes` (`parser/result.tks`) e em `Function` (`ast.tks:442-448`) são
`has_arena_depth: bool` / `arena_depth: u64`, e o ramo em `parse_decl_attributes` é o gémeo do
`arena_size` (`parse_decl.tks:899-905`), incluindo a recusa de duplicado e a mensagem de atributo
desconhecido, que ganha `#arena_depth(N)` na enumeração.

**E o consumo no emissor é substituição, não adição:** os quatro `regions.len < 64` passam a
`regions.len < cg_arena_depth_of(f)`, uma função que devolve `f.arena_depth` quando declarado, o valor
do `.tkprof` quando há PGO, e `64` por padrão. **A constante mágica de hoje torna-se o ramo padrão da
mesma expressão** — é a mesma linha, com um nome.

---

## 3. Como ele REAPROVEITA o journal em vez de abrir um segundo canal

**Reaproveita quase inteiramente, e a peça que falta ao journal é UMA — e não é infraestrutura.**

O `Rec`/transporte/fan-in/2048/`chunk_n` das §§17–29 foram desenhados para **veredictos e
asserções**: registos pequenos, muitos, atribuíveis, em tempo real. **A afinação de arena não tem essa
forma, e a §28.2 já decidiu o caso irmão** — a cobertura **não vira tráfego de canal**, porque
acumula em tabelas e despeja **uma vez**.

**Os dados de arena são exatamente esse caso, e por um argumento que se pode produzir:**
`tk_alloc` tem **3 484 564** chamadas num build. Um registo por alocação seriam 3,48 M mensagens de
80 bytes = **~265 MB de tráfego de canal** para medir 1926 MB de alocação. **A instrumentação custaria
uma fração significativa daquilo que mede** — e a §28.2 já apanhou esse desastre uma vez, na forma
"um registo por linha executada". Repetir seria repetir o erro que o dono já corrigiu.

> **Logo: acumula em tabelas de tamanho fixo, despeja UMA vez, e o journal transporta o FATO do
> despejo.** É a forma da cobertura, aplicada a uma segunda medição — não uma segunda infraestrutura.

### 3.1 A emenda ao journal, e ela é de um campo

A §28.3 tem `Cov` **vazio**, e o vazio é o ponto: ele transporta *"o despejo deste escritor está
completo"*, o que torna um despejo em falta **detectável**. O profiler precisa exatamente da mesma
propriedade, para o seu despejo. A emenda mínima:

```teko
/**
 * DumpMark — o marcador de que UM despejo de tabelas deste escritor está COMPLETO.
 *
 * ERA `Cov` (§28.3) E É A MESMA COISA COM O NOME HONESTO: passou a haver mais do que um despejo, e
 * o registo continua a não transportar identificadores — transporta o FATO de o despejo existir,
 * UM por escritor e por espécie. O argumento de §28.3 fica intacto; só o nome deixou de ser estreito.
 *
 * RENOMEAR CUSTA ZERO E É POR ISSO QUE SE FAZ AGORA: `teko::journal` não existe na árvore (medido —
 * zero acertos de `teko::journal`/`tk_journal` em `src/`), logo o tipo ainda não tem um usuário.
 *
 * @since 0.3.1
 */
pub type DumpMark = struct {
    /** que despejo: `0` cobertura (`.tkcov`), `1` arena (`.tkprof`). Um `u8`, não uma espécie
        nova — duplicar a maquinaria do marcador para um fato que difere num valor enumerado é
        exatamente o que a §28 recusou quando eliminou o `RecVerdict`. */
    what: u8
}

/**
 * RecBody — o corpo de um registo. DUAS espécies, e continuam duas.
 *
 * @since 0.3.1
 */
pub type RecBody = variant Assert | DumpMark
```

### 3.2 O que o journal dá de graça, e é mais do que parece

| peça das §§17–29 | serve o profiler? |
|---|---|
| `Rec` binário, quadro prefixado por comprimento (§26.4) | **sim, sem alteração** — o `.tkprof` copia o enquadramento e a forma magic+versão+remédio do `.tkb` |
| transporte (`AF_UNIX`/`\\.\pipe\`), tecto 2048, `chunk_n` (§29, adenda 07-31) | **sim** — para o marcador, que tem 1 byte de corpo; **e nunca dispara chunk**, o que é a resposta certa |
| fan-in por `writer` (§16) | **sim, e é necessário** — um build paralelo tem N escritores e N `.tkprof` a fundir |
| `sweep`/`run_root`/`scratch` (§2.2, §7.2) | **sim** — o `.tkprof` é um artefato da corrida e obedece à mesma raiz |
| `tk_rt_rename` (§2.3) | **sim** — o `.tkprof` é um artefato INTEIRO, publica-se por `rename`, nunca meio-escrito |
| a detecção de despejo em falta (§28.3) | **sim, e é o que impede o defeito que a cobertura já teve**: um `.tkprof` da corrida anterior lido como desta |
| ordenação global, carimbo de tempo | **não é preciso** — e a §24.2 já o recusou por bons motivos, que se mantêm |

**Nada tem de ser diferente. Não tenho prova a apresentar porque não há divergência a provar.**

---

## 4. O `.tkcov` — o que sobra do crumb 12.1 depois do journal

O `#475` pede *"sinal de disco apenas, zero alocação em corrida"*. **Fui verificar as três metades e a
conclusão é que o requisito, tal como está escrito, mede a grandeza errada — e a certa é pior.**

### 4.1 O despejo já está certo, e não se toca

`tk_cov_dump` (`teko_rt.c:3285`) é um `fopen(path,"wb")` que percorre as tabelas e escreve três
secções com o magic `TKCOV1`. O `fread` está do lado do agregador. **A forma "acumula, despeja uma
vez, lê no fim" já é a que o dono descreve.** Quem abrir o 12.1 à procura de um append de cobertura
para "consertar" está consertando o que já está certo.

### 4.2 "Zero alocação em corrida" é **falso hoje** — e o defeito é outro, e maior

| sumidouro | dedup por acerto | aloca em corrida? | fonte |
|---|---|---|---|
| **linhas** | conjunto de endereçamento aberto | sim, no `rehash` (dobra) | `tk_line_insert_packed`, `:3233` |
| **funções** | **varrimento LINEAR do vector inteiro** | sim, `realloc` dobrando | `tk_cov_mark`, `:3127` |
| **ramos** | **varrimento LINEAR do vector inteiro** | sim, `realloc` dobrando | `tk_covb_add`, `:3178` |

E a frequência é o pior: `tk_cov_mark` é **prólogo de entrada de função** e `tk_cov_branch_at` corre
em **cada passagem por ramo**. **O custo dominante não é a alocação — é `O(distintos)` por acerto.**

### 4.3 O que sobra do 12.1, e fecha o requisito à LETRA em vez de o aproximar

Três itens, e o terceiro é o que torna a frase do `#475` literalmente verdadeira pela primeira vez:

1. **Funções e ramos adotam o conjunto de endereçamento aberto que as linhas já usam.** A função
   existe três dezenas de linhas acima, no mesmo arquivo. O despejo, o formato e o agregador **não
   mudam uma linha**.
2. **O `DumpMark` no journal** (§3.1) — um despejo em falta passa a ser detectável em vez de ser
   silenciosamente substituído pelo da corrida anterior.
3. **As três tabelas nascem PRÉ-DIMENSIONADAS a partir da caminhada estática.** O compilador já conta
   funções (`count_prod_fns`), sítios de ramo (`cov_walk`) e linhas instrumentadas — os três números
   que decidem a capacidade. Com eles no arranque, **não há um único `realloc`/`rehash` em corrida**,
   e `zero alocação em corrida` deixa de ser aspiração e passa a ser invariante verificável.

**Este terceiro item é de graça porque a caminhada já existe.** É a mesma economia do §5: quem já sabe
o número não precisa de o descobrir a correr.

---

## 5. O TESTE DE FOGO — o sítio de chamada de topo, e a resposta é SIM, com uma condição

1700,5 MB, 3 484 564 alocações, `?`. **O profiler consegue responder. Mas não pela via que a pergunta
oferece primeiro, e a razão é decisiva.**

### 5.1 As três vias, medidas nesta caixa

Probe em `/tmp/.../scratchpad/ra/p.c` — função `static`, PIE, símbolo não exportado: **o mesmo caso
que produz o `?`**.

| via | medido | veredicto |
|---|---|---|
| `dladdr` como está hoje | `dladdr=1`, **`sname=(NULL)`**, `fname=./p`, `fbase=0x55a43e91b000` | é o `?`, e explica-o |
| **`ra - dli_fbase` + `addr2line`** | offset `0x10c1`; `addr2line -f -e ./p 0x10c1` → **`main` + `p.c:13`** | **funciona, sem religar, com FICHEIRO:LINHA** |
| **`-rdynamic`** | `sname=main` — um **nome**, sem linha; e custa uma religação | **estritamente PIOR. Recusado.** |

E as duas fronteiras, medidas:

* **sem `-g`, não-despojado:** `addr2line -f` dá o **nome da função**, `??:?` na linha. Chega para
  atribuir.
* **despojado (`strip`):** `??` / `??:0`. **Nada.** O requisito é *binário não despojado*, e tem de
  estar escrito onde alguém o possa quebrar.

**Custo desta via: um `fprintf`.** O `ra` já está gravado (§0.2); o despejo só nunca o imprimiu.

### 5.2 E é aqui que ela morre — duas vezes, e as duas por medição

**Primeira morte: simbolizar o `ra` nomeia uma função do RUNTIME, nunca uma declaração Teko.**

`tk_alloc` tem **18 sítios de chamada em toda a árvore**, e **todos** dentro de `src/runtime/teko_rt.c`.
Simbolizar o topo devolveria algo como `tk_slice_push` — e `#arena_size` **não se pode escrever numa
função do runtime**: a diretiva liga-se a um `fn` de topo em Teko (`ast.tks:433-441`, e as
`parse_decl_attributes` nem sequer correm sobre métodos). **O `ra` responde "onde no runtime". O
afinador precisa de "qual declaração".** É precisamente por isso que a árvore já construiu o RA1/RA2
(`tk_obs_push`, `tk_g_push_ra`, `#148`) para atravessar o salto do embrulho — e construiu-o **para um
caminho só**, o `tk_slice_push`.

**Segunda morte, e esta é definitiva: no backend nativo não há para onde resolver.**

Varri a árvore: **zero ocorrências de `debug_info`, `debug_abbrev`, `DW_TAG` ou `.debug` em todo o
`src/`.** O backend próprio emite `.symtab` (`objfile_elf.tks:906`, `Elf64_Sym`) e **nenhum DWARF**.
`addr2line` sobre um binário nativo dá, no melhor caso, o nome de uma função — nunca a linha, nunca o
bloco. **E a onda 0.3.1.0 é a onda nativa.** Uma via de atribuição que só funciona na rota C é uma via
que morre no dia em que a onda fecha.

### 5.3 A resposta, então — e ela é a "tabela de sítios emitida em compilação"

**O precedente está no mesmo arquivo que vai ser alterado:** `emit_cov_line` emite
`tk_cov_line_at(<fn_idx>, <line>)` — **a posição é injectada pelo compilador, não descoberta em
execução**. A mesma forma, aplicada às aberturas de região:

```
tk_region_new(parent):   tk_region_new_at(parent, <site_id>)
```

**Cinco sítios de emissão no `codegen.tks`, todos já localizados:** `:5441` (moldura de função),
`:7431` (braço de valor), `:8165` (`emit_block_region`), `:8772` (corpo de `loop`), `:5159` (região
por objeto de classe). Mais uma tabela estática que o emissor escreve com
`(fn_name, file, line, col, kind, depth_léxica)`.

| propriedade | `ra` + `addr2line` | tabela de sítios |
|---|---|---|
| nomeia uma **declaração Teko** | **não** | **sim** |
| granularidade sub-função (bloco, braço, corpo de laço) | não | **sim** |
| funciona no **backend nativo** | **não** (zero DWARF) | **sim** |
| funciona no Windows | não (`dladdr` é POSIX) | **sim** |
| exige binário não despojado | **sim** | não |
| custo | um `fprintf` | uma tabela + 5 sítios de emissão |
| vê as **cópias em linha** do R3 (§1.3) | **não** — não passam por runtime | **sim** |

**Proposta: as DUAS, com trabalhos diferentes, e nenhuma delas é `-rdynamic`.**

* **A tabela de sítios é o instrumento principal.** É ela que atribui bytes a declarações, que carrega
  a profundidade, que sobrevive ao nativo e ao Windows, e que é a única que vê as cópias do R3.
* **O `ra` + offset fica como canal RESIDUAL**, e ganha um trabalho preciso: **os bytes que chegam à
  raiz sem passar por região nenhuma.** Esses não têm sítio de abertura para lhes atribuir, e são hoje
  a maioria — 88 % num único posto. **O residual é a medida do que o actuador ainda não alcança**, o
  que o liga diretamente ao §7. Custa um `fprintf` e a linha de documentação que diz "não despojado,
  `-g` para linha, rota C apenas".

**E respondendo à parte dura da pergunta — "se o profiler não consegue responder a isto, não vale a
pena":** ele consegue, e o que a pergunta destapou é melhor do que uma resposta. **A tabela de sítios
não é um custo do profiler — é uma peça de que a espinha, o R3 e o `depth` precisam todos**, e o
profiler foi o primeiro a ter de a nomear.

---

## 6. `tk_str_concat_r` NÃO EXISTE — e o profiler tem de o DIZER, não contornar

Verificado no cabeçalho: existem `tk_str_concat` (`teko_rt.h:386`) e `tk_str_concat_len` (`:411`).
**Não existe `tk_str_concat_r`.** E os únicos alocadores de toda a árvore que aceitam região são
**dois**: `tk_slice_push_r` e `tk_slice_with_cap_r`.

No build real: **66,4 MB em 2 165 811 buffers**, na linha `MALLOC str total` — **um contador
separado, fora da árvore de regiões** (`tk_obs_mstr`). Nenhum drop de região os alcança. Não hoje, não
com sub-regiões por padrão, não com o predicado alargado, não com `depth` nenhum.

**A consequência para o afinador é direta e não é opcional:**

> Um sítio cujos bytes são maioritariamente `str` **não pode ser afinado por `#arena_size`**. O chão
> da região não toca em bytes que nunca entram na região.

**Logo o profiler separa, POR SÍTIO, os bytes roteáveis dos irroteáveis, e recusa sugerir sobre um
sítio dominado pelos segundos — com a razão nomeada, nunca em silêncio:**

```
  src/codegen/codegen.tks:8120  emit_block
      1204.3 MB observados · 91 % em `str` (irroteável: falta `tk_str_concat_r`)
      SEM SUGESTÃO — afinar a região aqui mexeria em 9 % dos bytes.
```

**Isto transforma o buraco de nota de rodapé em SAÍDA de primeira classe do profiler**, e dá-lhe um
papel que nenhuma outra peça tem: **é ele que mede quanto vale fechar o buraco, sítio a sítio, antes
de alguém pagar o trabalho.** Uma recusa que traz o número é mais útil do que uma sugestão que não
pode funcionar.

**Ordenação que daqui sai, e é dura:** `tk_str_concat_r` é pré-requisito de qualquer suposição de que
o `#arena_size` afina o consumo dominante do compilador. Enquanto não existir, o profiler afina — com
honestidade — a minoria roteável, e **relata o tamanho da maioria que não pode tocar**.

---

## 7. O profiler como ORÁCULO DO ACTUADOR — a peça que ele hoje não tem

A observação é do integrador e é a mais valiosa deste arquivo: **a espinha classifica na perfeição e
a memória não se mexe.** O eixo `PtFrame`/`PtRoot`/`PtParam`/`PtAdopter` (`spine.tks:98`) raciocina
sobre *onde o armazenamento vive* — e está medido que nada é encaminhado para lá: recuperação
**0,0 %**, dois alocadores com região, seletor de dois níveis, `tk_str_concat_r` inexistente.

**Proposta: o profiler é o oráculo de que a mudança de arena fez alguma coisa.** As quatro medições já
existem no despejo de hoje; o que falta é declará-las como afirmações que podem **falhar**.

| # | afirmação | valor de hoje | porque sozinha não chega |
|---|---|---|---|
| 1 | `reclaim ratio` > 0 | **0,0 %** | podia subir com regiões novas que não aliviam a raiz |
| 2 | `regions dropped / regions new` sobe | **11 / 5007** | podia subir largando regiões vazias |
| 3 | `scoped (freed at region drop)` > 0 MB | **0,0 MB** | podia subir movendo bytes entre regiões |
| 4 | **`root (never freed)` DESCE** | **1926,3 MB** | **é a única que não se pode satisfazer por acidente** |

**As quatro juntas, e a 4 é a que manda.** As três primeiras podem todas mover-se enquanto a raiz fica
igual — bastaria abrir regiões a mais. **Só a quarta mede que o problema encolheu**, e é por isso que
ela é a que vai ao portão.

**A forma de portão** (`arena_reclaim_ratio_nonzero`, §9): depois de o roteamento entrar, uma corrida
instrumentada do self-build cujo `.tkprof` tenha `reclaim ratio == 0,0 %` **reprova a corrida**. É a
inversão que a lei do projeto exige — sem ela, "o actuador funciona" é prosa que substituiu prova.

---

## 8. A superfície proposta — `teko::profile`

Escrita já em Javadoc completo, para ser copiada tal como está. Verificada contra a árvore
(`enum { A; B }`, `variant` de tipos nomeados, `pub type X = struct { }`, `-> T | error`).

```teko
// src/profile/profile.tks   (namespace 'teko::profile')

use teko::checker

/**
 * SiteKind — que construção abriu a região que este sítio nomeia.
 *
 * OS CINCO SÃO OS CINCO SÍTIOS DE EMISSÃO QUE JÁ EXISTEM no `codegen.tks` (`:5441`, `:7431`,
 * `:8165`, `:8772`, `:5159`) — a enumeração é um espelho da árvore, não uma taxonomia nova. Não há
 * membro para o `adopt`: a construção sai (§11.2), e a sua região era a única incondicional.
 *
 * @since 0.3.1
 */
pub type SiteKind = enum { Frame; Block; Arm; LoopBody; Object }

/**
 * Site — UMA abertura de região, tal como o compilador a conhece: a entrada da tabela estática que
 * o emissor escreve e que o `.tkprof` carrega no seu cabeçalho.
 *
 * CARREGAR A TABELA DENTRO DO FICHEIRO É O QUE O TORNA PORTÁVEL — é a mesma decisão da §28.6 do
 * journal, e pela mesma razão: um índice que aponta para uma tabela externa (o `TProgram` de outro
 * compilador) não atravessa versões; um índice que aponta para o cabeçalho do próprio arquivo sim.
 *
 * @since 0.3.1
 */
pub type Site = struct {
    /** o identificador que o emissor injecta em `tk_region_new_at(parent, id)`. */
    id: u32
    /** que construção o abriu. */
    kind: SiteKind
    /** a declaração Teko a que uma diretiva se aplicaria — é ISTO que o `ra` nunca dá (§5.2). */
    fn_name: str
    /** o arquivo da declaração. */
    file: str
    /** a linha da abertura. */
    line: u32
    /** a coluna da abertura — duas aberturas na mesma linha têm sítios DIFERENTES. */
    col: u32
    /** a profundidade LÉXICA de aninhamento neste sítio; a atingida em execução vem no `SiteStat`. */
    lexical_depth: u32
}

/**
 * SiteStat — o que a corrida observou neste sítio. Sete grandezas, e cada uma tem consumidor (§1.2).
 *
 * TABELA DE TAMANHO FIXO POR CONSTRUÇÃO: `hist` tem sempre 32 baldes, um por potência de dois, e é o
 * que permite tirar um quantil sem guardar amostras — o requisito de "zero alocação em corrida" do
 * `#475` aplicado a esta medição desde o primeiro dia, em vez de remendado depois.
 *
 * @since 0.3.1
 */
pub type SiteStat = struct {
    /** o `Site.id` a que estes números pertencem. */
    site: u32
    /** quantas vezes a região deste sítio abriu — o denominador de tudo. */
    opens: u64
    /** o histograma log2 da marca-d'água por abertura; 32 baldes, `hist[k]` = aberturas cujo pico caiu em `[2^k, 2^(k+1))`. */
    hist: []u64
    /** chunks pedidos ao todo — quantos `malloc` o chão não evitou. */
    chunks: u64
    /** bytes de cauda (`cap - used`) somados sobre os chunks deste sítio. */
    tail_bytes: u64
    /** bytes recuperados nos drops deste sítio — o numerador do critério de `depth` (§2.2). */
    reclaimed_bytes: u64
    /** a profundidade MÁXIMA de árvore de regiões observada ao abrir aqui. */
    max_depth: u32
    /** bytes que passaram por região (afináveis por `#arena_size`). */
    routable_bytes: u64
    /** bytes fora da árvore de regiões — `str`/`malloc` (§6). É por causa deste campo que uma recusa
        pode trazer um número em vez de um encolher de ombros. */
    unroutable_bytes: u64
    /** bytes copiados para arena atribuíveis a parâmetros só-lidos — o R3 da spine (§1.3). */
    copy_bytes: u64
}

/**
 * Confidence — quanta massa de amostra sustenta uma sugestão.
 *
 * EXISTE PORQUE UM QUANTIL p99,9 SOBRE DOZE AMOSTRAS É O MÁXIMO COM OUTRO NOME, e uma sugestão que
 * não diz isso está apresentando um palpite com a mesma cara de uma medição.
 *
 * @since 0.3.1
 */
pub type Confidence = enum { Measured; Thin; Absent }

/**
 * Suggestion — o que o profiler propõe para UMA declaração, com o que é preciso para o desmentir.
 *
 * TRAZ OS CUSTOS, NÃO SÓ O NÚMERO: `saved_chunks` e `cost_bytes` são os dois lados da assimetria de
 * §2.1, e sem eles a sugestão é inauditável — quem a lê não pode verificar a conta.
 *
 * @since 0.3.1
 */
pub type Suggestion = struct {
    /** a declaração a anotar. */
    fn_name: str
    /** o chão proposto em bytes, ou `0` quando há recusa (`reason` diz porquê). */
    size: u64
    /** a profundidade proposta, ou `0` quando há recusa. */
    depth: u32
    /** o quantil usado, em partes por milhão (999240 = p99,924) — IMPRESSO, para a conta ser refeita. */
    quantile_ppm: u32
    /** chunks que o chão proposto teria evitado nesta corrida. */
    saved_chunks: u64
    /** bytes de excesso que o chão proposto teria reservado sem usar. */
    cost_bytes: u64
    /** quanta amostra sustenta isto. */
    confidence: Confidence
    /** vazio quando há sugestão; a razão NOMEADA quando não há (§6). Nunca silêncio. */
    reason: str
}

/**
 * Profile — um `.tkprof` lido: o cabeçalho, a tabela de sítios e as observações.
 *
 * @since 0.3.1
 */
pub type Profile = struct {
    /** a versão do FORMATO — governa a leitura; divergente ⇒ recusa com remédio, no estilo do `.tkb`. */
    format: u32
    /** o resumo da lista de declarações que produziu esta corrida — o PGO recusa aplicar um relatório
        de outra árvore, porque um `Site.id` só significa alguma coisa contra as declarações que o
        emitiram (é a guarda que o `.tkcov` nunca teve). */
    decl_digest: u64
    /** a tabela de sítios, escrita pelo emissor e carregada aqui — é o que torna o arquivo portável. */
    sites: []Site
    /** o que a corrida observou, paralelo a `sites` por `Site.id`. */
    stats: []SiteStat
    /** bytes que chegaram à raiz sem passar por região — a medida do que o actuador não alcança (§7). */
    root_bytes: u64
    /** bytes recuperados por drop de região — o numerador da razão de recuperação (§7). */
    reclaimed_bytes: u64
}

/**
 * walk — o modo ESTÁTICO: enumerar todos os sítios de abertura de região de um programa tipado.
 *
 * É A CAMINHADA DA COBERTURA COM OUTRO SOMATÓRIO — `src/coverage/coverage.tks` já percorre o
 * `TProgram` a enumerar sítios de ramo e linhas; esta enumera aberturas de região. Dá o
 * DENOMINADOR (que sítios existem), nunca o numerador (quais valem a pena) — §1.1.
 *
 * @param prog  o programa tipado
 * @return      a tabela de sítios, ordenada por arquivo e linha
 * @since 0.3.1
 */
pub fn walk(prog: checker::TProgram): []Site

/**
 * read — ler um `.tkprof` produzido por uma corrida instrumentada.
 *
 * @param path  o arquivo a ler
 * @return      o perfil lido
 * @throws      magic errado, `format` divergente (com o remédio na mensagem), ou quadro rasgado no fim
 * @since 0.3.1
 */
pub fn read(path: str): Profile | error

/**
 * suggest — derivar `#arena_size`/`#arena_depth` por declaração a partir de um perfil.
 *
 * NÃO ESCREVE NA FONTE, POR LEI: a ruling de 2026-07-13 (`ast.tks:439`) diz que o compilador nunca
 * INFERE o número da diretiva; um `teko profile` pode SUGERIR, nunca atribuir. O que sai daqui é
 * texto para uma pessoa colar, ou entrada para o PGO — que substitui a OMISSÃO, não a diretiva
 * (§2.3).
 *
 * Uma declaração dominada por bytes irroteáveis sai com `size = 0` e `reason` preenchida (§6).
 *
 * @param p  o perfil lido
 * @return   uma sugestão (ou uma recusa nomeada) por declaração com sítios observados
 * @since 0.3.1
 */
pub fn suggest(p: Profile): []Suggestion

/**
 * render — o relatório humano: a distribuição, os dois custos, a diretiva pronta a colar.
 *
 * IMPRIME A DISTRIBUIÇÃO E NÃO SÓ O NÚMERO, porque a escolha do quantil (§2.1) é uma decisão de
 * custo que quem lê tem de poder contestar com os mesmos dados.
 *
 * @param p    o perfil
 * @param sug  as sugestões derivadas
 * @return     o relatório
 * @since 0.3.1
 */
pub fn render(p: Profile, sug: []Suggestion): str

/**
 * run_cli — o subcomando `teko profile`.
 *
 * DOIS FRONTENDS, UM LEITOR — a mesma disciplina da §26.7 do journal: `teko profile <projdir>`
 * corre e relata; `teko profile <arquivo>.tkprof` só relata; e o pré-linker do `#480` lê o MESMO
 * arquivo pelo MESMO `read`.
 *
 * @param args  o `argv` completo (`args[1]` é `profile`)
 * @return      o código de saída do processo
 * @since 0.3.1
 */
pub fn run_cli(args: []str): i32
```

E o fundo de runtime — **C mantido, e cabe nas duas permissões da lei** (o backend nativo precisa de
uma forma que o runtime não oferece; nada disto é emissão de C pelo compilador):

```c
// tk_region_new_at — tk_region_new com o SÍTIO ESTÁTICO que a abriu. O `site` indexa a tabela que o
// emissor escreve (o molde do tk_cov_line_at), e é o que liga bytes a uma DECLARAÇÃO Teko em vez de
// a uma função do runtime — a distinção que o endereço de retorno nunca consegue fazer.
// Quando a observação está desligada é tk_region_new com um argumento ignorado: um compare previsto.
tk_region *tk_region_new_at(tk_region *parent, uint32_t site);
// tk_prof_note_copy — somar `n` bytes de cópia-para-arena ao sítio `site` (R3 da spine). Emitido em
// linha pelo codegen, que é o único que sabe que a cópia existe e quanto ela mede.
void tk_prof_note_copy(uint32_t site, uint64_t n);
// tk_prof_dump — despejar as tabelas de sítios UMA vez, no molde do tk_cov_dump: magic, versão,
// digest das declarações, secções. Publicado por tk_rt_rename: nenhum leitor vê meio arquivo.
void tk_prof_dump(const char *path);
```

---

## 9. A sequência de crumbs, e as fixtures

**Ordem obrigatória, e cada degrau é gateável sozinho.**

| crumb | o quê | porque aqui |
|---|---|---|
| **12.0** | **#476 `#arena_depth(N)`** — parse, AST, checker, consumo no emissor (os quatro `regions.len < 64` viram `regions.len < cg_arena_depth_of(f)`) | o afinador não pode afinar um botão que não existe; e a constante mágica passa a ser o ramo padrão da mesma expressão |
| **12.1a** | **tabela de sítios + `tk_region_new_at`** nos cinco sítios de emissão | é a peça de que 12.2, o R3 e o `depth` dependem todos (§5.3) |
| **12.1b** | **#475** — funções/ramos adotam o endereçamento aberto; três tabelas pré-dimensionadas pela caminhada estática; `DumpMark` | independente de 12.1a; fecha o requisito à letra (§4.3) |
| **12.2a** | `teko::profile` **estático** (`walk`) + o `.tkprof` (escrita, `tk_prof_dump`, `rename`) | o denominador antes do numerador |
| **12.2b** | `teko::profile` **dinâmico** (`read`, `suggest`, `render`) + `teko profile` | **#479 fecha aqui** |
| **12.2c** | o `ra` residual: imprimir `ra - dli_fbase` e `dli_fname` no despejo do `TEKO_ARENA_OBS` | um `fprintf`; mede o que o actuador ainda não alcança |
| **12.3** | **#480** — `--pgo <arquivo.tkprof>`, precedência `manual > PGO > omissão`, recusa por `decl_digest` divergente | consome 12.2b |

**Ritual — portão completo em três momentos, e cada um tem uma razão para não ser adiado:**

1. **depois de 12.0**, porque toca o emissor em quatro sítios de aresta de saída de região: um erro
   aqui é uso-depois-de-libertar, não um número errado;
2. **depois de 12.1a**, porque muda a assinatura de uma primitiva de arena em cinco sítios de emissão
   e é a primeira vez que o `.tkprof` existe;
3. **depois de 12.3**, o portão da onda.

E **corte de semente** em `0.3.1.12-beta`, como o plano já fixa. **Regra de carga aditiva:** 12.0
ensina uma diretiva nova ao compilador, logo **`src/` não a adota na mesma carga** — a semente
anterior tem de continuar a construir gen1. As fixtures exercitam-na através do gen1, que a tem.

### 9.1 Fixtures — entradas e códigos de saída nativos

| fixture | o que afirma | saída |
|---|---|---|
| `arena_depth_parsed` | `#arena_depth(3)` numa `fn` compila e corre | **exit 0** |
| `arena_depth_zero_rejected` | `#arena_depth(0)` | `EXPECT_COMPILE_FAIL` |
| `arena_depth_doubled_rejected` | duas `#arena_depth(…)` na mesma declaração | `EXPECT_COMPILE_FAIL` |
| `arena_depth_on_extern_rejected` | `#arena_depth` antes de `extern` (gémeo da recusa de `#arena_size`, `parse_decl.tks:375`) | `EXPECT_COMPILE_FAIL` |
| `arena_depth_flattens` | 5 escopos aninhados com `#arena_depth(2)`; o programa lê o próprio `.tkprof` e sai com a profundidade máxima observada | **exit 2** |
| `arena_depth_default_is_64` | sem diretiva, o achatamento continua no tecto de hoje — o ramo padrão não regrediu | **exit 64** |
| `arena_size_manual_beats_pgo` | `#arena_size(4096)` + `--pgo` cujo relatório diz 65536; o programa lê o `.tkprof` e sai com `chão/1024` | **exit 4** |
| `pgo_applies_when_absent` | a MESMA fonte sem diretiva, o mesmo `--pgo` | **exit 64** |
| `pgo_stale_report_rejected` | `.tkprof` com `decl_digest` de outra árvore | `EXPECT_COMPILE_FAIL` (mensagem nomeia o remédio) |
| `profile_names_the_declaration` | **o teste de fogo**: um `fn` que aloca em ciclo; o relatório nomeia a declaração Teko, não um símbolo de runtime | **exit 0** |
| `profile_refuses_on_unroutable_str` | um `fn` cujos bytes são `str`: sai `size = 0` com `reason` a nomear `tk_str_concat_r` | **exit 0** |
| `profile_suggestion_is_auditable` | `saved_chunks` e `cost_bytes` presentes e a conta fecha | **exit 0** |
| `profile_static_without_run` | `walk` sozinho enumera sítios e **não** produz sugestão | **exit 0** |
| `profile_thin_sample_is_marked` | 3 aberturas ⇒ `Confidence::Thin` | **exit 0** |
| `coverage_disk_signal_no_alloc` | corrida instrumentada com contador paranóico ⇒ **zero** `realloc`/`rehash` de cobertura | **exit 0** |
| `coverage_missing_dump_detected` | um escritor morto antes do despejo: sem `DumpMark`, o sumarizador acusa | **exit 1** |
| `arena_reclaim_ratio_nonzero` | **o oráculo do actuador (§7)**: `reclaim ratio == 0,0 %` **reprova** | **exit 1 quando 0 %** |
| `arena_root_bytes_decrease` | a quarta afirmação do §7 — a raiz desce entre duas medições | **exit 0** |
| `profile_chunk_never_fires` | o `DumpMark` tem 1 byte de corpo: nunca há troço, e a via de troço não é exercida por engano | **exit 0** |

**Três destas não são testes de funcionalidade — são inversões**, e sem elas as outras dizem só *"não
vi problema"*: `arena_reclaim_ratio_nonzero`, `pgo_stale_report_rejected` e
`coverage_missing_dump_detected` **têm de falhar** quando o mecanismo é removido.

---

## 10. O que fica FORA, e o motivo de cada um

| fora | motivo |
|---|---|
| **reescrever `.tks` com a diretiva sugerida** | ruling de 2026-07-13 (`ast.tks:439`): sugerir sim, atribuir não. E um corpus que o build reescreve deixa de ser reproduzível do git |
| **descoberta automática de um `.tkprof` no diretório do projeto** | é o defeito "arquivo da corrida anterior a flutuar" (`journaling` §5.2), e um portão que não gateia. `--pgo` explícito |
| **profiler de TEMPO (amostragem, flamegraphs)** | é outro instrumento: precisa de sinal de temporizador e de caminhada de pilha, e a árvore já regista que `execinfo` não existe em musl (`TK_HAVE_BACKTRACE`). O `#479` pede afinação de arena |
| **`#arena_depth` num bloco ou num método** | `parse_decl_attributes` só corre na posição de declaração de topo; um método nunca a atravessa (`ast.tks:438`). Alargar é gramática nova e nenhuma medição a exige |
| **dimensionar a raiz / o processo** | desde o F1 a raiz é **por tarefa**, e o `isolate` dá a cada uma a sua (`teko-laws-digest`, ruling 2026-07-27). É matéria do SW2, não deste |
| **`-rdynamic`** | **medido estritamente pior** (§5.1): dá um nome sem linha e custa uma religação, quando o offset dá arquivo:linha sem nenhuma |
| **um segundo canal/transporte para dados de arena** | §3: seria a segunda infraestrutura para o mesmo problema, e a instrumentação por alocação custaria ~265 MB de tráfego para medir 1926 MB |
| **encolher `TK_REGION_DEFAULT_CHUNK` com a profundidade** | é o achado adjacente do §2.2 — **REPORTADO**, não convertido em issue por mim. É custo, não correção, e a decisão de o puxar é do dono |
| **defender ou usar o `adopt`** | decisão do dono, tomada como dada (§11.2) |

---

## 11. Riscos e tensões de lei

### 11.1 Tensão de lei — RESOLVIDA law-first: "o compilador nunca infere" × "o PGO atribui"

Resolvida em §2.3, e a resolução passa nas duas leis sem torcer nenhuma: a **diretiva** continua
100 % escrita por uma pessoa; o **PGO** substitui a **omissão**. A precedência que o plano já
escreveu é o enunciado dessa composição, e o artefato separado (`.tkprof` por `--pgo`) é o que a
torna verificável em vez de convencional. **Não HALTO nisto.**

### 11.2 A retirada do `adopt` TEM ordem obrigatória, e é esta

Tomado como dado que sai, e não o defendo. **Mas foi-me pedido para dizer se a retirada tem ordem, e
tem — e trocá-la abre um buraco silencioso.**

`PtAdopterId` (`spine.tks:63`) carrega `region: u32` — o identificador da sub-região léxica — e a
rede de segurança `top: bool`, o `PtAdopter(⊤)`, que a §5.1 da espinha define como *"qualquer
alocação dentro de QUALQUER adotante cuja região precisa exceda o orçamento de uma função"*, e que
o reticulado trata como `PtTop`, isto é, **a fuga segura**.

> **As sub-regiões léxicas têm de existir ANTES de o `adopt` sair.**

Se o `adopt` sair primeiro, o eixo fica sem sítio concreto para nomear e **tudo sobe para `PtTop`** —
que é sólido (a fuga é segura) e por isso **não dá sinal nenhum**. O checker continua a aprovar, a
memória continua a vazar para a raiz, e o profiler perde a atribuição de profundidade que é metade do
seu trabalho. **É a família de defeito que este projeto já pagou várias vezes: correto, silencioso,
e invisível ao ASan.**

Segunda nota de ordem: `emit_adopt` (`codegen.tks:9107`) é hoje **a única abertura de região
incondicional**. Retirá-lo retira essa incondicionalidade — e é exatamente por isso que o `SiteKind`
proposto no §8 não tem membro para ele.

### 11.3 Riscos, cada um com a medição que o produz

| risco | produzido por | mitigação proposta |
|---|---|---|
| **A sugestão de `size` é boa e não muda nada** porque os bytes do sítio são `str` | 66,4 MB em 2,17 M buffers fora da árvore de regiões | a recusa nomeada do §6 — o profiler diz-o em vez de sugerir na mesma |
| **O `depth` troca bytes residentes por agitação do alocador** e ninguém repara | §0.3: com drop a funcionar, o custo migra de moeda | as duas grandezas no relatório, lado a lado; nunca só uma |
| **A tabela de sítios encolhe a granularidade se dois sítios colapsarem** | a mesma armadilha que a §26.5 do journal nomeou para as asserções | a coluna entra na chave; duas aberturas na mesma linha têm sítios diferentes, e há fixture |
| **O `.tkprof` de outra árvore aplicado em silêncio** | o `.tkcov` tem magic `TKCOV1` e **nem versão nem hash** — a guarda nunca existiu ali | `decl_digest` no cabeçalho e recusa com remédio, no molde do `.tkb` (magic + versão + hash + remédio) |
| **A via do `ra` apodrece por só correr na rota C** | zero DWARF em `src/`; `dladdr` é POSIX | é declarada RESIDUAL desde o primeiro dia, com o seu limite escrito no sítio |
| **O quantil p99,9 sobre amostra fina é o máximo disfarçado** | 3,48 M alocações num posto, cauda pesada certa | `Confidence::Thin` marcado na saída |
| **`profile_chunk_never_fires` esconde a via de troço** | o alarme da própria adenda do dono de 07-31: *"código que quase nunca corre apodrece"* | a fixture que **força** o troço vive no lote do journal, não neste — dito para não se pensar que este lote a cobre |

### 11.4 O que NÃO medi, e digo-o em vez de estimar

1. **O custo em tempo de `tk_region_new_at` com a observação LIGADA.** Desligada é um compare
   previsto; ligada é uma escrita numa tabela. Não corri.
2. **O `malloc` de um chunk em tempo.** A conta do quantil de §2.1 conta só a **cauda** de um chunk
   extra (1,32 KB medidos) e ignora a latência do `posix_memalign` — logo `q* = 0,99924` é um
   **limite inferior**; com o tempo contado, o quantil sobe.
3. **Se páginas não tocadas de um chunk sobredimensionado entram no RSS.** No Linux espero que não
   (alocação preguiçosa), no Windows `_aligned_malloc` tem outro regime — **conhecimento, não
   medição**, e por isso o custo de excesso do §2.1 está contado em bytes reservados e não em
   residentes. Se a medição desmentir, o quantil desce e a regra continua a mesma: é a medição que
   fica.
4. **O volume de cópia do R3.** É a primeira coisa que a instrumentação de 12.1a produz, e o limiar de
   10 %/20 sítios do §1.3 é a minha proposta, não um número medido.

---

## 12. Nada aqui HALTA

A pergunta que o plano tinha em aberto foi fechada pelo ruling do dono e pela sua consequência
direta: **o `#476` entra como crumb 12.0.** A única tensão de lei que encontrei (§11.1) resolve-se
pela composição das duas leis, sem escolha de gosto. As decisões que já vinham dadas — a retirada do
`adopt`, a espinha como quem decide — foram tomadas como dadas, e a única coisa que acrescento sobre
elas é uma **ordem obrigatória** (§11.2), que é engenharia e não opinião.

**O que este documento pede a alguém: nada. O que ele entrega: o desenho inteiro do SW12, com os
quatro números que o justificam medidos nesta árvore, e as inversões que o podem desmentir.**
