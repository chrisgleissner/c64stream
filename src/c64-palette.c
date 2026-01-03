/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#include "c64-palette.h"
#include "c64-color.h"
#include "c64-file.h"
#include "c64-logging.h"

#include <obs-module.h>
#include <util/dstr.h>
#include <util/platform.h>
#include <util/threading.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <direct.h>
#define PATH_SEP '\\'
#define strcasecmp _stricmp
#else
#include <sys/stat.h>
#include <dirent.h>
#define PATH_SEP '/'
#endif

// Global palette system state
static struct c64_palette_system palette_system;
static bool palette_initialized = false;

// Forward declarations
static void discover_shipped_palettes(void);
static void discover_custom_palettes(void);
static bool add_palette_entry(const char *id, const char *name, const char *path, bool is_shipped);
static void sort_palettes(void);
static char *trim_whitespace(char *str);

bool c64_palette_init(void)
{
    if (palette_initialized) {
        return true;
    }

    memset(&palette_system, 0, sizeof(palette_system));
    palette_system.active_palette_index = -1;

    // Set up user palette directory
    if (!c64_palette_get_user_dir(palette_system.user_palette_dir, sizeof(palette_system.user_palette_dir))) {
        C64_LOG_WARNING("" PALETTE_LOG_PREFIX " Failed to set up user palette directory");
    }

    // Add Default palette first (always present, uses hardcoded colors)
    memcpy(palette_system.palettes[0].colors, c64_default_palette, sizeof(c64_default_palette));
    strncpy(palette_system.palettes[0].id, "Default", sizeof(palette_system.palettes[0].id) - 1);
    strncpy(palette_system.palettes[0].name, "Default", sizeof(palette_system.palettes[0].name) - 1);
    palette_system.palettes[0].path[0] = '\0'; // No file for default
    palette_system.palettes[0].is_shipped = true;
    palette_system.palettes[0].colors_loaded = true;
    palette_system.palette_count = 1;
    C64_LOG_INFO("" PALETTE_LOG_PREFIX
                 " Initialized Default palette from build-time generated array (source: data/palettes/default.vpl)");

    // Discover shipped palettes from data/palettes/
    discover_shipped_palettes();

    // Discover custom palettes from user directory
    discover_custom_palettes();

    // Sort palettes (Default first, then alphabetically)
    sort_palettes();

    // Select Default palette
    c64_palette_select("Default");

    palette_initialized = true;
    C64_LOG_INFO("" PALETTE_LOG_PREFIX " Palette system initialized with %d palettes", palette_system.palette_count);

    return true;
}

void c64_palette_cleanup(void)
{
    if (!palette_initialized) {
        return;
    }

    palette_initialized = false;
    memset(&palette_system, 0, sizeof(palette_system));
    C64_LOG_INFO("" PALETTE_LOG_PREFIX " Palette system cleaned up");
}

struct c64_palette_system *c64_palette_get_system(void)
{
    if (!palette_initialized) {
        return NULL;
    }
    return &palette_system;
}

