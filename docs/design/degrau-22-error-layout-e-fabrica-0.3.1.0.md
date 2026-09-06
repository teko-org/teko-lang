# Degrau 22 — o layout nativo do `error`, e a fábrica `error::new` (0.3.1.0)

> **Status:** DESENHO E MEDIÇÃO. Nenhum código de produto foi escrito nesta passagem.
> **Base:** `origin/remodel/0.3.1.0-linux-native-2` @ `8c57bcf`.
> **Medido com:** `.gen1b` (a escada `build_gen1_from_c.sh bootstrap/teko.c src .gen1` →
> `TEKO_BACKEND=c .gen1/teko . -o .gen1b --no-verify --release` → relink), nunca com a semente
> crua. Sondas em projectos descartáveis fora da árvore.

## Resposta de uma linha

**Degrau.** 4 ficheiros, ~380 linhas, **zero C**, **zero superfície**, **zero ciclo de semente** —
o mesmo molde do degrau 21 (419 linhas, 7 ficheiros, com C). A fábrica **não dissolve** o degrau
22: dissolve-o ao contrário — `error::new_pos(msg, line, pos, file)` é **impossível de baixar** no
backend nativo enquanto `error_struct_layout` não carregar `line`/`col`/`file`, logo **o degrau 22
é o alicerce da fábrica, não o seu concorrente**.

---

## 1. As três correcções que a medição obriga

### C1 — "2148 sítios constroem `error { ... }`" — **são 928**

| grep em `src/**/*.tks` | ocorrências |
|---|---|
| `error {` (total) | **2152** |
| `error {` no FIM da linha (abertura de corpo de `fn … -> T \| error {`) | **1214** |
| corpo de uma linha (`fn f(): T \| error { expr }`) | **8** |
| **construções reais** (`error { message = `) | **928** |

O número 2148 conta `fn f(): T | error {` como construção. Não é. **A contagem honesta de
construções em `src/` é 928**; no resto da árvore (`examples/`, `tooling/`) são mais **60**, e
**16** ficheiros em `docs/`. Nenhuma delas tem de mudar para o degrau 22.

### C2 — `err_loc` é chamado **10** vezes, não 19 — e existe um SEGUNDO builtin que ninguém mencionou

| builtin | chamadas reais | constrói-e-adorna | adorna um `error` já existente |
|---|---|---|---|
| `teko::error::err_loc` | **10** | 7 | **3** |
| `teko::error::err_typed` | **22** | 19 | **3** |

`err_typed` não aparecia em nenhuma medição anterior e tem **mais do dobro** dos sítios de
`err_loc`. Ambos param o backend nativo, com stops distintos e confirmados por sonda:

```
native backend N1: builtin `err_loc` not yet lowered (N2)
native backend N1: builtin `err_typed` not yet lowered (N2)
```

Os **3 + 3** sítios que adornam um `error` **já existente** (`collect.tks:57`, `typer.tks:3253`,
`typer.tks:6133`; `typer.tks:4101`, `typer.tks:6077`, `typer.tks:6134`) **não são subsumíveis por
construtor nenhum**. Portanto **a fábrica não remove nenhum dos dois builtins**: os dois têm de ser
baixados de qualquer maneira.

### C3 — "o layout do `error` mantém-se com UM campo" — **não pode**

A decisão de achatar o `join` garante que **o `join` não faz o layout crescer**. Isso é verdade e
fica ratificado. Mas o layout **já tem de crescer por outras duas razões, independentes do
`join`**:

1. **7 linhas de código em 3 ficheiros LÊEM os campos de diagnóstico** (`line`, `col`, `expected`,
   `actual`). Nenhum construtor remove uma leitura:

   | ficheiro:linha | leitura |
   |---|---|
   | `src/checker/collect.tks:56` | `e.line` |
   | `src/checker/typer.tks:3253` | `inner.line` |
   | `src/checker/typer.tks:6130` | `inner.line` (guarda + valor) |
   | `src/checker/typer.tks:6131` | `inner.line`, `inner.col` |
   | `src/checker/typer.tks:6134` | `inner.expected`, `inner.actual` |
   | `src/checker/diagnostics.tks:38` | `inner.line` (guarda + valor) |
   | `src/checker/diagnostics.tks:39` | `inner.line`, `inner.col` |

2. **A própria `error::new_pos(msg, line, pos, file)` escreve três campos que um layout de um campo
   não tem onde guardar.** A fábrica proposta pelo dono exige o layout crescido.

Ou seja: `join` achatado preserva a promessa *"o `join` não engorda o `error`"*. Não preserva
*"o layout nunca cresce"* — esse já cresceu na rota C há muito, e é essa divergência que é o degrau
22.

---

## 2. Onde os campos do `error` estão declarados, e quais o compilador LÊ

