# teko documentation

A C#-like language taught to [`mc`](https://github.com/minicompiler/mc): classes,
interfaces, traits, generics, delegates, compile-time dependency injection and
scope-based reference counting, on five native `(os, arch)` pairs. This tree describes
the port as it stands today, proved by [`tests/*.tk`](../tests/) and
[`scripts/check-docs.sh`](../scripts/check-docs.sh); what is designed but not yet
implemented lives in [`specs/`](specs/), never mixed into the pages describing what runs.

## The guide — task-oriented, read in order

[`guide/`](guide/README.md): install, first project, types, control flow, memory,
dependency injection, the self-hosting fixed point.

## The reference — exhaustive, read by lookup

[`reference/`](reference/README.md): [types](reference/types.md),
[classes and interfaces](reference/classes.md), [generics](reference/generics.md),
[delegates](reference/delegates.md), [arrays](reference/arrays.md),
[namespaces](reference/namespaces.md), [control flow](reference/control-flow.md),
[parameters](reference/parameters.md), [dependency injection](reference/di.md),
[memory](reference/memory.md), [the runtime](reference/runtime.md),
[the CLI and `teko.toml`](reference/build.md), every
[diagnostic](reference/diagnostics.md) the taught compiler emits, and
[what v0.1.0 refuses](reference/not-yet.md).

## The specs — designed, not yet implemented

[`specs/`](specs/README.md): self-hosting, the DI model beyond what is built, package
distribution, and the roadmap of what v0.1.0 leaves out.

## The internals — how the port itself is built

[`internals/`](internals/README.md): the 31 `.tk` modules, the pass model, reference
counting, the bootstrap ladder, CI, and known pitfalls.

## The history — frozen record

[`history/`](history/README.md): the retired standalone compiler, the decision log
entries that predate the port, and the design documents that led here.

## How this documentation is checked

`sh scripts/check-docs.sh` (also the `docs` job in CI) checks that every relative
markdown link in this tree resolves, that no fenced ` ```teko ` block goes uncompiled,
that no live page names a path or filename of the retired standalone compiler, and that
every `teko: …` diagnostic string the sources carry is listed in
[`reference/diagnostics.md`](reference/diagnostics.md). `history/` (frozen, verbatim) and
`brand/` (icon metadata, not a claim about today) are exempt from the second check; the
comment header of `scripts/check-docs.sh` has the exact rule.
