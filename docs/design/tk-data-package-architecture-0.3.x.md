---
section: design
created: 2026-08-20
status: DESIGN-AHEAD — forward-looking architecture ONLY. NOT 0.3.1 execution. This doc IS the
        deliverable: no `.tks`, no build, no reseed, no `teko test`, NO CRUMBS. Implementation is gated
        POST-memory-stable (dry build ≤ 1.5 GB) AND POST-package-mode (`kind = "package"` consumable end
        to end). Nothing here is dispatched to an implementer until both gates close.
owner-ruling: 2026-08-20 — the specialized data structures ship as ONE dedicated INDEPENDENT PACKAGE named
        `tk_data` (NOT `tk_storage`, NOT `teko::collections::specialized`), with TWO honest sub-namespaces:
        `tk_data::storage` (on-disk engine) and `tk_data::collections` (specialized in-memory structures).
        The everyday BASE collections the compiler itself consumes stay in the monolith (`teko::collections`).
source: colecoes-e-memoria-modelo-unificado-0.3.1.md (the settled unified collections + memory model),
        colecoes-memoria-fila-implementacao-0.3.1.md (the base/composition library queue), arena-em-teko.md
        (the arena in Teko), table-collection-sql-linq-0.3.1.md (Table core = the in-mem multi-index seam),
        TEKO_ROADMAP_PACKAGES.md (PK0–PK8 package ecosystem), TEKO_ROADMAP_DB.md (the DB surface),
        TEKO_ROADMAP_STDLIB_CORE.md (`teko::io` streaming), package-manager.md §5 (artifact kinds by role),
        io-streaming-0.3.1.md (on-disk streaming feed).
frozen: bootstrap/teko.c + the C twins are OUTPUT/FROZEN; all new product work is `.tks` only. tk_data is a
        pure-`.tks` package over the monolith's taught surface.
---

# `tk_data` — the independent data-structures package (architecture, 0.3.x design-ahead)

The owner's first big projects are a multi-paradigm database (graph/RDF-based, speaks SQL, 100% ACID), a
bare-metal kernel, and possibly an OS. The database is the first big **PACKAGE** (a side-car, not part of
the compiler binary). This document decides the seam under it: the specialized data structures the DB is
built from ship as **one independent package, `tk_data`**, so the monolith stays lean and the ≤ 1.5 GB dry
build memory gate is helped rather than hurt.

`tk_data` carries only the SPECIALIZED / advanced structures. It depends on the monolith's base collections,
memory model, arena, io-streaming, and concurrency substrate — it does not re-invent them. The DB package
depends on `tk_data`; it never reaches past it into the monolith's raw layers for storage concerns.

---

## 0. The one-breath summary

- **Package:** `tk_data`, `[artifact] kind = "package"` → distributed as a `.tkl`, **statically integrated,
  monomorphized-by-use** into whoever imports it (package-manager.md §5.1 — there is no shared Teko dep).
- **Two sub-namespaces, honestly named** (owner ruling): `tk_data::storage` (on-disk STORAGE-ENGINE
  components — not "collections") and `tk_data::collections` (specialized IN-MEMORY structures).
- **Boundary:** monolith `teko::` (base collections + memory model + arena + io + concurrency) → `tk_data`
  (specialized structures) → DB package (graph/RDF/SQL/ACID).
- **The master composition:** `tk_data::storage::lsm` = SSTable + WAL + Bloom filter + memtable
  (a `SkipList` or an in-mem `BPlusTree` from `tk_data::collections`) + compaction.
- **The DB seam:** `tk_data` hands the DB durability (WAL), atomic multi-index publish, epoch-pinned
  snapshots (the MVCC substrate), and page/extent management — everything BELOW the SQL/graph/planner layer.
- **Timing:** design-ahead only. Implementation waits for the memory gate (§9) and package mode (§9). **No
  crumbs are produced here** (§9).

---

