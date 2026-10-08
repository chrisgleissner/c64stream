/* Follow device worker (c64-palette-follow.c) against an in-process fake
 * Ultimate: what the source shows and reports for every situation the worker
 * decides between, how often it talks to the device, and that the stream's
 * palette always wins. Each check runs one synchronous poll, so nothing here
 * depends on timing. Runs in CI on Linux, macOS and Windows. */

#include "c64-palette-follow.h"
#include "c64-color.h"
#include "c64-stream-control.h"
#include "c64-types.h"
#include "fake_ultimate.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <process.h>
#else
#include <signal.h>
#include <unistd.h>
#endif

// A failed check ends the run: a test that returned early would leave its
// worker thread and fake device running into the next test.
#define CHECK(expr)                                                                                                    \
    do {                                                                                                               \
        if (!(expr)) {                                                                                                 \
            fprintf(stderr, "CHECK failed: %s (%s:%d)\nFAIL\n", #expr, __FILE__, __LINE__);                           \
            fflush(NULL);                                                                                              \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)

bool c64_debug_logging = false;
static volatile long properties_refreshes;

// Provided by the plugin; the worker only uses these two.
void c64_source_request_properties_refresh(struct c64_source *context)
{
    (void)context;
    os_atomic_inc_long(&properties_refreshes);
}
const char *obs_module_text(const char *key)
{
    // Echo the key with a placeholder, so tests can see which text was chosen.
    static char text[128];
    snprintf(text, sizeof(text), "%s|%%s", key);
    return text;
}

static char *vpl_with_colour_14(unsigned rgb)
{
    static char text[4][600];
    static int next;
    char *out = text[next++ % 4];
    int n = snprintf(out, 600, "# test\n");
    for (int i = 0; i < 16; i++) {
        const unsigned c = i == 14 ? rgb : (unsigned)(i * 0x101010);
        n += snprintf(out + n, 600 - n, "%02X %02X %02X\n", (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
    }
    return out;
}

static uint32_t bgra(unsigned rgb)
{
    return 0xFF000000u | ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF);
}

typedef struct {
    struct c64_source *context;
    fake_ultimate_t fake;
} rig_t;

// Every rig gets its own empty palette cache folder under this root.
static char cache_root[400];
static int rig_count;

static bool rig_start(rig_t *rig, const char *password)
{
    memset(rig, 0, sizeof(*rig));
    rig->fake.has_setting = true;
    rig->fake.ftp_enabled = true;
    snprintf(rig->fake.password, sizeof(rig->fake.password), "%s", password ? password : "");
    if (!fake_start(&rig->fake)) {
        return false;
    }
    c64_palette_follow_set_ports_for_test(rig->fake.http_port, rig->fake.ftp_port);
    char cache[512];
    snprintf(cache, sizeof(cache), "%s/rig-%d", cache_root, ++rig_count);
    c64_palette_follow_set_cache_dir_for_test(cache);
    struct c64_source *context = calloc(1, sizeof(*context));
    pthread_mutex_init(&context->palette_mutex, NULL);
    pthread_mutex_init(&context->config_mutex, NULL);
    context->follow_device_palette = true;
    context->stream_control_transport = C64_STREAM_TRANSPORT_AUTO;
    context->streaming = true;
    snprintf(context->ip_address, sizeof(context->ip_address), "127.0.0.1");
    snprintf(context->active_device_id, sizeof(context->active_device_id), "device-a");
    snprintf(context->c64_password, sizeof(context->c64_password), "%s", password ? password : "");
    c64_color_lut_init(&context->color_lut, c64_default_palette);
    context->palette_initialized = true;
    os_atomic_set_bool(&context->palette_worker_reset, true);
    rig->context = context;
    return true;
}

static void rig_stop(rig_t *rig)
{
    c64_palette_follow_release(rig->context);
    fake_stop(&rig->fake);
    pthread_mutex_destroy(&rig->context->palette_mutex);
    pthread_mutex_destroy(&rig->context->config_mutex);
    free(rig->context);
}

static void poll_now(rig_t *rig)
{
    c64_palette_follow_poll_for_test(rig->context);
}

// The colour the source renders for VIC colour index.
static uint32_t shown(rig_t *rig, int index)
{
    pthread_mutex_lock(&rig->context->palette_mutex);
    const uint32_t color = rig->context->color_lut.palette[index];
    pthread_mutex_unlock(&rig->context->palette_mutex);
    return color;
}

static c64_palette_source_t source(rig_t *rig)
{
    return (c64_palette_source_t)os_atomic_load_long(&rig->context->palette_source);
}

static bool stream_fresh(rig_t *rig)
{
    pthread_mutex_lock(&rig->context->palette_mutex);
    const bool fresh = c64_palette_follow_stream_is_fresh(rig->context);
    pthread_mutex_unlock(&rig->context->palette_mutex);
    return fresh;
}

static bool test_builtin_then_file_then_change(void)
{
    rig_t rig;
    CHECK(rig_start(&rig, NULL));
    fake_put_file(&rig.fake, "red.vpl", vpl_with_colour_14(0xFF0000));
    fake_put_file(&rig.fake, "green.vpl", vpl_with_colour_14(0x00FF00));

    // Empty setting: the firmware's built-in palette.
    poll_now(&rig);
    CHECK(source(&rig) == C64_PALETTE_SOURCE_DEVICE_DEFAULT);
    CHECK(shown(&rig, 14) == c64_default_palette[14]);

    // A palette file: downloaded once and applied.
    fake_set_setting(&rig.fake, "red.vpl");
    poll_now(&rig);
    CHECK(source(&rig) == C64_PALETTE_SOURCE_DEVICE_FILE);
    CHECK(!strcmp(rig.context->palette_source_file, "red.vpl"));
    CHECK(shown(&rig, 14) == bgra(0xFF0000));
    CHECK(FAKE_COUNT(&rig.fake, ftp_retrs) == 1);

    // Unchanged: one small REST request per poll, no FTP.
    const long settings_before = FAKE_COUNT(&rig.fake, setting_requests);
    for (int i = 0; i < 5; i++) {
        poll_now(&rig);
    }
    CHECK(FAKE_COUNT(&rig.fake, ftp_retrs) == 1);
    CHECK(FAKE_COUNT(&rig.fake, setting_requests) - settings_before == 5);
    CHECK(shown(&rig, 14) == bgra(0xFF0000));

    // Selecting another file on the device.
    fake_set_setting(&rig.fake, "green.vpl");
    poll_now(&rig);
    CHECK(shown(&rig, 14) == bgra(0x00FF00));
    CHECK(FAKE_COUNT(&rig.fake, ftp_retrs) == 2);

    // Back to the first file: served from the cache, no download.
    fake_set_setting(&rig.fake, "red.vpl");
    poll_now(&rig);
    CHECK(shown(&rig, 14) == bgra(0xFF0000));
    CHECK(FAKE_COUNT(&rig.fake, ftp_retrs) == 2);

    // And back to the built-in palette.
    fake_set_setting(&rig.fake, "");
    poll_now(&rig);
    CHECK(source(&rig) == C64_PALETTE_SOURCE_DEVICE_DEFAULT);
    CHECK(shown(&rig, 14) == c64_default_palette[14]);
    rig_stop(&rig);
    return true;
}

static bool test_overwritten_file_same_size_is_picked_up(void)
{
    rig_t rig;
    CHECK(rig_start(&rig, NULL));
    fake_put_file(&rig.fake, "p.vpl", vpl_with_colour_14(0xFF0000));
    fake_set_setting(&rig.fake, "p.vpl");
    poll_now(&rig);
    CHECK(shown(&rig, 14) == bgra(0xFF0000));
    // Same name, same length, different colour.
    fake_put_file(&rig.fake, "p.vpl", vpl_with_colour_14(0x0000FF));
    bool seen = false;
    for (int i = 0; i < 12 && !seen; i++) {
        poll_now(&rig);
        seen = shown(&rig, 14) == bgra(0x0000FF);
    }
    CHECK(seen);
    rig_stop(&rig);
    return true;
}

// A file replaced on the device under the same name and size, while the
// setting pointed elsewhere, is shown with its new content as soon as the
// setting selects it again (cached copy first, then verified by download).
static bool test_reselected_file_is_verified(void)
{
    rig_t rig;
    CHECK(rig_start(&rig, NULL));
    fake_put_file(&rig.fake, "a.vpl", vpl_with_colour_14(0xFF0000));
    fake_put_file(&rig.fake, "b.vpl", vpl_with_colour_14(0x00FF00));
    fake_set_setting(&rig.fake, "a.vpl");
    poll_now(&rig);
    CHECK(shown(&rig, 14) == bgra(0xFF0000));
    fake_set_setting(&rig.fake, "b.vpl");
    poll_now(&rig);
    CHECK(shown(&rig, 14) == bgra(0x00FF00));
    fake_put_file(&rig.fake, "a.vpl", vpl_with_colour_14(0x0000FF)); // same size
    os_sleep_ms(3100);                                               // older than the "verified moments ago" window
    fake_set_setting(&rig.fake, "a.vpl");
    poll_now(&rig);
    CHECK(shown(&rig, 14) == bgra(0x0000FF));
    rig_stop(&rig);
    return true;
}

static bool test_stream_palette_wins(void)
{
    rig_t rig;
    CHECK(rig_start(&rig, NULL));
    fake_put_file(&rig.fake, "red.vpl", vpl_with_colour_14(0xFF0000));
    fake_set_setting(&rig.fake, "red.vpl");
    poll_now(&rig);
    CHECK(shown(&rig, 14) == bgra(0xFF0000));

    // Palette packets arrive: the stream is the source, polling stops.
    pthread_mutex_lock(&rig.context->palette_mutex);
    c64_palette_follow_note_stream_packet(rig.context);
    uint32_t stream_palette[16];
    memcpy(stream_palette, c64_default_palette, sizeof(stream_palette));
    stream_palette[14] = bgra(0x123456);
    c64_color_lut_update(&rig.context->color_lut, stream_palette);
    pthread_mutex_unlock(&rig.context->palette_mutex);
    const long requests = FAKE_COUNT(&rig.fake, http_requests);
    fake_set_setting(&rig.fake, ""); // a device setting change must not override the stream
    for (int i = 0; i < 3; i++) {
        poll_now(&rig);
    }
    CHECK(source(&rig) == C64_PALETTE_SOURCE_STREAM);
    CHECK(FAKE_COUNT(&rig.fake, http_requests) == requests);
    CHECK(shown(&rig, 14) == bgra(0x123456));

    // Packets stop (stale for more than 5 s): the device setting applies again.
    rig.context->palette_packet_last_ns = os_gettime_ns() - 6000000000ULL;
    poll_now(&rig);
    CHECK(source(&rig) == C64_PALETTE_SOURCE_DEVICE_DEFAULT);
    CHECK(shown(&rig, 14) == c64_default_palette[14]);
    rig_stop(&rig);
    return true;
}

static bool test_force_legacy_uses_default(void)
{
    rig_t rig;
    CHECK(rig_start(&rig, NULL));
    fake_put_file(&rig.fake, "red.vpl", vpl_with_colour_14(0xFF0000));
    fake_set_setting(&rig.fake, "red.vpl");
    poll_now(&rig);
    CHECK(shown(&rig, 14) == bgra(0xFF0000));

    rig.context->stream_control_transport = C64_STREAM_TRANSPORT_LEGACY;
    const long requests = FAKE_COUNT(&rig.fake, http_requests);
    poll_now(&rig);
    CHECK(source(&rig) == C64_PALETTE_SOURCE_NEEDS_REST);
    CHECK(shown(&rig, 14) == c64_default_palette[14]);
    CHECK(FAKE_COUNT(&rig.fake, http_requests) == requests); // no device traffic

    // Auto keeps reading the device setting even while stream control fell
    // back to the control port: only Force Legacy turns REST off.
    rig.context->stream_control_transport = C64_STREAM_TRANSPORT_AUTO;
    rig.context->stream_rest_demoted_until_ns = UINT64_MAX;
    poll_now(&rig);
    CHECK(source(&rig) == C64_PALETTE_SOURCE_DEVICE_FILE);
    CHECK(shown(&rig, 14) == bgra(0xFF0000));
    rig_stop(&rig);
    return true;
}

static bool test_password(void)
{
    rig_t rig;
    CHECK(rig_start(&rig, "s3cret"));
    fake_put_file(&rig.fake, "red.vpl", vpl_with_colour_14(0xFF0000));
    fake_set_setting(&rig.fake, "red.vpl");
    poll_now(&rig);
    CHECK(source(&rig) == C64_PALETTE_SOURCE_DEVICE_FILE); // REST and FTP both use the network password
    CHECK(shown(&rig, 14) == bgra(0xFF0000));

    snprintf(rig.context->c64_password, sizeof(rig.context->c64_password), "wrong");
    poll_now(&rig);
    CHECK(source(&rig) == C64_PALETTE_SOURCE_ERROR);
    CHECK(rig.context->palette_error == C64_PALETTE_ERROR_PASSWORD);
    CHECK(shown(&rig, 14) == bgra(0xFF0000)); // the last colours stay
    rig_stop(&rig);
    return true;
}

static bool test_errors_keep_last_colours(void)
{
    rig_t rig;
    CHECK(rig_start(&rig, NULL));
    fake_put_file(&rig.fake, "red.vpl", vpl_with_colour_14(0xFF0000));
    fake_put_file(&rig.fake, "broken.vpl", "this is not a palette\n");
    fake_set_setting(&rig.fake, "red.vpl");
    poll_now(&rig);
    CHECK(shown(&rig, 14) == bgra(0xFF0000));

    fake_set_setting(&rig.fake, "missing.vpl");
    poll_now(&rig);
    CHECK(source(&rig) == C64_PALETTE_SOURCE_ERROR);
    CHECK(rig.context->palette_error == C64_PALETTE_ERROR_FILE_NOT_FOUND);
    CHECK(!strcmp(rig.context->palette_source_file, "missing.vpl"));
    CHECK(shown(&rig, 14) == bgra(0xFF0000));

    fake_set_setting(&rig.fake, "broken.vpl");
    poll_now(&rig);
    CHECK(rig.context->palette_error == C64_PALETTE_ERROR_INVALID_FILE);
    CHECK(shown(&rig, 14) == bgra(0xFF0000));

    fake_set_setting(&rig.fake, "red.vpl");
    FAKE_SET(&rig.fake, ftp_enabled, false); // cached: no FTP needed
    poll_now(&rig);
    CHECK(source(&rig) == C64_PALETTE_SOURCE_DEVICE_FILE);

    FAKE_SET(&rig.fake, has_setting, false);
    poll_now(&rig);
    CHECK(rig.context->palette_error == C64_PALETTE_ERROR_NO_SETTING);
    rig_stop(&rig);
    return true;
}

static bool test_device_switch_shows_each_devices_palette(void)
{
    rig_t rig;
    CHECK(rig_start(&rig, NULL));
    fake_put_file(&rig.fake, "red.vpl", vpl_with_colour_14(0xFF0000));
    fake_set_setting(&rig.fake, "red.vpl");
    poll_now(&rig);
    CHECK(shown(&rig, 14) == bgra(0xFF0000));

    // Switch to a device not seen before: the default at once, not red.
    snprintf(rig.context->active_device_id, sizeof(rig.context->active_device_id), "device-b");
    c64_palette_follow_device_changed(rig.context, "device-b");
    CHECK(shown(&rig, 14) == bgra(0xFF0000)); // device A's frames are still on screen
    c64_palette_follow_cutover(rig.context);  // device B's first video packet
    CHECK(shown(&rig, 14) == c64_default_palette[14]);
    fake_set_setting(&rig.fake, ""); // device b uses the built-in palette
    poll_now(&rig);
    CHECK(source(&rig) == C64_PALETTE_SOURCE_DEVICE_DEFAULT);

    // Switch back: device a's palette at once, before any request.
    const long requests = FAKE_COUNT(&rig.fake, http_requests);
    snprintf(rig.context->active_device_id, sizeof(rig.context->active_device_id), "device-a");
    c64_palette_follow_device_changed(rig.context, "device-a");
    c64_palette_follow_cutover(rig.context);
    CHECK(shown(&rig, 14) == bgra(0xFF0000));
    CHECK(FAKE_COUNT(&rig.fake, http_requests) == requests);
    rig_stop(&rig);
    return true;
}

static bool test_status_text_and_refresh(void)
{
    rig_t rig;
    CHECK(rig_start(&rig, NULL));
    fake_put_file(&rig.fake, "red.vpl", vpl_with_colour_14(0xFF0000));
    fake_set_setting(&rig.fake, "red.vpl");
    const long refreshes = properties_refreshes;
    poll_now(&rig);
    poll_now(&rig);
    CHECK(properties_refreshes == refreshes + 1); // only on a change

    // A status that flips on every check (a flaky link) redraws Properties
    // at most once a second; the last state is still shown afterwards.
    for (int i = 0; i < 8; i++) {
        FAKE_SET(&rig.fake, setting_status, i % 2 ? 0 : 503);
        poll_now(&rig);
    }
    CHECK(properties_refreshes == refreshes + 1);
    CHECK(os_atomic_load_bool(&rig.context->palette_refresh_pending));
    os_sleep_ms(1100);
    FAKE_SET(&rig.fake, setting_status, 503);
    poll_now(&rig);
    CHECK(properties_refreshes == refreshes + 2);
    FAKE_SET(&rig.fake, setting_status, 0);
    poll_now(&rig);
    char text[256];
    c64_palette_follow_describe(rig.context, text, sizeof(text));
    CHECK(!strcmp(text, "DevicePaletteSource.File|red.vpl"));
    // Every source and error has a translation key.
    for (int s = C64_PALETTE_SOURCE_OFF; s <= C64_PALETTE_SOURCE_ERROR; s++) {
        for (int e = C64_PALETTE_ERROR_NONE; e <= C64_PALETTE_ERROR_CACHE; e++) {
            CHECK(c64_palette_follow_source_key((c64_palette_source_t)s, (c64_palette_error_t)e) != NULL);
        }
    }
    rig_stop(&rig);
    return true;
}

static bool test_worker_thread_lifecycle(void)
{
    rig_t rig;
    CHECK(rig_start(&rig, NULL));
    fake_put_file(&rig.fake, "red.vpl", vpl_with_colour_14(0xFF0000));
    fake_set_setting(&rig.fake, "red.vpl");
    rig.context->palette_poll_interval_ms = 250;
    c64_palette_follow_start(rig.context);
    c64_palette_follow_wake(rig.context);
    uint64_t deadline = os_gettime_ns() + 3000000000ULL;
    while (shown(&rig, 14) != bgra(0xFF0000) && os_gettime_ns() < deadline) {
        os_sleep_ms(10);
    }
    const bool applied = shown(&rig, 14) == bgra(0xFF0000);
    // Stopping while a request hangs returns promptly (the request is cancelled).
    FAKE_SET(&rig.fake, http_delay_ms, 3000);
    c64_palette_follow_wake(rig.context);
    os_sleep_ms(100);
    const uint64_t start = os_gettime_ns();
    c64_palette_follow_stop(rig.context);
    const uint64_t stop_ms = (os_gettime_ns() - start) / 1000000ULL;
    const c64_palette_source_t after_stop = source(&rig);
    FAKE_SET(&rig.fake, http_delay_ms, 0);
    rig_stop(&rig);
    CHECK(applied);
    CHECK(stop_ms < 500);                                // the request in flight is abandoned within about 50 ms
    CHECK(after_stop == C64_PALETTE_SOURCE_DEVICE_FILE); // a cancelled request is not reported as a failure
    return true;
}

// A check that was running for the previous device when the source switched
// must not put that device's colours on screen, remember them for the new
// device, or cache its file under the new device's name.
static bool test_stale_result_after_switch_is_dropped(void)
{
    rig_t rig;
    CHECK(rig_start(&rig, NULL));
    fake_put_file(&rig.fake, "red.vpl", vpl_with_colour_14(0xFF0000));
    // Device A's file is already cached, so its late result needs no
    // download: only the device-switch check can stop it.
    fake_set_setting(&rig.fake, "red.vpl");
    poll_now(&rig);
    fake_set_setting(&rig.fake, "");
    poll_now(&rig);
    CHECK(shown(&rig, 14) == c64_default_palette[14]);
    fake_set_setting(&rig.fake, "red.vpl");
    const long downloads = FAKE_COUNT(&rig.fake, ftp_retrs);
    FAKE_SET(&rig.fake, http_delay_ms, 600); // device A answers late
    rig.context->palette_poll_interval_ms = 10000;
    const long before = FAKE_COUNT(&rig.fake, setting_requests);
    c64_palette_follow_start(rig.context);
    c64_palette_follow_wake(rig.context);
    const uint64_t deadline = os_gettime_ns() + 2000000000ULL;
    while (FAKE_COUNT(&rig.fake, setting_requests) == before && os_gettime_ns() < deadline) {
        os_sleep_ms(5);
    }
    CHECK(FAKE_COUNT(&rig.fake, setting_requests) > before); // A's answer is on its way

    // Switch to device B while A's answer is in flight. B does not answer
    // yet (still booting), so nothing legitimate replaces what is shown.
    pthread_mutex_lock(&rig.context->config_mutex);
    snprintf(rig.context->active_device_id, sizeof(rig.context->active_device_id), "device-b");
    pthread_mutex_unlock(&rig.context->config_mutex);
    c64_palette_follow_device_changed(rig.context, "device-b");
    c64_palette_follow_cutover(rig.context); // device B's first video packet
    FAKE_SET(&rig.fake, http_delay_ms, 0);
    FAKE_SET(&rig.fake, setting_status, 503);

    // Watch the screen until well after A's late answer and B's first check.
    bool red_seen = false;
    const uint64_t watch_until = os_gettime_ns() + 2500000000ULL;
    while (os_gettime_ns() < watch_until) {
        red_seen |= shown(&rig, 14) == bgra(0xFF0000);
        os_sleep_ms(5);
    }
    const c64_palette_source_t final_source = source(&rig);
    c64_palette_follow_stop(rig.context);
    const bool rig_downloads_unchanged = FAKE_COUNT(&rig.fake, ftp_retrs) == downloads;
    pthread_mutex_lock(&rig.context->palette_mutex);
    bool b_remembered_red = false;
    for (size_t i = 0; i < rig.context->palette_memory_count; i++) {
        b_remembered_red |= !strcmp(rig.context->palette_memory[i].device_key, "device-b") &&
                            rig.context->palette_memory[i].colors[14] == bgra(0xFF0000);
    }
    pthread_mutex_unlock(&rig.context->palette_mutex);
    rig_stop(&rig);
    CHECK(rig_downloads_unchanged);
    CHECK(!red_seen);
    CHECK(!b_remembered_red);
    CHECK(final_source == C64_PALETTE_SOURCE_ERROR); // B unreachable, shown with the default colours
    return true;
}

typedef struct {
    struct c64_source *context;
    volatile bool done;
} switch_call_t;

static void *call_device_changed_holding_config(void *opaque)
{
    switch_call_t *call = opaque;
    pthread_mutex_lock(&call->context->config_mutex);
    c64_palette_follow_device_changed(call->context, "device-b");
    pthread_mutex_unlock(&call->context->config_mutex);
    os_atomic_set_bool(&call->done, true);
    return NULL;
}

// c64_update holds config_mutex while it applies a new host. Telling the
// worker about the switch must never need config_mutex again (a
// non-recursive mutex would hang the OBS UI thread).
static bool test_device_changed_with_config_mutex_held(void)
{
    rig_t rig;
    CHECK(rig_start(&rig, NULL));
    switch_call_t call = {rig.context, false};
    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, call_device_changed_holding_config, &call) == 0);
    const uint64_t deadline = os_gettime_ns() + 2000000000ULL;
    while (!os_atomic_load_bool(&call.done) && os_gettime_ns() < deadline) {
        os_sleep_ms(5);
    }
    if (!os_atomic_load_bool(&call.done)) {
        fprintf(stderr, "c64_palette_follow_device_changed deadlocked with config_mutex held\n");
        return false; // the thread is stuck; main() exits with a failure
    }
    pthread_join(thread, NULL);
    rig_stop(&rig);
    return true;
}

// Palette packets that pause and resume with the same palette generation
// (firmware repeats it) must replace the polled colours again: the polled
// palette resets the stream's ordering.
static bool test_stream_resumes_after_polled_palette(void)
{
    rig_t rig;
    CHECK(rig_start(&rig, NULL));
    fake_put_file(&rig.fake, "red.vpl", vpl_with_colour_14(0xFF0000));
    fake_set_setting(&rig.fake, "red.vpl");
    rig.context->device_palette.ordering_valid = true;
    poll_now(&rig);
    CHECK(shown(&rig, 14) == bgra(0xFF0000));
    CHECK(!rig.context->device_palette.ordering_valid);

    // Freshness is what decides between stream and polled colours.
    CHECK(!stream_fresh(&rig));
    pthread_mutex_lock(&rig.context->palette_mutex);
    c64_palette_follow_note_stream_packet(rig.context);
    pthread_mutex_unlock(&rig.context->palette_mutex);
    CHECK(stream_fresh(&rig));
    rig.context->palette_packet_last_ns = os_gettime_ns() - 6000000000ULL;
    CHECK(!stream_fresh(&rig));
    rig_stop(&rig);
    return true;
}

// A change of setting, device or mode is acted on at once, not after the
// remaining interval.
static bool test_wake_is_prompt(void)
{
    rig_t rig;
    CHECK(rig_start(&rig, NULL));
    fake_put_file(&rig.fake, "red.vpl", vpl_with_colour_14(0xFF0000));
    fake_set_setting(&rig.fake, "");
    rig.context->palette_poll_interval_ms = 10000;
    c64_palette_follow_start(rig.context);
    c64_palette_follow_wake(rig.context);
    uint64_t deadline = os_gettime_ns() + 2000000000ULL;
    while (source(&rig) != C64_PALETTE_SOURCE_DEVICE_DEFAULT && os_gettime_ns() < deadline) {
        os_sleep_ms(5);
    }
    CHECK(source(&rig) == C64_PALETTE_SOURCE_DEVICE_DEFAULT);
    fake_set_setting(&rig.fake, "red.vpl");
    const uint64_t start = os_gettime_ns();
    c64_palette_follow_wake(rig.context);
    deadline = start + 2000000000ULL;
    while (shown(&rig, 14) != bgra(0xFF0000) && os_gettime_ns() < deadline) {
        os_sleep_ms(2);
    }
    const uint64_t latency_ms = (os_gettime_ns() - start) / 1000000ULL;
    rig_stop(&rig);
    CHECK(latency_ms < 500);
    return true;
}

// Each failure names its cause; none is mistaken for another.
static bool test_errors_are_reported_per_cause(void)
{
    rig_t rig;
    CHECK(rig_start(&rig, NULL));
    fake_put_file(&rig.fake, "red.vpl", vpl_with_colour_14(0xFF0000));
    fake_set_setting(&rig.fake, "red.vpl");
    FAKE_SET(&rig.fake, setting_status, 503);
    poll_now(&rig);
    CHECK(rig.context->palette_error == C64_PALETTE_ERROR_UNREACHABLE);
    FAKE_SET(&rig.fake, setting_status, 0);
    FAKE_SET(&rig.fake, info_status, 500);
    poll_now(&rig);
    CHECK(rig.context->palette_error == C64_PALETTE_ERROR_UNREACHABLE); // not "file not found"
    FAKE_SET(&rig.fake, info_status, 0);
    FAKE_SET(&rig.fake, ftp_enabled, false);
    poll_now(&rig);
    CHECK(rig.context->palette_error == C64_PALETTE_ERROR_FTP_UNREACHABLE);
    FAKE_SET(&rig.fake, ftp_enabled, true);
    poll_now(&rig);
    CHECK(source(&rig) == C64_PALETTE_SOURCE_DEVICE_FILE);
    CHECK(shown(&rig, 14) == bgra(0xFF0000));
    rig_stop(&rig);
    return true;
}

// ---------------------------------------------------------------------------
// Mixed fleet: device A sends its palette in the video stream (newer
// firmware), device B does not and is polled. Both fakes listen on loopback;
// the target hook maps the configured host names onto them.

static fake_ultimate_t *fleet_a;
static fake_ultimate_t *fleet_b;

static void fleet_hook(char *host, size_t host_size, uint16_t *rest_port, uint16_t *ftp_port)
{
    const fake_ultimate_t *fake = !strcmp(host, "device-a") ? fleet_a : fleet_b;
    snprintf(host, host_size, "127.0.0.1");
    *rest_port = fake->http_port;
    *ftp_port = fake->ftp_port;
}

typedef struct {
    struct c64_source *context;
    // The simulated network: which device is selected, and which device's
    // frames are on screen. Switches and video packets are serialised by
    // net_mutex, so every sample sees a consistent pair.
    pthread_mutex_t net_mutex;
    long active;        // 0: A, 1: B
    long frames_device; // device of the frames on screen, -1: none yet
    volatile bool stop;
    volatile long stream_packets;
    uint64_t a_started_ns;      // A's stream (re)start, under net_mutex
    uint64_t a_last_palette_ns; // A's last palette packet, under net_mutex
    uint32_t a_palette[16];
} fleet_t;

// Both devices stream video; device A also sends its palette in the stream.
// Every 10 ms the selected device delivers a video packet, which switches
// the colours over exactly when its frames take over the screen (as the
// video processing thread does). Like the firmware, A sends its first
// palette packet about 100 ms after its stream starts, then periodically.
static void *fleet_stream_main(void *opaque)
{
    fleet_t *fleet = opaque;
    while (!os_atomic_load_bool(&fleet->stop)) {
        pthread_mutex_lock(&fleet->net_mutex);
        const long device = fleet->active;
        c64_palette_follow_cutover(fleet->context);
        pthread_mutex_lock(&fleet->context->palette_mutex);
        fleet->frames_device = device;
        const uint64_t now_ns = os_gettime_ns();
        if (device == 0 && now_ns - fleet->a_started_ns >= 100000000ULL &&
            now_ns - fleet->a_last_palette_ns >= 250000000ULL &&
            os_atomic_load_bool(&fleet->context->follow_device_palette)) {
            fleet->a_last_palette_ns = now_ns;
            c64_palette_follow_note_stream_packet(fleet->context);
            c64_palette_follow_show_stream_palette(fleet->context, fleet->a_palette);
            os_atomic_inc_long(&fleet->stream_packets);
        }
        pthread_mutex_unlock(&fleet->context->palette_mutex);
        pthread_mutex_unlock(&fleet->net_mutex);
        os_sleep_ms(10);
    }
    return NULL;
}

// What c64_update does on a device change.
static void fleet_switch(fleet_t *fleet, long device)
{
    struct c64_source *context = fleet->context;
    const char *key = device == 0 ? "device-a" : "device-b";
    pthread_mutex_lock(&fleet->net_mutex);
    if (device == 0 && fleet->active != 0) {
        fleet->a_started_ns = os_gettime_ns();
        fleet->a_last_palette_ns = 0;
    }
    fleet->active = device;
    pthread_mutex_lock(&context->config_mutex);
    snprintf(context->active_device_id, sizeof(context->active_device_id), "%s", key);
    snprintf(context->ip_address, sizeof(context->ip_address), "%s", key);
    os_atomic_inc_long(&context->palette_device_generation);
    pthread_mutex_unlock(&context->config_mutex);
    c64_palette_follow_device_changed(context, key);
    pthread_mutex_unlock(&fleet->net_mutex);
}

// The colour on screen and the device whose frames show it.
static uint32_t fleet_shown(fleet_t *fleet, long *frames_device)
{
    pthread_mutex_lock(&fleet->net_mutex);
    pthread_mutex_lock(&fleet->context->palette_mutex);
    *frames_device = fleet->frames_device;
    const uint32_t color = fleet->context->color_lut.palette[14];
    pthread_mutex_unlock(&fleet->context->palette_mutex);
    pthread_mutex_unlock(&fleet->net_mutex);
    return color;
}

static bool memory_is_clean(struct c64_source *context, uint32_t a_color, uint32_t b_color)
{
    bool clean = true;
    pthread_mutex_lock(&context->palette_mutex);
    for (size_t i = 0; i < context->palette_memory_count; i++) {
        const uint32_t c = context->palette_memory[i].colors[14];
        if (!strcmp(context->palette_memory[i].device_key, "device-a")) {
            clean &= c != b_color;
        } else if (!strcmp(context->palette_memory[i].device_key, "device-b")) {
            clean &= c != a_color;
        }
    }
    pthread_mutex_unlock(&context->palette_mutex);
    return clean;
}

typedef struct {
    rig_t a;
    fake_ultimate_t b;
    fleet_t fleet;
    pthread_t stream;
} fleet_rig_t;

static void fleet_start(fleet_rig_t *rig)
{
    CHECK(rig_start(&rig->a, NULL));
    // A: setting empty (built-in), but the stream reports red: a palette loaded
    // from the file browser, which only the stream can reveal.
    memset(&rig->b, 0, sizeof(rig->b));
    rig->b.has_setting = true;
    rig->b.ftp_enabled = true;
    CHECK(fake_start(&rig->b));
    fake_put_file(&rig->b, "green.vpl", vpl_with_colour_14(0x00FF00));
    fake_set_setting(&rig->b, "green.vpl");
    fleet_a = &rig->a.fake;
    fleet_b = &rig->b;
    c64_palette_follow_set_target_hook_for_test(fleet_hook);

    fleet_t *fleet = &rig->fleet;
    memset(fleet, 0, sizeof(*fleet));
    pthread_mutex_init(&fleet->net_mutex, NULL);
    fleet->frames_device = -1;
    fleet->context = rig->a.context;
    memcpy(fleet->a_palette, c64_default_palette, sizeof(fleet->a_palette));
    fleet->a_palette[14] = bgra(0xFF0000);
    rig->a.context->palette_poll_interval_ms = 250;
    fleet->active = 1;
    fleet_switch(fleet, 0);
    c64_palette_follow_start(rig->a.context);
    CHECK(pthread_create(&rig->stream, NULL, fleet_stream_main, fleet) == 0);
}

static void fleet_stop(fleet_rig_t *rig)
{
    os_atomic_set_bool(&rig->fleet.stop, true);
    pthread_join(rig->stream, NULL);
    c64_palette_follow_set_target_hook_for_test(NULL);
    fake_stop(&rig->b);
    rig_stop(&rig->a);
    pthread_mutex_destroy(&rig->fleet.net_mutex);
}

// Waits until the screen shows want for the selected device.
static bool fleet_settles(fleet_t *fleet, uint32_t want, uint64_t timeout_ms)
{
    const uint64_t deadline = os_gettime_ns() + timeout_ms * 1000000ULL;
    while (os_gettime_ns() < deadline) {
        long frames_device;
        if (fleet_shown(fleet, &frames_device) == want) {
            return true;
        }
        os_sleep_ms(2);
    }
    return false;
}

// Once both devices have been seen, a switch shows the target's colours at
// once (from memory) and they stay: no flash of the default palette, of the
// device setting of a streaming device, or of the other device's colours.
static bool test_switch_between_stream_and_polled_devices(void)
{
    fleet_rig_t rig;
    fleet_start(&rig);
    fleet_t *fleet = &rig.fleet;
    const uint32_t red = bgra(0xFF0000);
    const uint32_t green = bgra(0x00FF00);
    CHECK(fleet_settles(fleet, red, 2000));
    fleet_switch(fleet, 1);
    CHECK(fleet_settles(fleet, green, 2000));

    for (int round = 0; round < 6; round++) {
        const long device = round % 2 == 0 ? 0 : 1;
        const uint32_t want = device == 0 ? red : green;
        fleet_switch(fleet, device);
        // Watch for 600 ms: longer than two checks and the stream start.
        const uint64_t until = os_gettime_ns() + 600000000ULL;
        while (os_gettime_ns() < until) {
            long frames_device;
            const uint32_t color = fleet_shown(fleet, &frames_device);
            const uint32_t frames_want = frames_device == 0 ? red : green;
            if (color != frames_want) {
                fprintf(stderr, "round %d: frames of device %ld shown in %08X, want %08X\n", round, frames_device,
                        color, frames_want);
                CHECK(false);
            }
            os_sleep_ms(1);
        }
        CHECK(fleet_shown(fleet, &(long){0}) == want);
        CHECK(source(&rig.a) == (device == 0 ? C64_PALETTE_SOURCE_STREAM : C64_PALETTE_SOURCE_DEVICE_FILE));
    }
    // The streaming device's setting was never applied over its stream palette.
    CHECK(memory_is_clean(fleet->context, red, green));
    fleet_stop(&rig);
    return true;
}

// Hundreds of switches with random dwell from 0 to 40 ms (and a few longer
// stays so checks complete): at no stable moment does the screen show the
// other device's colours, the palette memory never files one device's
// colours under the other, and after the last switch the right palette and
// status follow within a second.
static bool test_chaos_switching(void)
{
    fleet_rig_t rig;
    fleet_start(&rig);
    fleet_t *fleet = &rig.fleet;
    const uint32_t red = bgra(0xFF0000);
    const uint32_t green = bgra(0x00FF00);
    uint32_t seed = 64;
    long violations = 0;
    long wrong_device = 0;
    long samples = 0;
    const uint64_t start = os_gettime_ns();
    const int switches = 600;
    for (int i = 0; i < switches; i++) {
        seed = seed * 1103515245u + 12345u;
        const long device = (long)((seed >> 16) & 1);
        fleet_switch(fleet, device);
        seed = seed * 1103515245u + 12345u;
        unsigned dwell_ms = (seed >> 16) % 41;
        if (i % 97 == 0) {
            dwell_ms = 700; // long enough for a check of the polled device
        }
        const uint64_t until = os_gettime_ns() + dwell_ms * 1000000ULL;
        do {
            long frames_device;
            const uint32_t color = fleet_shown(fleet, &frames_device);
            if (frames_device >= 0) {
                samples++;
                // Frames are always shown in their own device's colours
                // (default only before that device's palette is known).
                const uint32_t own = frames_device == 0 ? red : green;
                if (color != own && color != c64_default_palette[14]) {
                    if (violations++ < 5) {
                        fprintf(stderr, "switch %d: frames of device %ld shown in %08X\n", i, frames_device, color);
                    }
                }
                if (color == (frames_device == 0 ? green : red)) {
                    wrong_device++;
                }
            }
            os_sleep_ms(1);
        } while (os_gettime_ns() < until);
        CHECK(memory_is_clean(fleet->context, red, green));
    }
    const uint64_t elapsed_ms = (os_gettime_ns() - start) / 1000000ULL;
    printf("  %d switches in %llu ms, %ld stable samples, %ld stream packets, %ld setting requests, %ld downloads\n",
           switches, (unsigned long long)elapsed_ms, samples, os_atomic_load_long(&fleet->stream_packets),
           FAKE_COUNT(&rig.b, setting_requests), FAKE_COUNT(&rig.b, ftp_retrs));
    CHECK(violations == 0);
    CHECK(wrong_device == 0);
    CHECK(samples > 1000);

    // Both ends settle on the right palette and status.
    fleet_switch(fleet, 1);
    CHECK(fleet_settles(fleet, green, 1000));
    const uint64_t status_deadline = os_gettime_ns() + 1500000000ULL;
    while (source(&rig.a) != C64_PALETTE_SOURCE_DEVICE_FILE && os_gettime_ns() < status_deadline) {
        os_sleep_ms(5);
    }
    CHECK(source(&rig.a) == C64_PALETTE_SOURCE_DEVICE_FILE);
    fleet_switch(fleet, 0);
    CHECK(fleet_settles(fleet, red, 1000));
    const uint64_t stream_deadline = os_gettime_ns() + 1500000000ULL;
    while (source(&rig.a) != C64_PALETTE_SOURCE_STREAM && os_gettime_ns() < stream_deadline) {
        os_sleep_ms(5);
    }
    CHECK(source(&rig.a) == C64_PALETTE_SOURCE_STREAM);
    // Rapid switching does not mean one download per switch: after the first
    // download the file is fetched again only by the content recheck (every
    // 10th check) and when it is selected again after more than 3 s.
    const long downloads = FAKE_COUNT(&rig.b, ftp_retrs);
    const long allowed = 2 + FAKE_COUNT(&rig.b, setting_requests) / 10 + (long)(elapsed_ms / 3000);
    printf("  %ld downloads of the polled device's file, at most %ld allowed\n", downloads, allowed);
    CHECK(downloads <= allowed);
    CHECK(downloads < switches / 10);
    fleet_stop(&rig);
    return true;
}

// Follow device toggled on and off while switching: the selected palette
// comes back when off, the device palette when on, nothing hangs.
static bool test_toggle_follow_while_switching(void)
{
    fleet_rig_t rig;
    fleet_start(&rig);
    fleet_t *fleet = &rig.fleet;
    for (int i = 0; i < 200; i++) {
        fleet_switch(fleet, i % 3 == 0 ? 0 : 1);
        os_atomic_set_bool(&fleet->context->follow_device_palette, i % 2 == 0);
        c64_palette_follow_wake(fleet->context);
        os_sleep_ms(i % 7);
    }
    os_atomic_set_bool(&fleet->context->follow_device_palette, true);
    c64_palette_follow_wake(fleet->context);
    fleet_switch(fleet, 1);
    CHECK(fleet_settles(fleet, bgra(0x00FF00), 1500));
    fleet_switch(fleet, 0);
    CHECK(fleet_settles(fleet, bgra(0xFF0000), 1500));
    CHECK(memory_is_clean(fleet->context, bgra(0xFF0000), bgra(0x00FF00)));
    fleet_stop(&rig);
    return true;
}

// Removes the test's cache folders (one level of rig folders with files).
static void remove_cache_root(void)
{
    os_dir_t *root = os_opendir(cache_root);
    if (!root) {
        return;
    }
    struct os_dirent *rig_entry;
    while ((rig_entry = os_readdir(root)) != NULL) {
        if (!rig_entry->directory || rig_entry->d_name[0] == '.') {
            continue;
        }
        char rig_dir[1024];
        snprintf(rig_dir, sizeof(rig_dir), "%s/%s", cache_root, rig_entry->d_name);
        os_dir_t *files = os_opendir(rig_dir);
        struct os_dirent *file_entry;
        while (files && (file_entry = os_readdir(files)) != NULL) {
            if (!file_entry->directory) {
                char file[1400];
                snprintf(file, sizeof(file), "%s/%s", rig_dir, file_entry->d_name);
                os_unlink(file);
            }
        }
        if (files) {
            os_closedir(files);
        }
        os_rmdir(rig_dir);
    }
    os_closedir(root);
    os_rmdir(cache_root);
}

int main(void)
{
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return 1;
    }
    const char *temp = getenv("TEMP") ? getenv("TEMP") : ".";
    snprintf(cache_root, sizeof(cache_root), "%s\\c64-palette-follow-test-%d", temp, _getpid());
#else
    signal(SIGPIPE, SIG_IGN);
    const char *temp = getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp";
    snprintf(cache_root, sizeof(cache_root), "%s/c64-palette-follow-test-%d", temp, (int)getpid());
#endif
    setvbuf(stdout, NULL, _IONBF, 0);
    curl_global_init(CURL_GLOBAL_DEFAULT);
    struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        {"builtin_then_file_then_change", test_builtin_then_file_then_change},
        {"overwritten_file_same_size_is_picked_up", test_overwritten_file_same_size_is_picked_up},
        {"reselected_file_is_verified", test_reselected_file_is_verified},
        {"stream_palette_wins", test_stream_palette_wins},
        {"force_legacy_uses_default", test_force_legacy_uses_default},
        {"stale_result_after_switch_is_dropped", test_stale_result_after_switch_is_dropped},
        {"device_changed_with_config_mutex_held", test_device_changed_with_config_mutex_held},
        {"stream_resumes_after_polled_palette", test_stream_resumes_after_polled_palette},
        {"wake_is_prompt", test_wake_is_prompt},
        {"errors_are_reported_per_cause", test_errors_are_reported_per_cause},
        {"switch_between_stream_and_polled_devices", test_switch_between_stream_and_polled_devices},
        {"chaos_switching", test_chaos_switching},
        {"toggle_follow_while_switching", test_toggle_follow_while_switching},
        {"password", test_password},
        {"errors_keep_last_colours", test_errors_keep_last_colours},
        {"device_switch_shows_each_devices_palette", test_device_switch_shows_each_devices_palette},
        {"status_text_and_refresh", test_status_text_and_refresh},
        {"worker_thread_lifecycle", test_worker_thread_lifecycle},
    };
    int failures = 0;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        const bool ok = tests[i].run();
        printf("%s %s\n", ok ? "PASS" : "FAIL", tests[i].name);
        failures += ok ? 0 : 1;
    }
    curl_global_cleanup();
    remove_cache_root();
    return failures ? 1 : 0;
}
