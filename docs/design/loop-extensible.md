# Extensible `loop` — frontend recovery (prefix labels + heads + 3-part)

Design doc for issue **#517** (in-wave, 0.3). Recovers a frontend design that
failed to land: labels shipped in the **suffix** position (`loop NAME {}`),
which foreclosed the condition/range head. This doc settles the load-bearing
decision, the parser algorithm, the AST/checker/VM/codegen/LIR work, the
migration, and the ordered crumb sequence.

Ground truth this doc was written against (read before implementing):

- `src/parser/parse_stmt.tks:18-40` — the current suffix-label `loop`/`break`/`continue`.
- `src/parser/ast.tks:114-116` + `src/checker/tast.tks:96-98` — `LoopStmt`/`TLoopStmt` = `{ label, body }`.
- `src/checker/typer.tks:3035-3038` (`type_loop`), `:3365+` (`check_labels`).
- `src/lir/lower.tks:1707-1866` — `lower_loop`, `lower_break`, `lower_continue`, `LoopTargets`, `close_loop_body` (the #382 SSA/block-arg loop mechanism).
- `src/codegen/codegen.tks:5808-5856` (`emit_loop`), `:6046-6068` (break/continue emit).
- `src/vm/vm.tks:3469-3488` (loop/break/continue exec), `:300-308` (defers), `:3500-3508` (`exec_adopt` — the base/pop scope pattern reused for `init`).
- `src/parser/parse_expr.tks:393-397` — `parse_expr_no_struct` (the if/match scrutinee form; **reused** for the while-head so a trailing `{` opens the body, not a struct literal).
- `src/parser/parse_pattern.tks:7-75` — pattern grammar (`_` = `WildcardPattern`, no `as`; `Foo as x`/`i64 as v` needs a **type name**).
- `src/iter/int_iter.tks`, `src/iter/int_terminals.tks` — ITER0: an iterator is a **closure `() -> T?`** (`null` = exhausted); consumers match `i64 as v`/`null`.

---

## 1. THE load-bearing decision

**Chosen: a pure parser-desugar into a `LoopStmt` widened by ONE field
`init: []Statement`, keeping the loop MECHANISM and all five backends'
loop/break/continue lowering UNCHANGED.**

Rejected alternative: a `step`/`head` field the *lowering* interprets (C-`for`
continue-routing in the loop mechanism).

### Why — the continue trap, and how the desugar dodges it

The 3-part and range heads must run their **step before re-checking the
condition** on `continue` (C `for` semantics). A naive `{ B; step }` desugar is
WRONG: the existing primitive routes `continue` to the **top** of the body
(`lower_continue` jumps to `target.head`; codegen bare `continue` = C
`continue;`), so a `continue` in `B` would skip `step`.

Two ways to make it correct:

- **(a) step-field lowering** — thread `step` onto `LoopStmt`; make
  `lower_continue`, `close_loop_body`, and codegen route `continue`/fall-through
  through a step block. This edits the **delicate #382 loop mechanism** in THREE
  backends (VM `run_loop`, LIR `LoopTargets`/`lower_continue`, codegen
  continue-emission).
- **(b) parser-desugar with a first-iteration guard (CHOSEN)** — fold the step
  to the **TOP** of the body behind a `_first` guard, so the existing
  continue-to-top routing runs the step for free:

  ```
  loop { if !_first { <step> }; _first = false; if !<cond> { break }; B }
  ```

  `continue` in `B` → top → runs `<step>` (guard false after iter 1) → re-checks
  `<cond>`. Exactly C-`for`. The loop MECHANISM is never touched.

For the **for-each** heads there is no `_first` at all: the "step" is the
`_it()` pull inside the `match` **subject** at the top of the body, so
`continue` re-pulls (advances) for free.

### Backend impact (explicit)

| Component | (a) step-field | (b) init-field desugar (CHOSEN) |
|---|---|---|
| arm64 / x86-64 / riscv64 / wasm | unchanged (consume LIR) | **unchanged** (consume LIR) |
| LIR `lower.tks` loop mechanism (`LoopTargets`, `lower_continue`, `close_loop_body`) | **changed** (step block, continue routing) | **unchanged** — `lower_loop` only lowers `init` before the existing loop |
| C codegen loop mechanism (`emit_loop` continue/while) | **changed** (cont-label at step, bare `continue`→goto) | **unchanged** — `emit_loop` only wraps a non-empty `init` in a `{ }` scope |
| VM `run_loop` | **changed** (step on continue+fall-through) | **unchanged** — `exec` only runs `init` once in a base/pop scope |

Only the **new** behavior "run `init` once, loop-scoped, before iterating" is
added, and it lives entirely OUTSIDE the loop mechanism. This is the strongly
preferred "backends unchanged" outcome.

Cost: one `bool _first` + one **predictable** branch per iteration for
range/3-part (none for while/for-each). Acceptable; the loop-perf lever is #449.

### Two secondary load-bearing decisions

1. **Type-free for-each element bind** — a new, small, general
   `_ as x` **wildcard-binding pattern** (payload type INFERRED by the checker
   from the subject) lets the PARSER desugar for-each *without knowing the
   element type* (the parser can never write `i64 as v` generically). See §5.4.

2. **Destructure = per-field expansion, NOT B.13** — `loop mut { a; b } in it`
   expands (in the parser) to per-field `mut a = _elem.a; mut b = _elem.b`,
   reusing fully-supported field-access + simple bindings. This **sidesteps the
   incomplete B.13 general-destructure-binding path** (which today no-ops in the
   typer, `typer.tks:2903`, and **panics in the VM**, `vm.tks:3392`). See §5.5
   and the adjacent-findings note.

---

## 2. The parser algorithm

### 2.1 Prefix label — the ONLY label form

`[LABEL:] loop …` where `LABEL ::= [A-Z][A-Z0-9_]*`. At statement position,
`Ident Colon Loop` (three tokens) is a labeled loop. `Ident Colon` NOT followed
by `Loop` is left untouched (falls through to the existing dispatch, which errors
as today). The suffix form `loop NAME {` is **removed**.

Format enforcement lives in the checker's `check_labels` (single source of truth
for all label rules — enclosing + uniqueness already there), using a new lexer
predicate `is_upper_snake`. The parser accepts any `Ident : loop` structurally;
a lowercase/mixed label yields the checker diagnostic `loop label must be
UPPER_SNAKE`.

