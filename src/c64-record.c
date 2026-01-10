/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#include <obs-module.h>
#include <util/platform.h>
#include <util/threading.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include "c64-logging.h"
#include "c64-record.h"
#include "c64-record-obs.h"
#include "c64-record-network.h"
#include "c64-record-video.h"
#include "c64-record-audio.h"
#include "c64-record-frames.h"
#include "c64-rest-client.h"
#include "c64-types.h"
#include "c64-file.h"

enum c64_rest_job_action {
    C64_REST_JOB_RUN_PRG = 0,
    C64_REST_JOB_RESET = 1,
};

struct c64_rest_job {
    enum c64_rest_job_action action;
    char base_url[256];
    char password[256];
    char prg_path[1024];
};

static bool c64_rest_read_file_to_buffer(const char *path, uint8_t **out_data, size_t *out_size)
{
    if (!path || !out_data || !out_size) {
        return false;
    }

    *out_data = NULL;
    *out_size = 0;

    FILE *f = os_fopen(path, "rb");
    if (!f) {
        return false;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }

    long sz = ftell(f);
    if (sz <= 0) {
        fclose(f);
        return false;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }

    uint8_t *data = (uint8_t *)malloc((size_t)sz);
    if (!data) {
        fclose(f);
        return false;
    }

    size_t n = fread(data, 1, (size_t)sz, f);
    fclose(f);

    if (n != (size_t)sz) {
        free(data);
        return false;
    }

    *out_data = data;
    *out_size = n;
    return true;
}

static bool c64_rest_starts_with(const char *s, const char *prefix)
{
    if (!s || !prefix) {
        return false;
    }
    size_t n = strlen(prefix);
    return strncmp(s, prefix, n) == 0;
}

static void c64_rest_build_base_url(const char *input, char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return;
    }

    out[0] = '\0';
    if (!input || input[0] == '\0') {
        return;
    }

    const char *s = input;
    if (c64_rest_starts_with(s, "http://")) {
        s += strlen("http://");
    } else if (c64_rest_starts_with(s, "https://")) {
        // Match previous behavior: accept, but use plain HTTP.
        s += strlen("https://");
    }

    // Strip any path.
    const char *slash = strchr(s, '/');
    size_t host_len = slash ? (size_t)(slash - s) : strlen(s);
    if (host_len == 0) {
        return;
    }

    const char prefix[] = "http://";
    const size_t prefix_len = sizeof(prefix) - 1;
    if (out_size <= prefix_len + host_len) {
        // Not enough space for prefix + host + NUL.
        return;
    }

    memcpy(out, prefix, prefix_len);
    memcpy(out + prefix_len, s, host_len);
    out[prefix_len + host_len] = '\0';
}

static void *c64_rest_thread_main(void *arg)
{
    struct c64_rest_job *job = (struct c64_rest_job *)arg;
    if (!job) {
        return NULL;
    }

    const char *password = (job->password[0] != '\0') ? job->password : NULL;
    c64_rest_client_t *client = c64_rest_client_create(job->base_url, password);
    if (!client) {
        C64_LOG_WARNING("REST: failed to create client for %s", job->base_url);
        free(job);
        return NULL;
    }

    bool ok = false;
    if (job->action == C64_REST_JOB_RESET) {
        ok = c64_rest_reset(client);
    } else if (job->action == C64_REST_JOB_RUN_PRG) {
        uint8_t *prg_data = NULL;
        size_t prg_size = 0;
        if (c64_rest_read_file_to_buffer(job->prg_path, &prg_data, &prg_size)) {
            ok = c64_rest_run_prg(client, prg_data, prg_size);
        }
        free(prg_data);
    }

    if (!ok) {
        const char *err = c64_rest_get_error(client);
        if (err && err[0] != '\0') {
            C64_LOG_WARNING("REST: request failed: %s", err);
        } else {
            C64_LOG_WARNING("REST: request failed");
        }
    }

    c64_rest_client_destroy(client);
    free(job);
    return NULL;
}

