#include "vm_scheduler.h"
#include <stdlib.h>
#include <string.h>

VMScheduler* vm_scheduler_create(void) {
    auto sched = (VMScheduler*)malloc(sizeof(VMScheduler));
    if (!sched) return NULL;
    sched->queue_head = NULL;
    sched->queue_tail = NULL;
    sched->current_thread = NULL;
    sched->next_thread_id = 1;
    return sched;
}

// Creates and enqueues a new Green Thread (M)
GreenThread* vm_scheduler_spawn(VMScheduler* sched, uint32_t entry_ip) {
    if (!sched) return NULL;

    auto thread = (GreenThread*)malloc(sizeof(GreenThread));
    if (!thread) return NULL;

    thread->id = sched->next_thread_id++;
    thread->state = THREAD_READY;
    thread->ip = entry_ip;
    memset(thread->registers, 0, sizeof(thread->registers));
    thread->thread_arena = teko_arena_create(); // Each thread gets its own sub-arena
    thread->next = NULL;

    // Places in the ready queue (FIFO)
    if (!sched->queue_head) {
        sched->queue_head = thread;
        sched->queue_tail = thread;
    } else {
        sched->queue_tail->next = thread;
        sched->queue_tail = thread;
    }

    return thread;
}

// Performs the voluntary Context Switch saving the VM registers
void vm_scheduler_yield(VMScheduler* sched, TekoVM* main_vm) {
    if (!sched || !main_vm || !sched->current_thread) return;

    // 1. Saves the current physical VM context inside the active Green Thread
    auto current = sched->current_thread;
    current->ip = main_vm->ip;
    memcpy(current->registers, main_vm->registers, sizeof(main_vm->registers));

    if (current->state == THREAD_RUNNING) {
        current->state = THREAD_READY;
    }

    // 2. Clears the current pointer to force queue rotation
    sched->current_thread = NULL;
}

// Picks the next ready Green Thread and restores its state into the physical VM
bool vm_scheduler_run_next(VMScheduler* sched, TekoVM* main_vm) {
    if (!sched || !main_vm || !sched->queue_head) return false;

    // Finds the first ready thread in the circular queue
    GreenThread* prev = NULL;
    auto curr = sched->queue_head;

    while (curr && curr->state != THREAD_READY) {
        prev = curr;
        curr = curr->next;
    }

    if (!curr) return false; // No ready thread found

    // Removes curr from its current position and sets it as active
    sched->current_thread = curr;
    curr->state = THREAD_RUNNING;

    // Restores the context into the physical VM
    main_vm->ip = curr->ip;
    memcpy(main_vm->registers, curr->registers, sizeof(main_vm->registers));

    return true;
}

void vm_scheduler_destroy(VMScheduler* sched) {
    if (!sched) return;
    auto curr = sched->queue_head;
    while (curr) {
        auto next = curr->next;
        if (curr->thread_arena) teko_arena_destroy(curr->thread_arena);
        free(curr);
        curr = next;
    }
    free(sched);
}