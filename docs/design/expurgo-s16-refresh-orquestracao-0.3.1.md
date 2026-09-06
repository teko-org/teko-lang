# §16 — Expurgo da libc: REFRESH de estado + orquestração concreta (@ HEAD `ddf97dc9`)

Status: DESIGN (arquiteto). Read-and-design ONLY — nenhum código de produto escrito aqui.
Base: `origin/fix/retirement` HEAD `ddf97dc9` (D84). Autor: arquiteto. Idioma: PT-BR.

> **O que este documento É.** NÃO reescreve o mapa-mestre (`plano-s16-expurgo-libc-completo.md`, do
> HEAD `be8fc1e1`). Ele **complementa**: (1) audita fase-a-fase o que JÁ está aterrado vs pendente @
> HEAD atual (a campanha de memória, a reforma TagPtr e a migração de subsistemas mudaram muito desde
> `be8fc1e1`); (2) concretiza o CAMINHO DE CONSTRUÇÃO de F6 (process) e F7 (threads/channels), que o
> dono ordenou 100% (D84 — sem pré-deferir); (3) dá a sequência de orquestração concreta
> (fase→bites→ritual→deps), marcando Linux-primeiro vs braço mac/win.
>
> **CRITÉRIO DE ACEITE CORRIGIDO (dono, 2026-08-24).** O alvo do §16 NÃO é "sem `#include`" — é **ZERO
> dependência de qualquer libc/libm no C final**. O C emitido é o degrau para o native (o backend
> native não linka header NEM biblioteca C). Portanto:
> - **O SWEEP (F9) é `cc bootstrap/teko.c` SEM `-lm` e sem nenhuma lib C** (alvo final
>   `-ffreestanding`/`-nostdlib`), NÃO `cc bootstrap/teko.c -lm` como o mapa-mestre dizia. O `-lm` SAI:
>   `floor`/soft-float viram implementação própria.
> - **Cada fase tem, no ritual point, a PROVA DE LINK ZERO-LIBC do braço migrado** (nenhum símbolo
>   libc/libm indefinido no objeto — `nm -u`/`readelf` sem `floor@GLIBC`, `malloc@GLIBC`, etc.), não só
>   o grep de include. Uma fase que deixa símbolo libc residual NÃO está "feita" pelo critério do dono.
> - **O norte é o native:** zero-libc-C = a condição de possibilidade do gen2/gen3 native que não emite
>   nem depende de C.

---

## §0 — Sumário do delta (o que mudou desde `be8fc1e1`)

O mapa-mestre subestima o progresso em F0/F1/F3/F7 e superestima o bloqueio em F6/F7. Medido @ HEAD:

