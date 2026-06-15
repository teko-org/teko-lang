cmake_minimum_required(VERSION 3.25)
project(teko C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

# --- Native Dependency Manager (Unity) ---
include(FetchContent)
FetchContent_Declare(
        unity
        GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity
        GIT_TAG        v2.6.1
)
FetchContent_MakeAvailable(unity)

# 1. Core compiler engine sources (everything in src/ except the main entry point).
#    Listed explicitly on purpose: GLOB_RECURSE does not re-trigger CMake when files
#    are added/removed, which causes stale builds. When you add a new .c file under
#    src/, add it to this list.

# Native cryptography (Phase 13) — embedded runtime primitives, the single source of
# truth. Shared between teko_core (compiler + KAT suite) and teko_rt (the archive linked
# into produced native binaries via the hosted runner). When you add a crypto .c, add it
# here once.
set(TEKO_CRYPTO_SOURCES
        src/runtime/teko_crypto_sha2.c
        src/runtime/teko_crypto_md5.c
        src/runtime/teko_crypto_sha1.c
        src/runtime/teko_crypto_sha512.c
        src/runtime/teko_crypto_hmac.c
        src/runtime/teko_crypto_sha3.c
        src/runtime/teko_crypto_blake3.c
        src/runtime/teko_crypto_random.c
        src/runtime/teko_crypto_hkdf.c
        src/runtime/teko_crypto_pbkdf2.c
        src/runtime/teko_crypto_chacha20.c
        src/runtime/teko_crypto_poly1305.c
        src/runtime/teko_crypto_chachapoly.c
        src/runtime/teko_crypto_aes.c
        src/runtime/teko_crypto_aes_modes.c
        src/runtime/teko_crypto_aes_gcm.c
        src/runtime/teko_crypto_scrypt.c
        src/runtime/teko_crypto_blake2b.c
        src/runtime/teko_crypto_argon2.c
        src/runtime/teko_crypto_fe25519.c
        src/runtime/teko_crypto_x25519.c
        src/runtime/teko_crypto_ed25519.c
        src/runtime/teko_uuid.c
        src/runtime/teko_crypto_bn.c
        src/runtime/teko_crypto_ec.c
        src/runtime/teko_crypto_ecc.c
        src/runtime/teko_crypto_p256.c
        src/runtime/teko_crypto_p384.c
        src/runtime/teko_crypto_rsa.c
)

set(CORE_SOURCES
        # Frontend: lexer + parser modules
        src/lexer.c
        src/lexer_string.c
        src/parser.c
        src/parser_async_control.c
        src/parser_concurrent.c
        src/parser_di.c
        src/parser_extensions.c
        src/parser_ffi.c
        src/parser_generics.c
        src/parser_messaging.c
        src/parser_statements.c
        src/parser_string.c
        src/parser_types.c
        src/parser_visibility.c
        # Semantics + symbols + type checking
        src/semantic_concurrent.c
        src/semantic_struct.c
        src/symbol_table.c
        src/type_checker.c
        # Intermediate language + bytecode codegen
        src/codegen_li.c
        src/codegen_li_wasm.c
        src/frontend_interop.c
        src/codegen_aot.c
        src/teko_il.c
        # Virtual machine + runtime
        src/vm_core.c
        src/vm_arena.c
        src/vm_concurrency.c
        src/vm_debug.c
        src/vm_intrinsics.c
        src/vm_scheduler.c
        src/runtime/teko_arena.c
        src/runtime/teko_channel.c
        src/runtime/teko_mem_sys.c
        src/runtime/teko_thread.c
        # Native cryptography (Phase 13) — embedded runtime primitives (shared list)
        ${TEKO_CRYPTO_SOURCES}
        # Tooling: target detection, SDK, LSP, project manager
        src/teko_target.c
        src/teko_sdk.c
        src/teko_lsp.c
        src/project_manager.c
        # Metal backend orchestrator + optimizer
        src/codegen/codegen_metal.c
        src/codegen/codegen_opt.c
        # Shared per-family emitter cores (de-duplicated)
        src/codegen/emit_x86_64_sysv_common.c
        src/codegen/emit_arm64_gas_common.c
        # Phase 13 native runner: libc-hosted, assemble-able host-arch emitter
        src/codegen/emit_native_hosted.c
        # Native static linker (tld): object/executable format writers
        src/codegen/tld_elf.c
        src/codegen/tld_elf_arch.c
        src/codegen/tld_elf_reloc.c
        src/codegen/tld_macho.c
        src/codegen/tld_pe.c
        src/codegen/tld_symbols.c
        src/codegen/tld_wasm.c
        # Per-target instruction emitters
        src/codegen/apple/emit_darwin_arm64.c
        src/codegen/apple/emit_darwin_x86_64.c
        src/codegen/bare_metal/emit_wasm.c
        src/codegen/bsd_unix/emit_freebsd_arm64.c
        src/codegen/bsd_unix/emit_freebsd_x86_64.c
        src/codegen/linux/emit_linux_arm32.c
        src/codegen/linux/emit_linux_arm64.c
        src/codegen/linux/emit_linux_mips.c
        src/codegen/linux/emit_linux_ppc64.c
        src/codegen/linux/emit_linux_riscv32.c
        src/codegen/linux/emit_linux_riscv64.c
        src/codegen/linux/emit_linux_riscv_common.c
        src/codegen/linux/emit_linux_x86.c
        src/codegen/linux/emit_linux_x86_64.c
        src/codegen/windows/emit_win_arm64.c
        src/codegen/windows/emit_win_x86.c
        src/codegen/windows/emit_win_x86_64.c
)

# Create the internal static library with the entire compiler engine
add_library(teko_core STATIC ${CORE_SOURCES})

# Ensure visibility of includes in relative subdirectories
target_include_directories(teko_core PRIVATE src)

# 2. TARGET 1: The Official Compiler (Production Entry Point)
add_executable(teko src/main.c)
target_link_libraries(teko PRIVATE teko_core)
target_include_directories(teko PRIVATE src)

# 2b. TARGET: teko_rt — the native runtime archive linked into produced executables.
#     A `teko build --target=<native>` binary links against libteko_rt.a; it carries
#     the surface ABI shims (teko_rt_emit, and the crypto wrappers added in Sub-phase B)
#     over the portable C crypto runtime (the single source of truth). Self-contained so
#     produced binaries need nothing else.
add_library(teko_rt STATIC runtime/native/teko_rt.c ${TEKO_CRYPTO_SOURCES})
target_include_directories(teko_rt PRIVATE runtime/native src/runtime src)
if (WIN32)
    target_link_libraries(teko_rt PRIVATE bcrypt) # CSPRNG: BCryptGenRandom
endif()

# 3. TARGET 2: The Isolated Test Suite (Test Entry Point).
#    Also listed explicitly. When you add a new test .c file, add it here.
set(TEST_SOURCES
        tests/test_main.c
        tests/test_async.c
        tests/test_codegen_li.c
        tests/test_concurrent.c
        tests/test_di_semantics.c
        tests/test_messaging.c
        tests/test_project.c
        tests/test_semantics.c
        tests/test_statement.c
        tests/test_strings.c
        tests/test_lexer_phase12.c
        tests/test_target.c
        tests/test_teko_il.c
        tests/test_tooling.c
        tests/test_type_checker.c
        tests/test_types.c
        tests/test_visibility.c
        tests/test_ffi.c
        tests/test_generics.c
        tests/test_extensions.c
        tests/test_codegen_aot.c
        tests/test_vm_arena.c
        tests/test_vm_concurrency.c
        tests/test_vm_core.c
        tests/test_vm_debug.c
        tests/test_vm_scheduler.c
        tests/codegen/test_codegen_apple.c
        tests/codegen/test_codegen_cse.c
        tests/codegen/test_codegen_dce.c
        tests/codegen/test_codegen_emitters_arithmetic.c
        tests/codegen/test_codegen_embedded.c
        tests/codegen/test_codegen_folding.c
        tests/codegen/test_codegen_linker.c
        tests/codegen/test_codegen_linker_arch.c
        tests/codegen/test_codegen_linker_e2e.c
        tests/codegen/test_codegen_linker_macho.c
        tests/codegen/test_codegen_linker_pe.c
        tests/codegen/test_codegen_linker_reloc.c
        tests/codegen/test_codegen_linker_wasm.c
        tests/codegen/test_codegen_linux.c
        tests/codegen/test_codegen_opt.c
        tests/codegen/test_codegen_symbols.c
        tests/codegen/test_codegen_unix.c
        tests/codegen/test_codegen_windows.c
        tests/runtime/test_runtime_arena.c
        tests/runtime/test_runtime_channel.c
        tests/runtime/test_runtime_mem.c
        tests/runtime/test_runtime_thread.c
        tests/runtime/test_crypto_sha2.c
        tests/runtime/test_crypto_md5.c
        tests/runtime/test_crypto_sha1.c
        tests/runtime/test_crypto_sha512.c
        tests/runtime/test_crypto_hmac.c
        tests/runtime/test_crypto_sha3.c
        tests/runtime/test_crypto_blake3.c
        tests/runtime/test_crypto_random.c
        tests/runtime/test_crypto_hkdf.c
        tests/runtime/test_crypto_pbkdf2.c
        tests/runtime/test_crypto_chacha20.c
        tests/runtime/test_crypto_poly1305.c
        tests/runtime/test_crypto_chachapoly.c
        tests/runtime/test_crypto_aes.c
        tests/runtime/test_crypto_aes_modes.c
        tests/runtime/test_crypto_aes_gcm.c
        tests/runtime/test_crypto_scrypt.c
        tests/runtime/test_crypto_blake2b.c
        tests/runtime/test_crypto_argon2.c
        tests/runtime/test_crypto_x25519.c
        tests/runtime/test_crypto_ed25519.c
        tests/runtime/test_uuid.c
        tests/runtime/test_crypto_bn.c
        tests/runtime/test_crypto_p256.c
        tests/runtime/test_crypto_p384.c
        tests/runtime/test_crypto_rsa.c
)
add_executable(teko_tests ${TEST_SOURCES})
target_link_libraries(teko_tests PRIVATE teko_core unity)
target_include_directories(teko_tests PRIVATE src)

# Phase 13.3a: the native CSPRNG uses BCryptGenRandom on Windows (bcrypt.lib). Other
# platforms route to arc4random_buf / getrandom / /dev/urandom (no extra link needed).
if (WIN32)
    target_link_libraries(teko PRIVATE bcrypt)
    target_link_libraries(teko_tests PRIVATE bcrypt)
endif()
