# plano-s16 — runtime §16 cross-plataforma REAL (sync + arena + thread-stack)

Status: DESIGN (arquiteto). Não implementa produto. Autor: sessão arquiteto 2026-08-17.
Ruling do dono (2026-08-17): "se existe em C, existe em Teko"; tudo que fazemos à mão
TEM QUE FUNCIONAR (não só compilar) em macOS-arm64, windows-x86_64, linux-x86_64/arm64
(glibc+musl), SEM STUBS, SEM ATALHOS. C hand-written (teko_rt.c/.h) CONGELADO (só deleção).

Este doc cobre os TRÊS subsistemas §16-runtime que hoje só são reais no Linux, na ordem de
prioridade: (1) **sync** (destrava o seed / CI), (2) **arena** (fonte de memória off-Linux),
(3) **thread-stack** (reusa a fonte de memória da arena). O split é o MESMO padrão em todos:
ramo linux = syscall atual INALTERADO, ramo macos = FFI libSystem, ramo windows = FFI kernel32.

---

## 0. Diagnóstico confirmado (lido, não presumido)

- `src/runtime/sync.tks`: `futex_wait`/`futex_wake` chamam `teko::sys::syscall6` (builtin, CROSS)
  com `teko::sys::SYS_FUTEX` / `FUTEX_WAIT|WAKE|PRIVATE_FLAG` — consts `#os("linux")` em
  `src/sys/sys.tks`. Off-Linux essas consts são PODADAS pelo §17 antes do checker, então
  `teko::sys::SYS_FUTEX` vira "unknown type: sys" (sync.tks:20,34) → o degrau não builda o tip
  nessas pernas → ladder obsoleta → FATAL. É ISTO que deixa o CI vermelho em toda perna != linux-x86_64.
- `mtx_lock`/`mtx_unlock`/`cv_wait`/`cv_signal`/`cv_broadcast` ficam POR CIMA, só chamam
  `futex_wait`/`futex_wake` + os builtins `atomic_*` (CROSS, resolvidos por nome no checker
  `src/checker/scope.tks:1205-1208` → `tk_atomic_*`). **Não mudam.**
- **Sem chamadores externos**: `rg` confirma que `futex_*`, `FUTEX_*`, `mtx_*`, `cv_*` NÃO têm
  chamador fora de `sync.tks`/`sys.tks`. A mudança de sync é auto-contida (sync.tks + sys.tks + teko.tkp).
- `src/runtime/arena.tks`: `ar_mmap`/`ar_munmap`/`ar_oom` já são split `#if(os=="linux") … #else … #endif`,
  com o ramo `#else` sendo STUB (retorna 0). O restante da arena é cross (só `teko::mem::load/store_u64`).
- `src/runtime/thread.tks`: `thr_mmap`/`thr_guard`/`thread_stack_free` idem (`#else` STUB).
  `thread_stack_new` fica por cima, inalterado.

### Infra FFI existente — SUFICIENTE (sem crumb-0 de compilador)

Verificado em `src/parser/parse_decl.tks`, `src/codegen/codegen.tks`, `src/codegen/ffi_export.tks`,
`src/build/manifest.tks`:

1. **Declaração**: `extern fn nome(p: T, …): R = "simbolo" from "lib"` já é parseada
   (`parse_function`, campos `is_extern`/`c_symbol`/`from_lib`/`os_guard`). Aceita `#os("…")`
   como atributo (o `os_guard` é threadado; sys.tks já usa `#os`/`#arch` em `const`).
2. **Emissão**: um extern NÃO-`teko_rt` recebe protótipo C `extern <R> <simbolo>(<params>);`
   (`emit_function_sig`, codegen.tks:11098-11105 usa `f.c_symbol` VERBATIM, sem mangle) e a
   CHAMADA baixa para o símbolo cru (`codegen.tks:5454-5458 ext_symbol = cf.c_symbol`). Ou seja:
   basta declarar o extern e o teko.c fica com o protótipo + a call do símbolo do SO.
3. **Link**: o `from "lib"` é INFORMATIVO (io.tks:13-14) — o link real vem de
   `teko.tkp [extern.libs.<os>]`, resolvido por `mf_extern_spec` a `-l<nome>` e selecionado
   por-target por `os_lib_key_matches`. `kernel32 = []` já existe → `-lkernel32`.
4. **Prune**: `prune_cc` (prune.tks:99-101) descarta QUALQUER item top-level (fn/extern/const)
   cujo `guard` falhe, ANTES do checker. Logo `#os("macos")`/`#os("windows")` somem no build Linux.

