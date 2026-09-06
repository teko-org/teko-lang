# Arena/Região como TIPO com métodos (D149) — projeto

Design-only. Fia a arena de `src/runtime/arena.tks` — hoje ~120 funções soltas sobre
`u64` cru + offsets de campo (`ar_region_get(region, field)`, `ar_ctrl_get(control,
field)`, `ar_load(address)`, `region_new(parent: ptr)`) — em **dois tipos com
métodos** (`Region`, `Arena`), o padrão OO-like que o `types.tks`/`base.tks` já usa.
Causa-raiz que isto mata: o ponteiro-na-mão foi o que levou o W2 a reachar por C.

Leis de referência: D149 (direção), D148 (zero C; adendo root+parent+filhas), D130
(modelo por-escopo, refinamentos 1/2/3/5/6), D128 (arena é Teko mantido).

---

## 0. Verdade de base (verificada em file:line)

- **`arena.tks` é 100% Teko, VIVO** (`src/runtime/arena.tks`, 1095 linhas). Já usa `ptr`
  como tipo de superfície (`region_alloc(r: ptr, n: u64): ptr`). Nenhum `from "teko_rt"`
  de arena além de 3 acessores de slot de controle (`tk_arena_control_get/set/paranoid`).
- **`ptr`/`uptr` LANDADOS** em `src/base/base.tks` (`exp global type ptr = isize { }`,
  `exp global type uptr = usize { }`) — D139. **Este projeto depende SÓ do `base.tks`**
  (já na árvore), **NÃO** dos crumbs `types.tks` 0175-0182.
- **Método-sobre-struct LANDADO e maduro** (`src/io/stream.tks` `Buf`): tipo `struct`
  com métodos `static` e de instância; o receptor é o **primeiro parâmetro sem tipo**
  (convenção `self`); `recv.metodo(a)` **dessugariza para `Tipo::metodo(recv, a)` no
  typer ANTES do codegen** (`typer.tks:812-819`); o codegen nunca casa o receptor por
  nome (`codegen.tks:5950`) → **renomear/reempacotar o receptor é fixpoint-neutro**
  (`oop-this-base-static.md §0`). **Nenhuma capacidade nova é necessária.**
- **A emissão de arena do codegen** é por NOME de símbolo, via `CgArenaSym`
  (`codegen.tks:103-151`): 17 entradas (`RegionAlloc`/`RegionNew`/`RegionDrop`/
  `Alloc`/`ArenaPush`/… `cg_arena_kind_suffix`). É a ABI codegen↔runtime — o código
  compilado de TODO programa chama esses ~17 símbolos.
- **Superfície de call-site fora de `arena.tks`** (medida): `region_program`×10,
  `region_alloc`×10, `region_new`×3, `region_drop`×3, `region_current`/`enter`/`leave`
  ×1 cada — em `scope.tks`, `process.tks`, `env.tks`, `project.tks`, `lower.tks`,
  `codegen.tks`, `rtio.tks`. Migração de call-site escrito-à-mão ≈ 30 sítios; o grosso
  das ~120 fns é INTERNO a `arena.tks` (privados `ar_*` → helpers/métodos privados).

---

## 1. Os dois tipos

### 1.1 `Region` — um handle de região (o alocador por-escopo/por-objeto)

```
exp type Region = struct { addr: uptr }
```

`addr` é o handle: o endereço do bloco de controle de região de 64 bytes na memória meta
do arena (o mesmo `region: u64` de hoje, agora tipado). **É um tipo-VALOR de 8 bytes**:
copiar uma `Region` copia o handle (um ponteiro) — semântica correta de compartilhamento
(vários handles endereçam o mesmo bloco, como copiar um ponteiro).

Por que `struct { addr: uptr }` e **não** newtype `type Region = uptr { }`: o newtype-
sobre-builtin é a capacidade TY-C0 do `types.tks` (dep de 0175-0182). O struct de um
campo usa SÓ a capacidade struct+método já landada → **desacopla do `types.tks`**.
(Fallback, se o struct-de-um-campo-primitivo tiver quirk de lowering: newtype sobre
`uptr`, gated em TY-C0 — risco menor, §6.)