`error` **não é palavra reservada do lexer**. É resolvido por nome em
`src/checker/scope.tks:385` (`if name == "error" { return Error { } }`), e `Error` é uma etiqueta
vazia em `src/checker/type.tks:94` (`pub type Error = struct { }`). Os campos são **injectados**
em dois sítios distintos e assimétricos:

| | onde | o quê |
|---|---|---|
| **construção** | `src/checker/typer.tks:2513` | exactamente **um** campo, `message: str`. Qualquer outra forma é rejeitada: `` `error` is constructed as `error { message = <str> }` `` |
| **leitura** | `src/checker/typer.tks:2110-2124` | **seis** campos: `message`/`file`/`expected`/`actual` → `str`; `line`/`col` → `u32` |

**A assimetria é a chave de todo este desenho.** Cinco dos seis campos sempre foram *legíveis e
nunca construíveis* por literal. A confirmação da correcção do coordenador está aqui: manter o
literal pina **apenas** `message`, que já estava pinado. Todo o resto do layout continua livre.

**Quais o compilador realmente lê:** `message` (112 sítios), `line` (7), `col` (2), `expected` (1),
`actual` (1). **`file` tem zero leituras e zero escritas** no corpus inteiro — existe só porque a
rota C o carrega e porque `type_field_access` o aceita, logo um programa de utilizador pode lê-lo.

---

## 3. O que a rota C faz hoje — o oráculo

`src/runtime/teko_rt.h:77-101` define o `tk_error` **completo**, e as três funções que o manipulam:

| campo | classe | tamanho | offset |
|---|---|---|---|
| `message` | `tk_str` | 16 | 0 |
| `file` | `tk_str` | 16 | 16 |
| `line` | `uint32_t` | 4 | 32 |
| `col` | `uint32_t` | 4 | 36 |
| `expected` | `tk_str` | 16 | 40 |
| `actual` | `tk_str` | 16 | 56 |
| **total** | | **72** | align 8 |

`tk_error_make` **zera** `file`/`line`/`col`/`expected`/`actual`. `tk_error_loc` e `tk_error_types`
recebem o `error` **por valor** e devolvem uma **cópia modificada** — a origem não é mutada. O
emissor C liga-se a elas em `src/codegen/codegen.tks:3768-3792`.

**Nota de rota:** `tk_error` só existe no `.h` como `static inline`; **não há símbolo ligável** em
`teko_rt.c`. O backend nativo, portanto, **não pode** chamar `tk_error_loc` como gémeo de runtime
— tem de baixar em Teko. Isso é uma vantagem: é o caminho que a lei Teko-only prefere de qualquer
maneira, e evita a questão de devolver 72 bytes por valor através de um `LCall` que só lê um
registo de resultado.

**Oráculo medido** (sonda `probe_read`, projecto descartável): um `error { message = "x" }`
lido na rota C dá `line == 0`, `col == 0`, `file == ""`, `expected == ""`, `actual == ""`,
`message == "x"` — saída 42. A rota nativa para com o stop do degrau 22.

---

## 4. A contagem honesta de sítios a mudar

### 4.1 Layout — **1 função**

`error_struct_layout` (`src/lir/lower.tks:8633`). Regista hoje um campo. O tamanho é **derivado**
(`layout.size`), por isso todos os consumidores acompanham sozinhos:

| consumidor | ficheiro:linha | acompanha? |
|---|---|---|
| `aggregate_box_bytes` (bytes que o wrapper de variante possui) | `lower.tks:7533` | **sim** — lê `error_struct_layout().size` |
| `find_struct_layout` / `field_offset_of` / `field_type_of` | `lower.tks:8643/8656/8683` | **sim** — tabela |
| `lower_field_access` (leitura escalar de campo) | `lower.tks:8748` | **sim** — genérico sobre a tabela |
| `lower_fat_field` (leitura de campo gordo) | `lower.tks:8007` | **sim** — genérico sobre a tabela |
| `register_struct_layouts` | `lower.tks:9919` | **sim** |

**Consequência medida:** as leituras de `e.line`/`e.col`/`e.file`/`e.expected`/`e.actual` passam a
funcionar **sem uma linha de código novo de leitura**. Só a tabela muda.

### 4.2 A dívida que o crescimento cria — **3 sítios**