**Conclusão:** a infra FFI-da-ABI já linka libSystem (macOS) e kernel32/synchronization (Windows).
Nenhum crumb-0 de compilador é necessário. O único ajuste de manifest é `[extern.libs.windows]`
(+`synchronization`); `[extern.libs.macos]` fica VAZIO (libSystem é linkada implicitamente por
`clang` via `-lSystem` default — `os_sync_wait_on_address`/`os_sync_wake_by_address_*`, `mmap`, `munmap`, `mprotect` resolvem dela).

### Toolchain do CI (decide a lib de link)

`.github/workflows/pr.yml:406-410,534-537` documenta que no runner Windows `cc` resolve para
`/c/mingw64/bin/cc` (triple `x86_64-w64-mingw32`) — **MinGW**, não MSVC. Logo a import-lib de
`WaitOnAddress`/`WakeByAddress*` é `libsynchronization.a` → `-lsynchronization`. macOS usa o
`clang` próprio do runner (libSystem default).

---

## 1. DECISÃO de API real por SO (com prós/contras)

### 1.1 sync — wait/wake por endereço

| SO | Escolha | Símbolo(s) | Lib |
|----|---------|-----------|-----|
| linux | **INALTERADO** (syscall futex) | `SYS_FUTEX`+`FUTEX_WAIT/WAKE\|PRIVATE` | (syscall) |
| macos-arm64 | **`os_sync_wait_on_address`/`os_sync_wake_by_address_any`/`_all`** | idem | libSystem (implícita) |
| windows-x86_64 | **`WaitOnAddress`/`WakeByAddressSingle`/`WakeByAddressAll`** | idem | synchronization |

**macOS: `os_sync_wait_on_address` — API PÚBLICA (ruling do dono: mirar na versão mais recente).**
Justificativa:

- **API pública e suportada (decisor):** `os_sync_wait_on_address`/`os_sync_wake_by_address_any`/
  `_all` são a interface OFICIAL e DOCUMENTADA da Apple para wait/wake por endereço
  (`<os/os_sync_wait_on_address.h>`, libSystem). Sob a lei "sem atalhos", uma ABI pública estável
  é a implementação REAL; `__ulock_*` é símbolo PRIVADO não-documentado (não é o alvo).
- **Piso de versão:** exige **macOS 14.4+** (mar/2024). Ruling do dono — mirar na versão mais
  recente do SO; não suportamos Darwin antigo. Todo alvo macos-arm64 relevante é ≥ 14.4.
- **Encaixe semântico exato:** `os_sync_wait_on_address(addr, value, size, flags)` bloqueia
  enquanto os `size` bytes em `addr` == `value` — com `size=4` é bit-a-bit a semântica do nosso
  futex de 32 bits. `value` é passado por VALOR → o ramo macos NÃO precisa de scratch (diferente
  do Windows). `os_sync_wake_by_address_any`/`_all` acordam um / todos.
- **Sem consts de operação:** `flags` é `OS_SYNC_WAIT_ON_ADDRESS_NONE`/
  `OS_SYNC_WAKE_BY_ADDRESS_NONE` = `0` — dispensa a família `UL_*`/`ULF_*` do `__ulock` privado.
  Mais simples e sem transcrever ABI não-documentada.

(`__ulock_wait`/`__ulock_wake` ficam apenas REGISTRADOS como o mecanismo de compat pré-14.4, NÃO
adotados — a decisão é a API pública.)

**Windows: `WaitOnAddress`/`WakeByAddress*` (Win8+).** É a API de futex do Win32, cobre todo
Windows alvo. Alternativa `NtWaitForKeyedEvent` (ntdll) é mais baixa e exige handle de keyed-event
— sem ganho. `WaitOnAddress` precisa de `CompareAddress` (ponteiro para o valor esperado): usamos
`teko::mem::buf_ptr(8)` (bump de 8 B na região da arena corrente, no caminho LENTO/contendido) como
scratch e `store_u64` o esperado nele. Sem re-entrância: `buf_ptr` só toca a arena por-tarefa, que
não usa o mutex que estamos esperando.

### 1.2 arena — fonte de memória

| SO | `ar_mmap` | `ar_munmap` | `ar_oom` |
|----|-----------|-------------|----------|
| linux | **INALTERADO** (`SYS_MMAP`) | **INALTERADO** (`SYS_MUNMAP`) | **INALTERADO** (`SYS_EXIT_GROUP`) |
| macos | **`mmap` FFI** (libSystem) | **`munmap` FFI** | `exit` FFI (libSystem `_exit`) |
| windows | **`VirtualAlloc`** (kernel32) | **`VirtualFree`** (kernel32) | `ExitProcess` (kernel32) |

