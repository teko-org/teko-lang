---
section: design
created: 2026-08-19
status: DESIGN — implementation QUEUE. No product line: this doc IS the deliverable (no `.tks`, no build, no
        reseed, no `teko test`). It consolidates the settled unified collections + memory model into a
        PHASED, SERIAL work-queue the coordinator dispatches to implementer agents ONE AT A TIME. Serial is a
        HARD rule for anything touching the compiler/reseed — "um reseed de cada vez" (owner). It composes with,
        and never duplicates, the feeder crumb sets.
source: colecoes-e-memoria-modelo-unificado-0.3.1.md (the RECORD — source of truth, incl. §7 corrections + §8
        owner-open GATES), colecoes-remodelagem-backing-fixo-0.3.1.md (the collections remodel, ALIGNED via its
        §0.0), table-collection-sql-linq-0.3.1.md (Table, fase1c), mudancas-superficie-0.3.1.md:1614-1637
        (Doc-2 three-category memory law), reducao-memoria-arrays-0.3.1.md (C1–C17, the `of_len`+index-assign
        fixed-backing prerequisite), io-streaming-0.3.1.md (crumbs 1–9, for FileTable),
        arena-especificacao-unica-0.3.1.md / arena-em-teko.md (the arena; mostly built in src/runtime/arena.tks).
rulings: three owner refinements (2026-08-19) bake the SHAPE of this queue — (1) TEACH-ONCE: one consolidated
        front phase teaches the compiler ALL new surface the whole library needs = ONE reseed; the library is
        then pure `.tks` over the taught surface; (2) NO REDUNDANT FIXTURES: write `.tkr` only for what the
        self-build FIXPOINT does NOT exercise (error branches, edge/boundary, concurrency); (3) EXPURGO not
        tombstone: a de-taught construct is cleanly removed from lexer+parser+checker — simply unrecognised, no
        "X was removed" diagnostic.
frozen: bootstrap/teko.c + the C twins are OUTPUT/FROZEN; new product work is `.tks` only. Drains DIRECT into
        fix/retirement by fast-forward (no PR).
---

# Collections + memory — the implementation QUEUE (0.3.1)

The settled model in one breath (record §0): **bases** = chunk-chain (the growable + thread-safe substrate) /
Ring / Hash / Ordered / Heap / BitSet / weak-ref; **TS is DEFAULT** (chunk-chain + fine mutex or CAS-append on
the tail; the single growable-array swap is BANNED as an RMW race; no Concurrent family; non-TS = raw `[]T`);
**memory is three-category** per-type (value = deep-copy + bucket / class = arena-per-object + region-drop via
escape-analysis / wrapped = refcount); **every collection is a base or a composition of bases**; and **the arena
is the base library instantiated at the raw/intrusive layer**.

## The shape (three owner refinements, baked in)

The queue is **three phases**, ordered to MINIMISE reseeds and honour build-first:

- **FASE 0 — TEACH THE COMPILER, ONCE (exactly 1 teaching reseed).** One consolidated phase teaches the
  compiler EVERY new surface the whole library will need — all new intrinsics/primitives/lowerings that touch
  checker/codegen/lower — in a SINGLE reseed. It is **purely additive** (nothing is removed here, so nothing
  breaks; the old `push`/`empty`/… keep working). Spreading "teach the compiler" across many crumbs would cost
  one reseed each; we refuse that.
- **FASE 1 — THE LIBRARY IN PURE TEKO (no new teaching).** Bases, the 7 conversions, the compositions, and
  Table are ordinary `.tks` over the taught surface — they teach the compiler NOTHING new. A collection the
  compiler core does NOT consume needs no reseed at all (`[dry]`). A collection the core DOES consume (e.g.
  `Map` via `teko::env`) rides a **fixpoint rebuild** — a byte-identity gen2==gen3 check, NOT a new teaching —
  because the compiler is rebuilt on top of it; that is the only reseed pressure in FASE 1 and it teaches no
  language.
- **FASE 2 — THE EXPURGO, ONCE (exactly 1 expurgo reseed).** After FASE 1 has migrated EVERY caller off the
  old constructs, one consolidated step **removes** them (the single growable array, `teko::list::push`/`grow`,
  `empty`/`with_cap`/`grow_inplace` at `typer.tks:805-835`) cleanly from lexer + parser + checker. A removed
  construct is simply **unrecognised** — no tombstone, no "X was removed" diagnostic, as if it never existed
  (owner ruling 3). Expurgo MUST follow migration (build-first): it cannot fold into FASE 0 because at FASE 0
  time the library still calls the old constructs.

**Reseed budget: exactly 2 teaching/expurgo reseeds (FASE 0 + FASE 2)** bracketing a pure-Teko library, plus
the unavoidable fixpoint rebuilds where the core consumes a swapped collection (FASE 1, teaching nothing).

**No-redundant-fixtures rule (owner ruling 2).** The self-build FIXPOINT already exercises every path the
compiler itself walks: the compiler USES `List`/`Map`/… so building itself IS their happy-path test, and a
memory leak would trip the 6.5 GiB guard or blow the fixpoint. So this queue writes a `.tkr` fixture ONLY for
what the self-build does NOT hit:
- **error branches** the compiler never triggers (e.g. `deep_copy` depth-255 → `error`, a failing atomic txn
  rollback);
- **edge/boundary** the compiler happens not to reach (empty-pop, weak-dead, bounded-full);
- **concurrency** — the compiler is single-threaded, so it exercises ZERO thread races; every TS path needs a
  fixture;
- **any collection the compiler does NOT consume** — the self-build never touches it, so it gets full fixtures.
A collection the core DOES consume gets fixtures only for the three non-self-build categories above.

---

## 1. How to read this queue

Each item gives: **id + one-line goal**; **files touched** (real paths); **API shape** (W15 doc-comments the
implementer copies verbatim); **fixtures** (only the non-self-build-exercised paths); **gate**; **reseed-class**;
**deps + why here**.

**Gate legend.**
- **`[dry]`** = compiles (`--no-verify --release`) + the item's scoped `.tkr` fixtures green + a trivial
  fixpoint (the additive `.tks` changes no emitted compiler byte). Used for a library module the compiler core
  does not instantiate.
- **`[fixpoint]`** = build gen2 (`TEKO_BACKEND=native`), scoped regression green, FIXPOINT gen2==gen3
  byte-identical. Used for a FASE 1 swap the core consumes — it teaches nothing, but the compiler is rebuilt on
  it. NO new seed language; the blessed binary advances only to carry the new library body.
- **`[RITUAL]`** = the full gate + a genuine **teaching/expurgo reseed** (a new seed that understands new/less
  surface). ONLY FASE 0 and FASE 2. The 6.5 GiB build guard (`ulimit -v 6815744`) is INVIOLABLE at every gate.

