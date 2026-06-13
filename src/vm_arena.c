#include "vm_arena.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Helper to instantiate a new contiguous memory page
static TekoArenaPage* create_arena_page(size_t capacity) {
    auto page = (TekoArenaPage*)malloc(sizeof(TekoArenaPage));
    if (!page) return NULL;

    page->capacity = capacity < TEKO_ARENA_PAGE_SIZE ? TEKO_ARENA_PAGE_SIZE : capacity;
    page->offset = 0;
    page->next = NULL;
    page->memory = (unsigned char*)malloc(page->capacity);

    if (!page->memory) {
        free(page);
        return NULL;
    }
    return page;
}

// Initializes the context Arena
TekoArena* teko_arena_create(void) {
    auto arena = (TekoArena*)malloc(sizeof(TekoArena));
    if (!arena) return NULL;

    arena->head = create_arena_page(TEKO_ARENA_PAGE_SIZE);
    if (!arena->head) {
        free(arena);
        return NULL;
    }
    arena->current = arena->head;
    return arena;
}

// Contiguous linear allocation in constant time O(1)
void* teko_arena_alloc(TekoArena* arena, size_t size) {
    if (!arena || size == 0) return NULL;

    // Memory alignment for ARM64 (aligns pointers to multiples of 8 bytes)
    size_t aligned_size = (size + 7) & ~7;

    // If the current block does not have enough space, create a new cascaded page
    if (arena->current->offset + aligned_size > arena->current->capacity) {
        size_t next_cap = aligned_size > TEKO_ARENA_PAGE_SIZE ? aligned_size : TEKO_ARENA_PAGE_SIZE;
        auto next_page = create_arena_page(next_cap);
        if (!next_page) return NULL;

        arena->current->next = next_page;
        arena->current = next_page;
    }

    // Advances the offset linearly and returns the physical address of the slice
    void* alloc_ptr = &arena->current->memory[arena->current->offset];
    arena->current->offset += aligned_size;

    return alloc_ptr;
}

// Resets the Arena in O(1), making all memory reusable without deallocating from the OS
void teko_arena_reset(TekoArena* arena) {
    if (!arena) return;

    auto page = arena->head;
    while (page != NULL) {
        page->offset = 0; // Just zeroes the offset. No free loops.
        page = page->next;
    }
    arena->current = arena->head; // Points back to the beginning
}

// Complete physical destruction of the Arena at the end of the Thread/VM lifecycle
void teko_arena_destroy(TekoArena* arena) {
    if (!arena) return;

    auto page = arena->head;
    while (page != NULL) {
        auto temp = page;
        page = page->next;
        free(temp->memory);
        free(temp);
    }
    free(arena);
}