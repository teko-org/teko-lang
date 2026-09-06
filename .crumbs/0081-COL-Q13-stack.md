---
seq: 0081
crumb-id: COL-Q13
milestone: M2
gate: "[dry]"
reseed-class: "none"
deps: [COL-Q1]
sources:
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:694-700"   # Q13 Stack
  - "docs/design/plano-mestre-0.3.1-implementacao.md:241"                # M2 collections row COL-Q13
---

# 0081 · COL-Q13 — `Stack<T>` (wraps ChunkChain)

> `Stack<T>` — a LIFO over `ChunkChain<T>` (push/pop at the tail, O(1)). Pure `.tks`, teaches nothing, `[dry]`.

## Goal

Deliver `Stack<T>` — a last-in-first-out sequence that wraps `ChunkChain<T>` (COL-Q1): `push`/`pop` at the tail
in amortized O(1), growth by linking a fixed chunk (never a swap). It is a pure `.tks` composition over the
SM-R1-taught surface — teaches nothing, `[dry]`, no reseed. The compiler core does NOT consume `Stack`, so it
gets FULL fixtures. Byte-preserving: additive leaf, no core consumer.

## Where

- `src/collections/stack.tks` — NEW module wrapping `ChunkChain<T>`. No compiler-surface edit.
- No EXISTING fn modified; additive leaf.

## How

1. **Create `src/collections/stack.tks`.** `Stack<T>` holds `intern items: ChunkChain<T>`. `push(x)`→
   `items.push(x)` (tail), `pop()`→`items.pop()` (last), `peek()` reads the tail without removing, `len()`/
   `is_empty()` delegate. Copy W15 doc-comments per the Q9 pattern.
2. **LIFO discipline:** every op is at the tail; no mid-chain touch.
3. **Empty-pop** returns the empty variant (`null`), never faults.

```teko
/**
 * Stack<T> — a LIFO sequence over a ChunkChain<T>: push and pop both act at the tail in amortized O(1),
 * growth links a fixed chunk (never a whole-backing swap). Reclamation is three-category via the chain
 * (value bucket / class region-drop / wrapped release).
 *
 * @since 0.3.1
 */
exp type Stack<T> = class {
    /** The chunk-chain backing; the tail is the top of stack. */
    intern items: ChunkChain<T>

    /** Build an empty stack. */
    pub static fn make(): Stack<T>
    /** Push `x` onto the top (O(1)). */
    pub fn push(x: T)
    /** Pop and return the top, or null when empty (O(1)). */
    pub fn pop(): T | null
    /** Peek the top without removing, or null when empty. */
    pub fn peek(): T | null
    /** The live element count. */
    pub fn len(): u64
}
```

## Rulings & laws

- **Teko-only:** `.tks` leaf; no C twin; no checker/codegen edit.
- **W15 full Javadoc** on the type + every member; no inline `//`.
- **Teach-once (owner 2026-08-19):** no new surface — stands on ChunkChain (SM-R1); `[dry]`, zero reseed.
- **Byte-preserving:** additive leaf, no core consumer.
- **Safety:** NEVER `teko test .` (fixtures ISOLATED); build in a subshell with `ulimit -v 6815744`; commit the
  green step.

## Fixtures

The compiler does NOT consume `Stack` → FULL fixtures (the self-build never touches it).

| fixture | asserts | expected |
|---|---|---|
| `stack_lifo` | push 1..N, pop N..1 (LIFO order exact) | `0` |
| `stack_empty_pop` | `pop()` on empty → null (no fault) | `0` |

## Gate

`[dry]` — compiles + the two fixtures green + trivial fixpoint (byte-identical). "Green" = LIFO order and
empty-pop hold, build byte-identical. **Reseed-class: none.**

## Deps

`COL-Q1` (the ChunkChain base).

## Done when

`src/collections/stack.tks` compiles, `stack_lifo` + `stack_empty_pop` pass, and a `[dry]` build is
byte-identical.
