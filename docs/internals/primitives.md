# A primitive with members

`TimeSpan` is not a class. It has no row in [`teko_struct.tk`](../../teko_struct.tk)'s type
table, so it has no field, no vtable, no constructor and no method — it is one
`type_new("TimeSpan", 8, 8, TK_SINT)`, eight bytes the core moves like an `i64`. What gives
it `.Ticks`, `.Days`, `TimeSpan.FromHours(x)` and `a + b` is a **lowering table**:
[`teko_prim.tk`](../../teko_prim.tk) holds the rows, [`teko_time.tk`](../../teko_time.tk)
registers them, and four sites that already existed read them.

This page is the mechanism. The surface it produces is
[timespan.md](../reference/timespan.md); the design it comes from is
[`docs/specs/datetime.md`](../specs/datetime.md) § 2, and `DateTime`, `decimal`, `Guid` and
the `enum` statics are meant to land on the same table without a line of their own here.

---

## The two tables

A **member row** is `(type id, member name, kind, parameter type, symbol, result type)`:

| kind | reached as | example |
|---|---|---|
| `TK_PMSVAL` | `Type.Name` | `TimeSpan.Zero` |
| `TK_PMSFUN` | `Type.Name(arg)` | `TimeSpan.FromHours(2.0)` |
| `TK_PMPROP` | `x.Name` | `t.Days` |
| `TK_PMFUN` | `x.Name(arg)` | `t.CompareTo(u)` |
| `TK_PMCTOR` | `new Type(arg)` | `new TimeSpan(864000000000)` |

An **operator row** is `(token, arity, left type, right type, symbol, result type, swap)`.
`swap` is C#'s reversed declaration — `n * t` and `t * n` are one function, called with the
arguments in the row's own order.

Every row's parameter and result type is a real type id, which is what keeps the mechanism
**statically typed**: the oracle answers `i64` for `t.Days` and `TimeSpan` for `a - b`
without a run-time tag anywhere.

## The two casts

Every symbol a row names lives in [`lib/time.tk`](../../lib/time.tk) and is written over the
RAW ticks — `i64` in, `i64` out. The compiler writes the crossing:

```
t.Days                 ->  tk_ts_days((i64) t)
t.Duration()           ->  (TimeSpan) tk_ts_duration((i64) t)
TimeSpan.FromHours(2)  ->  (TimeSpan) tk_ts_from_hours((f64) 2)
a - b                  ->  (TimeSpan) tk_ts_sub((i64) a, (i64) b)
t.Ticks                ->  (i64) t                    a row with no symbol: the identity
new TimeSpan(n)        ->  (TimeSpan) n               the same, the other way
```

Neither cast is an instruction: both slots are eight bytes, and `walk_narrow`
(`mc`'s own `mc/src/gen_walk.mc`) answers 0 for a width-8 `TK_SINT`. Three things follow, and all
three are why the design is this and not a compiler intrinsic:

- **`lib/time.tk` never names the type.** It is ordinary teko over `i64`, so nothing in it
  can recurse into the very operator it implements.
- **Every existing check stays honest.** The argument
  [`teko_rc.tk`](../../teko_rc.tk)'s pass sees is an `i64` because it IS an `i64` by then,
  and the result the oracle sees is a `TimeSpan` because the cast says so.
- **No new intrinsic and no new pass** (D2/D21): `mc limits` shows `passes 15/30` and
  `intrin 8/16` unmoved.

## The four sites that read the table

| site | file | what it answers |
|---|---|---|
| the type word in expression/statement position | `teko_prim.tk`'s `tk_prim_expr`/`tk_prim_stmt`, registered by `teko_time.tk` through `syntax_expr`/`syntax_stmt` | `TimeSpan.Zero`, `TimeSpan.FromDays(1.0)`, and `TimeSpan t = …;` (which is the core's own `parse_var`) |
| `new Name(args)` | [`teko_expr.tk`](../../teko_expr.tk)'s `tk_new` | the value constructor, before the struct/class lookup fails |
| `.` on a receiver the PARSER can type | `teko_expr.tk`'s `tk_dot`, through `tk_pty_of` | a local, a cast, a call, a field load |
| `.` on a receiver only the PASS can type | [`teko_typeof.tk`](../../teko_typeof.tk)'s `tk_pend_do` | a parameter, and any receiver built by another deferred `.` — the deferred road that already existed, at the exact line where `tk_reject_scalar_member` used to answer |
| a binary or unary operand | [`teko_ops.tk`](../../teko_ops.tk)'s pass | the operator rows, claimed before anything else looks at the node |

