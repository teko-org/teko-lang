# Port teko → mc — plano executável

Fonte de decisão: `DECISION_LOG.md` D211 (dono 2026-09-03). Repos no disco:
`teko` (este) e `mc` (`/home/user/schivei/mc`, `origin/main d358ae7`). Autoritativo do mc:
`mc/docs/plan.md`. Esta doc é para EXECUTAR — tabela densa + passos verificáveis, não leitura.

---

## 1. Contexto e decisão

O dono construiu o `mc` (minicompiler.dev, `schivei/mc`) em ~1 dia; o teko levou 3 meses e empacou
nos três "buracos de núcleo" (memória, multi-alvo, comptime). O `mc` **já é o endgame do teko
realizado limpo**: emite objeto direto (Mach-O `.o`/`--exe` auto-assinado SHA-256 em
`mc/src/backend_exe.mc`; ELF em `mc/src/backend_elf.mc`), **arena bump com doubling O(1) amortizado**
(`mc/src/arena.mc:xalloc`/`arena_chunk`), e é **extensível por diretiva em 3 camadas sem tocar o
`src/`**. O `examples/lang` prova uma linguagem inteira (classes/genéricos/interfaces/RC) ensinada
só por hooks.

**Decisão (D211):** finalizar o trabalho em voo do teko (FEITO — IR-builders Chunked), **CONGELAR a
campanha native**, e **PORTAR a superfície do teko como prelúdio/hooks do mc** quando o mc amadurecer.

O que o port DISSOLVE (não se porta — ver §5):

| Campanha do teko | Por que morre no mc |
|---|---|
| Memória (NO-PUSHES, 4-naturezas, arena-scoped, streaming, ratchet D68, purge-na-reatribuição) | doubling-arena mata O(n²) DENTRO da unidade **E** compilação separada (cada arquivo = TU, `namespace`=açúcar de `#include`) nunca acumula o programa todo. Pico = maior arquivo isolado, não a AST inteira residente. |
| §16 zero-libc / expurgo-libc | REVERTIDO. libc é lib do SISTEMA (glibc/musl, libSystem, kernel32/ntdll), não do compilador C. Chão irredutível = syscall do kernel; a libc mora acima. mc usa libs do SO + escolhe linker + sysroots (M25). Zero-libc = opção-piso (`_start` freestanding, `examples/minimal`), não dogma. |
| comptime/macro | APOSENTA. As diretivas do mc (Tier 1/2/3) SÃO o mecanismo de extensão que a macro tenta ser. Redundante. |
| DPS/região=param obrigatório (D130 regra 2) | DEMOVIDO a opcional. Bump-arena por-unidade libera no fim da unidade → o retornado morre junto → sem pressão de reclaim → DPS vira otimização (menos cópias), não fundação. O byte-mover de MAIOR RISCO sai da lista. |
| teko.c, backend native inteiro, Chunked<T> | muleta/infra descartada — o mc emite objeto de nascença. |

**GATILHO do port = M24 (floats)** fechar no mc. Fila do mc até lá:
**M17** (walker+máquina x86-64, Linux x64 — em curso) → **M19** (Win arm64) → **M20** (Win x64) →
**M25** (sysroots) → **M24** (f32/f64, `#machine`). M24 é o último; quando floats landar, os outros
já estão verdes. Não-bloqueadores pós-port: M18, M28-M30, M13, M33, M35, M36.

---

## 2. Tabela de cruzamento: milestone mc × teko

Veredito: **PROVADO** (rodando no mc hoje) · **MAPEIA** (existe hook, falta escrever o módulo teko) ·
**GATED-Mxx** (espera milestone) · **APOSENTA** (não se porta).

### Fase 1 do mc (M0–M13) — fundação, toda LANÇADA