## 1. The three-layer boundary

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ MONOLITH  teko::   (the compiler binary; must fit the ≤1.5 GB dry-build gate) │
│                                                                               │
│  memory model (3-category: value / class / wrapped)   arena  region_alloc/drop│
│  COL-F0 fixed-backing intrinsics  of_len place read write bucket              │
│  COL-Q bases   ChunkChain · Ring · Hash · Ordered · Heap · BitSet · weak-ref  │
│  teko::collections  List Map Dictionary Set HashSet Queue Deque Stack         │
│                     LinkedList PriorityQueue SortedSet SortedDictionary        │
│                     Counter/Multiset MultiMap WeakMap/WeakSet RingBuffer       │
│                     Table (in-mem multi-index)   ← the compiler consumes these │
│  teko::io  Reader Writer Seeker Closer + BufReader/BufWriter                   │
│  teko::fs  open create fsync rename   sync.tks  fine-mutex + CAS   teko::math  │
└───────────────────────────────────────────┬───────────────────────────────────┘
                                             │ static, mono-by-use (.tkl)
┌───────────────────────────────────────────▼───────────────────────────────────┐
│ PACKAGE  tk_data   (specialized data structures — side-car, off the crit path) │
│                                                                                │
│  tk_data::collections   SkipList RedBlackTree Trie BloomFilter RTree GiST GIN  │
│                         BTree BPlusTree            (specialized IN-MEMORY)      │
│  tk_data::storage       WAL SSTable LSMTree Inode Extent Pager                 │
│                         persistent-BPlusTree InvertedIndex    (ON-DISK ENGINE) │
└───────────────────────────────────────────┬───────────────────────────────────┘
                                             │ static, mono-by-use (.tkl)
