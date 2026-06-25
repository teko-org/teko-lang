// src/emit/header.h — E-emit-a, the `.tkh` HEADER-BUILDING DRIVER. Mirrors header.tks.
// Given a CHECKED typed program, keep only the EXPORTED (`vis == exp`) items, build their
// export signatures, assemble a `tk_header`, and (emit_program) emit it. Reuses
// tk_resolve_type (the checker) and tk_emit_tkh (tkh.c). Adds no codec primitive.
#ifndef TK_EMIT_HEADER_H
#define TK_EMIT_HEADER_H

#include "tkh.h"                  // tk_header, tk_header_result, tk_emit_tkh, tk_bytes
#include "../checker/tast.h"      // tk_tprogram, tk_tfunction, tk_titem
#include "../checker/resolve.h"   // tk_type_table, tk_resolve_type

TK_RESULT(tk_bytes, tk_bytes_result);   // []byte | error — emit_program's surface

tk_header_result tk_build_header(tk_tprogram prog, tk_type_table table);
tk_bytes_result  tk_emit_program(tk_tprogram prog, tk_type_table table);

#endif // TK_EMIT_HEADER_H
