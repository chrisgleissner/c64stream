# Effect Scaling Compensation Research

Date: 2026-03-18

## Problem Definition

### Current behavior

When C64 Stream enables any effect that changes pixel geometry, the source/filter can become larger in OBS even though the user only asked for a visual style change.

Today the dimension-affecting settings are:

- `pixel_width`
- `pixel_height`
- `scan_line_distance` (via the scanline unit multiplier)

For the input source, [`src/c64-source.c`](../../src/c64-source.c) does two coupled things:

1. `c64_get_width()` / `c64_get_height()` return an effect-scaled size.
2. `c64_video_render()` draws the sprite at that same scaled size.

For the filter, [`src/video/c64-stream-effects.c`](../../src/video/c64-stream-effects.c) does the same:

1. `c64_stream_effects_get_width()` / `c64_stream_effects_get_height()` return an effect-scaled size.
2. `c64_stream_effects_video_render()` draws the filtered sprite at that scaled size.

OBS therefore sees a physically larger source, so the scene item's visible footprint changes. Existing position, crop, fit, and center transforms are no longer correct for the new size. The preview appears to grow, shift, or crop.

### Root cause

The plugin currently treats effect scaling as part of the source contract instead of treating it as an internal rendering detail.

Current sizing model:

```text
logical source size -> effect scale applied to get_width/get_height
                    -> OBS scene item footprint changes
                    -> existing scene transform no longer matches
```

Current scale model, using `K(scan_line_distance)` as the scanline multiplier:

```text
K(d) = 1  when d == 0
     = 5  when 0 < d <= 0.25
     = 3  when 0.25 < d <= 0.5
     = 4  when 0.5 < d <= 1.0
     = 3  when d > 1.0

W_current = W_logical * pixel_width  * K(d)
H_current = H_logical * pixel_height * K(d)
```

That is correct for "make the source itself larger", but it is the wrong contract for "keep the preview footprint stable while changing internal pixel geometry".

### Assumptions used in this research

- OBS scene item sizing follows `obs_source_get_width()` / `obs_source_get_height()` multiplied by scene-item scale. This is corroborated by OBS UI source code and explains the observed footprint changes.
- `obs_source_get_base_width()` / `obs_source_get_base_height()` expose the pre-filter base size, which is the correct logical footprint anchor for compensation.
- The CRT shader runs per rasterized fragment in the final render target, so the shader can reason about a "virtual effect resolution" that differs from the declared source size.

## Rendering Pipeline Analysis

### Current C64 Stream input source flow

```text
UDP video packets
  -> frame assembly
  -> RGBA frame_buffer (logical C64 size: 384x240 NTSC or 384x272 PAL)
  -> render_texture
  -> c64_video_render()
       if no effects:
         draw at logical width/height
       if effects enabled:
         set shader uniforms
         compute render_width/render_height from effect scale
         gs_draw_sprite(..., render_width, render_height)
  -> OBS scene item uses c64_get_width()/c64_get_height()
```

Important details:

- The backing texture remains at logical size.
- The visible size change happens at draw time and in the reported width/height.
- The shader's `output_height` uniform currently means "scaled output height", not "stable logical output height".

### Current C64 Stream Effects filter flow

```text
target source
  -> obs_source_video_render(target) into texrender at target base size
  -> stage surface map to CPU
  -> CPU afterglow / CPU scanline path (optional)
  -> output_texture at target base size
  -> c64_stream_effects_video_render()
       if no effects:
         draw at logical width/height
       if effects enabled:
         compute render_width/render_height from effect scale
         gs_draw_sprite(..., render_width, render_height)
  -> OBS scene item uses filter get_width()/get_height()
```

Important details:

- The filter already captures at the target's base size.
- The filter also inflates its reported output size, so the parent scene still sees a larger object.

### How OBS interacts with this

Relevant OBS behavior:

- `obs_source_get_base_width()` / `obs_source_get_base_height()` are documented as not taking filtering into account.
- `obs_filter_get_target()` is the supported way for a filter to access its target during render.
- OBS UI code computes on-scene item width/height from `obs_source_get_width(source)` and `obs_source_get_height(source)` times scene-item scale.

