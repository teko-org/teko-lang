# S10 · Migração C→Teko do cluster TASK/CONCORRÊNCIA (zero-libc)

Design-doc do arquiteto para o expurgo do cluster de concorrência do `teko_rt.c`.
Base: `fix/retirement`. Escopo: F7 (threads/sync/canais). NÃO toca F9. NÃO toca
`teko_rt.c`/`.h`/`assert.c` (D90). Validação FORMAL pós-F9; SHADOW no scratchpad
durante (D117).

## 1. Censo REAL do cluster vivo (o census super-conta)

O census bruto lista 58 símbolos, mas isso conta campos de struct (`tk_chan_out`,
`tk_chan_err`, `tk_chan_label`…), macros do seam de arena (`tk_g_root`, `tk_arena_marks`…),
wrappers `_u` (`tk_waitgroup_add_u`…), selftests (`*_selftest*`) e internos estáticos
(`tk_thread_start`, `tk_thread_call`, `tk_oschan_writer_fd`…). Cruzando **defs em
`teko_rt.c` × refs em `bootstrap/teko.c` × refs em `src/**/*.tks`**, o cluster REAL de
símbolos migráveis é **~20**, não 30:

| família | símbolos-API | backend C hoje | quente/frio build seco | OS-dep | aloca | refs emitidas (boot) |
|---|---|---|---|---|---|---|
| thread-create | `tk_thread_spawn`, `tk_thread_join`, `tk_thread_join_spawn` | `pthread_create`/`join` + `malloc` blob | **FRIO** (o compilador é single-thread; `#spawn`=0 no src) | sim | sim (malloc) | `tk_thread_spawn`=1 (via `emit_spawn`) |
| waitgroup | `tk_waitgroup_make/add/done/wait/end` | `pthread_mutex`+`pthread_cond` | FRIO | não (POSIX) | region_program | 0 |
| memchan (in-proc, tkt) | `tk_memchan_make/send/recv/close/end` | `pthread_mutex`+2`cond`+ring | FRIO | não (POSIX) | region_program + grow(malloc) | 0 |
| oschan (cross-proc, tkr) | `tk_oschan_make/send/recv/close/end` | AF_UNIX `SOCK_DGRAM` (Linux); Win=panic-stub | FRIO | **sim** (socket ABI) | region_program | 0 |
| tk_chan (captura stdout/stderr por `#test`) | `tk_chan_append` + emit-family | buffer libc | FRIO | não | task-heap | 0 |
| tk_task (seam de arena) | `tk_task_begin/current/end/reset` | `calloc` per-thread | **QUENTE** (todo alloc) | não | libc-heap | 2 (`tk_task_reset`) |

### 1.1 O que JÁ está em Teko (não migrar — é a "arquitetura sem libc" do dono)

- **Mutex / condvar / futex:** `src/runtime/sync.tks` — `mtx_lock`/`mtx_unlock`,
  `cv_wait`/`cv_signal`/`cv_broadcast`, sobre `futex_wait`/`futex_wake` nos 3 SOs
  (Linux `SYS_FUTEX`; macOS `os_sync_wait_on_address`/`wake_by_address` via
  `teko::sys::abi`; Windows `WaitOnAddress`/`WakeByAddress`). **PRONTO.**
- **Atomics:** `teko::sys::atomic_cas_u32/add_u32/load_u32/xchg_u32` — intrínsecos do
  compilador (registrados em `scope.tks`, lowered em `codegen.tks`). **PRONTO.**
- **Stack de thread:** `src/runtime/thread.tks` — `thread_stack_new`/`thread_stack_free`
  via `mmap`+guard nos 3 SOs. **PRONTO.**
- **clone x86_64:** helper `tk_thread_clone` emitido por `codegen.tks`
  (`cg_emit_thread_clone_helper`), builtin `thread_clone`. aarch64 = `#error`
  (T3, não validado — native gated).
- **Superfície de canais:** `src/threads/threads.tks` — tipos `IChannelKind<T>`,
  `Rx<T>`, `Tx<T>`, `Ctx`, `Closed` (S10-SURF, crumb 0115). **Corpos são STUBS**
  (`pop→Closed{}`, `send→null`) — falta cablear ao transporte.

**Conclusão:** o expurgo restante é **só o transporte** (pthread thread-create, ring/
socket dos canais, pthread interno do waitgroup) — a sincronização primitiva já saiu
do libc. O "difícil" (sync sem libc) está feito.

## 2. Desenho da sincronização por-SO (mapa — já landado, referência)

| primitiva | Linux | macOS | Windows |
|---|---|---|---|
| wait | `SYS_FUTEX FUTEX_WAIT` | `os_sync_wait_on_address` (`__ulock`) via abi | `WaitOnAddress` |
| wake | `SYS_FUTEX FUTEX_WAKE` | `os_sync_wake_by_address_{any,all}` | `WakeByAddress{Single,All}` |
| atomic | intrínseco `atomic_*_u32` | idem | idem |
| thread-create | `clone` cru (x86_64; aarch64 T3) | `pthread_create from "System"` (T2/D101; Apple proíbe clone cru) | `CreateThread` (kernel32) |
| thread-join | `SYS_FUTEX` no ctid / wait | `pthread_join from "System"` | `WaitForSingleObject` (já em abi) |
| canal cross-proc | AF_UNIX `SOCK_DGRAM` (raw syscalls) | AF_UNIX via `socket/bind/... from "System"` | named pipe `FILE_FLAG_OVERLAPPED` (D87) |

Waitgroup e memchan NÃO precisam de primitiva nova — reusam `mtx_*`/`cv_*` de `sync.tks`.

## 3. Estado por-tarefa (`tk_task`) — resolução law-first (SEM tocar teko_rt.c)