`store_struct_fields` (`lower.tks:8706`) escreve **só os campos declarados**. Um `alloca` não é
zerado — o próprio doc-comment de `store_struct_field` (`lower.tks:8726-8728`) regista este exacto
género de defeito já apanhado uma vez ("read whatever garbage the fresh `alloca` happened to
hold"). Com o layout crescido, `error { message = "x" }` deixa **56 bytes de lixo** e
`const_type_located` passa a ler `e.line` do lixo.

| sítio que constrói um `error` à mão no backend nativo | ficheiro:linha |
|---|---|
| `lower_struct_init` (o literal do utilizador) | `lower.tks:8695` |
| `lower_last_index_of_error_arm` (degrau 15) | `lower.tks:2112` |
| o braço de erro do `str_from_utf8` (degrau 21) | `lower.tks:2283` |

Os três precisam da **mesma** função de zeragem. É **um** helper novo e **três** chamadas.

### 4.3 `err_loc` / `err_typed` — **2 funções novas + 1 partilhada + 2 linhas de intercepção**

Não podem entrar em `native_builtin_symbol` (não há símbolo). Entram como intercepção em
`lower_call` (`lower.tks:1802-1808`), o molde exacto que `lower_last_index_of_call` e
`lower_str_from_utf8_call` já usam.

### 4.4 Sítios que assumem 16 bytes — **zero**

Procurados e **não encontrados**:

- `[]error` em `src/`: **0 ocorrências**. Nenhum `elem_byte_stride` sobre `error`.
- `error` como campo de `struct`/`class`: **0 ocorrências**.
- Constante 16 codificada para o `error`: **0**. `fat_slot_bytes()` (16) é dos pares `{ptr,len}`,
  não do `error`.
- `ltype_of(Error) == LType::Ptr` (`lower.tks:336`, braço `_`) e
  `is_register_value_type(Error) == false` (`lower.tks:7636`, braço `_`) — **ambos independentes do
  tamanho**. O `error` é, e continua a ser, representado pelo **endereço** do seu slot.

### 4.5 Total

| categoria | contagem |
|---|---|
| funções de produto a crescer | **1** (`error_struct_layout`) |
| funções de produto novas | **4** (zeragem, cópia, `err_loc`, `err_typed`) |
| sítios de chamada a tocar | **5** (3 construções + 2 intercepções em `lower_call`) |
| ficheiros de produto | **1** (`src/lir/lower.tks`) |
| ficheiros C | **0** |
| ficheiros de checker/parser/superfície | **0** |
| sítios de corpus a reescrever | **0** |

---

## 5. O risco pedido explicitamente: o que parte se o `error` mudar de tamanho

**Resposta medida: nada parte. Foi provado antes de se escrever uma linha.**

A sonda `probe_errlayout` (projecto descartável, fora da árvore) declara um `ErrShape` que é o
**gémeo byte a byte** do `tk_error` crescido — mesma ordem, mesmas classes, mesmas larguras
(`str`, `str`, `u32`, `u32`, `str`, `str` → 72 bytes, offsets 0/16/32/36/40/56) — e exercita:

1. os valores por omissão a zero e a leitura dos seis campos;
2. a **leitura de 4 bytes ao offset 32 e 36 dentro de um agregado de 72 bytes**
   (exactamente o caso `error.line`/`error.col`), comparada contra `0` e contra `7`;
3. a **passagem por valor** através de uma fronteira de chamada (o caso `const_type_located(e:
   error): error`) e a **semântica de cópia** que `tk_error_loc` define (a origem não é mutada);
4. o **embrulho de variante de 24 bytes** dos degraus 15 e 21 (`str | ErrShape`), construído e
   destruído por `match`.

| rota | saída |
|---|---|
| `TEKO_BACKEND=c` | **42** |
| `TEKO_BACKEND=native` | **42** |

**Porque é que a interacção com o wrapper de 24 bytes é imune.** `store_scalar_variant_payload`
(`lower.tks:6141`) não guarda o `error` **dentro** do wrapper: chama `variant_payload_box_bytes`
→ `aggregate_box_bytes` → `error_struct_layout().size`, e `box_aggregate_value` copia esses bytes
para armazenamento que o wrapper possui (`tk_slice_elem_box`), guardando **o endereço** ao
`variant_payload_offset()` (8). O wrapper guarda **um ponteiro**, seja o payload de 16 ou de 72
bytes. O `24` é o tamanho do **wrapper**, não do payload — e não muda.

**O único risco real é a zeragem** (§4.2), e é um defeito de omissão, não de tamanho: silencioso,
não um stop. É por isso que o crumb 1 tem de trazer a zeragem no mesmo commit que o crescimento do
layout, e é por isso que o fixture do crumb 1 afirma `line == 0` por VALOR e não só que compilou.

---

## 6. A fábrica `error::new` — dimensionamento

### 6.1 Sintaxe: **de graça**

`error` é um identificador comum, não um token de palavra-chave. Medido por sonda: `error::new("x")`
e `teko::error::new("x")` **ambos parseiam hoje**, e falham só na resolução —
`unknown function: new`. Portanto:

- **zero** mudanças no lexer;
- **zero** mudanças no parser;
- **zero** mudanças na gramática ou no `tooling/` (realce de sintaxe);
- `teko::error::*` **já é um namespace de builtins registado** (`err_loc`, `err_typed` vivem lá).
  As três fábricas são irmãs de builtins que já existem.

### 6.2 Custo por rota

| peça | ficheiro | tamanho |
|---|---|---|
| 3 assinaturas de builtin | `src/checker/scope.tks` (junto de `err_loc`, l. 655) | ~45 linhas com Javadoc |
| 3 braços de emissão C | `src/codegen/codegen.tks` (junto de `err_loc`, l. 3768) | ~45 linhas |
| `tk_error_at`, `tk_error_join` | `src/runtime/teko_rt.h` (C mantida — excepção da lei) | ~15 linhas |
| 3 baixadas nativas | `src/lir/lower.tks` | ~140 linhas com Javadoc |
| fixtures | `examples/regressions/own_native/` | ~50 linhas |

**Cerca de 300 linhas, 5 ficheiros, 1 dos quais C.** Isto é **uma lane curta ou um degrau largo** —
comparável ao degrau 21 (419 linhas, 7 ficheiros).

### 6.3 A conversão dos 928 sítios: **opcional, e sujeita à lei da semente**

Com o literal a ficar (decisão do dono), a conversão é arrumação e não pré-requisito. Mas se
alguma vez se fizer, há uma trava dura: **não há `bootstrap/DEGRAU` declarado neste tronco**, logo
a semente é o binário `teko` **publicado**. O corpus não pode USAR `error::new` antes de uma
release que já a conheça. **Um ciclo de release inteiro de atraso.** Isto é mais uma razão para a
conversão ser opcional — e para nunca ser condição de nada.

### 6.4 A dependência que decide a ordem

`error::new_pos(msg, line, pos, file)` escreve `line`, `col` e `file`. **Esses campos não existem
no layout nativo.** A fábrica é, no backend nativo, **inimplementável antes do degrau 22**. E o
helper de zeragem do crumb 1 e o helper de cópia do crumb 2 são **exactamente** as duas primitivas
que `error::new` e `error::new_pos` vão consumir.

> **Ordem ratificada: degrau 22 primeiro, fábrica a seguir.** O degrau 22 não espera pela fábrica;
> a fábrica é que herda o alicerce do degrau 22 e sai ~40% mais barata por isso.

### 6.5 `join` — a decisão do dono, e o que a medição diz sobre ela

O dono fechou em **concatenação achatada**. A medição **confirma-o e reforça-o**:

| pergunta | medição |
|---|---|
| sítios em `src/` que precisam de desembrulhar um erro composto | **0** |
| sítios em `src/` que fazem junção de PARES de erros à mão | **0** |
| o produto já resolve "muitos erros"? | **sim** — `join_checker_diags([]str): str` e `diags_error([]str): error` (`src/checker/diagnostics.tks:88` e `:110`) juntam N diagnósticos por `\n` numa única `message`. **A leitura (a) já é o idioma da casa, já embarcado.** |
| sítios que compõem a mensagem de OUTRO erro | **10** directos + **79** via envolventes (`err_at`/`one_diag`/`fail`) |

**Achado adjacente, REPORTADO e não transformado em issue:** esses 89 sítios não são junção de
pares — são **prefixação de contexto** (`"exported function \`f\`: " ~ e.message`). A forma que o
corpus realmente pede 89 vezes é `error::wrap(inner, context)` (o `fmt.Errorf("%w: …")` do Go),
não `join(left, right)`. `join` como especificado terá **zero chamadores no compilador** no dia em
que aterrar. Isso não a torna errada — torna-a API pública sem consumidor interno, o que é uma
decisão legítima do dono e fica só registada.

A porta para a cadeia desembrulhável fica aberta: como nada fora do checker constrói os cinco
campos não-`message`, acrescentar uma lista depois não toca em código de utilizador.

### 6.6 A referência espelhada: **Go**, e onde o paralelo se parte

| Teko | Go | honesto? |
|---|---|---|
| `error::new(msg)` | `errors.New(msg)` | **sim, exacto** |
| `error::join(a, b)` achatado | `errors.Join(a, b)` — mas só a metade `Error()` dele | **parcial** |
| `error::new_pos(msg, line, pos, file)` | **não existe** em Go | **parte aqui** |

**Onde parte, e porquê importa.** Em Go, `error` é uma **interface** (`interface { Error() string }`).
Qualquer tipo pode implementá-la, e é por isso que `errors.Join` pode guardar um `[]error` e expor
`Unwrap() []error` sem o `error` "crescer" — não há layout nenhum para crescer. Em Teko, `error` é
um **struct** com um layout fixo que os dois backends têm de concordar. Consequências:

1. **`errors.Join` não é replicável em Teko sem um campo de lista.** A escolha do achatamento não é
   uma preferência de estilo — é a única forma que a representação struct permite hoje. Dizer
   "espelhamos o Go" sobre o `join` só é honesto se se disser também que espelhamos o `Error()`
   dele e não o `Unwrap()`.
2. **`new_pos` não tem espelho em Go.** A convenção de Go para um erro posicionado é um **struct
   concreto** definido pelo autor que implementa `error` — `go/scanner.Error{Pos, Msg}`,
   `os.PathError{Op, Path, Err}` — construído por literal. Ou seja: o que o Teko tem **hoje**
   (`error { message = … }` com campos injectados) é *mais* parecido com o Go do que a fábrica é.
   `new_pos` é o `scanner.Error` do Go colapsado num construtor porque o Teko tem **um** tipo de
   erro em vez de N.
3. **O ganho real da fábrica não é o espelho do Go — é o encapsulamento.** Cinco dos seis campos
   nunca foram construíveis por literal (`typer.tks:2513`), logo o layout **já** estava escondido.
   A fábrica torna esse facto **legível na superfície** em vez de implícito numa rejeição do
   checker. Esse é o argumento que se sustenta sozinho, sem precisar do Go.

---

## 7. A sequência de crumbs

Cada crumb é independentemente fechável e traz o seu próprio fixture. `regressor.tkr` promove a
linha KNOWN-STOP correspondente no crumb em que ela passa a verde.

### Crumb 1 — o layout crescido **e** a zeragem, no MESMO commit

Não são separáveis: crescer o layout sem zerar é o defeito silencioso do §4.2.

```teko
/**
 * error_struct_layout — o `LStructLayout` sintético do `error` built-in: os SEIS campos que a
 * rota C já carrega em `tk_error` (`src/runtime/teko_rt.h`), na mesma ordem, com as mesmas
 * classes e larguras — `message`@0, `file`@16, `line`@32, `col`@36, `expected`@40, `actual`@56;
 * 72 bytes, alinhamento 8.
 *
 * PORQUE SEIS E NÃO UM (0.3.1.0 degrau 22). O checker permite construir exactamente UM campo
 * (`type_struct_lit`, `typer.tks:2513`) mas permite LER seis (`type_field_access`,
 * `typer.tks:2110-2124`), e o compilador exerce essa assimetria em sete linhas de três ficheiros
 * (`collect.tks:56`, `typer.tks:3253/6130/6131/6134`, `diagnostics.tks:38/39`). Um layout de um
 * campo fazia a leitura parar (`unknown field \`line\` on struct \`error\``) em vez de divergir
 * em silêncio — a paragem honesta que este degrau fecha.
 *
 * OS CINCO CAMPOS NÃO-CONSTRUÍVEIS TÊM DE FICAR A ZERO. `store_struct_fields` escreve só os
 * campos declarados e um `alloca` não é zerado, por isso todo o construtor de um `error` passa
 * por `zero_error_diagnostics` — a rota C garante o mesmo com `tk_error_make`.
 *
 * @return LStructLayout  o layout registado do `error`
 * @since 0.3.1 (degrau N2 — o campo `message`); 0.3.1.0 degrau 22 (os cinco de diagnóstico)
 */
