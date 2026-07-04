// src/checker/collect.h — pass 1: collect top-level names (forward references).
#ifndef TK_CHECK_COLLECT_H
#define TK_CHECK_COLLECT_H
#include "resolve.h"
#include "tast.h"   // tk_tprogram — needed by tk_type_table_of (C7.12)

typedef struct { tk_type_table types; tk_env env; } tk_collected;
TK_RESULT(tk_collected, tk_collected_result);

tk_collected_result tk_collect(tk_program program);

// (W10b.CLASS increment 2) inheritance helpers — shared with expr.c/typer.c (a class's
// instance-method dispatch, field access, and construction all need the EFFECTIVE, base-
// inherited shape, not just what's textually declared on the class itself).
typedef struct { bool ok; union { tk_class_body value; tk_error error; } as; } tk_classbody_result;
typedef struct { bool ok; union { struct { tk_field *ptr; size_t len; } value; tk_error error; } as; } tk_fieldsvec_result;
typedef struct { bool ok; union { struct { tk_function *ptr; size_t len; } value; tk_error error; } as; } tk_methodsvec_result;
tk_classbody_result  tk_find_class_body(tk_str name, tk_type_table table);
tk_fieldsvec_result  tk_effective_class_fields(tk_class_body cb, tk_type_table table);
tk_methodsvec_result tk_effective_class_methods(tk_class_body cb, tk_type_table table);

// (W10b.D3) dynamic dispatch — the interface-value surface. `tk_iface_methods_by_name` returns an
// interface's EFFECTIVE methods (extends-transitive first, then own); a method's INDEX in this
// list IS its vtable slot (checker typing, codegen vtables, and the VM dispatch all agree on it).
// `tk_type_conforms_to` is the NOMINAL upcast/constraint gate (declared `implements` only, through
// the class base chain and interface `extends`). `tk_method_func_type` (A1) is a method signature
// as a FuncType with the untyped receiver resolved to Named{owner} — shared with expr.c/typer.c
// for interface-receiver call typing.
tk_methodsvec_result tk_iface_methods_by_name(tk_str iface, tk_type_table table);
bool tk_is_interface_name(tk_str name, tk_type_table table);
bool tk_type_conforms_to(tk_str name, tk_str iface, tk_type_table table);
tk_type_result tk_method_func_type(tk_function f, tk_str struct_name, tk_type_table table, tk_str ref_ns);   // (#109 W1) ref_ns = the method's declaring namespace

// (#98) a POLYMORPHIC BASE class's VIRTUAL METHOD TABLE — its effective methods (the same slot
// order codegen's `tk_vt_<Sub>_<Base>` build reads); `tk_base_vtable_slot` returns the slot of
// `method` (or !ok when it isn't an effective base method → a subclass-only, direct-dispatched
// method). Shared by the checker (base-typed dispatch) and codegen (vtable emission).
typedef struct { bool ok; union { uint32_t value; tk_error error; } as; } tk_slot_result;
tk_methodsvec_result tk_base_vtable_methods(tk_str base, tk_type_table table);
tk_slot_result       tk_base_vtable_slot(tk_str base, tk_str method, tk_type_table table);

// (W10b.CLASS residual — intern visibility) a member's REACH: which class's OWN code declared
// it, plus its vis/is_intern AT that declaration (an override "moves" the declaration to the
// overriding class). Shared with expr.c/typer.c.
typedef struct { tk_str declaring_class; tk_visibility vis; bool is_intern; } tk_member_owner;
typedef struct { bool ok; union { tk_member_owner value; tk_error error; } as; } tk_member_owner_result;
bool tk_is_subclass_of(tk_str class_name, tk_str ancestor_name, tk_type_table table);
tk_member_owner_result tk_find_field_owner(tk_str class_name, tk_type_table table, tk_str field_name);
tk_member_owner_result tk_find_method_owner(tk_str class_name, tk_type_table table, tk_str method_name);
bool tk_member_accessible(tk_member_owner owner, tk_str accessor_type, tk_type_table table);
tk_str tk_class_name_from_method_ns(tk_str ns);

// (TR0, 2026-07-02) the trait-derivation FOLD — runs BEFORE collect/typing (tk_type_program's
// first step): rewrites each struct/class TypeDecl whose `&`-list names traits (trait fields +
// bodied methods fold in; trait names leave `implements`; bodyless requirements checked against
// the folded table). NO-OP when the program declares no trait. Mirrors collect.tks::fold_traits.
TK_RESULT(tk_program, tk_fold_traits_result);
tk_fold_traits_result tk_fold_traits(tk_program program);
// (#152) base_name -> qualified, order-independent base-chain hops. Mirror collect.tks.
tk_program tk_canon_class_bases(tk_program program);

// C7.12 — reconstruct a TypeTable from a typed program's pass-through TypeDecl items.
// Used by the package backend in driver.c (which has a tk_tprogram, not a tk_program).
// The namespace is set to "" — sufficient for resolve_type; W-vis-enforce is not called.
tk_type_table tk_type_table_of(tk_tprogram prog);

// C7.10 — pre-seed the collection environment from an already-typed dep TProgram.
// Inserts the dep's type declarations into `table` and its function signatures into `env`.
// Returns the updated {types, env} — the caller passes these into the project's collect().
// The dep's bodies are NOT re-typed (they were verified when the dep was built).
tk_collected_result tk_seed_from_dep(tk_tprogram dep, tk_type_table table, tk_env env);

// C7.10 — collect with a pre-seeded (table, env). Adds the project's type decls + function
// signatures on top of the seed. Mirrors collect.tks::collect_with_seed.
tk_collected_result tk_collect_with_seed(tk_program program, tk_collected seed);

#endif // TK_CHECK_COLLECT_H
