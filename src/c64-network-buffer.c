/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#include "c64-network-buffer.h"
#include "c64-logging.h"
#include <obs-module.h>
#include <util/platform.h>
#include <util/threading.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

struct packet_slot {
    uint8_t data[C64_VIDEO_PACKET_SIZE > C64_AUDIO_PACKET_SIZE ? C64_VIDEO_PACKET_SIZE : C64_AUDIO_PACKET_SIZE];
    size_t size;
    uint64_t timestamp_us;
    uint16_t sequence_num;
    uint16_t frame_num; // For video packets - frame number (0 for audio)
    uint16_t line_num;  // For video packets - line number (0 for audio)
    bool valid;         // Indicates if this slot contains valid data
};

typedef enum { BUFFER_TYPE_VIDEO, BUFFER_TYPE_AUDIO } buffer_type_t;

struct packet_ring_buffer {
    struct packet_slot *slots;     // Points to either video or audio slot array
    size_t max_capacity;           // Either C64_MAX_VIDEO_PACKETS or C64_MAX_AUDIO_PACKETS
    size_t active_slots;           // computed from delay
    volatile long head;            // Lock-free using atomic operations
    volatile long tail;            // Lock-free using atomic operations
    size_t packet_size;            // Either C64_VIDEO_PACKET_SIZE or C64_AUDIO_PACKET_SIZE
    uint16_t next_expected_seq;    // Expected next sequence number
    volatile bool seq_initialized; // Whether we have initialized sequence tracking (atomic)
    uint64_t delay_us;             // Delay in microseconds
    buffer_type_t type;            // For logging and type-specific behavior
    pthread_mutex_t mutex;         // Guards buffer access
};

struct c64_network_buffer {
    struct packet_ring_buffer video;
    struct packet_ring_buffer audio;
    // Storage for the actual slot arrays
    struct packet_slot video_slots[C64_MAX_VIDEO_PACKETS];
    struct packet_slot audio_slots[C64_MAX_AUDIO_PACKETS];
};

// ----------------------------------
// Internal helpers
// ----------------------------------

// Debug function to verify buffer sequence ordering
static void debug_verify_buffer_ordering(struct packet_ring_buffer *rb, const char *context)
{
#ifdef DEBUG_SEQUENCE_ORDERING
    // Note: This function assumes caller already holds the mutex
    size_t head = rb->head;
    size_t tail = rb->tail;

    if (head == tail)
        return; // Empty buffer

    uint16_t prev_seq = 0;
    bool first = true;
    size_t current = tail;

    while (current != head) {
        if (rb->slots[current].valid) {
            if (!first) {
                bool ordering_violation = false;
                if (rb->type == BUFFER_TYPE_VIDEO) {
                    // Check frame-based ordering for video
                    static uint16_t prev_frame = 0;
                    static uint16_t prev_line = 0;
                    int16_t frame_diff = (int16_t)(rb->slots[current].frame_num - prev_frame);
                    if (frame_diff < 0 || (frame_diff == 0 && (int16_t)(rb->slots[current].line_num - prev_line) < 0)) {
                        ordering_violation = true;
                        C64_LOG_ERROR(
                            "%s: Video ordering violation - frame %u line %u after frame %u line %u at pos %zu",
                            context, rb->slots[current].frame_num, rb->slots[current].line_num, prev_frame, prev_line,
                            current);
                    }
                    prev_frame = rb->slots[current].frame_num;
                    prev_line = rb->slots[current].line_num;
                } else {
                    // Check sequence ordering for audio
                    uint16_t seq = rb->slots[current].sequence_num;
                    static uint16_t prev_seq_audio = 0;
                    int16_t diff = (int16_t)(seq - prev_seq_audio);
                    if (diff <= 0) {
                        ordering_violation = true;
                        C64_LOG_ERROR("%s: Audio ordering violation - seq %u after %u at pos %zu", context, seq,
                                      prev_seq_audio, current);
                    }
                    prev_seq_audio = seq;
                }
            }
            first = false;
        }
        current = (current + 1) % rb->max_capacity;
    }
#else
    // Avoid unused parameter warnings in release builds
    (void)rb;
    (void)context;
#endif
}

