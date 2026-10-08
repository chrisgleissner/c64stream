#include "c64-palette-follow.h"

#include "c64-color.h"
#include "c64-file.h"
#include "c64-logging.h"
#include "c64-source.h"
#include "c64-stream-control.h"
#include "c64-types.h"
#include "device/c64-device-palette.h"

#include <obs-module.h>
#include <stdio.h>
#include <string.h>
#include <util/platform.h>

// A stream that delivered palette packets this recently is the source of the
// colours; polling the device setting would only add load and could disagree.
#define C64_PALETTE_STREAM_FRESH_NS (5ULL * 1000000000ULL)
// Longest sleep while there is nothing to do; wakes come through the event.
#define C64_PALETTE_WORKER_IDLE_MS 1000
// A status that flips on every check (a flaky link) refreshes the
// Properties dialog at most this often.
#define C64_PALETTE_REFRESH_MIN_NS (1000ULL * 1000000ULL)
#define C64_PALETTE_REST_TIMEOUT_MS 1500L
#define C64_PALETTE_FTP_TIMEOUT_MS 4000L
// Every this many polls the configured file is downloaded again and compared,
// so a file overwritten under the same name (even with the same size) is
// picked up without an FTP transfer on every poll.
#define C64_PALETTE_CONTENT_RECHECK_POLLS 10
#define C64_PALETTE_MEMORY_MAX                                                                                         \
    (sizeof(((struct c64_source *)0)->palette_memory) / sizeof(((struct c64_source *)0)->palette_memory[0]))

// Ports of the device services and the cache folder; tests point these at
// fake servers and a temporary folder.
static uint16_t rest_port = 0; // 0: the REST host as configured (port 80)
static uint16_t ftp_port = 21;
static char cache_dir_override[512];

typedef struct {
    char name[C64_DEVICE_PALETTE_NAME_MAX];
    uint64_t size;
    bool have_size;
    unsigned polls_since_check;
    char applied_key[800];
    unsigned error_streak;
} follow_state_t;

// The device a check talks to, copied in one go so host, password and key
// always belong together. generation identifies the device selection: a
// result for an older generation belongs to a device the source has left.
typedef struct {
    char host[64];
    char rest_host[80];
    char password[sizeof(((struct c64_source *)0)->c64_password)];
    char device_key[64];
    long generation;
} follow_target_t;

// Caller holds palette_mutex (palette_packet_last_ns is written under it).
static bool stream_is_fresh(const struct c64_source *context, uint64_t now_ns)
{
    const uint64_t last = context->palette_packet_last_ns;
    return last && now_ns >= last && now_ns - last < C64_PALETTE_STREAM_FRESH_NS;
}

static void read_target(struct c64_source *context, follow_target_t *target)
{
    // c64_update changes the device id and the host under config_mutex and
    // bumps the generation in the same locked sections, so the copy and its
    // generation always belong together.
    pthread_mutex_lock(&context->config_mutex);
    target->generation = os_atomic_load_long(&context->palette_device_generation);
    snprintf(target->host, sizeof(target->host), "%s", context->ip_address);
    snprintf(target->password, sizeof(target->password), "%s", context->c64_password);
    snprintf(target->device_key, sizeof(target->device_key), "%s",
             context->active_device_id[0] ? context->active_device_id : context->ip_address);
    pthread_mutex_unlock(&context->config_mutex);
    if (rest_port) {
        snprintf(target->rest_host, sizeof(target->rest_host), "%s:%u", target->host, rest_port);
    } else {
        snprintf(target->rest_host, sizeof(target->rest_host), "%s", target->host);
    }
}

static bool is_current(const struct c64_source *context, long generation)
{
    return os_atomic_load_long(&context->palette_device_generation) == generation;
}

// Caller holds palette_mutex.
static void remember_locked(struct c64_source *context, const char *device_key, const uint32_t colors[16])
{
    size_t slot = context->palette_memory_count;
    for (size_t i = 0; i < context->palette_memory_count; i++) {
        if (!strcmp(context->palette_memory[i].device_key, device_key)) {
            slot = i;
            break;
        }
    }
    if (slot == C64_PALETTE_MEMORY_MAX) {
        memmove(&context->palette_memory[0], &context->palette_memory[1],
                (C64_PALETTE_MEMORY_MAX - 1) * sizeof(context->palette_memory[0]));
        slot = C64_PALETTE_MEMORY_MAX - 1;
    } else if (slot == context->palette_memory_count) {
        context->palette_memory_count++;
    }
    snprintf(context->palette_memory[slot].device_key, sizeof(context->palette_memory[slot].device_key), "%s",
             device_key);
    memcpy(context->palette_memory[slot].colors, colors, sizeof(context->palette_memory[slot].colors));
}

