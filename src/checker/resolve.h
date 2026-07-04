// src/checker/resolve.h — type resolution + the user-type registry.
#ifndef TK_CHECK_RESOLVE_H
#define TK_CHECK_RESOLVE_H

#include "type.h"
#include "scope.h"
#include "../parser/ast.h"   // tk_type_expr, tk_type_decl, tk_path, … (the parser's AST)

// A user-type registry entry: the type's name, the NAMESPACE it was declared in (A3
// provenance — for W-vis-enforce cross-namespace checks), its VISIBILITY (private/pub/exp),
// and the declaration itself. namespace/vis drive the module-system enforcement pass.
typedef struct { tk_str name; tk_str namespace; tk_visibility vis; tk_type_decl decl; } tk_type_reg;
TK_LIST(tk_type_reg, tk_type_table);

TK_RESULT(tk_type_decl, tk_decl_result);   // TypeDecl | error

// Build a checker error "<msg>: <name>" — names the offending symbol (M.3 — clarify, never vague).
tk_error tk_error_named(const char *msg, tk_str name);
// (#121) Byte-identical twin builders — weave a name INTO the sentence (matching the .tks
// `$"…{x}…"` interpolation) instead of appending "…: name". One / two woven names.
tk_error tk_error_woven1(const char *a, tk_str n1, const char *b);
tk_error tk_error_woven2(const char *a, tk_str n1, const char *b, tk_str n2, const char *c);

// (#109 W1) `ref_ns` = the REFERENCING namespace (the namespace of the code that wrote this type
// reference). Reserved for the R0-R5 resolution rules (W2); the W1 body IGNORES it (byte-identical).
tk_decl_result tk_type_table_find(tk_type_table table, tk_str name, tk_str ref_ns);
// (#109 W2) the declaring namespace of a type by its (resolved) name ("" if unknown) — so a field /
// method-sig source annotation resolves in the type's OWN namespace. Shared with collect.c, typer.c, match.c.
tk_str type_ns_of(tk_type_table table, tk_str name);
// (#109 W3) canonical-name helpers — the semantic Named.name is now "ns::Name". Shared across the
// checker/codegen/vm twins. qualify: (ns,name)→"ns::Name" (bare at root); name_last_segment: the bare
// tail after the final "::"; name_qualifier: the ns before it; mangle_ns_frag: "::"→"__" symbol fragment.
tk_str tk_qualify(tk_str ns, tk_str name);
bool tk_qualify_eq(tk_str ns, tk_str bare, tk_str name);   // (#148) qualify(ns,bare)==name without building the string
tk_str tk_name_last_segment(tk_str name);
tk_str tk_name_qualifier(tk_str name);
// (#109 order fix) resolve a SOURCE-WRITTEN type name string ("Reader" / "io::Reader") by the
// R0-R5 rules from ref_ns; tk_resolved_name_ns mirrors the winning namespace. (resolve.tks twins.)
tk_decl_result tk_resolve_name_ref(tk_str name, tk_type_table table, tk_str ref_ns);
tk_str tk_resolved_name_ns(tk_str name, tk_type_table table, tk_str ref_ns);
tk_str tk_mangle_ns_frag(tk_str name);
tk_type_result tk_resolve_type(tk_type_expr te, tk_type_table table, tk_str ref_ns);
// (S4) extend the table with generic type-params as OPAQUE nominal types (see resolve.c). Used by
// collect (func sigs), check_modules (vis check), and typer (bodies). Empty → table unchanged.
tk_type_table tk_type_param_table(tk_str *type_params, size_t n_type_params, tk_constraint_expr *type_constraints, tk_str ns, tk_type_table table);

// (S4) generics inference. tk_subst = a type-param→concrete binding (parallel arrays); `params` is
// the type-param universe, `names`/`types` the bindings. (resolve.tks: Subst / subst_type / unify.)
typedef struct { tk_str *params; size_t n_params; tk_str *names; tk_type *types; size_t n_bind; } tk_subst;
TK_RESULT(tk_subst, tk_subst_result);
tk_type tk_subst_type(tk_type t, tk_subst s);                                       // substitute bound type-params
tk_subst_result tk_unify(tk_type pattern, tk_type arg, tk_subst s, tk_type_table table);  // bind type-params from args
void tk_collect_sig_type_params(tk_type t, tk_type_table table, tk_str **names, size_t *n);  // type-param names in a sig
bool tk_is_type_param(tk_str name, tk_str *params, size_t np);                               // membership in a name list
tk_type_result resolve_named(tk_path path, tk_type_table table, tk_str ref_ns);   // shared with match.c (C7); (#109 W1) ref_ns threaded
// B.14 — a NAMED type that refers to a `variant` decl → its expanded TK_TYPE_VARIANT (members
// stay NAMED, so it terminates); anything else is returned unchanged. Lets assignability and
// exhaustiveness see a named variant's cases without changing the nominal representation.
tk_type tk_expand_variant(tk_type t, tk_type_table table);

