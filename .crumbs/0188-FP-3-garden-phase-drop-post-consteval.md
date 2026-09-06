---
seq: 0188
crumb-id: FP-3
milestone: M5
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [FP-2]
sources:
  - "docs/design/fold-and-prune-por-passo-0.3.1.md:§3"
  - "docs/design/projecao-ast-entre-fases-0.3.1.md:590-594"    # v2 PJ-4
  - "src/build/project.tks:204-256"                            # checked_program_of
  - "src/runtime/arena.tks:1239,1247"                          # child_sized, drop_subtree
---

# 0188 · FP-3 — `garden_phase` + drop pós-consteval (TypeTable + região do checker)

> O golpe grande: rodar checker→mono→comptime→consteval numa Region-filha, projetar o `inlined` slim
> para o pai, e `drop_subtree` a filha — reclamando o scaffolding do checker + a `TypeTable` que dominam
> os ~3140 MB. A MAIOR queda isolada.

## Goal

Habilitar o primeiro DROP real, agora que FP-2 provou a cópia fiel. `garden_phase` roda o bloco
checker→consteval numa `program.child_sized(floor)`, projeta `inlined` para a região de programa
(alias-free, doc podado) e derruba a filha inteira via `drop_subtree` — reclamando o `Env`/escopos/caches
do checker E a `TypeTable` (morta: `backend` recebe só `prog`, `project.tks:2461`). O pico deixa de somar
o scaffolding do checker ao codegen.

## Where

- `src/build/project.tks:204` `checked_program_of` — envolver o corpo (checker→mono→comptime→consteval)
  no bracket `garden_phase(program, run)`: `child = program.child_sized(floor)`, `child.enter()`, roda,
  `child.leave()`, `out = project_program(inlined, program)`, `child.drop_subtree()`, retorna `out`.
- `src/checker/gardening.tks` — `garden_phase` (copiar verbatim abaixo).

## How

1. `garden_phase(parent, run)`: abre `child = parent.child_sized(floor)`, `child.enter()`, executa `run`
   (produz `inlined` na filha), `child.leave()`, `out = project_program(inlined, parent)`,
   `child.drop_subtree()`, retorna `out`. Se `run` erra, ainda dropa a filha (sem projetar).
2. Substituir o corpo de `checked_program_of` para retornar `garden_phase(program, () => <pipeline>)`.
3. `floor` dimensionado pela contagem de items × tamanho médio (aproveitar o `region_slots` sizing já
   landado; dúvida → floor maior é seguro, o excedente é reclamado no drop).
4. Validar `gardening_typetable_dropped` (build verde após o drop) + RSS pós-drop medido (FP-0).

```teko
/**
 * garden_phase — o BRACKET de jardinagem: roda `run` numa Region-filha própria, projeta sua saída
 * para `parent` (de sobrevivência) e derruba a filha (drop_subtree), reclamando todo o scaffolding
 * da fase (Env, escopos, tabelas) que a projeção não copiou. É o modelo por-escopo (D130) aplicado na
 * FRONTEIRA DE FASE; o drop só é seguro porque project_program quebrou todo alias (doc §3.3).
 *
 * @param parent  a Region de sobrevivência onde a saída projetada passa a viver
 * @param run     a fase a executar (produz um TProgram na Region-filha)
 * @return        a saída da fase, projetada e alias-free em `parent`
 * @throws        propaga o erro de `run` (a filha é dropada antes de retornar)
 * @since 0.3.1
 */
fn garden_phase(parent: teko::runtime::Region, run: fn(): checker::TProgram | error): checker::TProgram | error
```

## Rulings & laws

- **Teko-only (.tks); zero C (D148).** Reusa `Region.child_sized`/`drop_subtree` (0183/0184).
- **R2 (drop-safety):** o drop só ocorre APÓS `project_program` (FP-2, provado). O PARANOID guard +
  fixpoint pegam qualquer alias remanescente — divergência = PARA e acha o alias, NÃO maquia.
- **R6 (fixpoint):** a projeção é fiel (FP-2) e o `doc` podado é invisível ao codegen → `teko.c`
  byte-idêntico; o drop não muda a emissão.
- **R-A (2× transitório):** a cópia paga 2× a árvore num instante; o ganho (reclamar o scaffolding)
  domina. Se estourar o guard `ulimit -v 4718592`, achar a causa (não subir o teto — CLAUDE.md).
- **Fork protocol:** nenhum novo (o fork ambiente foi resolvido em FP-2).
- **W15 full Javadoc; no `//`.** **Safety:** never `teko test .`; subshell `ulimit -v 4718592`;
  `[RITUAL]` gen2==gen3 + PARANOID 0. **Ratchet D68: BAIXA (estrito)** — esta é a queda; medir o pico
  e reportar.

## Fixtures

| fixture | asserts | expected |
|---|---|---|
| `gardening_typetable_dropped` | a `TypeTable` é liberada após consteval; build verde | `0` |
| `gardening_generic_instance_survives` | fn genérica instanciada, cujo genérico vivia na região dropada, emite corpo correto | `0` |

## Gate

`[RITUAL]` — **RESEED:** `gen2==gen3` byte-idêntico, PARANOID 0, `teko.c` idêntico ao pré-FP-3 (a poda é
invisível à emissão). "Green" = o bloco checker→consteval roda em Region-filha, `inlined` é projetado
alias-free, a filha é dropada, os 2 fixtures passam, e o **RSS pós-drop CAIU** (medido via FP-0).
Reseed-class: `fixpoint-rebuild`.

## Deps

`FP-2` (0187 — `project_program`/intern provados fiéis + `Region.enter/leave`).

## Done when

O scaffolding do checker + a `TypeTable` são reclamados por `drop_subtree` após consteval, o build fecha
verde e byte-idêntico, o PARANOID é 0, e o RSS pós-drop caiu — a maior queda isolada da campanha.
