# `teko::text::bytes_of_str`/`teko::text::str_from_utf8` — a wrapper era não precisa (0.3.1.0)

**Encomenda**: uma lane anterior (`cargo/0.3.1.0-text-bytes`, base `remodel/0.3.1.0-linux-native-2`)
não criou `teko::text::bytes_of_str` porque concluiu que um WRAPPER Teko com esse nome, dentro da
própria namespace `teko::text`, chamaria o builtin bare `bytes_of_str` e **recursaria infinitamente**
(mesmo-nome-mesma-namespace vence em `lookup_call`). O raciocínio era bom E acabou por ser
irrelevante — medido, não assumido, abaixo.

**Resposta curta: `teko::text::bytes_of_str(s)` e `teko::text::str_from_utf8(bytes)` JÁ FUNCIONAM
hoje, sem wrapper nenhum, na rota C.** Nenhum wrapper foi escrito porque nenhum é preciso — e
nenhum EXISTE hoje (não há recursão possível porque não há o código que recursaria). Na rota
own-native (nativa) as duas param HONESTAMENTE num "not yet lowered" — um gap real, mas de uma
família totalmente diferente (registo de lowering, não resolução de nomes), já enumerado como
sibling em aberto em `docs/memory/0.3.1.0-linux-native-first-stop.md` (fim da entrada do degrau 17).

Commit medido: `9847fe6` (branch `cargo/0.3.1.0-text-bytes`, base `remodel/0.3.1.0-linux-native-2`).

## Método

`sh scripts/fetch_teko.sh` falhou com 403 (bloqueio de proxy do agente, conhecido). Escada usada:

```sh
sh scripts/build_gen1_from_c.sh bootstrap/teko.c src .gen1        # gen1 (C comitado)
TEKO_BACKEND=c .gen1/teko . -o .gen1b --no-verify --release       # gen1b (fonte ACTUAL, rota C)
```

`.gen1b` completou de facto (89.4s, `teko: memory: peak 1398.5 MB`) — a auto-construção via C route
fecha inteiramente nesta árvore; é a rota NATIVA da auto-construção que hoje pára no degrau 18
(`src/build/project.tks:253`, `reference deref-assignment not yet lowered`), um assunto sem relação
com este, já localizado noutra lane.

Toda sonda de valor foi um projecto standalone descartável em `/tmp/.../scratchpad/probe*/`
(`teko.tkp` + `main.tks`), nunca dentro da árvore, compilado por `.gen1b/teko` pelas DUAS rotas.

## 1. Rota C (`TEKO_BACKEND=c`) — funciona, valor confirmado

Fonte (string multi-byte: acento + emoji, para que um bug de fronteira não se esconda atrás de
ASCII):

```teko
let s: str = "café🐝"
let bs: []byte = teko::text::bytes_of_str(s)
println($"len_bytes={bs.len}")
let s2: str | error = teko::text::str_from_utf8(bs)
match s2 {
    str as r => println($"roundtrip_str={r}")
    error as e => println($"roundtrip_error={e.message}")
}
```

Build (`TEKO_BACKEND=c .gen1b/teko . -o outc --no-verify`): `checker 5/5 ✓ … cc outc/probe ✓`,
`teko: .: built outc/probe`. Execução:

```
len_bytes=9
roundtrip_str=café🐝
```

`9` é o valor certo: `c`+`a`+`f`+`é`(2 bytes UTF-8)+`🐝`(4 bytes) = 1+1+1+2+4 = 9. O roundtrip
devolve a string original byte a byte, acento e emoji incluídos.

**Qual implementação foi alcançada?** O `.c` emitido (`outc/probe.c`) contém:

```c
tk_slice_byte bs = tk_bytes_of_str(s);
...
tk_ffi_sres _h848r = tk_rt_str_from_utf8(_h848s.ptr, (uint64_t)_h848s.len);
```

