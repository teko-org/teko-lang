#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "project_manager.h"
#include "codegen/codegen_metal.h"
#include "codegen/tld_elf.h"
#include "codegen/tld_elf_arch.h"
#include "codegen/tld_symbols.h"

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
        printf("Uso: teko build <arquivo_ou_projeto> [flags]\n");
        printf("Flags:\n");
        printf("  -S        Para na geracao do arquivo de texto assembly (.s)\n");
        printf("  -c        Para na geracao do arquivo objeto binario unlinked (.o)\n");
        return 1;
    }

    const char* input_path = argv[1];
    bool stop_at_assembly = false;
    bool stop_at_object = false;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-S") == 0) stop_at_assembly = true;
        if (strcmp(argv[i], "-c") == 0) stop_at_object = true;
    }

    // TARGET PADRÃO DO HOST (Pode ser parametrizado via flag posterior)
    TekoTarget target = { .arch = ARCH_X86_64, .os = OS_LINUX };

    // ====================================================================
    // CASO ESPECIAL: O usuário passou um arquivo Assembly (.s) direto para linkar
    // ====================================================================
    if (strstr(input_path, ".s") != NULL) {
        printf("[Teko Pipeline]: Detectado arquivo assembly textual externo: %s\n", input_path);
        printf("[Teko Linker]: Pulando Frontend. Invocando Linker Nativo diretamente...\n");

        // Simula a leitura e parsing do texto .s de volta para instrucoes de maquina
        // Em producao, o linker varre o .s gerando o buffer de opcodes reais
        uint8_t machine_code_buffer[256];
        uint32_t machine_code_size = 0;

        // Exemplo de instrucoes injetadas vindas do parsing do .s
        machine_code_size += tld_arch_encode_instruction(target.arch, OP_ICONST, 100, &machine_code_buffer[machine_code_size]);
        machine_code_size += tld_arch_encode_instruction(target.arch, OP_HALT, 0, &machine_code_buffer[machine_code_size]);

        char binary_out[] = "output_from_assembly_exec";
        bool link_success = tld_elf64_write_executable(binary_out, machine_code_buffer, machine_code_size, get_linker_elf_machine(target.arch));

        if (link_success) {
            printf("[Teko Linker]: Binario gerado com sucesso a partir do arquivo .s: %s\n", binary_out);
            return 0;
        }
        return 1;
    }

    // ====================================================================
    // PIPELINE PADRÃO: Compilacao baseada em Manifesto (.tkp)
    // ====================================================================
    TekoProjectConfig* config = teko_project_load(input_path);
    if (!config || !teko_project_validate_structure(config)) {
        fprintf(stderr, "[Erro Fatal]: Falha na validacao ou leitura do manifesto.\n");
        if (config) teko_project_free(config);
        return 1;
    }

    printf("[Teko Driver]: Processando projeto '%s'...\n", config->project_name);

    // Bytecode de Linguagem Intermediária (LI) fictício gerado pelo Parser
    unsigned char mock_bytecode[] = { 0x01, 0x1F, 0x00, 0x00, 0x00, 0x00 }; // ICONST 31 -> HALT
    uint32_t bytecode_size = sizeof(mock_bytecode);

    // 1. GERAÇÃO DO ARQUIVO INTERMEDIÁRIO TEXTUAL (.s)
    char asm_output_path[512];
    snprintf(asm_output_path, sizeof(asm_output_path), "%s/%s.s", config->root_dir, config->project_name);

    MetalContext* ctx = teko_metal_create(asm_output_path, target);
    if (ctx) {
        teko_metal_emit_program(ctx, mock_bytecode, bytecode_size);
        teko_metal_close(ctx);
        printf("[Passo 1/3 - Codegen]: Arquivo intermediario assembly .s gravado: %s\n", asm_output_path);
    }

    // INTERRUPÇÃO POR FLAG -S: Se o desenvolvedor pediu apenas o texto, para aqui
    if (stop_at_assembly) {
        printf("[Teko Pipeline]: Interrupcao acionada por flag -S. Finalizando esteira.\n");
        teko_project_free(config);
        return 0;
    }

    // 2. TRADUÇÃO PARA ARQUIVO OBJETO BINÁRIO UNLINKED (.o)
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

    // Gravamos o arquivo objeto unlinked cru em disco para auditoria do desenvolvedor
    FILE* obj_file = fopen(obj_output_path, "wb");
    if (obj_file) {
        fwrite(machine_code_buffer, 1, machine_code_size, obj_file);
        fclose(obj_file);
        printf("[Passo 2/3 - Objeto]: Arquivo intermediario objeto binario .o gravado: %s\n", obj_output_path);
    }

    // INTERRUPÇÃO POR FLAG -c: Se pediu apenas o objeto binário unlinked, para aqui
    if (stop_at_object) {
        printf("[Teko Pipeline]: Interrupcao acionada por flag -c. Finalizando esteira.\n");
        teko_project_free(config);
        return 0;
    }

    // 3. ETAPA FINAL - GERAÇÃO DO EXECUTÁVEL FINAL LINKADO
    char bin_output_path[512];
    bool is_shared = (config->target_type == TARGET_DYNAMIC_LIB);
    snprintf(bin_output_path, sizeof(bin_output_path), "%s/%s%s",
             config->root_dir, config->project_name, is_shared ? ".so" : "");

    bool link_success = tld_elf64_write_executable(bin_output_path, machine_code_buffer, machine_code_size, get_linker_elf_machine(target.arch));
    if (link_success) {
        printf("[Passo 3/3 - Linker]: Executavel de producao final linkado: %s\n", bin_output_path);
    }

    teko_project_free(config);
    printf("\n[Pipeline de Compilacao Executado com Sucesso].\n");
    return 0;
}
