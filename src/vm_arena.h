#ifndef VM_ARENA_H
#define VM_ARENA_H

#include <stdint.h>
#include <stddef.h>

#define TEKO_ARENA_PAGE_SIZE 4096 // Base size of 4KB per Arena page

// Structure of a contiguous Arena page/block
typedef struct TekoArenaPage {
    unsigned char* memory;
    size_t capacity;
    size_t offset;
    struct TekoArenaPage* next; // Linked list for overflow cases
} TekoArenaPage;

// Structure of the Arena manager (serves as the language-level arena context)
typedef struct {
    TekoArenaPage* head;
    TekoArenaPage* current;
} TekoArena;

// Public signatures of the Region Allocator
TekoArena* teko_arena_create(void);
void* teko_arena_alloc(TekoArena* arena, size_t size);
void teko_arena_reset(TekoArena* arena);
void teko_arena_destroy(TekoArena* arena);

#endif // VM_ARENA_H