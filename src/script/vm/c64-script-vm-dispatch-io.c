/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-script-vm-dispatch-io.h"

#include "c64-script-builtins.h"
#include "c64-script-vm-internal.h"
#include "c64-file.h"

#include <curl/curl.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// stb_image / stb_image_write are vendored at src/video/. We use them for the
// ASSERT IMAGE_EQUALS feature so the plugin has no external libpng dependency
// (an external libpng caused macOS load failures via @rpath/png.framework — see
// issue #116). This file owns the implementations; c64-logo.c only consumes
// declarations.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#else
#include <sys/wait.h>
#endif

typedef struct {
    char *data;
    size_t size;
} c64script_http_response_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint8_t *pixels;
} c64script_image_rgba_t;

static bool c64script_rgba_buffer_size_fits(size_t width, size_t height)
{
    if (width == 0 || height == 0 || width > SIZE_MAX / 4u) {
        return false;
    }
    return height <= SIZE_MAX / (width * 4u);
}

static void c64script_free_image(c64script_image_rgba_t *image)
{
    if (!image) {
        return;
    }

    // stbi_load uses its own allocator; release via stbi_image_free for any
    // image we loaded ourselves. For our own malloc'd diff buffers we still
    // need plain free, so callers handle those separately.
    if (image->pixels) {
        stbi_image_free(image->pixels);
        image->pixels = NULL;
    }
    image->width = 0;
    image->height = 0;
}

static bool c64script_ensure_parent_directory(const char *path, char *error_msg, size_t error_size)
{
    if (!path || path[0] == '\0') {
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Invalid path");
        }
        return false;
    }

    char directory[1024];
    snprintf(directory, sizeof(directory), "%s", path);

    char *last_slash = strrchr(directory, '/');
#ifdef _WIN32
    char *last_backslash = strrchr(directory, '\\');
    if (!last_slash || (last_backslash && last_backslash > last_slash)) {
        last_slash = last_backslash;
    }
#endif
    if (!last_slash) {
        return true;
    }

    *last_slash = '\0';
    if (directory[0] == '\0') {
        return true;
    }

    if (!c64_create_directory_recursive(directory)) {
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Failed to create output directory");
        }
        return false;
    }

    return true;
}

static bool c64script_load_png_rgba(const char *path, c64script_image_rgba_t *image, char *error_msg, size_t error_size)
{
    if (!path || !image) {
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Invalid PNG load arguments");
        }
        return false;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    // Force 4-channel RGBA so the byte layout matches the writer and the
    // comparison loop below — stb_image handles palette, grayscale, 16-bit, and
    // tRNS expansion internally.
    uint8_t *pixels = stbi_load(path, &width, &height, &channels, 4);
    if (!pixels) {
        const char *reason = stbi_failure_reason();
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Failed to load PNG '%s': %s", path, reason ? reason : "unknown error");
        }
        return false;
    }

    if (width <= 0 || height <= 0 || (uint64_t)width > UINT32_MAX || (uint64_t)height > UINT32_MAX ||
        !c64script_rgba_buffer_size_fits((size_t)width, (size_t)height)) {
        stbi_image_free(pixels);
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "PNG dimensions out of range for '%s': %d x %d", path, width, height);
        }
        return false;
    }

    image->width = (uint32_t)width;
    image->height = (uint32_t)height;
    image->pixels = pixels;
    return true;
}

static bool c64script_write_png_rgba(const char *path, uint32_t width, uint32_t height, const uint8_t *pixels,
                                     char *error_msg, size_t error_size)
{
    if (!path || !pixels || width == 0 || height == 0) {
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Invalid PNG write arguments");
        }
        return false;
    }

    if (!c64script_ensure_parent_directory(path, error_msg, error_size)) {
        return false;
    }

    if (width > (uint32_t)INT_MAX || height > (uint32_t)INT_MAX || width > (uint32_t)(INT_MAX / 4) ||
        !c64script_rgba_buffer_size_fits((size_t)width, (size_t)height)) {
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "PNG dimensions out of range for '%s': %u x %u", path, width, height);
        }
        return false;
    }

    const int width_int = (int)width;
    const int height_int = (int)height;
    const int stride = width_int * 4;
    if (!stbi_write_png(path, width_int, height_int, 4, pixels, stride)) {
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Failed to write PNG: %s", path);
        }
        return false;
    }
    return true;
}