| mc | Entrega | teko tem/precisa | Veredito |
|---|---|---|---|
| M0–M8 | stage0 C23 congelado + auto-host + ponto-fixo `mc1→mc2→mc3` byte-idêntico | teko tem fixpoint próprio (`gen2==gen3`) que **APOSENTA** — o fixpoint passa a ser o do mc | PROVADO (base) |
| M9 | `#rule` (macro higiênica statement) | equivalente a parte do `comptime`/açúcar do teko | PROVADO / teko APOSENTA macro |
| M10 | `pass()`/`backend()` (Tier 2) | passes de AST e backend do teko viram módulos `.mc` | MAPEIA (§3) |
| M11 | executável direto assinado, sem `ld` | é o endgame native do teko (`objfile_*`) realizado | PROVADO / teko APOSENTA native |
| M12 | Tier 3: `syntax`/`syntax_stmt`, `type_alias`, `#dylib`; api HTTP+SQLite | superfície de decl do teko (fn/type/class/namespace) | MAPEIA (§3) |
| M13 | dimensionar arena em compile-time (`--mem-report`) | **= D133/`#arena_size`** do teko (comptime-heap) | PROVADO-parcial (`mc limits` + tolerance já é isso) |

### mc lançado hoje além da Fase 1

| mc | Entrega | teko | Veredito |
|---|---|---|---|
| M14 | `mc build` + `mc.toml` (`[project]/[target]/[linker]/[externs]/[compiler]`) | driver de projeto do teko (`src/build/project.tks`) | MAPEIA → `mc.toml` |
| M15 | stdlib embarcada + `#include <name>` + `#embed` (bundle LZ no binário) | **= `#embed`/VFS D134** do teko (prelúdio self-contained) | PROVADO (mc `src/bundle.mc`+`lz.mc`) |
| M16 | Linux arm64 ELF + musl + `ld.lld` | um alvo do monólito-cross do teko | PROVADO (arm64); x64 é M17 |
| M21 | Tier 3 completo: `syntax_expr`, `syntax_infix`, record/replay | operadores + genéricos do teko | MAPEIA (§3, precedente lx) |
| M21.5 | `on_stmt`, `parse_block` via `syntax_stmt("{")`, `--compiler-only` | rastreio de escopo / RC do teko | PROVADO (lx usa) |
| M22 | `examples/lang` (lx): classes/generics/`where`+`const N`/`ref`/namespaces/RC | **o TEMPLATE do port** — cobre o grosso da superfície teko | PROVADO |
| M23 | limites dinâmicos + `[limits] tolerance` (comptime-heap) | **= D133** genérico | PROVADO |
| M26/M27 | docs + site (`mcsite` em mc) | docs do teko | GATED-pós (Fase 3) |
| M31 | `examples/conc`: threads/`spawn`/`intent`/`await`/`lock`/`chan`/atomics | concorrência do teko (`await`/`Intent<T>`, `chan`, `wait_group`, `src/threads`) | PROVADO |
| M32 | `examples/desktop`: GTK4 via `extern`+`&fn`+`[externs]` | FFI da stdlib teko (a mais pesada) | PROVADO (FFI+callbacks bidirecional) |
| M34 | `examples/minimal`: pisos por-alvo (`-nostdlib` `_start`) | zero-libc do teko como OPÇÃO-piso | PROVADO |

### Fila até o gatilho (bloqueadores do port)

| mc | Entrega | teko precisa porque | Veredito |
|---|---|---|---|
| **M17** | walker target-indep + tabela de máquina (~30 primitivas) + x86-64 SysV + ELF Linux x64 | monólito cross-compila → precisa de x64; arm64 fica byte-idêntico pós-refactor | GATED-M17 (em curso) |
| **M19** | Windows arm64: COFF + kernel32 + `lld-link` | alvo Windows do monólito | GATED-M19 |
| **M20** | Windows x64: relocs COFF + ABI Win64 | idem | GATED-M20 |
| **M25** | sysroots/cross-link (musl/mingw/Apple SDK, stubs `.tbd`) | teko emite TODOS os alvos numa build | GATED-M25 |
| **M24** | `f32`/`f64` + `#machine ARCH task ENCODING` | **stdlib teko exige float** (crypto/math/numeric) | GATED-M24 = **GATILHO** |

### Pós-port (não-bloqueadores)

