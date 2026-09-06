# §14 — Parametric type-macros (comptime `if`-select), design v1

> **Status:** DESIGN. PASSO A of the owner's ruling: this document + the crumb-plan are the artefact to
> REVIEW before any build. No `.tks` edited, no build, no reseed until the owner approves. Companion:
> `section14-macro-comptime-impl-v1.md` (A0–A4 / B1–B5, the engine this extends),
> `plano-9d-capabilities-crumbs-r1.md` (the §9.D sweep that consumes this feature).
>
> **Scope (owner-reduced):** NO loops, NO `text::join`, NO general `${string}`→type. The feature is
> exactly **type-macro parameters + a comptime `if` over the bool param**.

## 0. Owner ruling — three macro families (text→AST→value)

The macro system is THREE families (revises the older "no third class"), split by the pipeline stage
each expands at:
- **A · `macro`** — expands the **AST** via `lowering` (post-lexer, pre-type-check). **Form 2 (this
  wave) lives here.**
- **B · `comptime`** — a function EXECUTED without expanding the AST; returns values inlined at the
  call site (B1–B5).
- **C · `generator`** — expands BEFORE the lexer, manipulating the **source STRING**. **Form 1**
  (`var ns = if … ; lowering { ${ns}B }` — string concat / `${string}` name-prefix) does NOT die; it
  MOVES to this new family C (pre-lexer), FUTURE work, out of `macro`'s scope. Recorded so it is not
  lost.

## 1. Goal

Let a Family-A **type-macro** take a bool parameter and select its lowered type with a compile-time
`if`, so the §9.D sweep's remaining 14 cross-namespace variants (incl. the 6 roots) are authored as:

```teko
/** Type — the checker's resolved-type union (§9.D sweep). */
macro Type(local: bool = false) {
    if local { lowering { Named | Prim | Slice } }
    else     { lowering { checker::Named | checker::Prim | checker::Slice } }
}
```

`@Type()` ⇒ `local=false` ⇒ the `else` branch ⇒ the **qualified** union
`checker::Named | checker::Prim | checker::Slice`, which the checker self-qualification fix (`check_named`)
resolves at BOTH same- and cross-namespace use sites. `@Type(true)` ⇒ the bare union (escape hatch for a
rare same-file bare spot). Semantics: `if !local` ⇒ default qualified, cross-ns-safe at every site — no
self-`use` anywhere.

## 2. Form chosen: Form 2 (comptime `if`-select), and why (least engine delta)

The owner offered two forms; **Form 2 is the smaller engine delta**:

| | Form 1 (`var ns = if … ; lowering { ${ns}B }`) | **Form 2 (`if local { lowering{…} } else { lowering{…} }`)** |
|---|---|---|
| params | yes | yes |
| comptime eval | a `var` bound to an `if`-expr yielding a **string** | just a bool **condition** (`local` / `!local`) |
| splice | `${string}` glued as a name **prefix** → re-lex/re-parse | **none** — branches are LITERAL `lowering{}` blocks |
| lexer reentrancy | yes (tokenize the prefix string, glue to ident) | **no** |
| authoring | members written 1× | members written 2× (bare + qualified) |

Form 2 needs **no string values, no `${}` substitution, and no re-lexing** — only (1) type-macro params
and (2) relaxing "the body is exactly one `lowering{}`" to "the body is one `lowering{}` **or** one
comptime `if/else` whose branches are `lowering{}` blocks", picking the branch by the bool condition. The
2× authoring cost is the trade for the far smaller motor. **Recommendation: Form 2.**

No B3 needed: the condition's only free name is a bool param bound to a bool literal, so a ~10-line
bool-expr reader (BoolLit / param `Var` / unary `!`) evaluates it — `eval_const` (typed, post-check) is
neither reachable in this pre-check phase nor required here.

## 3. Crumb sequence (each additive + INERT + fixpoint gen1==gen2==gen3, guarded)

**PT1 — Typed, defaulted macro parameters.**
- `parse_macro_params` (`src/parser/parse_decl.tks`) forces every macro param untyped, no default. Extend
  it to accept an OPTIONAL `: <type>` and `= <default-expr>` (the `Param` fields `has_type`/`type_ann`,
  `has_default`/`default_expr` already exist); a bare untyped param stays legal (value macros unaffected).
- `expand_macro_type` (`src/parser/macro_expand.tks`) ignores `mt.args` today. Bind the (single, bool)
  param → the call's `mt.args[0]` when present, else the param's `default_expr`. The bound value is a
  `BoolLit` node; no evaluation beyond reading the literal.
