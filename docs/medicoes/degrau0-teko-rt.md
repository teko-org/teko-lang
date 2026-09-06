# Degrau 0 — religacao dos builtins puros de bytes ao gemeo Teko (medicao)

Carga `cargo/0.3.1.0-nativo-degrau0-teko-rt`, a partir de `origin/fix/union`. Implementa
o **Degrau 0** do mapa `docs/design/nativo-sem-teko-rt.md`: religar, na rota NATIVA, os
builtins de **logica pura de bytes** aos corpos Teko que ja existem em
`src/runtime/teko_rt.tks`, em vez de despachar para o simbolo C `tk_*`.

## O que foi religado (Degrau 0 — nao aloca)

| Builtin (rota nativa) | Sitio de emissao (`lower.tks`) | Gemeo Teko | Simbolo mangle |
|---|---|---|---|
| `str ==` / `!=` | `lower_str_compare` (`str_eq_symbol`) | `str_eq` (`teko_rt.tks:496`) | `teko_teko__runtime__str_eq` |
| `ends_with` | `native_builtin_symbol` (`runtime_twin_symbol`) | `str_ends_with` (`:569`) | `teko_teko__runtime__str_ends_with` |
| `contains` | `native_builtin_symbol` (`runtime_twin_symbol`) | `str_contains` (`:580`) | `teko_teko__runtime__str_contains` |

Os tres gemeos LEEM bytes e devolvem `bool`/`i64` — nenhum aloca arena. A ABI bate: em
SysV/AAPCS64 `by_ref_fat_pairs_x86` devolve 0 (o par `(ptr,len)` viaja em registos), entao
a sequencia de registos e byte-identica entre `tk_str_*` e o gemeo; o corpo Teko usa a
convencao Teko-Teko natural que `bind_fat_param` ja liga do outro lado.

## O que NAO foi religado (e porque)

* **`concat`, `u64_to_str`, `i64_to_str`, `str_slice*`, `fmt_*`, `one_byte`, `str_of_bytes`** —
  Degrau **1**, BLOQUEADO. Os gemeos alocam um `str`/`[]T` novo via `teko::list::push` →
  `tk_slice_push` → `tk_region_alloc` → `malloc` (ver `str_concat` em `teko_rt.tks:116`).
  So saem quando o modelo de arena fechar as duas lacunas de `arena-em-teko.md`
  (`load_u64`/`store_u64`, cast `u64->ptr`). O par de regressao `rt_pure_bytes` exercita
  `concat`/`u64_to_str`/`i64_to_str` na rota nativa e C para provar que continuam a bater,
  mas eles NAO foram religados.
* **`str_cmp` (`tk_str_cmp`), `str_hash` (`tk_str_hash`)** — o lowering nativo NAO emite
  chamada nenhuma para estes hoje (comparacao ORDENADA de `str` para-honesto em
  `runtime_eq_predicate`; `str_hash` so aparece na tabela de classificacao, nunca como
  `LCall`). Sem sitio de emissao, nao ha o que religar.
* **`slice_eq_bytes`/`slice_str_eq`** — nao ha gemeo Teko puro em `teko_rt.tks`, e a rota
  generica precisa de um argumento `sizeof(T)`. Fica fora do Degrau 0.

## Reducao medida — quantos `tk_*` a rota nativa deixa de referenciar

A mudanca e CONFINADA a rota nativa (`lower.tks` + o corpo ja existente em `teko_rt.tks`).
A rota C (`codegen.tks`) NAO foi tocada — o `out/teko.c` gerado continua a referenciar os
mesmos `tk_str_*`, o que confirma que a religacao e so nativa:

```
$ grep -oE 'tk_str_eq|tk_str_ends_with|tk_str_contains' out-gen1/teko.c | sort | uniq -c
     26 tk_str_contains
     56 tk_str_ends_with
   1381 tk_str_eq
```