static bool c64_rest_launch_job(struct c64_rest_job *job)
{
    if (!job) {
        return false;
    }

    pthread_t t;
    int err = pthread_create(&t, NULL, c64_rest_thread_main, job);
    if (err != 0) {
        free(job);
        return false;
    }

    pthread_detach(t);
    return true;
}

static bool c64_rest_run_prg_async(const char *host, const char *password, const char *prg_path)
{
    if (!host || host[0] == '\0' || !prg_path || prg_path[0] == '\0') {
        return false;
    }

    struct c64_rest_job *job = (struct c64_rest_job *)calloc(1, sizeof(*job));
    if (!job) {
        return false;
    }

    job->action = C64_REST_JOB_RUN_PRG;
    c64_rest_build_base_url(host, job->base_url, sizeof(job->base_url));
    if (job->base_url[0] == '\0') {
        free(job);
        return false;
    }

    if (password && password[0] != '\0') {
        strncpy(job->password, password, sizeof(job->password) - 1);
    }
    strncpy(job->prg_path, prg_path, sizeof(job->prg_path) - 1);
    return c64_rest_launch_job(job);
}

static bool c64_rest_reset_machine_async(const char *host, const char *password)
{
    if (!host || host[0] == '\0') {
        return false;
    }

    struct c64_rest_job *job = (struct c64_rest_job *)calloc(1, sizeof(*job));
    if (!job) {
        return false;
    }

    job->action = C64_REST_JOB_RESET;
    c64_rest_build_base_url(host, job->base_url, sizeof(job->base_url));
    if (job->base_url[0] == '\0') {
        free(job);
        return false;
    }

    if (password && password[0] != '\0') {
        strncpy(job->password, password, sizeof(job->password) - 1);
    }
    return c64_rest_launch_job(job);
}

#ifndef S_ISDIR
#ifdef _WIN32
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
#endif

// Session management functions

/**
 * Ensure recording session exists, create timestamped folder if needed
 * @param context Source context containing session state
 */
void c64_session_ensure_exists(struct c64_source *context)
{
    // If session already exists, do nothing
    if (context->session_folder[0] != '\0') {
        return;
    }

    // Create new session folder with timestamp
    time_t rawtime = time(NULL);
    struct tm *timeinfo = localtime(&rawtime);

    snprintf(context->session_folder, sizeof(context->session_folder), "%s/session_%04d%02d%02d_%02d%02d%02d",
             context->save_folder, timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday, timeinfo->tm_hour,
             timeinfo->tm_min, timeinfo->tm_sec);

    // Create the session directory recursively (cross-platform)
    C64_LOG_INFO("" RECORD_LOG_PREFIX " Attempting to create session directory: %s", context->session_folder);
    if (!c64_create_directory_recursive(context->session_folder)) {
        C64_LOG_WARNING("" RECORD_LOG_PREFIX " Failed to create session directory: %s", context->session_folder);
        context->session_folder[0] = '\0'; // Clear on failure
    } else {
        C64_LOG_INFO("" RECORD_LOG_PREFIX " Successfully created recording session: %s", context->session_folder);
    }
}

/**
 * Check if any recording type is currently active
 * @param context Source context
 * @return true if frame saving, video recording, or CSV recording is enabled
 */
bool c64_session_any_recording_active(struct c64_source *context)
{
    return context->record_frames || context->record_video || context->record_csv || context->record_av_sync;
}

/**
 * Stop CSV timing recording
 * @param context Source context
 */
void c64_stop_obs_csv_recording(struct c64_source *context)
{
    if (context->timing_file) {
        fclose(context->timing_file);
        context->timing_file = NULL;
        context->csv_timing_base_ns = 0; // Reset timing base for next recording session
        C64_LOG_INFO("" RECORD_LOG_PREFIX " CSV timing recording stopped");
    }
}

void c64_stop_av_sync_csv_recording(struct c64_source *context)
{
    if (context->av_sync_file) {
        fclose(context->av_sync_file);
        context->av_sync_file = NULL;
        C64_LOG_INFO("" RECORD_LOG_PREFIX " av-sync CSV recording stopped");
    }
}

