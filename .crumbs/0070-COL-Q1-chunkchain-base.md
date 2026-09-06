---
seq: 0070
crumb-id: COL-Q1
milestone: M2
gate: "[dry]"
reseed-class: "none"
deps: [COL-F0a]
sources:
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:173-290"   # Q1 — the chunk-chain base (full W15 shape)
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:27-30"     # §0 the settled model: bases + TS default + three-category
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:136-138"   # FASE 0 chunk-node capability from the arena
---

# 0070 · COL-Q1 — ChunkChain base (growable + TS substrate)

> `ChunkChain<T>` — the unrolled linked list of FIXED chunks that is the growable, thread-safe, cache-friendly,
> stable-index substrate every default collection sits on; the FIRST base, reused by Hash/Heap/List/Stack/MultiMap.

## Goal

The settled collections model (`colecoes-memoria-fila…` §0) makes the **chunk-chain** — an unrolled linked list
of fixed chunks — the growable, thread-safe substrate under every default collection (record §2). It grows by
LINKING a new fixed chunk (never a whole-backing swap), so growth is fine-grained, invalidates nothing, and
recopies no value — the v1 grow-leak is closed by construction. A stable index is a `u64` (chunk ordinal +
in-chunk slot); an element's address is stable for its node's life. Thread-safety is DEFAULT: CAS-append on the
tail (lock-free readers) or a fine mutex around the structural mutation only — there is NO non-TS variant here,
the non-TS escape is the raw `[]T`. Reclamation is three-category (value slot bucket / class region-drop /
wrapped retain-release). This crumb adds `ChunkChain<T>` + `ChunkNode<T>` + `TsMode` in a NEW pure-`.tks` file
over the FASE 0 chunk-node capability + `of_len`. It is **additive / feature-gated-inert**: the core does not
instantiate `ChunkChain` until `List`/`Map` migrate (Q9/Q10), so it changes no emitted byte today — a `[dry]`
leaf, reseed-class `none`, teaching nothing.

Not blocked by any open dependency (its dep COL-F0a is in this wave, `0021`); this is executable design.

## Where

- NEW `src/collections/chunk_chain.tks` — defines `ChunkChain<T>`, `ChunkNode<T>`, `TsMode`; pure `.tks` over
  the FASE 0 chunk-node capability (COL-F0a) + `of_len`.
- NO existing collection touched — the migrations of `List`/`Map` onto `ChunkChain` are Q9 (`0077`) / Q10
  (`0078`), NOT this crumb.
- Reuses COL-F0a's `of_len`/`place`/`read`/`write`/`bucket` fixed-backing intrinsics for the per-chunk slot
  array and the chunk-node link capability.

## How

Create `src/collections/chunk_chain.tks` with the shapes below, copied VERBATIM from `colecoes-memoria-fila…`
Q1 (already in W15 full-Javadoc form):

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

Implementation notes: `make` allocates the chain header (no chunk until first push); `push` writes into the
tail's next free slot via COL-F0a `place`/`write` or links a fresh `of_len(chunk_cap)` chunk under CAS/mutex;
`get` walks `i/chunk_cap` chunks then reads slot `i%chunk_cap`; `pop` lowers the watermark and buckets the
slot. No whole-backing swap anywhere.

## Rulings & laws

- **Teko-only:** the base lands in NEW `src/collections/chunk_chain.tks` (`.tks`); no C twin.
- **W15 full Javadoc** on every declaration (type/member/fn, pub + private); flatten/extract; no inline `//` —
  the shapes above are already W15-conformant, copy verbatim.
- **Removals = clean expurgo, NO tombstone:** additive — removes no surface (the v1 `push`/`grow` retire is the
  M3 consolidated expurgo `0093` COL-F2, not this crumb).
- **Three-category memory law (Doc-2, `colecoes-memoria-fila…` §0):** VALUE = place-once + slot bucket; CLASS =
  region-drop via escape; WRAPPED = retain/release. `push`/`get`/`pop` honor the element's category.
- **TS is DEFAULT, single-growable BANNED (`colecoes-memoria-fila…` §0):** CAS-append or fine mutex; the RMW
  race of a single growable array is forbidden — the chunk-link growth is race-free by construction.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744` cap — a blown guard is a
  root-cause fix, never a raised ceiling; commit the green step; **reseed ONLY at a [RITUAL]** — this is a
  `[dry]` leaf, reseed-class `none`, NO reseed; the trivial fixpoint (additive, core does not instantiate) is
  the proof; sweep `.tkt`/`.tkr` after the new type is added.

## Fixtures

The chunk-chain is core-consumed once `List`/`Map` migrate (Q9/Q10), so the happy path (push/get/iterate) is
fixpoint-covered THEN; write only the concurrency + leak-boundary oracles the guard alone would not localise
(`colecoes-memoria-fila…` 275-284):

| fixture | asserts | expected |
|---|---|---|
| `chunkchain_cas_concurrent_push` | N tasks each push K distinct values; join; the final multiset is exactly the union, `len()==N*K` (no lost update — the RMW race is gone) | `0` |
| `chunkchain_reader_during_grow` | one task iterates while another pushes to force chunk links; the reader never faults and sees a monotonically consistent prefix (no UAF of a chunk under a reader) | `0` |
| `chunkchain_mutex_mode` | the CAS test rerun with `ts_mode = FineMutex`; identical final multiset | `0` |

(No `chunkchain_grow_links`/`iterate_order` happy-path fixture — the self-build exercises push/get/iterate the
moment `Map`/`List` migrate; the 6.5 GiB guard + fixpoint is the leak regression.)

## Gate

`[dry]` — compile + the concurrency/leak fixtures + the trivial fixpoint (additive; the core does not
instantiate `ChunkChain` until Q9/Q10, so no emitted-byte change). "Green" = `ChunkChain`/`ChunkNode`/`TsMode`
compile, the CAS/mutex concurrency fixtures exit `0` with the exact union multiset, and no emitted byte changes.
**Reseed-class:** `none` (pure `.tks`, no teaching).

## Deps

`COL-F0a` (`0021` — the `of_len` + `place`/`read`/`write`/`bucket` fixed-backing intrinsics + the chunk-node
capability the chain links).

## Done when

`ChunkChain<T>` (with `ChunkNode<T>`/`TsMode`) compiles as a pure-`.tks` base that grows by linking fixed
chunks (never a swap), the CAS/mutex concurrency fixtures exit `0` with no lost update, and a `[dry]` build is
byte-identical (the core does not yet instantiate it).
