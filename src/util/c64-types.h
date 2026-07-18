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
#include <util/threading.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include "c64-network.h"
#include "c64-network-buffer.h"
#include "c64-protocol.h"
#include "audio/c64-audio-timeline.h"
#include "c64-network-fifo.h"
#include "c64-effect-afterglow.h"
#include "c64-color.h"

// Forward declarations
typedef struct c64_rest_client c64_rest_client_t;
typedef struct c64_keyboard c64_keyboard_t;
typedef struct c64_keymap c64_keymap_t;
struct c64_record_writer;

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
    uint64_t start_time;            // When frame assembly started (first packet)
    uint64_t last_packet_time;      // When last packet arrived (for A/V sync)
    uint64_t packets_received_mask; // Bitmask of received packets (for 64 packets max)
};

#define C64_AV_SYNC_EVENT_QUEUE_SIZE 8
#define C64_AV_SYNC_ORIGIN_COUNT 2

struct c64_av_sync_event {
    uint64_t ts;
    uint32_t seq;
    uint16_t frame_num;
    bool used;
};

struct c64_av_sync_match {
    uint64_t video_ts;
    uint64_t audio_ts;
    uint32_t video_seq;
    uint32_t audio_seq;
    uint16_t video_frame_num;
    bool valid;
};

struct c64_av_sync_state {
    uint64_t last_video_pop_ts;
    uint64_t last_audio_pop_ts;
    uint64_t last_audio_pop_detection_ts; // Debounce: last audio pop detection time
    uint64_t last_video_pop_detection_ts; // Debounce: last video pop detection time
    uint32_t video_pop_count;
    uint32_t audio_pop_count;

    struct c64_av_sync_match last_match;

    struct c64_av_sync_event audio_events[C64_AV_SYNC_EVENT_QUEUE_SIZE];
    struct c64_av_sync_event video_events[C64_AV_SYNC_EVENT_QUEUE_SIZE];
    size_t audio_events_count;
    size_t video_events_count;
};

struct c64_source {
    obs_source_t *source;

    // Configuration
    char hostname[64];            // C64 Ultimate hostname or IP as entered by user
    char ip_address[64];          // C64 Ultimate IP Address (resolved from hostname)
    char dns_server_ip[64];       // DNS server IP for resolving hostnames (optional)
    char active_device_id[64];    // Selected profile currently applied to this source
    char c64_password[256];       // Optional Ultimate 64 REST password (sent as X-Password header)
    char obs_ip_address[64];      // OBS IP Address (this machine)
    pthread_mutex_t config_mutex; // Protects dns_server_ip/hostname/ip_address from concurrent access
    bool auto_detect_ip;
    // Written under config_mutex, but read per-packet by the video/audio
    // receivers without it (see c64-ingest-filter.h). volatile keeps the
    // receive loop from hoisting the load and latching one verdict forever;
    // the fields are word-sized, and the ip is published before the flag, so a
    // reader racing a switch either fails open or drops a few packets from the
    // device it is about to stop anyway.
    volatile bool expected_peer_ip_set; // Whether expected_peer_ip contains a valid IPv4 address
    volatile uint32_t expected_peer_ip; // Expected peer IPv4 address in network byte order (AF_INET)
    bool initial_ip_detected;           // Flag to track if initial IP detection was done
    uint32_t video_port;
    uint32_t audio_port;
    uint32_t control_port;
    bool streaming;
    uint64_t last_start_command_time_ns; // When we last requested streaming (START commands or start_streaming)

    // Per-source palette/color conversion state (C64STR-014).
    // Owns this source's active palette colours and the double-buffered pixel
    // LUT read by its video thread, so palette selection and colour edits stay
    // isolated per instance.  edit_mutex serialises writers (UI-thread selects
    // and colour edits); readers are lock-free via c64_color_lut_acquire.
    struct c64_color_lut color_lut;
    char palette_id[64];
    bool palette_initialized;
    pthread_mutex_t palette_mutex;

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
    uint64_t last_audio_submit_ns;
    uint64_t last_video_submit_ns;