// Generic ring buffer operations using macros to work with both buffer types

// Generic packet ring buffer push with sequence-based ordering
static void rb_push(struct packet_ring_buffer *rb, const uint8_t *data, size_t len, uint64_t ts)
{
    // Validate packet size based on type
    size_t min_header_size = (rb->type == BUFFER_TYPE_VIDEO) ? 6 : 2; // Video needs seq+frame+line, audio needs seq
    if (!rb || !data || len < min_header_size) {
        return;
    }

    // Extract sequence number from packet header (first 2 bytes, little-endian)
    uint16_t seq_num = *(uint16_t *)(data);

    // For video packets, also extract frame and line numbers
    uint16_t frame_num = 0;
    uint16_t line_num = 0;
    if (rb->type == BUFFER_TYPE_VIDEO && len >= 6) {
        frame_num = *(uint16_t *)(data + 2);
        line_num = *(uint16_t *)(data + 4);
        line_num &= 0x7FFF; // Remove last packet bit
    }

    const char *type_name = (rb->type == BUFFER_TYPE_VIDEO) ? "Video" : "Audio";

    // Initialize sequence tracking on first packet (atomic)
    if (!os_atomic_load_bool(&rb->seq_initialized)) {
        rb->next_expected_seq = seq_num;
        os_atomic_set_bool(&rb->seq_initialized, true);
        C64_LOG_DEBUG("%s buffer: initialized with sequence %u", type_name, seq_num);
    }

    // The ring buffer insertion sort will handle duplicates and ordering automatically

    // Find insertion point to maintain sequence order (lock-free)
    size_t head = (size_t)os_atomic_load_long(&rb->head);
    size_t tail = (size_t)os_atomic_load_long(&rb->tail);

    // Calculate buffer utilization for monitoring
    size_t current_packets = (head >= tail) ? (head - tail) : (rb->max_capacity - tail + head);
    size_t utilization_percent = (current_packets * 100) / rb->max_capacity;

    // Check if buffer is full or at high utilization
    size_t next_head = (head + 1) % rb->max_capacity;
    if (next_head == tail) {
        // Buffer full: use dropping strategy to prevent continuous packet loss
        // Drop 10% of packets (minimum 2) to create breathing room
        size_t packets_to_drop = (current_packets / 10) + 2; // At least 2, typically 10%
        if (packets_to_drop > current_packets / 2) {
            packets_to_drop = current_packets / 2; // Never drop more than half
        }

        for (size_t i = 0; i < packets_to_drop && tail != head; i++) {
            tail = (tail + 1) % rb->max_capacity;
            os_atomic_set_long(&rb->tail, (long)tail);
        }

        // Log buffer utilization once per second
        static uint64_t last_full_log_time = 0;
        uint64_t now = os_gettime_ns();
        if (now - last_full_log_time >= 1000000000ULL) {
            C64_LOG_WARNING("%s buffer full: dropped %zu packets, utilization was=%zu%% (%zu/%zu packets)", type_name,
                            packets_to_drop, utilization_percent, current_packets, rb->max_capacity);
            last_full_log_time = now;
        }
    } else if (utilization_percent >= 90) {
        // Warn when approaching capacity (but don't spam logs)
        static uint64_t last_warn_log_time = 0;
        uint64_t now = os_gettime_ns();
        if (now - last_warn_log_time >= 5000000000ULL) { // Every 5 seconds for warnings
            C64_LOG_DEBUG("%s buffer high utilization: %zu%% (%zu/%zu packets)", type_name, utilization_percent,
                          current_packets, rb->max_capacity);
            last_warn_log_time = now;
        }
    }

    // Limit insertion sort complexity to prevent blocking
    size_t insert_pos = head;

    // For real-time performance, only do limited insertion sorting
    // Audio packets arrive more frequently, so use smaller search depth
    const size_t MAX_SEARCH_DEPTH = (rb->type == BUFFER_TYPE_VIDEO) ? 8 : 6;
    size_t search_depth = 0;
    bool found_insert_pos = false;

    // Always do insertion sort to maintain sequence order
    {
        // Limited backward search for insertion point
        size_t current = head;
        while (current != tail && search_depth < MAX_SEARCH_DEPTH) {
            size_t prev = (current == 0) ? rb->max_capacity - 1 : current - 1;
            if (prev == tail)
                break;

            if (!rb->slots[prev].valid) {
                current = prev;
                search_depth++;
                continue;
            }

            // Compare packets based on buffer type
            bool should_insert_here = false;
            if (rb->type == BUFFER_TYPE_VIDEO) {
                // Video packets: sort by frame number first, then line number
                int16_t frame_diff = (int16_t)(frame_num - rb->slots[prev].frame_num);
                if (frame_diff > 0) {
                    should_insert_here = true;
                } else if (frame_diff == 0) {
                    // Same frame: sort by line number
                    int16_t line_diff = (int16_t)(line_num - rb->slots[prev].line_num);
                    if (line_diff >= 0) {
                        should_insert_here = true;
                    }
                }
            } else {
                // Audio packets: use sequence number with wraparound handling
                int16_t seq_diff = (int16_t)(seq_num - rb->slots[prev].sequence_num);
                if (seq_diff >= 0) {
                    should_insert_here = true;
                }
            }

            if (should_insert_here) {
                // Found correct insertion point
                insert_pos = current;
                found_insert_pos = true;
                break;
            }

            current = prev;
            insert_pos = current;
            search_depth++;
        }
    }

    // Only do expensive shift operation if we found insertion point within search limit
    if (found_insert_pos && insert_pos != head) {
        // Limited shift operation to prevent blocking
        size_t shift_pos = head;
        size_t shift_count = 0;
        const size_t MAX_SHIFT_COUNT = (rb->type == BUFFER_TYPE_VIDEO) ? 8 : 6; // Audio has smaller limit

        while (shift_pos != insert_pos && shift_count < MAX_SHIFT_COUNT) {
            size_t prev = (shift_pos == 0) ? rb->max_capacity - 1 : shift_pos - 1;
            rb->slots[shift_pos] = rb->slots[prev];
            shift_pos = prev;
            shift_count++;
        }

        if (shift_count >= MAX_SHIFT_COUNT) {
            // Shift limit exceeded - insert at head to avoid blocking (packet may be slightly out of order)
            insert_pos = head;
            C64_LOG_DEBUG("%s: Shift limit exceeded for seq %u, inserting at head", type_name, seq_num);
        }
    }

    // Insert the new packet
    size_t copy_len = len < rb->packet_size ? len : rb->packet_size;
    memcpy(rb->slots[insert_pos].data, data, copy_len);

    // Zero-pad if packet is smaller than expected size
    if (copy_len < rb->packet_size) {
        memset(rb->slots[insert_pos].data + copy_len, 0, rb->packet_size - copy_len);
    }

    rb->slots[insert_pos].size = rb->packet_size;
    rb->slots[insert_pos].timestamp_us = ts;
    rb->slots[insert_pos].sequence_num = seq_num;
    rb->slots[insert_pos].frame_num = frame_num;
    rb->slots[insert_pos].line_num = line_num;
    rb->slots[insert_pos].valid = true;

    // No complex sequence tracking needed - let the pop function handle this

    // Log packet insertion details for debugging
    if (insert_pos != head) {
        if (rb->type == BUFFER_TYPE_VIDEO) {
            C64_LOG_DEBUG("%s: Inserted frame %u line %u (seq %u) at pos %zu (head was %zu)", type_name, frame_num,
                          line_num, seq_num, insert_pos, head);
        } else {
            C64_LOG_DEBUG("%s: Inserted seq %u at pos %zu (head was %zu)", type_name, seq_num, insert_pos, head);
        }
    }

    // Atomically update head pointer (single producer per buffer, so this is safe)
    os_atomic_set_long(&rb->head, (long)next_head);

    // Verify buffer ordering in debug builds
    debug_verify_buffer_ordering(rb, type_name);
}

