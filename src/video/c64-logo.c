/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#include <inttypes.h>
#include <string.h>
#include <obs-module.h>
#include <util/platform.h>

// STB Image library for PNG loading. STB_IMAGE_IMPLEMENTATION is defined in
// c64-script-vm-dispatch-io.c so the implementation lives in exactly one TU
// that both the plugin and the test binaries link against.
#include "stb_image.h"

#include "c64-logo.h"
#include "c64-source.h"
#include "c64-types.h"
#include "c64-logging.h"
#include "c64-protocol.h"
#include "c64-color.h"

// Load PNG pixel data using stb_image
static bool load_logo_pixels(struct c64_source *context)
{
    C64_LOG_DEBUG("Loading PNG pixel data with stb_image...");

    // Use obs_module_file() to get the path to the scaled logo in the data directory
    char *logo_path = obs_module_file("images/c64stream-logo.png");
    if (!logo_path) {
        C64_LOG_WARNING("Failed to locate PNG file in module data directory");
        return false;
    }

    C64_LOG_DEBUG("PNG path resolved to: %s", logo_path);

    // Load PNG data with stb_image (force RGBA format)
    int width, height, channels;
    unsigned char *img_data = stbi_load(logo_path, &width, &height, &channels, 4); // Force 4 channels (RGBA)

    bfree(logo_path);

    if (!img_data) {
        C64_LOG_WARNING("Failed to load PNG with stb_image: %s", stbi_failure_reason());
        return false;
    }

    // Cache the pixel data
    context->logo_width = (uint32_t)width;
    context->logo_height = (uint32_t)height;

    size_t pixel_data_size = width * height * sizeof(uint32_t);
    context->logo_pixels = bmalloc(pixel_data_size);
    if (!context->logo_pixels) {
        C64_LOG_ERROR("Failed to allocate memory for PNG pixel cache (%zu bytes)", pixel_data_size);
        stbi_image_free(img_data);
        return false;
    }

    // Copy RGBA pixel data
    uint32_t *dest = context->logo_pixels;
    uint8_t *src_bytes = img_data;

    for (int i = 0; i < width * height; i++) {
        uint8_t r = src_bytes[i * 4 + 0];
        uint8_t g = src_bytes[i * 4 + 1];
        uint8_t b = src_bytes[i * 4 + 2];
        uint8_t a = src_bytes[i * 4 + 3];
        dest[i] = (a << 24) | (b << 16) | (g << 8) | r;
    }

    stbi_image_free(img_data);

    C64_LOG_DEBUG("Loaded PNG pixel data: %ux%u (%zu bytes)", width, height, pixel_data_size);
    return true;
}

// Load the C64S logo texture from module data directory
static gs_texture_t *load_logo_texture(void)
{
    C64_LOG_DEBUG("Attempting to load logo texture...");

    // Use obs_module_file() to get the path to the scaled logo in the data directory
    char *logo_path = obs_module_file("images/c64stream-logo.png");
    if (!logo_path) {
        C64_LOG_WARNING("Failed to locate logo file in module data directory");
        return NULL;
    }

    C64_LOG_DEBUG("Logo path resolved to: %s", logo_path);

    // Load texture from file
    gs_texture_t *logo_texture = gs_texture_create_from_file(logo_path);

    if (!logo_texture) {
        C64_LOG_WARNING("Failed to load logo texture from: %s", logo_path);
    } else {
        uint32_t width = gs_texture_get_width(logo_texture);
        uint32_t height = gs_texture_get_height(logo_texture);
        C64_LOG_DEBUG("Loaded logo texture from: %s (size: %ux%u)", logo_path, width, height);
    }

    bfree(logo_path);
    return logo_texture;
}

