#include "vm_concurrency.h"
#include <stdlib.h>
#include <string.h>

// Helper to enqueue a Green Thread into a blocked list
static void block_thread(GreenThread** list_head, GreenThread* thread) {
    if (!thread) return;
    thread->state = THREAD_BLOCKED;
    thread->next = NULL;

    if (!*list_head) {
        *list_head = thread;
    } else {
        auto curr = *list_head;
        while (curr->next) curr = curr->next;
        curr->next = thread;
    }
}

// Helper to unblock the first Green Thread from a list and return it to the Scheduler
static void unblock_thread(GreenThread** list_head, VMScheduler* sched) {
    if (!*list_head || !sched) return;

    auto thread = *list_head;
    *list_head = thread->next;

    thread->state = THREAD_READY;
    thread->next = NULL;

    // Re-enqueues the released coroutine at the tail of the global execution queue
    if (!sched->queue_head) {
        sched->queue_head = thread;
        sched->queue_tail = thread;
    } else {
        sched->queue_tail->next = thread;
        sched->queue_tail = thread;
    }
}

TekoVMChannel* teko_channel_create(uint32_t capacity) {
    auto chan = (TekoVMChannel*)malloc(sizeof(TekoVMChannel));
    if (!chan) return NULL;

    chan->capacity = capacity == 0 ? 1 : capacity; // Guarantees a minimum size of 1 for basic bounded channels
    chan->buffer = (int32_t*)calloc(chan->capacity, sizeof(int32_t));
    chan->head = 0;
    chan->tail = 0;
    chan->count = 0;
    chan->blocked_readers = NULL;
    chan->blocked_writers = NULL;
    return chan;
}

// Asynchronous Write operation on the channel (chan.put)
bool teko_channel_put(TekoVMChannel* chan, int32_t value, VMScheduler* sched, TekoVM* vm) {
    if (!chan || !sched || !vm) return false;

    // If the channel is full, immediately block the current Green Thread
    if (chan->count >= chan->capacity) {
        auto active = sched->current_thread;
        if (active) {
            block_thread(&chan->blocked_writers, active);
            vm_scheduler_yield(sched, vm); // Suspends execution of the current VM
        }
        return false;
    }

    chan->buffer[chan->tail] = value;
    chan->tail = (chan->tail + 1) % chan->capacity;
    chan->count++;

    // If there was a Green Thread waiting to read data, release it!
    if (chan->blocked_readers) {
        unblock_thread(&chan->blocked_readers, sched);
    }
    return true;
}

// Channel Read operation
bool teko_channel_get(TekoVMChannel* chan, int32_t* out_value, VMScheduler* sched, TekoVM* vm) {
    if (!chan || !out_value || !sched || !vm) return false;

    // If the channel is completely empty, put the Green Thread on hold
    if (chan->count == 0) {
        auto active = sched->current_thread;
        if (active) {
            block_thread(&chan->blocked_readers, active);
            vm_scheduler_yield(sched, vm);
        }
        return false;
    }

    *out_value = chan->buffer[chan->head];
    chan->head = (chan->head + 1) % chan->capacity;
    chan->count--;

    // If there was a Green Thread blocked waiting for space to write, wake it up!
    if (chan->blocked_writers) {
        unblock_thread(&chan->blocked_writers, sched);
    }
    return true;
}

// mtx.lock() operation
void teko_mutex_lock(TekoVMMutex* mutex, VMScheduler* sched, TekoVM* vm) {
    if (!mutex || !sched || !vm) return;

    auto active = sched->current_thread;
    if (mutex->is_locked) {
        if (active && mutex->owner != active) {
            block_thread(&mutex->blocked_threads, active);
            vm_scheduler_yield(sched, vm);
        }
    } else {
        mutex->is_locked = true;
        mutex->owner = active;
    }
}

// mtx.unlock() operation
void teko_mutex_unlock(TekoVMMutex* mutex, VMScheduler* sched, TekoVM* vm) {
    if (!mutex || !mutex->is_locked || !sched) return;

    mutex->is_locked = false;
    mutex->owner = NULL;

    if (mutex->blocked_threads) {
        unblock_thread(&mutex->blocked_threads, sched);
    }
}

// wg.add(X) operation
void teko_waiter_add(TekoVMWaiter* waiter, int32_t delta) {
    if (waiter) waiter->counter += delta;
}

// wg.done() operation
void teko_waiter_done(TekoVMWaiter* waiter) {
    if (waiter) waiter->counter--;
}

// wg.wait() operation
void teko_waiter_wait(TekoVMWaiter* waiter, VMScheduler* sched, TekoVM* vm) {
    if (!waiter || !sched || !vm) return;

    // If the counter is greater than zero, the current Green Thread stays locked until the others call .done()
    if (waiter->counter > 0) {
        auto active = sched->current_thread;
        if (active) {
            block_thread(&waiter->blocked_waiters, active);
            vm_scheduler_yield(sched, vm);
        }
    }
}