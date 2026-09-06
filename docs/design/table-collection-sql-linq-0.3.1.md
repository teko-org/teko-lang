---
section: design
created: 2026-08-19
status: DESIGN-AHEAD (fase1c) — no product line. This doc IS the deliverable: no `.tks`, no build, no
        reseed, no `teko test`. Table<…> is DEFERRED — off the critical path; it does NOT block §16, §10,
        or the arena. The owner wants it fully specced so the collection library has real usage potential.
source: mudancas-superficie-0.3.1.md:1624-1631 (the owner's Table<…> ruling: in-memory multi-index, ≤16
        generics = the type-arg ceiling, atomic multi-value swaps, its own stdlib op-set, fase1c, does NOT
        block §16/§10), colecoes-remodelagem-backing-fixo-0.3.1.md (§2 ensure_cap watermark, §4.1 the
        immortal segment-directory — the chunk-chain substrate + the TS reclaim model), io-streaming-0.3.1.md
        (§2 FileStream/stream_read/stream_write/stat/CHUNK — the sole IO surface the file variant reuses),
        src/collections/{map,sorted_set,dictionary,hashset,priority_queue}.tks (the Map/SortedSet bases),
        src/runtime/sync.tks:63-93 (the fine mutex + condvar the atomic transaction runs under),
        src/cmp/cmp.tks (IEq/IOrd/IHash), src/iter/iter.tks:2 (the Iterator<T> pull protocol),
        plano-secao9E-closures.md (func<…,R>/action<…> delegates — LANDED — the typed predicate/projection).
frozen: bootstrap/teko.c + the C twins are FROZEN. New work is `.tks` only. Table is a COMPOSITION of the
        settled bases (chunk-chain + Map + SortedSet + fixed arrays) plus ONE novel capability (the atomic
        multi-index transaction) — NOT a reinvented structure. The single-growable-array-by-swap is BANNED.
---

# Table<…> — the multi-index in-memory collection, with a SQL/LINQ query surface + an IO-backed variant (0.3.1)

Architect, 2026-08-19. Base: `origin/fix/retirement` @ `f694696a`. DESIGN-ONLY — this document is the
whole deliverable. Table is **fase1c** (Doc-2 stdlib-generics, one lane beyond the fase1b
Dictionary/HashSet/Sorted* remodel): **buildable now** (the generics machine is landed), but **DEFERRED**
and **off the critical path — it does NOT block §16, §10-(c), or the arena** (owner,
`mudancas-superficie-0.3.1.md:1629`). We spec it in full so the collection library gains its biggest
reuse case and real query potential; the coordinator dispatches it as an independent stdlib lane when
build capacity frees (avoid heavy builds parallel to §16).

