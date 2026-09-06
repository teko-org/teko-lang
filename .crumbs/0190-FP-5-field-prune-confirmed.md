---
seq: 0190
crumb-id: FP-5
milestone: M5
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [FP-4]
sources:
  - "docs/design/fold-and-prune-por-passo-0.3.1.md:§2"
  - "docs/design/projecao-ast-entre-fases-0.3.1.md:602-604"    # v2 PJ-6
  - "src/checker/tast.tks:75-93"                                # campos das decls
---

# 0190 · FP-5 — poda de campo confirmada (`doc`/`type_constraints`), medir NENHUM-leitor antes

> Refina a projeção: OMITIR na cópia os campos que provadamente NENHUMA fase a jusante lê. `doc`/`has_doc`
> já podados em FP-2; aqui confirma-se e adiciona-se `type_constraints` (pós-mono) SÓ com prova de
> zero-leitor a jusante.

## Goal

Encolher cada nó copiado tirando trivia morta confirmada. `doc`/`has_doc` já saem em FP-2. `type_constraints`
(`TFunction`, `tast.tks:75`) é lida por mono (instanciação genérica) mas NÃO pelo codegen — logo podável
na projeção PÓS-mono (FP-3, bracket 2). Este crumb só landa a poda de `type_constraints` se um grep +
medição confirmarem zero-leitor no codegen/backend; dúvida → NÃO poda (leak-safe).

## Where

- `src/checker/gardening.tks` `project_program` (o do bracket pós-mono, FP-3) — omitir `type_constraints`
  na reconstrução do `TFunction` (vira `[]`), CONDICIONADO à prova de zero-leitor a jusante.
- Auditoria (não-código): grep `type_constraints` em `src/codegen/`, `src/lir/`, `src/emit/` — confirmar
  zero uso no build seco.

## How

1. Auditar: `type_constraints` tem leitor após mono? (mono já instanciou; codegen não filtra por
   constraint). Se grep vazio em codegen/lir/emit → podável.
2. Se confirmado: omitir na reconstrução pós-mono (FP-3 bracket 2). Se NÃO confirmado → só documenta e
   não poda (o crumb vira verify-only para `doc`).
3. Medir o RSS delta (FP-0) — a poda de campo é o menor ganho; só landa se medir queda ou flat-com-limpeza.

## Rulings & laws

- **Teko-only (.tks); zero C (D148).**
- **R-B (poda de `type_constraints`):** confirmar zero-leitor a jusante ANTES de podar; dúvida → não
  poda (v2 §12 R-B). NÃO barrar/detectar o inexistente (CLAUDE.md): se não há leitor, só não copia.
- **R6 (fixpoint):** campo não-lido pelo codegen → poda invisível ao `teko.c` (byte-idêntico).
- **NÃO DETECTAR/BARRAR O QUE NÃO EXISTE:** a poda é OMISSÃO na cópia, não um ramo de validação novo.
- **Fork protocol:** nenhum novo.
- **W15 full Javadoc; no `//`.** **Safety:** never `teko test .`; subshell `ulimit -v 4718592`;
  `[fixpoint]` gen2==gen3. **Ratchet D68:** baixa ou flat-com-limpeza (menor ganho).

## Fixtures

none — the fixpoint self-build exercises this (o `gardening_docspan_pruned` de FP-2 já cobre a poda de
doc; `type_constraints` é exercitada pela instanciação genérica do próprio self-build).

## Gate

`[fixpoint]` — `gen2==gen3` byte-idêntico. "Green" = a auditoria de zero-leitor está registrada, a poda de
`type_constraints` landa SÓ se confirmada (senão documenta e não poda), `teko.c` byte-idêntico, RSS
medido. Reseed-class: `fixpoint-rebuild` (se podar) / `none` (se só documentar).

## Deps

`FP-4` (0189 — a projeção pós-mono onde `type_constraints` seria omitida).

## Done when

`type_constraints` é podada na projeção pós-mono COM prova de zero-leitor a jusante (ou documentada como
não-podável), `doc`/`has_doc` confirmados podados, build verde e byte-idêntico.
