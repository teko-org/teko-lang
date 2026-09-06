---
seq: 0170
crumb-id: PRE-C2
milestone: M5
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [BT-C1]
sources:
  - "docs/design/embed-vfs-sweep-integration-0.3.1.md:5"        # §5.1 read prelude from VFS
  - "DECISION_LOG.md:1167"                                       # D134 R2 retracted: no disk prelude
  - "src/build/project.tks:277-396"                              # rt_dir_tks_paths / inject_runtime_prelude
---

# 0170 · PRE-C2 — `inject_runtime_prelude` reads the prelude from the VFS (memory), not disk

> The flip that makes the binary self-contained: `inject_runtime_prelude` stops calling `fs::list_dir`/
> `read_file` over `src/` and instead pulls each prelude unit from the embedded VFS (`FILES.get` over the
> `teko::/src/…` keys, PRE-C1). The dev no longer needs the Teko source tree to run the compiler (M.0,
> D134-R2). Behavior-preserving (same prelude content, same namespaces) → fixpoint-gated; byte-mover in
> the compiler → RITUAL + reseed.

## Goal

Re-wire `inject_runtime_prelude` (annex §5.1): replace the disk walk (`rt_dir_tks_paths`→`fs::list_dir`,
`rt_prelude_units`, per-path `read_file`) with VFS reads (`FILES.get(key)` + `stream_read` decode) over
the embedded prelude keys. Preserve `rt_prelude_guard`/`rt_units_cover_all` — the coverage guard now
iterates VFS keys instead of `dsc_walk` of disk. The base unit (BT-C1) is injected universally; runtime
units conditionally. Content-identical → the injected sources are byte-for-byte the same → `gen2==gen3`.

## Where

- `src/build/project.tks:368-396` `inject_runtime_prelude` — read from VFS keys, not disk paths.
- `src/build/project.tks:277-307` `rt_dir_tks_paths`/`rt_prelude_units` — RETIRE the disk walk (or repoint
  to VFS key enumeration via `FILES.list` filtered by the `teko::/src/…` prefix).
- `src/build/project.tks:331-346` `rt_prelude_guard` — iterate VFS keys for the coverage check.

## How

1. Enumerate prelude keys via `FILES.list()` filtered to the `teko::/src/{runtime,sys,sys/abi,assert}/`
   prefixes; group by namespace (as `rt_prelude_units` did).
2. For each key, `FILES.get(key)` → `RoFile`; since PRE-C1 embedded with Deflate, `inflate` the
   `content` (the compiler here IS the dev — it decompresses per `compress`), decode to `str`, feed as a
   `SourceFile`.
3. Keep the guard: every shipped prelude namespace is covered by an injected VFS unit.
4. DELETE the disk-walk path (`fs::list_dir` over `src/` for the prelude) — the root-cause removal, not a
   fallback (D134: no disk prelude).

## Rulings & laws

- **Teko-only.**
- **D134-R2 (retracted disk prelude):** the prelude comes from the embedded VFS, NEVER disk; removing the
  disk walk is the root-cause fix (no workaround coexistence past this crumb).
- **NO detecting-the-nonexistent:** the removed disk path leaves no tombstone.
- **W15 full Javadoc; no `//`.**
- **Safety:** never `teko test .`; subshell `ulimit -v 4718592`; **RITUAL** — full ladder, `gen2==gen3`
  (content-identical prelude), MEM_PARANOID. Ratchet: this REPLACES disk reads (materialized) with
  streamed VFS reads (≤1024 B) → peak should FLATTEN or DROP (not grow).

## Fixtures

`none — the prelude injection is the self-build's own path (the compiler injects its prelude every build);
the fixpoint proves content-identity`.

## Gate

`[RITUAL]` — full ladder; the prelude injects from the VFS, the disk walk is gone, `gen2==gen3`, peak
flat/down. "Green" = a build with NO `src/` prelude tree on disk still self-hosts (self-contained proof),
`gen2==gen3`. Reseed-class: `fixpoint-rebuild`.

## Deps

`BT-C1`

## Done when

`inject_runtime_prelude` reads every prelude unit from the embedded VFS, the disk walk is removed, the
compiler self-hosts without the source tree, and the RITUAL gate is green with peak not grown.