    // Synthetic A/V timeline (single shared origin)
    pthread_mutex_t stream_start_mutex; // Guards one-time init/reset of stream_start_ns
    uint64_t stream_start_ns;           // Shared synthetic start time (ns)
    volatile bool stream_start_set;     // Atomic read (lock-free fast path)

    // Synthetic timestamps (derived solely from stream_start_ns + indices)
    uint64_t last_video_ts_ns; // Last synthetic video timestamp submitted to OBS
    uint64_t last_audio_ts_ns; // Last synthetic audio timestamp submitted to OBS

    // One-time startup timing logs
    uint64_t first_video_ts_ns;
    uint64_t first_audio_ts_ns;
    bool first_video_ts_logged;
    bool first_audio_ts_logged;
    bool initial_av_delta_logged;

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
    volatile long script_render_count;

    // Synthetic timestamp generation (monotonic, packet-index based)
    uint64_t audio_packet_count;              // Total audio packets processed since stream start
    struct c64_audio_timeline audio_timeline; // C64CLK-001/002: seq -> index state machine + error counters
    bool audio_last_sample_set;               // audio_last_left/right hold the final samples of the last played packet
    int16_t audio_last_left;                  // Concealment hold value, left channel
    int16_t audio_last_right;                 // Concealment hold value, right channel
    uint64_t audio_resync_warn_ns;            // Last RESYNC warning time (rate limit)
    uint64_t network_error_last_warning_ns;   // C64CLK-003 warning summary rate limit
    uint64_t network_error_window_lost;       // Counters at the last 60 s summary
    uint64_t network_error_window_concealed;
    uint64_t network_error_window_late;
    uint64_t network_error_window_duplicates;
    uint64_t network_error_window_resyncs;
    uint64_t audio_interval_ns;               // Nanoseconds per audio packet (format-specific)
    double audio_sample_rate;                 // Audio sample rate (exact: 47982.887 Hz PAL, 47940.341 Hz NTSC)
    uint64_t last_audio_timestamp_validation; // Last timestamp for progression validation

    // Video timestamp tracking (frame-index based, derived from observed frame numbers)
    uint16_t last_video_ts_frame_num;
    bool video_ts_frame_num_set;
    uint64_t video_frame_index;

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
    bool retry_thread_valid;           // retry_thread has been created and must be joined
    bool retry_shutting_down;          // C64STR-001: set under retry_thread_mutex during destroy;
                                       // blocks any late c64_schedule_retry from spawning a new
                                       // worker on a context being torn down (use-after-free guard)
    pthread_mutex_t retry_thread_mutex;
    volatile bool udp_port_conflict; // A local UDP port is in use; wait for a settings change before retrying
    uint32_t retry_count;            // Number of retry attempts
    uint32_t consecutive_failures;   // Consecutive TCP failures for backoff

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

    // Debug counters for packet loss investigation
    volatile long debug_recvfrom_calls;       // Total recvfrom calls made
    volatile long debug_recvfrom_eagain;      // Total EAGAIN/EWOULDBLOCK results
    volatile long debug_recvfrom_bytes_total; // Sum of all bytes from recvfrom (including partial/headers)
    volatile long debug_packets_dropped_size; // Packets dropped due to wrong size
    volatile long debug_packets_dropped_peer; // Packets dropped: sender != expected peer (ingest ownership filter)

    // Stage-1 UDP network FIFOs (decouple socket recv from buffering/order work)
    struct c64_network_fifo video_fifo;
    struct c64_network_fifo audio_fifo;
    struct c64_network_fifo_packet *video_fifo_entries;
    struct c64_network_fifo_packet *audio_fifo_entries;

    uint64_t last_stats_log_time;       // Last time video statistics were logged (non-atomic)
    uint64_t last_audio_stats_log_time; // Last time audio statistics were logged (non-atomic)
    uint64_t last_stats_tick_ns;        // Last time stats batching was checked (limits per-packet timing calls)

