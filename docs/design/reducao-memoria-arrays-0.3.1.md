# Redução do pico de memória da build — de +6 GB para ≤1,5 GB

Base de leitura: `feat/expurgo-arrays` @ `df63f88c`. Autor: arquiteto (só design; não
toca código de produto). Este documento é o **crumb sequence** que o implementer de
continuação executa, escrevendo **apenas Teko**.

Meta dura: pico da build de ~6,2 GB (perto do guard `ulimit -v 6815744` = 6,5 GiB) para
**≤1,5 GB**. O guard é INVIOLÁVEL: estouro = achar causa-raiz do consumo, NUNCA levantar
o teto.

---

## 1. Diagnóstico — as duas fontes do pico

O profiler `tk_obs` já dividiu o pico:

| Fonte | Peso | Natureza |
|---|---|---|
| `tk_slice_push_r` (copy-grow amortizado) | **4980 MB (93%)** | 20,3 M cópias-crescimento que **vazam na arena `root`** (nunca liberada) |
| Construção da AST + scratch residual | ~1,2 GB (7%) | dados **vivos** (não vazados) + scratch por-arquivo que também cai em `root` |

A conta que governa o design: cada `push` num loop é copy-grow **amortizado** — a cada
dobra, o buffer anterior é abandonado na arena `root`, que nunca é liberada até o fim do
processo. Um loop que empurra `n` elementos deixa para trás `O(n)` bytes de buffers
mortos além do buffer final. Multiplicado por 20,3 M crescimentos → 4980 MB de lixo vivo.

**Três alavancas independentes, todas construíveis 100% em Teko:**

- **Eixo A — matar o crescimento** (as 4 naturezas): converter cada `push` para pré-alocação
  de tamanho exato + escrita por índice. Elimina a cópia-crescimento na origem. É o
  golpe que derruba os 93%. (Crumbs C3–C5.)
- **Eixo B — reclamar o scratch** (arena-por-escopo, já pronta em `src/runtime/arena.tks`):
  o que ainda se aloca transitório é reclamado em massa na saída de escopo, em vez de
  vazar em `root`. Ataca o residual. (Crumbs C6–C8.)
- **Eixo C — pipeline em estágios com despejo** (§6bis): processar por unidade, linkar antes
  do checker, despejar memória entre estágios (arena-drop ou artefato em disco). O pico vira
  o MÁXIMO de um estágio, não a soma. Ataca SÓ o residual não-push. Crumbs C10–C16 (terminal native = endgame).

---

## 2. A fundação já pronta em `df63f88c` (NÃO reconstruir)

O expurgo já construiu a maquinária aditiva. O implementer HERDA e USA:

| Peça | Estado | Sítio |
|---|---|---|
| `[n]T = []` (array fixo runtime, zero-fill) | pronto | `emit_slice_of_len` `src/codegen/codegen.tks:3167`; `TSliceOfLen` no checker |
| literal byte-string `b"abc"` → `[]byte` | pronto | commit `54097f6f` |
| cast implícito `str`↔`[]byte` (reinterpret, sem cópia) | pronto | commit `dc1cf767` |
| `ref []T` = escrita-por-posição `dst[i] = v` | pronto | commit `1bf3ec6c` |
| guard null-deref de slot zerado | pronto | commit `df63f88c` |
| **arena Teko completa** (mmap/syscall, regiões, chunks, free-list, marcas) | pronto | `src/runtime/arena.tks` |

A arena Teko (`src/runtime/arena.tks`) já expõe, em Teko puro sobre `mmap`/`munmap` via
syscall (Linux) / `VirtualAlloc` (Windows) / `mmap` FFI (macOS):

```
pub fn region_alloc(r: ptr, n: u64): ptr
pub fn region_new(parent: ptr): ptr
pub fn region_drop(r: ptr)
pub fn region_drop_subtree(r: ptr)
pub fn region_root(): ptr
pub fn region_current(): ptr
pub fn region_enter(child: ptr)
pub fn region_leave()
pub fn alloc(n: u64): ptr
pub fn arena_push()
pub fn arena_pop()
pub fn arena_commit()
```

O codegen já roteia todas essas por `CgArenaSym` (`codegen.tks:101`) e escolhe símbolo
Teko (`cg_arena_teko_sym`) ou C (`cg_arena_c_sym`). O `emit_slice_of_len` já aloca `[n]T`
via `RegionAlloc` na **região do escopo corrente** (`cg_enclosing_region_expr(regions)`).
**A arena-por-escopo já está fisicamente ligada ao codegen.** O que falta é (1) matar os
`push`, (2) fechar o loop de escopo (enter/leave nas fronteiras), e (3) expor o free
targetado para o purge-na-reatribuição.

Único resíduo em C: 3 shims `from "teko_rt"` (`tk_arena_control_get`/`_set`/`_paranoid`)
— o slot mutável de processo (P2 de `arena-em-teko.md`). Fora do caminho crítico de
memória; transcrição registrada no crumb 9 (não bloqueia a meta).

---

## 3. `arena_doc1` — o que a Doc-1 de arena delibera

`docs/design/arena-em-teko.md` (carga `cargo/20-arena-teko`) é a Doc-1. Ela projeta e
**já entregou** (em `src/runtime/arena.tks`) o arena em Teko: árvore de regiões
(`parent`), registro plano de regiões vivas (`reg_next`), chunks de 64 KB por bump,
free-list com bins (ceil no take / floor no park), e — decisivo aqui — o **checkpoint
mark/rewind** (`arena_push`/`arena_pop`/`arena_commit`) e o **enter/leave de região
corrente** (`region_enter`/`region_leave`).

A deliberação que resolve o free deste projeto: **arena-por-escopo com drop em massa
dispensa free individual** para o transitório. `region_drop_subtree` libera uma subárvore
inteira; `arena_pop` rebobina o bump da `root` até a marca. Para o scratch que nasce e
morre dentro de um escopo (a esmagadora maioria dos arrays do compilador), NÃO é preciso
liberar bloco a bloco — entra-se numa região/marca na entrada do escopo e derruba-se tudo
na saída. O free targetado (crumb 8) é reservado para o caso patológico: acumulador que é
reatribuído muitas vezes DENTRO de um escopo longo, onde esperar o drop-de-escopo ainda
piquearia.

