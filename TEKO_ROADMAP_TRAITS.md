# TEKO — ROADMAP (LANGUAGE DESIGN): `trait` (struct-shaped, derivable mixin) + structural traits

> **Status:** DESIGN — **RATIFIED (user 2026-07-02, no tribunal needed)** · **Created:** 2026-07-02 · **Branch:** `feat/net-connectors`
> **Supersedes** the `#derive`-attribute proposal (rejected in favor of a first-class construct).
>
> A new type-declaration kind — **`trait`** — **written like a struct** (it may hold fields and methods,
> with bodies), but a **special non-instantiable type that any `struct` or `class` can DERIVE**. Deriving a
> trait folds its fields + methods into the deriver (composition/mixin), reusing the field-flattening the
> class inheritance already does (W10b.CLASS increment 2) plus the method model (W10b.A1). A struct/class
> may derive several traits.
>
> A small set of **compiler-known structural traits** (`Eq`/`Ord`/`Hash`/`Clone`/`Default`/`Json`…) are
> the special case: they hold no fields, and their method bodies are synthesized by the compiler from the
> **deriver's** fields — so deriving them IS "derive" in the usual sense, with no attribute grammar and no
> runtime reflection.
>
> Unblocks `teko::encoding` auto-(de)serialize, WEB W8 binding + W9 OpenAPI, CLOUD-NATIVE CN0 config
> binding, and satisfies `Map`'s `Hashable & Eq` key constraint ([[teko-collections-rulings]]).

---

## 0. What `trait` is — and how it fits the already-defined model

The master plan (W10b) already defines: **`interface`** = a PURE signature contract (no fields, no
bodies); **`class`** = the OOP unit (single base + field-flattening inheritance, reference type);
**`struct`** = value data + methods; the receiver rule; inline conformance.

**`trait` is a new sibling type-decl kind** that sits between struct and interface:

- an **`interface`** says *what a type must provide* (signatures only) — a contract;
- a **`trait`** *provides reusable implementation* — a **struct-shaped bundle of fields + methods (with
  bodies)** that a `struct` or `class` **derives** to gain those members. It is **not instantiable on its
  own** (like an abstract template); it exists only to be derived.

```teko
// declared LIKE A STRUCT — fields + methods with bodies:
type Timestamped = trait {
    created_at: i64                       // a field the deriver gains
    updated_at: i64
    fn touch(self: Ref<Timestamped>) -> void { self.updated_at = now() }   // a method with a body
}

// any struct or class DERIVES it → gains created_at, updated_at, and touch():
type Post = struct Timestamped {
    title: str
    body: str
}
// Post now has title, body, created_at, updated_at + touch() — folded in (field-flattening)
```

`trait` does **not** invent a new dispatch/method model — it reuses W10b: methods follow the receiver
rule, deriving reuses the class field-flattening, and a trait is usable as a generic constraint (S6) and
(when it has bodyless requirements) as a dynamic vtable value.

## 1. The `trait` construct (design — settled)

- **Declaration:** `type Name = trait { <fields> ; <methods, with or without bodies> }` — the body is
  written exactly like a struct's (fields + methods). A method WITHOUT a body is a *requirement* the
  deriver must satisfy; a method WITH a body is *provided* (and overridable). Fields are provided members.
- **Deriving:** a `struct`/`class` derives one or more traits by listing them (same conformance surface as
  interfaces/base): `type Post = struct Timestamped & Tagged { … }`, `type Svc = class Base & Loggable
  { … }`. Deriving folds in the trait's fields (field-flattening, reusing W10b.CLASS increment 2) and its
  methods; the deriver may **override** any provided method and must satisfy any bodyless requirement.
- **Not instantiable:** a `trait` value is never constructed directly — only derived. (It may still be
  used as a `<T: Trait>` constraint and, for its bodyless requirements, as a dynamic interface-like value.)
- **Multiple derivation — exactly like implementing multiple interfaces.** A struct/class may derive any
  number of traits (`struct T1 & T2 & T3`), the same way it can implement several interfaces. The ONLY
  thing to resolve is **method conflicts**, and there is exactly one rule for it (consistent with Teko's
  **no-overloading / override-only** ruling from W10b.CLASS): if two derived traits provide a method with
  the **same name**, the deriver **must provide its own method of that name** (an override) to resolve it —
  a compile error otherwise, never an implicit resolution order. A method the deriver defines itself always
  wins over a trait-provided one (it IS the override). A bodyless requirement shared by several traits is
  satisfied once. (**Fields** cannot be overridden, so a field-name collision across derived traits — or
  with the deriver's own fields — is simply a compile error; rename or don't co-derive.)
- **Receiver / visibility:** methods follow the W10b receiver rule (`self` value, `self: Ref<T>` to
  mutate); a trait's fields obey the deriver's kind (struct = public value data; class = encapsulated).
- **As a constraint (S6):** `<T: Timestamped>` monomorphized, exactly like an interface constraint;
  compound `<K: Hashable & Eq>` composes.

## 2. Structural traits — the `#derive` replacement (compiler-known)

The special case: **field-less** compiler-known traits whose *provided* method bodies are synthesized
from the **deriver's** fields. Deriving one, with no override, is "derive":

