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
#include <math.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
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

static const char *C64_AUDIO_MIXER_CATEGORY = "Audio Mixer";
static const char *C64_AUDIO_MIXER_ZERO_DB_1 = " 0 dB";
static const char *C64_AUDIO_MIXER_ZERO_DB_2 = "0 dB";

static void c64_audio_mixer_snapshot_clear(struct c64_source *context)
{
    if (!context) {
        return;
    }

    if (context->audio_mixer_snapshot_items || context->audio_mixer_snapshot_values) {
        for (size_t i = 0; i < context->audio_mixer_snapshot_count; i++) {
            free(context->audio_mixer_snapshot_items ? context->audio_mixer_snapshot_items[i] : NULL);
            free(context->audio_mixer_snapshot_values ? context->audio_mixer_snapshot_values[i] : NULL);
        }
    }

    free(context->audio_mixer_snapshot_items);
    free(context->audio_mixer_snapshot_values);
    context->audio_mixer_snapshot_items = NULL;
    context->audio_mixer_snapshot_values = NULL;
    context->audio_mixer_snapshot_count = 0;
    context->audio_mixer_snapshot_active = false;
}

static const char *c64_audio_mixer_find_zero_db(char **options, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (!options[i]) {
            continue;
        }
        if (strcmp(options[i], C64_AUDIO_MIXER_ZERO_DB_1) == 0) {
            return C64_AUDIO_MIXER_ZERO_DB_1;
        }
        if (strcmp(options[i], C64_AUDIO_MIXER_ZERO_DB_2) == 0) {
            return C64_AUDIO_MIXER_ZERO_DB_2;
        }
    }
    return NULL;
}

static bool c64_audio_mixer_snapshot_apply_zero_db(struct c64_source *context)
{
    if (!context || !context->rest_client) {
        return false;
    }

    if (context->audio_mixer_snapshot_active) {
        return true;
    }

    char **items = NULL;
    size_t item_count = 0;
    if (!c64_rest_config_list(context->rest_client, C64_AUDIO_MIXER_CATEGORY, &items, &item_count)) {
        C64_LOG_WARNING("" RECORD_LOG_PREFIX " Failed to list Audio Mixer items: %s",
                        c64_rest_get_error(context->rest_client));
        return false;
    }

    context->audio_mixer_snapshot_items = calloc(item_count, sizeof(char *));
    context->audio_mixer_snapshot_values = calloc(item_count, sizeof(char *));
    if (!context->audio_mixer_snapshot_items || !context->audio_mixer_snapshot_values) {
        C64_LOG_WARNING("" RECORD_LOG_PREFIX " Failed to allocate Audio Mixer snapshot storage");
        c64_rest_string_list_free(items, item_count);
        c64_audio_mixer_snapshot_clear(context);
        return false;
    }

    size_t stored = 0;
    for (size_t i = 0; i < item_count; i++) {
        if (!items[i] || items[i][0] == '\0') {
            continue;
        }

        char **options = NULL;
        size_t option_count = 0;
        if (!c64_rest_config_list_options(context->rest_client, C64_AUDIO_MIXER_CATEGORY, items[i], &options,
                                          &option_count)) {
            continue;
        }

        const char *zero_db = c64_audio_mixer_find_zero_db(options, option_count);
        if (!zero_db) {
            c64_rest_string_list_free(options, option_count);
            continue;
        }

        char current_value[128] = {0};
        if (!c64_rest_config_get_value(context->rest_client, C64_AUDIO_MIXER_CATEGORY, items[i], current_value,
                                       sizeof(current_value))) {
            c64_rest_string_list_free(options, option_count);
            continue;
        }

        context->audio_mixer_snapshot_items[stored] = strdup(items[i]);
        context->audio_mixer_snapshot_values[stored] = strdup(current_value);
        if (!context->audio_mixer_snapshot_items[stored] || !context->audio_mixer_snapshot_values[stored]) {
            c64_rest_string_list_free(options, option_count);
            c64_rest_string_list_free(items, item_count);
            c64_audio_mixer_snapshot_clear(context);
            return false;
        }

        stored++;

        if (strcmp(current_value, zero_db) != 0) {
            if (!c64_rest_config_set_value(context->rest_client, C64_AUDIO_MIXER_CATEGORY, items[i], zero_db)) {
                C64_LOG_WARNING("" RECORD_LOG_PREFIX " Failed to set %s/%s to %s: %s", C64_AUDIO_MIXER_CATEGORY,
                                items[i], zero_db, c64_rest_get_error(context->rest_client));
            }
        }

        c64_rest_string_list_free(options, option_count);
    }

    c64_rest_string_list_free(items, item_count);

    context->audio_mixer_snapshot_count = stored;
    context->audio_mixer_snapshot_active = (stored > 0);
    if (context->audio_mixer_snapshot_active) {
        C64_LOG_INFO("" RECORD_LOG_PREFIX " Audio Mixer volumes forced to 0 dB (%zu entries)", stored);
    }

    if (!context->audio_mixer_snapshot_active) {
        c64_audio_mixer_snapshot_clear(context);
    }

    return context->audio_mixer_snapshot_active;
}