**Métodos de instância** (`self: Region`):

| método | assinatura | substitui |
|--------|-----------|-----------|
| `alloc` | `alloc(n: usize): ptr` | `region_alloc` / `ar_region_alloc_w` |
| `child` | `child(): Region` | `region_new(parent)` / `ar_region_new_w` |
| `child_sized` | `child_sized(floor: usize): Region` | **NOVO** (absorve o sized-region do W2) |
| `drop` | `drop()` | `region_drop` / `ar_region_drop_w` |
| `drop_subtree` | `drop_subtree()` | `region_drop_subtree` / `ar_region_drop_subtree_w` |
| `register` | `register(tid: u64, inst: ptr)` | `region_register` / `ar_region_register_w` |
| `lookup` | `lookup(tid: u64): ptr` | `region_lookup` / `ar_region_lookup_w` |
| `parent` | `parent(): Region` | `ar_region_get(r, REGION_PARENT)` |
| `root` | `root(): Region` | **NOVO** campo direto (D148 adendo — sem subir a cadeia) |
| `control` | `control(): Arena` | `region_control` (D130 — controle a partir de qualquer região) |
| `free_block` | `free_block(addr: ptr, n: usize)` | `free_block` |

**Métodos estáticos** (`Region::`):

| método | assinatura | substitui |
|--------|-----------|-----------|
| `Region::at` | `Region::at(addr: uptr): Region` | fronteira ABI/codegen (empacota um handle cru) |

**Helpers privados** (as internas `ar_*` de região viram método/helper privado do tipo,
`pub`, sem doc): `field_get(f)`, `field_set(f, v)`, `bump(off, size)`, `grow(size)`,
`release()`, `registry_unlink()`, `reaches(root)`, `entry_slot`/`entry_index_of`/
`entries_grow` (o registry de DI), `subtree_collect`.

**D148 adendo — layout do bloco de região ganha `root` + `children`:** hoje o bloco de
64 bytes tem `head`/`reg_next`/`parent`/`entries`/`nentries`/`entries_cap`/`gen`
(offsets `REGION_*`). Adiciona-se **`REGION_ROOT`** (ponteiro direto da root) e a lista de
filhas (`REGION_CHILDREN`/head-de-filhas ligado por um `REGION_SIB_NEXT`) — aditivo,
alarga o bloco (arredondar para o próximo múltiplo de 16). `root()` lê `REGION_ROOT`
direto; `child()` empurra a nova região na lista de filhas do pai. Reduz navegação-para-
descoberta (W5/W6 usam root/filhas). Consistente com o fat-region (D130 refinamento 5).

### 1.2 `Arena` — o bloco de controle (singleton por-tarefa)

```
exp type Arena = struct { addr: uptr }
```

`addr` é o handle do bloco de controle (`control: u64` de hoje). Alcançado de qualquer
`Region` via `r.control()` (D130: o controle é da root, nascida no `_start`).

**Métodos de instância** (`self: Arena`):

| método | assinatura | substitui |
|--------|-----------|-----------|
| `root` | `root(): Region` | `region_root` / `ar_region_root_w` |
| `program` | `program(): Region` | `region_program` / `ar_region_program_w` |
| `push` | `push()` | `arena_push` / `ar_checkpoint_push_w` |
| `pop` | `pop()` | `arena_pop` / `ar_checkpoint_pop_w` |
| `commit` | `commit()` | `arena_commit` / `ar_checkpoint_commit_w` |
| `environ` / `set_environ` | `environ(): uptr` / `set_environ(a: uptr)` | `environ_slot`/`set_environ_slot` |
| `fd_stage` / `set_fd_stage` | idem | `fd_stage_slot`/`set_fd_stage_slot` |
| `intern_table` / `set_intern_table` | idem | `intern_table_slot`/… |
| `intern_region` / `set_intern_region` | idem | `intern_region_slot`/… |
| `names_state` / `set_names_state` | idem | `names_state_slot`/… |

**Métodos estáticos** (`Arena::`):

| método | assinatura | substitui |
|--------|-----------|-----------|
| `Arena::current` | `Arena::current(): Arena` | `ar_control()` first-touch (RETIRADO por W6 → `r.control()`) |
| `Arena::at` | `Arena::at(addr: uptr): Arena` | empacota um handle de controle |

