# 0.3.1 — comparações (str/char/[]T/null) e o estreitamento por `if`

**Lane**: `cargo/0.3.1-comparacoes`, sobre o vagão `remodel/0.3.1.0-linux-native-2` (`9847fe6`).
**Papel**: verificador — mede e regista, não corrige.

**Compilador usado para medir**: a escada de emergência (o `fetch_teko.sh` normal dá 403 no proxy
da Claude e o binário em cache está obsoleto demais para reconhecer construções deste vagão):

```sh
sh scripts/build_gen1_from_c.sh bootstrap/teko.c src .gen1     # a semente antiga (NÃO usar para medir)
TEKO_BACKEND=c .gen1/teko . -o .gen1b --no-verify --release    # .gen1b = auto-hospedado a partir do FONTE ACTUAL
```

**Armadilha que já me mordeu uma vez, registada para quem repetir a medição**: `.gen1/teko` (a
semente C do bootstrap) é uma imagem ANTIGA do compilador — não reflecte `src/lir/lower.tks` nem
`src/checker/*.tks` actuais. As minhas primeiras medições de `char`/`str` usaram `.gen1/teko` por
engano e davam "N1: `str` has no single PrimKind" mesmo para uma comparação de strings simples —
completamente falso, era o compilador errado. **Toda medição abaixo usa `.gen1b/teko`** (o
auto-hospedado, construído a partir do fonte desta branch), que é o único que fala a verdade sobre
o que este vagão faz.

Cada caso: um projecto-sonda mínimo (`<nome>.tkp` + `src/main.tks`), construído com
`TEKO_BACKEND=c` e depois com `TEKO_BACKEND=native`, executado, saída colada literal.

**Convenção das três colunas** (pedida pelo dono, 2026-07-29): divergência entre rotas é bug do
nativo (a rota C é o oráculo); CONCORDÂNCIA em falhar é buraco da LINGUAGEM (nenhum oráculo — os
dois lados não fazem, ponto).

**Convenção das quatro referências** (pedida pelo dono, 2026-07-29): superfície → **Rust**,
controlo → **Zig**, addins → **C#**, certos comportamentos → **Go**. Cada achado abaixo nomeia qual
das quatro resolveria o buraco, e porquê — "como as outras linguagens fazem" esconde que elas fazem
coisas diferentes entre si.

---

## 1. Strings — `==`/`!=`

### 1.1 O programa (todas as variações num só ficheiro, todas em posição `if`)

```teko
let lit1: str = "a"
let lit2: str = "a"
let empty1: str = ""
let empty2: str = ""
let accented1: str = "café"
let accented2: str = "café"
let ascii_only: str = "cafe"
let emoji1: str = "😀x"
let emoji2: str = "😀x"
let long_a: str = "hello world"
let short_a: str = "hi"
let concatenated: str = teko::str::concat("caf", "é")

if "a" == "a" { teko::io::println("lit_lit_eq: true") } else { teko::io::println("lit_lit_eq: false") }
if "a" != "a" { teko::io::println("lit_lit_neq: true") } else { teko::io::println("lit_lit_neq: false") }
if lit1 == "a" { teko::io::println("var_lit_eq: true") } else { teko::io::println("var_lit_eq: false") }
if lit1 == lit2 { teko::io::println("var_var_eq: true") } else { teko::io::println("var_var_eq: false") }
if empty1 == empty2 { teko::io::println("empty_eq: true") } else { teko::io::println("empty_eq: false") }
if empty1 == lit1 { teko::io::println("empty_vs_nonempty_eq: true") } else { teko::io::println("empty_vs_nonempty_eq: false") }
if long_a == short_a { teko::io::println("diff_len_eq: true") } else { teko::io::println("diff_len_eq: false") }
if long_a != short_a { teko::io::println("diff_len_neq: true") } else { teko::io::println("diff_len_neq: false") }
if accented1 == accented2 { teko::io::println("accent_eq_same: true") } else { teko::io::println("accent_eq_same: false") }
if accented1 == ascii_only { teko::io::println("accent_vs_ascii_eq: true") } else { teko::io::println("accent_vs_ascii_eq: false") }
if emoji1 == emoji2 { teko::io::println("emoji_eq_same: true") } else { teko::io::println("emoji_eq_same: false") }
if emoji1 != emoji2 { teko::io::println("emoji_neq_same: true") } else { teko::io::println("emoji_neq_same: false") }
if concatenated == accented1 { teko::io::println("concat_vs_lit_eq: true") } else { teko::io::println("concat_vs_lit_eq: false") }
exit(0)
```

