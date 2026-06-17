#include "codegen_metal.h"
#include "emit_native_hosted.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

MetalContext* teko_metal_create(const char* output_asm_path, TekoTarget target) {
    if (!output_asm_path) return NULL;

    FILE* f = fopen(output_asm_path, "w");
    if (!f) return NULL;

    MetalContext* ctx = (MetalContext*)malloc(sizeof(MetalContext));
    ctx->file = f;
    ctx->target = target;
    ctx->label_count = 0;
    ctx->wasm_open = 0;
    ctx->wasm_routine_count = 0;
    memset(ctx->wasm_routine_ids, 0, sizeof(ctx->wasm_routine_ids));
    ctx->wasm_routine_yields = 0;
    ctx->wasm_yield_idx = 0;
    ctx->wasm_strings = NULL;
    ctx->wasm_string_count = 0;
    ctx->wasm_imports = NULL;
    ctx->wasm_import_count = 0;
    ctx->wasm_local_count = 0;
    ctx->wasm_emit_codecs = 0;
    ctx->wasm_emit_hash = 0;
    ctx->wasm_emit_random = 0;
    ctx->wasm_emit_uuid_rng = 0;
    ctx->wasm_emit_crypto_ext = 0;
    ctx->wasm_emit_spawn = 0;
    ctx->wasm_emit_duplex = 0;
    ctx->wasm_emit_object = 0;
    ctx->wasm_emit_vtable = 0;
    ctx->wasm_emit_array = 0;
    ctx->wasm_emit_iarray = 0;
    ctx->wasm_emit_simd = 0;
    ctx->wasm_emit_delayed = 0;
    ctx->wasm_emit_bcast = 0;
    ctx->wasm_emit_shared = 0;
    ctx->wasm_emit_wait = 0;
    ctx->wasm_emit_await = 0;
    ctx->wasm_emit_retry = 0;
    ctx->cf_id_next = 0;
    ctx->cf_loop_sp = 0;
    ctx->cf_if_sp = 0;
    ctx->float_pool = NULL;     // Phase 17 (17.A)
    ctx->float_count = 0;
    ctx->wasm_emit_float = 0;
    ctx->decimal_pool = NULL;   // Phase 17.F.3
    ctx->decimal_count = 0;
    ctx->wasm_emit_decimal = 0;
    ctx->hosted = 0;
    return ctx;
}

void teko_metal_set_strings(MetalContext* ctx, const char** strings, int count) {
    if (!ctx) return;
    ctx->wasm_strings = strings;
    ctx->wasm_string_count = (count > 0) ? count : 0;
}

void teko_metal_set_imports(MetalContext* ctx, const TekoWasmImport* imports, int count) {
    if (!ctx) return;
    ctx->wasm_imports = imports;
    ctx->wasm_import_count = (count > 0) ? count : 0;
}

void teko_metal_set_local_count(MetalContext* ctx, int count) {
    if (!ctx) return;
    ctx->wasm_local_count = (count > 0) ? count : 0;
}

void teko_metal_set_emit_codecs(MetalContext* ctx, int enabled) {
    if (!ctx) return;
    ctx->wasm_emit_codecs = enabled ? 1 : 0;
}

void teko_metal_set_emit_hash(MetalContext* ctx, int enabled) {
    if (!ctx) return;
    ctx->wasm_emit_hash = enabled ? 1 : 0;
}

void teko_metal_set_emit_random(MetalContext* ctx, int enabled) {
    if (!ctx) return;
    ctx->wasm_emit_random = enabled ? 1 : 0;
}

void teko_metal_set_emit_uuid_rng(MetalContext* ctx, int enabled) {
    if (!ctx) return;
    ctx->wasm_emit_uuid_rng = enabled ? 1 : 0;
}

void teko_metal_set_emit_crypto_ext(MetalContext* ctx, int enabled) {
    if (!ctx) return;
    ctx->wasm_emit_crypto_ext = enabled ? 1 : 0;
}

void teko_metal_set_emit_spawn(MetalContext* ctx, int enabled) {
    if (!ctx) return;
    ctx->wasm_emit_spawn = enabled ? 1 : 0;
}