| mc | Entrega | teko | Veredito |
|---|---|---|---|
| M18 | Linux x86-32 (opcional; `uptr`=4B quebra layout 8B/campo) | opcional | GATED, opcional |
| M28/M29 | `mc lsp` + extensão VS Code (LSP = compilador ensinado) | LSP do teko (`src/lsp`) reescreve sobre o mc | GATED-pós |
| M30 | DWARF em `.o`/`--exe` | debug info do teko | GATED-pós |
| M13 | arena compile-time genérica | **= D133** | PROVADO-parcial |
| M33 | WebAssembly (`wasm32`/`wasm64`, WASI+browser) | classe de alvo NOVA (teko nunca tentou) — de graça | GATED-pós |
| M35 | `<bench>`/`<memcheck>` (`--paranoid`) | = MEM_PARANOID do teko | GATED-pós |
| M36 | multi-target `[[targets]]` (uma build → todas as saídas) | **= lei "monólito que cross-compila"** | GATED-pós |

---

## 3. Mapeamento de superfície teko → mecanismo mc

Concreto, citando os hooks reais (`mc/src/hooks.mc`) e o precedente no `lx`
(`mc/examples/lang/*.mc`) / `conc` (`mc/examples/conc/*.mc`). Registros de hook:
`syntax`/`syntax_stmt`/`syntax_expr`/`syntax_infix`/`type_alias`/`on_stmt`/`on_jump` (Tier 3),
`pass`/`backend` (Tier 2), Tier 1 `#token/#infix/#prefix/#rule/#section/#opcode/emit/reloc/#dylib`.
Record/replay: `p_skip_balanced`/`p_push_source`/`p_subst_name`/`p_subst_int`/`p_resplit_punct`.

