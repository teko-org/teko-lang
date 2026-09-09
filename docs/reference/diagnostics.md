# Diagnostics

Every message the taught compiler can refuse a program with, by family, with the cause and
— where there is one — the fix. A diagnostic is written

```
file:line: teko: <cause>
```

and the compiler exits 1. A failure at **run time** is a panic with exit 70 and is
[memory.md](memory.md) § Panics instead.

The list below is checked against the sources by
[`../../scripts/check-docs.sh`](../../scripts/check-docs.sh): a message that exists and is
not documented here fails the `docs` gate. Some messages end mid-sentence in this page
because the compiler appends a name to them — the quoted part is the fixed text, and the
entry says what completes it.

---

## Declarations and types

- `"teko: a type is declared at top level; there is no type inside a type"` — a `class`,
  `struct`, `interface`, `trait` or `enum` inside another type's body. Move it out; there
  is no nested type.
- `"teko: the name is already a type"` — the name is already a `class`, `struct`,
  `interface`, `trait` or `delegate` in this namespace.
- `"teko: the name is already a generic"` — the name belongs to a generic declaration.
- `"teko: name of "` — completed by *`<what>` expected*: a declaration keyword was read and
  what followed is not a usable name.
- `"teko: type not taught yet"` — the word in type position is reserved for a construct
  this version does not implement.