┌───────────────────────────────────────────▼───────────────────────────────────┐
│ PACKAGE  DB (working name)   depends on tk_data                                │
│  graph / RDF model · SQL parse+plan+execute · ACID txn manager (MVCC + WAL)    │
│  catalog · query optimizer · storage layout over tk_data::storage             │
└───────────────────────────────────────────────────────────────────────────────┘
```

**Why the split is law-grounded, not taste.**

- **≤ 1.5 GB dry-build gate (CLAUDE.md, owner 2026-08-19).** Every specialized structure that stays in the
  monolith is compiled into the compiler and counts against the gate for zero self-host benefit (the
  compiler never range-queries an R-Tree). Moving them to a package is on-demand: a binary "pays only for
  what it imports" (TEKO_ROADMAP_PACKAGES.md PK8) — the compiler imports none of `tk_data`, so it costs the
  compiler nothing.
- **Base collections CANNOT be a package (owner ruling, and it is forced).** `src/` uses `List`/`Map`/`Set`
  directly; the compiler is `kind = "binary"` and cannot take a `.tkl` dependency on structures it needs to
  compile that very `.tkl`. Base collections are therefore monolith PRIMITIVES, not a package. `tk_data`
  reuses them; it does not contain them (see the open question in §11 before assuming otherwise).
- **Honest namespacing (owner ruling).** WAL / SSTable / LSM are storage-engine components, not
  "collections". They live under `tk_data::storage::…`, never `teko::collections::specialized`.

---

## 2. Boundary decisions — the precise placement of every structure

Read this table as the ratified split. "Monolith PRIMITIVE" = compiled into the compiler, in `src/`.
"`tk_data::…`" = the package. "DB" = the database package above `tk_data`.

| structure | placement | why here (law-first) |
|---|---|---|
| arena, `region_alloc`/`region_drop`, 3-category memory model | **monolith PRIMITIVE** (`src/runtime/arena.tks`) | the compiler's own allocation substrate; already built |
| COL-F0 `of_len`/`place`/`read`/`write`/`bucket` | **monolith PRIMITIVE** (checker/codegen, FASE-0 taught) | the VALUE regime every collection rests on; taught once |
| COL-Q bases: ChunkChain, Ring, Hash, Ordered, Heap, BitSet, weak-ref | **monolith PRIMITIVE** (`src/collections/…`) | the substrate the base collections AND `tk_data` both reuse |
| List, Map, Dictionary, Set, HashSet, Queue, Deque, Stack, LinkedList, PriorityQueue, SortedSet, SortedDictionary, Counter, MultiMap, WeakMap/WeakSet, RingBuffer | **monolith** `teko::collections` | the compiler consumes them (self-build is their test); cannot be an external package for internal use |
| Table (in-mem multi-index) | **monolith** `teko::collections` (already `src/collections/table.tks`) | pure composition of monolith bases + the atomic multi-index txn; the in-mem seam the DB and `tk_data` both build on. Placement flagged in §11 |
| **SkipList** | **`tk_data::collections`** | specialized ordered structure; an LSM memtable + a concurrent ordered map — not needed by the compiler |
| **RedBlackTree** | **`tk_data::collections`** | specialized balanced BST; ordered-map alternative with worst-case guarantees |
| **Trie** | **`tk_data::collections`** | prefix structure; term dictionary for GIN / inverted index, routing tables |
| **BloomFilter** | **`tk_data::collections`** | probabilistic membership over the monolith `BitSet`; the SSTable/LSM read-path skip |
| **RTree** | **`tk_data::collections`** | spatial index; a leaf, or a GiST opclass |
| **GiST** | **`tk_data::collections`** (in-package BASE) | generalized search tree — the template R-Tree / B-Tree / GIN opclasses ride (Postgres model) |
| **GIN** | **`tk_data::collections`** (composite) | generalized inverted index: term dictionary (Trie/BTree) + posting lists (ChunkChain); the in-mem twin of `storage::inverted` |
| **BTree** (in-mem) | **`tk_data::collections`** (base) | high-fanout ordered index; range-index base |
| **BPlusTree** (in-mem) | **`tk_data::collections`** (base) | linked-leaf ordered index for range scans; in-mem twin of `storage::bplus` and a candidate LSM memtable |
| **WAL** | **`tk_data::storage`** | append-only durability log — a storage-engine component, NOT a collection |
| **SSTable** | **`tk_data::storage`** (composite) | immutable sorted file = data blocks + index + Bloom + footer |
| **LSMTree** | **`tk_data::storage`** (TOP composite) | memtable + WAL + SSTables + Bloom + compaction — the master composition |
| **Inode** | **`tk_data::storage`** | file-organization metadata node |
| **Extent** | **`tk_data::storage`** | contiguous block-range allocation unit |
| **Pager / buffer pool** | **`tk_data::storage`** (package-internal) | random-access page cache the on-disk trees sit on; a storage-engine gap `tk_data` fills itself (§8) |
| **persistent BPlusTree** | **`tk_data::storage`** (composite) | page-based B+Tree over Pager + Extent + WAL |
| **InvertedIndex** | **`tk_data::storage`** (composite) | on-disk full-text: term dictionary + posting lists; on-disk twin of GIN |
| graph / RDF model, SQL, planner, catalog, MVCC visibility, unique/schema constraints | **DB package** | database semantics ABOVE the storage seam; consume `tk_data`, add meaning |

**The three genuinely-hard placements, decided:**

1. **Table core stays in the monolith** (already `src/collections/table.tks`). It is a pure composition of
   monolith bases and carries the atomic multi-index transaction the DB rides. `tk_data` does not duplicate
   it; instead `tk_data::collections` supplies richer INDEX kinds (a B-Tree / SkipList range index) that a DB
   table can select in place of the monolith `SortedDictionary`. (Placement re-examined in §11.)
2. **GiST is an in-package base, not a leaf.** R-Tree, B-Tree, and GIN are expressible as GiST opclasses
   (the Postgres lineage). We ship GiST as the `tk_data::collections` template AND keep R-Tree standalone as
   the concrete spatial leaf, so a consumer who wants only spatial pays only for R-Tree.
3. **In-mem vs on-disk twins are two structures, not one.** `collections::bplus` (in-mem) and
   `storage::bplus` (page-based, durable) share an algorithm but not a backing — one lives in the arena, the
   other over the Pager + WAL. Likewise `collections::gin` vs `storage::inverted`. They are namespaced apart
   on purpose; conflating them would drag the pager into the in-mem path.

---

## 3. The `tk_data` namespace tree

```
tk_data
├── collections                 (specialized IN-MEMORY structures)
│   ├── skiplist                SkipList<K,V>            leaf   · concurrent variant (CAS)
│   ├── rbtree                  RedBlackTree<K,V>        leaf
│   ├── trie                    Trie<V>                  leaf
│   ├── bloom                   BloomFilter<T>           leaf   · over monolith BitSet
│   ├── rtree                   RTree<K,V>               leaf   · or a GiST opclass
│   ├── gist                    GiST<K,V,Op>             BASE   · template for rtree/btree/gin
│   ├── gin                     Gin<K,V>                 composite (trie/btree + posting lists)
│   ├── btree                   BTree<K,V>               base
│   └── bplus                   BPlusTree<K,V>           base   · in-mem, linked leaves
│
└── storage                     (ON-DISK storage-engine components)
    ├── wal                     Wal                      leaf   · append-only durability log
    ├── extent                  Extent, ExtentAlloc      leaf   · block-range allocation
    ├── inode                   Inode                    leaf   · file metadata node
    ├── pager                   Pager, Page              base   · package-internal buffer pool
    ├── sstable                 SsTable, SsTableWriter   composite (bloom + block index + io)
    ├── bplus                   PersistentBPlusTree      composite (pager + extent + wal)
    ├── inverted                InvertedIndex            composite (term dict + posting lists + pager)
    └── lsm                     LsmTree                  TOP composite (memtable + wal + sstable + bloom)
