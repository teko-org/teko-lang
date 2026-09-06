---
seq: 0198
crumb-id: EX-A5
milestone: M3
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [EX-A0]
sources:
  - "docs/design/expurgo-intrinsecos-inlines-0.3.1.md:1"      # §1.A A21–A23
---

# 0198 · EX-A5 — remover name-detect misc não-alocante (peak_rss/stdin_eof/parse)

> `peak_rss`(u64, `rtio.tks:311`)/`stdin_eof`(bool, `rtio.tks:242`)/`parse`→`float_parse`(f64,
> `teko_rt.tks:1`) — escalares, abaixo-da-linha. Fecha o grupo A não-alocante.

## Goal

Remover `peak_rss` (codegen 3845), `stdin_eof` (3765), `parse`→`tk_float_parse` (3767) e os espelhos
native. `parse` remapeia a `teko::runtime::float_parse`; verificar o casamento de símbolo.

## Where

- `src/codegen/codegen.tks:3765` (`stdin_eof`), `3767` (`parse`), `3845` (`peak_rss`).
- `src/lir/lower.tks:2093` — `builtin_peak_rss_symbol`; `is_float_parse_call` (1266) → `stdin_eof` via io.
- Atenção: `parse` no lower usa `is_float_parse_call(c.callee)` (lower 1266) — remover esse honest-stop
  de name-detect e deixar `call_symbol` resolver via `call_ns`. Confirmar que a native ainda COMPILA
  (execução native deferida pós-marco, mas o self-build compila as duas rotas).
- NÃO tocar `rtio.tks`/`teko_rt.tks`.

## How

1. Remover os 3 ramos (codegen) + espelhos native (`builtin_peak_rss_symbol`, `is_float_parse_call`, io
   `stdin_eof`).
2. Verificar remap `parse`→`float_parse` (símbolo idêntico ao desvio `tk_float_parse`? — NÃO: o desvio
   emite `tk_float_parse` (C!), mas a superfície é `teko::runtime::float_parse`). **ATENÇÃO:** aqui o
   desvio de `parse` aponta ao C `tk_float_parse`, mas EXISTE a superfície `float_parse` (`teko_rt.tks:1`)
   — verificar se o corpo de `float_parse` é auto-suficiente em Teko ou se ele mesmo chama `tk_*`. Se
   `float_parse` ainda depender de C internamente, `parse` é **grupo B** (mover para EX-B3, não remover
   aqui). Decidir por leitura: se `teko_rt.tks:1 float_parse` tem corpo Teko puro → A; se chama `tk_*` →
   B. (O censo o listou como A; VERIFICAR o corpo antes de remover.)
3. `peak_rss`/`stdin_eof` são A confirmados (corpo Teko).
4. Fixpoint + ASan + reseed.

## Rulings & laws

- **Teko-only. D161. D159 (escalar abaixo-da-linha). D148 (par C+native).**
- **Fork protocol:** se `float_parse` revelar-se C-dependente, é reclassificação determinada (mover a
  EX-B3), não HALT.
- **W15; sem `//`.**
- **Safety:** nunca `teko test .`; `ulimit -v 4718592`; `gen2==gen3` + ASan+UBSan (D166); ratchet D68;
  reseed (D164).

## Fixtures

`none — the fixpoint self-build exercises this` (o build lê stdin/mede rss/parseia floats ao rodar).

## Gate

`[fixpoint]` — `gen2==gen3` + ASan+UBSan limpo. "Green" = zero name-detect misc-nonallocant; `parse`
resolvido (ou reclassificado a B). Reseed-class: `fixpoint-rebuild`.

## Deps

`EX-A0`

## Done when

Nenhum `if last=="peak_rss|stdin_eof"` (e `parse` se confirmado A) em codegen/lower e `gen2==gen3`.
</content>