That means C64 Stream has one clean lever for preserving size:

```text
Do not change the reported source/filter width/height when the user only wants
internal effect scaling.
```

### Where scaling is applied today

```text
logical texture sample domain: 384x240 or 384x272
shader sample snapping: uses source_width/source_height
visible quad size: effect-scaled
reported source size to OBS: effect-scaled
```

The scaling is therefore externalized twice:

- once in OBS layout (`get_width` / `get_height`)
- once in the draw call (`gs_draw_sprite(..., render_width, render_height)`)

That double encoding is what must change.

## Approach Catalogue

### Approach A: Shader-level compensation with stable declared size

#### Description

Keep the OBS-facing width/height fixed at logical size, but tell the shader what the effect's virtual scale would have been. The shader then preserves scanline spacing, pixel snapping, and bloom/tint behavior inside the unchanged bounding box.

```text
logical source size
  -> report logical size to OBS
  -> draw logical-size quad
  -> shader uses virtual_output_width/height for effect math
```

#### Diagram

```text
input texture (logical)
  -> shader(image, source_size, virtual_effect_size)
  -> draw quad at logical size
  -> OBS sees unchanged source footprint
```

#### Mathematical model

```text
W_virtual = W_logical * pixel_width  * K(d)
H_virtual = H_logical * pixel_height * K(d)

W_declared = W_logical
H_declared = H_logical

Cx = W_declared / W_virtual
Cy = H_declared / H_virtual
```

The key change is semantic, not necessarily algebraic:

- UV snapping still uses `source_width` / `source_height`.
- Scanline row selection uses `virtual_output_height`, not declared output height.
- The quad size stays logical, so OBS layout remains stable.

#### Integration point

- Shared scale helper used by source and filter.
- Shader uniform changes:
  - replace or reinterpret `output_height` as `virtual_output_height`
  - optionally add `virtual_output_width`
- Render path change:
  - draw logical-size sprite when compensation is enabled
- `get_width()` / `get_height()` return logical size when compensation is enabled

#### Complexity

Medium.

Most of the work is contract cleanup and shader-uniform semantics, not new infrastructure.

#### Risks

- If any effect logic implicitly assumes that declared size equals virtual size, it must be updated carefully.
- Very small on-screen displays can still alias fine virtual scanline patterns, because the final raster does not have infinite resolution. This is a display-size limit, not a compensation bug.

### Approach B: Fixed-size render target with post-effect downscale

#### Description

Render the effect into an enlarged offscreen texture, then resolve it back down into a fixed logical-size output texture before giving it to OBS.

```text
logical input
  -> enlarged RT (virtual effect size)
  -> resolve/downscale
  -> fixed logical output RT
  -> OBS sees stable footprint
```

#### Diagram

```text
logical texture
  -> pass 1: expand to virtual RT
  -> pass 2: downscale to logical RT
  -> draw logical RT at logical size
```

#### Mathematical model

```text
RT1 = (W_virtual, H_virtual)
RT2 = (W_logical, H_logical)

resolve = Downsample(RT1 -> RT2)
```

#### Integration point

- New texrender / render target management in source and filter
- Possibly a second shader technique or second effect file for resolve

#### Complexity

High.

It adds RT lifecycle management, pass ordering, and resolve policy choices.

#### Risks

- Downscale policy is the core problem:
  - linear resolve risks blur
  - point resolve risks aliasing / dropped scanline detail
  - custom resolve adds more complexity
- Higher GPU cost and memory bandwidth
- Harder to keep deterministic across backends

### Approach C: OBS transform manipulation

#### Description

Leave current effect behavior alone, but automatically apply the inverse size change to the OBS scene item transform.

```text
plugin reports larger source
  -> plugin finds scene item(s)
  -> plugin applies inverse scale
  -> on-screen footprint appears unchanged
```

#### Diagram

```text
effect scale S
  -> source width/height become S times larger
  -> scene-item scale becomes 1/S
  -> net footprint stays similar
```

