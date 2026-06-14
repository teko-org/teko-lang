// Phase 10 stability — multi-spawn / contention / channel fill+drain scenario.
// Drives the real Teko backend to emit a program with FIVE concurrent green-thread
// producers all competing on ONE channel, and a consumer that drains and sums
// them. Exercises the run queue with several routines, a function table of 5
// entries, and a channel that fills to 5 then drains. Deterministic: main() == 15.
//
// Build:
//   cc -I src runtime/wasm/emit-demo/emit_multi.c build/libteko_core.a -o emit_multi
//   ./emit_multi runtime/wasm/samples/emitted_multi.wat   # main() => 15
//
// IL:
//   main:       CHAN_INIT, (ICONST k, SPAWN_ASYNC) for k in 0..4,
//               CHAN_GET STORE, (CHAN_GET ADD STORE)x3, CHAN_GET ADD, HALT
//   routine k:  FUNC_BEGIN(k), ICONST (k+1), CHAN_PUT, FUNC_END   ;; puts k+1
// The first CHAN_GET drains the scheduler, running all five producers (channel
// fills to [1..5]); the five receives then sum 1+2+3+4+5 = 15.
#include "codegen/codegen_metal.h"
#include <stdio.h>
#include <string.h>

static void le32(unsigned char* b, int* n, int32_t v) {
    b[(*n)++] = v & 0xFF; b[(*n)++] = (v >> 8) & 0xFF;
    b[(*n)++] = (v >> 16) & 0xFF; b[(*n)++] = (v >> 24) & 0xFF;
}

int main(int argc, char** argv) {
    const char* out = (argc > 1) ? argv[1] : "emitted_multi.wat";
    TekoTarget target;
    memset(&target, 0, sizeof target);
    target.arch = ARCH_WASM32; target.os = OS_WASI;
    strncpy(target.target_string, "wasm32-wasi", sizeof(target.target_string) - 1);
    MetalContext* ctx = teko_metal_create(out, target);
    if (!ctx) { fprintf(stderr, "emit_multi: cannot open %s\n", out); return 2; }

    const int N = 5;
    unsigned char b[256]; int n = 0;
    // --- main ---
    b[n++] = OP_CHAN_INIT;
    for (int k = 0; k < N; k++) { b[n++] = OP_ICONST; le32(b, &n, k); b[n++] = OP_SPAWN_ASYNC; }
    b[n++] = OP_CHAN_GET; b[n++] = OP_STORE;                 // sum = v1
    for (int k = 1; k < N - 1; k++) { b[n++] = OP_CHAN_GET; b[n++] = OP_ADD; b[n++] = OP_STORE; }
    b[n++] = OP_CHAN_GET; b[n++] = OP_ADD;                   // sum += last value
    b[n++] = OP_HALT;
    // --- N producer routines: routine k puts (k+1) ---
    for (int k = 0; k < N; k++) {
        b[n++] = OP_FUNC_BEGIN; le32(b, &n, k);
        b[n++] = OP_ICONST; le32(b, &n, k + 1);
        b[n++] = OP_CHAN_PUT;
        b[n++] = OP_FUNC_END;
    }
    teko_metal_emit_program(ctx, b, (uint32_t)n);
    teko_metal_close(ctx);
    printf("emit_multi: wrote %d IL bytes (5 producers contending on 1 channel) -> %s\n", n, out);
    return 0;
}
