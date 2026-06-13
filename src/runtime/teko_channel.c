#include "teko_channel.h"
#include <stdio.h>

void tld_channel_init(TekoChannel* chan, uint32_t capacity) {
    if (!chan) return;
    chan->capacity = capacity > MAX_CHANNEL_BUFFER ? MAX_CHANNEL_BUFFER : capacity;
    chan->size = 0;
    chan->head = 0;
    chan->tail = 0;
    chan->blocked_senders_count = 0;
    chan->blocked_receivers_count = 0;
}

bool tld_channel_send(TekoChannel* chan, TekoScheduler* sched, int32_t value) {
    if (!chan || !sched) return false;

    int32_t current_thread_id = sched->current_running_id;

    // CHANNEL FULL: Blocks the sender cooperatively
    if (chan->size >= chan->capacity) {
        if (current_thread_id != -1 && chan->blocked_senders_count < MAX_THREADS) {
            printf("[Teko Channels]: CHANNEL FULL! Blocking Green Thread %d on write.\n", current_thread_id);
            chan->blocked_senders[chan->blocked_senders_count++] = current_thread_id;
            sched->threads[current_thread_id].state = THREAD_BLOCKED;

            // Forces the CPU to switch to another ready thread
            tld_thread_yield(sched);
        }
        return false;
    }

    // Injects the data into the circular buffer
    chan->buffer[chan->tail] = value;
    chan->tail = (chan->tail + 1) % MAX_CHANNEL_BUFFER;
    chan->size++;

    // HANDSHAKE FLOW: If there was a blocked receiver waiting for data, put it back in READY
    if (chan->blocked_receivers_count > 0) {
        int32_t waiting_thread_id = chan->blocked_receivers[0];
        printf("[Teko Channels]: Data injected. Waking up Green Thread %d that was blocked on read.\n", waiting_thread_id);

        // Removes from the top of the blocked queue by sliding the array
        for (uint32_t i = 1; i < chan->blocked_receivers_count; i++) {
            chan->blocked_receivers[i - 1] = chan->blocked_receivers[i];
        }
        chan->blocked_receivers_count--;

        // Thread returns to the scheduler's active conveyor
        sched->threads[waiting_thread_id].state = THREAD_READY;
    }

    return true;
}

bool tld_channel_receive(TekoChannel* chan, TekoScheduler* sched, int32_t* out_value) {
    if (!chan || !sched || !out_value) return false;

    int32_t current_thread_id = sched->current_running_id;

    // CHANNEL EMPTY: Blocks the receiver cooperatively
    if (chan->size == 0) {
        if (current_thread_id != -1 && chan->blocked_receivers_count < MAX_THREADS) {
            printf("[Teko Channels]: CHANNEL EMPTY! Blocking Green Thread %d on read.\n", current_thread_id);
            chan->blocked_receivers[chan->blocked_receivers_count++] = current_thread_id;
            sched->threads[current_thread_id].state = THREAD_BLOCKED;

            tld_thread_yield(sched);
        }
        return false;
    }

    // Extracts the data from the circular buffer
    *out_value = chan->buffer[chan->head];
    chan->head = (chan->head + 1) % MAX_CHANNEL_BUFFER;
    chan->size--;

    // HANDSHAKE FLOW: If there was a blocked sender waiting for buffer space, wake it up
    if (chan->blocked_senders_count > 0) {
        int32_t waiting_thread_id = chan->blocked_senders[0];
        printf("[Teko Channels]: Space freed. Waking up Green Thread %d that was blocked on write.\n", waiting_thread_id);

        for (uint32_t i = 1; i < chan->blocked_senders_count; i++) {
            chan->blocked_senders[i - 1] = chan->blocked_senders[i];
        }
        chan->blocked_senders_count--;

        sched->threads[waiting_thread_id].state = THREAD_READY;
    }

    return true;
}
