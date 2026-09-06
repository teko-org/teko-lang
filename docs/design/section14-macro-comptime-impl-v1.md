# §14 macro + comptime — IMPLEMENTATION crumb-plan (keystone)

Version: **v1** (2026-08-14). Status: **IMPLEMENTATION PLAN — ordered, gate-able crumbs.**
Read-only on product code; this document writes no `.tks`, triggers no reseed, runs no build,
and — per the task constraint — **`teko test` / build were NOT run in any form** (the
`monomorph` leak crashes the container). Author: architect. Base: `origin/fix/retirement`.

> **Relationship to `docs/design/plano-macro.md` (EXTENDS, does not supersede).** That doc is
> the SEALED two-family DESIGN PROPOSAL SET (the deliberation + the option matrices + the sealed
> rulings). This doc is the **IMPLEMENTATION sequence** that turns it into ordered, independently
> gate-able crumbs, keyed to the three keystone UNLOCKS — (1) serialization via comptime
> field-reflection `@fields<T>()`, (2) the §9.D `@Type()` union abbreviation, (3) comptime
> `sizeof`. It does NOT re-litigate any sealed point; it adds the file-level recon deltas the
> proposal set did not carry, and it splits the work into Family-A and Family-B crumb chains with
> concrete signatures and fixtures. Where a NEW implementation mechanic is genuinely open (union
> splice in TYPE position; aggregate `literal_of`; `@fields` projection), it is presented as 3+
> options with a law-first recommendation. The serialization CONSUMER design lives in
> `docs/design/serial-tags-comptime-field-reflection-0.3.1.md`; this plan is its §14 producer.

---

## 1. RECON — current state (file:line)

### 1.1 Family B engine — what `comptime_fold` COVERS and what is MISSING

- `eval_const(e: TExpr, table: TypeTable, env: Env, agg: AggConstMap): ConstValue | error` —
  `src/checker/comptime_fold.tks:306`. Folds a typed op-tree over the `ConstValue` domain
  (`CVInt | CVFloat | CVBool | CVBytes | CVAgg` — `:18-36`, constructor `cv_agg` `:150`),
  including index (`:963`) and `.len` (`:1079`) over `CVAgg`/`CVBytes`. `predicate_folds_const`
  — `:339`. **COVERS:** name-free/const arithmetic, index, len, aggregate VALUES.
- `inline_consts(prog: TProgram): TProgram | error` — `src/checker/consteval.tks:531`, wired at
  `src/build/project.tks:367`, **after `monomorph` (`:353-354`), before lowering**. This is the
  exact pipeline slot for the Family-B pass (§B.1-Opt-1 of `plano-macro.md`).
- **GAP #1 (load-bearing).** `literal_of(v: ConstValue, ty, line, col): TExpr | null` —
  `comptime_fold.tks:1997` — reconstructs **ONLY** `CVInt` (`int_literal_of`, `:2015`); every
  other kind is `_ => null` (`:2000`). Family B inlines its RETURN as a literal, so
  `@typename` (str/`CVBytes`), `@sum` (usize is int — OK), `@crc_table` (`[256]u32`/`CVAgg`),
  and `@fields` (`[]FieldInfo`/`CVAgg`) all need **aggregate/scalar `literal_of`** that does
  not exist yet. This is crumb B2 below and is the single biggest engine delta.
- **GAP #2.** There is **no `@`-call dispatch** — `eval_const` folds an op-tree, but nothing
  binds a `comptime` DECL's params to a call-site's argument `ConstValue`s and folds the body.
  That binder is the Family-B pass proper (crumb B3).
- **GAP #3.** There is **no type-reflection** (`T.size`/`T.name`/`T.fields`) reachable from a
  comptime body. `@sizeof`/`@typename`/`@fields` need it (crumbs B4/B5).

### 1.2 Family A machinery — GREENFIELD (confirmed absent)

`git grep -w` over `src/**/*.tks` for `macro`, `comptime`, `lowering`, `@`-as-token finds
**nothing** (the only hits are `MDivSeqX86` "macro-instruction" and backend "lowering" in the
codegen sense — unrelated). So the entire Family-A path — the `@` token, the `macro`/`comptime`
keywords, the `MacroCall` node, `lowering`/`${}`, the pre-type-check expand pass, stable-mangle
hygiene — is net-new. `plano-macro.md` §0 already maps the seams: `@` is an unclaimed lexical
slot (only `@"…"` verbatim strings claim it — `src/lexer/lexer.tks:429-453`); `keyword_kind`
(`:331-372`) reserves neither keyword; `Expr` carries `line`/`col` (`src/parser/ast.tks:293`) —
the stable-mangle key.

### 1.3 The pipeline, and where the two families sit

```
lexer → parser (parser::Program)
                 └─[Family A] expand_macros_syntactic   ← BEFORE type-check (untyped AST enlarge)
      → type-check → PreMono → monomorph (TProgram)      project.tks:353-354
                 └─[Family B] expand_comptime            ← AFTER type-check, project.tks:367
                 └─[serialization] synthesize_serializers ← same slot, monomorph-style synthesis
      → inline_consts → lower → backend
```

