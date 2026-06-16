#ifndef TEKO_VTABLE_H
#define TEKO_VTABLE_H

// Phase 15 (15.B) — STATIC method-dispatch table backing abstract-class / trait dynamic dispatch.
// A compile-time-populated 2-D table: vtable[type_id][method_id] = routine table slot. The
// compiler assigns each concrete class a dense `type_id` and each trait/abstract method a dense
// `method_id`, then emits one teko_vtable_set per (implementing type, method) at program start;
// a dynamic call site `ref.method()` does teko_vtable_get(type_id, method_id) -> slot and dispatches
// the routine slot via the Phase-15.A synchronous call (OP_CALL_FUNC).
//
// ZERO RUNTIME REFLECTION: the (type_id, method_id) -> slot mapping is FIXED at compile time — this
// is only the storage + an O(1) lookup, not an introspectable attribute/type registry. It is the
// classic static vtable (C++ vptr / Go itab) model, expressed as a runtime table the SAME way every
// Phase-14 channel runtime is a portable C source of truth (native teko_rt_vtable_* + wasm reactor).
//
// `to_string` (the Phase-16 casting hook) is just another method in this table: a class's/trait's
// `to_string` gets a method_id and a vtable entry, so Phase 16 resolves + auto-invokes it through
// the same path as any other dynamically-dispatched method.

#define TEKO_VT_MAX_TYPES   256 // max concrete classes participating in dynamic dispatch
#define TEKO_VT_MAX_METHODS 64  // max distinct dynamically-dispatched method ids

// Register a vtable entry (compile-time content). Out-of-range ids are ignored (defensive).
void teko_vtable_set(long type_id, long method_id, long slot);

// Look up the routine slot for (type_id, method_id). Returns the registered slot, or -1 if the
// pair was never set (the compiler always populates a dispatched pair; -1 is a hard-fail sentinel).
long teko_vtable_get(long type_id, long method_id);

// Reset the whole table to "unset" (-1). Called once before the compiler's population sequence so a
// never-set lookup is a definite -1 rather than a stale/zero slot. Idempotent.
void teko_vtable_reset(void);

#endif // TEKO_VTABLE_H
