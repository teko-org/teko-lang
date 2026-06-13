#include "../emit_x86_64_sysv_common.h"

// Linux x86_64 (ELF, System V AMD64 ABI). Thin wrapper over the shared
// x86_64 System V emitter — only the Linux-specific params differ.
void emit_linux_x86_64(MetalContext* ctx, OpCode op, int32_t arg) {
    static const X86SysvEmitParams p = {
        .banner        = "Intel/AMD (64-bit Linux)",
        .sym_prefix    = "",
        .str_label     = ".L_linux_str_",
        .tag           = "linux",
        .halt_comment  = "    ;; [Linux x86_64 Halt]: native sys_exit via interrupt\n",
        .halt_exit_num = 60,
        .halt_trap     = "syscall",
        .spawn_comment = "    ;; --- [Linux AOT Coroutine Spawn via pthread_create] ---\n",
        .chan_comment  = "    ;; --- [Linux AOT Arena-Based Channel Allocation] ---\n",
    };
    emit_x86_64_sysv_common(ctx, op, arg, &p);
}
