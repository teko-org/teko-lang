// src/checker/check_modules.h — W-vis-enforce: the MODULE-SYSTEM enforcement pass.
//
// Given the flattened program (items tagged with their source namespace — A3) and the
// collected type table (each reg carries its namespace + visibility), enforce the module
// system on every TYPE reference:
//   * a BARE reference to a type declared in ANOTHER namespace is an error (must be
//     qualified `ns::Type` with a `use teko::ns`, or the absolute path) — M.3/M.2;
//   * a QUALIFIED reference to a PRIVATE type (not `pub`/`exp`) in another namespace is an
//     error (cross-namespace access requires `pub` — visibility, B.9);
//   * a qualified reference whose `use` alias is unbound, or that names the wrong
//     namespace, is an error.
// Same-namespace references stay bare. Builtins/predefined types are never namespaced.
// Returns NULL on success, or a static error message (the `const char*` check idiom).
//
// (Value-level references — call callees, enum-member paths — are a follow-on increment;
// this pass covers the dominant TYPE surface: params, returns, struct fields, variant members.)
#ifndef TK_CHECK_MODULES_H
#define TK_CHECK_MODULES_H

#include "resolve.h"          // tk_type_table (regs carry namespace + vis)
#include "../parser/ast.h"    // tk_program, tk_item

const char *tk_check_modules(tk_program prog, tk_type_table table);

// Build a located diagnostic "file:line:col: msg" (omitting absent parts) — the shared
// formatter for checker errors (W-loc-2). Returns a fresh string (whole-compile lifetime).
const char *tk_diag_at(tk_str file, uint32_t line, uint32_t col, const char *msg);

#endif // TK_CHECK_MODULES_H
