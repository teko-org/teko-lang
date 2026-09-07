# `params T[]`: a typed variadic list

**Built, and what runs today** is [the parameters reference](../reference/parameters.md)
§ `params`. This page is the design it was built from; the steps stand as
**P0 ✔** (the probes), **C1 ✔** (the element store), **C2 ✔** (the flip: the modifier, the
call site, the pass move, and the removal of the word list), **C2b ✔** (the ownership
registration corrected: borrowed is not pure) and **C3 ✔** (overload resolution with a list
among the candidates). What the flip and the resolution measured, and where each diverged,
is recorded at the bottom of this page.

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
in C# (§12.6.4.5). That falls out of the round ORDER: the two exact rounds, and then the
default-completing one, all run before a list is ever asked to swallow a tail — so `f(1)`
against `f(i64)` and `f(params i64[])` is the first, and so is `f(1)` against
`f(i64 a, i64 b = 5)`. Element conversion is identity or derived-to-base; an integer literal
lands in any integer of the core; there is **no** implicit numeric conversion, so `total(1)`
against `params f64[]` is a refusal.

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
for it, and the tree of one that does not is unchanged. It is
`T[] tkarr_put_T(uptr a, i64 off, T v)`, one store and the array handed back, the shape
`tk_cap_put` already gives a closure's captures. The element type is **declared** in the
generated parameter (a float must be declared float all the way down) and the second
argument is a byte **offset**, `24 + k * width`, constant at the site rather than an index;
the increment is emitted only when the element is counted. Nothing is added to `lib/rt.tk`
for any of it: `tk_stn` already answers `st8`/`st16`/`st32`/`st64`/`stf32`/`stf64` for every
element kind, and `rt_own` is the increment a counted one earns.

**Ownership** needs no new machinery, and it is **one reference for the whole chain** — the
count the allocator was born with. Neither end may add to it or take from it, so the rule has
two halves and both are needed:

