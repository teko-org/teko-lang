---
seq: 0131
crumb-id: STR-CC3
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [STR-CC2]
sources:
  - "docs/design/str-concat-slice-eager-free-0.3.1.md:48-78"     # veredito frente C + condição de segurança
  - "DECISION_LOG.md:1066-1072"                                  # D112 string=array eager-free
---

# 0131 · STR-CC3 — eager-free de reatribuição self-consuming de string (`x = concat(x,…)`)

> Estende o eager-free-na-reatribuição (hoje só ARRAY) para `x = concat(x,…)`/`x = str_concat(x,…)`
> string-typed: alocação FRESH → libera o backing antigo na hora. EXCLUI `x = slice(x,…)` (view).
> Completa a semântica string=array do D112.

## Goal

O eager-free existe PARCIAL para array (`escape.tks:assign_frees_old` + `codegen.tks:emit_assign`
7089 emitem `free_block(x.ptr, x.len*sizeof(*x.ptr))` guardado por `assign_is_self_append`, birth
`[]`, sem escape). Não cobre `x = concat(x,…)`. Este crumb adiciona o predicado str-específico e o
costura na mesma maquinaria de contagem-de-reads, reclamando os acumuladores `out=concat(out,…)`
quentes que sobrarem após `0130`. Concat aloca buffer FRESH (não aliasa `x`) → liberar o backing
antigo é seguro; slice retorna VIEW no backing → NUNCA entra (liberar = UAF). Só correto QUANDO
concat já é arena (pós-`0130`) — por isso depende de `STR-CC2`. `fixpoint-rebuild`.

## Where

- `src/checker/escape.tks:264-285` `assign_is_self_append` — NÃO alterar (fica array-only, para não
  contaminar `assign_routes_to_frame`/`mark_self_append_items`). Adicionar predicado SEPARADO.
- `src/checker/escape.tks` (novo, junto de `assign_is_self_append`) — `pub fn
  assign_is_self_concat(a: TAssign): bool` — Simple-assign, `a.value` = call a `str_concat`/`concat`/
  `teko::str::concat`/`teko::runtime::str_concat` com arg0 == `TVar{a.name}`, e `a.bound` é `Str`.
- `src/checker/escape.tks:318-328` `binding_value_is_list_empty` — adicionar irmão `fn
  binding_value_is_empty_str(b: TBinding): bool` (value = `TStrLit` VAZIO `""`), e no `chain_stats_stmt`
  birth (`:415`) contar `binding_value_is_list_empty(b) || binding_value_is_empty_str(b)` como
  `births_empty`.
- `src/checker/escape.tks:426-428` `chain_stats_stmt` — classificar `assign_is_self_append(a) ||
  assign_is_self_concat(a)` como `self_appends += 1`.
- `src/checker/escape.tks:457-458` `assign_frees_old` — `if !assign_is_self_append(a) &&
  !assign_is_self_concat(a) { return false }`.
- `src/checker/escape.tks:216` `collect_frees_old_stmt` — `(assign_is_self_append(a) ||
  assign_is_self_concat(a)) && !str_set_has(...) && assign_frees_old(...)`.
- `src/checker/escape.tks:637` (dentro de `count_reads_stmt`) — `if a.name == name &&
  (assign_is_self_append(a) || assign_is_self_concat(a)) { n = n + count_reads_expr(a.value, name) }`.
- `src/codegen/codegen.tks:7089-7111` `emit_assign` — o free-path é GENÉRICO em `a.bound`; para
  `Str`, `emit_type(Str)`→`tk_str`, `.ptr`/`.len` válidos, `sizeof(*.ptr)`=1. VERIFY-ONLY (confirmar
  que dispara com `a.bound == Str`; provável zero-mudança).

## How

1. `assign_is_self_concat`: `match a.kind { Simple => {}; _ => return false }`; `a.bound` deve ser
   `Str`; `a.value` deve ser `TCall` cujo último segmento é `str_concat`|`concat` (bare ou
   `teko::str::`|`teko::runtime::`), com `args.len >= 1` e `args[0]` = `TVar{name==a.name}`.
