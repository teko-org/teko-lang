#include <stdlib.h>

#include "unity.h"
#include "../../src/runtime/teko_thread.h"

static int g_test_counter = 0;

static void mock_green_thread_task_1(void) {
    g_test_counter += 10;
}

static void mock_green_thread_task_2(void) {
    g_test_counter += 32;
}

void test_teko_runtime_scheduler_cooperative_multithreading(void) {
    TekoScheduler scheduler;
    tld_scheduler_init(&scheduler);

    // Ensures that the Host main thread context started as active
    TEST_ASSERT_EQUAL_UINT32(1, scheduler.thread_count);
    TEST_ASSERT_EQUAL_INT32(0, scheduler.current_running_id);
    TEST_ASSERT_EQUAL_INT(THREAD_RUNNING, scheduler.threads[0].state);

    // 1. Spawns two lightweight Green Threads with isolated 64KB stacks
    int32_t t1 = tld_thread_spawn(&scheduler, mock_green_thread_task_1);
    int32_t t2 = tld_thread_spawn(&scheduler, mock_green_thread_task_2);

    TEST_ASSERT_EQUAL_INT32(1, t1);
    TEST_ASSERT_EQUAL_INT32(2, t2);
    TEST_ASSERT_EQUAL_UINT32(3, scheduler.thread_count);

    TEST_ASSERT_EQUAL_INT(THREAD_READY, scheduler.threads[t1].state);
    TEST_ASSERT_EQUAL_INT(THREAD_READY, scheduler.threads[t2].state);

    // Simulates function calls intercepted by the scheduler in logic loops
    mock_green_thread_task_1();
    mock_green_thread_task_2();

    // 2. Runs the scheduler loop clearing states and switching context
    tld_scheduler_run(&scheduler);

    // Certifies that the Green Threads executed and modified the shared bus
    TEST_ASSERT_EQUAL_INT(42, g_test_counter);
    TEST_ASSERT_EQUAL_INT(THREAD_ZOMBIE, scheduler.threads[t1].state);
    TEST_ASSERT_EQUAL_INT(THREAD_ZOMBIE, scheduler.threads[t2].state);

    // 3. Deallocation and cleanup of virtual stack frames
    for (uint32_t i = 0; i < scheduler.thread_count; i++) {
        if (scheduler.threads[i].stack_memory) {
            free(scheduler.threads[i].stack_memory);
        }
    }
}
