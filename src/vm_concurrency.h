#ifndef VM_CONCURRENCY_H
#define VM_CONCURRENCY_H

#include <stdint.h>
#include <stdbool.h>
#include "vm_scheduler.h"

// Physical structure of a Channel (supports Bounded and circular Unbounded)
typedef struct {
    int32_t* buffer;
    uint32_t capacity;
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    GreenThread* blocked_readers; // Queue of Green Threads waiting for data
    GreenThread* blocked_writers; // Queue of Green Threads waiting for space
} TekoVMChannel;

// Physical structure of a Mutex (Mutual Exclusion)
typedef struct {
    bool is_locked;
    GreenThread* owner;
    GreenThread* blocked_threads; // Wait queue for the lock
} TekoVMMutex;

// Physical structure of a Waiter (Wait Group)
typedef struct {
    int32_t counter;
    GreenThread* blocked_waiters; // Green Threads stopped at .wait()
} TekoVMWaiter;

// Public signatures of the Concurrency Runtime
TekoVMChannel* teko_channel_create(uint32_t capacity);
bool teko_channel_put(TekoVMChannel* chan, int32_t value, VMScheduler* sched, TekoVM* vm);
bool teko_channel_get(TekoVMChannel* chan, int32_t* out_value, VMScheduler* sched, TekoVM* vm);

void teko_mutex_lock(TekoVMMutex* mutex, VMScheduler* sched, TekoVM* vm);
void teko_mutex_unlock(TekoVMMutex* mutex, VMScheduler* sched, TekoVM* vm);

void teko_waiter_add(TekoVMWaiter* waiter, int32_t delta);
void teko_waiter_done(TekoVMWaiter* waiter);
void teko_waiter_wait(TekoVMWaiter* waiter, VMScheduler* sched, TekoVM* vm);

#endif // VM_CONCURRENCY_H