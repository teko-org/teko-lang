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

    // Robust arg scan: the input is the first positional that is neither the
    // optional `build` subcommand, a flag, nor a flag's value.
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-S") == 0) { stop_at_assembly = true; continue; }
        if (strcmp(argv[i], "-c") == 0) { stop_at_object = true; continue; }
        if (strncmp(argv[i], "--target=", 9) == 0) {
            if (strncmp(argv[i] + 9, "wasm", 4) == 0) target_wasm = true;
            continue;
        }
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
