# Cobertura de lowering NATIVE do expurgo zero-libc — mapa definitivo (0.3.1)

Status: DESIGN (arquiteto). Read-and-design ONLY — nenhum código de produto escrito aqui.
Base medida: `origin/fix/retirement` @ `fb3c9ed7` (contém `4f488589` capture_panic C-leg); DECISION_LOG @ D110.
Toda ref de linha abaixo foi re-derivada por leitura/grep na ÁRVORE ATUAL desta base. Idioma: PT-BR.

> **O buraco que este doc fecha.** O plano do tail-§16 (D105/D106; `expurgo-s16-refresh-orquestracao-0.3.1.md`)
> desenhou a rota **C** (RT-L5→F7a→F8→F7b→entry→F9) e o orçamento de reseeds, mas **não cobriu o lowering
> NATIVE**. A lei do dono é dura: **todo C emitido tem que ser expressável em native** — cada emissão C de
> `src/codegen/codegen.tks` precisa do espelho ESCRITO em `src/lir/lower.tks` + `src/backend/**`. Native é
> **WRITE-ONLY**: escreve-se agora, NÃO se roda `TEKO_BACKEND=native` antes do pós-F9. Este doc enumera a
> cobertura, corrige a MÉTRICA de aderência, mede a aderência REAL contra o CI, decide o escopo (A+B ambos),
> ordena pela fronteira empírica, define o graceful-stop (dono 2026-08-26), e amarra tudo aos reseeds do
> tail-§16 dentro do teto ≤5 + 1 sweep, em 3 passadas de ensino.

---

## §0 — A MÉTRICA (corrigida): "gen1 emite gen2 native COMPLETO por-arch", write-only

O plano antigo mediria aderência "contando honest-stops". **Errado.** A prova de aderência é empírica e já
existe no CI: a perna native roda `gen1` sobre o **próprio source do compilador** e conta
`native-lowering item N/TOTAL`. Se o lowering fosse aderente, o gen2 (`.c`/objeto) sairia inteiro. Não sai:
morre no meio. **A métrica única é: `gen1` percorre os ~9378 itens e EMITE gen2 até o fim, em cada perna
(arch×SO), sem parar e sem corromper.** Write-only: emitir gen2, NÃO executar o binário.

Os crumbs NAT existentes (`0113`/`0114`) declaram no corpo o gate **`gen2==gen3` native byte-idêntico** —
**inalcançável hoje**: gen2 native nem existe (o stream morre em ~10%). Correção adotada (§5): o gate de
aderência native passa a ser **"gen1 emite gen2 native completo por-arch (write-only)"** — medido pelo
`item N/TOTAL` chegar a TOTAL sem parar/corromper — GUARDADO pelo **fixpoint C `gen2.c==gen3.c`** do reseed
de fase (a rota que RODA agora). O `gen2==gen3` **native** só passa a ser gate pós-F9, quando a perna roda
(migração já prevista em `0106`/RM-C16).

### Aderência REAL medida (CI Pull Request CI #1135, run 32913378908, commit `4f488589`)

| perna | onde `gen1` para | item / TOTAL | sintoma | classe |
|---|---|---|---|---|
| **linux-x86_64-glibc** | `teko::backend::emit_elf_shdr` (`src/backend/objfile_elf.tks:398`, encadeado por `emit_elf_shdrs:425`) | **933 / 9378** (~10%) | HANG/OOM: 158 ms no item (~8× os vizinhos), SIGTERM 143, sem fail-fast | **(b)** retorno-fat `[]byte` em cadeia |
| **linux-arm64-musl** | `teko::backend::debug_info_locals` (`src/backend/dwarf.tks:508`) | **90 / 9378** (~1%) | `*** stack smashing detected ***` | **(b)** retorno-agregado `DwLocalsInfo` (sret AAPCS64) |
| **windows-x86_64** | `teko::build::asm_module_items` (`src/build/assemble.tks:28`) | **1145 / 9367** (~12%) | VERDICT FAILED (dura) | **(b)** retorno-slice-agregado `[]parser::Item` (sret Win64) |
| **macos-arm64** | `bootstrap/teko.c:191627` **nem COMPILA** | — | `__builtin_longjmp is not supported for the current target` | **mecanismo errado** (capture via longjmp) |

