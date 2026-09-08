# The internals

How the port itself is built. Teko has no compiler of its own: it is a set of **hook
modules** that teach [`mc`](https://github.com/minicompiler/mc) the constructs teko adds on
top of mc's base grammar, plus a driver that gives the resulting binary its own command
line. Everything below is the machinery behind
[the reference](../reference/README.md) — a reader who only wants to write teko does not
need it.

| page | covers |
|---|---|
| [modules.md](modules.md) | the 33 module files, one entry each: what it teaches, what it registers, what it keeps |
| [primitives.md](primitives.md) | a primitive with members: the lowering table, the two identity casts, the four sites that read it, and the P0 probes that measured the mechanism |
| [passes.md](passes.md) | the fifteen `pass()` registrations, in order, and why the order is what it is |
| [nodes-and-xt.md](nodes-and-xt.md) | the node table keyed by position, in-place rewriting, and the rule about which node carries the type |
| [runtime.md](runtime.md) | [`lib/rt.tk`](../../lib/rt.tk) from the compiler's side: the layouts it assumes and the calls the passes emit into it |
| [di.md](di.md) | the service registry, `inject` as a deferred placeholder, and how a scope becomes locals of a block |
| [bootstrap.md](bootstrap.md) | the teko0 → teko3 ladder, the derived configs, and what the criteria compare |
| [ci.md](ci.md) | the workflows, the twelve jobs every push runs and the one check `main` requires, the composite actions and the release |
| [pitfalls.md](pitfalls.md) | the traps this port has already paid for, each as a rule |
| [debts.md](debts.md) | what is known to be missing on the inside |

## How teko is taught to mc

`mc` reads its hook modules **before** it reads a program, and every registration a module
makes is in force from then on. Three files decide what gets registered:

| file | role |
|---|---|
| [`teko.tk`](../../teko.tk) | `#include`s the 33 `teko_*.tk` modules, defines `teko_init()` — every `syntax*`, `on_stmt`, `pass()` and lexer callback in one place — and holds the `build`/`limits` subcommand handlers |
| [`core_teko.mc`](../../core_teko.mc) | the taught compiler's own `main()`: the parts of the mc core teko links, and the two `subcommand()` entries that make the binary answer to `teko` rather than to `mc` |
| [`user.mc`](../../user.mc) | the project's own `user_init()`, whose whole body is `teko_init()` |

The split between the last two is packaging, not taste: a package never defines
`user_init`, it exports `<name>_init()` and the consuming project's own module calls it. So
`teko.tk` exports `teko_init()`, and `user.mc` — which a consumer replaces with its own —
calls it. [`teko.toml`](../../teko.toml) names the three in order:

```toml
[compiler]
core    = "<mc/core_min>"
modules = ["core_teko.mc", "teko.tk", "user.mc"]
out     = "build/teko"
```

`user.mc` comes last because `teko_init()` has to be declared before it is called.

## The four parts on top of `<mc/core_min>`

`[compiler].core` names the minimal core; `core_teko.mc` adds exactly the four parts this
project uses, and calls the pieces of each rather than the bundle's own `init`:

| part | why |
|---|---|
| `<mc/core_machines>` | arm64 and x86-64 — every CI leg needs both |
| `<mc/core_writers>` | Mach-O, ELF and COFF, plus the two direct-executable backends |
| `<mc/core_build>` | `mc build --entry-only`, how each fixture is compiled |
| `<mc/core_bundle>` | `#include <name>`, which [`lib/rt.tk`](../../lib/rt.tk) needs for `<sys>` |

`<mc/core_pkg>` and `<mc/core_sandbox>` are left out: nothing here calls `mc pkg`,
`mc update` or `mc sandbox`. `mc_build_init()` itself is never called — it would register
mc's own `build`/`limits` entries beside teko's, and `subcommand_usage()` prints every
registration whichever one wins the dispatch. Calling its four public pieces instead keeps
the printed table exactly teko's two lines.

## Which sources teko's words apply in

A word a module registers is reserved **program-wide** unless the dialect says otherwise,
and a self-hosted teko has to read mc's own `.mc` files, where `type`, `out` and `params`
are ordinary parameter names. `tk_source_claim` (`teko_fwd.tk`) is the filter:

| source | read with |
|---|---|
| any name ending in `.tk` — a program, a fixture, `lib/rt.tk`, the modules themselves | teko's vocabulary |
| a frame teko itself pushed (a generic instance, a trait body, a `params` instance, the loop prelude) | teko's vocabulary — `tk_push_source` raises a flag for exactly that push |
| `<mc/host>`, `<mc/core_min>`, `<sys>`, `<prelude>`, every `.mc` | mc's own vocabulary, unchanged |

The claim is asked once per frame, at push time, and re-asked for frames already open when
the handler registers — so `tk_fwd_init()` running first in `teko_init()` is honest
bookkeeping rather than a requirement.

## Self-hosting is one unit

[`mc_teko.tk`](../../mc_teko.tk) is the whole compiler as a single translation unit, and it
is nothing but includes:

```mc
#include <mc/host>
#include <mc/core_min>
#include "core_teko.mc"
#include "teko.tk"
#include "user.mc"
```

Because the file is named `.tk`, `tk_source_claim` claims it and teko's vocabulary applies
inside it; the four includes reach sources that are **not** claimed, which is what lets mc's
own files keep `type`, `out` and `params` as parameter names. Having no identifier of its
own, the file cannot collide with a taught word. It is also the package's `check` unit —
the unit a registry validator compiles on its own — which is why it stays in core syntax.
[bootstrap.md](bootstrap.md) is the ladder built from it.

## The budget

`mc limits . --config <config>` prints every table the taught compiler occupies against
what mc reserves. The numbers that matter here are the hook tables: today
`passes 15/30`, `syntax 15/30`, `alias 17/34`, `types 10/20`, `on_stmt 4/8`,
`on_source 1/8`, `source_claim 1/8`, `syntax_param 1/8`, `syntax_type 1/8` — and
`intrin 8/16`, all eight of them mc's `<float>` library's. Teko registers **no intrinsic of
its own**: every function it emits a call to has surface code somewhere in this repository.
