# Classes, interfaces and traits

Everything a `class` carries: a base class, virtual dispatch, abstract members,
interfaces (with default bodies and `static abstract` members), traits in PHP's model,
properties and operator overloading — plus the two rules that hold the whole page
together, the implicit receiver and the free order of declaration.

The shape of a declaration, its fields and its modifiers are [types.md](types.md).

---

## Inheritance

A class names **one** base class, first in the `:` list, and any number of interfaces
after it. The base's fields are laid out first, so an offset the base publishes keeps its
value in every class below it.

```
class Name : Base, IfaceA, IfaceB { members }
```

A cycle in the chain is refused, and so is a `struct` as a base.

### The receiver is not written

A method declares no receiver. Inside the body, a bare name is a member of `this` when no
local or parameter carries the name — C#'s own rule, local shadows field. `this.side` says
the same thing out loud, `this` is only valid inside the body of a type, and neither
`this` nor `base` exists in a static member.

`base.m()` calls the base class's own implementation **directly**, without the vtable,
which is what lets an `override` build on the method it overrides instead of recursing.

### `virtual` and `override`

A `virtual` method takes a slot in the vtable; an `override` fills the slot its base
declared. Slots are keyed by (name, **signature**), so two overloads of one name are two
slots and each `override` lands on its own. A method that hides an inherited virtual
without saying `override` is refused, and so is an `override` of a method no base
declares. A `struct` has no vtable at all, so neither word is accepted there.

```teko
// expect-exit: 42
#include "rt.tk"

class Shape {
    public i64 side;

    public i64 twice() {
        return side * 2;                         // `side` is `this.side`
    }

    public virtual i64 area() {
        return this.side;
    }
}

class Square : Shape {
    public override i64 area() {
        return base.area() * side;               // the base's own body, directly
    }
}

i64 area_of(Shape s) {
    return s.area();                             // through the vtable
}

i64 main() {
    Shape p = new Shape;
    p.side = 3;
    if (p.area() != 3) return 1;
    if (p.twice() != 6) return 2;

    Square q = new Square;
    q.side = 5;
    if (q.area() != 25) return 3;

    Shape r = new Square;                        // a derived object in a base-typed slot
    r.side = 4;
    if (r.area() != 16) return 4;                // the override answers
    if (area_of(r) != 16) return 5;

    return p.area() + q.area() + r.area() - 2;   // 3 + 25 + 16 - 2
}
```

---

## Constructors and destructors

`Name(params) { }` inside `class Name` is a constructor: overloadable by signature, chosen
by the argument count at `new Name(args)`, and written with no return type. `: base(args)`
chains to the base's own, and a base that declares only constructors taking arguments must
be named that way.

`~Name() { }` is the destructor: no modifier, no parameter, one per class. It runs when the
reference count reaches zero, **before** the fields are released, and a chain runs the
derived class's first and the base's last — the reverse of the constructor chain.

`new Name` with no matching constructor still hands out the zeroed object.

```teko
// expect-exit: 42
#include "rt.tk"

i64 order = 0;

class Base {
    public i64 tag;

    public Base(i64 t) {
        tag = t;
    }

    ~Base() {
        order = order * 10 + 1;
    }
}

class Derived : Base {
    public Derived(i64 t) : base(t + 1) {
    }

    ~Derived() {
        order = order * 10 + 2;
    }
}

i64 main() {
    {
        Derived d = new Derived(1);
        if (d.tag != 2) return 1;                // `: base(t + 1)` ran first
    }
    if (order != 21) return 2;                   // the derived's destructor, then the base's
    if (rt_live() != 0) return 3;
    return 42;
}
```

---

## `abstract`

An `abstract class` has no object of its own: `new` on it is refused, and it emits no
constructor. An `abstract` member has no body and takes an empty vtable slot; the first
class down the chain that is not abstract has to fill every one of them with an `override`,
which is the conformance check an interface already gets. An abstract class in between may
leave what it inherited open and add members of its own. A property is abstract by the same
rule, one slot per accessor and no backing field. An `abstract` member outside an abstract
class is refused, and so is `abstract` on a `static` member.

```teko
// expect-exit: 42
#include "rt.tk"

abstract class Shape {
    public i64 side;

    public abstract i64 area();

    public abstract i64 Scale { get; }

    public i64 twice() {
        return area() * 2;                       // a concrete method calling the abstract one
    }
}

abstract class Sided : Shape {
    public i64 doubled() {
        return side * 2;                         // adds its own, leaves `area` open
    }
}

class Square : Sided {
    public override i64 area() {
        return side * side;
    }

    public override i64 Scale {
        get => side + 1;
    }
}

i64 main() {
    Square q = new Square;
    q.side = 4;

    Shape s = q;
    if (s.area() != 16) return 1;
    if (s.twice() != 32) return 2;
    if (s.Scale != 5) return 3;

    Sided d = q;
    if (d.doubled() != 8) return 4;
    return 42;
}
```

