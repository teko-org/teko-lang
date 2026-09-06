# §16 — EXPURGO COMPLETO da libc: o mapa-mestre de TODA dep-C → Teko + syscall/FFI-do-SO

Status: DESIGN (architect). Read-and-design ONLY — nenhum código de produto é escrito aqui.
Base: `origin/fix/retirement` HEAD `be8fc1e1`. Autor: arquiteto.

> **O que este documento É.** O owner ruling "VIRADA §16 — libc está FORA … TROCAR TODAS as deps C,
> inclusive a arena, por Teko+syscall, SEM EXCEÇÕES" (`mudancas-superficie-0.3.1.md` §11.2, 2026-08-16)
> exige que AMBAS as pernas de build — a perna C (`cc bootstrap/teko.c -lm`) e a perna native (código
> de máquina direto, que **não pode** `#include` nem linkar header/lib C) — fechem com **zero
> dependência de header/biblioteca C**. O critério de aceitação é o **SWEEP**: deletar
> `src/runtime/teko_rt.{c,h}`, `src/assert/assert.c`, `src/win32_compat.h` e o build **ainda** compilar
> e passar o fixpoint (`mudancas-superficie` §11.2, "CRITÉRIO DE ACEITAÇÃO"). Este documento é o
> **mapa-mestre**: enumera CADA header/símbolo C que hoje existe nas duas pernas, classifica, dá a
> substituição por-plataforma (Linux raw-syscall / macOS libSystem / Windows kernel32-ntdll) e por-perna
> (C emite / native lowera), e reconcilia com o que os docs-§16 já cobrem — marcando o que é NOVO.
>
> **O que este documento NÃO É.** Não é o desenho detalhado de cada subsistema — a arena
> (`plano-s16-arena-mmap.md`), o intrínseco de syscall (`plano-s16-syscall-intrinsic.md`), a fundação
> FFI (`plano-s16-fundacao-crumbs.md`) e o monólito cross-arch (`plano-s16-monolith-cc-emit.md`) já são
> os desenhos por-subsistema, e este mapa REFERENCIA-os, não os reescreve. Onde um dep ainda não tem
> desenho, este doc entrega o CONTRATO (símbolos, classe, syscall/FFI alvo, mecanismo de perna) e o
> marca **NOVO**, para o implementador resumir em minutos.

---

## §0 — Ground truth (medido em HEAD `be8fc1e1`)

Superfície-C a retirar (o SWEEP): `src/runtime/teko_rt.c` (5562 linhas) + `teko_rt.h` (1759) +
`src/win32_compat.h` (339) + `src/assert/assert.{c,h}` (~256). ~264 fns públicas sobre ~242
libc/syscall, em 21 subsistemas (`mudancas-superficie` §11.2 scout).

**A superfície-C tem DOIS locais, e o §16 tem de matar OS DOIS:**

1. **O C EMITIDO** (`bootstrap/teko.c`, o self-image + todo programa gerado). Codegen emite os includes
   em `src/codegen/codegen.tks:13912-13918`:
   ```
   :13912  #include <stdint.h>
   :13913  #include <stdbool.h>
   :13914  #include <stdlib.h>   // malloc/abort — slice copy-append
   :13915  #include <math.h>     // floor/… — float ops (link -lm)
   :13916  #include <string.h>   // (guarded por spawn_sites.len > 0)
   :13918  #include "assert.h"   // teko::assert seed decls
   ```
   e `teko.c` também **re-emite** `#include <math.h>` etc. nas strings do codegen para os programas que
   ELE compila (`codegen.tks:~186939`). Contagens reais em `bootstrap/teko.c`: `malloc` 1393× (todas via
   o seam da arena), `abort` 1397× (via `tk_panic`), `floor(` 5× (fp real), `memcpy` 2× (só guarded).
2. **O RUNTIME à-mão** (`teko_rt.c` + `teko_rt.h` + `win32_compat.h` + `assert.c`), linkado ao lado. Os
   `#include` medidos em `teko_rt.c` (`grep ^#include`): 28 headers POSIX/C + o `win32_compat.h`.

**O mecanismo de substituição já AVALIZADO (owner, per-SO):**

| Caso | Mecanismo | compilador C? |
|---|---|---|
| Linux — core/hot | **syscall CRU = intrínseco de codegen** (asm inline `syscall`/instrução native); números no `teko::sys` | não |
| macOS | bind de símbolos do **libSystem.dylib** via `extern fn`, resolvido pelo linker | não |
| Windows | bind de **kernel32.dll/ntdll.dll** via `extern fn`, resolvido pelo linker | não |
| env (`environ` é memória) | **Teko puro** sobre `environ` | não |
| Libs opcionais (openssl/…) | **FFI em runtime** (`dlopen`/`dlsym`) — fora do §16-core | não |

