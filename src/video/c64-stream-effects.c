/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#include "c64-stream-effects.h"
#include "c64-effect.h"
#include "c64-effect-geometry.h"
#include "c64-logging.h"
#include <string.h>
#include <util/platform.h>

static const char *C64_PRESET_LAST_APPLIED_KEY = "crt_preset_last_applied";
static const char *const C64_STREAM_EFFECTS_SAVED_SETTING_KEYS[] = {
    "crt_preset",     "scan_line_distance",    "scan_line_strength", "pixel_width", "pixel_height",  "blur_strength",
    "bloom_strength", "afterglow_duration_ms", "afterglow_curve",    "tint_mode",   "tint_strength",
};

static bool c64_stream_effects_settings_have_effect_overrides(obs_data_t *settings)
{
    if (!settings) {
        return false;
    }

    return obs_data_has_user_value(settings, "scan_line_distance") ||
           obs_data_has_user_value(settings, "scan_line_strength") ||
           obs_data_has_user_value(settings, "pixel_width") || obs_data_has_user_value(settings, "pixel_height") ||
           obs_data_has_user_value(settings, "blur_strength") || obs_data_has_user_value(settings, "bloom_strength") ||
           obs_data_has_user_value(settings, "tint_mode") || obs_data_has_user_value(settings, "tint_strength") ||
           obs_data_has_user_value(settings, "afterglow_duration_ms") ||
           obs_data_has_user_value(settings, "afterglow_curve");
}

static bool c64_stream_effects_apply_crt_preset_if_needed(obs_data_t *settings, const char *preset_name, bool is_update)
{
    if (!settings || !preset_name || preset_name[0] == '\0') {
        return false;
    }

    const char *last_applied = obs_data_get_string(settings, C64_PRESET_LAST_APPLIED_KEY);
    const bool preset_changed = !last_applied || last_applied[0] == '\0' || strcmp(last_applied, preset_name) != 0;
    if (!preset_changed) {
        return false;
    }

    if (!last_applied || last_applied[0] == '\0') {
        if (c64_effect_matches_preset(settings, preset_name)) {
            obs_data_set_string(settings, C64_PRESET_LAST_APPLIED_KEY, preset_name);
            return false;
        }
    }

    if (!is_update && (!last_applied || last_applied[0] == '\0') &&
        c64_stream_effects_settings_have_effect_overrides(settings)) {
        C64_LOG_INFO("" EFFECT_LOG_PREFIX " Skipping preset auto-apply; custom effect overrides detected");
        return false;
    }

    if (!c64_effect_apply(settings, preset_name)) {
        if (is_update) {
            C64_LOG_WARNING("" EFFECT_LOG_PREFIX " CRT preset in update not found: %s", preset_name);
        } else {
            C64_LOG_WARNING("" EFFECT_LOG_PREFIX " CRT preset not found: %s", preset_name);
        }
        return false;
    }

    obs_data_set_string(settings, C64_PRESET_LAST_APPLIED_KEY, preset_name);
    if (is_update) {
        C64_LOG_INFO("Applied CRT preset on update: %s", preset_name);
    } else {
        C64_LOG_INFO("Applied CRT preset from settings: %s", preset_name);
    }
    return true;
}

static void c64_stream_effects_apply_scanlines_cpu(struct c64_stream_effects *state, uint32_t *pixels, uint32_t width,
                                                   uint32_t height)
{
    if (!state || !state->cpu_scanlines_enabled || !pixels || width == 0 || height == 0 ||
        state->scan_line_distance <= 0.0f || state->scan_line_strength <= 0.0f) {
        return;
    }

    uint32_t total_pixels = 0;
    uint32_t scanline_pixels = 0;
    c64_effect_get_scanline_scaling_info(state->scan_line_distance, &total_pixels, &scanline_pixels);
    if (total_pixels == 0) {
        return;
    }

    const float darken = 1.0f - state->scan_line_strength;
    for (uint32_t y = 0; y < height; y++) {
        const uint32_t pos = y % total_pixels;
        if (pos < scanline_pixels) {
            continue;
        }

        uint32_t *row = pixels + (size_t)y * (size_t)width;
        for (uint32_t x = 0; x < width; x++) {
            const uint32_t pixel = row[x];
            const uint8_t r = (uint8_t)(pixel & 0xFF);
            const uint8_t g = (uint8_t)((pixel >> 8) & 0xFF);
            const uint8_t b = (uint8_t)((pixel >> 16) & 0xFF);

            const uint8_t dr = (uint8_t)((float)r * darken);
            const uint8_t dg = (uint8_t)((float)g * darken);
            const uint8_t db = (uint8_t)((float)b * darken);

            row[x] = 0xFF000000 | ((uint32_t)db << 16) | ((uint32_t)dg << 8) | (uint32_t)dr;
        }
    }
}

