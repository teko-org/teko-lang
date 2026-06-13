#include "unity.h"
#include "vm_core.h"
#include "codegen_li.h"
#include <stdlib.h>

// Tests the execution of a simple linear mathematical opcode sequence injected into the interpreter
void test_vm_core_mathematical_execution_loop(void) {
    // We assemble a simplified manual LI bytecode vector:
    // OP_ICONST (0x01) -> value 42 -> OP_STORE (0x04) -> register 5 -> OP_HALT (0x00)
    unsigned char mock_bytecode[] = {
        0x01, 0x2A, 0x00, 0x00, 0x00, // OP_ICONST 42
        0x04, 0x05, 0x00, 0x00, 0x00, // OP_STORE r5
        0x03, 0x05, 0x00, 0x00, 0x00, // OP_LOAD r5 (move to r0)
        0x00                          // OP_HALT
    };

    // Initializes the core interpreter with the injected stream
    TekoVM* vm = teko_vm_create(mock_bytecode, sizeof(mock_bytecode), NULL, 0);
    TEST_ASSERT_NOT_NULL(vm);

    // Executes and validates the exit code contained in register r0
    int32_t exit_code = teko_vm_execute(vm);
    TEST_ASSERT_EQUAL_INT32(42, exit_code);
    TEST_ASSERT_EQUAL_INT32(42, vm->registers[5]);

    teko_vm_destroy(vm);
}