    // Frame saving for analysis (logo handled by async video - no manual logo needed)
    bool record_frames;
    char save_folder[512];
    uint32_t saved_frame_count;

    // Frame state flags
    bool frame_dirty; // Set when a new frame arrives; cleared after texture upload to avoid redundant copies

    // Video recording for analysis
    bool record_video;
    bool record_csv;     // CSV recording control (network and OBS events)
    bool record_av_sync; // av-sync.csv recording control (OBS AV SYNC matches)
    bool av_sync_prg_started;
    FILE *video_file;
    FILE *audio_file;
    FILE *timing_file;
    FILE *network_file;
    FILE *av_sync_file;
    char session_folder[800]; // Current session folder path
    uint64_t recording_start_time;
    uint64_t csv_timing_base_ns;   // Shared nanosecond timestamp when first CSV entry is written (network or OBS)
    bool csv_debug_enabled;        // Snapshot of debug state for CSV headers/rows
    uint64_t last_video_packet_us; // Last video packet timestamp for interval calculation (per-session)
    uint64_t last_audio_packet_us; // Last audio packet timestamp for interval calculation (per-session)
    volatile long recorded_frames;
    volatile long recorded_audio_samples;
    uint64_t recorded_audio_bytes;
    struct c64_record_writer *record_writer; /* C64CLK-005 background file writer */
    volatile long record_write_drops;
    /* Each legacy AVI file is independently valid.  Keep per-segment state
     * separate from session-wide counters so a rollover never wraps RIFF's
     * 32-bit length fields. */
    uint32_t avi_segment_index;
    uint32_t avi_segment_width;
    uint32_t avi_segment_height;
    double avi_segment_fps;
    uint32_t avi_segment_frames;
    uint64_t avi_segment_bytes;
    pthread_mutex_t recording_mutex;

    // Debug-only A/V pop detection (edge-based)
    bool av_sync_last_video_all_white;
    bool av_sync_last_audio_has_signal;

    // UDP full-frame-pop marker detection (packet-level). Kept separate from OBS frame probe state.
    bool av_sync_last_video_pop_marker;
    bool av_sync_last_audio_pop_marker;

    struct c64_av_sync_state av_sync[C64_AV_SYNC_ORIGIN_COUNT];
    pthread_mutex_t av_sync_mutex;

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
    struct c64_afterglow afterglow;     // CPU afterglow state + decay cache
    bool tint_enable;                   // Screen tint effect enable
    int tint_mode;                      // Tint mode (0=none, 1=amber, 2=green, 3=monochrome)
    float tint_strength;                // Tint strength (0.0-1.0)
    bool preserve_size;                 // Preserve OBS-facing footprint when effects change virtual size
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

    // REST control and keyboard capture
    c64_rest_client_t *rest_client; // REST API client
    c64_keyboard_t *keyboard;       // Keyboard capture and injection
    c64_keymap_t *keymap;           // Current keymap
    bool keyboard_capture_active;   // Runtime state: capture is currently active
    bool keyboard_ctrl_down;        // Ctrl key pressed state
    bool keyboard_meta_down;        // Meta/Super key pressed state
    bool keyboard_tab_down;         // Tab key pressed state
    bool keyboard_escape_down;      // Escape key pressed state
    bool keyboard_ctrl_meta_armed;  // Ctrl+Meta chord armed (no non-mod key pressed)
    bool keyboard_ctrl_meta_consumed;
    bool keyboard_reboot_consumed; // ESC+TAB chord already handled for current press cycle
    char rest_base_url[256];       // REST API base URL
    char keyboard_keymap_name[64]; // Keymap name (e.g., "symbolic_us")
    int stream_control_transport;  // 0 auto, 1 REST, 2 legacy
    uint64_t stream_rest_demoted_until_ns;
    volatile long rest_rebuild_pending;
    // Set for the whole of c64_start_streaming, which leaves `streaming` false
    // until its final step. Without it, a device switch landing inside that
    // window sees "not streaming", records no pending transition, and leaves
    // the previous device streaming at OBS forever. (atomic)
    volatile bool stream_start_in_flight;
    bool device_transition_pending;
    char device_transition_host[64];
    uint32_t device_transition_control_port;
    bool device_discovery_in_progress; // Drives the Find Devices button's label while a scan runs