---

## 4. `mem_model` — estimativa de queda por etapa

Baseline: **~6,2 GB**. `push` = 4980 MB; não-push ≈ 1,2 GB.

| Etapa (crumbs) | O que derruba | Pico após |
|---|---|---|
| Fundação (já em `df63f88c`) | — | ~6,2 GB |
| **C3 — buffer do codegen** (`cb`/`append_fo` → duas-passadas/spread) | o maior consumidor único em runtime; -3,0 a -3,5 GB | ~2,8 GB |
| **C4/C5 — checker+lir+build+parser** (4 naturezas) | resto das 20,3 M cópias; -1,4 a -1,8 GB | ~1,2–1,3 GB |
| **C6 — arena-por-escopo** (enter/leave + push/pop nas fronteiras) | reclama scratch que caía em `root`; -0,2 a -0,4 GB | ~0,9–1,1 GB |
| **C7 — literal de array via arena** (tirar `malloc` cru do `emit_array_lit`) | fecha vazamento de `malloc` nunca-liberado; -0,1 a -0,2 GB | ~0,9 GB |
| **C8 — purge-na-reatribuição** (free targetado eager) | fecha o pico do acumulador reatribuído em loop longo; robustez | ≤1,0 GB |
| **C10–C16 — pipeline em estágios + terminal native** (Eixo C, §6bis) | residual não-push: pico = máx de um estágio; e terminal native (endgame, sem C); -0,3 a -0,6 GB | ~0,5–0,7 GB |

**Dois orçamentos separados (não misturar):**
- **PUSH (93% = 4980 MB):** derrubado por **Eixo A** (C3+C4/C5, matar o crescimento). Esta é
  a maior parte da meta; sozinha leva o pico a ~1,2–1,3 GB.
- **RESIDUAL (~1,2 GB, não-push):** o programa inteiro (AST+typed+LIR+buffer) vivo junto.
  Derrubado por **C6** (arena-por-escopo, -0,2 a -0,4 GB) e **Eixo C** (pipeline em estágios,
  -0,3 a -0,6 GB). O Eixo C NÃO toca os 93% — só o residual; não superestimar.

O golpe principal são **C3+C4/C5**. Meta ≤1,5 GB é atingida já em C4/C5; C6 + Eixo C dão a
margem confortável (≤0,7 GB). Como o profiler mostra que a **AST crua não é dominante**, o
Eixo C é ganho de residual bounded — vale pelo TETO garantido (pico = um estágio), mais do
que pelo número absoluto.

---

## 5. `Como se constrói em Teko` (seção concreta — SÓ Teko, sem `teko_rt.c`)

### 5.1 Free targetado — o que a arena precisa expor

`ar_free_block(control, block, bytes)` já existe interno em `arena.tks` (park na
free-list com ceil/floor + modo paranoico). Falta **expô-lo**. Crumb 8 adiciona:

```teko
/**
 * Devolve um backing de array à free-list da arena Teko, para reuso imediato.
 *
 * @param p ponteiro-base do backing a liberar (0 = no-op)
 * @param bytes tamanho do backing em bytes (len * sizeof(elem))
 */
exp fn region_free(p: ptr, bytes: u64) {
    ar_free_block(ar_control(), teko::sys::ptr_word(p) to u64, bytes)
}
```

Não usa `teko_rt.c`: `ar_free_block` opera por `load`/`store` sobre memória crua vinda de
`mmap`. É a mesma família de alocador dos chunks. O purge-na-reatribuição (5.2) chama
ESTA função, nunca `tk_slice_*`.

### 5.2 Purge-imediato-na-reatribuição (`a = <novo>`) — o ponto de codegen

Regra do dono: ao reatribuir a variável-array (`a = <novo>`), o backing ANTERIOR é
purgado IMEDIATAMENTE (a variável-array POSSUI o backing; UAF é responsabilidade do dev
no design de uso, não do backend).

Ponto de emissão: `emit_assign` em `src/codegen/codegen.tks:6020`. Já existe o esqueleto
`checker::assign_frees_old(fn_body, a)` (hoje só para valor `TCall`/`list::push`, ramo
`@fo`). O crumb 8 **generaliza** o predicado do checker para "reatribuição cujo valor é um
backing novo E cuja variável possui o backing anterior" e o codegen passa a emitir, ANTES
do store:

```
{ ptr _old = <lvalue>.ptr; uint64_t _oldn = <lvalue>.len * sizeof(<elem>);
  <lvalue> = <novo backing>;
  teko::mem::region_free(_old, _oldn); }
```

O `_old` é capturado antes do overwrite (sequência correta). A chamada emitida é o
símbolo Teko `region_free` via novo `CgArenaSym::RegionFree` (roteado por
`cg_arena_teko_sym`) — **zero** referência a `tk_slice_push_*`.

**Qual reatribuição qualifica (predicado do checker, load-bearing):** só quando o backing
anterior é (i) uma alocação de arena fresca possuída pela variável (não um literal, não um
sub-slice `s[a..b]`, não um reinterpret `str`↔`[]byte`, não um parâmetro emprestado). Um
free de sub-slice ou de string reinterpretada corromperia o vizinho. Ver risco R2.

### 5.3 Arena-por-escopo — a rota que DISPENSA free individual

Para o transitório (a maioria): NÃO liberar bloco a bloco. Na fronteira de escopo:

- **Por-função** (scratch de codegen/lir por função): `region_enter(region_new(region_current()))`
  no prólogo emitido, `region_leave()` + `region_drop_subtree(<essa região>)` no epílogo.
  Todo `region_alloc` da função (o `emit_slice_of_len` já roteia para a região corrente)
  cai nessa região e é derrubado em massa no retorno.
