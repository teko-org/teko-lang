#include "codegen_li_wasm.h"
#include <stdlib.h>

int codegen_li_emit_wasm(const BytecodeBuffer* buffer, const char* wat_path,
                         TekoTarget target,
                         const char* glue_path,
                         const char* facade_path, const char* facade_glue_module,
                         const TekoWasmFacadeEntry* facade_entries, int facade_count) {
    if (!buffer || !wat_path) return 1;

    MetalContext* ctx = teko_metal_create(wat_path, target);
    if (!ctx) return 1;

    // String pool → (data …). pool.strings is char**; the setter takes const char**.
    teko_metal_set_strings(ctx, (const char**)buffer->pool.strings, buffer->pool.count);

    // Import table: the frontend's TekoILImport and the backend's TekoWasmImport are
    // structurally identical but distinct types; translate into a temporary array that
    // outlives emit_program (the setter only stores the pointer).
    TekoWasmImport* wimports = NULL;
    if (buffer->import_count > 0) {
        wimports = (TekoWasmImport*)malloc(sizeof(TekoWasmImport) * buffer->import_count);
        if (!wimports) { teko_metal_close(ctx); return 1; }
        for (int i = 0; i < buffer->import_count; i++) {
            wimports[i].ns = buffer->imports[i].ns;
            wimports[i].name = buffer->imports[i].name;
            wimports[i].n_params = buffer->imports[i].n_params;
            wimports[i].has_result = buffer->imports[i].has_result;
        }
        teko_metal_set_imports(ctx, wimports, buffer->import_count);
    }

    // Phase 12: declare the program's named locals in $main.
    teko_metal_set_local_count(ctx, buffer->local_count);
    // Phase 12 (P12-G): emit the base64/hex codec runtime only if the program uses it.
    teko_metal_set_emit_codecs(ctx, buffer->uses_codec);
    // Phase 13 (13.1): emit the in-module SHA hash runtime only if the program uses it.
    teko_metal_set_emit_hash(ctx, buffer->uses_hash);
    // Phase 13 (Sub-phase C): declare the host entropy import + CSPRNG wrapper only if used.
    teko_metal_set_emit_random(ctx, buffer->uses_random);
    // Phase 13 (Sub-phase C): declare the host entropy + time imports + uuid.v4/v7 runtime.
    teko_metal_set_emit_uuid_rng(ctx, buffer->uses_uuid_rng);
    // Phase 13 (Sub-phase C, "big step"): import the compiled-C crypto reactor (crypto.wasm)
    // + share its linear memory when the program uses a reactor-backed crypto primitive.
    teko_metal_set_emit_crypto_ext(ctx, buffer->uses_crypto_ext);

    teko_metal_emit_program(ctx, buffer->code, (uint32_t)buffer->size);

    // Auto-generated host glue / facade (before close — close frees ctx).
    int rc = 0;
    if (glue_path) {
        if (teko_metal_emit_dom_glue(ctx, glue_path) != 0) rc = 1;
    }
    if (rc == 0 && facade_path && facade_glue_module && facade_entries && facade_count > 0) {
        if (teko_metal_emit_facade(ctx, facade_path, facade_glue_module,
                                   facade_entries, facade_count) != 0) rc = 1;
    }

    teko_metal_close(ctx);
    free(wimports);
    return rc;
}
