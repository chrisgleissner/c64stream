/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_TYPES_H
#define C64_TYPES_H

#include <obs-module.h>
#include <media-io/audio-io.h>
#include <graphics/graphics.h>
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include "c64-network.h"
#include "c64-network-buffer.h"
#include "c64-protocol.h"

// Frame packet structure for reordering
struct frame_packet {
    uint16_t line_num;
    uint8_t lines_per_packet;
    uint8_t packet_data[780 - 12]; // C64_VIDEO_PACKET_SIZE - C64_VIDEO_HEADER_SIZE
    bool received;
};

// Frame assembly structure (optimized for lock-free operations)
struct frame_assembly {
    uint16_t frame_num;
    struct frame_packet packets[C64_MAX_PACKETS_PER_FRAME];
    uint16_t received_packets; // Number of packets received
    uint16_t expected_packets;
    bool complete;                  // Frame completion flag
    uint64_t start_time;            // When frame assembly started
    uint64_t packets_received_mask; // Bitmask of received packets (for 64 packets max)
};

struct c64_source {
    obs_source_t *source;

    // Configuration
    char hostname[64];            // C64 Ultimate hostname or IP as entered by user
    char ip_address[64];          // C64 Ultimate IP Address (resolved from hostname)
    char dns_server_ip[64];       // DNS server IP for resolving hostnames (optional)
    char obs_ip_address[64];      // OBS IP Address (this machine)
    pthread_mutex_t config_mutex; // Protects dns_server_ip/hostname/ip_address from concurrent access
    bool auto_detect_ip;
    bool initial_ip_detected; // Flag to track if initial IP detection was done
    uint32_t video_port;
    uint32_t audio_port;
    uint32_t control_port;
    bool streaming;

    // Video data
    uint32_t width;
    uint32_t height;
    uint8_t *video_buffer;

    // Single frame buffer for direct async video output
    uint32_t *frame_buffer; // Single buffer for UDP assembly and direct output via obs_source_output_video()

    // Pre-rendered logo frame buffers for instant no-connection display
    uint32_t *logo_frame_buffer_pal;    // Pre-rendered PAL logo frame (384x272)
    uint32_t *logo_frame_buffer_ntsc;   // Pre-rendered NTSC logo frame (384x240)
    bool last_connected_format_was_pal; // Track last connected format for logo selection

    // Frame assembly and packet reordering
    struct frame_assembly current_frame;
    uint16_t last_completed_frame;
    uint32_t frame_drops;
    uint32_t packet_drops;

    // Frame diagnostic counters (Stats for Nerds style)
    uint32_t frames_expected;
    uint32_t frames_captured;
    uint32_t frames_delivered_to_obs;
    uint32_t frames_completed;
    uint64_t last_capture_time;
    uint64_t total_capture_latency;
    uint64_t total_pipeline_latency;

    // Dynamic video format detection
    uint32_t detected_frame_height;
    bool format_detected;
    bool format_hint_set;
    double expected_fps;

    // Audio data
    struct audio_output_info audio_info;

    // Network
    socket_t video_socket;
    socket_t audio_socket;
    socket_t control_socket;
    pthread_t video_thread;
    pthread_t video_processor_thread;
    pthread_t audio_thread;
    volatile bool thread_active;                 // Atomic thread control flags (lock-free)
    volatile bool video_thread_active;           // Atomic thread control flags (lock-free)
    volatile bool video_processor_thread_active; // Atomic thread control flags (lock-free)
    volatile bool audio_thread_active;           // Atomic thread control flags (lock-free)

    // Synchronization (frame_mutex no longer needed for async video output)
    pthread_mutex_t assembly_mutex;

    // Frame timing
    uint64_t last_frame_time;
    uint64_t frame_interval_ns; // Target frame interval (20ms for 50Hz PAL)

    // Ideal timestamp generation for OBS async video
    uint64_t stream_start_time_ns; // Base timestamp when streaming started
    uint16_t first_frame_num;      // First frame number seen (for offset calculation)
    bool timestamp_base_set;       // Flag indicating if base timestamp is established

    // Monotonic audio timestamp generation
    uint64_t audio_packet_count;              // Total audio packets processed since stream start
    uint64_t audio_interval_ns;               // Nanoseconds per audio packet (4ms for 192 samples at 48kHz)
    uint64_t audio_base_time;                 // Base timestamp for synthetic audio timestamps
    uint64_t last_audio_timestamp_validation; // Last timestamp for progression validation

    // Logo texture for no-connection display
    gs_texture_t *logo_texture; // Loaded logo texture for async video output
    bool logo_texture_loaded;   // Flag to track if logo texture loading was attempted

    // Logo PNG pixel data cache (loaded once with stb_image)
    uint32_t *logo_pixels; // Cached PNG pixel data (RGBA format)
    uint32_t logo_width;   // PNG image width
    uint32_t logo_height;  // PNG image height

    // Render callback based timeout detection
    uint64_t last_udp_packet_time;     // Timestamp of last UDP packet (DEPRECATED - use separate fields)
    uint64_t last_video_packet_time;   // Timestamp of last video UDP packet
    uint64_t last_audio_packet_time;   // Timestamp of last audio UDP packet
    volatile long retry_in_progress;   // Flag to prevent redundant retry attempts (atomic: 0/1)
    pthread_t retry_thread;            // Background retry/connect thread (never run on OBS UI thread)
    volatile long retry_thread_active; // atomic: 0/1
    uint32_t retry_count;              // Number of retry attempts
    uint32_t consecutive_failures;     // Consecutive TCP failures for backoff