// Pop the oldest packet (FIFO order) - essential for proper frame assembly (LOCK-FREE)
static int rb_pop_oldest(struct packet_ring_buffer *rb, struct packet_slot **out)
{
    if (!rb || !out) {
        return 0;
    }

    // Lock-free single consumer approach - only one thread should pop from each buffer
    long head = os_atomic_load_long(&rb->head);
    long tail = os_atomic_load_long(&rb->tail);

    if (head == tail) {
        // No packets available
        return 0;
    }

    *out = &rb->slots[tail];

    // Update expected sequence to track what we've consumed
    if (rb->slots[tail].valid && os_atomic_load_bool(&rb->seq_initialized)) {
        uint16_t popped_seq = rb->slots[tail].sequence_num;
        rb->next_expected_seq = popped_seq + 1;
    }

    // Atomically advance tail (single consumer, so this is safe)
    long new_tail = (tail + 1) % rb->max_capacity;
    os_atomic_set_long(&rb->tail, new_tail);

    return 1;
}

// Reset buffer with new active slot count
static void rb_reset(struct packet_ring_buffer *rb, size_t active_slots)
{
    if (!rb) {
        return;
    }

    pthread_mutex_lock(&rb->mutex);
    if (active_slots > rb->max_capacity) {
        active_slots = rb->max_capacity;
    }
    rb->active_slots = active_slots;

    // Atomically reset head and tail pointers (inside mutex for safety)
    os_atomic_set_long(&rb->head, 0);
    os_atomic_set_long(&rb->tail, 0);

    // Reset sequence tracking
    rb->next_expected_seq = 0;
    os_atomic_set_bool(&rb->seq_initialized, false);

    // Clear all slot validity
    for (size_t i = 0; i < rb->max_capacity; i++) {
        rb->slots[i].valid = false;
    }
    pthread_mutex_unlock(&rb->mutex);
}

