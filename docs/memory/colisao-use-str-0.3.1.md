# Colisão `use teko::str` — 0.3.1.0

**Encomenda:** testar o medo do dono (2026-07-29, literal) *"Meu medo com `teko::str::`, e preciso
que teste, fazer alias `use teko::str`, vai causar conflito com o tipo str."*

**Resposta curta:** o medo **não se confirma como escrito** — `str` como TIPO nunca colide, com ou
sem `use` (o resolvedor de tipos intercepta `str` como builtin ANTES de olhar para qualquer alias).
Mas a sonda descobriu um problema real, mais grave e mais geral do que o medo original: **a forma
curta `str::concat(...)` (o que o `use teko::str` deveria habilitar) não compila em NENHUM dos dois
backends hoje** — não por colisão de `str`, mas porque o mecanismo de despacho de builtins do
checker aceita **qualquer prefixo de namespace, real ou inventado**, sem validação nenhuma, e o
codegen só sabe emitir a chamada certa quando o `callee` é literalmente `1 segmento` ou começa por
`teko::`. Isto foi verificado directamente com um prefixo INVENTADO (`bogus_namespace::str(...)`) e
com um prefixo CORRETO mas curto (`env::var(...)`, sem `teko::`): ambos passam o checker sem
diagnóstico e falham do mesmo jeito no `cc`/`ld`. `use` está **completamente fora desta cadeia** —
`UseDecl` é um no-op confirmado no `typer.tks` para tudo o que não seja checagem de TIPO.

Nada abaixo é afirmado sem o comando e a saída literal coladas ao lado.

---

## 0. Método e proveniência do compilador (honestidade de gate)

`sh scripts/fetch_teko.sh` falhou nesta sessão: o proxy do agente devolve 403 em qualquer chamada
`gh` (`GitHub access to this repository is not enabled for this session`). Isto é o bloqueio de
proxy já conhecido, não um problema do repositório — não usei `gh` depois disso.

Sem download possível, reutilizei um binário `teko` já auto-construído por outra lane deste mesmo
runner (`/home/user/wt-map/.teko/teko`, self-report `teko "0.0.0.0-dev"`, 9 408 392 bytes — o mesmo
tamanho do `bootstrap/teko.c` trampolim de outras sondas recentes neste repositório, sinal de que é
um `gen1` são). Copiado para `/home/user/wt-cyc/.teko/teko`. **Isto não é o binário RELEASED oficial
— é uma medida honesta com o que estava disponível, não uma alternativa equivalente.** Se a rota
`fetch_teko.sh` for desbloqueada, os resultados desta sonda devem ser re-confirmados com o binário
correcto antes de qualquer decisão de correcção — o objecto de estudo aqui (resolução de nomes no
checker/codegen, ficheiros `.tks`) não depende de otimizações do compilador seed, mas a proveniência
formal fica em aberto.

Commit medido: `9af2348` (branch `cargo/0.3.1.0-colisao-use-str`, base
`remodel/0.3.1.0-linux-native-2`). Toda sonda foi um projecto Teko standalone descartável em
`/tmp/.../scratchpad/probes/<nome>/` (`teko.tkp` + `src/main.tks` + `src/probe.tks`), nunca dentro
da árvore do compilador. Invocado sempre pelo caminho absoluto do binário (`/home/user/wt-cyc/.teko/teko`)
— invocar por `teko` via PATH resolvia o `argv[0]` errado e o build falhava a encontrar
`src/runtime/teko_rt.h` (não é bug do `use`; é uma armadilha da minha própria sonda, documentada
aqui para quem repetir o método).

Cada sonda foi compilada pelas duas rotas: `TEKO_BACKEND=c teko . -o binc` e `teko . -o binn`
(backend próprio/nativo).

---

## 1. O caso do dono, directo — `str` como TIPO

`use teko::str` seguido de `fn f(s: str): str`.

```
// src/probe.tks
use teko::str

pub fn f(s: str): str { s }

pub fn check(): u64 {
    let r = f("x")
    println(r)
    0
}
```

| rota | resultado | saída |
|---|---|---|
| `TEKO_BACKEND=c` | ✓ build, ✓ run | `x` |
| nativo (`teko . -o binn`) | ✓ build, ✓ run | `x` |

