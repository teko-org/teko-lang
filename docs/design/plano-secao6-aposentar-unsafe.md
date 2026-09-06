# Plano — §6: Aposentar `unsafe` e ponteiros crus (0.3.1 superfície)

> **Status:** DESIGN. Read+design (nenhum código de produto, nenhum reseed). Este documento é O
> ARTEFACTO; o único commit é ele próprio.
> **Fonte de lei:** `mudancas-superficie-0.3.1.md` §6 (linhas 192-224 — SAI/ENTRA/ORDEM). Base
> ratificada (NÃO re-derivar): `memory-unsafe-backend-remodel.md` (unsafe-por-TIPO — o selo que perde
> função), `arena-por-escopo-0.3.1.md` + `modelo-de-memoria-por-escopo-0.3.1.md` (região-como-escopo
> que substitui a `Arena` manual), `arena-especificacao-unica-0.3.1.md` Doc 1 (o que o drop de região
> já faz). Superfície §5 já aterrou: `ptr`/`uptr` opacos atômicos + `p.__wrap<T>()` (o honest-stop de
> FFI em que o §6 se apoia).
> **Branch:** `fix/retirement` @ `5f24e443` (generics completos + §5 Parte A opaco `ptr`/`uptr`).
> **Regras do dono incorporadas (2026-08-11):** (i) `#must_free` e TODO controlo de memória manual
> DE SUPERFÍCIE caem JUNTO com `unsafe` — incluindo o dataflow de consumo `#must_free` do checker e o
> intrínseco `type_mem_free`; (ii) primitivas de BACKEND que o compilador ainda precisa por dentro são
> MANTIDAS, de-superficializadas (sem gramática, sem visibilidade); (iii) o shift de ordinal `.tkb` NÃO
> é fork — renumera-se livremente e BUMPA-SE `TKB_EXPR_VERSION`/`TKB_PROGRAM_VERSION` (precedente: a
> renumeração de `PrimKind` do R2, `tkb_frame.tks:406`, `tkh.tks:246-248`); o reseed self-host builda
> de FONTE, não de `.tkb` persistido, então o fixpoint é indiferente.

---

## 0. O que o §6 pede (recap normativo) e as DUAS categorias do dono

**SAI (superfície, visível ao dev):** a keyword `unsafe` (+ `is_unsafe` + contágio), a memória manual
`mem::free` / `#must_free` / `Arena` não-lexical / `RawBuf` / `Owned<T>`.

**ENTRA:** NADA de keyword. O único resíduo de "confiança" é FFI: `extern fn` permanece; a fronteira
estrangeira cruza-se embrulhando o `ptr` estrangeiro com `.__wrap<T>()` (§5). A `Arena` manual vira um
**escopo** (região lexical — já modelado em `arena-por-escopo`/`modelo-de-memoria-por-escopo`, sem
keyword nova); `mem::free` é obsoletado pelo drop de região.

**ORDEM MANDATÓRIA (passos do dono):** (1) reclassificar as intrínsecas de `teko::mem`/região como
**seguras** (largar a exigência de `unsafe`); (2) aposentar a memória manual; (3) **DELETAR a keyword
`unsafe` por ÚLTIMO**, quando nada mais a contém.

### 0.1 A distinção crítica do dono — SUPERFÍCIE (sai) vs BACKEND-INTERNO (fica)

Aposentar ≠ perder a capacidade de libertar memória. É **removê-la da superfície da linguagem**
preservando o mecanismo interno de que o backend precisa. O plano separa as duas categorias em CADA
sítio da varredura:

| categoria | o que é | ação §6 |
|---|---|---|
| **(a) SUPERFÍCIE** — visível/chamável pelo dev | keyword `unsafe`; decorator `#must_free` + o dataflow de consumo que o policia; `teko::mem::free` chamável; `RawBuf`/`Owned<T>`/`Arena` não-lexical; a gramática e os diagnósticos deles | **REMOVIDO**, com REJECT-fixture |
| **(b) BACKEND-INTERNO** — de que o compilador precisa por dentro | o `free` real que o drop de região emite (`tk_region_drop`/`tk_region_drop_subtree`/`tk_regions_free_all`); o alocador de runtime (`tk_region_new`/`tk_region_alloc`); as primitivas de ciclo-de-vida `region_new`/`enter`/`leave`/`drop`/`root` (nomes NUS, já só usadas por `src/build/project.tks`) e os seus gémeos nativos `tk_region_*_u` | **MANTIDO**, sem gramática e sem soletração de superfície |

**A auditoria que o dono exigiu (o drop de região passa PELA superfície `mem::free`?): NÃO.** Medido:

