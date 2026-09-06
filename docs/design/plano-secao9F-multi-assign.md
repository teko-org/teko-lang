# Plano — §9 F: atribuição múltipla, decomposição de array, retorno múltiplo

> **Status:** DESIGN. Read+design apenas — nenhum código de produto editado, nenhum build, nenhum
> reseed, nunca `teko test .`. Este documento É o artefacto; o único commit desta crumb é ele próprio.
> **Branch:** `fix/retirement` (onda de migração-de-superfície 0.3.1; drena sequencial, SEM PRs).
> **Fonte de lei:** rulings SELADOS do dono para §9 F (abaixo, VINCULATIVOS — o plano desenha À
> VOLTA deles, nunca os re-abre). **NUNCA existe um tipo tuplo** (`( )` só na assinatura e no sítio de
> binding; nunca um valor de primeira classe).
> **Modelo:** `docs/design/plano-secao9A-method-overload.md` (mesma estrutura, mesmos rituais).
> **Lei permanente:** Teko-only (.tks), W15+Javadoc-completo em TODA declaração, law-first, reseed
> `cc -std=c2x` + `--no-verify`.

---

## 0. Rulings SELADOS do dono (LEI — desenha-se à volta, não se re-abre)

1. **Atribuição múltipla (paralela).** `var a, b, c = e1, e2, e3` — liga posicionalmente, tipos
   inferidos POR POSIÇÃO (cada alvo toma o tipo do seu valor). `var a, b: T = e1, e2` — UMA anotação
   de tipo partilhada por TODOS os alvos.
2. **Decomposição de array.** `var [a, b, c] = arr` — desempacota um `[]T` REAL em ligações
   posicionais. Saltar um: `var [a, _, c] = x` (`_`). ADENDO (dono): saltar um INTERVALO:
   `var [a, …, b, c] = x` — o `…` salta o meio (0+ elementos), liga cabeça + cauda.
3. **Retorno múltiplo (NÃO é um tipo tuplo).** `fn div(a: i32, b: i32): (i32, i32) { return (a/b, a%b) }`
   — os `( )` aparecem SÓ na assinatura e no sítio de binding (`var q, r = div(17,5)`), NUNCA como
   valor de primeira classe (`var t = div(…)` NÃO existe). Sem tipo tuplo no sistema de tipos.
4. **O destructure de campo por NOME `{ x; y }` (B.13, `DestructurePattern`) FICA.** Intocado.
5. **Semântica de comprimento.** SEM `…`, comprimento EXATO exigido (panic em runtime se diferir);
   COM `…`, comprimento ≥ à contagem fixa (panic se menos). Guarda de RUNTIME (o comprimento do slice
   é de runtime).
6. **Base de §10.3 await.** O retorno múltiplo é a base do `await` (mata `when_all`/`when_any`) — mas
   isso é §10; aqui só se constrói a SUPERFÍCIE.

---

## 1. Estado de HOJE — os pontos de contacto (todos verificados por leitura, com file:line)

### 1.1 Parser — binding
- **Dispatcher de statement:** `parse_statement` (`src/parser/parse_stmt.tks:7`) → `is_binding_head`
  (`parse_stmt.tks:329`) → `parse_binding` (`parse_stmt.tks:250`).
- **Cabeça `var` contextual:** `is_var_binding_head` (`parse_stmt.tks:312`) reconhece HOJE só
  `var {…}` (destructure de campo) e `var NAME :`/`var NAME =`. **NÃO reconhece** `var a, b` (após o
  nome vem `,`, não `:`/`=`) nem `var [a,b]` (após `var` vem `[`). Estes DOIS lookaheads faltam.
- **Alvo:** `parse_bind_target` (`parse_stmt.tks:362`) devolve `SimpleName` ou, para `{`,
  `DestructurePattern` (reusa `parse_field_names`, `src/parser/parse_pattern.tks:120`). Rejeita `_`
  como alvo (`parse_stmt.tks:367`).
- **RHS:** `parse_binding` (`parse_stmt.tks:263`) parseia UM `parse_expr`. Não há lista de valores.
- **AST:** `BindTarget = variant SimpleName | DestructurePattern` (`src/parser/ast.tks:265`);
  `Binding` (`ast.tks:285`) tem `target: BindTarget` + `value: Expr` (um só).
- **Statement variant:** `pub type Statement = variant Binding | Assign | Return | … | BlockStmt`
  (`ast.tks:373`) — 10 casos. Consumidores de `parser::Binding as` no SOURCE: só **3** ocorrências
  em 2 ficheiros (`src/checker/resolve.tks`, `src/checker/typer.tks`) — superfície SOURCE minúscula.
- **Grouping `( )`:** `parse_expr` trata `( e )` como grouping TRANSPARENTE de UM expr
  (`src/parser/parse_expr.tks:422-427`); `( a, b )` FALHA hoje (após `a` espera `)`, vê `,`). ⇒ os
  parênteses de tuplo não colidem com nenhuma sintaxe existente.

