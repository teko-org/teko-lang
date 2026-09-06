---
seq: 0048
crumb-id: IO-7
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [IO-6]
sources:
  - "docs/design/io-streaming-0.3.1.md:306-338"   # §6 read sites
  - "docs/design/io-streaming-0.3.1.md:373-375"   # §7 crumb 7
  - "docs/design/io-streaming-0.3.1.md:167-196"   # §2.5 TOTAL-over-stream form
---

# 0048 · IO-7 — migrate compiler reads to `read_stream` (`assemble`/`fmt`/`project`/`regression`)

> Move the compiler's own read sites from the TOTAL `read_file` to `read_stream` (or the TOTAL-over
> -stream form) where read-only: `src/build/assemble.tks` (each source file), `src/fmt/fmt.tks`,
> `src/build/project.tks`, `src/build/regression.tks` — preservation-only.

## Goal

With the TOTAL forms now stream-backed (IO-6), migrate the compiler's read call-sites to the STREAM read
path so the decree "strictly stream" holds for the compiler's own I/O. The load-bearing read is
`assemble.tks:123`/`:179` — the source text of every `SourceFile` (the compiler's read path); plus
`fmt.tks` (the source to format), `project.tks` (`.tkl` binary, manifest, deps), and `regression.tks`
(`.out`/`.err`/`.rc` fixtures + `.tkr` sources). Since IO-6 already made the TOTAL form stream-backed,
most sites can stay on `read_file` (now stream under the hood) and satisfy the decree; the ones worth
switching to `read_stream` directly are the large read-only paths. Byte-mover for the emitted `teko.c`?
Preservation — the read path reads the SAME bytes; the emitted output is byte-identical, but the read
site's emitted call changes shape → a real emit delta → `fixpoint-rebuild` reseed. Gate on `gen2==gen3`.

## Where

- `src/build/assemble.tks:123` and `:179` — `read_file(sf.path)` for each `SourceFile` — the compiler's
  read path; migrate to `read_stream` (bytes → `str` reinterpret) where read-only.
- `src/build/project.tks:126`,`:340`,`:354` — `.tkl` binary, deps, `.tkp` manifest reads.
- `src/build/regression.tks` — the `.out`/`.err`/`.rc` fixture + `.tkr` source reads.
- `src/fmt/fmt.tks` — the source-to-format read.

## How

1. **Switch the large read-only sites to `read_stream`.** For `assemble.tks`'s source read, call
   `read_stream(sf.path)` and reinterpret `[]byte`↔`str` (zero-copy) instead of `read_file`. The match
   arms (`str as s => s; error as e => return e`) are preserved; only the callee changes.
2. **Leave the small/TOTAL-appropriate sites on `read_file`.** The `.tkp` manifest, `.rc` fixtures, and
   other small reads may stay on the (now stream-backed, IO-6) TOTAL form — the decree is satisfied
   because the TOTAL form IS stream underneath (`io-streaming §6`). Do NOT churn sites where the switch
   buys nothing.
3. **Binary reads stay bytes.** `.tkl` (binary) reads use the byte path (`read_stream` → `[]byte`),
   never the `str` reinterpret.

```teko
/**
 * assemble_read_source — read one `SourceFile`'s text via `read_stream` (the compiler's read path,
 * migrated off the FFI-materializing `read_file`). Reinterprets the `[]byte` as `str` (same `{ptr,len}`,
 * no copy). Read-only; the file text is materialized once for the lexer, exact-sized, no growth.
 * @param path  the source file path
 * @return      the source text, or the read error propagated to the caller
 * @throws      when the underlying stream read fails
 * @since 0.3.1
 */
fn assemble_read_source(path: str): str | error
```

4. **Preservation proof.** The reads return the SAME bytes, so every downstream stage sees identical
   input → the emitted `teko.c` is byte-identical as OUTPUT. The reseed exists only because the read
   site's emitted call shape changed. Converge `gen2 == gen3`; a divergence means a read path changed
   what it returned (e.g. a truncated last chunk) — HALT and fix the drain, do not paper over.

## Rulings & laws

- **Teko-only:** call-site edits in `src/build/*`, `src/fmt/*` (`.tks`); the stream layer is Teko over
  syscalls; C twins frozen.
- **W15 full Javadoc** on any new read helper; flatten; no inline `//`.
- **Owner decree:** the compiler reads STRICTLY via stream; the TOTAL-over-stream form satisfies it for
  small sites, `read_stream` for the large read-only paths.
- **Preservation:** the migration reads the same bytes → the emitted `teko.c` is byte-identical; the
  fixpoint `gen2==gen3` is the guard — the smallest divergence stops and reconciles.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit each migrated
  file as its own green step; reseed ONLY at the fixpoint; `gen2==gen3` byte-identical; sweep
  `.tkt`/`.tkr` after the call-shape change.

## Fixtures

none — the fixpoint self-build exercises this. The compiler reads ITS OWN sources (`assemble`), manifest
(`project`), and fixtures (`regression`) through these sites on every self-build; `gen2==gen3`
byte-identity (same input bytes → same emitted `teko.c`) IS the regression. IO-5's isolated round-trip
fixtures already cover the stream mechanics.

## Gate

`[fixpoint]` — build gen2 (`--no-verify --release`, `TEKO_BACKEND=c`, `TEKO_CC=clang`,
`ulimit -v 6815744`) + scoped regression + `gen2==gen3` byte-identity, per migrated file. "Green" = the
compiler's read sites go through the stream path, every read returns identical bytes, the emitted `teko.c`
is byte-identical, and the 3-gen ladder converges. Reseed-class: `fixpoint-rebuild`.

## Deps

`IO-6`.

## Done when

`assemble.tks` (source reads), `fmt.tks`, `project.tks`, and `regression.tks` read via the stream path,
the reads return identical bytes, `gen2==gen3` holds, and the compiler self-builds over stream reads.
