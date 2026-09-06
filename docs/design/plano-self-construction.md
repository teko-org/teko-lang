# Plano — self-construction (o gap do §9) via `.{ … }` target-typed

Status: DESIGN (arquiteto). Ruling do dono (2026-08-13, DUAS ratificações): (1) a via
target-typed **`.{ campo = valor }`** — um `.` unário PREFIXADO + bloco de inicialização que
**lowera para o TIPO ESPERADO do contexto**, exige tipo-alvo conhecido (sem-alvo = erro);
(2) as DUAS formas de construção são **`Tipo { … }` (nominal)** e **`.{ … }` (target-typed)** —
o **`self { }` como CONSTRUTOR é RETIRADO** (não consertado): `self` fica só como **receptor** e
como **tipo**. Este doc desenha o plano de IMPLEMENTAÇÃO em torno dessa lei (agora selada em Doc 2
§4.1, commit `4b4781d4`), com o RECON que localiza a falha atual, o **sweep de remoção** do
`self { }` e o blast-radius/ordem de fixpoint source-only. Lei de referência:
`mudancas-superficie-0.3.1.md` §4.1 (as duas formas), §10.3 (Intent em `.{}`), §13.2/§13.3
(item 14, `self` ref implícito; construção via `.{}`). Selados NÃO reabertos: §9.4, item 14,
Intent §10.3, §9.D (união `|` inline por extenso, abreviável por macro Família A `@Type()`).

---

## 0. TL;DR

- **Gap:** `static fn zero(): self { self { _n = 0 } }` erra com *"the function's final
  expression does not match its declared return type"* — **em qualquer tipo declarado sob um
  namespace** (todo o corpus do compilador + a Intent). Causa exata isolada abaixo (§1.4): o
  valor construído por `self { … }` recebe o nome do owner **cru/bare** (`env.owner_type`),
  enquanto o retorno `self` resolve para o nome **canônico qualificado** — e `widens_into`
  compara nomes de `Named` por igualdade EXATA de string. Bare `Counter` ≠ `ns::Counter` ⇒ falha.
- **Resolução (dono): retirar `self { }`, não consertar.** Como a lei agora tem `.{ }` (que nasce
  no nome CANÔNICO do alvo) e `Tipo { }`, o `self { }` construtor é **removido** — e com ele
  **desaparece o bug bare→canônico** (não há mais `self { }` a canonizar). `self` sobra só como
  receptor (`self.x`, `self::y`) e como tipo (`(): self`).
- **Via target-typed:** `.{ campo = valor }` constrói o **tipo-alvo do contexto** (retorno de
  `static fn` cujo tipo é `self`; slot `var x: Foo = .{…}`; argumento; campo). Factory:
  `static fn zero(): self { .{ _n = 0 } }`. Sem nomear `self` nem o tipo.
- **Regra ratificada:** `.{}` é legal **somente num slot tipado** (um contexto que forneça um
  tipo-alvo). **Sem tipo-alvo ⇒ erro de compilação** (mensagem em §2.4).