static void c64_audio_mixer_snapshot_restore(struct c64_source *context)
{
    if (!context) {
        return;
    }

    if (!context->audio_mixer_snapshot_active) {
        c64_audio_mixer_snapshot_clear(context);
        return;
    }

    if (!context->rest_client) {
        c64_audio_mixer_snapshot_clear(context);
        return;
    }

    for (size_t i = 0; i < context->audio_mixer_snapshot_count; i++) {
        const char *item = context->audio_mixer_snapshot_items[i];
        const char *value = context->audio_mixer_snapshot_values[i];
        if (!item || !value) {
            continue;
        }
        if (!c64_rest_config_set_value(context->rest_client, C64_AUDIO_MIXER_CATEGORY, item, value)) {
            C64_LOG_WARNING("" RECORD_LOG_PREFIX " Failed to restore %s/%s to %s: %s", C64_AUDIO_MIXER_CATEGORY, item,
                            value, c64_rest_get_error(context->rest_client));
        }
    }

    C64_LOG_INFO("" RECORD_LOG_PREFIX " Audio Mixer volumes restored");
    c64_audio_mixer_snapshot_clear(context);
}

void c64_record_on_rest_client_ready(struct c64_source *context)
{
    if (!context) {
        return;
    }

    if (context->record_av_sync && !context->audio_mixer_snapshot_active) {
        if (!c64_audio_mixer_snapshot_apply_zero_db(context)) {
            C64_LOG_WARNING("" RECORD_LOG_PREFIX " Audio Mixer 0 dB enforcement skipped or failed");
        }
    }
}

static bool c64_rest_read_file_to_buffer(const char *path, uint8_t **out_data, size_t *out_size)
{
    if (!path || !out_data || !out_size) {
        return false;
    }

    *out_data = NULL;
    *out_size = 0;

    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }

    const long szl = ftell(f);
    if (szl <= 0 || (unsigned long)szl > SIZE_MAX) {
        fclose(f);
        return false;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }

    const size_t sz = (size_t)szl;
    uint8_t *data = (uint8_t *)malloc(sz);
    if (!data) {
        fclose(f);
        return false;
    }

    const size_t n = fread(data, 1, sz, f);
    fclose(f);

    if (n != sz) {
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
        C64_LOG_ERROR("REST: thread received NULL job");
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
        } else {
            C64_LOG_WARNING("REST: failed to read PRG file: %s", job->prg_path);
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
        C64_LOG_ERROR("REST: c64_rest_launch_job called with NULL job");
        return false;
    }

    pthread_t t;
    int err = pthread_create(&t, NULL, c64_rest_thread_main, job);
    if (err != 0) {
        C64_LOG_ERROR("REST: pthread_create failed with error %d", err);
        free(job);
        return false;
    }
    int detach_err = pthread_detach(t);
    if (detach_err != 0) {
        C64_LOG_ERROR("REST: pthread_detach failed with error %d", detach_err);
        // Thread is already running, can't safely clean up job
        return false;
    }
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
    if (!context || pthread_mutex_lock(&context->recording_mutex) != 0) {
        return;
    }

    // If session already exists, do nothing
    if (context->session_folder[0] != '\0') {
        pthread_mutex_unlock(&context->recording_mutex);
        return;
    }

    // Create new session folder with timestamp
    time_t rawtime = time(NULL);
    if (rawtime == (time_t)-1) {
        C64_LOG_ERROR("" RECORD_LOG_PREFIX " time() failed");
        pthread_mutex_unlock(&context->recording_mutex);
        return;
    }

    struct tm timeinfo_buf;
    struct tm *timeinfo;

