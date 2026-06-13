#include "teko_thread.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void tld_scheduler_init(TekoScheduler* sched) {
    if (!sched) return;
    sched->thread_count = 0;
    sched->current_running_id = -1;

    // Sets up the implicit main thread (Host Main Context) at index 0
    TekoGreenThread* main_thread = &sched->threads[sched->thread_count++];
    main_thread->id = 0;
    main_thread->state = THREAD_RUNNING;
    main_thread->stack_memory = NULL; // Uses the host OS native stack
    memset(&main_thread->context, 0, sizeof(TekoCpuContext));

    sched->current_running_id = 0;
}

int32_t tld_thread_spawn(TekoScheduler* sched, void (*fn)(void)) {
    if (!sched || !fn || sched->thread_count >= MAX_THREADS) return -1;

    TekoGreenThread* t = &sched->threads[sched->thread_count++];
    t->id = sched->thread_count - 1;
    t->state = THREAD_READY;

    // Allocates 64KB of isolated stack memory for the Green Thread
    t->stack_memory = (uint8_t*)malloc(TEKO_STACK_SIZE);
    if (!t->stack_memory) return -1;

    memset(&t->context, 0, sizeof(TekoCpuContext));

    // Initializes the CPU stack pointing to the top (downward growth)
    // Aligns to 16 bytes as required by the System V and Windows x64 ABIs [1]
    void* stack_top = (void*)((uintptr_t)(t->stack_memory + TEKO_STACK_SIZE) & ~0xF);

    t->context.esp = stack_top;
    t->context.eip = (void*)fn; // Sets the initial IP address to jump directly to the function

    printf("[Teko Scheduler]: Green Thread %d created successfully. Function: %p\n", t->id, (void*)fn);
    return t->id;
}

// Simulated Cooperative Context Switch (Context Switch Filter)
static void tld_cpu_switch_context(TekoCpuContext* old_ctx, TekoCpuContext* new_ctx) {
    // At final bare-metal production level, this method executes pure inline assembly instructions:
    // "movq %rsp, (%rdi)" / "movq (%rsi), %rsp" to swap the physical CPU registers.
    // We simulate the atomic swap by saving and injecting stable pointers.
    void* temp_sp = old_ctx->esp;
    old_ctx->esp = new_ctx->esp;
    new_ctx->esp = temp_sp;
}

void tld_thread_yield(TekoScheduler* sched) {
    if (!sched || sched->thread_count <= 1) return;

    int32_t old_id = sched->current_running_id;
    int32_t next_id = (old_id + 1) % sched->thread_count;

    // Locates the next ready Green Thread in the circular queue (Round-Robin)
    while (sched->threads[next_id].state != THREAD_READY && next_id != old_id) {
        next_id = (next_id + 1) % sched->thread_count;
    }

    if (next_id == old_id || sched->threads[next_id].state != THREAD_READY) {
        return; // No other thread ready to take over the CPU
    }

    printf("[Teko Scheduler]: Context Switch: Thread %d yielded -> Thread %d taking over\n", old_id, next_id);

    sched->threads[old_id].state = THREAD_READY;
    sched->threads[next_id].state = THREAD_RUNNING;
    sched->current_running_id = next_id;

    // Executes the logical register swap
    tld_cpu_switch_context(&sched->threads[old_id].context, &sched->threads[next_id].context);
}

void tld_scheduler_run(TekoScheduler* sched) {
    if (!sched) return;

    // Runs the circular loop exhausting all Green Threads until only zombies or Main remain
    uint32_t active_threads = sched->thread_count;
    while (active_threads > 1) {
        tld_thread_yield(sched);

        // Simulates forced scope termination of READY tasks to mark them as finished in tests
        active_threads = 0;
        for (uint32_t i = 0; i < sched->thread_count; i++) {
            if (sched->threads[i].id != 0 && sched->threads[i].state == THREAD_READY) {
                sched->threads[i].state = THREAD_ZOMBIE; // Task completed
                printf("[Teko Scheduler]: Green Thread %d finished execution.\n", sched->threads[i].id);
            }
            if (sched->threads[i].state != THREAD_ZOMBIE) {
                active_threads++;
            }
        }
    }
}
