---
seq: 0200
crumb-id: EX-B1
milestone: M3
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [EX-A0, S16-HOSTINFO]
sources:
  - "docs/design/expurgo-intrinsecos-inlines-0.3.1.md:2"      # §2 cruzamento §16
---

# 0200 · EX-B1 — remover name-detect de host-info (os/arch/version) após corpo §16

> `os`/`arch`/`version` emitem C `tk_rt_os`/`tk_rt_arch`/`tk_rt_version` (codegen 3842–3844) — **sem
> superfície Teko**. Grupo B: o corpo é **NOVO** e é do §16 (string estática por (os,arch)). Este crumb
> só REMOVE o name-detect DEPOIS que o §16 escrever `teko::runtime::os/arch/version`. **BLOQUEADO no §16.**

## Goal (design-ahead)

Contrato contra a superfície DECLARADA pelo §16: `exp fn os(): str`, `exp fn arch(): str`,
`exp fn version(): str` (strings estáticas resolvidas em compile-time por (os,arch)). Quando o §16 landar
o corpo, remover os 3 ramos (codegen 3842–3844) e o espelho `builtin_host_info_symbol` (lower 2086).

## Where

- `src/codegen/codegen.tks:3842-3844` — remover após §16.
- `src/lir/lower.tks:2086` — `builtin_host_info_symbol` — remover após §16.
- **REPORTE (adjacente):** o §16 ainda NÃO tem crumb dedicado a host-info como superfície → reportar ao
  dono para o §16 absorver (não criar issue). Este crumb é o consumidor da remoção.

## How

1. **Bloqueado:** aguardar `teko::runtime::os/arch/version` com corpo (§16).
2. Remover name-detect das duas rotas; fixpoint (emissão muda de `tk_rt_os` → símbolo Teko) → reseed.
3. ASan + reseed.

## Rulings & laws

- **Teko-only. D161. §16 (libc→Teko). D148 (par C+native).**
- **Achado adjacente REPORTADO, não vira issue** (host-info surface ainda sem crumb §16).
- **W15; sem `//`.**
- **Safety:** `ulimit -v 4718592`; `gen2==gen3` + ASan+UBSan (D166); ratchet D68; reseed (D164).

## Fixtures

`none — the fixpoint self-build exercises this` (o compilador reporta os/arch/version).

## Gate

`[fixpoint]` — `gen2==gen3` + ASan+UBSan limpo. Reseed-class: `fixpoint-rebuild`.

## Deps

`EX-A0`, **§16 host-info (corpo, a criar)** — HARD BLOCK.

## Done when

Nenhum `tk_rt_os|tk_rt_arch|tk_rt_version` name-detect em codegen/lower e `gen2==gen3`.
</content>
