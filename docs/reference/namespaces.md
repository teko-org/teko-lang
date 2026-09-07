# Namespaces, `using` and `import`

A namespace qualifies the names declared inside it. Two namespaces may declare the same
short name and the two types coexist; a use site picks one by writing the qualification, by
a `using`, or by standing inside the namespace itself.

---

## Declaring

```
namespace A.B { ... }        // a block
namespace A.B;               // file-scoped: the rest of THIS file
```

A path has one or more segments. Reopening a namespace merges into it — a second block
simply adds to what the first one declared. A **nested** namespace is not taught: a block
inside a block is refused.

The real name of a type declared inside `geo` is the qualified one; the short name is
reserved as a word, and which type it means is decided at the **use site**, never memoized.

Inside a namespace **block**, a `global`, an `extern` and `main` are refused: they belong
outside every namespace.

## Using a name

| written | resolves |
|---|---|
| `geo.Circle c = new geo.Circle();` | the qualified type |
| `geo.Circle.made` | a static member through the qualified type |
| `geo.area(3)` | a free function of the namespace |
| `geo.N` | a `const` of the namespace |
| `using geo;` then `Circle` | the short name, for the rest of the file |

`using A.B;` sits at the top level of a file and applies to that file. There is no
`using X = A.B;` and no `using static`.

```teko
// expect-exit: 42
#include "rt.tk"

namespace geo {
    const i64 N = 5;

    class Circle {
        public static i64 made;
        public i64 r;

        public i64 area() {
            made = made + 1;
            return r * r;
        }
    }

    i64 area(i64 r) {
        return r * r;
    }

    i64 area(i64 w, i64 h) {                     // overloaded inside the namespace
        return w * h;
    }
}

namespace mesh {
    class Circle {                               // the same short name, another type
        public i64 side;

        public i64 area() {
            return side * side;
        }
    }
}

namespace geo {                                  // reopened: merged into the first block
    class Box {
        public i64 v;
    }
}

using geo;

i64 main() {
    geo.Circle gc = new geo.Circle();
    gc.r = 3;
    if (gc.area() != 9) return 1;

    mesh.Circle mc = new mesh.Circle();
    mc.side = 2;
    if (mc.area() != 4) return 2;

    Circle c = new Circle();                     // bare, through `using geo;`
    c.r = 5;
    if (c.area() != 25) return 3;
    if (geo.Circle.made != 2) return 4;          // the two Circles are one type

    if (geo.area(3) != 9) return 5;              // qualified call
    if (area(3, 4) != 12) return 6;              // bare, and the other overload
    if (geo.N != 5) return 7;

    geo.Box b = new geo.Box();
    b.v = 1;
    return gc.area() + mc.area() + c.area() + b.v + 3;
}
```

A base class, an interface in a `:` list and a trait in a `use` may each be written
qualified (`class Rhomb : geo.IShape`, `use geo.Loud;`) or bare under a `using`.

---

## How a bare name resolves

In this order, first match wins:

1. a **local or a parameter** of the enclosing function;
2. a **member of the current type** — declared by it, inherited from a base, or static;
3. the **current namespace** and its prefixes, outermost last;
4. a **plain top-level declaration** of that exact name;
5. the file's **`using`** directives.

A `using` never outranks something already visible without it, and two `using`s offering
the same name are `teko: ambiguous name X (a, b)`.

```teko
// expect-exit: 42
#include "rt.tk"

i64 helper(i64 x) {
    return x + 100;
}

namespace geo {
    i64 f(i64 x) {
        return x + 1000;
    }

    i64 offset(i64 x) {
        return helper(x);                        // a plain global: the call stays flat
    }

    i64 use_own_f(i64 z) {
        return f(z);                             // geo's own f, from inside geo
    }
}

i64 f(i64 y) {                                   // a plain top-level f
    return y + 1;
}

class Circle {
    public i64 f(i64 x) {
        return x + 1;
    }

    public i64 test(i64 x) {
        return f(x);                             // the member, not geo.f
    }
}

using geo;

i64 main() {
    if (geo.offset(1) != 101) return 1;
    if (geo.use_own_f(5) != 1005) return 2;
    if (f(5) != 6) return 3;                     // the plain one beats the `using`
    Circle c = new Circle;
    if (c.test(1) != 2) return 4;
    {
        i64 f = 40;                              // a local shadows all of it
        return f + 2;
    }
}
```

---

## `import`

```
import A.B;
```

`import A.B;` reads the file `A/B.tk`, relative to the including file, and adds an implicit
`using A.B;`. It is once-only — writing it twice reads the file once — and it comes **at
the top of its own file, before any namespace of that file**: inside a namespace block, or
after the file has declared a namespace of its own, it is refused.

A file brought in this way normally opens with the file-scoped form:

```
// parts/geo.tk
namespace parts.geo;

class Circle {
    public i64 r;
}

i64 twice(i64 x) {
    return x * 2;
}
```

and the importer then writes `Circle`, `parts.geo.Circle`, `twice(3)` or
`parts.geo.twice(3)`, and may take the address of either spelling with `&`.

A plain `#include "x.tk"` still works and is still textual — but the forward scan that
makes the order of declaration free does not read across it, so a type used before an
`#include` that declares it is not found. `import` is the form to use.

---

## `internal`

A top-level type with no modifier is `internal`: it is reachable from the **project**, and
the project is the directory of the build config the compilation used. A declaration read
from an absolute path, from a path that climbs out of that directory, or from a bundled
`#include <name>` is outside it, and reaching such a type is
`teko: X is internal to another project`. Two different outside packages are not told apart
— there are two origins, this project and everything else.

---

## Limits

| limit | value |
|---|---|
| a nested `namespace` | not taught |
| `using X = A.B;`, `using static` | not taught |
| a **qualified generic** (`geo.Box<i64>`) | not taught; a generic keeps its short name |
| a namespaced `const` as a `case` label | not taught (the label is folded before the namespace pass runs) |
| a global `T[]` declared in a namespace | keeps the bare name |
| a **qualified** base or interface declared **below** its use, or a base in another namespace | not taught |
| `global`, `extern`, `main` inside a namespace block | refused |
| namespaces declared in one source | 16 |
| `using` directives, across every file | 32 |
| namespaced short type words | 64 |
| files declaring a file-scoped namespace | 16 |
