# teko documentation

A C#-like language taught to [`mc`](https://github.com/minicompiler/mc): classes,
interfaces, traits, generics, delegates, compile-time dependency injection and
scope-based reference counting, on five native `(os, arch)` pairs. This tree describes
the port as it stands today, proved by [`tests/*.tk`](../tests/) and
[`scripts/check-docs.sh`](../scripts/check-docs.sh); what is designed but not yet
implemented lives in [`specs/`](specs/), never mixed into the pages describing what runs.

## The guide — task-oriented, read in order

[`guide/`](guide/README.md), twelve numbered pages: [getting
started](guide/00-getting-started.md) (the pinned `mc`, the taught compiler, a first
program, the fixed point), [values and types](guide/10-values-and-types.md),
[classes](guide/20-classes.md), [generics](guide/30-generics.md), [delegates and
lambdas](guide/40-delegates-and-lambdas.md), [control flow](guide/50-control-flow.md),
[parameters](guide/60-parameters.md), [namespaces and
imports](guide/70-namespaces-and-imports.md), [dependency
injection](guide/80-dependency-injection.md), [memory](guide/90-memory.md),
[packages](guide/95-packages.md) and [what is not there
yet](guide/99-what-is-not-there-yet.md).

## The reference — exhaustive, read by lookup

[`reference/`](reference/README.md): [types](reference/types.md),
[classes and interfaces](reference/classes.md), [generics](reference/generics.md),
[delegates](reference/delegates.md), [arrays](reference/arrays.md),
[namespaces](reference/namespaces.md), [control flow](reference/control-flow.md),
[parameters](reference/parameters.md), [dependency injection](reference/di.md),
[memory](reference/memory.md), [the runtime](reference/runtime.md),
[the CLI and `teko.toml`](reference/build.md), every
[diagnostic](reference/diagnostics.md) the taught compiler emits, and
[what v0.4.0 refuses](reference/not-yet.md).

## The specs — designed, not yet implemented

[`specs/`](specs/README.md): [the surface policy](specs/surface.md), [how teko is packaged
and published](specs/packages.md), [self-hosting](specs/self-hosting.md), [the DI model
beyond what is built](specs/dependency-injection.md), [the typed variadic
list](specs/params-typed.md), [`DateTime` and `TimeSpan`](specs/datetime.md),
[`decimal`](specs/decimal.md), and a [draft roadmap to v1.0.0](specs/roadmap-1.0.md).

The types C# has and teko does not are designed there too, in one ordered sequence
([the order they land in](specs/README.md)): [the missing
integers](specs/small-ints.md), [`enum`](specs/enum.md), [`Guid`](specs/guid.md),
[`string`](specs/string.md) and [the three derived date
types](specs/datetime-extras.md).

## The internals — how the port itself is built

[`internals/`](internals/README.md): the [31 `.tk` modules](internals/modules.md), the
[pass model](internals/passes.md), the [node table](internals/nodes-and-xt.md),
[reference counting](internals/runtime.md), [dependency
injection](internals/di.md), the [bootstrap ladder](internals/bootstrap.md),
[CI](internals/ci.md), the [pitfalls](internals/pitfalls.md) and the
[known debts](internals/debts.md).

## The history — kept elsewhere

[`history/`](history/README.md): a pointer. The frozen record — the retired standalone
compiler, the decision log entries that predate the port, the design documents that led
here — lives in the private repository `teko-org/teko-history`, in Portuguese. This
repository is English-only and describes only what runs today.

## Where this documentation is published

This tree is the source of [teko-lang.org](https://teko-lang.org). The four sections above
become the site's four sections; `home-extra.md` is the home page's prose, which is why it
sits at this root and belongs to no section; `brand/` and `history/` are not rendered, and
a link to either resolves to the file on GitHub. The generator is `mcsite`,
`minicompiler/mc`'s own, built from the release `MC_VERSION` pins and never vendored here —
[`site/README.md`](../site/README.md) is how it is configured and how to look at a page
locally.

## How this documentation is checked

`sh scripts/check-docs.sh` (also the `docs` job in CI) checks that every relative
markdown link in this tree resolves, that no fenced ` ```teko ` block goes uncompiled,
that no live page names a path or filename of the retired standalone compiler, and that
every `teko: …` diagnostic string the sources carry is listed in
[`reference/diagnostics.md`](reference/diagnostics.md). It also checks that no tracked
source carries Portuguese: this repository is English-only, and Portuguese belongs in chat
with the maintainer or in the private history repository. `brand/` (icon metadata, not a
claim about today) is exempt from the legacy check; the comment header of
`scripts/check-docs.sh` has the exact rules.