// Pre-render logo into a specific frame buffer with given dimensions and format
static bool prerender_logo_frame_format(struct c64_source *context, uint32_t *buffer, uint32_t width, uint32_t height,
                                        bool is_pal)
{
    if (!buffer) {
        return false;
    }

    const uint32_t c64_border_color = (0xFF << 24) | (84 << 16) | (60 << 8) | 10;
    const uint32_t c64_screen_color = (0xFF << 24) | (25 << 16) | (18 << 8) | 3;

    // Create authentic C64 display layout
    for (uint32_t i = 0; i < width * height; i++) {
        buffer[i] = c64_border_color;
    }

    // Set border dimensions based on format
    uint32_t border_left, border_right, border_top, border_bottom;
    if (is_pal) {
        // PAL (384×272): T35, B37, L32, R32 → 320×200 screen
        border_left = 32;
        border_right = 32;
        border_top = 35;
        border_bottom = 37;
    } else {
        // NTSC (384×240): T20, B20, L32, R32 → 320×200 screen
        border_left = 32;
        border_right = 32;
        border_top = 20;
        border_bottom = 20;
    }

    uint32_t screen_x = border_left;
    uint32_t screen_y = border_top;
    uint32_t screen_width = width - border_left - border_right;
    uint32_t screen_height = height - border_top - border_bottom;

    // Fill screen area
    for (uint32_t y = screen_y; y < screen_y + screen_height && y < height; y++) {
        for (uint32_t x = screen_x; x < screen_x + screen_width && x < width; x++) {
            buffer[y * width + x] = c64_screen_color;
        }
    }

    // If PNG pixel data is available, render it directly
    if (context->logo_pixels && context->logo_width > 0 && context->logo_height > 0) {
        uint32_t logo_w = context->logo_width;  // Should be 224px
        uint32_t logo_h = context->logo_height; // Should be 149px

        // Calculate logo position (centered in black screen area)
        uint32_t logo_x = screen_x + (screen_width - logo_w) / 2;
        uint32_t logo_y = screen_y + (screen_height - logo_h) / 2;

        // Render PNG pixel data with alpha blending
        uint32_t *logo_pixels = context->logo_pixels;

        for (uint32_t y = 0; y < logo_h && (logo_y + y) < height; y++) {
            for (uint32_t x = 0; x < logo_w && (logo_x + x) < width; x++) {
                uint32_t logo_pixel = logo_pixels[y * logo_w + x];
                uint32_t alpha = (logo_pixel >> 24) & 0xFF;

                // Only draw non-transparent pixels
                if (alpha > 0) {
                    uint32_t frame_idx = (logo_y + y) * width + (logo_x + x);

                    if (alpha == 255) {
                        // Fully opaque - direct copy
                        buffer[frame_idx] = logo_pixel;
                    } else {
                        // Alpha blending
                        uint32_t bg = buffer[frame_idx]; // Current background color
                        uint32_t bg_r = (bg >> 16) & 0xFF;
                        uint32_t bg_g = (bg >> 8) & 0xFF;
                        uint32_t bg_b = bg & 0xFF;

                        uint32_t logo_r = (logo_pixel >> 16) & 0xFF;
                        uint32_t logo_g = (logo_pixel >> 8) & 0xFF;
                        uint32_t logo_b = logo_pixel & 0xFF;

                        // Alpha blend: result = logo * alpha + bg * (1 - alpha)
                        uint32_t r = (logo_r * alpha + bg_r * (255 - alpha)) / 255;
                        uint32_t g = (logo_g * alpha + bg_g * (255 - alpha)) / 255;
                        uint32_t b = (logo_b * alpha + bg_b * (255 - alpha)) / 255;

                        buffer[frame_idx] = (0xFF << 24) | (r << 16) | (g << 8) | b;
                    }
                }
            }
        }

        const char *standard_name = is_pal ? "PAL" : "NTSC";
        C64_LOG_INFO("🔷 Pre-rendered C64 display with PNG logo: %ux%u (70%% of screen) at (%u,%u) in %ux%u %s frame",
                     logo_w, logo_h, logo_x, logo_y, width, height, standard_name);
    } else {
        const char *standard_name = is_pal ? "PAL" : "NTSC";
        C64_LOG_INFO("🔷 Pre-rendered authentic C64 display: %ux%u %s frame with borders", width, height,
                     standard_name);
    }
    return true;
}

// Pre-render both PAL and NTSC logo frames
static bool prerender_logo_frames(struct c64_source *context)
{
    bool pal_success = false, ntsc_success = false;

    // Pre-render PAL version (384x272)
    if (context->logo_frame_buffer_pal) {
        pal_success =
            prerender_logo_frame_format(context, context->logo_frame_buffer_pal, C64_PAL_WIDTH, C64_PAL_HEIGHT, true);
    }

    // Pre-render NTSC version (384x240)
    if (context->logo_frame_buffer_ntsc) {
        ntsc_success = prerender_logo_frame_format(context, context->logo_frame_buffer_ntsc, C64_NTSC_WIDTH,
                                                   C64_NTSC_HEIGHT, false);
    }

    return pal_success && ntsc_success;
}

// Initialize logo system - load texture and pre-render frame buffer
bool c64_logo_init(struct c64_source *context)
{
    if (!context) {
        return false;
    }

    C64_LOG_DEBUG("Initializing logo system...");

    // Allocate separate PAL and NTSC logo frame buffers
    size_t pal_frame_size = C64_PAL_WIDTH * C64_PAL_HEIGHT * sizeof(uint32_t);
    size_t ntsc_frame_size = C64_NTSC_WIDTH * C64_NTSC_HEIGHT * sizeof(uint32_t);

    context->logo_frame_buffer_pal = bmalloc(pal_frame_size);
    if (!context->logo_frame_buffer_pal) {
        C64_LOG_ERROR("Failed to allocate PAL logo frame buffer (%zu bytes)", pal_frame_size);
        return false;
    }

    context->logo_frame_buffer_ntsc = bmalloc(ntsc_frame_size);
    if (!context->logo_frame_buffer_ntsc) {
        C64_LOG_ERROR("Failed to allocate NTSC logo frame buffer (%zu bytes)", ntsc_frame_size);
        bfree(context->logo_frame_buffer_pal);
        context->logo_frame_buffer_pal = NULL;
        return false;
    }

    // Initialize format preference to PAL (default on boot)
    context->last_connected_format_was_pal = true;

    // Load PNG pixel data using stb_image (primary method)
    if (!load_logo_pixels(context)) {
        C64_LOG_WARNING("Failed to load PNG pixel data, will use fallback");
    }

    // Load logo texture (secondary, for compatibility)
    context->logo_texture = load_logo_texture();
    context->logo_texture_loaded = true;

    // Pre-render both PAL and NTSC logo frames for instant display
    if (!prerender_logo_frames(context)) {
        C64_LOG_WARNING("Failed to pre-render logo frames during initialization");
    }

    C64_LOG_INFO(
        "✅ Logo system initialized successfully (PAL: %zu bytes, NTSC: %zu bytes, PNG pixels: %s, texture: %s)",
        pal_frame_size, ntsc_frame_size, context->logo_pixels ? "loaded" : "fallback",
        context->logo_texture ? "loaded" : "fallback");
    return true;
}