    // Keyboard-driven joystick emulation (toggled by F10/F11; see c64_key_click)
    bool joystick_mode_active;   // false = direct keyboard relay, true = cursor/space -> joystick
    int joystick_emulation_port; // 1 or 2; default 2

    // Held interactive keys, so a physically held key reaches the C64 held down
    // (real-time games / pinball flippers) instead of a one-frame tap. Tracked
    // by native vkey: press on key-down, release on key-up, and auto-repeat
    // key-downs for an already-held key are ignored. Also dedupes held joystick
    // directions. See c64_key_click / c64_release_held_keys.
    struct {
        bool active;            // Slot occupied? (macOS vkey 0x00 = 'A' is valid, so no 0 sentinel)
        uint32_t vkey;          // Host virtual key that is held
        uint8_t output_byte;    // Resolved PETSCII byte, replayed as RELEASE on key-up
        bool is_joystick;       // Slot holds a joystick direction rather than a keystroke
        char joystick_input[8]; // For is_joystick: "up"/"down"/"left"/"right"/"fire"
    } held_keys[16];

    // Audio mixer snapshot for AV sync runs
    char **audio_mixer_snapshot_items;  // Item names (Audio Mixer)
    char **audio_mixer_snapshot_values; // Original values for each item
    size_t audio_mixer_snapshot_count;  // Number of tracked items
    bool audio_mixer_snapshot_active;   // Whether snapshot is valid

    // Script automation
    void *script_executor;          // c64_script_executor_t* (forward declaration to avoid circular dependency)
    void *current_script;           // c64_script_t* currently loaded script
    char script_file_path[512];     // Path to .c64script file
    uint64_t script_start_time;     // os_gettime_ns() when script started
    uint64_t script_end_time;       // os_gettime_ns() when script ended
    bool script_ended_successfully; // true if completed successfully, false if error/stopped
    int last_script_status;         // Last known c64_script_status_t for detecting state changes
    uint64_t last_ui_update_time;   // Last time UI was updated for throttling (os_gettime_ns())
    bool force_ui_update;           // Force immediate UI update on next tick (bypass throttling)
    bool script_autostarted;        // Whether auto-start has been triggered for current script
    char cached_last_line[512];     // Cached "last executed" line string for UI display
    char cached_next_line[512];     // Cached "next to execute" line string for UI display

    // Content automation
    void *automation;                      // c64_automation_t* automation engine
    char automation_status[256];           // Current automation status for UI display
    bool playlist_ui_update_in_progress;   // Guard for properties playlist UI updates
    bool playlist_properties_initializing; // Suppress playlist rebuild side effects during properties creation
    bool playlist_refresh_suppressed_once; // Skip one playlist rebuild during script-only UI refresh
    bool playlist_fingerprint_valid;       // Whether playlist fingerprint is initialized
    int playlist_last_selected_index;      // Last selected playlist index
    bool playlist_last_selected_valid;     // Whether last selected index is valid
    bool playlist_pending_active;          // Whether a user selection is pending while running
    int playlist_pending_index;            // Pending selection index while running
    int playlist_ignore_changes;           // Count of programmatic selection changes to ignore
    int playlist_window_start;             // First playlist index currently shown in the UI window
    bool playlist_window_follow_selection; // Center the UI window around the selected/current item
    int playlist_last_file_system;         // Last file system (0=local,1=C64U)
    int playlist_last_playback_source;     // Last playback source (0=single,1=folder)
    bool playlist_last_shuffle;            // Last shuffle flag
    bool playlist_last_include_subfolders; // Last recursive flag
    char playlist_last_path[512];          // Last path used to build playlist
};

#endif // C64_TYPES_H
