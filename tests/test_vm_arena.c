#include "unity.h"
#include "vm_arena.h"
#include <stdint.h>

// Validates the integrity of the linear allocator and constant-time memory recycling
void test_vm_arena_linear_allocation_and_reset(void) {
    TekoArena* arena = teko_arena_create();
    TEST_ASSERT_NOT_NULL(arena);

    // 1. Executes multiple consecutive small allocations
    int* ptr1 = (int*)teko_arena_alloc(arena, sizeof(int));
    int* ptr2 = (int*)teko_arena_alloc(arena, sizeof(int));

    TEST_ASSERT_NOT_NULL(ptr1);
    TEST_ASSERT_NOT_NULL(ptr2);

    // 2. Validates pointer alignment to 8 bytes (required for ARM64)
    uintptr_t addr1 = (uintptr_t)ptr1;
    uintptr_t addr2 = (uintptr_t)ptr2;
    TEST_ASSERT_EQUAL_INT(0, addr1 % 8);
    TEST_ASSERT_EQUAL_INT(0, addr2 % 8);
    TEST_ASSERT_TRUE(addr2 > addr1); // Should be sequential in physical memory

    // 3. Tests controlled page overflow by forcing allocation of a large block
    void* big_ptr = teko_arena_alloc(arena, 5000);
    TEST_ASSERT_NOT_NULL(big_ptr);
    TEST_ASSERT_NOT_NULL(arena->head->next); // Ensures that the second page was created in cascade

    // 4. Executes the O(1) reset and validates that memory was made available again
    teko_arena_reset(arena);
    TEST_ASSERT_EQUAL_INT(0, arena->head->offset);

    teko_arena_destroy(arena);
}