/**
 * Stop network packet recording
 * @param context Source context
 */
void c64_stop_network_csv_recording(struct c64_source *context)
{
    if (context->network_file) {
        fclose(context->network_file);
        context->network_file = NULL;
        C64_LOG_INFO("" RECORD_LOG_PREFIX " Network packet recording stopped");
    }
}

/**
 * Clean up session if no recording is active
 * @param context Source context
 */
void c64_session_cleanup_if_needed(struct c64_source *context)
{
    if (!c64_session_any_recording_active(context)) {
        // Stop all recording when session ends
        c64_stop_obs_csv_recording(context);
        c64_stop_network_csv_recording(context);
        c64_stop_av_sync_csv_recording(context);
        context->session_folder[0] = '\0';
        context->csv_debug_enabled = false;
        C64_LOG_INFO("" RECORD_LOG_PREFIX " Recording session ended");
    }
}

// Main entry point functions - delegate to appropriate modules

/**
 * Save single frame as BMP file (delegates to frames module)
 * @param context Source context
 * @param frame_buffer RGBA frame data
 */
void c64_save_frame_as_bmp(struct c64_source *context, uint32_t *frame_buffer)
{
    c64_frames_save_as_bmp(context, frame_buffer);
}

/**
 * Record video frame to AVI file (delegates to video module)
 * @param context Source context
 * @param frame_buffer RGBA frame data
 */
void c64_record_video_frame(struct c64_source *context, uint32_t *frame_buffer)
{
    c64_video_record_frame(context, frame_buffer);
}

/**
 * Record audio data to WAV file (delegates to audio module)
 * @param context Source context
 * @param audio_data PCM audio data
 * @param data_size Size of audio data in bytes
 */
void c64_record_audio_data(struct c64_source *context, const uint8_t *audio_data, size_t data_size)
{
    c64_audio_record_data(context, audio_data, data_size);
}

/**
 * Start CSV timing recording for any recording type
 * @param context Source context
 */
void c64_start_obs_csv_recording(struct c64_source *context)
{
    if (context->timing_file) {
        return; // Already recording CSV
    }

    // Ensure we have a recording session
    C64_LOG_INFO("" RECORD_LOG_PREFIX " Creating CSV recording session...");
    c64_session_ensure_exists(context);
    if (context->session_folder[0] == '\0') {
        C64_LOG_WARNING("" RECORD_LOG_PREFIX " Failed to create recording session for CSV logging");
        return;
    }
    C64_LOG_INFO("" RECORD_LOG_PREFIX " Session folder created: %s", context->session_folder);

    // Create CSV timing file
    char timing_filename[950];
    snprintf(timing_filename, sizeof(timing_filename), "%s/obs.csv", context->session_folder);

    context->timing_file = fopen(timing_filename, "w");
    if (!context->timing_file) {
        C64_LOG_ERROR("" RECORD_LOG_PREFIX " Failed to create CSV timing file: %s", timing_filename);
        return;
    }

    context->csv_debug_enabled = c64_debug_logging;

    // Write CSV header
    c64_obs_write_header(context);
    C64_LOG_INFO("" RECORD_LOG_PREFIX " Started CSV timing recording: %s", timing_filename);
}

