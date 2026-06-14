#include "unity.h"
#include <stdio.h>
#if defined(_MSC_VER)
#include <stdlib.h>   // _set_error_mode, _OUT_TO_STDERR
#include <crtdbg.h>   // _CrtSetReportMode / _CrtSetReportFile (/RTC1 + assert routing)
#endif

void setUp(void) {}
// Flush after every test so that, if a later test crashes (e.g. a segfault on a
// CI runner), the log still shows every test that completed — the crashing test
// is the next one in RUN_TEST order. On Windows a pipe'd stdout is fully buffered,
// so without this a crash loses all output and the failing test is invisible.
void tearDown(void) { fflush(stdout); fflush(stderr); }

extern void test_string_raw_interpolation_and_arity(void);
extern void test_async_control_flow_and_raised_catch(void);
extern void test_concurrency_and_channel_semantics(void);
extern void test_cqrs_handler_with_dependency_injection(void);
extern void test_ffi_extern_struct_function_and_block_parsing(void);
extern void test_generics_type_parameters_and_where_constraints(void);
extern void test_extensions_methods_and_operator_overload_parsing(void);
extern void test_codegen_aot_c_transpilation_emits_runtime_and_statements(void);
extern void test_global_visibility_modifiers_and_service_restriction(void);
extern void test_parser_recovery_on_syntax_error(void);
extern void test_virtual_namespace_expansion_at_sign(void);
extern void test_reserved_namespace_protection(void);
extern void test_concurrency_methods_validation(void);
extern void test_symbol_table_scopes_and_mutability(void);
extern void test_required_properties_validation(void);
extern void test_type_compatibility_assignment(void);
extern void test_let_immutability_protection(void);
extern void test_bytecode_emission_and_constant_pooling(void);
extern void test_di_lifetime_arena_assignment(void);
extern void test_escape_analysis_region_promotion(void);
extern void test_implicit_precision_coercion_for_arbitrary_types(void);
extern void test_composite_assignment_immutability_protection(void);
extern void test_elvis_operator_coalescence_semantics(void);
extern void test_safe_navigation_with_elvis_coalescence(void);
extern void test_teko_il_binary_header_integrity(void);
extern void test_vm_arena_linear_allocation_and_reset(void);
extern void test_vm_core_mathematical_execution_loop(void);
extern void test_vm_scheduler_context_switching_and_spawn(void);
extern void test_vm_channel_blocking_and_unblocking(void);
extern void test_vm_debugger_breakpoint_interception_and_dap(void);
extern void test_teko_project_manifest_parsing(void);
extern void test_vm_intrinsic_resolution_and_execution(void);
extern void test_virtual_sdk_injection_and_isolation(void);
extern void test_teko_lsp_protocol_handshake_and_routing(void);
extern void test_teko_bare_metal_target_detection_and_parsing(void);
extern void test_teko_cli_driver_assembly_mode_and_target_parsing(void);
extern void test_teko_cli_driver_object_mode_win_arm64(void);
extern void test_teko_cli_driver_default_host_fallback(void);
extern void test_teko_aot_darwin_x86_64_pure_emission(void);
extern void test_teko_aot_darwin_arm64_pure_emission(void);
extern void test_teko_aot_linux_x86_64_pure_emission(void);
extern void test_teko_aot_linux_x86_32_pure_emission(void);
extern void test_teko_aot_linux_riscv64_pure_emission(void);
extern void test_teko_aot_linux_riscv32_pure_emission(void);
extern void test_teko_aot_linux_ppc64_pure_emission(void);
extern void test_teko_aot_linux_mips_pure_emission(void);
extern void test_teko_aot_linux_arm64_pure_emission(void);
extern void test_teko_aot_linux_arm32_pure_emission(void);
extern void test_teko_aot_wasm_pure_emission_integrity(void);
extern void test_teko_aot_wasm_arena_and_concurrency_hooks(void);
extern void test_teko_aot_wasm_multifunction_spawn_lowering(void);
extern void test_teko_aot_wasm_midfunction_suspension(void);
extern void test_teko_aot_wasm_threads_layer_b_emission(void);
extern void test_teko_aot_wasm_multispawn_contention(void);
extern void test_teko_aot_freebsd_x86_64_pure_emission(void);
extern void test_teko_aot_freebsd_arm64_pure_emission(void);
extern void test_teko_aot_windows_x86_32_pure_emission(void);
extern void test_teko_aot_windows_x86_64_pure_emission(void);
extern void test_teko_aot_windows_arm64_pure_emission(void);
extern void test_teko_aot_escape_analysis_register_allocation(void);
extern void test_teko_aot_dead_code_elimination_purgue(void);
extern void test_teko_aot_common_subexpression_elimination_filter(void);
extern void test_teko_aot_constant_folding_math_collapse(void);
extern void test_teko_linker_elf64_binary_header_generation(void);
extern void test_teko_linker_elf32_binary_header_and_class_validation(void);
extern void test_teko_linker_elf64_relative_jmp_patch_calculation(void);
extern void test_teko_linker_arch_x64_mov_encoding_integrity(void);
extern void test_teko_linker_arch_arm64_add_encoding_integrity(void);
extern void test_teko_linker_arch_x86_32_halt_encoding(void);
extern void test_teko_linker_arch_arm32_mov_encoding(void);
extern void test_teko_linker_arch_ppc64_add_encoding(void);
extern void test_tld_static_dependency_injection_and_symbol_resolution(void);
extern void test_teko_linker_e2e_extern_service_injection_and_elf_generation(void);
extern void test_teko_linker_wasm_leb128_compression_logic(void);
extern void test_teko_linker_macho_64_binary_signature_integrity(void);
extern void test_teko_linker_pe_coff_multi_architecture_signatures(void);
extern void test_codegen_emitters_arithmetic_per_target(void);

