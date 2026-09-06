# The CLI and Editor Tooling

Every tool described here is a **subcommand of the single `teko` binary** — there is nothing
separate to install for formatting, documentation, linting, the REPL, or the language server.

## Command surface

```sh
teko build <projdir>            # front-end + test gate + native codegen → a binary
teko build <projdir> -o <dir>   # choose the output directory
teko build <projdir> --backend=native|c   # choose the codegen path (native is default)
teko build <projdir> --debug=lines        # emit `.tsym` + DWARF line/frame info for `tdb`
teko build <projdir> --coverage           # emit a Cobertura XML coverage report
teko build <projdir> --no-verify          # skip the test gate entirely (bootstrap escape hatch)

teko run <projdir>              # debug-profile build (-O0) + run immediately, in one step
teko test <projdir>             # run #test functions natively
teko test <projdir> --coverage  # …with a Cobertura report

teko fmt <path>...              # canonical formatter — rewrites in place
teko fmt --check <path>...      # CI gate: nonzero exit if anything is unformatted

teko doc <projdir>              # generate a documentation site (HTML + Markdown + JSON index)
teko doc <projdir> --serve      # serve it locally for preview
teko doc <projdir> --json       # just the machine-readable index

teko lint <path>...             # style/best-practice checks beyond type errors
teko lint <path>... --fix       # auto-apply the mechanically-fixable subset

teko lsp                        # start the language server (stdio JSON-RPC) — for editors, not humans
teko repl                       # an interactive read-eval-print session

teko init                       # scaffold a new project (teko.tkp + main.tks)
teko <projdir>                  # bare form ≡ `teko build <projdir>`
--version / -v                  # print the compiler's version
```

`teko` is **project-only**: it compiles a directory with a `*.tkp` manifest, never a bare `.tks`
file — a lone-file argument is rejected with an explicit message rather than guessed at.

## `teko build` — what actually happens

1. Dependencies resolve and load (see `packages.md`).
2. The whole program type-checks with those dependencies in scope.
3. `#test` functions run; coverage is measured against the manifest's floors.
4. Only if that gate passes does codegen run — native object emission (default) or the C
   fallback path (`--backend=c`), followed by linking.

`--no-verify` skips step 3 entirely; it exists for the compiler's own bootstrap chain, where a
generation is rebuilt from a source tree whose tests were already proven at an earlier step —
using it on an ordinary project trades the release gate for speed, deliberately and visibly.

## The formatter — `teko fmt`

Zero-option, canonical: there is exactly one formatting of any given program, the same way
`gofmt` settled the question for Go. Author-chosen line breaks are preserved (newlines are
statement separators, `fmt` never joins or splits a line on its own); comment placement is
reconciled from the raw token stream rather than lost, because comments are not AST nodes.
`teko fmt --check` is the CI-friendly form: a repository can require every merged change to
already be in canonical form.

## The documentation generator — `teko doc`

Walks every `pub`/`exp` item and its `/** … */` doc-comment into a documentation model, then
renders it as a static HTML site, Markdown, and a JSON index. Doc-comments follow a small,
rustdoc/godoc-style convention: a summary line, `@param`/`@returns` entries, and runnable
examples. The JSON index is the same one the LSP server's hover feature reads from, so an
item's documentation never diverges between "what `teko doc` publishes" and "what your editor
shows on hover."

## The linter — `teko lint`

Checks style and best-practice conventions the type checker doesn't enforce on its own — the
language's own law-encoded rules (never `match` on a `bool`, `loop`-only, prefer building a
slice with `[..xs, x]` over a manual push chain), unused imports, shadowing, and
unhandled-match-arm hints layered on top of the checker's own exhaustiveness proof. `--fix`
mechanizes whatever subset of findings is safely automatable.

## The REPL — `teko repl`

An interactive session: type an expression or declaration, see it evaluated immediately,
inspect values incrementally without a full build/link cycle. Good for exploring the standard
library or trying out a snippet before it goes into a project.

## The debugger — `tdb`

```sh
teko build --debug=lines .
tdb exec ./bin/myprogram
```

```
#0  teko::demo::add   at src/main.tks:42
#1  teko::demo::main  at src/main.tks:52
(tdb) break src/main.tks:42
(tdb) continue
(tdb) print x
(tdb) locals
(tdb) backtrace
```

`tdb` speaks Teko's own names and exact source positions natively — no demangling a C-shaped
symbol by hand. `--debug=lines` is Teko's explicit debug-info flag (there is no `-g`; Teko's
manifest has no place for a bare convention flag to hide in, and the spelling is deliberately
different from a C toolchain's `-g` since the underlying debug-info format isn't a drop-in
match for one). See the developer reference (`docs/canonical/dev/debugger.md`) for the
architecture behind it.

## Editor support

`teko lsp` is a real language server (stdio JSON-RPC) built into the compiler binary — it calls
the exact same lexer/parser/checker a build does, in-process, so a diagnostic your editor shows
is produced by the same logic `teko build` would use. It provides live diagnostics, formatting,
hover, go-to-definition, completion, and semantic tokens.

Supported editors, each via a thin client wired to `teko lsp` over stdio:

- **VS Code** — a full extension (syntax highlighting, the language server, and build/run/test
  task integration). Install from the marketplace or the extension's `.vsix`.
- **Vim / Neovim** — a plugin wiring the same server through the editor's native LSP client.
- **Emacs** — an Emacs Lisp package doing the same through `eglot`/`lsp-mode`.

Syntax coloring has two layers everywhere: a baseline TextMate grammar (works even without the
language server running) and richer LSP semantic tokens once the server is connected (colors
driven by real type information, e.g. distinguishing a resolved class instance from an
unresolved identifier — something a lexical grammar alone cannot do).

## The manifest, briefly

Every command above operates on a project described by `teko.tkp` — see `packages.md` for the
full format (dependencies, artifact kind, coverage floors, `[extern]` FFI linkage, and
publishing).