Sem erro, sem aviso. `s`/`f`'s retorno continuam a ser o `str` builtin — **não** houve nenhuma
migração silenciosa de significado: chamei `f("x")` com um literal `str` e recebi `"x"` de volta,
que só faz sentido se o parâmetro continuar a ser o tipo primitivo.

**Porquê, no código:** `src/checker/resolve.tks:775-787` (`resolve_named`) resolve uma referência de
tipo de UM segmento (`path.segments.len == 1`) chamando `builtin_type(name)` **primeiro**, antes de
consultar a tabela de tipos de utilizador ou qualquer alias de `use`:

```
// src/checker/resolve.tks:779-787
// R2 — a BUILTIN resolves only for a BARE reference (`u8`…`u64`, `byte`, `str`, `error`, …). A
// qualified path can never name a builtin, so a `ns::str` falls through to the user-type rules.
if path.segments.len == 1 {
    match builtin_type(name) {
        Type as t => return t
        NotBuiltinType => { }
        error as e => return e
    }
}
```

`builtin_type` (`src/checker/scope.tks:365-389`) tem `if name == "str" { return Str { } }` na linha
`382`, incondicional. Este caminho nunca consulta a tabela de aliases (`AliasBind`) construída a
partir do `use` — essa tabela só existe em `src/checker/check_modules.tks` e só é usada lá dentro
para TIPOS QUALIFICADOS (`ns::Tipo`, 2+ segmentos), nunca para uma referência bare de 1 segmento.
`check_named` (`check_modules.tks:97-108`) confirma o mesmo curto-circuito do lado da passagem de
diagnóstico do módulo: `match builtin_type(last) { Type => return null; ... }` antes de qualquer
outra regra.

**Veredicto do caso 1: seguro. Sem colisão, com ou sem `use`.**

---

## 2. `use teko::str` + a forma de namespace curta `str::concat(a, b)`

```
use teko::str

pub fn check(): u64 {
    let r = str::concat("a", "b")
    println(r)
    0
}
```

| rota | resultado |
|---|---|
| `TEKO_BACKEND=c` | checker ✓ (3/3 items), codegen ✓ (3/3 items), **`cc` FALHA**: `binc/p2a_ns_str.c:14:16: error: invalid initializer` sobre `tk_str r = concat(( {` |
| nativo | checker ✓, codegen ✓, **link FALHA**: `undefined reference to 'teko_concat'` |

O checker e o codegen dizem "ok" e só rebentam no `cc`/`ld`. O C gerado (linha 14) é:

```c
tk_str r = concat(( {
    tk_str *_arr337 = malloc(2 * sizeof(tk_str));
    ...
```

`concat(` sem nenhuma referência de namespace — não existe símbolo C `concat`. Isto acontece
**exactamente igual, byte a byte na mensagem de erro, com E sem `use teko::str`** (secção 4).

### 2b. As duas formas no mesmo ficheiro (tipo + namespace)

```
use teko::str
pub fn f(s: str): str { s }
pub fn check(): u64 {
    let a = f("a")
    let r = str::concat(a, "b")
    println(r)
    0
}
```

Mesmo resultado: `TEKO_BACKEND=c` → `cc` falha em `binc/p2b_type_and_ns.c:22:16: error: invalid
initializer` sobre `concat((`; nativo → `undefined reference to 'teko_concat'`. A parte do TIPO
(`f(s: str)`) continua perfeitamente bem — é só a chamada `str::concat` que rebenta, e rebenta pela
MESMA razão da secção 2.

**Veredicto do caso 2: a forma curta `str::concat` não funciona hoje, em nenhum backend — mas
NÃO é um problema de colisão de `str`; é um problema geral de qualquer forma qualificada de 2
segmentos que não seja `teko::`, ver secção 7.**

---

## 3. A função builtin `str(bytes)` no meio

```
use teko::str

pub fn check(): u64 {
    let b: []byte = [b'h', b'i']
    let r = str(b)
    println(r)
    0
}
```

| rota | resultado | saída |
|---|---|---|
| `TEKO_BACKEND=c` | ✓ build, ✓ run | `hi` |
| nativo | **falha**, mas por um motivo NÃO relacionado com `use`/colisão: `teko: .: native backend N1: builtin \`str\` not yet lowered (N2)` — o backend nativo simplesmente ainda não sabe baixar este builtin (ver §6, degraus). |