| Construto teko | Camada/hook mc | Precedente | Notas |
|---|---|---|---|
| primitivos `u8/u16/u32/u64/i64/uptr/void` | **core do mc** (7 words nativas) | `mc/docs/core-language.md` §Types | idênticos; `i64`=inteiro de trabalho |
| `str` | `type_alias("str", TY_UPTR)` + módulo `str` como fns | `lang.mc:39`, `oop.mc` (api) | **LANDADO (entrega 2) como NUL-terminated `uptr`** (`ngen/teko_type.mc:23`, `lib/rt.mc` `tk_str_len`/`tk_str_slice` — view por ponteiro, sem cópia, D197). A forma `{ptr,len}` do teko-clássico NÃO foi herdada (D215); o código vence esta tabela. |
| `char=u32`, `'x'`/`b'x'` (D198 D2) | `type_alias("char", TY_U32)` + `#token` p/ literal byte | `type_alias` (M12) | D205 (char fat vs `byte`) morre — no mc `char` é `u32` escalar, 1 word |
| `bool` | `type_alias("bool", TY_U8)` | `lang.mc:40` | comparações já dão 0/1 |
| `isize`/`usize` (D131) | `type_alias` sobre `i64`/`u64` | `type_alias` | 64-bit hoje; coerção implícita já é a regra |
| `ptr`/`uptr` + `wrap`/`unwrap` (D131) | `uptr` core + fns de reinterpret | core `uptr` opaco; `ld*/st*` | `wrap`/`unwrap` = reinterpret zero-custo → `ld64`/`st64` no offset; sem intrínseco mágico |
| `type`/struct/subtipo (campos+métodos, D196) | `syntax("class"/"struct", &f)` → `top_add` de decls achatadas | `lang_class.mc` (`lg_class`) | layout: vtable@0, campos base-first (`lang/README.md §Layout`) |
| `interface` (+ `error` interface D199) | `syntax("interface", &f)` + itab | `lang.mc:50` `lg_interface`; `Printable` | dispatch `rt_itab(ld64(obj),ID)`+`callp` |
| `trait` (D216 — modelo do PHP) | `syntax("trait", &f)`: corpo gravado por `p_skip_balanced`, re-parseado por classe via `p_push_source`; `use A, B;` lido no corpo do tipo, sem registro global | sem precedente no mc — desenho próprio, `ngen/teko_trait.mc` | flattening em compile-time pela mesma máquina de membros; NÃO é tipo (sem `type_new`); precedência classe > trait > base; conflito/ciclo = erro claro |
| genéricos `<T, const N: i64>` + `where` | record/replay: `p_skip_balanced`+`p_subst_name`/`p_subst_int`; `syntax_expr` na instanciação | `lang_type.mc`; `Box<Circle,4>` | `>>` fecha com `p_resplit_punct(1)`; constraint re-parseada na instância |
| constraint `&` (interseção) + form/lifetime (D199) | mesma máquina `where` do lx, estendida no MÓDULO | `lang_type.mc` (`where`) | `|`-disjunção MORREU no teko (D199) → nada a portar; só `&`/asserção |
| operador overload = método do tipo (D-2026-08-28) | `syntax_infix(op, prec, &f)` — resolve pelo método do tipo estático | `lang_expr.mc` `lg_dot`/`lg_index` (`.`,`[`) | operador-com-opcode fica core; sem-opcode = chamada de método genérica |
| `match`/`when` + destructuring | `syntax_stmt("match", &f)` + `on_stmt`; downcast de interface por `as` | `sd_unless` (padrão if); itab p/ downcast | sem união (D199) → match vira if-chain/downcast; multi-retorno+destructuring já é core |
| error-union `T \| error` → tríplice `(bool,T,error)` (D199) | multi-retorno + struct + `interface error` | `oop.mc` interface; retorno via slots | união ABOLIDA no teko → porta a FORMA NOVA (tríplice), não a máquina de união |
| `null<T>` opcional (D199) | `class null<T> { _value:T; _setted:bool }` via `syntax`/módulo | `lang_class.mc` | pânico automático por ponteiro-vazio (guard já existe no rt) |
| `namespace`/`import`/`using` + qualificado `a.b.X` | `syntax("namespace"/"import"/"using")` — açúcar de `#include`+mangling | `lang_class.mc` `lg_namespace`; `geo.lx` | merge por prefixo; `import`=`#include`+`using` once-only |
| visibilidade `exp`/`pub` | convenção de mangling + quais símbolos entram no `.tkh`/bundle | mangling do lx (`Owner_method`) | `exp`→símbolo emitido/no header; `pub`→interno. `.tkh` = bundle `#embed` (M15) |
| `service`/DI `ServiceLifetime{Singleton;Scoped;Transient}` (D130 r4) | `syntax("service")` + `pass()` + arena: Singleton→bss estático, Scoped→arena de escopo, Transient→por-chamada | arena por-unidade (`arena.mc`); região=param provada como exemplo (D211) | **candidato** — lx não exercita DI; hook claro, ver §7 |
| `spawn`/`intent`/`await`/`chan`/`lock` | `syntax_stmt`/`syntax_expr` + runtime; `on_jump` p/ liberar em toda saída | `conc_stmt.mc`; `on_jump`+`lock` | PROVADO integral no `examples/conc` |
| arena/região (`src/runtime/arena.tks`) | **core do mc** (`arena.mc` bump+doubling) | `xalloc`/`grow` | RC opcional por cima (lx `lib/rt.mc`); região=param vira opcional |
| I/O streaming (buffer ≤1024B) | `extern` syscalls + `Buf`/`io_write` | `arena.mc` `read_file`/`io_write` | mc já lê por chunk `RF_CHUNK`; sem materializar programa inteiro |
| FFI (crypto/os/gtk…) | `extern` + `&fn` + `[libs]`/`[externs]` + `#dylib` | `desktop/lib/gtk.mc`; `api` sqlite | callbacks bidirecionais provados |
| stdlib (`list`/`map`/`fmt`/`sort`/…) | módulos `.mc` de prelúdio (core-lang) + hooks onde precisa de açúcar | `lib/prelude.lx`, `lib/rt.mc` | ver §6 passo A5 (piloto) |

**Onde o teko tem algo que o lx ainda NÃO exercita, com hook candidato:**
- **DI-scoped/`service`** → `syntax("service")` + `pass()` de wiring + arena de escopo. Sem precedente
  lx, mas D211 já classifica "arenas injetadas + passagem por referência" como exemplo/hook provado.