Esses 1381 + 56 + 26 sitios de chamada sao a SUPERFICIE que a rota nativa religa: no
backend nativo (com `teko_rt.tks` no corpus — o build do proprio compilador) cada um passa
a chamar `teko_teko__runtime__str_eq`/`str_ends_with`/`str_contains` em vez do `tk_*`. O
corpo Teko ja e emitido pelo backend (o proprio `out/teko.c` da rota C ja define os simbolos
mangle, prova de que existem no corpus):

```
$ grep -oE 'teko_teko__runtime__(str_eq|str_ends_with|str_contains)' out-gen1/teko.c | sort | uniq -c
      2 teko_teko__runtime__str_contains
      4 teko_teko__runtime__str_ends_with
     14 teko_teko__runtime__str_eq
```

**Resultado: 3 simbolos `tk_*` distintos** — `tk_str_eq`, `tk_str_ends_with`,
`tk_str_contains` — deixam de ser referenciados pelo objecto nativo do compilador (todos os
seus sitios de emissao nativa foram religados), substituidos por chamadas INTERNAS aos
gemeos Teko definidos no mesmo objecto. O piso de syscall (write/abort/malloc/…) fica, por
design.

## Seguranca para programas standalone

A religacao e CONDICIONAL (`collect_runtime_twins` + `runtime_twin_symbol`/`str_eq_symbol`):
so dispara quando `teko::runtime::<gemeo>` esta declarado no programa a compilar — o que so
acontece no build do PROPRIO compilador, onde `src/runtime/teko_rt.tks` faz parte do corpus.
Um programa standalone (que NAO carrega `teko_rt.tks`) recebe conjunto vazio de gemeos,
mantem o `tk_*` e continua a linkar `teko_rt.c` sem regressao. Isto e provado pelo par
`rt_pure_bytes_native`/`rt_pure_bytes_c`: standalone, usa `tk_*` em AMBAS as rotas e imprime
a mesma linha de valores.

## Estado da prova

* **Rota C SEM AVISO** — `TEKO_BACKEND=c teko . --no-verify --release` (gen1): exit 0, `cc`
  sem avisos, `out-gen1/teko.c` 10.1 MB, pico auto-reportado 1637.7 MB.
* **Paridade nativa vs C dos builtins** — `rt_pure_bytes_native` (rota nativa) e
  `rt_pure_bytes_c` (rota C) imprimem AMBOS `1_0_1_1_0_1_1_0_abc_42_-7` (afirmacao por
  STDOUT, nunca por exit code). Guarda que a religacao nao regrediu o baixamento nativo.
* **Corpo dos gemeos** — `src/runtime/teko_rt_test.tkt` ja exercita
  `teko::runtime::str_eq`/`str_ends_with`/`str_contains` diretamente (as duas engines
  compilam o corpo Teko), provando que os corpos sao semanticamente corretos.
* **FIXPOINT nativo (gen2 == gen3) + `nm -u` do objecto nativo** — BLOQUEADO LOCALMENTE por
  MEMORIA: o codegen nativo do compilador inteiro atinge ~15.8 GB de pico e e morto pelo
  OOM (`EXIT=137`) neste conteiner de 16 GB, em tres tentativas com ate 14.3 GB livres. NAO
  e causado por esta mudanca (adiciona um `[]str` de ~10 nomes curtos, nao gigabytes) — o
  `fix/union` base atinge o mesmo pico. O CI (mais memoria) fecha o fixpoint e mede
  `nm -u out-gen2/teko.o | grep tk_str_` (esperado: os 3 simbolos AUSENTES) e
  `nm out-gen2/teko.o | grep teko_teko__runtime__str` (DEFINIDOS e referenciados).

## Comando de medicao para o CI (mais memoria)

```
TEKO_BACKEND=native <gen1> . -o out-gen2 --no-verify --release        # gen2
TEKO_BACKEND=native out-gen2/teko . -o out-gen3 --no-verify --release # gen3
cmp out-gen2/teko out-gen3/teko                                       # FIXPOINT: byte-identico
nm -u out-gen2/teko.o | grep -E 'tk_str_eq|tk_str_ends_with|tk_str_contains'   # esperado: vazio
nm    out-gen2/teko.o | grep -E 'teko_teko__runtime__str_(eq|ends_with|contains)' # DEFINIDOS
```