// ----------------------------------
// Public API
// ----------------------------------

struct c64_network_buffer *c64_network_buffer_create(void)
{
    struct c64_network_buffer *buf = (struct c64_network_buffer *)malloc(sizeof(struct c64_network_buffer));
    if (!buf) {
        C64_LOG_ERROR("Failed to allocate network buffer");
        return NULL;
    }

    // Initialize video buffer
    buf->video.slots = buf->video_slots;
    buf->video.max_capacity = C64_MAX_VIDEO_PACKETS;
    buf->video.packet_size = C64_VIDEO_PACKET_SIZE;
    buf->video.delay_us = 0; // Initialize with no delay by default
    buf->video.type = BUFFER_TYPE_VIDEO;
    pthread_mutex_init(&buf->video.mutex, NULL);
    rb_reset(&buf->video, C64_MAX_VIDEO_PACKETS);

    // Initialize audio buffer (use smaller allocation for audio)
    buf->audio.slots = buf->audio_slots;
    buf->audio.max_capacity = C64_MAX_AUDIO_PACKETS;
    buf->audio.packet_size = C64_AUDIO_PACKET_SIZE;
    buf->audio.delay_us = 0; // Initialize with no delay by default
    buf->audio.type = BUFFER_TYPE_AUDIO;
    pthread_mutex_init(&buf->audio.mutex, NULL);
    rb_reset(&buf->audio, C64_MAX_AUDIO_PACKETS);

