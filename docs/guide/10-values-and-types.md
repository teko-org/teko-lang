# Values and types

Everything a teko program can name before it declares a type of its own: the scalars, the
seven words teko adds on top of them, floats, strings, the opaque pointer, and the two
array shapes.

## The scalars

`u8`, `u16`, `u32`, `u64`, `i32`, `i64`, `uptr` and `void` come from `mc`'s core.
`i64` is the working integer: an integer literal is one, arithmetic happens on 64 bits, and
a comparison yields `0` or `1`.

Teko adds seven **aliases** — new words for a representation the core already has, so
mixing an alias with its base needs no cast and costs no instruction.

| word | is | for |
|---|---|---|
| `bool` | `u8` | `true` and `false`, which are the literals `1` and `0` |
| `byte` | `u8` | a byte |
| `char` | `u32` | one code point; `'a'`, `'\n'` |
| `isize` / `usize` | `i64` / `u64` | an index or a size |
| `ptr` | `uptr` | teko draws no signed/unsigned pointer distinction |
| `str` | `uptr` | a NUL-terminated string, by pointer |

A cast is C-shaped and unambiguous, `(usize) b`: narrowing masks, widening is zero for the
unsigned words and **sign** for `i32`. Every type word is reserved program-wide, so
`i64 str = 1;` is refused the way C# refuses a variable named `int`.

## Floats

`f32` and `f64` come from the `<float>` library the taught compiler loads, with their own
literals (`2.0`, `0.5f`). A cast between a float and an integer converts the **value**;
the bit pattern is reached through `tk_f64_bits`/`tk_f64_from_bits`.

## Strings and pointers

`str` is a pointer to bytes ending in a NUL — there is no string object and no length
field, so `tk_str_len` walks to the NUL and `tk_str_slice` is a view that copies nothing.
Memory behind a `ptr` is read and written by explicit width (`ld8`…`ld64`,
`st8`…`st64`), `&x` takes the address of a local, a global or a function, and `p + 1` is
one **byte** further: a pointer has no pointee to scale by.

Those names come from the runtime, so a program that touches them opens with
`#include "rt.tk"` ([the runtime reference](../reference/runtime.md)).

## Time

`TimeSpan` and `DateTime` are C#'s, behind one include of their own:

```
#include "time.tk"          // brings rt.tk with it
```

A `TimeSpan` is a length of time in 100-nanosecond ticks; a `DateTime` is a point in time,
a tick count from `0001-01-01` with a `Kind` (`Unspecified`, `Utc`, `Local`) carried along.
Both are eight bytes, both are types of their own — no integer becomes one without
`new DateTime(t)` and neither becomes an integer without `.Ticks` — and the arithmetic
between them is C#'s: `b - a` is a `TimeSpan`, `d + t` is a `DateTime`, and everything past
the range panics instead of wrapping.

```
DateTime leap = new DateTime(2024, 2, 29);
DateTime next = leap.AddDays(1);               // 2024-03-01
TimeSpan gap  = next - leap;                   // one day, 864000000000 ticks
```

`DateTime.Now` is not taught: a wall clock is one symbol per operating system and belongs
to `mc`. [`timespan.md`](../reference/timespan.md) and
[`datetime.md`](../reference/datetime.md) are the two reference pages.

## The two array shapes

They are different types, and the difference is where the length lives.

| | fixed `T a[N]` | heap `T[] xs` |
|---|---|---|
| length | written in the source, an integer literal or a `const` | any expression, at `new T[n]` |
| lives | where it is declared: a local, a global, a field | an object of its own, reference-counted |
| elements | scalars only | any type, objects included |
| `a.Length` | on a local (not on a global) | always |
| an index out of range | checked when it is a literal, unguarded when computed | **always** checked: a panic, exit 70 |

`new T[n]` hands out zeroed elements, and a negative `n` panics.

## One program

```teko
// expect-exit: 42
#include "rt.tk"

u8 tbl[8];                                   // a global fixed array

i64 main() {
    byte  b = 20;
    char  c = 12;
    usize u = (usize) b + (usize) c;         // 32, no instruction spent on the aliases
    bool ok = u == 32;
    if (ok == false) return 1;

    str s = "hello";
    if (tk_str_len(s) != 5) return 2;
    if (tk_str_len(tk_str_slice(s, 3)) != 2) return 3;

    st64(tbl, 7);                            // eight bytes at the array's own address
    ptr p = tbl;
    if (ld64(p) != 7) return 4;

    i64 a[4];                                // fixed: zeroed, length in the source
    for (i64 i = 0; i < a.Length; i++) {
        a[i] = i;
    }

    i64[] xs = new i64[3];                   // heap: length carried, every index guarded
    xs[0] = 4;
    xs[1] = 9;
    if (xs.Length != 3) return 5;

    f64 sum = 0.5 + 5.5;
    i64 six = (i64) sum;                     // the value, not the bits

    return (i64) u + a[3] + xs[0] + xs[1] - six;   // 32 + 3 + 4 + 9 - 6
}
```

The exhaustive account is [types.md](../reference/types.md) and
[arrays.md](../reference/arrays.md); what `#include "rt.tk"` brings in is
[runtime.md](../reference/runtime.md).
