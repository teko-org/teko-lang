---
seq: 0130
crumb-id: STR-CC2
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [STR-CC1]
sources:
  - "docs/design/str-concat-slice-eager-free-0.3.1.md:20-46"     # veredito frente B + causas do 2992 MB
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:42"       # conversão str = Teko puro
  - "DECISION_LOG.md:1066-1072"                                  # D112
---

# 0130 · STR-CC2 — interpolação + `concat` variádico + `str_concat` → UMA alocação Teko

> Colapsa o fold pairwise (N-1 intermediários) da interpolação `$"..."` e do `concat` variádico numa
> ÚNICA chamada `teko::runtime::concat([pieces])` (passada única, tamanho exato); `str_concat`
> binário → símbolo Teko. Esta é a alavanca de memória do D112.

## Goal

Hoje `emit_interp` (`codegen.tks:~4311`) e `emit_concat_fold` (`~3539`) lowerarm interpolação e
`concat` variádico para `tk_str_concat` ANINHADO/em-loop → N-1 buffers intermediários por
interpolação (tráfego quadrático, N-1 buffers vivos). É a causa dominante do "dobra pra 2992 MB"
(shift libc→arena + acúmulo). Este crumb reescreve as duas emissões para UMA chamada à fn variádica
Teko `concat(params pieces: []str)` (`teko_rt.tks:52` — uma passada de tamanho EXATO, 1 buffer) e
roteia `str_concat` binário (`codegen.tks:3809`) pro símbolo Teko. Remove N-1 intermediários por
interpolação; o buffer único morre no pop de escopo (a saída do codegen JÁ streama — o resultado do
concat é escrito no `FileStream` e descartado). **MEDIR o pico é o gate.** `fixpoint-rebuild`.

## Where

- `src/codegen/codegen.tks:3809` — `else if last == "str_concat" { builtin = "tk_str_concat" }` →
  `builtin = cb_fn_name_str("teko::runtime", "str_concat")` (símbolo Teko, como `str_eq`/`str_hash`
  já fazem).
- `src/codegen/codegen.tks:~3539` `emit_concat_fold` — REESCREVER: em vez do loop
  `{ca} = tk_str_concat({ca}, {cs}.ptr[{ci}])`, emitir UMA chamada
  `teko_teko__runtime__concat(<slice []str das pieces>)` — o arg `cs` já É o `[]str`; passá-lo
  direto à `concat` variádica (ABI de `params`/slice que o codegen já emite).
- `src/codegen/codegen.tks:~4311-4315` `emit_interp` — REESCREVER: em vez de N-1 `tk_str_concat(`
  aninhados abertos por `concat_open`, montar um `[]str` literal dos `nsegs` segmentos (pieces via
  `emit_piece_lit`, holes via os `emit_*` de spec) e emitir UMA chamada
  `teko_teko__runtime__concat((tk_str[]){seg0, seg1, …}, nsegs)`.
- `src/runtime/teko_rt.tks:5-13,52-66` — `str_concat` (binário, aloca `[a.len+b.len]byte` exato) e
  `concat` (variádico, passada única exata) — JÁ corretos; confirmar `exp` (linkável, D111). NÃO
  alterar corpo.
- `src/runtime/teko_rt.c:310,325` `tk_str_concat`/`_concat_r` — ficam MORTOS até F9 (D90).

## How

1. Rotear `str_concat` binário pro símbolo Teko (1 linha, 3809).
2. `emit_concat_fold`: substituir o bloco `({ … for(...) tk_str_concat … })` por uma única chamada
   à `concat` variádica passando o `[]str` de entrada (`cs`) direto. Sem loop, sem intermediário.
3. `emit_interp`: coletar os `nsegs` segmentos num `[]str` (mesma ordem piece/hole de hoje) e emitir
   `teko_teko__runtime__concat` UMA vez. Remover `concat_open`/o laço de abre-parênteses.
   Preservar a residência: quando `str_region.len > 0` (escopo), a `concat` Teko já aloca na região
   corrente via a maquinária de arena-por-escopo (RM-C6) — o buffer morre no pop.
4. Regra de forma do `[]str` de segmentos: montar por índice/literal (NO PUSHES) — o número de
   segmentos é conhecido em compile-time (`nsegs`).
5. **Medir o pico** do build seco (`teko: memory: peak N MB`) e comparar maçã-com-maçã ao baseline
   1146,1 MB. O single-alloc + reclamação por escopo deve manter/baixar; se um sítio quente
   `out=concat(out,…)` regredir, ver acoplamento com `0131` (drenam juntos).

Nenhuma declaração Teko nova de superfície; as fns `str_concat`/`concat` pré-existem. As mudanças
são em `fn` privadas de codegen (`emit_interp`/`emit_concat_fold` — sem doc).

## Rulings & laws

- **Teko-only / D90:** zero edição em `teko_rt.c`; corpos C `tk_str_concat`/`_concat_r` mortos até
  F9. Mudança 100% em `codegen.tks` (+ `exp` confirmado em `teko_rt.tks`).
- **D112 (DECISION_LOG:1066):** concat na nossa codebase = acúmulo → alocação-única/saída direta;
  interpolação materializada N-1 = mesma classe do `push` → expurgada pro single-alloc.
- **D68 ratchet (DURO):** pico só cai/flat. **Este é o gate — o crumb NÃO landa se medir >1146 MB.**
- **NO PUSHES:** o `[]str` de segmentos é montado por índice/tamanho-exato, nunca `push`.
- **I/O streaming:** a saída do codegen já é `FileStream` append-only ≤1024 B (`cg_emit_c_file_mode`)
  — nada a converter aqui; o resultado do concat é escrito e descartado.
- **Fork protocol:** deliberado em D112 + design-doc; sem fork aberto.
- **Safety:** NUNCA `teko test .`; subshell `ulimit -v 4718592`; `TEKO_CC=clang`; gen0 do
  `bootstrap/teko.c`; commit por passo; fixpoint `gen2==gen3` byte-idêntico; **reportar o pico
  medido**; reseedar `bootstrap/teko.c` ao fim (incondicional); gen2/gen3 no scratchpad.

## Fixtures

`none — the fixpoint self-build exercises this` (o compilador usa interpolação `$"..."` e `concat`
massivamente ao emitir; o fixpoint exercita ambos os paths).

## Gate

`[fixpoint]` — build gen2 + `gen2==gen3` byte-idêntico + **pico do build seco medido ≤ 1146,1 MB**.
"Green" = interpolação e `concat` variádico emitem UMA chamada `teko::runtime::concat`, `str_concat`
binário roteia pro símbolo Teko, nenhum `tk_str_concat`/`_concat_r` no path do codegen de
concat/interp, pico ≤ baseline, `teko.c` reproduz. **Reseed-class:** `fixpoint-rebuild`.

## Deps

`STR-CC1` (`0129` — landa a frente A antes; a headroom do slice-view precede a batalha de pico do
concat).

## Done when

Interpolação/`concat`/`str_concat` emitem alocação-única Teko, nenhum símbolo C de concat no path,
pico do build seco medido ≤1146 MB, `[fixpoint]` gen2==gen3 byte-idêntico, `bootstrap/teko.c`
reseedado. (Se regressão por acumulador quente → drenar junto com `0131`.)