#ifdef _WIN32
    // Windows: use localtime_s (thread-safe)
    if (localtime_s(&timeinfo_buf, &rawtime) != 0) {
        C64_LOG_ERROR("" RECORD_LOG_PREFIX " localtime_s failed on Windows");
        pthread_mutex_unlock(&context->recording_mutex);
        return;
    }
    timeinfo = &timeinfo_buf;
#else
    // POSIX: use localtime_r (thread-safe)
    timeinfo = localtime_r(&rawtime, &timeinfo_buf);
    if (!timeinfo) {
        C64_LOG_ERROR("" RECORD_LOG_PREFIX " localtime_r failed");
        pthread_mutex_unlock(&context->recording_mutex);
        return;
    }
#endif

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

    pthread_mutex_unlock(&context->recording_mutex);
}

/**
 * Check if any recording type is currently active
 * @param context Source context
 * @return true if frame saving, video recording, or CSV recording is enabled
 */
bool c64_session_any_recording_active(struct c64_source *context)
{
    if (!context) {
        return false;
    }

    bool record_frames = false;
    bool record_video = false;
    bool record_csv = false;
    bool record_av_sync = false;
    if (pthread_mutex_lock(&context->recording_mutex) == 0) {
        record_frames = context->record_frames;
        record_video = context->record_video;
        record_csv = context->record_csv;
        record_av_sync = context->record_av_sync;
        pthread_mutex_unlock(&context->recording_mutex);
    }

    return record_frames || record_video || record_csv || record_av_sync;
}

/**
 * Stop CSV timing recording
 * @param context Source context
 */
void c64_stop_obs_csv_recording(struct c64_source *context)
{
    if (!context || pthread_mutex_lock(&context->recording_mutex) != 0) {
        return;
    }
    if (context->timing_file) {
        fclose(context->timing_file);
        context->timing_file = NULL;
        context->csv_timing_base_ns = 0; // Reset timing base for next recording session
        C64_LOG_INFO("" RECORD_LOG_PREFIX " CSV timing recording stopped");
    }
    pthread_mutex_unlock(&context->recording_mutex);
}

void c64_stop_av_sync_csv_recording(struct c64_source *context)
{
    FILE *f = NULL;

    if (pthread_mutex_lock(&context->recording_mutex) == 0) {
        f = context->av_sync_file;
        context->av_sync_file = NULL;
        pthread_mutex_unlock(&context->recording_mutex);
    }

    if (f) {
        fclose(f);
        C64_LOG_INFO("" RECORD_LOG_PREFIX " av-sync CSV recording stopped");
    }
}

/**
 * Stop network packet recording
 * @param context Source context
 */
void c64_stop_network_csv_recording(struct c64_source *context)
{
    if (!context || pthread_mutex_lock(&context->recording_mutex) != 0) {
        return;
    }
    if (context->network_file) {
        fclose(context->network_file);
        context->network_file = NULL;
        C64_LOG_INFO("" RECORD_LOG_PREFIX " Network packet recording stopped");
    }
    pthread_mutex_unlock(&context->recording_mutex);
}

/**
 * Clean up session if no recording is active
 * @param context Source context
 */
