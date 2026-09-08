# The reference

Exhaustive, read by lookup: every construct v0.4.0 accepts, with its spelling, its
semantics, its known limit and a whole program that proves it. What the language refuses is
here too — as a page of its own and as the catalogue of every message.

Every fenced `teko` example on these pages is **compiled and run** by
[`../../scripts/check-docs.sh`](../../scripts/check-docs.sh), and its `// expect-exit: N`
header is the assertion. Most of them are derived from the fixtures in
[`tests/`](https://github.com/teko-org/teko-lang/tree/main/tests), which the five native
CI legs run on every push.

| page | covers |
|---|---|
| [types.md](types.md) | the scalars, teko's seven aliases, `f32`/`f64`, `ptr`/`uptr`/`str`, `struct`, `class`, `enum`, members, modifiers, `static`, `const` |
| [timespan.md](timespan.md) | `TimeSpan`: the tick, the builders, the components against the totals, the operators and what a primitive with members refuses |
| [datetime.md](datetime.md) | `DateTime` and `DateTimeKind`: the calendar constructors, the components, the `Add*` family, the operators mixing dates and spans, and what a date refuses |
| [classes.md](classes.md) | inheritance, the implicit receiver, `base`, constructors and destructors, `abstract`, interfaces, traits, properties, operators, `partial`, free order of declaration |
| [generics.md](generics.md) | `class Box<T, const N: i64>`, instantiation and mangling, inline array fields, partial generics |
| [delegates.md](delegates.md) | `delegate`, contextual and explicit values, the null panic, lambdas, `use (...)` by value and by reference |
| [arrays.md](arrays.md) | fixed arrays local, global and inline; the heap `T[]`, its run-time index guard and its counted elements |
| [nullable.md](nullable.md) | `T?` over a reference: the rule that `null` needs a `T?` slot, `HasValue`/`Value`, the conversions, the overloads and the reclaim |
| [namespaces.md](namespaces.md) | `namespace`, `using`, `import`, how a bare name resolves, `internal` |
| [control-flow.md](control-flow.md) | `if`, `loop`/`break N`/`continue N`, `while`, `do`, `for`, `foreach`, both `switch` spellings, the ternary |
| [parameters.md](parameters.md) | default arguments, overloads, `ref`/`out`, `params` |
| [di.md](di.md) | the three lifetime markers, `inject`, constructor injection, `scope { }` |
| [memory.md](memory.md) | the arena, reference counting per scope, ownership, destructors, panics |
| [runtime.md](runtime.md) | everything `lib/rt.tk` exports, by signature |
| [build.md](build.md) | `teko build`, `teko limits`, `teko.toml`, `mc.toml`, the fixed point |
| [diagnostics.md](diagnostics.md) | every `teko: …` message, by family, with cause and fix |
| [not-yet.md](not-yet.md) | what v0.4.0 refuses, and the message each refusal answers |

Start at [types.md](types.md) if you are reading in order; the [guide](../guide/README.md)
is the task-oriented route, and [`../specs/`](../specs/README.md) holds what is designed
and not built.
