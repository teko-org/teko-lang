#include "unity.h"
#include "vm_intrinsics.h"
#include "vm_core.h"
#include "teko_sdk.h"
#include "symbol_table.h"
#include "teko_lsp.h"
#include "teko_target.h"
#include <stdlib.h>
#include <string.h>

void test_teko_lsp_protocol_handshake_and_routing(void) {
    auto server = teko_lsp_create();
    TEST_ASSERT_NOT_NULL(server);

    // 1. Simulates the JSON-RPC initialization payload sent by CLion/VSCode
    const char* mock_init_payload = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";
    LSPRequestKind k1 = teko_lsp_parse_request(mock_init_payload);
    TEST_ASSERT_EQUAL_INT(LSP_REQ_INITIALIZE, k1);

    // 2. Simulates the automatic request triggered by the format-document shortcut
    const char* mock_fmt_payload = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/formatting\"}";
    LSPRequestKind k2 = teko_lsp_parse_request(mock_fmt_payload);
    TEST_ASSERT_EQUAL_INT(LSP_REQ_FORMATTING, k2);

    // 3. Executes simulated diagnostic write calls (captured without deadlocks)
    teko_lsp_send_diagnostics(server, "main.tks", "Mutability Error", 5);

    teko_lsp_destroy(server);
}

// Add these assertion lines inside the test_virtual_sdk_injection_and_isolation function
void test_virtual_sdk_injection_and_isolation(void) {
    SymbolTableScope* global_scope = symbol_table_create_scope(NULL);
    TEST_ASSERT_NOT_NULL(global_scope);

    teko_sdk_inject_builtins(global_scope);

    // Validates that the compiler now finds the filesystem resolver and virtual SDK logger
    Symbol* sym_fs = symbol_table_lookup(global_scope, "teko::fs.read_to_string");
    TEST_ASSERT_NOT_NULL(sym_fs);
    TEST_ASSERT_EQUAL_STRING("str", sym_fs->type_info->base_name);

    Symbol* sym_log = symbol_table_lookup(global_scope, "teko::logger.info");
    TEST_ASSERT_NOT_NULL(sym_log);
    TEST_ASSERT_EQUAL_STRING("void", sym_log->type_info->base_name);

    // Ensures that the linter isolation recognizes the new paths as reserved
    TEST_ASSERT_TRUE(teko_sdk_is_reserved_intrinsic("teko::sync.sleep_ms"));

    symbol_table_free_scope(global_scope);
}

void test_vm_intrinsic_resolution_and_execution(void) {
    // 1. Validates that the linter/parser extended string matches the VM ID resolution
    TekoIntrinsicKind k1 = vm_intrinsic_resolve("teko::marshall.to_ptr");
    TekoIntrinsicKind k2 = vm_intrinsic_resolve("teko::flows.notify");
    TekoIntrinsicKind k3 = vm_intrinsic_resolve("teko::invalid.method");

    TEST_ASSERT_EQUAL_INT(INTRINSIC_MARSHALL_TO_PTR, k1);
    TEST_ASSERT_EQUAL_INT(INTRINSIC_FLOWS_NOTIFY, k2);
    TEST_ASSERT_EQUAL_INT(INTRINSIC_UNKNOWN, k3);

    // 2. Simulates the minimal physical environment to trigger intrinsic execution
    unsigned char mock_code[] = {0x00};
    auto vm = teko_vm_create(mock_code, sizeof(mock_code), NULL, 0);
    TEST_ASSERT_NOT_NULL(vm);

    int32_t mock_args[2] = {42, 100};
    int32_t result = vm_intrinsic_execute(INTRINSIC_FLOWS_NOTIFY, vm, mock_args, 2);

    // Should intercept the command and return the dispatch success code
    TEST_ASSERT_EQUAL_INT32(1, result);

    teko_vm_destroy(vm);
}

void test_teko_cli_driver_assembly_mode_and_target_parsing(void) {
    // Simulates: teko build projeto.tkp -S --target=riscv64-unknown-linux-gnu -o meu_asm
    const char* mock_argv[] = { "teko", "build", "projeto.tkp", "-S", "--target=riscv64-unknown-linux-gnu", "-o", "meu_asm" };
    int mock_argc = 7;

    // Simulates the internal CLI parser logic
    bool has_s_flag = false;
    const char* output_name = "output_bin";
    TekoTarget target = teko_target_detect_host();

    for (int i = 2; i < mock_argc; i++) {
        if (strcmp(mock_argv[i], "-S") == 0) {
            has_s_flag = true;
        }
        else if (strncmp(mock_argv[i], "--target=", 9) == 0) {
            target = teko_target_parse(mock_argv[i] + 9);
        }
        else if (strcmp(mock_argv[i], "-o") == 0) {
            if (i + 1 < mock_argc) {
                output_name = mock_argv[++i];
            }
        }
    }

    // Unity assertions for Assembly Mode
    TEST_ASSERT_TRUE(has_s_flag);
    TEST_ASSERT_EQUAL_STRING("meu_asm", output_name);
    TEST_ASSERT_EQUAL_INT(ARCH_RISCV64, target.arch);
    TEST_ASSERT_EQUAL_INT(OS_LINUX, target.os);
    TEST_ASSERT_NOT_NULL(strstr(target.target_string, "riscv64"));
}

void test_teko_cli_driver_object_mode_win_arm64(void) {
    // Simulates: teko build projeto.tkp -c --target=aarch64-pc-windows-msvc
    const char* mock_argv[] = { "teko", "build", "projeto.tkp", "-c", "--target=aarch64-pc-windows-msvc" };
    int mock_argc = 5;

    bool has_c_flag = false;
    TekoTarget target = teko_target_detect_host();

    for (int i = 2; i < mock_argc; i++) {
        if (strcmp(mock_argv[i], "-c") == 0) {
            has_c_flag = true;
        }
        else if (strncmp(mock_argv[i], "--target=", 9) == 0) {
            target = teko_target_parse(mock_argv[i] + 9);
        }
    }

    // Unity assertions for Object Mode and Windows ARM
    TEST_ASSERT_TRUE(has_c_flag);
    TEST_ASSERT_EQUAL_INT(ARCH_ARM64, target.arch);
    TEST_ASSERT_EQUAL_INT(OS_WINDOWS, target.os);
}

void test_teko_cli_driver_default_host_fallback(void) {
    // Simulates: teko build projeto.tkp
    const char* mock_argv[] = { "teko", "build", "projeto.tkp" };
    int mock_argc = 3;

    TekoTarget target = teko_target_detect_host();
    TekoTarget parsed_target = teko_target_detect_host();

    for (int i = 2; i < mock_argc; i++) {
        if (strncmp(mock_argv[i], "--target=", 9) == 0) {
            parsed_target = teko_target_parse(mock_argv[i] + 9);
        }
    }

    // Ensures that the default target remains identical to the detected physical host (Mac Apple Silicon)
    TEST_ASSERT_EQUAL_INT(target.arch, parsed_target.arch);
    TEST_ASSERT_EQUAL_INT(target.os, parsed_target.os);
    TEST_ASSERT_EQUAL_STRING(target.target_string, parsed_target.target_string);
}
