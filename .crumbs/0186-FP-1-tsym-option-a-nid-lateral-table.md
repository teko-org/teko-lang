---
seq: 0186
crumb-id: FP-1
milestone: M5
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [FP-0]
sources:
  - "docs/design/fold-and-prune-por-passo-0.3.1.md:§5"
  - "docs/design/projecao-ast-entre-fases-0.3.1.md:113-225"    # v2 §2 .tsym
  - "src/checker/tast.tks:8-9"                                  # line/col nos nós
  - "src/codegen/codegen.tks:9685"                             # tk_emit_tsym stub
---

# 0186 · FP-1 — `.tsym` opção A: `nid` na AST + tabela lateral, `teko.c` byte-idêntico

> Move as posições (`line`/`col`) da AST residente para uma tabela lateral estilo `.pdb`, deixando na
> árvore só um `nid` de 4 B. O codegen resolve `nid → line` no EMIT → `teko.c` byte-idêntico.

## Goal

Encolher a AST residente tirando a duplicação de `line`/`col` (8 B/nó copiados em ~4 fases → `nid` de
4 B + uma tabela única). Byte-preservante por REQUISITO: o codegen resolve `nid → line` no emit e emite
o MESMO `_tk_cast_loc_line = <line>`/cobertura → `teko.c` idêntico, sem mudar semântica. É o passo que
torna a jardinagem (FP-2+) mais barata (o deep-copy copia 4 B, não reconcilia posições).

## Where

- `src/checker/tast.tks:8-9,83-84,93` — `TExpr`/`TFunction`/`TConstDecl` (e demais nós com posição):
  `line: u32; col: u32` → `nid: u32`.
- `src/parser/*` (atribuição de `nid`) — no parse, contador monotônico por unidade em ordem de
  pré-visita; cada nó recebe `nid`.
- `src/checker/monomorph.tks`, `src/checker/consteval.tks` — toda reconstrução `line = e.line; col = e.col`
  → `nid = e.nid` (nós sintéticos herdam o `nid` do nó-origem, ou `nid = 0`).
- `src/codegen/codegen.tks` (cast-loc/cobertura) — `cb_u64_digits(buf, e.line)` →
  `cb_u64_digits(buf, tsym_lookup(t, e.nid).line)`.
- `src/codegen/codegen.tks:9685` — `tk_emit_tsym` (stub) — dar corpo: materializar a `TsymTable` no
  schema do doc (v2 §2.3), ordem crescente de `nid`, paths internados.

## How

1. Adicionar `nid` (parse) mantendo uma tabela `nid → (path,line,col)` acumulada por unidade.
2. Trocar `line/col`→`nid` nos nós; propagar `nid` nas reconstruções (mesmo padrão do `line = e.line`
   de hoje, agora 4 B).
3. Codegen resolve `nid → line` no emit via `tsym_lookup`; dar corpo ao `tk_emit_tsym`.
4. Validar `teko.c` byte-a-byte contra o build pré-FP-1 (opção A é byte-preservante por construção).

```teko
/**
 * tsym_lookup — resolve um id de nó (`nid`) para sua posição de fonte, o inverso de tsym_record. É o
 * caminho da opção A: o codegen o usa ao emitir `_tk_cast_loc_line`/cobertura, mantendo o teko.c
 * byte-idêntico; o runtime/LSP o usam para diagnósticos.
 *
 * @param t    a tabela `.tsym`
 * @param nid  o id de nó a resolver
 * @return     a posição (path, line, col), ou posição vazia se `nid == 0` (sintético)
 * @since 0.3.1
 */
fn tsym_lookup(t: TsymTable, nid: u32): TsymPos
```

## Rulings & laws

- **Teko-only (.tks); zero C (D148).** **`.tsym` opção A byte-preservante** (v2 §2.4/§7): `teko.c`
  idêntico, o reseed é só o rebuild-do-fixpoint (a AST slim muda a memória, não o C emitido).
- **Determinismo (R6):** `nid` em ordem de pré-visita; tabela ordenada por `nid`; paths internados por
  primeira-visita; sem `map`/`hashset`/endereço/timestamp no caminho.
- **NO-PUSHES:** a tabela `.tsym` é dimensionada por contagem de nós (1ª passada) + gravação por índice.
- **Fork protocol:** R-C (nid sintético) resolvido — herda o `nid` do nó-origem ou `nid=0`.
- **W15 full Javadoc; no `//`.** **Safety:** never `teko test .`; subshell `ulimit -v 4718592`;
  `[RITUAL]` gen2==gen3 byte-idêntico + identidade contra o build pré-FP-1. Ratchet D68: baixa (fatia
  de posições sai da árvore).

## Fixtures

| fixture | asserts | expected |
|---|---|---|
| `tsym_cast_loc_resolves` | um cast que estreita inteiro emite `_tk_cast_loc_line`/`col` corretos via `tsym_lookup(nid)` | `0` |
| `tsym_roundtrip_deterministic` | mesmo fonte → `.tsym` byte-idêntico em dois builds | `0` |
| `tsym_synthetic_node_no_pos` | nó sintético (nid=0) de mono/consteval resolve para posição vazia sem crash | `0` |

## Gate

`[RITUAL]` — **RESEED:** `gen2==gen3` byte-idêntico E `teko.c` idêntico ao build pré-FP-1 (opção A).
"Green" = a AST carrega `nid`, as posições vivem só na `.tsym`, o codegen emite os mesmos literais de
posição, os 3 fixtures passam, RSS baixa (fatia de posições fora da árvore). Reseed-class: `fixpoint-rebuild`.

## Deps

`FP-0` (0185 — o baseline que mede a fatia de posições).

## Done when

`line/col` saíram da AST residente (só `nid` de 4 B na árvore), a `.tsym` resolve posições no emit, o
`teko.c` é byte-idêntico, o fixpoint fecha e o RSS baixou.