**macOS: `mmap`/`munmap` via FFI-da-ABI, NÃO raw syscall.** Justificativa (ruling do dono):
Darwin NÃO garante números de syscall estáveis (BSD-class tag + convenção de carry); a interface
SUPORTADA é o C ABI de libSystem. `mmap` mantém a semântica idêntica ao Linux (mesmos `PROT_*`,
páginas, guard via `mprotect`), minimizando divergência — só mudam os valores de flag (Darwin
`MAP_ANON=0x1000`, Linux `0x20`) e a convenção de erro (`MAP_FAILED=(void*)-1`, não a banda
`[-4095,-1]`). Preferido sobre `mach_vm_allocate`/`mach_vm_deallocate`: estas retornam
`kern_return_t`, exigem `mach_task_self()` e não dão guard-page tão direto — mais superfície, zero
benefício para mapeamento anônimo.

**Windows: `VirtualAlloc`/`VirtualFree`/`VirtualProtect` (kernel32).** API nativa de memória
virtual, zero libc, kernel32 já linkada. `VirtualAlloc(NULL, size, MEM_COMMIT|MEM_RESERVE,
PAGE_READWRITE)` devolve base zero-preenchida (como `MAP_ANON`) ou NULL. Divergência importante:
`VirtualFree(base, 0, MEM_RELEASE)` — size DEVE ser 0 e o addr DEVE ser a base de `VirtualAlloc`.
Compatível com o nosso uso: a arena sempre libera chunks INTEIROS na sua base (`ar_chunk_free`
passa a base), então o comprimento é ignorado no ramo windows.

### 1.3 thread-stack — reusa 1.2

`thr_mmap` = mesma fonte de 1.2. `thr_guard` = `mprotect(low, GUARD, PROT_NONE)` (macOS) /
`VirtualProtect(low, GUARD, PAGE_NOACCESS, &old)` (windows — `&old` é scratch `buf_ptr(8)`).
`thread_stack_free` = `munmap(low, STACK)` (macOS) / `VirtualFree(low, 0, MEM_RELEASE)` (windows).

> **ESCOPO — honestidade (M.3):** este design torna reais a FONTE DE MEMÓRIA do thread (o trio
> `thr_mmap`/`thr_guard`/`thread_stack_free`) off-Linux. O SPAWN/JOIN de thread (`thread_clone`
> builtin sobre `SYS_CLONE`, o trampolim, e o join sobre `CLONE_CHILD_CLEARTID`) permanece Linux-only
> e é um design DISTINTO e MAIOR (macOS: `pthread_create`/`pthread_join` de libSystem;
> Windows: `CreateThread`/`WaitForSingleObject` de kernel32) — NÃO trava o seed e NÃO está nesta
> sequência de crumbs. Fica REPORTADO como o próximo alvo §10-native off-Linux, real, sem stub.

---

## 2. Constantes ABI a declarar (em `src/sys/sys.tks`, disciplina transcrita, `#os`-guardadas)

Todos são `const` Teko literais, transcritos da ABI do SO, `#os`-guardados (o §17 poda os
não-alvo antes do checker). Doc-comment completo W15 por const (padrão dos vizinhos linux).

**macOS (`#os("macos")`):**
- `OS_SYNC_WAIT_ON_ADDRESS_NONE: u32 = 0` — flags default de `os_sync_wait_on_address`.
- `OS_SYNC_WAKE_BY_ADDRESS_NONE: u32 = 0` — flags default de `os_sync_wake_by_address_*`.
  (Ambas = 0; podem ser passadas como literal `0` no call-site — as consts existem por clareza W15.)
- `PROT_NONE/READ/WRITE: i32 = 0/1/2` — iguais aos do Linux, mas bloco `#os("macos")` próprio.
- `MAP_PRIVATE: i32 = 0x0002`, `MAP_ANON: i32 = 0x1000` — Darwin `<sys/mman.h>`.
- `MAP_FAILED_WORD: u64 = 18446744073709551615` — `(void*)-1`, a sentinela de erro do `mmap`.

**Windows (`#os("windows")`):**
- `MEM_COMMIT: u32 = 0x1000`, `MEM_RESERVE: u32 = 0x2000`, `MEM_RELEASE: u32 = 0x8000`.
- `PAGE_READWRITE: u32 = 0x04`, `PAGE_NOACCESS: u32 = 0x01`.
- `WIN_INFINITE: u32 = 0xFFFFFFFF` — `dwMilliseconds` de `WaitOnAddress` (bloqueio infinito).

(As consts linux existentes ficam INTOCADAS.)

---

## 3. Declarações `extern` FFI (assinatura exata → símbolo → lib)

Colocadas em `src/runtime/sync.tks` (as de sync) e `src/runtime/arena.tks`/`thread.tks` (as de
memória), cada uma `#os`-guardada, doc-comment W15 completo. Assinaturas C reais e o mapeamento:

