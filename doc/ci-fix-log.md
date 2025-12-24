# CI Fix Log - Sharp Pixels & E2E Testing

**Created:** 2025-12-24
**Branch:** `copilot/extend-crt-afterglow-effect`
**Goal:** Get CI green with working sharp_pixels E2E test

## Reference: Known-Good Build

**Successful build:** [PR #379, run 20444787063](https://github.com/chrisgleissner/c64stream/actions/runs/20444787063)
- All E2E tests passed (Ubuntu 24.04, Debian 12, Fedora 40, Arch Linux)
- A/V sync pops working correctly
- This was BEFORE the sharp_pixels changes

## Current State

### Modified Files (Uncommitted)
- `data/effect_presets.ini` - pixel_width/height = 4.0 for Sharp Pixels preset
- `data/effects/crt_effect.effect` - UV snapping for pixel expansion
- `src/c64-properties.c` - max slider value 3.0 → 4.0
- `src/c64-source.c` - pass source_width/height to shader
- `src/c64-types.h` - added source_width/height to context struct
- `tests/e2e/assertions/sharp_pixels.py` - block-based verification

### Key Issue: sharp_pixels Assertion Failing

**Symptom:** Detected blocks are 12x12, 17x15, etc. instead of 4x4

**Root Cause Analysis:**
1. Scene config shows `"scale_filter": "disable"` instead of `"point"`
2. OBS is applying bilinear interpolation, causing blur
3. The generated scene JSON has correct settings, but OBS isn't using them

**Evidence from frame analysis:**
```
Block 1: pos=(198,0) size=175x222 area=25893  ← Top-left marker (expected)
Block 2: pos=(369,0) size=19x17 area=93       ← Blurry dot (should be 4x4)
```

Pixel values show gradients (36→95→157→186) proving bilinear interpolation is active.

## Hypotheses

### H1: Scene not being loaded correctly ❓
- Generated scene has `scale_filter: "point"` but used scene has `scale_filter: "disable"`
- The E2E framework may be defaulting to base template instead of scenario override

### H2: OBS source size mismatch ❓
- Sharp Pixels preset sets pixel_width=4.0, pixel_height=4.0
- Source renders at 4x native resolution (384×4 = 1536, 240×4 = 960)
- But scene config shows OBS applying 3.97x scale on top of that
- This suggests OBS doesn't know the source is already scaled

### H3: Scenario loader not applying preset correctly ❓
- scenario.yaml specifies `preset: Sharp Pixels`
- Need to verify preset settings are being applied to source settings

## Investigation Plan

1. [ ] Check if scenario_loader.py applies preset settings to source
2. [ ] Verify the scene generation flow for ntsc_sharp_pixels
3. [ ] Check if e2e.sh uses the generated scene or base template
4. [ ] Test with explicit scene override file

## Excluded Solutions (from successful build)

The successful build used:
- OBS 32.0.2
- E2E tests with PAL/NTSC default scenarios
- A/V sync pops working
- NO sharp_pixels scenario (that's new)

Key insight: The successful build didn't test sharp_pixels, so we can't compare directly.

## Action Log

### 2025-12-24 Session 1

**12:30** - Initial analysis of sharp_pixels assertion failure
- User confirmed PNG extraction shows correct 4x4 blocks visually
- But video assertion finds 12x12+ blocks

**12:35** - Rewrote sharp_pixels.py with block-based verification:
- Find all white blocks using connected components
- Verify each block is 4x4 (3-5 allowed for compression)
- Verify solid white content
- Verify surrounded by black

**12:40** - Test shows blocks are much larger than expected (12x12, 17x15)
- Root cause: scene config has `scale_filter: "disable"`
- OBS is applying bilinear interpolation on already-scaled source

**12:50** - **ROOT CAUSE FOUND:**
- local-build.sh looks for `overrides` directory by default
- e2e.sh generates scene to `generated` directory
- Generated scene has correct settings but NEVER GETS APPLIED
- Log shows: "Scenario overrides directory not found for NTSC Sharp Pixels"

**12:51** - Fix: Added `overrides_dir: generated` to scenario.yaml
- This tells local-build.sh to use the generated directory as overrides

**12:55** - Scene settings now correct:
- `scale: 1.0` ✓
- `scale_filter: point` ✓
- `pixel_width/height: 4.0` ✓
- BUT blocks are still 20x16 instead of 4x4!

**13:00** - **SECOND ROOT CAUSE FOUND:**
- local-build.sh doesn't pass `--scenario` to e2e.sh
- Therefore e2e.sh never calls `load_scenario()` 
- Therefore pattern is never extracted from scenario.yaml
- Default diagonal pattern is used instead of dots pattern

**13:01** - Fix: Added `--scenario "$scenario_key"` to e2e_args in local-build.sh

**13:10** - New test shows:
- Dots pattern being generated ✓
- Scene overrides being applied ✓
- Config shows scale_filter=point, scale=1.0 ✓
- BUT recording file not saved (OBS killed before finalization)

**13:15** - OBS log analysis:
- Recording started correctly
- Video data received for ~5 seconds (296 frames)
- Then "No video packets" again (UDP replay finished)
- OBS killed before Recording Stop event
- MP4 file never finalized

**Key insight:** The test framework is now correctly configured. The recording issue is a separate problem related to test teardown timing.

**Next Steps:**
1. The scene configuration fix is correct - commit these changes
2. The recording file archival has a separate issue
3. Need to investigate why previous tests had the recording but this one doesn't

---

## Ideas & Notes

- The Sharp Pixels effect scales source internally to 4x
- OBS should NOT apply any additional scaling
- Scene item should have scale=1.0 and scale_filter=point
- If source outputs 1536x960 (NTSC) or 1536x1088 (PAL), OBS just displays it

- **Key insight:** When pixel_width/height are used, the C64 source outputs at scaled resolution, not native. OBS scene item should have scale=1.0.

**13:55** - **SUCCESS!** Fresh E2E test passes:
- E2E test completed: all validations passed
- Recording file saved: 14.5MB MP4
- A/V sync: Perfect (100%) - avg offset 9.9ms, max 10.7ms
- **Sharp pixels assertion: 315/315 blocks are 4x4 with black surround ✓**

---

## Summary of Fixes Made

1. **scenario.yaml**: Added `overrides_dir: generated` 
   - Tells local-build.sh where to find generated scene JSON
   
2. **local-build.sh**: Added `--scenario "$scenario_key"` to e2e_args
   - Enables e2e.sh to load scenario.yaml and use correct pattern (dots vs diagonal)

---

## Metrics

| Attempt | Date | Result | Issue |
|---------|------|--------|-------|
| 1 | 2025-12-24 | FAIL | Blocks 12x12+ instead of 4x4, bilinear interpolation |
| 2 | 2025-12-24 | PASS | 315/315 blocks are 4x4 with black surround ✅ |
