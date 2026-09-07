# Namespaces, files and `partial`

A namespace qualifies the names declared inside it, and two namespaces may declare the
same short name without colliding: which one a bare name means is decided at the use site.

```
namespace A.B { ... }        // a block
namespace A.B;               // file-scoped: the rest of THIS file
using A.B;                   // the short names of A.B, in this file
```

Reopening a namespace merges into it. A **nested** namespace is refused, a `global`, an
`extern` or `main` inside a namespace block is refused too, and `using X = A.B;` and
`using static` do not exist.

A bare name resolves in this order, first match winning: a local or parameter, a member of
the current type, the current namespace and its prefixes, a plain top-level declaration,
then the file's `using` directives.

## Several files

`import A.B;` reads `A/B.tk` relative to the importing file and adds an implicit
`using A.B;`. It is once-only, and goes at the top of its own file, before any namespace
that file declares.

```teko
// no-run
namespace parts.geo;               // parts/geo.tk, file-scoped

class Circle {
    public i64 r;

    public i64 area() {
        return r * r;
    }
}
```

```teko
// no-run
import parts.geo;                  // the importer, in the same project

i64 main() {
    Circle c = new Circle();       // bare; `parts.geo.Circle` names the same type
    c.r = 3;
    return c.area();
}
```

A plain `#include "x.tk"` still works and is still textual, but the forward scan that
makes the order of declaration free does not read across it: a type used **above** an
`#include` declaring it is not found, so `import` is the form to reach for.

`internal`, the default for a top-level type, means the **project**: the directory of the
build config. A file read from an absolute path, from one climbing out of it or from a
bundled `#include <name>` is outside.

**`partial`.** The same class may be declared in more than one place, in one file or
across several, and is united at compile time: the first part opens the row, the others
add members, and the type closes at its first use. There is no partial **method**.

## One program

```teko
// expect-exit: 42
#include "rt.tk"

namespace geo {
    const i64 N = 5;

    class Circle {
        public i64 r;

        public i64 area() {
            return r * r;
        }
    }

    i64 area(i64 w, i64 h) {
        return w * h;
    }
}

namespace mesh {
    class Circle {                     // the same short name, a different type
        public i64 side;
    }
}
partial class Shape {
    public i64 w;

    public i64 area() {
        return w * h;                  // `h` is declared in the other part
    }
}

partial class Shape {
    public i64 h;
}

using geo;

i64 main() {
    geo.Circle gc = new geo.Circle();
    gc.r = 3;
    if (gc.area() != 9) return 1;
    Circle c = new Circle();           // bare, through `using geo;`
    c.r = 2;
    if (c.area() != 4) return 2;

    if (area(2, 3) != 6) return 3;     // a free function of the namespace
    if (geo.N != 5) return 4;
    Shape s = new Shape;
    s.w = 4;
    s.h = 5;
    return gc.area() + c.area() + s.area() + 9;
}
```

Every rule, with the limits: [namespaces.md](../reference/namespaces.md).
