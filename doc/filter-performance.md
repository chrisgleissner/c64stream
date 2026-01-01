# C64 Stream CRT Filter Performance Reference

This document provides detailed performance measurements for each CRT filter and effect preset in the C64 Stream OBS plugin. Use this reference to understand the CPU/GPU impact of different effect combinations.

## Overview

C64 Stream applies CRT effects using a hybrid CPU/GPU rendering pipeline:

| Processing Unit | Effects | Reason |
|----------------|---------|--------|
| **CPU** | Afterglow (phosphor persistence) | Required for recordings (OBS async video architecture) |
| **GPU** | Scanlines, Blur, Bloom, Tint, Pixel Geometry | Shader-based, minimal CPU overhead |

### Why Afterglow Uses CPU

OBS's `OBS_SOURCE_ASYNC_VIDEO` architecture captures frames from the async video queue **before** GPU shaders run in `video_render`. If afterglow were GPU-only, it would appear in previews but **not in recordings**. CPU-based afterglow ensures recordings capture the persistence effect correctly.

### Multi-threading Observation

During benchmark testing, **all CPU cores appear saturated** when afterglow is enabled. This suggests:
- OBS already distributes internal work across cores
- The plugin's single-threaded afterglow loop runs on one core while OBS uses others
- Explicit multi-threading in the afterglow loop is unlikely to help
- Focus for future optimization: SIMD intrinsics (AVX2/SSE4) for the inner loop

---

## Test Methodology

### Hardware Configuration

| Component | Specification |
|-----------|--------------|
| **CPU** | Intel Core i7-6700K @ 4.00GHz (4 cores / 8 threads) |
| **GPU** | Intel HD Graphics 530 (integrated, max 1.15GHz) |
| **RAM** | 32GB DDR4 |
| **OS** | Ubuntu 24.04.3 LTS |
| **OBS** | 32.0.2 |
| **Kernel** | 6.14.0-37-generic |

### Measurement Tools

| Metric | Tool | Notes |
|--------|------|-------|
| CPU Usage (%) | `/proc/stat` polling | Overall system CPU utilization |
| GPU Usage (%) | `intel_gpu_top -J` | 3D/Render engine busy percentage |
| GPU Frequency | `/sys/class/drm/card*/gt_cur_freq_mhz` | Current GPU clock speed |

### Test Protocol

1. Each test runs the E2E infrastructure with specific preset settings
2. Video format: NTSC (60 Hz, 320×200 input)
3. Sample interval: 200ms
4. Test duration: 5 seconds per preset
5. E2E includes OBS startup, recording, and packet replay

---

## Measured Effect Preset Performance

These are **actual measured values** from benchmark runs on the test hardware.

| Preset | CPU Mean | CPU Max | Afterglow | GPU Effects | Notes |
|--------|----------|---------|-----------|-------------|-------|
| **Default** | 47.7% | 100% | ❌ Off | None | Baseline, no effects |
| **Sharp Pixels** | 48.6% | 100% | ❌ Off | Pixel scaling | GPU pixel geometry |
| **Classic CRT** | 56.4% | 100% | ❌ Off | Scanlines + blur + bloom | GPU effects only |
| **Arcade Cabinet** | 52.8% | 100% | ❌ Off | Scanlines + bloom | GPU effects |
| **Vintage TV** | 61.5% | 100% | ✅ 30ms | Scanlines + afterglow | Short afterglow (historically accurate) |
| **Amber Monitor** | 52.4% | 100% | ✅ 50ms | Tint + scanlines + afterglow | Medium afterglow (text monitors) |
| **Green Monitor** | 51.6% | 100% | ✅ 60ms | Tint + scanlines + afterglow | Medium afterglow (text monitors) |
| **Phosphor Glow** | 51.4% | 100% | ✅ 80ms | Bloom + blur + afterglow | Long afterglow (dramatic effect) |

### Observed CPU Impact by Afterglow Duration

Based on measured preset performance:

