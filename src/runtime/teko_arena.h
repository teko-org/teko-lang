#ifndef TEKO_ARENA_H
#define TEKO_ARENA_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define TEKO_ARENA_PAGE_SIZE (128 * 1024) // 128KB per local Sub-Arena page

// Control structure of an isolated regional Sub-Arena
typedef struct {
    uint8_t* memory_raw; // Raw pointer returned by syscalls (mmap/VirtualAlloc)
    size_t   capacity;   // Total size of the region (128KB)
    size_t   offset;     // Active O(1) linear allocation cursor
} TekoSubArena;

// Region Manager coupled to the Green Thread lifecycle
typedef struct {
    TekoSubArena thread_arenas[128]; // Index-mapped by Thread ID
} TekoRegionManager;

// Public signatures of the native region allocator
void tld_region_init(TekoRegionManager* manager);
bool tld_region_register_thread(TekoRegionManager* manager, uint32_t thread_id);
void* tld_region_alloc(TekoRegionManager* manager, uint32_t thread_id, size_t size);
void tld_region_reset_thread(TekoRegionManager* manager, uint32_t thread_id);
void tld_region_destroy(TekoRegionManager* manager, uint32_t thread_count);

#endif // TEKO_ARENA_H
