# Compiler Architecture — the Pipeline

Teko compiles a **project** (a directory with a `*.tkp` manifest), never a lone file. A single
run walks a fixed pipeline; every stage below is a real, addressable namespace under `src/`
(`teko::lexer`, `teko::parser`, `teko::checker`, `teko::lir`, `teko::backend`, …), each with
its `.tks` implementation and sibling `_test.tkt` file(s).

```
source (.tks, per src/<namespace>/)
   │
   ▼
LEXER        teko::lexer      — byte-level scan → tokens (file:line:col), maximal-munch
   │
   ▼
PARSER       teko::parser     — tokens → AST; doc-comments (`/** … */`) preserved on the node
   │
   ▼
CHECKER      teko::checker    — name resolution, typing, the safety spine, const-eval,
   │                            monomorphization, diagnostics — the TAST (typed AST) comes out
   │
   ├── MONOMORPH   teko::checker::monomorph   — generics instantiated per concrete use-site
   ├── CONSTEVAL   teko::checker::consteval   — compile-time constant folding & evaluation
   │                                            (comptime_fold, consteval_order/_form)
   │
   ▼
LIR LOWERING  teko::lir        — TAST → a low-level, block-structured IR (LIR): virtual
   │                             registers, explicit block args (SSA-lite, no phi nodes),
   │                             architecture-agnostic
   │
   ▼
NATIVE BACKEND  teko::backend  — LIR → machine bytes, per target (see native-backend.md)
   │
   ▼
OBJECT + LINK   — a native object file (ELF/Mach-O/COFF), linked against the pre-built
                  `teko_rt` runtime object into the final binary
```

## Stage-by-stage map

### Lexer (`src/lexer/`)
Byte-level scanner. Maximal-munch tokenization (longest token always wins — `<<` never
`<`+`<`), a fixed keyword table (primitive type names are *not* reserved words — they are
injected predefined types resolved by the checker), string/char/number literal rules
(bases `0x`/`0b`/`0o`, `_` digit separators, `\u{HEX}` escapes). Comments are trivia and never
reach the AST; doc-comments (`/** … */`) are a distinct, structured token the parser attaches
to the following declaration.

### Parser (`src/parser/`)
Produces a full AST (`ast.tks`) — declarations, expressions, patterns, generics, attributes
(`#test`, `#os(...)`, …). Doc-comments are preserved on `Function`, `Field`, `TypeDecl`,
`ConstDecl` nodes, which is what makes the documentation generator, hover-on-symbol, and `fmt`'s
comment re-attachment all possible without a second pass over raw tokens.

### Checker (`src/checker/`, ~45 files)
The largest stage, and the one that owns most of the language's guarantees:

- **`resolve.tks`** — name resolution: absolute-path addressing, `use`-aliasing, builtin
  bare-call resolution (name **and** qualifier, never name alone).
- **`typer.tks`** / **`type.tks`** / **`tast.tks`** — the type checker proper; produces the
  TAST (`tk_tprogram`), the single artifact every backend (native or otherwise) consumes.
  Backend-agnostic by construction: nothing downstream re-reads source or an intermediate C tree.
- **`scope.tks`** — lexical scoping, the builtin table (bare-callable names: `print`,
  `println`, `write`, `ewrite`, `eprint`, `eprintln`, `panic`, `exit`, `args`).
- **`spine.tks`** / **`borrow.tks`** / **`escape.tks`** — the safety spine: a bounded,
  per-function points-to / borrowed-from / uniqueness lattice over a finite universe of cells
  (bindings + one-hop fields + `Reference` params). This is what makes stored borrows and
  manual `mem::free` sound without a user-facing borrow checker — see `memory-model.md`.
- **`monomorph.tks`** — generic instantiation, consumer-driven (a generic function/type is
  specialized once per concrete type argument set actually used).
- **`consteval.tks`**, **`comptime_fold.tks`**, **`consteval_order.tks`** — compile-time
  evaluation: constant folding over pure expressions, with a defined evaluation order so
  const-eval never has to guess dependency order between `const` declarations.
- **`initanalysis.tks`** — definite-assignment / use-before-init analysis (structural, always on).
- **`diagnostics.tks`** / **`warnings.tks`** — the single diagnostic surface every stage feeds:
  structured `{file, line, col, end_line, end_col, severity, message}` records, which is what
  the LSP server, `teko build`'s error output, and `teko lint` all read from one place.
- **`lsp_api.tks`** — the `pub` seam the LSP server calls into the checker through (in-process,
  same binary — no separate analysis server).

### LIR — Low-level IR (`src/lir/`)
`lower.tks` walks the TAST once and emits LIR: virtual-register operations over explicit
basic blocks with block arguments (simpler than full SSA — no phi nodes to reconcile).
Architecture-agnostic: nothing in this stage knows whether the target is x86_64 or arm64.
A native backend's compiled behavior is checked against the transpile-to-C path (the
C-vs-native differential-correctness comparison).

### Native backend (`src/backend/`)
Consumes LIR, produces real machine code. See `native-backend.md` for the full architecture
(instruction selection, register allocation, encoders, object writers, the target matrix, and
the linking model).

## Two coexisting emission paths, one TAST

Historically the compiler's only shipping backend lowered the TAST to **C text**, handed to
the host `cc`. That backend still exists as the cross-generation **fixpoint bridge** during
bootstrap (see `self-host-fixpoint.md`) and as a differential-correctness comparative, but it
is not the target production path: the own native backend is. Both consume the exact same
`tk_tprogram` — they are siblings over one typed tree, never one layered on the other.

## Where a symbol lives

Every symbol's canonical name is its full path from the project's root namespace
(`teko::lexer::Token`), and the source root (`src/`) is invisible in that address. Within one
namespace/directory, references are bare; crossing to any other namespace requires the
absolute path. This is why the module map above doubles as the addressing scheme: knowing a
symbol's file tells you its name.
