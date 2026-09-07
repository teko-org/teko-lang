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
  `struct`, `interface` or `trait` inside another type's body. Move it out; there is no
  nested type.
- `"teko: the name is already a type"` — the name is already a `class`, `struct`,
  `interface`, `trait` or `delegate` in this namespace.
- `"teko: the name is already a generic"` — the name belongs to a generic declaration.
- `"teko: name of "` — completed by *`<what>` expected*: a declaration keyword was read and
  what followed is not a usable name.
- `"teko: type not taught yet"` — the word in type position is reserved for a construct
  this version does not implement.
- `"teko: a value of type "` — completed by *`X` does not convert to `Y`*: the only implicit
  reference conversions are derived-to-base and class-to-interface.
- `"teko: field of type void"` — a field has a type; `void` is a return type only.
- `"teko: duplicate field"` — two fields of one type share a name.
- `"teko: an array field size is an integer literal"` — `T items[N]` takes a literal or a
  `const`, folded at the declaration.
- `"teko: an array field size is positive"` — the size is `> 0`.
- `"teko: a member declaration needs a name"` — a member's type was read and no name
  followed.
- `"teko: the modifier opens a class, a struct, an interface, a trait or a delegate"` —
  `public`/`internal` at top level must be followed by one of those five words.
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
- ``"teko: `[` indexes a `params` list only"`` — an index on a receiver whose type the parse
  does not know. Bind it to a local of the right type first.

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
- `"teko: no overload of "` — completed by *`X` matches these arguments*.
- `"teko: more than one overload of "` — completed by *`X` matches these arguments*: two
  candidates fit.
- `"teko: ambiguous overload; two signatures take this many arguments"` — the arity alone
  does not choose.
- `"teko: the type of argument "` — completed by *N of `X` is not known here*: the
  argument's type could not be determined, so the overload could not be chosen.
- `"teko: too few arguments"` — the call passes fewer than the declaration requires.
- ``"teko: `params` declares a parameter list, nothing else"`` — `params` only in parameter
  position.
- ``"teko: `params` must be the last parameter, and there is only one"`` — one list, at the
  end.
- ``"teko: a `params` list has no default"`` — the site decides the count.
- ``"teko: a `params` list cannot be overloaded"`` — one declaration per name.
- ``"teko: a `params` function exists once per call site and has no address"`` — `&f` on a
  variadic.
- ``"teko: a `params` list is instantiated per call site and needs a body"`` — a prototype
  cannot carry one.
- ``"teko: an `extern` symbol takes no `params` list"`` — the C ABI is not variadic here.
- ``"teko: a `params` list holds words; a float argument is not taught yet"`` — pass the
  bits, or a fixed parameter.
- ``"teko: a `params` list holds words; its element does not read as a float"`` — an element
  read back as a float.
- ``"teko: the name a `params` instance takes is already declared"`` — the per-site
  instance's name collides with an existing declaration.

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
| `"teko: too many arguments"` | 12 at one call |
| ``"teko: too many arguments for a `params` list (twelve, the fixed ones included)"`` | 12 |
| ``"teko: too many parameters before `params` (the list costs two of the twelve)"`` | 10 |
| ``"teko: too many `params` declarations in one unit"`` | 16 |
| ``"teko: too many `params` instances"`` | 64 over the whole unit |
| ``"teko: too many `ref`/`out` parameters in one unit"`` | 512 |
| ``"teko: too many `ref`/`out` arguments in one unit"`` | 512 |
| `"teko: too many delegate targets"` | 64 (delegate, function) pairs |
| `"teko: too many captures in one lambda"` | 32, summed across the lambdas being read |
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
| `"teko: too many services"` | 32 marked classes |
| ``"teko: too many `inject` sites"`` | 32 |
| ``"teko: too many `scope` blocks"`` | 64, plus one per singleton |
| `"teko: scopes nested too deep"` | 32 open at once |
| `"teko: too many nested service dependencies"` | 32 under construction at once |

---

What a v0.4.0 program cannot express at all, with the message it gets, is
[not-yet.md](not-yet.md).
