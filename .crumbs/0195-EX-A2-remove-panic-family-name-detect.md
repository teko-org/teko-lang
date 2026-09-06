---
seq: 0195
crumb-id: EX-A2
milestone: M3
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [EX-A0]
sources:
  - "docs/design/expurgo-intrinsecos-inlines-0.3.1.md:1"      # §1.A A08–A12
---

# 0195 · EX-A2 — remover name-detect da família panic (noreturn)

> `panic`/`panic_div0`/`panic_oob`/`panic_cast`/`panic_overflow` têm corpo (`rtio.tks:76`,
> `teko_rt.tks:239/242/245/248`). Noreturn → no DPS não há destino → abaixo-da-linha (D159); a msg
> interna do `panic` é throwaway (processo morre). Remove-se o desvio; emissão genérica ao mesmo símbolo.

## Goal

Remover os ramos `panic` (codegen 3758) e `panic_div0/oob/cast/overflow` (3804–3807) de `emit_call_inner`
e o espelho native. Chamada resolve via `call_ns` (EX-A0). Byte-idêntico salvo suffix → reseed.

## Where

- `src/codegen/codegen.tks:3758` — ramo `panic`.
- `src/codegen/codegen.tks:3804-3807` — ramos `panic_div0/panic_oob/panic_cast/panic_overflow`.
- `src/lir/lower.tks` — espelho: os símbolos panic em `native_builtin_symbol` (via `assert`/panic path;
  localizar por `panic_div0`).
- NÃO tocar os corpos (`rtio.tks`/`teko_rt.tks`).

## How

1. Remover os 5 ramos de name-detect (codegen).
2. Remover o espelho native correspondente.
3. Confirmar noreturn permanece below-line (D159): o guard `maypanic`/`cg_fn_is_below_line` já os
   classifica noreturn → sem região. `panic(msg)` recebe `str` mas retorna noreturn → não conduz destino.
4. Fixpoint + ASan + reseed.

## Rulings & laws

- **Teko-only.**
- **D161 / D159 (noreturn abaixo-da-linha) / D148 (par C+native).**
- **W15; sem `//`.**
- **Safety:** nunca `teko test .`; `ulimit -v 4718592`; `gen2==gen3` + ASan+UBSan (D166); ratchet D68;
  reseed (D164).

## Fixtures

`none — the fixpoint self-build exercises this` (o compilador chama `panic`/panic-guards nos caminhos de
erro do próprio checker; o self-build compila o corpo; casos de disparo são cobertos pelos oráculos de
rejeição existentes, não por esta onda).

## Gate

`[fixpoint]` — `gen2==gen3` + ASan+UBSan limpo. "Green" = zero name-detect de panic nas duas rotas.
Reseed-class: `fixpoint-rebuild`.

## Deps

`EX-A0`

## Done when

Nenhum `if last=="panic|panic_div0|panic_oob|panic_cast|panic_overflow"` em codegen/lower e `gen2==gen3`.
</content>
