---
seq: 0123
crumb-id: COL-F0c2
milestone: M1
gate: "[dry]"
reseed-class: "(folds R1)"
deps: [COL-F0c, COL-F0d]
sources:
  - "docs/design/wrap-refcount-table-redesign-0.3.1.md:1-999"     # this crumb's full spec
  - "DECISION_LOG.md:650-661"                                     # D63 ruling (root redesign 1+2+5)
  - "docs/design/arena-especificacao-unica-0.3.1.md:748-768"      # program region (F2) = process-global seat
  - "docs/design/mudancas-superficie-0.3.1.md:1614-1637"          # Doc-2 three-category; wrapped kind
---

# 0123 · COL-F0c2 — wrap-refcount table root redesign (O(1) shared hash + CAS + chunk-chain)

> Replace the per-thread linear `addr→count` wrap table with a process-global, O(1) open-addressing hash
> keyed by region-ptr: lock-free CAS hot path, chunk-chain growth, fail-loud-only-on-OOM (D63, Findings 1+2+5+3).

## Goal

Root-redesign the `wrapped`-kind reference-count table so it is (1) **cross-thread-SHARED** — out of the
`_Thread_local` control block into a process-global root; (2) **O(1)** — an open-addressing hash keyed by
pointer, not an O(n) linear scan; (3) **unbounded / fail-loud** — grows by linking fixed bucket-chunks
(`chunk_node_link`, F0d), so the silent `WRAP_TABLE_CAP` saturation is gone and the only failure is OOM
(already `ar_oom`, fail-loud); and (4) honors the **ptr contract** — the table keys on the object's REGION
pointer, so `region_drop` at count zero drops exactly the right region (Finding 3). Byte-preservation:
**feature-gated-inert** — no `src/` caller holds a `wrapped` object (the compiler's own collections are
VALUE + `class`), so a `[dry]` build is byte-identical. Full spec:
`docs/design/wrap-refcount-table-redesign-0.3.1.md`. **BLOCKED on a fork** (the process-global anchor
mechanism, §Rulings) — this crumb is written design-ahead against the declared `wrap_root()` shape; the
implementer binds that one function once the owner rules the fork.

## Where

- `src/runtime/arena.tks:87-103` — the `CTRL_WRAP_*` + `WRAP_*` const block — REPLACE with the §6 constants
  (`WRAP_DIR_SLOTS`/`WRAP_DIR_BITS`/`WRAP_BUCKETS_PER_CHUNK`/`WRAP_LOAD_*`/`WRAP_ENTRY_STATE`/`WRAP_ENTRY_COUNT`/
  `WRAP_ST_*`/`WRAP_GOLDEN`); DELETE `WRAP_TABLE_CAP`, `CTRL_WRAP_LOCK`, `CTRL_WRAP_TABLE`, `CTRL_WRAP_COUNT`.
- `src/runtime/arena.tks:763-826` — `ar_wrap_table`/`ar_wrap_entry`/`ar_wrap_find`/`ar_wrap_inc`/
  `ar_wrap_remove`/`ar_wrap_dec` — REMOVE wholesale; replace with the hash helpers below (`ar_wrap_root`/
  `ar_wrap_hash`/`ar_wrap_probe`/`ar_wrap_insert`/`ar_wrap_inc`/`ar_wrap_dec`/`ar_wrap_grow`).
- `src/runtime/arena.tks:828-845` — `wrap_retain`/`wrap_release` — rewrite over the new helpers; `wrap_release`
  keeps `region_drop(region)` OUTSIDE the lock (region == the stored key).
- `src/runtime/arena.tks:13-19` — `CHUNK_NEXT`/`CHUNK_CAP`/`CHUNK_USED` + `chunk_node_link` (F0d) — the
  bucket-chunk chain is linked through these; do not re-teach.
- `src/runtime/sync.tks:63-64` — `mtx_lock`/`mtx_unlock` + `teko::sys::atomic_cas_u32`/`atomic_add_u32`/
  `atomic_xchg_u32`/`atomic_load_u32` — the structural lock + the u32 CAS hot path. No new atomic.
- `src/codegen/codegen.tks:2589` — `emit_retain`/`emit_release` — pass `region(obj)` (the per-object region
  the emitter allocated), NOT the raw object pointer (Finding 3 contract, §5 of the spec). Codegen-side note
  only — no runtime C.
- `src/runtime/teko_rt.c:2320` — `tk_g_arena_control` (`_Thread_local`) — the obstacle; the wrap root is
  SEPARATE from this per-thread seam (see the fork).

NEW: no new module. New pure-Teko hash helpers in `arena.tks`; one NEW anchor function `wrap_root()` whose
binding is the forked decision.

