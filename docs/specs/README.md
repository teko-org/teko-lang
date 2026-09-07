# The specs

**Designed, not built** — kept apart from [the guide](../guide/README.md) and
[the reference](../reference/README.md), which describe only what runs today. A reader
looking up a construct must never find a plan described as if it worked.

| page | covers |
|---|---|
| [surface.md](surface.md) | the policy: where a form comes from, what the surface will not become, and the closed list of what has no surface code |
| [packages.md](packages.md) | one integrated registry with `mc`, what makes a package a teko package, and how a consumer pins the compiler and the library |
| [self-hosting.md](self-hosting.md) | the single unit, and why the criterion is the object rather than the executable |
| [dependency-injection.md](dependency-injection.md) | the DI model as decided, including the parts that are not built |
| [params-typed.md](params-typed.md) | `params T[]`, the typed variadic list — **built**, the reference describes it; the page is kept for the steps still open and for what the flip measured |
| [small-ints.md](small-ints.md) | `i8`, `i16`, `i128` and `u128` — the integers C# has and teko does not, and the `tk_is_int_ty` predicate they change |
| [enum.md](enum.md) | `enum` with an underlying type, a distinct type of its own, the bitwise operators and a `switch` over the names |
| [guid.md](guid.md) | `Guid` — sixteen bytes, `Parse`/`ToString`, ordering, and the one function blocked on `mc` |
| [string.md](string.md) | `string` as a counted class beside `str`, the interned literal, value equality, indexing, and interpolation blocked on `mc`'s lexer |
| [datetime-extras.md](datetime-extras.md) | `DateOnly`, `TimeOnly` and `DateTimeOffset` — a proposed section of the `DateTime` page, kept separate so two branches do not conflict |
| [roadmap-1.0.md](roadmap-1.0.md) | **a draft**: what v1.0.0 should require, and what of it depends on `mc` |

## The order the type work lands in

The four pages above, together with [decimal.md](decimal.md) and [datetime.md](datetime.md),
are **one sequence**, cheapest first. Each crumb lands
on its own and is gated on its own fixtures; the dependency column is the only thing that
fixes the order.

| # | crumb | page | size | depends on |
|---|---|---|---|---|
| 1 | **N0** `i8`/`i16` and the `tk_is_int_ty` predicate | small-ints.md | S | — |
| 2 | **N1b** a constant that does not fit its slot (optional) | small-ints.md | S | N0 |
| 3 | **N2a** `enum`: the type, the operators, the `switch` | enum.md | M | N0 |
| 4 | **P0** the probes | `docs/specs/datetime.md` | S | — |
| 5 | **C1** `TimeSpan`, and the primitive-member mechanism | `docs/specs/datetime.md` | L | P0 |
| 6 | **N2b** `enum`: `ToString`, `Parse`, the statics | enum.md | S | N2a, C1 |
| 7 | **C2** `DateTime` | `docs/specs/datetime.md` | L | C1 |
| 8 | **N2c** `DateTimeKind` becomes an `enum` | enum.md | S | N2a, C2 |
| 9 | **N4** `DateOnly` and `TimeOnly` | datetime-extras.md | M | C2 |
| 10 | **C3** the sixteen-byte value and `teko_wide.tk` | `docs/specs/decimal.md` | M | C1 |
| 11 | **N3** `Guid` | guid.md | M | C3, C1 |
| 12 | **N5** `DateTimeOffset` | datetime-extras.md | M | C2, C3 |
| 13 | **C4** the `decimal` arithmetic | `docs/specs/decimal.md` | L | C3 |
| 14 | **C5** `decimal` round and text | `docs/specs/decimal.md` | M | C4 |
| 15 | **N6** `i128` and `u128` | small-ints.md | L | C3, C4 |
| 16 | **N7** `string`, the value | string.md | L | — |
| 17 | **N8** `string`, the methods and the index | string.md | L | N7 |
| 18 | **C6** `DateTime.Now`, `UtcNow`, `Today` | `docs/specs/datetime.md` | S | **blocked**: a wall clock in `mc`'s `<sys>` |
| 19 | **N9** `Guid.NewGuid` | guid.md | S | **blocked**: an entropy source in `mc`'s `<sys>` |
| 20 | **N10** `$"..."` interpolation | string.md | M | **blocked**: `mc`'s lexer refusing `$` before `"` |
| 21 | **C7** the native wide instructions (optional, speed only) | `docs/specs/decimal.md` | S | C4 |

**N7 and N8 depend on nothing** and are placed late only because they are the most
expensive; they can be pulled forward at any point without moving anything else. **N0 is
first for a reason that is not its size**: it fixes the predicate that decides what an
integer is, and it has to be right before a second `TK_SINT` type exists.

`object` and `Nullable<T>` are outside this sequence and
[string.md](string.md) § 11 says where they enter and what each one waits on.


What v0.4.0 **refuses** is not here: it is catalogued in
[`../reference/not-yet.md`](../reference/not-yet.md), with the message each refusal answers.
Nothing on that list is a promise about a later version, and nothing on this one is a
schedule.
