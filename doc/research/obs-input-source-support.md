| Option | User value fit | Feature fidelity | Arch change | Impl complexity | Perf risk | X-platform risk | Backward compat risk | Testability | Maintainability | Impl risk (0-10) | Maint score (0-10) | Status |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| A. In-repo CRT filter (shader + presets, no afterglow) | High | Medium | Medium | Medium | Low-Med | Low | Low | Medium | Medium | 4 | 4 | Viable - lowest effort in-repo |
| A2. External shader filter using crt_effect.effect | Med | Low-Med | None | Low | Low | Med (3rd party) | None | Low | Low | 2 | 3 | Viable - stopgap, external dep |
| B. Shared core for source + filter | High | High | High | High | Med | Med | Med | Med | High | 8 | 7 | Rejected - refactor required |
| C. Input switch to use existing OBS source | High | High | Very high | Very high | Med | High | High | Low | High | 9 | 8 | Rejected - ingest pipeline mismatch |

## Executive summary
The current plugin is an OBS input source that owns UDP ingest, frame assembly, and timing (src/plugin-main.c, src/c64-source.c, src/c64-video.c). The CRT look is applied in the source render path via a shader loaded from data/effects/crt_effect.effect and presets loaded from data/effect_presets.ini (src/c64-effect.c). The lowest-effort path to apply the same look to HDMI capture or other OBS sources is a new OBS filter that reuses the existing shader and preset values but skips the C64 ingest and CPU afterglow path. This keeps architecture changes limited to a new filter source, reuses existing data assets, and avoids refactoring the existing source pipeline.

## Option comparison table
The comparison table is at the top of this document.