- **Por-arquivo** (parse/lower scratch): `arena_push()` antes de compilar um arquivo,
  `arena_pop()` depois — rebobina o bump da `root` até a marca, devolvendo todo o scratch
  daquele arquivo. `arena_commit()` onde o resultado precisa sobreviver (a AST que segue
  para o próximo passo).

Isso já é fisicamente possível: `RegionFrame` + `cg_enclosing_region_expr` existem. O
crumb 6 fecha o loop emitindo enter/leave nas fronteiras certas.

### 5.4 Remoção das raízes C (código morto)

Após C3–C5 (nenhum `src/` chama mais o crescimento), REMOVER:

- de `src/runtime/teko_rt.c`: `tk_slice_push_r`, `tk_slice_push`, `tk_slice_push_fo`,
  `tk_slice_with_cap_r`, `grow_inplace`, `tk_push_cache*`, `tk_g_push_ra`;
- de `src/checker/scope.tks`: os builtins `append_fo`/`push_fo`/`with_cap`/`grow_inplace`;
- de `src/lir/lower.tks`: `is_append_fo_call`/`lower_append_fo_call`/`slice_push_symbol`/
  `lower_list_push` (paths `tk_slice_push*`);
- de `src/list/list.tks` e `src/collections/list.tks`: `push`/`empty`.

Metodologia "compilador enumera a limpeza": remover a RAIZ → o self-compile ERRA em cada
sítio remanescente → cada erro é um sítio a converter. NÃO caçar 4917 à mão.

### 5.5 O que precisa ser TRANSCRITO de C para Teko

Quase nada: `[n]T=[]` já emite `memset` inline; a cópia de array é loop por índice (ou
`mem::copy` em Teko). Os 3 shims de controle da arena (`tk_arena_control_get/set/paranoid`)
são o único resíduo `from "teko_rt"` — transcritos no crumb 9 (slot `.bss` via
`LGlobalAddr` ou `mmap MAP_FIXED`, o P2 de `arena-em-teko.md`). Fora do caminho de
memória; não bloqueia a meta. NENHUM novo `from "teko_rt"`.

---

## 6. `crumbs` — sequência ordenada (cada um gate-ável isolado)

Ordem obrigatória: **construir/converter ANTES de remover a raiz** (metodologia do dono).
Fixpoint `gen2==gen3` é a guarda a cada harvest; guard 6,5 GiB inviolável.

### C1 — Reconhecimento e shadow do buffer do codegen
Medir, com o `tk_obs`, a fração do pico atribuída ao `cb`/`append_fo` (o buffer de emissão
de C). Montar um `.tkr` shadow isolado (fora do OOM do `teko test .`) que emite um bloco
grande de texto pelas duas vias (append_fo velho vs. duas-passadas novo) e compara pico.
Baseline registrada. Nenhuma mudança em `src/` ainda.

### C2 — `mem::copy` e o idioma de junção por índice (aditivo)
Garantir em Teko o primitivo de cópia por índice `exp fn copy(dst: ref []byte, at: u64,
src: []byte)` (loop `dst[at+i]=src[i]`; sem crescimento) e o idioma "conta-total →
`[total]byte=[]` → copia por índice". Não remove nada. Seed.

### C3 — Converter o buffer do codegen (`cb`/`append_fo`) — o de 93%
Reescrever a emissão de C do codegen do idioma `out = cb(out, ...)` (append_fo encadeado)
para **peça = spread-literal** `b"…"` nos literais + `..str` nos dinâmicos, acumulando
`total` e materializando `var final: [total]byte = []` por índice. Alvos densos:
`emit_*` de `codegen.tks`. `cb`/`cb_str`/`cb_byte` viram finos sobre o idioma novo até
sumirem. Fixpoint a cada arquivo convertido. **Maior queda isolada.**

### C4 — Converter checker + build (4 naturezas)
`src/checker/*` (1615 sítios) e `src/build/*` (652): classificar cada `push` em MAP /
PARSE / FILTRO / BUFFER e converter (MAP→`of_len(fonte.len)`+índice; PARSE→duas passadas;
FILTRO→aloca o maior+`count`+corte `s[0..count]`). Redesenhar posse dos agregados de
aliasing (`Env` em `scope.tks`). Fixpoint por módulo.

### C5 — Converter lir + backend + parser + codegen residual
`src/lir/*` (877, incl. os 6 arrays paralelos de `LEnv`/`LowerCtx`), `src/backend/*` (633),
`src/parser/*` (293), resíduo de `src/codegen` (163). Mesmas 4 naturezas. Fixpoint por
módulo. Ao fim: nenhum `src/` chama mais o crescimento.

### C6 — Arena-por-escopo nas fronteiras (reclamar scratch)
Emitir `region_enter`/`region_leave`+`region_drop_subtree` por função e
`arena_push`/`arena_pop`/`arena_commit` por arquivo (5.3). Reclama o transitório que caía
em `root`. Fixpoint.

### C7 — Literal de array via arena (tirar `malloc` cru)
`emit_array_lit` (`codegen.tks`, ramo não-spread) hoje usa `malloc(...)` direto — vazamento
nunca-liberado. Rotear para `region_alloc` na região do escopo (como `emit_slice_of_len`).
Fixpoint.

### C8 — Free targetado + purge-na-reatribuição
Expor `exp fn region_free(p, bytes)` (5.1); generalizar `assign_frees_old` para
reatribuição-que-possui-backing (5.2); adicionar `CgArenaSym::RegionFree` e emitir o
free eager em `emit_assign`. Fixpoint.

**— Eixo C (pipeline em estágios com despejo, §6bis): eixo próprio, ataca o RESIDUAL não-push, paralelo a C6–C8; C9 é o passo terminal de tudo —**

### C10 — Determinizar gensym (pré-condição do Eixo C)
Trocar todo nome temporário derivado de `buf.len` (`_oln{buf.len}`, `_arr{buf.len}`, e
similares) por contador determinístico global ou (namespace, fn_idx, seq). Sem streaming
ainda; só remove a dependência do tamanho de buffer. Fixpoint: `teko.c` deve sair idêntico
(é rename mecânico de temporários — validar byte-a-byte). Bloqueia C11.

