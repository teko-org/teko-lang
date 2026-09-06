# Ranges e açúcar de corte — `a[i..j]`: medição, desenho, e o que a lei já decidiu

> **Status:** MEDIÇÃO E DESENHO. **Zero código de produto.** Página nova — não toca em nenhum
> ficheiro que o implementador do degrau 23 ou da branch `cargo/0.3.1.0-array-literal-escape` leia.
> **Base:** `cargo/0.3.1-ranges-e-corte-desenho` @ `63e7302` (de `origin/remodel/0.3.1.0-linux-native-2`).
> **Método:** toda a medição de VALOR foi feita com `.gen1b/teko` sobre projectos descartáveis em
> scratchpad, nunca dentro da árvore, pela rota C. Nenhum número desta página é adjectivo.

## Resposta de uma linha

**São DUAS lanes e um vagão fino, e o subconjunto mínimo que entrega "substring fácil" é a lane 1
sozinha: `s[a..b]` / `s[a..]` / `s[..b]` / `s[..]` / `s[a..=b]` sobre `str`, a cortar CARACTERES,
desaçucarado no CHECKER para dois builtins novos — sem tipo `Range`, sem tocar em `[]T`, sem tocar
nos 104 sítios de corte por bytes que já existem.**

Lane 2 (`[]T`) é separável e maior porque **`[]T` não tem corte nenhum hoje** — nem builtin, nem
runtime; sete módulos re-escreveram o mesmo laço à mão. O vagão fino é o alinhamento com a lane do
`.len` (§8).

---

## 0. As três leituras do coordenador: uma confirmada, duas refutadas

O coordenador pediu confirmação ou correcção. Uma está certa, duas não.

### 0.1 "Os tokens `DotDot` e `DotDotEq` JÁ EXISTEM" — **CONFIRMADO**

`src/lexer/lexer.tks:640` (`..=`, 3 bytes, testado PRIMEIRO por maximal-munch) e `:671` (`..`,
2 bytes); `src/lexer/token.tks:87`/`:147`. **Zero trabalho de lexer.** O desenho abaixo não pede
um único token novo.

### 0.2 "Teko escreve ranges em TRÊS posições sintácticas" — **REFUTADO EM PARTE: são duas**

O spread não é um range.

| posição | sítio | o que o `..` significa lá | é um range? |
|---|---|---|---|
| cabeça de laço | `loop_head.tks:727` (`..`) / `:730` (`..=`) | limite inferior..limite superior | **SIM** |
| padrão de `match` | `parse_pattern.tks:92` | `lo ..= hi` | **SIM** (só inclusivo — §0.3) |
| literal de array | `parse_expr.tks:607` | **prefixo de SPREAD** (`..xs` = "espalha os elementos de `xs` aqui") | **NÃO** |

Medido, projecto descartável, rota C:

```teko
let xs: []i64 = [1, 2]
let ys: []i64 = [0, ..xs, 3]     // ys_len=4 ys3=3
```

`..xs` não tem limite superior, não tem semântica de limite, e não produz um intervalo — produz
N elementos. É o MESMO token com um significado inteiramente diferente.

**Isto importa duas vezes.** (a) A leitura correcta do argumento interno é *"Teko já diz `..` em
DUAS posições"* — e continua a ser o argumento mais forte, porque as duas que são ranges são-no com
a mesma semântica que o corte precisa. (b) O spread **é dono do `..` prefixo dentro de `[`**, e é
por isso que um range de primeira classe não cabe na gramática (§3).

### 0.3 "`..` vs `..=` já distinguidos nos padrões" — **REFUTADO**

Os padrões **só aceitam `..=`**. `parse_pattern_range` (`parse_pattern.tks:92`) testa
`TokenKind::DotDotEq` e mais nada; `RangePattern` está documentado `lo ..= hi (inclusive)`
(`pattern.tks:7`). Um `..` num padrão não é range — simplesmente termina o padrão.

Quem distingue os dois é a **cabeça de laço**, e distingue-os bem: `parse_loop_binding` testa
`DotDot` (exclusivo) e `DotDotEq` (inclusivo) em linhas consecutivas. A coerência que o coordenador
invocou existe — vem do laço, não dos padrões.

### 0.4 Uma quarta correcção, que ninguém pediu: **não se corta por `str_slice_chars`; corta-se por BYTES**

O coordenador escreveu *"hoje corta-se por CHAMADA: `str_slice_chars`"*. Medido: `str_slice_chars`
é **10 chamadas**. O corte real do corpo é a família de BYTES, com **104**. E essa família está
errada (§2).

---

## 1. O número: **154 sítios de corte, 12 nomes**

Contagem sobre linhas de CÓDIGO (comentários e doc-comments excluídos), toda a árvore (`src/`,
`tooling/`, `examples/`).

### 1.1 Corte de `str` por builtin — 114 chamadas, 4 nomes

| nome | indexa | chamadas | ficheiros | lowering nativo |
|---|---|---|---|---|
| `teko::str::slice(s, u64, u64)` | **BYTES** | **54** | 15 | sim (`tk_str_slice_len`) |
| `teko::str::slice_to(s, u64)` | **BYTES** | **27** | 13 | sim (`tk_str_slice_to_len`) |
| `teko::str::slice_from(s, u64)` | **BYTES** | **23** | 9 | sim (`tk_str_slice_from_len`) |
| `str_slice_chars(s, i64, i64)` | **CARACTERES** | **10** | 4 | **NÃO** |
| | | **114** | | |

Assinaturas em `src/checker/scope.tks:579`/`:616`/`:636`; tabela nativa em
`src/lir/lower.tks:3156` (`builtin_str_slice_symbol` — as três de bytes; a de caracteres não está lá).

**104 dos 114 cortam bytes. 10 cortam caracteres.** As duas famílias são indistinguíveis no sítio de
chamada: `slice(s, 1, 3)` e `str_slice_chars(s, 1, 3)` lêem-se igual e fazem coisas diferentes.

### 1.2 Corte de `[]T` — **não existe operação nenhuma**, e por isso 7 módulos re-escreveram-na

Não há builtin de corte de `[]T`. Não há função de runtime. `builtin_fn` não tem entrada. O resultado
é o padrão exacto da fábrica `error`: **um nome por módulo, corpos byte-a-byte idênticos.**

| nome | definido em | defs | chamadas |
|---|---|---|---|
| `bytes_slice([]byte,u64,u64): []byte` | `src/crypto/hash.tks:281` | 2 | 12 |
| `slice_bytes([]byte,u64,u64): []byte` | `src/compress/deflate.tks:80` | 2 | 16 |
| `slice_u32([]u32,u64,u64): []u32` | `src/compress/inflate.tks:541` | 1 | 2 |
| `bytes_slice_view([]byte,u64,u64): []byte` | `src/encoding/base64/base64.tks:260` | 2 | 2 |
| `str_slice_bytes([]byte,u64,u64): str` | `src/encoding/json/json.tks:407` | 2 | 6 |
| `str_slice_bytes_view([]byte,u64,u64): str` | `src/encoding/csv/csv.tks:242` | 2 | 2 |
| `io::Buf::slice(self,u64,u64): Buf \| error` | `src/io/stream.tks:182` | 1 | — |
| | | **12** | **40** |

`bytes_slice` e `slice_bytes` são o **mesmo corpo com o nome invertido**:

```teko
mut out: []byte = teko::list::empty()
mut i = from
loop {
    if i >= to { break }
    out = teko::list::push(out, data[i])
    i++
}
out
```

`slice_u32` é o mesmo com `[]u32`. `bytes_slice_view` é o mesmo com um nome que promete uma vista e
entrega uma cópia. **Seis nomes, um laço.**

### 1.3 O total, e o contra-número honesto

**114 + 40 = 154 sítios de corte, sob 12 nomes.** É este o número que justifica o açúcar, do mesmo
modo que os 238 justificaram a fábrica `error`.

**E o contra-número, porque medi-o e não o vou esconder:** o laço-range já existe e o corpo quase
não o usa — **24 cabeças `loop mut i in a..b` na árvore inteira**, e a maioria são fixtures
(`examples/regressions/...`). O compilador escreve `loop { if i >= n { break } … i++ }` à mão. Duas
leituras cabem na evidência: ou o corpo precede o laço-range, ou o laço-range não resolve o problema
que o corpo tem. **Não sei qual, e não invento.** O que o número mostra com segurança é a proporção:
**154 cortes contra 24 laços-range** — o que os devs deste corpo fazem é CORTAR, não iterar
intervalos. O açúcar de corte é a peça pedida; o range-como-valor não é (§3).

---

## 2. A lei já decidiu o corte de `str`: **CARACTERES** — e o corte por bytes de hoje é ilegal

