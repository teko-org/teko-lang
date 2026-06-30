// src/checker/monomorph.h — (S4b crumb 2c) the monomorphization pass.
// Stamps a concrete copy of each generic function per distinct instantiation and rewrites
// every generic call to the stamped instance's mangled name, so codegen/VM/tests receive a
// tree with only concrete functions. No-op (byte-identical) for a program with no generics.
// Pairs monomorph.tks (tk_monomorphize ↔ monomorphize).
#ifndef TK_CHECK_MONOMORPH_H
#define TK_CHECK_MONOMORPH_H

#include "tast.h"
#include "resolve.h"   // tk_type_table, tk_subst

tk_tprogram_result tk_monomorphize(tk_tprogram prog, tk_type_table table);

#endif // TK_CHECK_MONOMORPH_H

tk_type_expr tk_type_to_texpr(tk_type t);   // (W10) codegen synthesizes lifted-lambda fn params from resolved Types