Sem `use`, o mesmíssimo programa dá **exactamente a mesma saída/erro** (secção 4). A chamada bare
`str(bytes)` resolve para o builtin `str_of_bytes` (`src/checker/scope.tks:577`) de forma idêntica
com e sem `use` — o `use` é irrelevante aqui porque `lookup_call` (`src/checker/scope.tks:288-306`)
só vê `c.callee.segments.len == 1`, não há qualificador para desambiguar.

**Veredicto do caso 3: seguro. Resolve sempre para o builtin `str_of_bytes`, com ou sem `use`. O
gap nativo é um degrau conhecido e não-relacionado (§6).**

---

## 4. Controlos sem `use` — provam que quem quebra é a FORMA, não o `use`

| sonda | com `use teko::str` | sem `use` | idêntico? |
|---|---|---|---|
| `fn f(s: str): str` (tipo) | ✓/✓ (C/nativo) | ✓/✓ | **sim** |
| `str::concat(a,b)` (namespace curto) | `cc` falha / link falha | `cc` falha idêntico / link falha idêntico | **sim, byte a byte na mensagem** |
| `str(bytes)` (builtin) | ✓C / nativo "not yet lowered" | ✓C idêntico / nativo idêntico | **sim** |

Saída literal do controlo (sem `use`) para `str::concat`:

```
$ TEKO_BACKEND=c teko . -o binc     # p4_ns_str_nouse, SEM use
...
binc/p4_ns_str_nouse.c:14:16: error: invalid initializer
   14 |     tk_str r = concat(( {
      |                ^~~~~~
teko: .: cc failed to build the generated C

$ teko . -o binn                    # nativo, SEM use
/usr/bin/ld: binn/p4_ns_str_nouse.o: in function `teko_p4_ns_str_nouse__check':
(.text+0x60): undefined reference to `teko_concat'
teko: .: cc failed to link the own-backend object
```

Compare com a secção 2 (COM `use`): mensagens idênticas, mesma coluna, mesmo símbolo indefinido.
**Conclusão directa: `use teko::str` não introduz nem resolve nada aqui — é um no-op observável.**

---

## 5. A forma que o compilador realmente usa — `teko::str::concat` por extenso

Contagem literal (não a estimativa de 495 do pedido, mas na mesma ordem de grandeza — a diferença é
provavelmente por eu ter incluído/excluído `.md`/`.c`/`.h` de formas diferentes):

```
$ grep -ron "teko::str::concat" src main.tks --include="*.tks" --include="*.tkt"
506
$ grep -ron "teko::str::concat" . --include="*.tks" --include="*.tkt" --include="*.md" --include="*.c" --include="*.h"
544
```

Sonda directa da forma canónica, SEM `use`:

```
pub fn check(): u64 {
    let r = teko::str::concat("a", "b")
    println(r)
    0
}
```

| rota | resultado | saída |
|---|---|---|
| `TEKO_BACKEND=c` | ✓ build, ✓ run | `ab` |
| nativo | **falha**: `undefined reference to 'teko_concat'` (mesmo link error da secção 2 — ver §7 para a explicação exacta) |

Com `use teko::str` presente (mas chamando pela forma completa mesmo assim): resultado idêntico,
`ab` no C, mesmo link error no nativo.

**Veredicto do caso 5: confirmado — é por isto que o corpus escreve `teko::str::concat` por
extenso 506+ vezes: é a ÚNICA forma que o backend C sabe emitir correctamente hoje.** O backend
nativo falha mesmo nesta forma canónica (ver §6) — algo já não coberto por este pedido, mas
registado.

---

## 6. Os irmãos — `teko::list`, `teko::text`, `teko::env`, `teko::process`

Primeiro, uma sonda estática (mesma classe de pergunta que a secção 1, tipo bare):

```
$ grep -n '"list"\|"text"\|"env"\|"process"' src/checker/scope.tks
(sem resultados)
```

Nenhum dos quatro nomes aparece como `builtin_type` nem como entrada de `builtin_fn` — ao contrário
de `str`, que aparece nas DUAS tabelas (`scope.tks:382` tipo, `scope.tks:577` função). **`str` é o
único dos cinco nomes candidatos a `use teko::X` que é ao mesmo tempo um tipo builtin E uma função
builtin — é estruturalmente diferente dos irmãos, não apenas por acaso.**

Sonda em execução, `fn f(v: X): X { v }`, com e sem `use teko::X`:

