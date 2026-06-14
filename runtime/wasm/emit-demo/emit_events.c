// Phase 11 (Browser FFI) MVP-3 — executable proof of JS->Teko callbacks. Drives
// the real backend to emit a module that, in $main, looks up #count and registers
// a click listener bound to a Teko callback routine (table slot 0):
//
//   el = dom.getElementById("count")
//   dom.on(el, "click", /*handler fn index*/ 0)
//
// and a callback routine (the handler) that runs when the host fires the event —
// the glue calls the exported teko_invoke(0, el), which dispatches the routine
// with $w0 = el, and the routine sets the element's text:
//
//   handler(el): dom.setText(el, "clicked!")
//
// The host harness (run-events.mjs / browser/events-run.mjs) loads the module with
// the auto-generated glue, clicks #count, and asserts its text became "clicked!".
//
// Build:
//   cc -I src runtime/wasm/emit-demo/emit_events.c build/libteko_core.a -o emit_events
//   ./emit_events runtime/wasm/samples/emitted_events.wat runtime/wasm/samples/emitted_events.glue.mjs
//
// Models the eventual lowering of `@dom.on(el, "click", handler)`.
#include "codegen/codegen_metal.h"
#include <stdio.h>
#include <string.h>

static void le32(unsigned char* b, int* n, int32_t v) {
    b[(*n)++] = v & 0xFF; b[(*n)++] = (v >> 8) & 0xFF;
    b[(*n)++] = (v >> 16) & 0xFF; b[(*n)++] = (v >> 24) & 0xFF;
}

int main(int argc, char** argv) {
    const char* out_wat  = (argc > 1) ? argv[1] : "emitted_events.wat";
    const char* out_glue = (argc > 2) ? argv[2] : "emitted_events.glue.mjs";

    TekoTarget target;
    memset(&target, 0, sizeof target);
    target.arch = ARCH_WASM32; target.os = OS_WASI;
    strncpy(target.target_string, "wasm32-wasi", sizeof(target.target_string) - 1);
    MetalContext* ctx = teko_metal_create(out_wat, target);
    if (!ctx) { fprintf(stderr, "emit_events: cannot open %s\n", out_wat); return 2; }

    static const char* strings[] = { "count", "click", "clicked!" };
    enum { S_COUNT = 0, S_CLICK = 1, S_TEXT = 2 };
    teko_metal_set_strings(ctx, strings, 3);

    static const TekoWasmImport imports[] = {
        { "dom", "getElementById", 2, 1 }, // (ptr,len) -> handle           : #0
        { "dom", "setText",        3, 0 }, // (handle,ptr,len)              : #1
        { "dom", "on",             4, 0 }, // (handle,ptr,len,fn_index)     : #2
    };
    enum { I_GETBYID = 0, I_SETTEXT = 1, I_ON = 2 };
    teko_metal_set_imports(ctx, imports, 3);

    const int len_count = (int)strlen(strings[S_COUNT]); // 5
    const int len_click = (int)strlen(strings[S_CLICK]); // 5
    const int len_text  = (int)strlen(strings[S_TEXT]);  // 8
    const int HANDLER_FN = 0;                            // table slot of $routine_0

    unsigned char b[160]; int n = 0;

    // --- $main: el = getElementById("count"); dom.on(el, "click", HANDLER_FN) ---
    b[n++] = OP_SCONST;      le32(b, &n, S_COUNT);    // $w0 = &"count"
    b[n++] = OP_SETARG;      le32(b, &n, 0);          // $a0 = ptr
    b[n++] = OP_ICONST;      le32(b, &n, len_count);  // $w0 = len
    b[n++] = OP_CALL_IMPORT; le32(b, &n, I_GETBYID);  // $w0 = el

    b[n++] = OP_SETARG;      le32(b, &n, 0);          // $a0 = el
    b[n++] = OP_SCONST;      le32(b, &n, S_CLICK);    // $w0 = &"click"
    b[n++] = OP_SETARG;      le32(b, &n, 1);          // $a1 = ptr
    b[n++] = OP_ICONST;      le32(b, &n, len_click);  // $w0 = len
    b[n++] = OP_SETARG;      le32(b, &n, 2);          // $a2 = len
    b[n++] = OP_ICONST;      le32(b, &n, HANDLER_FN); // $w0 = handler fn index
    b[n++] = OP_CALL_IMPORT; le32(b, &n, I_ON);       // dom.on(el, "click", 0)
    b[n++] = OP_HALT;

    // --- handler routine (table slot 0): setText(arg, "clicked!") ---
    // teko_invoke(0, el) enters here with $w0 = el (the attached handle).
    b[n++] = OP_FUNC_BEGIN;  le32(b, &n, HANDLER_FN);
    b[n++] = OP_SETARG;      le32(b, &n, 0);          // $a0 = el (the event arg)
    b[n++] = OP_SCONST;      le32(b, &n, S_TEXT);     // $w0 = &"clicked!"
    b[n++] = OP_SETARG;      le32(b, &n, 1);          // $a1 = ptr
    b[n++] = OP_ICONST;      le32(b, &n, len_text);   // $w0 = len
    b[n++] = OP_CALL_IMPORT; le32(b, &n, I_SETTEXT);  // setText(el, ptr, len)
    b[n++] = OP_FUNC_END;

    teko_metal_emit_program(ctx, b, (uint32_t)n);

    int glue_rc = teko_metal_emit_dom_glue(ctx, out_glue);
    teko_metal_close(ctx);
    if (glue_rc != 0) {
        fprintf(stderr, "emit_events: cannot write glue %s\n", out_glue);
        return 2;
    }
    printf("emit_events: wrote %d IL bytes -> %s (+ glue %s)\n", n, out_wat, out_glue);
    return 0;
}
