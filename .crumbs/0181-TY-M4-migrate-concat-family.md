---
seq: 0181
crumb-id: TY-M4
milestone: M5
gate: "[fixpoint]"
reseed-class: "none"
deps: [TY-M3]
sources:
  - "docs/design/types-tks-prelude-forma3-0.3.1.md:5"        # §5 escalonamento
---

# 0181 · TY-M4 — migrar a família CONCAT (486 sites, o maior) para método

> `teko::str::concat` (486 sites) → `a.concat(b)` (binário) / `str::of(...)` (variádico). O maior
> lote, deixado por último após a ponte estar validada nas famílias menores. Behavior-preserving →
> fixpoint gen2==gen3. Sem reseed (acumulado no terminal).

## Goal

Converter os 486 `teko::str::concat` para o idioma-método. Dois casos:
`teko::str::concat(a, b)` (2 args) → `a.concat(b)`; `teko::str::concat(a, b, c, …)` (variádico) →
`str::of(a, b, c, …)` (o estático variádico) ou encadeamento `a.concat(b).concat(c)` conforme
legibilidade. O método/estático delega ao builtin `concat` → semântica idêntica.

## Where

- Todos os `teko::str::concat(...)` em `src/**.tks` (486). Localizar por
  `grep -rn 'teko::str::concat' src --include=*.tks`. Concentração: checker/codegen/build/lir.

## How

1. Binário `teko::str::concat(a, b)` → `a.concat(b)`.
2. Variádico `teko::str::concat(a, b, c, …)` → `str::of(a, b, c, …)` (estático, mapeia ao `concat`
   variádico de `teko_rt.tks:173`). Evitar O(n²) de encadeamento em pontos hot — preferir `str::of`.
3. Fazer em sub-lotes por módulo (checker → codegen → build → lir → resto) para revisão incremental,
   mas TODOS antes do fixpoint final do crumb.
4. Fixpoint como guard.

## Rulings & laws

- **Teko-only.** **D145 fixpoint-safe.** **Behavior-preserving.**
- **fmt.tks:497 nota:** concat variádico evita o O(n²) de encadeamento 2-arg — usar `str::of` nos
  pontos de múltiplas peças.
- **W15; no `//`.**
- **Safety:** nunca `teko test .`; subshell `ulimit -v 4718592`; build gen2 + `gen2==gen3`; SEM
  reseed (acumulado em TY-T1). Ratchet: pico ~flat (idioma, não novo dado).

## Fixtures

`none — the fixpoint self-build exercises this` (concat é onipresente no compiler-core).

## Gate

`[fixpoint]` — build gen2 + `gen2==gen3` byte-idêntico. "Green" = zero `teko::str::concat` como
call-site direto, fixpoint fecha. Reseed-class: `none`.

## Deps

`TY-M3`

## Done when

Zero `teko::str::concat` como call-site direto na árvore (só delegado interno) e `gen2==gen3`.