- the **call site** registers the OUTER node as **borrowed**, so the park happens once, at
  the allocator inside — registering it as owned parks the same array once per link, and the
  statement's sweep then releases it as many times as it parked it (`teko: reference count
  below zero`, measured). **Borrowed is the ownership answer and nothing else**: a link is a
  store, so it is not pure, and the two are separate columns of `tk_xt` (C2b below);
- the **generated store** registers the name it returns as **owned**, because
  `tk_rc_return` increments a *borrowed* value on the way out of a function whose declared
  return type is counted. An increment per link is the mirror defect: a reference the sweep
  never gives back, and the array leaks (measured as well).

Past those two, a `T[]` is counted, the node the allocator returns is born owned, and an
owned value in argument position is already parked and swept at the end of the statement,
taking each counted element with it through the generated release.

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
constant, the float refusals, and the three runtime helpers of `lib/rt.tk` that backed it.
The per-site ceiling of twelve disappears with them — the tail goes to **memory**, not to
the ABI, so only the fixed parameters have to fit the call convention.

The two debts it closed were [internal ones](../internals/debts.md): `params` carrying
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

## Deliberately outside this design

**`params` on a method or a constructor.** Five call-shaping sites and a vtable slot keyed by
signature are a different piece of work. Today the virtual path is quietly wrong; refusing it
trades a silent hole for a message, which is the right trade while the rest lands.

**A generic `params`, `T[][]`, a lambda as an element, and a default plus expansion in the
same call.** None is refused on principle; none is part of this design.

**Implicit numeric conversion of an element.** C# has it; teko does not, here or anywhere
else in overload resolution.

---

## What C2 measured, and where it diverged

Every refusal above is in force, with three corrections and two additions the flip made:

* **The old spelling and a non-array type are one message.** `params xs`, `params i64 xs`
  and `params ref i64[] xs` are all "the type after the modifier is not a `T[]`", so the
  first two answer ``teko: `params` names an array type: write `params T[] xs``` and only
  the `ref`/`out` case has one of its own. A separate message for the retired spelling would
  be a message about a form the surface no longer has.
* **There is no ceiling to refuse.** ``teko: too many arguments for a `params` list`` and
  ``teko: too many parameters before `params``` are gone with the words: the tail is memory,
  so only the declaration's own parameter list meets `MAXPARAMS`, through the core's own
  check. A site passing fourteen arguments is in the fixture.
* **A prototype needs no body.** The list is an ordinary `T[]` parameter now, so a
  declaration without a definition is the linker's business like any other; the refusal
  that a `params` list "needs a body" went with the instantiation.
* **`params` on a method, a constructor, an interface signature or a `delegate`** is one
  refusal, ``teko: `params` is taught on a free function only``. The first three are caught
  where the member's parameter list is read (`tk_params`, `teko_class.tk`); a `delegate` and
  a lambda go through the core's own `parse_params`, so they reach the same
  `syntax_param` handler and are caught in the pass instead, as a marked parameter no
  declaration of the unit claimed.
* **The overload refusal stayed for C2 only.** ``teko: a `params` list cannot be
  overloaded`` was what kept the flip from expanding a call the overload resolution had not
  decided yet — the pass matches a call site to a declaration **by name**, and
  `tk_over_pass` runs behind it. C3 below makes a list one candidate among many, and the
  refusal is gone with it.

Two mechanisms the design did not name:

* **A leftover index is refused where it stands.** With `xs[i]` on a list resolving at parse
  time like any other array, nothing rewrites an `N_INDEX` the parse could not resolve, and
  the core defines that node without lowering it. The pass refuses it,
  ``teko: `[` needs an array`` — the same backstop the old walk gave under another name.
* **The normal form needs the walked declaration's own parameters.** Deciding that a single
  argument is *already* a `T[]` means typing it, and the unit-wide table of "the most recent
  declaration of this name" does not answer for a **parameter** — which is exactly the name
  a list is passed on under (`total(rest)`). The pass reads the parameter list of the
  declaration it is walking, and falls back to that table and then to a global `T[]`.

---

## What C2b corrected

The design above says "registers the outer node as **borrowed**", and the flip wrote that as
`xt_pure = 1`. Wrong column. `xt_pure` is the flag that says a node is **safe to
re-evaluate** — `tk_pure`, which the virtual-call shaping reads before `tk_clone` copies a
receiver into the vtable load. A `tkarr_put_T` link is a **store**, plus an `rt_own` on a
counted element, so declaring it pure handed out a licence to duplicate both. What the site
meant was ownership, and ownership is a different question.

So `tk_xt` carries an `xt_own` column of its own, `TK_OWNED` or `TK_BORROWED`; `tk_rc_own`
reads that and nothing else, `tk_pure` answers purity alone, and both writers take both
answers so every registration in the port states both. The audit of all forty of them is in
[nodes-and-xt.md](../internals/nodes-and-xt.md) § *Purity and ownership are two questions*.

**The hazard was latent, not live.** The `params` pass runs behind the oracle, and the
oracle is the last pass that consults `tk_pure` — so no shaper ever saw a link, and a probe
that puts a chain in all four positions a shaper reaches for (the receiver of a call, an
argument of a virtual call, an operand of an overloaded operator, an arm of `?:` and of a
`switch` expression) counts one construction per element and `rt_live()` back to zero on the
commit **before** the correction as much as after it. The correction is contractual: it
stops the next pass to be moved from inheriting a licence nobody meant to give. The receiver
case is in the fixture (`boxed`), and the shape the purity gate really guards — a chain as
the receiver of a **virtual** call — is the refusal it always was,
`teko: a virtual call needs a name or a field on the left`.

---

## What C3 measured, and where it diverged

**The shape.** The fifth round is `tk_ov_resolve`'s (teko_over.tk), mirroring the fourth:
tried only once the two exact-arity rounds and the default-completing one have all failed,
which is C#'s "the normal form wins" for free — round order and no comparison of candidates
at all. The alternative shape, moving the `params` pass BEHIND `tk_over_pass` and matching
by the resolved symbol, was measured on the tree and rejected: `tk_default_fill`
(teko_default.tk) refuses a call short of a name's parameter count, so `total()` would be
``teko: total takes at least 1 arguments`` before the list ever saw it, and the default pass
would have had to learn what a `params` list is. The round keeps that knowledge in one
place.

**Which pass expands.** A name declared once is expanded where it always was, in
`tk_params_pass`. A name that carries a **second signature** is marked shared
(`tk_pm_scan_shared`) and its call sites are left as written; the fifth round chooses the
candidate and calls `tk_pm_expand_call` back in teko_params.tk, which builds the same chain
with the same ownership registration. One expansion, two callers — the second knowing which
list a site means, which is the only thing the first cannot answer.

**Two holes the refusal was hiding.** Neither was a second signature the C2 check could see,
because both declarations carried a list and the check only looked for one that did not:

* `f(params i64[])` beside `f(params f64[])` compiled, and every site went to the FIRST row
  — `f(1.5)` was `teko: a value of type f64 does not convert to i64`;
* `f(params i64[])` beside `f(i64 a, params i64[] rest)` compiled too, and `f(1, 2)` silently
  called the first (measured: exit 102 where C# gives 202).

C3 answers both from the same table: the row is shared when any other declaration of the
name has a different parameter shape (`tk_pm_same_shape`), which a prototype and its own
definition never do — spelled with the modifier or without it.

**Two stages, so a failure earns the right message.** The round reads the SHAPE first (the
fixed prefix against the parameters before the list, and any tail length from zero up); a
single list applicable by shape is chosen there and its elements are checked where the chain
is built, so a tail that does not convert is reported against the list it was written for.
Only when two lists take the site do the element types have to tell them apart, and only a
site no element type takes leaves with nothing.

**C#'s last tie-break, adopted.** Between two lists both applicable in expanded form, the
one with **more declared parameters** wins (§12.6.4.5), so `f(i64, params i64[])` takes
`f(1, 2)` over `f(params i64[])`. Without it a perfectly ordinary C# pair would be refused
as ambiguous. What is left ambiguous is a genuine tie: the same declared parameter count and
two element types that both take the arguments — `params u8[]` beside `params u64[]` at
`f(1)`, refused with `teko: more than one overload of f matches these arguments`.

**An integer literal picks `i64` here too.** The element check has the same two rounds the
argument check has: strictly, a bare `1` lands only in an `i64` element, and loosely in any
integer of the core. That is what makes `f(1)` the `params i64[]` when a `params u8[]` is
also in reach, and it is the tie-break `tk_ov_args_fit` already applied to a parameter.

**A ceiling the overloaded path does have.** Resolution types every argument of a site at
once, into an array of the frame, so a call of an OVERLOADED name is bounded — the bound
went from `MAXPARAMS` (12) to **64**, `teko: too many arguments`. A name declared once is
unbounded as before: nothing types its arguments as a set. A fourteen-argument site on an
overloaded list is in the probes.

**What C3 did not touch.** The refusal on a method, a constructor, an interface signature
and a `delegate` stands unchanged; no new message was added, and the three the round can
reach (`no overload of`, `more than one overload of`, `does not convert`) are the ones the
resolution and the element check already had.
