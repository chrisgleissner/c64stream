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

// ═══════════════════════════════════════════════════════════════════════════════
// SIMD INTRINSICS MUST BE INCLUDED BEFORE OBS HEADERS
// ═══════════════════════════════════════════════════════════════════════════════
// OBS uses SIMDE (SIMD Everywhere) for portability, which conflicts with native
// intrinsics if included afterward. We include native intrinsics first and define
// SIMDE_ENABLE_NATIVE_ALIASES=0 to prevent SIMDE from redefining them.
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#define C64_HAS_X86_SIMD 1
// Prevent SIMDE from overriding native intrinsics
#define SIMDE_ENABLE_NATIVE_ALIASES 0
#include <immintrin.h>
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

#include "c64-effect-afterglow.h"
#include "c64-logging.h"
#include "c64-video.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <util/platform.h>

// ═══════════════════════════════════════════════════════════════════════════════
// ALIGNED MEMORY ALLOCATION HELPERS
// ═══════════════════════════════════════════════════════════════════════════════
// Cache-line aligned allocation improves SIMD performance (AVX2 benefits from 32/64-byte alignment)

void *c64_alloc_aligned(size_t size, size_t alignment)
{
#ifdef _WIN32
    return _aligned_malloc(size, alignment);
#else
    // aligned_alloc requires size to be multiple of alignment (C11)
    const size_t aligned_size = ((size + alignment - 1) / alignment) * alignment;
    return aligned_alloc(alignment, aligned_size);
#endif
}

void c64_free_aligned(void *ptr)
{
#ifdef _WIN32
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

// ═══════════════════════════════════════════════════════════════════════════════
// SIMD-OPTIMIZED AFTERGLOW IMPLEMENTATION
// ═══════════════════════════════════════════════════════════════════════════════
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

    // AVX-VEX transition: zero upper YMM to avoid performance penalty
    _mm256_zeroupper();

    // Memory fence to ensure all non-temporal stores complete
    if (use_streaming) {
        _mm_sfence();
    }
}
#endif // __AVX2__ || _MSC_VER

#endif // C64_HAS_X86_SIMD

void c64_afterglow_init(struct c64_afterglow *ag)
{
    if (!ag) {
        return;
    }

    memset(ag, 0, sizeof(*ag));
    ag->curve = 2;
}

void c64_afterglow_reset(struct c64_afterglow *ag)
{
    if (!ag) {
        return;
    }

    ag->accum_valid = false;
    ag->decay_cache_valid = false;
}

void c64_afterglow_free(struct c64_afterglow *ag)
{
    if (!ag) {
        return;
    }

    if (ag->accum) {
        c64_free_aligned(ag->accum);
        ag->accum = NULL;
    }
    ag->accum_bytes = 0;
    ag->accum_valid = false;
    ag->decay_cache_valid = false;
}