void c64_session_cleanup_if_needed(struct c64_source *context)
{
    if (!c64_session_any_recording_active(context)) {
        pthread_mutex_lock(&context->recording_mutex);
        // Stop all recording when session ends
        c64_stop_obs_csv_recording(context);
        c64_stop_network_csv_recording(context);
        c64_stop_av_sync_csv_recording(context);
        context->session_folder[0] = '\0';
        context->csv_debug_enabled = false;
        C64_LOG_INFO("" RECORD_LOG_PREFIX " Recording session ended");
        pthread_mutex_unlock(&context->recording_mutex);
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
    if (!context || pthread_mutex_lock(&context->recording_mutex) != 0) {
        return;
    }
    if (context->timing_file) {
        pthread_mutex_unlock(&context->recording_mutex);
        return; // Already recording CSV
    }

    // Ensure we have a recording session
    C64_LOG_INFO("" RECORD_LOG_PREFIX " Creating CSV recording session...");
    c64_session_ensure_exists(context);
    if (context->session_folder[0] == '\0') {
        C64_LOG_WARNING("" RECORD_LOG_PREFIX " Failed to create recording session for CSV logging");
        pthread_mutex_unlock(&context->recording_mutex);
        return;
    }
    C64_LOG_INFO("" RECORD_LOG_PREFIX " Session folder created: %s", context->session_folder);

    // Create CSV timing file
    char timing_filename[950];
    snprintf(timing_filename, sizeof(timing_filename), "%s/obs.csv", context->session_folder);

    context->timing_file = fopen(timing_filename, "w");
    if (!context->timing_file) {
        C64_LOG_ERROR("" RECORD_LOG_PREFIX " Failed to create CSV timing file: %s", timing_filename);
        pthread_mutex_unlock(&context->recording_mutex);
        return;
    }

    context->csv_debug_enabled = c64_debug_logging;

    // Write CSV header
    c64_obs_write_header(context);
    C64_LOG_INFO("" RECORD_LOG_PREFIX " Started CSV timing recording: %s", timing_filename);
    pthread_mutex_unlock(&context->recording_mutex);
}

void c64_start_av_sync_csv_recording(struct c64_source *context)
{
    if (!context || pthread_mutex_lock(&context->recording_mutex) != 0) {
        return;
    }
    if (context->av_sync_file) {
        pthread_mutex_unlock(&context->recording_mutex);
        return;
    }

    c64_session_ensure_exists(context);
    if (context->session_folder[0] == '\0') {
        C64_LOG_WARNING("" RECORD_LOG_PREFIX " Failed to create recording session for av-sync CSV");
        pthread_mutex_unlock(&context->recording_mutex);
        return;
    }

    char filename[950];
    snprintf(filename, sizeof(filename), "%s/av-sync.csv", context->session_folder);

    FILE *f = fopen(filename, "w");
    if (!f) {
        C64_LOG_ERROR("" RECORD_LOG_PREFIX " Failed to create av-sync CSV file: %s (errno=%d)", filename, errno);
        pthread_mutex_unlock(&context->recording_mutex);
        return;
    }

    setvbuf(f, NULL, _IOLBF, 4096);

    fprintf(f,
            "trigger,detected,obs_offset_ms,obs_video_seq,obs_audio_seq,obs_video_frame,obs_video_ts_ns,obs_audio_ts_"
            "ns,has_network_match,net_offset_ms,net_video_seq,net_audio_seq,net_video_frame,net_video_ts_ns,net_audio_"
            "ts_ns,net_to_obs_video_ms,net_to_obs_audio_ms\n");

    fflush(f);

    context->av_sync_file = f;
    f = NULL;
    pthread_mutex_unlock(&context->recording_mutex);

    if (f) {
        fclose(f);
        return;
    }

    C64_LOG_INFO("" RECORD_LOG_PREFIX " Started av-sync CSV recording: %s", filename);
}

/**
 * Start network packet recording for network analysis
 * @param context Source context
 */
void c64_start_network_csv_recording(struct c64_source *context)
{
    if (!context || pthread_mutex_lock(&context->recording_mutex) != 0) {
        return;
    }
    if (context->network_file) {
        pthread_mutex_unlock(&context->recording_mutex);
        return; // Already recording network packets
    }

    // Ensure we have a recording session
    C64_LOG_DEBUG("" RECORD_LOG_PREFIX " Creating network recording session...");
    c64_session_ensure_exists(context);
    if (context->session_folder[0] == '\0') {
        C64_LOG_WARNING("" RECORD_LOG_PREFIX " Failed to create recording session for network logging");
        pthread_mutex_unlock(&context->recording_mutex);
        return;
    }
    C64_LOG_DEBUG("" RECORD_LOG_PREFIX " Network session folder: %s", context->session_folder);

    // Create network packet file
    char network_filename[950];
    snprintf(network_filename, sizeof(network_filename), "%s/network.csv", context->session_folder);

    context->network_file = fopen(network_filename, "w");
    if (!context->network_file) {
        C64_LOG_ERROR("" RECORD_LOG_PREFIX " Failed to create network packet file: %s", network_filename);
        pthread_mutex_unlock(&context->recording_mutex);
        return;
    }

    // Optimize for high throughput: 4MB buffer to minimize syscalls
    // This allows the consumer thread to write to memory and stay ahead of the ring buffer
    setvbuf(context->network_file, NULL, _IOFBF, 4 * 1024 * 1024);

    context->csv_debug_enabled = c64_debug_logging;

    // Write network CSV header
    c64_network_write_header(context);
    C64_LOG_INFO("" RECORD_LOG_PREFIX " Started network packet recording: %s", network_filename);
    pthread_mutex_unlock(&context->recording_mutex);
}

/**
 * Start video recording session (AVI + WAV + CSV)
 * @param context Source context
 */
bool c64_start_video_recording(struct c64_source *context)
{
    if (!context->record_video || context->video_file) {
        return context->video_file != NULL;
    }

    if (pthread_mutex_lock(&context->recording_mutex) != 0) {
        return false;
    }

    // Ensure session exists for video recording
    c64_session_ensure_exists(context);
    if (context->session_folder[0] == '\0') {
        C64_LOG_ERROR("" RECORD_LOG_PREFIX " Failed to create recording session for video recording");
        pthread_mutex_unlock(&context->recording_mutex);
        return false;
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
        return false;
    }

    uint64_t timestamp_ms = os_gettime_ns() / 1000000;
    context->recording_start_time = timestamp_ms;
    os_atomic_store_long(&context->recorded_frames, 0);
    os_atomic_store_long(&context->recorded_audio_samples, 0);
    context->recorded_audio_bytes = 0;
    context->avi_segment_index = 0;
    context->avi_segment_width = 0;
    context->avi_segment_height = 0;
    context->avi_segment_fps = 0.0;
    context->avi_segment_frames = 0;
    context->avi_segment_bytes = 0;

    // Write AVI header with detected frame rate
    c64_video_write_avi_header(context->video_file, context->width, context->height, context->expected_fps);
    context->avi_segment_index = 0;
    context->avi_segment_width = context->width;
    context->avi_segment_height = context->height;
    context->avi_segment_fps = context->expected_fps;
    context->avi_segment_frames = 0;
    context->avi_segment_bytes = 216; /* fixed header size written above */

    // Write WAV header to audio file
    c64_audio_write_wav_header(context->audio_file, (uint32_t)llround(context->audio_sample_rate), 2, 16);

    C64_LOG_INFO("" RECORD_LOG_PREFIX " Started video recording: %s", video_filename);

    pthread_mutex_unlock(&context->recording_mutex);
    return true;
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
        c64_video_stop_recording(context);
    }
    if (context->audio_file) {
        // Update WAV header with final file size
        // recorded_audio_samples counts stereo samples, each stereo sample = 4 bytes (16-bit L + 16-bit R)
        c64_audio_finalize_wav_header(context->audio_file, context->recorded_audio_bytes);
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
    context->av_sync_prg_started = false;
    context->video_file = NULL;
    context->audio_file = NULL;
    context->timing_file = NULL;
    context->network_file = NULL;
    context->av_sync_file = NULL;
    context->recording_start_time = 0;
    context->csv_timing_base_ns = 0;
    os_atomic_store_long(&context->recorded_frames, 0);
    os_atomic_store_long(&context->recorded_audio_samples, 0);
    context->recorded_audio_bytes = 0;
    context->avi_segment_index = 0;
    context->avi_segment_width = 0;
    context->avi_segment_height = 0;
    context->avi_segment_fps = 0.0;
    context->avi_segment_frames = 0;
    context->avi_segment_bytes = 0;

    context->audio_mixer_snapshot_items = NULL;
    context->audio_mixer_snapshot_values = NULL;
    context->audio_mixer_snapshot_count = 0;
    context->audio_mixer_snapshot_active = false;

    // Recursive locking keeps the session/starters atomic even when video recording
    // invokes them while it already owns this mutex.
    pthread_mutexattr_t recording_mutex_attr;
    pthread_mutexattr_init(&recording_mutex_attr);
    pthread_mutexattr_settype(&recording_mutex_attr, PTHREAD_MUTEX_RECURSIVE);
    if (pthread_mutex_init(&context->recording_mutex, &recording_mutex_attr) != 0) {
        C64_LOG_ERROR("" RECORD_LOG_PREFIX " Failed to initialize recording mutex");
    }
    pthread_mutexattr_destroy(&recording_mutex_attr);
}

/**
 * Clean up recording system resources
 * @param context Source context to clean up
 */
void c64_record_cleanup(struct c64_source *context)
{
    c64_audio_mixer_snapshot_restore(context);

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
    const bool new_record_av_sync = obs_data_get_bool(settings, "record_av_sync");
    bool old_record_av_sync = false;
    if (pthread_mutex_lock(&context->recording_mutex) == 0) {
        old_record_av_sync = context->record_av_sync;
        pthread_mutex_unlock(&context->recording_mutex);
    }

    if (new_record_av_sync != old_record_av_sync) {
        C64_LOG_INFO("" RECORD_LOG_PREFIX " record_av_sync changed from %d to %d", old_record_av_sync,
                     new_record_av_sync);
        if (new_record_av_sync) {
            if (!c64_audio_mixer_snapshot_apply_zero_db(context)) {
                C64_LOG_WARNING("" RECORD_LOG_PREFIX " Audio Mixer 0 dB enforcement skipped or failed");
            }
            c64_start_av_sync_csv_recording(context);

            // Only enable recording after the file is created, to avoid FILE* races in background loggers.
            pthread_mutex_lock(&context->recording_mutex);
            context->record_av_sync = (context->av_sync_file != NULL);
            pthread_mutex_unlock(&context->recording_mutex);

            const char *host = obs_data_get_string(settings, "c64_host");
            const char *password = obs_data_get_string(settings, "c64_password");

            if (host && host[0] != '\0' && strcmp(host, "0.0.0.0") != 0) {
                char *prg_path = obs_module_file("prg/av-sync-auto.prg");
                if (prg_path) {
                    if (!c64_rest_run_prg_async(host, password, prg_path)) {
                        C64_LOG_WARNING("" RECORD_LOG_PREFIX " Failed to start av-sync PRG via REST");
                    } else {
                        context->av_sync_prg_started = true;
                        C64_LOG_INFO("" RECORD_LOG_PREFIX " av-sync PRG async call initiated");
                    }
                    bfree(prg_path);
                } else {
                    C64_LOG_WARNING("" RECORD_LOG_PREFIX
                                    " av-sync PRG not found in plugin data (prg/av-sync-auto.prg)");
                }
            }
        } else {
            c64_audio_mixer_snapshot_restore(context);
            // Disable first so background threads won't attempt to write while we close.
            pthread_mutex_lock(&context->recording_mutex);
            context->record_av_sync = false;
            pthread_mutex_unlock(&context->recording_mutex);
            c64_stop_av_sync_csv_recording(context);
            c64_session_cleanup_if_needed(context);

            const char *host = obs_data_get_string(settings, "c64_host");
            const char *password = obs_data_get_string(settings, "c64_password");
            if (context->av_sync_prg_started && host && host[0] != '\0' && strcmp(host, "0.0.0.0") != 0) {
                if (!c64_rest_reset_machine_async(host, password)) {
                    C64_LOG_WARNING("" RECORD_LOG_PREFIX " Failed to reset device via REST");
                } else {
                    C64_LOG_INFO("" RECORD_LOG_PREFIX " Device reset async call initiated");
                }
            }
            context->av_sync_prg_started = false;
        }
    }

    // Update video recording settings
    bool new_record_video = obs_data_get_bool(settings, "record_video");
    if (new_record_video != context->record_video) {
        context->record_video = new_record_video;

        if (new_record_video) {
            // Start recording (will join/create session)
            if (c64_start_video_recording(context)) {
                C64_LOG_INFO("" RECORD_LOG_PREFIX " Video recording started");
            } else {
                context->record_video = false;
                obs_data_set_bool(settings, "record_video", false);
                C64_LOG_ERROR("" RECORD_LOG_PREFIX
                              " Video recording was not enabled because its files could not be opened");
                c64_session_cleanup_if_needed(context);
            }
        } else {
            // Stop recording
            c64_stop_video_recording(context);
            c64_session_cleanup_if_needed(context);
        }
    }
}
