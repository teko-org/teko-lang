#include "unity.h"
#include "teko_target.h"
#include <string.h>

void test_teko_bare_metal_target_detection_and_parsing(void) {
    // 1. Validates auto-detection on the local hardware (Mac Apple Silicon running Darwin)
    TekoTarget host = teko_target_detect_host();
    TEST_ASSERT_EQUAL_INT(ARCH_APPLE_SILICON, host.arch);
    TEST_ASSERT_EQUAL_INT(OS_MACOS_DARWIN, host.os);
    TEST_ASSERT_NOT_NULL(strstr(host.target_string, "aarch64-apple-darwin"));

    // 2. Cross-compilation test for the open RISC-V 64-bit architecture
    TekoTarget riscv_target = teko_target_parse("riscv64-unknown-linux-gnu");
    TEST_ASSERT_EQUAL_INT(ARCH_RISCV64, riscv_target.arch);
    TEST_ASSERT_EQUAL_INT(OS_LINUX, riscv_target.os);

    // 3. Cross-compilation test for WebAssembly targeting Web/Cloud runtimes (WASI)
    TekoTarget wasm_target = teko_target_parse("wasm32-unknown-wasi");
    TEST_ASSERT_EQUAL_INT(ARCH_WASM32, wasm_target.arch);
    TEST_ASSERT_EQUAL_INT(OS_WASI, wasm_target.os);
}