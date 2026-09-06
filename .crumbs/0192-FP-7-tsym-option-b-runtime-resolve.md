---
seq: 0192
crumb-id: FP-7
milestone: M5
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [FP-6]
sources:
  - "docs/design/fold-and-prune-por-passo-0.3.1.md:§5"
  - "docs/design/projecao-ast-entre-fases-0.3.1.md:606-610"    # v2 PJ-7
  - "src/codegen/codegen.tks"                                  # cast-loc / cobertura emit
---

# 0192 · FP-7 — (opcional) `.tsym` opção B: emitir `nid`, resolver em runtime

> O `.pdb` pleno: o codegen emite `_tk_cast_loc_nid = <nid>` (menor que line+col) e o runtime resolve
> `nid → path:line` via `.tsym` embarcado ao falhar um cast/assert. Encolhe o `teko.c` e o binário.
> MUDA a emissão deliberadamente → reseed. SÓ se o ganho de tamanho justificar.

## Goal

Depois de FP-1 (opção A, byte-preservante) e a jardinagem, opcionalmente trocar a emissão de posições
literais por um `nid` resolvido em runtime — o modelo `.pdb` completo. Remove `line`/`col`/paths
literais do `teko.c` e do binário, substituindo por um `nid` de 4 B + a tabela `.tsym` embarcada. É o
único crumb que MUDA o `teko.c` deliberadamente (os anteriores são byte-preservantes) → reseed + fixpoint
re-valida no NOVO formato. Só landa se o ganho de tamanho de binário justificar o reseed.

## Where

- `src/codegen/codegen.tks` (cast-loc/cobertura) — `_tk_cast_loc_line = <line>; _tk_cast_loc_col = <col>`
  → `_tk_cast_loc_nid = <nid>`.
- `src/runtime/` (resolvedor) — ao falhar cast/assert, abrir a `.tsym` embarcada (via `#embed`/VFS, D134)
  e resolver `nid → path:line` para a mensagem `arquivo:linha:coluna`.
- Empacotamento — embarcar a `.tsym` no binário (VFS read-only) para o resolvedor de runtime.

## How

1. Codegen emite `nid` no lugar dos literais de posição.
2. Runtime resolve via `.tsym` embarcada (reusa a infra `#embed`/VFS já landada — D134).
3. Reseed: o `teko.c` muda (emite `nid`) → harvest `gen2==gen3` no novo formato.
4. Shadow (D117): rodar um cast-fail no BASE e no NOVO, conferir a mensagem `arquivo:linha:coluna`
   idêntica (o resolvedor de runtime reproduz o texto que os literais davam).

## Rulings & laws

- **Teko-only (.tks); zero C (D148).** Reusa `#embed`/VFS (D134) para a `.tsym` embarcada.
- **R6 (fixpoint):** MUDANÇA DELIBERADA — o `teko.c` encolhe; o fixpoint re-valida no novo formato
  (gen2==gen3), NÃO contra o build anterior.
- **Shadow (D117):** a mensagem de runtime tem que reproduzir `arquivo:linha:coluna` byte-idêntico ao que
  os literais davam — validar num projeto avulso no scratchpad.
- **Opcional:** só landa se o ganho de tamanho de binário justificar o reseed (v2 §11 PJ-7). Dúvida →
  não landa (FP-1 opção A já entregou o ganho de memória residente).
- **W15 full Javadoc; no `//`.** **Safety:** never `teko test .`; subshell `ulimit -v 4718592`;
  `[RITUAL]` gen2==gen3 no novo formato. **Ratchet D68:** o alvo é tamanho de binário, não RSS de build.

## Fixtures

| fixture | asserts | expected |
|---|---|---|
| `tsym_runtime_resolve_message` | um cast-fail resolve `arquivo:linha:coluna` via `.tsym` embarcada, idêntico ao formato literal | `134` |

## Gate

`[RITUAL]` — **RESEED (novo formato):** `gen2==gen3` byte-idêntico no formato com `nid`; o shadow prova a
mensagem de runtime byte-idêntica. "Green" = o codegen emite `nid`, o runtime resolve via `.tsym`
embarcada, a mensagem é idêntica, o binário encolheu, o fixpoint fecha. Reseed-class: `fixpoint-rebuild`.

## Deps

`FP-6` (0191) — e a infra `#embed`/VFS (D134) para embarcar a `.tsym`.

## Done when

O `teko.c`/binário emitem `nid` em vez de posições literais, o runtime resolve `nid → path:line` via
`.tsym` embarcada com mensagem byte-idêntica, o binário encolheu e o fixpoint fecha no novo formato —
OU o crumb é registrado como não-justificado e não landa.
