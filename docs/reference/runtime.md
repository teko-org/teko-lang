# The runtime

Seven library files, all of them **program** code compiled under the same taught vocabulary
as the program that includes them: `lib/rt.tk`, the runtime every program links against,
[`lib/time.tk`](#the-time-library), the surface code every `TimeSpan` and `DateTime` member
and operator lowers to, the three files of the sixteen-byte types —
[`lib/decimal.tk`, `lib/guid.tk` and `lib/wide.tk`](#the-wide-libraries) — the limb vector
they compute over ([`lib/limbs.tk`](#the-limb-library)), and `lib/math.tk`, C#'s `Math`
over a `decimal`, which is an ordinary class and no mechanism at all.

`lib/rt.tk` is the runtime a teko program links against: the arena, the reference counting
the compiler injects, the guards behind an index and an interface call, and a handful of
primitives over `str` and `f64`. It is **program** code, compiled under the same taught
vocabulary as the program that includes it.

```
#include "rt.tk"          // resolved through [include].paths (this repository names `lib`)
#include "../lib/rt.tk"   // or relative to the file that writes it
```

Most of what is below is called for you: the compiler emits `rt_own`, `rt_store`,
`rc_dec`, `tk_arr_at`, `tk_itab` and the rest at the sites that need them. What a program
writes by hand is usually only `panic`, `rt_live`/`rt_used`/`rt_peak`, `tk_str_len`,
`tk_str_slice` and the two `f64` bit functions.

Including the runtime also brings `<sys>`, so `write`, `read`, `open`, `close`, `exit`,
`strlen`, `puts` and `putnum` are in scope.

---

## The arena

| signature | does |
|---|---|
| `uptr rt_alloc(i64 n)` | `n` zeroed bytes, 16-byte aligned, from the free list of its size class or from the bump pointer. Never returns 0; panics when the arena is exhausted or `n` is negative |
| `void rt_free(uptr p, i64 n)` | gives the `n` bytes at `p` back to their size class; a block over 256 bytes is dropped |
| `void rt_zero(uptr p, i64 n)` | writes `n` zero bytes at `p` |
| `i64 rt_class(i64 sz)` | the size class of `sz` bytes, or `-1` when no list holds it |
| `i64 rt_used()` | how far the bump pointer has moved |
| `i64 rt_live()` | blocks handed out and not yet given back |
| `i64 rt_peak()` | the high-water mark of the bump pointer |

`rt_fl_at(i64 i)` and `set_rt_fl_at(i64 i, uptr v)` read and write the head of one free
list; they belong to the allocator itself.

## Failing

| signature | does |
|---|---|
| `void panic(str msg)` | writes `teko: <msg>` to standard error and exits **70** |
| `void rt_panic(uptr msg)` | the same, under the name the runtime's own guards use |

## Reference counting

| signature | does |
|---|---|
| `void rc_inc(uptr p)` | one more reference; a null pointer is a no-op |
| `void rc_dec(uptr p)` | one fewer; at zero it calls the class's release function through word 0 of the vtable, which runs the destructor, releases the counted fields and frees the block |
| `uptr rt_own(uptr p)` | borrowed to owned: increments and hands the pointer back |
| `void rt_drop(uptr p)` | releases a value produced and thrown away |
| `void rt_store(uptr slot, uptr v)` | `*slot = v` with both counts kept straight; the increment comes first, so `x = x` is safe |
| `void rt_store_own(uptr slot, uptr v)` | the same when `v` is already owned — from `new`, or from a function that returned its own reference |
| `void rt_release_array(uptr base, i64 n)` | releases `n` object slots starting at `base`: an inline array field of counted elements |

## The temporaries of one statement

A counted value handed to a call has no owner. It is parked and swept when the statement
ends. The mark nests with the calls, so a callee's temporaries sweep back down to the
caller's.

| signature | does |
|---|---|
| `i64 rt_mark()` | the current top of the parking table |
| `uptr rt_park(uptr p)` | parks `p` and hands it back; more than 64 in one statement panics |
| `void rt_sweep(i64 mark)` | releases everything parked above `mark` |

## Dispatch and guards

| signature | does |
|---|---|
| `uptr tk_itab(uptr vt, i64 id)` | the method table of interface `id` inside the class whose vtable is `vt`; panics when the class has no table or does not implement it |
| `uptr tk_deleg_code(uptr d)` | a delegate value's code pointer; a null delegate panics instead of faulting |
| `uptr tk_arr_at(uptr a, i64 i, i64 w)` | the address of element `i` of a `T[]` whose elements are `w` bytes wide, guarded against null, a negative index and an index past `Length` |

## `str`

| signature | does |
|---|---|
| `i64 tk_str_len(str s)` | the length of a NUL-terminated `str` |
| `str tk_str_slice(str s, i64 from)` | a view of `s` from byte `from` to the same NUL — pointer arithmetic only, **zero copy** |
| `i64 tk_str_eq(str a, str b)` | byte-wise equality, no allocation |

## `enum` text

What `.ToString()`, `Color.Parse(s)`, `TryParse` and `IsDefined` lower to
([types.md § enum](types.md#tostring-parse-tryparse-isdefined), `docs/specs/enum.md` § 6).
`names`/`vals` are the two globals `teko_enum.tk` writes per enum, the first time one of
these four is spelled on it; `n` is their shared length. Called by generated code, not
usually by hand.

| signature | does |
|---|---|
| `str tk_enum_name(uptr names, uptr vals, i64 n, i64 v)` | the member's own name, or the decimal digits when `v` matches none |
| `i64 tk_enum_find(uptr names, i64 n, str s)` | the INDEX of the member named `s` in `names`, or `-1` — kept apart from the member's own value so a negative member value is never mistaken for "not found" |
| `i64 tk_enum_isdefined(uptr vals, i64 n, i64 v)` | 1 when some member carries `v` |
| `i64 tk_enum_parse(uptr names, uptr vals, i64 n, str s, str msg)` | `vals[tk_enum_find(...)]`, or `panic(msg)` (exit 70) when `tk_enum_find` answers `-1` |
| `i64 tk_enum_tryparse8/16/32/64(uptr names, uptr vals, i64 n, str s, uptr outp)` | 0/1, writing `vals[tk_enum_find(...)]` into `outp` at the width named (0, `outp` untouched, when `tk_enum_find` answers `-1`) — one per underlying width an enum may declare |
| `i64 tk_i64_to_dec(i64 v, uptr buf)` | `v` in decimal, signed, NUL-terminated, into `buf` (24 bytes); the digits `tk_enum_name`'s own fallback formats |

## `f64` bits

A cast between a float and an integer converts the value. These two reinterpret the eight
bytes instead.

| signature | does |
|---|---|
| `u64 tk_f64_bits(f64 x)` | the bit pattern of `x` |
| `f64 tk_f64_from_bits(u64 bits)` | the `f64` those bits are |

## Closure captures

Written at the site that builds a closure, one link per capture, each handing the object
back to the next.

| signature | does |
|---|---|
| `uptr tk_cap_put(uptr p, i64 off, i64 v)` | a capture by value, or the address of a capture by reference |
| `uptr tk_cap_own(uptr p, i64 off, uptr v)` | a capture of counted type: the closure takes a reference of its own |
| `uptr tk_cap_putf(uptr p, i64 off, f64 v)` | a capture of `f64` — a float travels in another register file, so its slot is written through the float accessor |
| `uptr tk_cap_putf32(uptr p, i64 off, f32 v)` | the same for `f32` |

---

```teko
// expect-exit: 42
#include "rt.tk"

class Cell {
    public i64 v;
}

i64 main() {
    i64 floor = rt_live();

    str s = "hello";
    if (tk_str_len(s) != 5) return 1;
    str tail = tk_str_slice(s, 3);               // a view, no copy
    if (tk_str_len(tail) != 2) return 2;

    f64 x = 2.5;
    if (tk_f64_from_bits(tk_f64_bits(x)) != 2.5) return 3;

    {
        Cell c = new Cell;
        c.v = 40;
        if (rt_live() != floor + 1) return 4;
    }
    if (rt_live() != floor) return 5;            // the block gave it back
    if (rt_used() < 16) return 6;
    if (rt_peak() < rt_used()) return 7;

    return 40 + 2;
}
```

---

## The time library

`lib/time.tk`, what [`TimeSpan`](timespan.md) and [`DateTime`](datetime.md) lower to. It
includes `rt.tk` for `panic`, so a program
that includes it has both:

```
#include "time.tk"          // brings rt.tk with it
```

**Nothing here is called by hand.** Every function takes and answers the RAW tick count as
an `i64`, and the compiler writes the two casts around it — `(i64) t` on the way in,
`(TimeSpan) r` on the way out — so `t.Days` IS `tk_ts_days((i64) t)` and `a + b` IS
`(TimeSpan) tk_ts_add((i64) a, (i64) b)`. Calling one directly works and is not a supported
spelling: the surface is the member, not the symbol.

| signature | is |
|---|---|
| `i64 tk_ts_zero()` `tk_ts_min()` `tk_ts_max()` | `TimeSpan.Zero`, `MinValue`, `MaxValue` |
| `i64 tk_ts_per_day()` `tk_ts_per_hour()` `tk_ts_per_minute()` `tk_ts_per_second()` `tk_ts_per_ms()` | the five `TicksPer*` constants |
| `i64 tk_ts_scaled(f64 value, f64 scale)` | the shared builder: scale, range-check (a NaN included) and truncate toward zero |
| `i64 tk_ts_from_days(f64)` `tk_ts_from_hours(f64)` `tk_ts_from_minutes(f64)` `tk_ts_from_seconds(f64)` `tk_ts_from_ms(f64)` | the `From*` family |
| `i64 tk_ts_from_ticks(i64)` | `TimeSpan.FromTicks` |
| `i64 tk_ts_days(i64)` `tk_ts_hours(i64)` `tk_ts_minutes(i64)` `tk_ts_seconds(i64)` `tk_ts_ms(i64)` | the components |
| `f64 tk_ts_total_days(i64)` `tk_ts_total_hours(i64)` `tk_ts_total_minutes(i64)` `tk_ts_total_seconds(i64)` `tk_ts_total_ms(i64)` | the totals |
| `i64 tk_ts_add(i64, i64)` `tk_ts_sub(i64, i64)` `tk_ts_mul(i64, i64)` `tk_ts_div(i64, i64)` `tk_ts_neg(i64)` | the arithmetic, each with its overflow check |
| `i64 tk_ts_duration(i64)` | `.Duration()` |
| `i64 tk_ts_eq(i64, i64)` `tk_ts_ne` `tk_ts_lt` `tk_ts_le` `tk_ts_gt` `tk_ts_ge` | the six comparisons, 0 or 1 |
| `i64 tk_ts_cmp(i64, i64)` | `.CompareTo()`: `-1`, `0` or `1` |

...and the same for a `DateTime`, whose raw eight bytes are the ticks in bits 0..61 and the
`Kind` in the two above them. Every function below masks the `Kind` off before it computes
and puts it back when it answers a date, which is exactly why `.Ticks` is a call here and
not the identity cast `TimeSpan.Ticks` is:

| signature | is |
|---|---|
| `i64 tk_dt_ticks(i64)` `i64 tk_dt_kind_of(i64)` | the two halves of the eight bytes |
| `i32 tk_dt_kind(i64)` | `.Kind`, answered as the underlying `i32` and cast to `DateTimeKind` at the site |
| `enum DateTimeKind : i32` | the three values, an ordinary `enum` declaration in the same file and not a function (N2c) |
| `i64 tk_dt_pack(i64 ticks, i64 kind)` | the two halves back into one value, range-checked |
| `i64 tk_dt_days_before(i64 month, i64 leap)` | the cumulative month table, as a function |
| `i64 tk_dt_is_leap(i64 y)` `i64 tk_dt_days_in_month(i64 y, i64 m)` | the two calendar statics |
| `i64 tk_dt_days_from_ymd(i64 y, i64 m, i64 d)` | the days since `0001-01-01` of a civil date, every field checked |
| `i64 tk_dt_time_ticks(i64 h, i64 mi, i64 s, i64 ms)` | the ticks of a time of day, every field checked |
| `i64 tk_dt_part(i64 d, i64 part)` | the 400/100/4-year walk: year, month, day or day of year |
| `i64 tk_dt_year(i64)` `tk_dt_month(i64)` `tk_dt_day(i64)` `tk_dt_doy(i64)` `tk_dt_dow(i64)` | the civil components |
| `i64 tk_dt_hour(i64)` `tk_dt_minute(i64)` `tk_dt_second(i64)` `tk_dt_ms(i64)` | the time of day |
| `i64 tk_dt_date(i64)` `i64 tk_dt_tod(i64)` | `.Date` and `.TimeOfDay` |
| `i64 tk_dt_min()` `tk_dt_max()` `tk_dt_epoch()` | `MinValue`, `MaxValue`, `UnixEpoch` |
| `i64 tk_dt_from_ticks(i64)` `i64 tk_dt_from_ticks_kind(i64, i64)` | the two raw constructors |
| `i64 tk_dt_ymd(i64, i64, i64)` `tk_dt_ymdhms(...)` `tk_dt_ymdhmsms(...)` | the three calendar constructors |
| `i64 tk_dt_add_ticks(i64, i64)` | `.AddTicks`, and what every other `Add*` ends in |
| `i64 tk_dt_scaled(i64 d, f64 value, f64 scale)` | the shared `double` builder: C#'s own rounding to the nearest millisecond |
| `i64 tk_dt_add_days(i64, f64)` `tk_dt_add_hours` `tk_dt_add_minutes` `tk_dt_add_seconds` `tk_dt_add_ms` | the `Add*` family that takes a `double` |
| `i64 tk_dt_add_months(i64, i64)` `i64 tk_dt_add_years(i64, i64)` | the calendar ones, day clamped to the target month |
| `i64 tk_dt_add(i64, i64)` `tk_dt_sub_ts(i64, i64)` `tk_dt_sub(i64, i64)` | `d + t`, `d - t` and `d - d` |
| `i64 tk_dt_eq(i64, i64)` `tk_dt_ne` `tk_dt_lt` `tk_dt_le` `tk_dt_gt` `tk_dt_ge` | the six comparisons, `Kind` masked off |
| `i64 tk_dt_cmp(i64, i64)` | `.CompareTo()`: `-1`, `0` or `1` |

...and the `DateOnly` half (N4a), whose value is the DAY NUMBER since `0001-01-01` and not
a tick count. Nothing is packed above it, so the four bytes the compiler hands in are the
number itself; the calendar is not rewritten here, it is the `tk_dt_*` half above reached
by multiplying that number back into the ticks of its own midnight:

| signature | is |
|---|---|
| `i64 tk_do_min()` `tk_do_max()` | `DateOnly.MinValue`, `MaxValue`: day `0` and day `3652058` |
| `i64 tk_do_ymd(i64 y, i64 m, i64 d)` | `new DateOnly(y, m, d)`, through `tk_dt_days_from_ymd` — where a date that does not exist panics |
| `i64 tk_do_from_daynum(i64 n)` | `DateOnly.FromDayNumber`, range-checked |
| `i64 tk_do_from_datetime(i64 d)` | `DateOnly.FromDateTime`: the date half, time of day and `Kind` dropped |
| `i64 tk_do_year(i64)` `tk_do_month(i64)` `tk_do_day(i64)` `tk_do_doy(i64)` | the civil components, through `tk_dt_part` |
| `i64 tk_do_dow(i64)` | `.DayOfWeek`, `(n + 1) % 7`: 0 is Sunday, and `0001-01-01` was a Monday |
| `i64 tk_do_add_days(i64, i64)` | `.AddDays`, overflow-checked before the range check |
| `i64 tk_do_add_months(i64, i64)` `i64 tk_do_add_years(i64, i64)` | `.AddMonths`/`.AddYears`, through `tk_dt_add_months` and its clamp |

`.DayNumber` is in no row: it is the four bytes themselves, the identity read
`TimeSpan.Ticks` is. Neither are the comparisons — `d < e`, `.CompareTo` and `.Equals`
lower to `tk_ts_lt`, `tk_ts_cmp` and `tk_ts_eq` above, because a day number is an ordinary
non-negative `i64` and those functions already answer exactly this (D54; the spec had
named a `tk_do_eq` … `tk_do_ge` family, and six wrappers that forward and nothing else are
what this does not write).

...and the `TimeOnly` half (N4b), whose value is the TICKS SINCE MIDNIGHT and not a day
number: an ordinary, non-negative `i64` in `0 .. TICKS_PER_DAY - 1`. `DateTime`'s own
component functions are read directly for `.Hour`, `.Minute`, `.Second` and
`.Millisecond` — masking `DATETIME_TICK_MASK` off a value already well under it is a
no-op, so four more functions that would repeat the same division and modulo are not
written:

| signature | is |
|---|---|
| `i64 tk_to_min()` `tk_to_max()` | `TimeOnly.MinValue`, `MaxValue`: ticks `0` and `863999999999` |
| `i64 tk_to_from_ticks(i64)` | `new TimeOnly(ticks)` AND `TimeOnly.FromTimeSpan(ts)`: the one range check both share |
| `i64 tk_to_hm(i64, i64)` `tk_to_hms(i64, i64, i64)` `tk_to_hmsms(i64, i64, i64, i64)` | the three calendar-free constructors, through `tk_dt_time_ticks` |
| `i64 tk_to_add(i64 t, i64 ts)` | `.Add(TimeSpan)`, wrapping at both ends of the day; never panics |
| `i64 tk_to_add_hours(i64, f64)` `tk_to_add_minutes(i64, f64)` | C#'s own form, `.Add(TimeSpan.FromHours(v))` and its sibling |
| `i64 tk_to_is_between(i64 t, i64 s, i64 e)` | `.IsBetween`: `s` inclusive, `e` exclusive, wraps when `s` is after `e` |
| `i64 tk_to_sub(i64 a, i64 b)` | `a - b`: the elapsed `TimeSpan`, wrapping FORWARD across midnight, never negative |
| `i64 tk_do_to_datetime(i64 n, i64 t)` | `DateOnly.ToDateTime(TimeOnly)`: the day number back into ticks, plus the time of day, `Kind` Unspecified |

`.Ticks` and `.ToTimeSpan()` are in no row: the former is the eight bytes themselves,
`TimeSpan.Ticks`'s own identity, and the latter is the SAME eight bytes read back under
`TimeSpan`, the identity the other way — neither calls a function of this file.
`TimeOnly.FromDateTime(d)` is `tk_dt_tod` above, named directly and not copied: it already
answers ticks since midnight, masked and modulo `TICKS_PER_DAY`, which is exactly a
`TimeOnly`'s own range. Nor are `.CompareTo`, `.Equals` or the six comparisons: like
`DateOnly`'s, they lower to `tk_ts_cmp` and `tk_ts_eq` … `tk_ts_ge`, because a tick count of
a time of day is an ordinary non-negative `i64` too.

Five constants come with the file, and a program may read them: `TICKS_PER_MILLISECOND`,
`TICKS_PER_SECOND`, `TICKS_PER_MINUTE`, `TICKS_PER_HOUR`, `TICKS_PER_DAY`, plus
`TIMESPAN_MAX_TICKS` and `TIMESPAN_MIN_TICKS` — the date half adds
`DATETIME_MAX_TICKS`, `UNIX_EPOCH_TICKS`, `DATETIME_TICK_MASK`, `DAYS_PER_YEAR`,
`DAYS_PER_4_YEARS`, `DAYS_PER_100_YEARS`, `DAYS_PER_400_YEARS`, the four `DT_*`
selectors and `DATEONLY_MAX_DAYNUM` — and the time-of-day half adds `TIMEONLY_MAX_TICKS`.
They are ordinary top-level `const i64`, so a program that declares one of those names
itself collides with it.

Fifteen panics live here, all exit **70**:

| panic | when |
|---|---|
| `teko: a time span overflowed` | `+ - * /`, the unary minus and `.Duration()` past the range |
| `teko: a time span divided by zero` | `t / 0` |
| `teko: a time span is out of range` | a `From*` value that scales past the range, or a NaN |
| `teko: a date does not exist` | a day past the end of its own month: `new DateTime(2023, 2, 29)` |
| `teko: a date is out of range` | a tick count outside `0 .. 3155378975999999999`, and every `Add*` that lands outside it |
| `teko: a year is out of range` | a year outside `1..9999`, `DateTime.IsLeapYear` included |
| `teko: a month is out of range` | a month outside `1..12` |
| `teko: an hour is out of range` | an hour outside `0..23` |
| `teko: a minute is out of range` | a minute outside `0..59` |
| `teko: a second is out of range` | a second outside `0..59` |
| `teko: a millisecond is out of range` | a millisecond outside `0..999` |
| `teko: a date kind is out of range` | a `Kind` outside `0..2` at `new DateTime(ticks, kind)` |
| `teko: a month count is out of range` | `AddMonths` outside ±120000, C#'s own bound |
| `teko: a year count is out of range` | `AddYears` outside ±10000 |
| `teko: a time of day is out of range` | a tick count outside `0 .. 863999999999`: `new TimeOnly(ticks)` or `TimeOnly.FromTimeSpan(ts)` |

...and the `DateTimeOffset` half (N5, D76): sixteen bytes, `+0` a `DateTime` whose `Kind`
is always `Utc` (the instant), `+8` the offset in signed minutes. Every function below takes
and answers the `DateTimeOffset` STRUCT itself, never the raw halves — D75's rule for a wide
value's own member row — and reads or writes them through `tk_dto_instant`/`tk_dto_offmin`.
The calendar is not rewritten a third time: every component is `tk_dt_*` above, called on
the LOCAL reading (the instant plus the offset, an ordinary Unspecified tick count).

| signature | is |
|---|---|
| `DateTimeOffset tk_dto_make(i64 instant, i64 offmin)` | the two halves into one value, unchecked — every caller has already checked |
| `i64 tk_dto_instant(DateTimeOffset)` `i64 tk_dto_offmin(DateTimeOffset)` | the two halves back out |
| `i64 tk_dto_local_ticks(DateTimeOffset)` | the instant plus the offset: what `.Year` … `.Millisecond` and the text both read |
| `i64 tk_dto_check_offset(i64 ts)` | the offset validator: a whole minute, `-14:00 .. +14:00`; panics `teko: that UTC offset does not exist` otherwise |
| `DateTimeOffset tk_dto_make(i64 instant, i64 offmin)` | the one constructor every value passes; panics `teko: the local time of that DateTimeOffset is out of range` when instant + offset leaves the `DateTime` range |
| `DateTimeOffset tk_dto_min()` `tk_dto_max()` `tk_dto_unixepoch()` | `MinValue`, `MaxValue`, `UnixEpoch` |
| `DateTimeOffset tk_dto_new(i64 dt, i64 ts)` | `new DateTimeOffset(DateTime, TimeSpan)`: the `DateTime` read as a LOCAL reading at `ts`'s offset, the instant that minus the offset |
| `DateTimeOffset tk_dto_from_unix_seconds(i64)` `tk_dto_from_unix_ms(i64)` | the two `FromUnixTime*` builders, range-checked before the multiply |
| `i64 tk_dto_to_unix_seconds(DateTimeOffset)` `tk_dto_to_unix_ms(DateTimeOffset)` | their inverse |
| `i64 tk_dto_offset(DateTimeOffset)` | `.Offset`, minutes back into ticks |
| `i64 tk_dto_utcdatetime(DateTimeOffset)` `tk_dto_localdatetime(DateTimeOffset)` | `.UtcDateTime`; `.DateTime`/`.LocalDateTime` under one function, `Kind` Unspecified |
| `i64 tk_dto_year(DateTimeOffset)` `tk_dto_month` `tk_dto_day` `tk_dto_hour` `tk_dto_minute` `tk_dto_second` `tk_dto_ms` | the components, of the local reading, through `tk_dt_*` above |
| `DateTimeOffset tk_dto_to_offset(DateTimeOffset, i64 ts)` | `.ToOffset`: the instant kept, the offset changed |
| `DateTimeOffset tk_dto_add(DateTimeOffset, i64 t)` `tk_dto_sub_ts(DateTimeOffset, i64 t)` | `o + t`, `o - t`: the offset kept, the instant moved, through `tk_dt_add`/`tk_dt_sub_ts` |
| `i64 tk_dto_sub(DateTimeOffset, DateTimeOffset)` | `o - o`: a `TimeSpan`, through `tk_dt_sub` |
| `i64 tk_dto_eq(DateTimeOffset, DateTimeOffset)` `tk_dto_ne` `tk_dto_lt` `tk_dto_le` `tk_dto_gt` `tk_dto_ge` `tk_dto_cmp` | the instant only, offset ignored, through `tk_dt_eq` … `tk_dt_cmp` |
| `i64 tk_dto_put_num(ptr buf, i64 v, i64 w)` | zero-padded decimal digits, `w` wide — the inverse of `tk_dto_scan_num` |
| `str tk_dto_tostring_o(DateTimeOffset)` `tk_dto_tostring_s(DateTimeOffset)` `str tk_dto_tostring(DateTimeOffset)` `str tk_dto_tostring_fmt(DateTimeOffset, str f)` | the `"o"` (33 characters) and `"s"` (19) forms; `ToString()` is `"o"`; panics `teko: the DateTimeOffset format is not taught` on anything else |
| `i64 tk_dto_try_scan(str s, ptr utcp, ptr offp)` | the inverse of the two `tostring_*`; `"s"` reads as UTC, offset zero |
| `DateTimeOffset tk_dto_parse(str s)` | the value, or panics `teko: the string is not a DateTimeOffset` |
| `i64 tk_dto_tryparse(str s, out DateTimeOffset o)` | `1` and the value, or `0` and `MinValue` written through `o` |

`DTO_MAX_OFFSET_MIN` (840, `14 * 60`), `DTO_UNIX_EPOCH_SEC`, `DTO_UNIX_EPOCH_MS`,
`DTO_MAX_UNIX_SEC`, `DTO_MIN_UNIX_SEC`, `DTO_MAX_UNIX_MS` and `DTO_MIN_UNIX_MS` come with the
file, the same as the constants above.

---

## The wide libraries

`lib/decimal.tk`, `lib/guid.tk` and `lib/wide.tk` are what a `TK_WIDE` type owes the
**program**: a machine emits code for the program being compiled, and the three things a
sixteen-byte value needs are declarations of the program's own
([`teko_wide.tk`](../../teko_wide.tk), D74/D75).
Each file declares the same three, under its own names. `DateTimeOffset`'s own three are the
same shape, but live in `lib/time.tk` rather than in a file of their own — N5 folds the third
wide type into the include `DateTime`/`TimeSpan` already read, since there is no third
sixteen-byte FILE the way `decimal`/`Guid` each got one, only a third registration (D76).
`lib/wide.tk` is the one file that declares **two** sets, for `i128` and `u128`: the pair is
one include because the two types are one representation (N6a, D81).

| | |
|---|---|
| `decimal tk_dec_retbuf;` / `Guid tk_guid_retbuf;` / `DateTimeOffset tk_dto_retbuf;` / `i128 tk_i128_retbuf;` / `u128 tk_u128_retbuf;` | the buffer a sixteen-byte RETURN travels through. The callee copies its value here and returns the ADDRESS; the call site copies it out into the call depth's own slot immediately after the branch, which is what makes ONE buffer safe under recursion and under nesting. It is **per type**, so a program that returns a `Guid` and never mentions `decimal` includes `guid.tk` alone. A program that forgot it is refused by name: `teko: include "guid.tk" before returning a sixteen-byte value` |
| `decimal tk_dec_ld(ptr p)` / `Guid tk_guid_ld(ptr p)` / `DateTimeOffset tk_dto_ld(ptr p)` / `i128 tk_i128_ld(ptr p)` / `u128 tk_u128_ld(ptr p)` | the INDIRECT load: two `ld64`s over the address of the sixteen bytes. `tk_ldn` ([`teko_struct.tk`](../../teko_struct.tk)) lowers every field, array element, `ref`/`out` pointee and closure capture of a wide type to it, because no raw `ldW` moves sixteen bytes and the width table would answer `ld64` and move half the value in silence |
| `void tk_dec_st(ptr p, decimal d)` / `void tk_guid_st(ptr p, Guid g)` / `void tk_dto_st(ptr p, DateTimeOffset o)` / `void tk_i128_st(ptr p, i128 v)` / `void tk_u128_st(ptr p, u128 v)` | the store, the same two words the other way |

`&d` on a local or on a parameter of wide type is the address of its own sixteen-byte frame
slot — the core's own `MTASK_LOCAL_ADDR`, untouched by the wide machine — so surface code
reaches the halves with no accessor and no intrinsic.

### The limb library

`lib/limbs.tk` is the scratch vector every 128-bit arithmetic in this repository is written
over: eight 32-bit limbs held one per `u64` slot, and sixteen operations on them —
`tk_dv_zero`, `tk_dv_copy`, `tk_dv_at`/`tk_dv_put`, `tk_dv_is_zero`, `tk_dv_over96`,
`tk_dv_cmp`, `tk_dv_add`, `tk_dv_sub`, `tk_dv_inc`, `tk_dv_muladds`, `tk_dv_muls`,
`tk_dv_divs`, `tk_dv_shl1`, `tk_dv_mul` and `tk_dv_divmod`. It knows nothing about the type
above it: `lib/decimal.tk` and `lib/wide.tk` both include it, so `decimal` and
`i128`/`u128` share one long division rather than carrying two (D81).

The limbs are 32 bits and never 64 because `mc` compares every integer SIGNED, `u64`
included: `a < b` on two `u64` halves of a 128-bit value answers backwards the moment bit
63 is set. A limb below 2^32 has no such bit.

### The wide-integer library

`lib/wide.tk` is what `i128` and `u128` owe the program: the two return buffers
(`tk_i128_retbuf`, `tk_u128_retbuf`), the four indirect-access helpers (`tk_i128_ld`,
`tk_i128_st`, `tk_u128_ld`, `tk_u128_st`), and the arithmetic every operator and every cast
of the two types lowers to. One include carries the pair
([the specification](../specs/small-ints.md) § 6, N6a, D81).

Everything in it is ordinary teko over `lib/limbs.tk`'s eight 32-bit limbs, so no leg needs
a 128-bit instruction and `mc limits`' `intrin` row does not move. `mc`'s own bundled
`<i128>` is deliberately **not** included and never will be: measured on mc 0.17.2, it
cannot coexist with `<float>` (the opcode ranges collide on both instruction sets), it adds
four intrinsics, and its machine handlers break 34 of this repository's fixtures.

| | |
|---|---|
| `tk_i128_add`, `_sub`, `_mul`, `_div`, `_neg` | `+ - * /` and unary `-`. `+ - * -` wrap modulo 2^128 — C#'s unchecked default. `/` truncates toward zero and takes the sign from the two operands; `/ 0i` panics `teko: division by zero`, exit 70, and (D82) `MinValue / -1i` panics `teko: an integer division overflowed`, exit 70 rather than wrapping — the one quotient the other operations do not share |
| `tk_i128_mod` | `%`, truncating: `tk_dv_divmod` already leaves the remainder where it computed it (`lib/limbs.tk`), so this is `/`'s own magnitude split with the SIGN OF THE DIVIDEND alone on the way out — unlike `/`'s `sign(a) != sign(b)`. `% 0i` and `MinValue % -1i` panic the same two ways `/` does (D83, D82's ruling 7 amended) |
| `tk_i128_shl`, `tk_i128_shr` | `<<` and `>>`, `>>` ARITHMETIC (sign-filled). The count is the SAME wide type as the value (`tk_ops_promote`'s wide arm runs first), so the wrapper reads its own low 7 bits — `count & 127`, .NET's own `Int128` shift mask; a negative count falls out under the same mask (D83) |
| `tk_i128_and`, `_or`, `_xor`, `_not` | `& \| ^ ~`, limb-wise over the four live limbs |
| `tk_i128_cmp`, `_eq`, `_ne`, `_lt`, `_le`, `_gt`, `_ge` | the six comparisons, SIGNED: the sign bits decide first and the limbs after them |
| `tk_u128_*` | the same eighteen, with `/`, `%` and the four orderings UNSIGNED and `>>` LOGICAL (zero-filled) rather than arithmetic. `+ - *`, unary `-` and `& \| ^ ~` are the same bits either way and the wrappers differ only in the type they build |
| `tk_i128_from_i64` / `_from_u64` / `tk_u128_from_i64` / `_from_u64` | an integer widened. The SIGNED source sign-extends and the UNSIGNED one does not, which is why there are four and not two: a `u64` at or above 2^63 through a signed door is a negative 128-bit value, in silence (D77's ruling 8, the shape every wide type takes for this question) |
| `tk_i128_to_i64` / `tk_u128_to_i64` | back to 64 bits, truncating — C#'s own unchecked narrowing. `(u64) x` and `(i32) x` ride these too: the call answers an `i64` and the cast the source wrote narrows it |
| `tk_i128_from_u128` / `tk_u128_from_i128` | the two directions that move no bit at all |
| `tk_i128_lo`/`_hi`, `tk_u128_lo`/`_hi`, `tk_i128_of`, `tk_u128_of` | the two halves of the layout § 6 fixes, read off `&v` and written back |

`ToString`, `Parse`, `TryParse`, the members and the `decimal`/`f64` conversions are still
**N6b's own remainder** and are not here ([not-yet.md](not-yet.md)).

### The decimal library

`lib/decimal.tk` carries `decimal`'s own arithmetic on top of those three plus
`lib/limbs.tk`, and includes `rt.tk` itself for `panic` and for `tk_f64_bits`. Every operator and every conversion of
[`decimal`](types.md#decimal) lowers to one of the symbols below, and **none of them is an
instruction**: the whole of it is ordinary teko over 32-bit limbs held in `u64` locals — the
technique `mc`'s own `<i128>` uses for a literal — so no leg needs a 128-bit instruction and
`mc limits`' `intrin` row does not move (D77, [the specification](../specs/decimal.md) § 8).

A working value is eight limbs of 32 bits, 256 bits in all, which is what the widest
intermediate needs: an operand of `+` aligned by 10^28 (~2^189), the 192-bit product of `*`,
and the 96-bit divisor shifted by 96 that bounds the quotient of `/`. The vectors are LOCAL
arrays, so a nested call cannot find another call's scratch.

| | |
|---|---|
| `decimal tk_dec_add(decimal a, decimal b)` / `tk_dec_sub` | the scales align to the larger one; panics `teko: decimal overflow` |
| `decimal tk_dec_mul(decimal a, decimal b)` | the scales SUM, the mantissas multiply |
| `decimal tk_dec_div(decimal a, decimal b)` | the SMALLEST scale that preserves the result (C# § 12.9.3), at most 28 places, the quotient bounded by 96 bits; panics `teko: decimal division by zero` |
| `decimal tk_dec_rem(decimal a, decimal b)` | the sign of the DIVIDEND, the scale `max(sa, sb)`; the same panic on a zero divisor |
| `decimal tk_dec_neg(decimal a)` | bit 63 of the high word, flipped |
| `i64 tk_dec_cmp(decimal a, decimal b)` | `-1`/`0`/`1`: zero first (three patterns, one value), then the sign, then the magnitudes at a common scale |
| `i64 tk_dec_eq` … `tk_dec_ge` | the six comparisons, each `tk_dec_cmp` and a test |
| `decimal tk_dec_from_i64(i64 n)` | the IMPLICIT direction (C# §10.2.3), written by the compiler in every one of D33's nine slots, for every integer source but `u64` |
| `decimal tk_dec_from_u64(u64 n)` | the same direction from a `u64` SOURCE, whose magnitude an `i64` cannot hold: the limbs are filled from the two 32-bit halves and no sign question is asked (D77, ruling 8) |
| `i64 tk_dec_to_i64(decimal a)` | `(i64) d`, truncating toward zero; panics `teko: decimal overflow` outside `i64` |
| `f64 tk_dec_to_f64(decimal a)` | `(f64) d`; one rounding, not twenty-eight — the mantissa is divided once by the scale's own power of ten |
| `decimal tk_dec_from_f64(f64 x)` | `(decimal) x`, the double's own value rounded to fifteen significant digits with the trailing zeros dropped, which is why `(decimal) 0.1` is `0.1m` |

Everything a result goes through is `tk_dec_pack(uptr r, i64 scale, i64 sign)`: it reduces
the scale by dividing by ten until the value fits both 28 places and 96 bits, rounding
**half away from zero** on the last digit — the OPERATORS' rounding, C#'s own, and not
`decimal.Round`'s half to even, which is the table below. A value that cannot be reduced
because it is already at scale zero is the overflow this type is loud about.

The same file carries the API of [`decimal`](types.md#decimal) — every static and every
member is one of these, and none of them is an instruction either (D79,
[the specification](../specs/decimal.md) § 7):

| | |
|---|---|
| `decimal tk_dec_zero()` / `tk_dec_one` / `tk_dec_minusone` | `decimal.Zero`, `One`, `MinusOne`: a CALL and not a folded constant, because sixteen bytes have no `MTASK_CONST` |
| `decimal tk_dec_max()` / `tk_dec_min()` | `±79228162514264337593543950335m`, the three mantissa limbs full at scale zero |
| `decimal tk_dec_round_at(decimal d, i64 places)` / `tk_dec_round` | **half to even**, C#'s `MidpointRounding.ToEven`: the parity of what is kept is read off limb zero, because ten is even and a binary value shares the parity of its last decimal digit. The scale never grows; `places` outside `0..28` panics `teko: the decimal places are out of range` |
| `decimal tk_dec_intpart(decimal d, i64 way)` | `Truncate`, `Floor` and `Ceiling` in one function, told apart by the SIGN a discarded fraction grows the magnitude for; all three answer at scale 0 |
| `decimal tk_dec_abs(decimal d)` | the sign bit cleared, the scale untouched |
| `i64 tk_dec_signum(decimal d)` | `.Sign`, `-1`/`0`/`1` over the VALUE — `tk_dec_scale`/`tk_dec_sign` are the raw scale byte and the raw sign BIT, and `.Scale` is the first of those two |
| `i64 tk_dec_fmt(ptr buf, decimal d)` | the text into `buf` (32 bytes), NUL-terminated, returning the length: the allocation-free half, the split `<float_rt>` makes between `putf64` and `fmt_f64` |
| `str tk_dec_tostring(decimal d)` | `.ToString()`: the shortest exact form, trailing zeros kept, the buffer `rt_alloc`-owned |
| `str tk_dec_tostring_n(decimal d, i64 places)` | `.ToString(places)`, C#'s `"F<n>"`: rounded first, then padded with zeros in the TEXT — the mantissa never grows |
| `i64 tk_dec_scan(ptr p, str s)` | the grammar, three-valued: `1` and the value, `0` for text that is no decimal, `-1` for a number that is one and does not fit |
| `decimal tk_dec_parse(str s)` | `decimal.Parse(s)`: panics `teko: the string is not a decimal` or `teko: decimal overflow` |
| `i64 tk_dec_tryparse(str s, out decimal o)` | `decimal.TryParse(s, out d)`: `0`/`1`, `decimal.Zero` on failure, and never a panic |

`lib/math.tk` is C#'s other spelling of five of them — `Math.Round`, `Truncate`, `Floor`,
`Ceiling`, `Abs` over a `decimal` — and is the one library file that needs **no mechanism
at all**: a static method on a declared class is a construct this compiler already runs, so
nothing there is registered and `mc limits` does not move for it.

`lib/guid.tk` carries `Guid`'s own surface code on top of those three, and needs `rt.tk` for
`rt_alloc` and `rt_panic`. Every member and every operator of [`Guid`](types.md#guid) lowers
to one of them.

| | |
|---|---|
| `Guid tk_guid_empty()`, `i64 tk_guid_isempty(Guid g)` | the all-zero value, and the one comparison against it |
| `Guid tk_guid_parse(str s)` | the `"D"` and `"N"` forms, either case; panics `teko: the string is not a Guid` |
| `i64 tk_guid_tryparse(str s, uptr o)` | `1` and the value, or `0` and `Guid.Empty` written through `o` |
| `str tk_guid_tostring(Guid g)`, `str tk_guid_tostring_fmt(Guid g, str f)` | 36 characters of the `"D"` form, or `"N"`'s 32; the buffer is `rt_alloc`'d and the caller keeps it, exactly as `tk_enum_digits` hands one back |
| `i64 tk_guid_fmt(ptr buf, Guid g, i64 dash)` | the allocation-free half, returning the length. `buf` needs 37 bytes with `dash`, 33 without |
| `i64 tk_guid_cmp(Guid a, Guid b)` and `tk_guid_eq`…`tk_guid_ge` | the sixteen bytes, UNSIGNED, left to right, stopping at the first difference |

---

## Layout the runtime assumes

| | |
|---|---|
| an object | vtable at +0, reference count at +8, fields from +16 |
| a vtable | the class's release function at +0, its interface table (or 0) at +8, virtual slots from +16 |
| an interface table | a count, then one `(id, methods)` row per interface the class declares |
| a delegate value | vtable, count, code pointer at +16, then one word per capture |
| a `T[]` | vtable, count, `Length` at +16, elements from +24 |

The arena is 4 MiB with a free list per 16-byte size class up to 256 bytes
([memory.md](memory.md)).
