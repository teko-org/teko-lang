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

None of these is teko's to write, and each changes what teko can promise.

| needed from `mc` | what it unblocks here |
|---|---|
| the registry's **R2** — building the taught compiler from `[package].modules` and the package's `[deps]` before validating | a teko library can declare `check` units written in **teko**; until then every one of them has to be core syntax |
| the registry's **R3** — classifying on `[package].language = "teko"`, or on a `[deps] teko` when the key is absent | the key is written into `mc.toml` today and ignored; R3 is what makes it mean something |
| **`mc tool install`** (the mc project's C3) | `tekoc` as an installable tool. Until it exists, the road that runs is `[compiler] modules`, and that road stays valid afterwards |
| **stdlib 0.16.0** — `<float>` and the two float machines moving into the `stdlib` package | teko declares `[deps] stdlib` instead of relying on what the release binary bundles |
| **the hook API frozen at 1.0.0, with a deprecation policy** — a package taught in a `1.x` teko validates on any `1.y` `mc`. Until it lands, the registry's validator compiles `check` with the same `mc` release it itself pins, and a hook API break between the validator's release and a package's own tag forces a re-tag, not a silent re-validation (D35) | the pin can move without a survey of every module. This is the one that decides whether a 1.0 is maintainable at all |
| **`[package].mc`, the minimum `mc` release a package reads as its own floor** | today a package names no minimum, so a tag validated once can start failing under a newer validator's `mc` for a reason the package's own `mc.toml` says nothing about (the fork this pin closes, D35). A floor `mc` itself checks before it builds is what lets a package say "I need at least this hook surface" instead of the registry silently rebuilding on whatever release happens to run |

The last one is worth stating plainly. Everything teko is, is hooks: fifteen passes, fourteen
`syntax` registrations, a `source_claim`, an `on_source`, a `syntax_param`, a `syntax_type`.
A patch release of `mc` that changes what one of them returns changes this compiler. Today
that is handled by pinning and by proving before raising the pin; a 1.0 needs the guarantee
on the other side.

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
