---
seq: 0168
crumb-id: PRE-C1
milestone: M5
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [EMB-C5]
sources:
  - "docs/design/embed-vfs-sweep-integration-0.3.1.md:5"        # §5.1 embedded prelude
  - "DECISION_LOG.md:1151,1167"                                  # D135/D134 prelude-from-VFS, small-prelude-only
  - "src/build/project.tks:296-307"                              # rt_prelude_units (the ~13 files)
---

# 0168 · PRE-C1 — embed the small runtime prelude (~13 files) into the VFS at compiler build

> Bake the SMALL prelude (`teko::runtime`/`teko::sys`/`teko::sys::abi`/`teko::assert` — the files
> `rt_prelude_units` lists) into the compiler's own VFS via `#embed`, so the binary carries its prelude in
> rodata (M.0 self-contained). NEVER the stdlib (enormous). This crumb only EMBEDS them (the read-seam
> flip to VFS is PRE-C2); the compiler now USES `#embed` in its own corpus → requires EMB-C5 in the seed.
> Byte-mover (rodata now carries the prelude) → fixpoint.

## Goal

Add `#embed("/src/runtime/*.tks")` etc. (or the enumerated exact paths until glob EMB-C7) to the
compiler's corpus, so `FILES` (`teko::embed`) holds the ~13 prelude sources under keys
`teko::/src/runtime/…`. Compress them (Deflate) to keep the binary lean (ratchet: the compressed prelude
must not grow the peak vs the disk read it will replace in PRE-C2). Only runtime/sys/abi/assert — the
stdlib stays on disk/package (it is not injected). This is purely additive to rodata; `inject_runtime_
prelude` still reads disk until PRE-C2 (both coexist one crumb).

## Where

- The compiler's top-level corpus (a `.tks` in `src/build/` or a dedicated `src/embed/prelude_embed.tks`)
  — the `#embed` directives for the prelude tree, `/`-anchored at project root (annex §2).
- `src/build/project.tks:296-307` `rt_prelude_units` — the authoritative list of the ~13 files/namespaces
  to embed (source of the exact paths).

## How

1. Enumerate the prelude files (from `rt_prelude_units`) and add `#embed("/src/runtime/<f>.tks", Deflate,
   6)` per file (exact paths; glob is EMB-C7). Keys become `teko::/src/runtime/<f>.tks`.
2. Do NOT touch `inject_runtime_prelude` yet — it keeps reading disk (PRE-C2 flips it). This crumb only
   proves the prelude is embedded and reachable via `FILES.get`.
3. Verify the compressed prelude size does not grow the build peak (ratchet non-crescer; compression
   should keep rodata small vs materialized disk reads).

## Rulings & laws

- **Teko-only + maintained-C exception limited to EMB-C3's seam.**
- **D134/D135:** embed ONLY the small prelude (runtime/sys/abi/assert), NEVER the stdlib; binary self-
  contained.
- **Seed law:** the corpus uses `#embed` → EMB-C1..C5 must be in the seed first (they are, deps EMB-C5).
- **W15 full Javadoc; no `//`.**
- **Safety:** never `teko test .`; subshell `ulimit -v 4718592`; build gen2, `gen2==gen3`. Ratchet:
  ADDITIVE → peak must NOT grow (compressed prelude vs disk).

## Fixtures

`none — the embedded prelude is exercised by the self-build once PRE-C2 reads it; keys are proven by the
compiler resolving its own prelude`.

## Gate

`[fixpoint]` — build gen2, `gen2==gen3` (the rodata now carries the compressed prelude, deterministic).
"Green" = `FILES.get("teko::/src/runtime/…")` returns each prelude file, peak not grown. Reseed-class:
`fixpoint-rebuild`.

## Deps

`EMB-C5`

## Done when

The ~13 prelude files are embedded (compressed) in the compiler's VFS, reachable via `FILES.get`, with
the peak not grown and `gen2==gen3`.
