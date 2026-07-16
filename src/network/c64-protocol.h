/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_PROTOCOL_H
#define C64_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// C64 Stream constants
#define C64_VIDEO_PACKET_SIZE 780
#define C64_STREAM_DEST_MAX 80
#define C64_AUDIO_PACKET_SIZE 770
#define C64_VIDEO_HEADER_SIZE 12
#define C64_AUDIO_HEADER_SIZE 2
#define C64_CONTROL_PORT 64
#define C64_DEFAULT_VIDEO_PORT 21000
#define C64_DEFAULT_AUDIO_PORT 21001
#define C64_DEFAULT_HOST "c64u"

/*
 * C64STR-016: give each source instance a distinct default UDP port pair so two
 * sources added with defaults do not both bind 21000/21001 (which starves one
 * of video/audio). pair_index is a 0-based per-instance counter; instance N
 * gets video=21000+2N, audio=video+1, so pairs never overlap.
 */
static inline void c64_default_ports_for_pair(long pair_index, uint32_t *video_port, uint32_t *audio_port)
{
    const uint32_t video = C64_DEFAULT_VIDEO_PORT + (uint32_t)(pair_index * 2);
    if (video_port) {
        *video_port = video;
    }
    if (audio_port) {
        *audio_port = video + 1;
    }
}

// Video format constants
#define C64_PAL_WIDTH 384
#define C64_PAL_HEIGHT 272
#define C64_NTSC_WIDTH 384
#define C64_NTSC_HEIGHT 240
#define C64_PIXELS_PER_LINE 384
#define C64_BYTES_PER_LINE 192 // 384 pixels / 2 (4-bit per pixel) - keeping original
#define C64_LINES_PER_PACKET 4

/* All frame storage is PAL-sized.  Packet-derived heights must therefore
 * never escape this range before reaching render or recording code. */
static inline uint32_t c64_clamp_frame_height(uint32_t height)
{
    if (height < C64_NTSC_HEIGHT) {
        return C64_NTSC_HEIGHT;
    }
    if (height > C64_PAL_HEIGHT) {
        return C64_PAL_HEIGHT;
    }
    return height;
}

// Frame assembly constants
#define C64_MAX_PACKETS_PER_FRAME 68           // PAL: 272 lines ÷ 4 lines/packet = 68 packets
#define C64_FRAME_TIMEOUT_MS 100               // Timeout for incomplete frames
// Frame timing constants (ns precision for butter-smooth playback)
// These MUST match OBS canvas FPS to avoid frame duplication/skipping:
// - PAL: 401/8 fps = 50.125 Hz → 1000000000 / (401/8) = 19950124.844... ns
// - NTSC: 29913/500 fps = 59.826 Hz → 1000000000 / (29913/500) = 16715140.562... ns
#define C64_PAL_FRAME_INTERVAL_NS 19950125ULL  // 19.950125ms for 50.125Hz PAL (matches OBS canvas 401/8)
#define C64_NTSC_FRAME_INTERVAL_NS 16715141ULL // 16.715141ms for 59.826Hz NTSC (matches OBS canvas 29913/500)

// Audio sample rate constants (format-specific, derived from color subcarrier)
// These are exact fractional frequencies, not rounded integers.
// PAL:  (4433618.75 Hz × 16/9 × 15 / 77 / 32) = 47982.8869047619 Hz (-356.52 ppm from 48kHz)
// NTSC: (3579545.45 Hz × 16/7 × 15 / 80 / 32) = 47940.3408482143 Hz (-1242.9 ppm from 48kHz)
#define C64_PAL_AUDIO_SAMPLE_RATE 47982.8869047619   // PAL audio sample rate (exact)
#define C64_NTSC_AUDIO_SAMPLE_RATE 47940.3408482143  // NTSC audio sample rate (exact)

// Forward declaration
struct c64_source;

/**
 * Build the stream destination string "IP:PORT" used by the control protocol
 * (FF2n) and reusable verbatim as the REST `ip=` query parameter.
 *
 * This is the single source of truth for the destination string: the
 * legacy TCP command path and the REST stream-control path both consume it.
 * Defined inline so it is unit-testable without dragging in the heavy
 * c64-protocol.c dependency chain.
 * @return true on success, false if obs_ip is empty/NULL or the result would not fit
 */
static inline bool c64_stream_dest_is_ipv4(const char *ip)
{
    if (!ip || !ip[0]) {
        return false;
    }

    unsigned int octets = 0;
    unsigned int value = 0;
    bool have_digit = false;
    for (const char *cursor = ip;; cursor++) {
        const char ch = *cursor;
        if (ch >= '0' && ch <= '9') {
            have_digit = true;
            value = value * 10U + (unsigned int)(ch - '0');
            if (value > 255U) {
                return false;
            }
            continue;
        }
        if ((ch == '.' || ch == '\0') && have_digit) {
            octets++;
            if (ch == '\0') {
                return octets == 4;
            }
            value = 0;
            have_digit = false;
            continue;
        }
        return false;
    }
}

static inline bool c64_build_stream_dest(char *out, size_t out_size, const char *obs_ip, uint16_t port)
{
    if (!out || out_size == 0 || !c64_stream_dest_is_ipv4(obs_ip)) {
        return false;
    }
    const int n = snprintf(out, out_size, "%s:%u", obs_ip, (unsigned)port);
    return (n > 0 && (size_t)n < out_size);
}

/**
 * Send a legacy control command (TCP port 64) to an EXPLICIT target.
 *
 * Approach A's parameterised send: the destination device is a parameter, not
 * ambient context->ip_address state. c64_send_control_command() is a thin
 * wrapper around this that reads context->ip_address — the one permitted
 * ambient-state read (see the seamless-device-transition DRY ledger).
 *
 * @param host         Target device host/IP (must not be "0.0.0.0")
 * @param control_port Target device control port (default 64)
 * @param enable       true = start stream, false = stop stream
 * @param stream_id    0 = video, 1 = audio
 * @param dest         "IP:PORT" destination for start commands (NULL for stop)
 */
void c64_send_control_command_to(const char *host, uint32_t control_port, bool enable, uint8_t stream_id,
                                 const char *dest);

// Protocol operations
void c64_send_control_command(struct c64_source *context, bool enable, uint8_t stream_id);

// Network packet logging utilities (conditional execution for performance)
void c64_log_video_packet_if_enabled(struct c64_source *context, const uint8_t *packet, size_t packet_size,
                                     uint64_t timestamp_ns);
void c64_log_audio_packet_if_enabled(struct c64_source *context, const uint8_t *packet, size_t packet_size,
                                     uint64_t timestamp_ns);

#endif // C64_PROTOCOL_H