### 1.2 Parser — assinatura / retorno
- **Retorno da assinatura:** `parse_function` (`src/parser/parse_decl.tks:320`) — em
  `is_return_type_intro_at` (`parse_decl.tks:295`, `:`), parseia UM `parse_type`
  (`parse_decl.tks:401`), grava `has_return`+`return_type: TypeExpr` (`ast.tks:407-408`).
- **`TypeExpr`:** `variant NamedType | SliceType | UnionType | FunctionType` (`src/parser/type.tks:10`)
  — SEM tuplo, e assim FICA (ruling 3: os `( )` não entram na gramática geral de tipo).
- **`return`:** `parse_statement` (`parse_stmt.tks:12-19`) parseia UM `parse_expr`; nó
  `Return { has_value; value: Expr }` (`ast.tks:327`).

### 1.3 Checker
- **Binding:** `type_binding` (`src/checker/typer.tks:4436`) — tipa o valor
  (`type_binding_value`, `typer.tks:4374`), define no env SÓ para `SimpleName`
  (`typer.tks:4463`); `DestructurePattern` devolve o env INALTERADO (refinamento,
  `typer.tks:4464`). Nó `TBinding { kind; target; bound; value }` (`src/checker/tast.tks:119`).
  `_` no valor tipa e é descartado no codegen (não define binding).
- **Retorno:** `type_return` (`typer.tks:4638`) tipa UM valor; `check_returns`/`check_return_stmt`
  (`typer.tks:4980`, `:5386`) casam CADA `return e` contra `ret: Type` único.
- **`Func`:** `pub type Func = struct { params; ret: Type; variadic; param_names; n_required; defaults }`
  (`src/checker/type.tks:101`) — `ret` é UM `Type`. `Type` variant NÃO tem tuplo
  (`type.tks:147`) e assim FICA.
- **`_` wildcard:** token `Underscore` (`src/lexer/token.tks:9`); `..`/spread = `DotDot`
  (`token.tks:146`). **NÃO existe token `…` unicode** — ver Decisão D1 (§10).

### 1.4 Codegen / LIR (os DOIS backends em paridade)
- **Binding C:** `emit_binding` (`src/codegen/codegen.tks:8568`) — só `SimpleName`;
  `DestructurePattern` é honest-stop "destructuring binding not yet supported" (`codegen.tks:8571`).
  `_` = `(void)(expr);` sem variável C (`codegen.tks:8575`).
- **Binding nativo:** `bind_target_name` (`src/lir/lower.tks:6991`) honest-stops o destructure
  (`lower.tks:6994`); `lower_binding` (`lower.tks:6267`).
- **Retorno C:** `emit_return` (`codegen.tks:8919`) — `return <um valor>` via `emit_as_r`
  (`codegen.tks:8952`).
- **Retorno nativo:** `lower_return` (`lower.tks:7033`) + `lower_return_fat` (`lower.tks:7074`) — o
  ramo "fat" JÁ trata retornos multi-palavra (struct por valor).
- **PRECEDENTE-CHAVE (retorno de dois valores sem tuplo):** `TThunkEmit`
  (`codegen.tks:87`, doc `codegen.tks:81-86`) — *"A named struct is the seed-compatible way to return
  two values … the seed rejects bare tuple return types"*. É EXATAMENTE o idioma que o retorno
  múltiplo reusa (ver §4).

---

## 2. A DECISÃO de arquitetura (a espinha do plano)

Três formas de superfície, UMA estratégia de baixo custo/fixpoint-seguro: **desugar no CHECKER para
nós TIPADOS que JÁ existem** (`TBinding`, `TBlockStmt`, índice, field-access, `if`+`panic`), de modo
que **codegen e LIR NÃO mudam** para a atribuição-múltipla e a decomposição-de-array. O ÚNICO sítio
que precisa de codegen novo é o LADO-FUNÇÃO do retorno múltiplo (assinatura + `return (…)` + o
agregado-por-valor sintetizado). O lado-BINDING do retorno múltiplo também é desugar.

Consequência de fixpoint (o argumento central, §6): tudo é ADITIVO. Nenhuma fonte atual do compilador
usa `var a,b=…`, `var [a]=…` nem `(A,B)`; portanto o corpus-seed reproduz-se byte-a-byte enquanto os
novos caminhos ficarem inertes (código morto compilável) até as fixtures os exercitarem.

### 2.1 Superfície de AST nova (parser) — MÍNIMA

Um ÚNICO novo caso de `Statement` (não se toca em `Binding`, que fica byte-idêntico — fixpoint), mais
os alvos posicionais. Nada de tuplo em `TypeExpr` nem em `Type`.