// Caller holds palette_mutex.
static const uint32_t *recall_locked(const struct c64_source *context, const char *device_key)
{
    for (size_t i = 0; i < context->palette_memory_count; i++) {
        if (!strcmp(context->palette_memory[i].device_key, device_key)) {
            return context->palette_memory[i].colors;
        }
    }
    return NULL;
}

// Caller holds palette_mutex.
static void show_locked(struct c64_source *context, const uint32_t colors[16])
{
    if (!context->palette_initialized) {
        c64_color_lut_init(&context->color_lut, colors);
        context->palette_initialized = true;
    } else {
        c64_color_lut_update(&context->color_lut, colors);
    }
}

// Asks OBS to redraw Properties for a changed status, at most once per
// C64_PALETTE_REFRESH_MIN_NS; a refresh held back is sent by the worker loop.
static void flush_refresh(struct c64_source *context)
{
    const uint64_t now_ns = os_gettime_ns();
    if (!os_atomic_load_bool(&context->palette_refresh_pending) ||
        now_ns - context->palette_refresh_last_ns < C64_PALETTE_REFRESH_MIN_NS) {
        return;
    }
    os_atomic_set_bool(&context->palette_refresh_pending, false);
    context->palette_refresh_last_ns = now_ns;
    c64_source_request_properties_refresh(context);
}

// Publishes the status shown in Properties, unless the device selection
// changed while the check ran.
static void set_status(struct c64_source *context, long generation, c64_palette_source_t source,
                       c64_palette_error_t error, const char *file)
{
    file = file ? file : "";
    pthread_mutex_lock(&context->palette_mutex);
    const bool current = is_current(context, generation);
    const bool changed = current && (os_atomic_load_long(&context->palette_source) != (long)source ||
                                     os_atomic_load_long(&context->palette_error) != (long)error ||
                                     strcmp(context->palette_source_file, file) != 0);
    if (changed) {
        os_atomic_set_long(&context->palette_source, (long)source);
        os_atomic_set_long(&context->palette_error, (long)error);
        snprintf(context->palette_source_file, sizeof(context->palette_source_file), "%s", file);
    }
    pthread_mutex_unlock(&context->palette_mutex);
    if (changed) {
        char text[256];
        c64_device_palette_describe(source, error, file, text, sizeof(text));
        C64_LOG_INFO("PALETTE: Follow device palette source: %s", text);
        os_atomic_set_bool(&context->palette_refresh_pending, true);
        flush_refresh(context);
    }
}

// Shows colours read from the device setting, unless the stream has taken
// over in the meantime (its packets are authoritative) or the source has
// switched to another device since the check started.
static bool apply_polled(struct c64_source *context, long generation, const char *device_key, const uint32_t colors[16])
{
    pthread_mutex_lock(&context->palette_mutex);
    const bool apply = is_current(context, generation) && os_atomic_load_bool(&context->follow_device_palette) &&
                       !stream_is_fresh(context, os_gettime_ns());
    if (apply) {
        memcpy(context->polled_palette, colors, sizeof(context->polled_palette));
        context->polled_palette_valid = true;
        if (device_key) {
            remember_locked(context, device_key, colors);
        }
        show_locked(context, colors);
        // The stream may resume with the palette generation it sent before;
        // that palette must replace these colours again.
        context->device_palette.ordering_valid = false;
    }
    pthread_mutex_unlock(&context->palette_mutex);
    return apply;
}

static bool cache_dir(char *out, size_t out_size)
{
    if (cache_dir_override[0]) {
        snprintf(out, out_size, "%s", cache_dir_override);
        os_mkdirs(out);
        return true;
    }
    char settings[512];
    if (!c64_get_user_dir(C64_USER_DIR_SETTINGS, settings, sizeof(settings))) {
        return false;
    }
    if (snprintf(out, out_size, "%s/device-palettes", settings) >= (int)out_size) {
        return false;
    }
    os_mkdirs(out);
    return true;
}