void c64_start_av_sync_csv_recording(struct c64_source *context)
{
    if (context->av_sync_file) {
        return;
    }

    c64_session_ensure_exists(context);
    if (context->session_folder[0] == '\0') {
        C64_LOG_WARNING("" RECORD_LOG_PREFIX " Failed to create recording session for av-sync CSV");
        return;
    }

    char filename[950];
    snprintf(filename, sizeof(filename), "%s/av-sync.csv", context->session_folder);

    context->av_sync_file = fopen(filename, "w");
    if (!context->av_sync_file) {
        C64_LOG_ERROR("" RECORD_LOG_PREFIX " Failed to create av-sync CSV file: %s", filename);
        return;
    }

    // av-sync.csv is very low volume; if OBS terminates abruptly the stdio buffer may never flush.
    // Prefer line buffering and explicit flushes so E2E can reliably validate the file.
    setvbuf(context->av_sync_file, NULL, _IOLBF, 0);

    fprintf(context->av_sync_file,
            "trigger,detected,obs_offset_ms,obs_video_seq,obs_audio_seq,obs_video_frame,obs_video_ts_ns,obs_audio_ts_"
            "ns,has_network_match,net_offset_ms,net_video_seq,net_audio_seq,net_video_frame,net_video_ts_ns,net_audio_"
            "ts_ns,net_to_obs_video_ms,net_to_obs_audio_ms\n");

    fflush(context->av_sync_file);

    C64_LOG_INFO("" RECORD_LOG_PREFIX " Started av-sync CSV recording: %s", filename);
}

/**
 * Start network packet recording for network analysis
 * @param context Source context
 */
void c64_start_network_csv_recording(struct c64_source *context)
{
    if (context->network_file) {
        return; // Already recording network packets
    }

    // Ensure we have a recording session
    C64_LOG_DEBUG("" RECORD_LOG_PREFIX " Creating network recording session...");
    c64_session_ensure_exists(context);
    if (context->session_folder[0] == '\0') {
        C64_LOG_WARNING("" RECORD_LOG_PREFIX " Failed to create recording session for network logging");
        return;
    }
    C64_LOG_DEBUG("" RECORD_LOG_PREFIX " Network session folder: %s", context->session_folder);

    // Create network packet file
    char network_filename[950];
    snprintf(network_filename, sizeof(network_filename), "%s/network.csv", context->session_folder);

    context->network_file = fopen(network_filename, "w");
    if (!context->network_file) {
        C64_LOG_ERROR("" RECORD_LOG_PREFIX " Failed to create network packet file: %s", network_filename);
        return;
    }

    // Optimize for high throughput: 4MB buffer to minimize syscalls
    // This allows the consumer thread to write to memory and stay ahead of the ring buffer
    setvbuf(context->network_file, NULL, _IOFBF, 4 * 1024 * 1024);

    context->csv_debug_enabled = c64_debug_logging;

    // Write network CSV header
    c64_network_write_header(context);
    C64_LOG_INFO("" RECORD_LOG_PREFIX " Started network packet recording: %s", network_filename);
}

/**
 * Start video recording session (AVI + WAV + CSV)
 * @param context Source context
 */
void c64_start_video_recording(struct c64_source *context)
{
    if (!context->record_video || context->video_file) {
        return; // Already recording or not enabled
    }

    if (pthread_mutex_lock(&context->recording_mutex) != 0) {
        return;
    }

    // Ensure session exists for video recording
    c64_session_ensure_exists(context);
    if (context->session_folder[0] == '\0') {
        C64_LOG_ERROR("" RECORD_LOG_PREFIX " Failed to create recording session for video recording");
        pthread_mutex_unlock(&context->recording_mutex);
        return;
    }

    // Start CSV and network recording if enabled
    if (context->record_csv) {
        c64_start_obs_csv_recording(context);
        c64_start_network_csv_recording(context);
    }

    // Create filenames in the session folder
    char video_filename[950], audio_filename[950];
    snprintf(video_filename, sizeof(video_filename), "%s/video.avi", context->session_folder);
    snprintf(audio_filename, sizeof(audio_filename), "%s/audio.wav", context->session_folder);

    // Open files for recording
    context->video_file = fopen(video_filename, "wb");
    context->audio_file = fopen(audio_filename, "wb");

    if (!context->video_file || !context->audio_file) {
        C64_LOG_ERROR("" RECORD_LOG_PREFIX " Failed to create recording files");
        if (context->video_file) {
            fclose(context->video_file);
            context->video_file = NULL;
        }
        if (context->audio_file) {
            fclose(context->audio_file);
            context->audio_file = NULL;
        }
        pthread_mutex_unlock(&context->recording_mutex);
        return;
    }

    uint64_t timestamp_ms = os_gettime_ns() / 1000000;
    context->recording_start_time = timestamp_ms;
    os_atomic_store_long(&context->recorded_frames, 0);
    os_atomic_store_long(&context->recorded_audio_samples, 0);

    // Write AVI header with detected frame rate
    c64_video_write_avi_header(context->video_file, context->width, context->height, context->expected_fps);

    // Write WAV header to audio file
    c64_audio_write_wav_header(context->audio_file, 48000, 2, 16); // 48kHz stereo 16-bit

    C64_LOG_INFO("" RECORD_LOG_PREFIX " Started video recording: %s", video_filename);

    pthread_mutex_unlock(&context->recording_mutex);
}