const uint32_t *c64_afterglow_apply(struct c64_afterglow *ag, const uint32_t *curr_pixels, size_t pixel_count,
                                    float dt_ms)
{
    if (!ag || !curr_pixels || pixel_count == 0) {
        return curr_pixels;
    }

    if (ag->duration_ms <= 0) {
        return curr_pixels;
    }

    const size_t frame_bytes = pixel_count * 4;
    if (ag->accum_bytes != frame_bytes) {
        if (ag->accum) {
            c64_free_aligned(ag->accum);
        }
        // Align to 64-byte cache line for optimal SIMD performance
        ag->accum = (uint32_t *)c64_alloc_aligned(frame_bytes, 64);
        ag->accum_bytes = frame_bytes;
        ag->accum_valid = false; // Invalidate on resize (Medium #8)
    }

    if (!ag->accum) {
        return curr_pixels;
    }

    // Clamp dt_ms to reasonable range (1-100ms) to handle frame rate variation (Medium #6)
    // Prevents afterglow from decaying too fast on irregular frames or stuttering on pauses.
    if (dt_ms < 1.0f)
        dt_ms = 1.0f;
    if (dt_ms > 100.0f)
        dt_ms = 100.0f;

    const float base_duration_ms = (float)((ag->duration_ms > 1) ? ag->duration_ms : 1);

    // Curve mapping: 0=linear-ish, 1=faster fade, 2=normal, 3=long tail
    float duration_ms = base_duration_ms;
    switch (ag->curve) {
    case 0:
        break;
    case 1:
        duration_ms = base_duration_ms * 0.5f;
        break;
    case 3:
        duration_ms = base_duration_ms * 2.0f;
        break;
    case 2:
    default:
        break;
    }

    // Per-channel time constants (blue fastest, green medium, red slowest).
    const float tau_r = duration_ms * 1.35f;
    const float tau_g = duration_ms * 1.00f;
    const float tau_b = duration_ms * 0.75f;

    // Optimize expf() calls by caching decay factors when parameters haven't changed
    // This saves ~60-120 CPU cycles per frame (3 expf calls @ 20-40 cycles each)
    float decay_r, decay_g, decay_b;
    if (ag->decay_cache_valid && ag->cached_dt_ms == dt_ms && ag->cached_duration_ms == ag->duration_ms &&
        ag->cached_curve == ag->curve) {
        // Use cached values
        decay_r = ag->cached_decay_r;
        decay_g = ag->cached_decay_g;
        decay_b = ag->cached_decay_b;
    } else {
        // Recompute and cache
        if (ag->curve == 0) {
            decay_r = 1.0f - (dt_ms / tau_r);
            decay_g = 1.0f - (dt_ms / tau_g);
            decay_b = 1.0f - (dt_ms / tau_b);
        } else {
            decay_r = expf(-dt_ms / tau_r);
            decay_g = expf(-dt_ms / tau_g);
            decay_b = expf(-dt_ms / tau_b);
        }

        // Clamp to [0, 1]
        if (decay_r < 0.0f)
            decay_r = 0.0f;
        if (decay_r > 1.0f)
            decay_r = 1.0f;
        if (decay_g < 0.0f)
            decay_g = 0.0f;
        if (decay_g > 1.0f)
            decay_g = 1.0f;
        if (decay_b < 0.0f)
            decay_b = 0.0f;
        if (decay_b > 1.0f)
            decay_b = 1.0f;

        // Update cache
        ag->cached_decay_r = decay_r;
        ag->cached_decay_g = decay_g;
        ag->cached_decay_b = decay_b;
        ag->cached_dt_ms = dt_ms;
        ag->cached_duration_ms = ag->duration_ms;
        ag->cached_curve = ag->curve;
        ag->decay_cache_valid = true;
    }

    uint32_t *acc = ag->accum;
    if (!ag->accum_valid) {
        memcpy(acc, curr_pixels, frame_bytes);
        ag->accum_valid = true;
        return acc;
    }

// ─────────────────────────────────────────────────────────────────────────────
// SIMD-accelerated afterglow loop (3-4x faster than scalar)
// Dispatch: AVX2 (8 pixels) > SSE2 (4 pixels) > Scalar fallback
// ─────────────────────────────────────────────────────────────────────────────
#ifdef C64_HAS_X86_SIMD
    c64_detect_simd_support();

    // Prefetch next cache lines to reduce memory stall cycles (typical L1 miss: ~4-7 cycles)
    // Prefetch removed - was causing performance issues

#if defined(__AVX2__) || defined(_MSC_VER)
    if (c64_cpu_has_avx2) {
        c64_afterglow_avx2(acc, curr_pixels, pixel_count, decay_r, decay_g, decay_b, false);
        return acc;
    }
#endif
    // SSE2 fallback (guaranteed on x86-64)
    c64_afterglow_sse2(acc, curr_pixels, pixel_count, decay_r, decay_g, decay_b);
#else
    // Scalar fallback for non-x86 platforms (ARM, etc.)
    for (size_t i = 0; i < pixel_count; i++) {
        const uint32_t curr = curr_pixels[i];
        const uint32_t prev = acc[i];

        const float pr = (float)((prev >> 0) & 0xFF);
        const float pg = (float)((prev >> 8) & 0xFF);
        const float pb = (float)((prev >> 16) & 0xFF);

        const float cr = (float)((curr >> 0) & 0xFF);
        const float cg = (float)((curr >> 8) & 0xFF);
        const float cb = (float)((curr >> 16) & 0xFF);

        const float tr = pr * decay_r;
        const float tg = pg * decay_g;
        const float tb = pb * decay_b;

        float or_ = (cr > tr) ? cr : tr;
        float og_ = (cg > tg) ? cg : tg;
        float ob_ = (cb > tb) ? cb : tb;

        if (or_ > 255.0f)
            or_ = 255.0f;
        if (og_ > 255.0f)
            og_ = 255.0f;
        if (ob_ > 255.0f)
            ob_ = 255.0f;

        acc[i] = 0xFF000000 | ((uint32_t)ob_ << 16) | ((uint32_t)og_ << 8) | ((uint32_t)or_);
    }
#endif // C64_HAS_X86_SIMD

    return acc;
}