| Fase | Mapa dizia | Estado REAL @ `ddf97dc9` |
|---|---|---|
| **F0 infra** | "em voo" | **ATERRADO** — `syscall0..6`, `ptr_word`/`word_ptr`, `load_u64`/`store_u64`, `thread_clone`, `atomic_*`, `floor`, `f64_bits`/`f64_from_bits` são intrínsecos vivos (`scope.tks:514-606`). `teko::sys` tem 32 blocos de const per-`#os`/`#arch` (`sys.tks`), incl. CLONE_*/FUTEX_* (threads) e OPENAT/STATX (fs). Helpers de emit vivos: `cg_emit_syscall_helpers` (asm inline x86_64+aarch64), `cg_emit_thread_clone_helper` (raw clone!), `cg_emit_atomic_helpers`. |
| **F1 emit** | "pendente (floor/memcpy/typedefs)" | **~80% ATERRADO** — o EMIT já perdeu `<stdint.h>`/`<stdbool.h>`/`<math.h>`/`<string.h>`. `cg_emit_fixed_width_typedefs` (`codegen.tks:9599`) emite os `typedef __UINT8_TYPE__ …`. `floor` via `__builtin_floor` (`:3549`). **PENDENTE:** ainda emite `#include <stdlib.h>` (`:9621`) + `"teko_rt.h"`/`"assert.h"`; e **`__builtin_floor` NÃO é zero-libm** (§F1 abaixo). |
| **F2 exit/write** | "aterrado" | **ATERRADO** no leg-Teko — `io/file_stream.tks` usa SYS_WRITE/SYS_READ raw (Linux) + extern `System`/`kernel32`; `ftoa` é **Teko puro** (`numfmt.tks:415`, sem snprintf). Exit via arena `os_exit`/SYS. |
| **F3 arena** | "keystone, pendente" | **ATERRADO no braço Linux** — `arena.tks` (893 linhas) faz `ar_mmap` via SYS_MMAP raw (Linux), `os_mmap` from `System` (macOS), `VirtualAlloc` from `kernel32` (Windows). O emit tem `cg_emit_arena_provider_ladder` (`:177`): Linux→símbolo Teko `teko_teko__runtime__*`; não-Linux→símbolo C `tk_*`. **PENDENTE:** o braço não-Linux ainda cai no arena-C de `teko_rt.c`. |
| **F4 fs/env/tempo/random** | "pendente" | **PENDENTE (leaf ainda em C)** — `fs.tks`, `time.tks`, `env.tks`, `crypto/rand.tks` **ainda roteiam para `tk_rt_*`** (`tk_rt_list_dir`/`_mkdir`/`_monotonic_ns`/`_getcwd`/`_getenv`/`_secure_bytes`). Os consts de fs (OPENAT/STATX) existem; **faltam** GETDENTS64/MKDIRAT/CHDIR/GETCWD/DUP3/GETRUSAGE/NEWFSTATAT. |
| **F5 assert** | "pendente" | **~60% ATERRADO** — `assert/assert.tks` já é Teko (compara valor + `panic`); só 3 seams `tk_assert_scenario_*` restam extern a `teko_rt`. `assert.c` (252 linhas) é a superfície a apagar. |
| **F6 process** | "BLOQUEADO" | **PENDENTE, braço-Linux DESBLOQUEADO** — `process/process.tks` roteia 11 fns a `tk_rt_*`. Mas os syscalls de processo Linux (clone/execve/wait4/pipe2/ppoll) são **ponteiro-based, sem struct-by-value** → o braço Linux é construível JÁ (§2). Só o braço Windows precisa de struct-FFI + import-lib linker. |
| **F7 threads/channels** | "DEFERIDO §17+" | **RETRATADO por D84 + já ~70% ATERRADO** — `runtime/sync.tks` tem mutex/cond via **SYS_FUTEX raw** (Linux) + `os_sync_wait_on_address` (macOS) + `WaitOnAddress` (Windows). `runtime/thread.tks` aloca stack+guard via SYS_MMAP/SYS_MPROTECT. O intrínseco `thread_clone` + helper de emit existem. **PENDENTE:** o SPAWN glue ainda usa `pthread_create` (`teko_rt.c:2305/2318`); channels (sockets AF_UNIX) não migrados. |
| **F8 test/crash** | "ruling" | **PENDENTE** — R2 ratifica intrínseco de captura; `capture_panic` (D48, cooperativo, sem jump) é a rota escolhida. `setjmp`/`longjmp`/`signal`/`backtrace`/`dladdr` seguem em `teko_rt.c`. |
| **F9 SWEEP** | "gate terminal" | **PENDENTE** — build ainda linka `-lm -ldl -pthread` + `teko_rt.c` (5073 l) + `assert.c` (252 l). Superfície-C total a apagar: **7328 linhas** (`teko_rt.c`+`.h`+`assert.c`+`win32_compat.h`). |

**Leitura de uma linha:** o §16 está MUITO mais adiantado do que o mapa (be8fc1e1) sugere — o
mecanismo (F0), o emit (F1), a arena-Linux (F3) e a fundação de threads (F7) estão aterrados. O que
falta é **migrar os módulos-folha** (F4 fs/env/tempo/random, F5 assert, F6 process, F7 channels/spawn)
do seam `tk_rt_*`-em-C para raw-syscall/extern-Teko, **fechar os braços mac/win** onde só o Linux
fechou, e **remover o `-lm`/soft-float o floor** (o degrau que o critério corrigido do dono expõe).

---

## §1 — Auditoria por-fase (feito ✓ / pendente ⧗ @ HEAD) + prova de aceite zero-libc

Legenda: ✓ aterrado · ◑ parcial · ⧗ pendente · ⛔ bloqueado por dep não-nossa.
"Prova de aceite" = o teste de link zero-libc/libm do braço, no ritual point da fase.

### F0 — Infra (mecanismo) — ✓ ATERRADO
- ✓ `syscall0..6` (`scope.tks:517-523`, emit asm inline x86_64/aarch64 `codegen.tks:9287-9330`).
- ✓ `ptr_word`/`word_ptr` (`:529-536`), `load_u64`/`store_u64` (`:605-606`).
- ✓ `thread_clone` (`:524`, emit `tk_thread_clone` raw-clone `:9523`), `atomic_cas/xchg/add/load_u32` (`:525-528`).
- ✓ `teko::sys` consts per-`#os`/`#arch` (`sys.tks`, 32 blocos): MMAP/MUNMAP/MPROTECT/CLONE/FUTEX/GETTID/EXIT/EXIT_GROUP/WRITE/READ/CLOSE/LSEEK/OPENAT/STATX/CLOCK_GETTIME/GETRANDOM + CLONE_*/FUTEX_*/PROT_*/MAP_*/O_* + Windows MEM_*/PAGE_*.
- **Delta:** nenhum — a fundação está pronta. Serve TODAS as fases S.
- **Prova de aceite:** n/a (mecanismo; provado transitivamente pelas fases que o usam).

