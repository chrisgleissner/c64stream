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

#include <obs-module.h>
#include <util/platform.h>
#include <util/threading.h> // For atomic operations
#include <string.h>
#include <inttypes.h>
#include <pthread.h>
#include <math.h>
#include <stdlib.h> // For aligned_alloc and free
#include "c64-network.h"
#include "c64-network-buffer.h"

#include "c64-logging.h"
#include "c64-video.h"
#include "c64-logo.h"
#include "c64-audio.h"
#include "c64-color.h"
#include "c64-types.h"
#include "c64-protocol.h"
#include "c64-record-network.h"
#include "c64-protocol.h"
#include "c64-record.h"
#include "c64-source.h"

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

    C64_LOG_DEBUG("SIMD detection: AVX2 %s", c64_cpu_has_avx2 ? "available" : "not available (using SSE2)");
}

// ─────────────────────────────────────────────────────────────────────────────
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
    // Broadcast decay factors to all 8 lanes
    const __m256 vdecay_r = _mm256_set1_ps(decay_r);
    const __m256 vdecay_g = _mm256_set1_ps(decay_g);
    const __m256 vdecay_b = _mm256_set1_ps(decay_b);
    const __m256 v255 = _mm256_set1_ps(255.0f);
    const __m256i vmask_channel = _mm256_set1_epi32(0xFF);
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

// Forward declarations
static uint64_t c64_calculate_ideal_timestamp(struct c64_source *context, uint16_t frame_num);