// (#41-followon) Value-position widening — the single source of truth shared by the
// return/trailing-value check and the match/`if` arm join. `tk_widens_into`: does `from` widen
// into `to` (equality, T→T?, a variant member → the variant)? `tk_type_join`: the least-upper-
// bound of two arm/branch types — the wider one when either widens into the other (so `error`
// and `Type | error` join to `Type | error`); false when incompatible (out set only on success).
bool tk_widens_into(tk_type from, tk_type to, tk_type_table table);
bool tk_type_join(tk_type a, tk_type b, tk_type_table table, tk_type *out);

// (W10b.D3) nominal contract conformance — the interface-value upcast gate (tk_widens_into's
// class→interface rule) and the `<T: I>` constraint gate (monomorph). Kind probes are by decl
// tag; `tk_type_conforms_to` walks declared `implements` through the class base chain and the
// contract's `extends` closure (declared conformance only — nominal, closed-world).
bool tk_is_interface_name(tk_str name, tk_type_table table);
bool tk_is_class_name(tk_str name, tk_type_table table);
bool tk_is_trait_name(tk_str name, tk_type_table table);   // (TR0) decl-tag probe for the trait honest stops
bool tk_type_conforms_to(tk_str name, tk_str iface, tk_type_table table);
bool tk_is_polymorphic_base(tk_str name, tk_type_table table);   // (#98) a non-sealed class — may be a base
bool tk_subclass_reaches(tk_str sub, tk_str want, tk_type_table table, int depth);   // (#98) Sub→Base ancestor walk
// (#83) does `t` carry a SENTINEL slice/optional (element/inner == NULL) anywhere, incl. nested
// (`[]NULL`, `[][]NULL`, `(NULL)?`, …)? Shared by tk_type_join's concrete-preference tie-break and
// by array-literal element retyping (expr.c::type_array_lit) once the joined element is concrete.
bool tk_type_has_sentinel(const tk_type *t);

// (C1.8) RENDER a semantic type to a human string for diagnostics ("expected X, found Y"):
//   prims → "i32"/"u64"/"f64"/"bool"/… (the surface spellings), "byte", "str", "error", "void",
//   slice → "[]<elem>", optional → "<inner>?", variant → "A | B | …", named → "<name>".
// The result is a fresh, whole-compile-lifetime string (tk_alloc; recursion for slice/optional/
// variant builds it bottom-up). Never returns NULL (an unknown tag renders "<type>").
const char *tk_type_render(tk_type t);

// (S4) generic-instance name mangling — shared by resolve (type instantiation) and the mono pass
// so a use and its stamped decl agree byte-for-byte. tk_type_mangle: a type → a symbol fragment;
// tk_generic_inst_name: `<base>__g__<arg0>[__<arg1>…]`.
tk_str tk_type_mangle(tk_type t);
tk_str tk_generic_inst_name(tk_str base, tk_type *args, size_t nargs);

// (S4) type-generic instantiation: stamp concrete decls for written generic uses (`Box<i64>`) into
// the table before typing. Returns the table UNCHANGED when no generic type is used. Mirror of
// resolve.tks::instantiate_types.
tk_type_table tk_instantiate_types(tk_program program, tk_type_table table);

// (S4) `__g__`-instance name test + the stamped generic-type instance decls in a table (for the
// mono pass to emit as items). Mirror of resolve.tks name_is_g_instance / table_generic_instances.
bool tk_name_is_g_instance(tk_str name);
void tk_table_generic_instances(tk_type_table table, tk_type_decl **out, size_t *n);

// (W9.4) NORMALIZE generic-INSTANCE references in a type-decl to the bare stamped name (`Gen<i64>`
// member → `Gen__g__i64`), so the syntactic body codegen/VM/TKB read agrees with the resolved value
// type. No-op when the body has no generic-instance reference. Mirror of resolve.tks::normalize_inst_decl.
tk_type_decl tk_normalize_inst_decl(tk_type_decl d, tk_type_table table);

#endif // TK_CHECK_RESOLVE_H
