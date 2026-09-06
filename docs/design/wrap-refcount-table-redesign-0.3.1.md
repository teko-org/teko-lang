# Wrap-refcount table — root redesign (Findings 1+2+5, D63) — 0.3.1

Design-ahead spec for the root redesign of the `wrapped`-kind reference-count table, ratified by
DECISION_LOG **D63** (supersedes the D61 deferral). It resolves, in ONE structure, the three defects
the COL-F0c review (a31e3bcf) found in the current `addr→count` table (`src/runtime/arena.tks:763-845`):

- **Finding 1 — not cross-thread.** The table + its lock live inside the `_Thread_local` arena control
  block (`teko_rt.c:2320` `tk_g_arena_control`), so each thread owns a private table; a `retain` on one
  thread and a `release` on another never meet → leak / UAF, and `mtx_lock` guards nothing cross-thread.
- **Finding 2 — silent saturation.** `ar_wrap_inc` does `if n >= WRAP_TABLE_CAP { return }` — above 4096
  live wrapped objects it drops the increment with no panic → leak / corruption.
- **Finding 5 — O(n²).** `ar_wrap_find` is an O(n) linear scan under a global lock; every op is
  O(live-objects) and fully serialized.

Also folded in: **Finding 3** — the implicit obj-ptr-vs-region-ptr contract (`region_drop` wants a REGION
pointer; the code passes the OBJECT pointer, and it only happens to work because the fixture allocates the
object AS a region).

The whole mechanism is INERT today (no `src/` caller holds a `wrapped` object; the compiler's own
collections hold VALUE and `class`). The `[dry]` build stays byte-identical; the seed folds into SM-R1.
Concurrency itself is gated on the memory milestone (D52) — this spec designs the correct end-state so the
implementer lands it once F0d (`chunk_node_link`) and the Finding-4 cleanup drain.

---

## 1. Model (one paragraph)

An **open-addressing hash table keyed by pointer**, anchored in a **process-global** root (not the
per-thread arena control), grown without any dynamic array by **linking fixed bucket-chunks** through the
F0d `chunk_node_link` primitive, and operated **lock-free on the hot path** (count inc/dec via 32-bit CAS)
with a **structural lock only for insert / remove / chunk-link**. The table is a fixed **directory** of
power-of-two slots; each slot owns a **chain of fixed bucket-chunks** (linked, never rehashed, never
whole-backing-swapped); a pointer hashes (Fibonacci) to a directory slot and linear-probes its chunk chain.
Capacity is unbounded via chunk-linking, so silent saturation is gone — the only failure is OOM, which is
already fail-loud (`ar_oom`). The bucket stores the object's **region pointer** as the key, making the
lifetime unit explicit: at count zero the runtime drops exactly that region (Finding 3 honored by
construction).

---

## 2. Structure layout

### 2.1 Bucket (16 bytes, unchanged width)

| offset | width | field | access | meaning |
|---|---|---|---|---|
| 0  | 8 | `WRAP_ENTRY_ADDR`  | 64-bit, lock-published | the KEY = the wrapped object's **region pointer** (F2 per-object region). 0 in an empty bucket. |
| 8  | 4 | `WRAP_ENTRY_STATE` | 32-bit atomic | `0`=EMPTY, `1`=OCCUPIED, `2`=TOMBSTONE (deleted, probe-through). |
| 12 | 4 | `WRAP_ENTRY_COUNT` | 32-bit atomic | the reference count. Meaningful only while STATE==OCCUPIED. |

The 32-bit `STATE`/`COUNT` sit at 4-aligned offsets 8 and 12 inside the 16-aligned bucket, so each is a
valid independent target for `teko::sys::atomic_*_u32`. The 64-bit `ADDR` is read/written with
`teko::mem::load_u64`/`store_u64` (there is no 64-bit atomic primitive; safety comes from the publication
protocol in §4.3, not from atomicity of the key load).

