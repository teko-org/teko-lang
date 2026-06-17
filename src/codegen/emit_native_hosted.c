#include "emit_native_hosted.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

// See emit_native_hosted.h. Self-contained, libc-hosted, assemble-able emitter for the
// host arches. Selected by ctx->hosted; the freestanding emitters are left untouched.

#define TEKO_HOSTED_STAGE_SLOTS 8 // OP_SETARG staging slots (max import/runtime arity)

// --- OP_CALL_RUNTIME dispatch table (mirrors emit_wasm.c) ------------------------
const char* teko_native_runtime_symbol(int32_t id, int* out_arity) {
    int arity = 1;
    const char* sym = NULL;
    switch (id) {
        case 0: sym = "teko_rt_base64_encode"; break;
        case 1: sym = "teko_rt_base64_decode"; break;
        case 2: sym = "teko_rt_hex_encode";    break;
        case 3: sym = "teko_rt_hex_decode";    break;
        case 4: sym = "teko_rt_sha256_hex";    break; // Phase 13.1
        case 5: sym = "teko_rt_sha512_hex";    break;
        case 10: sym = "teko_rt_sha384_hex";   break;
        case 11: sym = "teko_rt_sha3_256_hex"; break;
        case 12: sym = "teko_rt_sha3_512_hex"; break;
        case 15: sym = "teko_rt_blake3_hex";   break;
        case 16: sym = "teko_rt_blake2b_hex";  break;
        case 17: sym = "teko_rt_hmac_sha256";  arity = 2; break; // (hexKey, msg)
        case 18: sym = "teko_rt_hmac_sha384";  arity = 2; break;
        case 19: sym = "teko_rt_hmac_sha512";  arity = 2; break;
        case 20: sym = "teko_rt_aes_gcm_seal";            arity = 4; break; // (key,nonce,aad,pt)
        case 21: sym = "teko_rt_aes_gcm_open";            arity = 4; break; // (key,nonce,aad,ct‖tag)
        case 22: sym = "teko_rt_chacha20poly1305_seal";   arity = 4; break;
        case 23: sym = "teko_rt_chacha20poly1305_open";   arity = 4; break;
        case 24: sym = "teko_rt_ed25519_sign";   arity = 2; break; // (seed, msg)
        case 25: sym = "teko_rt_ed25519_verify"; arity = 3; break; // (pub, msg, sig)
        case 26: sym = "teko_rt_x25519";         arity = 2; break; // (scalar, u)
        case 27: sym = "teko_rt_hkdf_sha256";    arity = 4; break; // (ikm, salt, info, len)
        case 28: sym = "teko_rt_pbkdf2_sha256";  arity = 4; break; // (pass, salt, iters, len)
        case 29: sym = "teko_rt_ecdsa_p256_sign";   arity = 2; break; // (priv, hash)
        case 30: sym = "teko_rt_ecdsa_p256_verify"; arity = 3; break; // (pub, hash, sig)
        case 31: sym = "teko_rt_ecdsa_p384_sign";   arity = 2; break;
        case 32: sym = "teko_rt_ecdsa_p384_verify"; arity = 3; break;
        case 33: sym = "teko_rt_shake128"; arity = 2; break; // (msg, out_len)
        case 34: sym = "teko_rt_shake256"; arity = 2; break;
        case 37: sym = "teko_rt_rsa_pss_sign";     arity = 3; break; // (n, d, mhash)
        case 38: sym = "teko_rt_rsa_pss_verify";   arity = 4; break; // (n, e, mhash, sig)
        case 39: sym = "teko_rt_rsa_oaep_encrypt"; arity = 3; break; // (n, e, msg)
        case 40: sym = "teko_rt_rsa_oaep_decrypt"; arity = 3; break; // (n, d, ct)
        case 41: sym = "teko_rt_random_bytes";     arity = 1; break; // (n)
        case 42: sym = "teko_rt_uuid_v4";          arity = 1; break; // ignored arg
        case 43: sym = "teko_rt_uuid_v7";          arity = 1; break; // ignored arg
        // Phase 14 (wall-clock / timezone surface) — OS-sourced civil time (string-returning).
        case 44: sym = "teko_rt_time_now_unix";    arity = 1; break; // ignored arg
        case 45: sym = "teko_rt_time_now_local";   arity = 1; break; // ignored arg
        case 46: sym = "teko_rt_time_now_utc";     arity = 1; break; // ignored arg
        case 47: sym = "teko_rt_time_format_local"; arity = 1; break; // (epoch str)
        case 48: sym = "teko_rt_time_format_utc";  arity = 1; break; // (epoch str)
        // Phase 16 (Casting / Conversions & Parsing) — culture-invariant conversion surface.
        case 49: sym = "teko_rt_int_to_string";    arity = 1; break; // (i32) -> decimal str
        // Phase 17.D — float->string. id 50 is the ONLY f64-ARG runtime id: the value rides in the
        // FP-arg register (xmm0/d0 = $f0), NOT $w0, so OP_CALL_RUNTIME special-cases it to a BARE
        // call (no $w0->arg-reg marshal). arity 1 only describes the result-marshalling (char*->$w0).
        case 50: sym = "teko_rt_float_to_string";  arity = 1; break; // (f64) -> shortest `.`-decimal
        case 51: sym = "teko_rt_bool_to_string";   arity = 1; break; // (0/1) -> "true"/"false"
        case 52: sym = "teko_rt_str_concat";       arity = 2; break; // (a, b) -> a‖b
        case 56: sym = "teko_rt_to_radix";         arity = 2; break; // (v, radix) -> base str
        case 57: sym = "teko_rt_pad";              arity = 2; break; // (v, width) -> zero-padded
        case 58: sym = "teko_rt_group";            arity = 1; break; // (v) -> "1,000,000"
        case 53: sym = "teko_rt_parse_int";        arity = 1; break; // (str) -> i32 (checked)
        case 55: sym = "teko_rt_parse_bool";       arity = 1; break; // (str) -> 0/1 (checked)
        // Phase 17.E — string->f64. id 54 is the INVERSE of id 50's ABI: the arg is a STRING (i32 ptr
        // in $w0 -> rdi/x0 via the normal emit_call marshal), and the `double` result is returned in
        // xmm0/d0 (= $f0) AUTOMATICALLY by the SysV/AAPCS ABI. So unlike id 50 it needs NO special
        // emission — the generic `emit_call(sym, 1)` is correct (rax/$w0 is clobbered but unused; the
        // frontend reads the result from $f0 as VT_FLOAT).
        case 54: sym = "teko_rt_parse_float";      arity = 1; break; // (str) -> f64 (checked)
        // Phase 17.F.4 — decimal language surface (by-pointer ABI; OP_CALL_RUNTIME special-cases the
        // emission, but the symbol/arity feed the WASM import declaration).
        case 59: sym = "teko_rt_decimal_to_string"; arity = 1; break; // (&decimal) -> str
        case 60: sym = "teko_rt_decimal_parse";     arity = 1; break; // (str, &decimal) checked
        case 6: sym = "teko_rt_md5_hex";       break; // legacy
        case 7: sym = "teko_rt_sha1_hex";      break; // legacy
        case 8: sym = "teko_rt_uuid_v3";       break;
        case 9: sym = "teko_rt_uuid_v5";       break;
        default: break;
    }
    if (out_arity) *out_arity = arity;
    return sym;
}

// Phase 14 (14.B): OP_DUPLEX_* -> teko_rt_duplex_* symbol + arity (mirrors the WASM reactor
// import table). The duplex C runtime is the single source of truth; these wrappers adapt the
// integer-handle ABI onto it (linked from libteko_rt.a).
const char* teko_native_duplex_symbol(OpCode op, int* out_arity) {
    int arity = 1;
    const char* sym = NULL;
    switch (op) {
        case OP_DUPLEX_OPEN:  sym = "teko_rt_duplex_open";  arity = 1; break; // (capacity)
        case OP_DUPLEX_SEND:  sym = "teko_rt_duplex_send";  arity = 3; break; // (handle, ep, value)
        case OP_DUPLEX_RECV:  sym = "teko_rt_duplex_recv";  arity = 2; break; // (handle, ep)
        case OP_DUPLEX_POLL:  sym = "teko_rt_duplex_poll";  arity = 2; break; // (handle, ep)
        case OP_DUPLEX_CLOSE: sym = "teko_rt_duplex_close"; arity = 1; break; // (handle)
        default: break;
    }
    if (out_arity) *out_arity = arity;
    return sym;
}

// Phase 14 (14.C): OP_DELAYED_* -> teko_rt_delayed_* symbol + arity (mirrors the WASM reactor).
const char* teko_native_delayed_symbol(OpCode op, int* out_arity) {
    int arity = 1;
    const char* sym = NULL;
    switch (op) {
        case OP_DELAYED_OPEN:    sym = "teko_rt_delayed_open";    arity = 1; break; // (capacity)
        case OP_DELAYED_SEND:    sym = "teko_rt_delayed_send";    arity = 3; break; // (handle, value, delay)
        case OP_DELAYED_RECV:    sym = "teko_rt_delayed_recv";    arity = 1; break; // (handle)
        case OP_DELAYED_POLL:    sym = "teko_rt_delayed_poll";    arity = 1; break; // (handle)
        case OP_DELAYED_CLOSE:   sym = "teko_rt_delayed_close";   arity = 1; break; // (handle)
        default: break;
    }
    if (out_arity) *out_arity = arity;
    return sym;
}

// Phase 14 (14.D): OP_BCAST_* -> teko_rt_bcast_* symbol + arity (mirrors the WASM reactor).
const char* teko_native_bcast_symbol(OpCode op, int* out_arity) {
    int arity = 1;
    const char* sym = NULL;
    switch (op) {
        case OP_BCAST_OPEN:      sym = "teko_rt_bcast_open";      arity = 1; break; // (capacity)
        case OP_BCAST_SUBSCRIBE: sym = "teko_rt_bcast_subscribe"; arity = 1; break; // (handle)
        case OP_BCAST_PUBLISH:   sym = "teko_rt_bcast_publish";   arity = 2; break; // (handle, value)
        case OP_BCAST_RECV:      sym = "teko_rt_bcast_recv";      arity = 2; break; // (handle, sub_id)
        case OP_BCAST_POLL:      sym = "teko_rt_bcast_poll";      arity = 2; break; // (handle, sub_id)
        case OP_BCAST_CLOSE:     sym = "teko_rt_bcast_close";     arity = 1; break; // (handle)
        default: break;
    }
    if (out_arity) *out_arity = arity;
    return sym;
}