**⚑ OWNER RULING (2026-08-19): the queue is APPROVED. GATE-2 = LINQ (owner: "vamos de LINQ, melhor").
GATE-1 ships on the conservative default (region-drop-via-escape); promote-to-wrapped stays an additive
follow-up.** Dispatch is serial, one item at a time, FASE 0 → FASE 1 → FASE 2.

**Two owner-open GATES block specific items (record §8) — flagged inline:**
- **GATE-1 (class lifetime in a long-lived collection):** a `class` element removed early — region-drop via
  escape-analysis (the collection is a holder that raised the residence LUB) vs promote-to-wrapped (refcount,
  freed on the spot). **Resolution baked in: FASE 0 teaches the conservative region-drop-via-escape holder
  semantics NOW (leak-safe, never UAF); promote-to-wrapped is an additive follow-up that unblocks when GATE-1
  closes.** No item is BLOCKED by GATE-1 — only the class-early-remove *eager-free optimization* is gated.
- **GATE-2 (query surface) — RESOLVED: LINQ-typed (owner 2026-08-19).** The SQL-string front-end is dropped
  (may return later as sugar desugaring to the LINQ core). Q-Query is UNBLOCKED and builds the LINQ surface.

---

## 2. FASE 0 — TEACH THE COMPILER, ONCE (the single teaching reseed)

**F0 — the consolidated teaching.** One implementer, one reseed. Teach the compiler EVERY new surface the whole
collection library + memory model needs. Purely additive: nothing existing is removed, so nothing breaks.

**Files touched (compiler surface).** `src/checker/typer.tks` (new intrinsics' types, next to the existing
`of_len`/slice forms `:805-835`), `src/lir/lower.tks` (lowerings), `src/codegen/codegen.tks` (emission; note
`emit_slice_of_len` already exists `:3167`), `src/checker/escape.tks` (the class-holder residence rule),
`src/parser/*` + `src/lexer/*` (ONLY if a new intrinsic needs surface syntax — most are builtin calls, no new
syntax), `src/runtime/arena.tks` (expose the intrusive-node capability), `src/runtime/sync.tks` (already has
`mtx_lock`/`cv_*`; expose the CAS-append helper if missing), `src/runtime/teko_rt.{c,h}` (ONLY the maintained-C
assert/atomic seed if a new atomic intrinsic is needed — otherwise untouched).

**The surface to teach (the complete list — everything checker/codegen/lower must learn in this ONE step):**

1. **`of_len<T>(n): []T` + index-assign `xs[i] = v` + the `count` watermark idiom** — IF not already landed
   from `reducao-memoria-arrays-0.3.1.md` (`emit_slice_of_len` at `codegen.tks:3167`, `TSliceOfLen` in the
   checker exist; the typer still exposes `empty`/`push`/`with_cap`/`grow_inplace` at `:805-835`). Teach the
   ADD side here; the REMOVE of the old four is FASE 2.
