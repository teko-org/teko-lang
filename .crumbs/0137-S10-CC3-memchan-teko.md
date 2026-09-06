---
seq: 0137
crumb-id: S10-CC3
milestone: F7
gate: "[fixpoint]"
reseed-class: expurgo
deps: [S10-CC1, S10-CC2]
sources:
  - "docs/design/plano-s10-concurrency-cluster-migration.md:1-90"
  - "src/runtime/sync.tks:65-84"
  - "DECISION_LOG.md:609-625"   # D58.1 canais→DI, memchan p/ tkt
---

# 0137 · S10-CC3 — memchan (canal in-proc, tkt) em Teko sobre sync.tks

> Reescreve o transporte de canal em-memória (`tk_memchan_*`, pthread+ring, C) como um
> ring de tamanho-fixo na arena operado por `mtx_*`/`cv_*`, sem grow-realloc.

## Goal

`tk_memchan_make/send/recv/close/end` é um ring buffer MPSC com `pthread_mutex`+2`cond`
(`not_full`/`not_empty`) e um `tk_memchan_grow` que dobra o ring por `malloc` — o transporte
in-proc dos testes `tkt` (D58.1/D62). Migra para Teko: ring de **capacidade fixa**
(derivada de `bounds`; unbounded = capacidade-teto pré-dimensionada, **sem grow** — NO
PUSHES) na `region_program`, com dois seqs de condvar via `sync.tks`. Corpos C mortos até
F9. **byte-mover** → reseed. FRIO no build seco → pico flat.

## Where

- NOVO `src/threads/memchan.tks` — tipo `MemChan` + `memchan_make`/`send`/`recv`/`close`/
  `end` (`exp`), sobre `mtx_*`/`cv_*` de `sync.tks`.
- `src/threads/threads.tks` — `IChannelKind` impl do transporte memchan; `Tx.send`/
  `Rx.pop`/`Tx.close`/`Ctx.close` cablados a `MemChan` (resolução `svc<>` = honest-stop DI).
- **NÃO tocar** `teko_rt.c` `tk_memchan_*` — D90; mortos até F9.

## How

1. **Ring de tamanho fixo.** `memchan_make(elem_size, bounds)` aloca na `region_program`
   um bloco `{lock:u32, not_empty:u32, not_full:u32, head:u64, tail:u64, count:u64,
   cap:u64, closed:u32, buf:ptr}` + o `buf` de `cap*elem_size` bytes (zero-fill). `cap`
   = `bounds` se `bounds>0`; senão uma capacidade-teto de projeto (const, ex. 1024) —
   **jamais cresce** (unbounded vira "muito grande fixo"; o design NO PUSHES proíbe o
   `tk_memchan_grow`; se encher, `send` bloqueia como bounded).

```teko
/**
 * memchan_make — allocate a fixed-capacity in-process MPSC ring in the program region.
 * `bounds` sets the capacity; `bounds == 0` uses the project unbounded ceiling (the
 * ring never grows — NO PUSHES).
 *
 * @param elem_size  bytes per element (fixed-width v1)
 * @param bounds     capacity, or 0 for the unbounded ceiling
 * @return           handle (control-block address) of the channel
 * @since 0.3.1
 */
exp fn memchan_make(elem_size: u64, bounds: u64): u64
```

```teko
/**
 * memchan_send — copy one element in, blocking while the ring is full and open.
 *
 * @param ch        handle from memchan_make
 * @param elem_ptr  address of the source element (elem_size bytes)
 * @since 0.3.1
 */
exp fn memchan_send(ch: u64, elem_ptr: u64)
```

```teko
/**
 * memchan_recv — copy the next element out; blocks while empty and open.
 *
 * @param ch       handle from memchan_make
 * @param out_ptr  address of the destination (elem_size bytes)
 * @return         1 if an element was received, 0 if the channel is closed and drained
 * @since 0.3.1
 */
exp fn memchan_recv(ch: u64, out_ptr: u64): i64
```

