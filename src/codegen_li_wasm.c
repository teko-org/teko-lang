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
    // Phase 14 (14.A): drain the cooperative scheduler at $main close when the program
    // fires background tasks (`routines { … }`), so the spawned routines run before exit.
    teko_metal_set_emit_spawn(ctx, buffer->uses_spawn);
    // Phase 14 (14.B): import the duplex entry points from the runtime reactor + share memory.
    teko_metal_set_emit_duplex(ctx, buffer->uses_duplex);
    // Phase 14 (14.C): import the delayed-channel entry points from the reactor + share memory.
    teko_metal_set_emit_delayed(ctx, buffer->uses_delayed);
    // Phase 14 (14.D): import the broadcast entry points from the reactor + share memory.
    teko_metal_set_emit_bcast(ctx, buffer->uses_bcast);
    // Phase 14 (14.E): import the shared-memory entry points from the reactor + share memory.
    teko_metal_set_emit_shared(ctx, buffer->uses_shared);
    // Phase 14 (14.G): declare the host sleep/await imports for `wait`/`await` waiters.
    teko_metal_set_emit_wait(ctx, buffer->uses_wait);
    teko_metal_set_emit_await(ctx, buffer->uses_await);
    // Phase 14 (14.F): import the retry/circuit policy entry points from the reactor + share memory.
    teko_metal_set_emit_retry(ctx, buffer->uses_retry);
    // Phase 15 (15.A): import the object instance-store entry points from the reactor + share memory.
    teko_metal_set_emit_object(ctx, buffer->uses_object);
    // Phase 15 (15.B): import the static-vtable dispatch entry points from the reactor + share memory.
    teko_metal_set_emit_vtable(ctx, buffer->uses_vtable);
    // Phase 18 (18.E.1): import the fixed-size array entry points from the reactor + share memory.
    teko_metal_set_emit_array(ctx, buffer->uses_array);
    // Phase 18 (18.E.2): import the typed `i32[]` packed-array entry points from the reactor + share memory.
    teko_metal_set_emit_iarray(ctx, buffer->uses_iarray);
    // Phase 18 (18.E.4): emit the REAL simd128 vector kernel (teko_simd_sum_i32) + import iarray_data.
    teko_metal_set_emit_simd(ctx, buffer->uses_simd);
    // Phase 17 (17.A): hand the WASM emitter the float-constant pool + uses_float flag (the latter
    // gates the `(local $f0/$f1/$fvN f64)` declarations — float-free modules stay byte-identical).
    teko_metal_set_floats(ctx, buffer->floats, buffer->float_count);
    teko_metal_set_emit_float(ctx, buffer->uses_float);
    // Phase 17.F.3: hand the WASM emitter the decimal-constant pool + uses_decimal flag (the latter
    // gates the decimal linear-memory region + reactor imports — decimal-free modules byte-identical).
    teko_metal_set_decimals(ctx, buffer->decimals, buffer->decimal_count);
    teko_metal_set_emit_decimal(ctx, buffer->uses_decimal);

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
