# C64 Stream Performance Optimization Analysis
**Date**: 2025-12-30
**Focus**: Afterglow CPU optimization with zero-regression mandate
**Status**: Analysis Phase

---

## Executive Summary

This document tracks the performance optimization effort for the C64 Stream OBS plugin, specifically targeting the CPU-intensive Afterglow (phosphor persistence) effect. The optimization maintains **zero regression** in visual output, timing semantics, and E2E assertions.

### Current State Analysis

**Afterglow Implementation** ([src/c64-video.c](../src/c64-video.c)):
- Already SIMD-optimized with AVX2 (8 pixels/iteration) and SSE2 (4 pixels/iteration)
- Runtime CPU detection for optimal instruction set selection
- Per-channel phosphor decay (R/G/B with different time constants)
- Processes 92,160 pixels (NTSC) or 104,448 pixels (PAL) per frame at 60Hz/50Hz

**Measured Performance** (from [filter-performance.md](../doc/filter-performance.md)):
- Hardware: Intel i7-6700K @ 4.00GHz, Intel HD Graphics 530
- Baseline (no effects): 32% CPU mean
- Phosphor Glow (20ms afterglow): 40.8% CPU mean (+8.8 percentage points)
- Green Monitor (50ms afterglow): 42.8% CPU mean (+10.8 percentage points)

**Existing Optimizations**:
- ✅ AVX2/SSE2 intrinsics (3-4x speedup over scalar)
- ✅ Runtime CPUID detection
- ✅ Frame buffer reuse (no malloc/free in hot path)
- ✅ Early exit when disabled
- ✅ Atomic counters for thread safety

---

## Identified Optimization Opportunities

### 1. High-Priority: expf() Call Optimization

**Issue**: Three `expf()` calls per frame for decay factor calculation
```c
decay_r = expf(-dt_ms / tau_r);  // Line 408
decay_g = expf(-dt_ms / tau_g);  // Line 409
decay_b = expf(-dt_ms / tau_b);  // Line 410
```

**Cost**: expf() typically ~20-40 cycles per call = 60-120 cycles per frame
**Impact**: LOW - only called once per frame (not per pixel)
**Opportunity**: Cache decay factors when dt_ms and duration_ms haven't changed
**Zero-regression risk**: NONE - mathematically identical if cached correctly

**Proposed fix**:
- Cache decay_r/decay_g/decay_b when dt_ms and afterglow_duration_ms match last frame
- Invalidate cache on parameter changes
- Expected savings: ~100 cycles per frame (~0.1% CPU at 4GHz, negligible)

**Decision**: SKIP - negligible impact (not per-pixel), focus on SIMD loop

### 2. Medium-Priority: Memory Access Pattern

**Current State**:
- Sequential access to curr_pixels (read-only)
- Sequential read-write to acc (accumulator)
- No explicit prefetch hints
- No cache-line alignment guarantees

**Opportunities**:

#### 2.1 Prefetch Hints
```c
// Add before SIMD loop
for (size_t p = 0; p + 64 <= pixel_count; p += 64) {
    __builtin_prefetch(&curr_pixels[p + 64], 0, 3);  // Read, high temporal locality
    __builtin_prefetch(&acc[p + 64], 1, 3);          // Write, high temporal locality
}
```
**Expected impact**: 2-5% improvement (reduce memory stall cycles)
**Zero-regression risk**: LOW (compiler hint only, no semantic change)

#### 2.2 Cache-Line Alignment
```c
// In c64_get_afterglow_output_pixels():
context->afterglow_cpu_accum = aligned_alloc(64, frame_bytes);  // 64-byte align
```
**Expected impact**: 2-5% improvement (better AVX2 load/store performance)
**Zero-regression risk**: NONE (only affects allocation, not data)

#### 2.3 Non-Temporal Stores
```c
// In AVX2 loop, replace:
_mm256_storeu_si256((__m256i *)&acc[i], result);
// With:
_mm256_stream_si256((__m256i *)&acc[i], result);
```
**Expected impact**: 3-7% improvement (avoid cache pollution on large buffers)
**Zero-regression risk**: LOW (cache behavior only, data identical)
**Caveat**: Requires 32-byte alignment for _mm256_stream_si256

### 3. Medium-Priority: FMA (Fused Multiply-Add)

**Current Code** (AVX2, line ~240):
```c
const __m256 trail_r = _mm256_mul_ps(prev_r, vdecay_r);
// ...later...
__m256 out_r = _mm256_max_ps(curr_r, trail_r);
```

**Optimized with FMA** (AVX2 has FMA support):
```c
// Combine multiply and max in fewer instructions
const __m256 trail_r = _mm256_mul_ps(prev_r, vdecay_r);
__m256 out_r = _mm256_max_ps(curr_r, trail_r);
// No FMA opportunity here - max() not add/sub
```

**Analysis**: FMA not applicable (no add/sub after multiply in critical path)
**Decision**: SKIP - no FMA opportunity in current algorithm

### 4. Low-Priority: Loop Unrolling

**Current**: Process 8 pixels per iteration (AVX2) or 4 pixels (SSE2)

**Proposed**: Process 16 or 32 pixels per iteration (2x/4x unroll)

**Benefits**:
- Reduced loop overhead
- Better instruction-level parallelism (ILP)
- More registers available for intermediate values

**Risks**:
- Larger code size (instruction cache pressure)
- Diminishing returns beyond 2x unroll

**Expected impact**: 3-8% improvement
**Zero-regression risk**: LOW (compiler optimization, semantics identical)

**Decision**: Test 2x unroll (16 pixels) in AVX2 path