**Leitura de uma linha:** aderência native **<12%** no melhor caso, **CRASH a ~1%** no arm64, e o mac **nem
compila o teko.c**. Confirma o veredito do dono ("native <50%"): na verdade **<12%**. **Padrão-chave: as
TRÊS pernas que compilam morrem no MESMO ponto de design — RETORNO DE AGREGADO/FAT** (`[]byte` em cadeia no
x86, struct `DwLocalsInfo` por sret no arm64, slice `[]parser::Item` por sret no Win64). E o mac cai por um
MECANISMO errado (longjmp), não por lowering. Prioridade absoluta = a fronteira de retorno-fat/sret +
graceful-stop.

---

## §1 — As DUAS classes de falha (contadas na árvore atual)

Um scout que só grepa `"not yet lowered"` vê UMA classe e perde a que mata cedo.

### Classe (a) — honest-stops explícitos: `error { "native backend N1: … not yet lowered (N2)" }`
Contagem medida em `src/lir/lower.tks` @ base: **`grep -c "not yet lowered"` = 38**. Destas, **8 são
invariante-interno** (braço `_ =>`/`null =>` de um `match` exaustivo sobre conjunto que o checker/mono já
restringe — o self-build NUNCA os atinge se o checker estiver correto):

| linha | honest-stop | por que é invariante |
|---|---|---|
| 239 | float operator `_ =>` | aritmética float só é `+ - * /`; resto vai a outro path |
| 255 | integer operator `_ =>` | binops inteiros cobertos; comparações vão a outro path |
| 290 | unary operator `_ =>` | só `-`/`~` são unários numéricos válidos |
| 917 | named-type não-enum como operando aritmético `null =>` | struct/class/flags não tipam como operando aritmético |
| 934 / 946 | comparison operator `_ =>` | todos os operadores de comparação reais cobertos |
| 5722 | compound-assign operator `_ =>` | default sobre o conjunto que o parser produz |
| 6070 | função genérica (`type_params.len != 0`) | **mono roda ANTES do emit** (`src/build/project.tks:227` `monomorphize`) → pós-mono nenhuma fn tem `type_params` |

Os **30 restantes são construtos REAIS** sem lowering: operadores residuais; match-patterns
range(3976)/alt(3977)/float(3994)/slice(4051)/field(4083,4150)/grouped-bind(4093,4160); fat-pointer
iface-dispatch(2187,4353)/closure-call(2316)/receiver(4347)/cast(4914)/mutable-capture(2376)/lambda-return(2824,2859,6020);
vtable de classe polimórfica(2200); interpolação bool(4979)/typed(4995); `in`-expr(5102);
field/index não-struct(5131); destructuring(2801); if-sem-else como valor(3463)/fat(4673); `str::concat`
sobre `[]str`(4384); `float::parse` result-class(2112); comparação union não-null/não-numérica(1141);
`has no single PrimKind`(909). **Estes já estão desenhados nos crumbs `0113`/`0114`** (ver §5).

### Classe (b) — bugs de lowering INTERNOS: `(internal)`, hang, OOM, stack-smash
**NÃO grepáveis por `not yet lowered`.** Contagem de error-paths `(internal)` em `lower.tks`:
**`grep -c "(internal)"` = 77**. São invariantes que o checker "deveria" garantir mas que o lowering VIOLA —
ou pior, lowering que nem para: emite errado, trava, ou corrompe a pilha. É a classe que mata as 3 pernas
que compilam:

- **`objfile_elf.tks:398` `emit_elf_shdr` / `:425` `emit_elf_shdrs`** — HANG/OOM (x86_64@933).
  `emit_elf_shdrs` encadeia ~9× `b = emit_elf_shdr(b, …)`, cada um retornando um novo `[]byte`
  (retorno-fat). O lowering do threading fat-`[]byte`-por-cadeia (DPS/dest-passing) está defeituoso →
  realoca/copia sem parar ou entra em laço → cresce sem limite. **É a MESMA família dos honest-stops
  "fat-typed lambda return" (2824/2859/6020), mas manifesta como HANG, não como stop.**
- **`dwarf.tks:508` `debug_info_locals`** — stack-smash (arm64@90). Retorna `DwLocalsInfo` (struct,
  `dwarf.tks:426`). Retorno-agregado por `sret` na ABI **AAPCS64** escrevendo além do slot, OU overrun de
  buffer `[]byte`/`[]u16` no encoder arm64. **Bug de backend arch-específico (isel/regalloc/encode/ABI arm64).**