---

## Interfaces

An `interface` declares methods and properties — never a field — and produces no object.
A class that names one in its `:` list carries an **interface table**, and a call through
an interface-typed value walks that table, so two unrelated classes answer the same
interface and a class implements it at any vtable slot.

Every interface member is public: `private`/`protected` on one is refused, and a method
that implements an interface has to be `public`.

An interface may:

| | written | notes |
|---|---|---|
| declare a method | `i64 area();` | no receiver, as in a class |
| carry a **default body** | `i64 doubled() { return area() * 2; }` | C# 8; the class that does not redeclare it points at this symbol |
| declare a property | `i64 Label { get; set; }` | accessors only, no bodies |
| declare a `static abstract` member | `static abstract i64 unit();` | C# 11; the type provides it, resolved at compile time, no slot |
| **extend** another interface | `interface I2 : I1 { }` | the conformance set is flattened: a class naming `I2` owes `I1` too |

Inside a default body, `this` is the implementing receiver typed as the interface, and
every member reached from it dispatches through the table — so the class that redeclared a
member is the one that answers, including for the default that called it.

```teko
// expect-exit: 42
#include "rt.tk"

interface Shape {
    i64 side();

    i64 area() {
        return this.side() * this.side();        // a default body
    }

    i64 doubled() {
        return area() * 2;                       // unqualified: through the table
    }

    static abstract i64 unit();

    i64 Label { get; set; }
}

class Square : Shape {
    public i64 w;
    public i64 Label { get; set; }

    public i64 side() {
        return w;
    }

    public static i64 unit() {
        return 5;
    }
}

class Fixed : Shape {
    public i64 Label { get; set; }

    public i64 side() {
        return 3;
    }

    public i64 area() {
        return 100;                              // redeclared: the default defers to it
    }

    public static i64 unit() {
        return 7;
    }
}

i64 doubled_of(Shape s) {
    return s.doubled();
}

i64 main() {
    Square q = new Square;
    q.w = 4;
    Shape s = q;
    if (s.area() != 16) return 1;
    if (doubled_of(s) != 32) return 2;

    Fixed f = new Fixed;
    Shape t = f;
    if (t.area() != 100) return 3;
    if (t.doubled() != 200) return 4;            // the default reached the redeclaration

    s.Label = 11;
    if (q.Label != 11) return 5;                 // a property, through the table

    if (Square.unit() != 5) return 6;            // static abstract, answered by the type
    if (Fixed.unit() != 7) return 7;

    return t.area() - s.area() - Square.unit() - Fixed.unit() - 30;   // 100-16-5-7-30
}
```

### Interface inheritance and the diamond

`interface IC : IA, IB` where both declare `m()` is C#'s diamond: the class implements `m`
once and the tables of `IA`, `IB` and `IC` all point at that one symbol. There is no
covariance or contravariance.

```teko
// expect-exit: 42
#include "rt.tk"

interface IA {
    i64 m();
}

interface IB {
    i64 m();
}

interface IC : IA, IB {
}

class Box : IC {
    public i64 v;

    public i64 m() {
        return v * 2;
    }
}

i64 main() {
    Box bx = new Box;
    bx.v = 3;
    IA a = bx;
    IB b = bx;
    IC c = bx;
    if (a.m() != 6) return 1;
    if (b.m() != 6) return 2;
    if (c.m() != 6) return 3;                    // IC declares nothing of its own
    return a.m() + b.m() + c.m() + 24;
}
```

### Assignment compatibility

A derived class goes into a base-typed slot, and a class goes into a slot typed by an
interface it implements — those two conversions, and nothing else. Both are the same
reference; no object is built to perform them.

---

## Traits

A `trait` is PHP's, not an interface: its members are **copied into the class** at compile
time, so a trait field lands in the class's own layout and a trait method is the class's
own method. A class brings one in with `use Name;`, and a trait may itself `use` another.

The three precedences are PHP's:

| | wins |
|---|---|
| the class's own member vs the trait's | the **class's** |
| the trait's member vs one inherited from the base | the **trait's**, and it takes the base's virtual slot |
| a trait used by a trait | flattened into whoever uses the outer one |

A trait method names a field with no receiver, and it resolves against the class the member
was copied **into**. Only a class uses a trait; a trait declares no `const`, `insteadof`/`as`
in a `use` block are not taught, and a trait is never a base class nor an interface.

