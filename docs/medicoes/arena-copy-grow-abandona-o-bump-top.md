# Onde vivem os ~2 GB da raiz do self-build — e por que o copy-grow os abandona

Medido com `TEKO_ARENA_OBS` sobre o proprio compilador compilando a si mesmo (rota C,
`TEKO_BACKEND=c teko . -o OUT --no-verify --release`), binario relinkado com `-g -rdynamic`
para simbolizar os call sites. Caixa de 16 GB, 4 CPUs.

## O quadro geral (ANTES)

```
root (process-lifetime, never freed):     1996.3 MB
scoped (freed at region drop):               0.0 MB
reclaimed by region drops:                   0.0 MB   (11 of 5127 regions dropped)
reclaimed by test-gate rewinds:              0.0 MB
reclaim ratio: 0.0%
CHUNKS: 5116 regions, 21846 chunks, malloc'd cap 1532.1 MB, used 1505.0 MB, tail-waste 27.1 MB
```

A raiz do processo cresce ate ~2 GB e nunca cai: `reclaim ratio` = **0,0%**.

## O alocador DOMINANTE e o copy-grow de slice

`tk_slice_push_r` responde por **1755,9 MB** dos 1996 MB da raiz (88%). A tabela de copy-grow
por funcao chamadora aponta o ofensor unico:

```
=== PUSH copy-grow bytes by CALLING fn (RA1): 1755.88 MB total ===
   0      946.0 MB      1249981 allocs  teko::checker::variant_siblings
   1       63.7 MB        86665 allocs  teko::checker::inline_rw_block
   2       60.7 MB        88320 allocs  teko::checker::resolve_type
   3       52.0 MB         2318 allocs  teko::lexer::tokenize
   4       51.5 MB          864 allocs  teko::checker::type_param_table
   5       34.3 MB       121492 allocs  teko::codegen::cb_byte
```

`variant_siblings` sozinho: **946 MB em 1,25 M copy-grows.**

## A brecha, no codigo

`variant_siblings` (`src/checker/resolve.tks`) e o padrao canonico do problema — um `loop`
que constroi UMA lista, cujo UNICO alocador no corpo e o proprio `push`:

```
mut out: []Type = teko::list::empty()
loop {
    if i >= xs.len { break }
    if i != skip { out = teko::list::push(out, xs[i]) }
    i++
}
```

Num alocador bump, cada crescimento por copia (`cap` esgotado → dobra) aloca um buffer NOVO e
**abandona o antigo** como espaco morto na regiao raiz, que so cai no fim do processo (nunca).
Mas o buffer antigo AQUI e sempre o **topo do bump**: nada mais foi alocado desde o ultimo
append (o corpo do loop so faz `push`). Um buffer no topo do bump pode ser **estendido no
lugar** — sem copia, sem abandono.

## Por que o cache de live-tail existente nao basta

O `tk_push_cache` ja faz append in-place quando `len < cap`, mas na fronteira de dobra
(`len == cap`, "cap-full") ele copy-grow. Pior: o cache e um hash de 65536 slots COMPARTILHADO
por todo o programa, entao um buffer que ainda e o topo do bump perde sua entrada por colisao/
eviccao e o proximo `push` vira um miss "other-ptr" → copy-grow. As razoes de miss confirmam:

```
=== PUSH miss reasons: empty 1162665  other-ptr 2189530  len 2581  cap-full 415509  esz/gen 0 ===
```

**2,19 M misses "other-ptr" + 415 K "cap-full"** — a esmagadora maioria copy-grow que abandona
um buffer que, no caso do loop-uma-lista, ainda e o topo do bump.

## A hipotese do conserto (grow-in-place seguro) — e por que a MEDICAO a rejeitou

A hipotese era uma prova de "topo do bump" INDEPENDENTE do cache, dentro de `tk_slice_push_r`,
antes de alocar fresco: se `off(ptr) + round_up(len*esz, 16) == head->used` da regiao, entao o
bloco de `ptr` termina exatamente no topo do bump — nada foi alocado depois — e da para
**estender `head->used` no lugar**, sem copia e sem abandonar nada. `[0, len*esz)` nunca e
tocado, logo qualquer alias vivo sobrevive (mesma seguranca do append in-place). Prova puramente
aritmetica, sem analise de escape.

O conserto e SEGURO. Mas a medicao (pico RSS do self-build, rota C) mostrou que ele **REGRIDE**
a memoria, nao reduz. O motivo esta em `arena-copy-grow-ja-e-reclamado-pelo-fo.md`: o copy-grow
dominante NAO e um vazamento — `variant_siblings` e os outros ofensores lowerizam para
`tk_slice_push_fo` (free-old provado por escape), que ja PARKEIA o buffer antigo na free-list.
O grow-in-place COMPETE com esse mecanismo e o faz passar fome. O "reclaim ratio: 0,0%" e um
ARTEFATO da metrica (ela nao contava o reuso da free-list), nao a ausencia de reclamacao.
