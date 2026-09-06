---
seq: 0087
crumb-id: COL-Q19
milestone: M2
gate: "[dry]"
reseed-class: "none"
deps: [COL-Q3, S16-SYNC]
sources:
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:714-717"   # Q19 RingBuffer/BlockingCollection
  - "docs/design/plano-mestre-0.3.1-implementacao.md:247"                # M2 collections row COL-Q19
  - "src/runtime/sync.tks:78-93"                                         # cv_wait/cv_signal condvar
---

# 0087 · COL-Q19 — `RingBuffer<T>` / `BlockingCollection<T>` (bounded Ring + condvar)

> `RingBuffer`/`BlockingCollection` — a bounded `Ring<T>` (COL-Q3) + the condvar (`sync.tks` cv_wait/cv_signal)
> for producer/consumer blocking. Pure `.tks`, teaches nothing, `[dry]`; concurrency fixtures mandatory.

## Goal

Deliver `RingBuffer<T>` / `BlockingCollection<T>` — a bounded `Ring<T>` (COL-Q3, never grows) plus the condvar
from `src/runtime/sync.tks` (`cv_wait` `:78`, `cv_signal` `:85`, `mtx_lock` `:63`): a producer blocks when the
buffer is full, a consumer blocks when empty, each wakes the other. Pure `.tks` composition over the SM-R1
surface + the (M2) S16-SYNC condvar — teaches nothing, `[dry]`, no reseed. The compiler is single-threaded, so
it exercises ZERO thread races here → concurrency fixtures are MANDATORY (every TS path needs one).
Byte-preserving: additive leaf.

## Where

- `src/collections/ringbuffer.tks` — NEW module: `RingBuffer<T>` = bounded `Ring<T>` + condvar;
  `BlockingCollection<T>` = the blocking producer/consumer wrapper.
- Reads `src/runtime/sync.tks:63,78,85` (`mtx_lock`/`cv_wait`/`cv_signal`) — landed via S16-SYNC. No edit to
  `sync.tks`.
- No EXISTING fn modified; additive leaf.

## How

1. **Create `src/collections/ringbuffer.tks`.** `RingBuffer<T>` holds `intern items: Ring<T>` (bounded, fixed
   `cap`), `intern mtx: u64`, `intern not_full: u64`, `intern not_empty: u64` (condvar sequences). Copy W15
   doc-comments per the Q9 pattern.
2. **`put(x)`** under `mtx_lock`: while full, `cv_wait(not_full, mtx)`; push_back; `cv_signal(not_empty)`.
3. **`take()`** under `mtx_lock`: while empty, `cv_wait(not_empty, mtx)`; pop_front; `cv_signal(not_full)`;
   return the value.
4. **`BlockingCollection<T>`** wraps `RingBuffer<T>` exposing the blocking producer/consumer surface (`add` /
   `take` + a `complete()` sentinel that wakes all waiters). Bounded → NEVER grows (the Ring's bounded mode).

```teko
/**
 * RingBuffer<T> — a BOUNDED, blocking producer/consumer queue: a fixed Ring<T> (COL-Q3, never grows) guarded
 * by a mutex + two condition variables (src/runtime/sync.tks cv_wait/cv_signal). `put` blocks while full,
 * `take` blocks while empty; each signals the other. The bounded-full backpressure is the point — it never
 * grows past `cap`.
 *
 * @since 0.3.1
 */
exp type RingBuffer<T> = class {
    /** The bounded ring backing (fixed capacity). */
    intern items: Ring<T>
    /** The guarding mutex word (src/runtime/sync.tks:63). */
    intern mtx: u64
    /** The "not full" condition sequence a blocked producer waits on. */
    intern not_full: u64
    /** The "not empty" condition sequence a blocked consumer waits on. */
    intern not_empty: u64

    /** Build a bounded ring buffer of fixed capacity `cap`. */
    pub static fn make(cap: u64): RingBuffer<T>
    /** Put `x`, blocking while the buffer is full; signals a waiting consumer. */
    pub fn put(x: T)
    /** Take an element, blocking while the buffer is empty; signals a waiting producer. */
    pub fn take(): T
}
```

## Rulings & laws

- **Teko-only:** `.tks` leaf; the condvar is `sync.tks` (S16-SYNC, `.tks`); no C twin touched.
- **W15 full Javadoc** on both types + every member; no inline `//`.
- **TS-by-default + bounded backpressure:** `RingBuffer` NEVER grows (bounded Ring); a full producer blocks —
  the point of the bounded collection.
- **Concurrency fixtures MANDATORY (owner ruling 2):** the compiler is single-threaded → it exercises zero
  races; every TS path here needs an isolated fixture.
- **Teach-once (owner 2026-08-19):** no new surface — stands on Ring + the sync condvar (SM-R1 / S16-SYNC);
  `[dry]`, zero reseed.
- **Byte-preserving:** additive leaf, no core consumer.
- **Safety:** NEVER `teko test .` (fixtures ISOLATED, one per project — a monomorph leak in a combined run
  crashes the container); build in a subshell with `ulimit -v 6815744`; commit the green step.

## Fixtures

The compiler does NOT consume these AND they are concurrency paths → FULL + mandatory concurrency fixtures.

| fixture | asserts | expected |
|---|---|---|
| `ringbuffer_bounded` | fill to cap, over-put blocks then unblocks on a take; drain FIFO; never grows past cap | `0` |
| `blocking_producer_consumer` | N producers + M consumers over the buffer; each item delivered exactly once (no loss, no dup) | `0` |

## Gate

`[dry]` — compiles + the two concurrency fixtures green + trivial fixpoint (byte-identical). "Green" = bounded
backpressure + exactly-once producer/consumer delivery hold, build byte-identical. **Reseed-class: none.**

## Deps

`COL-Q3` (the Ring base), `S16-SYNC` (the futex/condvar sync FFI — the mutex + condition variables).

## Done when

`src/collections/ringbuffer.tks` compiles, `ringbuffer_bounded` + `blocking_producer_consumer` pass, and a
`[dry]` build is byte-identical — a bounded blocking queue over Ring + condvar.
