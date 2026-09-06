---
seq: 0187
crumb-id: FP-2
milestone: M5
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [FP-1]
sources:
  - "docs/design/fold-and-prune-por-passo-0.3.1.md:§3"
  - "docs/design/projecao-ast-entre-fases-0.3.1.md:378-404"    # v2 §6 drop-safety
  - "src/runtime/arena.tks:1224-1270"                          # Region métodos
  - "src/checker/tast.tks:95"                                  # TItem embute cru
---

# 0187 · FP-2 — `Region.enter/leave` + `project_program`/`intern_str_into` como transformação-IDENTIDADE

> A rede de segurança R2 ANTES de qualquer drop: um deep-copy TOTAL alias-free (com intern de string
> obrigatório) que projeta a saída de uma fase, mantendo a origem viva e SEM dropar ainda. Prova
> byte-idêntica de que a cópia é fiel.

## Goal

Construir a maquinaria de poda (doc §3, §6) e provar que ela copia TOTAL e fiel ANTES de habilitar
qualquer `drop_subtree` (FP-3+). Adiciona (a) os métodos `Region.enter/leave` (teaching mínimo, doc §3.2)
para o bracket dizer "as alocações caem nesta região"; (b) `project_program`/`intern_str_into` (novo
`src/checker/gardening.tks`) — deep-copy total que quebra todo alias, internando cada backing de string.
Inserido como passe IDENTIDADE (projeta, mantém origem viva, NÃO dropa) → o `teko.c` sai byte-idêntico,
provando a fidelidade da cópia. É o crumb que fecha o risco R2.

## Where

- `src/runtime/arena.tks:1224` (tipo `Region`) — NOVOS métodos `enter()`/`leave()`, delegadores finos
  às loose `region_enter`/`region_leave` (arena.tks:824/828).
- `src/checker/gardening.tks` (NOVO módulo) — `project_program`, `intern_str_into`, e o walk de
  deep-copy por variante de `TItem`/`TExpr` (ordem fixa de campo).
- `src/build/project.tks:204` `checked_program_of` — inserir UMA chamada `project_program(inlined, program)`
  como passe identidade (mantém origem viva; não dropa) para provar a cópia.

## How

1. Teaching: `Region.enter/leave` (copiar verbatim abaixo).
2. `intern_str_into(dest, s)`: aloca `s.len` bytes em `dest`, copia o backing, retorna `{ptr,len}` novo
   apontando para `dest`. É o primitivo LOAD-BEARING (sem ele o deep-copy é UAF no drop).
3. `project_program`: walk determinístico que reconstrói cada nó em `dest`, internando toda string,
   copiando o `nid` (4 B), PODANDO `doc`/`has_doc` (vira `docspan_none()`), deep-copy de `params`/`guard`.
4. Inserir como passe identidade em `checked_program_of` (origem NÃO dropada) → `teko.c` byte-idêntico.
5. Fixture-inversão `gardening_string_backing_copied`: uma projeção que copia o header mas NÃO o backing
   + drop DEVE dar SIGSEGV/UAF (prova que o intern é necessário).

```teko
/** enter — torna esta região o alvo de alocação corrente (empilha a anterior). */
fn enter() { region_enter(region_to_ptr(self.addr)) }

/** leave — restaura o alvo de alocação anterior a esta região. */
fn leave() { region_leave() }
```

```teko
/**
 * intern_str_into — interna uma string em `dest`, copiando o backing, de modo que o resultado não
 * aliase a região de origem. Primitivo de segurança do project_program (doc §3.3): sem ele o
 * deep-copy é incompleto e o drop_subtree vira UAF.
 *
 * @param dest  a região de sobrevivência (o backing é copiado para cá)
 * @param s     a string a internar (backing pode viver na região de origem)
 * @return      a string cujo backing vive em `dest`
 * @since 0.3.1
 */
fn intern_str_into(dest: teko::runtime::Region, s: str): str

/**
 * project_program — deep-copy TOTAL de um TProgram para `dest`, quebrando todo alias com a região de
 * origem e podando doc/has_doc/DocSpan. Copia só o `nid` (posições vivem na `.tsym`). Invariante
 * load-bearing (R2): ao retornar, nenhum ponteiro — inclusive backing de string — endereça a origem.
 * Determinístico (R6): ordem fixa de campo, sem map/hashset/endereço/timestamp.
 *
 * @param prog  o programa tipado a projetar (vive na região de origem)
 * @param dest  a região de sobrevivência onde a cópia alias-free passa a viver
 * @return      um TProgram equivalente, alias-free em `dest`, com doc podado
 * @since 0.3.1
 */
fn project_program(prog: checker::TProgram, dest: teko::runtime::Region): checker::TProgram
```

## Rulings & laws

- **Teko-only (.tks); zero C (D148).** Reusa `arena.tks` (`Region`); nenhum `teko_rt.c` novo.
- **R2 (drop-safety) — o coração do crumb:** intern de string OBRIGATÓRIO; projeção TOTAL; dúvida →
  copia (leak-safe, nunca UAF) — postura de `escape.tks`.
- **R6 (fixpoint):** passe identidade (origem viva) → `teko.c` byte-idêntico; `doc` podado é invisível
  ao codegen; walk determinístico.
- **NO-PUSHES:** `project_program` dimensiona os arrays por contagem exata + grava por índice.
- **Fork (doc §9):** o bracket usa `Region.enter/leave` AMBIENTE (não threada região-param nas fases) —
  recomendado passar (é o mesmo mecanismo do `open/close_native_region`); HALT só se o dono exigir
  região-param nas fases.
- **W15 full Javadoc; no `//`.** **Safety:** never `teko test .`; subshell `ulimit -v 4718592`;
  `[RITUAL]` gen2==gen3 + identidade contra pré-FP-2. Ratchet D68: ~flat (ainda não dropa; o custo é a
  cópia transitória, coberto por FP-3+).

## Fixtures

| fixture | asserts | expected |
|---|---|---|
| `gardening_string_backing_copied` | inversão R2: projeção que copia header de str mas NÃO o backing + drop DEVE dar SIGSEGV/UAF | `EXPECT_COMPILE_FAIL`/crash |
| `gardening_docspan_pruned` | após a projeção, o `.doc` de toda decl é `docspan_none()` | `0` |

## Gate

`[RITUAL]` — **RESEED:** `gen2==gen3` byte-idêntico E `teko.c` idêntico ao pré-FP-2 (passe identidade).
"Green" = `project_program`/`intern_str_into`/`Region.enter/leave` existem, o passe identidade roda,
`doc` é podado, o fixture-inversão prova o intern necessário, `teko.c` byte-idêntico. Reseed-class:
`fixpoint-rebuild`.

## Deps

`FP-1` (0186 — `.tsym`, para o deep-copy copiar só o `nid`).

## Done when

O deep-copy de projeção total (com intern de string) roda como passe identidade, poda `doc`, o
fixture-inversão confirma a necessidade do intern, e o `teko.c` é byte-idêntico — a rede de segurança R2
está provada ANTES de qualquer drop.
