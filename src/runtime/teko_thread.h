#ifndef TEKO_THREAD_H
#define TEKO_THREAD_H

#include <stdint.h>
#include <stdbool.h>

#define TEKO_STACK_SIZE (64 * 1024) // 64KB of isolated stack per Green Thread
#define MAX_THREADS     128

typedef enum {
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_ZOMBIE
} TekoThreadState;

// Physical structure for CPU context preservation (abstraction-agnostic)
typedef struct {
    void*    esp;      // Stack Pointer
    void*    ebp;      // Frame Pointer
    void*    eip;      // Instruction Pointer (Program Counter)
    uint64_t regs[8];  // Callee-saved general registers (r12-r15, rbx, etc.)
} TekoCpuContext;

// Thread Control Block (TCB) structure
typedef struct {
    uint32_t        id;
    TekoThreadState state;
    uint8_t*        stack_memory;  // Pointer to the allocated stack
    TekoCpuContext  context;       // Saved CPU context
} TekoGreenThread;

// The Central Cooperative M:N Scheduler
typedef struct {
    TekoGreenThread threads[MAX_THREADS];
    uint32_t        thread_count;
    int32_t         current_running_id; // ID of the thread currently active on the CPU
} TekoScheduler;

// Public signatures of the native concurrent bus
void tld_scheduler_init(TekoScheduler* sched);
int32_t tld_thread_spawn(TekoScheduler* sched, void (*fn)(void));
void tld_thread_yield(TekoScheduler* sched);
void tld_scheduler_run(TekoScheduler* sched);

#endif // TEKO_THREAD_H
