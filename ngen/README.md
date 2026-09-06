# ngen — teko ported onto mc

`src/` is **frozen** (DECISION_LOG.md D211/D212). teko no longer grows there: every
new construct is taught to [`mc`](https://github.com/minicompiler/mc) as a module of
its own, the way `mc`'s `examples/lang` teaches it a class system without touching
`mc`'s `src/`. This directory is that module, built with `mc`'s own project tooling
(`mc.toml`, `mc build`, `[compiler]`).

Design and rationale: `docs/design/port-teko-mc.md`, `docs/design/plano-ngen-entrega4.md`
and `DECISION_LOG.md` D211-D214/D225/D226/D229/D230. Day-to-day state (what each entrega
taught, gates, measured limits): `ngen/HANDOFF.md`.

## Building

Needs the [`mc`](https://github.com/minicompiler/mc) toolchain on `PATH`, at the version
`ngen/MC_VERSION` names (`cat ngen/MC_VERSION`; `.github/workflows/ngen.yml` pins to it
too — see "Toolchain version" below). From the repository root:

```sh
mc build ngen
ngen/build/teko-hello   # exits 42
```

Two steps come out of `mc build` (`mc`'s own `docs/build.md`): first it links `teko.tk`
into a taught compiler (`ngen/build/teko`), then it uses THAT binary to compile
`ngen/tests/hello.tk` into `ngen/build/teko-hello`. Nothing in `mc`'s `src/` changes, and
nothing in this repository's `src/` changes either.

On a host `ngen/mc.toml` (which targets `linux/x86_64`) does not link on, derive a config
with the host's own `[target] os`/`arch` and drop `[linker]` so the built-in `macho-exe`/
ELF backend writes the binary itself (`ngen/HANDOFF.md` §4 has the full recipe; never edit
`ngen/mc.toml` itself outside an authorized crumb).

## Fixtures

Each `ngen/tests/*.tk` is a program the taught compiler builds and RUNS; its
`// expect-exit: N` header is the oracle. `.github/workflows/ngen.yml` compiles and runs
every one of them on five native legs (linux/x86_64, linux/aarch64, macos/aarch64,
windows/x86_64, windows/aarch64) with `--entry-only`, reusing the compiler `mc build ngen`
already produced instead of rebuilding it per fixture:

```sh
for src in ngen/tests/*.tk; do
  n=$(basename "$src" .tk); w=$(grep -m1 '// expect-exit:' "$src" | sed 's/.*expect-exit: *//')
  sed -e "s#^entry = .*#entry = \"tests/$n.tk\"#" -e "s#^out   = .*#out   = \"build/$n\"#" \
      ngen/mc.toml >"ngen/mc.$n.toml"
  ngen/build/teko build ngen --config "ngen/mc.$n.toml" --entry-only && "ngen/build/$n"
  echo "$n exit=$?  want=$w"; rm -f "ngen/mc.$n.toml"
done
```

## Self-hosting: `sh ngen/scripts/bootstrap.sh`

The fixed point of the self-hosted compiler: `teko0` (the stock `mc` building `ngen`) →
`teko1` → `teko2` → `teko3`, each stage compiling `ngen/mc_teko.tk` with the compiler the
previous stage produced. Green means `cmp` of the objects `teko2.o`/`teko3.o` is empty,
`--dump-asm` of teko2 vs teko3 diffs empty, and teko1 compiles and runs every fixture —
printed as `FIXPOINT OK`. Runs in CI as the `fixpoint` job on five native legs (linux/x86_64,
linux/aarch64, macos/aarch64, windows/x86_64, windows/aarch64), covering the same pairs as the
build matrix (`ngen/HANDOFF.md` §3.1).

## Toolchain version

The mc release this repository builds and tests against is **pinned** in
`ngen/MC_VERSION` (one line, e.g. `0.15.12`) — `.github/actions/setup-mc` reads it by
default; nothing resolves `latest` on its own. Bumping it: get the baseline green against
the new `mc` locally (45/45 fixtures, then `sh ngen/scripts/bootstrap.sh` → `FIXPOINT OK`)
**before** editing the file (`ngen/HANDOFF.md` §3.2 has the log of what each version
changed and why).

## How this will be consumed

Not shipped yet (v0.1.0 is still ahead), but the shape is decided (D230):

* **The runtime**, as source: `[deps] teko = "x.y.z"` in the consumer's `mc.toml`, then
  `#include <teko>` in their `.mc`/`.tk`.
* **The taught compiler**, two roads: today, `[compiler] modules = ["<teko/teko.tk>",
  "user.mc"]` in the consumer's own project (their `mc build` links `teko` locally,
  pinned by the dependency's tree hash); later, `teko` as a tool package
  (`mc tool install teko`).
* **The registry** is `https://pkg.minicompiler.dev` (D230; `module =` in `mc.toml` is a
  key the registry and `mc` both ignore today — kept as documentation, not semantics).

## What is OUT of v0.1.0

Refused with a `teko: <cause>` message, never silently wrong (the rule the "v0.1.0
estável" detour cuts by, `docs/design/plano-ngen-entrega4.md` §76/§77):

* `Func<>`/`Action<>` (generic delegate types) and delegate multicast (`+=`/`-=`).
* `params T[]` wrapping a lambda, and a lambda's own target-typing.
* `T[][]` / multidimensional arrays.
* Nested `namespace` (one level only) and qualified generics (`using static`).
* Dependency injection resolved through a generic call (`Services.Get<T>()`); the
  marker-interface form (`IServiceSingleton`/`IServiceScoped`/`IServiceTransient` +
  `inject`, D229) IS implemented.
* A float argument or element inside a `params` list (the list is words; `f32`/`f64`
  live in a different register bank — refused where the type is visible, silent
  elsewhere by the same rule field-store checks already follow).
* `when` on a switch expression's textually LAST `_` arm (that arm is the chain's
  unconditional base — its guard would never run).

## Local validation without network

A sandbox with no `github.com` access cannot download `mc` or self-host it; validation
there is static (reading the diff, following the same recipes by hand). The gate that
matters is CI, which has network access the same way this document's commands assume.