- **`match` exaustivo com análise estática** → `syntax_stmt("match")` + itab de downcast; a exaustividade
  é lógica do módulo (o mc não valida, o módulo valida — filosofia "core aprende o mecanismo").
- **operador com sobrecarga por método de subtipo** → `syntax_infix` + tabela de tipo-estático do módulo
  (o lx faz isso pra `.`/`[`; estender pra `+`/`==` é código de módulo).

---

## 4. Já provado no mc (não re-provar)

| Exemplo | Prova | Arquivo:símbolo |
|---|---|---|
| **lx** | classes single-inherit, `virtual`/`override`, interfaces, genéricos `<T,const N>`+`where`, `ref`, namespaces, RC com free-lists (200k objetos em arena 4 MiB, `live()==0` `peak()==64`) | `mc/examples/lang/lang_class.mc`, `lang_type.mc`, `lang_stmt.mc`, `lib/rt.mc` |
| **conc** | threads, `spawn`/`intent`/`await` (sem `async`, steal-on-await), `lock` com `on_jump`, `chan` de objetos, mutex/gate, atomics LSE; módulo EMPILHADO sobre lx (`lg_more`) | `mc/examples/conc/conc_stmt.mc`, `lib/conc_rt.mc`, `lib/atomic.mc` |
| **desktop** | FFI GTK4 pesada: `extern`+`&fn` callbacks C↔teu-código, `[externs]` prefix→4 dylibs, DSL de UI por `syntax()` | `mc/examples/desktop/lib/gtk.mc`, `ui.mc`, `mc.toml` |
| **api** | HTTP + SQLite real, `class`/`interface`/`bool`/`str`, `#dylib` libsqlite3, saída `--exe` assinada | `mc/examples/api/oop.mc`, `mc-api.mc`, `main.mc` |
| **minimal** | pisos por-alvo, `-nostdlib` `_start` = zero-libc como OPÇÃO | `mc/examples/minimal/nolibc.mc`, `mc.nolibc.toml` |
| **mc limits** | comptime-heap: pré-scan estima, `[limits] tolerance` reserva `est*(1+tol)`, `mc limits` audita — **= D133/`#arena_size`** | `mc/src/limits.mc`, `mc/src/arena.mc` (registry `T_*`, `grow`) |

---

## 5. O que o teko APOSENTA (não portar — não gastar tempo em muleta)

| Aposentado | Onde mora no teko | Motivo |
|---|---|---|
| `comptime`/macro (maquinaria inteira) | checker/parser do comptime | diretivas mc (Tier 1/2/3) já são o mecanismo de extensão |
| Backend native inteiro | `src/lir/{lir,lower}.tks`, `src/backend/{isel_*,regalloc*,encode_*,minst*,objfile_*}.tks`, path native de `src/build/project.tks` | mc emite `.o`/`--exe` de nascença (`backend_exe.mc`/`backend_elf.mc`) |
| `teko.c` (muleta bootstrap) | `bootstrap/teko.c` | mc não tem muleta C; stage0 é o seed C23 do mc |
| §16 expurgo-libc / zero-libc absoluto | `plano-s16-expurgo-libc-completo.md`, `src/runtime/teko_rt.{c,h}`, `assert.{c,h}`, `win32_compat.h` | REVERTIDO: libc é lib do SO; syscalls feitas viram base do backend, o absolutismo cai |
| Máquina de memória | NO-PUSHES, 4-naturezas, arena-scoped (`residence.tks`), streaming forçado, ratchet D68, purge-na-reatribuição, região=param obrigatório | doubling-arena + compilação separada dissolvem tudo |
| DPS obrigatório (D130 regra 2) | codegen DPS / `cg_fn_needs_region` | demovido a otimização opcional |
| `Chunked<T>` (container D207) | `src/collections/chunked.tks` | criado só pra crescer array no native sem recopy; moot sem campanha de memória |
| Fixpoint `gen2==gen3`/`.o` do teko | scripts de reseed | substituído pelo ponto-fixo do mc (M7) |