---

## Implementation Plan

### Phase 1: Low-Risk Optimizations (Current Focus)

#### Optimization 1: Cache-Line Alignment
**File**: [src/c64-video.c](../src/c64-video.c), line ~352
**Change**: Replace `bmalloc()` with aligned allocation
**Risk**: NONE
**Expected**: 2-5% improvement

#### Optimization 2: Prefetch Hints
**File**: [src/c64-video.c](../src/c64-video.c), line ~430
**Change**: Add prefetch loop before SIMD dispatch
**Risk**: LOW
**Expected**: 2-5% improvement

#### Optimization 3: Non-Temporal Stores
**File**: [src/c64-video.c](../src/c64-video.c), line ~263 (AVX2) and ~174 (SSE2)
**Change**: Replace `_mm256_storeu_si256` with `_mm256_stream_si256`
**Risk**: LOW (requires alignment verification)
**Expected**: 3-7% improvement

**Combined Expected**: 7-17% CPU reduction

### Phase 2: Medium-Risk Optimizations (After Phase 1 validation)

#### Optimization 4: Loop Unrolling (2x)
**File**: [src/c64-video.c](../src/c64-video.c), AVX2 loop
**Change**: Process 16 pixels per iteration
**Risk**: MEDIUM (larger code, needs testing)
**Expected**: 3-8% improvement

**Total Expected (Phases 1+2)**: 10-25% CPU reduction

---

## Zero-Regression Validation Strategy

For each optimization, the following validation steps are **mandatory**:

### ⚠️ MANDATORY: Performance Verification Method

**All performance improvements MUST be verified using:**

```bash
# From project root - THIS IS THE OFFICIAL METHOD
./local-build.sh linux --install --e2e-scenario ntsc_green_monitor
```

**Why this matters:**
- Ensures full build + install + E2E test cycle
- Uses ntsc_green_monitor (50ms afterglow, exercises CPU optimization)
- Provides consistent, reproducible metrics
- Results tracked in [performance-tracking.md](performance-tracking.md)

### 1. Code Formatting
```bash
./build-aux/run-clang-format --check
```

### 2. Build Validation
```bash
cmake --preset ubuntu-x86_64
cmake --build build_x86_64
# Verify c64stream.so exists
```

### 3. E2E Test Suite
```bash
cd tests/e2e
./run_all_scenarios.sh
# All scenarios must pass
```

### 4. CSV Output Comparison
```bash
# Compare network timing CSVs
diff baseline/obs.csv optimized/obs.csv
# Must be identical (byte-for-byte)
```

### 5. Visual Validation
```bash
# Generate SSIM comparison
ffmpeg -i baseline.mp4 -i optimized.mp4 -lavfi ssim -f null -
# Target: SSIM ≥ 0.9999 (pixel-perfect or near-perfect)
```

### 6. Performance Measurement (See Mandatory Section Above)
**Use the local-build.sh method documented at the top of this section.**

## Current Status

**Phase**: 1 - Low-Risk Optimizations (In Progress)
**Completed**:
- ✅ Cache-line alignment (64-byte aligned allocation)
- ✅ Prefetch hints (64-pixel lookahead)

**Next**: Build E2E baseline, verify zero regression, measure performance improvement
**Blocked**: None

### Implementation Log

#### 2025-12-30: Phase 1 Optimizations Implemented

**Optimization 1: Cache-Line Alignment** (Completed)
- **Changes**:
  - Added `c64_alloc_aligned()` and `c64_free_aligned()` helper functions
  - Replaced `bmalloc()` with 64-byte aligned allocation for `afterglow_cpu_accum`
  - Updated allocation in [c64-video.c](../src/c64-video.c) L353-365
  - Updated allocation in [c64-source.c](../src/c64-source.c) L524-532
  - Updated deallocation in [c64-source.c](../src/c64-source.c) L628-633
- **Expected Impact**: 2-5% CPU reduction
- **Risk**: NONE (only affects memory allocation, not data)
- **Validation**: Build succeeded, no errors

**Optimization 2: Prefetch Hints** (Completed)
- **Changes**:
  - Added prefetch loop before SIMD dispatch in [c64-video.c](../src/c64-video.c) L460-465
  - Prefetch 64 pixels (256 bytes) ahead for both `curr_pixels` (read) and `acc` (write)
  - Temporal locality hint: 3 (highest)
- **Expected Impact**: 2-5% CPU reduction
- **Risk**: LOW (compiler hint only, no semantic change)
- **Validation**: Build succeeded, no errors

**Combined Expected**: 4-10% CPU reduction at 40ms afterglow

---

## Current Status

**Phase**: 1 - Low-Risk Optimizations (Completed - Ready for Testing)
**Next**: Run E2E tests to verify zero regression and measure performance
**Blocked**: None

---

## Appendix: Profiling Data

### A. Compiler Flags
- Build type: RelWithDebInfo
- Flags: `-O2 -g -DNDEBUG`
- SIMD: AVX2 enabled via `-mavx2` (GCC target attribute)

### B. SIMD Instruction Mix (Expected)
- AVX2 path: ~92% of pixels (main loop)
- SSE2 fallback: ~4% of pixels (AVX2 remainder)
- Scalar tail: ~4% of pixels (SSE2 remainder)

### C. Memory Access Pattern
- Sequential read: curr_pixels (384*272*4 = 417KB PAL)
- Sequential RMW: acc (same size)
- Cache lines touched: ~6,528 (64-byte lines)
- L3 cache (typical): 8MB+ (fits comfortably)

---

**Last Updated**: 2025-12-30