### 2.2 First-token dispatch after `loop`

```
after = position just past `loop`
  token[after] == '{'        → INFINITE   : parse_block(after)                      → init=[]
  token[after] == 'mut'       → BINDING-HEADED (§2.3)
  otherwise                   → WHILE      : parse_expr_no_struct(after) as <cond>  → init=[], body=[ if !<cond> {break}, ...B ]
```

- `loop ready {}` → `ready` is a **bool cond** (never a label). Zero ambiguity.
- The while-head **reuses `parse_expr_no_struct`** so the trailing `{` opens the
  body, not a struct literal — identical to `if`/`match` scrutinees.
- `mut` is the ONLY binding keyword (owner 2026-07-12). `let`/`const` never start
  a loop head. `ref` (aliasing) is **out of scope — gated on #498** (see §9).

### 2.3 Binding-headed: `in` = range/for-each, `;` = 3-part

After `mut`, parse a loop-head binding `mut <bind-target> [: T] [= <expr>]`
(a dedicated parse — NOT `parse_binding`; the for-each form has **no** `= expr`):

```
<bind-target> ::= NAME                       // SimpleName
                | { NAME (; NAME)* }          // DestructurePattern (reuses parse_field_names, parse_pattern.tks:109)
```

Then the separator decides:

```
next == 'in'          → RANGE or FOR-EACH (§2.4)
next == ';'           → 3-PART   : ';' <bool-expr> ';' <stmt> '{' … (§5.3)
otherwise             → error "expected `in` or `;` after a loop binding"
```

### 2.4 `in <iterable>` — range vs closure-iterator

Teko has **no first-class range expression** (`ExprKind` has no `Range`). So the
head parser handles `..`/`..=` explicitly:

```
lo = parse_expr_no_struct(after `in`)
  token == DotDot   → RANGE (exclusive): hi = parse_expr_no_struct(...)   (§5.2)
  token == DotDotEq → RANGE (inclusive): hi = parse_expr_no_struct(...)   (§5.2)
  otherwise         → CLOSURE-ITERATOR: <lo> is the iterator expression   (§5.4)
```