// Phase 14 (14.E): OP_SHARED_*/OP_ATOMIC_* -> teko_shared_*/teko_atomic_* symbol + arity. These
// C functions already use the register-width ABI, so they are called directly (no rt wrapper).
const char* teko_native_shared_symbol(OpCode op, int* out_arity) {
    int arity = 0;
    const char* sym = NULL;
    switch (op) {
        case OP_SHARED_ENTER: sym = "teko_shared_enter"; arity = 0; break;
        case OP_SHARED_LEAVE: sym = "teko_shared_leave"; arity = 0; break;
        case OP_ATOMIC_CELL:  sym = "teko_atomic_cell";  arity = 1; break; // (initial)
        case OP_ATOMIC_ADD:   sym = "teko_atomic_add";   arity = 2; break; // (handle, delta)
        case OP_ATOMIC_LOAD:  sym = "teko_atomic_load";  arity = 1; break; // (handle)
        case OP_ATOMIC_STORE: sym = "teko_atomic_store"; arity = 2; break; // (handle, value)
        default: break;
    }
    if (out_arity) *out_arity = arity;
    return sym;
}

// Phase 14 (14.F): OP_RETRY_*/OP_CIRCUIT_* -> teko_rt_retry_*/teko_rt_circuit_* symbol + arity
// (mirrors the WASM reactor import table). The teko_retry C policy runtime is the source of truth.
const char* teko_native_retry_symbol(OpCode op, int* out_arity) {
    int arity = 1;
    const char* sym = NULL;
    switch (op) {
        case OP_RETRY_NEW:             sym = "teko_rt_retry_new";             arity = 4; break;
        case OP_RETRY_SHOULD_CONTINUE: sym = "teko_rt_retry_should_continue"; arity = 2; break; // real clock
        case OP_RETRY_NEXT_DELAY:      sym = "teko_rt_retry_next_delay";      arity = 2; break;
        case OP_CIRCUIT_NEW:           sym = "teko_rt_circuit_new";           arity = 2; break;
        case OP_CIRCUIT_ALLOW:         sym = "teko_rt_circuit_allow";         arity = 1; break; // real clock
        case OP_CIRCUIT_RECORD:        sym = "teko_rt_circuit_record";        arity = 2; break; // real clock
        default: break;
    }
    if (out_arity) *out_arity = arity;
    return sym;
}

// Phase 15 (15.A): OP_OBJ_* -> teko_rt_object_* symbol + arity (mirrors the WASM reactor import
// table). The teko_object C runtime is the single source of truth; these wrappers adapt the
// integer-handle ABI onto it (linked from libteko_rt.a).
const char* teko_native_object_symbol(OpCode op, int* out_arity) {
    int arity = 1;
    const char* sym = NULL;
    switch (op) {
        case OP_OBJ_NEW:  sym = "teko_rt_object_new";  arity = 1; break; // (nfields)
        case OP_OBJ_SET:  sym = "teko_rt_object_set";  arity = 3; break; // (handle, idx, value)
        case OP_OBJ_GET:  sym = "teko_rt_object_get";  arity = 2; break; // (handle, idx)
        case OP_OBJ_FREE: sym = "teko_rt_object_free"; arity = 1; break; // (handle)
        default: break;
    }
    if (out_arity) *out_arity = arity;
    return sym;
}

// Phase 18 (18.E.1): OP_ARR_* -> teko_rt_array_* symbol + arity (mirrors the WASM reactor import
// table). The teko_array C runtime is the single source of truth (linked from libteko_rt.a); the
// get/set wrappers are CHECKED FAIL-LOUD on an out-of-range index (the wrapper aborts).
const char* teko_native_array_symbol(OpCode op, int* out_arity) {
    int arity = 1;
    const char* sym = NULL;
    switch (op) {
        case OP_ARR_NEW: sym = "teko_rt_array_new"; arity = 1; break; // (n)
        case OP_ARR_GET: sym = "teko_rt_array_get"; arity = 2; break; // (handle, idx)
        case OP_ARR_SET: sym = "teko_rt_array_set"; arity = 3; break; // (handle, idx, value)
        case OP_ARR_LEN: sym = "teko_rt_array_len"; arity = 1; break; // (handle)
        default: break;
    }
    if (out_arity) *out_arity = arity;
    return sym;
}

// Phase 18 (18.E.2): OP_IARR_* -> teko_rt_iarray_* symbol + arity (mirrors the WASM reactor import
// table). The teko_iarray C runtime (PACKED int32 cells, the SIMD substrate) is the single source of
// truth (linked from libteko_rt.a); the get/set wrappers are CHECKED FAIL-LOUD on an out-of-range
// index (the wrapper aborts).
const char* teko_native_iarray_symbol(OpCode op, int* out_arity) {
    int arity = 1;
    const char* sym = NULL;
    switch (op) {
        case OP_IARR_NEW: sym = "teko_rt_iarray_new"; arity = 1; break; // (n)
        case OP_IARR_GET: sym = "teko_rt_iarray_get"; arity = 2; break; // (handle, idx)
        case OP_IARR_SET: sym = "teko_rt_iarray_set"; arity = 3; break; // (handle, idx, value)
        case OP_IARR_LEN: sym = "teko_rt_iarray_len"; arity = 1; break; // (handle)
        default: break;
    }
    if (out_arity) *out_arity = arity;
    return sym;
}

// Phase 15 (15.B): OP_VTABLE_* -> teko_rt_vtable_* symbol + arity (mirrors the WASM reactor import
// table). The teko_vtable C runtime is the single source of truth (linked from libteko_rt.a).
const char* teko_native_vtable_symbol(OpCode op, int* out_arity) {
    int arity = 2;
    const char* sym = NULL;
    switch (op) {
        case OP_VTABLE_SET: sym = "teko_rt_vtable_set"; arity = 3; break; // (type_id, method_id, slot)
        case OP_VTABLE_GET: sym = "teko_rt_vtable_get"; arity = 2; break; // (type_id, method_id)
        default: break;
    }
    if (out_arity) *out_arity = arity;
    return sym;
}

// --- helpers ---------------------------------------------------------------------
static int is_macho(const MetalContext* ctx) { return ctx->target.os == OS_MACOS_DARWIN; }
static int is_arm64(const MetalContext* ctx) {
    return ctx->target.arch == ARCH_ARM64 || ctx->target.arch == ARCH_APPLE_SILICON;
}
static const char* sym_prefix(const MetalContext* ctx) { return is_macho(ctx) ? "_" : ""; }

// Stack-frame geometry (recomputed identically at PROLOG and EPILOG; deterministic).
static int frame_locals(const MetalContext* ctx) {
    return ctx->wasm_local_count > 0 ? ctx->wasm_local_count : 0;
}
// Phase 17.F.3: the 256-byte decimal value-model slots. The decimal accumulator $d0/$d1 (slots
// 0/1) + one 256-byte decimal-local slot per integer-local slot index (slots 2..2+frame_locals-1,
// the $dvN file parallel to $vN — a slot has one type per use, like floats). Only present when the
// program uses decimals (wasm_emit_decimal), so decimal-free frames stay byte-identical. 256 is
// 16-aligned, so the region keeps every existing alignment invariant unchanged.
#define TEKO_HOSTED_DECIMAL_BYTES 256
// Slot layout: $d0=0, $d1=1, a CMP-scratch i32 sink slot=2, then per-local slots 3..3+nlocals-1.
#define TEKO_HOSTED_DECIMAL_SCRATCH 2
static int decimal_slot_count(const MetalContext* ctx) {
    return ctx->wasm_emit_decimal ? (3 + frame_locals(ctx)) : 0; // $d0,$d1,cmp-scratch + per-local
}
static int decimal_region_bytes(const MetalContext* ctx) {
    return decimal_slot_count(ctx) * TEKO_HOSTED_DECIMAL_BYTES;
}
static int frame_size_x86(const MetalContext* ctx) {
    int slots = frame_locals(ctx) + TEKO_HOSTED_STAGE_SLOTS + 1; // +1 = the spawn-args pointer slot
    int f = 8 + 8 * slots;        // first slot at rbp-16 (rbx occupies rbp-8)
    f += decimal_region_bytes(ctx); // Phase 17.F.3: 256-byte decimal slots below the GPR slots
    if (f % 16 != 8) f += 8;      // after `push rbp; push rbx`, sub F keeps rsp%16==0 iff F%16==8
    return f;
}
static int frame_size_arm(const MetalContext* ctx) {
    int slots = frame_locals(ctx) + TEKO_HOSTED_STAGE_SLOTS + 1; // +1 = the spawn-args pointer slot
    int f = 32 + 8 * slots;       // saved x29/x30/x19 occupy 0/8/16
    f += decimal_region_bytes(ctx); // Phase 17.F.3: 256-byte decimal slots above the GPR slots
    return (f + 15) & ~15;        // 16-aligned for stp/AAPCS
}
static int local_off_x86(int n) { return -(16 + 8 * n); }
static int stage_off_x86(const MetalContext* ctx, int i) {
    return -(16 + 8 * frame_locals(ctx) + 8 * i);
}
static int local_off_arm(int n) { return 32 + 8 * n; }
static int stage_off_arm(const MetalContext* ctx, int i) {
    return 32 + 8 * frame_locals(ctx) + 8 * i;
}
// Phase 17.F.3: rbp/x29-relative byte offset of the START (lowest address) of decimal slot `k`
// ($d0=0, $d1=1, decimal-local n = 2+n). x86 stacks DOWN: the GPR slots end at the anchor below;
// the decimal region runs further negative. arm stacks UP from x29: decimal slots sit above the
// GPR area. `lea`/`add` this offset into an ABI arg register to pass &slot by pointer.
static int hosted_gpr_anchor(const MetalContext* ctx) {
    return 16 + 8 * (frame_locals(ctx) + TEKO_HOSTED_STAGE_SLOTS + 1); // one past the last GPR slot
}
static int decimal_slot_off_x86(const MetalContext* ctx, int k) {
    return -(hosted_gpr_anchor(ctx) + TEKO_HOSTED_DECIMAL_BYTES * (k + 1));
}
static int decimal_slot_off_arm(const MetalContext* ctx, int k) {
    return hosted_gpr_anchor(ctx) + TEKO_HOSTED_DECIMAL_BYTES * k;
}
// Phase 14 (14.I): the reserved slot holding the routine's spawn-args pointer (one past the stage
// slots), so OP_LOAD_SPAWN_ARG can read args[idx] anywhere in the body (callee-saved across calls).
static int argsptr_off_x86(const MetalContext* ctx) { return stage_off_x86(ctx, TEKO_HOSTED_STAGE_SLOTS); }
static int argsptr_off_arm(const MetalContext* ctx) { return stage_off_arm(ctx, TEKO_HOSTED_STAGE_SLOTS); }

// Emit one C string into the active data section as an escaped .asciz directive.
static void emit_asciz(FILE* f, const char* s) {
    fputs("    .asciz \"", f);
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
        unsigned char c = *p;
        if (c == '\\' || c == '"') { fputc('\\', f); fputc(c, f); }
        else if (c >= 0x20 && c < 0x7f) { fputc(c, f); }
        else { fprintf(f, "\\%03o", c); } // octal escape — portable across GAS/clang
    }
    fputs("\"\n", f);
}

// SysV/AAPCS argument registers.
static const char* X86_ARG_REGS[6] = { "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9" };

