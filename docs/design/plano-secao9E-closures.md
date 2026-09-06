# Plano — §9 E: tipos de closure `func<…>` / `action<…>` (+ Bloco-B em posição de expressão)

> **Status:** DESIGN. Read+Write(este .md) apenas — nenhum código de produto editado, nenhum build,
> nenhum `teko test .`, nenhum reseed nesta crumb. Este documento É o artefacto.
> **Branch:** `fix/retirement` (onda de migração-de-superfície 0.3.1; drena sequencial, SEM PRs).
> **Fonte de lei:** rulings SELADOS do dono para §9 E ("estilo C#", abaixo, VINCULATIVOS — o plano
> desenha À VOLTA deles, nunca os re-abre). Superfície: `docs/design/mudancas-superficie-0.3.1.md`
> §9.2 (`func<…>`/`action<…>`).
> **Irmãos:** §9 A (sobrecarga, `plano-secao9A-method-overload.md`), §9 B (uniões estruturais
> `A | B` + construção sempre com prefixo de tipo), §6 (aposentar `unsafe` — de onde o Bloco-B em
> posição de expressão foi DIFERIDO).
> **Lei permanente:** Teko-only (.tks), W15+Javadoc-completo em TODA declaração, law-first.

---

## 0. Rulings SELADOS do dono (LEI — desenha-se à volta, não se re-abre)

1. **Dois construtores de tipo-delegate genéricos, subtipos de COMP-TIME** (monomorfizados, SEM objeto
   delegate em runtime): **`func<T1,…,Tn,R>`** — recebe `T1..Tn`, RETORNA `R` (o ÚLTIMO parâmetro de
   tipo é o retorno); **`action<T1,…,Tn>`** — recebe `T1..Tn`, sem retorno. `func<R>` = zero params;
   `action` (nu) = `()` sem retorno. Aridade total ≤ 16.
2. **A closure LITERAL `(params) => expr` / `(params) => { … }` JÁ parseia** (`Lambda`,
   `parse_expr.tks:334`) — sem `fn`, sem `-> R`, retorno inferido; o corpo em bloco é BLOCO-B (o
   valor final do bloco é o resultado, NÃO um `return` estilo C#). MANTÉM-SE.
3. **Remove a anotação de tipo antiga `fn(T): R`** — colide com uniões de retorno (`fn(T): R | null`
   é ambíguo entre `(fn(T):R) | null` e `fn(T): (R|null)`); `func<Record,str> | null` é inequívoco.
4. **Inferência:** os tipos dos parâmetros da closure vêm do tipo-alvo `func`/`action`
   (anotação/parâmetro/retorno). Sem alvo ⇒ anotação obrigatória.
5. **Coerção fn→delegate por FORMA** (params + retorno batem): `var g: func<i32,i32> = triple`.
6. **Captura: estilo C# — captura a VARIÁVEL.** (Investigar mutabilidade de `var`/bindings e como o
   `borrow.tks` trata a captura; sinalizar interação de lifetime/borrow.)
7. **Bloco-B generaliza para posição de EXPRESSÃO FORA de uma closure**
   (`var r = { let a = f(); a + 1 }`) — a peça diferida de §6. NÃO pode colidir com a construção de
   §9 B: sob B, um `{` em posição de expressão é SEMPRE um bloco (a construção tem SEMPRE prefixo de
   tipo), logo o Bloco-B é inequívoco.

---

## 1. Estado de HOJE — o ACHADO CENTRAL: o espinhaço de §9 E JÁ ATERROU