```

Every symbol is addressed from the package's alias root, exactly as the package model prescribes
(TEKO_ROADMAP_PACKAGES.md PK0: `use tk_data` → `tk_data::storage::lsm::LsmTree`). The `teko` root stays
reserved and inviolate — `tk_data` never claims a `teko::` name.

---

## 4. The reuse graph — leaves vs composites

**Leaves** (depend only on MONOLITH primitives, never on another `tk_data` module):

- `collections`: `skiplist`, `rbtree`, `trie`, `bloom`, `rtree`, `btree`, `bplus`
- `storage`: `wal`, `extent`, `inode`

**Bases** (in-package, reused by composites in the same package):

- `collections::gist` — the generalized-search-tree template
- `storage::pager` — the buffer pool the on-disk trees sit on

**Composites** (compose leaves/bases — this is where the reuse concentrates):

```
collections::gin      ← collections::trie (or btree)  +  ChunkChain (posting lists, monolith)
collections::rtree    ← may specialize collections::gist
collections::btree    ← may specialize collections::gist

storage::sstable      ← collections::bloom  +  collections::btree (block index)  +  teko::io
storage::bplus        ← storage::pager  +  storage::extent  +  storage::wal
storage::inverted     ← collections::trie/btree  +  storage::pager  +  ChunkChain (posting lists)
storage::lsm          ← memtable( collections::skiplist | collections::bplus )   ← the choice point
                        + storage::wal
                        + storage::sstable  ( ⊃ collections::bloom )
                        + Heap (monolith, k-way merge for compaction)
