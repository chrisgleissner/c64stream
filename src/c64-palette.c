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
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <direct.h>
#define PATH_SEP '\\'
#else
#include <sys/stat.h>
#include <dirent.h>
#define PATH_SEP '/'
#endif

// Global palette system state
static struct c64_palette_system palette_system;
static bool palette_initialized = false;

// Default palette (matches vic_colors in c64-color.c exactly)
static const uint32_t default_palette_colors[C64_PALETTE_COLORS] = {
    0xFF000000, // 0: Black
    0xFFF7F7F7, // 1: White
    0xFF342F8D, // 2: Red
    0xFFCDD46A, // 3: Cyan
    0xFFA43598, // 4: Purple
    0xFF42B44C, // 5: Green
    0xFFB1292C, // 6: Blue
    0xFF5DEFEF, // 7: Yellow
    0xFF204E98, // 8: Orange
    0xFF00385B, // 9: Brown
    0xFF6D67D1, // 10: Pink
    0xFF4A4A4A, // 11: Dark Grey
    0xFF7B7B7B, // 12: Medium Grey
    0xFF93EF9F, // 13: Light Green
    0xFFEF6A6D, // 14: Light Blue
    0xFFB2B2B2  // 15: Light Grey
};

// Forward declarations
static void discover_shipped_palettes(void);
static void load_palette_ini(void);
static void save_palette_ini(void);
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
        C64_LOG_WARNING("Failed to set up user palette directory");
    }

    // Set up palette INI path
    int ini_len = snprintf(palette_system.palette_ini_path, sizeof(palette_system.palette_ini_path), "%s%cpalettes.ini",
                           palette_system.user_palette_dir, PATH_SEP);
    if (ini_len < 0 || (size_t)ini_len >= sizeof(palette_system.palette_ini_path)) {
        C64_LOG_WARNING("Palette INI path too long, truncated");
    }

    // Add Default palette first (always present, uses hardcoded colors)
    memcpy(palette_system.palettes[0].colors, default_palette_colors, sizeof(default_palette_colors));
    strncpy(palette_system.palettes[0].id, "Default", sizeof(palette_system.palettes[0].id) - 1);
    strncpy(palette_system.palettes[0].name, "Default", sizeof(palette_system.palettes[0].name) - 1);
    palette_system.palettes[0].path[0] = '\0'; // No file for default
    palette_system.palettes[0].is_shipped = true;
    palette_system.palettes[0].colors_loaded = true;
    palette_system.palette_count = 1;

    // Discover shipped palettes
    discover_shipped_palettes();

    // Load user palette references from INI
    load_palette_ini();

    // Sort palettes (Default first, then alphabetically)
    sort_palettes();

    // Select Default palette
    c64_palette_select("Default");

    palette_initialized = true;
    C64_LOG_INFO("🎨 Palette system initialized with %d palettes", palette_system.palette_count);

    return true;
}

void c64_palette_cleanup(void)
{
    if (!palette_initialized) {
        return;
    }

    palette_initialized = false;
    memset(&palette_system, 0, sizeof(palette_system));
    C64_LOG_INFO("🎨 Palette system cleaned up");
}

struct c64_palette_system *c64_palette_get_system(void)
{
    if (!palette_initialized) {
        return NULL;
    }
    return &palette_system;
}

void c64_palette_populate_list(obs_property_t *palette_prop)
{
    if (!palette_prop) {
        return;
    }

    obs_property_list_clear(palette_prop);

    for (int i = 0; i < palette_system.palette_count; i++) {
        obs_property_list_add_string(palette_prop, palette_system.palettes[i].name, palette_system.palettes[i].id);
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
        C64_LOG_WARNING("Palette not found: %s", palette_id);
        return false;
    }

    // Load colors if not already loaded
    struct c64_palette_entry *entry = &palette_system.palettes[index];
    if (!entry->colors_loaded && entry->path[0]) {
        char name_buf[C64_PALETTE_NAME_MAX];
        char desc_buf[256];
        if (c64_palette_parse_vpl(entry->path, entry->colors, name_buf, sizeof(name_buf), desc_buf, sizeof(desc_buf))) {
            entry->colors_loaded = true;
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
            C64_LOG_WARNING("Failed to load palette: %s", entry->path);
            return false;
        }
    }

    palette_system.active_palette_index = index;

    // Copy colors to working buffer
    memcpy(palette_system.working_colors, entry->colors, sizeof(entry->colors));
    palette_system.working_modified = false;

    // Rebuild LUT with new palette
    c64_palette_rebuild_lut(palette_system.working_colors);

    C64_LOG_INFO("🎨 Selected palette: %s", entry->name);
    return true;
}

const char *c64_palette_get_active_id(void)
{
    if (palette_system.active_palette_index >= 0 &&
        palette_system.active_palette_index < palette_system.palette_count) {
        return palette_system.palettes[palette_system.active_palette_index].id;
    }
    return "Default";
}

