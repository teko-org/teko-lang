# The runtime, from the compiler's side

[`lib/rt.tk`](../../lib/rt.tk) is **program** code: it is `#include`d by the program, not
linked into the compiler, and it compiles under the same taught vocabulary as the program
that includes it. What each function does for a program is
[the runtime reference](../reference/runtime.md); this page is the other half — which module
emits the call, and what layout both sides agree on.

Everything here is ordinary teko. No function in `lib/rt.tk` is recognised by name in a
backend, and teko registers no intrinsic of its own.

## The layouts

| | +0 | +8 | +16 | +24 … |
|---|---|---|---|---|
| an object | vtable | reference count | fields, base class's first | |
| a vtable | `Name_release` | the interface table, or 0 | virtual slot 0 | slot 1, … |
| an interface table | count | `(id, methods)` row | row | … |
| a delegate value | vtable (release only) | count | code pointer | one word per capture |
| a `T[]` | vtable | count | `Length` | elements |

Two words are fixed in every vtable (`TK_VT_FIXED`, [`teko_class.tk`](../../teko_class.tk));
virtual slot `k` is at `(TK_VT_FIXED + k) * 8`. A `struct` has none of this: its fields
start at offset 0, it has no count and no release, and it is not reclaimed.

Word 0 of the vtable is the release **because** it lets `rc_dec` free an object it knows
nothing about: it loads the vtable, loads word 0, and calls it with the object. That single
indirection is the whole reason a class, a delegate and a `T[]` can share one reclaim path.

## What emits what

| call | emitted by | at |
|---|---|---|
| `rt_alloc` | `teko_class.tk`, `teko_deleg.tk`, `teko_heaparr.tk` | the generated allocator of a class, a delegate object and a `T[]` |
| `rt_own` | `teko_rc.tk` | `C x = e;` — a borrowed initializer becomes an owning slot |
| `rt_store` / `rt_store_own` | `teko_rc.tk` | `x = e;` and `p.f = e;`, the second on a store the parser marked as counted |
| `rc_dec` | `teko_rc.tk` | the `}` that closes a block, in reverse declaration order; every jump that leaves it; the `return` that leaves the function |
| `rt_drop` | `teko_rc.tk` | `f();` on its own line, where the reference the callee handed out has no owner |
| `rt_mark` / `rt_park` / `rt_sweep` | `teko_rc.tk` | a counted value in argument or receiver position, and the fence around the statement that built it |
| `rt_release_array` | `teko_class.tk`, `teko_heaparr.tk` | inside a release, for an inline array field and for a `T[]` of counted elements |
| `tk_itab` | `teko_expr.tk`, `teko_this.tk`, `teko_typeof.tk` | every interface call, wherever the receiver was typed |
| `tk_deleg_code` | `teko_deleg.tk` | every call through a delegate value |
| `tk_arr_at` | `teko_heaparr.tk` | every index into a `T[]` |
| `tk_va_new` / `tk_va_put` / `tk_va_at` | `teko_params.tk` | a `params` call site, and the reads inside the instance |
| `tk_cap_put` / `tk_cap_own` / `tk_cap_putf` / `tk_cap_putf32` | `teko_deleg.tk` | one link per capture, at the site that builds the closure |
| `panic` / `rt_panic` | `lib/rt.tk`'s own guards | a null delegate, an index out of range, an interface a class does not implement, an exhausted arena |

## Reclaim at the `}`

The reference-counting pass ([passes.md](passes.md), last of the fifteen) writes exactly
this and nothing else:

| written | becomes |
|---|---|
| `C x = e;` | `C x = rt_own(e);` — `new` and a returning call move in instead |
| `x = e;` | `rt_store(&x, e)`, or `rt_store_own` when `e` is already owned |
| `p.f = e;` | `rt_store(p + F, e)` |
| `{ … }` | the same block with an `rc_dec` per counted local at the end, newest first |
| `break N;` `continue N;` | the same jump wrapped in the releases of every scope it leaves |
| `return e;` | a temporary, an increment on it, the releases, then the return |
| `f();` | `rt_drop(f())` |
| `g(f())` | `g(rt_park(f()))`, with `rt_mark`/`rt_sweep` around the statement |

The condition of an `if` is read into a name of its own first, because `if (c) return 1;`
would jump straight over a sweep written after it. A destructor may reach a field the
release is about to drop, so the destructors run **before** any field is released. A unit
that declares no class and no interface comes back untouched — a `struct`-only program keeps
the tree it had, byte for byte.

## The parking table

A counted value handed to a call has **no** owner: a parameter carries no count. So a value
produced in argument position is parked in a fixed table of 64 slots and released when the
statement ends, which is where C# and C++ end a full expression too. It is a **mark**, not a
count: `a && f(new Cell(1))` may never evaluate its right side, and what is released is
whatever the statement actually parked. The marks nest with the calls, so a callee's
statements park above the caller's and sweep back down to it.

## Thunks and closures

A delegate value is an object, so `Op f = add;` cannot store `&add`: the shapes differ. The
module generates one **thunk** per `(delegate, function)` pair —
`Op__thunk_add(uptr env, i64 a, i64 b) { return add(a, b); }` — plus a vtable, a release and
an allocator, and the delegate object's word 2 points at the thunk. A call is
`callp(tk_deleg_code(d), d, …)`, which is why a null delegate panics rather than faults.

A lambda takes the same shape with the captures appended, one word per name in the
`use (…)` list. They are written by a **chain** of calls at the creation site
(`tk_cap_put(tk_cap_put(alloc, off, a), off2, b)`) rather than by an allocator with one
parameter per capture: a store is a statement, the whole construction has to be one
expression, and the ABI's twelve-parameter ceiling would otherwise cap a `use` list at
twelve names ([pitfalls.md](pitfalls.md)). A capture of counted type goes through
`tk_cap_own`, which takes a reference of its own; a float capture goes through the float
accessor, because a float travels in another register file and an integer slot never
receives it.

## Panics

Every guard writes `teko: <cause>` to standard error and exits **70**. There is no recovery
and no unwinding: the surface has no exceptions, and a guard fires where a program would
otherwise read memory that is not there.
