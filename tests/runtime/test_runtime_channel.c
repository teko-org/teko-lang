#include "unity.h"
#include "../../src/runtime/teko_channel.h"

void test_teko_runtime_channels_blocking_and_signaling(void) {
    TekoScheduler scheduler;
    tld_scheduler_init(&scheduler);

    TekoChannel chan;
    tld_channel_init(&chan, 2); // Initializes channel with capacity for 2 elements

    TEST_ASSERT_EQUAL_UINT32(2, chan.capacity);
    TEST_ASSERT_EQUAL_UINT32(0, chan.size);

    // 1. Normal write within capacity
    bool send1 = tld_channel_send(&chan, &scheduler, 100);
    bool send2 = tld_channel_send(&chan, &scheduler, 200);
    TEST_ASSERT_TRUE(send1);
    TEST_ASSERT_TRUE(send2);
    TEST_ASSERT_EQUAL_UINT32(2, chan.size);

    // 2. Barrier test: Tries to write to a full channel (should fail and simulate logical blocking)
    bool send3 = tld_channel_send(&chan, &scheduler, 300);
    TEST_ASSERT_FALSE(send3); // Does not add, triggers protection

    // 3. Contiguous read of data clearing the channel
    int32_t val1 = 0;
    int32_t val2 = 0;
    bool recv1 = tld_channel_receive(&chan, &scheduler, &val1);
    bool recv2 = tld_channel_receive(&chan, &scheduler, &val2);

    TEST_ASSERT_TRUE(recv1);
    TEST_ASSERT_TRUE(recv2);
    TEST_ASSERT_EQUAL_INT32(100, val1);
    TEST_ASSERT_EQUAL_INT32(200, val2);
    TEST_ASSERT_EQUAL_UINT32(0, chan.size);

    // 4. Barrier test: Tries to read from an empty channel (should fail and trigger blocking)
    int32_t val3 = 0;
    bool recv3 = tld_channel_receive(&chan, &scheduler, &val3);
    TEST_ASSERT_FALSE(recv3);
}