void c64_palette_validate_filesystem(obs_data_t *settings)
{
    C64_LOG_INFO("" PALETTE_LOG_PREFIX " Validating palette filesystem (checking for deleted files)...");

    // Check all custom (non-shipped) palettes and verify their files exist
    int i = 0;
    int removed_count = 0;
    bool active_palette_missing = false;
    const char *active_palette_id = c64_palette_get_active_id();

    C64_LOG_INFO("" PALETTE_LOG_PREFIX "   Current active palette: %s (total palettes: %d)", active_palette_id,
                 palette_system.palette_count);

    while (i < palette_system.palette_count) {
        struct c64_palette_entry *entry = &palette_system.palettes[i];

        // Skip shipped palettes (they're always valid)
        if (entry->is_shipped) {
            i++;
            continue;
        }

        // Check if the file exists
        bool file_exists = false;
        if (entry->path[0]) {
            FILE *file = fopen(entry->path, "r");
            if (file) {
                file_exists = true;
                fclose(file);
            }
        }

        if (!file_exists) {
            C64_LOG_WARNING("" PALETTE_LOG_PREFIX
                            " Palette file no longer exists (likely deleted by user), removing from dropdown: '%s' "
                            "(path: %s)",
                            entry->id, entry->path);

            // Check if this was the active palette (defensive: ensure active_palette_id is non-NULL)
            if (active_palette_id && strcmp(entry->id, active_palette_id) == 0) {
                active_palette_missing = true;
            }

            // Remove this entry by shifting remaining entries down
            for (int j = i; j < palette_system.palette_count - 1; j++) {
                memcpy(&palette_system.palettes[j], &palette_system.palettes[j + 1], sizeof(struct c64_palette_entry));
            }
            palette_system.palette_count--;
            removed_count++;

            // Update active index if it was after the deleted entry
            if (palette_system.active_palette_index > i) {
                palette_system.active_palette_index--;
            }

            // Don't increment i, check the same position again (which now has the next entry)
        } else {
            i++;
        }
    }

    // Rediscover custom palettes if we removed any entries to resync with filesystem
    if (removed_count > 0) {
        C64_LOG_WARNING("" PALETTE_LOG_PREFIX " Removed %d stale palette reference%s from palette list", removed_count,
                        removed_count == 1 ? "" : "s");
        discover_custom_palettes();
        sort_palettes();
    }

    // If the active palette is missing, fall back to Default
    if (active_palette_missing) {
        C64_LOG_WARNING("" PALETTE_LOG_PREFIX
                        " Active palette '%s' file was deleted by user, falling back to 'Default (Preset)'",
                        active_palette_id);
        c64_palette_select("Default");
    }

    // Also check if settings contains a stale palette ID that doesn't exist in our list
    // This handles the case where the palette was deleted in a previous OBS session
    // and the settings file still references it
    bool settings_palette_stale = false;
    if (settings) {
        const char *settings_palette_id = obs_data_get_string(settings, "palette");
        if (settings_palette_id && settings_palette_id[0]) {
            // Check if this palette exists in our system
            bool found = false;
            for (int k = 0; k < palette_system.palette_count; k++) {
                if (strcmp(palette_system.palettes[k].id, settings_palette_id) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                C64_LOG_WARNING("" PALETTE_LOG_PREFIX
                                " Settings reference stale palette '%s' that no longer exists, resetting to 'Default'",
                                settings_palette_id);
                settings_palette_stale = true;
            }
        }
    }

    // Update settings if we need to fallback to Default
    if (settings && (active_palette_missing || settings_palette_stale)) {
        obs_data_set_string(settings, "palette", "Default");

        // Clear any stale export path
        obs_data_erase(settings, "palette_export_path");

        // Set color values to Default palette colors (not just erase)
        // This prevents OBS from having stale color values that would trigger
        // palette recreation when color picker callbacks fire
        uint32_t *default_colors = c64_palette_get_working_colors();
        if (default_colors) {
            for (int j = 0; j < 16; j++) {
                char key[32];
                snprintf(key, sizeof(key), "palette_color_%d", j);

                // Convert BGRA to OBS color format (ABGR stored as int)
                uint32_t obs_color = c64_bgra_to_obs_color(default_colors[j]);

                // Set actual value to overwrite any stale cached values
                obs_data_set_int(settings, key, (long long)obs_color);
            }
        }
    }
}

void c64_palette_populate_list(obs_property_t *palette_prop)
{
    if (!palette_prop) {
        return;
    }

    obs_property_list_clear(palette_prop);

    for (int i = 0; i < palette_system.palette_count; i++) {
        char display_name[C64_PALETTE_NAME_MAX + 16]; // Extra space for " (Preset)" suffix
        if (c64_palette_get_display_name(palette_system.palettes[i].id, display_name, sizeof(display_name))) {
            obs_property_list_add_string(palette_prop, display_name, palette_system.palettes[i].id);
        }
    }
}

bool c64_palette_select(const char *palette_id)
{
    if (!palette_id || !palette_id[0]) {
        palette_id = "Default";
    }

    // Find the palette
    int index = -1;
    for (int i = 0; i < palette_system.palette_count; i++) {
        if (strcmp(palette_system.palettes[i].id, palette_id) == 0) {
            index = i;
            break;
        }
    }

    if (index < 0) {
        C64_LOG_WARNING("" PALETTE_LOG_PREFIX " Palette not found: %s", palette_id);
        return false;
    }

    // Defensive bounds check
    if (index < 0 || index >= palette_system.palette_count) {
        C64_LOG_WARNING("" PALETTE_LOG_PREFIX " Invalid palette index: %d (count: %d)", index,
                        palette_system.palette_count);
        return false;
    }

    // Load colors if not already loaded
    struct c64_palette_entry *entry = &palette_system.palettes[index];
    if (!entry->colors_loaded && entry->path[0]) {
        C64_LOG_INFO("" PALETTE_LOG_PREFIX " Loading palette '%s' from file: %s", entry->id, entry->path);
        char name_buf[C64_PALETTE_NAME_MAX];
        char desc_buf[256];
        if (c64_palette_parse_vpl(entry->path, entry->colors, name_buf, sizeof(name_buf), desc_buf, sizeof(desc_buf))) {
            entry->colors_loaded = true;
            C64_LOG_INFO("" PALETTE_LOG_PREFIX " Successfully loaded palette '%s' from VPL file", entry->id);
            // Update display name if we got one from the file
            if (name_buf[0]) {
                strncpy(entry->name, name_buf, sizeof(entry->name) - 1);
            }
            // Store description
            if (desc_buf[0]) {
                strncpy(entry->desc, desc_buf, sizeof(entry->desc) - 1);
                entry->desc[sizeof(entry->desc) - 1] = '\0';
            }
        } else {
            C64_LOG_WARNING("" PALETTE_LOG_PREFIX " Failed to load palette: %s", entry->path);
            return false;
        }
    }

    // Check if we're actually changing the palette
    bool is_palette_change = (palette_system.active_palette_index != index);
    const char *previous_palette = NULL;
    if (is_palette_change && palette_system.active_palette_index >= 0 &&
        palette_system.active_palette_index < palette_system.palette_count) {
        previous_palette = palette_system.palettes[palette_system.active_palette_index].name;
    }

    palette_system.active_palette_index = index;

    // Copy colors to working buffer
    memcpy(palette_system.working_colors, entry->colors, sizeof(entry->colors));
    palette_system.working_modified = false;

    // Rebuild LUT with new palette
    c64_palette_rebuild_lut(palette_system.working_colors);

    // Log palette activation with appropriate detail
    if (entry->path[0]) {
        if (is_palette_change && previous_palette) {
            C64_LOG_INFO("" PALETTE_LOG_PREFIX " ⚡ Palette changed: '%s' -> '%s' (source: VPL file %s)",
                         previous_palette, entry->name, entry->path);
        } else {
            C64_LOG_INFO("" PALETTE_LOG_PREFIX " ✓ Activated palette: '%s' (source: VPL file %s)", entry->name,
                         entry->path);
        }
    } else {
        // Default palette has no file path
        if (is_palette_change && previous_palette) {
            C64_LOG_INFO(
                "" PALETTE_LOG_PREFIX
                " ⚡ Palette changed: '%s' -> '%s' (source: build-time generated from data/palettes/default.vpl)",
                previous_palette, entry->name);
        } else {
            C64_LOG_INFO("" PALETTE_LOG_PREFIX
                         " ✓ Activated palette: '%s' (source: build-time generated from data/palettes/default.vpl)",
                         entry->name);
        }
    }

    return true;
}

const char *c64_palette_get_active_id(void)
{
    // Defensive bounds check
    if (palette_system.active_palette_index >= 0 &&
        palette_system.active_palette_index < palette_system.palette_count) {
        const char *id = palette_system.palettes[palette_system.active_palette_index].id;
        return id;
    }

    return "Default";
}

uint32_t *c64_palette_get_working_colors(void)
{
    // Lock-free read: Single-writer (UI thread), multiple readers (render threads)
    // Readers can tolerate brief stale values during palette changes
    return palette_system.working_colors;
}

bool c64_palette_set_working_color(int index, uint32_t bgra_color)
{
    if (index < 0 || index >= C64_PALETTE_COLORS) {
        return false;
    }

    // Log if this is the first modification
    if (!palette_system.working_modified) {
        const char *palette_name = "Unknown";
        if (palette_system.active_palette_index >= 0 &&
            palette_system.active_palette_index < palette_system.palette_count) {
            palette_name = palette_system.palettes[palette_system.active_palette_index].name;
        }
        C64_LOG_INFO("" PALETTE_LOG_PREFIX " 🎨 User modified color %d in palette '%s' (0x%08X)", index, palette_name,
                     bgra_color);
    }

    palette_system.working_colors[index] = bgra_color;
    palette_system.working_modified = true;

    // Rebuild LUT immediately for live preview
    c64_palette_rebuild_lut(palette_system.working_colors);

    return true;
}

const char *c64_palette_get_description(const char *palette_id)
{
    if (!palette_id) {
        return NULL;
    }

    for (int i = 0; i < palette_system.palette_count; i++) {
        if (strcmp(palette_system.palettes[i].id, palette_id) == 0) {
            return palette_system.palettes[i].desc[0] ? palette_system.palettes[i].desc : NULL;
        }
    }

    return NULL;
}

bool c64_palette_has_modifications(void)
{
    return palette_system.working_modified;
}

void c64_palette_revert(void)
{
    if (palette_system.active_palette_index < 0) {
        return;
    }

    struct c64_palette_entry *entry = &palette_system.palettes[palette_system.active_palette_index];
    memcpy(palette_system.working_colors, entry->colors, sizeof(entry->colors));
    palette_system.working_modified = false;

    // Rebuild LUT with reverted colors
    c64_palette_rebuild_lut(palette_system.working_colors);

    C64_LOG_INFO("" PALETTE_LOG_PREFIX " Reverted to saved palette: %s", entry->name);
}

bool c64_palette_save(void)
{
    if (palette_system.active_palette_index < 0) {
        return false;
    }

    struct c64_palette_entry *entry = &palette_system.palettes[palette_system.active_palette_index];

    // Cannot overwrite shipped palettes
    if (entry->is_shipped) {
        C64_LOG_INFO("" PALETTE_LOG_PREFIX " Cannot overwrite shipped palette: %s (use Save As)", entry->name);
        return false;
    }

    // Write to VPL file
    if (!c64_palette_write_vpl(entry->path, palette_system.working_colors, entry->name)) {
        C64_LOG_WARNING("" PALETTE_LOG_PREFIX " Failed to save palette: %s", entry->path);
        return false;
    }

    // Update stored colors
    memcpy(entry->colors, palette_system.working_colors, sizeof(entry->colors));
    palette_system.working_modified = false;

    C64_LOG_INFO("" PALETTE_LOG_PREFIX " Saved palette: %s", entry->name);
    return true;
}

bool c64_palette_save_as(const char *name, const char *path)
{
    if (!name || !name[0] || !path || !path[0]) {
        return false;
    }

    // Ensure user directory exists
    char user_dir[C64_PALETTE_PATH_MAX];
    if (!c64_palette_get_user_dir(user_dir, sizeof(user_dir))) {
        return false;
    }

    // Write to VPL file
    if (!c64_palette_write_vpl(path, palette_system.working_colors, name)) {
        C64_LOG_WARNING("" PALETTE_LOG_PREFIX " Failed to save palette as: %s", path);
        return false;
    }

    // Extract ID from filename
    char id[C64_PALETTE_NAME_MAX];
    const char *filename = strrchr(path, PATH_SEP);
    if (!filename) {
        filename = path;
    } else {
        filename++;
    }
    strncpy(id, filename, sizeof(id) - 1);
    id[sizeof(id) - 1] = '\0';

    // Remove .vpl extension from ID
    char *ext = strrchr(id, '.');
    if (ext && strcasecmp(ext, ".vpl") == 0) {
        *ext = '\0';
    }

    // Add to palette list if not already present
    bool found = false;
    for (int i = 0; i < palette_system.palette_count; i++) {
        if (strcmp(palette_system.palettes[i].id, id) == 0) {
            found = true;
            palette_system.palettes[i].colors_loaded = true;
            memcpy(palette_system.palettes[i].colors, palette_system.working_colors,
                   sizeof(palette_system.palettes[i].colors));
            break;
        }
    }

    if (!found) {
        if (!add_palette_entry(id, name, path, false)) {
            C64_LOG_WARNING("" PALETTE_LOG_PREFIX " Failed to add palette entry: %s", id);
            return false;
        }
        // Load colors for the new entry
        int new_index = palette_system.palette_count - 1;
        memcpy(palette_system.palettes[new_index].colors, palette_system.working_colors,
               sizeof(palette_system.palettes[new_index].colors));
        palette_system.palettes[new_index].colors_loaded = true;
    }

    // Sort and select the new palette
    sort_palettes();
    c64_palette_select(id);

    C64_LOG_INFO("" PALETTE_LOG_PREFIX " Saved palette as: %s", name);
    return true;
}

bool c64_palette_load_from_file(const char *path)
{
    if (!path || !path[0]) {
        return false;
    }

    // Parse the VPL file
    uint32_t colors[C64_PALETTE_COLORS];
    char name[C64_PALETTE_NAME_MAX];
    char desc[256];
    if (!c64_palette_parse_vpl(path, colors, name, sizeof(name), desc, sizeof(desc))) {
        C64_LOG_WARNING("" PALETTE_LOG_PREFIX " Failed to parse VPL file: %s", path);
        return false;
    }

    // Copy to user palette directory
    char user_dir[C64_PALETTE_PATH_MAX];
    if (!c64_palette_get_user_dir(user_dir, sizeof(user_dir))) {
        return false;
    }

    // Extract filename
    const char *src_filename = strrchr(path, PATH_SEP);
#ifdef _WIN32
    if (!src_filename) {
        src_filename = strrchr(path, '/');
    }
#endif
    if (!src_filename) {
        src_filename = path;
    } else {
        src_filename++;
    }

    // Create destination path
    char dest_path[C64_PALETTE_PATH_MAX];
    int dest_len = snprintf(dest_path, sizeof(dest_path), "%s%c%s", user_dir, PATH_SEP, src_filename);
    if (dest_len < 0 || (size_t)dest_len >= sizeof(dest_path)) {
        C64_LOG_WARNING("" PALETTE_LOG_PREFIX " Destination path too long for palette import");
        return false;
    }

    // Extract ID from filename
    char id[C64_PALETTE_NAME_MAX];
    strncpy(id, src_filename, sizeof(id) - 1);
    id[sizeof(id) - 1] = '\0';
    char *ext = strrchr(id, '.');
    if (ext && strcasecmp(ext, ".vpl") == 0) {
        *ext = '\0';
    }

    // Check if this is a shipped palette - reject loading them
    for (int i = 0; i < palette_system.palette_count; i++) {
        if (palette_system.palettes[i].is_shipped && strcmp(palette_system.palettes[i].id, id) == 0) {
            C64_LOG_WARNING(
                "" PALETTE_LOG_PREFIX " Cannot load shipped palette '%s' - it's already available in the dropdown", id);
            return false;
        }
    }

    // If name is empty, use ID
    if (!name[0]) {
        strncpy(name, id, sizeof(name) - 1);
    }

    // Write the file to user directory
    if (!c64_palette_write_vpl(dest_path, colors, name)) {
        C64_LOG_WARNING("" PALETTE_LOG_PREFIX " Failed to copy palette to user directory: %s", dest_path);
        return false;
    }

    // Check if already exists
    bool found = false;
    for (int i = 0; i < palette_system.palette_count; i++) {
        if (strcmp(palette_system.palettes[i].id, id) == 0) {
            found = true;
            // Update existing entry
            memcpy(palette_system.palettes[i].colors, colors, sizeof(colors));
            palette_system.palettes[i].colors_loaded = true;
            strncpy(palette_system.palettes[i].name, name, sizeof(palette_system.palettes[i].name) - 1);
            strncpy(palette_system.palettes[i].path, dest_path, sizeof(palette_system.palettes[i].path) - 1);
            break;
        }
    }

    if (!found) {
        if (!add_palette_entry(id, name, dest_path, false)) {
            return false;
        }
        // Load colors for the new entry
        int new_index = palette_system.palette_count - 1;
        memcpy(palette_system.palettes[new_index].colors, colors, sizeof(colors));
        palette_system.palettes[new_index].colors_loaded = true;
    }

    // Sort palettes
    sort_palettes();

    // Select the loaded palette
    c64_palette_select(id);

    C64_LOG_INFO("" PALETTE_LOG_PREFIX " Loaded palette from file: %s", name);
    return true;
}

bool c64_palette_parse_vpl(const char *path, uint32_t *colors, char *name, size_t name_size, char *desc,
                           size_t desc_size)
{
    if (!path || !colors) {
        return false;
    }

    FILE *file = fopen(path, "r");
    if (!file) {
        C64_LOG_WARNING("" PALETTE_LOG_PREFIX " Failed to open VPL file: %s", path);
        return false;
    }

    if (name && name_size > 0) {
        name[0] = '\0';
    }
    if (desc && desc_size > 0) {
        desc[0] = '\0';
    }

    char line[256];
    int color_count = 0;
    bool got_name = false;
    bool got_desc = false;

    while (fgets(line, sizeof(line), file) && color_count < C64_PALETTE_COLORS) {
        // Remove trailing newline
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }

        // Find and remove inline comments (anything after #)
        char *comment = strchr(line, '#');
        if (comment) {
            char *content_start = comment + 1;
            // Trim leading whitespace
            while (*content_start && isspace((unsigned char)*content_start)) {
                content_start++;
            }

            // Check for DESC: prefix
            if (!got_desc && desc && desc_size > 0 && strncmp(content_start, "DESC:", 5) == 0) {
                char *desc_start = content_start + 5;
                // Trim leading whitespace after DESC:
                while (*desc_start && isspace((unsigned char)*desc_start)) {
                    desc_start++;
                }
                if (*desc_start) {
                    strncpy(desc, desc_start, desc_size - 1);
                    desc[desc_size - 1] = '\0';
                    // Trim trailing whitespace
                    size_t desc_len = strlen(desc);
                    while (desc_len > 0 && isspace((unsigned char)desc[desc_len - 1])) {
                        desc[--desc_len] = '\0';
                    }
                    got_desc = true;
                }
            }
            // Check for NAME: prefix
            else if (!got_name && name && name_size > 0 && strncmp(content_start, "NAME:", 5) == 0) {
                char *name_start = content_start + 5;
                // Trim leading whitespace after NAME:
                while (*name_start && isspace((unsigned char)*name_start)) {
                    name_start++;
                }
                if (*name_start) {
                    strncpy(name, name_start, name_size - 1);
                    name[name_size - 1] = '\0';
                    // Trim trailing whitespace
                    size_t name_len = strlen(name);
                    while (name_len > 0 && isspace((unsigned char)name[name_len - 1])) {
                        name[--name_len] = '\0';
                    }
                    got_name = true;
                }
            }
            // If this is a non-metadata comment and we haven't got a name yet, use it as fallback
            else if (!got_name && name && name_size > 0 && *content_start) {
                // Skip metadata lines (TYPE:, Syntax:, VICE Palette file, Red Green Blue, etc.)
                if (strncmp(content_start, "TYPE:", 5) != 0 && strncmp(content_start, "Syntax:", 7) != 0 &&
                    strncmp(content_start, "Red Green Blue", 14) != 0 &&
                    strncmp(content_start, "VICE Palette", 12) != 0) {
                    strncpy(name, content_start, name_size - 1);
                    name[name_size - 1] = '\0';
                    // Trim trailing whitespace
                    size_t name_len = strlen(name);
                    while (name_len > 0 && isspace((unsigned char)name[name_len - 1])) {
                        name[--name_len] = '\0';
                    }
                    got_name = true;
                }
            }
            *comment = '\0';
        }

        // Trim whitespace
        char *trimmed = trim_whitespace(line);
        if (!trimmed || !*trimmed) {
            continue; // Skip empty lines
        }

        // Parse RGB values (hex format: RR GG BB, optionally followed by dither value)
        unsigned int r, g, b;
        if (sscanf(trimmed, "%x %x %x", &r, &g, &b) >= 3) {
            if (r > 255)
                r = 255;
            if (g > 255)
                g = 255;
            if (b > 255)
                b = 255;

            // Convert RGB to BGRA (OBS format)
            colors[color_count] = 0xFF000000 | (r << 16) | (g << 8) | b;
            // Note: Above creates ARGB, but OBS uses BGRA
            // Actually OBS VIDEO_FORMAT_RGBA stores as BGRA in memory (little-endian)
            // The c64_default_palette uses BGRA format: 0xFFBBGGRR
            colors[color_count] = 0xFF000000 | (b << 16) | (g << 8) | r;
            color_count++;
        }
    }

    fclose(file);

    if (color_count != C64_PALETTE_COLORS) {
        C64_LOG_WARNING("" PALETTE_LOG_PREFIX " VPL file has %d colors, expected %d: %s", color_count,
                        C64_PALETTE_COLORS, path);
        return false;
    }

    // If no name was found, use camel-cased filename as fallback
    if (name && name_size > 0 && !name[0]) {
        const char *filename = strrchr(path, PATH_SEP);
#ifdef _WIN32
        if (!filename) {
            filename = strrchr(path, '/');
        }
#endif
        if (filename) {
            filename++; // Skip separator
        } else {
            filename = path;
        }

        // Copy filename without extension and capitalize first letter
        size_t i = 0;
        bool capitalize_next = true;
        while (filename[i] && filename[i] != '.' && i < name_size - 1) {
            if (filename[i] == '_' || filename[i] == '-') {
                capitalize_next = true;
            } else {
                name[i] = capitalize_next ? toupper((unsigned char)filename[i]) : filename[i];
                capitalize_next = false;
                i++;
            }
        }
        name[i] = '\0';
    }

    return true;
}