### 1.2 Saída — `TEKO_BACKEND=c` e `TEKO_BACKEND=native` (idêntica, colada uma vez)

```
lit_lit_eq: true
lit_lit_neq: false
var_lit_eq: true
var_var_eq: true
empty_eq: true
empty_vs_nonempty_eq: false
diff_len_eq: false
diff_len_neq: true
accent_eq_same: true
accent_vs_ascii_eq: false
emoji_eq_same: true
emoji_neq_same: false
concat_vs_lit_eq: true
[exit code: 0]
```

Todas as 13 asserções têm o VALOR correcto (não só "compila") nas duas rotas, incluindo o par
multi-byte (`café` acentuado, `😀x` emoji) e a comparação contra o resultado de `teko::str::concat`
— confirma comparação por CONTEÚDO, não por ponteiro, em ambas as rotas.

### 1.3 Posições vizinhas (varrimento pedido pelo dono — "não só em if")

Mesmo par `x = "x"`, `y = "x"`: `let eq_let = x == y`; `loop { if !(x == y && i < 1) { break } … }`
(o `loop`+`break` é a forma do `while` em Teko — não há palavra-chave `while`); argumento de função
(`takes_str_eq(x == y)`); valor de retorno de `returns_cmp(a,b) { return a == b }`; `||`; comparação
contra o resultado de uma chamada (`x == make_x()`); guarda de `match` (`_ when x == y => …` — a
guarda usa a palavra-chave `when`, não `if`).

