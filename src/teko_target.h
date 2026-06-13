#ifndef TEKO_TARGET_H
#define TEKO_TARGET_H

// Exhaustive enumeration of supported CPU architectures for Metal and Web compilation
typedef enum {
    ARCH_X86,           // Intel/AMD 32-bit (i386/i686)
    ARCH_X86_64,        // Intel/AMD 64-bit (amd64)
    ARCH_ARM32,         // ARM 32-bit (armv7/armhf)
    ARCH_ARM64,         // Generic ARM64
    ARCH_APPLE_SILICON, // ARM64 specifically optimized for Apple M-Series chips (Darwin)
    ARCH_RISCV32,       // RISC-V 32-bit (modern embedded systems)
    ARCH_RISCV64,       // RISC-V 64-bit (servers and edge computing)
    ARCH_WASM32,        // WebAssembly 32-bit (browsers and Cloud Native environments)
    ARCH_WASM64,        // WebAssembly 64-bit (Memory-64 extension)
    ARCH_MIPS32,       // MIPS 32-bit (routers and network hardware)
    ARCH_MIPS64,       // MIPS 64-bit
    ARCH_PPC64,         // PowerPC 64-bit (mainframes and supercomputers)
    ARCH_UNKNOWN
} TekoArch;

// Detailed enumeration of Operating Systems and Web Runtime Environments
typedef enum {
    OS_MACOS_DARWIN,    // Darwin kernel (native macOS)
    OS_LINUX,
    OS_WINDOWS,
    OS_WASI,            // WebAssembly System Interface (abstract OS for the Web)
    OS_BARE_METAL,      // No OS (embedded systems, Arduino, microcontrollers)
    OS_UNKNOWN
} TekoOS;

// Structure defining the unified Target Triple for LLVM
typedef struct {
    TekoArch arch;
    TekoOS os;
    char target_string[64]; // E.g. "riscv64-unknown-linux-gnu" or "wasm32-unknown-wasi"
} TekoTarget;

// Public signatures of the Expanded Target Manager
TekoTarget teko_target_detect_host(void);
TekoTarget teko_target_parse(const char* target_str);

#endif // TEKO_TARGET_H