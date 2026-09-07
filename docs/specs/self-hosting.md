# Self-hosting

**What is built, and what the criterion is.** The mechanics of the script are
[the bootstrap internals](../internals/bootstrap.md); the operator's view is
[the build reference](../reference/build.md) § The fixed point. This page is the claim
itself: what "teko compiles itself" is taken to mean here, and why it is measured the way it
is.

## One unit

The compiler compiles itself as a **single translation unit**,
[`mc_teko.tk`](../../mc_teko.tk). The file is five `#include` lines and nothing else: mc's
platform layer, mc's minimal core, this project's `main()`, the thirty-one teko modules and
the project's `user_init()`.

There is no separate compilation, no object archive and no link order to get right. That is
a deliberate simplification, not a limitation being tolerated: the thing being proved is
that the **language** reproduces its own compiler, and a build graph in between would be one
more thing that could explain a difference.

The file is named `.tk`, so teko's vocabulary applies inside it, while the sources it
includes are not claimed and keep mc's — which is what lets mc's own files go on using
`type`, `out` and `params` as parameter names. Having no identifier of its own, the unit
cannot collide with a taught word.

## The criterion is the object

| compared | verdict |
|---|---|
| `teko2.o` against `teko3.o` | must be **byte-identical** |
| `--dump-asm` of teko2 against teko3 | must differ nowhere |
| the 45 fixtures, compiled and run by teko1 | each one's `// expect-exit: N` |

`teko1.o` against `teko2.o` is deliberately **not** the criterion. Those two objects come
from two different code generators — the stock `mc`'s and teko1's own — and requiring them
to agree would be requiring teko to reproduce mc's output, which is a different claim and
not the interesting one.

**The object, not the executable.** An executable carries what the linker put around the
code: a loader path, a signature on macOS, section padding, a build identifier. None of that
is the compiler's output, and all of it can differ between two runs that produced the same
compilation. The object is exactly what the compiler wrote.

**The dump, next to the object.** A `cmp` says two files differ and nothing more. The
`--dump-asm` diff is the same criterion in a form that can be read, and it is what localizes
a divergence when one appears.

**The fixtures, because a fixed point is not enough.** A compiler that refused every program
would reproduce itself trivially. Criterion 3 is what makes the first two mean something: the
compiler that reached the fixed point also compiles 45 programs and each exits with the
number its header claims.

## The five pairs

linux/x86_64, linux/aarch64, macos/aarch64, windows/x86_64, windows/aarch64 — each on a
runner of that operating system **and** that architecture.

Reproducing on three and asserting it on five would be exactly the cross-compiled claim this
setup exists to refuse. The ladder **runs** every stage it builds, so a target that is not
the machine running it is refused rather than produced unproven, and `mc --host` is asserted
against the leg before anything is built.

## What would change the claim

**A second unit.** If the compiler ever stopped fitting in one translation unit, the
criterion would have to become "the set of objects reproduces", with a defined order, and
the determinism rules would have to cover the archive as well as each object.

**Teko's own surface in the modules.** The modules are written in mc's surface today. If
they were written in teko's, the unit would stop being compilable by a stock `mc` — and it
is also the package's `check` unit ([the internal debts](../internals/debts.md)). The fixed
point itself would still close; the packaging would not.

**A pinned object.** Reproducibility **across** runs and machines is a stronger claim than
the fixed point, and it is measured and reported today without being enforced. Pinning it
would add a fourth criterion, and would have to come with the rule for updating the pin when
the compiler legitimately changes.