extern void test_teko_runtime_sys_allocation_and_page_recycling(void);
extern void test_teko_runtime_scheduler_cooperative_multithreading(void);
extern void test_teko_runtime_channels_blocking_and_signaling(void);
extern void test_teko_runtime_arena_thread_isolation_and_alignment(void);

int main(void) {
    // Unbuffered output: stream every line live so a crash on a CI runner shows
    // exactly how far the suite got (Windows pipes are otherwise fully buffered).
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

#if defined(_MSC_VER)
    // CI diagnostics: route the MSVC CRT error/assert and runtime-check (/RTC1)
    // reports to stderr instead of a GUI message box, so a /RTC "used without
    // being initialized" failure prints its file:line and aborts non-interactively
    // on a headless runner (otherwise it would hang waiting on a dialog).
    _set_error_mode(_OUT_TO_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    UNITY_BEGIN();

    RUN_TEST(test_string_raw_interpolation_and_arity);
    RUN_TEST(test_async_control_flow_and_raised_catch);
    RUN_TEST(test_cqrs_handler_with_dependency_injection);
    RUN_TEST(test_ffi_extern_struct_function_and_block_parsing);
    RUN_TEST(test_generics_type_parameters_and_where_constraints);
    RUN_TEST(test_extensions_methods_and_operator_overload_parsing);
    RUN_TEST(test_codegen_aot_c_transpilation_emits_runtime_and_statements);
    RUN_TEST(test_parser_recovery_on_syntax_error);
    RUN_TEST(test_global_visibility_modifiers_and_service_restriction);
    RUN_TEST(test_virtual_namespace_expansion_at_sign);
    RUN_TEST(test_reserved_namespace_protection);
    RUN_TEST(test_concurrency_and_channel_semantics);
    RUN_TEST(test_concurrency_methods_validation);
    RUN_TEST(test_symbol_table_scopes_and_mutability);
    RUN_TEST(test_required_properties_validation);
    RUN_TEST(test_type_compatibility_assignment);
    RUN_TEST(test_let_immutability_protection);
    RUN_TEST(test_bytecode_emission_and_constant_pooling);
    RUN_TEST(test_di_lifetime_arena_assignment);
    RUN_TEST(test_escape_analysis_region_promotion);
    RUN_TEST(test_implicit_precision_coercion_for_arbitrary_types);
    RUN_TEST(test_composite_assignment_immutability_protection);
    RUN_TEST(test_elvis_operator_coalescence_semantics);
    RUN_TEST(test_safe_navigation_with_elvis_coalescence);
    RUN_TEST(test_teko_il_binary_header_integrity);
    RUN_TEST(test_vm_arena_linear_allocation_and_reset);
    RUN_TEST(test_vm_core_mathematical_execution_loop);
    RUN_TEST(test_vm_scheduler_context_switching_and_spawn);
    RUN_TEST(test_vm_channel_blocking_and_unblocking);
    RUN_TEST(test_vm_debugger_breakpoint_interception_and_dap);
    RUN_TEST(test_teko_project_manifest_parsing);
    RUN_TEST(test_vm_intrinsic_resolution_and_execution);
    RUN_TEST(test_virtual_sdk_injection_and_isolation);
    RUN_TEST(test_teko_lsp_protocol_handshake_and_routing);
    RUN_TEST(test_teko_bare_metal_target_detection_and_parsing);
    RUN_TEST(test_teko_cli_driver_assembly_mode_and_target_parsing);
    RUN_TEST(test_teko_cli_driver_object_mode_win_arm64);
    RUN_TEST(test_teko_cli_driver_default_host_fallback);
    RUN_TEST(test_teko_aot_darwin_x86_64_pure_emission);
    RUN_TEST(test_teko_aot_darwin_arm64_pure_emission);
    RUN_TEST(test_teko_aot_linux_x86_64_pure_emission);
    RUN_TEST(test_teko_aot_linux_x86_32_pure_emission);
    RUN_TEST(test_teko_aot_linux_riscv64_pure_emission);
    RUN_TEST(test_teko_aot_linux_riscv32_pure_emission);
    RUN_TEST(test_teko_aot_linux_ppc64_pure_emission);
    RUN_TEST(test_teko_aot_linux_mips_pure_emission);
    RUN_TEST(test_teko_aot_linux_arm64_pure_emission);
    RUN_TEST(test_teko_aot_linux_arm32_pure_emission);
    RUN_TEST(test_teko_aot_wasm_pure_emission_integrity);
    RUN_TEST(test_teko_aot_wasm_arena_and_concurrency_hooks);
    RUN_TEST(test_teko_aot_wasm_multifunction_spawn_lowering);
    RUN_TEST(test_teko_aot_wasm_midfunction_suspension);
    RUN_TEST(test_teko_aot_wasm_threads_layer_b_emission);
    RUN_TEST(test_teko_aot_wasm_multispawn_contention);
    RUN_TEST(test_teko_aot_freebsd_x86_64_pure_emission);
    RUN_TEST(test_teko_aot_freebsd_arm64_pure_emission);
    RUN_TEST(test_teko_aot_windows_x86_32_pure_emission);
    RUN_TEST(test_teko_aot_windows_x86_64_pure_emission);
    RUN_TEST(test_teko_aot_windows_arm64_pure_emission);
    RUN_TEST(test_teko_aot_escape_analysis_register_allocation);
    RUN_TEST(test_teko_aot_dead_code_elimination_purgue);
    RUN_TEST(test_teko_aot_common_subexpression_elimination_filter);
    RUN_TEST(test_teko_aot_constant_folding_math_collapse);
    RUN_TEST(test_teko_linker_elf64_binary_header_generation);
    RUN_TEST(test_teko_linker_elf64_relative_jmp_patch_calculation);
    RUN_TEST(test_teko_linker_arch_x64_mov_encoding_integrity);
    RUN_TEST(test_teko_linker_arch_arm64_add_encoding_integrity);
    RUN_TEST(test_teko_linker_arch_x86_32_halt_encoding);
    RUN_TEST(test_teko_linker_arch_arm32_mov_encoding);
    RUN_TEST(test_teko_linker_arch_ppc64_add_encoding);
    RUN_TEST(test_tld_static_dependency_injection_and_symbol_resolution);
    RUN_TEST(test_teko_linker_e2e_extern_service_injection_and_elf_generation);
    RUN_TEST(test_teko_linker_elf32_binary_header_and_class_validation);
    RUN_TEST(test_teko_linker_wasm_leb128_compression_logic);
    RUN_TEST(test_teko_linker_macho_64_binary_signature_integrity);
    RUN_TEST(test_teko_linker_pe_coff_multi_architecture_signatures);
    RUN_TEST(test_codegen_emitters_arithmetic_per_target);

    RUN_TEST(test_teko_runtime_sys_allocation_and_page_recycling);
    RUN_TEST(test_teko_runtime_scheduler_cooperative_multithreading);
    RUN_TEST(test_teko_runtime_channels_blocking_and_signaling);
    RUN_TEST(test_teko_runtime_arena_thread_isolation_and_alignment);

    return UNITY_END();
}