// Emit a marshalled call: staging slots 0..arity-2 -> arg regs 0..arity-2, the live
// accumulator ($w0) -> arg reg arity-1, then call `sym`. Result (if any) stays in $w0.
static void emit_call(MetalContext* ctx, const char* sym, int arity) {
    FILE* f = ctx->file;
    const char* pre = sym_prefix(ctx);
    if (arity < 0) arity = 0;
    if (arity > TEKO_HOSTED_STAGE_SLOTS) arity = TEKO_HOSTED_STAGE_SLOTS;

    if (is_arm64(ctx)) {
        // live arg ($w0 = x0) -> last reg; skip the x0->x0 self-move for arity 1.
        if (arity >= 2) fprintf(f, "    mov x%d, x0\n", arity - 1);
        for (int i = 0; i < arity - 1; i++)
            fprintf(f, "    ldr x%d, [x29, #%d]\n", i, stage_off_arm(ctx, i));
        fprintf(f, "    bl %s%s\n", pre, sym);
    } else {
        if (arity >= 1) fprintf(f, "    movq %%rax, %s\n", X86_ARG_REGS[arity - 1]);
        for (int i = 0; i < arity - 1; i++)
            fprintf(f, "    movq %d(%%rbp), %s\n", stage_off_x86(ctx, i), X86_ARG_REGS[i]);
        // PLT on ELF (PIE-safe); direct on Mach-O (PC-relative stub).
        fprintf(f, "    call %s%s%s\n", pre, sym, is_macho(ctx) ? "" : "@PLT");
    }
}

// Emit the register-save prolog (identical frame for $main and every routine): set up the
// frame pointer + saved-register area sized by frame_size_*.
static void emit_frame_enter(MetalContext* ctx) {
    FILE* f = ctx->file;
    if (is_arm64(ctx)) {
        int fr = frame_size_arm(ctx);
        // Phase 17.F.3: the decimal frame region can push `fr` past the `stp …, [sp, #-imm]!`
        // pre-index range (±504, multiple of 8). For a large frame, allocate the stack with a
        // separate `sub sp` (12-bit imm, optionally shifted — emit two subs above 4095) and store
        // the saved pair at [sp, #0]. Small frames keep the original single pre-indexed `stp`
        // (byte-identical to before 17.F.3 when the program is decimal-free).
        if (fr <= 504) {
            fprintf(f, "    stp x29, x30, [sp, #-%d]!\n", fr);
            fprintf(f, "    mov x29, sp\n");
        } else {
            if (fr > 4095) fprintf(f, "    sub sp, sp, #%d, lsl #12\n", fr >> 12);
            fprintf(f, "    sub sp, sp, #%d\n", fr & 0xFFF);
            fprintf(f, "    stp x29, x30, [sp]\n");
            fprintf(f, "    mov x29, sp\n");
        }
        fprintf(f, "    str x19, [x29, #16]\n");
    } else {
        int fr = frame_size_x86(ctx);
        fprintf(f, "    pushq %%rbp\n");
        fprintf(f, "    movq %%rsp, %%rbp\n");
        fprintf(f, "    pushq %%rbx\n");
        fprintf(f, "    subq $%d, %%rsp\n", fr);
    }
}

// Emit the matching epilog + return (recomputes the frame size identically).
static void emit_frame_leave(MetalContext* ctx) {
    FILE* f = ctx->file;
    if (is_arm64(ctx)) {
        int fr = frame_size_arm(ctx);
        fprintf(f, "    ldr x19, [x29, #16]\n");
        // Phase 17.F.3: mirror the prologue's large-frame split (see emit_frame_enter).
        if (fr <= 504) {
            fprintf(f, "    ldp x29, x30, [sp], #%d\n", fr);
        } else {
            fprintf(f, "    ldp x29, x30, [sp]\n");
            fprintf(f, "    add sp, sp, #%d\n", fr & 0xFFF);
            if (fr > 4095) fprintf(f, "    add sp, sp, #%d, lsl #12\n", fr >> 12);
        }
        fprintf(f, "    ret\n");
    } else {
        int fr = frame_size_x86(ctx);
        fprintf(f, "    addq $%d, %%rsp\n", fr);
        fprintf(f, "    popq %%rbx\n");
        fprintf(f, "    popq %%rbp\n");
        fprintf(f, "    ret\n");
    }
}

// Emit a no-argument runtime call (used for the Phase 14 scheduler drain teko_rt_run).
static void emit_call_noarg(MetalContext* ctx, const char* sym) {
    FILE* f = ctx->file;
    const char* pre = sym_prefix(ctx);
    if (is_arm64(ctx)) fprintf(f, "    bl %s%s\n", pre, sym);
    else               fprintf(f, "    call %s%s%s\n", pre, sym, is_macho(ctx) ? "" : "@PLT");
}

// Phase 14 (14.A): emit the routine function-pointer table + count the native scheduler
// (teko_rt_sched.c) walks. Slots are dense (0..count-1) in declaration order, matching the
// frontend's collect_functions slot assignment and the OP_SPAWN_ASYNC slot operand.
static void emit_routine_table(MetalContext* ctx) {
    FILE* f = ctx->file;
    const char* pre = sym_prefix(ctx);
    int n = ctx->wasm_routine_count;
    if (is_macho(ctx)) fprintf(f, "    .section __DATA,__data\n");
    else               fprintf(f, "    .data\n");
    fprintf(f, "    .p2align 3\n");
    fprintf(f, "    .globl %steko_routine_table\n", pre);
    fprintf(f, "%steko_routine_table:\n", pre);
    for (int k = 0; k < n && k < 64; k++)
        fprintf(f, "    .quad %steko_routine_%d\n", pre, ctx->wasm_routine_ids[k]);
    if (n <= 0) fprintf(f, "    .quad 0\n"); // never empty (defensive)
    fprintf(f, "    .globl %steko_routine_count\n", pre);
    fprintf(f, "%steko_routine_count:\n", pre);
    fprintf(f, "    .quad %d\n", n);
}

// Phase 18 (18.E.4): lower OP_SIMD_SUM. On entry $w0 (rax/x0) holds the typed i32[] run HANDLE.
// Three steps, minding register clobbering across the helper calls (handle + data ptr are saved on
// the frame stage slots — callee-saved across calls): (1) data ptr = teko_rt_iarray_data(handle);
// (2) len = teko_rt_iarray_len(handle); (3) sum = teko_simd_sum_i32(ptr, len) -> $w0. The kernel is
// the REAL per-ISA vector loop emitted ONCE per module (emit_simd_kernel).
static void emit_simd_sum_lowering(MetalContext* ctx) {
    FILE* f = ctx->file;
    const char* pre = sym_prefix(ctx);
    // Reuse two staging slots as callee-saved scratch (no OP_SETARG is live during this lowering):
    // slot 0 = handle, slot 1 = data ptr. They survive the helper calls (frame-relative, not regs).
    if (is_arm64(ctx)) {
        fprintf(f, "    str x0, [x29, #%d]\n", stage_off_arm(ctx, 0));   // save handle
        fprintf(f, "    bl %steko_rt_iarray_data\n", pre);               // x0 = data ptr
        fprintf(f, "    str x0, [x29, #%d]\n", stage_off_arm(ctx, 1));   // save data ptr
        fprintf(f, "    ldr x0, [x29, #%d]\n", stage_off_arm(ctx, 0));   // reload handle
        fprintf(f, "    bl %steko_rt_iarray_len\n", pre);                // x0 = length
        fprintf(f, "    mov x1, x0\n");                                  // arg1 = length
        fprintf(f, "    ldr x0, [x29, #%d]\n", stage_off_arm(ctx, 1));   // arg0 = data ptr
        fprintf(f, "    bl %steko_simd_sum_i32\n", pre);                 // x0 = sum
    } else {
        fprintf(f, "    movq %%rax, %d(%%rbp)\n", stage_off_x86(ctx, 0));         // save handle
        fprintf(f, "    movq %%rax, %%rdi\n");
        fprintf(f, "    call %steko_rt_iarray_data%s\n", pre, is_macho(ctx) ? "" : "@PLT"); // rax = data ptr
        fprintf(f, "    movq %%rax, %d(%%rbp)\n", stage_off_x86(ctx, 1));         // save data ptr
        fprintf(f, "    movq %d(%%rbp), %%rdi\n", stage_off_x86(ctx, 0));         // reload handle
        fprintf(f, "    call %steko_rt_iarray_len%s\n", pre, is_macho(ctx) ? "" : "@PLT");  // rax = length
        fprintf(f, "    movq %%rax, %%rsi\n");                                    // arg1 = length
        fprintf(f, "    movq %d(%%rbp), %%rdi\n", stage_off_x86(ctx, 1));         // arg0 = data ptr
        fprintf(f, "    call %steko_simd_sum_i32%s\n", pre, is_macho(ctx) ? "" : "@PLT");   // rax = sum
    }
}

