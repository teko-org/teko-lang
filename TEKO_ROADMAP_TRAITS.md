# TEKO — ROADMAP (LANGUAGE DESIGN): `trait` (default-method contracts) + structural traits

> **Status:** DESIGN EXPLORATION — **needs a tribunal** · **Created:** 2026-07-02 · **Branch:** `feat/net-connectors`
> **Supersedes** the `#derive`-attribute proposal (rejected in favor of a first-class construct).
>
> A new type-declaration kind — **`trait`** — a contract that can also carry **default method
> implementations**. It is the principled, first-class alternative to a compiler-magic `#derive`
> attribute: shared/derivable behavior becomes *a type you adopt*, using the same conformance mechanism
> the master plan already defines for interfaces. A small set of **compiler-known structural traits**
> (`Eq`/`Ord`/`Hash`/`Clone`/`Default`/`Json`…) provides the field-wise implementations that `#derive`
> would have generated — but expressed as ordinary trait adoption, with no new attribute grammar and no
> runtime reflection.
>
> This is a **language** change (new construct + keyword) → **tribunal required** before implementation.
> Unblocks `teko::encoding` auto-(de)serialize, WEB W8 binding + W9 OpenAPI, CLOUD-NATIVE CN0 config
> binding, and satisfies `Map`'s `Hashable & Eq` key constraint ([[teko-collections-rulings]]).

---

## 0. What `trait` is — and how it fits the already-defined model

The master plan (W10b) already defines: **`interface`** = a PURE signature contract (no default bodies,
no statics, no constructors — [[teko-oop-w10b-design]]); **struct/class methods**, the receiver rule, and
inline conformance (`type S = struct IName { … }`, `type C = class Base & IName { … }`).

**`trait` is a new sibling type-decl kind** (alongside `struct`/`class`/`interface`/`variant`/`enum`/
`flags`) that is an interface **plus default method bodies**. That's the whole idea:

- an **`interface`** *requires* methods (the type must provide all of them);
- a **`trait`** *requires some and provides others* — a type that adopts it gets the provided (default)
  methods for free, and may override any of them.

`trait` does **not** introduce a new way to define behavior — it uses the exact conformance + method +
dispatch mechanism already in the master plan. It adds one thing: the trait declaration can *ship* method
bodies that adopters inherit.

```teko
// interface (already exists) — pure contract, adopter MUST write compare:
type Comparable = interface { fn compare(self, other: Comparable) -> i64 }

// trait (new) — requires compare, PROVIDES lt/gt/eq in terms of it:
type Ordered = trait {
    fn compare(self, other: Ordered) -> i64            // required (no body)
    fn lt(self, other: Ordered) -> bool { self.compare(other) <  0 }   // provided default
    fn gt(self, other: Ordered) -> bool { self.compare(other) >  0 }   // provided default
    fn eq(self, other: Ordered) -> bool { self.compare(other) == 0 }   // provided default
}

// a type adopts the trait like an interface; it writes ONLY the required method:
type Version = struct Ordered {
    major: i64; minor: i64
    pub fn compare(self, other: Version) -> i64 { /* … */ }
    // lt/gt/eq come from the trait — no boilerplate, and overridable if needed
}
```

## 1. The `trait` construct (design)