O ponto 4 do briefing pedia prova com `café🐝`. Aqui está, e vai mais longe do que o pedido.

### 2.1 Medido: o corte por bytes fabrica `str` inválida

`"café🐝"` — 5 caracteres, 9 bytes (`c`,`a`,`f` = 1 cada; `é` = 2; `🐝` = 4).

```
chars_3_5=[é🐝]        str_slice_chars(s, 3, 5)
bytes_3_5=[é]          slice(s, 3, 5)
chars_0_4=[café]       str_slice_chars(s, 0, 4)
bytes_0_4=[caf<EF><BF><BD>]   slice(s, 0, 4)   <-- MOJIBAKE: metade do `é`
len_chars=5   len_bytes=9
```

`slice(s, 0, 4)` devolveu `"caf\xc3"` — **uma `str` que não é UTF-8 válido.**

### 2.2 A lei que isso quebra, nomeada

`TEKO_LEGISLATION.md`, "Text & encoding", literal:

> `str` **always validates** — UTF-8 is **forced**: a `str` *means* valid UTF-8, so it *is* (the
> invariant) … **Governing Law: M.3** (UTF-8 named; `str` cannot lie about being valid) **+ M.1**
> (valid by construction).

Um corte por bytes pode produzir uma `str` que mente sobre ser UTF-8 válido. **Portanto a decisão
"`s[a..b]` corta caracteres" não é uma escolha de desenho — é a única saída que passa em B.36/M.3/M.1.
Não há tensão para o dono arbitrar aqui.**

E é reforçada pelo ruling que o dono já cravou (`docs/memory/text-bytes-escape-hatch-0.3.1.0.md`):
`"café🐝".len == 5` (caracteres), `c'🐝'.len == 4` (bytes). Uma `str` conta caracteres; o corte de
uma `str` corta caracteres. Coerência total.

### 2.3 Achado adjacente — **REPORTADO, não corrigido aqui**

**`teko::str::slice` / `slice_to` / `slice_from` são hoje uma fábrica de `str` inválida, com 104
sítios de chamada.** Na prática quase todos cortam ASCII (nomes de namespace, chaves de manifesto,
paths) e por isso o defeito nunca se manifestou — é *acidentalmente* correcto, não correcto.

Não abro issue (não é a minha alçada) e não o dobro nesta lane. **Recomendação de casa:** dobrar na
lane do **`.len`-conta-caracteres**, que já vai virar a superfície de `str` de bytes para caracteres,
já paga um ciclo de semente, e é o único sítio onde re-apontar estes três nomes para caracteres custa
zero ciclos extra. A alternativa (retirar os três e obrigar `teko::text::bytes_of_str(s)` +
`str_from_utf8` para o corte por bytes) é a escada de escape que o dono já ratificou. **Não é
adiamento: é a lane certa, e ela já está agendada e a bloquear.**

---

## 3. Range como VALOR: **medido, não é preciso — e custa uma regressão de gramática**

O briefing pediu as duas saídas medidas em separado. Estão.

### 3.1 O `..` prefixo já tem dono, e não é o range

`[0, ..xs, 3]` → 4 elementos (medido). Um range aberto-à-esquerda como expressão (`..n`) escreve-se
com o **mesmo token na mesma posição**: dentro de `[`, prefixo, seguido de expressão. `[..n]` passaria
a ter duas leituras — "array com o range `..n`" e "array com os elementos de `n` espalhados". **Não é
resolúvel por precedência: é a mesma forma.**

### 3.2 O `..` infixo partiria todos os laços-range

`parse_loop_binding` (`loop_head.tks:725-731`) lê o limite inferior com `parse_expr_no_struct` e
**depois** testa `DotDot`/`DotDotEq`. Se `parse_expr` aprendesse `..` infixo, `parse_expr_no_struct`
engoliria `0..n` inteiro, o teste de `DotDot` nunca dispararia, e a cabeça cairia no
`err_at(… "a for-each element's mutability is inherited …")`. **As 24 cabeças de laço-range da árvore
deixariam de compilar** até `loop_head` ser re-encanado para consumir um VALOR de range em vez de
desaçucarar no parser.

### 3.3 Em Teko, `..` nunca foi um valor — e é por isso que é grátis

`parse_loop_range` (`loop_head.tks:320-332`) **desaçucara no PARSER** para um laço de 3 partes com um
contador e um `_hi` temporário. Nunca existe um range. `RangePattern` idem: dois `Expr` literais num
struct de padrão, comparados directamente em `checker/match.tks:259`. **Não há tipo `Range` em lado
nenhum**, e `grep DotDot src/checker src/codegen src/lir` devolve **zero** — o token morre no parser.

Um range de primeira classe pediria: um tipo (`Range<T>`) com layout, participação em `variant`/
genéricos, serialização `.tkb`, lowering nas duas rotas, e um `RangePattern` nativo que **hoje ainda
é paragem honesta** (`lir/lower.tks:6711`: *"range match pattern not yet lowered (N2)"*).

### 3.4 Zero sítios precisam dele

Nenhum dos 154 sítios de corte passa um intervalo adiante. Todos passam `(from, to)` e consomem-nos
imediatamente. Um `let r = 1..3` guardável não desbloqueia nenhuma chamada que exista.

### 3.5 O que o C# fez, e porque não obriga

O C# 8.0 (2019) fez **as duas coisas**: `System.Index`/`System.Range` de primeira classe **e** açúcar
que baixa para aritmética. É dado útil — mostra que não são exclusivas — mas não é obrigação: o C# não
tinha o `..` prefixo já ocupado por um spread, e Teko tem (§3.1). **Referência espelhada na
superfície é o Rust; e mesmo o Rust não pode ser copiado inteiro aqui.** Rust faz `..` um OPERADOR de
expressão que produz `std::ops::Range`; Teko não pode, e o desvio nomeia-se: **em Teko `..` é
PONTUAÇÃO dentro de uma forma delimitada, nunca um operador** — como já é no laço e no padrão.

### 3.6 Veredicto

**Range como valor: NÃO.** O pedido do dono ("ranges literais e açúcar de slice-range") **satisfaz-se
inteiramente com a segunda metade.** O que ele descreveu a seguir — *"facilitar acesso de corte em
arrays … fazer substring de forma fácil"* — é literalmente o açúcar, e nenhum dos exemplos dele pede
um range guardável.

---

## 4. A semântica, decisão a decisão — com a ordem "fazer barulho" aplicada

### 4.1 `..` exclusivo, `..=` inclusivo — **ambos, sem tensão**

Interno: a cabeça de laço já distingue os dois com exactamente esta semântica. Externo: Rust
(`a[i..j]` / `a[i..=j]`), eixo SUPERFÍCIE. Zero tensão. Ambos admitidos.

### 4.2 Formas abertas `a[..n]`, `a[n..]`, `a[..]` — **as três, admitidas**

Rust tem as três; a família de builtins de hoje já é de três formas (`slice`/`slice_to`/`slice_from`),
ou seja o corpo já pediu as formas abertas antes de haver açúcar. O C# define as omissões da mesma
maneira (primeiro operando defaulta a `0`). Zero tensão.

**`a[..=n]` é REJEITADO no parser**, com mensagem: um limite superior inclusivo sem limite superior
não quer dizer nada.

### 4.3 Índice a partir do fim (`^1` do C#) — **FORA, e é feature separada**

Três razões, por ordem de força:

1. **Eixo errado.** A superfície espelha **Rust**, e Rust não tem `^n`. O C# é a referência dos
   *addins*, não da sintaxe. A lei obriga a nomear qual das quatro se espelha; aqui é o Rust, e o
   Rust não traz isto.
2. **O `^0` mente.** Na especificação do C#, `^0` é "uma posição DEPOIS do fim": `a[^0]` rebenta ao
   indexar mas `..^0` é válido ao cortar. Um índice que é legal para cortar e ilegal para indexar é
   exactamente o tipo de assimetria que M.3 proíbe.
3. **Já se escreve.** `a[a.len - 1]` compila hoje (§5.1). O ganho é encurtar, não capacitar.

Se o dono quiser `^n`, é uma ratificação própria, com o `^0` decidido explicitamente — **não entra
por arrasto neste desenho.**

### 4.4 Fora dos limites — **PANIC, e já é assim; nada a decidir**

Medido (`slice("abc", 1, 99)`, rota C): `teko: deliberate panic: string slice out of range`, **exit
134** (SIGABRT), com stack trace. `tk_str_slice_chars` faz o mesmo com quatro `tk_panic` distintos
(`teko_rt.c:948-967`): índice negativo, `from > to`, codepoint fora de alcance.