static void c64script_build_diff_path(const char *actual_path, char *diff_path, size_t diff_path_size)
{
    if (!actual_path || !diff_path || diff_path_size == 0) {
        return;
    }

    diff_path[0] = '\0';

    const char *ext = strrchr(actual_path, '.');
    int written = 0;
    if (ext && strcmp(ext, ".png") == 0) {
        written = snprintf(diff_path, diff_path_size, "%.*s.diff.png", (int)(ext - actual_path), actual_path);
    } else {
        written = snprintf(diff_path, diff_path_size, "%s.diff.png", actual_path);
    }

    if (written < 0 || (size_t)written >= diff_path_size) {
        diff_path[0] = '\0';
    }
}

static bool c64script_compare_images(const c64script_image_rgba_t *actual, const c64script_image_rgba_t *expected,
                                     int tolerance, size_t *out_mismatch_count, uint8_t **out_diff_pixels,
                                     uint32_t *out_diff_width, uint32_t *out_diff_height)
{
    if (!actual || !expected || !out_mismatch_count || !out_diff_pixels || !out_diff_width || !out_diff_height) {
        return false;
    }

    const uint32_t diff_width = actual->width > expected->width ? actual->width : expected->width;
    const uint32_t diff_height = actual->height > expected->height ? actual->height : expected->height;
    uint8_t *diff_pixels = calloc((size_t)diff_width * (size_t)diff_height * 4u, 1);
    if (!diff_pixels) {
        return false;
    }

    size_t mismatch_count = 0;
    for (uint32_t y = 0; y < diff_height; y++) {
        for (uint32_t x = 0; x < diff_width; x++) {
            const bool actual_in_bounds = x < actual->width && y < actual->height;
            const bool expected_in_bounds = x < expected->width && y < expected->height;
            uint8_t *diff = diff_pixels + (((size_t)y * (size_t)diff_width + (size_t)x) * 4u);

            if (!actual_in_bounds || !expected_in_bounds) {
                diff[0] = 255;
                diff[1] = 0;
                diff[2] = 255;
                diff[3] = 255;
                mismatch_count++;
                continue;
            }

            const uint8_t *actual_px = actual->pixels + (((size_t)y * (size_t)actual->width + (size_t)x) * 4u);
            const uint8_t *expected_px = expected->pixels + (((size_t)y * (size_t)expected->width + (size_t)x) * 4u);

            int max_diff = 0;
            for (size_t channel = 0; channel < 4; channel++) {
                int channel_diff = abs((int)actual_px[channel] - (int)expected_px[channel]);
                if (channel_diff > max_diff) {
                    max_diff = channel_diff;
                }
            }

            if (max_diff > tolerance) {
                diff[0] = (uint8_t)abs((int)actual_px[0] - (int)expected_px[0]);
                diff[1] = (uint8_t)abs((int)actual_px[1] - (int)expected_px[1]);
                diff[2] = (uint8_t)abs((int)actual_px[2] - (int)expected_px[2]);
                diff[3] = 255;
                mismatch_count++;
            }
        }
    }

    *out_mismatch_count = mismatch_count;
    *out_diff_pixels = diff_pixels;
    *out_diff_width = diff_width;
    *out_diff_height = diff_height;
    return true;
}

static size_t http_write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    size_t total = size * nmemb;
    c64script_http_response_t *response = (c64script_http_response_t *)userdata;
    if (!response || total == 0) {
        return 0;
    }

    char *new_data = realloc(response->data, response->size + total + 1);
    if (!new_data) {
        return 0;
    }

    response->data = new_data;
    memcpy(response->data + response->size, ptr, total);
    response->size += total;
    response->data[response->size] = '\0';
    return total;
}