bool c64_palette_write_vpl(const char *path, const uint32_t *colors, const char *name)
{
    C64_LOG_INFO("" PALETTE_LOG_PREFIX " Writing VPL file: %s (name: %s)", path ? path : "(null)",
                 name ? name : "(null)");

    if (!path || !colors) {
        C64_LOG_WARNING("" PALETTE_LOG_PREFIX " Write VPL failed: invalid parameters");
        return false;
    }

    // Ensure parent directory exists
    char dir[C64_PALETTE_PATH_MAX];
    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *sep = strrchr(dir, PATH_SEP);
#ifdef _WIN32
    if (!sep) {
        sep = strrchr(dir, '/');
    }
#endif
    if (sep) {
        *sep = '\0';
        c64_create_directory_recursive(dir);
    }

    // Extract filename and convert to camel case for NAME field
    char camel_name[C64_PALETTE_NAME_MAX];
    const char *filename = strrchr(path, PATH_SEP);
#ifdef _WIN32
    if (!filename) {
        filename = strrchr(path, '/');
    }
#endif
    if (!filename) {
        filename = path;
    } else {
        filename++;
    }

    // Convert filename to camel case (remove extension, capitalize first letter and after underscores/spaces)
    strncpy(camel_name, filename, sizeof(camel_name) - 1);
    camel_name[sizeof(camel_name) - 1] = '\0';

    // Remove .vpl extension
    char *ext = strrchr(camel_name, '.');
    if (ext && strcasecmp(ext, ".vpl") == 0) {
        *ext = '\0';
    }

    // Convert to camel case: capitalize first letter and after underscores/hyphens
    bool capitalize_next = true;
    for (char *p = camel_name; *p; p++) {
        if (*p == '_' || *p == '-') {
            *p = ' '; // Replace with space
            capitalize_next = true;
        } else if (capitalize_next && *p >= 'a' && *p <= 'z') {
            *p = *p - 'a' + 'A';
            capitalize_next = false;
        } else if (*p >= 'A' && *p <= 'Z') {
            capitalize_next = false;
        } else if (*p >= 'a' && *p <= 'z') {
            capitalize_next = false;
        }
    }

    FILE *file = fopen(path, "w");
    if (!file) {
        C64_LOG_WARNING("" PALETTE_LOG_PREFIX " Failed to create VPL file: %s (check permissions/path)", path);
        return false;
    }

    C64_LOG_INFO("" PALETTE_LOG_PREFIX " VPL file opened for writing: %s", path);

    // Write VICE VPL format header
    fprintf(file, "# VICE Palette file\n");
    fprintf(file, "#\n");
    fprintf(file, "# Syntax:\n");
    fprintf(file, "# Red Green Blue\n");
    fprintf(file, "#\n");
    fprintf(file, "# TYPE:VICII\n");
    // KEY LESSON: Always use the provided name parameter for both NAME and DESC to preserve special
    // characters (parentheses, etc). Only fall back to filename-derived camel_name when no explicit
    // name is provided. This ensures "Muted (Custom)" stays intact rather than becoming "Muted Custom".
    const char *display_name = (name && name[0]) ? name : camel_name;
    fprintf(file, "# NAME:%s\n", display_name);
    fprintf(file, "# DESC:%s\n", display_name);
    fprintf(file, "\n");

    // Standard C64 color names (in order 0-15)
    static const char *color_names[16] = {"Black",   "White",      "Red",       "Cyan",     "Purple", "Green",
                                          "Blue",    "Yellow",     "Orange",    "Brown",    "Pink",   "DarkGrey",
                                          "MedGrey", "LightGreen", "LightBlue", "LightGrey"};

    // Write colors in VPL format (RR GG BB) with color name comments
    for (int i = 0; i < C64_PALETTE_COLORS; i++) {
        uint32_t bgra = colors[i];
        // Extract BGR from BGRA (little-endian format)
        uint8_t b = (bgra >> 16) & 0xFF;
        uint8_t g = (bgra >> 8) & 0xFF;
        uint8_t r = bgra & 0xFF;

        fprintf(file, "%02X %02X %02X  # %s\n", r, g, b, color_names[i]);
    }

    fclose(file);
    C64_LOG_INFO("" PALETTE_LOG_PREFIX " VPL file written successfully: %s", path);
    return true;
}

