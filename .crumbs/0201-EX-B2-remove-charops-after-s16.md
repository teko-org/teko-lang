---
seq: 0201
crumb-id: EX-B2
milestone: M3
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [EX-A0, S16-CHARS]
sources:
  - "docs/design/expurgo-intrinsecos-inlines-0.3.1.md:2"
---

# 0201 · EX-B2 — remover name-detect de char-ops após corpo §16

> `chars`/`len_chars`/`char_at`/`str_slice_chars`/`to_lower`/`to_upper` emitem C
> `tk_str_chars`/`tk_str_len_chars`/`tk_char_at`/`tk_str_slice_chars`/`tk_to_lower`/`tk_to_upper`
> (codegen 3814–3817, 3821–3822) — **sem superfície**. Grupo B = §16 chars (decode UTF-8/case).
> **BLOQUEADO no §16.** (Alguns retornam `[]char`/`str` alocante → coordenam com região/W4 quando removidos.)

## Goal (design-ahead)

Contrato contra a superfície §16 (decode de char em Teko puro). Removal DEPOIS do corpo. Os alocantes
(`chars`→`[]char`, `str_slice_chars`→str, `to_lower/to_upper`→str) exigem região → removê-los só com o
W4 LIVE (como EX-A6); os escalares (`len_chars`→u64, `char_at`→char) são below-line, removíveis assim que
o corpo §16 existir.

## Where

- `src/codegen/codegen.tks:3814-3817, 3821-3822` — remover após §16 (split escalar/alocante).
- `src/lir/lower.tks:2045` (`builtin_str_query_symbol` cobre `char_at`?) + char paths — remover após §16.

## How

1. **Bloqueado:** aguardar §16 chars. 2. Remover escalares (`len_chars`/`char_at`) primeiro (below-line).
3. Remover alocantes (`chars`/`str_slice_chars`/`to_lower`/`to_upper`) só com W4 LIVE. 4. Fixpoint+ASan+reseed.

## Rulings & laws

- **Teko-only. D161. §16 chars. D159 (split escalar/alocante). D160 (região auto alocante). D148.**
- **W15; sem `//`.**
- **Safety:** `ulimit -v 4718592`; `gen2==gen3` + ASan+UBSan (D166); ratchet D68; reseed (D164).

## Fixtures

`none — the fixpoint self-build exercises this` (o lexer decodifica chars/case ao rodar).

## Gate

`[fixpoint]` — `gen2==gen3` + ASan+UBSan limpo. Reseed-class: `fixpoint-rebuild`.

## Deps

`EX-A0`, **§16 chars (corpo)**; alocantes também **W4** — HARD BLOCK.

## Done when

Nenhum `tk_str_chars|tk_str_len_chars|tk_char_at|tk_str_slice_chars|tk_to_lower|tk_to_upper` em
codegen/lower e `gen2==gen3`.
</content>