static bool read_file(const char *path, char *buffer, size_t buffer_size, size_t *length)
{
    FILE *file = os_fopen(path, "rb");
    if (!file) {
        return false;
    }
    *length = fread(buffer, 1, buffer_size, file);
    const bool ok = !ferror(file) && *length < buffer_size;
    fclose(file);
    return ok;
}

static void write_file(const char *path, const char *data, size_t length)
{
    // Write under a temporary name and rename, so an interrupted write never
    // leaves a truncated file that a later session would take as the palette.
    // Unique per writer: two sources following the same device may write the
    // same cache file at the same time.
    char temp[900];
    snprintf(temp, sizeof(temp), "%s.%llx.part", path, (unsigned long long)os_gettime_ns());
    FILE *file = os_fopen(temp, "wb");
    if (!file) {
        return;
    }
    const bool ok = fwrite(data, 1, length, file) == length;
    if (fclose(file) != 0 || !ok || os_rename(temp, path) != 0) {
        os_unlink(temp);
    }
}

// The palette setting is read over REST (with the worker's own requests, not
// the stream control client). Auto uses it even while stream control fell
// back to the control port: the setting may still be readable, and if it is
// not, the status says so. Only Force Legacy turns REST off.
static bool uses_rest(const struct c64_source *context)
{
    // An int written only by c64_update; a stale read delays the change by one check.
    return (c64_stream_transport_t)context->stream_control_transport != C64_STREAM_TRANSPORT_LEGACY;
}

static void succeed(struct c64_source *context, follow_state_t *state, long generation, c64_palette_source_t source,
                    const char *file)
{
    state->error_streak = 0;
    set_status(context, generation, source, C64_PALETTE_ERROR_NONE, file);
}

static void fail(struct c64_source *context, follow_state_t *state, long generation, c64_palette_error_t error,
                 const char *file)
{
    if (os_atomic_load_bool(&context->palette_worker_stop)) {
        return; // the transfer was cancelled by stop, not failed
    }
    state->error_streak++;
    set_status(context, generation, C64_PALETTE_SOURCE_ERROR, error, file);
}