Family A rewrites `parser::Program` (§5.1-A of plano-macro). Family B rewrites `TProgram` at
`project.tks:367` (§B.1-Opt-1). Serialization's per-type synthesis is a THIRD pass at the same
post-monomorph slot, a thin driver over Family B (§4 recon below).

**Owner ruling 2026-08-14 — the macro system is THREE families, along the text→AST→value boundary**
(revises the older "no third class"). Each expands at a different pipeline stage:
- **A · `macro`** — expands the **AST** via `lowering` (runs POST-lexer, PRE-type-check;
  `expand_macros_syntactic`). Parametric type-macros (comptime `if`-select, Form 2) live here —
  `section14-parametric-type-macros-v1.md`.
- **B · `comptime`** — a function EXECUTED without expanding the AST; returns values inlined at the
  call site (POST-type-check; `expand_comptime`). B1–B5.
- **C · `generator`** — expands BEFORE the lexer, manipulating the SOURCE STRING of the written code.
  The `${string}`/concat/prefix authoring (the parametric-type-macro "Form 1") belongs HERE, not in
  `macro`; it is a NEW family, FUTURE work, not this wave.

**Owner ruling 2026-08-20 (recovered post-reset — DECISION_LOG D66).** Refinements to the three-family model above:
- **Composition matrix (what each form's body may contain).** `generator` is **atomic** — no meta inside and none in its emitted string. `macro` may nest only `macro`; `comptime` may nest only `comptime`. Every cross-family nesting is **forbidden** (macro✗comptime, comptime✗macro, and none may contain a `generator`). Self-nesting rides the existing recursion (expansion/fold are already recursive). No dedicated checker guard is needed — a cross-family nesting **fails naturally** in the pipeline.
- **`generator` (C) was BUILT and LOST, not never-written** — it was dropped in the working-tree reset. Since the corpus does not use it, it stays **parked as future work**; if ever needed, **rebuild from this design, do not "recover" code.** Why it alone needs a pre-compilation: it runs at LEX time and must return a **string**, but the lexer cannot execute an AST. So a pre-compilation scans the source, classifies the three families into an in-memory dispatch table, and compiles **only the generators** into an in-memory intermediate binary; at lex time the lexer just calls the generator's pointer. `macro` (template token-splice) and `comptime` (interpreted fold) need no such ahead-of-time compile.
- **Dispatch is implicit (lookup-based) and that IS the canon.** The explicit GENERATOR/MACRO/COMPTIME tagged cascade once considered was a means to remove a blocker that no longer exists — **dropped.**
- **Inertness preserved:** the Family-A/Family-B passes are no-ops when the corpus uses no `@`/`macro`/`comptime`, so generation stays byte-identical.

### 1.4 Serialization substrate — `@fields<T>()` projects over `FieldView`

- `pub type FieldView = struct { name: str; type: Type }` — `src/checker/collect.tks:1618`.
- `pub fn deriver_field_view(type_name: str, table: TypeTable): []FieldView | error` —
  `collect.tks:1673` — already enumerates a struct's own fields (trait-flattened) and a class's
  full base chain (`effective_class_fields`), each field's annotation **resolved to a real
  `Type`**. This is exactly the substrate `@fields<T>()` projects onto: `name` and `type` are
  in hand; `is_nullable` is a predicate over the resolved `Type` (a `X | null` union, §9.D);
  `vis` is on the source `parser::Field` (`ast.tks:643`).
- **BLOCKER (declared, not this plan's to close).** `pub type Field = struct { name; type_ann;
  vis; is_intern; has_doc; doc; is_readonly }` — `ast.tks:643` has **NO `tag` field**. The
  serialization doc's `FieldInfo.tag` (backtick Go-style tags) needs a parser-level backtick-tag
  addition that the SERIALIZATION work owns, not §14. See §6 (DESIGN-AHEAD).

### 1.5 The synthesis precedent

`monomorph` (`project.tks:353-354`) already **synthesizes new named `TFunction`s** (specialized
generic instances) into the `TProgram` post-type-check and memoizes them. `synthesize_serializers`
(serial doc §4.1c) copies this shape exactly: read `@fields<T>()`, assemble one `serialize_T`
`TFunction`, insert once memoized by `(T, format)`, retarget the call. Zero new machinery kind.

### 1.6 Composition dependencies not yet landed

- **`global` (§15) is ABSENT.** `git grep -w global` over lexer/parser/ast finds no modifier and
  no `is_global` field. `exp global comptime sizeof<T>()` / `global comptime fields<T>()` need
  §15 to be reachable UNQUALIFIED. The comptime MACHINERY does not depend on `global`; only the
  unqualified REACH of the three stdlib decls does (§6, composition point).
- The `macro`/`comptime` keywords and the `@`-call token are unreserved (see §1.2) — crumb 0.

---

## 2. Crumb-plan — FAMILY A (`macro`, syntactic, pre-type-check)

Each crumb is independently gate-able. **★ = ritual point** (full C+self-host+native gate +
FIXPOINT byte-identity). Signatures are in full-Javadoc Teko, copy-verbatim.

### A0 — lex `@` + reserve `macro`/`comptime` (shared with Family B)

Add an `At` `TokenKind` (`src/lexer/token.tks`, next to `Hash` `:133`); emit it for a bare `@`
NOT followed by `"` (guard the `@"…"`/`$@"…"` verbatim path at `lexer.tks:429-453`, fall
through to a new `At` emission instead of `read_symbol`'s "unexpected character"). Reserve
`macro` and `comptime` in `keyword_kind` (`:331-372`) — the names are CLOSED (§14.3). **Inert:**
no source uses `@`/`macro`/`comptime` yet, so the lexer output over the whole corpus is
byte-identical. **Gate:** lexer `.tkt` — `@name` lexes to `[At, Ident]`; `@"x"` still lexes to
one verbatim string; `macro`/`comptime` classify as keywords.

### A1 — AST nodes + parse (both families, native-only, NO `extern` form)

Add to `parser::Decl` (`ast.tks:826`) two variants and to `ExprKind` (`:292`) the call node:

```teko
/**
 * MacroDecl — a Family-A syntactic macro declaration (`macro name(params) { body }`). Pre-type-check,
 * copy-in-place, ENLARGES the AST via `lowering`/`${}`; it has NO return type (a `macro` always emits,
 * never yields a value — the value case is `ComptimeDecl`). Native-only: there is NO `extern macro`
 * (rejected, plano-macro §7). Params are anonymous nodes (untyped until materialized), so arity is free
 * and a trailing `...pack` (the `is_params` precedent, `ast.tks:511`) binds the variadic tail.
 *
 * @field name    the macro's declared name (the `@name(...)` call target)
 * @field params  the anonymous parameter list; a trailing `is_params` param binds the node pack
 * @field body    the macro body: comptime logic + `lowering { … }` blocks (no return type)
 * @field vis     visibility (`pub`/`exp`/internal); `global` (§15) reaches it unqualified when landed
 * @since 0.4-macros
 */
pub type MacroDecl = struct { name: str; params: []Param; body: Block; vis: Visibility }

/**
 * ComptimeDecl — a Family-B comptime-evaluation declaration (`comptime name(params): T { body }`).
 * Post-type-check, args are typed comptime-KNOWN values; the body folds to a value INLINED as a literal
 * at the call site. Native-only: there is NO `extern comptime` (rejected, plano-macro §7). Carries a
 * return type (the inlined literal's type); `T.size`/`T.name`/`T.fields` reflection is available in the
 * body per the ratified extent (§4).
 *
 * @field name         the comptime macro's name (the `@name(...)` call target)
 * @field type_params  the `<T…>` generic params (a comptime may INFER a runtime arg's type, §14.2)
 * @field params       typed parameters; a trailing `is_params` param binds a typed pack
 * @field ret          the return type — the inlined literal's type
 * @field body         the body, folded by `eval_const` over typed values
 * @field vis          visibility; `global` (§15) reaches it unqualified when landed
 * @since 0.4-macros
 */
pub type ComptimeDecl = struct { name: str; type_params: []str; params: []Param; ret: TypeExpr; body: Block; vis: Visibility }

/**
 * MacroCall — an `@name(...)` call site in the untyped AST: a compiler-executed call the resolver
 * routes to a `MacroDecl` (Family A, expanded pre-type-check) or a `ComptimeDecl` (Family B, folded
 * post-type-check) by the resolved declaration's family. A BARE call (no `@`) never produces this node —
 * it stays an ordinary runtime `Call`. Carries `callee_type_args` for `@sizeof<T>()`/`@fields<T>()`.
 *
 * @field callee           the macro/comptime name path
 * @field args             the raw call-site argument expressions (nodes for Family A; folded for B)
 * @field callee_type_args the explicit `<T…>` type arguments (`@sizeof<i32>()`)
 * @since 0.4-macros
 */
pub type MacroCall = struct { callee: Path; args: []Expr; callee_type_args: []TypeExpr }
```

Parse `macro`/`comptime` decls in `parse_decl.tks` (mirroring the `Function` split at `:401-489`,
minus every `extern`/`c_symbol`/`from_lib` branch — rejected), and the `@`-prefixed call in
`parse_expr` (an `At` before a `Call` shape ⇒ `MacroCall`). Bodies land as honest-stop that
compiles. **Inert.** **Gate:** parser `.tkt` — round-trip of a `macro`/`comptime` decl and an
`@name(...)`/`@name<T>()` call; a bare `name(...)` still parses to `Call`.

### A2 — the `lowering { … }` + `${expr}` splice (SEALED shape; NEW impl mechanic below)

`lowering { … }` captures a verbatim fragment template; `${expr}` marks an injection hole. Per
plano-macro §A.2 this is SEALED to `lowering`/`${}` (not `quote`). The **open implementation
mechanic** is the GRAMMATICAL POSITION the fragment re-parses in — plano-macro's examples are all
EXPRESSION/STATEMENT position, but §9.D's `@Type()` splices into a **TYPE position**
(`members: []@Type()`). Options in §5.1.

### A3 ★ — Family A pre-pass `expand_macros_syntactic`

```teko
/**
 * expand_macros_syntactic — Family A pre-pass: rewrite every `@macro`-call in the UNTYPED, os-pruned
 * program into its produced AST fragment, to a bounded fixpoint, BEFORE type-check. Runs in
 * `frontend_check` (`project.tks:464`) immediately after `#os` pruning (`:116-126`) and before
 * `type_program_with_deps_pre_mono`. For each `MacroCall` whose callee resolves to a `MacroDecl`: run
 * the body's comptime logic, emit the selected `lowering` fragment(s) with `${}` holes filled by the
 * caller's INTACT arg nodes and the `lowering` verbatim STABLE-MANGLED for hygiene (plano-macro §A.3),
 * splice the fragment at the call position (expr OR type position, §5.1). Deterministic: the mangle key
 * is a pure function of macro name + call-site line/col + name + expansion index (mirrors
 * `lower_const`), so self-host generations are byte-identical. Bounded depth: past the fixed limit is a
 * located compile error (no unbounded expansion reaches type-check).
 *
 * @param prog  the parsed, os-pruned program (types do NOT exist yet)
 * @return      the program with every Family-A call expanded, or the first expansion error
 * @throws      an expansion-depth-exceeded diagnostic, or a macro body `@error`
 * @since 0.4-macros
 */
fn expand_macros_syntactic(prog: parser::Program): parser::Program | error { /* honest-stop until A2/A3 */ }
```

Surface = A.1-Tier-1 (`.len`/`.source`/index — plano-macro §A.1). Errors = call-site diagnostic
+ `@error` intrinsic (§5.3-A/C). **Gate (★, full + FIXPOINT):** `macro_a_stringify`,
`macro_a_unless`, `macro_a_hygiene_no_capture`, `macro_a_depth_rejected`,
`macro_a_error_intrinsic_rejected`, **plus the keystone** `macro_a_type_union_9d` (§5.1, §7).

### A4 — UNLOCK §9.D `@Type()` (rides A3, needs the TYPE-position splice of §5.1)

Once A3 + the context-polymorphic splice (§5.1-Opt-1) land, `pub macro Type() { lowering { (Prim
| Byte | … | Null) } }` and `pub type Variant = struct { members: []@Type() }` expand pre-type-check
into the literal inline union — no named `type`, §9.D honored. This is a **source-only** consumer
that lands only AFTER the seed bump (crumb S).

---

## 3. Crumb-plan — FAMILY B (`comptime`, evaluation, post-type-check)

### B1 — resolver: route `@name(...)` to `ComptimeDecl`

Teach name resolution that a `MacroCall` whose callee is a `ComptimeDecl` is a Family-B call
(the survivors after A3 removed every Family-A `MacroCall`). Type-check binds
`callee_type_args`/args normally (a `comptime` call is typed like a generic fn call; the RESULT
type is `ret`). **Gate:** checker `.tkt` — a `@comptime_name()` type-checks with `ret` as its
type; unknown `@name` ⇒ call-site diagnostic.

### B2 — GAP #1: aggregate/scalar `literal_of` (the engine delta)

Extend `literal_of` (`comptime_fold.tks:1997`) beyond `CVInt`. This is REQUIRED for every Family-B
return that is not a plain int (`@typename`→str, `@crc_table`→`[256]u32`, `@fields`→`[]FieldInfo`).
See §5.2 for the three options + recommendation.

```teko
/**
 * literal_of — reconstruct a literal `TExpr` from a computed `ConstValue`, typed as `ty`. EXTENDED
 * (this crumb) from the CVInt-only inverse to the full domain: CVBool→TBool, CVFloat→TNumber(float),
 * CVBytes→TStrLit, CVAgg→TArrayLit/TStructLit (recursing per element). Above a size threshold an
 * aggregate is instead emitted to a rodata const symbol via `lower_const` and referenced (§5.2-Opt-3),
 * so a 256-entry table does not become a giant AST literal. The produced node is indistinguishable to
 * lowering from a hand-written literal — zero runtime, zero reflective metadata.
 *
 * @param v     the computed comptime value
 * @param ty    the type the fold site resolved (the reconstructed literal's `.type`)
 * @param line  the original expression's source line
 * @param col   the original expression's source column
 * @return      a literal `TExpr` carrying `v` typed `ty`, or null when `v` has no literal form under `ty`
 * @since 0.4-macros (extends #comptime-fold)
 */
fn literal_of(v: ConstValue, ty: Type, line: u32, col: u32): TExpr | null { /* §5.2 */ }
```

**Gate:** `comptime_fold` `.tkt` — CVBool/CVFloat/CVBytes/CVAgg round-trip through
`eval_const`→`literal_of`→re-`eval_const` to the same value.

### B3 ★ — Family B pass `expand_comptime`

```teko
/**
 * expand_comptime — Family B pass: fold every surviving `@name(...)` comptime call in the typed,
 * monomorphized program to a literal, at `project.tks:367` (after `monomorph`, before `inline_consts`).
 * For each call whose callee is a `ComptimeDecl`: bind params to the caller's argument `ConstValue`s via
 * `eval_const` over an `Env` (`scope.tks:72`) — a param needing a VALUE bound to a runtime arg is a
 * call-site error (the comptime-KNOWN rule, §14.2); evaluate the body with `eval_const`; replace the
 * call `TExpr` with `literal_of(result)`. INFERRING a runtime arg's TYPE (generics) is OK and never
 * evaluates the arg. Deterministic ⇒ byte-identical fixpoint. Downstream (`inline_consts`, lowering) is
 * untouched — comptime calls become ordinary literals.
 *
 * @param prog   the type-checked, monomorphized program
 * @param table  the folded type table (for arg binding + type reflection, §4)
 * @return       the program with every Family-B call inlined as a literal, or the first fold error
 * @throws       a call-site diagnostic when an argument's VALUE is required but not comptime-constant
 * @since 0.4-macros
 */
fn expand_comptime(prog: TProgram, table: TypeTable): TProgram | error { /* honest-stop until B2/B3 */ }
```

**Gate (★, full):** `macro_b_count` (exit 2), `macro_b_sum` (exit 10), `macro_b_crc_table`,
`macro_b_nonconst_rejected`.

### B4 — UNLOCK comptime `sizeof` + `typename` (type reflection Extent-1: `T.size`/`T.name`)

Add `T.size`/`T.name` reflection reachable from a comptime body (a `TFieldAccess` on a TYPE
operand folds to a `CVInt`/`CVBytes` in `eval_const`). Then the two stdlib decls (in `src/mem`):

```teko
/**
 * sizeof (comptime) — the SLOT size of `T`, folded at comptime over type reflection (`T.size`) and
 * inlined as a literal. Explicit `T`, NO value param. `exp` (enters the `.tkh`); `global` (§15) reaches
 * it unqualified as `@sizeof<T>()`. A VISIBLE stdlib declaration — not a hidden builtin (the no-shadow
 * principle, §14.2). `@` marks the compiler-executed call; the bare `sizeof(t)` below is the runtime fn.
 *
 * @return  the slot size (bytes) of `T`, inlined as a literal
 * @since 0.4-macros
 * @see the runtime `sizeof` function (bytes occupied by a VALUE)
 */
exp global comptime sizeof<T>(): usize { T.size }

/**
 * sizeof (runtime) — a plain RUNTIME function (NOT a macro): the bytes OCCUPIED by a value. Takes a
 * value, called bare (no `@`). The "needs a runtime value ⇒ real function" rule (§14.2).
 *
 * @param t  the value whose occupied byte-size is measured (null ⇒ 0)
 * @return   the number of bytes occupied by `t`
 * @since 0.4-macros
 */
exp fn sizeof<T>(t: T | null = null): u64 { if t == null { return 0 } /* bytes occupied by t */ }
```

**Composition note (§15 dependency).** Until `global` (§15) lands, these are reachable ONLY
qualified (`mem::sizeof<T>()` under `@`); the `global`-unqualified reach is added when §15 closes
(§6). The comptime MACHINERY (B1–B4) does not block on §15. **Gate:** `macro_b_sizeof` (exit 4),
`macro_b_typename`, `macro_b_infer_type_ok`.

### B5 ★ — UNLOCK serialization input `@fields<T>()` (type reflection Extent-3, GUARDED)

`@fields<T>()` is the serialization keystone. It requires reflection Extent-3 (`T.fields`), which
plano-macro §B.2 lists as "owner-pending/deferred" — this plan **recommends ratifying Extent-3
now**, because the serialization proposal REQUIRES it and the seal already permits it GUARDED
(reflection feeds comptime VALUES/descriptors ONLY — never a runtime field-value read; §14.2). Not
a HALT: the guardrail is the seal itself (see §8).

```teko
/**
 * FieldInfo — one field of a type's COMPTIME field view: declared name, nullability (`X | null`, §9.D)
 * and visibility. Compile-time-only: carries NO runtime value and never survives to the emitted program
 * (Law M.0). The bounded projection of the checker's `FieldView` (`collect.tks:1618`). The `tag` member
 * is added when the backtick-tag parser lands (serialization-owned; §6) — omitted in v1.
 *
 * @field name        the field's declared name
 * @field is_nullable true iff the declared type is a `X | null` union (§9.D)
 * @field vis         the field's visibility
 * @since 0.4-macros (serialization input)
 */
exp type FieldInfo = struct { name: str; is_nullable: bool; vis: parser::Visibility }

/**
 * fields — Family B comptime reflection: the compile-time field view of `T`, one `FieldInfo` per field
 * in declaration order, as a comptime aggregate (CVAgg). A comptime VALUE (descriptors only) — it never
 * reads any field's runtime VALUE, so M.0's guardrail holds. Projects `deriver_field_view`
 * (`collect.tks:1673`): `name`/`vis` are direct; `is_nullable` is read off the already-resolved `Type`
 * (a `X | null` union). The INPUT the synthesis pass (§4) consumes — never emitted itself.
 *
 * @return  the field descriptors of `T`, as a comptime aggregate
 * @since 0.4-macros (serialization input)
 */
exp global comptime fields<T>(): []FieldInfo { T.fields }
```

`@fields<T>()` folds (via B3 + B2's CVAgg `literal_of`) to a comptime `[]FieldInfo`. **Gate (★,
full):** `comptime_b_fields_count` (`@fields<Point>().len` → exit 2), `comptime_b_fields_names`
(a per-field name comparison), `comptime_b_fields_nullable` (a `X | null` field reads
`is_nullable == true`).

### B6 ★ — UNLOCK serialization synthesis `synthesize_serializers` (monomorph-style)

The per-type serializer synthesis (serial doc §4.1c) — a THIRD pass at the post-monomorph slot,
driven by `@fields<T>()`:

```teko
/**
 * synthesize_serializers — for every reached `json::encode<T>`/`decode<T>` instantiation, read
 * `@fields<T>()`, ASSEMBLE one `serialize_T`/`parse_T` `TFunction` (walking the field descriptors, one
 * baked branch per field per §9.D nullability), INSERT it into the `TProgram` ONCE (memoized by
 * `(T, format)`), and retarget the call. Mirrors `monomorph`'s synthesize+memoize (`project.tks:353-354`)
 * — the established precedent — at the same slot, post-type-check so nullability is real. Zero runtime
 * reflection: the routine is a plain `TFunction` of direct field accesses; `@fields`/tags/the walk
 * evaporate. The compiler's per-field walk is ordinary host-Teko AST building (the serial doc's
 * `comptime for`/`emit` are the illustrative shape of THIS loop, not new surface constructs).
 *
 * @param prog   the type-checked, monomorphized program
 * @param table  the folded type table (field views + resolved types)
 * @return       the program with per-(type,format) serializer routines synthesized in, or an error
 * @throws       a diagnostic when a type is not field-shaped
 * @since 0.4-macros (serialization synthesis)
 */
fn synthesize_serializers(prog: TProgram, table: TypeTable): TProgram | error { /* serial doc §5.2 */ }
```

Runs adjacent to `expand_comptime` at `project.tks:353-367`. **BLOCKED on the backtick-tag parser**
for the FULL grammar (tag-driven key renaming/`omitempty`/`json:"-"`) — but the UNTAGGED path
(field-name keys, `X | null` nullability) is fully buildable on `@fields<T>()` v1. **Gate (★,
full):** `serial_encode_untagged/` (a struct → its JSON by field name, native exit proves the
bytes), `serial_encode_nullable/` (a `X | null` field omitted/nulled per §9.D).

---

## 4. What each unlock needs, precisely

| Unlock | Needs | Crumbs | Blocked-on |
|---|---|---|---|
| §9.D `@Type()` | Family A + TYPE-position splice (§5.1) | A0–A4 | seed bump (source-only consumer) |
| comptime `sizeof`/`typename` | Family B + reflection Extent-1 (`T.size`/`T.name`) + aggregate `literal_of` (str) | A0,A1,B1–B4 | `global` unqualified reach → §15 (machinery not blocked) |
| serialization `@fields<T>()` | Family B + reflection Extent-3 (`T.fields`, guarded) + CVAgg `literal_of` | A0,A1,B1–B3,B5 | Extent-3 ratification (recommended, §8); `.tag` member → backtick parser (serialization-owned) |
| serialization synthesis | `@fields<T>()` + monomorph-style synth pass | B5,B6 | full tag grammar → backtick parser; untagged path unblocked |

---

## 5. Open IMPLEMENTATION mechanics — options + law-first recommendation

### 5.1 The `lowering` splice POSITION (`@Type()` needs a TYPE-position splice) — OPEN

plano-macro's `lowering` examples are all expr/stmt position; §9.D splices a UNION into a TYPE
annotation (`members: []@Type()`). How does one `lowering` produce both an `Expr` fragment
(`@unless`) and a `TypeExpr` fragment (`@Type`)?

- **Opt-1 — context-polymorphic `lowering` (position decides) — RECOMMENDED.** The parser
  captures the `lowering { … }` body as a raw token run and re-parses it in the GRAMMATICAL
  position of the `@call`: an `@Type()` sitting in a type annotation re-parses its fragment as a
  `TypeExpr` (a union `(A | B | …)`); an `@unless(...)` in expression position re-parses as an
  `Expr`. One splice, no new keyword — matches the seal's "`lowering` = lowers code into the
  AST" generically. Example: `macro Type() { lowering { (Prim | Byte | … | Null) } }` used as
  `[]@Type()` yields the array-of-union `TypeExpr`; used nowhere-else it never materializes.
- **Opt-2 — category-tagged splice (`lowering type { }` / `lowering expr { }`).** The macro
  declares the production category on each block. More explicit, but adds two spellings and
  surface (fights M.0), and forces `@Type` and `@unless` to spell different `lowering`s.
- **Opt-3 — decl-site production kind (`macro Type() -> type { … }`).** The macro fixes its
  output category at declaration. Cleanest resolver story, but re-introduces a return-type-shaped
  discriminator on `macro` — grates against the seal ("`macro` has NO return type", §14.1).

**Recommendation: Opt-1 (context-polymorphic).** Law-first: M.0 (no new keyword/spelling), and
the seal's generic "lowers code into the AST" reading. The position where `@name` appears already
tells the parser which grammar to re-parse the fragment in; `@Type()` in `[]@Type()` is exactly a
type-position splice. Opt-3's decl-tag is the fallback if resolver ambiguity ever bites.

### 5.2 Aggregate/scalar `literal_of` strategy (GAP #1) — OPEN

`literal_of` must gain CVBool/CVFloat/CVBytes/CVAgg (§B2). `@crc_table` is a 256-entry aggregate.

- **Opt-1 — direct structural inverse (all kinds inline as AST literals).** CVBool→`TBool`,
  CVFloat→`TNumber(float)`, CVBytes→`TStrLit`, CVAgg→`TArrayLit`/`TStructLit` recursing per
  element. Simplest, reuses existing `TExprKind` constructors; but a 256-entry table becomes a
  256-node `TArrayLit` in the AST (heavy, but deterministic).
- **Opt-2 — aggregates to rodata (const-symbol) always.** Any CVAgg is emitted as a
  `lower_const` rodata symbol (stable key, `const_rodata_symbol`) and `literal_of` returns a
  reference. Small footprint, byte-identical; but scalars still need Opt-1, and a 2-element
  `[]FieldInfo` to rodata is overkill.
- **Opt-3 — hybrid by size threshold — RECOMMENDED.** Scalars + small aggregates inline (Opt-1);
  aggregates above a fixed element/byte threshold go to rodata (Opt-2). `@sum`/`@typename`/small
  `@fields` inline; `@crc_table` rides rodata. The threshold is a pure constant ⇒ deterministic
  fixpoint.

**Recommendation: Opt-3 (hybrid).** Law-first: keeps the small-case honest and readable while not
ballooning the AST for `@crc_table`; the rodata path already exists in `lower_const` with the
stable-key determinism the fixpoint needs. Ship Opt-1's scalar cases in B2; add the CVAgg rodata
threshold in the same crumb (both are `literal_of` internals, one gate).

### 5.3 `@fields<T>()` descriptor shape (the `.type` question) — OPEN

Serialization dispatches rendering by each field's static type at the SYNTHESIZED access
(`v.<name>`), so `@fields` need not carry a full reflective `Type` value.

- **Opt-1 — descriptors only (`name`, `is_nullable`, `vis`) — RECOMMENDED (v1).** The synthesis
  reads names + nullability; `v.<name>`'s type is resolved by the checker at synthesis. No
  `Type`-as-comptime-value surface needed. Minimal M.0 footprint.
- **Opt-2 — add a comptime `TypeId`/`typename` per field.** Lets a macro branch on a field's type
  by name string. Useful later (format-specific type dispatch), but adds a reflection surface not
  yet needed.
- **Opt-3 — full `Type` value per field.** Maximum reflection; largest surface; only if a macro
  must programmatically inspect nested type structure at comptime. Defer.

**Recommendation: Opt-1 for v1**, `tag` added when the backtick parser lands, Opt-2 as a gated
follow-on if a format needs type-name dispatch. Law-first: expose only what the serialization
keystone consumes.

---

## 6. DESIGN-AHEAD — what is BLOCKED vs. buildable now

- **BUILDABLE NOW (no blocked dep):** all of Family A (A0–A3), Family B machinery (B1–B4), the
  aggregate `literal_of` (B2), `@fields<T>()` v1 descriptors over `FieldView` (B5), and the
  UNTAGGED serialization synthesis (B6). The `@Type()` §9.D expansion (A4) is buildable once A3 +
  the §5.1 splice land (its only gate is the seed bump before source adoption).
- **BLOCKED — `global` unqualified reach (§15).** `@sizeof<T>()`/`@fields<T>()`/`@typename<T>()`
  as UNQUALIFIED globals need §15's `global` modifier (absent, §1.6). DESIGN-AHEAD: write the
  three decls with the `global` modifier NOW in the stdlib source behind the seed gate; until §15
  lands they resolve only qualified. The moment §15 closes, they are reachable unqualified with no
  further §14 change. The comptime machinery never blocks on §15.
- **BLOCKED — `FieldInfo.tag` + full tag grammar (serialization-owned).** `parser::Field`
  (`ast.tks:643`) has no `tag`; the backtick Go-tag parser is a SEPARATE serialization crumb.
  DESIGN-AHEAD: `FieldInfo` ships WITHOUT `.tag` in v1 (§5.3-Opt-1); `synthesize_serializers`
  ships its untagged path (field-name keys + `X | null` nullability). When `Field.tag` lands,
  add the `tag` member + the tag-parse branches — the `@fields` projection reads `f.tag` off the
  extended `FieldView` with no machinery change. This is REPORTED as an adjacent dependency, not
  turned into a new issue here.

---

## 7. Dependencies, order, blast-radius, fixtures

**Fixpoint order (leaves→roots).** A0 (lex) → A1 (AST/parse) are inert leaves. A3 (Family A
pre-pass) is deterministic by stable-mangle key. B2 (`literal_of`) → B3 (`expand_comptime`) →
B4/B5 build on the folded engine. B6 (synthesis) memoizes by `(T, format)` with the stable
`serialize_<T>` name — byte-identical. Every pass mirrors `lower_const`/`monomorph`
determinism; no global mutable counters. **Blast-radius:** A0–A1 touch lexer+parser+AST but are
inert (byte-identical corpus output); A3 adds one `frontend_check` pass; B3/B6 add two passes at
`project.tks:353-367`; B2/B4/B5 extend `comptime_fold`/`collect` (additive). The C twins
(checker/codegen/build) are FROZEN — all crumbs are `.tks`-only.

**Inertness contract.** §14 is ADDITIVE: new tokens, nodes, passes. Until the corpus USES
`@`/`macro`/`comptime`, every generation is **byte-identical** — the ritual gates (A3, B3, B5,
B6, seed) each assert the fixpoint. Source adoption (`@Type()`, `@sizeof`, serialization) lands
only AFTER the seed bump.

**Seed bump (crumb S ★).** After A0–B6 land inert, release a seed understanding both families
(D40/D41 module-const discipline). ONLY THEN may `src/**.tks` use `@`/`macro`/`comptime` —
`@Type()` (§9.D migration), `@sizeof`, `json::encode`.

**Fixtures** (native exit codes; never `teko test` here). Reuse plano-macro §8's table
(`macro_a_*`, `macro_b_*`) and ADD the keystone fixtures:

| Fixture | Family | Input | Expect |
|---|---|---|---|
| `macro_a_type_union_9d/` | A | `macro Type() { lowering { (A \| B) } }` + `struct { m: []@Type() }` | ACCEPT — `m` types as `[](A\|B)`; native exit proves a value flows |
| `comptime_b_fields_count/` | B | `@fields<Point>().len` from `main` (Point has 2 fields) | ACCEPT — **exit 2** |
| `comptime_b_fields_nullable/` | B | a `X \| null` field → `is_nullable` returned | ACCEPT — **exit 1** |
| `comptime_b_literal_agg/` | B | `const CRC = @crc_table()`, exit = `CRC[i] & 0xFF` | ACCEPT — exit = table entry (proves CVAgg `literal_of`) |
| `serial_encode_untagged/` | synth | struct → JSON by field name | ACCEPT — exit proves the bytes |
| `serial_encode_nullable/` | synth | `X \| null` field omitted/nulled (§9.D) | ACCEPT — exit proves null handling |
| `comptime_b_fields_runtime_value_rejected/` | B | a `comptime` that tries to read a runtime field VALUE via `@fields` | REJECT — M.0 guardrail diagnostic |

Each new/changed line carries 100% delta coverage (D32) on the native gate.

**Ritual points (★, full gate + fixpoint):** A3 (Family A pre-pass), B3 (Family B pass), B5
(`@fields` reflection Extent-3), B6 (serialization synthesis), S (seed bump).

---

## 8. Law tensions + HALT check

| Tension | Resolution (law-first) |
|---|---|
| **D33 / `TEKO_LEGISLATION.md:607` + `DECISION_LOG.md:320` "no macros / post-1.0"** | Owner override (§14 seal). On landing, amend those references to "superseded for the macro facility by owner ruling" — doc-sync, not a code change here. |
| **Reflection Extent-3 (`T.fields`) was "owner-pending/deferred" in plano-macro §B.2, but serialization REQUIRES it** | RECOMMEND ratifying Extent-3 NOW, GUARDED by the seal: `.fields` feeds comptime VALUES/descriptors only, never a runtime field-value read (§14.2). The guardrail is the seal itself + the `comptime_b_fields_runtime_value_rejected` fixture. This is a decision to CONFIRM, not a genuine tension — the seal already permits it. |
| **M.0 small surface** — `@fields` descriptor shape, reflection extent | Stage: Extent-1 (`size`/`name`) in B4, Extent-3 (`fields`, guarded) in B5; `FieldInfo` = descriptors only (§5.3-Opt-1), `.tag`/type-name deferred. |
| **§15 `global` absent** blocks unqualified `@sizeof`/`@fields` reach | Machinery independent of §15 (§6); write the decls with `global` behind the seed gate; unqualified reach activates when §15 lands. Not a §14 blocker. |
| **`Field.tag` absent** blocks full tag grammar | Serialization-owned backtick parser; §14 ships the untagged path; `.tag` slots in with no machinery change (§6). Reported adjacent, not a new issue. |
| **Fixpoint byte-identity** | Stable-mangle keys (A3), deterministic `literal_of` + rodata stable keys (B2), memoized `serialize_<T>` (B6) — all mirror `lower_const`. |

**HALT check.** No genuine unresolved tension. The one item needing an owner CONFIRMATION (not a
resolution) is ratifying reflection **Extent-3** for the serialization keystone — and the seal
already permits it guarded, so this plan proceeds on the law-first recommendation rather than
halting. **No HALT.**
