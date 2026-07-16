/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

// Linux-specific feature macros must be defined before any includes
#ifdef __linux__
#define _GNU_SOURCE
#endif

#include <obs-module.h>
#include <util/platform.h>
#include <util/threading.h> // For atomic operations
#include <string.h>
#include <inttypes.h>
#include <pthread.h>
#include <math.h>
#include <time.h> // For localtime_r/localtime_s in AV SYNC logging
#include "c64-network.h"
#include "c64-network-buffer.h"

#include "c64-logging.h"
#include "c64-video.h"
#include "c64-logo.h"
#include "c64-audio.h"
#include "c64-color.h"
#include "c64-dimensions.h"
#include "c64-types.h"
#include "c64-protocol.h"
#include "c64-ingest-filter.h"
#include "c64-record-network.h"
#include "c64-protocol.h"
#include "c64-record.h"
#include "c64-source.h"
#include "c64-av-sync.h"
#include "c64-effect-afterglow.h"

#ifdef _WIN32
#include <timeapi.h>
#pragma comment(lib, "winmm.lib") // For timeBeginPeriod/timeEndPeriod
#endif

#ifdef __linux__
#include <sys/socket.h>
#endif

#include "c64-protocol.h"

// ═══════════════════════════════════════════════════════════════════════════════
// ALIGNED MEMORY ALLOCATION HELPERS
// ═══════════════════════════════════════════════════════════════════════════════
// Cache-line aligned allocation improves SIMD performance (AVX2 benefits from 32/64-byte alignment)

// ═══════════════════════════════════════════════════════════════════════════════
// SIMD-OPTIMIZED AFTERGLOW IMPLEMENTATION (moved to c64-effect-afterglow.c)
// ═══════════════════════════════════════════════════════════════════════════════
#if 0
// Processes 4/8 pixels at a time using SSE2/AVX2 intrinsics for ~3-4x speedup.
// Runtime CPU detection selects the best available implementation.

#ifdef C64_HAS_X86_SIMD

// CPU feature detection (cached after first call)
static int c64_cpu_has_avx2 = -1; // -1 = not checked, 0 = no, 1 = yes

static void c64_detect_simd_support(void)
{
    if (c64_cpu_has_avx2 >= 0)
        return; // Already detected

#ifdef _MSC_VER
    int cpu_info[4];
    __cpuidex(cpu_info, 7, 0);
    c64_cpu_has_avx2 = (cpu_info[1] & (1 << 5)) ? 1 : 0; // EBX bit 5 = AVX2
#else
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        c64_cpu_has_avx2 = (ebx & (1 << 5)) ? 1 : 0; // EBX bit 5 = AVX2
    } else {
        c64_cpu_has_avx2 = 0;
    }