## How

1. **Constants** — replace the `WRAP_*` block with §6 of the spec. The bucket is 16 B: `ADDR`@0 (64-bit
   region-ptr key), `STATE`@8 (32-bit atomic: EMPTY/OCCUPIED/TOMBSTONE), `COUNT`@12 (32-bit atomic count).
   STATE/COUNT are 4-aligned → valid `atomic_*_u32` targets; ADDR uses `load_u64`/`store_u64`.

2. **The process-global root** — `ar_wrap_root(): u64` returns the root header {directory ptr, lock word,
   live-count}, allocated ONCE from the program region (F2) and published under a thread-safe double-check
   (`atomic_cas_u32` on the anchor's low word). It is INDEPENDENT of `ar_control()` (the per-thread seam).
   The anchor word itself is the forked decision — bind `wrap_root()` per the owner's ruling.

```teko
/**
 * ar_wrap_hash — Fibonacci hash of a 16-aligned region pointer to a directory slot. Drops the always-zero
 * low 4 alignment bits, multiplies by the 64-bit golden ratio, takes the top WRAP_DIR_BITS bits. O(1);
 * spreads sequential allocations across the directory.
 *
 * @param key  the wrapped object's region pointer (16-aligned)
 * @return     the directory slot index in [0, WRAP_DIR_SLOTS)
 * @since 0.3.1
 */
fn ar_wrap_hash(key: u64): u64
```

```teko
/**
 * ar_wrap_probe — locate the bucket holding `key` in its directory slot's bucket-chunk chain by linear
 * probing. Reads STATE atomically (acquire); stops at EMPTY, skips TOMBSTONE, matches OCCUPIED with equal
 * ADDR. Returns the bucket address, or 0 when `key` is absent. Lock-free (the hot-path reader).
 *
 * @param root  the process-global wrap root
 * @param key   the region pointer to find
 * @return      the matching bucket address, or 0 if absent
 * @since 0.3.1
 */
fn ar_wrap_probe(root: u64, key: u64): u64
```

3. **Hot path (lock-free).** `ar_wrap_inc` — probe; if found, `atomic_add_u32(bucket + WRAP_ENTRY_COUNT, 1)`
   and return; else fall to the structural insert. `ar_wrap_dec` — probe; CAS-decrement the COUNT word in a
   loop; if it reaches 0, take the structural remove path and return the captured region for the caller to
   drop.

4. **Structural path (one futex lock).** `ar_wrap_insert` — lock; re-probe (a racer may have inserted); if
   present, inc + unlock; else write ADDR (`store_u64`), publish STATE=OCCUPIED via `atomic_xchg_u32`
   (release, LAST), set COUNT=1, bump live-count, unlock. If the chain is full under the load factor, call
   `ar_wrap_grow` first. `ar_wrap_grow` — `chunk_node_link(wrap_region, tail, WRAP_BUCKETS_PER_CHUNK)`
   appends ONE fixed bucket-chunk (no rehash, no whole-backing swap). Remove — lock; re-read COUNT; if a
   racing retain resurrected it (>0) unlock and DON'T free; else STATE=TOMBSTONE (release), dec live-count,
   capture `region=ADDR`, unlock.

5. **`wrap_retain`/`wrap_release`.** `wrap_retain(region)` → `ar_wrap_inc`. `wrap_release(region)` →
   `ar_wrap_dec`; if it returned the at-zero region, `region_drop(region)` OUTSIDE the lock. The param is the
   object's REGION pointer (the emitter passes `region(obj)`), so `region_drop` is correct by construction.

6. **Publication ordering.** Writer: `store_u64(ADDR)` then `atomic_xchg_u32(STATE,OCCUPIED)`. Reader:
   `atomic_load_u32(STATE)` then `load_u64(ADDR)`. The acquire/release pair makes the 64-bit key read
   tear-free without a 64-bit atomic (§4.3 of the spec). This is why u32-only atomics suffice.

7. **Stay inert.** No `src/` caller holds a `wrapped` object → the whole path is dead on the corpus → `[dry]`
   byte-identical. The native leg of any new emit path is a Doc-2 honest-stop (NAT-*, M4), unchanged here.

## Rulings & laws

- **Teko-only:** all new work in `arena.tks`/`codegen.tks` `.tks`; the CAS atomics are the maintained-C
  `teko_rt` seed exception (`sync.tks`), NO new atomic in `teko_rt.c`.
- **D63 (owner 2026-08-20, `DECISION_LOG.md:650-661`):** the root redesign resolves Findings 1+2+5 TOGETHER
  (a patch to any one is a workaround). O(1) hash keyed by addr; process-global shared; CAS lock-free hot
  path, lock only on structural change; chunk-chain growth via `chunk_node_link` OR fail-loud; Finding 3
  ptr-contract resolved; pure-Teko, no new `teko_rt.c`.
- **NO-PUSHES / fail-loud (CLAUDE.md):** no dynamic array; growth LINKS a fixed chunk (F0d TS-substrate rule
  — never a whole-backing swap); saturation is unbounded-via-link so the silent `return` is gone, and the
  terminal failure (`ar_oom`) is already `arquivo:linha:coluna` fail-loud.
- **Fork protocol (owner 2026-08-19):** the process-global **anchor word** is a genuine open fork (spec §7).
  Checked `DECISION_LOG.md` (D61/D63), `arena-especificacao-unica-0.3.1.md`, `.crumbs/**` — no ruling fixes
  the anchor mechanism; Teko has no `global` surface and the only seam (`tk_arena_control`) is
  `_Thread_local`. NOT invented — escalated. Recommendation: the one companion process-global C seam word
  (same class as the existing `tk_arena_control` maintained-C exception). Everything else is anchor-agnostic
  against the declared `wrap_root()` shape; the implementer binds one function when the owner rules.
- **Doc-2 three-category (SEALED):** refcount is the **wrapped** kind ONLY; `class` stays region-drop
  (COL-F0b); value is bump + bucket (COL-F0a). This crumb touches only the wrapped table.
- **W15 full Javadoc** on every new fn/const (pub + private); flatten/extract; no inline `//`.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 4194304` (4 GiB); a blown guard is a
  root-cause fix, never a raised ceiling; commit each green step; reseed ONLY at SM-R1 (`0030`); fixpoint
  `gen2==gen3` byte-identical (the mechanism is inert — no corpus `wrapped`).

## Fixtures

The self-build fixpoint holds NO `wrapped` object and is single-threaded, so it exercises none of this — each
path needs an isolated oracle. The cross-thread ones require a genuinely multi-threaded oracle the fixpoint
never drives (spawn two threads, retain on one and release on the other).

| fixture | asserts | expected |
|---|---|---|
| `wrap_hash_o1_manyrefs` | `retain`/`release` over > 4096 distinct wrapped objects (past the OLD `WRAP_TABLE_CAP`): all tracked, none dropped silently, each freed exactly at zero — proves unbounded growth + no silent saturation | `0` |
| `wrap_crossthread_shared` | a wrapped object retained on thread A and released on thread B reaches the SAME table: freed exactly once at the last release, no leak, no double-free — proves process-global sharing | `0` |
| `wrap_crossthread_race` | N threads concurrently retain/release the same object: final count and lifetime are correct under contention — proves the CAS hot path + structural lock | `0` |
| `wrap_region_drop_contract` | at zero, the object's OWN region is dropped (not a neighbour, not the root) — proves the region-ptr key contract (Finding 3) | `0` |
| `wrap_tombstone_reuse` | insert, release-to-zero (tombstone), re-insert a colliding key: probe finds it, no lost slot — proves tombstone probe-through | `0` |

## Gate

`[dry]` — compile + the five isolated oracles + fixpoint (byte-identical; the table is inert — no corpus
`wrapped` object). "Green" = the wrap table is a process-global O(1) open-addressing hash keyed by region-ptr,
inc/dec is lock-free CAS with the structural lock only on insert/remove/grow, growth links fixed bucket-chunks
(no silent saturation), `region_drop` at zero drops the keyed region, and a `[dry]` build is byte-identical.
Reseed-class: `(folds R1)`.

## Deps

`COL-F0c` (the `retain`/`release` surface + `wrap_retain`/`wrap_release` this redesign rewrites) and
`COL-F0d` (`chunk_node_link`, the growth primitive). SEQUENCING (D63): land AFTER F0d and the queued
**F0c-fix** (Finding 2+4 cleanup) drain — all three touch `arena.tks`/`codegen.tks` and would collide.
ALSO BLOCKED on the owner's ruling of the process-global anchor fork (§Rulings) before the `wrap_root()`
binding can be written; the rest is implementable design-ahead now.

## Done when

The wrap-refcount table is a process-global, O(1) open-addressing hash keyed by the object's region pointer,
with a lock-free CAS inc/dec hot path, a structural lock only on insert/remove/chunk-link, unbounded
chunk-chain growth (no silent saturation; OOM is fail-loud), and `region_drop`-at-zero honoring the region-ptr
contract; the five oracles pass and a `[dry]` build is byte-identical (inert on the corpus).