### F1 — Intrínsecos do EMIT (math/string/type) — ◑ ~80%
- ✓ Typedefs de largura fixa próprios (`cg_emit_fixed_width_typedefs`) — `<stdint.h>`/`<stdbool.h>` mortos do emit.
- ✓ `<math.h>`/`<string.h>` fora do preâmbulo; `f64` literais via `__builtin_nan`/`__builtin_inf` (const-fold, zero-libm).
- ⧗ **`#include <stdlib.h>` ainda emitido** (`codegen.tks:9621`) — `malloc`/`abort`/`_Exit` (o seam da arena+panic). Sai quando F3-braço-C morre e o panic/exit vira SYS puro no emit.
- ⧗ **`floor` via `__builtin_floor` NÃO é zero-libm.** No x86-64 com SSE4.1 o gcc baixa a `roundsd` (ok), MAS no caminho genérico/`-ffreestanding` OU quando o alvo não garante SSE4.1, `__builtin_floor` **emite `call floor@PLT`** — dependência de libm. **RECOMENDO:** intrínseco `floor` soft (manipulação de bits IEEE-754, como `f64_bits_are_*` já faz em `codegen.tks:213-223`) OU `__builtin_floor` com garantia de baixamento a instrução (asserção `-msse4.1` no braço x86 + `frintm` no aarch64). O soft-float é o único zero-libm portável.
- **Prova de aceite:** o `.o` do emit não tem `floor@GLIBC`/`U floor` em `nm -u`; `cc bootstrap/teko.c` sem `-lm` linka (uma vez que F1+F3 fecharem).

### F2 — exit + write (I/O) — ✓ ATERRADO (leg-Teko Linux)
- ✓ `io/file_stream.tks`: SYS_WRITE/SYS_READ raw (Linux) + `System`/`kernel32` (mac/win). `ftoa` Teko puro (`numfmt.tks`), sem `snprintf`/`%.17g`-via-C.
- ⧗ exit no braço-C do emit ainda via `abort`/`_Exit` (`<stdlib.h>`); no leg-Teko-Linux já é SYS_exit_group. Fecha com F1-stdlib-drop.
- **Prova de aceite:** fixture S1/S2 (exit 42 / stdout "hi") + `nm -u` sem `fwrite`/`fputs`/`fputc`.

### F3 — ARENA sobre mmap — ◑ Linux ✓, mac/win via ladder-C
- ✓ `arena.tks` completo os 3 OSes (SYS_MMAP Linux / `os_mmap` System / `VirtualAlloc` kernel32).
- ✓ `cg_emit_arena_provider_ladder`: Linux→Teko-sym, else→`tk_*`-C.
- ⧗ **O braço não-Linux ainda resolve para o arena-C de `teko_rt.c`.** Para zero-libc no braço mac/win, o ladder tem de apontar mac/win para os símbolos Teko também (o corpo mac/win de `ar_mmap` já existe — falta só flipar o ladder e provar o self-host mac/win).
- **Prova de aceite (RITUAL):** fixpoint `tc1==tc2==tc3` + MEM_PARANOID após reseed load-bearing; `nm -u` do emit-Linux sem `malloc@GLIBC`/`free`/`posix_memalign`.

### F4 — fs + env + tempo + random — ⧗ PENDENTE (folhas ainda em C)
- ⧗ `fs.tks`→`tk_rt_list_dir`/`_mkdir`/`_remove_file`; `time.tks`→`tk_rt_monotonic_ns`/`_wall_*`; `env.tks`→`tk_rt_getenv`/`_setenv`/`_chdir`/`_getcwd`/`_args`; `rand.tks`→`tk_rt_secure_bytes`.
- ⧗ **Consts faltando** em `sys.tks`: GETDENTS64, MKDIRAT, NEWFSTATAT (stat), CHDIR, GETCWD, DUP3, GETRUSAGE, UNLINKAT (+ mac/win equivalentes). OPENAT/STATX/CLOCK_GETTIME/GETRANDOM já existem.
- ⧗ **`extern type` de SO** por escrever: `Stat`/`Statx`, `linux_dirent64`, `Rusage`, `Timespec` — cada um guarded por `#os` (R3, layout diverge Darwin≠Linux). C1 (`extern type = struct`) já aterrado → são skeletons que compilam hoje.
- ⧗ `localtime_r`→calendário civil Teko puro (algoritmo Howard Hinnant); `env`→`environ`-Teko.
- **Prova de aceite (RITUAL):** S7-S14; braço-fs do `win32_compat.h` órfão (D-TS1 desbloqueou via `#os`-guard); `nm -u` sem `open`/`stat`/`getdents`/`clock_gettime`/`getrandom`/`getenv`.

### F5 — panic/assert — ◑ ~60%
- ✓ `assert/assert.tks` é Teko (compara + `panic`).
- ⧗ 3 seams `tk_assert_scenario_prefix`/`_set`/`_ok` extern a `teko_rt` — migrar para estado-Teko (`#singleton` de cenário).
- ⧗ apagar `assert/assert.c` (252 l) — parte do SWEEP.
- **Prova de aceite:** S15 (assert eq passa / neq panica); `nm -u` sem `assert__*`.

