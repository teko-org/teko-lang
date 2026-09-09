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
| [datetime.md](datetime.md) | `DateTime` and `TimeSpan`: the tick, the members a primitive gets, and `Now` as teko's own `extern` per host — **both types and the mechanism are built** ([timespan.md](../reference/timespan.md), [datetime.md](../reference/datetime.md)); what is left of the page is the text half and `Now` |
| [decimal.md](decimal.md) | `decimal`: C#'s 128-bit exact base-ten number, as a sixteen-byte primitive that moves by address |
| [params-typed.md](params-typed.md) | `params T[]`, the typed variadic list — **built**, the reference describes it; the page is kept for the steps still open and for what the flip measured |
| [small-ints.md](small-ints.md) | `i8`, `i16`, `i128` and `u128` — the integers C# has and teko does not, and the `tk_is_int_ty` predicate they change |
| [enum.md](enum.md) | `enum` with an underlying type, a distinct type of its own, the bitwise operators and a `switch` over the names |
| [guid.md](guid.md) | `Guid` — sixteen bytes, `Parse`/`ToString`, ordering, and `NewGuid` over teko's own entropy `extern` |
| [string.md](string.md) | `string` as a counted class beside `str`, the interned literal, value equality, indexing, and interpolation over the `$` token `mc` 0.15.25 lets a module claim |
| [nullable.md](nullable.md) | `T?` — one nullable mechanism over any type, reference or value: the handle, the box, `HasValue`/`Value`/`??`/`?.`, definite assignment, and the migration `null` outside a `T?` slot forces — **all of it is built** ([nullable.md](../reference/nullable.md), D43/D44/D45/D46); what is left of the page is the lifted `==` (Q4a) |
| [datetime-extras.md](datetime-extras.md) | `DateOnly`, `TimeOnly` and `DateTimeOffset` — a proposed section of the `DateTime` page, kept separate so two branches do not conflict |
| [roadmap-1.0.md](roadmap-1.0.md) | **a draft**: what v1.0.0 should require, and what of it depends on `mc` |

## The order the type work lands in

The four pages above, together with [decimal.md](decimal.md) and [datetime.md](datetime.md),
are **one sequence**, cheapest first. Each crumb lands
on its own and is gated on its own fixtures; the dependency column is the only thing that
fixes the order.

| # | crumb | page | size | depends on |
|---|---|---|---|---|
| 1 | ~~**N0** `i8`/`i16` and the `tk_is_int_ty` predicate~~ **landed**, D38 | small-ints.md | S | — |
| 2 | **N1b** a constant that does not fit its slot (optional) | small-ints.md | S | N0 |
| 3 | ~~**N2a** `enum`: the type, the operators, the `switch`~~ **landed**, D39 | enum.md | M | N0 |
| 4 | ~~**P0** the probes~~ **landed**, D40 | `docs/specs/datetime.md` | S | — |
| 5 | ~~**C1** `TimeSpan`, and the primitive-member mechanism~~ **landed**, D40 | `docs/specs/datetime.md` | L | P0 |
| 5a | ~~**Q0** the nullable probes~~ **landed**, D43 | `docs/internals/nullable-probes.md` | S | — |
| 5b | ~~**Q1a** `T?` over a reference, and `null` only in a `T?` slot~~ **landed**, D43 | `docs/reference/nullable.md` | M | Q0 |
| 5c | ~~**Q1b** `T?` over a value (the counted box)~~ **landed**, D44 | `docs/reference/nullable.md` | M | Q1a |
| 5d | ~~**Q2** `??` and `?.`~~ **landed**, D45 | `docs/reference/nullable.md` | M | Q1b |
| 5e | ~~**Q3** definite assignment~~ **landed**, D46 | `docs/reference/nullable.md` | M | Q0 |
| 6 | ~~**N2b** `enum`: `ToString`, `Parse`, the statics~~ **landed**, D47 | enum.md | S | N2a, C1 |
| 7 | ~~**C2** `DateTime`~~ **landed**, D41 | `docs/specs/datetime.md` | L | C1 |
| 8 | ~~**N2c** `DateTimeKind` becomes an `enum`~~ **landed**, D48 | enum.md | S | N2a, C2 |
| 9 | **N4** `DateOnly` and `TimeOnly` | datetime-extras.md | M | C2 |
| 10 | **C3** the sixteen-byte value and `teko_wide.tk` | `docs/specs/decimal.md` | M | C1 |
| 11 | **N3** `Guid` | guid.md | M | C3, C1 |
| 12 | **N5** `DateTimeOffset` | datetime-extras.md | M | C2, C3 |
| 13 | **C4** the `decimal` arithmetic | `docs/specs/decimal.md` | L | C3 |
| 14 | **C5** `decimal` round and text | `docs/specs/decimal.md` | M | C4 |
| 15 | **N6** `i128` and `u128` | small-ints.md | L | C3, C4 |
| 16 | **N7** `string`, the value | string.md | L | — |
| 17 | **N8** `string`, the methods and the index | string.md | L | N7 |
| 18 | **C6** `DateTime.Now`, `UtcNow`, `Today` | `docs/specs/datetime.md` | S | C2 — a wall-clock `extern` per target host, teko's own (the owner's ruling, 2026-09-08) |
| 19 | **N9** `Guid.NewGuid` | guid.md | S | N3 — an entropy `extern` per target host and a `bcrypt.def` in teko's Windows sysroot, teko's own |
| 20 | **N10** `$"..."` interpolation | string.md | M | N8, and a pin at `mc` ≥ 0.15.25, where `$` before `"` is a token a module claims with `syntax_expr("$", …)` |
| 21 | **C7** the native wide instructions (optional, speed only) | `docs/specs/decimal.md` | S | C4 |

**N7 and N8 depend on nothing** and are placed late only because they are the most
expensive; they can be pulled forward at any point without moving anything else. **N0 is
first for a reason that is not its size**: it fixes the predicate that decides what an
integer is, and it has to be right before a second `TK_SINT` type exists.

**`Nullable<T>` is no longer outside this sequence.** The owner's ruling of 2026-09-08 —
one nullable mechanism, `T?` over any type — puts [nullable.md](nullable.md)'s crumbs
(rows 5a–5e above: Q0, Q1a, Q1b, Q2, Q3) **ahead of every row that has not landed**, and mandatorily
ahead of **N7**: `string` is a counted class, so `string? s = null;` has to be the spelling
from its first day rather than a second migration. `decimal?` and `Guid?` then cost C3 and
N3 one row each. That supersedes [string.md](string.md) § 11 on `Nullable<T>`; on `object`,
which runs into D4, § 11 still stands.


What v0.4.0 **refuses** is not here: it is catalogued in
[`../reference/not-yet.md`](../reference/not-yet.md), with the message each refusal answers.
Nothing on that list is a promise about a later version, and nothing on this one is a
schedule.