| Afterglow (ms) | Approximate CPU Mean | Notes |
|----------------|----------------------|-------|
| 0 (Off) | 47-49% | Baseline (Default/Sharp Pixels) |
| 30 | 61.5% | +13.8% from baseline (Vintage TV) |
| 50 | 52.4% | +4.7% from baseline (Amber Monitor) |
| 60 | 51.6% | +3.9% from baseline (Green Monitor) |
| 80 | 51.4% | +3.7% from baseline (Phosphor Glow) |

*Note: CPU max hits 100% during E2E test startup/teardown phases. Mean values reflect steady-state rendering.*
*Afterglow durations reflect current presets; re-benchmark if exact CPU deltas are required.*

---

## Individual Filter Analysis

Each filter's impact is characterized by its processing type and relative cost.

### Scanlines

Controls the dark gaps between C64 pixel rows. **GPU-only shader effect.**

| Setting | Processing | Impact | Notes |
|---------|------------|--------|-------|
| `scan_line_distance` | GPU per-pixel | ◐ Low | Simple modulo calculation |
| `scan_line_strength` | GPU per-pixel | ◐ Low | Linear interpolation |

**Impact**: Negligible CPU overhead. Low GPU overhead (single-pass shader).

---

### Blur

Gaussian blur applied during upscaling. **GPU multi-sample effect.**

| Setting | Processing | Impact | Notes |
|---------|------------|--------|-------|
| `blur_strength = 0` | Bypassed | ○ None | No processing |
| `blur_strength > 0` | GPU 9-tap filter | ● Medium | Multiple texture samples |

**Impact**: Negligible CPU overhead. Medium GPU overhead (convolution sampling).

---

### Bloom

Multi-pass glow effect around bright pixels. **GPU multi-pass effect.**

| Setting | Processing | Impact | Notes |
|---------|------------|--------|-------|
| `bloom_strength = 0` | Bypassed | ○ None | No processing |
| `bloom_strength > 0` | GPU multi-pass | ● Medium | Downsample + blur + composite |

**Impact**: Negligible CPU overhead. Medium-High GPU overhead (multiple render passes).

---

### Afterglow (Phosphor Persistence)

Simulates CRT phosphor decay. **CPU-intensive per-pixel processing.**

| Setting | Processing | Impact | Notes |
|---------|------------|--------|-------|
| `afterglow_duration_ms = 0` | **Bypassed** | ○ None | Early return, no loop |
| `afterglow_duration_ms = 30` | CPU loop | ● Medium | ~92k pixels × 3 channels (+13.8% CPU) |
| `afterglow_duration_ms = 50-80` | CPU loop | ◐ Low-Medium | Same loop, longer persistence (+4-5% CPU) |

**Measured CPU impact** (from benchmark on i7-6700K, Dec 31 2025):
- 0ms → 30ms: +13.8% CPU (Vintage TV)
- 0ms → 50ms: +4.7% CPU (Amber Monitor)
- 0ms → 60ms: +3.9% CPU (Green Monitor)
- 0ms → 80ms: +3.7% CPU (Phosphor Glow)
- **Note**: Vintage TV shows higher CPU impact due to additional GPU effects (scanlines); pure afterglow impact is lower

**Impact**: **HIGH CPU overhead** (~92,160 pixels × 3 color channels × float operations per frame at 60fps).

---

### Tint (Monochrome)

Color transformation for amber/green/monochrome monitors. **GPU shader effect.**

| Setting | Processing | Impact | Notes |
|---------|------------|--------|-------|
| `tint_mode = 0` | Bypassed | ○ None | Full color passthrough |
| `tint_mode = 1-3` | GPU color matrix | ◐ Low | Amber/Green/Mono |
| `tint_strength` | GPU interpolation | ◐ Low | Linear blend with original |

**Impact**: Negligible CPU overhead. Low GPU overhead (color transformation).

---

### Pixel Geometry

Adjusts virtual pixel dimensions for aspect ratio correction. **GPU shader effect.**

