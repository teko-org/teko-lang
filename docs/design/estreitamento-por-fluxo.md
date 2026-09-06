# Estreitamento por análise de fluxo — desenho e dimensionamento

Vagão: **por abrir** (o dono aprovou vagão próprio a 2026-07-29: *"Sim, estreitamento pode receber
um vagão para isso, mas a análise de fluxo precisa de correção, só precisa saber o tamanho"*).
Branch de desenho: `cargo/0.3.1-estreitamento-fluxo`, sobre `remodel/0.3.1.0-linux-native-2` (`9847fe6`).
Autor: arquitecto. Data: 2026-07-29.

Este documento **não implementa nada**. Entrega: o estado medido, o alcance delimitado, as
assinaturas, a resposta ao fixpoint, a lane recomendada, a sequência de crumbs e as fixtures
(positivas E negativas).

---

## 0. O ruling que origina o trabalho

O dono (2026-07-29):

```teko
let a: i32 | null = 0
if a != null {
  exit(a) // como null foi testado, 'a' está livre do tipo soma com a parte null (desenrola)
}
```
*"Este último funciona em match, mas deveria funcionar em um if"*, e depois **"Sim, estreitamento e
Sim, o fluxo também"**.

### Qual das quatro referências se espelha, e porquê

O digest de leis (`docs/memory/teko-laws-digest.md`) fixa: superfície → **Rust**, controlo → **Zig**,
addins → **C#**, comportamentos → **Go**. A obrigação anexa é nomear qual se espelha.

Aqui o eixo é o do **C#**: refinar **A MESMA variável** ao longo do fluxo. As outras três não fazem
isto — todas ligam um **nome NOVO**:

| referência | forma | refina `a`? |
|---|---|---|
| Rust | `if let Some(x) = a { … }` | não — liga `x` |
| Zig | `if (opt) \|v\| { … }` | não — liga `v` |
| Go | `if v, ok := m[k]; ok { … }` | não — liga `v` |
| **C#** | `if (a is not null) { a.Foo(); }` | **sim — refina `a`** |

**Confirmado no código, não presumido.** O `match` do Teko liga um nome novo, exactamente como
Rust/Zig/Go — `src/checker/match.tks:217`:

```teko
define(env, bp.binding, ct, false)   // `Type as name` → name : the case type (match-binding immutable — B.21)
```

e o braço `null` **não devolve env alterado** (`src/checker/match.tks:194`):

```teko
parser::NullPattern => {
    if variant_has_null_member(subject_variant) { return env }
    …
}
```

Medição que fecha a questão (gen1 construído de `bootstrap/teko.c`, backend C):

```
### match-binds-new-name:      teko: memory: peak 7.2 MB          ← COMPILA
### match-does-not-refine-a:   src/main.tks:2:33: exit's argument must be an integer status code
```
onde os dois programas são:
```teko
match a { null => { }; i32 as n => { exit(n) } }   // ok — nome NOVO
match a { null => { }; i32     => { exit(a) } }   // REJEITADO — `a` continua `i32 | null`
```

Ou seja: **o `match` do Teko também NÃO estreita `a`.** Estreita apenas por rebind. O pedido do dono
é genuinamente o eixo C#, e é novo no Teko em qualquer construção — `if` ou `match`.

---

## 1. O estado actual, medido

Ferramenta: `sh scripts/build_gen1_from_c.sh bootstrap/teko.c src .gen1` seguido de
`TEKO_BACKEND=c .gen1/teko . -o out --no-verify --release` sobre projectos mínimos.

### 1.1 Onde o `if` é tipado

`src/checker/typer.tks:3732` (`type_if`, forma-valor) e `src/checker/typer.tks:3755`
(`type_if_stmt`, forma-statement). Ambos tipam a condição e depois **tipam os dois ramos com o
MESMO `env` da entrada**:

```teko
fn type_if(f: parser::IfExpr, env: Env, table: TypeTable): TExpr | error {
    let c = match type_expr(f.cond, env, table) { TExpr as te => te; error as e => return e }
    if !is_bool(c.type) { return error { message = "an `if` condition must be a bool" } }
    if !f.has_else { return error { message = "an `if` used as a value needs an `else`" } }
    let tb = match type_block(f.then_blk, env, table) { TypedBlock as bk => bk; error as e => return e }
    let eb = match type_block(f.else_blk, env, table) { TypedBlock as bk => bk; error as e => return e }
```

`env` entra igual nos dois `type_block`. **Não existe canal por onde a condição informe os ramos.**
Esse é, literalmente, o buraco.

### 1.2 Existe QUALQUER refinamento de tipo por fluxo hoje? Não.

Sete programas medidos, todos rejeitados:

| caso | programa | resultado medido |
|---|---|---|
| ruling do dono | `if a != null { exit(a) }` | `src/main.tks:3:5: exit's argument must be an integer status code` |
| guarda precoce | `if a == null { exit(1) }` / `exit(a)` | `src/main.tks:3:1: exit's argument must be an integer status code` |
| ramo `else` | `if a == null { exit(1) } else { exit(a) }` | `src/main.tks:2:33: …` |
| `&&` | `if a != null && a > 0 { exit(a) }` | `src/main.tks:2:25: …` |
| negação | `if !(a == null) { exit(a) }` | `src/main.tks:2:19: …` |
| `loop`+`break` | `loop { if a == null { break }; exit(a) }` | `src/main.tks:2:32: …` |
| braço de `match` | `match a { null => {}; i32 => { exit(a) } }` | `src/main.tks:2:33: …` |

Zero estreitamento por fluxo. (Não existe `while` no Teko — `grep '"while"' src/lexer/*.tks` não dá
nada; o item "`while a != null`" do briefing é **vazio por construção**, e reduz-se ao caso
`loop { if … { break } }` acima.)

O doc-comment de `require_narrowed` (`src/checker/typer.tks:2088`) já **PROMETE** este
comportamento:

> *"The two accepted narrowing scopes rebind the value to its bare member BEFORE this point (a
> `match` arm's pattern binding; **the flow-narrowed branch of an equality guard**)"*

**Essa segunda frase é falsa hoje.** Não existe nenhum "flow-narrowed branch of an equality guard"
no código. Isto é dívida de documentação a corrigir junto com o trabalho (crumb 0).

### 1.3 PRÉ-REQUISITO BLOQUEANTE: `a != null` nem sequer compila hoje

Medido — o programa **sem qualquer estreitamento** já falha:

```teko
let a: i32 | null = 0
if a != null { exit(7) }
exit(3)
```
```
### plain-ne-null-if: teko: .: codegen: a bare `null` needs a known optional type from context (use it where a `T?` is expected)
```

O checker **aceita**; o **codegen rejeita**. Raiz encontrada (dois sítios):

1. `src/checker/typer.tks:591-596` — `adopt_binop_operands` faz um retype **RASO**, copiando
   `l.type` inteiro para o nó da direita:
   ```teko
   if literal_adopts(r, l.type) { return BinOperands { l = l; r = TExpr { kind = r.kind; type = l.type; line = r.line; col = r.col } } }
   ```
   O `null` fica com `kind = TNullLit` e `type = Variant{Null, i32}`.
2. `src/codegen/codegen.tks:7201-7204` — o emissor de `TNullLit` só aceita `Null`:
   ```teko
   checker::TNullLit => match e.type {
       checker::Null => cb(buf, "0")
       _ => error { message = "codegen: a bare `null` needs a known optional type from context (use it where a `T?` is expected)" }
   }
   ```

A função correcta já existe: `retype_literal` (`src/checker/typer.tks:4630`), cujo próprio
doc-comment diz *"a `null` leaf becomes `Null`"*. `adopt_binop_operands` devia chamá-la em vez de
fazer o retype raso.

> **Fronteira de lane.** A comparação `x != null` é território da lane
> `cargo/0.3.1-comparacoes` (o agente que mede os quatro casos do dono). **Não dupliquei a medição
> dele**; entrego-lhe a raiz acima, que é o que ele precisa. O estreitamento **depende** desta
> correcção: sem ela, nenhum programa estreitado chega a produzir binário. Isto é uma ordenação
> entre lanes, não uma escolha de desenho.

---

## 2. A análise de fluxo que JÁ EXISTE — avaliação

O dono disse *"a análise de fluxo precisa de CORREÇÃO"*, não "de construção". Tem razão: existe
maquinaria carregada. Inventário:

| função | ficheiro:linha | o que faz |
|---|---|---|
| `texpr_diverges` | `typer.tks:3281` (pub) | uma expressão termina o frame? (`panic`/`exit`, if/match totalmente divergente) |
| `tblock_diverges` | `typer.tks:3299` | um bloco diverge? |
| `must_free_consumed_on_all_paths` | `typer.tks:3364` | travessia por CAMINHOS de um bloco, procurando consumo do `#must_free` |
| `stmt_diverges_standalone` | `typer.tks:3398` | um statement isolado diverge (sem `return`) |
| `stmt_branches_all_consume_must_free` | `typer.tks:3494` | recursão em if/match |
| `match_arms_all_consume_must_free` | `typer.tks:3514` | recursão nos braços |
| `cg_block_diverges` | `codegen.tks:6687` | **cópia** de `tblock_diverges` |
| `cg_expr_diverges` | `codegen.tks:3986` | **cópia** de `texpr_diverges` |

Usos: 23 no `typer.tks`, mais `lir/lower.tks:3331` e `codegen.tks:8396`.

### 2.1 Quanto do que o estreitamento precisa já lá está?

**Muito pouco, e a parte que interessa está na função errada.**

- `must_free_consumed_on_all_paths` **é** análise de fluxo a sério — percorre statement a statement,
  desce em if/match, e tem noção de "todos os caminhos". Mas é **per-binding, por NOME de string**,
  procura um predicado fixo (consumo), e é **read-only sobre o env**: devolve `bool`, não devolve
  ambiente. Não há nada nela que se possa reaproveitar directamente para propagar factos de tipo.
- `tblock_diverges`/`texpr_diverges` **são** exactamente o que o estreitamento por `return` precoce
  precisa (para saber que `if a == null { return }` fecha o ramo). Essa peça, sim, é reutilizável —
  e é precisamente a que está com defeito.

Veredicto: o estreitamento **precisa** dos predicados de divergência (que existem e estão partidos)
e **não** consegue reaproveitar o walker do `#must_free` (que está correcto dentro dos seus limites
declarados, mas resolve outro problema).

### 2.2 O que está ERRADO. Três defeitos, todos medidos.

#### Defeito D1 — `tblock_diverges` é um espreitadela ao ÚLTIMO statement, não uma travessia

`src/checker/typer.tks:3299`:
```teko
fn tblock_diverges(stmts: []TStatement): bool {
    if stmts.len == 0 { return false }
    match stmts[stmts.len - 1] {   // ← SÓ o último
```

Consequência medida — dois programas **semanticamente idênticos**, um compila e o outro não:

```
### B3-match-diverge-last:  teko: memory: peak 7.3 MB     ← COMPILA
### B2-match-diverge-mid:   src/bottom.tks:1:8: the function's final expression does not match its declared return type
### B4-match-return-mid:    src/bottom.tks:1:8: (idem)
### B5-all-arms-diverge-mid: src/bottom.tks:1:8: (idem)
### C1-if-value-diverge-mid: src/bottom.tks:1:8: (idem)
### C2-if-value-diverge-last: teko: memory: peak 6.6 MB   ← COMPILA
```
Os programas:
```teko
pub fn f(c: i32): i32 { match c { 1 => { exit(1) };                _ => { 2 } } }  // ok
pub fn f(c: i32): i32 { match c { 1 => { exit(1); let z: i32 = 0 }; _ => { 2 } } }  // REJEITADO
pub fn f(c: i32): i32 { match c { 1 => { return 1; let z: i32 = 0 }; _ => { 2 } } } // REJEITADO
```

**Isto é falsa-rejeição pura** (o compilador recusa programa correcto), e é o defeito de que o dono
falava. Não é "conservador"; é um peephole a fingir-se de análise.

Nota anexa medida: **não há diagnóstico de código inalcançável**. `C3-unreachable-after-return`
(`return c` seguido de mais statements) compila em silêncio. Isto é *reportado*, não convertido em
issue por mim.

#### Defeito D2 — `texpr_diverges` reconhece `panic`/`exit` por NOME NU. Buraco de solidez.

`src/checker/typer.tks:3281`:
```teko
TCall as c => c.callee.segments.len == 1 && (c.callee.segments[0].name == "panic" || c.callee.segments[0].name == "exit")
```

Nenhuma verificação de que o nome resolve ao builtin. Uma `fn exit` do utilizador **coexiste** com o
builtin e **ganha** a resolução. Medido:

```teko
// src/bottom.tks
pub fn exit(code: i32): i32 { code + 40 }
pub fn f(c: i32): i32 { if c == 1 { exit(1) } else { 2 } }
// src/main.tks
exit(f(1))
```
```
$ ./out/q ; echo $?
81
```
`41` do `f`, depois `81` do `main` — a `exit` do utilizador correu **duas vezes**. O checker
continua a tratar ambas as chamadas como divergentes.

Escalado até partir o tipo:
```teko
pub fn exit(code: i32): str { "shadowed" }
pub fn f(c: i32): i32 { match c { 1 => { exit(1) }; _ => { 2 } } }
```
```
checker    2/2 items   0.0s  ✓                      ← o CHECKER ACEITA
out/q.c:20:20: error: incompatible types when returning type 'tk_str' but 'int32_t' {aka 'int'} was expected
   20 |             return teko_q__exit(((int32_t)1ULL));
```

**O checker aceitou um programa mal-tipado.** O braço devolve `str` onde o `match` tem de dar
`i32`; foi ignorado na unificação (`type_match`, `typer.tks:3795`, `if !tblock_diverges(…)`) porque
a análise de fluxo mentiu. Só o `cc` o apanhou — e o backend **nativo não tem `cc`**. No caminho
nativo do degrau 18 isto é miscompilação silenciosa. É o pior tipo de falha, e é o mesmo tipo de
falha que um estreitamento demasiado permissivo produziria.

#### Defeito D3 — a análise "todos os caminhos retornam" (M.4) não existe, e o emissor sabe disso

`src/lir/lower.tks:4318-4326`, palavras do próprio código:

> *"`check_trailing_value` (typer.tks) makes NO claim whatsoever about a non-expression tail … so
> the checker ALREADY admits, e.g., a breakless `loop` whose only exits are inner `return`s,
> **TRUSTING the programmer** that every reachable path actually diverges (**M.4, the full
> every-path-returns analysis, is a documented SEPARATE later item**). The C emitter honors that
> exact same trust: `cg_stmt_c_terminates` returns `false` … and `emit_function_cov` follows a
> `false` with a bare `__builtin_unreachable()` — no value materialized, **the fall-through is UB
> IF the trust is ever violated**"*

M.4 é a análise de fluxo em falta. O estreitamento por `return`/`break`/`continue` precoce — que é o
padrão que mais aparece em código real — **é o mesmo grafo** que M.4 percorre. Fazer os dois
separadamente é pagar duas vezes.

#### Não-defeitos (avaliados e considerados sãos)

- **Os limites do `#must_free`** (não desce em `loop`; `return outro` conta como drop; sem
  points-to) estão **declarados por escrito** no doc-comment de `must_free_consumed_on_all_paths`
  e em `docs/design/memory-unsafe-backend-remodel.md` §5a. São limites honestos e **conservadores**
  (rejeitam a mais, nunca aceitam a menos). Não são defeitos; são âmbito.
- Os braços `TBreakStmt`/`TContinueStmt` de `stmt_diverges_standalone` são inalcançáveis hoje — o
  próprio doc-comment diz que estão lá por compatibilidade futura. Correcto e documentado.

### 2.3 O estreitamento estende a análise existente ou vive ao lado?

**Recomendação: as duas coisas, separadas por camada.**

- **Reutilizar** (corrigindo) `texpr_diverges`/`tblock_diverges`. São predicados `-> bool` sobre a
  TAST; o estreitamento precisa exactamente deles para o `return` precoce. Corrigi-los é obrigatório
  de qualquer maneira (D1 é falsa-rejeição, D2 é buraco de solidez), e beneficia os 23 sítios em vez
  de os pôr em risco: D1 só faz `tblock_diverges` devolver `true` em casos onde hoje devolve `false`
  erradamente; D2 só faz `texpr_diverges` devolver `false` onde hoje mente. Nenhum sítio que hoje
  esteja correcto muda de resposta.
- **Ao lado**: a propagação de factos (`NarrowSet`) é estrutura NOVA, transportada no `Env`. Não
  toca em `must_free_consumed_on_all_paths` nem no seu walker. Zero risco para os 23 sítios.

Enfiar o estreitamento dentro do walker do `#must_free` seria acoplar um problema de tipos a um
problema de memória, com um único walker a servir dois predicados incompatíveis. Rejeitado.

---

## 3. O alcance — o que ENTRA e o que FICA DE FORA

Princípio de decisão, e é o único que uso: **um estreitamento que aceita a mais é pior do que não
ter estreitamento**. Cada forma só entra se a sua condição de invalidação for decidível localmente
com o que o checker já sabe.

### 3.1 ENTRA

| # | forma | argumento |
|---|---|---|
| E1 | `if a != null { … }` — ramo `then` estreita | o ruling literal do dono |
| E2 | `if a == null { … } else { … }` — ramo `else` estreita | o dual exacto de E1; excluí-lo seria arbitrário e obrigaria a escrever `!=` só para agradar ao compilador |
| E3 | `if a != null { … } else { … }` — o `else` estreita a `null` | mesmo mecanismo, sinal invertido. Útil e grátis |
| E4 | guarda precoce: `if a == null { return }` / `{ exit(1) }` / `{ break }` / `{ continue }`, e daí para baixo `a` estreito | **o padrão dominante em código real** e onde o C# brilha. Depende de D1+D2 corrigidos, o que é a razão de os corrigir primeiro |
| E5 | `&&`: `if a != null && a > 0 { … }` | o operando direito de `&&` já vive sob o facto do esquerdo (curto-circuito), e o ramo `then` sob a conjunção. Composição de conjuntos: união dos factos |
| E6 | `\|\|` na posição **dual**: `if a == null \|\| a == 0 { … } else { … }` — o `else` estreita | dual de De Morgan de E5. Cai do mesmo mecanismo se este for feito com dois conjuntos (verdadeiro/falso) desde o início |
| E7 | negação: `if !(a == null) { … }` | trocar os dois conjuntos. Uma linha, se a estrutura tiver os dois lados desde o início |
| E8 | **união de TRÊS ou mais membros**: `i32 \| str \| null` estreitado de `null` dá `i32 \| str`, **não um tipo só** | tem de estar no desenho **desde o início**, como o briefing exige. Ver §3.3 |
| E9 | o mesmo estreitamento visível ao `match` como subject (`match a { … }` sobre `a` já estreito) | consequência automática: o subject é tipado no env estreitado |

### 3.2 FICA DE FORA (com argumento — não é preguiça)

| # | forma | porque fica de fora |
|---|---|---|
| F1 | `while a != null` | **não existe `while` no Teko** (medido: nenhuma keyword `while` no lexer). Item vazio. O `loop { if a == null { break }; … }` é E4 |
| F2 | estreitamento sobre **campos**: `if p.x != null { use(p.x) }` | exige identidade de lvalue e invalidação por qualquer escrita a `p` ou a alias de `p`. O Teko não tem points-to (o próprio `#must_free` declara isso como limite). Não é decidível localmente ⇒ **aceitaria a mais**. Fora |
| F3 | estreitamento sobre **índices**: `if v[i] != null { use(v[i]) }` | idem, agravado: `i` pode mudar. Fora |
| F4 | estreitamento **entre iterações** de `loop` | um `continue`/re-entrada re-executa o corpo com `a` possivelmente reatribuído. Manter um facto vivo por cima do topo do `loop` exige ponto fixo. Regra em vez disso: **entrar num `loop` LIMPA todos os factos sobre variáveis `mut`** (factos sobre `let` sobrevivem, ver §3.4). Conservador e trivialmente correcto |
| F5 | estreitamento por **`when` guard** de um braço de `match` | o guard é ortogonal ao pattern; misturá-lo com o pattern-bind existente duplica caminhos de estreitamento no mesmo nó. Fora do primeiro vagão; nomeado aqui como extensão futura óbvia |
| F6 | estreitamento **inter-procedimental** (`if is_some(a) { … }`) | exige contratos de predicado (o `[NotNullWhen]` do C#). Grande, e ortogonal. Fora |
| F7 | estreitamento por comparação a um **case não-`null`** (`if x == Foo { … }`) | um `Variant` de casos nominais não é comparável por `==` hoje (`is_comparable`, `expr.tks:51`, exige `type_eq`). Não há forma de sintaxe. Fora |
| F8 | `T?` (`Optional`) além de `T \| null` (`Variant` com membro `Null`) | são dois modelos distintos no checker (`type.tks:87` vs `type.tks:127`). O ruling do dono é sobre `i32 \| null`, que é `Variant`. Cobrir `Optional` no mesmo vagão duplica todo o eixo de teste. Fora, **mas** as assinaturas de §4 são agnósticas: `NarrowFact` guarda um `Type`, não um "membro removido" |

### 3.3 A união de três membros — o caso que define a estrutura

`let a: i32 | str | null = 0` resolve para `Variant { members = [Null{}, Prim{I32}, Str{}] }` — o
`null` é normalizado para primeiro membro (`resolve.tks:1612`, `union_normalize_null`). Estreitar
de `null` **remove um membro**, dando `Variant { members = [Prim{I32}, Str{}] }` — ainda um
`Variant`, não um tipo escalar.

Consequências que o desenho tem de honrar desde o crumb 1:

1. A operação primitiva é **`variant_without_null(t): Type`**, não "desembrulhar o par".
2. Quando restar **um único** membro, o resultado **colapsa** ao membro nu (`i32 | null` sem `null`
   dá `i32`, não `Variant{i32}`) — senão `exit(a)` continua a falhar, e o ruling do dono não fecha.
3. O ramo negativo (`if a == null { … }` no `then`) estreita para **`Null{}`** puro,
   independentemente de quantos membros a união tinha.
4. Um `Variant` **sem** membro `null` produz **facto nenhum** — `if a != null` sobre `i32` puro não
   é sequer comparável (`is_comparable` já rejeita), portanto não chega cá.

Se isto não estiver no crumb 1, o crumb que acrescenta o terceiro membro reescreve tudo. Por isso
está.

### 3.4 O que INVALIDA um facto de estreitamento — a parte perigosa

Regra-mãe: **um facto sobrevive enquanto se puder PROVAR que a variável não mudou.** Na dúvida,
apaga-se. Quatro fontes de invalidação, e o que o Teko já garante sobre cada uma:

| fonte | regra | o que a torna decidível |
|---|---|---|
| **I1 — atribuição directa** `a = …` no bloco | o facto sobre `a` **morre no ponto da atribuição**; os statements seguintes vêem `a` com o tipo declarado. Uma re-atribuição a um valor não-`null` **não** reinstala o facto (fica para o vagão seguinte — conservador) | `TAssign` com `kind = Simple` e `name == a`, visível directamente no walker de statements |
| **I2 — `a` é `let`, não `mut`** | **imune**. Um `let` nunca é reatribuído (B.21), e nenhum dos I1/I3/I4 se lhe aplica. Os factos sobre `let` **sobrevivem a tudo**, incluindo entrar em `loop` | `ValBinding.is_mut == false` (`scope.tks:24`) |
| **I3 — chamada que possa mudar `a`** | **NÃO invalida**, e isto é uma prova, não uma aposta: o Teko não tem out-params, não tem passagem por referência implícita, e um `mut` local não pode ser observado por um callee **excepto** via a keyword `ref` (internamente `Type::Reference`). Regra operacional: um facto sobre `a` morre numa chamada **se e só se** `a` for de tipo `Reference` ou o seu endereço tiver sido tomado (`Borrow`) algures na função | `Type::Reference` (`type.tks:127`); `parser::Borrow` (`ast.tks:258`) é uma forma sintáctica localizável |
| **I4 — captura por lambda** | quase-imune, **e isto foi verificado**: as capturas são **por CÓPIA** (`tast.tks:94`) e o checker **rejeita** escrita a capturada por cópia (`typer.tks:200`, `lam_reject_copy_capture_write`): *"a lambda cannot assign to the captured variable `{nm}` — the capture is by COPY, so the write would be lost"*. Só uma captura `by_ref` (i.e. a variável é `ref`, i.e. `Type::Reference` internamente) escapa — e essa já cai em I3 | `lam_reject_copy_capture_write`, `typer.tks:200-215` |

**Regra final de invalidação, na forma implementável:**

```
um facto sobre `a` sobrevive a um statement S  SSE
    (a é `let`)                                            → sempre sobrevive
  OU (a é `mut` E a.type não é Reference
      E `a` nunca aparece sob `&`/Borrow nesta função
      E S não é uma atribuição a `a`)
```
e adicionalmente: **entrar num corpo de `loop` limpa todo o facto sobre um `mut`** (F4).

Esta regra é conservadora nos dois eixos que interessam: pode apagar um facto que ainda era válido
(falsa-rejeição, visível, o autor escreve o `match`), e **não pode** manter um facto inválido.

---

## 4. Onde vive — a estrutura e as assinaturas

### 4.1 Onde nasce, onde morre

- **Nasce** em `type_if`/`type_if_stmt`, ao tipar a condição: a condição produz, além do `TExpr`,
  **dois** conjuntos de factos — o que vale se ela for verdadeira e o que vale se for falsa.
- **Vive** no `Env`, num campo novo. O `Env` já é o transportador de factos-por-item
  (`cur_ns`, `owner_type`, `fn_unsafe`, `file`) e já é copiado por valor a cada fork de âmbito —
  exactamente a semântica que o estreitamento quer.
- **Morre** ao sair do bloco (o `Env` do bloco é descartado — `type_block` devolve `TypedBlock` cujo
  `env` o chamador só usa para statements irmãos), e no ponto de invalidação (§3.4).
- **Atravessa** o corpo de um bloco pelo acumulador que **já existe**: `type_block`
  (`typer.tks:3686-3728`) já faz `cur = ts.env` a cada statement. O estreitamento por `return`
  precoce (E4) entra exactamente aí: depois de tipar um `if` cujo `then` diverge, `cur` passa a ser
  `cur` mais os factos do ramo FALSO da condição. **Não é preciso passe novo, nem walker novo, nem
  segunda travessia.** Esta é a razão pela qual E4 sai barato apesar de ser a forma mais valiosa.

### 4.2 Assinaturas — todas em Javadoc completo (W15), para copiar verbatim

Ficheiro **novo**: `src/checker/narrow.tks` (namespace `teko::checker`).

```teko
/**
 * NarrowFact — one flow-derived refinement of ONE local binding: the name, and the type that
 * binding provably has at this point in the control flow, which is STRICTLY narrower than its
 * declared type. Produced by a condition that tested the binding (`a != null`), consumed by
 * `lookup_binding` when the body reads the name.
 *
 * The narrowed type is carried WHOLE (a `Type`, not a "removed member" list) so a three-member
 * union narrows to a still-union `i32 | str` exactly as a two-member union narrows to a bare
 * `i32` — one representation, no special case for arity.
 *
 * @param name  the narrowed local binding's name; matches `ValBinding.name`
 * @param type  the binding's proven type in this flow region (never the declared type itself)
 * @since 0.3.2 (flow narrowing)
 */
pub type NarrowFact = struct {
    name: str
    type: Type
}

/**
 * NarrowSet — the facts in force at one point of the control flow, newest last. A flat list, in
 * the SAME shape and for the same reason as `Env.bindings`: the sets are tiny (a handful of
 * entries in the deepest real nesting), and a flat list makes the fork-per-branch a cheap copy
 * with no shared mutable state to unwind.
 *
 * The EMPTY set is the overwhelmingly common case (any function with no null-union guard), and
 * an empty `NarrowSet` makes every consumer a single length test — so a corpus with no narrowing
 * types byte-identically to today.
 *
 * @param facts  the refinements in force; a later entry for the same name SHADOWS an earlier one
 * @since 0.3.2 (flow narrowing)
 */
pub type NarrowSet = struct {
    facts: []NarrowFact
}

/**
 * CondFacts — what a boolean condition proves, on BOTH of its outcomes. Both sides are carried
 * from the start because every interesting form needs both: `if a == null { … } else { … }`
 * narrows on `when_false`, `!(…)` swaps the two, and `||` composes on `when_false` exactly as
 * `&&` composes on `when_true` (De Morgan). A design that carried only the true side would have
 * to be rewritten to add `else`.
 *
 * @param when_true   the facts in force in the branch taken when the condition holds
 * @param when_false  the facts in force in the branch taken when it does not
 * @since 0.3.2 (flow narrowing)
 */
pub type CondFacts = struct {
    when_true: NarrowSet
    when_false: NarrowSet
}

/**
 * narrowset_empty — the no-facts set, the state every function body starts in and the value every
 * consumer short-circuits on.
 *
 * @return  a `NarrowSet` holding no refinements
 * @since 0.3.2 (flow narrowing)
 */
pub fn narrowset_empty(): NarrowSet

/**
 * narrowset_lookup — the proven type of `name` under `ns`, or `null` when no fact refines it.
 * Scans newest-first so a nested guard's fact shadows an outer one, mirroring `lookup_binding`'s
 * innermost-first rule over `Env.bindings`.
 *
 * @param ns    the facts in force at the read site
 * @param name  the binding being read
 * @return      the refined type, or `null` when the declared type stands
 * @since 0.3.2 (flow narrowing)
 */
pub fn narrowset_lookup(ns: NarrowSet, name: str): Type | null

/**
 * narrowset_add — `ns` extended with one refinement. A later fact for the same name shadows the
 * earlier rather than replacing it in place, so the set is append-only and a branch's fork never
 * mutates its parent's view.
 *
 * @param ns    the facts in force before the new one is proven
 * @param name  the binding the new fact refines
 * @param t     the binding's proven type
 * @return      `ns` with the new fact appended
 * @since 0.3.2 (flow narrowing)
 */
pub fn narrowset_add(ns: NarrowSet, name: str, t: Type): NarrowSet

/**
 * narrowset_union — every fact of `a` plus every fact of `b`, `b` winning on a name both refine.
 * The composition rule for `&&` on the true side and for `||` on the false side: both operands'
 * proofs hold simultaneously in that branch.
 *
 * @param a  the left operand's facts
 * @param b  the right operand's facts
 * @return   the combined facts
 * @since 0.3.2 (flow narrowing)
 */
pub fn narrowset_union(a: NarrowSet, b: NarrowSet): NarrowSet

/**
 * narrowset_drop — `ns` with every fact about `name` removed. The invalidation primitive: called
 * when a `mut` binding is assigned, when its address may have been taken, and when a `loop` body
 * is entered (every `mut` fact dropped at once via `narrowset_drop_mut`).
 *
 * @param ns    the facts in force
 * @param name  the binding whose proof is no longer sound
 * @return      `ns` without any fact about `name`
 * @since 0.3.2 (flow narrowing)
 */
pub fn narrowset_drop(ns: NarrowSet, name: str): NarrowSet

/**
 * narrowset_drop_mut — `ns` with every fact about a `mut` binding removed, `let` facts kept. The
 * `loop`-entry rule (a `mut` may be reassigned by a later iteration, so no fact about one may
 * cross the loop's back edge; a `let` can never be reassigned — B.21 — so its fact is sound in
 * every iteration).
 *
 * @param ns   the facts in force outside the loop
 * @param env  the environment naming which bindings are `mut`
 * @return     `ns` restricted to facts about immutable bindings
 * @since 0.3.2 (flow narrowing)
 */
pub fn narrowset_drop_mut(ns: NarrowSet, env: Env): NarrowSet

/**
 * cond_facts — what the typed condition `c` proves on each outcome, given `env` (which names the
 * bindings and their declared types) and `table` (which expands a named union to its members).
 *
 * Recognized forms, and nothing else — every unrecognized condition yields two EMPTY sets, which
 * is the sound answer (no proof, no refinement):
 *
 *   `x != null`  /  `null != x`   → true: `x` without its null member; false: `x` is `Null`
 *   `x == null`  /  `null == x`   → the swap of the above
 *   `!e`                          → `cond_facts(e)` with its two sides swapped
 *   `p && q`                      → true: union of both; false: empty (either may have failed)
 *   `p || q`                      → false: union of both negatives; true: empty
 *
 * `x` must be a BARE local binding (`TVar`) — a field, an index, or any other lvalue yields no
 * fact, because the checker has no points-to information with which to invalidate one.
 *
 * @param c      the already-typed condition expression
 * @param env    the environment in force where the condition is written
 * @param table  the type table, for expanding a named union to its members
 * @return       the facts proven on each outcome; both empty when nothing is proven
 * @since 0.3.2 (flow narrowing)
 */
pub fn cond_facts(c: TExpr, env: Env, table: TypeTable): CondFacts

/**
 * type_without_null — `t` with its `null` member removed, COLLAPSING to the bare member when one
 * remains: `i32 | null` yields `i32`, `i32 | str | null` yields `i32 | str`. A `t` carrying no
 * null member yields `null` (no refinement is available), as does a `t` that is nothing but
 * `null`.
 *
 * The collapse is what makes the owner's ruling close: without it, `exit(a)` inside the narrowed
 * branch would still see a one-member `Variant` rather than a plain `i32`.
 *
 * @param t      the declared union type
 * @param table  the type table, for expanding a named union alias to its members
 * @return       the null-free type, or `null` when `t` carries no null member to remove
 * @since 0.3.2 (flow narrowing)
 */
pub fn type_without_null(t: Type, table: TypeTable): Type | null

/**
 * fn_takes_address_of — does the function body `stmts` anywhere take the address of `name` (a
 * `&name` borrow) or bind it to a `Reference`? A binding whose address escapes may be written
 * through a `ref` binding (internally `Type::Reference`) by any callee, so no flow fact about it survives a call. Computed ONCE per
 * function body and consulted for every candidate fact, rather than re-walked per guard.
 *
 * @param stmts  the function body's typed statements
 * @param name   the candidate binding's name
 * @return       true iff a fact about `name` must not survive a call
 * @since 0.3.2 (flow narrowing)
 */
pub fn fn_takes_address_of(stmts: []TStatement, name: str): bool
```

### 4.3 Como se compõe com o `Env` que já existe

**Um campo, exactamente no molde dos quatro que já lá estão.** `src/checker/scope.tks:69`:

```teko
pub type Env = struct { base: []ValBinding; bindings: []ValBinding; cur_ns: str; owner_type: str; di: DiRegistry; injects: []DiInjectBind; fn_unsafe: bool; file: str; narrow: NarrowSet }
```

mais o setter no molde de `with_fn_unsafe`/`with_file`:

```teko
/**
 * with_narrow — set the flow-narrowing facts in force for the region about to be typed. The twin
 * of `with_ns`/`with_file`/`with_fn_unsafe`, set from the same kind of place and for the same
 * reason: a per-REGION fact that the deep expression typers need without a parameter threaded
 * through every one of them. Unlike those three (which are per-ITEM), this one changes at every
 * branch — which is precisely why `Env` being a copied value type is the right carrier.
 *
 * @param env     the environment to derive from
 * @param narrow  the facts proven to hold in the region about to be typed
 * @return        `env` with `narrow` set
 * @since         0.3.2 (flow narrowing)
 */
pub fn with_narrow(env: Env, narrow: NarrowSet): Env
```

**Um único ponto de consumo**, e é o que faz o custo ser pequeno: `lookup_binding` (`scope.tks`).
Todo o resto do checker lê tipos de variáveis por lá. `type_var` não muda; `type_field_access` não
muda; `type_call` não muda. O que muda é o que `lookup_binding` devolve.

```teko
/**
 * lookup_binding — the binding named `name`, with any flow-narrowing fact in force APPLIED to its
 * type. Scans `bindings` innermost-first, then `base`, exactly as before; the only change is that
 * a name refined by `env.narrow` is returned carrying its PROVEN type rather than its declared
 * one. `is_mut`/`ns`/`is_const`/`vis` are untouched — narrowing refines the TYPE and nothing else,
 * so a narrowed `mut` is still assignable (and assigning to it drops the fact).
 *
 * A binding with no fact, and every lookup under an empty `NarrowSet`, takes the identical path it
 * took before flow narrowing existed.
 *
 * @param env   the environment to search
 * @param name  the identifier being read
 * @return      the binding (narrowed if a fact refines it), or an error when unbound
 * @throws      error naming the unbound identifier
 * @since       0.3.2 (flow narrowing — the narrow-aware return)
 */
pub fn lookup_binding(env: Env, name: str): ValBinding | error
```

### 4.4 Os três pontos de produção

1. **`type_if_stmt`** (`typer.tks:3755`) — a forma que o ruling do dono usa. Depois de tipar `c`:
   ```teko
   let cf = cond_facts(c, env, table)
   let tb = match type_block(f.then_blk, with_narrow(env, narrowset_union(env.narrow, cf.when_true)), table) { … }
   ```
   e simetricamente para o `else` com `cf.when_false`.
2. **`type_if`** (`typer.tks:3732`) — idêntico, forma-valor.
3. **`type_block`** (`typer.tks:3686`) — o E4. No acumulador `cur`, depois de tipar um `if` SEM
   `else` cujo `then_blk` diverge (`tblock_diverges`, já corrigido por D1), estender `cur` com
   `cf.when_false`. É aqui, e só aqui, que o `return` precoce entra.

Invalidação: em `type_statement`, no braço `TAssign` com `kind = Simple`, aplicar
`narrowset_drop(env.narrow, a.name)` ao env devolvido; no braço `TLoopStmt`, tipar o corpo sob
`narrowset_drop_mut(env.narrow, env)`.

---

## 5. O FIXPOINT — confirmado ADITIVO, com uma ressalva nomeada

**A tua leitura está certa, e confirmo-a — mas não pela razão geral "estreitamento só aceita mais".
Essa razão é insuficiente, e digo porquê.**

O que torna o estreitamento **não-aditivo** noutras linguagens é a **resolução de sobrecarga**: se
`a` passa de `i32 | null` para `i32`, uma chamada `f(a)` pode passar a resolver noutra sobrecarga, e
um programa que já compilava muda de SIGNIFICADO. Isso partiria `gen2 == gen3` e obrigaria a bump
com a semente a acompanhar, exactamente como o `.len` a contar caracteres.

**No Teko isso não pode acontecer, e é uma propriedade verificável do checker, não uma esperança:**

1. **Não há sobrecarga por tipo de argumento.** A resolução de chamada é por NOME + namespace
   (`#41`, `Env.cur_ns`); `ValBinding` guarda **um** `type` por nome. Um argumento com tipo
   diferente não escolhe outra função — ou encaixa no parâmetro declarado, ou é erro.
2. **A adopção de literais é dirigida pelo DESTINO, não pela origem** (`literal_adopts` /
   `retype_literal`, `typer.tks:4549`/`4630`). Estreitar a origem não muda o destino.
3. **Só se estreita o que hoje é ERRO.** Um `T | null` não pode ser desreferenciado
   (`require_narrowed`, `typer.tks:2097`), não é aceite onde `T` é pedido (medido: `let b: i32 = a`
   → *"value type does not match annotation"*), e não é comparável a nada excepto ao seu próprio
   `Variant`. **Todo** sítio onde um valor estreitado passa a ser aceite é um sítio onde hoje há
   diagnóstico. Um programa que hoje compila **não tem** nenhuma dessas construções.
4. **A única variação observável seria a resolução de MÉTODO por tipo de receptor** (`type_method_call`
   sobre `Named`). Mas um `T | null` **não tem** métodos hoje (`type_field_access` para em
   `require_narrowed` antes de chegar ao corpo do struct, `typer.tks:2140`). Não há método a mover.

**Conclusão: ADITIVO. Não parte `gen2 == gen3`; não precisa de bump com a semente a acompanhar.**

**Ressalva, e é sobre a CORRECÇÃO, não sobre o estreitamento:**

- **D1** (`tblock_diverges` a percorrer o bloco) é **puramente aditivo**: só passa a devolver `true`
  onde hoje devolve `false` erradamente, e só em programas que hoje são REJEITADOS (medido: B2/B4/
  B5/C1 falham). Nenhum programa que hoje compila muda.
- **D2** (`texpr_diverges` a exigir que `exit`/`panic` resolvam ao builtin) **NÃO é aditivo**: passa
  a **REJEITAR** o programa que hoje é aceite e mal-compilado (o teste do `exit` sombreado). É a
  correcção de um buraco de solidez, e portanto é **subtractiva por desenho**. O corpus do compilador
  não define `fn exit`/`fn panic` (verificar com um `grep` no crumb 2), pelo que o `gen2 == gen3` do
  próprio compilador não é afectado — mas a mudança **é** uma quebra de compatibilidade para
  qualquer programa lá fora que sombreie esses nomes, e **isso** merece a nota de release.

Isto separa as duas parcelas também no eixo do fixpoint, e é mais uma razão para o dono poder
escolher a ordem.

---

## 6. Em que lane cabe

**Recomendação: vagão PRÓPRIO, a seguir a `0.3.1.0`. Nem aqui, nem "depois" indefinido.**

Argumento, em três factos:

1. **Não é ortogonal a esta lane — é ANTAGÓNICO a ela no eixo do risco.** A lane `0.3.1.0` é "Linux
   gera NATIVO", está no degrau 18, e a auto-construção completa **não fecha**. O estreitamento toca
   `Env` (um tipo que atravessa todo o checker) e `lookup_binding` (o sítio mais quente do checker).
   Meter isso num vagão cuja bissecção de falhas nativas ainda está aberta significa que qualquer
   regressão passa a ter duas causas candidatas. Isso não é preferência; é o custo de bissecção a
   duplicar.
2. **Mas a CORRECÇÃO D2 tem um argumento genuíno para vir para ESTA lane, e é forte.** O buraco de
   solidez do `exit` sombreado é hoje apanhado pelo `cc` — e o backend **nativo não tem `cc`**. No
   caminho que esta lane está a construir, o mesmo programa **miscompila em silêncio**. Um degrau de
   nativo assente numa análise de fluxo que mente é um degrau que vai ter de ser refeito.
   Recomendação: **D2 (e só D2) é candidato a esta lane**, como correcção de solidez do nativo, não
   como parte do estreitamento. D1 e D3 e o estreitamento vão para o vagão próprio.
3. **Nunca "depois".** D3 (M.4) já está declarado como *"a documented SEPARATE later item"* há
   várias versões, e o efeito é `__builtin_unreachable()` sobre confiança não verificada. Adiar mais
   uma vez tem custo composto: quanto mais degraus nativos assentarem em cima, mais caro é.

Ordem recomendada ao dono: **D2 aqui (solidez do nativo) → vagão próprio com D1 + D3 + estreitamento**,
e o vagão próprio **depende** de a lane `cargo/0.3.1-comparacoes` ter fechado `x != null`
(§1.3), porque sem isso nenhuma fixture positiva produz binário.

---

## 7. O TAMANHO — o que o dono pediu explicitamente

Contado em **sítios a tocar**, separado nas duas parcelas para o dono poder escolher a ordem.

### Parcela A — CORRIGIR a análise de fluxo existente

| # | o quê | ficheiro:linha | sítios |
|---|---|---|---|
| A1 | `tblock_diverges` → travessia de statements (D1) | `src/checker/typer.tks:3299` | 1 fn reescrita + 1 helper novo |
| A2 | `cg_block_diverges` — a cópia, mesma correcção | `src/codegen/codegen.tks:6687` | 1 fn |
| A3 | `texpr_diverges` → exigir que `exit`/`panic` resolvam ao builtin (D2) | `src/checker/typer.tks:3281` | 1 fn (+ assinatura passa a precisar de `Env`) |
| A4 | `cg_expr_diverges` — a cópia, mesma correcção | `src/codegen/codegen.tks:3986` | 1 fn |
| A5 | os chamadores de `texpr_diverges` que passam a ter de fornecer `Env` | `typer.tks:2483,3402,4785,5058`; `lower.tks:3331`; `codegen.tks:8396` | 6 chamadas |
| A6 | corrigir o doc-comment de `require_narrowed` que promete o que não existe | `src/checker/typer.tks:2088` | 1 comentário |
| A7 | M.4 — "todos os caminhos retornam" (D3), se entrar | `typer.tks` (`check_trailing_value`) + `lower.tks:4322` + `codegen.tks:8659-9013` | 3 sítios, **e é o item aberto de tamanho maior** |

**Parcela A sem A7: ~11 sítios, 4 funções reescritas.** Pequeno.
**Parcela A com A7: +3 sítios, mas A7 é uma análise nova completa** — dimensioná-la exige medir
quantos corpos do corpus dependem hoje da confiança (um `loop` sem `break` cujas saídas são só
`return`). **Não medido neste documento**; nomeio-o como o único item de tamanho não fechado.

### Parcela B — ACRESCENTAR o estreitamento por cima

| # | o quê | ficheiro | sítios |
|---|---|---|---|
| B1 | `src/checker/narrow.tks` — ficheiro novo (`NarrowFact`, `NarrowSet`, `CondFacts` + 8 fns de §4.2) | novo | 1 ficheiro, ~10 declarações |
| B2 | `Env` ganha `narrow` + `with_narrow` | `src/checker/scope.tks:69` | 1 campo, **11 construtores de `Env` a actualizar** — medido: `grep -rn "Env { base" --include=*.tks src/` dá **11, todos em `src/checker/scope.tks`** (`env_empty`, `with_di`, `with_injects`, `with_fn_unsafe`, `seal`, `with_ns`, `with_file`, `with_owner`, `define`, `define_fn`, `define_const`), **zero fora dele** |
| B3 | `lookup_binding` aplica o facto | `src/checker/scope.tks` | 1 fn |
| B4 | `type_if` produz e propaga | `src/checker/typer.tks:3732` | 1 fn, 2 linhas |
| B5 | `type_if_stmt` produz e propaga | `src/checker/typer.tks:3755` | 1 fn, 2 linhas |
| B6 | `type_block` — o `return` precoce (E4) | `src/checker/typer.tks:3686` | 1 fn, ~6 linhas |
| B7 | invalidação em `TAssign` e `TLoopStmt` | `src/checker/typer.tks:4327` (`type_statement`) | 2 braços |
| B8 | fixtures | `examples/probes/` ou o harness de regressão | **35 programas** (§8: 18 positivas + 17 negativas) |

**Parcela B: ~8 sítios de código + 1 ficheiro novo + 11 construtores mecânicos + 35 fixtures.**

**Cuidado sobre B2, e é o único risco de tamanho escondido:** 11 construtores de `Env` escritos por
extenso, campo a campo. Acrescentar um campo obriga a tocar nos 13, e **esquecer um é um erro de
compilação, não um bug silencioso** — o que torna isto tedioso mas seguro. Se o dono quiser reduzir,
o caminho é extrair um `env_with(...)` antes, o que é refactor separado e **não** deve entrar neste
vagão.

### O total, em uma linha para o dono

> **Corrigir a análise: ~11 sítios / 4 funções (pequeno), mais M.4 (A7) que é o único item por
> dimensionar. Acrescentar o estreitamento: ~8 sítios + 1 ficheiro novo + 11 construtores mecânicos
> + 35 fixtures (médio-pequeno). A ordem obrigatória é A antes de B — E4, a forma mais valiosa,
> assenta directamente em A1.**

---

## 8. As fixtures

Cada uma é um projecto mínimo (`.tkp` + `src/main.tks`), gate no motor legado de LIR **e** no
nativo. Exit code é o contrato.

### 8.1 POSITIVAS — o que tem de passar a compilar

| id | forma | programa (núcleo) | esperado |
|---|---|---|---|
| `nf-p01` | ruling do dono (E1) | `let a: i32\|null = 7`<br>`if a != null { exit(a) }`<br>`exit(1)` | exit **7** |
| `nf-p02` | `else` de `== null` (E2) | `if a == null { exit(1) } else { exit(a) }` | exit **7** |
| `nf-p03` | `else` de `!= null` (E3) | `if a != null { exit(a) } else { exit(0) }` com `a = null` | exit **0** |
| `nf-p04` | **guarda precoce por `return`** (E4) | `fn f(a: i32\|null): i32 { if a == null { return 0 }; a }` | exit **7** |
| `nf-p05` | guarda precoce por `exit` (E4) | `if a == null { exit(1) }`<br>`exit(a)` | exit **7** |
| `nf-p06` | guarda precoce por `break` (E4) | `loop { if a == null { break }; exit(a) }` | exit **7** |
| `nf-p07` | `&&` (E5) | `if a != null && a > 0 { exit(a) }` | exit **7** |
| `nf-p08` | `\|\|` dual (E6) | `if a == null \|\| a == 0 { exit(1) } else { exit(a) }` | exit **7** |
| `nf-p09` | negação (E7) | `if !(a == null) { exit(a) }` | exit **7** |
| `nf-p10` | **três membros** (E8) | `let a: i32\|str\|null = 7`<br>`if a != null { match a { i32 as n => exit(n); str => exit(1) } }` | exit **7** |
| `nf-p11` | três membros, o estreito ainda é união | `if a != null { let b: i32\|str = a; exit(2) }` | exit **2** |
| `nf-p12` | campo de struct atrás de guarda (E1 + deref) | `let p: P\|null = P{x=7}`<br>`if p != null { exit(p.x) }` | exit **7** — hoje bate em `require_narrowed` |
| `nf-p13` | `let` sobrevive a chamada | `if a != null { g(); exit(a) }` | exit **7** |
| `nf-p14` | `let` sobrevive a entrada de `loop` (I2) | `if a != null { loop { exit(a) } }` | exit **7** |
| `nf-p15` | guardas encaixados | `if a != null { if b != null { exit(a + b) } }` | exit **9** |
| `nf-p16` | **D1** — divergência a meio do bloco | `match c { 1 => { exit(7); let z: i32 = 0 }; _ => { 2 } }` | exit **7** (hoje: rejeitado) |
| `nf-p17` | **D1** — `return` a meio do bloco | `match c { 1 => { return 7; let z: i32 = 0 }; _ => { 2 } }` | exit **7** (hoje: rejeitado) |
| `nf-p18` | **D1** — `if`-valor com divergência a meio | `if c == 1 { exit(7); let z: i32 = 0 } else { 2 }` | exit **7** (hoje: rejeitado) |

### 8.2 NEGATIVAS — o que TEM de continuar a ser rejeitado

Esta é a metade que impede o estreitamento de aceitar a mais. **Todas devem falhar na fase
`checker`, com diagnóstico, exit ≠ 0.**

| id | o que testa | programa (núcleo) | porquê tem de falhar |
|---|---|---|---|
| `nf-n01` | **I1 — reatribuição no bloco** | `mut a: i32\|null = 7`<br>`if a != null { a = null; exit(a) }` | o facto morreu na atribuição |
| `nf-n02` | I1 — reatribuição antes da leitura, guarda precoce | `mut a: i32\|null = 7`<br>`if a == null { exit(1) }`<br>`a = null`<br>`exit(a)` | idem, no eixo do E4 |
| `nf-n03` | **I3 — `mut` cujo endereço foi tomado** | `mut a: i32\|null = 7`<br>`let r = &a`<br>`if a != null { g(r); exit(a) }` | o callee pode ter escrito por `r` |
| `nf-n04` | **I4 — lambda que escreve a capturada** | `if a != null { let f = () => { a = null }; exit(a) }` | já rejeitado hoje por `lam_reject_copy_capture_write`; **tem de continuar** |
| `nf-n05` | **F4 — `mut` a atravessar um `loop`** | `mut a: i32\|null = 7`<br>`if a != null { loop { a = null; exit(a) } }` | o facto não atravessa a aresta de retorno |
| `nf-n06` | ramo ERRADO não estreita | `if a == null { exit(a) }` | no `then` de `== null`, `a` é `Null`, não `i32` |
| `nf-n07` | ramo ERRADO, dual | `if a != null { exit(0) } else { exit(a) }` | no `else`, `a` é `Null` |
| `nf-n08` | facto não escapa do `if` | `if a != null { }`<br>`exit(a)` | o facto morre com o bloco |
| `nf-n09` | facto não escapa de guarda NÃO divergente | `if a == null { let z = 1 }`<br>`exit(a)` | o `then` não diverge ⇒ E4 não se aplica |
| `nf-n10` | **`&&` no lado errado** | `if a != null && a > 0 { } else { exit(a) }` | o `else` do `&&` não prova nada |
| `nf-n11` | **`\|\|` no lado errado** | `if a != null \|\| c > 0 { exit(a) }` | o `then` do `\|\|` não prova nada |
| `nf-n12` | três membros não colapsa a um só | `let a: i32\|str\|null = 7`<br>`if a != null { exit(a) }` | o estreito é `i32\|str`, e `exit` quer inteiro |
| `nf-n13` | **F2 — campo não estreita** | `if p.x != null { exit(p.x) }` | sem points-to; fora do alcance |
| `nf-n14` | **F3 — índice não estreita** | `if v[i] != null { exit(v[i]) }` | idem |
| `nf-n15` | **F6 — predicado de função não estreita** | `if is_some(a) { exit(a) }` | sem contratos de predicado |
| `nf-n16` | **D2 — `exit` sombreado deixa de divergir** | `pub fn exit(c: i32): str { "x" }`<br>`match c { 1 => { exit(1) }; _ => { 2 } }` | **hoje o checker ACEITA e o `cc` apanha**; depois de D2 o CHECKER tem de rejeitar |
| `nf-n17` | `mut` reatribuído a não-null não reinstala | `mut a: i32\|null = null`<br>`if a == null { a = 7 }`<br>`exit(a)` | conservador por desenho; deve REJEITAR e o autor escreve `match` |

`nf-n16` é a fixture mais importante do conjunto: é a única que prova que a correcção da análise de
fluxo fechou um buraco de solidez em vez de apenas aceitar mais programas.

### 8.3 Fixtures de não-regressão da parcela A

`nf-p16`/`nf-p17`/`nf-p18` já cobrem D1. Adicionalmente, o gate completo do corpus (o próprio
compilador a construir-se) é a fixture de A: se `tblock_diverges` passar a percorrer o bloco e o
`gen2 == gen3` se mantiver, D1 é aditivo como afirmado em §5.

---

## 9. A sequência ordenada de crumbs

Cada crumb é independentemente gate-ável. **Nenhum crumb deixa o corpus a usar uma feature que a
semente não tem** — `NarrowSet` é um `struct` com um `[]NarrowFact`, e `type_without_null` só usa
`match`/`loop`/`teko::list::push`, tudo já na semente.

| crumb | o quê | gate |
|---|---|---|
| **0** | corrigir o doc-comment de `require_narrowed` (`typer.tks:2088`) que promete um "flow-narrowed branch of an equality guard" inexistente. Zero risco, e tira do caminho uma afirmação falsa | build |
| **1** | **A3+A4+A5** — `texpr_diverges`/`cg_expr_diverges` exigem que `exit`/`panic` resolvam ao builtin. **medido**: um `grep` por declarações `fn exit` / `fn panic` em `src/` e `examples/` não devolve **nada** — nem o compilador nem os probes sombreiam esses nomes, logo o `gen2 == gen3` do corpus não é afectado. Fixture `nf-n16` | **RITUAL — gate completo** (é subtractivo, §5) |
| **2** | **A1+A2** — `tblock_diverges`/`cg_block_diverges` passam a percorrer o bloco. Fixtures `nf-p16..p18` | **RITUAL — gate completo + `gen2 == gen3`** |
| **3** | `src/checker/narrow.tks` — os três tipos e as oito funções de §4.2, **implementadas e testadas em unidade, mas ainda não ligadas**. Compila hoje; não muda nenhum programa | build + `.tkt` |
| **4** | **B2+B3** — `Env.narrow` + `with_narrow` + `lookup_binding` narrow-aware. Ninguém ainda POVOA o conjunto ⇒ conjunto sempre vazio ⇒ **corpus tipa byte-idêntico**. Este é o crumb que prova que o custo em runtime do checker é zero quando não há estreitamento | **RITUAL — gate completo + `gen2 == gen3` (tem de ser byte-idêntico)** |
| **5** | **B4+B5** — `type_if`/`type_if_stmt` chamam `cond_facts`, forma `x != null` / `x == null` apenas, ambos os ramos (E1+E2+E3). Fixtures `nf-p01..p03`, `nf-n06..n08` | gate completo |
| **6** | **B7** — invalidação: `TAssign` e `TLoopStmt`. Fixtures `nf-n01`, `nf-n05`. **Este crumb vem ANTES de alargar o alcance**, deliberadamente: as portas fecham-se antes de a casa crescer | gate completo |
| **7** | I3/I4 — `fn_takes_address_of`, factos sobre `Reference`/`Borrow` mortos à chamada. Fixtures `nf-n03`, `nf-n04`, `nf-p13` | gate completo |
| **8** | **B6 / E4** — o `return`/`exit`/`break`/`continue` precoce em `type_block`. **O crumb de maior valor, e depende do crumb 2.** Fixtures `nf-p04..p06`, `nf-n09` | **RITUAL — gate completo** |
| **9** | E5+E6+E7 — `&&`, `\|\|`, `!`. Fixtures `nf-p07..p09`, `nf-n10`, `nf-n11` | gate completo |
| **10** | E8 — três ou mais membros, com o colapso ao membro nu. Fixtures `nf-p10`, `nf-p11`, `nf-n12` | gate completo |
| **11** | E1 sobre receptor de campo (`nf-p12`) — `require_narrowed` deixa de disparar quando há facto | gate completo |
| **12** | as negativas de alcance (`nf-n13..n15`, `nf-n17`) como gate permanente de que o alcance NÃO cresceu por acidente | **RITUAL — gate completo + `gen2 == gen3`** |
| **13** | *(opcional, se o dono quiser A7)* M.4 — todos-os-caminhos-retornam. **Tamanho por dimensionar** | **RITUAL** |

**Pontos de ritual (gate completo obrigatório): crumbs 1, 2, 4, 8, 12** (e 13 se entrar).

---

## 10. Riscos e tensões com as Leis

| # | risco / tensão | resolução recomendada |
|---|---|---|
| R1 | **D2 é subtractivo** — rejeita programa hoje aceite | Lei-primeiro: a Constituição não admite que o checker aceite programa mal-tipado. Um buraco de solidez não tem direito adquirido. Resolvido: **corrige-se**, com nota de release. Não há tensão genuína |
| R2 | O estreitamento aceita a mais por um caso de invalidação não previsto | Mitigado por desenho: 17 fixtures negativas, e o crumb 6 (invalidação) vem **antes** dos crumbs que alargam o alcance. Se surgir dúvida sobre um caso, a resposta por omissão é **apagar o facto** |
| R3 | `Env` cresce um campo ⇒ 11 construtores | Tedioso, mas **falha em compilação** se algum for esquecido. Sem risco silencioso. Refactor de `env_with` NÃO entra neste vagão |
| R4 | O trabalho depende de `cargo/0.3.1-comparacoes` fechar `x != null` (§1.3) | Sequenciamento entre lanes, não decisão de desenho. **Os crumbs 0-8 não dependem disso** (nenhum precisa que o programa produza binário para gate de checker); só as fixtures positivas com exit-code é que dependem. Adiantável quase por inteiro |
| R5 | Tensão **Teko-only vs. os gémeos C**: `cg_block_diverges`/`cg_expr_diverges` estão em `src/codegen/codegen.tks` — que é **Teko**, não C. `bootstrap/teko.c` é semente congelada e não se toca | Sem tensão. Confirmado: os dois sítios a corrigir são `.tks` |
| R6 | **Tensão de lane**: D2 pertence ao nativo (solidez) mas o resto pertence ao vagão do estreitamento | Resolvido em §6 pela lei do risco de bissecção: **D2 aqui, o resto no vagão próprio**. Se o dono preferir tudo junto no vagão próprio, é aceitável — o custo é o degrau nativo assentar mais tempo numa análise que mente. **Não HALTO por isto**; a recomendação é clara e o dono escolhe |
| R7 | A7/M.4 tem tamanho por dimensionar | **Nomeado como o único item aberto.** Recomendação: o dono decide se M.4 entra neste vagão ou recebe o seu; medir o custo exige contar os corpos do corpus que dependem hoje da confiança, o que é uma medição própria |

**Nenhuma tensão de Lei genuinamente irresolúvel. Não há HALT.**

---

## 11. Resumo — o que entra, o que fica de fora, e o custo

**Entra:** `if a != null` e `if a == null` com **os dois ramos**; a **guarda precoce**
(`return`/`exit`/`break`/`continue`), que é o padrão dominante e o crumb de maior valor; `&&`, `||` e
`!` compostos; **união de três ou mais membros** com colapso ao membro nu quando resta um; o
estreitamento visível a `.campo`. Espelha o **C#** — refina a mesma variável — porque Rust, Zig e Go
ligam um nome novo, que é o que o `match` do Teko **já** faz.

**Fica de fora:** campos e índices (sem points-to, aceitaria a mais); `while` (não existe no Teko);
factos sobre `mut` a atravessar `loop`; `when` guards; predicados inter-procedimentais; `T?`
(modelo distinto de `T | null`). Cada um com o seu argumento em §3.2.

**Sobre a análise de fluxo existente, que o dono disse precisar de correcção: ele tem razão, e são
três defeitos, todos medidos.**
1. `tblock_diverges` (`typer.tks:3299`) é uma **espreitadela ao último statement**, não uma
   travessia — rejeita programa correcto (`exit(1); let z = 0`).
2. `texpr_diverges` (`typer.tks:3281`) reconhece `exit`/`panic` **por nome nu** — e o checker
   **aceitou um programa mal-tipado** que só o `cc` apanhou; no nativo miscompilaria em silêncio.
3. M.4 ("todos os caminhos retornam") **não existe**, e o emissor emite `__builtin_unreachable()`
   sobre confiança não verificada — o próprio código admite-o.
O que o estreitamento **reaproveita** são exactamente os predicados (1) e (2); o walker do
`#must_free` está são dentro dos seus limites declarados e **não** serve para isto.

**Fixpoint: ADITIVO** — o Teko não tem sobrecarga por tipo de argumento, a adopção de literais é
dirigida pelo destino, e todo o sítio que passa a ser aceite é hoje um erro. Não parte
`gen2 == gen3`, não precisa de bump com a semente. **Excepto D2**, que é subtractivo por ser
correcção de solidez.

**Custo, que é o que o dono pediu:**
- **Corrigir a análise: ~11 sítios, 4 funções reescritas.** Pequeno.
- **Acrescentar o estreitamento: ~8 sítios + 1 ficheiro novo + 11 construtores mecânicos +
  24 fixtures.** Médio-pequeno.
- **Um item por dimensionar: M.4.** Nomeado, não estimado.
- **Ordem obrigatória: corrigir antes de acrescentar** — a guarda precoce assenta directamente na
  correcção de `tblock_diverges`.

**Lane: D2 nesta (`0.3.1.0`, é solidez do nativo); D1 + M.4 + estreitamento em vagão próprio, que
depende de `cargo/0.3.1-comparacoes` fechar `x != null` (§1.3 dá-lhes a raiz).**

---

## Anexo — achados adjacentes, REPORTADOS (não convertidos em issue por mim)

1. **Sem diagnóstico de código inalcançável.** `return c` seguido de mais statements compila em
   silêncio (medido, `C3`).
2. **Um erro de collect vira "unknown function: f".** Um ficheiro cujo item falha a recolha faz o
   chamador reportar `unknown function`, escondendo a causa real (observado com padrões `true`/
   `false` sobre subject `bool`).
3. **Padrões literais `true`/`false` sobre um subject `bool` não são aceites**
   (`match c { true => …; false => … }` faz `f` desaparecer da tabela).
4. **`literal_adopts` deixa um literal numérico com o tipo `Variant` inteiro** em posição de
   comparação (`a != 5` sobre `i32 | null`) e o codegen falha com *"numeric literal with a
   non-primitive type not yet supported"* — mesma raiz do bug de §1.3, mesma correcção
   (`adopt_binop_operands` devia usar `retype_literal`).