static void c64_stream_effects_update_frame_timing(struct c64_stream_effects *state)
{
    if (!state) {
        return;
    }

    struct obs_video_info ovi;
    if (obs_get_video_info(&ovi) && ovi.fps_den > 0) {
        state->expected_fps = (float)ovi.fps_num / (float)ovi.fps_den;
        if (state->expected_fps > 1.0f) {
            state->frame_interval_ns = (uint64_t)(1000000000.0 / (double)state->expected_fps);
            return;
        }
    }

    state->expected_fps = 0.0f;
    state->frame_interval_ns = 0;
}

void c64_stream_effects_process_frame(struct c64_stream_effects *state, const uint32_t *input_rgba, uint32_t width,
                                      uint32_t height, uint64_t timestamp_ns, uint32_t *output_rgba)
{
    if (!state || !input_rgba || !output_rgba || width == 0 || height == 0) {
        return;
    }

    float dt_ms = state->afterglow_dt_ms;
    if (dt_ms <= 0.001f) {
        dt_ms = c64_afterglow_nominal_dt_ms(state->frame_interval_ns, state->expected_fps);
        if (timestamp_ns > 0 && state->last_frame_timestamp_ns > 0 && timestamp_ns > state->last_frame_timestamp_ns) {
            dt_ms = (float)(timestamp_ns - state->last_frame_timestamp_ns) / 1000000.0f;
        }
    }
    state->last_frame_timestamp_ns = timestamp_ns;

    const size_t pixel_count = (size_t)width * (size_t)height;
    const uint32_t *afterglow_pixels = input_rgba;
    if (state->afterglow_enable && state->afterglow.duration_ms > 0) {
        const uint32_t *applied = c64_afterglow_apply(&state->afterglow, input_rgba, pixel_count, dt_ms);
        if (applied) {
            afterglow_pixels = applied;
        }
    }

    if (output_rgba != afterglow_pixels) {
        memcpy(output_rgba, afterglow_pixels, pixel_count * sizeof(uint32_t));
    }

    c64_stream_effects_apply_scanlines_cpu(state, output_rgba, width, height);
}

static bool c64_stream_effects_preset_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);

    if (!settings) {
        return false;
    }

    const char *preset_name = obs_data_get_string(settings, "crt_preset");
    if (!preset_name || preset_name[0] == '\0') {
        return false;
    }

    const char *last_applied = obs_data_get_string(settings, C64_PRESET_LAST_APPLIED_KEY);
    if (last_applied && last_applied[0] != '\0' && strcmp(last_applied, preset_name) == 0) {
        return false;
    }

    if (c64_effect_apply(settings, preset_name)) {
        C64_LOG_INFO("" EFFECT_LOG_PREFIX " Applied CRT preset: %s", preset_name);
        obs_data_set_string(settings, C64_PRESET_LAST_APPLIED_KEY, preset_name);
        return true;
    }

    return false;
}

const char *c64_stream_effects_get_name(void *unused)
{
    UNUSED_PARAMETER(unused);
    return obs_module_text("C64StreamEffects");
}

static void c64_stream_effects_get_geometry(struct c64_stream_effects *state, uint32_t logical_width,
                                            uint32_t logical_height, struct c64_effect_geometry *geometry)
{
    c64_effect_geometry_init(geometry, logical_width, logical_height, state ? state->pixel_width : 1.0f,
                             state ? state->pixel_height : 1.0f, state ? state->scan_line_distance : 0.0f,
                             state ? state->preserve_size : true);
}

