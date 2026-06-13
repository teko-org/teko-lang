#include "../emit_x86_64_sysv_common.h"

// macOS Intel x86_64 (Mach-O). Thin wrapper over the shared x86_64 System V
// emitter — underscore-decorated symbols and a comment-only halt (no syscall).
void emit_darwin_x86_64(MetalContext* ctx, OpCode op, int32_t arg) {
    static const X86SysvEmitParams p = {
        .banner        = "Intel x86_64 (macOS)",
        .sym_prefix    = "_",
        .str_label     = ".L_str_",
        .tag           = "darwin_x86",
        .halt_comment  = "    ;; [AOT Halt]: Terminates flow on Mac Intel.\n",
        .halt_exit_num = -1,
        .halt_trap     = NULL,
        .spawn_comment = NULL,
        .chan_comment  = NULL,
    };
    emit_x86_64_sysv_common(ctx, op, arg, &p);
}