    C64_LOG_INFO("Network buffer created - Video: %zu slots, Audio: %zu slots", (size_t)C64_MAX_VIDEO_PACKETS,
                 (size_t)C64_MAX_AUDIO_PACKETS);

    return buf;
}

void c64_network_buffer_destroy(struct c64_network_buffer *buf)
{
    if (!buf) {
        return;
    }

    C64_LOG_INFO("Network buffer destroyed");
    pthread_mutex_destroy(&buf->video.mutex);
    pthread_mutex_destroy(&buf->audio.mutex);
    free(buf);
}

void c64_network_buffer_set_delay(struct c64_network_buffer *buf, size_t video_delay_ms, size_t audio_delay_ms)
{
    if (!buf) {
        return;
    }

    // Clamp delay values to maximum
    if (video_delay_ms > C64_MAX_DELAY_MS) {
        video_delay_ms = C64_MAX_DELAY_MS;
    }
    if (audio_delay_ms > C64_MAX_DELAY_MS) {
        audio_delay_ms = C64_MAX_DELAY_MS;
    }

    // Compute active slots based on packet rates
    size_t video_slots = (size_t)ceil((C64_MAX_VIDEO_RATE * video_delay_ms) / 1000.0);
    if (video_slots > buf->video.max_capacity) {
        video_slots = buf->video.max_capacity;
    }

    size_t audio_slots = (size_t)ceil((C64_MAX_AUDIO_RATE * audio_delay_ms) / 1000.0);
    if (audio_slots > buf->audio.max_capacity) {
        audio_slots = buf->audio.max_capacity;
    }

    // Store new delay values in microseconds
    uint64_t old_video_delay = buf->video.delay_us;
    uint64_t old_audio_delay = buf->audio.delay_us;
    buf->video.delay_us = video_delay_ms * 1000;
    buf->audio.delay_us = audio_delay_ms * 1000;

    C64_LOG_INFO("Buffer delay values set: video=%llu us (%zu ms), audio=%llu us (%zu ms)",
                 (unsigned long long)buf->video.delay_us, video_delay_ms, (unsigned long long)buf->audio.delay_us,
                 audio_delay_ms);

    // If delay was reduced, discard excess packets and adjust timestamps for immediate availability
    // IMPORTANT: Discard from tail (oldest in sequence order) to maintain packet ordering

    if (buf->video.delay_us < old_video_delay) {
        // Only flush entire buffer for extreme delay reductions (to zero or very small delays)
        // This prevents black screens while still allowing cleanup when needed
        if (buf->video.delay_us == 0 && old_video_delay > 50000) { // Only flush when going to zero from >50ms
            C64_LOG_INFO("Extreme video delay reduction to zero (%llu->0 us), flushing buffer for immediate playback",
                         (unsigned long long)old_video_delay);
            pthread_mutex_lock(&buf->video.mutex);
            rb_reset(&buf->video, video_slots);
            pthread_mutex_unlock(&buf->video.mutex);
        } else {
            // Calculate how many packets to keep for new delay
            size_t new_video_capacity = video_slots;
            pthread_mutex_lock(&buf->video.mutex);
            size_t head = buf->video.head;
            size_t tail = buf->video.tail;

            // Count current packets in buffer
            size_t current_packets = (head >= tail) ? (head - tail) : (buf->video.max_capacity - tail + head);

            if (current_packets > new_video_capacity) {
                size_t packets_to_discard = current_packets - new_video_capacity;
                size_t discarded = 0;

                // Discard from tail (oldest packets) to maintain sequence order
                while (discarded < packets_to_discard && tail != head) {
                    struct packet_slot *slot = &buf->video.slots[tail];
                    if (slot->valid) {
                        // Physically clear the slot data to ensure no stale data remains
                        slot->valid = false;
                        memset(slot->data, 0, sizeof(slot->data));
                        slot->size = 0;
                        slot->timestamp_us = 0;
                        slot->sequence_num = 0;
                        slot->frame_num = 0;
                        slot->line_num = 0;
                        discarded++;
                    }
                    tail = (tail + 1) % buf->video.max_capacity;
                    os_atomic_set_long(&buf->video.tail, (long)tail);
                }

                if (discarded > 0) {
                    C64_LOG_INFO("Video buffer: discarded %zu old packets due to delay reduction (sequence-ordered)",
                                 discarded);
                }
            }

            pthread_mutex_unlock(&buf->video.mutex);

            // Make all remaining packets immediately ready for reduced delay
            // This ensures instant effect when delay is reduced, regardless of their original timestamps
            pthread_mutex_lock(&buf->video.mutex);
            uint64_t now_us = os_gettime_ns() / 1000;
            size_t adjusted = 0;

            // Calculate target timestamp that makes packets ready NOW for the new delay
            uint64_t ready_timestamp = now_us - buf->video.delay_us - 1000; // 1ms safety margin

            // Adjust all slots in the buffer, not just occupied ones
            // This handles race conditions where packets might be in intermediate states
            for (size_t i = 0; i < buf->video.max_capacity; i++) {
                struct packet_slot *slot = &buf->video.slots[i];
                if (slot->valid) {
                    uint64_t old_timestamp = slot->timestamp_us;
                    slot->timestamp_us = ready_timestamp;
                    adjusted++;

                    // Log first few adjustments for debugging
                    if (adjusted <= 3) {
                        C64_LOG_INFO("Video packet %zu: adjusted timestamp %llu -> %llu us (seq=%u)", adjusted,
                                     (unsigned long long)old_timestamp, (unsigned long long)ready_timestamp,
                                     slot->sequence_num);
                    }
                }
            }

            if (adjusted > 0) {
                C64_LOG_INFO("Video buffer: made %zu packets immediately ready for new delay (%llu us)", adjusted,
                             (unsigned long long)buf->video.delay_us);
            }

            pthread_mutex_unlock(&buf->video.mutex);
        }
    }

    if (buf->audio.delay_us < old_audio_delay) {
        // Only flush entire buffer for extreme delay reductions (to zero or very small delays)
        // This prevents audio dropouts while still allowing cleanup when needed
        if (buf->audio.delay_us == 0 && old_audio_delay > 50000) { // Only flush when going to zero from >50ms
            C64_LOG_INFO("Extreme audio delay reduction to zero (%llu->0 us), flushing buffer for immediate playback",
                         (unsigned long long)old_audio_delay);
            pthread_mutex_lock(&buf->audio.mutex);
            rb_reset(&buf->audio, audio_slots);
            pthread_mutex_unlock(&buf->audio.mutex);
        } else {
            // Calculate how many packets to keep for new delay
            size_t new_audio_capacity = audio_slots;
            pthread_mutex_lock(&buf->audio.mutex);
            size_t head = buf->audio.head;
            size_t tail = buf->audio.tail;

            // Count current packets in buffer
            size_t current_packets = (head >= tail) ? (head - tail) : (buf->audio.max_capacity - tail + head);

            if (current_packets > new_audio_capacity) {
                size_t packets_to_discard = current_packets - new_audio_capacity;
                size_t discarded = 0;

                // Discard from tail (oldest packets) to maintain sequence order
                while (discarded < packets_to_discard && tail != head) {
                    struct packet_slot *slot = &buf->audio.slots[tail];
                    if (slot->valid) {
                        // Physically clear the slot data to ensure no stale data remains
                        slot->valid = false;
                        memset(slot->data, 0, sizeof(slot->data));
                        slot->size = 0;
                        slot->timestamp_us = 0;
                        slot->sequence_num = 0;
                        slot->frame_num = 0;
                        slot->line_num = 0;
                        discarded++;
                    }
                    tail = (tail + 1) % buf->audio.max_capacity;
                    os_atomic_set_long(&buf->audio.tail, (long)tail);
                }

                if (discarded > 0) {
                    C64_LOG_INFO("Audio buffer: discarded %zu old packets due to delay reduction (sequence-ordered)",
                                 discarded);
                }
            }

            pthread_mutex_unlock(&buf->audio.mutex);

            // Make all remaining packets immediately ready for reduced delay
            // This ensures instant effect when delay is reduced, regardless of their original timestamps
            pthread_mutex_lock(&buf->audio.mutex);
            uint64_t now_us = os_gettime_ns() / 1000;
            size_t adjusted = 0;

            // Calculate target timestamp that makes packets ready NOW for the new delay
            uint64_t ready_timestamp = now_us - buf->audio.delay_us - 1000; // 1ms safety margin

            // Adjust all slots in the buffer, not just occupied ones
            // This handles race conditions where packets might be in intermediate states
            for (size_t i = 0; i < buf->audio.max_capacity; i++) {
                struct packet_slot *slot = &buf->audio.slots[i];
                if (slot->valid) {
                    uint64_t old_timestamp = slot->timestamp_us;
                    slot->timestamp_us = ready_timestamp;
                    adjusted++;

                    // Log first few adjustments for debugging
                    if (adjusted <= 3) {
                        C64_LOG_INFO("Audio packet %zu: adjusted timestamp %llu -> %llu us (seq=%u)", adjusted,
                                     (unsigned long long)old_timestamp, (unsigned long long)ready_timestamp,
                                     slot->sequence_num);
                    }
                }
            }
            if (adjusted > 0) {
                C64_LOG_INFO("Audio buffer: made %zu packets immediately ready for new delay (%llu us)", adjusted,
                             (unsigned long long)buf->audio.delay_us);
            }

            pthread_mutex_unlock(&buf->audio.mutex);
        }
    }

    C64_LOG_INFO("Network buffer delay set - Video: %zu ms (%zu slots), Audio: %zu ms (%zu slots)", video_delay_ms,
                 video_slots, audio_delay_ms, audio_slots);
}

