# A primitive with members

`TimeSpan` is not a class. It has no row in [`teko_struct.tk`](../../teko_struct.tk)'s type
table, so it has no field, no vtable, no constructor and no method — it is one
`type_new("TimeSpan", 8, 8, TK_SINT)`, eight bytes the core moves like an `i64`. What gives
it `.Ticks`, `.Days`, `TimeSpan.FromHours(x)` and `a + b` is a **lowering table**:
[`teko_prim.tk`](../../teko_prim.tk) holds the rows, [`teko_time.tk`](../../teko_time.tk)
registers them, and four sites that already existed read them.

This page is the mechanism. The surface it produces is
[timespan.md](../reference/timespan.md) and [datetime.md](../reference/datetime.md); the
design it comes from is
[`docs/specs/datetime.md`](../specs/datetime.md) § 2, and `decimal`, `Guid` and
the `enum` statics are meant to land on the same table without a line of their own here.
`DateTime` was the second primitive and cost the mechanism three additions, each listed
below: rows of more than one argument, a row that is refused BY NAME, and the list that
tells a compiler-written cast from a hand-written one. `DateOnly` was the third and cost it
a WIDTH — four bytes, where the other two are eight. `TimeOnly` was the fourth and cost it
nothing at all — eight bytes again, and every trick the mechanism already had was enough.
Both are their own sections further down this page.

---

## The two tables

A **member row** is
`(type id, member name, kind, how many arguments, a head into the pool of per-position
argument types, symbol, result type)` — a position may carry a late type NAME (an id below
-1) until the site reads it:

| kind | reached as | example |
|---|---|---|
| `TK_PMSVAL` | `Type.Name` | `TimeSpan.Zero` |
| `TK_PMSFUN` | `Type.Name(args)` | `DateTime.DaysInMonth(2024, 2)` |
| `TK_PMPROP` | `x.Name` | `t.Days` |
| `TK_PMFUN` | `x.Name(args)` | `t.CompareTo(u)` |
| `TK_PMCTOR` | `new Type(args)` | `new DateTime(2024, 2, 29)` |
| `TK_PMSOON` | `Type.Name`, refused by name | `Guid.NewGuid` |

**The parameter list is a count and a HEAD into a pool of positions**, one column per
argument. It was a count and ONE type until N2c, because every row took its arguments in a
single type — three integers for `new DateTime(y, m, d)`, one float for
`TimeSpan.FromHours(x)` — and `new DateTime(ticks, kind)` is the first that does not: an
`i64` beside a `DateTimeKind`. `tk_prim_membern` still writes `np` positions of one type
for every other row, so nothing else moved, exactly as this page said it would not.

**A column may name a type that does not exist yet.** `DateTimeKind` is an `enum` declared
inside `lib/time.tk`, so its id is created when a program writes `#include "time.tk"`, long
after `tk_time_init()` wrote the rows. Such a column carries the NAME instead, encoded as
an id below -1, and `tk_prim_ty` reads the real id at the SITE, where the enum's own row of
the type table exists (`tk_prim_late`). A name still undeclared answers -1, which every
reader here already takes as "nothing known, refuse nothing" — and that site is the very
one `tk_prim_need_include` refuses for the missing include.

**Rows of one name and different counts are the overload set of that name.** `new
DateTime(...)` is five rows (1, 2, 3, 6 and 7 arguments) and the site picks by how many it
wrote; a count no row has is `teko: wrong number of arguments for new`.