// Phase 18 (18.E.4): emit the REAL per-ISA vector reduction kernel `teko_simd_sum_i32(ptr, n) -> sum`
// ONCE per module (gated on wasm_emit_simd). 4-wide vector accumulate -> collapse 4 partials ->
// scalar tail (N not a multiple of 4). x86_64 = SSE2 (movdqu/paddd); arm64 = NEON (ld1/add v.4s/addv);
// EVERY OTHER hosted arch (riscv64, …) = an HONEST SCALAR loop under the same symbol (RVV is documented
// future work). All three are LEAF functions (no calls; the x86 path balances its own subq/addq $16),
// so no frame is needed. The kernel is correctness-checked by the in-program scalar self-check + CI on
// both ISAs (the x86 SSE2 path is validated on CI Linux x86_64, the arm64 NEON path on macOS arm64).
static int hosted_is_x86_64(const MetalContext* ctx) {
    return ctx->target.arch == ARCH_X86_64;
}
static void emit_simd_kernel(MetalContext* ctx) {
    FILE* f = ctx->file;
    const char* pre = sym_prefix(ctx);
    int arm = is_arm64(ctx);
    int x86 = hosted_is_x86_64(ctx);
    fprintf(f, "\n    .p2align %d\n", arm ? 2 : 4);
    fprintf(f, "    .globl %steko_simd_sum_i32\n", pre);
    fprintf(f, "%steko_simd_sum_i32:\n", pre);
    if (x86) {
        // SSE2 (AT&T): rdi=ptr, rsi=n -> rax. 4-wide paddd accumulate, collapse via stack, scalar tail.
        fprintf(f, "    pxor    %%xmm0, %%xmm0\n");
        fprintf(f, "    xorq    %%rax, %%rax\n");
        fprintf(f, "    xorq    %%r8, %%r8\n");
        fprintf(f, "    movq    %%rsi, %%r9\n");
        fprintf(f, "    andq    $-4, %%r9\n");
        fprintf(f, ".Lsimdv_0:\n");
        fprintf(f, "    cmpq    %%r9, %%r8\n");
        fprintf(f, "    jge     .Lsimdve_0\n");
        fprintf(f, "    movdqu  (%%rdi,%%r8,4), %%xmm1\n");
        fprintf(f, "    paddd   %%xmm1, %%xmm0\n");
        fprintf(f, "    addq    $4, %%r8\n");
        fprintf(f, "    jmp     .Lsimdv_0\n");
        fprintf(f, ".Lsimdve_0:\n");
        fprintf(f, "    subq    $16, %%rsp\n");
        fprintf(f, "    movdqu  %%xmm0, (%%rsp)\n");
        fprintf(f, "    movl    (%%rsp), %%eax\n");
        fprintf(f, "    addl    4(%%rsp), %%eax\n");
        fprintf(f, "    addl    8(%%rsp), %%eax\n");
        fprintf(f, "    addl    12(%%rsp), %%eax\n");
        fprintf(f, "    addq    $16, %%rsp\n");
        fprintf(f, ".Lsimdt_0:\n");
        fprintf(f, "    cmpq    %%rsi, %%r8\n");
        fprintf(f, "    jge     .Lsimdte_0\n");
        fprintf(f, "    addl    (%%rdi,%%r8,4), %%eax\n");
        fprintf(f, "    addq    $1, %%r8\n");
        fprintf(f, "    jmp     .Lsimdt_0\n");
        fprintf(f, ".Lsimdte_0:\n");
        fprintf(f, "    ret\n");
    } else if (arm) {
        // NEON (GAS): x0=ptr, x1=n -> w0. 4-wide add v.4s accumulate, addv collapse, scalar tail.
        fprintf(f, "    movi    v0.4s, #0\n");
        fprintf(f, "    mov     x2, #0\n");
        fprintf(f, "    and     x3, x1, #-4\n");
        fprintf(f, ".Lsimdv_0:\n");
        fprintf(f, "    cmp     x2, x3\n");
        fprintf(f, "    b.ge    .Lsimdve_0\n");
        fprintf(f, "    add     x4, x0, x2, lsl #2\n");
        fprintf(f, "    ld1     {v1.4s}, [x4]\n");
        fprintf(f, "    add     v0.4s, v0.4s, v1.4s\n");
        fprintf(f, "    add     x2, x2, #4\n");
        fprintf(f, "    b       .Lsimdv_0\n");
        fprintf(f, ".Lsimdve_0:\n");
        fprintf(f, "    addv    s0, v0.4s\n");
        fprintf(f, "    fmov    w5, s0\n");
        fprintf(f, ".Lsimdt_0:\n");
        fprintf(f, "    cmp     x2, x1\n");
        fprintf(f, "    b.ge    .Lsimdte_0\n");
        fprintf(f, "    add     x4, x0, x2, lsl #2\n");
        fprintf(f, "    ldr     w6, [x4]\n");
        fprintf(f, "    add     w5, w5, w6\n");
        fprintf(f, "    add     x2, x2, #1\n");
        fprintf(f, "    b       .Lsimdt_0\n");
        fprintf(f, ".Lsimdte_0:\n");
        fprintf(f, "    mov     w0, w5\n");
        fprintf(f, "    ret\n");
    } else {
        // HONEST SCALAR fallback (riscv64, any non-x86/arm64 hosted arch): plain summing loop under the
        // SAME symbol. RVV is documented future work — this keeps the surface correct everywhere. The
        // riscv64 ABI (a0=ptr, a1=n -> a0) is the System V LP64 convention emit_native_hosted targets.
        // t0=ptr (preserved), t1=i, t2=&cell/cell; the result accumulates in a0.
        fprintf(f, "    mv      t0, a0\n");        // t0 = ptr
        fprintf(f, "    li      a0, 0\n");         // sum = 0
        fprintf(f, "    li      t1, 0\n");         // i = 0
        fprintf(f, ".Lsimds_0:\n");
        fprintf(f, "    bge     t1, a1, .Lsimdse_0\n");
        fprintf(f, "    slli    t2, t1, 2\n");     // t2 = i*4
        fprintf(f, "    add     t2, t0, t2\n");    // t2 = &cell[i]
        fprintf(f, "    lw      t2, 0(t2)\n");     // t2 = cell[i]
        fprintf(f, "    addw    a0, a0, t2\n");    // sum += cell[i] (32-bit add)
        fprintf(f, "    addi    t1, t1, 1\n");
        fprintf(f, "    j       .Lsimds_0\n");
        fprintf(f, ".Lsimdse_0:\n");
        fprintf(f, "    ret\n");
    }
}

// Phase 17.F.3: inline 256-byte copy between two decimal STACK slots (a decimal is a 256-byte
// VALUE TYPE — copy on store/assign). Uses ONLY scratch registers (x86 r8/r9/r10/r11; arm64
// x9/x10/x11/x12) so it never clobbers $w0/$w1 (rax/rbx, x0/x19) — the integer accumulator and the
// CSE invariant survive (DCONST/DSTORE/DLOAD are pure CSE barriers, not $w0-cache resets). 256/8 = 32
// quad copies via a counted loop. `src_k`/`dst_k` are decimal slot indices ($d0=0, $d1=1, dvN=2+n).
static void emit_decimal_slot_copy(MetalContext* ctx, int dst_k, int src_k) {
    FILE* f = ctx->file;
    int arm = is_arm64(ctx);
    int id = ctx->cf_id_next++;
    if (arm) {
        fprintf(f, "    add x9, x29, #%d\n", decimal_slot_off_arm(ctx, src_k));   // src
        fprintf(f, "    add x10, x29, #%d\n", decimal_slot_off_arm(ctx, dst_k));  // dst
        fprintf(f, "    mov w11, #32\n");
        fprintf(f, ".Ldcpy_%d:\n", id);
        fprintf(f, "    ldr x12, [x9], #8\n    str x12, [x10], #8\n");
        fprintf(f, "    subs w11, w11, #1\n    b.ne .Ldcpy_%d\n", id);
    } else {
        fprintf(f, "    leaq %d(%%rbp), %%r11\n", decimal_slot_off_x86(ctx, src_k)); // src
        fprintf(f, "    leaq %d(%%rbp), %%r10\n", decimal_slot_off_x86(ctx, dst_k)); // dst
        fprintf(f, "    movl $32, %%r8d\n");
        fprintf(f, ".Ldcpy_%d:\n", id);
        fprintf(f, "    movq (%%r11), %%r9\n    movq %%r9, (%%r10)\n");
        fprintf(f, "    addq $8, %%r11\n    addq $8, %%r10\n");
        fprintf(f, "    decl %%r8d\n    jnz .Ldcpy_%d\n", id);
    }
}

// Phase 17.F.3: load &(decimal slot `k`) into ABI arg register `argreg` (0=rdi/x0, 1=rsi/x1,
// 2=rdx/x2) — the by-pointer ABI for the teko_rt_decimal_* calls. Scratch-free (only the arg reg).
static void emit_decimal_slot_addr(MetalContext* ctx, int argreg, int k) {
    FILE* f = ctx->file;
    if (is_arm64(ctx)) fprintf(f, "    add x%d, x29, #%d\n", argreg, decimal_slot_off_arm(ctx, k));
    else {
        static const char* R[3] = { "%rdi", "%rsi", "%rdx" };
        fprintf(f, "    leaq %d(%%rbp), %s\n", decimal_slot_off_x86(ctx, k), R[argreg]);
    }
}