`tk_task` é o **seam de disciplina de memória (arena)** — a exceção **maintained-C F1**
(D90: `teko_rt.c` intocável; a arena é o keystone F3, não faz parte do expurgo de
concorrência). Resolução:

1. **`tk_task_begin`/`tk_task_current`/`tk_task_end` PERMANECEM maintained-C** e são
   chamados pela trampoline Teko de spawn **via `extern`** — exatamente como
   `tk_thread_start` (C) faz hoje. O `calloc` per-thread dentro de `tk_task_begin` é
   bootstrap de arena (F1/F3), **fora do escopo do expurgo de concorrência**. Nenhuma
   edição em `.c`.
2. **O estado por-tarefa de CONCORRÊNCIA** (cache de socket-writer do oschan, registro
   de nomes de canal/waitgroup) **NÃO** vai no `tk_task`; migra para **slots
   `CTRL_*` no bloco de controle da `region_program`** — o padrão `names_state`/D110
   e coverage/D113 (`arena.tks` já tem `CTRL_WRAP_*`, `CTRL_ENVIRON`; ~30 slots u64
   livres). O `_Thread_local tk_oschan_wcache` do C vira slot per-região.

Isto NÃO é fork: D90 + F1 (tk_task = maintained-C) + D110/D113 (region_program para
estado persistente cross-arena-pop) já deliberam.

## 4. Dependência de DI (D58.1) — design-ahead

A **resolução** de canais (`svc<Rx<T>>(key)`/`svc<Tx<T>>(key)`, lifetime scoped) depende
de **DI**, que NÃO está implementado (DECISION_LOG: `di.tks`=13 linhas, `service`/`svc`
nunca lançados — a implementação foi perdida). Por D58.1 essa aresta é **conhecida e
deliberada** (canais→DI), não é fork.

**Design-ahead:** o **transporte** (corpos runtime memchan/oschan/waitgroup em Teko) NÃO
precisa de DI — desenha-se e implementa-se AGORA (crumbs 0136–0138). O **cablagem final**
de `Rx.pop`/`Tx.send`/`Ctx.wait` aos transportes via `svc<>` fica **BLOQUEADA em DI**: os
crumbs deixam os corpos de transporte prontos e `exp`, com honest-stops nos stubs de
`threads.tks` que resolvem por `svc<>`. Quando DI landar, o wiring é minutos. Marcado
explicitamente em cada crumb.

## 5. Ordem, particionamento e reseeds

Particionado por dependência (cada crumb `[fixpoint]` + reseed ao fim; pico esperado
**flat/sub-percentual** — D118: crescimento sub-percentual do expurgo é ACEITO, ratchet
mira regressão material):

1. **0135 · S10-CC1** thread-create/join/detach em Teko (per-OS) + reroteio do `emit_spawn`;
   retira pthread do spawn. Dep: sync.tks/thread.tks (prontos).
2. **0136 · S10-CC2** waitgroup → Teko sobre `sync.tks`. Dep: 0135 (spawn dirige workers).
3. **0137 · S10-CC3** memchan (canal in-proc, tkt) → Teko sobre `sync.tks`. Dep: 0135.
   Aresta DI (D58.1) marcada.
4. **0138 · S10-CC4** oschan (canal cross-proc, tkr) → Teko syscalls per-OS (+ socket ABI
   nova). Dep: 0135. Aresta DI + D87 (Windows named pipe). Maior/risco.

`tk_chan` (captura de stdout/stderr por `#test`) é **harness/RT-L6 (F8)** — não é canal de
concorrência; **fora deste cluster**, migra com F8. Reportado, não vira crumb aqui.

Ordem serial (reseeds em série — cada um regrava `bootstrap/teko.c`). Todos os corpos C
retirados ficam **mortos até F9** (não deletar agora — F9 SWEEP).

## 6. SHADOW-validation (D117) — projetos avulsos no scratchpad

Assume-se que a shadow em projeto standalone JÁ resolve `teko::sys::abi` (fix de injeção
do prelúdio tratado à parte pelo coordenador). Cada crumb especifica seu shadow (rodar
DE VERDADE, conferir contra o C que substitui). NÃO viram `.tkt`/`.tkr` versionados.

- **CC1 spawn/join:** N threads incrementam um contador guardado por `mtx_*`; join em
  todas; conferir soma == N·incrementos e exit 0.
- **CC2 waitgroup:** `add(N)`, N workers `done()` uma vez; `wait()` só retorna após todos;
  conferir barreira (sem retorno antecipado).
- **CC3 memchan:** producer envia `0..n`, consumer recebe até `Closed`; conferir ordem,
  contagem == n e soma == n(n-1)/2 (espelha `tk_memchan_selftest`).
- **CC4 oschan:** W writers × R readers datagramas; conferir contagem total e sentinela
  de close (recv 0-length → Closed).

## 7. Reseed / medição

- Cada crumb: `[fixpoint]` gen2==gen3 byte-idêntico + reseed `bootstrap/teko.c`
  (INCONDICIONAL por agente). Deixa gen2/gen3 no scratchpad da worktree.
- Pico: mede a linha `teko: memory: peak <N> MB` do build seco, mesma máquina/geração.
  Esperado **flat ou +<0,2%** (código FRIO — sinks nunca crescem no build seco; D113/D116
  shift-de-contabilidade). Regressão material (dezenas de MB) = investigar; sub-percentual
  = landa (D118).

## 8. Forks para o dono

**Nenhum.** Tudo traça a ruling existente: T2 macbook thread-create=pthread-from-System
(D101/R1), T3 aarch64 clone honest-stop (native gated), Windows named pipe (D87), tk_task
maintained-C (D90/F1), canais→DI (D58.1, design-ahead), pico sub-percentual (D118). Execução.
</content>
</invoke>
