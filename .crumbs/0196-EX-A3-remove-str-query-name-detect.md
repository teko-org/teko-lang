---
seq: 0196
crumb-id: EX-A3
milestone: M3
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [EX-A0]
sources:
  - "docs/design/expurgo-intrinsecos-inlines-0.3.1.md:1"      # §1.A A13–A17
---

# 0196 · EX-A3 — remover name-detect da família str-query não-alocante

> `str_hash`(u64)/`str_compare`(i64)/`len`→`str_len`(u64)/`ends_with`→`str_ends_with`(bool)/
> `contains`→`str_contains`(bool) têm corpo (`teko_rt.tks:101/109/140/144/152`). Retornam escalar →
> não-alocante → abaixo-da-linha → sem região → independe do W4.

## Goal

Remover os ramos `str_hash`/`str_compare`/`len`/`ends_with`/`contains` (codegen 3810/3811/3812/3823/3824)
e o espelho native. Note o remap de nome: `len`→`str_len`, `ends_with`→`str_ends_with`,
`contains`→`str_contains`. A resolução via `call_ns` deve mapear o nome-nu à `exp fn` correta do
prelúdio; verificar que o símbolo emitido casa com o do desvio.

## Where

- `src/codegen/codegen.tks:3810-3812, 3823-3824` — ramos str-query.
- `src/lir/lower.tks:2045` — `builtin_str_query_symbol(last)` — remover as entradas correspondentes.
- Atenção ao alias `len`: o desvio hoje emite `str_len`; a resolução do nome-nu `len` sobre `str` deve
  achar `teko::runtime::str_len` (ou o método `.len` já roteado — verificar que NÃO colide com `str::len`
  de outras vias). Se `len` for ambíguo (slice `.len` vs str), MANTER só o caso `str` e confirmar por
  fixpoint. Se surgir ambiguidade genuína não-deliberada → HALT (fork).
- NÃO tocar `teko_rt.tks`.

## How

1. Remover os 5 ramos (codegen) + espelho native (`builtin_str_query_symbol`).
2. Verificar o remap de nome na resolução (`len`/`ends_with`/`contains` → `str_len`/`str_ends_with`/
   `str_contains`): a `exp fn` de destino é a de `teko::runtime`, símbolo idêntico ao desvio.
3. Confirmar below-line (retorno escalar).
4. Fixpoint + ASan + reseed.

## Rulings & laws

- **Teko-only. D161. D159 (escalar abaixo-da-linha). D148 (par C+native).**
- **Fork protocol:** se `len` resolver ambíguo entre `str` e slice de modo não-deliberado, HALT curto.
- **W15; sem `//`.**
- **Safety:** nunca `teko test .`; `ulimit -v 4718592`; `gen2==gen3` + ASan+UBSan (D166); ratchet D68;
  reseed (D164).

## Fixtures

`none — the fixpoint self-build exercises this` (o checker/lexer usam hash/compare/len/contains ao rodar).

## Gate

`[fixpoint]` — `gen2==gen3` + ASan+UBSan limpo. "Green" = zero name-detect str-query nas duas rotas e o
alias `len`/`ends_with`/`contains` resolve ao símbolo idêntico. Reseed-class: `fixpoint-rebuild`.

## Deps

`EX-A0`

## Done when

Nenhum `if last=="str_hash|str_compare|len|ends_with|contains"` em codegen/lower e `gen2==gen3`.
</content>
