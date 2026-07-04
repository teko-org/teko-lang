# TEKO — ROADMAP: developer tooling (`teko fmt` · `teko doc` · lint · repl)

> **Status:** DT0 ✅ DONE (issue #95) · DT1–DT3 DESIGN · **Created:** 2026-07-02 · **Branch:** `main`
>
> Native `teko` **subcommands** — written in Teko inside the compiler (`src/...`, SUPREME-RULE twin pair,
> same ruling as `teko lsp` [[teko-lsp-native-deferred]]) — that reuse the existing lexer/parser/AST.
> These are ecosystem-quality multipliers: a canonical formatter and a documentation generator are what
> make a language feel finished.
>
> Distinct from [`TEKO_ROADMAP_TOOLING.md`](TEKO_ROADMAP_TOOLING.md) (editor **plugins**: colors,
> intellisense, build integration) and the deferred native LSP. Same agent-distributable contract.

---

## 0. Why these belong in the compiler

The frontend already produces everything these tools need: the lexer emits tokens with `file:line:col`
(E1), the parser builds a full AST, and **doc-comments are already parsed** (`has_doc`/`doc`, `/** … */`
in `ast.tks`). Nothing consumes them yet. A formatter re-prints the AST; a doc generator walks it. Both
are pure-Teko passes over structures that already exist — no new runtime, VM-`.tkt`-testable.

---

## 1. Units

### ▪ DT0 — `teko fmt` (canonical formatter) — ✅ DONE (issue #95)
**Delivered:** `src/fmt/fmt.tks` (pure-Teko, no C twin — the `teko::regex` precedent; the C seed's `fmt`
dispatch arm is an honest stop, M.3) + `src/fmt/fmt_test.tkt` (37 tests) + minimal-append dispatch in
`main.c`/`main.tks`. Zero-option canonical (decision #1 → rec), comment re-attachment via a separate
raw-token pass (decision #2 → rec). gofmt-style: author line breaks preserved (newlines are statement
separators — fmt never joins/splits lines); `<`/`>`/`<<`/`>>` spacing preserves source adjacency (generic
vs comparison is ambiguous at token level). Safety: input must pass the real lexer+parser, and the output
is re-scanned and token-stream-compared — a mismatch refuses to write. **Proven:** idempotence over the
whole corpus (2nd pass changes 0 of 84 files) and semantic preservation (the fully formatted corpus passes
the entire 459-test gate and builds a compiler that judges the formatted tree a fixpoint). **Open:** the
corpus reformat commit + `fmt --check` CI gate (decision #5 below); stdin mode (no stdin host surface yet
— honest stop). Original unit spec follows.

**Deps:** parser/AST. **Files:** `src/fmt/*.tks` (namespace `teko::fmt`), CLI wiring in the driver.
A **canonical** pretty-printer: parse → format the AST/token stream → emit, so there is exactly one
formatting of any program (gofmt-style: no options, no debate). Handles the whole grammar (decls,
`match`/`loop`/`if`, `type … = struct/class/interface/variant/enum/flags`, generics, closures, the string
kinds, `~`/`in`, attributes `#test`/`#os`/…). **Comment preservation** is the hard part — comments
aren't AST nodes, so they must be re-attached from the token stream (leading/trailing/free-standing).
- `teko fmt <path>` (rewrite), `teko fmt --check` (CI gate: exit non-zero if unformatted), stdin/stdout.
**Verify:** **idempotence** (`fmt(fmt(x)) == fmt(x)`) as a `.tkt` over the whole corpus; the compiler's own
`src/**/*.tks` is a formatting fixpoint; formatting never changes behavior (re-typecheck equal).

### ▪ DT1 — `teko doc` (documentation generator)
**Deps:** parser/AST (doc-comments), encoding (JSON/HTML), optionally `teko::web` for a local server.
**Files:** `src/doc/*.tks` (namespace `teko::doc`). Extract `pub`/`exp` items + their `/** … */` doc,
signatures, types, and namespaces → a documentation model → **HTML** (static site) and **Markdown**;
a machine-readable **JSON** index (feeds editor hovers / search). Doc-comment mini-format: a summary line,
params, returns, examples (rustdoc/godoc-style). Cross-links by qualified name (`teko::net::tcp::connect`).
- `teko doc <project>` → `doc/` site; `teko doc --serve` (local preview via `teko::web`); `--json`.
**Verify:** `.tkt` — the doc model extracted from a fixture matches expected (items, visibility filter,
doc text); a snapshot of the generated Markdown.

### ▪ DT2 — `teko lint` (adjacent, T2)
**Deps:** checker. **Files:** `src/lint/*.tks`. Style/best-practice lints BEYOND type errors, reusing the
checker's structures: the law-encoded ones (no `match` on bool, prefer `x != 0` over `x to bool`, `loop`
only, `[..xs, x]` over push chains — the Phase-11 sweep targets), unused imports, shadowing hints,
unhandled-`match`-arm hints. `teko lint <path>`, `--fix` for the mechanical ones. **Verify:** `.tkt`
per-lint fixtures.

### ▪ DT3 — `teko repl` (adjacent, T3)
**Deps:** VM (`teko run`). **Files:** `src/repl/*.tks`. An interactive read-eval-print loop on the VM
(which already tree-walks the checked program): evaluate expressions/decls incrementally, inspect values.
The VM makes this feasible without codegen. **Verify:** native session smoke (scripted input → expected
output).

## 2. Dependency graph + tiers

```
lexer/parser/AST ─┬─ DT0 fmt   (comment re-attachment is the hard part)
                  ├─ DT1 doc   (doc-comments already parsed) ── --serve via teko::web
                  └─ DT2 lint  (reuses checker) ── --fix
VM ─────────────── DT3 repl
```

**Tiers.** **T1:** DT0 `fmt` (highest ecosystem ROI, plus a CI `--check` gate), DT1 `doc`.
**T2:** DT2 `lint`. **T3:** DT3 `repl`.

All are native `teko` subcommands (SUPREME-RULE twins), pure-Teko passes over existing structures — no new
runtime, no keystone dependency. Can start immediately in parallel with the library roadmaps.

## 3. Open decisions (ratify with the sibling roadmaps in PR #80)

1. **`fmt` canonicality:** zero-option canonical (rec — gofmt model, ends bikeshedding) vs a small config.
2. **Comment attachment model** for `fmt`: attach comments to adjacent AST nodes during parse (cleaner,
   touches the parser) vs a separate token-stream reconciliation pass (rec — keeps the AST clean).
3. **`doc` output targets:** HTML + Markdown + JSON (rec) — confirm the set; is the JSON index the same one
   the future LSP hover consumes?
4. **Subcommand vs external tool:** all four are in-compiler native subcommands (rec, per the `teko lsp`
   ruling) vs separate binaries.
5. **`fmt --check` as a CI gate** on `main` once the corpus is a fixpoint — confirm we add it.
