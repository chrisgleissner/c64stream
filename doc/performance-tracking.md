# C64 Stream Performance Optimization Tracking

**Purpose**: Track performance impact of each optimization change with measured E2E test results.

**Test Command**: `./local-build.sh linux --install --e2e-scenario ntsc_green_monitor`

**Test Scenario**: ntsc_green_monitor (NTSC format with 50ms afterglow + green tint + scanlines)

---

## Performance Metrics Table

| Date | Git Commit | Change Description | CPU Mean (%) | CPU Max (%) | GPU Mean (%) | RAM (MB) | Δ CPU vs Baseline | Notes |
|------|------------|-------------------|--------------|-------------|--------------|----------|-------------------|-------|
| 2025-12-30 | TBD | **BASELINE** (before optimizations) | TBD | TBD | TBD | TBD | 0% | Establish baseline metrics first |
| 2025-12-30 | TBD | Cache-line alignment (64-byte) | TBD | TBD | TBD | TBD | TBD | Aligned allocation for afterglow buffer |
| 2025-12-30 | TBD | Prefetch hints (64-pixel lookahead) | TBD | TBD | TBD | TBD | TBD | Memory prefetch before SIMD loop |

---

## Measurement Methodology

### E2E Test Execution
```bash
# From project root
./local-build.sh linux --install --e2e-scenario ntsc_green_monitor

# Results location
# Check tests/e2e/results/ntsc_green_monitor/obs.csv for metrics
```

### Metric Extraction
- **CPU Mean/Max**: Extract from `tests/e2e/results/ntsc_green_monitor/obs.csv` (cpu_mean, cpu_max columns)
- **GPU Mean**: Extract from `tests/e2e/results/ntsc_green_monitor/obs.csv` (gpu_mean column)
- **RAM**: Extract from `tests/e2e/results/ntsc_green_monitor/obs.csv` (ram_mb column)

### Target
- **Minimum**: 20% CPU reduction vs baseline
- **Stretch**: 25-30% CPU reduction

---

## Change Log (Detailed)

### Baseline (TBD)
**Status**: Pending measurement
**Purpose**: Establish reference performance before optimizations
**Expected**: CPU ~42-43% mean (based on filter-performance.md)

### Optimization 1: Cache-Line Alignment
**Status**: Implemented, pending measurement
**Files**: src/c64-video.c, src/c64-video.h, src/c64-source.c
**Changes**:
- Added c64_alloc_aligned() / c64_free_aligned() helpers
- Replaced bmalloc() with 64-byte aligned allocation for afterglow_cpu_accum
- Cross-platform support (Windows: _aligned_malloc, Linux: aligned_alloc)

**Expected Impact**: 2-5% CPU reduction
**Measured Impact**: TBD after E2E test

### Optimization 2: Prefetch Hints
**Status**: Implemented, pending measurement
**Files**: src/c64-video.c
**Changes**:
- Added prefetch loop before SIMD dispatch (64-pixel lookahead)
- Prefetches both curr_pixels (read) and acc (write) arrays
- Uses __builtin_prefetch with temporal locality 3

**Expected Impact**: 2-5% CPU reduction
**Measured Impact**: TBD after E2E test

---

## Notes

### Test Requirements
- Tests MUST be run locally (not in CI/cloud)
- Full build + install + E2E scenario execution
- Scenario: ntsc_green_monitor (exercises afterglow effect)
- Duration: Default (sufficient for stable metrics)

### Validation Criteria
- CPU reduction ≥ 20% vs baseline
- GPU usage stable or reduced (no increases)
- RAM usage stable or reduced (no increases)
- All E2E assertions pass (zero regression)
- Visual output identical (SSIM ≥ 0.9999)

---

*Last Updated: 2025-12-30 (Initial version)*