void c64_stream_effects_defaults(obs_data_t *settings)
{
    obs_data_set_default_double(settings, "scan_line_distance", 0.0);
    obs_data_set_default_double(settings, "scan_line_strength", 0.0);
    obs_data_set_default_double(settings, "pixel_width", 1.0);
    obs_data_set_default_double(settings, "pixel_height", 1.0);
    obs_data_set_default_double(settings, "blur_strength", 0.0);
    obs_data_set_default_double(settings, "bloom_strength", 0.0);
    obs_data_set_default_int(settings, "afterglow_duration_ms", 0);
    obs_data_set_default_int(settings, "afterglow_curve", 2);
    obs_data_set_default_int(settings, "tint_mode", 0);
    obs_data_set_default_double(settings, "tint_strength", 0.0);
    obs_data_set_default_bool(settings, "preserve_size", true);
}

obs_properties_t *c64_stream_effects_properties(void *data)
{
    UNUSED_PARAMETER(data);

    obs_properties_t *props = obs_properties_create();
    obs_property_t *effects_group = obs_properties_add_group(props, "effects_group", obs_module_text("Effects"),
                                                             OBS_GROUP_NORMAL, obs_properties_create());
    obs_properties_t *effects_props = obs_property_group_content(effects_group);

    obs_property_t *preset_prop = obs_properties_add_list(effects_props, "crt_preset", obs_module_text("Presets"),
                                                          OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_set_long_description(preset_prop, obs_module_text("Presets.Description"));
    c64_effect_populate_list(preset_prop);
    obs_property_set_modified_callback(preset_prop, c64_stream_effects_preset_changed);

    obs_property_t *preserve_size_prop =
        obs_properties_add_bool(effects_props, "preserve_size", obs_module_text("PreservePreviewSize"));
    obs_property_set_long_description(preserve_size_prop, obs_module_text("PreservePreviewSize.Description"));

    obs_property_t *scanline_distance_prop = obs_properties_add_list(effects_props, "scan_line_distance",
                                                                     obs_module_text("ScanLineDistance"),
                                                                     OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_FLOAT);
    obs_property_list_add_float(scanline_distance_prop, obs_module_text("ScanLineDistance.None"), 0.0);
    obs_property_list_add_float(scanline_distance_prop, obs_module_text("ScanLineDistance.Tight"), 0.25);
    obs_property_list_add_float(scanline_distance_prop, obs_module_text("ScanLineDistance.Normal"), 0.50);
    obs_property_list_add_float(scanline_distance_prop, obs_module_text("ScanLineDistance.Wide"), 1.0);
    obs_property_list_add_float(scanline_distance_prop, obs_module_text("ScanLineDistance.ExtraWide"), 2.0);
    obs_property_set_long_description(scanline_distance_prop, obs_module_text("ScanLineDistance.Description"));

    obs_property_t *scanline_strength_prop = obs_properties_add_float_slider(
        effects_props, "scan_line_strength", obs_module_text("ScanLineStrength"), 0.0, 1.0, 0.05);
    obs_property_set_long_description(scanline_strength_prop, obs_module_text("ScanLineStrength.Description"));

    obs_property_t *pixel_width_prop =
        obs_properties_add_float_slider(effects_props, "pixel_width", obs_module_text("PixelWidth"), 0.5, 4.0, 0.1);
    obs_property_set_long_description(pixel_width_prop, obs_module_text("PixelWidth.Description"));

    obs_property_t *pixel_height_prop =
        obs_properties_add_float_slider(effects_props, "pixel_height", obs_module_text("PixelHeight"), 0.5, 4.0, 0.1);
    obs_property_set_long_description(pixel_height_prop, obs_module_text("PixelHeight.Description"));

    obs_property_t *blur_strength_prop = obs_properties_add_float_slider(
        effects_props, "blur_strength", obs_module_text("BlurStrength"), 0.0, 1.0, 0.05);
    obs_property_set_long_description(blur_strength_prop, obs_module_text("BlurStrength.Description"));

    obs_property_t *bloom_strength_prop = obs_properties_add_float_slider(
        effects_props, "bloom_strength", obs_module_text("BloomStrength"), 0.0, 1.0, 0.05);
    obs_property_set_long_description(bloom_strength_prop, obs_module_text("BloomStrength.Description"));

    obs_property_t *afterglow_duration_prop = obs_properties_add_int_slider(
        effects_props, "afterglow_duration_ms", obs_module_text("AfterglowDuration"), 0, 250, 10);
    obs_property_set_long_description(afterglow_duration_prop, obs_module_text("AfterglowDuration.Description"));

    obs_property_t *afterglow_curve_prop = obs_properties_add_list(
        effects_props, "afterglow_curve", obs_module_text("AfterglowCurve"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(afterglow_curve_prop, obs_module_text("AfterglowCurve.InstantFade"), 0);
    obs_property_list_add_int(afterglow_curve_prop, obs_module_text("AfterglowCurve.RapidFade"), 1);
    obs_property_list_add_int(afterglow_curve_prop, obs_module_text("AfterglowCurve.GradualFade"), 2);
    obs_property_list_add_int(afterglow_curve_prop, obs_module_text("AfterglowCurve.LongTail"), 3);
    obs_property_set_long_description(afterglow_curve_prop, obs_module_text("AfterglowCurve.Description"));

    obs_property_t *tint_mode_prop = obs_properties_add_list(effects_props, "tint_mode", obs_module_text("TintMode"),
                                                             OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(tint_mode_prop, obs_module_text("TintMode.None"), 0);
    obs_property_list_add_int(tint_mode_prop, obs_module_text("TintMode.Amber"), 1);
    obs_property_list_add_int(tint_mode_prop, obs_module_text("TintMode.Green"), 2);
    obs_property_list_add_int(tint_mode_prop, obs_module_text("TintMode.Monochrome"), 3);
    obs_property_set_long_description(tint_mode_prop, obs_module_text("TintMode.Description"));

    obs_property_t *tint_strength_prop = obs_properties_add_float_slider(
        effects_props, "tint_strength", obs_module_text("TintStrength"), 0.0, 1.0, 0.05);
    obs_property_set_long_description(tint_strength_prop, obs_module_text("TintStrength.Description"));

    return props;
}

void *c64_stream_effects_create(obs_data_t *settings, obs_source_t *source)
{
    if (!source) {
        return NULL;
    }

    struct c64_stream_effects *state = bzalloc(sizeof(*state));
    if (!state) {
        return NULL;
    }

    state->source = source;
    c64_afterglow_init(&state->afterglow);
    state->cpu_scanlines_enabled = false;
    c64_stream_effects_update_frame_timing(state);
    state->preserve_size = c64_effect_settings_resolve_preserve_size(
        settings, C64_STREAM_EFFECTS_SAVED_SETTING_KEYS,
        sizeof(C64_STREAM_EFFECTS_SAVED_SETTING_KEYS) / sizeof(C64_STREAM_EFFECTS_SAVED_SETTING_KEYS[0]));

    const char *initial_preset = obs_data_get_string(settings, "crt_preset");
    c64_stream_effects_apply_crt_preset_if_needed(settings, initial_preset, false);

    c64_stream_effects_update(state, settings);
    return state;
}

void c64_stream_effects_destroy(void *data)
{
    struct c64_stream_effects *state = data;
    if (!state) {
        return;
    }

    obs_enter_graphics();
    if (state->input_texrender) {
        gs_texrender_destroy(state->input_texrender);
        state->input_texrender = NULL;
    }
    if (state->stage_surface) {
        gs_stagesurface_destroy(state->stage_surface);
        state->stage_surface = NULL;
    }
    if (state->output_texture) {
        gs_texture_destroy(state->output_texture);
        state->output_texture = NULL;
        state->output_texture_width = 0;
        state->output_texture_height = 0;
    }
    if (state->crt_effect) {
        gs_effect_destroy(state->crt_effect);
        state->crt_effect = NULL;
    }
    if (state->point_sampler) {
        gs_samplerstate_destroy(state->point_sampler);
        state->point_sampler = NULL;
    }
    obs_leave_graphics();

    if (state->cpu_input) {
        bfree(state->cpu_input);
        state->cpu_input = NULL;
    }
    if (state->cpu_output) {
        bfree(state->cpu_output);
        state->cpu_output = NULL;
    }
    state->cpu_bytes = 0;

    c64_afterglow_free(&state->afterglow);
    bfree(state);
}

void c64_stream_effects_update(void *data, obs_data_t *settings)
{
    struct c64_stream_effects *state = data;
    if (!state || !settings) {
        return;
    }

    state->preserve_size = c64_effect_settings_resolve_preserve_size(
        settings, C64_STREAM_EFFECTS_SAVED_SETTING_KEYS,
        sizeof(C64_STREAM_EFFECTS_SAVED_SETTING_KEYS) / sizeof(C64_STREAM_EFFECTS_SAVED_SETTING_KEYS[0]));

    const char *preset_name = obs_data_get_string(settings, "crt_preset");
    c64_stream_effects_apply_crt_preset_if_needed(settings, preset_name, true);

    state->scan_line_distance = (float)obs_data_get_double(settings, "scan_line_distance");
    state->scan_line_strength = (float)obs_data_get_double(settings, "scan_line_strength");
    state->pixel_width = (float)obs_data_get_double(settings, "pixel_width");
    state->pixel_height = (float)obs_data_get_double(settings, "pixel_height");
    state->blur_strength = (float)obs_data_get_double(settings, "blur_strength");
    state->bloom_strength = (float)obs_data_get_double(settings, "bloom_strength");
    state->bloom_enable = state->bloom_strength > 0.0f;
    state->afterglow.duration_ms = (int)obs_data_get_int(settings, "afterglow_duration_ms");
    state->afterglow.curve = (int)obs_data_get_int(settings, "afterglow_curve");
    state->afterglow_enable = state->afterglow.duration_ms > 0;
    state->tint_mode = (int)obs_data_get_int(settings, "tint_mode");
    state->tint_strength = (float)obs_data_get_double(settings, "tint_strength");
    state->tint_enable = (state->tint_mode > 0 && state->tint_strength > 0.0f);

    c64_stream_effects_update_frame_timing(state);
}

static bool c64_stream_effects_ensure_cpu_buffers(struct c64_stream_effects *state, size_t frame_bytes)
{
    if (!state) {
        return false;
    }

    if (state->cpu_bytes != frame_bytes) {
        if (state->cpu_input) {
            bfree(state->cpu_input);
            state->cpu_input = NULL;
        }
        if (state->cpu_output) {
            bfree(state->cpu_output);
            state->cpu_output = NULL;
        }

        state->cpu_input = bmalloc(frame_bytes);
        state->cpu_output = bmalloc(frame_bytes);
        state->cpu_bytes = frame_bytes;
    }

    return state->cpu_input && state->cpu_output;
}

static bool c64_stream_effects_ensure_stage_surface(struct c64_stream_effects *state, uint32_t width, uint32_t height)
{
    if (!state) {
        return false;
    }

    if (state->stage_surface && (state->stage_width != width || state->stage_height != height)) {
        gs_stagesurface_destroy(state->stage_surface);
        state->stage_surface = NULL;
    }

    if (!state->stage_surface) {
        state->stage_surface = gs_stagesurface_create(width, height, GS_RGBA);
        state->stage_width = width;
        state->stage_height = height;
    }

    return state->stage_surface != NULL;
}

static bool c64_stream_effects_ensure_output_texture(struct c64_stream_effects *state, uint32_t width, uint32_t height)
{
    if (!state) {
        return false;
    }

    if (state->output_texture && (state->output_texture_width != width || state->output_texture_height != height)) {
        gs_texture_destroy(state->output_texture);
        state->output_texture = NULL;
        state->output_texture_width = 0;
        state->output_texture_height = 0;
    }

    if (!state->output_texture) {
        state->output_texture = gs_texture_create(width, height, GS_RGBA, 1, NULL, GS_DYNAMIC);
        state->output_texture_width = width;
        state->output_texture_height = height;
    }

    return state->output_texture != NULL;
}

static bool c64_stream_effects_capture_input(struct c64_stream_effects *state, obs_source_t *target, uint32_t width,
                                             uint32_t height)
{
    if (!state || !target) {
        return false;
    }

    if (!state->input_texrender) {
        state->input_texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
        if (!state->input_texrender) {
            return false;
        }
    }

    gs_texrender_reset(state->input_texrender);
    if (!gs_texrender_begin(state->input_texrender, width, height)) {
        return false;
    }

    struct vec4 clear_color = {0};
    gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
    gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);

    gs_blend_state_push();
    gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
    obs_source_video_render(target);
    gs_blend_state_pop();

    gs_texrender_end(state->input_texrender);

    gs_texture_t *input_texture = gs_texrender_get_texture(state->input_texrender);
    if (!input_texture) {
        return false;
    }

    if (!c64_stream_effects_ensure_stage_surface(state, width, height)) {
        return false;
    }

    gs_stage_texture(state->stage_surface, input_texture);

    uint8_t *mapped = NULL;
    uint32_t linesize = 0;
    if (!gs_stagesurface_map(state->stage_surface, &mapped, &linesize)) {
        return false;
    }

    const size_t row_bytes = (size_t)width * sizeof(uint32_t);
    for (uint32_t y = 0; y < height; y++) {
        memcpy((uint8_t *)state->cpu_input + (size_t)y * row_bytes, mapped + (size_t)y * linesize, row_bytes);
    }

    gs_stagesurface_unmap(state->stage_surface);
    return true;
}

uint32_t c64_stream_effects_get_width(void *data)
{
    struct c64_stream_effects *state = data;
    if (!state || !state->source) {
        return 0;
    }

    obs_source_t *target = obs_filter_get_target(state->source);
    if (!target) {
        return 0;
    }

    uint32_t width = obs_source_get_base_width(target);
    if (width == 0) {
        return 0;
    }

    const uint32_t height = obs_source_get_base_height(target);
    struct c64_effect_geometry geometry;
    c64_stream_effects_get_geometry(state, width, height, &geometry);
    return geometry.reported_width;
}

uint32_t c64_stream_effects_get_height(void *data)
{
    struct c64_stream_effects *state = data;
    if (!state || !state->source) {
        return 0;
    }

    obs_source_t *target = obs_filter_get_target(state->source);
    if (!target) {
        return 0;
    }

    uint32_t height = obs_source_get_base_height(target);
    if (height == 0) {
        return 0;
    }

    const uint32_t width = obs_source_get_base_width(target);
    struct c64_effect_geometry geometry;
    c64_stream_effects_get_geometry(state, width, height, &geometry);
    return geometry.reported_height;
}

void c64_stream_effects_video_render(void *data, gs_effect_t *effect)
{
    UNUSED_PARAMETER(effect);
    struct c64_stream_effects *state = data;
    if (!state || !state->source) {
        return;
    }

    obs_source_t *target = obs_filter_get_target(state->source);
    if (!target) {
        obs_source_skip_video_filter(state->source);
        return;
    }

    uint32_t width = obs_source_get_base_width(target);
    uint32_t height = obs_source_get_base_height(target);
    if (width == 0 || height == 0) {
        obs_source_skip_video_filter(state->source);
        return;
    }

    struct c64_effect_geometry geometry;
    c64_stream_effects_get_geometry(state, width, height, &geometry);

    const size_t frame_bytes = (size_t)width * (size_t)height * sizeof(uint32_t);
    if (!c64_stream_effects_ensure_cpu_buffers(state, frame_bytes)) {
        obs_source_skip_video_filter(state->source);
        return;
    }

    const uint64_t frame_time_ns = obs_get_video_frame_time();
    const uint64_t wall_time_ns = os_gettime_ns();
    const bool size_changed = (state->input_width != width) || (state->input_height != height);

    uint64_t input_frame_ts_ns = 0;
    bool input_frame_has_ts = false;
    bool input_frame_new = false;
    struct obs_source_frame *input_frame = obs_source_get_frame(target);
    if (input_frame) {
        if (input_frame->timestamp != 0) {
            input_frame_has_ts = true;
            input_frame_ts_ns = input_frame->timestamp;
        }
        if (input_frame_has_ts) {
            input_frame_new = input_frame_ts_ns != state->last_input_frame_timestamp_ns;
        }
        obs_source_release_frame(target, input_frame);
        input_frame = NULL;
    }

    if (size_changed) {
        state->input_width = width;
        state->input_height = height;
        c64_afterglow_reset(&state->afterglow);
        state->last_frame_timestamp_ns = 0;
        state->last_input_frame_timestamp_ns = 0;
        state->synthetic_frame_time_ns = 0;
        state->last_processed_frame_time_ns = 0;
        state->last_processed_wall_time_ns = 0;
        state->afterglow_last_tick_ns = 0;
        state->afterglow_dt_ms = 0.0f;
    }

    uint64_t interval_ns = state->frame_interval_ns;
    if (interval_ns == 0 && state->expected_fps > 1.0f) {
        interval_ns = (uint64_t)(1000000000.0 / (double)state->expected_fps);
    }
    if (interval_ns == 0) {
        interval_ns = 33333333ull;
    }

    uint32_t skipped_frames = 0;
    if (input_frame_new && input_frame_has_ts && state->last_input_frame_timestamp_ns > 0 &&
        input_frame_ts_ns > state->last_input_frame_timestamp_ns && interval_ns > 0) {
        const uint64_t delta_ns = input_frame_ts_ns - state->last_input_frame_timestamp_ns;
        const uint64_t steps = delta_ns / interval_ns;
        if (steps > 1) {
            skipped_frames = (uint32_t)(steps - 1);
            if (skipped_frames > 8) {
                // Clamp catch-up work to avoid long stalls after large timestamp jumps.
                skipped_frames = 8;
            }
        }
    }

    bool should_process = size_changed || !state->output_texture;
    bool should_capture = size_changed || !state->output_texture || input_frame_new;
    if (!should_process) {
        if (input_frame_has_ts) {
            should_process = input_frame_new;
        } else if (frame_time_ns != 0) {
            should_process = frame_time_ns != state->last_processed_frame_time_ns;
            if (should_process) {
                should_capture = true;
            }
        } else if (state->last_processed_wall_time_ns == 0) {
            should_process = true;
            should_capture = true;
        } else if (wall_time_ns > state->last_processed_wall_time_ns) {
            should_process = (wall_time_ns - state->last_processed_wall_time_ns) >= interval_ns;
            if (should_process) {
                should_capture = true;
            }
        }
    }

    if (should_process) {
        state->afterglow_dt_ms = c64_afterglow_nominal_dt_ms(state->frame_interval_ns, state->expected_fps);
        state->afterglow_last_tick_ns = wall_time_ns;

        if (skipped_frames > 0 && state->afterglow_enable && state->afterglow.duration_ms > 0 && state->cpu_input) {
            const size_t pixel_count = (size_t)width * (size_t)height;
            const float dt_ms = state->afterglow_dt_ms;
            for (uint32_t i = 0; i < skipped_frames; i++) {
                (void)c64_afterglow_apply(&state->afterglow, state->cpu_input, pixel_count, dt_ms);
            }
        }

        bool captured = true;
        if (should_capture) {
            captured = c64_stream_effects_capture_input(state, target, width, height);
        }
        if (captured && state->cpu_input) {
            uint64_t timestamp_ns = frame_time_ns;
            if (timestamp_ns == 0) {
                if (state->synthetic_frame_time_ns == 0) {
                    state->synthetic_frame_time_ns = wall_time_ns;
                } else {
                    state->synthetic_frame_time_ns += interval_ns;
                }
                timestamp_ns = state->synthetic_frame_time_ns;
            } else {
                state->synthetic_frame_time_ns = frame_time_ns;
            }

            c64_stream_effects_process_frame(state, state->cpu_input, width, height, timestamp_ns, state->cpu_output);

            if (!c64_stream_effects_ensure_output_texture(state, width, height)) {
                obs_source_skip_video_filter(state->source);
                return;
            }

            gs_texture_set_image(state->output_texture, (const uint8_t *)state->cpu_output, width * 4, false);
            state->last_processed_frame_time_ns = frame_time_ns;
            state->last_processed_wall_time_ns = wall_time_ns;
            if (input_frame_has_ts && input_frame_new) {
                state->last_input_frame_timestamp_ns = input_frame_ts_ns;
            }
        }
    }

    if (!state->output_texture) {
        obs_source_skip_video_filter(state->source);
        return;
    }

    const bool any_effects_enabled = (state->scan_line_distance > 0.0f) || (state->bloom_strength > 0.0f) ||
                                     (state->tint_mode > 0 && state->tint_strength > 0.0f) ||
                                     (state->pixel_width != 1.0f || state->pixel_height != 1.0f) ||
                                     state->blur_strength > 0.0f;

    if (!any_effects_enabled) {
        gs_effect_t *default_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
        if (default_effect) {
            gs_effect_set_texture(gs_effect_get_param_by_name(default_effect, "image"), state->output_texture);
            while (gs_effect_loop(default_effect, "Draw")) {
                gs_draw_sprite(state->output_texture, 0, width, height);
            }
        }
        return;
    }

    if (!state->crt_effect) {
        char *effect_path = obs_module_file("effects/crt_effect.effect");
        if (effect_path) {
            state->crt_effect = gs_effect_create_from_file(effect_path, NULL);
            bfree(effect_path);
            if (!state->crt_effect) {
                C64_LOG_ERROR("" EFFECT_LOG_PREFIX
                              " Failed to load CRT effect shader - falling back to default rendering");
            }
        } else {
            C64_LOG_ERROR("" EFFECT_LOG_PREFIX
                          " Failed to find CRT effect shader file - falling back to default rendering");
        }
    }

    if (!state->crt_effect) {
        gs_effect_t *default_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
        if (default_effect) {
            gs_effect_set_texture(gs_effect_get_param_by_name(default_effect, "image"), state->output_texture);
            while (gs_effect_loop(default_effect, "Draw")) {
                gs_draw_sprite(state->output_texture, 0, width, height);
            }
        }
        return;
    }

    if (!state->point_sampler) {
        struct gs_sampler_info sampler_info = {
            .filter = GS_FILTER_POINT,
            .address_u = GS_ADDRESS_CLAMP,
            .address_v = GS_ADDRESS_CLAMP,
        };
        state->point_sampler = gs_samplerstate_create(&sampler_info);
    }

    gs_eparam_t *image_param = gs_effect_get_param_by_name(state->crt_effect, "image");
    gs_effect_set_texture(image_param, state->output_texture);
    if (state->blur_strength == 0.0f && state->point_sampler) {
        gs_effect_set_next_sampler(image_param, state->point_sampler);
    }

    gs_effect_set_float(gs_effect_get_param_by_name(state->crt_effect, "scan_line_distance"),
                        state->scan_line_distance);
    gs_effect_set_float(gs_effect_get_param_by_name(state->crt_effect, "scan_line_strength"),
                        state->scan_line_strength);
    gs_effect_set_float(gs_effect_get_param_by_name(state->crt_effect, "pixel_width"), state->pixel_width);
    gs_effect_set_float(gs_effect_get_param_by_name(state->crt_effect, "pixel_height"), state->pixel_height);
    gs_effect_set_int(gs_effect_get_param_by_name(state->crt_effect, "afterglow_duration_ms"), 0);
    gs_effect_set_int(gs_effect_get_param_by_name(state->crt_effect, "afterglow_curve"), state->afterglow.curve);
    gs_effect_set_int(gs_effect_get_param_by_name(state->crt_effect, "tint_mode"), state->tint_mode);
    gs_effect_set_float(gs_effect_get_param_by_name(state->crt_effect, "tint_strength"), state->tint_strength);
    gs_effect_set_float(gs_effect_get_param_by_name(state->crt_effect, "dt_ms"), 33.33f);
    gs_effect_set_texture(gs_effect_get_param_by_name(state->crt_effect, "texture_accum_prev"), state->output_texture);

    gs_effect_set_float(gs_effect_get_param_by_name(state->crt_effect, "virtual_output_width"),
                        (float)geometry.draw_width);
    gs_effect_set_float(gs_effect_get_param_by_name(state->crt_effect, "virtual_output_height"),
                        (float)geometry.draw_height);
    gs_effect_set_float(gs_effect_get_param_by_name(state->crt_effect, "blur_strength"), state->blur_strength);
    gs_effect_set_float(gs_effect_get_param_by_name(state->crt_effect, "bloom_strength"), state->bloom_strength);
    gs_effect_set_float(gs_effect_get_param_by_name(state->crt_effect, "source_width"), (float)width);
    gs_effect_set_float(gs_effect_get_param_by_name(state->crt_effect, "source_height"), (float)height);

    while (gs_effect_loop(state->crt_effect, "Draw")) {
        gs_draw_sprite(state->output_texture, 0, geometry.draw_width, geometry.draw_height);
    }
}