### C11 — Parse-por-unidade → AST incompleta → LINK (a FFI interna)
Reestruturar `frontend` para parsear por namespace produzindo a **AST incompleta** (decls
`exp`+`pub`: tipos/assinaturas/`const` + referências pendentes cross-unit), sem reter corpos;
e o LINK (barreira global) que monta a **FFI interna** — a tabela de link RICA (assinatura
Teko + símbolo + ABI, por decl `exp`+`pub`) que resolve as pendências e **alimenta o checker
de cada unidade**, descartada após o link. NÃO desenhar tabela só-ABI (o checker roda depois
do link e precisa dos tipos Teko). O `.tkh` (só `exp`) é emitido à parte, ortogonal. Reusar a
projeção de decls do `.tkb` (`emit/tkb_frame.tks`), estendida para incluir `pub`. Aditivo
(convive com whole-program). Fixpoint.

### C12 — Check+lower+emit FUNDIDOS por unidade (despejo em memória = C6)
Reestruturar `backend`/`codegen_and_report` para iterar namespaces em ordem determinística:
(re)carrega corpos da unidade, checa contra a FFI interna (C11), lowera, emite a
**saída-de-unidade** (abstrata: na rota C = pedaço de texto concatenado; na rota native =
`.o`), anexa/escreve e **derruba a região da unidade** (C6) antes da próxima. Prólogo global
sai da FFI interna; corpos streamed. **Guarda dura: `teko.c` byte-idêntico ao
whole-program.** Este é o despejo-em-memória do princípio unificador. Fixpoint a cada
namespace migrado.

### C13 — Dump typed `.tkb` por unidade (despejo em DISCO, onde o estágio não funde)
Estender `serialize_program`/`deserialize_program` (`emit/tkb_frame.tks`,`tkb_read.tks`) para
serializar/deserializar UM namespace. Onde o working set do estágio não cabe fundido (a
barreira do LINK precede os corpos), o typed de cada unidade é despejado em disco e relido um
por vez. Determinismo do frame obrigatório (R6). Fixpoint.

### C14 — Build incremental (opcional, DESLIGADO no self-build)
Cache do `.tkb` typed por-unidade em disco, chaveado por hash(unidade)+hash(tabela-linkada);
recompila só o que mudou. Caso persistente do despejo-em-disco (C13). Desligado no caminho de
fixpoint (build limpo = `teko.c` idêntico). Não reduz pico; reduz tempo de dev. Só depois de
C12/C13 verdes.

### C15 — Terminal native: objeto linkável POR UNIDADE (endgame)
Escrever o back-end de emissão por-unidade da rota native: cada namespace lowered
(`lower_program`/`lower_item`) → isel (`select_module*`) → `regalloc_module` →
`encode_module` → `emit_elf`/`emit_macho`/`emit_coff` produz um `.o` no disco; a região da
unidade é derrubada (o `.o` É o despejo). **Visibilidade → tabela de símbolos do `.o`:**
`exp`+`pub` = símbolo GLOBAL (visível ao `ld`/link cross-unit); privado = `static`/local
(nem entra na tabela). A tabela de símbolos do `.o` É a FFI interna no plano de linkagem —
`pub` alcança o `ld` sem vazar para o `.tkh`. O "linker próprio" (a FFI interna de C11)
resolve símbolos entre unidades; `ld` do SO (ou `objfile_ar`) junta no binário final. Saída
abstrata de C12 = `.o` aqui. Só depois de C12 verde na rota C.

### C16 — Fixpoint de objeto native + retirar a muleta C
Migrar o critério de fixpoint de `gen2.c==gen3.c` para **objeto native reproduzível**
(determinismo do `.o`: sem timestamp, ordem estável de símbolos/seções, sem path absoluto,
relocations canônicas — auditar `objfile_*`/`objfile_ar*`). Quando as 4 pernas native do CI
fecham verde + o objeto reproduz, **remover a rota C** (`teko.c` e o `cc`); as 2 pernas em C
viram native (triagem do CI). Reseed do bootstrap passa a ser o objeto/binário native.

### C17 — Emitir + empacotar o `.tkh` do próprio compilador
A build do executável final (rota C OU native) TAMBÉM emite o `.tkh` do compilador (a
superfície `exp` agregada, via `emit/tkh.tks`) e o **pacote entrega binário + `.tkh`**.
Backend-independente. Só `exp` embarca — a FFI interna (`exp`+`pub`) é transitória e NÃO vai
pro `.tkh` (R8). Sem impacto de memória; habilita features da próxima versão. Fixpoint.

### C9 — (TERMINAL) Remover raízes C + transcrever o slot de controle
Passo final, depois de TODAS as conversões (Eixo A e Eixo C). Remover o código morto de 5.4
(o self-compile enumera qualquer resíduo). Transcrever os 3
shims de controle da arena para Teko (slot `.bss`/`MAP_FIXED`), zerando `from "teko_rt"`
no caminho de array. Passe de mensagens unificado + reseed ITERATIVO final. Fixpoint
`gen2==gen3`.

---

## 6bis. Eixo C — pipeline em ESTÁGIOS com despejo (por unidade + linker interno + incremental)

Nota do dono: em vez de CARREGAR TUDO na memória (whole-program AST → check → codegen),
processar **por unidade**, linkando incrementalmente ANTES de emitir tudo, para segurar só
UMA unidade por vez. Este é um eixo próprio (C10–C16) que soma com o Eixo A (matar push) e
com C6 (arena-por-escopo) — todos liberam por fronteira.

**Refinamento do dono (apoiado pelo profiler):** como `tk_slice_push_r` = 93% do pico, a
**AST crua NÃO é o dominante** — então o alvo do Eixo C é o RESIDUAL não-push (~1,2 GB), e a
tática é emitir **ASTs INCOMPLETAS por unidade e LINKAR ANTES do CHECKER**, em vez de montar
a AST completa do programa inteiro antes de checar. O linker interno passa a sentar **entre o
parse e o checker**, não só antes do codegen.

