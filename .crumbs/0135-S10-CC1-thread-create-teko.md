---
seq: 0135
crumb-id: S10-CC1
milestone: F7
gate: "[fixpoint]"
reseed-class: expurgo
deps: []
sources:
  - "docs/design/plano-s10-concurrency-cluster-migration.md:1-90"
  - "DECISION_LOG.md:1016-1017"   # T2/T3 thread-create per-OS, R1/D101
  - "docs/design/plano-s10-multithread-nativo.md"
---

# 0135 · S10-CC1 — thread-create/join/detach em Teko (per-OS), retira pthread do spawn

> Move a criação de thread do `#spawn` do `pthread_create` (C) para primitivas Teko
> per-SO, empacotando o ctx na arena (sem `malloc`), e reusando o seam maintained-C
> `tk_task_begin`/`tk_task_end` via extern.

## Goal

O `#spawn` hoje emite `tk_thread_spawn(...)` (pthread, C em `teko_rt.c`) e empacota os
args num blob de `malloc`/`free`. Este crumb introduz `thread_create`/`thread_join`/
`thread_detach` em Teko (`src/runtime/thread.tks`) despachando por SO — Linux `clone`
(helper `tk_thread_clone` já emitido) + `thread_stack_new`; macOS `pthread_create/join
from "System"` (T2/D101, Apple proíbe clone cru); Windows `CreateThread`/
`WaitForSingleObject` (kernel32) — e reroteia `emit_spawn`/`emit_spawn_thunk_one`
(`codegen.tks`) para chamar a primitiva Teko, alocando o ctx na `region_program`. Os
corpos C `tk_thread_spawn`/`tk_thread_join`/`tk_thread_join_spawn`/`tk_thread_start`
ficam MORTOS até F9. **byte-mover** (muda a emissão do `#spawn`) → dirige o reseed.
FRIO no build seco (o compilador não faz spawn ao compilar) → pico flat/sub-percentual.

## Where

- `src/runtime/thread.tks` — NOVO: `thread_create`, `thread_join`, `thread_detach`
  (`exp`), cada um com 3 corpos `#os`. Reusa `thread_stack_new`/`thread_stack_free`
  (já lá) e o builtin `thread_clone` (Linux).
- `src/sys/abi/mac.tks` — NOVO `pub extern pthread_create`, `pthread_join`, `pthread_detach`
  (`from "System"`), casando a ABI real (`pthread_create(pthread_t*, const void*, void*(*)(void*), void*)`).
- `src/sys/abi/windows.tks` — NOVO `pub extern CreateThread` (`from "kernel32"`);
  `WaitForSingleObject` já existe (linha 41).
- `src/codegen/codegen.tks:7604` — `emit_spawn` — trocar `tk_thread_spawn(tk_spawn_tramp_{k}, _sc)`
  por chamada à `thread_create` Teko (nome mangled) passando trampoline + ctx.
- `src/codegen/codegen.tks:7559` — `emit_spawn_thunk_one` — trocar o `malloc(_need)` do
  ctx por `tk_region_alloc(tk_region_program(), _need)`; remover o `free(_raw)` da
  trampoline (linha 7579) — a arena recupera.
- **NÃO tocar** `teko_rt.c` (`tk_thread_*`, `tk_task_*`) — D90; ficam mortos até F9.

## How

1. **Primitiva Teko de criação** em `thread.tks`. A trampoline do child instala a
   disciplina de arena via o seam maintained-C (`tk_task_begin`/`tk_task_end`, extern):

```teko
/**
 * thread_create — start `entry(ctx)` on a new OS thread and return an opaque join
 * handle. The child installs its own arena discipline via the maintained-C task seam
 * and tears it down on return; `ctx` is arena-owned by the caller.
 *
 * @param entry  address of the trampoline `fn(ctx: u64)` the child runs
 * @param ctx    address of the arena-packed argument block
 * @return       join handle (thread id / OS handle word)
 * @since 0.3.1
 */
exp fn thread_create(entry: u64, ctx: u64): u64
```

```teko
/**
 * thread_join — block until the thread named by `handle` has finished.
 *
 * @param handle  join handle from thread_create
 * @since 0.3.1
 */
exp fn thread_join(handle: u64)
```

```teko
/**
 * thread_detach — release the join resources without waiting.
 *
 * @param handle  join handle from thread_create
 * @since 0.3.1
 */
exp fn thread_detach(handle: u64)
```