static void poll_once(struct c64_source *context, follow_state_t *state)
{
    const uint64_t now_ns = os_gettime_ns();
    const volatile bool *cancel = &context->palette_worker_stop;
    follow_target_t target;
    read_target(context, &target);
    const long generation = target.generation;

    if (!os_atomic_load_bool(&context->follow_device_palette)) {
        state->applied_key[0] = '\0';
        succeed(context, state, generation, C64_PALETTE_SOURCE_OFF, NULL);
        return;
    }
    if (!uses_rest(context)) {
        // The device cannot be asked: show the default colours rather than a
        // palette that may no longer be true.
        if (strcmp(state->applied_key, "no-rest") != 0 &&
            apply_polled(context, generation, NULL, c64_default_palette)) {
            snprintf(state->applied_key, sizeof(state->applied_key), "no-rest");
        }
        succeed(context, state, generation, C64_PALETTE_SOURCE_NEEDS_REST, NULL);
        return;
    }
    pthread_mutex_lock(&context->palette_mutex);
    const bool stream_fresh = stream_is_fresh(context, now_ns);
    pthread_mutex_unlock(&context->palette_mutex);
    if (stream_fresh) {
        state->applied_key[0] = '\0'; // re-apply the setting if the stream stops carrying palettes
        succeed(context, state, generation, C64_PALETTE_SOURCE_STREAM, NULL);
        return;
    }
    if (!context->streaming) {
        set_status(context, generation, C64_PALETTE_SOURCE_WAITING, C64_PALETTE_ERROR_NONE, NULL);
        return;
    }

    char name[C64_DEVICE_PALETTE_NAME_MAX];
    c64_palette_error_t error = C64_PALETTE_ERROR_NONE;
    if (!c64_device_palette_fetch_setting(target.rest_host, target.password, C64_PALETTE_REST_TIMEOUT_MS, cancel, name,
                                          sizeof(name), &error)) {
        fail(context, state, generation, error, NULL);
        return;
    }
    if (!name[0]) {
        if (strcmp(state->applied_key, "builtin") != 0 &&
            apply_polled(context, generation, target.device_key, c64_default_palette)) {
            snprintf(state->applied_key, sizeof(state->applied_key), "builtin");
        }
        state->name[0] = '\0';
        succeed(context, state, generation, C64_PALETTE_SOURCE_DEVICE_DEFAULT, NULL);
        return;
    }
    if (!c64_device_palette_name_is_safe(name)) {
        fail(context, state, generation, C64_PALETTE_ERROR_BAD_NAME, name);
        return;
    }

    const bool name_changed = strcmp(name, state->name) != 0;
    const bool recheck = !name_changed && ++state->polls_since_check >= C64_PALETTE_CONTENT_RECHECK_POLLS;
    if (name_changed || !state->have_size || recheck) {
        uint64_t size = 0;
        if (!c64_device_palette_fetch_size(target.rest_host, target.password, name, C64_PALETTE_REST_TIMEOUT_MS, cancel,
                                           &size, &error)) {
            state->have_size = false;
            fail(context, state, generation, error, name);
            return;
        }
        snprintf(state->name, sizeof(state->name), "%s", name);
        state->size = size;
        state->have_size = true;
        state->polls_since_check = 0;
    }

    char dir[600];
    char path[800];
    if (!cache_dir(dir, sizeof(dir)) ||
        !c64_device_palette_cache_path(path, sizeof(path), dir, target.device_key, name, state->size)) {
        fail(context, state, generation, C64_PALETTE_ERROR_CACHE, name);
        return;
    }
    if (!strcmp(path, state->applied_key) && !recheck) {
        succeed(context, state, generation, C64_PALETTE_SOURCE_DEVICE_FILE, name);
        return; // unchanged
    }

    char data[C64_DEVICE_PALETTE_VPL_MAX + 1];
    size_t length = 0;
    uint32_t colors[16];
    // A periodic recheck always downloads; otherwise the cache is enough.
    const bool cached = !recheck && read_file(path, data, sizeof(data), &length) && length == state->size &&
                        c64_device_palette_parse_vpl(data, length, colors);
    if (!cached) {
        if (!c64_device_palette_download(target.host, ftp_port, target.password, name, C64_PALETTE_FTP_TIMEOUT_MS,
                                         cancel, data, sizeof(data), &length, &error)) {
            fail(context, state, generation, error, name);
            return;
        }
        if (!c64_device_palette_parse_vpl(data, length, colors)) {
            fail(context, state, generation, C64_PALETTE_ERROR_INVALID_FILE, name);
            return;
        }
        if (!is_current(context, generation)) {
            return; // the file belongs to a device the source has left: cache nothing under its key
        }
        char previous[C64_DEVICE_PALETTE_VPL_MAX + 1];
        size_t previous_length = 0;
        const bool same = read_file(path, previous, sizeof(previous), &previous_length) && previous_length == length &&
                          !memcmp(previous, data, length);
        if (!same) {
            write_file(path, data, length);
            C64_LOG_INFO("PALETTE: downloaded \"%s\" (%zu bytes) from %s", name, length, target.host);
        } else if (!strcmp(path, state->applied_key)) {
            succeed(context, state, generation, C64_PALETTE_SOURCE_DEVICE_FILE, name);
            return; // recheck found no change
        }
    }
    if (apply_polled(context, generation, target.device_key, colors)) {
        snprintf(state->applied_key, sizeof(state->applied_key), "%s", path);
    }
    succeed(context, state, generation, C64_PALETTE_SOURCE_DEVICE_FILE, name);
}

// Interval until the next check: the configured one, doubled per consecutive
// failure up to the maximum, so an unreachable device is not hammered.
static uint64_t next_interval_ms(const struct c64_source *context, const follow_state_t *state)
{
    uint64_t interval = c64_device_palette_clamp_interval(os_atomic_load_long(&context->palette_poll_interval_ms));
    for (unsigned i = 0; i < state->error_streak && interval < C64_DEVICE_PALETTE_POLL_MAX_MS; i++) {
        interval *= 2;
    }
    return interval > C64_DEVICE_PALETTE_POLL_MAX_MS ? C64_DEVICE_PALETTE_POLL_MAX_MS : interval;
}

