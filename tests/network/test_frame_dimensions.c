/* C64 Stream - C64STR-008 regression.
 *
 * A PAL<->NTSC format change updates the source width/height on the video
 * processor thread while the graphics thread reads them to size and upload the
 * render texture. Reading or writing the two fields separately can hand the
 * reader a torn pair (new height with old width, or a height that no longer
 * matches the texture stride).
 *
 * This drives the exact production synchronisation primitive
 * (c64_dimensions_publish / c64_dimensions_snapshot, the same functions used by
 * the video processor and c64_video_tick) from two threads and asserts every
 * snapshot is one whole coherent pair -- never a mix of the two formats -- under
 * ThreadSanitizer. */
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "c64-dimensions.h"

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* Distinct width AND height per "format" so a torn read is detectable by value
 * (the production width is a constant 384, so this test uses distinct widths to
 * make tearing observable in addition to TSan's data-race detection). */
#define PAL_W 3840u
#define PAL_H 272u
#define NTSC_W 1920u
#define NTSC_H 240u
#define ITERS 500000

struct shared {
    pthread_mutex_t lock;
    uint32_t width;
    uint32_t height;
    volatile bool stop;
};

static void *publisher(void *arg)
{
    struct shared *s = arg;
    for (long i = 0; i < ITERS && !s->stop; i++) {
        if (i & 1) {
            c64_dimensions_publish(&s->lock, &s->width, &s->height, PAL_W, PAL_H);
        } else {
            c64_dimensions_publish(&s->lock, &s->width, &s->height, NTSC_W, NTSC_H);
        }
    }
    return NULL;
}

static void *reader(void *arg)
{
    struct shared *s = arg;
    for (long i = 0; i < ITERS && !s->stop; i++) {
        uint32_t w = 0, h = 0;
        c64_dimensions_snapshot(&s->lock, &s->width, &s->height, &w, &h);
        /* Must always be one whole format, never a torn cross-format pair. */
        bool pal = (w == PAL_W && h == PAL_H);
        bool ntsc = (w == NTSC_W && h == NTSC_H);
        assert(pal || ntsc);
    }
    return NULL;
}

int main(void)
{
    struct shared s = {.width = PAL_W, .height = PAL_H, .stop = false};
    pthread_mutex_init(&s.lock, NULL);

    pthread_t pub, rd;
    pthread_create(&pub, NULL, publisher, &s);
    pthread_create(&rd, NULL, reader, &s);
    pthread_join(pub, NULL);
    s.stop = true;
    pthread_join(rd, NULL);

    pthread_mutex_destroy(&s.lock);
    printf("test_frame_dimensions: PASS\n");
    return 0;
}