```
/**
 * BindElem — um alvo POSICIONAL numa forma múltipla de binding (§9 F): um nome, o descarte `_`, ou o
 * marcador de intervalo saltado `…`/`..` da decomposição de array (ruling 2 adendo). NUNCA um tuplo:
 * é uma folha de uma LISTA de alvos, jamais um valor.
 *
 * @field BindName  liga o valor da sua posição ao nome (`a`).
 * @field BindSkip  descarta a posição sem ligar nome (`_`); nenhuma variável emitida.
 * @field BindRest  o `…`/`..` de `[a, …, b, c]`: salta 0+ elementos do MEIO; legal SÓ numa
 *                  `ArrayPattern`, no máximo UMA vez, nunca numa `NameList` nem num retorno múltiplo.
 * @since §9 F
 */
pub type BindName = struct { name: str }
pub type BindSkip = struct { }
pub type BindRest = struct { }
pub type BindElem = variant BindName | BindSkip | BindRest

/**
 * MultiTargetKind — que forma múltipla o binding declara, o que fixa como o RHS é interpretado:
 *
 * @field Parallel   `var a, b, c = …` — alvos separados por vírgula; o RHS é OU uma lista de N exprs
 *                   (paralela, ruling 1) OU um único call de retorno múltiplo (ruling 3) destruído.
 * @field ArrayDecomp `var [a, b, c] = arr` — alvos entre `[ ]`; o RHS é UM `[]T` desempacotado por
 *                    posição (ruling 2). Admite um `BindRest` (`…`).
 * @since §9 F
 */
pub type MultiTargetKind = enum { Parallel; ArrayDecomp }

/**
 * MultiBind — o statement de binding MÚLTIPLO `var|const TARGETS [: T] = VALUES` (§9 F). Deliberadamente
 * SEPARADO de `Binding` (que fica byte-idêntico — o binding-simples não ganha campos, o único caminho
 * que o fixpoint do compilador exercita): assim toda a superfície nova é um caso `Statement` novo, e o
 * checker desugara-o para nós TIPADOS já existentes, deixando codegen/LIR intocados.
 *
 * @field kind     a mutabilidade (`Const`/`Mut`); `let`/`mut` já estão retirados a montante.
 * @field shape    `Parallel` (`a, b`) ou `ArrayDecomp` (`[a, b]`).
 * @field targets  os alvos posicionais, em ordem de fonte (`BindName`/`BindSkip`/`BindRest`).
 * @field has_type true iff havia UMA anotação `: T` partilhada (ruling 1).
 * @field type_ann a anotação partilhada, significativa só quando `has_type`.
 * @field values   `Parallel`: N exprs (paralelo) OU 1 expr (call de retorno múltiplo); `ArrayDecomp`:
 *                 exatamente 1 expr (o `[]T`).
 * @field line     linha 1-based do primeiro alvo (diagnósticos W-loc-2).
 * @field col      coluna 1-based do primeiro alvo.
 * @since §9 F
 */
pub type MultiBind = struct {
    kind: BindKind
    shape: MultiTargetKind
    targets: []BindElem
    has_type: bool
    type_ann: TypeExpr
    values: []Expr
    line: u32
    col: u32
}
```

`Statement` passa a `variant Binding | Assign | Return | LoopStmt | BreakStmt | ContinueStmt | ExprStmt
| DeferStmt | AdoptStmt | BlockStmt | MultiBind` (`ast.tks:373`). Todo `match` EXAUSTIVO sobre
`Statement` (poucos — a maioria usa `_ =>`) ganha um arm; ver §8 crumb 2 para a lista de contas.

O **retorno múltiplo** na ASSINATURA e no `return`:

```
/**
 * Function.ret_types — a lista de tipos de retorno de um `fn f(): (A, B, …)` (§9 F, ruling 3). VAZIA
 * para uma fn de retorno-único (usa-se `return_type`), garantindo fixpoint: cada `Function { … }`
 * existente acrescenta `ret_types = teko::list::empty()` e nada muda. Os `( )` são parseados SÓ nesta
 * posição de assinatura (`parse_function`), pelo que a multi-valoração NUNCA entra em `TypeExpr` —
 * não há tuplo de primeira classe.
 * @since §9 F
 */
ret_types: []TypeExpr   // novo campo em Function (ast.tks:402)

/**
 * Return.values — os valores de um `return (e1, e2, …)` múltiplo (§9 F). Comprimento ≤ 1 ⇒ retorno
 * simples (usa `value`/`has_value`, byte-idêntico); ≥ 2 ⇒ retorno múltiplo, casado posicionalmente
 * contra `Function.ret_types`. O `( )` de retorno é reconhecido SÓ na posição de `return` (nunca um
 * expr `(a,b)` autónomo, que continua a ser um erro de parse), honrando ruling 3.
 * @since §9 F
 */
values: []Expr          // novo campo em Return (ast.tks:327)
```

### 2.2 Superfície de checker nova (tipo) — o retorno múltiplo, SEM tuplo

`Func` ganha UM campo de aridade de retorno; `Type` NÃO ganha variante (ruling 3):