```

**The master composition, spelled out:** `LSM = SSTable + WAL + Bloom + memtable`. Reads consult the
memtable, then each SSTable newest-to-oldest, skipping an SSTable whose Bloom filter says the key is absent;
writes append to the WAL (durability) then the memtable; a full memtable is frozen and flushed to a new
SSTable; compaction k-way-merges SSTables (a monolith `Heap`) and drops the inputs. Every arrow above is a
reuse edge — no structure is re-implemented, which is the whole argument for one package with two
namespaces rather than nine loose libraries.

---

## 5. What `tk_data` consumes from the monolith (the dependency contract)

`tk_data` is pure `.tks` over the monolith's **declared** surface. The contract, by consumer:

| monolith surface | consumed by | for |
|---|---|---|
| arena `region_alloc`/`region_drop` (`src/runtime/arena.tks`) | all in-mem structures | node/slot allocation; region-drop bulk reclaim |
| COL-F0 `of_len`/`place`/`read`/`write`/`bucket` | all in-mem structures | fixed-backing slot arrays, place-once value regime |
| `ChunkChain<T>` (COL-Q1) | gin/inverted posting lists, skiplist/lsm memtable node runs | growable-by-linking, stable-index, thread-safe substrate |
| `BitSet` (COL-Q7) | `collections::bloom` | the bit array behind the Bloom filter |
| `Heap` (COL-Q6) | `storage::lsm` compaction, `storage::sstable` merge | k-way merge / priority ordering |
| `Map`/`SortedDictionary` (base) | index bookkeeping, manifests | in-mem lookup tables the engine keeps |
| `deep_copy` (3-category value regime) | value-typed keys/values | independent copies on read (no aliasing internal storage) |
| `teko::io` Reader/Writer/Seeker/Closer + BufReader/BufWriter | all `storage::…` | on-disk streaming (WAL append, SSTable read/write, pager IO) |
| `teko::fs` open/create/rename/fsync | wal, sstable, pager | file lifecycle + crash-safe publish |
| `sync.tks` fine-mutex + CAS | concurrent skiplist, lsm memtable switch, wal append | thread-safe mutation (TS is the DEFAULT model) |
| `teko::threads` spawn/join | lsm compaction, wal group-commit flush | background work |
| `teko::encoding` varint / fixed-LE | all `storage::…` serialization | stable on-disk key/value/offset encoding |
| `teko::math` bit ops, log2 | skiplist levels, bloom sizing, page math | index arithmetic |
| `teko::crypto::hash` (or a fast checksum) | bloom hashing, block integrity | see the gap in §8 (crypto SHA is too slow for block CRC) |

Everything above is a stable, ALREADY-DECLARED monolith surface (or one already queued in the collections /
io-streaming design). The design-ahead contracts in §7 are written against these declared shapes so they
compile the day both gates close.

---

## 6. ACID / persistence hooks the DB needs from `tk_data`

This section validates that `tk_data` is the RIGHT seam under the DB: the DB should get every
below-the-planner durability/isolation primitive from `tk_data`, and nothing forces it back into the
monolith raw layer.

- **Durability (D) — `tk_data::storage::wal`.** Append a record, group-commit a batch, force an `fsync`
  barrier, and REPLAY on recovery. The DB's transaction log IS this WAL; a committed txn = WAL append +
  fsync + apply. Recovery replays the WAL tail past the last checkpoint.
- **Atomicity (A) — WAL redo + single-commit-point publish.** The LSM and the persistent B+Tree publish a
  write by ONE commit step (bump a watermark / flip a page pointer LAST), the exact shape Table core already
  uses (`table-collection-sql-linq-0.3.1.md` §2.4). A crash before that step leaves the store untouched;
  after it, the WAL redoes it. All-or-nothing with no separate undo log on the read path.
- **Isolation (I) — epoch-pinned snapshots = the MVCC substrate.** `tk_data` structures expose a `Snapshot`
  handle: an immutable read view pinned by a monotone `epoch: u64` (Table core already carries `epoch`).
  Readers observe the version live at pin time; writers publish new versions without disturbing pinned
  readers (the LSM's immutable SSTables + memtable versioning give this naturally). The DB layers MVCC
  visibility (txn ids, read/write sets) ON TOP — it does not need `tk_data` to know about transactions.
- **Consistency (C) — the atomic multi-index txn primitive.** Unique / schema enforcement is DB semantics,
  but the DB rides `tk_data`'s (and Table core's) atomic multi-index publish so index maintenance is
  all-or-nothing under a single writer lock.
- **Page / extent management — `tk_data::storage::{pager, extent, inode}`.** The block allocator + buffer
  pool the DB's heap and index files sit on. The DB chooses the file layout; `tk_data` owns the paging.
- **Checkpoint / recover seam.** LSM = immutable SSTable set + a manifest is a natural checkpoint; the
  persistent B+Tree checkpoints by WAL truncation after a durable flush. `tk_data` exposes `checkpoint()` /
  `recover()`; the DB decides checkpoint cadence.

Conclusion: the DB sits cleanly on `tk_data` for durability, atomic publish, snapshot isolation, and paging.
The seam holds — the DB package adds graph/RDF/SQL/ACID SEMANTICS, not storage mechanism.

---

## 7. Design-ahead interface contracts (the shapes the implementer fills)

These are the top-level surface signatures, written against the monolith's DECLARED shapes so they compile
the day the gates close. They are the contract, not the implementation — bodies are honest-stops until then.
Per the W15 convention in force here, doc-comments (`/** */`) sit ONLY on exported (`exp`) declarations, no
`//` or `/* */` anywhere, and each doc stays no longer than the code it documents.

### 7.1 `tk_data::collections::bloom` — a leaf over the monolith `BitSet`

```teko
/**
 * BloomFilter — probabilistic set membership over a monolith BitSet. `maybe(x)` may false-positive
 * but never false-negatives, so an SSTable/LSM read skips a filter that answers "absent" with certainty.
 */
exp type BloomFilter<T> = class {
    intern bits: teko::collections::BitSet
    intern k: u32
    intern seeds: []u64
}

/**
 * make — size a filter for `n` expected items at false-positive rate `p`; picks bit count and `k`.
 */
exp fn make<T>(n: u64, p: f64): BloomFilter<T>

/**
 * add — record `x`'s membership by setting its `k` hashed bits.
 */
exp fn add<T>(ref f: BloomFilter<T>, x: T)

/**
 * maybe — true if `x` MIGHT be present (never a false negative); false means certainly absent.
 */
exp fn maybe<T>(f: BloomFilter<T>, x: T): bool
```

