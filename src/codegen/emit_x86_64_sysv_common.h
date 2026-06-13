#ifndef EMIT_X86_64_SYSV_COMMON_H
#define EMIT_X86_64_SYSV_COMMON_H

#include "codegen_metal.h"

// Per-OS parameters distinguishing the x86_64 System V emitters (Linux, FreeBSD,
// macOS). The instruction bodies are identical; only these differ.
typedef struct {
    const char* banner;        // text after "Target: " in the banner
    const char* sym_prefix;    // "" (ELF) or "_" (Mach-O) — prefixes main / pthread_create
    const char* str_label;     // SCONST label prefix, e.g. ".L_linux_str_" or ".L_str_"
    const char* tag;           // local-label tag: "linux" / "bsd" / "darwin_x86"
    const char* halt_comment;  // full HALT comment line, incl. leading spaces and trailing "\n"
    int halt_exit_num;         // exit syscall number, or -1 for comment-only (macOS)
    const char* halt_trap;     // "syscall" / "int $0x80" (used only when halt_exit_num >= 0)
    const char* spawn_comment; // SPAWN comment line, or NULL for none
    const char* chan_comment;  // CHAN_INIT comment line, or NULL for none
} X86SysvEmitParams;

void emit_x86_64_sysv_common(MetalContext* ctx, OpCode op, int32_t arg, const X86SysvEmitParams* p);

#endif // EMIT_X86_64_SYSV_COMMON_H