bool c64_palette_get_user_dir(char *path, size_t path_size)
{
    return c64_get_user_dir(C64_USER_DIR_PALETTES, path, path_size);
}

void c64_palette_rebuild_lut(const uint32_t *colors)
{
    if (!colors) {
        return;
    }

    // Lock-free LUT rebuild: Direct writes to globally shared LUT
    // Individual uint64_t writes are atomic on x86_64 (naturally 8-byte aligned)
    // Worst case: one frame sees partial old/new palette during 256-entry update
    // No locks in rendering path for maximum performance (3400+ packets/sec)
    extern uint64_t c64_color_pair_lut[256];
    extern uint32_t c64_current_palette[16];
    extern bool c64_color_lut_initialized;

    // Update the current palette
    memcpy(c64_current_palette, colors, sizeof(c64_current_palette));

    // Rebuild the LUT with direct atomic writes
    for (int i = 0; i < 256; i++) {
        uint8_t color1 = i & 0x0F;
        uint8_t color2 = (i >> 4) & 0x0F;
        uint64_t packed = ((uint64_t)colors[color2] << 32) | colors[color1];
        c64_color_pair_lut[i] = packed; // Atomic write on x86_64
    }

    c64_color_lut_initialized = true;
    C64_LOG_INFO("" PALETTE_LOG_PREFIX " 🔄 Color LUT rebuilt with palette colors (256 lookup entries updated)");
}