### sync (sync.tks)
```
/**
 * os_sync_wait_on_address — libSystem `int os_sync_wait_on_address(void *addr, uint64_t value,
 * size_t size, os_sync_wait_on_address_flags_t flags)`: bloqueia enquanto os `size` bytes em `addr`
 * == `value`. Com `size=4` é a semântica do nosso futex de 32 bits. `addr`/`value`/`size` passam em
 * registrador inteiro (ABI arm64), por isso `u64`; `flags` é enum de 32 bits → `u32`.
 *
 * @param addr   endereço da palavra futex de 32 bits
 * @param value  valor esperado (comparado `size` bytes)
 * @param size   bytes a comparar (4)
 * @param flags  OS_SYNC_WAIT_ON_ADDRESS_NONE (0)
 * @return       nº de waiters restantes / -1 em erro — descartado
 */
#os("macos")
extern fn os_sync_wait_on_address(addr: u64, value: u64, size: u64, flags: u32): i32 = "os_sync_wait_on_address" from "System"

/**
 * os_sync_wake_by_address_any — libSystem `int os_sync_wake_by_address_any(void *addr, size_t size,
 * os_sync_wake_by_address_flags_t flags)`: acorda UM waiter bloqueado na palavra em `addr`.
 *
 * @param addr   endereço da palavra futex de 32 bits
 * @param size   bytes (4)
 * @param flags  OS_SYNC_WAKE_BY_ADDRESS_NONE (0)
 * @return       0 em sucesso / -1 em erro — descartado
 */
#os("macos")
extern fn os_sync_wake_by_address_any(addr: u64, size: u64, flags: u32): i32 = "os_sync_wake_by_address_any" from "System"

/**
 * os_sync_wake_by_address_all — libSystem `int os_sync_wake_by_address_all(void *addr, size_t size,
 * os_sync_wake_by_address_flags_t flags)`: acorda TODOS os waiters na palavra em `addr`.
 *
 * @param addr   endereço da palavra futex de 32 bits
 * @param size   bytes (4)
 * @param flags  OS_SYNC_WAKE_BY_ADDRESS_NONE (0)
 * @return       0 em sucesso / -1 em erro — descartado
 */
#os("macos")
extern fn os_sync_wake_by_address_all(addr: u64, size: u64, flags: u32): i32 = "os_sync_wake_by_address_all" from "System"

/**
 * WaitOnAddress — kernel32 `BOOL WaitOnAddress(volatile VOID *Address, PVOID CompareAddress,
 * SIZE_T AddressSize, DWORD dwMilliseconds)`: bloqueia enquanto os `size` bytes em `addr` forem
 * iguais aos em `compare`. Ponteiros passam em registrador inteiro (x86_64), por isso `u64`.
 *
 * @param addr     endereço da palavra futex
 * @param compare  endereço de um valor com o esperado (scratch)
 * @param size     bytes a comparar (4)
 * @param ms       timeout em ms (WIN_INFINITE = infinito)
 * @return         não-zero em wake, 0 em timeout/erro — descartado
 */
#os("windows")
extern fn WaitOnAddress(addr: u64, compare: u64, size: u64, ms: u32): i32 = "WaitOnAddress" from "synchronization"

/**
 * WakeByAddressSingle — kernel32 `void WakeByAddressSingle(PVOID Address)`: acorda UM waiter.
 * @param addr  endereço da palavra futex
 */
#os("windows")
extern fn WakeByAddressSingle(addr: u64) = "WakeByAddressSingle" from "synchronization"

/**
 * WakeByAddressAll — kernel32 `void WakeByAddressAll(PVOID Address)`: acorda TODOS os waiters.
 * @param addr  endereço da palavra futex
 */
#os("windows")
extern fn WakeByAddressAll(addr: u64) = "WakeByAddressAll" from "synchronization"
```

