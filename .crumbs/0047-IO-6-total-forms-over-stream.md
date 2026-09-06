---
seq: 0047
crumb-id: IO-6
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [IO-5]
sources:
  - "docs/design/io-streaming-0.3.1.md:167-196"   # §2.5 TOTAL forms over stream
  - "docs/design/io-streaming-0.3.1.md:288-338"   # §5/§6 coexistence + sites
  - "docs/design/io-streaming-0.3.1.md:369-372"   # §7 crumb 6
  - "docs/design/io-streaming-0.3.1.md:410-433"   # §9 arena co-dependence
---

# 0047 · IO-6 — rewrite TOTAL forms (`read_file`/`write_file`/…) over stream (kill the `teko_rt` FFI edge)

> Rewrite the BODIES of `teko::io::read_file`/`write_file`/`write_file_bytes`/`append_file` in
> `src/io/io.tks` from `extern … from "teko_rt"` to calls on the IO-5 helpers — signature IDENTICAL, so
> no caller changes; the FFI edge dies.

## Goal

The TOTAL forms (`read_file`/`write_file`/`write_file_bytes`/`append_file`) stay on the surface (some
call-sites want the whole-file convenience), but their bodies stop being `extern … from "teko_rt"` (the
`tk_rt_read_file`/`tk_rt_write_file` FFI that `fseek`+`ftell`+`tk_alloc`+`fread`/`fwrite` the whole 22 MB
`teko.c` in one shot) and become thin wrappers over `read_stream`/`write_stream`/`append_stream` (IO-5).
The signature is byte-identical → NO caller changes; only the `extern` body is replaced. Byte-mover for
the emitted `teko.c`? YES — the emitted body changes (the `teko_rt` FFI call disappears, replaced by the
stream helper calls) → a real emit delta → `fixpoint-rebuild` reseed. This is the RITUAL that removes the
FFI edge for the TOTAL forms; the deep memory win (codegen emitting DIRECT into a writer) is a later
arena/expurgo carga, not this crumb (`§9`).

## Where

- `src/io/io.tks:3` — `read_file` — body: `read_stream(path)` then reinterpret `[]byte`↔`str` (same
  `{ptr,len}`, no copy) instead of the `teko_rt` FFI.
- `src/io/io.tks:22` — `write_file` — body: `write_stream(path, <content as []byte>)`.
- `src/io/io.tks:34` — `write_file_bytes` — body: `write_stream(path, data)`.
- `src/io/io.tks:29` — `append_file` — body: `append_stream(path, <content as []byte>)`.

The `read_line`/`stdin_eof`/`read_stdin`/`print`/`println`/`write`/`flush` stay FFI for now (stdin/stdout
are not seekable files — `§5`, out of scope).

## How

1. **`read_file` over `read_stream`** (`io-streaming §2.5`):

```teko
/**
 * read_file — read `path` as UTF-8 text (TOTAL form, over read_stream). Body no longer `extern … from
 * "teko_rt"`: it calls `read_stream` and reinterprets the `[]byte` as `str` (same `{ptr,len}`, no copy).
 * Signature unchanged → no caller changes; the `tk_rt_read_file` FFI edge dies.
 * @param path  the path to read
 * @return      the content, or an error
 * @throws      when the underlying stream read fails
 * @since 0.3.1
 */
exp fn read_file(path: str): str | error
```

2. **`write_file` / `write_file_bytes` / `append_file`** over `write_stream`/`append_stream`, each
   body-only (signature intact, `io-streaming §2.5`). `str`↔`[]byte` is the sanctioned zero-copy
   reinterpret.
3. **Signature invariance is the safety net.** Because the signatures are byte-identical, the checker's
   host-primitive injection (`scope.tks`) and the codegen host-FFI lower (`emit_host_ffi`) do NOT change
   — only the `io.tks` body stops being `extern`. (If the Teko body makes the host-primitive injection
   redundant, REMOVING it is REPORTED cleanup, not part of this crumb — `§6`.)
4. **Fixpoint delta.** The emitted `teko.c` loses the `tk_rt_read_file`/`tk_rt_write_file` FFI calls in
   favor of the stream-helper calls → `gen1 ≠ gen0`; converge `gen2 == gen3`. Because `write_file →
   write_stream` writes exactly the same bytes in the same order, the emitted `teko.c` for any program
   is byte-identical to before AS OUTPUT — the identity must survive; gate HARD on `gen2 == gen3`.

## Rulings & laws

- **Teko-only:** body rewrite in `src/io/io.tks` over IO-5 helpers; the `teko_rt` FFI edge is removed
  from these four forms (the C twin itself is deleted later at S16-SWEEP, not here).
- **W15 full Javadoc** on the four rewritten forms; flatten; no inline `//`.
- **Owner decree (`io-streaming` header, `§2.5`):** the TOTAL forms remain on the surface but become
  stream-backed; signature identical so no caller changes.
- **Reported, not actioned (`§6`):** if the Teko body makes the checker host-primitive injection
  redundant, that removal is reported UP, not turned into work here.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  reseed ONLY at the fixpoint; `gen2==gen3` byte-identical; sweep `.tkt`/`.tkr` after the body change.

## Fixtures

none — the fixpoint self-build exercises this. The compiler READS its own sources and WRITES its own
`teko.c`/artifacts through these TOTAL forms during the self-build; `gen2==gen3` byte-identity (the same
bytes written in the same order) IS the regression. The isolated round-trip coverage already lives in
IO-5's fixtures.

## Gate

`[fixpoint]` — build gen2 (`--no-verify --release`, `TEKO_BACKEND=c`, `TEKO_CC=clang`,
`ulimit -v 6815744`) + scoped regression + `gen2==gen3` byte-identity. "Green" = the four TOTAL forms are
stream-backed (no `extern … from "teko_rt"` in their bodies), no caller changed, the 3-gen ladder
converges (`gen2==gen3`), and the compiler self-builds reading/writing through the stream forms.
Reseed-class: `fixpoint-rebuild`.

## Deps

`IO-5`.

## Done when

`read_file`/`write_file`/`write_file_bytes`/`append_file` bodies call the IO-5 stream helpers (no
`teko_rt` FFI), signatures are unchanged, `gen2==gen3` holds, and the compiler self-builds over the
stream-backed TOTAL forms.
