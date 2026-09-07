# Classes, interfaces and traits

A `class` is a reference type with a vtable and a reference count; a `struct` is the same
member grammar with neither. `new Name` and `new Name(args)` both hand out **zeroed**
bytes: a field nobody assigned reads as `0`, a reference field as null.

```
[public|internal] [abstract] [partial] class Name [: Base] [, Iface...] { members }
```

A type carries fields, methods, constructors (`Name(...)`), a destructor (`~Name()`),
properties, constants and operators. A member with no modifier is `private`, a top-level
type with no modifier is `internal` — C#'s defaults. `protected` is the type and what
derives from it; `static` means no receiver, so a static field is a global of its own.

**The receiver is never written.** A method declares no `self`: inside the body a bare
name is a member of `this` whenever no local or parameter carries that name. `this.side`
says the same out loud, and `base.m()` calls the base's implementation directly, without
the vtable.

One base class, first in the `:` list, then any number of interfaces. `virtual` takes a
vtable slot and `override` fills it — hiding an inherited virtual without saying
`override` is refused. An `abstract` class cannot be instantiated and its abstract members
have no body; the first concrete class below it owes an `override` for each of them, and
`: base(args)` chains to the base constructor.

An `interface` declares methods and properties, never a field, and a class naming one
carries an interface table. An interface member may carry a **default body** (C# 8): a
class that does not redeclare it points at that symbol, and a call from inside the default
dispatches back through the table, so a redeclaration always wins. A `trait` is PHP's, not
C#'s: `use Name;` **copies** its fields and methods into the class at compile time; the
class's own member beats the trait's, and the trait's beats one inherited from the base.

A property reads like a field and is a pair of methods: `{ get; set; }` generates the
backing field, `get => e` / `set => e` is the expression form, `get { }` the block form,
`value` is the name a `set` is handed, and each accessor owns a vtable slot. An operator
is a `public static` member naming **both** operands, resolved by their static types;
pairs are mandatory (`==`/`!=`, `<`/`>`, `<=`/`>=`).

Order of declaration is free: a type may be used above the line that declares it, as a
field, a parameter, a base class, an interface or the target of `new`.

## One program

```teko
// expect-exit: 42
#include "rt.tk"

interface IArea {
    i64 edge();

    i64 area() {                          // a default body, C# 8
        return this.edge() * this.edge();
    }
}

trait Counted {
    protected i64 n;

    public i64 bump(i64 k) {
        n = n + k;                        // the field of whoever uses the trait
        return n;
    }
}

abstract class Shape : IArea {
    public i64 side;
    public static i64 made;               // a global of its own: no byte in the object

    public Shape(i64 s) {
        side = s;                         // `side` is `this.side`
        made = made + 1;
    }

    public i64 edge() {
        return side;
    }

    public abstract i64 kind();

    public i64 Twice {                    // read like a field, stored as two methods
        get { return edge() * 2; }
    }
}

class Square : Shape {
    use Counted;

    public Square(i64 s) : base(s) {
    }

    public override i64 kind() {
        return 3;
    }

    public static Square operator+(Square a, Square b) {
        return new Square(a.side + b.side);
    }
}

i64 main() {
    Square q = new Square(4);
    Shape s = q;                          // a derived object in a base-typed slot
    if (s.kind() != 3) return 1;          // the override answers, through the vtable
    if (s.Twice != 8) return 2;

    IArea i = q;                          // the same reference, through the itable
    if (i.area() != 16) return 3;         // the interface's own default body

    if (q.bump(3) != 3) return 4;         // the trait's method, on the class's field
    Square sum = q + q;                   // a static operator, by the operand types
    if (sum.side != 8) return 5;
    if (Shape.made != 2) return 6;

    return i.area() + sum.side + Shape.made + 16;
}
```

Every form, with its limits: [classes.md](../reference/classes.md) and
[types.md](../reference/types.md).