```
/**
 * Func.ret_extra — os tipos de retorno ADICIONAIS (posições 2..N) de um retorno múltiplo (§9 F,
 * ruling 3). VAZIO para a fn de retorno-único: `ret` continua a ser o (único) tipo de retorno e cada
 * `Func { … }` existente só acrescenta `ret_extra = teko::list::empty()` (fixpoint). A aridade total
 * de retorno é `1 + ret_extra.len` quando `ret_extra` é não-vazio. NÃO existe um `Type` tuplo: a
 * multi-valoração vive como uma LISTA na assinatura da função, jamais como um valor tipável.
 * @since §9 F
 */
ret_extra: []Type       // novo campo em Func (type.tks:101)
```

Um call de fn de retorno múltiplo tipa-se num `TCall` marcado como multi-valorado (novo
`TCall.ret_extra: []Type`, `tast.tks`); o seu `.type` é o PRIMEIRO retorno mas o nó carrega os
extra. `type_expr` REJEITA um `TCall` multi-valorado em QUALQUER contexto que não seja o RHS único de
um `MultiBind` ou de um `return` múltiplo — é isto que faz `var t = div(…)` NÃO existir (ruling 3), sem
nunca haver um tipo tuplo. Guarda:

```
/**
 * reject_multivalue_in_value_position — honest-stop quando um call de retorno múltiplo aparece onde só
 * um ÚNICO valor cabe (ruling 3: `var t = div(…)` não existe). Um call multi-valorado é legal SÓ como
 * o RHS único de um `MultiBind` posicional (`var q, r = div(…)`) ou de um `return (…)` de aridade
 * casada; em todo o resto (arg de call, elemento de array, operando, `var t = …` simples) é um erro,
 * porque não há tipo tuplo que o segure.
 * @param call  o TCall recém-tipado
 * @return      null quando não é multi-valorado; senão o erro localizado
 * @since §9 F
 */
fn reject_multivalue_in_value_position(call: TCall): error | null
```

---

## 3. Atribuição múltipla e decomposição — o DESUGAR (checker → nós tipados existentes)

`type_statement` (`typer.tks:4679`) ganha um arm `parser::MultiBind as mb => type_multibind(mb, env,
table)`. `type_multibind` NÃO produz nó tipado novo: devolve um `TBlockStmt`
(`tast.tks`, já existente) cujo corpo são `TBinding`s sintetizados. Assim escapa/borrow/metrics/
codegen/LIR (41 consumidores de `TBinding as` em 19 ficheiros) ficam INTOCADOS.

```
/**
 * type_multibind — tipa e DESUGARA um `MultiBind` (§9 F) para um `TBlockStmt` de `TBinding`s já
 * conhecidos por todo o pipeline, sem nó tipado novo. Três formas, um desugar cada:
 *
 *   PARALELO com N valores  (`var a, b = e1, e2`):
 *       avalia cada `ei` num temp fresco `_mbN` (preserva a semântica PARALELA — `var a,b=b,a` troca),
 *       depois liga cada alvo ao seu temp. Tipos inferidos por posição (ruling 1); com `: T`, cada
 *       temp/alvo toma `T` (anotação partilhada). `_` (BindSkip) liga a `(void)` (nenhuma variável).
 *
 *   PARALELO com 1 valor multi-valorado  (`var q, r = div(17,5)`):
 *       liga o call ao temp agregado `_mb` (tipo = o agregado sintetizado de retorno, §4), depois cada
 *       alvo a um field-access `_mb._0`, `_mb._1`, … A aridade dos alvos DEVE casar `1 + ret_extra.len`
 *       (senão erro). É o único sítio (com `return`) onde um call multi-valorado é legal.
 *
 *   DECOMPOSIÇÃO DE ARRAY  (`var [a, _, c] = arr` / `var [a, …, b, c] = arr`):
 *       liga `arr` ao temp `_mb: []T`, EMITE a guarda de comprimento (ruling 5) como um `if`+`panic`
 *       sintetizado, depois cada alvo a um índice: cabeça `_mb[i]`, cauda (após `…`) `_mb[_mb.len-k]`.
 *       `_` salta a posição (índice avança, nenhuma variável). `…` (BindRest) não consome contagem
 *       fixa; separa cabeça de cauda. Sem `…`: guarda `_mb.len == N` exato; com `…`: guarda
 *       `_mb.len >= headN + tailN`.
 *
 * @param mb     o statement de binding múltiplo
 * @param env    o ambiente de tipos
 * @param table  a tabela de tipos do programa
 * @return       o `TBlockStmt` desugarado + o env estendido com os nomes ligados, ou um erro
 * @throws       aridade RHS≠LHS no paralelo; call multi-valorado de aridade errada; `[]T` esperado na
 *               decomposição; `…` fora de `ArrayDecomp` ou repetido; anotação `: T` incompatível
 * @since §9 F
 */
fn type_multibind(mb: parser::MultiBind, env: Env, table: TypeTable): TypedStmt | error
```

