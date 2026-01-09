# Frame Progression Marker Detection

## Overview

The frame progression assertion verifies that video frames are correctly sequenced by detecting a position marker in the bottom-left corner of each frame. This document explains the detection algorithm and the adaptive thresholds that ensure robustness across different platforms and encoding environments.

## Marker Design

The position marker is a horizontal progress bar with 8 slots (0-7) inside a framed corner element:

```
C64 coordinates (384x272):
- Outer element: 88x56 pixels (aspect ratio 1.57, matches C64)
- Frame: 1px white outer + 7px black inner = 8px total border
- Inner content: 72x40 pixels starting at offset (8, 8)
- Position bar: 8 slots × 7px + 7 gaps × 1px = 63px, centered in 72px
```

## Detection Algorithm

### 1. Central Horizontal Scanline Sampling

Instead of analyzing the entire marker area, we sample a **single horizontal line** through the vertical middle of the progress bar. This line travels across all 8 slots from left to right, providing a robust 1D luminance profile even with effects and scaling.

### 2. Temporal Delta Detection

The detector finds which slot had the **largest brightness INCREASE** compared to the previous frame. This approach is robust against:

- **Afterglow effects**: Previous positions remain partially lit
- **Monochrome presets**: Works with Green Monitor, Amber Monitor (luminance only)
- **Compression artifacts**: Temporal coherence reduces noise sensitivity

### 3. Adaptive Thresholds

**Problem**: Fixed thresholds fail across different encoding environments:
- Different OBS versions (30.2.2 vs 32.0.2)
- Different codecs and bitrates
- Different CPU architectures (AMD EPYC vs Intel)
- Different effect combinations and scaling factors

**Solution**: Compute thresholds as **percentages of observed signal ranges**:

```python
# 1. Calculate signal characteristics for this frame
lum_range = max(slot_luminances) - min(slot_luminances)
delta_range = max_delta - min(deltas)

# 2. Compute adaptive thresholds
adaptive_min_delta = max(2.0, 0.15 * lum_range)      # 15% of range, min 2.0
adaptive_min_margin = max(0.3, 0.10 * delta_range)   # 10% of range, min 0.3
adaptive_min_contrast = max(4.0, 0.20 * lum_range)   # 20% of range, min 4.0
adaptive_min_brightness = max(15.0, 0.25 * max_lum)  # 25% of max, min 15.0

# 3. Apply adaptive thresholds
if (max_delta >= adaptive_min_delta and
    delta_margin >= adaptive_min_margin and
    lum_contrast >= adaptive_min_contrast and
    slot_lum >= adaptive_min_brightness):
    return detected_slot
```

### 4. Fallback: Absolute Brightness

When temporal delta detection fails (first frame or ambiguous delta), fall back to detecting the brightest slot using adaptive contrast:

```python
adaptive_min_contrast = max(4.0, 0.15 * max_lum)
if max_lum - min_lum >= adaptive_min_contrast:
    return brightest_slot
```

## Ambiguous Frame Handling

Frames where detection fails are marked as "ambiguous" and excluded from analysis. The assertion fails if:

```
ambiguous_ratio = ambiguous_frames / analyzed_frames > 0.40
```

**Before adaptive thresholds**: Fedora 40 CI hit >40% ambiguous ratio (sporadic failures)
**After adaptive thresholds**: Ambiguous ratio dropped to 0% (robust detection)

## Threshold Rationale

### Temporal Delta Thresholds

1. **Delta (15% of luminance range, min 2.0)**
   - Ensures detected slot is meaningfully brighter than previous frame
   - Scales with video encoding quality and effect intensity
   - Minimum prevents false positives in very dark scenes

2. **Margin (10% of delta range, min 0.3)**
   - Ensures detected slot is clearly the brightest (not ambiguous)
   - Rejects frames where multiple slots have similar brightness increases
   - Minimum handles low-contrast scenarios

3. **Contrast (20% of luminance range, min 4.0)**
   - Ensures detected slot is brighter than minimum slot brightness
   - Verifies the marker is visible (not washed out or too dark)
   - Minimum ensures some minimum signal quality

4. **Brightness (25% of max brightness, min 15.0)**
   - Ensures detected slot is reasonably bright in absolute terms
   - Prevents detection in very dark regions
   - Minimum handles scenes with overall low luminance

### Fallback Contrast Threshold

**Fallback contrast (15% of max luminance, min 4.0)**
- Lower than temporal threshold since we don't have delta information
- Ensures some minimum contrast exists for absolute brightness detection
- Minimum prevents false detection in uniform dark regions

## Testing

Run E2E tests with various formats and effects:

```bash
# Basic test (PAL, 300 frames)
cd tests/e2e
./e2e.sh --format PAL --frames 300 --verbose

# Test with network simulation (jitter + reordering)
./e2e.sh --scenario ntsc_delay_500ms --verbose

# Full scenario suite
scenarios=$(./e2e.sh --list-scenarios | awk -F: '/^  [a-z0-9_]+:/{print $1}' | tr -d ' ')
for s in $scenarios; do
    ./e2e.sh --scenario "$s" --duration 5 || exit 1
done
```

## Metrics

Key metrics reported in validation results:

- `valid_frames`: Number of frames where marker was detected
- `ambiguous_frames`: Number of frames where detection failed
- `ambiguous_ratio`: ambiguous_frames / analyzed_frames (must be < 0.40)
- `distinct_slots`: Number of unique slots detected (must be ≥3 for progression)

## Debugging

Enable verbose mode to see detection details:

```bash
./e2e.sh --format PAL --frames 300 --verbose
```

Look for:
```
[frame_progression] Content: left=X, right=Y, top=Z, bottom=W, scale=S
[frame_progression] Using content bounds: START-END
[frame_progression] Filtering false positive repeat at frame N (alternating pattern)
```

Check metrics in `results/validation_results.json`:
```bash
python3 -c "
import json
with open('results/validation_results.json') as f:
    data = json.load(f)
    fsb = data['frame_sequence_box']
    print(f\"Ambiguous ratio: {fsb['metrics']['ambiguous_ratio']:.3f}\")
    print(f\"Valid frames: {fsb['details']['valid_frames']}\")
"
```

## Performance Impact

The adaptive threshold computation adds negligible overhead:
- Calculations: 4 min/max operations + 4 multiplications per frame
- Total cost: <1μs per frame (vs ~16ms frame time at 60 FPS)
- Memory: No additional allocations

## References

- Implementation: `tests/e2e/assertions/frame_progression.py`
- Test content generation: `tests/e2e/util/generate_packets.py` (position marker rendering)
- E2E test harness: `tests/e2e/e2e.py`
- CI workflow: `.github/workflows/build-and-test.yml`
