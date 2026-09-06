---
seq: 0166
crumb-id: EMB-C4
milestone: M5
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [EMB-C3]
sources:
  - "docs/design/embed-vfs-sweep-integration-0.3.1.md:3"        # §3 Tier-A resolution
  - "docs/design/embed-vfs.md:391-399,511-531"                  # #594 Tier-A flatten
  - "src/lir/lower.tks:763,774"                                 # find_const_rodata / const rodata path
---

# 0166 · EMB-C4 — const materialization: four Tier-A rodata consts + `FILES` thin view (BLOCKER, touches binary)

> The embed pass emits its four flattened heaps as Tier-A module-level consts
> (`EMBED_NAMES`/`EMBED_BLOBS`/`EMBED_TABLE` = `[]byte`, `EMBED_COUNT` = `u64`) into rodata via the #594
> Tier-A aggregate→rodata path, and `files()` builds the thin `FileSystem` view over them (zero-copy,
> runtime — NOT a pointer-bearing const aggregate → NO Tier-B). This is the FIRST crumb that changes the
> emitted bytes when a corpus has `#embed`. Byte-mover → RITUAL + reseed.

## Goal

Materialize the VFS DATA (annex §3): the pass emits `EMBED_NAMES`/`EMBED_BLOBS`/`EMBED_TABLE`/`EMBED_COUNT`
as Tier-A rodata (a `[]byte` is self-contained rodata — plan §6.4, zero backend change). `files()`
(EMB-C0) already reads those four consts; wire them to the pass output so a real `#embed` populates them.
CRUCIAL (the Tier-A resolution): `FILES`/`files()` is a runtime thin view over the four consts — never a
serialized `FileSystem` aggregate holding slices (that would be Tier-B, blocked). Empty corpus → the
consts are empty → byte-identical to pre-#embed (pay-per-use).

## Where

- The embed pass — emit the four consts (packed table records: `name_off,name_len,blob_off,blob_len,
  orig_len,comp_tag`, fixed-width POD).
- `src/lir/lower.tks:763-778` (`find_const_rodata`/const rodata path) — the `[]byte` consts ride the
  existing string-literal→`LRodata` machine; confirm a top-level `const []byte` lowers to Tier-A rodata.
- `src/embed/embed.tks` — `files()` reads the four (now populated) consts; NO const-aggregate of the
  struct is serialized.
- `src/compress/deflate.tks:38` `deflate` + `src/compress/inflate.tks:405` `inflate` — promote `pub`→
  `exp` (the dev must call `inflate` to decompress a `Deflate` embed per D135; `deflate` for symmetry with
  the already-`exp` `gzip_*`/`zlib_*`). This changes the `.tkh`/ABI → it FOLDS INTO THIS crumb's reseed
  (coordinator 2026-08-27), NOT a separate reseed.

## How

1. Serialize the ordered key set → the three heaps + count; emit as four module-level consts in
   `teko::embed`.
2. Verify the `const []byte` → rodata path is Tier-A (no `serialize_const` honest-stop): a `[]byte` has no
   internal pointer to another aggregate; it is a `{ptr→rodata, len}` — Tier-A.
3. `files()` composes the thin view at runtime (zero-copy slices into the four consts). If the compiler
   cannot lower a `const FileSystem` name to a runtime thin-constructor, KEEP `files()` accessor (annex
   §3.2 veto-open — cosmetic, NOT a fork). Do NOT emit a pointer-bearing `FileSystem` const.
4. Empty corpus (compiler's own build has no `#embed` yet) → consts empty → emitted bytes unchanged.
5. Promote `deflate`/`inflate` `pub`→`exp` (D135 dev-decompresses); the `.tkh`/ABI change rides THIS
   crumb's reseed (coordinator 2026-08-27), not a separate one.

## Rulings & laws

- **Teko-only.**
- **Tier-A resolution (annex §3, RESOLVED — no fork):** four `[]byte`/scalar consts = Tier-A; `FILES` a
  runtime thin view. NO #594 Tier-B dependency.
- **M.0 self-contained:** the datum lives in rodata; no read syscall in the emitted program.
- **W15 full Javadoc; no `//`.**
- **Safety:** never `teko test .`; subshell `ulimit -v 4718592`; **RITUAL** — re-golden backend/object
  goldens, `gen2==gen3` byte-identity, native validation (same read/exit), `TEKO_MEM_PARANOID=1`, Javadoc/
  `//`-audit. Ratchet: ADDITIVE → peak must NOT grow (empty corpus keeps it byte-flat).

## Fixtures

`none — the empty-corpus byte-identity is proven by the fixpoint; the populated round-trip is EMB-C5's`.

## Gate

`[RITUAL]` — full native ladder; empty corpus → emitted bytes byte-identical (pay-per-use), `gen2==gen3`,
peak flat. "Green" = the four Tier-A consts emit, `files()` reads them, NO Tier-B stop hit, empty case
byte-identical. Reseed-class: `fixpoint-rebuild`.

## Deps

`EMB-C3`

## Done when

The four Tier-A consts materialize in rodata, `files()` composes the thin view without a Tier-B
aggregate, the empty corpus is byte-identical, and the RITUAL gate is green.