- **Rota C** (`codegen.tks`): o drop de escopo emite `tk_region_new(<enclosing>)` na entrada e
  `tk_region_drop` na aresta de saída DIRETAMENTE em texto-C (`codegen.tks:5620,7431,8496,9159,9513`
  e as arestas de drop) — nunca via `teko::mem::free`.
- **Rota nativa** (`lir/lower.tks`): `open_native_region` emite `tk_region_new_u`/`tk_region_enter_u`/
  `tk_region_drop_u` diretamente (`lower.tks:1649-1688`) — nunca via `teko::mem::free`.
- **`teko_rt.tks`:** declara as primitivas de arena (`tk_region_new`/`tk_region_alloc`/`tk_region_drop`/
  `tk_region_root`, `teko_rt.tks:657-665`; `tk_regions_free_all`, `:679-686`) como host-edge externs
  honestos; NÃO chama `mem::free`/`RawBuf`/`Owned`/`Arena`/`#must_free` — nenhum resíduo §6.

Logo a maquinaria de lowering do `teko::mem::free` de superfície (`lower_mem_free_call`/`lower_free_slice`/
`lower_free_named`/`lower_free_region_handle`; `codegen.tks` free-on-region-handle) é um CAMINHO
SEPARADO, alcançado só pela chamada de superfície. Cortar a superfície NÃO toca o drop de região. **Só
há UM chamador de superfície a re-rotear/remover antes de apagar a soletração** (§C2, abaixo).

---

## 1. Blast radius MEDIDO (sítios semânticos REAIS, o grep sobre-conta)

Bruto: `unsafe`/`is_unsafe` ~272 hits; `mem::free`/`must_free` ~202 hits — a maioria doc-comment,
mangling, ou a PRÓPRIA implementação do compilador (não call-sites). Os sítios semânticos reais,
agrupados pelos 3 passos ordenados:

### PASSO 1 — reclassificar intrínsecas como SEGURAS (JÁ realizado por §5; confirmar)

| # | sítio | arquivo:linha | veredito |
|---|---|---|---|
| 1 | `buf_ptr`/`bytes_from_ptr`/`as_ptr`/`as_cstr`/`str_from_cstr` | `scope.tks:540,577,1073`; `lower.tks:4235-4300`; `codegen.tks:3835+` | retornam `ptr` opaco/`[]byte` (SEGUROS pós-§5); **sem gate `env.fn_unsafe`** — já safe-callable. Passo 1 = confirmar + fixture, ZERO edição |

**Achado decisivo:** não existe gate `env.fn_unsafe` sobre estas intrínsecas. A única "unsafe-ness"
hoje é os TIPOS `unsafe` (`RawBuf`, `Arena`); as intrínsecas de FFI que SOBREVIVEM já retornam tipos
seguros desde o §5. O Passo 1 está substancialmente CUMPRIDO — o crumb apenas trava-o com fixture.

### PASSO 2 — aposentar memória manual (superfície) + de-superficializar o interno