fn error_struct_layout(): LStructLayout {
    let names = error_field_names()
    let types = error_field_types()
    layout_of_sized(error_struct_name(), names, types, error_field_sizes(types), error_field_aligns(types))
}
```

```teko
/**
 * zero_error_diagnostics — escreve zero em TODOS os campos do `error` que o construtor não
 * escreveu, começando no campo a seguir a `message`.
 *
 * PORQUE EXISTE. `error { message = … }` é a única forma construtível (`typer.tks:2513`), por
 * isso `store_struct_fields` escreve 16 dos 72 bytes do slot e deixa 56 por escrever. O
 * `alloca` não é zerado — `store_struct_field` já regista este mesmo género de defeito, apanhado
 * uma vez num campo gordo cuja metade de comprimento nunca era escrita. Aqui o sintoma seria
 * `const_type_located` a ler `e.line` do lixo da moldura e a decidir que um erro sem posição já
 * tinha uma. A rota C garante o mesmo através de `tk_error_make`, que zera os cinco.
 *
 * Um campo GORDO precisa de duas palavras a zero (ponteiro e comprimento); um escalar de uma.
 *
 * @param LowerCtx ctx  o contexto de baixamento
 * @param u32 base  o VReg do endereço-base do slot do `error`
 * @param LStructLayout layout  o layout registado do `error`
 * @param u64 from_index  o índice do primeiro campo a zerar (1 — logo a seguir a `message`)
 * @param u32 line  a linha de origem a atribuir às instruções emitidas
 * @param u32 col  a coluna de origem a atribuir às instruções emitidas
 * @return LowerCtx  o contexto avançado
 */