**Guarda de comprimento (ruling 5) — reuso puro.** A guarda é um `if` tipado sobre `.len` do slice
(campo já existente) mais o builtin `panic` (o typer já conhece `panic`, ramo em `typer.tks:2078`).
Sem `…`: `if _mb.len != N { panic("array decomposition: expected exactly N elements") }`. Com `…`:
`if _mb.len < headN + tailN { panic("array decomposition: expected at least K elements") }`. Ambos
desugaram para `TIfExpr`+`TCall(panic)` existentes — zero codegen novo. A indexação de cauda usa
`_mb.len - k` (aritmética `u64` sobre `.len`), também já suportada.

**Helper de índice de cauda (comprimento em runtime):**

```
/**
 * decomp_index_expr — a expressão de índice tipada para o alvo na posição `slot` de uma
 * `ArrayDecomp` sobre o temp `arr_name: []T` (§9 F). Antes do `…` (cabeça): `arr_name[slot]`, índice
 * constante. Depois do `…` (cauda): `arr_name[arr_name.len - (tailN - j)]`, índice calculado em
 * RUNTIME a partir do comprimento (ruling 5), onde `j` é o offset 0-based dentro da cauda. Reusa
 * `Index`/`FieldAccess(len)` existentes; não introduz nó novo.
 * @param arr_name  o nome do temp que segura o slice-fonte
 * @param slot      posição na cabeça (pré-`…`), 0-based
 * @param j         offset na cauda (pós-`…`), 0-based
 * @param in_tail   true iff o alvo está depois do `…`
 * @param tailN     nº de alvos de cauda
 * @return          o `Expr` de índice pronto a tipar
 * @since §9 F
 */
fn decomp_index_expr(arr_name: str, slot: u64, j: u64, in_tail: bool, tailN: u64): parser::Expr
```

---

## 4. Retorno múltiplo — como LOWERA SEM tipo tuplo (a questão de design central)

**Resposta:** um **agregado-por-valor SINTETIZADO pelo codegen**, gerado a partir da LISTA
`Func.ret_extra` (checker), NUNCA um tipo do sistema de tipos nem uma entrada de utilizador na
`TypeTable`. É o mesmo idioma já em produção em `TThunkEmit` (`codegen.tks:87`) — *"a named struct is
the seed-compatible way to return two values"* — elevado do caso-fixo-dois-valores para o caso-N,
derivado da assinatura.

Porquê agregado-por-valor e NÃO out-params: ambos os backends JÁ retornam structs por valor em todo o
lado (o C retorna structs nomeados nativamente; o nativo tem `lower_return_fat`, `lower.tks:7074`, o
caminho de retorno multi-palavra). Out-params exigiria uma convenção de chamada nova em DOIS backends
e paridade `cgt_mangle`/ABI — muito mais superfície. O agregado reusa tudo o que já lá está.

**Como honra "sem tipo tuplo no sistema de tipos" (ruling 3), à LETRA:**
- O `Type` variant NÃO ganha caso (`type.tks:147` intocado). O agregado NÃO é um `Named` de
  utilizador — não entra na `TypeTable`, não é `resolve_type`-ável, não é spellável.
- O checker representa a multi-valoração como a LISTA `Func.ret_extra` (na assinatura) + a flag no
  `TCall`. Não há valor de tipo-tuplo em lado nenhum.
- `var t = div(…)` é rejeitado por `reject_multivalue_in_value_position` (§2.2): sem um tipo que o
  segure, um call multi-valorado só vive destruído.
- O struct-C sintetizado (`struct tk_mret_<mangle> { T0 _0; T1 _1; … };`) é um ARTEFACTO DE CODEGEN,
  emitido a partir de `ret_extra`, keyed pela lista de tipos (determinístico, injetivo, C-legal via os
  manglers `cb_tysym`/`mangle_type_name` existentes — o mesmo requisito que o sufixo de §9 A). Vive no
  header de codegen ao lado dos thunks; não é observável na superfície.

**Fluxo:**
1. **Assinatura.** `parse_function` (`parse_decl.tks:400`): quando o token pós-`:` é `(`, parseia uma
   LISTA de `parse_type` separada por `,` até `)`, grava em `Function.ret_types` (aridade ≥ 2). Um
   `(` com um só tipo `(A)` é rejeitado (use `A`). `collect`/`type_function` mapeia `ret_types` →
   `Func.ret = types[0]`, `Func.ret_extra = types[1..]`.
2. **`return (e1, e2)`.** `parse_statement` (`parse_stmt.tks:12`): num `return` seguido de `( … , … )`,
   parseia a lista para `Return.values` (o grouping de UM expr `return (e)` continua a ser retorno
   simples). `type_return` (`typer.tks:4638`): quando `values.len ≥ 2`, tipa cada um, casa
   posicionalmente contra `Func.ret`+`ret_extra` (`check_return_stmt`, `typer.tks:5386`, ganha o ramo
   multi).