**O runtime já faz barulho.** O açúcar herda-o sem uma linha nova. A ordem do dono ("a ordem é fazer
barulho") está satisfeita por construção, e um `null` em vez de panic seria uma REGRESSÃO face ao que
já existe. **Nada a arbitrar.**

*(Nota: o C# admite fatia vazia sobre colecção vazia — `a[0..0]` é válido. Teko idem: `from == to` é
válido nos dois builtins e devolve uma `str`/`[]T` vazia. Só `from > to` faz barulho.)*

### 4.5 **Critério de admissão: FECHADO E VERIFICADO PELO CHECKER — nada de convenção**

Aplicando a ordem do dono, e contra o modelo do C#.

O C# admite corte **por padrão não verificado**: um tipo ganha corte de graça se tiver `Length` ou
`Count`, um indexador `int`, e um `Slice(int, int)` — e a própria especificação diz que *a linguagem
não impõe nem verifica a semântica destes membros*. É suposição por convenção. **Rejeitado por ordem
explícita do dono.**

O critério de Teko é uma **lista fechada, decidida sobre o tipo RESOLVIDO do receptor pelo checker**:

| tipo do receptor | `a[i..j]` |
|---|---|
| `str` | corta **CARACTERES** → `str` |
| `[]T` (`Slice`) | corta **ELEMENTOS** → `[]T` |
| **tudo o resto** | **erro alto e nomeado** |

E os casos de rejeição não são "tudo o resto" genérico — cada um tem a sua frase, porque a mensagem
faz parte do desenho:

- **`char`** — `"cannot cut a `char`: a codepoint is ONE unit, and cutting it would produce invalid
  UTF-8 (B.36). Cut the `str` it came from, or take its bytes with `teko::text::bytes_of_str`."`
  (o `char` baixa para `[]byte` e tem `.len`, por isso seria precisamente o que uma admissão por
  convenção deixaria passar em silêncio.)
- **tipo do utilizador (`Named`, `class`, `struct`)** — `"cannot cut a value of type `<T>`: Teko does
  not admit a cut BY CONVENTION. Only `str` and `[]T` are cuttable; declare a method and call it."`
  (via `teko::error::err_typed`, esperado = `"str or []T"`, actual = `type_render(recv.type)`).
- **`Slice { element = Void }`** (o `[]` vazio sem tipo) — `"cannot cut an untyped empty slice — its
  element type is unknown"`, a mesma frase que `type_index_assign` já usa (`typer.tks:4231`).
- **limite não inteiro** — `"a cut bound must be an integer"`, irmã de `"a subscript index must be an
  integer"` (`typer.tks:2690`).
- **corte como alvo de atribuição** (`a[i..j] = v`) — `"cannot assign into a cut: `a[i..j]` is not a
  write target; assign element by element (`a[i] = v`)"`, emitida em `parse_stmt.tks:84-88`, que hoje
  aceita qualquer `Index` como LHS.

---

## 5. `str` vs `[]T`: **são dois pedidos diferentes, e a tese do coordenador só vale para um**

A tese a testar era: *"a operação já existe com nome longo, e o açúcar reduz vocabulário em vez de
acrescentar capacidade."*

**Confirmada para `str`. Refutada para `[]T`.**

- **`str`:** a operação existe (`str_slice_chars`, 10 sítios) e o açúcar substitui-a. Reduz
  vocabulário. *Mas com uma emenda:* a operação que o corpo REALMENTE usa (104 sítios de bytes) está
  errada (§2), portanto o açúcar não é só mais curto — é **o primeiro corte de `str` correcto por
  construção que a linguagem oferece**. Acrescenta correcção, não só brevidade.
- **`[]T`:** **não existe operação nenhuma.** Nem builtin, nem runtime, nem `builtin_fn`. O açúcar
  **acrescenta capacidade** — e a prova de que a capacidade falta são os 7 nomes / 12 definições /
  40 chamadas que a re-implementaram à mão (§1.2).

É esta assimetria que separa as duas lanes.

### 5.1 O que já compila hoje (medido, para não desenhar o que existe)

```teko
fn f(x: u64): u64 { x + 1 }
let a: []u64 = [10, 20, 30]
a[f(0)]        // → 20      ✓ compila e corre
let s: str = "café🐝"
s[1]           // → 97      ✓ compila; devolve BYTE
```

`parse_postfix` (`parse_expr.tks:474`) lê o índice com `parse_expr` completo — **subscrito por
expressão arbitrária já funciona.** `type_index` (`typer.tks:2687-2696`) tipa `Str → Byte` e
`Slice → element`.

**Aviso que sai daqui e que a lane do `.len` tem de ver:** `s[i]` indexa BYTES enquanto `s.len` vai
passar a contar CARACTERES. Depois dessa lane, `loop mut i in 0..s.len { s[i] }` sub-lê qualquer
string não-ASCII, em silêncio. **Não é criado por este desenho** (existe hoje na forma latente) e
**não o resolvo aqui** — reporto-o, porque é a mesma família do §2.3 e o mesmo dono.

---

## 6. "buscar um info por uma expressão na chave" — **NÃO PERCEBI. Halt parcial.**

Como o coordenador instruiu: digo que fiquei em dúvida, em vez de adivinhar.

Duas leituras cabem na frase, e **medi as duas**:

**(A) "expressão arbitrária dentro dos colchetes"** — `a[f(x)]`, `a[i + 1]`, `a[base + off * 2]`.
**JÁ FUNCIONA** (§5.1, medido: `a[f(0)]` → 20). Se é isto, não há trabalho nenhum — só falta dizê-lo
ao dono.

**(B) "indexar um MAPA pela CHAVE"** — `m["nome"]`, `m[expr]`. **NÃO EXISTE.**
`teko::collections::Map<V>` (`src/collections/map.tks:30`) é uma `class` com chaves `str` e acesso por
método: `get(k): V | null`, `insert`, `contains`, `remove`, `keys`. Não há `m[k]`.

**A palavra "chave" pesa para (B)** — em (A) chamar-se-ia índice, não chave. E as três linguagens que
o dono citou (Python, C#, TS) fazem todas `d[key]` em dicionários. Mas **(A) também encaixa** na
frase, e como (A) já funciona, é possível que ele simplesmente não saiba.

**Não decido, e a diferença é grande:** (B) é uma terceira lane inteira — desaçucarar `m[k]` para
`Map::get` obriga o checker a resolver um método de instância a partir de um subscrito, e o resultado
seria `V | null`, o que faz de `m[k]` a única forma de subscrito de Teko que devolve uma união. **É
uma decisão de produto, não uma tensão de lei.** Pergunta ao dono qual das duas, e diz-lhe que (A) já
funciona.

---

## 7. Custo nativo — **o molde existe, medido, e para as duas lanes**

### 7.1 `str`: o molde é o do degrau 11, e a família de caracteres NÃO está lá

`builtin_str_slice_symbol` (`lir/lower.tks:3156`) tem `slice`/`slice_to`/`slice_from` a apontar para
`tk_str_slice_len`/`tk_str_slice_to_len`/`tk_str_slice_from_len` — os *twins* de comprimento por
out-parameter que o degrau 9 inventou. `str_slice_chars` **não está na tabela** (nem `char_at`): uma
chamada directa hoje pára honestamente com `builtin \`str_slice_chars\` not yet lowered`.

O doc-comment do próprio `builtin_str_slice_symbol` diz porquê o molde chega: *"Resolving the symbol
here is enough: `lower_call_fat`'s own generic tail (`lower_args` + `lower_len_out_call`) already
appends the hidden length out-slot … so no new lowering ARM is needed."* **Uma entrada de tabela por
builtin novo, zero ARMs de lowering.**

Custo nativo da lane 1: **2 entradas de tabela + 2 wrappers finos em `teko_rt.c`/`.h`** (cada um
delegando na `tk_str_slice_chars` que já existe — zero lógica nova, exactamente a permissão estreita
que a lei dá ao runtime C).

### 7.2 `[]T`: o molde é o do `tk_slice_push`, e serve tal e qual

`lower_list_push` (`lower.tks:7774-7816`) calcula `esz = ltype_size(ltype_of(elem))`, empurra-o como
argumento, e chama `lower_len_out_call(…, "tk_slice_push", …)`. A assinatura de runtime é
`void *tk_slice_push(const void *ptr, uint64_t len, const void *elem, uint64_t esz, uint64_t *out_len)`
— **paramétrica no tamanho do elemento, uma função para todos os `T`**.

Um corte de `[]T` cabe no MESMO molde:
`void *tk_slice_cut(const void *ptr, uint64_t len, uint64_t from, uint64_t to, uint64_t esz, uint64_t *out_len)`.
Custo: **1 braço em `lower_list_builtin` + 2 funções de runtime**, sem tocar em isel, regalloc ou
encoder.

### 7.3 Rota C

`codegen.tks:4130` já emite `tk_str_slice_chars` directamente (retorno `tk_str` por valor — a rota C
não precisa dos twins `_len`). O push emite `(T *)tk_slice_push(b.ptr, b.len, &item, esz, &n)`
(`codegen.tks:3508`) e reconstrói o struct de slice a partir de `ptr`+`len`. **As duas formas do corte
copiam-se de linhas que já lá estão.**

---

## 8. Colisões — duas, e uma delas é alta

### 8.1 Com a lane do `.len`-conta-caracteres — **BLOQUEIO PARCIAL, provado**

Medido, rota C:

```teko
let s: str = "café🐝"
str_slice_chars(s, 0, s.len to i64)
// teko: deliberate panic: str_slice_chars: codepoint index out of range
```

**Um corte de CARACTERES limitado por um comprimento de BYTES rebenta.** Enquanto `s.len` contar
bytes, `s[0..s.len]` é um panic garantido em qualquer string não-ASCII — e `s[0..s.len]` é a coisa
mais natural que um dev escreve.

**Isto NÃO bloqueia a lane 1, e é por isso que o desenho tem a forma que tem.** Ao dar ao corte de
`str` uma forma `cut_from` que calcula o fim DENTRO do runtime (§9.2), `s[a..]` e `s[..]` nunca
mencionam `.len` e funcionam hoje. Só a forma `s[a..s.len]`, escrita à mão pelo dev, fica exposta —
e essa é a lane do `.len`, não esta.

**Sequenciamento recomendado:** lane 1 pode entrar ANTES da lane do `.len`. A adopção nos 154 sítios
não pode — e não deve entrar nesta lane de qualquer maneira (§9.6, semente).

### 8.2 Com `cargo/0.3.1.0-array-literal-escape` — **DIGO ALTO: partilham o oráculo do stride**

A branch ainda não tem commits sobre a base (`63e7302`), portanto não pude medir o diff dela.
Analiticamente, o ponto de contacto está localizado e é nomeável.

`docs/memory/raiz-comum-dos-degraus-0.3.1.0.md:280` regista o defeito:

> **store de elemento de array literal** — `store_array_elements`, `lower.tks:7060` — `stride` =
> `ltype_size(ltype_of(elem))` = 8 — **DEFEITO SILENCIOSO**

`ltype_size(ltype_of(elem))` é **exactamente o mesmo oráculo** que `lower_scalar_element_push` usa
para o `esz` do `tk_slice_push` (`lower.tks:7808`) e que o `tk_slice_cut` desta lane vai usar.

**A regra, e é curta: o `esz` do corte de `[]T` LÊ o oráculo de stride, nunca o recalcula.** Se a
branch do array-literal mudar `store_array_elements` para um stride de layout (elementos gordos com
dois words), o corte tem de ver a mesma mudança pelo mesmo sítio. Se o corte copiar a expressão em
vez de chamar a função, as duas divergem em silêncio e o defeito reaparece do lado do corte.

**Consequência de agenda:** a **lane 2 (`[]T`) deve entrar DEPOIS** de a branch do array-literal
aterrar. A **lane 1 (`str`) não toca em stride nenhum** e é independente. Mais uma razão para a lane 1
ser o subconjunto mínimo.

---

## 9. O plano — crumbs

Cada crumb compila sozinho e é gateável sozinho.

### Lane 1 — `str` (o subconjunto mínimo: "substring fácil")

#### Crumb 1.1 — AST: o subscrito passa a carregar um corte opcional

**Ficheiro:** `src/parser/ast.tks`. **Toca:** `pub type Index`.

**A forma escolhida, e porquê esta e não um `ExprKind` novo.** `Index.index` passa a ser **o limite
INFERIOR** no caso de corte, e a forma de elemento fica com `cut = null`. Medido: `parser::Index`
tem **2 sítios de construção** (`parse_expr.tks:477`, `loop_head.tks:445`) e **4 sítios de match**
(`loop_head.tks:479`, `typer.tks:153`, `:3240`, `:4228`, `resolve.tks:2442`), e **não é serializado
em `.tkb`** (só `checker::TIndex` é, `tkb_write.tks:170`; os `parser::Expr` serializados são os dos
PADRÕES, e um padrão não tem subscrito). Um membro novo em `parser::ExprKind` obrigaria, em vez
disso, a tocar em cada `match` exaustivo sobre `ExprKind`.

**A propriedade que isto compra:** todo o walker que hoje desce por `ix.index` continua correcto sem
uma linha — desce pelo limite inferior, que é exactamente o que deve visitar.

```teko
/**
 * IndexCut — a metade SUPERIOR de um subscrito de corte, `recv[lo..hi]`. Existe como struct
 * separada, e não como campos soltos em `Index`, para que a presença do corte seja UM teste
 * (`cut != null`) em vez de uma combinação de bandeiras que possa exprimir estados impossíveis.
 *
 * O limite INFERIOR não vive aqui: vive em `Index.index`, o mesmo campo que a forma de elemento
 * já usa. Uma forma aberta à esquerda (`recv[..hi]`) recebe do parser um literal `0` sintetizado,
 * porque `recv[..hi]` É `recv[0..hi]` — sintetizar a constante é mais honesto do que uma bandeira
 * "sem limite inferior" que todo o consumidor teria de traduzir para `0` outra vez.
 *
 * @field hi         o limite superior, ou `null` para uma forma aberta à direita (`recv[lo..]`)
 * @field inclusive  `true` para `..=` (o limite superior entra), `false` para `..`
 * @see Index
 * @since 0.3.1.0
 */
pub type IndexCut = struct { hi: Expr | null; inclusive: bool }

/**
 * Index — um subscrito, `recv[…]`. UM nó cobre as duas formas porque as duas são o mesmo gesto e
 * só o checker as pode distinguir de facto: a forma depende do TIPO do receptor, que o parser não
 * conhece.
 *
 * `cut == null` é a forma de ELEMENTO (`recv[i]`) — `str` → `byte`, `[]T` → `T`. `cut != null` é a
 * forma de CORTE (`recv[lo..hi]` e as suas variantes abertas/inclusivas) — `str` → `str` cortada
 * por CARACTERE, `[]T` → `[]T`. Qualquer outro tipo de receptor é recusado alto e por nome no
 * checker: Teko NÃO admite corte por convenção (ordem do dono, 2026-07-29).
 *
 * @field receiver  a expressão cortada ou indexada
 * @field index     o índice (forma de elemento) OU o limite inferior (forma de corte)
 * @field cut       a metade superior do corte, ou `null` na forma de elemento
 * @see IndexCut
 */
pub type Index = struct { receiver: Expr; index: Expr; cut: IndexCut | null }
```

**Também toca:** as 2 construções passam a escrever `cut = null` (comportamento idêntico ao de hoje).

**Gate:** a árvore constrói e o gate completo passa, com **zero mudanças de comportamento**.

#### Crumb 1.2 — Parser: reconhecer as cinco formas

**Ficheiro:** `src/parser/parse_expr.tks`. **Toca:** `parse_postfix` (o braço `LBracket`, linhas
469-479), que perde a lógica de corpo para uma função extraída (W15: achatar, extrair).

```teko
/**
 * parse_subscript — o braço `[` … `]` de `parse_postfix`, extraído porque passou a reconhecer
 * CINCO formas onde antes reconhecia uma, e uma cadeia de guardas dentro do laço de postfix
 * empurrava a complexidade ciclomática dele para fora do limite.
 *
 * As formas, e o que cada uma põe em `Index`:
 *
 * | fonte        | `index`   | `cut`                            |
 * |--------------|-----------|----------------------------------|
 * | `a[i]`       | `i`       | `null`                           |
 * | `a[i..j]`    | `i`       | `{ hi = j, inclusive = false }`   |
 * | `a[i..=j]`   | `i`       | `{ hi = j, inclusive = true }`    |
 * | `a[i..]`     | `i`       | `{ hi = null, inclusive = false }`|
 * | `a[..j]`     | `0` sint. | `{ hi = j, inclusive = false }`   |
 * | `a[..]`      | `0` sint. | `{ hi = null, inclusive = false }`|
 *
 * O `0` sintetizado leva a linha/coluna do `..` que o omitiu, para que um diagnóstico sobre ele
 * aponte para o sítio que o dev escreveu e não para a coluna 0.
 *
 * @param tokens       o fluxo de tokens
 * @param recv         a expressão receptora já construída (o nó à esquerda do `[`)
 * @param pos          a posição do `[`
 * @return             o `Index` construído e a posição depois do `]`
 * @throws             quando falta o `]`, quando um limite não é expressão, ou quando um `..=`
 *                     não traz limite superior (`a[i..=]` não quer dizer nada)
 * @since 0.3.1.0
 */
fn parse_subscript(tokens: []lexer::Token, recv: Expr, pos: u64): Parsed<Expr> | error
```

Auxiliares, todas privadas a este ficheiro:

```teko
/**
 * subscript_zero — o literal `0` que uma forma aberta à esquerda (`a[..j]`, `a[..]`) usa como
 * limite inferior, posicionado no token que o omitiu.
 *
 * @param line  a linha do `..` omissor
 * @param col   a coluna do `..` omissor
 * @return      a expressão `0`
 * @since 0.3.1.0
 */
fn subscript_zero(line: u32, col: u32): Expr

/**
 * parse_subscript_hi — o limite superior de um corte, depois de o `..`/`..=` ter sido consumido.
 * Um `]` imediato é a forma aberta à direita e devolve `null`; qualquer outra coisa é uma
 * expressão completa (literais de struct permitidos — o `]` delimitador desambigua o `{`, pelo
 * mesmo motivo que `parse_postfix` já invoca `parse_expr` para o índice de elemento).
 *
 * @param tokens     o fluxo de tokens
 * @param pos        a posição logo a seguir ao `..`/`..=`
 * @param inclusive  se o operador consumido foi `..=` (que EXIGE limite superior)
 * @return           o limite superior (ou `null` para forma aberta) e a posição seguinte
 * @throws           `a[i..=]` — um limite superior inclusivo sem limite superior
 * @since 0.3.1.0
 */
fn parse_subscript_hi(tokens: []lexer::Token, pos: u64, inclusive: bool): ParsedCutHi | error

/**
 * ParsedCutHi — o resultado de `parse_subscript_hi`: o limite superior (ou a sua ausência) e a
 * posição do token seguinte.
 *
 * @field hi    o limite superior, ou `null` numa forma aberta à direita
 * @field next  a posição depois do limite
 * @since 0.3.1.0
 */
type ParsedCutHi = struct { hi: Expr | null; next: u64 }
```

**Também toca:** `src/parser/parse_stmt.tks:84-88` — o teste `is_index` que aceita um `Index` como
alvo de atribuição passa a exigir `ix.cut == null`, com a mensagem do §4.5.

**Gate:** o corte PARSEIA e o checker recusa-o com `"cannot index a value of this type"` (a mensagem
existente) — paragem honesta e loud, sem miscompilação possível. A árvore constrói inteira.

#### Crumb 1.3 — Runtime: os dois twins de caracteres

**Ficheiros:** `src/runtime/teko_rt.c` + `.h` (o runtime C mantido — permissão estreita da lei, e é
o caso (a) literal: *"quando o backend nativo precisa de uma forma que o runtime existente não
oferece"*).

```c
// tk_str_cut — o sub-string de índice de CODEPOINT `from` (inclusive) a `to` (exclusivo), como
// str própria e copiada. Wrapper fino sobre tk_str_slice_chars com limites u64 (os índices de
// Teko são u64; a assinatura i64 de tk_str_slice_chars é anterior a essa convenção). Zero lógica
// nova: os panics de fora-de-limites são os da própria tk_str_slice_chars.
tk_str tk_str_cut(tk_str s, uint64_t from, uint64_t to);

// tk_str_cut_from — do índice de codepoint `from` até ao FIM. A contagem de codepoints é feita
// DENTRO do runtime, de propósito: é o que permite `s[a..]` funcionar sem que o Teko mencione
// `s.len` — e `s.len` conta bytes até a lane do `.len` fechar, o que faria de `s[a..s.len]` um
// panic garantido em qualquer string não-ASCII.
tk_str tk_str_cut_from(tk_str s, uint64_t from);

// os twins de out-parameter, para o backend nativo (molde do degrau 9/11: o LCall captura UM
// registo e um tk_str ocupa o par de dois eightbytes).
const uint8_t *tk_str_cut_len(const uint8_t *p, uint64_t n, uint64_t from, uint64_t to, uint64_t *out_len);
const uint8_t *tk_str_cut_from_len(const uint8_t *p, uint64_t n, uint64_t from, uint64_t *out_len);
```

**Gate:** o runtime compila; nada em Teko chama isto ainda. Gate completo verde por vacuidade.

#### Crumb 1.4 — Checker: assinatura dos builtins + o desaçúcar

**Ficheiros:** `src/checker/scope.tks` (2 entradas em `builtin_fn`), `src/checker/typer.tks`
(`type_index` + as funções novas).

```teko
// scope.tks — junto de `slice`/`slice_to`/`slice_from`, e com a diferença dita no sítio:
//   str_cut(str, u64, u64): str        CARACTERES
//   str_cut_from(str, u64): str        CARACTERES
```

```teko
/**
 * type_index — tipa um subscrito. Despacha primeiro pela FORMA (elemento vs corte) e só depois
 * pelo tipo do receptor, porque as duas formas têm listas de receptores admissíveis diferentes:
 * a de elemento aceita `str` (→ `byte`) e `[]T` (→ `T`); a de corte aceita `str` (→ `str`, por
 * CARACTERE) e `[]T` (→ `[]T`), e recusa tudo o resto por nome.
 *
 * @param ix     o subscrito por tipar
 * @param env    o ambiente de tipos
 * @param table  a tabela de tipos do programa
 * @return       a expressão tipada
 * @throws       num índice/limite não inteiro, ou num receptor que a forma não admite
 */
fn type_index(ix: parser::Index, env: Env, table: TypeTable): TExpr | error

/**
 * type_index_cut — o braço de CORTE de `type_index`: `recv[lo..hi]` e as suas variantes.
 *
 * O corte não é um nó próprio na árvore tipada: desaçucara aqui para uma CHAMADA a builtin, do
 * mesmo modo que uma interpolação com padrão de calendário desaçucara para uma chamada a
 * `to_string` (`desugar_datetime_hole`, typer.tks:2762). Consequência deliberada: nenhum `TExprKind` novo,
 * nenhum braço novo de lowering, nenhuma mudança no codec `.tkb` — as duas rotas de backend
 * recebem uma chamada de builtin que já sabem baixar.
 *
 * O ALVO por forma (`s: str`; `xs: []T`):
 *
 * | fonte        | desaçúcar                                    |
 * |--------------|----------------------------------------------|
 * | `s[a..b]`    | `str_cut(s, a, b)`                           |
 * | `s[a..=b]`   | `str_cut(s, a, b + 1)`                       |
 * | `s[a..]`     | `str_cut_from(s, a)`                         |
 * | `s[..b]`     | `str_cut(s, 0, b)`                           |
 * | `s[..]`      | `str_cut_from(s, 0)`                         |
 * | `xs[a..b]`   | `teko::list::cut(xs, a, b)`      (lane 2)    |
 * | `xs[a..]`    | `teko::list::cut_from(xs, a)`    (lane 2)    |
 *
 * DUAS formas, não três: a forma aberta à ESQUERDA reaproveita a de dois limites com o `0` que o
 * parser sintetizou, porque `0` é uma constante e não pode ser reavaliada; a forma aberta à
 * DIREITA precisa de nome próprio porque a alternativa (`str_cut(s, a, s.len)`) avaliaria o
 * RECEPTOR duas vezes — e `f()[a..]` chamaria `f` duas vezes.
 *
 * `..=` soma 1 ao limite superior com um `TBinary` real, visível na árvore tipada. Não há
 * conversão implícita em lado nenhum: os dois builtins tomam `u64`, que é o tipo de `.len` e de
 * todo o índice deste corpo.
 *
 * @param ix     o subscrito (o seu `index` é o limite inferior)
 * @param cut    a metade superior do corte
 * @param env    o ambiente de tipos
 * @param table  a tabela de tipos do programa
 * @return       a chamada a builtin que realiza o corte, tipada com o tipo do receptor
 * @throws       num limite não inteiro, ou num receptor que não é `str` nem `[]T` — ver
 *               `cut_receiver_error` para a frase de cada recusa
 * @since 0.3.1.0
 */
fn type_index_cut(ix: parser::Index, cut: parser::IndexCut, env: Env, table: TypeTable): TExpr | error

/**
 * cut_receiver_error — a recusa NOMEADA de um receptor não cortável. Teko não admite corte por
 * convenção (ordem do dono, 2026-07-29: "a ordem é fazer barulho"), ao contrário do C#, cuja
 * especificação admite qualquer tipo com `Length`/`Count` + indexador + `Slice` e diz de si
 * própria que não verifica a semântica desses membros. Aqui a lista é fechada e o checker
 * verifica-a; cada recusa tem a sua frase porque a mensagem é parte do desenho.
 *
 * @param t  o tipo resolvido do receptor
 * @return   o erro a devolver, com o par esperado/actual quando faz sentido
 * @since 0.3.1.0
 */
fn cut_receiver_error(t: Type): error

/**
 * cut_builtin_call — constrói a `TCall` a um builtin de corte: `call_ns` vazio (a convenção que
 * marca um builtin — `scope.tks`), sem despacho de interface, com o tipo de retorno já resolvido
 * pelo chamador a partir do tipo do receptor.
 *
 * @param name  o nome bare do builtin (`str_cut`, `str_cut_from`, …)
 * @param args  os argumentos já tipados, em ordem
 * @param ret   o tipo do resultado (o tipo do receptor)
 * @param line  a linha de origem do subscrito
 * @param col   a coluna de origem do subscrito
 * @return      a chamada tipada
 * @since 0.3.1.0
 */
fn cut_builtin_call(name: str, args: []TExpr, ret: Type, line: u32, col: u32): TExpr

/**
 * cut_hi_inclusive — o limite superior de um `..=`, elevado em 1 para o converter no limite
 * exclusivo que os builtins tomam. Um `TBinary` de soma real, não uma bandeira levada até ao
 * runtime: o runtime só conhece uma convenção de limites, e é a exclusiva.
 *
 * @param hi  o limite superior escrito pelo dev
 * @return    `hi + 1`
 * @since 0.3.1.0
 */
fn cut_hi_inclusive(hi: TExpr): TExpr
```

**Gate:** `s[a..b]` compila e corre na ROTA C (o codegen alcança `tk_str_cut` pela mesma via de
`tk_str_slice_chars`); na rota nativa pára honestamente com `builtin \`str_cut\` not yet lowered`.
Crumb verde com uma paragem nomeada, nunca com uma miscompilação.

#### Crumb 1.5 — Codegen (rota C) e Lowering (rota nativa)

**Ficheiros:** `src/codegen/codegen.tks` (2 linhas ao pé da de `tk_str_slice_chars`, `:4130`);
`src/lir/lower.tks` (uma função de tabela nova, registada em `native_builtin_symbol`).

```teko
/**
 * builtin_str_cut_symbol — o twin de runtime dos DOIS builtins de corte por CARACTERE que este
 * subconjunto N1 baixa: `str_cut`/`str_cut_from`, cada um resolvendo para o twin de comprimento
 * por out-parameter (`tk_str_cut_len`/`tk_str_cut_from_len`), o MESMO molde `lower_len_out_call`
 * que `builtin_str_slice_symbol` usa para a família de BYTES. Resolver o símbolo aqui chega: a
 * cauda genérica de `lower_call_fat` já acrescenta o slot de comprimento e já achata o receptor
 * `str` no seu par (ptr, len).
 *
 * Porquê uma tabela SEPARADA da de bytes, e não duas entradas na mesma: as duas famílias contam
 * unidades diferentes (caracteres vs octetos), e um leitor que veja `slice` e `cut` na mesma
 * função assume que contam o mesmo. A separação é a documentação.
 *
 * @param last  o último segmento do caminho da chamada (o nome bare do builtin)
 * @return      o twin `tk_str_cut*_len`, ou `null` quando `last` não nomeia nenhum dos dois
 * @since 0.3.1.0
 */
fn builtin_str_cut_symbol(last: str): str | null
```

**Gate — RITUAL COMPLETO.** As duas rotas produzem o mesmo valor; o canal `own_native` fecha com
exit 42; `diff <(./outc/probe) <(./outn/probe)` idêntico byte a byte.

### Lane 2 — `[]T` (a capacidade que falta) — **depende de `cargo/0.3.1.0-array-literal-escape`**

#### Crumb 2.1 — Runtime: `tk_slice_cut` / `tk_slice_cut_from`

Molde de `tk_slice_push`, paramétrico em `esz`:

```c
void *tk_slice_cut(const void *ptr, uint64_t len, uint64_t from, uint64_t to,
                   uint64_t esz, uint64_t *out_len);
void *tk_slice_cut_from(const void *ptr, uint64_t len, uint64_t from,
                        uint64_t esz, uint64_t *out_len);
```

Fora de limites: `tk_panic`, com as mesmas três frases que `tk_str_slice_chars` já usa.

#### Crumb 2.2 — Checker: `teko::list::cut` / `cut_from` + o braço `Slice` de `type_index_cut`

`teko::list::*` já é uma família de builtins genéricos intercetada no lowering, e o checker já lhe
resolve o tipo de elemento a partir do argumento (`typer.tks:861-893`, os braços de `empty`/`push`).
`cut`/`cut_from` entram por lá: `cut([]T, u64, u64): []T`.

#### Crumb 2.3 — Lowering: um braço em `lower_list_builtin`

```teko
/**
 * lower_list_cut — `teko::list::cut(xs, from, to)` / `cut_from(xs, from)`: o corte de um `[]T`
 * baixa para a entrada de runtime paramétrica em tamanho de elemento, exactamente como o push
 * (`lower_list_push`). O `esz` é LIDO do mesmo oráculo de stride que o push e o store de literal
 * de array usam (`ltype_size(ltype_of(elem))`) e NUNCA recalculado aqui: um elemento gordo mudou
 * de stride uma vez e o defeito foi silencioso; três consumidores a ler o mesmo oráculo é a
 * única forma de a próxima mudança os apanhar aos três.
 *
 * @param ctx  o contexto de lowering
 * @param e    a expressão de chamada (tipo e posição)
 * @param cl   a chamada verificada
 * @param sym  o símbolo de runtime (`tk_slice_cut` ou `tk_slice_cut_from`)
 * @return     o par (ptr, len) do corte
 * @throws     num elemento de largura não fixável, ou propagado do lowering dos limites
 * @since 0.3.1.0
 */
fn lower_list_cut(ctx: LowerCtx, e: checker::TExpr, cl: checker::TCall, sym: str): LoweredFat | error
```

**Gate — RITUAL COMPLETO**, e adicionalmente: `[]T` de elemento GORDO (`[]str`, `[]char`) cortado nas
duas rotas, contra o oráculo de stride que a branch do array-literal deixou.

### 9.6 O que NÃO entra em nenhuma das duas lanes: a adopção

**Os 154 sítios NÃO são reescritos aqui.** A semente do bootstrap é o binário `teko` publicado
anterior, e o corpo não pode USAR uma forma de linguagem que a semente dele não conhece. Sequência
obrigatória: **feature aterra (o compilador ACEITA `a[i..j]`) → release + semente nova → só então a
adopção no corpo**, numa carga própria. Misturar as duas partiria o fixpoint gen1==gen2.

---

## 10. Fixtures de regressão

### 10.1 Canal `examples/regressions/own_native` — valor nas DUAS rotas

O canal é `src/corpus.tks` (funções `f_*` devolvendo 0 ou um código distinto por falha) + `main.tks`
(o despachante que mapeia cada falha para um exit code). Códigos livres a partir de **60** (o último
usado é 59, `f_err_typed`).

| fn | código | o que prova |
|---|---|---|
| `f_str_cut_ascii` | 60 | `"hello world"[6..11] == "world"`; `[..5] == "hello"`; `[6..] == "world"`; `[..] == s`; `[6..=10] == "world"` |
| `f_str_cut_multibyte` | 61 | **`café🐝`**: `[0..4] == "café"` (o corte por bytes daria `caf\xc3`), `[3..5] == "é🐝"`, `[4..] == "🐝"`, e `s[0..4].len` coerente com o contador de `str` em vigor |
| `f_str_cut_empty` | 62 | `s[2..2] == ""`; `""[..] == ""`; `""[0..0] == ""` — fatia vazia é válida, não é panic |
| `f_str_cut_expr_bounds` | 63 | `s[f(x)..g(y)]` — os limites são expressões arbitrárias; **apanha um walker que se esqueça de descer por `cut.hi`** (se `g` não for resolvida/monomorfizada, o build falha alto) |
| `f_slice_cut_scalar` | 64 | `[]u64` e `[]byte`: `[1..3]`, `[..2]`, `[2..]`, `[..]` (lane 2) |
| `f_slice_cut_fat` | 65 | **`[]str` e `[]char`** — elemento gordo, o eixo do stride do §8.2 (lane 2) |
| `f_slice_cut_empty` | 66 | `xs[1..1].len == 0`; `[][..]` (lane 2) |

Cada uma entra em `own_native.tkr` como `Scenario: … / When built and run / Then exit = 42`.

### 10.2 Canal `examples/regressions/diagnostics` — as recusas, uma por frase

Fixtures `When compilation fails / Then diagnostic = "…"`. **A ordem "fazer barulho" é o que estas
fixtures protegem** — sem elas, uma recusa que se degradasse em aceitação silenciosa passaria.

| caso | fonte | diagnóstico exigido |
|---|---|---|
| `cut_on_user_type` | `let p = P { }; p[0..1]` | `"cannot cut a value of type \`P\`: Teko does not admit a cut BY CONVENTION"` |
| `cut_on_char` | `c'é'[0..1]` | `"cannot cut a \`char\`"` |
| `cut_bound_not_integer` | `s["a".."b"]` | `"a cut bound must be an integer"` |
| `cut_as_assign_target` | `xs[0..2] = ys` | `"cannot assign into a cut"` |
| `cut_inclusive_open` | `s[1..=]` | `"an inclusive range needs an upper bound"` |
| `cut_untyped_empty` | `let e = []; e[0..1]` | `"cannot cut an untyped empty slice"` |

### 10.3 Panic de fora-de-limites — exit **134**

`s[1..99]` sobre `"abc"` e `xs[5..2]`: **exit 134** (SIGABRT), nas duas rotas. Medido hoje para a
família de bytes; a de caracteres herda-o de `tk_str_slice_chars`. Sem esta fixture, um `null`
silencioso poderia entrar num refactor futuro sem que ninguém desse por isso.

### 10.4 Fixture de NÃO-regressão do spread

`[0, ..xs, 3]` com `xs = [1,2]` → 4 elementos. **É a fixture que prova que o corte não roubou o `..`
ao spread.** Sem ela, uma mudança de gramática pode partir o spread e só se descobrir no self-build.

---

## 11. Pontos de ritual (onde o gate completo tem de passar)

1. **Fim do crumb 1.1** — mudança de AST com comportamento idêntico. Gate completo + `teko test .`.
2. **Fim do crumb 1.2** — o corte parseia e para honestamente. Gate completo; nenhuma fixture de
   valor ainda.
3. **Fim do crumb 1.5 — RITUAL MAIOR.** Lane 1 fechada: as duas rotas com valor idêntico
   (`diff` byte a byte), `own_native` a exit 42, `diagnostics` com as seis frases, fixpoint
   gen1==gen2, self-host verde. **É aqui que "substring fácil" está entregue.**
4. **Fim do crumb 2.3 — RITUAL MAIOR.** Lane 2 fechada, com o eixo do elemento gordo contra o
   oráculo de stride que a branch do array-literal deixou.
5. **Vagão de semente (fora destas lanes)** — release, semente nova, e SÓ DEPOIS a adopção nos 154
   sítios, em carga própria.

---

## 12. Riscos e tensões

| # | risco | resolução recomendada |
|---|---|---|
| R1 | **Um walker esquece-se de descer por `cut.hi`.** É a contrapartida de reaproveitar `Index` em vez de criar um `ExprKind` novo: um walker não actualizado compila e visita menos. | Fixture `f_str_cut_expr_bounds` (§10.1) com uma CHAMADA no limite superior — um `hi` não visitado deixa a chamada por resolver e o build cai alto. Além disso, o crumb 1.1 é gateado sozinho, com `cut` sempre `null`, o que isola a mudança de forma da mudança de comportamento. |
| R2 | **Colisão de stride com `cargo/0.3.1.0-array-literal-escape`** (§8.2). | Lane 2 depois dessa branch aterrar; e `lower_list_cut` LÊ `ltype_size(ltype_of(elem))`, nunca o recalcula. |
| R3 | **`s[0..s.len]` rebenta enquanto `.len` contar bytes** (§8.1, medido). | Não é criado por esta lane e não a bloqueia: as formas abertas não mencionam `.len` (é por isso que `str_cut_from` existe). Fecha sozinho quando a lane do `.len` fechar. |
| R4 | **Vocabulário cresce em 2 nomes antes de encolher.** `str_cut`/`str_cut_from` entram enquanto `slice`/`slice_to`/`slice_from` ficam. | Honesto e temporário: os três de bytes estão ERRADOS (§2) e a sua retirada/re-apontamento pertence à lane do `.len`, que já paga o ciclo de semente. Digo-o em vez de fingir que o saldo é imediato. |
| R5 | **A rota nativa ainda não baixa `RangePattern`** (`lower.tks:6711`). | Sem relação: este desenho não toca em padrões. Registado para que ninguém leia o corte como tendo fechado aquele buraco. |

**Tensões de lei genuínas: ZERO.** As quatro decisões que pareciam abertas fecharam-se por lei ou por
medição, e nenhuma sobrou para o dono:

- **caracteres vs bytes** → B.36/M.3/M.1 (`str` não pode mentir sobre ser UTF-8 válido) + o ruling
  do `.len` já cravado. **Fechada por lei.**
- **inclusivo/exclusivo e formas abertas** → coerência com a cabeça de laço + superfície Rust.
  **Fechada por lei.**
- **fora-de-limites** → o runtime já faz `tk_panic`; a ordem do dono ("fazer barulho") confirma o
  que já existe. **Fechada por medição.**
- **admissão por convenção** → ordem explícita do dono, contra o modelo do C#. **Fechada por ruling.**

**O que HALTA, e é a única coisa: o ponto 5 do briefing (§6).** Não sei se "buscar um info por uma
expressão na chave" é `a[f(x)]` (que já funciona, medido) ou `m[chave]` sobre `Map<V>` (que não
existe e é uma terceira lane). **Não adivinho.** O coordenador pergunta; o desenho acima está
completo e entregável sem essa resposta, e nenhum crumb dele muda conforme ela.

**→ RESOLVIDO. Ver Apêndice A.**

---

# Apêndice A — o modelo de acesso do C# por inteiro

> **Origem:** resposta do dono, 2026-07-29, literal: *"já respondi, quis dizer sobre fazer acessos
> como o C# faz"*. **Acrescento, não revisão:** nada em §0–§12 muda, e a resposta de uma linha
> mantém-se — a lane 1 continua a ser o subconjunto mínimo que entrega "substring fácil".

## A.0 O modelo, item a item, e onde cada peça aterra

| peça do modelo de acesso do C# | Teko |
|---|---|
| `a[i]` — índice inteiro | **já existe** |
| `a[expr]` — expressão na posição do índice | **já existe** (medido, §5.1) |
| `d[chave]` — indexador por chave, **lança** se ausente | **lane 3** — `m[k]` → `Map::at`, **panic** |
| `d.TryGetValue(k, out v)` — a forma segura | **já existe** — `Map::get(k): V \| null` |
| `a[1..3]`, `a[..n]`, `a[n..]`, `a[..]`, `a[1..=3]` | **lanes 1 e 2** |
| `System.Range` de 1.ª classe | **RECUSADO** (§3 — medido) |
| `System.Index` / `^n` | **RECUSADO** (§4.3 + A.4) |
| admissão por FORMA (`Length`/`Count` + indexador + `Slice`) | **RECUSADO** (ordem do dono, §4.5) |

Cinco peças entram, três ficam fora, cada uma com a sua razão já escrita.

**E a objecção de §6 cai, pelo lado que o coordenador apontou.** Escrevi que `m[k]` seria "o único
subscrito de Teko a devolver uma união". Cai porque o indexador do C# **não devolve união nenhuma**:
lança. O par certo é `d[key]` lança / `TryGetValue` devolve — e Teko já tem a metade segura escrita
(`Map::get -> V | null`). Falta-lhe a metade barulhenta, que é a que o subscrito usa. Sem união, sem
excepção à regra do barulho, e coerente com o que §4.4 mediu: fora-de-limites **já é panic** hoje.

## A.1 Custo de `m[chave]` — **mecanismo DIFERENTE do corte, e lowering ZERO**

**Não é o mesmo mecanismo.** A diferença é a que importa para orçamentar:

| | corte (lanes 1/2) | `m[k]` (lane 3) |
|---|---|---|
| alvo do desaçúcar | `TCall` a **builtin** (`call_ns = ""`) | **método de instância** de uma classe declarada |
| resolução de nome | nenhuma — o nome é literal | busca de método, visibilidade `intern`, defargs |
| monomorfização | nenhuma | **sim** — `Map<V>` é genérica |
| função de runtime nova | 4 (`tk_str_cut*`, `tk_slice_cut*`) | **0** |
| entrada em `native_builtin_symbol` | 4 | **0** |
| linha nova em `codegen.tks` | 4 | **0** |

`type_method_call` (`typer.tks:1278`) já faz metade do trabalho: reescreve `.m(a)` numa
`parser::Call` com o receptor a viajar em `args[0]` e encaminha para a tipagem de chamada normal —
que já carrega a busca de método, a visibilidade e a estampagem genérica. **A lane 3 desaçucara para
CÓDIGO TEKO**, não para uma primitiva; os dois backends baixam-no como baixam qualquer chamada de
método. Por isso o lowering custa zero: não há nada de novo para baixar.

O que é preciso, e é tudo:

**(a) um braço `Named` em `type_index`**, que reescreve `Index{receiver, index}` em
`parser::MethodCall { receiver, method = "at", args = [index] }` e delega em `type_method_call`. O
limite (`cut != null`) é recusado: **um `Map` não se corta** — não tem ordem, e cortar por chave não
quer dizer nada. Frase: `"cannot cut a \`Map<V>\`: a map has no order — subscript it by ONE key"`.

**(b) um método novo em `src/collections/map.tks`**, o irmão barulhento de `get`:

```teko
    /**
     * at — o valor de `k`, ou PANIC se a chave não existir. É o alvo do subscrito `m[k]` e o irmão
     * BARULHENTO de `get`: `get` devolve `V | null` e obriga o chamador a decidir; `at` afirma que
     * a chave existe e faz barulho quando a afirmação é falsa. O par espelha o do C# — `d[key]`
     * lança, `TryGetValue` devolve o sucesso ao lado —, com a excepção trocada pelo `panic` que
     * Teko já usa para todo o acesso fora de limites (a ordem do dono, 2026-07-29).
     *
     * Chama a função LIVRE `map_find_index` e NÃO `self.get(k)`: um método de instância genérico
     * que chama um irmão em `self` pode falhar a ligar no nativo (o irmão estampado nunca é
     * emitido) — a razão que o doc-comment deste módulo já regista, e a razão por que os
     * ajudantes partilhados desta classe são funções livres.
     *
     * @param k  a chave a procurar
     * @return   o valor associado a `k`
     * @throws   panic `"Map::at: key not found"` quando `k` não está no mapa — a forma que NÃO
     *           faz barulho é `get`
     * @see get
     * @since 0.3.1.0
     */
    pub fn at(self, k: str): V
```

**Um subscrito por chave INTEIRA não existe** e não é criado por arrasto: `Map<V>` tem chaves `str`
(a razão está no doc-comment do módulo — traço estrutural opaco sobre parâmetro de tipo). Se um dia
`Map<K, V>` chegar, o braço de `type_index` não muda: continua a delegar em `at`, e é a assinatura
de `at` que passa a aceitar `K`.

## A.2 A lista fechada com TRÊS membros — **continua lista, e a diferença fica mais nítida**

Sim, admite. O critério não afrouxa, porque o critério de Teko nunca foi o do C#:

- o do **C#** é uma **FORMA** — `Length`/`Count` + indexador `int` + `Slice(int,int)` —, e a
  especificação dele diz de si própria que a linguagem não impõe nem verifica a semântica desses
  membros. É suposição.
- o de **Teko** é **IDENTIDADE**, e com dois sabores, ambos factos que o checker **calcula**:
  - `str` e `[]T` — membros **estruturais**: os braços `Str` e `Slice` do `Type` resolvido. Não há
    forma nenhuma a adivinhar.
  - `Map<V>` — membro **nominal**: `Named.name == "teko::collections::Map"`, o nome **CANÓNICO já
    resolvido**, nunca o nome bare. É a mesma disciplina de `call_symbol` (*"decided by the callee's
    ORIGIN NAMESPACE and never by its bare name"*), e o canal de regressão
    `examples/regressions/builtin_name_not_hijacked` existe precisamente para provar que um `Map`
    declarado pelo utilizador não sequestra o do `teko::`.

**A regra que mantém a lista fechada, e tem de ficar escrita no doc-comment de `cut_receiver_error`
e do braço `Named`:** *um membro novo entra por emenda que o NOMEIA; o critério nunca vira teste de
forma.* No dia em que alguém escrever "qualquer tipo com um método `at`", isso É o modelo do C# e
precisa de ratificação nova.

Três nomes ainda se lêem de uma vez. É esse o teste prático de uma lista: cabe numa tabela e um
leitor sabe dizer, sem executar nada, se o seu tipo está lá.

## A.3 Ordem das lanes — **confirmo, com duas correcções**

**Correcção 1 — a lane 3 não depende de nenhuma das outras duas.** `m[k]` é a forma de ELEMENTO
(`cut == null`), que existe hoje; não precisa do `IndexCut` do crumb 1.1 nem de nada da lane 2. Pode
ser puxada para a frente sem custo se o dono quiser `m[k]` cedo. Não recomendo — ver a correcção 2 —
mas o desenho não a prende.

**Correcção 2, e é a que importa — a lane 3 tem um pré-requisito que não é código.** Medido:
**`teko::collections::Map<V>` tem ZERO consumidores na árvore.** As três ocorrências fora do próprio
módulo são doc-comments (`encoding/json/json.tks:26`/`:603`, `codegen/codegen.tks:469`). O único
exercitador é `examples/regressions/bulk/src/q046_collections_map/body.tks` — e o `bulk` está
**DESLIGADO** (registado em `9d7c5c8`: 214 ficheiros que ninguém corre).

Dar açúcar de sintaxe a um tipo cuja única prova está desligada é pôr o açúcar antes da rede. E não
é um risco teórico: `Map<V>` é uma classe **genérica**, e o doc-comment do próprio módulo já regista
**dois** perigos de ligação nativa nesse terreno (construir um genérico aninhado dentro de um corpo
de método genérico; um método de instância genérico a chamar um irmão em `self`). **Pré-requisito da
lane 3: uma fixture VIVA de `Map<V>` no canal `own_native`** (`make`/`insert`/`get`/`at`, valor nas
duas rotas, exit 42) — que também é a primeira prova real de que a classe funciona no nativo.

**Ordem recomendada:**

> lane 1 (`str`) → lane 2 (`[]T`) → **[fixture viva de `Map<V>` no `own_native`]** → lane 3 (`Map`)

Confirmo a tua leitura; acrescento o degrau da fixture entre a 2 e a 3.

**E confirmo a dependência da lane 2 como satisfeita, com uma precisão que muda o R2 da tabela de
riscos.** `63480b2` mexeu no `box_aggregate_value` e no `lower_array_lit` (escape de frame — o
literal devolvido passa a ser boxed em armazenamento por-EXECUÇÃO) e **não** tocou no oráculo de
stride. O oráculo é `elem_byte_stride` (`lower.tks:9782`): `is_fat_type(t) → fat_slot_bytes()`, senão
`ltype_size(ltype_of(t))` — a função que já concilia o elemento gordo com o escalar.

**R2 deixa de ser "esperar" e passa a ser um endereço:** `lower_list_cut` chama **`elem_byte_stride`**,
não `ltype_size(ltype_of(elem))` cru. E o corte fica melhor servido que o push: como `tk_slice_cut` é
paramétrico no stride, **uma chamada cobre o elemento gordo e o escalar**, enquanto o push precisa
dos dois braços (`lower_fat_element_push` / `lower_scalar_element_push`). O corte junta-se como
quarto consumidor do oráculo que o fix acabou de alinhar — não como um quinto sítio a recalculá-lo.

## A.4 `^n` — **confirmo o NÃO, e ganha um quarto argumento, que é o mais forte**

Concordo com a tua leitura. Os três argumentos de §4.3 continuam de pé e o dono não contestou nenhum.
O quarto só ficou visível agora que ele nomeou o modelo INTEIRO:

**`^n` é a parte do modelo do C# que depende da parte que a medição fechou.** Em C# `^1` não é
açúcar: é um valor de `System.Index`, um tipo real, e `System.Range` existe para que `..^1` componha
— um `Index` é passável, guardável, e o indexador de corte recebe um `Range`. Teko mediu que **não
pode ter range de primeira classe**: o `..` prefixo é do spread (§3.1) e o `..` infixo parte as
cabeças de laço (§3.2).

Restam dois caminhos, e ambos estão bloqueados por medições diferentes:

- **`^n` com o modelo completo** (tipos `Index`/`Range` de primeira classe) reabre exactamente o §3
  — a decisão que a medição fechou com mais força.
- **`^n` como açúcar puro** (`a[^1]` → `a[a.len - 1]`) fica sem o tipo que lhe dá sentido em C#, e
  sobra-lhe só a assimetria do `^0` (legal ao cortar, ilegal ao indexar) que M.3 proíbe.

**`^n` FICA FORA.** Se o dono o quiser, é ratificação própria, e tem de decidir o `^0` explicitamente
— não entra por arrasto no "modelo do C#", porque a peça de que ele depende já foi recusada com
medição.