### Estado atual (o modelo de pico)

Hoje o build é whole-program: `frontend_parse` parseia TODOS os arquivos → `expand_macros`
em tudo → `checked_program_of` checa tudo → `backend`/`codegen` itera `prog.items` (a lista
plana da AST inteira) e emite UM `teko.c`. O `checker::TProgram { items }` inteiro + todos
os corpos tipados + todo o LIR coexistem na memória. É esse "segurar tudo" que domina o
residual não-push (~1,2 GB).

### O pipeline novo: parse-unidade → AST incompleta → LINK → check/lower/emit-por-unidade

A linguagem é monólito com recursão mútua entre namespaces (o checker resolve nomes/tipos
cruzando namespaces) e cross-compila para UM `teko.c` (`#if` de todos os alvos). A fronteira
de unidade é de **processamento**, não de linkagem — o `teko.c` final continua UM arquivo.
Granularidade: **namespace** (arquivo é fino demais — corpos intra-namespace cruzam-se para
inline/const-fold; whole-program é o pico atual). O pipeline vira:

1. **Parse por unidade → AST INCOMPLETA.** Cada namespace é parseado para uma forma compacta
   que carrega SÓ o necessário ao link: as **declarações de linkagem** (tipos, assinaturas de
   fn, valores de `const`) de TODA decl `exp` **e** `pub` (não só `exp` — ver a FFI interna
   abaixo) e uma lista de **referências pendentes** (nomes cross-unit ainda não resolvidos).
   Os CORPOS (árvores de statement) NÃO ficam residentes — são adiados (descartados após
   extrair decls+pendências; recarregados por reparse da unidade quando ela chegar ao checker).
2. **LINK (o linker interno).** Junta as ASTs incompletas: monta a **tabela de link interno
   (a "FFI interna")**, resolve as referências pendentes contra as decls `exp`+`pub` das
   outras unidades. Compacto — assinaturas, não corpos. É a estrutura que vive o link e
   alimenta o checker; **some após o link** (não é embarcada; ver abaixo).
3. **Check + lower + emit POR UNIDADE (streaming).** Para cada namespace, em ordem
   determinística: (re)carrega os corpos daquela unidade, checa CONTRA a tabela de link
   interno, lowera, emite o C, anexa à saída e **derruba a unidade** (região de arena, C6).
   Só os corpos de UM namespace vivem por vez.

### O furo do `.tkh`-só-`exp` e a resolução: uma FFI INTERNA (ruling do dono)

Furo real: linkar cross-namespace SÓ pela interface exportada NÃO funciona no Teko, porque
**só `exp` alcança o `.tkh`** (lei de visibilidade, `tast.tks` M.4) e o compilador chama
MUITO símbolo `pub` (interno, cross-namespace) entre módulos. Se o link só visse o
`.tkh`/`exp`, seria preciso ou marcar quase tudo `exp` (fura a visibilidade + incha a
superfície) ou exportar tudo no `.tkh` (incha o header). Nenhum serve.

Resolução (ruling do dono): **separar duas tabelas ortogonais.**

- **`.tkh` = interface do USUÁRIO = só `exp`.** Enxuto, intocado, EMBARCADO no pacote.
- **Tabela de link interno = a "FFI interna" = `exp` + `pub`.** Artefato TRANSITÓRIO do
  pipeline (parte do `.tkb`/link), **descartado após o link, NUNCA embarcado no `.tkh`**. É
  como um `.tkh` completo que INCLUI `pub`, mas interno ao build.

**Mapeamento visibilidade → linkagem (registrar):**

| Visibilidade | Símbolo no `.o` | No `.tkh`? | Na FFI interna? |
|---|---|---|---|
| **`exp`** | global (visível ao `ld`) | **sim** (API pública) | sim |
| **`pub`** | global (visível ao `ld`/link cross-unit) | **não** | **sim** |
| **privado** | `static`/local | não | não (nem entra na tabela de símbolos) |

**CUIDADO CRÍTICO — a FFI interna é RICA, não só-ABI.** Como no pipeline o **CHECKER roda
DEPOIS do LINK**, a tabela de link interno tem que carregar, por símbolo `exp`+`pub`, o
suficiente para DUAS necessidades:

1. **Assinatura Teko** (tipos de param/retorno no nível da linguagem) — para o CHECKER tipar
   a chamada cross-namespace.
2. **Símbolo de linkagem + ABI** — para o codegen emitir a chamada e o `ld`/link interno
   resolver.

Ou seja, a barreira de LINK (C11) monta uma tabela com **tipos Teko + ABI** (não uma tabela
só-ABI), ela alimenta o checker de cada unidade, e **some após o link**. Desenhar a tabela
só-ABI seria um furo: o checker não teria como tipar a chamada `pub` cross-namespace.

Isto reforça o terminal native: a **tabela de símbolos do `.o` É a tabela de link interno**
no plano de linkagem (globais = `exp`+`pub`; privado = `static`). O `.tkh` (só `exp`) é
ortogonal — o `.o` expõe `pub` ao `ld` sem que `pub` vaze para o header.

### O checker precisa de duas passadas (coleta → checagem)

Sim: o monólito tem recursão mútua, então o checker de qualquer unidade precisa da tabela
linkada COMPLETA (exports de todas as outras) antes de checar corpo algum. Por isso o LINK
(passo 2) é uma barreira: coleta exports de TODAS as unidades primeiro, resolve, e só então o
passo 3 checa corpos em streaming. O que NUNCA fica residente são N conjuntos de corpos —
apenas a FFI interna (compacta) + os corpos da unidade corrente. A "AST incompleta"
existe justamente para que o passo de coleta não segure corpos.

### O que o "linker interno" RETÉM entre unidades × o que DESCARTA

