#ifndef CODEGEN_LI_WASM_H
#define CODEGEN_LI_WASM_H

#include "codegen_li.h"
#include "codegen/codegen_metal.h"

// Phase 11 (Browser FFI frontend FE-B): bridge a frontend-produced IL BytecodeBuffer
// to the WASM backend. Builds a MetalContext for `target`, threads the string pool
// and import table into the emitter, lowers the IL to a `.wat` at `wat_path`, and
// (when glue_path/facade_path are non-NULL) writes the auto-generated dom.* glue and,
// with facade entries, the ergonomic facade module. Returns 0 on success.
//
// `facade_glue_module` is the relative import the facade uses to reach the glue
// (e.g. "./foo.glue.mjs"); only used when facade_entries is non-NULL.
int codegen_li_emit_wasm(const BytecodeBuffer* buffer, const char* wat_path,
                         TekoTarget target,
                         const char* glue_path,
                         const char* facade_path, const char* facade_glue_module,
                         const TekoWasmFacadeEntry* facade_entries, int facade_count);

#endif // CODEGEN_LI_WASM_H