void teko_metal_set_emit_duplex(MetalContext* ctx, int enabled) {
    if (!ctx) return;
    ctx->wasm_emit_duplex = enabled ? 1 : 0;
}

void teko_metal_set_emit_delayed(MetalContext* ctx, int enabled) {
    if (!ctx) return;
    ctx->wasm_emit_delayed = enabled ? 1 : 0;
}

void teko_metal_set_emit_bcast(MetalContext* ctx, int enabled) {
    if (!ctx) return;
    ctx->wasm_emit_bcast = enabled ? 1 : 0;
}

void teko_metal_set_emit_shared(MetalContext* ctx, int enabled) {
    if (!ctx) return;
    ctx->wasm_emit_shared = enabled ? 1 : 0;
}

void teko_metal_set_emit_wait(MetalContext* ctx, int enabled) {
    if (!ctx) return;
    ctx->wasm_emit_wait = enabled ? 1 : 0;
}

void teko_metal_set_emit_await(MetalContext* ctx, int enabled) {
    if (!ctx) return;
    ctx->wasm_emit_await = enabled ? 1 : 0;
}

void teko_metal_set_emit_retry(MetalContext* ctx, int enabled) {
    if (!ctx) return;
    ctx->wasm_emit_retry = enabled ? 1 : 0;
}

void teko_metal_set_emit_object(MetalContext* ctx, int enabled) {
    if (!ctx) return;
    ctx->wasm_emit_object = enabled ? 1 : 0;
}

void teko_metal_set_emit_vtable(MetalContext* ctx, int enabled) {
    if (!ctx) return;
    ctx->wasm_emit_vtable = enabled ? 1 : 0;
}

void teko_metal_set_emit_array(MetalContext* ctx, int enabled) {
    if (!ctx) return;
    ctx->wasm_emit_array = enabled ? 1 : 0;
}

void teko_metal_set_emit_iarray(MetalContext* ctx, int enabled) {
    if (!ctx) return;
    ctx->wasm_emit_iarray = enabled ? 1 : 0;
}

void teko_metal_set_emit_simd(MetalContext* ctx, int enabled) {
    if (!ctx) return;
    ctx->wasm_emit_simd = enabled ? 1 : 0;
}

void teko_metal_set_hosted(MetalContext* ctx, int enabled) {
    if (!ctx) return;
    ctx->hosted = enabled ? 1 : 0;
}

void teko_metal_set_floats(MetalContext* ctx, const double* floats, int count) {
    if (!ctx) return;
    ctx->float_pool = floats;
    ctx->float_count = (count > 0) ? count : 0;
}

void teko_metal_set_emit_float(MetalContext* ctx, int enabled) {
    if (!ctx) return;
    ctx->wasm_emit_float = enabled ? 1 : 0;
}

void teko_metal_set_decimals(MetalContext* ctx, const unsigned char* decimals, int count) {
    if (!ctx) return;
    ctx->decimal_pool = decimals;
    ctx->decimal_count = (count > 0) ? count : 0;
}

void teko_metal_set_emit_decimal(MetalContext* ctx, int enabled) {
    if (!ctx) return;
    ctx->wasm_emit_decimal = enabled ? 1 : 0;
}

