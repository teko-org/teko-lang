---
seq: 0180
crumb-id: TY-M3
milestone: M5
gate: "[fixpoint]"
reseed-class: "none"
deps: [TY-M2]
sources:
  - "docs/design/types-tks-prelude-forma3-0.3.1.md:4"        # §4.3 fronteira byte/char
  - "src/codegen/codegen.tks:3733-3743"                      # intrínsecos chars/char_at/to_lower
  - "src/text/text.tks:138,251"                              # decode_utf8_u32 / str_to_u32
---

# 0180 · TY-M3 — char-level → `char::from`/`[]char` (encoding EXPLÍCITO) + numérico `to_str`/`parse`

> Torna a fronteira byte↔char explícita nos call-sites do compilador: `s.char_at(i)`/`chars(s)`
> (hidden-UTF-8) → `char::from(s, Encoding::Utf8)[i]` / `char::from(s, Encoding::Utf8)`. E os
> numéricos `i64_to_str`/`float_parse` → `x.to_str()` / `u64::parse(s)`. Behavior-preserving (o
> compilador SABE que seu fonte é UTF-8 → passa `Utf8` explícito). Fixpoint gen2==gen3. Sem reseed.

## Goal

Este é o lote que MATERIALIZA o modelo do dono: `str` é bytes crus, `char` é code point, e a
travessia nomeia o encoding. Onde o compiler-core hoje itera chars sobre `str` via os intrínsecos
mágicos (`chars`/`char_at`/`len_chars`/`str_slice_chars`), passa a decodificar EXPLICITAMENTE com
`char::from(s, Encoding::Utf8)` e operar no `[]char`. Os numéricos ganham `.to_str()`/`::parse`.

## Where

- Call-sites de `chars`/`char_at`/`len_chars`/`str_slice_chars` (os intrínsecos de
  `codegen.tks:3733-3743`) no `src/**.tks` → `char::from(s, Encoding::Utf8)` + índice/len sobre o
  `[]char`. Localizar por `grep -rn '\bchar_at\|\bchars(\|len_chars\|str_slice_chars' src --include=*.tks`.
- Call-sites de conversão numérica (`i64_to_str`/`u64_to_str`/`ftoa`/`float_parse`) → `.to_str()` /
  `u64::parse(s)` / `f64::parse(s)` onde melhora legibilidade (opcional onde já é intrínseco fino).
- Case-mapping char-level (`to_lower`/`to_upper` sobre char) → método de `char`.

## How

1. Cada `char_at(s, i)` → `char::from(s, Encoding::Utf8)[i]` (ou extrair `var cs = char::from(s,
   Encoding::Utf8)` quando há vários acessos, evitando re-decode). `chars(s)` → `char::from(s,
   Encoding::Utf8)`. `len_chars(s)` → `char::from(s, Encoding::Utf8).len`.
2. Onde o site é hot e o re-decode cresceria memória, decodificar UMA vez para `var cs: []char` e
   operar por índice (respeita NO-PUSHES / bound-exato).
3. `to_lower`/`to_upper` sobre `char` → `c.to_lower()`.
4. Numérico: `i64_to_str(x)` → `x.to_str()`, `float_parse(s)` → `f64::parse(s)`, etc.
5. Fixpoint como guard. Atenção: onde o intrínseco mágico e o `char::from` divergirem em bytes
   emitidos, o método é o novo alvo — o fixpoint valida gen2==gen3 na NOVA forma (não contra a antiga).

## Rulings & laws

- **Teko-only.**
- **Modelo do dono (mid-task):** `str`=bytes, `char`=code point, travessia EXPLÍCITA nomeando o
  encoding — SEM UTF-8 escondido. O compiler-core passa `Encoding::Utf8` porque conhece seu input.
- **Reuso:** `char::from` consome `teko::text::decode_utf8_u32` (landado).
- **NO-PUSHES / bound-exato:** decodificar uma vez, operar por índice; não crescer array.
- **W15; no `//`.**
- **Safety:** nunca `teko test .`; subshell `ulimit -v 4718592`; build gen2 + `gen2==gen3`; SEM
  reseed (acumulado em TY-T1). Ratchet D68: medir — decode-uma-vez não deve crescer o pico; se
  crescer, é causa-raiz (re-decode em loop), não teto.

## Fixtures

`none — the fixpoint self-build exercises this` (o compiler-core itera chars sobre o próprio fonte
UTF-8; o self-build exercita o caminho novo).

## Gate

`[fixpoint]` — build gen2 + `gen2==gen3` byte-idêntico + medição de pico (ratchet). "Green" = os
call-sites char-level nomeiam o encoding, operam no `[]char`, e o fixpoint fecha sem crescer o pico.
Reseed-class: `none`.

## Deps

`TY-M2`

## Done when

Os intrínsecos char-level sobre `str` no compiler-core viram `char::from(…, Encoding::Utf8)`
explícito, os numéricos usam `.to_str()`/`::parse`, e `gen2==gen3` com pico ~flat.