| Retém durante o LINK+streaming (compacto) | Descarta por unidade (libera na fronteira) |
|---|---|
| **FFI interna** (`exp`+`pub`: tipos Teko + símbolo + ABI) — TRANSITÓRIA, some após o link | AST parseada dos corpos daquele namespace |
| `.tkh` (só `exp`) — este SIM embarcado no pacote, ortogonal | Árvores `TStatement` tipadas daquele namespace |
| Pedidos de monomorfização que cruzam unidades | LIR baixado daquele namespace |
| Acumulador de saída C — **idealmente streamed pro arquivo**, não retido | (o C daquele namespace, já escrito) |

O descarte por unidade casa com C6: **uma região de arena por unidade**, derrubada após o
emit daquela unidade (`region_drop_subtree`). Nenhum free bloco-a-bloco.

### Princípio unificador: pipeline em ESTÁGIOS com DESPEJO entre eles

Enquadramento do dono que amarra tudo: a cada ETAPA do compilador, **gerar um ARTEFATO para
a próxima e despejar a memória** antes de seguir. O pico deixa de ser a SOMA
(AST+typed+LIR+buffer vivos juntos) e passa a ser o **MÁXIMO de um único estágio**. Os três
sub-modelos acima (por-unidade, AST-incompleta-linkada-antes-do-checker, incremental) são
CASOS deste princípio — variam só a granularidade da unidade e o ponto de despejo.

Pipeline: `parse → (AST incompleta: decls exp+pub por unidade) → despejo → link → (FFI
interna, transitória) → checker por unidade → (artefato typed) → despejo → lower → (LIR) →
despejo → codegen → saída-de-unidade`. A FFI interna é descartada ao fim do streaming; o
`.tkh` (só exp) é emitido à parte e embarcado.

**O "despejo" tem duas formas, ambas bounded a um estágio:**

- **Despejo em memória (arena-drop = C6):** quando dois estágios FUNDEM por unidade
  (check→lower→emit da mesma unidade, back-to-back), o artefato passa direto em memória e a
  região da unidade é derrubada na fronteira. O drop-em-massa da arena É o despejo. Sem custo
  de IO.
- **Despejo em disco (serialização):** quando um estágio NÃO funde (ex.: o LINK precisa dos
  exports de TODAS as unidades antes de checar QUALQUER corpo — barreira global), o artefato
  do estágio anterior vai para disco e o próximo relê UMA unidade por vez. O working set some
  do heap; volta sob demanda.

**Formato do artefato — REUSAR `.tkb`, não inventar.** `src/emit/tkb_frame.tks` já tem
`serialize_program(prog: checker::TProgram): []byte` e `src/emit/tkb_read.tks`
`deserialize_program(data): checker::TProgram` — a serialização binária do typed-AST, já em
produção no path de pacote (`.tkl`). É EXATAMENTE o artefato do estágio typed (check→lower).
Estendê-lo para **por-unidade** (serializar/deserializar um namespace, não só o programa
inteiro) fecha o dump typed. Para o estágio parse→link basta a forma compacta de exports
(a "AST incompleta"): uma projeção do `.tkb` só com declarações exportadas + pendências, ou
um frame novo mínimo. LIR **não precisa de artefato de disco** se lower→emit fundem por
unidade (despejo em memória); só ganharia `.tkb`-de-LIR se o incremental quisesse cachear
lowered — adiar até medir.

**Quais estágios valem o dump de DISCO** (onde o working set é grande E o próximo estágio
não funde):

| Fronteira | Vale disco? | Por quê |
|---|---|---|
| parse → link | exports em disco/compacto; corpos re-parseáveis | link é barreira global; corpos não cabem todos |
| check → lower | típico: **fundir** por unidade (despejo em memória) | lower consome typed da MESMA unidade na hora |
| lower → codegen | **fundir** por unidade (despejo em memória) | emit consome LIR da mesma unidade na hora |
| typed `.tkb` p/ INCREMENTAL | disco, cache entre builds | reusar unidade não-alterada sem re-checar |

Ou seja: um único dump de disco estrutural (parse→link, para não segurar N corpos), o resto
é fusão por unidade com arena-drop. O `.tkb` em disco reaparece no incremental (cache).

### Convivência com o fixpoint (byte-identidade gen2==gen3)

O `teko.c` emitido DEVE continuar byte-idêntico. O prólogo global (type decls + forward fn
decls) sai da FFI interna (o link a tem); os corpos são streamed em ordem determinística
de namespace. A concatenação por-unidade tem que IGUALAR a emissão whole-program de hoje →
exige a MESMA ordenação global de itens e o mesmo seccionamento.

**Risco load-bearing (R4):** hoje nomes temporários gensym derivam do TAMANHO DO BUFFER
corrente (`$"_oln{buf.len}"`, `$"_arr{buf.len}"` em `emit_slice_of_len`/`emit_array_lit`).
Sob streaming, o buffer é por-unidade, não global → o mesmo corpo geraria nome DIFERENTE →
`teko.c` diverge → fixpoint quebra. **Pré-condição de C10:** trocar todo gensym derivado de
`buf.len` por um contador determinístico global (ou por (namespace, índice-de-fn, seq)),
independente de streaming. Sem isso o Eixo C é impossível de manter verde.

**Determinismo do artefato (R6):** serialize→deserialize tem que ser round-trip SEM perda e
SEM não-determinismo — mesma entrada → mesmo `.tkb` byte-a-byte → mesmo `TProgram` → mesmo
`teko.c`. Proibido no frame: timestamp, endereço de ponteiro, ordem de iteração de
`map`/`hashset` (ordenar chaves), ou qualquer estado global implícito. O `.tkb` já roda no
path de pacote (round-trip exercitado), mas o uso POR-UNIDADE tem que preservar a MESMA
ordem de itens que o whole-program produz — a ordenação de unidades e de itens dentro da
unidade é a âncora do fixpoint.

### Build incremental (caso do princípio unificador, C14)

