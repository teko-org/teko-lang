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
not documented here fails the `docs` gate, and so does a `// expect-refuse:` message under
`tests/refuse/` whose fixed text this page does not carry (the check goes both ways). Some messages end mid-sentence in this page
because the compiler appends a name to them — the quoted part is the fixed text, and the
entry says what completes it.

A refusal has its own harness: [`tests/refuse/`](https://github.com/teko-org/teko-lang/tree/main/tests/refuse), run by
[`scripts/fixtures.sh`](../../scripts/fixtures.sh) beside `tests/*.tk`. Each fixture there
carries a two-line header naming the exact message and the exact line —
`// expect-refuse: teko: <message>` / `// expect-refuse-line: N` — and the build has to fail
with that line in its stderr (D52).

---

## Declarations and types

- `"teko: a type is declared at top level; there is no type inside a type"` — a `class`,
  `struct`, `interface`, `trait` or `enum` inside another type's body. Move it out; there
  is no nested type.
- `"teko: the name is already a type"` — the name is already a `class`, `struct`,
  `interface`, `trait` or `delegate` in this namespace — or a primitive's own type word:
  a teko primitive (`TimeSpan`, `DateTime`, `DateOnly`, `ref`, `out`, `params`, `i8`,
  `i16`), mc's own core word (`f64`, `f32`, `f64raw`, `i32`) or one of the seven
  `type_alias` words teko declares (`bool`, `char`, `byte`, `isize`, `usize`, `ptr`,
  `str`) (D60). A namespaced type keeps the word free for its own namespace
  (`geo.TimeSpan` beside the primitive `TimeSpan`) — only the exact qualified name
  collides.
- `"teko: the name is already a generic"` — the name belongs to a generic declaration.
- `"teko: name of "` — completed by *`<what>` expected*: a declaration keyword was read and
  what followed is not a usable name.
- `"teko: type not taught yet"` — the word in type position is reserved for a construct
  this version does not implement.
- `"teko: a value of type "` — completed by *`X` does not convert to `Y`*: the only implicit
  reference conversions are derived-to-base and class-to-interface, the only implicit
  numeric ones are an integer into a float and `f32` into `f64`
  ([types.md](types.md#f32-and-f64), D78), and nothing narrows back. A value of float type
  (`f64`, `f32`) never lands in a slot of another kind, an `f64` never lands in an `f32`
  slot (D78), `null` — whose type is `uptr` — never lands in a numeric one, and neither
  does a NON-null reference: a struct, a class, an interface, a delegate or a `T[]` of heap
  written where an `i64`/`f64` is declared (D34). All four hold in every slot: an
  argument (of a free function, a method, a virtual call, an interface call), an
  overload's parameter, an element of a `params` list, an assignment, an initializer, a
  field store, a GLOBAL slot's own initializer and assignment, a `return`
  ([parameters.md](parameters.md#what-a-literal-converts-to)), and the NULLABLE VALUE
  payload a `T?` boxes (Q1b, [nullable.md](../specs/nullable.md) § 4) — `f32? x = 2.5;`
  reads under the nullable's own name, `teko: a value of type f64 does not convert to
  f32?`, judged by the same width rule the plain slot is (D78,
  `tests/refuse/f32_from_f64_nullable.tk`), and a `params f32[]` element the same way
  (`tests/refuse/f32_params_from_f64.tk`). `i64 n = 2.5;`, `solo(null)`, `i64 n = f;` with
  `f` a class value, and `f32 c = 2.5;` are the four shortest forms of it, all written out
  in [types.md](types.md#f32-and-f64).
- **The same rules, at file scope** (D53): a global slot is judged exactly as a local is, so
  each one has a fixture of its own under `tests/refuse/`.

  | written at file scope, or into a global from a body | message |
  |---|---|
  | `i64 gn = 1.5;` | `teko: a value of type f64 does not convert to i64` (`global_narrow_init.tk`) |
  | `i64 gn; ... gn = 1.5;` | `teko: a value of type f64 does not convert to i64` (`global_narrow_assign.tk`) |
  | `f32 c = 2.5;` | `teko: a value of type f64 does not convert to f32` (D78, `f32_from_f64_global.tk`) |
  | `Cell gc; ... gc = null;` | `teko: null needs a slot declared Cell?` (`global_null_nonnullable.tk`) |
  | `i64 gn; ... gn = c;` on a `Cell c` | `teko: a value of type Cell does not convert to i64` (`global_ref_into_numeric.tk`) |
  | `Color gk = 1;` | `teko: a value of type i64 does not convert to Color` (`global_enum_from_int.tk`) |
  | `Cell gc; ... gc = b;` on a `Box b` | `teko: a value of type Box does not convert to Cell` (`global_row_mismatch.tk`) |
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
  target of `=` or of a call, through its type (`Type.C = e` / `Type.C(...)`) or, since D70's
  own write door, through a bare NAME on the LEFT of `=` inside a lambda — one string, asked
  from both doors (`tk_const_assign_refuse`, `teko_access.tk`).
- `"teko: unknown static member of "` — completed by the type's name: no static field,
  method, property or `const` of that name.
- `"teko: an array field on a type declared below is not taught yet"` — reaching an inline
  array field through a type whose declaration is further down.
- `"teko: an array field is assigned one element at a time"` — `p.items = e` on an inline
  array field. Assign `p.items[i]`.
- `"teko: an array field is read one element at a time"` — the same, reading.
- ``"teko: an array field is reached through `this.`"`` — the bare name does not reach an
  inline array field; write the receiver.
- `"teko: the address of a member is not taught yet"` — completed by the name: `&n`,
  `ref n` or `out n` on a bare member name (a field, a static field, a member `const`, a
  property, an interface property in a default body, a base interface's included), in a
  method or inside a lambda
  alike. No spelling of a member's address works from inside the type yet; from outside,
  `ref h.n` / `out h.n` through a variable holding the object does.
- `"teko: methods take no explicit receiver; use this"` — a parameter named as the
  receiver. The receiver is implicit; `this` names it.
- ``"teko: `this` is only valid inside the body of a type"`` — `this` in a free function.
- ``"teko: `this` is not there in a static member"`` — a static member has no receiver.
- ``"teko: `this` is read-only"`` — `this = e;`, and the compound spellings of the same
  write, `this += e`, `this -= e`, `this++`, `this--`. The receiver is a borrowed
  parameter, not a slot: it names the object the call arrived on, and nothing rebinds it.
  Assign to a field instead. All five forms compiled silently before D102, with no
  reference counting at all, because the receiver parameter is declared `uptr`.
- ``"teko: `this` is not a value in a struct"`` — `return this;`, `f(this)`, `S x = this;`
  or any other bare `this` read as a value inside a `struct` body. A struct carries no
  object header and no reference count, so its `this` cannot be the value a class's or an
  interface's is, and the language does not copy a struct at assignment yet (`P b = a;
  b.x = 9;` changes `a.x`), so handing the receiver out would alias where C# copies. The
  field roads are untouched: `this.x`, a bare `x` and `this.x = v` inside the same method
  all keep working. Before D103 this answered the generic
  `teko: a value of type uptr does not convert to P`, which named the receiver parameter's
  declared type rather than saying why.
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
[teko_prim.tk's lowering table](#primitives-with-members-timespan-datetime-dateonly-timeonly): registering
an enum there would make `tk_ty_binary` ask a table its own bitwise/comparison operators
never populate. The two globals a text-using enum needs (`Color__names`, `Color__vals`)
are built lazily, the first time one of these four names is spelled on that enum — an enum
a program never asks text of pays nothing.

- `"teko: wrong number of arguments for "` — completed by the name: `Parse` and
  `IsDefined` take exactly one argument, `ToString` takes none.
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

## Primitives with members (`TimeSpan`, `DateTime`, `DateOnly`, `TimeOnly`)

A primitive is a type of four or eight bytes with no row in the type table — no field, no vtable,
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
  operand of an operator over a primitive is an expression the oracle cannot type. A
  primitive operand is exactly where leaving the node to the core's raw arithmetic would
  answer a wrong number, so the site is refused rather than guessed at; bind the value to a
  local first. An **array element** was this message's one shape until D50 and is one no
  longer: every element load carries the element's own type now, so `xs[i] + t` and
  `xs[i] + xs[j]` are ordinary operand pairs.
- `"teko: the type of this argument is not known here"` — the same rule one position over:
  an argument landing on a parameter of primitive or `enum` type, in a row of the member
  table, whose type nothing can tell even after the oracle has run. That position converts
  with a cast, so an unread value would cross under the column's name instead of being
  refused. Bind it to a local first. An array element reaches this one no longer either
  (D50): `new DateTime(1, a[0])` on an `i64 a[2]` is judged by the type the element has,
  `teko: a value of type i64 does not convert to DateTimeKind`.
- `"teko: this primitive has no constructor"` — `new` on a primitive whose table declares
  no constructor row. `TimeSpan` declares one, so nothing in v0.4.0 reaches this; it is the
  mechanism's own guard for the primitives the specs still have coming.
- `"teko: a type name reaches its static members"` — the type word alone, with no `.`
  after it, in expression position.
- `"teko: the field is not an array"` — completed by the member's name: `t.Ticks[0]`.
- `"teko: an "` — the same two refusals with the other article. English takes *an* before a
  vowel SOUND and not before a vowel letter, so `i128` reads *an i128* and `u128` reads
  *a u128* — "you-128", the rule that gives *a union* and *a user*. Every primitive
  registered before N6a begins with a consonant, so `decimal`, `Guid`, `DateTime`,
  `TimeSpan` and `DateTimeOffset` read exactly as they always did
  (`tk_prim_article`, [`teko_prim.tk`](../../teko_prim.tk), D81).
- `"teko: a "` — completed by *`X` does not cast; `.Ticks` reads it and `new X(...)` builds
  it*: `(i64) d` and `(DateTime) n` written by hand, and the same pair over a `TimeSpan`.
  A cast is the one syntax that would convert what converts to nothing but itself, and over
  a `DateTime` it would answer a wrong number rather than refuse — the `Kind` sits in the
  two bits above the ticks. The compiler writes both casts itself, in the lowering, and
  knows its own. **The two clauses after the semicolon are columns of the primitive's own
  registration**, since D54 the reader and since D75 the builder, and neither is one word
  for every primitive: a `DateOnly` has no `.Ticks` at all, so it reads
  ``teko: a DateOnly does not cast; `.DayNumber` reads it and `new DateOnly(...)` builds it``.
  A `TimeOnly` DOES have `.Ticks` — the same identity `TimeSpan` and `DateTime` read by — so
  its own reads
  ``teko: a TimeOnly does not cast; `.Ticks` reads it and `new TimeOnly(...)` builds it``.
  Each clause carries its VERB as well as its spelling, because the pair is not the same
  sentence for every type: a `Guid` is not constructed at all, so its own reads
  ``teko: a Guid does not cast; `.ToString()` writes it and `Guid.Parse(s)` reads it``.
- `"teko: the wall clock is not available"` — `DateTime.Now`/`UtcNow`/`Today` and
  `DateTimeOffset.Now`/`UtcNow` (C6, D90), exit 70. `clock_gettime` is the only one of the
  wall clock's two host answers that documents a failure at all (a bad clock id or an
  unmapped buffer, neither reachable through this call); `GetSystemTimePreciseAsFileTime`,
  Windows' own, cannot fail by its own contract, so it has no panic to raise.
### `DateTimeKind`, since it became an `enum`

`DateTimeKind` is an ordinary `enum` declared in `lib/time.tk` (N2c), not a compiler
registration, so it refuses what [every enum refuses](#enums) and under the same wordings.
Four of them are what the crumb bought, and each used to compile:

```teko
// no-run
#include "../lib/time.tk"

i64 main() {
    DateTime d = new DateTime(1, DateTimeKind.Utc);
    i64 n = d.Kind;              // teko: a value of type DateTimeKind does not convert to i64
    DateTimeKind k = 7;          // teko: a value of type i64 does not convert to DateTimeKind
    DateTime e = new DateTime(1, 7);   // teko: a value of type i64 does not convert to DateTimeKind
    i64 s = d.Kind + DateTimeKind.Utc; // teko: no operator `+` takes these operands
    return 0;
}
```

**The same refusal when the value's type is only the PASS's to tell.** The constructor's
kind argument converts with a cast, so an argument the parser cannot type used to cross as
the underlying `i64` and reach the run-time kind guard instead; the check is deferred to
the pass now, and every one of these is a compile-time refusal (D48, the review finding on
[#697](https://github.com/teko-org/teko-lang/pull/697)):

```teko
// no-run
#include "../lib/time.tk"

struct S { public i64 k; }

i64 seven() { return 7; }
DateTime from_param(i64 k) { return new DateTime(1, k); }
                             // teko: a value of type i64 does not convert to DateTimeKind

i64 main() {
    S s = new S();
    s.k = 7;
    DateTime a = new DateTime(1, s.k);      // teko: a value of type i64 does not convert to DateTimeKind
    DateTime b = new DateTime(1, seven());  // teko: a value of type i64 does not convert to DateTimeKind
    i64 arr[2];
    arr[0] = 7;
    DateTime c = new DateTime(1, arr[0]);   // teko: a value of type i64 does not convert to DateTimeKind
    return 0;
}
```

**And an argument that is a CALL is judged by the signature the ARGUMENTS pick.** A name
may carry more than one, and the symbol a site calls is written two passes after the walk
that would otherwise judge the argument, so the check waits for it — in both declaration
orders, and with no wording of its own:

```teko
// no-run
#include "../lib/time.tk"

DateTimeKind pick(i64 a) { return DateTimeKind.Utc; }
i64 pick(i64 a, i64 b) { return 7; }

i64 main() {
    DateTime d = new DateTime(1, pick(1, 2));
    // teko: a value of type i64 does not convert to DateTimeKind
    return 0;
}
```

`new DateTime(1, pick(1))` in that same program is accepted, and so is the mirror pair —
an `i64` overload landing on the ticks position of a name whose other overload answers the
enum. What decides is the overload, never the first declaration of the name.

**A FLOAT position of a primitive row waits for that pick too**, because there the picked
signature decides the conversion and not only the check: an integer return is widened to
the float the lowering symbol declares (`TimeSpan.FromHours(2)` is `FromHours(2.0)`) and
anything else is not, so a guess would write the wrong node and then hide the argument
behind it. Deciding it late is what makes this a refusal instead of a value read as a
mantissa (D48, the fourth review pass on
[#697](https://github.com/teko-org/teko-lang/pull/697)):

```teko
// no-run
#include "../lib/time.tk"

i64 pick(i64 a) { return a; }
DateTimeKind pick(i64 a, i64 b) { return DateTimeKind.Utc; }

i64 main() {
    TimeSpan t = TimeSpan.FromHours(pick(1, 2));
    // teko: a value of type DateTimeKind does not convert to f64
    return 0;
}
```

`TimeSpan.FromHours(pick(1))` in that same program is accepted and widened, in either
declaration order, and an overload that answers a float of its own crosses with no cast at
all.

**An OPERATOR over one of those shapes is judged the same way**, because a bitwise
operator over an enum answers the enum ([enum.md](../specs/enum.md) § 4): `k |
DateTimeKind.Utc` on a `DateTimeKind` parameter is as legal in the kind position as `k` is,
and the `i64` twin is refused with the type it really has rather than with "not known
here":

```teko
// no-run
#include "../lib/time.tk"

DateTime f(i64 x) { return new DateTime(1, x | 1); }
                    // teko: a value of type i64 does not convert to DateTimeKind
```

**...and the operator is judged on the PICK, wherever it is written.** The type of a call
to an overloaded name is the return type of the signature its own arguments choose, and the
oracle every pass asks answers that and not the first declaration of the name (D49, the
seventh review pass on [#697](https://github.com/teko-org/teko-lang/pull/697)). An enum
takes no integer operand ([enum.md](../specs/enum.md) § 4), so this is refused in a
primitive row's argument and outside one alike:

```teko
// no-run
#include "../lib/time.tk"

i64 pick(i64 a, i64 b) { return 7; }
DateTimeKind pick(i64 a) { return DateTimeKind.Utc; }

i64 main() {
    DateTime d = new DateTime(1, pick(1) + 1);
    // teko: no operator `+` takes these operands
    DateTimeKind k = pick(1) + 1;
    // teko: no operator `+` takes these operands
    return 0;
}
```

The same refusal covers `pick(1) - 1`, `pick(1) * 2`, `1 + pick(1)`, the unary `-pick(1)`
and the unary `+pick(1)` — `+` is the identity over a number and the identity over nothing
else, so an enum operand refuses it where an `i64` one erases it. `pick(1) |
DateTimeKind.Utc`, which a bitwise operator over an enum makes LEGAL, is accepted in that
same program: it is the enum the pick answers on both sides, in either declaration order.
A PRIMITIVE the first declaration hid is lowered to its own row rather than run raw:
`TimeSpan.FromTicks(2).CompareTo(dpick(3) - epick(1))` on two `DateTime`-returning overloads
subtracts through `lib/time.tk` — `Kind` bits masked, overflow checked — where the core's
raw `-` carried the two top bits into the `TimeSpan`.

A GLOBAL is accepted wherever its declaration says the enum: `DateTimeKind g =
DateTimeKind.Utc;` at the top of a file and `new DateTime(t, g)` inside a function is the
enum in the enum's own position. A global is in scope in every body, so its declared type
is what the check reads, exactly as a local's is.

The mirror holds as well: a `DateTimeKind` in the `i64` position of the same constructor —
`new DateTime(k, DateTimeKind.Utc)` with `k` of enum type — is
`teko: a value of type DateTimeKind does not convert to i64`. That position needs no cast,
so it was never the leak; it is checked against the declaration itself.

`(DateTimeKind) 7` is still accepted — an explicit cast into an enum is C#'s own, and the
value is caught at run time by the range check of `tk_dt_from_ticks_kind` (`teko: a date
kind is out of range`, exit 70) if it reaches a constructor.

**A program that forgot `#include "time.tk"` is no longer told which file it forgot.** The
name belongs to the library file now, so `DateTimeKind.Utc` without the include reaches
`teko: unknown member: Utc` and `DateTimeKind k;` reaches the core's own `expected ; after
expression`, where the old registration answered `teko: DateTimeKind needs #include
"time.tk" before it is used`. `DateTime` and `TimeSpan` keep that refusal, because they are
compiler registrations; the trade is [D48](../../DECISION_LOG.md)'s.

The message `"teko: unknown static member of DateTimeKind"` is gone with the handler that
raised it: a member the enum does not declare now reads as
`teko: DateTimeKind has no member Nope`, the wording every enum shares.

### `DateOnly` (N4a)

`DateOnly` is the same mechanism one width down — four bytes, the day number since
`0001-01-01` — so every wording above is its own. Five of them are what the type is, and
each has a fixture under `tests/refuse/` ([datetime.md § `DateOnly`](datetime.md#dateonly)):

```teko
// no-run
#include "../lib/time.tk"

i64 main() {
    DateOnly d = new DateOnly(2024, 2, 29);
    DateTime t = new DateTime(2024, 2, 29);

    DateOnly e = 5;         // teko: a value of type i64 does not convert to DateOnly
    i64 n = d;              // teko: a value of type DateOnly does not convert to i64
    DateTime x = d;         // teko: a value of type DateOnly does not convert to DateTime
    i64 c = (i64) d;        // teko: a DateOnly does not cast; `.DayNumber` reads it and `new DateOnly(...)` builds it
    DateTime y = d + t;     // teko: no operator `+` takes these operands
    DateOnly z = new DateOnly(2024, 2, 30);   // teko: a date does not exist, exit 70
    return 0;
}
```

The `+` is refused because **the table registers no arithmetic row for a `DateOnly` at
all** — C# declares none either — so `d + t` and `d + 1` reach that wording, `d - d`
reaches ``teko: no operator `-` takes these operands``, and `d1.DayNumber -
d2.DayNumber` is the form. `.ToString()` and `DateOnly.Parse` reach
`teko: unknown member of DateOnly` and its static twin ([not-yet.md](not-yet.md));
`.ToDateTime(t)` is taught now — [`TimeOnly` (N4b)](#timeonly-n4b) below.

The three panics `lib/time.tk` raises for a `TimeSpan` at RUN time (`a time span
overflowed`, `a time span divided by zero`, `a time span is out of range`) and the eleven it
raises for a `DateTime` (`a date does not exist`, `a date is out of range`, `a year is out
of range`, `a month is out of range`, `an hour is out of range`, `a minute is out of
range`, `a second is out of range`, `a millisecond is out of range`, `a date kind is out of
range`, `a month count is out of range`, `a year count is out of range`) are exit 70 and
are listed in [runtime.md](runtime.md#the-time-library). A `DateOnly` adds no panic of its
own: `new DateOnly(2024, 2, 30)` raises the `a date does not exist` of that same list, and
a day number outside `0 .. 3652058` raises `a date is out of range`.

### `TimeOnly` (N4b)

`TimeOnly` is the same mechanism at the ORIGINAL width — eight bytes, the ticks since
midnight — so every wording above is its own too, and each of the five has a fixture under
`tests/refuse/` ([datetime.md § `TimeOnly`](datetime.md#timeonly)):

```teko
// no-run
#include "../lib/time.tk"

i64 main() {
    TimeOnly t = new TimeOnly(13, 45, 30);
    TimeOnly u = new TimeOnly(1, 0, 0);

    TimeOnly e = 5;          // teko: a value of type i64 does not convert to TimeOnly
    i64 n = t;               // teko: a value of type TimeOnly does not convert to i64
    DateTime x = t;          // teko: a value of type TimeOnly does not convert to DateTime
    i64 c = (i64) t;         // teko: a TimeOnly does not cast; `.Ticks` reads it and `new TimeOnly(...)` builds it
    TimeOnly y = t + u;      // teko: no operator `+` takes these operands
    TimeOnly z = new TimeOnly(864000000000);   // teko: a time of day is out of range, exit 70
    return 0;
}
```

The `+` is refused the same way `DateOnly`'s is: **the table registers only `-` for a
`TimeOnly`**, C# declares no `operator +` between two times of day either, and `.Add(ts)`
is the form for advancing one by a `TimeSpan`. `.ToString()`, `TimeOnly.Parse` and
`TryParse` reach `teko: unknown member of TimeOnly` and its static twin
([not-yet.md](not-yet.md)).

`TimeOnly` adds **one** panic of its own, `teko: a time of day is out of range`: a raw tick
count outside `0 .. 863999999999`, whether it arrives through `new TimeOnly(ticks)` or
`TimeOnly.FromTimeSpan(ts)`. No existing wording in the list above reads honestly for an
interval that starts at zero — `an hour/minute/second/millisecond is out of range` are what
the three calendar-free constructors reach instead, the same checks a `DateTime`'s own time
of day takes. It is listed in [runtime.md](runtime.md#the-time-library) beside them.

## `decimal`, the sixteen-byte value

`decimal` is registered with `type_new("decimal", 16, 16, TK_WIDE)` and MOVES by address:
three derived machines copy its sixteen bytes between frame slots, globals, arguments and
the return buffer ([`teko_wide.tk`](../../teko_wide.tk), D74). C4 adds the ARITHMETIC — the
five operators, the two unary ones, the six comparisons and the four conversions, all of
them a call into `lib/decimal.tk` (D77) — and C5 the MEMBER table: nineteen rows, the
rounding, the text and the statics (D79,
[the specification](../specs/decimal.md) § 7, [not-yet.md](not-yet.md)).

- ``"teko: a decimal does not cast; `.ToString()` writes it and `decimal.Parse(s)` reads it"``
  — a cast whose target the conversion table does not name, `(str) d` being the one
  the surface reaches. The four casts § 6 opens — ``(i64) d``, ``(f64) d``, ``(decimal) n``,
  ``(decimal) x`` — are each a CALL and never reach this row; `MTASK_CAST` on a sixteen-byte
  value still has no meaning. The two clauses are the type's own `pt_read`/`pt_build`
  columns, filled by C5 (`tk_prim_cast_check`, [`teko_prim.tk`](../../teko_prim.tk)); C3 and
  C4 registered neither and got the shorter *a decimal does not cast yet* instead, because a
  message naming a member the type does not have yet would be wrong.
- `"teko: a global "` — completed by *decimal takes no initializer*. A `decimal` has no
  folded form, so a global of that type is a slot and an assignment: `decimal g = 3.25m;`
  dies one phase earlier with mc's own `global initializer must be constant` (the literal is
  the `N_IDENT` of its own blob global), and `decimal g = 5;` is the one spelling that gets
  past that rule — `5` IS the `N_INT` `parse_global` demands — only to need a call to
  `tk_dec_from_i64`, which a global initializer may not be either. Refused by name
  (`tk_rc_glb_widen`, [`teko_rc.tk`](../../teko_rc.tk), D77) rather than rewritten as the
  `f64` bits the float slot beside it takes.
- `"teko: new "` — completed by *`decimal() is not taught; write 0m`*, and the same
  template's own two other spellings, *`Guid() is not taught; write Guid.Empty`* (below) and
  *`DateTimeOffset() is not taught; write DateTimeOffset.MinValue`* ([below](#datetimeoffset),
  D76). `new T()` with no arguments is C#'s own parameterless value constructor and every
  OTHER primitive answers it with the identity cast to zero (`TimeSpan.Zero`,
  `DateTime.MinValue`) — but that cast is `tk_cast(ty, tk_int(0))`, and for a WIDE type it
  would reach `tw_cast`'s own `die` (`teko_wide.tk`): there is no zero BIT PATTERN a
  sixteen-byte cast could write, since a wide value moves by address and never through a
  register. `tk_prim_new` refuses it by name instead, reading the type's own named zero from
  a fourth column of `tk_prim_type` (`pt_zero`, D76) — one guard, in the shared code, for
  every wide type at once.
- `"teko: decimal literal out of range"` — the mantissa is 96 bits and the literal needs
  more. The digits are accumulated into four 32-bit limbs and a carry out of the top one is
  remembered rather than dropped, so `1e40m` is refused instead of wrapping.
- `"teko: a decimal carries at most 28 decimal places"` — the scale is eight bits of the
  high word and runs `0..28`, C#'s own range. The exponent shifts the scale, so `1e-29m`
  reaches it too.
- ``"teko: an `extern` takes no "`` — completed by the type's name: the sixteen-byte
  convention is teko's own (a pointer in an ordinary integer argument register, a buffer for
  the return) and no C ABI shares it, so the two would disagree at run time rather than at
  the declaration. Refused where the type and the word `extern` are both in hand
  (`tk_ov_extern_wide`, [`teko_over.tk`](../../teko_over.tk)).
- `"teko: "` — completed by *decimal needs #include "decimal.tk" before it is used*: every
  one of § 6's conversions is a CALL into `lib/decimal.tk`, so a program that converts
  without the include is told which file it forgot instead of reaching the core's own `call
  to unknown function`. It is the same message `TimeSpan` and an `enum`'s own text already
  give, and it now covers **both** roads: the explicit `(decimal) n`
  (`tk_prim_cast_lower`, [`teko_prim.tk`](../../teko_prim.tk)) and the implicit
  `decimal d = 1;` in all nine of D33's slots plus the mixed `d + 1`
  (`tk_num_widen`, [`teko_typeof.tk`](../../teko_typeof.tk); D77, ruling 10).
- `"teko: include "` — completed by *"decimal.tk" before returning a sixteen-byte value*: a
  wide return travels through `tk_dec_retbuf`, a global the **program** declares, and
  `lib/decimal.tk` is where it is declared. It is the rule `lib/rt.tk` already has for an
  `enum`'s own lowering symbols. The buffer and the file named are **per type** since D75,
  so a function that returns a `Guid` reads
  *teko: include "guid.tk" before returning a sixteen-byte value* and is never pointed at a
  file it does not use. D76: the include COLUMN itself is bare everywhere now
  (`tk_prim_type`'s `inc` argument) — `teko_time.tk` used to bake its own quotes in
  (`tk_time_include()`), which would have doubled them for a WIDE type registered under that
  column (`DateTimeOffset`, below); every caller now asks `tk_prim_inc_q` (`teko_prim.tk`)
  for the quoted form, and this message's own wording is unchanged because the quotes moved
  from the column to the one reader.

Four are **run-time panics** of `lib/decimal.tk`, all exit 70 and all on stderr with no
`file:line` ([runtime.md](runtime.md#the-decimal-library)):

- `"teko: decimal overflow"` — a result that needs more than the 96 bits of a mantissa at a
  scale that cannot be reduced any further: `79228162514264337593543950335m + 1m`, an
  `(i64) d` outside `i64`'s own range, a `(decimal) x` on an `f64` above the type's ceiling,
  and on NaN or an infinity, which are values base ten does not hold. C# raises
  `OverflowException` at every one of them; teko has no exceptions, so it panics
  (`tk_dec_pack`, `tk_dec_to_i64`, `tk_dec_from_f64`).
- `"teko: decimal division by zero"` — `d / 0m` and `d % 0m`. `0m`, `0.00m` and `-0m` are
  three bit patterns and one zero, and all three reach it (`tk_dec_is_zero`).
- `"teko: the string is not a decimal"` — `decimal.Parse(s)` on text the grammar does not
  read: a letter, an empty string, a space, a thousands separator, two points, an `e` with
  no digits after it. C#'s own `Parse` raises `FormatException` there and never answers a
  sentinel value no caller could tell from a parsed one; `decimal.TryParse(s, out d)` is the
  other half and answers 0 without panicking (`tk_dec_scan`, D79). A string the grammar DOES
  read and the type cannot hold — a mantissa past 96 bits, a scale past 28 places once the
  exponent moved it — is `decimal overflow` above instead: two failures, two causes.
- `"teko: the decimal places are out of range"` — `decimal.Round(d, n)` and
  `d.ToString(n)` with `n` outside `0..28`, which is the whole range a scale has. It is a
  run-time guard because the argument is an ordinary `i64` and nothing at compile time knows
  what a parameter holds; `ToString` shares the message because it rounds first, and one
  cause reads better as one message (D79, ruling 4).

Five more are **guards on the machine**, in the same file. Every one of them is unreachable
from the surface — teko refuses each construct earlier, with a line and a name — and they
are there so that a hole in that reasoning is a message rather than eight bytes moved where
sixteen were meant ([the specification](../specs/decimal.md) § 2, last row):

- `"teko: arithmetic is not defined on a sixteen-byte value yet"` — `MTASK_BIN`. Since C4
  every arithmetic operator a `decimal` takes is LOWERED to a call before codegen, and an
  operator with no row is ``teko: no operator `&` takes these operands`` at the surface, so
  the guard covers the hole between the two.
- `"teko: a comparison is not defined on a sixteen-byte value yet"` — `MTASK_CMP`; the six
  comparisons are calls too (`tk_dec_eq` … `tk_dec_ge`).
- `"teko: a unary operator is not defined on a sixteen-byte value yet"` — `MTASK_UN`. `-d`
  is `tk_dec_neg` and `+d` is the operand itself, written by the unary lowering with no row
  at all (`tk_prim_unary_plus`, D77).
- `"teko: a cast is not defined on a sixteen-byte value yet"` — `MTASK_CAST`; the four casts
  § 6 opens are calls and the rest is `` teko: a decimal does not cast; `.ToString()` writes it and `decimal.Parse(s)` reads it `` (the shorter *does not cast yet* was C3/C4's, before the read and build clauses existed).
- `"teko: a constant is not defined on a sixteen-byte value yet"` — `MTASK_CONST`. A
  `decimal` has no folded form at all, so the surface refusals are
  `teko: const requires a constant expression` and
  `teko: a case label must be a constant expression`.
- `"teko: a sixteen-byte value in an allocatable register"` — `MTASK_PARAM_REG` under
  `--opt=1`. The walker allocates no sixteen-byte local today; the guard is what says so if
  it ever does.

## `Guid`

`Guid` is the second `TK_WIDE` type and the first that is not `decimal`
([the specification](../specs/guid.md), D75), so every refusal of the section above that is
about the sixteen bytes rather than about `decimal` reaches it too — the `extern` one by
name, the five machine guards as guards. `Guid.NewGuid()` (N9, D91) reads the host's own
entropy through the same wrapped bundle resolver C6 (D90) gave the wall clock and is no
longer refused by name. What is its own is short.

- ``"teko: a Guid does not cast; `.ToString()` writes it and `Guid.Parse(s)` reads it"`` —
  `(i64) g` and `(Guid) n` written by hand. Sixteen bytes are no number, and unlike
  `decimal` this type teaches both directions already, so the refusal names them.
- ``"teko: TryParse's second argument is `out <name>`"`` — `Guid.TryParse(s, g)` without the
  `out`, or with anything but a variable after it. `Guid.TryParse` is parsed by hand
  ([`teko_guid.tk`](../../teko_guid.tk)), exactly as `Color.TryParse` is, because `out` is
  no column the primitive-member table has.
- `"teko: a value of type "` — completed by *X does not convert to Guid*: the second
  argument of `TryParse` is an `out` of a `Guid` slot and of nothing else, checked against
  the pointee `tk_ref_addr` hands back.
- ``"teko: new Guid() is not taught; write Guid.Empty"`` — D76's own shared guard (`"teko: new
  "`, [above](#decimal-the-sixteen-byte-value)): `Guid` registers no `new` row at all, so
  before D76 this reached `teko: this primitive has no constructor` instead — untested by any
  fixture until now, since no program had written it — and the shared wording is a strictly
  better answer, naming the static that reads as the type's own zero.

Three more are **run-time panics** of `lib/guid.tk`, all exit 70 and all on stderr with no
`file:line` — the same abort every other guard in this port takes
([runtime.md](runtime.md)):

- `"teko: the string is not a Guid"` — `Guid.Parse(s)` on anything that is neither the
  36-character hyphenated form nor the 32-character bare one. C#'s own `Parse` throws here;
  `Guid.TryParse(s, out g)` is the pair that answers `0` and writes `Guid.Empty` instead.
- `"teko: the Guid format is not taught"` — `g.ToString(fmt)` on a format outside `"D"` and
  `"N"`. `"B"`, `"P"` and `"X"` are three more spellings of the same sixteen bytes and are
  not taught ([the specification](../specs/guid.md) § 6).
- `"teko: the entropy source is not available"` — `Guid.NewGuid()` (N9, D91) on a host whose
  entropy call fails or makes no progress: `getentropy`'s documented failure, `getrandom` returning
  negative, or `BCryptGenRandom` answering a nonzero `NTSTATUS`. Not reachable on a healthy
  host; a panic rather than a silently weak fallback, exactly like the wall clock's own
  `"the wall clock is not available"` (C6, D90) — there is no fallback a version-4 `Guid` can
  take.

## `DateTimeOffset`

`DateTimeOffset` is the third `TK_WIDE` type, after `decimal` and `Guid`
([the specification](../specs/datetime-extras.md), N5, D76), registered LAST in
`tk_time_init()` so its columns can name the live ids `DateTime` and `TimeSpan` carry. Every
refusal the two sections above document that is about the sixteen bytes rather than about
`decimal` or `Guid` reaches it too — the `extern` one by name, the five machine guards as
guards, the shared `"teko: new "` wording above. What is its own is short: one compile-time
refusal beyond the shared ones, and three run-time panics.

- ``"teko: a DateTimeOffset does not cast; `.UtcDateTime` reads it and `new DateTimeOffset(...)` builds it"`` —
  `(i64) o` and `(DateTimeOffset) n` written by hand. Sixteen bytes are no number, and unlike
  `decimal` this type teaches both a reader and a builder already, so the refusal names them
  (`tk_prim_cast_check`, D75's own mechanism).
- ``"teko: a value of type i64 does not convert to DateTime"`` — the `new DateTimeOffset(i64
  ticks, TimeSpan)` overload C# has is **not taught** (`docs/reference/not-yet.md`); the one
  row this table carries is `new DateTimeOffset(DateTime, TimeSpan)`, and `tk_prim_pick`
  chooses a `"new"` row by ARGUMENT COUNT alone, so an `i64` written where the row's own first
  position asks for a `DateTime` reaches this ordinary mismatch instead of a row of its own.
  `new DateTimeOffset(new DateTime(t), ts)` is the written form.

Three are **run-time panics** of `lib/time.tk`, all exit 70 and all on stderr with no
`file:line` ([runtime.md](runtime.md#the-time-library)):

- `"teko: that UTC offset does not exist"` — the constructor's second argument, or
  `.ToOffset`'s, outside `-14:00 .. +14:00` or not a whole minute (`tk_dto_check_offset`).
- `"teko: the local time of that DateTimeOffset is out of range"` — the instant plus the offset
  would read outside `DateTime`'s range (`MaxValue.ToOffset(+01:00)`); refused where the value is
  built (`tk_dto_make`, lib/time.tk), exit 70.
- `"teko: the DateTimeOffset format is not taught"` — `o.ToString(fmt)` on a format outside
  the lowercase `"o"` and `"s"` § 5 of the specification names; C#'s own format characters are
  case-sensitive for these two, and none of C#'s other calendar formats is taught.
- `"teko: the string is not a DateTimeOffset"` — `DateTimeOffset.Parse(s)` on anything that is
  not exactly one of the two strings `ToString()` writes (33 characters with a fraction and an
  offset, or 19 without either); `DateTimeOffset.TryParse(s, out o)` is the pair that answers
  `0` and writes `DateTimeOffset.MinValue` instead. A 19-character string is read as UTC,
  offset zero — teko has no time-zone database to read a bare wall-clock string against
  ([the specification](../specs/datetime-extras.md) § 8), and zero is the one offset every
  reader agrees on.

## `i128` and `u128`

`i128` and `u128` are the fourth and fifth `TK_WIDE` types
([the specification](../specs/small-ints.md) § 6, N6a, D81), so every refusal the `decimal`
section documents that is about the sixteen bytes rather than about `decimal` reaches them
too — the `extern` one and the global-initializer one by name, the five machine guards as
guards, the shared `"teko: new "` wording. `%`, `<<`, `>>`, `&`, `|`, `^` and `~` are landed
too now (N6b-1, D83): each type carries eighteen rows, eleven of N6a's plus seven of
N6b-1's (fourteen over the two types), and no MIXED row of any kind — `i128 + u128` still earns
``teko: no operator `+` takes these operands`` because C# refuses the same expression
without a cast. The `f64` and `decimal` conversions, both directions and both types, landed
too (N6b-2, D84): eight new rows of the cast-to-call table, none of them opening an implicit
door. N6b-3 (D85) closes the rest: `ToString`, `Parse`, `TryParse`, `CompareTo`, `Equals`,
`MinValue`, `MaxValue`, `Zero`, `One`, both types — after this crumb N6b owes nothing more
([not-yet.md](not-yet.md)).

- `"teko: an i128 literal is out of range"` — an `i128` literal is the MAGNITUDE only, so
  the ceiling is 2^127−1 and `170141183460469231731687303715884105728i` is one too many. A
  leading `-` is the unary operator applied to the value the literal answers and is no part
  of it — `i128.MinValue` is still written `-170141183460469231731687303715884105727i - 1i`
  at the LITERAL level (this refusal is about the literal grammar, which never gained a
  constant of its own); `i128.MinValue` the STATIC (N6b-3, below) is the same value read a
  second, shorter way, a call and not a literal. The digits go into four 32-bit limbs and a
  carry out of the top one is remembered rather than dropped, so the check is at compile
  time and never a silent wrap (`tk_w128_lit`, [`teko_i128.tk`](../../teko_i128.tk)).
- `"teko: a u128 literal is out of range"` — the same check with all 128 bits available:
  the ceiling is 2^128−1.
- ``"teko: an i128 does not cast; `.ToString()` writes it and `i128.Parse(s)` reads it"`` /
  the `u128` twin — a cast whose target the conversion table does not name: `(str) x` alone,
  N6b-3's own wording now (D85), `decimal`'s own shape (D79) — text is a MEMBER call on
  each side, never a cast, so `(str) x` still refuses even though `i128`/`u128` now register
  a reader and a builder clause (`tk_i128_init`, [`teko_i128.tk`](../../teko_i128.tk)); only
  the WORDING moved from N6a's shorter `"...does not cast yet"`. The sixteen rows N6a and
  N6b-2 register between them — `(i128) n`/`(u128) n` from any integer, `(i64) x` and every
  narrower target, the two bit-preserving directions between `i128` and `u128`, and D84's own
  eight `f64`/`decimal` rows — are each a CALL and never reach this refusal.
- `"teko: a value of type i128 does not convert to f64"` / `"...to decimal"` — D84's eight new
  rows are EXPLICIT only: `f64 x = v;` and `decimal d = v;` on an `i128`/`u128` `v` build no
  cast at all (`tk_num_wide_widens`, teko_typeof.tk, still answers 0 for a wide source, D38),
  so the ordinary "does not convert" wording every implicit-widening refusal already carries
  fires here too, not a wide-specific message.
- `"teko: decimal overflow"` — already `decimal`'s own wording (D77), two more paths into it
  now: `(decimal) x` on an `i128`/`u128` magnitude at or past `2^96` (`decimal.MaxValue` is `2^96 - 1`),
  and `(u128) d` on a negative `decimal` whose truncated magnitude is still nonzero (D84).
- `"teko: the string is not an i128"` / `"teko: the string is not a u128"` — `i128.Parse(s)` /
  `u128.Parse(s)` on text that is not exactly `[+|-] digits`, no surrounding white space, no
  separator, no exponent, no suffix (D85, ruling 1: narrower than `decimal.Parse`'s own
  grammar, and narrower than C#'s `NumberStyles.Integer`, which additionally accepts
  surrounding white space) — or a number in that shape that does not fit the type's own
  range. One message for both causes, `decimal`'s own SHAPE one level up: `TryParse` cannot
  tell them apart either, so `Parse` does not pretend to (D85, ruling 2). `TryParse` is the
  pair that answers `0` and zeroes the `out` argument instead of panicking, exit 70.
  `u128.Parse("-1")` panics here too (D85, ruling 1): a leading `-` is a format failure
  exactly when the magnitude is nonzero — C#'s own documented `UInt128.Parse` throws
  `OverflowException` for `"-1"` and succeeds for `"-0"`, answering `0`.
- ``"teko: i128.TryParse is static; reach it through its type"`` / the `u128` twin — an
  INSTANCE reaching for `TryParse` (`x.TryParse(...)`); it is registered as a static row so
  a program reaching for it this way is told why instead of reading as a member nobody ever
  heard of, `decimal`'s own shape (D79).

Two are **run-time panics** raised by `lib/wide.tk` itself, exit 70 and on stderr with no
`file:line` ([runtime.md](runtime.md#the-wide-integer-library)). They share their wording
with ["Integer division"](#integer-division) above, whose guards over the eight plain widths
are a different source — built by `teko_ternary.tk` into the program and calling the
runtime's `panic` (D82):

- `"teko: division by zero"` — `x / 0i` and `x / 0u`, and (N6b-1, D83) `x % 0i` and
  `x % 0u`, the same guard: `tk_w_udiv` is asked about the divisor BEFORE the long division
  runs, because `tk_dv_divmod` over a zero divisor answers every bit set rather than
  failing, and a wrong quotient is worse than an abort. C# raises `DivideByZeroException`
  here; teko has no exceptions, so it panics.
- `"teko: an integer division overflowed"` — `i128.MinValue / -1i`, and (N6b-1, D83)
  `i128.MinValue % -1i` — the quotient `%` would need is the one value the width cannot
  hold, so `tk_w_smod` reads `tk_w_sdiv_ovf` unchanged, before either operand's sign is
  touched. D81's ruling 3, amended by D82: the two operands are read directly, ahead of the
  magnitude split every other divide takes (`MinValue` is bit 127 set and nothing else,
  `-1` is every bit set — the one value whose own negation is a no-op divided by the one
  divisor whose magnitude is 1). `u128` has no such row: an unsigned divide never overflows
  its own width.

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
- `` "teko: `%` takes no float operand" `` — D95: over a pair the CORE would own (neither
  side declares its own operator — a type that names `operator%` is unaffected, and still
  resolves the usual way against the rows it declares), `%` is not one of the promoted
  operators (this backend has no float remainder instruction), and either side being an
  `f32`/`f64` reached it exactly as written. `7 % 2.5` used to compile clean and read the
  float's bits as an integer's; `2.5 % 7` and `f64 % f64` already failed, but at the mc
  level (`mc: no float remainder`, no `file:line`). All three now refuse here instead,
  symmetrically on either operand, with one pinnable message.

## Integer division

D82: `/` and `%` over a plain integer (`i64`, `u64`, `i32`, `u16`, `i16`, `u8`, `i8`, …)
refuse a zero divisor at run time on every leg, and the signed forms also refuse the one
quotient the machine cannot hold — the core's own `sdiv`/`idiv` used to answer three
different things for the same source depending on which CI leg ran it (aarch64's `sdiv`
answered 0 for `/0` and the dividend for `%0` in silence; x86_64's `idiv` trapped, SIGFPE,
exit 136; and `MinValue / -1` overflowed the quotient register on x86_64 while aarch64
answered `MinValue` back). C# throws `DivideByZeroException` and, on both its own ISAs,
`OverflowException` for the second; this project has no exceptions, so it panics
(`tk_div_guard`, [`teko_ternary.tk`](../../teko_ternary.tk)).

A **literal** divisor is never guarded, zero or nonzero. `x / 2` is left exactly as the
core wrote it; a literal ZERO is answered where it is written, the way C# answers it
(CS0020, *Division by constant zero*, whatever the dividend is) — `12 / 0`, both sides
constant, is mc's own compile-time refusal (`division by zero`, no `teko:` prefix,
`fold_binary`/`const_bin`, `mc/src/parse.mc`), and `a / 0`, a dividend the folder cannot
see through, is the `teko:` refusal below. Only a divisor that is no literal at all
carries a run-time guard. The overflow check applies to signed integers only —
`type_signed`, the core's own predicate — and is built against the WIDTH's own `MinValue`
(`type_width`), so a narrow `i32`/`i16`/`i8` guards its own 32/16/8-bit minimum, not
`i64`'s.

A division in the RIGHT operand of `&&`/`||` is guarded **inside the branch that reaches
it**: the operator is lowered into the same `if`/`else` form `?:` already takes
(`tk_div_lazy_lower`, [`teko_ternary.tk`](../../teko_ternary.tk)) before the guard is
built, so `if (n != 0 && a / n > 1)` with `n == 0` runs neither the division nor its
guard, and a divisor with a side effect runs exactly once and only where it is reached.
Only an `&&`/`||` that carries such a division takes that form; every other one is
untouched.

- `"teko: division by zero"` — the divisor is the literal `0` and the dividend is not a
  literal (`a / 0`, `a % 0`). The same wording as the run-time panic below, and the same
  cause, answered at compile time because the source already says it.
- `"teko: an integer division needs #include rt.tk"` — a division the guard has to build
  reaches a unit where `panic` (`lib/rt.tk`) is not declared, directly or through
  `decimal.tk`/`time.tk`/… The declaration has to be the RUNTIME's own — the file it was
  read from is checked, not only the name — so a program carrying its own `panic` and no
  include is refused here too, rather than compiled into a guard that calls it. (A program
  that declares its own `panic` AND includes the runtime is mc's own `function declared
  twice`, ahead of this pass.) A program with no division that needs a guard never sees
  any of this: `tests/hello.tk`-style code with no `#include` at all keeps compiling.

Two are **run-time panics**, exit 70 and on stderr with no `file:line`
([runtime.md](runtime.md#failing)):

- `"teko: division by zero"` — the divisor was 0. Shared wording with `lib/wide.tk`'s own
  `i128`/`u128` panic below: the same defect, the same cause, on every integer width this
  project has.
- `"teko: an integer division overflowed"` — the divisor was `-1` and the dividend was the
  operand type's own `MinValue`. New with D82; shared wording with `lib/wide.tk`'s own
  `i128` panic below (D81's ruling 3, amended).

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
  cannot be returned, nor stored in a field, a static field, a GLOBAL of delegate type or
  an ELEMENT of a `T[]`, whether the array is a local or a global. A slot declared `T?` is
  one of those slots too: a `?` says the slot may hold nothing, never that it lives less
  long. A ternary is read BRANCH BY BRANCH at every one of them: `c ? held : other` is
  refused when either branch carries the capture, since either branch is what the slot ends
  up holding. Handing it to another LOCAL is allowed: the scope that owns the capture is
  still the one holding it.
- ``"teko: a delegate declared below `new` is not taught yet"`` — move the `delegate`
  declaration above the `new` that names it.
- `"teko: an array of this type is not taught yet"` — an array whose element is a type with
  no layout of its own here: a fixed array of such a type, and, since D99, the HEAP spelling
  of a qualified or generic one (`Geo.Circle[]`, `Box<Circle,2>[]`), which used to fall to
  the core's own `name expected`. A plain `T[]` of a class, `str`, an enum or a delegate is
  taught and is not this.
- `"teko: expected a captured name"` — `use ()` with something that is not a name inside.
- `"teko: use (...) already captures"` — the same name twice in one `use`.
- `"teko: the lambda does not match the delegate"` — the parameter count or the types
  differ from the delegate's.
- `"teko: unknown function"` — the name given to a delegate is no function.
- `"teko: argument "` — completed by *N is not passed by reference* or by *N needs
  `ref`/`out`*: a delegate or a function declares that parameter by reference and the site
  does not say so, or it declares it by value and the site writes `ref`/`out` anyway. The
  POINTEE is checked beside the kind, by the identity
  rule every `ref`/`out` argument takes (`ref i64` does not fit a `ref f64` slot), and a
  call through a delegate reads it from the delegate's own signature — the same wording a
  direct call gives, *teko: a value of type i64 does not convert to f64*. A VIRTUAL call, an
  INTERFACE call and the unqualified form of either give the same two sentences, with N
  counting the RECEIVER as argument 1 exactly as the mangled `Owner_method` a direct
  method call lowers to does.
- `"teko: wrong number of arguments for "` — completed by the name: the call's arity does
  not match.
- `"teko: "` — completed by one of the delegate-shaped messages:
  `" does not match the delegate "` (the target's own name on the left and the delegate's
  signature on the right, `teko: byval does not match the delegate Mut(ref f64)` — the
  arity, the return, the parameter types AND the `ref`/`out` kind of each all have to
  match, on the contextual road and on `new Op(...)` alike, which share one thunk; the
  POINTEE is part of the type, so `void fillc(ref Cell c)` on a `Fill(ref Box)` is refused
  here and nowhere later),
  *`Op` takes a function, another `Op`, or null*, *`X` is not
  captured; add it to use (...)*, and *`X` is used but never declared*. A MEMBER of the
  enclosing class (a field, a delegate field, a member const, a property, or a bare method
  NAME with no call around it) reads the same *X is not captured; add it to use (...)*
  when it is an INSTANCE one, judged before any global of the same name ever gets a look —
  a lambda captures nothing implicitly (D11), `this` included, so a global that only
  happens to share the name is never silently read or called in its place (D70,
  `tests/refuse/lambda_field_name.tk`, `tests/refuse/lambda_deleg_field_call.tk`,
  `tests/refuse/lambda_prop_name.tk`, `tests/refuse/lambda_method_name.tk`). A static
  member needs no receiver and still resolves. The WRITE side — a bare NAME on the LEFT of
  `=` (and `+=`/`++`, `<teko-loop-prelude>`'s own lowering to `=`) — is judged through the
  identical table before falling through as an ordinary local or global assignment: an
  INSTANCE field, an INSTANCE property with a `set`, or a method name reads the same
  sentence (D70, `tests/refuse/lambda_field_name_write.tk`); a STATIC field or a STATIC
  property's `set` needs no receiver and stores through it, the same `tk_field_store_val`
  coercion gate a plain static store already takes.
- `"teko: a method is not reachable from a lambda"` — completed by the method's name: a
  METHOD of the enclosing class, called bare inside a lambda (D70,
  `tests/refuse/lambda_method_call.tk`). `use (...)` captures a VALUE, never a method, so
  the wording above would read false here — this is its own sentence. A static method
  takes no receiver and still resolves.
- `"teko: "` — completed by *X* ` has no overload matching ` *Op(...)* (D67, `tests/
  refuse/deleg_new_overload_none.tk`): `new Op(f)` and the contextual `Op f = ...;` both
  resolve `f` at the same LATE walk now, and a genuinely OVERLOADED `f` is judged by every
  declaration's own signature — arity, return, every parameter's type AND `ref`/`out` kind
  — rather than whichever one happened to be declared first.
  `"teko: ambiguous overload for "` — completed by *Op(...)* `": "` *X*, its twin, when
  more than one candidate's signature fits — kept defensively (C# §10.2.3, exact match, no
  widening): two declarations of the identical signature, return type included, are
  `function declared twice` at the core's own hand before this check ever runs, so no
  fixture reaches it.

An ELEMENT of a `T[]` whose element type is a delegate is judged exactly as any other slot
of that type, and a GLOBAL array's element is judged exactly as a local array's. The store
into a global one is built by a PASS, where no scope stands around it, so the whole
judgement — the escape above, the conversion, and the type of the value — is taken at the
site by the delegate walk instead, with the lexical scope of that site live (D51, fourth
pass):

```teko
// no-run
delegate i64 Op(i64 a);
Op[] g_ops;
i64 main() {
    i64 x = 1;
    g_ops = new Op[2];
    g_ops[0] = new Op((i64 a) use (&x) => x + a);
                  // teko: a lambda that captures by reference cannot leave its scope
    i64 n = 5;
    g_ops[1] = n;                   // teko: Op takes a function, another Op, or null
    return 0;
}
```

A local wins over a global of the same name at that store as it does everywhere else, and a
local of delegate type wins over a free FUNCTION of that name: `Op f = addOne; g_ops[0] = f;`
stores the local `f`, never a wrap of the function `f`.

A plain GLOBAL of delegate type outlives its writer the same way an element of a global
array does, so a write into one takes the same verdict (D51, fifth pass):

```teko
// no-run
delegate i64 Op(i64 a);
Op g_op;
i64 f() {
    i64 acc = 0;
    Op local = new Op((i64 x) use (&acc) => acc + x);
    Op copy = local;                // fine: a LOCAL, inside the scope that owns `acc`
    g_op = local;
                  // teko: a lambda that captures by reference cannot leave its scope
    return copy(1);
}
```

A slot declared `T?` takes that verdict on the same terms — the plain global and the
element of a `T[]` alike. `Op?` is a row of its own, so the two sites that ask whether a
slot is of delegate type used to answer "no" for it and leave the rule unasked, where the
`Op`/`Op[]` beside it is refused (D51, twelfth pass). What a `?` changes is the value the
slot may take, not how long the slot lives:

```teko
// no-run
delegate i64 Op(i64 a);
Op? g_maybe;
Op?[] g_maybes;
i64 f() {
    i64 acc = 0;
    g_maybe = new Op((i64 x) use (&acc) => acc + x);
                  // teko: a lambda that captures by reference cannot leave its scope
    g_maybes = new Op?[1];
    g_maybes[0] = new Op((i64 x) use (&acc) => acc + x);
                  // teko: a lambda that captures by reference cannot leave its scope
    return 0;
}
```

`g_maybe = new Op((i64 x) => x + 1);` and `g_maybes[0] = null;` are unaffected: no capture
leaves anything, and `null` is the value a `?` slot is declared for.

And a value the store is written at cannot TYPE waits for the same walk whether the array
is global or local: `ops[0] = chooser(1)`, with `chooser` a local delegate that answers
`Op`, is judged where `chooser` has a type, never refused at the store (D51, fifth pass).
A bare NAME is one of those values at every store, because a PARAMETER shadows a function
of the same name and nothing the parser keeps records a parameter (D51, sixth pass). So is
a CALL, and a ternary of them with it: the store's own oracle reads the FIRST declaration
of the called name, which is neither the local that shadows it nor the overload the call
picks (D51, seventh pass). The judgement is the walk's, and the words are the delegate's:

```teko
// no-run
delegate i64 Op(i64 a);
i64 addOne(i64 a) { return a + 1; }
Op make(i64 k) { return addOne; }       // the FIRST declaration of `make`
i64 make(i64 k, i64 j) { return 7; }    // ...and the one this call picks
i64 main() {
    Op[] ops = new Op[1];
    ops[0] = make(1, 2);                // teko: Op takes a function, another Op, or null
    return ops[0](1);
}
```

The escape reads a ternary branch by branch, at every slot that outlives the capture
(D51, sixth pass):

```teko
// no-run
delegate i64 Op(i64 a);
i64 addOne(i64 a) { return a + 1; }
Op g_op;
i64 f(i64 c) {
    i64 acc = 0;
    Op held = new Op((i64 x) use (&acc) => acc + x);
    Op other = addOne;
    Op picked = c == 1 ? held : other;   // fine: a LOCAL holds it
    g_op = c == 1 ? held : other;
                  // teko: a lambda that captures by reference cannot leave its scope
    return picked(1);
}
```

The `null` a store or a `return` accepts is one shape and one only: `teko_type.tk`'s own
`tk_null`, the single `N_INT` this project ever types `TY_UPTR` (`tk_is_null_lit`,
[teko_struct.tk](../../teko_struct.tk)). An ordinary `i64` literal `0` is a DIFFERENT
node with the same kind and the same value, typed `TY_I64` — and the two checks that decide
whether a store waits for the walk (`tk_deleg_store_late`) or is coerced right there
(`tk_deleg_coerce`) used to ask `nd_kind(v) == N_INT && nd_val(v) == 0`, which both nodes
answer. A literal `0` written where an `Op` is expected slipped past both checks as if it
were `null`, and the mismatch the delegate's own words exist to catch never ran (D51,
eighth pass):

```teko
// no-run
delegate i64 Op(i64 a);
i64 main() {
    Op[] ops = new Op[1];
    ops[0] = 0;                         // teko: Op takes a function, another Op, or null
    return ops[0](1);
}
```

`null` itself is unaffected at every one of these sites — `ops[0] = null` still needs the
element declared `Op?` (the array's own rule, above), and `Op? h = null;` still passes; the
fix is the ONE predicate, `tk_is_null_lit`, asked in both places instead of the value alone.

…and *which slot a `null` may land in* is a question the delegate's own validator has to ask
itself, not one it may leave to the check beside it (D51, tenth pass). A store into an
element of an `Op[]` that waits for the walk is coerced by `tk_deleg_coerce`
([teko_deleg.tk](../../teko_deleg.tk)) and by nothing else — `tk_check_field_store`, which
carries rule 1 for every element store decided at its own site, is skipped for a value that
site cannot type (seventh pass, above) — and that validator returned every `null` unjudged.
A ternary is taken apart branch by branch, so an all-`null` one had no check at all in front
of it: both branches came back unchanged, the lowering saw two arms of the same `uptr` type,
and the element held a null until the first call through it panicked, *teko: call through a
null delegate*, exit 70. The rule is the one every other slot reads, in the same words: a
`null` branch at an `Op[]` element is a refusal, and an `Op?[]` is the array that takes one
(its element type answers `tk_deleg_row` with -1 and never reaches this validator at all):

```teko
// no-run
delegate i64 Op(i64 a);
i64 main() {
    i64 flag = 1;
    Op[] ops = new Op[1];
    ops[0] = flag == 1 ? null : null;   // teko: null needs a slot declared Op?
    return ops[0](1);
}
```

One `null` branch beside a real one (`ops[0] = flag == 1 ? addOne : null;`) is the same
refusal, at the branch that wrote it. It was refused before too, but by the lowering and one
pass later — *teko: the two arms of ?: have different types*, which named the shape of the
ternary and not the cause.

A FIELD of delegate type takes a bare function name the same way, and by the same judge
(D62). `Op cb = add;` on a local wraps the function, and `h.cb = add;` on the field beside
it answered *teko: the type of this value is not known here* — a free function's name is in
no slot table, so the oracle every field store reads types it -1 and the general judge
refused what no one could name. The name is handed to the delegate's own validator instead,
at the ONE door every field store passes through (`tk_field_store_val`,
[teko_typeof.tk](../../teko_typeof.tk)), and judged in `tk_deleg_pass` where names have
types. The detection sits at the door, so every caller of the door gets the same verdict —
these are all of them:

| the store | the caller of the door |
|---|---|
| `h.cb = add;` and `this.cb = add;` | `tk_field_use`, [teko_expr.tk](../../teko_expr.tk) |
| the implicit `cb = add;` of a method or constructor | `tk_this_assign`, [teko_this.tk](../../teko_this.tk) |
| `H.scb = add;` on a STATIC field | `tk_static_use`, [teko_access.tk](../../teko_access.tk) |
| the same, on a type declared BELOW the store | `tk_fwd_resolve_static_one`, [teko_access.tk](../../teko_access.tk) |
| `h.cbs[0] = add;`, an element of an ARRAY FIELD | `tk_array_index`, [teko_struct.tk](../../teko_struct.tk) |
| `h.cb = add;` through a receiver typed at pass time (a parameter of a class declared below) | `tk_pend_field`, [teko_typeof.tk](../../teko_typeof.tk) |
| `a[i] = e` on a FIXED array | `tk_arr_elem_store`, [teko_array.tk](../../teko_array.tk) — no element of delegate type reaches it: ``teko: an array of this type is not taught yet`` refuses the declaration |
| `xs[i] = e` on a `T[]` | `tk_ha_store`, [teko_heaparr.tk](../../teko_heaparr.tk) — a bare name there never reaches the door at all: the element road's own validator judges it (D51) |

All six that can carry one are exercised by `tests/surface_delegate.tk`. A
signature that is not the delegate's is refused in the delegate's own words, the same
sentence the local slot gives:

```teko
// no-run
delegate i64 Op(i64 a);
i64 wrong(i64 a, i64 b) { return a + b; }
class H { public Op cb; }
i64 main() {
    H h = new H;
    h.cb = wrong;                   // teko: wrong does not match the delegate Op(i64)
    return 0;
}
```

A PARAMETER of delegate type stored into a field is the name that proves the judge is the
walk's and not the door's: `void setcb(H h, Op p) { h.cb = p; }` stores the parameter, never
a wrap of a free function `p` declared elsewhere in the unit. And a name of a type that is
neither is refused in those same words — `i64 g_n; h.cb = g_n;` now reads *teko: Op takes a
function, another Op, or null*, the delegate's own sentence, where the general conversion
words (*a value of type i64 does not convert to Op*) used to answer for it.

## Arrays

- ``"teko: `new T[]` needs a length; write `new T[n]`"`` — the length is an expression, and
  it is required.
- `"teko: an array has no member"` — a `T[]` has `Length` and nothing else.
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
- `"teko: a string is immutable"` — `s[i] = c` (N8, `docs/specs/string.md` § 6/§ 10,
  D89): a `string` receiver's `[` is refused a write outright, ahead of parsing the
  right-hand side at all — C#'s own rule, and teko's. `s[i]` alone still reads (§ 6),
  through `tk_string_at`, `lib/string.tk`'s own top-level function.
- `"teko: string index out of range"` — a RUN-TIME panic, exit 70, written by
  `tk_string_at` (`lib/string.tk`, not a compiler literal): `s[i]` past `s.Length - 1`,
  or negative, the same guard every `T[]` index already carries.
- ``"teko: `[` needs an array"`` — an index on a receiver whose type the parse does not
  know to be one. Bind it to a local of the right type first. The name the base spells is
  appended when it has one, and a name that SHADOWS a global array is the shape that
  reads oddest: the global's own index is rewritten by a pass that matches by name alone,
  so the refusal is raised where the parser still knows what the binding really is. A
  LOCAL and a FIELD of the class being parsed both shadow that way, and so do the other two
  members a bare name stands for — a member `const` and a PROPERTY. A field or a property
  that IS an array is read as the member instead, index and all: the field through its own
  load, the property through its getter.

  Two bindings the parse cannot see are judged in the typeof pass instead, and answer with
  the very same sentence (D96): a PARAMETER of the enclosing function, which is in no
  parse-time scope at all, and a member declared BELOW the method that reads it. A member
  declared below that IS a `T[]` is refused here too, not read as the member — the parser
  never saw it, exactly as it never sees it when no same-named global exists. Declare the
  member above its readers, or rename one of the two names.

```teko
// no-run
i64[] src;                    // a GLOBAL array...
i64[] dst;

i64 f() {
    i64 src = 3;              // ...shadowed by a local of another type
    dst[0] = src[0];          // teko: `[` needs an array: src
    return dst[0];
}

class H {
    public i64 src;           // ...and shadowed by a FIELD, one scope out
    public i64[] xs;

    public i64 g() {
        return src[0];        // teko: `[` needs an array: src
    }

    public i64 h() {
        xs[0] = 8;            // ...while a field that IS an array reads the field
        return xs[0];
    }
}

class K {
    public const i64 src = 5; // ...and shadowed by a member CONST

    public i64 g() {
        return src[0];        // teko: `[` needs an array: src
    }
}

class L {
    private i64  n;
    private i64[] back;
    public i64 src { get { return n; } set { n = value; } }
    public i64[] ys { get { return back; } set { back = value; } }

    // ...and by a scalar PROPERTY, the third member a bare name resolves to
    public i64 g() {
        return src[0];        // teko: `[` needs an array: src
    }

    public i64 h() {
        ys[0] = 8;            // ...while a `T[]` PROPERTY is read by its getter
        return ys[0];
    }
}

// D96, the two bindings the parse cannot see
i64 p(i64 src) {
    return src[0];            // teko: `[` needs an array: src -- a PARAMETER
}

class M {
    public i64 g() {
        return src[0];        // teko: `[` needs an array: src -- declared BELOW
    }

    public i64 src;
}
```

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
- `"teko: a global does not hold a nullable box"` — completed by the global's own name: a
  GLOBAL declared `T?` over a VALUE (`i64? n;` at the top of a file). The box that type
  promises is built by the rc pass out of the SCOPE a local lives in, and a global has
  none, so the slot would hold the bare value and every read through it would follow it as
  a pointer. Declare the global `T` and a local `T?`, or keep the state in a class. A `T?`
  over a REFERENCE is a global like any other — its handle IS the pointer — and is read,
  written, `.Value`d and `??`d exactly as a local is (D48).
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

A `ref`/`out` argument that names a GLOBAL is checked exactly as one naming a local: the
pointee identity rule above (`tk_ref_check_pointee`, teko_ref.tk) reads a global's own
declared type (`tk_ty_global`, teko_array.tk) once the argument is neither a parameter nor a
local of the function being walked, so `ref f64` refuses an `i64` global with the same
wording a local of the wrong type already gets (D51):

```teko
// no-run
i64 g = 7;
void bump(ref f64 x) { x = x + 1.0; }
i64 main() {
    bump(ref g);                    // teko: a value of type i64 does not convert to f64
    return 0;
}
```

A local of the same name wins over the global, as it does everywhere — and it wins
LEXICALLY, at the site: `if (c) { f64 g = 2.0; bump(ref g); }` passes, global `g` or no
global `g`, because inside that block the name IS the `f64` local; the same `bump(ref g)`
written after the block has closed names the global again and refuses. One answer per site,
from the scope open there (`tk_ty_scope_or_global`, teko_typeof.tk) — the same oracle that
types the argument downstream, which is what keeps the check and the conversion from
disagreeing (D51, verifier finding: they disagreed, and an ADDRESS was widened into a
float). The one pointee still read as "not known here", refusing nothing, is a name neither
the scope open at the site nor the table of globals holds a row for.

A call through a DELEGATE is judged by the same rule, from the delegate's own signature.
That call is a `callp`, built after the `ref` pass and naming no callee, so nothing
downstream ever reads its arguments: the delegate's own argument check
(`tk_deleg_check_arg_kinds`, [teko_deleg.tk](../../teko_deleg.tk)) is the only door — and
it is the door for all three roads alike, a delegate LOCAL, PARAMETER or GLOBAL called by
name, a delegate FIELD, and an `Op[]` ELEMENT. An argument whose type that door cannot read
where it stands is deferred to the same point a virtual call's is, so the pointee is judged
on every road (D59; before it, the two roads whose `callp` is built while the file is still
being parsed read no pointee at all and an `i64`'s address reached a callee that writes a
double through it):

```teko
// no-run
delegate void Mut(ref f64 x);
void bumpf(ref f64 x) { x = x + 1.0; }
i64 main() {
    Mut m = bumpf;
    i64 g = 7;
    m(ref g);                       // teko: a value of type i64 does not convert to f64
    return 0;
}
```

The argument passed BY VALUE takes the same judgement at that same door, and the same
conversion: `delegate i64 Op(i64); Op f = twice; f64 lf = 8.5; f(lf);` is refused
`teko: a value of type f64 does not convert to i64`, exactly as `twice(lf)` is, and
`delegate f64 F(f64); F f = half; f(5);` widens the 5 the way `half(5)` does (D59 — before
it the door read the `ref`/`out` kind and nothing else, so the value crossed as its own raw
bits on all three roads).

A VIRTUAL call, an INTERFACE call and the UNQUALIFIED form of either inside a method are
`callp`s too, and their arguments take the judgement a direct call's take — by type
identity, by *derives/implements*, with C# §10.2.3's widening of an integer onto a float
parameter, and with the `ref`/`out` pointee rule above (D57). The site that builds such a
call is the only point that knows which method it reaches, and until this rule it judged
only what the parser could type there: a GLOBAL, a `ref`/`out` pointee, and — on the
unqualified road, which is rewritten from a pass — a plain local too crossed unjudged and
unconverted. A `const` is folded into its literal before any of the three reads it, so it
is judged as a literal and never deferred:

```teko
// no-run
class Point { public i64 x; public Point(i64 v) { x = v; } }
class Box {
    public i64 w;
    public Box(i64 v) { w = v; }
    public virtual i64 take(Point p) { return p.x; }
    public virtual i64 takei(i64 n) { return n; }
    public virtual i64 bump(ref f64 d) { d = 2.0; return 42; }
    public virtual i64 mine() {
        f64 lf = 1.5;
        return takei(lf);           // teko: a value of type f64 does not convert to i64
    }
}
Box gb;
f64 gf;
i64 gi;
i64 main() {
    Box b = new Box(9);
    b.take(gb);                     // teko: a value of type Box does not convert to Point
    b.takei(gf);                    // teko: a value of type f64 does not convert to i64
    b.bump(ref gi);                 // teko: a value of type i64 does not convert to f64
    return 0;
}
```

An interface call refuses each of the three in the same words. A name that reaches the
judge with no type at all is `"teko: the type of this argument is not known here"`, the
sentence a primitive row's deferred argument already gets.

An argument that is a COMPOSITE EXPRESSION — a binary, a ternary, a negation — is judged
and widened exactly like a bare name, on all four roads: `b.takef(1 + 2)` on an `f64`
parameter gets C# §10.2.3's widening, and `b.takei(lf * 2.0)` is refused *teko: a value of
type f64 does not convert to i64*, in the direct call's own words
(`tests/refuse/vcall_arg_expr_narrow.tk`, `deleg_arg_expr_narrow.tk`).

Two things are skipped in silence here, and on the direct road for the same reason. The
first is a `ref`/`out` argument whose POINTEE is named by neither the lexical scope nor the
table of globals: the address was built by the source, its target has no declared type, and
there is nothing to compare it against — every road reads that one rule
(`tk_ref_check_pointee_ty`, teko_ref.tk). The second is a by-value argument no reader teko
has can type even at the last pass — a `.` on a receiver no pass resolved, an indirect
`callp` — where refusing would refuse code with nothing wrong with it. That second case was
a wider hole until D59's second pass: it read "not a bare name and not a call to an
overloaded one", which covered every composite expression and let each one cross unjudged.

The `ref`/`out` TAG itself is not skipped on any road. It is compared with the parameter's
own kind before anything else, in the direct call's own words — *teko: argument 2 is not
passed by reference* for `b.takei(ref x)` on a by-value `i64`, *teko: argument 2 needs
`ref` at the call site* for `b.bump(x)` on a `ref f64`.

An argument that is a CALL to an OVERLOADED name is judged after the pick, never against
the first declaration of that name: with `f64 pick(f64)` declared ahead of `i64 pick(i64)`,
`b.takei(pick(2))` is accepted and `b.takef(pick(2))` gets the widening it is owed
(`tests/vcall_overloaded_arg.tk`).

- ``"teko: `main` takes one signature"`` — the entry point is not overloaded.
- ``"teko: an `extern` name owns its symbol and cannot be overloaded"`` — an `extern` keeps
  the C symbol.
- `"teko: the name is the compiler's own"` — completed by the name: a top-level declaration
  of the program's own carries a symbol some generator writes — `tkarr_new_i64`,
  `tkarr_put_i64` (a `params T[]` or a `new T[n]`), `tk_nl_ck`, `tk_nl_new`,
  `tk_nl_box_i64`, `tk_nl_dflt`, `tk_nl_vt` (a `T?`), a class's own `Name__vt`, an enum's
  `Name__names`, and every symbol a type declaration lowers into: a method's `point_area`
  and a constructor's `point_ctor__i64`, a property accessor's `square_get_Side` /
  `square_set_Side`, a struct's own allocator `stamp_new`, a static field's global
  `stamp_made`, a service's `svc_di_slot` / `svc_di_get`, and a generic instance's
  `box__circle__2_cap`. The two would reach the linker as one symbol and a call site would
  pick whichever table answered first. Only the EXACT name a generator wrote is taken, and
  only against a declaration the compiler did not write: no prefix is reserved, so
  `tkarray`, `tk_nl_boxer`, `point_areas` and `pointarea` are a program's own names like any
  other. Rename the declaration. A function a `namespace` mangles (`geo__area` out of
  `namespace geo { i64 area() }`) is NOT one of these: the program wrote that declaration,
  and a program that also writes `geo__area` at top level reaches the core's own
  `function declared twice`.
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
- `"teko: unknown member of "` — completed by the type's name. Since D61 a receiver that
  is a CALL is among them — `mk().Nope`, `pick(1, 2).Nope` and `f().Nope` through a
  delegate slot all read `teko: unknown member of DateOnly: Nope`, the type named, where
  a call through a slot used to be refused by the member's name alone. The type named is
  the one the PICK returns, on a row type as much as on a primitive one: with
  `Cell cpick(i64)` declared ahead of `Box cpick(i64, i64)`, `cpick(1, 2).pad` reads
  `teko: unknown member of Box: pad` (`tests/refuse/call_member_unknown.tk`), where the
  first declaration of the name used to answer for the call and let the line compile.
- `"teko: unknown member"` — the same, where the type has no name to print.
- `"teko: the member is a field, not a method"` — drop the `()`. A field of **delegate**
  type is the exception: it is callable wherever it is read, on every road a receiver
  takes ([delegates.md](delegates.md#calling)).
- `"teko: the member is a method; call it with ()"` — add them.
- `"teko: a virtual call needs a name or a field on the left"` — a virtual call needs a
  receiver the compiler can name.
- `"teko: a "` — completed by *`<what>` needs a class*: a construct that only a class
  carries (a constructor, a destructor, a vtable slot) was written on another kind of type.

### A field store whose value only the pass can type

A store is built where it is written, and the value's type is the one thing the site may
not know then. A parameter is in no parse-time scope; an implicit `f = e` is rewritten from
a pass with its right-hand side still a bare name; a `static` field on a type declared below
is resolved one pass ahead of the scope that would answer; a **call to an overloaded name**
is typed by the signature its arguments pick, which is written at the overload pass and is
not always the first declaration of the name; and a **user operator** is not the call it
stands for until the operator pass has lowered it. None of that is a licence to write the
raw bits — the check and the conversion are deferred to one point, the end of the overload
pass, where every one of those is already settled, and the value is then judged by the very
rule every other slot is judged by (D50, the review findings on
[#698](https://github.com/teko-org/teko-lang/pull/698)). What the deferral buys is a
compile-time refusal where the eight bytes used to cross unread:

```teko
// no-run
enum Color { Red, Green, Blue }
class Cell { public i64 v; }
class Box { public Cell c; }
class H { public static Cell c; }

i64 pick(i64 a)         { return 5; }
f64 pick(i64 a, i64 b)  { return 2.5; }

class N {
    public i64 n;
    // the PICK is the two-argument one, whatever the first declaration returns
    public N() { this.n = pick(1, 2); }   // teko: a value of type f64 does not convert to i64
}

void put(Box b, Color k, uptr raw, i64 n) {
    b.c = k;                     // teko: a value of type Color does not convert to Cell
    b.c = raw;                   // teko: a value of type uptr does not convert to Cell
    H.c = n;                     // teko: a value of type i64 does not convert to Cell
}

i64 main() { return 0; }
```

The receiver above is a **parameter**, which the parser cannot type either: that store is
rebuilt by the pass and goes through the same gate, so a `T?` field written through it boxes
its value like every other site rather than taking the raw bits for a box handle.

An **element of a `T[]`** is that same slot: `xs[i] = e` is judged by the very gate, after
the pick, so a narrowing into an `i64[]` is refused, an integer into an `f64[]` is widened
and an `i64?[]` boxes what it is given.

```teko
// no-run
class Cell { public i64 v; }

i64 rick(i64 a)         { return 5; }
Cell rick(i64 a, i64 b) { Cell c = new Cell(); c.v = 7; return c; }

i64 main() {
    i64[] ns = new i64[2];
    ns[0] = 1.5;                 // teko: a value of type f64 does not convert to i64
    Cell[] cs = new Cell[2];
    cs[0] = null;                // teko: null needs a slot declared Cell?
    cs[1] = rick(1, 2);          // ...and this one compiles: the PICK returns a Cell
    return 0;
}
```

- `"teko: the type of this value is not known here"` — the deferred store reached its one
  point of judgement and **no** oracle could type the value even there. A store is judged or
  it is refused; it is never written raw, because what crosses unread is the value's own
  eight bytes in a slot of another type. Bind the value to a local of the right type first,
  the same answer *the type of this argument is not known here* gives one position over.

A **`void` call** reaches the same judgement with a type, and it is no value: the store is
refused in the wording every mismatched value gets. mc's core already refuses a `void` where
it can see one (`value of type void`, its own message, the one a local initializer gets),
but a VIRTUAL or an INTERFACE call is indirect and the core types every indirect call `i64`
by itself — so the declared return, `void` included, is what the compiler records and what
the store is judged against. The verdict does not depend on WHEN the store learns it: on a
receiver the parser already types (`B b = d; h.n = b.M();`) the answer is there at the store
site itself, on a receiver only the pass types it arrives at the judgement, and one function
answers for both.

```teko
// no-run
class B { public virtual void M() { } }
class H {
    public i64 n;
    public void go(B b) {
        this.n = b.M();      // teko: a value of type void does not convert to i64
    }
}
```

## `.ToString()` on a core scalar (D98)

`.ToString()` on `i8`/`i16`/`i32`/`i64`/`u8`/`u16`/`u64`/`usize`/`char` is the very
conversion an interpolation hole makes (`tk_scalar_tostring`, `teko_typeof.tk`), so the two
always write the same text. Everything else keeps the refusal it already had:
`"teko: f64 has no members: ToString"` for a float (there is no float formatter in this
tree, which is why `$"{3.5}"` refuses too), `"teko: uptr has no members: ToString"` for
`str`/`ptr`/`uptr` (one type id, so no oracle can tell text from an address),
`"teko: unknown member of Box: ToString"` for a class that declares none, and
`"teko: unknown member of i64?: ToString"` for a `T?`, which is read through `.Value`.
`.ToString` without `()` answers `"teko: the member is a method; call it with ()"` and
`x.ToString("D4")` answers `"teko: wrong number of arguments for ToString"` — C#'s format
string is not taught.

- `"teko: .ToString() needs #include \"string.tk\" before it is used"` — the call builds a
  `string`, so the class has to be declared first, exactly as an interpolation hole needs
  it. Add the include. Named rather than left to the member-name refusal, which would blame
  `ToString` for a missing `#include`.

## String interpolation (`$"…"`)

- `"teko: $ needs a string literal for interpolation"` — `$` is claimed program-wide the
  moment `teko_init()` runs, so the handler reads every `$` in the program, not only the
  ones that open an interpolation. It answers this for `$` followed by a token that is not
  a string literal *and that the handler is the one to see*: `${1}`, `$ 1`, `$(1)`. Two
  neighbouring forms never reach it and answer with mc's own words instead — `$1` is
  `expression with no codegen` and `$name` is `hole $name has no rule binding it`, both
  the core's, both unchanged by this crumb. Write `$"…"`.
- `"teko: string interpolation needs #include \"string.tk\" before it is used"` — `$"…"`
  lowers into the `string` class's own `operator+`, which only exists once the class does.
  Add the include. The check runs *after* the string-literal one above, so a `$` that is
  not an interpolation at all is never blamed on a missing include.
- `"teko: a line comment does not fit in an interpolation hole"` — a `//` inside `{…}`.
  The handler pushes the whole `+` chain back through the lexer as **one line**, so a `//`
  would comment out the rest of it. A block comment (`{x /* note */}`) is fine and is
  skipped by the brace scan.
- `"teko: an interpolation hole holds no null"` — `$"{null}"`. `str`, `ptr`, `uptr` and the
  `null` literal are ONE type ([types.md](types.md#limits)), and `new string(str)` measures
  its argument with `tk_str_len`, so a `null` hole would read address zero. The literal is
  the one member of that family the interpolation pass can tell apart (`tk_is_null_lit`),
  and it is refused where it is written; a `str` VARIABLE holding 0 is the constructor's
  own guard instead, which answers the EMPTY string (D92's amendment).
- `"teko: an interpolation hole holds no ternary, ?? or ?."` — `$"{(a > 0 ? 5 : 6)}"`,
  `$"{x ?? 3}"`, `$"{o?.Name}"`. Each parks as a marker call that `tk_ternary_pass`
  unpacks, and that pass runs *after* the one the hole is typed in, so the hole's type
  cannot be asked for yet ([not-yet.md](not-yet.md)). Compute the value into a local and
  interpolate the local.
- `"teko: an interpolated string needs }} for a literal }"` — a bare `}` with no matching
  `{` is not this page's own escape; double it, `}}`, C#'s own answer for the same byte.
- `"teko: unterminated interpolation hole"` — a `{` opened a hole and no matching `}`
  closed it before the string literal itself ended.
- `"teko: an interpolation hole needs an expression"` — `{}` is empty; write one.
- `"teko: an interpolation hole holds one expression, no alignment or format specifier"` —
  a top-level `,` (C#'s alignment, `{x,10}`) or `:` (its format specifier, `{x:N2}`) inside
  a hole. Neither is taught ([string.md](../specs/string.md) § 9); format the value first
  and write the result into the hole.
- `"teko: malformed literal text in an interpolated string"` — the `+` chain the handler
  builds from the literal's pieces and holes did not parse back into the shape it wrote.
  The chain is pushed as its own source, terminated by a `;` the handler writes itself, so
  the parse cannot run past its own frame into the file around it; this message is what is
  left when the parse still does not come back with that exact chain. It is an invariant of
  the handler's own re-lexing and no hand-written source is known to reach it — but "known"
  is the claim, not "impossible": an earlier head reached it from plain C# (`$"a{n}" + x`,
  `$"c" == s`) because the terminator was missing, and those all compile now.
- `"teko: no interpolation of a value of type "` — completed by the type's name. Only
  `string`, `str`/`ptr`/`uptr`, `char` and the integers have a formatter
  ([string.md](../specs/string.md) § 9's own table); everything else — `f64`/`f32`,
  `decimal`, `DateTime`, `TimeSpan`, `Guid`, an `enum` — needs its value turned into a
  `string` first (`.ToString()` where the type has one) and concatenated with `+`.
- `"teko: the type of this value is not known here"` — a hole whose static type the
  compiler cannot determine at all, the same "not known here" every other deferred site
  gives. The ternary, `??` and `?.` cases have their own message above.

A `.` written directly on a `$"…"` (`$"a{n}".Length`) is refused `teko: uptr has no
members: Length`. That is not this construct's own answer: a plain `"abc".Length` and a
plain `("a" + s).Length` are refused the same way on `main` today, because a `str` literal
carries no members. Bind the interpolated string to a `string` local first.

## Capacity

Every table the compiler keeps has a ceiling. Hitting one is a diagnostic, not a silent
truncation; the fix is to split the unit.

| message | limit |
|---|---|
| `"teko: too many type declarations"` | 256 rows of the shared type table in one unit (D69; was 32, raised to match `TK_MAXFWD`): every struct, class, interface, enum and delegate declared takes one, and so does every distinct `T[]` (`tk_ha_row`) and `T?` (`tk_nl_row`) the unit spells, so the ceiling can be reached with fewer than 256 declarations |
| `"teko: too many fields"` | 256 fields, summed |
| `"teko: too many methods"` | 1024 methods, summed (D69; was 128) |
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
| `"teko: too many forward-declared types"` | 256 read ahead of their use (D69; was 32) |
| ``"teko: too many `new` on a type declared below"`` | 32 |
| `"teko: too many static accesses on a type declared below"` | 32 |
| `"teko: too many consts"` | 128 member constants |
| `"teko: too many compiler-written nulls in one unit"` | 64 ternaries over a reference type in one unit |
| `"teko: too many top-level consts"` | 128 |
| `"teko: too many wide types"` | 8 sixteen-byte types registered with `tk_wide_add` (`decimal`, `Guid`, `DateTimeOffset`, `i128` and `u128` today, five of the eight; D81 raised the ceiling from 4, which N6a's two registrations filled exactly). It is a COMPILER ceiling, not a program's: it fires at `teko_init()` time and no source can reach it |
| `"teko: too many local arrays"` | 1024 declarations in scope |
| `"teko: too many global arrays"` | 512 in one source |
| ``"teko: too many global `T[]` of heap"`` | 32 in one source |
| `"teko: too many globals"` | 2048 global slots in one source — every global that holds one value, which is what the oracle answers for by name |
| `"teko: too many array writes waiting to be resolved"` | 512 |
| `"teko: too many array indexes waiting to be resolved"` | 4096 index bases a global-array rewrite consumed and left for the typeof pass to judge the binding of (D96) — one per `g[i]` on a global array, read or write |
| `"teko: too many array-field accesses"` | 128 |
| ``"teko: too many `T[]` parameters in one declaration"`` | 32 |
| `"teko: too many locals in one unit"` | 8192 |
| `"teko: too many locals in one function"` | 8192, the same ceiling — the names one body has in scope at once (its parameters, its locals and the temporaries the compiler declares beside them) are a subset of the unit's own locals, so a body the parser accepted always fits and only a compiler-written temporary can reach this. It was a silent stop at 256 before, which answered −1 about a declaration that was right there: past 255 locals a `f64 x` shadowing a `ref i64 x` parameter went unrecorded, and the call that passed `ref x` was refused *teko: a value of type i64 does not convert to f64* on a legal program |
| `"teko: too many locals of struct type"` | 256 |
| `"teko: too many expressions whose type is known"` | 4096 expressions the parser typed in one unit — every load of a field, of an array element and of a `T[]`, every box and every indirect return spends one; 239 in `tests/surface_nullable_ops.tk`, the busiest fixture |
| `"teko: too many member accesses on a value of unknown type"` | 4096 member accesses waiting for the pass — a `.` on a receiver the parser cannot type (a parameter, a global, a type declared below) and, since D61, a `.` on any CALL the node itself carries no type for, `mkday().Day` included. It was 128 while only the first kind waited here |
| `"teko: too many stores into a slot of class type"` | 4096 (D69; was 128) |
| `"teko: too many field stores of unknown type"` | 4096 field stores whose value no oracle types at the site, waiting for the pass; 34 in `tests/surface_field_store.tk`, the busiest fixture |
| `"teko: too many deferred call arguments"` | 4096 arguments of a VIRTUAL, an INTERFACE or an unqualified virtual call whose type the site that built the `callp` could not read — a global, a `ref`/`out` pointee, a bare name on the unqualified road — waiting for the pass; 26 in `tests/surface_globals_calls.tk`, the busiest fixture, and 1 in `tests/primitives_float.tk` |
| `"teko: too many declarations in one unit"` | 8192 |
| `"teko: too many generated declarations in one unit"` | 4096 top-level declarations the compiler itself writes — a vtable, a release, an allocator, a thunk, a box, an enum's two globals (D69; was 512); 134 in `tests/surface_lambda.tk`, the busiest fixture |
| `"teko: too many overloaded names in one unit"` | 64 |
| `"teko: too many free-function declarations with parameters"` | 4096 |
| `"teko: too many arguments"` | 64 at one call of an overloaded name |
| ``"teko: too many `params` lists in one unit"`` | 64 |
| ``"teko: too many `params` declarations in one unit"`` | 64 |
| ``"teko: too many `ref`/`out` parameters in one unit"`` | 512 |
| ``"teko: too many `ref`/`out` arguments in one unit"`` | 512 |
| `"teko: too many delegate targets"` | 64 (delegate, function) pairs |
| `"teko: too many element stores of unknown type"` | 512 stores into an element of delegate type, in one unit, whose value only the walk can type. A delegate is a counted type, so every element store of one takes a row of the table above first, but that ceiling is 4096 now (D69) — comfortably past 512 — so this table's own ceiling is the one a program hits first: 513 of them refuse with this wording, at the 513th, not the row above's |
| `"teko: too many delegate targets awaiting resolution"` | the same 512-row table (`TK_MAXDGLATE`, `teko_deleg.tk`), shared rather than duplicated (D67): an explicit `new Op(f)` site waits here exactly as an element store does, keyed by the SAME node-id bucket, so a unit's element stores and its explicit thunk sites draw on one combined budget of 512, not two separate ones |
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
| `"teko: too many primitive types"` | 16 primitives with a member table (D74's own row was stale at 8; D81 raised it to 16, because N6a's `i128` and `u128` took 6 to 8 and filled the old table exactly) |
| `"teko: too many primitive members"` | 192 rows, over every primitive (D76 corrects a stale `96` this table carried since before D74; D79 raises 160 to 192, C5's nineteen `decimal` rows having filled the old cap exactly; D85: 160 to 178, N6b-3's nine rows per type over `i128`/`u128`) |
| `"teko: too many primitive operators"` | 128 rows, over every primitive (D76: raised from 48, itself a stale `32` this table carried since before D74 — `DateTimeOffset`'s nine rows took the true count past 48; D77: raised to 80, `decimal`'s twelve rows took 50 to 62; D81: raised to 128, N6a's twenty-two wide-integer rows took 62 to 84) |
| `"teko: too many primitive conversions"` | 32 rows of the cast-to-call table, over every primitive — the five `decimal` registers, N6a's eight `i128`/`u128` rows and N6b-2's eight more `f64`/`decimal` rows (D77, D81: raised to 16, which those thirteen overflowed; D84: raised to 32, twenty-one of it now spent). A cast whose target the table does not name is refused, never lowered, so the ceiling is a COMPILER one and no source can reach it |
| `"teko: too many primitive parameter positions"` | 128 argument positions, summed over every member row — 101 spent after C5 (D79); 111 after D85, N6b-3's five positions per type over `i128`/`u128` |
| `"teko: too many late type names over a primitive"` | 4 types a row names before the include that declares them is read |
| `"teko: too many primitive arguments of unknown type"` | 128 arguments of primitive or `enum` position, in one unit, whose type only the pass can tell |
| `"teko: too many casts over a primitive in one unit"` | 4096 casts the compiler wrote itself, in one compilation unit |
| `"teko: too many services"` | 32 marked classes |
| ``"teko: too many `inject` sites"`` | 32 |
| ``"teko: too many `scope` blocks"`` | 64, plus one per singleton |
| `"teko: scopes nested too deep"` | 32 open at once |
| `"teko: too many nested service dependencies"` | 32 under construction at once |
| `"teko: too many distinct string literals"` | 512 distinct `"..."` literals reaching a `string` slot, in one compilation unit (N7b, D88 — each interns to its own `$tk_str_<n>` global, § 4) |

---

What a v0.4.0 program cannot express at all, with the message it gets, is
[not-yet.md](not-yet.md).
