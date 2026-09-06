---
seq: 0202
crumb-id: EX-B3
milestone: M3
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [EX-A0, S16-F1]
sources:
  - "docs/design/expurgo-intrinsecos-inlines-0.3.1.md:2"
  - "docs/design/plano-s16-expurgo-libc-completo.md:86"       # F1 floor + float-bits
---

# 0202 · EX-B3 — remover name-detect de float (fdiv/floor/f64_bits/f64_from_bits) após §16-F1

> `fdiv`/`floor`/`f64_bits`/`f64_from_bits` emitem C `tk_fdiv`/`tk_floor`/`tk_f64_bits`/`tk_f64_from_bits`
> (codegen 3768–3770, 3846). Grupo B = **§16 F1** (`math_floor_intrinsic` fixture S4; float-bits fundacao
> C5). Escalares (f64/bool) → below-line. **BLOQUEADO no §16-F1.**

## Goal (design-ahead)

Quando o §16-F1 landar `floor`/`round`/`ceil` como intrínseco de codegen SEM header (`__builtin_floor` ou
soft) e `f64_bits`/`f64_from_bits` como bitcast (fundacao C5), remover os name-detects que apontam ao C
`tk_*`. `fdiv` = divisão float checada → §16 F1 (fp `/`). Todos escalares → below-line.

## Where

- `src/codegen/codegen.tks:3768-3770, 3846` — remover após §16-F1.
- `src/lir/lower.tks:1265` — `is_f64_bitcast_call` (f64_bits/from_bits, já native-bitcast) + float paths.
- **Nota:** `f64_bits`/`f64_from_bits` no native JÁ são bitcast (`is_f64_bitcast_call`) — a rota-C que
  emite `tk_f64_bits` é o resíduo; §16-F1 (C5) dá a forma sem-C. `floor` NOVO no §16 (F1).

## How

1. **Bloqueado:** aguardar §16-F1 (floor/round/ceil intrínseco + float-bits C5).
2. Remover name-detect rota-C; native já tem bitcast (verificar `floor`/`fdiv` native).
3. Fixpoint + ASan + reseed.

## Rulings & laws

- **Teko-only. D161. §16-F1 (math/bits intrínseco sem header). D159 (escalar below-line). D148.**
- **W15; sem `//`.**
- **Safety:** `ulimit -v 4718592`; `gen2==gen3` + ASan+UBSan (D166); ratchet D68; reseed (D164).

## Fixtures

`none — the fixpoint self-build exercises this` (o numfmt usa floor/bits; §16-F1 traz o oráculo S4
`math_floor_intrinsic` — é do §16, não desta onda).

## Gate

`[fixpoint]` — `gen2==gen3` + ASan+UBSan limpo. Reseed-class: `fixpoint-rebuild`.

## Deps

`EX-A0`, **§16-F1 (corpo float/bits)** — HARD BLOCK.

## Done when

Nenhum `tk_fdiv|tk_floor|tk_f64_bits|tk_f64_from_bits` name-detect em codegen (rota-C) e `gen2==gen3`.
</content>
