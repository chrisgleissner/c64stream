/* C64 Stream - C64STR-014 regression: per-source palette / colour-LUT isolation.
 *
 * Proves two failure modes documented in the finding are prevented:
 *   1. Selecting or rebuilding one source's palette must not change another
 *      source's output or state (previously they shared one global LUT).
 *   2. A palette rebuild that overlaps frame conversion must never expose a
 *      half-updated table (previously an unsynchronised global-LUT race).
 *
 * Links only src/video/c64-color.c, which is free of OBS dependencies. */
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "c64-color.h"

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PIXEL_PAIRS 192
#define PIXELS (PIXEL_PAIRS * 2)

/* Tag byte (bits 16-23) identifies which palette a converted pixel came from,
 * so a frame that mixes two palettes (a torn LUT) is detectable. */
#define PAL_TAG(id) (((uint32_t)(id)) << 16)

static void make_palette(uint32_t palette[16], uint8_t id)
{
    for (int i = 0; i < 16; i++) {
        palette[i] = 0xFF000000u | PAL_TAG(id) | (uint32_t)i;
    }
}

static void fill_source(uint8_t *src)
{
    for (int i = 0; i < PIXEL_PAIRS; i++) {
        src[i] = (uint8_t)((i * 7) & 0xFF); /* two 4-bit indices per byte */
    }
}

static uint8_t pixel_tag(uint32_t pixel)
{
    return (uint8_t)((pixel >> 16) & 0xFF);
}

/* --- Subtest 1: two independent sources do not couple --------------------- */
static void test_two_source_isolation(void)
{
    uint32_t pal1[16], pal2[16];
    make_palette(pal1, 0x01);
    make_palette(pal2, 0x02);

    struct c64_color_lut a, b;
    c64_color_lut_init(&a, pal1);
    c64_color_lut_init(&b, pal2);

    uint8_t src[PIXEL_PAIRS];
    fill_source(src);

    uint64_t lut_a[256], lut_b[256];
    uint32_t out_a[PIXELS], out_b[PIXELS], out_b_before[PIXELS];

    c64_color_lut_snapshot(&a, lut_a);
    c64_color_lut_snapshot(&b, lut_b);
    c64_convert_pixels_optimized(lut_a, src, out_a, PIXEL_PAIRS);
    c64_convert_pixels_optimized(lut_b, src, out_b, PIXEL_PAIRS);

    /* Identical input, different palettes -> different, correctly-tagged output. */
    assert(memcmp(out_a, out_b, sizeof(out_a)) != 0);
    for (int i = 0; i < PIXELS; i++) {
        assert(pixel_tag(out_a[i]) == 0x01);
        assert(pixel_tag(out_b[i]) == 0x02);
    }
    memcpy(out_b_before, out_b, sizeof(out_b));

    /* Rebuild source A's palette. Source B must be entirely unaffected -- this
     * is exactly what the shared global LUT used to break. */
    c64_color_lut_update(&a, pal2);
    c64_color_lut_snapshot(&a, lut_a);
    c64_convert_pixels_optimized(lut_a, src, out_a, PIXEL_PAIRS);

    c64_color_lut_snapshot(&b, lut_b);
    c64_convert_pixels_optimized(lut_b, src, out_b, PIXEL_PAIRS);
    assert(memcmp(out_b, out_b_before, sizeof(out_b)) == 0); /* B unchanged */
    for (int i = 0; i < PIXELS; i++) {
        assert(pixel_tag(out_a[i]) == 0x02); /* A now uses pal2 */
        assert(pixel_tag(out_b[i]) == 0x02); /* B still uses its own pal2 */
    }

    printf("test_two_source_isolation: PASS\n");
}

/* --- Subtest 2: rebuild vs conversion never tears (TSan target) ----------- */
struct churn_state {
    struct c64_color_lut lut;
    pthread_mutex_t lock;
    bool stop; /* accessed via __atomic_* to stay TSan-clean */
    uint32_t pal_a[16];
    uint32_t pal_b[16];
};

static bool churn_stopped(struct churn_state *s)
{
    return __atomic_load_n(&s->stop, __ATOMIC_RELAXED);
}

static void *writer_thread(void *opaque)
{
    struct churn_state *s = opaque;
    for (long i = 0; i < 200000 && !churn_stopped(s); i++) {
        const uint32_t *p = (i & 1) ? s->pal_b : s->pal_a;
        pthread_mutex_lock(&s->lock);
        c64_color_lut_update(&s->lut, p);
        pthread_mutex_unlock(&s->lock);
    }
    return NULL;
}

static void *reader_thread(void *opaque)
{
    struct churn_state *s = opaque;
    uint8_t src[PIXEL_PAIRS];
    fill_source(src);
    uint64_t snapshot[256];
    uint32_t out[PIXELS];

    for (long i = 0; i < 200000 && !churn_stopped(s); i++) {
        pthread_mutex_lock(&s->lock);
        c64_color_lut_snapshot(&s->lut, snapshot);
        pthread_mutex_unlock(&s->lock);

        c64_convert_pixels_optimized(snapshot, src, out, PIXEL_PAIRS);

        /* Every pixel in this frame must come from a single palette snapshot. */
        uint8_t tag = pixel_tag(out[0]);
        assert(tag == 0x01 || tag == 0x02);
        for (int p = 1; p < PIXELS; p++) {
            assert(pixel_tag(out[p]) == tag);
        }
    }
    return NULL;
}

static void test_concurrent_rebuild(void)
{
    struct churn_state s;
    memset(&s, 0, sizeof(s));
    make_palette(s.pal_a, 0x01);
    make_palette(s.pal_b, 0x02);
    pthread_mutex_init(&s.lock, NULL);
    c64_color_lut_init(&s.lut, s.pal_a);

    pthread_t w, r;
    pthread_create(&w, NULL, writer_thread, &s);
    pthread_create(&r, NULL, reader_thread, &s);
    pthread_join(w, NULL);
    __atomic_store_n(&s.stop, true, __ATOMIC_RELAXED);
    pthread_join(r, NULL);
    pthread_mutex_destroy(&s.lock);

    printf("test_concurrent_rebuild: PASS\n");
}

int main(void)
{
    test_two_source_isolation();
    test_concurrent_rebuild();
    printf("All palette isolation tests passed\n");
    return 0;
}