| # | sítio | arquivo:linha | categoria | ação |
|---|---|---|---|---|
| 2 | chamador self-host `teko::mem::free(tokens)` | `build/assemble.tks:102` | (a) | **REMOVER a linha** — `tokens` passa a morrer no escopo (leak-to-root seguro até a região-escopo reclamar); severa o único uso de superfície ANTES de apagar a soletração |
| 3 | `type_mem_free` (intrínseco de superfície) | `checker/typer.tks:938-960` | (a) | DELETAR |
| 4 | dataflow de consumo `#must_free` | `checker/typer.tks:3610-3970` (`must_free_consumed_on_all_paths`, `stmt_consumes_must_free`, `texpr_is_must_free_var`, `stmt_branches_all_consume_must_free`, `match_arms_all_consume_must_free`, `check_must_free_params`, `check_must_free_locals*`, `check_must_free`, `must_free_dropped_message`) + a chamada `typer.tks:6413` | (a) | DELETAR (só policia uma anotação de superfície) |
| 5 | guarda afim `mem::free` (#331 L2b) | `checker/typer.tks:6043-6160` (`free_target_name`, guarda `is_unique_at`, scans `block_embeds_free`); `spine.tks:994-1000,1158,1544-1556` (probes `is_mem_free_call`) | (a) | DELETAR os probes/guardas de `mem::free`; PRESERVAR `is_unique_at`/o reticulado (serve o modelo de escopo/cross-thread) |
| 6 | intrínsecos de superfície `teko::mem::region_new`/`region_alloc` | `checker/typer.tks:982-1028` (`type_region_new`/`type_region_alloc`) + dispatch `typer.tks:1769` | (a) | DELETAR (existem SÓ para servir a `Arena` manual) |
| 7 | lowering de superfície `mem::free`/`region_new`/`region_alloc` | `lir/lower.tks:4397-4650` (`is_region_new_call`/`is_region_alloc_call`/`lower_region_new_call`/`lower_region_alloc_call`/`region_alloc_elem`/`is_mem_free_call`/`lower_mem_free_call`/`lower_free_slice`/`lower_free_named`/`lower_free_region_handle`) + interceptação `lower.tks:3084-3088`; `codegen.tks:3851-3900` (`emit_region_alloc`), `4110-4160` (free-on-region-handle), dispatch `4407-4412` + `mem::free` | (a) | DELETAR (caminho de superfície) |
| 8 | helpers de region-handle | `checker/resolve.tks:1407-1485` (`is_must_free_name`, `is_region_handle_name`/`_decl`, `region_handle_field_name`) | (a) | DELETAR (só reconhecem a forma `Arena` de superfície) |
| 9 | `src/mem/unsafe/` inteiro | `rawbuf.tks` (`RawBuf`, `Owned<T>`, `rawbuf_alloc/read/len`), `arena.tks` (`Arena`), `rawbuf_test.tkt` | (a) | DELETAR o diretório |
| 10 | decorator `#must_free` (parse) + campo AST `must_free` | `parser/parse_decl.tks:919,1285,1355-1364`; `parser/ast.tks:596`; ~21 literais `must_free =` em `src/*.tks`; `emit/tkb_read.tks:823,844` (lê sempre `false` — NÃO está no wire) | (a) | REJEITAR o decorator; remover o campo (sweep) |
| — | primitivas internas `tk_region_*`/`region_new/enter/leave/drop/root` nus | `teko_rt.tks:657-686`; `build/project.tks:3701,4371,4390,4676,4695,4853,4872`; `lower.tks` `tk_region_*_u` | (b) | **MANTER** — sem soletração de superfície; é o backend do drop de escopo |

### PASSO 3 — DELETAR a keyword `unsafe` por último

| # | sítio | arquivo:linha | ação |
|---|---|---|---|
| 11 | maquinaria de contágio (agora MORTA — nada é `is_unsafe`) | `collect.tks:39-43,96,112,170-237,2000-2040,2114,2144` (`reject_unsafe_signature_contagion`/`reject_unsafe_field_contagion`/`reject_unsafe_alias`/`method_is_effective_unsafe`); `resolve.tks:1501-1604` (`is_unsafe_type`/`unsafe_carrying*`/`func_type_unsafe_carrying`/`generic_args_unsafe_carrying`); `typer.tks:4530-4532` (gate de local), `5950`+`6364` (chamadas `with_fn_unsafe`), `6361-6362` (`#arena_size`×`unsafe`); `scope.tks:80,88,109-110` (campo `fn_unsafe` + `with_fn_unsafe`) | DELETAR |
| 12 | modificador contextual `unsafe` (parse) + campo AST `is_unsafe` | `parser/parse_decl.tks:190-211` (`is_unsafe_modifier_at`/`consume_unsafe_modifier`), chamadas `392-394,883-885,1285,1388`; `parser/ast.tks:442,593` (campos); ~31 literais `is_unsafe =`; `lexer.tks:325-326` (doc-comment: `unsafe` deixa de ser contextual) | DELETAR (sweep) |
| 13 | serialização `is_unsafe` (trailing) | `emit/tkb_write.tks:483,533` (escreve — TRAILING), `emit/tkb_read.tks:723,823` (lê) | remover I/O; **BUMP `TKB_EXPR_VERSION`+`TKB_PROGRAM_VERSION`** (`tkb_frame.tks:412,419`) — trailing não desloca ordinal, mas mudar o wire exige versão nova (procedimento-padrão R2) |

**Nota `.tkb` (regra posicional, procedimento-padrão):** `is_unsafe` "cavalga por ÚLTIMO" no registo
TypeDecl/method (`tkb_write.tks:483,533`; `tkb_read.tks:723,823`), então removê-lo NÃO desloca tag/campo
nenhum — é o caso trailing benigno. `must_free` nem sequer está no wire (o reader devolve sempre
`false`, `tkb_read.tks:823`), logo remover o campo é puramente in-memory. Em AMBOS: bumpar
`TKB_EXPR_VERSION`/`TKB_PROGRAM_VERSION`; o reader já rejeita artefactos velhos em voz alta ("...must be
rebuilt with the current compiler"). Se algum campo NÃO-trailing precisasse mover, seria o MESMO
procedimento (renumerar + bumpar), não um fork.

---

## 2. Sequência de crumbs ORDENADA (honra o passo-1→passo-2→passo-3 do dono)

Cada crumb: TAG (ADITIVO / SWEEP / REMOÇÃO), ficheiros, teach/un-teach, gate. O seed é o `teko` lançado
anterior — que ENTENDE toda a sintaxe velha; o §6 só REMOVE uso, então a corpus builda com o mesmo seed
em cada crumb (nenhuma feature nova a semear). Reseed único no RITUAL FINAL.

### C0 — [DOC] Banner de aposentadoria + este plano
Adicionar banner `SUPERSEDED-by-§6` a `memory-unsafe-backend-remodel.md` (o modelo unsafe-por-tipo é
histórico após o §6; **não apagar** — é base ratificada, ganha aviso apontando aqui). Commit deste
plano. Doc-only. **Ritual: NÃO.**

### PASSO 1 — reclassificar intrínsecas como seguras (PRIMEIRO)

### C1 — [ADITIVO] Travar "intrínsecas de FFI são safe-callable" com fixture
Confirmar (§1 #1) que `buf_ptr`/`bytes_from_ptr`/`as_ptr` são chamáveis de um `pub fn` NU (sem `unsafe`)
e devolvem tipos seguros — realizado por §5, sem edição de produto. Autorar ACCEPT
`mem_intrinsics_safe_callable`. Se qualquer resíduo de gate `env.fn_unsafe` sobre uma intrínseca
SOBREVIVENTE for encontrado na implementação, removê-lo aqui (medição diz: nenhum existe).
**Ficheiros:** fixtures. **Un-teach:** nenhum. **Gate:** trivial (fixture ACCEPT compila+corre).
**Ritual: NÃO.**

### PASSO 2 — aposentar memória manual (superfície) + de-superficializar

### C2 — [SWEEP, self-host] Severar o único chamador self-host de superfície `mem::free`
Remover `teko::mem::free(tokens)` (`build/assemble.tks:102`) e flatten `asm_lex_and_parse`: `tokens`
passa a morrer no seu escopo (leak-to-root seguro; a região-como-escopo reclama-o quando aquele modelo
aterrar). Isto corta o ÚNICO uso de superfície ANTES de C3 apagar a soletração — o drop de região não
depende dele (auditoria §0.1). **Ficheiros:** `build/assemble.tks`. **Un-teach:** o self-host deixa de
chamar `mem::free`. **Gate — RITUAL COMPLETO:** dry build + `teko test .` verde + FIXPOINT (a saída
emitida não muda — remover um park de free não altera bytes). **Ritual: SIM.**

> **ACHADO ADJACENTE (REPORTADO, não convertido em issue):** o fato ruled #2 do briefing dizia "NADA no
> self-host CHAMA `mem::free` nos seus próprios dados". `assemble.tks:102` é EXATAMENTE isso — um
> chamador vivo. Não é bloqueio: a ação é remover a linha (acima). Reportado ao dono como correção do
> fato #2.

### C3 — [REMOÇÃO] Deletar os intrínsecos de superfície de memória manual + o dataflow `#must_free`
Deletar: `type_mem_free` (§1 #3); o dataflow de consumo `#must_free` inteiro (§1 #4); os probes/guardas
afins de `mem::free` (§1 #5, preservando `is_unique_at`); os intrínsecos `type_region_new`/
`type_region_alloc` de superfície + dispatch (§1 #6); o lowering de superfície `mem::free`/`region_new`/
`region_alloc` em `lower.tks`+`codegen.tks` (§1 #7); os helpers `is_must_free_name`/
`is_region_handle_name`/`region_handle_field_name` (§1 #8). **MANTER** `buf_ptr`/`bytes_from_ptr`/`as_ptr`
(FFI/str, §5) e as primitivas internas `tk_region_*`/`region_new` NU (backend do escopo). **Ficheiros:**
`checker/typer.tks`, `checker/resolve.tks`, `checker/spine.tks`, `lir/lower.tks`, `codegen/codegen.tks`.
**Un-teach:** o checker deixa de reconhecer `teko::mem::free`/`region_new`/`region_alloc`/`#must_free` de
superfície. REJECT-fixtures `mem_free_rejected`, `region_alloc_surface_rejected`. **Gate — RITUAL
COMPLETO** (dry build + fixpoint). **Ritual: SIM.**

### C4 — [REMOÇÃO] Deletar `src/mem/unsafe/` (RawBuf, Owned<T>, Arena)
Apagar `rawbuf.tks`, `arena.tks`, `rawbuf_test.tkt` (§1 #9). Corrigir as referências doc-comment
sobreviventes (`collect.tks ~1997-2007`, `lower.tks ~16143/16586` — exemplos que citam `RawBuf`/
`Owned<T>`). Inverter/remover as fixtures `examples/regressions/arena_manual_ok` e `arena_manual_leak`
(nomeadas em `arena.tks`) → viram REJECT. **Ficheiros:** `src/mem/unsafe/*`, doc-comments citados,
fixtures de regressão. **Un-teach:** `RawBuf`/`Owned<T>`/`Arena` deixam de existir. REJECT-fixtures
`rawbuf_rejected`, `owned_rejected`, `arena_manual_rejected`. **Gate — RITUAL COMPLETO.** **Ritual: SIM.**

### C5 — [REMOÇÃO/SWEEP] Retirar o decorator `#must_free` + o campo AST `must_free`
Rejeitar o decorator `#must_free` no parser (`parse_decl.tks:919,1355-1364`; o skip `:1285` some com
C7); remover o campo `must_free` de `parser::TypeDecl` (`ast.tks:596`) e varrer os ~21 literais
`must_free =`; remover a leitura/escrita `.tkb` (não está no wire — só limpar as referências de campo em
`tkb_read.tks:823,844`). **Ficheiros:** `parser/parse_decl.tks`, `parser/ast.tks`, ~21 sítios de literal,
`emit/tkb_read.tks`. **Un-teach:** `#must_free` deixa de parsear. REJECT-fixture
`must_free_decorator_rejected`. **Gate — RITUAL COMPLETO** (parser muda; `.tkb`: `must_free` fora do wire,
sem bump por si só — o bump vem em C7). **Ritual: SIM.**

### PASSO 3 — deletar a keyword `unsafe` por ÚLTIMO

### C6 — [REMOÇÃO] Deletar a maquinaria de contágio unsafe (agora MORTA)
Após C4, nenhum tipo é `is_unsafe = true`, então TODO o contágio é código morto. Deletar (§1 #11):
`reject_unsafe_signature_contagion`/`reject_unsafe_field_contagion`/`reject_unsafe_alias`/
`method_is_effective_unsafe` + chamadas em `collect.tks`; `is_unsafe_type`/`unsafe_carrying*`/
`func_type_unsafe_carrying`/`generic_args_unsafe_carrying` em `resolve.tks`; o gate de local
`typer.tks:4530-4532`, as chamadas `with_fn_unsafe` (`typer.tks:5950,6364`) e o check
`#arena_size`×`unsafe` (`typer.tks:6361-6362`); o campo `fn_unsafe` do `Env` + `with_fn_unsafe`
(`scope.tks:80,88,109-110` e cada cópia de struct que o propaga). **Ficheiros:** `checker/collect.tks`,
`checker/resolve.tks`, `checker/typer.tks`, `checker/scope.tks`. **Un-teach:** o checker deixa de ter
qualquer conceito de contágio. **Gate — RITUAL COMPLETO.** **Ritual: SIM.**

### C7 — [REMOÇÃO/SWEEP] Deletar o modificador `unsafe` + campo `is_unsafe` + serialização
Deletar `is_unsafe_modifier_at`/`consume_unsafe_modifier` e as suas chamadas
(`parse_decl.tks:190-211,392-394,883-885,1285,1388`); remover o campo `is_unsafe` de `parser::Function`
e `parser::TypeDecl` (`ast.tks:442,593`) e varrer os ~31 literais `is_unsafe =`; remover a I/O de
`is_unsafe` em `tkb_write.tks:483,533` + `tkb_read.tks:723,823` (caso trailing benigno) e **BUMPAR
`TKB_EXPR_VERSION` + `TKB_PROGRAM_VERSION`** (`tkb_frame.tks:412,419`); atualizar o doc-comment
`lexer.tks:325-326` (`unsafe` já não é palavra contextual). **Ficheiros:** `parser/parse_decl.tks`,
`parser/ast.tks`, ~31 sítios de literal, `emit/tkb_write.tks`, `emit/tkb_read.tks`, `emit/tkb_frame.tks`,
`lexer/lexer.tks`. **Un-teach:** `unsafe fn`/`unsafe type` deixam de parsear. REJECT-fixtures
`unsafe_fn_rejected`, `unsafe_type_rejected`. **Gate — RITUAL COMPLETO** — este é o crumb que MOVE bytes
(parser+serialização+versão), o gate fixpoint prova-o. **Ritual: SIM.**

### RITUAL FINAL — dry build + reseed manual (1×) + fixpoint + provenance + self-suficiência
Ver §7. **1 reseed.**

---

## 3. Contagem de reseed + justificação

**1 reseed** (no RITUAL FINAL). Justificação:

- O seed é o binário `teko` lançado ANTERIOR, que entende TODA a sintaxe velha (`unsafe`, `#must_free`,
  `mem::free`). O §6 só REMOVE uso da corpus e maquinaria do compilador — nunca ensina idioma novo ao
  seed. Logo cada crumb C2–C7 builda gen1 com o MESMO seed (a corpus pós-crumb já não usa o que foi
  retirado; o seed continua capaz de a compilar).
- Não há dependência circular seed↔corpus: como nada NOVO entra, nenhum crumb precisa de um reseed
  intermédio para se tornar o seed do próximo. O único reseed é o final, que regenera o binário
  committado JÁ sem a maquinaria.
- **Crumb(s) gatilho do fixpoint (onde os bytes se movem):** **C7** (parser + serialização + bump de
  versão) é o crumb decisivo — muda a forma emitida do `.tkb` e o parse. C6/C5/C3 removem código morto
  ou caminhos de superfície sem chamadores na corpus, então tendem a fixpoint byte-idêntico já; C7 é o
  que exige o BUMP de versão e a prova gen2==gen3 sobre o novo wire. Cada crumb com "Ritual: SIM" corre
  dry-build+fixpoint; o AVANÇO do seed committado é único (final), como no §5.

---

## 4. Destino de cada peça (delete vs migrate-safe), crumb a crumb

| peça | decisão | contra qual doc | crumb |
|---|---|---|---|
| `src/mem/unsafe/rawbuf.tks` (`RawBuf`) | **DELETE** — buffer cru de superfície; substituído por `[]byte` seguro + `buf_ptr`/`bytes_from_ptr` (§5) na fronteira FFI | §6 "o que sai" | C4 |
| `Owned<T>` (`rawbuf.tks:75`) | **DELETE** — o dono lista-o em "o que sai"; não é `unsafe`-marcado mas é o wrapper move-only de superfície de memória manual; nada no self-host o usa (só a def + 1 doc em `lower.tks`) | §6 "o que sai" | C4 |
| `Arena` (`arena.tks`, tipo não-lexical `unsafe #must_free`) | **DELETE o tipo** — substituído pela REGIÃO-COMO-ESCOPO (drop lexical automático, SEM keyword nova), já desenhada em `arena-por-escopo`/`modelo-de-memoria-por-escopo`. NÃO se adiciona um bloco `region {}` (o dono: "nada de keyword") | §6 + `modelo-de-memoria-por-escopo` §4/§8 (os 5 escopos léxicos drop-por-omissão) | C4 |
| `type_region_new`/`type_region_alloc` (intrínsecos de SUPERFÍCIE `teko::mem::…`) | **DELETE** — existem só para servir a `Arena` manual | §6 | C3 |
| `region_new`/`enter`/`leave`/`drop`/`root` NUS (+ `tk_region_*_u`) | **MANTER (interno)** — backend do drop de escopo; já de-superficializados (nome nu, só `project.tks` os usa); sem gramática | `arena-por-escopo` §M1b (usa `region_new(region_root())`) | — (nenhum) |
| `type_mem_free` + dataflow `#must_free` + guarda afim | **DELETE** — policiam anotações de superfície | §6 + ruling do dono | C3 |
| `teko_rt.tks` memória manual | **NENHUMA MUDANÇA** — não usa nada §6-retirado (§0.1). O free que o drop de região precisa é `tk_region_drop`/`tk_regions_free_all`, host-edge extern DIRETO, sem soletração de superfície | `teko_rt.tks:657-686` | — (nenhum) |

**`teko_rt.tks` self-suficiência (veredito):** `teko_rt.tks` NÃO depende do gémeo C para nenhum caminho
§6 e NÃO usa memória manual de superfície. Declara as primitivas de arena como externs honestos
(host-edge, o gémeo C carrega o comportamento em runtime — a divisão runtime NORMAL, igual para ambos os
backends, não uma dependência §6). O drop de região liberta via `tk_region_drop`/`tk_region_drop_subtree`/
`tk_regions_free_all` — já interno, sem soletração de superfície. **§6 exige ZERO edição a `teko_rt.tks`;
a self-suficiência é PRESERVADA e nenhum mecanismo novo é necessário.**

---

## 5. Fixtures (AUTORADAS; suite NÃO executada — dry build + fixpoint é o gate)

**ACCEPT — oráculo nativo (exit = valor):**

| fixture | exercita | exit |
|---|---|---|
| `mem_intrinsics_safe_callable` | `buf_ptr`/`bytes_from_ptr` de um `pub fn` NU (sem `unsafe`) → roundtrip de bytes | soma dos bytes |
| `mem_scope_drop_replaces_free` | valor construído num `{}` reclamado na aresta de saída SEM `mem::free` | valor conhecido |
| `ffi_wrap_boundary` | `extern fn` devolve `ptr` estrangeiro cruzado por `.__wrap<T>()` (§5), sem `unsafe` | valor embrulhado |

**REJECT — `EXPECT_COMPILE_FAIL` (após a retirada respetiva):**

| fixture | rejeita | após crumb |
|---|---|---|
| `mem_free_rejected` | `teko::mem::free(x)` | C3 |
| `region_alloc_surface_rejected` | `teko::mem::region_alloc(h, v)` / `teko::mem::region_new()` | C3 |
| `rawbuf_rejected` | anotar/nomear `RawBuf` | C4 |
| `owned_rejected` | anotar/nomear `Owned<T>` | C4 |
| `arena_manual_rejected` | `Arena::new()` não-lexical (inverte `arena_manual_ok`) | C4 |
| `must_free_decorator_rejected` | `#must_free type D = struct { … }` | C5 |
| `unsafe_fn_rejected` | `unsafe fn f() { }` | C7 |
| `unsafe_type_rejected` | `unsafe type M = struct { }` | C7 |

Fixtures existentes a RETIRAR/inverter: `examples/regressions/arena_manual_ok` (→ remover),
`arena_manual_leak` (→ vira `must_free_decorator_rejected` ou remover), `src/mem/unsafe/rawbuf_test.tkt`
(apagado com o diretório, C4), e os arms `unsafe`/`must_free`/`Arena` em `checker_test.tkt` (~1220-1380),
`parser_test.tkt`, `borrow_test.tkt`, `instantiate_order_test.tkt` — limpar junto do crumb que remove o
código que testam.

---

## 6. Decisões para o dono (recomendação, não poll — cada uma com default + exemplo)

1. **`Arena{}` manual → bloco `region {}` OU simplesmente deletado?** — **Recomendo DELETAR o tipo, SEM
   adicionar `region {}`.** A região-como-escopo (`modelo-de-memoria-por-escopo` §4/§8) já dá o drop
   lexical por OMISSÃO nos 5 escopos, sem keyword. O dono disse "nada de keyword"; um `region {}`
   explícito seria uma. O escopo `{ }` nu É a substituição.
   ```teko
   // antes (retirado)
   mut a = Arena::new(); let p = a.alloc<Node>(...); teko::mem::free(a)
   // depois — o { } é a região; o drop é na aresta de saída, automático, sem soletração
   { let n = Node { value: 42 } /* n morre aqui, sem mem::free */ }
   ```
2. **Os intrínsecos `teko::mem` de região mantêm nomes ao perder `unsafe`?** — **Recomendo: as
   primitivas INTERNAS (`region_new`/`enter`/`leave`/`drop`/`root` nus, `tk_region_*_u`) mantêm nome
   (já são internas, só `project.tks` as usa); as de SUPERFÍCIE (`teko::mem::region_new`/`region_alloc`/
   `free`) são DELETADAS, não renomeadas.** Nenhuma soletração de superfície sobrevive.
3. **`Owned<T>`: deletar ou reter como wrapper move-only seguro?** — **Recomendo DELETAR.** O dono
   lista-o em "o que sai"; não é `unsafe`-marcado mas é superfície de memória manual; o único uso é a
   def + um doc. Reter seria manter superfície que o §6 aposenta.
4. **Texto EXATO dos diagnósticos REJECT** (o que as fixtures asseguram) — **Recomendo:**
   - `unsafe`: `"unsafe was retired in 0.3.1: memory safety is the arena's job (capability + lifetime) + F1; there is no unsafe surface to mark"`
   - `#must_free`/`mem::free`: `"manual memory was retired in 0.3.1: values die at their lexical scope (region drop); the FFI boundary is crossed with __wrap<T>()"`
   - `RawBuf`/`Owned<T>`/`Arena`: `"'{name}' was retired in 0.3.1 (manual-memory surface) — use a []byte / a lexical scope / __wrap<T>()"`

---

## 7. O ritual

Em CADA invocação de compilador:

1. `export TK_RT_DIR="$PWD/src/runtime"`.
2. **Dry build:** `TEKO_BACKEND=c <compiler> build . -o out --no-verify --release`.
3. **Reseed manual (RITUAL FINAL, 1×):** `bootstrap/teko.c` → binário; `TEKO_BACKEND=c binário build .
   --no-verify --release` → `OUT/teko.c`; **fixpoint byte-idêntico gen2==gen3** (o gate; sem correr a
   suite de testes — as fixtures são AUTORADAS, não executadas).
4. **PROVENANCE:** atualizar o registo de proveniência do reseed.
5. **Check de self-suficiência de `teko_rt.tks` (explícito):** confirmar que `teko_rt.tks` builda/funciona
   SEM depender do gémeo C para qualquer caminho §6 — i.e., que nenhuma edição §6 introduziu uma chamada
   a `mem::free`/`RawBuf`/`Owned`/`Arena` no runtime, e que o free do drop de região continua a resolver
   por `tk_region_drop`/`tk_regions_free_all` (host-edge direto). Medição: `grep` de `mem::free`/`RawBuf`/
   `Owned`/`must_free` em `src/runtime/teko_rt.tks` = 0 hits (é 0 hoje; tem de permanecer 0).

---

## 8. Riscos e tensões de lei

| risco | mitigação |
|---|---|
| **R1 — apagar a superfície `mem::free` quebra o drop de região** | NÃO: auditoria §0.1 — o drop de região emite `tk_region_new`/`tk_region_drop`(`_u`) DIRETO, nunca via `mem::free`. C2 remove o único chamador self-host antes de C3 apagar a soletração. |
| **R2 — remover o dataflow `#must_free` reabre um leak que ele barrava na corpus** | O único `#must_free` na corpus é o `Arena` (deletado, C4); nenhum outro sítio o usa. Sem site a re-proteger. Leaks residuais são leak-to-root (seguros, nunca UAF, por `modelo-de-memoria-por-escopo` §1). |
| **R3 — sweep de ~31 `is_unsafe =` + ~21 `must_free =` erra um literal** | O compilador REJEITA um literal de struct com campo em falta (erro de tipo) no dry build ANTES do fixpoint — a rede é o próprio checker. Não é reescrita silenciosa. |
| **R4 — `.tkb` shift de ordinal** | Não é fork (ruling do dono): `is_unsafe` trailing não desloca nada; bumpar `TKB_EXPR_VERSION`/`TKB_PROGRAM_VERSION` (procedimento R2). Reseed builda de FONTE → fixpoint indiferente. |
| **R5 — deletar o modificador contextual `unsafe` muda o parse de `teko::mem::unsafe` PATH** | MEDIDO: zero usos de `teko::mem::unsafe`/`mem::unsafe` como PATH/import na corpus (o `src/mem/unsafe/` é descoberto por diretório, não importado; e é deletado em C4). Remover o reconhecedor contextual não altera parse nenhum. Fork CLEARED. |
| **R6 — `teko_rt.tks` precisa de mecanismo novo para ser self-suficiente** | NÃO: já declara `tk_region_drop`/`tk_regions_free_all` host-edge; §6 exige ZERO edição ao runtime (§4). Fork CLEARED. |

**Tensão de lei residual: NENHUMA que force HALT.** Teko-only respeitado (produto em `.tks`; `teko_rt.c/.h`
intocados por §6). Issue-100%: os 8 crumbs entregam o §6 inteiro (reclassificar → retirar manual →
deletar `unsafe`), sem regressão. Achado adjacente (`assemble.tks:102`) REPORTADO, tratado em C2, não
convertido em issue. Bootstrap-safe: nada de idioma novo — só remoção, o seed anterior constrói gen1
igual até ao reseed final.

---

## 9. Forks genuínos (stop-and-report) — AMBOS CLEARED, sem HALT

O briefing (com os rulings do dono) reduziu os eixos de HALT a DOIS. Medição fecha ambos:

1. **`teko_rt.tks` self-suficiente exige mecanismo novo que os docs de região-como-escopo não dão?**
   **NÃO** (R6/§4): `tk_region_drop`/`tk_regions_free_all` já existem e servem o drop; §6 não toca o
   runtime.
2. **Consumidor de `unsafe` contextual cuja remoção muda o parse de `teko::mem::unsafe` PATH?** **NÃO**
   (R5): zero usos de path `teko::mem::unsafe` na corpus.

**Sem fork genuíno. Sem HALT.** O plano é executável na íntegra.

---

*Fonte: `mudancas-superficie-0.3.1.md` §6. Base ratificada: `memory-unsafe-backend-remodel.md`,
`arena-por-escopo-0.3.1.md`, `modelo-de-memoria-por-escopo-0.3.1.md`, `arena-especificacao-unica-0.3.1.md`
(Doc 1). Read+design apenas — nenhum código de produto, nenhum reseed.*