— o BUILTIN injectado (`tk_bytes_of_str`/`tk_rt_str_from_utf8`, `scope.tks:622-627` +
`codegen.tks:3732`/`5548`), **não** a função Teko real de `src/text/text.tks:56`. Isto é o
esperado: o projecto de sonda (`probe`) não declara o seu próprio `teko::text::str_from_utf8`, e o
doc-comment de `scope.tks:498-505` já dizia que o fallback só entra quando "no real declaration
answers". Confirmado por leitura do `.c`, não assumido.

**A própria árvore do compilador alcança a função REAL**, não o builtin, nos dois únicos call
sites QUALIFICADOS (`teko::text::str_from_utf8`): `src/numeric/bigint/bigint.tks:531` e
`src/numeric/dec/dec.tks:205` — porque aí `cur_ns` é diferente e o qualificador `teko::text` casa
com a namespace REAL da função declarada em `src/text/text.tks`, e `lookup_call` vence sobre o
fallback (exactamente o que o doc-comment de `scope.tks` afirma). Todos os OUTROS call sites do
próprio compilador (`regex.tks`, `stream.tks`, `csv.tks`, `base64.tks`, `url.tks`, `json.tks`,
`str_iter.tks`, `byte_iter.tks`) chamam a forma BARE, fora da namespace `teko::text` — essas
alcançam o BUILTIN, não a função real, porque uma chamada bare só casa com uma ligação real cuja
`ns == cur_ns` (`call_binding_matches`, `scope.tks:271-274`).

## 2. A recursão temida — refutada por ausência do código, não por argumento

`src/text/text.tks:61-63` documenta `bytes_of_str` **num comentário apenas**:

```
// bytes_of_str(s: str): []byte — expose the raw UTF-8 bytes of a str as a []byte
// slice. Zero-copy; the returned slice views the same memory. Registered as a builtin
// (like str_of_bytes) in scope.c/tks; this comment is the canonical documentation.
```

Não há `pub fn bytes_of_str` nenhuma nesse ficheiro nem em nenhum outro (`grep -rn "fn bytes_of_str"
src/` não devolve nenhuma definição). O medo era: um WRAPPER `teko::text::bytes_of_str` chamando o
builtin bare de dentro da própria namespace recursaria (mesmo-nome-mesma-namespace vence em
`lookup_call`). **Esse wrapper nunca existiu** — a recursão nunca poderia ter acontecido, porque o
código que recursaria não foi escrito, não porque alguém o evitou correctamente. E não precisa de
ser escrito: `builtin_qualifier_ok` (`typer.tks:1536-1538`) já aceita qualquer callee "bare ou
literalmente enraizado em `teko`", a QUALQUER profundidade — `teko::text::bytes_of_str` é uma
dessas formas, tal como `teko::str::concat` já era. Nenhuma mudança de código fecha esta peça;
apenas a prova.

## 3. Rota own-native (nativa) — pára, mas por razão NÃO relacionada

O MESMO ficheiro, mesmo `.gen1b`, backend por omissão (sem `TEKO_BACKEND=c`):

```
teko: .: native backend N1: builtin `bytes_of_str` not yet lowered (N2) [in `snippet::text_byteview_roundtrip_probe`]
```

E, isolando só `str_from_utf8` (sem o `bytes_of_str` anterior no mesmo ficheiro, para que a PRIMEIRA
falha não esconda a segunda):

```
teko: .: native backend N1: builtin `str_from_utf8` not yet lowered (N2)
```