**Helpers privados** (as internas `ar_*` de controle): `ctrl_get(f)`/`ctrl_set(f, v)`,
`meta_alloc(bytes)`, `header_alloc()`/`header_free(r)`, `meta_slab_new(bytes)`, toda a
free-list (`free_bin_slot`/`free_is_binnable`/`freenode_get`/`free_purge`/
`free_take_binned`/`free_block_fits`/`free_take_large`/`free_take`/`free_park`/
`poison`/`free_block`), a wrap-table (`wrap_table`/`wrap_entry`/`wrap_find`/`wrap_inc`/
`wrap_remove`/`wrap_dec`/`wrap_alive`), as marcas (`mark_slot`/`write_mark`).

### 1.3 O que FICA como função solta (não é "arena como tipo")

- **Superfície de pânico/capture** (`capture_arm`/`disarm`/`deliver`, `panic_raise`,
  `panicking`, `take_panic`, `rt_exit`, `rt_exit_os`): é a superfície do subsistema de
  pânico, ortogonal ao alocador. Fica como `exp fn` livre; internamente lê
  `Arena::current()` (interino) / `region.control()` (pós-W6).
- **Refcount/weak** (`wrap_retain`/`wrap_release`/`wrap_weak_get`): o usuário RECEBE
  retain/release, não constrói a tabela (teste-de-`exp`, CLAUDE.md). Fica como `exp fn`
  livre delegando aos métodos privados de wrap-table do `Arena`.
- **Primitivas de OS** (`ar_mmap`/`ar_munmap`/`ar_oom`): helpers privados de módulo,
  antecedem qualquer região — não são método de tipo.
- **`copy(dst, at, src)`**: cópia de bytes, `exp fn` livre ortogonal.
- **`ret_dest`/`set_ret_dest`**: RETIRADO por W4 (o param carrega o destino).
- **`region_enter`/`region_leave`/`ar_cur_*`**: ambiente RETIRADO por W6.

---

## 2. Mapa das ~120 funções → responsabilidade (resumo por balde)

1. **load/store/align crus** (`ar_load`/`ar_store`/`ar_align_up`/`ar_align_down`) →
   helpers privados de módulo (candidatos a método de `uptr` no futuro; ficam privados).
2. **acesso ao controle** (`ar_ctrl_get`/`set`, `ctrl_get`/`set`, `ar_control`,
   `region_control`) → métodos de `Arena` + `Region.control()`.
3. **meta/header alloc** (`ar_meta_slab_new`/`ar_meta_alloc`/`ar_header_alloc`/`free`) →
   métodos privados de `Arena`.
4. **chunk** (`ar_chunk_*`, `chunk_node_link`) → helpers privados de `Region` (o chunk é
   armazenamento interno da região).
5. **região núcleo** (`ar_region_*`, `region_new`/`alloc`/`drop`/`drop_subtree`/`root`/
   `program`/`bump`/`grow`/`release`/`registry_unlink`/`reaches`/`subtree_collect`) →
   métodos de `Region` (exp + privados).
6. **current-stack ambiente** (`ar_cur_*`, `region_enter`/`leave`/`current`) → RETIRADO
   por W6; interino como privados de `Arena`.
7. **entries/DI** (`ar_entry_*`/`ar_entries_grow`/`region_register`/`lookup`) →
   `Region.register`/`lookup` + privados.
8. **marcas/checkpoint** (`ar_mark_*`/`ar_write_mark`/`ar_checkpoint_*`/`arena_push`/
   `pop`/`commit`) → `Arena.push`/`pop`/`commit`.
9. **free-list** (`ar_free_*`/`ar_poison`/`free_block`) → privados de `Arena` +
   `Region.free_block`.
10. **wrap refcount** (`ar_wrap_*`/`wrap_retain`/`release`/`weak_get`) → `exp fn` livres
    sobre privados de `Arena` (§1.3).
11. **slots** (`environ`/`fd_stage`/`intern`/`names`/`ret_dest`) → métodos de slot de
    `Arena`; `ret_dest` RETIRADO por W4.
