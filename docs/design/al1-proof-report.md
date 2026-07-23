# AL1 — Relatório da prova (proof-first, fechado)

Status: **FECHADO (2026-07-19).** Medição de runtime + censo estático do gargalo de emit,
antes de qualquer código de produto. Versionado na umbrella `remodel/emit-throughput`.
Design: `al-wave-emit-throughput.md`. Crumbs: `al-wave-crumbs.md`.

## Metodologia
- **Runtime:** self-build instrumentado com `TEKO_ARENA_OBS` (histograma de miss-reason +
  copy-grow por função RA1/RA2 + tabela dark-matter de str). Binário **simbolizado**
  (`cc -std=c2x -g -rdynamic`, sem strip — `c2x` ≡ C23) pra `dladdr` nomear as funções.
- **Estático:** censo dos ~1344 push-sites classificados por nível de conhecimento.

## Veredito das hipóteses
- **"Constantes disfarçadas" (AL0) NÃO é a dor de perf:** censo achou **5 funções** const-ificáveis
  (~1,4 KB rodata). ~92% dos push-sites são genuinamente dinâmicos (inevitável).
  → AL0 se mantém por **HONESTIDADE/W15**, não por perf.
- **Storm de colisão CONFIRMADO:** **2.579.369** misses `other-ptr` (colisão de slot no cache
  global `tk_push_cache`) — a maior categoria de miss. (Snapshot parcial de 512 MB tinha
  subestimado em 10×; a foto completa confirma.) Os grows >1MB seguem saudáveis (58 cap-full —
  a eviction size-aware protege o buffer grande), mas os **milhões de buffers menores** não.
- **Str dark-matter ENORME:** 310 MB em **15.469.819** buffers de str/format malloc'd.
- **Arena recicla 0%:** 1.698 MB de root nunca liberado (reclaim ratio 0,0%).

## Números
- copy-grow total: **1.138 MB**
- miss reasons (all | >1MB): empty 1.706.912|0 · **other-ptr 2.579.369|0** · len 2.877|0 ·
  cap-full 285.237|58 · esz/gen 0|0
- str dark-matter: **310 MB / 15,5M buffers**
- root nunca liberado: **1.698 MB**, reclaim **0,0%**

## Copy-grow por função (RA1) — distribuído no checker+codegen
```
117 MB  checker::inline_rw_block      71 MB  checker::resolve_type
108 MB  checker::type_param_table     59 MB  codegen::cg_lift_block
 78 MB  lexer::tokenize               58 MB  checker::mono_block
 56 MB  checker::type_block           55 MB  checker::inline_rw_exprs
 43 MB  codegen::cb_byte              41 MB  codegen::cg_name_reaches_byvalue
```
→ value-thread push pervasivo; **AL3 (ref-push) é o fix GLOBAL**, não pontual.

## Str dark-matter por função — nomes/chaves manglados recomputados
```
99 MB  codegen::cg_variant_typename_str   ← 6.650.000 allocs  (mesma string, determinística!)
22 MB  codegen::cg_variant_key            ← 1.030.000 allocs
20 MB  codegen::cg_opt_key                ← 2.850.000 allocs
17 MB  checker::qualify                   ← 1.100.000 allocs
 9 MB  codegen::cg_format_c               ← 1.600.000 allocs
```
As entradas de "2 allocs" grandes (`run_native_gate` 43 MB, `tk_emit_c_mode` 30+24 MB) são o
padrão BOM (aloca o todo uma vez, no fim — a regra "stream e lê o completo" do owner).

## ACHADO NOVO — AL4a: interning de nomes manglados
`cg_variant_typename_str` reconstrói a MESMA string manglada 6,65 MILHÕES de vezes (o nome de
um tipo é determinístico). Isso pede **memoização/interning** (computa 1× por tipo distinto,
cacheia), não só str-builder: 6,65M + 2,85M + 1,03M allocs → ~milhares. Semi-independente do
ref-push, grande e barato — entra CEDO, como o AL0. Não estava no design original.

## Levers finais (com alvo nomeado)
| Lever | Alvo medido | Onde |
|---|---|---|
| **AL3** ref-push (cap-no-valor) | 2,58M colisões / 1,14 GB | inline_rw, resolve_type, mono, cg_lift, cb_byte (global) |
| **AL4a** intern nomes manglados | 6,65M+2,85M+1,03M str | cg_variant_typename_str, cg_opt_key, cg_variant_key, qualify |
| **AL4b** str-builder (stream-não-concat) | resto dos 15,5M | cg_format_c, member_key |
| **AL5** region-per-phase | 1,7 GB, reclaim 0% | arena |
| **AL0** const-ificação | 5 sites | honestidade/W15 |

## Regras do owner incorporadas (2026-07-19)
- **"Código não mente sobre o que é":** um produtor determinístico de dado fixo é `const`, não
  função — AL0 é obrigatório por honestidade, independente do ganho de perf.
- **"Stream, não concat":** não concatenar em memória (concat realoca como push); escrever
  fragmentos num writer/stream e materializar uma vez, do tamanho final conhecido (alloc única).