## Current architecture (module map)
| Module | Path | Responsibility | Key symbols | Key deps |
| --- | --- | --- | --- | --- |
| Plugin registration | src/plugin-main.c | Registers C64 Stream source as OBS_SOURCE_TYPE_INPUT with custom draw | obs_module_load, obs_register_source, c64_info | c64-effect, c64-palette, c64-source |
| Source lifecycle + render | src/c64-source.c, src/c64-source.h | Source create/update, video_tick, video_render, effect settings | c64_create, c64_update, c64_video_tick, c64_video_render, c64_get_width/height | c64-video, c64-effect, c64-palette, OBS graphics |
| Video ingest + output | src/c64-video.c, src/c64-video.h | UDP ingest, frame assembly, RGBA output, synthetic timestamps | c64_video_thread_func, c64_video_processor_thread_func, c64_render_frame_direct, c64_calculate_ideal_timestamp | c64-protocol, c64-network-buffer, c64-color |
| Network + jitter buffer | src/c64-network.c/h, src/c64-network-buffer.c/h | Socket setup, DNS, packet buffering | c64_create_udp_socket, c64_network_buffer_push/pop | OS sockets, c64-protocol |
| CRT presets | src/c64-effect.c/h, data/effect_presets.ini | Load and apply effect presets into OBS settings | c64_effect_init, c64_effect_apply | obs_module_file |
| CRT shader | data/effects/crt_effect.effect | Shader-based CRT effects (scanlines, bloom, tint) | technique Draw, uniforms: scan_line_distance, pixel_width, bloom_strength | OBS effect system |
| Properties UI | src/c64-properties.c | Defines effect sliders and preset dropdown | c64_create_properties, crt_preset_changed | c64-effect, obs properties |
| Palette system | src/c64-palette.c/h, data/palettes/*.vpl | Palette selection for C64 stream conversion | c64_palette_select, c64_palette_get_active_id | c64-color |

## Effect boundary analysis (portable vs tied)
Smallest portable boundary in this repo:
- CRT shader and parameter set:
  - data/effects/crt_effect.effect (uniforms: scan_line_distance, scan_line_strength, pixel_width/height, blur_strength, bloom_strength, tint_mode, tint_strength)
  - c64_source uses these in c64_video_render (src/c64-source.c)
- Preset loading:
  - src/c64-effect.c/h with data/effect_presets.ini

Tied to C64 ingest or source lifecycle:
- UDP ingest and frame assembly: c64_video_thread_func, c64_video_processor_thread_func, c64_process_video_packet_direct (src/c64-video.c)
- C64 packet format and timing: C64_VIDEO_PACKET_SIZE, C64_FRAME_INTERVAL_NS, c64_calculate_ideal_timestamp (src/c64-protocol.h, src/c64-video.c)
- C64 pixel conversion: c64_convert_pixels_optimized and palette LUT (src/c64-video.c, src/c64-color.h)
- CPU afterglow: c64_get_afterglow_output_pixels uses frame_buffer and SIMD paths (src/c64-video.c)

OBS glue that would need a filter-specific variant:
- Source callbacks and settings: c64_create, c64_update, c64_video_tick, c64_video_render (src/c64-source.c)
- Effect properties UI: c64_create_properties and related effect entries (src/c64-properties.c)

## Viable options

### Option A: Minimal OBS filter inside this plugin (shader + presets, no afterglow)
What the user gets:
- Apply CRT scanlines, bloom, blur, tint, and pixel geometry to any existing OBS source (HDMI capture included).
- Preset values from data/effect_presets.ini and the same shader look as the source render path.

What the user does not get:
- C64 device control, UDP ingest, palette conversion, or C64Script automation (all source-only features).
- CPU afterglow unless a new ping-pong render path is added.

Technical approach summary:
- Register a new OBS_SOURCE_TYPE_FILTER in src/plugin-main.c alongside the existing input source.
- Implement a filter render path that loads data/effects/crt_effect.effect with gs_effect_create_from_file and binds shader params similar to c64_video_render (src/c64-source.c).
- Store filter effect settings in a small filter context (scan_line_distance, scan_line_strength, pixel_width, pixel_height, blur_strength, bloom_strength, tint_mode, tint_strength).
- Reuse c64_effect_init and c64_effect_apply (src/c64-effect.c) to apply presets into the filter settings, using the same keys as the source (scan_line_distance, bloom_strength, etc).
- Build a filter properties panel that mirrors the effects group from src/c64-properties.c (only effect controls and presets).
- Set shader input to the filter source texture, and set source_width/source_height to the input texture size so UV snapping works for arbitrary inputs.

Key risks and mitigations:
- Afterglow mismatch: c64_video_render disables shader afterglow and uses CPU accumulation in c64-video.c. For low effort, keep afterglow disabled in the filter (set afterglow_duration_ms = 0). If afterglow is required, add a ping-pong render target and render-to-texture pass, which is extra work.
- Pixel-perfect scanlines depend on output sizing: the C64 source adjusts output size via c64_get_width/height (src/c64-source.c). Filters do not change source dimensions, so scanlines may not align perfectly unless users scale the source to integer multiples. Mitigate via docs and a simple "suggested scale" note.
- Shader parameter drift: keep parameter names consistent with data/effect_presets.ini and c64_effect_apply to avoid preset drift.

Ongoing maintenance:
- Keep effect parameter keys in sync across c64_effect_apply, filter settings, and crt_effect.effect.
- Keep effect preset values valid for non-C64 inputs (larger resolutions may need different defaults).

Testing implications:
- No existing test harness for filters. Validation would be manual in OBS using a capture source and a few presets. No E2E coverage in this repo for filter paths.

### Option A2: External shader filter using existing crt_effect.effect (stopgap)
What the user gets:
- Apply the same crt_effect.effect shader to any OBS source using an external shader filter plugin.
- Manual use of preset values from data/effect_presets.ini.

What the user does not get:
- Integrated presets UI, afterglow (needs ping-pong texture support), or any C64-specific features.

Technical approach summary:
- Use data/effects/crt_effect.effect with a third-party OBS shader filter plugin.
- Copy parameter values from data/effect_presets.ini into the shader filter UI.

Key risks and mitigations:
- External dependency and compatibility risk across OBS versions and platforms.
- Afterglow is unlikely to work without texture history support; document it as unsupported.

Ongoing maintenance:
- Keep docs in sync with shader parameter names and preset values.

Testing implications:
- Manual validation only. No automated tests inside this repo.

## Rejected options (high effort)

### Option B: Shared core used by both source and filter
- Effect state is embedded in the c64_source struct (scan_line_distance, render_texture, crt_effect) in src/c64-types.h.
- The render path depends on c64_video_tick and source-owned render_texture creation (src/c64-source.c).
- CPU afterglow runs inside the video ingest path (c64_get_afterglow_output_pixels in src/c64-video.c).
These indicate a refactor is needed to extract a shared effect core, which is beyond low-effort scope.

### Option C: Input switch to use an existing OBS source inside C64 Stream
- Ingest is built around UDP packets and fixed packet sizes (C64_VIDEO_PACKET_SIZE in src/c64-protocol.h).
- Video thread uses recvfrom and expects C64 packet layout (c64_video_thread_func in src/c64-video.c).
- Pixel conversion assumes 4-bit C64 pixels (c64_convert_pixels_optimized in src/c64-video.c).
Supporting an OBS source as input would require a new ingest path and frame queue, which is a major change.

## Recommendation and next decision points
Recommendation: Option A (add a CRT filter inside this plugin, shader + presets, no afterglow). It is the lowest-effort in-repo path that preserves the existing shader and presets, and avoids refactoring the current C64 source pipeline.

Decision points:
- Decide if afterglow is required for the filter. If yes, budget for a render-target ping-pong path.
- Decide whether to allow filter-driven output resizing or to rely on user scaling in OBS for scanline accuracy.
- Decide whether to share effect UI code or duplicate a minimal set of effect properties.

## Code touch points per option

Option A:
- src/plugin-main.c: add a new obs_source_info for the filter (OBS_SOURCE_TYPE_FILTER).
- New filter module (example: src/c64-filter.c/h): filter create/update/render using gs_effect_create_from_file and gs_effect_set_* similar to c64_video_render.
- src/c64-effect.c/h: reuse c64_effect_init and c64_effect_apply for presets.
- src/c64-properties.c: reuse or duplicate the effects group (scan_line_distance, blur_strength, bloom_strength, tint_mode, etc).
- data/effects/crt_effect.effect: shader used by both source and filter.
- data/effect_presets.ini: preset values shared by source and filter.
- CMakeLists.txt: add the new filter source file to target_sources.

Option A2:
- No code changes. Uses data/effects/crt_effect.effect and data/effect_presets.ini as reference inputs for a third-party shader filter plugin.

Option B (rejected):
- Would require splitting c64_source render state (src/c64-types.h) and moving logic out of c64_source.c and c64_video.c.

Option C (rejected):
- Would require new ingest APIs beyond recvfrom, plus a new frame path that bypasses c64_process_video_packet_direct.

## Risk and maintainability scoring rationale
- Option A: Low risk because it reuses existing shader and preset assets (data/effects/crt_effect.effect, data/effect_presets.ini). Moderate risk because filter rendering is a new path and afterglow is not reused.
- Option A2: Low implementation risk because no code is added, but moderate maintenance risk due to third-party plugin compatibility.
- Option B: High risk due to refactor scope across c64-source.c, c64-video.c, and c64-types.h.
- Option C: High risk because the ingest pipeline is specialized for UDP packets and C64 pixel conversion.

## Time estimates (WBS + ranges + evidence)
Estimates are grounded in existing assets: crt_effect.effect and effect_presets.ini already exist, and c64_video_render shows the required shader parameter wiring.

Option A (in-repo CRT filter, no afterglow):
- WBS
  - Filter design and API choice (OBS filter callbacks, settings keys): 4-6h
  - Filter implementation + OBS registration (plugin-main.c, new filter module): 8-12h
  - Effect properties and preset integration (reuse c64_effect_apply, mirror c64-properties.c): 6-8h
  - Shader parameter wiring and sizing (match c64_video_render): 6-8h
  - Manual OBS validation across 2-3 sources and presets: 6-10h
  - Docs update for filter usage and limitations: 2-4h
- Range
  - Best: 32h (about 4 days)
  - Expected: 44h (about 5-6 days)
  - Worst: 64h (about 8 days)
- Risk drivers
  - Render context and sampler handling (obs graphics API, see c64_video_render in src/c64-source.c).
  - Output sizing differences vs c64_get_width/height and scanline accuracy.

Option A2 (external shader filter stopgap):
- WBS
  - Validate shader usage with a third-party shader filter: 4-6h
  - Map preset values from data/effect_presets.ini into usage notes: 4-6h
  - Short how-to doc for users: 2-3h
- Range
  - Best: 10h (about 1-2 days)
  - Expected: 14h (about 2 days)
  - Worst: 22h (about 3 days)
- Risk drivers
  - Third-party plugin compatibility across OBS versions and platforms.
