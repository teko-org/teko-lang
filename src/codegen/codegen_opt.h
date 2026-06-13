#ifndef CODEGEN_OPT_H
#define CODEGEN_OPT_H

#include <stdbool.h>
#include <stdint.h>
#include "../codegen_li.h"

// Enumeration of Assigned Optimized Registers
typedef enum {
    REG_ACCUMULATOR, // w0 / %eax (default return/interrupt)
    REG_BASE,        // w1 / %ebx (default secondary operand)
    REG_TEMP0,       // w2 / %ecx (optimized O(1) local retention)
    REG_TEMP1,       // w3 / %edx (optimized O(1) local retention)
    REG_ARENA        // x19 / %r12 (escaped to memory)
} TekoPhysReg;

// Structure that tracks the lifecycle of an instruction in the LI
typedef struct {
    OpCode op;
    int32_t arg;
    bool escapes;
    TekoPhysReg assigned_reg;
} OptInstruction;

// Public signatures of the AOT Optimization Engine
void teko_optimize_register_allocation(const unsigned char* bytecode, uint32_t size, OptInstruction* out_optimized, uint32_t* out_count);

#endif // CODEGEN_OPT_H