```teko
// expect-exit: 42
#include "rt.tk"

trait Counted {
    protected i64 n;

    public i64 bump(i64 k) {
        n = n + k;                               // the field of whoever uses it
        return n;
    }

    public i64 kind() {
        return 7;
    }

    public i64 label() {
        return 8;
    }
}

trait Tagged {
    use Counted;

    public i64 tag() {
        return 10;
    }
}

class Base {
    public virtual i64 kind() {
        return 99;
    }

    public virtual i64 label() {
        return 98;
    }
}

class Widget : Base {
    use Tagged;

    public i64 w;

    public override i64 label() {
        return 5;
    }
}

i64 main() {
    if (WIDGET_W != 16) return 1;                // the class's own field
    if (WIDGET_N != 24) return 2;                // the trait's, in the same layout

    Widget g = new Widget;
    if (g.bump(3) != 3) return 3;
    if (g.tag() != 10) return 4;                 // the outer trait's own method
    if (g.kind() != 7) return 5;                 // the trait beats the base
    if (g.label() != 5) return 6;                // the class beats the trait

    Base b = g;
    if (b.kind() != 7) return 7;                 // ...both through the base's slot
    return g.bump(0) + g.tag() + g.kind() + g.label() + 17;   // 3+10+7+5+17
}
```

---

## Properties

A property reads like a field at every use site and is a pair of methods everywhere else.
The three C# forms are here:

| form | written |
|---|---|
| auto | `public i64 Raw { get; set; }` — a private backing field is generated |
| expression-bodied | `public i64 Area { get => side; set => side = value; }` |
| block-bodied | `public i64 Twice { get { return Area * 2; } }` |

`value` is the name the `set` is handed, and it means that inside a `set` and nowhere else.
An accessor may narrow the property's visibility (`{ get; private set; }`) but never widen
it. A property is `virtual`/`override`/`abstract`/`static` like a method, with **one vtable
slot per accessor** — so an `override` that redeclares only the `get` inherits the `set`.
Writing to a get-only property, declaring a property with no accessor, declaring the same
accessor twice and calling a property with `()` are each refused.

```teko
// expect-exit: 42
#include "rt.tk"

class Shape {
    public i64 side;

    public virtual i64 Area {
        get => side;
        set => side = value;
    }

    public i64 Twice {
        get { return Area * 2; }
    }
}

class Square : Shape {
    public override i64 Area {
        get => side * side;                      // only the `get`; the `set` is inherited
    }
}

class Counter {
    public i64 N { get; private set; }

    public i64 bump(i64 k) {
        N = N + k;                               // the private setter, from inside
        return N;
    }
}

i64 main() {
    Shape s = new Shape;
    s.side = 3;
    if (s.Area != 3) return 1;
    s.Area = 5;
    if (s.side != 5) return 2;
    if (s.Twice != 10) return 3;

    Square q = new Square;
    q.side = 4;
    Shape b = q;
    if (b.Area != 16) return 4;                  // the override, through the slot
    b.Area = 6;                                  // the setter it did not redeclare
    if (q.side != 6) return 5;

    Counter c = new Counter;
    if (c.bump(3) != 3) return 6;
    if (c.N != 3) return 7;

    return b.Area + c.N + 3;                     // 36 + 3 + 3
}
```

---

## Operators

An operator is a `public static` member that **names both operands**: no receiver, no
`this`, no vtable slot. Which declaration a site lands on is decided by the static types of
the operands and by nothing else.

```
public static T operator<op>(A a, B b)      // binary
public static T operator<op>(A a)           // unary
```

| | |
|---|---|
| binary tokens | `+ - * / % == != < <= > >= & \| ^ << >>` |
| unary tokens | `- ! ~ +` |
| candidates of a site | the operators declared by the type of **either** operand, and by its bases |
| required | at least one parameter of the declaring type; a return value; no default on a parameter |
| pairs | `==`/`!=`, `<`/`>`, `<=`/`>=` are declared together |
| refused | `virtual`/`override`/`abstract`, `this`, an operator in an interface, a token that is not overloadable |

Resolution runs exact first, then over integer literals, then over base classes — and among
bases the **closest** ancestor wins rather than the site being called ambiguous. A reversed
form (`2 + v`, an `i64` on the left) is an ordinary declaration, because both operands are
parameters. The core's own arithmetic is untouched: `20 + 22` is still the core's sum.

