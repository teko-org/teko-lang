#include "../emit_x86_64_sysv_common.h"

// FreeBSD x86_64 (ELF). Thin wrapper over the shared x86_64 System V emitter —
// only the FreeBSD-specific params differ (BSD sys_exit via int $0x80).
void emit_freebsd_x86_64(MetalContext* ctx, OpCode op, int32_t arg) {
    static const X86SysvEmitParams p = {
        .banner        = "Intel/AMD (64-bit FreeBSD)",
        .sym_prefix    = "",
        .str_label     = ".L_str_",
        .tag           = "bsd",
        .halt_comment  = "    ;; [FreeBSD Halt]: native sys_exit via BSD interrupt\n",
        .halt_exit_num = 1,
        .halt_trap     = "int $0x80",
        .spawn_comment = "    ;; --- [FreeBSD AOT Spawn via pthread_create] ---\n",
        .chan_comment  = "    ;; --- [FreeBSD x86_64 Arena-Based Channel Allocation] ---\n",
    };
    emit_x86_64_sysv_common(ctx, op, arg, &p);
}
