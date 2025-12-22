# CRT Phosphor Afterglow Feature Analysis

## Overview

This document analyzes the physically-based CRT phosphor afterglow implementation added to c64stream, identifying potential issues, edge cases, and feature gaps that should be addressed.

## Feature Summary

The afterglow effect simulates CRT phosphor persistence using:
- **CPU-based frame accumulation** for deterministic behavior
- **Per-channel exponential decay** (R/G/B phosphor modeling with tau values 0.6/0.8/0.3)
- **Dual-phase decay** for GradualFade and LongTail curves (70% fast + 30% slow components)
- **Flash detection** to boost newly-lit pixels (enhances fast-moving object visibility)
- **GPU ping-pong buffers** for shader-based accumulation when CRT effects are active
- **Frame-rate independent** operation using delta time calculations

## Potential Issues and Gaps

### 1. Memory Management

**Issue**: Afterglow CPU accumulator allocation timing
```c
// src/c64-source.c: CPU accumulator allocated in video_tick
context->afterglow_cpu_accum = bmalloc(alloc_size);
```
- **Risk**: Memory allocation in hot path (video_tick called per frame)
- **Gap**: No error handling if allocation fails during runtime
- **Impact**: Could cause frame drops or crashes under memory pressure
- **Recommendation**: Pre-allocate in c64_create(), add NULL checks before use

**Issue**: Texture recreation on resize
```c
// Textures destroyed and recreated when resolution changes
gs_texture_destroy(context->render_texture);
context->render_texture = gs_texture_create(...);
```
- **Risk**: Afterglow state lost on resolution change
- **Gap**: CPU accumulator not invalidated when textures recreated
- **Impact**: Visual glitch (one frame of incorrect persistence)
- **Recommendation**: Set `afterglow_cpu_valid = false` after texture recreation

### 2. Thread Safety

**Issue**: Retry thread and UI thread interaction
```c
// Atomic flags added but not consistently used
volatile long retry_in_progress;
volatile long retry_thread_active;
```
- **Risk**: DNS resolution moved to background but settings read without locks
- **Gap**: `context->dns_server_ip` accessed from multiple threads
- **Impact**: Race condition reading/writing DNS settings
- **Recommendation**: Add mutex for configuration data or use atomic operations consistently

**Issue**: Afterglow CPU accumulator access
```c
// CPU accumulator accessed in video_tick and potentially render callback
context->afterglow_cpu_accum
```
- **Risk**: No synchronization between video_tick and render threads
- **Gap**: Potential data race when effects disabled/enabled dynamically
- **Impact**: Corrupted afterglow state, visual artifacts
- **Recommendation**: Add mutex or ensure single-threaded access path

### 3. Configuration and State Management

**Issue**: Preset last-applied tracking
```c
// Preset tracking to prevent re-application
obs_data_set_string(settings, "crt_preset_last_applied", preset);
```
- **Risk**: Import/export doesn't sync "last_applied" correctly
- **Gap**: User imports preset, tweaks sliders, exports - reimport may reset tweaks
- **Impact**: User configuration loss
- **Recommendation**: Clear "last_applied" on import, or don't export it

**Issue**: Default values inconsistency
```c
// Default afterglow: duration=0 (disabled), curve=2 (GradualFade)
obs_data_set_default_int(settings, "afterglow_duration_ms", 0);
obs_data_set_default_int(settings, "afterglow_curve", 2);
```
- **Risk**: Curve has default value even when effect disabled
- **Gap**: Unclear if curve=2 is intentional default for first enable
- **Impact**: Minor UX inconsistency
- **Recommendation**: Document that curve persists when duration=0

### 4. Afterglow Algorithm

**Issue**: Flash detection amplification
```c
// Flash boost: 50% extra brightness for newly-lit pixels
float3 flash_boost = current_rgb * flash_factor * 0.5;
float3 persisted = base_persisted + flash_boost;
```
- **Risk**: Can exceed 1.0 even after saturate() if base_persisted already bright
- **Gap**: No consideration for HDR color space
- **Impact**: Clipping artifacts on bright fast-moving objects
- **Recommendation**: Consider multiplicative boost or pre-saturate check

**Issue**: Dual-phase decay blending
```c
// 70/30 blend of fast/slow decay
final_decay = decay_fast * 0.7 + decay_slow * 0.3;
```
- **Risk**: Hardcoded ratio doesn't match all CRT phosphor types
- **Gap**: No per-channel dual-phase (all channels use same ratio)
- **Impact**: Less physically accurate for some phosphor types
- **Recommendation**: Make blend ratio configurable or use per-channel ratios

