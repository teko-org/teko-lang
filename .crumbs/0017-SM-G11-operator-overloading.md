---
seq: 0017
crumb-id: SM-G11
milestone: M1
gate: "[dry]"
reseed-class: "(folds R1)"
deps: []
sources:
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1015-1078"# §7c.2 operator overloading
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1086-1088"# §7c.3 G11 crumb
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1111"     # §9.1 byte-preserving until used
---

# 0017 · SM-G11 — operator overloading (dunder map + derived `!=`/`>`/`<=`/`>=`)

> Operator overloading (dunder map + derived `!=`/`>`/`<=`/`>=`).

## Goal

Customize operator BEHAVIOR for user types via a DUNDER-METHOD convention (consistent with SM-G5's
`__wrap`/`__unwrap` and the DI `ctor` convention). When an operand's type defines the dunder, the
operator desugars to the method call; primitives keep the builtin path UNTOUCHED. Explicitly NO
C#-style `implicit`/`explicit` CONVERSION operators — conversion stays `to` (a separate axis); `a + b`
on user types calls a user method, never converts `a` to `b`'s type. A type defining `__eq` + `__lt`
gets `!=`/`>`/`<=`/`>=` DERIVED automatically (one source of truth). The prim/prim path is guarded
(never looks up a dunder), so the hot arithmetic path is byte-identical. INERT until a user type defines
a dunder. Its seed folds into SM-R1.

## Where

- `src/checker/typer.tks:278-479` — `check_binary`/`check_unary`/`check_compare` regimes — add ONE branch
  at the FRONT of each operator's typing: if a user-typed operand's type defines the matching dunder,
  REWRITE the operator node to a method call and type THAT; else fall through to the existing builtin
  path (byte-identical for primitives).
- `src/checker/typer.tks:280` — the binary concat `~` builtin — UNCHANGED (a user type wanting
  concatenation defines a named method, not an overload of the string operator).
- The operator→dunder map — NEW (a checker table).
- Lowering: `a.__add(b)` is an ordinary method call; if `R` is an aggregate it returns via DPS (SM-A2) —
  no new lowering.

## How

1. **Add the operator→dunder map** (§7c.2):

   | operator | dunder | signature shape |
   |---|---|---|
   | `a + b` | `__add` | `fn __add(self, rhs: T): R` |
   | `a - b` | `__sub` | `fn __sub(self, rhs: T): R` |
   | `a * b` | `__mul` | `fn __mul(self, rhs: T): R` |
   | `a / b` | `__div` | `fn __div(self, rhs: T): R` |
   | `a % b` | `__mod` | `fn __mod(self, rhs: T): R` |
   | `a == b` | `__eq` | `fn __eq(self, rhs: T): bool` |
   | `a < b` | `__lt` | `fn __lt(self, rhs: T): bool` |
   | `-a` | `__neg` | `fn __neg(self): R` (unary) |
   | `~a` | `__not` | `fn __not(self): R` (unary bitwise NOT; distinct from binary `~` concat) |
   | `a[i]` | `__index` | `fn __index(self, i: I): R` |
   | `a & b` / `a \| b` / `a ^ b` | `__band`/`__bor`/`__bxor` | bitwise (OPT-IN) |

2. **Add the dunder-lookup branch** at the front of each operator's typing:

```teko
/**
 * try_operator_dunder — the desugar front-branch of an operator's typing: if a user-typed operand's type
 * defines the matching dunder (a method lookup on the type), REWRITE the operator node to a method call
 * (`a.__add(b)`) and return that for typing; else return null and the existing builtin path handles it
 * (byte-identical for primitives). Guard: BOTH operands primitive → never look up a dunder (the hot
 * arithmetic path is unconditional prim/prim). Because it rewrites to an ordinary method call, overload
 * resolution (SM-G10), generics, DPS, and mangling all apply with zero extra machinery.
 *
 * @param op     the operator being typed
 * @param lhs    the left operand's typed expression
 * @param rhs    the right operand's typed expression (or null for a unary operator)
 * @return       the rewritten method-call node, or null to fall through to the builtin path
 * @since 0.3.1
 */
fn try_operator_dunder(op: BinOp, lhs: TExpr, rhs: TExpr | null): TExpr | null
```

