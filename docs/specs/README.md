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
| [roadmap-1.0.md](roadmap-1.0.md) | **a draft**: what v1.0.0 should require, and what of it depends on `mc` |

What v0.4.0 **refuses** is not here: it is catalogued in
[`../reference/not-yet.md`](../reference/not-yet.md), with the message each refusal answers.
Nothing on that list is a promise about a later version, and nothing on this one is a
schedule.