fn zero_error_diagnostics(ctx: LowerCtx, base: u32, layout: LStructLayout, from_index: u64, line: u32, col: u32): LowerCtx { … }
```

Toca em: `lower_struct_init` (só no braço `error`), `lower_last_index_of_error_arm`,
o braço de erro do `str_from_utf8`.

**Fixture** — `examples/regressions/own_native/src/corpus.tks`, `f_error_diagnostic_fields`,
código **57** em `main.tks`:

```teko
/**
 * f_error_diagnostic_fields — os seis campos legíveis do `error` built-in (0.3.1.0 degrau 22).
 *
 * Afirma por VALOR o que a rota C garante através de `tk_error_make`: um `error { message = … }`
 * tem `line`/`col` a 0 e `file`/`expected`/`actual` vazios. Antes deste degrau o backend nativo
 * modelava só `message` e uma leitura de `line` parava a compilação; um layout crescido sem
 * zeragem tê-la-ia feito ler lixo da moldura — por isso o fixture afirma o ZERO, não só que
 * compilou.
 *
 * O braço embrulhado em variante cobre a interacção com o wrapper uniforme de 24 bytes dos
 * degraus 15 e 21: o payload de 72 bytes é caixotado por endereço, o wrapper não cresce.
 *
 * @return i64  0 em sucesso, um código distinto por verificação falhada
 */
