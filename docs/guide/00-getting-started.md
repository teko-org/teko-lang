# Getting started

Teko is a C#-like language — classes, interfaces, traits, generics, delegates,
compile-time dependency injection, reference counting — and it has **no compiler of its
own**. It is a set of hook modules that teach [`mc`](https://github.com/minicompiler/mc)
the constructs teko adds on top of `mc`'s base grammar. A build assembles that taught
compiler out of this repository, and the taught compiler then compiles your program into a
native executable. This page goes from nothing to a running binary, and then to the two
proofs this repository keeps: the fixtures and the fixed point.

## Install the pinned `mc`

The version is [`MC_VERSION`](../../MC_VERSION), one line, and it is the only one expected
to build this tree.

```sh
version=$(cat MC_VERSION)                          # 0.15.18
name="mc-$version-macos-arm64"                     # or linux-x86_64, linux-arm64, windows-x86_64, windows-arm64
base="https://github.com/minicompiler/mc/releases/download/v$version"
curl -fsSLO "$base/$name.tar.gz"
curl -fsSLO "$base/$name.tar.gz.sha256"
sha256sum -c "$name.tar.gz.sha256" 2>/dev/null || shasum -a 256 -c "$name.tar.gz.sha256"   # Linux, then macOS
tar xzf "$name.tar.gz"
mkdir -p ~/.local/bin && install -m 755 "$name/mc" ~/.local/bin/mc   # no root needed; put ~/.local/bin on PATH

mc --version                                       # must print the pinned version
mc --host                                          # the (os, arch) pair this binary is
```

On Windows the asset is a `.tar.gz` too; extract it and put `mc.exe` on `PATH`.

The release names the machine `x86_64` or `arm64`; `mc --host` calls the same machine
`x86_64` or `aarch64`. On macOS the binary is ad-hoc signed, so a download through a
browser needs `xattr -d com.apple.quarantine mc` once.

## Build the taught compiler

```sh
mc build . --config teko.toml
```

Two steps come out of that one command: `build/teko`, the taught compiler assembled from
`[compiler].modules`, and then `tests/hello.tk` compiled **by that binary** into
`build/teko-hello`. [`teko.toml`](../../teko.toml) targets linux/x86_64; on any other host,
derive a config instead of editing it — replace the pair with what `mc --host` printed:

```sh
sed -e 's/^os   = .*/os   = "macos"/' -e 's/^arch = .*/arch = "aarch64"/' \
    teko.toml >mc.host.toml
mc build . --config mc.host.toml
```

`teko.toml` is the build config; `mc.toml` beside it is the package manifest and carries
`[package]` only. The two roles cannot share one file: `mc pkg` reads `mc.toml` and takes
no `--config`. Every flag and every key is [the build reference](../reference/build.md).

## Your first program

Write `hello.tk`:

```teko
// expect-exit: 42
bool is_the_answer(i64 x) {
    return x == 42;
}

i64 main() {
    if (is_the_answer(42)) return 42;
    return 1;
}
```

`main` returns `i64` and that value **is** the process exit code — teko programs are
proved by their exit code throughout this guide and in every fixture. `bool` is the first
thing teko adds that `mc`'s core does not have, so a program using it already proves a
hook fired.

Compile it with the taught compiler, by deriving a config that names it as the entry:

```sh
sed -e 's#^entry = .*#entry = "hello.tk"#' -e 's#^out   = .*#out   = "build/hello"#' \
    mc.host.toml >mc.hello.toml
build/teko build . --config mc.hello.toml --entry-only
./build/hello; echo $?          # 42
```

`--entry-only` skips the `[compiler]` step: the binary already running **is** the taught
compiler, so it compiles the entry and nothing else.

## Run the fixtures

Every program in [`tests/`](https://github.com/teko-org/teko-lang/tree/main/tests) carries
a `// expect-exit: N` oracle, and
each one exercises one part of the surface. The loop that runs all 45 of them is in
[`CONTRIBUTING.md`](../../CONTRIBUTING.md); it is the same shape as the two commands
above, once per file.

## The fixed point

The taught compiler compiles **itself** — the whole compiler as one unit,
[`mc_teko.tk`](../../mc_teko.tk) — and the proof is a ladder:

```sh
sh scripts/bootstrap.sh
```

`teko0` (built by the stock `mc`) compiles the unit into `teko1`, `teko1` into `teko2`,
`teko2` into `teko3`. Three criteria: `teko2.o` and `teko3.o` byte-identical, the two
`--dump-asm` dumps equal, and `teko1` compiling and running all 45 fixtures. It prints
`FIXPOINT OK`, and it runs on five native pairs in CI.

## Where to go next

[10-values-and-types.md](10-values-and-types.md) starts the surface itself; the whole
route, one page at a time, is [the index](README.md), and the exhaustive side is
[the reference](../reference/README.md).