Saída (as duas rotas, sem interpolação para não misturar com o buraco separado #7 abaixo):

```
let_position: true
while_position: true
arg_position: true
return_position: true
or_position: true
call_result_position: true
match_guard_position: true
[C route exit code: 0]
[native route exit code: 0]
```

### 1.4 Veredicto — strings

**FUNCIONA NAS DUAS ROTAS**, em todas as posições varridas, com VALOR correcto (não um `==` que
sempre devolve `true`). Implementado via `tk_str_eq` (`src/runtime/teko_rt.h`) — comparação por
conteúdo — chamado a partir de `lower_str_compare` (`src/lir/lower.tks:1343`, nativo) e do bloco
`prev_str || opnd_str` de `emit_compare` (`src/codegen/codegen.tks:2904-2916`, C). Nenhum buraco.

---

## 2. `char` — `==`/`!=`

### 2.1 O programa

```teko
let ca: char = c'a'
let cb: char = c'a'
let cc: char = c'b'
if c'a' == c'a' { teko::io::println("lit_lit_eq: true") } else { teko::io::println("lit_lit_eq: false") }
if c'a' != c'b' { teko::io::println("lit_lit_neq: true") } else { teko::io::println("lit_lit_neq: false") }
if ca == c'a' { teko::io::println("var_lit_eq: true") } else { teko::io::println("var_lit_eq: false") }
if ca == cb { teko::io::println("var_var_eq_same: true") } else { teko::io::println("var_var_eq_same: false") }
if ca == cc { teko::io::println("var_var_eq_diff: true") } else { teko::io::println("var_var_eq_diff: false") }
exit(0)
```

### 2.2 Saída — `TEKO_BACKEND=c`

```
.../probe.c:21:7: error: invalid operands to binary == (have 'tk_char' and 'tk_char')
.../probe.c:33:7: error: invalid operands to binary != (have 'tk_char' and 'tk_char')
.../probe.c:43:16: error: invalid operands to binary == (have 'tk_char' and 'tk_char')
.../probe.c:53:16: error: invalid operands to binary == (have 'tk_char' and 'tk_char')
.../probe.c:61:16: error: invalid operands to binary == (have 'tk_char' and 'tk_char')
cc failed to build the generated C
```

### 2.3 Saída — `TEKO_BACKEND=native`

```
teko: TRACE prim_kind_of stop: native backend N1: `char` has no single PrimKind, asked by the comparison chain (N2)
```

Repetido com `c'é'` (acentuado) — **mesmo erro, mesmo shape**, em ambas as rotas (não é um efeito
de largura de bytes; é o TIPO `char` em si que nenhuma rota sabe comparar). Também não depende da
posição (`if`, `let eq = …`): testado num `let` isolado, falha de forma idêntica.

### 2.4 Veredicto — char: **FALHA NAS DUAS ROTAS. Buraco de linguagem, não divergência.**

Nem a rota C nem a nativa sabem comparar `char`. Não há oráculo a discordar — as duas rotas
concordam silenciosamente em não fazer, exactamente o "óbvio que passou despercebido" que o dono
desconfiava.

**Causa, com evidência de código (ambas as rotas cometem o MESMO esquecimento — nenhuma foi
estendida de `str` para `char`, embora `char` seja, em runtime, o mesmo par `{ptr,len}`):**

- **C** (`src/codegen/codegen.tks:2904-2917`, `emit_compare`): o desvio para comparação por
  conteúdo só dispara quando `prev.type`/`term.operand.type` é `checker::Str` — nunca `checker::Char`.
  Um `char == char` cai no ramo genérico `else` (linha 2918-2924) e emite `==` bruto do C sobre dois
  `tk_char`, que é um `struct {ptr,len}` (`src/runtime/teko_rt.h:57`). `cc` rejeita: struct não tem
  `==`.
- **Nativo** (`src/lir/lower.tks:1312-1317`, `is_str_type` — "a narrower sibling of `is_fat_type`:
  ... but only a `str` has a runtime equality entry to compare with"): o mesmo desvio, mesma
  omissão. `lower_compare` (linha 1414) só chama `lower_str_compare` para `Str`; `char` cai no
  caminho genérico ICmp, que pede um `PrimKind` a `prim_kind_of` (linha 1157-1163) — `char` não é
  `Prim`/`Byte`/`Named`, cai no `_ => error` com o honest-stop N1 visto acima.

**Nota sobre a hipótese já registada nesta lane** ("`lower_char_lit` baixa `c'A'` para o inteiro 65
no nativo enquanto a rota C baixa para `{ptr,len}`" — representações diferentes entre rotas): essa
observação sobre `lower_char_lit` (`src/lir/lower.tks:7751-7756`, decodifica sempre para um
`i64` escalar) está correcta como FACTO isolado, mas **não chega a ser testada em prática** — a
comparação já para mais cedo, no `prim_kind_of` do tipo declarado (`char`, não no literal em si).
Um `char` VARIÁVEL (`let ca: char = c'a'`) nunca chega a ser lowered como escalar OU como
`{ptr,len}` para efeitos de `==`, porque a função que decide como comparar nunca reconhece `Char`
como candidato. A divergência de representação é real (ver ficheiro à parte se o dono quiser essa
medição), mas não é a causa do FALHA aqui — a causa é a omissão de `Char` em `is_str_type`/
`prev_str`/`opnd_str`, mais a jusante.

**Qual das quatro referências resolveria isto, e porquê**: nenhuma das quatro precisa de desenho
novo — é a MESMA solução que já existe para `str` (`tk_str_eq`), só que também vale para `char`
(Rust deriva `PartialEq` para `char` automaticamente por ser um Copy scalar; Go compara `rune`
— que é só um `int32` — com `==` nativo; C# compara `char` como um inteiro UTF-16 também nativo).
O ponto comum às quatro é que nenhuma trata `char` como um tipo à parte de "tamanho variável
comparável só por conteúdo" — é exactamente a folga que falta aqui. **Custo**: estender os DOIS
locais já identificados (`is_str_type`/`prev_str`+`opnd_str`) para reconhecer `Char` e rotear para
`tk_str_eq` (ou um `tk_char_eq` dedicado) — 2 sítios, ambos já nomeados, nenhum redesenho.

---

## 3. Arrays / slices — `==`/`!=`

### 3.1 O programa

```teko
let a: []i64 = [1, 2, 3]
let c: []i64 = [1, 2, 4]
let d: []i64 = [1, 2]
let e1: []i64 = []
let e2: []i64 = []
let sa: []str = ["x", "y"]
let sb: []str = ["x", "y"]
if a == c { teko::io::println("diff_content_eq: true") } else { teko::io::println("diff_content_eq: false") }
if a == d { teko::io::println("diff_len_eq: true") } else { teko::io::println("diff_len_eq: false") }
if e1 == e2 { teko::io::println("empty_eq: true") } else { teko::io::println("empty_eq: false") }
if sa == sb { teko::io::println("str_slice_eq: true") } else { teko::io::println("str_slice_eq: false") }
exit(0)
```

(Mesmo conteúdo — `a == b` com `b = [1,2,3]` — testado à parte, mesmo resultado abaixo.)

### 3.2 Saída — `TEKO_BACKEND=c`

```
.../probe.c:79:13: error: invalid operands to binary == (have 'tk_slice_i64' and 'tk_slice_i64')
.../probe.c:87:15: error: invalid operands to binary == (have 'tk_slice_i64' and 'tk_slice_i64')
.../probe.c:95:16: error: invalid operands to binary == (have 'tk_slice_i64' and 'tk_slice_i64')
.../probe.c:103:16: error: invalid operands to binary == (have 'tk_slice_str' and 'tk_slice_str')
cc failed to build the generated C
```

### 3.3 Saída — `TEKO_BACKEND=native`

```
teko: TRACE prim_kind_of stop: native backend N1: `[]i64` has no single PrimKind, asked by the comparison chain (N2)
```

Testado também isoladamente num `let eq = a == b` (mesmo resultado — não depende da posição).

### 3.4 Veredicto — arrays/slices: **NÃO COMPILA, NAS DUAS ROTAS, para NENHUMA variação testada**
(mesmo conteúdo, conteúdo diferente, comprimentos diferentes, vazio, elementos gordos `[]str`).
**Buraco de linguagem** — concordância, não divergência.

O checker PERMITE a comparação (`is_comparable`, `src/checker/expr.tks:51-55`, cai no `type_eq(a,b)`
para dois `Slice` do mesmo elemento — passa), mas NENHUMA rota tem lowering para ela: exactamente a
mesma omissão de `char` — `is_str_type`/`prev_str`+`opnd_str` só reconhecem `Str`, nunca `Slice`.

**Qual das quatro referências, e porquê — aqui é onde elas DIVERGEM de facto, não é um "óbvio
universal" como em `char`:**
- **Go**: `==` de slice é **deliberadamente proibido em runtime também** (só compara contra `nil`);
  Go escolheria dar um erro do checker mais claro, não implementar `==` de slice — a linguagem
  espelho aqui aponta para MANTER a rejeição, só que com uma mensagem de compilador em vez de um
  `cc` a rebentar.
- **Rust**: `[T]`/`Vec<T>` só ganham `==` se `T: PartialEq` — e mesmo assim é comparação por
  conteúdo, elemento a elemento, DERIVADA, não builtin. Copiar Rust custaria construir a noção de
  "T é comparável" recursivamente (já existe parcialmente — `is_comparable` — mas teria de percorrer
  o elemento de um `Slice` em vez de exigir `type_eq` do slice inteiro).
  Trait — 0.3.1 não tem.
- **C#**: `array1 == array2` em C# é identidade de referência por omissão (structural equality não é
  o padrão para `T[]`) — copiar C# aqui daria exactamente o comportamento ERRADO que o dono já avisou
  que quer evitar ("um `==` que devolva sempre `true`" — pior ainda, aqui seria sempre `false` salvo
  para o mesmo array).
- **Zig**: não tem `==` de slice embutido de todo — força `std.mem.eql` explícito.

**A escolha entre estas é decisão do dono, não deste relatório** — mas a pista mais barata e mais
alinhada com "o óbvio que um dev vai escrever sem pensar" é espelhar Go/Zig: **rejeitar no checker**
com uma mensagem nomeada (`"slice equality is not implemented — compare elements in a loop"`, ou
similar), fechando a fenda por onde hoje o `cc` explode com um erro de C cru. Implementar a
comparação por CONTEÚDO in extenso (a leitura Rust) é trabalho maior — recursão sobre o elemento,
mais um `tk_slice_eq_<T>` gerado por instanciação, nas DUAS rotas.

---

## 4. Nulo e o estreitamento — o caso central

### 4.1 O exemplo do dono, tal como escrito

```teko
let a: i32 | null = 0
if a != null {
  exit(a)
}
exit(99)
```

### 4.2 Saída — `TEKO_BACKEND=c` e `TEKO_BACKEND=native` (idêntica)

```
teko 0.3.0.31-beta
  checker ... ✗  src/main.tks:3:3: exit's argument must be an integer status code
teko: ...: src/main.tks:3:3: exit's argument must be an integer status code
```

**Reproduzido exactamente como o dono descreveu**: `a` continua `i32 | null` dentro do `if`, e
`exit(a)` é rejeitado nas duas rotas (mesma mensagem, mesma linha — não há divergência aqui, é o
checker, partilhado por ambas).

### 4.3 Separando os dois eixos — (a) a comparação em si

```teko
let a: i32 | null = 0
if a != null { teko::io::println("a_neq_null: true") } else { teko::io::println("a_neq_null: false") }
if a == null { teko::io::println("a_eq_null: true") } else { teko::io::println("a_eq_null: false") }
```

**Saída — `TEKO_BACKEND=c`:**
```
checker ... ✓ (3/3 items — o checker ACEITA a comparação)
codegen ... ✗  codegen: a bare `null` needs a known optional type from context (use it where a `T?` is expected)
```

**Saída — `TEKO_BACKEND=native`:**
```
checker ... ✓ (3/3 items)
teko: TRACE prim_kind_of stop: native backend N1: `i32 | null` has no single PrimKind, asked by the cast target (N2)
```

**A COMPARAÇÃO EM SI JÁ NÃO COMPILA, nas duas rotas** — isto é anterior e mais fundamental do que o
estreitamento: mesmo sem nenhuma tentativa de usar `a` estreitado, `a != null`/`a == null` sobre um
`i32 | null` explícito falha no CODEGEN (não no checker) nas duas rotas. **Concordância — buraco de
linguagem, não divergência.**

**Causa, com evidência exacta** (`src/codegen/codegen.tks:7198-7204`, o ramo `TNullLit` de
`emit_expr_ctx`):
```
checker::TNullLit => match e.type {
    checker::Null => cb(buf, "0")
    _ => error { message = "codegen: a bare `null` needs a known optional type from context (use it where a `T?` is expected)" }
}
```
Um literal `null` só emite se o seu `.type` resolvido for exactamente `checker::Null`. Isso só
acontece quando o `null` é tipado num contexto COM tipo esperado conhecido (`src/checker/typer.tks:3838-3839`,
`if null_widens_into(expected, table) { type = Null{} }` — usado em `let`/atribuição/retorno). Mas
`type_compare` (`src/checker/typer.tks:787-807`) tipa cada operando da comparação com
`type_expr(c.rest[i].operand, env, table)` **sem** passar um tipo esperado — para um `null` nu isso
cai no caminho genérico `type_nulllit` (`src/checker/typer.tks:46-47`):
```
fn type_nulllit(n: parser::NullLit): TExpr | error {
    TExpr { kind = TNullLit { }; type = Optional { inner = Void { } }; line = 0; col = 0 }
}
```
— tipo `Optional{inner=Void}`, não `Null`. `is_comparable` (`src/checker/expr.tks:51-55`) aceita a
comparação porque cai no `type_eq` genérico contra o `Variant{i32,Null}` de `a` (via alguma via de
adopção que o checker aceita), mas o CODEGEN nunca vê o tipo certo no literal `null` e cai no
`_ => error` acima. É o MESMO bug nas duas rotas porque a causa-raiz é do CHECKER (`type_compare`
não adopta o `null` para o contexto), partilhado por ambas.

O nativo falha por um caminho lateral diferente ("cast target", não "comparison chain") — sinal de
que o desaçúcar interno de `!= null`/`== null` sobre um `Variant` passa por um `to`/cast de tag
algures antes de chegar ao ICmp — mas o RESULTADO observável é o mesmo: nem uma rota nem a outra
compila a comparação.

### 4.4 (b) O estreitamento em si — confirmado ausente no checker, com evidência directa

`type_if`/`type_if_stmt` (`src/checker/typer.tks:3732-3763`) tipam o `then_blk` e o `else_blk` com o
MESMO `env` recebido, sem qualquer inspecção da FORMA de `f.cond`:
```teko
fn type_if(f: parser::IfExpr, env: Env, table: TypeTable): TExpr | error {
    let c = match type_expr(f.cond, env, table) { ... }
    ...
    let tb = match type_block(f.then_blk, env, table) { ... }   // MESMO env
    let eb = match type_block(f.else_blk, env, table) { ... }   // MESMO env
    ...
}
```
Não há nenhuma função `narrow_env_for_cond`/equivalente — a busca `grep -rniE
'narrow|refine.*type|flow.*sensit' src/checker/*.tks` já não encontrava nada relacionado, e a leitura
directa do código confirma: **não há maquinaria de estreitamento por fluxo no checker, ponto.**

### 4.5 Por que `match` "funciona" — confirmado: liga um NOME NOVO, não estreita o existente

```teko
let a: i32 | null = 5
match a {
    i32 as n => exit(n)
    null => exit(-1)
}
```
**Saída — `TEKO_BACKEND=c`**: `[exit code: 5]` — funciona, valor correcto.
**Saída — `TEKO_BACKEND=native`**: falha por OUTRA razão (ver §4.6) — a divergência aqui é
tangencial ao mecanismo de bind, não à narrowing question.

Evidência de código (`src/checker/match.tks:199-217`, `check_pattern`, ramo `BindPattern`):
```teko
parser::BindPattern as bp => {
    ...
    if !bp.has_binding { return env }   // bare case / `as _` discard — binds nothing
    define(env, bp.binding, ct, false)   // `Type as name` → name : the case type
}
```
`define(env, bp.binding, ct, false)` acrescenta uma entrada NOVA ao ambiente para o nome `n`, com o
tipo do CASO (`ct` = `i32`) — `a` em si permanece, inalterado, `i32 | null` no ambiente exterior; só
`n` (o nome ligado pelo padrão) é que nasce já estreito. **Confirmado: é ligação de nome novo, não
estreitamento da variável existente.**

**Confirmação da hipótese das quatro referências (pedida pelo dono, 2026-07-29): das quatro, só
C# faz o que o dono pediu — refinar a MESMA variável por análise de fluxo.**
- **Rust**: `if let Some(x) = a { … }` — liga `x`, novo nome. Mesma família que o `match` do Teko.
- **Zig**: `if (opt) |v| { … }` — liga `v`, novo nome. Mesma família.
- **Go**: `if v, ok := m[k]; ok { … }` — liga `v`, novo nome (e nem é `null`-específico, é o padrão
  comma-ok). Mesma família.
- **C#**: `if (a != null) { … a usa-se directamente, já não-nulo … }` — a ÚNICA das quatro que
  refina a variável ORIGINAL por fluxo (null-state analysis do compilador, desde o C# 8 nullable
  reference types).

**O Teko hoje está na família Rust/Zig/Go** (`match` com bind-novo funciona; nunca fez a promessa de
refinar `a` em si). **O pedido do dono é acrescentar o eixo do C# por cima** — não é bug do que já
existe, é pedido de FUNCIONALIDADE NOVA (fluxo-sensível), ausente das outras três referências
também.

### 4.6 Formas vizinhas medidas

**`if a == null {...} else { exit(a) }`** — mesma rejeição no `else` (`3:3: exit's argument must be
an integer status code`), consistente: nem o ramo `else` de um `== null` estreita.

**`if a != null && a > 0 { exit(2) } exit(3)`** — checker aceita (3/3 items — não rejeita `a > 0`
sobre o `Variant` no `&&`, interessante à parte, mas não estreitou nada), falha no MESMO ponto do
§4.3 (`codegen: a bare null needs a known optional type from context`) — o `&&` não muda o
diagnóstico, é a mesma comparação-contra-null a bloquear mais cedo.

**União de três membros `i32 | str | null`** — `if a != null { exit(1) } exit(2)` falha
IDENTICAMENTE (mesmo erro de codegen em C; `native backend N1: i32 | str | null has no single
PrimKind, asked by the cast target` no nativo) — o buraco da comparação-contra-null não depende da
aridade da união.

### 4.7 Veredicto — caso 4

| sub-caso | C | nativo | veredicto |
|---|---|---|---|
| `a != null`/`a == null`, valor da comparação | não compila (codegen) | não compila (N1, cast target) | **CONCORDÂNCIA — buraco de linguagem** (não divergência) |
| estreitamento dentro do `if`/`else` | rejeitado no checker | rejeitado no checker (mesmo) | **ausente, por desenho actual — não é bug, é funcionalidade em falta** |
| `match` com `T as n` | funciona, valor correcto | falha por razão à parte (§ próximo) | bind de nome novo, não estreitamento |

### 4.8 Nota lateral encontrada por acidente (não pedida, mas reproduzível e barata de registar)

`let r = match a { i32 as n => n; null => 0 }` (usar o `match` como VALOR, não como statement)
produz um `r` cujo tipo NÃO é `i32` — é rejeitado por QUALQUER anotação testada (`i32`, `i64`, `u32`,
`bool`, `str` — todas dão `"value type does not match annotation"`), e sem anotação `exit(r)` dá
`"exit's argument must be an integer status code"` apesar de as duas alternativas do match serem
inteiras. Não investiguei a fundo (fora do âmbito pedido), mas a suspeita, com uma pista: `type_join`
(`src/checker/resolve.tks:1456-1467`) tem um ramo que constrói um `Variant` quando os dois lados não
widen um no outro — se o braço `null => 0`/`null => -1` está a ser typado com um tipo que não
"widen_into" o `i32` do outro braço, o resultado do `match`-valor pode ficar a UNIÃO inteira em vez
de `i32`. Deixo registado para o dono decidir se quer que isto seja medido a sério; não fiz mais
testes sobre isto para não desviar do pedido central.

### 4.9 Se o dono quiser o eixo C# — custo estimado (não é desenho, é onde entraria)

**Isto é FUNCIONALIDADE nova, não correcção de bug.** Estreitamento por fluxo dentro de `if`
exigiria, no mínimo:
1. Uma função `narrow_type_for_cond(cond: TExpr, env: Env): (then_env: Env, else_env: Env)` chamada
   a partir de `type_if`/`type_if_stmt` (`src/checker/typer.tks:3732`, `3755`) ANTES de tipar cada
   ramo com o env correspondente em vez do env comum de hoje.
2. Reconhecer, no mínimo, a FORMA `<var> != null` / `<var> == null` (e o inverso no ramo oposto) —
   exige subir do `TExpr` genérico até identificar que o operando esquerdo é um `TVar` simples (não
   uma expressão composta — `a.b != null` levanta a questão de estreitar um CAMPO, mais complexo
   ainda) e reescrever a entrada desse nome no env com o tipo sem o membro `Null`.
3. Propagar a mesma máquina por `&&` (dois testes, ambos a contribuir estreitamento no mesmo ramo —
   já auto-testado acima que `&&` hoje nem chega a compilar a comparação) e por `match`-guard
   (`when`) — cada um é outro ponto de entrada.
4. Decidir se o estreitamento sobrevive ao FIM do bloco `if` sem `else` (Rust/TypeScript fazem-no
   quando o ramo `then` diverge — `if a == null { return }`; `a` estreita depois do `if`) — eixo à
   parte, não pedido explicitamente, mas nasce da mesma máquina.

**Sítios que mudariam, no mínimo (contagem, não desenho)**: `type_if` + `type_if_stmt`
(`typer.tks`, 2 funções), mais a MESMA correcção do §4.3 (o `null`-literal-em-comparação não
codegenar) que é PRÉ-REQUISITO — sem essa, a comparação nem chega a compilar, logo o estreitamento
seria inútil por cima de uma base que já não compila. É trabalho de checker (typing), não de
codegen/lowering — os dois backends herdam automaticamente qualquer ambiente mais estreito que o
checker produza, exactamente como hoje herdam (sem estreitar) o mesmo env.

---

## Tabela final

| caso | funciona (nas duas rotas, valor certo) | não compila (nas duas rotas — buraco de linguagem) | compila mas dá resposta ERRADA |
|---|---|---|---|
| `str == / !=` (lit/var/concat/vazia/multi-byte/emoji), todas as posições varridas | ✔ | | — nenhuma encontrada |
| `char == / !=` (lit/var, incl. acentuado) | | ✔ (mesmo erro estrutural nas duas rotas) | — |
| `[]T == / !=` (i64, str, vazio, comprimentos diferentes) | | ✔ (mesmo erro estrutural nas duas rotas) | — |
| `x != null` / `x == null` sobre `T \| null` explícito | | ✔ (codegen, nas duas rotas — anterior ao estreitamento) | — |
| estreitamento de `a` dentro de `if a != null {…}` (o exemplo do dono) | | não existe hoje — funcionalidade em falta (checker, não bug) | — |
| `match … as n` liga tipo estreito ao NOME `n` | ✔ (rota C; nativo diverge por razão à parte) | | — |

**Nenhum caso na coluna "compila mas responde ERRADO"** foi encontrado nas quatro áreas medidas —
onde as duas rotas produzem um resultado, produzem o resultado CERTO (strings, em toda a
combinatória testada, e a bind de `match`). O padrão dominante aqui não é "resposta silenciosamente
errada" — é "recusa honesta em ambas as rotas" (char, slice, comparação-contra-null) ou "recusa
honesta e simétrica no checker" (estreitamento por `if`). É exactamente o tipo de achado que o dono
pediu para procurar: um buraco que ninguém vê sozinho porque as duas rotas concordam silenciosamente
em não fazer, e nenhuma delas grita alto o suficiente para ser óbvio sem medir os dois lados lado a
lado.
