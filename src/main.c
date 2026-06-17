#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "project_manager.h"
#include "codegen/codegen_metal.h"
#include "codegen/tld_elf.h"
#include "codegen/tld_elf_arch.h"
#include "codegen/tld_symbols.h"
#include "codegen_li.h"
#include "codegen_li_wasm.h"
#include "frontend_interop.h"
#include "teko_target.h"

// Read an entire file into a NUL-terminated heap string (caller frees). NULL on error.
static char* read_file_to_string(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

// Phase 11 (Browser FFI frontend FE-D): real .tks -> IL -> WASM compile path. No
// mock bytecode: the interop frontend lexes/parses the source, lowers it to IL, and
// the bridge emits a real .wat (plus auto-generated dom.* glue alongside it).
static int compile_wasm_source(const char* input_path, const char* out_wat) {
    char* source = read_file_to_string(input_path);
    if (!source) {
        fprintf(stderr, "[Teko WASM]: cannot read source %s\n", input_path);
        return 1;
    }

    BytecodeBuffer* buffer = codegen_li_create_context();
    if (!buffer) { free(source); return 1; }

    if (teko_compile_interop(source, buffer) != 0) {
        fprintf(stderr, "[Teko WASM]: frontend failed on %s\n", input_path);
        codegen_li_free_context(buffer);
        free(source);
        return 1;
    }

    TekoTarget target;
    memset(&target, 0, sizeof target);
    target.arch = ARCH_WASM32;
    target.os = OS_WASI;
    strncpy(target.target_string, "wasm32-wasi", sizeof(target.target_string) - 1);

    // Glue path: "<out_wat without .wat>.glue.mjs" (emitted next to the module).
    char glue_path[512];
    size_t wl = strlen(out_wat);
    if (wl > 4 && strcmp(out_wat + wl - 4, ".wat") == 0) {
        snprintf(glue_path, sizeof(glue_path), "%.*s.glue.mjs", (int)(wl - 4), out_wat);
    } else {
        snprintf(glue_path, sizeof(glue_path), "%s.glue.mjs", out_wat);
    }

    int rc = codegen_li_emit_wasm(buffer, out_wat, target, glue_path, NULL, NULL, NULL, 0);
    codegen_li_free_context(buffer);
    free(source);

    if (rc == 0) {
        printf("[Teko WASM]: compiled %s -> %s (+ glue %s)\n", input_path, out_wat, glue_path);
    }
    return rc;
}

// Phase 13 (native runner): real .tks -> IL -> hosted native assembly -> system `cc`
// assemble+link against teko_rt -> a runnable executable. This replaces the mock
// bytecode on the native path for the host arches (x86_64 / arm64, ELF & Mach-O).
// `target_str` selects the target ("host"/"native" => detect host); `rt_lib` is the
// path to libteko_rt.a (default "libteko_rt.a"); `stop_at_assembly` halts after the .s.
static int compile_native_source(const char* input_path, const char* out_path,
                                 const char* target_str, const char* rt_lib,
                                 bool stop_at_assembly) {
    char* source = read_file_to_string(input_path);
    if (!source) {
        fprintf(stderr, "[Teko Native]: cannot read source %s\n", input_path);
        return 1;
    }

    BytecodeBuffer* buffer = codegen_li_create_context();
    if (!buffer) { free(source); return 1; }

    if (teko_compile_interop(source, buffer) != 0) {
        fprintf(stderr, "[Teko Native]: frontend failed on %s\n", input_path);
        codegen_li_free_context(buffer);
        free(source);
        return 1;
    }

    TekoTarget target;
    if (!target_str || strcmp(target_str, "host") == 0 || strcmp(target_str, "native") == 0) {
        target = teko_target_detect_host();
    } else {
        target = teko_target_parse(target_str);
    }

    // Derive the executable and .s paths. exe = -o value, else "<input w/o .tks>".
    char exe_buf[512];
    if (!out_path) {
        size_t il = strlen(input_path);
        if (il > 4 && strcmp(input_path + il - 4, ".tks") == 0)
            snprintf(exe_buf, sizeof(exe_buf), "%.*s", (int)(il - 4), input_path);
        else
            snprintf(exe_buf, sizeof(exe_buf), "%s.out", input_path);
        out_path = exe_buf;
    }
    char asm_path[600];
    snprintf(asm_path, sizeof(asm_path), "%s.s", out_path);

    MetalContext* ctx = teko_metal_create(asm_path, target);
    if (!ctx) { codegen_li_free_context(buffer); free(source); return 1; }

    teko_metal_set_hosted(ctx, 1);
    teko_metal_set_strings(ctx, (const char**)buffer->pool.strings, buffer->pool.count);

    TekoWasmImport* wimports = NULL;
    if (buffer->import_count > 0) {
        wimports = (TekoWasmImport*)malloc(sizeof(TekoWasmImport) * buffer->import_count);
        if (wimports) {
            for (int i = 0; i < buffer->import_count; i++) {
                wimports[i].ns = buffer->imports[i].ns;
                wimports[i].name = buffer->imports[i].name;
                wimports[i].n_params = buffer->imports[i].n_params;
                wimports[i].has_result = buffer->imports[i].has_result;
            }
            teko_metal_set_imports(ctx, wimports, buffer->import_count);
        }
    }
    teko_metal_set_local_count(ctx, buffer->local_count);
    // Phase 14 (14.A): emit the routine function-pointer table + a teko_rt_run drain at
    // $main exit when the program fires background tasks (`routines { … }`). Spawn-free
    // programs leave the flag 0 → emitted assembly is byte-identical to before Phase 14.
    teko_metal_set_emit_spawn(ctx, buffer->uses_spawn);
    // Phase 17 (17.A): hand the hosted native emitter the float-constant pool (OP_FCONST's index
    // space) + the uses_float flag. Float-free programs leave uses_float 0 → emitted assembly
    // byte-identical to before Phase 17.
    teko_metal_set_floats(ctx, buffer->floats, buffer->float_count);
    teko_metal_set_emit_float(ctx, buffer->uses_float);
    // Phase 17.F.3: hand the hosted native emitter the decimal-constant pool (OP_DCONST's index
    // space; 256-byte blobs) + the uses_decimal flag. Decimal-free programs leave uses_decimal 0 →
    // emitted assembly byte-identical to before 17.F.3.
    teko_metal_set_decimals(ctx, buffer->decimals, buffer->decimal_count);
    teko_metal_set_emit_decimal(ctx, buffer->uses_decimal);
    // Phase 18 (18.E.4): emit the REAL per-ISA SIMD reduction kernel (teko_simd_sum_i32) once when
    // the program uses simd.sum. Native gates the KERNEL emission on this (the OP_SIMD_SUM lowering
    // itself routes unconditionally, like OP_IARR_*); simd-free programs leave it 0 → byte-identical.
    teko_metal_set_emit_simd(ctx, buffer->uses_simd);

    teko_metal_emit_program(ctx, buffer->code, (uint32_t)buffer->size);
    teko_metal_close(ctx);
    codegen_li_free_context(buffer);
    free(source);
    free(wimports);

    printf("[Teko Native]: emitted hosted assembly: %s\n", asm_path);
    if (stop_at_assembly) {
        printf("[Teko Pipeline]: Pipeline halted by -S flag. Finishing.\n");
        return 0;
    }

    // Assemble + link with the system C toolchain against the teko_rt archive.
    const char* lib = rt_lib ? rt_lib : "libteko_rt.a";
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "cc \"%s\" \"%s\" -o \"%s\"", asm_path, lib, out_path);
    printf("[Teko Native]: linking: %s\n", cmd);
    int rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "[Teko Native]: link failed (cc exit %d)\n", rc);
        return 1;
    }
    printf("[Teko Native]: produced executable: %s\n", out_path);
    return 0;
}

