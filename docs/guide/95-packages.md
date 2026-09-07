# Packages

Teko is packaged as `mc` packages, in `mc`'s own registry — there is one integrated index,
not a teko one beside an mc one. This page separates what a build reads **today** from
what is agreed and **not yet live**; the full agreement is
[`../specs/packages.md`](../specs/packages.md).

## Today: two files at the root

| file | role |
|---|---|
| `teko.toml` | the build config — `mc build . --config teko.toml` |
| `mc.toml` | the package manifest: `[package]` only, read by `mc pkg` |

They cannot be one file: `mc pkg hash DIR` reads `DIR/mc.toml` and takes no `--config`.
The manifest carries no `[project]`, and that absence is what makes the name a **library**
rather than a tool; it also carries `toolchain = "teko"` and `licence`, both written now so
that the first publication does not have to change the manifest again.

```toml
[package]
name      = "teko"
toolchain = "teko"
licence   = "MIT OR Apache-2.0"
lib       = "lib/rt.tk"
files     = [ "core_teko.mc", "lib/rt.tk", "teko.tk", "..." ]
check     = ["mc_teko.tk"]
```

`files` is the exact list a consumer would receive and the bytes the tree hash digests;
`check` names the units the registry compiles on their own to prove the package builds.

## Today: consuming teko from a checkout

Until the package is published, a consumer names the modules of a teko checkout by path in
its own build config, and gets the taught compiler out of its own `mc build`:

```toml
[compiler]
core    = "<mc/core_min>"
modules = ["core_teko.mc", "teko.tk", "user.mc"]
out     = "build/teko"
```

That is exactly what [`teko.toml`](../../teko.toml) does for this repository, and it is the
road that keeps working after publication.

## Planned: pinning the package

Once `teko` is published, the same build config names the package instead of the paths, and
the version pins the bytes through the lock:

```toml
[deps]
teko     = "0.4.0"
teko_std = "0.4.0"

[compiler]
core    = "<mc/core_min>"
modules = ["<teko/teko.tk>", "user.mc"]
```

The library is then included by package name, with the extension **spelled out** — `mc`
drops a trailing `.mc` from an angle-bracket name and does not drop `.tk`:

```teko
// no-run
#include <teko_std/strings.tk>
```

**`teko_std` does not exist yet.** What a program includes today is the runtime,
`#include "rt.tk"`, from this repository ([runtime.md](../reference/runtime.md)).

## Planned: three shapes, and how a package is validated

| package | what it is | version line |
|---|---|---|
| `teko` | the compiler: the hook modules a consumer names in `[compiler].modules` | its own |
| `teko_std` | the standard library | **lockstep with `teko`** — same tag, released together |
| any other library | a teko library published by anyone | its own, over `[deps] teko_std` |

For a package marked `toolchain = "teko"` the registry will build the taught compiler from
`[package].modules` and the package's `[deps]` **first**, in a sandbox with no network, and
compile the `check` units with it — which is what lets a teko library declare check units
written in teko. Until that support lands, the validator compiles `check` with the stock
`mc`, which is why this repository's own check unit, `mc_teko.tk`, is written in core
syntax.

The closure rule is `mc`'s: a package reaches its own files, the libraries the binary ships
and its declared `[deps]`, and nothing else. Compiler modules **run** at build time inside
the compiler `mc build` assembles, so a teko dependency is trusted code and the lock is the
review.

`tekoc`, the compiler as an executable package, is a separate name published later: a
package's kind is fixed at its first publication, so a library and a tool cannot share one.
