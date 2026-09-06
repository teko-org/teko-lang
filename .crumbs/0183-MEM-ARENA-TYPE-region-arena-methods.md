---
seq: 0183
crumb-id: MEM-ARENA-TYPE
milestone: M5
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [MEM-W1]
sources:
  - "DECISION_LOG.md:1151"                                            # D149 arena = tipo com métodos
  - "DECISION_LOG.md:1158"                                            # D148 zero C + adendo root/parent/filhas
  - "docs/design/arena-region-tipo-com-metodos-0.3.1.md"             # o projeto (tipo Region/Arena, mapa das ~120 fns)
  - "src/runtime/arena.tks"                                           # as ~120 fns soltas a envolver
  - "src/base/base.tks:7,15"                                          # ptr/uptr (dep única de superfície)
  - "src/io/stream.tks:33-88"                                        # Buf = o padrão struct+métodos (static+instância)
---

# 0183 · MEM-ARENA-TYPE — ENSINA `Region`/`Arena` como tipos com métodos (delegam às fns soltas) — RESEED-1

> D149: a arena vira TIPO com MÉTODOS, não ~120 funções soltas sobre `u64` cru + offsets. Este crumb ENSINA
> a superfície (`exp type Region = struct { addr: uptr }` + `exp type Arena = struct { addr: uptr }` com
> todos os métodos), cada método DELEGANDO à fn solta atual — gen0 aprende a superfície ANTES de a migração
> (0184) usá-la. Nasce `child_sized` (a lógica sized-region que o W2/0157 precisava, agora método). Aditivo,
> zero call-site migrado → o `teko.c` emitido pelo compilador fica com a semântica inalterada. RESEED-1.

## Goal

Definir os dois tipos do projeto (§1 do doc) em `src/runtime/arena.tks`, cada método um DELEGADOR fino à
fn solta existente, de forma que a superfície nova exista e gen0 a aprenda, sem MIGRAR nenhum consumidor
(isso é 0184). `Region` = handle de região (`alloc`/`child`/`child_sized`/`drop`/`drop_subtree`/`register`/
`lookup`/`parent`/`root`/`control`/`free_block` + `Region::at`); `Arena` = bloco de controle
(`root`/`program`/`push`/`pop`/`commit` + slots environ/fd_stage/intern/names + `Arena::current`/`Arena::at`).
Adiciona os campos de layout D148 (`REGION_ROOT` + lista de filhas) ao bloco de região, aditivo. A superfície
de pânico/capture, wrap-refcount, OS-prims e `copy` FICAM como fn livre (§1.3 do doc).

## Where

- `src/runtime/arena.tks` — ADD `exp type Region = struct { addr: uptr }` + `exp type Arena =
  struct { addr: uptr }` com os métodos (static + instância) do doc §1.1/§1.2. Cada método = 1 linha
  delegando: `fn alloc(n: usize): ptr { region_alloc(word_of(self), n) }` onde `word_of(self)` empacota
  `self.addr` no `ptr`/`u64` que a fn solta espera. `Region::at(addr: uptr): Region { Region { addr = addr } }`.
- `src/runtime/arena.tks` (layout `REGION_*`, ~21-35) — ADD `REGION_ROOT` + `REGION_CHILDREN`/`REGION_SIB_NEXT`
  (aditivo; `REGION_BYTES` sobe ao próximo múltiplo de 16); `ar_region_new_w` grava `REGION_ROOT` (da chain do
  parent ou self quando parent==0) e liga a nova região na lista de filhas do parent. `root()`/`child()` leem
  esses campos. NÃO remove nenhum campo/fn existente.

## How

1. Adiciona os campos de layout D148 (`REGION_ROOT`/filhas) e popula-os em `ar_region_new_w` (aditivo; as
   fns soltas seguem funcionando byte-idênticas na semântica).
2. Define `Region`/`Arena` (structs de um campo `uptr`) + TODOS os métodos como delegadores finos às fns
   soltas (`alloc`→`region_alloc`, `child`→`region_new`, `drop`→`region_drop`, `control`→`region_control`,
   `Arena.push`→`arena_push`, …). O receptor é o 1º param sem tipo (`self`), padrão `Buf`.
3. Nasce `child_sized(floor: usize): Region` — abre uma filha pré-dimensionada a `floor` (o sized-region do
   W2): `ar_region_new_w` + primeiro chunk de `max(floor, DEFAULT_CHUNK)` via `ar_region_grow` sem entregar
   bytes (só semear o floor). ZERO C (D148) — tudo sobre as primitivas Teko de `arena.tks`.
4. NÃO migra nenhum consumidor (as fns soltas continuam sendo os alvos do codegen e dos call-sites). Build
   → gen0 aprende → RESEED-1.

## Rulings & laws

- **D149 direção RULED:** tipo+métodos. **D148 zero C:** nada de `teko_rt.c`/`.h`/`tk_*` novo; `child_sized`
  usa as primitivas Teko existentes. **D148 adendo:** `REGION_ROOT`+filhas, aditivo.
- **Struct-de-um-campo, NÃO newtype-sobre-builtin:** usa só a capacidade struct+método landada (`Buf`),
  desacopla de `types.tks` TY-C0 (doc §6 Fork-3; fallback newtype se houver quirk de lowering).
- **Método-sobre-struct é fixpoint-neutro no receptor** (`oop-this-base-static.md §0`): o codegen não casa
  o receptor por nome.
- **Ensino agora, uso depois (CLAUDE.md):** ENSINA a superfície inteira aqui; o USO (migração) é 0184 + o
  sweep. NÃO se defere ensino.
- **W15 full Javadoc; no `//`.** Doc só nos `exp` (os métodos exp de `Region`/`Arena`); delegadores privados
  e helpers descartam doc. **Safety:** never `teko test .`; subshell `ulimit -v 4718592`; `[RITUAL]` gen2==gen3.

## Fixtures

Zero fixture versionada — o self-build exercita a arena pesado (o fixpoint valida). Um round-trip avulso
não-versionado no scratchpad (`Region::at(a).alloc(n)` == `region_alloc(a,n)`) pode servir de sanity ao
implementer, descartado depois.

## Gate

`[RITUAL]` — **RESEED-1**: build gen2, `gen2==gen3` byte-idêntico (superfície aditiva, determinística),
MEM_PARANOID 0. "Green" = `Region`/`Arena` + métodos existem e compilam, delegam às fns soltas, `child_sized`
semeia o floor, os campos D148 populam, NENHUM consumidor migrado, `gen2==gen3`, `bootstrap/teko.c` reseedado.
Reseed-class: `fixpoint-rebuild`.

## Deps

`MEM-W1` (landado, 0156). `ptr`/`uptr` (base.tks, landado). Struct+método (landado).

## Done when

Os tipos `Region`/`Arena` com todos os métodos existem em `arena.tks` delegando às fns soltas, `child_sized`
nasce, os campos de layout D148 populam, nenhum call-site foi migrado, e o gate ritual é verde com gen0
reseedado aprendendo a superfície (RESEED-1).
</content>
