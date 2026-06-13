#ifndef VM_SCHEDULER_H
#define VM_SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>
#include "vm_core.h"

// Possible states of a Green Thread in the VM runtime
typedef enum {
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_DEAD
} ThreadState;

// Structure representing an isolated Green Thread (M)
typedef struct GreenThread {
    uint32_t id;
    ThreadState state;
    uint32_t ip;                            // Own Instruction Pointer
    int32_t registers[VM_REGISTERS_COUNT];  // Saved virtual registers
    TekoArena* thread_arena;                // Coroutine-local frame Arena
    struct GreenThread* next;               // Linked list for the queue
} GreenThread;

// Structure of the M:N Scheduler
typedef struct {
    GreenThread* queue_head;
    GreenThread* queue_tail;
    GreenThread* current_thread;
    uint32_t next_thread_id;
} VMScheduler;

// Public signatures of the Scheduler
VMScheduler* vm_scheduler_create(void);
GreenThread* vm_scheduler_spawn(VMScheduler* sched, uint32_t entry_ip);
void vm_scheduler_yield(VMScheduler* sched, TekoVM* main_vm);
bool vm_scheduler_run_next(VMScheduler* sched, TekoVM* main_vm);
void vm_scheduler_destroy(VMScheduler* sched);

#endif // VM_SCHEDULER_H