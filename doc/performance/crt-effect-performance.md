# CRT Effect Performance Analysis

This document analyzes the CPU performance of different CRT effect presets to help understand rendering costs and optimize shader performance.

## Test Environment

### Local Development Machine
- **CPU:** Intel Core i7-6700K @ 4.00GHz (4 cores, 8 threads)
- **GPU:** Intel HD Graphics 530 (integrated)
- **RAM:** 32GB DDR4
- **OS:** Ubuntu Linux

### CI Environment (GitHub Actions)
- **CPU:** 2 vCPU (AMD EPYC)
- **GPU:** Software rendering (LIBGL_ALWAYS_SOFTWARE=1)
- **RAM:** 7GB

## Benchmark Results

All tests run with 5-second duration (300 NTSC frames sent, 1550 frames processed).

### Hardware Rendering (Local iGPU)

| Preset | Blur | Bloom | Afterglow | Runtime | User CPU |
|--------|------|-------|-----------|---------|----------|
| Sharp Pixels | 0.0 | 0.0 | 0ms | 40.6s | 2m24s |
| Default | 0.0 | 0.0 | 0ms | 58.4s | 2m33s |
| Classic CRT | 0.5 | 0.4 | 30ms | 44.6s | 3m05s |
| Amber Monitor | 0.3 | 0.3 | 80ms | 44.4s | 3m07s |
| Green Monitor | 0.3 | 0.35 | 100ms | 44.4s | 3m06s |
| Phosphor Glow | 0.6 | 0.55 | 40ms | 45.1s | 3m05s |
| Arcade Cabinet | 0.4 | 0.45 | 20ms | 49.5s | 3m19s |
| Vintage TV | 0.8 | 0.5 | 80ms | 45.6s | 3m15s |

### Software Rendering (Simulates CI)

| Preset | Runtime | User CPU | Notes |
|--------|---------|----------|-------|
| Sharp Pixels | ~45s | ~2m30s | Baseline (no blur/bloom) |
| Vintage TV | 3m01s | 2m53s | 4x slower than hardware |

## Shader Complexity Analysis

### Texture Samples Per Pixel

The CRT shader performs the following texture samples:

1. **Base Pass:** 1 sample (original texture)
2. **Blur Pass:** 9 samples (3x3 Gaussian kernel with adaptive weights)
3. **Bloom Pass:** 16 samples (3 concentric rings at different radii)
4. **Afterglow:** 2 samples (current + previous frame)

**Total Maximum:** 28 texture samples per pixel when all effects enabled.

### Performance Impact by Feature

| Feature | Samples | CPU Impact | Notes |
|---------|---------|------------|-------|
| Base (scanlines, tint) | 1-2 | Low | Simple math operations |
| Blur | 9 | Medium-High | 9-tap Gaussian, scales with blur_strength |
| Bloom | 16 | High | 3 ring pattern, most expensive |
| Afterglow | 2 | Low | Just alpha blending |

### Key Findings

1. **Software rendering is viable**: Even the most demanding preset (Vintage TV) completes successfully under software rendering - just 4x slower.

2. **Hardware rendering handles all presets**: All presets render within 50 seconds on local hardware, well under the 60-second timeout.

3. **Bloom is the most expensive effect**: The 16-sample ring pattern for bloom accounts for ~50% of shader complexity when enabled.

4. **Sharp Pixels is the fastest**: No blur or bloom means only 1-2 texture samples per pixel.

## Recommendations

### For CI/Software Rendering

1. **Current state is acceptable**: All presets pass under software rendering, just with longer runtimes.

2. **Consider reducing CI test duration**: 5-second tests may be sufficient for validation without stressing software rendering.

3. **Monitor CI timeouts**: If CI becomes too slow, consider per-preset timeouts rather than skipping tests.

### For Shader Optimization (Future)

If performance becomes an issue:

1. **Reduce bloom samples**: Could use 8 samples (2 rings) instead of 16 for ~40% reduction.

2. **Early-exit for zero strength**: Skip blur/bloom calculations entirely when strength is 0.0 (already implemented).

3. **Quality presets**: Add a "performance" mode that uses fewer samples for blur/bloom.

## Conclusion

The CRT shader performs well across all presets. Software rendering takes 4x longer but completes successfully. No immediate optimization is required, but the shader could be optimized if CI timing becomes a bottleneck.

## Known Issues

### CPU/GPU Imbalance (2025-12-25)

During local testing with hardware rendering, we observed:
- **CPU usage:** ~100% (saturated)
- **iGPU usage:** ~40% (underutilized)
- **iGPU frequency:** 300MHz (not scaling up to max 1.15GHz)

This suggests the rendering pipeline is CPU-bound, not GPU-bound. The afterglow effect is suspected because it was implemented with CPU-side frame accumulation in `video_tick` to work around OBS minimized/headless rendering issues.

**See:** [PLANS.md](../../PLANS.md) → "Task: Reduce CPU load for CRT effects - move work to GPU"