Recompilar só a unidade que mudou, reusando o artefato `.tkb` typed em cache de disco
(chaveado por hash do conteúdo da unidade + hash da FFI interna de que ela depende). Se a
assinatura exportada de uma unidade muda, os dependentes recompilam. É o **caso persistente**
do despejo-em-disco: o mesmo `.tkb` que bounded o pico serve de cache entre builds. **Para o
self-build / fixpoint, o incremental é DESLIGADO** (build limpo tem que produzir `teko.c`
idêntico); é otimização de tempo de dev, não reduz pico. Atrás de C11–C13, opcional.

### O terminal do pipeline: `teko.c` é MULETA; native/`ld` é o endgame

Correção do dono: o estágio TERMINAL do Eixo C **não pode ser `teko.c`**. O `teko.c` é
muleta — existe só enquanto a linguagem ainda não emite o binário final que ela própria
produz. O endgame é a linguagem emitir seu **PRÓPRIO objeto linkável por `ld`**, sem
depender de compilador C. O backend native já existe e é o caminho real:
`teko::lir::lower_program` → `select_module`/`select_module_x86` (isel) →
`regalloc_module` → `encode_module` → `emit_elf`/`emit_macho`/`emit_coff`
(`src/backend/objfile_*.tks`), com `objfile_ar*` arquivando `.o` em `.a`. As ABIs por SO
(`abi_sysv64`/`abi_aapcs64`/`abi_win64`) já estão lá.

**O estágio terminal tem DOIS alvos; o native é o definitivo:**

- **Rota C (muleta, transitória):** emite UM `teko.c` que cross-compila via `#if` de todos
  os alvos, passado a `cc`. É o que existe hoje.
- **Rota native (endgame):** emite **objeto linkável por unidade** (ELF/Mach-O/COFF) e o
  **`ld` do SO** (ou o link interno / `objfile_ar`) junta no binário final. Sem C.

**Encaixe com o Eixo C — o ganho é MAIOR na rota native.** Compilação por namespace onde
**cada namespace emite UM objeto** é compilação separada clássica: a unidade é lowered para
LIR, encodada para `.o`, escrita no disco, e a memória da unidade é **derrubada** — o
objeto no disco É o despejo natural (não precisa nem de `.tkb` intermediário para o terminal;
o `.o` é o artefato). O "linker próprio" que o dono citou é exatamente isto: a peça que
resolve símbolos ENTRE unidades (a FFI interna de C11) e produz objetos que `ld` costura.
Na rota C, o terminal ainda concatena tudo num `teko.c` (uma "unidade só" degenerada); na
rota native, o terminal é naturalmente por-unidade → o per-unit-object cai de graça do
desenho do Eixo C.

**Portanto o Eixo C deve MIRAR o objeto native como saída de unidade**, com o `teko.c`
como caso degenerado (uma unidade só) durante a transição. C12 (o passo de emit por unidade)
é escrito com a saída-de-unidade abstrata: na rota C ela é um pedaço de texto concatenado; na
rota native ela é um `.o` no disco. Mesmo laço de streaming, dois back-ends de emissão.

**Fixpoint migra de "`teko.c` idêntico" para "objeto native idêntico".** Hoje o critério é
`gen2.c == gen3.c` (o C emitido byte-idêntico). No endgame o critério vira o **objeto/binário
native se reproduzir** — determinismo do `.o`: sem timestamp, ordem estável de símbolos e de
seções, sem paths absolutos embutidos, relocations em ordem canônica. Os `objfile_*` e
`objfile_ar*` já ordenam símbolos (`coff_lib_sorted_symbols`, `bsd_sorted_symbols`); o
crumb do terminal native audita que NADA no `.o` depende de tempo/ambiente. **A muleta C sai
quando o native fecha verde** — as 4 pernas native do CI passando + fixpoint de objeto
reprodutível; aí a rota C é removida (as 2 pernas em C viram native, como a triagem do CI já
prevê).

**Sequência (o que depende de quê):**
- **Eixos A e B (memória) NÃO dependem do native** — matar push e arena-por-escopo valem
  IDÊNTICOS nas duas rotas (o LIR e a arena são compartilhados; só o back-end de emissão
  difere). Entregam a meta ≤1,5 GB independentemente do terminal.
- **Eixo C (pipeline por unidade) é o que HABILITA o per-unit-object → `ld`.** Seu desenho já
  mira o objeto native como saída de unidade; o `teko.c` é o degenerado transitório. O
  terminal native é um sub-eixo de C (crumbs C15–C16), depois de C12 verde na rota C.

### A build do compilador entrega binário + `.tkh`

A emissão do executável final (rota C OU native) deve TAMBÉM emitir o `.tkh` do próprio
compilador — a superfície `exp` agregada (`emit/tkh.tks`) — e o **pacote entrega binário +
`.tkh`**. Backend-independente: é interface de tipos, idêntica nas duas rotas. Serve às
features da próxima versão: o dev usa o `.tkh` do compilador para **intellisense na IDE** e
para **linkar/estender o compilador**. **Só `exp` embarca**; a FFI interna (`exp`+`pub`) é
transitória e NÃO vai pro `.tkh` — coerente com R8 (as duas tabelas são ortogonais). Sem
impacto de memória; é entrega de artefato. Crumb C17.

---

## 7. Disciplina de fixpoint e guard (a cada crumb)

- Validação local = **compilação** (`--no-verify --release`, `TEKO_BACKEND=c`,
  `TEKO_CC=clang`, `ulimit -v 6815744`) + fixpoint (tc2==tc3) + cross-check offline.
- A conversão é **preservante** (mesmo layout `{ptr,len}`, zero-fill puro, sem tag) →
  transição escalonada-verde: o idioma novo coexiste com o velho durante a migração.
- Reseed ITERATIVO (ensinar→seed→sweep→seed) quando a mudança altera o C emitido.
  Módulos-folha não exigem reseed.
- Guard 6,5 GiB INVIOLÁVEL: se um crumb estoura, achar a causa-raiz do consumo daquele
  crumb e corrigir — nunca levantar o teto.

---

## 8. `riscos` e decisões abertas