Quirks por-SO que o mapa herda (owner): **Linux** mmap/futex/AF_UNIX · **macOS** mmap-ANON/kqueue/
getentropy · **Windows** VirtualAlloc/Events/**sem** AF_UNIX.

---

## §1 — A TABELA-MESTRE (uma linha por dep-C)

Legenda de classe: **T**=type-only (o native supõe seus próprios tipos → no-op nativo; a perna C ou
mantém o header trivial ou emite os `typedef` próprios) · **I**=inline/intrínseco (poucas linhas de
Teko ou um intrínseco de codegen) · **S**=syscall-backed (raw `teko::sys::syscallN` no Linux; `extern
fn` a libSystem/kernel32 no mac/win) · **X**=complexo/stateful (buffering, dylib, sinal/setjmp, sockets,
threads). Coluna "doc": **✔ coberto** (desenho existe) · **◑ parcial** · **NOVO** (sem desenho — contrato
abaixo).

Mecanismo de perna (vale para TODA linha S, salvo nota): **perna C** o codegen emite `tk_syscallN(nr,
…)` (helper `asm volatile("syscall")` no preâmbulo, `plano-s16-syscall-intrinsic.md` §2) ou o `extern
fn` cru; **perna native** lowera a instrução `syscall`/`svc`/`SYSCALL` direto (honest-stop hoje,
Doc-2 terminal, `syscall-intrinsic` §3). Endereços cruzam por `ptr_word`/`ref_word`/`word_ptr`.

### 1.1 — Núcleo do C EMITIDO (o caminho crítico do self-host — `codegen.tks:13912-13918`)

| # | header | símbolos REAIS usados | classe | Linux | macOS | Windows | doc |
|---|---|---|---|---|---|---|---|
| 1 | `<stdint.h>` | `uint8_t`,`int64_t`,`uint64_t`,`uintptr_t`,`int32_t`,`uint32_t` | **T** | native: tipos próprios; C: emitir `typedef`s próprios (largura fixa) OU manter (header-only, zero-símbolo) | idem | idem | ✔ (trivial) |
| 2 | `<stdbool.h>` | `bool`,`true`,`false` | **T** | `_Bool` é builtin do C; native já tem `bool` | idem | idem | ✔ (trivial) |
| 3 | `<stdlib.h>` (emit) | `malloc`,`abort` (slice copy-append + OOM) | **S**+**I** | `malloc`→arena/**SYS_mmap**; `abort`→**SYS_exit_group**+SYS_write(msg) | mmap-ANON / `_exit` libSystem | VirtualAlloc / `ExitProcess` | ✔ arena-mmap; ✔ syscall §7 |
| 4 | `<math.h>` | `floor` (5×) + fp `/` checado | **I** | `floor` = intrínseco de codegen (soft ou builtin `__builtin_floor` SEM header) | idem | idem | **◑** (float-bits ✔; `floor`/`round`/`ceil` NOVO) |
| 5 | `<string.h>` (emit) | `memcpy` (2×, guarded `spawn_sites`) | **I** | `memcpy` = intrínseco de codegen (loop inline / `__builtin_memcpy`) | idem | idem | ✔ (fundacao C5 usa o mesmo padrão p/ f64_bits) |
| 6 | `"assert.h"` / `assert.c` | `assert`,`assert__eq_i64/_u64/_f64/_str/_bytes_eq/_ge_i64/_gt_i64/_is_error/_is_absent` | **X** | comparação de valor + `panic` (L0-shaped) → **Teko puro** sobre os corpos migrados; sem símbolo C | idem | idem | **NOVO** (migração doc Fase 6; sem desenho de detalhe) |

> **Nota #1/#2 (type-only).** O native NUNCA inclui um header; ele já materializa `i64`/`byte`/`bool`
> como tipos-máquina. A perna C só precisa que `uint8_t`&cia existam; hoje vêm de `<stdint.h>`. Duas
> saídas law-first: (a) o codegen emite um bloco fixo de `typedef unsigned char uint8_t; …` (zero
> header, o padrão que Go/musl usam) — RECOMENDADO, é o que fecha o SWEEP; (b) manter os dois headers
> como exceção "type-only, zero-símbolo-de-lib" (mais frouxo, contraria o "sem exceções"). **Recomendo
> (a):** é mecânico e o único que faz `cc bootstrap/teko.c` compilar sem `-include` de header C.

### 1.2 — `teko_rt.c` — computação pura / char (sem SO)

| # | header | símbolos REAIS | classe | Linux | macOS | Windows | doc |
|---|---|---|---|---|---|---|---|
| 7 | `<ctype.h>` | `isalpha` (3×) [+ família ROUND-0] | **I** | scan `[]byte` puro em Teko (RFC-3629 / categorias) | idem | idem | ✔ fundacao Fase 1/3 |
| 8 | `<inttypes.h>` | `PRId64`,`PRIx64`,`PRIX64` (macros de format) | **T/I** | os `fmt_*` já são Teko puro (`teko_rt.tks`) → macro morre com o caminho C de fmt | idem | idem | ✔ fundacao Fase 1 |
| 9 | `<stddef.h>` | `max_align_t`,`offsetof`,`size_t` | **T** | arena mmap dá alinhamento de página (⊇16) → `max_align_t` desnecessário; `size_t`→`u64` | idem | idem | ✔ arena-mmap §2.3 |
| 10 | `<stdarg.h>` | `va_list`,`va_start`,`va_copy`,`va_end` (`fmt_alloc_vsnprintf`, issue #48) | **X→removido** | o `%.*f` variádico morre com o caminho C de fmt; o fmt Teko não é variádico (monomorfiza por arity) | idem | idem | **◑** (fmt ✔; o float-bottom `%.17g` NOVO — ver #11) |

### 1.3 — `teko_rt.c` — I/O + panic + exit (o caminho de 11 símbolos vivos no native)

| # | header | símbolos REAIS | classe | Linux | macOS | Windows | doc |
|---|---|---|---|---|---|---|---|
| 11 | `<stdio.h>` | `fwrite`(17),`fputc`(12),`fputs`(17),`stdout`,`stderr`,`snprintf`(29),`vsnprintf`(3) | **S**+**◑** | write→**SYS_write**(fd 1/2); SEM buffering (write direto); `snprintf("%.17g")` = float-bottom (ftoa Teko puro) | write libSystem | WriteFile/kernel32 | ✔ write (syscall §7 crumb 2); **NOVO** o float `%.17g→str` (ftoa preciso Teko) |
| 12 | `<stdlib.h>` exit | `exit`(33),`_Exit`(9),`abort`(23) | **S** | **SYS_exit_group** (encerra o processo, não só a thread) | `exit` libSystem | `ExitProcess` | ✔ syscall §5/§7 crumb 1 |

### 1.4 — `teko_rt.c` — memória (a ARENA, keystone)

| # | header | símbolos REAIS | classe | Linux | macOS | Windows | doc |
|---|---|---|---|---|---|---|---|
| 13 | `<stdlib.h>` alloc | `malloc`(69),`free`(110),`realloc`(17),`posix_memalign`(3) | **S** | **SYS_mmap**/**SYS_munmap** + META-POOL (sub-aloca headers 64 B; página≠64 B) | mmap-ANON | VirtualAlloc/VirtualFree | ✔ **arena-mmap** (o keystone inteiro) |
| 14 | `<malloc.h>` (win) | `_aligned_malloc`,`_aligned_free` | **S** | (n/a) | (n/a) | VirtualAlloc (align de página ⊇16) | ✔ arena-mmap (braço win notado) |