### 7.2 `tk_data::collections::skiplist` — an ordered leaf / LSM memtable

```teko
/**
 * SkipList — an ordered map with O(log n) probabilistic search; the default LSM memtable and a
 * lock-free-reader concurrent ordered map. Nodes live in the arena; levels are geometric.
 */
exp type SkipList<K, V> = class {
    intern head: *SkipNode<K, V>
    intern height: u32
    intern count: u64
    intern lock: u64
}

/**
 * insert — place `(k, v)` in order, splicing a new tower of random height under CAS or the fine mutex.
 */
exp fn insert<K, V>(ref s: SkipList<K, V>, k: K, v: V)

/**
 * get — the value for `k`, or null when absent (value returned by copy, never an alias).
 */
exp fn get<K, V>(s: SkipList<K, V>, k: K): V?

/**
 * range — a lazy `teko::iter::Iterator` over `[lo, hi)` in key order for scans and LSM merge.
 */
exp fn range<K, V>(s: SkipList<K, V>, lo: K, hi: K): teko::iter::Iterator<Pair<K, V>>
```

### 7.3 `tk_data::storage::wal` — the durability leaf over `teko::io`

```teko
/**
 * Wal — an append-only write-ahead log over a seekable file. `append` stages a record; `sync` forces
 * it durable (the D in ACID); `replay` re-reads the tail on recovery. Group-commit batches fsyncs.
 */
exp type Wal = class {
    intern file: teko::io::Writer
    intern tail: u64
    intern lock: u64
}

/**
 * append — write one length-prefixed, checksummed record; returns its LSN. Not durable until `sync`.
 */
exp fn append(ref w: Wal, rec: []byte): u64 | error

/**
 * sync — force every appended record to durable storage (fsync barrier); the group-commit point.
 */
exp fn sync(ref w: Wal): error?

/**
 * replay — iterate the log from `after` on recovery, stopping at the first torn/short tail record.
 */
exp fn replay(w: Wal, after: u64): teko::iter::Iterator<[]byte>
```

### 7.4 `tk_data::storage::lsm` — the top composition

```teko
/**
 * LsmTree — the master composition: a memtable (SkipList/BPlusTree) fronting a WAL for durability and
 * an ordered set of immutable SSTables (each with a Bloom filter) merged by background compaction.
 * Reads consult memtable then SSTables newest-first, skipping Bloom-negative tables. A Snapshot pins an
 * epoch for MVCC-style isolation.
 */
exp type LsmTree<K, V> = class {
    intern mem: tk_data::collections::skiplist::SkipList<K, V>
    intern wal: tk_data::storage::wal::Wal
    intern levels: []tk_data::storage::sstable::SsTable
    intern epoch: u64
    intern lock: u64
}

/**
 * put — durably record `(k, v)`: WAL append + memtable insert; may trigger a memtable flush to an SSTable.
 */
exp fn put<K, V>(ref t: LsmTree<K, V>, k: K, v: V): error?

/**
 * get — the newest value for `k` across memtable and SSTables, or null (Bloom-skips absent tables).
 */
exp fn get<K, V>(t: LsmTree<K, V>, k: K): V? | error

/**
 * snapshot — an immutable read view pinned at the current epoch; the DB's MVCC read view rides this.
 */
exp fn snapshot<K, V>(t: LsmTree<K, V>): Snapshot<K, V>

/**
 * checkpoint — flush the memtable and record the manifest so recovery can truncate the WAL to here.
 */
exp fn checkpoint<K, V>(ref t: LsmTree<K, V>): error?
```