static void take_reset(struct c64_source *context, follow_state_t *state)
{
    if (os_atomic_set_bool(&context->palette_worker_reset, false)) {
        memset(state, 0, sizeof(*state));
    }
}

static void *c64_palette_follow_main(void *arg)
{
    struct c64_source *context = arg;
    os_set_thread_name("c64-palette-follow");
    follow_state_t state = {0};
    uint64_t next_poll_ns = 0;
    while (!os_atomic_load_bool(&context->palette_worker_stop)) {
        const bool wake = os_atomic_set_bool(&context->palette_worker_wake, false);
        const uint64_t now_ns = os_gettime_ns();
        flush_refresh(context);
        if (!wake && now_ns < next_poll_ns) {
            uint64_t wait_ms = (next_poll_ns - now_ns + 999999ULL) / 1000000ULL;
            // A held-back refresh is due within C64_PALETTE_REFRESH_MIN_NS.
            const uint64_t max_wait_ms = os_atomic_load_bool(&context->palette_refresh_pending)
                                             ? C64_PALETTE_REFRESH_MIN_NS / 1000000ULL / 4
                                             : C64_PALETTE_WORKER_IDLE_MS;
            if (wait_ms > max_wait_ms) {
                wait_ms = max_wait_ms;
            }
            os_event_timedwait(context->palette_worker_event, (unsigned long)wait_ms);
            continue;
        }
        if (wake) {
            state.error_streak = 0; // something changed: back to the normal pace
        }
        take_reset(context, &state);
        poll_once(context, &state);
        // While Follow device is off there is nothing to check until a wake.
        const uint64_t interval_ms = os_atomic_load_bool(&context->follow_device_palette)
                                         ? next_interval_ms(context, &state)
                                         : C64_PALETTE_WORKER_IDLE_MS;
        next_poll_ns = os_gettime_ns() + interval_ms * 1000000ULL;
    }
    return NULL;
}

void c64_palette_follow_start(struct c64_source *context)
{
    if (!context || context->palette_worker_valid) {
        return;
    }
    if (!context->palette_worker_event && os_event_init(&context->palette_worker_event, OS_EVENT_TYPE_AUTO) != 0) {
        C64_LOG_WARNING("PALETTE: could not start the Follow device worker");
        return;
    }
    os_atomic_set_bool(&context->palette_worker_stop, false);
    if (pthread_create(&context->palette_worker, NULL, c64_palette_follow_main, context) == 0) {
        context->palette_worker_valid = true;
    } else {
        C64_LOG_WARNING("PALETTE: could not start the Follow device worker");
    }
}

void c64_palette_follow_stop(struct c64_source *context)
{
    if (!context || !context->palette_worker_valid) {
        return;
    }
    os_atomic_set_bool(&context->palette_worker_stop, true); // also aborts a transfer in flight
    os_event_signal(context->palette_worker_event);
    pthread_join(context->palette_worker, NULL);
    context->palette_worker_valid = false;
}

void c64_palette_follow_release(struct c64_source *context)
{
    c64_palette_follow_stop(context);
    if (context && context->palette_worker_event) {
        os_event_destroy(context->palette_worker_event);
        context->palette_worker_event = NULL;
    }
}

void c64_palette_follow_wake(struct c64_source *context)
{
    if (!context) {
        return;
    }
    os_atomic_set_bool(&context->palette_worker_wake, true);
    if (context->palette_worker_event) {
        os_event_signal(context->palette_worker_event);
    }
}

void c64_palette_follow_device_changed(struct c64_source *context, const char *device_key)
{
    if (!context) {
        return;
    }
    // Show what is known about the new device at once (its last palette this
    // session, else the default) instead of the previous device's colours.
    // Results of checks still running for the previous device are dropped.
    pthread_mutex_lock(&context->palette_mutex);
    os_atomic_inc_long(&context->palette_device_generation);
    context->palette_packet_last_ns = 0;
    const uint32_t *known = recall_locked(context, device_key ? device_key : "");
    context->polled_palette_valid = known != NULL;
    if (known) {
        memcpy(context->polled_palette, known, sizeof(context->polled_palette));
    }
    if (os_atomic_load_bool(&context->follow_device_palette) && context->palette_initialized) {
        show_locked(context, known ? known : c64_default_palette);
    }
    pthread_mutex_unlock(&context->palette_mutex);
    os_atomic_set_bool(&context->palette_worker_reset, true);
    c64_palette_follow_wake(context);
}