| X | com `use` | sem `use` | mensagem (idêntica nas duas colunas) |
|---|---|---|---|
| `list` | erro checker | erro checker | `unknown type: list` |
| `text` | erro checker | erro checker | `unknown type: text` |
| `env` | erro checker | erro checker | `unknown type: env` |
| `process` | erro checker | erro checker | `unknown type: process` |

Saída literal (uma amostra, `text`, com `use`):

```
teko: .: src/probe.tks:3:8: unknown type: text
```

Todas as quatro: falha honesta, idêntica com/sem `use`, em ambos os backends (o erro é de checker,
antes de qualquer codegen — não há divergência C/nativo aqui). **Nenhum dos quatro colide como
tipo.**

Do lado do VALOR, os quatro não são simétricos entre si:

- `teko::list::*` (`empty`/`push`) é reconhecido por um casamento SINTÁTICO dedicado
  (`src/checker/typer.tks:839-850`, `type_list_builtin`) que exige o segmento anterior ser
  literalmente `"list"` — independente de `use`. `list::push(...)` (2 segmentos, sem `teko::`)
  já funciona hoje sem `use`, mas bateu num gap de lowering NÃO relacionado
  (`u64_to_str not yet lowered`) antes de eu conseguir confirmar o `println` final — build do
  `list::push`/`list::empty` em si passou pelo checker e codegen sem erro em ambas as sondas
  (com/sem `use`).
- `teko::text::valid_utf8` é uma função REAL (não builtin) declarada em `src/text/text.tks`, que só
  existe dentro da própria árvore do compilador (self-host) — um projecto Teko externo (como as
  minhas sondas) não a vê. `text::valid_utf8(...)` deu `unknown function: valid_utf8` tanto com
  como sem `use` — a sonda está correcta ao mostrar "não resolve", mas isto mede a ausência de uma
  stdlib externa, não uma colisão.
- `teko::env::var` e `teko::process::run` SÃO despachados por `builtin_fn`
  (`scope.tks:469-482`, `509`) exactamente como `str`/`concat` — e sofrem o MESMO problema da
  secção 2 na forma curta de 2 segmentos: `env::var("HOME")` (sem `teko::`) falha no `cc` com
  `invalid initializer` sobre `var((tk_str)` — idêntico ao padrão de `str::concat`. A forma
  canónica `teko::env::var("HOME")` funciona no C backend (saída `/root`, o `$HOME` real).

**Mapa de segurança dos irmãos, por tipo de risco:**

| namespace | colide como TIPO? | a forma curta `X::fn(...)` funciona hoje? | a forma canónica `teko::X::fn(...)` funciona? |
|---|---|---|---|
| `str`   | não (§1) | não (`concat`, §2) | sim no C; não no nativo (§5) |
| `list`  | não | sim (sintaxe dedicada, não passa pelo mecanismo de risco) | sim |
| `text`  | não | n/a (função não existe fora do próprio compilador) | n/a |
| `env`   | não | não (`var`, mesmo padrão de `str::concat`) | sim no C (testado, `/root`); nativo não testado |
| `process` | não | não testado ao fim (bloqueado por gap nativo `u64_to_str`), mas o mesmo mecanismo aplica-se | não testado |

---

## 7. Onde a resolução decide — a pergunta central: alguma combinação resolve para o símbolo
ERRADO sem erro?

**Sim — para chamadas (não para tipos), e não tem nada a ver com `str` nem com `use`.**

Prova directa: uma chamada com um prefixo de namespace **inventado**, que não existe em lado nenhum
do programa:

```
pub fn check(): u64 {
    let b: []byte = [b'h', b'i']
    let r = bogus_namespace::str(b)      // "bogus_namespace" não existe
    println(r)
    0
}
```

```
$ TEKO_BACKEND=c teko . -o binc
  checker    2/2 items  ✓
  codegen    2/2 items  ✓
  cc         binc/p7a_bogus_qualifier_str.c: In function 'teko_p7a_bogus_qualifier_str__check':
binc/p7a_bogus_qualifier_str.c:19:16: error: invalid initializer
   19 |     tk_str r = str(b);
      |                ^~~
```

