---
seq: 0197
crumb-id: EX-A4
milestone: M3
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [EX-A0]
sources:
  - "docs/design/expurgo-intrinsecos-inlines-0.3.1.md:1"      # §1.A A18–A20
---

# 0197 · EX-A4 — remover name-detect dos predicados de char

> `is_alpha`/`is_digit`/`is_space` (bool) têm corpo (`teko_rt.tks:174/180/186`). Escalar → abaixo-da-linha.
> Cuidado: os ramos hoje casam `&& c.call_ns.len == 0` (codegen 3818–3820) — a condição confirma que só o
> nome-nu era pego; com EX-A0 o `call_ns` passa a existir, então o ramo já não dispararia — remover é limpo.

## Goal

Remover `is_alpha`/`is_digit`/`is_space` (codegen 3818–3820) e o espelho native. Como o guard atual é
`&& c.call_ns.len == 0`, EX-A0 já os torna inertes (call_ns setado) — este crumb faz a remoção formal e
prova byte-identidade.

## Where

- `src/codegen/codegen.tks:3818-3820` — ramos `is_alpha/is_digit/is_space` (guard `c.call_ns.len == 0`).
- `src/lir/lower.tks` — espelho (predicado de char em `native_builtin_symbol`; localizar por `is_alpha`).
- NÃO tocar `teko_rt.tks`.

## How

1. Remover os 3 ramos (codegen) + espelho native.
2. Confirmar que a resolução via `call_ns` (EX-A0) emite `teko::runtime::is_alpha` etc — símbolo idêntico.
3. below-line (bool).
4. Fixpoint + ASan + reseed.

## Rulings & laws

- **Teko-only. D161. D159 (bool abaixo-da-linha). D148 (par C+native).**
- **W15; sem `//`.**
- **Safety:** nunca `teko test .`; `ulimit -v 4718592`; `gen2==gen3` + ASan+UBSan (D166); ratchet D68;
  reseed (D164).

## Fixtures

`none — the fixpoint self-build exercises this` (o lexer classifica chars ao rodar).

## Gate

`[fixpoint]` — `gen2==gen3` + ASan+UBSan limpo. Reseed-class: `fixpoint-rebuild`.

## Deps

`EX-A0`

## Done when

Nenhum `if last=="is_alpha|is_digit|is_space"` em codegen/lower e `gen2==gen3`.
</content>
