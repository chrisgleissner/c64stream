/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

// Ensure asserts are always enabled in tests
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "c64-stream-effects.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool c64_debug_logging = false;

struct pipeline_options {
    const char *output_path;
    uint32_t width;
    uint32_t height;
    uint32_t frames;
    float dt_ms;
    int afterglow_duration_ms;
    int afterglow_curve;
    float scan_line_distance;
    float scan_line_strength;
};

static void pipeline_options_init(struct pipeline_options *opts)
{
    opts->output_path = "output.raw";
    opts->width = 64;
    opts->height = 48;
    opts->frames = 60;
    opts->dt_ms = 16.6667f;
    opts->afterglow_duration_ms = 80;
    opts->afterglow_curve = 2;
    opts->scan_line_distance = 0.5f;
    opts->scan_line_strength = 0.6f;
}

static bool parse_arg(const char *arg, const char *name, const char **value_out)
{
    size_t len = strlen(name);
    if (strncmp(arg, name, len) == 0 && arg[len] == '=') {
        *value_out = arg + len + 1;
        return true;
    }
    return false;
}

static void pipeline_options_parse(struct pipeline_options *opts, int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *value = NULL;
        if (parse_arg(argv[i], "--output", &value)) {
            opts->output_path = value;
        } else if (parse_arg(argv[i], "--width", &value)) {
            opts->width = (uint32_t)strtoul(value, NULL, 10);
        } else if (parse_arg(argv[i], "--height", &value)) {
            opts->height = (uint32_t)strtoul(value, NULL, 10);
        } else if (parse_arg(argv[i], "--frames", &value)) {
            opts->frames = (uint32_t)strtoul(value, NULL, 10);
        } else if (parse_arg(argv[i], "--dt-ms", &value)) {
            opts->dt_ms = (float)atof(value);
        } else if (parse_arg(argv[i], "--afterglow-duration-ms", &value)) {
            opts->afterglow_duration_ms = atoi(value);
        } else if (parse_arg(argv[i], "--afterglow-curve", &value)) {
            opts->afterglow_curve = atoi(value);
        } else if (parse_arg(argv[i], "--scan-line-distance", &value)) {
            opts->scan_line_distance = (float)atof(value);
        } else if (parse_arg(argv[i], "--scan-line-strength", &value)) {
            opts->scan_line_strength = (float)atof(value);
        }
    }
}

static uint32_t make_rgba(uint8_t r, uint8_t g, uint8_t b)
{
    return 0xFF000000 | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

static bool write_metadata(const struct pipeline_options *opts)
{
    char meta_path[512];
    snprintf(meta_path, sizeof(meta_path), "%s.json", opts->output_path);
    FILE *meta = fopen(meta_path, "w");
    if (!meta) {
        return false;
    }

    fprintf(meta,
            "{\n"
            "  \"width\": %u,\n"
            "  \"height\": %u,\n"
            "  \"frames\": %u,\n"
            "  \"dt_ms\": %.4f,\n"
            "  \"afterglow_duration_ms\": %d,\n"
            "  \"afterglow_curve\": %d,\n"
            "  \"scan_line_distance\": %.3f,\n"
            "  \"scan_line_strength\": %.3f\n"
            "}\n",
            opts->width, opts->height, opts->frames, opts->dt_ms, opts->afterglow_duration_ms, opts->afterglow_curve,
            opts->scan_line_distance, opts->scan_line_strength);
    fclose(meta);
    return true;
}

int main(int argc, char **argv)
{
    struct pipeline_options opts;
    pipeline_options_init(&opts);
    pipeline_options_parse(&opts, argc, argv);

    if (opts.width == 0 || opts.height == 0 || opts.frames == 0) {
        fprintf(stderr, "Invalid dimensions or frame count\n");
        return 1;
    }

    FILE *out = fopen(opts.output_path, "wb");
    if (!out) {
        fprintf(stderr, "Failed to open output file: %s\n", opts.output_path);
        return 1;
    }

    const size_t pixel_count = (size_t)opts.width * (size_t)opts.height;
    uint32_t *input = malloc(pixel_count * sizeof(uint32_t));
    uint32_t *output = malloc(pixel_count * sizeof(uint32_t));
    if (!input || !output) {
        fprintf(stderr, "Failed to allocate buffers\n");
        fclose(out);
        free(input);
        free(output);
        return 1;
    }

    struct c64_stream_effects state;
    memset(&state, 0, sizeof(state));
    c64_afterglow_init(&state.afterglow);
    state.afterglow.duration_ms = opts.afterglow_duration_ms;
    state.afterglow.curve = opts.afterglow_curve;
    state.afterglow_enable = opts.afterglow_duration_ms > 0;
    state.scan_line_distance = opts.scan_line_distance;
    state.scan_line_strength = opts.scan_line_strength;
    state.cpu_scanlines_enabled = true;

    const uint32_t white = make_rgba(255, 255, 255);
    const uint32_t black = make_rgba(0, 0, 0);

    for (uint32_t frame = 0; frame < opts.frames; frame++) {
        for (size_t i = 0; i < pixel_count; i++) {
            input[i] = black;
        }

        uint32_t x = frame % opts.width;
        uint32_t y = opts.height / 2;
        input[(size_t)y * (size_t)opts.width + x] = white;

        uint64_t timestamp_ns = (uint64_t)((double)frame * (double)opts.dt_ms * 1000000.0);
        c64_stream_effects_process_frame(&state, input, opts.width, opts.height, timestamp_ns, output);

        if (fwrite(output, sizeof(uint32_t), pixel_count, out) != pixel_count) {
            fprintf(stderr, "Failed to write frame %u\n", frame);
            fclose(out);
            free(input);
            free(output);
            c64_afterglow_free(&state.afterglow);
            return 1;
        }
    }

    fclose(out);
    free(input);
    free(output);
    c64_afterglow_free(&state.afterglow);

    if (!write_metadata(&opts)) {
        fprintf(stderr, "Failed to write metadata\n");
        return 1;
    }

    printf("Generated %u frames (%ux%u) to %s\n", opts.frames, opts.width, opts.height, opts.output_path);
    return 0;
}
