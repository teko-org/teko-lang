# `one_byte` no backend nativo: o degrau 32 fecha, e o degrau 33 chama-se `str_slice_chars`

Medição do degrau 32 da série 0.3.1.0 — o **único** degrau que separava a lane do **fixpoint
NATIVO**. Fecha, e a medição diz com um endereço qual é o degrau que estava atrás dele.

## 0. O que parava, e exactamente onde

A rota nativa parava a construir o próprio compilador com:

```
teko: .: native backend N1: builtin `one_byte` not yet lowered (N2)
       [in `teko::encoding::json::parse_string`]
```

A mensagem é **construída**, não literal: quem a procurar por texto no fonte não a encontra. Nasce
em `src/lir/lower.tks:4262` (`unresolved_builtin_stop`), chamada de `src/lir/lower.tks:4216` quando
`call_symbol` não resolve nome nenhum.

O disparo é `src/encoding/json/json.tks:218-225` — os oito ramos de escape de
`teko::encoding::json::parse_string`, cada um a juntar um `one_byte(...)` ao `str` acumulado — e
`src/encoding/json/json.tks:296`, o par de bytes de um ponto de código UTF-8.

O builtin já existia nos dois sítios que não são o baixamento nativo:

| papel | endereço |
|---|---|
| assinatura no checker, `(byte): str` | `src/checker/scope.tks:787` |
| espelho de runtime em Teko | `src/runtime/teko_rt.tks:169` |
| resolução da rota C, `tk_one_byte` | `src/codegen/codegen.tks:4334` |
| **baixamento nativo** | **ausente** |

Por isso a rota C **nunca** parou e **não prova nada** sobre a rota nativa: são duas realizações
independentes, e só a segunda estava por fazer.

## 1. A correcção, e por que o símbolo é o gémeo `_len` e não o `tk_one_byte` da rota C

`one_byte` devolve um `str`, que é um valor **GORDO** (ponteiro + comprimento, dois eightbytes na
SysV/AAPCS64). O `LCall` do backend nativo lê **UM** registo de resultado, nunca o par. Todos os
produtores gordos deste backend passam portanto pelo molde `lower_len_out_call`
(`src/lir/lower.tks:10517`): o ponteiro viaja no registo de retorno e o comprimento numa ranhura de
saída que o chamador reserva.

`src/lir/lower.tks:4067` (`builtin_one_byte_symbol`) resolve para `tk_one_byte_len`, o gémeo novo em
`src/runtime/teko_rt.c:347` / `src/runtime/teko_rt.h:439`. Está registado na família de
`native_builtin_symbol` (`src/lir/lower.tks:4136`) ao lado de `builtin_str_of_bytes_symbol` e
`builtin_bytes_of_str_symbol`, que já usavam esse mesmo molde nas suas próprias direcções.

**Nenhum braço de baixamento novo foi preciso.** O único argumento é um `byte`, um escalar simples,
que atravessa `lower_args` como uma palavra tal como o `i64`/`u64` de `builtin_int_to_str_symbol`.

**O parâmetro do gémeo é de largura inteira (`uint64_t`) e a máscara está DENTRO.** A SysV e a
AAPCS64 deixam por especificar os bits **acima** da largura declarada de um argumento, e este
backend estreita **RETORNOS** apenas (`apply_native_c_return_narrow`) — não tem passagem de
estreitamento de argumentos. É a mesma forma que `tk_i64_to_str_len` e `tk_str_slice_len` já dão aos
seus escalares. Confiar nos bits altos de um `tk_byte` aqui seria ler lixo com uma probabilidade que
depende do que a chamada anterior deixou no registo.

## 2. A medição do regressor: seis pesos disjuntos, duas rotas, o mesmo 255

`examples/regressions/one_byte_native` (rota nativa, backend por omissão) e
`examples/regressions/one_byte_c` (o **oráculo**, `TEKO_BACKEND=c`) correm o **mesmo programa byte a
byte** (`src/ob/ob.tks`, idêntico nas duas pastas).

| peso | forma medida | origem no fonte real |
|---|---|---|
| 3 | uma chamada isolada, comprimento e conteúdo lidos | o caso mínimo |
| 12 | `str` acumulado com `~ one_byte(...)` repetido | `json.tks:218-225` |
| 16 | dois resultados na MESMA expressão | `json.tks:296` |
| 32 | argumento vindo de um índice de slice num laço | `json.tks:242`, `json.tks:412` |
| 64 | dois `str` de um byte VIVOS ao mesmo tempo | apanhou o par `f64_bits` no degrau 24 |
| 128 | `one_byte(0)` — o byte NUL | nenhum comprimento por `strlen` o produziria |