uint32_t *c64_palette_get_working_colors(void)
{
    return palette_system.working_colors;
}

bool c64_palette_set_working_color(int index, uint32_t bgra_color)
{
    if (index < 0 || index >= C64_PALETTE_COLORS) {
        return false;
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

    C64_LOG_INFO("🎨 Reverted to saved palette: %s", entry->name);
}

bool c64_palette_save(void)
{
    if (palette_system.active_palette_index < 0) {
        return false;
    }

    struct c64_palette_entry *entry = &palette_system.palettes[palette_system.active_palette_index];

    // Cannot overwrite shipped palettes
    if (entry->is_shipped) {
        C64_LOG_INFO("Cannot overwrite shipped palette: %s (use Save As)", entry->name);
        return false;
    }

    // Write to VPL file
    if (!c64_palette_write_vpl(entry->path, palette_system.working_colors, entry->name)) {
        C64_LOG_WARNING("Failed to save palette: %s", entry->path);
        return false;
    }

    // Update stored colors
    memcpy(entry->colors, palette_system.working_colors, sizeof(entry->colors));
    palette_system.working_modified = false;

    C64_LOG_INFO("🎨 Saved palette: %s", entry->name);
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
        C64_LOG_WARNING("Failed to save palette as: %s", path);
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
            C64_LOG_WARNING("Failed to add palette entry: %s", id);
            return false;
        }
        // Load colors for the new entry
        int new_index = palette_system.palette_count - 1;
        memcpy(palette_system.palettes[new_index].colors, palette_system.working_colors,
               sizeof(palette_system.palettes[new_index].colors));
        palette_system.palettes[new_index].colors_loaded = true;
    }

    // Save palette INI
    save_palette_ini();

    // Sort and select the new palette
    sort_palettes();
    c64_palette_select(id);

    C64_LOG_INFO("🎨 Saved palette as: %s", name);
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
        C64_LOG_WARNING("Failed to parse VPL file: %s", path);
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
        C64_LOG_WARNING("Destination path too long for palette import");
        return false;
    }

    // Extract ID
    char id[C64_PALETTE_NAME_MAX];
    strncpy(id, src_filename, sizeof(id) - 1);
    id[sizeof(id) - 1] = '\0';
    char *ext = strrchr(id, '.');
    if (ext && strcasecmp(ext, ".vpl") == 0) {
        *ext = '\0';
    }

    // If name is empty, use ID
    if (!name[0]) {
        strncpy(name, id, sizeof(name) - 1);
    }

    // Write the file to user directory
    if (!c64_palette_write_vpl(dest_path, colors, name)) {
        C64_LOG_WARNING("Failed to copy palette to user directory: %s", dest_path);
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

    // Save palette INI and sort
    save_palette_ini();
    sort_palettes();

    // Select the loaded palette
    c64_palette_select(id);

    C64_LOG_INFO("🎨 Loaded palette from file: %s", name);
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
        C64_LOG_WARNING("Failed to open VPL file: %s", path);
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
            // The vic_colors use BGRA format: 0xFFBBGGRR
            colors[color_count] = 0xFF000000 | (b << 16) | (g << 8) | r;
            color_count++;
        }
    }

    fclose(file);

    if (color_count != C64_PALETTE_COLORS) {
        C64_LOG_WARNING("VPL file has %d colors, expected %d: %s", color_count, C64_PALETTE_COLORS, path);
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
    if (!path || !colors) {
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

    FILE *file = fopen(path, "w");
    if (!file) {
        C64_LOG_WARNING("Failed to create VPL file: %s", path);
        return false;
    }

    // Write name as comment
    if (name && name[0]) {
        fprintf(file, "# %s\n", name);
    }

    // Write colors in VPL format (RR GG BB)
    static const char *color_names[C64_PALETTE_COLORS] = {"Black",    "White",       "Red",        "Cyan",
                                                          "Purple",   "Green",       "Blue",       "Yellow",
                                                          "Orange",   "Brown",       "Pink",       "Dark Grey",
                                                          "Med Grey", "Light Green", "Light Blue", "Light Grey"};

    for (int i = 0; i < C64_PALETTE_COLORS; i++) {
        uint32_t bgra = colors[i];
        // Extract BGR from BGRA (little-endian format)
        uint8_t b = (bgra >> 16) & 0xFF;
        uint8_t g = (bgra >> 8) & 0xFF;
        uint8_t r = bgra & 0xFF;

        fprintf(file, "%02X %02X %02X  # %d: %s\n", r, g, b, i, color_names[i]);
    }

    fclose(file);
    return true;
}

bool c64_palette_get_user_dir(char *path, size_t path_size)
{
    if (!path || path_size < 64) {
        return false;
    }

    // Try OBS module config path first
    char *config_path = obs_module_config_path("palettes");
    if (config_path) {
        strncpy(path, config_path, path_size - 1);
        path[path_size - 1] = '\0';
        bfree(config_path);
    } else {
        // Fall back to Documents folder
        char documents[256];
        if (!c64_get_user_documents_path(documents, sizeof(documents))) {
            C64_LOG_WARNING("Failed to get user documents path");
            return false;
        }
#ifdef _WIN32
        snprintf(path, path_size, "%s\\obs-studio\\c64stream\\palettes", documents);
#else
        snprintf(path, path_size, "%s/obs-studio/c64stream/palettes", documents);
#endif
    }

    // Create directory if it doesn't exist
    if (!c64_create_directory_recursive(path)) {
        C64_LOG_WARNING("Failed to create user palette directory: %s", path);
        return false;
    }

    return true;
}

void c64_palette_rebuild_lut(const uint32_t *colors)
{
    if (!colors) {
        return;
    }

    // Access the color_pair_lut through the c64-color module
    // We need to rebuild it with the new palette colors
    extern uint64_t c64_color_pair_lut[256];
    extern uint32_t c64_current_palette[16];
    extern bool c64_color_lut_initialized;

    // Update the current palette
    memcpy(c64_current_palette, colors, sizeof(c64_current_palette));

    // Rebuild the LUT
    for (int i = 0; i < 256; i++) {
        uint8_t color1 = i & 0x0F;
        uint8_t color2 = (i >> 4) & 0x0F;
        uint64_t packed = ((uint64_t)colors[color2] << 32) | colors[color1];
        c64_color_pair_lut[i] = packed;
    }

    c64_color_lut_initialized = true;
    C64_LOG_DEBUG("🎨 Color LUT rebuilt with custom palette");
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
        C64_LOG_WARNING("Shipped palettes directory not found");
        return;
    }

    C64_LOG_INFO("Discovering shipped palettes from: %s", palettes_path);

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

                // Skip "Default" - we add it manually
                if (strcmp(id, "Default") == 0) {
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

            // Skip "Default" - we add it manually
            if (strcmp(id, "Default") == 0) {
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

static void load_palette_ini(void)
{
    FILE *file = fopen(palette_system.palette_ini_path, "r");
    if (!file) {
        // No INI file yet, that's fine
        return;
    }

    C64_LOG_INFO("Loading user palettes from: %s", palette_system.palette_ini_path);

    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        // Remove trailing newline
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }

        // Skip comments and empty lines
        char *trimmed = trim_whitespace(line);
        if (!trimmed || !*trimmed || *trimmed == '#' || *trimmed == ';') {
            continue;
        }

        // Skip section headers
        if (*trimmed == '[') {
            continue;
        }

        // Parse id=path format
        char *equals = strchr(trimmed, '=');
        if (!equals) {
            continue;
        }

        *equals = '\0';
        char *id = trim_whitespace(trimmed);
        char *path = trim_whitespace(equals + 1);

        if (!id || !*id || !path || !*path) {
            continue;
        }

        // Check if palette already exists
        bool found = false;
        for (int i = 0; i < palette_system.palette_count; i++) {
            if (strcmp(palette_system.palettes[i].id, id) == 0) {
                found = true;
                break;
            }
        }

        if (!found) {
            // Try to get display name from file
            char name[C64_PALETTE_NAME_MAX];
            char desc[256];
            uint32_t temp_colors[C64_PALETTE_COLORS];
            if (c64_palette_parse_vpl(path, temp_colors, name, sizeof(name), desc, sizeof(desc))) {
                if (!name[0]) {
                    strncpy(name, id, sizeof(name) - 1);
                }
                add_palette_entry(id, name, path, false);
                // Store description if we got one
                if (desc[0] && palette_system.palette_count > 0) {
                    int idx = palette_system.palette_count - 1;
                    strncpy(palette_system.palettes[idx].desc, desc, sizeof(palette_system.palettes[idx].desc) - 1);
                    palette_system.palettes[idx].desc[sizeof(palette_system.palettes[idx].desc) - 1] = '\0';
                }
            }
        }
    }

    fclose(file);
}

static void save_palette_ini(void)
{
    FILE *file = fopen(palette_system.palette_ini_path, "w");
    if (!file) {
        C64_LOG_WARNING("Failed to save palette INI: %s", palette_system.palette_ini_path);
        return;
    }

    fprintf(file, "# C64 Stream User Palettes\n");
    fprintf(file, "# Format: palette_id=path_to_vpl_file\n\n");
    fprintf(file, "[palettes]\n");

    for (int i = 0; i < palette_system.palette_count; i++) {
        // Only save user palettes (not shipped)
        if (!palette_system.palettes[i].is_shipped && palette_system.palettes[i].path[0]) {
            fprintf(file, "%s=%s\n", palette_system.palettes[i].id, palette_system.palettes[i].path);
        }
    }

    fclose(file);
}

static bool add_palette_entry(const char *id, const char *name, const char *path, bool is_shipped)
{
    if (palette_system.palette_count >= C64_MAX_PALETTES) {
        C64_LOG_WARNING("Maximum palette count reached");
        return false;
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