3. **Comparison derivation (automatic).** A user type defines `__eq` + `__lt` ONLY; the compiler DERIVES
   `!=` (¬`__eq`), `>` (rhs `__lt` self), `<=` (¬(rhs `__lt` self)), `>=` (¬`__lt`). A type may define
   `__eq` without `__lt` (equality only, no ordering — `<`/`>`/`<=`/`>=` then a clear "type T defines no
   ordering" error, while `==`/`!=` work). A type defining neither has no comparison operators (builtin
   path stays for primitives).
4. **Which operators (decided):** overloadable = arithmetic (`+ - * / %`), comparison (`== <` → derived),
   unary `-`/`~`, index `[]`, bitwise (`& | ^`, opt-in). NOT overloadable, and why: `=` (storage model),
   `.`/`::` (resolution not computation), `&&`/`||` (overloading forces EAGER eval, destroying
   short-circuit — a correctness trap, M.3), binary `~` concat (a user type defines a named method).
   Range/`in`/`to` are cast/membership (conversion stays `to`).
5. **Lowering (none new):** `a.__add(b)` is an ordinary method call; an aggregate `R` returns via DPS
   (SM-A2) into the caller's arena. Comparison dunders return `bool` (scalar, no DPS).
6. **Confirm byte-neutrality.** The prim/prim guard keeps `i64 + i64` on the builtin path (never a dunder
   lookup); the desugar is inert until a user type defines a dunder — `src/` byte-identical.

## Rulings & laws

- **Teko-only:** checker `.tks`; no C twin.
- **W15 full Javadoc** on `try_operator_dunder` and helpers; no `//`.
- **NO conversion operators (owner):** conversion stays `to`; operator overloading adds no coercion path.
- **`&&`/`||` NOT overloadable (M.3):** overloading them destroys short-circuit — a correctness trap.
- **Prim/prim guard:** the hot arithmetic path is unconditional and byte-identical.
- **Byte-preserving until used (§9.1):** inert until a type defines a dunder.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step.

## Fixtures

`src/` defines no dunders (adoption is SM-S7), so the desugar, derivation, and short-circuit reject are
NOT self-exercised — isolated oracles required:

| fixture | asserts | expected |
|---|---|---|
| `op_overload_add_aggregate` | `a + b` on a user type calls `__add`; aggregate result via DPS | 0 |
| `op_overload_cmp_derived` | defining `__eq`+`__lt` makes `!=`/`>`/`<=`/`>=` work (derived) | 0 |
| `op_no_overload_shortcircuit` | `&&`/`\|\|` cannot be overloaded (short-circuit preserved) | EXPECT_COMPILE_FAIL |
| `op_prim_path_unchanged` | `i64 + i64` never looks up a dunder (builtin path byte-identical) | 0 |

## Gate

`[dry]` — compile + the four fixtures + fixpoint (byte-identical; prim/prim guarded, no `src/` dunders).
"Green" = user-type operators desugar to dunders, comparison derives from `__eq`/`__lt`, `&&`/`||` reject,
the prim path is byte-identical, `[dry]` build byte-identical. Reseed-class: `(folds R1)`.

## Deps

`—` (composes with SM-G10 overload resolution and SM-A2 DPS, but neither is a build dependency — the
desugar rewrites to an ordinary method call that those handle).

## Done when

Each overloadable operator desugars to its dunder when a user-typed operand defines it (prim/prim guarded
byte-identical), `__eq`+`__lt` derive the remaining comparisons, `&&`/`||` are rejected as
non-overloadable, the four fixtures pass, and a `[dry]` build is byte-identical.