fn f_error_diagnostic_fields(): i64 {
    let e = error { message = "x" }
    if e.line != 0 { return 1 }
    if e.col != 0 { return 2 }
    if e.file != "" { return 3 }
    if e.expected != "" { return 4 }
    if e.actual != "" { return 5 }
    if e.message != "x" { return 6 }
    match mk_error_union() {
        str => return 7
        error as w => {
            if w.line != 0 { return 8 }
            if w.message != "y" { return 9 }
        }
    }
    0
}

/**
 * mk_error_union — um `str | error` que toma sempre o membro de erro, para exercitar o payload
 * de `error` caixotado dentro do wrapper de variante de 24 bytes.
 *
 * @return str|error  sempre o membro `error`
 * @throws  sempre
 */
fn mk_error_union(): str | error { error { message = "y" } }
```

**Portão:** `f_error_diagnostic_fields` passa de vermelho a verde na rota nativa; a rota C mantém
o mesmo valor. Auto-hospedagem completa na rota C verde.

### Crumb 2 — `err_loc` nativo (+ o helper de cópia partilhado)

```teko
/**
 * copy_error_value — reserva um slot novo de `error` e copia lá os seis campos de `src`.
 *
 * PORQUE UMA CÓPIA E NÃO UMA MUTAÇÃO. `tk_error_loc`/`tk_error_types` (`teko_rt.h`) recebem o
 * `error` POR VALOR e devolvem uma cópia modificada: `let b = err_loc(a, 7, 3)` deixa `a.line`
 * a 0 na rota C. Neste backend um `error` é o ENDEREÇO do seu slot
 * (§ NATIVE-AGG-SLICE-BY-ADDRESS), por isso adornar no sítio mutaria o `a` do chamador e as duas
 * rotas divergiriam em silêncio, sem paragem.
 *
 * @param LowerCtx ctx  o contexto de baixamento
 * @param u32 src  o VReg do endereço-base do `error` de origem
 * @param LStructLayout layout  o layout registado do `error`
 * @param checker::TExpr e  a expressão da chamada (só a posição de origem)
 * @return Lowered  o endereço-base do slot novo
 */
fn copy_error_value(ctx: LowerCtx, src: u32, layout: LStructLayout, e: checker::TExpr): Lowered { … }

/**
 * lower_err_loc_call — `teko::error::err_loc(e, line, col)` (E2): uma cópia de `e` com `line` e
 * `col` sobrescritos.
 *
 * PORQUE BAIXADO E NÃO RESOLVIDO POR SÍMBOLO. `tk_error_loc` é `static inline` no `teko_rt.h` e
 * não tem símbolo ligável em `teko_rt.c`, logo `native_builtin_symbol` não tem entrada nenhuma
 * para resolver; e um gémeo de runtime devolveria 72 bytes por valor, que o `LCall` deste
 * backend — que lê UM registo de resultado — não capta. Intercepta-se em `lower_call`, o mesmo
 * molde de `lower_last_index_of_call` (degrau 15) e `lower_str_from_utf8_call` (degrau 21).
 *
 * @param LowerCtx ctx  o contexto de baixamento
 * @param checker::TExpr e  a expressão da chamada (tipo `error` + posição de origem)
 * @param checker::TCall c  o nó de chamada (três argumentos: o erro, a linha, a coluna)
 * @return Lowered | error  o endereço-base do `error` adornado
 * @throws  propagado do baixamento dos argumentos, ou quando a aridade não é 3 (quebra de
 *          invariante — o checker já a validou em `scope.tks`)
 */
fn lower_err_loc_call(ctx: LowerCtx, e: checker::TExpr, c: checker::TCall): Lowered | error { … }
```

Intercepção, ao lado das duas que já lá estão (`lower.tks:1806-1807`):

```teko
    if c.call_ns.len == 0 && is_err_loc_call(c.callee) { return lower_err_loc_call(ctx, e, c) }
