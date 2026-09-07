# Dependency injection, inside

Everything happens while the unit is compiled. There is no container at run time, no
reflection and no registration API: `inject T` becomes either a call to a generated getter
or a reference to a local of the enclosing block, and by the time the program starts there
is nothing left to resolve. The surface is
[the DI reference](../reference/di.md); the model as decided is
[the DI spec](../specs/dependency-injection.md). This page is
[`teko_di.tk`](../../teko_di.tk).

## The registry

A class marks its lifetime by naming `IServiceSingleton`, `IServiceScoped` or
`IServiceTransient` in the list after `:`. Those are **names**, not base types: they are
read where the conformance list is already read, before any lookup in the type table, so a
program that writes none of the three words never allocates a row, a global or a symbol for
this module at all.

Registration happens when that list closes, against the class's own row:

| column | holds |
|---|---|
| `sv_cls` | the row of the type table |
| `sv_life` | Singleton, Scoped or Transient |
| `sv_slot` | 1 once the root slot and getter have been emitted |
| `sv_getter` | the getter's symbol, once emitted |
| `sv_ownscope` | a Singleton's own private scope row, or `-1` |

The **keys** a service answers to are the interfaces its class row already publishes — the
flattened set, so an interface's own base counts — plus the class itself. `inject IClock`
and `inject Clock` therefore reach the same instance.

A class that names two lifetimes is refused, and so is an `abstract class` as a service.

## `inject` is a deferred placeholder

`tk_inject` never resolves anything. It records the site — the node, the wanted key, the
innermost open scope, and the position — and hands back a placeholder, exactly the way a
`.` on an untypeable receiver and a ternary do. `tk_di_pass` ([passes.md](passes.md), the
fourth) is what resolves them, once the unit is fully parsed and every service is
registered, whatever order the source named them in.

That is also the safety net: if the pass were somehow not registered, the core's own
resolver would refuse the placeholder call outright rather than compile something wrong.

## Constructor injection

`tk_di_ctor_pick` walks the constructors of the service's class and keeps the one with the
**most** parameters, every one of which is either a resolvable service key or carries a
default. A tie between two equally long satisfiable constructors is refused rather than
guessed; a class none of whose constructors qualify gets its own message. A
private or protected constructor is refused with a message of its own, ahead of the generic
accessibility one.

`tk_di_ctor_args` then builds the call: a service-typed parameter recurses into the same
resolution, everything else clones its default. A parameter that is both a service key and
defaulted takes the service.

`di_stk` holds the services currently under construction. A key already on it is a
**cycle**, refused with the chain written out (`A -> B -> A`) before the compiler recurses
into itself again — the guard protects the compiler's stack, not the program's.

## Scopes

`scope { … }` is an ordinary block. A parse-time stack is pushed when it opens and popped
when it closes, and every `inject` parsed inside records the innermost row still open. A
function **called** from inside a scope does not inherit it: parsing that function's body
runs with a stack of its own, empty.

| lifetime | resolved as |
|---|---|
| Singleton | a memoized getter over one global slot: `Name_di_slot`, `Name_di_get()` |
| Scoped, with no scope open | the same root getter — the root is a scope like any other |
| Scoped, inside `scope { }` | a **local of that block**, built once per `(scope, service)` pair |
| Transient | a fresh construction at every injection site |

A Transient built inside a scope resolves its own Scoped dependencies against the very scope
that asked for it: the scope number is threaded all the way down the constructor argument
list. That is what "a Transient inherits the scope of whoever receives it" means
mechanically.

The scope's locals are **prepended** to the block's own statements once the whole unit is
parsed. From there the ordinary block-scope reference counting frees them exactly where it
frees any other local of class type — one lap of a loop at a time, with no rule of its own
([runtime.md](runtime.md)).

## A Singleton has a scope of its own

This is the one place teko differs from C#, and the mechanism is why it is safe: a
Singleton's dependency graph is built under a **private scope row**, allocated from the same
pool `scope { }` uses and remembered in `sv_ownscope`. So a Scoped dependency of a Singleton
becomes a local of that Singleton's own builder — one row per Singleton, never shared
between two of them, and never captured from a caller's scope that is about to end.

## Two refusals worth the reason

`inject` written lexically inside a lambda is refused outright. A lambda's body is a
**separate generated function** ([`teko_deleg.tk`](../../teko_deleg.tk)), so a Scoped local
prepended to the enclosing `scope { }` would not be reachable from it; the message says to
bind it outside and capture it with `use (…)`.

A generic key (`IRepo<T>`), a keyed registration, a factory and a decoration are not taught
at all, and neither is any `Services.Get<T>()`-shaped API — there is no run-time surface to
put one on.

## Caps

`TK_MAXSVC 32` services, `TK_MAXDISITE 32` `inject` sites, `TK_MAXDISTK 32` services under
construction at once, `TK_MAXDISCOPE 64` scope rows (lexical ones plus one per Singleton),
`TK_MAXDISCOPESTK 32` nested open scopes, `TK_MAXDISCOPELOC 64` `(scope, service)` locals.
Each overflows with its own message.
