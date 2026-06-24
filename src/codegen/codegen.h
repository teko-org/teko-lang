// src/codegen/codegen_c.h — F2 BACKEND: lower a CHECKED typed program (tk_tprogram)
// to C source text. The host `cc` then compiles that text to the first native Teko
// binary (M0). Distinct from src/emit/* (the `.tkb`/`.tkh` serializers): this is the
// path-to-machine-code lowering.
//
// COVERAGE (M.3 — honesty): M0 only needs integer arithmetic + let + return. Any node
// kind not yet lowered makes tk_emit_c FAIL with a clear message (no broken C, no
// crash), which the driver surfaces as `teko: <path>: codegen: <node> not yet
// supported`.
#ifndef TK_CODEGEN_C_H
#define TK_CODEGEN_C_H

#include "../checker/tast.h"   // tk_tprogram
#include "../core.h"           // TK_RESULT, tk_error

// `char* | error` — the emitted C source (heap-owned, NUL-terminated) or a codegen
// error. On failure the message reads "<node> not yet supported" / similar.
TK_RESULT(char *, tk_cstr_result);

// tk_emit_c — lower the whole checked program to a single C translation unit:
//   #include <stdint.h> / <stdbool.h>
//   each top-level tk_tfunction  -> a C function
//   the top-level loose statements (the VIRTUAL-MAIN) -> int main(void) { … }
// with early-return semantics (a `return n` -> `return (int)(n);`; fall-through and
// bare `return` -> `return 0;`). Returns heap C-source on success.
tk_cstr_result tk_emit_c(tk_tprogram prog);

#endif // TK_CODEGEN_C_H
