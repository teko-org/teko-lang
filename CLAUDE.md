# Working instructions — teko-lang

## What this repository is

**Teko is a language taught to [`mc`](https://github.com/minicompiler/mc)**, not a
compiler of its own. The whole language lives at the root as hook modules — `teko.tk`
and 30 `teko_*.tk` modules (31 module files in all) plus `lib/rt.tk`, driven by
`core_teko.mc` and `user.mc` — and `mc` is **pinned by `MC_VERSION`**: download exactly
that release, no other one runs. `mc.toml` is the package manifest (`[package]` only);
`teko.toml` is the build config (`mc build . --config teko.toml`).

## Language (hard rule)

The owner reads Portuguese only. **Chat with him is PT-BR, always.** **Everything that
lands in this repository is English** — documentation, code comments, commit messages, PR
bodies, workflow comments; `sh scripts/check-docs.sh` fails on Portuguese in tracked
sources. **Never use a quiz or a menu of options (the AskUserQuestion tool): ask in short
prose.** When the owner is answering something else, wait — do not stack questions.

## Fork protocol

Before halting on a design question, be sure it is not already decided:

1. Search `DECISION_LOG.md`, then `docs/specs/` and `docs/reference/`.
2. **The newest ruling wins** — a later entry supersedes an earlier one on the same point.
3. **Halt only on a genuinely open fork**, stating it short and clear. Default otherwise:
   follow C#, or the market when C# has no form, and record the choice in the log.

## Process in force

- **One agent at a time**, on its own branch and its own worktree — never the main
  checkout. Branch prefixes: `ngen/**`, `feat/**`, `fix/**`, `docs/**`, `verify/**`, `chore/**`,
  `ci/**` — the `All Green` ruleset excludes them, so an agent can push to them directly.
  Commit and push per commit; never run `git config user.*`.
- **Scout → implementer → independent verifier → PR.** The scout checks the task against
  the current tree (are the citations right? is it already landed? are the dependencies
  satisfied?); the implementer runs only if the scout says it is needed, carrying the
  scout's findings; the verifier re-proves the result without trusting the implementer's
  word.
- **A PR squash-merges into `main` with CI green** — the five `ngen` legs, the five
  `fixpoint` legs, `docs`, and the aggregator `mc build ngen && run` — and with **every
  Copilot review finding resolved** before the merge.
- A dispatch that started wrong is killed and re-dispatched clean, never patched in
  flight.

## Code laws

- **Zero changes to mc's core.** A defect on mc's side is reported to `minicompiler/mc`
  with a minimal, pure-mc reproducer; it is never worked around here.
- **Zero new intrinsics.** Every function has surface code; `mc limits` is the budget a
  construct has to fit in. A construct that "wants" backend magic is a fork.
- **Every fixture carries `// expect-exit: N`.** No oracle, no fixture.
- **Refusals say `teko: <short cause>`** — compiler style, no prose, no references.
- **`--dump-ast` is identical when a change does not change accepted code.** That is the
  proof a refactor is a no-op.
- **No workarounds:** find the root cause.

## Local recipe

```sh
sed -e 's/^os   = .*/os   = "macos"/' -e 's/^arch = .*/arch = "aarch64"/' \
    teko.toml >mc.macos.toml
mc build . --config mc.macos.toml
for src in tests/*.tk; do
  n=$(basename "$src" .tk); w=$(grep -m1 '// expect-exit:' "$src" | sed 's/.*expect-exit: *//')
  sed -e "s#^entry = .*#entry = \"tests/$n.tk\"#" -e "s#^out   = .*#out   = \"build/$n\"#" \
      mc.macos.toml >"mc.$n.toml"
  ./build/teko build . --config "mc.$n.toml" --entry-only && "./build/$n"
  echo "$n exit=$?  want=$w"; rm -f "mc.$n.toml"
done
sh scripts/bootstrap.sh --os macos --arch aarch64   # prints FIXPOINT OK
sh scripts/check-docs.sh
```

`MC_VERSION` is raised only after that whole recipe is green locally on the new mc.

## Where things live

`docs/README.md` is the map: `guide/` (task-oriented), `reference/` (by lookup), `specs/`
(designed, not built), `internals/` (how the port is built), `history/` (a pointer to the
private `teko-org/teko-history`). `DECISION_LOG.md` holds the decisions in force. The
channel with the mc session is outside this repository — decisions that affect mc, and
questions only mc can answer, go through the mc project's own notices file.

## Versioning

`vX.Y.Z`, mc's own three-part format. The next release is **v0.4.0**; **v1.0.0 ships only
together with mc 1.0.0**.