### 1.5 — `teko_rt.c` — FFI de host: fs + env + tempo + random

| # | header | símbolos REAIS | classe | Linux | macOS | Windows | doc |
|---|---|---|---|---|---|---|---|
| 15 | `<unistd.h>` fs/io | `read`(46),`write`(43),`close`(14),`chdir`,`getcwd`,`dup`,`dup2`(11),`_exit` | **S** | SYS_read/write/close/chdir/getcwd/dup2 | libSystem | ReadFile/WriteFile/SetCurrentDirectory | ✔ read/write (syscall §7); **NOVO** chdir/getcwd/dup2 (fs) |
| 16 | `<fcntl.h>` | `open`(21),`O_WRONLY`,`O_CREAT`,`O_APPEND`,`O_RDONLY`,`O_NONBLOCK`,`FD_CLOEXEC` | **S**+consts | **SYS_openat** + consts `#os` no `teko::sys` | `open` libSystem + consts | CreateFile + disposições | **NOVO** (fs; consts = padrão monolith §1.4) |
| 17 | `<sys/stat.h>` | `mkdir`,`stat`/`_S_IREAD`/`_S_IWRITE` | **S**+**T** | **SYS_mkdirat**/**SYS_newfstatat**; `struct stat`=`extern type=struct` | libSystem + `struct stat` (layout Darwin!) | `_mkdir`/GetFileAttributes | **NOVO** (fs; layout de `stat` DIVERGE por-SO — flag) |
| 18 | `<dirent.h>` | `opendir`(3),`readdir`(3),`closedir`(1) | **S** | **SYS_getdents64** (buffer + parse em Teko) | `getdirentries`/`readdir` libSystem | FindFirstFile/FindNextFile (win32_compat) | **NOVO** (fs; `linux_dirent64`=extern struct) |
| 19 | `<sys/random.h>` | `getrandom`(4),`getentropy`(4) | **S** | **SYS_getrandom** (flags 0) | `getentropy` libSystem | BCryptGenRandom / RtlGenRandom | ✔ fundacao Fase 4 / syscall §7 crumb 5 |
| 20 | `<sys/resource.h>` | `getrusage`(2) (`teko::mem::peak_rss`, #148) | **S** | **SYS_getrusage** + `extern type Rusage` | `getrusage` libSystem | GetProcessMemoryInfo | **NOVO** (leaf pequeno) |
| 21 | `<time.h>` | `clock_gettime`(3),`localtime_r`(2),`CLOCK_REALTIME`,`CLOCK_MONOTONIC` | **S**+**I** | **SYS_clock_gettime** + `Timespec`; `localtime_r`→**civil-calendar Teko puro** | clock_gettime libSystem | GetSystemTimePreciseAsFileTime | ✔ clock (fundacao Fase 4); **NOVO** `localtime_r`→Teko (algoritmo Howard Hinnant, puro) |
| 22 | `<stdlib.h>` env | `getenv`(15),`setenv`(3) | **I/S** | **Teko puro sobre `environ`** (owner: env é memória) OU `extern fn getenv/setenv` | idem | GetEnvironmentVariable | ✔ fundacao §11.1 (getenv/setenv recipe); **◑** o purista `environ`-Teko é NOVO |

### 1.6 — `teko_rt.c` — FFI de host: processo / pipes / redirect (o BLOCO GRANDE)

| # | header | símbolos REAIS | classe | Linux | macOS | Windows | doc |
|---|---|---|---|---|---|---|---|
| 23 | `<unistd.h>` proc | `fork`(10),`execvp`(9),`pipe`(11) | **S/X** | **SYS_clone**/**SYS_execve**/**SYS_pipe2** (execvp = resolver PATH em Teko) | `posix_spawn`/fork libSystem | CreateProcess (win32_compat) | **NOVO** (process; bloqueado — ver §5 R4) |
| 24 | `<sys/wait.h>` | `waitpid`(5) | **S** | **SYS_wait4** | wait4 libSystem | WaitForSingleObject+GetExitCode | **NOVO** (process) |
| 25 | `<poll.h>` | `poll`(4) (`tk_rt_fd_wait_readable`, F5) | **S** | **SYS_ppoll** + `extern type Pollfd` | poll libSystem | WaitForMultipleObjects | **NOVO** (process/pipes) |
| 26 | `<io.h>` (win) | `_dup`,`_dup2`,`_close`,`_pipe`,`_read`,`_get_osfhandle`,`_open`,`_write` | **S** | (n/a) | (n/a) | kernel32 HANDLE-based (win32_compat) | **NOVO** (bloqueado, Fase E) |
| — | `<sys/stat.h>` win | (mkdir mode via `_S_I*`) | — | — | — | (coberto em #17) | — |

### 1.7 — `teko_rt.c` — canais / threads / sync (§10, cross-thread)

| # | header | símbolos REAIS | classe | Linux | macOS | Windows | doc |
|---|---|---|---|---|---|---|---|
| 27 | `<pthread.h>` | `pthread_create`,`_detach`,`_join`,`_mutex_{init,lock,unlock,destroy}`,`_cond_{init,wait,signal,broadcast,destroy}`,`_getspecific` | **X** | **SYS_clone**(thread) + **SYS_futex**(mutex/cond) + TLS via `%fs`/`set_tid_address` | pthread libSystem (ABI estável) | CreateThread + SRWLOCK/CONDITION_VARIABLE | **NOVO/DEFERIDO** (owner: pthread TRANSITÓRIO; clone/CreateThread = §17+) |
| 28 | `<sys/socket.h>` | `socket`(11),`bind`(7),`sendto`(4),`recvfrom`(2) (`tk_oschan` AF_UNIX) | **S** | **SYS_socket**/**SYS_bind**/**SYS_sendto**/**SYS_recvfrom** | idem libSystem | Named Pipes (win **sem** AF_UNIX!) | **NOVO** (channels; win diverge de arquitetura) |
| 29 | `<sys/un.h>` | `struct sockaddr_un` (abstract-namespace) | **T** | `extern type Sockaddr_un = struct` | idem | (n/a — named pipes) | **NOVO** (com #28) |
| 30 | `<errno.h>` | `errno`,`EEXIST` | **I/consts** | resolvido-por-construção: o raw-syscall retorna `-errno` no `rax` (`syscall-intrinsic` §1.1); `EEXIST`=const `teko::sys` | idem | GetLastError | ✔ syscall-intrinsic (o `errno` TLS SOME) |

### 1.8 — `teko_rt.c` — harness de teste / crash / observabilidade (o núcleo irredutível-parcial)

| # | header | símbolos REAIS | classe | Linux | macOS | Windows | doc |
|---|---|---|---|---|---|---|---|
| 31 | `<setjmp.h>` | `setjmp`(3),`longjmp`(5) (`tk_test_run` — captura de panic sem matar a suíte) | **X** | **TENSÃO** — intrínseco de captura do backend native OU shim C mínimo residual | idem | idem | **NOVO/TENSÃO** (migração doc R3 — owner ruling) |
| 32 | `<signal.h>` | `signal`(35) (SIGSEGV/BUS/ILL/FPE crash-handler) | **S** | **SYS_rt_sigaction** + `extern type Sigaction`; corpo do handler = Teko | `sigaction` libSystem | SetUnhandledExceptionFilter | **NOVO** (crash; baixa prioridade, degradável) |
| 33 | `<execinfo.h>` | `backtrace`(26),`backtrace_symbols_fd`(8) | **X** | SEM syscall — stack-unwind próprio (ler `.eh_frame`/frame-pointers) OU degradar a "sem símbolos" | idem (só glibc/mac; musl não tem) | CaptureStackBackTrace/DbgHelp | **NOVO** (crash; DEGRADÁVEL — pode virar no-op) |
| 34 | `<dlfcn.h>` | `dladdr`(4) (obs: `TEKO_ARENA_OBS` symbolization) | **X** | só diagnóstico opcional — degradar a "sem nome de símbolo" | dladdr libSystem | SymFromAddr | **NOVO** (obs; DEGRADÁVEL) |

### 1.9 — `win32_compat.h` — o shim POSIX→Win32 (uma FOLHA de #15/#17/#18/#23/#26)

| # | header | símbolos REAIS | classe | mecanismo | doc |
|---|---|---|---|---|---|
| 35 | `<windows.h>` | `CreateProcessA`,`FindFirstFileA`,`FindNextFileA`,`FindClose`,`CreateFileA`,`DuplicateHandle`,`GetStdHandle`,`WaitForSingleObject`,`GetExitCodeProcess`,`GetEnvironmentStringsA` | **X/S** | `extern fn … from "kernel32"` per-target (target-guarded + import-lib linker) | **NOVO/BLOQUEADO** (migração doc §3.3 etapa B; Fase E) |
| 36 | `<direct.h>` | `_chdir`,`_mkdir`,`_getcwd` | **S** | idem, resolvidos por-alvo via target-guarded `extern fn` (a metade fs) | **NOVO** (migração doc §3.3 etapa A) |
| 37 | `<process.h>` | `_spawnvp`,`_P_WAIT` | **X** | substituído por CreateProcessA (#35) | **NOVO/BLOQUEADO** |

**`win32_compat.h` não tem vida própria** — some quando #15/#17/#18/#23/#26 migrarem (2 etapas:
metade-fs precisa só de target-guarded `extern fn` (ratified D-TS1, 2026-08-19); metade-processo precisa de struct-by-value-FFI + linker de
import-lib Win32, encostando na Fase E). `migracao-runtime-c-para-teko-0.3.1.md` §3.3.

**Contagem:** 37 linhas de dep (headers/subsistemas). **Cobertas por desenho §16 existente:** 1,2,3,5,7,
8,9,12,13,14,19,21(clock),22(recipe),30 = **15**. **Parciais** (◑, parte coberta / parte NOVA): 4,10,
11,20-nota,22-purista = **~4**. **NOVAS** (contrato aqui, sem desenho de detalhe): 6,15(fs),16,17,18,20,
21(localtime),23,24,25,26,27,28,29,31,32,33,34,35,36,37 = **~18**. (O somatório passa de 37 porque
vários headers dividem-se em sub-símbolos de classes distintas — a divisão real está nas células.)

---

## §2 — Reconciliação com o desenho §16 existente (NÃO duplicar)

O que já está **desenhado e/ou aterrado** — este mapa apenas aponta:

| Peça de infra §16 | Doc | O que cobre da tabela |
|---|---|---|
| **Intrínseco de syscall** `syscall0..6`, `ptr_word`, `ref_word` (aterrado `fb0ec8c7`, reseed `1a03a68e`) | `plano-s16-syscall-intrinsic.md` | O MECANISMO de toda linha **S**: perna C emite `tk_syscallN` asm-inline; perna native lowera a instrução (honest-stop hoje). `errno` (-rax). `SYS_write`/`SYS_exit_group` já no `teko::sys`. |
| **Arena sobre mmap** + `word_ptr` + `load_u64`/`store_u64` (C-leg) + META-POOL | `plano-s16-arena-mmap.md` | #3-alloc, #13, #14, #9-`stddef`. A circularidade de bootstrap, o P2-seam (`tk_arena_control_get/set`, JÁ em `teko_rt.h:390`), a escada L0→L3. |
| **Fundação FFI:** `extern type = struct` (C1, aterrado `c7ac134b`), `teko::sys` consts (C2), recipe `as_cstr`/`str_from_c` | `plano-s16-fundacao-crumbs.md` (+ §11 refresh) | #7 (ctype/char), #11-write, #19 (random), #21-clock, #22 (getenv/setenv). O float-bits intrínseco (C5) cobre o padrão de #4/#5. |
| **Monólito cross-arch** (`#os`/`#arch` const → ladder `#if` no C) | `plano-s16-monolith-cc-emit.md` | O MECANISMO de TODA const de `teko::sys` (SYS_*, O_*, PROT_*, MAP_*, AF_*, CLOCK_*, EEXIST) emitir na íntegra p/ cross-compile. Vale p/ #16, #17, #28, #30. |
| **Roadmap camada-2** (folha→SO, ordem L0-L6, como o native LINKA seu runtime Teko sem `cc`) | `migracao-runtime-c-para-teko-0.3.1.md` | A ORDEM geral, o núcleo irredutível (#31 setjmp, argc/argv, #32 signal, #34 dladdr, `tk_cov_dump`), a morte de `win32_compat.h` (#35-37). |

**O que é genuinamente NOVO (sem desenho de detalhe hoje) — o trabalho que este mapa expõe:**

1. **fs completo** (#15-chdir/getcwd, #16 open+flags, #17 stat/mkdir, #18 getdents64). Read/write já
   têm crumb; o RESTO do fs (abrir, listar diretório, stat, criar dir) NÃO. É a "Etapa A" que mata a
   metade-fs do `win32_compat.h`, mas os SYSCALLs Linux + os `extern type` (`Stat`, `linux_dirent64`)
   não estão desenhados. `stat`/`dirent` têm **layout que diverge por-SO** (Darwin `stat` ≠ Linux
   `stat`) — cada um é um `extern type = struct` per-`#os`.
2. **process/exec** (#23 clone/execve, #24 wait4, #25 ppoll, #26 io-win). Bloqueado (owner + migração
   doc §3.3 etapa B): precisa de struct-by-value-FFI completo + o linker de import-lib Win32 (Fase E).
3. **threads/sync** (#27). Owner: **pthread é TRANSITÓRIO; clone/CreateThread fica §17+**. NÃO
   desenhado ao nível de `SYS_clone`+`SYS_futex`+TLS. É o dep mais profundo depois da arena; os canais
   (#28) dependem dele.
4. **channels sobre sockets** (#28 AF_UNIX, #29 sockaddr_un). Windows **não tem AF_UNIX** → precisa de
   um transporte alternativo (Named Pipes) — divergência de ARQUITETURA, não só de símbolo.
5. **harness/crash** (#31 setjmp — TENSÃO real; #32 signal; #33 backtrace; #34 dladdr; #6 assert).
6. **`localtime_r`** (#21) → algoritmo de calendário civil em Teko puro (leaf, mas ainda por escrever).
7. **math `floor`/`round`/`ceil`** (#4) → intrínsecos de codegen (como f64_bits), NÃO cobertos pelo
   crumb de float-bits (que só faz o bit-reinterpret).
8. **type-only emit** (#1/#2) → o codegen emitir os `typedef` de largura fixa em vez de incluir
   `<stdint.h>`/`<stdbool.h>` (recomendação §1.1-nota-(a)).

---

## §3 — Sequência de retirada (ordem de alto-nível — a folha primeiro, o SWEEP por último)

A ordem NÃO é por tamanho — é por **profundidade de acoplamento ao SO** e pelo **gate de self-host**
(o núcleo do C emitido, §1.1, é o que o compilador compila a SI PRÓPRIO). Cada fase é gate-able e traz
seu reseed/fixpoint quando toca `codegen.tks`.

```
FASE 0  infra (JÁ AVANÇADA)  ── syscall-intrinsic ✔ · C1 extern-struct ✔ · teko::sys ✔ · monolith cc-emit (em desenho)
                                A ORDEM abaixo assume estas fechadas/em-voo.

FASE 1  math/string/type intrínsecos do EMIT  ── #4 floor/round/ceil + #5 memcpy + #1/#2 typedefs próprios
        (intrínsecos de codegen; matam <math.h>/<string.h>/<stdint.h>/<stdbool.h> do C emitido)   [self-host gate]

FASE 2  exit + write (I/O)  ── #12 SYS_exit_group (aterrado §5) · #11 SYS_write (aterrado §7 crumb 2)
        + o float-bottom ftoa/%.17g Teko puro (#10/#11)                                            [leaf/1-reseed]

FASE 3  ARENA sobre mmap  ── #13/#14/#3-alloc/#9. O KEYSTONE (plano-s16-arena-mmap). Reseed load-bearing.
        Tudo aloca por aqui → é o centro de gravidade; escada L0→L3.                               [RITUAL]

FASE 4  fs + env + tempo + random  ── #15-fs · #16 open · #17 stat/mkdir · #18 getdents · #19 random ✔
        · #21 clock ✔ + localtime Teko · #22 env · #20 getrusage. Mata a metade-fs do win32_compat.  [RITUAL]

FASE 5  panic/assert  ── #6 assert.c → Teko (comparação+panic, L0-shaped)                            [leaf após 1-4]

FASE 6  process/exec/pipes  ── #23 clone/execve · #24 wait4 · #25 ppoll · #26 io-win. Metade-processo
        do win32_compat. BLOQUEADO em struct-FFI + linker import-lib Win32 (Fase E).                [BLOQUEADO]

FASE 7  threads/sync/channels  ── #27 clone/futex (pthread transitório até aqui) · #28/#29 sockets.
        DEFERIDO a §17+ por ruling; canais dependem de threads.                                     [DEFERIDO]

FASE 8  test/crash  ── #31 setjmp (TENSÃO — ruling) · #32 signal · #33 backtrace · #34 dladdr.       [ruling do owner]

FASE 9  SWEEP  ── parar de emitir #include "teko_rt.h"/"assert.h"/win32_compat (RESEED); DELETAR os 4
        arquivos; ajustar ~20 scripts de CI/build; provar `cc bootstrap/teko.c -lm` compila +
        tc2==tc3 + MEM_PARANOID + provenance. O GATE TERMINAL do §16.                                [RITUAL FINAL]
```

**Por que esta ordem (a escada não pode quebrar):** Fase 1 primeiro porque mata o C do EMIT sem tocar
SO (self-host puro, o gate mais barato de provar). Fase 3 (arena) antes de 4-8 porque TODO subsistema
aloca por ela. As folhas puras (2,4,5) adiantam enquanto 6/7 (o mais difícil, com deps não-nossas —
struct-FFI + linker) esperam. Fase 9 é ÚLTIMA e é o TESTE, não a consequência — apagar antes de todos
os subsistemas migrarem provaria menos (`mudancas-superficie` §11.2 "sweep é o passo FINAL"). O seed
publicado emite C durante toda a transição; `ci_provision_teko.sh` provê o runtime da era do seed até
o sweep — a migração não quebra a escada porque o C do seed vem do release do seed.

---

## §4 — Grafo de dependências (o que bloqueia o quê)

```
                       ┌─────────────────────────────────────────────────┐
                       │  INFRA (Fase 0)                                  │
                       │  syscall-intrinsic ✔ ── ptr_word/ref_word ✔      │
                       │  extern type=struct (C1) ✔                       │
                       │  teko::sys consts (C2) ✔                         │
                       │  monolith cc-emit (#if ladder)  ◑ em desenho     │
                       └───────────────┬─────────────────────────────────┘
                                       │ (todo S/T depende daqui)
        ┌──────────────────────────────┼───────────────────────────────┐
        ▼                              ▼                               ▼
  FASE 1 math/string/type       FASE 2 exit+write            word_ptr + load/store C-leg
  (#4,#5,#1,#2)                 (#12 ✔, #11 ✔)               (arena-mmap §2.2 — PRÉ-REQ da arena)
  intrínsecos codegen                  │                               │
        │  [self-host gate]            │                               ▼
        └──────────────┬───────────────┘                        FASE 3 ARENA (#13,#14,#3,#9)
                       │                                         META-POOL · escada L0-L3  ◄── keystone
                       │                                                │ (TUDO aloca aqui)
                       ▼                                                ▼
              FASE 4 fs/env/tempo/random ──────────────────────►  FASE 5 assert (#6)
              (#15,#16,#17,#18,#19✔,#20,#21,#22)                       │
              mata metade-fs win32_compat                              │
                       │                                               │
                       ▼                                               │
           ┌───────────────────────┐                                  │
           │ BLOQUEADO em deps      │                                  │
           │ não-nossas:            │                                  │
           ▼                        ▼                                  │
  FASE 6 process/exec         FASE 7 threads/channels                  │
  (#23,#24,#25,#26)           (#27 clone/futex, #28/#29 sockets)       │
  ⟵ struct-FFI completo       ⟵ §17+ ruling (pthread transitório)      │
  ⟵ linker import-lib Win32   ⟵ canais dependem de threads            │
  (Fase E)                    Windows sem AF_UNIX → named pipes        │
           │                        │                                  │
           └────────────┬───────────┴──────────────────────────────────┘
                        ▼
              FASE 8 test/crash (#31 setjmp ⚖ TENSÃO, #32/#33/#34 degradáveis)
                        │
                        ▼
              FASE 9  SWEEP  ── delete os 4 · `cc bootstrap/teko.c -lm` · tc2==tc3   ◄── gate terminal §16
```

Arestas críticas: **arena (F3) é o gargalo** — precisa do C-leg load/store (arena-mmap §2.2, o
verdadeiro primeiro blocker: `load_u64`/`store_u64` só existem no native leg hoje) e do `word_ptr`
ANTES de qualquer coisa. **F6/F7 têm deps NÃO-NOSSAS** (struct-FFI reverse + linker de import-lib +
o ruling §17 de threads) → não force. **F9 depende de TODAS** (é o `rm` provado).

---

## §5 — Os 5 maiores riscos / questões que precisam de ruling do owner

### **OVERARCHING LAW — SEM ATALHOS (lei do dono, 2026-08-17)**
**NO shortcuts / workarounds — if it exists in C, it exists in Teko.** Toda função de libc vira implementação **real** em Teko (raw syscall na Linux / FFI-da-ABI-do-SO em macOS/Windows). Nenhum no-op, nenhum degrade sem ratificação explícita. Rulings R1–R5 ratificadas abaixo.

---

**R1 — Threads: `clone`/`futex` (Linux) + FFI-to-OS-thread-API — RATIFIED (owner, 2026-08-17)** 
**DECIDED:** threads = **raw `clone`/`futex` (Linux) + FFI-to-the-OS-thread-API (macOS libSystem, Windows kernel32/ntdll)**; pthread is **fully retired in §16, NOT deferred to §17**. The "keep pthread via FFI shared-lib" shortcut is **REJECTED**. 
O §16 fecha com threads via raw syscall/FFI direto do kernel — sem trampolim C para `libpthread` (a shared-lib nativa será suportada apenas como transporte de FFI, não como implementação de runtime, quando #27 migrar). A justificativa: o modelo Rust/Go/Zig que o próprio ruling citou (no ruleset anterior) sai de libc; nós fazemos o mesmo — raw `clone` no Linux (SYS_clone intrinseco + context-switch), `CreateThread`/libSystem no Windows/macOS.

**R2 — `setjmp`/`longjmp`: backend intrinsic context-capture — RATIFIED (owner, 2026-08-17)**
**DECIDED:** **build a backend context-capture/restore INTRINSIC** (a real setjmp/longjmp equivalent lowered by codegen). The out-of-process test-harness workaround is **REJECTED**. 
`tk_test_run` captura panic + retorna via intrínseco do backend nativo (stack unwind + PC restore), não shim C. Fase 8 escreve o intrínseco; nenhum resíduo de setjmp sobrevive ao SWEEP.

**R3 — struct layout per-`#os` (e possível per-`#arch`): CONFIRMED — RATIFIED (owner, 2026-08-17)**
**DECIDED:** **CONFIRMED** — `stat`/`dirent`/`sockaddr` etc. get dedicated struct layouts **per `#os` (and possibly per `#arch`)** in the monolith mechanism. 
Cada struct-de-SO (Darwin `stat` ≠ Linux `stat`) é um `extern type` guarded por `#os`, materializado no monolith cc-emit via `#if`. Windows sem AF_UNIX → transporte alternativo (Named Pipes) reportado a §10, não inventado aqui. Fixture pinada em cada layout (S9 via stat real).

**R4 — per-target symbol binding: use pragmas + `.tkp` — RATIFIED (owner, 2026-08-17; REAFFIRMED D-TS1, owner 2026-08-19)**
**DECIDED:** use the **existing pragmas + the `.tkp`** (which already carry FFI-instrumentation helpers) — do **NOT invent a new declaration form**. 
O `TargetSymbol` como forma sintática é **WITHDRAWN** (D-TS1, 2026-08-19); em vez disso, cada `extern fn` que diverge por target (ex: FS Windows vs POSIX) redeclara-se sob `#os` guarding (precedente: `src/io/file_stream.tks`). Pragma-FFI existente + `prune_cc` resolve a ligação de símbolo nativo. Metade-fs (F4) sai com essa regra; metade-processo (F6) atrasa até struct-by-value-FFI + linker import-lib (Fase E).

**R5 — Backtrace/dladdr: BUILD in Teko — RATIFIED (owner, 2026-08-17)**
**DECIDED:** **BUILD it in Teko** — no degrade-to-no-op. 
`backtrace` e `dladdr` (#33/#34) ganham implementação própria em Teko (unwind `.eh_frame` / frame-pointers) quando Fase 8 rodar. Nenhuma degradação temporária a "sem símbolos"; o diagnose de crash é **obrigatório** no §16 (crash-optional era marcador anterior, agora revogado). Se o tamanho do emit inflar, refatora-se o backend; não degrada-se a função.

---

## §6 — O que permanece BLOQUEADO (honesto) e o que pode adiantar HOJE

**Bloqueado (deps não-nossas / rulings pendentes):**
- **F6 (process) + `win32_compat.h`-processo:** struct-by-value-FFI reverse + linker import-lib Win32
  (Fase E). Sem isso, o bloco Win32 não vira Teko.
- **F7 (threads/channels):** ruling §17+ (clone/CreateThread); canais dependem de threads; Windows sem
  AF_UNIX precisa de transporte alternativo (REPORTAR a §10).
- **Per-target symbol selection (RESOLVED D-TS1, 2026-08-19):** metade-fs (F4) agora desbloqueada via
  target-guarded `extern fn` (não há nova forma sintática `TargetSymbol`; usa `#os` guarding + `prune_cc`).
- **3 rulings do owner:** (R1) shared-lib-nativa-pthread conta como sweep-limpo? (R2) intrínseco de
  captura vs harness out-of-process p/ setjmp? (R5) degradar backtrace/dladdr é aceitável?

**Adiantável HOJE (design/scaffolding que compila):**
- **F1** (math/string/type intrínsecos): puro codegen, sem SO — o padrão de f64_bits (fundacao C5) já
  existe; `floor`/`round`/`ceil`/`memcpy`/`typedefs` são a mesma forma. Pode desenhar+implementar já.
- **F4-leaves puros:** `localtime_r`→Teko (algoritmo civil), env→`environ`-Teko, `getrusage` — puros.
- Os `extern type` de SO (`Stat`, `Pollfd`, `Rusage`, `linux_dirent64`, `Sockaddr_un`) como skeletons
  `#os`-guarded que compilam hoje (C1 aterrado).
- As **fixtures de regressão** de cada linha S (abaixo, §7), todas expressáveis contra o contrato
  declarado dos intrínsecos já aterrados.

---

## §7 — Fixtures de regressão (inputs → exit-code native esperado)

Padrão (o dos crumbs de syscall já aterrados): compilar `--no-verify --release`, `TEKO_BACKEND=c`,
`ulimit -v 6291456`, rodar o binário, ler `$?`. NUNCA `teko test .`. Cada uma sob
`examples/regressions/` com `main.tks` + `.tkp` (`kind="binary"`) + `.tkr` + mirror local de `src/sys/`.

| # | fixture | corpo | exit |
|---|---|---|---|
| S1 | `sys_exit_group` | `syscall1(SYS_EXIT_GROUP, 42)` (JÁ EXISTE, keystone) | `42` |
| S2 | `sys_write_stdout` | `syscall3(SYS_WRITE, 1, ptr_word(as_cstr("hi\n")), 3)` (JÁ EXISTE) | `0` (stdout `hi`) |
| S3 | `sys_mmap` | mmap 1 página · guardar sentinela via `store_u64` · ler via `load_u64` · munmap (JÁ especificado arena-mmap §7.1) | `0` |
| S4 | `math_floor_intrinsic` | `floor(3.9) == 3.0 && floor(0.0-2.1) == (0.0-3.0)` → exit 0/1 | `0` |
| S5 | `memcpy_intrinsic` | copiar 16 bytes A→B via o intrínseco, comparar iguais | `0` |
| S6 | `no_stdint_header` | grep no `teko.c` emitido: NÃO contém `#include <stdint.h>`; contém `typedef … uint8_t` | `0` (compile+grep) |
| S7 | `sys_openat_read` | criar `/tmp/tk_fx` (openat+write), reabrir, ler, comparar bytes → 0/1 | `0` |
| S8 | `sys_getdents` | listar `/` via getdents64, assertar que contém `usr` → 0/1 | `0` |
| S9 | `sys_stat_size` | stat um arquivo de 42 bytes, assertar `st_size==42` (extern struct `Stat` per-`#os`) | `0` |
| S10 | `sys_clock_monotonic` | `monotonic_ns()` duas vezes, `b >= a` (fundacao F4, JÁ) | `0` |
| S11 | `localtime_civil` | `civil_from_days(0) == (1970,1,1)` e um dia bissexto conhecido → 0/1 | `0` |
| S12 | `sys_getrandom_fills` | 32 bytes, não-todos-zero (fundacao F7, JÁ) | `0` |
| S13 | `sys_getrusage_rss` | `peak_rss() > 0` após alocar 8 MB → 0/1 | `0` |
| S14 | `env_environ_teko` | `set("TK","42")`; `get("TK")` parse → i32 (fundacao F6, JÁ) | `42` |
| S15 | `assert_migrated` | `assert__eq_i64(21+21, 42)` retorna sem panic; um `!=` panica (via probe) | `0` |
| S16 | `sweep_lone_c` | build `cc bootstrap/teko.c -lm` (sem os 4 arquivos) → hello-world exit 0 + tc2==tc3 | `0` (o gate terminal) |

S3-S16 exercitam cada linha S/I/X; S6/S16 são os provadores do SWEEP (o C emitido não depende mais de
header C). S9/S11 são os NOVOS de maior risco (layout per-SO / algoritmo de calendário).

---

## §8 — Ritual points (onde o gate COMPLETO tem de passar)

1. **Após F1** (math/string/type intrínsecos) — compiler-touching (`codegen.tks` preâmbulo) →
   fixpoint tri-gen + reseed. Prova: S4/S5/S6 + o C emitido perde `<math.h>`/`<string.h>`/`<stdint.h>`.
2. **Após F3** (arena) — o reseed load-bearing (arena-mmap §5 escada L2). Fixpoint `tc1==tc2==tc3` +
   MEM_PARANOID exit 0 + árvore completa. O RITUAL mais perigoso (uma arena sutilmente errada corrompe
   todo emit — arena-em-teko §6).
3. **Após F4** (fs/env/tempo) — mata a metade-fs do `win32_compat.h`; S7-S14. Per-target: POSIX verde,
   Win32 compila (via target-guarded `extern fn`, D-TS1 ratified 2026-08-19).
4. **Após CADA crumb de DELEÇÃO de símbolo C** (a regra das duas-pernas): a perna C (`cc
   bootstrap/teko.c`) E a perna native (emit linka com o `teko_rt.c` encolhido) ambas buildam.
5. **F9 — o SWEEP — RITUAL TERMINAL do §16.** Parar de emitir os `#include` (reseed), deletar os 4,
   ajustar ~20 scripts de CI (`mudancas-superficie` §11.2 checklist), provar `cc bootstrap/teko.c -lm`
   compila + `TEKO_BACKEND=c gen .` re-emite byte-idêntico (tc2==tc3) + MEM_PARANOID + provenance. S16.

---

## §9 — Índice de seções

- §0 Ground truth (as duas fontes de C: o emit + o runtime à-mão; o modelo per-SO avalizado)
- §1 A TABELA-MESTRE (37 linhas de dep: header | símbolos reais | classe | Linux/mac/win | doc)
- §2 Reconciliação com o desenho §16 (o coberto: syscall/arena/fundação/monólito/roadmap) × o NOVO
- §3 Sequência de retirada (Fase 1 math→ … →F9 SWEEP; a folha primeiro, o sweep terminal)
- §4 Grafo de dependências (arena é o gargalo; F6/F7 têm deps não-nossas)
- §5 Os 5 maiores riscos + questões de ruling (threads/sweep, setjmp, layout-per-SO, Fase-E, degradar-crash)
- §6 Bloqueado × adiantável-hoje
- §7 Fixtures de regressão (S1-S16, inputs → exit-code)
- §8 Ritual points (F1, F3-arena, F4, deleções, F9-SWEEP)