const uint32_t *c64_palette_get_active_colors(void)
{
    return palette_system.working_colors;
}

// ============================================================================
// Internal helper functions
// ============================================================================

static char *trim_whitespace(char *str)
{
    if (!str) {
        return NULL;
    }

    // Trim leading
    while (*str && isspace((unsigned char)*str)) {
        str++;
    }

    if (!*str) {
        return str;
    }

    // Trim trailing
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        *end-- = '\0';
    }

    return str;
}

static void discover_shipped_palettes(void)
{
    char *palettes_path = obs_module_file("palettes");
    if (!palettes_path) {
        C64_LOG_WARNING("" PALETTE_LOG_PREFIX " Shipped palettes directory not found");
        return;
    }

    C64_LOG_INFO("" PALETTE_LOG_PREFIX " Discovering shipped palettes from: %s", palettes_path);

#ifdef _WIN32
    char search_path[C64_PALETTE_PATH_MAX];
    snprintf(search_path, sizeof(search_path), "%s\\*.vpl", palettes_path);

    WIN32_FIND_DATAA find_data;
    HANDLE find_handle = FindFirstFileA(search_path, &find_data);
    if (find_handle != INVALID_HANDLE_VALUE) {
        do {
            if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                char full_path[C64_PALETTE_PATH_MAX];
                snprintf(full_path, sizeof(full_path), "%s\\%s", palettes_path, find_data.cFileName);

                char id[C64_PALETTE_NAME_MAX];
                strncpy(id, find_data.cFileName, sizeof(id) - 1);
                id[sizeof(id) - 1] = '\0';
                char *ext = strrchr(id, '.');
                if (ext)
                    *ext = '\0';

                // Skip "default" and "Default" - we add it manually
                if (strcasecmp(id, "Default") == 0) {
                    continue;
                }

                // Try to get display name from file
                char name[C64_PALETTE_NAME_MAX];
                char desc[256];
                uint32_t temp_colors[C64_PALETTE_COLORS];
                if (c64_palette_parse_vpl(full_path, temp_colors, name, sizeof(name), desc, sizeof(desc))) {
                    if (!name[0]) {
                        strncpy(name, id, sizeof(name) - 1);
                    }
                    add_palette_entry(id, name, full_path, true);
                    // Store description if we got one
                    if (desc[0] && palette_system.palette_count > 0) {
                        int idx = palette_system.palette_count - 1;
                        strncpy(palette_system.palettes[idx].desc, desc, sizeof(palette_system.palettes[idx].desc) - 1);
                        palette_system.palettes[idx].desc[sizeof(palette_system.palettes[idx].desc) - 1] = '\0';
                    }
                }
            }
        } while (FindNextFileA(find_handle, &find_data));
        FindClose(find_handle);
    }