void c64_network_buffer_push_video(struct c64_network_buffer *buf, const uint8_t *data, size_t len,
                                   uint64_t timestamp_ns)
{
    if (!buf || !data) {
        return;
    }

    // Debug logging for network buffer push operations (respects global debug setting)
    static int push_count = 0;
    if ((push_count++ % 5000) == 0) {
        C64_LOG_DEBUG("Network buffer push video: packet %d (len=%zu)", push_count, len);
    }

    // Convert nanoseconds to microseconds for internal storage
    uint64_t timestamp_us = timestamp_ns / 1000;
    rb_push(&buf->video, data, len, timestamp_us);
}

void c64_network_buffer_push_audio(struct c64_network_buffer *buf, const uint8_t *data, size_t len,
                                   uint64_t timestamp_ns)
{
    if (!buf || !data) {
        return;
    }

    // Convert nanoseconds to microseconds for internal storage
    uint64_t timestamp_us = timestamp_ns / 1000;
    rb_push(&buf->audio, data, len, timestamp_us);
}

// Check if oldest packet in buffer has been delayed long enough
static bool is_packet_ready_for_pop(struct packet_slot *slot, uint64_t delay_us)
{
    if (!slot->valid) {
        return false;
    }

    // Get current time in microseconds (OBS uses nanoseconds, so convert)
    uint64_t now_us = os_gettime_ns() / 1000;
    uint64_t age_us = now_us - slot->timestamp_us;
    bool ready = age_us >= delay_us;

    // Debug logging removed for production use

    return ready;
}

