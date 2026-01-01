/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_NETWORK_BUFFER_H
#define C64_NETWORK_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define C64_VIDEO_PACKET_SIZE 780
#define C64_AUDIO_PACKET_SIZE 770

// Precise packet rates based on C64 Ultimate specification and code analysis:
// PAL: 68 packets per 19.95ms frame = 3408 packets/sec
// NTSC: 60 packets per 16.71ms frame = 3590 packets/sec - MAXIMUM
#define C64_MAX_VIDEO_RATE_PAL 3408                // PAL video packets per second
#define C64_MAX_VIDEO_RATE_NTSC 3590               // NTSC video packets per second (peak rate)
#define C64_MAX_VIDEO_RATE C64_MAX_VIDEO_RATE_NTSC // Use NTSC as worst case

// Audio: PAL 250.0 packets/sec (exact), NTSC 249.7 packets/sec
#define C64_MAX_AUDIO_RATE 250 // PAL rate (slightly higher than NTSC)

#define C64_MAX_DELAY_MS 500

// Maximum supported network jitter (ms). Must be < C64_MAX_DELAY_MS.
// Jitter = random per-packet delay variation (0 to max_jitter_ms).
// With jitter < delay, all packets arrive before their playback time.
#define C64_MAX_JITTER_MS 400

// Buffer sizing rationale:
// - At steady state, buffer holds ~delay_ms worth of packets awaiting playback.
// - With jitter, packets arrive reordered: early arrivals wait longer, late arrivals less.
// - Worst case: burst when many delayed packets arrive together with on-time packets.
// - Conservative sizing: (delay + jitter) * rate handles transients and startup bursts.
// - Formula: rate * (delay_ms + jitter_ms) / 1000 * 1.1 (10% margin)
#define C64_MAX_VIDEO_PACKETS (((C64_MAX_VIDEO_RATE * (C64_MAX_DELAY_MS + C64_MAX_JITTER_MS)) * 11) / 10000)
#define C64_MAX_AUDIO_PACKETS (((C64_MAX_AUDIO_RATE * (C64_MAX_DELAY_MS + C64_MAX_JITTER_MS)) * 11) / 10000)

struct c64_network_buffer;

// Create/destroy buffer (allocates maximum size buffers)
struct c64_network_buffer *c64_network_buffer_create(void);
void c64_network_buffer_destroy(struct c64_network_buffer *buf);

// Set dynamic delay in milliseconds (flushes buffer)
void c64_network_buffer_set_delay(struct c64_network_buffer *buf, size_t video_delay_ms, size_t audio_delay_ms);

// Push packet from network thread (timestamp in nanoseconds, converted internally to microseconds)
void c64_network_buffer_push_video(struct c64_network_buffer *buf, const uint8_t *data, size_t len,
                                   uint64_t timestamp_ns);
void c64_network_buffer_push_audio(struct c64_network_buffer *buf, const uint8_t *data, size_t len,
                                   uint64_t timestamp_ns);

// Pop the latest aligned pair for render thread
// Returns 1 if a pair is available, 0 if buffers empty
int c64_network_buffer_pop(struct c64_network_buffer *buf, const uint8_t **video_data, size_t *video_size,
                           const uint8_t **audio_data, size_t *audio_size, uint64_t *timestamp_us);

// Flush all buffers (clear all pending data)
void c64_network_buffer_flush(struct c64_network_buffer *buf);

#ifdef __cplusplus
}
#endif

#endif // C64_NETWORK_BUFFER_H
