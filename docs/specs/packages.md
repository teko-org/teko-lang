# Packages: teko in mc's registry

**Designed, not yet built.** This page records the agreement between this project and
[`mc`](https://github.com/minicompiler/mc) on how teko is published and consumed. The
decisions in force are [D15 and D16](../../DECISION_LOG.md); what a build reads today is
[the build reference](../reference/build.md).

## One integrated registry

There is **one** index, not a teko one beside an mc one. `pkg.minicompiler.dev` is its
canonical host and `mc pkg` reads `<registry>/index/<name>.toml` from it. **`pkg.teko-lang.org`
is an alias host of the same index** — the same generator, the same bytes, the same cache
policy and the same 404 — so a teko package resolves identically under either name, and
nothing is stranded by which host a lock happens to carry. The registry also answers a
**read-only `/mcp` endpoint**: an MCP server over the same rows, for searching and reading a
package's metadata. Nothing is published through it.

One index means one namespace: a teko package and an mc package cannot share a name.

## A teko package is an mc package

The manifest is `mc`'s, with one key added:

```toml
[package]
name      = "teko_std"
toolchain = "teko"
licence   = "MIT OR Apache-2.0"
```

`[package].toolchain = "teko"` is what the registry classifies on. `mc` itself ignores an
unknown `[package]` key, so the manifest keeps working with the pinned release; the registry
records the key when it grows support for it. Everything else — `files`, `check`, `lib`,
`[deps]`, the lock, the tree hash — is `mc`'s and is not restated here.

**Validation.** For a package marked `toolchain = "teko"` the registry **builds the taught
compiler first**, from `[package].modules` and the package's `[deps]`, and compiles the
`check` units **with it**, in a sandbox with no network. That is what lets a teko library
declare check units written in teko: without the key they would be handed to the stock `mc`,
which does not know the surface.

## The three shapes

| package | what it is | version line |
|---|---|---|
| `teko` | the compiler: the hook modules a consumer names in `[compiler].modules` | its own |
| `teko_std` | the standard library, source a program includes | **lockstep with `teko`** — same tag, released together |
| any other library | a teko library published by anyone | its own, over `[deps] teko_std = ">= x"` |

`teko` and `teko_std` move together because a library compiled by a compiler that does not
teach its surface is not a library. Everything else evolves on its own line and states the
`teko_std` it needs as an ordinary dependency.

## The manifest at the root

- **`mc.toml` at the repository root, `[package]` only.** No `[project]`: that absence is what
  makes the registry classify the name as a library rather than an executable.
- **The build config is a separate file** (`teko.toml` here), because `mc pkg hash DIR` reads
  `DIR/mc.toml` and takes no `--config`. It is **not** listed in `files`: it describes how this
  repository builds itself, not what a consumer receives.
- **`licence`** is an SPDX identifier, and the registry requires it at publication.
- **`files`** is the exact list a consumer receives, in `LC_ALL=C` byte order.
- **`check`** names the units the registry compiles to prove the package builds. Every `check`
  entry is also in `files`.

## Consuming teko

A consumer pins the two packages and names the taught compiler in its own build config:

```toml
[deps]
teko     = "0.4.0"
teko_std = "0.4.0"

[compiler]
core    = "<mc/core_min>"
modules = ["<teko/core_teko.mc>", "<teko/teko.tk>", "user.mc"]
out     = "build/teko"
```

and includes the library by package name:

```c
#include <teko_std/strings.tk>
```

The extension is **spelled out**. `mc` drops a trailing `.mc` from an angle-bracket name, so
`<geo/geo.mc>` and `<geo/geo>` are one name; `.tk` is not dropped, and `<teko_std/strings.tk>`
is written in full.

Installing the compiler as a tool is the road that opens once `mc` ships `mc tool install`;
the `[compiler].modules` road runs today and stays valid afterwards.

**Note:** `[replace]` in `mc.toml` does not spare an unpublished teko package from being fetched. To
develop against an unpublished teko, use the vendored path (`deps/teko` on the tag, with `mc.lock` pinning
its tree hash), as `teko-org/teko-std` does.

## The closure rule

**A package reaches its own files, the libraries the binary ships, and its declared `[deps]` —
nothing else.** A consumer cannot include a file of `teko`'s tree that `teko` did not publish,
and `teko_std` cannot reach the consumer's sources. Together with the lock, which pins the
bytes, that is the brake on a compiler-module package: those modules **run** at build time,
inside the compiler `mc build` assembles, so a teko dependency is trusted code and the lock is
the review.

## The mc 0.16.0 migration

Today `teko_float.tk` includes `<float>`, `<machine_arm64_float>` and
`<machine_x86_64_float>` — the float library and the two float machines — and they are
answered by the bundle inside the `mc` binary. On **mc 0.16.0 they move into the `stdlib`
package**: teko declares `[deps] stdlib` and the same names are answered by the lock instead
of the bundle. Raising [`MC_VERSION`](../../MC_VERSION) to 0.16.0 and declaring that
dependency is one change, made after the whole local recipe is green on the new release.

## What is still missing here

- `teko` declares `licence`; `[package].modules` is needed only where the `check` unit needs the taught compiler.
- The registry records `[package].toolchain` when its own support lands.
- `tekoc`, the compiler as an executable package, is a separate name published later: a
  package's kind is fixed at its first publication, so the library and the tool cannot share
  one.