#else
    DIR *dir = opendir(palettes_path);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            // Skip directories and non-.vpl files
            size_t len = strlen(entry->d_name);
            if (len < 5 || strcasecmp(entry->d_name + len - 4, ".vpl") != 0) {
                continue;
            }

            char full_path[C64_PALETTE_PATH_MAX];
            snprintf(full_path, sizeof(full_path), "%s/%s", palettes_path, entry->d_name);

            char id[C64_PALETTE_NAME_MAX];
            strncpy(id, entry->d_name, sizeof(id) - 1);
            id[sizeof(id) - 1] = '\0';
            char *ext = strrchr(id, '.');
            if (ext) {
                *ext = '\0';
            }

            // Skip "default" and "Default" - we add it manually
            if (strcasecmp(id, "Default") == 0) {
                continue;
            }

            // Try to get display name from file
            char name[C64_PALETTE_NAME_MAX];
            char desc[256];
            uint32_t temp_colors[C64_PALETTE_COLORS];
            if (c64_palette_parse_vpl(full_path, temp_colors, name, sizeof(name), desc, sizeof(desc))) {
                if (!name[0]) {
                    strncpy(name, id, sizeof(name) - 1);
                }
                add_palette_entry(id, name, full_path, true);
                // Store description if we got one
                if (desc[0] && palette_system.palette_count > 0) {
                    int idx = palette_system.palette_count - 1;
                    strncpy(palette_system.palettes[idx].desc, desc, sizeof(palette_system.palettes[idx].desc) - 1);
                    palette_system.palettes[idx].desc[sizeof(palette_system.palettes[idx].desc) - 1] = '\0';
                }
            }
        }
        closedir(dir);
    }
#endif

    bfree(palettes_path);
}