O checker chega ao MESMO ponto nas duas rotas (`checker 2/2 items ✓`, `monomorph 0/0`, `consteval
0/0` idênticos) — só a fase de LIR LOWERING (`src/lir/lower.tks`, `call_symbol` →
`native_builtin_symbol`) pára. `native_builtin_symbol` (`lower.tks:2172-2183`) é uma lista fechada
de dez famílias (io/cov/arena/str-query/str-slice/int-to-str/`str_of_bytes`/host-info/`peak_rss`/
`env::args`) — nem `bytes_of_str` nem `str_from_utf8` estão nela. Isto já estava ENUMERADO como
sibling em aberto, sem molde estreito identificado ainda, no fim da entrada do degrau 17 de
`docs/memory/0.3.1.0-linux-native-first-stop.md` ("`len`/`bytes_of_str`/`chars`/…" e
"`str_from_utf8`" na lista de host-FFI `sres`/`ures`/`slres`). Nada aqui contradiz esse mapa; esta
sonda apenas o confirma com um programa mínimo isolado em vez de o alcançar através da
auto-construção inteira (que hoje nem chega lá — pára antes, no degrau 18, um gap de LINGUAGEM,
não de resolução de builtin).

**Bónus, medido para não confundir os dois gaps**: uma função Teko REAL equivalente (não passando
pelo builtin nenhum, apenas `str(b)`, que ESTÁ na lista de `native_builtin_symbol` via
`str_of_bytes`) compila e corre nativamente sem problema:

```
$ ./outn/probe2
roundtrip_str=Café
$ echo $?
0
```

Mas a validação UTF-8 real (`valid_utf8`, `text.tks:17-47`) usa `match b { 0xC2..=0xDF => …, … }` —
um PADRÃO DE INTERVALO (`RangePattern`) — que tem o SEU PRÓPRIO gap nativo, distinto e já conhecido:
`native backend N1: range match pattern not yet lowered (N2)`. Ou seja: mesmo fechando o gap do
builtin injectado, a função REAL de `text.tks` teria um SEGUNDO obstáculo nativo antes de correr —
um gap de padrão de match, nada a ver com nomes ou namespaces.

## Veredicto final

| forma | rota C | rota own-native | implementação alcançada (rota C, projecto sem `teko::text` próprio) |
|---|---|---|---|
| `teko::text::bytes_of_str(s)` | funciona, valor certo (9 bytes) | pára: `builtin \`bytes_of_str\` not yet lowered (N2)` | builtin injectado (`tk_bytes_of_str`) |
| `teko::text::str_from_utf8(bytes)` | funciona, valor certo (roundtrip exacto) | pára: `builtin \`str_from_utf8\` not yet lowered (N2)` | builtin injectado (`tk_rt_str_from_utf8`) |
| `teko::text::str_from_utf8(bytes)`, dentro da PRÓPRIA árvore, chamada QUALIFICADA (`bigint.tks`/`dec.tks`) | — | — | a função REAL de `text.tks:56` (via `lookup_call`, não o fallback) |

**Conclusão para o dono**: a peça da RECURSÃO/nomenclatura está FECHADA — refutada, não corrigida,
porque não havia nada para corrigir. Nenhum wrapper foi escrito, nenhum é preciso. A peça do GAP
NATIVO continua ABERTA, mas é uma peça DIFERENTE (registo de lowering em `native_builtin_symbol`),
já mapeada noutro documento, e fora do escopo desta lane.

## Fixtures

- `/regressor.tkr` — duas `Feature`s (uma por KIND, `tkr_feature_is_compile_fail` decide pela
  PRIMEIRA scenario da feature — medido o preço de as misturar: a segunda scenario perdeu o seu
  próprio `When compilation fails` e foi julgada como "deve compilar"):
  - "teko::text byte-view escape hatches — …" :: rota C, `When built and run` / `Then exit = 0`.
  - "teko::text byte-view escape hatches — the OWN-BACKEND gap (KNOWN-STOP, …)" :: rota own-native,
    `When compilation fails` / `Then diagnostic = "builtin \`bytes_of_str\` not yet lowered"`.
- `/cases/text_byteview_roundtrip.tks` — a fonte partilhada pelas duas scenarios acima (multi-byte,
  0=ok / 1=comprimento errado / 2=roundtrip errado / 3=erro inesperado).

## Critério de aceitação da lane `.len` — cravado pelo dono (2026-07-29)

Literal do dono: *"o que esperamos é que o len de uma str 'café🐝' retorne 5, neste caso, e a
iteração resulte 5 passos extraindo os caracteres."*

