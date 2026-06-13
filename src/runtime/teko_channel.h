#ifndef TEKO_CHANNEL_H
#define TEKO_CHANNEL_H

#include <stdint.h>
#include <stdbool.h>
#include "teko_thread.h"

#define MAX_CHANNEL_BUFFER 16

// Physical structure of a runtime-level channel
typedef struct {
    int32_t  buffer[MAX_CHANNEL_BUFFER]; // Circular buffer for data
    uint32_t capacity;                   // Maximum capacity (0 for unbuffered/synchronous channels)
    uint32_t size;                       // Current number of elements
    uint32_t head;                       // Read index
    uint32_t tail;                       // Write index

    // Control queues for Blocked Threads in the M:N design
    int32_t blocked_senders[MAX_THREADS];
    uint32_t blocked_senders_count;
    int32_t blocked_receivers[MAX_THREADS];
    uint32_t blocked_receivers_count;
} TekoChannel;

// Public signatures of the concurrent channel bus
void tld_channel_init(TekoChannel* chan, uint32_t capacity);
bool tld_channel_send(TekoChannel* chan, TekoScheduler* sched, int32_t value);
bool tld_channel_receive(TekoChannel* chan, TekoScheduler* sched, int32_t* out_value);

#endif // TEKO_CHANNEL_H