**A `TK_PMSOON` row names a member the type HAS and this version does not teach.**
`Guid.NewGuid` needs an entropy source, teko's own `extern` to give
([not-yet.md](../reference/not-yet.md)), and the row is what makes the site say
`teko: Guid.NewGuid is not taught yet` instead of reading as an unknown member.
`DateTime.Now`/`UtcNow`/`Today` used to be `TK_PMSOON` rows too, for the same reason
(a wall clock, also teko's own); C6 (D90) moved them to ordinary `TK_PMSVAL` rows.

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

Neither cast is an instruction where the primitive is eight bytes wide: both slots are the
machine word, and `walk_narrow` (`mc`'s own `mc/src/gen_walk.mc`) answers 0 for a width-8
`TK_SINT`. Over a NARROWER one — `DateOnly`, four bytes (N4a) — they are a sign extension
in and a narrowing store out, the same pair an `enum : i32` already takes, and nothing else
about them changes. Three things follow, and all three are why the design is this and not a
compiler intrinsic:

- **`lib/time.tk` never names the type.** It is ordinary teko over `i64`, so nothing in it
  can recurse into the very operator it implements.
- **Every existing check stays honest.** The argument
  [`teko_rc.tk`](../../teko_rc.tk)'s pass sees is an `i64` because it IS an `i64` by then,
  and the result the oracle sees is a `TimeSpan` because the cast says so. The cast is
  therefore also what that pass can no longer judge the SOURCE's value by, which is why the
  argument check is the compiler's own and is deferred rather than skipped when the parser
  cannot type the value — the section below.
- **No new intrinsic and no new pass** (D2/D21): `mc limits` shows `passes 15/30` and
  `intrin 8/16` unmoved.

`DateTime` is the type that makes the pair load-bearing rather than tidy. Its raw eight
bytes are the ticks in bits 0..61 and the `Kind` in the two above them, so `(i64) d` is
**not** `d.Ticks` — for a `Local` date it is a negative number — and `(DateTime) n` is an
integer that skipped every range check. Both are therefore **refused in the surface**
(`docs/specs/datetime.md` § 3), and the compiler's own casts, which are the very same two
nodes, are told apart by a list of node indices: `tk_cast` (teko_array.tk) records the ones
it builds toward a primitive and `tk_prim_raw` the ones it builds away from one, and
`tk_prim_cast_check` refuses every other cast that touches a primitive. A MARK on the node
was not an option — `--dump-ast` prints every field a node has, so a mark would move the
dump of code that did not change.

Two details of that list are the whole of its subtlety. A node replaced **in place**
(`node_assign`, mc's own) keeps the placeholder's index and not the built node's, so every
site that replaces a value node hands the record over with `tk_prim_own_cast_moved`. And
the check runs over the operator pass's walk, which covers function bodies only, so a
second tiny walk (`tk_prim_cast_globals`) covers what is written outside them: a global's
own initializer.

**That hand-over is a rule for every in-place replacement, not for two of them** (D54).
While every primitive was eight bytes wide the only cast a replaced node could carry was a
lowering's own result, so the deferred `.` (`tk_pend_do`) and the operator rewrite
(`tk_ops_replace`) were the whole list. A primitive **narrower** than the machine word
makes every indirect load a cast as well — `tk_ld` (teko_struct.tk), `tk_arr_load` and
`tk_callp_ret` (teko_array.tk) all write one — and six more doors turned out to copy such
a value into a placeholder and drop the record, each of them refusing a `DateOnly` the
compiler had just built itself: the implicit-`this` rewrite (teko_this.tk), a `ref`/`out`
pointee read (teko_ref.tk), the two global-array element reads (teko_array.tk), the
delegate call (teko_deleg.tk) and the forward-resolved static field read (teko_access.tk).
A **seventh** — the read of a by-reference lambda capture (`tk_lam_walk`, teko_deleg.tk) —
was found by the verifier after those six had been patched one by one, which is the lesson
of the entry: the record is handed over inside the single `tk_node_replace`
([teko_struct.tk](../../teko_struct.tk), [nodes-and-xt.md](nodes-and-xt.md)), which now
holds the compiler's only `node_assign`, so a door cannot forget what it never writes.
An `enum : i32` is four bytes too and needs none of this: nothing in the language casts an
enum, so an enum load never reaches `tk_prim_cast_check` at all.

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

## The argument the parser cannot type

The same cast that makes the crossing honest is what a WRONG value would hide behind. An
argument is checked against its column with `tk_check_scalar_compat` at the site that reads
the row, and both oracles — `tk_pty_of` at parse time, `tk_ty_of` at pass time — answer -1
for what they cannot see, which that check reads as "refuse nothing". For a column whose
conversion is a cast (a primitive one, `(i64) t`, and an enum one, `(i64) k`) the silence
used to be final: the value crossed as the `i64` the lowering symbol declares, and
`tk_rc_call_args` saw an `i64` argument for an `i64` parameter. `new DateTime(1, k)` on an
`i64 k` parameter compiled and reached the run-time kind guard (exit 70) where
`docs/specs/enum.md` § 5 refuses it — the review finding on #697, D48.

So a column whose answer the parser's silence would hide remembers the argument and judges
it later (`tk_prim_arg_defer`, and `tk_prim_arg_do` once the wait is over). Three columns
do it: the two CAST ones above, and the FLOAT one — there the argument's type decides the
conversion itself (`tk_num_widen` widens an integer and leaves everything else alone), so a
guess would write the wrong node and then hide the value behind it.

**The wait ends at ONE point, and it is a walk of its own**: `tk_prim_arg_judge` drives
`tk_ty_pass_walk` at the END of `tk_over_pass` (teko_over.tk), not on the operator pass's
walk. That is the only place where both halves of what types an argument are true at once —
the scope a parameter is read under is live, and every call under the argument already
carries the symbol its own arguments picked and committed — the pass-time oracle asks the
overload table since D49, but the parser's cannot, and the judgement wants the committed
symbol, not a query. It adds no pass (`passes` 15/30), and a unit that
deferred nothing walks nothing.

The two cast columns defer only the CHECK: `tk_prim_conv` never reads the argument's type
on either arm, so the node written at parse time is the node it would write knowing it. The
float column defers the conversion with it, and `tk_prim_arg_widen` writes the cast where
the literal already put it — so every `--dump-ast` of a program that compiled before is
byte-identical either way. A value nothing types even there is refused rather than taken raw
(`teko: the type of this argument is not known here`), the same rule `tk_prim_binary`
already holds for an operand; a local array's element is the one shape that reaches it,
because `a[0]` lowers to `ld64(a + i * 8)` at parse time and its element type does not
survive the lowering.

**An OPERATOR under a deferred argument needs nothing of its own** since D49. The operator
pass runs three passes earlier, and what made its verdict a guess was the oracle, not the
distance: `tk_ty_of` answered a call to an overloaded name by the FIRST declaration of the
name, so `new DateTime(1, pick(1) + 1)` on a `DateTimeKind pick(i64)` was left as raw
integer arithmetic and then answered the enum once the pick was written. With the oracle
asking the overload table ([passes.md](passes.md), pass 6), pass 11 sees the picked type on
its first visit: it refuses the `+` over an enum where it is written, in an argument or
outside one, and it lowers an operator over a primitive the guess used to hide. The second
judgement (`tk_ops_rejudge`) that stood here is DELETED -- 57 lines -- and the fixture codes
that measured it, 63 and 64 of `tests/surface_datetime_kind.tk`, pass without it.

## The ceilings

| table | cap | counts |
|---|---|---|
| `TK_MAXPRIMT` | 16 | primitive types with a member table |
| `TK_MAXPRIMM` | 192 | member rows, over every primitive |
| `TK_MAXPRIMO` | 128 | operator rows, over every primitive |
| `TK_MAXPRIMX` | 32 | conversion rows — a cast or an implicit widening the compiler lowers to a CALL, over every primitive (D77) |
| `TK_MAXPRIMC` | 4096 | casts over a primitive the COMPILER wrote, in one unit |
| `TK_MAXPRIMP` | 128 | parameter positions, over every row (N2c) |
| `TK_MAXPRIML` | 4 | types a row names before they exist, resolved late by name (N2c) |
| `TK_MAXPARG` | 128 | arguments of a primitive or `enum` position, in one unit, whose type only the pass can tell (D48) |

`TimeSpan` uses 1, 30 and 12 of the first three; `DateTime` brings the totals to 2, 66 and
22, with 41 parameter positions and one late type (`DateTimeKind`); `DateOnly` (N4a) brought
them to 3, 82 and 28, with 51 parameter positions and no late type of its own. The second
and third caps were 96 and 32 until that crumb and are 160 and 48 now — `TimeOnly` (N4b)
would have overflowed both, and raising a `#define` costs nothing but the array it sizes.
The operator cap was raised twice more for the same reason: to 64 by D76, whose nine
`DateTimeOffset` rows took the true count past 48, and to 80 by D77, whose twelve `decimal`
rows took it from 50 to 62. Both `TK_MAXPRIMT` and `TK_MAXPRIMX` were raised again by D81 —
8 to 16 — when `i128`/`u128` (N6a) took the type table's fill to 8 of 16 and the conversion
table's own to 13 of 16 (`teko_prim.tk`'s own `#define` comments carry the running count,
which this page had fallen behind); N6a's twenty-two operator rows took `TK_MAXPRIMO`'s own
fill from 62 to 84, past the 80 cap, which D81 raised to **128**. N6b-1 (D83), the operators
`%`, `<<`, `>>`, `&`, `|`, `^`, `~` on both wide types, added fourteen more operator rows,
taking the fill to **98 of 128** — no cap move needed. N6b-2 (D84), the `f64` and `decimal`
conversions of `i128`/`u128`, both directions, added eight more CONVERSION rows — the
`TK_MAXPRIMX` fill D81 left at 13 of 16 is **21 of 32** now, `TK_MAXPRIMX` raised again for
the same reason D81 raised it the first time: eight more rows than the sixteen it had room
for. The MEMBER cap was raised once, by
D79: C5's nineteen `decimal` rows take the total to **160 of 160**, the old cap exactly
full, so it is **192** now and 101 of the 128 parameter positions are spent. N6b-3 (D85),
`i128`/`u128`'s own `ToString`, `Parse`, `TryParse`, `CompareTo`, `Equals` and the four
statics, added eighteen more member rows (nine per type) — the fill is **178 of 192** now,
no cap move needed — and ten more parameter positions (five per type: `Parse`'s one,
`TryParse`'s two, `CompareTo`'s one, `Equals`'s one), taking `TK_MAXPRIMP`'s own fill to
**111 of 128**. Measured with a counter printed at the end of `teko_init()`, not by counting
registration lines.
`TimeOnly` itself (N4b) brings the totals to **4, 103 and 35**, with **71** parameter
positions — twenty of its own rows plus the one N4a's own `DateOnly` table gained
(`.ToDateTime(TimeOnly)`), seven operator rows, and twenty of its own positions plus one on
`DateOnly`'s — and still no late type of its own: it is registered BEFORE `DateOnly`, so
that seventeenth `DateOnly` row's own column reads a live id and neither table needs
`tk_prim_late`. The `TK_MAXPRIMC` row is per compilation unit and not per registration: it
grows with how much date arithmetic one program writes, roughly two entries per member
access, and a unit past it is `teko: too many casts over a primitive in one unit`.

## What the second primitive actually cost

`DateTime` added one `type_new`, one `syntax_expr`/`syntax_stmt` pair, its 36 rows, and the
calendar in `lib/time.tk` — about 300 lines of ordinary teko. C2 also registered
`DateTimeKind` as a `type_alias` over `i32` with a three-value handler of its own; N2c
deleted both and wrote an ordinary `enum` in `lib/time.tk` instead, which is why the
compiler's own `alias` row is 18 today and not 19.

In `teko_prim.tk` it cost the three additions this page names (multi-argument rows,
`TK_PMSOON`, the own-cast list) and no pass.

## What the third primitive cost: a width

`DateOnly` (N4a) is **four bytes**, `type_new("DateOnly", 4, 4, TK_SINT)`, and it is the
first primitive narrower than the machine word. It added sixteen rows, six operator rows
and the `tk_do_*` half of `lib/time.tk` — and in the mechanism itself, exactly two things:

- **The two casts became instructions.** `tk_prim_raw`'s `(i64) d` is a sign extension of
  the four bytes and `tk_prim_ret`'s `(DateOnly) r` a narrowing store, where over an
  eight-byte primitive both were nothing at all (`walk_narrow`, mc's own, answers 0 for a
  width-8 `TK_SINT`). Nothing in any machine changed: an `enum : i32` already travelled
  every slot a value has, and `tests/surface_dateonly.tk` walks the same set — a local, a
  parameter, a return, a field, a global, an element of a fixed array and of a `T[]`, a
  `ref`/`out` pointee and a closure's captured copy — with the largest day number there is.
- **The own-cast record had to travel further**, which is the five doors named above: with
  every load of a narrow primitive being a cast, every in-place replacement that copies one
  into a placeholder had to hand the record over.

The refusal a hand-written cast earns also gained a column: the member it names is the
primitive's own reader (`` `.Ticks` ``, `` `.DayNumber` ``), because a message naming a
member the type does not have would be wrong (D54).

## What the fourth primitive cost: nothing

`TimeOnly` (N4b) is **eight bytes**, `type_new("TimeOnly", 8, 8, TK_SINT)`, back at the
original width — so neither of `DateOnly`'s two costs repeats. It added twenty rows of its
own (four statics, four constructors, twelve instance members), seven operator rows, one
more row on `DateOnly`'s own table (`.ToDateTime(TimeOnly)`, the member N4a left out
because the argument's type did not exist yet) and the `tk_to_*` half of `lib/time.tk` —
and asked the mechanism for nothing it did not already have: no new `TK_PM*` kind, no new
entry in the own-cast list, no pass. Half of its rows do not even call a new function —
`.Hour`/`.Minute`/`.Second`/`.Millisecond` point straight at `DateTime`'s own
`tk_dt_hour`/`tk_dt_minute`/`tk_dt_second`/`tk_dt_ms` (masking `Kind` off a value that
never carried one is a no-op) and the six comparisons plus `.CompareTo`/`.Equals` point at
`TimeSpan`'s own `tk_ts_eq` … `tk_ts_ge`/`tk_ts_cmp`, the same reuse `DateOnly` made under
D54. `.Ticks` and `.ToTimeSpan()` are symbol-less identity rows, the mechanism's own trick
for a member that renames eight bytes rather than computing them, used here for a METHOD
(`ToTimeSpan()`) for the first time — nothing in `tk_prim_emit` special-cases the row's
`kind` except `TK_PMCTOR`, so the trick was already general enough.

The ONE thing it did add to `lib/time.tk` is a single new panic, `teko: a time of day is
out of range` ([runtime.md](../reference/runtime.md#the-time-library)): no existing
wording reads honestly for an interval that starts at zero, unlike every other range this
file already guards.

The next primitive to ask the mechanism something new is
[`decimal`](../specs/decimal.md), sixteen bytes and a derived machine — the fifth
registration, and the first wider than the machine word rather than narrower.
