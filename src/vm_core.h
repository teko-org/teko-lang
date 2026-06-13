#ifndef VM_CORE_H
#define VM_CORE_H

#include <stdint.h>
#include "codegen_li.h"
#include "vm_arena.h"

#define VM_REGISTERS_COUNT 16
#define VM_STACK_MAX_DEPTH 256

// Structure representing an activation frame of a function on the Call Stack
typedef struct {
    uint32_t return_address;     // IP address to return to
    int base_register_offset;    // Local register window
} VMFrame;

// Main structure of the Teko VM Runtime state
typedef struct {
    unsigned char* bytecode;     // Contiguous vector of opcodes (.tkb)
    uint32_t bytecode_size;      // Total size of the code segment
    uint32_t ip;                 // Instruction Pointer (current instruction pointer)

    int32_t registers[VM_REGISTERS_COUNT]; // Generic Virtual Registers

    // Constants Table (String Pool) resolved in memory
    char** constant_pool;
    uint32_t constant_pool_count;

    // Call Stack for function scope control
    VMFrame call_stack[VM_STACK_MAX_DEPTH];
    uint32_t csp;                // Call Stack Pointer

    TekoArena* memory_arena;     // The active Arena injected implicitly
} TekoVM;

// Public signatures of the Core Interpreter
TekoVM* teko_vm_create(unsigned char* bytecode, uint32_t size, char** string_pool, uint32_t pool_count);
int32_t teko_vm_execute(TekoVM* vm);
void teko_vm_destroy(TekoVM* vm);

#endif // VM_CORE_H