---
seq: 0182
crumb-id: TY-T1
milestone: M5
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [TY-M4]
sources:
  - "docs/design/types-tks-prelude-forma3-0.3.1.md:6"        # §6 terminal
  - "DECISION_LOG.md:1151-1157"                              # D145 "funções soltas saem no fim"
---

# 0182 · TY-T1 — retirar o idioma-solto órfão + RESEED final da onda types.tks

> Fecha a onda: colhe o reseed acumulado dos lotes TY-M1..M4 e retira do compiler-core o que ficou
> ÓRFÃO (call-sites soltos zerados). NÃO remove a IMPL builtin (os métodos ainda delegam a ela); o
> colapso `Str`→Named + retirada dos intrínsecos é o TERMINAL owner-present (§6 do doc, fork §7-c) —
> FORA desta onda.

## Goal

Um único reseed harvest ao fim da migração (lei "limpeza primeiro, reseed só no fim, tudo junto").
Confirmar que os call-sites soltos das famílias migradas estão zerados, que o idioma-método é o
único usado no compiler-core, e colher `bootstrap/teko.c`. A superfície solta (`teko::str::*`
builtins) PERMANECE como delegado interno dos métodos — sua retirada exige o colapso de
representação, que é owner-present.

## Where

- Varredura final: `grep -rn 'teko::str::' src --include=*.tks` deve retornar só o INTERIOR dos
  métodos de `types.tks` (os delegados), zero em outros módulos.
- `bootstrap/teko.c` — reseed (harvest do fixpoint gen2==gen3).

## How

1. Confirmar zero call-site solto fora de `types.tks` para todas as famílias migradas
   (slice/predicados/char/concat).
2. Qualquer resíduo → converter (não deixar meio-migrado).
3. Build gen2, fixpoint gen2==gen3 byte-idêntico, medir pico (ratchet).
4. Colher `bootstrap/teko.c`. Deixar gen2/gen3 no scratchpad da worktree (evita rebuild no dreno).
5. Registrar na `.crumbs/EXECUTION-ORDER.md`: esta onda (0175-0182) roda ENTRE 0160 (W5) e 0161
   (W6); o 0161 ganha dep em TY-C2.

## Rulings & laws

- **Teko-only.**
- **D145:** "as funções soltas + intrínsecos mágicos saem no FIM" — o FIM do idioma (call-sites) é
  aqui; a retirada da IMPL/intrínsecos é o terminal owner-present (§6), NÃO neste crumb.
- **Reseed incondicional por agente que toca compiler-core (dono 2026-08-24).**
- **Reseed só no fim, tudo junto (dono 2026-08-18).**
- **W15; no `//`.**
- **Safety:** nunca `teko test .`; subshell `ulimit -v 4718592`; build gen2 + `gen2==gen3` + reseed.
  Ratchet D68: pico da onda inteira tem que fechar ~flat (idioma, não dado novo) — medir e reportar.

## Fixtures

`none — the fixpoint self-build exercises this`.

## Gate

`[RITUAL]` — build gen2 + `gen2==gen3` byte-idêntico + reseed genuíno + medição de pico. "Green" =
idioma-método único no compiler-core, fixpoint fecha, seed colhido, pico ~flat. Reseed-class:
`fixpoint-rebuild`.

## Deps

`TY-M4`

## Done when

Call-sites soltos zerados fora de `types.tks`, `bootstrap/teko.c` reseedado, `gen2==gen3`, pico
reportado, e a EXECUTION-ORDER registra a onda antes de W6.
