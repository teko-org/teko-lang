---
seq: 0136
crumb-id: S10-CC2
milestone: F7
gate: "[fixpoint]"
reseed-class: expurgo
deps: [S10-CC1]
sources:
  - "docs/design/plano-s10-concurrency-cluster-migration.md:1-90"
  - "src/runtime/sync.tks:65-84"   # mtx_unlock, cv_wait/signal/broadcast
---

# 0136 · S10-CC2 — waitgroup em Teko sobre sync.tks

> Reescreve o `tk_waitgroup_*` (pthread mutex+cond, C) como um bloco de controle na
> `region_program` operado por `mtx_*`/`cv_*` de `sync.tks`, e cabla `Ctx`/`Rx`/`Tx`
> add/done/wait.

## Goal

`tk_waitgroup_make/add/done/wait/end` são um contador com espera bloqueante em
`pthread_mutex`+`pthread_cond`. Como `sync.tks` já tem `mtx_lock`/`mtx_unlock` e
`cv_wait`/`cv_signal`/`cv_broadcast` (futex, zero-libc, 3 SOs), a migração é direta: o
waitgroup vira uma struct de palavras `{count:i64, lock:u32, seq:u32}` alocada na
`region_program` (sobrevive ao arena-pop, sede de estado cross-thread — padrão
D110/D113). Os corpos C ficam MORTOS até F9. **byte-mover** (novos símbolos emitidos) →
dirige reseed. FRIO no build seco → pico flat.

## Where

- `src/runtime/sync.tks` (ou NOVO `src/threads/waitgroup.tks`) — NOVO: tipo `WaitGroup`
  + `wg_make`/`wg_add`/`wg_done`/`wg_wait`/`wg_end` (`exp`), sobre `mtx_*`/`cv_*`.
- `src/threads/threads.tks:54-68` — `Ctx.add`/`Ctx.wait`/`Ctx.close` (stubs) — cablear
  ao `WaitGroup`. `Rx.add`/`Rx.done`, `Tx.add`/`Tx.done` (linhas 25-48) idem.
- **NÃO tocar** `teko_rt.c` `tk_waitgroup_*` — D90; mortos até F9.

## How

1. **Bloco de controle na region_program.** `wg_make` aloca via
   `teko::runtime::region_alloc(teko::runtime::region_program(), 16)` (contador + 2
   palavras de sync), zero-fill; retorna o endereço como handle `u64`.

```teko
/**
 * wg_make — allocate a WaitGroup control block in the program region (survives
 * arena-pop; shared cross-thread). Counter starts at zero.
 *
 * @return  handle (control-block address) of the fresh WaitGroup
 * @since 0.3.1
 */
exp fn wg_make(): u64
```

```teko
/**
 * wg_add — add `n` to the counter before spawning the awaited tasks (race-free path).
 * A non-positive result wakes all waiters.
 *
 * @param wg  handle from wg_make
 * @param n   delta (usually the worker count)
 * @since 0.3.1
 */
exp fn wg_add(wg: u64, n: i64)
```

```teko
/**
 * wg_done — decrement the counter by one from a finished worker; wakes waiters at zero.
 *
 * @param wg  handle from wg_make
 * @since 0.3.1
 */
exp fn wg_done(wg: u64)
```

```teko
/**
 * wg_wait — block until the counter reaches zero.
 *
 * @param wg  handle from wg_make
 * @since 0.3.1
 */
exp fn wg_wait(wg: u64)
```

```teko
/**
 * wg_end — release the WaitGroup (no OS object to destroy; idempotent no-op kept for
 * transport symmetry).
 *
 * @param wg  handle from wg_make
 * @since 0.3.1
 */
exp fn wg_end(wg: u64)
```

2. **Corpos** (offsets: `count`=+0 i64, `lock`=+8 u32, `seq`=+12 u32; endereços via
   `teko::mem::load_i64`/`store_i64`/`load_u32`/`store_u32`):
   - `wg_add`: `mtx_lock(lock_addr)`; `count += n`; se `count <= 0` →
     `cv_broadcast(seq_addr)`; `mtx_unlock(lock_addr)`.
   - `wg_done`: `mtx_lock`; `count -= 1`; se `<= 0` → `cv_broadcast`; `mtx_unlock`.
   - `wg_wait`: `mtx_lock`; `loop { if count <= 0 { break } cv_wait(seq_addr, lock_addr) }`;
     `mtx_unlock`.
   Espelha byte-a-byte o idioma C (`teko_rt.c:2510-2526`) sobre as primitivas Teko.

3. **Cablagem** em `threads.tks`: `Ctx` guarda o handle `wg`; `Ctx.add(n)`→`wg_add`,
   `Ctx.wait()`→`wg_wait`, `Ctx.close()`→`wg_end`. `Rx.add`/`Tx.add`→`wg_add(wg,1)`,
   `.done`→`wg_done`. **Aresta DI (D58.1):** a RESOLUÇÃO do `wg` por chave via `svc<>`
   fica bloqueada em DI — deixar honest-stop `panic("channel WaitGroup: DI svc<> not
   yet wired")` no ponto de resolução por chave; o `WaitGroup` puro (handle explícito)
   fica pronto e testável no shadow AGORA.

## Rulings & laws

- **Teko-only / D90:** `teko_rt.c` intocado; `tk_waitgroup_*` mortos até F9.
- **sync.tks reuse:** nada de primitiva nova — `mtx_*`/`cv_*` já zero-libc nos 3 SOs.
- **region_program (D110/D113):** estado cross-thread persistente vai no bloco de
  controle da região de programa, não em libc-heap.
- **NO PUSHES:** bloco de tamanho fixo (16 B).
- **exp (D111):** `wg_*` linkável → `exp`.
- **D58.1 (design-ahead):** resolução por `svc<>` bloqueada em DI → honest-stop; núcleo
  landado.
- **D117:** shadow valida a barreira; **D118:** pico flat esperado.
- **Fork protocol:** sem fork.
- **Safety:** subshell `ulimit -v 4718592`; sem `teko test .`; reseed no `[fixpoint]`.

## Fixtures

`none — o fixpoint não exercita waitgroup; prova de runtime é o SHADOW (D117).`

**SHADOW (scratchpad):** `wg_make`; `wg_add(N)`; spawn N workers (CC1) cada um
`wg_done()` após trabalho; `wg_wait()`; conferir que `wait` só retorna após TODOS
(sem retorno antecipado) e exit 0. Espelha `tk_waitgroup_selftest`.

## Gate

`[fixpoint]` — gen2 + shadow verde + `gen2==gen3` byte-idêntico; reseed `expurgo`.

## Deps

S10-CC1 (spawn dirige os workers do shadow).

## Done when

`WaitGroup` roteia por `mtx_*`/`cv_*` de `sync.tks` na `region_program`, o shadow de
barreira roda verde, `Ctx`/`Rx`/`Tx` add/done/wait cablados (resolução `svc<>` em
honest-stop DI), e o fixpoint gen2==gen3 é byte-idêntico reseedado.
</content>