A MESMA string que já serve de sonda deste documento pelo lado dos BYTES passa a servir de sonda
pelo lado dos CARACTERES. Uma string, os dois contadores, os dois medidos:

| expressão | valor exigido | estado |
|---|---|---|
| `"café🐝".len` | **5** | a implementar (hoje devolve bytes) |
| `teko::text::bytes_of_str("café🐝").len` | **9** | MEDIDO e a funcionar na rota C |
| passos de `for c in "café🐝"` | **5** | a implementar |

`5 = c + a + f + é + 🐝`. `9 = 1+1+1+2+4`. A string foi escolhida porque cobre as três larguras
UTF-8 que interessam (1, 2 e 4 bytes) — nenhum bug de fronteira se esconde atrás de ASCII, e a
diferença 5≠9 é grande o bastante para que uma troca de contador falhe alto em vez de passar
despercebida.

**Consequência que muda a sequência dos vagões.** "Extrair os caracteres" quer dizer que a variável
do laço é um `char`, e `char` é açúcar que baixa para `[]byte` (ruling do dono). Logo os 5 valores
produzidos têm larguras 1,1,1,2,4 — a iteração NÃO pode ser um passeio indexado por byte, precisa de
descodificar UTF-8 e devolver uma vista de largura variável.

E daí sai o encaixe: o primeiro gesto de qualquer dev que itere caracteres é comparar um deles —
`if c == c'é'`. Isso é EXACTAMENTE o buraco de Categoria 3 achado pela auditoria da superfície
óbvia (`cargo/0.3.1-superficie-obvia`, commits `6dbc5f7`/`7952d73`): `char == char` falha nas DUAS
rotas (rota C: `cc` recusa com `invalid operands to binary == (have 'tk_char' and 'tk_char')`;
nativa: `'char' has no single PrimKind`). Portanto **`char ==` não é um achado lateral — está no
caminho crítico desta lane** e tem de fechar ANTES de a iteração por caracteres ser entregue, senão
entregamos um laço que produz valores que ninguém consegue comparar.

Ordem que isto impõe: `char ==` → `c'X'` baixa para `[]byte` (vagão próprio do `char`) → `str`
carrega os dois contadores → `.len` conta caracteres (lane própria, com bump de versão e a semente a
seguir, porque o fixpoint não compara um compilador cuja semântica de `.len` difere da sua semente).

Fixture: tem de verificar VALOR (5 e 9), não que compila — a lei das fixtures desta esteira nasceu
precisamente de quatro miscompilações silenciosas que passaram por só verificarem compilação.

### `.len` de um `char` = BYTES — decidido pelo dono (2026-07-29)

Eu recomendei `1` (por coerência com "`.len` conta caracteres"). **O dono corrigiu, e a correcção é
melhor.** Literal: *"Se char tem len (por fazer lower de []byte), o len do char deve responder o
número de bytes que ele carrega; já o len de str é o número de caracteres; pq o char não carrega
2,3,4 caracteres, o caractere é 1 de largura sempre."*

O argumento: perguntar quantos CARACTERES tem um `char` não informa nada — a resposta é sempre 1,
por definição. A única resposta com conteúdo é quantos bytes ele carrega. E isso mantém o `char`
honesto sobre o que ele é: açúcar que baixa para `[]byte`.

| expressão | valor |
|---|---|
| `"café🐝".len` | **5** (caracteres) |
| `c'🐝'.len` | **4** (bytes) |
| `c'a'.len` | **1** (bytes — e caractere, por coincidência) |

**Invariante que sai daqui, e que vira fixture:** a soma dos `.len` dos chars produzidos pela
iteração de uma `str` tem de ser igual ao `.len` do seu `bytes_of_str`.

    1 + 1 + 1 + 2 + 4 = 9 == teko::text::bytes_of_str("café🐝").len

Uma fixture que verifica os dois contadores E a sua reconciliação apanha uma troca de contador em
qualquer um dos três sítios — coisa que verificar `5` e `9` isoladamente não apanha.

