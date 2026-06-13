#include "unity.h"
#include "vm_debug.h"
#include "vm_core.h"
#include <stdlib.h>

void test_vm_debugger_breakpoint_interception_and_dap(void) {
    auto dbg = teko_debugger_create();
    TEST_ASSERT_NOT_NULL(dbg);

    // 1. Feeds the linear symbol map: source line 10 = VM IP 5
    teko_debugger_add_symbol(dbg, 10, 5);

    // 2. The IDE requests a breakpoint at line 10
    teko_debugger_set_breakpoint(dbg, 10);
    TEST_ASSERT_EQUAL_UINT32(1, dbg->breakpoint_count);
    TEST_ASSERT_EQUAL_UINT32(5, dbg->breakpoints[0]);

    // 3. Simulates VM IP advancement. At IP 0 it should not pause
    bool p1 = teko_debugger_should_pause(dbg, 0);
    TEST_ASSERT_FALSE(p1);

    // At IP 5, should hit the breakpoint and trigger the freeze!
    bool p2 = teko_debugger_should_pause(dbg, 5);
    TEST_ASSERT_TRUE(p2);
    TEST_ASSERT_TRUE(dbg->is_paused);

    // 4. Simulates sending a "continue" JSON-RPC message from the IDE via DAP
    unsigned char mock_code[] = {0x00};
    auto vm = teko_vm_create(mock_code, sizeof(mock_code), NULL, 0);

    const char* dap_json = "{\"seq\": 2, \"type\": \"request\", \"command\": \"continue\"}";
    teko_debugger_handle_dap_message(dbg, dap_json, vm);

    // The Debugger should release the loop and clear step mode
    TEST_ASSERT_FALSE(dbg->is_paused);
    TEST_ASSERT_FALSE(dbg->is_stepping);

    // Cleanups
    teko_vm_destroy(vm);
    teko_debugger_destroy(dbg);
}