- **`assemble.tks:28` `asm_module_items`** — VERDICT FAILED (windows@1145). Constrói array de tamanho-runtime
  `var items: [m.uses.len + m.decls.len]parser::Item = []` por índice e **retorna `[]parser::Item`** — slice
  de structs. Retorno de slice-agregado por `sret` na ABI **Win64**. **MESMA raiz de retorno-agregado, terceira ABI.**
- **`lower.tks:3602` e `:3819`** — `"…is not a member of its declared variant (internal)"`, em
  `variant_member_index_of` (`lower.tks:3595`). Causa-raiz: `variant_member_type_matches` (`lower.tks:3605`)
  só casa `Named`/`Error`/`Prim`/`Str`/`Byte`/`Slice` (`_ => false` em `:3613`); um membro que seja `Null`
  (null-como-união), `Func` (closure), `Void` ou agregado cai no `_ => false` → índice não encontrado →
  falso-erro-interno. **É invariante ERRADO, não honest-stop** — desbloqueia a família union/nullable.

**Raiz unificadora medida:** as 3 pernas que compilam morrem em **RETORNO DE AGREGADO/FAT** (`[]byte` cadeia,
struct sret, slice sret), cada uma expondo uma ABI diferente do MESMO defeito de lowering. **Implicação de
design:** os 77 `(internal)` são a lista latente da classe-(b); o CI é o oráculo que diz QUAIS são atingidos
e em que ordem (fronteira). O trabalho é **frontier-driven e iterativo**: corrige o primeiro-ofensor
por-arch → re-emite gen2 native → o próximo aparece mais fundo → repete até `item == TOTAL`. Não é checklist
fechável por leitura.

---

## §2 — DOIS EIXOS e a DECISÃO de escopo (decidida law-first, sem fork)

### Eixo (A) — emissões PRÓPRIAS da campanha zero-libc
O C que a campanha ELA MESMA passou a emitir e cujo espelho native precisa existir:

| emissão C (campanha) | sítio C (verificado) | espelho native | status native |
|---|---|---|---|
| `capture_panic`/exit/cancel — **REDESENHADO graceful-stop** (dono 2026-08-26; SUPERSEDE o setjmp/longjmp de D105/0064) | `codegen.tks:3215` `emit_capture_panic` (usa `__builtin_setjmp` em `:3228`) + `:3249` `emit_capture_longjmp` (`__builtin_longjmp` em `:3250`); dispatch `:3513`/`:3514` — a REESCREVER | **controle de fluxo puro** (check-flag + defers/drops + return); MESMA lógica das duas rotas; some a primitiva de unwind/PC-SP | **a REDESENHAR** (0126) — o mac-blocker é sintoma deste mecanismo errado |
| `thread_clone` helper x86_64 (raw clone) | `codegen.tks:9606` `cg_emit_thread_clone_helper_text_x86_64` | idem asm-inline **aarch64** | **AUSENTE aarch64** (`#error` `codegen.tks:9616`) |
| entry `_start` + `stack_ptr` intrínseco per-OS | native: `lower.tks:6717` `native_entry_stub` (esqueleto existe); C-side `_start`/`stack_ptr` ainda NÃO em `codegen.tks` | `_start` LFunc + `stack_ptr`→`mov reg,%rsp`/`mov reg,sp` (isel) | **a ESCREVER** (crumb 0125 pendente) |
| spawn mmap/VirtualAlloc glue (thread stack) | `thread.tks` (SYS_MMAP) / kernel32 | chamada extern/syscall — já lowerável | rides F7a |
| símbolos do harness + cov | codegen `emit_cov_line:19`/`emit_cov_branch:30`; builtins `cov_*` dispatch | `builtin_cov_symbol` (`lower.tks:1970`) já mapeia os `cov_*` | **FEITO** (extern-call) |
| `load_u8`/`store_u8` | codegen intrínsecos | `lower_load_u8_call`/`lower_store_u8_call` (`lower.tks:1771`/`:1786`) | **FEITO** |
| `syscallN`, atomics | codegen intrínsecos | honest-stop `unresolved_builtin_stop` (`lower.tks:2154`) — NÃO rebaixado | **NÃO FEITO** (corrigido 2026-09-01 — verdade empírica do build seco `TEKO_BACKEND=native`; era falso-positivo agrupado com load/store; ver `plano-emissao-objeto-nativo-0.3.1.md §2.1`) |