## Degrau 21 — CLOSED (`cargo/0.3.1.0-degrau-21`): ambos os builtins, na rota nativa

O gap da §3 (rota own-native) está FECHADO. `native_builtin_symbol` (`src/lir/lower.tks`) ganhou
duas famílias novas:

- `bytes_of_str` (`builtin_bytes_of_str_symbol`) cabe EXACTAMENTE no molde `_len` out-parameter já
  usado por `str_of_bytes` (a mesma direcção invertida): `s`'s `(ptr, len)` já chega flattened por
  `lower_args`, e o runtime twin (`tk_bytes_of_str_len`, `teko_rt.c`/`.h`) devolve o MESMO par — sem
  cópia, sem alocação (o próprio `bytes_of_str` já era zero-copy). Uma entrada na tabela, nenhum
  ARM novo de lowering.
- `str_from_utf8` (`str | error`) É o irmão de payload-gordo de `last_index_of` (degrau 15, `u64 |
  error`): a ABI real do runtime (`tk_ffi_sres`, três eightbytes) não cabe no ÚNICO registo de
  resultado que `LCall` lê, então ganhou o mesmo tratamento — um twin `tk_rt_str_from_utf8_ok` que
  devolve `bool` (achou/falhou) e escreve o par `(ptr, len)` ACTIVO (o valor decodificado OU a
  mensagem "invalid UTF-8") em dois out-parameters partilhados pelas duas saídas —, um branch sobre
  esse bool, e a construção do wrapper de variant de 24 bytes uniforme (degrau 6): o arm OK escreve
  o par fat directamente no payload (`store_fat_variant_payload_pair`, extraído de
  `store_fat_variant_payload` para ser reutilizável sem uma `TExpr` fonte), o arm de erro caixa um
  `error { message = … }` do MESMO jeito que o arm not-found de `last_index_of` já fazia.

**Medido por VALOR, sonda `café🐝`, `/tmp/.../scratchpad/probe/` (projecto descartável, nunca dentro
da árvore), pelas DUAS rotas, `.gen1b/teko`**:

```
$ TEKO_BACKEND=c .gen1b/teko . -o outc --no-verify && ./outc/probe
len_bytes=9
roundtrip_str=café🐝
invalid_error=invalid UTF-8

$ .gen1b/teko . -o outn --no-verify && ./outn/probe
len_bytes=9
roundtrip_str=café🐝
invalid_error=invalid UTF-8
```

`diff <(./outc/probe) <(./outn/probe)` — IDÊNTICO, byte a byte, incluindo o caso de erro (a mesma
mensagem "invalid UTF-8" da própria `tk_rt_str_from_utf8`, não uma mensagem vazia). Confirmado
também no channel `examples/regressions/own_native` (`f_bytes_of_str`/`f_str_from_utf8`,
`main.tks` códigos 55/56): as duas rotas (C e own-native) do PROJECTO deste channel constroem e
correm, exit 42 nas duas.

`bootstrap/teko.c`/`teko.tkp` não foram tocados — só `.tks` (a tabela de lowering + a construção do
variant) e o runtime `teko_rt.c`/`.h` (dois twins novos, `tk_bytes_of_str_len` e
`tk_rt_str_from_utf8_ok`, cada um um wrapper fino sobre uma função já existente — nenhuma lógica de
validação/cópia duplicada).

### Achado ADJACENTE, fora de escopo — reportado, não corrigido

A auto-construção NATIVA do compilador inteiro (`teko . -o out --no-verify`, sem `TEKO_BACKEND=c`)
NÃO chega a `bytes_of_str`/`str_from_utf8` nesta árvore — pára ANTES, num sítio diferente e já
DOCUMENTADO como fora de escopo:

```
teko: .: native backend N1: unknown field `line` on struct `error` (internal) [in `teko::checker::const_type_located`]
```

