# `params T[]`: a typed variadic list

**Designed, not built.** What runs today is
[the parameters reference](../reference/parameters.md) § `params`: a list of machine
**words**, instantiated once per argument count. This page is the design that replaces it
with C#'s own form, a real heap array.

It needs nothing new from `mc`. The parameter position and the `T[]` type suffix are both
already reachable, and `params` stops being the parameter's **type** and becomes a
**modifier** read before the type — exactly the shape `ref` and `out` already have at both
parameter positions.

## The semantics, as C# has them

**Declaration.** `void f(params i64[] xs)` — the last parameter, one only, never `ref` or
`out`, never with a default, never on an `extern`. The type is a genuine `T[]`.

**Body.** `xs` is an ordinary `T[]`: `xs.Length`, `xs[i]`, `xs[i] = e`,
`foreach (T v in xs)`, passing it on, returning it. Nothing new is needed for any of that —
the heap-array machinery already answers for a `T[]` parameter at every one of those sites,
and the type oracle types `xs` like any other parameter.

**Call, expanded form.** N arguments of type `T`, N ≥ 0; the compiler builds the array. Zero
arguments is an array of length 0, never null.

**Call, normal form.** A single argument that is already a `T[]` passes **straight through,
without a copy**. Surfacing a form that was a view must never regress into a copy.

**Element types.** A scalar (width and sign from the declared element type), a **float**
(declared float in every slot it crosses — which is what closes today's "a float argument is
not taught" refusal by making it work rather than by refusing it), a class, an interface or
a delegate (the array owns the references: an increment on the store, a release per slot),
and `str`/`ptr`, identical to a `uptr[]` and uncounted.

**Overload resolution.** A candidate applicable in **normal** form beats an expanded one, as
in C#. That falls out of the existing rounds: the two exact rounds match `pick(1)` against
`pick(i64)` before the new round runs. Element conversion is identity or derived-to-base;
an integer literal lands in any integer of the core; there is **no** implicit numeric
conversion, so `total(1)` against `params f64[]` is a refusal.

## The mechanism

A sibling of the existing `ref`/`out` reader, called at both parameter positions before the
type is read:

- `params` in front of a type is the modifier; the type after it must be a `T[]`, or the
  declaration is refused with a message that shows the right spelling.
- the parameter node is **marked**, keyed by node, the way a `ref`/`out` parameter already
  is; a table derived in the pass carries `(name, fixed count, row type, element type)`.
- the word `params` keeps its own type identity, which is what lets the check for `params`
  written outside parameter position go on working; no parameter node carries that id any
  more.

**At the call site**, a chain of expressions, the same shape the closure captures and the
current `params` block already use — a store is a statement and the whole construction has
to be one expression, so each link hands the array back to the next:

```mc
total(a, b)  ->  total(tkarr_put_i64(tkarr_put_i64(tkarr_new_i64(2), 24, a), 32, b))
```

`tkarr_put_T` is **generated per element type**, beside the allocator and release the heap
array already generates, behind a flag of its own — only a program that uses `params` pays
for it, and the tree of one that does not is unchanged. The element type is **declared** in
the generated parameter (a float must be declared float all the way down) and the offset is
a constant at the site; the increment is emitted only when the element is counted.

**Ownership** needs no new machinery. A `T[]` is counted, the node the allocator returns is
born owned, and an owned value in argument position is already parked and swept at the end of
the statement, taking each counted element with it through the generated release. The outer
node of the chain is registered as **borrowed** so the park happens once, at the allocator
inside — registering it as owned would release the array twice.

**Interactions.** `ref`/`out` and defaults do not mix with it; the type oracle needs no new
branch; DI is untouched. `&total` becomes legal, so the refusal that a `params` function has
no address disappears.

## Where the pass moves to

`params` is a pass today, and it **moves** rather than multiplies — the pass count must not
change. It goes from before the type oracle to **after the operator pass and before the
default fill**:

- **after the oracle, `ref`, delegates and operators**, because those are what give an
  argument its type. A delegate call is only a typed `callp` with its cast attached once the
  delegate pass has run, and an overloaded operator is only a typed call once the operator
  pass has.
- **before defaults and overloads**, which census by **arity** — the call has to already have
  its single argument — and before reference counting, which is what parks the array.

One consequence: the reason the global-array pass has to run ahead of the `params` pass
dissolves, because the index resolves at parse time instead. That ordering note is corrected
in the same change, with a probe that puts an indexed global array and a `params` call in one
program.

The pass walks in **post-order**, so `total(total(1, 2), 3)` packs the inner call first, and
it detaches each argument from its sibling list before making it an argument of a put.

## What the change removes

The whole word-list machine: the per-count instantiation, the element read, the length
constant, the float refusals, and the three runtime helpers that back it
(`tk_va_new`/`tk_va_put`/`tk_va_at`). The per-site ceiling of twelve disappears with them —
the tail goes to **memory**, not to the ABI, so only the fixed parameters have to fit the
call convention.

The two debts it closes are [the internal ones](../internals/debts.md): `params` carrying
words rather than a type, and the argument block never being handed back. A `T[]` is counted,
so the array is released at the end of the statement that built it.

## Refusals the design adds

| written | message |
|---|---|
| the old spelling, `params xs` | ``teko: `params` names an array type: write `params T[] xs` `` |
| not last, or two of them | ``teko: `params` must be the last parameter, and there is only one`` |
| `params ref i64[] xs` | ``teko: a `params` list is not `ref` or `out` `` |
| `params i64[] xs = …` | ``teko: a `params` list has no default`` |
| on a method or a constructor | ``teko: `params` is taught on a free function only`` |
| on an `extern` | ``teko: an `extern` symbol takes no `params` list`` |
| an argument that does not convert | the existing element-conversion message |
| `params i64[][] xs` | the existing "an array of arrays is not taught yet" |
| more arguments than the site's ceiling | ``teko: too many arguments for a `params` list`` |

## Deliberately outside this design

**`params` on a method or a constructor.** Five call-shaping sites and a vtable slot keyed by
signature are a different piece of work. Today the virtual path is quietly wrong; refusing it
trades a silent hole for a message, which is the right trade while the rest lands.

**A generic `params`, `T[][]`, a lambda as an element, and a default plus expansion in the
same call.** None is refused on principle; none is part of this design.

**Implicit numeric conversion of an element.** C# has it; teko does not, here or anywhere
else in overload resolution.
