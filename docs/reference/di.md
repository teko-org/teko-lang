# Dependency injection

Services are wired at **compile time**. A class declares its lifetime by naming a marker,
`inject T` asks for one, and the compiler builds the graph: there is no container at run
time, no registration call and no reflection.

---

## Marking a service

```
class Clock : IClock, IServiceSingleton { ... }
```

`IServiceSingleton`, `IServiceScoped` and `IServiceTransient` are **markers**, not base
classes and not interfaces: a class names one of them in its `:` list, alongside the
interfaces it really implements. Naming two of them is refused, and an `abstract class` is
not a service.

| marker | one instance per |
|---|---|
| `IServiceSingleton` | the program — a root, allocated once and never released |
| `IServiceScoped` | the enclosing `scope { }`, or the private scope of the singleton that pulled it in |
| `IServiceTransient` | every injection |

A class **derived** from a service is not itself a service: the marker does not inherit, so
such a class answers no key and is built with a plain `new`.

## Asking for one

```
IClock a = inject IClock;      // an interface the service implements
Clock  b = inject Clock;       // the service's own name
IBase  c = inject IBase;       // an interface that interface extends
app.ICache d = inject app.ICache;   // qualified, or bare under a `using`
```

A service answers to every one of those keys, and they all land on the same instance for a
Singleton. If no class implements the type, the site is `teko: no service implements this
type`; if two do, `teko: two services implement this interface: ...`.

## Constructor injection

A service that needs others declares them as **constructor parameters**; the compiler fills
them in, recursively, and a parameter with a default that is not a service is filled from
that default. A service has exactly one injectable constructor — two taking the same number
of injectable parameters is refused — and its constructor has to be reachable
(`private`/`protected` is `teko: the constructor of this service is not accessible`). A
cycle in the graph is `teko: cyclic service: ...`.

```teko
// expect-exit: 42
#include "rt.tk"

interface IClock {
    i64 tick();
}

interface IDb {
    i64 query();
}

interface IRepo {
    i64 load();
}

class Clock : IClock, IServiceSingleton {
    i64 n;

    public i64 tick() {
        n = n + 1;
        return n;
    }
}

class Db : IDb, IServiceSingleton {
    i64 n;

    public Db(i64 seed = 5) {                    // not a service: the default fills it
        n = seed;
    }

    public i64 query() {
        n = n + 1;
        return n;
    }
}

class Repo : IRepo, IServiceSingleton {
    IDb db;

    public Repo(IDb db) {                        // injected by constructor
        this.db = db;
    }

    public i64 load() {
        return this.db.query();
    }
}

class Svc : IServiceSingleton {
    IRepo repo;
    IClock clock;

    public Svc(IRepo repo, IClock clock) {
        this.repo = repo;
        this.clock = clock;
    }

    public i64 run() {
        return this.repo.load() + this.clock.tick();
    }
}

i64 main() {
    IClock a = inject IClock;
    Clock b = inject Clock;                      // the same instance, by its own name
    if (a.tick() != 1) return 1;
    if (b.tick() != 2) return 2;

    IRepo r = inject IRepo;                      // through the interface: the table answers
    if (r.load() != 6) return 3;                 // Db's own default (5), then one query

    Svc s = inject Svc;                          // built from IRepo and IClock
    if (s.run() != 10) return 4;                 // the same Repo and Clock

    if (rt_live() != 4) return 5;                // Clock, Db, Repo, Svc -- never more
    return 42;
}
```

---

## `scope { }`

`scope { ... }` opens a lexical scope. Inside it a `IServiceScoped` service is one instance
shared by every injection of that scope, and it is released at the `}` — including when a
`return` leaves the block early. Two scopes in a row build two instances; a scope nested in
another builds its own and leaves the outer one untouched. A `IServiceTransient` built
inside a scope is fresh per injection, and its own scoped dependency is that scope's shared
instance.

