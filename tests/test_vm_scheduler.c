#include "unity.h"
#include "vm_scheduler.h"
#include "vm_core.h"
#include <stdlib.h>

void test_vm_scheduler_context_switching_and_spawn(void) {
    auto sched = vm_scheduler_create();
    TEST_ASSERT_NOT_NULL(sched);

    // Dummy bytecode to simulate two IP routes
    unsigned char mock_code[] = {0x00, 0x00, 0x00, 0x00};
    auto vm = teko_vm_create(mock_code, sizeof(mock_code), NULL, 0);
    TEST_ASSERT_NOT_NULL(vm);

    // Spawns two Green Threads at different IPs
    auto g1 = vm_scheduler_spawn(sched, 10);
    auto g2 = vm_scheduler_spawn(sched, 20);
    TEST_ASSERT_EQUAL_UINT32(1, g1->id);
    TEST_ASSERT_EQUAL_UINT32(2, g2->id);

    // Runs the first thread
    bool run1 = vm_scheduler_run_next(sched, vm);
    TEST_ASSERT_TRUE(run1);
    TEST_ASSERT_EQUAL_UINT32(10, vm->ip);

    // Modifies a VM register to test state persistence
    vm->registers[3] = 999;

    // Executes the yield (voluntary context switch)
    vm_scheduler_yield(sched, vm);
    TEST_ASSERT_EQUAL_INT32(999, g1->registers[3]); // The state should have been saved!

    // Teardowns
    teko_vm_destroy(vm);
    vm_scheduler_destroy(sched);
}