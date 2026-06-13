#include "unity.h"
#include "codegen/tld_symbols.h"
#include <stdlib.h>
#include <string.h>

void test_tld_static_dependency_injection_and_symbol_resolution(void) {
    TekoSymbolTable table;
    tld_symbols_init(&table);

    // Simulates a raw code buffer where byte 4 represents the 4 formally empty bytes of a 'call' instruction
    uint8_t mock_code_buffer[32];
    memset(mock_code_buffer, 0x90, sizeof(mock_code_buffer)); // Initialized filled with NOPs

    // 1. DEFINE THE SYMBOLS: The injection function '@flows.notify' starts physically at byte offset 20
    bool def_ok = tld_symbols_define(&table, "@flows.notify", SYM_SERVICE, 20);
    TEST_ASSERT_TRUE(def_ok);

    // 2. REGISTER THE INVOCATION: At byte 4 of main, we invoke the dependency '@flows.notify'
    tld_symbols_reference(&table, "@flows.notify", 4);

    // Runs the global resolution simulating the default Linux ELF64 base address (0x400000)
    bool resolve_ok = tld_symbols_resolve_and_inject(&table, mock_code_buffer, 0x400000);
    TEST_ASSERT_TRUE(resolve_ok);

    // 3. MATHEMATICAL VALIDATION OF THE INJECTION:
    // Distance in bytes = Local_Destination (20) - (Local_Patch (4) + 4 operand bytes) = 20 - 8 = 12 bytes!
    TEST_ASSERT_EQUAL_HEX8(12, mock_code_buffer[4]);
    TEST_ASSERT_EQUAL_HEX8(0,  mock_code_buffer[5]);
    TEST_ASSERT_EQUAL_HEX8(0,  mock_code_buffer[6]);
    TEST_ASSERT_EQUAL_HEX8(0,  mock_code_buffer[7]);
}