```teko
// expect-exit: 42
#include "rt.tk"

class Vec {
    public i64 x;
    public i64 y;

    public static Vec operator+(Vec a, Vec b) {
        Vec r = new Vec;
        r.x = a.x + b.x;
        r.y = a.y + b.y;
        return r;
    }

    public static Vec operator+(i64 k, Vec v) {  // the reversed form
        Vec r = new Vec;
        r.x = k + v.x;
        r.y = k + v.y;
        return r;
    }

    public static Vec operator-(Vec a) {         // the unary form
        Vec r = new Vec;
        r.x = 0 - a.x;
        r.y = 0 - a.y;
        return r;
    }

    public static i64 operator==(Vec a, Vec b) {
        if (a.x != b.x) return 0;
        if (a.y != b.y) return 0;
        return 1;
    }

    public static i64 operator!=(Vec a, Vec b) {
        return !(a == b);
    }
}

Vec combine(Vec a, Vec b) {
    return a + b;                                // both operands are parameters
}

i64 main() {
    Vec a = new Vec;
    a.x = 10;
    a.y = 4;
    Vec b = new Vec;
    b.x = 20;
    b.y = 8;
    Vec c = new Vec;
    c.x = 30;
    c.y = 12;

    if (((a + b) == c) != 1) return 1;           // `+` resolved first, then `==`
    if (((a + b) != c) != 0) return 2;
    if ((combine(a, b) == c) != 1) return 3;

    Vec d = -a;
    if (d.x != 0 - 10) return 4;

    Vec e = 2 + a;
    if (e.x != 12) return 5;

    Vec s = a + b;
    return s.x + s.y;                            // 30 + 12
}
```

---

## `partial`

The same class may be declared in more than one place and is united at compile time. The
first part opens the row, every part after it adds members, and the type **closes at its
first use** — a part written after that is refused where it stands. The base class is named
in the first part, before any member is laid out; interfaces are a union and may come in
any part. A generic is partial by the same rule. There is no partial **method**.

```teko
// expect-exit: 42
#include "rt.tk"

partial class Shape {
    public i64 w;

    public i64 area() {
        return w * h;                            // `h` is declared in the other part
    }
}

class Between {
    public i64 z;                                // declared between the parts, unaffected
}

partial class Shape {
    public i64 h;
}

i64 main() {
    Shape s = new Shape;
    s.w = 6;
    s.h = 7;
    Between t = new Between;
    t.z = 0;
    return s.area() + t.z;
}
```

---

## Order of declaration is free

A type may be used above the line that declares it: as a field, a parameter, a return type,
a local, a base class, an interface, the target of `new`, the owner of a static member or a
`const`, and as the type a `use` names. The declaration is materialized on the spot, so the
base is whole — fields, virtual slots and constructor — before the derived class lays
itself out.

```teko
// expect-exit: 42
#include "rt.tk"

class Holder {
    public Circle mine;                          // `Circle` is declared below

    public Circle same(Circle c) {
        return c;
    }
}

class GrandDog : Dog {
    public override i64 speak() {
        return base.speak() + 10;
    }
}

class Dog : Animal {
    public Dog(i64 n) : base(n) {
    }

    public override i64 speak() {
        return base.speak() + 1;
    }
}

class Animal {
    public i64 legs;

    public Animal(i64 n) {
        legs = n;
    }

    public virtual i64 speak() {
        return legs;
    }
}

class Circle {
    public i64 r;
}

i64 main() {
    Holder h = new Holder;
    Circle c = new Circle;
    c.r = 7;
    h.mine = c;
    if (h.same(c).r != 7) return 1;

    Dog d = new Dog(4);
    if (d.speak() != 5) return 2;

    GrandDog g = new GrandDog;                   // no constructor of its own
    if (g.speak() != 11) return 3;               // 0 + Dog's +1 + GrandDog's +10

    return h.mine.r + d.speak() + g.speak() + 19;   // 7 + 5 + 11 + 19
}
```

---

## Limits

| limit | value |
|---|---|
| base classes per class | 1 |
| destructors per class | 1 |
| parameters of a method | 12, the receiver and the vtable pointer of a virtual call included |
| methods, summed across all types | 128 |
| virtual slots, summed across all types | 128 |
| interface methods, summed across all interfaces | 128 |
| interfaces named in one class's `:` list | 8 |
| (class, interface) pairs, summed across all classes | 64 |
| constructors, summed across all classes | 32 |
| traits declared in one source | 32 |
| properties, summed across all types | 64 |
| operators, summed across all types | 64 |
| a type inside a type | not taught |
| a partial method | not taught |
| `insteadof`/`as` in a `use` block | not taught |

The full catalogue of refusals is [not-yet.md](not-yet.md) and
[diagnostics.md](diagnostics.md).