2. **Corpos `#os`.**
   - **Linux (`#os("linux")`):** `var top = teko::runtime::thread_stack_new()`;
     `tk_thread_clone(entry, top, ctx, ctid_addr, CLONE_VM|CLONE_FS|CLONE_FILES|
     CLONE_THREAD|CLONE_SIGHAND|CLONE_SETTLS|CLONE_CHILD_CLEARTID…)`; a trampoline no
     child chama `tk_task_begin` (extern), `entry(ctx)`, `tk_task_end`,
     `thread_stack_free(top)`; `thread_join` faz `futex_wait` no ctid word (o kernel
     zera com `CHILD_CLEARTID`). Handle = ctid addr / stack top. aarch64: honest-stop
     `#error` já existente no helper (T3, native gated) — mantém.
   - **macOS (`#os("macos")`):** `teko::sys::abi::pthread_create(&h, 0, entry, ctx)`;
     `thread_join`=`pthread_join(h, 0)` (T2/D101 — libSystem é a ABI sancionada).
   - **Windows (`#os("windows")`):** `teko::sys::abi::CreateThread(0, STACK_BYTES,
     entry, ctx, 0, 0)`; `thread_join`=`WaitForSingleObject(h, WIN_INFINITE)`;
     `thread_detach`=`CloseHandle(h)` (já em abi).

3. **ctx na arena.** Em `emit_spawn_thunk_one`, o `_blk` do ctx passa a sair de
   `tk_region_alloc(tk_region_program(), _need ? _need : 1)` (sobrevive ao pop do
   frame do caller até o child copiar; o child roda na própria arena). Remover o
   `free(_raw)` da trampoline emitida — a `region_program` recupera no fim.

4. **Reroteio.** `emit_spawn` emite `{{ … thread_create((long)&tk_spawn_tramp_{k},
   (long)_sc); }}` (nome Teko-mangled da primitiva). A assinatura da trampoline
   emitida `tk_spawn_tramp_{k}(void *)` casa `entry: u64`.

5. **Retirada.** Nenhuma edição em `teko_rt.c`. Após o reroteio, `tk_thread_spawn`/
   `tk_thread_join*`/`tk_thread_start`/`tk_thread_call`/`tk_thread_handle` ficam sem
   referência emitida — MORTOS, varridos no F9.

## Rulings & laws

- **Teko-only:** novo código só `.tks`; `teko_rt.c` intocado (D90); `tk_task` = exceção
  maintained-C F1 (reusada via extern, não migrada).
- **T2/T3 (D101/R1, DECISION_LOG:1016-1017):** macOS thread-create = `pthread_create
  from "System"`; Linux = clone cru; Windows = CreateThread; aarch64 clone = honest-stop.
- **NO PUSHES:** ctx é bloco de tamanho exato (`_need`) — sem crescimento.
- **exp (D111):** `thread_create`/`thread_join`/`thread_detach` são linkáveis → `exp`.
- **D117:** shadow no scratchpad valida spawn/join de verdade (fixpoint só prova emissão).
- **D118:** pico sub-percentual do expurgo ACEITO; regressão material investiga.
- **Fork protocol:** sem fork — tudo deliberado.
- **Safety:** build em subshell `ulimit -v 4718592`; NUNCA `teko test .`; reseed só no
  `[fixpoint]`; gen2==gen3 byte-idêntico.

## Fixtures

`none — o fixpoint self-build não exercita spawn; a prova de runtime é o SHADOW abaixo
(D117), não fixture versionada.`

**SHADOW (scratchpad, não versionado):** projeto Teko avulso: `main` faz `add`/`spawn`
de N threads, cada uma incrementa um `u32` guardado por `teko::runtime::mtx_lock`/
`mtx_unlock`; join em todas; `assert soma == N`; exit 0. Rodar de verdade; conferir
contra o comportamento do `tk_thread_spawn`+join que substitui.

## Gate

`[fixpoint]` — build gen2 + shadow verde + `gen2==gen3` byte-idêntico; reseed-class
`expurgo` (reseed `bootstrap/teko.c`). Verde = fixpoint byte-idêntico, shadow exit 0,
pico flat/sub-percentual reportado.

## Deps

—

## Done when

`#spawn` emite a primitiva Teko `thread_create` (zero `pthread_create`/`malloc` no ctx do
spawn emitido), o shadow spawn+join roda verde, e o fixpoint gen2==gen3 é byte-idêntico
com `bootstrap/teko.c` reseedado.
</content>
