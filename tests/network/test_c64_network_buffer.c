/* C64 Stream - deterministic concurrency regression for the jitter buffer. */
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "c64-network-buffer.h"

#include <assert.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>

/* c64-network-buffer uses the shared logging gate; the unit harness does not
 * link the plugin's logging implementation. */
bool c64_debug_logging = false;

struct test_state {
    struct c64_network_buffer *buffer;
};

static void *push_packets(void *opaque)
{
    struct test_state *state = opaque;
    uint8_t packet[C64_VIDEO_PACKET_SIZE] = {0};
    for (uint16_t i = 0; i < 5000; ++i) {
        memcpy(packet, &i, sizeof(i));
        c64_network_buffer_push_video(state->buffer, packet, sizeof(packet), (uint64_t)i * 1000);
    }
    return NULL;
}

static void *push_audio_packets(void *opaque)
{
    struct test_state *state = opaque;
    uint8_t packet[C64_AUDIO_PACKET_SIZE] = {0};
    for (uint16_t i = 0; i < 5000; ++i) {
        memcpy(packet, &i, sizeof(i));
        c64_network_buffer_push_audio(state->buffer, packet, sizeof(packet), (uint64_t)i * 1000);
    }
    return NULL;
}

static void *change_delay(void *opaque)
{
    struct test_state *state = opaque;
    for (size_t i = 0; i < 5000; ++i) {
        c64_network_buffer_set_delay(state->buffer, i % 501, (500 - i) % 501);
    }
    return NULL;
}

/* C64STR-026: the render thread pops aligned pairs while pushes and delay
 * changes run concurrently. Exercising pop here (not just push+delay) is what
 * covers the rb_pop_oldest_locked path against the delay-publication race. */
static void *pop_packets(void *opaque)
{
    struct test_state *state = opaque;
    for (size_t i = 0; i < 20000; ++i) {
        const uint8_t *vdata = NULL, *adata = NULL;
        size_t vsize = 0, asize = 0;
        uint64_t ts = 0;
        c64_network_buffer_pop(state->buffer, &vdata, &vsize, &adata, &asize, &ts);
    }
    return NULL;
}

int main(void)
{
    struct test_state state = {.buffer = c64_network_buffer_create()};
    assert(state.buffer != NULL);

    pthread_t producer;
    pthread_t audio_producer;
    pthread_t consumer;
    pthread_t updater;
    assert(pthread_create(&producer, NULL, push_packets, &state) == 0);
    assert(pthread_create(&audio_producer, NULL, push_audio_packets, &state) == 0);
    assert(pthread_create(&consumer, NULL, pop_packets, &state) == 0);
    assert(pthread_create(&updater, NULL, change_delay, &state) == 0);
    assert(pthread_join(producer, NULL) == 0);
    assert(pthread_join(audio_producer, NULL) == 0);
    assert(pthread_join(consumer, NULL) == 0);
    assert(pthread_join(updater, NULL) == 0);

    assert(c64_network_buffer_get_video_packet_count(state.buffer) <= C64_MAX_VIDEO_PACKETS - 1);
    c64_network_buffer_destroy(state.buffer);
    return 0;
}
