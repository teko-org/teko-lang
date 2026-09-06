---
seq: 0176
crumb-id: TY-C1
milestone: M5
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [TY-C0]
sources:
  - "docs/design/types-tks-prelude-forma3-0.3.1.md:8"        # §8 re-fiação provenance
  - "DECISION_LOG.md:1240-1244"                              # D133 Forma-3
  - "DECISION_LOG.md:1159-1166"                              # D144 Fork-2 (corrige D139)
  - "src/checker/check_modules.tks:174-195"                  # gate por namespace (desvio)
  - "src/build/project.tks:329-362"                          # inject_runtime_prelude
  - "src/build/assemble.tks:174-247"                         # merge Item<-SourceFile
---

# 0176 · TY-C1 — provenance por INJEÇÃO (namespace→tag Base), corrige o desvio D139

> O gate de nome-reservado usa hoje `ns_is_base_provenance(ns)` = prefixo `teko::` (desvio D139).
> Forma-3 (D133/D144) quer a etiqueta de ORIGEM-POR-INJEÇÃO: uma flag `base` fiada de
> `inject_runtime_prelude` (Base) vs `discover` (User) até o `parser::Item`. Additiva para corpora
> válidos → fixpoint.

## Goal

Substituir o marcador namespace-string pela proveniência real: `SourceFile`/`Item` carregam
`base: bool`; `inject_runtime_prelude` marca `true` no prelúdio injetado, todo o resto `false`.
`check_reserved_type_redefs` testa `!item.base`. Dogfood: o `src/` do compilador é `base=false`
(User) e consome as defs Base injetadas → prova o gate contra si mesmo. Remove `ns_is_base_provenance`.

## Where

- `src/build/discover.tks:2-6` `SourceFile` — novo campo `base: bool`. `discover`/`dsc_walk` e o
  `main.tks` synthetic (`project.tks:412`) = `false`.
- `src/build/project.tks:357` `inject_runtime_prelude` — os `SourceFile` anexados = `base = true`.
- `src/build/assemble.tks:174` `AsmFileItems` — carregar `base` do `SourceFile`; `assemble.tks:191`
  e `:247` (`append_tagged_items`) — propagar para `parser::Item`.
- `src/parser/ast.tks:262` `Item` — novo campo `base: bool`. TODOS os construtores de `Item`
  (`assemble.tks:16,22,51,61-65`, `project.tks:398`) recebem `base` (default `false` para os
  sintéticos de uses/manifesto).
- `src/checker/check_modules.tks:179-195` `check_reserved_type_redefs` — trocar
  `!ns_is_base_provenance(prog.items[i].namespace)` por `!prog.items[i].base`; **remover**
  `ns_is_base_provenance` (`:174-177`).

## How

1. `SourceFile` ganha `base: bool`; atualizar todos os literais de `SourceFile` na árvore
   (`discover.tks`, `project.tks`) — `false` exceto os do `inject_runtime_prelude`.
2. `parser::Item` ganha `base: bool`; atualizar todos os literais de `Item`.
3. `assemble` fia `sf.base` → `AsmFileItems.base` → `Item.base` no merge.
4. Gate passa a `!prog.items[i].base`. Remover `ns_is_base_provenance`.
5. Verificar que o `src/` (User) NÃO redefine reservado hoje: `ptr`/`uptr` estão em `base.tks`
   (namespace `teko::base`) que É injetado como Base → seguem legítimos. Qualquer redef residual
   em User vira erro real (limpeza que este crumb revela).

## Rulings & laws

- **Teko-only.**
- **D133/D144 Forma-3:** marcador é ORIGEM (Base-vs-User por injeção), NÃO nome-de-projeto, NÃO
  namespace-string, NÃO flag de build. Supersede o `ns_is_base_provenance` (desvio D139).
- **Ortogonal ao `<project-name>::`** (asset-namespacing, D135).
- **NÃO detectar o inexistente:** o gate só dispara em redef real de nome reservado por User.
- **W15 full Javadoc; no `//`.**
- **Safety:** nunca `teko test .`; subshell `ulimit -v 4718592`; build gen2, `gen2==gen3` (corpora
  válidos inalterados). Ratchet: check aditiva → pico não cresce.

## Fixtures

| fixture | asserts | expected |
|---|---|---|
| `types_user_redef_str` | projeto User `type str = []byte { … }` (Base define `str`) | `EXPECT_COMPILE_FAIL` |
| `types_user_own_newtype_ok` | projeto User `type mystr = []byte { … }` (nome livre) | `0` |

## Gate

`[fixpoint]` — build gen2 + os dois oráculos + `gen2==gen3` (corpora válidos byte-idêntico).
"Green" = User redefinindo nome reservado erra por `base`, User newtype livre passa, o `src/`
(User) compila contra as defs Base injetadas. Reseed-class: `fixpoint-rebuild`.

## Deps

`TY-C0`

## Done when

`Item`/`SourceFile` carregam `base`, o gate usa `!item.base`, `ns_is_base_provenance` some, os
oráculos passam e `gen2==gen3`.
