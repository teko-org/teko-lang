---
seq: 0191
crumb-id: FP-6
milestone: M5
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [FP-5]
sources:
  - "docs/design/fold-and-prune-por-passo-0.3.1.md:§4"
  - "DECISION_LOG.md:1155"                                      # D153 drena-AST-por-unidade
  - "src/build/project.tks:1201,2461"                          # backend / codegen_and_report
  - "src/codegen/codegen.tks"                                  # cg_find_decl cross-namespace
---

# 0191 · FP-6 — drena-por-unidade: codegen por-namespace + drop do CORPO pós-emit

> O segundo lever (D118/RM-C9): emite os corpos de função namespace a namespace e DROPA o corpo de cada
> fn logo após emiti-lo. As `TypeDecl`/`Param` (cross-lidas) sobrevivem; só o `body` (lido só pelo seu
> emit) é podado. Byte-preservante.

## Goal

Após a jardinagem por-fase (FP-3/FP-4) reclamar o scaffolding, atacar a retenção dos CORPOS no codegen:
hoje todos os `TFunction.body` de todos os namespaces ficam vivos até o fim da emissão. Como o corpo de
uma fn é lido SÓ pelo seu próprio emit (as `TypeDecl`/`UseDecl`/`Param` é que são cross-lidas por
`cg_find_decl`), o corpo pode ser dropado assim que emitido. O codegen passa a agrupar por namespace,
abrir uma Region-filha por namespace, emitir os corpos, e `drop_subtree` a filha — reclamando os corpos
daquele namespace. O buffer global (`cb`) e o gensym (`buf.len`) NÃO mudam (o body já virou bytes) →
byte-preservante, sem depender de RM-C10.

## Where

- `src/build/project.tks:1201` `backend` / `:2461` `codegen_and_report` — o loop de emissão passa a: (1)
  projetar `TypeDecl`/`UseDecl`/`Param` + assinaturas de TODAS as fns para a região de programa
  (survivor); (2) iterar namespace a namespace, cada um numa `program.child()`, emitir os corpos, dropar
  a filha.
- `src/checker/gardening.tks` — `project_decls_survivor(prog, dest)`: projeta só a superfície cross-lida
  (decls + assinaturas), deixando os corpos na região de origem para a drena por-namespace.
- `src/codegen/codegen.tks` — ordenar os items por namespace ANTES de emitir (ordem determinística,
  estável — não muda a ordem de emissão dentro do buffer, R6).

## How

1. `project_decls_survivor`: deep-copy das `TypeDecl`/`UseDecl`/`Param` + assinaturas de fn (sem corpo)
   para `dest`. Os corpos NÃO são copiados (ficam na origem, dropados por-namespace).
2. Agrupar `prog.items` por namespace preservando a ordem de emissão global (a ordem do buffer NÃO muda).
3. Loop: `child_ns = program.child()`, emitir os corpos das fns do namespace `ns` (grava no `cb` global),
   `child_ns.drop_subtree()`.
4. Provar `teko.c` byte-idêntico: a ordem de emissão no `cb` é a mesma; só a VIDA do body encurta.

```teko
/**
 * project_decls_survivor — projeta para `dest` a superfície CROSS-lida pelo codegen (TypeDecl/UseDecl/
 * Param + assinaturas de fn, sem corpo), que precisa sobreviver até o fim da emissão. Os corpos ficam
 * na região de origem, para serem dropados por-namespace após o emit (doc §4). Intern de string
 * obrigatório (R2).
 *
 * @param prog  o programa tipado final (typed slim)
 * @param dest  a região de programa (survivor até o link)
 * @return      um TProgram com decls+assinaturas alias-free em `dest`, corpos ainda na origem
 * @since 0.3.1
 */
fn project_decls_survivor(prog: checker::TProgram, dest: teko::runtime::Region): checker::TProgram
```

## Rulings & laws

- **Teko-only (.tks); zero C (D148).**
- **A restrição dura (doc §4):** `cg_find_decl` lê `TypeDecl`/`Param` CROSS-namespace → só o CORPO é
  podável por-namespace; decls/params sobrevivem. NÃO dropar decls (UAF cross-namespace).
- **R6 (fixpoint):** a ordem de emissão no `cb` global é preservada; o gensym (`buf.len`) não muda (o
  body já é bytes) → `teko.c` byte-idêntico. NÃO depende de RM-C10 (doc §4).
- **R2 (drop-safety):** o corpo só é dropado APÓS emitido; nada mais o referencia. PARANOID + fixpoint
  pegam erro de ordem.
- **Fork protocol:** nenhum novo.
- **W15 full Javadoc; no `//`.** **Safety:** never `teko test .`; subshell `ulimit -v 4718592`;
  `[RITUAL]` gen2==gen3 + PARANOID 0. **Ratchet D68: BAIXA** — mede e reporta (escala com o nº de fns).

## Fixtures

| fixture | asserts | expected |
|---|---|---|
| `drain_cross_namespace_decl_survives` | uma fn em ns A que usa um tipo de ns B emite correto após o drop dos corpos de A | `0` |

## Gate

`[RITUAL]` — **RESEED:** `gen2==gen3` byte-idêntico, PARANOID 0, `teko.c` idêntico ao pré-FP-6 (mesma
ordem de emissão). "Green" = o codegen emite por-namespace, dropa o corpo de cada namespace após emitir,
as decls cross-lidas sobrevivem, o fixture passa, e o RSS no codegen CAIU. Reseed-class: `fixpoint-rebuild`.

## Deps

`FP-5` (0190 — a projeção final slim de onde a superfície survivor é derivada).

## Done when

O codegen emite namespace a namespace dropando os corpos pós-emit, as `TypeDecl`/`Param` cross-lidas
sobrevivem até o link, `teko.c` byte-idêntico, PARANOID 0, e o RSS de codegen caiu.
