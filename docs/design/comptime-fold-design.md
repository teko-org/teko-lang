# Comptime fold — general comptime capability (design / reading only)

Status: **DESIGN — counter-argued, law-grounded, awaiting owner on the three open
questions in §11.** Owner ruling 2026-07-19 selects reach **C (general comptime)**.
Author: architect. This document is design + reading only — **no product code is
written here**. It is ORTHOGONAL to the AL wave (F1/AL4a in flight); it touches the
FOLD side of const-eval/checker/codegen and does **not** touch AL-owned files.

> **Attribution convention used throughout.** A clause tagged **[owner 2026-07-19]**
> is the owner's ruling (the fold mandate). A clause tagged **[integrator-pinned,
> veto open]** is my inference, ratified law-first and standing until the owner vetoes.
> A genuine unresolved tension would HALT in plain text — §11 shows none does; the
> three items there are questions-with-recommendation, not HALTs.

---

## 0. The ruling and the canonical example

> **[owner 2026-07-19]** A **const-known** value must fold to a **literal
> pre-converted at comptime**, with **zero runtime overhead** — on **both engines**
> (`.tkb` VM and AOT-native). The reach is **C (general comptime)**: not only fold of
> isolated literal expressions, but **propagation** of const-known bindings and
> **general comptime evaluation**.

Canonical example (owner):

```teko
let a: u64 = 0xFF
let b = $"{a:X}"     // TODAY: runtime tk_str_concat + tk_fmt_x_upper(a)
                     // TARGET: FOLD → b = "FF" (a str literal in rodata), zero runtime
```

Read the canonical example precisely — it decides the mandate's shape:

- `a` is a **LOCAL `let`**, not a module const. So the mandate REQUIRES
  **local-binding const-propagation** (Layer 1b below), not only module-const
  inlining (which `inline_consts` already does).
- `b`'s initializer is a **`$"…"` interpolation with a static format spec `:X`**. So
  the mandate REQUIRES **folding interpolation + format at comptime** (Layer 2 below),
  which needs a comptime formatter that mirrors the runtime byte-for-byte.

**Therefore Layers 1 and 2 are both MANDATE** (the owner's own example does not fold
without both). Layer 3 (arbitrary comptime *execution* — const fn, loops, recursion)
is the only part that is a new *capability* to be M.5-staged (§4.3, §11).

---

## 1. Grounding — which Laws MANDATE the fold, and why M.5 does not block

I read `TEKO_CONSTITUTION.md` M.0–M.5 in full. The fold of a provably-const value is
mandated by the **composition** of three Laws, with M.5 out of jurisdiction.

### 1.1 The mandate (M.3 + M.0 + M.1 compose; none competes)

