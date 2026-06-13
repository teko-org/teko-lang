#include "unity.h"
#include "../../src/runtime/teko_mem_sys.h"
#include <string.h>

void test_teko_runtime_sys_allocation_and_page_recycling(void) {
    size_t page_size = 4096; // Allocates exactly one standard 4KB page

    // 1. Performs the low-level request to the Kernel
    uint8_t* raw_memory = (uint8_t*)teko_sys_allocate_pages(page_size);
    TEST_ASSERT_NOT_NULL(raw_memory);

    // 2. Physical stress test: Writes to the beginning and end of the allocated block
    // If the Kernel has not mapped the page correctly, this will cause an immediate Segmentation Fault
    memset(raw_memory, 0xA5, page_size);
    TEST_ASSERT_EQUAL_HEX8(0xA5, raw_memory[0]);
    TEST_ASSERT_EQUAL_HEX8(0xA5, raw_memory[page_size - 1]);

    // 3. Performs the atomic return of the page to the Operating System
    bool free_ok = teko_sys_free_pages(raw_memory, page_size);
    TEST_ASSERT_TRUE(free_ok);
}
