---
seq: 0189
crumb-id: FP-4
milestone: M5
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [FP-3]
sources:
  - "docs/design/fold-and-prune-por-passo-0.3.1.md:§2"
  - "docs/design/projecao-ast-entre-fases-0.3.1.md:596-600"    # v2 PJ-5
  - "src/build/project.tks:210-236"                            # checker → mono
---

# 0189 · FP-4 — drop na fronteira mono (parse/checker-scaffolding antes de mono)

> Subdivide o bracket: roda o checker numa Region própria, projeta `PreMono{prog,table}` para o pai, e
> derruba as árvores de parse + o scaffolding do checker ANTES de mono rodar — reclamando o pico entre
> checker e mono.

## Goal

Após FP-3 dropar pós-consteval, apertar a fronteira interna: em vez de manter parse + checker vivos até
o fim do bloco, dropar o working-set do checker (árvores de parse cruas não-embutidas + `Env`/escopos)
assim que `PreMono` está projetado — antes de `monomorphize` alocar suas instâncias. Reclama o delta
entre o muro de checker (~3140) e mono (~3407).

## Where

- `src/build/project.tks:210-236` `checked_program_of` — dividir o `garden_phase` único (FP-3) em DOIS
  brackets aninhados: (1) checker numa filha → projeta `PreMono{prog,table}` → dropa a filha do checker;
  (2) mono→comptime→consteval numa filha → projeta `inlined` → dropa (FP-3).
- `src/checker/gardening.tks` — `project_premono(pre, dest)` (projeta `prog` E `table`, ambos vivos p/
  mono), análogo a `project_program` mas preservando a `TypeTable` (mono a lê).

## How

1. `project_premono(pre, dest)`: deep-copy total de `pre.prog` E `pre.table` para `dest` (a `TypeTable`
   sobrevive a mono; só é dropada em FP-3, pós-consteval). Intern de string obrigatório (R2).
2. Bracket 1: `child_ck = program.child_sized(f1)`, `child_ck.enter()`, roda checker, `child_ck.leave()`,
   `pre2 = project_premono(pre, program)`, `child_ck.drop_subtree()`.
3. Bracket 2 (FP-3): roda mono→consteval sobre `pre2` numa filha, projeta `inlined`, dropa.
4. Validar `gardening_pred_guard_survives` (decl sob `#os(...)` projetada da região dropada filtra por
   plataforma) + `gardening_generic_instance_survives`.

```teko
/**
 * project_premono — deep-copy TOTAL de um PreMono (prog + table) para `dest`, quebrando o alias com a
 * região do checker. Diferente de project_program: PRESERVA a TypeTable (mono a lê); ela só é dropada
 * pós-consteval (FP-3). Intern de string obrigatório (R2, doc §3.3); copia só o `nid`.
 *
 * @param pre   a saída do checker (prog + table) na região do checker
 * @param dest  a região de sobrevivência onde o PreMono alias-free passa a viver
 * @return      um PreMono equivalente, alias-free em `dest`
 * @since 0.3.1
 */
fn project_premono(pre: checker::PreMono, dest: teko::runtime::Region): checker::PreMono
```

## Rulings & laws

- **Teko-only (.tks); zero C (D148).**
- **R2 (drop-safety):** o drop do checker só após `project_premono` copiar `prog` E `table` alias-free.
  A `TypeTable` referencia nós do `prog` → o deep-copy tem que reconciliar (copiar a table apontando
  para o `prog` COPIADO, não o original). Ponto sutil — o walk projeta table+prog juntos.
- **R6 (fixpoint):** projeção fiel → `teko.c` byte-idêntico; drop invisível à emissão.
- **PARANOID guard + fixpoint** pegam alias remanescente (esp. o cross-referência table↔prog).
- **Fork protocol:** nenhum novo.
- **W15 full Javadoc; no `//`.** **Safety:** never `teko test .`; subshell `ulimit -v 4718592`;
  `[RITUAL]` gen2==gen3 + PARANOID 0. **Ratchet D68: BAIXA (estrito)** — mede e reporta.

## Fixtures

| fixture | asserts | expected |
|---|---|---|
| `gardening_pred_guard_survives` | decl sob `#os(...)` (guard `Pred`) projetada da região dropada filtra por plataforma | `0` |

## Gate

`[RITUAL]` — **RESEED:** `gen2==gen3` byte-idêntico, PARANOID 0, `teko.c` idêntico ao pré-FP-4. "Green" =
o checker roda em filha própria, `PreMono` (prog+table) é projetado alias-free, a filha do checker é
dropada antes de mono, o fixture passa, e o RSS entre checker e mono CAIU. Reseed-class: `fixpoint-rebuild`.

## Deps

`FP-3` (0188 — o bracket `garden_phase` + drop pós-consteval).

## Done when

O working-set do checker (parse + `Env`/escopos) é reclamado por `drop_subtree` antes de mono, com a
`TypeTable` reconciliada no `prog` copiado, build verde e byte-idêntico, PARANOID 0, RSS caído.
