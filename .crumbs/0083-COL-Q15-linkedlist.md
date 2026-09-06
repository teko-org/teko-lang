---
seq: 0083
crumb-id: COL-Q15
milestone: M2
gate: "[dry]"
reseed-class: "none"
deps: [COL-Q5]
sources:
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:703-706"   # Q15 LinkedList
  - "docs/design/plano-mestre-0.3.1-implementacao.md:243"                # M2 collections row COL-Q15
---

# 0083 · COL-Q15 — `LinkedList<T>` (doubly-linked; shares Node with Q5)

> `LinkedList<T>` — a doubly-linked node list (O(1) anywhere given the node), sharing `Node`/`link_after` with
> the Ordered base (COL-Q5) + the arena chunk-list. Pure `.tks`, teaches nothing, `[dry]`.

## Goal

Deliver `LinkedList<T>` — a doubly-linked node list with NO backing array: O(1) insert/remove anywhere given a
node, the structure for middle-churn workloads (where `List`/chunk-chain pays O(n)). It reuses the `Node`/
`link_after` capability shared with the Ordered base (COL-Q5) and the arena's intrusive chunk-list (record §4).
Pure `.tks` over the SM-R1 surface — teaches nothing, `[dry]`, no reseed. The compiler core does NOT consume it
→ FULL fixtures. GATE-1 note: a class node removed early is region-dropped (conservative default). Byte-
preserving: additive leaf.

## Where

- `src/collections/linked_list.tks` — NEW module. Reuses `Node<T>`/`link_after` from `src/collections/ordered.tks`
  (COL-Q5) — do NOT redefine the node; share it.
- No EXISTING fn modified; additive leaf.

## How

1. **Create `src/collections/linked_list.tks`.** `LinkedList<T>` holds `head`/`tail` `*Node<T>` + `count`.
   `push_front`/`push_back`/`insert_after(node)`/`remove(node)` link/unlink in O(1); `get(i)` walks O(n).
2. **Share the node:** reuse `Node<T>`/`link_after` defined in the Ordered base (COL-Q5) — one node type across
   LinkedList, Ordered, and the arena chunk-list (record §4). Copy W15 doc-comments per the Q9 pattern.
3. **No backing array:** the list is pure nodes; `len` is the maintained `count`, ordering is the link order.
4. **Reclamation (three-category):** value node bucket / class node region-drop (conservative GATE-1 default) /
   wrapped node release.

```teko
/**
 * LinkedList<T> — a doubly-linked list of individually arena-allocated nodes: O(1) insert/remove ANYWHERE
 * given the node, O(n) positional `get`. No backing array (the middle-churn alternative to List's chunk-chain).
 * Shares Node<T>/link_after with the Ordered base (COL-Q5) and the arena chunk-list. Reclamation is
 * three-category per node (value bucket / class region-drop / wrapped release).
 *
 * @since 0.3.1
 */
exp type LinkedList<T> = class {
    /** The first node, or null when empty. */
    intern head: *Node<T>
    /** The last node, for O(1) push_back. */
    intern tail: *Node<T>
    /** The live node count. */
    intern count: u64

    /** Build an empty linked list. */
    pub static fn make(): LinkedList<T>
    /** Prepend `x` (O(1)). */
    pub fn push_front(x: T)
    /** Append `x` (O(1)). */
    pub fn push_back(x: T)
    /** The live node count. */
    pub fn len(): u64
}
```

## Rulings & laws

- **Teko-only:** `.tks` leaf; no C twin; no checker/codegen edit.
- **W15 full Javadoc** on the type + every member; no inline `//`.
- **Node sharing (record §4):** reuse `Node<T>`/`link_after` from COL-Q5 — do NOT introduce a second node type.
- **GATE-1 (class node):** conservative region-drop-via-escape default (leak-safe); promote-to-wrapped is a
  later follow-up.
- **Teach-once (owner 2026-08-19):** no new surface; `[dry]`, zero reseed.
- **Byte-preserving:** additive leaf, no core consumer.
- **Safety:** NEVER `teko test .` (fixtures ISOLATED); build in a subshell with `ulimit -v 6815744`; commit the
  green step.

## Fixtures

The compiler does NOT consume LinkedList → FULL fixtures.

| fixture | asserts | expected |
|---|---|---|
| `linked_list_no_backing` | build via push_front/push_back; order + len correct (no backing array) | `0` |
| `linked_remove_node` | remove a node: value node bucket / class node region-drop (the class path the self-build never reaches) | `0` |

## Gate

`[dry]` — compiles + the two fixtures green + trivial fixpoint (byte-identical). "Green" = order/len + node
remove reclamation hold, build byte-identical. **Reseed-class: none.**

## Deps

`COL-Q5` (the Ordered base + the shared `Node`/`link_after`).

## Done when

`src/collections/linked_list.tks` compiles reusing COL-Q5's node, the two fixtures pass, and a `[dry]` build is
byte-identical.