// --- the emitter -----------------------------------------------------------------
void emit_native_hosted(MetalContext* ctx, OpCode op, int32_t arg) {
    if (!ctx || !ctx->file) return;
    FILE* f = ctx->file;
    int arm = is_arm64(ctx);
    const char* pre = sym_prefix(ctx);

    switch (op) {
        case OP_PROLOG: {
            // Comment leader: '#' for x86 AT&T/GAS, '//' for arm64 (both ELF & Mach-O).
            // ';;' is invalid on the GNU x86_64 assembler, so do not reuse the
            // freestanding emitters' banner style here (this output is really assembled).
            const char* cm = arm ? "//" : "#";
            fprintf(f, "%s ==================================================\n", cm);
            fprintf(f, "%s Teko AOT Compiler - Hosted Native Runner (%s)\n", cm, ctx->target.target_string);
            fprintf(f, "%s ==================================================\n\n", cm);
            // Constant string pool -> read-only data.
            if (ctx->wasm_string_count > 0 && ctx->wasm_strings) {
                if (is_macho(ctx)) fprintf(f, "    .section __TEXT,__cstring,cstring_literals\n");
                else               fprintf(f, "    .section .rodata\n");
                for (int i = 0; i < ctx->wasm_string_count; i++) {
                    fprintf(f, ".L_str_%d:\n", i);
                    emit_asciz(f, ctx->wasm_strings[i] ? ctx->wasm_strings[i] : "");
                }
                fprintf(f, "\n");
            }
            // Phase 17.F.3: the decimal-constant pool -> read-only data, one 256-byte blob per
            // OP_DCONST index (.L_dec_<idx>), 8-byte aligned (teko_decimal is _Alignof 8). DCONST
            // copies 256 bytes from here into the $d0 stack slot. Emitted only when the program uses
            // decimals (wasm_emit_decimal) so decimal-free output stays byte-identical.
            if (ctx->wasm_emit_decimal && ctx->decimal_count > 0 && ctx->decimal_pool) {
                if (is_macho(ctx)) fprintf(f, "    .section __TEXT,__const\n");
                else               fprintf(f, "    .section .rodata\n");
                fprintf(f, "    .p2align 3\n");
                for (int i = 0; i < ctx->decimal_count; i++) {
                    const unsigned char* blob = ctx->decimal_pool + (size_t)256 * i;
                    fprintf(f, ".L_dec_%d:\n", i);
                    for (int b = 0; b < 256; b++) {
                        if ((b & 15) == 0) fprintf(f, "    .byte ");
                        fprintf(f, "%u%s", (unsigned)blob[b], ((b & 15) == 15 || b == 255) ? "\n" : ",");
                    }
                }
                fprintf(f, "\n");
            }
            // Text section + main.
            fprintf(f, "    .text\n");
            fprintf(f, "    .globl %smain\n", pre);
            fprintf(f, "    .p2align %d\n", arm ? 2 : 4);
            fprintf(f, "%smain:\n", pre);
            emit_frame_enter(ctx);
            fprintf(f, "\n");
            ctx->wasm_open = 1;          // $main open (Phase 14 multi-function tracking)
            ctx->wasm_routine_count = 0; // routine table accumulated at FUNC_BEGIN
            break;
        }

        case OP_HALT:
            // Phase 14: drain fired background tasks before $main returns (run-to-completion).
            // The frame is still live here; the actual ret is emitted when $main closes.
            if (ctx->wasm_emit_spawn) emit_call_noarg(ctx, "teko_rt_run");
            // Set the process exit status to 0; the actual teardown/ret is at the close.
            if (arm) fprintf(f, "    mov w0, #0\n");
            else     fprintf(f, "    movl $0, %%eax\n");
            break;

        // Phase 14 (14.A): green-thread body boundaries. Each routine is emitted as a
        // separate native function `teko_routine_<slot>(long args)` AFTER $main returns, so
        // FUNC_BEGIN first closes the open function (main → ret, or the previous routine),
        // then opens the new one. On entry the arg register holds the spawn-args pointer (Phase
        // 14.I); save it to the reserved frame slot so OP_LOAD_SPAWN_ARG can read args[idx] in the
        // body. $w0 also gets it (legacy: arg-less/single-arg routines that ignore it).
        case OP_FUNC_BEGIN:
            if (ctx->wasm_open == 1)      emit_frame_leave(ctx); // close $main
            else if (ctx->wasm_open == 2) emit_frame_leave(ctx); // close previous routine
            fprintf(f, "\n    .p2align %d\n", arm ? 2 : 4);
            fprintf(f, "%steko_routine_%d:\n", pre, arg);
            emit_frame_enter(ctx);
            if (arm) {
                fprintf(f, "    str x0, [x29, #%d]\n", argsptr_off_arm(ctx)); // save spawn-args ptr
            } else {
                fprintf(f, "    movq %%rdi, %d(%%rbp)\n", argsptr_off_x86(ctx)); // save spawn-args ptr
                fprintf(f, "    movq %%rdi, %%rax\n");                            // $w0 = args ptr (legacy)
            }
            if (ctx->wasm_routine_count < 64)
                ctx->wasm_routine_ids[ctx->wasm_routine_count] = arg;
            ctx->wasm_routine_count++;
            ctx->wasm_open = 2;
            break;

        case OP_FUNC_END:
            if (ctx->wasm_open == 2) { emit_frame_leave(ctx); ctx->wasm_open = 0; }
            break;

        case OP_EPILOG:
        case OP_RETURN:
            // Close whatever function is still open ($main when the program has no routines;
            // already 0 for a routines program — $main closed at the first FUNC_BEGIN).
            if (ctx->wasm_open != 0) { emit_frame_leave(ctx); ctx->wasm_open = 0; }
            // Phase 14: emit the routine function-pointer table the scheduler walks.
            if (ctx->wasm_emit_spawn) emit_routine_table(ctx);
            // Phase 18 (18.E.4): emit the REAL per-ISA vector reduction kernel ONCE per module (a leaf
            // text-section function after $main + the routine bodies), gated on wasm_emit_simd so
            // simd-free output stays byte-identical.
            if (ctx->wasm_emit_simd) emit_simd_kernel(ctx);
            break;

        // Phase 14 (14.A): fire the routine whose table slot is in $w0 as a background task.
        case OP_SPAWN_ASYNC:
            if (arm) {
                fprintf(f, "    mov x1, #0\n");        // arg = 0 (slot already in x0 = $w0)
                fprintf(f, "    bl %steko_rt_spawn\n", pre);
            } else {
                fprintf(f, "    movq %%rax, %%rdi\n");  // slot
                fprintf(f, "    xorl %%esi, %%esi\n");  // arg = 0
                fprintf(f, "    call %steko_rt_spawn%s\n", pre, is_macho(ctx) ? "" : "@PLT");
            }
            break;

        // Phase 14 (14.I): fire a routine with `arg` (=argc) staged arguments. Push each staged
        // arg (stage slot i) into the scheduler's pending-args buffer via teko_rt_spawn_setarg(i,v),
        // then teko_rt_spawn_args(slot=$w0) captures them + enqueues. The task receives an args
        // pointer it reads with OP_LOAD_SPAWN_ARG.
        case OP_SPAWN_ASYNC_ARGS: {
            int argc = arg;
            // Preserve the slot ($w0) in the callee-saved $w1 across the setarg calls (each clobbers
            // the accumulator register), then restore it for teko_rt_spawn_args.
            if (arm) fprintf(f, "    mov x19, x0\n");
            else     fprintf(f, "    movq %%rax, %%rbx\n");
            for (int k = 0; k < argc; k++) {
                if (arm) {
                    fprintf(f, "    mov x0, #%d\n", k);
                    fprintf(f, "    ldr x1, [x29, #%d]\n", stage_off_arm(ctx, k));
                    fprintf(f, "    bl %steko_rt_spawn_setarg\n", pre);
                } else {
                    fprintf(f, "    movl $%d, %%edi\n", k);
                    fprintf(f, "    movq %d(%%rbp), %%rsi\n", stage_off_x86(ctx, k));
                    fprintf(f, "    call %steko_rt_spawn_setarg%s\n", pre, is_macho(ctx) ? "" : "@PLT");
                }
            }
            if (arm) {
                fprintf(f, "    mov x0, x19\n");                  // slot
                fprintf(f, "    bl %steko_rt_spawn_args\n", pre);
            } else {
                fprintf(f, "    movq %%rbx, %%rdi\n");            // slot
                fprintf(f, "    call %steko_rt_spawn_args%s\n", pre, is_macho(ctx) ? "" : "@PLT");
            }
            break;
        }

        // Phase 15 (15.A): SYNCHRONOUS table call (method dispatch). Same arg staging as
        // OP_SPAWN_ASYNC_ARGS (slot in $w0, args in the staging slots), but instead of enqueuing
        // we call teko_rt_call(slot), which invokes teko_routine_table[slot] with the staged args
        // and RETURNS its result (in rax/x0 = $w0). The native routine epilogue leaves the body's
        // last $w0 in rax across `ret`, so teko_rt_call propagates it back here.
        case OP_CALL_FUNC: {
            int argc = arg;
            if (arm) fprintf(f, "    mov x19, x0\n");             // preserve slot across setarg calls
            else     fprintf(f, "    movq %%rax, %%rbx\n");
            for (int k = 0; k < argc; k++) {
                if (arm) {
                    fprintf(f, "    mov x0, #%d\n", k);
                    fprintf(f, "    ldr x1, [x29, #%d]\n", stage_off_arm(ctx, k));
                    fprintf(f, "    bl %steko_rt_spawn_setarg\n", pre);
                } else {
                    fprintf(f, "    movl $%d, %%edi\n", k);
                    fprintf(f, "    movq %d(%%rbp), %%rsi\n", stage_off_x86(ctx, k));
                    fprintf(f, "    call %steko_rt_spawn_setarg%s\n", pre, is_macho(ctx) ? "" : "@PLT");
                }
            }
            if (arm) {
                fprintf(f, "    mov x0, x19\n");                  // slot
                fprintf(f, "    bl %steko_rt_call\n", pre);       // $w0 = result
            } else {
                fprintf(f, "    movq %%rbx, %%rdi\n");            // slot
                fprintf(f, "    call %steko_rt_call%s\n", pre, is_macho(ctx) ? "" : "@PLT");
            }
            break;
        }

        // Phase 14 (14.I): in a routine, $w0 = the idx-th spawn argument (args pointer is in the
        // reserved frame slot; each arg is a register-width long).
        case OP_LOAD_SPAWN_ARG:
            if (arm) {
                fprintf(f, "    ldr x1, [x29, #%d]\n", argsptr_off_arm(ctx)); // args ptr
                fprintf(f, "    ldr x0, [x1, #%d]\n", 8 * arg);               // args[idx]
            } else {
                fprintf(f, "    movq %d(%%rbp), %%rcx\n", argsptr_off_x86(ctx)); // args ptr
                fprintf(f, "    movq %d(%%rcx), %%rax\n", 8 * arg);             // args[idx]
            }
            break;

        case OP_ICONST:
            if (arm) {
                uint32_t v = (uint32_t)arg;
                fprintf(f, "    movz w0, #%u\n", v & 0xFFFFu);
                if (v >> 16) fprintf(f, "    movk w0, #%u, lsl #16\n", v >> 16);
            } else {
                fprintf(f, "    movl $%d, %%eax\n", arg);
            }
            break;

        case OP_SCONST:
            if (arm) {
                if (is_macho(ctx)) {
                    fprintf(f, "    adrp x0, .L_str_%d@PAGE\n", arg);
                    fprintf(f, "    add x0, x0, .L_str_%d@PAGEOFF\n", arg);
                } else {
                    fprintf(f, "    adrp x0, .L_str_%d\n", arg);
                    fprintf(f, "    add x0, x0, :lo12:.L_str_%d\n", arg);
                }
            } else {
                fprintf(f, "    leaq .L_str_%d(%%rip), %%rax\n", arg);
            }
            break;

        case OP_STORE: // $w1 <- $w0
            if (arm) fprintf(f, "    mov x19, x0\n");
            else     fprintf(f, "    movq %%rax, %%rbx\n");
            break;

        case OP_LOAD: // $w0 <- $w1
            if (arm) fprintf(f, "    mov x0, x19\n");
            else     fprintf(f, "    movq %%rbx, %%rax\n");
            break;

        case OP_STORE_LOCAL:
            if (arm) fprintf(f, "    str x0, [x29, #%d]\n", local_off_arm(arg));
            else     fprintf(f, "    movq %%rax, %d(%%rbp)\n", local_off_x86(arg));
            break;

        case OP_LOAD_LOCAL:
            if (arm) fprintf(f, "    ldr x0, [x29, #%d]\n", local_off_arm(arg));
            else     fprintf(f, "    movq %d(%%rbp), %%rax\n", local_off_x86(arg));
            break;

        case OP_SETARG:
            if (arm) fprintf(f, "    str x0, [x29, #%d]\n", stage_off_arm(ctx, arg));
            else     fprintf(f, "    movq %%rax, %d(%%rbp)\n", stage_off_x86(ctx, arg));
            break;

        case OP_CALL_IMPORT: {
            int arity = 1;
            const char* name = NULL;
            if (ctx->wasm_imports && arg >= 0 && arg < ctx->wasm_import_count) {
                arity = ctx->wasm_imports[arg].n_params;
                name = ctx->wasm_imports[arg].name;
            }
            if (name) emit_call(ctx, name, arity);
            break;
        }

        case OP_CALL_RUNTIME: {
            int arity = 1;
            const char* sym = teko_native_runtime_symbol(arg, &arity);
            // Phase 17.D — id 50 (teko_rt_float_to_string) is the f64-ARG runtime call: the double
            // is ALREADY in xmm0/d0 (= $f0) per the SysV/AAPCS FP-arg convention (the frontend
            // guarantees $f0 holds the value before the call), so emit a BARE call — emit_call would
            // wrongly marshal the integer $w0 into the first GP-arg register. The char* result lands
            // in rax/x0 = $w0, exactly like the other to_string ids.
            if (arg == 50) emit_call_noarg(ctx, "teko_rt_float_to_string");
            else if (arg == 59) {
                // Phase 17.F.4 — decimal.to_string: the decimal value is in the $d0 slot (NOT $w0),
                // so pass &$d0 as the first GP arg; the char* result lands in rax/x0 = $w0 (VT_STR).
                emit_decimal_slot_addr(ctx, 0, 0);    // rdi/x0 = &$d0
                if (arm) fprintf(f, "    bl %steko_rt_decimal_to_string\n", pre);
                else     fprintf(f, "    call %steko_rt_decimal_to_string%s\n", pre, is_macho(ctx) ? "" : "@PLT");
            } else if (arg == 60) {
                // Phase 17.F.4 — decimal.parse: the string ptr is in $w0 (arg0); the result decimal
                // is written into the $d0 slot (arg1 = &$d0). Checked/fail-loud inside the wrapper.
                if (arm) {
                    // x0 already holds the string ptr (= $w0). arg1 = x1 = &$d0.
                    emit_decimal_slot_addr(ctx, 1, 0);
                    fprintf(f, "    bl %steko_rt_decimal_parse\n", pre);
                } else {
                    fprintf(f, "    movq %%rax, %%rdi\n");  // rdi = string ptr (arg0)
                    emit_decimal_slot_addr(ctx, 1, 0);      // rsi = &$d0 (arg1)
                    fprintf(f, "    call %steko_rt_decimal_parse%s\n", pre, is_macho(ctx) ? "" : "@PLT");
                }
            }
            else if (sym) emit_call(ctx, sym, arity);
            break;
        }

        // Phase 14 (14.B): duplex channel ops — lower to the teko_rt_duplex_* runtime calls
        // (staging slots + $w0 marshalled by emit_call, exactly like OP_CALL_RUNTIME).
        case OP_DUPLEX_OPEN:
        case OP_DUPLEX_SEND:
        case OP_DUPLEX_RECV:
        case OP_DUPLEX_POLL:
        case OP_DUPLEX_CLOSE: {
            int arity = 1;
            const char* sym = teko_native_duplex_symbol(op, &arity);
            if (sym) emit_call(ctx, sym, arity);
            break;
        }

        // Phase 14 (14.C): delayed (timed) channel ops -> teko_rt_delayed_* runtime calls.
        case OP_DELAYED_OPEN:
        case OP_DELAYED_SEND:
        case OP_DELAYED_RECV:
        case OP_DELAYED_POLL:
        case OP_DELAYED_CLOSE: {
            int arity = 1;
            const char* sym = teko_native_delayed_symbol(op, &arity);
            if (sym) emit_call(ctx, sym, arity);
            break;
        }

        // Phase 14 (14.D): broadcast pub-sub ops -> teko_rt_bcast_* runtime calls.
        case OP_BCAST_OPEN:
        case OP_BCAST_SUBSCRIBE:
        case OP_BCAST_PUBLISH:
        case OP_BCAST_RECV:
        case OP_BCAST_POLL:
        case OP_BCAST_CLOSE: {
            int arity = 1;
            const char* sym = teko_native_bcast_symbol(op, &arity);
            if (sym) emit_call(ctx, sym, arity);
            break;
        }

        // Phase 14 (14.E): shared-block lock + atomic cell ops -> teko_shared_*/teko_atomic_*.
        case OP_SHARED_ENTER:
        case OP_SHARED_LEAVE:
        case OP_ATOMIC_CELL:
        case OP_ATOMIC_ADD:
        case OP_ATOMIC_LOAD:
        case OP_ATOMIC_STORE: {
            int arity = 0;
            const char* sym = teko_native_shared_symbol(op, &arity);
            if (sym) emit_call(ctx, sym, arity);
            break;
        }

        // Phase 14 (14.G): timespan waiters. The ms delay is in $w0. `wait` is a real synchronous
        // sleep (teko_rt_sleep_ms, always linked); `await` is a cooperative yield that drains the
        // run queue (teko_rt_await_ms, in the scheduler TU — emit_await sets uses_spawn so the
        // routine table the scheduler walks is emitted). Both take ms in arg0, return void.
        case OP_WAIT:      emit_call(ctx, "teko_rt_wait_ns", 1); break;  // real-time cooperative wait
        case OP_AWAIT_FOR: emit_call(ctx, "teko_rt_await_ns", 1); break; // real-time wait + drain

        // Phase 14 (14.F): resilience policy ops -> teko_rt_retry_*/teko_rt_circuit_* runtime calls.
        case OP_RETRY_NEW:
        case OP_RETRY_SHOULD_CONTINUE:
        case OP_RETRY_NEXT_DELAY:
        case OP_CIRCUIT_NEW:
        case OP_CIRCUIT_ALLOW:
        case OP_CIRCUIT_RECORD: {
            int arity = 1;
            const char* sym = teko_native_retry_symbol(op, &arity);
            if (sym) emit_call(ctx, sym, arity);
            break;
        }

        // Phase 15 (15.A): object model ops -> teko_rt_object_* runtime calls (the `class`
        // instance store; staging slots + $w0 marshalled by emit_call, like OP_CALL_RUNTIME).
        case OP_OBJ_NEW:
        case OP_OBJ_SET:
        case OP_OBJ_GET:
        case OP_OBJ_FREE: {
            int arity = 1;
            const char* sym = teko_native_object_symbol(op, &arity);
            if (sym) emit_call(ctx, sym, arity);
            break;
        }

        // Phase 18 (18.E.1): fixed-size array ops -> teko_rt_array_* runtime calls (the `array`
        // store; staging slots + $w0 marshalled by emit_call, like OP_OBJ_*). get/set are CHECKED
        // FAIL-LOUD: the wrapper aborts (exit 70 + stderr) on an out-of-range index.
        case OP_ARR_NEW:
        case OP_ARR_GET:
        case OP_ARR_SET:
        case OP_ARR_LEN: {
            int arity = 1;
            const char* sym = teko_native_array_symbol(op, &arity);
            if (sym) emit_call(ctx, sym, arity);
            break;
        }

        // Phase 18 (18.E.2): typed `i32[]` packed-array ops -> teko_rt_iarray_* runtime calls (same
        // staging/ABI as OP_ARR_*; PACKED int32 cells). get/set are CHECKED FAIL-LOUD: the wrapper
        // aborts (exit 70 + stderr "iarray: index out of bounds") on an out-of-range index.
        case OP_IARR_NEW:
        case OP_IARR_GET:
        case OP_IARR_SET:
        case OP_IARR_LEN: {
            int arity = 1;
            const char* sym = teko_native_iarray_symbol(op, &arity);
            if (sym) emit_call(ctx, sym, arity);
            break;
        }

        // Phase 18 (18.E.4): the REAL SIMD reduction. The run HANDLE is in $w0; fetch its data ptr +
        // length, then call the per-ISA vector kernel teko_simd_sum_i32 (emitted once at module close).
        // The scalar sum lands in $w0. The kernel is the REAL SSE2/NEON vector loop (scalar elsewhere).
        case OP_SIMD_SUM:
            emit_simd_sum_lowering(ctx);
            break;

        // Phase 15 (15.B): static-vtable ops -> teko_rt_vtable_* runtime calls (abstract/trait
        // dynamic dispatch — VTABLE_GET feeds the resolved slot to a following OP_CALL_FUNC).
        case OP_VTABLE_SET:
        case OP_VTABLE_GET: {
            int arity = 2;
            const char* sym = teko_native_vtable_symbol(op, &arity);
            if (sym) emit_call(ctx, sym, arity);
            break;
        }

        // Phase 14 (control-flow foundation): structured loops + branches via local asm labels.
        // The loop top is `.Lcont_<id>` (continue target) and the exit is `.Lbrk_<id>` (break
        // target); `if` skips to `.Lendif_<id>`. The id stacks resolve break/continue to the
        // innermost loop. Conditions arrive in $w0 (0 = false).
        case OP_LOOP_BEGIN: {
            int id = ctx->cf_id_next++;
            if (ctx->cf_loop_sp < 64) ctx->cf_loop_stack[ctx->cf_loop_sp++] = id;
            fprintf(f, ".Lcont_%d:\n", id);
            break;
        }
        case OP_LOOP_END: {
            int id = (ctx->cf_loop_sp > 0) ? ctx->cf_loop_stack[--ctx->cf_loop_sp] : 0;
            fprintf(f, arm ? "    b .Lcont_%d\n.Lbrk_%d:\n" : "    jmp .Lcont_%d\n.Lbrk_%d:\n",
                    id, id);
            break;
        }
        case OP_BREAK: {
            int id = (ctx->cf_loop_sp > 0) ? ctx->cf_loop_stack[ctx->cf_loop_sp - 1] : 0;
            fprintf(f, arm ? "    b .Lbrk_%d\n" : "    jmp .Lbrk_%d\n", id);
            break;
        }
        case OP_CONTINUE: {
            int id = (ctx->cf_loop_sp > 0) ? ctx->cf_loop_stack[ctx->cf_loop_sp - 1] : 0;
            fprintf(f, arm ? "    b .Lcont_%d\n" : "    jmp .Lcont_%d\n", id);
            break;
        }
        case OP_BREAK_IF_FALSE: {
            int id = (ctx->cf_loop_sp > 0) ? ctx->cf_loop_stack[ctx->cf_loop_sp - 1] : 0;
            if (arm) fprintf(f, "    cmp w0, #0\n    b.eq .Lbrk_%d\n", id);
            else     fprintf(f, "    cmpl $0, %%eax\n    je .Lbrk_%d\n", id);
            break;
        }
        case OP_IF_BEGIN: {
            int id = ctx->cf_id_next++;
            if (ctx->cf_if_sp < 64) ctx->cf_if_stack[ctx->cf_if_sp++] = id;
            if (arm) fprintf(f, "    cmp w0, #0\n    b.eq .Lendif_%d\n", id);
            else     fprintf(f, "    cmpl $0, %%eax\n    je .Lendif_%d\n", id);
            break;
        }
        case OP_IF_END: {
            int id = (ctx->cf_if_sp > 0) ? ctx->cf_if_stack[--ctx->cf_if_sp] : 0;
            fprintf(f, ".Lendif_%d:\n", id);
            break;
        }

        // Integer ALU ($w0 = $w0 <op> $w1). Pointers use x; arithmetic uses w.
        case OP_ADD: fprintf(f, arm ? "    add w0, w0, w19\n"  : "    addl %%ebx, %%eax\n"); break;
        case OP_SUB: fprintf(f, arm ? "    sub w0, w0, w19\n"  : "    subl %%ebx, %%eax\n"); break;
        case OP_MUL: fprintf(f, arm ? "    mul w0, w0, w19\n"  : "    imull %%ebx, %%eax\n"); break;
        case OP_DIV:
            if (arm) {
                fprintf(f, "    sdiv w0, w0, w19\n");
            } else {
                fprintf(f, "    cltd\n    idivl %%ebx\n");
            }
            break;
        case OP_MOD:
            if (arm) {
                fprintf(f, "    sdiv w1, w0, w19\n    msub w0, w1, w19, w0\n");
            } else {
                fprintf(f, "    cltd\n    idivl %%ebx\n    movl %%edx, %%eax\n");
            }
            break;
        case OP_EQ: case OP_NE: case OP_LT: case OP_LE: case OP_GT: case OP_GE: {
            // $w0 = ($w0 <cmp> $w1) ? 1 : 0
            if (arm) {
                const char* cc = (op == OP_EQ) ? "eq" : (op == OP_NE) ? "ne" :
                                 (op == OP_LT) ? "lt" : (op == OP_LE) ? "le" :
                                 (op == OP_GT) ? "gt" : "ge";
                fprintf(f, "    cmp w0, w19\n    cset w0, %s\n", cc);
            } else {
                const char* cc = (op == OP_EQ) ? "sete" : (op == OP_NE) ? "setne" :
                                 (op == OP_LT) ? "setl" : (op == OP_LE) ? "setle" :
                                 (op == OP_GT) ? "setg" : "setge";
                fprintf(f, "    cmpl %%ebx, %%eax\n    %s %%al\n    movzbl %%al, %%eax\n", cc);
            }
            break;
        }

        // ============================================================================
        // Phase 17 (17.A): f64 VALUE MODEL — a parallel float accumulator. $f0=xmm0/d0,
        // $f1=xmm1/d1 (both caller-saved; no float expression spans a call in the hosted
        // subset). Float locals reuse the EXISTING integer frame slots (one type per slot),
        // accessed with movsd / str d / ldr d. FCONST's scratch GPR is r11 (x86) / x9 (arm64)
        // — NEVER $w0(rax/x0) or $w1(rbx/x19) — so the integer accumulator is preserved.
        // ============================================================================
        case OP_FCONST: {
            // Load the 64-bit bit pattern of float_pool[arg] into $f0.
            uint64_t u = 0;
            if (ctx->float_pool && arg >= 0 && arg < ctx->float_count) {
                double d = ctx->float_pool[arg];
                memcpy(&u, &d, 8); // read the f64 bit pattern (no aliasing UB)
            }
            if (arm) {
                fprintf(f, "    movz x9, #%u\n", (unsigned)(u & 0xFFFFu));
                fprintf(f, "    movk x9, #%u, lsl #16\n", (unsigned)((u >> 16) & 0xFFFFu));
                fprintf(f, "    movk x9, #%u, lsl #32\n", (unsigned)((u >> 32) & 0xFFFFu));
                fprintf(f, "    movk x9, #%u, lsl #48\n", (unsigned)((u >> 48) & 0xFFFFu));
                fprintf(f, "    fmov d0, x9\n");
            } else {
                fprintf(f, "    movabsq $%llu, %%r11\n", (unsigned long long)u);
                fprintf(f, "    movq %%r11, %%xmm0\n");
            }
            break;
        }
        case OP_FADD: fprintf(f, arm ? "    fadd d0, d0, d1\n" : "    addsd %%xmm1, %%xmm0\n"); break;
        case OP_FSUB: fprintf(f, arm ? "    fsub d0, d0, d1\n" : "    subsd %%xmm1, %%xmm0\n"); break;
        case OP_FMUL: fprintf(f, arm ? "    fmul d0, d0, d1\n" : "    mulsd %%xmm1, %%xmm0\n"); break;
        case OP_FDIV: fprintf(f, arm ? "    fdiv d0, d0, d1\n" : "    divsd %%xmm1, %%xmm0\n"); break;

        // Float compares: $w0 = ($f0 <cmp> $f1) ? 1 : 0. x86 `ucomisd %xmm1,%xmm0` sets CF/ZF/PF
        // from the unsigned-style float comparison of xmm0 vs xmm1; the seta/setae/setb/setbe family
        // reads them. ucomisd raises the parity flag on unordered (NaN): for ==, NaN must yield 0
        // (a NaN compares-equal only via the false path) so we AND with `setnp`; for !=, NaN yields 1
        // so we OR with `setp`. arm64 `fcmp d0,d1` + cset with the standard (unordered-aware) CCs.
        case OP_FEQ: case OP_FNE: case OP_FLT: case OP_FLE: case OP_FGT: case OP_FGE: {
            if (arm) {
                const char* cc = (op == OP_FEQ) ? "eq" : (op == OP_FNE) ? "ne" :
                                 (op == OP_FLT) ? "mi" : (op == OP_FLE) ? "ls" :
                                 (op == OP_FGT) ? "gt" : "ge";
                // AArch64 fcmp: mi=LT, ls=LE, gt=GT, ge=GE, eq=EQ, ne=NE — all NaN-correct
                // (NaN sets C=1,Z=0,N=0,V=1; mi/ls/gt/ge/eq are false, ne is true under that flag set).
                fprintf(f, "    fcmp d0, d1\n    cset w0, %s\n", cc);
            } else {
                if (op == OP_FEQ) {
                    fprintf(f, "    ucomisd %%xmm1, %%xmm0\n");
                    fprintf(f, "    sete %%al\n    setnp %%cl\n    andb %%cl, %%al\n    movzbl %%al, %%eax\n");
                } else if (op == OP_FNE) {
                    fprintf(f, "    ucomisd %%xmm1, %%xmm0\n");
                    fprintf(f, "    setne %%al\n    setp %%cl\n    orb %%cl, %%al\n    movzbl %%al, %%eax\n");
                } else {
                    // Ordered LT/LE/GT/GE: use the carry/zero flags. seta = CF=0&ZF=0 (>), setae = CF=0
                    // (>=); for < and <= swap operands so the same a/ae flags express them. NaN sets
                    // CF=1 → a/ae are false → result 0, the correct ordered-compare-with-NaN answer.
                    const char* setcc; const char* xa; const char* xb;
                    if (op == OP_FGT)      { setcc = "seta";  xa = "%xmm1"; xb = "%xmm0"; } // f0 >  f1
                    else if (op == OP_FGE) { setcc = "setae"; xa = "%xmm1"; xb = "%xmm0"; } // f0 >= f1
                    else if (op == OP_FLT) { setcc = "seta";  xa = "%xmm0"; xb = "%xmm1"; } // f1 >  f0
                    else                   { setcc = "setae"; xa = "%xmm0"; xb = "%xmm1"; } // f1 >= f0  (FLE)
                    fprintf(f, "    ucomisd %s, %s\n", xa, xb);
                    fprintf(f, "    %s %%al\n    movzbl %%al, %%eax\n", setcc);
                }
            }
            break;
        }

        case OP_FSTORE: fprintf(f, arm ? "    fmov d1, d0\n" : "    movapd %%xmm0, %%xmm1\n"); break; // $f1 = $f0
        case OP_FLOAD:  fprintf(f, arm ? "    fmov d0, d1\n" : "    movapd %%xmm1, %%xmm0\n"); break; // $f0 = $f1

        case OP_FSTORE_LOCAL:
            if (arm) fprintf(f, "    str d0, [x29, #%d]\n", local_off_arm(arg));
            else     fprintf(f, "    movsd %%xmm0, %d(%%rbp)\n", local_off_x86(arg));
            break;
        case OP_FLOAD_LOCAL:
            if (arm) fprintf(f, "    ldr d0, [x29, #%d]\n", local_off_arm(arg));
            else     fprintf(f, "    movsd %d(%%rbp), %%xmm0\n", local_off_x86(arg));
            break;

        case OP_I2F: // $f0 = (double)$w0  (register-width signed int → double)
            if (arm) fprintf(f, "    scvtf d0, x0\n");
            else     fprintf(f, "    cvtsi2sdq %%rax, %%xmm0\n");
            break;

        // Phase 17 (17.B): float modulo `%` = IEEE remainder toward zero, INLINE as
        // `$f0 - trunc($f0/$f1)*$f1` — the EXACT same op sequence the WASM emitter uses
        // (f64.div / f64.trunc / f64.mul / f64.sub), so native and WASM are byte-identical for every
        // reachable input. Inlining (vs `call fmod`) keeps the hosted runner free of a libm
        // dependency — Linux `cc` does not auto-link libm, and the wasm32 reactor has no libm either,
        // so a libm call would be an asymmetric, non-portable dependency. SSE2-only: truncation is a
        // `cvttsd2si`→`cvtsi2sdq` round-trip through the FCONST scratch GPR `r11` (NOT $w0/rax, so the
        // op leaves the integer accumulator intact — it stays a pure CSE barrier, no $w0 clobber); the
        // round-trip is exact for the |$f0/$f1| < 2^53 range the `.`-form float grammar can reach.
        // xmm2 / d2 are scratch (outside the $f0/$f1 accumulator). $f0 receives the result.
        case OP_FMOD:
            if (arm) {
                fprintf(f, "    fdiv d2, d0, d1\n");   // d2 = a/b
                fprintf(f, "    frintz d2, d2\n");      // d2 = trunc(a/b) toward zero
                fprintf(f, "    fmul d2, d2, d1\n");    // d2 = trunc(a/b)*b
                fprintf(f, "    fsub d0, d0, d2\n");    // d0 = a - trunc(a/b)*b
            } else {
                fprintf(f, "    movapd %%xmm0, %%xmm2\n");   // xmm2 = a
                fprintf(f, "    divsd %%xmm1, %%xmm0\n");    // xmm0 = a/b
                fprintf(f, "    cvttsd2si %%xmm0, %%r11\n"); // r11 = trunc(a/b) (toward zero)
                fprintf(f, "    cvtsi2sdq %%r11, %%xmm0\n"); // xmm0 = (double)trunc(a/b)
                fprintf(f, "    mulsd %%xmm1, %%xmm0\n");    // xmm0 = trunc(a/b)*b
                fprintf(f, "    subsd %%xmm0, %%xmm2\n");    // xmm2 = a - trunc(a/b)*b
                fprintf(f, "    movapd %%xmm2, %%xmm0\n");   // $f0 = result
            }
            break;

        // Phase 17 (17.B): CHECKED float->int (truncate toward zero), FAIL-LOUD. cvttsd2si/fcvtzs do
        // NOT trap (they return a sentinel on overflow), so emit an explicit NaN + i32-range guard
        // matched to WASM's `i32.trunc_f64_s` valid OPEN interval: -2147483649.0 < $f0 < 2147483648.0
        // (and not NaN). A value outside it `call`s teko_rt_f2i_fail (the SAME exit-70 + stderr
        // fail-loud path 16.F uses), so a value that traps on WASM also aborts here. The scratch
        // GPR is r11/x9 (materialize the bound, NEVER $w0/$w1); the bound goes in $f1=xmm1/d1
        // (clobbering $f1 is fine — F2I consumes $f0 to produce an int and we never read $f1 after).
        case OP_F2I: {
            int id = ctx->cf_id_next++;
            uint64_t hi = 0, lo = 0; // 2147483648.0 ; -2147483649.0
            double dhi = 2147483648.0, dlo = -2147483649.0;
            memcpy(&hi, &dhi, 8); memcpy(&lo, &dlo, 8);
            if (arm) {
                // NaN: fcmp d0,d0 sets V (unordered) -> b.vs die.
                fprintf(f, "    fcmp d0, d0\n    b.vs .Lf2idie_%d\n", id);
                // upper: d1 = 2147483648.0 ; if d0 >= d1 -> die.
                fprintf(f, "    movz x9, #%u\n", (unsigned)(hi & 0xFFFFu));
                fprintf(f, "    movk x9, #%u, lsl #16\n", (unsigned)((hi >> 16) & 0xFFFFu));
                fprintf(f, "    movk x9, #%u, lsl #32\n", (unsigned)((hi >> 32) & 0xFFFFu));
                fprintf(f, "    movk x9, #%u, lsl #48\n", (unsigned)((hi >> 48) & 0xFFFFu));
                fprintf(f, "    fmov d1, x9\n    fcmp d0, d1\n    b.ge .Lf2idie_%d\n", id);
                // lower: d1 = -2147483649.0 ; if d0 <= d1 -> die.
                fprintf(f, "    movz x9, #%u\n", (unsigned)(lo & 0xFFFFu));
                fprintf(f, "    movk x9, #%u, lsl #16\n", (unsigned)((lo >> 16) & 0xFFFFu));
                fprintf(f, "    movk x9, #%u, lsl #32\n", (unsigned)((lo >> 32) & 0xFFFFu));
                fprintf(f, "    movk x9, #%u, lsl #48\n", (unsigned)((lo >> 48) & 0xFFFFu));
                fprintf(f, "    fmov d1, x9\n    fcmp d0, d1\n    b.le .Lf2idie_%d\n", id);
                fprintf(f, "    fcvtzs w0, d0\n");
                fprintf(f, "    b .Lf2iok_%d\n", id);
                fprintf(f, ".Lf2idie_%d:\n    bl %steko_rt_f2i_fail\n", id, pre);
                fprintf(f, ".Lf2iok_%d:\n", id);
            } else {
                // NaN: ucomisd %xmm0,%xmm0 sets PF (unordered) -> jp die.
                fprintf(f, "    ucomisd %%xmm0, %%xmm0\n    jp .Lf2idie_%d\n", id);
                // upper: xmm1 = 2147483648.0 ; ucomisd %xmm1,%xmm0 -> jae die (x >= 2^31).
                fprintf(f, "    movabsq $%llu, %%r11\n    movq %%r11, %%xmm1\n", (unsigned long long)hi);
                fprintf(f, "    ucomisd %%xmm1, %%xmm0\n    jae .Lf2idie_%d\n", id);
                // lower: xmm1 = -2147483649.0 ; ucomisd %xmm1,%xmm0 -> jbe die (x <= -(2^31+1)).
                fprintf(f, "    movabsq $%llu, %%r11\n    movq %%r11, %%xmm1\n", (unsigned long long)lo);
                fprintf(f, "    ucomisd %%xmm1, %%xmm0\n    jbe .Lf2idie_%d\n", id);
                fprintf(f, "    cvttsd2si %%xmm0, %%eax\n");
                fprintf(f, "    jmp .Lf2iok_%d\n", id);
                fprintf(f, ".Lf2idie_%d:\n    call %steko_rt_f2i_fail%s\n", id, pre, is_macho(ctx) ? "" : "@PLT");
                fprintf(f, ".Lf2iok_%d:\n", id);
            }
            break;
        }

        // ============================================================================
        // Phase 17.F.3: the 256-byte `decimal` VALUE MODEL. $d0/$d1 (+ a cmp-scratch sink + the $dvN
        // local file) are 256-byte STACK slots; a decimal flows ONLY by pointer (lea &slot -> ABI arg
        // reg). The arith ops call teko_rt_decimal_*(&$d0,&$d1,&$d0) (fail-loud on overflow/divzero);
        // the compares call teko_rt_decimal_cmp(&$d0,&$d1,&scratch) then map -1/0/+1 -> the i32 0/1
        // each compare wants in $w0(rax/x0). DCONST/DSTORE/DLOAD/D*_LOCAL are 256-byte copies that use
        // ONLY scratch regs, so the integer accumulator $w0/$w1 is preserved (pure CSE barriers).
        // The decimal-local frontend slot `n` maps to decimal slot 3+n (0/1/2 = $d0/$d1/cmp-scratch).
        // ============================================================================
        case OP_DCONST: {
            // $d0 = decimal_pool[arg]: copy 256 B from the .L_dec_<arg> rodata blob into slot 0.
            int id = ctx->cf_id_next++;
            if (arm) {
                if (is_macho(ctx)) {
                    fprintf(f, "    adrp x9, .L_dec_%d@PAGE\n    add x9, x9, .L_dec_%d@PAGEOFF\n", arg, arg);
                } else {
                    fprintf(f, "    adrp x9, .L_dec_%d\n    add x9, x9, :lo12:.L_dec_%d\n", arg, arg);
                }
                fprintf(f, "    add x10, x29, #%d\n", decimal_slot_off_arm(ctx, 0));
                fprintf(f, "    mov w11, #32\n");
                fprintf(f, ".Ldcpy_%d:\n    ldr x12, [x9], #8\n    str x12, [x10], #8\n", id);
                fprintf(f, "    subs w11, w11, #1\n    b.ne .Ldcpy_%d\n", id);
            } else {
                fprintf(f, "    leaq .L_dec_%d(%%rip), %%r11\n", arg);
                fprintf(f, "    leaq %d(%%rbp), %%r10\n", decimal_slot_off_x86(ctx, 0));
                fprintf(f, "    movl $32, %%r8d\n");
                fprintf(f, ".Ldcpy_%d:\n    movq (%%r11), %%r9\n    movq %%r9, (%%r10)\n", id);
                fprintf(f, "    addq $8, %%r11\n    addq $8, %%r10\n");
                fprintf(f, "    decl %%r8d\n    jnz .Ldcpy_%d\n", id);
            }
            break;
        }
        case OP_DADD: case OP_DSUB: case OP_DMUL: case OP_DDIV: case OP_DMOD: {
            const char* sym = (op == OP_DADD) ? "teko_rt_decimal_add" :
                              (op == OP_DSUB) ? "teko_rt_decimal_sub" :
                              (op == OP_DMUL) ? "teko_rt_decimal_mul" :
                              (op == OP_DDIV) ? "teko_rt_decimal_div" : "teko_rt_decimal_mod";
            emit_decimal_slot_addr(ctx, 0, 0); // arg0 = &$d0  (a)
            emit_decimal_slot_addr(ctx, 1, 1); // arg1 = &$d1  (b)
            emit_decimal_slot_addr(ctx, 2, 0); // arg2 = &$d0  (out, in place)
            if (arm) fprintf(f, "    bl %s%s\n", pre, sym);
            else     fprintf(f, "    call %s%s%s\n", pre, sym, is_macho(ctx) ? "" : "@PLT");
            break;
        }
        case OP_DEQ: case OP_DNE: case OP_DLT: case OP_DLE: case OP_DGT: case OP_DGE: {
            // teko_rt_decimal_cmp(&$d0,&$d1,&scratch) writes -1/0/+1 to scratch; map to the i32 0/1.
            emit_decimal_slot_addr(ctx, 0, 0); // &$d0 (a)
            emit_decimal_slot_addr(ctx, 1, 1); // &$d1 (b)
            emit_decimal_slot_addr(ctx, 2, TEKO_HOSTED_DECIMAL_SCRATCH); // &scratch (out i32)
            if (arm) fprintf(f, "    bl %steko_rt_decimal_cmp\n", pre);
            else     fprintf(f, "    call %steko_rt_decimal_cmp%s\n", pre, is_macho(ctx) ? "" : "@PLT");
            // Reload the tri-state (-1/0/+1) and turn it into the requested boolean in $w0.
            if (arm) {
                fprintf(f, "    add x9, x29, #%d\n    ldr w9, [x9]\n", decimal_slot_off_arm(ctx, TEKO_HOSTED_DECIMAL_SCRATCH));
                fprintf(f, "    cmp w9, #0\n");
                const char* cc = (op == OP_DEQ) ? "eq" : (op == OP_DNE) ? "ne" :
                                 (op == OP_DLT) ? "lt" : (op == OP_DLE) ? "le" :
                                 (op == OP_DGT) ? "gt" : "ge";
                fprintf(f, "    cset w0, %s\n", cc);
            } else {
                fprintf(f, "    movl %d(%%rbp), %%r11d\n", decimal_slot_off_x86(ctx, TEKO_HOSTED_DECIMAL_SCRATCH));
                fprintf(f, "    cmpl $0, %%r11d\n");
                const char* cc = (op == OP_DEQ) ? "sete" : (op == OP_DNE) ? "setne" :
                                 (op == OP_DLT) ? "setl" : (op == OP_DLE) ? "setle" :
                                 (op == OP_DGT) ? "setg" : "setge";
                fprintf(f, "    %s %%al\n    movzbl %%al, %%eax\n", cc);
            }
            break;
        }
        case OP_DSTORE: emit_decimal_slot_copy(ctx, 1, 0); break; // $d1 = $d0
        case OP_DLOAD:  emit_decimal_slot_copy(ctx, 0, 1); break; // $d0 = $d1
        case OP_DSTORE_LOCAL: emit_decimal_slot_copy(ctx, 3 + arg, 0); break; // $dv<arg> = $d0
        case OP_DLOAD_LOCAL:  emit_decimal_slot_copy(ctx, 0, 3 + arg); break; // $d0 = $dv<arg>

        // Phase 17.F.4: int/float ↔ decimal CASTS. I2D/F2D write &$d0; D2I/D2F read &$d0 and return a
        // register value. The decimal value flows ONLY by pointer (lea &$d0 -> the GP arg reg). I2D
        // takes the int from $w0 (rax/x0); F2D takes the double from $f0 (xmm0/d0 = the FP arg reg);
        // D2I returns the checked i32 in rax/eax = $w0; D2F returns the double in xmm0/d0 = $f0.
        case OP_I2D: // teko_rt_decimal_from_i32(int v=$w0, &$d0)
            if (arm) {
                // x0 already holds the int (= $w0). The pointer is arg1 = x1.
                emit_decimal_slot_addr(ctx, 1, 0);   // x1 = &$d0
                fprintf(f, "    bl %steko_rt_decimal_from_i32\n", pre);
            } else {
                fprintf(f, "    movl %%eax, %%edi\n"); // edi = int (arg0)  [movl zero-extends rdi]
                emit_decimal_slot_addr(ctx, 1, 0);    // rsi = &$d0 (arg1)
                fprintf(f, "    call %steko_rt_decimal_from_i32%s\n", pre, is_macho(ctx) ? "" : "@PLT");
            }
            break;
        case OP_F2D: // teko_rt_decimal_from_f64(double v=$f0, &$d0)
            // The double is already in xmm0/d0 (= $f0) per the FP-arg ABI; the pointer is GP arg0.
            emit_decimal_slot_addr(ctx, 0, 0);        // rdi/x0 = &$d0 (arg1 in C; first GP reg)
            if (arm) fprintf(f, "    bl %steko_rt_decimal_from_f64\n", pre);
            else     fprintf(f, "    call %steko_rt_decimal_from_f64%s\n", pre, is_macho(ctx) ? "" : "@PLT");
            break;
        case OP_D2I: // $w0 = teko_rt_decimal_to_i32(&$d0)  (checked, fail-loud)
            emit_decimal_slot_addr(ctx, 0, 0);        // rdi/x0 = &$d0
            if (arm) fprintf(f, "    bl %steko_rt_decimal_to_i32\n", pre);
            else     fprintf(f, "    call %steko_rt_decimal_to_i32%s\n", pre, is_macho(ctx) ? "" : "@PLT");
            // result i32 already in eax/w0 (= $w0).
            break;
        case OP_D2F: // $f0 = teko_rt_decimal_to_f64(&$d0)
            emit_decimal_slot_addr(ctx, 0, 0);        // rdi/x0 = &$d0
            if (arm) fprintf(f, "    bl %steko_rt_decimal_to_f64\n", pre);
            else     fprintf(f, "    call %steko_rt_decimal_to_f64%s\n", pre, is_macho(ctx) ? "" : "@PLT");
            // result double already in xmm0/d0 (= $f0).
            break;

        default:
            break; // unsupported opcode in the hosted subset (straight-line surface)
    }
}