**Verificação:** os `cov_*` e `load_u8/store_u8` **já** têm espelho native (confirmado). O eixo-A que FALTA:
(1) o graceful-stop portável de capture/exit/cancel (0126, mac-blocker), (2) clone aarch64
(`codegen.tks:9616`), (3) entry `_start`/`stack_ptr` (crumb 0125).

### Eixo (B) — backlog native geral pré-existente
Operadores/comparações/match-patterns/fat-pointers/union-nullable/result-class/vtable — os 30 honest-stops
reais da classe (a) MAIS os bugs de classe (b). **JÁ DESENHADO** em `0097` (NAT-A1), `0113` (NAT-N1
union/nullable) e `0114` (NAT-N2 fat/dispatch/match/operator/KNOWN-WRONG) + `recon-native-n1n2-gaps-strategy.md`.
**O que NÃO está coberto por 0113/0114:** a classe (b) hang/OOM/stack-smash (elas tratam honest-stops e
valores-errados, não travas/crashes), a raiz de retorno-agregado/sret por-ABI, a ordem empírica por-arch, e
o invariante-errado `3602`/`3819`.

### DECISÃO (law-first, sem fork): **A e B ambos ENTRAM (write-native-now).**
1. **"Todo C expressável em native"** (âncora do dono) é absoluta; **D106 "antecipar, nunca deferir"** proíbe
   estacionar (B) num track diferido. O grosso da <12% de aderência é (B) — deferir (B) = deferir a métrica.
2. **Native é WRITE-ONLY** → escrever (B) **não adiciona reseed de validação** (native não roda agora); rides
   os reseeds do tail-§16. Único custo é tamanho/offset do `teko.c`, absorvido nos reseeds já orçados
   (ratchet D100: adds proporcionais C→Teko).
3. **(B) já está desenhado** (0097/0113/0114 + recon). Puxá-lo à frente custa design-zero — só RE-SEQUENCIAR,
   CORRIGIR o gate/métrica e ADICIONAR a cobertura de classe (b) + a raiz de retorno-agregado que faltou.

**O que legitimamente FICA pós-F9** (não é write-native-now): a **execução/validação runtime** do native
(`gen2==gen3` native) e o **endgame .tkb/.tkh do LINKER** (`0105`/RM-C15, `0106`/RM-C16, `0107`/RM-C17).
Esses são native-RUN + distribuição, corretamente pós-F9. O eixo (B) NÃO pertence a RM-C; dobra-se nos
reseeds de fase do tail-§16.

---

## §3 — Ordenação pela FRONTIER empírica + 3 passadas de ensino

Classe (b) mata mais cedo → vem primeiro. Dentro dela, o primeiro-ofensor POR-ARCH dita a ordem (o frontier
avança sozinho a cada correção). Passadas são camadas de ENSINO (dependência lógica), NÃO gates temporais —
**passadas são livres, RESEEDS são o teto (≤5 + 1 sweep)**.

### Passada 1 — DESTRAVAR A FRONTEIRA (retorno-agregado/sret + variante/match)
O que impede gen2 de EXISTIR. Na ordem do CI:
- **P1.1 · retorno-agregado/fat por-ABI (a raiz das 3 pernas).** Uma disciplina única de dest-passing
  (DPS) para valores agregados/fat: o caller aloca o slot destino (arena do caller) e passa o ponteiro; o
  callee escreve NO destino e NÃO realoca/copia por chamada. Materializa por-ABI:
  - x86_64 SysV: `[]byte` em cadeia (`emit_elf_shdrs`) sem realloc → mata o HANG/OOM@933.
  - AAPCS64: struct `DwLocalsInfo` via `x8`/sret, tamanho exato, guard de overrun → mata o stack-smash@90.
  - Win64: slice `[]parser::Item` via sret com shadow-space correto → mata o VERDICT FAILED@1145.
  Cobre também os honest-stops "fat-typed lambda return" (`lower.tks:2824`/`:2859`/`:6020`).
- **P1.2 · invariante-errado de membro-de-variante (`lower.tks:3602`/`:3819`).** Estender
  `variant_member_type_matches` (`:3605`, `_ => false` em `:3613`) para `Null`, `Func`, `Void` e agregado.
  Desbloqueia a família union/nullable inteira.
- **P1.3 · match-patterns + operator residual + union/nullable** (= **NAT-N1 `0113` + o braço match/operator
  de NAT-N2 `0114`**, PUXADOS PARA FRENTE): range/alt/float/slice/field/grouped-bind; residual de
  `LBinOp`/`LUnOp`; `has no single PrimKind`(909) e comparação union(1141).

