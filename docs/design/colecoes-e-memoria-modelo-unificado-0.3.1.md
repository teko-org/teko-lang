---
section: design
created: 2026-08-19
status: DESIGN — record of the unified collections + memory model settled with the owner (session
        2026-08-19). No product line. Anchors and reconciles: Doc-2 memory ruling
        (`mudancas-superficie-0.3.1.md:1614-1637`, 2026-08-16), the sealed arena spec
        (`arena-especificacao-unica-0.3.1.md`), the collections remodel
        (`colecoes-remodelagem-backing-fixo-0.3.1.md`), and the Table design
        (`table-collection-sql-linq-0.3.1.md`). This doc is the RECORD so the deliberation is not lost;
        the per-doc specs are updated to match (see §7 corrections).
---

# Collections + memory — the unified model (0.3.1)

The whole collection library reduces to a few **base structures** and one **per-type memory model**; every
named collection is a base or a composition of bases; the **arena itself is the base library instantiated
at the raw layer**. Thread-safety is by default, structural, and free. This doc records the settled model
and the corrections it forces on the earlier docs.

---

## 1. The per-type memory model (Doc-2 law, 2026-08-16)

`mudancas-superficie-0.3.1.md:1614-1637` — transparent per-type, extensible, guarantees **no-UAF + no-leak**:

| kind | lifetime mechanism |
|---|---|
| **value** (value-struct / scalar) | **deep-copy** at the boundary (new materialization, item 14). Reclaim = the **bucket**: reassign/remove/discard only MARKS dead; physical free is the region's bulk-drop. No mid-region reclaim (arena core intact). |
| **class** (ordinary object) | **arena-per-object** (own box, pointer semantics, `codegen.tks:5824`); freed by **region-drop** via escape-analysis (`src/checker/escape.tks`, the residence/LUB). **NOT refcount.** |
| **wrapped** (service / opaque-ref / FFI) | **refcount** (the opt-in escape-hatch): dict in the ROOT arena (addr→count); `wrap`++/`drop`−−; zero → free all, else drop only the shallow pointer (data survives for remaining holders). Only the wrapped enters → the value path's O(1) bulk-free stays intact. |

**Deep-copy surface for objects:** an explicit stdlib `deep_copy<T>(o): T | error`, max depth `u8::MAX` (255)
HARD; at the cap (deep/cyclic graph) it returns the `error` variant — never truncates, never shares
silently. Distinct from the default reference-copy.

**Correction to `colecoes-remodelagem-backing-fixo-0.3.1.md` (was over-applied):** that doc applied
wrap-refcount to ALL object elements. The law scopes refcount to the **wrapped** kind only; a plain
**class** element lives in its own arena-per-object box and is freed by **region-drop / escape-analysis**.
Collection element rules: value → deep-copy + bucket; class → hold a pointer, lifetime by
escape-analysis/region-drop; wrapped → refcount. (§8 open point: a class held in a long-lived collection and
removed early — region-drop-via-escape, or promote to wrapped-refcount — is the one case Doc-2 leaves
implicit.)

---

## 2. Thread-safety is DEFAULT, structural, and free

**Every collection is thread-safe by default. The non-TS escape is the raw array `[]T`.** There is NO
separate "Concurrent" family — that is the C# split we reject (`mudancas-superficie-0.3.1.md:1621`,
"collections TS resolvem a contenção cross-thread"; supersedes the old `plano-collections §3.2`
Arc<Mutex>-opt-in-over-isolation-default model).

**Why the single growable array is banned as a collection substrate.** The current `List` (`class` with
`intern items: []T`, `self.items = teko::list::push(...)`) is **NOT** thread-safe: the whole-backing swap is
a read-modify-write (two concurrent pushes read the same old backing → one update lost), and a bare pointer
store is not even atomic. A coarse mutex would fix it but locks the whole collection per op.

**The TS substrate = the chunk-chain (unrolled linked list).** A collection is a **chain of FIXED chunks**
(each node holds a fixed array of N elements + `next`):
- **grows by linking a new chunk** (a CAS on the tail, or a tiny locked region) — never a whole-backing
  swap, so growth is fine-grained and does not invalidate the structure;
- **iterates cache-friendly** (arrays inside chunks) — recovering what a pure element-per-node list loses;
- a **RowId/index is a stable `u64` handle, never a moving pointer** — growth never moves an element, so a
  reference/index never dangles;
- **reclaim of a superseded chunk is deferred** under concurrent readers (epoch/RCU or the §7.8 retain),
  immediate single-thread (bucket / region-drop).

The fine mutex (`src/runtime/sync.tks`) guards the structural mutation; readers traverse a consistent chain
lock-free. **F1 (fixed/immutable backing) is what makes this sound.**

---

## 3. The base structures + the reuse graph

Picking the strategy by the **dominant operation** (not by covering every op) collapses ~20 collections to
a few bases + thin compositions.