```teko
/**
 * memchan_close — mark the channel closed and wake all waiters; idempotent.
 *
 * @param ch  handle from memchan_make
 * @since 0.3.1
 */
exp fn memchan_close(ch: u64)
```

```teko
/**
 * memchan_end — release the channel (region-owned; wakes waiters). Idempotent.
 *
 * @param ch  handle from memchan_make
 * @since 0.3.1
 */
exp fn memchan_end(ch: u64)
```

2. **Corpos** (espelham `teko_rt.c:2399-2430` sobre `sync.tks`):
   - `send`: `mtx_lock`; se `closed` → `mtx_unlock; return`; `loop { if count<cap ||
     closed break; cv_wait(not_full_seq, lock) }`; se `closed` → unlock/return; copia
     `elem_ptr` → `buf + tail*elem_size` (`teko::mem` copy exato); `tail=(tail+1)%cap`;
     `count+=1`; `cv_signal(not_empty_seq)`; `mtx_unlock`.
   - `recv`: `mtx_lock`; `loop { if count>0 || closed break; cv_wait(not_empty_seq,
     lock) }`; se `count==0 && closed` → unlock/return 0; copia `buf + head*elem_size` →
     `out_ptr`; `head=(head+1)%cap`; `count-=1`; `cv_signal(not_full_seq)`; unlock;
     return 1.
   - `close`/`end`: `mtx_lock`; `closed=1`; `cv_broadcast(not_empty_seq)`;
     `cv_broadcast(not_full_seq)`; `mtx_unlock`.

3. **Cablagem** `threads.tks`: `Tx.send(v)`→`memchan_send`, `Rx.pop()`→`memchan_recv`
   (0→`Closed{}`, 1→`v`), `Tx.close`/`Ctx.close`→`memchan_close`. **Aresta DI (D58.1):**
   a resolução do handle por chave via `svc<Rx<T>>`/`svc<Tx<T>>` fica bloqueada em DI —
   honest-stop no ponto de resolução; o transporte puro (handle explícito) roda no shadow.

## Rulings & laws

- **Teko-only / D90:** `teko_rt.c` intocado; `tk_memchan_*` mortos até F9.
- **NO PUSHES / ZERO grow:** `tk_memchan_grow` NÃO é portado — ring fixo, `send`
  bloqueia ao encher (lei dura: array não cresce dinamicamente).
- **region_program (D110/D113):** ring + control-block persistente cross-thread.
- **exp (D111):** `memchan_*` linkável → `exp`.
- **D58.1 (design-ahead):** resolução `svc<>` bloqueada em DI → honest-stop; transporte
  landado.
- **D117 shadow; D118 pico flat.**
- **Fork protocol:** sem fork.
- **Safety:** subshell `ulimit -v 4718592`; sem `teko test .`; reseed no `[fixpoint]`.

## Fixtures

`none — o fixpoint não exercita memchan; prova de runtime é o SHADOW (D117).`

**SHADOW (scratchpad):** producer (spawn/CC1) envia `0..n`; consumer `recv` até 0
(closed); conferir ordem crescente, contagem == n, soma == n(n-1)/2 (espelha
`tk_memchan_selftest`, `teko_rt.c:2470-2495`); exit 0.

## Gate

`[fixpoint]` — gen2 + shadow verde + `gen2==gen3` byte-idêntico; reseed `expurgo`.

## Deps

S10-CC1 (spawn dirige producer), S10-CC2 (Ctx/WaitGroup do canal).

## Done when

O transporte memchan roteia por `sync.tks` num ring fixo da `region_program` (zero
pthread, zero grow), o shadow producer/consumer roda verde, `Tx`/`Rx` cablados
(resolução `svc<>` em honest-stop DI), e o fixpoint gen2==gen3 é byte-idêntico reseedado.
</content>
