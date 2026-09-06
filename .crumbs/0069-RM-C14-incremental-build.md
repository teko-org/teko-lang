---
seq: 0069
crumb-id: RM-C14
milestone: M2
gate: "[dry]"
reseed-class: "none"
deps: [RM-C12, RM-C13]
sources:
  - "docs/design/reducao-memoria-arrays-0.3.1.md:305-309"          # C14 — incremental build (optional, OFF on self-build)
  - "docs/design/reducao-memoria-arrays-0.3.1.md:519-526"          # §6bis incremental = persistent case of the disk dump; OFF on fixpoint
---

# 0069 · RM-C14 — incremental build (per-unit typed cache; OFF on self-build/fixpoint)

> Cache each unit's typed `.tkb` on disk keyed by `hash(unit)+hash(linked-table)`, recompile only what changed —
> a dev-time optimization, DISABLED on the self-build/fixpoint path so a clean build stays byte-identical.

## Goal

C14 is the persistent case of C13's disk dump (`reducao…` 519-526): the same per-unit typed `.tkb` that bounded
the memory peak becomes a **cache between builds**. Keyed by `hash(unit) + hash(linked-table)`, it recompiles
ONLY the units that changed (source hash) or whose linked dependencies changed (table hash), reusing the cached
typed artifact for the rest. It does NOT reduce the memory peak (C12/C13 already did) — it reduces DEV BUILD
TIME. It is **OFF on the self-build / fixpoint path**: a clean build must produce `teko.c` byte-identical, so
the fixpoint always builds from scratch with the cache disabled (`reducao…` 305-308, 524-526). This makes it a
`[dry]` leaf with reseed-class `none` — it changes no emitted byte on the fixpoint path (the cache is inert
there) and teaches nothing. It rests on C12 (fused per-unit) and C13 (the disk dump it caches).

Not blocked by any open dependency (its deps RM-C12 and RM-C13 are in this wave); this is executable design.

## Where

- `src/build/project.tks:2763` — `compile_project_g(...)` — add the incremental cache lookup/store around the
  per-unit stream, GATED OFF whenever the build is a self-build/fixpoint (a `no_incremental` flag defaulting ON
  for the compiler's own build).
- `src/emit/tkb_frame.tks:369` / `src/emit/tkb_read.tks:930` (via C13's `serialize_unit`/`deserialize_unit`) —
  the cache stores/loads the per-unit typed `.tkb`.
- NEW module skeleton: `src/build/incremental.tks` — the cache key, lookup, store, and the disable gate. New
  decls below.
- NO change to the emitted `teko.c` on the fixpoint path (the cache is inert there).

## How

1. **Define the cache key** (`reducao…` 519-521): `hash(unit source) + hash(linked-table slice the unit
   depends on)`. A unit is reused iff BOTH hashes match a cached entry; else it is re-checked+re-emitted. The
   linked-table hash captures cross-unit signature changes (a `pub` signature change invalidates dependents).
2. **The disable gate — OFF on self-build/fixpoint** (`reducao…` 524-526). The W15 surface:

```teko
/**
 * incremental_enabled — whether the per-unit typed cache is active for this build. It is FORCED OFF on the
 * self-build / fixpoint path: a clean build must reproduce `teko.c` byte-identical, so the fixpoint always
 * rebuilds from scratch with no cache (`reducao…` 524-526). It is a DEV-TIME optimization only — it reduces
 * build time, never the memory peak (C12/C13 own that).
 *
 * @param self_build  true when this is the compiler building itself / a fixpoint gen — forces the cache OFF
 * @return            true iff the cache may be consulted (never on a self-build/fixpoint)
 * @since 0.3.1
 */
fn incremental_enabled(self_build: bool): bool { !self_build }

/**
 * cache_key — the per-unit cache key: the source hash of the unit joined with the hash of the linked-table
 * slice it depends on. A cache HIT (both match) reuses the unit's typed `.tkb`; a miss re-checks+re-emits.
 *
 * @param unit_src_hash    the hash of the unit's source
 * @param linked_dep_hash  the hash of the linked-table slice the unit depends on
 * @return                 the composite cache key
 * @since 0.3.1
 */
fn cache_key(unit_src_hash: u64, linked_dep_hash: u64): u64
```

3. **Lookup/store around the per-unit stream** (`compile_project_g`): on a non-self-build, before checking a
   unit, look up its key; on a hit, load the cached typed `.tkb` (C13's `deserialize_unit`) and skip
   re-checking; on a miss, check+emit and store the fresh `.tkb`. On a self-build/fixpoint, `incremental_enabled`
   is false — the whole path is bypassed.
4. **Prove inertness on the fixpoint path.** With the cache OFF, the self-build produces `teko.c` byte-identical
   to a C12/C13 build — the cache adds NO emitted-byte change. This is the `[dry]` posture: additive, inert on
   the gated path.

Reused (do NOT redeclare): `serialize_unit`/`deserialize_unit` (C13), the per-unit stream (C12), the internal
FFI (C11), `teko::str::hash`/an FNV hash for the keys.

## Rulings & laws

- **Teko-only:** the cache lands in `src/build/{project,incremental}.tks` (`.tks`); no C twin.
- **W15 full Javadoc** on `incremental_enabled`/`cache_key` + the cache fns; flatten/extract; no inline `//`.
- **Removals = clean expurgo, NO tombstone:** additive — removes no surface.
- **Fixpoint byte-identity is sacrosanct (`reducao…` 305-308, 524-526):** the cache is FORCED OFF on the
  self-build/fixpoint path; a clean build must be byte-identical. The cache must never influence the fixpoint
  output — this is the load-bearing constraint that keeps it a `[dry]` leaf.
- **Dev-time only:** it reduces build time, not the memory peak; it must not be relied on for correctness.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744` cap — a blown guard is a
  root-cause fix, never a raised ceiling; commit each green step; **reseed ONLY at a [RITUAL]** — this is a
  `[dry]` leaf, reseed-class `none`, NO reseed; the trivial fixpoint (cache OFF → no emitted-byte change) is the
  proof; sweep `.tkt`/`.tkr` after the signature changes.

## Fixtures

The self-build fixpoint exercises the cache-OFF path (byte-identity), but the cache-ON dev path is NOT
self-build-exercised (it is disabled there) — isolated oracles for the cache behavior:

| fixture | asserts | expected |
|---|---|---|
| `incremental_hit_reuses` | a rebuild with no source change reuses every cached unit (no unit re-checked) and produces identical output | `0` |
| `incremental_dep_invalidates` | changing a `pub` signature invalidates the dependent units (their `linked_dep_hash` changes) and re-emits them | `0` |
| `incremental_off_self_build` | with `self_build=true` the cache is bypassed and the output is byte-identical to a clean C12/C13 build | `0` |

## Gate

`[dry]` — compile + the scoped fixtures + the trivial fixpoint (cache OFF → no emitted-byte change). "Green" =
the incremental cache reuses unchanged units and invalidates on source/dep change on the DEV path, is FORCED
OFF on the self-build/fixpoint path, and adds no emitted-byte change there. **Reseed-class:** `none` (pure
`.tks`, inert on the gated path, teaches nothing).

## Deps

`RM-C12` (`0067` — the fused per-unit stream the cache wraps), `RM-C13` (`0068` — the per-unit typed `.tkb`
disk dump the cache stores/loads).

## Done when

the per-unit typed cache reuses unchanged units and invalidates on source/dependency change on the dev path, is
forced OFF on the self-build/fixpoint path (a clean build stays byte-identical), the fixtures exit `0`, and a
`[dry]` build shows no emitted-byte change with the cache disabled.