**Bases (distinct strategies):**
| base | paradigm | strategy |
|---|---|---|
| **Chunk-chain** | build → iterate, growable, TS | unrolled linked list of fixed chunks (the §2 substrate) |
| **Ring** | FIFO / both ends, bounded | fixed array, head+tail wrap |
| **Hash** | key→value by hash | parallel chunk-chains + probe |
| **Ordered** | sorted iterate + range | node-linked skip-list / BST |
| **Heap** | repeated get-min | binary heap over a chunk-chain |
| **BitSet** | dense small-int set | packed `[]u64` (fixed) |
| **Weak-ref** | non-retaining pointer (cycle-breaker) | language primitive |

**Compositions (reuse a base):**
| collection | is | reuses |
|---|---|---|
| Stack | chunk-chain + LIFO | Chunk-chain |
| Queue / Deque | ring | Ring |
| Map / Dictionary | the hash table | Hash |
| HashSet | Dictionary, keys only | Hash |
| Counter / MultiSet | `Dictionary<T,u64>` | Hash |
| MultiMap | `Dictionary<K, List<V>>` | Hash + Chunk-chain |
| SortedDictionary | SortedSet + values | Ordered |
| WeakMap / WeakSet | Hash with weak entries | Hash + Weak-ref |
| RingBuffer / BlockingCollection | the ring (+ sync) | Ring |
| **Table** | chunk-chain rows + Map/SortedSet indices + atomic multi-index transaction | Chunk-chain + Hash + Ordered |

**List = ONE strategy: chunk-chain.** Its dominant use is append + iteration (rare removal, rare positional
access), so contiguous-iterating chunk-chain wins; a pure per-element node list would lose the dominant
iterate to pointer-chasing.

---

## 4. The arena IS the base library, at the raw layer

The arena is not merely built on a collection — it **is a collection-of-collections**, each sub-structure a
base in its **intrusive / raw** form (link in the block header, allocated by `mmap`, never from an arena —
that would be circular):

| arena part | base |
|---|---|
| chunk-list (per region) | Chunk-chain (intrusive) |
| region tree (`parent`, `drop_subtree`) | Ordered / tree |
| live-region registry (`reg_next`) | intrusive list/set |
| free-list bins (§7.8, size-classed) | Hash(size-class) → Linked |
| current-region stack + mark/rewind | Stack |

**One base library, two instantiations:** the **intrusive/raw** variant is the arena's own bookkeeping (the
foundation — cannot allocate from an arena); the **arena-backed** variant is the stdlib collections. Same
shapes, two allocation regimes. The base structures must therefore expose an **intrusive-node capability**
(link in the header, pluggable raw-vs-arena allocation).

---

## 5. Inventory + the new collections (add all)

**Have (embedded):** List, Map, Dictionary, HashSet, SortedSet, SortedDictionary, PriorityQueue.
**Planned (design):** Deque, Queue, Stack, LinkedList (+ the former Concurrent* — now DISSOLVED into
TS-by-default).
**Add (owner 2026-08-19):**
- **BitSet / BitArray** — ⭐ compiler-critical (liveness, reachability, visited-sets, the escape-check).
- **Weak / WeakMap / WeakSet** — ⭐ the cycle-breaker for wrap-refcount (the open cycle policy).
- **RingBuffer** — bounded single-thread FIFO.
- **MultiMap** — key→many values.
- **Counter / MultiSet** — element→count.
- **Table<…>** — see §6.

Only **BitSet** and the **weak-ref primitive** are genuinely new structures; the rest are compositions.

---

## 6. Table<…> — recorded (full spec in `table-collection-sql-linq-0.3.1.md`)

A multi-index in-memory table = **composition** (chunk-chain rows + `RowId u64` handles + Map/SortedSet
indices), plus **one novel capability: the atomic multi-index transaction** (insert/update/delete mutates
the row + all its index entries atomically, TS — fine mutex + a single-store commit-point watermark as the
linearization point; CoW opt-in later). Columns ≤16 (the type-arg law) → a comptime `Table1..Table16`
family. A **LINQ-style typed query surface** (primary; SQL-string optional later, desugaring to it) with
lazy `Query<Row>` / `Cursor<Row>` / structured `Predicate` / `Aggregate` / `IndexHint`. An optional
**`FileTable<Row>`** binary single-table store ("SQLite-lite") over the io-streaming surface
(rebuild-indices-on-load, stream not mmap, explicit save). **Deferred fase1c — off the critical path.**

---

## 7. Corrections this record forces on the other docs

1. `colecoes-remodelagem-backing-fixo-0.3.1.md` (`f694696a`): (a) refcount scoped to **wrapped** only —
   class elements are region-drop/escape-analysis, not refcount; (b) the growable substrate is the
   **chunk-chain**, not the single growable pointer-index-array (which is not TS); (c) TS is **default**, no
   Concurrent family.
2. The 0.3.1 index artifact: **Doc-1 = tuning only**; the arena **capability** (incl. wrap-refcount) is
   **Doc-2/§16** (`mudancas-superficie-0.3.1.md:1632-1635`).

These are alignment edits (the architect records them); no semantics change beyond what the owner ruled.

---

## 8. Still owner-open (small)

- **Class element in a long-lived collection, removed early:** region-drop via escape-analysis (the
  collection is a holder that extends residence), or promote to wrapped-refcount? Doc-2 leaves this implicit
  for collections.
- **Query surface:** LINQ-typed (architect-recommended) vs a SQL-string front-end — owner to confirm.