**Issue**: Frame rate dependency
```c
// Delta time calculated from wall clock
float dt_ms = (float)(current_time_ns - last_tick_ns) / 1000000.0f;
```
- **Risk**: Variable frame rate causes non-uniform decay
- **Gap**: No frame rate cap or smoothing
- **Impact**: Afterglow "stutters" if frame rate varies
- **Recommendation**: Clamp dt_ms to reasonable range (e.g., 1-100ms)

### 5. CPU vs GPU Accumulation Path

**Issue**: Dual accumulation paths
```c
// GPU path: ping-pong textures + shader
// CPU path: afterglow_cpu_accum buffer
```
- **Risk**: Two different code paths can diverge
- **Gap**: No validation that CPU and GPU paths produce identical results
- **Impact**: E2E tests may pass but GPU rendering differs visually
- **Recommendation**: Add test comparing CPU/GPU output, or consolidate paths

**Issue**: CPU accumulator persistence
```c
// CPU accumulator used for "deterministic" E2E tests
context->afterglow_cpu_valid
```
- **Risk**: CPU path may not be used in production (GPU always available)
- **Gap**: CPU path less tested in real-world scenarios
- **Impact**: Bugs in CPU path may not be caught
- **Recommendation**: Force CPU path in specific scenarios or remove if unused

### 6. Shader Efficiency

**Issue**: Conditional branching in fragment shader
```c
// Multiple if statements in per-pixel shader code
if (afterglow_curve == 0) { ... }
else if (afterglow_curve == 1) { ... }
```
- **Risk**: GPU branching can cause performance degradation
- **Gap**: No profiling data for afterglow shader performance
- **Impact**: Reduced frame rate on complex scenes
- **Recommendation**: Use uniform-based switch or technique variants

**Issue**: Exponential calculations per pixel
```c
// exp() called 3+ times per pixel (once per channel, potentially twice for dual-phase)
float3 decay = exp(-dt_ms / 1000.0 / tau);
```
- **Risk**: Expensive transcendental functions in hot path
- **Gap**: No approximation or optimization
- **Impact**: GPU bottleneck on older hardware
- **Recommendation**: Profile and consider approximations (e.g., exp2 with rescaling)

### 7. Import/Export Functionality

**Issue**: No validation after import
```c
// Import applies values directly without range checks in some cases
obs_data_set_double(settings, "scan_line_distance", os_strtod(value));
```
- **Risk**: Malformed INI file can set invalid values
- **Gap**: Some fields validated (ports), others not (floats)
- **Impact**: Corrupted configuration, potential crashes
- **Recommendation**: Add comprehensive validation for all imported values

**Issue**: File path handling
```c
// Export path handling
c64_ensure_parent_dir_exists(path);
```
- **Risk**: No validation of path length or special characters
- **Gap**: Cross-platform path handling not tested
- **Impact**: Export fails on some file systems
- **Recommendation**: Add path validation, length checks, sanitization

**Issue**: No export/import feedback
```c
// Silent failures in export/import
if (!c64_export_settings_to_ini(settings, path)) { ... }
```
- **Gap**: User not notified if export/import fails
- **Impact**: User thinks settings saved but they weren't
- **Recommendation**: Show OBS info/error message on success/failure

### 8. Effect Interaction

**Issue**: Tint applied before afterglow
```c
// Order: effects → tint → afterglow
// Tint comment says "before afterglow to prevent feedback"
```
- **Risk**: Tint changes color space, afterglow operates on tinted values
- **Gap**: Not clear if this is physically accurate
- **Impact**: Afterglow on tinted output may not match real CRT behavior
- **Recommendation**: Document rationale or test alternative ordering

**Issue**: Bloom interaction with afterglow
```c
// Bloom adds brightness, then afterglow persists it
```
- **Risk**: Bloomed pixels persist longer than original
- **Gap**: No physical basis for bloom+afterglow interaction
- **Impact**: Exaggerated glow effects
- **Recommendation**: Consider applying afterglow before bloom, or document tradeoff

### 9. Resolution and Scaling

**Issue**: Afterglow UV coordinates
```c
// UV clamped to [0,1] but no validation for non-square pixels
float2 uv = clamp(v_in.uv, 0.0, 1.0);
```
- **Risk**: Pixel aspect ratio changes may affect afterglow alignment
- **Gap**: No test for non-1:1 pixel aspect ratios
- **Impact**: Afterglow smearing on scaled content
- **Recommendation**: Test with various pixel geometries