### F6 — process/exec/pipes — ⧗ braço-Linux DESBLOQUEADO, braço-Windows ⛔
Ver §2 (caminho de construção). Resumo:
- ⧗ `process/process.tks` roteia 11 fns a `tk_rt_*`; braço-Linux construível JÁ (clone/execve/wait4/pipe2/ppoll — ponteiro-based, sem struct-by-value).
- ⛔ braço-Windows: `STARTUPINFOA`/`PROCESS_INFORMATION` por valor → precisa de **struct-by-value-FFI reverse** + **linker de import-lib Win32** (M-linker / Fase E).
- **Prova de aceite:** S(process): spawn `/bin/true`→exit 0, pipe round-trip; `nm -u` sem `fork`/`execvp`/`waitpid`/`pipe`/`poll`.

### F7 — threads/sync/channels — ◑ ~70% (RETRATADO de "deferido")
Ver §3. Resumo:
- ✓ mutex/cond via SYS_FUTEX raw (`sync.tks`); stack+guard via SYS_MMAP/MPROTECT (`thread.tks`); intrínseco `thread_clone` + emit-helper.
- ⧗ **spawn glue ainda `pthread_create`** (`teko_rt.c:2305/2318`) — trocar por `thread_clone` intrínseco + TLS (`set_tid_address`/`%fs`); consts faltando: SET_TID_ADDRESS.
- ⧗ channels: `tk_oschan` AF_UNIX (sockets) não migrado; consts SOCKET/BIND/SENDTO/RECVFROM faltando; Windows sem AF_UNIX → Named Pipes (REPORTAR a §10, divergência de arquitetura).
- **Prova de aceite:** spawn+join N threads soma correta; mutex sob contention; `nm -u` sem `pthread_*`.

### F8 — test/crash — ⧗ PENDENTE (rota ratificada)
- ⧗ R2: intrínseco de captura = `capture_panic` cooperativo (D48, sem jump; panic/exit mudam ROTA, rodam defers+arenas como return, detecção por `TEKO_TEST_GATE`). Remove `setjmp`/`longjmp`. WHOLE-PROGRAM, alto risco, POR ÚLTIMO.
- ⧗ R5: `signal` (SYS_rt_sigaction + handler Teko), `backtrace` (unwind `.eh_frame`/frame-pointers Teko), `dladdr` (símbolo próprio) — construídos, sem degradar.
- **Prova de aceite:** suíte captura panic sem morrer; `nm -u` sem `setjmp`/`longjmp`/`signal`/`backtrace`/`dladdr`.

### F9 — SWEEP — ⧗ gate terminal
- ⧗ parar de emitir `#include "teko_rt.h"`/`"assert.h"`/`<stdlib.h>` (reseed); deletar os 4 (`teko_rt.{c,h}`+`assert.c`+`win32_compat.h`, 7328 l); ajustar ~20 scripts CI (remover `-lm -ldl -pthread` de `build_gen1_from_c.sh:51`, `package_release.sh:140`, `native_linux_asset.sh:211/281`).
- **Prova de aceite (RITUAL FINAL):** `cc bootstrap/teko.c` **sem `-lm`, sem lib C** compila + `TEKO_BACKEND=c gen .` re-emite byte-idêntico (tc2==tc3) + MEM_PARANOID + provenance + `nm -u bootstrap/teko` sem NENHUM símbolo `@GLIBC`/`@GLIBC_*`/libm. Alvo-final: `-ffreestanding -nostdlib` linka. S16.

---

## §2 — Caminho de construção de F6 (process) — o que é construível JÁ vs o fork REAL

O mapa marcava F6 "BLOQUEADO em struct-by-value-FFI reverse + linker import-lib Win32". **Isto é
verdade só para o braço Windows.** Decompondo por-braço:

### 2.1 — Braço LINUX (CONSTRUÍVEL JÁ — nenhuma dep não-nossa)
Os syscalls de processo do Linux são **todos ponteiro-based** (recebem endereços, não structs por
valor). Logo NÃO precisam de struct-by-value-FFI. Precisam só de: (a) consts novos em `sys.tks`;
(b) resolver PATH em Teko (execvp = leitura de `$PATH` + tentativa de `execve`); (c) `pipe2` retorna
2 fds via ponteiro de buffer (`load_u64`/`store_u64` já existem).

