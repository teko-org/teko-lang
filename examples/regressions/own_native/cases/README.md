# `examples/regressions/own_native/cases/` — the cross-target case files

`own_native.tkp` declares `source = "src"`, so `cases/` is outside this project's build and invisible
to it. The scenarios of `own_native.tkr` that name a file here with `Given source` — instead of running off
the project build — do so for one of two reasons. A CROSS-TARGET row is the ONLY row of its target, so
a project build for a target nothing else uses buys nothing and a one-line program is the honest
minimum for "the emitter produces a well-formed object for that target". A TRAP row (the cast-width
guards, the slice-store bounds guard, the slice/str-READ bounds guard) aborts the process, which would
take every later assertion of the channel's one program with it.

The files carry **no leading comment block**, and that is load-bearing, not an oversight: the runner
synthesizes a scratch project from a case file by splitting it into DECLARATIONS and STATEMENTS
(`split_snippet_decls_and_stmts`), and a file that declares nothing is written to `main.tks`
verbatim — where a leading `/** … */` with no declaration after it is a parse error (C7.9). Each
case's documentation therefore lives beside its `Scenario` in `own_native.tkr`, next to the
`Given source` line that names it.
