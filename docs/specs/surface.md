# The surface policy

How a question about the language is answered when nobody has answered it yet, and what the
answer is never allowed to be. The rulings themselves are in
[`DECISION_LOG.md`](../../DECISION_LOG.md); this page is the procedure behind them.

## Where a form comes from

1. **C# has it** → teko takes it, spelling and semantics. Classes, structs, interfaces,
   generics, delegates, properties, `params`, `ref`/`out`, both `switch` spellings, the
   ternary, the access modifiers and their defaults, constructor injection: all of them are
   C#'s, and where they differ the difference is a ruling with a reason.
2. **C# has no form** → the market decides. `trait` is PHP's, because PHP is where the
   compile-time flattening model is; explicit capture lists are PHP's for the same reason.
3. **The base grammar** comes from `mc` and is reused as it is. Expressions, statements,
   types, functions, `loop`, `break N` are not re-taught: what this repository teaches is the
   **delta** on top of them.

Teko's own older spelling is not inherited. A form that existed before the port is not an
argument for keeping it.

## Two things the surface will not become

**No dynamic value.** There is no variant, no tagged run-time value and no "any". Every
receiver, argument and operand has a type the compiler knows, or the program is refused where
it is written. A construct that wants a variant is a fork, not something to emulate — an
`interface`, a `class` or a generic is the answer.

**No silently wrong result.** A construct that is not taught is refused **where it is
written**, with a `teko: <short cause>` naming it. That rule is what makes the refusal list a
documented surface rather than an apology: a program either does what it says or does not
compile. Every message the compiler can emit is catalogued, and the documentation gate fails
when one is not.

## What is "magic", and why the list is closed

Nothing in the backend recognises a teko function by name and synthesises it inline. Every
function teko emits a call to has surface code in this repository, and `mc limits` is the
budget a construct fits into.

The list of things that genuinely have no surface code is therefore exactly **what mc
gives**, and teko adds nothing to it:

| primitive | from | what it is |
|---|---|---|
| `ld8` `ld16` `ld32` `ld64`, `st8` `st16` `st32` `st64` | mc's core | the only memory access in the language; there is no dereference syntax |
| `&name` | mc's core | the address of a local, a global or a function |
| `callp(p, …)` | mc's core | the indirect call, and the one place a cast **declares** a return type instead of converting it |
| `ldf64` `ldf32` `stf64` `stf32` `sqrt_f64` `fabs` `fmin` `fmax` | mc's `<float>` | the float register file, which an integer load cannot reach |
| the system calls of `<sys>` | mc's `<sys>` | `write`, `read`, `open`, `close`, `exit` and their siblings, declared as `extern` |

`mc limits` reports `intrin 8/16` for the taught compiler, and all eight are `<float>`'s.
**Teko registers no intrinsic of its own.** A feature that seems to need one is a fork: it is
recorded and asked, never implemented quietly.

The rest — the arena, reference counting, the interface dispatch, the array guard, the
delegate call, the closure captures, `str` and the `f64` bit pattern — is ordinary teko in
[`lib/rt.tk`](../../lib/rt.tk), which is why
[the runtime reference](../reference/runtime.md) can list it as functions with signatures.

## What is not written here

Nothing in `minicompiler/mc`'s own sources changes for teko's sake. When `mc` genuinely
cannot express something, the answer is a minimal reproducer written in pure `mc` and a
report to that project — never a workaround here, and never a patch there that this
repository starts depending on before it is released.

That constraint is what shapes the surface more than any other. Several constructs are the
shape they are because a hook could reach them and a different shape could not, and the
honest way to record that is a ruling that says so.

## How a fork is settled

Before halting on a design question: search
[`DECISION_LOG.md`](../../DECISION_LOG.md), then these specs, then the reference.
**The newest ruling wins** — a later entry supersedes an earlier one on the
same point, and when a decision changes the entry is rewritten and dated rather than appended
to.

If nothing has settled it, the default is rule 1 above, or rule 2 when C# is silent; the
choice is recorded and the work continues. A halt is for what neither C#, nor the market, nor
`mc` answers.

## The relationship with the reference

[`reference/`](../reference/README.md) describes only what **runs**, and
[`reference/not-yet.md`](../reference/not-yet.md) is the list of what is refused, with the
message each refusal answers. `specs/` — this directory — is for what is **designed** and not
built. The two are kept apart on purpose: a reader looking up a construct must never find a
plan described as if it worked, and nothing on the refusal list is a promise about a later
version.