| fn C hoje | syscall Linux | const faltando | struct-by-value? |
|---|---|---|---|
| `tk_rt_run`/`_run_quiet` | fork/exec/wait sequencia | SYS_EXECVE, SYS_WAIT4 | não |
| `fork` | SYS_CLONE (flags=SIGCHLD) OU SYS_FORK | (CLONE já existe; SIGCHLD const) | não |
| `execvp` | SYS_EXECVE + PATH-resolve em Teko | SYS_EXECVE | não (argv/envp = `char**` = ponteiro) |
| `pipe` | SYS_PIPE2 | SYS_PIPE2 | não (`int[2]` via ponteiro) |
| `waitpid` | SYS_WAIT4 | SYS_WAIT4 | não (`int* status` via ponteiro) |
| `poll` (`fd_wait_readable`) | SYS_PPOLL | SYS_PPOLL | `struct pollfd*` = ponteiro para array (extern type `Pollfd`, R3) |
| `dup2` (redirect) | SYS_DUP3 | SYS_DUP3 | não |

**Contrato Teko (braço Linux, para o implementador):**
```teko
/**
 * Spawn a child that runs `argv[0]` resolved through `$PATH`, returning its raw pid.
 *
 * @param argv the argument vector; `argv[0]` is the program name resolved via `$PATH`
 * @return the child pid, or a negative `-errno` word if `clone`/`execve` failed
 */
#os("linux")
fn spawn_linux(argv: []str): i64 { /* SYS_CLONE(SIGCHLD) -> child: SYS_EXECVE(resolve_path(argv[0]), argv, environ) */ }
```
**Conclusão F6-Linux:** construível já — é F4-shaped (novos consts + extern types ponteiro-based +
raw syscall). Sai no mesmo braço-Linux que fecha o self-host.

### 2.2 — Braço macOS (CONSTRUÍVEL JÁ via extern-fn libSystem)
`posix_spawn`/`fork`/`waitpid`/`pipe`/`poll` de `System` via `extern fn … from "System"` (o padrão
`os_mmap` de `arena.tks` já vive). `posix_spawn` recebe `posix_spawn_file_actions_t*` por ponteiro —
sem struct-by-value. Braço mac fecha com R4 (`#os`-guard) — não precisa de import-lib linker.

### 2.3 — Braço WINDOWS — o FORK REAL (dep não-nossa, decisão de design aberta)
Aqui e só aqui está o bloqueio. `CreateProcessA` preenche `STARTUPINFOA` (struct por valor, ~68
bytes) e retorna `PROCESS_INFORMATION` (struct por valor). Duas deps não-nossas:
1. **Struct-by-value-FFI reverse** — passar/retornar struct por valor honrando a classificação
   sret/register-pair do ABI Win64 (`abi_win64.tks` existe; falta o caminho reverse-FFI que
   `star-ref-and-ffi-0.3.1.md §4` desenha, ainda não aterrado).
2. **Linker de import-lib Win32** — resolver `kernel32.dll`/`ntdll.dll` sem `cc`/`ld`. É o **M-linker
   / Level-2** de `package-toolchain-and-own-linker-0.3.x.md §2` (reusa `objfile_coff.tks` como leitor
   inverso; sintetiza `_start`; emite imagem freestanding). Level-1 já linka ELF sem `cc` mas ainda
   usa crt libc; o `freestanding` knob (`project.tks:921`) detoura a `invoke_cc` — o último ponto onde
   o caminho native alcança um driver C.

**O FORK pro dono (F6-Windows):** a metade-processo do Windows só fecha quando (1) struct-by-value-FFI
reverse E (2) M-linker de import-lib existirem. Isto NÃO é "é difícil" — é uma **ordem de precedência
de milestones**: o M-linker é um milestone próprio (kernel bare-metal o força). **Decisão aberta:** o
§16 espera o M-linker (serializa F6-Win depois da Fase E), OU o §16 fecha o SWEEP no braço Linux+mac
primeiro (o `teko.c` cross-compila por `#if`, então o braço Windows pode ser o ÚLTIMO `#if` a virar
Teko) e o braço-Win-processo é o último resíduo C sob `#ifdef _WIN32`, apagado quando o M-linker
fecha. **Recomendação law-first:** a segunda — Linux+mac fecham o self-host e o zero-libc HOJE;
o braço Win-processo permanece como o único `#if _WIN32` residual até o M-linker (que já é milestone
ratificado por outra frente). Isto respeita "if it exists in C, it exists in Teko" (o braço Win VAI
existir em Teko) sem bloquear os 100% dos braços POSIX. **Surfar ao dono a escolha de precedência.**

---

## §3 — Caminho de construção de F7 (threads/sync/channels)

### 3.1 — O que JÁ existe (surpresa: ~70%)
- **Mutex/cond:** `sync.tks` — `futex_wait`/`futex_wake` via SYS_FUTEX raw (Linux), `os_sync_wait_on_address` (macOS 14.4+ libSystem), `WaitOnAddress` (Windows synchronization.lib). Mutex de 3 estados + cond por sequência — completo os 3 OSes.
- **Stack de thread:** `thread.tks` — `thr_mmap` (SYS_MMAP) + `thr_guard` (SYS_MPROTECT PROT_NONE) + free (SYS_MUNMAP); braços mac (`mprotect` System) e win (`VirtualProtect` kernel32).
- **Intrínseco `thread_clone`** + `cg_emit_thread_clone_helper` (raw `clone` com trampolim de entrada: empilha entry/arg, `syscall 56`, child chama e `exit`).