    // Network buffer for packet jitter correction
    struct c64_network_buffer *network_buffer; // Unified network buffer for video and audio packets
    uint32_t buffer_delay_ms;                  // Buffer delay in milliseconds

    // Auto-start control
    bool auto_start_attempted;

    // Statistics counters (atomic for lock-free hot path updates)
    volatile long video_packets_received; // Total video packets received (atomic)
    volatile long video_bytes_received;   // Total video bytes received (atomic)
    volatile long video_sequence_errors;  // Sequence number errors (atomic)
    volatile long video_frames_processed; // Total video frames processed (atomic)
    volatile long audio_packets_received; // Total audio packets received (atomic)
    volatile long audio_bytes_received;   // Total audio bytes received (atomic)
    uint64_t last_stats_log_time;         // Last time video statistics were logged (non-atomic)
    uint64_t last_audio_stats_log_time;   // Last time audio statistics were logged (non-atomic)
    uint64_t last_stats_tick_ns;          // Last time stats batching was checked (limits per-packet timing calls)

    // Frame saving for analysis (logo handled by async video - no manual logo needed)
    bool record_frames;
    char save_folder[512];
    uint32_t saved_frame_count;

    // Frame state flags
    bool frame_dirty; // Set when a new frame arrives; cleared after texture upload to avoid redundant copies

    // Video recording for analysis
    bool record_video;
    bool record_csv; // CSV recording control (network and OBS events)
    FILE *video_file;
    FILE *audio_file;
    FILE *timing_file;
    FILE *network_file;
    char session_folder[800]; // Current session folder path
    uint64_t recording_start_time;
    uint64_t csv_timing_base_ns; // Shared nanosecond timestamp when first CSV entry is written (network or OBS)
    volatile long recorded_frames;
    volatile long recorded_audio_samples;
    pthread_mutex_t recording_mutex;

    // Pre-allocated recording buffers (eliminates malloc/free in hot paths)
    uint8_t *bmp_row_buffer;      // Pre-allocated BMP row buffer for frame saving
    uint8_t *bgr_frame_buffer;    // Pre-allocated BGR buffer for video recording
    size_t recording_buffer_size; // Size of allocated recording buffers

    // CRT visual effects
    float scan_line_distance;           // Scan line distance (0.0-2.0, percentage of scan line width)
    float scan_line_strength;           // Scan line strength (0.0-1.0, darkness of gaps)
    float pixel_width;                  // Pixel geometry width (0.5-3.0)
    float pixel_height;                 // Pixel geometry height (0.5-3.0)
    float blur_strength;                // Blur strength for pixel geometry (0.0-1.0)
    bool bloom_enable;                  // Bloom effect enable
    float bloom_strength;               // Bloom strength (0.0-1.0, internally scaled 7.5x)
    bool afterglow_enable;              // Afterglow effect enable
    int afterglow_duration_ms;          // Afterglow duration in milliseconds
    int afterglow_curve;                // Afterglow decay curve (0=linear, 1=exponential)
    bool tint_enable;                   // Screen tint effect enable
    int tint_mode;                      // Tint mode (0=none, 1=amber, 2=green, 3=monochrome)
    float tint_strength;                // Tint strength (0.0-1.0)
    gs_texture_t *render_texture;       // GPU texture for rendering with effects
    uint32_t render_texture_width;      // Cached render_texture width (avoid gs_texture_get_width outside graphics)
    uint32_t render_texture_height;     // Cached render_texture height
    gs_texture_t *intermediate_texture; // Intermediate texture for blur/bloom/tint at native resolution
    gs_effect_t *crt_effect;            // CRT shader effect
    gs_samplerstate_t *point_sampler;   // Point (nearest-neighbor) sampler for sharp pixel rendering

    // Afterglow (shader ping-pong targets; also used by CPU/headless paths for deterministic persistence)
    gs_texture_t *afterglow_accum_prev; // Ping-pong texture for afterglow accumulation
    gs_texture_t *afterglow_accum_next; // Ping-pong texture for afterglow accumulation
    uint64_t last_frame_time_ns;        // Last frame timestamp for afterglow delta calculation
    float afterglow_dt_ms;              // Stable per-tick delta time (ms) used by afterglow shader
    uint64_t afterglow_last_tick_ns;    // Timestamp of last video_tick (ns) for dt calculation

    // CPU afterglow accumulation (fallback / deterministic path for E2E + headless)
    uint32_t *afterglow_cpu_accum; // RGBA accumulator (same size as frame_buffer)
    size_t afterglow_cpu_bytes;    // Allocated size in bytes for afterglow_cpu_accum
    bool afterglow_cpu_valid;      // Whether accumulator contains valid prior state

    // Cached decay factors for expf() optimization (avoid recomputation)
    float cached_decay_r;
    float cached_decay_g;
    float cached_decay_b;
    float cached_dt_ms;     // Delta time used for cached decay factors
    int cached_duration_ms; // Duration used for cached decay factors
    int cached_curve;       // Curve used for cached decay factors
    bool decay_cache_valid; // Whether decay cache is valid
};

#endif // C64_TYPES_H
