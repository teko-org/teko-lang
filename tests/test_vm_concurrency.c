#include "unity.h"
#include "vm_concurrency.h"
#include "vm_scheduler.h"
#include "vm_core.h"
#include <stdlib.h>

void test_vm_channel_blocking_and_unblocking(void) {
    auto sched = vm_scheduler_create();
    auto chan = teko_channel_create(1); // Channel with capacity 1

    unsigned char mock_code[] = {0x00};
    auto vm = teko_vm_create(mock_code, sizeof(mock_code), NULL, 0);

    // Creates the producer and consumer Green Threads
    auto writer = vm_scheduler_spawn(sched, 0);
    auto reader = vm_scheduler_spawn(sched, 0);

    // Activates the writer in the VM
    vm_scheduler_run_next(sched, vm);

    // 1. Successful write!
    bool p1 = teko_channel_put(chan, 42, sched, vm);
    TEST_ASSERT_TRUE(p1);
    TEST_ASSERT_EQUAL_UINT32(1, chan->count);

    // 2. Second consecutive write should fail and BLOCK the writer thread (channel full)
    bool p2 = teko_channel_put(chan, 99, sched, vm);
    TEST_ASSERT_FALSE(p2);
    TEST_ASSERT_EQUAL_INT(THREAD_BLOCKED, writer->state);

    // Activates the reader in the VM to drain the bus
    sched->current_thread = reader;
    reader->state = THREAD_RUNNING;

    int32_t val = 0;
    bool g1 = teko_channel_get(chan, &val, sched, vm);
    TEST_ASSERT_TRUE(g1);
    TEST_ASSERT_EQUAL_INT32(42, val);

    // The read should have woken the writer Green Thread automatically!
    TEST_ASSERT_EQUAL_INT(THREAD_READY, writer->state);

    // Clean heap memory deallocations
    free(chan->buffer);
    free(chan);
    teko_vm_destroy(vm);
    vm_scheduler_destroy(sched);
}