### 3.2 — O que FALTA (o spawn glue)
- ⧗ **O SPAWN em runtime ainda é `pthread_create`** (`teko_rt.c:2305/2318`, `tk_thread_spawn`). Trocar por: `thread_stack_new()` (já existe) → `thread_clone(entry, stack_top, arg, ctid, flags)` (intrínseco já existe) → **TLS setup** via `SYS_set_tid_address` + `CLONE_SETTLS` (const `CLONE_SETTLS` já em `sys.tks:116`; falta SYS_SET_TID_ADDRESS + o arch-detail de `%fs`/`tpidr_el0`).
- ⧗ **join/detach:** `CLONE_CHILD_CLEARTID` (já const) + futex-wait no ctid word (a primitiva `futex_wait` já existe) — é composição pura dos blocos existentes.
- ⧗ **braço mac/win spawn:** `bsdthread_create`/`pthread_create-from-System` (mac) e `CreateThread` from kernel32 (win) — extern-fn, R4/`#os`-guard.

**Contrato Teko (spawn Linux, para o implementador):**
```teko
/**
 * Spawn an OS thread that enters `entry(arg)` on a fresh guarded stack.
 *
 * @param entry the machine address of the thread trampoline entry
 * @param arg   the single word passed to `entry`
 * @return the child tid, or 0 if the stack `mmap` or `clone` failed
 */
#os("linux")
fn thread_spawn_linux(entry: u64, arg: u64): u64 {
    var stack_top = teko::runtime::thread_stack_new()
    if stack_top == 0 { return 0 }
    var flags = teko::sys::CLONE_VM | teko::sys::CLONE_FS | teko::sys::CLONE_FILES | teko::sys::CLONE_SIGHAND | teko::sys::CLONE_THREAD | teko::sys::CLONE_SYSVSEM | teko::sys::CLONE_SETTLS | teko::sys::CLONE_CHILD_CLEARTID
    teko::thread_clone(entry to i64, stack_top to i64, arg to i64, 0, flags) to u64
}
```

### 3.3 — Channels (o resíduo mais profundo de F7)
- ⧗ `tk_oschan` AF_UNIX (abstract-namespace socket) — consts SOCKET/BIND/SENDTO/RECVFROM + `extern type Sockaddr_un` (R3). Braço Linux/mac raw-syscall/extern-fn.
- ⛔ **Windows sem AF_UNIX** — divergência de ARQUITETURA (não de símbolo): transporte alternativo = **Named Pipes**. **REPORTAR a §10** (não inventar aqui); é design de subsistema, não de expurgo.
- **O FORK pro dono (F7-channels-Windows):** o transporte de canal no Windows precisa de decisão de arquitetura (Named Pipes vs socket TCP-loopback vs shared-mem+futex). Surfar; não pré-decidir.

### 3.4 — Conclusão F7
Threads **NÃO é fork** — é construível JÁ nos braços Linux/mac (composição dos blocos existentes +
TLS + poucos consts). O único fork é o **transporte de canal no Windows** (§10). D84 retrata
corretamente o "deferido §17+" do mapa.

---

## §4 — Sequência de orquestração concreta (fase → bites → ritual → deps)

Serial, reseed-serial (D84). Cada bite = arquivo/fn concreto. LINUX-primeiro onde o raw-syscall fecha;
o braço mac (extern-fn libSystem) acompanha por R4; o braço Windows-processo/channel espera M-linker.

### F1 — intrínsecos do emit (self-host gate; adiantável JÁ) — **começa agora**
- **Bite 1.1:** `floor` soft-float OU garantia de baixamento-a-instrução. Arquivo: `codegen.tks:3549` (a decisão builtin→soft) + possível intrínseco novo em `scope.tks`. **Este é o bite que fecha o `-lm`.**
- **Bite 1.2:** remover `#include <stdlib.h>` do preâmbulo (`codegen.tks:9621`) — depende de F3-braço-C-morto + panic/exit-SYS no emit; SEQUENCIAR após F3.
- **Deps:** F0 (✓). **Ritual:** fixpoint tri-gen + reseed; S4/S5/S6 + `nm -u` sem `floor`.
- **Braço:** todos (intrínseco de codegen, sem SO).

### F2 — exit/write — ✓ (fechado no leg-Teko; só o emit-C herda de F1)
- **Bite 2.1:** confirmar exit-SYS no emit quando F1-stdlib-drop. **Ritual:** S1/S2. **Braço:** todos.

### F3 — arena (KEYSTONE; RITUAL) — o gargalo
- **Bite 3.1:** flipar `cg_arena_provider_ladder` (`codegen.tks:177`) para apontar mac/win aos símbolos Teko (`ar_mmap` mac/win já existem em `arena.tks`).
- **Bite 3.2:** reseed load-bearing + MEM_PARANOID.
- **Deps:** F0, `load_u64`/`store_u64` (✓). **Ritual (o mais perigoso):** `tc1==tc2==tc3` + árvore completa + `nm -u` sem `malloc`/`free`/`posix_memalign`.
- **Braço:** Linux ✓; mac/win = flip + provar self-host mac/win.