(The remaining modules — `rbtree`, `trie`, `rtree`, `gist`, `gin`, `btree`, `bplus`, `sstable`, `extent`,
`inode`, `pager`, `storage::bplus`, `inverted` — follow the same shape: an `exp type` + `exp fn` surface
against the monolith contract in §5, bodies honest-stopped until §9's gates close.)

---

## 8. Monolith gaps `tk_data` needs filled

These are surfaces `tk_data` requires that the monolith does not yet (or not obviously) provide. Each is
REPORTED here for the owner/coordinator — this doc does not open issues (Laws: adjacent findings are
reported up, not turned into new issues by the architect). None blocks the DESIGN; each blocks the eventual
IMPLEMENTATION and is design-ahead-safe to name now.

1. **Durable flush (`fsync`/`fdatasync`).** WAL durability needs an explicit force-to-disk on a file handle.
   Confirm `teko::fs`/`teko::io` exposes a `sync`/`flush-durable` distinct from a buffer flush. **Likely a
   gap** — the io-streaming design defines `Writer` but a durability barrier is a separate capability.
2. **Positional (random-access) file read/write.** The Pager needs `pread`/`pwrite`-style block IO.
   `teko::io::Seeker` is DECLARED (STDLIB_CORE IO0); confirm the file type implements `Seeker` + a
   positional read that does not disturb a shared cursor. **Confirm, likely present-once-IO0-lands.**
3. **Atomic file rename + directory fsync.** Crash-safe SSTable publish and WAL rotation need atomic
   `rename` and a directory fsync. Confirm `teko::fs::rename` is atomic-replace and a dir-sync exists.
   **Likely a partial gap.**
4. **A fast non-cryptographic checksum (CRC32C / xxhash).** Block/record integrity wants a fast checksum;
   `teko::crypto::hash` is SHA-family (correct but too slow per-block). **Gap** — either a `teko::hash`
   fast-checksum primitive or a `tk_data`-internal CRC32C (pure Teko, no monolith change). Recommend the
   monolith primitive so the DB and net share it.
5. **Concurrency: an RW-lock and/or epoch/hazard reclamation.** Concurrent SkipList readers during
   compaction, and freeing old SSTables/nodes while readers are pinned, need either an RW-lock or
   epoch-based reclamation. `sync.tks` provides a fine mutex + CAS; an RW-lock and a reclaimer may be
   missing. **Gap, S8-adjacent** — this is the concurrency substrate the "concurrent variants" ride; flag
   for the S8 concurrency track.
6. **Aligned page buffers (O_DIRECT-friendly).** The Pager wants aligned page buffers. The arena already
   does aligned allocation (`c_aligned_alloc`, arena-em-teko.md §3), and the net keystone's arena-backed
   `Buf` may suffice. **Confirm** the shared `Buf` region covers this; no new primitive expected.
7. **`mmap` (optional).** A memory-mapped pager would want `mmap`; arena-em-teko.md §2 records only
   `posix_memalign` today. **Optional, not required** — a buffer-pool pager works without mmap; noted so the
   mmap decision is explicit, not accidental.
8. **A thread-pool / background scheduler (optional).** LSM compaction + WAL group-commit run in the
   background. `teko::threads` spawn/join is enough for a first cut; a pool is an optimization. **Not a
   blocker**, named for completeness.

The load-bearing ones are **1, 3, 4, 5** — durability barrier, atomic rename, fast checksum, and the
concurrency reclaimer. They are the monolith work `tk_data`'s implementation will wait on beyond the two
timing gates.

---

## 9. Timing — why there are NO crumbs

Implementation of `tk_data` is gated on TWO doors, BOTH of which are still shut, so this document produces
**no crumb sequence** (a crumb is an executable step against a live seed; there is no seed to execute
against yet):

- **Gate M — memory stable.** The dry build must measure peak ≤ 1.5 GB (CLAUDE.md, owner 2026-08-19). Until
  then the regime is compile-only, and adding a specialized-structure package is exactly the kind of surface
  the memory campaign is trying to keep OUT of the monolith. `tk_data` being a package is aligned with the
  gate, but its implementation waits for the gate to close.
- **Gate P — package mode consumable end to end.** `kind = "package"` must build a `.tkl`, resolve, fetch,
  lock, and static-link mono-by-use (TEKO_ROADMAP_PACKAGES.md PK0–PK8; package-manager.md §5.1). `tk_data`
  is the FIRST real third-party-shaped package; it cannot exist as a consumable dependency before the
  ecosystem that consumes it does.

Both gates are outside this design's scope and this branch. When they close, the implementation sequences
as: FASE 0 the leaves (`bloom`, `wal`, `extent`, `inode`, `skiplist`, `rbtree`, `trie`, `btree`, `bplus`,
`rtree`) → FASE 1 the in-package bases (`gist`, `pager`) → FASE 2 the composites (`gin`, `sstable`,
`storage::bplus`, `inverted`) → FASE 3 the top (`lsm`) → FASE 4 the DB package on top. That ordering is the
design-ahead skeleton, NOT a crumb plan — crumbs get written when a live seed and both gates exist.

---

## 10. Risks + law tensions

- **A subtly-wrong storage structure is worse than none** (the arena's own lesson, arena-em-teko.md §6). WAL
  and the persistent B+Tree corrupt data on disk if the durability/atomic-publish invariants are a hair off,
  and unlike an in-mem bug it survives a restart. Resolution: every leaf ships with a
  behavior-asserting fixture (a crash-injection replay for WAL, a torn-write test for the pager), not a
  crash-absence test — mirrors the arena's per-group behavior gates.
- **Two namespaces, one package — keep the twins apart.** `collections::bplus` (arena) and `storage::bplus`
  (pager+WAL) share an algorithm; the tension is a tempting shared base that drags the pager into the in-mem
  path. Resolution (law-first, M.3 no-pretending): they are DISTINCT types with a shared ALGORITHM doc, not
  a shared backing. The namespaces enforce the separation.
- **Concurrency substrate is the real dependency.** The "concurrent variants" (SkipList, LSM) need the
  reclaimer in §8.5. Tension: shipping a concurrent SkipList over only a fine mutex is a correctness lie
  under compaction. Resolution: concurrent variants are gated on the S8 concurrency track; the single-thread
  variants ship first and are honest about their TS mode (the collections model already bans the growable
  single-array RMW race and makes TS explicit).
- **Base-collection duplication.** If external users of `tk_data` also want `List`/`Map`, the tension is
  monolith `teko::collections` (compiler-internal) vs a package re-export. Resolution: do NOT duplicate;
  §11 raises this to the owner rather than deciding it, because it is a genuine preference tension, not a
  law question.
- **On-demand link honesty (PK8).** A DB that imports `tk_data` but uses only the LSM must not drag R-Tree /
  GiST into its binary. Resolution: `tk_data` is structured as independent leaf modules so PK8 reachability
  tree-shakes the unused ones — the namespace tree in §3 is deliberately module-per-structure to make this
  free.

No genuine unresolved LAW tension remains — the placement is fully determined by the ≤1.5 GB gate + the
compiler-consumes-base ruling + honest namespacing. The one open item is an owner PREFERENCE (§11), not a
law tension, so this design does not HALT.

---

## 11. Open question for the owner (preference, not a law tension)

The coordinator's instruction was explicit: base collections stay in the monolith, and if the architect
thinks the owner might want to move base too, FLAG it as a question rather than decide. Two related
preference calls, raised — not decided:

1. **Should the monolith's base collections ALSO be surfaced to external `tk_data` users?** Today the
   compiler needs `List`/`Map`/`Set` internally, so they MUST stay monolith primitives (settled). But an
   external app that depends on `tk_data` will also want the base collections. Options: (a) the app depends
   on both the monolith surface and `tk_data` (no duplication, but couples app builds to the monolith's
   public collection surface); (b) `tk_data` thinly RE-EXPORTS the base collections so a data-heavy app has
   one dependency (risks a second canonical instance — tension with byte-identity, package-manager.md §2.6);
   (c) a future `teko::collections` becomes its own package the monolith ALSO consumes (large, changes the
   self-host boundary). Recommendation, law-first: **(a)** — no duplication, no second canonical instance —
   but this is the owner's call because it trades ergonomics against the byte-identity wall.
2. **Does Table core stay in the monolith, or move to `tk_data::collections`?** It is currently
   `src/collections/table.tks` and the compiler does not consume it, so it COULD move to `tk_data` and slim
   the monolith further. It stays put in this design (it is the shared in-mem multi-index seam both the DB
   and `tk_data` build on, and it is small), but if the owner prefers a leaner monolith, Table is the one
   base-adjacent structure that could cross into `tk_data::collections` without breaking the
   compiler-consumes-base rule. Flagged, not decided.

Neither blocks the architecture. The namespace tree, the boundary, the reuse graph, and the monolith gaps
stand regardless of how the owner rules these two.