The governing discipline: **Table is a COMPOSITION.** The storage is trivial reuse of the settled bases;
the only genuinely new design is the **atomic multi-index transaction**. Every fork below is resolved
law-first, and every place a choice collides with a sealed law (the ≤16 type-arg ceiling, the TS reclaim
model, io-streaming's single IO surface, closure opacity) is counter-argued and resolved, not silenced.

---

## 0. The settled ground this reduces to (do NOT reinvent)

Table composes exactly the four sealed bases and adds nothing structural:

| base | role in Table | file:line |
|---|---|---|
| **chunk-chain node-linked** (unrolled linked list of FIXED chunks + a fixed directory of chunk pointers) | the **row store** — append-stable slots; a `RowId` never dangles across growth | `colecoes-remodelagem-…:105-176` (watermark), `:322-336` (the immortal segment-directory) |
| **Map** (hash, parallel arrays, linear-scan today; bucket form is the fase1b follow-up) | a **hash index** — point lookup `key → RowId` | `src/collections/map.tks:2`, `dictionary.tks` |
| **SortedSet** (ascending, binary-searchable) / **SortedDictionary** | a **range index** — ordered `key → RowId`, for range + `order_by` | `src/collections/sorted_set.tks:2`, `sorted_dictionary.tks` |
| **fixed arrays** (`[]T`, `[]bool` tombstones, `[]u64` bitset) | the chunk backings, the live-bit set, the directory | CLAUDE.md fixed-array form |

**TS by default** (the sealed model): every collection is thread-safe via the chunk-chain + a **fine
mutex** (or lock-free CAS-append at the tail); the old backing's reclaim is **deferred** under concurrent
readers (epoch / §7.8 retain), immediate single-thread. There is **no separate "Concurrent" family** — TS
is intrinsic. Table honours this: writers serialize under the table's fine mutex
(`src/runtime/sync.tks:63` `mtx_lock`); the row store's deferred reclaim under lock-free readers is the
same §4.1 immortal-segment model.

**Per-type memory** (`mudancas-superficie-0.3.1.md:1614-1637`): **value** columns = deep-copy into the
row-store bucket; **class** columns = arena-per-object, region-drop; **wrapped** = refcount opt-in. Table
itself is a `class` (reference-semantic wrapper) → arena-per-object, freed by region-drop. **`RowId` is a
plain `u64` handle, never a pointer** — this is why chunk-chain + directory beats the banned
growable-array-by-swap: growth never moves a row, so an index entry (a `RowId`) never becomes a UAF.

**The banned shape, restated.** The single growable-array-by-swap is OUT: a whole-backing swap is a coarse
RMW race, and it would invalidate every index (indices would hold stale addresses). The chunk-chain's
fixed chunks + fixed directory give **append-stable addressing**, which is precisely what a multi-index
structure needs — the indices point at stable `RowId`s, never at a backing that can be swapped underneath
them. The composition is not incidental; it is *forced* by the multi-index requirement.

---

## 1. The column ceiling — the ≤16 type-arg law, and how Table honours it (counter-argument + resolution)

**The sealed law.** A generic declares **at most 16 type parameters** — `max_generic_arity(): u64 { 16 }`
(`src/checker/resolve.tks:659`), enforced at declaration (`src/checker/check_modules.tks:255-256`, *"a
generic may declare at most 16 type parameters"*) and at call (`src/checker/typer.tks:2004-2005`, *"at most
16 explicit type arguments"*). The owner names this the **column ceiling**: a Table carries **≤16 columns**
because each column type is a type argument (`mudancas-superficie-0.3.1.md:1625`).

**The tension (counter-argument).** The owner wrote `Table<C1..Cn>` — a *variadic* generic. **Teko has no
variadic generics**: a user `type` has a *fixed* arity. So `Table<C1..Cn>` for `n ∈ [1,16]` cannot be one
declared type. This is the same shape `func`/`action` faced, and how they resolved it is the precedent:
`func<T1,…,Tn,R>` is variadic *at the surface* but desugars in the parser to one fixed AST node
(`FunctionType { params; ret }`, `plano-secao9E-closures.md` §1.1) and is **monomorphized at comptime**.
Table cannot desugar to a single node the same way (it is a user-facing storage type, not a delegate), so
the variadic surface must be *realized* as a family.

**FORK — how the columns are typed:**

- **Fork A (column-as-type-arg family) — RECOMMENDED, owner-named.** A family `Table1<C1>` …
  `Table16<C1..C16>`, each a distinct monomorphized type; the **row is a positional tuple**
  `Row_n = (C1,…,Cn)`. Columns are **positional, per-ordinal compile-time typed** — `col 0: C1`, …, exactly
  the owner's `Table<C1..Cn>` with per-column type safety at each ordinal. Realized by **comptime family
  generation** (the §14 parametric-type-macro machine, `section14-parametric-type-macros-v1.md`) emitting
  arities 1..16 from one template — the same "variadic-at-surface, monomorphic-underneath" trick as
  func/action. Fallback if §14 macros are not desired: 16 hand-written types from one template (mechanical,
  reviewable). **This is the primary** — it matches the ≤16 ceiling exactly and gives typed positional
  columns.
- **Fork B (row-struct single-generic).** `Table<R>` where `R` is a user **value-struct** (the row type);
  columns = *named fields*; single type-arg, no family, no arity fight. But extracting an index key needs
  either field-reflection or a projection `func<R, K>` per index. More idiomatic, less positional.

**Resolution (law-first): ship Fork A as primary, adopt Fork B's projection idiom for indices.** Fork A
honours the owner's exact framing and the ceiling; but a *positional tuple row* is awkward for the query
surface, so we borrow Fork B's key-extraction: an index is described not by "column 3" alone but by a
**projection `func<Row, K>`** (§9E delegate, landed) — which reads column 3 in Fork A and reads a named
field in Fork B. The two forks then differ only in how `Row` is spelled; the index + query design below is
written against `Row` and a projection, so it serves both. **Recommendation: Fork A** (positional-typed
family, comptime-generated), because per-ordinal typing at ≤16 is exactly the owner's ruling; Fork B is the
documented alternative if the owner prefers named columns over positional.

> The doc below writes `Table<Row>` / `Row` generically. Read `Row` as `(C1,…,Cn)` under Fork A.

---

## 2. DELIVERABLE 1 — the Table core as a composition

### 2.1 The row store — chunk-chain + a fixed directory (append-stable)

```teko
/**
 * RowId — the stable handle to a stored row: the chunk ordinal in the high bits, the slot index within
 * the chunk in the low bits, packed into one `u64`. It is a VALUE, never a pointer — the chunk-chain's
 * fixed chunks and the fixed directory never move a stored row, so a `RowId` handed to an index stays
 * valid across every growth (this is why the multi-index structure composes on chunk-chain and NOT on the
 * banned growable-array-by-swap, whose swap would dangle every index entry).
 *
 * @since 0.3.1
 */
exp type RowId = struct {
    /** The chunk ordinal (index into the directory) and the slot within the chunk, packed `chunk << 32 | slot`. */
    bits: u64
}

/**
 * RowChunk<Row> — one FIXED node of the unrolled row list: a fixed `[]Row` backing of exactly
 * `CHUNK_ROWS` slots, a `used` watermark (the live-prefix length, in the R9 sense), a per-slot `live`
 * tombstone bitset (a deleted row lowers its bit without moving neighbours), and the `next` link. The
 * backing never resizes: a full table appends a NEW chunk, it never swaps this one — so addresses are
 * append-stable.
 *
 * @since 0.3.1
 */
type RowChunk<Row> = class {
    /** The fixed row backing; `backing.len == CHUNK_ROWS` (capacity), never a live count. */
    intern backing: []Row
    /** The high-water slot ever written in this chunk; the commit point for lock-free readers (see §2.4). */
    intern used: u32
    /** One bit per slot: 1 = live, 0 = tombstoned. A delete clears the bit; the slot is not moved. */
    intern live: []u64
    /** The next chunk in the chain, or null at the tail. */
    intern next: RowChunk<Row> | null
}

/**
 * RowStore<Row> — the chunk-chain plus a FIXED directory of chunk pointers (the §4.1 immortal-segment
 * shape). The directory gives O(1) `RowId -> &Row` (index by the chunk ordinal) without walking `next`;
 * it is sized at construction and, if exhausted, chains a directory-of-directories — the existing
 * directory NEVER moves, so no reader's chunk pointer dangles. `count` is the total live rows.
 *
 * @since 0.3.1
 */
type RowStore<Row> = class {
    /** The fixed directory: `dir[chunk_ord]` is the chunk pointer; never reallocated in place (chains on exhaustion). */
    intern dir: []RowChunk<Row>
    /** The number of chunks currently installed (the live prefix of `dir`). */
    intern nchunks: u32
    /** The tail chunk, where the next append lands. */
    intern tail: RowChunk<Row>
    /** The total live-row count across all chunks (tombstones excluded). */
    intern count: u64
}
```

`CHUNK_ROWS` is a fixed constant (recommend 256 — one cache-friendly node; a fork with the owner if a
different fan-out is wanted, but it is a one-line const, not a redesign). Append is amortized O(1): write
into `tail.backing[tail.used]`, bump `tail.used`; when a chunk fills, allocate a new fixed chunk, install
it at `dir[nchunks]`, bump `nchunks`. **No backing ever grows** — each chunk is a brand-new fixed array;
the directory grows by the R9 `ensure_cap` idiom (`colecoes-remodelagem-…:141`) but the *old directory is
retained immortal, not dropped* (a concurrent reader may be indexing it — the §4.1 rule). This is the
sole place Table diverges from sequential-drop, and it is *required* by cross-thread safety, not a
relaxation of the fixed-array law: every array (chunk backing, directory) is fixed; growth allocates a new
fixed array; nothing resizes in place.

### 2.2 The index descriptors — Map for hash, SortedSet for range

```teko
/**
 * IndexKind — how an index is physically stored and what queries it accelerates. `Hash` is a Map-backed
 * point index (equality lookup); `Range` is a SortedSet/SortedDictionary-backed ordered index (range
 * scans and `order_by`). Both map an extracted key to one or more `RowId`s over the ONE shared row store.
 *
 * @since 0.3.1
 */
exp type IndexKind = Hash | Range

/**
 * Uniqueness — whether an index key maps to at most one row (`Unique`, a `Dictionary<K, RowId>`) or to
 * many (`Multi`, a `Dictionary<K, RowIdBag>` where the bag is a small chunk-chain of row ids). A `Unique`
 * index rejects a second insert of the same key by aborting the transaction (§2.4).
 *
 * @since 0.3.1
 */
exp type Uniqueness = Unique | Multi

/**
 * Index<Row, K> — one index over the table, sharing the single row store. It names itself, extracts its
 * key from a row by a typed projection `func<Row, K>` (the §9E delegate — this is the ONE key-extraction
 * that serves both Fork A positional columns and Fork B named fields, §1), and stores that key -> RowId in
 * either a hash map (`Hash`) or an ordered map (`Range`). `K` bounds: `IEq & IHash` for `Hash`,
 * `IOrd` for `Range` (`src/cmp/cmp.tks`).
 *
 * @since 0.3.1
 */
exp type Index<Row, K> = class {
    /** The index name, used by `use_index` hints and the planner. */
    intern name: str
    /** Hash (point) or Range (ordered). */
    intern kind: IndexKind
    /** Unique or Multi. */
    intern uniq: Uniqueness
    /** The typed key projection: reads column-`i` (Fork A) or a named field (Fork B) out of a row. */
    intern key_of: func<Row, K>
    /** Hash storage (present iff kind == Hash): key -> RowId (Unique) or key -> RowIdBag (Multi). */
    intern hash_ix: Dictionary<K, RowId> | null
    /** Range storage (present iff kind == Range): an ordered key -> RowId, binary-searchable for range. */
    intern range_ix: SortedDictionary<K, RowId> | null
}
```

Multiple indices share the **one** `RowStore`. An `Index` holds `RowId`s (values), so growing the store
never invalidates an index. This is the composition's whole point: N indices, one row store, all consistent
by construction because they address rows by stable id.

### 2.3 The Table wrapper

```teko
/**
 * Table<Row> — an in-memory, multi-index, thread-safe collection: one chunk-chain row store (§2.1) shared
 * by zero or more indices (§2.2), guarded by a fine mutex (§2.4). `Row` is the positional tuple
 * `(C1,…,Cn)` (Fork A, ≤16 columns = the type-arg ceiling, §1) or a user value-struct (Fork B). Every
 * mutation (`insert`/`update`/`delete`) is an ATOMIC multi-index transaction: the row AND every index
 * entry move as one all-or-nothing step, so the indices are ALWAYS consistent with the rows. Reads are
 * lock-free against the row store (the commit-point watermark, §2.4); writers serialize on the mutex.
 *
 * @since 0.3.1
 */
exp type Table<Row> = class {
    /** The single shared row store (chunk-chain + directory). */
    intern store: RowStore<Row>
    /** The registered indices over `store`; each is `Index<Row, K_i>` erased to a common handle (§9 note). */
    intern indices: []IndexHandle
    /** The table's fine mutex word: a `u64` slot passed to `mtx_lock`/`mtx_unlock` (src/runtime/sync.tks:63). */
    intern mtx: u64
    /** A monotone version counter, bumped on every committed transaction — the epoch a lock-free reader snapshots. */
    intern epoch: u64
}
```

> **§9 note (erasure):** a `Table` holds indices of *different* `K` per column, which a homogeneous
> `[]Index<Row, K>` cannot express. Resolve exactly as the row-tuple family does: either (a) the columns'
> index set is *itself* generated by the comptime family (each arity knows its `K_i` statically — Fork A's
> natural shape), or (b) an `IndexHandle` interface erases the key type behind the transaction operations
> (`ix_insert(RowId)`, `ix_remove(RowId)`, `ix_seek(...)`) that take the row and re-extract the key
> internally. Recommend (a) under Fork A (fully static, no erasure), (b) as the Fork B fallback. This is a
> known, bounded design point, not a blocker — called out so the implementer picks per the chosen fork.

### 2.4 The novel capability — the ATOMIC multi-index transaction

This is the only genuinely new design; everything above is reuse. A mutation must change the row **and**
every index entry as one atomic, thread-safe step so that **an index never disagrees with the rows**.

**FORK — the atomicity mechanism:**

- **Fork 1 (fine mutex + commit-point watermark) — RECOMMENDED.** All writers serialize on the table's
  fine mutex (`mtx_lock`, `src/runtime/sync.tks:63`). Within the critical section the transaction stages
  every change, validates unique constraints, then **publishes by a single commit step** — bumping the
  chunk `used` watermark (for an insert) or clearing the `live` bit (for a delete) **last**, after the row
  bytes and index entries are in place. Lock-free readers key off that watermark/live-bit, so they never
  observe a half-written row or a half-updated index. Deferred reclaim (§4.1 / §7.8 retain, epoch-gated)
  frees tombstoned slots once no reader in an older epoch remains. This matches the sealed TS model exactly
  (fine mutex + deferred reclaim) and is correct, simple, and serializes only *writers*.
- **Fork 2 (CoW snapshot swap).** Build a new index set + row view, then swap an atomic root pointer
  (`teko::sys::atomic_cas`), deferring the old under readers (epoch). Lock-free readers *and* writers, but
  each write copies index structure — only worth it read-mostly. Recommend as an opt-in `mode` **later**,
  not v1.

**Recommendation: Fork 1.** It is the settled model, needs no new primitive, and gives lock-free reads
already. The precise all-or-nothing contract:

```teko
/**
 * insert — atomically add `row` to the table and to EVERY index, or abort with `error` (leaving the table
 * untouched) if any `Unique` index already holds `row`'s key. Runs under the table's fine mutex; the row
 * slot and all index entries are staged first, unique constraints validated, then a SINGLE commit step
 * (bump the chunk watermark) publishes the row to lock-free readers. All-or-nothing: on a unique-violation
 * abort NOTHING was committed (the watermark was not bumped, the index puts were not applied), so there is
 * nothing to roll back.
 *
 * @param t    the table to insert into
 * @param row  the row value to store (value columns are deep-copied into the row-store bucket)
 * @return     the new row's stable id, or an error on a unique-index violation
 * @throws error when a Unique index already contains `row`'s projected key
 * @since 0.3.1
 */
exp fn insert<Row>(ref t: Table<Row>, row: Row): RowId | error

/**
 * update — atomically overwrite the row at `id`: rewrite its fields IN PLACE (the slot address, hence the
 * RowId, is unchanged, so indices on unchanged columns are untouched) and, for every index whose projected
 * key differs between old and new, remove the old key->id and insert the new. Runs under the fine mutex,
 * validates Unique constraints on the changed indices, commits or aborts whole.
 *
 * @param t       the table to mutate
 * @param id      the stable id of the row to update
 * @param newrow  the replacement row value
 * @return        null on success, or an error on a unique-index violation / a stale (tombstoned) id
 * @throws error  on a Unique violation for a changed key, or when `id` is not live
 * @since 0.3.1
 */
exp fn update<Row>(ref t: Table<Row>, id: RowId, newrow: Row): error | null

/**
 * delete — atomically remove the row at `id` from every index and tombstone its slot (clear the `live`
 * bit; the slot is reclaimed later by the epoch-gated deferred sweep, never moved). Runs under the fine
 * mutex. Idempotent-safe: deleting an already-tombstoned id is a no-op success.
 *
 * @param t   the table to mutate
 * @param id  the stable id of the row to remove
 * @return    null on success (including a no-op on an already-deleted id)
 * @since 0.3.1
 */
exp fn delete<Row>(ref t: Table<Row>, id: RowId): error | null
```

**Why the watermark is the commit point.** The row store's `used`/`live` are the exact analogue of the
io-streaming `count` boundary (`io-streaming-0.3.1.md:110`, "the return is the exact frontier") and the R9
watermark (`colecoes-remodelagem-…:39`, "presence = watermark `count`"): a slot is *invisible* to readers
until the watermark says it is live. Writing the row bytes + index entries *before* bumping the watermark
makes the whole insert atomic to a lock-free reader with **no reader lock** — the single monotone store to
`used` is the linearization point. The `epoch` bump lets deferred reclaim know when tombstones are safe.

**Roll-back is trivial by construction:** because the commit is the last single store, an abort (unique
violation) simply *does not perform it* — the staged row bytes sit in an unpublished slot (overwritten by
the next insert) and the index puts were validated-before-applied, so nothing is half-done. No undo log
needed. This is the cleanest all-or-nothing shape available and it falls straight out of the watermark
model — a strong signal the composition is right.

---

## 3. DELIVERABLE 2 — the SQL/LINQ query surface + the auxiliary objects

### 3.1 FORK — LINQ-style typed chaining vs a SQL-string front-end

- **Fork A (LINQ-style method chaining) — RECOMMENDED as PRIMARY.** A fluent, **compile-time-typed** query
  built from `func<Row, …>` delegates (§9E, landed): no string parsing, composable, each stage's element
  type known statically, errors caught by the checker. Idiomatic for a typed language; reuses the delegate
  machine already reseeded.
- **Fork B (SQL-string front-end).** A `query(t, "SELECT … WHERE …")` parsed at runtime (or comptime via
  §14). Familiar, but: untyped at the boundary (a typo is a runtime error, not a checker error), needs a
  parser + planner + a value/row dynamic representation that fights the monomorphized static rows, and
  duplicates the LINQ semantics underneath. **Recommend as an OPTIONAL later layer** that lowers onto the
  LINQ core (parse once, emit the same `Query` chain) — never the primary.

**Recommendation: LINQ-typed primary, SQL-string as an optional thin front-end that desugars to the LINQ
chain.** Rationale is law-aligned: Teko is statically typed and monomorphized; a string front-end would
require a dynamic value model (a `Value` union of every column type) that is exactly what the type system
exists to avoid, and it cannot type-check projections against `Row`. The typed chain is the idiomatic,
composable, checkable surface; a SQL string, if ever wanted, is sugar over it.

### 3.2 The auxiliary objects (the owner's "subconjunto de objetos auxiliares")

```teko
/**
 * Query<Row> — a LAZY query pipeline over a table: a descriptor holding the source (a table + an optional
 * chosen index) and an ordered chain of stages (where/select/order_by/…). Nothing executes until a
 * terminal op (§3.3) pulls it. Each combinator returns a NEW Query (immutable, composable); a `select`
 * that projects to `P` returns a `Query<P>`. Built from typed `func<…>` delegates, so the whole chain is
 * compile-time typed with no string parsing.
 *
 * @since 0.3.1
 */
exp type Query<Row> = class {
    /** The source table this query reads (borrowed; the query does not own it). */
    intern src: ref Table<Row>
    /** The ordered pipeline stages; each is a typed Stage (§3.4). */
    intern stages: []Stage
    /** The planner's chosen access path: a named index to seek, or a full row-store scan (§3.5). */
    intern plan: AccessPath
}

/**
 * Cursor<Row> — the pull-based ResultSet: a live iterator over the query's output, implementing the
 * settled `Iterator<T>` protocol (`src/iter/iter.tks:2`, `fn next(): Row | null`). It walks the chosen
 * access path (index seek or chunk-chain scan), applying the lazy stages per element, yielding rows one at
 * a time and `null` at exhaustion. It snapshots the table `epoch` on creation and checks the `live` bit as
 * it reads, so a row tombstoned mid-iteration is skipped (never a UAF — the slot is epoch-retained).
 *
 * @since 0.3.1
 */
exp type Cursor<Row> = class {
    /** The query being driven. */
    intern q: Query<Row>
    /** The current chunk ordinal + slot of the scan cursor (for the full-scan path). */
    intern at: RowId
    /** The epoch snapshotted at creation, gating deferred reclaim against this reader. */
    intern epoch: u64
}

/**
 * Predicate<Row> — a filter form. Two shapes: the OPAQUE closure form `func<Row, bool>` (fully general,
 * but the planner cannot introspect it → full scan unless an index is hinted), and the STRUCTURED form
 * `PredEq`/`PredRange` (a column ordinal + a key/bound) which the planner CAN match to an index (§3.5).
 * The structured form is the index-aware path; the closure is the escape hatch.
 *
 * @since 0.3.1
 */
exp type Predicate<Row> = PredClosure<Row> | PredEq<Row> | PredRange<Row>

/**
 * Aggregate<Row, A> — a fold accumulator: a seed `A`, a step `func<A, Row, A>`, and a finish `func<A, R>`.
 * count/sum/min/max/avg are built by supplying these three; avg carries `(sum, n)` in `A` and divides at
 * finish. Runs eagerly at the terminal (§3.3), pulling the cursor once.
 *
 * @since 0.3.1
 */
exp type Aggregate<Row, A> = struct {
    /** The initial accumulator (e.g. 0 for sum, the first element for min/max). */
    seed: A
    /** The per-row fold step. */
    step: func<A, Row, A>
}

/**
 * IndexHint — an explicit planner override: force a named index (`use_index`) or force a full scan
 * (`scan`). Optional; the planner (§3.5) chooses automatically from the structured predicates when no hint
 * is given.
 *
 * @since 0.3.1
 */
exp type IndexHint = UseIndex | ForceScan
```

### 3.3 The fluent operators (the stdlib op-set the owner required)

Each is a `pub fn` on `Query<Row>`; lazy combinators return a new `Query`, terminal ops drive the cursor.

```teko
/**
 * where — keep only rows satisfying `p`. A structured `PredEq`/`PredRange` lets the planner seek an index
 * (§3.5); an opaque `func<Row, bool>` closure filters the driven path element-by-element. Lazy: returns a
 * new Query, executes nothing.
 *
 * @param q  the source query
 * @param p  the predicate (structured for index-awareness, or a closure escape hatch)
 * @return   a new query with the filter appended
 * @since 0.3.1
 */
exp fn where<Row>(q: Query<Row>, p: Predicate<Row>): Query<Row>

/**
 * select — project each row to `P` via a typed projection. Returns a `Query<P>`; the element type changes
 * at compile time. Lazy.
 *
 * @param q     the source query
 * @param proj  the row-to-P projection (a §9E delegate)
 * @return      a new query of the projected element type
 * @since 0.3.1
 */
exp fn select<Row, P>(q: Query<Row>, proj: func<Row, P>): Query<P>

/**
 * order_by — sort the output ascending by `key: K (IOrd)`. If a Range index exists on that key the planner
 * iterates it in order (no sort); otherwise this is a PIPELINE BREAKER (§3.4) that buffers into a
 * chunk-chain and sorts. Lazy in construction, eager at the break.
 *
 * @param q    the source query
 * @param key  the ordering key projection
 * @return     a new ordered query
 * @since 0.3.1
 */
exp fn order_by<Row, K>(q: Query<Row>, key: func<Row, K>): Query<Row>

/**
 * group_by — partition rows by `key: K` into groups. A PIPELINE BREAKER: buffers all rows, builds a
 * `Dictionary<K, RowIdBag>`, yields one `Group<K, Row>` per distinct key. Lazy in construction.
 *
 * @param q    the source query
 * @param key  the grouping key projection
 * @return     a query of groups
 * @since 0.3.1
 */
exp fn group_by<Row, K>(q: Query<Row>, key: func<Row, K>): Query<Group<Row, K>>

/**
 * join — inner-join `q` with `other` on matching keys `lk`/`rk`, projecting each matched pair to `P`. The
 * planner seeds the right side from a Hash index on `rk` when one exists (hash join); otherwise it builds a
 * transient hash of `other` (a chunk-chain-backed Dictionary). A PIPELINE BREAKER on the build side.
 *
 * @param q      the left query
 * @param other  the right table
 * @param lk     the left key projection
 * @param rk     the right key projection
 * @param proj   the matched-pair projection to the result element
 * @return       a query of joined results
 * @since 0.3.1
 */
exp fn join<Row, Other, K, P>(q: Query<Row>, other: ref Table<Other>, lk: func<Row, K>, rk: func<Other, K>, proj: func<Row, Other, P>): Query<P>

/**
 * distinct — drop duplicate rows by `K`. A PIPELINE BREAKER backed by a `HashSet<K>` seen-set. Lazy build.
 *
 * @param q    the source query
 * @param key  the distinctness key projection
 * @return     a de-duplicated query
 * @since 0.3.1
 */
exp fn distinct<Row, K>(q: Query<Row>, key: func<Row, K>): Query<Row>

/**
 * limit / skip — bound the output: `skip(n)` discards the first `n`, `limit(n)` stops after `n`. Both are
 * lazy and streaming (no buffering) — pure cursor arithmetic.
 *
 * @param q  the source query
 * @param n  the count to skip / the max to yield
 * @return   a bounded query
 * @since 0.3.1
 */
exp fn limit<Row>(q: Query<Row>, n: u64): Query<Row>
exp fn skip<Row>(q: Query<Row>, n: u64): Query<Row>

/**
 * to_array — TERMINAL: drive the cursor to exhaustion, materializing an independent `[]Row` snapshot (a
 * fresh fixed array, never a view over the row store — a view would dangle when the store later reclaims a
 * tombstone, exactly the R9 to_array correction, `colecoes-remodelagem-…:172`).
 *
 * @param q  the query to run
 * @return   a fresh snapshot of the result rows
 * @since 0.3.1
 */
exp fn to_array<Row>(q: Query<Row>): []Row

/**
 * first / single / count — TERMINALS. `first` yields the first matching row or null; `single` yields it or
 * an error if zero-or-many; `count` folds the cursor to a tally. Each drives the cursor lazily and stops as
 * soon as the answer is known (`first` after one, `single` after two).
 *
 * @param q  the query to run
 * @return   the terminal result (row|null, row|error, or a u64 count)
 * @since 0.3.1
 */
exp fn first<Row>(q: Query<Row>): Row | null
exp fn single<Row>(q: Query<Row>): Row | error
exp fn count<Row>(q: Query<Row>): u64

/**
 * aggregate / sum / min / max / avg — TERMINALS folding the cursor once through an `Aggregate<Row, A>`.
 * `sum`/`min`/`max`/`avg` are thin wrappers supplying the seed+step for a chosen numeric column projection.
 *
 * @param q    the query to run
 * @param agg  the fold (seed + step)
 * @return     the folded accumulator
 * @since 0.3.1
 */
exp fn aggregate<Row, A>(q: Query<Row>, agg: Aggregate<Row, A>): A
```

### 3.4 Lazy vs eager — FORK + recommendation

- **RECOMMENDED: lazy pull (cursor) by default.** `where`/`select`/`limit`/`skip` stream element-by-element
  through the `Cursor` with **no intermediate materialization** — the same "zero acumulador" discipline the
  io-streaming decree enforces (`io-streaming-0.3.1.md:6`). Terminals (`to_array`, `count`, aggregates)
  pull. This is composable and memory-lean.
- **Eager only at PIPELINE BREAKERS.** `order_by` (needs all rows to sort), `group_by`, `distinct`, and the
  build side of `join` intrinsically require the whole set — they **buffer into a chunk-chain** (a `List`
  in the R9 fixed-backing sense), then resume lazy downstream. This is stated per-operator above.

**Recommendation: lazy-by-default, break-only-where-forced.** No global eager mode; the breakers are the
minimal, named set. This maximizes streaming and matches the language-wide "no growing accumulator" law.

### 3.5 The planner — index selection (FORK + the closure-opacity counter-argument)

**The tension (counter-argument to a naive planner).** A planner wants to see `where(row -> row.age == 30)`
and pick the hash index on `age`. **But `func<Row, bool>` is OPAQUE** — the planner cannot introspect a
monomorphized closure body to learn it is an equality on column *k*. Pretending to would require reflecting
over compiled closure IR, which the language does not offer and which would be fragile.

**Resolution (law-first): two predicate forms, only the structured one is index-matchable.**
- The **structured** predicates `PredEq(col, key)` and `PredRange(col, lo, hi)` carry the column ordinal and
  the key/bounds as DATA the planner reads directly — no introspection. `where_eq(t, col, key)` and
  `where_range(t, col, lo, hi)` are the index-aware entry points.
- The **closure** predicate `func<Row, bool>` is the general escape hatch: the planner treats it as a
  non-seekable filter and applies it over whatever access path the *structured* predicates (or a
  `use_index` hint) chose — full scan if nothing else narrows.

**Selection rule (simple rule-based v1 — RECOMMENDED over a cost-based planner):**
1. If a `PredEq(col, key)` matches a `Hash` index on `col` → **seek** it (point/O(1)); feed only those
   RowIds downstream.
2. Else if a `PredRange(col, lo, hi)` matches a `Range` index on `col` → **range-scan** the SortedSet
   between bounds.
3. Else if `order_by(key)` matches a `Range` index on `key` → **iterate the index in order**, skipping the
   sort entirely.
4. Else → **full chunk-chain scan**, applying every predicate (structured or closure) per row.
5. A `use_index(name)` hint overrides 1-4; a `ForceScan` hint forces 4.

A cost-based planner (statistics, selectivity) is the documented **later** evolution (ADJACENT — reported,
not opened as an issue). Rule-based is correct and predictable for the "small datasets with special
handling" the owner targets.

---

## 4. DELIVERABLE 3 — the IO-backed variant ("SQLite-lite", single table)

The owner: *"como se fosse SQLite, mas é uma única tabela, para dados não muito longos mas que precisam de
algum tratamento especial."* A **persistent single-table** store on a **binary file**, reusing the
io-streaming syscall layer — persistence + indexed query for small datasets. **Single table only — not a
DB.**

### 4.1 The dependency + the reuse rule (counter-argument to inventing a second IO surface)

The file variant **reuses io-streaming's declared surface** — `FileStream`, `open_read`/`open_write`/
`open_append`, `stream_read`/`stream_write`/`stream_seek`/`stream_close`, `stat`/`file_size`, `CHUNK ≤
1024` (`io-streaming-0.3.1.md` §2.1-2.4). It invents **no** second IO surface (the sealed decree,
`io-streaming-0.3.1.md:6`, "estritamente STREAM"). **io-streaming is itself unbuilt** — this variant is
**design-ahead against its DECLARED shape** and resumes in minutes when io-streaming lands. Noted as the
one hard dependency.

### 4.2 FORK — the file format: persist indices vs rebuild-on-load

- **RECOMMENDED (v1): header + typed rows, REBUILD indices on load.** The file is `[header][rows…]`;
  indices are NOT persisted — on `open` the in-memory `Table` is rebuilt by streaming the rows and calling
  `insert` (which populates every registered index). O(n) load, trivially correct (no on-disk index
  consistency problem), tiny format. Ideal for "dados não muito longos".
- **Alternative: persist indices too** (`[header][rows…][index blocks…]`). Faster load for larger sets, but
  the format must keep on-disk indices consistent with rows (a torn write corrupts them) and is far more
  format surface. **Recommend deferring** — it is the large-dataset optimization the owner explicitly says
  this is NOT for.

**Recommendation: rebuild-on-load.** It matches the small-data framing, keeps the format minimal, and makes
the indices *derived* (never a persistence bug). Persisted indices are a later evolution.

### 4.3 FORK — mmap vs stream-read

- **RECOMMENDED: stream-read via io-streaming.** Load by `open_read` + a `CHUNK`-sized reusable buffer +
  `stream_read` loop (`io-streaming-0.3.1.md:159`, the `read_stream` idiom). Reuses the sole IO surface,
  no new syscall, portable across the three OSes io-streaming already handles.
- **Alternative: mmap the file.** `os_mmap` exists (`src/runtime/arena.tks`) but for *anonymous arena
  pages*, not file mappings — using it for files would be a **second IO surface**, which the io-streaming
  decree bans. **Reject** on law grounds.

**Recommendation: stream-read.** mmap loses on the "one IO surface" law regardless of any performance
argument, and for small data the difference is negligible.

### 4.4 FORK — write-back-on-change vs explicit save

- **RECOMMENDED: explicit `save(path)` + an OPTIONAL append-only op-log.** `save` serializes the whole live
  table (header + live rows, tombstones dropped) via `write_stream`. For durability between saves, an
  optional append-only journal appends each committed transaction via `append_stream`
  (`io-streaming-0.3.1.md:157`) — replayed on load after the base snapshot.
- **Alternative: write-back on every mutation.** Thrashes the file, fights the small-data framing, and
  couples every `insert` to an fsync. **Reject** as the default.

**Recommendation: explicit save (whole-table snapshot), append-only journal optional.** Matches "special
handling for small data" without turning a single-table cache into a write-heavy DB.

### 4.5 The FileTable shape + the serialization dependency

```teko
/**
 * FileTable<Row> — a persistent single-table store: an in-memory `Table<Row>` (§2) plus a backing file
 * path. `open` streams the rows off disk (io-streaming `read_stream` idiom) and REBUILDS the indices by
 * re-inserting (§4.2); `save` serializes the live rows back via `stream_write`. All in-memory queries and
 * atomic transactions are the §2/§3 machinery unchanged — persistence is a load/save skin, not a second
 * engine.
 *
 * @since 0.3.1
 */
exp type FileTable<Row> = class {
    /** The in-memory table doing all the real work (indices, transactions, queries). */
    intern mem: Table<Row>
    /** The backing file path. */
    intern path: str
    /** True once the in-memory state has uncommitted changes not yet `save`d (drives an optional autosave). */
    intern dirty: bool
}

/**
 * open — load a FileTable from `path`: read the header (magic, version, column type tags, row count),
 * stream each typed row via the io-streaming CHUNK loop, `insert` it into a fresh in-memory Table (which
 * rebuilds every index), and return the ready table. A missing file yields an empty table at `path`.
 *
 * @param path     the backing file
 * @param indices  the index descriptors to (re)build over the loaded rows
 * @return         the loaded table, or an error on a corrupt header / read failure
 * @throws error   on bad magic, unsupported version, a short/garbled row, or an io-streaming read error
 * @since 0.3.1
 */
exp fn open<Row>(path: str, indices: []IndexHandle): FileTable<Row> | error

/**
 * save — serialize the live rows of a FileTable to its backing file: write the header, then each live row
 * (tombstones dropped) via `stream_write` in CHUNK-bounded pieces (io-streaming §2.3). Indices are NOT
 * written (rebuilt on the next open). Clears `dirty`.
 *
 * @param ft  the table to persist
 * @return    null on success, or the first io-streaming write error
 * @throws error on an underlying write failure
 * @since 0.3.1
 */
exp fn save<Row>(ref ft: FileTable<Row>): error | null
```

**The binary format (v1, rebuild-on-load):**

```
┌─ header ──────────────────────────────────────────────────────────────────┐
│ magic:   [8]byte  = "TEKOTBL1"                                             │
│ version: u32                                                               │
│ ncols:   u32      (the arity n, ≤16 — the type-arg ceiling, §1)           │
│ coltags: [n]u32   (a per-column type tag; see the serializer dependency)  │
│ nrows:   u64      (live row count)                                        │
└───────────────────────────────────────────────────────────────────────────┘
┌─ rows (nrows of them) ────────────────────────────────────────────────────┐
│ each row = the n columns serialized in order:                             │
│   fixed-width numeric column  -> its little-endian bytes                  │
│   str / variable column       -> u32 length prefix + the bytes            │
└───────────────────────────────────────────────────────────────────────────┘
```

**Serialization dependency (design-ahead, blocked piece).** Serializing a typed row needs a per-column
`col_write(col) -> []byte` / `col_read(bytes) -> col`. Two routes, both design-ahead:
- **RECOMMENDED when it lands: comptime field reflection** (`serial-tags-comptime-field-reflection-0.3.1.md`)
  — the compiler emits the per-column codec from the row tuple's static types. Zero user code. **Blocked on
  serial-tags being built** — noted.
- **Fallback available today: injected codecs** — `open`/`save` take a `func<Row, []byte>` /
  `func<[]byte, Row>` pair supplied by the caller. Compiles now against the §9E delegates; no serial-tags
  dependency. Recommend shipping the fallback first, swapping to reflection when serial-tags lands.

---

## 5. Crumb sequence + ritual points

Smallest independently gate-able steps. `[dry]` = compiles + trivial fixpoint (no emit consumer);
`[RITUAL]` = full gate (build gen2, isolated regressions green, FIXPOINT gen2==gen3). **No crumb here
touches the compiler self-build** — Table is new leaf stdlib the compiler-core does not consume — so most
are `[dry]`; only a reseed to publish the new stdlib symbols is a ritual.

1. **`RowId` + `RowChunk`/`RowStore` + the directory** (§2.1). Leaf types, no consumer. `[dry]`.
2. **`ensure_cap`/directory growth reuse** — reuse R9's `ensure_cap` (`colecoes-remodelagem-…:141`) for the
   directory; the chunk backings are `of_len`. `[dry]`. *(Depends on R9 step-1 `of_len`+index-assign being
   seeded — the SAME dependency the whole fixed-backing remodel carries; noted, not re-derived.)*
3. **`Index<Row,K>` + `IndexKind`/`Uniqueness`** over `Dictionary`/`SortedDictionary` (§2.2). `[dry]`.
4. **`Table<Row>` + the atomic transaction** `insert`/`update`/`delete` under `mtx_lock` (§2.3-2.4). `[dry]`
   (leaf; the fine mutex is already landed in `src/runtime/sync.tks`).
5. **The comptime family generation** (arities 1..16, Fork A) via §14 parametric macros, OR the hand-written
   template family. `[dry]` per arity; a **[RITUAL]** to reseed once the family + the transaction publish new
   stdlib symbols the corpus can name.
6. **`Query<Row>`/`Cursor<Row>` + the lazy operators** (§3.2-3.3). `[dry]`.
7. **The rule-based planner + structured predicates** (§3.5). `[dry]`.
8. **`FileTable` open/save + the format** with the **injected-codec fallback** (§4.5). `[dry]`. *(Blocked
   pieces: io-streaming's surface and serial-tags — design-ahead; see §7.)*
9. **Swap the injected codec for comptime reflection** when serial-tags lands. `[RITUAL]` if it changes
   emitted symbols. *(Blocked on serial-tags.)*

**Ritual points:** crumb 5's reseed (publishing the Table family + transaction into the seed so the corpus
may use them) and crumb 9 (if reflection changes emit). Ordering: 1→2→3→4 (the core), 5 (family/reseed),
6→7 (query), 8→9 (IO). Bootstrap-safe: no crumb teaches an idiom the seed lacks — Table depends on the R9
`of_len`+index-assign step-1 (crumb 2's noted dependency) and the §9E delegates (already seeded).

---

## 6. Regression fixtures (`.tkr`, ISOLATED, native exit code — spec, do NOT run here)

Each runs ISOLATED (never `teko test .` — the monomorph leak crashes the container, hard rule). The
`exit`/token encodes WHICH branch ran (axis-law: test the value, never an incidental effect).

| fixture | proof | expected exit |
|---|---|---|
| `table_insert_get` | insert N rows, `get` each by primary hash index; all match | 0 |
| `table_multi_index_consistent` | two indices (hash on col0, range on col1); after N inserts BOTH indices return the same row set | 0 |
| `table_atomic_unique_abort` | insert a duplicate key on a `Unique` index → error branch; table unchanged (count, both indices intact) | exit own (e.g. 40) |
| `table_update_stable_rowid` | update a non-indexed column; RowId unchanged, other indices untouched, changed index moved | 0 |
| `table_delete_tombstone` | delete; the row vanishes from every index; a re-scan skips the tombstone; count correct | 0 |
| `table_rowid_stable_across_growth` | insert past `CHUNK_ROWS` (force new chunks + directory grow); old RowIds still resolve | 0 |
| `query_where_index_seek` | `where_eq` on an indexed col; result equals the brute-force filter (planner picked the index) | 0 |
| `query_where_closure_scan` | `where(func closure)`; result equals brute-force (full-scan escape hatch correct) | 0 |
| `query_order_by_index` | `order_by` on a Range-indexed col; output ascending, no sort taken | 0 |
| `query_group_by_aggregate` | `group_by` + `sum` per group; group sums match a hand tally | 0 |
| `query_join_hash` | inner `join` two tables on a hash-indexed key; matched pairs exact | 0 |
| `query_lazy_limit` | `where...limit(3)` pulls only until 3 are yielded (a counter proves early stop) | 0 |
| `query_distinct` | `distinct` de-dups by key; count correct | 0 |
| `filetable_roundtrip` | build a table, `save`, `open`, re-query; every row + every index equal (rebuild-on-load correct) | 0 |
| `filetable_missing_open` | `open` a nonexistent path → empty table (not an error) | 0 |
| `filetable_corrupt_header` | `open` a file with bad magic → error branch | exit own (e.g. 41) |
| `filetable_chunk_boundary` | rows straddling the io-streaming CHUNK=1024 boundary round-trip byte-identical | 0 |

---

## 7. Blocked pieces (design-ahead) — what resumes in minutes when the deps close

- **crumb 2** depends on R9 step-1 (`of_len<T>` + index-assign + the `count` idiom,
  `colecoes-remodelagem-…:537`) being seeded — the SAME prerequisite the whole fixed-backing collection
  remodel carries. Everything else in crumbs 1-7 compiles against LANDED surface (the delegates, the fine
  mutex, `Dictionary`/`SortedDictionary`, the cmp interfaces).
- **crumb 8 (FileTable)** design-aheads against **io-streaming's DECLARED surface** (`io-streaming-0.3.1.md`
  §2 — itself unbuilt). The format, load/save flow, and the injected-codec fallback are written against that
  declared shape; the implementer wires the real `stream_read`/`stream_write` when io-streaming lands. **The
  injected-codec route compiles today** (only §9E delegates) — so FileTable is buildable ahead of
  serial-tags, just not with reflection.
- **crumb 9 (comptime codec)** depends on `serial-tags-comptime-field-reflection-0.3.1.md`. Until then, the
  injected-codec fallback (crumb 8) is the shipping path.
- **Nothing here blocks §16, §10-(c), or the arena** (owner, `mudancas-superficie-0.3.1.md:1629`). Table is
  fase1c, an independent stdlib lane; it is design-complete now and buildable when build capacity frees.

---

## 8. Risks + law tensions (law-first) — SEM HALT

- **T-1 — `Table<C1..Cn>` variadic vs no variadic generics.** RESOLVED (§1). Realized as a comptime family
  `Table1..Table16` (Fork A), the func/action precedent — variadic surface, monomorphic underneath;
  positional-typed columns at the ≤16 ceiling. Fork B (row-struct single-generic) is the documented
  alternative. Counter-argued, not silenced. **Not a HALT.**
- **T-2 — index heterogeneity vs a homogeneous `[]Index`.** RESOLVED (§2.3 note). Either the comptime family
  knows each `K_i` statically (Fork A, no erasure), or an `IndexHandle` interface erases the key type behind
  transaction ops (Fork B). Bounded design point, per chosen fork. **Not a HALT.**
- **T-3 — growable-array-by-swap (banned) vs multi-index.** RESOLVED (§0, §2.1). The composition is FORCED
  onto chunk-chain + a fixed directory precisely because indices must address rows by a stable `RowId`; a
  backing swap would dangle every index. This is why the banned shape is not merely disallowed but
  structurally wrong here. **Not a HALT.**
- **T-4 — the atomic transaction vs the TS reclaim model.** RESOLVED (§2.4). Writers serialize on the fine
  mutex; the single-store commit-point watermark makes inserts atomic to lock-free readers with no reader
  lock; tombstones are epoch-retained then swept (the §4.1/§7.8 deferred reclaim). All arrays stay fixed;
  the old directory is retained-immortal, not dropped (cross-thread-safety requirement, not a fixed-array
  relaxation). **Not a HALT.**
- **T-5 — planner index selection vs closure opacity.** RESOLVED (§3.5). A closure `func<Row, bool>` is
  opaque and NOT introspected; index-awareness comes from the structured `PredEq`/`PredRange` forms the
  planner reads as data; the closure is the full-scan escape hatch. No pretend-introspection. **Not a HALT.**
- **T-6 — SQL-string front-end vs the static type system.** RESOLVED (§3.1). LINQ-typed chaining is primary
  (composable, checkable, reuses the delegate machine); a SQL string would need a dynamic value model the
  type system exists to avoid, so it is an OPTIONAL later front-end that desugars to the LINQ core, never the
  primary. **Not a HALT.**
- **T-7 — a second IO surface (mmap) vs the io-streaming decree.** RESOLVED (§4.3). File load/save reuses
  io-streaming's stream primitives exclusively; mmap-for-files is rejected on the "one IO surface" law
  regardless of performance. **Not a HALT.**

**SEM HALT.** Every tension resolves law-first without relaxing a sealed law: the ≤16 ceiling is honoured by
a comptime family; the TS model is honoured by the fine mutex + watermark commit + deferred reclaim; the
fixed-array law is honoured (every chunk/directory is a fixed array, growth allocates a new one, the old is
retained-immortal only where cross-thread safety demands); the one IO surface is honoured by reusing
io-streaming; closure opacity is honoured by the structured-predicate planner. Table is a COMPOSITION plus
one novel, precisely-specified capability (the atomic multi-index transaction), not a reinvented structure.
```