The parser cannot tell a closure iterator from a raw collection syntactically, so
**any non-range iterable** desugars to the closure-call form; a **raw
collection** then fails typing at `_it()` ("not callable") — that is the
**deferred** raw-collection case (needs IntoIterator + generic `Iterator<T>`;
blocked on the monomorphization gap; owner: "arbitrary iterators, define when
their time comes"). Diagnostic-quality improvement is a micro-decision (§8).

---

## 3. AST changes

Widen the loop node in BOTH the surface and typed AST by ONE field. The label
stays a `str` (now populated from the prefix parse). No new variant, no
for-each payload on the typed node (it is desugared away before typing
completes).

```
// src/parser/ast.tks (replaces line 114)
/**
 * `[LABEL:] loop [<head>] { … }` (M.5, #517). The head is fully desugared by the
 * parser: `init` holds the run-once, LOOP-SCOPED preamble (counter/bound/`_first`
 * for range & 3-part, `_it` for for-each; empty for infinite & while), and `body`
 * already contains the folded condition-break, the guarded step, and/or the
 * closure-iterator `match`. `label` empty (len 0) = unlabeled.
 *
 * @field label the UPPER_SNAKE prefix label, or "" when unlabeled
 * @field init  the loop-scoped preamble, run once before the first iteration; it
 *              does NOT leak past the loop (checker scope / VM base-pop / codegen
 *              brace)
 * @field body  the iteration body (folded head + user statements)
 * @since M.5 (#517 recovers the head; init added by #517)
 */
pub type LoopStmt = struct { label: str; init: []Statement; body: []Statement }

// src/checker/tast.tks (replaces line 96) — same shape, typed
pub type TLoopStmt = struct { label: str; init: []TStatement; body: []TStatement }
```

`BreakStmt`/`ContinueStmt`/`TBreak`/`TContinue` are **unchanged** (`{ label }`).

Widening the struct forces `init = teko::list::empty()` at every constructor and
`init` recursion at every walker (§7, crumb 2). The loop MECHANISM readers
(`emit_loop`, `lower_loop`, VM loop arm) additionally learn to run `init`.

---

## 4. Checker work

All four head forms arrive at the checker as **ordinary statements** (the parser
already folded them), so most checker work comes for free:

- **bool-cond typing** — free: the while/3-part condition is a folded
  `if !<cond> { break }`, and `type_if_stmt` already requires a `bool`
  condition. (Diagnostic wording is if-flavored; the loop-specific message is a
  micro-decision, §8.)
- **iterator/closure wiring** — free: `_it()` is an ordinary call; the checker's
  call typing requires `_it : () -> T?` and yields `T?` for the `match`. A raw
  collection fails here (not callable) — the deferred case.
- **element type inference** — free: `mut x = _v` (simple) / `mut a = _elem.a`
  (destructure) infer from the subject's non-null / field type. No `: T` needed.
- **init scope (must NOT leak)** — `type_loop` types `init` in a child scope,
  then `body` in that scope, then returns the **outer** env (init + body bindings
  dropped). Shape:

  ```
  /**
   * type_loop — type a `[LABEL:] loop [head] { … }`. The head is already desugared
   * (§3): `init` is typed in a LOOP-PRIVATE scope so its bindings (counter/`_it`/…)
   * do NOT leak past the loop, then `body` is typed in that same scope. The env
   * returned is the CALLER's env (loop-local bindings dropped) (#517).
   *
   * @param l the surface loop node (label + init + body)
   * @param env the enclosing type environment
   * @param table the type table
   * @return the typed loop statement paired with the UNCHANGED outer env, or a type error
   */
  fn type_loop(l: parser::LoopStmt, env: Env, table: TypeTable) -> TypedStmt | error {
      let ib = match type_block(l.init, env, table) { TypedBlock as bk => bk; error as e => return e }
      let bb = match type_block(l.body, ib.env, table) { TypedBlock as bk => bk; error as e => return e }
      TypedStmt { node = TLoopStmt { label = l.label; init = ib.stmts; body = bb.stmts }; env = env }
  }
  ```

  (`type_block` already keeps body bindings block-local; scoping `init` the same
  way is the whole change. Confirm `type_block` returns the grown env for
  threading `init` → `body`; if it does not expose it, thread the bindings
  explicitly — an implementation detail, not a design fork.)

- **UPPER_SNAKE enforcement** — in `check_labels`, reject any non-empty loop
  label failing `lexer::is_upper_snake`:

  ```
  /**
   * A loop label must be UPPER_SNAKE (`[A-Z][A-Z0-9_]*`) — the only label form
   * (#517). Enforced alongside the existing enclosing-scope + uniqueness checks.
   *
   * @param label the loop's declared label ("" = unlabeled, always ok)
   * @return true when the label is empty or a valid UPPER_SNAKE identifier
   */
  fn label_format_ok(label: str) -> bool { label.len == 0 || lexer::is_upper_snake(label) }
  ```

---

## 5. Desugar / lowering per head form

All desugars are built by the parser from **ordinary AST nodes** positioned at
the loop token's `line`/`col`. Hygienic temp names use a reserved prefix + the
loop token's position (unique per loop, unwritable by users):
`loop_temp(kind, line, col)` → e.g. `_tk_first_L12C5`, `_tk_hi_L12C5`,
`_tk_it_L12C5`, `_tk_elem_L12C5`.

### 5.1 while — `loop <cond> { B }`

```
init = []
body = [ if !<cond> { break }, ...B ]
```

`continue` → top → re-checks `<cond>`. No `_first` (no step).

### 5.2 range — `loop mut i in a..b { B }` (`..=` uses `<=`)

Endpoints captured **once** (Rust-range semantics — evaluated at loop entry, not
re-evaluated per iteration; the guard reads the captured `_hi`):

```
init = [ mut i = a, mut _hi = b, mut _first = true ]
body = [
  if !_first { i += 1 },          // guarded step at TOP → continue runs it
  _first = false,
  if !(i <  _hi) { break },       // `..=` → (i <= _hi)
  ...B
]
```

Trace `0..3`: yields 0,1,2. `0..=3`: yields 0,1,2,3. `continue` in `B` → top →
`i += 1` (guard false after iter 1) → re-check → correct. `i` is a normal `mut`
(rebindable in `B`). The bind-target must be a `SimpleName` for a range; a
`{…}` destructure on a range element (`i64`, no fields) is a type error
(acceptable; §8 micro-decision).

### 5.3 3-part — `loop mut i = a ; <cond> ; <step> { B }`

```
init = [ mut i = a, mut _first = true ]
body = [
  if !_first { <step> },          // user's 3rd part, guarded at TOP
  _first = false,
  if !<cond> { break },           // user's 2nd part, RE-EVALUATED each iteration
  ...B
]
```

`<step>` is any statement (`i++`, `i += 2`, `p = p.next`, …). `<cond>` is
re-evaluated each iteration (C-`for`), unlike the range bound (captured once).

### 5.4 for-each closure-iterator — `loop mut x in <it>` (`<it> : () -> T?`)

Requires the **new `_ as x` wildcard-binding pattern** (§5.4.1). `mut` = a local
**COPY** (owner 2026-07-12) — NO write-back logic; class elements share their
object naturally (the copied handle points at the same arena object), struct
elements copy:

```
init = [ mut _it = <it> ]
body = [
  match _it() {                   // pull at TOP → continue re-pulls (advances)
    null   => break
    _ as _v => { mut x = _v; ...B }   // _v = arm bind (immutable); `mut x` = the mutable loop-local copy
  }
]
```

`continue` in `B` → top → `_it()` re-pulls → advances. No `_first`. `x` is a
`mut` local: `x = e` rebinds the copy (never writes back); `x.f = e` writes the
copy's field, which reaches the shared object only when `T` is a class.

#### 5.4.1 The `_ as x` pattern (new, general, small)

`_ as x` binds the matched value to `x`; the type is the subject's type with the
already-matched `null` stripped. On `match opt { null => …; _ as x => … }` with
`opt : T?`, `x : T`. General beyond loops (bind-any). Touch points:

- **parser** `parse_pattern_primary` (`parse_pattern.tks:10`): when `_` is
  followed by `As`, produce a `BindPattern` with an EMPTY `type_name` (wildcard)
  + `has_binding = true`.
- **checker** match-arm typing: a wildcard-bind arm binds `x` to `subject-minus-
  matched-cases` (after a `null` arm on `T?` that is `T`; on a plain `T` it is
  `T`).
- **VM** match exec: bind `x` to the value (optional payload if non-null) — reuse
  the existing `T as x` payload extraction, minus the tag check.
- **codegen** + **tkb codec**: extract the payload with the checker-annotated
  type; serialize the empty-type-name bind.

### 5.5 for-each destructure — `loop mut { index; value } in <it>`

Expand (in the parser) to per-field `mut` bindings via field access — reuses
fully-supported machinery, **not B.13**:

```
init = [ mut _it = <it> ]
body = [
  match _it() {
    null    => break
    _ as _elem => { mut index = _elem.index; mut value = _elem.value; ...B }
  }
]
```

Each field type is inferred; an unknown field name (`_elem.nope`) is the
existing "no such field" type error. Iterator elements are named-field structs
(`enumerate → IntIndexed { index; value }`, `zip → IntPair { first; second }`),
so destructuring is by field name. Partial destructure (a subset of fields) is
allowed and harmless.

---

## 6. Migration + byte-identity re-baseline

### 6.1 Corpus (suffix → UPPER_SNAKE prefix)

| Site | Change |
|---|---|
| `examples/regressions/adopt_break_unknown_label/src/lib.tks` | `loop outer {` → `OUTER: loop {`; `break outer` → `break OUTER`; `break nosuchlabel` → `break NOSUCHLABEL` (still unknown → still compile-fails, now via the enclosing-check not the format-check) |
| `src/emit/tkb_test.tkt:240-260` | label string `"outer"` → `"OUTER"` (×3 sites); add `init = teko::list::empty()` to the `TLoopStmt {…}` at :247 |
| `src/lir/lower_test.tkt:95-96, 1362-1372` | `lwt_loop` ctor gains `init = teko::list::empty()`; labels `"outer"` → `"OUTER"` |
| `src/vm/vm_test.tkt` | any `TLoopStmt {…}` ctor gains `init = teko::list::empty()`; labels → UPPER_SNAKE (audit; the listed defer-loop fixtures build unlabeled loops) |
| `src/checker/{checker,spine}_test.tkt`, `src/codegen/codegen_test.tkt` | every `TLoopStmt {…}` / `LoopStmt {…}` ctor gains `init = teko::list::empty()` |
| `src/parser/parser_test.tkt:862-877, 1289` | update loop error tests (`loop x` now = while-head with no `{` → new message); add head-form + prefix-label acceptance/rejection cases |

Constructor-widening sites (must all land in ONE compile, crumb 2): typer 3037,
monomorph 766, codegen 7628, tkb_read 522, + the test ctors above.

### 6.2 Byte-identity

- **Empty-head `loop {}` (labeled or not) stays byte-identical**: `emit_loop`
  emits NO wrapper when `init` is empty → identical C; `lower_loop` lowers no
  init → identical LIR → identical machine code. This is a regression guard.
- **The label rename changes generated gotos** (`tk_lbl_outer_break` →
  `tk_lbl_OUTER_break`): re-baseline any codegen/emit assertion referencing
  `tk_lbl_outer_*`.
- **The self-host fixpoint re-baselines** because the compiler SOURCE grows (new
  parser/checker/desugar code) — gen1==gen2 must re-establish at every crumb
  (determinism, always required). Note: the compiler `src/` uses **no** labeled
  or head-form loops, so none of the compiler's own generated C changes from the
  feature itself; the fixpoint moves only because new source is added.
- **.tkb**: adding `init` to the TLoopStmt wire record changes `.tkb` bytes, but
  `.tkb` is not a fixpoint artifact; the `tkb_test` roundtrip (write→read) is the
  guard and covers empty `init`.

### 6.3 Seed sequencing (stable-seed lane)

The implementation is ordinary Teko parsed by the current beta seed; the seed
only needs to LEX `in`/`..`/`..=`/`;` (all already lexed) and parse the
implementation code (no new syntax used in `src/`). New-syntax fixtures are
DATA strings fed to the built compiler at test time, and the `examples/`
regression is compiled by the freshly-built `teko` (never the seed). So this
issue introduces **no seed-lane hazard**. `src/` may dogfood the new head forms
only after #517 merges and a new seed is cut (out of scope here).

---

## 7. Ordered crumb sequence

Each crumb is one commit, independently gate-able, names its ritual leg and its
proving fixture. New/changed code owes **100% coverage** (measured natively,
`teko . -o bin --coverage`).

**Crumb 1 — lexer: `is_upper_snake`.**
Add `pub fn is_upper_snake(text: str) -> bool` (`[A-Z][A-Z0-9_]*`; empty → false).
Ritual: unit (`lexer_test`). Fixture: truth table — `OUTER`✓, `A`✓, `A0_B9`✓,
`outer`✗, `_X`✗, `0A`✗, `A-B`✗, ``✗.

**Crumb 2 — AST widening (`init`), behavior-preserving.**
Add `init: []Statement` / `[]TStatement` to `LoopStmt`/`TLoopStmt`; set
`init = teko::list::empty()` at ALL ctors (typer, monomorph, codegen-lift,
tkb_read, every test ctor); tkb frame/write/read carry `init`; every walker
recurses `l.init` (escape ×3, initanalysis ×2, spine ×5, monomorph, reachability,
codegen collect/lift). VM/codegen/LIR loop readers IGNORE the (always-empty)
`init` here → **byte-identical**. Ritual: FULL gate + self-host fixpoint +
byte-identity of existing loops. Fixture: `tkb` roundtrip asserts `init` survives
(empty); the whole existing suite is the regression. **Ritual point.**

**Crumb 3 — prefix labels + while + infinite; drop suffix; migrate.**
Parser: recognize `Ident : loop`; dispatch `{`→infinite, else→while-fold
(`parse_expr_no_struct`); `mut`→temporary honest error `loop binding forms land
in a later step`. Remove suffix-label parsing. Checker: `check_labels` enforces
UPPER_SNAKE. **Migration** (§6.1): rename all label fixtures; migrate the adopt
example; update `parser_test`. Ritual: FULL gate + fixpoint re-baseline (goto
labels). Fixtures (VM + native): `OUTER: loop { break OUTER }` runs; `loop i < n {}`
counts to exit code n; `loop ready {}` treats `ready` as bool; **compile-fail**:
`outer: loop {}` (lowercase label), adopt example on `NOSUCHLABEL`. **Ritual point.**

**Crumb 4 — range (`loop mut i in a..b` / `a..=b`) — brings `init` online.**
Parser: loop-head binding + `in` + range; desugar §5.2. First non-empty `init`:
add VM run-init (base/pop, modeled on `exec_adopt`), codegen `{ }` wrapper for
non-empty `init`, LIR `lower_loop` lowering `init` before the loop. Ritual: FULL
gate. Fixtures (VM + native, exit codes): `loop mut i in 0..3 { s += i }` → 3;
`0..=3` → 6; a `continue`-skips-evens loop proves `continue` runs the step;
**compile-fail**: reading `i` after the loop (non-leak); endpoint captured once
(mutating `n` in the body does not extend the range). **Ritual point.**

**Crumb 5 — 3-part (`loop mut i = a; cond; step {}`).**
Parser: binding + `;` cond `;` step; desugar §5.3. Reuses crumb-4 machinery.
Ritual: FULL gate. Fixtures (VM + native): `loop mut i = 0; i < 4; i++ { s += i }`
→ 6; a `continue` loop proves the step still runs; **compile-fail**: non-bool
cond, reading `i` after the loop.

**Crumb 6 — `_ as x` wildcard-binding pattern.**
Parser (`_ as name`), checker (subject-minus-null bind), VM (bind payload),
codegen (payload extract), tkb codec. Ritual: FULL gate. Fixture (VM + native):
`match some_opt { null => 0; _ as x => x }` binds the payload; `match plainval {
_ as x => x }` binds the whole value. **Ritual point.**

**Crumb 7 — for-each closure-iterator (simple `mut x`).**
Parser: non-range `in` → closure desugar §5.4. Ritual: FULL gate. Fixtures
(VM + native): `loop mut x in range(rc) { s += x }` over `0..3` → 3;
`continue` re-advances; a raw-collection iterable is a clean **type error** at
`_it()` (deferred case). Confirm the `mut` copy has no write-back (rebinding `x`
does not change the source).

**Crumb 8 — for-each destructure (`mut { a; b }`).**
Parser: destructure target → per-field `mut` bindings §5.5. Ritual: FULL gate.
Fixtures (VM + native): `loop mut { index; value } in enumerate(range(rc)) { … }`
binds both fields with inferred types; **compile-fail**: destructuring an unknown
field. Verify class-element field-write reaches the shared object; struct-element
field-write does not.

**Crumb 9 — final fixpoint re-baseline + coverage sweep.**
Re-establish gen1==gen2, confirm 100% coverage on all new/changed code
(justify any unreachable arm in the PR), full ritual gate. **Ritual point.**

Ritual points (where the FULL gate + fixpoint must pass): crumbs 2, 3, 4, 6, 9
(and every crumb runs the native test gate; crumbs 5, 7, 8 are additive parser
desugars proven by their fixtures).

---

## 8. Micro-decisions for owner ratification (defaults proposed — not blocking)

1. **for-each binding keyword** — required `mut` (never bare). **Default:
   required `mut`** (matches the ratified grammar; `ref` rides #498).
2. **Range stride** — `loop mut i in 0..n` steps by 1 only; no `step k`.
   **Default: unit stride for #517**; a `.. by k` / `step k` head is a named
   follow-on if wanted.
3. **`const` as a loop binding** — **Default: rejected** (the loop advances the
   var; only `mut` and — later — `ref`).
4. **Range endpoint evaluation** — **Default: captured once** at loop entry
   (`mut _hi = b`), Rust-range semantics; 3-part `<cond>` is re-evaluated each
   iteration (C-`for`).
5. **Destructure on a range element** (`loop mut { x } in 0..n`) — **Default:
   left to the type error** ("no field `x` on i64"); optional earlier parser
   diagnostic later.
6. **while-cond diagnostic wording** — a non-bool while-head reports the folded
   if-condition message. **Default: accept for #517**; tag synthesized
   loop-conditions for a loop-specific message as a polish follow-on.
7. **Raw-collection for-each diagnostic** — currently "not callable" at `_it()`.
   **Default: accept**; a targeted "raw collections are not yet iterable — pass a
   closure iterator `() -> T?`" message is a polish follow-on.
8. **UPPER_SNAKE enforcement site** — **Default: checker `check_labels`** (single
   source of truth) with the lexer predicate; a parser-side early diagnostic is
   the alternative (better position, but splits label rules).

---

## 9. Explicitly out of scope (named follow-ons)

- **`ref` for-each** (`loop ref x in xs`) — aliasing / write-back binding. `ref`
  is not a keyword/token yet; it is the core of **#498** (transparent `Ref`,
  design-doc-only, 0.3.1). #517 ships the `mut` (copy) forms; the `ref` for-each
  rides #498. Do **not** build a loop-only partial `ref`.
- **Raw-collection for-each** (`loop mut x in list`) — needs an
  IntoIterator/`.iter()` bridge + a usable generic `Iterator<T>` value, both
  blocked on the monomorphization gap. Desugars to the closure-call form and
  fails typing today; the bridge is a separate follow-on.

## 10. Adjacent findings (REPORTED, not turned into scope)

- **B.13 general destructure-binding is incomplete.** Standalone
  `mut { x; y } = <expr>` no-ops in the typer (`typer.tks:2903` returns env
  unchanged — names never bound) and **PANICS in the VM** (`vm.tks:3392`
  "destructuring binding not yet supported"); codegen yields an empty name
  (`codegen.tks:773`). #517's for-each destructure routes AROUND this via
  per-field field-access expansion (§5.5), so #517 does not depend on it.
  Completing B.13 as a first-class binding (nested patterns, variant destructure,
  non-struct RHS) is a separate follow-on for the owner to sequence.
