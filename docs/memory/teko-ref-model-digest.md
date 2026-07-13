---
section: type-system
created: 2026-07-13
source: docs/design/ref-transparent-model.md, docs/design/marshall-spec.md, DECISION_LOG.md (D31)
---

# Teko Ref Model Ruling Set (2026-07-13)

**Tripartite + class exception:** values (structs, primitives, inline variants) are cheap-to-copy; references (classes, slices, optionals) are capability-passing. Classes are reference types. Plain refs `T` carry referent into scope; optionals `T?` sink to value (? → unwrap → T).

**Mandatory ref modifier everywhere:** functions receiving class args use `ref` (receiver implicit; params explicit at call); same for stored borrows in structs (ref fields = borrowed pointers). `Ref<T>` internal (no user surface); capability-import model for Wasm.

**Ref grafia:** everywhere in surface (param + field decls); `Ref<>` in internal C (VM/codegen); cap-2 safety invariant (two owning refs max per heap allocation; spine infers, checker validates); unbounded unsafe (explicit `unsafe` type modifier breaks the cap).

**Never-null refs:** `T` is guaranteed non-null (NULL ≠ absent); `T?` is the absence vessel. Bare `?` is narrowing (T → T?); methods return T or T? explicitly.

**No narrowing through refs:** a ref to T? does not "become" T (no view-type). Stored borrow of optional field is rejected unless field + scope have matching lifetime (spine bounds).

**Sigils unsafe-only:** `&`, `*` as untyped pointers only in `unsafe` code; safe code uses typed `T`, `ref T`, `T?`, `ptr<T>` (the last two explicit at unsafe boundary).

**No safe immutable borrow:** safe borrows are mutable (receiver/parameter); immutable requires `unsafe const ref` or value-copy. Immutability is not a safety property; it's an annotation (future).

**Marshall operand + wrapper laws (D31b):** struct fields that embed wrapper/wrapper-descent match correctly (no descent bug; only direct field match). Return values that are wrappers auto-unwrap (T as result of constructor-like fn).

**Swap = value-swap:** ref-to-T fields swap by value (deep copy of T), not pointer swap; no reference aliasing via swap.