### arena/thread (mmap family — declarada UMA vez em arena.tks; thread.tks reusa)
```
/**
 * os_mmap — libSystem `void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)`.
 * @return endereço-base alinhado à página, ou MAP_FAILED (0xFFFF…FFFF) em falha
 */
#os("macos")
extern fn os_mmap(addr: u64, len: u64, prot: i32, flags: i32, fd: i32, off: i64): u64 = "mmap" from "System"

/** os_munmap — libSystem `int munmap(void *addr, size_t len)`. @return 0 em sucesso */
#os("macos")
extern fn os_munmap(addr: u64, len: u64): i32 = "munmap" from "System"

/** os_mprotect — libSystem `int mprotect(void *addr, size_t len, int prot)`. @return 0 em sucesso */
#os("macos")
extern fn os_mprotect(addr: u64, len: u64, prot: i32): i32 = "mprotect" from "System"

/** os_exit — libSystem `void _exit(int status)` — encerra o processo sem flush (OOM cru). */
#os("macos")
extern fn os_exit(status: i32) = "_exit" from "System"

/**
 * VirtualAlloc — kernel32 `LPVOID VirtualAlloc(LPVOID addr, SIZE_T size, DWORD type, DWORD prot)`.
 * @return base zero-preenchida, ou 0 (NULL) em falha
 */
#os("windows")
extern fn VirtualAlloc(addr: u64, size: u64, alloc_type: u32, protect: u32): u64 = "VirtualAlloc" from "kernel32"

/** VirtualFree — kernel32 `BOOL VirtualFree(LPVOID addr, SIZE_T size, DWORD freeType)`. */
#os("windows")
extern fn VirtualFree(addr: u64, size: u64, free_type: u32): i32 = "VirtualFree" from "kernel32"

/** VirtualProtect — kernel32 `BOOL VirtualProtect(LPVOID addr, SIZE_T size, DWORD newProt, PDWORD oldProt)`. */
#os("windows")
extern fn VirtualProtect(addr: u64, size: u64, new_prot: u32, old_prot: u64): i32 = "VirtualProtect" from "kernel32"

/** ExitProcess — kernel32 `void ExitProcess(UINT code)` — encerra o processo (OOM cru). */
#os("windows")
extern fn ExitProcess(code: u32) = "ExitProcess" from "kernel32"
```

ABI check (x86_64/arm64): todo ponteiro (void*/PVOID/LPVOID) e size_t/SIZE_T ocupam 1 registrador
inteiro = `u64`; `int`/`DWORD`/`UINT`=`i32`/`u32`; `off_t`=`i64`; `BOOL`/retorno-int=`i32`;
`void*` de retorno=`u64`. Protótipos são AUTÔNOMOS (nenhum header do SO é incluído no teko.c), então
não há conflito com `<sys/mman.h>`/`<windows.h>`.

---

## 4. Split `#os` por função (corpo linux VERBATIM → teko.c idêntico)

Padrão sys.tks: cada função declarada uma vez por `#os` (o prune mantém exatamente uma). O corpo
`#os("linux")` é **byte-idêntico** ao atual (mesma assinatura, mesmo lowering) → o teko.c do build
Linux não muda.

### sync.tks — futex_wait / futex_wake
```
#os("linux")
fn futex_wait(addr: u64, expected: i64) {           // CORPO ATUAL, INALTERADO
    _ = teko::sys::syscall6(teko::sys::SYS_FUTEX, addr to i64,
        teko::sys::FUTEX_WAIT | teko::sys::FUTEX_PRIVATE_FLAG, expected, 0, 0, 0)
}
#os("macos")
fn futex_wait(addr: u64, expected: i64) {
    _ = os_sync_wait_on_address(addr, (expected to u64) & 0xffffffff, 4, OS_SYNC_WAIT_ON_ADDRESS_NONE)
}
#os("windows")
fn futex_wait(addr: u64, expected: i64) {
    var scratch = teko::sys::ptr_word(teko::mem::buf_ptr(8)) to u64
    teko::mem::store_u64(scratch, expected to u64)   // baixo-4 (LE) = esperado
    _ = WaitOnAddress(addr, scratch, 4, WIN_INFINITE)
}

#os("linux")
fn futex_wake(addr: u64, n: i64) {                   // CORPO ATUAL, INALTERADO
    _ = teko::sys::syscall6(teko::sys::SYS_FUTEX, addr to i64,
        teko::sys::FUTEX_WAKE | teko::sys::FUTEX_PRIVATE_FLAG, n, 0, 0, 0)
}
#os("macos")
fn futex_wake(addr: u64, n: i64) {
    if n == 1 { _ = os_sync_wake_by_address_any(addr, 4, OS_SYNC_WAKE_BY_ADDRESS_NONE) }
    else { _ = os_sync_wake_by_address_all(addr, 4, OS_SYNC_WAKE_BY_ADDRESS_NONE) }
}
#os("windows")
fn futex_wake(addr: u64, n: i64) {
    if n == 1 { WakeByAddressSingle(addr) } else { WakeByAddressAll(addr) }
}
```
`futex_wake` é chamado APENAS com `n==1` (mtx_unlock/cv_signal) ou `n==INT_MAX` (cv_broadcast) —
o split `n==1 ? single : all` é exato. `mtx_*`/`cv_*` NÃO mudam (cada doc-comment W15 ganha uma
frase notando que `futex_*` é `#os`-split por baixo). Cada corpo macos/windows leva doc-comment W15.