```teko
type Point = struct Eq & Hash {      // derive the structural traits → compiler fills eq/hash from fields
    x: i64; y: i64
}
// Point is now a valid Map key (Hashable & Eq) — zero hand-written boilerplate

type User = struct Json {            // compiler-known Json trait → to_json/from_json from fields
    id: i64; name: str
}
teko::encoding::json::encode(User { id = 1; name = "ana" })   // "{\"id\":1,\"name\":\"ana\"}"
```

- **Standard structural traits:** `Eq`, `Ord`, `Hash`, `Clone`, `Default` (field-wise), then serialization
  traits `Json` (→ `protobuf`/`Cbor`). Their synthesized bodies use a **compile-time-only field view**
  (compiler-internal; zero runtime metadata — preserves the monomorphization / no-runtime-reflection law,
  Law M.0). This is the only compiler-magic part, and it hides behind ordinary trait derivation.
- **Customization = override (the master-plan way).** Need a renamed JSON field or custom equality? Derive
  the trait and **write that method by hand** — the structural default steps aside for any method the
  deriver provides itself. (Field-level tweaks like `json rename` are a later optional refinement.)

## 3. Units

**▪ TR0 — the `trait` construct.** **Deps:** interfaces (W10b.IF ✅), class field-flattening + methods
(W10b.CLASS/A1 ✅). **Files:** `lexer` (keyword `trait`), `parser`/`ast` (`TraitBody` decl kind: fields +
methods, bodies optional), `checker` (derive = fold fields (flattening) + methods; requirement-satisfaction;
override + field/method collision rules; reject direct instantiation), `codegen`+`vm` (emit folded members;
vtable for bodyless-requirement dynamic use). Both twins. **Verify:** `.tkt` — a struct and a class each
derive a trait (gain fields + methods), override one, satisfy a requirement; collision → compile error;
`trait` instantiation → compile error; VM == native; gen-2 == gen-3.

**▪ TR1 — trait as generic constraint + dynamic value.** **Deps:** TR0, S6 (W11 ✅), W10b.D3 dynamic
dispatch. `<T: Trait>` monomorphized; a trait's bodyless-requirement surface usable as a vtable value.
**Verify:** `.tkt` static + dynamic dispatch.

**▪ TR2 — compile-time field view (internal).** **Deps:** checker. The compiler-internal enumeration of a
deriver's fields/types/doc used to synthesize structural-trait bodies. No surface syntax, no runtime value.
**Verify:** internal — the view of a fixture struct matches.

**▪ TR3 — standard structural traits.** **Deps:** TR0, TR2. `Eq`, `Ord`, `Hash`, `Clone`, `Default`
(field-wise synthesized; overridable). Immediately unblocks `Map` keys (`Hashable & Eq`). **Verify:**
`.tkt` per trait; a derived `eq`/`hash` == a hand-written one.

**▪ TR4 — serialization traits.** **Deps:** TR3, `teko::encoding` (JSON first → protobuf/CBOR). `Json`
(+ siblings): synthesized `to_json`/`from_json`; the encoding/web unblocker. **Verify:** `.tkt` roundtrip.

**▪ TR5 — schema trait (T2).** **Deps:** TR4, doc-comments. Derive an OpenAPI/JSON-Schema description of a
type (feeds WEB W9); reuses doc-comments for descriptions. **Verify:** `.tkt` snapshot.

## 4. Dependency graph + tiers

```
interface + class field-flattening (W10b ✅) ── TR0 trait construct ─┬─ TR1 constraint + dynamic
                                                                     ├─ TR2 field view (internal) ─ TR3 Eq/Ord/Hash/Clone/Default
                                                                     │                                    └─ TR4 Json (→ protobuf/cbor)  ← encoding/web unblocker
                                                                     └─                                       └─ TR5 schema (OpenAPI) [T2]
```

**Tiers.** **T1 (unblock the stack):** TR0, TR1, TR2, TR3, TR4(Json). **T2:** TR4(protobuf/CBOR), TR5.
General compile-time evaluation ("meta-code execution") stays a **separate, later, deferred** proposal —
`trait` + structural traits cover the encoding/web needs without it.

## 5. Settled rulings (user 2026-07-02) + small opens

**Settled:** trait is a first-class construct, **no tribunal**; body is **struct-shaped** (fields +
methods, bodies optional); a **non-instantiable** special type; **any struct or class derives it**
(multiple allowed), folding in members via field-flattening; `interface` (pure contract) and `trait`
(derivable implementation) COEXIST; structural derives are compiler-known and compile-time-only (no runtime
reflection); customization = override; general comptime stays deferred.

**Small opens (design detail, not blocking):**
1. **Derive syntax:** reuse the interface `&`-list (`struct T1 & T2`) — rec — vs a distinct `derives`
   keyword. *(rec: reuse the `&`-list; it already means "adopt this named contract/bundle".)*
2. **Self-type in trait bodies:** refer to the deriver as the trait name (per the W10b "no `Self`" rule) —
   rec — vs a `Self` type.
3. **Trait fields' mutability/visibility** when derived into a class vs a struct — confirm they obey the
   deriver's kind (struct = public value; class = encapsulated), rec.
4. **Structural set closed** (`Eq/Ord/Hash/Clone/Default/Json`) first — rec — vs open registration.