**Regra:** a sessão local NÃO porta nenhum arquivo acima. O syscall/`_start` já escrito (`src/sys`,
crumbs native) é REFERÊNCIA de conhecimento (números de syscall, encoding), não código a migrar.

---

## 6. PRÓXIMOS PASSOS ordenados

### (A) AGORA, local, pré-M24 (preparatório) — cada passo com entregável verificável

**A1 · Buildar o toolchain do mc local.**
- Entregável: `mc/build/mc1` funcional + `make check` verde no host do mc (macOS arm64).
- Como: no repo mc, `make stage0` (clang C23 → `build/mc0`) → `make bootstrap` (`mc0 src/mc.mc→mc1`,
  fixpoint `mc1→mc2→mc3` `cmp` idêntico) → `make check`.
- CAVEAT (ver §7): a máquina de dev do teko é **Linux**; o mc hoje self-hospeda em **macOS arm64**
  (Linux x64 é M17, em curso). Até M17, autorar módulos `.mc` é portável (texto), mas VALIDAR o
  compilador-ensinado precisa do binário mc no host. Rodar a validação no host macOS OU esperar M17.
- Verificação: `mc1 build examples/lang && examples/lang/build/lang-demo` imprime `13,25,12,box,0`.

**A2 · Catalogar a superfície `exp` do teko (o inventário do port).**
- Entregável: `docs/design/port-surface-inventory.md` — tabela de TODA decl `exp` (fn/type/método)
  por módulo, com assinatura, agrupada em {primitivo, tipo/classe, operador, stdlib, FFI}.
- Como: grep `^exp ` / `exp global` / `exp fn` / `exp type` na árvore `src/` (módulos §1: base, list,
  collections, str, map, crypto, math, numeric, encoding, sort, cmp, iter, fmt, text, io, fs, time,
  regex, process, env, threads). Marcar cada linha com o hook mc destino da §3.
- Verificação: contagem por módulo bate com a superfície exportada; cada entrada tem hook atribuído.

**A3 · Esqueleto "teko-como-exemplo-mc" espelhando `examples/lang`.**
- Entregável: no repo mc, `examples/teko/` com: `teko.mc` (registro `user_init`, espelho de
  `lang.mc`), `teko_tab.mc`/`teko_util.mc`/`teko_type.mc`/`teko_class.mc`/`teko_stmt.mc`/`teko_expr.mc`
  (skeletons com doc-comment + honest-stop), `lib/rt.mc` (arena/RC/print — clone de `lang/lib/rt.mc`),
  `mc.toml` (`[compiler] modules=["teko.mc"]`), `tests/*.tk` mínimos.
- Como: copiar a estrutura do `lang` e substituir os handlers por stubs que compilam (registro
  vazio + `die("teko: <construto> not taught yet")`).
- Verificação: `mc1 build examples/teko --compiler-only` produz o compilador-ensinado sem erro
  (registra hooks, nenhum ainda implementado).

**A4 · Prototipar lexer/parser/tipos do teko como hooks contra os milestones JÁ lançados.**
- Entregável: os handlers dos construtos PROVADOS no lx (M12/M21/M22): `fn`, `type`/`class`,
  `interface`, genéricos, `namespace`/`import`/`using`, `ref`, operadores `.`/`[`, `type_alias`
  de `str`/`char`/`bool`. Reusar `lang_*.mc` como ponto de partida (a semântica teko diverge no
  detalhe, não na mecânica).
- Como: por construto, um commit; validar isolado (`mc1 build examples/teko` + um `.tk` fixture).
- Verificação: um `.tk` com classe+interface+genérico+namespace compila e roda o exit esperado,
  espelhando `main.lx`.

**A5 · Portar 1 módulo stdlib piloto (`list` OU `str`) como prelúdio.**
- Entregável: `examples/teko/lib/list.tk` (ou `str.tk`) escrito na superfície teko-sobre-mc,
  compilado pelo compilador-ensinado de A3/A4.