### arena.tks — ar_mmap / ar_munmap / ar_oom (substitui o `#else` STUB por ramos reais)
```
#os("macos")
fn ar_mmap(length: u64): u64 {
    var r = os_mmap(0, length, teko::sys::PROT_READ | teko::sys::PROT_WRITE,
        teko::sys::MAP_PRIVATE | teko::sys::MAP_ANON, -1, 0)
    if r == teko::sys::MAP_FAILED_WORD { return 0 }
    r
}
#os("macos")
fn ar_munmap(address: u64, length: u64) { _ = os_munmap(address, length) }
#os("macos")
fn ar_oom() { os_exit(AR_OOM_CODE to i32) }

#os("windows")
fn ar_mmap(length: u64): u64 {
    VirtualAlloc(0, length, teko::sys::MEM_COMMIT | teko::sys::MEM_RESERVE, teko::sys::PAGE_READWRITE)
}   // NULL(0) em falha já é o contrato "0 = recusa"
#os("windows")
fn ar_munmap(address: u64, length: u64) { _ = length; _ = VirtualFree(address, 0, teko::sys::MEM_RELEASE) }
#os("windows")
fn ar_oom() { ExitProcess(AR_OOM_CODE to u32) }
```
O ramo `#os("linux")` = corpo atual dos três (hoje sob `#if(os=="linux")`), reescrito como bloco
`#os("linux")` (mesmo corpo). Remove-se o `#if/#else/#endif` e o STUB.

### thread.tks — thr_mmap / thr_guard / thread_stack_free (idem)
```
#os("macos")
fn thr_mmap(length: u64): u64 {
    var r = os_mmap(0, length, teko::sys::PROT_READ | teko::sys::PROT_WRITE,
        teko::sys::MAP_PRIVATE | teko::sys::MAP_ANON, -1, 0)
    if r == teko::sys::MAP_FAILED_WORD { return 0 }
    r
}
#os("macos")
fn thr_guard(low: u64) { _ = os_mprotect(low, GUARD_BYTES, teko::sys::PROT_NONE) }
#os("macos")
pub fn thread_stack_free(stack_top: u64) { _ = os_munmap(stack_top - STACK_BYTES, STACK_BYTES) }

#os("windows")
fn thr_mmap(length: u64): u64 {
    VirtualAlloc(0, length, teko::sys::MEM_COMMIT | teko::sys::MEM_RESERVE, teko::sys::PAGE_READWRITE)
}
#os("windows")
fn thr_guard(low: u64) {
    var old = teko::sys::ptr_word(teko::mem::buf_ptr(8)) to u64
    _ = VirtualProtect(low, GUARD_BYTES, teko::sys::PAGE_NOACCESS, old)
}
#os("windows")
pub fn thread_stack_free(stack_top: u64) { _ = VirtualFree(stack_top - STACK_BYTES, 0, teko::sys::MEM_RELEASE) }
```
`os_mmap`/`os_munmap`/`os_mprotect`/`VirtualAlloc`/`VirtualFree`/`VirtualProtect` são declarados em
arena.tks; thread.tks os RE-declara `#os`-guardados (ou o design pode centralizá-los num módulo
`src/runtime/osmem.tks` — ver §7 "alternativa"). `thread_stack_new` fica inalterado.

> **Nota `#arch`:** macos-arm64 é o único macOS alvo e windows-x86_64 o único Windows alvo, então os
> ramos macos/windows não precisam de `#arch`. O ramo linux referencia consts `teko::sys::SYS_*`
> que JÁ são `#arch`-split (x86_64/arm64) — resolvidas pós-prune, sem mudança aqui.

---

## 5. Ajustes de link no `teko.tkp`

```
[extern.libs.macos]
# libSystem é linkada implicitamente pelo clang (-lSystem default): os_sync_wait_on_address/
# os_sync_wake_by_address_*, mmap/munmap/mprotect/_exit resolvem dela. NENHUMA entrada necessária.  (mantém vazio)

[extern.libs.windows]
kernel32 = []          # já existe: FindFirstFileA…, + VirtualAlloc/VirtualFree/VirtualProtect/ExitProcess
synchronization = []   # NOVO: WaitOnAddress/WakeByAddressSingle/WakeByAddressAll (MinGW libsynchronization.a → -lsynchronization)
```
`[extern.libs.linux]` fica vazio (syscalls, sem lib). `mf_extern_spec("synchronization")` → `-lsynchronization`.

---

## 6. RITUAL / reseed — análise (CONFIRMADA)

- **Linux (x86_64 e arm64, glibc+musl):** os blocos `#os("macos")`/`#os("windows")` e os `extern`
  novos são PODADOS por `prune_cc` ANTES do checker (prune.tks:99-101). Os corpos `#os("linux")` de
  `futex_*`/`ar_*`/`thr_*` são byte-idênticos aos atuais. Logo o **teko.c emitido pelo build host
  Linux é byte-idêntico a `bootstrap/teko.c`** → **SEM reseed**. O gate é exatamente esse: build
  host emite teko.c idêntico ao seed. (Módulos-folha de runtime, não mudam o C emitido no alvo do seed.)