```

**Fixture** `f_err_loc`, código **58** — afirma `line == 7`, `col == 3`, `message` preservada, **e
que a origem não foi mutada** (`orig.line == 0` depois do adorno).

### Crumb 3 — `err_typed` nativo

Gémeo exacto do crumb 2 sobre `expected`/`actual` (dois campos gordos em vez de dois escalares),
reutilizando `copy_error_value`.

**Fixture** `f_err_typed`, código **59** — `expected == "i64"`, `actual == "str"`, `line`
preservada através do adorno, origem não mutada. Mais uma verificação encadeada que espelha
`surface_at` (`err_typed(err_loc(e, l, c), exp, act)`), porque é o único sítio do compilador onde os
dois se compõem.

### Crumb 4 — o ritual

Auto-hospedagem nativa completa (`TEKO_BACKEND=native .gen1b/teko . -o …`), quatro pernas Linux, o
gate inteiro. **A paragem seguinte é o degrau 23** e regista-se em `docs/memory/`: nome da função,
mensagem exacta, e a confirmação de que o stop do degrau 22 desapareceu.

### Pontos de ritual

| depois de | o portão completo tem de passar |
|---|---|
| crumb 1 | sim — o layout muda a representação de TODO o `error`; é o crumb de maior raio |
| crumb 2 | não — fixture + auto-hospedagem C chegam |
| crumb 3 | sim — fecha a superfície E2 nas duas rotas |
| crumb 4 | sim — é o ritual, por definição |

---

## 8. Riscos e tensões de lei

| # | risco | avaliação medida | resolução |
|---|---|---|---|
| R1 | o `error` cresce 16 → 72 e algo assume 16 | **0 sítios**: sem `[]error`, sem `error` como campo, sem constante codificada; `ltype_of`/`is_register_value_type` são independentes do tamanho | nenhuma acção |
| R2 | interacção com o wrapper de variante de 24 bytes | **imune**: o payload é caixotado por endereço (`box_aggregate_value`); o wrapper guarda um ponteiro. **Provado por sonda**: `str \| ErrShape` de 72 bytes dá 42 nas duas rotas hoje | nenhuma acção |
| R3 | leitura de 4 bytes ao offset 32 num agregado de 72 | **provado por sonda** hoje (`u32` ao offset 32, comparada contra 0 e contra 7) | fixture do crumb 1 tranca-o |
| R4 | **lixo nos cinco campos não construídos** | **real, silencioso, não é uma paragem** | crumb 1 traz a zeragem no MESMO commit; o fixture afirma o zero por VALOR |
| R5 | `err_loc` a mutar em vez de copiar | **real**: divergiria da rota C sem paragem. Nenhum sítio do corpus depende disso hoje, mas a rota C define a semântica | `copy_error_value`; o fixture afirma que a origem não foi mutada |
| R6 | lei Teko-only vs. gémeo de runtime em C | **sem tensão**: `tk_error_loc` não tem símbolo ligável, logo o caminho em Teko é o único que existe. **Zero C no degrau 22** | nenhuma |
| R7 | lei da semente | **sem tensão**: o degrau 22 não usa nenhuma funcionalidade de linguagem nova. Só a fábrica (§6.3) a tocaria, e ficou opcional | nenhuma |

**Nenhuma tensão de lei genuína. Nada a HALTAR.**

---

## 9. Achados adjacentes — REPORTADOS, não transformados em issues

1. **`err_typed` (22 chamadas) esteve fora de todas as medições anteriores** desta paragem, apesar
   de ter mais do dobro dos sítios de `err_loc` e de ser uma paragem nativa distinta e confirmada.
2. **A forma que o corpus pede 89 vezes é `wrap(inner, context)`, não `join(left, right)`.**
   `join` aterra sem chamador interno.
3. **`error.file` tem zero leituras e zero escritas** em toda a árvore. É carregado pela rota C e
   aceite pelo checker, mas o compilador nunca o preenche — nem sequer `surface_at`, que recebe um
   `file: str` e o usa só para a string renderizada. `error::new_pos(…, file)` seria o primeiro
   escritor deste campo em todo o produto.
4. **A afirmação de que esta paragem já existia na base `3caecd5` é falsa**, e o mecanismo explica
   porquê: o compilador reporta **a primeira** função que falha na ordem de travessia. Fechado o
   `bytes_of_str` (degrau 21), a travessia passa a chegar mais longe e descobre a seguinte. As duas
   paragens nunca poderiam ter sido observadas ao mesmo tempo — um `git stash` mostra a **anterior**,
   não a **ausência** desta. O CI de `3caecd5` reportou `bytes_of_str`; o de `8c57bcf`
   (corrida 30458446943) reporta esta. É uma escada, e o degrau 21 foi o que a fez subir.

---

## 10. `error` como interface — avaliação da lane futura

> Proposta do dono: *"nada impede de que `error` pudesse ser interface e não estrutura"* — o
> espelho completo do Go. **Não muda a resposta ao degrau 22** (§ resposta de uma linha): o degrau
> 22 fecha a divergência entre as duas rotas na representação que existe HOJE, e essa divergência
> tem de fechar seja qual for a representação de amanhã.

### 10.1 As quatro perguntas, respondidas por medição

**P1 — Uma interface pode ser membro de união (`T | error`) hoje? NÃO.**

Sonda `probe_iface_union` (interface declarada, classe implementadora, `fn mk(): str | ErrIface`).
As duas rotas rejeitam **na mesma linha**:

```
src/corpus.tks:27:4: an interface cannot be a variant member yet
  — bind the interface value on its own (no `I | error` unions)