/**
 * Stop video recording session and finalize files
 * @param context Source context
 */
void c64_stop_video_recording(struct c64_source *context)
{
    if (!context->video_file) {
        return; // Not recording
    }

    if (pthread_mutex_lock(&context->recording_mutex) != 0) {
        return;
    }

    // Close recording files and finalize formats
    if (context->video_file) {
        fclose(context->video_file);
        context->video_file = NULL;
    }
    if (context->audio_file) {
        // Update WAV header with final file size
        // recorded_audio_samples counts stereo samples, each stereo sample = 4 bytes (16-bit L + 16-bit R)
        c64_audio_finalize_wav_header(context->audio_file, context->recorded_audio_samples * 4);
        fclose(context->audio_file);
        context->audio_file = NULL;
    }

    C64_LOG_INFO("" RECORD_LOG_PREFIX " Recording stopped. Frames: %ld, Audio samples: %ld",
                 os_atomic_load_long(&context->recorded_frames), os_atomic_load_long(&context->recorded_audio_samples));

    pthread_mutex_unlock(&context->recording_mutex);
}

/**
 * Initialize recording system state
 * @param context Source context to initialize
 */
void c64_record_init(struct c64_source *context)
{
    // Initialize recording fields
    context->record_frames = false;
    context->saved_frame_count = 0;
    memset(context->save_folder, 0, sizeof(context->save_folder));
    strncpy(context->save_folder, "./recordings", sizeof(context->save_folder) - 1);

    // Initialize video recording
    context->record_video = false;
    context->record_csv = false;
    context->record_av_sync = false;
    context->video_file = NULL;
    context->audio_file = NULL;
    context->timing_file = NULL;
    context->network_file = NULL;
    context->av_sync_file = NULL;
    context->recording_start_time = 0;
    context->csv_timing_base_ns = 0;
    os_atomic_store_long(&context->recorded_frames, 0);
    os_atomic_store_long(&context->recorded_audio_samples, 0);

    // Initialize recording mutex
    if (pthread_mutex_init(&context->recording_mutex, NULL) != 0) {
        C64_LOG_ERROR("" RECORD_LOG_PREFIX " Failed to initialize recording mutex");
    }
}

/**
 * Clean up recording system resources
 * @param context Source context to clean up
 */
void c64_record_cleanup(struct c64_source *context)
{
    if (pthread_mutex_lock(&context->recording_mutex) == 0) {
        if (context->video_file) {
            fclose(context->video_file);
            context->video_file = NULL;
        }
        if (context->audio_file) {
            fclose(context->audio_file);
            context->audio_file = NULL;
        }
        if (context->timing_file) {
            fclose(context->timing_file);
            context->timing_file = NULL;
        }
        if (context->network_file) {
            fclose(context->network_file);
            context->network_file = NULL;
        }
        if (context->av_sync_file) {
            fclose(context->av_sync_file);
            context->av_sync_file = NULL;
        }
        pthread_mutex_unlock(&context->recording_mutex);
    }

    // Clean up recording mutex
    pthread_mutex_destroy(&context->recording_mutex);
}

/**
 * Update recording settings from OBS properties
 * @param context Source context
 * @param settings_ptr OBS settings data
 */