- **macOS/Windows:** o teko.c desses alvos MUDA (ganha os protótipos+calls FFI), mas o seed
  (`bootstrap/teko.c`) é o binário Linux; esses alvos são compilados a partir do gen0 e não
  alimentam o seed. Não há reseed por conta deles.
- **Provenance/fixpoint:** como o teko.c do alvo-do-seed (Linux) não muda, `provenance_gate` e
  `fixpoint (tc2==tc3)` seguem PASS sem reseed. Se, por engano, algum corpo linux for tocado
  (mudar o C emitido), aí sim exigiria fixpoint gen2==gen3 + PROVENANCE + reseed — o design EVITA
  isso mantendo os corpos linux verbatim.

---

## 7. Sequência de crumbs (ordenada, cada um gate-ável; sync PRIORITÁRIO)

Cada crumb: compila `--no-verify --release`, `TEKO_BACKEND=c`, `ulimit -v 8388608`, fixpoint local,
e o **cross-check dos 3 OS** (§8). Ritual pleno (gate completo/CI) nos pontos marcados ⭑.

**Crumb 1 — consts ABI (sys.tks).** Adiciona os `#os("macos")`/`#os("windows")` consts do §2 (doc
W15). Não referenciado ainda → só type-check. Folha, sem reseed. Toca: `src/sys/sys.tks`.
Fixtures: nenhum runtime ainda; o gate é type-check nos 3 OS.

**Crumb 2 — sync FFI + split (sync.tks + teko.tkp) ⭑ DESTRAVA O SEED.**
Adiciona os `extern` de sync (§3) e reescreve `futex_wait`/`futex_wake` no split `#os` (§4), corpo
linux verbatim. `[extern.libs.windows] += synchronization`. `mtx_*`/`cv_*` inalterados (só doc).
Shapes: `fn futex_wait(addr: u64, expected: i64)`, `fn futex_wake(addr: u64, n: i64)` (assinaturas
INALTERADAS); externs do §3. Toca: `src/runtime/sync.tks`, `teko.tkp`.
Gate: os 3 OS type-checkam+emitem; Linux teko.c byte-idêntico (SEM reseed) ⭑. **É o crumb que fecha
o CI vermelho.** Fixture de regressão: §8 (o próprio cross-emit é o teste; o CI real exercita
mutex/condvar quando §10-native ligar).

**Crumb 3 — arena FFI de memória (arena.tks + osmem).** Substitui o `#else` STUB de
`ar_mmap`/`ar_munmap`/`ar_oom` pelos ramos reais `#os("macos")`/`#os("windows")` (§4); adiciona os
externs mmap-family (§3). Toca: `src/runtime/arena.tks` (+ opcional `src/runtime/osmem.tks`).
Gate: 3 OS type-check+emit; Linux idêntico (SEM reseed).

**Crumb 4 — thread-stack FFI (thread.tks).** Substitui o `#else` STUB de
`thr_mmap`/`thr_guard`/`thread_stack_free` pelos ramos reais (§4), reusando os externs do crumb 3.
Toca: `src/runtime/thread.tks`. Gate: idem.

**Crumb 5 ⭑ — ritual pleno / CI verde nas 5 pernas.** Roda a matriz completa (macos-arm64;
windows-x86_64; linux-x86_64 glibc+musl; linux-arm64 glibc+musl). Confirma link real
(`-lsynchronization` no Windows; libSystem implícita no macOS) e execução real dos smokes que
tocam a arena (todo processo usa a arena) nos 3 SO.

> **Alternativa de organização (recomendada):** centralizar os externs mmap-family +
> `VirtualProtect` num novo `src/runtime/osmem.tks` (folha) para arena E thread compartilharem uma
> única declaração, evitando duplicar os `extern` em dois arquivos. Fica a critério do implementador;
> não muda o teko.c do alvo-do-seed (Linux poda tudo).

**Ordem de dependência de seed (bootstrap):** o seed é o `teko` Linux anterior. Nenhum crumb usa
recurso de linguagem novo (só `extern`/`#os`/`buf_ptr`/`store_u64` — todos já no seed). Crumb 1→2
destrava; 3/4 são independentes entre si (podem paralelizar após 1); 5 é o selo.

---

## 8. Validação cross LOCAL (no host Linux) — método por crumb

Objetivo: provar que o checker/codegen passam LIMPOS em `.tks` para os 3 OS e emitem teko.c (o link
`cc` pode falhar no host Linux por headers/libs do alvo ausentes — ESPERADO; o gate é type-check+emit).