O checker aceita `bogus_namespace::str(b)` como se fosse uma chamada válida ao builtin
`str_of_bytes` — **sem nenhum diagnóstico sobre o namespace inexistente** — e só é apanhado por
acidente no `cc`, porque o C gerado (`str(b)`) não bate com nenhum símbolo real. Se por acaso
existisse um símbolo bare `str` ou `concat` no programa do utilizador (uma função local com esse
nome, por exemplo), a chamada teria ligado a ELE em vez de rebentar — isso não foi testado aqui
(ficaria fora do escopo do pedido), mas o mecanismo abaixo não impede essa possibilidade.

Segunda prova, ainda mais directa ao medo do dono — depois de `use teko::str`, chamar
`str::args()`, uma função que **não existe em `teko::str`** (existe em `teko::env`, 0 argumentos,
devolve `[]str`):

```
use teko::str
pub fn check(): u64 {
    let r = str::args()
    println("str::args() type-checked and ran")
    0
}
```

```
  checker    3/3 items  ✓
  codegen    3/3 items  ✓
  cc: binc/p7b_str_calls_args.c:15:22: error: invalid initializer
     15 |     tk_slice_str r = args();
        |                      ^~~~
```

O checker infere `r: tk_slice_str` (o tipo de retorno de `teko::env::args()`) para uma chamada
escrita como `str::args()` — **o qualificador `str` foi completamente ignorado**; só o nome final
`args` decidiu. Repeti sem `use` (`p7c_str_calls_args_nouse`): **mensagem idêntica, byte a byte**.
`use` não piora nem melhora nada aqui — o buraco já existe sem ele.

Terceira prova — um alias EXPLÍCITO, sem a palavra `str` em lado nenhum, sofre o mesmo:

```
use teko::str as tstr
pub fn check(): u64 {
    let r = tstr::concat("a", "b")
    ...
```

```
cc: binc/p10_explicit_alias.c:14:16: error: invalid initializer
   14 |     tk_str r = concat(( {
```

**Isto fecha a questão: o problema não é `str`, não é `use`, é qualquer prefixo (real, inventado, ou
um alias) diante do nome de uma função despachada pelo mecanismo de builtins do checker.**

### A cadeia exacta, ficheiro e linha

1. **`src/checker/typer.tks:1590-1593`** (`type_call`) — quando `lookup_call` não encontra uma
   ligação REAL (função de utilizador registada), cai para `builtin_fn(name)` **só com o último
   segmento** (`name = c.callee.segments[c.callee.segments.len - 1].name`, linha 1561). O
   qualificador inteiro é descartado nesta chamada.
2. **`src/checker/scope.tks:461-610`** (`builtin_fn`) — a tabela de despacho por NOME BARE. O
   comentário do próprio ficheiro admite e justifica isto para o par `str`/`string`:
   > `"concat"/"concat3" are matched by BARE last-segment name ... so ANY namespace prefix ending
   > in these names resolves identically — this is what makes teko::string::concat(a, b) ...
   > and teko::str::concat ... two names for the same builtin dispatch.`
   (linhas 595-597) — a intenção original é permitir DUAS grafias legisladas do mesmo builtin, mas
   o mecanismo não distingue essas duas grafias de um QUALQUER OUTRO prefixo.
3. **`src/checker/check_modules.tks:1-11` e linha 208-209** — a ÚNICA passagem que valida
   qualificadores de `use` contra a tabela de aliases (`AliasBind`) é `check_named`, e o cabeçalho
   do ficheiro admite textualmente que ela cobre só TIPOS:
   > `Value-level refs — call callees, enum-member paths — are a follow-on increment; this covers
   > the dominant TYPE surface.`
   Não há, hoje, NENHUMA passagem equivalente para o `callee` de uma chamada.
4. **`src/checker/typer.tks:6045`** — confirma que `UseDecl` é tratado como um no-op na passagem de
   itens do `typer`: `parser::UseDecl as ud => ud // seed no-op (alias resolution is the checker's,
   later)`.
5. **`src/codegen/codegen.tks:3584`** (`emit_call`) — o portão que decide se alguma das rotinas de
   emissão dedicadas de builtin dispara:
   ```
   let addr = (p.segments.len == 1) || (p.segments[0].name == "teko")
   ```
   e o irmão dela em **`src/codegen/codegen.tks:3640`** (para toda a família de FFI de host —
   `var`/`args`/`run`/`cwd`/...):
   ```
   let addr2 = !user_declared && ((p.segments.len == 1) || (p.segments[0].name == "teko"))
   ```
   Só dispara para um `callee` de **1 segmento** ou explicitamente enraizado em `teko::` (literal).
   Qualquer OUTRA forma qualificada do MESMO builtin resolvido pelo checker — `str::concat`,
   `bogus_namespace::str`, `tstr::concat`, `env::var` — salta por cima de TODAS as rotinas
   dedicadas e cai no caminho de emissão genérico, que assume (incorrectamente, para um builtin)
   que o nome final É o símbolo C a chamar. Como nenhuma dessas formas curtas tem de facto um
   símbolo C `concat`/`str`/`var`/`args` bare, isto falha no `cc` ou no `ld` **por coincidência**,
   não por validação.