int c64_network_buffer_pop(struct c64_network_buffer *buf, const uint8_t **video_data, size_t *video_size,
                           const uint8_t **audio_data, size_t *audio_size, uint64_t *timestamp_us)
{
    if (!buf || !video_data || !audio_data) {
        return 0;
    }

    // Lock-free check if we have video packets available
    long video_head = os_atomic_load_long(&buf->video.head);
    long video_tail = os_atomic_load_long(&buf->video.tail);

    if (video_head == video_tail) {
        // No video packets available - rare spot checks only (once per minute)
        static uint64_t last_empty_log_time = 0;
        uint64_t now = os_gettime_ns();
        if (now - last_empty_log_time >= 60000000000ULL) { // 1 minute in nanoseconds
            C64_LOG_DEBUG("📦 BUFFER EMPTY SPOT CHECK: No video packets available");
            last_empty_log_time = now;
        }
        return 0;
    }

    // Check if the OLDEST packet (at tail) has been delayed long enough
    struct packet_slot *oldest_video = &buf->video.slots[video_tail];
    if (!oldest_video->valid || !is_packet_ready_for_pop(oldest_video, buf->video.delay_us)) {
        // Oldest packet not ready yet - must wait to preserve FIFO ordering
        // Rare spot checks only (once per minute)
        static uint64_t last_delay_log_time = 0;
        uint64_t now = os_gettime_ns();
        if (now - last_delay_log_time >= 60000000000ULL) { // 1 minute in nanoseconds
            uint64_t now_us = now / 1000;
            uint64_t age_us = oldest_video->valid ? (now_us - oldest_video->timestamp_us) : 0;
            C64_LOG_DEBUG("⏰ DELAY WAIT SPOT CHECK: Oldest packet age=%llu us, need=%llu us",
                          (unsigned long long)age_us, (unsigned long long)buf->video.delay_us);
            last_delay_log_time = now;
        }
        return 0;
    }

    // Pop the ready video packet (oldest first for proper FIFO)
    struct packet_slot *v;
    if (!rb_pop_oldest(&buf->video, &v)) {
        C64_LOG_WARNING("Failed to pop ready video packet");
        return 0;
    }

    // Try to get audio packet (optional - lock-free check)
    struct packet_slot *a = NULL;
    bool has_audio = false;

    long audio_head = os_atomic_load_long(&buf->audio.head);
    long audio_tail = os_atomic_load_long(&buf->audio.tail);

    if (audio_head != audio_tail) {
        struct packet_slot *oldest_audio = &buf->audio.slots[audio_tail];
        if (is_packet_ready_for_pop(oldest_audio, buf->audio.delay_us)) {
            has_audio = rb_pop_oldest(&buf->audio, &a);
        }
    }

    // Periodic buffer pop monitoring (every 10 minutes)
    static int pop_count = 0;
    static uint64_t last_pop_log_time = 0;
    uint64_t now = os_gettime_ns();
    if ((++pop_count % 100000) == 0 || (now - last_pop_log_time >= 600000000000ULL)) { // Every 100k pops OR 10 minutes
        C64_LOG_DEBUG("Network buffer pop SPOT CHECK: video=yes, audio=%s (total count: %d)", has_audio ? "yes" : "no",
                      pop_count);
        last_pop_log_time = now;
    }

    // Return packet data
    *video_data = v->data;
    *video_size = v->size;

    if (has_audio && a) {
        *audio_data = a->data;
        *audio_size = a->size;
    } else {
        *audio_data = NULL;
        *audio_size = 0;
    }

    // Return timestamp (use video timestamp if no audio)
    if (timestamp_us) {
        *timestamp_us = (has_audio && a && a->timestamp_us < v->timestamp_us) ? a->timestamp_us : v->timestamp_us;
    }

    return 1;
}

void c64_network_buffer_flush(struct c64_network_buffer *buf)
{
    if (!buf) {
        return;
    }

    // Reset both buffers to empty state while maintaining active slot counts
    size_t video_active = buf->video.active_slots;
    size_t audio_active = buf->audio.active_slots;

    rb_reset(&buf->video, video_active);
    rb_reset(&buf->audio, audio_active);

    C64_LOG_INFO("Network buffers flushed");
}
