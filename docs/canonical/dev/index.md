# Teko — Developer / Contributor Reference

Teko is a **self-hosting, all-native compiler**: the compiler is written in Teko
(`src/**/*.tks`, canonical), compiles its own source tree, and the result rebuilds itself to a
byte-identical fixpoint (generation N == generation N+1). The only C the tree still carries
by design is the small execution runtime the generated programs link against
(`src/runtime/teko_rt.{c,h}`) and its `assert` twin — everything else, including the compiler
itself, is Teko.

## Map of this tree

| Document | Covers |
|---|---|
| [`architecture-pipeline.md`](architecture-pipeline.md) | The compilation pipeline: lexer → parser → checker → monomorphization → consteval → LIR → native backend, module layout, `src/` map |
| [`memory-model.md`](memory-model.md) | Arena regions, the spine (points-to/uniqueness inference), `adopt`, `unsafe`, the reference model, `isolate` concurrency |
| [`native-backend.md`](native-backend.md) | The own AOT backend: LIR, instruction selection, register allocation, encoders, object writers, linking, the target matrix |
| [`testing-coverage-journal.md`](testing-coverage-journal.md) | The native test gate, coverage floors, the parallel test harness, the run journal |
| [`self-host-fixpoint.md`](self-host-fixpoint.md) | Bootstrap seed, the rung ladder, `gen2 == gen3` fixpoint, per-platform native migration |
| [`ffi-and-runtime.md`](ffi-and-runtime.md) | `extern`, marshalling, `teko_rt`, the C boundary and its containment |
| [`debugger.md`](debugger.md) | `tdb`, `.tsym`, DWARF-as-interop, editor debugging |
| [`lsp-and-tooling.md`](lsp-and-tooling.md) | The `teko lsp` server, editor clients, formatter/doc/lint/repl |
| [`contributing.md`](contributing.md) | Branch model, wave/train discipline, W15 quality bar, versioning |

## Orientation: what "compiler" means here

There is exactly one production compiler binary, `teko`, self-hosted. Native
AOT is the sole engine — see [`self-host-fixpoint.md`](self-host-fixpoint.md). The compiler's
source lives under `src/`, one namespace-directory per stdlib/compiler module
(`teko::lexer`, `teko::parser`, `teko::checker`, `teko::lir`, `teko::backend`,
`teko::runtime`, `teko::io`, …), with the source root itself invisible in addressing.

Tests live beside the code they test: `foo.tks` + `foo_test.tkt`, functions annotated `#test`,
run by `teko test .` — see [`testing-coverage-journal.md`](testing-coverage-journal.md).