2. `binding_value_is_empty_str`: `match b.value.kind { TStrLit as s => s.text.len == 0; _ => false }`
   (campo `text`, `tast.tks:14 TStrLit = struct { text: str }`).
3. Costurar os 5 pontos acima (birth-empty, chain-stats, frees_old, collect, count_reads) com
   `assign_is_self_concat`/`binding_value_is_empty_str` em OR ao lado dos existentes.
4. `mark_self_append_items` (`:287`) / `assign_routes_to_frame` (`:302`) — NÃO tocar: são array-only
   (guardados por `a.bound == Slice`); string não entra.
5. Confirmar (verify) que `emit_assign` free-path dispara para `a.bound == Str` — se o `emit_type`/
   `emit_assign_lvalue` já cobrem `Str`, zero-mudança de codegen.
6. Medir o pico: deve ficar ≤ o de `0130` (reclama acumuladores quentes). Segurança do 1º free:
   birth `""` (len 0) → `free_block(ptr, 0)` = no-op (`arena.tks:736`/`ar_free_block` sub-mínimo).

Sem superfície `exp` nova (predicados são `pub`/privados de checker — sem doc, lei de estilo). O
`assign_is_self_concat` é `pub` (usado por codegen indiretamente via `assign_frees_old`), mas não é
superfície de usuário → sem doc-comment.

## Rulings & laws

- **D112 (DECISION_LOG:1066):** string = array → eager-free-na-reatribuição idêntico ao array; concat
  = alocação fresh (dev pode concatenar), o backing antigo é purgado no ato da reatribuição.
- **Segurança (design-doc §condição EXATA):** libera SÓ se RHS é FRESH self-consuming (concat), `x`
  não escapa, birth único vazio `""`, `other_writes==0`, contagem de reads casa (view/read extra
  desqualifica); ordem `temp=RHS; free(old); x=temp` (já garantida). `slice(x,…)` (view) NUNCA entra.
- **Teko-only / D90:** só `.tks` (checker/codegen); `teko_rt.c` intocado.
- **Não-detectar-o-inexistente:** o predicado casa CONSTRUÇÃO que a superfície produz (`x=concat(x,…)`),
  não caso impossível.
- **D68 ratchet:** pico ≤ o de `0130`; medir.
- **Estilo:** sem `//`; sem doc em `pub`/privado.
- **Fork protocol:** deliberado em D112 + design-doc; sem fork aberto.
- **Testes:** o self-build exercita concat/reatribuição — NENHUM `.tkt`/`.tkr` afirmativo novo.
- **Safety:** NUNCA `teko test .`; subshell `ulimit -v 4718592`; `TEKO_CC=clang`; gen0 do
  `bootstrap/teko.c`; commit por passo; **sweep `.tkt`/`.tkr`** após mudança de assinatura no checker;
  fixpoint `gen2==gen3` byte-idêntico; reportar pico; reseedar `bootstrap/teko.c` ao fim; gen2/gen3
  no scratchpad.

## Fixtures

`none — the fixpoint self-build exercises this` (o compilador contém `out=concat(out,…)` e o fixpoint
executa gen1 em larga escala; o path de free é dirigido pelo self-build). Caso de UAF/rejeição não
se aplica: o guard é conservador (não emite free onde não é seguro), não há caminho de erro a
oraculizar.

## Gate

`[fixpoint]` — build gen2 + `gen2==gen3` byte-idêntico + **pico ≤ o medido em `0130`**. "Green" =
`x = concat(x,…)`/`str_concat(x,…)` string-typed emite `free_block` do backing antigo sob a condição
segura, `slice(x,…)` NÃO emite free, pico não regride, `teko.c` reproduz. **Reseed-class:**
`fixpoint-rebuild`.

## Deps

`STR-CC2` (`0130` — concat tem que ser arena/Teko ANTES: liberar um ptr malloc'd do `tk_str_concat`
C via freelist da arena = corrupção).

## Done when

`x = concat(x,…)` self-consuming string libera o backing antigo no ato (semântica array-idêntica),
`slice(x,…)` fica de fora, pico ≤ o de `0130`, `[fixpoint]` gen2==gen3 byte-idêntico, `.tkt`/`.tkr`
varridos, `bootstrap/teko.c` reseedado.