Rationale for the split: the available atomics are **u32-only** (`atomic_cas_u32` / `atomic_add_u32` /
`atomic_xchg_u32` / `atomic_load_u32`, `sync.tks:64`, `codegen.tks:8790`). The count and the publish gate
are 32-bit so they ride those primitives directly; the 64-bit key never needs an atomic because it only
changes under the structural lock and is published via the 32-bit STATE gate.

### 2.2 Directory + bucket-chunks

- **Directory** — a single fixed `[]u64` of `WRAP_DIR_SLOTS` (power of two, e.g. `1024`) allocated ONCE
  from the wrap region (`of_len`, zero-filled). Slot `d` holds the head bucket-chunk pointer for that slot
  (0 until first insert into `d`).
- **Bucket-chunk** — a fixed block of `WRAP_BUCKETS_PER_CHUNK` (e.g. `64`) buckets, allocated from the wrap
  region and linked through the arena's intrusive chunk-list slots (`CHUNK_NEXT`/`CHUNK_CAP`/`CHUNK_USED`,
  `arena.tks:13-19`) via **`chunk_node_link`** (F0d). A directory slot owns a **chain** of these chunks.
- **Live-count** — a process-global 32-bit atomic counter of OCCUPIED buckets (diagnostic + load-factor
  input), bumped/decremented under the structural lock alongside insert/remove.

### 2.3 Hash — Fibonacci over the aligned key

Region pointers are 16-byte aligned (`ARENA_ALIGN=16`), so the low 4 bits carry no entropy. Drop them,
then multiply by the 64-bit golden-ratio constant and take the top `WRAP_DIR_BITS` bits:

```
h        = (key >> 4) * 0x9E37_79B9_7F4A_7C15        // 64-bit wrap multiply
dir_idx  = h >> (64 - WRAP_DIR_BITS)                  // top bits → directory slot
probe0   = (h >> (64 - WRAP_DIR_BITS - PROBE_BITS)) & (WRAP_BUCKETS_PER_CHUNK - 1)  // start bucket in chunk
```

Fibonacci hashing spreads even sequential allocations (the common case) across the directory in O(1).

---

## 3. Placement — process-global anchor (see FORK, §7)

The table + its lock + the live-count MUST leave the `_Thread_local` control block and live in a
**process-global root** so `retain`/`release` on different threads reach the SAME table. The natural seat
in the ratified arena model is the **program region (F2)** — the region that is on NO task's registry, one
per process, where cross-thread singletons already live (the channel transport service,
`arena-especificacao-unica-0.3.1.md:761-768`). The wrap directory, its bucket-chunks, and the lock/live
words are allocated from, and rooted in, that process-global program region.

The open architectural question is the **anchor word** that lets every thread FIND that one process-global
root without a per-thread indirection. Teko exposes no process-global mutable storage today (no module
`global` surface; the only runtime seam, `tk_arena_control`, is `_Thread_local` by construction). Resolving
this touches the arena model, so it is raised as a fork (§7). The rest of this spec is written against the
**declared shape** of the anchor:

```
/** wrap_root — the process-global base word all threads share: the address of the wrap
 *  table's root header (directory pointer + lock word + live-count), lazily initialized
 *  once under a thread-safe guard. INDEPENDENT of the per-thread arena control. */
fn wrap_root(): u64
```

Everything downstream is anchor-agnostic: the implementer binds `wrap_root()` to whatever mechanism the
owner ratifies for the fork, and no other function changes.

### 3.1 Thread-safe one-time init

`wrap_root()` double-checks: read the anchor word; if a root is already published, return it; else build
the root (allocate the directory + lock word + live-count from the program region), then publish it with a
single `atomic_cas_u32`/`atomic_xchg` on the anchor's low word (release), losers freeing their speculative
directory and re-reading the winner's. The build happens at most once per process.

---

## 4. Concurrency protocol

Two tiers. The **hot path is lock-free**; the **structural path holds one futex lock**.

### 4.1 Hot path — inc / dec of an EXISTING key (lock-free)