bool c64_palette_follow_stream_is_fresh(const struct c64_source *context)
{
    return context && stream_is_fresh(context, os_gettime_ns());
}

const char *c64_palette_follow_error_key(c64_palette_error_t error)
{
    switch (error) {
    case C64_PALETTE_ERROR_PASSWORD:
        return "DevicePaletteError.Password";
    case C64_PALETTE_ERROR_NO_SETTING:
        return "DevicePaletteError.NoSetting";
    case C64_PALETTE_ERROR_FILE_NOT_FOUND:
        return "DevicePaletteError.NotFound";
    case C64_PALETTE_ERROR_FTP_UNREACHABLE:
        return "DevicePaletteError.Ftp";
    case C64_PALETTE_ERROR_INVALID_FILE:
    case C64_PALETTE_ERROR_BAD_NAME:
        return "DevicePaletteError.Invalid";
    case C64_PALETTE_ERROR_CACHE:
        return "DevicePaletteError.Cache";
    case C64_PALETTE_ERROR_UNREACHABLE:
    case C64_PALETTE_ERROR_NONE:
    default:
        return "DevicePaletteError.Unreachable";
    }
}

const char *c64_palette_follow_source_key(c64_palette_source_t source, c64_palette_error_t error)
{
    switch (source) {
    case C64_PALETTE_SOURCE_STREAM:
        return "DevicePaletteSource.Stream";
    case C64_PALETTE_SOURCE_DEVICE_FILE:
        return "DevicePaletteSource.File";
    case C64_PALETTE_SOURCE_DEVICE_DEFAULT:
        return "DevicePaletteSource.BuiltIn";
    case C64_PALETTE_SOURCE_NEEDS_REST:
        return "DevicePaletteSource.NeedsRest";
    case C64_PALETTE_SOURCE_ERROR:
        return c64_palette_follow_error_key(error);
    case C64_PALETTE_SOURCE_WAITING:
    case C64_PALETTE_SOURCE_OFF:
    default:
        return "DevicePaletteSource.Waiting";
    }
}

void c64_palette_follow_describe(struct c64_source *context, char *out, size_t out_size)
{
    if (!out || !out_size) {
        return;
    }
    char file[sizeof(((struct c64_source *)0)->palette_source_file)] = {0};
    c64_palette_source_t source = C64_PALETTE_SOURCE_WAITING;
    c64_palette_error_t error = C64_PALETTE_ERROR_NONE;
    if (context) {
        pthread_mutex_lock(&context->palette_mutex);
        source = (c64_palette_source_t)os_atomic_load_long(&context->palette_source);
        error = (c64_palette_error_t)os_atomic_load_long(&context->palette_error);
        snprintf(file, sizeof(file), "%s", context->palette_source_file);
        pthread_mutex_unlock(&context->palette_mutex);
    }
    // Translations carry at most one "%s", the palette file name.
    const char *text = obs_module_text(c64_palette_follow_source_key(source, error));
    const char *placeholder = strstr(text, "%s");
    if (placeholder && !strstr(placeholder + 2, "%")) {
        snprintf(out, out_size, "%.*s%s%s", (int)(placeholder - text), text, file, placeholder + 2);
    } else {
        snprintf(out, out_size, "%s", text);
    }
}

void c64_palette_follow_note_stream_packet(struct c64_source *context)
{
    if (!context) {
        return;
    }
    const uint64_t now_ns = os_gettime_ns();
    const bool first = !stream_is_fresh(context, now_ns);
    context->palette_packet_last_ns = now_ns;
    if (first) {
        c64_palette_follow_wake(context); // show "video stream" promptly
    }
}

void c64_palette_follow_set_ports_for_test(uint16_t test_rest_port, uint16_t test_ftp_port)
{
    rest_port = test_rest_port;
    ftp_port = test_ftp_port ? test_ftp_port : 21;
}

void c64_palette_follow_set_cache_dir_for_test(const char *dir)
{
    snprintf(cache_dir_override, sizeof(cache_dir_override), "%s", dir ? dir : "");
}

void c64_palette_follow_poll_for_test(struct c64_source *context)
{
    static follow_state_t state;
    take_reset(context, &state);
    poll_once(context, &state);
}