// Cleanup logo resources
void c64_logo_cleanup(struct c64_source *context)
{
    if (!context) {
        return;
    }

    C64_LOG_DEBUG("Cleaning up logo system...");

    if (context->logo_texture) {
        gs_texture_destroy(context->logo_texture);
        context->logo_texture = NULL;
    }

    if (context->logo_pixels) {
        bfree(context->logo_pixels);
        context->logo_pixels = NULL;
        context->logo_width = 0;
        context->logo_height = 0;
    }

    if (context->logo_frame_buffer_pal) {
        bfree(context->logo_frame_buffer_pal);
        context->logo_frame_buffer_pal = NULL;
    }

    if (context->logo_frame_buffer_ntsc) {
        bfree(context->logo_frame_buffer_ntsc);
        context->logo_frame_buffer_ntsc = NULL;
    }

    C64_LOG_DEBUG("Logo system cleanup completed");
}

// Render logo to async video output (uses pre-rendered buffer)
void c64_logo_render_to_frame(struct c64_source *context, uint64_t timestamp_ns)
{
    if (!context || !context->frame_buffer) {
        return;
    }

    // Select appropriate logo buffer based on last connected format
    uint32_t *logo_buffer = NULL;
    if (context->last_connected_format_was_pal && context->logo_frame_buffer_pal) {
        logo_buffer = context->logo_frame_buffer_pal;
    } else if (!context->last_connected_format_was_pal && context->logo_frame_buffer_ntsc) {
        logo_buffer = context->logo_frame_buffer_ntsc;
    } else if (context->logo_frame_buffer_pal) {
        // Default to PAL if no previous connection or preferred format unavailable
        logo_buffer = context->logo_frame_buffer_pal;
    } else {
        return; // No logo buffers available
    }

    // Copy only rows present in the selected fixed-size logo buffer.
    const uint32_t buffer_height = (logo_buffer == context->logo_frame_buffer_ntsc) ? C64_NTSC_HEIGHT : C64_PAL_HEIGHT;
    const uint32_t copy_height = context->height < buffer_height ? context->height : buffer_height;
    memcpy(context->frame_buffer, logo_buffer, context->width * copy_height * sizeof(uint32_t));
    if (context->height > copy_height) {
        memset(context->frame_buffer + context->width * copy_height, 0,
               context->width * (context->height - copy_height) * sizeof(uint32_t));
    }
    context->frame_dirty = true;

    // Output logo frame via async video
    struct obs_source_frame obs_frame = {0};
    obs_frame.data[0] = (uint8_t *)context->frame_buffer;
    obs_frame.linesize[0] = context->width * 4; // 4 bytes per pixel (RGBA)
    obs_frame.width = context->width;
    obs_frame.height = context->height;
    obs_frame.format = VIDEO_FORMAT_RGBA;
    obs_frame.timestamp = timestamp_ns;
    obs_frame.flip = false;

    obs_source_output_video(context->source, &obs_frame);

    // Very rare spot checks for logo rendering (every 10 minutes)
    static int logo_debug_count = 0;
    static uint64_t last_logo_log_time = 0;
    uint64_t now = os_gettime_ns();
    if ((++logo_debug_count % 10000) == 0 ||
        (now - last_logo_log_time >= 600000000000ULL)) { // Every 10k renders OR 10 minutes
        C64_LOG_DEBUG("🔷 LOGO SPOT CHECK: %ux%u RGBA, timestamp=%" PRIu64 " (total count: %d)", obs_frame.width,
                      obs_frame.height, obs_frame.timestamp, logo_debug_count);
        last_logo_log_time = now;
    }
}

// Check if logo system is available and ready
bool c64_logo_is_available(struct c64_source *context)
{
    return context && (context->logo_frame_buffer_pal != NULL || context->logo_frame_buffer_ntsc != NULL);
}

// Set format preference (PAL or NTSC) for logo display
void c64_logo_set_format_preference(struct c64_source *context, bool prefer_pal)
{
    if (context) {
        context->last_connected_format_was_pal = prefer_pal;
        C64_LOG_DEBUG("Logo format preference updated: %s", prefer_pal ? "PAL" : "NTSC");
    }
}
