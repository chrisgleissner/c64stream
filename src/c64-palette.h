/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_PALETTE_H
#define C64_PALETTE_H

#include <obs-module.h>
#include <util/threading.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Logging prefix for palette operations
#define PALETTE_LOG_PREFIX "🎨 PALETTE:"

#ifdef __cplusplus
extern "C" {
#endif

// Maximum number of palettes (shipped + user)
#define C64_MAX_PALETTES 64
#define C64_PALETTE_COLORS 16
#define C64_PALETTE_NAME_MAX 64
#define C64_PALETTE_PATH_MAX 512

// Palette entry structure
struct c64_palette_entry {
    char id[C64_PALETTE_NAME_MAX];       // Unique identifier (filename without extension)
    char name[C64_PALETTE_NAME_MAX];     // Display name (from VPL comment or filename)
    char desc[256];                      // Description (from VPL DESC: line, for tooltip)
    char path[C64_PALETTE_PATH_MAX];     // Full path to VPL file
    bool is_shipped;                     // True if shipped with plugin, false if user palette
    uint32_t colors[C64_PALETTE_COLORS]; // BGRA colors (loaded on demand)
    bool colors_loaded;                  // True if colors have been loaded
};

// Palette system state
struct c64_palette_system {
    struct c64_palette_entry palettes[C64_MAX_PALETTES];
    int palette_count;
    int active_palette_index;                    // Index of currently selected palette
    uint32_t working_colors[C64_PALETTE_COLORS]; // In-memory working copy for editing
    bool working_modified;                       // True if working_colors differs from active palette
    char user_palette_dir[C64_PALETTE_PATH_MAX]; // User palette directory
    pthread_mutex_t mutex;                       // Protects all palette system state
};

/**
 * @brief Initialize the palette system
 *
 * Discovers shipped palettes from plugin data directory and loads user palette
 * references from the palette INI file.
 *
 * @return true if initialization succeeded, false otherwise
 */
bool c64_palette_init(void);

/**
 * @brief Clean up the palette system
 */
void c64_palette_cleanup(void);

/**
 * @brief Get the palette system state
 *
 * @return Pointer to the palette system state (NULL if not initialized)
 */
struct c64_palette_system *c64_palette_get_system(void);

/**
 * @brief Populate an OBS dropdown property with available palettes
 *
 * @param palette_prop The OBS list property to populate
 */
void c64_palette_populate_list(obs_property_t *palette_prop);

/**
 * @brief Validate that custom palette files still exist on the filesystem
 *
 * Scans all custom (non-shipped) palettes and removes entries for files that no longer exist.
 * If the currently active palette is missing, falls back to "Default".
 *
 * @param settings OBS settings to update the palette ID if fallback to Default is needed
 */
void c64_palette_validate_filesystem(obs_data_t *settings);

/**
 * @brief Select a palette by ID
 *
 * Loads the palette if not already loaded and updates the working copy.
 *
 * @param palette_id The palette ID to select
 * @return true if palette was found and selected, false otherwise
 */
bool c64_palette_select(const char *palette_id);

/**
 * @brief Get the currently active palette ID
 *
 * @return The active palette ID, or "Default" if none selected
 */
const char *c64_palette_get_active_id(void);

/**
 * @brief Get the current working colors (for editing)
 *
 * @return Pointer to 16 BGRA colors
 */
uint32_t *c64_palette_get_working_colors(void);

/**
 * @brief Set a single color in the working palette
 *
 * @param index Color index (0-15)
 * @param bgra_color Color in BGRA format
 * @return true if color was set, false if index out of range
 */
bool c64_palette_set_working_color(int index, uint32_t bgra_color);

/**
 * @brief Get palette description by ID
 *
 * @param palette_id Palette identifier
 * @return Description string, or NULL if palette not found or has no description
 */
const char *c64_palette_get_description(const char *palette_id);

/**
 * @brief Check if the working palette has unsaved modifications
 *
 * @return true if modifications exist, false otherwise
 */
bool c64_palette_has_modifications(void);

/**
 * @brief Revert working palette to the active palette's saved state
 */
void c64_palette_revert(void);

/**
 * @brief Save the working palette
 *
 * For user palettes, overwrites the VPL file. For shipped palettes,
 * this function returns false (use c64_palette_save_as instead).
 *
 * @return true if save succeeded, false otherwise
 */
bool c64_palette_save(void);

/**
 * @brief Save the working palette to a new file
 *
 * Creates a new user palette with the given name.
 *
 * @param name Display name for the new palette
 * @param path Full path to save the VPL file
 * @return true if save succeeded, false otherwise
 */
bool c64_palette_save_as(const char *name, const char *path);

/**
 * @brief Load a palette from an external VPL file
 *
 * Validates the file, adds it to the user palette list, and selects it.
 *
 * @param path Path to the VPL file
 * @return true if load succeeded, false otherwise
 */
bool c64_palette_load_from_file(const char *path);

/**
 * @brief Parse a VPL file and extract colors
 *
 * @param path Path to the VPL file
 * @param colors Output array of 16 BGRA colors
 * @param name Output buffer for display name (from NAME: line or first comment)
 * @param name_size Size of name buffer
 * @param desc Output buffer for description (from DESC: line), can be NULL
 * @param desc_size Size of desc buffer
 * @return true if parsing succeeded, false otherwise
 */
bool c64_palette_parse_vpl(const char *path, uint32_t *colors, char *name, size_t name_size, char *desc,
                           size_t desc_size);

/**
 * @brief Write colors to a VPL file
 *
 * @param path Path to the VPL file
 * @param colors Array of 16 BGRA colors
 * @param name Display name to include as comment
 * @return true if write succeeded, false otherwise
 */
bool c64_palette_write_vpl(const char *path, const uint32_t *colors, const char *name);

/**
 * @brief Get the user palette directory path
 *
 * Creates the directory if it doesn't exist.
 *
 * @param path Output buffer
 * @param path_size Size of output buffer
 * @return true if successful, false otherwise
 */
bool c64_palette_get_user_dir(char *path, size_t path_size);

/**
 * @brief Rebuild the color conversion LUT from a palette
 *
 * This function rebuilds the pixel-pair LUT used for optimized pixel conversion.
 * It should be called whenever the active palette changes.
 *
 * @param colors Array of 16 BGRA colors to use for LUT generation
 */
void c64_palette_rebuild_lut(const uint32_t *colors);

/**
 * @brief Get the active palette colors for LUT generation
 *
 * @return Pointer to 16 BGRA colors (working copy if modified, else active palette)
 */
const uint32_t *c64_palette_get_active_colors(void);

/**
 * @brief Check if a palette is a shipped preset
 *
 * @param palette_id Palette identifier
 * @return true if palette is shipped with plugin, false if user palette
 */
bool c64_palette_is_preset(const char *palette_id);

/**
 * @brief Get the display name for a palette (adds " (Preset)" suffix for shipped palettes)
 *
 * @param palette_id Palette identifier
 * @param display_name Output buffer for display name
 * @param display_name_size Size of output buffer
 * @return true if successful, false if palette not found
 */
bool c64_palette_get_display_name(const char *palette_id, char *display_name, size_t display_name_size);

/**
 * @brief Auto-save working colors if current palette is custom and has modifications
 *
 * Creates a copy if currently editing a preset palette.
 *
 * @param settings OBS settings object to update with new palette ID (if preset converted to custom), can be NULL
 * @return true if save succeeded or no save needed, false on error
 */
bool c64_palette_auto_save(obs_data_t *settings);

/**
 * @brief Delete a custom palette
 *
 * Removes the palette file and unregisters it from the system.
 * Cannot delete shipped palettes.
 *
 * @param palette_id Palette identifier
 * @return true if deletion succeeded, false if palette not found, is shipped, or delete failed
 */
bool c64_palette_delete(const char *palette_id);

#ifdef __cplusplus
}
#endif

#endif // C64_PALETTE_H