`error_struct_layout` (`lir/lower.tks:8620-8632`) já documenta, por decisão PRÉVIA e deliberada, que
os campos de diagnóstico do `error` interno do checker (`file`/`line`/`col`/`expected`/`actual`) —
que `teko::checker::const_type_located`/`teko::error::err_loc` (`#594`) LÊEM — "have no
Teko-surface constructor and stay out of scope" (degrau N2, #382). `const_type_located` já existia
antes desta lane (mesclado por `#594`/`#600`, muito antes de `cargo/0.3.1.0-degrau-21`); medido com
`git stash` que a paragem é a MESMA, no MESMO ponto (`checker 6140/6140 ✓`), COM ou SEM as mudanças
desta lane — não é uma regressão introduzida aqui.

Ampliar `error_struct_layout` para os cinco campos de diagnóstico (e actualizar TODO ponto que hoje
caixa um `error { message = … }` de duas words só) é uma peça MAIOR, MAL escopada — um redesenho do
layout do `error` nativo — e não o "molde estreito" que este degrau fecha. Reportado ao integrador
para o wagon apropriado, não expandido aqui.

**Porque o `bytes_of_str`/`str_from_utf8` da CI (corrida 30455263710) e o `const_type_located` desta
lane nunca colidem no MESMO run**: a auto-construção nativa pára no PRIMEIRO item cujo lowering
falha, e qual item é esse depende da ORDEM de processamento dos itens do projecto — determinística
NESTA árvore/binário (reproduzido idêntico em runs repetidos), mas não necessariamente a MESMA de um
binário `gen1b` diferente (outra cadeia de bootstrap, outra CI). Os dois gaps são REAIS e
INDEPENDENTES; qual aparece primeiro num run de auto-construção completa é um detalhe de qual
binário/ordem, não um sinal de qual foi corrigido quando.

## `teko::str::slice` produz `str` INVÁLIDA — 104 sítios (medido 2026-07-29)

Achado pelo desenho de ranges e corte (`cargo/0.3.1-ranges-e-corte-desenho`, `5576a4c`), reportado e
não corrigido nessa lane. **Pertence à lane do `.len`-conta-caracteres**, que já vira a superfície de
`str` e já paga o ciclo de semente.

Medido na rota C, com a sonda canónica desta esteira:

```
teko::str::slice(s, 0, 4)      → caf\xc3     ← mojibake, meio codepoint
teko::str::str_slice_chars(s, 0, 4)  → café
```

`TEKO_LEGISLATION.md` diz que uma `str` **significa** UTF-8 válido — *"a `str` means valid UTF-8, so
it is (M.3 + M.1)"*. Logo isto não é escolha de desenho a debater: é violação de uma lei já escrita.

| forma | sítios | corta |
|---|---|---|
| `teko::str::slice` | 54 | **bytes** |
| `teko::str::slice_to` | 27 | **bytes** |
| `teko::str::slice_from` | 23 | **bytes** |
| `teko::str::str_slice_chars` | 10 | caracteres |

**104 contra 10.** A maioria esmagadora do compilador corta `str` por bytes, e está acidentalmente
correcta porque quase todo o texto que manipula é ASCII. O primeiro identificador acentuado, o
primeiro literal com emoji, ou a primeira mensagem de diagnóstico com um nome não-ASCII produz
`str` inválida — silenciosamente.

### Consequência para o sequenciamento, provada

`str_slice_chars(s, 0, s.len)` sobre `café🐝` **rebenta** com `codepoint index out of range`:
o `.len` de hoje devolve BYTES (9) e o corte por caracteres só tem 5. Ou seja, as duas metades da
lane do `.len` estão acopladas — não se pode migrar os 104 sítios para corte por caracteres antes de
`.len` contar caracteres, nem o contrário.

É também por isso que o desenho de ranges propõe um `str_cut_from` cujo fim é contado DENTRO do
runtime: `s[a..]` e `s[..]` passam a funcionar sem o programa mencionar `.len`, o que corta a
dependência circular e deixa a lane 1 dos ranges entrar ANTES da lane do `.len`.