12. **pânico/capture + OS + copy** → superfície livre (§1.3).

---

## 3. Escalonamento fixpoint-safe (2-3 reseeds — padrão ensino-staging D132)

A lei do fixpoint é **gen2==gen3 byte-idêntico** (o compilador se reproduz), NÃO
"diff-zero contra antes". Cada reseed é um harvest auto-consistente; a migração é uma
sequência de reseeds encenados, cada um determinístico.

### Passada 1 — ENSINA → RESEED-1 (crumb 0183)
Define `Region` + `Arena` com todos os métodos, **cada um DELEGANDO à fn solta atual**
(`fn alloc(n: usize): ptr { region_alloc(Region::at_ptr(self), n) }`, etc.). Adiciona os
campos de layout D148 (`REGION_ROOT`/filhas), aditivo. Nasce `child_sized` (a lógica
sized-region que o W2 precisava — agora método, não fn-solta-sobre-`u64`). gen0 não
conhece os tipos → o build ENSINA → **RESEED-1** (gen0 aprende tipo+métodos). O `teko.c`
ganha símbolos novos (os métodos), determinístico → gen2==gen3. **Zero call-site
migrado** → as chamadas de arena que o compilador EMITE ficam inalteradas.

### Passada 2 — MIGRA → RESEED-2 (crumb 0184, por LOTE)
Reescreve os call-sites escrito-à-mão loose→método, módulo a módulo:
- **Lote A — interno a `arena.tks`**: as auto-chamadas (`region_alloc`→`self.alloc`,
  `ar_region_new_w`→`self.child`, …).
