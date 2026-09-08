# `DateTime` and `TimeSpan`

**Three of this page's crumbs have landed: P0 (the probes), C1 (`TimeSpan`, and the
primitive-member mechanism under it) — D40 — and C2 (`DateTime`, `DateTimeKind` and the
calendar), D41.** What the two types actually do today is
[timespan.md](../reference/timespan.md) and [datetime.md](../reference/datetime.md), and
the mechanism is [the internals note](../internals/primitives.md). What is left of this
page as design and nothing more is the TEXT half (§ 7: `ToString`, `Parse`, `TryParse`)
and `Now`/`UtcNow`/`Today` (§ 8, blocked on `mc`). The samples below still carry `// no-run`
because each of them names one of those.

The rest of the page is unchanged, and deliberately: it is the design C1 and C2 were built
from. Where the built type differs from the words below, D41 is the ruling and this page is
the older text — the tick constant in § 3's sample is `2024-02-23T16:00`, not the leap day
it is labelled; the fixtures are `tests/surface_datetime.tk` and
`tests/surface_datetime_panic.tk` rather than § 11's five names; and `SpecifyKind`,
`Subtract` and the six `TicksPer*`-style extras of § 6 that need a row with two parameter
TYPES are [not taught](../reference/not-yet.md).

The two types share one page because they share one number. A `TimeSpan` is a count of
ticks, a `DateTime` is a count of ticks since an origin, `DateTime - DateTime` **is** a
`TimeSpan` and `DateTime + TimeSpan` **is** a `DateTime`: neither type is usable without
the other, and splitting them would duplicate the mechanism section below word for word.
`decimal` is on [its own page](decimal.md) because it needs a sixteen-byte value and a
machine module, and neither of these two does.

Both are C#'s, spelling and semantics ([the surface policy](surface.md), rule 1): a tick is
100 nanoseconds, a `TimeSpan` is a signed 64-bit tick count, and a `DateTime` is a tick
count from `0001-01-01 00:00:00` with a `Kind` in the two bits above it.

---

## 1. Representation

| type | width | align | `type_new` kind | what the eight bytes hold |
|---|---|---|---|---|
| `TimeSpan` | 8 | 8 | `TK_SINT` | ticks, signed, `-2^63 .. 2^63 - 1` |
| `DateTime` | 8 | 8 | `TK_SINT` | bits 0..61 ticks, bits 62..63 `Kind` |
| `DateTimeKind` | 4 | 4 | `type_alias` over `i32` | `0` Unspecified, `1` Utc, `2` Local |

