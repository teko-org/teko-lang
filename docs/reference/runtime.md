# The runtime

Two library files, both **program** code compiled under the same taught vocabulary as the
program that includes them: `lib/rt.tk`, the runtime every program links against, and
[`lib/time.tk`](#the-time-library), the surface code every `TimeSpan` and `DateTime` member
and operator lowers to.

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
| `i64 tk_enum_value(uptr names, uptr vals, i64 n, str s)` | the value of the member named `s`, or `-1` |
| `i64 tk_enum_isdefined(uptr vals, i64 n, i64 v)` | 1 when some member carries `v` |
| `i64 tk_enum_parse(uptr names, uptr vals, i64 n, str s, str msg)` | `tk_enum_value`, or `panic(msg)` (exit 70) when it answers `-1` |
| `i64 tk_enum_tryparse8/16/32/64(uptr names, uptr vals, i64 n, str s, uptr outp)` | 0/1, writing the value into `outp` at the width named — one per underlying width an enum may declare |
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
| `i32 tk_dt_kind(i64)` | `.Kind`, as a `DateTimeKind` |
| `i32 tk_dtk_unspecified()` `tk_dtk_utc()` `tk_dtk_local()` | the three `DateTimeKind` values |
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

Five constants come with the file, and a program may read them: `TICKS_PER_MILLISECOND`,
`TICKS_PER_SECOND`, `TICKS_PER_MINUTE`, `TICKS_PER_HOUR`, `TICKS_PER_DAY`, plus
`TIMESPAN_MAX_TICKS` and `TIMESPAN_MIN_TICKS` — and the date half adds
`DATETIME_MAX_TICKS`, `UNIX_EPOCH_TICKS`, `DATETIME_TICK_MASK`, `DAYS_PER_YEAR`,
`DAYS_PER_4_YEARS`, `DAYS_PER_100_YEARS`, `DAYS_PER_400_YEARS` and the four `DT_*`
selectors. They are ordinary top-level `const i64`, so
a program that declares one of those names itself collides with it.

Fourteen panics live here, all exit **70**:

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