static const uint32_t *c64_get_afterglow_output_pixels(struct c64_source *context, const uint32_t *curr_pixels,
                                                       size_t pixel_count)
{
    if (!context || !curr_pixels || pixel_count == 0)
        return curr_pixels;

    if (!(context->afterglow_enable && context->afterglow_duration_ms > 0))
        return curr_pixels;

    const size_t frame_bytes = pixel_count * 4;
    if (context->afterglow_cpu_bytes != frame_bytes) {
        if (context->afterglow_cpu_accum) {
            c64_free_aligned(context->afterglow_cpu_accum);
        }
        // Align to 64-byte cache line for optimal SIMD performance
        context->afterglow_cpu_accum = (uint32_t *)c64_alloc_aligned(frame_bytes, 64);
        context->afterglow_cpu_bytes = frame_bytes;
        context->afterglow_cpu_valid = false; // Invalidate on resize (Medium #8)
    }

    if (!context->afterglow_cpu_accum)
        return curr_pixels;

    // Use the detected frame interval (PAL/NTSC) for stable dt.
    float dt_ms = 33.33f;
    if (context->frame_interval_ns > 0) {
        dt_ms = (float)context->frame_interval_ns / 1000000.0f;
    } else if (context->expected_fps > 1.0) {
        dt_ms = (float)(1000.0 / context->expected_fps);
    }

    // Clamp dt_ms to reasonable range (1-100ms) to handle frame rate variation (Medium #6)
    // Prevents afterglow from decaying too fast on irregular frames or stuttering on pauses.
    if (dt_ms < 1.0f)
        dt_ms = 1.0f;
    if (dt_ms > 100.0f)
        dt_ms = 100.0f;

    const float base_duration_ms = (float)((context->afterglow_duration_ms > 1) ? context->afterglow_duration_ms : 1);

    // Curve mapping: 0=linear-ish, 1=faster fade, 2=normal, 3=long tail
    float duration_ms = base_duration_ms;
    switch (context->afterglow_curve) {
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
    if (context->decay_cache_valid && context->cached_dt_ms == dt_ms &&
        context->cached_duration_ms == context->afterglow_duration_ms &&
        context->cached_curve == context->afterglow_curve) {
        // Use cached values
        decay_r = context->cached_decay_r;
        decay_g = context->cached_decay_g;
        decay_b = context->cached_decay_b;
    } else {
        // Recompute and cache
        if (context->afterglow_curve == 0) {
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
        context->cached_decay_r = decay_r;
        context->cached_decay_g = decay_g;
        context->cached_decay_b = decay_b;
        context->cached_dt_ms = dt_ms;
        context->cached_duration_ms = context->afterglow_duration_ms;
        context->cached_curve = context->afterglow_curve;
        context->decay_cache_valid = true;
    }

    uint32_t *acc = context->afterglow_cpu_accum;
    if (!context->afterglow_cpu_valid) {
        memcpy(acc, curr_pixels, frame_bytes);
        context->afterglow_cpu_valid = true;
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

// Helper functions for frame assembly (updated to use lock-free implementation)
void c64_init_frame_assembly(struct frame_assembly *frame, uint16_t frame_num)
{
    memset(frame, 0, sizeof(struct frame_assembly));
    frame->frame_num = frame_num;
    frame->start_time = os_gettime_ns();
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
        C64_LOG_DEBUG("🎬 Frame completion check: frame %u has %u/%u packets (complete=%d)", frame->frame_num, received,
                      expected, complete);
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
            C64_LOG_DEBUG("🎬 Frame COMPLETION SPOT CHECK: frame %u with %u/%u packets! (total count: %d)",
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

// Direct frame rendering with row interpolation for missing packets
void c64_render_frame_direct(struct c64_source *context, struct frame_assembly *frame, uint64_t timestamp_ns)
{
    // First, assemble the frame with interpolation for missing rows
    c64_assemble_frame_with_interpolation(context, frame);

    // Generate monotonic timestamp based on frame sequence for butter-smooth playback
    uint64_t monotonic_timestamp = c64_calculate_ideal_timestamp(context, frame->frame_num);

    // Apply afterglow in the video thread (prevents races/flicker with raw frame_buffer).
    const size_t pixel_count = (size_t)context->width * (size_t)context->height;
    const uint32_t *out_pixels = c64_get_afterglow_output_pixels(context, context->frame_buffer, pixel_count);

    // Save frame to disk if enabled
    if (context->record_frames) {
        c64_save_frame_as_bmp(context, (uint32_t *)out_pixels);

        // Note: CSV logging for video events is now handled independently in the video processor thread
    }

    // Record frame to video file if recording is enabled
    if (context->record_video) {
        c64_record_video_frame(context, (uint32_t *)out_pixels);
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

    // Mark frame_dirty so the render thread can skip redundant texture uploads when unchanged.
    context->frame_dirty = true;

    // Log video frame delivery to CSV if enabled (high-level event: complete frame delivered to OBS)
    if (context->timing_file) {
        size_t frame_size = context->width * context->height * 4; // RGBA bytes
        c64_obs_log_video_event(context, frame->frame_num, frame_size);
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
        C64_LOG_DEBUG("🎬 MONOTONIC SPOT CHECK: frame=%u, monotonic_ts=%" PRIu64 ", packet_ts=%" PRIu64
                      ", delta=%+" PRId64 ", packets=%u/%u (count: %d)",
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
        C64_LOG_DEBUG("🎬 ASSEMBLY SPOT CHECK: frame %u with %u/%u packets (count: %d)", frame->frame_num,
                      frame->received_packets, frame->expected_packets, assembly_debug_count);
        last_assembly_log_time = now;
    }

    // Stack-allocated line tracking (max 272 lines for PAL, fits easily on stack)
    // Using uint8_t instead of bool for guaranteed 1-byte size
    uint8_t line_written[272] = {0}; // Zero-initialized on stack
    const uint32_t height = context->height;
    uint32_t lines_written_count = 0;

    if (height > 272) {
        C64_LOG_ERROR("Frame height %u exceeds maximum 272", height);
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

            c64_convert_pixels_optimized(src_line, dst_line, C64_BYTES_PER_LINE);
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
    if (packets_received > 0) {
        C64_LOG_INFO("📺 VIDEO: %.1f fps | %.2f Mbps | %.0f pps | Frames: %u", frames_per_second, bandwidth_mbps,
                     packets_per_second, (uint32_t)frames_processed);
        C64_LOG_INFO("🎯 DELIVERY: Expected %.0f fps | Captured %.1f fps | Delivered %.1f fps | Completed %.1f fps",
                     expected_fps, context->frames_captured / duration_seconds, frame_delivery_rate,
                     frame_completion_rate);
        C64_LOG_INFO("📊 PIPELINE: Capture drops %.1f%% | Delivery drops %.1f%% | Avg latency %.1f ms",
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

    uint64_t time_since_last_log = current_time - context->last_stats_log_time;
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

        C64_LOG_INFO("🔊 AUDIO: %.2f Mbps | %.0f pps | Packets: %llu", bandwidth_mbps, packets_per_second,
                     (unsigned long long)packets_received);
    }
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

    C64_LOG_DEBUG("Video receiver thread started on port %u", context->video_port);

#ifdef _WIN32
    HANDLE thread_handle = GetCurrentThread();
    if (SetThreadPriority(thread_handle, THREAD_PRIORITY_ABOVE_NORMAL)) {
        C64_LOG_DEBUG("Set video receiver thread to above-normal priority on Windows");
    } else {
        C64_LOG_WARNING("Failed to set video receiver thread priority on Windows");
    }

    timeBeginPeriod(1);
#else
    // Best-effort priority boost on POSIX (no noise on failure)
    struct sched_param param;
    param.sched_priority = sched_get_priority_max(SCHED_OTHER);
    pthread_setschedparam(pthread_self(), SCHED_OTHER, &param);
#endif

    // Set socket receive timeout to reduce blocking jitter
#ifdef _WIN32
    DWORD timeout_ms = 10;
    setsockopt(context->video_socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout_ms, sizeof(timeout_ms));
#else
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 10000; // 10ms
    setsockopt(context->video_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    C64_LOG_DEBUG("Video thread function started with optimized scheduling");

#ifdef C64_ENABLE_TIMING_INSTRUMENTATION
    // Initialize timing profiling
    context->timing_recv_total_ns = 0;
    context->timing_recv_count = 0;
#endif

#ifdef __linux__
// Batch read optimization for Linux: receive up to 8 packets per syscall
#define BATCH_SIZE 8
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
    C64_LOG_DEBUG("Linux batch recv enabled (recvmmsg with up to %d packets per syscall)", BATCH_SIZE);
#endif

    while (os_atomic_load_bool(&context->thread_active)) {
#ifdef C64_ENABLE_TIMING_INSTRUMENTATION
        uint64_t iter_start = os_gettime_ns();
#endif
        // Check socket validity before each recv call (prevents Windows WSAENOTSOCK errors)
        if (context->video_socket == INVALID_SOCKET_VALUE) {
            os_sleep_ms(10); // Wait a bit before checking again
            continue;
        }

#ifdef __linux__
        // Linux: batch read with recvmmsg (reduces syscall overhead)
        int num_msgs = recvmmsg(context->video_socket, msgs, BATCH_SIZE, MSG_DONTWAIT, NULL);

        if (num_msgs < 0) {
            int error = c64_get_socket_error();
            if (error == EAGAIN || error == EWOULDBLOCK) {
                os_sleep_ms(1);
                continue;
            }
            if (error == EBADF && context->video_socket == INVALID_SOCKET_VALUE) {
                C64_LOG_DEBUG("Video socket closed (EBADF) - exiting receiver thread gracefully");
                break;
            }
            C64_LOG_ERROR("Video socket error: %s (error code: %d)", c64_get_socket_error_string(error), error);
            break;
        }

        // Process each received packet
        for (int i = 0; i < num_msgs; i++) {
            ssize_t received = msgs[i].msg_len;
            uint8_t *packet = packet_batch[i];
#else
        // Non-Linux: fallback to single recvfrom
        uint8_t packet[C64_VIDEO_PACKET_SIZE];
        struct sockaddr_in sender_addr;
        socklen_t sender_len = sizeof(sender_addr);
        ssize_t received = recvfrom(context->video_socket, (char *)packet, (int)sizeof(packet), 0,
                                    (struct sockaddr *)&sender_addr, &sender_len);

        if (received < 0) {
            int error = c64_get_socket_error();
#ifdef _WIN32
            if (error == WSAEWOULDBLOCK) {
                Sleep(0);
                continue;
            }
            // On Windows, WSAENOTSOCK means socket was closed - this is normal during shutdown
            if (error == WSAENOTSOCK && context->video_socket == INVALID_SOCKET_VALUE) {
                C64_LOG_DEBUG("Video socket closed (WSAENOTSOCK) - exiting receiver thread gracefully");
                break; // Socket was closed, exit gracefully
            }
            // On Windows, WSAESHUTDOWN means socket was shutdown - this is normal during reconnection
            if (error == WSAESHUTDOWN) {
                C64_LOG_DEBUG("Video socket shutdown (WSAESHUTDOWN) - waiting for reconnection");
                os_sleep_ms(100); // Wait for reconnection to complete
                continue;         // Continue waiting instead of exiting thread
            }
#else
            if (error == EAGAIN || error == EWOULDBLOCK) {
                os_sleep_ms(1);
                continue;
            }
            // On POSIX, EBADF means socket was closed - this is normal during shutdown
            if (error == EBADF && context->video_socket == INVALID_SOCKET_VALUE) {
                C64_LOG_DEBUG("Video socket closed (EBADF) - exiting receiver thread gracefully");
                break; // Socket was closed, exit gracefully
            }
#endif
            C64_LOG_ERROR("Video socket error: %s (error code: %d)", c64_get_socket_error_string(error), error);
            break;
        }
        { // Scope block for shared packet processing code
#endif

            if (received != C64_VIDEO_PACKET_SIZE) {
                // Small packets (2-4 bytes) are normal during stream startup/buffer changes
                // Log as debug to avoid confusing users with normal control/startup packets
                static uint64_t last_incomplete_log_time = 0;
                uint64_t now = os_gettime_ns();
                if (now - last_incomplete_log_time >= 2000000000ULL) { // Throttle to every 2 seconds
                    if (received <= 4) {
                        C64_LOG_DEBUG("Video startup/control packets: " SSIZE_T_FORMAT
                                      " bytes (normal during initialization)",
                                      SSIZE_T_CAST(received));
                    } else {
                        C64_LOG_WARNING("Received incomplete video packet: " SSIZE_T_FORMAT " bytes (expected %d)",
                                        SSIZE_T_CAST(received), C64_VIDEO_PACKET_SIZE);
                    }
                    last_incomplete_log_time = now;
                }
                continue;
            }

            uint64_t packet_time = os_gettime_ns();
            context->last_udp_packet_time = packet_time; // DEPRECATED - kept for compatibility
            context->last_video_packet_time = packet_time;

            os_atomic_set_long(&context->video_packets_received,
                               os_atomic_load_long(&context->video_packets_received) + 1);
            os_atomic_set_long(&context->video_bytes_received,
                               os_atomic_load_long(&context->video_bytes_received) + (long)received);

            // Log network packet at UDP reception (conditional - no parsing overhead if disabled)
            c64_log_video_packet_if_enabled(context, packet, received, packet_time);

            // Parse packet header for validation (always needed for packet validation)
            uint16_t pixels_per_line = *(uint16_t *)(packet + 6);
            uint8_t lines_per_packet = packet[8];
            uint8_t bits_per_pixel = packet[9];

            // Simple approach: just count packets received, no complex sequence tracking
            uint64_t now = os_gettime_ns();
            if (now - context->last_stats_tick_ns >= 50000000ULL) { // ~50ms cadence
                context->last_stats_tick_ns = now;
                c64_process_video_statistics_batch(context, now);
            }

            if (lines_per_packet != C64_LINES_PER_PACKET || pixels_per_line != C64_PIXELS_PER_LINE ||
                bits_per_pixel != 4) {
                static uint64_t last_invalid_log = 0;
                uint64_t now_invalid = os_gettime_ns();
                if (now_invalid - last_invalid_log >= 5000000000ULL) { // 5 sec throttle
                    C64_LOG_WARNING("Invalid packet format: lines=%u, pixels=%u, bits=%u", lines_per_packet,
                                    pixels_per_line, bits_per_pixel);
                    last_invalid_log = now_invalid;
                }
                continue;
            }

            if (context->network_buffer) {
                c64_network_buffer_push_video(context->network_buffer, packet, received, now);
            } else {
                c64_process_video_packet_direct(context, packet, received, now);
            }

#ifdef __linux__
        } // End batch packet processing loop
#else
        } // End scope block
#endif

#ifdef C64_ENABLE_TIMING_INSTRUMENTATION
        // Accumulate timing for this iteration
        uint64_t iter_end = os_gettime_ns();
        context->timing_recv_total_ns += (iter_end - iter_start);
        context->timing_recv_count++;

        // Log timing stats at 1Hz
        if (iter_end - context->timing_last_log_ns >= 1000000000ULL) {
            uint64_t recv_avg_us = context->timing_recv_count > 0
                                       ? (context->timing_recv_total_ns / context->timing_recv_count / 1000)
                                       : 0;
            uint64_t processor_avg_us =
                context->timing_processor_count > 0
                    ? (context->timing_processor_total_ns / context->timing_processor_count / 1000)
                    : 0;
            uint64_t tick_avg_us = context->timing_tick_count > 0
                                       ? (context->timing_tick_total_ns / context->timing_tick_count / 1000)
                                       : 0;

            C64_LOG_INFO("⏱️ Hot-path timing: recv=%" PRIu64 "us/%u iters, processor=%" PRIu64
                         "us/%u iters, tick=%" PRIu64 "us/%u iters",
                         recv_avg_us, context->timing_recv_count, processor_avg_us, context->timing_processor_count,
                         tick_avg_us, context->timing_tick_count);

            // Reset counters for next interval
            context->timing_last_log_ns = iter_end;
            context->timing_recv_total_ns = 0;
            context->timing_recv_count = 0;
            context->timing_processor_total_ns = 0;
            context->timing_processor_count = 0;
            context->timing_tick_total_ns = 0;
            context->timing_tick_count = 0;
        }
#endif // C64_ENABLE_TIMING_INSTRUMENTATION
    }

    C64_LOG_DEBUG("Video receiver thread stopped");

#ifdef _WIN32
    timeEndPeriod(1);
#endif

    return NULL;
}

// Calculate ideal timestamp for a frame based on sequence number and video standard
static uint64_t c64_calculate_ideal_timestamp(struct c64_source *context, uint16_t frame_num)
{
    // Initialize timing base if not already set - prefer audio's base if already set for A/V sync
    if (!context->timestamp_base_set) {
        // Use audio's timing base if already established (ensures A/V sync)
        // Otherwise use current real time
        if (context->audio_base_time > 0) {
            context->stream_start_time_ns = context->audio_base_time;
            C64_LOG_INFO("📐 Video using audio timing base for A/V sync: %" PRIu64 " ns",
                         context->stream_start_time_ns);
        } else {
            context->stream_start_time_ns = os_gettime_ns();
            C64_LOG_INFO("📐 Video timing base established: %" PRIu64 " ns", context->stream_start_time_ns);
        }
        context->timestamp_base_set = true;
    }

    // Set first frame reference for video calculations
    if (context->first_frame_num == 0 || frame_num < context->first_frame_num) {
        context->first_frame_num = frame_num;
        C64_LOG_INFO("📐 Video first frame reference: %u", frame_num);
    }

    // Calculate frame offset from the first frame
    int32_t frame_offset = (int32_t)(frame_num - context->first_frame_num);

    // Handle sequence number wraparound (16-bit counter)
    if (frame_offset < -32768) {
        frame_offset += 65536; // Wrapped forward
    } else if (frame_offset > 32768) {
        frame_offset -= 65536; // Wrapped backward
    }

    // Calculate ideal timestamp: base + (frame_offset * frame_interval)
    // Handle negative offsets correctly by using signed arithmetic first
    int64_t signed_offset_ns = (int64_t)frame_offset * (int64_t)context->frame_interval_ns;
    uint64_t ideal_timestamp = context->stream_start_time_ns + signed_offset_ns;

    // Debug log occasionally to verify timestamp calculation
    static uint32_t log_counter = 0;
    if ((log_counter++ % 250) == 0) { // Log every 250 frames (~5 seconds at 50Hz)
        C64_LOG_DEBUG("📐 Ideal timestamp: frame %u (offset %d) = %" PRIu64 " ns", frame_num, frame_offset,
                      ideal_timestamp);
    }

    return ideal_timestamp;
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
    context->frame_dirty = true;

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
        C64_LOG_DEBUG("⚫ BLACK SCREEN SPOT CHECK: %ux%u RGBA, timestamp=%" PRIu64 " (total count: %d)", context->width,
                      context->height, timestamp_ns, black_screen_debug_count);
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
                    if (frame_diff > 0) {
                        C64_LOG_WARNING("📽️ FRAME SKIP: Expected frame %u, got %u (skipped %d frames)", expected_next,
                                        frame_num, frame_diff);
                    } else if (frame_diff < 0) {
                        C64_LOG_WARNING("Frame sequence regression: Expected frame %u, got %u (offset %d frames)",
                                        expected_next, frame_num, -frame_diff);
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
                            // Calculate ideal timestamp for smooth OBS async video rendering
                            uint64_t ideal_timestamp =
                                c64_calculate_ideal_timestamp(context, context->current_frame.frame_num);

                            // Direct rendering - assembly and output with ideal timestamp!
                            c64_render_frame_direct(context, &context->current_frame, ideal_timestamp);
                            context->last_completed_frame = context->current_frame.frame_num;

                            // Track diagnostics
                            context->frames_completed++;
                            context->total_pipeline_latency += (os_gettime_ns() - capture_time);
                        }
                    } else {
                        // Frame timeout - log drop and continue
                        C64_LOG_WARNING("⏰ FRAME TIMEOUT: Frame %u timed out with %u/%u packets (%.1f%% complete)",
                                        context->current_frame.frame_num, context->current_frame.received_packets,
                                        context->current_frame.expected_packets,
                                        (context->current_frame.received_packets * 100.0f) /
                                            context->current_frame.expected_packets);
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
            }
        } else {
            C64_LOG_WARNING("❌ INVALID PACKET: Frame %u, Line %u out of range (packet_index %u >= %d) - seq %u",
                            frame_num, line_num, packet_index, C64_MAX_PACKETS_PER_FRAME, seq_num);
            context->packet_drops++;
        }

        // Update expected packet count and detect video format based on last packet
        if (last_packet && context->current_frame.expected_packets == 0) {
            context->current_frame.expected_packets = packet_index + 1;

            // Detect PAL vs NTSC format from frame height
            uint32_t frame_height = line_num + lines_per_packet;
            if (!context->format_detected || context->detected_frame_height != frame_height) {
                context->detected_frame_height = frame_height;
                context->format_detected = true;

                // Calculate expected FPS and frame interval based on detected format
                if (frame_height == C64_PAL_HEIGHT) {
                    context->expected_fps = 50.125;
                    context->frame_interval_ns = C64_PAL_FRAME_INTERVAL_NS;
                    context->last_connected_format_was_pal = true; // Update logo format preference
                    C64_LOG_INFO("🎥 Detected PAL format: 384x%u @ %.3f Hz", frame_height, context->expected_fps);
                } else if (frame_height == C64_NTSC_HEIGHT) {
                    context->expected_fps = 59.826;
                    context->frame_interval_ns = C64_NTSC_FRAME_INTERVAL_NS;
                    context->last_connected_format_was_pal = false; // Update logo format preference
                    C64_LOG_INFO("🎥 Detected NTSC format: 384x%u @ %.3f Hz", frame_height, context->expected_fps);
                } else {
                    // Unknown format, estimate based on packet count
                    context->expected_fps = (frame_height <= 250) ? 59.826 : 50.125;
                    context->frame_interval_ns = (frame_height <= 250) ? C64_NTSC_FRAME_INTERVAL_NS
                                                                       : C64_PAL_FRAME_INTERVAL_NS;
                    context->last_connected_format_was_pal = (frame_height > 250); // Assume PAL for larger heights
                    C64_LOG_WARNING("⚠️ Unknown video format: 384x%u, assuming %.3f Hz", frame_height,
                                    context->expected_fps);
                }

                // Update context dimensions if they changed
                if (context->height != frame_height) {
                    context->height = frame_height;
                    context->width = C64_PIXELS_PER_LINE; // Always 384
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

void *c64_video_processor_thread_func(void *data)
{
    struct c64_source *context = data;
    uint64_t last_logo_frame_time = 0;
    uint64_t last_retry_attempt = 0;
    const uint64_t logo_frame_interval_ns = 20000000ULL; // 50Hz (20ms) for logo frames
    const uint64_t retry_interval_ns = 1000000000ULL;    // 1 second retry interval

    C64_LOG_DEBUG("Video processor thread started");

    // Initialize last_frame_time to 0 so logo shows immediately on startup
    context->last_frame_time = 0;

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

        if (context->network_buffer) {
            const uint8_t *video_data, *audio_data;
            size_t video_size, audio_size;
            uint64_t timestamp_us;

            if (c64_network_buffer_pop(context->network_buffer, &video_data, &video_size, &audio_data, &audio_size,
                                       &timestamp_us)) {

                if (video_data && video_size > 0) {
                    c64_process_video_packet_direct(context, video_data, video_size, timestamp_us * 1000);

                    // Reset retry count on successful video packet processing
                    if (context->retry_count > 0) {
                        C64_LOG_INFO("Video stream restored, resetting retry count (was %u)", context->retry_count);
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
                    C64_LOG_DEBUG("Significant timing issue: last_video_packet_time ahead by %" PRIu64
                                  "ns (%.1fms) - investigating",
                                  timing_diff, (double)timing_diff / 1000000.0);
                }
                time_since_last_video = 0;
            }

            uint64_t time_since_last_logo = current_time - last_logo_frame_time;
            uint64_t time_since_last_retry = current_time - last_retry_attempt;

            // Sanity check to prevent timestamp overflow (should never exceed ~1 hour)
            if (time_since_last_video > 3600000000000ULL) {
                C64_LOG_DEBUG("Long-running stream: resetting video timing base after %" PRIu64 "ns (%.1f hours)",
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
            if (time_since_last_video > retry_interval_ns && time_since_last_retry >= retry_interval_ns &&
                !os_atomic_load_long(&context->retry_in_progress)) {
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

    C64_LOG_DEBUG("Video processor thread stopped");
    return NULL;
}