#endif

    C64_LOG_DEBUG("" VIDEO_LOG_PREFIX " SIMD detection: AVX2 %s",
                  c64_cpu_has_avx2 ? "available" : "not available (using SSE2)");
}
// SSE2 Implementation (4 pixels at a time) - baseline for all x86-64
// ─────────────────────────────────────────────────────────────────────────────
static void c64_afterglow_sse2(uint32_t *acc, const uint32_t *curr_pixels, size_t pixel_count, float decay_r,
                               float decay_g, float decay_b)
{
    // Broadcast decay factors to all 4 lanes
    const __m128 vdecay_r = _mm_set1_ps(decay_r);
    const __m128 vdecay_g = _mm_set1_ps(decay_g);
    const __m128 vdecay_b = _mm_set1_ps(decay_b);
    const __m128 v255 = _mm_set1_ps(255.0f);
    const __m128i vmask_channel = _mm_set1_epi32(0xFF);
    const __m128i valpha = _mm_set1_epi32(0xFF000000);

    size_t i = 0;

    // Process 4 pixels at a time
    for (; i + 4 <= pixel_count; i += 4) {
        // Load 4 current and 4 previous pixels
        const __m128i curr = _mm_loadu_si128((const __m128i *)&curr_pixels[i]);
        const __m128i prev = _mm_loadu_si128((const __m128i *)&acc[i]);

        // Extract R channel (bits 0-7) from 4 pixels
        const __m128i prev_r_i = _mm_and_si128(prev, vmask_channel);
        const __m128i curr_r_i = _mm_and_si128(curr, vmask_channel);

        // Extract G channel (bits 8-15)
        const __m128i prev_g_i = _mm_and_si128(_mm_srli_epi32(prev, 8), vmask_channel);
        const __m128i curr_g_i = _mm_and_si128(_mm_srli_epi32(curr, 8), vmask_channel);

        // Extract B channel (bits 16-23)
        const __m128i prev_b_i = _mm_and_si128(_mm_srli_epi32(prev, 16), vmask_channel);
        const __m128i curr_b_i = _mm_and_si128(_mm_srli_epi32(curr, 16), vmask_channel);

        // Convert to float
        const __m128 prev_r = _mm_cvtepi32_ps(prev_r_i);
        const __m128 prev_g = _mm_cvtepi32_ps(prev_g_i);
        const __m128 prev_b = _mm_cvtepi32_ps(prev_b_i);
        const __m128 curr_r = _mm_cvtepi32_ps(curr_r_i);
        const __m128 curr_g = _mm_cvtepi32_ps(curr_g_i);
        const __m128 curr_b = _mm_cvtepi32_ps(curr_b_i);

        // Apply decay: trail = prev * decay
        const __m128 trail_r = _mm_mul_ps(prev_r, vdecay_r);
        const __m128 trail_g = _mm_mul_ps(prev_g, vdecay_g);
        const __m128 trail_b = _mm_mul_ps(prev_b, vdecay_b);

        // Take max of current and trail
        __m128 out_r = _mm_max_ps(curr_r, trail_r);
        __m128 out_g = _mm_max_ps(curr_g, trail_g);
        __m128 out_b = _mm_max_ps(curr_b, trail_b);

        // Clamp to 255
        out_r = _mm_min_ps(out_r, v255);
        out_g = _mm_min_ps(out_g, v255);
        out_b = _mm_min_ps(out_b, v255);

        // Convert back to int
        const __m128i out_r_i = _mm_cvttps_epi32(out_r);
        const __m128i out_g_i = _mm_cvttps_epi32(out_g);
        const __m128i out_b_i = _mm_cvttps_epi32(out_b);

        // Pack: (A << 24) | (B << 16) | (G << 8) | R
        const __m128i rgb =
            _mm_or_si128(out_r_i, _mm_or_si128(_mm_slli_epi32(out_g_i, 8), _mm_slli_epi32(out_b_i, 16)));
        const __m128i result = _mm_or_si128(rgb, valpha);

        _mm_storeu_si128((__m128i *)&acc[i], result);
    }

    // Scalar tail for remaining pixels
    for (; i < pixel_count; i++) {
        const uint32_t curr = curr_pixels[i];
        const uint32_t prev = acc[i];

        const float pr = (float)((prev >> 0) & 0xFF);
        const float pg = (float)((prev >> 8) & 0xFF);
        const float pb = (float)((prev >> 16) & 0xFF);

        const float cr = (float)((curr >> 0) & 0xFF);
        const float cg = (float)((curr >> 8) & 0xFF);
        const float cb = (float)((curr >> 16) & 0xFF);

        float or_ = (cr > pr * decay_r) ? cr : pr * decay_r;
        float og_ = (cg > pg * decay_g) ? cg : pg * decay_g;
        float ob_ = (cb > pb * decay_b) ? cb : pb * decay_b;

        if (or_ > 255.0f)
            or_ = 255.0f;
        if (og_ > 255.0f)
            og_ = 255.0f;
        if (ob_ > 255.0f)
            ob_ = 255.0f;

        acc[i] = 0xFF000000 | ((uint32_t)ob_ << 16) | ((uint32_t)og_ << 8) | ((uint32_t)or_);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// AVX2 Implementation (8 pixels at a time) - for Haswell+ CPUs (2013+)
// ─────────────────────────────────────────────────────────────────────────────
#if defined(__AVX2__) || defined(_MSC_VER)
// Note: MSVC always has AVX2 intrinsics available, runtime check selects them
// GCC/Clang need target attribute, MSVC doesn't support it
#ifdef _MSC_VER
static void c64_afterglow_avx2(uint32_t *acc, const uint32_t *curr_pixels, size_t pixel_count, float decay_r,
                               float decay_g, float decay_b, bool use_streaming)
#else
__attribute__((target("avx2"))) static void c64_afterglow_avx2(uint32_t *acc, const uint32_t *curr_pixels,
                                                               size_t pixel_count, float decay_r, float decay_g,
                                                               float decay_b, bool use_streaming)
#endif
{
    static uint32_t afterglow_idle_spins = 0;

    // Broadcast decay factors to all 8 lanes
    afterglow_idle_spins++;
    if (afterglow_idle_spins < 1000) {
        os_sleep_ms(0);
    } else {
        os_sleep_ms(1);
        afterglow_idle_spins = 0;
    }
    const __m256 vdecay_r = _mm256_set1_ps(decay_r);
    const __m256 vdecay_g = _mm256_set1_ps(decay_g);
    const __m256 vdecay_b = _mm256_set1_ps(decay_b);
    const __m256 v255 = _mm256_set1_ps(255.0f);
    const __m256i vmask_channel = _mm256_set1_epi32(0xFF);

    afterglow_idle_spins = 0;
    const __m256i valpha = _mm256_set1_epi32(0xFF000000);

    size_t i = 0;

    // Process 16 pixels at a time (2x unroll for better ILP and reduced loop overhead)
    for (; i + 16 <= pixel_count; i += 16) {
        // ===== First 8 pixels =====
        const __m256i curr0 = _mm256_loadu_si256((const __m256i *)&curr_pixels[i]);
        const __m256i prev0 = _mm256_loadu_si256((const __m256i *)&acc[i]);

        // Extract channels
        const __m256i prev_r_i0 = _mm256_and_si256(prev0, vmask_channel);
        const __m256i curr_r_i0 = _mm256_and_si256(curr0, vmask_channel);
        const __m256i prev_g_i0 = _mm256_and_si256(_mm256_srli_epi32(prev0, 8), vmask_channel);
        const __m256i curr_g_i0 = _mm256_and_si256(_mm256_srli_epi32(curr0, 8), vmask_channel);
        const __m256i prev_b_i0 = _mm256_and_si256(_mm256_srli_epi32(prev0, 16), vmask_channel);
        const __m256i curr_b_i0 = _mm256_and_si256(_mm256_srli_epi32(curr0, 16), vmask_channel);

        // Convert to float
        const __m256 prev_r0 = _mm256_cvtepi32_ps(prev_r_i0);
        const __m256 prev_g0 = _mm256_cvtepi32_ps(prev_g_i0);
        const __m256 prev_b0 = _mm256_cvtepi32_ps(prev_b_i0);
        const __m256 curr_r0 = _mm256_cvtepi32_ps(curr_r_i0);
        const __m256 curr_g0 = _mm256_cvtepi32_ps(curr_g_i0);
        const __m256 curr_b0 = _mm256_cvtepi32_ps(curr_b_i0);

        // ===== Second 8 pixels (overlapped with first for ILP) =====
        const __m256i curr1 = _mm256_loadu_si256((const __m256i *)&curr_pixels[i + 8]);
        const __m256i prev1 = _mm256_loadu_si256((const __m256i *)&acc[i + 8]);

        // Extract channels
        const __m256i prev_r_i1 = _mm256_and_si256(prev1, vmask_channel);
        const __m256i curr_r_i1 = _mm256_and_si256(curr1, vmask_channel);
        const __m256i prev_g_i1 = _mm256_and_si256(_mm256_srli_epi32(prev1, 8), vmask_channel);
        const __m256i curr_g_i1 = _mm256_and_si256(_mm256_srli_epi32(curr1, 8), vmask_channel);
        const __m256i prev_b_i1 = _mm256_and_si256(_mm256_srli_epi32(prev1, 16), vmask_channel);
        const __m256i curr_b_i1 = _mm256_and_si256(_mm256_srli_epi32(curr1, 16), vmask_channel);

        // Convert to float
        const __m256 prev_r1 = _mm256_cvtepi32_ps(prev_r_i1);
        const __m256 prev_g1 = _mm256_cvtepi32_ps(prev_g_i1);
        const __m256 prev_b1 = _mm256_cvtepi32_ps(prev_b_i1);
        const __m256 curr_r1 = _mm256_cvtepi32_ps(curr_r_i1);
        const __m256 curr_g1 = _mm256_cvtepi32_ps(curr_g_i1);
        const __m256 curr_b1 = _mm256_cvtepi32_ps(curr_b_i1);

        // ===== Apply decay for first 8 pixels =====
        const __m256 trail_r0 = _mm256_mul_ps(prev_r0, vdecay_r);
        const __m256 trail_g0 = _mm256_mul_ps(prev_g0, vdecay_g);
        const __m256 trail_b0 = _mm256_mul_ps(prev_b0, vdecay_b);
        __m256 out_r0 = _mm256_min_ps(_mm256_max_ps(curr_r0, trail_r0), v255);
        __m256 out_g0 = _mm256_min_ps(_mm256_max_ps(curr_g0, trail_g0), v255);
        __m256 out_b0 = _mm256_min_ps(_mm256_max_ps(curr_b0, trail_b0), v255);

        // ===== Apply decay for second 8 pixels =====
        const __m256 trail_r1 = _mm256_mul_ps(prev_r1, vdecay_r);
        const __m256 trail_g1 = _mm256_mul_ps(prev_g1, vdecay_g);
        const __m256 trail_b1 = _mm256_mul_ps(prev_b1, vdecay_b);
        __m256 out_r1 = _mm256_min_ps(_mm256_max_ps(curr_r1, trail_r1), v255);
        __m256 out_g1 = _mm256_min_ps(_mm256_max_ps(curr_g1, trail_g1), v255);
        __m256 out_b1 = _mm256_min_ps(_mm256_max_ps(curr_b1, trail_b1), v255);

        // ===== Pack and store first 8 pixels =====
        const __m256i out_r_i0 = _mm256_cvttps_epi32(out_r0);
        const __m256i out_g_i0 = _mm256_cvttps_epi32(out_g0);
        const __m256i out_b_i0 = _mm256_cvttps_epi32(out_b0);
        const __m256i rgb0 =
            _mm256_or_si256(out_r_i0, _mm256_or_si256(_mm256_slli_epi32(out_g_i0, 8), _mm256_slli_epi32(out_b_i0, 16)));
        const __m256i result0 = _mm256_or_si256(rgb0, valpha);
        if (use_streaming) {
            _mm256_stream_si256((__m256i *)&acc[i], result0);
        } else {
            _mm256_storeu_si256((__m256i *)&acc[i], result0);
        }

        // ===== Pack and store second 8 pixels =====
        const __m256i out_r_i1 = _mm256_cvttps_epi32(out_r1);
        const __m256i out_g_i1 = _mm256_cvttps_epi32(out_g1);
        const __m256i out_b_i1 = _mm256_cvttps_epi32(out_b1);
        const __m256i rgb1 =
            _mm256_or_si256(out_r_i1, _mm256_or_si256(_mm256_slli_epi32(out_g_i1, 8), _mm256_slli_epi32(out_b_i1, 16)));
        const __m256i result1 = _mm256_or_si256(rgb1, valpha);
        if (use_streaming) {
            _mm256_stream_si256((__m256i *)&acc[i + 8], result1);
        } else {
            _mm256_storeu_si256((__m256i *)&acc[i + 8], result1);
        }
    }

    // Process remaining 8 pixels
    if (i + 8 <= pixel_count) {
        const __m256i curr = _mm256_loadu_si256((const __m256i *)&curr_pixels[i]);
        const __m256i prev = _mm256_loadu_si256((const __m256i *)&acc[i]);

        const __m256i prev_r_i = _mm256_and_si256(prev, vmask_channel);
        const __m256i curr_r_i = _mm256_and_si256(curr, vmask_channel);
        const __m256i prev_g_i = _mm256_and_si256(_mm256_srli_epi32(prev, 8), vmask_channel);
        const __m256i curr_g_i = _mm256_and_si256(_mm256_srli_epi32(curr, 8), vmask_channel);
        const __m256i prev_b_i = _mm256_and_si256(_mm256_srli_epi32(prev, 16), vmask_channel);
        const __m256i curr_b_i = _mm256_and_si256(_mm256_srli_epi32(curr, 16), vmask_channel);

        const __m256 prev_r = _mm256_cvtepi32_ps(prev_r_i);
        const __m256 prev_g = _mm256_cvtepi32_ps(prev_g_i);
        const __m256 prev_b = _mm256_cvtepi32_ps(prev_b_i);
        const __m256 curr_r = _mm256_cvtepi32_ps(curr_r_i);
        const __m256 curr_g = _mm256_cvtepi32_ps(curr_g_i);
        const __m256 curr_b = _mm256_cvtepi32_ps(curr_b_i);

        const __m256 trail_r = _mm256_mul_ps(prev_r, vdecay_r);
        const __m256 trail_g = _mm256_mul_ps(prev_g, vdecay_g);
        const __m256 trail_b = _mm256_mul_ps(prev_b, vdecay_b);
        __m256 out_r = _mm256_min_ps(_mm256_max_ps(curr_r, trail_r), v255);
        __m256 out_g = _mm256_min_ps(_mm256_max_ps(curr_g, trail_g), v255);
        __m256 out_b = _mm256_min_ps(_mm256_max_ps(curr_b, trail_b), v255);

        const __m256i out_r_i = _mm256_cvttps_epi32(out_r);
        const __m256i out_g_i = _mm256_cvttps_epi32(out_g);
        const __m256i out_b_i = _mm256_cvttps_epi32(out_b);
        const __m256i rgb =
            _mm256_or_si256(out_r_i, _mm256_or_si256(_mm256_slli_epi32(out_g_i, 8), _mm256_slli_epi32(out_b_i, 16)));
        const __m256i result = _mm256_or_si256(rgb, valpha);
        if (use_streaming) {
            _mm256_stream_si256((__m256i *)&acc[i], result);
        } else {
            _mm256_storeu_si256((__m256i *)&acc[i], result);
        }
        i += 8;
    }

    // SSE2 for remaining 4-7 pixels
    if (i + 4 <= pixel_count) {
        const __m128 vdecay_r_128 = _mm_set1_ps(decay_r);
        const __m128 vdecay_g_128 = _mm_set1_ps(decay_g);
        const __m128 vdecay_b_128 = _mm_set1_ps(decay_b);
        const __m128 v255_128 = _mm_set1_ps(255.0f);
        const __m128i vmask_128 = _mm_set1_epi32(0xFF);
        const __m128i valpha_128 = _mm_set1_epi32(0xFF000000);

        const __m128i curr = _mm_loadu_si128((const __m128i *)&curr_pixels[i]);
        const __m128i prev = _mm_loadu_si128((const __m128i *)&acc[i]);

        const __m128i prev_r_i = _mm_and_si128(prev, vmask_128);
        const __m128i curr_r_i = _mm_and_si128(curr, vmask_128);
        const __m128i prev_g_i = _mm_and_si128(_mm_srli_epi32(prev, 8), vmask_128);
        const __m128i curr_g_i = _mm_and_si128(_mm_srli_epi32(curr, 8), vmask_128);
        const __m128i prev_b_i = _mm_and_si128(_mm_srli_epi32(prev, 16), vmask_128);
        const __m128i curr_b_i = _mm_and_si128(_mm_srli_epi32(curr, 16), vmask_128);

        const __m128 prev_r = _mm_cvtepi32_ps(prev_r_i);
        const __m128 prev_g = _mm_cvtepi32_ps(prev_g_i);
        const __m128 prev_b = _mm_cvtepi32_ps(prev_b_i);
        const __m128 curr_r = _mm_cvtepi32_ps(curr_r_i);
        const __m128 curr_g = _mm_cvtepi32_ps(curr_g_i);
        const __m128 curr_b = _mm_cvtepi32_ps(curr_b_i);

        __m128 out_r = _mm_min_ps(_mm_max_ps(curr_r, _mm_mul_ps(prev_r, vdecay_r_128)), v255_128);
        __m128 out_g = _mm_min_ps(_mm_max_ps(curr_g, _mm_mul_ps(prev_g, vdecay_g_128)), v255_128);
        __m128 out_b = _mm_min_ps(_mm_max_ps(curr_b, _mm_mul_ps(prev_b, vdecay_b_128)), v255_128);

        const __m128i out_r_i = _mm_cvttps_epi32(out_r);
        const __m128i out_g_i = _mm_cvttps_epi32(out_g);
        const __m128i out_b_i = _mm_cvttps_epi32(out_b);

        const __m128i rgb =
            _mm_or_si128(out_r_i, _mm_or_si128(_mm_slli_epi32(out_g_i, 8), _mm_slli_epi32(out_b_i, 16)));
        _mm_storeu_si128((__m128i *)&acc[i], _mm_or_si128(rgb, valpha_128));
        i += 4;
    }

    // Scalar tail for remaining 0-3 pixels
    for (; i < pixel_count; i++) {
        const uint32_t curr = curr_pixels[i];
        const uint32_t prev = acc[i];

        const float pr = (float)((prev >> 0) & 0xFF);
        const float pg = (float)((prev >> 8) & 0xFF);
        const float pb = (float)((prev >> 16) & 0xFF);

        const float cr = (float)((curr >> 0) & 0xFF);
        const float cg = (float)((curr >> 8) & 0xFF);
        const float cb = (float)((curr >> 16) & 0xFF);

        float or_ = (cr > pr * decay_r) ? cr : pr * decay_r;
        float og_ = (cg > pg * decay_g) ? cg : pg * decay_g;
        float ob_ = (cb > pb * decay_b) ? cb : pb * decay_b;

        if (or_ > 255.0f)
            or_ = 255.0f;
        if (og_ > 255.0f)
            og_ = 255.0f;
        if (ob_ > 255.0f)
            ob_ = 255.0f;

        acc[i] = 0xFF000000 | ((uint32_t)ob_ << 16) | ((uint32_t)og_ << 8) | ((uint32_t)or_);
    }

    // AVX-VEX transition: zero upper YMM to avoid performance penalty
    _mm256_zeroupper();

    // Memory fence to ensure all non-temporal stores complete
    if (use_streaming) {
        _mm_sfence();
    }
}
#endif // __AVX2__ || _MSC_VER

#endif // C64_HAS_X86_SIMD
#endif // C64_AFTERGLOW_MOVED

// Forward declarations
static uint64_t c64_calculate_ideal_timestamp(struct c64_source *context, uint16_t frame_num);

static const uint32_t *c64_get_afterglow_output_pixels(struct c64_source *context, const uint32_t *curr_pixels,
                                                       size_t pixel_count)
{
    if (!context || !curr_pixels || pixel_count == 0)
        return curr_pixels;

    if (!(context->afterglow_enable && context->afterglow.duration_ms > 0))
        return curr_pixels;

    // Use the detected frame interval (PAL/NTSC) for stable dt.
    float dt_ms = c64_afterglow_nominal_dt_ms(context->frame_interval_ns, context->expected_fps);
    return c64_afterglow_apply(&context->afterglow, curr_pixels, pixel_count, dt_ms);
}

// Helper functions for frame assembly (updated to use lock-free implementation)
void c64_init_frame_assembly(struct frame_assembly *frame, uint16_t frame_num)
{
    memset(frame, 0, sizeof(struct frame_assembly));
    frame->frame_num = frame_num;
    frame->start_time = os_gettime_ns();
    // last_packet_time is 0 initially, will be set when first packet arrives
    frame->received_packets = 0;
    frame->expected_packets = 0;
    frame->complete = false;
    frame->packets_received_mask = 0;
}

bool c64_is_frame_complete(struct frame_assembly *frame)
{
    uint16_t received = frame->received_packets;
    uint16_t expected = frame->expected_packets;

    if (expected == 0) {
        return false;
    }

    bool complete = (received >= expected);

    static uint16_t last_debug_frame = 0;
    static uint64_t last_debug_time = 0;
    uint64_t now = os_gettime_ns();

    if (frame->frame_num != last_debug_frame && received > 0 &&
        (last_debug_time == 0 || (now - last_debug_time) > 1000000000ULL)) {
        C64_LOG_DEBUG("" VIDEO_LOG_PREFIX " 🎬 Frame completion check: frame %u has %u/%u packets (complete=%d)",
                      frame->frame_num, received, expected, complete);
        last_debug_frame = frame->frame_num;
        last_debug_time = now;
    }

    if (complete && !frame->complete) {
        frame->complete = true;
        // Periodic frame completion monitoring (every 5 minutes)
        static int completion_debug_count = 0;
        static uint64_t last_completion_log_time = 0;
        uint64_t now = os_gettime_ns();
        if ((++completion_debug_count % 5000) == 0 ||
            (now - last_completion_log_time >= 300000000000ULL)) { // Every 5k completions OR 5 minutes
            C64_LOG_DEBUG("" VIDEO_LOG_PREFIX
                          " 🎬 Frame COMPLETION SPOT CHECK: frame %u with %u/%u packets! (total count: %d)",
                          frame->frame_num, received, expected, completion_debug_count);
            last_completion_log_time = now;
        }
    }

    return complete;
}

bool c64_is_frame_timeout(struct frame_assembly *frame)
{
    uint64_t elapsed = (os_gettime_ns() - frame->start_time) / 1000000; // Convert to ms
    return elapsed > C64_FRAME_TIMEOUT_MS;
}

static bool c64_debug_frame_is_all_white_fast_probe(const uint32_t *pixels, uint32_t width, uint32_t height)
{
    // Ultra-fast video pop detection for debug/testing.
    // A pop marker is expected to be very bright; we classify "white" as RGB >= 0xE0 (alpha ignored).
    // Probe two tiny 3x3 areas:
    //  - Center: catches full-frame flashes.
    //  - ~Bottom-right of the content: catches small pop markers embedded in the scene.
    if (!pixels || width == 0 || height == 0) {
        return false;
    }

    const uint32_t probe_x[2] = {
        width / 2,
        (uint32_t)(((uint64_t)width * 78ULL) / 100ULL),
    };
    const uint32_t probe_y[2] = {
        height / 2,
        (uint32_t)(((uint64_t)height * 80ULL) / 100ULL),
    };

    for (size_t p = 0; p < 2; p++) {
        size_t white_count = 0;
        size_t probed = 0;

        for (int dy = -1; dy <= 1; dy++) {
            int y = (int)probe_y[p] + dy;
            if (y < 0 || y >= (int)height) {
                continue;
            }
            for (int dx = -1; dx <= 1; dx++) {
                int x = (int)probe_x[p] + dx;
                if (x < 0 || x >= (int)width) {
                    continue;
                }

                const uint32_t rgb = pixels[(size_t)y * (size_t)width + (size_t)x] & 0x00FFFFFF;
                const uint8_t r = (rgb >> 16) & 0xFF;
                const uint8_t g = (rgb >> 8) & 0xFF;
                const uint8_t b = (rgb >> 0) & 0xFF;

                probed++;
                if (r >= 0xE0 && g >= 0xE0 && b >= 0xE0) {
                    white_count++;
                }
            }
        }

        if (probed == 0) {
            continue;
        }

        const size_t threshold_count = (size_t)(probed * 0.8);
        if (white_count > threshold_count) {
            return true;
        }
    }

    return false;
}

// Direct frame rendering with row interpolation for missing packets
void c64_render_frame_direct(struct c64_source *context, struct frame_assembly *frame, uint64_t timestamp_ns)
{
    // First, assemble the frame with interpolation for missing rows
    c64_assemble_frame_with_interpolation(context, frame);

    // Generate monotonic synthetic timestamp based on a shared stream_start_ns and frame index.
    uint64_t monotonic_timestamp = c64_calculate_ideal_timestamp(context, frame->frame_num);
    if (monotonic_timestamp == 0) {
        // If we cannot generate a synthetic timestamp yet, do not submit video with a zero timestamp.
        return;
    }

    // Apply afterglow in the video thread (prevents races/flicker with raw frame_buffer).
    // This is ONLY for OBS display/streaming, NOT for recording.
    const size_t pixel_count = (size_t)context->width * (size_t)context->height;
    const uint32_t *out_pixels = c64_get_afterglow_output_pixels(context, context->frame_buffer, pixel_count);

    // Check for video pops (debug/testing mode)
    bool is_all_white = false;
    bool video_pop_rise = false;
    if (c64_debug_logging) {
        is_all_white = c64_debug_frame_is_all_white_fast_probe(out_pixels, context->width, context->height);
        const bool was_all_white = context->av_sync_last_video_all_white;
        video_pop_rise = (!was_all_white && is_all_white);
    }
    context->av_sync_last_video_all_white = is_all_white;

    // Save RAW frame to disk if enabled (NO effects applied)
    if (context->record_frames) {
        c64_save_frame_as_bmp(context, context->frame_buffer);

        // Note: CSV logging for video events is now handled independently in the video processor thread
    }

    // Record RAW frame to video file if recording is enabled (NO effects applied)
    if (context->record_video) {
        c64_record_video_frame(context, context->frame_buffer);
    }

    // Direct async video output - optimized for low latency
    // This ensures the source always shows video regardless of CRT effects
    struct obs_source_frame obs_frame = {0};

    // Set up frame data - RGBA format optimized for immediate display
    obs_frame.data[0] = (uint8_t *)out_pixels;
    obs_frame.linesize[0] = context->width * 4; // 4 bytes per pixel (RGBA)
    obs_frame.width = context->width;
    obs_frame.height = context->height;
    obs_frame.format = VIDEO_FORMAT_RGBA;
    obs_frame.timestamp = monotonic_timestamp; // Synthetic timestamp for smooth low-latency playback
    obs_frame.flip = false;                    // No vertical flip needed

    // Output frame directly to OBS
    obs_source_output_video(context->source, &obs_frame);
    context->last_video_submit_ns = os_gettime_ns();
    context->last_video_ts_ns = monotonic_timestamp;

    if (!context->first_video_ts_logged) {
        context->first_video_ts_logged = true;
        context->first_video_ts_ns = monotonic_timestamp;

        int64_t initial_delta = 0;
        if (context->first_audio_ts_logged) {
            initial_delta = (int64_t)context->first_audio_ts_ns - (int64_t)context->first_video_ts_ns;
        }

        C64_LOG_INFO("" VIDEO_LOG_PREFIX " FIRST VIDEO SUBMIT: stream_start_ns=%" PRIu64 " first_video_ts_ns=%" PRIu64
                     " first_audio_ts_ns=%" PRIu64 " initial_audio_minus_video_delta_ns=%" PRId64,
                     context->stream_start_ns, context->first_video_ts_ns, context->first_audio_ts_ns, initial_delta);

        if (context->first_audio_ts_logged && !context->initial_av_delta_logged) {
            context->initial_av_delta_logged = true;
        }
    }

    // Emit AV-sync pop only once it's been handed off to OBS.
    if (c64_debug_logging && video_pop_rise) {
        c64_av_sync_on_video_pop(context, C64_AV_SYNC_ORIGIN_OBS, frame->frame_num, context->last_video_submit_ns);
    }

    // Log video frame delivery to CSV if enabled (high-level event: complete frame delivered to OBS)
    if (context->timing_file) {
        size_t frame_size = context->width * context->height * 4; // RGBA bytes
        c64_obs_log_video_event(context, frame->frame_num, frame_size, is_all_white);
    }

    // Update timing and status
    context->last_frame_time = monotonic_timestamp;
    context->frames_delivered_to_obs++;
    os_atomic_set_long(&context->video_frames_processed, os_atomic_load_long(&context->video_frames_processed) + 1);

    // Periodic timestamp debugging (every 5 minutes)
    static int timestamp_debug_count = 0;
    static uint64_t last_timestamp_log_time = 0;
    uint64_t now = os_gettime_ns();
    if ((++timestamp_debug_count % 10000) == 0 ||
        (now - last_timestamp_log_time >= 300000000000ULL)) { // Every 10k frames OR 5 minutes
        C64_LOG_DEBUG("" VIDEO_LOG_PREFIX " 🎬 MONOTONIC SPOT CHECK: frame=%u, monotonic_ts=%" PRIu64
                      ", packet_ts=%" PRIu64 ", delta=%+" PRId64 ", packets=%u/%u (count: %d)",
                      frame->frame_num, monotonic_timestamp, timestamp_ns,
                      (int64_t)(monotonic_timestamp - timestamp_ns), frame->received_packets, frame->expected_packets,
                      timestamp_debug_count);
        last_timestamp_log_time = now;
    }
}

// Simplified frame assembly with row interpolation for missing packets
// Optimized: uses stack-allocated line tracking to avoid malloc/free per frame
void c64_assemble_frame_with_interpolation(struct c64_source *context, struct frame_assembly *frame)
{
    // Periodic frame assembly monitoring (every 5 minutes)
    static int assembly_debug_count = 0;
    static uint64_t last_assembly_log_time = 0;
    uint64_t now = os_gettime_ns();
    if ((++assembly_debug_count % 5000) == 0 ||
        (now - last_assembly_log_time >= 300000000000ULL)) { // Every 5k frames OR 5 minutes
        C64_LOG_DEBUG("" VIDEO_LOG_PREFIX " 🎬 ASSEMBLY SPOT CHECK: frame %u with %u/%u packets (count: %d)",
                      frame->frame_num, frame->received_packets, frame->expected_packets, assembly_debug_count);
        last_assembly_log_time = now;
    }

    // Stack-allocated line tracking (max 272 lines for PAL, fits easily on stack)
    // Using uint8_t instead of bool for guaranteed 1-byte size
    uint8_t line_written[272] = {0}; // Zero-initialized on stack
    const uint32_t height = context->height;
    uint32_t lines_written_count = 0;

    // C64STR-014: snapshot this source's own colour LUT once per frame under
    // the palette lock, then convert lock-free against the local copy. A
    // concurrent palette edit is fully applied or not at all, so the whole
    // frame renders with one consistent palette and never a torn table.
    uint64_t lut[256];
    pthread_mutex_lock(&context->palette_mutex);
    c64_color_lut_snapshot(&context->color_lut, lut);
    pthread_mutex_unlock(&context->palette_mutex);

    if (height > 272) {
        C64_LOG_ERROR("" VIDEO_LOG_PREFIX " Frame height %u exceeds maximum 272", height);
        return;
    }

    // First pass: assemble all received packets
    for (int i = 0; i < C64_MAX_PACKETS_PER_FRAME; i++) {
        struct frame_packet *packet = &frame->packets[i];
        if (!packet->received)
            continue;

        uint16_t line_num = packet->line_num;
        uint8_t lines_per_packet = packet->lines_per_packet;

        for (int line = 0; line < (int)lines_per_packet && (int)(line_num + line) < (int)height; line++) {
            uint32_t current_line = line_num + line;
            if (current_line >= height)
                break;

            uint32_t dst_line_offset = current_line * C64_PIXELS_PER_LINE;
            uint32_t *dst_line = context->frame_buffer + dst_line_offset;
            uint8_t *src_line = packet->packet_data + (line * C64_BYTES_PER_LINE);

            c64_convert_pixels_optimized(lut, src_line, dst_line, C64_BYTES_PER_LINE);
            if (!line_written[current_line]) {
                line_written[current_line] = 1;
                lines_written_count++;
            }
        }
    }

    if (lines_written_count == height) {
        return;
    }

    // Second pass: interpolate missing lines by duplicating the nearest valid line above.
    // O(height) by tracking the last valid line as we scan top-to-bottom.
    bool have_last_valid = false;
    uint32_t last_valid_line = 0;
    for (uint32_t line = 0; line < height; line++) {
        if (line_written[line]) {
            last_valid_line = line;
            have_last_valid = true;
            continue;
        }

        const uint32_t source_line = have_last_valid ? last_valid_line : 0;
        uint32_t *dst = context->frame_buffer + (line * C64_PIXELS_PER_LINE);
        uint32_t *src = context->frame_buffer + (source_line * C64_PIXELS_PER_LINE);
        memcpy(dst, src, C64_PIXELS_PER_LINE * sizeof(uint32_t));
    }
}

void c64_process_video_statistics_batch(struct c64_source *context, uint64_t current_time)
{
    static const uint64_t STATS_INTERVAL_NS = 5000000000ULL;

    uint64_t time_since_last_log = current_time - context->last_stats_log_time;
    if (time_since_last_log < STATS_INTERVAL_NS) {
        return;
    }
    uint64_t packets_received = (uint64_t)os_atomic_load_long(&context->video_packets_received);
    uint64_t bytes_received = (uint64_t)os_atomic_load_long(&context->video_bytes_received);
    uint32_t frames_processed = (uint32_t)os_atomic_load_long(&context->video_frames_processed);
    os_atomic_set_long(&context->video_packets_received, 0);
    os_atomic_set_long(&context->video_bytes_received, 0);
    os_atomic_set_long(&context->video_frames_processed, 0);

    double duration_seconds = time_since_last_log / 1000000000.0;
    double packets_per_second = packets_received / duration_seconds;
    double bandwidth_mbps = (bytes_received * 8.0) / (duration_seconds * 1000000.0);
    double frames_per_second = frames_processed / duration_seconds;
    // No loss percentage calculation - we don't track sequence errors anymore

    double expected_fps = context->format_detected ? context->expected_fps : 50.0;
    double frame_delivery_rate = context->frames_delivered_to_obs / duration_seconds;
    double frame_completion_rate = context->frames_completed / duration_seconds;
    double capture_drop_pct =
        context->frames_expected > 0
            ? (100.0 * (context->frames_expected - context->frames_captured)) / context->frames_expected
            : 0.0;
    double delivery_drop_pct =
        context->frames_completed > 0
            ? (100.0 * (context->frames_completed - context->frames_delivered_to_obs)) / context->frames_completed
            : 0.0;
    double avg_pipeline_latency = context->frames_delivered_to_obs > 0
                                      ? context->total_pipeline_latency / (context->frames_delivered_to_obs * 1000000.0)
                                      : 0.0;

    // Load and reset debug counters
    long recv_calls = os_atomic_load_long(&context->debug_recvfrom_calls);

    os_atomic_set_long(&context->debug_recvfrom_calls, 0);
    os_atomic_set_long(&context->debug_recvfrom_eagain, 0);
    os_atomic_set_long(&context->debug_recvfrom_bytes_total, 0);
    os_atomic_set_long(&context->debug_packets_dropped_size, 0);

    if (packets_received > 0 || recv_calls > 0) {
        const uint64_t stream_start_ns = os_atomic_load_bool(&context->stream_start_set) ? context->stream_start_ns : 0;
        const uint64_t current_video_ts_ns = context->last_video_ts_ns;
        const uint64_t video_ts_minus_stream_start_ns = (stream_start_ns != 0 && current_video_ts_ns >= stream_start_ns)
                                                            ? (current_video_ts_ns - stream_start_ns)
                                                            : 0;

        double video_buffer_depth_ms = 0.0;
        if (context->network_buffer) {
            const size_t v_pkts = c64_network_buffer_get_video_packet_count(context->network_buffer);
            video_buffer_depth_ms = ((double)v_pkts * 1000.0) / (double)C64_MAX_VIDEO_RATE;
        }

        C64_LOG_DEBUG("" VIDEO_LOG_PREFIX " %.1f fps | %.2f Mbps | %.0f pps | Frames: %u | current_video_ts_ns=%" PRIu64
                      " video_ts_minus_stream_start_ns=%" PRIu64 " video_buf_depth_ms=%.1f",
                      frames_per_second, bandwidth_mbps, packets_per_second, (uint32_t)frames_processed,
                      current_video_ts_ns, video_ts_minus_stream_start_ns, video_buffer_depth_ms);
        C64_LOG_DEBUG("" VIDEO_LOG_PREFIX
                      " Expected %.0f fps | Captured %.1f fps | Delivered %.1f fps | Completed %.1f fps",
                      expected_fps, context->frames_captured / duration_seconds, frame_delivery_rate,
                      frame_completion_rate);
        C64_LOG_DEBUG("" VIDEO_LOG_PREFIX " Capture drops %.1f%% | Delivery drops %.1f%% | Avg latency %.1f ms",
                      capture_drop_pct, delivery_drop_pct, avg_pipeline_latency);
    }

    // Reset diagnostic counters and update timestamp
    context->frames_expected = 0;
    context->frames_captured = 0;
    context->frames_delivered_to_obs = 0;
    context->frames_completed = 0;
    context->total_pipeline_latency = 0;
    context->last_stats_log_time = current_time;
}

void c64_process_audio_statistics_batch(struct c64_source *context, uint64_t current_time)
{
    static const uint64_t STATS_INTERVAL_NS = 5000000000ULL;

    uint64_t time_since_last_log = current_time - context->last_audio_stats_log_time;
    if (time_since_last_log < STATS_INTERVAL_NS) {
        return;
    }
    uint64_t packets_received = (uint64_t)os_atomic_load_long(&context->audio_packets_received);
    uint64_t bytes_received = (uint64_t)os_atomic_load_long(&context->audio_bytes_received);
    os_atomic_set_long(&context->audio_packets_received, 0);
    os_atomic_set_long(&context->audio_bytes_received, 0);

    if (packets_received > 0) {
        double duration_seconds = time_since_last_log / 1000000000.0;
        double packets_per_second = packets_received / duration_seconds;
        double bandwidth_mbps = (bytes_received * 8.0) / (duration_seconds * 1000000.0);

        const uint64_t stream_start_ns = os_atomic_load_bool(&context->stream_start_set) ? context->stream_start_ns : 0;
        const uint64_t current_audio_ts_ns = context->last_audio_ts_ns;
        const uint64_t audio_ts_minus_stream_start_ns = (stream_start_ns != 0 && current_audio_ts_ns >= stream_start_ns)
                                                            ? (current_audio_ts_ns - stream_start_ns)
                                                            : 0;

        int64_t audio_minus_video_delta_ns = 0;
        if (context->last_video_ts_ns != 0 && context->last_audio_ts_ns != 0) {
            audio_minus_video_delta_ns = (int64_t)context->last_audio_ts_ns - (int64_t)context->last_video_ts_ns;
        }

        double audio_buffer_depth_ms = 0.0;
        if (context->network_buffer) {
            const size_t a_pkts = c64_network_buffer_get_audio_packet_count(context->network_buffer);
            audio_buffer_depth_ms = ((double)a_pkts * 1000.0) / (double)C64_MAX_AUDIO_RATE;
        }

        C64_LOG_DEBUG("" AUDIO_LOG_PREFIX " %.2f Mbps | %.0f pps | Packets: %llu | current_audio_ts_ns=%" PRIu64
                      " audio_ts_minus_stream_start_ns=%" PRIu64 " audio_minus_video_delta_ns=%" PRId64
                      " audio_buf_depth_ms=%.1f",
                      bandwidth_mbps, packets_per_second, (unsigned long long)packets_received, current_audio_ts_ns,
                      audio_ts_minus_stream_start_ns, audio_minus_video_delta_ns, audio_buffer_depth_ms);
    }

    // Update audio stats timestamp separately from video
    context->last_audio_stats_log_time = current_time;
}

bool c64_try_add_packet_lockfree(struct frame_assembly *frame, uint16_t packet_index)
{
    if (packet_index >= C64_MAX_PACKETS_PER_FRAME) {
        return false;
    }

    uint64_t packet_mask = 1ULL << packet_index;
    uint64_t old_mask = frame->packets_received_mask;
    frame->packets_received_mask |= packet_mask;

    if (old_mask & packet_mask) {
        return false;
    }

    frame->received_packets++;
    return true;
}

// Video thread function
void *c64_video_thread_func(void *data)
{
    struct c64_source *context = data;
    uint32_t wouldblock_spins = 0;

    C64_LOG_DEBUG("" VIDEO_LOG_PREFIX " Video receiver thread started on port %u", context->video_port);

#ifdef _WIN32
    HANDLE thread_handle = GetCurrentThread();
    if (SetThreadPriority(thread_handle, THREAD_PRIORITY_ABOVE_NORMAL)) {
        C64_LOG_DEBUG("" VIDEO_LOG_PREFIX " Set video receiver thread to above-normal priority on Windows");
    } else {
        C64_LOG_WARNING("" VIDEO_LOG_PREFIX " Failed to set video receiver thread priority on Windows");
    }

    timeBeginPeriod(1);
#else
    // Best-effort priority boost on POSIX (no noise on failure)
    struct sched_param param;
    param.sched_priority = sched_get_priority_max(SCHED_OTHER);
    pthread_setschedparam(pthread_self(), SCHED_OTHER, &param);
#endif

#ifdef _WIN32
    // Set socket receive timeout to reduce blocking jitter (Windows only)
    DWORD timeout_ms = 10;
    setsockopt(context->video_socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout_ms, sizeof(timeout_ms));
#endif

    C64_LOG_DEBUG("" VIDEO_LOG_PREFIX " Video thread function started with optimized scheduling");

#ifdef __linux__
// Batch read optimization for Linux: receive up to 8 packets per syscall
#define BATCH_SIZE 64
    uint8_t packet_batch[BATCH_SIZE][C64_VIDEO_PACKET_SIZE];
    struct mmsghdr msgs[BATCH_SIZE];
    struct iovec iovs[BATCH_SIZE];
    struct sockaddr_in addrs[BATCH_SIZE];

    memset(msgs, 0, sizeof(msgs));
    for (int i = 0; i < BATCH_SIZE; i++) {
        iovs[i].iov_base = packet_batch[i];
        iovs[i].iov_len = C64_VIDEO_PACKET_SIZE;
        msgs[i].msg_hdr.msg_iov = &iovs[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
        msgs[i].msg_hdr.msg_name = &addrs[i];
        msgs[i].msg_hdr.msg_namelen = sizeof(struct sockaddr_in);
    }
    C64_LOG_DEBUG("" VIDEO_LOG_PREFIX " Linux batch recv enabled (recvmmsg with up to %d packets per syscall)",
                  BATCH_SIZE);
#endif

    while (os_atomic_load_bool(&context->thread_active)) {
        // Check socket validity before each recv call (prevents Windows WSAENOTSOCK errors)
        if (context->video_socket == INVALID_SOCKET_VALUE) {
            os_sleep_ms(10); // Wait a bit before checking again
            continue;
        }

#ifdef __linux__
        // Reset message headers to prevent size/address corruption/truncation across calls
        for (int i = 0; i < BATCH_SIZE; i++) {
            msgs[i].msg_hdr.msg_namelen = sizeof(struct sockaddr_in);
            iovs[i].iov_len = C64_VIDEO_PACKET_SIZE;
        }

        // Linux: batch read with recvmmsg (reduces syscall overhead)
        // Use MSG_DONTWAIT to drain socket buffer as fast as possible without waiting for full batch
        int num_msgs = recvmmsg(context->video_socket, msgs, BATCH_SIZE, MSG_DONTWAIT, NULL);

        if (num_msgs < 0) {
            int error = c64_get_socket_error();
            if (error == EAGAIN || error == EWOULDBLOCK) {
                os_atomic_inc_long(&context->debug_recvfrom_eagain);
                // Socket empty - mostly yield without adding millisecond-scale receive jitter.
                // After long idle, occasionally sleep 1ms to avoid burning CPU.
                wouldblock_spins++;
                if (wouldblock_spins < 1000) {
                    os_sleep_ms(0);
                } else {
                    os_sleep_ms(1);
                    wouldblock_spins = 0;
                }
                continue;
            }
            if (error == EBADF && context->video_socket == INVALID_SOCKET_VALUE) {

                C64_LOG_DEBUG("" VIDEO_LOG_PREFIX " Video socket closed (EBADF) - exiting receiver thread gracefully");
                break;
            }
            C64_LOG_ERROR("" VIDEO_LOG_PREFIX " Video socket error: %s (error code: %d)",
                          c64_get_socket_error_string(error), error);
            break;
        }

        wouldblock_spins = 0;

        // Count just one "call" for the batch (approximating overhead)
        os_atomic_inc_long(&context->debug_recvfrom_calls);

        // Process each received packet
        for (int i = 0; i < num_msgs; i++) {
            ssize_t received = msgs[i].msg_len;
            uint8_t *packet = packet_batch[i];

            // Ingest ownership filter: drop packets from a sender that is not
            // the expected peer (e.g. an abandoned device still streaming).
            if (!c64_packet_from_expected_peer(context, &addrs[i])) {
                os_atomic_inc_long(&context->debug_packets_dropped_peer);
                if ((os_atomic_load_long(&context->debug_packets_dropped_peer) & 0x3FF) == 0) {
                    C64_LOG_DEBUG("" VIDEO_LOG_PREFIX " dropped packet: sender != expected peer (%ld total dropped)",
                                  os_atomic_load_long(&context->debug_packets_dropped_peer));
                }
                continue;
            }
#else
        // Non-Linux: fallback to single recvfrom
        uint8_t packet[C64_VIDEO_PACKET_SIZE];
        struct sockaddr_in sender_addr;
        socklen_t sender_len = sizeof(sender_addr);

        os_atomic_inc_long(&context->debug_recvfrom_calls);

        ssize_t received = recvfrom(context->video_socket, (char *)packet, (int)sizeof(packet), 0,
                                    (struct sockaddr *)&sender_addr, &sender_len);

        if (received < 0) {
            int error = c64_get_socket_error();
#ifdef _WIN32
            if (error == WSAEWOULDBLOCK) {
                os_atomic_inc_long(&context->debug_recvfrom_eagain);
                wouldblock_spins++;
                if (wouldblock_spins < 1000) {
                    os_sleep_ms(0);
                } else {
                    os_sleep_ms(1);
                    wouldblock_spins = 0;
                }
                continue;
            }
            // On Windows, WSAENOTSOCK means socket was closed - this is normal during shutdown
            if (error == WSAENOTSOCK && context->video_socket == INVALID_SOCKET_VALUE) {
                C64_LOG_DEBUG("" VIDEO_LOG_PREFIX
                              " Video socket closed (WSAENOTSOCK) - exiting receiver thread gracefully");
                break; // Socket was closed, exit gracefully
            }
            // On Windows, WSAESHUTDOWN means socket was shutdown - this is normal during reconnection
            if (error == WSAESHUTDOWN) {
                C64_LOG_DEBUG("" VIDEO_LOG_PREFIX " Video socket shutdown (WSAESHUTDOWN) - waiting for reconnection");
                os_sleep_ms(100); // Wait for reconnection to complete
                continue;         // Continue waiting instead of exiting thread
            }
#else
            if (error == EAGAIN || error == EWOULDBLOCK) {
                os_atomic_inc_long(&context->debug_recvfrom_eagain);
                wouldblock_spins++;
                if (wouldblock_spins < 1000) {
                    os_sleep_ms(0);
                } else {
                    os_sleep_ms(1);
                    wouldblock_spins = 0;
                }
                continue;
            }
            // On POSIX, EBADF means socket was closed - this is normal during shutdown
            if (error == EBADF && context->video_socket == INVALID_SOCKET_VALUE) {
                C64_LOG_DEBUG("" VIDEO_LOG_PREFIX " Video socket closed (EBADF) - exiting receiver thread gracefully");
                break; // Socket was closed, exit gracefully
            }
#endif
            C64_LOG_ERROR("" VIDEO_LOG_PREFIX " Video socket error: %s (error code: %d)",
                          c64_get_socket_error_string(error), error);
            break;
        }

        wouldblock_spins = 0;

        // Ingest ownership filter: drop packets from a sender that is not the
        // expected peer (e.g. an abandoned device still streaming).
        if (received > 0 && !c64_packet_from_expected_peer(context, &sender_addr)) {
            os_atomic_inc_long(&context->debug_packets_dropped_peer);
            if ((os_atomic_load_long(&context->debug_packets_dropped_peer) & 0x3FF) == 0) {
                C64_LOG_DEBUG("" VIDEO_LOG_PREFIX " dropped packet: sender != expected peer (%ld total dropped)",
                              os_atomic_load_long(&context->debug_packets_dropped_peer));
            }
            continue;
        }

        { // Scope block for shared packet processing code
#endif
            if (received > 0) {
                os_atomic_set_long(&context->debug_recvfrom_bytes_total,
                                   os_atomic_load_long(&context->debug_recvfrom_bytes_total) + (long)received);
            }

            // Stage-1: UDP ingest
            // Keep the socket receive path minimal to avoid receiver-side backpressure.
            // No parsing, no sorting, no per-packet logging, no blocking.
            if (received != C64_VIDEO_PACKET_SIZE) {
                if (received > 0) {
                    os_atomic_inc_long(&context->debug_packets_dropped_size);
                }
                continue;
            }

            const uint64_t packet_time = os_gettime_ns();
            context->last_udp_packet_time = packet_time; // DEPRECATED - kept for compatibility
            context->last_video_packet_time = packet_time;

            // Ensure shared synthetic start time is initialized on first packet receipt.
            c64_try_init_stream_start_ns(context, packet_time, "video packet recv");

            os_atomic_set_long(&context->video_packets_received,
                               os_atomic_load_long(&context->video_packets_received) + 1);
            os_atomic_set_long(&context->video_bytes_received,
                               os_atomic_load_long(&context->video_bytes_received) + (long)received);

            (void)c64_network_fifo_push(&context->video_fifo, packet, (uint16_t)received, packet_time);

#ifdef __linux__
        } // End batch packet processing loop
#else
        } // End scope block
#endif
    }

    C64_LOG_DEBUG("" VIDEO_LOG_PREFIX " Video receiver thread stopped");

#ifdef _WIN32
    timeEndPeriod(1);
#endif

    return NULL;
}

// Calculate ideal timestamp for a frame based on sequence number and video standard
static uint64_t c64_calculate_ideal_timestamp(struct c64_source *context, uint16_t frame_num)
{
    if (!context) {
        return 0;
    }

    // Shared origin must exist; if we haven't seen any packets yet, keep timestamp at 0.
    if (!os_atomic_load_bool(&context->stream_start_set) || context->stream_start_ns == 0) {
        return 0;
    }

    // Initialize on first submitted frame.
    if (!context->video_ts_frame_num_set) {
        context->last_video_ts_frame_num = frame_num;
        context->video_ts_frame_num_set = true;
        context->video_frame_index = 0;
    } else {
        // Map observed 16-bit frame numbers to a monotonic frame_index.
        // Rules:
        // - Always advance by at least 1 to avoid timestamp reuse.
        // - If frames were dropped, advance by the number of missing frames.
        int32_t frame_diff = (int32_t)frame_num - (int32_t)context->last_video_ts_frame_num;
        if (frame_diff < -32768) {
            frame_diff += 65536;
        } else if (frame_diff > 32768) {
            frame_diff -= 65536;
        }

        if (frame_diff > 0) {
            context->video_frame_index += (uint64_t)frame_diff;
            context->last_video_ts_frame_num = frame_num;
        } else {
            // Out-of-order/duplicate frame numbers should never cause timestamp reuse.
            context->video_frame_index += 1;
        }
    }

    uint64_t ideal_timestamp = context->stream_start_ns + (context->video_frame_index * context->frame_interval_ns);

    // Debug log occasionally to verify timestamp calculation
    static uint32_t log_counter = 0;
    if ((log_counter++ % 250) == 0) { // Log every 250 frames (~5 seconds at 50Hz)
        C64_LOG_DEBUG("" VIDEO_LOG_PREFIX " 📐 Ideal timestamp: frame %u -> index=%" PRIu64 " ts=%" PRIu64, frame_num,
                      context->video_frame_index, ideal_timestamp);
    }

    return ideal_timestamp;
}

static bool c64_predict_video_timestamp_for_frame(const struct c64_source *context, uint16_t frame_num,
                                                  uint64_t *out_frame_index, uint64_t *out_timestamp_ns)
{
    if (out_frame_index) {
        *out_frame_index = 0;
    }
    if (out_timestamp_ns) {
        *out_timestamp_ns = 0;
    }

    if (!context || !os_atomic_load_bool(&context->stream_start_set) || context->stream_start_ns == 0) {
        return false;
    }

    uint64_t predicted_index = 0;
    if (!context->video_ts_frame_num_set) {
        predicted_index = 0;
    } else {
        int32_t frame_diff = (int32_t)frame_num - (int32_t)context->last_video_ts_frame_num;
        if (frame_diff < -32768) {
            frame_diff += 65536;
        } else if (frame_diff > 32768) {
            frame_diff -= 65536;
        }

        predicted_index = context->video_frame_index + (uint64_t)((frame_diff > 0) ? frame_diff : 1);
    }

    const uint64_t predicted_ts = context->stream_start_ns + (predicted_index * context->frame_interval_ns);

    if (out_frame_index) {
        *out_frame_index = predicted_index;
    }
    if (out_timestamp_ns) {
        *out_timestamp_ns = predicted_ts;
    }

    return true;
}

// Render black screen fallback when no logo is available
static void c64_render_black_screen(struct c64_source *context, uint64_t timestamp_ns)
{
    if (!context->frame_buffer) {
        return;
    }

    // Fill frame buffer with black (fully transparent in RGBA)
    uint32_t *buffer = context->frame_buffer;
    uint32_t width = context->width;
    uint32_t height = context->height;

    // Black screen: 0x00000000 (fully transparent black in RGBA)
    memset(buffer, 0, width * height * sizeof(uint32_t));

    // Output black frame via async video - ALWAYS output to maintain video stream
    struct obs_source_frame obs_frame = {0};
    obs_frame.data[0] = (uint8_t *)context->frame_buffer;
    obs_frame.linesize[0] = context->width * 4; // 4 bytes per pixel (RGBA)
    obs_frame.width = context->width;
    obs_frame.height = context->height;
    obs_frame.format = VIDEO_FORMAT_RGBA;
    obs_frame.timestamp = timestamp_ns;
    obs_frame.flip = false;

    obs_source_output_video(context->source, &obs_frame);

    // Periodic black screen monitoring (every 10 minutes)
    static int black_screen_debug_count = 0;
    static uint64_t last_black_screen_log_time = 0;
    uint64_t now = os_gettime_ns();
    if ((++black_screen_debug_count % 10000) == 0 ||
        (now - last_black_screen_log_time >= 600000000000ULL)) { // Every 10k renders OR 10 minutes
        C64_LOG_DEBUG("" VIDEO_LOG_PREFIX " ⚫ BLACK SCREEN SPOT CHECK: %ux%u RGBA, timestamp=%" PRIu64
                      " (total count: %d)",
                      context->width, context->height, timestamp_ns, black_screen_debug_count);
        last_black_screen_log_time = now;
    }
}

// Direct packet processing function
void c64_process_video_packet_direct(struct c64_source *context, const uint8_t *packet, size_t packet_size,
                                     uint64_t timestamp_ns)
{
    if (!context || !packet || packet_size != C64_VIDEO_PACKET_SIZE) {
        return;
    }

    // Parse packet header with stack locals (streamlined - only what we need for processing)
    const uint16_t seq_num = *(const uint16_t *)(packet + 0);
    const uint16_t frame_num = *(const uint16_t *)(packet + 2);
    const uint16_t line_num_raw = *(const uint16_t *)(packet + 4);
    const uint8_t lines_per_packet = packet[8];
    const uint8_t *payload_ptr = packet + C64_VIDEO_HEADER_SIZE;

    // Branchless last-packet detection
    const uint16_t line_num = line_num_raw & 0x7FFF;
    const bool last_packet = (line_num_raw & 0x8000) != 0;

    const bool need_lock = (context->network_buffer == NULL);
    bool locked = false;
    if (!need_lock || (pthread_mutex_lock(&context->assembly_mutex) == 0)) {
        locked = need_lock;
        // Track frame capture timing for diagnostics (per-frame, not per-packet)
        uint64_t capture_time = timestamp_ns;

        // Check if this is a new frame
        if (context->current_frame.frame_num != frame_num) {
            // Log frame transitions to detect skips and duplicates (throttled)
            if (context->current_frame.frame_num != 0) {
                uint16_t expected_next = context->current_frame.frame_num + 1;
                int16_t frame_diff = (int16_t)(frame_num - expected_next);

                static uint64_t last_skip_log = 0;
                uint64_t now_skip = os_gettime_ns();
                if ((frame_diff != 0) && (now_skip - last_skip_log >= 5000000000ULL)) { // 5 sec throttle
                    uint64_t predicted_index = 0;
                    uint64_t predicted_ts_ns = 0;
                    (void)c64_predict_video_timestamp_for_frame(context, frame_num, &predicted_index, &predicted_ts_ns);
                    if (frame_diff > 0) {
                        C64_LOG_WARNING("" VIDEO_LOG_PREFIX
                                        " 📽️ FRAME SKIP: Expected frame %u, got %u (skipped %d frames)"
                                        " predicted_video_ts_ns=%" PRIu64 " predicted_video_frame_index=%" PRIu64,
                                        expected_next, frame_num, frame_diff, predicted_ts_ns, predicted_index);
                    } else if (frame_diff < 0) {
                        C64_LOG_WARNING("" VIDEO_LOG_PREFIX
                                        " Frame sequence regression: Expected frame %u, got %u (offset %d frames)"
                                        " predicted_video_ts_ns=%" PRIu64 " predicted_video_frame_index=%" PRIu64,
                                        expected_next, frame_num, -frame_diff, predicted_ts_ns, predicted_index);
                    }
                    last_skip_log = now_skip;
                }
            }

            // Count expected and captured frames only on new frame start
            if (context->last_capture_time > 0) {
                context->frames_expected++;
            }
            context->frames_captured++;
            context->last_capture_time = capture_time;

            // Complete previous frame if it exists and is reasonably complete
            if (context->current_frame.received_packets > 0) {
                if (c64_is_frame_complete(&context->current_frame) || c64_is_frame_timeout(&context->current_frame)) {
                    if (c64_is_frame_complete(&context->current_frame)) {
                        if (context->last_completed_frame != context->current_frame.frame_num) {
                            // Direct rendering - synthetic timestamp is derived from stream_start_ns inside the renderer.
                            c64_render_frame_direct(context, &context->current_frame,
                                                    context->current_frame.last_packet_time);
                            context->last_completed_frame = context->current_frame.frame_num;

                            // Track diagnostics
                            context->frames_completed++;
                            context->total_pipeline_latency += (os_gettime_ns() - capture_time);
                        }
                    } else {
                        // Frame timeout - log drop and continue
                        uint64_t predicted_index = 0;
                        uint64_t predicted_ts_ns = 0;
                        (void)c64_predict_video_timestamp_for_frame(context, context->current_frame.frame_num,
                                                                    &predicted_index, &predicted_ts_ns);
                        C64_LOG_WARNING("" VIDEO_LOG_PREFIX
                                        " ⏰ FRAME TIMEOUT: Frame %u timed out with %u/%u packets (%.1f%% complete)"
                                        " predicted_video_ts_ns=%" PRIu64 " predicted_video_frame_index=%" PRIu64,
                                        context->current_frame.frame_num, context->current_frame.received_packets,
                                        context->current_frame.expected_packets,
                                        (context->current_frame.received_packets * 100.0f) /
                                            context->current_frame.expected_packets,
                                        predicted_ts_ns, predicted_index);
                        context->frame_drops++;
                    }
                }
            }

            // Initialize new frame
            c64_init_frame_assembly(&context->current_frame, frame_num);
        }

        // Add packet to current frame
        uint32_t packet_index = line_num / lines_per_packet;
        if (packet_index < C64_MAX_PACKETS_PER_FRAME) {
            struct frame_packet *fp = &context->current_frame.packets[packet_index];
            if (!fp->received) {
                fp->line_num = line_num;
                fp->lines_per_packet = lines_per_packet;
                fp->received = true;
                memcpy(fp->packet_data, payload_ptr, C64_VIDEO_PACKET_SIZE - C64_VIDEO_HEADER_SIZE);
                context->current_frame.received_packets++;
                // Track last packet arrival time for A/V sync
                context->current_frame.last_packet_time = timestamp_ns;
            }
        } else {
            C64_LOG_WARNING("" VIDEO_LOG_PREFIX
                            " ❌ INVALID PACKET: Frame %u, Line %u out of range (packet_index %u >= %d) - seq %u",
                            frame_num, line_num, packet_index, C64_MAX_PACKETS_PER_FRAME, seq_num);
            context->packet_drops++;
        }

        // Update expected packet count and detect video format based on last packet
        if (last_packet && context->current_frame.expected_packets == 0) {
            context->current_frame.expected_packets = packet_index + 1;

            // Detect PAL vs NTSC format from frame height
            const uint32_t reported_frame_height = line_num + lines_per_packet;
            const uint32_t frame_height = c64_clamp_frame_height(reported_frame_height);
            if (reported_frame_height != frame_height) {
                C64_LOG_WARNING("" VIDEO_LOG_PREFIX " Dropping invalid frame height %u", reported_frame_height);
                context->packet_drops++;
                if (locked) {
                    pthread_mutex_unlock(&context->assembly_mutex);
                }
                return;
            }
            if (!context->format_detected || context->detected_frame_height != frame_height) {
                context->detected_frame_height = frame_height;
                context->format_detected = true;

                // Calculate expected FPS and frame interval based on detected format
                if (frame_height == C64_PAL_HEIGHT) {
                    context->expected_fps = 50.125;
                    context->frame_interval_ns = C64_PAL_FRAME_INTERVAL_NS;
                    context->audio_sample_rate = C64_PAL_AUDIO_SAMPLE_RATE;
                    context->audio_interval_ns = 0;
                    context->audio_info.samples_per_sec = (uint32_t)C64_PAL_AUDIO_SAMPLE_RATE;
                    context->last_connected_format_was_pal = true; // Update logo format preference
                    C64_LOG_INFO("" VIDEO_LOG_PREFIX " 🎥 Detected PAL format: 384x%u @ %.3f Hz (audio: %.1f Hz)",
                                 frame_height, context->expected_fps, C64_PAL_AUDIO_SAMPLE_RATE);
                } else if (frame_height == C64_NTSC_HEIGHT) {
                    context->expected_fps = 59.826;
                    context->frame_interval_ns = C64_NTSC_FRAME_INTERVAL_NS;
                    context->audio_sample_rate = C64_NTSC_AUDIO_SAMPLE_RATE;
                    context->audio_interval_ns = 0;
                    context->audio_info.samples_per_sec = (uint32_t)C64_NTSC_AUDIO_SAMPLE_RATE;
                    context->last_connected_format_was_pal = false; // Update logo format preference
                    C64_LOG_INFO("" VIDEO_LOG_PREFIX " 🎥 Detected NTSC format: 384x%u @ %.3f Hz (audio: %.1f Hz)",
                                 frame_height, context->expected_fps, C64_NTSC_AUDIO_SAMPLE_RATE);
                } else {
                    // Unknown format, estimate based on packet count
                    context->expected_fps = (frame_height <= 250) ? 59.826 : 50.125;
                    context->frame_interval_ns = (frame_height <= 250) ? C64_NTSC_FRAME_INTERVAL_NS
                                                                       : C64_PAL_FRAME_INTERVAL_NS;
                    context->audio_sample_rate = (frame_height <= 250) ? C64_NTSC_AUDIO_SAMPLE_RATE
                                                                       : C64_PAL_AUDIO_SAMPLE_RATE;
                    context->audio_interval_ns = 0;
                    context->audio_info.samples_per_sec = (uint32_t)context->audio_sample_rate;
                    context->last_connected_format_was_pal = (frame_height > 250); // Assume PAL for larger heights
                    C64_LOG_WARNING("" VIDEO_LOG_PREFIX
                                    " ⚠️ Unknown video format: 384x%u, assuming %.3f Hz (audio: %.1f Hz)",
                                    frame_height, context->expected_fps, context->audio_sample_rate);
                }

                /* C64STR-008: publish width+height as one coherent pair so the
                 * graphics thread never observes a torn (new-height/old-width)
                 * combination. The non-buffer path already holds assembly_mutex
                 * here; the network-buffer path does not, so it publishes under
                 * the same short lock the graphics thread snapshots with. */
                if (context->height != frame_height) {
                    if (locked) {
                        context->width = C64_PIXELS_PER_LINE; // Always 384
                        context->height = frame_height;
                    } else {
                        c64_dimensions_publish(&context->assembly_mutex, &context->width, &context->height,
                                               C64_PIXELS_PER_LINE, frame_height);
                    }
                }
            }
        }

        // Note: Frame completion is handled by the "complete previous frame" logic
        // when transitioning to a new frame. This avoids duplicate frame deliveries.

        if (locked) {
            pthread_mutex_unlock(&context->assembly_mutex);
        }
    }
}

static bool c64_stage2_drain_video_fifo(struct c64_source *context, uint32_t max_packets)
{
    if (!context) {
        return false;
    }

    bool did_work = false;
    uint8_t packet[C64_VIDEO_PACKET_SIZE];

    for (uint32_t i = 0; i < max_packets; i++) {
        struct c64_network_fifo_packet *slot = c64_network_fifo_peek(&context->video_fifo);
        if (!slot) {
            break;
        }

        const uint64_t packet_time = slot->timestamp_ns;
        const uint16_t received = slot->size;
        memcpy(packet, slot->data, received);
        c64_network_fifo_commit_pop(&context->video_fifo);
        did_work = true;

        // Stage-2: buffering / ordering / validation / optional CSV logging.
        c64_log_video_packet_if_enabled(context, packet, received, packet_time);

        // Parse packet header for validation (existing behavior, moved out of recv hot path).
        const uint16_t pixels_per_line = *(uint16_t *)(packet + 6);
        const uint8_t lines_per_packet = packet[8];
        const uint8_t bits_per_pixel = packet[9];

        if (packet_time - context->last_stats_tick_ns >= 50000000ULL) { // ~50ms cadence
            context->last_stats_tick_ns = packet_time;
            c64_process_video_statistics_batch(context, packet_time);
        }

        if (lines_per_packet != C64_LINES_PER_PACKET || pixels_per_line != C64_PIXELS_PER_LINE || bits_per_pixel != 4) {
            static uint64_t last_invalid_log = 0;
            if (packet_time - last_invalid_log >= 5000000000ULL) { // 5 sec throttle
                C64_LOG_WARNING("" VIDEO_LOG_PREFIX " Invalid packet format: lines=%u, pixels=%u, bits=%u",
                                lines_per_packet, pixels_per_line, bits_per_pixel);
                last_invalid_log = packet_time;
            }
            continue;
        }

        if (context->network_buffer) {
            c64_network_buffer_push_video(context->network_buffer, packet, received, packet_time);
        } else {
            // In direct mode (buffer disabled), keep existing behavior (including duplicate logging).
            c64_log_video_packet_if_enabled(context, packet, received, packet_time);
            c64_process_video_packet_direct(context, packet, received, packet_time);
        }
    }

    return did_work;
}

static bool c64_stage2_drain_audio_fifo(struct c64_source *context, uint32_t max_packets)
{
    if (!context) {
        return false;
    }

    bool did_work = false;
    uint8_t packet[C64_AUDIO_PACKET_SIZE];

    for (uint32_t i = 0; i < max_packets; i++) {
        struct c64_network_fifo_packet *slot = c64_network_fifo_peek(&context->audio_fifo);
        if (!slot) {
            break;
        }

        const uint64_t packet_time = slot->timestamp_ns;
        const uint16_t received = slot->size;
        memcpy(packet, slot->data, received);
        c64_network_fifo_commit_pop(&context->audio_fifo);
        did_work = true;

        c64_log_audio_packet_if_enabled(context, packet, received, packet_time);
        c64_process_audio_statistics_batch(context, packet_time);

        if (context->network_buffer) {
            c64_network_buffer_push_audio(context->network_buffer, packet, received, packet_time);
        } else {
            c64_process_audio_packet(context, packet, received, packet_time);
        }
    }

    return did_work;
}

void *c64_video_processor_thread_func(void *data)
{
    struct c64_source *context = data;
    uint64_t last_logo_frame_time = 0;
    uint64_t last_retry_attempt = 0;
    const uint64_t logo_frame_interval_ns = 20000000ULL; // 50Hz (20ms) for logo frames
    const uint64_t retry_interval_ns = 1000000000ULL;    // 1 second retry interval

    C64_LOG_DEBUG("" VIDEO_LOG_PREFIX " Video processor thread started");

    // Initialize last_frame_time to 0 so logo shows immediately on startup
    context->last_frame_time = 0;

    // Adaptive idle spin counter (persists across iterations)
    // Adaptive idle spin counter (persists across iterations)
    int idle_spins = 0;

    while (os_atomic_load_bool(&context->thread_active)) {
#ifdef C64_ENABLE_TIMING_INSTRUMENTATION
        uint64_t iter_start = os_gettime_ns();
        uint64_t current_time = iter_start;
#else
        uint64_t current_time = os_gettime_ns();
#endif
        bool packet_processed = false;

        // Stage-2 ingress: consume from Stage-1 UDP ingest rings and feed the ordering/delay buffer.
        // Drain video more aggressively (higher PPS) and audio lightly.
        if (c64_stage2_drain_video_fifo(context, 512)) {
            packet_processed = true;
        }
        if (c64_stage2_drain_audio_fifo(context, 128)) {
            packet_processed = true;
        }

        if (context->network_buffer) {
            const uint8_t *video_data, *audio_data;
            size_t video_size, audio_size;
            uint64_t timestamp_us;

            // Drain multiple ready packets per loop iteration to avoid falling behind.
            for (int pop_i = 0; pop_i < 64; pop_i++) {
                if (!c64_network_buffer_pop(context->network_buffer, &video_data, &video_size, &audio_data, &audio_size,
                                            &timestamp_us)) {
                    break;
                }

                if (video_data && video_size > 0) {
                    c64_process_video_packet_direct(context, video_data, video_size, timestamp_us * 1000);

                    // Reset retry count on successful video packet processing
                    if (context->retry_count > 0) {
                        C64_LOG_INFO("" VIDEO_LOG_PREFIX " Video stream restored, resetting retry count (was %u)",
                                     context->retry_count);
                        context->retry_count = 0;
                    }
                }

                if (audio_data && audio_size > 0) {
                    c64_process_audio_packet(context, audio_data, audio_size, timestamp_us * 1000);
                }

                context->last_frame_time = current_time;
                packet_processed = true;
            }
        }

        // If no packets processed and enough time has passed, show logo at 50Hz
        if (!packet_processed) {
            uint64_t time_since_last_frame = current_time - context->last_frame_time;
            // Calculate time differences, handling potential underflow from timing precision issues
            uint64_t time_since_last_video = 0;
            if (current_time >= context->last_video_packet_time) {
                time_since_last_video = current_time - context->last_video_packet_time;
            } else {
                // Handle underflow case where last_video_packet_time is slightly in the future
                uint64_t timing_diff = context->last_video_packet_time - current_time;
                // Only log if the timing difference is very significant (>10ms), indicating a real timing problem
                if (timing_diff > 10000000) { // 10 milliseconds - anything less is normal precision variance
                    C64_LOG_DEBUG("" VIDEO_LOG_PREFIX
                                  " Significant timing issue: last_video_packet_time ahead by %" PRIu64
                                  "ns (%.1fms) - investigating",
                                  timing_diff, (double)timing_diff / 1000000.0);
                }
                time_since_last_video = 0;
            }

            uint64_t time_since_last_logo = current_time - last_logo_frame_time;
            uint64_t time_since_last_retry = current_time - last_retry_attempt;

            // Sanity check to prevent timestamp overflow (should never exceed ~1 hour)
            if (time_since_last_video > 3600000000000ULL) {
                C64_LOG_DEBUG("" VIDEO_LOG_PREFIX " Long-running stream: resetting video timing base after %" PRIu64
                              "ns (%.1f hours)",
                              time_since_last_video, (double)time_since_last_video / 3600000000000.0);
                context->last_video_packet_time = current_time;
                time_since_last_video = 0;
            }

            // Show logo if no frames for 3 seconds AND we haven't shown logo recently
            // Increased from 1s to 3s to reduce logo flashing during slider adjustments
            if (time_since_last_frame > 3000000000ULL && time_since_last_logo >= logo_frame_interval_ns) {
                if (c64_logo_is_available(context)) {
                    c64_logo_render_to_frame(context, current_time);
                } else {
                    // Fallback: render black screen if no logo available
                    c64_render_black_screen(context, current_time);
                }
                last_logo_frame_time = current_time;
                context->last_frame_time = current_time;
            }

            // Retry TCP connection and recreate UDP sockets if no VIDEO packets for 1+ seconds
            // During initial startup it's normal to have a longer delay between sending START commands and
            // receiving the first UDP packets (e.g. E2E harness setup). Avoid thrashing sockets in that window.
            const uint64_t initial_no_packet_grace_ns = 2000000000ULL; // 2 seconds
            long video_packets_received = os_atomic_load_long(&context->video_packets_received);
            bool have_seen_any_video = (video_packets_received > 0);

            uint64_t no_video_retry_threshold_ns = retry_interval_ns;
            if (!have_seen_any_video) {
                // If we haven't even requested streaming yet, don't schedule no-packet retries.
                if (context->last_start_command_time_ns == 0) {
                    no_video_retry_threshold_ns = UINT64_MAX;
                } else {
                    no_video_retry_threshold_ns = initial_no_packet_grace_ns;
                }
            }

            if (time_since_last_video > no_video_retry_threshold_ns && time_since_last_retry >= retry_interval_ns &&
                !os_atomic_load_long(&context->retry_in_progress) &&
                !os_atomic_load_bool(&context->udp_port_conflict)) {
                uint64_t time_since_last_audio = current_time - context->last_audio_packet_time;
                C64_LOG_INFO(
                    "No video packets for %.1fs (audio: %.1fs), retrying TCP commands and recreating UDP sockets",
                    time_since_last_video / 1000000000.0, time_since_last_audio / 1000000000.0);

                last_retry_attempt = current_time;

                // Schedule retry in a background thread (never on OBS UI thread).
                c64_schedule_retry_task(context, "no video packets");
            }

            // Adaptive idle: spin a few iterations then short sleep
            if (idle_spins < 10) {
                idle_spins++;
                // Spin without sleep for a few iterations
            } else {
                os_sleep_ms(1);
            }
        } else {
            // Reset spin counter when work is processed
            idle_spins = 0;
        }

#ifdef C64_ENABLE_TIMING_INSTRUMENTATION
        // Accumulate timing for this iteration
        uint64_t iter_end = os_gettime_ns();
        context->timing_processor_total_ns += (iter_end - iter_start);
        context->timing_processor_count++;
#endif
    }

    C64_LOG_DEBUG("" VIDEO_LOG_PREFIX " Video processor thread stopped");
    return NULL;
}
