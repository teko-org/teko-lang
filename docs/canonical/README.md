# Teko — Canonical Documentation

This directory is the **canonical, target-state reference** for Teko: the language, the
compiler, and its tooling described **as if the 0.3 backlog were complete** — the coherent
end-state that the project's design documents (`docs/design/`, `TEKO_CONSTITUTION.md`,
`TEKO_LEGISLATION.md`, the `TEKO_ROADMAP_*.md` family) already commit to, written as finished
and stable rather than as work in progress.

## This is NOT the current build status

Teko today is pre-release beta, under fast-moving construction. Some of what is described
here — most visibly the LSP editor client, the own linker, `teko doc`/`teko lint`/`teko repl`,
and full parity of the native backend across every OS — is designed but not yet (fully) built.
Where the target diverges from what `main` currently ships, that is a deliberate editorial
choice of this reference, not an error: **describe the destination, not the trailhead.**

For the actual, moment-by-moment state of the project, read (in this order):

- `TEKO_MASTER_PLAN.md` — the live, ordered execution sequence (start here for real status)
- `docs/design/*.md` — architecture/decision proposals, some settled, some still open
- `docs/memory/*.md` — dated session notes: what a given lane measured, decided, or found
- `DECISION_LOG.md` / `TEKO_HISTORY.md` — the historical record of how a ruling was reached

Those trees are **not altered by this canonical set** — they remain the honest, in-progress
history. This canon is the map of where that history is walking to.

## Two audiences, two trees

- **`dev/`** — for compiler contributors: pipeline architecture, the memory model, the native
  backend, testing/coverage/the run journal, the self-hosting fixpoint, FFI, the debugger, the
  LSP server, and how to contribute.
- **`product/`** — for people writing Teko programs: what Teko is, a language guide, the CLI and
  editor tooling, package management, and a getting-started walkthrough.

## How to read a claim in this canon

Every non-obvious claim here is traceable to a source: a ratified law in
`TEKO_CONSTITUTION.md`/`TEKO_LEGISLATION.md`, a `docs/design/*.md` proposal, or a
`docs/memory/*.md` finding. Where the target is genuinely open (the design itself leaves a
question unanswered, or two sources disagree), this canon says so explicitly instead of
picking silently — see each file's "Open / needs a ruling" note where present.

See also: `docs/canonical/README.draft.md` — a proposed as-if-done rewrite of the repository's
top-level `README.md`, staged here for review before it replaces the real one.
