---
seq: 0204
crumb-id: EX-B5
milestone: M3
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [EX-A0, S16-COV]
sources:
  - "docs/design/expurgo-intrinsecos-inlines-0.3.1.md:2"
  - "docs/design/plano-s16-expurgo-libc-completo.md:191"      # camada-2 tk_cov_dump
---

# 0204 · EX-B5 — remover name-detect de coverage (cov_*) após §16 camada-2

> 14 `cov_*` emitem C `tk_cov_*` (codegen 3783–3796): reset/mark/distinct/is_marked/branches_on/
> branch_reset/enter/leave/branch/branch_hit/lines_on/line_reset/line/line_hit. Grupo B = §16 camada-2
> (`tk_cov_dump` é o núcleo irredutível). **BLOQUEADO no §16.**

## Goal (design-ahead)

Quando o §16 (camada-2 runtime) escrever o subsistema de coverage em Teko (estado + mark/branch/line
sobre memória Teko, `tk_cov_dump` o único irredutível), remover os 14 name-detects. A maioria é
void/bool → below-line.

## Where

- `src/codegen/codegen.tks:3783-3796` — remover após §16.
- `src/lir/lower.tks:2007` — `builtin_cov_symbol` — remover após §16.

## How

1. **Bloqueado:** aguardar §16 coverage-em-Teko.
2. Remover os 14 name-detects das duas rotas.
3. Fixpoint + ASan + reseed.

## Rulings & laws

- **Teko-only. D161. §16 camada-2 coverage. D148.**
- **W15; sem `//`.**
- **Safety:** `ulimit -v 4718592`; `gen2==gen3` + ASan+UBSan (D166); ratchet D68; reseed (D164).

## Fixtures

`none — the fixpoint self-build exercises this` (o instrumentador de coverage é exercitado pelo `teko test`
no CI; o corpo é do §16).

## Gate

`[fixpoint]` — `gen2==gen3` + ASan+UBSan limpo. Reseed-class: `fixpoint-rebuild`.

## Deps

`EX-A0`, **§16 coverage (corpo camada-2)** — HARD BLOCK.

## Done when

Nenhum `tk_cov_*` name-detect em codegen/lower e `gen2==gen3`.
</content>
