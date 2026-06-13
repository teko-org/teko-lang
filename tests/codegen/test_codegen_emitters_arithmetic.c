#include "unity.h"
#include "codegen/codegen_metal.h"
#include "teko_target.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Conservative golden safety-net for the per-target emitters: drives OP_ADD
// through every one of the 16 emitters and asserts the architecture-specific
// add mnemonic. Arithmetic emission was previously unasserted for most targets;
// this hardens the net before the emitter de-duplication refactor (tech-debt #7).
void test_codegen_emitters_arithmetic_add_per_target(void) {
    typedef struct {
        TekoArch arch;
        TekoOS os;
        const char* name;
        const char* add_mnemonic;
    } EmitterCase;

    // Expected emitted text (fprintf turns "%%" into a single "%").
    static const EmitterCase cases[] = {
        { ARCH_X86_64,        OS_LINUX,        "linux-x86_64",   "addl %ebx, %eax" },
        { ARCH_X86,           OS_LINUX,        "linux-x86",      "addl %ebx, %eax" },
        { ARCH_ARM64,         OS_LINUX,        "linux-arm64",    "add w0, w0, w1" },
        { ARCH_ARM32,         OS_LINUX,        "linux-arm32",    "add r0, r0, r1" },
        { ARCH_RISCV64,       OS_LINUX,        "linux-riscv64",  "add a0, a0, a1" },
        { ARCH_RISCV32,       OS_LINUX,        "linux-riscv32",  "add a0, a0, a1" },
        { ARCH_MIPS64,        OS_LINUX,        "linux-mips",     "addu $v0, $v0, $v1" },
        { ARCH_PPC64,         OS_LINUX,        "linux-ppc64",    "add 3, 3, 4" },
        { ARCH_APPLE_SILICON, OS_MACOS_DARWIN, "darwin-arm64",   "add w0, w0, w1" },
        { ARCH_X86_64,        OS_MACOS_DARWIN, "darwin-x86_64",  "addl %ebx, %eax" },
        { ARCH_X86_64,        OS_UNKNOWN,      "freebsd-x86_64", "addl %ebx, %eax" },
        { ARCH_ARM64,         OS_UNKNOWN,      "freebsd-arm64",  "add w0, w0, w1" },
        { ARCH_X86_64,        OS_WINDOWS,      "win-x86_64",     "add eax, ebx" },
        { ARCH_X86,           OS_WINDOWS,      "win-x86",        "add eax, ebx" },
        { ARCH_ARM64,         OS_WINDOWS,      "win-arm64",      "add w0, w0, w1" },
        { ARCH_WASM32,        OS_WASI,         "wasm",           "i32.add" },
    };
    const int n = (int)(sizeof(cases) / sizeof(cases[0]));

    // OP_ADD (0x05) then OP_HALT (0x00). With no ICONST pair, constant folding
    // does not collapse the add, so it must reach the emitter verbatim.
    const unsigned char program[] = { 0x05, 0x00 };
    const char* path = "golden_arith_emit.s";

    for (int i = 0; i < n; i++) {
        TekoTarget target = { .arch = cases[i].arch, .os = cases[i].os };
        MetalContext* ctx = teko_metal_create(path, target);
        TEST_ASSERT_NOT_NULL_MESSAGE(ctx, cases[i].name);

        teko_metal_emit_program(ctx, program, sizeof(program));
        teko_metal_close(ctx);

        FILE* f = fopen(path, "r");
        TEST_ASSERT_NOT_NULL_MESSAGE(f, cases[i].name);
        char buf[4096];
        size_t bytes = fread(buf, 1, sizeof(buf) - 1, f);
        buf[bytes] = '\0';
        fclose(f);
        remove(path);

        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, cases[i].add_mnemonic), cases[i].name);
    }
}
