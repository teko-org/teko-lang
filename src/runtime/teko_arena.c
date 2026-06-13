#include "teko_arena.h"
#include "teko_mem_sys.h"
#include <stdio.h>
#include <string.h>

void tld_region_init(TekoRegionManager* manager) {
    if (!manager) return;
    memset(manager, 0, sizeof(TekoRegionManager));
}

bool tld_region_register_thread(TekoRegionManager* manager, uint32_t thread_id) {
    if (!manager || thread_id >= 128) return false;

    TekoSubArena* arena = &manager->thread_arenas[thread_id];

    // Invokes the raw system call (mmap / VirtualAlloc) to request 128KB of clean memory
    arena->memory_raw = (uint8_t*)teko_sys_allocate_pages(TEKO_ARENA_PAGE_SIZE);
    if (!arena->memory_raw) {
        return false;
    }

    arena->capacity = TEKO_ARENA_PAGE_SIZE;
    arena->offset = 0;

    printf("[Teko Arenas]: O(1) Sub-Arena registered and isolated for Green Thread %d (128KB Virtual RAM).\n", thread_id);
    return true;
}

void* tld_region_alloc(TekoRegionManager* manager, uint32_t thread_id, size_t size) {
    if (!manager || thread_id >= 128 || size == 0) return NULL;

    TekoSubArena* arena = &manager->thread_arenas[thread_id];
    if (!arena->memory_raw) return NULL;

    // Native 8-byte memory alignment to maximize the CPU data read bus
    size_t aligned_size = (size + 7) & ~7;

    // Overflow protection: if the local Sub-Arena overflows, the runtime denies the allocation
    if (arena->offset + aligned_size > arena->capacity) {
        fprintf(stderr, "[Teko Runtime Panic]: Out of Memory in Thread %d Sub-Arena!\n", thread_id);
        return NULL;
    }

    // Atomic O(1) allocation: just advances the cursor and returns the starting address
    void* alloc_ptr = &arena->memory_raw[arena->offset];
    arena->offset += aligned_size;

    return alloc_ptr;
}

void tld_region_reset_thread(TekoRegionManager* manager, uint32_t thread_id) {
    if (!manager || thread_id >= 128) return;

    TekoSubArena* arena = &manager->thread_arenas[thread_id];

    // CONSTANT TIME O(1) CLEANUP: Instantly zeroes the linear cursor.
    // All memory is marked for reuse without individual free loops!
    arena->offset = 0;
    printf("[Teko Arenas]: Lifecycle ended. Thread %d Sub-Arena recycled in 1 clock cycle.\n", thread_id);
}

void tld_region_destroy(TekoRegionManager* manager, uint32_t thread_count) {
    if (!manager) return;

    for (uint32_t i = 0; i < thread_count; i++) {
        TekoSubArena* arena = &manager->thread_arenas[i];
        if (arena->memory_raw) {
            // Returns the raw pages back to the Kernel cleanly
            teko_sys_free_pages(arena->memory_raw, arena->capacity);
            arena->memory_raw = NULL;
        }
    }
}