static void teko_metal_route_instruction(MetalContext* ctx, OpCode op, int32_t arg) {
    TekoOS os = ctx->target.os;
    TekoArch arch = ctx->target.arch;

    // Phase 13 native runner: the libc-hosted, assemble-able path for the host arches.
    if (ctx->hosted &&
        (arch == ARCH_X86_64 || arch == ARCH_ARM64 || arch == ARCH_APPLE_SILICON)) {
        emit_native_hosted(ctx, op, arg);
        return;
    }

    if (os == OS_MACOS_DARWIN) {
        if (arch == ARCH_APPLE_SILICON || arch == ARCH_ARM64) {
            emit_darwin_arm64(ctx, op, arg);
            return;
        }
        if (arch == ARCH_X86_64) {
            emit_darwin_x86_64(ctx, op, arg);
            return;
        }
    }
    else if (os == OS_WINDOWS) {
        if (arch == ARCH_X86_64) { emit_win_x86_64(ctx, op, arg); return; }
        if (arch == ARCH_X86)    { emit_win_x86(ctx, op, arg); return; }
        if (arch == ARCH_ARM64)  { emit_win_arm64(ctx, op, arg); return; }
    }
    else if (os == OS_BARE_METAL || os == OS_WASI) {
        if (arch == ARCH_WASM32 || arch == ARCH_WASM64) { emit_wasm_pure(ctx, op, arg); return; }
    }
    else {
        if (os == OS_LINUX) {
            switch (arch) {
                case ARCH_X86_64: emit_linux_x86_64(ctx, op, arg); return;
                case ARCH_X86:    emit_linux_x86(ctx, op, arg); return;
                case ARCH_ARM64:  emit_linux_arm64(ctx, op, arg); return;
                case ARCH_ARM32:  emit_linux_arm32(ctx, op, arg); return;
                case ARCH_RISCV64:emit_linux_riscv64(ctx, op, arg); return;
                case ARCH_RISCV32:emit_linux_riscv32(ctx, op, arg); return;
                case ARCH_MIPS32:
                case ARCH_MIPS64: emit_linux_mips(ctx, op, arg); return;
                case ARCH_PPC64:  emit_linux_ppc64(ctx, op, arg); return;
                default: break;
            }
        }
        else {
            if (arch == ARCH_X86_64) { emit_freebsd_x86_64(ctx, op, arg); return; }
            if (arch == ARCH_ARM64)  { emit_freebsd_arm64(ctx, op, arg); return; }
        }
    }
}

static int32_t read_le_int32(const unsigned char* bytecode, uint32_t index) {
    return (bytecode[index + 0] << 0)  |
           (bytecode[index + 1] << 8)  |
           (bytecode[index + 2] << 16) |
           (bytecode[index + 3] << 24);
}

// Count the suspension points (blocking OP_CHAN_GET) in the routine body that
// begins just after an OP_FUNC_BEGIN at `start`, up to its OP_FUNC_END. The WASM
// emitter needs this count up front to size the state-machine block nest and the
// br_table that dispatches a resumed green thread to the right yield point (10.3).
static int count_routine_yields(const unsigned char* il, uint32_t start, uint32_t size) {
    int yields = 0;
    uint32_t p = start;
    while (p < size) {
        OpCode op = (OpCode)il[p];
        if (op == OP_FUNC_END) break;
        if (op == OP_CHAN_GET) yields++;
        if (op == OP_ICONST || op == OP_SCONST || op == OP_JMP || op == OP_JMP_IF_FALSE ||
            op == OP_FUNC_BEGIN || op == OP_CALL_IMPORT || op == OP_SETARG ||
            op == OP_LOAD_LOCAL || op == OP_STORE_LOCAL || op == OP_CALL_RUNTIME ||
            op == OP_SPAWN_ASYNC_ARGS || op == OP_LOAD_SPAWN_ARG || op == OP_CALL_FUNC ||
            op == OP_FCONST || op == OP_FSTORE_LOCAL || op == OP_FLOAD_LOCAL ||
            op == OP_DCONST || op == OP_DSTORE_LOCAL || op == OP_DLOAD_LOCAL) p += 5; // Phase 17 4-byte float/decimal ops
        else p += 1;
    }
    return yields;
}

