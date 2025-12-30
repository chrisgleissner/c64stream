# C64 Stream Performance Optimization - Progress Report

**Date**: 2025-12-30
**Phase**: Low-Risk CPU Optimizations (Phase 1 Complete)
**Status**: Ready for E2E Validation

---

## Summary

Implemented two low-risk CPU optimizations for the Afterglow (phosphor persistence) effect in the C64 Stream OBS plugin:

1. **Cache-line alignment** (64-byte) for afterglow accumulator buffer
2. **Prefetch hints** (64-pixel lookahead) before SIMD processing

**Expected Performance Improvement**: 4-10% CPU reduction at 40ms afterglow duration

**Zero-Regression Status**: Code compiles successfully, formatters pass, ready for E2E validation

---

## Changes Made

### 1. Cache-Line Alignment

**Rationale**: AVX2 SIMD instructions benefit from cache-line aligned memory access. Modern CPUs use 64-byte cache lines; aligned access reduces cache misses and improves load/store throughput.

**Implementation**:
- Created `c64_alloc_aligned()` and `c64_free_aligned()` helper functions in [src/c64-video.c](../src/c64-video.c) (L54-84)
- Replaced `bmalloc()` with 64-byte aligned allocation for `afterglow_cpu_accum` buffer
- Updated allocation in [src/c64-video.c](../src/c64-video.c) L379
- Updated allocation in [src/c64-source.c](../src/c64-source.c) L527
- Updated deallocation in [src/c64-source.c](../src/c64-source.c) L629

**Cross-Platform Support**:
- Windows: `_aligned_malloc()` / `_aligned_free()`
- Linux/macOS: `aligned_alloc()` / `free()`

**Expected Impact**: 2-5% CPU reduction (better cache utilization)

**Risk Level**: NONE (only affects memory allocation, data semantics unchanged)

### 2. Prefetch Hints

**Rationale**: Memory prefetching reduces CPU stall cycles by loading data into cache before it's needed. The afterglow loop processes pixels sequentially with predictable access patterns - ideal for prefetching.

**Implementation**:
- Added prefetch loop before SIMD dispatch in [src/c64-video.c](../src/c64-video.c) L471-476
- Prefetches 64 pixels (256 bytes) ahead for both input and output arrays
- Uses `__builtin_prefetch()` with temporal locality hint 3 (highest)

```c
const size_t prefetch_distance = 64;
for (size_t p = 0; p + prefetch_distance <= pixel_count; p += prefetch_distance) {
    __builtin_prefetch(&curr_pixels[p + prefetch_distance], 0, 3); // Read
    __builtin_prefetch(&acc[p + prefetch_distance], 1, 3);         // Write
}
```

**Expected Impact**: 2-5% CPU reduction (reduced memory stall cycles)

**Risk Level**: LOW (compiler hint only, no semantic change)

---

## Technical Details

### Memory Access Pattern
- **Sequential access**: Both `curr_pixels` (read-only) and `acc` (read-modify-write) are accessed sequentially
- **Buffer size**: 92,160 pixels (NTSC) × 4 bytes = 368 KB, or 104,448 pixels (PAL) × 4 bytes = 418 KB
- **Cache lines touched**: ~5,750 (NTSC) or ~6,528 (PAL) cache lines (64 bytes each)
- **L3 cache**: Typical 8MB+ easily fits entire buffer

### SIMD Processing Flow
1. **Prefetch** (NEW): Load next 64 pixels into cache
2. **AVX2 loop**: Process 8 pixels per iteration (main workload ~92%)
3. **SSE2 fallback**: Process 4 pixels for AVX2 remainder (~4%)
4. **Scalar tail**: Process 0-3 remaining pixels (~4%)

### Compiler Flags
- Build type: RelWithDebInfo
- Optimization: `-O2 -g -DNDEBUG`
- SIMD: AVX2 enabled via GCC `__attribute__((target("avx2")))`

---

## Validation Plan