```
# 1) gen0 a partir do C seed
CC=cc sh scripts/build_gen1_from_c.sh bootstrap/teko.c src /tmp/g0

# 2) para cada alvo, forçar target na seção [extern] do teko.tkp e emitir
#    (aarch64-apple-darwin / x86_64-pc-windows-msvc / x86_64-unknown-linux-gnu)
#    editar teko.tkp:  [extern]\n target = "aarch64-apple-darwin"
ulimit -v 8388608
TK_RT_DIR=$PWD/src/runtime TEKO_BACKEND=c /tmp/g0/teko . -o /tmp/x/teko --no-verify --release
#    → DEVE type-checkar e emitir /tmp/x/teko.c limpo para os 3 alvos (repetir p/ cada target)
#    NUNCA `teko test` (OOM).

# 3) Linux byte-idêntico (o gate do seed): com target linux (ou sem target),
#    diff <(emite teko.c host) bootstrap/teko.c  →  IDÊNTICO (nenhuma linha muda)
```
Sinal de PASS por crumb:
- **Crumb 1:** os 3 targets type-checkam (consts novas visíveis só no alvo certo, podadas nos outros).
- **Crumb 2/3/4:** macOS emite teko.c com `os_sync_wait_on_address`/`mmap`/… ; Windows com
  `WaitOnAddress`/`VirtualAlloc`/… ; Linux emite teko.c **byte-idêntico** a `bootstrap/teko.c`.
- O `"unknown type: sys"` de sync.tks:20,34 DESAPARECE nos 3 alvos (era o sintoma do CI vermelho).

Fixtures de regressão (entrada → exit code nativo esperado) para quando §10-native ligar mutex/condvar
no CI real (não roda local por OOM): um `.tkr` com um cenário `scenario_begin("mtx_mutual_exclusion")`
que faz N threads incrementarem um contador sob `mtx_lock`/`mtx_unlock` e um `cv_wait`/`cv_broadcast`
de barreira, `eq_i64(contador, N*iters)` → exit 0. Vale para os 3 SO (mesma superfície, `futex_*`
split por baixo). Este fixture pertence ao crumb 5 (CI), não à validação local.

---

## 9. Riscos + tensões de lei (com resolução recomendada)

1. **Piso macOS 14.4+ (`os_sync_wait_on_address`).** Risco: a API pública exige macOS 14.4+.
   Resolução: ruling do dono — mirar na versão mais recente do SO; não suportamos Darwin antigo.
   É a API PÚBLICA e documentada (não o símbolo privado `__ulock`), a implementação REAL alinhada à
   lei "sem atalhos". Sem tensão de lei.
2. **`buf_ptr` no caminho de `futex_wait`/`thr_guard` (Windows).** Risco: alocação na região da arena
   dentro de uma primitiva de sync/thread. Resolução: só no caminho LENTO (contendido) e só toca a
   arena por-tarefa — que NÃO usa o mutex esperado → sem re-entrância/deadlock. Alternativa sem
   alocação (scratch por-thread) fica registrada, mas `buf_ptr` é suficiente e simples.
3. **`MAP_ANON` diverge (Darwin 0x1000 vs Linux 0x20).** Risco: valor errado silencioso. Resolução:
   const `#os("macos")` própria (§2), transcrita e doc-comentada com o valor Darwin — nunca compartilha
   com o bloco linux (disciplina sys.tks de "cada per-target é uma unidade autônoma").
4. **`VirtualFree` exige size=0 com MEM_RELEASE.** Risco: se algum caller passasse um sub-range, falharia.
   Resolução: a arena/thread só liberam chunks/stacks INTEIROS na base → compatível; o ramo windows
   ignora `length` explicitamente (`_ = length`).
5. **Lei "issues são 100%".** A issue imediata é sync (destrava o seed); o ruling ampliou para
   arena+thread-memória — TODOS entregues aqui (crumbs 2,3,4). O SPAWN/JOIN de thread off-Linux é
   REPORTADO (§1.3) como alvo distinto/maior, não convertido em issue nova por mim (adjacência reportada
   para cima). Sem tensão que exija HALT.
6. **Congelamento do C (§16).** Nenhum crumb toca `teko_rt.c/.h` — tudo é superfície `.tks` +
   FFI-da-ABI. `atomic_*`/`syscall*`/`buf_ptr`/`store_u64` já são builtins/emissões existentes; nenhum
   builtin novo de compilador é criado (sem reseed por essa via). Conforme a lei.

Nenhuma tensão de lei genuína permanece → SEM HALT. Design ratificável (passa Teko-only, W15,
§16-sem-atalho, forward-only, reseed-quando-muda-o-C).