3. **Codegen do agregado.** Uma fase (nova fn `cg_emit_mret_structs`, análoga a
   `cg_emit_vtable_thunks`) percorre as fns com `ret_extra` não-vazio, emite cada struct-agregado UMA
   vez (dedup por mangle, como os thunks). `emit_function_sig` (o tipo de retorno) e `emit_return`
   (`codegen.tks:8919`) usam o struct-agregado; `return (e1,e2)` vira
   `return (struct tk_mret_…){ ._0 = e1, ._1 = e2 };`. Gémeo nativo em `lower_return`
   (`lower.tks:7033`) via o caminho `_fat` já existente.
4. **Sítio de binding.** JÁ desugarado por `type_multibind` (§3, forma "PARALELO com 1 valor
   multi-valorado"): `var q, r = div(17,5)` → `_mb = div(17,5); q = _mb._0; r = _mb._1;` — field-access
   sobre o agregado, nós tipados existentes.

**Paridade de mangle (rede).** O nome do struct-agregado é produzido por UMA fn-fonte-da-verdade no
checker/codegen e ambos backends anexam-no verbatim, tal como o sufixo de §9 A; `cgt_mangle_parity_c_and_native`
prova a paridade. O implementador DEVE fixar a gramática do nome (`tk_mret_` + mangle dos tipos por
`_`) num Javadoc na fn produtora e cobri-la por fixture.

---

## 5. Parser — os dois lookaheads e o RHS-lista (detalhe)

- `is_var_binding_head` (`parse_stmt.tks:312`) ganha dois reconhecimentos novos: `var [` (LBracket ⇒
  `ArrayDecomp`) e `var NAME ,` (Ident+Comma ⇒ `Parallel`). O `var {` (destructure de campo B.13) e o
  `var NAME :`/`=` (simples) ficam. `const` idem via `is_binding_head` (`parse_stmt.tks:329`).
- `parse_binding` (`parse_stmt.tks:250`) ramifica: se a cabeça é múltipla (`[` ou lista de nomes),
  chama `parse_multibind` (nova) em vez do caminho `SimpleName`/`DestructurePattern`; senão byte-idêntico.
- `parse_multibind` (nova): parseia a lista de `BindElem` (nomes/`_`/`…`), a anotação partilhada
  opcional `: T`, o `=`, e a lista de valores (`parse_expr` separada por `,`). Reusa `parse_annotation`,
  `parse_expr`. Rejeita `…` fora de `[ ]`, `…` repetido, `[ ]` vazio, lista vazia.
- `_` como alvo múltiplo: NÃO passa por `parse_bind_target` (que o rejeita, `parse_stmt.tks:367` —
  correto, esse é o discard-assign); `parse_multibind` aceita `_` produzindo `BindSkip`.

---

## 6. Segurança de FIXPOINT (aditivo; quantificado)

- **Nenhuma fonte atual usa as formas novas.** `var a,b=…`, `var [a]=…` e `(A,B)` na assinatura são
  todos NOVOS; o compilador em `src/` não os contém. O implementador DEVE confirmar por grep antes de
  reseed (não pude correr build): `var\s+\[`, `var\s+\w+\s*,`, `:\s*\(` na posição de retorno, e
  `return\s*\(` com vírgula.
- **`Binding`/`TBinding`/`Return`/`Func` só GANHAM campos com default vazio/inerte.** `Function.ret_types`
  = `empty()`, `Return.values` = `empty()`, `Func.ret_extra` = `empty()`. Cada literal existente
  acrescenta o campo vazio; o comportamento é byte-idêntico. **RITUAL** em cada crumb que toca estes
  tipos centrais.
- **`Statement` ganha o caso `MultiBind`.** `match` EXAUSTIVO sobre `Statement` (SOURCE) — poucos
  consumidores (`parser::Binding as` no source = 3 ocorrências em 2 ficheiros); a maioria dos walks usa
  `_ =>`. O implementador acrescenta um arm (tipicamente `_ => …` já cobre, ou um walk trivial dos
  `values`) a cada `match` sem default. Como `type_multibind` desugara para `TBlockStmt`+`TBinding`,
  NENHUM `match` sobre `TStatement`/`TBinding` (41 sítios) muda — a superfície TIPADA é ZERO.
- **O agregado de retorno só existe quando `ret_extra` é não-vazio** — nenhuma fn atual o tem ⇒ zero
  structs sintetizados no corpus ⇒ codegen byte-idêntico. Inerte até uma fixture declarar um
  `fn …: (A,B)`.

---

## 7. Fixtures de regressão

Layout (confirmado em §9 A): projeto `examples/regressions/<nome>/` com `.tkp`/`.tkr`/`main.tks`;
ACEITAR usa `Then stdout pattern = "…"` (valor aritmético que só o caminho certo produz); REJEITAR
dobra em `examples/regressions/diagnostics/` (um `src/<caso>/case.tks` + `Then diagnostic = "…"`).

### 7.1 ACEITAR — `examples/regressions/multi_bind/` (novo projeto)
- **A1 — paralelo, tipos por posição.** `var a, b = 3, 7` → `a*10 + b` = 37.
- **A2 — swap paralelo (semântica paralela).** `var x, y = 1, 2` depois `var y2, x2 = x, y` — prova
  que o RHS é avaliado ANTES de ligar (temps): codifica `x2*10+y2` = 12.
- **A3 — anotação partilhada.** `var a, b: u8 = 200, 55` → `a to i64 + b` = 255 (ambos `u8`).
- **A4 — decomposição exata + `_`.** `var xs: []i64 = [10,20,30]; var [a, _, c] = xs` → `a + c` = 40.
- **A5 — decomposição com `…` (cabeça+cauda).** `var xs: []i64 = [1,2,3,4,5]; var [h, …, p, q] = xs`
  → `h*100 + p*10 + q` = 145.
- **A6 — retorno múltiplo destruído.** `fn div(a: i32, b: i32): (i32, i32) { return (a/b, a%b) }`;
  `var q, r = div(17, 5)` → `q*10 + r` = 32.
- **A7 — `…` de comprimento mínimo (0 no meio).** `var xs: []i64 = [1,2]; var [h, …, t] = xs` →
  `h*10 + t` = 12 (o meio salta 0 elementos, ≥ 2 satisfeito).

### 7.2 REJEITAR — dobrar em `examples/regressions/diagnostics/`
- **R1 — aridade paralela.** `var a, b = 1, 2, 3` → `Then diagnostic = "targets"` (LHS≠RHS).
- **R2 — retorno múltiplo em valor simples (ruling 3).** `var t = div(17, 5)` (com A6) →
  `Then diagnostic = "multi"` (só destruível, sem tuplo).
- **R3 — comprimento exato falha (RUNTIME).** `var xs: []i64 = [1,2]; var [a, b, c] = xs` — é PANIC de
  runtime, não diagnóstico de compilação ⇒ vai em ACEITAR como cenário que espera exit-code de panic
  (padrão dos testes de bounds), não em `diagnostics/`.
- **R4 — `…` duplo.** `var [a, …, b, …, c] = xs` → `Then diagnostic = "…"`/`".."` (um só intervalo).
- **R5 — `(A)` retorno de aridade 1.** `fn f(): (i32) { … }` → `Then diagnostic = "single"` (use `A`).

---

## 8. Sequência de crumbs (ordenada; cada uma gate-ável isoladamente)

Cada crumb compila e passa o gate rápido; os RITUAIS (gate completo) marcados. Toda a §9 F é aditiva
sobre o seed atual — pode aterrar independentemente das outras letras de §9.

1. **AST — nós novos, inertes.** `BindElem`/`BindName`/`BindSkip`/`BindRest`, `MultiTargetKind`,
   `MultiBind`; `Statement` += `MultiBind`; `Function.ret_types`; `Return.values`. Inicializar os
   campos novos a `empty()` em TODOS os literais existentes (grep `Function {`, `Return {`). Nenhum
   produtor ainda. — *inerte.* **RITUAL: gate completo** (toca `Statement`/tipos-fonte centrais).
2. **Checker — tipo, inerte.** `Func.ret_extra` (`empty()` em todos os `Func {`); `TCall.ret_extra`;
   `reject_multivalue_in_value_position` (código morto compilável). — *inerte.* **RITUAL.**
3. **Parser — atribuição múltipla + decomposição.** `is_var_binding_head`/`is_binding_head`
   lookaheads (`[`, lista); `parse_multibind`; ramo em `parse_binding`. Produz `MultiBind` só para a
   sintaxe nova; o binding simples fica byte-idêntico. — *aditivo (nova sintaxe).*
4. **Checker — desugar de `MultiBind`.** `type_multibind` (§3) + `decomp_index_expr` + a guarda de
   comprimento; arm em `type_statement`. Desugara para `TBlockStmt`/`TBinding`/`if`+`panic`. — *codegen/LIR
   INTOCADOS.* **RITUAL** (é a crumb que fecha atribuição-múltipla+decomposição ponta-a-ponta).
5. **Fixtures A1–A5, A7 + R1, R3, R4** (multi-assign/decomp; ainda SEM retorno múltiplo). **RITUAL.**
6. **Parser — retorno múltiplo.** `parse_function` lista `(A,B)` → `ret_types`; `parse_statement`
   `return (…)` → `Return.values`. Rejeita `(A)` aridade-1. — *aditivo.*
7. **Checker — retorno múltiplo.** `collect`/`type_function` → `Func.ret_extra`; `type_return` +
   `check_return_stmt` ramo multi; `type_call` marca o `TCall` multi-valorado e liga
   `reject_multivalue_in_value_position`; a forma "1 valor multi-valorado" de `type_multibind`. — *aditivo.*
8. **Codegen + LIR — agregado por valor.** `cg_emit_mret_structs` (fase, dedup por mangle);
   `emit_function_sig`/`emit_return` usam o struct-agregado; gémeo nativo via `lower_return_fat`.
   Confirmar `cgt_mangle_parity_c_and_native` verde. — *default: sem `ret_extra` ⇒ zero structs ⇒
   byte-idêntico.* **RITUAL** (é a crumb que emite código novo; a paridade C↔nativo é a rede).
9. **Fixtures A6 + R2, R5** (retorno múltiplo ponta-a-ponta). **RITUAL.**
10. **Reseed + PROVENANCE** (crumb final, §9).

---

## 9. Ritual de reseed + PROVENANCE (crumb final)

Só depois de todas as crumbs verdes e do gate completo passar:
1. `cc -std=c2x -w -O2 -I src/runtime -I src/assert bootstrap/teko.c src/runtime/teko_rt.c
   src/assert/assert.c -lm -o gen0`
2. `TEKO_BACKEND=c ./gen0 build . --no-verify --release` → `bin-a`.
3. Re-build com `bin-a` como seed → `bin-b`. **Fixpoint: `bin-a == bin-b`** byte-a-byte
   (`scripts/fixpoint_gate.sh`). §9 F é aditivo-inerte no corpus do compilador (nenhuma fn usa as
   formas novas), logo o fixpoint DEVE fechar na crumb 8; as fixtures (5, 9) introduzem as formas no
   CORPUS DE TESTE, não no compilador.
4. Harvest: `bootstrap/teko.c` (novo seed) + `bootstrap/PROVENANCE` (novo hash/proveniência).
5. NUNCA `teko test .` (fuga de memória). Gate por `--no-verify` + os `scripts/*.sh`.

---

## 10. Riscos + tensões de lei (com resolução recomendada)

- **Grafia do `…` (Decisão D1 — recomendação law-first, NÃO bloqueante).** O ruling escreve `…`
  (unicode), mas **não existe token `…` no lexer** (`src/lexer/token.tks`); o spread/range já é `..`
  (`DotDot`, `token.tks:146`). Adicionar um token unicode novo alarga o seed e diverge da grafia
  ASCII do resto da linguagem. **Recomendação:** grafar o intervalo-saltado com `..` (o `DotDot`
  existente, zero mudança de lexer), tratando `…` como render de documentação. `var [a, .., b, c]`.
  Lei: mudança mínima de seed, consistência com a grafia de spread. Se o dono INSISTIR no `…`
  unicode, é um token novo no lexer (crumb extra antes da crumb 3) — REPORTO a necessidade, não
  invento. Sem tensão de lei genuína; é uma escolha de grafia.
- **Sem tipo tuplo (ruling 3) vs. o agregado sintetizado (§4).** Aparente tensão, RESOLVIDA à letra: o
  agregado é artefacto de CODEGEN derivado de `Func.ret_extra`, não um `Type` variant nem entrada de
  `TypeTable`, não spellável, não admissível em posição de valor (guarda de §2.2). É o mesmo idioma já
  em produção (`TThunkEmit`, `codegen.tks:87`). "Sem tuplo no sistema de tipos" refere-se ao sistema
  de tipos da SUPERFÍCIE — honrado. Sem HALT.
- **Semântica paralela (ruling 1) exige temps.** `var a, b = b, a` tem de trocar; desugar para temps
  frescos ANTES de ligar (§3) garante-o. Coberto por A2. Sem tensão.
- **Guarda de comprimento é runtime (ruling 5).** Panic, não diagnóstico — R3 é fixture de exit-code
  de panic (ACEITAR), não `diagnostics/`. Reusa `panic` (já builtin) e `.len` (já existente). Sem
  tensão.
- **Aridade do agregado e o mangle.** O nome do struct-agregado tem de ser determinístico/injetivo/
  C-legal (mesma disciplina do sufixo de §9 A); reusar `cb_tysym`/`mangle_type_name`. Cobrir por
  fixture de paridade. Sem tensão de lei.
- **Nenhuma tensão de lei genuína identificada.** Todos os rulings selados encaixam num desenho
  law-first coerente (desugar-para-nós-existentes + agregado-de-codegen). **Nenhum HALT necessário.**

---

## 11. Perguntas ao dono/integrador (não-bloqueantes; adiantei tudo o que não depende delas)

1. **Grafia do intervalo-saltado (D1):** `..` (ASCII, reusa `DotDot`, zero lexer — RECOMENDADO) vs.
   `…` (token unicode novo). Adiantei todo o desenho assumindo `..`; trocar para `…` é só um token de
   lexer + o predicado de parse, sem impacto nas crumbs 4–9.
2. **Aridade máxima / anotação por posição:** o desenho usa UMA anotação partilhada (`var a,b: T`,
   ruling 1) — confirmo que NÃO há forma `var a: A, b: B` (por-posição). Se o dono quiser a
   por-posição no futuro, é aditivo sobre `MultiBind` (uma lista de anotações), fora de §9 F.
3. **`_` em retorno múltiplo destruído:** `var q, _ = div(…)` — o desenho aceita (BindSkip liga a
   `(void)` o campo do agregado). Confirmo que é desejado (paridade com a decomposição de array).
```
