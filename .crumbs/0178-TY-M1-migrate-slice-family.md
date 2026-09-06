---
seq: 0178
crumb-id: TY-M1
milestone: M5
gate: "[fixpoint]"
reseed-class: "none"
deps: [TY-C2]
sources:
  - "docs/design/types-tks-prelude-forma3-0.3.1.md:5"        # §5 escalonamento
---

# 0178 · TY-M1 — migrar a família SLICE para método (`teko::str::slice*` → `.slice*`)

> Primeiro lote de troca-de-idioma: `teko::str::slice`/`slice_to`/`slice_from` (~80 sites) viram
> `s.slice(a,b)`/`s.slice_to(b)`/`s.slice_from(a)`. Behavior-preserving (o método resolve à MESMA
> impl builtin) → fixpoint gen2==gen3. Valida a ponte TY-C0 em campo estreito antes de `concat`.

## Goal

Converter os call-sites da família slice do idioma-solto para o idioma-método. Nenhuma mudança de
semântica: o método `str.slice` delega a `teko::str::slice`. O `teko.c` muda (call-sites), o
fixpoint prova a equivalência. Sem reseed (gen0 já tem a ponte+métodos de TY-C2).

## Where

- Todos os `teko::str::slice(...)` (34), `teko::str::slice_to(...)` (24), `teko::str::slice_from(...)`
  (22) na árvore `src/**.tks` → forma-método no receptor. Localizar por
  `grep -rn 'teko::str::slice' src --include=*.tks`.

## How

1. Para cada site `teko::str::slice(s, a, b)` → `s.slice(a, b)`; `teko::str::slice_to(s, b)` →
   `s.slice_to(b)`; `teko::str::slice_from(s, a)` → `s.slice_from(a)`.
2. Onde o receptor é uma sub-expressão complexa, manter legibilidade (extrair `var` se preciso).
3. NÃO tocar a impl builtin (`teko::str::slice` segue existindo como delegado do método).
4. Rodar o fixpoint; gen2==gen3 é o guard da equivalência.

## Rulings & laws

- **Teko-only.**
- **D145 fixpoint-safe:** migração por família, cada lote guardado por fixpoint.
- **Behavior-preserving:** método = mesma impl; sem novo comportamento.
- **W15; no `//`.**
- **Safety:** nunca `teko test .`; subshell `ulimit -v 4718592`; build gen2 + `gen2==gen3`. SEM
  reseed neste lote (reseed acumulado no terminal TY-T1). Ratchet: pico ~flat.

## Fixtures

`none — the fixpoint self-build exercises this` (a família slice é usada pelo compiler-core; o
self-build a exercita).

## Gate

`[fixpoint]` — build gen2 + regressão de escopo + `gen2==gen3` byte-idêntico. "Green" = a árvore usa
`.slice*` como idioma e o fixpoint fecha. Reseed-class: `none`.

## Deps

`TY-C2`

## Done when

Zero `teko::str::slice*` como call-site direto na árvore (só como delegado interno do método) e
`gen2==gen3`.