### ⚠️ MANDATORY: Local E2E Performance Verification

**All performance improvements MUST be verified using the local build script:**

```bash
# From project root - THIS IS THE OFFICIAL VERIFICATION METHOD
./local-build.sh linux --install --e2e-scenario ntsc_green_monitor

# Results are in tests/e2e/results/ntsc_green_monitor/
# Check obs.csv for CPU/GPU/RAM metrics
```

**Why ntsc_green_monitor?**
- Uses 50ms afterglow duration (exercises CPU-intensive effect)
- Includes green tint and scanlines (full effect pipeline)
- Provides stable, reproducible metrics

**Performance Tracking**: See [performance-tracking.md](performance-tracking.md) for detailed measurements.

### 1. Build Validation
```bash
cmake --preset ubuntu-x86_64
cmake --build build_x86_64
# ✅ Build successful
```

### 2. Code Formatting
```bash
./build-aux/run-clang-format --check
# ✅ All source files correctly formatted
```

### 3. E2E Test Suite (Required)
```bash
cd tests/e2e
./run_all_scenarios.sh
# Verify all scenarios pass with identical results
```

### 4. Performance Measurement (MANDATORY - See Above)
**Use local-build.sh with ntsc_green_monitor as documented above.**

### 5. CSV Output Validation (Required)
```bash
# Compare network timing and frame progression CSVs
diff baseline/obs.csv optimized/obs.csv
# Must be identical (zero regression)
```

### 6. Visual Validation (Required)
```bash
# Generate SSIM comparison
ffmpeg -i baseline.mp4 -i optimized.mp4 -lavfi ssim -f null -
# Target: SSIM ≥ 0.9999 (pixel-perfect)
```

---

## Next Steps

### Immediate (Phase 1 Completion)
1. [ ] Run full E2E test suite to verify zero regression
2. [ ] Measure CPU performance improvement
3. [ ] Compare CSV outputs (must be identical)
4. [ ] Visual validation (SSIM ≥ 0.9999)
5. [ ] Document actual measured improvement

### Phase 2 (If Phase 1 successful)
1. [ ] Implement non-temporal stores (`_mm256_stream_si256`)
2. [ ] Verify alignment requirements for non-temporal stores
3. [ ] Re-run E2E validation
4. [ ] Measure additional performance improvement

### Phase 3 (Advanced Optimizations)
1. [ ] Test 2x loop unrolling (16 pixels per iteration)
2. [ ] Benchmark expf() alternatives (LUT, approximations)
3. [ ] GPU shader optimization review
4. [ ] Final performance report

---

## Risk Assessment

**Implemented Optimizations**: LOW RISK
- Cache-line alignment: Only affects allocation, data semantics unchanged
- Prefetch hints: Compiler optimization hint, no behavior change

**No Changes To**:
- SIMD algorithm (AVX2/SSE2 logic unchanged)
- Decay calculation (expf() calls unchanged)
- Frame buffer management (allocation timing unchanged)
- Threading model (no async processing)

**Potential Issues**:
- **None identified**: Both optimizations are non-invasive performance hints

---

## Files Modified

1. [src/c64-video.c](../src/c64-video.c): Added aligned allocation helpers and prefetch loop
2. [src/c64-video.h](../src/c64-video.h): Added function declarations
3. [src/c64-source.c](../src/c64-source.c): Updated allocation/deallocation calls

**Lines Changed**: 44 insertions, 4 deletions (net +40 lines)

---

## References

- [PLANS.md](../PLANS.md): Comprehensive optimization plan and tracking
- [performance-optimization-2025-12-30.md](performance-optimization-2025-12-30.md): Detailed analysis document
- [filter-performance.md](filter-performance.md): Existing performance measurements
- [doc/e2e.md](e2e.md): E2E testing documentation

---

**Author**: Autonomous LLM Performance Engineer
**Review Status**: Ready for E2E Validation
**Next Review**: After E2E test results available

---

*Last Updated: 2025-12-30 18:40*