- `"teko: a value of type "` — completed by *`X` does not convert to `Y`*: the only implicit
  reference conversions are derived-to-base and class-to-interface, the only implicit
  numeric one is an integer into a float ([types.md](types.md#f32-and-f64)), and nothing
  narrows back. A value of float type (`f64`, `f32`) never lands in a slot of another
  kind, `null` — whose type is `uptr` — never lands in a numeric one, and neither does a
  NON-null reference: a struct, a class, an interface, a delegate or a `T[]` of heap
  written where an `i64`/`f64` is declared (D34). All three hold in every slot: an
  argument (of a free function, a method, a virtual call, an interface call), an
  overload's parameter, an element of a `params` list, an assignment, an initializer, a
  field store and a `return`
  ([parameters.md](parameters.md#what-a-literal-converts-to)). `i64 n = 2.5;`,
  `solo(null)` against a single `i64 solo(i64)` and `i64 n = f;` with `f` a class value
  are the three shortest forms of it, all written out in
  [types.md](types.md#f32-and-f64).
- `"teko: field of type void"` — a field has a type; `void` is a return type only.
- `"teko: duplicate field"` — two fields of one type share a name.
- `"teko: an array field size is an integer literal"` — `T items[N]` takes a literal or a
  `const`, folded at the declaration.
- `"teko: an array field size is positive"` — the size is `> 0`.
- `"teko: a member declaration needs a name"` — a member's type was read and no name
  followed.
- `"teko: the modifier opens a class, a struct, an interface, a trait, a delegate or an enum"`
  — `public`/`internal` at top level must be followed by one of those six words.
- `"teko: only a class is abstract"` — `abstract` on a `struct`, an `interface` or a
  `trait`.
- `"teko: only a class is partial"` — `partial` on anything but a class.
- `"teko: the declaration already has a visibility"` — `public internal class ...`.
- `"teko: the declaration is already abstract"` — `abstract` written twice.
- `"teko: the declaration is already partial"` — `partial` written twice.
- `"teko: the type is declared without `partial`"` — a second declaration of a name whose
  first one is not `partial`. Write `partial` on every part.
- `"teko: this part comes after the type was used"` — the type closed at its first use;
  move the part above that use.
- ``"teko: the parts disagree on `abstract`"`` — one part says `abstract` and another does
  not.
- ``"teko: the parts disagree on `public`/`internal`"`` — the parts give the type two
  visibilities.
- `"teko: the base class is named in a part that comes before the members"` — the base
  decides where the fields start, so it is named in the first part, before any member.
- `"teko: a partial base is not forward-declarable yet"` — a `partial` class used as a base
  before it is declared.
- `"teko: a partial method is not taught; only a partial class"` — `partial` on a member.
- ``"teko: the declaration disagrees with its materialized form on `abstract`"`` — the
  declaration read ahead of use said one thing and the real one says another.
- ``"teko: the declaration disagrees with its materialized form on `public`/`internal`"`` —
  the same, for visibility.

## Members and modifiers

- `"teko: the member already has a visibility"` — two of `public`/`private`/`protected` on
  one member.
- `"teko: the member is already abstract"` — `abstract` twice.
- `"teko: the member is already const"` — `const` twice.
- `"teko: the member is already static"` — `static` twice.
- `"teko: the member is already virtual, override or abstract"` — two of the three.
- `"teko: const is already static; drop static"` — a `const` occupies no slot and is
  already reached through the type.
- `"teko: a static field and a method cannot share a name"` — a static field becomes a
  global of its own; a method of the same name would collide.
- `"teko: a const and a member cannot share a name"` — the same, for a member `const`.
- `"teko: a property and a field cannot share a name"` — a property's accessors would
  collide with the field.
- `"teko: duplicate method"` — two methods of one type with the same name **and**
  signature. Change one signature, or the name.
- `"teko: method name reserved by the class"` — the name is one the class machinery
  generates (an accessor, a release, an allocator).
- `"teko: method with too many parameters (the receiver counts, and so does the vtable pointer of a virtual call)"`
  — the ABI takes at most twelve.
- `"teko: parameter of type void"` — a parameter has a type.
- `"teko: an instance member is not reachable from a static method"` — a static method has
  no receiver. Pass the object as a parameter.
- `"teko: a type name reaches its static members"` — a type name in expression position was
  not followed by `.`.
- `"teko: a constant is not assigned or called"` — a member `const` was written as the
  target of `=` or of a call.
- `"teko: unknown static member of "` — completed by the type's name: no static field,
  method, property or `const` of that name.
- `"teko: an array field on a type declared below is not taught yet"` — reaching an inline
  array field through a type whose declaration is further down.
- `"teko: an array field is assigned one element at a time"` — `p.items = e` on an inline
  array field. Assign `p.items[i]`.
- `"teko: an array field is read one element at a time"` — the same, reading.
- ``"teko: an array field is reached through `this.`"`` — the bare name does not reach an
  inline array field; write the receiver.
- `"teko: methods take no explicit receiver; use this"` — a parameter named as the
  receiver. The receiver is implicit; `this` names it.
- ``"teko: `this` is only valid inside the body of a type"`` — `this` in a free function.
- ``"teko: `this` is not there in a static member"`` — a static member has no receiver.
- ``"teko: `base` is not there in a static member"`` — the same, for `base`.
- ``"teko: `base` in a type with no base class"`` — the type has no base to reach.
- ``"teko: `base` reaches a method of the base class"`` — `base` was followed by something
  that is not a method call.
- `"teko: the base class has no such method"` — `base.m()` where no base declares `m`.
- `"teko: the base's method is abstract; it has no body to call"` — an abstract member has
  nothing to run directly.
- `"teko: the base's method is static; reach it through its type"` — write `Type.m()`.
- ``"teko: `value` is the value a `set` accessor is handed"`` — `value` used outside a
  `set`.

## Constructors and destructors

- ``"teko: a constructor chains to `base`"`` — what follows `:` in a constructor header is
  `base(...)`.
- `"teko: a constructor fills no vtable slot; it is not virtual"` — `virtual`/`override`/
  `abstract` on a constructor.
- `"teko: a constructor is written without a return type"` — the member named after its
  type carries no return type; `void Name(...)` is C#'s own mistake.
- `"teko: a constructor takes a receiver; it is not static"` — `static` on a constructor.
- `"teko: a destructor is named after its type"` — `~Other()` inside `class Name`.
- `"teko: a destructor takes no modifier"` — no `public`, no `static`, nothing.
- `"teko: a destructor takes no parameter"` — `~Name()` and nothing else.
- `"teko: the class already has a destructor"` — one per class.
- `"teko: two constructors with the same parameter types"` — overload by signature; these
  two are the same signature.
- `"teko: ambiguous constructor; two of them take this many arguments"` — the site's
  argument count fits two constructors.
- `"teko: ambiguous base constructor; two of them take this many arguments"` — the same,
  for `: base(...)`.
- `"teko: no constructor of this class takes these arguments"` — no constructor of that
  arity.
- `"teko: no constructor of the base class takes these arguments"` — the same, for
  `: base(...)`.
- ``"teko: the base class has no constructor taking no argument; write `: base(...)`"`` —
  the base has to be chained explicitly.
- `"teko: an abstract class is not instantiated"` — `new` on an `abstract class`.
- `"teko: an interface has no object to allocate"` — `new` on an interface.
- ``"teko: a trait is not a type; `new` needs a struct or a class"`` — a trait is copied
  into a class, never instantiated.
- ``"teko: unknown struct or class after `new`"`` — the name after `new` is no known type.

## Inheritance, `virtual` and `abstract`

- `"teko: a class has one base class"` — two class names in the `:` list.
- `"teko: a base has to be a class, not a struct"` — a struct has no vtable, hence no
  derivation.
- `"teko: the base class comes before the interfaces"` — the base is the first entry of the
  `:` list.
- `"teko: cyclic base"` — the chain of base classes closes on itself.
- `"teko: unknown base class or interface"` — a name in the `:` list is no known type.
- ``"teko: a trait is not a base class nor an interface; use `use`"`` — bring a trait in
  with `use Name;` inside the body.
- ``"teko: a struct has no vtable; `virtual`/`override` needs a class"`` — a struct's
  methods are direct calls.
- `"teko: method hides an inherited virtual; use override"` — the base declares that slot;
  say `override`.
- `"teko: override of a method the base does not declare"` — there is no slot to fill.
- `"teko: the slot is inherited; use override"` — a second `virtual` on an inherited slot.
- `"teko: virtual/override on a field"` — only a method or a property takes a slot.
- `"teko: a static member has no vtable slot; it is not virtual"` — a static member is
  reached through the type.
- `"teko: const has no vtable slot; it is not virtual"` — a `const` occupies nothing.
- `"teko: a static member is not abstract"` — `static abstract` belongs to an interface.
- `"teko: an abstract member has no body"` — write `;` after the signature.
- `"teko: an abstract member needs an abstract class"` — the class holding it says
  `abstract` too.
- `"teko: abstract method not overridden"` — the first concrete class down the chain fills
  every abstract slot.
- ``"teko: the `get` of an abstract property not overridden"`` — one slot per accessor, and
  this one is still empty.
- ``"teko: the `set` of an abstract property not overridden"`` — the same, for the setter.

## Interfaces

- `"teko: an interface declares methods and properties, not fields"` — there is no
  interface field.
- `"teko: an interface extends another interface, not a class or a struct"` — an
  interface's `:` list names interfaces.
- `"teko: an interface member is public"` — `private`/`protected` on an interface member.
- `"teko: an interface property declares accessors, not bodies"` — `{ get; set; }`, no
  body.
- `"teko: an abstract interface member has no body"` — a member declared abstract carries
  no body.
- ``"teko: a static interface member is `static abstract`"`` — a `static` interface member
  is the C# 11 form; write `static abstract`.
- ``"teko: a static interface member is reached through the type"`` — call it as
  `Type.m()`, never through an instance.
- ``"teko: the interface declares the member `static abstract`"`` — the implementing type
  provides it as `static`.
- `"teko: a static method implements no interface"` — a `static` method cannot be the
  implementation of an instance signature.
- `"teko: the method of an interface is implemented by a public one"` — the implementing
  method has to be `public`.
- `` "teko: method of `" `` — completed by *`I` not implemented*: the class owes that
  method.
- `` "teko: the `get` of a property of `" `` — completed by *`I` not implemented*.
- `` "teko: the `set` of a property of `" `` — completed the same way.
- `"teko: method with a return type different from the interface"` — the signature has to
  match exactly.
- `"teko: method with an arity different from the interface"` — the same, for the count.
- `"teko: method with parameter types different from the interface"` — the same, for the
  types.
- `"teko: cyclic interface base"` — the `:` chain of interfaces closes on itself.
- `"teko: duplicate interface method"` — two members of one interface with the same
  signature.
- `"teko: interface with no methods"` — an interface declares at least one member.
- `"teko: unknown base interface"` — a name in an interface's `:` list is unknown.
- `"teko: an interface call needs a name or a field on the left"` — a call through the
  interface table needs a receiver the compiler can name.
- ``"teko: a trait is not an interface; use `use`"`` — a trait in a `:` list.
- `"teko: an operator is declared by a class or a struct, not by an interface"` — an
  operator is a static member of a concrete type.

## Traits

- ``"teko: `abstract` in a trait not taught yet"`` — a trait's members carry bodies.
- ``"teko: `insteadof`/`as` in a `use` block not taught yet"`` — conflict resolution in a
  `use` block is not implemented; rename or redeclare in the class.
- `"teko: a trait brings no "` — completed by *constructor* or *destructor*: both belong to
  the class itself.
- `"teko: a trait brings no const; only a class or struct declares one"` — declare the
  `const` in the class that uses the trait.
- `` "teko: field of trait `" `` — completed by *`T` collides with a field of the class*:
  rename one of the two.
- `"teko: only a class uses a trait"` — a struct has no `use`.
- `"teko: the class already uses this trait"` — `use` the same trait twice.
- `"teko: two traits bring the same member"` — flattening two traits would give the class
  the member twice. Redeclare it in the class, which wins over both.
- `"teko: two traits bring the same property"` — the same, for a property.
- `"teko: duplicate trait"` — two traits of one name.
- `"teko: expected { in the trait body"` — a trait's body is a block.
- `"teko: expected { in the trait method body"` — a trait method carries a body.
- `"teko: trait cycle"` — a trait that uses itself, directly or through another.
- `"teko: unknown trait"` — the name in `use` is no trait.
- `"teko: unterminated trait"` — the file ended inside the trait's body.

## Enums

- `"teko: an enum's underlying type is one of u8 u16 u32 u64 i8 i16 i32 i64"` — the eight
  types C# allows after `:` in `enum Name : underlying { ... }`; anything else, `f64`
  included, is refused.
- `"teko: an enum declares at least one member"` — `enum Empty { }`.
- `"teko: duplicate enum member"` — completed by the member's name: two members of one
  enum share a name. Two members sharing a VALUE is legal (C#'s aliases).
- `"teko: an enum member value is a constant expression"` — the value after `=` has to
  fold to an integer at compile time, the same rule a top-level `const` follows.
- `"teko: "` — completed by *`Name` has no member `Member`*: the member on the right of
  `Name.` is not one this enum declares. The bare member name (`Red` without `Color.`) is
  an ordinary identifier and resolves to nothing, `mc`'s own "unknown name".
- `"teko: a value of type "` — an enum converts from and to nothing but itself, implicitly:
  a bare integer (the literal `0` included), a different enum, `null`, and any reference
  type all refuse a slot of enum type this way, and an enum refuses a numeric slot the
  same way, from the other direction (`i64 n = c;`, `Color c = 0;`,
  [types.md](types.md#enum)). The two explicit conversions this refusal does not cover —
  `(i64) c` and `(Color) n`, a plain machine cast, unchecked — are C#'s own escape hatch.
- `` "teko: no operator `" `` — completed by *`X` takes these operands*: an enum computes
  only the six comparisons and `& | ^ ~`, and only against the SAME enum; every other
  operator, and a mismatched or bare-integer operand on any of those six, is refused this
  way too.

### `ToString`, `Parse`, `TryParse`, `IsDefined` (N2b)

Four names, dispatched by the enum's own row rather than by
[teko_prim.tk's lowering table](#primitives-with-members-timespan-datetime): registering
an enum there would make `tk_ty_binary` ask a table its own bitwise/comparison operators
never populate. The two globals a text-using enum needs (`Color__names`, `Color__vals`)
are built lazily, the first time one of these four names is spelled on that enum — an enum
a program never asks text of pays nothing.

- `"teko: wrong number of arguments for "` — completed by the name (`Parse`, `IsDefined` or
  `ToString`): each takes exactly one argument, `ToString` takes none.
- `"teko: TryParse's second argument is `out <name>`"` — `Color.TryParse(s, out c)`'s second
  argument has to be `out` over a variable; a bare value, `ref`, or nothing at that position
  is refused this way.
- `"teko: a value of type "` — `TryParse`'s `out` argument has to be a variable already
  declared the SAME enum type: `i64 x; Color.TryParse(s, out x);` is refused the same way an
  ordinary mismatched value is (D34's own wording, reused).
- `"teko: "` — completed by *`Name` needs #include "rt.tk" before it is used*: `ToString`,
  `Parse`, `TryParse` and `IsDefined` all lower to `lib/rt.tk` (§ 9 of
  [docs/specs/enum.md](../specs/enum.md), the same rule `TimeSpan` follows), and the message
  names the file a program forgot instead of reaching `mc`'s own "call to unknown function".
- `"teko: the member is a method; call it with ()"` — `c.ToString` without `()` (reused
  verbatim from the primitives table's own wording).
- `"teko: too many enums asked for text in one unit"` — more than 128 distinct enums asked
  `ToString`/`Parse`/`TryParse`/`IsDefined` of in one compilation unit; `[limits] tolerance`
  is the escape hatch, the same one every other fixed table in this port gives.

`Color.Parse(s)` **panics** (exit 70, `rt_panic`) on a name no member spells, with
`the string is not a `, followed by the enum's own name — C#'s own `FormatException` road,
teko's own answer to it (no exceptions in this language). `TryParse` answers 0/1 instead and
never panics. Both are runtime messages, from `lib/rt.tk`, not a compile-time `teko: …`
refusal, so they carry no entry of their own in this list (D19 scans `teko*.tk`, the
compiler's own sources, not the runtime library it teaches programs to link).

## Primitives with members (`TimeSpan`, `DateTime`)

A primitive is a type of eight bytes with no row in the type table — no field, no vtable,
no method — whose members come from a lowering table instead
([timespan.md](timespan.md), [datetime.md](datetime.md),
[the internals note](../internals/primitives.md)). The
messages reuse the wordings a declared type already gets; only a handful are its own.

- `"teko: "` — completed by *`TimeSpan` needs #include "time.tk" before it is used*: the
  type word is reserved program-wide, but the functions its members lower to live in
  `lib/time.tk`. Add the include.
- `"teko: unknown member of "` / `"teko: unknown static member of "` — completed by the
  type's name and then by the member: the name is not a row of that primitive's table.
- `"teko: "` — completed by *`TimeSpan.Ticks` is an instance member; reach it through an
  object* (a member read through the type word) and by *`TimeSpan.Zero` is static; reach it
  through its type* (a static read through a value). The two wordings are the ones a class
  already answers with.
- `"teko: a member of "` — completed by *`TimeSpan` is read-only*, then by the member's
  name: a member of a primitive is a value read out of its eight bytes, never a slot, so
  `t.Ticks = 5;` has nothing to assign to.
- `"teko: the member is a property; it is not called"` — `t.Ticks()`; drop the `()`.
- `"teko: the member is a method; call it with ()"` — `t.Duration`; add them.
- `"teko: wrong number of arguments for "` — completed by the member's name: no row of
  that name takes as many arguments as the site wrote. `new DateTime(...)` takes 1, 2, 3, 6
  or 7 of them and nothing else.
- `"teko: a value of type "` — completed by *`X` does not convert to `Y`*: a primitive
  converts to nothing but itself, in either direction and in all nine slots — an integer, a
  float, `null`, a class, a struct, a `T[]` or a different primitive into a `TimeSpan`
  slot, and a `TimeSpan` into an `i64`, an `f64` or a slot of row type.
- `` "teko: no operator `" `` — completed by *`X` takes these operands*: the operator table
  claims every binary and unary with a primitive operand and refuses the ones with no row
  (`t % t`, `t & t`, `t * t`, `t / t`, `t + 1`, `~t`, `!t`, `+t`).
- ``"teko: the type of the left side of `+` is not known here"`` (also *right*) — the other
  operand of an operator over a primitive is an expression the oracle cannot type, such as
  an array element. Bind it to a local first; a primitive operand is exactly where leaving
  the node to the core's raw arithmetic would answer a wrong number.
- `"teko: this primitive has no constructor"` — `new` on a primitive whose table declares
  no constructor row. `TimeSpan` declares one, so nothing in v0.4.0 reaches this; it is the
  mechanism's own guard for the primitives the specs still have coming.
- `"teko: a type name reaches its static members"` — the type word alone, with no `.`
  after it, in expression position.
- `"teko: the field is not an array"` — completed by the member's name: `t.Ticks[0]`.
- `"teko: a "` — completed by *`X` does not cast; `.Ticks` reads it and `new X(...)` builds
  it*: `(i64) d` and `(DateTime) n` written by hand, and the same pair over a `TimeSpan`.
  A cast is the one syntax that would convert what converts to nothing but itself, and over
  a `DateTime` it would answer a wrong number rather than refuse — the `Kind` sits in the
  two bits above the ticks. The compiler writes both casts itself, in the lowering, and
  knows its own.
- `"teko: "` — completed by *`DateTime.Now` is not taught yet*, and by `UtcNow` and
  `Today`: the three need a wall clock, which is one symbol per operating system and `mc`'s
  to give ([the spec](../specs/datetime.md) § 8). The member is named by the table so that
  the site says so, instead of reading as a member nobody declared.
- `"teko: unknown static member of DateTimeKind"` — completed by the member: the three
  values are `Unspecified`, `Utc` and `Local`.

The three panics `lib/time.tk` raises for a `TimeSpan` at RUN time (`a time span
overflowed`, `a time span divided by zero`, `a time span is out of range`) and the eleven it
raises for a `DateTime` (`a date does not exist`, `a date is out of range`, `a year is out
of range`, `a month is out of range`, `an hour is out of range`, `a minute is out of
range`, `a second is out of range`, `a millisecond is out of range`, `a date kind is out of
range`, `a month count is out of range`, `a year count is out of range`) are exit 70 and
are listed in [runtime.md](runtime.md#the-time-library).

## Properties

- ``"teko: a property declares `get`, `set` or both"`` — an empty accessor list, or a word
  that is neither.
- ``"teko: an accessor is `;`, `=> ...;` or a block"`` — the three forms an accessor has.
- `"teko: an accessor is not more visible than its property"` — `{ get; public set; }` on a
  `private` property; an accessor only narrows.
- `"teko: an auto-property has no accessor with a body"` — `{ get; set => e; }` mixes the
  two forms; pick one.
- `"teko: duplicate property"` — two properties of one type share a name.
- `"teko: property of type void"` — a property has a type.
- `"teko: the accessor already has a visibility"` — two modifiers on one accessor.
- ``"teko: the property already declares `get`"`` — two getters.
- ``"teko: the property already declares `set`"`` — two setters.
- `"teko: the property already declares this accessor"` — the same, in an interface.
- ``"teko: the property has no `get`"`` — reading a set-only property.
- ``"teko: the property has no `set`"`` — assigning to a get-only property.
- `"teko: the member is a property; it is not called"` — write `p.Name`, not `p.Name()`.
- `"teko: unterminated accessor"` — the file ended inside an accessor.
- `"teko: unterminated property"` — the file ended inside the accessor list.

## Operators

- ``"teko: `operator` is followed by one of "`` — completed by the list of overloadable
  tokens.
- `"teko: an operator is a method; it takes a parameter list"` — `operator+` with no `(`.
- `"teko: an operator is static and names both operands"` — the receiver form is not
  accepted: write `public static T operator+(A a, B b)`.
- `"teko: an operator is static; it is not virtual, override or abstract"` — an operator
  fills no slot.
- `"teko: an operator names an operand of the type that declares it"` — at least one
  parameter is of the declaring type.
- `"teko: an operator parameter has no default; a site always passes it"` — every operand
  comes from the site.
- `"teko: an operator returns a value"` — `void` is not an operator's return type.
- `` "teko: the operator `" `` — completed by *`X` names one operand or two* (the arity does
  not match the token) or by *`X` is declared with `Y`* (the pairs `==`/`!=`, `<`/`>`,
  `<=`/`>=` are declared together).
- `` "teko: no operator `" `` — completed by *`X` takes these operands*: no declaration of
  either operand's type fits.
- `` "teko: more than one operator `" `` — completed by *`X` takes these operands*: two
  candidates fit and neither dominates.
- `"teko: the type of the "` — completed by *left|right side of `X` is not known here*: the
  operand's type could not be determined, so the operator could not be resolved.
- `"teko: this operator cannot be overloaded"` — the token is not in the overloadable set.
- `` "teko: `" `` — completed by *`X` is a binary operator; it names two operands*, by
  *`X` is a unary operator; it names one operand*, or, for a generic, by *`X` takes N
  arguments*.

## Generics

- ``"teko: a const generic argument is an integer literal or a declared const"`` — the
  argument has to be folded at compile time.
- ``"teko: a const generic parameter is an `i64`"`` — the only type a `const` parameter
  takes.
- `"teko: duplicate generic"` — two generic declarations of one name.
- `"teko: not a generic type"` — a `<` after a type that declares no parameters. This is
  also what `Func<>`/`Action<>` answer: there is no generic delegate.
- `"teko: the generic produced no type"` — the replay of the recorded body declared
  nothing.
- `"teko: the parts disagree on the generic parameters"` — two `partial` parts of one
  generic list different parameters.
- `"teko: unknown type as a generic argument"` — the argument names no known type.
- `"teko: unterminated generic"` — the file ended inside a recorded body or an argument
  list.

## Delegates, lambdas and captures

- ``"teko: `new Op(...)` takes the name of a function or a lambda"`` — the argument of a
  delegate constructor is a function name or a lambda literal.
- ``"teko: `use` captures a local; this name is not one"`` — only a local of the declaring
  function may be captured; a parameter cannot.
- `"teko: a capture by reference of a counted type is not taught yet"` — capture the object
  **by value**; the closure then holds a reference of its own.
- `"teko: a lambda that captures by reference cannot leave its scope"` — such a closure
  cannot be returned, nor stored in a field or a static field.
- ``"teko: a delegate declared below `new` is not taught yet"`` — move the `delegate`
  declaration above the `new` that names it.
- `"teko: an array of this type is not taught yet"` — a fixed array whose element is a type
  with no layout of its own here; use `T[]`.
- `"teko: expected a captured name"` — `use ()` with something that is not a name inside.
- `"teko: use (...) already captures"` — the same name twice in one `use`.
- `"teko: the lambda does not match the delegate"` — the parameter count or the types
  differ from the delegate's.
- `"teko: unknown function"` — the name given to a delegate is no function.
- `"teko: argument "` — completed by *N is not passed by reference* or by *N needs
  `ref`/`out`*: a delegate or a function declares that parameter by reference and the site
  does not say so.
- `"teko: wrong number of arguments for "` — completed by the name: the call's arity does
  not match.
- `"teko: "` — completed by one of the delegate-shaped messages: *`X` does not match the
  delegate `Op(...)`*, *`Op` takes a function, another `Op`, or null*, *`X` is not
  captured; add it to use (...)*, and *`X` is used but never declared*.

## Arrays

- ``"teko: `new T[]` needs a length; write `new T[n]`"`` — the length is an expression, and
  it is required.
- `"teko: an array has no member"` — a `T[]` has `Length` and nothing else.
- `"teko: an array of arrays is not taught yet"` — `T[][]` and any multidimensional form.
- `"teko: an array of objects is not taught yet; use a field array or wait for T[]"` — a
  **fixed** array of a class or struct type. Use `T[]`.
- `"teko: an array of type void"` — an element has a type.
- `"teko: index "` — completed by *K is out of range for `name[N]`*: a literal index outside
  the bounds, caught at compile time.
- `"teko: is read-only"` — `xs.Length = e`.
- `"teko: not a known array"` — an index or a `.Length` on something this compiler does not
  know to be an array.
- `"teko: the field is not an array"` — the member indexed is a plain field.
- `"teko: the left side of = is not a place"` — the target of the assignment is not a
  variable, a field or an element.
- ``"teko: `[` needs an array"`` — an index on a receiver whose type the parse does not
  know to be one. Bind it to a local of the right type first.

## The nullable `T?`

Every one of these is [nullable.md](nullable.md)'s.

- `"teko: null needs a slot declared "` — completed by the slot's own type and a `?`
  (`teko: null needs a slot declared Cell?`). `null` lands only in a slot written `T?`;
  declare it that way, or build a value. A COMPARISON against `null` is untouched.
- `"teko: a Cell? is read through .Value"` — the `a ` and the ` is read through .Value`
  around the type's own name: a member of the enclosed type, read straight off the
  nullable. `Nullable<T>` has the members `Nullable<T>` has, so write `c.Value.v`.
- `"teko: unknown member of Cell?: zz"` — the same wording every other receiver gets, for a
  name that is a member of nothing.
- `"teko: .Value is not a slot"` — `x.Value = e`. A nullable is written whole.
- `"teko: a reference nullable has no default"` — `GetValueOrDefault()` on a `T?` over a
  reference. Its C# answer is `null`, which is the one value a `T` slot may not take.
- `"teko: too many nullable value types in one unit"` — more than 32 distinct payload
  types boxed in one source. Each one costs a writer of three lines; the ceiling is the
  table that remembers which have been emitted.
- `"teko: "` — completed by *`i64?` declares no operator `+`* (the row's own name, then the
  operator's spelling between backticks): a nullable operand takes `==`/`!=` against `null`
  and nothing else — every other binary, and `==`/`!=` against anything but `null` (another
  nullable included, before Q4a's lifted rule), is refused by name. The handle is not the
  value: `a == 5` would compare the box's own address against five, always false, which is
  the mistake this claim exists to catch (`tk_op_none_msg`, [teko_ops.tk](../../teko_ops.tk),
  the same wording an ordinary type that declares no operator already gets).
- `"teko: "` — completed by *`i64?` is not a condition* (the row's own name; a `bool?`
  prints as `u8?`): a nullable used bare as `if`'s condition or the ternary's. A
  `while`/`for`/`do` guard is `!(cond)`, so there the `!` on a nullable meets the unary
  form of the operator refusal above first, ``declares no operator `!` ``.
  `a.HasValue`, `a == null` or `a.Value` is the form.
- `"teko: too many HasValue reads in one unit"` — more than 64 `.HasValue` reads in one
  source. `HasValue`'s own lowering (`left != 0`) is marked so the operator claim above does
  not refuse its own code; the mark table is this ceiling.
- `"teko: a nullable of a nullable is not taught"` — `T??`, and `T[]??`.
- `"teko: a raw pointer has no nullable"` — `uptr?`, `ptr?`, `str?`. `0` is an ordinary
  value of a raw pointer, and `null` already lands in one.
- `"teko: void? is not a type"` — `void?`.
- `"teko: a nullable is not a generic argument yet"` — `Box<Cell?>`. A type argument
  travels as a spelling and `Cell?` is not one the lexer can form.
- `"teko: a value of type Cell? does not convert to Cell"` — the ordinary conversion
  wording: `T?` does not convert to `T`, and one nullable row does not convert to another.
  Write `.Value`. A nullable over a VALUE is judged by the same wording under its own
  name: `teko: a value of type f64 does not convert to i64?`, and
  `teko: a value of type i64 does not convert to Color?`, since an `enum` and a primitive
  with members take their own type and nothing else — inside a box as outside one.
- `"teko: a nullable with no value"` — a run-time **panic**, exit 70: `.Value` on a handle
  of 0 ([memory.md](memory.md)).
- `"teko: ?? needs a nullable on the left"` — `k ?? 7` where `k` is not a `T?`. It is also
  what `a || b ?? c` prints: that reads as `(a || b) ?? c`, the precedence divergence
  [not-yet.md](not-yet.md) records, and `||`'s result is a truth value. Write the
  parentheses.
- `"teko: ??= is not taught"` — `a ??= b`. `??=` is no lexeme of its own; `a = a ?? b;` is
  the form.
- `"teko: ?. needs a nullable on the left"` — `h?.v` where `h` is a plain `Cell`. A member
  of a value that cannot be absent is read with `.`.
- `"teko: ?. needs a value"` — `a?.m()` where `m` returns `void`. The lowering is an
  expression and a `void` arm has no type; write `if (a != null) a.Value.m();`.
- `"teko: ?. is not a slot"` — `a?.v = e`. A member is written through `.Value`, once the
  nullable is known to have one.
- `"teko: ?. reads a member, not an element"` — completed by the member's own name:
  `a?.items[0]`. Bind the member first.
- `"teko: bind the ?? or ?. result to a variable before reading a member"` — a plain `.` on
  what either operator answers. `.` and `?.` share precedence 12, so `a?.b.c` would read as
  `(a?.b).c` where C# short-circuits the whole chain; `a?.b?.c`, or a local of its own, is
  the form.
- `"teko: tk_qdot is the compiler's own name"` — a call spelled `tk_qdot(...)` in the source: that name is the placeholder `?.` lowers through, and a program never calls it.
- `"teko: too many ?. accesses in one unit"` — more than 64 `?.` in one source. Each one
  remembers the member name and the form its rewrite needs; the table is this ceiling.
- `"teko: "` — completed by *`name` is used before it is assigned*: a **local** declared
  without `?` and without an initializer, read with no assignment to it anywhere earlier in
  the body. Reported at the read. Build it (`P p = new P();`), assign it before the read,
  or declare it `T?` if it is allowed to hold nothing. `f(out x)`, `f(ref x)`, a `foreach`
  variable, a `for` initialiser and a `use (...)` capture all count as assignments, and so
  does an assignment inside an `if`, a `loop` or a nested block, whether or not it runs —
  the rule over-approximates so it can never refuse a correct program
  ([nullable.md](nullable.md) § Definite assignment).

## Namespaces, `using` and `import`

- `"teko: a nested namespace is not taught"` — one level; write `namespace A.B` instead of
  a block inside a block.
- `"teko: a namespaced type at top level declares a function"` — a namespaced type name at
  top level opens a function declaration, not a statement.
- `"teko: a global is declared outside every namespace"` — a global belongs outside a
  namespace block.
- `"teko: an extern is declared outside every namespace"` — the same, for an `extern`.
- `"teko: main is declared outside every namespace"` — the entry point is not namespaced.
- ``"teko: expected { or ; after the namespace path"`` — a namespace is a block or is
  file-scoped.
- `"teko: unterminated namespace"` — the file ended inside a namespace block.
- `"teko: the file already has a file-scoped namespace"` — one per file.
- `"teko: ambiguous name "` — completed by *`X` (a, b)*: two `using`s offer the same short
  name. Qualify it.
- `"teko: unresolved name"` — the short name matches no type of the current namespace, of a
  prefix, or of a `using`.
- `"teko: unresolved qualified name"` — the qualified path names nothing.
- `"teko: the generated name is already declared"` — qualifying the declaration produced a
  name that already exists.
- `"teko: expected ; after import"` — `import A.B;`.
- `"teko: import is refused inside a namespace"` — it belongs at the top of the file.
- `"teko: import comes before every namespace in its own file"` — write every `import`
  above the file's own namespace.

## Visibility

- The access messages are the ones the compiler completes with a name: *`X.m` is private*,
  *`X.m` is protected* and *`X` is internal to another project*. `private` is the declaring
  type only, `protected` adds the types derived from it, and `internal` — the default for a
  top-level type — is the project the build config's directory defines
  ([namespaces.md](namespaces.md) § internal).

## Statements and control flow

- ``"teko: `++`/`--` need a name"`` — the operand of a compound statement is a variable.
- `"teko: the left side of a for step must be a name"` — the step assigns to a name.
- `"teko: a case label must be a constant expression"` — a literal or a `const`; there are
  no type patterns.
- `"teko: duplicate case label: "` — completed by the value: two unguarded labels of the
  same value.
- `"teko: duplicate default label"` — one `default` per switch.
- `"teko: control cannot fall out of a case; end it with break"` — a non-empty case body
  ends with `break`, `break N`, `continue` or `return`.
- `"teko: continue inside a switch needs an enclosing loop"` — a `continue` in a case with
  no real loop around the switch.
- ``"teko: a switch expression needs a `_` arm"`` — the last arm of the chain is
  unconditional.
- `"teko: a switch expression needs at least one arm"` — an empty `{ }`.
- ``"teko: the last `_` arm of a switch expression cannot carry a `when`"`` — that arm is
  never tested, so a guard on it would be silently ignored.
- `"teko: the two arms of ?: have different types"` — both arms of a ternary have one type.
- `"teko: expected the foreach variable's name"` — `foreach (T x in xs)`.
- `"teko: expected in after the foreach variable"` — the same.
- `"teko: expected a name after in"` — the source of a `foreach` is a name, or a name and a
  field.
- `"teko: expected a field name after ."` — the same, after the dot.
- `"teko: not a known array or object"` — the receiver of a `foreach` source is not a local
  this compiler knows.
- `"teko: unknown member of foreach source"` — that object has no such field.
- `"teko: not an array"` — the field named is not an array.
- `"teko: the foreach variable's type does not fit the array element"` — the element type
  may widen into the variable, never narrow.
- ``"teko: a local const is not taught; declare it at the top or as a member"`` — a `const`
  is a program-wide folded constant.
- `"teko: const requires a constant expression"` — the value is folded at the declaration.
- `"teko: match not taught yet"` — use the switch expression.
- `"teko: var not taught yet"` — declarations carry their type.
- `"teko: when not taught yet"` — `when` guards a `case` or a switch-expression arm; there
  is no standalone form.

## Parameters, overloads, `ref`/`out` and `params`

- `"teko: a default argument must be a constant"` — folded at the declaration.
- `"teko: a parameter without a default cannot follow one with a default"` — defaults are a
  trailing run.
- `"teko: an extern parameter has no default"` — an `extern` owns the C signature.
- `` "teko: a `" `` — completed by *`ref`|`out`* parameter has no default*: a by-reference
  parameter is always passed.
- ``"teko: expected a type after `"`` — completed by *`ref`* or *`out`*: the word is
  followed by the pointee type.
- ``"teko: `ref`/`out` is only valid as a parameter type"`` — not on a local, a field or a
  return type.
- ``"teko: `ref`/`out` requires a variable"`` — the argument is a name, never a call or an
  expression.
- ``"teko: the `out` parameter "`` — completed by *`x` is never assigned*: an `out`
  parameter is written before the function returns.
- `"teko: not an object of a known type"` — `ref p.f` where `p` is not a local of a known
  type.
- `"teko: not a local array"` — `ref a[i]` where `a` is not a local array.
- ``"teko: two overloads differ only by `ref`/`out`"`` — a site could not tell them apart.
- ``"teko: `main` takes one signature"`` — the entry point is not overloaded.
- ``"teko: an `extern` name owns its symbol and cannot be overloaded"`` — an `extern` keeps
  the C symbol.
- `"teko: cannot take the address of an overloaded function"` — `&f` needs one symbol.
- `"teko: an overloaded call outside a function body has no arguments to resolve it"` — a
  call in a global initializer has no site to type.
- `"teko: no overload of "` — completed by *`X` matches these arguments*: including a call
  no `params` list of the name can take, `f(1, 1.5)` against `params i64[]` and
  `params f64[]`.
- `"teko: more than one overload of "` — completed by *`X` matches these arguments*: two
  candidates fit, and nothing tells them apart — two `params` lists of the same declared
  parameter count whose element types both take the arguments, `params u8[]` and
  `params u64[]` at `f(1)`.
- `"teko: ambiguous overload; two signatures take this many arguments"` — the arity alone
  does not choose.
- `"teko: the type of argument "` — completed by *N of `X` is not known here*: the
  argument's type could not be determined, so the overload could not be chosen.
- `"teko: too few arguments"` — the call passes fewer than the declaration requires.
- ``"teko: `params` declares a parameter list, nothing else"`` — `params` only in parameter
  position.
- ``"teko: `params` names an array type: write `params T[] xs`"`` — the modifier takes a
  genuine `T[]`.
- ``"teko: `params` must be the last parameter, and there is only one"`` — one list, at the
  end.
- ``"teko: `params` is taught on a free function only"`` — not on a method, a constructor,
  an interface signature or a `delegate`.
- ``"teko: a `params` list is not `ref` or `out`"`` — the two do not mix.
- ``"teko: a `params` list has no default"`` — the site decides the count.
- ``"teko: an `extern` symbol takes no `params` list"`` — the C ABI is not variadic here.

## Dependency injection

- `"teko: a class names two service lifetimes"` — one marker per class.
- `"teko: a service marker names a class"` — the markers apply to classes only.
- `"teko: an abstract class is not a service"` — a service is instantiated.
- `"teko: this type is not a service"` — the type named after `inject` carries no marker.
- ``"teko: unknown type after `inject`"`` — the name is no known type.
- `"teko: no service implements this type"` — nothing marked implements that key.
- `"teko: two services implement this interface: "` — completed by the two class names.
- `"teko: cyclic service: "` — completed by the chain: the graph closes on itself.
- `"teko: no constructor of this service takes only services"` — every parameter is either
  a service or has a default.
- `"teko: two constructors of this service take the same number of injectable parameters"` —
  the graph cannot choose.
- `"teko: the constructor of this service is not accessible"` — it is `private` or
  `protected`.
- `"teko: bind the injected service to a variable before calling it"` — `inject T` is an
  initializer, not a receiver.
- `"teko: inject inside a lambda takes the service from the enclosing scope; bind it outside and capture it with use (...)"`
  — a closure has no scope of its own.
- `"teko: scope expects a block"` — `scope { ... }`.

## Reference counting

- `"teko: a parameter of class type is borrowed; it is not reassigned"` — a parameter
  carries no count of its own. Assign to a local, or declare the parameter `ref`.

## Expressions and the static-type oracle

- ``"teko: the type of the left side of `.` is not known here"`` — the receiver's type could
  not be determined at that site. Bind it to a local of the right type.
- `"teko: unknown member of "` — completed by the type's name.
- `"teko: unknown member"` — the same, where the type has no name to print.
- `"teko: the member is a field, not a method"` — drop the `()`.
- `"teko: the member is a method; call it with ()"` — add them.
- `"teko: a virtual call needs a name or a field on the left"` — a virtual call needs a
  receiver the compiler can name.
- `"teko: a "` — completed by *`<what>` needs a class*: a construct that only a class
  carries (a constructor, a destructor, a vtable slot) was written on another kind of type.

## Capacity

Every table the compiler keeps has a ceiling. Hitting one is a diagnostic, not a silent
truncation; the fix is to split the unit.

| message | limit |
|---|---|
| `"teko: too many type declarations"` | 32 structs, classes and interfaces in one source |
| `"teko: too many fields"` | 256 fields, summed |
| `"teko: too many methods"` | 128 methods, summed |
| `"teko: too many virtual slots"` | 128 slots, summed |
| `"teko: too many constructors"` | 32, summed |
| `"teko: too many default arguments"` | 64, summed across all signatures |
| `"teko: too many properties"` | 64, summed |
| `"teko: too many operators"` | 64 |
| `"teko: too many interface methods"` | 128, summed |
| `"teko: too many implemented interfaces"` | 64 (class, interface) pairs |
| `"teko: too many interfaces in one class"` | 8 in one `:` list |
| `"teko: too many traits"` | 32 declared in one source |
| ``"teko: too many traits in one `use`"`` | 32 queued at once |
| `"teko: too many traits used by one class"` | 32 |
| `"teko: traits nested too deep"` | a trait may use a trait 16 deep |
| `"teko: too many generic declarations"` | 16 |
| `"teko: too many generic parameters"` | 4 per generic, 64 summed |
| `"teko: too many generic instances"` | 32 |
| `"teko: too many parts of a generic"` | 32, summed |
| `"teko: too many forward-declared types"` | 32 read ahead of their use |
| ``"teko: too many `new` on a type declared below"`` | 32 |
| `"teko: too many static accesses on a type declared below"` | 32 |
| `"teko: too many consts"` | 128 member constants |
| `"teko: too many compiler-written nulls in one unit"` | 64 ternaries over a reference type in one unit |
| `"teko: too many top-level consts"` | 128 |
| `"teko: too many local arrays"` | 1024 declarations in scope |
| `"teko: too many global arrays"` | 512 in one source |
| ``"teko: too many global `T[]` of heap"`` | 32 in one source |
| `"teko: too many array writes waiting to be resolved"` | 512 |
| `"teko: too many array-field accesses"` | 128 |
| ``"teko: too many `T[]` parameters in one declaration"`` | 32 |
| `"teko: too many locals in one unit"` | 8192 |
| `"teko: too many locals of struct type"` | 256 |
| `"teko: too many expressions whose type is known"` | 256 |
| `"teko: too many member accesses on a value of unknown type"` | 128 waiting for the pass |
| `"teko: too many stores into a slot of class type"` | 128 |
| `"teko: too many declarations in one unit"` | 8192 |
| `"teko: too many overloaded names in one unit"` | 64 |
| `"teko: too many free-function declarations with parameters"` | 4096 |
| `"teko: too many arguments"` | 64 at one call of an overloaded name |
| `"teko: too many parameters in one declaration"` | 16, which the ABI's own 12 is under |
| ``"teko: too many `params` lists in one unit"`` | 64 |
| ``"teko: too many `params` declarations in one unit"`` | 64 |
| ``"teko: too many `ref`/`out` parameters in one unit"`` | 512 |
| ``"teko: too many `ref`/`out` arguments in one unit"`` | 512 |
| `"teko: too many delegate targets"` | 64 (delegate, function) pairs |
| `"teko: too many captures in one lambda"` | 32, summed across the lambdas being read |
| `"teko: too many captures by value in one unit"` | 256, summed over every lambda: definite assignment reads each one's own node |
| `"teko: too many capturing lambdas"` | 64 capturing by reference |
| `"teko: too many tainted lambda locals"` | 64 |
| `"teko: too many unresolved names in one lambda"` | 64 |
| `"teko: too many namespaces"` | 16 in one source |
| `"teko: too many namespace segments"` | 16 |
| `"teko: too many namespaced type names"` | 64 |
| `"teko: too many namespaced free functions"` | 256 in one namespace block |
| `"teko: too many file-scoped namespaces"` | 16 |
| `"teko: too many files declaring a namespace"` | 32 |
| `"teko: too many using directives"` | 32, across every file |
| `"teko: too many case labels"` | 128 |
| `"teko: too many switch expression arms"` | 64 |
| `"teko: too many bare continues inside a switch"` | 128 |
| `"teko: loops nested too deep"` | 32 open at one point of one function |
| `"teko: too many primitive types"` | 8 primitives with a member table |
| `"teko: too many primitive members"` | 96 rows, over every primitive |
| `"teko: too many primitive operators"` | 32 rows, over every primitive |
| `"teko: too many casts over a primitive in one unit"` | 4096 casts the compiler wrote itself, in one compilation unit |
| `"teko: too many services"` | 32 marked classes |
| ``"teko: too many `inject` sites"`` | 32 |
| ``"teko: too many `scope` blocks"`` | 64, plus one per singleton |
| `"teko: scopes nested too deep"` | 32 open at once |
| `"teko: too many nested service dependencies"` | 32 under construction at once |

---

What a v0.4.0 program cannot express at all, with the message it gets, is
[not-yet.md](not-yet.md).
