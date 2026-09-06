# Plano — emissão de objeto nativo `.o → executável` na superfície ATUAL (0.3.1)

> **Papel:** arquiteto (SÓ design/levantamento; nenhuma linha de produto tocada). Base:
> `origin/fix/retirement` HEAD `a68f15e7`. Branch `arch/emissao-objeto-nativo`.
>
> **Escopo:** o dono decidiu (2026-09-01) que o `/src` passa a mirar **emissão de `.o` nativo
> direto + link → executável rodável**, na **superfície ATUAL** por ora (a superfície nova do
> `ngen/` vem depois (⚠️ SUPERSEDIDO por D212: `ngen/` descartado, vai pro mc)). Este doc é o LEVANTAMENTO do que falta, ponta-a-ponta, para o alvo do
> fixpoint (**x86_64 Linux / ELF**), com os deltas anotados para arm64 / Mach-O / COFF / Windows.
> NÃO é começar do zero: há andaime native substancial — este doc mapeia
> **implementado / stub / dormente / ausente** com `arquivo:linha`.

---

## 0. Superseção do gate native pelo dono (2026-09-01) — REGISTRAR

O `CLAUDE.md` trazia o **gatilho tríplice** que deferia o native ("NATIVE É A ÚLTIMA ETAPA —
gated em MEMÓRIA ESTÁVEL": build seco ≤ 1,5 GB · fixpoint gen2==gen3 · testes verdes). **O dono
SUPERSEDEU esse gate em 2026-09-01** — o backend native **vem para a frente** para a emissão de
objeto, sem esperar o marco de memória. Implicações que permanecem em vigor:

- **Ratchet D68 continua governando** o que for efetivamente buildado: um crumb do native não
  pode CRESCER o pico da linha canônica `teko: memory: peak <N> MB` do build que ele exercita.
- O **alvo/marco de memória continua** (agora `< 2 GB` de pico, por complemento do dono
  2026-09-01; era `< 1 GB` RSS na campanha) — reduzir a memória da rota native é **item de
  lacuna** deste plano, não um pré-requisito para começar.
- A regra "adiantar o que der" (D154/D155) vale: o pré-requisito (o piso de syscall) vem ANTES do
  resto, feito direito — sem ponte transitória.

*(O coordenador faz a entrada D formal no `DECISION_LOG.md`; aqui só se registra a superseção.)*

---

## 1. Estado atual — mapa da cadeia `lower → isel → regalloc → encode → objfile → linker → runtime-floor`

Veredito por peça (verificado em `a68f15e7`):

| Peça | Estado | Sítio | Nota |
|---|---|---|---|
| **LIR / IR nativa** | IMPLEMENTADO (subset N1/N2) | `src/lir/lir.tks`, `src/lir/lower.tks` (6881 L) | rebaixa o subset da linguagem; ~dezenas de **honest-stops N1/N2** mapeados (§2) |
| **Modelo por-escopo no native** | PARCIAL — dirige elisão de arena | `lower.tks:3383,3493,4327,…` consultam `checker::scope_slot_count(...)==0` (regra 7 do D130); `open_native_region` | a residência (`residence.tks`) alimenta a decisão slots==0; o resto da região ainda é `tk_region_*` extern |
| **isel x86_64 / arm64** | IMPLEMENTADO (subset) | `src/backend/isel_x86_64.tks` (813 L), `isel_arm64.tks` (914 L) | seleção de instrução do subset; variádica não suportada (`isel_arm64.tks:738`) |
| **regalloc** | IMPLEMENTADO | `src/backend/regalloc.tks` (1113 L), `regalloc_x86.tks` | aloca sobre `MFunc`/`MFuncX86` |
| **encoder x86_64** | IMPLEMENTADO (subset) — **SEM `syscall`** | `src/backend/encode_x86_64.tks` (997 L) | **NÃO há opcode `SYSCALL` (0F 05)** — grep zero; lacuna §3 crumb 1 |
| **encoder arm64** | IMPLEMENTADO (subset) — **SEM `svc`** | `src/backend/encode_arm64.tks` (1310 L) | idem: sem `svc #0` |
| **objfile ELF** | IMPLEMENTADO, razoavelmente completo | `src/backend/objfile_elf.tks` (840 L) | seções `.text/.rodata/.symtab/.strtab/.shstrtab/.rela.text/.data.rel.ro/.rela.data.rel.ro` + DWARF; relocs `R_X86_64_64/PC32/PLT32` (`:191-203`); sem timestamp (determinista) |
| **objfile Mach-O** | IMPLEMENTADO (subset) | `src/backend/objfile_macho.tks` (513 L) | — |
| **objfile COFF** | IMPLEMENTADO (subset) | `src/backend/objfile_coff.tks` (368 L) | — |
| **objfile ar (.a)** | IMPLEMENTADO | `objfile_ar.tks`, `objfile_ar_coff.tks`, `objfile_ar_macho.tks` | símbolos ordenados (determinista); auditar `mtime`/`mode` (§6) |
| **DWARF** | IMPLEMENTADO (linhas) | `src/backend/dwarf.tks` (635 L) | `--debug=lines` |
| **"linker interno" (`src/build/linker.tks`)** | **NÃO é linker** — só RESOLVE um `ld` externo | `src/build/linker.tks` (132 L) | `resolve_linker` escolhe `ld.lld`/`ld`/`lld-link`/`ld64.lld`; **nenhuma disposição de seção / aplicação de reloc / escrita de executável** |
| **Link direto (`link_object_*_direct`)** | IMPLEMENTADO — mas **NÃO de-C** | `src/build/project.tks:986-1178` | invoca `ld` externo + **compila `teko_rt.c`+`assert.c` a `.o` via `cc`** (`compile_runtime_objects:1067`) + linka **glibc** (`-lc`, `Scrt1.o`, `crti.o`, `crtn.o`, `-dynamic-linker`) `-pie` |
| **Entry native** | STUB estilo-C, **não freestanding** | `lower.tks:6835 wrap_native_entry` → `native_entry_stub:6868` | emite `main(argc,argv)` que chama `tk_set_args` (teko_rt.c) + vmain + máscara de exit; depende do `_start` do CRT glibc |
| **`_start` per-OS / `stack_ptr`** | **AUSENTE** (esqueleto only) | `native-lowering-cobertura-zero-libc-0.3.1.md:119` (crumb 0125 pendente) | o `_start` zero-libc do D130 não existe |
| **Piso de runtime: `syscallN` native** | **AUSENTE** (honest-stop) | `lower.tks:2154 unresolved_builtin_stop` | **primeiro hard-stop empírico** (§2.1) |
| **Seleção de rota C×native** | env `TEKO_BACKEND` | `project.tks:1287-1294 backend_of`/`c_backend_selected` | `TEKO_BACKEND=c` → C; senão Native. Alvo via `native_target()` |

**Resumo em 6 linhas:** a rota native já é uma cadeia real e razoavelmente madura de
`lower→isel→regalloc→encode→objfile` que **emite um `.o` ELF válido e determinista hoje**. O que
NÃO existe é o **fecho de-C**: (1) o piso de runtime `syscallN` não rebaixa no native (para o
build antes de emitir `.o`); (2) o "linker interno" é só um resolvedor de `ld` externo — o link
real delega ao `ld` do SO e **ainda compila `teko_rt.c`/`assert.c` por `cc` e linka glibc**; (3) o
entry é um `main(argc,argv)` estilo-C dependente do CRT glibc, não um `_start` freestanding. Além
disso o subset de lowering N1/N2 é incompleto por design (dezenas de honest-stops mapeados). O
modelo por-escopo já dirige a elisão de arena no native (`scope_slot_count`).

---

## 2. Achado empírico — build seco `TEKO_BACKEND=native` (gen0 do seed `bootstrap/teko.c`)

Ladder do seed: `CC=clang scripts/build_gen1_from_c.sh bootstrap/teko.c src <out>` → `teko` gen1
(13.8 MB, OK). Projeto mínimo `hello` (`kind=binary`), build seco
`TEKO_BACKEND=native TEKO_CC=clang --no-verify`, subshell `ulimit -v 8388608` (8 GiB, exceção de
dev do dono 2026-09-01 só para medir).

### 2.1 Primeiro hard-stop da cadeia — `syscall6` no `ar_mmap`

Programa `var a: i32 = 40 / var b: i32 = 2 / var c: i32 = a + b` (bare statements, zero stdlib).
Frontend passa inteiro (lexer/parser/checker 628 itens / monomorph / consteval), entra no lowering
native e PARA em:

```
native backend N1: builtin `syscall6` not yet lowered (N2) [in `teko::runtime::ar_mmap`]
```

Causa-raiz exata: `src/runtime/arena.tks:173` chama `teko::sys::syscall6(SYS_MMAP, …)`; a arena
migrou para Teko-sobre-syscall (D128/D148, `plano-s16-arena-mmap.md`), então **todo programa
native** aterrissa em `ar_mmap` → `syscall6`. No `lower.tks`, `syscall0..6`/`ptr_word`/`ref_word`
**não são rebaixados** — caem no fallthrough genérico `unresolved_builtin_stop` (`lower.tks:2154`;
`native_builtin_symbol:2106` não os cobre). O `load_u8`/`store_u8`/`load_u64` JÁ rebaixam
(`lower.tks:1836,1866,1881`) — mas `syscall*` não.

> **DRIFT (achado de scout) a registrar:** `docs/design/native-lowering-cobertura-zero-libc-0.3.1.md:122`
> marca `load_u8/store_u8, syscallN, atomics` como **FEITO**. Isso é FALSO para `syscallN` no tip
> atual — só `load_u8/store_u8` estão feitos; `syscallN` honest-stopa. O doc agrupou errado ou o
> trabalho foi revertido. **A verdade de campo (build empírico) manda.**

### 2.2 Medição de pico native — NÃO obtenível hoje

A linha canônica `teko: memory: peak <N> MB` é emitida ao FIM do backend/codegen. Como o build
native **para no lowering** (antes do encode/objfile), **nenhuma linha de pico é emitida** — o pico
de um build native COMPLETO não é medível no tip atual até o crumb 1 (syscall) landar. O frontend
(lexer→consteval) é COMPARTILHADO com a rota C e seu pico já é rastreado pelo build seco canônico.
**Item de lacuna:** assim que o native completar um `hello`, medir e reportar o pico; o ratchet D68
passa a valer sobre ele (alvo `< 2 GB`).

### 2.3 Nota sobre a resolução de stdlib no projeto de usuário (adjacente, reportado)

Um `teko::io::println("x")` num projeto de usuário deu `unknown function: println` no checker (não
chegou ao backend). É ortogonal ao eixo native (é resolução de prelúdio de projeto de usuário) —
**reportado, não vira issue minha.** Para exercitar o native usei bare statements sem stdlib.

---

## 3. Lista de lacunas ORDENADA — degrau mínimo `x86_64 Linux ELF: fonte → .o → executável rodável`

Ordem = o caminho mínimo end-to-end na superfície atual. Cada item é gate-able isolado.

1. **[PISO] `syscallN` native (o hard-stop).** `lir.tks` ganha um op de syscall (ou o lowering
   emite via um caminho de instrução); `lower.tks` rebaixa `teko::sys::syscall0..6` +
   `ptr_word`/`ref_word`; `isel_x86_64` seleciona; `encode_x86_64` emite `0F 05` (`SYSCALL`) com
   os args nos registradores SysV (nr=`rax`, `rdi rsi rdx r10 r8 r9`, ret `rax`, clobber
   `rcx r11`). **Desbloqueia `ar_mmap` → todo programa native.** É o carve-out legítimo de
   primitiva irredutível (D134/D161: `syscall` é o chão do SO declarado como superfície).
2. **[PISO] `ptr_word`/`ref_word` reinterpret native.** Bridge endereço→`i64` para os args de
   ponteiro do syscall (`plano-s16-syscall-intrinsic.md §1.2`). Reinterpret-class (D131), sem cópia.
3. **[PISO] Entry `_start` freestanding + `stack_ptr` (crumb 0125 pendente).** Substituir o
   `native_entry_stub` estilo-`main(argc,argv)`+`tk_set_args` por um `_start` per-OS zero-libc que
   (D130) abre a região root e a passa a `main`, lê `argc`/`argv` do stack inicial
   (`stack_ptr`→`mov reg,%rsp`), e sai por `SYS_exit_group`. Remove a dep do CRT glibc.
4. **[PISO] Desligar o teko_rt.c/assert.c/glibc do link native.** Enquanto os builtins native
   ainda roteiam a `tk_rt_*`/`tk_*` C (`native_builtin_symbol:2106` → env/host/arena/int_to_str/
   str_of_bytes/peak_rss…), o `.o` referencia símbolos C e o link precisa de `teko_rt.o`. Migrar
   cada família para o corpo Teko (a arena já é Teko; o restante segue o mapa de
   `nativo-sem-teko-rt.md` §4, ordem: puras-de-bytes → alocantes → float). Meta honesta: piso
   mínimo de syscall, nada de lógica pura acima dele.
5. **[LINK] Link direto sem `cc` de runtime.** `link_object_elf_direct` (`project.tks:1089`) hoje
   compila `teko_rt.c`/`assert.c` por `cc` e linka glibc. Com os itens 3-4, o `.o` do compilador
   basta: linkar `objfile` + (se ainda houver) os `.o` de runtime **já emitidos por nós**, `ld`
   externo em `-static` (sem `-lc`, sem `Scrt1.o`). Passo transitório aceitável: `ld` do SO junta
   nossos `.o`; o linker INTERNO (item 8) é endgame.
6. **[SUBSET] Completar o lowering N1/N2 por família** — para o **compilador se auto-emitir**
   native (não só o `hello`). Backlog mapeado (não re-enumerar aqui): união/nullable (o maior,
   `str`-PrimKind ×17), fat/slice, operador inteiro (×8), push de agregado, `float::parse`
   (result-class XMM). Fontes: `docs/memory/bulk-native-verdicts-0.3.1.md`,
   `docs/memory/0.3.1.0-linux-native-first-stop.md`, `recon-native-n1n2-gaps-strategy.md`. Cada
   família é aditiva, byte-idêntica nas 6 pernas, gate-able só.
7. **[MEM] Medir + baixar o pico native.** Assim que o `hello` completa (item 1), medir
   `memory: peak`; ratchet D68 sobre a rota native; alvo `< 2 GB`. Os acumuladores `[..x, y]` em
   `project.tks` (`copy_*_to_current_region`, `emit_native_x86`) são dívida NO-PUSHES a converter.
8. **[ENDGAME — fora do 1º degrau] Linker interno + fixpoint de objeto.** O linker próprio
   per-namespace (`terminal-native-tkb-linker-0.3.1.md`, RM-C15/16/17, decisões D67 ratificadas) e
   a migração do fixpoint para `gen2.o==gen3.o` (§6). Gated; NÃO é o degrau mínimo.

**Degrau mínimo end-to-end = itens 1-5** (com o item 6 no mínimo suficiente para o programa-alvo).
Para o `hello` bare-statement, itens 1-3 + 5 já produzem executável rodável de-glibc; o item 4 é o
que torna o `.o` verdadeiramente de-C.

---

## 4. Sequência de crumbs (ordenada, bisectável)

Cada crumb baixa-ou-não-cresce o ratchet; os pontos de ritual exigem o gate completo (fixpoint
gen2==gen3 byte-idêntico + 3 harnesses C `scripts/*_test.sh` + ASan+UBSan limpo + grep zero-ref —
D163/D166/D185).

### Crumb 1 — `syscallN` native (o keystone do piso)
- **Onde:** `src/lir/lir.tks` (novo op), `src/lir/lower.tks` (`call_symbol`/`native_builtin_symbol`
  → braço syscall; NÃO cair em `unresolved_builtin_stop:2154`), `src/backend/isel_x86_64.tks`,
  `src/backend/encode_x86_64.tks` (opcode `0F 05`).
- **Assinaturas (Teko, superfície JÁ existe em `teko::sys`; só o lowering falta):**
```teko
/**
 * lower_syscall_call — rebaixa `teko::sys::syscallN(nr, a0..a{N-1})` para a primitiva de syscall
 * do alvo (x86_64: instrução `SYSCALL`, nr em `rax`, args `rdi rsi rdx r10 r8 r9`, resultado em
 * `rax`, clobber `rcx r11`). É o carve-out irredutível do chão do SO (D134/D161): `syscall` tem
 * identidade de superfície, não é name-detect escondido.
 *
 * @param c    a chamada tipada cujo último segmento é `syscall0`..`syscall6`
 * @param n    a aridade (0..6) derivada do nome do builtin
 * @return     o vreg de resultado (`i64`) ou um honest-stop nomeado para aridade fora de 0..6
 * @since 0.3.1
 */
fn lower_syscall_call(ctx: LowerCtx, c: checker::TCall, n: u64): LoweredExpr | error
```
- **Fixtures** (`examples/regressions/`, exit nativo): `sys_exit_group` (§5 do
  `plano-s16-syscall-intrinsic.md`) → **exit 42**; `sys_write_hello`
  (`syscall3(SYS_WRITE,1,ptr_word(as_cstr("hi\n")),3)` + `exit_group(0)`) → **exit 0** + `hi` em
  stdout (exercita `ptr_word`).
- **Ritual:** gate completo (é compiler-core: muda `lower`/`isel`/`encode`). Reseed.

### Crumb 2 — `ptr_word`/`ref_word` native reinterpret
- **Onde:** `lower.tks` (reinterpret endereço→`i64`, zero cópia, classe D131). Dobra no crumb 1 se
  a fixture `sys_write_hello` precisar (recomendado juntar).
- **Fixture:** coberto por `sys_write_hello`.

### Crumb 3 — `_start` freestanding + `stack_ptr` (crumb 0125)
- **Onde:** `lower.tks` (`wrap_native_entry`/`native_entry_stub` → `_start` LFunc per-OS),
  `isel_x86_64` (`stack_ptr`→`mov reg,%rsp`), sem `tk_set_args`.
- **Assinatura:**
```teko
/**
 * native_entry_start — emite o `_start` freestanding zero-libc do alvo: abre a região root
 * (D130), lê argc/argv do stack inicial via `stack_ptr`, chama `main`, e termina por
 * `SYS_exit_group(status)`. Substitui o stub estilo-`main(argc,argv)`+`tk_set_args` (CRT glibc).
 *
 * @param m       o módulo LIR com o vmain já renomeado
 * @param target  o alvo native (define o número de SYS_exit_group e o registrador de stack)
 * @return        o módulo com o `_start` acrescentado, ou honest-stop no alvo ainda não validado
 * @since 0.3.1
 */
fn native_entry_start(m: LModule, target: NativeTarget): LModule | error
```
- **Fixture:** `native_start_exit` (`_start` → `main` retorna 7 → processo sai 7), sem glibc.
- **Ritual:** gate completo. Reseed.

### Crumb 4 — link `-static` sem `cc`/glibc (x86_64 ELF)
- **Onde:** `src/build/project.tks:1089 link_object_elf_direct` — novo caminho quando
  `syscall+_start` cobrem o runtime: `ld -static` juntando só os `.o` nossos, sem
  `Scrt1.o/crti.o/crtn.o/-lc/-dynamic-linker`; `compile_runtime_objects` só enquanto restar C.
- **Fixture:** `native_static_hello` roda o binário e lê `$?` (nunca `teko test .`).

### Crumb 5+ — completar N1/N2 por família (união/nullable primeiro)
- Backlog em `docs/memory/*`; cada família aditiva/byte-idêntica; gate-able só; ritual por família.

### Crumb N — medir/baixar pico native (contínuo, ratchet D68)

---

## 5. Deltas por alvo (o que muda além de x86_64 Linux ELF)

| Alvo | Piso de syscall | Entry | Objfile | Link | Estado |
|---|---|---|---|---|---|
| **x86_64 Linux ELF** (alvo do fixpoint) | `SYSCALL` `0F 05`, `rax`+`rdi rsi rdx r10 r8 r9` | `_start` naked + `SYS_exit_group` | `objfile_elf` (pronto) | `ld -static` | **degrau primário** |
| **arm64 Linux ELF** | `svc #0`, `x8`(nr)+`x0-x5` | `_start` naked arm64 | `emit_elf_arm64` (pronto) | `ld -static` aarch64 | encoder arm64 sem `svc` (adicionar); isel arm64 sem variádica (`isel_arm64.tks:738`) |
| **arm64 macOS Mach-O** | **NÃO é raw-syscall** — bind `libSystem.dylib` via `extern fn`/native-lib (Apple não dá ABI de syscall estável — `plano-s16-syscall-intrinsic.md §9`) | entry Mach-O (dyld) | `objfile_macho` (pronto) | `ld64` + `-lSystem` + `Info.plist` (`project.tks:1137`) | **NÃO freestanding** por design; usa libSystem |
| **x86_64 Windows COFF/PE** | **NÃO é raw-syscall** — bind `kernel32.dll`/`ntdll.dll` via `extern fn`/native-lib | entry PE | `objfile_coff` (pronto) | `lld-link` + `libcmt` (`project.tks:1166`); MinGW PROIBIDO (`linker.tks:64`) | usa kernel32/ntdll |

**Segunda onda (FORA do 1º degrau):** `.so`/`.dylib`/`.dll` dinâmico — hoje honest-stop
(`project.tks:1214 "shared library output … not yet implemented"`). Exige `dynsym`/PLT/GOT/`DT_*`
— **fora do primeiro degrau** (o degrau é estático/relocável → executável). O linker INTERNO
(`terminal-native-tkb-linker`, RM-C15/16/17) também é segunda onda.

---

## 6. Nova forma do fixpoint — objeto native reproduzível

Hoje: `gen2.c == gen3.c` (o `teko.c` emitido byte-idêntico). Endgame (RM-C16,
`terminal-native-tkb-linker §8`): **`gen2.o == gen3.o`** (por unidade + binário juntado).
Pré-condições de determinismo do `.o`, a auditar em TODO `objfile_*` + `objfile_ar`:

- **Sem timestamp.** `objfile_elf` já não embute mtime (verificado — o header ELF não carrega
  data). **AUDITAR `objfile_ar`:** `ar_header` (`objfile_ar.tks:56`) tem campos `mtime`/`mode`; o
  `ar` clássico embute mtime — tem que ser fixo (0) para reproduzir.
- **Ordem estável de símbolos/seções.** `objfile_elf` separa `STB_LOCAL`/`STB_GLOBAL`
  deterministicamente (`objfile_elf.tks:36-127`); `objfile_ar` já ordena
  (`coff_lib_sorted_symbols`/`bsd_sorted_symbols`). Confirmar que a ordem vem de chave ORDENADA,
  não de iteração de `map`/`hashset`.
- **Sem path absoluto embutido.** Auditar DWARF (`dwarf.tks`) e nomes de seção/arquivo.
- **Relocations em ordem canônica; padding zerado.**
- **Mapeamento visibilidade → tabela de símbolos:** `exp`+`pub` = símbolo GLOBAL (`!local`);
  privado = local/`static` (nem entra). `Symbol.local` (`objfile_elf.tks`) é o slot.

Remoção da muleta C: só quando as 4 pernas native do CI fecham verde E o objeto reproduz — aí
`teko.c`+`cc` saem, as 2 pernas C viram native, e o reseed do bootstrap passa a ser o objeto/binário
native.

---

## 7. Riscos + pontos de maior risco byte-mover

- **[byte-mover MAIOR] `syscall` + `_start`** (crumbs 1-3): mexem em `lir`/`lower`/`isel`/`encode`
  — a lógica é compartilhada pelas 6 pernas; uma emissão errada de opcode quebra as 6 de uma vez.
  Gate ASan+UBSan obrigatório (D166 — a classe de UB de memória que o build seco esconde). Reseed.
- **`"memory"` clobber no syscall** (equivalente native do §8 do syscall-intrinsic): o backend tem
  que ordenar o store do buffer ANTES da instrução `SYSCALL`, senão o alocador reordena e corrompe
  (a fixture `sys_write_hello` com checagem de stdout pega a regressão).
- **Convenção de errno raw** (`< 0` = `-errno`): a primitiva devolve `i64` cru; os wrappers `.tks`
  fazem o `< 0 → error`. Um wrapper que esquece trata `-errno` como comprimento gigante.
- **Ratchet de memória** (D68): o native não pode CRESCER o pico do build que exercita. Os
  acumuladores `[..x, y]` em `project.tks` (`copy_reloc_to_current_region_*`,
  `commit_rodata_delta`, `emit_native_x86` fold) são copy-grow banido (NO-PUSHES) — dívida a
  converter para pré-alocação/4-naturezas antes que inflem o pico native.
- **Determinismo do encoder** (§6): qualquer não-determinismo (ordem de `map`, mtime do `ar`)
  quebra `gen2.o==gen3.o`; causa-raiz no writer, nunca critério relaxado.
- **Subset N1/N2 incompleto** para o auto-build: o compilador inteiro em native ainda honest-stopa
  em muitas famílias (mapeadas). O degrau mínimo (`hello`) não precisa delas, mas o **fixpoint
  native** precisa do subset completo — é trabalho por família, sequenciado.

---

## 8. Forks genuínos abertos (para o dono, se houver)

Nenhum HALT — o rumo está deliberado (superseção do gate + docs terminais + D67/D130/D134/D148).
Dois pontos de confirmação (NÃO impasses; o mais-recente-vence já resolve, mas registro):

1. **DRIFT do `native-lowering-cobertura-zero-libc-0.3.1.md:122` ("syscallN FEITO")** contra a
   verdade de campo (honest-stopa). Não é fork de design — é doc desatualizado; a verdade empírica
   manda. Reportado para o coordenador corrigir/anotar o doc.
2. **Freestanding `_start` de-glibc AGORA vs. transição via CRT.** O D130 (`_start` per-OS
   zero-libc) e o D148 (ZERO C novo) já decidem: freestanding é o rumo, e a lei "adiantar o que der"
   (D154/D155) diz para fazer o `_start` direito em vez de uma ponte glibc transitória. **Sem fork**
   — a sequência dos crumbs 3-4 já implementa o rumo decidido. Registrado por ser o ponto de maior
   mudança de comportamento (o binário native deixa de linkar glibc).

---

## 9. Âncoras (verificadas em `a68f15e7`)

| o quê | arquivo:linha |
|---|---|
| primeiro hard-stop empírico (syscall6 em ar_mmap) | `src/runtime/arena.tks:173` + `src/lir/lower.tks:2154` |
| builtin native routing (o que rebaixa) | `src/lir/lower.tks:2106 native_builtin_symbol`, `:2130 call_symbol` |
| entry estilo-C (não freestanding) | `src/lir/lower.tks:6835 wrap_native_entry`, `:6868 native_entry_stub` |
| link direto ELF com glibc+cc | `src/build/project.tks:986-1106` (`link_object`, `link_object_elf_direct`, `compile_runtime_objects:1067`) |
| "linker" = só resolve `ld` externo | `src/build/linker.tks:116 resolve_linker` |
| seleção de rota | `src/build/project.tks:1287 backend_of`, `:1293 c_backend_selected` |
| objfile ELF (relocs, símbolos) | `src/backend/objfile_elf.tks:191-203`, `:36-127` |
| encoder x86_64 sem `SYSCALL` | `src/backend/encode_x86_64.tks` (grep `0F 05`/`syscall` = 0) |
| modelo por-escopo dirige elisão de arena native | `src/lir/lower.tks:3383` (`checker::scope_slot_count(...)==0`) |
| `_start`/`stack_ptr` pendente (crumb 0125) | `docs/design/native-lowering-cobertura-zero-libc-0.3.1.md:119` |
| backlog N1/N2 por família | `docs/memory/bulk-native-verdicts-0.3.1.md`, `docs/design/recon-native-n1n2-gaps-strategy.md` |
| endgame linker interno + fixpoint de objeto (D67) | `docs/design/terminal-native-tkb-linker-0.3.1.md` |
| syscall keystone (rota C feita; native deferido §3) | `docs/design/plano-s16-syscall-intrinsic.md` |
| arena over mmap (Teko+syscall) | `docs/design/plano-s16-arena-mmap.md` |

*Grounding: `arquivo:linha` reais em `a68f15e7`. Build seco native rodado do seed
`bootstrap/teko.c` (gen0→gen1 via `build_gen1_from_c.sh`, `CC=clang`), `TEKO_BACKEND=native`,
`ulimit -v 8388608`. Nenhuma linha de produto tocada — só este doc.*