**A maior parte de §9 E já foi implementada e reseedada nesta branch** (commits `bd2f5108` "teach
func/action delegate types", `2f14ad49` "retire surface void and the old (params):R closure form;
final reseed", `b2d73949` merge). Isto muda o desenho: **este plano NÃO reimplementa o espinhaço — ele
(a) documenta o que aterrou, (b) tranca-o por fixtures de regressão que NÃO existem, e (c) desenha a
ÚNICA peça de produto que falta: o Bloco-B em posição de expressão (ruling 7)**, mais a reconciliação
law-first da semântica de captura (ruling 6).

### 1.1 O que JÁ está pronto (verificado por leitura — o implementador confirma, não reescreve)

- **Parser dos tipos-delegate** — completo em `src/parser/parse_type.tks`:
  - `is_delegate_kw_at` (`parse_type.tks:37`) — reconhece `func`/`action` como Idents contextuais em
    posição de tipo (não reservados no lexer), exceto quando seguidos de `::`.
  - `parse_delegate_type` (`parse_type.tks:63`) — `func<R>`, `func<T1,…,Tn,R>`, `func` nu/`func<>`
    INVÁLIDOS; `action`, `action<T1,…,Tn>`, `action<>` INVÁLIDO. Aridade > 16 rejeitada
    (`parse_type.tks:84`). Desugara para o nó AST existente `FunctionType { params; ret }`
    (`ast.tks` — TypeExpr).
  - `delegate_unit_ret` (`parse_type.tks:22`) — o sentinela de retorno-unit `()` que `action` carrega
    no slot `FunctionType.ret`; o checker resolve `()`→`Void`.
  - Despacho em `parse_type_primary` (`parse_type.tks:162`).
  - **A forma antiga `(A,B): R` está RETIRADA** — comentário-lápide em `parse_type.tks:6-10`; um `(`
    em posição de tipo já não é um tipo-função.
- **Checker — resolução** — `resolve_type` mapeia `FunctionType`→`Func` (`resolve.tks:1965`),
  rejeitando parâmetro `void` (M.3) e `ref` (R4); o retorno PODE ser `void`. `type_mangle` já trata
  `Func` (`resolve.tks:2078`).
- **Checker — genéricos/monomorfização** — `FunctionType` é substituído e normalizado nas passagens
  de genéricos: `subst_texpr_names` (`resolve.tks:2423`), `collect_texpr_insts` (`resolve.tks:2527`),
  `normalize_inst_texpr` (`resolve.tks:3057`); reconstrução `Func`→`FunctionType` em mono
  (`monomorph.tks:257`), substituição de `TLambda` (`monomorph.tks:899`). Assim `List<func<i64,str>>`
  e um `func<…>` dentro de um genérico monomorfizam.
- **Typer — closure literal** — `type_lambda` (`typer.tks:223`); inferência de parâmetro a partir do
  alvo (`lam_param_type`, `typer.tks:176`, ruling 4); dois pontos de despacho: SEM alvo
  (`typer.tks:3790`, exige anotação — ruling 4) e COM alvo (`typer.tks:4135`, infere). O tipo do
  `TExpr` é a `Func` da closure (`typer.tks:269`).
- **Coerção fn→delegate (ruling 5) — JÁ FUNCIONA.** Uma referência a um nome de função como valor
  produz um `TVar{is_func}` cujo tipo é `Func` (`typer.tks:3642-3643`). `type_eq` de `Func` compara
  APENAS `params` + `ret` (`type.tks:184-187`), IGNORANDO `param_names`/`variadic`/`defaults`; e
  `widens_into_at` aceita por `type_eq` no topo (`resolve.tks:1243`). Logo `var g: func<i32,i32> =
  triple` (com `fn triple(x: i32): i32`) coage por forma hoje — em atribuição
  (`coerce_value_into`, `typer.tks:4895`), argumento (`coerce_argument_into`, `typer.tks:4923`) e
  retorno. **SEM fixture que o trave.**
- **Codegen** — closures baixam para uma função LEVANTADA + `tk_closure`: C em `codegen.tks:2661`
  (`FunctionType`→`tk_closure`) e `codegen.tks:7699` (literal); nativo/LIR em `lower.tks:5764`. tkb
  serializa `FunctionType` como tag 4 (`tkb_write.tks:320`/`tkb_read.tks:594`/`tkb_frame.tks:197`).
- **Captura** — analisada em `type_lambda` (`typer.tks:251-268`): **por CÓPIA (snapshot) por padrão**,
  `by_ref` SÓ quando o binding capturado é `Reference` (`Ref<T>`, o único caminho a mutação
  partilhada). Escrever num capturado-por-cópia é REJEITADO (`lam_reject_copy_capture_write`,
  `typer.tks:202`; fixture `examples/regressions/diagnostics/src/c60_lambda_write_to_copy_capture_rejected/`).
  Borrow/escape: `resolve_captures_borrow` (`borrow.tks:417`) e a marcação-para-root quando a closure
  escapa (`escape.tks:253`).

### 1.2 Blast radius da remoção de `fn(T): R` — HISTÓRICO, JÁ ZERO

O ruling 3 pede remover a anotação antiga. **Já está removida e reseedada.** Varredura do corpus atual:
- `func<` em `src/**.tks`: **28** ocorrências (o compilador JÁ SE USA a si próprio na nova forma).
- `action<`: **9** ocorrências.
- Literais de closure (`=>`): ~3900 (a maioria são braços de `match`; irrelevante — a forma literal
  nunca mudou, ruling 2).
- Leftovers da forma antiga `: fn(...)` como anotação de tipo: **ZERO** (os únicos casamentos de `fn(`
  são texto de doc `Type<Args>::fn(...)` em `parse_expr.tks:84,115` e uma menção de design-futuro
  `cabi fn()` num comentário em `test/test.tks:84` — nenhum é uma anotação de tipo).

⇒ **O blast radius da remoção já foi absorvido pelo reseed `2f14ad49`.** Nada a remover; o
implementador NÃO toca o parser de tipos. (Se, ao correr o build, aparecer QUALQUER `: fn(...)`
residual, é achado adjacente — REPORTAR para cima, não corrigir aqui.)

---

## 2. O que RESTA de §9 E (o âmbito real deste plano)

1. **Bloco-B em posição de expressão (ruling 7)** — a ÚNICA peça de PRODUTO que falta. Hoje um `{` nu
   em posição de expressão NÃO é aceite: `parse_atom` (`parse_expr.tks:374`) só trata `{` (a)
   como corpo de closure (`parse_lambda`, `parse_expr.tks:362`) e (b) como literal de struct COM
   prefixo de path (`parse_expr.tks:471,482`). Um `{` líder cai no `err_at "expected an expression"`
   (`parse_expr.tks:490`). Ver §3.
2. **Reconciliação da semântica de captura (ruling 6)** — o ruling diz "captura a variável (estilo
   C#)"; o corpus captura por CÓPIA + `ref` para mutação partilhada. Isto é uma TENSÃO de lei — ver
   §5 (achado + resolução recomendada; possível HALT).
3. **Trancar por fixtures** o espinhaço já-aterrado (delegate types, inferência, coerção fn→delegate,
   captura) que HOJE não tem regressões dedicadas — ver §6.

---

## 3. Bloco-B em posição de expressão — o desenho (a peça de produto que falta)

**Definição (ruling 7):** `{ stmt* trailing_expr }` em posição de expressão é um BLOCO léxico inline
que introduz o seu próprio escopo, corre no MESMO frame (NÃO é uma closure, NÃO captura, NÃO aloca),
e cujo VALOR é a expressão final (Bloco-B: o valor final é o resultado — igual ao corpo `{ … }` de uma
closure, `tast.tks:90-95`). Se o bloco divergir ou não tiver valor final não-`void`, é erro em posição
de valor (como um `if` usado como valor precisa de `else`, `typer.tks:3953`).

### 3.1 Não-ambiguidade com §9 B (o ponto crítico do ruling 7)

Sob §9 B a **construção tem SEMPRE prefixo de tipo** (`Name { … }` / `Name<T>{ … }`), gated por
`allow_struct` DEPOIS de um path (`parse_expr.tks:471,482`). Um `{` **líder** (sem path antes) NUNCA é
uma construção. Logo o Bloco-B é inequívoco: se o átomo COMEÇA com `{`, é um Bloco-B. As braces de
corpo de `if`/`match`/`loop`/`fn` NÃO passam por `parse_atom` — são consumidas diretamente por
`parse_if`/`parse_match`/etc. (`parse_expr.tks:436,439`), portanto o novo ramo não as rouba.

**Risco residual (mitigar no desenho):** o scrutinee de `if`/`match` parseia com `allow_struct=false`.
Um `{` líder na POSIÇÃO de scrutinee (`if { … } { … }`) passaria agora a ser um Bloco-B como condição
— sintaxe bizarra mas gramaticalmente possível. RECOMENDAÇÃO: gate o ramo Bloco-B com
`allow_struct` (o mesmo flag que já distingue "aqui um `{` abre um bloco de scrutinee"), OU aceitá-lo
livremente (um Bloco-B como condição é benigno: tipa `bool` ou falha). Preferir gate por `allow_struct`
para paridade com o tratamento de struct-literal e mensagens estáveis. O implementador DEVE confirmar
por fixture que `if x { … } else { … }` e `match x { … }` não regridem.

### 3.2 Decisão de desenho: NÓ DEDICADO, não IIFE

Rejeitar o atalho "desugarar Bloco-B para uma closure imediatamente-invocada `(() => { … })()`":
uma IIFE (a) dispararia `lam_reject_copy_capture_write` sobre CADA escrita a variável envolvente
(`typer.tks:202`) — errado, o Bloco-B corre inline e PODE escrever no frame; (b) capturaria por
cópia — errado, deve LER o escopo envolvente diretamente; (c) pagaria overhead de `tk_closure` em
runtime — contra o espírito "sem objeto em runtime". ⇒ **Nó dedicado** `Block`/`TBlockExpr`, baixado
como uma GNU statement-expression em C (`({ …; valor; })`, idioma JÁ omnipresente no backend —
`codegen.tks:1737,3385,3444,…`) e como "baixa os stmts, cede o vreg do valor final" no nativo/LIR.

### 3.3 Formas a adicionar (Javadoc-completo — o implementador copia verbatim)

**Parser — novo nó AST** em `src/parser/ast.tks` (juntar a `ExprKind`, `ast.tks:258`):

```
/**
 * Block — (§9 E, ruling 7) um Bloco-B em posição de EXPRESSÃO: `{ stmt* trailing_expr }`. Um bloco
 * léxico inline que introduz o seu próprio escopo e corre no MESMO frame (não é uma closure, não
 * captura, não aloca); o seu VALOR é a expressão final (Bloco-B — o valor final é o resultado). Um
 * `{` LÍDER em posição de expressão é sempre um Bloco-B: a construção de §9 B tem sempre prefixo de
 * tipo (`Name { … }`), pelo que um `{` sem path antes nunca é uma construção — inequívoco.
 *
 * @since §9 E
 */
pub type Block = struct { body: []Statement }
```

`body` é produzido por `parse_block` (`parse_stmt.tks:154`) — a MESMA função que já parseia o corpo
`{ … }` de uma closure (`parse_expr.tks:362-364`), portanto zero código de parsing novo.

**Parser — despacho** em `parse_atom` (`parse_expr.tks:374`), como um novo ramo ANTES do
`err_at` final (`parse_expr.tks:490`), gated por `allow_struct` (§3.1):

```
    /**
     * Um `{` LÍDER abre um Bloco-B em posição de expressão (§9 E, ruling 7) — reusa `parse_block`
     * (o mesmo corpo de uma closure `{ … }`). Gated por `allow_struct` para não capturar o `{` de
     * corpo de um scrutinee de `if`/`match`; a construção de §9 B (que tem prefixo de tipo) já foi
     * despachada acima, logo um `{` sem path antes é inequivocamente um bloco.
     */
    if allow_struct && k == lexer::TokenKind::LBrace {
        var blk = match parse_block(tokens, pos) { ParsedList<Statement> as x => x; error as e => return e }
        return Parsed<Expr> { node = Expr { kind = Block { body = blk.items }; line = tokens[pos].line; col = tokens[pos].col }; next = blk.next }
    }
```

**Checker — novo nó TAST** em `src/checker/tast.tks` (juntar a `TExprKind`, `tast.tks:108`):

```
/**
 * TBlockExpr — (§9 E) um Bloco-B tipado em posição de valor. `body` é o bloco de statements tipado;
 * o `.type` do `TExpr` envolvente é o tipo do valor final (`tblock_type`, `typer.tks:3811`). Corre
 * inline no frame envolvente — sem captura, sem lift, sem `tk_closure` (contraste com `TLambda`).
 *
 * @since §9 E
 */
pub type TBlockExpr = struct { body: []TStatement }
```

**Checker — tipagem** — nova fn em `typer.tks`, despachada de `type_expr` no ramo `parser::Block`
(reusa `type_block`/`tblock_type` que JÁ existem, `typer.tks:3904`/`:3811`):

```
/**
 * type_block_expr — tipa um Bloco-B em posição de valor (§9 E, ruling 7). O corpo é tipado num
 * escopo-filho (`type_block`) — as suas ligações não vazam; o VALOR do bloco é o tipo do seu valor
 * final (`tblock_type`). Um bloco que diverge ou cujo valor final é `void` NÃO pode ocupar posição
 * de valor — erro honesto (o mesmo contrato que `type_if` impõe a um `if`-como-valor sem `else`).
 *
 * @param blk    o Bloco-B parseado
 * @param env    o ambiente de tipos envolvente (o bloco lê-o diretamente — sem captura)
 * @param table  a tabela de tipos dobrada
 * @return       o `TExpr` do bloco (kind `TBlockExpr`, tipo = valor final), ou um erro localizado
 * @throws       quando o bloco diverge ou o seu valor final é `void` em posição de valor
 * @since §9 E
 */
fn type_block_expr(blk: parser::Block, env: Env, table: TypeTable): TExpr | error
```

**Backends + passagens (o padrão "TLambda foi adicionado em todo o lado"):** um novo TExprKind exige um
braço em CADA sítio que faz `match` sobre `TExprKind`. Template = grep por `TLambda` (**16 ficheiros**).
Sítios a cobrir (cada um ganha um braço `TBlockExpr`):
- `typer.tks` (despacho `type_expr`), `revalidate.tks:148`, `initanalysis.tks:119`,
  `consteval.tks:200`, `consteval_form.tks:43`, `comptime_fold.tks:2134`/`:3079`,
  `warnings.tks:233`, `monomorph.tks:899` (substituir tipos + mono do corpo),
  `escape.tks:253`/`:827` (o corpo CORRE inline — walk normal do bloco, NÃO "lifted"; diferente de
  `TLambda`), `borrow.tks:417` (idem — resolve o bloco inline).
- Codegen C: `emit_expr` — baixar como `({ <stmts>; <valor_final>; })` (GNU stmt-expr).
- Nativo/LIR `lower.tks`: baixar os stmts, ceder o vreg do valor final.
- tkb ser/de: `tkb_write.tks`, `tkb_read.tks`, `tkb_frame.tks` — novo tag de ExprKind.
- `lsp/symbols.tks` se enumerar ExprKind.

**Nota de escopo/frame (achado importante — contraste com TLambda):** nas passagens `escape.tks` e
`borrow.tks`, o `TLambda` é tratado como uma função LEVANTADA (o corpo NÃO é walked no frame
corrente — `escape.tks:252` "the body belongs to the LIFTED function"). O `TBlockExpr` é o OPOSTO:
o corpo corre NO frame corrente, logo DEVE ser walked normalmente (as suas leituras/escritas são deste
frame; um valor que escapa do bloco escapa do frame). O implementador NÃO deve copiar o braço de
`TLambda` nessas duas passagens — deve tratá-lo como um bloco de statements inline.

---

## 4. Coerção fn→delegate (ruling 5) — já funciona; trancar, não construir

Como em §1.1, `var g: func<i32,i32> = triple` já coage por forma via `type_eq`(`Func`) que ignora
`param_names`/`variadic` (`type.tks:184-187`) + `widens_into_at` (`resolve.tks:1243`). **Ação: só
fixtures** (§6.1 F3). Duas subtilezas para o implementador CONFIRMAR (não alterar sem ruling novo):
- A coerção é por FORMA EXATA (`type_eq`), não por variância. `func<i64,i32>` NÃO aceita
  `fn f(x: i32): i32` (params diferem). Isto é o "params + return match" do ruling 5 — correto.
- Uma fn com DEFAULTS/nomes coage à mesma na delegate (a delegate perde os nomes — o `Func`-alvo tem
  `param_names=[]`). `type_eq` já ignora nomes, logo coage; mas a delegate resultante NÃO pode ser
  chamada por nome (`typer.tks:1477` já rejeita named-call sobre um valor-closure). Documentar num
  fixture ACEITAR (chamada posicional) — sem mudança de código.

---

## 5. Semântica de captura (ruling 6) — ACHADO + tensão de lei

**Ruling 6 literal:** "captura a VARIÁVEL (estilo C#)". **Estado do corpus:** captura por **CÓPIA
(snapshot)** por padrão; `by_ref` só para bindings `Reference`; escrever num capturado-por-cópia é
REJEITADO (`typer.tks:202`, com fixture c60). Estas são semânticas **DIFERENTES**:

- **C# "captura a variável"** = a variável local capturada é PROMOVIDA a uma célula de heap
  partilhada entre o frame envolvente e a closure; mutações de qualquer lado são VISÍVEIS no outro; a
  célula sobrevive enquanto a closure viver.
- **Teko hoje** = a closure recebe uma CÓPIA do valor no momento da criação; mutar o original depois
  NÃO afeta a closure e vice-versa; para partilha real, passa-se um `ref` (uma `Reference`, único
  caminho a aliasing mutável no modelo de memória).

**Porque teko diverge (law-first):** a captura-por-célula-partilhada do C# exige aliasing mutável
implícito e prolongamento de lifetime (a célula sobrevive ao frame). Isso colide com leis-constituição
de teko: **move-on-return** (o dono do valor move; não há duas referências mutáveis vivas por acaso) e
**R4 "uma ref não pode escapar/ser armazenada"** (`resolve.tks:1936,1973`). O binding `var`/`let` de
teko é uma LIGAÇÃO a um valor, não uma célula endereçável partilhável — não há "a variável" à moda C#
para capturar sem a promover a heap (o que o modelo de memória por-escopo não faz implicitamente).

**Interação de lifetime/borrow (sinalizada, como o ruling pede):** quando uma closure com captura
`by_ref` ESCAPA, `escape.tks:253-259` marca os capturados para alocarem na região raiz
(sobre-aproximação sã — nunca UAF, só menos DCE). Uma captura `by_ref` cujo referente é reclamado
antes da closure seria um UAF; hoje isso é evitado porque (a) a única forma de captura mutável é um
`ref` explícito e (b) o escape-analysis promove a raiz. Uma captura-a-variável estilo C# TERIA de
introduzir boxing (célula de heap) OU um borrow-checker de lifetime da closure — nenhum existe hoje.

**Resolução recomendada (law-first):** ratificar a semântica JÁ ATERRADA como a realização
law-first do ruling 6 — **captura por cópia + `ref` para mutação partilhada** — e ler "estilo C#" como
"a superfície é a de C# (a closure fecha sobre nomes do escopo), não a semântica de célula-partilhada
implícita, que colide com move-on-return/R4". O parêntese do próprio ruling ("investigar mutabilidade
de `var`/bindings … sinalizar interação de lifetime/borrow") indica que o dono ANTECIPOU esta tensão e
pediu o achado — não uma promessa de célula-partilhada.

**HALT condicional (§10):** se o dono QUISER a semântica literal C# de célula-partilhada, isso é uma
mudança de MODELO DE MEMÓRIA (boxing de locais capturados-e-mutados OU lifetime da closure), fora do
âmbito de superfície de §9 E e em tensão com leis-constituição — precisa de ratificação explícita.
Recomendo NÃO abrir isso aqui; entrego o achado e o desenho sobre a semântica cópia+ref.

---

## 6. Fixtures de regressão

Layout (confirmado, igual a §9 A): cada regressão ACEITAR é um projeto
`examples/regressions/<nome>/` (`.tkp`/`.tkr`/`main.tks`/`src/`) com `Then stdout pattern = "…"` ou
`Then exit = N`; cada REJEITAR é um `src/<caso>/case.tks` no canal partilhado
`examples/regressions/diagnostics/` + um Scenario com `Then diagnostic = "<substring>"`. Descoberta por
diretório (os scripts iteram `examples/`).

### 6.1 ACEITAR — `examples/regressions/closure_delegates/` (novo projeto)

Codificar em ARITMÉTICA qual caminho correu (padrão das fixtures existentes), para um relapso MOVER o
valor em vez de passar em silêncio:

- **F1 — `func<…>` como anotação + inferência de parâmetro (rulings 1,4).**
  `var dobro: func<i32,i32> = (x) => x * 2` (o `x` infere `i32` do alvo, sem anotação). `dobro(21)` → 42.
- **F2 — `action<…>` sem retorno (ruling 1).** `var log: action<i32> = (n) => { println(n) }`;
  invocar com um valor; a saída confirma que corre e não devolve valor.
- **F3 — coerção fn→delegate por forma (ruling 5).** `fn triple(x: i32): i32 { x * 3 }`;
  `var g: func<i32,i32> = triple`; `g(14)` → 42. (Trava a coerção por `type_eq`, §4.)
- **F4 — `func<R>` zero-params e `func` dentro de genérico (ruling 1).**
  `var seven: func<i32> = () => 7`; e um campo/parâmetro `func<i64,str>` dentro de um `struct`/genérico
  para exercitar a monomorfização (`monomorph.tks:257`).
- **F5 — captura POR CÓPIA (ruling 6, semântica ratificada).** `mut n = 10; let f = () => n + 1; n = 99;`
  então `f()` → 11 (a cópia — snapshot — NÃO vê o `n = 99`). Trava a semântica de captura decidida.
- **F6 — captura POR REF (mutação partilhada via `ref`).** `mut n = 10; let f = (ref r: i32) => { r = 5 };`
  chamar `f(&n)`; `n` passa a 5. (O único caminho a mutação partilhada — contraste com F5.)
- **F7 — Bloco-B em posição de expressão (ruling 7).**
  `var r = { let a = 20; let b = 22; a + b }` → `r == 42`. E um Bloco-B que LÊ e ESCREVE uma var
  envolvente (`mut acc = 0; var r = { acc = acc + 42; acc }`) → `r == 42` e `acc == 42` (prova que
  corre inline no frame, NÃO como closure — não seria rejeitado nem copiado).
- **F8 — Bloco-B NÃO colide com construção §9 B.** No MESMO ficheiro: um `Point { x = 1; y = 2 }`
  (construção, com prefixo) e um `var q = { 1 + 1 }` (Bloco-B, `{` líder) — ambos compilam; a
  aritmética distingue. Prova a não-ambiguidade do ruling 7.

Expectativa `.tkr`: um `Then stdout pattern`/`Then exit` que só a combinação correta produz (somar os
casos num único número — p.ex. exit code — é o padrão anti-silêncio).

### 6.2 REJEITAR — dobrar no canal `examples/regressions/diagnostics/`

Cada caso um `src/<caso>/case.tks` + Scenario `Then diagnostic = "<substring>"`:

- **R1 — closure sem alvo e sem anotação (ruling 4).** `let f = (x) => x + 1` sem anotação → diagnostic
  "cannot infer the type of closure parameter" (`typer.tks:180`).
- **R2 — escrita a capturado-por-cópia (semântica de captura).** JÁ EXISTE
  (`diagnostics/src/c60_lambda_write_to_copy_capture_rejected/`) — CONFIRMAR que continua verde; não
  duplicar.
- **R3 — `func<>`/`action<>`/`func` nu inválidos (ruling 1).** `var x: func<>` → "func<> is empty";
  `var y: action<>` → "action<> is empty"; `var z: func` → "func needs a return type"
  (`parse_type.tks:69,88,91`). (Trava as mensagens do parser já-aterrado.)
- **R4 — aridade > 16 (ruling 1).** Uma `func<…>` com 17 args → "at most 16 type arguments"
  (`parse_type.tks:85`).
- **R5 — parâmetro `void`/`ref` num tipo-função (M.3/R4).** `var p: func<void, i32>` /
  `var q: func<ref i32, i32>` → as mensagens de `resolve.tks:1972,1973`.
- **R6 — Bloco-B em posição de valor que diverge / valor final `void` (ruling 7).**
  `var r = { println("x") }` (valor final `void`) → erro "block used as a value" (a mensagem que
  `type_block_expr` emitir; espelhar a de `if`-sem-`else`, `typer.tks:3953`).

**Nota:** R3/R4/R5 travam superfície JÁ ATERRADA que hoje não tem regressão — o valor é impedir
regressão silenciosa num futuro refactor do parser de tipos.

---

## 7. Sequência de crumbs (ordenada; cada uma gate-ável isoladamente)

O espinhaço (crumbs "0") já aterrou; a sequência abaixo é o que RESTA. Pontos de RITUAL (gate completo)
marcados. Regra de seed: o Bloco-B introduz um nó AST NOVO ⇒ o tkb ganha um tag novo; o seed anterior
não o conhece, MAS o **código-fonte do compilador NÃO deve USAR a sintaxe Bloco-B até depois do
reseed** — assim o seed nunca precisa de parsear o nó novo, e o fixpoint fica seguro (nó aditivo).

1. **Fixtures de trava do espinhaço já-aterrado** (F1–F6, R1, R3–R5; confirmar R2/c60). PRIMEIRO —
   estabelece a rede de segurança ANTES de qualquer código novo, e documenta por execução o que
   aterrou. Nenhum código de produto. **RITUAL: gate completo** (prova que o espinhaço está verde
   ponta-a-ponta e que a coerção/inferência/captura fazem o que o plano afirma).
2. **Nó AST `Block` + despacho em `parse_atom`** (`parse_expr.tks`, gated por `allow_struct`, §3.3).
   Só parser; o checker ainda não conhece `parser::Block` ⇒ um braço honest-stop temporário em
   `type_expr` ("Bloco-B ainda não tipado") mantém a árvore a compilar. — *o compilador-fonte não usa
   Bloco-B ⇒ inerte.*
3. **tkb ser/de do nó `Block`** (`tkb_write`/`tkb_read`/`tkb_frame`, novo tag). — *inerte (nada emite
   um `Block` ainda).* **RITUAL: gate completo** (toca o formato serializado; a paridade de
   round-trip é a rede).
4. **`TBlockExpr` (TAST) + `type_block_expr` + fio no `type_expr`** (§3.3). Substitui o honest-stop da
   crumb 2. Reusa `type_block`/`tblock_type`. Adiciona os braços `TBlockExpr` nas passagens de checker
   (`revalidate`, `initanalysis`, `consteval`, `consteval_form`, `comptime_fold`, `warnings`,
   `monomorph`, `escape`, `borrow` — tratando-o como bloco INLINE, NÃO como lifted, §3.3 nota).
   **RITUAL: gate completo** (novo nó TAST central; garantir TODOS os `match TExprKind` cobertos).
5. **Codegen C** — `emit_expr` baixa `TBlockExpr` como GNU stmt-expr `({ …; valor; })`.
6. **Codegen nativo/LIR** — `lower.tks` baixa os stmts + cede o valor final; confirmar paridade
   C↔nativo. **RITUAL: gate completo** (é a crumb que pode divergir entre backends).
7. **Fixtures Bloco-B** (F7, F8, R6). PRIMEIRA crumb a USAR a sintaxe Bloco-B — só no CORPUS DE TESTE,
   nunca no compilador-fonte (regra de seed). **RITUAL.**
8. **Reseed + PROVENANCE** (crumb final, §8).

---

## 8. Ritual de reseed + PROVENANCE (crumb final)

Só depois de todas as crumbs verdes e do gate completo:

1. `cc -std=c2x -w -O2 -I src/runtime -I src/assert bootstrap/teko.c src/runtime/teko_rt.c
   src/assert/assert.c -lm -o gen0`
2. `TEKO_BACKEND=c ./gen0 build . --no-verify --release` → `bin-a`.
3. Re-build com `bin-a` como seed → `bin-b`. **Fixpoint: `bin-a == bin-b`** byte-a-byte
   (`scripts/fixpoint_gate.sh`). O nó `Block` é aditivo e o compilador-fonte NÃO usa a sintaxe ⇒ o
   fixpoint fecha na crumb 6; a crumb 7 introduz Bloco-B só nas FIXTURES (corpus de teste), não no
   compilador, logo não afeta o auto-tkb do próprio compilador.
4. Harvest: `bootstrap/teko.c` (novo seed) + `bootstrap/PROVENANCE` (novo hash/proveniência).
5. NUNCA correr `teko test .` (fuga de memória). Gate por `--no-verify` + os `scripts/*.sh`.

---

## 9. Riscos + tensões de lei (com resolução recomendada)

- **Tensão de captura (ruling 6 literal vs. modelo de memória).** É a ÚNICA tensão genuína. Resolução
  recomendada: ratificar cópia+`ref` como a realização law-first (§5). HALT condicional se o dono
  exigir célula-partilhada C# (mudança de modelo de memória, fora do âmbito de superfície).
- **Bloco-B × scrutinee (`allow_struct`).** Neutralizado gateando o ramo por `allow_struct` (§3.1);
  cobrir `if`/`match` por fixture de não-regressão. Sem tensão de lei.
- **Bloco-B nas passagens escape/borrow (inline vs lifted).** Risco de copiar o braço de `TLambda`
  (que é lifted) e tratar mal o Bloco-B (que é inline) — UAF-em-potencial ou DCE errada. Mitigação:
  §3.3 nota explícita — walk o corpo como bloco inline do frame corrente. Cobrir por F7 (o caso
  ler/escrever var envolvente). Sem tensão de lei.
- **Fixpoint (nó AST novo).** Neutralizado pela regra de seed (compilador-fonte não usa Bloco-B até
  pós-reseed; §7). Firme, não opcional.
- **Coerção fn→delegate por variância.** É EXATA (`type_eq`), não variante — casa com "params +
  return match" (ruling 5). Se um dia se quiser variância (contravariância de params), é afinação
  LOCAL de `widens_into_at`, fora de âmbito. Sem tensão.

---

## 10. Perguntas ao dono/integrador (uma é potencial HALT; adiantei tudo o que não depende delas)

1. **[POTENCIAL HALT] Semântica de captura (ruling 6).** O corpus captura por CÓPIA + `ref` para
   mutação partilhada; C# literal captura a VARIÁVEL (célula de heap partilhada). A segunda colide com
   move-on-return/R4 e exigiria boxing de locais ou lifetime de closure — mudança de MODELO DE
   MEMÓRIA. **Recomendo ratificar cópia+`ref`** como a leitura law-first de "estilo C#" (a superfície
   é C#, a semântica respeita as leis de teko). Confirmar; se o dono exigir célula-partilhada,
   isso é um item de modelo-de-memória separado, não de superfície §9 E. **Todo o resto do plano é
   independente desta resposta** (crumbs 2–8 e as fixtures F1–F4, F7–F8, R1, R3–R6 não dependem dela;
   só F5/F6 fixam a semântica ratificada e mudariam de expectativa se o dono decidir célula-partilhada).
2. **Gate do Bloco-B por `allow_struct`** (§3.1) — recomendo gatear (paridade com struct-literal,
   mensagens estáveis). Afinação local, não bloqueia as outras crumbs.
3. **Mensagem exata de "Bloco-B em posição de valor sem valor" (R6)** — adiantei "espelhar a de
   `if`-sem-`else`"; o dono pode fixar a grafia. Não bloqueia.
