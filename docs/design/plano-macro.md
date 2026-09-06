# Macro facility — design proposal set (the SEALED two-family model)

Status: **DESIGN — PROPOSAL SET for owner deliberation.** Read-only on product code;
this document writes no `.tks`, triggers no reseed, runs no build, and — per the task
constraint — **`teko test` was NOT run in any form** (the `monomorph` leak crashes the
container). Author: architect. Branch base: `origin/fix/retirement`.

> **What is SEALED (do not reopen).** `docs/design/mudancas-superficie-0.3.1.md` §14/§14.1/§14.2/§14.3
> + §15 (HEAD `17c0e7da`) seals the shape: Teko gains macros in **two keyword-identified
> families, both `@`-called, split by pipeline STAGE**, and the stage FIXES the arg model —
> there is **no single-construct hybrid** and **no third class** (the prior `plano-macro`
> 1A/1B/1C hybrid is SUPERSEDED and gone; "runtime macro" is a contradiction — see below):
>
> - **Family A — syntactic** *(keyword `macro`, CLOSED)* — expands on the **AST BEFORE
>   type-check** (`parse → AST → [expand] → TYPE-CHECK of the result → lower`). Args = **raw
>   AST only** (`.node`/`.source`/`.len`); types do NOT exist yet, so **no `.type`/`.value()`
>   in the body**. The body is **comptime logic that runs BEFORE enlarging** and decides which
>   **`lowering { … }`** blocks to emit (or none); inside `lowering { }` everything is
>   **verbatim** EXCEPT **`${expr}`**, which injects a macro-computed node. **`macro` has NO
>   return type — it ALWAYS emits code (substitutes itself and ENLARGES the AST via
>   `lowering`), always.** The "computes a value" case is **NOT `macro`; it is `comptime`**
>   (§14.2): counting args is `comptime count(...args): usize { args.len }`, **not** a `macro`.
>   Rust/Lisp model. The splice is **`lowering`/`${}`** (NOT `quote`); hygiene is
>   **stable-mangle, never an error** (only the `lowering` verbatim is mangled; a
>   `${}`-injected node is the USER's, kept INTACT — `${}` is the SOLE bridge into the verbatim).
> - **Family B — comptime evaluation** *(keyword `comptime`, CLOSED)* — runs **AFTER
>   type-check** (`parse → AST → TYPE-CHECK → [comptime eval] → lower`). Args = **typed
>   evaluable values**; computes an inlined **value**. Its return (when present) is **inlined
>   as a LITERAL** at the call site (`var x = @sizeof<i32>()` ends up as `var x = 4`). Zig
>   comptime model. **Comptime args must be comptime-KNOWN:** to EVALUATE the VALUE of a
>   runtime var is an **error**; to INFER the TYPE of a runtime var is **OK** (via generics —
>   a generic `comptime` reads a runtime arg's type without evaluating it).
> - **`@sizeof`/`@typename` are comptime MACROS over type reflection** (`T.size`/`T.name`),
>   **never hidden builtins**; `sizeof` is **TWO constructs**: `exp global comptime sizeof<T>():
>   usize` (slot size, explicit `T`, no value param, `@sizeof<i32>()`) and a **runtime**
>   `exp fn sizeof<T>(t: T | null = null): u64` (bytes occupied, bare `sizeof(t)`). `@` marks
>   the compiler-executed call; a bare call is the runtime function (§14.3, §B.2).
> - **NO third macro class.** Anything needing a runtime VALUE is a **real function/method** in
>   the stdlib, not a macro. Field-driven codegen over `.fields` (the retired structural `Eq`
>   heir) is a **function/method + trait impl** (§9.4), **NOT a macro** — the "`.fields`
>   tension" dissolves (§A.0).
> - **FFI = `extern fn` → libc ONLY. There is NO `extern macro` and NO `extern comptime`
>   (REJECTED, §14.3/§16).** Resolving C header macros/constants would require reading C
>   headers = a C preprocessor/toolchain (gcc/cc/clang) dependency — exactly what the project
>   is REMOVING. libc constants are **hand-declared Teko consts, per-platform behind
>   `#os`/`#arch`** in a curated `teko::sys` (§7). The native `macro`/`comptime` families are
>   unaffected — only the `extern` variant is rejected.
> - The keyword names `macro`/`comptime` are **CLOSED** (owner ruling, §14.3) — not
>   provisional; only their spelling was ever the open point, and it is now sealed.

> **Output-shape contract (owner's standing feedback).** Every open sub-design below is
> presented as **3+ complete, fully-specified alternatives**, and **each alternative carries a
> concrete Teko code example — the declaration PLUS its call site PLUS what it achieves**,
> followed by **my recommendation**. Sealed points are stated as decisions with 3+ examples,
> not as options. There are **zero bare open questions**: a genuine judgment call is framed as
> N concrete, runnable options with a recommendation; the only owner-pending item is the
> **type-reflection extent** (§B.2), framed as staged options, never as a question.

---

## 0. Current-state map (file:line — what each proposal reuses)

**Lexer / tokens.**
- `Hash` token (`#` attribute marker) — `src/lexer/token.tks:133`. There is **no `At`/`@`
  operator token today.**
- `@` is recognized ONLY as a verbatim/raw **string** prefix (`@"…"`, `$@"…"`) —
  `src/lexer/lexer.tks:429-453` (the `has_verbatim` body scanner; `@` = "NO escape
  processing"). A bare `@` **not** followed by `"` falls through to `read_symbol` →
  "unexpected character". **`@name` is an unclaimed lexical slot** — free for the macro call
  marker.
- Keyword classifier `keyword_kind` — `src/lexer/lexer.tks:331-372`; `extern` at `:354`,
  `const` at `:338`. Neither `macro` nor `comptime` is reserved today (`git grep` finds
  none). Reserving them is additive. Contextual-keyword precedent noted at `:322`/`:370`
  (`params`/`from` are position-significant, not lexer-reserved).

**Parser / AST (Family A operates on THIS tree — pre-type-check).**
- `Expr = struct { kind: ExprKind; line: u32; col: u32 }` — `src/parser/ast.tks:293`;
  `ExprKind = variant Number | Var | Call | … | Block` — `:292`. Every node already
  carries `line`/`col` (C1-POS) — the stable key for hygiene stable-mangle (§A.3, §5.4) and for
  `.source` reconstruction (§A.1).
- `Call = struct { callee: Path; args: []Expr; arg_names: []str; owner_type_args;
  callee_type_args }` — `:199`. The template for a new `MacroCall` node.
- `Function = struct { name; type_params; …; is_extern; c_symbol; from_lib; os_guard; … }`
  — `:519`, with `is_extern`/`c_symbol`/`from_lib` at `:533-535` and `os_guard` at `:536`.
  The `extern fn` split stays the template for **libc `extern fn`** (§16) — NOT for any
  `extern macro`/`extern comptime` (those are rejected, §7).
- `Param = struct { …; is_params: bool; has_default; default_expr }` — `:511` (C#-style
  variadic modifier, **trailing-only**, `type_ann` must be a Slice). The variadic precedent
  for `...args` (§5.2).
- `Decl = variant Function | TypeDecl | ConstDecl` — `:807`; `Item`/`Program` —
  `:824-826` (the flat item list the checker consumes — Family A rewrites `Program` in
  place).
- `extern fn` parsing — `src/parser/parse_decl.tks:401-489`. **No `macro`/`comptime`/`@`
  parsing exists yet.**

**Checker / comptime (Family B operates AFTER this — post-type-check).**
- **General comptime evaluator already mandated (reach-C)** — `docs/design/comptime-fold-design.md`
  (owner ruling 2026-07-19). Engine `eval_const(e: TExpr, table: TypeTable, env: Env, agg:
  AggConstMap): ConstValue | error` — `src/checker/comptime_fold.tks:306`;
  `predicate_folds_const(e: TExpr): bool` — `:339`; the `ConstValue` domain
  (`CVInt|CVFloat|CVBool|CVBytes|CVAgg`) — `:18-36`; the literal reconstructor
  `literal_of(v: ConstValue, ty: Type, line, col): TExpr | null` — `:1997`. **This is
  Family B's engine verbatim** (its return is inlined as a literal via `literal_of`).
- Module-const inlining `inline_consts(prog): TProgram | error` — `src/checker/consteval.tks:531`,
  wired at `src/build/project.tks:367` — **after** `monomorph` (`:353-354`), **before**
  lowering. **This is the exact pipeline slot for Family B's expand pass.**
- `Env = struct { … }` — `src/checker/scope.tks:72` (the binding env `eval_const` consumes).
- TAST: `TExpr` — `src/checker/tast.tks:10`; `TCall` — `:67`; `TExprKind` variant — `:148`;
  `TItem = variant TFunction | TypeDecl | UseDecl | TStatement | TConstDecl` — `:290`;
  `TProgram` — `:291`. Family B rewrites `TProgram` in place.

**Conditional-compilation / program-transform precedent.**
- `#os("…")` prunes non-matching function variants at `src/build/project.tks:116-126`, inside
  `frontend_check` (`:464`). This is the closest existing precedent for a **compile-time
  program-transform pass** — the shape both families' expand passes copy (visit items, rewrite
  the item list, keep it deterministic). It is ALSO where the hand-declared, `#os`/`#arch`-guarded
  libc constants live (§7) — those are ordinary `const` items, no macro involved.

**Fixpoint / seed precedent.**
- `src/checker/lower_const.tks` derives every rodata symbol as a **pure function of stable
  keys** (`const_leaf_symbol`, `const_rodata_symbol`), never a global counter, so generations
  are byte-identical. Any macro-introduced name MUST follow the same rule (§5.4).
- Seed discipline: module-level `const` (#594) landed, THEN a seed bump (`DECISION_LOG.md`
  D40/D41), and only after may the corpus use it. Macros inherit this sequence (§5.5).

---

## 1. The sealed model, restated as the design frame

The two families are **not two flavours of one construct** — they are two passes at two
stages, and the stage decides everything downstream. The whole document is organized under
that split:

| axis | **Family A — `macro`** (syntactic) | **Family B — `comptime`** (evaluation) |
|---|---|---|
| mechanism | **copy-in-place** — the body is pasted at the use site, ENLARGING the AST (not a call, not dispatch) | **evaluate** — the body is folded to one value, inlined as a literal |
| stage | **before** type-check (on `parser::Program`) | **after** type-check (on `checker::TProgram`) |
| args are | **raw, ANONYMOUS AST** — `.node`/`.source`/`.len`, un-typed until materialized | **typed evaluable values** — a `ConstValue`; must be comptime-KNOWN |
| variadics | **free** — anonymous args have no type, so arity is unconstrained (§A.0) | trailing typed pack (`is_params` precedent) |
| body may use | comptime logic + node reflection + `lowering`/`${}` | `eval_const` arithmetic over typed values; type reflection (`T.size`/`T.name`) |
| body may NOT use | `.type`, `.value()` (no types yet) | `.node`/`.source` (AST is gone; it's a value); the runtime VALUE of a runtime var (type-inference is OK) |
| produces | **code** via `lowering` — **ALWAYS** (a `macro` has NO return type; it never yields a value — the value case is Family B) | **a value**, inlined as a literal (`literal_of`) |
| needs | **stable-mangle** hygiene + `lowering`/`${}` splice (§A.2, §A.3) | nothing new — reuses `eval_const`/`literal_of` + type-table reflection |
| engine | new `expand_macros_syntactic` pre-pass (§5.1) | `expand_comptime` at `project.tks:367` (§B.1) |
| owner example | `@unless(done, { retry() })` emits `if !(done) { retry() }` | `@sum(2,3,5)` folds to `10`; `@count(a, b)` folds to `2` (arity, no eval) |

Both are invoked `@name(...)`. **The `@` marks a compiler-executed call** (`macro` OR
`comptime`); a **bare call (no `@`) is an ordinary RUNTIME function** (§14.3). The `@` prefix
plus the **declared family keyword** is what lets the compiler know, at the call site's
resolution, WHICH pass owns the expansion — the dev *says* the family at declaration, the
compiler *knows* the stage.

The sub-designs below split into **Family A detail** (§A), **Family B detail** (§B), the
**cross-cutting mechanics** (`@` placement, variadics, errors, fixpoint/seed — §5), then the
**FFI note** (§7 — extern-macro/extern-comptime REJECTED), fixtures (§8), crumbs (§9), risks
(§10).

---

# §A — FAMILY A (`macro`, syntactic) detail

## A.0 — the DEFINING mechanism: copy-in-place, enlarge the AST (the spine)

Per the seal (`mudancas-superficie-0.3.1.md` §14.1, HEAD `17c0e7da`), a Family-A macro is
defined by ONE mechanism, and every sub-design below serves it:

> **A Family-A macro COPIES ITSELF into the use site and ENLARGES the AST.** It is **inline
> expansion** — the body is *copied* at the call position, the args *substituted* in, and the
> AST *grows* there. It is **NOT a call** (no frame, no return) and **NOT dispatch** (no
> vtable, no runtime selection). This copy-in-place is exactly what distinguishes a macro from
> a function or a trait. **A `macro` has NO return type and NEVER yields a value — it ALWAYS
> emits code.**

Four consequences fix the whole section:

1. **The body is comptime logic that runs BEFORE enlarging.** The expander does not *evaluate*
   the body as a runtime call — it runs the body's **comptime logic**, which decides which
   **`lowering { … }`** blocks to emit (or none — conditional/assembled expansion), then
   *pastes* the emitted fragment at the call position. So the surfaces the body needs are (a)
   how it *reads* the pasted-in args (§A.1 — the `Node` reflection), (b) how it *writes* the
   fragment being pasted (§A.2 — the sealed **`lowering`/`${}`** splice), and (c) how the paste
   avoids clobbering the site it grows into (§A.3 — stable-mangle hygiene). The pipeline slot
   (§5.1-A) runs the paste **before type-check**, so the enlarged AST is type-checked as if the
   dev had written it by hand — a badly-typed expansion fails the normal type-check, pointing
   back at the site.

```teko
/**
 * log_if — Family A, the mechanism in one macro: the body is COMPTIME LOGIC that runs before
 * enlarging and decides whether to emit a `lowering` block at all. `@log_if(msg)` COPIES a
 * `println` into the use site; `@log_if()` emits NOTHING. No frame, no dispatch; the caller's
 * AST grows only when the comptime `if` fires. A `macro` ALWAYS emits code — it never returns.
 *
 * @param args  the pack of anonymous call-site nodes the comptime logic inspects
 * @since 0.4-macros
 */
macro log_if(...args) {
    if args.len > 0 {                          // comptime logic — runs BEFORE enlarging
        lowering { teko::io::println(${args[0]}) }   // verbatim, ${} injects the arg node
    }                                          // if false → nothing is pasted
}
// call site:  @log_if(msg)  →  teko::io::println(msg)   ;   @log_if()  →  (nothing)
```

2. **No explicit types ⇒ free arity (variadics are a consequence, not a bolt-on).** A
   Family-A arg carries **no declared type** — it is an **anonymous node**, un-typed until it
   is materialized into the AST and the whole enlarged tree is type-checked. Because nothing
   about an arg is typed at paste time, the macro does not care how MANY args there are:
   **untypedness is what sustains variadics.** `...args` is not a special variadic feature —
   it is the natural shape of "a pack of anonymous nodes" (see §5.2, framed as this
   consequence).

```teko
/**
 * emit_each — Family A, free arity as a consequence of untypedness: it accepts ANY number of
 * anonymous nodes and EMITS one `println` per node. Nothing about an arg is typed at paste
 * time, so the macro never constrains arity — `...args` is just "the pack". A `macro` emits;
 * it does not count-and-return (COUNTING the pack to a VALUE is Family B — see below).
 *
 * @param args  a pack of anonymous, un-typed call-site nodes
 * @since 0.4-macros
 */
macro emit_each(...args) {
    var acc = lowering { }
    for a in args { acc = lowering { ${acc}; teko::io::println(${a}) } }
    acc                                        // the assembled `lowering` fragment
}
// call site:  @emit_each(x, y, z)  →  println(x); println(y); println(z)
// counting instead of emitting is NOT this macro — it is  comptime count(...args): usize { args.len }  (§B)
```

3. **A named Family-A macro ≈ a `structural trait`, as an encapsulating HELPER — but ONLY for
   patterns a function cannot express.** It is the **explicit, visible, no-shadow heir** of the
   retired structural trait (§9.4 of the surface doc): the structural trait *encapsulated a
   structural pattern and pasted a synthesized body the dev never saw*; a named macro
   encapsulates a structural pattern too, but the body is **written by hand, visible, and
   copied in-place at the use site** — no compiler shadow. The honest test: a macro earns its
   keep when it must **inject syntax into the caller's own control flow** (something a function
   fundamentally cannot do), e.g. a guard that `return`s from the CALLER.

```teko
/**
 * guard — Family A as the honest structural-trait heir: it ENCAPSULATES the early-return
 * "guard clause" pattern and COPIES it into the caller — injecting a `return` into the
 * CALLER's control flow, which a function cannot do. Visible, hand-written, no shadow. A
 * `macro` emits; it has no return type.
 *
 * @param cond  the condition node; when false the guard returns
 * @param ret   the value node to return from the CALLER when the guard fires
 * @since 0.4-macros
 */
macro guard(cond, ret) {
    lowering { if !(${cond}) { return ${ret} } }
}
// call site:  fn f(x): i32 { @guard(x > 0, -1); x * 2 }
//   → fn f(x): i32 { if !(x > 0) { return -1 }; x * 2 }   (the return lands in f, not in guard)
```

> **The `.fields` tension DISSOLVES (§14.2).** Field-by-field EQUALITY — the retired
> structural `Eq` — computes a runtime VALUE from a struct's fields, so it is **NOT a macro**:
> it is a **function/method** (or an interface + operator impl, §9.4). There is **no third
> macro class** and **no typed-macro-for-field-codegen**: anything that reads a runtime value
> is a real function/method in the stdlib. A `macro` may paste field ACCESS syntax the author
> names explicitly, but iterating a type's `.fields` to synthesize a runtime comparison lives
> in a function, never a macro.

4. **A `macro` ALWAYS emits, NEVER returns — the compute-a-value case is `comptime` (Family
   B).** Sealed (§14.1/§14.2): `macro` has **no return type**; it substitutes itself and
   enlarges the AST via `lowering`, always. Even the source-capturing `stringify` (§A.1) does
   not "return a string" — it **EMITS a string-literal node** via `lowering { ${x.source} }`.
   Counting args, summing constants, sizing a type — every VALUE-producing job is **Family B
   `comptime`**, whose return is inlined as a literal (`comptime count(...args): usize {
   args.len }`, `@sum`, `@sizeof<T>()`). The keyword — `macro` vs `comptime` — *is* the
   emit-code / compute-value switch; there is no return-type discriminator inside a macro.

The three open sub-designs — the **AST reflection surface** the body reads (§A.1), the
**splice mechanism** the body writes with (§A.2, sealed to `lowering`/`${}`), and **hygiene**
for the paste (§A.3, sealed to stable-mangle) — are the three faces of this one copy-in-place
mechanism.

## A.1 — AST reflection surface (`Node`): how much of the AST to expose

The body inspects `parser::Expr` subtrees. The `Node` handle reuses the compiler's own AST
(`src/parser/ast.tks:293`); the tension is M.0 (small language) — every field exposed is
permanent surface. Three tiers, smallest-first.

### A.1-Tier-1 — opaque handle: `.len`, `.source`, indexing only — *RECOMMENDED as the seed floor*

`Node` is opaque. The only operations: a node pack has `.len`; `pack[i]` yields a `Node`;
a `Node` has `.source` (the verbatim source slice it spans, from `line`/`col` + the source
map) and `.len` (child arity). **No `.kind`, no field accessors.** This is enough for the two
canonical Family-A jobs — **pack-length inspection for conditional emit** (`log_if`,
`emit_each`) and **source capture** (`stringify`) — and exposes zero AST internals. (Counting a
pack to a VALUE is Family B, not this surface.)

```teko
/**
 * stringify — Family A: EMITS the verbatim source text of its single argument as a string
 * literal. It does NOT "return" — a `macro` has no return type; it lowers a string-literal
 * NODE into the AST via `${x.source}`. `.source` is the exact bytes the caller wrote — no
 * evaluation, no type.
 *
 * @param x  the argument node whose source text is captured
 * @since 0.4-macros
 */
macro stringify(x) { lowering { ${x.source} } }
// call site:  @stringify(a + b * 2)  →  expands to the string literal  "a + b * 2"

/**
 * dbg_len — Family A: uses `.len` on the anonymous pack to decide, at comptime, HOW MANY
 * `print` nodes to emit — reading arity WITHOUT evaluating any arg. Arity is structural,
 * available pre-type-check, and never runs the args.
 *
 * @param args  the verbatim call-site argument nodes (raw AST, unevaluated)
 * @since 0.4-macros
 */
macro dbg_len(...args) {
    if args.len == 0 { lowering { teko::io::println("<none>") } }
    else { lowering { teko::io::println(${args[0]}) } }
}
// call site:  @dbg_len(a, b)  →  println(a)   ;   @dbg_len()  →  println("<none>")
```

- **Unlocks:** `@stringify`, `@log_if`/`@dbg_len` (`.len`-driven emit), and (with §A.2's
  `lowering` splice) any macro that only needs to re-emit a node verbatim — e.g. a `@dbg(x)`
  that emits `print("x = " ++ @stringify(x))` then the value. **Cost:** minimal, honours M.0.
  **Limit:** a macro cannot branch on node shape (can't ask "is this a call?").

### A.1-Tier-2 — tagged handle: add `.kind` + typed child accessors

Add a read-only `.kind` (a `NodeKind` enum mirroring `ExprKind`, `ast.tks:292`) and
shape-specific accessors that return `Node | null` (e.g. `.callee`, `.arg(i)`, `.lhs`,
`.rhs`). The body can now pattern-match on structure.

```teko
/**
 * assert_eq — Family A: given a call-site expression the author expects to be `lhs == rhs`,
 * EMITS a runtime check that, on failure, prints BOTH operands' source text. Uses `.kind` to
 * verify the argument really is an equality and `.lhs`/`.rhs` to split it. A `macro` emits; it
 * has no return type.
 *
 * @param cond  the condition node; must be a `Binary` `==` or the macro emits its own error
 * @throws      a compile-time `@error` if `cond` is not an `==` comparison
 * @since 0.4-macros
 */
macro assert_eq(cond) {
    if cond.kind != NodeKind::Binary { @error("assert_eq expects `a == b`") }
    lowering {
        if !(${cond.lhs} == ${cond.rhs}) {
            panic(${cond.lhs.source} ++ " != " ++ ${cond.rhs.source})
        }
    }
}
// call site:  @assert_eq(total == expected)
//   → on failure prints:  "total != expected"   (the SOURCE, not the values)
```

- **Unlocks:** structure-directed macros (`assert_eq`, a `@matches(x, Pattern)` sugar, a
  `@pipe(a, f, g)` left-fold). **Cost:** `NodeKind` + ~6 accessors become permanent surface —
  a real M.0 increase, but bounded and read-only.

### A.1-Tier-3 — full builder: `.kind` + mutation + a `Node`-construction API

Add constructors (`Node::call(callee, args)`, `Node::binary(op, l, r)`, …) so a macro can
BUILD arbitrary AST programmatically instead of only via `lowering`. This is the Lisp
"AST-as-data" maximum.

```teko
/**
 * tuple_swap — Family A: given `(a, b)` node-pack, EMITS a two-statement block that swaps
 * them via a fresh temp, constructing every node through the builder API rather than quoting.
 * A `macro` emits; it has no return type.
 *
 * @param args  exactly two lvalue nodes to swap
 * @since 0.4-macros
 */
macro tuple_swap(...args) {
    var tmp = Node::fresh_local()                    // stable-mangled temp (§A.3)
    Node::block([
        Node::bind(tmp, args[0]),                    // var <tmp> = a
        Node::assign(args[0], args[1]),              // a = b
        Node::assign(args[1], tmp)                    // b = <tmp>
    ])
}
// call site:  @tuple_swap(x, y)  →  var _m = x; x = y; y = _m
```

- **Unlocks:** everything (arbitrary codegen). **Cost:** the LARGEST surface — a second way
  to build every node kind, doubling the AST's public API and fighting M.0 hardest.

**Recommendation: ship Tier-1 in the seed, add Tier-2 as a gated follow-on, defer Tier-3.**
Tier-1 satisfies `@stringify` and the `.len`-driven emit macros with near-zero surface. Tier-2
(`kind` + read accessors) is the first thing real macros want (`assert_eq`), and read-only
accessors are honest surface; land it as its own crumb once demand is shown. Tier-3's
builder is redundant with `lowering` (§A.2) for almost all cases and pays the M.0 cost twice —
defer until a concrete macro cannot be written with `lowering` + Tier-2.

## A.2 — the splice mechanism: `lowering { … }` + `${expr}` — **SEALED**

**SEALED (§14.1, HEAD `17c0e7da`): the splice is `lowering { … }` + `${expr}`, NOT `quote`.**
`lowering { … }` **lowers code into the AST** (the name = "lowers code into the AST", chosen
over `quote`). Inside `lowering { }` **everything is verbatim** — copied whole into the AST —
**EXCEPT `${expr}`**, the one escape, which **injects the node/value the macro computed**. A
`macro` (which ALWAYS emits code, §A.0) assembles one or more `lowering { … }` blocks; the
comptime logic around them decides which to emit (or none). This is the Rust `quote!`/Lisp
quasiquote role, renamed and pinned: the fragment reads like the code it produces.

```teko
/**
 * unless — Family A: emits an `if` whose condition is the NEGATION of the injected call-site
 * argument. `${cond}` injects the caller's verbatim condition node so it resolves in the
 * CALLER's scope; the `if`/`!` scaffolding is verbatim `lowering` structure. A `macro` ALWAYS
 * EMITS CODE via `lowering`.
 *
 * @param cond  the condition node, injected verbatim into the negation
 * @param body  the block node, injected as the `if` body
 * @since 0.4-macros
 */
macro unless(cond, body) {
    lowering { if !(${cond}) { ${body} } }
}
// call site:  @unless(done, { retry() })  →  if !(done) { retry() }

/**
 * log_expr — Family A: a LAZY-ARG macro — the argument `x` is injected into a branch that only
 * runs when logging is on, so a costly `x` is never evaluated when disabled. This is the reason
 * args are raw nodes, not values: laziness is free. `${x.source}` injects the arg's source text
 * as a string literal; `${x}` injects the arg node itself. A `macro` ALWAYS EMITS CODE.
 *
 * @param x  the expression node, injected UNEVALUATED into the guarded branch
 * @since 0.4-macros
 */
macro log_expr(x) {
    lowering { if log_on() { print(${x.source} ++ " = " ++ str(${x})) } }
}
// call site:  @log_expr(expensive())  →  expensive() runs ONLY when log_on() is true
```

- **Cost:** a `lowering` sub-parser + the `${}` injection grammar (a distinguished region the
  parser captures as a verbatim fragment template with `${}` holes). It keeps produced code
  READABLE (M.3 — you can read what the macro emits). **The `lowering` verbatim is mangled for
  hygiene (§A.3); a `${}`-injected node is the USER's and stays INTACT.**

**Fallback still available (NOT the primary): the `Node` builder API.** For a shape `lowering`
cannot express, the body may build the fragment with the A.1-Tier-3 builder calls
(`Node::if_(...)`, `Node::call(...)`) — same power, but the produced code is opaque at the
definition site (a tree of constructor calls, no readable `if`), so it is the escape hatch, not
the surface. A string-mixin path (emit a `str` of source the compiler re-parses, Zig/D style)
is **rejected** — stringly-typed, unhygienic (names are strings, no stable mangle) — recorded
only as the emergency-cheap bootstrap if `lowering` slips.

## A.3 — hygiene: **SEALED — stable mangle, never an error**

**SEALED (§14.1, HEAD `17c0e7da`): hygiene is a STABLE MANGLE, and a collision is NEVER an
error.** Family A pastes code into the caller's scope; a `var t` the macro introduces in a
`lowering` verbatim block would otherwise collide with the caller's `t`. The sealed answer is
one rule, no fork:

1. **A binding introduced by the macro's `lowering` verbatim is MANGLED to a unique name**
   (`t` → `t$…`), so it can never collide with the user's `t`. Collision does not diagnose — it
   is silently, deterministically disambiguated.
2. **The mangle is STABLE / deterministic** — key = **macro + call-site + name + expansion
   index** (`t$__<macro>_<line>_<col>_<idx>`), **never random**. A random mangle would make two
   compilations of the same source diverge and **break the byte-identical fixpoint**; the stable
   key mirrors `lower_const`'s symbol determinism (§5.4).
3. **ONLY the `lowering` verbatim is mangled** — it is the macro's own text. **A `${}`-injected
   node is the USER's and stays INTACT** — it references the caller's scope, so mangling it
   would capture the wrong binding. This asymmetry (verbatim mangled, `${}` intact) **is** the
   hygiene, and it is **airtight**: **`${}` is the SOLE bridge** from the macro's comptime scope
   (outside `lowering`) into the verbatim — nothing from outside appears inside except via the
   escape. So the mangle scope is **exactly** the verbatim: what is there is the macro's, what
   entered via `${}` is the user's, and the outside comptime logic **never becomes AST**.

```teko
/**
 * swap — Family A, HYGIENIC by stable mangle: the introduced temp `t` is a `lowering` verbatim
 * binding, so it is mangled to a stable unique name — a caller `t` is NEVER captured and NEVER
 * an error. The `${a}`/`${b}` injected args are the USER's nodes and stay INTACT. A `macro`
 * ALWAYS EMITS CODE.
 *
 * @param a  first lvalue node, injected verbatim (caller scope, intact)
 * @param b  second lvalue node, injected verbatim (caller scope, intact)
 * @since 0.4-macros
 */
macro swap(a, b) {
    lowering { var t = ${a}; ${a} = ${b}; ${b} = t }   // `t` (verbatim) → t$__swap_<line>_<col>_0
}
// call site with a caller `t`:
//   var t = 99
//   @swap(x, t)  →  var t$__swap_12_4_0 = x; x = t; t = t$__swap_12_4_0   ← correct, no error
```

- **Medium cost**, reuses the existing name-mangling infra; upholds M.3 (no silent capture)
  and stays fixpoint-safe via the stable key. **Colored (fully-scoped) hygiene was considered
  and rejected** — it needs resolver surgery to resolve macro-definition-scope references, and
  stable-mangle already covers the capture-of-introduced-binding case that is >90% of real need;
  the owner sealed stable-mangle.
- Because **Family B produces a VALUE, it owes NO hygiene** — hygiene is a Family-A-only
  obligation, another reason the stage split is clean.

---

# §B — FAMILY B (`comptime`, evaluation) detail

Family B runs after type-check; args are typed values; it computes a value **inlined as a
literal**. It is the Zig `comptime`. Its engine already exists — `eval_const` +
`literal_of` (`comptime_fold.tks:306`, `:1997`). Two rules and one open sub-design frame it:

**The comptime-KNOWN rule (§14.2, the fine ruling).** A `comptime` runs post-type-check, so it
**evaluates VALUES** — and a value is only evaluable if it is **comptime-const**. **Evaluating
the VALUE of a runtime var is an error.** But the **TYPE** of any arg is comptime-known (even a
runtime one), so a **generic** `comptime` may **INFER the type** of a runtime arg **without
evaluating it** — that is OK. This is what separates `@sum(a, b)` (a,b runtime → error, it
evaluates values) from `@sizeof(x)` (x runtime → OK, it only reads x's type).

```teko
/**
 * count — Family B, the sealed canonical VALUE macro: counts the arity of its argument pack at
 * comptime, inlined as a literal. `.len` is a STRUCTURAL fact (how many args) — comptime-known
 * even when the args are runtime, because it reads arity, NOT any arg's value. So `@count(a,
 * b)` folds to `2` and NEVER evaluates a or b. (This is why counting is `comptime`, not
 * `macro`, §14.1.)
 *
 * @param args  the call-site arguments (arity read structurally; values never evaluated)
 * @return      the argument count, inlined as a literal
 * @since 0.4-macros
 */
comptime count(...args): usize { args.len }
// call site:  @count(f(), g())  →  the literal  2   (f/g NEVER run — arity only)

/**
 * elem_size — Family B, the comptime-KNOWN rule in action: INFERS the TYPE `T` of a possibly
 * RUNTIME argument (OK — type is comptime-known) and folds to the slot size, WITHOUT ever
 * evaluating the argument's value. Contrast `@sum` below, which DOES evaluate values.
 *
 * @param x  a value whose TYPE is inferred at comptime; its value is never evaluated
 * @return   the slot size of `x`'s type, inlined as a literal
 * @since 0.4-macros
 */
exp global comptime elem_size<T>(x: T): usize { @sizeof<T>() }
// call site:  var x = read_runtime();  const S = @elem_size(x)  →  the slot size (x's value untouched)
```

## B.1 — comptime integration: how typed args + value inlining wire in

Three placements for the `comptime` expansion pass.

### B.1-Opt-1 — a pre-lower pass reusing `eval_const`, slotted at `project.tks:367` — *RECOMMENDED*

A new pass `expand_comptime(prog: TProgram): TProgram | error` runs **immediately before**
`inline_consts` at `src/build/project.tks:367` — after `monomorph` (`:353-354`), so types are
fully resolved. It: (1) finds each `@name(...)` `comptime` call in the TAST; (2) binds the
macro's params to the callers' argument `ConstValue`s via `eval_const` over an `Env`
(`scope.tks:72`) — a non-const arg where a value is needed is a call-site error; (3) evaluates
the body with `eval_const`; (4) replaces the call `TExpr` with `literal_of(result)`. Downstream
(`inline_consts`, lowering) is UNTOUCHED — comptime macros lower to ordinary literals.

```teko
/**
 * sum — Family B: folds a variadic pack of COMPILE-TIME integer values into their sum at
 * comptime, inlined as one literal. Each arg is a typed evaluable value (post-type-check) and
 * its VALUE is evaluated; a non-const arg is a call-site error (the comptime-KNOWN rule).
 *
 * @param args  the call-site arguments, each required to be a compile-time constant VALUE
 * @return      the comptime sum, inlined as a literal at the call site
 * @throws      a call-site diagnostic if any argument's value is not compile-time-constant
 * @since 0.4-macros
 */
comptime sum(...args): usize {
    var total = 0 to usize
    for a in args { total = total + a }             // `a` is a typed VALUE, not a node
    total
}
// call site:  @sum(2, 3, 5)  →  eval_const folds body → literal_of → the literal  10
//   @sum(a, b) on runtime a,b  →  ERROR: argument is not a compile-time constant (it evaluates VALUES)

/**
 * crc_table — Family B: builds a 256-entry CRC-32 lookup table at comptime, inlined as a
 * constant array. The loop runs entirely in `eval_const`; the emitted program contains only
 * the finished table (zero runtime init). The canonical "expensive table at comptime" case.
 *
 * @return  a 256-element `u32` array, inlined as a constant aggregate (CVAgg)
 * @since 0.4-macros
 */
comptime crc_table(): [256]u32 {
    var t = [0 to u32; 256]
    var i = 0 to usize
    loop while i < 256 {
        var c = i to u32
        var k = 0
        loop while k < 8 { c = if (c & 1) != 0 { 0xEDB88320 ^ (c >> 1) } else { c >> 1 }; k = k + 1 }
        t[i] = c; i = i + 1
    }
    t
}
// call site:  const CRC = @crc_table()  →  the 256-entry table baked into rodata
```

- **Clean layering, maximal reuse** — `eval_const` + `literal_of` + `Env` are the whole
  engine; no typer surgery. The `CVAgg` domain (`comptime_fold.tks`) already carries the
  aggregate the table needs.

### B.1-Opt-2 — typer-lazy (fold on demand inside the typer)

The typer expands a `comptime` call the moment it types the surrounding expression, so the
macro's result type participates in inference immediately.

```teko
/**
 * bit_width — Family B, typer-lazy rationale: a macro whose RESULT drives the type of the
 * surrounding declaration, so it must fold DURING type-check, not after.
 *
 * @param n  a compile-time count
 * @return   the number of bits needed to hold `n`
 * @since 0.4-macros
 */
comptime bit_width(n: usize): usize {
    var w = 0 to usize; var v = n
    loop while v > 0 { w = w + 1; v = v >> 1 }
    w
}
// call site:  var flags: u@bit_width(300) = 0   ← the macro result names a TYPE width
//   → needs the fold to complete mid-typecheck (Opt-1's post-pass is too late here)
```

- **Cost:** couples expansion into `typer.tks` with careful ordering (a comptime call may
  reference another). Necessary ONLY if a comptime result must feed type inference (the
  `u@bit_width(300)` case above). Heavier; reserve for when that capability is actually
  requested.

### B.1-Opt-3 — two-phase (collect, then a fixpoint expand loop)

A first phase collects every `comptime` call and its dependency edges; a second phase expands
in dependency order to a fixpoint, so a comptime macro may call another comptime macro whose
result it consumes.

```teko
/**
 * pow2 — Family B, two-phase rationale: a comptime macro that CALLS another comptime macro;
 * the expander must fold `pow2` before the caller that uses `@pow2(k)` can fold.
 *
 * @param k  a compile-time exponent
 * @return   2 raised to `k`, at comptime
 * @since 0.4-macros
 */
comptime pow2(k: usize): usize { 1 to usize << k }
comptime mask(k: usize): usize { @pow2(k) - 1 }      // comptime calling comptime
// call site:  const M = @mask(8)  →  expander folds pow2(8)=256 first, then mask → 255
```

- **Cost:** a dependency-ordered fixpoint loop with cycle detection (bounded depth, §5.4).
  Most general; needed once comptime-calls-comptime is common. Opt-1 already handles a single
  level (the inner call is just another `@` the same pass visits) — Opt-3 is the escalation
  when nesting/ordering gets real.

**Recommendation: B.1-Opt-1 (pre-lower pass at `project.tks:367`) as the base, escalate to
Opt-3 (two-phase fixpoint) when comptime-calls-comptime lands; Opt-2 (typer-lazy) only if a
comptime result must name a type.** Opt-1 is one isolated pass reusing the entire mandated
comptime engine — lowest risk, satisfies `@count`/`@sum`/`@crc_table` immediately.

## B.2 — `@sizeof` / `@typename`: comptime MACROS over type reflection — **SEALED shape, staged extent**

**SEALED (§14.2): `@sizeof`/`@typename` are comptime MACROS over type reflection
(`T.size`/`T.name`) — NEVER hidden builtins.** They are visible, no-shadow `comptime`
declarations in the stdlib, `global` (unqualified access, §15), and reflection computes **only
comptime VALUES** (the slot size of `T`, the name of `T`, later counting fields / summing
sizes) — it **never touches a runtime field value**. And **`sizeof` is TWO constructs** (the
correction): the **comptime** one gives the **slot** size of `T` (explicit `T`, no value
param); the version that takes a **value** is a **runtime function** (bytes occupied). `@`
marks the comptime one; a bare call is the runtime function (§14.3).

```teko
/**
 * sizeof (comptime) — Family B: the SLOT size of a type, computed at comptime over type
 * reflection (`T.size`), inlined as a literal. Explicit `T`, NO value param. `global` (§15)
 * so it is reached unqualified; `exp` so it enters the `.tkh`. This is a visible stdlib
 * declaration — NOT a hidden builtin.
 *
 * @return  the slot size (in bytes) of `T`, inlined as a literal
 * @since 0.4-macros
 * @see the runtime `sizeof` function below (bytes occupied by a VALUE)
 */
exp global comptime sizeof<T>(): usize { T.size }
// call site:  var x = @sizeof<i32>()   →   var x = 4     (comptime, slot; @ = compiler-executed)

/**
 * sizeof (runtime) — a plain RUNTIME function (NOT a macro): the bytes OCCUPIED by a value.
 * Takes a value, called bare (no `@`). This is the "needs a runtime value ⇒ real function"
 * rule (§14.2): the value case is never a macro.
 *
 * @param t  the value whose occupied byte-size is measured (null ⇒ 0)
 * @return   the number of bytes occupied by `t`
 * @since 0.4-macros
 */
exp fn sizeof<T>(t: T | null = null): u64 { if t == null { return 0 } /* bytes occupied by t */ }
// call site:  sizeof(x)   →   runtime — the bytes occupied by the value x   (bare, no @)

/**
 * typename — Family B: the NAME of a type as a string literal, over type reflection (`T.name`),
 * inlined at comptime. Same shape as `@sizeof<T>()`: visible, `global`, no-shadow.
 *
 * @return  the type's name, inlined as a string literal
 * @since 0.4-macros
 */
exp global comptime typename<T>(): str { T.name }
// call site:  const N = @typename<i32>()   →   const N = "i32"
```

- **Cost:** a `type`-as-comptime-reflection surface (`T.size`, `T.name`) into `comptime`
  bodies. **REJECTED alternatives (recorded, do not reopen):** (a) *compiler builtin
  intrinsics* — making `@sizeof`/`@typename` reserved names the pass resolves invisibly — is
  **rejected by the seal**: the owner ruled they are visible macros over reflection, not hidden
  builtins (no-shadow, the same principle that retired the structural trait). (b) *a plain
  generic `size_of<T>()` with no `@`* — rejected: it fragments the story (`@` must mark every
  compiler-executed call, §14.3).

**The one owner-pending sub-decision: the type-reflection EXTENT.** How much of `Type` to
expose into `comptime` bodies — staged, smallest-first, each its own crumb. This is explicitly
owner-pending; presented as options, not a bare question:

- **Extent-1 — RECOMMENDED seed floor: `T.size` + `T.name` only.** Exactly what `@sizeof` /
  `@typename` need; zero excess surface. Ships with Family B.
- **Extent-2 — add `T.align` (+ any scalar reflection).** A small, bounded step for alignment
  math (`@alignof<T>()`); land as a gated crumb when a macro needs it.
- **Extent-3 — add `T.fields` (structural field reflection).** The maximum. **Guarded by the
  seal:** `.fields` may only feed **comptime VALUES** (count fields, sum sizes) — it may
  **never** synthesize a runtime read of a field VALUE (that is a function/method, §14.2/§9.4).
  Defer until a concrete comptime job (e.g. `@field_count<T>()`) demands it; land as its own
  crumb.

```teko
/**
 * field_count — Family B, Extent-3 illustration (owner-pending): counts a struct's fields at
 * comptime via `T.fields.len` — a comptime VALUE (arity), never a runtime field read. Shown to
 * pin the guardrail: reflection may count/measure fields, but not evaluate their values.
 *
 * @return  the number of fields of `T`, inlined as a literal
 * @since 0.4-macros
 */
exp global comptime field_count<T>(): usize { T.fields.len }   // ONLY if Extent-3 is ratified
// call site:  const K = @field_count<Point>()   →   const K = 2   (comptime count, no field VALUE read)
```

---

# §5 — CROSS-CUTTING MECHANICS (both families)

## 5.1 — the `@` pre-pass placement (per family, per stage)

The sealed model puts the two families at two stages, so there are **two passes**:

- **5.1-A — Family A pre-pass (before type-check).** `expand_macros_syntactic(prog:
  parser::Program): parser::Program | error` runs in `frontend_check`
  (`src/build/project.tks:464`) **immediately after `#os` pruning** (`:116-126`) and
  **before** `type_program_with_deps_pre_mono`. It rewrites `@macro`-calls in the untyped
  AST into their produced fragments, then the normal type-check sees only ordinary AST. This
  is the sealed `parse → AST → [expand] → TYPE-CHECK` order, literally.
- **5.1-B — Family B pass (after type-check).** `expand_comptime(prog: TProgram): TProgram |
  error` at `project.tks:367` (§B.1-Opt-1), after `monomorph`, before `inline_consts`. This
  is the sealed `TYPE-CHECK → [comptime eval] → lower` order.

```teko
/**
 * expand_macros_syntactic — Family A pre-pass: rewrite every `@macro`-call in the UNTYPED
 * program into its produced AST fragment, to a bounded fixpoint, before type-check runs.
 * Deterministic (stable-key mangle) so self-host generations are byte-identical.
 *
 * @param prog  the parsed, os-pruned program (types do NOT exist yet)
 * @return      the program with all Family-A calls expanded, or the first expansion error
 * @throws      an expansion-depth-exceeded diagnostic, or a macro body `@error`
 * @since 0.4-macros
 */
fn expand_macros_syntactic(prog: parser::Program): parser::Program | error { /* §9 crumb 3 */ }
```

**Recommendation:** two named passes at the two sealed slots — no attempt to merge them (the
stages are different tables, `parser::Program` vs `TProgram`). Reject any single-pass design:
it would force one family to run at the wrong stage, breaking the seal.

## 5.2 — variadic mechanics (a CONSEQUENCE of untypedness for Family A)

For Family A, variadics are **not a bolt-on feature** — they fall out of §A.0's untypedness:
an anonymous node has no declared type, so a *pack* of anonymous nodes has no declared arity,
so `...args` is simply "the pack" and any count is legal for free. For Family B the args ARE
typed, so its variadic pack reuses the ordinary trailing-`is_params` discipline. Three shapes:

- **5.2-A — RECOMMENDED:** `...args` binds one **trailing** node/value pack after zero-or-more
  fixed params, reusing the established **trailing-only** `is_params` discipline
  (`ast.tks:511`, typer precedent). Family A → `args: []Node` (arity free, §A.0); Family B →
  `args: []<typed>` (each element const-foldable, or arity-only for `count`).

```teko
/**
 * printf_like — Family A: a fixed leading `fmt` node plus a trailing variadic pack, mirroring
 * the trailing-only `is_params` rule. Demonstrates fixed-then-pack binding. A `macro` emits.
 *
 * @param fmt   the leading format-string node
 * @param args  the trailing pack of argument nodes
 * @since 0.4-macros
 */
macro printf_like(fmt, ...args) { lowering { do_print(${fmt}, ${args.len}) } }
// call site:  @printf_like("x={}", a, b)  →  fmt fixed, args.len == 2
```

- **5.2-B:** a single pack only (no fixed leading params) — simpler, but can't express
  `@printf_like(fmt, ...)`. Rejected as too weak.
- **5.2-C:** splat forwarding `@m(...other)` to pass a pack through. Deferred (a later add).

**Recommendation: 5.2-A** (fixed params + one trailing pack, trailing-only). Forwarding
(5.2-C) is a later crumb.

## 5.3 — error model

- **5.3-A — RECOMMENDED primary:** macro errors are ordinary checker diagnostics at the
  **call site** (`@name` line/col), with a note pointing at the macro decl — reuse the
  `err_at` family. Family A errors surface either during expansion (bad node shape) or when
  the PRODUCED code fails the subsequent type-check (the sealed "ill-typed generated code
  fails normal type-check, pointing at the expansion"). Family B errors surface as
  `eval_const` failures (non-const arg VALUE, overflow) at the call site.
- **5.3-B:** report at the macro DEFINITION with a call-site backtrace note — better for buggy
  bodies, worse for the common misuse case. Secondary.
- **5.3-C — RECOMMENDED author intrinsic:** a `@error("msg")` intrinsic lets a macro body
  raise its OWN honest diagnostic (used in §A.1-Tier-2's `assert_eq`), a direct M.3
  expression.

```teko
/**
 * require_ident — Family A: rejects, with an author-authored honest diagnostic, any argument
 * that is not a bare identifier. Demonstrates `@error` (5.3-C) at the call site (5.3-A). A
 * `macro` emits; it has no return type.
 *
 * @param x  the argument node; must be a `Var`
 * @throws   a compile-time `@error` naming the offending node's source
 * @since 0.4-macros
 */
macro require_ident(x) {
    if x.kind != NodeKind::Var { @error("expected an identifier, got `" ++ x.source ++ "`") }
    lowering { var shadow = ${x} }                  // `shadow` (verbatim) is stable-mangled
}
// call site:  @require_ident(a + 1)  →  compile error: expected an identifier, got `a + 1`
```

**Recommendation: 5.3-A primary + 5.3-C intrinsic.**

## 5.4 — fixpoint & seed safety (both families)

**Fixpoint (self-host byte-identity).** Expansion MUST be deterministic and produce
byte-identical output across generations, exactly as `lower_const` guarantees symbol
byte-identity:
- Family B folds to literals via `literal_of` — inherently deterministic.
- Family A stable-mangle (§A.3) names are a **pure function of stable keys** (macro name +
  call-site line/col + local index — all available on `Expr.line`/`col`, `ast.tks:293`),
  **never** a global mutable counter — mirroring `const_leaf_symbol`/`const_rodata_symbol`.
  Only the `lowering` verbatim is mangled; a `${}`-injected node stays intact.
- **Expansion depth is bounded**: a macro that expands (directly or mutually) past a fixed
  depth is a **compile error** (a REJECT fixture, §8) — no unbounded/nondeterministic
  expansion reaches type-check (Family A) or lowering (Family B).

```teko
/**
 * loop_macro — Family A, the fixpoint HAZARD, caught: a macro that re-emits a call to itself
 * must hit the bounded-depth guard and become a compile error rather than expand forever. A
 * `macro` emits; it has no return type.
 *
 * @param x  any node
 * @since 0.4-macros
 */
macro loop_macro(x) { lowering { @loop_macro(${x}) } }
// call site:  @loop_macro(a)  →  compile error: macro expansion depth exceeded
```

**Seed safety (bootstrap ordering).** The bootstrap seed is the previously released `teko`
binary; the corpus must not USE a feature its seed lacks. Therefore:
1. Land lexer + parser + checker + **both** expansion passes — `src/**.tks` does **not** yet
   use `@`/`macro`/`comptime`.
2. **SEED BUMP** — release a seed that understands the two families (same discipline as
   module-const #594 → D40/D41).
3. **Only after the bump** may the corpus use `@`/`macro`/`comptime`. (There is no FFI-macro
   sequencing — `extern macro`/`extern comptime` are rejected, §7; libc constants are plain
   `const` items and need no seed gate.)

---

# §7 — FFI: `extern macro` / `extern comptime` are **REJECTED**

**Sealed (§14.3/§16): FFI is `extern fn` → libc ONLY. There is NO `extern macro` and NO
`extern comptime`.** Resolving a C header's function-like macros or object-like constants would
require **reading C headers = a C preprocessor / toolchain (gcc/cc/clang) dependency** — exactly
the dependency the project is REMOVING (`teko_rt` disappears; the own-linker resolves symbols
with no `cc`). So the FFI-macro tier machinery is **not "pending"; it is rejected** and does not
appear in this plan.

What replaces it:
- **libc SYMBOLS** are bound with ordinary **`extern fn N(...): R = "sym" from lib "c"`** (§16),
  resolved by the own-linker per OS/arch — no macros involved.
- **libc CONSTANTS** (`O_RDONLY`, `INT_MAX`, …) are **hand-declared Teko `const` items,
  per-platform, guarded by `#os`/`#arch`** in a curated `teko::sys` — a plain constant, not an
  `extern comptime`.

```teko
/**
 * O_RDONLY — a libc constant as a hand-declared, per-platform Teko `const` behind `#os` — NOT
 * an `extern comptime` (rejected: reading the header would need a C toolchain). The value is
 * curated in `teko::sys` for each platform.
 *
 * @since 0.3.1
 */
#os("linux") const O_RDONLY: i32 = 0
// call site:  var f = open(path, O_RDONLY)   →   an ordinary const, no `@`, no C toolchain

/**
 * htonl — a libc SYMBOL bound with a plain `extern fn` → libc (§16), NOT an `extern macro`.
 * The own-linker resolves the symbol per OS/arch; no C preprocessor is read.
 *
 * @param x  the u32 argument
 * @return   the byte-swapped result
 * @since 0.3.1
 */
extern fn htonl(x: u32): u32 = "htonl" from lib "c"
// call site:  var n = htonl(host_order)   →   an ordinary extern-fn call, no `@`
```

**Recommendation:** carry NO `extern macro`/`extern comptime` surface. libc symbols → `extern
fn` (§16); libc constants → hand-declared `#os`/`#arch`-guarded `const` in `teko::sys`. The
native `macro`/`comptime` families (§A/§B) are entirely unaffected — only the `extern` variant
is rejected.

---

## 8. Regression fixtures (inputs → expected native exit codes)

Named fixtures (pattern `macro_a_*/`, `macro_b_*/`), gated on the **native** engine (never
`teko test` here — design only). No `extern_*` macro fixtures — that FFI surface is rejected
(§7):

| Fixture | Family | Input shape | Expectation |
|---|---|---|---|
| `macro_a_stringify/` | A | `macro stringify(x) { lowering { ${x.source} } }` + `@stringify(a + b)` compared to `"a + b"`, exit = match | ACCEPT; **exit 0** on match |
| `macro_a_unless/` | A | `@unless(cond, { … })` expands to `if !(cond) {…}` | ACCEPT; exit proves the negated branch ran |
| `macro_a_emit_each/` | A | `macro emit_each(...args)` + `@emit_each(x, y, z)` | ACCEPT; exit proves each `println` emitted |
| `macro_a_guard/` | A | `macro guard(cond, ret)` + `@guard(x > 0, -1)` in a fn body | ACCEPT; exit proves the injected `return` fired in the CALLER |
| `macro_a_hygiene_no_capture/` | A | `@swap(x, t)` with a caller `t` (§A.3 stable-mangle) | ACCEPT; stable-mangle prevents capture (never an error); exit proves caller `t` intact |
| `macro_a_error_intrinsic_rejected/` | A | `@require_ident(a + 1)` (5.3-C) | REJECT — author `@error` "expected an identifier" (`EXPECT_COMPILE_FAIL`) |
| `macro_a_depth_rejected/` | A | `@loop_macro(a)` self-expanding (§5.4) | REJECT — "macro expansion depth exceeded" |
| `macro_b_count/` | B | `comptime count(...args): usize { args.len }` + `@count(f(), g())` returned from `main` | ACCEPT; native **exit 2** (arity only; f/g never run) |
| `macro_b_count_empty/` | B | `@count()` | ACCEPT; native **exit 0** |
| `macro_b_sum/` | B | `comptime sum(...args): usize { … }` + `@sum(2,3,5)` | ACCEPT; native **exit 10** |
| `macro_b_crc_table/` | B | `const CRC = @crc_table()`, exit = `CRC[i] & 0xFF` | ACCEPT; exit = the table entry (proves comptime fold) |
| `macro_b_sizeof/` | B | `exp global comptime sizeof<T>(): usize { T.size }` + `@sizeof<u32>()` returned from `main` | ACCEPT; native **exit 4** |
| `macro_b_typename/` | B | `@typename<i32>()` compared to `"i32"`, exit = match | ACCEPT; **exit 0** on match |
| `macro_b_infer_type_ok/` | B | `@elem_size(x)` / `@sizeof` over a RUNTIME local's inferred TYPE (value never evaluated) | ACCEPT; exit = the slot size (proves type-infer-of-runtime is OK) |
| `macro_b_nonconst_rejected/` | B | `@sum(a, b)` on runtime locals (evaluates VALUE) | REJECT — "argument N is not a compile-time constant" (`EXPECT_COMPILE_FAIL`) |
| `macro_unknown_rejected/` | both | `@nope(x)` with no such macro | REJECT — unknown macro at call site |
| `macro_arity_mismatch_rejected/` | both | fixed-param macro called with wrong arity | REJECT — arity diagnostic |

Each new/changed line carries **100% delta coverage** (D32, `DECISION_LOG.md:312`), measured
on the native gate, with any genuinely-unreachable arm listed with a one-line justification.

---

## 9. Ordered crumb sketch (recommended composite)

Each crumb is independently gate-able; **ritual points** (full C+self-host+native gate +
FIXPOINT) are marked ★.

1. **Lex `@` + the two family keywords.** Add `At` TokenKind (`token.tks`); emit it for a
   bare `@` not starting a string (`lexer.tks` `read_string_body`/`read_symbol`, guarding the
   `@"…"` verbatim path at `:429-453`); reserve `macro`/`comptime` in `keyword_kind`
   (`lexer.tks:331-372`) — the keyword names are CLOSED (§14.3). Gate: lexer tests.
2. **AST + parse (both families).** Add `MacroDecl`/`ComptimeDecl` to `Decl` (`ast.tks:807`)
   — native only, **no `extern` macro/comptime form** (rejected, §7) — and a `MacroCall` node
   to `ExprKind` (`:292`) + `Node` scaffolding (A.1-Tier-1); parse the decls and the
   `@`-prefix call in `parse_expr`. Honest-stop bodies that compile. Gate: parser tests.
3. ★ **Family A pre-pass (syntactic, §5.1-A).** `expand_macros_syntactic` after `#os` prune,
   before type-check (`project.tks:464`/`:116-126`); A.1-Tier-1 surface (`.len`/`.source`),
   the sealed `lowering`/`${}` splice (§A.2), stable-mangle hygiene (§A.3), **`macro` always
   emits code — no return type** (§A.0), 5.3-A/5.3-C errors, 5.4 bounded depth. Gate:
   FULL gate + `macro_a_stringify`, `macro_a_unless`, `macro_a_hygiene_no_capture`,
   `macro_a_depth_rejected`, `macro_a_error_intrinsic_rejected`.
4. ★ **Family B pass (comptime, §B.1-Opt-1).** `expand_comptime` before `inline_consts`
   (`project.tks:367`); bind typed args, `eval_const` the body, `literal_of` the result
   (return inlined as a LITERAL); the comptime-KNOWN rule (evaluate runtime value = error,
   infer runtime type = OK); `@sizeof`/`@typename` as **comptime macros over type reflection**
   (`T.size`/`T.name`, Extent-1) plus the **runtime `sizeof` fn** as a plain `global fn`
   (B.2); 5.3-A errors. Gate: FULL gate + `macro_b_count` (exit 2), `macro_b_sum` (exit 10),
   `macro_b_crc_table`, `macro_b_sizeof` (exit 4), `macro_b_typename`, `macro_b_infer_type_ok`,
   `macro_b_nonconst_rejected`.
5. ★ **SEED BUMP #1.** Release a seed understanding both families; only after may `src/**.tks`
   use `@`/`macro`/`comptime` (D40/D41 discipline). **Ritual.**
6. **Family A Tier-2 surface (post-seed).** `.kind` + read accessors (A.1-Tier-2) →
   `@assert_eq`; colored hygiene (the §A.3 rejected note) only if ever demanded. Gate: FULL
   gate + structure-directed fixtures.
7. **Type-reflection extent expansion (post-seed, owner-pending).** If the owner ratifies
   Extent-2/Extent-3 (§B.2), add `T.align` / `T.fields` reflection (guarded: `.fields` feeds
   comptime VALUES only, never a runtime field read); B.1-Opt-3 two-phase if
   comptime-calls-comptime lands. Gate: FULL gate + the relevant comptime fixtures.

**Ritual points (full gate must pass):** crumbs **3** (Family A pre-pass — new pre-typecheck
program transform), **4** (Family B comptime pass — shared comptime engine), **5** (seed bump).

---

## 10. Risks & law tensions (each with a recommended resolution)

| Risk / tension | Where it bites | Recommended resolution |
|---|---|---|
| **D33 / `TEKO_LEGISLATION.md:607` "no macros" seal** | The facility exists at all | Owner override (§14 seal) — noted, not relitigated. On landing, amend D33's reference + the legislation line to "superseded for the macro facility by owner ruling" (doc-sync, not a code change here). |
| **`macro` return-type discrimination (STALE)** | An earlier framing let a return type make a `macro` "compute a value" | **REMOVED (§14.1):** a `macro` has NO return type and ALWAYS emits code. Every value case is `comptime` (Family B), whose return is inlined as a literal. `count`/`sum`/`sizeof` are all `comptime`. No return discriminator inside a macro. |
| **"Third macro class" / typed-macro-for-field-codegen (STALE)** | A prior proposal for a `.fields`-driven codegen macro | **REMOVED (§14.2):** there is NO third class; "runtime macro" is a contradiction. Anything needing a runtime VALUE (incl. `.fields`-driven equality, the retired structural `Eq`) is a **function/method + trait impl** (§9.4). Reflection may count/measure fields (comptime VALUES) but never read a runtime field value. |
| **M.0 small-language surface** | Family A `Node` reflection; Family B type reflection | Stage the surface: A.1-Tier-1 (`.len`/`.source`) in the seed, Tier-2 (`.kind`/accessors) as a gated crumb, Tier-3 builder deferred; B.2 type-reflection **Extent-1** (`T.size`/`T.name`) in the seed, Extent-2 (`.align`) / Extent-3 (`.fields`) owner-pending, each its own crumb. |
| **M.3 honesty vs. silent capture** | Family A splice hygiene | **SEALED: stable-mangle, never an error** (§A.3) — only the `lowering` verbatim is mangled, a `${}`-injected node stays intact; colored hygiene considered and rejected; Family B owes NO hygiene (produces a value). |
| **`@sizeof`/`@typename` as hidden builtins (STALE)** | Where sizing/naming intrinsics live | **REMOVED (§14.2):** they are visible `comptime` MACROS over type reflection (`global`, no-shadow), NOT builtins. `sizeof` is TWO constructs: `exp global comptime sizeof<T>(): usize` (slot) + runtime `exp fn sizeof<T>(t): u64` (bytes). The builtin-intrinsic option is rejected by the seal. |
| **FFI `extern macro`/`extern comptime` (REJECTED)** | The C-macro/constant FFI tier | **REJECTED (§14.3/§16):** resolving C headers needs a C toolchain (gcc/cc/clang) — the very dependency being removed. libc symbols → `extern fn` → libc (§16); libc constants → hand-declared `#os`/`#arch` `const` in `teko::sys`. No `extern`-macro surface, not even "pending". Native families unaffected. |
| **Fixpoint (self-host byte-identity)** | Family A stable-mangle; expansion determinism | **Stable-key mangle** (pure fn of macro name + call-site loc + name + expansion index, `Expr.line`/`col`), never random, bounded depth, `literal_of` determinism — mirrors `lower_const`. |
| **Seed ordering** | Corpus using macros before the seed knows them | **Land-then-seed-bump** (crumb 5); corpus abstains until then. No FFI-macro sequencing (that surface is rejected). |
| **Two passes at two stages** | Family A on `parser::Program`, B on `TProgram` | Do NOT merge — two named passes at the two sealed slots (§5.1). Merging would force a family to the wrong stage, breaking the seal. |

**Owner-pending items (the ONLY open ones):** the **type-reflection EXTENT** (§B.2 —
Extent-1 `size`/`name` shipped; Extent-2 `align` / Extent-3 `fields` staged, each a crumb).
Everything else is resolved law-first with a concrete recommendation and runnable examples.
The keyword names `macro`/`comptime` are **CLOSED** (§14.3) — not a pending question. **No
genuine unresolved tension remains — no HALT.**