- INERT: no `src/` type-macro declares a param yet.

**PT2 — Comptime `if`-select over lowering blocks.**
- Replace `single_lowering_frag(md)` with `select_lowering_frag(md, param_binds)`:
  - body `[LoweringFrag]` → that frag (today's behaviour, unchanged).
  - body `[<if-statement>]` → read the condition with `macro_cond_bool(cond, param_binds)` (BoolLit /
    param `Var` / unary `!`; anything else is a located "a type-macro `if` condition must be a bool
    parameter or its negation" error), then recurse into the chosen branch's statement list and select ITS
    `LoweringFrag`. (Guarded by a small nesting cap; the branch must itself resolve to exactly one
    lowering.)
- `frag_has_hole` / the `${}` rejection in `expand_macro_type` is UNCHANGED — Form 2 introduces no holes.
- INERT: no `src/` macro body is an `if` yet.

**PT3 — RESEED (inert).** Re-harvest `bootstrap/teko.c` while PT1–PT2 are inert, via the SAME C-route
self-reproduction just proven (cc the emitted teko.c → rebuild → byte-identical; native gen1==gen2==gen3;
`provenance_gate.sh` PASS). Done BEFORE any parametric use enters `src/`, so CI's C lane always has a
capable seed — the exact lesson from the §14 reseed that motivated this feature.

**PT4+ — Author the roots + finish the sweep.** Only now rewrite the 14 remaining variants as `if !local`
macros (bare branch + qualified `else`). Complete 15/32 → 32/32, one root per fixpointed increment
(leaves→roots). Attempt `Decl` (#3): `Parsed<@Decl()>` is a generic over an inline union — if the union
type-arg mangle surfaces the `cg_opt_mangle_texpr` buffer-twin gap, fix it in scope (mirror the `_str`
twin, `SliceType`+`UnionType`); if the monomorph/byte-agreement shape genuinely breaks, revert and report.

## 4. Signatures added / touched (Form 2)

- `parse_macro_params` (extend): parse optional `: <type>` and `= <default>` per param.
- `fn macro_cond_bool(cond: parser::Expr, binds: MacroParamBinds): bool | error` — new, in
  `src/parser/macro_expand.tks`: evaluate a type-macro `if` condition (BoolLit / bool-param `Var` /
  unary `!`).
- `type MacroParamBinds = struct { names: []str; vals: []bool }` — new: the bound bool params for one
  expansion (A4's type macros are parameterless today, so this starts empty and stays empty until PT4).
- `fn select_lowering_frag(md: MacroDecl, binds: MacroParamBinds): LoweringFrag | error` — replaces
  `single_lowering_frag`; the `[LoweringFrag]` path is byte-identical to today.
- `expand_macro_type` (touch): build `MacroParamBinds` from `mt.args`/defaults, call
  `select_lowering_frag`. The value-macro path (`expand_macro_call`) is untouched.

## 5. Byte-identical-inert strategy

Every crumb is additive and exercised by NOTHING in `src/` until PT4: an existing parameterless type-macro
with a lone `lowering{}` body takes the unchanged path, so PT1–PT2 emit an identical `teko.c` — fixpoint
gen1==gen2==gen3 per crumb. The reseed (PT3) lands while inert. Only PT4 introduces the first `@X(...)` /
`if`-bodied macro, and each such retirement is its own fixpointed increment. Guard: every build
`( ulimit -v 6291456 )`.

## 6. Risks / notes for the owner

1. **Trade-off recorded (not a blocker).** With the `check_named` self-qualification fix, a NON-parametric
   `macro X() { lowering { ns::A | ns::B } }` already resolves at every site (proven, TFSpecKind, 15/32).
   Form 2's marginal value is the `local` toggle (a bare-members escape hatch); members are written 2×, so
   it is not DRY. You have ruled to build it — recorded so the cost/benefit is explicit.
2. **Condition domain.** Proposed: `BoolLit`, a bool-param `Var`, and unary `!` only. Enough for
   `if local` / `if !local`. Broader bool logic (`&&`/`||`/`==`) can be added later if a root needs it; I
   propose starting minimal. Confirm.
3. **Reseed cadence (PT3).** Confirm the reseed lands while inert, before PT4 — mirroring the §14 reseed
   just applied — so the C lane never breaks on a parametric use.

**No law tension:** Teko-only (`teko_rt.*` frozen); W15/Javadoc on every new declaration; each crumb
additive/inert with a byte-identical fixpoint; the reseed precedes any parametric use. Nothing here needs
B3, loops, strings, or `${}`.
