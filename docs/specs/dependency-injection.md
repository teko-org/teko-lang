# Dependency injection: the model

The decided semantics, including the parts that are not built. What runs today is
[the DI reference](../reference/di.md), and how it is implemented is
[the DI internals](../internals/di.md). The ruling in force is
[D13](../../DECISION_LOG.md).

## The shape of the decision

C# is the starting point, and teko takes its form: constructor injection, three lifetimes,
a lexical scope. Three things are deliberately **not** taken.

**No container at run time.** In C# the container is an object you configure and query. Here
registration happens while the program is compiled and injection is resolved then: by the
time the program starts there is nothing left to look up, no dictionary of types, and no
reflection to do it with. That follows from the surface being statically typed with no
dynamic value to key on.

**No registration API.** A class states its own lifetime by naming a marker in the list after
`:`. There is no `services.AddScoped<T>()` and no configuration file, because there is no
run-time object to configure and no phase in which to configure it.

**No `Get<T>()`.** The surface is a marker plus `inject`, which is a word the compiler
resolves, not a call with a return value that might fail.

## Markers, not base types

```teko
// no-run
class Clock : IClock, IServiceSingleton { }
class Repo  : IServiceTransient { public Repo(IClock c) { } }
```

`IServiceSingleton`, `IServiceScoped` and `IServiceTransient` appear where a base class and
an interface list appear, and are read as **names** before any lookup. They contribute no
member, no vtable slot and no itable row; a class that names two of them is a refusal.

The keys a service answers to are the interfaces its class publishes — the flattened set, so
an interface's own base counts — plus the class itself. `inject IClock` and `inject Clock`
therefore reach the same instance, and which spelling a program uses is a matter of what it
wants to depend on.

## The lifetimes

| lifetime | one instance per |
|---|---|
| Singleton | the program |
| Scoped | the enclosing `scope { }`, or the program when none is open |
| Transient | every injection |

**A Singleton may receive a Scoped.** This is where teko departs from C#, which forbids it as
a captive-dependency error. The reason it is safe here is structural rather than a matter of
discipline: a Singleton's dependency graph is built under a **scope of its own**, never under
whichever scope happened to ask for it first, so the Scoped instance a Singleton holds is one
nothing else can end.

**A Transient inherits the scope of whoever receives it.** A Transient built inside a
`scope { }` resolves its own Scoped dependencies against **that** scope. A Transient built
with no scope open resolves them against the program. There is no third answer and no way to
ask for one.

**A scope is lexical, and only lexical.** A function called from inside a `scope { }` does
not inherit it: what the function body sees is decided where the body is written, not where
it is called from. Anything else would need a run-time notion of "the current scope", which
is the container this design does not have.

## Constructor selection

Among a service's constructors, the one with the **most** parameters that are all either a
resolvable key or defaulted. A tie between two equally long candidates is a refusal, not a
choice made by declaration order. A class none of whose constructors qualify is a refusal
naming that fact.

A cycle in the graph is a refusal that prints the chain (`A -> B -> A`). It is found before
the resolution recurses into itself, so the message is the diagnosis rather than a stack
exhaustion.

## Refusals that are part of the design

| written | why it is refused |
|---|---|
| an `abstract class` as a service | there is nothing to construct |
| a service marker on something that is not a class | the marker names a class |
| a private or protected constructor on a service | the injection site could not call it |
| two lifetimes on one class | the lifetime is a property of the class, and it is single |
| `inject` inside a lambda | the lambda's body is a separate function, so a scope local of the enclosing block is not reachable from it — bind it outside and capture it |

## What is designed and not built

**A generic key.** `inject IRepo<T>` would need the key to carry the instantiation, and the
generics here are record-and-replay: `IRepo<Order>` and `IRepo<User>` are two unrelated
types once instantiated, which is workable but is a design of its own.

**Keyed and named registrations, factories, decorations.** Each of them adds a second axis to
the key. None is refused on principle; none has a spelling yet.

**Disposal order beyond reference counting.** A scope local is released where any other local
of class type is released, and its destructor runs then. A stated order **between** the
services of one scope, independent of declaration order, is not part of the model today.

**Injection into anything but a constructor.** No property injection and no method injection.
C# has both and teko takes neither, because the constructor is the only place the compiler
can prove the dependency was supplied.