Two smaller readers came with the crumb, each one line:
`tk_check_scalar_compat` (teko_typeof.tk) refuses every conversion in both directions, and
`tk_ty_binary` (same file) asks the operator table for the type of a binary — the core's
own "a binary is typed from its LEFT operand" is wrong for a reversed row, and `3 * hour`
is a `TimeSpan`.

## The P0 probes, and what they measured

Four questions were answered on the tree before a line of `TimeSpan` was written, with
throwaway programs under `build/` (not fixtures). Measured on `03189d92` +
the registration alone, macOS/aarch64, `mc` 0.15.23.

| # | question | measured |
|---|---|---|
| 1 | is a `type_new(name, 8, 8, TK_SINT)` word a type in every position, with no further code? | **yes** — a local, a global, a parameter, a return, a field, a local array element and a cast all work, and the signed `<`/`>=`/`==` the kind buys are correct on a negative value. `--dump-ast` prints the type **by name** (`VAR type=TimeSpan name=a`), which is also why registering it moves no existing dump |
| 2 | does `syntax_expr` on a `type_new` word reach the handler ahead of the core's "a type word is no expression"? | **yes** — `parse_primary` (`mc`'s own `mc/src/parse.mc`) consults the expression-syntax table before `type_of_token`, the same road a class name already takes. In STATEMENT position `syntax_stmt` wins over the core's declaration path (`parse_stmt_core`), so the handler must call `parse_var` itself with the type word **unread** |
| 3 | does the conversion clause keep `--dump-ast` byte-identical? | **yes** — all 47 pre-existing fixtures, byte for byte, before and after |
| 4 | what does the core do with `a - b`, `a + b` and `a * b` over two such values with NO operator claim? | it **compiles and runs them raw**: `mk(10) * mk(4)` answered 40. So the operator table must claim every binary and unary with a primitive operand and refuse the ones with no row — the spec's § 13 resolution, measured rather than assumed |
| 5 | how does a `.` on such a receiver reach the compiler today? | through the deferred road: `teko_expr.tk` defers it, the oracle types the receiver in the pass, and `tk_reject_scalar_member` answered `teko: TimeSpan has no members: Ticks`. That is the exact line the member lookup replaces |
| 6 | what does the registration cost in `mc limits`? | `types 9 → 10`, `alias 16 → 17` (a `type_new` moves both, D38's own finding), `syntax` unmoved at 15, `passes` 15, `intrin` 8. On the heaviest fixture (`tests/surface_enum.tk`, five type declarations of its own, single-file mode) `types` used goes 16 → 17 against a reserved 8 and the pre-existing `grew` verdict stays `grew` |

## The ceilings

| table | cap | counts |
|---|---|---|
| `TK_MAXPRIMT` | 8 | primitive types with a member table |
| `TK_MAXPRIMM` | 96 | member rows, over every primitive |
| `TK_MAXPRIMO` | 32 | operator rows, over every primitive |

`TimeSpan` uses 1, 30 and 12 of them.

## What a second primitive costs

`DateTime` (`docs/specs/datetime.md`'s C2) adds one `type_new`, one `syntax_expr`/
`syntax_stmt` pair, its rows, and the functions in `lib/time.tk` they name. Nothing in
`teko_prim.tk` changes, and no pass is added — the only genuinely new work is the calendar
itself, which is ordinary teko.