bool c64script_dispatch_io(c64script_runtime_t *runtime, const c64script_instruction_t *instr)
{
#ifdef C64SCRIPT_FUZZING
    if (runtime) {
        snprintf(runtime->error_msg, sizeof(runtime->error_msg), "IO disabled during fuzzing");
    }
    (void)instr;
    return false;
#else
    switch (instr->opcode) {
    case OP_ASSERT_IMAGE_EQUALS: {
        c64script_value_t tolerance_val;
        c64script_value_t expected_path_val;
        c64script_value_t actual_path_val;
        if (!c64script_runtime_pop(runtime, &tolerance_val))
            return false;
        if (!c64script_runtime_pop(runtime, &expected_path_val)) {
            c64script_value_free(&tolerance_val);
            return false;
        }
        if (!c64script_runtime_pop(runtime, &actual_path_val)) {
            c64script_value_free(&tolerance_val);
            c64script_value_free(&expected_path_val);
            return false;
        }

        if (actual_path_val.type != VALUE_STRING || expected_path_val.type != VALUE_STRING ||
            tolerance_val.type != VALUE_NUMBER) {
            c64script_value_free(&actual_path_val);
            c64script_value_free(&expected_path_val);
            c64script_value_free(&tolerance_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (ASSERT IMAGE_EQUALS)");
            return false;
        }

        int tolerance = 0;
        if (!number_to_int(runtime, &tolerance_val, &tolerance, "ASSERT IMAGE_EQUALS TOLERANCE")) {
            c64script_value_free(&actual_path_val);
            c64script_value_free(&expected_path_val);
            c64script_value_free(&tolerance_val);
            return false;
        }
        if (tolerance < 0) {
            c64script_value_free(&actual_path_val);
            c64script_value_free(&expected_path_val);
            c64script_value_free(&tolerance_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "ILLEGAL QUANTITY (ASSERT IMAGE_EQUALS)");
            return false;
        }

        char actual_path[1024];
        char expected_path[1024];
        if (!c64script_resolve_script_path(runtime, actual_path_val.as.string, actual_path, sizeof(actual_path)) ||
            !c64script_resolve_script_path(runtime, expected_path_val.as.string, expected_path,
                                           sizeof(expected_path))) {
            c64script_value_free(&actual_path_val);
            c64script_value_free(&expected_path_val);
            c64script_value_free(&tolerance_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "ASSERT IMAGE_EQUALS path too long");
            return false;
        }

        c64script_image_rgba_t actual = {0};
        c64script_image_rgba_t expected = {0};
        char io_error[256] = {0};
        bool ok = c64script_load_png_rgba(actual_path, &actual, io_error, sizeof(io_error)) &&
                  c64script_load_png_rgba(expected_path, &expected, io_error, sizeof(io_error));
        c64script_value_free(&actual_path_val);
        c64script_value_free(&expected_path_val);
        c64script_value_free(&tolerance_val);
        if (!ok) {
            c64script_free_image(&actual);
            c64script_free_image(&expected);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "%s", io_error[0] ? io_error : "PNG load failed");
            return false;
        }

        size_t mismatch_count = 0;
        uint8_t *diff_pixels = NULL;
        uint32_t diff_width = 0;
        uint32_t diff_height = 0;
        if (!c64script_compare_images(&actual, &expected, tolerance, &mismatch_count, &diff_pixels, &diff_width,
                                      &diff_height)) {
            c64script_free_image(&actual);
            c64script_free_image(&expected);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Out of memory comparing images");
            return false;
        }

        if (mismatch_count > 0) {
            char diff_path[1024] = {0};
            c64script_build_diff_path(actual_path, diff_path, sizeof(diff_path));
            if (!c64script_write_png_rgba(diff_path, diff_width, diff_height, diff_pixels, io_error,
                                          sizeof(io_error))) {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg),
                         "ASSERT IMAGE_EQUALS failed with %zu mismatched pixel(s) but diff write failed: %s",
                         mismatch_count, io_error[0] ? io_error : "unknown error");
            } else {
                snprintf(runtime->error_msg, sizeof(runtime->error_msg),
                         "ASSERT IMAGE_EQUALS failed: %zu mismatched pixel(s), tolerance=%d, diff=%s", mismatch_count,
                         tolerance, diff_path);
            }
            free(diff_pixels);
            c64script_free_image(&actual);
            c64script_free_image(&expected);
            return false;
        }

        free(diff_pixels);
        c64script_free_image(&actual);
        c64script_free_image(&expected);
        break;
    }

    case OP_RUNLOCAL: {
        c64script_value_t output_var_val, status_var_val, args_val, path_val;
        if (!c64script_runtime_pop(runtime, &output_var_val))
            return false;
        if (!c64script_runtime_pop(runtime, &status_var_val))
            return false;
        if (!c64script_runtime_pop(runtime, &args_val))
            return false;
        if (!c64script_runtime_pop(runtime, &path_val))
            return false;

        if (path_val.type != VALUE_STRING || args_val.type != VALUE_STRING || status_var_val.type != VALUE_STRING ||
            output_var_val.type != VALUE_STRING) {
            c64script_value_free(&path_val);
            c64script_value_free(&args_val);
            c64script_value_free(&status_var_val);
            c64script_value_free(&output_var_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (RUNLOCAL)");
            return false;
        }

        const char *exec_path = path_val.as.string ? path_val.as.string : "";
        char resolved_path[1024];
        if (exec_path[0] != '\0' &&
            (exec_path[0] == '.' || strchr(exec_path, '/') != NULL || strchr(exec_path, '\\') != NULL)) {
            if (!c64script_resolve_script_path(runtime, exec_path, resolved_path, sizeof(resolved_path))) {
                c64script_value_free(&path_val);
                c64script_value_free(&args_val);
                c64script_value_free(&status_var_val);
                c64script_value_free(&output_var_val);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Path too long");
                return false;
            }
            exec_path = resolved_path;
        }

        char cmd[2048];
        if (args_val.as.string[0] == '\0') {
            snprintf(cmd, sizeof(cmd), "%s 2>&1", exec_path);
        } else {
            snprintf(cmd, sizeof(cmd), "%s %s 2>&1", exec_path, args_val.as.string);
        }

        FILE *pipe = popen(cmd, "r");
        if (!pipe) {
            c64script_value_free(&path_val);
            c64script_value_free(&args_val);
            c64script_value_free(&status_var_val);
            c64script_value_free(&output_var_val);
            const char *prefix = "Failed to execute: ";
            size_t max_len = sizeof(runtime->error_msg) - strlen(prefix) - 1;
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "%s%.*s", prefix, (int)max_len, exec_path);
            return false;
        }

        const size_t max_output = 1024 * 1024;
        char *output = malloc(max_output);
        if (!output) {
            pclose(pipe);
            c64script_value_free(&path_val);
            c64script_value_free(&args_val);
            c64script_value_free(&status_var_val);
            c64script_value_free(&output_var_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Out of memory");
            return false;
        }

        size_t total_read = 0;
        while (total_read < max_output - 1) {
            size_t nread = fread(output + total_read, 1, max_output - 1 - total_read, pipe);
            if (nread == 0)
                break;
            total_read += nread;
        }
        output[total_read] = '\0';

        int exit_code = pclose(pipe);
#ifndef _WIN32
        if (WIFEXITED(exit_code)) {
            exit_code = WEXITSTATUS(exit_code);
        } else {
            exit_code = -1;
        }
#endif

        if (status_var_val.as.string[0] != '\0') {
            c64script_value_t status = {.type = VALUE_NUMBER, .as.number = (double)exit_code};
            c64script_runtime_set_var(runtime, status_var_val.as.string, status);
        }

        if (output_var_val.as.string[0] != '\0') {
            c64script_value_t output_value = {.type = VALUE_STRING, .as.string = output};
            c64script_runtime_set_var(runtime, output_var_val.as.string, output_value);
        }

        free(output);
        c64script_value_free(&path_val);
        c64script_value_free(&args_val);
        c64script_value_free(&status_var_val);
        c64script_value_free(&output_var_val);
        break;
    }

    case OP_READFILE: {
        c64script_value_t path_val;
        c64script_value_t var_name_val;
        if (!c64script_runtime_pop(runtime, &var_name_val))
            return false;
        if (!c64script_runtime_pop(runtime, &path_val))
            return false;

        if (var_name_val.type != VALUE_STRING) {
            c64script_value_free(&var_name_val);
            c64script_value_free(&path_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (READFILE variable name)");
            return false;
        }

        if (path_val.type != VALUE_STRING) {
            c64script_value_free(&var_name_val);
            c64script_value_free(&path_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (READFILE path)");
            return false;
        }

        char resolved_path[1024];
        if (!c64script_resolve_script_path(runtime, path_val.as.string, resolved_path, sizeof(resolved_path))) {
            c64script_value_free(&var_name_val);
            c64script_value_free(&path_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Path too long");
            return false;
        }

        char *content = NULL;
        char err[256] = {0};
        if (!load_text_file(resolved_path, &content, err, sizeof(err))) {
            c64script_value_free(&var_name_val);
            c64script_value_free(&path_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "%s", err[0] ? err : "Failed to read file");
            return false;
        }

        c64script_value_t content_val = {.type = VALUE_STRING, .as.string = content};
        c64script_runtime_set_var(runtime, var_name_val.as.string, content_val);

        c64script_value_free(&content_val);
        c64script_value_free(&var_name_val);
        c64script_value_free(&path_val);
        break;
    }

    case OP_WRITEFILE_APPEND:
    case OP_WRITEFILE_TRUNCATE: {
        bool truncate = (instr->opcode == OP_WRITEFILE_TRUNCATE);
        c64script_value_t content_val;
        c64script_value_t path_val;
        if (!c64script_runtime_pop(runtime, &content_val))
            return false;
        if (!c64script_runtime_pop(runtime, &path_val))
            return false;

        if (path_val.type != VALUE_STRING) {
            c64script_value_free(&path_val);
            c64script_value_free(&content_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (WRITEFILE path)");
            return false;
        }

        const char *content_str = NULL;
        char num_buf[64];
        if (content_val.type == VALUE_STRING) {
            content_str = content_val.as.string ? content_val.as.string : "";
        } else if (content_val.type == VALUE_NUMBER) {
            if (!c64script_builtin_str(content_val.as.number, num_buf, sizeof(num_buf))) {
                c64script_value_free(&path_val);
                c64script_value_free(&content_val);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "WRITEFILE conversion failed");
                return false;
            }
            content_str = num_buf;
        } else {
            c64script_value_free(&path_val);
            c64script_value_free(&content_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (WRITEFILE content)");
            return false;
        }

        char resolved_path[1024];
        if (!c64script_resolve_script_path(runtime, path_val.as.string, resolved_path, sizeof(resolved_path))) {
            c64script_value_free(&path_val);
            c64script_value_free(&content_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Path too long");
            return false;
        }

        char err[256] = {0};
        if (!write_file(resolved_path, content_str, truncate, err, sizeof(err))) {
            c64script_value_free(&path_val);
            c64script_value_free(&content_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "%s", err[0] ? err : "Failed to write file");
            return false;
        }

        c64script_value_free(&path_val);
        c64script_value_free(&content_val);
        break;
    }

    case OP_HTTP: {
        c64script_value_t response_var_val, status_var_val, body_val, headers_val, url_val;
        if (!c64script_runtime_pop(runtime, &url_val))
            return false;
        if (!c64script_runtime_pop(runtime, &headers_val))
            return false;
        if (!c64script_runtime_pop(runtime, &body_val))
            return false;
        if (!c64script_runtime_pop(runtime, &status_var_val))
            return false;
        if (!c64script_runtime_pop(runtime, &response_var_val))
            return false;

        if (url_val.type != VALUE_STRING) {
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (HTTP url)");
            c64script_value_free(&url_val);
            c64script_value_free(&headers_val);
            c64script_value_free(&body_val);
            c64script_value_free(&status_var_val);
            c64script_value_free(&response_var_val);
            return false;
        }

        const bool has_status_var =
            (status_var_val.type == VALUE_STRING && status_var_val.as.string && status_var_val.as.string[0]);
        const bool has_response_var =
            (response_var_val.type == VALUE_STRING && response_var_val.as.string && response_var_val.as.string[0]);
        const char *body_str = NULL;
        char body_buf[64];
        if (body_val.type == VALUE_STRING) {
            body_str = body_val.as.string ? body_val.as.string : "";
        } else if (body_val.type == VALUE_NUMBER) {
            if (!c64script_builtin_str(body_val.as.number, body_buf, sizeof(body_buf))) {
                c64script_value_free(&url_val);
                c64script_value_free(&headers_val);
                c64script_value_free(&body_val);
                c64script_value_free(&status_var_val);
                c64script_value_free(&response_var_val);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "HTTP body conversion failed");
                return false;
            }
            body_str = body_buf;
        } else if (body_val.type != VALUE_STRING) {
            c64script_value_free(&url_val);
            c64script_value_free(&headers_val);
            c64script_value_free(&body_val);
            c64script_value_free(&status_var_val);
            c64script_value_free(&response_var_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (HTTP body)");
            return false;
        }

        struct curl_slist *headers = NULL;
        if (headers_val.type == VALUE_STRING) {
            if (headers_val.as.string && headers_val.as.string[0] != '\0') {
                headers = curl_slist_append(headers, headers_val.as.string);
            }
        } else if (headers_val.type == VALUE_MAP) {
            c64script_map_t *map = headers_val.as.map;
            if (map) {
                for (size_t i = 0; i < map->count; i++) {
                    const char *key = map->entries[i].key;
                    const c64script_value_t *value = &map->entries[i].value;
                    if (!key) {
                        continue;
                    }

                    const char *value_str = NULL;
                    char value_buf[64];
                    if (value->type == VALUE_STRING) {
                        value_str = value->as.string ? value->as.string : "";
                    } else if (value->type == VALUE_NUMBER) {
                        if (!c64script_builtin_str(value->as.number, value_buf, sizeof(value_buf))) {
                            c64script_value_free(&url_val);
                            c64script_value_free(&headers_val);
                            c64script_value_free(&body_val);
                            c64script_value_free(&status_var_val);
                            c64script_value_free(&response_var_val);
                            curl_slist_free_all(headers);
                            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "HTTP header conversion failed");
                            return false;
                        }
                        value_str = value_buf;
                    } else {
                        c64script_value_free(&url_val);
                        c64script_value_free(&headers_val);
                        c64script_value_free(&body_val);
                        c64script_value_free(&status_var_val);
                        c64script_value_free(&response_var_val);
                        curl_slist_free_all(headers);
                        snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (HTTP headers)");
                        return false;
                    }

                    size_t header_len = strlen(key) + strlen(value_str) + 3;
                    char *header_line = malloc(header_len);
                    if (!header_line) {
                        c64script_value_free(&url_val);
                        c64script_value_free(&headers_val);
                        c64script_value_free(&body_val);
                        c64script_value_free(&status_var_val);
                        c64script_value_free(&response_var_val);
                        curl_slist_free_all(headers);
                        snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Out of memory");
                        return false;
                    }
                    snprintf(header_line, header_len, "%s: %s", key, value_str);
                    headers = curl_slist_append(headers, header_line);
                    free(header_line);
                }
            }
        } else if (headers_val.type != VALUE_STRING) {
            c64script_value_free(&url_val);
            c64script_value_free(&headers_val);
            c64script_value_free(&body_val);
            c64script_value_free(&status_var_val);
            c64script_value_free(&response_var_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (HTTP headers)");
            return false;
        }

        c64script_http_response_t response = {.data = NULL, .size = 0};
        CURL *curl = curl_easy_init();
        if (!curl) {
            curl_slist_free_all(headers);
            c64script_value_free(&url_val);
            c64script_value_free(&headers_val);
            c64script_value_free(&body_val);
            c64script_value_free(&status_var_val);
            c64script_value_free(&response_var_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "HTTP initialization failed");
            return false;
        }

        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_URL, url_val.as.string);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        switch (instr->operand) {
        case 0: // GET
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
            break;
        case 1: // POST
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str ? body_str : "");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)(body_str ? strlen(body_str) : 0));
            break;
        case 2: // PUT
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str ? body_str : "");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)(body_str ? strlen(body_str) : 0));
            break;
        case 3: // DELETE
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
            if (body_str) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str);
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body_str));
            }
            break;
        case 4: // PATCH
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str ? body_str : "");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)(body_str ? strlen(body_str) : 0));
            break;
        default:
            curl_easy_cleanup(curl);
            curl_slist_free_all(headers);
            c64script_value_free(&url_val);
            c64script_value_free(&headers_val);
            c64script_value_free(&body_val);
            c64script_value_free(&status_var_val);
            c64script_value_free(&response_var_val);
            snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Invalid HTTP method");
            return false;
        }

        if (headers) {
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        }

        CURLcode res = curl_easy_perform(curl);
        long status_code = 0;
        const char *error_text = NULL;

        if (res == CURLE_OK) {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
        } else {
            error_text = curl_easy_strerror(res);
            if (!has_status_var) {
                curl_easy_cleanup(curl);
                curl_slist_free_all(headers);
                c64script_value_free(&url_val);
                c64script_value_free(&headers_val);
                c64script_value_free(&body_val);
                c64script_value_free(&status_var_val);
                c64script_value_free(&response_var_val);
                snprintf(runtime->error_msg, sizeof(runtime->error_msg), "HTTP request failed: %s", error_text);
                free(response.data);
                return false;
            }
        }

        if (has_status_var) {
            c64script_value_t status_value = {.type = VALUE_NUMBER, .as.number = (double)status_code};
            c64script_runtime_set_var(runtime, status_var_val.as.string, status_value);
        }

        if (has_response_var) {
            const char *resp_text = response.data ? response.data : (error_text ? error_text : "");
            c64script_value_t response_value = {.type = VALUE_STRING, .as.string = strdup(resp_text)};
            c64script_runtime_set_var(runtime, response_var_val.as.string, response_value);
            c64script_value_free(&response_value);
        }

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        free(response.data);
        c64script_value_free(&url_val);
        c64script_value_free(&headers_val);
        c64script_value_free(&body_val);
        c64script_value_free(&status_var_val);
        c64script_value_free(&response_var_val);
        break;
    }

    default:
        return false;
    }

    return true;
#endif
}