### F4 — fs/env/tempo/random (RITUAL) — LINUX-primeiro
- **Bite 4.1:** adicionar consts a `sys.tks`: GETDENTS64, MKDIRAT, NEWFSTATAT, CHDIR, GETCWD, DUP3, GETRUSAGE, UNLINKAT (+ mac/win).
- **Bite 4.2:** `extern type` skeletons `#os`-guarded: `Statx`/`Stat`, `linux_dirent64`, `Rusage`, `Timespec` (compilam hoje, C1 ✓).
- **Bite 4.3:** reescrever `fs.tks` (list_dir→getdents64+parse; mkdir→mkdirat; stat→statx), `time.tks` (monotonic→clock_gettime; localtime→civil Teko puro), `env.tks` (getenv/setenv→`environ`-Teko; chdir/getcwd→syscall), `rand.tks` (secure_bytes→getrandom) — trocar `tk_rt_*` por raw-syscall Linux / extern-fn mac.
- **Deps:** F3 (aloca por arena). **Ritual:** S7-S14; braço-fs de `win32_compat.h` órfão (D-TS1); `nm -u` sem fs/time/env/rand libc.
- **Braço:** Linux raw ✓; mac extern-fn ✓ (R4); Windows fs via `#os`-guard extern-fn (D-TS1 desbloqueou).

### F5 — assert (leaf após F1-F4)
- **Bite 5.1:** migrar `tk_assert_scenario_*` (3 seams) para estado-Teko (`#singleton`). **Ritual:** S15.
- **Braço:** todos (Teko puro).

### F6 — process — LINUX+mac JÁ; Windows espera M-linker
- **Bite 6.1 (Linux):** consts SYS_EXECVE/WAIT4/PIPE2/PPOLL/DUP3 + `extern type Pollfd`; reescrever `process.tks` braço-Linux (spawn via clone+execve, PATH-resolve Teko, pipe2, wait4, ppoll).
- **Bite 6.2 (mac):** `posix_spawn`/`waitpid`/`pipe`/`poll` from `System` (`#os`-guard).
- **Bite 6.3 (Windows) — ESPERA:** struct-by-value-FFI reverse + M-linker import-lib (Fase E).
- **Deps:** F3, F4. **Ritual:** spawn `/bin/true`→0, pipe round-trip; `nm -u` sem fork/execvp/waitpid/pipe/poll (braços POSIX).
- **Braço:** Linux ✓ (agora), mac ✓ (agora), Windows ⛔ (M-linker).

### F7 — threads/sync/channels — LINUX+mac JÁ; channels-Windows a §10
- **Bite 7.1 (Linux threads):** const SYS_SET_TID_ADDRESS + arch-TLS (`%fs`/`tpidr_el0`); trocar `pthread_create` (`teko_rt.c`) por `thread_spawn_linux` (§3.2 contrato); join via CLONE_CHILD_CLEARTID+futex.
- **Bite 7.2 (mac/win spawn):** `bsdthread_create`/`CreateThread` extern-fn.
- **Bite 7.3 (channels Linux/mac):** consts SOCKET/BIND/SENDTO/RECVFROM + `extern type Sockaddr_un`; migrar `tk_oschan`.
- **Bite 7.4 (channels Windows) — ESPERA §10:** Named Pipes (decisão de arquitetura, surfar).
- **Deps:** F3 (stack via arena/mmap). **Ritual:** spawn+join soma; mutex contention; `nm -u` sem pthread_*.
- **Braço:** Linux ✓, mac ✓, Windows-thread ✓ (CreateThread), Windows-channel ⛔ (§10).

### F8 — test/crash (POR ÚLTIMO antes do SWEEP; alto risco)
- **Bite 8.1:** `capture_panic` cooperativo (D48/R2) — panic/exit mudam ROTA, rodam defers+arenas como return, detecção `TEKO_TEST_GATE`. Remove setjmp/longjmp. WHOLE-PROGRAM.
- **Bite 8.2:** `signal`→SYS_rt_sigaction + handler Teko (`extern type Sigaction`, R3); baixa prioridade.
- **Bite 8.3:** `backtrace`/`dladdr`→unwind `.eh_frame`/frame-pointers Teko (R5, sem degradar).
- **Deps:** F1-F7. **Ritual:** suíte captura panic sem morrer; `nm -u` sem setjmp/longjmp/signal/backtrace/dladdr.
- **Braço:** todos.