static void process_linear_il_bytes(MetalContext* ctx, const unsigned char* bytecode, uint32_t size) {
    uint32_t i = 0;
    bool dead_code_zone = false;

    bool accum_has_value = false;
    int32_t accum_last_value = 0;
    OpCode last_arith_op = (OpCode)0;

    unsigned char* local_il = (unsigned char*)malloc(size);
    if (!local_il) return;
    memcpy(local_il, bytecode, size);

    // STEP 1: CONSTANT FOLDING (Static collapse preprocessor)
    uint32_t scan = 0;
    while (scan < size) {
        OpCode op = (OpCode)local_il[scan];

        if (op == OP_ICONST && (scan + 10 < size)) {
            OpCode next_op = (OpCode)local_il[scan + 5];
            if (next_op == OP_ICONST) {
                OpCode arith_op = (OpCode)local_il[scan + 10];

                if (arith_op == OP_ADD || arith_op == OP_SUB || arith_op == OP_MUL || arith_op == OP_DIV) {
                    int32_t c1 = read_le_int32(local_il, scan + 1);
                    int32_t c2 = read_le_int32(local_il, scan + 6);
                    int32_t res = 0;
                    bool fold_ok = true;

                    if (arith_op == OP_ADD) res = c1 + c2;
                    else if (arith_op == OP_SUB) res = c1 - c2;
                    else if (arith_op == OP_MUL) res = c1 * c2;
                    else if (arith_op == OP_DIV) {
                        if (c2 != 0) res = c1 / c2;
                        else fold_ok = false;
                    }

                    if (fold_ok) {
                        local_il[scan + 1] = (res >> 0) & 0xFF;
                        local_il[scan + 2] = (res >> 8) & 0xFF;
                        local_il[scan + 3] = (res >> 16) & 0xFF;
                        local_il[scan + 4] = (res >> 24) & 0xFF;

                        // MASTER FIX: Fills collapsed positions with 0xCC (Neutral Marker)
                        // This protects the 0x00 byte of the legitimate OP_HALT!
                        memset(&local_il[scan + 5], 0xCC, 6);
                    }
                }
            }
        }

        if (op == OP_ICONST || op == OP_SCONST || op == OP_JMP || op == OP_JMP_IF_FALSE ||
            op == OP_FUNC_BEGIN || op == OP_CALL_IMPORT || op == OP_SETARG ||
            op == OP_LOAD_LOCAL || op == OP_STORE_LOCAL || op == OP_CALL_RUNTIME ||
            op == OP_SPAWN_ASYNC_ARGS || op == OP_LOAD_SPAWN_ARG || op == OP_CALL_FUNC ||
            op == OP_FCONST || op == OP_FSTORE_LOCAL || op == OP_FLOAD_LOCAL ||
            op == OP_DCONST || op == OP_DSTORE_LOCAL || op == OP_DLOAD_LOCAL) scan += 5; // Phase 17 4-byte float/decimal ops
        else scan += 1;
    }

    // STEP 2: COOPERATIVE EMITTER FILTER (DCE + CSE)
    while (i < size) {
        uint32_t current_op_index = i;
        OpCode op = (OpCode)local_il[i++];
        int32_t arg = 0;
        bool skip_by_cse = false;

        // STRICTLY ignores only the bytes marked by constant folding (0xCC)
        if (op == 0xCC) {
            continue;
        }

        if (op == OP_ICONST || op == OP_SCONST || op == OP_JMP || op == OP_JMP_IF_FALSE ||
            op == OP_FUNC_BEGIN || op == OP_CALL_IMPORT || op == OP_SETARG ||
            op == OP_LOAD_LOCAL || op == OP_STORE_LOCAL || op == OP_CALL_RUNTIME ||
            op == OP_SPAWN_ASYNC_ARGS || op == OP_LOAD_SPAWN_ARG || op == OP_CALL_FUNC ||
            op == OP_FCONST || op == OP_FSTORE_LOCAL || op == OP_FLOAD_LOCAL ||
            op == OP_DCONST || op == OP_DSTORE_LOCAL || op == OP_DLOAD_LOCAL) { // Phase 17 4-byte float/decimal ops
            arg = read_le_int32(local_il, current_op_index + 1);
            i += 4;
        }

        // Function boundaries start a fresh emission context, exactly like a
        // label target: a routine body after $main's HALT must not inherit
        // $main's dead-code zone, and CSE/const accumulators reset per function.
        if ((int)op >= 100 || op == OP_FUNC_BEGIN || op == OP_FUNC_END) {
            dead_code_zone = false;
            accum_has_value = false;
            last_arith_op = (OpCode)0;
        }

        // Hand the emitter the yield count for the routine it is about to open,
        // so it can size the state-machine dispatch (10.3 mid-function suspension).
        if (op == OP_FUNC_BEGIN) {
            ctx->wasm_routine_yields = count_routine_yields(local_il, i, size);
            ctx->wasm_yield_idx = 0;
        }

        if (!dead_code_zone) {
            if (op == OP_ICONST) {
                if (accum_has_value && accum_last_value == arg && last_arith_op == (OpCode)0) {
                    skip_by_cse = true;
                } else {
                    accum_last_value = arg;
                    accum_has_value = true;
                    last_arith_op = (OpCode)0;
                }
            }
            else if (op == OP_ADD || op == OP_SUB || op == OP_MUL || op == OP_DIV ||
                     op == OP_MOD || op == OP_EQ || op == OP_NE ||
                     op == OP_LT || op == OP_LE || op == OP_GT || op == OP_GE) {
                if (last_arith_op == op) {
                    skip_by_cse = true;
                } else {
                    last_arith_op = op;
                    accum_has_value = false;
                }
            }
            else if (op == OP_STORE || op == OP_LOAD || op == OP_SPAWN_ASYNC || op == OP_SPAWN_ASYNC_ARGS || op == OP_LOAD_SPAWN_ARG ||
                     op == OP_CHAN_INIT || op == OP_CHAN_GET || op == OP_CALL_IMPORT ||
                     op == OP_SETARG || op == OP_STORE_LOCAL || op == OP_LOAD_LOCAL ||
                     op == OP_CALL_RUNTIME ||
                     op == OP_DUPLEX_OPEN || op == OP_DUPLEX_SEND || op == OP_DUPLEX_RECV ||
                     op == OP_DUPLEX_POLL || op == OP_DUPLEX_CLOSE ||
                     op == OP_DELAYED_OPEN || op == OP_DELAYED_SEND || op == OP_DELAYED_ADVANCE ||
                     op == OP_DELAYED_RECV || op == OP_DELAYED_POLL || op == OP_DELAYED_CLOSE ||
                     op == OP_BCAST_OPEN || op == OP_BCAST_SUBSCRIBE || op == OP_BCAST_PUBLISH ||
                     op == OP_BCAST_RECV || op == OP_BCAST_POLL || op == OP_BCAST_CLOSE ||
                     op == OP_SHARED_ENTER || op == OP_SHARED_LEAVE || op == OP_ATOMIC_CELL ||
                     op == OP_ATOMIC_ADD || op == OP_ATOMIC_LOAD || op == OP_ATOMIC_STORE ||
                     op == OP_WAIT || op == OP_AWAIT_FOR ||
                     op == OP_LOOP_BEGIN || op == OP_LOOP_END || op == OP_BREAK ||
                     op == OP_CONTINUE || op == OP_BREAK_IF_FALSE ||
                     op == OP_IF_BEGIN || op == OP_IF_END ||
                     op == OP_RETRY_NEW || op == OP_RETRY_SHOULD_CONTINUE ||
                     op == OP_RETRY_NEXT_DELAY || op == OP_CIRCUIT_NEW ||
                     op == OP_CIRCUIT_ALLOW || op == OP_CIRCUIT_RECORD ||
                     op == OP_OBJ_NEW || op == OP_OBJ_SET || op == OP_OBJ_GET ||
                     op == OP_OBJ_FREE || op == OP_CALL_FUNC ||
                     op == OP_VTABLE_SET || op == OP_VTABLE_GET ||
                     // Phase 18 (18.E.1): array ops are $w0-clobbering runtime calls like OP_OBJ_*.
                     op == OP_ARR_NEW || op == OP_ARR_GET || op == OP_ARR_SET ||
                     op == OP_ARR_LEN ||
                     // Phase 18 (18.E.2): typed `i32[]` packed-array ops, same runtime-call family.
                     op == OP_IARR_NEW || op == OP_IARR_GET || op == OP_IARR_SET ||
                     op == OP_IARR_LEN ||
                     // Phase 18 (18.E.4): the SIMD reduction is a $w0-clobbering call (the run's data
                     // ptr/len + the vector kernel result land in $w0) — same family.
                     op == OP_SIMD_SUM ||
                     // Phase 17 (17.A): ALL float ops are an integer-CSE BARRIER — they are not
                     // integer arith, so they must reset last_arith_op (they must never be folded
                     // against an integer ADD/SUB/etc.). FCONST/FADD/etc. write $f0/$f1 only (native
                     // scratch r11/x9, NOT rax/rbx), so they don't touch $w0 — barrier suffices.
                     op == OP_FCONST || op == OP_FADD || op == OP_FSUB || op == OP_FMUL ||
                     op == OP_FDIV || op == OP_FEQ || op == OP_FNE || op == OP_FLT ||
                     op == OP_FLE || op == OP_FGT || op == OP_FGE || op == OP_FSTORE ||
                     op == OP_FLOAD || op == OP_FSTORE_LOCAL || op == OP_FLOAD_LOCAL ||
                     op == OP_I2F ||
                     // Phase 17 (17.B): OP_FMOD writes $f0 only (barrier suffices); OP_F2I writes
                     // $w0 with a non-const int, so it is a barrier here AND a $w0-cache reset below.
                     op == OP_FMOD || op == OP_F2I ||
                     // Phase 17.F.3: ALL decimal ops are an integer-CSE BARRIER (they are not integer
                     // arith). The arith/move ops touch $d0/$d1/$dvN only (native scratch GPRs, NOT
                     // $w0/$w1) — barrier suffices; the COMPARES (DEQ..DGE) write $w0 with a non-const
                     // 0/1, so they are ALSO a $w0-cache reset below.
                     op == OP_DCONST || op == OP_DADD || op == OP_DSUB || op == OP_DMUL ||
                     op == OP_DDIV || op == OP_DMOD || op == OP_DEQ || op == OP_DNE ||
                     op == OP_DLT || op == OP_DLE || op == OP_DGT || op == OP_DGE ||
                     op == OP_DSTORE || op == OP_DLOAD || op == OP_DSTORE_LOCAL ||
                     op == OP_DLOAD_LOCAL ||
                     // Phase 17.F.4: the int/float↔decimal CASTS are also integer-CSE barriers. I2D/
                     // F2D/D2F write $d0/$f0 only (barrier suffices); OP_D2I writes $w0 with a
                     // non-const i32, so it is ALSO a $w0-cache reset below (like OP_F2I).
                     op == OP_I2D || op == OP_F2D || op == OP_D2I || op == OP_D2F) {
                last_arith_op = (OpCode)0;
            }

            // Any op that overwrites the accumulator with a *non-constant* value
            // invalidates the ICONST CSE cache — otherwise a later `ICONST k` that
            // matches a stale accum_last_value gets wrongly skipped while $w0 in
            // fact holds a string ptr / load / call result. (SETARG/STORE/STORE_LOCAL
            // only read $w0 or write elsewhere, so they leave the cache intact.)
            // Phase 14: OP_SPAWN_ASYNC lowers to a runtime `call` on the native runner
            // (teko_rt_spawn), which clobbers the accumulator register ($w0/rax) — so it
            // must invalidate the cache too, exactly like CALL_IMPORT/CALL_RUNTIME. (On
            // WASM $w0 is a local that survives the call, but the cache reset is harmless
            // there — it only re-emits a redundant `local.set $w0`.) Without this, two
            // consecutive `routines { f(); f(); }` spawns of the same slot elide the second
            // ICONST and the second spawn reads the clobbered register → wrong slot.
            // Phase 14: OP_DUPLEX_* also lower to runtime `call`s (teko_rt_duplex_*) that
            // clobber $w0 with the handle/value/status result — same rule as the calls above.
            if (op == OP_SCONST || op == OP_LOAD || op == OP_CHAN_GET ||
                op == OP_CALL_IMPORT || op == OP_LOAD_LOCAL || op == OP_CALL_RUNTIME ||
                op == OP_SPAWN_ASYNC || op == OP_SPAWN_ASYNC_ARGS || op == OP_LOAD_SPAWN_ARG ||
                op == OP_DUPLEX_OPEN || op == OP_DUPLEX_SEND || op == OP_DUPLEX_RECV ||
                op == OP_DUPLEX_POLL || op == OP_DUPLEX_CLOSE ||
                op == OP_DELAYED_OPEN || op == OP_DELAYED_SEND || op == OP_DELAYED_ADVANCE ||
                op == OP_DELAYED_RECV || op == OP_DELAYED_POLL || op == OP_DELAYED_CLOSE ||
                op == OP_BCAST_OPEN || op == OP_BCAST_SUBSCRIBE || op == OP_BCAST_PUBLISH ||
                op == OP_BCAST_RECV || op == OP_BCAST_POLL || op == OP_BCAST_CLOSE ||
                op == OP_SHARED_ENTER || op == OP_SHARED_LEAVE || op == OP_ATOMIC_CELL ||
                op == OP_ATOMIC_ADD || op == OP_ATOMIC_LOAD || op == OP_ATOMIC_STORE ||
                op == OP_WAIT || op == OP_AWAIT_FOR ||
                op == OP_LOOP_BEGIN || op == OP_LOOP_END || op == OP_BREAK ||
                op == OP_CONTINUE || op == OP_BREAK_IF_FALSE ||
                op == OP_IF_BEGIN || op == OP_IF_END ||
                op == OP_RETRY_NEW || op == OP_RETRY_SHOULD_CONTINUE ||
                op == OP_RETRY_NEXT_DELAY || op == OP_CIRCUIT_NEW ||
                op == OP_CIRCUIT_ALLOW || op == OP_CIRCUIT_RECORD ||
                op == OP_OBJ_NEW || op == OP_OBJ_SET || op == OP_OBJ_GET ||
                op == OP_OBJ_FREE || op == OP_CALL_FUNC ||
                op == OP_VTABLE_SET || op == OP_VTABLE_GET ||
                // Phase 18 (18.E.1): array ops clobber $w0 (handle/value/len) — reset the cache.
                op == OP_ARR_NEW || op == OP_ARR_GET || op == OP_ARR_SET ||
                op == OP_ARR_LEN ||
                // Phase 18 (18.E.2): typed `i32[]` packed-array ops also clobber $w0 — reset.
                op == OP_IARR_NEW || op == OP_IARR_GET || op == OP_IARR_SET ||
                op == OP_IARR_LEN ||
                // Phase 18 (18.E.4): the SIMD reduction clobbers $w0 (the scalar sum result) — reset.
                op == OP_SIMD_SUM ||
                // Phase 17 (17.A): the float COMPARES write $w0 with a non-constant (0/1), so they
                // invalidate the ICONST reuse cache exactly like an integer compare / runtime call.
                // (FCONST/FADD/etc. write $f0/$f1 only — they don't clobber $w0, so they need only
                // the last_arith_op barrier above, not this $w0-cache reset.)
                op == OP_FEQ || op == OP_FNE || op == OP_FLT ||
                op == OP_FLE || op == OP_FGT || op == OP_FGE ||
                // Phase 17 (17.B): OP_F2I writes $w0 with the (non-constant) truncated int result,
                // so it invalidates the ICONST reuse cache exactly like a float compare. (OP_FMOD
                // touches $f0/$f1 only — no $w0 clobber — so it stays out of this set.)
                op == OP_F2I ||
                // Phase 17.F.3: the decimal COMPARES (DEQ..DGE) write $w0 with a non-constant 0/1,
                // so they invalidate the ICONST reuse cache exactly like a float compare. (DCONST/
                // DADD/etc. touch $d0/$d1/$dvN only — no $w0 clobber — so they stay out of this set.)
                op == OP_DEQ || op == OP_DNE || op == OP_DLT ||
                op == OP_DLE || op == OP_DGT || op == OP_DGE ||
                // Phase 17.F.4: OP_D2I writes $w0 with the (non-constant) checked i32 result, so it
                // invalidates the ICONST reuse cache exactly like OP_F2I / a decimal compare. (I2D/
                // F2D/D2F write $d0/$f0 — no $w0 clobber — so they stay out of this set.) The decimal
                // surface ids 59/60 ride OP_CALL_RUNTIME, already in the reset set above.
                op == OP_D2I) {
                accum_has_value = false;
            }
        }

        if (!dead_code_zone) {
            if (!skip_by_cse) {
                teko_metal_route_instruction(ctx, op, arg);
            }
        }

        if (op == OP_RETURN || op == OP_HALT) {
            dead_code_zone = true;
            accum_has_value = false;
            last_arith_op = (OpCode)0;
        }
    }

    free(local_il);
}

void teko_metal_emit_program(MetalContext* ctx, const unsigned char* bytecode, uint32_t size) {
    if (!ctx || !bytecode || size == 0) return;

    teko_metal_route_instruction(ctx, OP_PROLOG, 0);
    process_linear_il_bytes(ctx, bytecode, size);
    teko_metal_route_instruction(ctx, OP_EPILOG, 0);
}

void teko_metal_close(MetalContext* ctx) {
    if (!ctx) return;
    if (ctx->file) {
        fclose(ctx->file);
    }
    free(ctx);
}
