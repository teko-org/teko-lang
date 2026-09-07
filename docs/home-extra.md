## The whole idea, in one file

teko is not a compiler. It is a set of hook modules that teach one — `mc`, whose base
grammar already has expressions, statements, functions and loops. What this repository
adds is the delta a C#-shaped surface needs: types with a vtable, conformance through an
interface table, traits flattened at compile time, generics, delegates and lambdas,
properties, namespaces, dependency injection resolved while compiling, and reference
counting that reclaims at the end of a scope. `mc` enters by release, pinned in
[`MC_VERSION`](../MC_VERSION); a version other than the pin is not expected to build this
tree, and raising the pin is a change of its own.

```teko
// expect-exit: 42
#include "rt.tk"

interface Shape {
    i64 area();
}

class Square : Shape {
    public i64 side;

    public Square(i64 s) {
        side = s;                     // `side` is `this.side`: no receiver is written
    }

    public i64 area() {
        return side * side;
    }
}

i64 main() {
    Shape s = new Square(6);          // dispatch through the interface table
    return s.area() + 6;
}
```

That program is compiled and run by the documentation gate, like every other teko sample
on this site: the exit code in its first line is the oracle, and a page whose sample stops
producing it does not merge. The same rule governs the language itself — forty-five
fixtures under `tests/`, each carrying the exit code it must produce, built by the taught
compiler and executed on five native pairs.

Start with [the guide](guide/README.md), which reads in order from a first program
to packaging; look things up in [the reference](reference/README.md), which
describes only what runs today; read [what v0.4.0 refuses](reference/not-yet.md)
before assuming a construct is missing by accident. What is designed and not yet built is
kept apart, in [the specs](specs/README.md), and how the port itself is put
together — the modules, the passes, the bootstrap ladder — is
[the internals](internals/README.md).