- **Declaration:** `type Name = trait { <required sigs> ; <provided fns with bodies> }`. A trait with
  zero provided bodies is behaviorally an `interface` (but keep both — see decision #1).
- **Adoption:** identical to interface conformance — `type S = struct Name { … }`,
  `type C = class Base & Name1 & Name2 { … }`. Structs and classes may adopt traits; a type may adopt
  several. The type must supply every *required* method; *provided* methods are inherited and overridable.
- **Default bodies** are written in terms of `self` + the trait's own (required/provided) methods — NOT
  the adopter's private fields (a trait can't see fields it doesn't declare). Field-level behavior comes
  from the **structural traits** (§2), which are compiler-known.
- **As a generic constraint (S6):** `<T: Ordered>` works exactly like an interface constraint
  (monomorphized). Compound constraints compose: `<K: Hashable & Eq>`.
- **As a dynamic value:** like an interface value (data+vtable fat pointer, reusing `tk_closure`) — the
  vtable includes the provided methods. (Same dispatch story as W10b interface-as-value, ROUND 3.)
- **Visibility / receiver:** same rules as interfaces/methods (decl-level visibility; `self` receiver;
  `pub` to satisfy across namespaces).
- **No diamond ambiguity by construction:** if two adopted traits provide a method with the same name, the
  adopter MUST override to disambiguate (compile error otherwise) — no implicit resolution order.

## 2. Structural traits — the `#derive` replacement (compiler-known)

A small, fixed set of **compiler-known traits** whose *provided* implementation is the field-structural
one the compiler synthesizes from the adopter's declaration. Adopting the trait with no override =
"derive." No `#derive` attribute, no user-facing reflection — just adoption:

```teko
type Point = struct Eq & Hash {      // adopt the structural traits → compiler fills eq/hash from fields
    x: i64; y: i64
}
// now Point works as a Map key (Hashable & Eq) with zero hand-written boilerplate

type User = struct Json {            // compiler-known Json trait → to_json/from_json from fields
    id: i64; name: str
}
teko::encoding::json::encode(User { id = 1; name = "ana" })   // "{\"id\":1,\"name\":\"ana\"}"
```

- **Standard structural traits:** `Eq`, `Ord`, `Hash`, `Clone`, `Default` (field-wise), then serialization
  traits `Json` (→ `protobuf`/`Cbor`). Their synthesized bodies use a **compile-time-only field view**
  (compiler-internal; zero runtime metadata — preserves the monomorphization / no-runtime-reflection law,
  Law M.0). This is the ONLY compiler-magic part, and it hides behind ordinary trait adoption.
- **Customization = override (the master-plan way).** Need a renamed JSON field or custom equality? Adopt
  the trait and **write that method by hand** — the structural default steps aside for any method the type
  provides itself. (Field-level tweaks like `json rename` are a later, optional refinement; the baseline
  is "override the whole method," which needs no new syntax.)

## 3. Units (contingent on the tribunal ratifying `trait`)

**▪ TR0 — the `trait` construct.** **Deps:** interfaces (W10b.IF ✅). **Files:** `lexer` (keyword `trait`),
`parser`/`ast` (`TraitBody` decl kind, provided-method bodies), `checker` (conformance = required-methods
check; inject provided methods into the adopter; override + diamond rules), `codegen`+`vm` (emit provided
methods; vtable for dynamic use). Both twins. **Verify:** `.tkt` — a type adopts a trait, gets defaults,
overrides one; diamond → compile error; VM == native; gen-2 == gen-3.

**▪ TR1 — trait as generic constraint + dynamic value.** **Deps:** TR0, S6 constraints, W10b.IF ROUND 3
(interface-as-value dispatch). `<T: Trait>` monomorphized; a `Trait`-typed value dispatches through the
vtable (provided methods included). **Verify:** `.tkt` static + dynamic dispatch.

**▪ TR2 — compile-time field view (internal).** **Deps:** checker. The compiler-internal enumeration of an
adopter's fields/types/doc used to synthesize structural-trait bodies. No surface syntax, no runtime value.
**Verify:** internal — the view of a fixture struct matches.

**▪ TR3 — standard structural traits.** **Deps:** TR0, TR2. `Eq`, `Ord`, `Hash`, `Clone`, `Default`
(field-wise synthesized defaults; overridable). Immediately unblocks `Map` keys (`Hashable & Eq`).
**Verify:** `.tkt` per trait; a derived `eq`/`hash` == a hand-written one.

**▪ TR4 — serialization traits.** **Deps:** TR3, `teko::encoding` (JSON first → protobuf/CBOR). `Json`
(and siblings): synthesized `to_json`/`from_json` from fields; the encoding/web unblocker. **Verify:**
`.tkt` roundtrip against fixtures.

**▪ TR5 — schema trait (T2).** **Deps:** TR4, doc-comments. A trait deriving an OpenAPI/JSON-Schema
description of a type (feeds WEB W9); reuses doc-comments for descriptions. **Verify:** `.tkt` snapshot.

## 4. Dependency graph + tiers

```
interface (W10b.IF ✅) ── TR0 trait construct ─┬─ TR1 constraint + dynamic
                                               ├─ TR2 field view (internal) ── TR3 Eq/Ord/Hash/Clone/Default
                                               │                                     └─ TR4 Json (→ protobuf/cbor)  ← encoding/web unblocker
                                               └─                                        └─ TR5 schema (OpenAPI) [T2]
```

**Tiers.** **T1 (unblock the stack):** TR0, TR1, TR2, TR3, TR4(Json). **T2:** TR4(protobuf/CBOR), TR5.
General compile-time evaluation ("meta-code execution") remains a **separate, later, deferred** proposal —
`trait` + structural traits cover the encoding/web needs without it.

## 5. Open decisions — TRIBUNAL REQUIRED (new construct + keyword)

1. **`trait` vs `interface` coexistence:** keep BOTH — `interface` = pure contract (as shipped, W10b.IF),
   `trait` = contract + provided behavior (rec) — vs collapsing into one construct. *(rec: coexist;
   interface already shipped and the pure-contract vs behavior-carrying distinction is meaningful.)*
2. **Self-type in default bodies:** a trait default refers to the adopter as the trait name (per the
   W10b.IF "no `Self` keyword" ruling) vs introducing a `Self` type. *(rec: reuse the interface rule — no
   `Self`.)*
3. **Structural traits are compiler-known (closed set) first** (`Eq`/`Ord`/`Hash`/`Clone`/`Default`/`Json`)
   vs an open registration point. *(rec: closed set first.)*
4. **Field view boundary:** confirm the structural-trait synthesis is **compile-time only, zero runtime
   metadata** (rec — preserves the no-runtime-reflection law). No `typeof`/`TypeInfo` runtime value.
5. **Field-level customization** (JSON rename/skip): override the whole method first (no new syntax, rec) vs
   add per-field attributes later.
6. **Diamond rule:** same-named provided methods from two traits → mandatory override (rec) vs an ordering.
7. **Scope now:** ratify `trait` + TR0–TR4 as committed; general comptime explicitly a future separate
   proposal.
