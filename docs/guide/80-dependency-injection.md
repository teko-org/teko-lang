# Dependency injection

Services are wired **at compile time**. A class declares its lifetime by naming a marker,
`inject T` asks for one, and the compiler builds the graph where it stands: there is no
container at run time, no registration call, no reflection and nothing to configure.

## The three markers

A marker goes in the `:` list, beside the interfaces the class really implements. It is
not a base class and not an interface; naming two of them is refused, and an
`abstract class` is not a service.

| marker | one instance per |
|---|---|
| `IServiceSingleton` | the program — a root, allocated once and never released |
| `IServiceScoped` | the enclosing `scope { }`, or the private scope of the singleton that pulled it in |
| `IServiceTransient` | every injection |

A class **derived** from a service is not itself a service: the marker does not inherit.

## Asking for one

`inject T` accepts any key the service answers to: an interface it implements, an
interface that interface extends, or the class's own name — and for a Singleton every key
lands on the same instance. If no class implements the type the site is refused, and so is
an ambiguity between two that do.

A service that needs others declares them as **constructor parameters** and the compiler
fills them in recursively; a parameter with a default that is not a service is filled from
that default. A service has one injectable constructor, it has to be reachable
(`private`/`protected` is refused), and a **cycle** in the graph is refused by name.

## `scope { }`

`scope { ... }` opens a lexical scope. Inside it, an `IServiceScoped` service is one
instance shared by every injection of that scope, released at the `}` — including when a
`return` leaves early. Two scopes in a row build two instances, and a nested scope builds
its own without touching the outer one.

Teko differs from C# on one point, on purpose: **a Singleton may receive a Scoped**,
because the Singleton has a scope of its own. So the dependents of one Singleton share one
instance of that Scoped class, two different Singletons get two, and a Transient inherits
the scope it was injected into.

## One program

```teko
// expect-exit: 42
#include "rt.tk"

interface IClock {
    i64 tick();
}

class Clock : IClock, IServiceSingleton {
    i64 n;

    public i64 tick() {
        n = n + 1;
        return n;
    }
}

class Audit : IServiceScoped {
    i64 n;

    public i64 hit() {
        n = n + 1;
        return n;
    }
}

class Job : IServiceTransient {
    Audit a;
    IClock clock;

    public Job(Audit a, IClock clock) {          // injected through the constructor
        this.a = a;
        this.clock = clock;
    }

    public i64 run() {
        return this.a.hit() + this.clock.tick();
    }
}

i64 main() {
    IClock c = inject IClock;                    // through the interface
    Clock same = inject Clock;                   // the same instance, by its own name
    if (c.tick() != 1) return 1;
    if (same.tick() != 2) return 2;

    scope {
        Job j1 = inject Job;                     // a fresh Job per injection
        Job j2 = inject Job;
        if (j1.run() != 4) return 3;             // Audit 1, Clock 3
        if (j2.run() != 6) return 4;             // the SAME Audit (2), Clock 4
        if (rt_live() != 4) return 5;            // Clock, Audit, and the two Jobs
    }
    if (rt_live() != 1) return 6;                // only the Singleton is left

    return c.tick() + 37;
}
```

The whole model, with the compatibility table and the limits, is
[di.md](../reference/di.md); what a run-time container would offer instead — keyed
registrations, factories, generic keys — is not taught.
