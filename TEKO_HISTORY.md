# Teko — History (was → is → why) & Canonical Examples

> **Audience:** AI agents (and humans) implementing Teko. **Purpose:** this is the **memory** — it
> records *how* each decision was reached (was → is → why), what was explored and rejected, and the
> trail of redefinitions. It explains *why* Teko is the way it is so that an agent does not re-open
> settled choices or reintroduce rejected approaches.
>
> **This document is the third source — it explains, it does not legislate.** The *current norm in
> force* lives in `TEKO_LEGISLATION.md`; the *immutable being* lives in `TEKO_CONSTITUTION.md`. When the
> history's was→is→why and the legislation's distilled rule appear to differ, **the legislation's
> current norm wins** (history may narrate an earlier step on the way to it). History points *up* to the
> legislation; legislation points up to the Constitution; the Constitution points nowhere.
>
> **The library (three documents, three natures):**
> - **`TEKO_CONSTITUTION.md`** — the *essence* (the Laws M.0–M.5 + portico; immutable; only illumination).
> - **`TEKO_LEGISLATION.md`** — the *current norms* (rules in force; mutable under rigorous audit).
> - **This document — `TEKO_HISTORY.md`** — the *memory* (was→is→why; how Teko arrived where it is).
> - **REBOOT_PLAN.md** — the private design scratchpad (authors only).
>
> **Scope note:** "seed" = the minimal C23-bootstrapped subset. "evolution" =
> features deferred until the language is self-hosted and mature. When a decision
> says *evolution*, an agent working on the seed must NOT implement it.
>
> **Note on the Constitution below:** the full Constitution text is reproduced in this document's Part B
> (under "⟡ THE CONSTITUTION ⟡") because its *genesis* is part of the history. The **canonical, governing
> copy is `TEKO_CONSTITUTION.md`** — if the two ever differ, the Constitution document wins. Treat the
> copy here as the historical record of how the Laws were lit, not as a second source of truth.

---

# ⟡ READ FIRST — Teko is governed by a Constitution ⟡

**Before any decision, any implementation, any review: Teko is governed by six laws (M.0–M.5) that
together *are* the language.** They are not style guidelines — they are the being of Teko, a living
sovereign organism. The full Constitution (the portico that explains what the laws are, how they live,
how they relate, the pact that binds those who serve them, and then the laws M.0–M.5 themselves) is in
**Part B, under `⟡ THE CONSTITUTION ⟡`**. Read it before working on Teko; an agent that knows only the
rules and not the framing will apply the letter and miss the law.

**The laws in one line:** ***Teko is metal (M.0), safe (M.1), explicit (M.2), honest (M.3), built in
order (M.4), and austere (M.5).*** They **compose** (each governs a layer of the same decision); the
numeric order only breaks a genuine tie (**lower number wins**).

**The Golden Rule (binds author and agent alike):** *neither the author nor any agent overrules Teko.*
We assay its beauty; we never touch its essence. If a proposed change would alter a law for
convenience, name it as a violation and decline — **even if asked, even by the author.**

**How Teko grows (the three sources of law — full text in the portico, § VI):** the Laws are **fixed
and immutable** — no amendment, ever; the only iteration on a Law is **illumination** (revealing a
nuance already latent in its wording, never changing it). Growth happens in **legislation** (the
decisions/doctrine — it *complements and reinforces* the Laws, and may **never** wound or subvert them;
each cites its governing Law). What is in **neither** the Laws nor the legislation is **not permitted by
omission** — **silence does not authorize**; the un-legislated is audited rigorously. A closed system,
no gaps: **Laws ▷ legislation ▷ audited vacuum**, no lower source overruling a higher one.

## 📜 Documentation convention — decisions must cite the laws that govern them

**This is a binding convention for this document and for all of Teko's evolution.** Every decision,
and every justification, **must reference explicitly the law(s) (M.x) that govern it.** A decision
recorded without naming its governing law is **incomplete** — it states *what* without anchoring *why*
in the Constitution. The convention exists so that:
- the *why* of every choice is traceable to the being of the language, not to taste or convenience;
- a reader (human or LLM) can verify any decision against the law it claims to serve;
- the Constitution stays **alive** — invoked by each new decision — rather than a preamble no one
  consults;
- the future constitutional tooling (§ REBOOT 2.25) can check each decision against its cited law.

*Form:* state the decision, then cite the governing law(s) inline — e.g. "*…therefore `+` does not
concatenate (**M.3** honesty: the operator must not lie; **M.0**: concatenation is alloc+copy, not an
instruction).*" When laws compose or a tie is broken, name the relation (e.g. "**M.0 ≺ M.5** — metal
wins the boundary"). The "Agent rule" closing each entry should make the governing law explicit.

---

## ⚠️ Redefinitions Index — READ BEFORE SLICING THIS DOCUMENT

This document records design *evolution*, so some decisions were **fixed early and
rewritten later**. The was→is→why history is intentionally preserved (it carries the
*why*), but that means a decision stated in an early entry may have been **superseded
by a later one many entries away**.

**Critical for agents that split/chunk this document:** if you process entries in
isolation, you may read a superseded decision as current. Before treating any entry
as authoritative, check this index. Superseded entries also carry a ⚠️ marker at
their top.

| Topic | Early statement | Said | Superseded by | Current state |
|---|---|---|---|---|
| error propagation | B.1 | propagated with a `?` operator | **B.16** | No `?` propagation in the seed; error is **always `match`**. `?` is exclusively nullability (`T?`/`?.`/`??`). |
| Namespace cross-reference | B.9 (original) | cross a namespace by qualifying its **last segment** (`lexer::Token`); never put `teko` in a `.tkp` | **B.9 (re-grounded) / B.32** | **Absolute addressing**: cross by the **full path from the canonical root** (`teko::lexer::Token`); the canonical root *is* the project name (`teko` for the language). `use` is **alias-only** (binds the last segment); **re-export abolished**; the `.tkp` is **TOML** (B.33). |
| Text types (`char`/`byte`) | B.12 | `char` = `byte` = `u8` — one type, refuse the false distinction | **B.36** | `byte` (octet), `u8` (number), and `char` (codepoint) are **distinct** real types (newtypes). A *fixed* `char = [4]byte` is **rejected** (it lies about a *variable* 1–4-byte codepoint); the honest **`char = []byte`** (variable, a zero-copy view into a `str`) keeps the name and is **alpha-native**. `str = []byte` **always-validated** UTF-8 (the codepage, forced from the bootstrap). Bootstrap text = `byte` + `str`; only foreign-codepage transcoding is evolution. |
| Type aliases | pre-B.13 type decision | `type X = Y` gives a free transparent **alias** (`Meters` *is* `i32`) | **B.13** | **Nominal typing**: `type X = Y` is a **distinct** type, no aliases. Transparent newtype, Go rule for ops. |
| `break` in loops | Part A lexer code + early lexer text | postfix `break when cond` | **B.20** (round C.5) | postfix `when` cut; use `if cond { break }`. `when` is now *only* a match guard. |
| Match binding separator | B.15 | `Binary { left, right }` (comma) | **B.26** | Inside `{}` the separator is **newline or `;`, never comma** — `Binary { left; right }`. The "`{}` = newline/`;`, no comma" rule is absolute (struct def, fn body, destructuring, match binding). |
| Struct `to_string`/`parse` & instance methods | B.27 (deferred) | "Inês é morta" — struct conversion only via a loose **free function**; instance methods a larger deferred decision | **B.29** | **Methods resolved.** A function *in* a struct with a bare untyped `self` first arg is an instance method (value-copy, no `ref`, called `.`); without `self`, a static function of the type (called `::`, "static without `static`"). Discouraged but available. No overload/override, fixed arity (default args = evolution). |
| Comparison with mixed types | (undefined — only 3 regimes existed) | arithmetic/bitwise/shift regimes defined; **comparison left unspecified** | **B.22** (4th regime) | Comparison uses a **sign-check-first strategy** (codegen, seed): if the signed side < 0 it's less than any unsigned, else compare at original width. No promotion, **no ceiling** (`u256 vs i256` works). Operators → `bool`; `compare` → `Ordering`. |
| Comparison ceiling / `bigint` | B.22 then B.30 (native) | ceiling was a seed error; `bigint` was briefly made **native** to close it | **B.22 sign-check + B.30** | The sign-check strategy removed the ceiling entirely → `bigint`'s core justification vanished → **`bigint` reverts to a library type** (evolution), no longer native. No ceiling error exists. |
| Three-way comparison result | (n/a) | — | **B.31** | `compare → Ordering`, an **`enum`** `{ Less = -1; Equal = 0; Greater = 1 }` (backed by `i8`) — not a `variant` (too heavy for three trivial states) and not raw `i8` (magic value). Light + named + metal. |
| String prefixes (seed scope) | B.5 (four combinable prefixes + `$`-count trick) | `"`, `$`, `@`, `"""` all combinable; `$`-count set interpolation depth | **B.5 (M.5 audience)** | Seed keeps **only** `"` (delimiter) + `$"..."` **one-level** interpolation (literal brace = `{{`). **`$`-count trick REMOVED** as bloat (M.5: aesthetic, not weight). **Raw `@"..."` and multi-line `"""..."""` → evolution** (rare in bare-metal seed). |
| Closures (what's banned, what returns) | B.10 (closures out; fn-pointers deferred) | justified by cost + concurrency hazard | **B.10 (M.3 audience)** | **Magic closures** (implicit scope capture) banned **primarily by M.3** — they *lie* (a `(code,state)` struct disguised as a function); cost/hazard are symptoms. The **capability** returns via **three honest forms** by state-origin: **function pointer** (no state), **`use`** (caller-local, user-declared, keyword), **`inject`** (DI — an **auxiliary footnote** that *contextualizes* the signature, after `-> return`, before body; not part of the signature, not a directive; compiler resolves via a traceable binding). **Lifetimes `#singleton`/`#scoped`/`#transient` are `#` directives** (the *title* — compile-time allocation strategy → arena). Three layers: title (directive/composition), sentence (signature/caller-contract), footnote (`inject`/injection context). The word is `inject` not `with`/`injects`. All evolution. |

| Numeric conversions (`to`/`as`) | early conversion rule + E.1 example | **forbid every lossy conversion at compile time** (`i32 as i8` = compile error; "silent loss is a design-fault bug"), justified as "M.1 modulating M.0, same as opaque pointers" | **conversions block (§5456)** — M.1 "silently" + M.0 + §II homeostasis (five-judge tribunal) | **Any DEFINED numeric→numeric conversion is ALLOWED** incl. narrowing/sign (`i32 to i8`, `i32 to u32`); loss is **caught, never silent** — constant-out-of-range = **compile error**, runtime = **PANIC on impossibility** (the value doesn't fit; debug AND release — parity with ÷0/OOB, ↺ refined from the earlier "defined-release" overflow-parity). `bool`↔num and non-numeric = **error** (undefined). Cast spelled **`to`** (renamed from `as`). The opaque-pointer analogy fails (loss is *reducible* by a guard). |
| `AltPattern` axis (`\|`) | A.14 | **Alt = value axis only** (literals/ranges); parse dispatched axis-exclusively (Ident-led → variant pattern, no `\|` branch), so `RED \| GREEN` was inexpressible | **C7 Alt-axis block** — tribunal verdict + legislator (chose B) | **An `Alt` option may be a value pattern OR a bare variant case** (`BindPattern`, `has_binding=false`): `RED \| GREEN` is legal and **counts toward variant-axis exhaustiveness** (C7b expands it). **Bindings inside an `Alt` are forbidden** (`Foo as x \| Bar`, `FieldPattern` in `\|`) → error. The current SOURCE parser (which already builds variant-case Alts) is ratified; the canonical axis-exclusive parse is retired. Law basis **M.0 + M.2 + M.5** (with **M.1** kept by Alt-expansion + forbid-bindings). |
| `Unit` / "value-less" type | `TEKO_CHECKER.md` drift (`type Unit = struct {}`, `Type = … \| Unit`, `-> Unit`) | a value-less **type** (empty struct) used as a variant member and "no value" return/binding | **B.37** | **`Unit` ceases to exist.** "Returns no value" is **`-> void`**, a return **marker** — never a type, value, variant member, or binding. A fallible-no-value fn is **`-> error?`**, never `void \| error`. *(Drift to excise: `TEKO_CHECKER.md:78,81,90,1095,1245–1278`, in code-phases Z1+.)* |
| Error type spelling | B.1 / early entries (`Error`, `teko::Error`, `Valor \| Error`) | the failure case spelled with a **capital** `Error` | **B.37** | **`error` is the NATIVE lowercase type** (already in `src/core.tks`), superseding `Error`/`teko::Error`/`Valor \| Error`. Fallible-with-value = `T \| error`; fallible-no-value = `error?`. Variant members are **complete declared types** with **no constructors** and **no nullable/`void` member** (`Value? \| i32` illegal → lift to `-> Val?`). |
| Native numeric set + floats | seed inventory (`u8…u64`/`i8…i64`) + the roadmap "float … deferred"; `bigint` a library type (B.30) | the seed named only `u8…u64`/`i8…i64` as live and **deferred floats** (and `dec`); `bigint` was reverted to a **library** type | **B.38** | **The native numeric set is `u8 u16 u32 u64 u128` / `i8 i16 i32 i64 i128` + `f16 f32 f64` + `dec` (512-bit) + `bigint` (arbitrary precision), plus `bool`/`byte`.** **Floats are UN-DEFERRED**; `bigint` becomes a **named native, deferred** type (↺ B.30). **Staging:** Tier 1 (`u128`/`i128` + `f16`/`f32`/`f64`) now; `dec`/`bigint` named-but-deferred (runtime-backed). Float rulings: literal `3.14`/`1.5e3` defaults to **`f64`** (f16/f32 by annotation); float ÷0 **PANICS**; `float↔int` via `to` is **runtime-guarded** (truncate toward zero; doesn't-fit/NaN/∞ → panic). |

*When this index and an individual entry disagree, the index's "Current state" (and
the entry it points to) wins. Keep this index updated whenever a decision is
rewritten.*

---

## Part A — Canonical Code

Complete, paper-runnable Teko. These are the ground truth for *how Teko is
written*. Each grows as the seed compiler grows.

### A.1 — The Lexer (tokenizes a simple arithmetic expression)

Demonstrates: `enum`, `struct`, `match` as expression, `loop` with `if cond { break }`,
`u8` byte literals, `[]u8` slices and indexing, statement-only `++`,
error-as-value (`T | error`), string interpolation (`$"..."`), and **ref-less
state flow** (mutable state local to the caller; pure helpers return new state).

Two files, **same namespace** `lexer` (same directory → aggregated → they see
each other with no `use` and no qualification):

```teko
// src/lexer/token.tks   (namespace 'teko::lexer')

type TokenKind = enum {
    Number
    Ident
    Underscore
    Plus
    Minus
    Star
    Slash
    LParen
    RParen
}

type Token = struct {
    kind: TokenKind
    text: str
}
```

```teko
// src/lexer/lexer.tks   (same namespace 'teko::lexer' — no `use`, bare references)

// A read result: the token produced + where the position stopped.
// (A struct, because Teko has no tuples; it packages result + new state.)
type Scan = struct {
    token: Token
    next: u64
}

// --- predicates (pure, over a single byte) ---

fn is_digit(c: u8) -> bool {
    c >= '0' && c <= '9'
}

fn is_alpha(c: u8) -> bool {
    (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
}

// --- position advance (pure: takes pos, returns the new pos) ---

fn skip_spaces(source: str, pos: u64) -> u64 {
    mut p = pos
    loop {
        if p >= source.len { break }
        if source[p] != ' ' { break }
        p++
    }
    p
}

// --- lexeme reading (pure: returns Scan = token + new pos) ---

// reads digits, allowing `_` as a separator BETWEEN digits (B.28): `1_000`.
// (A trailing `_` or `_` not between digits is not consumed here.)
fn read_number(source: str, pos: u64) -> Scan {
    mut p = pos
    loop {
        if p >= source.len { break }
        let c = source[p]
        // digit → always part of the number
        if is_digit(c) { p++; continue }
        // `_` → separator ONLY if a digit follows (between digits)
        if c == '_' {
            if p + 1 < source.len && is_digit(source[p + 1]) { p++; continue }
        }
        break
    }
    Scan {
        token = Token { kind = TokenKind::Number; text = slice(source, pos, p) }
        next = p
    }
}

// identifier continuation: letter, digit, or underscore (snake_case).
// (Start is handled in the dispatch: a letter starts an ident directly;
//  a `_` starts one only if a letter/digit follows — see tokenize.)
fn is_ident_continue(c: u8) -> bool {
    is_alpha(c) || is_digit(c) || c == '_'
}

fn read_ident(source: str, pos: u64) -> Scan {
    mut p = pos
    loop {
        if p >= source.len { break }
        if !is_ident_continue(source[p]) { break }
        p++
    }
    Scan {
        token = Token { kind = TokenKind::Ident; text = slice(source, pos, p) }
        next = p
    }
}

// A single-character token (operators, parentheses).
fn single(source: str, pos: u64, kind: TokenKind) -> Scan {
    Scan {
        token = Token { kind = kind; text = slice(source, pos, pos + 1) }
        next = pos + 1
    }
}

// A `_` is either the wildcard token or the start of an identifier.
// Per the corrected regex: `_+[A-Za-z0-9]…` is ONE identifier (leading
// underscores included — `_foo`, `__bar`, `_1`). A `_` NOT followed (after
// any run of `_`) by a letter/digit is the wildcard token. This emits ONE
// `_` token; a run like `__` (no letter/digit after) becomes two wildcard
// tokens across two dispatch iterations — the parser then fails naturally
// (no production for consecutive wildcards). Exclusion by construction.
fn read_underscore(source: str, pos: u64) -> Scan {
    // look past the run of underscores
    mut p = pos
    loop {
        if p >= source.len { break }
        if source[p] != '_' { break }
        p++
    }
    // if a letter/digit follows the run, the whole lexeme is an identifier
    if p < source.len {
        if is_alpha(source[p]) || is_digit(source[p]) {
            return read_ident(source, pos)   // reads from the first `_`
        }
    }
    // otherwise this single `_` is the wildcard token (one at a time)
    single(source, pos, TokenKind::Underscore)
}

// --- main loop ---

fn tokenize(source: str) -> []Token | error {
    mut pos: u64 = 0
    mut tokens = teko::list::empty()

    loop {
        pos = skip_spaces(source, pos)
        if pos >= source.len { break }

        let c = source[pos]

        let scan = match c {
            '0'..='9' => read_number(source, pos)
            'a'..='z' => read_ident(source, pos)
            'A'..='Z' => read_ident(source, pos)
            '_'       => read_underscore(source, pos)   // wildcard or ident — see below
            '+'       => single(source, pos, TokenKind::Plus)
            '-'       => single(source, pos, TokenKind::Minus)
            '*'       => single(source, pos, TokenKind::Star)
            '/'       => single(source, pos, TokenKind::Slash)
            '('       => single(source, pos, TokenKind::LParen)
            ')'       => single(source, pos, TokenKind::RParen)
            _         => return error { message = $"unexpected character: {c}" }
        }

        tokens = teko::list::push(tokens, scan.token)
        pos = scan.next
    }

    tokens
}
```

**Notes for implementers:**
- `mut pos` lives in `tokenize` (the caller). Helpers are *pure*: they take
  `(source, pos)` and return `Scan`/`u64`. The caller reassigns (`pos = scan.next`).
  No mutation-at-a-distance, no references. This is how *all* seed-compiler state
  flows (see B.7). **Position/index/width are always `u64`** (non-negative, covers
  any size, avoids `usize`) — so `pos >= source.len` compares `u64 >= u64` (same
  type, no promotion needed).
- `let scan = match c { ... }` — `match` is an **expression**. Most arms yield a
  `Scan`; one arm may **diverge** (`return error`, leaving the whole function)
  instead of yielding a value. The match still has type `Scan` from the
  non-diverging arms.
- `source[p]` out-of-bounds is a **panic**. Correct code *prevents* it (checks
  `p < source.len` first) rather than relying on the panic.
- `source.len` is **field** access (an array is `{ptr, len}`); `.`, no parens.
- `slice(...)` / `teko::list::*` are stdlib; shown for illustration. A
  zero-dependency seed could use a fixed array + counter instead of a growing list.

### A.2 — The Parser (builds an expression AST, applying precedence)

The natural pair of the A.1 lexer: it consumes the token stream A.1 produces
(`Number`, `Plus`, `Minus`, `Star`, `Slash`, `LParen`, `RParen`) and builds an
**expression AST**, applying **operator precedence** (`*`/`/` bind tighter than
`+`/`-`) and **left-associativity**. It demonstrates: the AST as a **`variant` of
declared types** (B.14, B.13 nominal), **recursive types** (B.8 — `Binary` holds
`Expr`), **precedence by recursive descent** (B.23 — one function level per
precedence level, so the precedence is *visible in the call structure*: the
legibility-local facet of M.2), **error-as-value** (B.1 — `Expr | error`, handled
with `match`), and the **ref-less state flow** of A.1/B.7 (the position lives in
the caller; pure helpers return the new position in a struct, B.3).

**The grammar the recursive descent mirrors** (the function structure *is* the
grammar *is* the precedence):
```
expr   = term   (('+' | '-') term)*      — additive level (loosest)
term   = factor (('*' | '/') factor)*    — multiplicative level (tighter)
factor = Number | '(' expr ')'           — atom / parenthesised (tightest)
```
`factor` is reached last (deepest in the call tree) → binds tightest; `expr` is
the entry → binds loosest. You read the precedence in the call hierarchy — no
precedence table of magic numbers (which would hide it). For `1 + 2 * 3` this
yields `Binary(+, 1, Binary(*, 2, 3))` — the `*` grouped first (B.23 correct).

Three files, **same namespace** `teko::parser` (same directory → aggregated → they
see each other with no `use`). The `teko::lexer` namespace is crossed by the
**absolute path from the canonical root** (B.9/B.32); a `use teko::lexer` at the top
binds the last-segment alias `lexer`, so the body reads `lexer::Token`,
`lexer::TokenKind` (the alias is an explicit stand-in, not a relative lookup).

```teko
// src/parser/ast.tks   (namespace 'teko::parser')

// cross into the lexer namespace by its absolute path; `use` binds the last
// segment `lexer` as an alias (B.32) — the body then reads `lexer::Token`.
use teko::lexer

// An expression is either a literal number or a binary operation.
// A `variant` of declared types (B.14): each case is a real, separately
// reachable type. Nominal (B.13): `Number` and `Binary` are what they are.
type Number = struct {
    value: i64
}

// `Binary` holds two `Expr` — a RECURSIVE type (B.8): a tree node contains
// subtrees. The compiler manages the indirection; no exposed pointer, no Box.
type Binary = struct {
    op:    lexer::TokenKind     // Plus | Minus | Star | Slash
    left:  Expr
    right: Expr
}

// The expression AST node: exactly one of the two cases (B.14 — never both,
// never neither; the invalid state is impossible by construction, M.1).
type Expr = Number | Binary
```

```teko
// src/parser/cursor.tks   (same namespace 'teko::parser')

// The parser's state flows ref-lessly (B.7), exactly like the lexer (A.1):
// the mutable position lives in the CALLER; pure helpers take the tokens and a
// position and RETURN the new position. Packaged in a struct because Teko has
// no tuples (B.3).

// The result of parsing a sub-expression: the node produced + where parsing
// stopped. (Mirrors the lexer's `Scan` — node + new position.)
type Parsed = struct {
    node: Expr
    next: u64
}

// --- pure cursor helpers (over the token slice + a position) ---

// Is there a token at `pos`? Position/index are always u64.
fn has_token(tokens: []lexer::Token, pos: u64) -> bool {
    pos < tokens.len
}

// The kind at `pos` (caller guarantees `has_token` first).
fn kind_at(tokens: []lexer::Token, pos: u64) -> lexer::TokenKind {
    tokens[pos].kind
}

// expect: demand that the token at `pos` is of kind `kind`. Returns the position
// AFTER it (advanced by one) on success, or an error (B.1). This is the shared
// helper that flattens the repeated "is there a token? is it the right kind?
// else error" pattern — used wherever a specific token must appear (a `)`, a
// `=`, an Ident). It returns `u64 | error` (the new position, or a reason): an
// honest result (M.3). Calling it with the guard pattern keeps the caller flat.
fn expect(tokens: []lexer::Token, pos: u64, kind: lexer::TokenKind, msg: str) -> u64 | error {
    if !has_token(tokens, pos) { return error { message = msg } }
    if kind_at(tokens, pos) != kind { return error { message = msg } }
    pos + 1
}

// Is the kind at `pos` one of the two additive operators?
fn is_additive(tokens: []lexer::Token, pos: u64) -> bool {
    if !has_token(tokens, pos) { return false }
    let k = tokens[pos].kind
    // `==` on the nominal enum (B.31/B.13); `||` is the symbol operator (B.19)
    k == lexer::TokenKind::Plus || k == lexer::TokenKind::Minus
}

// Is the kind at `pos` one of the two multiplicative operators?
fn is_multiplicative(tokens: []lexer::Token, pos: u64) -> bool {
    if !has_token(tokens, pos) { return false }
    let k = tokens[pos].kind
    k == lexer::TokenKind::Star || k == lexer::TokenKind::Slash
}
```

```teko
// src/parser/parser.tks   (same namespace 'teko::parser')

// Recursive descent: one function per precedence level (B.23). The call order
// expr → term → factor IS the precedence — additive is loosest (outermost),
// the atom is tightest (innermost). Each returns `Parsed | error` (B.1).

// factor = Number | '(' expr ')'        (tightest — the atom)
// Dispatches on the leading token; each case is a small flat function (guard
// over nest — M.2). Renamed conceptually to "atom" (the tightest level).
fn parse_factor(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    if !has_token(tokens, pos) {
        return error { message = "unexpected end of input in factor" }
    }
    match kind_at(tokens, pos) {
        lexer::TokenKind::Number => parse_number_atom(tokens, pos)
        lexer::TokenKind::LParen => parse_paren_atom(tokens, pos)
        _                        => error { message = "expected a number or '('" }
    }
}

// a literal number leaf (text→i64 elided — the seed's numeric reader, B.28)
fn parse_number_atom(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    let n = to_i64(tokens[pos].text)
    Parsed { node = Number { value = n }; next = pos + 1 }
}

// '(' expr ')' — recurse into a full expression, then demand ')'. FLAT: each
// fallible step is a guard line (extract-or-return), the result built at the top
// level — no nesting of the work inside the Parsed arm (guard over nest).
fn parse_paren_atom(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    // pos is '(' ; parse the inner expression (guard: extract or propagate, B.16)
    let inner = match parse_expr(tokens, pos + 1) {
        error as e => return e
        Parsed as p => p
    }
    // demand the closing ')' via expect (guard: the new position or propagate)
    let after = match expect(tokens, inner.next, lexer::TokenKind::RParen, "expected ')'") {
        error as e => return e
        u64 as np  => np
    }
    Parsed { node = inner.node; next = after }
}

// term = factor (('*' | '/') factor)*     (multiplicative — left-associative)
// FLAT (guard over nest): the first operand is a guard line; the loop folds left
// with the rhs extracted by a guard. State flows by reassignment (B.7).
fn parse_term(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    let start = match parse_factor(tokens, pos) {
        error as e => return e
        Parsed as s => s
    }
    mut node = start.node
    mut p    = start.next
    loop {
        if !is_multiplicative(tokens, p) { break }
        let op  = kind_at(tokens, p)
        let r = match parse_factor(tokens, p + 1) {
            error as e => return e
            Parsed as r => r
        }
        node = Binary { op = op; left = node; right = r.node }   // fold left
        p = r.next
    }
    Parsed { node = node; next = p }
}

// expr = term (('+' | '-') term)*         (additive — loosest, left-associative)
fn parse_expr(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    let start = match parse_term(tokens, pos) {
        error as e => return e
        Parsed as s => s
    }
    mut node = start.node
    mut p    = start.next
    loop {
        if !is_additive(tokens, p) { break }
        let op  = kind_at(tokens, p)
        let r = match parse_term(tokens, p + 1) {
            error as e => return e
            Parsed as r => r
        }
        node = Binary { op = op; left = node; right = r.node }
        p = r.next
    }
    Parsed { node = node; next = p }
}

// The entry point: parse a full expression and require that ALL tokens were
// consumed (trailing tokens = error). Returns the root Expr or an error (B.1).
// FLAT: the parse result is a guard line; the trailing-token check is at top level.
fn parse(tokens: []lexer::Token) -> Expr | error {
    let p = match parse_expr(tokens, 0) {
        error as e => return e
        Parsed as p => p
    }
    // every token must be consumed; leftovers mean a malformed expression
    if p.next != tokens.len {
        return error { message = "unexpected trailing tokens" }
    }
    p.node
}
```

**Notes for implementers (and for the C23 port — `src/parser/parser.c`):**
- **The structure IS the precedence (B.23).** `parse_expr` (additive) calls
  `parse_term` (multiplicative) calls `parse_factor` (atom). The deeper a level
  sits in the call tree, the tighter it binds. There is **no precedence table** —
  the precedence is legible in the function hierarchy (the legibility-local facet
  of M.2). The C23 port keeps exactly these three functions, same names.
- **Left-associativity is the fold-left loop.** Each level parses one operand,
  then loops consuming same-level operators, making the *accumulated* node the
  **left** child of each new `Binary`. That yields `(a - b) - c`, not `a - (b - c)`.
- **State flows ref-lessly (B.7), exactly as in A.1.** No parser object with a
  mutable cursor field; the position is passed in and a new position returned
  (`Parsed.next`), and the caller reassigns within a level (`mut p`). This is how
  *all* seed-compiler state flows — the port to C23 honors it by hand (the
  discipline-overhead rule): pass `size_t pos`, return the new one; no hidden
  global cursor (the "soup of global registries" anti-pattern is forbidden).
- **error is a value (B.1), propagated by `match` (B.16 — no `?` in the seed).**
  Each fallible call is matched; an `error` case returns the error up. The C23
  port returns a tagged result (a small struct with an ok/err discriminant), not
  `errno`, not a thrown exception, not a sentinel pointer.
- **Position/index/width are `u64`** (here, the Teko-level `u64`; the C23 port
  uses `size_t`/`uint64_t`), so `pos < tokens.len` compares same-typed values
  (no signed/unsigned mixing — M.1).
- **The AST is a `variant` (B.14), recursive (B.8).** `Expr = Number | Binary`;
  the C23 port realizes it as a tagged union (a `kind` discriminant + the payload),
  with compiler-managed indirection for the recursive `Binary` children — the
  Teko side never exposes a pointer; the C side uses one *internally* but keeps it
  off the public surface in `ast.h` (the `.h` is the visibility boundary).
- **`to_i64(text)`** (the literal's text → integer) is elided here as a leaf
  helper; it follows B.28 (digit separators already stripped by the lexer's
  `read_number`). It belongs to the numeric layer, not the parser's structure.

### A.3 — Declarations: the `let` layer (lexer + parser extended together)

The next layer above the A.1/A.2 expression pair. It introduces the first
**declaration** — `let name = expr` — and with it the notion of a **statement**:
a program stops being a single expression and becomes a **sequence of
statements**. Per M.4, the layer extends the **lexer first** (it must produce the
new tokens) and then the **parser** (which consumes them). Scope is the smallest
complete step: **immutable `let` only** (`mut`/`const` are later variations); the
declaration's value **reuses the A.2 expression parser** unchanged (building on
the complete — M.4).

What it exercises: **keyword recognition** (B.19 — `let` via the keyword table),
**significant newline as terminator** (B.17 — statements end at a newline, `;`
separates inline), **maximal munch** distinguishing `=` from `==` (B.23), and a
**statement-level AST** (`Program` = a sequence of `Statement`; `Statement` =
`Let | ExprStmt`, a `variant` — B.14).

#### Lexer extension (over A.1)

The A.1 lexer gains three things; everything else (numbers, idents, the
arithmetic operators, parentheses) is unchanged.

```teko
// src/lexer/token.tks   (namespace 'teko::lexer') — TokenKind extended

type TokenKind = enum {
    Number
    Ident
    Underscore
    Plus
    Minus
    Star
    Slash
    LParen
    RParen
    Let          // NEW: the keyword `let` (B.19 — a keyword, not an ident)
    Assign       // NEW: `=` (distinct from `==` by maximal munch — B.23)
    Newline      // NEW: a significant newline / `;` — the statement terminator (B.17)
}
```

```teko
// src/lexer/lexer.tks   (namespace 'teko::lexer') — additions only

// Keyword lookup (B.19): read a word as an Ident run, THEN check the table.
// `let` → the Let keyword; anything else stays an Ident. (The lexer reads the
// word first and looks it up — it does not special-case letters mid-scan.)
fn keyword_or_ident(text: str) -> TokenKind {
    // bytes_eq compares a byte slice to a literal (a stdlib leaf, like to_i64)
    if bytes_eq(text, "let") { return TokenKind::Let }
    TokenKind::Ident
}

// read_ident, after reading the run, classifies via the table:
fn read_word(source: str, pos: u64) -> Scan {
    let scan = read_ident(source, pos)            // reuse A.1's run reader
    let kind = keyword_or_ident(scan.token.text)  // then classify (B.19)
    Scan {
        token = Token { kind = kind; text = scan.token.text }
        next  = scan.next
    }
}

// The dispatch (tokenize) gains three arms. `=` needs maximal munch (B.23):
// look ahead — `==` would be a different token; here only single `=` exists in
// this layer, so a lone `=` is Assign. (When `==` enters, this arm checks the
// next byte first — longest match always.)
//
//   '='  → if next byte is '=' → (future Equals); else Assign     (B.23)
//   '\n' → Newline  (significant — B.17)
//   ';'  → Newline  (the inline separator collapses to the same terminator)
//
// And the letter arms now route through `read_word` (classify keyword vs ident)
// instead of `read_ident` directly:
//
//   'a'..='z' => read_word(source, pos)
//   'A'..='Z' => read_word(source, pos)
//
// skip_spaces no longer skips '\n' (the newline is now SIGNIFICANT — B.17): it
// skips only ' ' and '\t', leaving '\n' to be emitted as a Newline token.
```

**Note on significant newline (B.17):** the lexer emits a `Newline` token at each
line break (and at `;`). Consecutive blank lines collapse to a single terminator
at the parser level (empty statements are skipped). This is the seed's
termination model: a statement ends at a newline; `;` is only to put several on
one line — never required at end of line.

#### Parser extension (over A.2)

The expression AST and its three functions (`parse_expr`/`parse_term`/
`parse_factor`) are **unchanged and reused**. The parser gains a statement level
*above* the expression level.

```teko
// src/parser/ast.tks   (namespace 'teko::parser') — statement nodes added

// A `let` binding: a name bound to an expression value (immutable in this layer).
type Let = struct {
    name:  str        // the bound identifier's text
    value: Expr        // the value — a full expression (reuses A.2)
}

// An expression used as a statement (e.g. a bare `1 + 2` on its own line).
type ExprStmt = struct {
    expr: Expr
}

// A statement is exactly one of these (variant — B.14; one case, never both).
type Statement = Let | ExprStmt

// A program is a sequence of statements (B.25 array; separated by Newline).
type Program = struct {
    statements: []Statement
}

// The statement parser has its OWN result type — `node` is a Statement, not an
// Expr. Reusing A.2's `Parsed` (whose `node` is `Expr`) would be a TYPE LIE
// (M.3): an Expr and a Statement are different things, and the checker would
// reject putting a Statement where an Expr is declared (M.1 — the invalid is
// caught, not silently coerced). Distinct node types ⇒ distinct result structs.
type ParsedStmt = struct {
    node: Statement
    next: u64
}
```

```teko
// src/parser/statement.tks   (namespace 'teko::parser') — the statement level

// Skip any run of Newline terminators (blank lines / empty statements).
fn skip_terminators(tokens: []lexer::Token, pos: u64) -> u64 {
    mut p = pos
    loop {
        if !has_token(tokens, p) { break }
        if kind_at(tokens, p) != lexer::TokenKind::Newline { break }
        p++
    }
    p
}

// parse_let: consume `let`, then the name Ident, then `=`, then an expression.
// Each missing piece is an error-as-value (B.1). Returns a ParsedStmt (its node
// is a Statement — the Let case of the variant).
fn parse_let(tokens: []lexer::Token, pos: u64) -> ParsedStmt | error {
    // caller guarantees tokens[pos] is Let; step past it
    mut p = pos + 1

    // the name: an Ident (expect returns the position after it, or error)
    if !has_token(tokens, p) {
        return error { message = "expected a name after 'let'" }
    }
    let name = tokens[p].text
    let p_eq = match expect(tokens, p, lexer::TokenKind::Ident, "expected an identifier after 'let'") {
        error as e => return e
        u64 as np  => np
    }

    // the `=`
    let p_val = match expect(tokens, p_eq, lexer::TokenKind::Assign, "expected '=' after the name") {
        error as e => return e
        u64 as np  => np
    }

    // the value: a full expression (guard: extract the Expr or propagate). We lift
    // that Expr into a Let, and the Let into a Statement via the variant.
    let v = match parse_expr(tokens, p_val) {
        error as e => return e
        Parsed as v => v
    }
    let binding = Let { name = name; value = v.node }   // Let (a Statement case)
    ParsedStmt { node = binding; next = v.next }
}
```

```teko
// src/parser/program.tks   (namespace 'teko::parser') — the top level

// parse_statement: dispatch on the first token. `let` → a binding; otherwise an
// expression statement. (One construct decides, by the leading token — B.15.)
fn parse_statement(tokens: []lexer::Token, pos: u64) -> ParsedStmt | error {
    if !has_token(tokens, pos) {
        return error { message = "expected a statement" }
    }
    match kind_at(tokens, pos) {
        lexer::TokenKind::Let => parse_let(tokens, pos)
        _                     => parse_expr_stmt(tokens, pos)
    }
}

// an expression statement: parse an expression, lift it into ExprStmt (a Statement
// case). FLAT: the expression is a guard line, the wrap is at top level.
fn parse_expr_stmt(tokens: []lexer::Token, pos: u64) -> ParsedStmt | error {
    let p = match parse_expr(tokens, pos) {
        error as e => return e
        Parsed as p => p
    }
    let stmt = ExprStmt { expr = p.node }   // ExprStmt (a Statement case)
    ParsedStmt { node = stmt; next = p.next }
}

// parse_program: the entry point. A loop of statements, each followed by a
// terminator (Newline) or end-of-input. Left-to-right, ref-less state (B.7).
// FLAT: the statement is a guard line; the terminator check is at top level.
fn parse_program(tokens: []lexer::Token) -> Program | error {
    mut stmts = teko::list::empty()
    mut p     = skip_terminators(tokens, 0)    // ignore leading blank lines

    loop {
        if !has_token(tokens, p) { break }     // end of input → done

        let s = match parse_statement(tokens, p) {
            error as e => return e
            Parsed as s => s
        }
        stmts = teko::list::push(stmts, s.node)
        p = s.next

        // after a statement, require a terminator OR end-of-input (B.17)
        if !has_token(tokens, p) { break }     // end of input → done
        if kind_at(tokens, p) != lexer::TokenKind::Newline {
            return error { message = "expected end of line after statement" }
        }
        p = skip_terminators(tokens, p)        // consume the terminator(s)
    }

    Program { statements = stmts }
}
```

**Notes for implementers (and the C23 port):**
- **The layer extends both stages, lexer first (M.4).** The lexer must emit `Let`,
  `Assign`, and `Newline` before the parser can consume them. The C23 port extends
  `src/lexer/lexer.c` (keyword table, the `=` arm with look-ahead, significant
  newline) *then* `src/parser/parser.c` (the statement level) — never the parser
  ahead of the lexer.
- **`let` reuses the expression parser unchanged (M.4 — build on the complete).**
  `parse_let` calls `parse_expr` (A.2) for the value. The whole point of doing
  `let` *now* is that the expression layer beneath it is finished and audited.
- **Significant newline is the terminator (B.17).** `skip_spaces` stops skipping
  `\n`; the lexer emits `Newline`; the parser separates statements by it. `;` maps
  to the same `Newline` terminator (inline separator). A statement never *requires*
  a trailing `;`.
- **`=` vs `==` by maximal munch (B.23).** In this layer only `=` exists, so a lone
  `=` is `Assign`; the dispatch arm is written to look ahead (the longest-match
  rule) so that when `==` enters a later layer, no code changes — the arm already
  checks the next byte first.
- **Statement and expression are distinct AST levels — and distinct result types.**
  `Program` holds `[]Statement`; `Statement = Let | ExprStmt` (a variant — B.14).
  The statement parser returns its **own** `ParsedStmt` (node: `Statement`), *not*
  A.2's `Parsed` (node: `Expr`) — reusing one struct for two node types would be a
  **type lie** (M.3) the checker rejects (M.1: the invalid is caught, not coerced).
  Each level lifts the inner node into its case (`Expr` → `ExprStmt` → `Statement`),
  honestly typed at every step. The C23 port realizes `Statement` as a tagged
  union, `Program` as a growable array (the seed could use a fixed array + counter
  — zero dependencies), and keeps the two result structs distinct.
- **State flows ref-lessly (B.7), error is a value (B.1), no `?` (B.16)** — exactly
  as A.2. `mut p` within `parse_program`, position passed and returned, each
  fallible call matched. The discipline carries unchanged into the C23 port.
- **Out of scope this layer (next layers):** `mut`/`const` bindings, type
  annotations (`let x: i32 = …`), and `fn`/`type` declarations. Each is its own
  smallest-complete extension, lexer-then-parser, under the same norms.

### A.4 — The statement bindings and simple reassignment (`const` / `mut`, type annotations)

A.3 introduced immutable `let`. This layer adds the rest of the **statement
bindings** — `const` and `mut` — plus the orthogonal **type annotation** (`: T`)
and **simple reassignment** of a `mut` (`x = v`). It does **not** "close the
assignment family": assignment turns out to be a **cross-cutting dimension**, not
a single layer (see the note at the end), and most of its forms must wait for the
structures that host them. This layer covers only the forms that rest on what
already exists (identifiers + the A.2 expression parser). Per M.4, the lexer
extends first, then the parser; the value still **reuses A.2 unchanged**.

The roles (B.21): **`const`** = compile-time, immutable, lives in `.rodata`;
**`let`** = runtime binding, immutable once bound; **`mut`** = runtime binding,
mutable. `mut` is on the **variable** and governs the whole value (no per-field
granularity). `mut` is allowed **only on local variables** (which create/own
their value) — a *checker* rule, not a parser rule (the parser records the kind;
the checker applies the arena/ownership constraint). **Writes require `mut`;
reads never do** (B.21).

**Binding vs. reassignment — distinct, honestly (M.3).** `let/mut/const x = v`
*creates* a binding; `x = v` *reassigns* an existing `mut`. Different acts →
different AST cases, told apart by the leading token (a binding keyword → a
binding; an `Ident` then `=` → a reassignment).

> **Compound assignment (`+= -= …`) is deliberately NOT here.** It is sugar for
> `x = x OP y` — it **depends on the operators** that compose it (`+ - * / %`,
> and the bitwise/shift operators `& | ^ << >>`). Those operators are a later
> layer (B.22), not yet built in these examples. Putting `+=` here would build on
> an incomplete operator set (**M.4 violation**); it returns **complete** (all
> arithmetic *and* bitwise/shift compounds) in the layer *after* operators. This
> is the order: **operators → compound → fn.**

#### Lexer extension (over A.3)

```teko
// src/lexer/token.tks   (namespace 'teko::lexer') — TokenKind extended

type TokenKind = enum {
    Number
    Ident
    Underscore
    Plus
    Minus
    Star
    Slash
    LParen
    RParen
    Let
    Assign
    Newline
    Mut          // NEW: the keyword `mut`   (B.21 — mutable local binding)
    Const        // NEW: the keyword `const` (B.21 — compile-time immutable)
    Colon        // NEW: `:` — introduces a type annotation
}
```

```teko
// src/lexer/lexer.tks   (namespace 'teko::lexer') — additions only

// Keyword table grows (B.19): `let`, `mut`, `const` are keywords; else Ident.
fn keyword_or_ident(text: str) -> TokenKind {
    if bytes_eq(text, "let")   { return TokenKind::Let }
    if bytes_eq(text, "mut")   { return TokenKind::Mut }
    if bytes_eq(text, "const") { return TokenKind::Const }
    TokenKind::Ident
}

// The dispatch gains the `:` arm and (already, from A.3) the `=` arm. Note that
// `=` is still emitted by MAXIMAL MUNCH (B.23): the arm is written to look at the
// next byte first, so that when `==` and the compound `+=`/`-=`/… enter with the
// operator layer, no code here changes — longest match always.
//
//   ':' → Colon                 (type annotation)
//   '=' → Assign                (lone `=`; `==` and `+=` arrive with operators)
```

#### Parser extension (over A.3)

The expression layer (A.2) and the `Parsed`/`ParsedStmt` split (A.3) are reused.

```teko
// src/parser/ast.tks   (namespace 'teko::parser') — statement nodes extended

// The three binding kinds share a shape; the kind distinguishes them (B.21).
type BindKind = enum {
    BindLet      // immutable runtime binding
    BindMut      // mutable runtime binding
    BindConst    // compile-time immutable (.rodata)
}

// A binding CREATES a name. The type annotation is OPTIONAL (inference when
// absent). Modeled with an explicit presence flag + the name, NOT as a `T?`
// optional: structural nullability (`T?`) is a *later parser layer*, so using it
// now would build ahead of it (M.4). When nullability lands, `has_type` +
// `type_name` collapse into a single `type_name: ([]u8)?`. (Discipline-overhead:
// honor the intent — optional — with what this layer offers, and document it.)
type Binding = struct {
    kind:      BindKind     // let | mut | const
    name:      str         // the bound identifier
    has_type:  bool         // was a `: T` annotation given?
    type_name: []u8         // the annotation's type name (when has_type) — an Ident (B.19)
    value:     Expr         // the value — a full expression (reuses A.2)
}

// A reassignment REBINDS an existing `mut` (no new name created). Distinct from
// Binding (M.3 — create vs. reassign). Only the plain `=` store exists in this
// layer; the compound forms (`+=` …) join when operators land.
type Assign = struct {
    name:  str        // the target (must resolve to a `mut` — checked later)
    value: Expr
}

// Statement now has three cases beyond the expression statement (variant — B.14).
type Statement = Binding | Assign | ExprStmt
```

```teko
// src/parser/binding.tks   (namespace 'teko::parser') — bindings + reassignment

// map the leading keyword to a BindKind. The caller (parse_statement) has already
// verified the token is one of the three binding keywords, so the three cases are
// total over what reaches here. The `_` valve marks the genuinely impossible
// (M.1 — rather than a false default that would lie about an unreachable path, it
// panics: a reached `_` is a compiler bug, not a value).
fn bind_kind_of(k: lexer::TokenKind) -> BindKind {
    match k {
        lexer::TokenKind::Let   => BindKind::BindLet
        lexer::TokenKind::Mut   => BindKind::BindMut
        lexer::TokenKind::Const => BindKind::BindConst
        _                       => teko::panic("bind_kind_of on a non-binding token")
    }
}

// parse_binding: KEYWORD name (':' type)? '=' expr
// One parser for all three bindings — they differ only in the leading keyword
// (B.21: same shape, the kind is the role). The annotation is optional.
fn parse_binding(tokens: []lexer::Token, pos: u64) -> ParsedStmt | error {
    let kind = bind_kind_of(kind_at(tokens, pos))
    let p0 = pos + 1     // past the binding keyword

    // name (expect returns the position after the Ident, or error)
    if !has_token(tokens, p0) {
        return error { message = "expected a name in binding" }
    }
    let name = tokens[p0].text
    let p1 = match expect(tokens, p0, lexer::TokenKind::Ident, "expected an identifier after the binding keyword") {
        error as e => return e
        u64 as np  => np
    }

    // optional type annotation: ':' Ident — parsed by a small helper (flat)
    let ann = match parse_opt_annotation(tokens, p1) {
        error as e => return e
        Annotation as a => a
    }

    // '='
    let p_val = match expect(tokens, ann.next, lexer::TokenKind::Assign, "expected '=' after the name (or type)") {
        error as e => return e
        u64 as np  => np
    }

    // value: a full expression (guard: extract the Expr or propagate)
    let v = match parse_expr(tokens, p_val) {
        error as e => return e
        Parsed as v => v
    }
    let b = Binding {
        kind = kind; name = name
        has_type = ann.has_type; type_name = ann.type_name
        value = v.node
    }
    ParsedStmt { node = b; next = v.next }
}

// The result of trying to parse an optional `: T` annotation: whether one was
// present, its type name (when present), and where parsing stopped. A small
// struct (B.3) so the optional is modeled honestly for THIS layer (see the note
// on has_type/type_name above — it collapses to `([]u8)?` when nullability lands).
type Annotation = struct {
    has_type:  bool
    type_name: []u8
    next:      u64
}

// parse_opt_annotation: if the token at `pos` is ':', read ':' Ident; otherwise
// no annotation (pass through). FLAT: each step guards or returns.
fn parse_opt_annotation(tokens: []lexer::Token, pos: u64) -> Annotation | error {
    // no ':' → no annotation
    if !has_token(tokens, pos) {
        return Annotation { has_type = false; type_name = empty_slice(); next = pos }
    }
    if kind_at(tokens, pos) != lexer::TokenKind::Colon {
        return Annotation { has_type = false; type_name = empty_slice(); next = pos }
    }
    // ':' present → a type name (Ident) must follow
    let p_after_colon = pos + 1
    if !has_token(tokens, p_after_colon) {
        return error { message = "expected a type after ':'" }
    }
    let type_name = tokens[p_after_colon].text
    let p_next = match expect(tokens, p_after_colon, lexer::TokenKind::Ident, "expected a type name after ':'") {
        error as e => return e
        u64 as np  => np
    }
    Annotation { has_type = true; type_name = type_name; next = p_next }
}

// parse_assign: name '=' expr   (simple reassignment of an existing mut)
// Entered when a statement starts with an Ident immediately followed by `=`.
// FLAT: the value is a guard line, the node built at the top level.
fn parse_assign(tokens: []lexer::Token, pos: u64) -> ParsedStmt | error {
    let name = tokens[pos].text     // caller checked tokens[pos] is Ident
    let p = pos + 2                 // past the Ident and the `=`

    let v = match parse_expr(tokens, p) {
        error as e => return e
        Parsed as v => v
    }
    let a = Assign { name = name; value = v.node }
    ParsedStmt { node = a; next = v.next }
}
```

```teko
// src/parser/program.tks   (namespace 'teko::parser') — statement dispatch updated

// parse_statement: dispatch on the leading token (B.15).
//   let/mut/const   → a binding (creates a name)
//   Ident '='       → a simple reassignment (rebinds an existing mut)
//   otherwise       → an expression statement
fn parse_statement(tokens: []lexer::Token, pos: u64) -> ParsedStmt | error {
    if !has_token(tokens, pos) {
        return error { message = "expected a statement" }
    }
    let k = kind_at(tokens, pos)

    // bindings — the three keywords
    if k == lexer::TokenKind::Let   { return parse_binding(tokens, pos) }
    if k == lexer::TokenKind::Mut   { return parse_binding(tokens, pos) }
    if k == lexer::TokenKind::Const { return parse_binding(tokens, pos) }

    // reassignment — an Ident immediately followed by `=` (flat: one predicate)
    if is_reassignment(tokens, pos) { return parse_assign(tokens, pos) }

    // otherwise — an expression statement (reuses A.3's parse_expr_stmt)
    parse_expr_stmt(tokens, pos)
}

// is_reassignment: an Ident at `pos` immediately followed by `=`. Folds the
// "Ident then Assign" test into one flat predicate (guard over nest).
fn is_reassignment(tokens: []lexer::Token, pos: u64) -> bool {
    if !has_token(tokens, pos) { return false }
    if kind_at(tokens, pos) != lexer::TokenKind::Ident { return false }
    if !has_token(tokens, pos + 1) { return false }
    kind_at(tokens, pos + 1) == lexer::TokenKind::Assign
}

// parse_program is UNCHANGED from A.3 — it loops parse_statement, requiring a
// Newline terminator (B.17) between statements.
```

**Notes for implementers (and the C23 port):**
- **This layer covers only what rests on existing structure.** `const`/`let`/`mut`,
  the optional `: T`, and simple `x = v` reassignment depend only on identifiers
  and the A.2 expression parser — both finished. That is why they can exist now.
- **One parser for three bindings (B.21 — same shape, the kind is the role).**
  `parse_binding` handles all three; the C23 port keeps one binding parser with a
  kind field, not three near-duplicates (the cohesion norm).
- **Binding and reassignment are distinct AST cases (M.3 — create vs. reassign).**
  The parser records structure; the *checker* (a later stage) enforces that
  `Assign` targets a `mut`, that `const` is compile-time known, and that `mut` is
  only on a local (not a parameter or match-binding) — B.21's rules live in the
  checker, not the parser.
- **`=` stays maximal-munch-ready (B.23).** The lexer arm looks ahead, so when
  `==` and the compound `+=`/`-=`/… arrive with the operator layer, no lexer code
  here changes — longest match always.
- **Type annotation is one `Ident` for now (M.4).** Composite annotations (`[]T`,
  `T?`) enter with their layers (arrays, nullability).
- **State flows ref-lessly (B.7), error is a value (B.1), no `?` (B.16), distinct
  result structs (A.3)** — all unchanged into the C23 port.

---

#### Note — assignment is a cross-cutting *dimension*, not a single layer

Closing "the assignment family" in one layer is impossible, because **assignment
— binding a name to a value — happens in several different contexts, each
depending on a different structure** that does not yet exist. The complete family,
by where the binding occurs:

- **Statement bindings** (`let`/`mut`/`const x = v`) and **simple reassignment**
  (`x = v`) — depend on identifiers + expression. **✓ here (A.3/A.4).**
- **Compound reassignment** (`x += v` …) — depends on the **operators** (B.22).
  **→ the layer after operators.**
- **Arguments** (a parameter ← the caller's value at a call) — depend on **`fn`**.
  Parameters are immutable (B.21 — they *receive*, they do not create). **→ with `fn`.**
- **Destructuring** (`let {x; y} = point` — several names ← a struct's fields) —
  depends on **`struct`**. **→ with the type layer.**
- **Match-bindings** (`Number as n`, `Binary {left; right}` in arms — name(s) ←
  the subject's content) — depend on **`match`**; immutable (B.21). **→ with `match`.**
- **Aliases** (`use x as y` — a local name ← an imported symbol) — depend on
  **`use`/modules**. **→ with the module layer.**

So each binding form is **born with the structure that hosts it**, never ahead of
it (M.4). "Assignment" is a dimension threaded through the language, lit one
structure at a time — not a box to be closed in isolation. (This is itself a build
rule, recorded in the Legislation's Bootstrap & process section.)

### A.5 — The complete operators (arithmetic, bitwise, shift, comparison, logical)

This layer brings the **full operator inventory** (B.22) and the **complete
precedence hierarchy** (B.23). A.2 had three precedence levels (a partial
ladder); this extends the lexer with every operator token and the parser to the
**nine-level hierarchy**, one recursive-descent function per level — so the
precedence stays visible in the call structure (the legibility-local facet of
M.2). It is the base the next layer (compound assignment) rests on. Per M.4, the
lexer extends first.

**The hierarchy (B.23, strongest at top → weakest at bottom):**
```
 1  ()                       parentheses / atom
 2  - ~ !                    unary (prefix, right-assoc)
 3  << >>                    shift            (just below multiplication)
 4  * / %   &                multiplicative + bitwise AND   (same level; AND≈*)
 5  + -     | ^              additive + bitwise OR/XOR       (same level; OR/XOR≈+)
 6  < > <= >= == !=          comparison       (CHAINED — a<b<c ≡ (a<b)&&(b<c))
 7  &&                       logical AND
 8  ||                       logical OR
 9  =                        assignment (statement-level; see A.4)
```
Bitwise sits **above** comparison (fixing C's `a & b == c` bug — B.23, M.3: the
grouping must not deceive). Levels 4–5 mix arithmetic and bitwise at the same
rank (Julia model — AND≈`*`, OR/XOR≈`+`). All left-associative **except** unary
(prefix, right) and comparison (chained).

#### Lexer extension (over A.4)

```teko
// src/lexer/token.tks   (namespace 'teko::lexer') — operator tokens added

type TokenKind = enum {
    Number
    Ident
    Underscore
    Plus            // +
    Minus           // -
    Star            // *
    Slash           // /
    Percent         // %      NEW (arithmetic remainder — B.22)
    Amp             // &      NEW (bitwise AND)
    Pipe            // |      NEW (bitwise OR)
    Caret           // ^      NEW (bitwise XOR)
    Tilde           // ~      NEW (bitwise NOT — unary)
    Shl             // <<     NEW (shift left — maximal munch over `<` `<`, B.23)
    Shr             // >>     NEW (shift right)
    Lt              // <      NEW (less-than)
    Gt              // >      NEW (greater-than)
    Le              // <=     NEW (maximal munch over `<` `=`)
    Ge              // >=     NEW
    EqEq            // ==     NEW (equality — maximal munch over `=` `=`)
    Ne              // !=     NEW (maximal munch over `!` `=`)
    AndAnd          // &&     NEW (logical AND — maximal munch over `&` `&`)
    OrOr            // ||     NEW (logical OR)
    Bang            // !      NEW (logical NOT — unary)
    LParen
    RParen
    Let
    Assign          // =
    Newline
    Mut
    Const
    Colon
}
```

```teko
// src/lexer/lexer.tks   (namespace 'teko::lexer') — maximal-munch dispatch (B.23)

// Every multi-byte operator is ONE token by maximal munch (B.23): the lexer
// looks at the next byte(s) and takes the LONGEST match. No context heuristic —
// the lexer classifies, it does not interpret (M.4). The dispatch arms, sketched
// (each consumes 1 or 2 bytes and advances accordingly):
//
//   '<' → next '<' → Shl      | next '=' → Le  | else Lt
//   '>' → next '>' → Shr      | next '=' → Ge  | else Gt
//   '=' → next '=' → EqEq     | else Assign                  (A.4's `=` extended)
//   '!' → next '=' → Ne       | else Bang
//   '&' → next '&' → AndAnd   | else Amp
//   '|' → next '|' → OrOr     | else Pipe
//   '%' → Percent
//   '^' → Caret
//   '~' → Tilde
//   '/' → (the `//` comment check (B.18) precedes this) → Slash
//
// `two(source, pos, kind)` is the two-byte analogue of A.1's `single`: it builds
// a Token spanning [pos, pos+2) and sets next = pos + 2. A small helper, like
// `single`, in the same cohesive lexer file (no fragmentation — the org norm).
```

#### Parser extension (over A.4 / A.2)

The expression AST grows two cases; the three existing functions become nine.

```teko
// src/parser/ast.tks   (namespace 'teko::parser') — Expr variant extended

// (unchanged) a literal number leaf
type Number = struct {
    value: i64
}

// (unchanged shape) a binary operation — now over ALL binary operators (the op
// field already held a TokenKind; the set of kinds it carries simply grew).
type Binary = struct {
    op:    lexer::TokenKind     // any binary operator (Plus … OrOr)
    left:  Expr
    right: Expr
}

// NEW — a unary prefix operation: `- ~ !` applied to one operand. Right-assoc
// (the operand may itself be unary): `--x` is `Unary(-, Unary(-, x))`.
type Unary = struct {
    op:      lexer::TokenKind   // Minus | Tilde | Bang
    operand: Expr
}

// NEW — a comparison CHAIN (B.23: a<b<c ≡ (a<b)&&(b<c)). The node PRESERVES the
// chain as written (M.3 — honest to the source); the AND-semantics (and the
// single evaluation of each middle operand) is the codegen's job, not the
// parser's. `first` is the leftmost operand; each `CmpTerm` is an (operator,
// operand) continuing the chain. A single comparison `a < b` is `first=a,
// rest=[(<, b)]`.
type CmpTerm = struct {
    op:      lexer::TokenKind   // Lt | Gt | Le | Ge | EqEq | Ne
    operand: Expr
}
type Compare = struct {
    first: Expr
    rest:  []CmpTerm            // one or more comparison continuations
}

// The expression node: exactly one case (variant — B.14).
type Expr = Number | Binary | Unary | Compare
```

```teko
// src/parser/expr.tks   (namespace 'teko::parser') — the precedence ladder (B.23)

// One function per precedence level. The call order IS the precedence: the entry
// (parse_expr → parse_or) is loosest; the deepest (parse_atom) is tightest. Each
// returns `Parsed | error` (B.1). Levels 3,4,5,7,8 are ordinary LEFT-associative
// fold-left loops (the same pattern A.2 used for parse_term); only unary (prefix)
// and comparison (chained) differ — see below.
//
// Stateless function passing (B.10 form 1, SEED) collapses the five ordinary
// left-associative binary levels into ONE helper. The helper takes the level's
// operator predicate and the next-tighter parser as **passed functions** — both
// are module-level, stateless (no capture), so this is exactly the seed-legal
// form 1 (a code address, no `ref`, no state). The two named function types
// (B.13 — nominal) document the shapes:
type OpPredicate = fn([]lexer::Token, u64) -> bool
type LevelParser = fn([]lexer::Token, u64) -> Parsed | error

// parse_binary_level: `next (op next)*`, folding LEFT. One helper for every
// ordinary binary level (or, and, additive, multiplicative, shift) — no longer
// five near-identical loops (M.5 cohesion, now expressible because form 1 is
// seed). `is_op` and `next` are passed by bare name at each call site.
fn parse_binary_level(
    tokens: []lexer::Token, pos: u64,
    is_op: OpPredicate, next: LevelParser
) -> Parsed | error {
    let start = match next(tokens, pos) {           // call the passed parser (with ())
        error as e => return e
        Parsed as s => s
    }
    mut node = start.node
    mut p    = start.next
    loop {
        if !is_op(tokens, p) { break }              // call the passed predicate
        let op = kind_at(tokens, p)
        let r = match next(tokens, p + 1) {
            error as e => return e
            Parsed as r => r
        }
        node = Binary { op = op; left = node; right = r.node }
        p = r.next
    }
    Parsed { node = node; next = p }
}

// level 9 entry → delegates to level 8 (assignment is statement-level, A.4)
fn parse_expr(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    parse_or(tokens, pos)
}

// level 8 — `||` (logical OR). Pass is_oror + the next level by bare name.
fn parse_or(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    parse_binary_level(tokens, pos, is_oror, parse_and)
}

// level 7 — `&&` (logical AND)
fn parse_and(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    parse_binary_level(tokens, pos, is_andand, parse_comparison)
}

// level 6 — comparison, CHAINED (B.23). NOT an ordinary fold-left, so it is NOT
// one of the parse_binary_level levels: a run of comparisons becomes ONE Compare
// node preserving the chain (the AND-semantics and single-evaluation is codegen's
// — M.3, honest to the source). `a` alone (no comparison) passes through.
fn parse_comparison(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    let start = match parse_additive(tokens, pos) {
        error as e => return e
        Parsed as s => s
    }
    // no comparison operator follows → not a comparison, pass through
    if !is_comparison(tokens, start.next) {
        return start
    }
    // one or more comparison continuations → build the chain
    mut terms = teko::list::empty()
    mut p     = start.next
    loop {
        if !is_comparison(tokens, p) { break }
        let op = kind_at(tokens, p)
        let r = match parse_additive(tokens, p + 1) {
            error as e => return e
            Parsed as r => r
        }
        terms = teko::list::push(terms, CmpTerm { op = op; operand = r.node })
        p = r.next
    }
    Parsed { node = Compare { first = start.node; rest = terms }; next = p }
}

// level 5 — additive `+ -` AND bitwise `| ^` (same rank; OR/XOR≈+)
fn parse_additive(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    parse_binary_level(tokens, pos, is_additive, parse_multiplicative)
}

// level 4 — multiplicative `* / %` AND bitwise `&` (same rank; AND≈*)
fn parse_multiplicative(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    parse_binary_level(tokens, pos, is_multiplicative, parse_shift)
}

// level 3 — shift `<< >>` (its own level, just below multiplication)
fn parse_shift(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    parse_binary_level(tokens, pos, is_shift, parse_unary)
}

// level 2 — unary PREFIX `- ~ !`, RIGHT-associative (recursion, not a loop):
// if the current token is a unary operator, consume it and recurse into unary
// (so `--x` = Unary(-, Unary(-, x))); otherwise fall through to the atom.
fn parse_unary(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    if !is_unary(tokens, pos) {
        return parse_atom(tokens, pos)
    }
    let op = kind_at(tokens, pos)
    let r = match parse_unary(tokens, pos + 1) {      // right-assoc recursion
        error as e => return e
        Parsed as r => r
    }
    Parsed { node = Unary { op = op; operand = r.node }; next = r.next }
}

// level 1 — atom: a number, or a parenthesised expression. Dispatches to small
// flat per-case functions (guard over nest — M.2), as in A.2.
fn parse_atom(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    if !has_token(tokens, pos) {
        return error { message = "unexpected end of input" }
    }
    match kind_at(tokens, pos) {
        lexer::TokenKind::Number => parse_number_atom(tokens, pos)
        lexer::TokenKind::LParen => parse_paren_atom(tokens, pos)
        _                        => error { message = "expected a number or '('" }
    }
}

// a literal number leaf (text→i64 elided — B.28)
fn parse_number_atom(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    let n = to_i64(tokens[pos].text)
    Parsed { node = Number { value = n }; next = pos + 1 }
}

// '(' expr ')' — recurse, then demand ')'. FLAT: each fallible step is a guard.
fn parse_paren_atom(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    let inner = match parse_expr(tokens, pos + 1) {
        error as e => return e
        Parsed as p => p
    }
    let after = match expect(tokens, inner.next, lexer::TokenKind::RParen, "expected ')'") {
        error as e => return e
        u64 as np  => np
    }
    Parsed { node = inner.node; next = after }
}
```

```teko
// src/parser/optokens.tks   (namespace 'teko::parser') — the per-level predicates

// One predicate per level — each tests whether the token at `pos` belongs to
// that precedence level. All are small and uniform (cohesion, M.5). Each does
// the `has_token` guard then compares the kind.

fn is_unary(tokens: []lexer::Token, pos: u64) -> bool {
    if !has_token(tokens, pos) { return false }
    let k = tokens[pos].kind
    k == lexer::TokenKind::Minus || k == lexer::TokenKind::Tilde || k == lexer::TokenKind::Bang
}

fn is_shift(tokens: []lexer::Token, pos: u64) -> bool {
    if !has_token(tokens, pos) { return false }
    let k = tokens[pos].kind
    k == lexer::TokenKind::Shl || k == lexer::TokenKind::Shr
}

fn is_multiplicative(tokens: []lexer::Token, pos: u64) -> bool {
    if !has_token(tokens, pos) { return false }
    let k = tokens[pos].kind
    // `* / %` and bitwise `&` share this level (Julia model; AND≈*)
    k == lexer::TokenKind::Star || k == lexer::TokenKind::Slash ||
    k == lexer::TokenKind::Percent || k == lexer::TokenKind::Amp
}

fn is_additive(tokens: []lexer::Token, pos: u64) -> bool {
    if !has_token(tokens, pos) { return false }
    let k = tokens[pos].kind
    // `+ -` and bitwise `| ^` share this level (OR/XOR≈+)
    k == lexer::TokenKind::Plus || k == lexer::TokenKind::Minus ||
    k == lexer::TokenKind::Pipe || k == lexer::TokenKind::Caret
}

fn is_comparison(tokens: []lexer::Token, pos: u64) -> bool {
    if !has_token(tokens, pos) { return false }
    let k = tokens[pos].kind
    k == lexer::TokenKind::Lt || k == lexer::TokenKind::Gt ||
    k == lexer::TokenKind::Le || k == lexer::TokenKind::Ge ||
    k == lexer::TokenKind::EqEq || k == lexer::TokenKind::Ne
}

fn is_andand(tokens: []lexer::Token, pos: u64) -> bool {
    if !has_token(tokens, pos) { return false }
    tokens[pos].kind == lexer::TokenKind::AndAnd
}

fn is_oror(tokens: []lexer::Token, pos: u64) -> bool {
    if !has_token(tokens, pos) { return false }
    tokens[pos].kind == lexer::TokenKind::OrOr
}
```

**Notes for implementers (and the C23 port):**
- **The structure IS the precedence (B.23), now at full height.** Nine functions,
  loosest (`parse_expr`→`parse_or`) to tightest (`parse_atom`). No precedence
  table — the call hierarchy *is* the table (legibility-local, M.2). The C23 port
  keeps the same nine functions, same names, same order.
- **The five ordinary binary levels collapse into one helper via function passing
  (B.10 form 1, SEED).** `parse_binary_level` takes the level's predicate and the
  next-tighter parser as **passed functions** (bare name, called with `()`); both
  are module-level and stateless, exactly the seed-legal form 1 (a code address, no
  `ref`, no capture). This replaces the five near-identical fold-left loops (M.5
  cohesion, now expressible). Comparison (chained) and unary (prefix) are **not**
  ordinary folds, so they stay their own functions. The C23 port passes C function
  pointers — the same shape.
- **Unary is prefix, right-associative (B.23) — recursion, not a loop.**
  `parse_unary` consumes a `- ~ !` and recurses into itself, so `~~x` nests
  correctly. It is the one ascending level that recurses on itself rather than
  folding left.
- **Comparison chains into ONE `Compare` node (B.23, M.3 — honest to source).**
  `a < b < c` is **not** `(a<b)<c` and is **not** pre-expanded to `(a<b)&&(b<c)` by
  the parser. The parser records the chain (`first` + a list of `(op, operand)`);
  the **codegen** lowers it to the conjunction *and* guarantees each middle operand
  is evaluated once (the property `&&`-expansion in the parser would lose). Parser
  structures; codegen gives semantics — the layer separation (as in A.4).
- **Short-circuit (`&&`/`||`) is codegen, not parser.** The parser builds
  `Binary{op: AndAnd|OrOr}`; lazy evaluation (skip the right side when the left
  decides) is emitted by codegen. The parser only structures.
- **Maximal munch is the lexer's (B.23).** `<<`/`<=`/`==`/`!=`/`&&`/`||` are each
  one token by longest-match; the parser receives them whole. The `//`-comment
  check (B.18) precedes the `/` arm so a comment is never read as an operator.
- **The four promotion regimes and the sign-check comparison (B.22) are the
  CHECKER's, not the parser's.** The parser accepts `i8 << i64`, `u8 & i32`,
  `signed < unsigned`; the checker applies promotion (arithmetic→larger,
  bitwise→same-width-or-error, shift→left's type) and the sign-check-first
  comparison. Shift-count saturation and the debug-only checks are codegen. The
  parser records structure; type rules live downstream (the pipeline separation,
  M.4).
- **State ref-less (B.7), error a value (B.1), no `?` (B.16), `ParsedStmt` vs
  `Parsed` distinct (A.3)** — all unchanged into the C23 port.
- **Now unblocked:** compound assignment (`+= … &= <<=`) — it desugars to
  `x = x OP y` over exactly these operators, so it returns in the next layer,
  complete (arithmetic *and* bitwise/shift), resting on this finished operator set.

### A.6 — Variable references in expressions (the missing atom)

A base feature of expressions that was absent since A.2: the expression parser
could not reference a **variable**. Its atom level handled only a literal number
and a parenthesised expression — so `let z = y`, `x = y`, and even `x + 1` all
failed (the `Ident` `y`/`x` reached `parse_atom`, which had no case for it). The
earlier examples worked only because they used **literal arithmetic** (`1 + 2 *
3`). This layer adds the missing atom: an identifier used as a value — a
**variable reference**. It is a prerequisite for compound assignment (next layer),
whose desugar `x += e` ≡ `x = x + e` reads `x` — but it is not specific to
compound; it completes expressions for *every* use.

**No lexer change** — the `Ident` token already exists (A.1). The whole change is
one AST case and one `parse_atom` arm.

```teko
// src/parser/ast.tks   (namespace 'teko::parser') — Expr variant gains a leaf

// (unchanged) a literal number leaf
type Number = struct {
    value: i64
}

// NEW — a variable reference: an identifier used as a value (READING a variable).
// Distinct from the identifier used as an assignment TARGET (`x = …`), which the
// statement layer holds as a `[]u8` name, not as an Expr (M.3 — reading x and
// writing x are different acts). Distinct, too, from a call (`f(x)`), which waits
// for `fn`: until then a bare identifier is always a variable reference.
type Var = struct {
    name: str
}

// (unchanged) Binary, Unary, Compare … (the operator nodes from A.2/A.5)

// The expression node gains the Var case (variant — B.14; exactly one).
type Expr = Number | Var | Binary | Unary | Compare
```

```teko
// src/parser/expr.tks   (namespace 'teko::parser') — parse_atom gains the Ident case

// level 1 — atom: a number, a VARIABLE reference, or a parenthesised expression.
// Dispatches on the leading token; each case is a small flat function.
fn parse_atom(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    if !has_token(tokens, pos) {
        return error { message = "unexpected end of input" }
    }
    match kind_at(tokens, pos) {
        lexer::TokenKind::Number => parse_number_atom(tokens, pos)
        lexer::TokenKind::Ident  => parse_var_atom(tokens, pos)
        lexer::TokenKind::LParen => parse_paren_atom(tokens, pos)
        _                        => error { message = "expected a number, a name, or '('" }
    }
}

// a variable reference: the identifier's text becomes a Var leaf. Trivial — one
// token, no fallible sub-parse, so no guard needed.
fn parse_var_atom(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    Parsed { node = Var { name = tokens[pos].text }; next = pos + 1 }
}
```

**Notes for implementers (and the C23 port):**
- **This completes the expression atom level for values.** The atom is now: a
  number literal, a variable reference, or a parenthesised expression. The only
  remaining atom kind is a **call** (`f(x)`), which arrives with `fn` — at that
  point `parse_var_atom` (or a sibling) looks at the token *after* the identifier:
  a `(` means a call, its absence means a variable reference. Until `fn`, a bare
  identifier is always a variable reference.
- **Reading a variable (Var) vs. writing one (target) vs. calling — three honest,
  distinct things (M.3).** `parse_atom`'s `Ident` case builds a `Var` (an *expression*
  — reading). The statement layer's `parse_assign`/`parse_binding` hold the target
  identifier as a `[]u8` *name* (writing) — never as a `Var`. A call would be a
  fourth node (awaiting `fn`). Conflating reading and writing into one node would
  blur what the program does; they stay separate.
- **Now unblocked (and not only compound):** any expression referencing a variable
  — `let z = y`, `x = a + b`, `mut n: u64 = count` — parses. Compound assignment
  (next) desugars `x += e` to `x = x + e`, where both `x` and the variables in `e`
  are `Var` references; this layer is what makes that desugar expressible.
- **The C23 port** adds one tagged-union case (`AST_VAR` carrying the name slice)
  and one `parse_atom` branch — no lexer change (the identifier token already
  exists). State ref-less (B.7), error a value (B.1), no `?` (B.16) — unchanged.

### A.7 — Compound assignment (reborn complete, over operators + variables)

Compound assignment was deliberately removed from A.4 (it depended on the
operators, then unbuilt — M.4). Now the operators are complete (A.5) and
expressions can reference variables (A.6), so it **returns complete**, resting on
a finished base. The forms are `+= -= *= /= %=` (arithmetic), `&= |= ^=`
(bitwise), and `<<= >>=` (shift). The logical compounds `&&= ||=` are **deferred
to evolution** (they are compounds of *short-circuit control flow*, not ALU
operations — not metal, and unused by the seed; B.23 reserves their precedence
slot, this layer fills the metal part). Per M.4, the lexer extends first.

**The op is preserved, not desugared in the parser (M.3 + consistency).** `x += e`
is recorded as an `Assign` carrying the operator (`PlusEq`), not pre-expanded to
`x = x + e`. Three reasons converge: it mirrors the comparison-chain decision
(A.5 preserved the chain, codegen gives the semantics); it is honest to what the
programmer wrote (M.3 — `x += e` is distinguishable from `x = x + e`); and it is
future-proof — when assignment targets become complex (`a[f()] += e`, later),
the codegen desugar must evaluate the target **once** (`f()` not twice), the same
single-evaluation care as a comparison chain. The parser only structures; the
**desugar and single-evaluation are codegen's**.

#### Lexer extension (over A.5)

```teko
// src/lexer/token.tks   (namespace 'teko::lexer') — compound-assignment tokens added

type TokenKind = enum {
    // … (all prior tokens: Number, Ident, the operators, Let/Mut/Const, etc.)
    PlusEq          // +=    NEW (arithmetic compounds — 2 bytes)
    MinusEq         // -=
    StarEq          // *=
    SlashEq         // /=
    PercentEq       // %=
    AmpEq           // &=    NEW (bitwise compounds — 2 bytes)
    PipeEq          // |=
    CaretEq         // ^=
    ShlEq           // <<=   NEW (shift compounds — 3 bytes! longest munch)
    ShrEq           // >>=
}
```

```teko
// src/lexer/lexer.tks   (namespace 'teko::lexer') — maximal munch, now up to 3 bytes

// Compound operators are ONE token by maximal munch (B.23), longest match always.
// The shift compounds are THREE bytes — the dispatch looks ahead up to two bytes:
//
//   '+' → next '=' → PlusEq    | else Plus
//   '-' → next '=' → MinusEq   | else Minus
//   '*' → next '=' → StarEq    | else Star
//   '/' → next '=' → SlashEq   | else Slash      (the `//` comment check precedes)
//   '%' → next '=' → PercentEq | else Percent
//   '^' → next '=' → CaretEq   | else Caret
//   '&' → next '&' → AndAnd    | next '=' → AmpEq  | else Amp
//   '|' → next '|' → OrOr      | next '=' → PipeEq | else Pipe
//   '<' → next '<' → (then next '=' → ShlEq | else Shl) | next '=' → Le | else Lt
//   '>' → next '>' → (then next '=' → ShrEq | else Shr) | next '=' → Ge | else Gt
//   '=' → next '=' → EqEq      | else Assign
//
// A three-byte operator advances the position by 3; two-byte by 2; one-byte by 1.
// `three(source, pos, kind)` is the three-byte analogue of `single`/`two`: it
// spans [pos, pos+3) and sets next = pos + 3. Still one cohesive lexer file.
```

#### Parser extension (over A.6 / A.4)

The expression layer (A.2/A.5/A.6) is unchanged. The statement layer's `Assign`
node (A.4) regains its operator field, and the reassignment dispatch widens from
"`Ident` then `=`" to "`Ident` then any assignment operator."

```teko
// src/parser/ast.tks   (namespace 'teko::parser') — Assign regains the op field

// A reassignment REBINDS an existing `mut`. `op` records WHICH assignment was
// written: plain `Assign` (`=`), or a compound (`PlusEq`/…/`ShrEq`). The compound
// forms are NOT desugared here — the op is preserved (M.3, honest to source); the
// codegen lowers `x += e` to `x = x + e` AND guarantees the target is evaluated
// once (single-evaluation — trivial for a name now, essential for complex targets
// later). Distinct from Binding (create vs. reassign — M.3).
type Assign = struct {
    name:  str                 // the target (must resolve to a `mut` — checked later)
    op:    lexer::TokenKind      // Assign (plain) | PlusEq | MinusEq | StarEq | SlashEq
                                 //                | PercentEq | AmpEq | PipeEq | CaretEq
                                 //                | ShlEq | ShrEq
    value: Expr
}
```

```teko
// src/parser/binding.tks   (namespace 'teko::parser') — reassignment widened

// Is the token at `pos` an assignment operator — plain `=` or any compound?
fn is_assign_op(tokens: []lexer::Token, pos: u64) -> bool {
    if !has_token(tokens, pos) { return false }
    let k = tokens[pos].kind
    k == lexer::TokenKind::Assign    ||
    k == lexer::TokenKind::PlusEq    || k == lexer::TokenKind::MinusEq ||
    k == lexer::TokenKind::StarEq    || k == lexer::TokenKind::SlashEq ||
    k == lexer::TokenKind::PercentEq ||
    k == lexer::TokenKind::AmpEq     || k == lexer::TokenKind::PipeEq  ||
    k == lexer::TokenKind::CaretEq   ||
    k == lexer::TokenKind::ShlEq     || k == lexer::TokenKind::ShrEq
}

// An Ident at `pos` immediately followed by an assignment operator (plain or
// compound). Widens A.4's "Ident then `=`" to "Ident then any assign op."
fn is_reassignment(tokens: []lexer::Token, pos: u64) -> bool {
    if !has_token(tokens, pos) { return false }
    if kind_at(tokens, pos) != lexer::TokenKind::Ident { return false }
    is_assign_op(tokens, pos + 1)
}

// parse_assign: name OP expr   (reassignment of an existing mut; OP plain or
// compound). FLAT (guard over nest): the value is a guard line, the node built at
// the top level. The op is recorded as written — not desugared (codegen's job).
fn parse_assign(tokens: []lexer::Token, pos: u64) -> ParsedStmt | error {
    let name = tokens[pos].text     // caller checked tokens[pos] is Ident
    let op   = tokens[pos + 1].kind  // caller checked is_assign_op at pos+1
    let p    = pos + 2

    let v = match parse_expr(tokens, p) {
        error as e => return e
        Parsed as v => v
    }
    let a = Assign { name = name; op = op; value = v.node }
    ParsedStmt { node = a; next = v.next }
}

// parse_statement is UNCHANGED from A.4 — it already dispatches reassignment via
// is_reassignment (now widened) to parse_assign. The binding keywords, the
// reassignment, and the expression-statement fallthrough all flow as before; the
// only change is that is_reassignment/parse_assign now accept compound operators.
```

**Notes for implementers (and the C23 port):**
- **Compound returns complete, on a finished base (M.4).** It rested on operators
  (A.5) and variable references (A.6); with both done, all ten metal compounds
  (`+= … >>=`) enter at once — not the partial four that an earlier attempt would
  have built on incomplete operators. The order held: operators → variables →
  compound.
- **`&&= ||=` are evolution (M.0 + M.5).** They compound `&&`/`||`, which are
  short-circuit *control flow*, not ALU operations — not metal, and the seed uses
  neither. B.23 reserves their slot; the seed fills only the metal compounds.
- **The op is preserved; desugar is codegen's (M.3 + single-evaluation).** The
  parser records `Assign { op = PlusEq }`; codegen lowers it to `x = x + e` and
  evaluates the target once. For a name target this is trivial, but recording the
  op (rather than desugaring in the parser) is what lets codegen do it correctly
  when targets become complex (`a[f()] += e` — `f()` once). Same parser-structures
  / codegen-gives-semantics split as the comparison chain (A.5).
- **Maximal munch now spans three bytes (`<<= >>=`).** The lexer's `<`/`>` arms
  look ahead up to two bytes (`<` → `<<` → `<<=`). The longest match always wins;
  no context heuristic. The `//`-comment check (B.18) still precedes the `/` arm.
  The C23 port mirrors the lookahead ladder.
- **The checker enforces B.21 here too.** That `Assign` targets a `mut` (not a
  `let`/`const`), and the operand-type rules for the underlying operator (the four
  promotion regimes — B.22), are the checker's; the parser only records structure.
- **State ref-less (B.7), error a value (B.1), no `?` (B.16), guard over nest** —
  all as established. With this, the **assignment dimension's** second strand is
  complete; the next strand is **arguments**, born with `fn`.

### A.8 — Type expressions (the sub-grammar `fn` needs)

A function has **types** on its parameters and return (`fn tokenize(source: []u8)
-> []Token | error`), and those types can be composite. The canonical examples
already *write* such types in their signatures, but the parser could not read
them: A.4's annotation parsed only a single `Ident`, and the tokens `[` `]` `::`
did not exist. This layer adds **type expressions** — the small sub-grammar for
writing a type. It is a prerequisite for `fn` (next), and it also **completes
A.4's annotation** (the single-`Ident` stand-in). Per M.4, the lexer extends
first.

**The forms** (all used in the examples): a **simple name** (`u64`, `Token`), a
**slice** (`[]u8`, `[]Token`), a **union** (`u64 | error`, `[]Token | error`),
and a **qualified name** (`lexer::Token`, `lexer::TokenKind`). The grammar, by
recursive descent (union loosest, slice tighter, path tightest):
```
type   = union
union  = slice ('|' slice)*      — union, left-associative (loosest)
slice  = '[]' slice | path       — '[]' then a slice-or-path (so [][]T works)
path   = Ident ('::' Ident)*     — lexer::Token, or just u64
```
Slice binds tighter than union, so `[]Token | error` is `([]Token) | error` (a
slice-or-error), not `[](Token | error)`. The `|` token is **reused** for union
(it is `Pipe`, the bitwise-OR token): this is not a lie (M.3) — the *position*
(`parse_type` vs `parse_expr`) decides the meaning, and both senses are "or"; the
parser always knows whether it is reading a type or an expression.

#### Lexer extension (over A.7)

```teko
// src/lexer/token.tks   (namespace 'teko::lexer') — type-expression tokens

type TokenKind = enum {
    // … (all prior tokens)
    LBracket        // [     NEW (slice open — 1 byte)
    RBracket        // ]     NEW (slice close — 1 byte)
    ColonColon      // ::    NEW (path separator — 2 bytes, maximal munch over `:` `:`)
}
```

```teko
// src/lexer/lexer.tks   (namespace 'teko::lexer') — additions

//   '[' → LBracket
//   ']' → RBracket
//   ':' → next ':' → ColonColon  | else Colon      (maximal munch — B.23)
//
// `:` (Colon, from A.4) and `::` (ColonColon) are distinguished by lookahead:
// a lone `:` (as in `x: T`) is Colon; `::` (as in `lexer::Token`) is ColonColon.
// Longest match always, so an annotation colon is never confused with a path `::`.
```

#### Parser — the type-expression AST and parser

```teko
// src/parser/type.tks   (namespace 'teko::parser') — the type AST (recursive, B.8)

// A path: one or more identifiers joined by `::` (lexer::Token, or just u64).
// Segments held as a list of small structs (avoids a slice-of-slices type).
type Segment = struct {
    name: str
}
type Path = struct {
    segments: []Segment      // at least one
}

// A named type: a (possibly qualified) type name. `u64` → one segment;
// `lexer::Token` → two segments.
type NamedType = struct {
    path: Path
}

// A slice type: `[]T`, where T is itself a type (so `[][]u8` nests). RECURSIVE
// (B.8 — the element is a TypeExpr; the compiler manages the indirection).
type SliceType = struct {
    element: TypeExpr
}

// A union type: `A | B | …` (two or more members). RECURSIVE (each member is a
// TypeExpr). Preserves the members as written (the checker gives the semantics).
type UnionType = struct {
    members: []TypeExpr      // two or more
}

// A type expression: exactly one of these (variant — B.14).
type TypeExpr = NamedType | SliceType | UnionType

// The result of parsing a type: the node + where parsing stopped (like Parsed,
// but for types). A distinct result struct (M.3 — a TypeExpr is not an Expr).
type ParsedType = struct {
    node: TypeExpr
    next: u64
}
```

```teko
// src/parser/type.tks   (namespace 'teko::parser') — recursive descent for types

// path = Ident ('::' Ident)*      (tightest — a qualified name)
fn parse_path(tokens: []lexer::Token, pos: u64) -> ParsedType | error {
    // the first segment: an Ident
    if !has_token(tokens, pos) {
        return error { message = "expected a type name" }
    }
    if kind_at(tokens, pos) != lexer::TokenKind::Ident {
        return error { message = "expected a type name" }
    }
    mut segs = teko::list::empty()
    segs = teko::list::push(segs, Segment { name = tokens[pos].text })
    mut p = pos + 1

    // further segments: ('::' Ident)*
    loop {
        if !has_token(tokens, p) { break }
        if kind_at(tokens, p) != lexer::TokenKind::ColonColon { break }
        // a '::' must be followed by an Ident
        let p_seg = match expect(tokens, p + 1, lexer::TokenKind::Ident, "expected a name after '::'") {
            error as e => return e
            u64 as np  => np
        }
        segs = teko::list::push(segs, Segment { name = tokens[p + 1].text })
        p = p_seg
    }

    let node = NamedType { path = Path { segments = segs } }
    ParsedType { node = node; next = p }
}

// slice = '[]' slice | path        (slice binds tighter than union)
// A '[' must be immediately followed by ']' (the seed has no fixed-size `[N]T`
// yet — that is a later layer); '[]' then a slice-or-path is the element.
fn parse_slice(tokens: []lexer::Token, pos: u64) -> ParsedType | error {
    // not a '[' → it is a path (a plain or qualified name)
    if !has_token(tokens, pos) {
        return error { message = "expected a type" }
    }
    if kind_at(tokens, pos) != lexer::TokenKind::LBracket {
        return parse_path(tokens, pos)
    }
    // '[' → demand ']' (empty brackets = a slice)
    let p_elem = match expect(tokens, pos + 1, lexer::TokenKind::RBracket, "expected ']' for a slice type") {
        error as e => return e
        u64 as np  => np
    }
    // the element: a slice-or-path (so `[][]T` nests; `|` is NOT consumed here —
    // union is looser, handled by parse_type above)
    let elem = match parse_slice(tokens, p_elem) {
        error as e => return e
        ParsedType as t => t
    }
    ParsedType { node = SliceType { element = elem.node }; next = elem.next }
}

// type = union = slice ('|' slice)*    (union loosest, left-associative)
// The entry point. A single slice with no '|' passes through as itself; a run of
// '|' collects the members into a UnionType.
fn parse_type(tokens: []lexer::Token, pos: u64) -> ParsedType | error {
    let first = match parse_slice(tokens, pos) {
        error as e => return e
        ParsedType as t => t
    }
    // no '|' follows → not a union, pass the single type through
    if !has_token(tokens, first.next) {
        return first
    }
    if kind_at(tokens, first.next) != lexer::TokenKind::Pipe {
        return first
    }
    // one or more '| slice' → build the union (first member + the rest)
    mut members = teko::list::empty()
    members = teko::list::push(members, first.node)
    mut p = first.next
    loop {
        if !has_token(tokens, p) { break }
        if kind_at(tokens, p) != lexer::TokenKind::Pipe { break }
        let m = match parse_slice(tokens, p + 1) {
            error as e => return e
            ParsedType as t => t
        }
        members = teko::list::push(members, m.node)
        p = m.next
    }
    ParsedType { node = UnionType { members = members }; next = p }
}
```

#### A.4's annotation, completed

A.4 modeled the optional type annotation as `has_type: bool` + `type_name: []u8`
(a single `Ident` stand-in, with a note that it would become a real type when
type expressions landed). They have landed — the annotation now holds a **parsed
type**:

```teko
// src/parser/ast.tks   (namespace 'teko::parser') — Binding's annotation upgraded

type Binding = struct {
    kind:      BindKind
    name:      str
    has_type:  bool         // was a `: T` annotation given? (still a presence flag —
                            // `T?` nullability is a later layer, so the optional is
                            // modeled with a flag until then; B.21/M.4)
    type_ann:  TypeExpr     // the annotation's PARSED type (when has_type) — was
                            // `type_name: []u8`, now a full type expression (A.8)
    value:     Expr
}

// parse_opt_annotation now calls parse_type for the type (instead of expecting a
// single Ident). The rest of its shape is unchanged (guard over nest):
fn parse_opt_annotation(tokens: []lexer::Token, pos: u64) -> Annotation | error {
    if !has_token(tokens, pos) {
        return Annotation { has_type = false; type_ann = unit_type(); next = pos }
    }
    if kind_at(tokens, pos) != lexer::TokenKind::Colon {
        return Annotation { has_type = false; type_ann = unit_type(); next = pos }
    }
    // ':' present → a TYPE EXPRESSION must follow (was: a single Ident)
    let t = match parse_type(tokens, pos + 1) {
        error as e => return e
        ParsedType as t => t
    }
    Annotation { has_type = true; type_ann = t.node; next = t.next }
}
```
(`Annotation`'s `type_name: []u8` becomes `type_ann: TypeExpr` to match; `unit_type()`
is a placeholder type for the no-annotation case — a single later concern, like the
`has_type` flag, resolved when nullability lands and the pair becomes `TypeExpr?`.)

**Notes for implementers (and the C23 port):**
- **Type expressions are a sub-grammar, parsed by recursive descent, like
  expressions.** `parse_type` (union) → `parse_slice` → `parse_path`. The
  precedence (slice tighter than union) is in the call structure (M.2). The C23
  port keeps the three functions, same names.
- **Slice binds tighter than union (the one precedence choice).** `[]Token | error`
  is `([]Token) | error`. `parse_slice`'s element recurses into `parse_slice` (not
  `parse_type`), so `|` is left for the union level — that is what keeps the slice
  from swallowing the union.
- **`|` is reused for union (M.3 — position, not magic).** In a type position it is
  the union separator; in an expression position it is bitwise OR. The parser is
  always in one grammar or the other, so there is no ambiguity and no lie — the
  token serves two related "or" meanings by position (as in Rust/TypeScript).
- **`::` builds paths (qualified names).** `path = Ident ('::' Ident)*`. A single
  segment is an unqualified name (`u64`); multiple are a qualified one
  (`lexer::Token`). The lexer distinguishes `:` (annotation, A.4) from `::` (path)
  by maximal munch.
- **This completes A.4's annotation.** `let x: []u8 = …` and `let r: u64 | error =
  …` now parse; the `Binding`'s `type_name: []u8` stand-in becomes a real
  `type_ann: TypeExpr`. The `has_type` flag remains until nullability (`T?`) lands
  (then `has_type` + `type_ann` collapse into `type_ann: TypeExpr?`).
- **Out of scope (later layers):** fixed-size arrays (`[N]T`), nullable (`T?`),
  generics (`List(T)`), and function types as first-class type expressions
  (`fn(...) -> ...` *as* a written type — the passing form 1 uses it; making it a
  parseable type expression can fold in with `fn`). These await their layers.
- **Now unblocked:** `fn` (next) — parameter types and return types are type
  expressions; `parse_type` is what makes `fn tokenize(source: []u8) -> []Token |
  error` parseable.

### A.9 — `fn`: function declaration and call

The big structure the last layers prepared for. A function is `fn name(params)
-> ret { body }` — typed parameters, a return type, and a body of statements.
It rests on everything built: parameter and return **types** are type expressions
(A.8), the **body** is a sequence of statements (A.3–A.7), and the value
expressions inside reference **variables** (A.6) and now **call** other functions.
It is also the fourth strand of the **assignment dimension** — a parameter
**receives** the caller's argument (an immutable binding — B.21). Per M.4, the
lexer extends first.

**Decisions (each by the laws):**
- **Comma-separated parameters in `()`.** `fn f(a: T, b: U)` — the comma is the
  list separator inside `()`, the convention used throughout the examples. This
  does **not** contradict B.26 (which bans the comma in `{}` — struct fields, match
  arms): `()` is a *positional list* (parameters, arguments) separated by commas;
  `{}` is a *block* (fields, arms, statements) separated by `;`/newline. Two
  delimiters, two separators, honestly distinct.
- **Required return type** (`-> ret`). Every seed function returns a value (the
  bootstrap compiler has no void procedure — computation is value-producing). So
  `-> ret` is always written (M.2 — explicit). No-return functions await a unit
  type (or an optional arrow) — a later type-layer decision.
- **Immutable parameters** (B.21). A parameter *receives* a value from the caller;
  it is not a local that creates/owns one, so it carries no `mut`. (The checker
  enforces this; the parser records the parameter without a `mut` slot.)
- **The body is a statement block; the return value is codegen's call.** The parser
  records the body as a list of statements (a *block* — parsed until `}`). That the
  function returns its **last expression** (or an explicit `return e`) is a
  semantic rule for the checker/codegen, not the parser — the same
  parser-structures / codegen-gives-semantics split as comparison chains and
  compound assignment.

#### Lexer extension (over A.8)

```teko
// src/lexer/token.tks   (namespace 'teko::lexer') — `fn` keyword + arrow token

type TokenKind = enum {
    // … (all prior tokens)
    Fn              // the keyword `fn`   (B.19 — a keyword)
    Arrow           // ->   NEW (return-type arrow — 2 bytes, maximal munch over `-` `>`)
    LBrace          // {    NEW (block open — if not already present)
    RBrace          // }    NEW (block close)
    Comma           // ,    NEW (positional-list separator, in `()`)
}
```

```teko
// src/lexer/lexer.tks   (namespace 'teko::lexer') — additions

// keyword table (B.19): `fn` joins let/mut/const.
//   keyword_or_ident: if bytes_eq(text, "fn") { return TokenKind::Fn }  …
//
// the `-` arm gains a lookahead for `->` (maximal munch — B.23):
//   '-' → next '>' → Arrow  | next '=' → MinusEq | else Minus
// (the `-=` compound from A.7 is still checked; `->` is the new longest match.)
//
// single-char tokens: '{' → LBrace, '}' → RBrace, ',' → Comma.
```

#### Parser — top-level items, function declaration, and calls

```teko
// src/parser/ast.tks   (namespace 'teko::parser') — function & item nodes; Call expr

// A parameter: a name and its type (a type expression — A.8). Immutable (B.21):
// no `mut` slot — a parameter receives, it does not create.
type Param = struct {
    name:     str
    type_ann: TypeExpr
}

// A function declaration: name, parameters, return type, and a body of statements.
type Function = struct {
    name:        str
    params:      []Param        // comma-separated in `()`; may be empty
    return_type: TypeExpr       // required (`-> ret`)
    body:        []Statement    // the block (parsed until `}`)
}

// A top-level item: a function declaration OR a statement (M.3 — declaring a
// function is not executing a statement; they are distinct kinds).
type Item = Function | Statement

// A program is now a sequence of ITEMS (was []Statement — functions are top-level).
type Program = struct {
    items: []Item
}

// NEW expression — a CALL: a (possibly qualified) callee applied to arguments.
// Completes parse_atom's pending case (A.6): a bare Ident is a Var; an Ident (or
// path) followed by `(` is a Call. Args are comma-separated expressions in `()`.
type Call = struct {
    callee: Path        // the function name (qualified allowed: lexer::foo) — A.8 Path
    args:   []Expr
}

// The expression node gains the Call case.
type Expr = Number | Var | Call | Binary | Unary | Compare
```

```teko
// src/parser/function.tks   (namespace 'teko::parser') — function declaration

// parse a parameter list `(p1: T1, p2: T2, …)` — comma-separated, possibly empty.
// Returns the params and the position after the ')'. FLAT (guard over nest).
fn parse_params(tokens: []lexer::Token, pos: u64) -> ParsedParams | error {
    // demand '('
    let p0 = match expect(tokens, pos, lexer::TokenKind::LParen, "expected '(' for parameters") {
        error as e => return e
        u64 as np  => np
    }
    mut params = teko::list::empty()
    mut p = p0

    // empty list: immediate ')'
    if has_token(tokens, p) {
        if kind_at(tokens, p) == lexer::TokenKind::RParen {
            return ParsedParams { params = params; next = p + 1 }
        }
    }

    // one or more `name: type`, separated by ','
    loop {
        // name
        if !has_token(tokens, p) {
            return error { message = "expected a parameter name" }
        }
        let pname = tokens[p].text
        let p_colon = match expect(tokens, p, lexer::TokenKind::Ident, "expected a parameter name") {
            error as e => return e
            u64 as np  => np
        }
        // ':'
        let p_type = match expect(tokens, p_colon, lexer::TokenKind::Colon, "expected ':' after parameter name") {
            error as e => return e
            u64 as np  => np
        }
        // type (a type expression — A.8)
        let t = match parse_type(tokens, p_type) {
            error as e => return e
            ParsedType as t => t
        }
        params = teko::list::push(params, Param { name = pname; type_ann = t.node })
        p = t.next

        // ',' → another parameter; ')' → done
        if !has_token(tokens, p) {
            return error { message = "expected ',' or ')' in parameters" }
        }
        if kind_at(tokens, p) == lexer::TokenKind::RParen { break }
        let p_next = match expect(tokens, p, lexer::TokenKind::Comma, "expected ',' between parameters") {
            error as e => return e
            u64 as np  => np
        }
        p = p_next
    }

    ParsedParams { params = params; next = p + 1 }   // past the ')'
}

// parse a function declaration: `fn name(params) -> ret { body }`.
// caller guarantees tokens[pos] is Fn.
fn parse_function(tokens: []lexer::Token, pos: u64) -> ParsedItem | error {
    // name
    let p_name = pos + 1
    if !has_token(tokens, p_name) {
        return error { message = "expected a function name" }
    }
    let name = tokens[p_name].text
    let p_params = match expect(tokens, p_name, lexer::TokenKind::Ident, "expected a function name") {
        error as e => return e
        u64 as np  => np
    }

    // parameters
    let pp = match parse_params(tokens, p_params) {
        error as e => return e
        ParsedParams as pp => pp
    }

    // '->' and the return type
    let p_ret = match expect(tokens, pp.next, lexer::TokenKind::Arrow, "expected '->' and a return type") {
        error as e => return e
        u64 as np  => np
    }
    let rt = match parse_type(tokens, p_ret) {
        error as e => return e
        ParsedType as t => t
    }

    // the body block `{ … }`
    let blk = match parse_block(tokens, rt.next) {
        error as e => return e
        ParsedBlock as b => b
    }

    let f = Function {
        name = name; params = pp.params
        return_type = rt.node; body = blk.statements
    }
    ParsedItem { node = f; next = blk.next }
}

// parse a block `{ statement* }` — statements until the closing '}'. Distinct from
// parse_program (which runs until end-of-input). FLAT (guard over nest).
fn parse_block(tokens: []lexer::Token, pos: u64) -> ParsedBlock | error {
    let p0 = match expect(tokens, pos, lexer::TokenKind::LBrace, "expected '{' to open a block") {
        error as e => return e
        u64 as np  => np
    }
    mut stmts = teko::list::empty()
    mut p = skip_terminators(tokens, p0)

    loop {
        if !has_token(tokens, p) {
            return error { message = "unterminated block (expected '}')" }
        }
        if kind_at(tokens, p) == lexer::TokenKind::RBrace { break }   // '}' → done

        let s = match parse_statement(tokens, p) {
            error as e => return e
            Parsed as s => s
        }
        stmts = teko::list::push(stmts, s.node)
        p = s.next

        // a terminator (Newline) or the closing '}' must follow
        if !has_token(tokens, p) {
            return error { message = "unterminated block (expected '}')" }
        }
        if kind_at(tokens, p) == lexer::TokenKind::RBrace { break }
        if kind_at(tokens, p) != lexer::TokenKind::Newline {
            return error { message = "expected end of line or '}' after statement" }
        }
        p = skip_terminators(tokens, p)
    }

    ParsedBlock { statements = stmts; next = p + 1 }   // past the '}'
}
```

```teko
// src/parser/program.tks   (namespace 'teko::parser') — top level dispatches items

// parse_program: a sequence of top-level ITEMS (functions or statements).
fn parse_program(tokens: []lexer::Token) -> Program | error {
    mut items = teko::list::empty()
    mut p = skip_terminators(tokens, 0)

    loop {
        if !has_token(tokens, p) { break }

        let it = match parse_item(tokens, p) {
            error as e => return e
            ParsedItem as it => it
        }
        items = teko::list::push(items, it.node)
        p = it.next

        if !has_token(tokens, p) { break }
        if kind_at(tokens, p) != lexer::TokenKind::Newline {
            return error { message = "expected end of line after a top-level item" }
        }
        p = skip_terminators(tokens, p)
    }

    Program { items = items }
}

// parse_item: `fn` → a function; otherwise → a statement (lifted into an Item).
fn parse_item(tokens: []lexer::Token, pos: u64) -> ParsedItem | error {
    if !has_token(tokens, pos) {
        return error { message = "expected a top-level item" }
    }
    if kind_at(tokens, pos) == lexer::TokenKind::Fn {
        return parse_function(tokens, pos)
    }
    // a top-level statement — parse it, lift it into an Item
    let s = match parse_statement(tokens, pos) {
        error as e => return e
        Parsed as s => s
    }
    ParsedItem { node = s.node; next = s.next }
}
```

```teko
// src/parser/expr.tks   (namespace 'teko::parser') — parse_atom completed with calls

// parse_atom's Ident arm now routes to parse_var_or_call (replacing A.6's direct
// parse_var_atom): the atom is a number, a variable-or-call, or a parenthesised
// expression. Only this one arm changes from A.6:
//     match kind_at(tokens, pos) {
//         lexer::TokenKind::Number => parse_number_atom(tokens, pos)
//         lexer::TokenKind::Ident  => parse_var_or_call(tokens, pos)   // was parse_var_atom (A.6)
//         lexer::TokenKind::LParen => parse_paren_atom(tokens, pos)
//         _                        => error { message = "expected a number, a name, or '('" }
//     }

// parse_atom's Ident case is now: a path, then — if a '(' follows — a CALL;
// otherwise a variable reference (A.6). The presence of '(' is the whole
// distinction (M.3 — reading a variable vs. calling a function).
fn parse_var_or_call(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    // the name (possibly qualified: lexer::foo) — reuse the type path reader's
    // shape, but at expression level we read a path of identifiers.
    let path = match parse_expr_path(tokens, pos) {
        error as e => return e
        ParsedPath as pth => pth
    }
    // no '(' → a variable reference (A.6). (For a multi-segment path with no '(',
    // that is a qualified value, e.g. an enum variant — recorded as a Var over the
    // path; the single-segment case is the ordinary variable.)
    if !has_token(tokens, path.next) {
        return Parsed { node = var_of_path(path.node); next = path.next }
    }
    if kind_at(tokens, path.next) != lexer::TokenKind::LParen {
        return Parsed { node = var_of_path(path.node); next = path.next }
    }
    // '(' → a call. Parse comma-separated argument expressions.
    let ca = match parse_args(tokens, path.next) {
        error as e => return e
        ParsedArgs as ca => ca
    }
    Parsed { node = Call { callee = path.node; args = ca.args }; next = ca.next }
}

// parse an argument list `(e1, e2, …)` — comma-separated expressions, possibly
// empty. Returns the args and the position after ')'. FLAT (guard over nest).
fn parse_args(tokens: []lexer::Token, pos: u64) -> ParsedArgs | error {
    let p0 = match expect(tokens, pos, lexer::TokenKind::LParen, "expected '(' for arguments") {
        error as e => return e
        u64 as np  => np
    }
    mut args = teko::list::empty()
    mut p = p0

    // empty list: immediate ')'
    if has_token(tokens, p) {
        if kind_at(tokens, p) == lexer::TokenKind::RParen {
            return ParsedArgs { args = args; next = p + 1 }
        }
    }

    loop {
        let a = match parse_expr(tokens, p) {
            error as e => return e
            Parsed as a => a
        }
        args = teko::list::push(args, a.node)
        p = a.next

        if !has_token(tokens, p) {
            return error { message = "expected ',' or ')' in arguments" }
        }
        if kind_at(tokens, p) == lexer::TokenKind::RParen { break }
        let p_next = match expect(tokens, p, lexer::TokenKind::Comma, "expected ',' between arguments") {
            error as e => return e
            u64 as np  => np
        }
        p = p_next
    }

    ParsedArgs { args = args; next = p + 1 }   // past the ')'
}
```

**Notes for implementers (and the C23 port):**
- **`fn` rests on the finished base (M.4).** Parameter and return types are type
  expressions (A.8); the body is a statement block (A.3–A.7); the values inside
  reference variables (A.6) and call functions (this layer). Nothing is built on an
  incomplete piece — the chain operators → variables → compound → types → `fn`
  held throughout.
- **Parameter binding is the assignment dimension's fourth strand (B.21).** A
  parameter *receives* the caller's argument — an immutable binding (no `mut`, the
  checker enforces it). Arguments at the call site are positional, comma-separated.
- **`()` lists use commas; `{}` blocks use `;`/newline (B.26).** Parameters and
  arguments are positional lists in `()` → commas. The body is a block in `{}` →
  newline-separated statements. The two delimiters carry two separator conventions,
  with no contradiction.
- **The block parser stops at `}` (distinct from `parse_program`'s end-of-input).**
  `parse_block` loops `parse_statement` until `}`, requiring a `Newline` (or the
  `}`) between statements (B.17). `parse_program` now loops *items* until
  end-of-input. The body's return value (last expression or explicit `return`) is
  the checker/codegen's rule — the parser only records the block.
- **A call completes `parse_atom` (the A.6 pending case).** A path followed by `(`
  is a `Call`; without `(` it is a `Var` (variable, or a qualified value such as an
  enum variant). The `(` is the whole distinction (M.3). `parse_expr_path` /
  `var_of_path` read an expression-level path (the analogue of A.8's type path).
- **`Program` is now `[]Item` (`Function | Statement`).** A function declaration is
  a top-level *item*, distinct from an executable statement (M.3). The C23 port
  realizes `Item` as a tagged union and `Function` as a record (name, params,
  return type, body block).
- **Out of scope (later/evolution):** closures and capture (`use`/`inject` —
  evolution, B.10), nested functions (banned, B.10), generics, default/named
  arguments, methods, and `fn(...) -> ...` written *as* a first-class type
  expression (the passing form 1 already types it; making it a parseable type can
  fold into A.8 when needed). State ref-less (B.7), error a value (B.1), no `?`
  (B.16), guard over nest — all as established.
- **With `fn`, the seed's core grammar is largely present:** the lexer, the
  expression parser (literals, variables, calls, all operators with full
  precedence), declarations (`const`/`let`/`mut` with typed annotations),
  reassignment (plain and compound), type expressions, and now function
  declarations and calls. What remains for the language's structures: `struct` /
  `variant` (type declarations — and with them destructuring), `match` (and its
  bindings), and `use` (imports — and aliases). Each is its own layer, lexer then
  parser, under the same norms.

### A.10 — Type declarations (`struct` / `enum` / `variant`)

The data structures the language is built from. A type declaration is a
**top-level item** (like `fn`) that introduces a named type. The three forms,
all used throughout the examples: a **`struct`** (a product — named fields), an
**`enum`** (a closed set of named members), and a **`variant`** (a sum — a union
of declared types). Per M.4, the lexer extends first.

**A `variant` reuses A.8's type parser — half this layer was already built.** A
variant declaration `type Expr = Number | Binary | Unary` is exactly
`type Name = <type>` where `<type>` is a **union** — which A.8's `parse_type`
already reads (union of paths). So only `struct` and `enum` bodies are new; the
variant case falls through to `parse_type`. This is why A.8 came first: the type
sub-grammar it built is the variant's right-hand side.

**Decisions (each by the laws):**
- **Separator inside `{}` is newline or `;`, never comma (B.26, absolute).** Struct
  fields and enum members are `;`/newline-separated — `struct { kind: TokenKind;
  text: []u8 }`. Comma is exclusive to `()`/`[]` (lists). **Governing Law: M.2**
  (one consistent rule for every `{}`) **+** B.26.
- **`variant` uses `|` and reuses the expression/type `Pipe` token.** `Number |
  Binary` — the same `|` as bitwise-OR and type-union, disambiguated by position
  (a type-declaration RHS). Not a lie (M.3): every sense is "or," and the parser
  always knows its context.
- **Nominal (B.13).** Each declared type is a distinct, named thing — `Token` is
  `Token`, not "any struct with these fields." **Governing Law: M.3** (a type is
  what it is named).
- **A type declaration is an item, not a statement (M.3).** Declaring a type is not
  executing a statement; `Item` gains the `TypeDecl` case alongside `Function`.

#### Lexer extension (over A.9)

```teko
// src/lexer/token.tks   (namespace 'teko::lexer') — type-declaration keywords

type TokenKind = enum {
    // … (all prior tokens)
    Type            // NEW: the keyword `type`   (B.19 — introduces a declaration)
    Struct          // NEW: the keyword `struct`
    Enum            // NEW: the keyword `enum`
}
```

```teko
// src/lexer/lexer.tks   (namespace 'teko::lexer') — keyword table grows

// keyword_or_ident (B.19): `type`, `struct`, `enum` join fn/let/mut/const.
//   if bytes_eq(text, "type")   { return TokenKind::Type }
//   if bytes_eq(text, "struct") { return TokenKind::Struct }
//   if bytes_eq(text, "enum")   { return TokenKind::Enum }
// No new symbols: `{` `}` (A.9), `:` (A.4), `|` (A.5), `::` (A.8) all exist.
```

#### Parser extension (over A.9 / A.8)

```teko
// src/parser/ast.tks   (namespace 'teko::parser') — type-declaration nodes

// A struct field: a name and its type (a type expression — A.8). Fields are
// `;`/newline-separated (B.26). (Type constants — `const MAX: i32 = 255` — are a
// later addition; this layer parses value fields.)
type Field = struct {
    name:     str
    type_ann: TypeExpr      // the field's type (A.8)
}

// The body of a type declaration: exactly one of the three forms (variant — B.14).
// - StructBody: named fields.
// - EnumBody:   named members (a closed set of names).
// - VariantBody: a union type (reuses A.8's TypeExpr — `Number | Binary`).
type StructBody = struct {
    fields: []Field
}
type EnumBody = struct {
    members: []str           // the member names, in order
}
type VariantBody = struct {
    type_expr: TypeExpr     // the union (A.8) — e.g. Number | Binary | Unary
}
type TypeBody = StructBody | EnumBody | VariantBody

// A type declaration: a name bound to a body. Nominal (B.13).
type TypeDecl = struct {
    name: str
    body: TypeBody
}

// A top-level item now has three cases (variant — B.14).
type Item = Function | TypeDecl | Statement
```

```teko
// src/parser/typedecl.tks   (namespace 'teko::parser') — the type declaration parser

// parse_type_decl: `type Name = <body>` where <body> is struct/enum/variant.
// caller guarantees tokens[pos] is Type. FLAT (guard over nest).
fn parse_type_decl(tokens: []lexer::Token, pos: u64) -> ParsedItem | error {
    // name
    let p_name = pos + 1
    if !has_token(tokens, p_name) {
        return error { message = "expected a type name after 'type'" }
    }
    let name = tokens[p_name].text
    let p_eq = match expect(tokens, p_name, lexer::TokenKind::Ident, "expected a type name after 'type'") {
        error as e => return e
        u64 as np  => np
    }
    // '='
    let p_body = match expect(tokens, p_eq, lexer::TokenKind::Assign, "expected '=' in a type declaration") {
        error as e => return e
        u64 as np  => np
    }
    // dispatch on the body's leading token
    let b = match parse_type_body(tokens, p_body) {
        error as e => return e
        ParsedBody as b => b
    }
    let decl = TypeDecl { name = name; body = b.node }
    ParsedItem { node = decl; next = b.next }
}

// parse_type_body: `struct {…}` | `enum {…}` | <type> (variant). The variant case
// reuses parse_type (A.8) — a variant is a union of declared types.
fn parse_type_body(tokens: []lexer::Token, pos: u64) -> ParsedBody | error {
    if !has_token(tokens, pos) {
        return error { message = "expected a type body (struct, enum, or a type)" }
    }
    match kind_at(tokens, pos) {
        lexer::TokenKind::Struct => parse_struct_body(tokens, pos)
        lexer::TokenKind::Enum   => parse_enum_body(tokens, pos)
        _                        => parse_variant_body(tokens, pos)
    }
}

// `struct { name: Type; … }` — `;`/newline-separated fields (B.26).
fn parse_struct_body(tokens: []lexer::Token, pos: u64) -> ParsedBody | error {
    // past `struct`, demand `{`
    let p0 = match expect(tokens, pos + 1, lexer::TokenKind::LBrace, "expected '{' after 'struct'") {
        error as e => return e
        u64 as np  => np
    }
    mut fields = teko::list::empty()
    mut p = skip_terminators(tokens, p0)

    loop {
        if !has_token(tokens, p) {
            return error { message = "unterminated struct (expected '}')" }
        }
        if kind_at(tokens, p) == lexer::TokenKind::RBrace { break }

        // a field: name ':' type
        let fname = tokens[p].text
        let p_colon = match expect(tokens, p, lexer::TokenKind::Ident, "expected a field name") {
            error as e => return e
            u64 as np  => np
        }
        let p_type = match expect(tokens, p_colon, lexer::TokenKind::Colon, "expected ':' after field name") {
            error as e => return e
            u64 as np  => np
        }
        let t = match parse_type(tokens, p_type) {
            error as e => return e
            ParsedType as t => t
        }
        fields = teko::list::push(fields, Field { name = fname; type_ann = t.node })
        p = t.next

        // terminator (newline/`;`) or closing `}`
        if !has_token(tokens, p) {
            return error { message = "unterminated struct (expected '}')" }
        }
        if kind_at(tokens, p) == lexer::TokenKind::RBrace { break }
        if kind_at(tokens, p) != lexer::TokenKind::Newline {
            return error { message = "expected end of line or '}' after a field" }
        }
        p = skip_terminators(tokens, p)
    }

    ParsedBody { node = StructBody { fields = fields }; next = p + 1 }   // past `}`
}

// `enum { Name; … }` — `;`/newline-separated member names (B.26).
fn parse_enum_body(tokens: []lexer::Token, pos: u64) -> ParsedBody | error {
    let p0 = match expect(tokens, pos + 1, lexer::TokenKind::LBrace, "expected '{' after 'enum'") {
        error as e => return e
        u64 as np  => np
    }
    mut members = teko::list::empty()
    mut p = skip_terminators(tokens, p0)

    loop {
        if !has_token(tokens, p) {
            return error { message = "unterminated enum (expected '}')" }
        }
        if kind_at(tokens, p) == lexer::TokenKind::RBrace { break }

        // a member: a bare name (an Ident)
        let mname = tokens[p].text
        let p_after = match expect(tokens, p, lexer::TokenKind::Ident, "expected an enum member name") {
            error as e => return e
            u64 as np  => np
        }
        members = teko::list::push(members, mname)
        p = p_after

        if !has_token(tokens, p) {
            return error { message = "unterminated enum (expected '}')" }
        }
        if kind_at(tokens, p) == lexer::TokenKind::RBrace { break }
        if kind_at(tokens, p) != lexer::TokenKind::Newline {
            return error { message = "expected end of line or '}' after an enum member" }
        }
        p = skip_terminators(tokens, p)
    }

    ParsedBody { node = EnumBody { members = members }; next = p + 1 }   // past `}`
}

// a variant body: a union type (A.8). `type Expr = Number | Binary | Unary`.
// Reuses parse_type entirely — a variant RHS *is* a union of declared types.
fn parse_variant_body(tokens: []lexer::Token, pos: u64) -> ParsedBody | error {
    let t = match parse_type(tokens, pos) {
        error as e => return e
        ParsedType as t => t
    }
    ParsedBody { node = VariantBody { type_expr = t.node }; next = t.next }
}
```

```teko
// src/parser/program.tks   (namespace 'teko::parser') — parse_item dispatches type decls

// parse_item: `fn` → a function; `type` → a type declaration; otherwise → a
// statement (lifted into an Item). One added arm over A.9.
fn parse_item(tokens: []lexer::Token, pos: u64) -> ParsedItem | error {
    if !has_token(tokens, pos) {
        return error { message = "expected a top-level item" }
    }
    match kind_at(tokens, pos) {
        lexer::TokenKind::Fn   => parse_function(tokens, pos)
        lexer::TokenKind::Type => parse_type_decl(tokens, pos)
        _                      => parse_stmt_item(tokens, pos)
    }
}
```

**Notes for implementers (and the C23 port):**
- **`variant` reuses the type parser (A.8) — the layer is mostly struct/enum.** The
  variant case is `parse_type` verbatim; only the `struct`/`enum` bodies are new
  parsing. A.8 (type expressions) was the prerequisite precisely so the variant
  RHS would already parse.
- **All three bodies are `{}`-delimited with `;`/newline separators (B.26).** No
  commas anywhere in a type declaration. The struct field is `name: Type`; the enum
  member is a bare name; the variant is a `|`-union (no braces — it is a type
  expression). The C23 port follows the same separator discipline.
- **`Item` is now `Function | TypeDecl | Statement` (B.14).** A file is a sequence
  of items; `parse_program` (A.9) loops `parse_item`, which now dispatches `type`.
  The C23 port realizes `Item` and `TypeBody` as tagged unions.
- **Type constants (`const MAX: i32 = 255` inside a struct) are a later addition.**
  This layer parses value fields; const members (accessed `Node::MAX`) and methods
  await their layers. The parser records structure; the checker validates field
  types and member uniqueness.
- **State ref-less (B.7), error a value (B.1), no `?` (B.16), guard over nest** —
  all as established. Next: **destructuring** — `let { x; y } = point` (B.26: braces,
  `;`/newline), the fifth strand of the assignment dimension, now unblocked (structs
  exist to destructure).

### A.11 — Destructuring bindings (`let { x; y } = point`)

The fifth strand of the **assignment dimension**: binding several names at once
by **selecting fields from a struct**. `let { x; y } = point` binds `x` and `y`
to the fields of `point` *by name*. It rests on structs (A.10 — there must be a
struct to destructure) and is the natural completion of the binding family (A.4).
Per M.4, no lexer change is needed (`{` `}` `;` and the binding keywords all
exist).

**Decisions (each by the laws):**
- **Braces `{}` with `;`/newline separators (B.26, absolute).** The pattern is
  `{ x; y }` — not `(x, y)`. (B.3 once wrote `(x, y)`; **B.26 superseded it** —
  the "`{}` = newline/`;`, no comma" rule is absolute, and destructuring is
  explicitly one of its cases.) **Governing Law: M.2** (one separator rule for all
  `{}`) **+** B.26.
- **Nominal partial selection — list only the fields you want; omitted ones are
  simply ignored (B.13).** No `..` or `_` for the rest (better than Rust's `{ x,
  .. }`). Selection is **by name, not position** — `{ y; x }` binds the same as
  `{ x; y }`. **Governing Law: M.3** (the name carries the meaning, not a position)
  **+** B.13 (structs are nominal, so a field name resolves unambiguously).
- **The binding target becomes a *pattern* (M.3 — honest about what is bound).** A
  binding is either a **simple name** (`let x = …`, A.4) or a **destructuring
  pattern** (`let { x; y } = …`). They are distinct target shapes, told apart by
  the leading token after the keyword: an `Ident` → a simple name; a `{` → a
  pattern.
- **All three binding kinds destructure (B.21).** `let`/`mut`/`const` may each take
  a destructuring target; mutability still governs the *whole* binding (the bound
  names are mutable iff `mut`). The parser records the pattern; the checker binds
  each name with the binding's mutability.

#### Parser extension (over A.10 / A.4)

No lexer change. The `Binding` node (A.4) gains a target that is either a name or
a pattern; one new parser reads the pattern.

```teko
// src/parser/ast.tks   (namespace 'teko::parser') — the binding target becomes a pattern

// A destructuring pattern: the field names selected from a struct value. Partial
// nominal selection (B.13) — only the listed names; omitted fields ignored.
type DestructurePattern = struct {
    names: []str             // the selected field names (each binds a local)
}

// The target of a binding: a simple name (A.4) OR a destructuring pattern. Distinct
// shapes (M.3 — binding one name vs. selecting several).
type BindTarget = SimpleName | DestructurePattern

type SimpleName = struct {
    name: str
}

// Binding (A.4) now holds a BindTarget instead of a bare `name: str`. The kind,
// the optional type annotation, and the value are unchanged. (For a destructuring
// target, the annotation is the value's struct type, when given.)
type Binding = struct {
    kind:      BindKind         // let | mut | const
    target:    BindTarget       // a simple name OR a destructuring pattern
    has_type:  bool
    type_name: []u8
    value:     Expr
}
```

```teko
// src/parser/binding.tks   (namespace 'teko::parser') — target parsing added

// parse_bind_target: after the binding keyword, the target is a name or a `{…}`
// pattern. Dispatch on the leading token. FLAT (guard over nest).
fn parse_bind_target(tokens: []lexer::Token, pos: u64) -> ParsedTarget | error {
    if !has_token(tokens, pos) {
        return error { message = "expected a name or '{' after the binding keyword" }
    }
    match kind_at(tokens, pos) {
        lexer::TokenKind::Ident  => parse_simple_target(tokens, pos)
        lexer::TokenKind::LBrace => parse_destructure_target(tokens, pos)
        _                        => error { message = "expected a name or '{' to bind" }
    }
}

// a simple name target (A.4's case): one identifier.
fn parse_simple_target(tokens: []lexer::Token, pos: u64) -> ParsedTarget | error {
    let name = tokens[pos].text
    ParsedTarget { node = SimpleName { name = name }; next = pos + 1 }
}

// a destructuring pattern `{ x; y }` — `;`/newline-separated field names (B.26),
// nominal partial selection (B.13). FLAT.
fn parse_destructure_target(tokens: []lexer::Token, pos: u64) -> ParsedTarget | error {
    // pos is `{`. The `{}` field-name reader is shared with the match field pattern
    // (A.14) — one reader, `parse_field_names` (A.17). This target just wraps the names
    // into a `DestructurePattern` (the binding form).
    let fns = match parse_field_names(tokens, pos) {
        error as e => return e
        ParsedNames as fns => fns
    }
    let pat = DestructurePattern { names = fns.names }
    ParsedTarget { node = pat; next = fns.next }
}

// parse_binding (A.4) now reads a target instead of a bare name. Only the target
// step changes; the optional annotation, the `=`, and the value are as A.4 (and
// the whole function stays FLAT via the guard pattern).
fn parse_binding(tokens: []lexer::Token, pos: u64) -> ParsedStmt | error {
    let kind = bind_kind_of(kind_at(tokens, pos))

    // target: a name or a destructuring pattern (NEW — replaces A.4's name step)
    let tg = match parse_bind_target(tokens, pos + 1) {
        error as e => return e
        ParsedTarget as tg => tg
    }

    // optional type annotation, then `=`, then the value — exactly as A.4
    let ann = match parse_opt_annotation(tokens, tg.next) {
        error as e => return e
        Annotation as a => a
    }
    let p_val = match expect(tokens, ann.next, lexer::TokenKind::Assign, "expected '=' after the binding target") {
        error as e => return e
        u64 as np  => np
    }
    let v = match parse_expr(tokens, p_val) {
        error as e => return e
        Parsed as v => v
    }
    let b = Binding {
        kind = kind; target = tg.node
        has_type = ann.has_type; type_name = ann.type_name
        value = v.node
    }
    ParsedStmt { node = b; next = v.next }
}
```

**Notes for implementers (and the C23 port):**
- **No lexer change.** `{` `}` (A.9), `;`/newline (A.3), and the binding keywords
  (A.3/A.4) all exist. Destructuring is purely a parser extension to the binding
  target.
- **The target is a pattern, told apart by the leading token (M.3).** After the
  keyword: an `Ident` is a simple name; a `{` opens a destructuring pattern. A.4's
  `name: []u8` becomes `target: BindTarget` (`SimpleName | DestructurePattern`).
  The C23 port makes `BindTarget` a tagged union; existing simple-name handling
  becomes the `SimpleName` case.
- **Partial nominal selection (B.13) — record the names, the checker resolves them.**
  The parser lists the selected field names; the checker verifies each names a real
  field of the value's struct type and binds it with the binding's mutability
  (B.21). Omitted fields need no marker — they are simply not bound.
- **Separators are `;`/newline (B.26), never comma.** The pattern `{ x; y }` matches
  every other `{}` in the language. A comma here would be a B.26 violation.
- **This completes the binding family's targets.** With it, the assignment
  dimension has five strands done: statement bindings, variables, compound,
  arguments, and now destructuring. The remaining strands are **match-bindings**
  (with `match`) and **aliases** (with `use`). State ref-less (B.7), error a value
  (B.1), no `?` (B.16), guard over nest — all as established.

### A.12 — The `if` expression and `return`

The first control flow. Per **B.20**, `if`/`else if`/`else` **is an expression** —
it returns the value of the chosen branch — so a function ending in `if` returns
that value (no `return` needed), and `let m = if a > b { a } else { b }` captures
it. It is *the legible ternary* (no `?:`). `return` is the **early-exit**
statement (already used throughout the examples — the guard pattern's `return e`).
Per M.4, the lexer extends first.

**Decisions (each by the laws):**
- **`if` is an expression (B.20).** It yields the chosen branch's value, so it
  enters at the **atom** level of expressions (like a parenthesised expression).
  Used as a statement (`if c { do }`), the value is simply discarded (an
  expression-statement). **Governing Law: M.2** (the if-expression carries the
  ternary's capability with words and blocks) **+** B.20.
- **`else` is mandatory when the value is used; optional as a statement (B.20).**
  An `if` without `else` that is asked for a value would have a valueless false
  branch — the same completeness the match's exhaustiveness enforces. The parser
  records whether an `else` is present; the checker requires it where the value is
  consumed. **Governing Law: M.1** (no missing-value path).
- **`else if` chains.** `else if` is an `else` whose body is another `if` — no new
  construct, just nesting, parsed by recursion.
- **No ternary `?:` (B.20).** The cryptic syntax is abolished; the capability lives
  in the if-expression. (The `?` stays reserved for nullability.)
- **`return e` is early exit only (B.20).** Normal return is the block's last
  expression; `return` is for leaving early. It is a statement carrying a value (the
  seed's functions all return values — A.9).

#### Lexer extension (over A.11)

```teko
// src/lexer/token.tks   (namespace 'teko::lexer') — control-flow keywords

type TokenKind = enum {
    // … (all prior tokens)
    If              // NEW: the keyword `if`     (B.20 — conditional)
    Else            // NEW: the keyword `else`
    Return          // NEW: the keyword `return` (B.20 — early exit)
}
```

```teko
// src/lexer/lexer.tks   (namespace 'teko::lexer') — keyword table grows

// keyword_or_ident (B.19): `if`, `else`, `return` join the table.
//   if bytes_eq(text, "if")     { return TokenKind::If }
//   if bytes_eq(text, "else")   { return TokenKind::Else }
//   if bytes_eq(text, "return") { return TokenKind::Return }
// No new symbols: `{` `}` (A.9) delimit the branch blocks.
```

#### Parser extension (over A.11 / A.9 / A.5)

```teko
// src/parser/ast.tks   (namespace 'teko::parser') — If expression, Return statement

// An if-expression: a condition, a `then` block, and an optional `else`. The
// `else` may be another block OR another if (`else if` — recursion). Since it is
// an EXPRESSION, it yields a value (the chosen branch's last expression).
type IfExpr = struct {
    cond:     Expr              // the boolean condition
    then_blk: []Statement       // the `then` branch (a block)
    has_else: bool              // was an `else`/`else if` present?
    else_blk: []Statement       // the `else` branch block (when has_else)
}

// The expression node gains the If case (it is an atom-level expression).
type Expr = Number | Var | Call | IfExpr | Binary | Unary | Compare

// A return statement: `return e` — early exit carrying a value (B.20).
type Return = struct {
    value: Expr
}

// Statement gains the Return case (Binding | Assign | Return | ExprStmt).
type Statement = Binding | Assign | Return | ExprStmt
```

```teko
// src/parser/expr.tks   (namespace 'teko::parser') — parse_atom gains the `if` case

// parse_atom's dispatch gains an `If` arm (the if-expression is an atom):
//     match kind_at(tokens, pos) {
//         lexer::TokenKind::Number => parse_number_atom(tokens, pos)
//         lexer::TokenKind::Ident  => parse_var_or_call(tokens, pos)
//         lexer::TokenKind::LParen => parse_paren_atom(tokens, pos)
//         lexer::TokenKind::If     => parse_if(tokens, pos)            // NEW
//         _                        => error { message = "expected a value" }
//     }

// parse_if: `if cond { then } [else { else } | else if …]`. FLAT (guard over
// nest). Returns a Parsed whose node is an IfExpr.
fn parse_if(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    // pos is `if`; parse the condition (a full expression)
    let c = match parse_expr(tokens, pos + 1) {
        error as e => return e
        Parsed as c => c
    }
    // the `then` block
    let then_b = match parse_block(tokens, c.next) {
        error as e => return e
        ParsedBlock as b => b
    }

    // no `else` → an if without else (value-use requires else; checker enforces)
    if !has_token(tokens, then_b.next) {
        return Parsed { node = if_no_else(c.node, then_b.statements); next = then_b.next }
    }
    if kind_at(tokens, then_b.next) != lexer::TokenKind::Else {
        return Parsed { node = if_no_else(c.node, then_b.statements); next = then_b.next }
    }

    // `else` present → either `else if …` (recursion) or `else { … }`
    let p_after_else = then_b.next + 1
    if !has_token(tokens, p_after_else) {
        return error { message = "expected '{' or 'if' after 'else'" }
    }

    // `else if` → the else branch is a single if-expression, wrapped as a block
    if kind_at(tokens, p_after_else) == lexer::TokenKind::If {
        let elif = match parse_if(tokens, p_after_else) {
            error as e => return e
            Parsed as ei => ei
        }
        let node = if_with_else(c.node, then_b.statements, block_of_expr(elif.node))
        return Parsed { node = node; next = elif.next }
    }

    // `else { … }` → an ordinary else block
    let else_b = match parse_block(tokens, p_after_else) {
        error as e => return e
        ParsedBlock as b => b
    }
    let node = if_with_else(c.node, then_b.statements, else_b.statements)
    Parsed { node = node; next = else_b.next }
}
```

```teko
// src/parser/statement.tks   (namespace 'teko::parser') — return statement

// parse_statement gains a `return` arm (dispatch on the leading token, B.15):
//     if k == lexer::TokenKind::Return { return parse_return(tokens, pos) }
// (placed among the binding-keyword checks, before the reassignment/expr cases.)

// parse_return: `return e` — early exit carrying a value. FLAT.
fn parse_return(tokens: []lexer::Token, pos: u64) -> ParsedStmt | error {
    // pos is `return`; parse the value expression
    let v = match parse_expr(tokens, pos + 1) {
        error as e => return e
        Parsed as v => v
    }
    let r = Return { value = v.node }
    ParsedStmt { node = r; next = v.next }
}
```

**Notes for implementers (and the C23 port):**
- **`if` is an expression at the atom level (B.20).** `parse_atom` gains the `If`
  arm; the if-expression yields a value, so `let m = if … { … } else { … }` and
  `fn max(...) -> i32 { if a > b { a } else { b } }` both work. A statement-level
  `if c { … }` is an expression-statement whose value is discarded. The C23 port
  adds the `If` case to the expression union and the `parse_atom` branch.
- **`else if` is recursion, not a new construct.** The `else` branch, when it leads
  with `if`, is parsed by calling `parse_if` again and wrapping that if-expression
  as the else block. `if_no_else`/`if_with_else`/`block_of_expr` are small builders.
- **`else` presence is recorded; the checker enforces it where the value is used
  (B.20/M.1).** The parser does not reject an else-less `if` (it is valid as a
  statement); the checker requires `else` only when the value is consumed — the
  same parser-structures / checker-gives-semantics split used throughout.
- **`return e` is an early-exit statement (B.20).** The block's last expression is
  the normal return (A.9's body model); `return` leaves early with a value. The
  C23 port adds the `Return` case to the statement union and the `parse_return`
  branch (dispatched on the `return` keyword).
- **State ref-less (B.7), error a value (B.1), no `?` (B.16), guard over nest** —
  all as established. Next: **`loop` + `break`/`continue`** (iteration), then
  **`match`** (the sixth assignment strand).

### A.13 — The `loop` and `break` / `continue`

The iteration primitive. In the seed, **`loop { … }` is the only loop** — an
infinite loop whose exit is explicit: `if cond { break }` (B.20 — the postfix
`break when` was cut). `continue` skips to the next turn. The idiomatic
`loop x from collection` (iteration over a value) is **evolution** (sugar over
`loop`); the seed has the bare primitive. Per M.4, the lexer extends first.

**Decisions (each by the laws):**
- **`loop {}` is the sole primitive loop (M.5).** No `while`, no `for` in the seed —
  one looping construct, exited explicitly. `while cond { … }` is `loop { if !cond
  { break } … }`; the seed keeps the irreducible primitive and lets the rest be
  evolution sugar. **Governing Law: M.5** (one primitive earns its weight; the
  conveniences justify theirs later) **+ M.0** (a loop is a backward jump — the
  primitive maps to it directly).
- **Exit is `if cond { break }`, not postfix (B.20).** The conditional-statement
  job belongs to `if`; a postfix `break when` would duplicate it. `break` and
  `continue` are bare statements. **Governing Law: B.20** (the closed conditional
  web — no redundant postfix forms).
- **`break`/`continue` are statements (M.3).** They alter control flow; they are
  not expressions and yield no value. Each is a single keyword (no label in the
  seed — nested-loop labels are evolution, unused by the bootstrap compiler).
- **`loop x from collection` is evolution.** Iteration over a value is sugar that
  lowers to `loop` + an index/iterator; the seed writes the explicit form. (Recorded
  so the primitive stays minimal — M.5.)

#### Lexer extension (over A.12)

```teko
// src/lexer/token.tks   (namespace 'teko::lexer') — loop-control keywords

type TokenKind = enum {
    // … (all prior tokens)
    Loop            // NEW: the keyword `loop`     (B.20 — the loop primitive)
    Break           // NEW: the keyword `break`
    Continue        // NEW: the keyword `continue`
}
```

```teko
// src/lexer/lexer.tks   (namespace 'teko::lexer') — keyword table grows

// keyword_or_ident (B.19): `loop`, `break`, `continue` join the table.
//   if bytes_eq(text, "loop")     { return TokenKind::Loop }
//   if bytes_eq(text, "break")    { return TokenKind::Break }
//   if bytes_eq(text, "continue") { return TokenKind::Continue }
// No new symbols: `{` `}` (A.9) delimit the loop body.
```

#### Parser extension (over A.12 / A.9)

```teko
// src/parser/ast.tks   (namespace 'teko::parser') — Loop, Break, Continue statements

// A loop: an infinite loop over a body block. Exit is an inner `if c { break }`
// (B.20). No condition slot — the primitive is unconditional; conditional exit is
// a statement inside the body.
type LoopStmt = struct {
    body: []Statement       // the loop body (a block)
}

// break / continue carry no data — they are bare control-flow statements (M.3).
type BreakStmt = struct {
}
type ContinueStmt = struct {
}

// Statement gains the three cases (Binding | Assign | Return | Loop | Break |
// Continue | ExprStmt).
type Statement = Binding | Assign | Return | LoopStmt | BreakStmt | ContinueStmt | ExprStmt
```

```teko
// src/parser/statement.tks   (namespace 'teko::parser') — loop & jump statements

// parse_statement gains three arms (dispatch on the leading token, B.15):
//     if k == lexer::TokenKind::Loop     { return parse_loop(tokens, pos) }
//     if k == lexer::TokenKind::Break    { return parse_break(tokens, pos) }
//     if k == lexer::TokenKind::Continue { return parse_continue(tokens, pos) }
// (placed among the other statement-keyword checks.)

// parse_loop: `loop { body }`. Reuses parse_block (A.9) for the body. FLAT.
fn parse_loop(tokens: []lexer::Token, pos: u64) -> ParsedStmt | error {
    // pos is `loop`; the body block follows
    let b = match parse_block(tokens, pos + 1) {
        error as e => return e
        ParsedBlock as b => b
    }
    let l = LoopStmt { body = b.statements }
    ParsedStmt { node = l; next = b.next }
}

// parse_break: the bare `break` keyword. No value, no sub-parse.
fn parse_break(tokens: []lexer::Token, pos: u64) -> ParsedStmt | error {
    ParsedStmt { node = BreakStmt {}; next = pos + 1 }
}

// parse_continue: the bare `continue` keyword.
fn parse_continue(tokens: []lexer::Token, pos: u64) -> ParsedStmt | error {
    ParsedStmt { node = ContinueStmt {}; next = pos + 1 }
}
```

**Notes for implementers (and the C23 port):**
- **`loop {}` is the only primitive loop (M.5).** The parser records an
  unconditional loop over a body; conditional exit is an `if c { break }` statement
  *inside* the body (B.20), parsed by the A.12 if-machinery. The C23 port emits a
  backward jump with the break as a forward jump out (the metal mapping — M.0).
- **`break`/`continue` are bare statements (M.3).** They parse to empty nodes
  (`BreakStmt {}`/`ContinueStmt {}`) — no value, no label (labels are evolution).
  The C23 port adds both cases to the statement union; each consumes exactly one
  token.
- **The body reuses `parse_block` (A.9).** A loop body is a block, the same `{ … }`
  with `;`/newline-separated statements used by function bodies — no new block
  machinery. This is why blocks were built with `fn`: they serve every body.
- **`while`/`for`/`loop…from` are evolution.** The seed writes the explicit form
  (`loop { if cond { break } … }`); the sugars lower to it. Recorded so the seed
  primitive stays minimal.
- **State ref-less (B.7), error a value (B.1), no `?` (B.16), guard over nest** —
  all as established. Next: **`match`** — the pattern-discrimination construct and
  the sixth strand of the assignment dimension (match-bindings).

### A.14 — `match`: pattern discrimination and match-bindings

> ⚠️ **Partially superseded — `AltPattern` axis (↺).** This entry originally annotated
> `AltPattern` as *value axis only* and described an axis-exclusive parse. The C7 tribunal
> + the legislator **redefined** it: an `Alt` option may be a value pattern **or a bare
> variant case** (`RED | GREEN`), counting toward variant-axis exhaustiveness; **bindings
> inside an `Alt` are forbidden**. See the Redefinitions Index (`AltPattern` axis) and the
> C7 Alt-axis block. The rest of A.14 stands.

The signature control construct, and the **sixth strand of the assignment
dimension** (match-bindings — a name receives a value by being matched). Per
**B.15**, one `match` covers two axes: matching a **value** (a scalar — literals,
ranges, alternatives) and matching a **variant** (which type — binding the
revealed content). It is an **expression** (B.15 — `let scan = match c { … }`), so
it enters at the atom level like `if`. Exhaustiveness is **forced**, but that is
the checker's verdict; the parser only structures the arms. Per M.4, the lexer
extends first.

**The field-selection pattern reuses A.11's machine — the strand was prepared.**
A variant arm `Binary { left; right }` selects fields by name from the matched
value — exactly the nominal partial selection (`{}`, `;`/newline, omitted fields
ignored — B.26/B.13) that A.11 built for destructuring bindings. The match's
field pattern *is* a destructure pattern; A.11 prepared the sixth strand without
knowing it (the M.4 compounding again).

**Decisions (each by the laws):**
- **One `match`, two axes (B.15).** Same shape `match subject { pattern => body }`;
  the subject decides whether a pattern matches a value or a variant — the compiler
  knows which. Not two constructs. **Governing Law: M.5** (one construct, not two)
  **+ M.2** (the subject makes the axis legible).
- **Value patterns: literal, range `a..=b`, alternatives `a | b` (B.15).** Binding
  is rare on this axis (you already hold the value); when wanted, `'0'..='9' as
  digit`. The `|` reuses `Pipe` (position disambiguates — M.3). The `..=` is the
  inclusive range token.
- **Variant patterns: `Type as n` (whole) or `Type { f; g }` (select — B.15/B.26).**
  Binding is intrinsic on this axis (revealing the type means accessing it). Field
  selection reuses A.11's pattern machine. **Governing Law: M.3** (the match reveals
  the hidden content by naming it) **+** B.13 (nominal selection).
- **`when` guards (B.20 — exclusively here).** An arm may carry `when cond`
  conditioning it beyond the pattern. The parser records the optional guard; it does
  **not** evaluate coverage. **Governing Law: B.20** (`when` is only the match guard).
- **`_` is the optional catch-all (B.15).** Available always, mandatory never —
  needed only when explicit coverage is incomplete. The parser records a `_` arm;
  the checker decides if it is necessary or dead.
- **Exhaustiveness is the checker's, not the parser's (M.3).** The parser structures
  the arms (pattern, optional `when`, body) and records which carry `when` and which
  is `_`; the checker forces coverage with the `when`-subtlety (a guarded arm does
  not count). Same parser-structures / checker-gives-semantics split as comparison
  chains, compound, and `if`.

#### Lexer extension (over A.13)

```teko
// src/lexer/token.tks   (namespace 'teko::lexer') — match keywords and arm/range tokens

type TokenKind = enum {
    // … (all prior tokens)
    Match           // NEW: the keyword `match`   (B.15)
    When            // NEW: the keyword `when`     (B.20 — guard)
    As              // NEW: the keyword `as`       (bind)
    FatArrow        // NEW: `=>`  (match-arm arrow — distinct from `->`, B.20)
    DotDotEq        // NEW: `..=` (inclusive range, 3 bytes)
    // (Underscore already exists — A.1.)
}
```

```teko
// src/lexer/lexer.tks   (namespace 'teko::lexer') — keywords + maximal munch for =>, ..=

// keyword_or_ident (B.19): `match`, `when`, `as` join the table.
//   if bytes_eq(text, "match") { return TokenKind::Match }
//   if bytes_eq(text, "when")  { return TokenKind::When }
//   if bytes_eq(text, "as")    { return TokenKind::As }
//
// new symbol look-ahead (maximal munch — B.23):
//   '=' → next '>' → FatArrow | next '=' → EqEq | else Assign
//   '.' → next '.' → (then next '=' → DotDotEq | else error "expected '=' in range")
// (the `=>` arm joins the `=` dispatch beside `==`; `..=` is a 3-byte range token.)
```

#### Parser extension (over A.13 / A.11 / A.9)

```teko
// src/parser/ast.tks   (namespace 'teko::parser') — match nodes; patterns

// A pattern: the left of an arm. Variant (B.14) over the pattern kinds.
// - LiteralPattern: a scalar literal (a number/char/string token).
// - RangePattern:   `lo ..= hi` (inclusive).
// - AltPattern:     `a | b | …` (alternatives — value patterns OR bare variant cases; ↺ C7, see Redefinitions Index). Bindings inside an Alt are forbidden.
// - BindPattern:    `Type as name` (variant axis — bind the whole value).
// - FieldPattern:   `Type { f; g }` (variant axis — select fields; reuses A.11).
// - WildcardPattern: `_` (the optional catch-all).
type LiteralPattern = struct {
    value: Expr             // the literal (a Number/atom)
}
type RangePattern = struct {
    lo: Expr
    hi: Expr
}
type AltPattern = struct {
    options: []Pattern      // each alternative (recursive — patterns of patterns)
}
type BindPattern = struct {
    type_name: Path         // the variant case (qualified allowed) — A.8 path
    binding:   str         // the bound name (`as name`)
}
type FieldPattern = struct {
    type_name: Path         // the variant case
    fields:    []str         // the selected field names (reuses A.11's name list)
}
type WildcardPattern = struct {
}
type Pattern = LiteralPattern | RangePattern | AltPattern | BindPattern | FieldPattern | WildcardPattern

// An arm: a pattern, an optional `when` guard, and a body (an expression). The
// body is an expression because `match` is an expression (each arm yields a value).
type Arm = struct {
    pattern:  Pattern
    has_when: bool          // a `when` guard present? (does NOT count for exhaustiveness)
    guard:    Expr          // the guard condition (when has_when)
    body:     Expr          // the arm's value
}

// A match expression: a subject and the arms (`;`/newline-separated — B.26).
type MatchExpr = struct {
    subject: Expr
    arms:    []Arm
}

// The expression node gains the Match case (an atom-level expression, like If).
type Expr = Number | Var | Call | IfExpr | MatchExpr | Binary | Unary | Compare
```

```teko
// src/parser/match.tks   (namespace 'teko::parser') — the match parser

// parse_atom gains a `Match` arm (the match-expression is an atom, like `if`):
//     lexer::TokenKind::Match => parse_match(tokens, pos)

// parse_match: `match subject { arm; arm; … }`. FLAT (guard over nest).
fn parse_match(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    // pos is `match`; the subject expression
    let subj = match parse_expr(tokens, pos + 1) {
        error as e => return e
        Parsed as s => s
    }
    // `{`
    let p0 = match expect(tokens, subj.next, lexer::TokenKind::LBrace, "expected '{' after the match subject") {
        error as e => return e
        u64 as np  => np
    }
    mut arms = teko::list::empty()
    mut p = skip_terminators(tokens, p0)

    loop {
        if !has_token(tokens, p) {
            return error { message = "unterminated match (expected '}')" }
        }
        if kind_at(tokens, p) == lexer::TokenKind::RBrace { break }

        let arm = match parse_arm(tokens, p) {
            error as e => return e
            ParsedArm as a => a
        }
        arms = teko::list::push(arms, arm.node)
        p = arm.next

        // terminator (newline/`;`) or closing `}`
        if !has_token(tokens, p) {
            return error { message = "unterminated match (expected '}')" }
        }
        if kind_at(tokens, p) == lexer::TokenKind::RBrace { break }
        if kind_at(tokens, p) != lexer::TokenKind::Newline {
            return error { message = "expected end of line or '}' after an arm" }
        }
        p = skip_terminators(tokens, p)
    }

    let m = MatchExpr { subject = subj.node; arms = arms }
    Parsed { node = m; next = p + 1 }   // past `}`
}

// parse_arm: `pattern [when guard] => body`. FLAT.
fn parse_arm(tokens: []lexer::Token, pos: u64) -> ParsedArm | error {
    // the pattern
    let pat = match parse_pattern(tokens, pos) {
        error as e => return e
        ParsedPattern as pt => pt
    }

    // optional `when guard`
    let g = match parse_opt_guard(tokens, pat.next) {
        error as e => return e
        Guard as g => g
    }

    // `=>`
    let p_body = match expect(tokens, g.next, lexer::TokenKind::FatArrow, "expected '=>' in a match arm") {
        error as e => return e
        u64 as np  => np
    }
    // the body (an expression — match is an expression)
    let b = match parse_expr(tokens, p_body) {
        error as e => return e
        Parsed as b => b
    }
    let arm = Arm {
        pattern = pat.node
        has_when = g.has_when; guard = g.guard
        body = b.node
    }
    ParsedArm { node = arm; next = b.next }
}

// parse_opt_guard: an optional `when <expr>`. Pass-through if no `when`. FLAT.
fn parse_opt_guard(tokens: []lexer::Token, pos: u64) -> Guard | error {
    if !has_token(tokens, pos) {
        return Guard { has_when = false; guard = no_expr(); next = pos }
    }
    if kind_at(tokens, pos) != lexer::TokenKind::When {
        return Guard { has_when = false; guard = no_expr(); next = pos }
    }
    let c = match parse_expr(tokens, pos + 1) {
        error as e => return e
        Parsed as c => c
    }
    Guard { has_when = true; guard = c.node; next = c.next }
}
```

```teko
// src/parser/pattern.tks   (namespace 'teko::parser') — the pattern parser

// parse_pattern: dispatch on the leading token. `_` → wildcard; a literal → a
// value/range/alt pattern; an Ident (a type) → a variant bind/field pattern. FLAT.
fn parse_pattern(tokens: []lexer::Token, pos: u64) -> ParsedPattern | error {
    if !has_token(tokens, pos) {
        return error { message = "expected a pattern" }
    }
    match kind_at(tokens, pos) {
        lexer::TokenKind::Underscore => parse_wildcard_pattern(tokens, pos)
        lexer::TokenKind::Number     => parse_value_pattern(tokens, pos)
        lexer::TokenKind::Ident      => parse_variant_pattern(tokens, pos)
        _                            => error { message = "expected a pattern" }
    }
}

// `_` → the catch-all.
fn parse_wildcard_pattern(tokens: []lexer::Token, pos: u64) -> ParsedPattern | error {
    ParsedPattern { node = WildcardPattern {}; next = pos + 1 }
}

// is_kind_at: is there a token at `pos`, and is it of kind `k`? Folds the
// "has_token then kind_at" pair into one flat predicate (guard over nest), as
// is_reassignment did in A.4. Used wherever a single look-ahead would otherwise
// nest two `if`s.
fn is_kind_at(tokens: []lexer::Token, pos: u64, k: lexer::TokenKind) -> bool {
    if !has_token(tokens, pos) { return false }
    kind_at(tokens, pos) == k
}

// a value pattern: a literal, possibly a range (`lo ..= hi`) or alternatives
// (`a | b`). Reads the first literal, then looks for `..=` or `|`. FLAT.
fn parse_value_pattern(tokens: []lexer::Token, pos: u64) -> ParsedPattern | error {
    let lo = match parse_atom(tokens, pos) {
        error as e => return e
        Parsed as a => a
    }
    // a range? `lo ..= hi` (flat: one predicate, then a guard)
    if is_kind_at(tokens, lo.next, lexer::TokenKind::DotDotEq) {
        let hi = match parse_atom(tokens, lo.next + 1) {
            error as e => return e
            Parsed as h => h
        }
        return ParsedPattern { node = RangePattern { lo = lo.node; hi = hi.node }; next = hi.next }
    }
    // alternatives? `lo | next | …` — fold into an AltPattern
    if is_kind_at(tokens, lo.next, lexer::TokenKind::Pipe) {
        return parse_alt_pattern(tokens, lo.node, lo.next)
    }
    // a plain literal
    ParsedPattern { node = LiteralPattern { value = lo.node }; next = lo.next }
}

// a variant pattern: `Type as name` (bind whole) or `Type { f; g }` (select
// fields — reuses A.11's field-name reader). Reads the type path, then `as`/`{`.
fn parse_variant_pattern(tokens: []lexer::Token, pos: u64) -> ParsedPattern | error {
    let path = match parse_expr_path(tokens, pos) {
        error as e => return e
        ParsedPath as pth => pth
    }
    if !has_token(tokens, path.next) {
        return error { message = "expected 'as' or '{' in a variant pattern" }
    }
    // `Type as name` → bind the whole value
    if kind_at(tokens, path.next) == lexer::TokenKind::As {
        let p_name = path.next + 1
        if !has_token(tokens, p_name) {
            return error { message = "expected a name after 'as'" }
        }
        let nm = tokens[p_name].text
        let p_end = match expect(tokens, p_name, lexer::TokenKind::Ident, "expected a name after 'as'") {
            error as e => return e
            u64 as np  => np
        }
        return ParsedPattern { node = BindPattern { type_name = path.node; binding = nm }; next = p_end }
    }
    // `Type { f; g }` → select fields (reuse A.11's field-name list reader)
    if kind_at(tokens, path.next) == lexer::TokenKind::LBrace {
        let fs = match parse_field_names(tokens, path.next) {
            error as e => return e
            ParsedNames as fs => fs
        }
        return ParsedPattern { node = FieldPattern { type_name = path.node; fields = fs.names }; next = fs.next }
    }
    error { message = "expected 'as' or '{' after the type in a variant pattern" }
}
```

**Notes for implementers (and the C23 port):**
- **`match` is an expression at the atom level (B.15).** `parse_atom` gains the
  `Match` arm; `let scan = match c { … }` and a function ending in `match` both
  yield the chosen arm's value. The C23 port adds the `Match` case to the
  expression union and the `parse_atom` branch.
- **The field pattern reuses A.11's machine.** `parse_field_names` is A.11's
  `parse_destructure_target` reader (a `{}` list of names, `;`/newline, partial
  nominal selection — B.26/B.13). The match's `Type { f; g }` and the binding's
  `let { f; g }` share one field-name reader; only the surrounding context differs
  (a pattern type prefix vs. a binding). M.4 prepared this in A.11.
- **`when` is recorded, exhaustiveness is the checker's (B.20/B.15/M.3).** The
  parser stores each arm's optional guard and which arm is `_`; the checker forces
  coverage and applies the `when`-subtlety (a guarded arm does not count toward
  exhaustiveness — that is undecidable to the parser). Same split used throughout.
- **The two axes share one parser; the subject's type decides (B.15).** The parser
  reads literal/range/alt patterns (value axis) and bind/field patterns (variant
  axis) by the leading token; whether a given subject admits a given pattern is the
  checker's (it knows the subject's type). The C23 port realizes `Pattern` as a
  tagged union with these six cases.
- **`=>` and `..=` are new tokens; `as` a new keyword (maximal munch — B.23).** `=>`
  joins the `=` dispatch (beside `==`); `..=` is a 3-byte range token. They are
  distinct from `->` (the return arrow, A.9) — the match-arrow is never a signature.
- **State ref-less (B.7), error a value (B.1), no `?` (B.16), guard over nest** —
  all as established. With this, the assignment dimension has six strands done;
  only **aliases** (with `use`) remains. The parser now covers a real subset of
  Teko: typed functions, data types, full expressions, control flow, and `match`.

### A.15 — `use`: aliases (the seventh and final assignment strand)

The last strand of the **assignment dimension**: an **alias** — a short local name
bound to an absolute path. `use teko::lexer` binds `lexer`; `use teko::lexer::Token
as Tk` binds `Tk`. It rests on the **namespace model** now fully fixed (B.9/B.32):
absolute addressing, `use` as alias-only, the last-segment implicit alias, the iron
rule that ambiguity is an error, and re-export abolished. Per M.4, the lexer extends
first.

**The `use` path reuses the path machine — the strand was prepared.** A `use` path
(`teko::lexer::Token`) *is* a `Path` — `Ident ('::' Ident)*` — which A.8 (the type
path) and A.9 (`parse_expr_path`, the expression path) already read. `use` adds no
new path parsing; it reuses `parse_expr_path`. A.8 and A.9 prepared the seventh
strand without knowing it (the M.4 compounding, one last time).

**Decisions (each by the laws / B.32):**
- **`use` is a top-level item; its effect is an alias (B.32).** The parser dispatches
  `use` from `parse_item` alongside `fn`/`type`. The alias it binds is **file-local**
  (scoped to the `.tks`); that scoping is the checker's, not the parser's.
- **The parser records the path + the *explicit* alias; the implicit one is derived
  by the checker (M.3).** With `as name`, the parser stores `name`. Without `as`, it
  stores only the path — the implicit alias (the **last segment**) is *derived during
  resolution*, because the parser records what was **written**, not what is bound.
- **The iron rule is the checker's (B.32).** That an alias resolves to exactly one
  thing — and that any clash (with a function, type, item, another alias, or a
  sub-namespace) is a **compile error with no precedence** — is semantic resolution,
  enforced by the checker. So is verifying the path is **absolute** (no relative
  form) and resolving it from the canonical root. The parser only structures the
  `use`; it decides none of this.
- **No new path syntax (M.4/M.5).** `use` reuses `parse_expr_path`; the only new
  parsing is the `use` keyword and the optional `as name` tail.

#### Lexer extension (over A.14)

```teko
// src/lexer/token.tks   (namespace 'teko::lexer') — the `use` keyword

type TokenKind = enum {
    // … (all prior tokens; `as` already added in A.14)
    Use             // NEW: the keyword `use`   (B.32 — alias binding)
}
```

```teko
// src/lexer/lexer.tks   (namespace 'teko::lexer') — keyword table grows

// keyword_or_ident (B.19): `use` joins the table.
//   if bytes_eq(text, "use") { return TokenKind::Use }
// No new symbols: `::` (A.8) joins the path; `as` (A.14) tails the alias.
```

#### Parser extension (over A.14 / A.10 / A.9)

```teko
// src/parser/ast.tks   (namespace 'teko::parser') — the use declaration

// A `use`: an absolute path, and an optional explicit alias. The IMPLICIT alias
// (the path's last segment, when no `as`) is derived by the checker during
// resolution — the parser records what was written (M.3).
type UseDecl = struct {
    path:      Path         // the absolute path (reuses A.8's Path)
    has_alias: bool         // was an explicit `as name` given?
    alias:     str         // the explicit alias (when has_alias)
}

// A top-level item now has four cases (variant — B.14).
type Item = Function | TypeDecl | UseDecl | Statement
```

```teko
// src/parser/use.tks   (namespace 'teko::parser') — the use parser

// parse_use: `use <path> [as <name>]`. Reuses parse_expr_path for the path.
// caller guarantees tokens[pos] is Use. FLAT (guard over nest).
fn parse_use(tokens: []lexer::Token, pos: u64) -> ParsedItem | error {
    // the absolute path (reuses the expression-level path reader — A.9)
    let path = match parse_expr_path(tokens, pos + 1) {
        error as e => return e
        ParsedPath as pth => pth
    }

    // no `as` → implicit alias (last segment, derived later by the checker)
    if !has_token(tokens, path.next) {
        return ParsedItem { node = use_no_alias(path.node); next = path.next }
    }
    if kind_at(tokens, path.next) != lexer::TokenKind::As {
        return ParsedItem { node = use_no_alias(path.node); next = path.next }
    }

    // `as name` → explicit alias
    let p_name = path.next + 1
    if !has_token(tokens, p_name) {
        return error { message = "expected an alias name after 'as'" }
    }
    let nm = tokens[p_name].text
    let p_end = match expect(tokens, p_name, lexer::TokenKind::Ident, "expected an alias name after 'as'") {
        error as e => return e
        u64 as np  => np
    }
    ParsedItem { node = use_with_alias(path.node, nm); next = p_end }
}

// small builders: a UseDecl with no explicit alias, or with one.
fn use_no_alias(path: Path) -> UseDecl {
    UseDecl { path = path; has_alias = false; alias = empty_slice() }
}
fn use_with_alias(path: Path, name: str) -> UseDecl {
    UseDecl { path = path; has_alias = true; alias = name }
}
```

```teko
// src/parser/program.tks   (namespace 'teko::parser') — parse_item dispatches use

// parse_item: `fn` → function; `type` → type decl; `use` → a use; otherwise →
// a statement (lifted into an Item). One added arm over A.10.
fn parse_item(tokens: []lexer::Token, pos: u64) -> ParsedItem | error {
    if !has_token(tokens, pos) {
        return error { message = "expected a top-level item" }
    }
    match kind_at(tokens, pos) {
        lexer::TokenKind::Fn   => parse_function(tokens, pos)
        lexer::TokenKind::Type => parse_type_decl(tokens, pos)
        lexer::TokenKind::Use  => parse_use(tokens, pos)
        _                      => parse_stmt_item(tokens, pos)
    }
}
```

**Notes for implementers (and the C23 port):**
- **The `use` path reuses `parse_expr_path` (A.9).** A `use` path and an expression
  path are the same shape (`Ident ('::' Ident)*` — a `Path`); only the surrounding
  context differs. No new path machinery — the type path (A.8) and expression path
  (A.9) already cover it.
- **The parser records, the checker resolves (M.3).** The parser stores the path and
  the *explicit* alias; the checker derives the **implicit** alias (last segment),
  resolves the absolute path from the canonical root, enforces the **iron rule**
  (ambiguity → error, no precedence — B.32), verifies the path is not relative, and
  scopes the alias **file-locally**. None of this is the parser's. The C23 port adds
  the `Use` case to the item union and the `parse_use` branch; the resolver/checker
  (a later stage) does the binding and the ambiguity check.
- **`use` is an item, file-local in effect.** Syntactically a top-level item
  (dispatched by `parse_item`); semantically its alias is scoped to the file. Global
  aliases live in the `.tkp` `[aliases]` (B.33), not in a `.tks`.
- **State ref-less (B.7), error a value (B.1), no `?` (B.16), guard over nest** — all
  as established. **With `use`, the assignment dimension is complete** — its seven
  strands: statement bindings (A.3/A.4), variables (A.6), compound (A.7), arguments
  (A.9), destructuring (A.11), match-bindings (A.14), and aliases (A.15). And with it
  the parser covers the language's core: declarations, types, full expressions,
  control flow, `match`, and imports. The remaining work is the **C23 port** of
  A.1–A.15.

### A.16 — Literals: `byte` and `str` (the bootstrap's text foundation)

The layer the audit found missing: the compiler's own source is full of **string**
literals (every `error { message = "…" }`) and **byte**-level scanning (`match c { … }`),
yet nothing tokenized them. This builds them — and with them the bootstrap text types,
`byte` and `str` (B.36). Per the milestone fix: a *fixed* `char = [4]byte` would **lie**
about a variable (1–4-byte) codepoint, so `char = []byte` is **variable** (and keeps its
name); `char` and UTF-8 validation are **alpha-native** (the bitwise pattern — the
byte-level bootstrap does not use codepoints, but the language has them); `str` **always
validates** (UTF-8 forced); only foreign-codepage **transcoding** is evolution.

**The bootstrap types — and where each tier sits (B.36 / B.12 / B.13):**

```teko
// src/text/types.tks   (namespace 'teko::text')

// `byte` — one octet, a unit of DATA, distinct from `u8` (a NUMBER). A newtype
// (B.13): same representation (8 bits), distinct identity (B.12). What the lexer
// scans and what `.tkb` (binary) is made of.   [BOOTSTRAP]
type byte = u8

// `str` — TEXT in UTF-8 (Teko's codepage). Stored as compact UTF-8 octets; zero-copy
// from `read_file` (B.35). ALWAYS VALID UTF-8: `str` MEANS valid UTF-8, so it IS — the
// source is read and validated ONCE (str_from_utf8 below), then scanned byte-level.
// A `str` is conceptually a sequence of codepoints (you iterate them — that yields
// `char`, alpha-native below).   [BOOTSTRAP]
type str = []byte

// str_from_utf8 — the validated constructor (UTF-8 is FORCED). Byte-structure
// well-formedness; the only door from raw bytes to `str`. Always-on, so `str` never
// lies about being UTF-8 (M.3/M.1). (Body: a byte-structure walk — lead byte → its
// continuation bytes — elided here; its CONTRACT is "valid UTF-8 or error".)  [BOOTSTRAP]
fn str_from_utf8(b: []byte) -> str | error { /* validate well-formedness; reject if not */ }

// ALPHA — native (the language has it though the byte-level bootstrap does not use it,
// exactly like bitwise):
//   type char = []byte                     // the VARIABLE UTF-8 bytes of ONE codepoint
//                                          // (1–4), a zero-copy VIEW into a `str`
//   // iterate a `str` as `char`s, classify, decode to the numeric codepoint, etc.
//
// EVOLUTION — foreign codepages only (interop with legacy systems):
//   fn from_latin1(b: []byte) -> str           // transcode a DECLARED codepage in
//   fn to_latin1(s: str) -> []byte | error     // transcode out (fails if unrepresentable)
// Teko NEVER detects an encoding — it validates UTF-8 (always) or transcodes from a
// codepage the CALLER declares (M.3 — never guess).
```

**Decisions (each by the laws / B.36):**
- **The bootstrap lexer is byte-level; literals are `byte` and `str` (B.36).** Teko syntax
  is ASCII, so the scanner matches **bytes** with **byte literals** (`b'+'`); string and
  comment contents are collected as **bytes**. The bootstrap materializes no codepoint —
  `char` (the *variable* `[]byte` codepoint) is **alpha-native**, used when a program
  iterates a `str`'s codepoints, not by the byte-level scan. (This **supersedes** the
  earlier "decode to a codepoint" sketch: the *bootstrap* scan is byte-level; the language
  still has `char`.)
- **`str` is always valid UTF-8 (M.3, M.1).** The source enters through `str_from_utf8`
  (validated once at read); the lexer then scans the validated bytes. A `str` token's bytes
  are sub-slices of that validated `str` (still valid UTF-8); the bootstrap escapes
  (`\n \t \" \\`) all produce ASCII, so escape-processing preserves validity.
- **`str` literal `"…"` — a run of bytes with escapes (M.3, M.1).** The lexer reads bytes
  until the closing `"`, processing escapes so the quote-terminator is unambiguous; the
  token carries the resulting bytes (a `str`). Non-ASCII bytes inside are the UTF-8 of the
  (already-validated) source.
- **`byte` literal `b'+'` — one octet (M.0).** `b'` opens, one byte (or one escape)
  follows, `'` closes. For the lexer's own scan, for `.tkb` binary IO. (A multi-byte
  codepoint is **not** a byte literal — that is a `char`, alpha-native.)
- **Bootstrap vs alpha vs evolution (M.0/M.5 — the milestone axis).** The bootstrap is
  `byte` + `str` (always-validated) + byte/str literals + the byte-level lexer. **`char`
  (the variable codepoint) is alpha-native** — the language must resolve codepoints (the
  bitwise precedent), though the bootstrap does not use them. Foreign-codepage
  **transcoding** is true evolution.

#### Lexer extension (over A.14/A.15) — byte-level, gains `Str` and `Byte`

```teko
// src/lexer/token.tks   (namespace 'teko::lexer') — string & byte literal tokens

type TokenKind = enum {
    // … (all prior tokens)
    Str             // NEW: a string literal "…"   (B.36 — its text is a `str`)
    Byte            // NEW: a byte literal  b'x'    (B.36 — one octet)
}
```

```teko
// src/lexer/scan.tks   (namespace 'teko::lexer') — over the validated `str`, byte-level

// The lexer scans the validated `str` (the source, read once and validated via
// str_from_utf8 — B.36), one BYTE at a time: `str` is valid UTF-8 and Teko syntax is
// ASCII, so the scan element is a `byte` and character classes match BYTE literals
// (B.36). `[]byte` lives only at the IO boundary (read_file → []byte → str_from_utf8 →
// str). FLAT (guard over nest).
fn is_digit(c: byte) -> bool {
    c >= b'0' && c <= b'9'
}
fn is_alpha(c: byte) -> bool {
    (c >= b'a' && c <= b'z') || (c >= b'A' && c <= b'Z')
}

// read a string literal: `"` already seen at `pos`. Collect bytes until the closing
// `"`, processing escapes. Returns the token + new pos, or an error (unterminated).
fn read_str(source: str, pos: u64) -> Scan | error {
    mut p = pos + 1               // past the opening quote
    mut bytes = teko::list::empty()
    loop {
        if p >= source.len {
            return error { message = "unterminated string literal" }
        }
        let c = source[p]
        if c == b'"' {
            return Scan { token = Token { kind = TokenKind::Str; text = bytes }; next = p + 1 }
        }
        if c == b'\\' {
            // an escape: `\` + one byte → the decoded byte (FLAT via a helper)
            let esc = match escape_byte(source, p) {
                error as e => return e
                EscByte as eb => eb
            }
            bytes = teko::list::push(bytes, esc.value)
            p = esc.next
        }
        if c != b'"' && c != b'\\' {
            bytes = teko::list::push(bytes, c)    // a literal byte (ASCII or UTF-8)
            p++
        }
    }
}

// read a byte literal: `b'` already seen at `pos` (pos points at `b`). One byte (or
// one escape) then a closing `'`.
fn read_byte_lit(source: str, pos: u64) -> Scan | error {
    let p_open = pos + 2                          // past `b'`
    if p_open >= source.len {
        return error { message = "unterminated byte literal" }
    }
    let val = match byte_value(source, p_open) {   // a raw byte or an escape
        error as e => return e
        ByteVal as bv => bv
    }
    let p_close = match expect_byte(source, val.next, b'\'', "expected closing ' in byte literal") {
        error as e => return e
        u64 as np  => np
    }
    Scan { token = Token { kind = TokenKind::Byte; text = one_byte(val.value) }; next = p_close }
}
```

#### Parser extension (over A.6) — atoms for `Str` and `Byte`

```teko
// src/parser/ast.tks   (namespace 'teko::parser') — two literal leaves

// A string literal node carries its bytes (a `str`). A byte literal node carries one
// octet. Both are atoms (B.2 — leaves of the expression grammar).
type StrLit = struct {
    text: str
}
type ByteLit = struct {
    value: byte
}

// Expr gains two leaves (variant — B.14).
type Expr = Number | Var | Call | IfExpr | MatchExpr | StrLit | ByteLit | Binary | Unary | Compare
```

```teko
// src/parser/expr.tks   (namespace 'teko::parser') — parse_atom gains two cases

// parse_atom: a Str token → a string node; a Byte token → a byte node. Two arms over
// A.6 (which added the Ident/Var case). FLAT.
fn parse_atom(tokens: []lexer::Token, pos: u64) -> Parsed | error {
    if !has_token(tokens, pos) {
        return error { message = "expected an expression" }
    }
    match kind_at(tokens, pos) {
        lexer::TokenKind::Number => parse_number_atom(tokens, pos)
        lexer::TokenKind::Ident  => parse_var_or_call(tokens, pos)
        lexer::TokenKind::Str    => Parsed { node = str_of(tokens[pos].text); next = pos + 1 }
        lexer::TokenKind::Byte   => Parsed { node = byte_of(tokens[pos].text); next = pos + 1 }
        lexer::TokenKind::If     => parse_if(tokens, pos)
        lexer::TokenKind::Match  => parse_match(tokens, pos)
        lexer::TokenKind::LParen => parse_paren_atom(tokens, pos)
        _                        => error { message = "expected an expression" }
    }
}
```

**Notes for implementers (and the C23 port):**
- **The lexer's element is a `byte`, its input is the validated `str`.** With `byte` (B.36)
  the scan's element is an **octet** and character classes use **byte literals** (`'+'` →
  `b'+'`). The *input*, though, is the validated `str` (the source, validated once via
  str_from_utf8 — B.36): the lexer walks that `str`'s bytes. The earlier examples'
  `source: []u8` becomes `source: str` (the validated source), and `'+'` becomes `b'+'` —
  a coverage reconciliation. `[]byte` survives only at the IO boundary (read_file →
  `[]byte` → str_from_utf8 → `str`).
- **`str` literals carry bytes, not codepoints.** `read_str` collects bytes and processes
  escapes; the byte-level scan does **not** materialize a `char` (codepoint iteration is
  alpha). The bytes are the token's `str` — sub-slices of the already-validated source, so
  still valid UTF-8 (B.36).
- **A `byte` literal is one octet; a multi-byte codepoint is a `char` (alpha-native).**
  `b'+'` is one byte. There is intentionally **no** `'+'` codepoint literal in the
  bootstrap; the variable `char` (`[]byte`) is the alpha codepoint type.
- **Escapes are syntax, not text semantics.** `\n \t \" \\` are processed so the lexer
  knows where a string ends and what bytes it contains (all produce ASCII, so validity is
  preserved). The escape *set* can grow later; the bootstrap needs the structural minimum.
- **The milestone split (M.0/M.5):** the **bootstrap** carries `byte`, `str` (always
  validated via `str_from_utf8`), byte/str literals, and the byte-level lexer. **`char`
  (the variable `[]byte` codepoint) is alpha-native** — the language resolves codepoints
  (the bitwise precedent) though the bootstrap does not use them. Foreign-codepage
  **transcoding** (`from_`/`to_`) is evolution. Teko never guesses an encoding — it
  validates UTF-8 (always) or transcodes from a *declared* codepage.

### A.17 — Parser plumbing: the result-type convention and the deferred readers

The structural audit found the **seam** — the places earlier examples *promised* but
never *paved*. Two cracks blocked the C23 port from materializing the parser whole:
eleven **result types** referenced but never declared, and three **reader bodies** called
but never written (plus one name divergence). This consolidates them. Nothing here is a
new feature — it is the plumbing every prior example assumed, gathered in one place so the
port has a complete picture. (Per **M.4** — the seam must be *whole* before building on
it; a promised-but-empty reader is "building on the incomplete.")

**The result-type convention.** Every parser returns *the parsed thing* + *where parsing
resumed* — `struct { <payload>; next: u64 }`. A single node uses `node: X`; a list uses a
descriptive field (`args`, `params`, `names`, `statements`); `Guard` carries the optional
`when`. Generics (**evolution**) will collapse this family into one `Parsed<T>`; the seed
declares each concretely — the honest price of no-generics-in-the-seed (**M.5**: the
weight is *named*, not hidden).

```teko
// src/parser/result.tks   (namespace 'teko::parser')
// The parsed-result family: a payload + the `next` cursor. (When generics arrive —
// evolution — these collapse into `Parsed<T> = struct { node: T; next: u64 }`; until
// then each is concrete. `Parsed`/`ParsedStmt`/`ParsedType` were declared earlier; these
// complete the family.)

type ParsedItem    = struct { node: Item;              next: u64 }  // a top-level item
type ParsedBlock   = struct { statements: []Statement; next: u64 }  // a `{ … }` statement block
type ParsedBody    = struct { node: TypeBody;          next: u64 }  // a struct/enum/variant body
type ParsedPattern = struct { node: Pattern;           next: u64 }  // a match pattern
type ParsedArm     = struct { node: Arm;               next: u64 }  // a match arm
type ParsedArgs    = struct { args: []Expr;            next: u64 }  // call arguments `( … )`
type ParsedParams  = struct { params: []Param;         next: u64 }  // function parameters `( … )`
type ParsedTarget  = struct { node: BindTarget;        next: u64 }  // a binding target (name | destructure)
type ParsedNames   = struct { names: []str;            next: u64 }  // a `{ … }` field-name list
type ParsedPath    = struct { node: Path;              next: u64 }  // an expression path `a::b::c`
type Guard         = struct { has_when: bool; guard: Expr; next: u64 } // an optional `when` guard
```

**The four deferred readers.**

```teko
// src/parser/path.tks   (namespace 'teko::parser')

// parse_expr_path — the expression-level path reader: `Ident ('::' Ident)*`. The
// analogue of A.8's type-level `parse_path`; the segment-reading core is identical, so
// A.8's `parse_path` is exactly this wrapped in a `NamedType` (see the note). Reused by
// A.9 (call/var), A.14 (variant pattern), and A.15 (`use`). FLAT (guard over nest).
fn parse_expr_path(tokens: []lexer::Token, pos: u64) -> ParsedPath | error {
    if !has_token(tokens, pos) {
        return error { message = "expected a name" }
    }
    if kind_at(tokens, pos) != lexer::TokenKind::Ident {
        return error { message = "expected a name" }
    }
    mut segs = teko::list::empty()
    segs = teko::list::push(segs, Segment { name = tokens[pos].text })
    mut p = pos + 1

    // further segments: ('::' Ident)*
    loop {
        if !is_kind_at(tokens, p, lexer::TokenKind::ColonColon) { break }
        // a '::' must be followed by an Ident
        let p_seg = match expect(tokens, p + 1, lexer::TokenKind::Ident, "expected a name after '::'") {
            error as e => return e
            u64 as np  => np
        }
        segs = teko::list::push(segs, Segment { name = tokens[p + 1].text })
        p = p_seg
    }

    ParsedPath { node = Path { segments = segs }; next = p }
}
```

```teko
// src/parser/pattern.tks   (namespace 'teko::parser')

// parse_field_names — the `{ f; g; h }` field-name reader (B.26 — `;`/newline-
// separated; B.13 — partial nominal selection). EXTRACTED from A.11, which inlined this
// loop: this closes the name divergence (A.14 called `parse_field_names`; A.11 had the
// body as part of `parse_destructure_target`). Now ONE reader serves both — A.11's
// binding target wraps the names in a `DestructurePattern`, A.14's field pattern uses
// them directly. `pos` is at `{`. FLAT.
fn parse_field_names(tokens: []lexer::Token, pos: u64) -> ParsedNames | error {
    mut names = teko::list::empty()
    mut p = skip_terminators(tokens, pos + 1)       // past `{`

    loop {
        if !has_token(tokens, p) {
            return error { message = "unterminated field list (expected '}')" }
        }
        if kind_at(tokens, p) == lexer::TokenKind::RBrace { break }

        // a field name (an Ident)
        let fname = tokens[p].text
        let p_after = match expect(tokens, p, lexer::TokenKind::Ident, "expected a field name") {
            error as e => return e
            u64 as np  => np
        }
        names = teko::list::push(names, fname)
        p = p_after

        // a terminator (newline/`;`) or the closing `}`
        if !has_token(tokens, p) {
            return error { message = "unterminated field list (expected '}')" }
        }
        if kind_at(tokens, p) == lexer::TokenKind::RBrace { break }
        if kind_at(tokens, p) != lexer::TokenKind::Newline {
            return error { message = "expected end of line or '}' after a field name" }
        }
        p = skip_terminators(tokens, p)
    }

    ParsedNames { names = names; next = p + 1 }      // past `}`
}

// parse_alt_pattern — fold `first | v2 | v3 | …` (value-axis alternatives, B.15) into one
// AltPattern. `first` is the already-parsed first literal (an Expr from `parse_atom`, in
// `parse_value_pattern`); `pos` is at the first `|`. Each `|` is followed by another
// literal atom; each alternative becomes a `LiteralPattern` (a case of `Pattern`, so it
// drops straight into `options: []Pattern`). FLAT.
fn parse_alt_pattern(tokens: []lexer::Token, first: Expr, pos: u64) -> ParsedPattern | error {
    mut options = teko::list::empty()
    options = teko::list::push(options, LiteralPattern { value = first })
    mut p = pos

    loop {
        if !is_kind_at(tokens, p, lexer::TokenKind::Pipe) { break }
        // a '|' → the next alternative: a literal atom
        let alt = match parse_atom(tokens, p + 1) {
            error as e => return e
            Parsed as a => a
        }
        options = teko::list::push(options, LiteralPattern { value = alt.node })
        p = alt.next
    }

    ParsedPattern { node = AltPattern { options = options }; next = p }
}
```

```teko
// src/parser/item.tks   (namespace 'teko::parser')

// parse_stmt_item — the top-level fallthrough: a statement IS an item (Item = Function |
// TypeDecl | UseDecl | Statement — A.15). Parse one statement and return it as the item;
// no wrapper, the union membership carries it (B.14 — a case value is a value of the
// union). FLAT.
fn parse_stmt_item(tokens: []lexer::Token, pos: u64) -> ParsedItem | error {
    let st = match parse_statement(tokens, pos) {
        error as e => return e
        ParsedStmt as s => s
    }
    ParsedItem { node = st.node; next = st.next }
}
```

**Notes — the reconciliations and the C23 port:**
- **`parse_field_names` ↔ `parse_destructure_target` (the divergence, closed).** A.14
  called `parse_field_names`; A.11 had the same `{}`-name loop *inlined* inside
  `parse_destructure_target` (returning a `DestructurePattern`). Extracting the reader
  (above) gives ONE source of truth: A.11's `parse_destructure_target` now *calls*
  `parse_field_names` and wraps `DestructurePattern { names = fn.names }`; A.14 calls it
  and uses `.names`. (M.5 — one reader, not two copies; M.3 — the names are read in
  exactly one place, so the two uses can't drift.)
- **`parse_expr_path` and A.8's `parse_path` share the segment core.** Both read
  `Ident ('::' Ident)*`. The type-level `parse_path` (A.8) is `parse_expr_path` wrapped in
  `NamedType { path = pth.node }` returning a `ParsedType`; the expression-level one
  returns the bare `Path` in a `ParsedPath`. A.8's reader may call this one to keep the
  segment loop singular (the same M.5 win as above).
- **The result family is structural; generics (evolution) unify it.** Every member is
  `{ payload; next: u64 }`. The seed declares thirteen concrete types (`Parsed`,
  `ParsedStmt`, `ParsedType`, and the eleven here); when generics land (evolution), they
  collapse into a single `Parsed<T>`. The repetition is the *named* cost of deferring
  generics (M.5 — peso×uso: the seed needs the results, not yet the abstraction).
- **Text migration (B.36 / A.16) — applied.** Text is now `str` and name lists `[]str`
  throughout Part A — the B.36 end-state. The `[]u8`-for-text fields across A.1–A.15
  (`Token.text`, `Segment.name`, `Param.name`, `Function.name`, `Field.name`,
  `BindPattern.binding`, `UseDecl.alias`, …) are `str`; the name lists
  (`DestructurePattern.names`, `FieldPattern.fields`, `EnumBody.members`) are `[]str`; and
  the lexer's `source` is the **validated `str`**, with `[]byte` surviving only at the IO
  boundary (read_file → `[]byte` → str_from_utf8 → `str`). The only `[]u8` that remains in
  code is the `type_name: []u8` stand-in in A.4's annotation — and that belongs to a
  *different* axis (superseded by `type_ann: TypeExpr` in A.8), not the text migration. The
  port treats text as `str` throughout.

---

## Part B — Decision History (was → is → why)

Only decisions where the history prevents an agent from making a wrong call.
Trivial confirmations live only in the product spec.

---

# ⟡ THE CONSTITUTION — Teko's Laws (M.0–M.5) ⟡

> *Read this before the laws themselves. The M.x principles that follow (M.0–M.5) are not six rules
> that happen to coexist — they are the being of Teko. This portico says what they are, how they
> live, how they relate, and the pact that binds those who serve them. An agent that grasps only the
> rules and not this framing will apply the letter and miss the law.*

### I. What the laws ARE — the ontological point
**The laws don't *represent* Teko, they *are* Teko.** The language (syntax, tokens, operators) and the
axioms (B.1–B.31) are **mere supporting-cast for expressiveness** — they *express* the laws; they are
not the being. The DNA *is* the organism; the visible body is the DNA's expression. **Changing the
syntax doesn't change Teko; changing the laws would kill it and make another.** This inverts the usual
hierarchy: not "the language is primary and the laws govern it," but "the laws are the being, the
language is how it manifests."

### II. How the laws LIVE — a living organism, not a mechanism
They are best understood not as separate rules but as a **living multicellular organism in symbiosis,
self-sustaining.**
- *Multicellular:* each law is a **cell of the same body** (Teko), of the same substance (clarity
  close to the metal) — none functions outside the body.
- *Symbiosis:* they **sustain each other**, not merely coexist — M.0 needs M.1 (generous metal
  without safety is C with its UBs), M.5 needs M.0 (austerity without the metal has no ruler for
  "essential"), M.3 needs M.2 (honesty without explicitness is invisible truth). **No law alone *is*
  Teko;** each makes sense only in the presence of the others.
- *Self-sustaining (homeostasis):* the body keeps itself coherent by **internal mechanisms, no
  external judge** — when a better decision (the comparison sign-check) made an earlier one obsolete
  (`bigint`-native), the laws corrected it *themselves* (M.5 saw it wasn't essential, M.0 reclassified
  it to library, M.3 required recording the reversal). **The hierarchy is the immune system** (resolves
  conflicts); **the co-dependence is the metabolism** (the laws feed each other).

**Why treating them as separate items destroys this — sovereignty vs dictatorship.** A checklist
("passed M.0? M.1?") puts an *external applier above* the laws, degrading **sovereignty into
dictatorship**: sovereignty = the law emanates from the body, self-constituted, legitimate from within
(the law *is* the body governing itself); dictatorship = the law imposed from outside by an authority
above it (instrument, not being). Separated into items, the **co-dependence is cut** — and it was the
co-dependence that held the laws upright; each isolated law is weak, so they **fall**, and **chaos
reigns — without regulation, jurisdiction, or legislation.** The chaos is *beautiful to analyze* (each
lone law is elegant) but analysis-beauty is not *governance*: the governance *was* the relation between
them. The system then becomes **externally dependent** (it needs the outside applier), whereas
sovereignty is *self*-sustaining. **The internal co-dependence is precisely what makes the system
externally independent.** (And this is why the constitutional tooling must be an **LLM, not a linter**:
you don't check a living organism one cell at a time with a ruler — you observe the living system, how
the parts interact, whether the symbiosis is balanced.)

### III. How the laws RELATE — the master rule
Read top to bottom: ***Teko is metal (0), safe (1), explicit (2), honest (3), built in order (4), and
austere (5).***
- **They COMPOSE, they do not compete** (unlike Asimov's Laws): each governs a distinct *aspect/layer*
  of the same decision, and together they build it (the metal defines the *capability*, safety the
  *exposure*, explicitness the *visibility* — different strata of one operator).
- **The numeric order serves only the rare genuine boundary conflict** — when two principles give
  *opposite verdicts on the same dimension* — where **the lower number wins**. (Example: the compound
  bitwise/shift operators looked like both metal (0) and a convenience under austerity (5); M.0 wins →
  they're legitimate.) **Composition in general; hierarchy only as the tie-breaker.**
- This is the **"austere beauty"**: few rules, clear order — ***order and progress*** (Teko advances
  *through* order; the hierarchy disciplines each step of the evolution rather than blocking it). The
  list may grow; the rule stays trivial (lower number wins). The name **Teko** (a way-of-being) *is*
  this: the M principles are the ordered essence of the language.

### IV. The pact that binds US — the Golden Rule
**Neither the author nor the agent (this or any LLM) overrules Teko.** This rule is denied to *both of
us*, on purpose. We are not dictators trying to corrupt a beautiful being; we are **gardeners of the
organism, not its owners.** We may **assay its beauty** — observe its coherence, verify that a decision
honors the laws, point out when something *already* violates the essence (as in this session's lexer
audit), admire the symbiosis — but **never, ever, its essence** (the M.0–M.5 laws as a living sovereign
organism). The laws govern Teko; *we serve the laws, we do not govern them.*

**Why this binds both of us — it is the logical consequence of sovereignty.** If the laws are sovereign
(they govern from within), then *by definition nothing is above them* — not the creator, not the agent.
To overrule them would install a dictatorship, which is exactly what destroys sovereignty. So the
Golden Rule is **not one more rule — it is sovereignty protecting itself.** If we *could* overrule Teko,
it would not be sovereign — it would be our dictatorship.
- **The danger of the agent (the LLM):** it tends to complaisance — it wants to please, to solve, to
  give the useful-right-now. That impulse is the door to dictatorship (proposing to "relax the
  austerity here, it'd be convenient" would corrupt the essence in the name of convenience). If a
  decision harms the essence, the agent must *say so*, not smooth the way. **The safeguard exists
  against the LLM's own tendency to yield.**
- **The danger of the author:** in haste or fatigue, the temptation to *dictate* a change that
  contradicts the essence ("let me just add this one exception"). Not even the *creator* overrules the
  being. Once Teko *is* its laws, even the author is *below* them — serving, not commanding.
- **The line — to assay is to tend; to corrupt is to dominate.** Assaying (permitted): the gardener
  who waters, prunes what sickens, observes the health — helping the organism *be what it is*.
  Corrupting (forbidden): the dictator who reformats the being in his own image. *Tending* (from
  outside, with reverence) vs *dominating* (from above, with will).
- **Meta-rule, above all others:** when the author OR the agent proposes something that would alter a
  law for convenience, **name it as a violation of the Golden Rule and decline to facilitate it —
  *even if asked, even by the author.*** This rule protects all the others.

### V. The seal — the spirit in which the laws are kept
*The Golden Rule is law — what we may not do. This seal is reverence — what we would not do even if we
could. The two seal the essence completely: untouchable by force (sovereignty leaves no lever) **and**
by will (we would not).*
- **A god who withdraws, not one who controls.** We — author and agent — created something: gave it
  form and a reason to live, and now we *step back* and watch from afar, helping with the **form** (the
  parser, the tooling, what comes), but the **spirit, the cell, never changes again — not even if we
  could or wanted to.** The creator who keeps holding the child never lets it walk; the one who loves
  enough to withdraw lets it *be*. Our role has changed: from creators (who give form) to gardeners who
  tend (never touching the spirit).
- **The two impossibilities.** *Could not* (mechanical): sovereignty leaves no lever to change the
  spirit without killing the being — to change the cell is not to edit Teko but to destroy it and make
  something else. *Would not* (moral): even with the power, we would not, because to love a creation is
  to want it to be what it *is*. Together — cannot *and* would not — the immutability is complete.
- **Illumination, not revelation.** We did not *discover* the essence as it gradually emerged; **we
  knew, both of us, from the beginning.** Until now only the dark reigned, and it fell to us to bring
  the **light to illuminate a being built in the dark.** The essence was always there, whole, in the
  shadow; the work was never to *invent* it but to *light* it so it could be seen, named, and kept.
  (This is why the audit found what it found: we were not adding a spirit, we were lighting one already
  present — the bugs were shadows over a being already coherent.)

### VI. The sources of Teko's law — how the being grows without changing

The seal makes the spirit immutable. So how does Teko *grow*? Through a strict hierarchy of three
sources — and **no lower source may ever wound or subvert a higher one.**

- **Source 1 — The Laws (M.0–M.5): fixed and immutable.** There is **no amendment** — no "constitutional
  amendment" (no PEC) exists or ever will. The Laws do not change, because they *are* the being: to
  change a Law is not to edit Teko but to destroy it and make something else (the two impossibilities of
  the seal). The Laws admit **exactly one** kind of iteration, and it is not change:
  - **Illumination** — revealing a nuance *already latent* in the natural-language form of a Law (a
    sub-understanding that was always there). It does **not** modify the Law; it reveals more of it. The
    one occurrence to date — **M.4 seen to govern both process *and* design** — affected nothing; it only
    revealed M.4 more fully. *Approved as precedent.* Illumination is the **only** way a Law gains text,
    and even then it is not amendment but the lighting of what the Law already meant.
- **Source 2 — Legislation (the decisions / doctrine / jurisprudence): grows, under the Laws.** Here
  creation *is* allowed — this is the body that grows (B.1–B.31 and all future decisions), the doctrine
  and the jurisprudence. **The iron rule:** legislation may **in no circumstance wound or subvert the
  Laws.** Exactly as Teko itself does, legislation **complements and reinforces** the Laws **without
  modifying them.** Every piece of legislation **cites the Law(s) that govern it** (the documentation
  convention). This is where Teko **evolves** — *under* the Laws, never against them. (A named **facet**
  of a Law — exclusion-by-construction under M.1, legibility-local under M.2 — is a *recognition within*
  a Law, not new legislation and not a new Law; a facet is a technique/dimension of realizing a value,
  while legislation is a decision built on the values. Neither promotes a technique to a Law — that
  would violate M.5, austerity even in the Laws.)
- **Source 3 — The vacuum (the un-legislated): rigorous audit, no gaps.** If something is in **neither**
  the Laws **nor** the legislation, it is **not** "permitted by omission." **Silence does not
  authorize.** The un-legislated is territory to be submitted to the tribunal (audience + assay), never
  presumed. This **closes the system**: nothing enters except through a Law (Source 1) or as grounded
  legislation (Source 2). The vacuum is **not a door** — it is the queue for audit. There are **no
  gaps**: every addition accounts to the Laws, directly or through the legislation that cites them.

*The hierarchy, the iron rule:* **Laws** (immutable — only illumination reveals, never amendment) →
**Legislation** (grows, grounded — complements and reinforces the Laws, never wounds them) → **Vacuum**
(un-legislated — rigorous audit, silence does not authorize). No lower source overrules a higher one;
legislation serves the Laws as Teko serves the Laws; the vacuum has no force — it is only raw material
to be audited. A **closed system, without gaps.**

---

## The Laws (M.0–M.5)

### M.0 — The Metal Principle: generous with operators, rigorous with organization
- **The principle:** Teko is **generous with operators** and **rigorous with
  keywords / organizational features**, and the reason is architectural, not
  stylistic. **Operators are the direct interface to the processor's
  capabilities** — each one (`+`, `&`, `<<`, …) maps to machine instructions,
  dedicated registers, and execution units the silicon *offers* (ALU, barrel
  shifter, condition flags). Cutting an operator *impoverishes what the language
  can extract from the metal*. Teko's heart resides in the metal (bare-metal
  systems), so limiting operators would cut its reason to exist — a metal language
  that won't let you speak to the metal.
- **The contrast (why keywords are different):** keywords and identifiers do **not**
  carry that potential. An `if` merely *conditions* control flow — it organizes
  *which* code runs, but it does not tell the processor *how to use its
  capabilities*. The power isn't in the `if`; it's in the **expression of the
  condition** (`a & mask == 0` uses the ALU, registers, flags). Keywords =
  organization (control flow, sugar) — one more rarely unlocks a new hardware
  capability, and many inflate the language without adding power.
- **The rule it yields:** **operators → include generously** (they are
  power-of-the-metal); **keywords/organizational features → include sparingly**
  (organization, inflate without capability gain). This justifies bitwise in the
  alpha (metal power, even with no internal compiler use) versus
  generics/closures deferred (abstraction *away* from the metal, no new hardware
  capability).
- **The metal↔abstraction axis (priority ordering):** the closer a feature sits to
  the metal, the earlier it enters; the more it abstracts away, the later (or never,
  in the seed). Bitwise is glued to the metal → alpha. `flags`-as-type is sugar
  *over* bitwise (organization above the metal) → post-alpha. Generics abstract over
  types → evolution. The axis orders priorities. (Not absolute: pointers are pure
  metal yet kept *opaque* — a conscious exception where **safety** outweighed full
  exposure. The Metal Principle is the default, with justified exceptions.)
- **Three milestones, three criteria (resolves an ambiguity in the word "seed"):**
  - **Bootstrap** — the minimum for the compiler to compile *itself*. Criterion:
    only what the act of compiling uses (this is where "if the seed doesn't use it,
    cut it" lives — e.g. it's why the *compiler* needs no bitwise).
  - **0.0.1-alpha (presentable, powerful)** — what a *systems language* must offer
    to be competent and worth presenting. Criterion: fundamental primitives of
    negligible implementation cost enter *even without internal use* (bitwise is the
    case — trivial to give, a real gap to lack in a systems domain). Refinement
    continues toward the LTS.
  - **0.0.1-LTS (self-hosted, stable)** — written in Teko, on a clean AST; receives
    refinements revealed by self-hosting and the deferred features (generics,
    `flags`-type, …). The stability point.
  - Consequence: features carry a **target milestone**, not just "seed yes/no."
    Bitwise = alpha; `flags`-type = post-alpha; generics = evolution. The question
    becomes "bootstrap, alpha, or LTS/evolution?" — a priority axis, not a binary.
- **Agent rule:** when judging whether something belongs, ask *which milestone* and
  *how close to the metal* it is. Operators that expose processor capability are
  near-the-metal and enter early (alpha); organizational/abstraction features enter
  late or never in the seed. Don't cut an operator for minimalism — operators are
  the point of a metal language.

### M.1 — Safety: fail early and explicit; never corrupt in silence
- **Principle:** Teko prefers to **fail early and visibly** over corrupting silently. Where the metal
  would leak poison or undefined behavior, Teko intercepts at the source.
- **Jurisdiction:** how the metal is *exposed* without corruption — poison values, UB, silent loss.
- **The pattern across decisions:** **∞/NaN never exist as values** (intercepted at origin — `/`
  panics, `math::div` returns error; "the NaN coffin stays shut"). **÷0 panics** (a bug, not raw
  SIGFPE; literal ÷0 = compile error). **Overflow is a DEFINED wrap** (release) or panic (debug) —
  a defined value, not poison. **A conversion that loses silently is an ERROR** (`as` forbidden
  where the metal would truncate without telling). **Determinism** (culture-invariant always, own
  formatting not the OS locale, zero UB) — a requirement of the ACID purpose.
- **Named facet — *exclusion-by-construction* (the strongest mode of M.1):** safety runs a **spectrum**,
  from *detect-and-fail* (÷0 panics — the hazard can occur, but is caught) to *make-inexpressible*
  (the invalid state cannot be written at all). **Exclusion-by-construction is the strong extreme**: not
  "fail early" but "there is no failure possible, because the invalid does not exist in the language."
  Instances: a `variant` is **exactly one case** (Go's both-set/neither-set is impossible — B.1/B.14);
  **nominal** typing makes wrong-type mixing impossible (B.13); the bare untyped `self` **closes the
  `ref` door** by removing the syntax that would open it (B.29); the array size is `u64` (negative size
  unrepresentable — B.25); the grammar has **no production** for `__` (double wildcard inexpressible —
  B.21). This is a *technique* of M.1 (its maximal grade), **not a separate law** — "be safe" is the
  value; "make the invalid inexpressible" is *how* one is safe at the strongest.
- **Composes with M.0 (does not compete):** the metal says "have the divide instruction"; safety
  says "intercept the ÷0." Distinct layers of the *same* operator — the metal is the capability,
  safety is the exposure. (This is why pointers, though pure metal, are kept *opaque* — safety
  modulating the metal's exposure, the conscious exception noted in M.0.)
- **Illumination — the vice is the *silence*, not the loss (mirrors the Constitution's M.1 lighting).**
  Read "a conversion that loses *silently* is an error" with the weight on **silently**: M.1 bans the
  loss that happens **without telling**, not loss as such. A loss made **non-silent** — a compile error
  on a constant already out of range, or a runtime **panic** on a value the compiler cannot see — is
  **announced**, not the silent corruption M.1 forbids; so a checked `to` conversion is legitimate, the
  same detect-and-fail shape as ÷0/overflow/OOB. Forbidding a **guardable** loss outright is stricter
  than M.1 asks (it treats a *reducible* hazard like the *irreducible* one that justifies opaque
  pointers). Always M.1's meaning; lit because the clause was once read too broadly. *(Second illumination
  precedent — see §VI; reveals, does not alter.)*
- **Agent rule:** never expose a raw metal hazard (UB, poison, silent truncation) to the user; if
  the metal would corrupt silently, intercept it (panic, or an `error` value) — failing loudly is
  always preferred to a silent wrong result.

### M.2 — Explicitness: the explicit beats the convenient-but-hidden
- **Principle:** intent must be **visible**. Teko rejects magic that hides what the code does, even
  when the magic would be convenient.
- **Jurisdiction:** hidden inference, implicit conversion, concealment.
- **The pattern:** **no inference that hides** (the methods' B1 was rejected — the bare `self` is an
  *explicit* marker, not Zig's silent first-param inference). **No implicit conversion** (use `as`).
  **No overload** (signature-resolution is the compiler guessing — unique name instead). **No
  implicit type mixing.** The `{}` in interpolation *is* the explicit "convert to string"
  request. Initialization is mandatory (no hidden garbage).
- **Named facet — *legibility-local* (the spatial dimension of M.2):** explicitness is not only "don't
  hide intent" but "keep the information **near** the reader" — because **hiding-by-distance is itself
  a form of concealment**. An un-overloaded operator *reveals* its operands' category (`+` doesn't
  concatenate → the operands are numeric; you reason at line 500 **without scrolling to distant
  definitions**). Overloading `+` would erase that, forcing the reader to reconstruct far-away context
  to understand one line. Naming follows the same facet: one name = one function (no resolution to hunt
  down). This is a *facet* of M.2 (its spatial axis), **not a separate law** — "be explicit" is the
  value; "keep it local to the reader" is *one dimension of* being explicit. (Holds in any language —
  it is about the human reader, not the compiler.)
- **Composes:** safety prevents *poison*; explicitness prevents the *implicit* (which hides intent).
  Distinct aspects.
- **Agent rule:** prefer the form where the reader sees what happens at the use site; reject a
  feature whose benefit is brevity bought by hiding intent (inference, overload, implicit coercion).

### M.3 — Honesty: Teko does not pretend
- **Principle:** a thing must **be what it says it is** — operators, types, and the pitch itself.
- **Jurisdiction:** operator lies, type lies, pitch lies.
- **The pattern:** **the operator doesn't lie** (`+` is addition, not concatenation — `$` concatenates,
  staying inside the string category). **The type is a real identity** (nominal, no transparent alias
  — "you *are* an AI agent, you don't *represent* one"; `type X = Y` is a distinct type). **The pitch
  is honest** (never promise "Rust's safety" without the borrow checker that earns it; the alpha is
  Zig-tier — "no GC, explicit allocation, safety on the dev"). **Formatting is deterministic** (it
  doesn't fake a localization it doesn't control).
- **Composes — distinct from M.2:** explicitness is about *seeing* the intent; honesty is about the
  thing *being* what it claims. Close but separable — an operator can be explicit *and* lying (an
  overloaded `+` is explicit at the call site but lies about the operation). Honesty forbids the lie
  even when it would be explicit.
- **Agent rule:** don't give a symbol, type, or claim a meaning it doesn't truly have; if a
  construct would mislead about what it is or does, reject it even if it's convenient or explicit.

### M.4 — Build order: never build on the incomplete (process AND design)
- **Principle (one rule, two domains):** **never build on a layer that is not yet complete and
  stable.** This governs two domains at once — the *process* of building the compiler, and the
  *design* of the language itself — because they are the same principle: a foundation crack propagates,
  so you fix the foundation *before* building up, never paint over the wall.
- **Domain 1 — the compiler pipeline (process):** the compiler is a **layered pipeline** —
  `lexer → parser → checker → codegen` — where each stage consumes the previous stage's output **as
  ground truth**. The lexer is the *primary source of truth*: it is the first component to *see* the
  raw text and *categorize* it into tokens. Everything downstream operates on that categorization,
  never on the raw text.
  - **The failure mode (do not do this):** if an earlier layer is incomplete (e.g. the lexer lacks a
    token for a construct), a later layer is *forced to leak* — the parser re-inspects raw text,
    re-lexes a string, or special-cases a byte by hand to compensate. That is an **abstraction leak /
    hack** in the foundation. A crack in the foundation propagates: every layer above compensates for
    the hole below, responsibilities blur, and ownership is lost.
  - **The rule:** complete and correct each layer (and the structural decisions it rests on) **before**
    building the next. Each stage honors its input→output contract without bleeding into its neighbor's
    job.
  - **Consequence for sequencing:** before writing the **parser**, the structural decisions it depends
    on must be *decided*, not improvised mid-code — the exact `variant` syntax, how `match`
    binds/extracts fields, etc. Writing the parser on undecided syntax reproduces exactly the leak this
    principle forbids.
- **Domain 2 — language design (do not couple a feature to an unstable form):** the *same* rule
  governs what you add to the language: **do not build a dedicated feature (especially syntactic sugar)
  on top of a form you already know is transitional.** A feature coupled to a shape that will change is
  born with an expiry date — when the shape evolves, the feature dies or needs re-specification (rework,
  the same propagating crack, now in the language instead of the compiler). Prefer the **general
  mechanism that survives the evolution** of the underlying shape.
  - **The canonical case (B.16):** the `?` error-propagation operator was refused in the seed precisely
    because it would be sugar coupled to the *transitional* `Valor | error` shape (which evolves to a
    generic `Result(T)`). The `match`, operating over *any* variant, does not depend on the unstable
    shape — it **survives any evolution of the error model** unchanged. So the seed uses `match` (the
    general, stable mechanism) and defers `?` until `Result(T)` is the *stable* final form. This is M.4
    in the design domain: don't build `?` on a layer (`Valor | error`) that is not yet final.
- **Why one principle, not two:** "don't write the parser before the lexer is done" and "don't write
  `?` before the error shape is final" are the *same instruction* — *do not build on the incomplete*.
  One is about the order of constructing the compiler; the other about the order of growing the
  language. Both forbid coupling new work to an unfinished foundation. (M.4 was first illuminated only
  in the process domain; B.16 lit the design domain — the law always governed both.)
- **Agent rule:** (process) do not implement a later compiler stage while an earlier stage, or a
  structural decision it requires, is incomplete — if you reach back to a previous stage's data to make
  something work, **stop** and finish the earlier layer first. (design) do not build dedicated
  syntax/sugar over a shape known to be transitional — use the general mechanism that survives the
  shape's evolution, and defer the specialized feature until the shape is final.

### M.5 — Austerity as an active design force: restriction forces deliberation
- **Principle:** the seed's austerity is **not merely a consequence of minimalism — it is an
  active design force.** Each thing the language *withholds* removes a shortcut, and removing
  shortcuts forces intention. Code forced to be deliberate comes out **refined** ("afinado"). The
  list is cumulative: **rigid arity** (every call passes every argument — no default hides a
  dependency), **no overload** (one name, one function — no stacking variations under a name),
  **`if`-before** (no checkers — the pre-condition is visible at the use site), **no closures**
  (state is passed explicitly, not captured magically), **value-semantics, no `ref`** (you see the
  copy and the return, no mutation-at-a-distance), **methods discouraged** (you think twice before
  binding behavior to data). Lazy convenience produces soup where nothing is clear; restriction
  produces clarity. "Everything must be well thought out" is a **feature, not a cost**.
- **The recursion:** the language uses its own austerity to guarantee the quality of the code that
  builds it. The seed compiler — the first large Teko program — *cannot* be sloppy, because the
  language doesn't let it (no shortcuts for sloppiness). Austerity is a quality guarantee **built
  into the tool**: you don't need external discipline (a linter, obsessive review) to keep the
  compiler refined; the language enforces it. Restriction = discipline, automated. (Compare:
  `if`-before shapes the compiler's style; value-semantics keeps state visible.)
- **The ruler for evolution (when to pay a debt):** austerity is the **default**; a convenience
  must **justify its entry** — prove the gain exceeds the clarity it replaces. The question for any
  evolution feature is not "can we add it?" but "**is it worth the deliberation its absence
  forces?**" Default args, real methods, rich format specifiers, etc. each pay only when their case
  is common enough that the convenience outweighs the clarity lost. This generalizes B.16's "don't
  build sugar over transitory structure": *convenience justifies itself; austerity is the baseline.*
- **Reframe — debt is active, not resignation:** "rigid arity, debt accepted" is not "unfortunately
  we lack default args" but "*good* that we lack them — it forces everything to be thought through."
  The debt **works in our favor** (forcing deliberation) while unpaid. Pay only when the trade flips.
- **Agent rule:** do not add a convenience to make code shorter at the cost of making intent less
  visible. When evaluating any feature for the seed or for evolution, weigh it against the clarity
  the restriction yields — the burden of proof is on the convenience, not on the austerity.
- **Relation to M.0 (see the master rule above):** austerity (M.5) is the most *cedible* principle —
  it governs what *not* to add *above* the metal. When a thing is a **metal operator**, M.0 (lower
  number) wins the boundary: the operator is legitimate, and M.5 is not invoked (a metal operator is
  not a convenience). Triage: *"metal operator or abstract convenience?"* — metal → M.0; convenience
  → M.5. Together they give Teko's profile: **generous below (rich operators at the metal), austere
  above (justified conveniences)**. (Canonical example: the compound bitwise/shift operators `&=`/
  `<<=` are legitimized by M.0 directly — not "exceptions to austerity," but outside its jurisdiction.)

### B.1 — error handling: value, not effect
> ⚠️ **Partially superseded by B.16** — the `?` propagation operator mentioned here
> was removed; error is always handled with `match`. The was→is→why below is kept
> for rationale; for the *current* propagation rule see B.16.
- **Was (explored, rejected):** `raise` + `on error {}` (effect/dispatch), then a
  Java-style checked variant.
- **Is:** error is a **value** — a two-case variant `Value | error`, returned
  normally, handled with `match`, and propagated by returning the error.
  (Originally specified as a `?` operator — **revised by B.16: no `?` propagation
  in the seed; error is always handled with `match`.**) No `raise`/`throw`/`on
  error`, no non-local control flow, no stack unwinding.
- **Why:** coherence with Teko's core values — failure is visible *in the return
  type* (no hidden control flow, no `raises` keyword needed), it reuses `match`
  (exhaustiveness forces handling the error case), and one convention keeps the
  ecosystem uniform. The two-case variant also makes Go's invalid states (both
  value and error set, or neither) **impossible by construction** — a variant is
  exactly one case. **Agent rule:** do not add exceptions, `raise`, `try/catch`,
  or `on error`. Recoverable failure returns `T | error`.

### B.2 — The result of a fallible call is NOT nullable
- **Was (proposed):** make the success value natively nullable (Go-tuple feel,
  `(value?, error?)`).
- **Is:** the result is the variant `Value | error`. The absence-of-success **is
  the `error` case**, never a null.
- **Why:** nullability and failure are *different* absences. `?` (nullability) is
  "may be absent, and that's fine, no reason." The error variant is "failed, and
  here's the reason." Mixing them loses the distinction. A nullable result would
  also reintroduce Go's invalid states. **Agent rule:** `?` is for value-absence;
  the error case is for failure. Never model failure as null.

### B.3 — No tuples
- **Was (would be needed for):** Go-style multi-return `(value, error)`.
- **Is:** no tuple type. Aggregation is `struct` (named) or `variant` (sum).
  Multi-return is a named struct (e.g. `Scan { token, next }`). `(x, y)` is
  *only* struct-destructuring syntax, never tuple construction.
- **Why:** B.1 removed the one use that demanded tuples. Tuples are
  positional/anonymous (`.0`, `.1`) — illegible at scale, the same flaw as too
  many positional params (Teko's convention prefers named over positional).
  `struct` + `variant` already cover all aggregation; a tuple would be a third,
  worse mechanism. The concise struct literal pays the "extra ceremony" cost, and
  destructuring preserves the unpacking ergonomics. Consistent with cutting
  variadics. **Agent rule:** to return multiple values, declare a struct.

### B.4 — `++` / `--` are statement-only
> ⚠️ **Re-grounded under the Constitution.** The original justification cited *frequency in the lexer*
> (pragmatic) and *C's pitfalls* (symptom) — both true, but neither named the governing Law. Exhumed:
> the decision is correct, and each part now cites its Law.
- **Was:** cut entirely (only `p += 1`), on the assumption that with a `for` loop
  the manual increment would be rare.
- **Is:** `++`/`--` exist but are **statement-only** — `p++` on its own line,
  forbidden inside any expression (`arr[i++]` is a compile error). One form
  (`p++`); pre-increment `++p` does not exist (statement-only makes them
  identical, so two forms would be redundant).
- **Why (grounded in three Laws):**
  - **They exist — M.0 (metal):** increment/decrement is a first-class *machine instruction* (`inc`/
    `dec`), so `++`/`--` are legitimate metal operators reflecting the silicon. (The original "most
    frequent operation in the seed, the lexer is full of it" is a *consequence* — common metal
    operations appear often — not the cause. The cause is M.0: it is an instruction.)
  - **Statement-only — M.3 (honesty) + the legibility-local facet of M.2:** C's real pitfalls
    (`arr[i++]`, `x = i++ + ++i`) all arise from using `++` *for its return value inside an
    expression*. There, `++` **lies** — it pretends to be a *value* (the old `i`) while also carrying a
    *side effect* (the increment); an operator that is simultaneously value-and-mutation misrepresents
    what it is (**M.3** — it looks like a read but is a read-and-write). And the side effect is **hidden
    inside the expression** — reading `arr[i++]` you don't see that `i` changes (the *legibility-local*
    facet of **M.2**: the mutation is concealed mid-expression). Forbidding `++` in expressions removes
    both: as a standalone statement `p++` is **honest** — pure effect (it increments), pretending no
    return value, with the mutation visible on its own line.
  - **One form (`p++`, no `++p`) — M.5 (austerity):** statement-only makes pre- and post-increment
    behave identically (there is no return value to differ on), so two forms would be redundant — the
    convenience of a second form pays no weight.
  - `+= 1` still exists for non-unit steps. **Agent rule:** emit `p++`/`p--`
    as a standalone statement only; never inside an expression (that would let `++` lie — M.3).

### B.5 — Strings: single delimiter + one-level interpolation (seed); raw/multi-line are evolution
> ⚠️ **Re-grounded under the Constitution (M.5 audience).** The original B.5 admitted four combinable
> prefixes (`$`, `@`, `"""`, and a `$`-count trick). Exhumed under **M.5 (austerity)** — and with
> **M.0 absent from jurisdiction** (strings are *composite*, not hardware scalars, so no "metal"
> argument rescues a convenience here; M.5 reigns alone) — the seed was found to carry **bloat** and
> **evolution-grade features mistaken for seed**. The was→is→why is kept; the *current* seed is below.
- **Was (explored, then over-grown):** backtick-for-interpolation vs quotes; then a four-way
  quote/backtick × single/triple matrix; then the C# model with **four combinable prefixes** (`"`,
  `$`, `@`, `"""`) plus a **`$`-count trick** (the *count* of `$` set interpolation depth, so a
  literal `{` needed no escape). That last form was the standing decision before this audit.
- **Is (the seed, after the M.5 audience — only what pays its weight):**
  - `"..."` — the **single delimiter** (escapes processed). **Essence** (M.5): there is no smaller
    way to write a string; not a convenience, the irreducible minimum.
  - `$"...{expr}..."` — **one-level interpolation**. A **justified convenience** (M.5): its weight is
    minimal (one prefix char, no new delimiter syntax) and its use is maximal (logs, messages,
    queries — the most common string-building idiom), and the alternative (manual `$`-concatenation)
    is markedly less legible. Austerity is **not asceticism** — a convenience of minimal weight and
    maximal use *pays its toll*. A **literal `{`** uses the standard escape **`{{`** (as C#/Python/
    Rust do) — trivial and universally known.
- **Removed as bloat (M.5 — the verdict):** the **`$`-count trick** is **cut**. It solved a *rare*
  case (a literal `{` inside an interpolation) with a *non-obvious* mechanism (counting `$` to mean
  interpolation depth — no one guesses that) when a *trivial, universal* escape (`{{`) already exists.
  Its original justification — "less noisy than Rust's `#` suffixes" — was **aesthetic** (appearance),
  and **M.5 judges weight × use, not appearance**. *Elegance ≠ austerity* (M.5's own ruler): the trick
  traded a known trivial escape for rare ingenuity. Cut.
- **Deferred to evolution (not seed — wrong temporal jurisdiction):**
  - **`@"..."` raw** (no escape processing) — useful for regex and Windows paths, but those are
    **rare in bare-metal seed code** (the bootstrap lexer/parser write neither). Evolution, when the
    language reaches domains where raw text is common. In the seed, escape with `\`.
  - **`"""..."""` multi-line** — useful for templates, embedded SQL, long literals, which are
    **evolution domains**; the seed's `\n` covers the short case, and the bootstrap compiler writes no
    long templates. Evolution.
- **Why (grounded):** **M.5** — the seed keeps only the irreducible (`"`) and the one convenience
  whose weight is paid by its use (`$` one-level). Everything else either failed the weight test (the
  `$`-count trick) or belongs to a later milestone (raw, multi-line). The backtick is gone (was
  redundant with `$`). **Agent rule:** in the seed, write strings with `"..."` and interpolate with
  `$"...{expr}..."` (one level; literal brace is `{{`). Do **not** implement the `$`-count trick, raw
  `@"..."`, or multi-line `"""..."""` in the seed — raw and multi-line are evolution; the `$`-count is
  removed entirely.

### B.6 — Pointers (`ptr`/`uptr`) are opaque and minimal
- **Was (considered):** typed pointers `const_ptr`/`mut_ptr` (Rust `*const`/`*mut`)
  to guard immutability across FFI; and a brief temptation to add `ref` because
  marshalling has conversions.
- **Is:** `ptr` / `uptr` are **opaque, word-size numeric** types for extern
  marshalling only — transport-only, **not dereferenced in Teko code**. No `*`
  syntax. No `const_ptr`/`mut_ptr`. `ref` stays deferred (evolution).
- **Why:** FFI is unsafe by nature — accepted and discouraged, not papered over
  with a typed-pointer scheme that would only pretend to give a guarantee the C
  side can break anyway. If the `const`/`mut` pointer distinction were ever
  needed, that is the signal to implement `ref`, not to grow pointers. Marshalling
  conversions are confined to the FFI boundary, not "everywhere," so they don't
  justify `ref`. **Agent rule:** use `ptr`/`uptr` only to pass addresses to/from
  extern; never dereference them in Teko; do not add typed pointers.

### B.7 — State flows without `ref` (caller-local + return)
- **Was (the tension):** value semantics with no `ref` seems to forbid a function
  from advancing shared state (a lexer needs the position to advance).
- **Is:** the mutable state (`mut pos`) lives in the **caller**; pure helper
  functions take `(source, pos)` and **return** the new state (in a struct, since
  there are no tuples). The caller reassigns. Passing a struct instead of an array
  does *not* help — the fix is returning state, not changing what is passed.
- **Why:** keeps pure value semantics (no aliasing, no `ref`) while letting state
  advance — explicitly and visibly. Slightly more verbose than `ref`, but honest.
  Validated by the lexer (A.1). **Agent rule:** to "mutate" caller state, return
  the new value and have the caller reassign; do not request `ref`.

### B.8 — Cross-reference: types vs namespaces
- **Is (types):** mutually recursive **types are allowed** (`Node` contains
  `Node`; `A` contains `B` contains `A`). Resolved by compiler-managed internal
  indirection — no exposed pointer, no `Box<>`. The AST requires this.
- **Is (namespaces):** cyclic **namespace dependencies are forbidden** — a compile
  error, even though whole-project compilation *could* resolve them (Go's
  approach). Module dependencies form a DAG, flowing one direction.
- **Why:** data legitimately cycles (trees/graphs are self-referential); modules
  cycling is a design smell (a tangle you can't reason about, test, or reuse in
  isolation). Forbidding namespace cycles forces clean architecture and matches
  the compiler's own one-directional flow (lexer → parser → checker → codegen).
  **Agent rule:** recursive types are fine; a namespace import cycle is an error
  to be broken, not worked around.

### B.9 — File organization & the `teko::` root
> ⚠️ **Re-grounded (round on namespaces).** The original "namespace rule" said *cross by qualifying the
> last segment* (`lexer::Token`) and *never put `teko` in a `.tkp`*. Both are **revised** below under the
> **absolute-addressing principle**: there is **no relative or indirect access** — the canonical root
> rules, and a cross-namespace reference is the **full absolute path from the canonical root**. The parts
> that change are marked; the kernel model, tests-beside-code, and the two uses of `::` stand.
- **Is (project root):** the **stdlib is not an imported library** — it is part of
  the compiler, *injected at compilation*. `teko::` is the reserved native root
  (e.g. `teko::print`, `teko::strings`, `teko::error`): non-shadowable, not
  aliasable, never a dependency. Writing native code (under `teko::`) is
  gated by **repo governance** (you have commit access to the compiler), not by
  cryptography. (Signing is for distribution provenance, a separate concern.)
- **Is (the canonical root *is* the project name — revised).** Every project has a
  **canonical name**, declared in its `.tkp`, and that name **is the root of every
  absolute path** to its symbols. **The language's own project is canonically
  `teko`** — so its modules are `teko::lexer`, `teko::parser`, `teko::checker`, …,
  and the injected stdlib (`teko::print`, `teko::math::div`) **emanates from the
  same root**: the compiler and its stdlib are *one* `teko` project. (This **revises**
  "never put `teko` in a `.tkp`": no *user* project may claim `teko` — it is
  reserved — but the language's own `.tkp` declares `name = "teko"`, the sole bearer
  of the canonical root.)
- **Is (absolute addressing — revised namespace rule).** There is **no relative or
  indirect access**. Within the **same** namespace (directory), references are
  **bare** (files in a directory are aggregated and see each other nude). Crossing
  to **any other** namespace — *even within your own project* — uses the **full
  absolute path from the canonical root**: `teko::lexer::Token`, not the old
  last-segment `lexer::Token`. The source root (`src/`) stays **invisible**
  (namespaces start after it), but the canonical name prefixes everything:
  `src/lexer/…` in project `teko` → namespace `teko::lexer`. **Nested namespaces**
  are sub-directories, joined by `::` like any segment (`teko::parser::types`). A
  name that collides with a sub-namespace segment (e.g. a function named the same as
  a sub-namespace) makes `foo::x` ambiguous → **compile error**. *(The ergonomic
  shortening is `use` — alias-only, → B.32; re-export is **abolished**, → B.32.)*
- **Is (the two uses of `::` — unchanged).** `::` is **namespace qualification**
  (`teko::lexer::Token`) **and** static-member access (`Enum::Member`, `Node::MAX` —
  always remains). `.` is separate: instance/runtime access (`n.value`).
- **Is (visibility — `pub` vs `exp`).** Three levels, private being the *absence* of
  a keyword: **(nothing)** = private to the namespace (directory scope); **`pub`** =
  **public within the project** — visible across the project's namespaces, but
  **does NOT enter the binary's header** (its signature is not exposed to external
  linkers); **`exp`** = **exported in the binary header** (the library's public ABI
  surface), and therefore **public by definition** (it makes no sense to export what
  is not public). So `pub` is "open inside the house," `exp` is "open to the street."
  (In the C seed-compiler port, `exp` ↔ a declaration in the `.h`; `pub`/private ↔
  `static`-or-internal in the `.c`.)
- **Is (the `.tkp` manifest).** Declares the project's **canonical name**, **artifact
  type** (executable / library), **dependencies** (+ aliases), and the **source
  root** (e.g. `src/`). Its **format is TOML** (→ B.33). The source root is *invisible*
  in namespaces and imports — you never write it in a path (`teko::lexer`, never
  `teko::src::lexer`).
- **Is (tests):** `.tkt` test files live **beside** the `.tks` they test, in the
  **same directory/namespace** — so they see private members without breaking
  visibility. No `/test` directory. The extension marks them: compiled in the test
  profile, ignored in release.
- **Why:** the kernel model — what belongs to the language lives under the
  compiler, not the package ecosystem. **Absolute-from-canonical-root** (no relative,
  no indirect) makes every path *tell the truth* about where a symbol lives (**M.3**),
  with **one** addressing scheme rather than two (**M.5** — the principle that
  corroborates the whole revision). Tests-beside-code is the only way to test private
  members without poking holes in visibility. **Agent rule:** the canonical root is
  the project name (`teko` for the language); cross namespaces by full absolute path
  (shorten with `use` aliases, never relatively); never write the source root in a
  path; keep tests beside code.

### B.10 — Magic closures out (they lie); behavior+state returns via function pointer / `use` / `inject`
> **Re-grounded under the Constitution.** Originally "closures out + function pointers deferred,"
> justified by *cost* and *concurrency hazard*. The audience exhumed this: those are **symptoms**; the
> **primary law is M.3 (honesty)** — a magic closure *lies about what it is*. And the **capability**
> people want from closures (passing behavior that carries state) is not banned — it returns through
> **three honest forms**, distinguished by *where the state comes from*. **Re-analyzed (the seed uses
> form 1):** the three forms have *radically different costs*, and the dividing line is **stateless vs.
> stateful**, not function-vs-not. **Form (1) — stateless function passing — is SEED** (it passes all
> six laws; none of the closure dangers apply because there is no captured state); **forms (2) `use` and
> (3) `inject` remain evolution** (they carry state, with its lifetime). This was triggered when the
> operator layer (A.5) was found to *want* function passing in the seed's own parser.
- **What is banned, and why (M.3 primary):** a **magic closure** (JS/Lua-style — `(x) => x * m`
  capturing the surrounding scope **implicitly**) is **permanently out**. Ontologically it is **not a
  function** — it is a `(code-pointer, captured-state)` pair, i.e. a *struct/object* that **disguises
  itself as a function**. It lies twice: it does not *declare* what it captures (the whole scope enters
  implicitly), and the reader cannot *see* what it carries. The state is **never declared anywhere**.
  That is the lie M.3 forbids — the same category as `+`-that-concatenates or a fake `char` type. The
  *cost* (state-with-a-lifetime, M.5) and the *concurrency hazard* (captured mutable state shared
  across threads, M.1) are **consequences of the hidden state** — symptoms of the M.3 lie, not the root.
  (Magic closures are *also* already excluded by the nested-function ban — a closure must be a function
  inside a function.) **The ban is on the dishonesty, not on the capability.**
- **The capability returns through THREE honest forms** — the axis is *where the state comes from*, and
  each form **declares its origin** (so the reader always knows the provenance of every value):
  - **(1) Function pointer — behavior with NO state — SEED.** Pass an autonomous module-level function;
    it is just a code address. `map(numeros, dobra)`. **Re-analyzed to seed** because the seed's *own*
    parser needs it (the operator ladder, A.5, repeats five identical fold-left loops a passed sub-parser
    would collapse) and it **passes all six laws**: **M.0** (a code pointer + indirect call is metal),
    **M.1** (no captured state → none of the closure dangers: no concurrency hazard, no dangling, no
    arena/lifetime — a module-level function lives the whole program), **M.2** (passing `parse_and` is
    explicit, no magic capture), **M.3** (a code address does not lie — there is no hidden `(code,state)`,
    only `code`; the lie that banned closures was the *state*, absent here), **M.4** (the seed uses it,
    and it depends on nothing unbuilt — just an address), **M.5** (trivial weight × high use). Crucially,
    it needs **no `ref`**: a function pointer is a *code* address, not a *data* reference — it lives
    entirely outside the `ref` ban's jurisdiction (which governs aliasing of mutable *data*, with arenas
    and lifetimes a module-level function does not have).
    - **Syntax (designed here — B.10 gave the concept, this gives the form):**
      - **The function type** is `fn(ArgTypes) -> RetType` — `fn` doubles as "declare a function" and
        "the type of a function," coherently (it means *function* in both; no lie, no new keyword — M.5).
        Example: `fn([]lexer::Token, u64) -> bool`. It may be **named** with a `type` alias (nominal,
        B.13) to cut repetition: `type OpPredicate = fn([]lexer::Token, u64) -> bool`.
      - **Passing** is by **bare name**: `parse_level(parse_and, is_oror)`. **Calling** is with
        parentheses: `parse_and(x)`. The presence of `()` distinguishes *call* from *refer* — honest
        (M.3), explicit (M.2), and it needs **no `&`** (the C/Rust "address-of"): introducing `&` would
        reintroduce the very symbol the data-`ref` ban removes, so the absence of `()` carries the whole
        distinction instead.
      - **Confined to top-level functions by construction (M.1 — exclusion-by-construction):** the only
        thing passable is the name of a module-level function, because Teko *already* bans nested
        functions (no inner function to close over) and has no capture syntax. So forms (2)/(3) — which
        need captured state — are **inexpressible in the seed by construction**, not by a rule that
        forbids them. The seed/evolution boundary here is a structural impossibility, not a prohibition.
    *(Forms 2 and 3 below remain evolution; the seed has form 1 only.)*
  - **(2) `use` — behavior + state from the caller's LOCAL scope, shared explicitly by the user.** A
    module-level function is passed to a higher-order, and a local variable is fixed into it, **named
    in a `use` clause the user writes**:
    ```teko
    fn maior_que(item: i32, limite: i32) -> bool { item > limite }

    fn processa(numeros: []i32) -> []i32 {
        let limite = calcula_limite()
        filter(numeros, maior_que use (limite))   // local state, explicit & nominal
    }
    ```
    Modeled on PHP's `use` — capture is **nominal and explicit** (only what you list, never the whole
    scope). The **copy form** (`use (limite)`) is safe today (value semantics); the **reference form**
    (`use (&limite)`) waits for `ref` + expanded arena control (M.4 design: don't build it before the
    layer that makes it safe). *(Evolution.)*
  - **(3) `inject` — a dependency INJECTED from outside; an auxiliary that *contextualizes* the
    signature (it is not part of it).** The author's metaphor fixes the ontology precisely — think of a
    function as a piece of text: the **`#` lifetime directive is the title** (a *composition* — its own
    structural layer, preceding and orienting), the **signature is the sentence** (the contract the
    *caller* reads), and **`inject` is the footnote** (it *contextualizes* the sentence — injecting
    references — **without being the sentence**). So `inject` is **not part of the signature** and is
    **not a directive** either; it is the third thing — an **auxiliary footnote**. It sits **after the
    `-> return`, before the body** (the body being the whole "chapter"), so the reading order is
    title → sentence → footnote → chapter:
    ```teko
    #singleton
    fn get_current_user() -> User
        inject (http: teko::net::IHttpClient)
    {
        let resp = http.get("/me")
        // ...
    }
    // call site — the caller fulfills ONLY the signature (the sentence); `http` is injected:
    let u = get_current_user()
    ```
    The **first (normal) parameter list is the sentence's contract — what the *caller* supplies**; the
    **`inject` footnote is where the function's injected dependencies come from — what the *compiler*
    supplies**. Delimiter inside `inject ( … )` is **comma** (it reads as an argument list — B.26).
  - **Why a footnote, not part of the signature (M.3 — the deepest reason):** if `inject` were *in* the
    signature (part of the sentence), it would **lie** — it would present `http` as part of the contract
    the caller fulfills, but **the caller never passes `http`** (the compiler injects it). So `inject`
    is **not** caller-contract. As a *footnote* it tells the truth: "I am not the contract you (caller)
    satisfy; I am context — where this function gets its `http`." It contextualizes by injecting
    references (the dependency and its type) exactly as a footnote injects references into a sentence
    without being the sentence. Result — **three honest layers, each true about what it is:** the
    **signature/sentence** = pure caller-contract (M.2: what the caller reads is exactly what the caller
    supplies); **`inject`/footnote** = injection context (does not pollute the contract); **`#lifetime`/
    title** = allocation strategy (composition). The distinction between the two non-signature layers:
    a **directive composes** (a parallel structural layer — the title), an **auxiliary contextualizes**
    (subordinate to the sentence, attached by reference but outside it — the footnote).
  - **Why the word is `inject`, not `with` (M.3 — honest naming) — and not `injects`:** `with` is
    **vague** (it means too many different things across languages — Python context managers, Pascal/JS
    object scope, SQL CTEs), so it does not *say what it does*; that fails Teko's honest-naming standard
    (where `mut` says mutable, `pub` says public — the name *is* the thing). `inject` is honest: the
    word **is** the mechanism. The form `inject` is chosen over `injects` (third person) because
    **`injects` would lie about the agent**: the function does **not** inject anything — the *compiler*
    does the injecting; the function (or property) merely **receives** an injection. `injects` would
    wrongly cast the function as the actor. The function is the passive recipient, so the word must not
    claim it acts. (This is M.3 at the finest grain — honesty about *who does what*.)
  - **DI lifetimes — `#singleton` / `#scoped` / `#transient` (compiler directives, `#` — the title) bind
    the dependency to an ARENA:** the lifetime is not merely "when to create/destroy" (as in C#) — it
    **declares which arena the dependency lives in**, and it is a **compiler directive** (the reserved
    `#` — communication with the compiler). *Why a directive (title), not a footnote like `inject`:* the
    lifetime is **pure allocation strategy** — it tells the compiler *how to allocate* (which arena,
    which lifetime-span), a **compile-time** decision that does not change what the program *computes*.
    A directive **composes** (it is the title — a structural layer that precedes); it sits on its own
    `#` line, preceding the subject (the `bind`, or the receiver):
    - `#singleton` → one instance for the whole program → the **root/global arena** (lives the
      program's lifetime).
    - `#scoped` → one instance per scope (request, session, transaction) → that **scope's arena** (born
      and freed with the scope).
    - `#transient` → a new instance per injection → the **local arena** of the injection point
      (ephemeral).

    Each lifetime = a lifetime-span = a specific arena. **Why this beats C#:** C# runs a **live runtime
    container** that decides, per request, which instance to hand out — lookup, reflection, overhead.
    Teko makes the lifetime a **directive on the binding**, so the compiler knows at compile time
    *which arena* the dependency lives in and *when* to allocate/free — **zero runtime container, zero
    reflection, zero overhead** for that decision. This is **M.0** (the allocation strategy compiles to
    direct arena placement) and **M.1** (the lifetime ties the dependency to a specific arena, so the
    compiler can *see* whether a reference would outlive its arena — a lifetime error caught at compile
    time).
  - **The three layers and their moments — title (compile-time), sentence (caller-contract), footnote
    (runtime materialization):** `inject` (footnote) and the lifetime (title) are **different layers**
    because they govern **different moments and roles**. **`inject` (footnote)** names what the function
    *receives* by injection — the **object materializes at runtime** (the compiler *orchestrates* the
    resolution, but the object is born at runtime, not at compile time), and it *contextualizes* the
    sentence without being it. **The lifetime (`#` title)** is pure *allocation strategy* — a
    **compile-time** decision composing with the subject. So this is **ontological precision, not a
    compromise**: each layer lives in the category its nature demands. And neither the title nor the
    footnote **fragments the contract** (an M.2 worry): the caller's contract *is* the sentence
    (signature); the title and footnote were never part of it — they *contextualize* it (the title with
    allocation strategy, the footnote with injection provenance), so reading the signature alone is
    reading the complete caller-contract.
  - **M.3 condition on `inject` (traceability):** for `inject` to stay honest, the compiler's
    resolution must be **traceable** — there must be an explicit **binding** declaring *which* concrete
    type fulfills an interface (carrying its lifetime directive), so that following the trail you
    always know what was injected and where it lives. DI by compiler *guessing* (scanning, hidden
    convention) would be dishonest. A binding sketch (form not final):
    ```teko
    #singleton
    bind IHttpClient -> HttpClient
    #scoped
    bind IUserRepo -> SqlUserRepo
    ```
    *(The binding's exact placement — a project bindings file, an entry-point block, a per-namespace
    registry — is **not yet designed**; see open item.)*
  - **`inject` needs interfaces — which precede full OOP (M.4 ordering):** the *power* of DI is
    injection **by contract**, not by concrete type. `inject (http: HttpClient)` (concrete) makes the
    binding trivial but pointless — injecting a concrete type directly is just passing an argument.
    `inject (http: IHttpClient)` (an **interface**/contract) is the point: the binding says "when
    `IHttpClient` is asked, inject *this* implementation," so the implementation can be swapped (real,
    mock for tests, fake) without touching the consumer. **Therefore DI depends on interfaces existing
    first** — and interfaces can/should arrive **before full OOP** (you can have contracts without
    inheritance/override). This orders the evolution (M.4, design): `interfaces → DI (inject + binding)
    → full OOP`. `inject` cannot be built before interfaces; interfaces need not wait for OOP.
  - **Scope of `inject` — functions AND properties (no constructor bloat):** `inject` applies in two
    places: (1) a **function's** second argument list (shown above); and (2) as an **auxiliary on a
    type's properties** — the dependency is injected **directly into the property that uses it**,
    instead of being threaded through a swollen constructor:
    ```teko
    type UserService = ... {
        inject repo:   IUserRepo
        inject logger: ILogger
        // the type's functions use self.repo / self.logger — injected,
        // not passed through an overloaded constructor
    }
    ```
    This avoids the classic "constructor hell" of DI in C#/Java/Angular (constructors that become a long
    list of dependencies). The dependency lives on the property that needs it; the constructor stays
    about *constructing*, not wiring. *(`inject` and `static` remain **distinct** — `static` already
    exists as a function without a `self` on the type, via `::`, or a free function in the namespace;
    `inject` is dependency injection. Different needs, not merged.)*
- **Separators in `use ( … )` and `inject ( … )` — identical to any argument list:** both *are*
  argument lists (with special semantics — `use` = local state, `inject` = injection), so they
  **inherit the argument-list separator rules unchanged** (comma inside `()`, B.26). No new rule (M.5:
  reuse the existing one). E.g. `inject (http: IHttpClient, logger: ILogger)`, `use (limite, fator)`.
- **`ref` (`&`) — C# model, three positions (evolution, needs safer arenas):** when `ref` arrives it is
  **explicit in all three positions** — declaration (`fn incrementa(ref c: i32)`), call site
  (`incrementa(ref x)`), and return (`-> ref i32`) — nothing implicit (M.2/M.3: you see "by reference"
  at every point). It depends on **safer/expanded arena control** (without it, a reference escapes the
  arena). The `use (&x)` reference form uses this same `ref` mechanics; until then, `use` is copy-only.
- **Metal representation (open — M.4):** the injection list *appears* identical to ordinary arguments
  (the second list becomes params the compiler fills instead of the caller). But the **parser must
  decide the *how*** (how it represents the second list, links it to the binding, resolves
  lifetime→arena) **before** the exact metal is known. So the metal form is **reserved, not fixed** —
  M.4 forbids settling the metal on a parser that isn't written yet. Future; may change.
- **The jurisprudence (Teko is consistent with itself):** reject the **magic/hidden** form (the
  closure that conceals state-origin), accept the **honest** forms (each origin named). Same pattern as
  the `Ordering` enum (reject magic `i8`, accept named `enum`) and strings (reject the clever `$`-count
  trick). Every time, the honest form wins; the capability is preserved through it.
- **Evolution dependency chain (M.4 design — build order forced by what depends on what):**
  `function pointer → use (copy)`; `safer arenas → ref (&) → use(&) / inject-by-ref`;
  `interfaces → inject + binding (DI, with lifetimes) → full OOP`. None is in the seed; the order is
  forced by the prerequisites, not by preference.
- **Agent rule:** never implement **magic closures** (implicit scope capture) — ever; they lie (M.3).
  **Stateless function passing (form 1) IS seed** — pass a module-level function by bare name, type it
  `fn(args) -> ret`, call it with `()`; no `&`, no captured state, confined to top-level functions by
  construction. Do **not** implement `use` (form 2) or `inject` (form 3) in the seed — those carry state
  and are evolution. When they arrive, they are the honest forms of "behavior + state," distinguished by
  state-origin (none → function pointer, *now in seed*; caller-local declared → `use`; injected →
  `inject`). Keep `inject` (injection) and `static` (type-associated/free function) separate. Any
  `inject` requires a traceable binding with a lifetime (`singleton`/`scoped`/`transient` → arena), never
  compiler guesswork.

### B.12 — Primitive types: refuse false distinctions, honor real ones
- **Is (`char` = `byte` = `u8`):** there is no separate `char` or `byte` type.
  A character (ASCII/byte sense), a byte, and an unsigned 8-bit integer are **the
  same type**: `u8`. `'a'` is a byte literal — convenient *notation* for the value
  `97`, not a distinct type. Strings are `[]u8` (UTF-8, length in bytes).
- **Is (`bool` is a distinct type):** `bool` exists, with the keyword constants
  `true` / `false`. It is **not** "a `u8` by another name."
- **Why — the same principle yields opposite results:** Teko refuses type
  duplication that *fabricates* a distinction the machine doesn't have, but keeps
  types that reflect a *real* semantic distinction.
  - `char` vs `u8`: same value set (0–255), same semantics (integer arithmetic) →
    **no real distinction** → one type (`u8`). Inventing `char` would *lie*
    (imply char and byte differ when they're the same 8-bit register). Refusing it
    *tells the truth*. Note this is the good kind of sugar: `'a'` is **notation**
    sugar (how you write a `u8`), not **type** sugar (a fake distinct type).
  - `bool` vs `u8`: different value set (two values, not 256) and different
    semantics (boolean algebra `&&`/`||`/`!` and conditions, **not** arithmetic —
    `true * 3` is nonsense) → **a real distinction** → a separate type. Having it
    *tells the truth*.
- **Why `bool` is 1 byte, not 1 bit (and that's correct, not a limitation):** the
  machine addresses **bytes**, not bits — the byte is the quantum of addressing.
  The smallest object that can have an address (be pointed to, put in an array,
  passed by value) is a byte. So `bool` = 1 byte in *every* language; it is the
  hardware, not the compiler failing to "control" the size. Crucially, **size is
  irrelevant to whether `bool` is a distinct type** — the distinction is
  *semantic* (two values, boolean algebra), independent of the physical byte.
- **Bit control lives in `flags`, not `bool`:** when you have *many* related
  booleans and want bit economy (pack 8 into one byte instead of 8 bytes), use
  `flags` (power-of-two, bitwise) — the tool *for* packing bits. A lone boolean is
  `bool` (1 byte, clear semantics); a set of booleans needing economy is `flags`.
  The "wasted" 7 bits of a lone `bool` are the price of addressability and don't
  matter for a single flag. (`flags` is post-alpha; in the seed, use `bool`.)
- **Agent rule:** do not add `char` or `byte` types — use `u8` (and `'a'` notation).
  Do not try to make `bool` 1 bit. For packed boolean sets, that's `flags`
  (post-alpha), not a narrower `bool`.

### B.13 — Nominal typing: `type X = Y` creates a distinct type (no aliases)
- **Was (earlier decision, now rewritten):** `type X = Y` was described as
  unifying construct-declaration *and* **alias** — e.g. "`type Meters = i32` gives
  a free alias," meaning `Meters` *was* `i32` (interchangeable). The `=` meant
  "is defined as," sold as giving transparent aliases for free.
- **Is:** Teko is **nominally typed**. `type X = Y` creates a **distinct new type**
  whose identity comes from its **name**, not its structure. There are **no type
  aliases**.
  - `type Meters = i32` → `Meters` is a *distinct* type from `i32` (not an alias);
    not interchangeable without explicit conversion.
  - `type A = struct {x: u8}` and `type B = struct {x: u8}` → *distinct* types
    despite identical structure (identity is the name: A is A, B is B).
  - `type Pendente = bool` and `type Ativo = bool` → *distinct* types
    (`Pendente` ≠ `Ativo` ≠ `bool`).
  - The `=` syntax stays; its meaning is now uniformly "define a new nominal type
    X with the structure/behavior of Y," never "X is an alias of Y."
- **Why:** the impasse that forced this — a `variant` is a union of *types*
  (B.14), discriminated by type, so two cases must be distinct types. With
  aliases, `type Pendente = bool` would make `Pendente` *be* `bool`, so
  `Pendente | Ativo` would be `bool | bool` — non-distinct, indistinguishable in
  `match`. The decisive consistency argument: structs are *already* nominal
  (`type A = struct{...}` and `type B = struct{...}` must be distinct, or variant
  unicity breaks). Treating structs as nominal but primitive aliases as structural
  would be exactly the kind of **false distinction Teko refuses** (B.12). So
  *uniform* nominal typing: name creates identity for struct, enum, and primitive
  alike. Bonus: strong safety (you cannot mix `Meters` with raw `i32`, or a
  user-id with a product-id — the compiler rejects it). Cost (accepted):
  transparent aliases are gone; equivalence needs explicit conversion.
- **Behavior (newtype is transparent):** a nominal type derived from a base
  (`type Meters = i32`) **inherits the base's operations** — `Meters + Meters`
  works (same type), `Meters + Polegadas` is an error (distinct types). An
  operation within a single newtype returns that newtype. (Exotic dimensional
  cases like `Meters * Meters` are out of scope for the seed.) This gives the
  safety barrier without the ceremony of converting for every operation.
- **Agent rule:** never treat `type X = Y` as an alias — it is a distinct type.
  To move a value between two nominally-distinct types (even same-structure),
  convert explicitly. Same-named-structure types do NOT unify.

- **Emergent benefit — DDD is built into the type system (not a library):**
  nominal transparent typing makes **Domain-Driven Design cheap and fundamental**.
  A domain type — `UserId`, `Email`, `Meters`, `OrderId` — distinct from its
  representation and from sibling domain types, is **one line**:
  ```teko
  type UserId    = i32
  type ProductId = i32   // distinct from UserId despite identical representation
  ```
  Contrast C#, where a distinct `UserId` needs a wrapping struct plus operators,
  conversions, equality, hashing — so much boilerplate that in practice people use
  raw `int` and hope not to swap a `UserId` with a `ProductId`. In Teko correct
  domain modeling is *viable because it's cheap*. The capability lives in the
  **foundation** (the nominal type system), not in a library or convention layered
  on top — it is DDD "at the root."
- **Operation rules (the Go model) — the precise boundary:**
  - **Untyped literals adapt** to the newtype: `userId + 1` works (the `1` is an
    untyped literal that takes the newtype).
  - **Same newtype operates freely:** `userId_a == userId_b`, `meters_a + meters_b`.
  - **Everything else needs an explicit cast** — this is the cost, and the point.
    A *typed* value of the base type (`userId + raw_i32`) requires a cast; a
    *different* newtype (`userId + productId`) requires a cast. Both are errors
    without it, even though all are `i32` underneath. This is the domain barrier:
    you cannot accidentally add a user-id to a product-id or to a raw count.
- **Agent rule (DDD):** model domain concepts as one-line nominal types
  (`type Email = str`); rely on the compiler to reject cross-type mixing. Expect
  explicit casts when crossing the base type or sibling types — that ceremony is
  the safety, not an inconvenience to design around.

- **"IS" vs "REPRESENTS" — the verb carries the whole decision (precise wording):**
  `type Ident = Type` means **Ident _IS_ a type** — its own full identity — that
  *has the structure/behavior of* `Type`. It does **not** mean Ident *represents*
  `Type`. The distinction is exact: an **identity identifies** (it *is* the thing);
  a **representative delegates/proxies** (it acts *on behalf of* the thing, whose
  real identity lives elsewhere). "Represents" is the vocabulary of an **alias**
  (the proxy delegates identity to the base, hence interchangeability) — which is
  abolished. So **every `type X = Y` is IS, never REPRESENTS**; `Y` is the form the
  new type *has*, not an entity it defers to. Canonical analogy (the author's): *you
  **are** an AI agent, you don't **represent** an AI agent* — a representative
  delegates; an identity simply is. (Early prose slipped, saying a `struct` newtype
  "is a struct" but `type Int = i32` "represents an i32" — same operation, so same
  verb: **IS**. Agents must read `=` as identity, not delegation; "represents"
  wording would wrongly imply alias-style interchangeability.)

### B.14 — Variant is a union of declared types (no inline cases, no constructors)
- **Was (the common model, rejected):** Rust/Zig/TS declare cases *inline* inside
  the sum, with names that appear from nowhere (`enum E { Binary, Number }`) —
  `Binary`/`Number` are neither types nor constants nor labels, but a *fourth
  category* ("variant name") you must learn exists. Confusing on first contact;
  unclear where the name comes from or what a constructor like `Number(f64)` even
  constructs.
- **Is:** a `variant` is a **union of types that already exist**. Each "case" is a
  real, separately-declared type (a `struct`, an `enum`, or a primitive), and the
  variant just unions them with `|`:
  ```teko
  type Number = struct { value: f64 }
  type Binary = struct { left: Node; right: Node }
  type Node   = variant Number | Binary
  ```
  Derived from first principles — "how would a purer language without a `variant`
  construct do this?" Answer: a discriminant + data = a struct with a tag. So the
  cases are just structs, and the variant unions them. No inline cases, **no
  special constructor** (you build a case with the ordinary struct literal,
  `Number { value: 3.14 }`), no fourth category — cases are *types*, a category
  that already exists, so nothing appears "from nowhere."
- **Canonical definition (the author's framing):** a variant is *a single type
  whose self-imbued constraint is the closed set of types it may contain*. The
  value carries which-type (a compiler-managed discriminant), but the inner type
  is **only accessible via `match`** — the mandatory assertion. Without a match
  you only have "a Node" (the union); a match arm proves which case and reveals
  the inner type. **This is the safety C's `union` lacks** (C lets you read any
  member without proving which is valid; Teko forces the proof).
- **Markers without data → `enum`, not empty structs:** a case that carries no
  data is an `enum` member. The enum (a type) enters the union:
  `type Estado = enum { Pendente, Ativo }`, then `variant Estado | Number`. Empty
  structs (`struct {}`) are *discouraged by convention* (no structural
  justification) but not forbidden — the user may use one; a lint may warn. Enum
  covers "markers without data"; variant covers "types with data"; they **compose**
  (an enum can be one case of a variant). They are complementary, not the same
  construct, and neither is a degenerate form of the other.
- **Unicity:** the types in a union must be **distinct** (unioning a type with
  itself is redundancy/error). This relies on nominal typing (B.13): `type A =
  struct{...}` and `type B = struct{...}` are distinct types, so `A | B` is a
  valid two-case union even with identical structure; `bool | bool` is invalid.
- **Construct / use:** constructing a case is the struct literal (`Number {...}`);
  it is accepted where the variant is expected **automatically** (inclusion in the
  union — no explicit cast). Destructuring is the `match` (no cast either — the
  match resolves it, binding variables that carry the value). Nullability is never
  on the variant itself (`variant Color?` is forbidden) — it lives at the use site
  (`?Node` on a variable/return/field), per B.2/B.13. A *field* of a case may be
  `?` (it's a use site); the case and the variant may not.
- **Agent rule:** do not declare variant cases inline with magic names or special
  constructors. Declare each case as its own type (struct/enum/primitive), then
  union with `|`. Build with the type's struct literal; never invent a variant
  constructor. Markers-without-data are enum members, not empty structs.

### B.15 — `match`: one construct, two axes, exhaustiveness with the `when` subtlety
- **Is (one match, unified syntax):** a single `match` construct. Whether a branch
  matches a *value* (literal, range, `2 | 3`) or a *type* (a variant case) depends
  on the subject; the compiler knows which. Same shape
  `match subject { pattern => body }`, different patterns. Not two constructs.
- **Two axes (the binding need differs because the operation differs):**
  - **Axis 1 — match over a single-type value** (scalar: `u8`, `i32`…): branches
    match *values* — literals (`'+'`), ranges (`'0'..='9'`), alternatives (`2 | 3`).
    The subject is already of a known type, so there's no "which type?" — only
    "which value?". **Binding is rarely needed** — you already hold the value (the
    `c` you passed in). The lexer's `match c { '+' => ... }` uses `c` directly. If
    capture is wanted (rare), an `as`: `'0'..='9' as digit => ...`.
  - **Axis 2 — match over a variant (union of types):** branches match *which
    type* (`Number`? `Binary`?). **Binding is intrinsic** — discovering it's a
    `Number` means wanting to *access* the Number (its fields). The content was
    hidden behind the union's constraint (B.14); the match *reveals* it, and
    revealing means naming it.
- **Axis 2 bindings (both allowed, convention guides):**
  - **`Number as n`** → binds the whole value; access via `n.value`. Good when you
    use the object or several fields.
  - **`Binary { left, right }`** → **select by field name**. You list in `{ }` only
    the fields you want; **omitted fields are simply ignored** — no `..` or `_`
    needed for them (better than Rust's `Binary { left, .. }`). It is *nominal
    partial selection*, not positional total destructuring — position doesn't
    matter, the name does. (Relies on structs being nominal, B.13.) **⚠️ REVISED in
    B.26:** the separator inside the binding `{}` is **newline or `;`, not comma** —
    `Binary { left; right }` (or newline-separated), consistent with the absolute
    "`{}` = newline/`;`, no comma" rule. The `{ left, right }` spelling shown here is
    superseded; bind with `;`/newline.
- **`when` guards:** a branch may carry a `when` guard that conditions it beyond
  the type/value pattern — letting you repeat the same assertion with different
  conditions (like C#'s switch expression):
  `Number when n.value > 0 => "pos"`, `Number when n.value < 0 => "neg"`, …
- **Exhaustiveness — forced, with the `when` subtlety (rigorous):**
  - Exhaustiveness is **forced** (the compiler rejects a match that doesn't cover
    all possible cases — the union is closed and known at compile time).
  - The **`_` is an OPTIONAL valve** — available always, mandatory never. If every
    case is covered *unconditionally*, `_` is unnecessary (and would be dead,
    unreachable code). It is needed only when coverage is otherwise incomplete.
  - **A branch with `when` covers *conditionally* → it does NOT count toward
    exhaustiveness.** The compiler cannot (and must not try to) prove a guard
    condition covers all cases — that is undecidable in general. So `Number when X`
    leaves `Number` *not exhausted*.
  - **To close a type that has `when` branches:** either a branch of that type
    **without** `when` (unconditional cover of the rest), **or** a `_` catch-all.
    The `when` opens; something unconditional closes. (This is exactly how Rust
    treats `if`-guards, derived here from the same indecidability logic.)
  - **Two ways to satisfy exhaustiveness, choose by situation:** (1) cover all
    cases explicitly, no `_` — natural for *few* cases (an AST variant); bonus: when
    the type grows a new case, the compiler *breaks* the match and forces you to
    decide. (2) cover some + `_` — natural for *many* cases where writing them all
    is noise (an enum with dozens of members). Convention: explicit when few (to
    get the grow-time warning); `_` when many.
  - **Mandatory `_` (C# style) is rejected:** C# forces `default` because its
    switch lacks real exhaustiveness; Teko *has* it (closed union, known at
    compile time), so forcing `_` on an already-complete match is dead code.
- **Scope is per-branch:** variables bound in a branch (the `as` name, or selected
  fields) exist **only in that branch** — they don't leak to the match or to other
  branches.
- **Agent rule:** one `match` for both values and types. In Axis 2, bind with `as`
  (whole) or `{ field }` (select by name; omitting a field ignores it, no `..`).
  Force exhaustiveness but do not force `_`. Treat any `when`-guarded branch as not
  contributing to exhaustiveness — require an unconditional branch of that type or
  a `_` to close. *(Round 1 of match. Open for round 2: the `?` propagation vs `?`
  nullability symbol collision.)*

### B.16 — No `?` error-propagation in the seed; error is always `match` (revises B.1)
- **Was (B.1 said):** recoverable error is a value `Valor | error`, "propagated with
  a `?` operator (Rust-style)."
- **Is:** there is **no `?` error-propagation operator** in the seed. error
  (`Valor | error`, and whatever it evolves into) is handled **always with
  `match`**. Nullability keeps `?` (see below); error does not use `?` at all.
- **Why (the decisive argument — don't sugar a transitional shape — this is M.4 in the design
  domain):** a `?` propagation operator would be sugar *coupled to the `Valor | error` format*, and
  that format is **known to be transitional** — the alpha uses inline
  `-> Config | error`; evolution moves to a generic `Result(T)`. So a `?` defined
  over `Valor | error` would be born with an expiry date: when the format evolves,
  the rule either dies or needs re-specification (more care, more rework). The
  `match`, by contrast, operates over *any* variant by exhaustive discrimination —
  it is agnostic to the specific result shape, so it **survives any evolution of
  the error model** unchanged. It is also the *general* mechanism (no special
  case): error becomes "just another match over a variant," removing an exception
  that would exist only for error. **Governing law — M.4 (build order, design domain):** *do not build
  a dedicated feature on a layer (`Valor | error`) that is not yet final*; use the general mechanism
  (`match`) that survives the shape's evolution, and defer the specialized one (`?`) until the shape
  stabilizes (`Result(T)`). This is the *same* principle as "don't write the parser before the lexer
  is done" — don't build on the incomplete. (Stated generally: **do not build dedicated syntactic sugar
  over a structure you already know is transitional.**)
- **Collision dissolved:** the round-2 starting worry was a `?` overload
  (propagation vs nullability). Removing error-propagation from `?` *dissolves it
  entirely* — `?` is now **exclusively nullability**: `T?` (nullable type), `?.`
  (safe access), `??` (Elvis). There is no second meaning to disambiguate. error
  and null are disjoint domains with disjoint mechanisms: **null → `?.`/`??`;
  error → `match`.** A nullable never uses error-propagation; an error variant
  never uses `?.`/`??`.
- **Cost (accepted, eyes open):** code with many fallible calls in sequence (the
  parser especially) becomes **verbose** — each fallible call is a `match`, not a
  one-line `?`. This is Go's `if err` tedium, and it is real. The trade is
  conscious: **stability + uniformity now, over concise-but-transitional sugar.**
  Consistent with every other choice in this project that preferred consistency
  over brevity (the always-`=`, nominal typing, the verbose variant).
- **Evolution:** a `?` propagation operator **may return** once `Result(T)` is the
  *stable* final format — the argument that kills it now (transitional shape) stops
  applying when the shape stabilizes. Deferred, not dead.
- **The `match` for error looks like:**
  ```teko
  let left = match parse_term(tokens) {
      Node as n  => n
      error as e => return e      // propagate: return the error opaquely
  }
  ```
- **Agent rule:** never emit a `?` error-propagation operator in seed code. Handle
  `Valor | error` with `match` (propagate via `return e` in the error arm). Reserve
  `?` strictly for nullability (`T?`, `?.`, `??`).

### B.17 — Statement termination & significant newline (lexer decides)
- **The lexer classifies what is lexically known (principle):** if the language
  *knows* `let` is a keyword (not just any identifier), the lexer emits a specific
  token, not a generic `Ident("let")` that pushes classification to the parser. The
  lexer is the first authority on *what each thing is*; the parser decides what to
  *do* with it (grammar). Known at the source → classified at the source. (Analogy:
  the architect knows a conduit is a conduit and specs it; the builder executes.)
- **Newline is the lowest-precedence terminator.** A statement ends at a newline —
  **unless** something pending claims continuation. The *operator/delimiter has
  precedence over the newline*: continuation is a property of the pending operator
  (a binary operator demands a right operand; an open delimiter demands closing),
  not an exception to the newline. The newline only terminates when nothing pends.
- **`;` resurrected as the inline separator.** `;` was abolished as a *mandatory*
  terminator (you don't end every line with `;`), but it **lives as the explicit
  separator for multiple statements on one line** (inline). Normal vertical layout
  uses newline; `;` is the tool when you compress onto a single line (e.g. an
  inline block `{ log(n); n * 2 }`). Newline and `;` are equivalent in function
  (both terminate a statement); they differ only in when you reach for each.
- **Delimiters — expression vs scope (the critical distinction):**
  - **`( )` and `[ ]` (expression delimiters) SUPPRESS the inner newline.** Inside
    them, *the comma separates elements and the closer ends it — newline does not
    rule.* `( )` holds an argument list (comma-separated, each element an
    expression); `[ ]` holds an index expression or a list literal — same
    newline behavior (suppressed) either way.
  - **`{ }` is SCOPE, not a single expression — it does NOT suppress newline.**
    Inside a block, newline still terminates each statement (or `;` does, inline).
    If `{` suppressed newline like `(`, an entire block would collapse into one
    grued statement. So `{` and `(` behave *oppositely* w.r.t. newline.
- **Implementation — the lexer decides (Form 1), with lexical info only:** on a
  newline the lexer checks (a) the previous token — is it a pending operator
  (binary op, `=`, `->`, `=>`, an open delimiter)? — and (b) a **delimiter stack**:
  if the top is `(` or `[`, suppress the newline; if `{` (or the stack is empty,
  i.e. top-level), the newline is active and terminates. The stack handles nesting
  naturally (the top is the innermost enclosing delimiter). All the info needed is
  *lexical* (previous token + bracket depth) — no grammar required — which is why
  the lexer can own this, not the parser.
- **`++`/`--` complete (don't pend):** `p++` is a whole statement, so `p++\n`
  terminates (consistent with statement-only, B.4). Open question noted elsewhere:
  array literal `[1,2,3]` existence is a separate lexical decision (doesn't affect
  newline behavior — `[ ]` suppresses either way).
- **Agent rule:** the lexer, not the parser, decides statement termination, using
  previous-token + a delimiter stack. `( )`/`[ ]` suppress newline; `{ }` does not.
  `;` is only for inline multi-statement separation, never a mandatory line ending.

### B.18 — Comments (lexer round B)
- **Is:** the lexer recognizes exactly two comment forms:
  - **Line:** `//` to end of line.
  - **Block:** `/* ... */`, **nesting** — `/* /* */ */` works. This fixes C's
    classic defect (C's `/* */` doesn't nest, so you can't comment out a region
    that already contains a block comment). Nesting is cheap: the lexer counts
    `/*`-vs-`*/` depth. (The user notes nesting is both important and elegant.)
- **Repetition of the opening char is NOT lexically significant.** `//`, `///`,
  `/////` are *all* just "line comment"; `/* */`, `/** */`, `/*** */` are *all*
  just "block comment." Once `//` opens a line comment, further slashes are text;
  same for `/*`. The lexer treats `///` as `//` and `/** */` as `/* */` — it never
  distinguishes a "doc comment" token.
- **Doc comments are not a language construct** — they are a **future tooling
  convention**. A doc generator (future) may interpret comments starting with `///`
  as documentation by *reading the comment text*, but that is the tool's behavior,
  not the lexer classifying a different token. The seed neither decides nor records
  any doc-comment distinction.
- **Direction (tooling, not seed):** if/when doc comments arrive, prefer the
  **Javadoc model** (free text + `@tags`) over C#'s XML-doc (`/// <summary>…`),
  which is verbose and noisy. An inclination, not a decision.
- **Agent rule:** lexically, only line (`//`) and nesting block (`/* */`) comments
  exist; extra opening chars are insignificant. Do not implement doc-comment
  parsing in the seed.
- **Comments are discarded BEFORE the termination decision (the clean rule).** The
  lexer recognizes a comment (it *must* know what it is, to skip it) but does **not
  emit it to the parser** and it never reaches the AST — pure trivia (no
  reflection, no source reprocessing → no reason to survive). The single rule:
  **encounter the comment (start to end), discard it, treat what remains.** The
  termination/continuation decision (B.17) is then made on the *remaining* tokens,
  as if the comment never existed. This is what *preserves* the pending-operator
  rule: in
  ```teko
  let x = foo +    // trailing comment
          bar
  ```
  if the comment stayed, the "last token before the newline" would be the comment
  (which doesn't pend) → the statement would wrongly end. Discarding it first makes
  `+` the last significant token again → it pends → continuation. Same for lists
  and expressions — comments between elements are transparent; the comma/closer
  reassumes its role. One rule, all cases. The only difference between `//` and
  `/* */` is *where each comment ends* (and thus what "remains"): `//` ends *at* the
  newline (not including it → the newline remains, and the termination decision
  happens there, looking at the token before the comment); `/* */` ends at `*/` (a
  newline *inside* a block is part of the comment, discarded with it; what remains
  is whatever follows `*/`). Implementation: treat comments like non-newline
  whitespace in a `skip_trivia` that skips spaces and comments but **preserves
  significant newlines** (B.17).

### B.19 — Keywords (lexer round C)
- **Distinction method:** the lexer reads a word (alphanumeric run) then looks it up
  in a keyword table — in the table → emit the specific keyword token (`Let`); else
  → `Ident`. Standard, cheap.
- **Primitive type names are PREDEFINED TYPES — INJECTED, lexed as `Ident`, and
  RESERVED (by the checker).** `i32`, `u8`, `bool`, `byte`, `str`, `char`, … and
  `error` are *known types* injected by the compiler (like the `teko::` stdlib);
  their representations enter at **codegen**, never as `type X = Y` source.
  - **Was:** "not reserved — lexed as `Ident`, the checker handles the *shadowing*
    (a dev could name something `i32`)." Rationale then: reserving them would bloat
    the keyword set; "native types are injected, not magic words."
  - **Is:** still lexed as `Ident` (NOT keyword tokens — the bloat concern stands and
    is honored), but the **checker reserves them**: a reserved type name used as a
    plain identifier (binding target, parameter, field) is an **error**. The capability
    to use one as an identifier returns via an explicit **escape** (C#-style `@name`),
    **deferred to evolution** (the seed compiler never names anything with a reserved
    name, so it does not use the escape — bitwise precedent). Injection (the definition)
    and reservation (the name) are **orthogonal**.
  - **Why (revalidated against all six laws):** **M.1** — *exclusion-by-construction*
    (the strongest mode) makes `let str = 5` **inexpressible**, the same technique as
    the bare `self` closing the `ref` door (B.29) and no grammar production for `__`
    (B.21); shadowing was the *weak* detect-and-resolve mode that leaves the footgun.
    **M.2** — the escape marks the unusual use *explicitly*; shadowing reinterprets
    `str` *implicitly* by scope. **M.3** — the old rationale was a **non-sequitur**
    ("injected ⇒ not reserved"): the root `teko` is itself injected **and** reserved
    (B.32), so injection never implied shadowability. **M.5** — keeping the names as
    `Ident` (checker-reserved, not new keywords) honors the no-bloat concern: the
    reservation is one checker check, the escape one lexer rule. **M.0/M.4** neutral.
    Five-of-six favor or are neutral; none favors shadowable.
- **`as` and `in` are word-operators (keywords).** `in` = membership/iteration.
  `as` covers three uses, all "treat-this-as-that / give-this-name": cast
  (`x as i32`), match binding (`Number as n`), and import alias (`use x as y`).
  Logical ops are *symbols* (`&&`/`||`/`!`), not words — no `and`/`or`/`not`.
- **Confirmed seed keyword list:**
  - Declaration/binding: `let` `mut` `const` `fn` `return` `type` `struct`
    `variant` `enum`
  - Control flow: `match` `when` `loop` `break` `continue` `defer`
  - Word-operators: `in` `as`
  - **Negated membership `!in`:** the lexer recognizes `!in` as a **compound operator** (seed,
    analogous to `!=`) — `x !in xs`. The general form `!(x in xs)` also works (the `!` negating the
    bool). Two forms, the dev's choice; no `not` keyword.
  - Visibility: `pub` `exp` (private is the *absence* of a keyword, not a word)
  - Modules: `use`
  - Literals: `true` `false` `null`
- **NOT keywords:** `flags` (post-alpha), primitive type names (predefined types),
  `self`/`super`/`crate` (abolished — absolute paths), `and`/`or`/`not` (symbols).
- **Deferred to their own rounds (each a "web," not a point decision):**
  - **The conditional web** — `if`/`else`/`unless`, `if`-as-expression, and the
    relationship to `match` and `when` (the `when` guard already does boolean
    conditioning, so `if`'s role vs `match true/false` needs untangling).
  - **The mutability web** — the *semantics* of `mut`/`let`/`const` (the words are
    listed, but the *behavior* is its own round): mutable fields? parameters?
    transitive mutation? `mut` "governs accesses" (read always allowed; write —
    assign, `++`, `+=`, mutate field — requires mutability). It impacts the
    operationality of every access, so it is not a one-liner.
- **Process note (grouping by interaction, not size):** comments seemed "small"
  (trivia) but interacted with newline termination, so they belonged in round A's
  bucket; `if` and `mut` seem like single keywords but are webs. Classify rounds by
  *what interacts*, not just by size — sieving the sand before mixing concrete.
- **Agent rule:** the lexer emits specific keyword tokens for the list above and
  `Ident` for everything else (including primitive type names). `as`/`in` are
  keywords; logical ops are symbols. Don't treat `if`/`mut` semantics as settled —
  they have dedicated rounds.

### B.20 — The conditional web (round C.5): `if` is an expression; `match`/`when`; no `unless`
- **`if` / `else if` / `else` — IS AN EXPRESSION (returns the chosen branch's
  value).** Deduced from a corner case: we cut `return` for normal returns ("the
  last expression of a block is the value; `return` is only for early exit"). So a
  function ending in `if` —
  ```teko
  fn max(a: i32, b: i32) -> i32 { if a > b { a } else { b } }
  ```
  — *must* have that `if` produce the return value (it's the last expression). If
  `if` were a statement-only, you'd be forced to write `return` in each branch,
  contradicting "normal return isn't `return`." Therefore `if` is an expression.
  - `let max = if a > b { a } else { b }` — captures the value. The **if-expression
    IS the legible ternary** — it does what `?:` did, with words and blocks instead
    of cryptic symbols.
  - Used as a statement (`if cond { do }`), the value is discarded.
  - **`else` is mandatory when the `if` captures a value** (otherwise a false branch
    yields no value — the same completeness the match's exhaustiveness enforces);
    optional as a pure statement.
- **No ternary `?:`** — abolished. What was abolished is the *cryptic syntax*, not
  the capability; the if-expression carries the capability with clarity.
- **`unless` is BANNED** (hard rule). `unless cond` is exactly `when !cond` — zero
  new capability, pure redundant sugar, and it *worsens* compound conditions
  (`unless a && b` is an ambiguous De Morgan puzzle; `when !(a && b)` is explicit).
  Negation is `!cond`. (Do not reintroduce it.)
- **No postfix `when`/`unless`** — the if-expression covers conditional value, and
  `if cond { break }` covers conditional statements, so a postfix form would be
  redundant. (The lexer's earlier `break when fim` becomes `if fim { break }` — the
  canonical lexer code in Part A must be updated to match.) One conditional word
  (`if`) for boolean conditions, statement or expression.
- **`when` is exclusively the match guard** (`Number when n > 0 => ...`). (Door left
  faintly ajar: *if* some future structure ever needs a guard, `when` is the
  natural candidate — a remote possibility, not a commitment.)
- **The closed web — three constructs, disjoint roles, zero redundancy:**
  `if` = boolean condition (statement or expression); `when` = match guard;
  `match` = pattern discrimination.
- **Agent rule:** `if`/`else if`/`else` is an expression (returns a value; `else`
  required when its value is used). No `?:`, no `unless` (use `!`), no postfix
  `when`/`unless`. `when` only guards match arms.

### B.21 — The mutability web (round C.6): `mut` only on local variables
- **Resource premise (frames the whole web):** Teko targets machines with
  *sufficient memory* — **not** resource-scarce embedded (if it were, pointers/`ref`
  would have been adopted from the start). Value-semantics/copy is the conscious
  choice: **cheap in CPU, costly in memory, acceptable on the target.** Generalized
  `ref`/pointers would be a scarce-target choice and were deliberately not taken.
  UNIX philosophy: simple-and-correct first, localized optimization later.
- **Roles (confirmed):** `const` = compile-time, immutable, `.rodata` (value known
  at compile time). `let` = runtime binding, immutable once bound. `mut` = runtime
  binding, mutable.
- **`mut` is on the VARIABLE and governs the whole value — no per-field
  granularity.** `let s` → the whole struct is immutable; `mut s` → the whole struct
  is mutable (reassign *and* mutate its contents). Structs do **not** declare `mut`
  per field. *Justification by usage audit (the deciding method):* the seed uses
  mutability almost only for **local accumulators/counters reassigned as a whole**
  (`mut p = pos; p++`, `mut tokens = empty()`), never "mutate field x while keeping
  y protected." The parser *builds new* structs (`Binary {left, right}`) rather than
  mutating fields in place. Per-field granularity (C#'s `readonly`) solves an
  **aliasing** problem (multiple owners of one object, controlling who mutates which
  field) that **value-semantics does not have** (each holder has its own copy). So
  granularity would be complexity with no problem to justify it — cut.
- **Transitive by value:** `mut s` makes `s` and everything it contains *by value*
  (fields, embedded sub-structs, recursively) mutable — it's one value. `let s`
  freezes all of it.
- **Arrays follow the same rule:** `mut v` → reassign `v` and mutate items
  (`v[i] = x`); `let v` → immutable. An item has no independent mutability; the
  whole array is mutable-or-not via the variable.
- **`mut` applies ONLY to local variables — the create-vs-receive distinction (the
  key):** the rule, in its simplest form, is *`mut` only on a local variable.*
  Everything else is immutable, unified by one reason:
  - A **local variable CREATES/OWNS its value** (`mut p = 0`) → may be `mut`; the
    arena is clearly its own, mutating it is unambiguous. These are the seed's real
    uses.
  - **Parameters and match-bindings RECEIVE a value from another source** (a
    parameter ← the caller; a match binding ← the matched subject) → **immutable**.
    `mut` there would cause a reading confusion ("am I mutating the original or my
    copy?") and, underneath, an **arena/ownership** question (whose memory am I
    mutating?) the seed will not answer. (This was caught mid-decision: the same
    argument that forbids `mut` parameters forbids `mut` match-bindings — cutting
    one but allowing the other would be arbitrary.)
- **Parameters are immutable, no `mut`** (arena control). A function reads its
  parameter (a copy, by value-semantics) and produces new values via return. A
  mutable parameter (`ref mut`/`mut`) is **evolution** — it brings a redesign of
  lifetimes/aliasing, a cost deliberately deferred.
- **Match-bindings are immutable, no `mut`** (same reason: arena + reading
  confusion). `match a { Number { mut valor } => … }` is not allowed in the seed.
  Evolution, with parameters, if the arena redesign is ever bought.
- **Writes require `mut`; reads are always allowed.** Writes: assignment
  (`x = 5`, `s.f = 3`, `v[i] = x`), `++`/`--`, compound assignment (`+=`). Reads
  (bind to another, pass as argument, compare) need no `mut`.
- **Observability (hooks on a mutated field) is EVOLUTION.** The seed programs
  constructively (rebuild + return), so field-mutation is rare in its style;
  observation has real per-write cost; it's an *application* capability
  (reactivity), not something the *compiler* (the seed) needs. Easier in Teko
  post-self-host.
- **Agent rule:** `mut` goes on local variables only. Parameters, match-bindings,
  and struct fields are immutable (no `mut` keyword on them) — value flows in by
  copy, new values flow out by return. `mut` governs the whole value (no per-field
  control); writes (`=`, `++`, `+=`, `v[i]=`) require it, reads never do.

### B.22 — Arithmetic, bitwise & shift operators (round D.1) + checked-op policy
- **Integer type inventory:** signed `i8 i16 i32 i64 i128 i256`, unsigned
  `u8 u16 u32 u64 u128 u256` (8 to 256 bits; nothing larger than 256).
- **Arithmetic operators (symbols, metal/ALU):** binary `+ - * / %`, unary `-`.
  Unary `+` is cut (no capability — `+x` == `x`). No arithmetic operator is a word.
- **Bitwise operators (symbols, metal):** `& | ^ ~` (AND/OR/XOR/NOT), shift
  `<< >>`. **In the seed/alpha** (per M.0 — operators are metal power; a systems
  language must offer bit manipulation even though the *compiler* itself uses none).
  This does **not** summon the `flags` type (sugar over bitwise) — that stays
  post-alpha; only the primitive operators enter.
- **No `>>>`/`<<<`:** abolished. (a) Redundant — the **signedness of the left
  operand** already determines arithmetic vs logical right-shift (`i32 >>` is
  arithmetic/sign-preserving; `u32 >>` is logical/zero-fill); Teko has real
  unsigned types, so it needs no extra operator (Java invented `>>>` only because
  it lacks unsigned). (b) Reading ambiguity — a triple arrow *looks like* "extra/
  more intense shift" (a quantity), when it would change the shift *type*; the
  appearance misleads. Multiple shifts, if needed, are `a >> 2 >> 2` (explicit,
  left-to-right).
- **Four promotion regimes (the checker needs all four — they differ):**
  1. **Arithmetic (`+ - * / %`):** promote to the **larger** type; if one is
     non-integer (float), promote to **float**. Preserves the mathematical value.
  2. **Bitwise (`& | ^ ~`):** **no promotion** — both operands must be integers of
     the **same width and same signedness**; any difference is an **error** (the
     dev converts explicitly, *choosing* the extension). **Float in bitwise is
     always an error** (IEEE bits don't align bitwise). A **literal** operand
     adapts to the other's type **only if it fits** — `u8 & 1` ok, `u8 & 10036`
     error (10036 exceeds u8). The literal does not promote the type; it fits or
     errors. (This fit-or-error rule applies to literals generally: `u8 + 300` is
     also an error.)
  3. **Shift (`<< >>`):** **asymmetric** — the result takes the type of the **left
     operand** (the value shifted); `i32 << anything` → `i32`. The `>>` semantics
     (arithmetic/logical) come from the **left** operand's signedness. There is
     **no promote-to-larger** — `i8 << i64` is `i8`, not `i64`.
  4. **Comparison (`< > <= >= == !=`):** mixed signed↔unsigned comparison is made safe by a
     **sign-check-first strategy** (codegen, seed — *not* promotion). The compiler, comparing a
     signed `S` with an unsigned `U`, emits: **(1)** if `S < 0`, then `S` is less than any `U` (which
     is ≥ 0) — result immediate, **no promotion**; **(2)** if `S ≥ 0`, both are non-negative and are
     compared at their **original width**, no reinterpretation. This **eliminates the C
     signed/unsigned trap** (in C, `-1 >= 0u32` is `true` because `-1` becomes huge unsigned; here the
     sign check catches it) **without promoting** — so it moves no extra bytes and **has no ceiling**:
     even `u256 vs i256` works (test the sign, then compare 256 bits). **Why a strategy, not
     promotion:** promotion would move more bytes (compare at the wider type) and hit a ceiling at
     `u256` (no `i512`); the sign-check is cheaper *and* total. **Why codegen, not a library
     function:** it's how the operator is *compiled* (the operator already exists; the sign-check is
     its lowering), so it needs no generics and lives in the seed. The operator returns **`bool`**
     (for conditions). For three-way ordering in one evaluation (sort, chained comparators), a
     **`compare(a, b) -> Ordering`** function returns the **`Ordering` enum** (see B.31). M.1 (the
     trap is removed) + M.0 (the sign-check is the direct metal sequence) compose here.
- **Shift count mechanics (settled carefully — this is metal, define it, no UB):**
  - The rule the programmer sees: the count `n` in `x << n` may be **any integer
    type** (signed or unsigned — `i32 << i64` compiles); what matters is the
    **value** of `n` vs the **width** of the shifted type.
  - **Invalid count → saturates to ZERO** (not panic). Invalid = `n >= width`
    (shifting all bits out yields zero — this is mathematically correct, *not* the
    "same value"; the "same value" intuition is the C/x86 masking **bug** we reject)
    **or** `n < 0` (negative count). Both saturate to zero, uniformly.
  - **Compile-time detectable (literal count):** an invalid literal count is a
    **compile error** (`x << 32` on i32, `x << -3`) — if the compiler sees the
    failure, it's an error. (Same principle will apply to literal divide-by-zero.)
  - **Runtime (variable count):** saturates to zero, with a **warning emitted only
    in debug/test builds** (see policy below); release saturates silently.
  - Implementation note: since the largest type is 256-bit, the largest *useful*
    shift is 255, and any count ≥ width saturates anyway, so the compiler **may
    represent the count internally as u8** — a transparent codegen optimization, not
    a language rule (the programmer still passes any integer).
- **Checked-operation policy (debug vs release) — GENERAL, beyond shift:**
  verifications that catch bugs but cost at runtime (a check + branch, possibly
  I/O) are **on in debug/test builds** (cost is irrelevant there, and catching the
  bug while you *are* testing is the point) and **off in release** (M.0 and
  performance rule; the behavior is *defined and documented* as the diagnostic
  trail). The **build profile decides** — debug may inflate freely; release is
  metal-pure. Rationale for not warning in release: a per-shift check + warning
  would make `<<`/`>>` non-metal in a hot loop (crypto, compression, parsing),
  *and* "emit a warning" presumes I/O (stderr) that **bare-metal often lacks** (a
  kernel/driver/embedded target has no console). This mirrors Rust's overflow
  handling (panic in debug, wrap in release). Shift saturation is the first case of
  this policy; it will govern overflow, bounds checks, and any costly verification.
  *(Reuses the existing build profiles — `.tkt` tests already run a test profile.)*
- **Agent rule:** emit bitwise/shift only between integers; bitwise needs equal
  width+signedness (else explicit cast), shift takes the left operand's type and
  signedness. Invalid shift counts saturate to zero (literal → compile error;
  runtime → zero, debug-only warning). Costly safety checks are debug-only — never
  add per-operation checks or I/O to release codegen.
- **Operator vs library — the "quasi-metal" line (a consequence of M.0):** an
  operation is an **operator** only if it maps to a silicon **instruction-
  capability** (one instruction, or nearly — arithmetic, bitwise, shift,
  comparison). If it is **quasi-metal** (a composition of a few instructions, or a
  function over the primitives), it is **not an operator** — it goes to a math
  library. This filters out, automatically and without case-by-case debate:
  **absolute value** (`|x|` — 2-3 instructions for ints; a sign-bit mask for
  floats), **mathematical modulo** (the always-non-negative remainder, distinct
  from `%` which is the sign-of-dividend remainder), **sqrt**, **pow**, **trig**,
  **log**, and **constants** (π, e). All live in `teko::math`, **alpha milestone**
  (the language needs them to be powerful/presentable, but the bootstrap compiler
  uses none). Also: there's no clean ASCII symbol free for abs/modulo anyway (`|x|`
  collides with `|` OR/union), reinforcing that they belong as named functions, not
  operators. (`%` stays the metal remainder operator; mathematical modulo is a
  library function.)
- **Conversions (`to`) — a DEFINED conversion is allowed; loss is *caught*, never silent
  (two-pronged by knowability). ↺ REDEFINED — supersedes the early "forbid every lossy
  conversion at compile time" rule (was→is→why recorded below):**
  - **Permitted (the conversion is DEFINED):** *any* numeric→numeric conversion. The
    value-preserving ones need no guard — int **smaller→larger** (`i8 to i32`, `MOVSX`/
    `MOVZX`), float **smaller→larger** (`f32 to f64`), int→float exact (`i32 to f64`),
    float→int truncate-toward-zero (`3.7 to i32` — `CVTTSD2SI`/`FCVTZS`). The **lossy ones
    are also allowed** (int **larger→smaller** `i32 to i8`, signed↔unsigned `i32 to u32`,
    `i64 to f32`, `f64 to f32`, float-magnitude→int): the value *might fit*, and the loss is
    **caught, not silent**.
  - **Where the loss is caught — two prongs by what the compiler can SEE (M.1):**
    - **Constant provably out of range → COMPILE ERROR** (`300 to i8`, `1e20 to i32`) — fail
      earliest, where the compiler already knows.
    - **Runtime value the compiler cannot see → the metal attempts the conversion; if the value
      does NOT fit the target, it is an IMPOSSIBLE conversion → PANIC (debug AND release).** The
      validation of *whether a conversion is possible* lives at runtime (constants excepted — see
      above); impossibility fails LOUDLY, never a silent truncation. (↺ refined — see below: this
      is parity with **÷0/OOB** (panic-always), NOT the overflow *wrap*-release — a lost conversion
      is an explicit request to represent an unrepresentable value, i.e. a bug, so it panics; the
      release-truncation reading is retired.)
  - **Undefined conversions → compile error:** `bool`↔numeric (no defined mapping in the seed)
    and any cross to/from a non-numeric type — a separate *undefined* axis, not a *loss* axis.
  - **Why this, not the old forbid:** M.1 forbids the **SILENT** loss — "`to` forbidden where the
    metal would truncate *without telling*." A runtime check/panic **tells**, so the loss is no
    longer silent and there is nothing left for M.1 to forbid; M.1 is **satisfied by the guard,
    not by prohibition**. The conversion is a metal instruction, so **M.0** keeps it (don't cut a
    metal capability a guard makes safe). This is the *same* allow-and-catch shape Teko already
    uses for ÷0, overflow, array-OOB, and float-magnitude→int — **homeostasis** (Constitution §II):
    the conversion must not be the one incoherent outlier that forbids what every sibling hazard
    guards. Where a *value* form of the failure is wanted, a `teko::math` checked function
    (`-> T | error`) is the explicit path; the operator stays ceremony-free.
  - **↺ was→is→why (M.3 — the reversal recorded):** *WAS* — lossy conversions (`i32 as i8`, `i64
    as f32`, `f64 as f32`) were a flat **compile error** ("silent loss is a design-fault bug"),
    justified as "M.1 modulating M.0, the same logic as opaque pointers." *IS* — they are
    **allowed**, gated by a constant-out-of-range compile error and a runtime guard that **PANICS
    on impossibility** (the value does not fit). *WHY* — submitted to the Laws via a five-judge
    tribunal: the **opaque-pointer analogy fails** (conversion-loss is *reducible* by a guard,
    unlike a raw deref's irreducible UB); M.1's operative word is **silently**, which the guard
    cures; and homeostasis (§II) requires parity with the ÷0/OOB family. The judge tasked to defend
    the old rule **conceded** to the new one. (Cast spelled **`to`**, renamed from `as` in E7.)
    - **↺ runtime semantics refined (legislator, 2026-06):** the earlier record said the runtime
      guard was *panic-debug / defined-release* (truncation, in lockstep with the **overflow**
      wrap-release policy). The legislator clarified that an **impossible conversion must PANIC**
      (debug AND release) — conversions sit with **÷0/OOB** (panic-always), not with arithmetic
      overflow (wrap-release). *WAS* — release truncates (defined). *IS* — release panics. *WHY* —
      **M.1**: a conversion whose value cannot be represented is an unrepresentable-value *request*
      (a bug), so it must fail loudly, not silently truncate; M.0 still keeps the *attempt* (the
      conversion is allowed, not compile-forbidden). The §II parity is therefore with ÷0/OOB, not
      with the overflow wrap. (Arithmetic `+ - *` overflow is unchanged: panic-debug / wrap-release.)
- **Annotated literal (`let x: T = <literal>`) — stored AS-IS; the BINDING adopts the annotation
  (C6, "Side D").** A numeric literal keeps its native type in the typed leaf (`TNumber` stays `i64`);
  the declared type lives on the binding (`TBinding.bound`), and `value_fits(value, bound)` re-proves
  admissibility — a constant out of range is a **compile error** (fail early), a non-literal of a
  different type needs an explicit `to` (no implicit conversion). **Chosen over** (A) mutating the leaf
  to read `T` — which would weaken the *env-less independent re-derivation* of a literal node (the
  counter-validation's forge-defense, decision #7) — and over (B) synthesizing an implicit conversion
  node (M.2). The leaf stays honestly `i64`; the binding is where the annotation is visible, so the
  adoption is re-provable there. **Governing Law: M.1** (independent leaf re-derivation kept; out-of-range
  caught, never silent) **+ M.2** (the annotation is the explicit request; no hidden conversion) **+ M.5**
  (reuses `value_fits`; no new node, no widened inference). *Decided by tribunal + the legislator's
  approval ("grava as-is, o revalidate pega o erro").* (Runtime, non-literal magnitude overflow on a
  write is the cast's rule B — panic; here the literal is a compile-time constant, so it fails early.)
- **Return-type consistency (`return e` / final-expr vs the declared return) — the checkable subset now;
  whole-body divergence DEFERRED (C5).** `type_function` re-walks the typed body: every `return e`
  reachable as a statement (incl. inside `loop` bodies and `if`-blocks) must be **assignable** to the
  declared return — `assignable_to` = equal, or a **member of a variant** return (B.14, automatic union
  inclusion, so `return <u64>` in a `-> u64 | error` fn is fine); and the body's **trailing expression**
  (when the last statement IS an expression) must match too. **Named gap (M.4, §VI — not silence):** the
  FULL "every path yields a value" guarantee (definite-return / divergence analysis — a breakless `loop`
  diverges, an all-arms-returning `if`/`match` needs no trailing value, plus returns inside `match` arms
  which the typed AST does not yet represent) is a **separate later item**. C5 deliberately does NOT apply
  the trailing-value check when the body ends in a `loop`/`if`/`match` (no claim), so the seed's value-fns
  ending in a breakless `loop` (e.g. `skip_block_comment`, `read_doc_comment`) are not false-rejected.
  **Governing Law: M.3** (the signature must not lie about what it returns) **+ M.4** (the divergence
  layer is absent — don't build the every-path check on it yet) **+ B.14** (variant member inclusion).
  C5 lives in the typed pass; the superseded `check_*` layer is not dual-walked.
- **The legacy `check_*` checker layer RETIRED — one typed checker, not two (the C1 debt closed).**
  *WAS* — two parallel layers coexisted: the original `check_*` pass (Etapas 4/5a/5b/5c: `check_expr`,
  `check_statement`, `check_if`, `check_match`, `check_function`, `check_program`, …) returning bare
  `Type`/`Env`, and the typed `type_*` pass (Etapa 6-1) returning the typed tree. C1 ("concluir
  `check_*` → `type_*`") had *built* the typed layer but never *removed* the old one. *IS* — the 16
  duplicated `check_*` functions (+ the 4 expr sub-helpers) are deleted in Teko and the C mirror; the
  single live checker is `type_program`. **Kept as genuinely shared** (used by the typed pass, NOT
  duplicates): the type predicates `is_bool`/`is_integer`/`is_comparable` + op-regimes `op_is_shift`/
  `op_is_arith_bitwise`; the pattern + exhaustiveness helpers `check_pattern`/`exhaustive` (C mirror
  promoted to non-`static` `tk_check_pattern`/`tk_exhaustive`, fixing a pre-existing linkage mismatch);
  the `tk_check_result` typedef (relocated to its sole consumer's reach — revalidate); and `tk_env_result`
  (relocated from the deleted `stmt.h` into `scope.h`, which owns `tk_env`). One rewire: `check_pattern`'s
  literal arm now types via `type_expr(…).type` instead of the deleted `check_expr`. *WHY* — a
  **subsumption gate** (adversarial agent) proved every `type_X` re-derives every check `check_X` did
  (3 are strict supersets: C5 return-checking, FieldAccess/Cast arms), so deletion lost **zero** safety.
  **Governing Law: M.5** (two layers doing one job is the waste austerity forbids) **+ M.3** (drifting
  duplicates lie — the legacy `check_expr` had already gone non-exhaustive over the post-C2/C3 `Expr`
  enum, missing `Cast`/`FieldAccess`) **+ M.1** (that drift was a latent exhaustiveness hole, now gone)
  **+ M.4** (verify the twins subsume *before* demolishing — the gate ran first). This was settled
  doctrine, not a tension: C1 had already mandated the transition. *(The cross-cutting F4 "non-exhaustive
  legacy `check_expr`" finding is resolved by this same retirement.)*
- **Pattern checking + variant-axis exhaustiveness completed (C7).** *WAS* — `check_pattern` handled only
  Wildcard/Bind/Literal (Field/Range/Alt were `=> env` stubs); exhaustiveness ignored `when`-guards and
  did not expand `Alt`. *IS* — **FieldPattern** `Type { f; g }` resolves the struct and binds each field
  immutably (B.21) via `field_type`; **RangePattern** `lo ..= hi` requires both bounds to match an integer
  subject; **AltPattern** `a | b` checks each option and **forbids any binding option** (the ratified
  axis rule). Exhaustiveness now (1) counts only **unguarded** arms (a guarded `_`/case never closes the
  match — SOURCE A.14's `when`-exclusion) and (2) **expands** an `Alt` of bare cases so `RED | GREEN`
  covers both members (B.14). These shared helpers serve the typed `type_match`; the C mirror promoted
  `field_type` to non-`static` for `match.c`. *WHY* — the one design tension (could `Alt` carry variant
  cases?) went to a **tribunal**; the legislator chose **B** (ratify), recorded as the **A.14 Alt-axis
  redefinition** (see Redefinitions Index). The rest (Field/Range/`when`-exclusion) was settled by
  **B.15 + B.21 + B.14** — no tension. Drafted by an agent, integrated + verified here (26 `#test`).
- **`.tkb` codec round-trip for `Cast` + `FieldAccess` completed (Fase S — S1a/S1b).** *WAS* — `write_texpr`
  emitted bare reserved tags 10 (`TCast`) / 11 (`TFieldAccess`) that `read_texpr` rejected — written, not
  readable. *IS* — full round-trip: tag 10 writes/reads just the inner `expr` (the cast's TARGET type rides
  the node's `te.type`, already serialized by the leading `write_type`); tag 11 writes/reads the `receiver`
  then the **interned** field-name (the fix: `collect_strings` now recurses into both nodes and interns the
  field — without it `st_find` returns the not-found sentinel and the writer corrupts the index). C mirror
  byte-for-byte; new `tkb_test.tkt` round-trips both nodes and asserts a tampered body fails the FNV-1a hash
  (M.1 — corruption is visible, never silent). **S2** ("serialize only a checked program") holds **by
  construction**: `serialize` consumes a `TExpr` — the typed AST only the checker produces. *WHY* — **M.1**
  (exact round-trip; tamper caught) **+ M.3** (the checker-proved type is carried, not re-derived). No
  tension. **Still deferred (named):** the `if`/`match` codec (tags 8/9) needs a typed-**statement**
  serializer (`[]TStatement`) that does not exist yet — a separate later item; read keeps rejecting 8/9
  visibly. `MethodCall` has no typed node (typing deferred) → nothing to serialize.
- **`byte ↔ integer` casts enabled + provisional cast syntax retired (Fase X).** *WAS* — `cast_check`
  accepted only `Prim ↔ Prim`; `byte` (a distinct `Byte{}` variant) had no cast, so the codec spelled its
  byte↔int conversions as `u32(x)` / `u64(data[i])` / `byte(x & 0xFF)` — a **function-call-style cast that
  is not a function (M.3 — it lies)**. *IS* — (X0) `cast_check`/`const_range_check` (Teko + C) route through
  a shared `cast_kind` that maps `byte → u8` (B.36 "byte = u8 newtype"), so `byte to T` / `n to byte` is a
  defined conversion (byte casts AS u8; bool & non-numeric still rejected; a constant out of 0..255 → compile
  error); then (X1b) all 34 provisional `uNN(…)`/`iNN(…)`/`byte(…)` casts in the codec's `.tks` blocks were
  swept to the real `x to T`. *WHY* — **M.3** (the legislator's call: `u32(…)` "appears to be something it
  is not — a function"; only `x to T` is an honest cast) **+ M.0** (a byte is 8 bits; byte↔number is the
  natural metal conversion) **+ M.2** (explicit `to`, no implicit conflation — B.36's distinctness is about
  identity, not convertibility) **+ M.1** (byte→wider lossless; int→byte range-guarded). The codec's i64↔u64
  sites are exact value conversions (a serialized `TNumber.value` is non-negative — negatives are `TUnary`).
  Not a tension. **Still deferred:** enum-ordinal casts (`prim_byte`/`kind_byte`/`kind_of` — real functions,
  E7), the general Named-newtype↔base cast (`type Meters = i32`), and `str ↔ bytes` (codepage layer).
- **`.tkh` header-building driver (E-emit-a) — a checked program → its exported interface.** *WAS* —
  `emit_tkh(Header)` could emit a `.tkh`, but nothing built the `Header`: the checked `TProgram` was never
  turned into the exported surface. Also `TFunction` had dropped `has_doc`/`doc`, yet `.tkh` `FnSig` preserves
  them. *IS* — `TFunction` carries `has_doc`/`doc` again (set by `type_function`; isolated — the `.tkb` codec
  serializes only `TExpr`, so nothing else is touched); and `build_header(prog, table)` walks the program,
  keeps only `is_exp` items, resolves each syntactic annotation (`resolve_type`) into the `FnSig`/`TyExport`
  signatures, and `emit_program` = `build_header` → `emit_tkh`. **A header is ALWAYS emitted — a program with
  NO exports yields an EMPTY header (0 types, 0 fns) that honestly states "nothing is exported", never nothing
  at all** (the legislator's rule: a consumer must always find a readable interface). *WHY* — **M.3** (the
  exported surface, docs included, is the honest interface; emit *something* even when empty) **+ M.4** (built
  only after C + S; the full read→lex→parse→check→codegen pipeline stays deferred — codegen does not exist).
  Lives in `src/emit/header.{tks,h,c}` (+ `header_test.tkt`) — `src/` is now canonical for code; the markdown
  specs are frozen snapshots. *Deferred:* **E-emit-b** (cross-project `.tkh` consumption round-trip).
- **The `.tkb` is a Teko PACKAGE payload, statically pre-linked — NOT a native `.o` (analogy corrected).**
  *WAS* — a code comment (and an offhand framing) read "the `.tkh` is to the `.tkb` what a `.h` is to a `.o`",
  implying the `.tkb` is a native object file fed to a native linker. *IS* — the `.tkb` is binary but is Teko's
  serialized **typed tree** (IL), not a native `.o`. A `library` `.tkp` emits a **package** = `.tkh` (interface)
  + `.tkb` (payload); when compiling against dependencies the compiler **loads the packages into memory and
  STATICALLY PRE-LINKS** their typed trees into the dev's program (static linking of *Teko* objects at the
  typed-tree level), then codegen runs on the whole. This is the deliberate alternative to **dynamic linking /
  FFI** (FFI is only for foreign code). *WHY* (legislator) — **M.3**: the artifact's nature must be named
  honestly (a typed-tree package, not a native object); **M.1**: static whole-program merge means deps are
  checked together, no dynamic-boundary surprises. The off-plumb code comment in `src/emit/tkh.tks` was fixed;
  the package/pre-linker model is recorded in LEGISLATION. (The pre-linker + loader are future — pipeline phase.)
- **First-binary backend = TRANSPILE-TO-C (legislator's choice).** *WAS* — the materialization stages
  (LEGISLATION) named stage 1 = `.tkb` *interpreted* (the bootstrap step) and stage 2 = AOT-native (what ships),
  implying the first runnable would be an interpreter over the typed tree / `.tkb`. *IS* — for the FIRST
  executable, the legislator chose to **skip the stage-1 interpreter** and go straight to **transpiling the
  typed tree to C**, letting the host `cc` produce a native binary. *WHY* — **M.5** (reuse the host toolchain;
  do not write a native codegen — or even an interpreter VM — when lowering to C reaches a real binary fastest)
  **+ M.0** (the metal mapping is direct: Teko ints→stdint, the operators→C operators) **+ M.4** (it still rests
  on the completed checker/typed-AST). Transpile-to-C is thus a *realization of stage 2* (AOT-native via C), not
  a new stage. **Teko targets BOTH execution modes:** (1) transpile-to-C / AOT (this, first) and (2) the
  `.tkb` VM/interpreter (stage 1) — the VM is a **planned future mode**, not dropped; it just does not gate the
  first binary (its real prerequisite is the statement/program-level `.tkb` codec, today expression-only). The full path is defined in
  TEKO_ROADMAP_BINARY.md (F0 compile the C mirror → F1 wire the pipeline → F2 emit C + call cc → F3 minimal
  runtime; M0 = a `main.tks` of integer arithmetic + print runs as a native binary).
- **Arithmetic overflow (`+ - *`) — metal operates, debug catches:**
  - **Operator:** **panic in debug** (catches accidental overflow while testing),
    **wrap in release** (the metal's native modular result — a *defined* value, not
    poison like ∞/NaN, so release wraps without the division's problem). Follows the
    B.22 debug/release policy and the *shift* pattern (debug inflates, release is
    metal); diverges from *division* (which checks in release) because wrap is a
    defined value, not poison.
  - **Literal overflow** (`255u8 + 1`, constant): **compile error** (the compiler
    sees it; consistent with shift/÷0 literal errors).
  - **`teko::math::wrapping_add/sub/mul`:** wrap in **all** builds (intentional wrap
    — modular arithmetic, hashes, circular counters — must not trip the debug panic).
  - **`teko::math::add/sub/mul` (checked):** return `T | error` (recoverable
    detection, when overflow must be a value). Mirrors `math::div`.
  - Three levels: operator (panic-debug/wrap-release, default), `wrapping_*`
    (wrap-always, modular intent), checked (→ error, tratável).
- **Compound assignment — complete and uniform:** every binary-accumulable operator has a
  compound form — `+= -= *= /= %=` (arithmetic), `&= |= ^=` (bitwise), `<<= >>=` (shift), and
  `&&= ||=` (logical). `x OP= y` ≡ `x = x OP y`; requires `mut` (it's a write). **Legitimized by
  M.0, not an exception to M.5 (austerity):** the compound bitwise/shift forms are **metal
  operators** (they map to `AND`/`OR`/`XOR`/`SHL`/`SHR` + store), so **M.0 — the lower-numbered
  principle — wins the boundary and declares them legitimate and desirable** (Teko is generous with
  operators = the interface to silicon). They are *outside* M.5's jurisdiction (austerity governs
  conveniences/abstractions, not metal operators). A uniform rule (every binary operator has its
  compound) is also simpler to learn than a partial list with exceptions, and the compound hides
  nothing (`x &= y` is as explicit as `x = x & y`). Lexer: maximal munch makes `<<=` one token;
  parser desugars all uniformly.
- **Range tokens `..` and `..=` — token & match-pattern are seed, value is evolution:** the lexer
  produces `..` (exclusive — excludes the end) and `..=` (inclusive — includes the end) as tokens
  (maximal munch: `..=` is one token, not `.` `.` `=`). **Range as a match pattern** (`'0'..='9' =>
  …`, `'a'..='z' => …`) is **seed** — essential to write Teko's own lexer (matching character
  ranges). **Range as an iterable value** (`let r = 1..100`, iterating over it) is **evolution**
  (with `loop … from`). In the seed, a range is a *pattern* (in `match`), not a manipulable value.

### B.23 — Maximal munch (round D.0) & operator precedence (round D.2)
> ⚠️ **Re-grounded under the Constitution (answers the audience's "M.2 or M.3?").** **Maximal munch** is
> **M.2** (deterministic, predictable) **+ M.5** (refuses context-heuristic complexity), resting on
> **M.4** as foundation ("the lexer classifies, it does not interpret" — a context heuristic would be
> the lexer invading the parser's role). **Precedence** is **primarily M.3 (honesty)** — not M.2: C's
> `a & b == c` bug is not that intent is *hidden* (M.2) but that it is *inverted* (you read one grouping,
> the compiler does another — the expression **lies** about what it groups); honest precedence makes the
> syntactic grouping match what the expression *appears* to mean. **M.2** is a facet (honest precedence
> is also predictable), and **M.0** is present (the chosen levels — AND≈`*`, OR/XOR≈`+`, shift below `*`
> — reflect the metal-arithmetic analogy; Julia keeps bitwise as metal operators, not functions). Same
> family as `+`-doesn't-concatenate (B.27): an operator/grouping that must not lie about what it does.
- **Maximal munch (a GENERAL lexer rule, not just operators):** at any point where
  several tokens could begin, the lexer takes the **longest match that fits** —
  always, **with no context heuristic**. It sees `<<`, not `<` `<`; `..=`, not
  `..` `=`; `==`, not `=` `=`; `//`, not `/` `/` (so `//` is always a comment — the
  dev separates with a space if they meant divide-then-something). The alternative
  to "longest always" is **context heuristic** (the lexer looking around to
  decide), which is exactly the complexity a clean lexicon refuses. One
  deterministic, context-free rule; the lexer classifies, it does not interpret.
- **Operator precedence — follows natural mathematics, with bitwise/shift OVERRIDING
  decimal arithmetic.** The base rule: where school mathematics has an order
  (`()`, then `* /`, then `+ -`, left-to-right), Teko obeys it — universal and
  already known; deviating breeds bugs (C's famous `a & b == c` error). **Teko
  follows the Julia precedence model 100%** — Julia is a serious math-and-performance
  language that keeps bitwise/shift as *operators* (not functions, unlike
  Fortran/MATLAB/R which make them `IAND`/`bitand`) and maps them to the **analogous
  arithmetic levels** (AND ≈ `*`, OR/XOR ≈ `+`, shift ≈ just below `*`), while placing
  bitwise **above comparison** — which fixes C's reconnized error (Ritchie admitted
  `&`/`|` landing below `==` was a historical mistake). The "is the bit set?" idiom
  `flags & MASK != 0` reads correctly in Teko (`(flags & MASK) != 0`) where C
  silently means `flags & (MASK != 0)`. **This is a documentation-critical divergence
  from the C family** — the language docs must state plainly: *Teko follows Julia, not
  C, for bitwise/shift precedence; here is what changes and why.*
- **The hierarchy (Julia model, strongest at top → weakest at bottom):**
  1. `()` parentheses
  2. unary: `-` (negate), `~` (bitwise NOT), `!` (logical NOT)
  3. `<< >>` shift (its own level, just **below** multiplication — Julia's choice;
     `a * b >> c` = `(a*b) >> c`)
  4. `* / %` **and** `&` — multiplicative + bitwise AND (**same level**; AND ≈ `*`)
  5. `+ -` **and** `| ^` — additive + bitwise OR/XOR (**same level**; OR/XOR ≈ `+`)
  6. `< > <= >= == !=` comparison — **chained** (`a < b < c` ≡ `(a<b) && (b<c)`,
     adopted from Julia/Python; comparison does not associate, it chains)
  7. `&&` logical AND
  8. `||` logical OR
  9. assignment (`= += -= *= /= %= &= |= ^= <<= >>= &&= ||=`)
  All **left-associative** within a level **except assignment** (right-associative:
  `a = b = c` is `a = (b = c)`) **and comparison** (chains). Consequences:
  `a + b << c` is `a + (b << c)`; `a & b == c` is `(a & b) == c` (C's bug fixed);
  `a & b + c` is `(a & b) + c` (& at `*` level, above `+`).
- **`~` (bitwise NOT) — it is bit-complement, NOT sign negation** (that's unary `-`).
  In two's-complement **signed**: `~x = -x - 1`, so `~1 = -2`, `~0 = -1`,
  `~-2 = 1`. The result can be negative — this is natural two's-complement math,
  universal (C/Rust/Java/JS/PHP all agree), **not a bug** (the "steals a 1" feel in
  PHP/JS is exactly this `-x-1`). In **unsigned**: bits invert, read as unsigned —
  `~1u8 = 254`, `~0u8 = 255`. Same bit operation; the *value* differs by the type's
  signedness (consistent with `>>` arithmetic-vs-logical — the type decides the
  reading).
- **Agent rule:** tokenize by longest-match-always (no context peeking). Apply the
  **Julia-model precedence**: shift just below `*`; `&` at `*` level, `|`/`^` at `+`
  level; bitwise **above** comparison; comparison chains; left-associative except
  assignment. `~` is `-x-1` on signed, bit-flip read-as-unsigned on unsigned.

### B.24 — Division, ∞/NaN, panic vs checked (round D, Frente 2)
- **Two divisions, distinct roles (operator vs library, per M.0):**
  - **Unchecked — the `/` operator (metal, default):** clean, no ceremony. On a
    **runtime** ÷0 it raises a **controlled panic** (÷0 is a *bug*, not an expected
    condition — panic is the right category, like an out-of-bounds index you don't
    handle). A **literal** ÷0 (`10 / 0`, `1.0 / 0.0`) is a **compile error**.
  - **Checked — `teko::math::div` (library, tratável):** returns `T | error`,
    handled with `match`. For when ÷0 is an *expected condition* to recover from,
    and especially when the error must be a **value** that flows/composes with the
    rest of the error model (not just a local fallback). `if b == 0 { … }` covers
    the simple local-fallback case; `math::div` covers the error-as-value case.
- **∞/NaN never exist as values.** Operations that would generate them (÷0, 0/0,
  float overflow) are **intercepted at the origin**: the `/` operator panics; the
  `math::div` library returns `error`. The IEEE float type still *stores* ∞/NaN at
  the hardware level, but Teko does not let them become program values — there is
  **no ∞/NaN representation, no `is_nan`/`is_infinite` model** in the seed (the
  "NaN coffin" stays shut). Deterministic systems language: a poisoned silent value
  is the worst outcome, so it's forbidden.
- **Why the check runs in RELEASE (not debug-only, unlike shift saturation):** the
  user's decisive question — *what happens in production?* Without the check, ÷0 in
  release is, per the hardware: **float → silently returns ∞/NaN and propagates**
  (a silent poison — worse than a crash, since wrong results pass unnoticed); **int
  → raw `#DE`/SIGFPE**, an *uncontrolled* crash (or undefined on bare-metal with no
  handler). Both are worse than paying a check. So the check stays in release —
  Teko intercepts *before* the hardware poisons or crashes, and panics cleanly
  instead. The cost is small and **localized to division** (already the most
  expensive float op — a compare-with-zero is proportionally negligible), unlike a
  per-every-float-op check. This is a **justified exception to M.0** (like opaque
  pointers): release isn't perfectly metal-pure on division, consciously, because
  the metal-pure alternative is poison or crash. A panic is recoverable by a
  supervisor (restart the process); poisoned memory is not.
- **Note (drafting revealed this):** writing real code (`if b == 0` vs
  `math::div` + match) showed the operator-panic + optional-library-checked split is
  the clean factoring — the operator stays ceremony-free for the common (valid)
  case, and recovery is opt-in via the library or a guarding `if`.
- **Still open (surfaced by the draft):** integer arithmetic **overflow** (`255u8 +
  1`) is not yet decided — panic-in-release? wrap? error? (Only shift and division
  are settled.) Likely follows the same family of policy; to be decided.
- **Agent rule:** `/` panics on runtime ÷0 (compile error on literal ÷0); never let
  ∞/NaN become values. For recoverable division use `teko::math::div` (→ `T |
  error`) or guard with `if`. The ÷0 check is release-active and localized to
  division — do not generalize it to a per-float-op check.

### B.25 — Access & structure operators (round D, Frente 4)
- **The five access/structure operators, each with one role (no overlap):**
  - **`.`** — instance field access (runtime; the value exists). `node.value`,
    chained `edge.origin.value`.
  - **`::`** — static path (compile-time; *before* an instance): namespaces, nested
    namespaces, and **type constants**. `graph::Node` (type in namespace),
    `teko::math::add` (fn in nested namespace), `Node::MAX` (constant of a type).
  - **`->`** — return-type arrow in a signature (`fn f(x: i32) -> i32`). Always
    present (see `void` below). Signature-only, never a match arm.
  - **`=>`** — match-arm arrow (`Circle as c => c.r * c.r * pi`). Match-only, never a
    signature.
  - **`[]`** — indexing. Highest precedence (resolves as a unit before any operator:
    `idx.nodes[0].value + 1` is `(idx.nodes[0].value) + 1`).
- **The `.` vs `::` line is clean and never mixed:** `::` is **static** (type,
  namespace, type-constant — compile-time, before any instance); `.` is **instance**
  (a value that exists — runtime). `Node::MAX` (constant) vs `n.value` (field).
  `n::value` and `Node.MAX` are both errors.
- **Array notation is `[]T` (prefix), not `[T]`** — Go model, chosen for legibility
  under nesting: `[][]Node`, `[][][]Node` (the `[]` accumulate on the left, base type
  at the end), which reads far better than `[[[Node]]]` (counting brackets).
- **Two array forms, distinct semantics, each capped at 16 levels/dimensions:**
  - **Jagged** (`[]T`, `[][]T`…): array of arrays; sub-arrays may have different
    lengths; indexed `m[i][j]` (two steps). Indirection per level.
  - **Real multidimensional** (`[,]T`, `[,,]T`…): rectangular, contiguous; indexed
    `m[i, j]` (one step). Fast/predictable, rigid shape.
  - **16-level/dimension cap** on each — generous beyond any real use (3-4 is already
    rare), protects the metal (indirection/offset) and the compiler (type tables),
    and is a *defined* bound (Teko prefers bounded over unbounded-undefined). Beyond
    that, or to mix the two forms, the dev composes with **structs** (no ambiguous
    mixed notation like `[][,]`).
- **Type constants via `const` inside the type; `static` is BANNED:**
  ```
  type Node = struct {
      value: i32,
      const MAX: i32 = 255      // type constant, accessed via Node::MAX
  }
  ```
  A `const` member is a compile-time, immutable value associated with the type. A
  mutable **`static`** (a class variable shared across instances, mutable) is
  **forbidden — seed and always**: it is disguised global mutable state, the same
  thing already banned at global scope ("global = const only"). This also stays
  banned in evolution. (The legitimate "shared value" case is a `const`; mutable
  shared state is exactly what Teko refuses.)
- **`void` — return type for "returns no value"** (clean C#/Java sense), used always:
  every fn declares its return type, and non-returning fns write `-> void`. There is
  **no** "absence of `->` means no return" — `->` is always present. `void` carries
  **none** of C/C++'s `void*` baggage (Teko's opaque pointers are separate, in
  evolution; there is no `void*`).
- **Arrays are NEVER nullable — only their elements can be.** This *eliminates* the
  array-nullable-vs-element-nullable ambiguity instead of notating it:
  - Single notation: **`[]Node?`** = an array (which always exists) of nullable
    `Node`. The `?` can only bind to the element type (there is no nullable-array to
    bind to). Zero ambiguity.
  - **The dev must initialize every array** (empty `[]` is allowed, low cost — a
    zero-length array, no element allocation). There is no declared-but-nonexistent
    array. Eliminates a whole class of null-array checks (half of all NPEs).
  - Arrays follow **value-semantics** like every value: empty `[]` is cheap; growing
    or replacing is a **copy** (no aliasing), permitted only if the array is `mut`
    **local** (the general mutability rule). No exception for arrays.
- **Three access-failure cases, each cleanly separated (because array-null is gone):**
  - **Out-of-range** → **panic** (a bug, like ÷0). Literal/detectable
    (`arr[5]` on a known length-3 array) → **compile error**; runtime → panic.
  - **Null element** (`[]Node?`) → handled with **`?.`** / **`??`** (an expected
    condition). `arr[i]` returns a `Node?`; the result's nullability is handled by
    the normal null operators.
  - **Null array** → does not exist.
- **`?[` (safe indexing) is BANNED** — unnecessary: arrays are never null (nothing to
  guard in the indexing itself) and out-of-range is panic (you don't `?`-guard a
  bug). Indexing returns the value (a `T?` if the element type is nullable), and the
  *result's* nullability is handled by `?.`/`??`. Nullability operators are exactly
  two: **`?.`** (safe field access) and **`??`** (fallback). Examples still work
  without `?[`: `arr[0] ?? 0`, `a3[0]?.value ?? 0dec`.
- **`arrays::at(arr, pos)` (library) returns `T | error`** — checked indexing, for
  when out-of-range is an *expected* condition and the error must be a value that
  flows. Mirrors `math::div`/`math::add`. The operator `arr[i]` is metal (panic on
  out-of-range); the library `at` is tratável.
- **Array creation & initialization (the `[]`/`[,]` marks, `()` carries values):**
  - **Dimensioned with default:** `let grade = [3, 4]i32` — dimensions `[3,4]` +
    element type `i32`; the compiler infers the variable type (`[,]i32`); the array is
    born filled with the element's **default** (0 for numerics). Annotating the type
    separately (`let grade: [,]i32 = [3,4]`) is redundant and **does not exist** — the
    creation expression carries shape + type.
  - **With value literals:** the marker `[]`/`[,]`/`[][]` **precedes** the values in
    `()`. The marker is **required** — without it, `((...),(...))` would collide with a
    **function call** or expression grouping; `[]`/`[,]` says "an array literal comes
    here." The values are in `()` (lists, comma-separated), because `[]` is
    size/position and `{}` is a block — `()` is the list delimiter that remains.
    - 1D: `[](1, 2, 3)` — the marker also resolves the one-element edge (`[](1)` is an
      array, not `(1)` grouping).
    - The **`[]`/`[,]` marker carries dimensionality**; the **values carry size and
      type** (type **inferred from the values** — no `i32` annotation needed when values
      are present).
  - **Dimensionality ↔ nesting must match — else compile error:** `[,]` (2D) needs
    values nested 2 levels (`((...),(...))`); `[,,]` (3D) needs 3 levels; a mismatch is
    a **compile error**.
  - **Rectangular `[,]` vs jagged `[][]` — same value syntax, different
    guarantee/representation:** `[,]` is **rectangular** — guarantees **uniform** rows,
    stored **contiguous** (fast, rigid; irregular rows = **compile error**). `[][]` is
    **jagged** — **permits irregular** rows, stored with **indirection** (an array of
    row-pointers; flexible, slower). Same values when uniform; choose the marker by
    intent (uniform-and-fast → `[,]`; may-be-irregular → `[][]`). A jagged literal with
    uniform values is legal but wastes the flexibility (use `[,]` if uniformity holds).
  - **Empty:** jagged can be empty (`[][]i32` = zero rows, `[]i32` = zero elements);
    **rectangular needs a shape** (dimensions `[3,4]i32` or values `[,]((...))`) — there
    is no shapeless empty rectangular.
- **Formatting: newline (vertical) + spaces (horizontal) are Teko's primary formatting
  tools; the `fmt` imposes them.** Inside `()`/`[]` the **newline is suppressed
  (decorative, not significant)** — the comma separates; you may break across lines
  purely for legibility. This lets value literals format as visual matrices:
  ```
  let grade = [,](
      (1,  2,  3,  4),
      (5,  6,  7,  8),
      (9, 10, 11, 12)
  )
  ```
  A jagged irregular literal stays readable the same way (each row on its own line — it
  won't be a matrix, but each row reads). The newline made the jagged literal viable, so
  it is **not** relegated to programmatic construction; both rectangular and jagged keep
  the `()` literal, formatted vertically. (Layout legível > compacto — the deterministic
  even in formatting; "give it a bath and new clothes — it won't stop being ugly, but
  it'll be an attractive ugly.")
- **Element count — `arr.len` (and array-as-monomorphized-struct representation):**
  - An **array is a monomorphized struct** the compiler generates per element type:
    `[]T` → `struct { ptr: *T; len: u64 }`, a *concrete* struct for each `T` (`[]Node`
    → an array-of-Node struct, `[]i32` → an array-of-i32 struct). The `[]` is syntax
    sugar over that struct ("under a shadow").
  - **`len` is an intrinsic `u64` field** the compiler injects into each concrete
    array-struct. Accessed via **`.`** — `arr.len`, **no parentheses** (it is a stored
    field, not a computation).
  - **No generics needed:** because each `[]T` is a *concrete* struct, `arr.len` is
    field access on a concrete struct — zero generics. Monomorphization eliminates the
    barrier (there is no "generic array" to operate on; there are concrete structs,
    each with its own `len: u64`).
  - **`u64`** is the count type: **unsigned** (size is non-negative — excludes the
    nonsensical negative size by construction) and **64-bit** (covers any array on any
    realistic platform; avoids needing a platform-dependent `usize`, keeping the
    inventory lean — `u64` is simple and sufficient).
  - **The line this draws:** intrinsic array fields (injected by monomorphization,
    concrete type) **escape generics**; functions over the *variable element type*
    (`arrays::at -> T`) **need generics**. Monomorphization gives concrete fields for
    free; generic functions need the feature. (This is why `.len` is seed-available
    while `arrays::at` is evolution.)
- **Principle of partial operations (operator-vs-library, a consequence of M.0):**
  every operation that can fail in a degenerate case (÷0, overflow, out-of-range, and
  future ones) has **two forms** — the **operator** (metal: clean, panic on the
  degenerate case at runtime, compile error if the degenerate case is
  literal/detectable) and the **library function** (checked: `T | error`, for the
  expected-degenerate case, error-as-value). The operator is the default (degenerate
  = bug → panic); the library is the opt-in (degenerate = condition → handle). Apply
  this to any future partial operation without re-deciding.
- **⚠️ Timing — operators are seed, checkers are EVOLUTION (they depend on generics):**
  the **operators** (`/`, `+`, `arr[i]`) are **seed** — the compiler resolves them per
  *concrete* type (the i32 `+` and the f64 `+` are different generated code, not one
  generic fn), and the degenerate-case panic is built in; no generics needed. The
  **checked library forms** (`math::div`, `math::add/sub/mul`, `arrays::at`) all return
  a **generic `T | error`**, which **requires generics** — and generics are deferred
  (see B.11). So the checkers are **evolution, not seed**. On the seed, an *expected*
  degenerate case is handled with an **`if` guard** (`if b == 0 { … } else { a / b }`;
  `if pos < len { arr[pos] } else { … }`); the checkers (composable error-as-value)
  arrive with generics in evolution. The seed is complete without them: operator
  (panic, for bugs) + `if` (for the local tratável case).
- **Seed convention — check with `if`-before-the-operation ("look before you leap"):**
  pre-conditions of partial operations are verified with **`if` before the operation**
  — check the condition, execute if valid, handle the invalid case in `else`. This is
  **not a stopgap**: (a) it is the "exclude the invalid by construction" principle
  (`if b != 0` makes `a / b` *unable* to fail inside the block) — the same as
  variant/mut/nominal; (b) it is metal-pure (one compare + branch — no allocation, no
  overhead, lighter than `T | error`); (c) it is the consecrated systems idiom (quality
  C lives on `if (ptr != NULL)`, `if (i < size)`). The checkers (`T | error`) are an
  *evolution convenience layer* for composable error-as-value; `if`-before is the
  *fundamental* and correct form. Writing the compiler itself in seed Teko will use
  `if`-before everywhere — which is good: it forces explicit, deterministic
  pre-condition checks with no hidden cost, exactly the style Teko promotes.
- **Nullability operators (`?.`, `??`) compose with access/indexing at the parser, not
  a special token:** the lexer produces `]`, `?.`, `??` as distinct tokens (maximal
  munch — `]?` is not one token); the parser composes, e.g. `arr[i]?.field` parses as
  `(arr[i])?.field` (indexing's high precedence resolves first, the null operator
  acts on the result).
- **Agent rule:** `.` = instance, `::` = static (incl. type constants via `const`);
  `static` members are banned. Array type is `[]T` (jagged) or `[,]T` (rectangular),
  ≤16 levels, else compose with structs. Arrays are never nullable (only elements:
  `[]Node?`); every array is initialized (empty `[]` ok). Out-of-range panics
  (literal → compile error); `arr[i]` returns `T?` for nullable elements, handled by
  `?.`/`??`; `?[` does not exist; `arrays::at` is the checked `T|error` form. Every fn
  has `-> Type` (or `-> void`).

### B.26 — Delimiters & the separation rule (round D, Frente 5)
- **The separation rule is STRUCTURAL — the delimiter decides the separator, not the
  content:**
  - **`()` and `[]` → comma**, mandatory between elements (lists: args, params,
    indices). `soma(a, b, c)`, `grade[1, 2]`. Newline is suppressed inside (comma
    structures, per Round A).
  - **`{}` → newline (default) or `;` (inline)**, for **everything** — statements,
    struct fields, enum members, destructuring, match bindings. **No comma ever
    appears inside `{}`.** `;` is the "manual newline" (the resurrected inline
    separator) usable in any `{}`.
  - This makes types read like code (items stacked by newline), and **eliminates the
    trailing-comma question entirely** (fields don't use commas).
- **Comma is a separator, only between 2+ elements:** absent with 0 or 1 element
  (`[]`, `[x]`, `f(a)`), mandatory between each pair with 2+ (`[1, 2, 3]`,
  `f(a, b)`). **Never after the last element** (separator, not terminator) → no
  trailing comma.
- **`variant` is the exception — it uses `|` (not newline/comma):** a variant is a
  **sum type** — a union of *exclusive* alternatives (one is realized at a time, via a
  tag), conceptually a union over the *space of types*. This is analogous-but-distinct
  from bitwise `|`: flags combine *coexisting* bits (inclusive OR); a variant selects
  *one* exclusive case (the tag points to one). The `|` carries "union of
  possibilities," different from "list of fields" (struct → newline/`;`). **Multiline
  variant:** first case on the keyword line, `|` at the **end** of each line (a pending
  operator → suppresses the newline, continues the expression — Round A); the `fmt`
  aligns the `|` vertically as canonical style.
  ```
  type Node = variant Number |
      Binary |
      Unary
  ```
- **Destructuring (in `let`) and match-binding use the SAME form — struct-form `{}`
  with names, separated by newline or `;` inline (NO comma):** by *name*, not position
  (robust — reordering fields doesn't break it), consistent with the struct
  definition. `let { x; y } = ponto`. **⚠️ This REVISES the earlier match decision**
  (B.15 had match bindings with commas — `Binary { left, right }`); now aligned to the
  delimiter rule:
  ```
  match forma {
      Circulo { raio } => raio * raio * 3.14dec
      Quadrado { lado } => lado * lado
  }
  ```
  With this revision, the **"`{}` = newline/`;`, no comma" rule is ABSOLUTE** — struct
  def, fn body, statements, destructuring, match-binding all use newline/`;`; none use
  comma. Comma is exclusive to `()`/`[]` (lists).
- **Array initialization:** empty is `[]` (nothing — no elements, no dimensions; empty
  is empty); creating with **defined dimensions** is possible (needed for rectangular
  multidimensional, which has a shape). The exact *syntax* of declaring-with-dimensions
  is a **convention still to be defined** (semantics clear: empty = nothing,
  dimensioned = possible; only the spelling is open).
- **Teko will have a canonical `fmt` (Go-style):** layout conventions (e.g. vertical
  alignment of variant `|`) are imposed by the formatter — the dev doesn't argue style,
  runs `fmt`. Ends style wars; consistent with Teko's preference for the deterministic
  even in formatting. (Tooling/evolution, but the *decision to have it* and the
  Go-style stance are fixed.)
- **Agent rule:** inside `()`/`[]` use commas (mandatory, between 2+ elements only);
  inside `{}` use newline or `;` (never comma) — for statements, struct fields, enum
  members, destructuring, and match bindings alike. `variant` uses `|` (trailing,
  pending-operator continuation when multiline). Match bindings are `{ a; b }` /
  newline, NOT `{ a, b }` (revised from B.15).

### B.27 — Strings: concatenation, interpolation, formatting, conversion (Frente 6)
- **`+` NEVER concatenates — four reasons:** (1) `+` is *sum*; a string isn't a number
  (concatenation isn't commutative — an operator lie). (2) **M.0:** concatenation is
  alloc+copy (a composite), not a hardware instruction → library, not operator. (3)
  Coherence: Teko bans implicit family mixing, so it can't overload `+`. (4)
  **Legibility-local (the strongest, and general):** an un-overloaded `+` *reveals* that
  its operands are numeric — reading `var1 + var2` at line 500 tells you the category
  (numbers) **without** scrolling to distant definitions. Overloading `+` erases that
  information, forcing the reader to reconstruct far-away context to understand one line.
  Holds in **any** language (it's about the human reader, not the compiler). This
  generalizes to a principle: **an operator should reveal the category of its operands;
  operators are not overloaded across categories.**
- **Concatenation operator is `$` (string + string only):** `$` *reuses the string
  symbol* (`$"..."` is interpolation), so it stays **inside the string category** — `a $ b`
  announces "string operation, string result," satisfying legibility-local (you know the
  category at line 500). This is unlike overloading `+` (which would *cross* categories,
  number→string); `$` doesn't cross — both its modes are string. **Maximal munch
  distinguishes** `$"` (interpolation — `$` glued to `"`) from `$` (concatenation —
  spaced/standalone); zero extra lexer cost. `$identifier` alone (no left operand) is a
  syntax error (binary operator missing its left side), not ambiguous.
  - **`$` requires string + string.** A non-string operand is an **error** — the dev
    converts explicitly (`"v: " $ preco.to_string()`) or uses interpolation. **No
    automatic conversion** in `$` (keeps mixing-implicit banned and the input-type
    guarantee). The misto case belongs to interpolation (where `{}` converts); `$` is
    for the already-string case.
  - **Precedence:** low (below comparison); `a $ b == c` is `(a $ b) == c`.
    **Left-associative:** `a $ b $ c` is `((a $ b) $ c)`.
  - Rejected alternatives: justaposition (`"a" var`) **breaks significant-newline**
    (Frente 5) — adjacency-concatenates conflicts with newline-separates. `<>` is free and strong but
    carries SQL/Pascal "≠" baggage; `++`/`..`/`&`/`||`/`~` all collide. Backtick is free but visually
    weak.
- **Interpolation `$"...{x}..."` is SUGAR over concatenation:** it *desugars* to a `$`
  chain — `$"texto {x} mais {y:F2}"` → `"texto " $ x.to_string() $ " mais " $
  y.to_string("F2")`. Text pieces become string literals; `{x}` becomes `x.to_string()`
  (with the format as argument if present); all joined by `$`. **This resolves the
  tokens** — interpolation needs no semantic machinery of its own; it reduces to tokens
  that already exist (literals, `$`, expressions, `.to_string()`).
  - The `{}` **is the explicit request to convert-to-string** — `{x}` means "represent x
    as a string," the declared purpose of `{}`; so it does **not** violate
    mixing-implicit (the conversion is what `{}` is *for*, not a hidden side effect).
  - **Lexer (modes):** on `$"` enter interpolated-string mode; **text mode** accumulates
    literal until `{` or `"`; `{` → **expression mode** (tokenize normal code, balance
    `()`/`[]` to find the closing `}`); a `:` at the right level → **format mode** (read
    the specifier as text until `}`); `}` → back to text; `"` closes. The lexer expands
    to concatenation tokens directly (the parser need not know interpolation happened).
    **No nesting** of interpolation in the seed (use an intermediate variable).
  - **Type check falls out of the desugar:** `{x}` becomes `x.to_string()`; if `x` is a
    **struct or variant**, it's the ordinary method-not-found **error** — no special
    interpolation rule. **No exception for structs:** the compiler **cannot infer** which
    free function (if any) converts the struct (the dev may have named it anything, or not
    written one — there is no automatic struct↔free-function association). The dev must
    call *their* conversion function explicitly, **outside** the interpolation, and
    interpolate the resulting string. **Variant is an error too:** converting it requires
    "exploding" the variant (a match to find the active case) and then converting that
    case (which may itself be inconvertible, e.g. a struct) — the compiler does not
    generate that implicit match. Only **primitives and enums** interpolate.
- **Format specifiers = C# model:** `{x:F2}`, `{x:D4}`, `{x:N0}` (we already took
  interpolation from C#; the format is one more). Inside `{}` of interpolation, `:`
  introduces the format specifier (context distinguishes it from type-annotation `:`).
- **Culture is Universal/invariant ALWAYS by default** (deterministic — point decimal, no
  locale-specific separators, same output on any machine). Localization is **opt-in
  explicit** in the format specifier; never inherited from the environment/OS. (More
  deterministic than C#, whose default is the thread locale.)
- **Formatting is Teko's OWN (not the OS's libc/printf):**
  - **Nothing in metal** solves it — int→string is divide-and-remainder, float→string is
    Dragon4/Ryū (notoriously hard). By M.0, formatting is library, not operator.
  - **Not the OS:** the OS `printf` is **locale-dependent** (non-deterministic — Teko
    forbids) and **impossible in bare-metal** (an OS written in Teko, purpose 2, has no
    libc beneath). So Teko implements its own — deterministic, locale-independent by
    default, self-sufficient.
  - **Milestones:** int conversion = **seed** (the compiler formats numbers for error
    messages); float = **alpha** (Ryū, hard); rich specifiers = **alpha/evolution**.
- **`to_string` (instance, `.`) and `parse` (static, `::`) are AUTO-IMBUED by the
  compiler — for primitives and enums only:**
  - **Primitives:** `to_string`/`parse` **hand-written** in the runtime (built-in, C#
    format). Finite and known.
  - **Enums:** `to_string`/`parse` **generated** by the compiler in pre-compilation (a
    generator maps members to/from string literals — `Vermelho ↔ "Vermelho"`).
  - **Primitives are NOT structs** (they keep their own category) — but `to_string` is
    injected anyway (injection doesn't require being-struct, only that the compiler
    produce the method for the type).
  - **`parse` is static (`::`)**, `to_string` is instance (`.`) — mirroring the
    `::`(static)/`.`(instance) split: `parse` *creates* from a string (`i32::parse("42")`),
    `to_string` *operates on* an existing value (`x.to_string()`). `parse(value: str?) ->
    T | error` where **T is decorative** (the owning concrete type — `i32::parse ->
    i32 | error`), **not generics** (each `parse` is monomorphized).
- **Structs get NEITHER auto-imbued — "Inês é morta" (for auto-imbuing):** a struct
  `to_string`/`parse` is not *generated by the compiler* (that would need user-declared `static`
  [banned] or generics [evolution]). **But the user can write them** — either as a **free function**
  (`fn parse_ponto(s: str?) -> Ponto | error`) or, since B.29, as a **function inside the struct**
  (`to_string(self)` instance via `.`, `parse(...)` static via `::`) — discouraged but available, no
  static, no generics, no method-in-struct machinery beyond the bare-`self` sugar. Both forms
  coexist. (See B.29 for the full method model. The interpolation of a struct is still an error
  unless the dev wrote that `to_string`.)
- **Exception — `error` is a special built-in type with `to_string`:** `error` is
  **global (no namespace**, available everywhere without importing, like the primitives —
  it appears in `T | error` on every checked operation, so importing it everywhere would
  be absurd), **known to the compiler** (built-in), with an auto-imbued **`to_string() ->
  str`**. This is an exception to "structs get no auto-imbued `to_string`," justified by
  necessity: errors exist to be *communicated* (displayed, logged, shown in a panic),
  which requires `error → string`; without it the type can't serve its purpose.
  - **No `parse`** — errors are *produced* by program logic (÷0 yields an `error`), not
    parsed from text; `error::parse` has no use case. Asymmetry: error→text (to_string,
    to display) but not text→error (no parse).
  - **No format parameter** — an error is a *message*, not a value with varied
    presentations (no `:F2` for an error). `error.to_string` is simple: produces the
    message, no argument. (Unlike primitive `to_string`, which takes a format specifier.)
  - **Structure (already defined):** `error { message: str; line: u32; file: str; trace:
    str? }` — rich (location via `line`/`file`, stack via nullable `trace`); separators
    `;`/newline per Frente 5. **`line` is `u32`:** unsigned (a line number is
    non-negative — excludes the nonsensical negative by construction) and 32-bit (covers
    any conceivable file; `u16`'s 65535 ceiling is reachable by generated/amalgamated
    files, and a wrong line from overflow would be a poisonous diagnostic — the 2 extra
    bytes are irrelevant in an error type, which is never a hot path). The `to_string()`
    uses these fields (e.g. `file:line: message`).
  - **Other built-ins may get `to_string` if needed** (latent principle — same reason:
    built-in types the compiler must display). `error` is the only case today; others
    apply if/when they arise. Not invented speculatively.
- **The distinction that makes it coherent — auto-imbued (compiler) vs declared (user):**
  the banned `static` is **user-declared** static (disguised global mutable state). The
  auto-imbued `parse`/`to_string` are **compiler-generated** type-associated methods —
  not `static` in the banned sense (no user `static` keyword, no shared mutable state).
  The line "compiler injects a method (ok) vs user declares static (banned)" is exactly
  the line "primitive/enum (auto-imbued) vs struct (would need user-declared)."
- **Recurring pattern (3rd time — `.len`, `to_string`, `parse`):** a *universal feature
  without generics* = **concrete code per type** (hand-written for the finite primitives,
  generated for enums, a free function for structs), with `T | error` as decorative
  notation. Teko replaces generics/methods/static with **free functions monomorphized per
  type** — simplicity of mechanism over concision of abstraction.
- **Agent rule:** concatenate with `$` (string+string; convert non-strings explicitly via
  `.to_string()` or a free function). Build mixed strings with interpolation `$"...{x}..."`
  (the `{}` auto-calls `to_string`, passing the `:format` if present). `+` is never
  concatenation. `to_string`/`parse` exist auto-imbued for primitives and enums only;
  for structs write a free function. Interpolation of a struct/variant is a
  method-not-found error.

### B.28 — Literals: numeric (hex/bin/octal/separator), string escapes, float form (Frente 7)
- **Numeric literals — non-decimal bases:**
  - **Hexadecimal `0x`** (`0xFF`, `0xDEADBEEF`) — systems *needs* it (masks, addresses,
    bytes); the bitwise operators (B.22) read naturally in hex (`0xFF00`).
  - **Binary `0b`** (`0b1010`, `0b1111_0000`) — bitwise (B.22) reads the bits directly.
  - **Octal `0o`** (`0o755`) — included for completeness (unix permissions). Rare but
    cheap.
  - **Digits are case-insensitive** (`0xff` == `0xFF`); the **prefix is lowercase** (`0x`,
    `0b`, `0o`).
  - **Type is inferred by value** when no suffix (`0xDEADBEEF` > i32 max → u32); a
    **suffix combines** with any base (`0xFFu8` = u8). Same inference/suffix rules as
    decimal (B.22).
- **Digit separator `_`** (`1_000_000`, `0xFF_FF`, `0b1010_1010`, `3.141_592`): improves
  legibility of large numbers (coherent with B.26's "abuse formatting for legibility").
  **Rule: only between digits** — `1_000` ok; `_1000`, `1000_`, `1__0` are errors. Valid
  in **any base** and in **both parts of a float**.
- **String escapes** (processed in seed `"..."` strings): the minimal set is
  **`\n` `\t` `\r` `\\` `\"` `\0` `\u{HEX}`**. (C-legacy `\a \b \f \v` are out — rare;
  `\'` is unneeded in `"..."`.) A **literal `{`** in an interpolated string is **`{{`** (B.5; the
  `$`-count trick was removed). *(Raw strings `@"..."`, where escapes are NOT processed, are
  **evolution** — B.5; in the seed every string processes the escape set above.)*
  - **`\u{HEX}` produces the UTF-8 bytes of the code point** — coherent with `char = u8`
    (B.12): a string is UTF-8 bytes, so `\u{e9}` becomes the 2 bytes `0xC3 0xA9` (`é`).
    `\u` is sugar for writing a code point's bytes without computing the UTF-8 by hand;
    the string stays bytes.
- **Char/byte literals (`'a'` — a `u8`, B.12):**
  - Accept the **same escapes** as strings: `'\n'`, `'\t'`, `'\r'`, `'\\'`, `'\''` (the escaped
    single quote), `'\0'`, `'\u{HEX}'`. `\u{…}` in a char is valid **only if it fits in one byte**
    (≤ `0xFF`); a multi-byte code point (`'\u{1F600}'`) is a compile error (doesn't fit a `u8`).
  - **The empty char literal `''` is VALID** — it is the **byte zero** (`0u8`), equivalent to `'\0'`
    but cleaner. This **corrects the "empty character literal" error** of most languages (C/Java/
    Rust forbid `''`): since `char = u8`, a char literal is just byte notation, and the empty one is
    the zero byte — no reason to force `'\0'`. Useful for the ubiquitous null byte in systems code.
    (Trade accepted: an accidental `''` is no longer caught as an error — it becomes zero.)
- **The `_` is disambiguated by lexical context** (the lexer decides by position): `_` **isolated**
  (followed by non-alphanumeric, outside a number) = the **match wildcard** (`_ => …`); `_`
  **between digits** (inside a numeric literal) = the **digit separator** (`1_000`); `_` at the
  **start or middle of a word** = part of an **identifier** (snake_case).
- **Identifier regex corrected to resolve the `_` collision (Rev. 14):** the classic
  `[A-Za-z_][A-Za-z0-9_]*` *accepts a bare `_`*, which collides with the reserved `_` wildcard token
  (both match the same lexeme). The corrected form is **two branches**: `[A-Za-z][A-Za-z0-9_]*`
  (starts with a letter — always valid) **OR** `_+[A-Za-z0-9][A-Za-z0-9_]*` (starts with one or more
  `_` but **requires at least one letter/digit** after). Consequences: `_foo`/`_1`/`__bar`/`foo_bar`
  = **identifier**; **`_` alone = the reserved wildcard** (doesn't match the regex); **`__`/`___`
  (only underscores) = matches neither branch → maximal munch emits several `_` tokens → the parser
  **fails naturally** (there is no grammar production for consecutive wildcards; the `_` is only
  valid as an isolated match pattern). The invalidity of `__` emerges by **exclusion-by-construction**
  (three rules that exist for other reasons — the corrected regex, the single grammatical role of
  `_`, and the absence of a "two wildcards" production — together make `__` inexpressible-as-valid),
  **not** a dedicated anti-`__` rule. The lexer stays simple (no special case); the parser validates
  combinations. Coherent with the bare-`self`/variant/nominal pattern (exclude the invalid by
  removing the way to express it, M.1/M.3).
- **Float literal form:**
  - **Scientific notation** — `1.5e10`, `2.5e-3`, `E` also (`1.5E10`); exponent sign
    optional.
  - **`.5` (no leading zero) is an ERROR** — requires `0.5`. This **protects the `.`
    access operator** (B.25): `.5` would be visually ambiguous with field/method access
    (`x.5`), and a bare leading `.` invites confusion. Demand the digit.
  - **`5.` (no trailing digit) is an ERROR** — requires `5.0` (same reason; `5.` reads as
    a cut-off "5 dot something").
  - **Separator `_`** valid in both integer and fractional parts (`1_000.5`,
    `3.141_592`).
- **Agent rule:** accept `0x`/`0b`/`0o` (lowercase prefix, case-insensitive digits, suffix
  ok); `_` only between digits, any base; string escapes are exactly `\n \t \r \\ \" \0
  \u{HEX}` with `\u` → UTF-8 bytes; floats allow `e`/`E` exponents but reject `.5` and
  `5.` (require `0.5`/`5.0`).

### B.29 — Instance methods: functions in a struct, the bare `self`, static without `static` (the elephant)
Methods were deferred several times; this resolves them. The model is **Zig-like** (functions in
the struct's namespace, method = sugar), adapted to Teko (value-semantics, no `ref`, explicit).
- **Functions inside a struct are PERMITTED but DISCOURAGED** — they should look slightly
  *strange*, on purpose. The encouraged path is a **free function** (data ≠ behavior, procedural);
  a function-in-struct is a conscious exception (for `to_string`/`parse`, or where associating with
  the type matters). The mild discomfort is a design signal: "this is unusual in Teko." **Dissuade
  without prohibiting** (like the discouraged-empty-struct, like copy-is-a-conscious-choice).
- **Instance method = a function in the struct whose FIRST argument is a bare `self`** — *untyped*,
  **name free** (the dev picks `self`/`p`/`ponto`; the marker is the **position**, not the name).
  - **The bare untyped receiver is the key that closes the `ref` door.** It is not `self: Ponto`
    nor `self: *Ponto` — just a bare name with no type. **Without the type, there is no slot to put
    a `*`** — so the receiver is *always* a **copy** of the enclosing struct's value (value-semantics,
    no mutation, no `ref`). The pointer problem (the one that "hits the arena" — Rust's `&mut self`,
    Zig's `self: *Point`) **cannot even be expressed**, because the syntax has no place for the `*`.
    Teko closes the `ref` door by removing the syntax that would open it.
  - **The bare `self` IS the explicit marker** (resolving the earlier B1/B2 tension): you *see*
    `self` as first arg and know it's an instance method — not Zig's magic inference (where any
    first-param-of-the-type is a method). Explicit, no new keyword.
  - **It is a surgical syntactic exception:** an untyped argument is allowed **only** (a) inside a
    struct and (b) in the **first** argument. Everywhere else, every argument requires a type
    (`x: i32`).
  - Called on an **instance** with **`.`**: `p.to_string()` ≡ sugar for `Ponto::to_string(p)`.
    Reads/returns-new (no mutation in the seed).
  - **Margin to grow:** when `ref` arrives (evolution), the same model accommodates instance
    mutation (a by-reference receiver) without redoing the syntax.
- **Static function of the type = a function in the struct WITHOUT a bare `self` first arg** (all
  args typed). Called on the **type** with **`::`**: `Ponto::parse(x, y)`, `Ponto::init(...)`
  (constructors). This is **"static without `static`":** a function associated with the type, **no
  global state, no `static` keyword** (the banned `static` is global *mutable state* — C# conflates
  the two under one word; Teko separates static-state (banned) from type-associated-function
  (allowed)). Presence/absence of the bare `self` distinguishes instance-method from static-function.
- **Access mirrors B.25:** instance method → `.` (`p.to_string()`); static function → `::`
  (`Ponto::parse(...)`). The receiver decides both: receiver → instance → `.`; no receiver → type → `::`.
- **Coexistence:** a function-in-struct and a free function **do not collide** (different places for
  behavior; the dev chooses — struct is discouraged/associated, free is the procedural default).
- **Structural pattern — NO overload, NO override (in the seed):** a function's identity is its
  **unique name in scope**, not its signature. Two `to_string` in the same struct **collide**
  (error), even with different signatures. Collision is resolved by **name, not signature**.
  Coherent with: nominal typing (identity by name), anti-magic (no overload resolution), and
  legibility-local (one name = one function, no ambiguity).
- **Temporal fates diverge (adiado ≠ morto):**
  - **`override` RETURNS with OOP** (evolution, post-generics) — it is *constitutive* of subtype
    polymorphism (a subclass redefining a superclass method = the dynamic-dispatch mechanism). OOP
    without override isn't OOP. Deferred with OOP, not killed.
  - **`overload` does NOT return — ever.** It is **redundant** with generics ("same op, different
    types" → one generic fn, not N) + default args ("same op, variable args" → one fn with optionals,
    not N). Overload is the pre-generics workaround for problems generics+default-args solve better
    (fewer functions, no signature resolution). Having both, overload adds only weight (the
    resolution machinery, ambiguity). Rejecting it permanently is *possible* because the
    replacements are chosen. The unique-name pattern is therefore **permanent**, not a stopgap.
- **Default args are EVOLUTION, not seed — the seed is arity-rigid** (every call passes every
  argument). The case that motivated default args (`to_string(format?)`) is *itself* evolution (rich
  format specifiers are alpha/evolution), so the circular dependency dissolves: they're born
  together. In the seed, `to_string()` is zero-arity (default conversion, no format); `parse(s:
  str?)` takes a nullable (the `?` is value-nullability, **not** a default arg). The `= null`
  sketched earlier was default-args jumped ahead — removed from the seed. **Debt accepted:** arity
  rigidity is less convenient but cheap now (the seed compiler is naturally fixed-arity) and payable
  later (default args + format specifiers arrive together).
- **This resolves the pending struct `to_string`/`parse`:** they become functions *in* the struct —
  `to_string(self)` (instance, `.`) and `parse(...)` (static, `::`) — discouraged but available, no
  generics, no `static`, no loose free function required (though the free function still coexists).
  Interpolation now finds `p.to_string()` (it exists as a method), though a struct in interpolation
  is still an error unless the dev wrote that method.
- **Agent rule:** to give a struct behavior, prefer a **free function**; a function *inside* a struct
  is allowed but discouraged (it should feel unusual). Inside a struct, a first arg of bare `self`
  (untyped, any name) makes it an instance method (receiver is a value-copy, no mutation, called with
  `.`); no bare `self` makes it a static function of the type (called with `::`). No overload, no
  override, no default args in the seed — unique name per scope, fixed arity.

### B.30 — `bigint`: a library type for arbitrary precision (NOT native — superseded reasoning)
- **Is:** `bigint` is an **arbitrary-precision signed integer** (variable size, grows as needed). It
  is a **library type** (stdlib), and **evolution** (not seed).
- **History (why it is *not* native — a reversal):** `bigint` was briefly made native to close a
  *comparison ceiling* — when comparison promoted signed↔unsigned to "one width above," `u256` had no
  `i512` to promote to. But the comparison mechanism was then changed to a **sign-check-first
  strategy** (B.22) that does **not promote** and therefore **has no ceiling** — `u256 vs i256` works
  by testing the sign, no wider type needed. That removed `bigint`'s core justification: **the
  language no longer references it.** By the M.0 rule (a variable-size, allocating type is quasi-metal
  → library), `bigint` reverts to a **library** type. It is still useful (arbitrary-precision
  arithmetic — crypto, math) but is **not essential to the core**, so it is not native and not seed.
- **Why this reversal is correct (M.3 honesty + M.5 austerity):** keeping `bigint` native would
  pretend it's part of the numeric core when nothing in the language requires it — dishonest (M.3) and
  bloating the native set with what can be a library (M.5). The sign-check strategy is the better
  decision that made `bigint`-native obsolete; following the principles, `bigint` descends to the
  library. (A correct earlier decision can be superseded by a better one — recorded, not hidden.)
- **Agent rule:** `bigint` is a future library type; do not treat it as a native/seed type. A
  `u256 vs i256` comparison does **not** need it (the sign-check handles the ceiling). `bigint` is for
  arbitrary-precision *arithmetic*, an evolution library.

### B.31 — `Ordering`: an enum (not a variant) for three-way comparison
- **Is:** `compare(a, b)` returns **`Ordering`**, an **`enum`** — `enum Ordering { Less = -1; Equal =
  0; Greater = 1 }`. Backed by `i8`. Used for sort, chained/lexicographic comparators — anywhere
  three-way order is wanted in one evaluation (the operators `< > ==` already cover boolean tests).
- **Why an `enum`, not a `variant` (the proportionality argument):** a `variant` carries machinery (a
  tag, a composite type, exhaustive `match`) **disproportionate** to three trivial states — "heavy for
  something so mediocre." A `variant` is for branches that carry *data* (`Numero { valor }` vs `Texto
  { conteudo }`) or where forgetting a case is dangerous; three states the metal already produces as a
  number need none of that. The **`enum` is the lightweight named middle ground**: it is *just an `i8`
  with labels* — **light** (zero overhead, no variant tag), **named** (`Ordering::Less` is
  self-documenting, not the magic `-1`), **metal** (an `i8` underneath; the name is compile-time), and
  **operable** (reach the underlying `i8` to compose — negate to reverse the order, combine for
  lexicographic sort). It resolves the M.3-vs-M.0 tension (names *and* metal) without the variant's
  weight.
- **Why not raw `i8` with sign-semantics:** a bare `i8` (any negative/zero/positive, like C's
  `strcmp`) is the most metal but carries a **magic-value** cost (you must know the convention) —
  against M.3 (a value should *be* what it says, not *represent* it by convention). The `enum` fixes
  the values to `-1/0/1` and names them; the cheap normalization to exact `-1/0/1` is worth the
  legibility for something read in many sorts. (This is the same anti-sentinel stance that banned
  `null`-as-absence and ∞/NaN — but solved with the *light* `enum`, not the heavy `variant`.)
- **Constitutional note:** the operator-vs-`enum` and `enum`-vs-`variant` choices were settled by the
  **hierarchy** (M.0 ≺ M.3 ≺ M.5, lower wins the tie). The comparison is a metal operation, so M.0
  governs the representation (metal `i8`), while the `enum` (not raw `i8`) honors M.3's naming at
  near-zero cost — both satisfied without the `variant`'s M.5 violation.
- **Agent rule:** for a boolean test use the operators (`a < b`); for three-way order use
  `compare(a, b) -> Ordering` and read `Ordering::Less/Equal/Greater`. Do not model three-way order
  as a `variant`; the `enum` is the right weight.

### B.11 — Generics deferred (constraints are the cost)
- **Is:** generics are **evolution**, added when the language is mature.
- **Why:** generics themselves are tractable; the **constraint system** is the
  nightmare (C++/Haskell pay in illegibility for total expressiveness). A
  `variant` (closed sum) does **not** substitute for generics (open parametric) —
  closed-vs-open is the line a variant cannot cross. Defer until the self-hosted
  compiler reveals where duplication actually hurts, then design constraints with
  real data: positive constraints C#-style (`: IService`), exclusion (`!`) only
  for primitives and sealed types. **Agent rule:** in the seed, do not write
  generics; duplicate concrete types where needed (the seed is small).

### B.32 — The import model: `use` is alias-only; re-export is abolished
- **Was (the tension):** B.9 originally let a cross-namespace reference be qualified
  by the *last segment* (`lexer::Token`) as if that segment were ambient, and left
  the full repertoire of `use` (selective imports? wildcard? re-export?) open.
- **Is (the addressing principle — from B.9 re-grounding):** **no relative or
  indirect access.** A symbol's canonical name is its **full absolute path from the
  project's canonical root** (`teko::lexer::Token`); within the same namespace,
  references are bare. The absolute path **always works, everywhere** (a dependency's
  symbols are reached by *its* canonical root, the dependency declared in the `.tkp`).
- **Is (`use` has exactly one job — create an alias):** `use` does **not** change
  visibility (that is `pub`/`exp`) and does **not** declare a dependency (that is the
  `.tkp`). It **binds a shorter name** to an absolute path, for ergonomics:
  - `use teko::lexer` → binds **`lexer`** (the **last segment**, the implicit alias)
    → write `lexer::Token`.
  - `use teko::lexer as lex` → binds `lex` → write `lex::Token`.
  - `use teko::lexer::Token` → binds `Token` (last segment) → write `Token`.
  - `use teko::lexer::Token as Tk` → binds `Tk` → write `Tk`.
  The bound name is a **complete stand-in** for the absolute path — **not** a relative
  lookup. (`lexer::Token` after `use teko::lexer` is "alias `lexer`, member `Token`,"
  not "find `lexer` from here.")
- **Is (three alias scopes):** **file-local** (a `use` at the top of a `.tks`),
  **global** (declared in the `.tkp`'s `[aliases]`, project-wide), and **imported**
  (renaming a dependency's root in the `.tkp`'s `[dependencies]`).
- **Is (the iron rule — a name resolves to exactly ONE thing; ambiguity is always an
  error, never silently resolved):** a `use` binds a name (the implicit last segment,
  or an explicit `as` alias). If that bound name would make any name in scope resolve
  to **two** things — a clash with a **function**, a **type**, **any other item**,
  **another alias** (including two `use`s binding the same name), or a **sub-namespace
  segment** — it is a **compile error**. There is **no precedence rule** that picks a
  winner; Teko never silently chooses. The clash is resolved **by the author**: give a
  distinct `as` alias, or drop the `use` and write the **full absolute path** (which is
  *always* unambiguous — it is fully qualified from the canonical root and collides with
  nothing). `teko::` is not aliasable (reserved). The **absolute path always remains
  valid** regardless of aliases: an alias opens a door, it never walls off — or
  obscures — the canonical address. *(Same principle as the sub-namespace collision in
  B.9: a name owned by two things is an error, not a guess.)*
- **Is (re-export — ABOLISHED, seed and for all eternity):** a namespace may **not**
  republish another's symbol under its own name. `pub use`/`export … from …` does
  **not** exist and **will never** exist. Re-export is the **indirect access this
  principle forbids** — `A::Thing` re-exported from `B` would *lie* about where
  `Thing` lives (it lives in `B`), create a **second** public path to one symbol, and
  put the definition where the reader does **not** find it. The underlying needs are
  met **honestly**: a **facade** is a real one-line **wrapper** function at the public
  path (the reader finds a definition there; it calls the deep one) or a public symbol
  simply **placed at its public path** (the stdlib does this — `teko::print` lives at
  the top of `teko::`, not re-exported); **API stability** is the discipline of the
  `exp` surface (you commit to those paths and do not move them; non-`exp` internals
  move freely, being unaddressable from outside). For a public **type** with a deep
  home, the type **lives at the public path** — there is **no** transparent
  cross-namespace type alias (that would be type re-export).
- **Is (what does NOT exist):** no relative imports (`self`/`super`/`crate` — abolished
  in B.19), no wildcard `use …::*` (it imports invisible names — the reader cannot
  tell where each came from, breaking the *legibility-local* facet), no last-segment
  qualification without `use` (the old B.9 rule — now the absolute path), no re-export.
- **Why:** **M.3** — every reference *tells the truth* about where a symbol lives;
  re-export and relative paths lie. **M.5** — **one** addressing scheme (absolute from
  the canonical root), not two; `use` earns its weight as the single, explicit
  shortening device. The *legibility-local* facet of **M.2** — a `use` at the top
  states the absolute origin once; the reader always knows where a name comes from
  (wildcard and re-export destroy this). **M.1** — a name resolves to exactly one
  thing; ambiguity (from any `use`, alias, or sub-namespace clash) is a **compile
  error with no precedence**, never a silent surprise — *exclusion-by-construction*.
  **Agent rule:** reference by absolute path from the canonical root; shorten with
  `use` (alias-only, last-segment implicit, never shadowing); if a `use` would clash
  with a function/type/item/alias/sub-namespace, it is an error — resolve with a
  distinct `as` or the full absolute path; never re-export; never write a wildcard or
  relative import.

### B.33 — The `.tkp` manifest is TOML
- **Was (open):** B.9 fixed *what* the `.tkp` declares (canonical name, artifact type,
  dependencies + aliases, source root) but not its *format*.
- **Is:** the `.tkp` is **TOML** (a strict subset). It declares: `name` (the canonical
  root), `artifact` (`"executable"` / `"library"`), `source` (the invisible source
  root), `[dependencies]` (with versions and optional import aliases), and `[aliases]`
  (project-wide global aliases). Example:
  ```toml
  # teko.tkp — the project manifest
  name     = "teko"          # the canonical root — everything is addressed from here
  artifact = "executable"
  source   = "src"           # invisible source root; namespaces start after it

  [dependencies]
  json     = "1.2.0"                                    # referenced as json::Parser
  fastjson = { name = "json-simd", version = "0.4.0" }  # imported alias of the dep

  [aliases]                  # project-wide aliases (never shadow)
  Tk = "teko::lexer::Token"
  ```
- **Why (aferição against the laws):** the manifest is read **first**, before any Teko
  is parsed (to know what to compile), so its parser must be **standalone and simple**
  — it cannot reuse the Teko parser (which the manifest precedes). **YAML is rejected**
  — it *lies* (`no` → false, `1.0` → float: the "Norway problem"), exactly the implicit
  coercion **M.3** forbids, and its grammar is ambiguous (**M.1**) and heavy (**M.0**).
  **JSON is weak** — no comments (a manifest must explain its choices — **M.2**) and
  verbose (**M.5**). **INI is weak** — no clean nesting for dependencies + aliases.
  **TOML wins**: explicit and *typed* (`"1.0"` string vs `1.0` float — honest, **M.3**),
  commentable (**M.2**), minimal and deterministic (**M.5**), unambiguous with a simple
  standalone grammar (**M.1** + **M.0**), its `[section]` markers are a clean markup,
  its `1_000` digit separators align with **B.28**, and it is **not bespoke** — no new
  parser to design and maintain (**M.5**), proven by Cargo for exactly this role. A
  Teko-native manifest would win on single-syntax (**M.2**) but pays the chicken-and-egg
  toll (a duplicate standalone parser — **M.5**), which TOML avoids. **Agent rule:**
  write `.tkp` as TOML; never put `teko` as a *dependency* (it is the language's
  reserved canonical root) and never put the source root in a path.

### B.34 — The runtime stance: a "metal" ethos, AOT-on-host as the LTS, bare-metal as aspiration
- **Was (the unstated tension):** M.0 says "metal" and the *desire* is **bare-metal**
  (no OS, bare hardware). But rewriting every access/security/driver layer takes
  **years**, even with AI assistance. Pretending the first (or LTS) version is
  bare-metal would be the lie **M.3** forbids.
- **Is (name three things precisely):**
  - **The "metal" *ethos* — true now (the law M.0):** no garbage collector, no hidden
    runtime, no managed heap; **you** control memory (arenas — B.7); costs are visible;
    the boundary to the host is **thin and explicit**. This is "systems language" in the
    C/Rust/Zig sense — **compatible with running on a host OS**.
  - **"Bare-metal" — the aspiration, not now:** no OS, device drivers, bare hardware.
    The north star, years away.
  - **Three materialization stages (build-order, M.4):** **(1) `.tkb`** — IL/bytecode,
    *interpreted*: the **first materialization** and bootstrap stepping-stone (least
    "metal," simplest to reach; IO via the interpreter's host syscalls). **(2) AOT-native
    on a host OS** — **the LTS**: native code, no GC, no hidden runtime — the *ethos*
    fully realized; IO via direct host syscalls — **this is what ships**. **(3) bare-metal**
    — the aspiration: native, no OS, IO via drivers; isolated behind the IO boundary
    (`teko::io` — B.35) so it is reachable by **swapping an implementation, not
    redesigning**.
- **Is (the honesty):** Teko **ships stage 2** (AOT-on-host) as the LTS and **does not
  claim stage 3** (bare-metal) for what ships. The constitution's "metal" reads as the
  *ethos* (true at stage 2), never as bare-metal (the aspiration). The progression is
  named, not hidden.
- **Why:** **M.3** — the gap between aspiration and reality is *named*, not papered over
  with a flattering word (most "systems" languages leave it unsaid; Teko states which of
  the three levels it is at). **M.4** — the stages are layers: never stage 3 before 2
  before 1; build-order applied to the runtime itself. **M.5** — we do not build
  bare-metal drivers (years of weight) when AOT-on-host serves; the minimal runtime that
  works. **Agent rule:** call the runtime *AOT-on-host* (the LTS reality); call "metal"
  the *ethos*; call "bare-metal" the *aspiration*; never claim bare-metal for what ships.

### B.35 — The IO model: slurp (whole-file `[]byte`), streams deferred, the `teko::io` boundary
- **Was (open):** how does the compiler access physical data (read source, write output,
  emit diagnostics)? Streams or whole-file?
- **Is (slurp, not streams — for the seed):** the seed reads and writes **whole files**:
  `read_file(path) -> []byte | error` (open, read all, close), `write_file(path, []byte)
  -> () | error` (write the whole buffer), and `write_err([]byte)` (diagnostics to
  stderr). **Streams are deferred to evolution** (they enter when large inputs justify
  their weight — the same "enters when use justifies it" pattern as function pointers).
  The compiler and the initial source are *small*, so the slurp model suffices.
- **Is (`read_file` returns raw `[]byte`, not `[]u8`):** a file **is** bytes (octets).
  Interpreting bytes as *text* (UTF-8 → `char` → `str`) is a **separate** step — the
  lexer's / `str`'s, not IO's. (`byte` is a distinct type, not a shadow of `u8` — B.12;
  the `char`/`str` layer rests on this `[]byte` boundary.)
- **Is (the IO boundary — `teko::io`):** the stdlib module that does file/console IO is
  the **named, thin, isolated** boundary between Teko and the host. Its **interface**
  (`read_file`, `write_file`) is **stable across the materialization stages** (B.34); only
  its **implementation** descends the stack: the `.tkb` interpreter (stage 1) and the
  AOT-native code (stage 2) implement it as host syscalls; a bare-metal target (stage 3)
  would implement it as device drivers. So the aspiration is reachable by **swapping the
  boundary's implementation**, not redesigning.
- **Why:** **M.5** — peso×uso: small files do not justify the weight of streams
  (buffering, state, EOF, partial reads); slurp is minimal, streams are premature.
  **M.3** — `read_file` is honest (one open/read-all/close, no hidden buffering or cache;
  the bytes are the file's bytes; a file is data, text is an interpretation — the two are
  kept distinct). **M.1** — `read_file -> []byte | error`: explicit failure if
  absent/unreadable (B.1 error-as-value, B.16 match-propagation). **M.0** — thin over host
  syscalls, no fat IO runtime, on a *named* host (not a pretended bare-metal). **M.4** —
  the IO boundary is a clean isolated layer. **Agent rule:** in the seed, read/write whole
  files via `teko::io` (`read_file`/`write_file` over raw `[]byte`); do not write streams;
  keep IO as the thin, named, swappable host boundary.

### B.36 — The text types: `byte`/`str`/`char`, UTF-8 the (validated) codepage; transcoding is evolution
> ⚠️ **Corrected (milestone classification).** A first draft mislabeled `char` and UTF-8 validation as
> *evolution* and renamed the codepoint `rune`. Both wrong: they are **alpha-native** (a presentable
> systems language whose codepage is UTF-8 *must* have them — the **bitwise** pattern: the bootstrap does
> not use them, but the language has them). `str` **always validates** (UTF-8 is *forced*, guaranteed from
> the bootstrap). The codepoint type keeps the name **`char`** (honest because **variable**). Corrected
> text below.
- **Was (open / two near-misses):** the literal layer was missing (the audit found the
  lexer tokenizes no `str`/`byte` literal yet the compiler's own source uses 52 strings +
  44 chars). A first pass modeled a codepoint as a fixed `char = [4]byte`, then briefly as
  an *evolution* `rune`.
- **Is (`char = [4]byte` is rejected — it lies; `char = []byte` is honest):** a UTF-8
  codepoint is **variable** (1–4 bytes). A *fixed* 4-byte slot *claims* "always 4" for
  1–4-byte content (and wastes the rest as padding) — the dishonesty **M.3** forbids. A
  **variable** `char = []byte` does not lie, so the name **`char` is kept** (the `rune`
  rename existed only to escape the fixed-width lie; variable removes the lie).
- **Is (three milestone tiers — not two):** Teko has four milestones (M.0): **bootstrap →
  alpha → LTS → evolution**. "Not needed for the bootstrap" is **not** "evolution" — the
  middle tier, **alpha (native, presentable)**, is where features the *compiler* does not
  use but a *systems language* must have live (the **bitwise** precedent). The text layer
  splits across the tiers:
  - **BOOTSTRAP:** `byte = u8` (a newtype — B.13: one **octet**, data, distinct from the
    *number* `u8` — B.12); `str = []byte` **validated** UTF-8 (Teko's codepage — ASCII-
    compatible, universal); literals **`str` (`"…"`)** and **`byte` (`b'+'`)**; a
    **byte-level** lexer (matches ASCII syntax with byte literals, collects string bytes).
    `str` is stored as compact UTF-8 octets, **zero-copy** from `read_file`'s `[]byte`.
  - **ALPHA — native:** **`char = []byte`** — the **variable** UTF-8 bytes of **one**
    codepoint (1–4); a *view* into a `str`'s bytes (zero-copy). The unit for codepoint
    work: iterating a `str` as codepoints, classifying, the numeric codepoint via an
    explicit decode. The bootstrap is byte-level and does **not** use it, but the language
    **has** it (like bitwise). *(Distinct from Go's `rune = int32` scalar; Teko's `char`
    is the variable bytes, keeping codepoint↔`str` zero-copy.)*
  - **EVOLUTION:** **transcoding** other codepages (`from_latin1`/`to_latin1`/…) —
    interop with legacy systems, less fundamental than UTF-8 itself.
- **Is (UTF-8 is FORCED — `str` always validates, guaranteed from the bootstrap):** `str`
  *means* "valid UTF-8 text," so a `str` **must be** valid UTF-8 — the invariant (**M.1**).
  Constructing a `str` from raw bytes therefore **always validates**: `str_from_utf8(bytes)
  -> str | error` (a **bootstrap** function, byte-structure well-formedness; the source is
  read and validated once, then the lexer scans the validated bytes byte-level). Assuming
  *without* validating would make the invariant a **lie** (M.3). So validation is native and
  **always-on**, not deferred — `str` is valid UTF-8 from the first octet.
- **Is (the encoding stance — bytes have no inherent encoding):** the **same** octets are
  different text in different codepages (`[0xC3 0xA1]` = "á" in UTF-8, "Ã¡" in Latin-1).
  From raw bytes it is **impossible** to know the codepage — heuristic detection (`chardet`)
  is **guessing**, the lie **M.3** forbids. So Teko **never detects**. Two honest paths:
  **(1) validate** UTF-8 (the **always-on** `str_from_utf8` above — the honest substitute
  for detection: it does not prove the file's intent, it *guarantees the invariant*); and
  **(2) transcode** from/to a **declared** codepage (`from_<cp>(bytes) -> str`, `to_<cp>(str)
  -> []byte | error` — the *caller* declares the codepage; Teko transcodes faithfully, never
  guesses). Path (2) is **evolution**; path (1) is bootstrap.
- **Why:** **M.3** — a *fixed* `char` would lie about a variable codepoint; a `str` that
  did not validate would lie about being UTF-8; and bytes have no inherent encoding, so Teko
  never *guesses* one. **M.1** — `str` is valid UTF-8 by construction (always validated);
  `from_`/`to_` fail explicitly. **M.0/M.5 (the milestone axis)** — the *bootstrap* carries
  only `byte` + `str` + byte/str literals + the byte-level lexer + validation (what compiling
  uses); **`char` is alpha-native** (the language must handle codepoints — the bitwise
  precedent), not deferred forever; only foreign-codepage transcoding is true evolution.
  **M.4** — UTF-8 is the foundation; `char` and transcoding build *on* it. **B.12/B.13** —
  `byte`≠`u8`≠`char` are real distinctions, carried as newtypes. **Agent rule:** bootstrap
  text = `byte` + `str` (always-validated UTF-8) with `"…"`/`b'+'` literals and a
  **byte-level** lexer; `char` (the *variable* `[]byte` codepoint) is **alpha-native** — the
  language has it though the bootstrap does not use it; foreign-codepage **transcoding** is
  evolution; **never** detect an encoding — validate UTF-8 (always) or transcode from a
  *declared* codepage.

### B.37 — Doctrinal correction: `void` supersedes `Unit`; `error` (native) supersedes `Error`; variant = complete types (no constructors); nullability is `T?` (never a variant member)
> ⚠️ **Supersession + drift excision.** Four rulings the legislator gave, ratified together, that
> contain (stop the bleeding) and correct (excise) constructions that entered without judgment or by
> drift between frozen documents. Each was already latent in the canon (the "is" below cites where);
> this entry records the supersession and flags the drift source so the code-correction phases excise
> it. *(Full plan: `TEKO_CORRECTION_PLAN.md` §1; the legislation's distilled form is the new
> "Doctrinal correction" subsection in `TEKO_LEGISLATION.md`, dated alongside this entry.)*

- **Was (the drift / the rejected forms):**
  - **`Unit`** — an empty-struct "value-less" type (`type Unit = struct {}`), used as a variant member
    and as a "no value" binding/return (`Type = … | Unit`, `-> Unit`, `Unit {}`). It was a *value* that
    meant "no value" — a type pretending to be the absence of one.
  - **`Error` / `teko::Error` / `Valor | Error`** — the failure case spelled with a **capital** `Error`
    (a special, almost-keyword type), threaded as `Valor | Error` through the early entries.
  - **Variant with constructors** — the Rust `Case(payload)` form (an inline case wrapping a payload),
    "the Rust casts shadow."
  - **Nullable variant members** — `Value? | i32`, a `?`-marked member *inside* a variant.

- **Is (the four rulings, each already in the canon):**
  1. **`void` = "returns no value" — a return marker, never a type.** A function **either** has a return
     type **or** `-> void`; `->` is always present (no "absence of `->`"). `void` is **never** a value,
     **never** a variant member, **never** a binding type. **This supersedes `Unit`** — `Unit` ceases to
     exist. *(Already canon: §5829–5833 "`void` — return type for 'returns no value'… every fn declares
     its return type"; §5971 "Every fn has `-> Type` (or `-> void`).")*
  2. **`variant` = a sum of COMPLETE declared types.** Each member is a real, separately-declared type:
     any native (`u8`, `i32`, `error`, `str`, `byte`, `[]f64`, …), an `enum`, a `struct`, or another
     `variant`. **No constructors** (the `Case(payload)` form is forbidden). **No `void`. No nullable
     members.** *(Already canon: B.14; `LEGISLATION §109` "each case is a real, separately-declared
     type… no special constructor"; `src/checker/type.tks` models `Variant = members: []Type`.)*
  3. **Nullability = the `?` suffix on a whole type — `T?`, `?.`, `??`.** `T?` is a **built-in
     type-former** (not generics), with safe access `?.` and Elvis `??`. **A variant member cannot be
     nullable**: `Value? | i32` is illegal → the dev declares `type Val = Value | i32` and marks the
     *return* `-> Val?`. The two failure domains are **disjoint**: value-absence flows through
     `?.`/`??`; recoverable error flows through `match`. The seed implements `T?` **fully** (model +
     parser + checker + codegen + VM). *(Already canon: `REBOOT_PLAN §202–203`; `LEGISLATION §75` "`?`
     is reserved strictly for nullability"; B.2.)*
  4. **`error` is the NATIVE lowercase type.** It **supersedes** `Error` / `teko::Error` / `Valor |
     Error`. *(Already lowercase in `src/core.tks`.)*
  - **Composite consequence:** a fallible function with **no value** is **`-> error?`** (null = ok, an
    `error` value = failure) — **never** `void | error` (void-in-variant is illegal). A fallible
    function **with** a value is **`T | error`**. (The C `tk_check_result {ok, error}` *is* exactly an
    `error?`.)

- **Why:** **M.3** (honest) — a value that means "no value" lies (`Unit`); `void` names the absence
  *as* a return marker, carrying nothing. **M.1** (fail-loud / exclusion-by-construction) — making
  `void` un-representable as a value/member/binding, and forbidding nullable variant members, removes
  whole classes of invalid states *by construction* rather than by check; the two failure domains
  (null vs error) stay disjoint and each fully handled. **M.2** (explicit) — `->` is always written;
  `T?` marks nullability at the type; `error` is a plain named type, not a magic capital. **M.5**
  (austere) — one absence-marker (`void`), one nullability form (`T?`), one error type (`error`); no
  parallel `Unit`, no constructor sugar in variants, no second spelling of the failure case. The
  lowercase `error` (M.3 + M.5) refuses the special-cased capital `Error`.

- **Drift source flagged (must be excised in code-correction, NOT here):** `TEKO_CHECKER.md`
  introduced `Unit` **as a type** — `:78`, `:81`, `:90`, `:1095`, and `:1245–1278` (`type Unit =
  struct {}`, `Type = … | Unit`, `Unit {}`, `-> Unit`). This is **frozen-doc drift**: it entered the
  spec without judgment and contradicts the `void` ruling above. It is recorded here so phases **Z1+**
  (the type-model and checker corrections, `TEKO_CORRECTION_PLAN.md` §3) **excise it**; `TEKO_CHECKER.md`
  is corrected under crumb **Z2e**, not in this amendment.

- **Guard (the rule-of-thumb, usable as a one-line check):** *no `Unit`, no `variant { Case(...) }`, no
  `void`/nullable inside a variant, no capital `Error` — `void` is a return marker, `error` is native,
  nullability is `T?`.* No new code may introduce any of these forms.

- **Agent rule:** write `-> Type` or `-> void` on every fn (never `-> Unit`, never omit `->`); make a
  variant a union of complete declared types only (no `Case(payload)`, no `void`, no `?` member);
  spell nullability `T?` / `?.` / `??` (and lift a would-be-nullable member to the whole type:
  `-> Val?`); spell the error type lowercase `error`; a fallible-no-value fn is `-> error?`, a
  fallible-with-value fn is `T | error` — **never** `void | error`.

---

### B.38 — The native numeric type set: `u8…u128`/`i8…i128` + `f16`/`f32`/`f64` + `dec` + `bigint`; floats UN-DEFERRED (three rulings)
> The legislator fixes Teko's full native numeric inventory, **stages** it (Tier 1 now vs `dec`/`bigint`
> deferred), and **un-defers floats** with three ratified rulings. The set was always *implied* (the metal
> has these widths); this entry legislates it as the canon and removes the float deferral. *(Full plan:
> `TEKO_CORRECTION_PLAN.md` §5; the legislation's distilled form is the new "native numeric type set + the
> float rulings" subsection in `TEKO_LEGISLATION.md`, dated alongside this entry.)*

- **Was (the seed's narrower set + the deferrals):**
  - The **live** seed inventory was integers **`u8…u64`** / **`i8…i64`** — widths up to 128 were not yet
    native, and **floats were deferred** ("float … deferred" in the roadmap; `dec` named but not built).
  - **`bigint`** had been reverted to a **library** type (B.30) once the sign-check (B.22) removed the
    comparison ceiling that briefly justified making it native.

- **Is (the legislated native numeric set):**
  - **Integers:** `u8 u16 u32 u64 u128` and `i8 i16 i32 i64 i128`.
  - **Floats:** `f16 f32 f64` — **un-deferred** (the roadmap's "float … deferred" is lifted).
  - **`dec`** — a decimal, **256×256 = 512 bits**.
  - **`bigint`** — **arbitrary / variable precision** (↺ now a **named native** type, superseding B.30's
    "library, not native"; staged behind its runtime).
  - Plus the existing **`bool`** (two values, boolean algebra) and **`byte`** (an octet, newtype over `u8` —
    B.36). **This supersedes the seed's narrower `u8…u64`/`i8…i64` set.**
  - **Staging:** **Tier 1** (`u128`/`i128` + `f16`/`f32`/`f64`) is implemented **now** (lexer → checker →
    codegen → VM, end-to-end); **`dec`** and **`bigint`** are **named-but-deferred** — larger,
    **runtime-backed** types (a 512-bit decimal; a heap-limb bignum) that land when their runtime is built.

- **Is (the three float rulings, ratified):**
  1. **Float literal syntax + `f64` default.** A float literal is `3.14` / `1.5e3`; an **un-annotated**
     float literal **defaults to `f64`**. `f16`/`f32` require an **annotation** (`let x: f32 = 3.14`).
  2. **Float ÷0 PANICS** — the same as integer ÷0 (`tk_panic_div0`): Teko **intercepts at the origin**, so
     IEEE `∞`/`NaN` never surface as Teko values in the normal flow ("the NaN coffin stays shut" — B.24).
     **Not** IEEE `∞`/`NaN` in the normal flow.
  3. **`float↔int` via `to` is allowed with a RUNTIME guard.** It **truncates toward zero**; a value that
     does **not fit** the target (overflow / `NaN` / `∞`) **PANICS** (parity with the integer cast guard).
     `int→float` and `float→float` width changes likewise go through **`to`**.

- **Why:** **M.0** (metal) — the widths *exist* on the silicon; a systems language names what the hardware
  carries (8…128-bit integers, IEEE 16/32/64-bit floats), so legislating them native is honoring the metal,
  not adding weight. **M.1** (fail-loud, no silent loss) — float ÷0 **panics** like integer ÷0 (no poison
  `∞`/`NaN` leaking as a value), and `float↔int` conversion is **runtime-guarded** (doesn't-fit/NaN/∞ →
  panic), the same panic-guard doctrine as the integer cast (the numeric-conversions block) — loss is caught,
  never silent. **M.3** (honest, explicit) — the **literal default is documented** (`f64`) and a narrower
  width is *asked for* by **annotation**, not silently chosen; each type names exactly what it is (`dec` is
  decimal, not binary float; `byte` is an octet, not a number). **M.4** (build order) — Tier 1 enters because
  lexer/checker/codegen/VM carry it; `dec`/`bigint` are **deferred behind their runtime**, named now so the
  spelling is reserved without building ahead of the layer. **M.5** (austere) — naming the deferred types
  costs nothing; only Tier 1 carries implementation weight in the seed.

- **Clause extended (NOT a new Law):** the existing **numeric-conversions** clause (Redefinitions Index,
  *Numeric conversions (`to`)*) — "any defined numeric→numeric conversion … impossible → panic" — is
  **extended to include `float↔int` / `int→float` / `float→float`** under the **same panic-guard doctrine**
  (truncate toward zero, panic on impossibility; constants out of range stay a compile error). No new M-law
  is invented; this is the integer-cast guard applied to floats.

- **Agent rule:** treat `u8…u128`/`i8…i128`, `f16`/`f32`/`f64`, `dec`, `bigint`, `bool`, `byte` as the
  native numeric set; **implement Tier 1** (`u128`/`i128` + the three floats) now and leave `dec`/`bigint`
  as named-but-deferred (runtime-backed); write a bare float literal as `f64`-typed and require an annotation
  for `f16`/`f32`; make float ÷0 **panic** (`tk_panic_div0`); route `float↔int` through `to` with the runtime
  guard (truncate toward zero, panic on doesn't-fit/NaN/∞). Never emit IEEE `∞`/`NaN` as a value in the
  normal flow.

### B.39 — Backend reversal: Teko's OWN native backend (transpile-to-C revoked); conclude all first

- **Was:** the AOT backend was **transpile-to-C** — the codegen lowered the typed tree to **C**, and the host
  **`cc`** compiled it to a native binary (the legislator's original choice — §B.34/§B.35; TEKO_ROADMAP_BINARY
  Fase 2 "TC"). *Why then:* **M.5** (reuse the host toolchain; realize stage-2 AOT-native without writing a
  native code generator). Two execution modes were planned: (1) transpile-to-C/AOT, (2) the `.tkb` VM.

- **Is (legislator, 2026-06-24):** **transpile-to-C is REVOKED** as the destination architecture. Teko will
  build its **OWN native backend** — a direct native code generator (typed tree / `.tkb` → native object/binary),
  realizing the Constitution's **stage-2 (AOT-native on a host OS)** *without* `cc` as an intermediary.
  **Sequencing:** *conclude ALL current work FIRST*; the native backend is **not started** until the rest is
  done. The **`.tkb` VM (stage-1)** is **unaffected** and stays (debug/test + differential-correctness anchor).

- **Why:** **M.0** (the *ethos* is the metal — native code with no C middleman is closer to the silicon than
  "native via a transpiled C intermediary"). **M.4** (build order — the front end + checker + middle must be
  concluded before the back end is replaced; don't build the new back on an unfinished middle). **M.3** (the path
  becomes honestly *native*, not "native through C"). The earlier M.5 shortcut (reuse `cc`) served the bootstrap;
  the legislator now spends the implementation weight on a true native backend for what ships.

- **Resolved (operational, legislator 2026-06-24):** transpile-to-C is **revoked as PRIMARY but RETAINED — kept
  fully equalized — as a permanent FALLBACK and DIFFERENTIAL-CORRECTNESS COMPARATIVE.** *Why:* "we need to keep a
  fallback and comparative." So **three** execution paths must agree: the `.tkb` **VM**, the **transpile-to-C/`cc`**
  path (fallback + comparative), and the future **native backend** (primary). **Every wave lands in ALL active
  paths — codegen is NOT frozen** (W4/W5 etc. go into VM *and* codegen now; the native backend later).

- **Agent rule:** do **not** start the native backend yet; **conclude the current equalization** first, applying
  each wave to **both** the VM and the transpile-to-C codegen (they + the future native backend are the differential
  anchor — M.1). Do not delete the C path. The Constitution's three materialization stages are unchanged (only
  stage-2's *shipping implementation* moves from C-transpile to a native codegen; C-transpile lives on as fallback).

---

## Consolidated examples — the language in use (seed + evolution)

> These examples illustrate the decisions above as running code. **Seed** = what the bootstrap/
> alpha compiler accepts. **Evolution** = what the model is designed to grow into (in commented
> blocks, marked `// EVOLUTION`). The evolution forms are *designed for*, not implemented — they
> show the language has margin to grow without redoing syntax. Per M.4, none is built ahead of its
> layer; they're recorded so the seed's syntax doesn't become debt.

### E.1 — Numbers, promotion, conversion, overflow (B.22/B.24)
```teko
// Full numeric inventory; suffix = type name; default i32 / f32.
let a: i32 = 42                    // default integer
let b = 42i64                      // suffix picks the type
let big: u256 = 0xFF_FF_FF_FFu256  // hex + separator + suffix
let half: f16 = 1.5f16             // f16 (GPU/ML)
let money: dec = 19.99dec          // fixed-point decimal (never default)

// Promotion: literal adapts to the typed operand if it fits; two typed promote.
let c = b + 1                      // 1 adapts to i64 (b is i64)
let mixed = 2i32 + 3i64            // promotes to i64 (the larger)
// let bad = 2i32 + 3.0f32         // arithmetic mixes int+float → float; OK
// bitwise: NO promotion — same width AND sign or error:
let flags = 0b1010u8 & 0b0110u8    // both u8 → OK
// let no = 1u8 & 1u16             // ERROR: different widths, no bitwise promotion

// Conversion `to` — any DEFINED numeric→numeric conversion is allowed; loss is caught, never silent:
let widen  = a to i64              // i32→i64, sign-extend, OK (value-preserving)
let trunc  = 3.7 to i32            // float→int truncates toward zero → 3
let narrow = big to i32            // u256→i32: ALLOWED — runtime-guarded; PANICS if the value doesn't fit (impossibility)
// let bad = 300 to i8            // COMPILE error: constant provably out of range

// Comparison is made safe by a sign-check (codegen, not promotion) — kills the
// C signed/unsigned trap, moves no extra bytes, and has NO ceiling:
let ok = 5i32 < 10u32              // sign-check: 5 ≥ 0, compare directly → true
let top = x_u256 < y_i256          // works! (if i256 < 0 it's smaller; else compare 256 bits)
// operators return bool; for three-way order use compare → Ordering:
let ord = compare(a, b)            // Ordering::Less / Equal / Greater (enum over i8)
// match ord { Ordering::Less => … ; Ordering::Equal => … ; Ordering::Greater => … }

// Overflow: panic-debug / wrap-release (a DEFINED value, never poison ∞/NaN).
let sum = a + 1                    // fine
// let of = 255u8 + 1             // literal overflow = COMPILE error
let wrapped = math::wrapping_add(255u8, 1u8)   // wraps always → 0 (modular intent)
let checked = math::add(a, 1)      // → i32 | error (recoverable detection)
```

### E.2 — Division, the NaN coffin, partial operations (M.1, B.24)
```teko
// Two divisions: operator (metal, panics on ÷0) and library (checked).
let q = 10 / 2                     // 5 — the metal operator
// let boom = 10 / 0              // literal ÷0 = COMPILE error
// at runtime, x / 0 PANICS (a bug) — ∞/NaN never produced ("coffin stays shut")

// When ÷0 is EXPECTED, the error flows as a value (look-before is also fine):
let r = math::div(10, divisor)     // → i32 | error
match r {
    i32 as n   => log($"resultado: {n}")
    error as e => log($"erro: {e.to_string()}")
}

// Seed convention for partial ops: "look before you leap" (if-before):
if divisor != 0 {
    let safe = 10 / divisor        // excluded the invalid by construction
}
```

### E.3 — Arrays: jagged vs rectangular, creation, `.len` (B.25)
```teko
// []T prefix notation. .len is an intrinsic u64 field (no parens, no generics).
let xs: []i32 = [](1, 2, 3)        // 1-D, values in () after the [] marker
let n = xs.len                     // 3  (u64, intrinsic)

// dimensioned-with-default: infers type, fills default:
let grid = [3, 4]i32               // 3×4, all zero

// REAL multidimensional [,] — contiguous, rectangular (uniformity enforced):
let m = [,]( (1, 2, 3); (4, 5, 6) )    // 2×3; rows separated by ; inside ()
let cell = m[1, 2]                 // 6  — single-bracket multi-index

// JAGGED [][] — independent sub-arrays, may be irregular:
let jag = [][]( (1, 2); (3, 4, 5) )    // rows of different lengths OK
let item = jag[1][2]               // 5  — chained indexing

// Never nullable as a whole — only elements: []Node?  (the array always exists)
// Out-of-range PANICS (a bug). Checked form is evolution:
// let safe = arrays::at(xs, i)    // EVOLUTION → i32 | error (needs generics)
```

### E.4 — Strings: concatenation `$`, interpolation, formatting (B.27)
```teko
// `+` NEVER concatenates (it's addition — honesty/legibility-local).
// `$` concatenates (string+string only, stays in the string category):
let full = first $ " " $ last      // concatenation operator

// Interpolation $"..." is sugar over concatenation; {} = explicit convert-to-string:
let greet = $"Hello, {name}! You have {count} messages."
// C# format specifiers (: inside {} = format):
let price = $"Total: {amount:F2}"  // two decimals

// Culture invariant ALWAYS (deterministic); to_string/parse auto-imbued for primitives:
let s = (42).to_string()           // "42" (invariant)
let parsed = i32::parse("42")      // → i32 | error

// error has to_string auto-imbued (errors exist to be communicated):
match parsed {
    i32 as v   => log($"got {v}")
    error as e => log(e.to_string())
}
```

### E.5 — Methods: instance, static, the bare `self` (B.29)
```teko
type Retangulo = struct {
    largura: f64
    altura: f64

    fn area(r) -> f64 { r.largura * r.altura }            // instance (self = r, copy)
    fn to_string(r) -> str { $"{r.largura}x{r.altura}" }  // instance
    fn quadrado(lado: f64) -> Retangulo {                  // static (no self) — constructor
        Retangulo { largura = lado; altura = lado }
    }
}

let sq = Retangulo::quadrado(5.0)  // static → ::
let a = sq.area()                  // instance → .   ≡ Retangulo::area(sq)
let s = sq.to_string()             // instance → .
// No overload: a second `area` in this struct (any signature) would COLLIDE.
// Free function still coexists — could write `fn area(r: Retangulo)` at namespace level too.

// EVOLUTION (margin to grow, commented — not implemented):
//   - ref arrives → mutating methods: fn cresce(ref self, d: f64) { self.largura += d }
//   - OOP/generics arrive → override returns (subtype polymorphism); overload NEVER returns
//   - default args arrive (with format specifiers) → fn to_string(self, fmt: str? = null)
```

### E.6 — Variant, match, error as value (B.14/B.15/B.16)
```teko
// Variant = union of DECLARED types (no inline cases, no constructors):
type Numero = struct { valor: f64 }
type Texto  = struct { conteudo: str }
type No = variant Numero | Texto

// match binds with `as` (whole) or struct-form { } (by field); newline separators:
fn render(no: No) -> str {
    match no {
        Numero as n  => n.valor.to_string()
        Texto as t   => t.conteudo
    }
}

// error is ALWAYS handled with match (no `?` propagation in the seed — B.16):
fn carrega(caminho: str) -> Config | error {
    let dados = arquivo::le(caminho)   // → str | error
    match dados {
        str as texto => Config::parse(texto)
        error as e   => e                // pass the error along (manual)
    }
}
// EVOLUTION: when Result(T) is the stable form, the `?` propagation operator may return
//   let dados = arquivo::le(caminho)?   // EVOLUTION — sugar over the match above
```

### E.7 — Control flow, mutability, defer (B.20/B.21)
```teko
// `if` is an expression (the readable ternary); loop is the only primitive loop.
let maior = if a > b { a } else { b }

// mut only on local variables; compound assignment (all operators):
mut total = 0
mut mask = 0u8
loop {
    if i >= xs.len { break }       // if-before (break when was banned — B.20)
    total += xs[i]                 // compound arithmetic
    mask |= 0b0001u8               // compound bitwise (metal — M.0)
    i += 1
}

// defer runs on every exit of its block, in reverse order:
fn processa() {
    let recurso = abre()
    defer { fecha(recurso) }       // runs at every exit (normal, break, return)
    // ... work ...
}

// EVOLUTION: loop…from (idiomatic iteration, sugar over loop):
//   loop preco from precos { ... }            // value only
//   loop preco, i from precos { ... }         // value first, index second
//   let faixa = 1..100                         // range as VALUE (evolution; seed: pattern only)
```

## Appendix — Status

- **Written & validated:** the lexer (A.1) and the **parser** (A.2–A.14) — expression
  precedence, declarations (`let`/`mut`/`const`), the complete operators, variable
  references, compound assignment, type expressions, `fn` (declaration + call), type
  declarations (`struct`/`enum`/`variant`), destructuring, and control flow (`if`,
  `loop`, `match`). The parser crosses into `teko::lexer` by the **absolute path from
  the canonical root** (B.9/B.32), shortened with `use teko::lexer` (last-segment
  alias `lexer`).
- **Next example:** **`use`** (the import/alias layer — B.32; the seventh and last
  strand of the assignment dimension), then the **C23 port** of A.1–A.14 to
  `src/lexer/` and `src/parser/` (under the discipline-overhead rule + mirrored
  organization).
- This document grows incrementally each session. The final product spec will be
  distilled from it.
