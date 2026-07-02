# TEKO — ROADMAP (LANGUAGE DESIGN): compile-time reflection + `derive` (`W-DERIVE`)

> **Status:** DESIGN EXPLORATION — **needs a tribunal** · **Created:** 2026-07-02 · **Branch:** `feat/net-connectors`
>
> This is a **language** proposal, not a library — it must clear the Constitution (Laws M.0–M.5) via a
> tribunal before any implementation. It is the hidden multiplier behind the encoding/web/config roadmaps:
> **bounded compile-time reflection** + a **`derive` mechanism** that auto-generates per-type code (JSON/
> protobuf/CBOR (de)serialization, request binding, validation, OpenAPI schema) from a struct's own
> declaration — instead of hand-writing a codec per type.
>
> Unblocks: `teko::encoding` auto-(de)serialize, WEB W8 typed binding + W9 OpenAPI, CLOUD-NATIVE CN0
> config-struct binding. Reuses the already-parsed doc-comments (`has_doc`/`doc`). Ties to the "meta-code
> execution" the old project description promised but never shipped.

---

## 0. The problem, concretely

Today, to send a struct as JSON you must write a `to_json`/`from_json` by hand for every field of every
type — and keep it in sync as the struct changes. Multiply by JSON + protobuf + CBOR + form-binding +
OpenAPI schema and it's O(types × formats) of boilerplate that drifts out of sync. Every batteries-included
language solves this with *some* form of compile-time introspection (Rust `derive`, Zig `comptime` +
`@typeInfo`, Go `reflect`+codegen, C# source generators).

**The constraint that shapes Teko's answer:** the memory model is **monomorphization, no runtime
reflection** — and that is a *feature* (soundness, `all-native`, no metadata bloat, Law M.0). So Teko's
answer must be **compile-time only**: the field/type information exists during checking/codegen and is
*consumed to generate code*, leaving **zero reflection metadata in the binary**.

---

## 1. Three layers (each a decision point)

### Layer 1 — bounded compile-time reflection (the minimum)
A compile-time-only view of a type's structure, available to the code generator:
- for a `struct`/`class`: its fields (name, type, visibility, doc-comment, attributes);
- for a `variant`/`enum`: its cases;
- primitive/optional/slice shape.

This is **NOT** a runtime `TypeInfo` value the program can inspect — it is information the **compiler**
uses while lowering a `derive`. Think "the checker can enumerate fields," not "the program calls
`typeof(x).fields`." Keeps the no-runtime-reflection law intact.

### Layer 2 — the `derive` mechanism (the deliverable)
A way to say "generate the standard implementation of interface `I` for this type from its structure."
**Options to ratify:**
- **(A) `#derive(...)` attribute** on a type: `#derive(Json, Eq, Hash)` → the compiler synthesizes the
  `to_json`/`from_json` (and `eq`/`hash`) impls by walking the fields (Layer 1). Rust-style. Each derivable
  interface has a **compiler-known generator**. Smallest surface; closed set initially (Json/Eq/Hash/Ord/
  Clone/Default), extensible later.
- **(B) user-written comptime generators** — a `comptime fn` that receives the Layer-1 type view and emits
  code (Zig/macro-style). Far more powerful (users derive their own), far larger language surface + a whole
  comptime evaluation model. 
- **(C) external codegen** — a `teko generate` build step reading types → writing `.tks` (Go-style). No
  language change, but a clunky two-step build and generated files in the tree.

**Recommendation:** **(A) a closed `#derive` set first** (fits the existing attribute system —
`#test`/`#inject`/`#os` — and the interface system; bounded, law-clean, immediately unblocks encoding/web),
with **(B) full comptime as a SEPARATE, later, larger proposal** if the demand for user-defined derives is
proven. (C) only as a stopgap if (A) slips.

### Layer 3 — general `comptime` evaluation (the stretch goal)
Const-folding + `comptime`-evaluated functions + `comptime` parameters (compile-time constants, sizes,
lookup-table generation). This is the "meta-code execution" differentiator, but it is a **major** language
undertaking (a compile-time interpreter — which the VM could partly serve). **Explicitly deferred**: not
required for the encoding/web unblocking, which Layer 1 + Layer 2(A) fully cover. Kept here so the `derive`
design doesn't accidentally foreclose it.

---

## 2. Units (contingent on the tribunal choosing Layer 2(A))

**▪ DV0 — compile-time type view (Layer 1).** **Deps:** checker. Expose, to the codegen phase, an
enumeration of a checked type's fields/cases with name/type/visibility/doc/attributes. Compiler-internal;
no surface syntax, no runtime value. **Verify:** `.tkt`/internal — the view of a fixture struct matches.

**▪ DV1 — `#derive(...)` attribute + the generator framework.** **Deps:** DV0, attributes, interfaces
(W10b.IF ✅). Parse `#derive(...)` on a type; a registry mapping each derivable interface → a
compiler generator that emits the impl by walking DV0. **Verify:** a derived impl type-checks + runs
== a hand-written one; both engines; gen-2 == gen-3.

**▪ DV2 — the standard derives.** **Deps:** DV1. `Eq`, `Ord`, `Hash`, `Clone`, `Default` (structural,
field-wise). Immediately useful project-wide (e.g. `Map` keys need `Hashable & Eq` —
[[teko-collections-rulings]] — which derive can now satisfy). **Verify:** `.tkt` per derive.

**▪ DV3 — serialization derives.** **Deps:** DV1, `teko::encoding` (JSON first, then protobuf/CBOR).
`#derive(Json)` → `to_json`/`from_json`; field-rename/skip/optional via field attributes
(`#json(name="…")`). **This is the encoding/web unblocker.** **Verify:** `.tkt` roundtrip + attribute
behavior against fixtures.

**▪ DV4 — schema derive (T2).** **Deps:** DV3, doc-comments. Derive an OpenAPI/JSON-Schema description of a
type (feeds WEB W9). Reuses doc-comments for descriptions. **Verify:** `.tkt` schema snapshot.

## 3. Dependency graph + tiers

```
checker ── DV0 type-view ── DV1 #derive framework ─┬─ DV2 std derives (Eq/Ord/Hash/Clone/Default)
                                                   ├─ DV3 serialization (Json→protobuf/CBOR)  ← unblocks encoding/web
                                                   └─ DV4 schema (OpenAPI)  [T2]
Layer 3 general comptime = SEPARATE later proposal (deferred)
```

**Tiers.** **T1 (unblock the stack):** DV0, DV1, DV2, DV3(Json). **T2:** DV3(protobuf/CBOR), DV4 schema.
**Deferred:** Layer 2(B) user comptime derives, Layer 3 general comptime.

## 4. Open decisions — TRIBUNAL REQUIRED (this is a language change)

1. **Layer 2 mechanism:** `#derive` closed set (rec) vs user comptime generators vs external codegen.
2. **Layer 1 boundary:** confirm reflection is **compile-time only, zero runtime metadata** (rec — preserves
   the monomorphization/no-runtime-reflection law). No `typeof`/`TypeInfo` runtime value.
3. **Derivable interfaces closed vs open:** a fixed compiler-known set first (rec) vs a plugin/registration
   point for libraries.
4. **Field attributes:** how types customize a derive (`#json(name=…, skip)`) — attribute grammar on fields
   (structs currently have plain fields; this adds per-field attributes).
5. **Interaction with generics:** deriving on a generic type + monomorphization order — the generator must
   run per-instantiation. Confirm it composes with the S4/S6 mono pass.
6. **Scope now:** ratify Layer 1 + Layer 2(A) + DV0–DV3 as the committed scope; Layer 3 (general comptime /
   "meta-code execution") explicitly a **future separate proposal**, not this deliverable.