```

A regra está em `src/checker/resolve.tks:1683` e a mensagem **nomeia literalmente este caso**
(`no \`I | error\` unions`). Em `src/` há **1454 ocorrências de `| error`**. Tornar o `error`
interface exige levantar esta restrição **primeiro**, para as 1454 — e isso não é uma lane sobre o
`error`, é uma lane sobre a **representação de variantes**.

**A tese de sequenciamento cai aqui.** Não cai por causa do dispatch (a medição do coordenador
está certa: `lower_iface_call`/`lower_iface_fatptr`/`lower_iface_slot` estão completos e o valor de
interface é `{data@0, vtable@8}` = 16 bytes, que **caberia** no slot gordo do wrapper de 24 bytes
sem o fazer crescer). Cai por causa do **checker**, não do backend. A fábrica não a torna barata,
porque a fábrica não toca nesta regra.

**P2 — Que método teria, e o que acontece aos cinco campos?**

`src/checker/typer.tks:2164`: *"an interface value exposes no fields — only its contract methods."*
Portanto, com o `error` como interface, **toda a leitura de campo passa a ilegal** e vira chamada de
método:

| leitura | sítios hoje | com interface |
|---|---|---|
| `.message` | **112** | `.message()` — 112 reescritas |
| `.line` | 7 | `.line()` |
| `.col` | 2 | `.col()` |
| `.expected` / `.actual` | 2 | métodos |
| **total** | **123** | **123 reescritas obrigatórias** |

E o contrato teria de declarar **seis** métodos (não um `Error() string` como o Go), porque o
compilador lê os seis. Ou então os cinco de diagnóstico ficam no tipo concreto — e aí é preciso o
downcast da P4, que não existe.

**P3 — `err_loc` sobrevive?** `err_loc(e, line, col)` devolve **uma cópia com dois campos
sobrescritos**. Um valor de interface é imutável do lado de fora (só métodos), portanto `err_loc`
deixa de poder existir na sua forma actual: teria de virar `with_pos(line, col): ErrIface` **no
contrato**, obrigando **todo** o implementador a saber reconstruir-se. Os 10 sítios de `err_loc` e
os 22 de `err_typed` — **32 no total** — são redesenho, não migração.

**P4 — Há equivalente a `errors.As`? NÃO. Zero.** Procurado em toda a árvore: nenhum downcast,
nenhum teste de tipo concreto por trás de uma interface, nenhuma forma de `match` sobre a classe
implementadora. Sem isso, **erros tipados não se conseguem interrogar** — só construir. A interface
acrescentaria indirecção sem nada em troca, que é exactamente a suspeita do coordenador,
**confirmada por medição**.

### 10.2 Custo total da lane-interface, e ordem

| pré-requisito | custo |
|---|---|
| levantar `resolve.tks:1683` (interface como membro de variante) | lane própria, sobre variantes, a montante de tudo |
| `errors.As` / downcast (novo na linguagem) | lane própria, superfície nova |
| 123 leituras de campo → chamadas de método | mecânico, mas obrigatório e a quebrar superfície |
| 32 sítios de `err_loc`/`err_typed` | redesenho |
| ciclo de semente | **um por cada** peça de superfície nova |

**Veredicto: lane grande, de várias lanes, e não desbloqueada pela fábrica.** A fábrica torna a
troca de representação invisível **para quem CONSTRÓI** um erro — e isso é verdade e é valioso. Mas
**91% do custo está do lado de quem LÊ** (123 leituras) e do checker (a regra da variante), e a
fábrica não toca em nenhum dos dois.

### 10.3 A ordem que a medição sustenta

```
degrau 22  →  fábrica (error::new / new_pos / join)  →  [variantes: interface como membro]
                                                      →  [linguagem: errors.As]
                                                      →  error como interface
```

O degrau 22 é o primeiro em qualquer ordenação: é ele que põe `line`/`col`/`file` no layout, sem os
quais a `new_pos` do dono não é implementável, e é ele que faz as duas rotas concordarem — invariante
que qualquer representação futura vai continuar a precisar.

---

## 11. O que fica bloqueado

**Nada, no degrau 22.** Não depende da fábrica, não depende de nenhuma API em aberto, não precisa
de C novo, não precisa de superfície nova e não espera por ciclo de semente. Está pronto a executar
tal como está.

**Bloqueado, e nomeado:** a lane-interface espera por duas lanes a montante que ainda não existem
(`resolve.tks:1683` e um `errors.As`). A fábrica não está bloqueada por nada — só é ~40% mais
barata depois do degrau 22, porque herda dele o layout e os dois helpers.
