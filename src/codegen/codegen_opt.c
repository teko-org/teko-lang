#include "codegen_opt.h"
#include <string.h>

void teko_optimize_register_allocation(const unsigned char* bytecode, uint32_t size, OptInstruction* out_optimized, uint32_t* out_count) {
    if (!bytecode || size == 0 || !out_optimized || !out_count) return;

    uint32_t i = 0;
    uint32_t count = 0;

    // Step 1: Initial linear transpilation into the analysis array
    while (i < size) {
        OpCode op = (OpCode)bytecode[i++];
        int32_t arg = 0;

        if (op == OP_ICONST || op == OP_SCONST || op == OP_JMP || op == OP_JMP_IF_FALSE) {
            arg = (bytecode[i + 0] << 0)  |
                  (bytecode[i + 1] << 8)  |
                  (bytecode[i + 2] << 16) |
                  (bytecode[i + 3] << 24);
            i += 4;
        }

        out_optimized[count].op = op;
        out_optimized[count].arg = arg;
        out_optimized[count].escapes = false;
        out_optimized[count].assigned_reg = REG_ACCUMULATOR; // Default
        count++;
    }

    // Step 2: Static Escape Analysis (sweep backwards looking for barriers)
    bool future_escape_detected = false;
    for (int32_t j = (int32_t)count - 1; j >= 0; j--) {
        OpCode current_op = out_optimized[j].op;

        // Physical escape barriers of the language: returns, calls, and concurrency
        if (current_op == OP_RETURN || current_op == OP_SPAWN_ASYNC || current_op == OP_CHAN_PUT) {
            future_escape_detected = true;
        }

        out_optimized[j].escapes = future_escape_detected;

        // Step 3: Optimized Register Assignment based on Escape
        if (current_op == OP_ICONST) {
            if (out_optimized[j].escapes) {
                // If the value will escape, it MUST go into the sovereign return register
                out_optimized[j].assigned_reg = REG_ACCUMULATOR;
            } else {
                // OPTIMIZATION: Retains the value in a pure high-speed temporary register
                out_optimized[j].assigned_reg = REG_TEMP0;
            }
        }
    }

    *out_count = count;
}
