# Known debts, on the inside

What the surface refuses is [`../reference/not-yet.md`](../reference/not-yet.md), and it is
not repeated here. This page is the other kind: things that **work** and are known to be
built on something narrower than they look, or measurements that are taken and not yet
enforced. Nothing here is a promise about a version.

## Tables sized by measurement

Every module keeps fixed-size global arrays with a `TK_MAX*` cap, and the caps are sized
against a **count taken over this tree** — the self-hosted unit is the largest program the
compiler has ever compiled, and six caps were raised when it first met them. A program
substantially larger than that unit will meet one.

This is a real ceiling and it is honest: an overflow is one clear message naming the table,
never silent corruption, and raising a cap is a one-line change. But no cap is derived from
anything except a measurement, so none of them scales with the input on its own.

## Reclaim has a floor above zero

A `struct` has no vtable, so there is no release to reach and no count to keep.
`rt_live()` counts it, so a program that mixes structs with classes sees a floor rather
than a wrong answer. What is and is not reclaimed for a program
is [`../reference/memory.md`](../reference/memory.md) § What is not reclaimed.

## The object is reproducible, and not pinned

The fixed point compares `teko2.o` with `teko3.o` **within one run**. Whether `teko2.o` is
byte-identical **across** runs and machines is a different claim, and this repository does
not pin it: the five `fixpoint` legs print the sizes and sha256 of every object into their
job summary, reported and not gated. Pinning a golden hash is possible only once the printed
numbers have been watched long enough to be worth pinning.

Two facts stand today, both from those summaries. `teko1.o == teko2.o` — the ladder reaches
its fixed point on the **first** turn, which says the stock `mc`'s code generator and teko1's
own agree on this input. And a single divergence of `teko1.o` between two runs on one
machine was seen once and has not reproduced since; the provenance table exists so that the
next one, if there is one, is attributable without rerunning anything
([bootstrap.md](bootstrap.md)).

## The self-hosted unit has to stay core syntax

`mc_teko.tk` is both the unit the fixed point compiles and the package's `check` unit — and
a `check` unit is compiled by a **stock** `mc`, with no teko loaded, in the registry's own
sandbox. It passes today because the modules are written in mc's own surface. Writing them
in teko's surface instead would leave the package with no `check` unit at all: the other two
candidates were measured and both fail on a stock `mc`, one because it is a fragment that
assumes the core is already included, the other because its signature uses a word teko
teaches.

## The aggregator's name is a key

`mc build ngen && run` names a directory that no longer exists. It cannot be renamed here
alone: a required check is matched by name, and renaming the job without changing `main`'s
ruleset in the same step leaves every pull request waiting for a check nobody reports.
Renaming it is a step of its own ([ci.md](ci.md)).

## Windows needs more than the script

The ladder runs on Windows only with `--linker-toml` and a sysroot built first: there is no
C runtime and no direct-executable backend there, so `cc` is not a linker and the default
`[linker]` block does not apply. CI assembles the three files with a composite action; a
local Windows run has to do the same by hand.

## Coverage the static analysis does not reach

CodeQL has no analyzer for `.tk` or `.mc`. What is scanned is `actions` — the workflows and
the composite actions — which is the part that downloads a toolchain, publishes with
`contents: write` and interpolates branch names. The language itself is proved by the
fixtures, the fixed point and the documentation gate, and by nothing else.

The branch-policy gate is in the same shape from the other side: it refuses `theory/**` and
`cargo/**` against a protected base, and neither namespace is in use. It is a required check
that currently cannot fail. Rewriting it to name what the process actually forbids, or
retiring it, is a ruleset decision rather than a workflow edit.

## The limits tolerance is inherited

`[limits] tolerance = 1.0` in [`../../teko.toml`](../../teko.toml) is a starting point taken
from a much smaller project, and the static estimate `mc limits` computes is a function of
source bytes. It has not been re-derived for a compiler this size; `mc limits . --config
<config>` is what says whether it still holds, and it is run rather than assumed.
