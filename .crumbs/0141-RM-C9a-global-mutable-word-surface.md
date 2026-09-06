---
seq: 0141
crumb-id: RM-C9a
milestone: M3
gate: "[fixpoint]"
reseed-class: "teaching"
deps: []
sources:
  - "docs/design/arena-terminal-zero-libc-0.3.1.md:40-90"
  - "docs/design/plano-s16-arena-mmap.md:110-135"
  - "DECISION_LOG.md:662-666"
---

# 0141 · RM-C9a — `global var` mutable-word surface (teach the `.bss`/thread-local process word)

> Teach the one language surface RM-C9 needs: a namespace-level mutable word, so the arena's CONTROL
> anchor can live in Teko instead of the C `_Thread_local uint64_t tk_g_arena_control`.

## Goal

Teko has NO mutable module/process word: `parse_decl.tks:1388` REJECTS `global` on a variable, and the
`global` keyword today only marks a namespace-level fn/type/const (visibility) lowering to read-only
RODATA (`lir_print.tks:281`). This crumb ADDS the writable form `global var name: T = <const>` at
namespace level, plus a `#thread_local` attribute selecting per-flow-of-control storage. C leg emits
`static <cty>` (process-global) or `_Thread_local static <cty>` (thread-local) — a C storage
qualifier, NOT libc and NOT `from "teko_rt"`, links freestanding. Native leg lowers to `.bss`/`.tbss`,
write-only (D52 honest-stop today). This is TEACHING (ensino-now, use-later): the surface lands even
though the two arena USES arrive in 0142/0143 and the process-global heavy use (wrap-table) is FASE-2.
Feature-gated-inert for the corpus until 0142 uses it → the teaching reseed shifts temp-var IDs but
changes no corpus behavior.

## Where

- `src/lexer/lexer.tks:271` — `global` token — already tokenized; no change (verify only).
- `src/parser/parse_decl.tks:1388` — the `"`global` is not allowed on a variable"` rejection — REPLACE
  with an ACCEPT arm at namespace level: parse `global var name: T = <const-init>` into a new
  `GlobalVarDecl` AST node; keep the rejection for `global var` inside a fn body / on a type member.
- `src/parser/parse_decl.tks` — the `#`-attribute parse (where `#os`/`#arch`/`#test` are read) —
  accept `#thread_local` as a boolean attribute on a `GlobalVarDecl`.
- `src/parser/ast.tks` (or the AST module) — NEW `GlobalVarDecl { name; ty; init; thread_local; line;
  col; namespace }`.
- `src/checker/collect.tks` + `src/checker/check_modules.tks` — collect `GlobalVarDecl` into the
  namespace symbol table (like `const`), enforce: namespace-level only, `init` is a compile-time
  constant of type `T`, `T` is a word-sized scalar (`u64`/`i64`/`ptr`/`bool`/integer). Duplicate-name
  diagnostic mirrors the existing `const` path.
- `src/checker/typer.tks` — resolve a reference to a `global var` (read yields `T`; assignment `name =
  v` is allowed and type-checks `v: T`).
- `src/codegen/codegen.tks` — emit the C definition (`_Thread_local`-qualified when `thread_local`) at
  file scope, and reads/writes as plain load/store of the mangled symbol.
- `src/lir/lower.tks` — native lowering: a `.bss` (or `.tbss` when `thread_local`) symbol + address
  load/store; write-only, terminal `_ =>` honest-stop for the run side (D52), mirroring `word_ptr`.
- `src/lir/lir_print.tks:281` — extend the `global` printer to distinguish writable `.bss`/`.tbss`
  from RODATA.

## How

1. **Parser:** at `parse_decl.tks:1388`, when the decl is namespace-level, ACCEPT `global var`:

```teko
type GlobalVarDecl = type {
    name: str
    ty: parser::TypeRef
    init: parser::Expr
    thread_local: bool
    namespace: str
    line: u32
    col: u32
}
```

   Keep the existing rejection ONLY for `global var` inside a fn body or on a type member (the
   "`global` is not allowed on a variable" message survives for those two positions — a real
   parser-guaranteed reject, kept per "não barrar o que não existe" only where the surface truly
   cannot appear).

2. **Attribute:** parse a leading `#thread_local` into `thread_local = true`; absence → process-global.

3. **Checker:** collect like `const`; reject non-namespace-level, non-constant `init`, and non-word
   `T` with `arquivo:linha:coluna: "…"` compiler-style messages. Reference read → `T`; assignment →
   type-check RHS as `T`.

4. **Codegen (C leg):** emit at file scope, once per decl:
   - process-global: `static <cty> <mangled> = <init>;`
   - thread-local: `static _Thread_local <cty> <mangled> = <init>;`
   A read emits `<mangled>`; a write emits `<mangled> = <v>` as a statement. No `tk_*`, no
   `from "teko_rt"` — the compiler EMITS the storage, exactly as it emits every other definition.

5. **Native (`lower.tks`):** allocate a `.bss`/`.tbss` symbol; lower reads/writes to address
   load/store. The run side is a D52 honest-stop (`_ =>` "global var not yet lowered (N)"), write-only
   like `word_ptr`/`ptr_word` — compiled into the self-image, not executed on the native leg yet.

6. **Reseed:** teaching-class; the corpus does not yet declare a `global var` (0142 is first user), so
   emit is byte-inert modulo temp-ID shift — `gen2==gen3`.

## Rulings & laws

- **Teko-only:** `.tks` only; the C leg EMITTING `_Thread_local` is codegen output, not a maintained-C
  edit — no `teko_rt.c` change, no `from "teko_rt"` (D90/D125).
- **§16 se existe em C, existe em Teko:** the `_Thread_local uint64_t` word exists in C → its faithful
  Teko form is this thread-local `global var` (`plano-s16-expurgo-libc-completo.md` R-series).
- **Fork protocol:** this teaches FORK-ABERTO-1 option **B** (`DECISION_LOG.md:665`), selected
  law-first by D119/D125 (more-recent) over the `plano-s16-arena-mmap.md:110-135` deferral. The owner
  should formally close the fork as B; the terminal law already forces it. NOT a new fork.
- **W15 full Javadoc** on any `exp` decl; the new AST/checker/codegen fns are `pub`/private → no doc.
- **Removals = clean expurgo, NO tombstone:** the fn-body/type-member `global var` reject STAYS (real
  guarantee), only the namespace-level reject is lifted.
- **Safety:** NEVER `teko test .`; build in a subshell `ulimit -v 4718592`; commit each green step;
  reseed ONLY at this gate; `gen2==gen3` byte-identical; sweep `.tkt`/`.tkr` after the AST/signature
  change.

## Fixtures

| fixture | asserts | expected |
|---|---|---|
| `global_var_body_rejected` | `global var` inside a fn body still rejects | `EXPECT_COMPILE_FAIL` |
| `global_var_member_rejected` | `global` on a type member still rejects | `EXPECT_COMPILE_FAIL` |

The accept-and-USE path is exercised by the self-build once 0142 declares the first `global var`; no
affirmative fixture (the fixpoint drives it). Only the two reject paths (which the self-build never
drives) get oracles.

## Gate

`[fixpoint]` — build gen2 + the two reject oracles + `gen2==gen3` byte-identity. "Green" = the surface
parses/checks/emits, the two rejects fire, and the corpus emit is byte-inert (teaching reseed).
Reseed-class: `teaching`.

## Deps

— (buildable today; all it touches are landed).

## Done when

`global var name: T = <const>` (and `#thread_local global var`) parses, checks, and emits a
`static`/`_Thread_local static` C definition with zero `from "teko_rt"`, the two reject oracles pass,
and `gen2==gen3`.