`TK_SINT` is the fifth kind ([`mc` hooks.md](https://github.com/minicompiler/mc/blob/main/docs/reference/hooks.md)
§ `type_new`): eight bytes wide is the word, so nothing narrows and nothing extends — the
kind buys the signed comparison and the signed `/` a machine gives an `i64`, and costs no
line in any machine. **Neither type needs a derived machine, a new instruction or a new
intrinsic**, on any of the five legs, which is the whole reason they land before `decimal`.

`DateTime`'s packing is C#'s own `dateData`: the maximum tick value,
`3155378975999999999` (`9999-12-31 23:59:59.9999999`), needs 62 bits, so the two above it
are free and carry the `Kind`. A `DateTime` therefore is **not** ordered by its raw bits —
`Kind` sits above the ticks — which is why every comparison is a call and not a bare `cmp`
(§ 4).

The two constants the surface names:

```
TICKS_PER_MILLISECOND     10000
TICKS_PER_SECOND       10000000
TICKS_PER_MINUTE      600000000
TICKS_PER_HOUR      36000000000
TICKS_PER_DAY      864000000000
UNIX_EPOCH_TICKS  621355968000000000
```

## 2. A primitive with members

This is the mechanism both this page and [`decimal.md`](decimal.md) are built on, and it is
the only genuinely new machinery either of them needs. A type registered with `type_new` is
a **primitive**: it has no row in `teko_struct.tk`'s type table, so it has no fields, no
vtable, no constructor and no methods, and `p.Year` reaches `teko: unknown member` today.

A primitive gets its members from a **lowering table**, not from a vtable: one row per
member, `(type id, member name, static?, arity, symbol, result type)`, registered by the
module that owns the primitive and read by two passes that already exist. A member access
becomes an ordinary call to an ordinary function with surface code in `lib/`:

```
dt.Year                 ->  tk_dt_year(dt)
dt.AddDays(1)           ->  tk_dt_add_days(dt, 1)
DateTime.IsLeapYear(y)  ->  tk_dt_is_leap(y)
a - b                   ->  tk_dt_sub(a, b)
a < b                   ->  tk_dt_lt(a, b)
```

Four properties make it fit the laws this repository already holds:

- **It is statically typed.** Every row names the result type, so the oracle types
  `dt.Year` as `i64` and `a - b` as `TimeSpan` without a run-time tag anywhere.
- **It adds no pass.** The instance side rides teko_typeof.tk's existing deferred-member
  resolution; the operator side rides teko_ops.tk's existing pass. The `passes` row of
  `mc limits` must not move.
- **It adds no intrinsic.** Every symbol a row names is a teko function in `lib/`, with a
  body, reachable from [the runtime reference](../reference/runtime.md) — which is exactly
  what the closed list in [surface.md](surface.md) demands.
- **It adds no message.** `teko: unknown member of DateTime` and
  `teko: unknown static member of DateTime` are the wordings teko_expr.tk and
  teko_access.tk already emit for a declared type.

Two lowerings are **not** calls, because they name the same bits under another type:

```
dt.Ticks         ->  (i64) dt          a cast between two eight-byte slots: no instruction
new DateTime(t)  ->  (DateTime) t      the same, the other way
```

C# spells the accessor `dt.Ticks` and the builder `new DateTime(ticks)`, so teko has both
without teaching a cast the surface does not have. **`(i64) dt` written by hand is
refused** (§ 3): the identity is the compiler's to write, not a conversion the program may
spell, exactly as `tk_num_widen` is the compiler's cast and not a licence to narrow.

`new DateTime(t)` allocates nothing. `new` on a primitive is C#'s value-type constructor,
and teko_expr.tk's `new` handler already dispatches by name; a primitive name reaches the
lowering table instead of `Name_new()`.

## 3. The surface

**Legal.**

```teko
// no-run
#include "time.tk"

i64 main() {
    DateTime leap = new DateTime(638443008000000000);   // 2024-02-29, Unspecified
    DateTime next = leap.AddDays(1);
    if (next.Month != 3) return 1;
    if (next.Day != 1) return 2;

    TimeSpan day = next - leap;
    if (day.Days != 1) return 3;
    if (day.Ticks != 864000000000) return 4;

    TimeSpan two = TimeSpan.FromHours(2);
    if ((day + two).TotalHours != 26.0) return 5;       // TotalHours is an f64

    if (leap.Kind != DateTimeKind.Unspecified) return 6;
    if (DateTime.IsLeapYear(2024) == false) return 7;
    if (leap >= next) return 8;
    return 42;
}
```

**Illegal, and what each one earns.**

```teko
// no-run
#include "time.tk"

i64 main() {
    DateTime a = new DateTime(0);
    DateTime b = new DateTime(1);

    DateTime sum = a + b;         // teko: no operator `+` takes these operands
    i64 raw = a;                  // teko: a value of type DateTime does not convert to i64
    DateTime c = 5;               // teko: a value of type i64 does not convert to DateTime
    i64 cast = (i64) a;           // teko: a date does not cast; `.Ticks` reads it
    TimeSpan t = b - 1;           // teko: a value of type i64 does not convert to DateTime
    i64 y = a.Yearr;              // teko: unknown member of DateTime
    i64 n = DateTime.Now();       // teko: DateTime.Now is not taught yet
    return 0;
}
```

| written | message |
|---|---|
| `a + b`, both `DateTime` | ``teko: no operator `+` takes these operands`` (teko_ops.tk's own) |
| a `DateTime` in an `i64` slot | `teko: a value of type DateTime does not convert to i64` |
| a `DateTime` in an `f64` slot | `teko: a value of type DateTime does not convert to f64` |
| an integer in a `DateTime`/`TimeSpan` slot | `teko: a value of type i64 does not convert to DateTime` |
| `(i64) dt`, `(DateTime) n` written by hand | ``teko: a date does not cast; `.Ticks` reads it and `new DateTime(t)` builds it`` |
| an unknown member | `teko: unknown member of DateTime` / `teko: unknown static member of DateTime` |
| `DateTime.Now`, `UtcNow`, `Today` | `teko: DateTime.Now is not taught yet` (§ 8) |
| `extern` with a `DateTime` parameter | none: a `DateTime` is eight bytes and passes as one, so an `extern` takes it |
| a `DateTime` field, array element, `ref`/`out`, generic argument | none: eight bytes with an alignment, like every other scalar |

## 4. Operators

Every row is a call into `lib/time.tk`; none is left to the core's own arithmetic, because
none of them is a plain add on the raw bits — `DateTime` carries its `Kind` above the
ticks, and C# raises on an overflow where the core would wrap.

| written | lowered to | result | overflow |
|---|---|---|---|
| `d + t`, `t + d` | `tk_dt_add(d, t)` | `DateTime` | panic |
| `d - t` | `tk_dt_sub_ts(d, t)` | `DateTime` | panic |
| `d - d` | `tk_dt_sub(a, b)` | `TimeSpan` | panic |
| `t + t`, `t - t` | `tk_ts_add`, `tk_ts_sub` | `TimeSpan` | panic |
| `-t` | `tk_ts_neg(t)` | `TimeSpan` | panic on `MinValue` |
| `t * n`, `n * t`, `t / n` (`n` an `i64`) | `tk_ts_mul`, `tk_ts_div` | `TimeSpan` | panic; `/ 0` panics |
| `d == d`, `!=`, `<`, `<=`, `>`, `>=` | `tk_dt_eq` … `tk_dt_ge` | `i64` 0/1 | — |
| `t == t`, `!=`, `<`, `<=`, `>`, `>=` | `tk_ts_eq` … `tk_ts_ge` | `i64` 0/1 | — |

**Precedence is not touched.** `+ - * /` and the six comparisons keep the core's own
table; the operator pass rewrites a resolved node and never reads a token. That is the same
guarantee `operator+` on a class already gives ([classes.md](../reference/classes.md)).

A comparison of two `DateTime` values compares **ticks**, `Kind` masked off, which is
C#'s own `Compare`. Two values of different `Kind` compare by their ticks and do not
convert; C# says the same and says it is the program's business.

`t * n` with an `f64` is C#'s `operator *(TimeSpan, double)` and is **not** in this design
([not-yet](../reference/not-yet.md) when it lands).

## 5. Conversions

| from | to | how |
|---|---|---|
| `i64` | `DateTime`, `TimeSpan` | **no** implicit conversion; `new DateTime(t)` / `new TimeSpan(t)` |
| `DateTime`, `TimeSpan` | `i64` | **no** implicit conversion; `.Ticks` |
| `DateTime` | `TimeSpan` | none, in either direction |
| `f64` | `TimeSpan` | `TimeSpan.FromSeconds(x)` and its five siblings |
| `TimeSpan` | `f64` | `.TotalSeconds` and its four siblings |
| `null` | either | `teko: a value of type uptr does not convert to DateTime` (D32's rule) |
| a class, a struct, a `T[]` | either | `teko: a value of type Foo does not convert to DateTime` (D34's rule) |

The refusals are D33/D34's own check widened by one clause: **a primitive converts to
nothing but itself.** `tk_check_scalar_compat` (teko_typeof.tk) asks it after the float and
`null` cases, on the value's type and on the slot's, and reuses `tk_reject_compat`'s
wording — no new message, and the nine slots D33 enumerated are covered at once because
they all end in that one function.

## 6. The API

**`TimeSpan`** — statics on the left, instance members on the right.

| static | instance |
|---|---|
| `TimeSpan.Zero`, `MinValue`, `MaxValue` | `.Ticks` `i64` |
| `FromDays(f64)` `FromHours(f64)` `FromMinutes(f64)` | `.Days` `.Hours` `.Minutes` `.Seconds` `.Milliseconds` `i64` |
| `FromSeconds(f64)` `FromMilliseconds(f64)` `FromTicks(i64)` | `.TotalDays` `.TotalHours` `.TotalMinutes` `.TotalSeconds` `.TotalMilliseconds` `f64` |
| `TimeSpan.Parse(str)`, `TryParse(str, out TimeSpan)` | `.Duration()` `.Negate()` `.CompareTo(TimeSpan)` `.Equals(TimeSpan)` `.ToString()` |

`.Hours` is the hour **component**, `0..23`; `.TotalHours` is the whole span as an `f64`.
That difference is C#'s and it is the one users get wrong, so both fixtures assert it.

**`DateTime`**

| static | instance |
|---|---|
| `DateTime.MinValue`, `MaxValue`, `UnixEpoch` | `.Ticks` `i64`, `.Kind` `DateTimeKind` |
| `new DateTime(i64 ticks)`, `new DateTime(i64 ticks, DateTimeKind k)` | `.Year` `.Month` `.Day` `.Hour` `.Minute` `.Second` `.Millisecond` `i64` |
| `new DateTime(y, m, d)`, `new DateTime(y, m, d, h, mi, s)` | `.DayOfWeek` `.DayOfYear` `i64` |
| `DateTime.IsLeapYear(i64)`, `DaysInMonth(i64, i64)` | `.Date` `DateTime`, `.TimeOfDay` `TimeSpan` |
| `DateTime.Parse(str)`, `TryParse(str, out DateTime)` | `.AddTicks` `.AddMilliseconds` `.AddSeconds` `.AddMinutes` `.AddHours` `.AddDays` `.AddMonths` `.AddYears` |
| `DateTime.SpecifyKind(DateTime, DateTimeKind)` | `.Subtract(DateTime)` `.Subtract(TimeSpan)` `.CompareTo` `.Equals` `.ToString()` `.ToString(str)` |

The civil calendar is the proleptic Gregorian one C# uses, computed the way C# computes
it — a days-to-year walk over the 400/100/4 cycles and the twelve-entry cumulative month
table, both `const i64` arrays in `lib/time.tk`. `AddMonths` clamps the day to the length
of the target month (`2024-01-31 + 1 month` is `2024-02-29`), which is C#'s rule and is a
fixture of its own.

## 7. Text

Two formats, both ISO 8601 and both C#'s:

| format | shape | notes |
|---|---|---|
| `"s"` (sortable) | `2024-02-29T13:45:30` | 19 characters, no fraction, no zone |
| `"o"` (round-trip) | `2024-02-29T13:45:30.1234567` | 27 characters, plus `Z` when `Kind` is Utc |

`ToString()` is `"o"`, as C# `DateTime.ToString("o")` writes it, and `ToString(str fmt)`
takes exactly those two strings; anything else is `teko: a date format is "o" or "s"`.
`TimeSpan.ToString()` is C#'s `"c"`: `[-][d.]hh:mm:ss[.fffffff]`.

Parsing accepts exactly what those formats write, and nothing else — no culture, no
`ParseExact` with an arbitrary pattern. `Parse` panics on a string it cannot read
(`teko: the string is not a date`), and `TryParse(s, out v)` answers `0`/`1` and writes
`MinValue` on failure, which is the C# pair and the reason `out` is worth having.

**Allocation.** `ToString()` hands back a `str` from the arena, alive for the run like
every other arena block, and `tk_dt_fmt(ptr buf, DateTime d, i64 fmt)` is the half that
writes into a caller's buffer and allocates nothing — the split `mc`'s own `<float_rt>`
makes between `putf64` and `fmt_f64`. A `str` is not reference counted
([memory.md](../reference/memory.md)), so a `ToString()` inside a loop grows the arena;
that is stated in the reference the crumb owes, not hidden.

## 8. What stays out, and the one thing that is blocked

**`DateTime.Now`, `DateTime.UtcNow` and `DateTime.Today` are blocked on `mc`.** They need a
wall clock, and a wall clock is one symbol whose name differs per host: `clock_gettime` on
Linux and macOS, `GetSystemTimeAsFileTime` on Windows. teko cannot pick between them —
`mc` has no conditional compilation, the five legs compile one source, and the Windows leg
resolves `<sys>`'s libc-shaped names through `mcrt.obj`, which is `mc`'s file and not this
repository's. Writing a second include per host, or an `[include].paths` root chosen by the
build config, would push an operating-system choice into every consumer's `teko.toml` for
one function.

So the ask goes to `mc`'s notices file, in `mc`'s own shape: **`<sys>` grows a wall clock**,
one function with one name across the three hosts (`i64 sys_time_ns()`, or the POSIX pair
`clock_gettime` shimmed on Windows as the other five already are). It unblocks on whichever
`mc` release carries it; until then `DateTime.Now` is refused by name and the rest of this
page lands without it. Everything else here — every component, every operator, every
format — needs no clock and no `mc` change at all.

Also deliberately outside:

| left out | why |
|---|---|
| `ToLocalTime`, `ToUniversalTime`, `DateTimeOffset` | a time-zone database is not a language feature |
| `enum DateTimeKind` | teko has no `enum`; the alias plus three constants is the same surface, and `enum` is a crumb of its own |
| culture, `ParseExact`, custom format strings | the market's answer is a formatting library, not a primitive |
| `TimeSpan * f64`, `DateTime` in a `switch` pattern | neither is refused on principle; neither is in this design |
| `DateOnly`, `TimeOnly` | C# has them; nothing asks for them yet |

## 9. The hooks, by module

| module | what it grows |
|---|---|
| **`teko_prim.tk`** (new) | the primitive registry: the id table, the member table, the operator table, `tk_prim_is`, `tk_prim_member`, `tk_prim_op`. Registrations only — no pass, no `syntax*` of its own |
| **`teko_time.tk`** (new) | three registrations (`type_new` ×2, `type_alias` ×1), `syntax_expr` for `DateTime`, `TimeSpan` and `DateTimeKind` so a type word opens an expression, and every row of § 4 and § 6 |
| `teko_expr.tk` | `.` on a receiver whose type is a primitive, and `new Name(args)` on a primitive name — two lookups into `teko_prim.tk`, at the two places the row question is already asked |
| `teko_typeof.tk` | the deferred `.` when the receiver is a parameter; `tk_ty_of` answers a primitive member's declared result; `tk_check_scalar_compat` gains the one clause of § 5 |
| `teko_access.tk` | `Type.member` where the type is a primitive: the same deferred static access, resolved against the member table |
| `teko_ops.tk` | the operator pass consults the primitive operator table when either operand is a primitive, before it consults the declaring type's own operators |
| `teko.tk` | `#include` and the two `_init()` calls, both **before** `tk_float_init()` so that the decimal literal of the next page can claim `1.5m` ahead of `<float>` |
| `lib/rt.tk` | nothing at all |
| `lib/time.tk` (new) | every symbol the tables name: about 700 lines of ordinary teko, `#include "rt.tk"` for `panic` |
| `core_teko.mc`, `user.mc` | nothing: `teko_init()` is still the one registration site |

**The include is part of the surface.** A program that names `DateTime` without
`#include "time.tk"` would reach `mc: call to unknown function` from the core; teko
pre-empts it where the type word is read — ``teko: `DateTime` needs #include "time.tk"`` —
because the module knows whether the unit declared `tk_dt_year`. Folding the two files into
`lib/rt.tk` instead would put a thousand lines of calendar into every program that only
wants an array, and is the cheaper flip if the owner prefers one include.

## 10. What it costs in `mc limits`

Measured on `6b868f0c` with `mc limits . --config teko.toml`, the gate's own configuration
(tolerance 1.0): `types 7/14`, `alias 14/28`, `syntax 14/28`, `passes 15/30`,
`intrin 8/16`, verdict `ok`.

| row | today | after | why |
|---|---|---|---|
| `types` | 7 | **10** | `decimal`, `DateTime`, `TimeSpan` — this page owns two of the three |
| `alias` | 14 | **15** | `DateTimeKind` |
| `syntax` | 14 | **17** | one `syntax_expr` per type word that opens an expression |
| `passes` | 15 | **15** | by design: the mechanism adds no pass |
| `intrin` | 8 | **8** | by design: teko still registers none |
| `nodes`, `funcs`, `heap` (compiler) | — | up | about 1200 lines of module and 700 of library |

`types 10/14` is the row to watch: the reserved column is the static estimate doubled at
tolerance 1.0, and a program of the project's own declaring many types shares that table.
The P0 crumb measures it on the heaviest fixture before C1 is written.

## 11. Fixtures

| fixture | asserts | `expect-exit` |
|---|---|---|
| `tests/primitives_timespan.tk` | construction, the six comparisons, `+ - *` and `/`, component against total (`.Hours` 2 vs `.TotalHours` 26.0), `Zero`/`MinValue`/`MaxValue`, `.Duration()`, `.Negate()` | `42` |
| `tests/primitives_timespan_overflow.tk` | `TimeSpan.MaxValue + TimeSpan.FromTicks(1)` | `70` |
| `tests/primitives_datetime.tk` | `2024-02-29 + 1 day` is `2024-03-01`; the epoch tick constant round-trips; every component of `2024-02-29T13:45:30.1234567`; `DayOfWeek`; `IsLeapYear(2024)` and `IsLeapYear(1900)`; `DaysInMonth(2024, 2)` is 29 | `42` |
| `tests/primitives_datetime_months.tk` | `2024-01-31.AddMonths(1)` is `2024-02-29`; `AddYears` on a leap day; `.Date` and `.TimeOfDay` | `42` |
| `tests/primitives_datetime_overflow.tk` | `DateTime.MaxValue.AddTicks(1)` | `70` |
| `tests/primitives_datetime_text.tk` | `"o"` and `"s"` both ways, `TryParse` on a good and a bad string, `Parse` of what `ToString` wrote | `42` |
| `tests/primitives_datetime_parse_bad.tk` | `DateTime.Parse("not a date")` | `70` |

Every one of them is a whole program with `#include "../lib/time.tk"`, returns `42` on
success and a small distinct number per failed assertion, which is the convention
`tests/primitives_float.tk` already follows.

## 12. The crumbs

Both are independent landings; C2 depends on C1 for the mechanism and on nothing else.

### P0 — the probes (S) — **landed (D40)**

The measurements are [`docs/internals/primitives.md`](../internals/primitives.md) § "The P0
probes". In short: the word is a type in every position and `--dump-ast` prints it BY NAME
(so registering it moves no existing dump); `syntax_expr` reaches the handler ahead of the
core's own type-word rule, and `syntax_stmt` wins over the core's declaration path, so the
handler calls `parse_var` itself; the conversion clause left all 47 fixtures byte-identical;
the registration costs `types 9 → 10` and `alias 16 → 17`, with `syntax`, `passes` and
`intrin` unmoved. The fourth answer is the load-bearing one: with **no** operator claim the
core compiles and runs raw arithmetic over two values of the new type (`a * b` answered
40), which is why § 13's resolution — claim every binary, refuse the rows that do not
exist — is the design and not a precaution.

No product module. Four questions answered on the tree, with throwaway programs under
`build/`, and the numbers recorded in [`docs/internals/`](../internals/README.md):

1. a `type_new("TimeSpan", 8, 8, TK_SINT)` from a teko module — is the word a type in all
   seven positions, does `--dump-ast` print it, does a field and a `T[]` element of it work
   with no further code?
2. `syntax_expr` on a word `type_new` reserved — does `TimeSpan.Zero` reach the handler
   ahead of the core's "a type word is no expression"?
3. `tk_check_scalar_compat` with the one clause — does `--dump-ast` stay byte-identical on
   all 45 fixtures?
4. the `types` row of `mc limits` on the heaviest fixture, with three extra ids simulated.

**Gate:** 45/45 fixtures at their `expect-exit`, `--dump-ast` byte-identical, `FIXPOINT
OK`, `mc limits` verdict `ok`. No documentation owed beyond the internals note.

### C1 — `TimeSpan`, and the mechanism under it (L) — **landed (D40)**

Built as designed, with four deltas the work measured and the log records (D40): the
lowering table lives in `teko_prim.tk` and the registrations in `teko_time.tk` (the split
this section already asks for); `.Ticks` and `new TimeSpan(t)` are the identity cast, which
made a symbol-less row worth having; `tk_ty_binary` had to ask the operator table, because
the core types a binary from its LEFT operand and `3 * hour` is a `TimeSpan`; and the two
fixtures are `tests/surface_timespan.tk` (42) and `tests/surface_timespan_overflow.tk`
(70), named for the `surface_*` family the repository already uses. Text and the hand-
written cast refusal are NOT in it ([not-yet.md](../reference/not-yet.md)).


`teko_prim.tk` and `teko_time.tk` as far as `TimeSpan` needs them; the member lowering in
teko_expr.tk/teko_typeof.tk/teko_access.tk; the operator rows in teko_ops.tk; the
conversion clause; `lib/time.tk`'s `TimeSpan` half.

**Gate:** `tests/primitives_timespan.tk` (42) and `tests/primitives_timespan_overflow.tk`
(70) added and green, the other 45 unchanged and `--dump-ast` byte-identical on all of
them, `FIXPOINT OK`, `mc limits` verdict `ok` with `passes` and `intrin` **not moved**,
`sh scripts/check-docs.sh` green. **Owes:** a `TimeSpan` section in
[types.md](../reference/types.md), the new refusals in
[diagnostics.md](../reference/diagnostics.md), the `lib/time.tk` functions in
[runtime.md](../reference/runtime.md), the two new modules in
[modules.md](../internals/modules.md) and the module count in
[`CLAUDE.md`](../../CLAUDE.md) and [docs/README.md](../README.md).

### C2 — `DateTime` (L)

The second `type_new`, the `DateTimeKind` alias, the calendar, the components, the `Add*`
family, the operators mixing the two types, `"o"`/`"s"` in both directions, and the
`Now`/`UtcNow`/`Today` refusal.

**Gate:** the five `DateTime` fixtures at their exit codes, everything C1 gated on again,
plus a `not-yet.md` row for `Now` and one for the time zone. **Owes:** the `DateTime`
section of types.md, diagnostics.md, runtime.md, and
[guide/10-values-and-types.md](../guide/10-values-and-types.md).

### C6 — `Now` and `UtcNow` (S, blocked)

One static row per name, one call into `lib/time.tk`, one fixture that asserts
`UtcNow > UnixEpoch` and `Now.Kind == DateTimeKind.Local`. **Blocked** until `mc`'s `<sys>`
carries a wall clock on the three hosts; it is numbered C6 because it lands after
`decimal`'s crumbs whenever it unblocks, and it needs nothing from them.

## 13. Risks and law tensions

| risk | recommended resolution |
|---|---|
| **`TK_SINT` makes the core willing to do arithmetic on a `DateTime`.** The core types `a - b` from the left operand and would emit a plain `sub` on the raw bits, `Kind` included, if the operator pass ever failed to claim the node. | The pass claims every binary where either operand is a primitive id and **refuses** the ones with no row (``teko: no operator `+` takes these operands``). A binary that reaches the machine with a primitive operand is a bug, not a silent wrong answer — the C1 fixture covers all sixteen combinations of the two types with `i64`. |
| **A member table is a second way to declare a member.** A class declares members in one place, a primitive in another. | Accepted, and it is the mc mechanism's own consequence: `type_new` gives primitives, not aggregates. The table is data in one module, listed in the reference beside the class members, and no third way is opened. |
| **The include.** `#include "time.tk"` is a build-time step C# does not have. | Refuse the type word with the include in the message, and leave the flip to `lib/rt.tk` open for the owner: it is a one-line change either way. |
| **`types 10/14` in `mc limits`.** Three primitives spend three of a program's type ids before the program declares one. | P0 measures it. If the heaviest fixture goes over, the fix is `[limits] tolerance`, not fewer types — and `tolerance` is already 1.0, so the row would be a `grew` verdict rather than an error. |
| **`Now` invites a workaround.** A per-host include, or a `[libs]`/`[externs]` mapping in the leg config, would make it work today. | Refuse it. The rule is `docs/specs/surface.md`'s: a construct `mc` cannot express is reported, never worked around here. `DateTime.Now` is refused by name and the ask is filed. |
| **`ToString()` leaks into the arena.** A `str` is not counted. | Ship the `fmt` half in the same crumb and document both, exactly as `mc`'s `<float_rt>` does. A counted string type is a separate decision. |

## 14. What the `mc` channel is asked

1. **A wall clock in `<sys>`** — one name on the three hosts (§ 8). Blocks `Now`, `UtcNow`
   and `Today`, and nothing else on this page.

Nothing else here needs `mc` to change: `type_new`, `type_alias`, `syntax_expr` and the
existing passes are all released mechanisms on the pinned `0.15.18`.
