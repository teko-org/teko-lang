---
seq: 0205
crumb-id: EX-B6
milestone: M3
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [EX-A0, S16-D126]
sources:
  - "docs/design/expurgo-intrinsecos-inlines-0.3.1.md:2"
---

# 0205 · EX-B6 — remover hand-emit de err_loc/err_typed após corpo §16-D126

> `err_loc`/`err_typed` são **hand-emit inline** (codegen 3653–3671: `tk_error_loc(...)` /
> `tk_error_types(...)` montados à mão) — o pior tipo de acoplamento D161 (corpo-C sintetizado no
> codegen). Grupo B = §16-D126 (`error_make`/loc). **BLOQUEADO no §16-D126.**

## Goal (design-ahead)

Quando o §16-D126 landar `teko::runtime::err_loc(msg, line, col)` e `err_typed(a, b, c)` como superfície
Teko, substituir o hand-emit (codegen 3653–3671) por chamada genérica e remover o espelho native
(`is_err_loc_call`/`is_err_typed_call`, lower 1262/1263). Elimina C-hand-emit escondido (endgame D161).

## Where

- `src/codegen/codegen.tks:3653-3661` (`err_loc` hand-emit), `3663-3671` (`err_typed` hand-emit).
- `src/lir/lower.tks:1262-1263` — `is_err_loc_call`/`is_err_typed_call` + `lower_err_loc_call`/
  `lower_err_typed_call`.

## How

1. **Bloqueado:** aguardar §16-D126 (`error_make`/loc superfície).
2. Trocar o bloco `({ … })` hand-emit por resolução genérica ao símbolo de superfície.
3. Remover o par native (honest-stop de name-detect → resolução por `call_ns`).
4. Fixpoint + ASan + reseed.

## Rulings & laws

- **Teko-only. D161 (matar hand-emit C escondido = endgame zero-C). §16-D126. D148.**
- **W15; sem `//`.**
- **Safety:** `ulimit -v 4718592`; `gen2==gen3` + ASan+UBSan (D166); ratchet D68; reseed (D164).

## Fixtures

`none — the fixpoint self-build exercises this` (o checker constrói erros com loc/types ao rodar; o corpo
é do §16-D126).

## Gate

`[fixpoint]` — `gen2==gen3` + ASan+UBSan limpo. Reseed-class: `fixpoint-rebuild`.

## Deps

`EX-A0`, **§16-D126 (error_make/loc corpo)** — HARD BLOCK.

## Done when

Zero hand-emit `tk_error_loc`/`tk_error_types` em codegen e zero `is_err_loc_call`/`is_err_typed_call` em
lower; `gen2==gen3`.
</content>