2. **The VALUE regime intrinsics** — `place<T>(v): *T` (write a value once into the collection's bump), `read<T>
   (p): T`, `write<T>(p, x)`, and the **bucket** mark (reassign/remove = mark-dead-only; physical free at
   region-drop). Remodel §2.7.
3. **The CLASS-holder residence rule in `escape.tks`** — a collection is a **holder that raises a stored
   `class` element's residence LUB to the collection's region**, so the element is freed by region-drop when
   the collection drops; the conservative default for GATE-1. No refcount for `class`.
4. **The WRAPPED refcount intrinsics** — `retain(obj)` / `release(obj)` (addr→count dict in the ROOT arena;
   zero → free) for the `wrapped` kind ONLY. Doc-2/§16 arena capability (`mudancas-superficie-0.3.1.md:1619-1623`).
5. **The weak-ref primitive** — the non-retaining reference hook against the wrap-refcount table (`Weak<T>`
   read that does NOT bump the count; `get` upgrades iff count > 0). The cycle-breaker.
6. **`deep_copy<T>(o): T | error`** — the monomorph-driven recursive clone with the HARD `u8::MAX`=255 depth
   cap returning the `error` variant at the cap (never truncates, never silently shares). Needs a codegen
   intrinsic to walk the monomorphized fields; remodel §2.5-4.
7. **The chunk-chain node capability from the arena** — expose `ChunkNode` alloc/link over the arena's intrusive
   chunk-list (`arena.tks:13-19` `CHUNK_NEXT/CHUNK_CAP/CHUNK_USED`, `region_alloc`/`region_drop`) so the pure-
   Teko `ChunkChain<T>` (Q1) can link fixed chunks and drop regions without new teaching.
8. **The CAS-append helper** — confirm `teko::sys::atomic_cas_*` reaches a tail-link/watermark-bump helper (the
   TS growth point); `sync.tks` already has the mutex/condvar side. Teach only if a helper is missing.

Everything ELSE the library needs is ordinary `.tks` over these — no further teaching. (BitSet is pure `.tks`
over `of_len`+`[]u64`; the collections are pure `.tks` over chunk-chain + place/read/retain/release.)

**Fixtures (ONLY the non-self-build-exercised surface paths).** The self-build will exercise `of_len`/`place`/
the class-holder path as soon as FASE 1 migrates the compiler's own `Map`/`List`, so NO happy-path fixture for
those. Write only:
- `deepcopy_depth_cap` — `deep_copy` of a graph deeper than 255 (or cyclic) returns the `error` variant; the
  `match` catches it; no silent partial copy. (Error branch the compiler never triggers.) exit 0.
- `deepcopy_exact` — `deep_copy` of a shallow graph is an independent clone; mutating the clone leaves the
  original; refcounts correct. (Edge the compiler does not walk.) exit 0.
- `wrapped_retain_release` — a wrapped object retained twice, released twice: freed exactly at zero, not before;
  no UAF, no leak. (The compiler uses no wrapped objects — this whole path needs a fixture.) exit 0.
- `weak_dead` / `weak_cycle_break` — a weak ref to a freed target returns null; a two-object cycle with one weak
  edge is fully freed. exit 0.

**Gate.** `[RITUAL]` — the ONE teaching reseed. Build gen2 native, the four surface fixtures green, FIXPOINT
gen2==gen3, reseed (the new seed understands the added intrinsics). **Reseed-class:** TEACHING (the only one on
the front).

**Deps / why here.** Depends on `reducao-memoria-arrays` P0.of_len having landed (or folds it in). FIRST and
ALONE: every FASE 1 item stands on this taught surface, so teaching it once here removes all downstream reseed
pressure except the core-consuming fixpoint rebuilds.

---

## 3. FASE 1 — THE LIBRARY IN PURE TEKO (no new teaching)

Order: **bases (Q1–Q8) → convert the 7 embedded collections (Q9–Q12) → compositions / new collections
(Q13–Q19) → Table (Q20–Q22, fase1c, LAST).** All are ordinary `.tks` over the FASE 0 surface. Build-first:
each conversion builds and proves the replacement green BEFORE FASE 2 removes the old root.

### Q1 — the CHUNK-CHAIN base (the growable + TS substrate)

**Goal.** `ChunkChain<T>` — the unrolled linked list of FIXED chunks that is the growable, thread-safe,
cache-friendly, stable-index substrate every default collection sits on (record §2).

**Files.** NEW `src/collections/chunk_chain.tks`. Pure `.tks` over the FASE 0 chunk-node capability + `of_len`.

**API shape (W15 — copy verbatim).**

```teko
/**
 * ChunkNode<T> — one fixed chunk of the unrolled linked list: a fixed `[]T` slots array of capacity
 * `slots.len` plus a `next` link. Growth links a NEW fixed chunk; a chunk is NEVER reallocated or swapped,
 * so an element's address is stable for the node's life and a `u64` handle into the chain never dangles.
 * This is the same shape the arena's own dynamic chunk-list uses at the raw layer (record §4).
 *
 * @since 0.3.1
 */
type ChunkNode<T> = class {
    /** The fixed slot array; `slots.len` is this chunk's capacity, never a live count. */
    intern slots: []T
    /** How many slots of this chunk are live (`used <= slots.len`). */
    intern used: u32
    /** The successor chunk, or null at the tail. */
    intern next: *ChunkNode<T>
}

/**
 * ChunkChain<T> — the growable, thread-safe base sequence: a chain of fixed `ChunkNode<T>` chunks with a
 * head, a tail (O(1) append), a total live `count`, and a lock word / TS-mode for the default thread-safety.
 * It grows by LINKING a new fixed chunk (never a whole-backing swap), so growth is fine-grained, invalidates
 * nothing, and recopies no value — the v1 grow-leak is closed by construction. Iteration is cache-friendly
 * within a chunk. A stable index is a `u64` (chunk ordinal + in-chunk slot). Reclamation is three-category:
 * value slot bucket, class held-by-pointer region-drop, wrapped retain/release. Thread-safety: CAS-append on
 * the tail (default, lock-free readers) or a fine mutex around the structural mutation only; there is NO
 * non-TS variant here — the non-TS escape is the raw `[]T`.
 *
 * @since 0.3.1
 */
exp type ChunkChain<T> = class {
    /** The first chunk, or null when empty. */
    intern head: *ChunkNode<T>
    /** The last chunk, for O(1) append and the CAS/mutex growth point. */
    intern tail: *ChunkNode<T>
    /** The total live element count (NOT any chunk capacity). */
    intern count: u64
    /** The fixed per-chunk capacity chosen at construction; every chunk shares it. */
    intern chunk_cap: u32
    /** The futex lock word for the fine-mutex mode (0 = free); unused under CAS-append. */
    intern lock: u64
    /** The thread-safety strategy chosen at construction (default CasAppend). */
    intern ts_mode: TsMode

    /**
     * Build an empty chain with a fixed per-chunk capacity and a TS mode.
     *
     * @param chunk_cap  the fixed slot count for every chunk (power-of-two recommended for index math)
     * @param ts_mode    CasAppend (default, lock-free readers) or FineMutex
     * @return           a fresh, empty `ChunkChain<T>`
     */
    pub static fn make(chunk_cap: u32, ts_mode: TsMode): ChunkChain<T>

    /** The total live element count. */
    pub fn len(): u64 { self.count }

    /**
     * Append `x` to the tail in amortized O(1), thread-safe: write into the tail chunk's next free slot, or
     * link a NEW fixed chunk when the tail is full — via CAS on the tail (a lost race retries) or the fine
     * mutex around the link only. VALUE places the value once; CLASS/WRAPPED stores the pointer (wrapped
     * also retains). No existing chunk is touched or freed.
     *
     * @param x  the element to append (value placed once, or pointer stored)
     */
    pub fn push(x: T)

    /**
     * Read the element at logical index `i` (`i` in `[0, len())`): walk to chunk `i / chunk_cap`, slot
     * `i % chunk_cap`. VALUE returns an independent copy; CLASS/WRAPPED returns the reference. Never returns
     * an alias into a chunk's storage for a value.
     *
     * @param i  the logical index
     * @return   a value copy or an object reference
     */
    pub fn get(i: u64): T

    /**
     * Drop the last live element by lowering the watermark, O(1): VALUE marks the slot dead (bucket); CLASS
     * is freed by region-drop at the chain's end (conservative GATE-1 default); WRAPPED releases. No chunk is
     * freed mid-life.
     */
    pub fn pop()
}

/**
 * TsMode — the thread-safety strategy of a ChunkChain: CAS-append on the tail (default, lock-free readers) or
 * a fine mutex around structural mutation. There is NO non-TS variant — the non-TS escape is the raw `[]T`.
 *
 * @since 0.3.1
 */
type TsMode = enum { CasAppend, FineMutex }
```

**Fixtures (only non-self-build paths — the chunk-chain is core-consumed once `List`/`Map` migrate, so the
happy path is fixpoint-covered; write only concurrency + the leak boundary the guard alone would not localise):**
- `chunkchain_cas_concurrent_push` — N tasks each push K distinct values; join; the final multiset is exactly
  the union, `len()==N*K` (no lost update — the RMW race is gone). exit 0.
- `chunkchain_reader_during_grow` — one task iterates while another pushes to force chunk links; the reader
  never faults and sees a monotonically consistent prefix (no UAF of a chunk under a reader). exit 0.
- `chunkchain_mutex_mode` — the CAS test rerun with `ts_mode = FineMutex`; identical final multiset. exit 0.

(No `chunkchain_grow_links`/`iterate_order` happy-path fixture — the self-build exercises push/get/iterate the
moment `Map`/`List` migrate; the 6.5 GiB guard + fixpoint is the leak regression.)

**Gate.** `[dry]` — additive; the core does not instantiate `ChunkChain` until Q9/Q10. **Reseed-class:** none
(pure `.tks`, no teaching).

**Deps / why here.** Needs FASE 0 (chunk-node capability, `of_len`). FIRST base — Hash (Q4), Heap (Q6), List
(Q9), Stack (Q13), MultiMap (Q17) reuse it.

### Q3 — the RING base

**Goal.** `Ring<T>` — a fixed array with head/tail wrap, O(1) at BOTH ends, zero shift; the base for
Queue/Deque and the bounded RingBuffer/BlockingCollection.

**Files.** NEW `src/collections/ring.tks`. Pure `.tks`.

**API shape (W15).**

```teko
/**
 * Ring<T> — a bounded double-ended queue over ONE fixed backing with `head`, `tail`, `count`. push/pop at
 * either end is O(1) with wrap-around modulo `slots.len` — ZERO shift. When unbounded and full it grows by
 * allocating a bigger fixed ring and re-linearizing from `head` (elements copied once into the new fixed
 * backing, the old ring's region dropped — F1: never a resize). A bounded ring is fixed-size and never grows.
 * Mid-ring removal is O(n); for mid churn use LinkedList (Q15).
 *
 * @since 0.3.1
 */
exp type Ring<T> = class {
    /** The fixed backing; `slots.len` is the capacity. */
    intern slots: []T
    /** The read cursor (front). */
    intern head: u64
    /** The write cursor (back). */
    intern tail: u64
    /** The live element count (`count <= slots.len`). */
    intern count: u64

    /** Build a ring with a fixed capacity; a bounded ring never grows past `cap`. */
    pub static fn make(cap: u64): Ring<T>
    /** Append at the back in O(1) (wrap); grows if unbounded and full. */
    pub fn push_back(x: T)
    /** Prepend at the front in O(1) (wrap); grows if unbounded and full. */
    pub fn push_front(x: T)
    /** Remove and return the front in O(1); VALUE by DPS + bucket, CLASS/WRAPPED release. */
    pub fn pop_front(): T
    /** Remove and return the back in O(1). */
    pub fn pop_back(): T
}
```

**Fixtures (non-self-build — the compiler does not use Ring, so full fixtures apply, plus concurrency in Q19):**
`ring_both_ends` (interleaved front/back push+pop, order correct, exit 0); `ring_wrap` (fill/drain/refill across
the wrap boundary, exit 0); `ring_bounded_full` (bounded ring at bound rejects/blocks per policy, no grow,
exit 0); `ring_grow_relinearize` (unbounded grow, old region dropped, order preserved from head, exit 0).

**Gate.** `[dry]`. **Reseed-class:** none.

**Deps / why here.** Needs FASE 0 (`of_len`). Independent base; before Queue/Deque (Q14) and RingBuffer (Q19).

### Q4 — the HASH base

**Goal.** `Hash<K,V>` — the hash table base: parallel chunk-chains (keys / cached-hashes / values) + probe;
base for Map/Dictionary/HashSet/Counter/MultiMap/WeakMap.

**Files.** NEW `src/collections/hash.tks`. Pure `.tks`.

**API shape (W15).**

```teko
/**
 * Hash<K,V> — the hash-table base: parallel ChunkChains for keys, cached u64 hashes, and values, sharing one
 * `count`, grown in lockstep by linking a chunk to each (never a whole-backing swap). Lookup is by hash then
 * probe (linear-scan today, matching the embedded impl; open-addressing with a load-factor rehash is the
 * fase1b follow-up — the bucket backing is a fixed index rehashed by rebuild, never a resize). Non-ordered by
 * contract (a hash table is a bag/set) so `remove` may swap-remove in O(1); where insertion order is
 * contractual (`keys()`), `remove` falls back to O(n) shift. Reclamation is three-category per key/value class.
 *
 * @since 0.3.1
 */
exp type Hash<K, V> = class {
    /** The key chain (pointers for class/wrapped keys, inline for word-sized keys). */
    intern keys: ChunkChain<K>
    /** The cached hash of each key, inline u64. */
    intern hashes: ChunkChain<u64>
    /** The value chain, aligned index-for-index with `keys`. */
    intern vals: ChunkChain<V>
    /** The shared live entry count. */
    intern count: u64

    /** Build an empty hash table. */
    pub static fn make(): Hash<K, V>
    /**
     * Insert or update `k -> v`: probe for `k`; if present, update the value in place (VALUE place + old slot
     * bucket; CLASS swap pointer; WRAPPED retain new / release old — count unchanged); else append to the
     * three chains and bump `count`.
     *
     * @param k  the key
     * @param v  the value
     */
    pub fn insert(k: K, v: V)
    /** Look up `k`; the value copy/reference, or the empty variant if absent. */
    pub fn get(k: K): V | null
    /** Remove `k`; swap-remove (O(1), unordered) or shift (O(n), insertion-order contract). */
    pub fn remove(k: K)
}
```

**Fixtures.** None at this stage beyond the collision edge: `hash_collision_probe` (forced hash collisions
probe correctly — a branch the self-build may not stress, exit 0). The happy path is fixpoint-covered once
`Map`/`Dictionary` migrate (Q10).

**Gate.** `[dry]`. **Reseed-class:** none.

**Deps / why here.** Needs **Q1** + FASE 0 `place`. Before Q10.

### Q5 — the ORDERED base

**Goal.** `Ordered<K>` — a node-linked skip-list / balanced BST: sorted iterate + range, O(log n), ZERO shift;
base for SortedSet/SortedDictionary (and Table's range index).

**Files.** NEW `src/collections/ordered.tks` (defines the reusable `Node<T>`/`link_after` if not factored
elsewhere — shared with LinkedList Q15 and the arena chunk-list, record §4).

**API shape (W15).**

```teko
/**
 * Ordered<K> — the ordered base: a node-linked skip-list (or balanced BST) keyed by `K`'s comparison.
 * Insert/remove/lookup are O(log n) with NO backing array to shift — strictly better than an index-array
 * sorted set (which shifts O(n) per ordered insert). Iteration is ascending by traversal. Nodes are
 * individually arena-allocated; reclamation is three-category (value node bucket / class node region-drop /
 * wrapped node release).
 *
 * @since 0.3.1
 */
exp type Ordered<K> = class {
    /** The head tower of the skip-list (or the BST root). */
    intern head: *OrderedNode<K>
    /** The live key count. */
    intern count: u64

    /** Build an empty ordered set. */
    pub static fn make(): Ordered<K>
    /** Insert `k` in order in O(log n); a no-op if already present. */
    pub fn add(k: K)
    /** True iff `k` is present (O(log n)). */
    pub fn contains(k: K): bool
    /** Remove `k` in O(log n); reclamation by class. */
    pub fn remove(k: K)
    /** The inclusive range `[lo, hi]` ascending, as a fresh snapshot. */
    pub fn range(lo: K, hi: K): []K
}
```

**Fixtures (the compiler may not use Ordered → full fixtures):** `ordered_sorted_iter` (insert out of order,
iterate ascending, exit 0); `ordered_range` (range window correct, exit 0); `ordered_remove_no_shift` (O(log n)
unlink, order intact, exit 0).

**Gate.** `[dry]`. **Reseed-class:** none.

**Deps / why here.** Needs FASE 0 (`class` nodes). Independent base; before Q11.

### Q6 — the HEAP base

**Goal.** `Heap<T>` — a binary heap over a chunk-chain, O(log n) push/pop-min; base for PriorityQueue.

**Files.** NEW `src/collections/heap.tks`. Pure `.tks`.

**API shape (W15).**

```teko
/**
 * Heap<T> — a binary min-heap over a ChunkChain indexed as an implicit tree. push appends then sifts up by
 * swapping POINTERS (no value move); pop-min returns the root (VALUE by DPS + bucket, CLASS/WRAPPED release),
 * moves the last to root, sifts down. Growth is the chunk-chain's (link a chunk, never a swap). For
 * merge/insert-heavy loads a pairing heap over nodes (remodel §9.2) is the alternative.
 *
 * @since 0.3.1
 */
exp type Heap<T> = class {
    /** The chunk-chain holding the implicit tree in level order. */
    intern nodes: ChunkChain<T>
    /** The comparison delegate deciding min. */
    intern less: func<T, T, bool>

    /** Build an empty heap with a comparison. */
    pub static fn make(less: func<T, T, bool>): Heap<T>
    /** Insert `x` and sift up in O(log n). */
    pub fn push(x: T)
    /** Remove and return the minimum in O(log n); the empty variant when empty. */
    pub fn pop_min(): T | null
    /** Peek the minimum without removing (O(1)). */
    pub fn peek(): T | null
}
```

**Fixtures (compiler likely does not use Heap → full):** `heap_min_order` (push out of order beyond a chunk,
pop_min ascending, exit 0); `heap_empty_pop` (pop_min on empty → null, exit 0).

**Gate.** `[dry]`. **Reseed-class:** none.

**Deps / why here.** Needs **Q1**. Before Q12.

### Q7 — the BITSET base

**Goal.** `BitSet` — a packed `[]u64` (fixed) dense small-int set; compiler-critical (liveness, reachability,
visited-sets, the escape-check — record §5). Pure `.tks` over `of_len` — teaches nothing.

**Files.** NEW `src/collections/bitset.tks`.

**API shape (W15).**

```teko
/**
 * BitSet — a dense set of small non-negative integers packed into a fixed `[]u64` (bit `i` in word `i/64`,
 * bit `i%64`). set/clear/test are O(1); union/intersect/difference are word-parallel O(words). Growing the
 * universe allocates a NEW fixed word array and copies the words once (F1: never a resize). No per-element
 * allocation, no pointers — the most compact base; serves the compiler's liveness/reachability/visited sets.
 *
 * @since 0.3.1
 */
exp type BitSet = class {
    /** The fixed packed word array; `words.len * 64` is the universe capacity. */
    intern words: []u64
    /** The highest admissible bit index + 1. */
    intern universe: u64

    /** Build a bitset sized for `[0, universe)`. */
    pub static fn make(universe: u64): BitSet
    /** Set bit `i`. */
    pub fn set(i: u64)
    /** Clear bit `i`. */
    pub fn clear(i: u64)
    /** Test bit `i`. */
    pub fn test(i: u64): bool
    /** In-place union with `other`. */
    pub fn union_with(other: BitSet)
    /** In-place intersection with `other`. */
    pub fn intersect_with(other: BitSet)
    /** The population count. */
    pub fn count(): u64
}
```

**Fixtures (full — new module):** `bitset_set_test` (set/clear/test across word boundaries, exit 0);
`bitset_ops` (union/intersect/difference vs a reference model, exit 0); `bitset_popcount` (count matches, exit 0).

**Gate.** `[dry]`. **Reseed-class:** none. **Note:** adopting BitSet to back the escape-check itself is a
SEPARATE core-consumes swap (a future `[fixpoint]`) — out of scope, reported.

**Deps / why here.** Needs FASE 0 (`of_len`). Independent base.

### Q8 — the WEAK-REF wrappers

**Goal.** `Weak<T>` — the pure-`.tks` wrapper over the FASE 0 weak-ref hook; base for WeakMap/WeakSet.

**Files.** NEW `src/collections/weak.tks`. Pure `.tks` over the FASE 0 hook (F0 item 5).

**API shape (W15).**

```teko
/**
 * Weak<T> — a non-retaining reference to a WRAPPED object: it does NOT increment the wrap-refcount, so it
 * never keeps the target alive. `get` upgrades to a live (momentarily retained) reference iff the refcount is
 * still > 0, else the empty variant. The cycle-breaker for wrap-refcount; meaningful ONLY for `wrapped` (value
 * has no identity; a plain class is region-drop, not refcount).
 *
 * @since 0.3.1
 */
exp type Weak<T> = class {
    /** The raw (non-retaining) address of the target in the wrap-refcount table. */
    intern addr: u64

    /** Build a weak reference from a strong wrapped reference (does NOT retain). */
    pub static fn of(strong: T): Weak<T>
    /**
     * Upgrade to a strong reference if the target is still alive.
     *
     * @return the live reference (retained), or null if the target is gone
     */
    pub fn get(): T | null
}
```

**Fixtures.** Covered by FASE 0's `weak_dead`/`weak_cycle_break` (the hook) + one wrapper fixture `weak_alive`
(target strongly held: `get()` returns it, exit 0).

**Gate.** `[dry]`. **Reseed-class:** none.

**Deps / why here.** Needs FASE 0 (weak hook, `wrapped`). Before Q18.

### Q9 — convert `List<T>` to chunk-chain + TS + three-category (build side)

**Goal.** Re-back `List<T>` on `ChunkChain<T>` with the three-category element model, replacing the value
copy-grow `push` (`src/collections/list.tks:2-3,15` — the leak). The REMOVAL of the old `teko::list::grow`/
`push` root is FASE 2; here we BUILD the replacement and prove it (build-first).

**Files.** `src/collections/list.tks` (rewrite the type body over `ChunkChain<T>`). The `arr_*` combinators
(`collections.tks:2-92`) become presize-exact (remodel §3.5) as a sub-step. `src/list/list.tks` is left for
FASE 2.

**API shape (W15 — the settled List).**

```teko
/**
 * List<T> — a growable, reference-semantic sequence backed by a ChunkChain<T> (TS-by-default). Append +
 * iteration is the dominant use, so the cache-friendly chunk-chain wins; push/pop at the end are amortized
 * O(1). Positional remove/insert in the middle is O(n) (an explicit trade; use LinkedList for middle churn).
 * Elements are three-category: value copied once + bucket; class held by pointer, freed by region-drop via
 * escape-analysis (the list is a holder raising residence); wrapped retained/released. `to_array` is a fresh
 * snapshot, never a view over the chain.
 *
 * @since 0.3.1
 */
exp type List<T> = class {
    /** The chunk-chain backing (growable + thread-safe). */
    intern items: ChunkChain<T>

    /** Build an empty list. */
    pub static fn make(): List<T> { .{ items = ChunkChain<T>::make(64, TsMode::CasAppend) } }
    /** The live element count. */
    pub fn len(): u64 { self.items.len() }
    /** True iff empty. */
    pub fn is_empty(): bool { self.items.len() == 0 }
    /** Append `x` (amortized O(1), TS by default). */
    pub fn push(x: T) { self.items.push(x) }
    /** Read the element at `i` (value copy / object reference). */
    pub fn get(i: u64): T { self.items.get(i) }
    /** Overwrite the element at `i` (O(1)); a no-op if `i >= len()`. */
    pub fn set(i: u64, x: T)
    /** Remove the last element (O(1)); value bucket / class region-drop / wrapped release. */
    pub fn pop() { self.items.pop() }
    /** Remove the element at `i`, preserving order (O(n) shift). */
    pub fn remove_at(i: u64)
    /** A fresh `[]T` snapshot of the live elements (never a view over the chain). */
    pub fn to_array(): []T
}
```

**Fixtures (self-build exercises push/get/pop/to_array once the compiler's own `List` uses build on this — so
NO happy-path fixture; write only what the self-build does not hit):**
- `list_class_region_drop` — a `class` held ONLY by the list, removed early: freed by region-drop at the list's
  drop under the conservative GATE-1 default; no premature free, no leak. (GATE-1 boundary the compiler may not
  reach.) exit 0.
- (concurrency comes from `chunkchain_*` in Q1 — no duplicate here.)

**Gate.** `[fixpoint]` — `List` is used across the compiler; build gen2 native, the fixture green, FIXPOINT
gen2==gen3. Teaches nothing (pure `.tks`). **Reseed-class:** fixpoint rebuild (no new seed language).

**Deps / why here.** Needs **Q1** + FASE 0. FIRST conversion — simplest; hardens the substrate before Map.
GATE-1 note: conservative default, not blocked.

### Q10 — convert `Map<V>` / `Dictionary<K,V>` / `HashSet<T>` to the Hash base

**Goal.** Re-back the three hash collections on `Hash<K,V>` (Q4), replacing the parallel-array
`teko::list::push`/`arr_drop_at` (`map.tks:31-33,47-49`, `dictionary.tks:31-33,47-49`, `hashset.tks:21-22,
35-36`).

**Files.** `src/collections/map.tks`, `dictionary.tks`, `hashset.tks`.

**API shape.** Each wraps a `Hash<K,V>` (HashSet: `Hash<T, unit>`), delegating `insert`/`get`/`remove`/`len`.
`Dictionary.keys()` keeps its insertion-order contract (`dictionary.tks:53`) → `remove` uses the O(n) shift
path, not swap-remove (remodel T-5). W15 doc-comments per Q9.

**Fixtures (Map is core-consumed → self-build covers happy path; write only the non-exercised):**
- `dict_class_value_ref` — a `class` value: `get` returns the reference (mutation reflects), removal region-drops
  it. (Class path the compiler's `teko::env` string-keyed map may not exercise.) exit 0.
- `hashset_add_dup` — duplicate add is a no-op (a boundary the self-build may not stress). exit 0.

**Gate.** `[fixpoint]` — `Map` IS consumed by `teko::env` (`plano-collections-…:762`). Build gen2 native, the
fixtures green, FIXPOINT gen2==gen3. **Reseed-class:** fixpoint rebuild (no teaching).

**Deps / why here.** Needs **Q4**, **Q9**. The core-consuming conversion, right after List hardens the base.

### Q11 — convert `SortedSet<T>` / `SortedDictionary<K,V>` to the Ordered base

**Goal.** Re-back the sorted collections on `Ordered<K>` (Q5), replacing the shift-insert (`sorted_set.tks:16`
`sorted_insert`→`arr_insert_at`; `sorted_dictionary.tks:30-31`) with O(log n) node insert.

**Files.** `src/collections/sorted_set.tks`, `sorted_dictionary.tks`; `sorted_insert` left in `collections.tks`
for FASE 2.

**API shape.** `SortedSet<T>` wraps `Ordered<T>`; `SortedDictionary<K,V>` wraps `Ordered<K>` + a parallel value
store on the same node. W15 per Q9.

**Fixtures.** Only if the compiler does not consume these (likely not) → full: `sortedset_shift_order` (insert
out of order beyond a node-block, iterate ascending, exit 0); `sorteddict_shift_pair` (keys+vals aligned, `get`
matches, exit 0). If a sorted collection IS core-consumed, drop these to fixpoint coverage.

**Gate.** `[dry]` if not core-consumed; `[fixpoint]` if it is (remodel §7: "na dúvida, RITUAL" → here, prefer
`[fixpoint]` if any doubt). **Reseed-class:** none / fixpoint.

**Deps / why here.** Needs **Q5**. After Q10. GATE-1 note on the class remove path.

### Q12 — convert `PriorityQueue<T>` to the Heap base

**Goal.** Re-back `PriorityQueue<T>` on `Heap<T>` (Q6), replacing `heap_sift_up`/`heap_pop_min` over
`teko::list::push` (`priority_queue.tks:16-17,24-28`; `collections.tks:108-145`).

**Files.** `src/collections/priority_queue.tks`; the heap helpers in `collections.tks` left for FASE 2.

**API shape.** `PriorityQueue<T>` wraps `Heap<T>`; `enqueue`→`push`, `dequeue`→`pop_min`, `peek`→`peek`. W15.

**Fixtures.** If not core-consumed → full: `pq_heap_fixed` (enqueue beyond a chunk, dequeue ascending, exit 0).
Else fixpoint-covered.

**Gate.** `[dry]` / `[fixpoint]` (as Q11). **Reseed-class:** none / fixpoint.

**Deps / why here.** Needs **Q6**. Completes the 7 embedded conversions.

### Q13–Q19 — the compositions / new collections (all pure `.tks`, `[dry]`, no reseed)

The compiler does NOT consume these, so each gets FULL fixtures (the self-build never touches them) and lands
`[dry]` with no reseed.

- **Q13 `Stack<T>`** — NEW `src/collections/stack.tks`, wraps `ChunkChain<T>` (LIFO at the tail, O(1)).
  Fixtures: `stack_lifo` (push 1..N, pop N..1, exit 0); `stack_empty_pop` (pop empty → null, exit 0). Deps Q1.
- **Q14 `Queue<T>`/`Deque<T>`** — NEW `src/collections/{queue,deque}.tks`, wrap `Ring<T>` (O(1) both ends).
  Fixtures: `queue_fifo` (exit 0); `deque_both_ends` (exit 0); `deque_wrap_grow` (exit 0). Deps Q3.
- **Q15 `LinkedList<T>`** — NEW `src/collections/linked_list.tks`, doubly-linked nodes (O(1) anywhere given the
  node; shares `Node`/`link_after` with Q5 + the arena chunk-list). Fixtures: `linked_list_no_backing`
  (no backing array; order+len correct, exit 0); `linked_remove_node` (value node bucket / class node
  region-drop, exit 0). Deps FASE 0 + Q5-node. GATE-1 note on the class node remove path.
- **Q16 `Counter<T>`/`MultiSet<T>`** — NEW `src/collections/counter.tks`, wraps `Dictionary<T,u64>`. Fixtures:
  `counter_add` (counts correct, exit 0); `counter_most_common` (top-n order, exit 0). Deps Q10.
- **Q17 `MultiMap<K,V>`** — NEW `src/collections/multimap.tks`, wraps `Dictionary<K, List<V>>`. Fixtures:
  `multimap_multi` (all values per key, exit 0); `multimap_remove_one` (leaves the rest, exit 0). Deps Q10, Q9.
- **Q18 `WeakMap<K,V>`/`WeakSet<T>`** — NEW `src/collections/weakmap.tks`, wraps `Hash<Weak<K>,V>` with a
  get-time liveness prune. Fixtures: `weakmap_live_key` (exit 0); `weakmap_dead_key` (collected key pruned, no
  UAF, exit 0). Deps Q4, Q8.
- **Q19 `RingBuffer<T>`/`BlockingCollection<T>`** — NEW `src/collections/ringbuffer.tks`, bounded `Ring<T>` +
  the condvar (`sync.tks` `cv_wait`/`cv_signal` `:78-93`). Fixtures (concurrency — mandatory): `ringbuffer_bounded`
  (fill/over/drain FIFO, exit 0); `blocking_producer_consumer` (N producers + M consumers, each item delivered
  once, exit 0). Deps Q3 + `sync.tks`.

All Q13–Q19: **Gate `[dry]`, Reseed-class none.**

### Q20 — `Table<…>` core (fase1c) — chunk-chain rows + Map/SortedSet indices + atomic multi-index txn

**Goal.** The multi-index in-memory table as a COMPOSITION: chunk-chain row store + `RowId u64` stable handles
+ Map (hash index) + SortedSet (range index), plus the ONE novel capability — the atomic multi-index
transaction (insert/update/delete mutates the row + all index entries atomically, TS via the fine mutex + a
single-store commit-point watermark as the linearization point). Columns ≤16 → a comptime `Table1..Table16`
family.

**Files.** NEW `src/collections/table.tks`. Composes Q1 (rows), Q4 (Map index), Q5 (SortedSet index). Pure
`.tks` (generics machinery is landed).

**API shape.** Copy verbatim from `table-collection-sql-linq-0.3.1.md:114-347`: `RowId`, `RowStore<Row>`,
`Index<K>`, `Table<Row>`, and `insert`/`update`/`delete` (the atomic txn). Do NOT re-derive.

**Fixtures (compiler does not consume Table → full, incl. the error branch + concurrency):**
`table_insert_lookup` (hash-index point lookup, exit 0); `table_range_index` (range query, exit 0);
`table_atomic_txn` (a FAILING update rolls back ALL index entries — the error branch, exit 0);
`table_rowid_stable` (RowId survives store growth, exit 0); `table_16_cols` (a 16-column table type-checks and
round-trips, exit 0).

**Gate.** `[dry]` — additive, off the critical path (run when build capacity is free to avoid heavy parallel
builds with §16, `mudancas-superficie-0.3.1.md:1630-1631`). **Reseed-class:** none.

**Deps / why here.** Needs **Q1**, **Q4**, **Q5**. LAST major feature; deferred fase1c. NOT blocked by GATE-2.

### Q-Query — `Table` query surface (LINQ-typed) — **BLOCKED on GATE-2**

**Goal.** The typed query surface (`table-collection-sql-linq-0.3.1.md:349+`): lazy `Query<Row>`/`Cursor<Row>`/
`Predicate`/`Aggregate`/`IndexHint` from typed `func<…>` delegates.

**Files.** NEW `src/collections/table_query.tks`.

**API shape.** Copy `Query<Row>` + the auxiliaries verbatim from the Table doc (W15 already written there).
Architect recommendation (record §8, Table doc §3.1): **LINQ-typed PRIMARY; SQL-string an OPTIONAL later thin
front-end that desugars to the LINQ chain** — law-aligned (Teko is monomorphized/typed; a string front-end
needs a dynamic `Value` model the type system exists to avoid).

**Gate.** `[dry]`. **Reseed-class:** none. **BLOCKED on GATE-2** — code waits on the owner ratifying LINQ vs
SQL; the recommendation is law-first, so no HALT, but do not implement until ratified.

**Deps / why here.** Needs **Q20** + GATE-2.

### Q-File — `FileTable<Row>` (io-streaming backed)

**Goal.** The optional binary single-table store over io-streaming: rebuild-indices-on-load, stream (not mmap),
explicit save (`table-collection-sql-linq-0.3.1.md:156`).

**Files.** NEW `src/collections/file_table.tks`; reads the io-streaming `FileStream`/helpers.

**API shape.** `FileTable<Row>` = `Table<Row>` + `save(path)` (stream rows) + `load(path)` (stream + rebuild
indices). W15.

**Fixtures.** `filetable_roundtrip` (save then load reproduces rows + working indices, exit 0);
`filetable_rebuild_index` (indices rebuilt on load, exit 0).

**Gate.** `[dry]`. **Reseed-class:** none.

**Deps / why here.** Needs **Q20** + the io-streaming crumbs 1–9 (`io-streaming-0.3.1.md:348-384`). Very last;
NOT blocked by GATE-2.

---

## 4. FASE 2 — THE EXPURGO, ONCE (the single expurgo reseed)

**F2-Expurgo — clean removal of every de-taught construct.** After FASE 1 has migrated EVERY caller (verified
by the self-compile enumerating references), one consolidated step REMOVES the old growable machinery. This is
the second and last teaching/expurgo reseed.

**Removed (all together, one reseed):**
- `teko::list::push` / `teko::list::grow` — the value copy-grow choke points (`src/list/list.tks:1-3`,
  `src/collections/collections.tks` push-based combinators). Delete the file/functions.
- The single-growable-array surface in the typer — `empty` / `push` / `with_cap` / `grow_inplace`
  (`typer.tks:805-835`) — and any lexer/parser tokens or checker rules that recognised them.
- The now-dead `arr_*` push-based helpers and `sorted_insert`/`heap_sift_up`/`heap_pop_min` left behind by
  Q9/Q11/Q12 (their replacements are the chunk-chain/ordered/heap bases).

**EXPURGO discipline (owner ruling 3 — NOT a tombstone).** The removed constructs must become simply
**unrecognised** — the lexer no longer tokenises them, the parser has no production for them, the checker has
no rule and emits NO special diagnostic. There is NO "`push` was removed / no longer exists" error: an author
writing the old form gets the SAME generic "unknown symbol / unexpected token" a never-existent name would get,
as if the construct never existed. Do NOT add a deprecation shim, a tombstone rule, or a migration hint. Clean
lexer + parser + checker.

**Fixtures.** `expurgo_unknown_not_tombstone` — a `.tkr` that uses the old `teko::list::push(...)` form and
asserts the diagnostic is the GENERIC unknown-symbol error (a specific exit token), NOT a bespoke "removed"
message; and that a program using the NEW surface still compiles. (This is the one path the self-build cannot
assert — the self-build no longer contains the old form.) exit 0.

**Gate.** `[RITUAL]` — the expurgo reseed. Build gen2 native (the whole corpus now uses only the new surface),
FIXPOINT gen2==gen3, reseed (the new seed no longer understands the removed surface). **Reseed-class:** EXPURGO
(the only one on the back). The self-compile enumerates any surviving reference as the roots drop — if any
appears, a FASE 1 migration was incomplete; fix it before this reseed.

**Deps / why here.** Depends on ALL of FASE 1 (every caller migrated). LAST, alone. Cannot precede migration
(build-first).

---

## 5. The queue at a glance (phase · gate · reseed · deps · flags)

| phase / # | item | gate | reseed-class | deps | flags |
|---|---|---|---|---|---|
| **FASE 0** | teach ALL new surface (of_len/place/read/write+bucket, class-holder escape, wrapped retain/release, weak hook, deep_copy, chunk-node cap, CAS helper) | **[RITUAL]** | **TEACHING (1)** | reducao-memoria P0.of_len | — |
| Q1 | ChunkChain base | [dry] | none | FASE 0 | — |
| Q3 | Ring base | [dry] | none | FASE 0 | — |
| Q4 | Hash base | [dry] | none | Q1 | — |
| Q5 | Ordered base (+Node) | [dry] | none | FASE 0 | — |
| Q6 | Heap base | [dry] | none | Q1 | — |
| Q7 | BitSet base | [dry] | none | FASE 0 | — |
| Q8 | weak wrappers | [dry] | none | FASE 0 | — |
| Q9 | List → chunk-chain | **[fixpoint]** | fixpoint rebuild | Q1, FASE 0 | GATE-1 (opt only) |
| Q10 | Map/Dictionary/HashSet → Hash | **[fixpoint]** | fixpoint rebuild (Map core-consumed) | Q4, Q9 | GATE-1 (opt only) |
| Q11 | SortedSet/SortedDictionary → Ordered | [dry]/[fixpoint] | none / fixpoint | Q5, Q10 | GATE-1 (opt only) |
| Q12 | PriorityQueue → Heap | [dry]/[fixpoint] | none / fixpoint | Q6, Q11 | GATE-1 (opt only) |
| Q13 | Stack | [dry] | none | Q1 | — |
| Q14 | Queue/Deque | [dry] | none | Q3 | — |
| Q15 | LinkedList | [dry] | none | FASE 0, Q5-node | GATE-1 (opt only) |
| Q16 | Counter/MultiSet | [dry] | none | Q10 | — |
| Q17 | MultiMap | [dry] | none | Q10, Q9 | — |
| Q18 | WeakMap/WeakSet | [dry] | none | Q4, Q8 | — |
| Q19 | RingBuffer/BlockingCollection | [dry] | none | Q3, sync.tks | — |
| Q20 | Table core (fase1c) | [dry] | none | Q1, Q4, Q5 | — |
| Q-Query | Table query (LINQ) | [dry] | none | Q20 | **GATE-2** |
| Q-File | FileTable | [dry] | none | Q20, io crumbs | — |
| **FASE 2** | expurgo (remove push/single-array/empty/with_cap/grow_inplace; clean lexer+parser+checker, NO tombstone) | **[RITUAL]** | **EXPURGO (1)** | ALL of FASE 1 | — |

**Reseed budget.** Exactly **2 teaching/expurgo reseeds** (FASE 0 + FASE 2), plus the FASE 1 `[fixpoint]`
rebuilds where the core consumes a swap (Q9, Q10, and conditionally Q11/Q12) — those teach nothing. Everything
else is `[dry]`, zero reseed. The `[fixpoint]` and `[RITUAL]` items are the RESEED-SERIAL spine: dispatch them
strictly one at a time; the `[dry]` bases/compositions may be authored in parallel in principle but the
coordinator serialises the dispatch and may interleave a `[dry]` item between two rebuilds only when its deps
are green.

**GATE-1 touches FASE 0 (the residence rule), Q9, Q10, Q11, Q12, Q15** — only the class-early-remove eager-free
optimization; each ships now with the conservative region-drop-via-escape default, so none is blocked. When
GATE-1 closes toward promote-to-wrapped, add one `[dry]` follow-up per collection.

**GATE-2 blocks ONLY Q-Query.** Q20 and Q-File proceed. The recommendation is LINQ-typed primary (law-aligned);
Q-Query's code waits on ratification, not a HALT.

---

## 6. Risks + law tensions (all law-first; no HALT)

- **T-A — reseed minimisation vs build-first (the two-reseed floor).** Teaching (add) and expurgo (remove)
  cannot be ONE reseed: build-first forbids removing `push` while the library still calls it. So the minimum is
  2 teaching/expurgo reseeds bracketing the pure-Teko library — this queue hits that floor. Recorded, not a
  tension.
- **T-B — the substrate change is the biggest edit.** The remodel's central pointer-index model is demoted to
  intra-chunk layout (its §0.0). Risk: an implementer reintroduces the banned single-array swap (RMW race).
  Mitigation: Q1's `push` links a chunk and NEVER frees one on grow; the pointer-index `grow_index` appears
  only where the non-TS `[]T` or the intra-chunk layout is explicit. Not a HALT.
- **T-C — class vs wrapped reclamation (GATE-1).** Resolved conservatively: FASE 0 teaches region-drop-via-
  escape holder semantics now; promote-to-wrapped is an additive follow-up. Leak-safe, never UAF. Not a HALT.
- **T-D — Map is core-consumed.** Q10 rides a `[fixpoint]` rebuild (no teaching) after Q9 hardens the substrate.
  Sequencing, not a tension.
- **T-E — the 6.5 GiB build guard.** Q9/Q10 rebuild the compiler on the new substrate; if a chunk-chain
  monomorph inflates the build, find that item's root cause (chunk_cap / Doc-1 presizing), never raise the
  ceiling. Inviolable.
- **T-F — expurgo cleanliness.** A lingering caller after FASE 1 would surface at the FASE 2 build as a generic
  unknown-symbol error (correct — no tombstone); that is the signal a migration was incomplete, fixed before
  the expurgo reseed. The `expurgo_unknown_not_tombstone` fixture guards the no-tombstone rule. Not a tension.
- **T-G — GATE-2 (LINQ vs SQL).** The recommendation is law-first (LINQ-typed; a string front-end needs the
  dynamic `Value` model the type system exists to avoid). Q-Query waits on ratification, not a HALT.

No genuine unresolved tension remains: the model is settled (record + Doc-2), the two owner-open points are
GATES on specific items (each with a law-first default that lets the rest proceed), and every item has a
concrete shape, non-redundant fixtures, gate, and dependency. **No HALT.**