- **M.3 — Honesty ("a thing must be what it says it is"; "Formatting is
  deterministic").** A value the compiler can *prove* is fixed, but which is
  *computed at runtime* (a `tk_str_concat` + `tk_fmt_x_upper` for `$"{a:X}"` where
  `a` is a known constant), **pretends to be dynamic**. The string `"FF"` *is* a
  constant; emitting code that re-derives it every execution makes the program *say*
  "this is computed" when it *is* "this is fixed." That is exactly the operator/type
  lie M.3 forbids, applied to a value. Folding makes the code *be* what it *is*.
- **M.0 — the Metal ethos ("no hidden runtime … costs are visible").** A runtime
  concat/format for a value that never varies is a **hidden cost** — work the metal
  performs that the program's meaning does not require. M.0's "what metal *means*"
  clause names exactly this: no hidden runtime. Folding removes the hidden cost.
- **M.1 — Safety / Determinism ("Determinism, own formatting not the OS locale";
  the illumination: a loss the compiler "can already see" is a **compile error**, not
  a silent runtime event; `TEKO_LEGISLATION.md:148` "constants out of range remain a
  compile error").** M.1 already establishes that the compiler **resolves constants
  statically** and reports their hazards at compile time. Folding is the *general
  form* of that same posture: what the compiler can see, it evaluates now; an
  out-of-range or ÷0 in a const fold becomes a **loud compile error** (§3.4), strictly
  *more* M.1-honest than deferring it to a runtime the fold would have removed.

These **compose** (Constitution §III — "composition in general; hierarchy only as the
tie-breaker"). They govern different strata of the same decision: M.3 the *lie*, M.0
the *hidden cost*, M.1 the *determinism/where-the-error-fires*. No two give opposite
verdicts on one dimension, so the numeric hierarchy is never invoked *among them*.

### 1.2 Why M.5 (austerity) does not block the fold

M.5 governs **conveniences added ABOVE the metal** — surface features whose benefit is
brevity/comfort ("Default args, real methods, rich format specifiers … each pay only
when their case is common enough"). Its relation to M.0 gives the triage: *"metal
operator or abstract convenience?"* — metal → M.0 wins the boundary; convenience → M.5.

The **fold of a provably-const value is not a surface convenience**: it adds no
syntax, no new way to write code, no shortcut. It is an **optimization that enforces
M.0 (no hidden cost) and M.3 (no lie)**. It sits *below* M.5's jurisdiction entirely —
the same way the compound bitwise operators are "outside austerity's jurisdiction,"
the fold is outside it because it is a metal/honesty obligation, not an added
convenience. **[integrator-pinned, veto open]** M.5 is therefore not invoked for
Layers 1–2.

**Where M.5 DOES bite: Layer 3.** A machine that *executes arbitrary code at compile
time* (const fn bodies, comptime loops, recursion — Zig-style `comptime`) **is** a new
capability above the metal. M.5's burden-of-proof applies: "is it worth the
deliberation its absence forces?" The distinction the ruling asks me to draw:

> **MANDATE** = fold what is *provably* const (Layers 1–2). Governed by M.3/M.0/M.1;
> M.5 out of jurisdiction. **CAPABILITY** = execute *arbitrary* code at comptime
> (Layer 3). Governed by M.5; staged and justified per concrete need, never adopted
> wholesale. **[owner 2026-07-19]** selected reach C, which I read as *the mandate in
> full now, and Layer 3's scope decided deliberately per §11 Q1 — not a blanket "add
> Zig comptime."*

### 1.3 M.4 note (build order) — favors the standalone Layer-2 formatter

M.4 (design domain): "do not build a dedicated feature on top of a form you already
know is transitional; prefer the general mechanism that survives." This pre-decides
the Layer-2 formatter tension (§4.2, §10): a Layer-2 formatter that *depends on* the
not-yet-existing Layer-3 execution machine would be building on the incomplete. The
self-contained, oracle-pinned formatter (Option A) survives regardless of whether
Layer 3 ever lands. **M.4 favors Option A now.**

---

## 2. What exists today (read before implementing — file:line ground truth)

| Concern | Location | State relative to the mandate |
|---|---|---|
| **Const-expr grammar gate** (Tiers 0–5; is a subtree *provably* const?) | `src/checker/consteval_form.tks:284` `is_const_expr` | EXISTS. Returns `error?`; does **NOT** evaluate numerically ("This gate does NOT fold numerically — the backend computes `~(0 to u64)`"). REUSE as the provably-const oracle. |
| **Const-propagation pass** (substitute const refs) | `src/checker/consteval.tks:522` `inline_consts` | EXISTS. Runs after `monomorphize`, before lowering. Substitutes **module-level** scalar/aggregate const references with clones of their initializers. |
| Total expr rewrite (the substitution walk) | `src/checker/consteval.tks:177` `inline_rw_expr` | EXISTS, TOTAL over `TExprKind`. Substitutes `TVar`→initializer but does **NOT** numerically evaluate `TBinary`/`TUnary`/`TIndex`, and does **NOT** fold `TInterp` to a literal. |
| Interp-hole rewrite | `src/checker/consteval.tks:323` `inline_rw_interp` | EXISTS. Substitutes const refs *inside* holes/specs but leaves the interpolation as a runtime `tk_str_concat` shape. **This is exactly the Layer-2 gap.** |
| Dep order / cycle detection | `src/checker/consteval.tks` `const_dep_order`, `ScalarConstMap`, `build_scalar_map`, `build_aggregate_map` | EXISTS. REUSE for ordering; extend the map for local bindings (Layer 1b). |
| Typed interpolation node | `src/checker/tast.tks:61` `TInterp = struct { pieces: []str; holes: []TExpr; specs: []TFSpec }` | The fold target. Spec kinds `TFSpecNone`/`TFSpecStatic{s:str}`/`TFSpecDynamic{args}` (`tast.tks:52-56`). |
| Runtime formatters (the byte source of truth) | `src/runtime/teko_rt.c` — `tk_fmt_x_upper/x_lower/b/f/e/g/p/d/n_i/n_f`, `tk_u64_to_str`, `tk_i64_to_str`, `tk_ftoa`, `tk_str_concat`, `tk_fmt_dyn_*` | **Maintained-C exception** (the one carve-out from Teko-only). Native LIR calls them by symbol (`src/lir/lower.tks:3975`); the VM runs the same LIR; the C backend emits the same calls (`src/codegen/codegen.tks:3022` `emit_interp`). |
| C-backend interp lowering (spec-family dispatch to mirror) | `src/codegen/codegen.tks:3022-3162` `emit_interp`, `:3016` `fmt_family_takes_prec` | The exact spec-char → formatter mapping the comptime formatter must reproduce byte-for-byte. |
| Local `const`/`let` binding | `src/parser/parse_stmt.tks:166` `BindKind::Const`; `DECISION_LOG.md:391` (D40) | A local binding **MAY hold a runtime value**; distinguished from module const purely by parse position; does **NOT** flow through consteval today. Layer 1b must preserve this. |

**The load-bearing finding.** The *propagation* substrate (Layer 1b's plumbing) and
the *grammar oracle* (`is_const_expr`) already exist. **What is missing is the
evaluator** — nothing in the tree turns a provably-const subtree into a *value*. The
old design deliberately deferred numeric folding to the C compiler
(`const-module-level-plan.md` D3: "No numeric folding … the backend computes"). That
delegation gives the *native* path zero runtime — **but the VM executes the op every
time**. The owner's "zero runtime on **both** engines" is precisely the demand that
the fold move **into the Teko compiler** (produce the literal), not be delegated to
the C compiler. That evaluator is the new machine this design introduces.

---

## 3. Layered scope (mandate → capability), staged

### 3.1 Layer 1 — const evaluation + propagation  [owner 2026-07-19, MANDATE]

The provably-const subset, folded to literals in the Teko compiler so **both** engines
see the literal.

**Layer 1a — the evaluator + expression fold.** A new comptime value domain
`ConstValue` and an evaluator `eval_const` that, over the `is_const_expr` grammar
(Tiers 0–5), computes the actual value; `fold_expr` then emits it as a literal
`TExpr`. `0xFF & 0x0F` → `0x0F`; `~(0 to u64)` → the literal `0xFFFFFFFFFFFFFFFF`;
`-(MAX_I64_I128) - 1` → the literal. **The VM now loads an immediate instead of
executing the op-tree.**

**Layer 1b — local-binding propagation.** Extend the substitution map to include a
LOCAL `let`/`const` binding whose initializer is provably const (`is_const_expr ==
null` in the local env). A per-function pre-pass seeds `env`/the map with such
bindings so a use of `a` folds. **Strictly guarded** (§3.3): a binding whose
initializer has *any* runtime leaf is left untouched — DECISION_LOG:391 preserved.
This is what makes the canonical `let a: u64 = 0xFF` fold.

**Layer 1c — `TIndex` of const.** `eval_const` handles `TIndex{ receiver =
<const aggregate>, index = <const int> }` → the element value. This **subsumes AL0
Tier-6** (`GZIP_MAGIC[0]`) — see §5 for the coordination (do not double-ship).

### 3.2 Layer 2 — interpolation + format fold at comptime  [owner 2026-07-19, MANDATE]

When (after Layer 1) **every** hole of a `TInterp` is a const literal and **every**
spec is `TFSpecNone` or `TFSpecStatic`, evaluate each hole through a **comptime
formatter** that mirrors `teko_rt.c` byte-for-byte, concat with the piece literals,
and replace the whole `TInterp` with a single `TStrLit` (interned to rodata like any
string literal). `$"{a:X}"` with `a==0xFF` → `TStrLit "FF"`.

A `TFSpecDynamic` (`:[width, prec]` with *runtime* args) or any *runtime* hole leaves
the interpolation as-is (runtime). A dynamic spec whose args are **also const** MAY
fold too, but I scope that as a follow-on (§11 Q3) — the mandate's example is static.

**The central risk lives here** (§10): the comptime formatter DUPLICATES the
formatting logic that today lives once in `teko_rt.c`. M.1 determinism demands the two
produce **byte-identical** output. Resolution in §4.2 and §10.

### 3.3 Boundary preservation (Layer 1b/2 must NOT change semantics)

The fold is an **optimization over the provably-const subset**, never a change to
`let`/`const` semantics (DECISION_LOG:391). Guarantees, asserted by fixtures (§8):

- A local binding that holds a **runtime** value (fn arg, IO result, a `mut`
  reassigned from runtime, a call outside the allowlist) is **never** folded, even if
  spelled `const`. `is_const_expr` on its initializer returns an error → skip.
- The fold **never rejects** a program that compiles today. Its only *new* rejection
  is an out-of-range/÷0 **inside a fold** (§3.4), which is already M.1's compile-error
  posture, and which the corpus does not currently hit (asserted by the fixpoint being
  green against the re-golden).

### 3.4 Overflow / ÷0 in a fold = loud compile error (not silent wrap)

**[integrator-pinned, veto open]** When `eval_const` hits an overflow, a ÷0, or an
out-of-range cast on a **constant** it can see, it returns a **located compile error**
— it does NOT wrap. Grounding: `TEKO_LEGISLATION.md:148` "constants out of range
remain a compile error"; M.1's illumination (a loss the compiler "can already see" is
a compile error). This is strictly *more* honest than today's release-wrap-at-runtime,
and it only fires on values that are provably const (a runtime overflow still wraps per
the existing release semantics — untouched). If a corpus site currently *relies* on a
const expression wrapping at runtime in release, the fold surfaces it as a compile
error; that is the correct M.1 outcome and is caught by the fixpoint delta. No corpus
site is expected to; asserted in §8.

### 3.5 Layer 3 — general comptime execution  [CAPABILITY, M.5-staged]

Executing arbitrary Teko at compile time: a `const fn` marker (a fn callable in const
context), comptime bounded loops, and bounded recursion — this is a **large** feature
(a comptime interpreter over the typed AST with a step/recursion budget, a purity
boundary excluding IO/allocation-with-runtime-identity, and a determinism guarantee).
**Honest size: L, plausibly multiple L crumbs** — it is a second evaluator that must
match runtime semantics for the *whole executable subset*, not just the const-expr
grammar.

**It is NOT required by the owner's canonical example** and is NOT in the mandate.
M.5 governs its scope. My recommendation (§11 Q1): do **not** adopt it wholesale;
stage a **minimal, pure** slice only when a concrete corpus need appears (the strongest
candidate: unifying the Layer-2 formatter into a single Teko implementation, §10
Option B). Until then, Layers 1–2 deliver the ruling; Layer 3 is a separately
ratifiable capability.

---

## 4. Type / function shapes the implementer adds (full-Javadoc, copy verbatim)

New home: **`src/checker/comptime_fold.tks`** (namespace `teko::checker`), a sibling of
`consteval_form.tks` (grammar) and `consteval.tks` (propagation). Rationale: bounds
cyclomatic complexity, gives the evaluator one obvious home, and keeps the grammar
gate / propagation walk / evaluator as three single-responsibility modules.

### 4.1 The comptime value domain + evaluator (Layer 1a)

```teko
/**
 * ConstValue — the comptime value domain: the result of evaluating a provably-const
 * expression subtree. An integer is stored as its raw two's-complement bit pattern in
 * a `u128` PLUS its declared type, so every integer family (i8..i128 / u8..u128 /
 * byte / char) re-emits and re-wraps exactly as the metal would; a float keeps its raw
 * bits so formatting is byte-exact; a string/bytes keeps its bytes; an aggregate holds
 * child ConstValues in order. This domain exists ONLY inside the fold — it never
 * escapes to lowering (the fold reconstructs a literal TExpr from it via `literal_of`).
 *
 * @field kind  the value's case (int / float / bool / bytes / aggregate)
 * @since #comptime-fold
 */
pub type ConstValue = struct { kind: ConstValueKind }

/**
 * ConstValueKind — the case of a ConstValue.
 *
 * @since #comptime-fold
 */
pub type ConstValueKind = variant CVInt | CVFloat | CVBool | CVBytes | CVAgg

/**
 * CVInt — an integer comptime value: the two's-complement bit pattern (`bits`) as it
 * would sit in a register of `ty`, plus the declared integer type (so a fold knows the
 * width for wrapping/overflow checks and re-emission).
 *
 * @field bits  the value's raw two's-complement bits, zero-extended into u128
 * @field ty    the declared integer type (drives width, signedness, wrap/overflow)
 * @since #comptime-fold
 */
pub type CVInt = struct { bits: u128; ty: Type }

/**
 * CVFloat — a float comptime value: the raw IEEE-754 bits (so re-emission and
 * formatting are byte-exact and never route through the host locale — M.1) plus
 * whether it is an f32 (32 significant bits in `bits`) or f64.
 *
 * @field bits   the raw IEEE-754 bits (f32 in the low 32)
 * @field is_f32 true iff the value is an f32, false for f64
 * @since #comptime-fold
 */
pub type CVFloat = struct { bits: u64; is_f32: bool }

/**
 * CVBool — a boolean comptime value.
 *
 * @field v  the boolean
 * @since #comptime-fold
 */
pub type CVBool = struct { v: bool }

/**
 * CVBytes — a string / char / []byte comptime value: the raw UTF-8 (str/char) or raw
 * (byte-slice) bytes, exactly as they would live in rodata.
 *
 * @field data  the raw bytes
 * @since #comptime-fold
 */
pub type CVBytes = struct { data: []byte }

/**
 * CVAgg — an aggregate (array / struct / variant) comptime value: the child values in
 * declaration order. Indexing (`TIndex`, Layer 1c) reads `elems[i]`; struct/array
 * re-emission rebuilds the aggregate literal from the children.
 *
 * @field elems  the child comptime values, in order
 * @since #comptime-fold
 */
pub type CVAgg = struct { elems: []ConstValue }

/**
 * eval_const — evaluate a PROVABLY-const expression subtree (one already accepted by
 * `is_const_expr`) to its ConstValue, matching the backend's arithmetic semantics
 * EXACTLY (Layer 1a). Recurses over the Tier-0..5 grammar plus `TIndex` of a const
 * aggregate at a const index (Layer 1c). An overflow, a ÷0, or an out-of-range cast on
 * a constant is a LOCATED COMPILE ERROR, never a silent wrap (M.1; LEGISLATION:148) —
 * a value the compiler can already see is out of range fails loudly at compile time.
 * The caller MUST have proven `is_const_expr(e) == null` first; a non-const form here
 * is an internal-error located at `e`.
 *
 * @param e      the typed, provably-const expression to evaluate
 * @param table  the type table (to classify a callee / resolve a referenced type)
 * @param env    the environment resolving a name to a const/binding + its value
 * @return       the computed comptime value, or a located error (overflow / ÷0 /
 *               out-of-range / a non-const form reaching this evaluator)
 * @since #comptime-fold
 */
fn eval_const(e: TExpr, table: TypeTable, env: Env) -> ConstValue | error

/**
 * literal_of — reconstruct a literal TExpr from a computed ConstValue, typed as `ty`
 * and positioned at (`line`, `col`) — the inverse of `eval_const`, used by `fold_expr`
 * to splice the folded literal back into the typed AST in place of the original
 * subtree. A CVInt becomes a `TNumber` (its decimal/hex text re-derived from `bits`
 * under `ty`); a CVBytes becomes a `TStrLit`; a CVAgg becomes a `TArrayLit` /
 * `TStructInit` of `literal_of` over its children. The produced node is
 * indistinguishable to lowering from a hand-written literal — zero runtime.
 *
 * @param v     the computed comptime value
 * @param ty    the type the fold site resolved (the reconstructed literal's `.type`)
 * @param line  the original expression's source line (diagnostics/position)
 * @param col   the original expression's source column
 * @return      a literal TExpr carrying `v`, typed `ty`
 * @since #comptime-fold
 */
fn literal_of(v: ConstValue, ty: Type, line: u32, col: u32) -> TExpr
```

### 4.2 The fold driver (Layer 1) + the comptime formatter (Layer 2)

```teko
/**
 * fold_expr — the Layer-1 fold: if `e` is PROVABLY const (`is_const_expr(e) == null`),
 * evaluate it and splice in the resulting literal (`literal_of(eval_const(e))`);
 * otherwise recurse into every child, folding each, and rebuild `e` around the folded
 * children (so a partially-const tree folds its const sub-trees). TOTAL over
 * `TExprKind`, mirroring `inline_rw_expr` (`consteval.tks:177`) — which it EXTENDS:
 * `inline_rw_expr` substitutes references but leaves the op-tree for the C compiler;
 * `fold_expr` evaluates the op-tree so the VM also gets the literal (zero runtime on
 * BOTH engines — the owner's mandate).
 *
 * @param e      the typed expression to fold
 * @param table  the type table (forwarded to the const oracle + evaluator)
 * @param env    the environment (module consts + provably-const locals, Layer 1b)
 * @return       the folded expression (a literal where provably const), or a located
 *               error propagated from `eval_const` (overflow / ÷0 / out-of-range)
 * @since #comptime-fold
 */
fn fold_expr(e: TExpr, table: TypeTable, env: Env) -> TExpr | error

/**
 * comptime_format — the Layer-2 comptime formatter: render `v` under `sp` exactly as
 * the runtime formatter in `teko_rt.c` would, producing the SAME bytes. Mirrors the
 * spec-family dispatch of `codegen.tks:emit_interp` (`:3022`) and
 * `fmt_family_takes_prec` (`:3016`): `x`/`X` → hex (`tk_fmt_x_lower/_upper`), `b` →
 * binary (`tk_fmt_b`), `d`/`f`/`e`/`g`/`p`/`n` → their families, `TFSpecNone` → the
 * natural form (`tk_u64_to_str`/`tk_i64_to_str`/`tk_ftoa`/bool text). This function is
 * the DUPLICATION risk (§10): its output MUST be byte-identical to `teko_rt.c` for
 * every (value, spec) pair, PINNED by the format-oracle differential fixture (§8, C5).
 *
 * @param v        the const hole value (from `eval_const`)
 * @param sp       the hole's format spec (`TFSpecNone` or `TFSpecStatic`)
 * @param hole_ty  the hole's resolved type (selects signed/unsigned/float family)
 * @return         the formatted bytes (byte-identical to the runtime), or a located
 *                 error for an unrecognized spec (mirrors emit_interp's error arm)
 * @since #comptime-fold
 */
fn comptime_format(v: ConstValue, sp: TFSpec, hole_ty: Type) -> []byte | error

/**
 * fold_interp — the Layer-2 interpolation fold: when EVERY hole of `in` is a const
 * literal (post Layer-1 fold) and EVERY spec is `TFSpecNone`/`TFSpecStatic`, format
 * each hole via `comptime_format`, concatenate with the piece literals, and return a
 * single `TStrLit` (interned to rodata by lowering, like any string literal) — zero
 * runtime concat/format. If ANY hole is runtime or ANY spec is `TFSpecDynamic` with a
 * runtime arg, `in` is returned UNCHANGED (still a runtime `tk_str_concat` shape),
 * preserving today's behavior for the non-const case.
 *
 * @param in     the typed interpolation (holes already Layer-1 folded)
 * @param e      the enclosing expression (its type/position for the produced TStrLit)
 * @param table  the type table (forwarded to the per-hole evaluator)
 * @param env    the environment (module consts + provably-const locals)
 * @return       a single folded `TStrLit` when fully const, else `in` unchanged; or a
 *               located error propagated from `comptime_format`
 * @since #comptime-fold
 */
fn fold_interp(in: TInterp, e: TExpr, table: TypeTable, env: Env) -> TExpr | error
```

### 4.3 Existing fns this touches (extend, do not rewrite)

- `src/checker/consteval.tks:522` `inline_consts` (pub) — the pass gains a `fold_expr`
  application after the existing reference-substitution, or `inline_rw_expr` calls into
  `fold_expr` at each node. **[integrator-pinned]** Cleanest: keep `inline_rw_expr` as
  the reference-substitution walk, then compose `fold_expr` over its result (two clear
  passes: substitute, then fold), so each stays single-responsibility and independently
  gate-able. `inline_rw_interp` (`:323`) delegates its final step to `fold_interp`.
- `build_scalar_map` / the map-building in `consteval.tks` — Layer 1b extends the
  seeding to include provably-const LOCAL bindings per function (a new per-fn pre-pass;
  do NOT globalize local names — key by (fn, binding) scope).
- `src/checker/consteval_form.tks:284` `is_const_expr` — **unchanged** as the oracle;
  Layer 1b needs it to see local const bindings, which flows through `env`, not through
  a change to the gate.
- Lowering/backends — **untouched**. A folded literal is an ordinary literal; a folded
  `TStrLit` interns rodata exactly as string literals already do (the "backend
  untouched" property inherited from `const-module-level-plan.md` §5).

---

## 5. Interaction with const-eval + AL0 Tier-6

**Same machine? Yes — this is the general form of const-eval.** `is_const_expr`
(grammar) and `inline_consts` (propagation) already exist; this design adds the missing
*evaluator* and *format folder* on top of them. The three modules compose:
`consteval_form.tks` says *whether* a subtree is const, `comptime_fold.tks` says *what
value* it has, `consteval.tks` *places* the result. There is one const machine, now
complete.

**AL0 Tier-6 (`TIndex` of a const aggregate at a const index — `GZIP_MAGIC[0]`) IS a
subcase of Layer 1c.** `eval_const` over `TIndex{const-aggregate, const-index}` → the
element is exactly what AL0 Tier-6 asks for, and the general folder subsumes it (plus
the AL0 "str→[]byte in const position" is a CVBytes coercion the evaluator handles
naturally). **BUT** this work is ORTHOGONAL to the AL wave and must not touch AL files.
Coordination, not co-option:

> **[integrator-pinned, veto open]** Do NOT double-implement `TIndex`-of-const. Two
> clean options for the owner/integrator to pick (§11 Q2): (a) AL0 Tier-6 defers to
> this general folder (AL0 keeps its two Huffman *generators* as-is per
> `al-wave-crumbs.md:104`, and its three fold targets — `gzip_header`, `wasm_preamble`,
> `wasm_narrow_msg_bytes` — resolve once Layer 1c lands); or (b) this design's Layer 1c
> waits until AL0 Tier-6 lands and reuses it. Recommendation: (a) — the general folder
> is the honest home for `TIndex`-of-const, and AL0's own note calls Tier-6 "um crumb
> próprio, não um sweep" with "ganho de perf ~zero," so the value is honesty/W15, which
> the general folder delivers for the whole corpus, not just AL0's 3 sites. Whichever
> is chosen, the OTHER lane drops its copy — flagged so no double-ship.

---

## 6. Surface (Teko A/B) — what folds, what does not

One case per block; the boundary is `is_const_expr` (provably const) vs any runtime leaf.

### 6.1 FOLDS (the mandate)

```teko
// A — module const scalar, expression folded (Layer 1a). VM previously executed `~`.
const MASK_ALL_U64: u64 = ~(0 to u64)      // → literal 0xFFFFFFFFFFFFFFFF at every use
let m = x & MASK_ALL_U64                    // → `x & 0xFFFFFFFFFFFFFFFF` (VM: no NOT op)

// B — local let, provably const, propagated + folded (Layer 1b). The canonical case.
let a: u64 = 0xFF
let hi = a >> 4                             // → literal 0x0F  (both engines, zero runtime)

// C — interpolation + static spec folded to a rodata literal (Layer 2). Canonical.
let a: u64 = 0xFF
let b = $"{a:X}"                            // → TStrLit "FF"  (no tk_str_concat, no tk_fmt_x_upper)

// D — TIndex of a const aggregate at a const index (Layer 1c; = AL0 Tier-6 subcase).
const GZIP_MAGIC: []byte = [0x1F to byte, 0x8B to byte]
let first = GZIP_MAGIC[0]                   // → literal 0x1F
```

### 6.2 DOES NOT FOLD (the boundary — stays runtime, behavior identical to today)

```teko
// E — binding holds a RUNTIME value (DECISION_LOG:391 preserved). Even spelled const.
const line = read_line(stdin)               // runtime; `is_const_expr` fails → not folded
let msg = $"{line:X}"                        // stays runtime tk_str_concat + tk_fmt_x_upper

// F — any runtime leaf poisons the subtree.
fn f(x: u64) -> u64 { x & MASK_ALL_U64 }    // MASK folds to a literal; `x & lit` stays runtime (x is runtime)

// G — dynamic spec with runtime args stays runtime (Layer-2 boundary; see §11 Q3).
let w = user_width()
let s = $"{a:[w]}"                           // runtime tk_fmt_dyn_* (w is runtime)

// H — a call outside the closed allowlist is not const (until/unless Layer 3 const fn).
let z = compute(a)                           // ordinary fn → not const → not folded
```

### 6.3 Error / behavior forms

- The fold **never** changes observable output for E–H — they lower exactly as today.
- The fold **never** adds a new rejection to A–D **except** the M.1 compile-error on a
  provably-const overflow/÷0/out-of-range (§3.4), e.g. `const bad: u8 = 300` was already
  a compile error; `const bad: u8 = 200 + 100` now is too (loud, at `file:line:col`),
  which is the correct M.1 outcome. Error shape reuses `const_form_error`
  (`consteval_form.tks:20`) positioning.

---

## 7. Crumb sequence (honest sizes, gate-able steps)

Lane suggestion: `feat/comptime-fold` off the current backend base. Each crumb is
independently gate-able; ritual points (full gate) marked **[RITUAL]**.

| # | Crumb | Size | Ritual of proof |
|---|---|---|---|
| **CF1** | `comptime_fold.tks` skeleton: `ConstValue` domain + `eval_const` for Tier 0–2 scalars (literals, cast, unary `~`/`-`, binary `+ - * / % & | ^ << >>`) with overflow/÷0/out-of-range → located compile error. Pure; no wiring yet. | **M** | `comptime_fold_test.tkt`: unit-assert `eval_const` on a value matrix (each int family min/max/wrap-edge, ÷0, out-of-range cast → error). Build green. |
| **CF2** | `literal_of` + `fold_expr` (Tier 0–5 driver) wired into `inline_consts` AFTER reference-substitution. Module consts now fold their op-trees to literals. | **S/M** | **[RITUAL]** fixpoint gen2==gen3 GREEN vs **new** golden (fold changes emitted bytes — §9); VM==native differential on a folding corpus; 100% coverage of the CF1/CF2 delta. |
| **CF3** | Layer 1b: per-fn pre-pass seeding provably-const LOCAL `let`/`const` bindings into the fold env; guard skips any runtime-valued binding (DECISION_LOG:391). | **M** | **[RITUAL]** fixpoint GREEN vs new golden; fixtures E/F (runtime binding NOT folded) exit-code identical VM+native; boundary coverage. |
| **CF4** | Layer 1c: `eval_const` handles `TIndex` of const aggregate + const index (+ str→[]byte coercion). **Coordinate with AL0 Tier-6 — do not double-ship (§5).** | **S** | fixture D folds; `TIndex` with runtime index/receiver stays runtime; VM==native. |
| **CF5** | Layer 2: `comptime_format` (mirror `teko_rt.c`) + `fold_interp`; `inline_rw_interp` delegates its final step. + the **format-oracle** differential fixture. | **M** | **[RITUAL]** fixpoint GREEN vs new golden; **format-oracle exhaustive fixture** (§8) byte-identical; VM==native on `$"{a:X}"`-class programs; coverage. |
| **CF6** | Metric probe wiring: emit the runtime-ops-eliminated count via the arena-observability probe (reuse AL1's dark-matter table) for the acceptance report. | **S** | Report: concat/format/alloc ops eliminated (§9), no behavior change. |
| — | **Layer 3 (const fn / comptime loops / recursion)** — NOT scheduled here. Separately ratifiable per §11 Q1. | **L+** | Deferred; M.5-staged. |

**Blocked-by / design-ahead status.** Nothing here is blocked by an open dependency —
the substrate (`is_const_expr`, `inline_consts`, `TInterp`, rodata interning) all
exist today. CF1–CF6 compile against the CURRENT tree. The single external coordination
is AL0 Tier-6 (§5) — a de-dup handshake, not a blocker; CF4 can proceed either as the
general home (AL0 defers) or wait (AL0 lands first), and CF1–CF3, CF5 do not depend on
it. **Everything in this plan is buildable now.**

---

## 8. Regression fixtures (inputs → expected exit codes, VM and native)

Placed as `.tkt` co-located suites plus end-to-end `.tkp` programs run on BOTH engines
for exit-code parity. The differential principle: a folded program and its unfolded
twin produce **identical observable output** (the fold is behavior-preserving), while
the arena probe shows the runtime ops gone.

| Fixture | Where | Input | Expected (VM == native) |
|---|---|---|---|
| fold-scalar | `src/checker/comptime_fold_test.tkt` | `const M: u64 = ~(0 to u64); use M & x` | `eval_const(M)==0xFFF…F`; program exit 0; folded literal present |
| fold-overflow-const | `comptime_fold_test.tkt` | `const B: u8 = 200 + 100` | **compile error** at `file:line:col` (M.1); exit ≠ 0, both paths reject |
| fold-div0-const | `comptime_fold_test.tkt` | `const D: u64 = 1 / (1 - 1)` | **compile error** (÷0 seen at comptime); exit ≠ 0 |
| fold-local-let | end-to-end `.tkp` | `let a: u64 = 0xFF; let hi = a >> 4; print(hi)` | prints `15`; folded literal; exit 0 (VM==native) |
| fold-interp-hex | end-to-end `.tkp` | `let a: u64 = 0xFF; print($"{a:X}")` | prints `FF`; emitted `TStrLit`, **no** `tk_str_concat`/`tk_fmt_x_upper` in output; exit 0 |
| fold-tindex-const | end-to-end `.tkp` | `const G: []byte = [0x1F to byte, 0x8B to byte]; print(G[0] to u64)` | prints `31`; folded literal; exit 0 |
| noflod-runtime-bind | end-to-end `.tkp` | `const line = read_line(stdin); print($"{line:X}")` (E) | stays runtime; output identical to today; exit 0 |
| noflod-runtime-hole | end-to-end `.tkp` | `fn f(x: u64) -> u64 { x & M }` (F) | `x & <literal>` remains; exit 0; behavior identical |
| noflod-dynamic-spec | end-to-end `.tkp` | `let w = user_width(); $"{a:[w]}"` (G) | runtime `tk_fmt_dyn_*`; exit 0 |
| **format-oracle** | `src/checker/comptime_fold_test.tkt` (differential) | for every (value ∈ matrix, spec ∈ {none,`X`,`x`,`b`,`d`,`f2`,`e`,`g`,`p`,`n`}) : assert `comptime_format(v,spec) == <runtime tk_fmt_*(v,spec)>` | **byte-identical** for every pair; ANY divergence fails the crumb — the M.1 discharge for §10 |

The **format-oracle** fixture is the load-bearing M.1 proof for Layer 2: it pins the
comptime formatter to `teko_rt.c` across an exhaustive input matrix (every integer
family min/0/max/edge, representative floats incl. subnormals/`inf`/`nan`-origin
guards, every static spec family + width/precision variants). It must run on the same
CI gate that guards `teko_rt.c`, so a change to either side that diverges fails.

---

## 9. Ritual + how the gain is measured

### 9.1 The fixpoint subtlety (state it plainly)

The fold **changes the emitted bytes for the better** (a runtime concat/format
op-tree becomes a rodata literal / an immediate). So the classic "byte-identical vs the
OLD golden" does NOT hold and MUST NOT be expected — that would be a regression signal
against the wrong baseline.

**The ritual is byte-identity against a NEW golden**, in three parts, at each
**[RITUAL]** crumb (CF2, CF3, CF5):

1. **Re-golden**: capture the folded output as the new golden; review the diff is
   *only* runtime-op-elimination (concat/format/op-tree → literal), never an observable
   change.
2. **Fixpoint gen2==gen3 GREEN against the new golden**: the compiler, compiled by
   itself, still reaches a fixed point — self-stability preserved *with* the fold on.
3. **VM==native differential**: the folded program's observable output (exit code +
   printed bytes) is identical on both engines, and identical to the unfolded twin.

### 9.2 The gain metric (owner's metric — ops, not peak bytes)

**[owner 2026-07-19]** the metric is **runtime operations eliminated** (accesses /
reallocs / concats / formats), NOT peak-memory bytes. Wire CF6 to the
arena-observability probe already built by AL1 (`al1-proof-report.md` dark-matter
table): for the fold corpus, report the count of eliminated

- `tk_str_concat` calls (interp fold),
- `tk_fmt_*` / `tk_u64_to_str` / `tk_ftoa` calls (format fold),
- intermediate str allocations (each folded interp removes its temporaries),
- VM op-tree evaluations (each folded scalar removes its `~`/`&`/`<<` executions per
  execution — the VM-side win the C-compiler delegation never captured).

Canonical example accounting: `$"{a:X}"` folded removes **2 `tk_str_concat` + 1
`tk_fmt_x_upper` + their intermediate str allocs, per execution** → for a value in a hot
loop, the count scales with iterations. Report per-corpus totals; that count is the
acceptance evidence, alongside the green ritual.

---

## 10. Risks + law tensions (with recommended resolution)

**R1 — Layer-2 formatter duplication vs `teko_rt.c` (M.1 determinism). THE central
risk.** The comptime formatter re-implements logic that today lives once in the
maintained-C runtime. If the two ever diverge by a byte, M.1 (deterministic formatting)
is violated and VM/native/comptime disagree.
- *Resolution (recommended, Option A):* keep the comptime formatter **self-contained in
  Teko** and **pin it to `teko_rt.c` with the exhaustive format-oracle fixture** (§8),
  on the same CI gate that guards the runtime. M.4 favors this: a self-contained
  formatter survives regardless of Layer 3. **[integrator-pinned, veto open].**
- *Alternative (Option B, later):* eliminate the duplication by making the formatters
  single-source — a Teko `teko::fmt` that BOTH the runtime lowering and the comptime
  fold use. True non-duplication requires the comptime fold to *execute* that Teko
  formatter at compile time → **that is Layer 3** (const-fn execution). So Option B
  *couples Layer 2 to Layer 3* and must wait for it. Surfaced as §11 Q1/Q2.
- *Verdict:* **no law tension** — M.1 is dischargeable by the oracle now (Option A);
  Option B is a future convergence, not a blocker. M.4 actively favors A.

**R2 — Overflow/÷0 in a const fold as a compile error.** Could a corpus site rely on a
provably-const expression *wrapping at runtime* in release? Then the fold's compile
error (§3.4) is a new rejection. *Resolution:* this is the correct M.1 outcome
(LEGISLATION:148 — constants out of range are a compile error), and the fixpoint delta
catches any site; none is expected. **No tension** — the fold *lights* an existing M.1
rule. **[integrator-pinned, veto open].**

**R3 — Local-binding propagation changing `let`/`const` semantics.** DECISION_LOG:391
guarantees a local const/let MAY hold a runtime value. *Resolution:* Layer 1b folds
ONLY the provably-const subset (`is_const_expr == null` in the local env); a
runtime-valued binding is untouched (§3.3, fixtures E/F). **No tension** — the fold is
an optimization over a subset, not a semantic change.

**R4 — AL0 Tier-6 overlap (coordination, not law).** `TIndex`-of-const is a subcase of
Layer 1c AND an AL0 target. *Resolution:* de-dup handshake (§5, §11 Q2); do not touch
AL files. **No tension.**

**R5 — Fixpoint re-golden mistaken for regression.** The fold changes bytes; a reviewer
expecting the old golden reads it as a regression. *Resolution:* §9.1 makes the
new-golden ritual explicit; the diff review asserts *only* runtime-op-elimination.

**No item HALTs.** Every tension resolves law-first (M.3/M.0/M.1 mandate the fold; M.5
out of jurisdiction for Layers 1–2, governing only Layer 3's scope; M.4 favors the
standalone formatter; M.1 dischargeable by the oracle).

---

## 11. Open questions for the owner (each WITH a recommendation)

**Q1 — Layer 3 scope (const fn / comptime loops / recursion). The M.5 decision.**
The mandate (Layers 1–2) does not need Layer 3. Layer 3 is a large capability (an
AST-executing comptime interpreter with budgets + a purity boundary) governed by M.5's
burden-of-proof.
> **Recommendation:** do **not** adopt Zig-style arbitrary comptime wholesale. Ratify
> Layers 1–2 as the mandate now. Treat Layer 3 as a **separately ratifiable
> capability**, and when it enters, stage the **minimal pure slice first** — pure
> `const fn` over the const-expr value domain, bounded (step + recursion budget), no
> IO, no allocation-with-runtime-identity — justified by a concrete corpus need. The
> strongest first need is **unifying the Layer-2 formatter** (Q2/Option B). Everything
> beyond the pure-bounded slice (comptime IO, comptime type construction, generics-at-
> comptime) stays in the audited vacuum until a concrete case pays for it (M.5).

**Q2 — Layer 2 formatter: ship the oracle-pinned duplicate now (A), or wait for a
minimal Layer 3 and single-source it (B)?**
> **Recommendation: A now, B as the convergence.** Ship Layer 2 with the self-contained
> comptime formatter + format-oracle fixture (delivers the canonical `$"{a:X}"` win
> immediately; M.4-clean; M.1 discharged by the oracle). When/if the minimal Layer 3
> lands (Q1), fold the formatter into a single Teko `teko::fmt` used by both runtime
> lowering and comptime, retiring the duplicate. This sequences the win early without
> coupling it to the unbuilt Layer 3 (M.4).

**Q3 — Does the mandate include folding a `TFSpecDynamic` whose args are ALSO const
(e.g. `$"{a:[4]}"` with a literal width), and folding provably-const bindings that are
LOCAL (the canonical `let a`)?**
> **Recommendation:** (i) LOCAL provably-const bindings: **yes, in the mandate** — the
> owner's own canonical example (`let a: u64 = 0xFF`) is a local `let`; without Layer 1b
> the example does not fold. I have scoped it as mandate. (ii) Const-arg dynamic specs:
> **fold them too, as a small Layer-2 extension** (a `:[const, const]` spec is just
> another provably-const input to `comptime_format`) — but it is not in the owner's
> example, so I default it ON with an easy OFF: if the owner prefers to keep Layer 2
> strictly to none/static specs for the first landing, drop the dynamic-const arm from
> CF5; the boundary (fixture G, runtime-arg dynamic) is unchanged either way.

---

## 12. Summary of what is buildable today (design-ahead)

- **Nothing is blocked.** `is_const_expr` (grammar oracle), `inline_consts`
  (propagation), `TInterp` + `TFSpec`, and rodata interning all exist. CF1–CF6 compile
  against the current tree.
- **New machine:** `src/checker/comptime_fold.tks` (`ConstValue`, `eval_const`,
  `literal_of`, `fold_expr`, `comptime_format`, `fold_interp`).
- **Extended (not rewritten):** `consteval.tks` (`inline_consts`, map-building for
  Layer 1b, `inline_rw_interp` delegates to `fold_interp`). `consteval_form.tks` and
  all backends **unchanged**.
- **One external handshake:** AL0 Tier-6 de-dup (§5, §11 Q2) — coordination, not a
  blocker.
- **Owner decisions pending:** the three questions in §11 (Layer-3 scope, formatter
  A/B, dynamic-const-spec + confirm local-binding mandate). Layers 1–2 are ready to
  ratify and sequence as the mandate the moment those land.
```