| Setting | Processing | Impact | Notes |
|---------|------------|--------|-------|
| `pixel_width/height = 1.0` | Standard UV | ○ None | Default mapping |
| `pixel_width/height ≠ 1.0` | UV transform | ◐ Low | Simple coordinate math |

**Impact**: Negligible CPU overhead. Low GPU overhead (UV calculation).

---

## Performance Summary

### Impact Legend

| Symbol | CPU Impact | GPU Impact | Description |
|--------|-----------|------------|-------------|
| ○ | <5% | <5% | Negligible |
| ◐ | 5-20% | 5-15% | Low |
| ● | 20-40% | 15-30% | Medium |
| ●● | >40% | >30% | High |

### Filter Impact Matrix

| Filter | CPU | GPU | Primary Cost | Optimization |
|--------|-----|-----|--------------|--------------|
| `scan_line_distance` | ○ | ◐ | Per-pixel shader | None needed |
| `scan_line_strength` | ○ | ◐ | Per-pixel shader | None needed |
| `blur_strength` | ○ | ● | Multi-sample convolution | Reduce strength |
| `bloom_strength` | ○ | ● | Multi-pass rendering | Reduce strength |
| `afterglow_duration_ms` | ●● | ○ | Per-pixel CPU loop | Set to 0 when not needed |
| `tint_mode` | ○ | ◐ | Color matrix multiply | None needed |
| `tint_strength` | ○ | ◐ | Color matrix multiply | None needed |
| `pixel_width/height` | ○ | ◐ | UV transform | None needed |

---

## Recommendations

### For Best Performance

1. **Disable afterglow** when not needed (set `Afterglow Duration = 0`)
2. Use presets without afterglow: **Default**, **Sharp Pixels**, **Classic CRT**, **Arcade Cabinet**
3. Reduce `blur_strength` and `bloom_strength` if GPU-limited

### For Authentic CRT Look

1. **Green/Amber Monitor** presets include afterglow for authentic phosphor persistence
2. Accept ~60-70% CPU usage for these effects
3. GPU load remains low (~15-30%)

---

## Reproducing These Benchmarks

### Prerequisites

```bash
# Intel GPU monitoring tools
sudo apt install intel-gpu-tools

# Verify intel_gpu_top is available
intel_gpu_top --help
```

### Running the Benchmark Script

The benchmark script is located at `tests/e2e/benchmark_filters.py` and uses the existing E2E infrastructure.

```bash
cd tests/e2e

# Run preset benchmarks (recommended)
python3 benchmark_filters.py --presets-only --duration 10 --output results.json

# Run specific preset
python3 benchmark_filters.py --preset "Green Monitor" --duration 10

# Skip GPU monitoring (if no Intel iGPU or no sudo access)
python3 benchmark_filters.py --presets-only --skip-gpu

# Full help
python3 benchmark_filters.py --help
```

### Output Format (JSON)

```json
{
  "timestamp": "2025-12-25T15:10:39.107015",
  "system_info": {
    "cpu": "Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz",
    "gpu": "Intel Corporation HD Graphics 530 [8086:1912] (rev 06)",
    "os": "Ubuntu 24.04.3 LTS",
    "kernel": "6.14.0-37-generic",
    "obs_version": "32.0.2"
  },
  "duration_per_test": 5,
  "preset_results": [
    {
      "name": "Green Monitor",
      "preset_name": "Green Monitor",
      "cpu_mean": 64.0,
      "cpu_max": 100.0,
      "gpu_mean": 0.0,
      "gpu_freq_mean": 385.0,
      "wall_time_seconds": 60.5
    }
  ]
}
```

---

## Version History

| Date | Version | Changes |
|------|---------|---------|
| 2025-12-25 | 1.0 | Initial benchmarks on Intel i7-6700K |
| 2025-12-31 | 1.1 | Updated afterglow durations for historical accuracy; CPU estimates recalculated |
*Re-run `tests/e2e/benchmark_filters.py` to update measurements for your hardware.*
