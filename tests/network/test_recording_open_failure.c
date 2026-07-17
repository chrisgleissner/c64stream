/* C64 Stream - C64STR-009 regression (P1).
 *
 * When the recording file cannot be opened, c64_start_video_recording must
 * report failure (and leave no dangling handle) rather than falsely reporting
 * success while silently recording nothing. */
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
#include <sys/stat.h>
#include <unistd.h>

struct c64_source;

/* c64-record.c emits the C64CLK-003 session-end summary. This open-failure
 * harness deliberately links no audio pipeline. */
void c64_audio_log_network_errors(struct c64_source *context, bool force)
{
    (void)context;
    (void)force;
}

#include "c64-record.h"
#include "c64-types.h"

/* --- no-op stubs for the record subsystem's heavy dependencies ------------ */
bool c64_debug_logging = false;
obs_module_t *obs_current_module(void)
{
    return NULL;
}
void c64_audio_write_wav_header(FILE *f, uint32_t r, uint16_t c, uint16_t b)
{
    (void)f;
    (void)r;
    (void)c;
    (void)b;
}
void c64_audio_finalize_wav_header(FILE *f, uint64_t s)
{
    (void)f;
    (void)s;
}
void c64_video_update_avi_header(FILE *f, uint32_t frames, uint32_t samples)
{
    (void)f;
    (void)frames;
    (void)samples;
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

static struct c64_source *make_ctx(void)
{
    struct c64_source *ctx = calloc(1, sizeof(struct c64_source));
    assert(ctx != NULL);
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    assert(pthread_mutex_init(&ctx->recording_mutex, &attr) == 0);
    pthread_mutexattr_destroy(&attr);
    ctx->expected_fps = 50.125;
    ctx->record_csv = false;
    return ctx;
}

int main(void)
{
    /* Case 1: the session folder path is actually a regular file, so opening
     * "<session_folder>/video.avi" fails with ENOTDIR -> start must return
     * false, no dangling handle, no false success. A regular-file parent is a
     * privilege-independent open failure (unlike a read-only directory, which
     * root bypasses), so it reproduces on CI runners that build as root. */
    struct c64_source *ctx = make_ctx();
    ctx->record_video = true;

    char notdir[] = "/tmp/c64ro_XXXXXX";
    int notdir_fd = mkstemp(notdir);
    assert(notdir_fd >= 0);
    assert(close(notdir_fd) == 0);
    snprintf(ctx->session_folder, sizeof(ctx->session_folder), "%s", notdir);

    bool ok = c64_start_video_recording(ctx);
    assert(!ok && "open failure must report failure, not false success");
    assert(ctx->video_file == NULL && "no dangling video handle on failure");
    assert(ctx->audio_file == NULL && "no dangling audio handle on failure");

    unlink(notdir);
    pthread_mutex_destroy(&ctx->recording_mutex);
    free(ctx);

    /* Case 2: recording disabled -> start is a no-op reporting no active file. */
    ctx = make_ctx();
    ctx->record_video = false;
    ok = c64_start_video_recording(ctx);
    assert(!ok);
    assert(ctx->video_file == NULL);
    pthread_mutex_destroy(&ctx->recording_mutex);
    free(ctx);

    printf("test_recording_open_failure: PASS\n");
    return 0;
}