- **Três formas, estado SELADO (§2.5):** `Tipo { … }` nominal = **válida sempre** ("à gosto do
  cliente"); `.{ … }` target-typed = válida onde há tipo-alvo (sem alvo = erro); `self { … }`
  construtor = **REMOVIDO** (self só receptor/tipo). `Tipo { }` e `.{ }` coexistem.
- **Custo real:** (a) o único plumbing NOVO é **propagar o tipo de retorno como `expected`** para
  as posições de cauda/`return` (hoje typadas sem `expected`) — binding-anotado e campo já
  propagam, argumento é um predicado a mais; (b) a **remoção** do `self { }` do parser/checker + o
  **sweep** de 4 sítios `.tks`/`.tkr` do corpus (§3.4). **Zero mudança no codegen** (§3.3).

---

## 1. RECON — como a construção funciona hoje, e onde `self`-como-retorno falha

### 1.1 A construção nominal `Name { field = … }` — parse

O parser NÃO tem um nó de construção dedicado: reusa `StructLit`
(`src/parser/ast.tks:244`):

```
pub type StructLit = struct { type_path: Path; type_args: []TypeExpr; field_names: []str; field_vals: []Expr }
```

`parse_struct_lit` (`src/parser/parse_expr.tks:122-152`) recebe o `type_path` já parseado e lê
o bloco `{ nome = valor; … }`. O PREFIXO é sempre dispatchado ANTES em `parse_atom`
(`parse_expr.tks:457-508`):

- `self {` — `SelfKw` seguido de `LBrace`, com `allow_struct`, vira
  `parse_struct_lit(tokens, pos+1, self_sentinel_path(), [])` (linha 457-459). O
  `self_sentinel_path()` é um path de 1 segmento com o nome reservado `self` — nenhum tipo de
  usuário pode soletrá-lo (§4 reserva `self`), então é um sentinela inequívoco.
- `Name<A..> {` — path + type-args + `LBrace` (linha 486-488).
- `Name {` — path + `LBrace` (linha 497-498).
- `{` solto (sem path) — é um **Block-B** (`parse_block_expr`, linha 505-507), NUNCA uma
  construção. **Construção sempre carrega um prefixo de tipo.**

**Não existe hoje um `.` em posição de átomo (líder).** Em `parse_atom` nenhum ramo começa com
`Dot`; um `.` líder cai em *"expected an expression"* (`parse_expr.tks:508`). Em
`parse_postfix` (`parse_expr.tks:564-567`) o `.` EXIGE um receptor à esquerda e um nome depois
(`.field`/`.method`). Logo **`.{` é hoje um buraco de gramática livre** — nenhuma produção o
reclama (prova de não-ambiguidade em §2.1).

### 1.2 A construção — checagem (typer)

`type_struct_lit` (`src/checker/typer.tks:4460`) tipa a construção. O prefixo é resolvido por
`construct_target` (`typer.tks:4434-4438`):

```
fn construct_target(sl, expected, env, table): ConstructTarget | error {
    if is_self_sentinel(sl.type_path) { return self_construct_target(env, table) }   // self { … }
    if sl.type_args.len > 0 { return explicit_inst_target(sl, expected, env, table) } // Foo<A..> { … }
    bare_construct_target(sl, env, table)                                             // Foo { … }
}
```

- `is_self_sentinel` (`typer.tks:4298`): `path.segments.len == 1 && segments[0].name == "self"`.
- `self_construct_target` (`typer.tks:4349-4363`): usa `env.owner_type` como nome do alvo.
  Owner não-genérico ⇒ `ConstructTarget { name = env.owner_type; … }`. Owner genérico ⇒ nome
  phantom `Base__g__<params>` (`self_inst_spelling`), remapeado depois no mono pass.

O corpo compartilhado (linhas 4490-4551) exige EXATAMENTE os campos declarados (uma vez cada),
tipa cada valor de campo com `type_value_expected(sl.field_vals[found], ft, env, table)` (linha
4512 — **o campo já propaga `expected` para construções aninhadas**), e emite o nó final:

```
TExpr { kind = TStructInit { field_names = names; field_vals = vals }; type = Named { name = name }; … }   // typer.tks:4551
```

Ou seja, **o tipo do valor construído é `Named { name = <name do ConstructTarget> }`**.

### 1.3 `self` como TIPO em posição de retorno de `static fn`

Em `type_method` (`src/checker/typer.tks:7938`), o tipo de retorno é resolvido em
`typer.tks:7997-8001`:

```
var ret = if f.has_return && return_type_is_self(f.return_type) {
    Named { name = qualify(env.cur_ns, struct_name) }        // ← CANÔNICO qualificado
} else {
    match function_return(f, tbl, env.cur_ns) { Type as t => t; error as e => return e }
}
```

O `struct_name` é `td.name` (bare, ex. `Counter`); `env.cur_ns` é o namespace do item (posto por
`ienv = with_ns(cenv, it.namespace)`, `typer.tks:8917/9066`). Então para um tipo declarado em
`ns`, `ret = Named { name = "ns::Counter" }`.

A validação da cauda roda DEPOIS de tipar o corpo:
- `check_returns` (`typer.tks:8017`) → `check_return_stmt` para `return e`.
- `check_trailing_value` (`typer.tks:8018` → `7506-7518`) para a expressão-cauda (retorno
  implícito). Ambos chamam `yields_declared_return(value, ret, table)` (`typer.tks:7422-7424`):

```
fn yields_declared_return(value, ret, table): bool {
    assignable_to(value.type, ret, table) || numeric_widens_implicitly(value.type, ret) || literal_adopts(value, ret)
}
```

`assignable_to = widens_into` (`typer.tks:6806`). Para `Named`→`Named`, `widens_into_at`
(`resolve.tks:1328`) começa por `type_eq` (`type.tks:201`): `Named as na => match b { Named as
nb => na.name == nb.name; … }` — **igualdade EXATA de string**. Os ramos de upcast seguintes
(`resolve.tks:1362-1371`) só valem para CLASSES (interface/ancestral); um STRUCT não widen-a por
nada além de nome idêntico.

### 1.4 O ponto exato da rejeição (causa-raiz do `self { }`)

`env.owner_type` dentro do corpo é posto por `with_owner(env, declaring_class)`
(`typer.tks:7942`; `with_owner` em `scope.tks:134` grava o argumento CRU em `owner_type`). Para
um **struct** (não-classe), `declaring_class = td.name` **bare** (`type_struct_methods`,
`typer.tks:8667-8671`: o ramo `else => td.name`). Logo:

- valor de `self { … }` : `type = Named { name = "Counter" }`  (bare `env.owner_type`)
- retorno `self`        : `ret  = Named { name = "ns::Counter" }` (canônico, `qualify(ns, …)`)

`widens_into("Counter", "ns::Counter")` = **false** ⇒ `check_trailing_value` emite
*"the function's final expression does not match its declared return type"*
(`typer.tks:7514`). No namespace-raiz (`cur_ns == ""`) os dois coincidem — por isso o bug só
morde **tipos namespaced** (todo o corpus do compilador + a Intent). Para owner **genérico** há
uma segunda divergência do mesmo tipo: `self { }` produz o phantom `Base__g__…` enquanto o
retorno-`self` produz o base bare-qualificado (`typer.tks:7998` não gera phantom) — duas grafias
distintas do mesmo tipo.

> **Consequência do ruling (2026-08-13): o bug DISSOLVE.** Como o dono RETIRA o `self { }`
> construtor (em vez de consertá-lo), não sobra nenhum `self { }` a canonizar — a divergência
> bare↔canônico deixa de existir por CONSTRUÇÃO. A remoção do `self { }` (parser + checker) vira
> um crumb **OBRIGATÓRIO** (§4, C7), com **sweep** dos sítios do corpus (§3.4). `self` permanece
> só como receptor e como tipo. A maquinaria phantom (`self_inst_spelling`, `resolve.tks:2226`)
> **NÃO** é deletada — é **reaproveitada** por `.{}` para estampar o alvo genérico (§2.2).

### 1.5 Sítios do corpus que esperam o padrão

- **Intent §10.3** (`mudancas-superficie-0.3.1.md:847-849, 864-866`):
  `pub static fn new(…): self { self { _canceled = c; _failure = f } }` — a ÚNICA construção da
  Intent protegida. Hoje inconstruível (namespaced).
- **Factory de value-struct (item 14)** — `static fn`-fábrica que retorna `self` (ex.
  `zero()`/`make()`), o padrão que a onda-das-traits reportou quebrado.
- **`service … { static fn ctor(): self }`** — a conformidade IService já exige a assinatura
  (`collect.tks:2499`); o CORPO `ctor` que constrói `self` cai no mesmo gap.

### 1.6 Threading de `expected` que já existe (o que `.{}` reaproveita)

`type_value_expected` (`typer.tks:5642`) é o ponto único que dá um `expected` a um `StructLit`
(→ `type_struct_lit(sl, expected, …)`). Ele já é chamado em:

| Posição | Sítio | Propaga `expected`? |
|---|---|---|
| Campo de struct-lit | `typer.tks:4512` | **sim** (`ft`) |
| Binding anotado `var x: Foo = …` | `type_binding_value` `typer.tks:5886-5889` (`at`) | **sim** |
| Const anotado | `typer.tks:8743` | sim |
| Argumento (só lambda/iface-arr/composite-num) | `typer.tks:3287-3290` (`arg_wants_expected`, `5709`) | **parcial** |
| `return e` | `type_return` `typer.tks:6249-6252` (plain `type_expr`) | **NÃO** |
| Cauda de bloco (retorno implícito) | `type_block` `typer.tks:5394-5432` (plain `type_expr`) | **NÃO** |

As duas últimas linhas são o plumbing NOVO (§3.1).

---

## 2. Mecânica do `.{}` (a via do dono)

### 2.1 Parsing — `.{` é inequívoco

Adiciona-se UM ramo no início de `parse_atom` (`parse_expr.tks`, junto ao dispatch de `self {`
em 457): quando o token corrente é `Dot` e o seguinte é `LBrace`, emitir um `StructLit` com um
**path-sentinela vazio** (`type_path.segments.len == 0`) — o "dot sentinel":

```
if k == lexer::TokenKind::Dot && is_kind_at(tokens, pos + 1, lexer::TokenKind::LBrace) {
    return parse_struct_lit(tokens, pos + 1, dot_sentinel_path(), teko::list::empty())
}
```

onde `dot_sentinel_path()` devolve `Path { segments = teko::list::empty() }`. `parse_struct_lit`
já parseia `{ nome = valor; … }` verbatim — **zero nova lógica de bloco**.

**Não-ambiguidade (prova):**
- `parse_atom` hoje não tem ramo iniciando com `Dot` → `.{` não colide com nada em posição de
  átomo (`parse_expr.tks:508` é o `else` de erro).
- `parse_postfix` só consome `.` DEPOIS de um átomo (receptor à esquerda) e SEMPRE exige um NOME
  após o `.` (`parse_expr.tks:565-567`), nunca um `{` → `x.{` continua erro (correto: não há
  "construção sobre receptor").
- `..` (DotDot, spread) e um float `.5` são tokens/lexemas distintos de `Dot` `LBrace`: o
  caractere após o `.` é `.` ou dígito, nunca `{`. Sem colisão de maximal-munch.
- O ramo NÃO precisa do gate `allow_struct`: o `.` líder já delimita — não pode ser confundido
  com o `{`-bloco de um scrutinee `if`/`match`. Recomenda-se parseá-lo **incondicionalmente**
  (mais previsível que copiar o gate); um `.{}` num scrutinee simplesmente falhará no type-check
  por não produzir `bool`, como qualquer outro valor mal-posto.

**Representação AST:** reusar `StructLit` com `type_path` vazio evita um novo `ExprKind` e
reaproveita TODO o tail de checagem de campos de `type_struct_lit`. O sentinela vazio é
distinguível do `self` sentinel (`segments.len == 1`, nome `self`) e do nominal (`segments.len
>= 1`).

### 2.2 Inferência do tipo-alvo (checker)

Um novo predicado `is_dot_sentinel(path): bool` = `path.segments.len == 0`, e um novo ramo em
`construct_target` (`typer.tks:4434`). **Estado FINAL (pós-R2, `self { }` removido):**

```
fn construct_target(sl, expected, env, table): ConstructTarget | error {
    if is_dot_sentinel(sl.type_path) { return dot_construct_target(expected, env, table) }
    if sl.type_args.len > 0 { return explicit_inst_target(sl, expected, env, table) }
    bare_construct_target(sl, env, table)
}
```

Na **janela aditiva (R1)** o ramo `if is_self_sentinel(sl.type_path) { return
self_construct_target(env, table) }` (`typer.tks:4435`) **permanece** — o parser ainda aceita
`self { }` até o sweep, então o checker precisa resolvê-lo. Ele (mais `is_self_sentinel`,
`self_construct_target`, `self_sentinel_path`) só é **deletado no crumb C7/R2**, junto com o
ramo `SelfKw`+`LBrace` do parser (`parse_expr.tks:457-459`). `self_inst_spelling` NÃO é deletado
(§2.2, reaproveitado).

`dot_construct_target(expected, env, table): ConstructTarget | error` resolve o alvo a partir
de `expected`, reusando a maquinaria já existente:

1. `expected` é `Named { name }`:
   - `type_table_find(table, name, "")` dá a `TypeDecl` concreta ⇒
     `ConstructTarget { name = name; decl = td; is_self = false }`. Instância genérica já
     estampada (`Foo__g__i64`) e phantom-instance seguem o MESMO tratamento de
     `explicit_inst_target` (`typer.tks:4388-4396`).
   - **Caso genérico-owner (o que `self { }` fazia):** num método genérico `static fn make():
     self` de `Cell<T>`, o `expected` é o base bare-qualificado (o retorno-`self`, hoje
     `typer.tks:7998`, ainda não estampa phantom). `dot_construct_target` REAPROVEITA
     `self_inst_spelling(name_last_segment(env.owner_type), decl.type_params)`
     (`resolve.tks:2226`, a MESMA função que o `self { }` usava) para produzir o phantom
     `Cell__g__T` + o template decl do owner, remapeado no mono pass. Este é o único ponto onde
     a lógica phantom do `self { }` sobrevive — sob `.{}`.
2. `expected` é qualquer OUTRA coisa (Void, Prim, Slice, união sem alvo único, etc.) ⇒
   **erro sem-alvo** (§2.4).

Como `dot_construct_target` produz um `ConstructTarget` com um `name` **canônico** (o de
`expected`, que JÁ é o nome canônico do retorno/slot), o `TExpr` final (`typer.tks:4551`)
sai com `type = Named { name = <canônico> }`, IDÊNTICO ao alvo — `widens_into` passa por
`type_eq`. **É exatamente isto que fecha o gap** — `.{ }` nasce no nome canônico do alvo, ao
contrário do `self { }` (que nascia bare); por isso a via target-typed nunca dispara a
divergência de §1.4, e o `self { }` pode ser removido em vez de canonizado.

### 2.3 Onde `expected` chega (as 5 posições de slot tipado)

`.{}` é legal SOMENTE onde há tipo-alvo. As 5 posições e o estado do threading:

1. **Retorno implícito de `static fn … : self`** (o headline) — NOVO threading (§3.1).
2. **`return .{…}`** — NOVO threading (§3.1).
3. **Binding anotado** `var x: Foo = .{…}` — JÁ funciona (`type_binding_value`, `typer.tks:5889`).
4. **Argumento** `f(.{…})` com tipo do parâmetro — predicado a mais (§3.1, crumb C4).
5. **Campo** `Outer { inner = .{…} }` — JÁ funciona (`type_struct_lit` field loop,
   `typer.tks:4512`).

### 2.4 Sem tipo-alvo → erro (RATIFICAR)

`.{}` num contexto sem tipo-alvo (ex. `var x = .{…}` sem anotação; `println(.{…})` sobre um
`params`/variádico sem tipo fixo; `.{…}` como scrutinee; `.{…}` num slot cujo `expected` é uma
união com >1 struct candidato) é **erro de compilação**, mensagem:

> `` `.{ … }` needs a known target type here — annotate the binding/return or write the type explicitly (`Name { … }`) ``

Emitida por `dot_construct_target` quando `expected` não resolve a um único `Named`
struct/classe construível. Ratifica-se: `.{}` **nunca** infere por back-flow de uso posterior;
o tipo-alvo vem SÓ do slot imediato. Isto mantém a checagem uni-direcional (a mesma disciplina
de `type_value_expected`), sem inferência global.

### 2.5 Estado SELADO das formas de construção (ruling final do dono, 2026-08-13)

**SELADO — não é presunção deste plano.** O dono ratificou o estado definitivo das TRÊS grafias
(Doc 2 §4.1, commit `4b4781d4`; ratificação final que sela o ponto que estava "a ratificar"):

| Forma | Status | Onde vale |
|---|---|---|
| `Tipo { … }` (nominal) | **VÁLIDA SEMPRE — "à gosto do cliente"** | Qualquer posição, com ou sem tipo-alvo no contexto (ex. dentro de `println`, num scrutinee, expressão solta). Com/sem type-args. |
| `.{ … }` (target-typed) | **VÁLIDA onde há tipo-alvo; sem alvo = ERRO** | Os 5 slots tipados (§2.3): retorno `(): self`, `var x: T = .{…}`, argumento, campo. |
| `self { … }` (construtor) | **REMOVIDO** | Em lugar nenhum. `self` fica só **receptor** (`self.x`/`self::y`, item 14 ref implícito) e **tipo** (`(): self`, `p: self`). |

Consequências firmadas: (a) `Tipo { }` e `.{ }` **coexistem** — o dev escolhe a nominal quando
quiser ser explícito ou quando não há tipo-alvo, e a target-typed quando o alvo já está no slot;
(b) todo `self { … }` do corpus é reescrito para `.{ … }` (§3.4) e o parser passa a **rejeitá-lo**
(crumb C7, OBRIGATÓRIO — não mais opcional); (c) a remoção DISSOLVE o bug bare↔canônico (§1.4) —
sem `self { }`, não há nome cru a divergir do canônico.

### 2.6 Interação com item 14 (value-struct, `self` ref implícito)

`.{ … }` é uma **materialização nova** — idêntica em runtime a `Name { … }`/`self { … }`: aloca
o valor fat (cabeçalho `uptr`/auto-ponteiro, §13.2) e preenche os campos. Como `.{}` reduz ao
MESMO `TStructInit` (§3.3), o codegen já emite o cabeçalho/ponteiro-para-si na construção; a
cópia na atribuição/passagem continua a ser nova materialização com novo ponteiro (value
semantics preservada). `.{}` **não** introduz identidade nem heap — é value-type, como a via
nominal. Num `static fn` (sem receptor) `.{}` cria a instância do zero; num método de instância
`.{ … }` cria uma **nova** instância (distinta do `self`-receptor ref).

### 2.7 Interação com a Intent (§10.3)

A Intent é `struct` protegido; a única construção é `pub static fn new(): self`. Com `.{}`:

```teko
/**
 * new — the runtime's sole constructor for a resultless Intent (§10.3): builds a fresh
 * Intent whose outcome fields are seeded from the awaited task's disposition.
 *
 * @param c  the cancellation flag the runtime writes
 * @param f  the cancellation reason, or null on a clean completion
 * @return   a freshly materialized Intent (the target type is `self`)
 * @since    §10.3
 */
pub static fn new(c: bool, f: error | null): self {
    .{ _canceled = c; _failure = f }
}
```

O `expected` do retorno é `self` = `Named{ns::Intent}`; `.{}` materializa exatamente esse tipo,
com os backing `_canceled`/`_failure` privados preenchidos na construção (o único lugar onde um
campo readonly/privado pode ser escrito, §13.3). Nenhuma menção ao nome `Intent` nem a `self` no
corpo — a proteção fica intacta. A união `error | null` do parâmetro é grafia §9.D inline por
extenso (abreviável por macro Família A `@…()`, fora do escopo deste doc).

---

## 3. Blast-radius + ordem de fixpoint (source-only; gate byte-identidade)

### 3.1 O único plumbing novo — propagar o tipo de retorno como `expected`

Hoje `type_return` e a cauda de `type_block` tipam sem `expected`. Para `.{}` em posição de
retorno, o tipo de retorno precisa estar em mãos ANTES de tipar o valor. Abordagem recomendada
(mínima e localizada): **carregar o alvo de retorno no `Env`** — um campo novo `ret_expected:
Type` (espelha `owner_type`), posto em `type_method`/`type_function` a partir do `ret` já
computado, e lido nas duas posições de cauda.

- `Env` (`scope.tks:72`): + `ret_expected: Type`. Atualizar `env_empty`/`seal`/`with_ns`/
  `with_file`/`with_owner` (5 construtores) para carregar o campo (default `Void {}`), e um
  novo `with_ret_expected(env, t)`.
- `type_method` (`typer.tks`): após computar `ret` (7997-8001), tipar o corpo com
  `with_ret_expected(local, ret)` (idem `type_function` para free fns com retorno anotado).
- `type_return` (`typer.tks:6249-6252`): tipar `r.value` via
  `type_value_expected(r.value, env.ret_expected, env, table)` quando `env.ret_expected` não é
  `Void`; senão o `type_expr` de hoje. (O check pós-hoc `check_returns` permanece — agora passa,
  porque o valor nasceu no alvo.)
- `type_block` cauda (`typer.tks:5394-5432`): SÓ a ÚLTIMA statement, quando é `ExprStmt` cujo
  `expr` é um dot-sentinel (ou struct-lit) e `env.ret_expected != Void`, tipar via
  `type_value_expected(es.expr, env.ret_expected, …)`. Para NÃO poluir blocos que não são cauda
  de função (corpo de loop, bloco não-cauda), `ret_expected` deve ser **limpo** (`Void`) ao
  descer em contexto não-cauda: `type_loop` (`typer.tks:6266`) tipa o corpo com
  `with_ret_expected(env, Void{})`; a cauda de `if`/`match` que É valor de função MANTÉM o alvo
  (elas já roteiam por `type_expr` → threading pode passar via um `type_expr_expected` fino, ver
  crumb C3). Disciplina: `ret_expected` vale só na cadeia de posições-cauda do corpo da função.

Alternativa considerada (rejeitada): passar `expected` como parâmetro explícito por toda a
cadeia `type_block`/`type_statement`/`type_return`. Rejeitada por churn de assinatura muito
maior (dezenas de sítios) sem benefício sobre o campo de `Env` (o `owner_type` já provou o
padrão).

### 3.2 Argumento `.{}` — um predicado a mais

Em `typer.tks:3287-3290` o arg `ti` só roteia por `type_value_expected` para
lambda/iface-arr/composite-numérico. Adicionar `arg_is_dot_init(dargs[ti])` (dot-sentinel) — e,
de brinde, struct-lit — à disjunção, para o parâmetro `f.params[ti]` virar `expected`. Inerte
para args que já tipam sozinhos (nominal struct-lit ignora um `expected` redundante).

### 3.3 Codegen — ZERO mudança

`.{}` reduz ao MESMO nó `TStructInit { field_names; field_vals }` com `type = Named{canônico}`.
O codegen consome `TStructInit` pela `e.type` resolvida (`codegen.tks:1346, 5180-5279, 7826,
9273, 9590, 11592`), **nunca** por um path de superfície. Como o checker já entrega o nome
concreto, o codegen não distingue `.{}` de `Name{}`/`self{}`. Nenhuma edição em
`src/codegen/`. Idem motor legado (aposentado). **O gate byte-identidade gen2==gen3 é
preservado por construção** — a AST typada é a mesma que a via nominal produziria.

### 3.4 Sweep do `self { }` no corpus (source-only) — inventário EXATO

Levantamento por `rg 'self\s*\{' <repo>` (executado). **CUIDADO — distinguir dois usos de
`self`:** `operator … (): self { <expr> }` é `self` como **tipo de retorno** com um CORPO de
expressão (ex. `(left to i32 …) to Cel`) — NÃO é construção `self { }`, **NÃO se toca**. Só se
reescreve onde o corpo é literalmente `self { campo = … }`.

**Sítios de CONSTRUÇÃO `self { }` a reescrever para `.{ }`:**

| Arquivo | Linha | Hoje | Vira |
|---|---|---|---|
| `src/collections/list.tks` | 26 | `static fn make(): List<T> { self { items = … } }` | `.{ items = … }` |
| `src/collections/map.tks` | 44 | `static fn make(): Map<V> { self { keys=…; hashes=…; vals=… } }` | `.{ keys=…; … }` |
| `examples/regressions/type_overload/src/probe.tks` | 46 | `pub static fn of(x: T): Box<T> { self { v = x } }` | `.{ v = x }` |
| `examples/regressions/type_overload/src/probe.tks` | 62 | `pub fn remap(nx: T): Box<T> { self { v = nx } }` | `.{ v = nx }` |
| `examples/regressions/type_overload_reject/src/self_free/case.tks` | 16 | `var c = self { a = 1 }` (teste de REJEIÇÃO) | ver abaixo |

**`.tkr` a atualizar:**
- `examples/regressions/type_overload/type_overload.tkr` — cenários `self_static`/`self_instance`
  (descrevem `self { }` numa fábrica/método): renomear para `dot_static`/`dot_instance`, prosa e
  probes para `.{ }`.
- `examples/regressions/type_overload_reject/type_overload_reject.tkr` — cenário `self_free`
  (hoje: "`self { }` fora de método é rejeitado"). **Reenquadrar:** com `self { }` removido, ele
  vira **erro de parse em QUALQUER lugar**; o teste de rejeição correspondente passa a ser
  `dot_no_target` — `.{ }` sem tipo-alvo (`var c = .{ a = 1 }`) é rejeitado (§2.4). O
  `self_free/case.tks` é reescrito para esse `.{ }` sem-alvo.

**Comentários stale (limpeza, não semântica):** `defaults_named/src/dn/dn.tks:9` (código já usa
`B { … }` nominal; só a prosa cita `self { }`); `ast.tks:612,637`; `parse_expr.tks:156`;
`typer.tks:4277-4291,4337-4356,4442-4452`; `resolve.tks:1216` e o DIAGNÓSTICO
`resolve.tks:1265` (mensagem "write `Name<…>` (or `self { … }` to construct …)" → trocar para
`.{ … }`). `trait_mixin/src/tm/tm.tks:35` (comentário "self-construction não suportada ainda" →
agora suportada por `.{ }`).

**Intent §10.3** já reescrita em Doc 2 (commit `4b4781d4`, linhas 883/900 usam `.{ … }`); o
`.tks` da Intent ainda não existe (DESIGN-AHEAD; entra com §10.3/§11).

### 3.5 Ordem de fixpoint (bootstrap-seed safe) — DOIS reseeds

O seed é o `teko` binário anterior: ele **entende `self { }`** mas **NÃO `.{}`**. O corpus (`src/`)
HOJE usa `self { }` (list/map). Duas invariantes: (a) o corpus não pode USAR `.{}` antes do seed o
entender; (b) o corpus **nunca pode ficar sem construtor** entre os dois passos. Logo, com o
parser em **janela aditiva** (aceita AMBOS, §12.1):

1. **R1 — CAPACIDADE aditiva.** Parser aceita `.{ }` **além** de `self { }`; checker resolve `.{}`
   (C1-C4) e **mantém** `self { }`. `src/` **inalterado** (ainda `self { }`). Build com o seed
   ATUAL (entende `self { }`; ignora `.{}` pois nada em `src/` o usa). **Reseed R1** → o novo seed
   entende `.{}` E `self { }`.
2. **R2 — SWEEP + REMOÇÃO.** Com o seed R1 (que entende `.{}`), reescrever `src/` `self { }`→`.{ }`
   (§3.4) E **remover** `self { }` do parser (`SelfKw`+`LBrace` → erro) e do checker
   (`is_self_sentinel`/`self_construct_target`/`self_sentinel_path`). Build com o seed R1 (entende
   `.{}`; o `src/` já não usa `self { }`). **Reseed R2** → o seed passa a REJEITAR `self { }`.

**Por que dois reseeds (confirmação):** não dá para sweepar `src/`→`.{}` antes do seed entender
`.{}` (invariante a) — logo a capacidade PRECEDE o sweep por um reseed. E a remoção do `self { }`
só é segura DEPOIS que `src/` deixou de usá-lo — logo remoção viaja COM o sweep (R2), nunca antes.
Entre R1 e R2 o corpus sempre tem construtor: `self { }` (pré-R2) ou `.{ }` (R2). A invariante (b)
do dono ("mesmo reseed" = capacidade `.{}` e a remoção do `self { }` na MESMA passada de
sweep-R2, sem um reseed intermediário que deixasse o corpus órfão) é satisfeita: R2 é atômico
(sweep+remoção+reseed). Ritual (gate cheio: compila + regressões + gen2==gen3) em R1 e em R2.

---

## 4. Sequência de crumbs (cada um gate-ável)

**Bloco R1 — capacidade aditiva `.{}` (parser+checker; `src/` intocado, `self { }` mantido):**

- **C1 — parse de `.{`** (`src/parser/parse_expr.tks`, `+ dot_sentinel_path`): novo ramo
  `Dot`+`LBrace` em `parse_atom` → `StructLit` com `type_path` vazio. `self { }` continua
  parseando (janela aditiva). Gate: parse-only.
- **C2 — `is_dot_sentinel` + `dot_construct_target`** (`typer.tks`, junto a `4298`/`4434`):
  resolve alvo de `expected`; erro sem-alvo (§2.4); reusa `explicit_inst_target` e a lógica
  phantom `self_inst_spelling` (§2.2). O ramo `is_self_sentinel` **permanece** aqui em R1.
- **C3 — threading de retorno** (`scope.tks` Env + `with_ret_expected`; `type_method`/
  `type_function`; `type_return`; cauda de `type_block`; `type_loop` limpa). O maior crumb.
  Introduzir um `type_expr_expected(e, expected, env, table)` fino que roteia struct/dot-lit por
  `type_value_expected` e o resto por `type_expr`, para a cauda de `if`/`match`.
- **C4 — argumento `.{}`** (`typer.tks:3287`): `+ arg_is_dot_init` na disjunção.
- **C5 — RESEED R1** (ritual: gate cheio + gen2==gen3). Depois: o seed entende `.{}`.

**Bloco R2 — sweep + remoção do `self { }` (com o seed R1):**

- **C6 — sweep do corpus** (§3.4): reescrever os 4 sítios de construção `.tks` `self { }`→`.{ }`
  (`list.tks:26`, `map.tks:44`, `type_overload/src/probe.tks:46,62`), atualizar os 2 `.tkr`
  (type_overload, type_overload_reject reenquadrado) + `self_free/case.tks`, o diagnóstico
  `resolve.tks:1265`, e os comentários stale. **NÃO tocar** os `operator … : self { <expr> }`
  (self é tipo de retorno, não construção). + fixtures novas (§5).
- **C7 — REMOÇÃO do `self { }` construtor** (OBRIGATÓRIO — o ex-crumb opcional, agora o ruling
  do dono): deletar do parser o ramo `SelfKw`+`LBrace` (`parse_expr.tks:457-459`) e
  `self_sentinel_path` (`:156`); do checker `is_self_sentinel` (`typer.tks:4298`),
  `self_construct_target` (`4349-4363`) e o ramo `is_self_sentinel` de `construct_target`
  (`4435`). `self { }` passa a ser **erro de parse**. **PRESERVAR** `self_inst_spelling`
  (`resolve.tks:2226`, reaproveitado por `.{}`). `self` receptor/tipo intocados. C6+C7 são uma
  passada atômica (não reseedar entre eles).
- **C8 — RESEED R2** (ritual: gate cheio + gen2==gen3). Depois: `self { }` rejeitado.

---

## 5. Fixtures de regressão (inputs → exit code nativo esperado)

Formato `.tkr` (Gherkin, `docs/design/tkr-regression-format.md`): `When compile/run`, `Then`
exit-code. Programas mínimos (namespaced, para exercitar o gap real do §1.4). Exit 0 = OK.

1. **`dot_static_self_ok`** — factory target-typed no retorno-`self`, namespaced:
   ```teko
   type Counter = struct {
       _n: i64
       static fn zero(): self { .{ _n = 0 } }
       exp get n(): i64 { self._n }
   }
   fn main() { var c = Counter::zero(); if c.n == 0 { exit(0) } else { exit(1) } }
   ```
   Esperado: **exit 0** (era o caso que falhava no type-check).
2. **`dot_binding_annotated`** — `var x: Counter = .{ _n = 7 }` → checa `x.n == 7` → **exit 0**.
3. **`dot_arg`** — `fn take(c: Counter): i64 { c._n }`; `take(.{ _n = 5 })` == 5 → **exit 0**.
4. **`dot_field_nested`** — `Outer { inner = .{ _n = 3 } }`; lê `o.inner.n` → **exit 0**.
5. **`dot_return_stmt`** — `static fn one(): self { return .{ _n = 1 } }` → **exit 0**.
6. **`dot_generic_self`** — `Cell<T>` com `static fn of(v: T): self { .{ _v = v } }`;
   `Cell<i64>::of(9)` → **exit 0** (exercita o phantom via `expected`).
7. **`dot_no_target_err`** (compile-fail) — `var x = .{ _n = 0 }` sem anotação →
   **compile error** com a mensagem de §2.4. `Then compile fails`. (Substitui o antigo
   `self_free` reject de `type_overload_reject`.)
8. **`dot_scrutinee_err`** (compile-fail) — `.{…}` como subject de `if` → **compile error**
   (não-`bool`), garantindo que o parse incondicional não abre buraco semântico.
9. **`self_ctor_removed_err`** (compile-fail, entra em R2) — `static fn zero(): self { self {
   _n = 0 } }` → **compile error de PARSE** (`self { }` não é mais construtor). Trava a remoção
   C7; garante que `self` receptor/tipo seguem válidos (o mesmo arquivo usa `self._n` e `(): self`
   sem erro).
10. **`intent_shape_ok`** (quando a Intent entrar) — os dois `new` da Intent em grafia `.{}` →
    **exit 0** (construção protegida via `.{}` com backing privados).
11. **`nominal_still_ok`** (não-regressão) — `Tipo { … }` nominal (com/sem type-args) continua a
    compilar/rodar como antes → **exit 0** (a forma que COEXISTE com `.{}`). **Não** inclui
    `self { }` (removido).

Adicionar, ainda, um teste de unidade `.tkt` em `src/parser/parser_test.tkt` para `.{` →
`StructLit` com `type_path` vazio (e, pós-C7, `self { }` → erro de parse), e em `src/checker`
para `dot_construct_target` (alvo resolvido de `expected`; erro sem-alvo).

---

## 6. Riscos + tensões de lei (com resolução)

- **R1 — `ret_expected` vazando para blocos não-cauda.** Se `ret_expected` não for limpo em
  corpo de loop / bloco não-cauda, um `.{}` mal-posto poderia herdar o alvo de retorno errado.
  Resolução: disciplina de set/clear de §3.1 (limpar em `type_loop`; manter só na cadeia de
  cauda). Coberto por fixture 8 e por um `.tkt` de bloco não-cauda. Baixo risco.
- **R2 — união como `expected`.** Um retorno `A | B` (dois structs) não dá alvo ÚNICO para
  `.{}`. Resolução (ratificada, §2.4): sem alvo único ⇒ erro sem-alvo; o dev escreve
  `Name { … }`. Consistente com a checagem uni-direcional. (Um `T | null` onde só `T` é
  construível PODE resolver a `T` — opcional; recomendo tratar como sem-alvo por simplicidade e
  reavaliar se o corpus pedir.)
- **R3 — owner genérico / phantom.** O retorno-`self` genérico hoje NÃO produz phantom
  (`typer.tks:7998`); `dot_construct_target` deve produzi-lo via `self_inst_spelling` quando
  `expected` é o owner corrente (§2.2). Fixture 6 (`Cell<T>::of`) trava isto. `self_inst_spelling`
  é PRESERVADO em C7 exatamente por isto — a única peça da máquina `self { }` que sobrevive.
- **R4 — sweep incompleto do `self { }`.** Um `self { }` de construção não reescrito viraria erro
  de parse pós-C7. Mitigação: o inventário EXATO (§3.4, `rg 'self\s*\{'` executado — 4 sítios de
  construção `.tks` + os `.tkr`) e a distinção explícita de `operator … : self { <expr> }` (self
  como TIPO, não construção — NÃO tocar). Fixture 9 + o gate cheio de R2 pegam qualquer resto.
- **T1 — tensão §13.2 × `self { }` retirado — RESOLVIDA na lei.** O ruling do dono (2026-08-13)
  já foi absorvido em Doc 2 §4.1 e §13.2 (commit `4b4781d4`): a construção passa a ser `.{ }`
  target-typed / `Tipo { }` nominal, e `self { }` construtor está formalmente RETIRADO. Não há
  mais tensão — a lei e este plano concordam. **Sem HALT.**
- **T2 — W15/Javadoc.** Todo `.tks` novo/tocado (parser ramo, checker fns, Env campo) leva
  Javadoc completo em CADA declaração. Os snippets deste doc já vêm nesse estilo para cópia
  verbatim.

Nenhuma tensão genuína permanece em aberto. Sem HALT.

---

## 7. Bloqueios / DESIGN-AHEAD

Nada BLOQUEADO. Todos os alvos (`.{}` parser, checker, threading, fixtures) são
implementáveis contra a superfície atual. A Intent em si (`intent_shape_ok`, fixture 9) depende
de §10.3/§11 entrarem — mas a FORMA da construção (`.{}`) e a reescrita doc-only já ficam
prontas aqui; o implementer da Intent copia verbatim quando ela abrir.
