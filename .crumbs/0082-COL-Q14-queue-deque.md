---
seq: 0082
crumb-id: COL-Q14
milestone: M2
gate: "[dry]"
reseed-class: "none"
deps: [COL-Q3]
sources:
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:701-702"   # Q14 Queue/Deque
  - "docs/design/plano-mestre-0.3.1-implementacao.md:242"                # M2 collections row COL-Q14
---

# 0082 · COL-Q14 — `Queue<T>` / `Deque<T>` (wrap Ring)

> `Queue<T>`/`Deque<T>` — O(1) both-ends over `Ring<T>` (COL-Q3). Pure `.tks`, teaches nothing, `[dry]`.

## Goal

Deliver `Queue<T>` (FIFO) and `Deque<T>` (double-ended), each wrapping the `Ring<T>` base (COL-Q3): O(1) at both
ends via head/tail wrap, zero shift; unbounded grow re-linearizes into a bigger fixed ring (never a resize).
Pure `.tks` composition over the SM-R1 surface — teaches nothing, `[dry]`, no reseed. The compiler core does NOT
consume these → FULL fixtures. Byte-preserving: additive leaves, no core consumer.

## Where

- `src/collections/queue.tks` — NEW module: `Queue<T>` wrapping `Ring<T>` (FIFO: `push_back`/`pop_front`).
- `src/collections/deque.tks` — NEW module: `Deque<T>` wrapping `Ring<T>` (both ends).
- No EXISTING fn modified; additive leaves.

## How

1. **`Queue<T>`** holds `intern items: Ring<T>`; `enqueue`→`items.push_back`, `dequeue`→`items.pop_front`,
   `peek`→front, `len`/`is_empty` delegate. Copy W15 doc-comments per the Q9 pattern.
2. **`Deque<T>`** exposes `push_front`/`push_back`/`pop_front`/`pop_back` all delegating to `Ring<T>`'s O(1)
   wrap ops.
3. **Grow** (unbounded) is the Ring's re-linearize-into-a-bigger-fixed-ring (old region dropped, order
   preserved from head) — never an in-place resize.

```teko
/**
 * Queue<T> — a FIFO over a Ring<T>: enqueue at the back, dequeue at the front, both O(1) with wrap (zero
 * shift). Unbounded growth re-linearizes into a larger fixed ring (F1: never a resize).
 *
 * @since 0.3.1
 */
exp type Queue<T> = class {
    /** The ring backing; front is the dequeue end, back the enqueue end. */
    intern items: Ring<T>

    /** Build an empty queue with an initial fixed capacity. */
    pub static fn make(cap: u64): Queue<T>
    /** Enqueue `x` at the back (O(1), wrap). */
    pub fn enqueue(x: T)
    /** Dequeue the front, or null when empty (O(1)). */
    pub fn dequeue(): T | null
    /** The live element count. */
    pub fn len(): u64
}
```

## Rulings & laws

- **Teko-only:** `.tks` leaves; no C twin; no checker/codegen edit.
- **W15 full Javadoc** on both types + every member; no inline `//`.
- **Fixed-backing (F1):** growth re-linearizes into a NEW fixed ring; NEVER an in-place resize.
- **Teach-once (owner 2026-08-19):** no new surface — stand on Ring (SM-R1); `[dry]`, zero reseed.
- **Byte-preserving:** additive leaves, no core consumer.
- **Safety:** NEVER `teko test .` (fixtures ISOLATED); build in a subshell with `ulimit -v 6815744`; commit the
  green step.

## Fixtures

The compiler does NOT consume Queue/Deque → FULL fixtures.

| fixture | asserts | expected |
|---|---|---|
| `queue_fifo` | enqueue 1..N, dequeue in FIFO order | `0` |
| `deque_both_ends` | interleaved front/back push+pop, order correct | `0` |
| `deque_wrap_grow` | fill/drain across the wrap boundary + an unbounded grow (order preserved from head) | `0` |

## Gate

`[dry]` — compiles + the three fixtures green + trivial fixpoint (byte-identical). "Green" = FIFO + both-ends +
wrap/grow order hold, build byte-identical. **Reseed-class: none.**

## Deps

`COL-Q3` (the Ring base).

## Done when

`queue.tks` + `deque.tks` compile, the three fixtures pass, and a `[dry]` build is byte-identical.
