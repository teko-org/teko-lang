#include "unity.h"
#include "../../src/runtime/teko_arena.h"

void test_teko_runtime_arena_thread_isolation_and_alignment(void) {
    TekoRegionManager region_manager;
    tld_region_init(&region_manager);

    // 1. Registers and allocates independent Sub-Arenas for two simulated Threads (ID 1 and ID 2)
    bool reg1 = tld_region_register_thread(&region_manager, 1);
    bool reg2 = tld_region_register_thread(&region_manager, 2);

    TEST_ASSERT_TRUE(reg1);
    TEST_ASSERT_TRUE(reg2);

    // 2. Performs local concurrent allocations and checks the physical isolation of pointers
    uint32_t* ptr_t1 = (uint32_t*)tld_region_alloc(&region_manager, 1, sizeof(uint32_t));
    uint64_t* ptr_t2 = (uint64_t*)tld_region_alloc(&region_manager, 2, sizeof(uint64_t));

    TEST_ASSERT_NOT_NULL(ptr_t1);
    TEST_ASSERT_NOT_NULL(ptr_t2);

    // The pointers must reside in completely distinct virtual memory pages requested from the kernel
    TEST_ASSERT_NOT_EQUAL((void*)ptr_t1, (void*)ptr_t2);

    // Writes to the blocks to verify that the addresses are valid
    *ptr_t1 = 1024;
    *ptr_t2 = 88888888;
    TEST_ASSERT_EQUAL_UINT32(1024, *ptr_t1);
    TEST_ASSERT_EQUAL_UINT64(88888888, *ptr_t2);

    // 3. Tests the native 8-byte alignment of the O(1) slicer
    uint8_t* byte_ptr = (uint8_t*)tld_region_alloc(&region_manager, 1, 1); // Requests only 1 byte
    uint32_t* next_ptr = (uint32_t*)tld_region_alloc(&region_manager, 1, sizeof(uint32_t));

    // The distance between the two must be exactly 8 bytes due to the CPU alignment mask
    ptrdiff_t distance = (uint8_t*)next_ptr - byte_ptr;
    TEST_ASSERT_EQUAL_INT(8, (int)distance);

    // 4. Performs instant local recycling and clears the pages
    tld_region_reset_thread(&region_manager, 1);
    tld_region_reset_thread(&region_manager, 2);
    tld_region_destroy(&region_manager, 3);
}