```teko
// expect-exit: 42
#include "rt.tk"

i64 dtors = 0;

interface IAudit {
    i64 hit();
}

class Audit : IAudit, IServiceScoped {
    i64 n;

    public i64 hit() {
        n = n + 1;
        return n;
    }

    ~Audit() {
        dtors = dtors + 1;
    }
}

class Job : IServiceTransient {
    IAudit a;

    public Job(IAudit a) {
        this.a = a;
    }

    public i64 use() {
        return this.a.hit();
    }
}

i64 main() {
    scope {
        IAudit a1 = inject IAudit;
        IAudit a2 = inject IAudit;
        if (a1.hit() != 1) return 1;
        if (a2.hit() != 2) return 2;             // the SAME instance
        if (rt_live() != 1) return 3;
    }
    if (rt_live() != 0) return 4;
    if (dtors != 1) return 5;                    // released at the `}`

    dtors = 0;
    scope {
        IAudit b1 = inject IAudit;
        if (b1.hit() != 1) return 6;             // a fresh instance, counting from 1
        scope {
            IAudit inner = inject IAudit;
            if (inner.hit() != 1) return 7;      // its own, again
            if (rt_live() != 2) return 8;
        }
        if (rt_live() != 1) return 9;            // the inner one is gone
        if (b1.hit() != 2) return 10;            // the outer one is untouched
    }

    dtors = 0;
    scope {
        Job j1 = inject Job;
        Job j2 = inject Job;                     // a fresh Job per injection
        if (j1.use() != 1) return 11;
        if (j2.use() != 2) return 12;            // sharing the scope's one Audit
        if (rt_live() != 3) return 13;           // the Audit and both Jobs
    }
    if (rt_live() != 0) return 14;
    if (dtors != 1) return 15;
    return 42;
}
```

---

## A Singleton may receive a Scoped

This is where teko differs from C# on purpose: a Singleton **has a scope of its own**, so a
Scoped service may be injected into it. The rules that follow from that:

- the dependents of one Singleton share **one** instance of a Scoped service — the instance
  of that Singleton's own scope;
- two different Singletons pulling in the same Scoped class get **different** instances;
- a Transient inherits the scope it was injected into.

```teko
// expect-exit: 42
#include "rt.tk"

class Meter : IServiceScoped {
    i64 n;

    public i64 tick() {
        n = n + 1;
        return n;
    }
}

class WrapA : IServiceTransient {
    Meter m;

    public WrapA(Meter m) {
        this.m = m;
    }

    public i64 read() {
        return this.m.tick();
    }
}

class WrapB : IServiceTransient {
    Meter m;

    public WrapB(Meter m) {
        this.m = m;
    }

    public i64 read() {
        return this.m.tick();
    }
}

class Board : IServiceSingleton {
    WrapA a;
    WrapB b;

    public Board(WrapA a, WrapB b) {
        this.a = a;
        this.b = b;
    }

    public i64 total() {
        return this.a.read() + this.b.read();
    }
}

class GaugeX : IServiceSingleton {
    Meter m;

    public GaugeX(Meter m) {
        this.m = m;
    }

    public i64 read() {
        return this.m.tick();
    }
}

class GaugeY : IServiceSingleton {
    Meter m;

    public GaugeY(Meter m) {
        this.m = m;
    }

    public i64 read() {
        return this.m.tick();
    }
}

i64 main() {
    Board brd = inject Board;
    if (brd.total() != 3) return 1;              // 1 + 2: ONE Meter, shared
    if (rt_live() != 4) return 2;                // Meter, WrapA, WrapB, Board

    GaugeX gx = inject GaugeX;
    GaugeY gy = inject GaugeY;
    if (gx.read() != 1) return 3;                // its OWN Meter, from 1
    if (gy.read() != 1) return 4;                // a different one, also from 1
    if (rt_live() != 8) return 5;
    return 42;
}
```

---

## Lifetime compatibility

| a … | may receive a Singleton | a Scoped | a Transient |
|---|---|---|---|
| Singleton | yes | yes — its own scope's instance | yes |
| Scoped | yes | yes — the same scope's instance | yes |
| Transient | yes | yes — the scope it was injected into | yes |

A `inject` of a Scoped service written **outside** any `scope { }` and outside a service's
own graph has no scope to belong to, and an `inject` written inside a **lambda** takes the
service of the enclosing scope: bind it outside the lambda and capture it with `use (...)`.

---

## Limits

| limit | value |
|---|---|
| a generic key (`IRepo<T>`) | not taught |
| a keyed or named registration | not taught |
| a factory, or a decoration | not taught |
| a service that is not a class | refused (`a service marker names a class`) |
| an `abstract class` as a service | refused |
| resolving a service by a call (`Services.Get<T>()`) | there is no such surface: the form is a marker plus `inject` |
| classes marked with a lifetime | 32 |
| `inject` sites | 32 |
| `scope { }` blocks (plus one per singleton) | 64 |
| services under construction at once (the cycle guard) | 32 |