static void discover_custom_palettes(void)
{
    if (!palette_system.user_palette_dir[0]) {
        return;
    }

    C64_LOG_INFO("" PALETTE_LOG_PREFIX " Discovering custom palettes from: %s", palette_system.user_palette_dir);
    int discovered_count = 0;

#ifdef _WIN32
    char search_path[C64_PALETTE_PATH_MAX];
    snprintf(search_path, sizeof(search_path), "%s\\*.vpl", palette_system.user_palette_dir);

    WIN32_FIND_DATAA find_data;
    HANDLE find_handle = FindFirstFileA(search_path, &find_data);
    if (find_handle != INVALID_HANDLE_VALUE) {
        do {
            if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                char full_path[C64_PALETTE_PATH_MAX];
                snprintf(full_path, sizeof(full_path), "%s\\%s", palette_system.user_palette_dir, find_data.cFileName);

                char id[C64_PALETTE_NAME_MAX];
                strncpy(id, find_data.cFileName, sizeof(id) - 1);
                id[sizeof(id) - 1] = '\0';
                char *ext = strrchr(id, '.');
                if (ext)
                    *ext = '\0';

                // Check if already exists (avoid duplicates)
                bool found = false;
                for (int i = 0; i < palette_system.palette_count; i++) {
                    if (strcmp(palette_system.palettes[i].id, id) == 0) {
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    char name[C64_PALETTE_NAME_MAX];
                    char desc[256];
                    uint32_t temp_colors[C64_PALETTE_COLORS];
                    if (c64_palette_parse_vpl(full_path, temp_colors, name, sizeof(name), desc, sizeof(desc))) {
                        if (!name[0]) {
                            strncpy(name, id, sizeof(name) - 1);
                        }
                        C64_LOG_INFO("" PALETTE_LOG_PREFIX "   Discovered: %s (%s)", id, name);
                        add_palette_entry(id, name, full_path, false);
                        discovered_count++;
                        if (desc[0] && palette_system.palette_count > 0) {
                            int idx = palette_system.palette_count - 1;
                            strncpy(palette_system.palettes[idx].desc, desc,
                                    sizeof(palette_system.palettes[idx].desc) - 1);
                            palette_system.palettes[idx].desc[sizeof(palette_system.palettes[idx].desc) - 1] = '\0';
                        }
                    }
                } else {
                    C64_LOG_INFO("" PALETTE_LOG_PREFIX "   Skipped (already exists): %s", id);
                }
            }
        } while (FindNextFileA(find_handle, &find_data));
        FindClose(find_handle);
    }
#else
    DIR *dir = opendir(palette_system.user_palette_dir);
    if (!dir) {
        C64_LOG_WARNING("" PALETTE_LOG_PREFIX " Failed to open palette directory: %s", palette_system.user_palette_dir);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN) {
            const char *ext = strrchr(entry->d_name, '.');
            if (ext && strcasecmp(ext, ".vpl") == 0) {
                char full_path[C64_PALETTE_PATH_MAX];
                int path_len =
                    snprintf(full_path, sizeof(full_path), "%s/%s", palette_system.user_palette_dir, entry->d_name);
                if (path_len < 0 || (size_t)path_len >= sizeof(full_path)) {
                    C64_LOG_WARNING("" PALETTE_LOG_PREFIX " Palette path too long, skipping: %s", entry->d_name);
                    continue;
                }

                char id[C64_PALETTE_NAME_MAX];
                strncpy(id, entry->d_name, sizeof(id) - 1);
                id[sizeof(id) - 1] = '\0';
                char *dot = strrchr(id, '.');
                if (dot)
                    *dot = '\0';

                // Check if already exists (avoid duplicates)
                bool found = false;
                for (int i = 0; i < palette_system.palette_count; i++) {
                    if (strcmp(palette_system.palettes[i].id, id) == 0) {
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    char name[C64_PALETTE_NAME_MAX];
                    char desc[256];
                    uint32_t temp_colors[C64_PALETTE_COLORS];
                    if (c64_palette_parse_vpl(full_path, temp_colors, name, sizeof(name), desc, sizeof(desc))) {
                        if (!name[0]) {
                            strncpy(name, id, sizeof(name) - 1);
                        }
                        C64_LOG_INFO("" PALETTE_LOG_PREFIX "   Discovered: %s (%s)", id, name);
                        add_palette_entry(id, name, full_path, false);
                        discovered_count++;
                        if (desc[0] && palette_system.palette_count > 0) {
                            int idx = palette_system.palette_count - 1;
                            strncpy(palette_system.palettes[idx].desc, desc,
                                    sizeof(palette_system.palettes[idx].desc) - 1);
                            palette_system.palettes[idx].desc[sizeof(palette_system.palettes[idx].desc) - 1] = '\0';
                        }
                    }
                } else {
                    C64_LOG_INFO("" PALETTE_LOG_PREFIX "   Skipped (already exists): %s", id);
                }
            }
        }
    }

    closedir(dir);
#endif

    C64_LOG_INFO("" PALETTE_LOG_PREFIX " Discovery complete: added %d new custom palette%s", discovered_count,
                 discovered_count == 1 ? "" : "s");
}

static bool add_palette_entry(const char *id, const char *name, const char *path, bool is_shipped)
{
    C64_LOG_INFO("" PALETTE_LOG_PREFIX " Adding palette entry to dropdown: id='%s', name='%s', path='%s', shipped=%d",
                 id ? id : "(null)", name ? name : "(null)", path ? path : "(null)", is_shipped);

    if (palette_system.palette_count >= C64_MAX_PALETTES) {
        C64_LOG_WARNING("" PALETTE_LOG_PREFIX " Maximum palette count reached (%d)", C64_MAX_PALETTES);
        return false;
    }

    // Check for duplicate IDs (case-insensitive)
    for (int i = 0; i < palette_system.palette_count; i++) {
        if (strcasecmp(palette_system.palettes[i].id, id) == 0) {
            C64_LOG_INFO("" PALETTE_LOG_PREFIX "   Palette with ID '%s' already exists in dropdown, skipping", id);
            return false;
        }
    }

    struct c64_palette_entry *entry = &palette_system.palettes[palette_system.palette_count];
    strncpy(entry->id, id, sizeof(entry->id) - 1);
    entry->id[sizeof(entry->id) - 1] = '\0';
    strncpy(entry->name, name, sizeof(entry->name) - 1);
    entry->name[sizeof(entry->name) - 1] = '\0';
    strncpy(entry->path, path, sizeof(entry->path) - 1);
    entry->path[sizeof(entry->path) - 1] = '\0';
    entry->is_shipped = is_shipped;
    entry->colors_loaded = false;

    palette_system.palette_count++;
    C64_LOG_INFO("" PALETTE_LOG_PREFIX " Palette entry added to dropdown: '%s' (total count: %d)", id,
                 palette_system.palette_count);
    return true;
}

bool c64_palette_is_preset(const char *palette_id)
{
    if (!palette_id) {
        return false;
    }

    for (int i = 0; i < palette_system.palette_count; i++) {
        if (strcmp(palette_system.palettes[i].id, palette_id) == 0) {
            return palette_system.palettes[i].is_shipped;
        }
    }

    return false;
}

bool c64_palette_get_display_name(const char *palette_id, char *display_name, size_t display_name_size)
{
    if (!palette_id || !display_name || display_name_size == 0) {
        return false;
    }

    for (int i = 0; i < palette_system.palette_count; i++) {
        if (strcmp(palette_system.palettes[i].id, palette_id) == 0) {
            if (palette_system.palettes[i].is_shipped) {
                snprintf(display_name, display_name_size, "%s (Preset)", palette_system.palettes[i].name);
            } else {
                strncpy(display_name, palette_system.palettes[i].name, display_name_size - 1);
                display_name[display_name_size - 1] = '\0';
            }
            return true;
        }
    }

    return false;
}