- **Lote B — consumidores src/**: `scope.tks`, `process.tks`, `env.tks`, `project.tks`,
  `rtio.tks` (`region_program`→`arena.program()`, `region_alloc`→`r.alloc`, …).
- **Lote C — emissores** (`codegen.tks`/`lower.tks`): a emissão `CgArenaSym`. **Decisão
  recomendada (§6 Fork-1): MANTER os ~17 símbolos de entrada como delegadores finos**
  (ABI codegen↔runtime) — o codegen segue emitindo `region_alloc` etc., que agora são
  delegadores finos para os métodos. Byte-mover mínimo, ratchet-safe.

gen2==gen3 por lote → **RESEED-2**.

### Passada 3 — REMOVE → RESEED-3 (funde no 0184 se o lote fechar)
Deleta as fns soltas agora mortas; os privados `ar_*` dobram em método privado. O
conjunto de entrada-runtime remanescente (alvos do codegen, §6 Fork-1) é o mínimo
estável. **RESEED-3** = harvest do fixpoint da superfície colapsada. Funde com RESEED-2
se o Lote fechar limpo.

**Ensino vs uso (CLAUDE.md):** o ENSINO (tipos+métodos) inteiro cai na passada 1 (não se
defere). O USO (migração de call-site + retirada do ambiente) É o sweep W3-W6.

---

## 4. Reconciliação dos crumbs 0157-0161 (o sweep sobre o tipo)

O **W5 (0160) é LITERALMENTE "a região vira tipo fat"** — o núcleo deste projeto. A onda
arena-tipo (0183/0184) é o VEÍCULO que W3-W6 passam a consumir.

- **W2 (0157) — sized-region:** o "sized region-new" que o W2 passa a `region_slots` **É
  `Region.child_sized(floor)`**, nascido na passada 1 (0183). Resolve o D148 "W2 refaz":
  o refaz = W2 vira `Region.child_sized`, exigindo o tipo ANTES. A remoção de
  `#arena_size`/`#arena_depth` (parse/checker/codegen) segue como está.
- **W3 (0158) — residência por-escopo:** o array fixo de regiões vivas vira
  `[TK_REGION_STACK_CAP]Region` (era `[…]ptr`); `open_native_region`/`close_native_region`
  viram `parent.child_sized(region_slots)` / `region.drop()`; `scope_region_of` /
  `native_scope_region_of` retornam `Region`. O seletor N-níveis roteia o crescimento do
  acumulador para a `Region` declarante.
- **W4 (0159) — move-on-return:** o param de região é uma `Region`; um retorno tier-Caller
  constrói em `param.alloc(...)`; `set_ret_dest`/`ret_dest` (slots ambiente do `Arena`)
  RETIRADOS. `region_from_param(ctx)` retorna `Region`.
- **W5 (0160) — objeto dono da arena = O NÚCLEO:** o fat pointer do objeto carrega uma
  `Region` (o campo `uptr` = `Region.addr`). O fat-region do D130 refinamento 2 **É o tipo
  `Region`**. Alocação de membro = `object.region().alloc(...)`; o handle viaja com o
  objeto (é parte do valor). W5 deixa de ser "adicionar um campo u64 cru" e passa a ser
  "o objeto carrega uma `Region`". A arena que viaja dissolve o bloqueador de threads
  (D127): cada thread recebe sua `Region` por param.
- **W6 (0161) — root no `_start` + reball + remove ambiente = RESEED-FINAL:** `_start` abre
  a root `Region` e passa a `main` como param; `Arena::current()` first-touch RETIRADO →
  `region.control()`; `region_enter`/`leave`/`ar_cur_*` removidos. O reball
  (posições→`usize`, palavras cruas→`ptr`/`uptr`, `str`↔`[]byte` zero-copy) já está
  EMBUTIDO nos tipos (`addr: uptr`) — a fatia de arena do reball é SUBSUMIDA por adotar os
  tipos. Provenance (0171) antes deste reball (toca nomes reservados str/[]byte).

**Ordem final do sweep:** W1 (landado, 0156) → **0183 ensina (RESEED-1)** → **0157 W2-redo
= `child_sized` + 0184 migra (RESEED-2)** → W3 (0158) → W4 (0159) → **W5 (0160, núcleo)** →
W6 (0161, RESEED-FINAL).

---

## 5. Ordem entre esta onda, o `types.tks` (0175-0182) e o resto do sweep

- `ptr`/`uptr` **já landaram** em `base.tks` (D139). A onda arena-tipo precisa SÓ de
  `base.tks` (na árvore) + struct-método (landado). **NÃO bloqueia em 0175-0182.**
- `types.tks` (0175-0182 — TODOS os tipos como superfície-com-métodos) roda em **trilha
  própria/paralela**, gated só por `base.tks` (landado) + provenance PV-C1/0171 para o
  próprio reball dele. Os NOMES `Region`/`Arena` são novos (não reservados) → a onda
  arena-tipo **não** depende do gate de provenance (só o reball str/[]byte do W6 depende,
  0171 antes).
- Recomendado: (1) W1 (landado) → (2) **0183 arena-tipo ensina** → (3) **0157 W2-redo +
  0184 migra** → (4) W3 → W4 → **W5 (núcleo)** → W6 (RESEED-FINAL). `types.tks` em paralelo.

---

## 6. Forks (registrados; recomendação law-first — nenhum é HALT genuíno)

- **Fork-1 — alvo de emissão de arena do codegen.** MANTER os ~17 símbolos `CgArenaSym`
  como delegadores finos (ABI codegen↔runtime) **vs** virar o codegen para emitir os
  símbolos method-mangled (`Region__alloc`). A direção método está RULED (D149); esta é uma
  sub-decisão de ABI-de-símbolo. **Recomendo MANTER o conjunto mínimo de entrada como
  delegadores** (law-first: menor byte-mover, ratchet-safe; o nome do símbolo é interno,
  não superfície de usuário). Registro pro dono caso ele queira o expurgo total dos ~17
  também. Não é HALT.
- **Fork-2 — `wrap_retain`/`release`/`weak` e pânico/capture.** Método sobre objeto vs fn
  livre. O objeto não tem tipo Teko aqui (é `ptr`); o usuário RECEBE a semântica. **Recomendo
  fn livre delegando a privados de `Arena`** (teste-de-`exp`). Resolvido.
- **Fork-3 — `Region` struct-de-um-campo vs newtype-sobre-`uptr`.** Struct evita a dep
  TY-C0 do `types.tks`; se o lowering de struct-de-um-campo-primitivo tiver quirk, o
  fallback é newtype sobre `uptr` (gated TY-C0). **Recomendo struct**; risco menor, medido
  no RESEED-1. Não é HALT.
</content>
