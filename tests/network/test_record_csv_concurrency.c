/* C64 Stream - C64STR-024 (and C64STR-018) regression.
 *
 * The UI record toggle (start/stop CSV) closes timing_file and NULLs it, while
 * the video/audio threads write CSV events to the same handle. Without shared
 * synchronisation this is a use-after-free / double-close race. All CSV file
 * lifecycle and writes are now serialised under the source recording_mutex.
 *
 * This drives the real production functions (c64_start_obs_csv_recording /
 * c64_stop_obs_csv_recording / c64_obs_log_video_event) from two threads and
 * asserts they are race-free under ThreadSanitizer. */
#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "c64-record.h"
#include "c64-record-obs.h"
#include "c64-types.h"

#define ITERS 20000

/* --- no-op stubs for the record subsystem's heavy dependencies ------------ */
bool c64_debug_logging = false;
obs_module_t *obs_current_module(void)
{
    return NULL;
}
/* c64_session_ensure_exists and c64_network_write_header come from the linked
 * record sources; session_folder is pre-set so ensure_exists is a no-op. */
void c64_audio_write_wav_header(FILE *f, uint32_t r, uint16_t ch, uint16_t b)
{
    (void)f;
    (void)r;
    (void)ch;
    (void)b;
}
void c64_audio_finalize_wav_header(FILE *f, uint64_t s)
{
    (void)f;
    (void)s;
}
void c64_audio_record_data(struct c64_source *c, const uint8_t *d, size_t n)
{
    (void)c;
    (void)d;
    (void)n;
}
void c64_create_directory_recursive(const char *p)
{
    (void)p;
}
void c64_frames_save_as_bmp(struct c64_source *c, uint32_t *f)
{
    (void)c;
    (void)f;
}
void *c64_rest_client_create(const char *u, const char *p)
{
    (void)u;
    (void)p;
    return NULL;
}
void c64_rest_client_destroy(void *c)
{
    (void)c;
}
bool c64_rest_config_get_value(void *a, const char *b, const char *c, char *d, size_t e)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    return false;
}
bool c64_rest_config_list(void *a, char ***b, size_t *c)
{
    (void)a;
    (void)b;
    (void)c;
    return false;
}
bool c64_rest_config_list_options(void *a, const char *b, const char *c, char ***d, size_t *e)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    return false;
}
bool c64_rest_config_set_value(void *a, const char *b, const char *c, const char *d)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    return false;
}
const char *c64_rest_get_error(void *c)
{
    (void)c;
    return "";
}
bool c64_rest_reset(void *c)
{
    (void)c;
    return false;
}
bool c64_rest_run_prg(void *a, const uint8_t *b, size_t c)
{
    (void)a;
    (void)b;
    (void)c;
    return false;
}
void c64_rest_string_list_free(char **a, size_t b)
{
    (void)a;
    (void)b;
}
void c64_video_record_frame(struct c64_source *c, uint32_t *f)
{
    (void)c;
    (void)f;
}
void c64_video_stop_recording(struct c64_source *c)
{
    (void)c;
}
void c64_video_write_avi_header(FILE *f, uint32_t w, uint32_t h, double fps)
{
    (void)f;
    (void)w;
    (void)h;
    (void)fps;
}

static struct c64_source *g_ctx;
static bool g_stop;

static bool stopped(void)
{
    return __atomic_load_n(&g_stop, __ATOMIC_RELAXED);
}

static void *toggler(void *arg)
{
    (void)arg;
    for (long i = 0; i < ITERS && !stopped(); i++) {
        c64_start_obs_csv_recording(g_ctx);
        c64_stop_obs_csv_recording(g_ctx);
    }
    return NULL;
}

static void *writer(void *arg)
{
    (void)arg;
    for (long i = 0; i < ITERS && !stopped(); i++) {
        /* Races the toggler's fclose+NULL of timing_file; must be serialised. */
        c64_obs_log_video_event(g_ctx, (uint16_t)i, 1024, false);
    }
    return NULL;
}

int main(void)
{
    g_ctx = calloc(1, sizeof(struct c64_source));
    assert(g_ctx != NULL);

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    assert(pthread_mutex_init(&g_ctx->recording_mutex, &attr) == 0);
    pthread_mutexattr_destroy(&attr);

    char tmpl[] = "/tmp/c64csv_XXXXXX";
    char *dir = mkdtemp(tmpl);
    assert(dir != NULL);
    snprintf(g_ctx->session_folder, sizeof(g_ctx->session_folder), "%s", dir);
    g_ctx->record_csv = true;
    g_ctx->expected_fps = 50.125;

    pthread_t a, b;
    pthread_create(&a, NULL, toggler, NULL);
    pthread_create(&b, NULL, writer, NULL);
    pthread_join(a, NULL);
    __atomic_store_n(&g_stop, true, __ATOMIC_RELAXED);
    pthread_join(b, NULL);

    c64_stop_obs_csv_recording(g_ctx);
    pthread_mutex_destroy(&g_ctx->recording_mutex);

    /* Best-effort cleanup of any CSV file left behind. */
    char path[1100];
    snprintf(path, sizeof(path), "%s/obs.csv", dir);
    remove(path);
    remove(dir);
    free(g_ctx);

    printf("test_record_csv_concurrency: PASS\n");
    return 0;
}
