---
seq: 0199
crumb-id: EX-A6
milestone: M3
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [EX-A0, W4-REGION-PARAM]
sources:
  - "docs/design/expurgo-intrinsecos-inlines-0.3.1.md:1"      # §1.A A24–A29 (†POST-W4)
---

# 0199 · EX-A6 — remover name-detect do grupo A ALOCANTE (pós-W4, região-aware)

> `intern_get`/`intern_put`/`intern_reset`, `read_line`/`read_stdin`, `slice`/`slice_to`/`slice_from`
> têm corpo de superfície, MAS retornam `str` (alocante) ou usam região → roteá-los genéricos ACIONA a
> maquinaria de região-por-param do **W4**. **BLOQUEADO até o W4 fechar** (não tanglar o byte-mover de
> risco — D155/D160).

## Goal

Após o W4 (`sweep/w4-region-param`) landar a região-por-param, remover os desvios alocantes do grupo A e
deixá-los fluir genéricos — a emissão genérica auto-injeta o param de região (D160), fonte-única-de-verdade.

## Where

- `src/codegen/codegen.tks:3801-3803` (`intern_get/put/reset`), `3764/3766` (`read_line/read_stdin`),
  `3730-3732` (`emit_str_slice`: `slice/slice_to/slice_from`).
- `src/lir/lower.tks` — `builtin_str_slice_symbol` (2051), io `read_line`/`read_stdin`, `is_str_arg_builtin`
  (1833) para os str-arg.
- Corpos: `teko_rt.tks:501/517/540/123/132/136`, `rtio.tks:217/253` — NÃO tocar.

## How

1. **Confirmar W4 landado** (região-por-param LIVE nas chamadas normais alocantes). Sem isso, PARAR.
2. Confirmar neutralidade de região de `intern_*`: o intern aloca na **sua própria** região (intern
   table), não na do caller → pode ser below-line quanto ao destino; verificar contra o oráculo antes de
   remover (se o retorno `str` do intern for tratado como heap-do-caller, precisa do param — deixar o
   oráculo decidir).
3. `read_line`/`read_stdin` (str no caller) e `slice*` (subslice sem cópia, mas retorna `str` = aponta ao
   backing do arg) → recebem região pela via genérica. `slice*` NÃO copia → verificar que o param de
   região não força cópia indevida.
4. Remover os desvios das duas rotas; fixpoint (agora a emissão MUDA — recebe região) → reseed.
5. ASan + reseed.

## Rulings & laws

- **Teko-only. D161. D160 (região auto na emissão genérica). D155 (não fazer ponte que morre — esperar o
  W4, não injetar região à mão). D148 (par C+native).**
- **Fork protocol:** neutralidade de região do intern é determinada pelo oráculo `residence.tks`; se o
  oráculo não decidir sem ambiguidade, HALT curto.
- **W15; sem `//`.**
- **Safety:** nunca `teko test .`; `ulimit -v 4718592`; `gen2==gen3` + ASan+UBSan (D166); ratchet D68
  (região-param pode mover pico — medir); reseed (D164).

## Fixtures

`none — the fixpoint self-build exercises this` (o compilador interna nomes, lê stdin, fatia strings).

## Gate

`[fixpoint]` — `gen2==gen3` + ASan+UBSan limpo + pico não-cresce. "Green" = zero name-detect alocante do
grupo A e a emissão genérica conduz a região. Reseed-class: `fixpoint-rebuild`.

## Deps

`EX-A0`, **W4 (região-por-param, `sweep/w4-region-param`)** — HARD BLOCK.

## Done when

Nenhum name-detect de `intern_*|read_line|read_stdin|slice*` em codegen/lower e `gen2==gen3` com região
conduzida pela via genérica.
</content>
