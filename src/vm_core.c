#include "vm_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Instantiates the complete VM runtime environment, coupling the Arena
TekoVM* teko_vm_create(unsigned char* bytecode, uint32_t size, char** string_pool, uint32_t pool_count) {
    if (!bytecode || size == 0) return NULL;

    auto vm = (TekoVM*)malloc(sizeof(TekoVM));
    if (!vm) return NULL;

    vm->bytecode = bytecode;
    vm->bytecode_size = size;
    vm->ip = 0;
    vm->csp = 0;
    vm->constant_pool_count = pool_count;

    // Initializes registers to zero
    memset(vm->registers, 0, sizeof(vm->registers));

    // Clones the string constant pool into the runtime context
    if (pool_count > 0 && string_pool) {
        vm->constant_pool = (char**)malloc(sizeof(char*) * pool_count);
        for (uint32_t i = 0; i < pool_count; i++) {
            vm->constant_pool[i] = strdup(string_pool[i]);
        }
    } else {
        vm->constant_pool = NULL;
    }

    // Instantiates the native contiguous region allocator O(1)
    vm->memory_arena = teko_arena_create();

    return vm;
}

// Fast inline helpers for reading bytes from the instruction stream
static inline uint8_t read_byte(TekoVM* vm) {
    return vm->bytecode[vm->ip++];
}

static inline int32_t read_int(TekoVM* vm) {
    int32_t val = (vm->bytecode[vm->ip + 0] << 0)  |
                  (vm->bytecode[vm->ip + 1] << 8)  |
                  (vm->bytecode[vm->ip + 2] << 16) |
                  (vm->bytecode[vm->ip + 3] << 24);
    vm->ip += 4;
    return val;
}

// THE HEART OF THE INTERPRETER: Ultra-high-speed loop using C23 Computed Gotos
int32_t teko_vm_execute(TekoVM* vm) {
    if (!vm || vm->bytecode_size == 0) return -1;

    // Dispatch table directly mapping ISA Opcodes to C code labels
    static const void* dispatch_table[] = {
        [OP_HALT]           = &&do_halt,
        [OP_ICONST]         = &&do_iconst,
        [OP_SCONST]         = &&do_sconst,
        [OP_LOAD]           = &&do_load,
        [OP_STORE]          = &&do_store,
        [OP_ADD]            = &&do_add,
        [OP_SUB]            = &&do_sub,
        [OP_MUL]            = &&do_mul,
        [OP_DIV]            = &&do_div,
        [OP_SPAWN_ASYNC]    = &&do_fallback,
        [OP_AWAIT_INTENT]   = &&do_fallback,
        [OP_CHAN_INIT]      = &&do_fallback,
        [OP_CHAN_PUT]       = &&do_fallback,
        [OP_JMP]            = &&do_jmp,
        [OP_JMP_IF_FALSE]   = &&do_jmp_if_false,
        [OP_RETURN]         = &&do_return
    };

    // Dispatch macro: reads the next opcode and jumps directly to its label, bypassing switch-case
#define DISPATCH() goto *dispatch_table[read_byte(vm)]

    // Fires the execution of the first instruction
    DISPATCH();

do_halt:
    return vm->registers[0]; // Returns the value held in register r0 as the program output

do_iconst: {
    int32_t val = read_int(vm);
    vm->registers[1] = val; // Temporarily loads into r1
    DISPATCH();
}

do_sconst: {
    int32_t pool_idx = read_int(vm);
    // Simulates runtime arena-allocation of the string literal using the contiguous Arena O(1)
    if (pool_idx >= 0 && (uint32_t)pool_idx < vm->constant_pool_count) {
        char* runtime_str = (char*)teko_arena_alloc(vm->memory_arena, strlen(vm->constant_pool[pool_idx]) + 1);
        if (runtime_str) {
            strcpy(runtime_str, vm->constant_pool[pool_idx]);
        }
    }
    DISPATCH();
}

do_load: {
    int32_t reg_idx = read_int(vm);
    if (reg_idx >= 0 && reg_idx < VM_REGISTERS_COUNT) {
        vm->registers[0] = vm->registers[reg_idx];
    }
    DISPATCH();
}

do_store: {
    int32_t reg_idx = read_int(vm);
    if (reg_idx >= 0 && reg_idx < VM_REGISTERS_COUNT) {
        vm->registers[reg_idx] = vm->registers[1]; // Saves r1 contents into the target register
    }
    DISPATCH();
}

do_add:
    vm->registers[0] = vm->registers[2] + vm->registers[3];
    DISPATCH();

do_sub:
    vm->registers[0] = vm->registers[2] - vm->registers[3];
    DISPATCH();

do_mul:
    vm->registers[0] = vm->registers[2] * vm->registers[3];
    DISPATCH();

do_div:
    if (vm->registers[3] != 0) {
        vm->registers[0] = vm->registers[2] / vm->registers[3];
    }
    DISPATCH();

do_jmp: {
    int32_t target_ip = read_int(vm);
    vm->ip = target_ip;
    DISPATCH();
}

do_jmp_if_false: {
    int32_t target_ip = read_int(vm);
    if (vm->registers[0] == 0) {
        vm->ip = target_ip;
    }
    DISPATCH();
}

do_return:
    if (vm->csp > 0) {
        vm->ip = vm->call_stack[--vm->csp].return_address;
        DISPATCH();
    }
    return vm->registers[0];

do_fallback:
    // Temporary fallback for concurrency instructions to be implemented in upcoming Sprints
    DISPATCH();

#undef DISPATCH
}

// Complete and thorough deallocation of the entire interpreter context
void teko_vm_destroy(TekoVM* vm) {
    if (!vm) return;

    if (vm->constant_pool) {
        for (uint32_t i = 0; i < vm->constant_pool_count; i++) {
            if (vm->constant_pool[i]) free(vm->constant_pool[i]);
        }
        free(vm->constant_pool);
    }

    if (vm->memory_arena) {
        teko_arena_destroy(vm->memory_arena);
    }

    free(vm);
}