**Por que P1 primeiro:** as travas de classe (b) e os stops de match/variante estão nos itens ~90–1145, ANTES
de qualquer construto de eixo-A (threads/capture/entry estão bem mais fundo no corpus). Sem P1, o stream não
passa dos ~12% — nenhum item de A ou B posterior é sequer alcançado. **O crash arm64@90 (P1.1/AAPCS64) é MUITO
cedo → intercala por-arch, guiado pelo frontier, não pelo rótulo de passada.**

### Passada 2 — FAT/ABI/RESULT-CLASS + vtable (o meio do corpus)
- **P2.1 · result-register-class** (o KNOWN-WRONG keystone): registrar a classe do registrador de resultado
  (int-reg / float-reg XMM0·D0 / fat-pair) — corrige `float::parse` (`lower.tks:2112`, resultado `double`
  lido do banco errado) e todo resultado float/fat capturado do registrador errado.
- **P2.2 · fat-pointer através de indireção**: iface-dispatch result(2187,4353), closure-call result(2316),
  fat cast(4914), fat receiver(4347), mutable-fat capture(2376).
- **P2.3 · vtable de classe polimórfica** (`lower.tks:2200`): slot-vtable + chamada indireta.
- **P2.4 · resíduo de expr/stmt**: interpolação bool(4979)/typed(4995), `in`-expr(5102), field/index
  não-struct(5131), destructuring(2801), if-sem-else como valor(3463)/fat(4673), `str::concat` sobre `[]str`(4384).

### Passada 3 — PRIMITIVAS DE RUNTIME DA CAMPANHA (eixo A próprio)
- **P3.1 · capture/exit/cancel = GRACEFUL-STOP** (0126; dono 2026-08-26): controle de fluxo PURO — flag de
  panic-pendente por-task + após cada call que pode entrar em panic, emitir
  `if (panicking()) { <defers/drops do frame>; return <sentinela>; }`; o frame de captura testa/consome.
  **Elimina `__builtin_setjmp`/`__builtin_longjmp`** (causa-raiz do erro macOS) → portável Linux/Windows/Mac,
  zero-libc. **Não há primitiva de unwind/PC-SP-restore.**
- **P3.2 · capture native = a MESMA lógica**: `lower.tks` emite o mesmo check-flag+cleanup+return que o
  codegen C — controle de fluxo normal, reusa o caminho de defer/drop do return. Some a "unwind primitive"
  do mapa de ensino; o native SIMPLIFICA.
- **P3.3 · `stack_ptr` intrínseco + naked `_start` per-OS** (crumb 0125): isel `mov reg,%rsp`/`mov reg,sp`.
- **P3.4 · clone aarch64** (asm-inline, `#error` hoje `codegen.tks:9616`): espelho do helper x86_64
  (`codegen.tks:9606`).

**Por que 3 passadas (dono confirmou 2-3):** P1 (fronteira) PRECEDE tudo (senão o stream para a <12%); P2
(fat/ABI/result-class) e P3 (runtime próprio) são independentes ENTRE SI mas ambas exigem o stream de P1
fluindo além dos ~1145 itens. NÃO cabe em 1 (P1 é pré-requisito de medir P2/P3); NÃO precisa de 4 (P2 e P3 não
dependem uma da outra). O graceful-stop ELIMINA a primitiva de unwind que o mapa antigo listava — P3 encolhe
no lado runtime. O mac-blocker do longjmp DESAPARECE (graceful-stop não emite builtin nenhum).

---

## §4 — AMARRAÇÃO aos reseeds do tail-§16 (dentro do teto ≤5 + 1)

O tail-§16 (D105, revisado por D106/D109) tem a sequência de reseeds SÉRIE:
**RT-L5 (0063, PARCIAL em D109) → F7a threads ∥ F8/RT-L6 (0064) → F7b canais → entry-native (0125) → F9 SWEEP.**
São **4 reseeds de fase + 1 sweep (F9) = ≤5 + 1**. O trabalho native **RIDE esses reseeds** (write-only não
adiciona reseed próprio). Cada dispatch de fase carrega seu escopo native:

| reseed (fase tail) | escopo NATIVE que carrega (write-only) | passada |
|---|---|---|
| **R#1 · F7a threads** | clone-aarch64 (P3.4) · spawn-mmap native (A) · **P1.1 retorno-agregado/sret + P1.2 variante-membro + P1.3 match/operator/union** (destrava a fronteira antes de tudo) | 1 (+P3.4) |
| **R#2 · F8/RT-L6 (0064)** | **0126 capture/exit/cancel graceful-stop (P3.1+P3.2, C+native idênticos, elimina longjmp, mac-unblock)** · harness/cov native (feito) · **P2.1 result-class + P2.2 fat-pointer + P2.3 vtable** | 2, 3 |
| **R#3 · F7b canais** | canais AF_UNIX/named-pipe native · **classe-(b) sweep frontier-driven** (próximos `(internal)` que surgirem após R#1/R#2, incl. resíduo Win64@1145 se persistir) | 1(b)/2 |
| **R#4 · entry-native (0125)** | `_start`/`stack_ptr` per-OS (P3.3) · **P2.4 resíduo expr/stmt** | 2, 3 |
| **R#5 = SWEEP · F9** | **GATE de aderência native (write-only): `gen1` emite gen2 native COMPLETO nas 4 pernas** (item→TOTAL, sem parar/corromper) ANTES do sweep C. +1 = o próprio SWEEP | — |

**Regra de ouro do teto:** nenhum lowering native abre reseed NOVO. Se o frontier-driven (R#3) revelar mais
ofensores que um reseed comporta, eles se acumulam no MESMO reseed de fase (o native não roda → não há pressa
de validar cada um isoladamente). O ratchet de memória (D100/D68) governa os reseeds que REALMENTE rodam (rota
C).

**Re-sequenciamento de `0113`/`0114`:** hoje são milestone **M4** com dep "logical predecessor of RM-C15
(`0105`)" (pós-marco de memória). **Puxar para frente** para rodar em R#1/R#2 — a lei D106 "antecipar nunca
deferir" o exige. Ajuste de gate: o corpo deles diz `gen2==gen3` **native** byte-idêntico (inalcançável);
trocar para **"gen1 emite gen2 native completo (write-only) + fixpoint C `gen2.c==gen3.c` do reseed de fase"**.
E CORRIGIR os `sources:` que driftaram (medido nesta base): 0113 `lower.tks:925`→**909**, `:1164`→**1141**;
0114 `:2174`→**2187**, `:2322`→**2316**, match `:3918-4081`→**3976-4160**. Registrado em `EXECUTION-ORDER.md`
e nos próprios crumbs.

---

## §5 — Crumbs NOVOS (write-native-now) — 0126, 0127, 0128 + re-sequenciamento

Criados em `.crumbs/` após `0125`:
- **`0126-NAT-B0-graceful-stop-capture.md`** — capture/exit/cancel REDESENHADO como graceful-stop (unwind
  cooperativo pela via de return, defers/drops por frame; ELIMINA setjmp/longjmp). Supersede o C-leg longjmp
  de `0064`/D105. C e native emitem a MESMA lógica de controle de fluxo. Rides R#2/F8-0064.
  **Desbloqueia mac** (o erro `__builtin_longjmp` era sintoma do mecanismo errado).
- **`0127-NAT-B3-lowering-crash-invariant-sweep.md`** — classe (b): retorno-agregado/sret por-ABI
  (x86@933 `[]byte`, arm64@90 struct, win64@1145 slice), invariante-errado membro-de-variante (3602/3819), +
  sweep frontier-driven dos `(internal)`. Rides R#1 (destrava) e R#3 (resíduo).
- **`0128-NAT-B4-campaign-emission-native-mirror.md`** — espelho native das emissões próprias da campanha
  (clone-aarch64, `stack_ptr`/`_start`, spawn-mmap) amarrado a F7a/entry-native/0125. Rides R#1/R#4.

O re-sequenciamento de `0113`/`0114` (pull-forward M4→R#1/R#2 + correção de gate + correção de `sources:`) é
aplicado nos próprios crumbs e no `000-INDEX.md`.

## §6 — Índice
- §0 A métrica corrigida (gen1-emite-gen2-native por-arch, write-only) + aderência REAL do CI #1135
- §1 As duas classes (a honest-stops / b bugs internos hang/OOM/crash) — contadas na árvore atual
- §2 Dois eixos (A campanha / B backlog) + a DECISÃO de escopo (ambos entram, law-first)
- §3 Ordenação pela fronteira empírica (retorno-agregado/sret keystone) + 3 passadas
- §4 Amarração aos ≤5+1 reseeds do tail-§16 + re-sequenciamento 0113/0114
- §5 Crumbs novos 0126/0127/0128