bool c64_palette_auto_save(obs_data_t *settings)
{
    // No modifications, nothing to do
    if (!palette_system.working_modified) {
        return false; // No change, no UI refresh needed
    }

    // Defensive bounds check for active palette index
    if (palette_system.active_palette_index < 0 ||
        palette_system.active_palette_index >= palette_system.palette_count) {
        C64_LOG_WARNING("" PALETTE_LOG_PREFIX " Invalid active palette index: %d (count: %d)",
                        palette_system.active_palette_index, palette_system.palette_count);
        return false;
    }

    struct c64_palette_entry *entry = &palette_system.palettes[palette_system.active_palette_index];

    // If editing a preset, create a custom copy
    if (entry->is_shipped) {
        // Generate a custom name with "(Custom)" suffix for display
        char custom_name[C64_PALETTE_NAME_MAX];
        int name_len = snprintf(custom_name, sizeof(custom_name), "%s (Custom)", entry->name);
        if (name_len < 0 || (size_t)name_len >= sizeof(custom_name)) {
            C64_LOG_WARNING("" PALETTE_LOG_PREFIX " Custom palette name too long");
            return false;
        }

        // Convert preset ID to lowercase for filename (e.g., "Default" -> "default")
        char lowercase_id[C64_PALETTE_NAME_MAX];
        strncpy(lowercase_id, entry->id, sizeof(lowercase_id) - 1);
        lowercase_id[sizeof(lowercase_id) - 1] = '\0';
        for (char *p = lowercase_id; *p; p++) {
            *p = tolower((unsigned char)*p);
        }

        // Build path with lowercase ID and "-custom" suffix (e.g., "default-custom.vpl")
        char custom_path[C64_PALETTE_PATH_MAX];
        int path_len = snprintf(custom_path, sizeof(custom_path), "%s%c%s-custom.vpl", palette_system.user_palette_dir,
                                PATH_SEP, lowercase_id);
        if (path_len < 0 || (size_t)path_len >= sizeof(custom_path)) {
            C64_LOG_WARNING("" PALETTE_LOG_PREFIX " Custom palette path too long");
            return false;
        }

        // Save as new custom palette (overwrites if exists)
        if (!c64_palette_save_as(custom_name, custom_path)) {
            C64_LOG_WARNING("" PALETTE_LOG_PREFIX " Failed to auto-save preset as custom palette: %s", custom_name);
            return false;
        }

        // Update OBS settings to point to the new custom palette ID
        // This ensures the custom palette is reloaded when properties dialog reopens
        if (settings) {
            const char *new_id = c64_palette_get_active_id();
            obs_data_set_string(settings, "palette", new_id);
            C64_LOG_INFO("�� PALETTE: Updated settings to use custom palette: %s (was: %s)", new_id, entry->id);
        }
        return true; // Palette converted, UI refresh needed
    }

    // For custom palettes, save in place (overwrites existing file)
    if (!c64_palette_write_vpl(entry->path, palette_system.working_colors, entry->name)) {
        C64_LOG_WARNING("" PALETTE_LOG_PREFIX " Failed to auto-save custom palette: %s", entry->path);
        return false;
    }

    // Update stored colors
    memcpy(entry->colors, palette_system.working_colors, sizeof(entry->colors));
    palette_system.working_modified = false;

    C64_LOG_INFO("" PALETTE_LOG_PREFIX " Auto-saved custom palette: %s", entry->name);
    return false; // Custom palette saved, no UI refresh needed
}

bool c64_palette_delete(const char *palette_id)
{
    if (!palette_id || !palette_id[0]) {
        return false;
    }

    C64_LOG_INFO("" PALETTE_LOG_PREFIX " Delete requested for palette: %s", palette_id);

    // Find the palette
    int index = -1;
    for (int i = 0; i < palette_system.palette_count; i++) {
        if (strcmp(palette_system.palettes[i].id, palette_id) == 0) {
            index = i;
            break;
        }
    }

    if (index < 0) {
        C64_LOG_WARNING("" PALETTE_LOG_PREFIX " Cannot delete palette: not found: %s", palette_id);
        return false;
    }

    struct c64_palette_entry *entry = &palette_system.palettes[index];

    // Cannot delete shipped palettes
    if (entry->is_shipped) {
        C64_LOG_WARNING("" PALETTE_LOG_PREFIX " Cannot delete shipped palette: %s", palette_id);
        return false;
    }

    C64_LOG_INFO("" PALETTE_LOG_PREFIX " Deleting file: %s", entry->path);

    // Delete the file
    C64_LOG_INFO("" PALETTE_LOG_PREFIX "   Checking if file exists: %s", entry->path);
    if (os_file_exists(entry->path)) {
        C64_LOG_INFO("" PALETTE_LOG_PREFIX "   File exists, attempting deletion: %s", entry->path);
        if (os_unlink(entry->path) != 0) {
            C64_LOG_WARNING("" PALETTE_LOG_PREFIX " Failed to delete palette file: %s (check permissions)",
                            entry->path);
            return false;
        }
        C64_LOG_INFO("" PALETTE_LOG_PREFIX " VPL file deleted successfully: %s", entry->path);
    } else {
        C64_LOG_WARNING("" PALETTE_LOG_PREFIX "   File already gone (already deleted?): %s", entry->path);
    }

    // If this was the active palette, switch to Default
    bool was_active = (palette_system.active_palette_index == index);
    if (was_active) {
        C64_LOG_INFO("" PALETTE_LOG_PREFIX " Switching active palette to Default");
        c64_palette_select("Default");
    }

    // Remove from array by shifting remaining entries
    C64_LOG_INFO("" PALETTE_LOG_PREFIX "   Removing palette entry from dropdown: '%s' (index %d of %d)", palette_id,
                 index, palette_system.palette_count);
    for (int i = index; i < palette_system.palette_count - 1; i++) {
        palette_system.palettes[i] = palette_system.palettes[i + 1];
    }
    palette_system.palette_count--;

    C64_LOG_INFO("" PALETTE_LOG_PREFIX " Palette entry removed from dropdown, count now: %d",
                 palette_system.palette_count);

    // Update active index if needed
    if (palette_system.active_palette_index > index) {
        palette_system.active_palette_index--;
    }

    C64_LOG_INFO("" PALETTE_LOG_PREFIX " Deleted custom palette: %s", palette_id);
    return true;
}

static int palette_compare(const void *a, const void *b)
{
    const struct c64_palette_entry *pa = (const struct c64_palette_entry *)a;
    const struct c64_palette_entry *pb = (const struct c64_palette_entry *)b;

    // Default always first
    if (strcmp(pa->id, "Default") == 0) {
        return -1;
    }
    if (strcmp(pb->id, "Default") == 0) {
        return 1;
    }

    // Then sort alphabetically by name
    return strcasecmp(pa->name, pb->name);
}

static void sort_palettes(void)
{
    if (palette_system.palette_count > 1) {
        qsort(palette_system.palettes, palette_system.palette_count, sizeof(struct c64_palette_entry), palette_compare);
    }
}