**Issue**: E2E test resolution update
```c
// Tests updated from 1280x720 to 1920x1080
// verify_output.py specs updated
```
- **Risk**: Old test recordings may not match new resolution
- **Gap**: No migration or warning for existing test data
- **Impact**: Test failures if old data present
- **Recommendation**: Document resolution requirement, clean old data

### 10. Localization

**Issue**: Inconsistent locale updates
```c
// 13 locale files updated for Import/Export
// "source" removed from descriptions
```
- **Risk**: Future locale additions may reintroduce "source"
- **Gap**: No automated test for locale consistency
- **Impact**: Confusing UI in some languages
- **Recommendation**: Add locale linting or templates

### 11. Performance and Resource Usage

**Issue**: Multiple texture samples per pixel
```c
// Afterglow samples prev frame
// Bloom samples multiple times
// Blur samples 9 times
```
- **Risk**: High texture bandwidth on older GPUs
- **Gap**: No performance budget or profiling
- **Impact**: Frame rate drops on complex effects
- **Recommendation**: Add performance mode or LOD system

**Issue**: Afterglow always allocates CPU buffer
```c
// CPU accumulator allocated even when GPU rendering active
context->afterglow_cpu_accum = bmalloc(alloc_size);
```
- **Risk**: Wastes memory (384x272 * 4 bytes = ~400KB) when unused
- **Gap**: No conditional allocation based on rendering path
- **Impact**: Unnecessary memory usage
- **Recommendation**: Only allocate CPU accumulator when needed (headless/E2E)

### 12. User Experience

**Issue**: Effect ordering not documented
```c
// Order: pixel geometry → scanlines → blur → bloom → tint → afterglow
```
- **Gap**: Users can't know which effects interact
- **Impact**: Unexpected results when combining effects
- **Recommendation**: Add documentation or UI tooltips explaining order

**Issue**: Preset switching behavior
```c
// Preset changes don't show preview until applied
```
- **Gap**: No real-time preview when selecting presets
- **Impact**: Trial-and-error to find right preset
- **Recommendation**: Consider preview mode or preset thumbnails

## Critical Path Issues (Must Fix)

1. **Memory allocation in hot path** (afterglow_cpu_accum)
2. **Thread safety for retry thread** (DNS resolution race)
3. **Import/export validation** (malformed INI crash risk)
4. **CPU/GPU accumulator divergence** (E2E vs production)

## Medium Priority Issues (Should Fix)

5. **Flash detection saturation** (brightness clipping)
6. **Frame rate dependency** (clamp dt_ms)
7. **Export/import feedback** (user notification)
8. **Texture recreation state loss** (invalidate accumulator)

## Low Priority Issues (Nice to Have)

9. **Shader branching optimization** (performance)
10. **Locale consistency** (automated checks)
11. **Effect interaction documentation** (UX)
12. **Preset preview** (UX enhancement)

## Testing Gaps

- **No unit tests** for afterglow algorithm (exp decay, dual-phase)
- **No performance benchmarks** for shader on various GPUs
- **No validation** that CPU and GPU paths match
- **No stress test** for rapid effect enable/disable
- **No cross-platform test** for import/export paths
- **No locale translation validation** (consistency checks)

## Recommendations for Future Work

1. **Refactor accumulator management**: Pre-allocate, consistent lifecycle
2. **Consolidate CPU/GPU paths**: Single algorithm, multiple backends
3. **Add comprehensive validation**: All imported values, all paths
4. **Implement performance monitoring**: FPS tracking with effects
5. **Create automated locale checks**: Enforce consistency
6. **Document effect interactions**: User guide with examples
7. **Add unit tests**: Core algorithms, edge cases
8. **Profile shader performance**: Optimize hot paths

## LLM-Friendly Summary

**Core feature**: Afterglow simulates CRT phosphor persistence using exponential decay with per-channel time constants and dual-phase curves.

**Critical issues**: Memory allocated in hot path, thread-unsafe DNS access, no import validation, dual accumulation paths may diverge.

**Key gaps**: No performance profiling, missing unit tests, unclear effect interaction order, inconsistent error handling.

**Quick wins**: Clamp dt_ms, pre-allocate accumulator, add import/export feedback, document effect order.

**Long-term**: Consolidate CPU/GPU paths, add comprehensive testing, optimize shader branching, implement locale validation.
