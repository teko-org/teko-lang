# Roadmap to v1.0.0

> **This page is a draft, pending the mc project's counterpart.** Half of what v1.0.0 needs
> is not this repository's to build, and the other half depends on when it lands.
> [D17](../../DECISION_LOG.md) is the one thing already settled: **v1.0.0 ships only together
> with `mc` 1.0.0**, which is also when the port is considered closed. Everything below is a
> proposal for what "ready" should mean, not a commitment and not a schedule.

The release before it is **v0.4.0**, and it is not a step towards this list: it is a cut of
what runs today ([`../reference/`](../reference/README.md)).

## What teko owes

**A surface with no blocking refusal.** Not an empty
[`not-yet.md`](../reference/not-yet.md) — some entries there are decisions, not gaps. The
criterion is that nothing on the list stops an ordinary program from being written: a
multidimensional array, an array of objects, a typed variadic list
([params-typed.md](params-typed.md)), a delegate that is a value in every position. Each
remaining entry has to be either implemented or restated as a decision with a reason.

**`teko_std` published.** The compiler alone is not a language a program can be written in.
The library is versioned in lockstep with `teko` — same tag, released together — because a
library compiled by a compiler that does not know its surface is not a library.

**Self-hosting on the five pairs.** Already true, and it has to stay true through everything
above: linux/x86_64, linux/aarch64, macos/aarch64, windows/x86_64, windows/aarch64, each on
a runner of that pair ([self-hosting.md](self-hosting.md)).

**A pinned golden object.** Today the fixed point is proved **within** a run and the object
hashes are reported and not gated. A 1.0 should pin `teko2.o` per pair, with the rule for
updating the pin written down. That is a fourth criterion, not a stricter version of the
existing three ([the internal debts](../internals/debts.md)).

**Zero silently wrong results.** The rule of the cut, held to at 1.0 the way it is held to
now: every construct either does what it says or is refused where it is written, and every
message is catalogued. The measurable form is that the diagnostics gate stays exhaustive and
that no refusal is downgraded to a warning to make something compile.

**Documentation green, and the site with it.** `sh scripts/check-docs.sh` compiles and runs
every example on this site. A 1.0 keeps that property; it does not acquire a second class of
example that is only illustrative.

**`tekoc` installable.** The compiler as a tool, under its own package name and its own
`kind` — a user who wants teko should not have to know what a hook module is
([packages.md](packages.md)).

## What depends on `mc`

Nothing that blocks a 1.0. The list this section used to carry has closed or moved:

| item | where it stands |
|---|---|
| the registry's **R2** (the taught compiler built from `[package].modules` and `[deps]` before validating) and **R3** (`language = "teko"`, derived from `[deps] teko`) | **delivered and proved**: `teko_std` 0.7.2 is on the index, its `check` unit compiled by a compiler the validator built from `<teko/teko.tk>` and `<teko/user.mc>` (D43's manifest shape) |
| a wall clock, an entropy source, `<i128>` on x86-64 | **teko's own** (the owner's ruling of 2026-09-08): an `extern` per target host, a `bcrypt.def` in teko's own Windows sysroot, a machine module for a primitive — the tooling `mc` already gives. Nothing is asked of `mc` for them |
| **`mc tool install`** (the mc project's C3) | `tekoc` as an installable tool; until it exists, the road that runs is `[compiler] modules`, and that road stays valid afterwards |
| **stdlib 0.16.0** — `<float>` and the two float machines moving into the `stdlib` package | one include line here (`[deps] stdlib`, `<stdlib/float.mc>`) when it ships |
| the hook API | **stable**: `mc` no longer changes it and works on real core defects only (the owner, 2026-09-08). teko treats the API pinned since 0.15.18 as the surface it builds on, proves every pin raise by the whole recipe (D29, D35, D37), and reports a core defect with a pure-mc reproducer — never a feature request |
| a minimum `mc` a package declares (`[package].mc`) | the owner's topic with the `mc` project directly; not a teko ask |

Everything teko is, is hooks: fifteen passes, fourteen `syntax` registrations, a
`source_claim`, an `on_source`, a `syntax_param`, a `syntax_type`. That is why a pin is
raised only after the whole recipe is green on the new release, and why the 1.0.0 of teko
ships together with the 1.0.0 of `mc` — the owner's rule, not a dependency on a change.

## Raising the pin, and a canary

The procedure in force is in [the CI internals](../internals/ci.md): run the whole recipe
against the new release, then `sh scripts/bootstrap.sh` for `FIXPOINT OK`, and only then
write `MC_VERSION`. Never the file first.

Two additions this page proposes, both **pending agreement with the mc project**:

- **A baseline that is a number, not a memory.** Alongside the pin, record what the pinned
  release measured — the object sizes and hashes the fixpoint legs already print, and the
  `mc limits` tables. A pin raised without a before-and-after is a pin raised on faith.
- **A cross-repo canary.** A job on the `mc` side that builds *this* tree against an
  unreleased `mc` and reports, so a hook change is noticed where it is made rather than in
  the gate of every open pull request here. It needs both projects to agree on what a red
  canary obliges: today an unannounced release can change what CI tests, and the only defence
  is that CI does not resolve `latest`.

## Cadence

`vX.Y.Z`, mc's own three-part format. Publication only from a stable version. The next
release is v0.4.0; between it and 1.0.0 the shape of the intermediate cuts is not decided
here — that is part of what this draft is waiting on.