#### Mathematical model

```text
scene_scale_x_new = scene_scale_x_old / (pixel_width  * K(d))
scene_scale_y_new = scene_scale_y_old / (pixel_height * K(d))
```

#### Integration point

- OBS scene-item APIs
- Possibly frontend/UI code if a single source has multiple scene items

#### Complexity

High.

The rendering math is simple; the ownership model is not.

#### Risks

- A source can exist in multiple scenes and groups at once.
- Filters do not naturally own scene-item transforms.
- User edits, studio mode, nested scenes, and undo/redo become state-management hazards.
- This is non-local behavior: effect changes would mutate scene layout, which is surprising and hard to test.

Conclusion: technically possible, architecturally weak.

### Approach D: Viewport / projection compensation

#### Description

Keep the shader mostly as-is, but apply an inverse scale in the draw transform or projection so the virtual scale is canceled before the quad reaches OBS layout.

```text
virtual effect scale in shader math
  + inverse scale in draw/projection
  = stable on-screen footprint
```

#### Diagram

```text
draw sprite at virtual size
  -> apply inverse matrix
  -> final logical-size footprint
```

#### Mathematical model

```text
M_final = M_scene * M_inverse_effect * M_draw

M_inverse_effect =
  [ Cx  0   0  0 ]
  [ 0   Cy  0  0 ]
  [ 0   0   1  0 ]
  [ 0   0   0  1 ]
```

#### Integration point

- Vertex transform setup around `gs_draw_sprite`
- Possibly custom quad drawing rather than relying on the current sprite helper

#### Complexity

Medium to High.

It avoids extra RTs, but it is more implicit than simply drawing the logical-size quad and exposing explicit virtual-size uniforms.

#### Risks

- Easy to misread later because the size contract is hidden in transform math
- Still requires shader semantics cleanup for scanline row math
- Less obvious for test authors and future maintainers

Conclusion: viable, but less clear than Approach A.

### Approach E: Effect metadata model

#### Description

Each effect exposes whether it changes virtual pixel density and by how much. The engine then applies compensation automatically.

This is not a standalone renderer strategy. It is a coordination layer that makes the chosen renderer strategy deterministic.

#### Diagram

```text
preset/settings
  -> effect metadata
       scale_x
       scale_y
       virtual_output_width
       virtual_output_height
       compensation mode
  -> render path consumes metadata
```

#### Mathematical model

```text
metadata.scale_x = pixel_width  * K(d)
metadata.scale_y = pixel_height * K(d)
metadata.virtual_width  = W_logical * metadata.scale_x
metadata.virtual_height = H_logical * metadata.scale_y
```

#### Integration point

- Shared helper or struct in the effects engine
- Used by both source and filter
- Used by UI, script, and tests for introspection

#### Complexity

Low to Medium.

#### Risks

- By itself it solves nothing unless paired with A, B, or D.
- If metadata is recomputed in multiple places instead of shared, drift is likely.

Conclusion: necessary as a supporting mechanism, but not sufficient alone.

### Approach F: Hybrid logical-vs-physical resolution split

#### Description

Separate two concepts explicitly:

- Logical output footprint: what OBS layout sees
- Virtual effect resolution: what the effect math sees

Use metadata to derive virtual scale, then implement compensation using stable declared size plus shader/projection logic.

```text
logical footprint is stable
virtual effect resolution is flexible
OBS transform stays untouched
```

#### Diagram

```text
logical source size
  -> metadata derives virtual effect size
  -> render logical-size quad
  -> shader uses virtual effect size
  -> optional legacy mode can still expose virtual size to OBS
```

#### Mathematical model

```text
logical_size  = (W_logical, H_logical)
virtual_size  = (W_virtual, H_virtual)
reported_size = preserve_size ? logical_size : virtual_size
draw_size     = preserve_size ? logical_size : virtual_size
```

#### Integration point

- Shared effects metadata helper
- Source and filter `get_width` / `get_height`
- Source and filter draw calls
- Shader uniform contract
- UI and script control surface

#### Complexity

Medium.

#### Risks