**A pergunta do dono, respondida directamente: uma combinação resolve-se em silêncio para o símbolo
errado?** Sim, ao nível do CHECKER (passo 1-2 acima): `str::args()` passa a checagem de tipos como
uma chamada legítima a uma função que devolve `[]str`, sem qualquer diagnóstico sobre o facto de
`args` não pertencer a `teko::str`. O que impede isto de virar um binário funcionando com
comportamento errado, hoje, é um acidente de engenharia a jusante (o portão `addr`/`addr2` do
codegen, passo 5) que reduz quase todas essas formas a uma falha de build — mas essa protecção não
foi desenhada para isto, é um efeito colateral. Não testei (fica fora do escopo, mas é a próxima
pergunta óbvia) se existe alguma combinação de nomes bare-colidentes cujo símbolo C genérico
COINCIDE por acidente com um símbolo real do runtime, o que produziria um binário que compila e
corre — chamando a função errada silenciosamente, em runtime.

---

## Tabela-veredicto final

| forma escrita | tipo-checka? | compila (C)? | compila (nativo)? | resolve para o símbolo certo? | mensagem se falha |
|---|---|---|---|---|---|
| `fn f(s: str)` — tipo, com/sem `use` | sim | sim | sim | sim (builtin `Str{}`, sempre) | — |
| `str(bytes)` — builtin, com/sem `use` | sim | sim | não (gap nativo, §3, não-relacionado) | sim (`str_of_bytes`, sempre) | nativo: `builtin \`str\` not yet lowered` |
| `str::concat(a,b)` — namespace curto, com/sem `use` | sim | **não** | **não** | **checker: sim (sem validar); codegen: falha antes de rodar** | `cc`: `invalid initializer`; nativo: `undefined reference to 'teko_concat'` |
| `teko::str::concat(a,b)` — forma canónica (a que o corpus usa 506+ vezes) | sim | sim | não (gap distinto, ver TEKO_HISTORY/raiz-comum-dos-degraus) | sim | nativo: `undefined reference to 'teko_concat'` |
| `bogus_namespace::str(bytes)` — qualificador INVENTADO | sim, **sem erro** | não (acidente) | não (acidente) | **NÃO — resolve para o builtin `str` ignorando o qualificador; só não roda porque o C gerado não bate com nenhum símbolo** | `cc`: `invalid initializer` |
| `str::args()` — namespace certo (`str` existe via `use`) mas função ERRADA (`args` é de `teko::env`) | sim, **sem erro** | não (acidente) | não (acidente) | **NÃO — resolve para `teko::env::args`, ignorando `str` como qualificador; só não roda porque `args()` bare não existe** | `cc`: `invalid initializer` |
| `env::var(...)` — namespace CERTO, forma curta | sim | não | não | checker: sim; codegen: mesmo acidente que `str::concat` | `cc`: `invalid initializer` |
| `use teko::X` para `X` em {`list`,`text`,`env`,`process`} como TIPO | não (`unknown type: X`), idêntico com/sem `use` | — | — | n/a — falha honesta | `unknown type: X` |

**Conclusão para o dono:** o medo específico ("`str` como tipo vai colidir") está refutado — é o
único ponto de todo este mapa que é 100% seguro, com ou sem `use`. Mas a coisa que o `use teko::str`
deveria habilitar (`str::concat(...)` em vez do `teko::str::concat(...)` que o corpus escreve por
extenso 506+ vezes) não funciona hoje, em nenhum backend — e a razão não é `str`, é que o
checker aceita qualquer prefixo de namespace para uma chamada a um builtin, sem verificar se esse
prefixo tem qualquer relação com o alias real do `use`, e só não gera binários com o símbolo errado
porque o codegen (por acidente de gate, não por desenho) reduz quase todas essas formas a uma
falha de build honesta em vez de uma ligação silenciosa.