- Escolha: **`str`** primeiro (é `{ptr,len}` de bytes → casa direto com `uptr`+`ld*`; exercita
  `slice_view` sem cópia = valida a semântica-preservada D197). `list` depois (exercita genéricos).
- Verificação: fixture que usa `str.len`/`str.slice`/concat roda; comparar bytes de saída contra o
  oráculo teko atual; rodar sob o ponto-fixo do mc (o `.o` reproduz byte-idêntico em 2 emissões).

### (B) GATED em M24 / migração

**B1 · Portar crypto/math/numeric (dependem de float).** GATED-M24. Módulos `src/crypto/*`,
`src/math/*`, `src/numeric/{bigint,dec}` — precisam de `f32`/`f64` + AAPCS64/SSE2 float ABI. Portar
só quando M24 landar `f32/f64` + `#machine`.

**B2 · Emissão multi-alvo.** GATED-M17→M25. Cada alvo do monólito teko (`arm64/x64 × linux/win/mac`)
mapeia num `[target]`/`[[targets]]` do `mc.toml`; a saída = `backend_elf`/COFF/Mach-O do mc. M36
fecha o "uma build → todas as saídas".

**B3 · Self-host completo do teko-sobre-mc.** GATED-B1/B2. O compilador teko reescrito como módulo(s)
`.mc` compila o PRÓPRIO `src/` teko portado.

**B4 · Fixpoint do teko-sobre-mc.** GATED-B3. `teko-mc` compila `teko-mc` → objeto reproduz
byte-idêntico (o ponto-fixo do mc M7 aplicado ao compilador teko portado). Substitui o `gen2==gen3`
do teko em definitivo.

---

## 7. Riscos residuais / questões abertas

Depois de ler os dois repos, a superfície do teko tem caminho claro no mc em quase tudo. Pontos que
merecem registro (nenhum é fork aberto — todos deliberados ou meros caveats de execução):

1. **Gap de host (Linux × macOS) — CAVEAT, não fork.** O mc self-hospeda em macOS arm64 hoje; a
   máquina de dev do teko é Linux. Autorar os módulos `.mc` é portável, mas VALIDAR o compilador-
   ensinado exige o binário mc no host. Resolve-se com **M17** (Linux x64, em curso) — já está na
   fila-gatilho, não precisa decisão do dono. Enquanto isso, A1–A5 validam no host macOS ou via
   docker linux/arm64 (M16 já roda lá).

2. **Superfície-alvo do port é a PÓS-D199 (união abolida), não a atual em voo.** O teko está no meio
   da abolição de união (`T|error`→tríplice `(bool,T,error)`, `Variant` aposentado). O port deve
   mirar a FORMA NOVA (§3): tríplice via multi-retorno + `interface error`, `null<T>` classe. NÃO
   portar a máquina de união (InlineTag/Box/union-injection). Deliberado em D199 — não é fork.

3. **DI-scoped/`service` sem precedente lx — candidato, não bloqueador.** `ServiceLifetime`
   {Singleton;Scoped;Transient} não é exercitado por nenhum exemplo mc. O hook é claro
   (`syntax("service")` + `pass()` + arena: Singleton→bss, Scoped→arena de escopo, Transient→
   por-chamada), e D211 já classifica "arenas injetadas + passagem por referência" como exemplo/hook
   provado. Fica como o construto de MAIOR incerteza de implementação (não de design) — vale um
   spike próprio depois de A4, espelhando o padrão de `on_jump`+arena do `conc`.

4. **`.tkh` (header/ABI do compilador) → bundle `#embed`.** O teko queria emitir `.tkh` junto do
   binário (intellisense/extensão). No mc isso é o bundle `#include <name>`/`#embed` (M15) já pronto
   — a superfície `exp` do teko-mc vira uma entrada de bundle. Sem trabalho novo; anotar no A2.

**Não encontrei construto do teko sem caminho no mc.** Tudo que a superfície produz mapeia num hook
das 3 camadas ou no core do mc, com precedente em lx/conc/desktop/api — ou aposenta explicitamente
(§5). Nenhum FORK genuíno a levar ao dono além dos caveats acima (todos já deliberados em
D211/D199/D130/D134).