- Requires a deliberate migration story because existing scenes may depend on legacy behavior.
- Documentation for "Perfect Scan Lines" must be rewritten around stable displayed size, not effect-specific source growth.

Conclusion: this is the strongest architectural direction.

## Comparative Evaluation Table

| Approach | Visual correctness | Performance | Implementation complexity | Compatibility with OBS | Risk level |
| --- | --- | --- | --- | --- | --- |
| A. Shader-level compensation with stable declared size | High | High | Medium | High | Medium |
| B. Fixed-size RT with post-effect downscale | Medium | Medium to Low | High | High | Medium to High |
| C. OBS transform manipulation | Medium in simple scenes, Low in complex scenes | High | High | Low | High |
| D. Viewport / projection compensation | High | High | Medium to High | High | Medium |
| E. Effect metadata only | Low as a standalone approach | High | Low | High | Medium |
| F. Hybrid logical-vs-physical split | High | High | Medium | High | Low to Medium |

Summary:

- Approach C is rejected because it pushes rendering compensation into scene-layout mutation.
- Approach B is rejected as the default because it pays extra GPU cost and makes blur/alias trade-offs unavoidable.
- Approach E is required as infrastructure, but not as the whole answer.
- Approaches A and D are both viable render mechanics.
- Approach F is the best complete architecture because it combines the clarity of A with the determinism of E.

## Edge Cases

### Non-integer scaling

`pixel_width` / `pixel_height` can be non-integer today. Compensation must not assume integer-only values.

Recommended handling:

- Virtual scale remains floating-point.
- UV snapping still targets source texel centers when `blur_strength == 0`.
- Documentation should state that exact pixel-perfect guarantees require integer displayed scaling in OBS, not merely integer internal effect scale.

### Scanline modes and "Perfect Scan Lines"

The README currently tells users to change OBS transform sizes for each scanline mode because the plugin output size changes. With compensation enabled, that workflow should change:

- one stable displayed size
- point filtering still required for the sharpest result
- effect changes no longer require transform edits

Important constraint:

- exact scanline regularity still depends on the final displayed height being appropriate for the chosen pattern
- if the displayed size is too small, any scanline solution will alias

### Aspect ratio mismatches

Because the current code multiplies both width and height by the scanline unit, compensation must preserve aspect ratio exactly the same way in virtual space. Do not special-case scanlines as a purely vertical feature unless the effect design itself changes.

### Different OBS canvas sizes

A correct solution must be canvas-agnostic:

- if a scene item was centered before enabling an effect, it remains centered
- if a scene item was fit to bounds before enabling an effect, it remains fit
- if a scene item was cropped before enabling an effect, it keeps the same crop box relative to its logical footprint

Stable reported size achieves that. Scene mutation does not.

### High-DPI / retina displays

The compensation design should be validated using the rendered scene output, not the UI preview widget alone. High-DPI preview scaling can change editor presentation, but the scene render/output contract should remain identical.

### PAL vs NTSC

Logical source size differs:

- NTSC: 384 x 240
- PAL: 384 x 272

The compensation math must anchor to the detected logical size first, then apply virtual scale on top. No PAL/NTSC-specific compensation branch is otherwise needed.

### Filter attached to arbitrary sources

The filter must anchor to the target's base width/height for stable footprint. The compensation model must not assume C64-native aspect ratios when the filter is attached to a media source, capture card, or emulator feed.

## Recommended Architecture

### Preferred approach

Adopt Approach F:

- stable logical output contract
- shared effect metadata model
- shader-level compensation as the default render mechanic
- optional legacy mode that preserves current "effect grows source" behavior

### Why this is preferred

1. It solves the actual contract problem.

The bug exists because OBS sees a larger source. Returning stable width/height removes the layout instability at the source.

2. It preserves OBS ownership boundaries.

OBS keeps controlling transforms. C64 Stream keeps controlling rendering. No scene-item mutation is needed.

3. It preserves sharp rendering better than RT downscale approaches.

No compulsory resolve pass means no forced blur-vs-alias trade-off.

4. It is equally applicable to the input source and the filter.