1. `dir_idx = hash(key)`; walk the directory slot's bucket-chunk chain, linear-probing from `probe0`.
2. Each probe: `s = atomic_load_u32(STATE)` (acquire). `s==EMPTY` → key absent, stop. `s==TOMBSTONE` →
   continue probing. `s==OCCUPIED` → read `ADDR` (`load_u64`, safe because the acquire-load of STATE
   ordered-after the writer's release-store of STATE, §4.3); if `ADDR==key`, this is the bucket.
3. **retain:** `atomic_add_u32(COUNT, 1)`. Done, no lock.
4. **release:** CAS-decrement loop — `c = atomic_load_u32(COUNT); atomic_cas_u32(COUNT, c, c-1)` until it
   sticks. If the decrement drove COUNT to 0 → enter the structural removal (§4.2, step R). A concurrent
   `retain` that raced COUNT back above 0 is caught by the re-check under the lock.

### 4.2 Structural path — insert / remove / grow (one lock)

Lock = a process-global futex word in the root (`mtx_lock`/`mtx_unlock`, `sync.tks:63`).

- **Insert** (retain of a key not found on the hot path): lock; re-probe (another thread may have inserted);
  if now present → `atomic_add_u32(COUNT,1)`, unlock. Else find the first EMPTY-or-TOMBSTONE bucket along
  the chain within the load threshold: `store_u64(ADDR, key)`; publish STATE=OCCUPIED via `atomic_xchg_u32`
  — **release, published LAST**; `atomic_xchg_u32(COUNT, 1)`; bump live-count; unlock. If the chain has no
  room under the load factor → **grow** (below) then insert into the new chunk.
- **Grow**: `chunk_node_link(wrap_region, tail_chunk, WRAP_BUCKETS_PER_CHUNK)` appends ONE fresh fixed
  bucket-chunk to this directory slot's chain. No existing chunk is touched, no rehash, no whole-backing
  swap (honoring the F0d TS-substrate rule: grow LINKS a fixed chunk). Capacity is therefore unbounded;
  the only hard failure is `chunk_node_link`'s underlying `ar_oom` — **fail-loud**, never a silent return.
- **Remove** (step R, count hit 0): lock; re-read `COUNT`; if a racing retain resurrected it (`>0`) →
  unlock, do NOT free. Else set STATE=TOMBSTONE (`atomic_xchg_u32`, release), decrement live-count, capture
  `region = ADDR`, unlock, then `region_drop(region)` OUTSIDE the lock (the drop is O(1) and must not run
  under the structural lock — resolves the review's Finding 3 "drop outside lock" note too).

### 4.3 Publication ordering (why the 64-bit key needs no atomic)

Writer (under lock): `store_u64(ADDR,key)` → `atomic_xchg_u32(STATE,OCCUPIED)` [SEQ_CST release].
Reader (lock-free): `atomic_load_u32(STATE)` [SEQ_CST acquire] → `load_u64(ADDR)`. If the reader observes
STATE==OCCUPIED, the acquire/release pair guarantees the prior `store_u64(ADDR)` is visible, so the plain
64-bit key read is never torn. TOMBSTONE and EMPTY reads never dereference ADDR. This is a standard
single-word publication gate; it is why u32-only atomics suffice for a 64-bit-keyed table.

---

## 5. Finding 3 — the ptr contract, made explicit

**Contract:** the wrap table keys on the wrapped object's **region pointer** (its F2 per-object region),
NOT the raw object pointer. `wrap_retain`/`wrap_release` receive that region pointer; the codegen emitters
(`emit_retain`/`emit_release`, `codegen.tks:2589`) pass `region(obj)` — the per-object region the emitter
allocated for the wrapped object at its birth site, which it already knows. At count zero the runtime does
`region_drop(key)` where `key` IS that region — correct by construction, no obj→region lookup needed, and
no reliance on the fixture's accidental `object == region_new(root)`. A wrapped object's lifetime UNIT is
its region (Doc-2 wrapped + COL-F0c "region_drop of the object's own region, F2, O(1)"), so keying on the
region is the honest model, not a workaround.

This is a **codegen-side contract note** for the F0c mechanism, not new runtime C: the emitter already
holds the region at the allocation site; it passes it to `wrap_retain`/`wrap_release` instead of the raw
object pointer.

---

## 6. Constants (replace the current `WRAP_*` block, `arena.tks:87-103`)

| const | value | note |
|---|---|---|
| `WRAP_DIR_SLOTS` | `1024` | directory length (power of two). |
| `WRAP_DIR_BITS` | `10` | `log2(WRAP_DIR_SLOTS)`. |
| `WRAP_BUCKETS_PER_CHUNK` | `64` | fixed buckets per linked chunk. |
| `WRAP_LOAD_NUM` / `WRAP_LOAD_DEN` | `7` / `8` | grow a slot's chain when a chunk exceeds 7/8 occupied. |
| `WRAP_ENTRY_BYTES` | `16` | unchanged. |
| `WRAP_ENTRY_ADDR` | `0` | key (region ptr). |
| `WRAP_ENTRY_STATE` | `8` | 32-bit atomic state. |
| `WRAP_ENTRY_COUNT` | `12` | 32-bit atomic count. |
| `WRAP_ST_EMPTY` / `WRAP_ST_OCCUPIED` / `WRAP_ST_TOMB` | `0` / `1` / `2` | STATE tags. |
| `WRAP_GOLDEN` | `0x9E3779B97F4A7C15` | Fibonacci multiplier. |

REMOVED: `WRAP_TABLE_CAP` (the fixed 4096 cap — replaced by unbounded chunk-linking), and the per-thread
`CTRL_WRAP_LOCK`/`CTRL_WRAP_TABLE`/`CTRL_WRAP_COUNT` control fields (the table leaves the control block).
The old linear-scan helpers `ar_wrap_find` / `ar_wrap_inc` / `ar_wrap_dec` / `ar_wrap_remove` /
`ar_wrap_table` / `ar_wrap_entry` are replaced wholesale.

---

## 7. FORK — the process-global anchor mechanism (for the owner)

D63 requires all three of: (a) **process-global** table, (b) **pure-Teko**, (c) **no new `teko_rt.c`**.
These are jointly unsatisfiable with the runtime as it stands: Teko exposes **no process-global mutable
storage** — there is no `global` declaration surface (the parser/AST have no such node), and the sole
runtime state seam, `tk_arena_control` (`teko_rt.c:2320`), is `_Thread_local` by construction. A
process-global table needs exactly ONE process-global anchor word, and the only ways to obtain it each
touch something ratified/frozen:

1. **One companion C seam word** — a non-`_Thread_local` `tk_wrap_root_get/set` (plain `static`) beside
   the existing `tk_arena_control` seam. This is the SAME architectural class as the seam the maintained-C
   exception already exists for ("exactly one mutable process word to reach that state", `teko_rt.c:2314`)
   — but D63 says "sem novo `teko_rt.c`". **Smallest, honest root-cause change; touches the frozen-C law's
   own exception.**
2. **A new Teko module-`global` static surface** — add a genuine process-global (non-thread-local) static
   binding to the language (lexer/parser/AST/checker/codegen/lir/backend). Pure-Teko and reusable, but a
   large blast radius for one word.
3. **`mmap` `MAP_FIXED` at a reserved constant address** — pure-Teko, no new C, but fragile/nonportable
   (can clobber existing mappings); rejected on safety grounds.

**Recommendation (law-first):** option 1. The maintained-C exception for `teko_rt.{c,h}` exists precisely
to hold the arena's one process word; the process-global anchor is the same one-word need, just
process-scoped instead of thread-scoped, and it is the minimal change that passes every other law (O(1),
lock-free hot path, chunk-chain growth, fail-loud, Finding-3 contract). D63's "no new `teko_rt.c`" was
written assuming a pure-Teko path exists; since none does, honoring it would force option 2 (disproportionate)
or option 3 (unsafe). If the owner prefers to keep `teko_rt.c` untouched, option 2 is the pure-Teko
fallback and should be scheduled as its own surface-teaching crumb BEFORE this redesign lands.

This fork is genuinely undecided (no DECISION_LOG / arena-spec ruling fixes the process-global anchor
mechanism), so it is escalated rather than invented. Everything else in this spec is anchor-agnostic and
proceeds against the declared `wrap_root()` shape (§3).
