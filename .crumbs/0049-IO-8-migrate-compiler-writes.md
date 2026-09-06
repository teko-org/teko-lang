---
seq: 0049
crumb-id: IO-8
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [IO-7]
sources:
  - "docs/design/io-streaming-0.3.1.md:306-338"   # §6 write sites
  - "docs/design/io-streaming-0.3.1.md:375-377"   # §7 crumb 8
  - "docs/design/io-streaming-0.3.1.md:404-406"   # §8 fixpoint proof
  - "docs/design/io-streaming-0.3.1.md:410-433"   # §9 arena co-dependence (next step)
---

# 0049 · IO-8 — migrate compiler writes to `write_stream`/`append_stream` (`project`/`fmt`/`regr_group`/`init`)

> Move the compiler's own write sites from the TOTAL `write_file`/`write_file_bytes` to
> `write_stream`/`append_stream`: `src/build/project.tks` (the 22 MB `teko.c`, `.tkh`, `.o`/`.tkl`/`.a`,
> `.tsym`, coverage), `src/fmt/fmt.tks` (in-place reformat), `src/build/regr_group.tks`,
> `src/build/init.tks` — byte-identical output.

## Goal

Complete the compiler's move to strictly-stream I/O by migrating the WRITE call-sites. The critical write
is `project.tks`'s `teko.c` (the 22 MB self-image, `:1490`/`:2832`/`:3232`/…), plus `.tkh` (`:2650`), the
`.o`/`.tkl`/`.a`/wasm artifacts, `.tsym`/`.plist`/coverage; `fmt.tks`'s in-place source reformat
(`:788`); `regr_group.tks`'s synthetic regression projects; and `init.tks`'s new-project scaffold.
`write_stream`/`append_stream` write the SAME bytes in the SAME order, so the output files are
byte-identical. Byte-mover for the emitted `teko.c`? The write site's emitted call changes shape (a real
emit delta) → `fixpoint-rebuild` reseed — but the WRITTEN `teko.c` must be byte-identical: this is the
sharpest fixpoint in the io wave (`§8`: the migration writes exactly the same bytes, so `gen2.c ==
gen3.c` must survive). The deep memory win (codegen emitting DIRECT into a `FileStream` rather than
building the 22 MB `csrc` first) is the arena/expurgo carga, REPORTED as the next step (`§9`), NOT this
crumb — this crumb delivers the sink migration.

## Where

- `src/build/project.tks` — `write_file`/`write_file_bytes` sites (30 + 4 across the file): `teko.c`,
  `.tkh`, `.o`/`.tkl`/`.a`/wasm, `.tsym`, `.plist`, coverage → `write_stream`/`write_file_bytes`-over
  -stream / `append_stream` where the output is built in order.
- `src/fmt/fmt.tks:788` — the in-place reformatted-source write.
- `src/build/regr_group.tks` — the synthetic regression-project writes (6).
- `src/build/init.tks` — the scaffold writes (3).

## How

1. **Migrate each write site** to `write_stream(path, <bytes>)` (truncating) or `append_stream` (where
   the output is genuinely appended). For text outputs, pass the `str` reinterpreted as `[]byte`
   (zero-copy). For binary artifacts (`.o`/`.tkl`/`.a`), pass the `[]byte` directly.

```teko
/**
 * project_write_teko_c — write the emitted C self-image to `path` via `write_stream` (migrated off the
 * FFI-materializing `write_file`). Writes the same bytes in the same order, sliced into CHUNK pieces —
 * so the produced `teko.c` is byte-identical to the pre-migration output (the fixpoint `gen2.c ==
 * gen3.c` must survive). The deeper win (emit direct into the stream, no 22 MB `csrc` first) is the
 * arena/expurgo next step, not this fn.
 * @param path  the destination path (e.g. `bootstrap/teko.c`)
 * @param csrc  the emitted C source bytes
 * @return      null on success, or the first write error
 * @throws      when the underlying stream write fails
 * @since 0.3.1
 */
fn project_write_teko_c(path: str, csrc: ref []byte): error | null
```

2. **Byte-order invariance.** `write_stream` slices `data` into CHUNK pieces and appends each in order —
   the concatenation is bit-identical to a single `write_file`. Confirm no site reorders or pads.
3. **Fixpoint is the write proof.** After this crumb, `teko.c` of gen2 and gen3 must be BYTE-IDENTICAL
   — the migration writes exactly the same bytes in the same order, so the identity has to survive
   (`io-streaming §8`). Gate HARD on `gen2.c == gen3.c`; a single-byte divergence means a write site
   changed the byte stream (padding, truncated tail, reorder) — HALT and fix.
4. **Report the direct-emit next step.** Note in the crumb close-out that codegen-emits-direct-into
   -writer (killing the 22 MB `csrc` materialization) is the arena/expurgo follow-up (`§9`), REPORTED
   up, not done here.

## Rulings & laws

- **Teko-only:** call-site edits in `src/build/*`, `src/fmt/*` (`.tks`); the stream layer is Teko over
  syscalls; C twins frozen.
- **W15 full Javadoc** on any new write helper; flatten; no inline `//`.
- **Owner decree:** the compiler writes STRICTLY via stream (output → stream, append-only preferred).
- **Byte-identity (`§8`):** the written `teko.c` must be byte-identical; the fixpoint `gen2.c == gen3.c`
  is the guard — the smallest divergence stops and reconciles, never maquiado.
- **Reported, not actioned (`§9`):** codegen-emits-direct-into-writer is the arena/expurgo next step,
  reported UP, not turned into work here.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit each migrated
  file as its own green step; reseed ONLY at the fixpoint; `gen2==gen3` byte-identical; sweep
  `.tkt`/`.tkr` after the call-shape change.

## Fixtures

none — the fixpoint self-build exercises this. The compiler WRITES its own `teko.c`, `.tkh`, and
artifacts through these sites on every self-build; the `gen2.c == gen3.c` byte-identity (same bytes,
same order) IS the regression — it is the sharpest write proof in the wave. IO-5's isolated write
round-trip fixtures already cover the stream mechanics.

## Gate

`[fixpoint]` — build gen2 (`--no-verify --release`, `TEKO_BACKEND=c`, `TEKO_CC=clang`,
`ulimit -v 6815744`) + scoped regression + `gen2.c == gen3.c` byte-identity, per migrated file. "Green" =
the compiler's write sites go through `write_stream`/`append_stream`, the produced `teko.c`/`.tkh`/
artifacts are byte-identical to pre-migration, and the 3-gen ladder converges. Reseed-class:
`fixpoint-rebuild`.

## Deps

`IO-7`.

## Done when

`project.tks` (`teko.c`, `.tkh`, artifacts), `fmt.tks`, `regr_group.tks`, and `init.tks` write via
`write_stream`/`append_stream`, the written `teko.c` is byte-identical (`gen2.c == gen3.c`), and the
direct-emit-into-writer follow-up is reported up.
