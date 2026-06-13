#include "teko_target.h"
#include <string.h>
#include <stdio.h>

TekoTarget teko_target_detect_host(void) {
    TekoTarget target;
    target.arch = ARCH_UNKNOWN;
    target.os = OS_UNKNOWN;
    strcpy(target.target_string, "unknown-unknown-unknown");

    // 1. Detailed Detection of the Host Architecture
#if defined(__APPLE__) && defined(__aarch64__)
    target.arch = ARCH_APPLE_SILICON;
#elif defined(__aarch64__) || defined(_M_ARM64)
    target.arch = ARCH_ARM64;
#elif defined(__x86_64__) || defined(_M_X64)
    target.arch = ARCH_X86_64;
#elif defined(__i386__) || defined(_M_IX86)
    target.arch = ARCH_X86;
#elif defined(__riscv) && (__riscv_xlen == 64)
    target.arch = ARCH_RISCV64;
#elif defined(__riscv) && (__riscv_xlen == 32)
    target.arch = ARCH_RISCV32;
#elif defined(__powerpc__) || defined(_ARCH_PPC64)
    target.arch = ARCH_PPC64;
#elif defined(__arm__) || defined(_M_ARM)
    target.arch = ARCH_ARM32;
#endif

    // 2. Detection of the Host Operating System
#if defined(__APPLE__) && defined(__MACH__)
    target.os = OS_MACOS_DARWIN;
#elif defined(__linux__)
    target.os = OS_LINUX;
#elif defined(_WIN32)
    target.os = OS_WINDOWS;
#endif

    // 3. Translates the detected host into the canonical string accepted by the LLVM ecosystem
    const char* arch_str = "unknown";
    switch (target.arch) {
        case ARCH_APPLE_SILICON: arch_str = "aarch64"; break;
        case ARCH_ARM64:         arch_str = "aarch64"; break;
        case ARCH_X86_64:        arch_str = "x86_64";  break;
        case ARCH_X86:           arch_str = "i386";    break;
        case ARCH_RISCV64:       arch_str = "riscv64"; break;
        case ARCH_RISCV32:       arch_str = "riscv32"; break;
        case ARCH_PPC64:         arch_str = "powerpc64le"; break;
        case ARCH_ARM32:         arch_str = "arm";     break;
        default: break;
    }

    const char* os_str = "unknown";
    switch (target.os) {
        case OS_MACOS_DARWIN: os_str = "apple-darwin";       break;
        case OS_LINUX:        os_str = "unknown-linux-gnu";   break;
        case OS_WINDOWS:      os_str = "pc-windows-msvc";     break;
        default: break;
    }

    snprintf(target.target_string, sizeof(target.target_string), "%s-%s", arch_str, os_str);
    return target;
}

// Dynamic cross-compilation parser driven by command-line parameters (e.g. teko build --target wasm32-wasi)
TekoTarget teko_target_parse(const char* target_str) {
    TekoTarget target = { ARCH_UNKNOWN, OS_UNKNOWN, "" };
    if (!target_str) return target;

    strncpy(target.target_string, target_str, sizeof(target.target_string) - 1);

    // Exhaustive mapping of architecture text keys
    if (strstr(target_str, "x86_64"))      target.arch = ARCH_X86_64;
    else if (strstr(target_str, "i386") || strstr(target_str, "x86")) target.arch = ARCH_X86;
    else if (strstr(target_str, "apple-darwin") && (strstr(target_str, "aarch64") || strstr(target_str, "arm64"))) target.arch = ARCH_APPLE_SILICON;
    else if (strstr(target_str, "aarch64") || strstr(target_str, "arm64")) target.arch = ARCH_ARM64;
    else if (strstr(target_str, "riscv64")) target.arch = ARCH_RISCV64;
    else if (strstr(target_str, "riscv32")) target.arch = ARCH_RISCV32;
    else if (strstr(target_str, "wasm64"))  target.arch = ARCH_WASM64;
    else if (strstr(target_str, "wasm32") || strstr(target_str, "wasm"))   target.arch = ARCH_WASM32;
    else if (strstr(target_str, "mips64"))  target.arch = ARCH_MIPS64;
    else if (strstr(target_str, "mips"))    target.arch = ARCH_MIPS32;
    else if (strstr(target_str, "powerpc") || strstr(target_str, "ppc"))  target.arch = ARCH_PPC64;
    else if (strstr(target_str, "arm"))     target.arch = ARCH_ARM32;

    // Mapping of operating environments and bare-metal firmware
    if (strstr(target_str, "darwin") || strstr(target_str, "apple") || strstr(target_str, "macos")) target.os = OS_MACOS_DARWIN;
    else if (strstr(target_str, "linux"))   target.os = OS_LINUX;
    else if (strstr(target_str, "windows") || strstr(target_str, "win32")) target.os = OS_WINDOWS;
    else if (strstr(target_str, "wasi"))    target.os = OS_WASI;
    else if (strstr(target_str, "none") || strstr(target_str, "none-eabi") || strstr(target_str, "avr"))   target.os = OS_BARE_METAL;

    return target;
}