### F9 — SWEEP (RITUAL TERMINAL)
- **Bite 9.1:** parar de emitir `#include "teko_rt.h"`/`"assert.h"`/`<stdlib.h>` (reseed).
- **Bite 9.2:** deletar `teko_rt.{c,h}`+`assert.c`+`win32_compat.h`.
- **Bite 9.3:** ajustar scripts CI — remover `-lm -ldl -pthread` (`build_gen1_from_c.sh:51`, `package_release.sh:140`, `native_linux_asset.sh:211/281`, `win/build_gen1_from_c.ps1`).
- **Deps:** TODAS. **Ritual FINAL:** `cc bootstrap/teko.c` **sem `-lm`/lib C** compila + tc2==tc3 + MEM_PARANOID + `nm -u` sem NENHUM `@GLIBC`/libm; alvo `-ffreestanding -nostdlib` linka. S16.
- **Braço:** POSIX fecha 100%; o `#ifdef _WIN32` de processo/channel é o único resíduo C se o M-linker não fechou (o SWEEP dos braços POSIX não espera o Windows-processo — o `#if` isola).

---

## §5 — Prova de aceite zero-libc por fase (a coluna nova)

Cada ritual point ADICIONA, ao fixpoint/reseed, a prova de link zero-libc do braço migrado:

| Fase | prova de aceite (além do fixpoint) | símbolo libc que NÃO pode restar |
|---|---|---|
| F1 | `cc bootstrap/teko.c` sem `-lm` linka (após F3) | `floor`, `ceil`, `round` |
| F2 | `nm -u` do emit | `fwrite`,`fputs`,`fputc`,`snprintf`,`vsnprintf` |
| F3 | `nm -u` + MEM_PARANOID | `malloc`,`free`,`realloc`,`posix_memalign`,`aligned_alloc` |
| F4 | `nm -u` (braços POSIX) | `open`,`stat`,`getdents`,`mkdir`,`chdir`,`getcwd`,`clock_gettime`,`localtime_r`,`getrandom`,`getrusage`,`getenv`,`setenv` |
| F5 | `nm -u` | `assert__*` |
| F6 | `nm -u` (POSIX) | `fork`,`execvp`,`waitpid`,`pipe`,`poll`,`dup2` |
| F7 | `nm -u` | `pthread_*`,`socket`,`bind`,`sendto`,`recvfrom` |
| F8 | `nm -u` | `setjmp`,`longjmp`,`signal`,`backtrace`,`dladdr` |
| F9 | `nm -u bootstrap/teko` GLOBAL | NENHUM `@GLIBC`/`@GLIBC_*`/libm; `-nostdlib` linka |

O SWEEP não é "apagar os arquivos" — é **provar que o link não puxa libc/libm**. É o degrau para o
native: um `teko.c` zero-libc é a última forma-C antes de o gen2/gen3 native emitir código de máquina
sem passar por C.

---

## §6 — Forks REAIS pro dono (decisões de design ainda abertas — NÃO pré-decididas)

1. **F1-floor:** soft-float próprio (portável, zero-libm garantido) vs `__builtin_floor` com asserção
   de baixamento-a-instrução (`-msse4.1`/`frintm`, mais simples mas frágil sob `-ffreestanding`/alvo
   genérico). **Recomendo soft-float** (único zero-libm portável; alinhado ao native-como-norte).
2. **F6-Windows-processo (precedência de milestone):** o §16 SERIALIZA F6-Win atrás do M-linker
   (import-lib) + struct-by-value-FFI reverse, OU fecha o SWEEP nos braços POSIX (Linux+mac) já e
   deixa o `#ifdef _WIN32` de processo como o único resíduo C até o M-linker? **Recomendo a 2ª**
   (POSIX fecha o zero-libc HOJE; o Win-processo vira Teko quando o M-linker — milestone independente —
   fechar). Precisa do OK do dono na precedência.
3. **F7-channels-Windows (arquitetura de transporte):** Named Pipes vs TCP-loopback vs shared-mem+futex
   para substituir AF_UNIX (que o Windows não tem). É design de subsistema §10 — **REPORTAR, não
   inventar aqui**. Surfar ao dono/§10.

Nenhum desses é uma TENSÃO DE LEI (não há HALT) — são escolhas de precedência/arquitetura que o dono
possui. Todo o resto (F1-F5, F6-Linux/mac, F7-Linux/mac) é construível já sem ruling.

---

## §7 — Índice
- §0 Sumário do delta (o que mudou desde `be8fc1e1`)
- §1 Auditoria por-fase (feito/pendente @ HEAD) + prova de aceite zero-libc
- §2 Caminho de F6 (process): Linux/mac construível já × Windows = fork (struct-FFI + M-linker)
- §3 Caminho de F7 (threads/channels): ~70% já; único fork = transporte de canal Windows (§10)
- §4 Orquestração concreta (fase→bites→ritual→deps; Linux-primeiro)
- §5 Prova de aceite zero-libc por fase (a coluna nova do critério corrigido)
- §6 Forks REAIS pro dono (floor soft-float; precedência F6-Win; transporte de canal Win)
