---
seq: 0179
crumb-id: TY-M2
milestone: M5
gate: "[fixpoint]"
reseed-class: "none"
deps: [TY-M1]
sources:
  - "docs/design/types-tks-prelude-forma3-0.3.1.md:5"        # §5 escalonamento
---

# 0179 · TY-M2 — migrar a família de PREDICADOS para método

> `teko::str::ends_with` (37), `contains` (20), `last_index_of` (6), `starts_with` (1) → forma-método
> (`s.ends_with(x)` etc., ~64 sites). Behavior-preserving → fixpoint gen2==gen3. Sem reseed.

## Goal

Segundo lote de troca-de-idioma, os predicados de busca byte-level. Método delega ao builtin;
`teko.c` muda nos call-sites; fixpoint prova.

## Where

- `teko::str::ends_with` / `contains` / `last_index_of` / `starts_with` em `src/**.tks`. Localizar por
  `grep -rn 'teko::str::\(ends_with\|contains\|last_index_of\|starts_with\)' src --include=*.tks`.

## How

1. `teko::str::ends_with(s, x)` → `s.ends_with(x)`; idem `contains`/`starts_with`.
2. `teko::str::last_index_of(s, x)` → `s.last_index_of(x)` (retorno `u64 | error` preservado).
3. Fixpoint como guard.

## Rulings & laws

- **Teko-only.** **D145 fixpoint-safe.** **Behavior-preserving.** **W15; no `//`.**
- **Safety:** nunca `teko test .`; subshell `ulimit -v 4718592`; build gen2 + `gen2==gen3`; SEM
  reseed (acumulado em TY-T1). Ratchet: pico ~flat.

## Fixtures

`none — the fixpoint self-build exercises this`.

## Gate

`[fixpoint]` — build gen2 + `gen2==gen3` byte-idêntico. "Green" = predicados como método, fixpoint
fecha. Reseed-class: `none`.

## Deps

`TY-M1`

## Done when

Zero call-site direto dos predicados na árvore e `gen2==gen3`.