void c64_record_update_settings(struct c64_source *context, void *settings_ptr)
{
    obs_data_t *settings = (obs_data_t *)settings_ptr;

    // Update frame saving settings
    const char *new_save_folder = obs_data_get_string(settings, "save_folder");
    if (new_save_folder && strlen(new_save_folder) > 0) {
        if (strcmp(context->save_folder, new_save_folder) != 0) {
            strncpy(context->save_folder, new_save_folder, sizeof(context->save_folder) - 1);
            context->save_folder[sizeof(context->save_folder) - 1] = '\0';
            context->saved_frame_count = 0; // Reset counter for new folder
            C64_LOG_INFO("" RECORD_LOG_PREFIX " Frame save folder updated: %s", context->save_folder);
        }
    }

    // Update frame saving (check if we need session cleanup)
    bool old_record_frames = context->record_frames;
    context->record_frames = obs_data_get_bool(settings, "record_frames");
    if (old_record_frames && !context->record_frames) {
        // Frame saving stopped, cleanup session if no other recording active
        c64_session_cleanup_if_needed(context);
    }

    // Update CSV recording setting
    bool new_record_csv = obs_data_get_bool(settings, "record_csv");
    if (new_record_csv != context->record_csv) {
        context->record_csv = new_record_csv;

        if (new_record_csv) {
            // Initialize a shared timing base once so obs.csv and network.csv use a consistent epoch.
            // This avoids racy "first event wins" initialization from multiple threads.
            context->csv_timing_base_ns = os_gettime_ns();

            // Start CSV recording independently
            c64_start_obs_csv_recording(context);
            c64_start_network_csv_recording(context);
            C64_LOG_INFO("" RECORD_LOG_PREFIX " CSV recording started");
        } else {
            // Stop CSV recording
            c64_stop_obs_csv_recording(context);
            c64_stop_network_csv_recording(context);
            c64_session_cleanup_if_needed(context);
            C64_LOG_INFO("" RECORD_LOG_PREFIX " CSV recording stopped");
        }
    }

    // Update av-sync CSV recording setting
    bool new_record_av_sync = obs_data_get_bool(settings, "record_av_sync");
    if (new_record_av_sync != context->record_av_sync) {
        context->record_av_sync = new_record_av_sync;

        if (new_record_av_sync) {
            c64_start_av_sync_csv_recording(context);

            const char *host = obs_data_get_string(settings, "c64_host");
            const char *password = obs_data_get_string(settings, "c64_password");
            if (host && host[0] != '\0' && strcmp(host, "0.0.0.0") != 0) {
                char *prg_path = obs_module_file("prg/av-sync-auto.prg");
                if (prg_path) {
                    if (!c64_rest_run_prg_async(host, password, prg_path)) {
                        C64_LOG_WARNING("" RECORD_LOG_PREFIX " Failed to start av-sync PRG via REST");
                    }
                    bfree(prg_path);
                } else {
                    C64_LOG_WARNING("" RECORD_LOG_PREFIX
                                    " av-sync PRG not found in plugin data (prg/av-sync-auto.prg)");
                }
            }
        } else {
            c64_stop_av_sync_csv_recording(context);
            c64_session_cleanup_if_needed(context);

            const char *host = obs_data_get_string(settings, "c64_host");
            const char *password = obs_data_get_string(settings, "c64_password");
            if (host && host[0] != '\0' && strcmp(host, "0.0.0.0") != 0) {
                if (!c64_rest_reset_machine_async(host, password)) {
                    C64_LOG_WARNING("" RECORD_LOG_PREFIX " Failed to reset device via REST");
                }
            }
        }
    }

    // Update video recording settings
    bool new_record_video = obs_data_get_bool(settings, "record_video");
    if (new_record_video != context->record_video) {
        context->record_video = new_record_video;

        if (new_record_video) {
            // Start recording (will join/create session)
            c64_start_video_recording(context);
            C64_LOG_INFO("" RECORD_LOG_PREFIX " Video recording started");
        } else {
            // Stop recording
            c64_stop_video_recording(context);
            c64_session_cleanup_if_needed(context);
        }
    }
}
