# The bootstrap ladder

[`scripts/bootstrap.sh`](../../scripts/bootstrap.sh) builds four compilers and compares two
objects. What the ladder proves and how to run it is
[the build reference](../reference/build.md) § The fixed point; this page is how the script
gets there.

## Four stages

| stage | built by | entry | output |
|---|---|---|---|
| teko0 | the stock `mc`, `--compiler-only` | `[compiler].modules` | `build/teko` |
| teko1 | teko0, `--entry-only` | `mc_teko.tk` | `build/teko1` and `build/teko1.o` |
| teko2 | teko1 | `mc_teko.tk` | `build/teko2` and `build/teko2.o` |
| teko3 | teko2 | `mc_teko.tk` | `build/teko3` and `build/teko3.o` |

teko0 is the only stage the stock `mc` writes. teko1 is the first compiler teko itself
produced; teko2 is the first one produced by a compiler teko produced. `teko1.o` against
`teko2.o` is **not** the criterion — they come from two different code generators — which is
the same reason `mc`'s own ladder does not compare its first two.

## The three criteria

1. `cmp build/teko2.o build/teko3.o` — byte-identical **objects**.
2. the `--dump-asm` of teko2 and of teko3, over the same source, differ nowhere.
3. teko1 compiles and runs all 51 fixtures, each one's `// expect-exit: N` being the oracle.

All three, then `FIXPOINT OK`.

## Why the object, and why a `[linker]` is always present

`mc` writes `<out>.o` and hands it to a linker **only when a `[linker]` block is there**.
With no linker, the built-in executable backend writes the binary directly and leaves no
object at all — and then there is nothing to `cmp`. So every config the script derives
carries a `[linker]`: `teko.toml`'s own `cc` on Linux and macOS, or the block
`--linker-toml FILE` names.

That option exists for Windows, where `cc` is not a linker: there is no C runtime and no
direct-executable backend, so the link is `lld-link` against a sysroot of three files. The
option **replaces** `teko.toml`'s `[linker]`; everywhere else it is not passed and
`teko.toml`'s own block stands.

## The derived configs

`teko.toml` is never edited. Each stage gets a config derived from it by `sed`, with only
`[target] os`/`arch` and `[project] entry`/`out` changed, written **next to `teko.toml`**
and removed on exit.

Next to it, and not in a scratch directory, because every path a config names is resolved
against the **config's own directory**: a config under `/tmp` cannot find `mc_teko.tk`, and
that directory is also the project root the `internal` guard measures from. The path passed
to `--config` stays relative and without a leading `./` for the same reason.

## The host guard

The ladder **runs** every stage it builds, so a target that is not this machine is not a
fixed point — it is a cross build with nothing to execute. `--os`/`--arch` are checked
against `mc --host` and a mismatch is refused up front.

The trap behind the guard: the taught compiler's `.exe` suffix comes from the **host**, not
from `[target]`, because that binary has to run on the machine that wrote it, while
`[project].out` is literal and the object is `out + ".o"`. So `--os windows` on a macOS box
would write `build/teko` while the script looked for `build/teko.exe`. Refusing the
mismatch is the cure; guessing suffixes is not.

On Windows every stage really is `<name>.exe`, so the objects the criterion compares are
`build/teko2.exe.o` and `build/teko3.exe.o` — COFF, and the same `cmp`.

## The glibc tail

`mc`'s ELF writer defaults the program interpreter and the libc soname to **musl**, and the
taught compiler is written by the host's own executable backend. On a glibc machine the
result exists, has a size, and does not run: `build/teko: not found`, exit 127 — the loader
speaking, not a missing file.

So on Linux the script detects the loader **this** machine has and appends it to `[target]`
of every derived config:

```toml
interp = "/lib64/ld-linux-x86-64.so.2"
libc   = "gnu"
```

On a musl machine nothing is appended and the defaults stand. `scripts/check-docs.sh` does
the same thing for the same reason, and the CI legs carry it in their matrix.

## Provenance

Printed, **not** gated: the `mc` version, and the sha256 of teko0, `teko1.o`, `teko2.o` and
`teko3.o`, plus whether `teko1.o == teko2.o` (it does when the ladder was already at the
fixed point on its first turn).

The four hashes are what makes a future divergence attributable without rerunning anything.
teko0 is the only stage the stock `mc` writes, so an **identical** teko0 with a different
`teko1.o` is a nondeterminism in the compiler, while a **different** teko0 is a different
input — another `mc`, another tree, or a stale build.

Whether `teko2.o` is byte-identical **across** runs and machines is a separate claim from
the fixed point, and nothing is pinned to it here ([debts.md](debts.md)).

## What CI adds

The five `fixpoint` legs run this same script, one per native pair, and then do two things
the script does not:

- write the sizes and hashes into a table in the job summary, from a file the release notes
  quote as well — one derivation, two readers;
- when `teko1.o != teko2.o`, archive the `--dump-asm` of teko0 and teko1 and their diff. An
  object cannot say **what** moved; the readable form can.

[ci.md](ci.md) is the rest.
