// Phase 10.2b — executable proof that the Teko WASM emitter lowers real
// concurrency. This drives the actual compiler backend (teko_metal_emit_program)
// with an IL program that spawns a green thread over a shared channel, and writes
// the emitted WAT to disk. CI then assembles it with wat2wasm and runs it under
// Node + wasmtime, asserting main() == 7 — i.e. the emitted module really
// schedules the spawned routine via call_indirect and round-trips a channel value.
//
// Build (against the compiler core static lib produced by the normal build):
//   cc -I src runtime/wasm/emit-demo/emit_spawn_channel.c build/libteko_core.a \
//      -o emit_spawn_channel
//   ./emit_spawn_channel runtime/wasm/samples/emitted.wat
//
// IL program:
//   main:      CHAN_INIT, ICONST 0, SPAWN_ASYNC, CHAN_GET, HALT
//   routine 0: FUNC_BEGIN(0), ICONST 7, CHAN_PUT, FUNC_END
// main inits a channel, spawns routine 0 (which receives the channel base as its
// argument), then blocking-receives. The receive finds the channel empty, yields
// to the scheduler, which dispatches routine 0 (it puts 7), then resumes -> 7.
#include "codegen/codegen_metal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void le32(unsigned char* b, int* n, int32_t v) {
    b[(*n)++] = v & 0xFF;
    b[(*n)++] = (v >> 8) & 0xFF;
    b[(*n)++] = (v >> 16) & 0xFF;
    b[(*n)++] = (v >> 24) & 0xFF;
}

int main(int argc, char** argv) {
    const char* out = (argc > 1) ? argv[1] : "emitted.wat";

    TekoTarget target;
    memset(&target, 0, sizeof target);
    target.arch = ARCH_WASM32;
    target.os = OS_WASI;
    strncpy(target.target_string, "wasm32-wasi", sizeof(target.target_string) - 1);

    MetalContext* ctx = teko_metal_create(out, target);
    if (!ctx) {
        fprintf(stderr, "emit_spawn_channel: cannot open %s\n", out);
        return 2;
    }

    unsigned char prog[64];
    int n = 0;
    // --- main ---
    prog[n++] = OP_CHAN_INIT;                 // channel at the arena base; base -> $cp
    prog[n++] = OP_ICONST; le32(prog, &n, 0); // $w0 = routine table index 0
    prog[n++] = OP_SPAWN_ASYNC;               // enqueue {fn=$w0, arg=$cp}
    prog[n++] = OP_CHAN_GET;                  // blocking receive -> $w0 (yields if empty)
    prog[n++] = OP_HALT;                      // main returns $w0
    // --- routine 0 ---
    prog[n++] = OP_FUNC_BEGIN; le32(prog, &n, 0); // green thread; $cp = its argument
    prog[n++] = OP_ICONST; le32(prog, &n, 7);     // $w0 = 7
    prog[n++] = OP_CHAN_PUT;                       // channel.put(7)
    prog[n++] = OP_FUNC_END;

    teko_metal_emit_program(ctx, prog, (uint32_t)n);
    teko_metal_close(ctx);
    printf("emit_spawn_channel: wrote %d IL bytes of cooperative WASM -> %s\n", n, out);
    return 0;
}
