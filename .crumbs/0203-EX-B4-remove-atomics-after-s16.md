---
seq: 0203
crumb-id: EX-B4
milestone: M3
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [EX-A0, S16-SYNC]
sources:
  - "docs/design/expurgo-intrinsecos-inlines-0.3.1.md:2"
  - ".crumbs/0029-S16-SYNC-const-sync-abi-consts.md:0"
---

# 0203 · EX-B4 — remover name-detect de atomics (atomic_*_u32) após §16-SYNC

> `atomic_cas_u32`/`atomic_xchg_u32`/`atomic_add_u32`/`atomic_load_u32` emitem C `tk_atomic_*_u32`
> (codegen 3779–3782). Grupo B = §16-SYNC (0029). Escalares (u32/bool) → below-line. **BLOQUEADO no §16.**
> Atenção: atomics são **primitiva quase-irredutível** (instrução `lock`/`ldaxr`) — o corpo Teko é
> `syscall`-classe ou intrínseco-de-instrução declarado como superfície; verificar com o §16 se vira
> corpo-Teko ou CARVE-OUT surface-declarado.

## Goal (design-ahead)

Quando o §16-SYNC definir os atomics (corpo Teko sobre instrução ou carve surface-declarado como
`syscall`), remover o name-detect C `tk_atomic_*`. Se o §16 decidir carve-out (instrução irredutível),
então este crumb vira "dar surface-decl" (como EX-C1), não remoção-para-corpo.

## Where

- `src/codegen/codegen.tks:3779-3782` — remover/rerotear após §16.
- `src/lir/lower.tks` — atomic paths em `native_builtin_symbol` (localizar por `atomic_cas`).

## How

1. **Bloqueado:** aguardar §16-SYNC decidir corpo-Teko vs carve-instrução.
2. Se corpo: remover name-detect → genérico. Se carve: surface-decl (`__atomic_*`) + manter.
3. Fixpoint + ASan + reseed.

## Rulings & laws

- **Teko-only. D161 (carve = surface-decl, não name-detect escondido). §16-SYNC. D134 (chão irredutível).
  D148.**
- **Fork:** corpo-Teko vs carve-instrução é decisão do §16-SYNC — se ainda não deliberada lá, HALT curto
  ao chegar aqui.
- **W15; sem `//`.**
- **Safety:** `ulimit -v 4718592`; `gen2==gen3` + ASan+UBSan (D166); ratchet D68; reseed (D164).

## Fixtures

`none — the fixpoint self-build exercises this` (concorrência do compilador; o oráculo, se houver, é do §16).

## Gate

`[fixpoint]` — `gen2==gen3` + ASan+UBSan limpo. Reseed-class: `fixpoint-rebuild`.

## Deps

`EX-A0`, **§16-SYNC (0029) + corpo/decisão-carve atomics** — HARD BLOCK.

## Done when

`tk_atomic_*_u32` não é mais name-detect escondido (removido OU surface-declarado como carve) e `gen2==gen3`.
</content>