static uint16_t get_linker_elf_machine(TekoArch arch) {
    switch (arch) {
        case ARCH_X86_64: return 62;
        case ARCH_X86:    return 3;
        case ARCH_ARM64:  return 183;
        case ARCH_ARM32:  return 40;
        default:          return 62;
    }
}

int main(int argc, char** argv) {
    printf("==================================================\n");
    printf(" Teko AOT Compiler Driver - Multi-Mode Pipeline   \n");
    printf("==================================================\n\n");

    if (argc < 2) {
        printf("Usage: teko build <file_or_project> [flags]\n");
        printf("Flags:\n");
        printf("  -S        Stop after generating the assembly text file (.s)\n");
        printf("  -c        Stop after generating the unlinked binary object file (.o)\n");
        return 1;
    }

    bool stop_at_assembly = false;
    bool stop_at_object = false;
    bool target_wasm = false;
    const char* out_path = NULL;
    const char* input_path = NULL;
    const char* target_str = NULL;  // value of --target= (NULL => default project path)
    const char* rt_lib = NULL;      // value of --rt-lib= (path to libteko_rt.a)

    // Robust arg scan: the input is the first positional that is neither the
    // optional `build` subcommand, a flag, nor a flag's value.
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-S") == 0) { stop_at_assembly = true; continue; }
        if (strcmp(argv[i], "-c") == 0) { stop_at_object = true; continue; }
        if (strncmp(argv[i], "--target=", 9) == 0) {
            target_str = argv[i] + 9;
            if (strstr(target_str, "wasm") != NULL) target_wasm = true;
            continue;
        }
        if (strncmp(argv[i], "--rt-lib=", 9) == 0) { rt_lib = argv[i] + 9; continue; }
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) { out_path = argv[++i]; continue; }
        if (strcmp(argv[i], "build") == 0) continue; // optional subcommand
        if (argv[i][0] == '-') continue;             // unknown flag
        if (!input_path) input_path = argv[i];       // first positional = input
    }

    if (!input_path) {
        fprintf(stderr, "[Fatal Error]: no input file/project given.\n");
        return 1;
    }

    // ====================================================================
    // WASM TARGET: real .tks -> IL -> WASM (Phase 11 Browser FFI; no mock).
    // ====================================================================
    if (target_wasm) {
        char derived[512];
        if (!out_path) {
            // Default output: "<input without .tks>.wat" (or "<input>.wat").
            size_t il = strlen(input_path);
            if (il > 4 && strcmp(input_path + il - 4, ".tks") == 0) {
                snprintf(derived, sizeof(derived), "%.*s.wat", (int)(il - 4), input_path);
            } else {
                snprintf(derived, sizeof(derived), "%s.wat", input_path);
            }
            out_path = derived;
        }
        printf("[Teko Pipeline]: WASM target — compiling Teko source directly.\n");
        int rc = compile_wasm_source(input_path, out_path);
        if (rc == 0) printf("\n[Compilation Pipeline Executed Successfully].\n");
        return rc;
    }

    // ====================================================================
    // NATIVE TARGET: real .tks -> IL -> hosted assembly -> cc -> executable
    // (Phase 13 native runner). Triggered by an explicit --target=<native>.
    // ====================================================================
    if (target_str != NULL) {
        printf("[Teko Pipeline]: Native target '%s' — compiling Teko source directly.\n", target_str);
        int rc = compile_native_source(input_path, out_path, target_str, rt_lib, stop_at_assembly);
        if (rc == 0) printf("\n[Compilation Pipeline Executed Successfully].\n");
        return rc;
    }

    // DEFAULT HOST TARGET (can be parameterized via a later flag)
    TekoTarget target = { .arch = ARCH_X86_64, .os = OS_LINUX };

    // ====================================================================
    // SPECIAL CASE: The user passed an Assembly file (.s) directly for linking
    // ====================================================================
    if (strstr(input_path, ".s") != NULL) {
        printf("[Teko Pipeline]: Detected external textual assembly file: %s\n", input_path);
        printf("[Teko Linker]: Skipping Frontend. Invoking Native Linker directly...\n");

        // Simulates reading and parsing the .s text back into machine instructions
        // In production, the linker scans the .s generating the real opcode buffer
        uint8_t machine_code_buffer[256];
        uint32_t machine_code_size = 0;

        // Example instructions injected from parsing the .s
        machine_code_size += tld_arch_encode_instruction(target.arch, OP_ICONST, 100, &machine_code_buffer[machine_code_size]);
        machine_code_size += tld_arch_encode_instruction(target.arch, OP_HALT, 0, &machine_code_buffer[machine_code_size]);

        char binary_out[] = "output_from_assembly_exec";
        bool link_success = tld_elf64_write_executable(binary_out, machine_code_buffer, machine_code_size, get_linker_elf_machine(target.arch));

        if (link_success) {
            printf("[Teko Linker]: Binary successfully generated from .s file: %s\n", binary_out);
            return 0;
        }
        return 1;
    }

    // ====================================================================
    // STANDARD PIPELINE: Compilation based on Manifest (.tkp)
    // ====================================================================
    TekoProjectConfig* config = teko_project_load(input_path);
    if (!config || !teko_project_validate_structure(config)) {
        fprintf(stderr, "[Fatal Error]: Failed to validate or read the manifest.\n");
        if (config) teko_project_free(config);
        return 1;
    }

    printf("[Teko Driver]: Processing project '%s'...\n", config->project_name);

    // Mock Intermediate Language (IL) bytecode generated by the Parser
    unsigned char mock_bytecode[] = { 0x01, 0x1F, 0x00, 0x00, 0x00, 0x00 }; // ICONST 31 -> HALT
    uint32_t bytecode_size = sizeof(mock_bytecode);

    // 1. GENERATION OF THE INTERMEDIATE TEXTUAL FILE (.s)
    char asm_output_path[512];
    snprintf(asm_output_path, sizeof(asm_output_path), "%s/%s.s", config->root_dir, config->project_name);

    MetalContext* ctx = teko_metal_create(asm_output_path, target);
    if (ctx) {
        teko_metal_emit_program(ctx, mock_bytecode, bytecode_size);
        teko_metal_close(ctx);
        printf("[Step 1/3 - Codegen]: Intermediate assembly .s file written: %s\n", asm_output_path);
    }

    // INTERRUPTION BY FLAG -S: If the developer requested only the text file, stop here
    if (stop_at_assembly) {
        printf("[Teko Pipeline]: Pipeline halted by -S flag. Finishing.\n");
        teko_project_free(config);
        return 0;
    }

    // 2. TRANSLATION TO UNLINKED BINARY OBJECT FILE (.o)
    uint8_t machine_code_buffer[4096];
    uint32_t machine_code_size = 0;
    uint32_t cursor = 0;

    while (cursor < bytecode_size) {
        OpCode op = (OpCode)mock_bytecode[cursor++];
        int32_t arg = 0;
        if (op == OP_ICONST) {
            arg = mock_bytecode[cursor] | (mock_bytecode[cursor+1] << 8) |
                  (mock_bytecode[cursor+2] << 16) | (mock_bytecode[cursor+3] << 24);
            cursor += 4;
        }
        machine_code_size += tld_arch_encode_instruction(target.arch, op, arg, &machine_code_buffer[machine_code_size]);
    }

    char obj_output_path[512];
    snprintf(obj_output_path, sizeof(obj_output_path), "%s/%s.o", config->root_dir, config->project_name);

    // Write the raw unlinked object file to disk for developer inspection
    FILE* obj_file = fopen(obj_output_path, "wb");
    if (obj_file) {
        fwrite(machine_code_buffer, 1, machine_code_size, obj_file);
        fclose(obj_file);
        printf("[Step 2/3 - Object]: Intermediate binary object .o file written: %s\n", obj_output_path);
    }

    // INTERRUPTION BY FLAG -c: If only the unlinked binary object was requested, stop here
    if (stop_at_object) {
        printf("[Teko Pipeline]: Pipeline halted by -c flag. Finishing.\n");
        teko_project_free(config);
        return 0;
    }

    // 3. FINAL STAGE - GENERATION OF THE FINAL LINKED EXECUTABLE
    char bin_output_path[512];
    bool is_shared = (config->target_type == TARGET_DYNAMIC_LIB);
    snprintf(bin_output_path, sizeof(bin_output_path), "%s/%s%s",
             config->root_dir, config->project_name, is_shared ? ".so" : "");

    bool link_success = tld_elf64_write_executable(bin_output_path, machine_code_buffer, machine_code_size, get_linker_elf_machine(target.arch));
    if (link_success) {
        printf("[Step 3/3 - Linker]: Final production executable linked: %s\n", bin_output_path);
    }

    teko_project_free(config);
    printf("\n[Compilation Pipeline Executed Successfully].\n");
    return 0;
}