3 + 12 + 16 + 32 + 64 + 128 = **255**, e **nenhuma soma parcial vale 255**: qualquer forma que caia
move o número e diz qual foi.

Medido nesta caixa (Linux x86_64, gcc 13.3.0):

| rota | código de saída | pico auto-reportado do build |
|---|---|---|
| nativa (por omissão) | **255** | 12,4 MB |
| C (`TEKO_BACKEND=c`) | **255** | 9,6 MB |

O argumento de cada chamada passa por `opaque_zero()`/`shift()` — um zero **sem forma literal** —
porque de outro modo `comptime_fold` resolveria a chamada inteira antes de o baixamento nativo
chegar a ver uma, e o regressor passaria sem exercitar nada.

### Por que são DUAS pastas e não dois cenários no mesmo `.tkr`

`tkr_ensure_built` (`src/build/regression.tks`) compila um projecto **UMA VEZ** por ficheiro `.tkr`,
com o env do **PRIMEIRO** cenário, e a cache é chaveada na **forma** do build e não no env
(`regr_built_serves`). Um cenário de rota C ao lado do nativo no mesmo ficheiro correria o binário
**NATIVO** com `TEKO_BACKEND=c` posto só à execução — mediria a coisa errada em silêncio. E dois
`.tkr` na mesma pasta escreveriam ambos em `<pasta>/bin/<nome>`.

### O registo no manifesto não tem glob

`src/build/manifest.tks:109-111` lê a lista `regression = [...]` de `teko.tkp` literalmente; um
caminho não listado **não corre**, e um caminho listado que não abra é **erro de manifesto** (M.3),
nunca um salto silencioso. Auditoria feita nesta medição: **14 directórios em
`examples/regressions/`, 14 caminhos na lista, correspondência exacta nos dois sentidos.**

## 3. O DEGRAU 33, com endereço — `str_slice_chars`

Com `one_byte` baixado, a construção nativa do compilador **avança** e para no degrau seguinte:

```
teko: .: native backend N1: builtin `str_slice_chars` not yet lowered (N2)
       [in `teko::encoding::json::trim_trailing_zeros`]
```

| papel | endereço |
|---|---|
| disparo | `src/encoding/json/json.tks:595`, em `trim_trailing_zeros` (declarada em `:589`) |
| assinatura no checker, `(str, i64, i64): str` | `src/checker/scope.tks:837` |
| resolução da rota C, `tk_str_slice_chars` | `src/codegen/codegen.tks:4352` |
| símbolo de runtime já na lista de permissão nativa | `src/lir/lower.tks:3480` |
| paragem | `src/lir/lower.tks:4262`, via `:4216` |
| **baixamento nativo** | **ausente** |

Outros chamadores que a mesma ausência atinge: `src/encoding/url/url.tks:245`, `:250`, `:267`.

**Este degrau NÃO foi fechado aqui, de propósito.** É outro degrau, com o seu próprio gémeo de
runtime a decidir (o resultado é gordo, tal como `one_byte`, portanto pede um `_len`) e o seu
próprio regressor. Fechá-lo de arrasto misturaria duas medições num commit e nenhum dos dois
regressores diria qual das duas o protegia. **Nenhum KNOWN-STOP foi cunhado**: a paragem continua a
ser uma paragem, e o veredito do fixpoint nativo continua FAILED até ela cair.

## 4. Ritual, com o backend de cada corrida dito por extenso

| corrida | backend | resultado |
|---|---|---|
| build do compilador, rota C | `TEKO_BACKEND=c` | **limpo, ZERO avisos**, 57,2 s, pico 1578,7 MB |
| `scripts/fixpoint_gate.sh` | **`TEKO_FIXPOINT_BACKEND=native`** | **FAILED em gen2** — e a razão é o **degrau 33**, `str_slice_chars`, não `one_byte` |
| `scripts/fixpoint_gate.sh` | `c` (omissão) | ver o quadro do PR |
| `./out/teko test .` | rota C | ver o quadro do PR |

A semente desta caixa foi construída de `bootstrap/teko.c` (`scripts/build_gen1_from_c.sh`), porque
o contentor foi reciclado e não havia binário publicado em cache; ela reporta-se como
`teko 0.3.0.31-beta`.

**O que a corrida nativa prova, e é o ponto todo desta medição:** o endereço da paragem **mudou**.
Antes era `one_byte` em `teko::encoding::json::parse_string`; depois é `str_slice_chars` em
`teko::encoding::json::trim_trailing_zeros`. O degrau 32 caiu, medido pelo instrumento que o
denunciava.
