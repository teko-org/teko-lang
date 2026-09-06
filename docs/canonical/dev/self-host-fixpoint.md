# Self-Hosting and the Fixpoint

Teko's compiler is written in Teko. The whole point of that arrangement is that the compiler
compiles itself, and the result of doing so twice in a row is **byte-identical** — a fixpoint.
This document describes the bootstrap chain and the verification discipline built around it.

## Why self-hosting needs a bootstrap chain at all

A compiler written in its own language cannot compile itself from nothing — something has to
produce the first working binary. Teko's answer is a **released seed**: a previously published
`teko` binary, fetched at build time (`scripts/fetch_teko.sh`), that compiles the current
source tree. Every ordinary contributor and every CI job starts here; nobody hand-builds a
bootstrap compiler from scratch.

## The generation chain and where the fixpoint is measured

```
released seed --(C backend)--> gen0 --(C backend)--> gen1 (this generation still emits C)
                                            │
                                            ▼ (native backend, no environment override)
                                          gen2 --(native)--> gen3
                                                                 ▲
                                                    ASSERT gen2 == gen3 (byte-identical)
```

The fixpoint assertion is fixed at **`gen2 == gen3`**, and deliberately never slides to
`gen1 == gen2`. `gen1` is C by construction — it is the last generation compiled through a seed
that itself only knows how to go through C — while `gen2` is native. Two different code
generators (the host `cc` versus Teko's own backend) are not expected to agree byte-for-byte,
for the same reason the same generated C compiled by `cc` and by `musl-gcc` are two different
binaries: the fixpoint begins where the compiler starts consuming its **own** output, which is
gen2 onward.

## Per-platform native migration

Whether a given platform generates `gen2`/`gen3` via the native backend or (temporarily) still
via C is decided **per platform**, not globally — each platform flips to native the moment its
own native backend can compile the compiler on that platform. The versioning plan sequences
this by platform-count, not by ease: the four Linux legs
(`x86_64-glibc`/`x86_64-musl`/`arm64-glibc`/`arm64-musl`) migrate together first, because they
measure the *same* shared lowering machinery four times over — maximizing validation confidence
per unit of work — before macOS and Windows (each a single leg) migrate. Once every platform
has migrated, the C backend is retired as a production path everywhere and survives only as
the historical bootstrap bridge described below.

## The rung ladder — bridging a capability gap, never rewriting the past

A released seed builds the **base lineage** it was released from — not necessarily the tip of
every in-flight branch. When a branch adds a language/codegen capability the seed doesn't have
*and* removes the corpus that worked without it, the seed cannot build that branch's own head
directly. The fix is never to rewrite the new corpus back down to what the old seed accepts —
that treats the bridge as more important than the destination. Instead, a bounded, self-limiting
climb finds the **rung**: the newest ancestor commit the compiler-in-hand *can* build, builds
that, and re-attempts the target with the resulting, slightly newer compiler — repeating until
it reaches the tip or a stage cap is hit. The rung is always **discovered by probing**, never
assumed from a fixed guess (a merge-base guess reliably lands on the wrong side of exactly the
kind of jump this ladder exists to cross).

`bootstrap/teko.c` is the artifact this ladder produces, not consumes: once a rung's build goes
green, its `teko.c` (the C emitted by the last C-backend generation on that lineage) is
harvested and versioned — an **output**, stored after success, never a hand-edited input. A
versioned `teko.c` may only be *used* when a matching `bootstrap/DEGRAU` declaration exists —
a written, human statement of *why* the released seed cannot reach this point directly. The
presence of the C file alone is not permission to use it; the declaration is the bridge.

## The gate: no C survives past `gen1`

The binary criterion for "the C emitter is truly gone" is simple and admits no partial credit:
**`gen1`, before it compiles `gen2`, must first remove every `.c`/`.h` file the seed produced,
and gen1 must still build without them.** If gen1 compiling gen2 still depends on any C
artifact — analyzer output, test scaffolding, or `teko.c` itself — the ladder has been climbed
incorrectly. There is no accepted justification for a nonzero C baseline at this boundary; the
target is exactly zero.

## Verification discipline

Every change that touches the compiler is verified, before it is considered done, by:

```sh
./bin/teko build . -o /tmp/gen1      # rebuild the compiler with itself
./bin/teko test .                     # the full native test gate, coverage enforced
```

with the fixpoint assertion (`gen2 == gen3`, byte-identical generated output) checked as part
of the same pass. A change is not "probably fine because the diff looks right" — it is proven
by actually compiling and running what changed. Verification happens on the working branch
*before* work is considered complete, but the branch is pushed to its remote *before* the
(potentially long) verification run starts — so a lost session never loses work that was never
pushed, only work whose verification result hadn't yet been recorded.