Both paths already compute the same scale factors. They can share metadata and policy.

5. It is deterministic and testable.

The compensation rule can be expressed as simple, shared math and verified in unit/E2E tests.

### Concrete architecture direction

Introduce a shared concept such as:

```text
struct c64_effect_geometry {
    float scale_x;
    float scale_y;
    uint32_t logical_width;
    uint32_t logical_height;
    float virtual_width;
    float virtual_height;
    bool preserve_size;
};
```

Behavior:

- Compute geometry once from logical size plus effect settings.
- When `preserve_size = true`:
  - `get_width()` / `get_height()` return logical size
  - source/filter draw logical-size quad
  - shader receives virtual size for scanline/pixel-geometry math
- When `preserve_size = false`:
  - current behavior remains available

Recommended shader contract changes:

- Treat `source_width` / `source_height` as source texel domain only.
- Replace `output_height` with `virtual_output_height`.
- Add `virtual_output_width` if horizontal virtual geometry needs it.
- Do not infer visible footprint from virtual size.

### Why alternatives were rejected

#### OBS transform manipulation

Rejected because it breaks source/render ownership boundaries, scales poorly with multiple scene items, and is too easy to make nondeterministic.

#### Fixed-size RT plus downscale

Rejected as the default because it introduces extra GPU work and makes scanline/pixel sharpness dependent on resolve strategy.

#### Metadata-only

Rejected as incomplete. It is necessary infrastructure, but it must feed a render strategy.

#### Pure projection compensation without explicit logical/virtual split

Rejected as the primary design because it is harder to reason about. The same math is acceptable internally, but the public architecture should still be expressed as a logical/virtual split.

## API / Script Design Proposal

### User-facing model

Default behavior should be:

```basic
EFFECT "Classic CRT"
```

Result:

- preset applies
- effect scaling is compensated automatically
- preview size and position remain stable

### Proposed property / setting

Add a source/filter setting:

- `preserve_size` = 1 by default

Suggested UI label:

- "Preserve preview size"

Suggested UI help text:

- "Keep the OBS source footprint stable when effects change internal pixel scaling."

### C64Script exposure

Preferred script surface:

```basic
EFFECT "Classic CRT"
EFFECTPARAM "preserve_size" 1
```

Optional override:

```basic
EFFECT "Classic CRT"
EFFECTPARAM "preserve_size" 0
```

### Why this belongs in EFFECTPARAM

Reasons:

- The current parser grammar only supports `EFFECT <preset_name>`.
- `KEEP_SIZE=1` would require grammar, AST, bytecode, runtime, and spec changes for optional named arguments on `EFFECT`.
- `preserve_size` is orthogonal to which preset is active; it is a source/filter behavior flag, not part of the preset identity.
- `EFFECTPARAM` already maps naturally to per-source setting writes.

Recommendation:

- Use `EFFECTPARAM "preserve_size" <0|1>` first.
- Only consider `EFFECT "Preset" KEEP_SIZE=1` later if the language gains a general optional-argument pattern for plugin action statements.

### Backward compatibility implications

There is a real behavior change risk:

- older scenes may have been manually arranged around the current inflated output size
- flipping the default to compensation may make those scenes appear smaller

Recommended migration strategy:

1. Add `preserve_size` with default ON for new sources/filters.
2. Treat missing `preserve_size` in old configs as the new default if the product requirement is strict.
3. Provide an explicit legacy escape hatch:
   - UI checkbox
   - `EFFECTPARAM "preserve_size" 0`
4. Call out the behavior change in release notes and docs.

If a softer rollout is preferred, the safest transitional variant is:

- existing instances without the key keep legacy behavior once
- new instances default to ON

That is more compatible, but it does not satisfy the requirement as strongly.

## Test Strategy

### Goals

Verify that enabling or changing any effect:

- does not change the visible bounds of the source in the scene
- does not move the source center
- does not introduce cropping
- preserves sharpness/scanline alignment expectations

### Unit / focused tests

Add focused coverage for the shared geometry helper:

- logical width/height in PAL and NTSC
- scanline unit mapping for all distance buckets
- `preserve_size = 1` returns logical reported size
- `preserve_size = 0` returns legacy virtual reported size
- source and filter use the same scale math

If shader uniform packing is abstracted, also test:

- logical size
- virtual size
- compensation mode

### E2E tests

Recommended new scenarios:

1. NTSC stable-size effect cycle
   - fixed scene transform
   - cycle `Default -> Classic CRT -> Sharp Pixels -> Vintage TV -> Default`
   - assert stable bounds and stable center

2. PAL stable-size effect cycle
   - same as above with PAL source size

3. Filter path stable-size cycle
   - use `c64_stream_effects` filter on a media source or capture source

4. Legacy opt-out scenario
   - `preserve_size = 0`
   - verify current size-changing behavior still exists intentionally

### Screenshot / frame comparison strategy

Use rendered output frames, not property JSON alone.

Recommended checks per sampled steady-state frame:

- detect content bounds
- compute centroid of visible content
- compare width/height of the visible footprint
- compare against the baseline frame taken before the effect change

Suggested pass thresholds:

- center drift <= 1 px
- bounds delta <= 1 px on each edge
- no content clipped at canvas edges unless it was already clipped in the baseline

Compression note:

- use tolerant edge detection on recorded frames
- if available, prefer PNG still capture or lossless source screenshots for tighter thresholds

### Reuse of existing assertions

Existing assertions already cover related quality dimensions:

- `scanlines`
- `sharp_pixels`
- `effect_transition`
- `content_bounds`

Recommended additions:

- `stable_bounds`
- `stable_center`
- optional OBS websocket assertion:
  - fetch scene-item transform before/after effect change
  - confirm the plugin did not mutate transforms

### Metrics to record

- reported source width/height
- logical width/height
- virtual width/height
- detected content bounds: left, right, top, bottom
- center point: `(cx, cy)`
- crop-to-canvas incidents
- scanline uniformity score
- sharp-pixel block metrics for the Sharp Pixels preset

## Open Questions

1. Should missing `preserve_size` in old saved settings immediately mean ON, or should there be a compatibility window?
2. Does any current shader path depend on "declared size equals virtual size" beyond scanline row math and sprite draw size?
3. Should the filter continue using its current CPU capture path, or should a later refactor move it closer to standard OBS filter processing once compensation is in place?
4. Is an additional explicit "quality mode" needed later for users who want expensive oversampled bloom, separate from the default stable-size compensation path?
5. Should docs redefine "Perfect Scan Lines" in terms of one stable displayed size per scene rather than per-effect transform recipes?

## References

Repository evidence:

- [`src/c64-source.c`](../../src/c64-source.c)
- [`src/video/c64-stream-effects.c`](../../src/video/c64-stream-effects.c)
- [`data/effects/crt_effect.effect`](../../data/effects/crt_effect.effect)
- [`README.md`](../../README.md)
- [`doc/c64script/c64script-spec.md`](../c64script/c64script-spec.md)
- [`src/script/frontend/c64-script-parser.c`](../../src/script/frontend/c64-script-parser.c)
- [`src/script/vm/c64-script-vm-dispatch-effects.c`](../../src/script/vm/c64-script-vm-dispatch-effects.c)
- [`tests/e2e/util/scenario_loader.py`](../../tests/e2e/util/scenario_loader.py)
- [`tests/e2e/assertions/content_bounds.py`](../../tests/e2e/assertions/content_bounds.py)

OBS references used to validate assumptions:

- OBS `obs-source.h` source definition and filter callback documentation:
  - https://sources.debian.org/src/obs-studio/30.2.3%2Bdfsg-3/libobs/obs-source.h/
- OBS `obs.h` width/base-width/filter helper declarations:
  - https://sources.debian.org/src/obs-studio/30.2.3%2Bdfsg-3/libobs/obs.h
- OBS UI code showing scene item sizing from source width/height and scene scale:
  - https://sources.debian.org/src/obs-studio/30.2.3%2Bdfsg-3/UI/window-basic-main.cpp