**R1 — Free targetado vs. arena-por-escopo (rota preferida = escopo).** A rota LIMPA para
o transitório é arena-por-escopo com drop em massa (C6), que **dispensa** free individual;
`region_free` (C8) é o backstop para o acumulador reatribuído em loop longo. Decisão de
design: preferir escopo; só emitir purge-na-reatribuição onde o checker prova posse do
backing. Não transformar TODO `a = <novo>` em free — ver R2.

**R2 — Qualificar a reatribuição (soundness do purge).** Free eager de um backing que é
sub-slice (`s[a..b]`), reinterpret (`str`↔`[]byte`, mesma memória), literal, ou parâmetro
emprestado CORROMPE o vizinho/aliás. O predicado `assign_frees_old` generalizado (C8) DEVE
liberar apenas quando o backing anterior é alocação de arena fresca possuída pela variável.
Conservador: na dúvida, NÃO liberar (o drop-de-escopo recolhe depois). Este é o ponto de
maior cuidado do plano.

**R3 — O slot de controle da arena ainda é C (3 shims `from "teko_rt"`).** Fora do caminho
de memória, mas viola "TUDO em Teko" enquanto não transcrito (C9). Rota: slot `.bss` via
`LGlobalAddr` (P2 honesto de `arena-em-teko.md`) ou `mmap MAP_FIXED` (plano B). Não bloqueia
a meta de memória; registrado para fechar o expurgo do C.

**R4 — Gensym derivado de `buf.len` quebra o streaming (load-bearing do Eixo C).** Nomes
temporários como `$"_oln{buf.len}"`/`$"_arr{buf.len}"` derivam do tamanho do buffer global;
sob emissão por-unidade o buffer é por-namespace → mesmo corpo, nome diferente → `teko.c`
diverge → fixpoint quebra. C10 (determinizar gensym) é PRÉ-CONDIÇÃO obrigatória de C11/C12;
sem ele o Eixo C é impossível de manter verde.

**R5 — Byte-identidade do `teko.c` sob compilação por unidade.** O modelo streaming tem que
produzir `teko.c` byte-idêntico ao whole-program: prólogo global da tabela de assinaturas +
corpos em ordem determinística de namespace, com o MESMO seccionamento. Qualquer estado
global implícito hoje acumulado durante a iteração whole-program (contadores, tabelas de
monomorfização, ordem de forward-decls) tem que ser reproduzido a partir do LINK (C11). C12
valida byte-a-byte por namespace migrado; na menor divergência, PARAR e reconciliar — não
maquiar. Granularidade decidida law-first: **namespace** (arquivo quebra coesão intra-módulo;
whole-program é o pico). Se surgir um namespace que sozinho ainda pique acima do guard, ele
NÃO se subdivide por arquivo — subdivide-se por REGIÃO de escopo interno (C6) dentro da
unidade, mantendo a fronteira de unidade em namespace.

**R6 — Determinismo do artefato serializado (`.tkb` inter-estágio).** O dump para disco tem
que ser round-trip sem perda e sem não-determinismo: proibido timestamp, endereço de
ponteiro, ou ordem de iteração de `map`/`hashset` no frame (ordenar chaves). Mesma entrada →
mesmo `.tkb` → mesmo `teko.c`. O `.tkb` já roda no path de pacote, mas o uso por-unidade
(C13) precisa preservar a MESMA ordem de itens do whole-program. Custo de serializar/reler é
o trade contra memória — só se paga onde o estágio não funde (a barreira do LINK); onde funde
(check→lower→emit por unidade), o despejo é arena-drop em memória, sem IO.

**Decisão em aberto (não-HALT) — ordem de execução dos eixos.** Eixo A (C3–C5) é pré-requisito
factual: matar o push primeiro derruba 93% e simplifica a conversão dos mesmos módulos que o
Eixo C reestrutura. Eixo C (C10–C16) é grande e arquitetural; recomendo entregá-lo DEPOIS de
A+B verdes e SÓ SE o residual pós-C6 ainda exceder a folga desejada. Se A+B já entregam
≤1,0 GB (provável), o Eixo C vira otimização de teto/robustez, não obrigação da meta. Sem
tensão de Lei aqui — é sequenciamento; registrado para o dono decidir a prioridade.

**R7 — Determinismo do objeto native (o fixpoint do endgame).** Ao migrar o critério de
`gen2.c==gen3.c` para "objeto native reproduzível" (C16), o `.o`/binário tem que ser
byte-idêntico entre gerações: sem timestamp, ordem estável de símbolos e seções, sem path
absoluto embutido, relocations em ordem canônica, padding zerado determinístico. Os
`objfile_*`/`objfile_ar*` já ordenam símbolos (`coff_lib_sorted_symbols`,
`bsd_sorted_symbols`), mas o audit de C16 tem que cobrir TODA fonte de não-determinismo do
encoder. A muleta C só sai quando as 4 pernas native do CI fecham verde E o objeto reproduz.
O terminal native (C15–C16) NÃO bloqueia a meta de memória (Eixos A/B a entregam nas duas
rotas) — é o endgame arquitetural: a linguagem emitindo seu próprio binário linkável por
`ld`, sem depender de compilador C.

**R8 — A FFI interna NÃO pode vazar `pub` para o `.tkh` (ortogonalidade das duas tabelas).**
O erro a evitar: reusar a projeção do `.tkb` estendida com `pub` e, por descuido, embarcá-la
no `.tkh`. As duas tabelas são ortogonais: `.tkh` = só `exp`, embarcado; FFI interna =
`exp`+`pub`, TRANSITÓRIA, descartada ao fim do link, jamais embarcada. O checker roda DEPOIS
do link, então a FFI interna tem que ser RICA (tipos Teko + símbolo + ABI por decl); uma
tabela só-ABI não deixaria o checker tipar a chamada `pub` cross-namespace. Guardas: (a) o
emissor de `.tkh` (`emit/tkh.tks`) continua filtrando só `exp`; (b) a FFI interna vive só na
região do link e é derrubada antes do emit final; (c) nenhum `pub` aparece no `.tkl`
empacotado. Mudar isto é mudança de superfície/ABI → exige reseed.
