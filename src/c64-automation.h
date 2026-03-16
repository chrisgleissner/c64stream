/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct obs_source obs_source_t;

/**
 * Automation engine for SID/PRG/D64 playback
 * Handles single files and folder batch mode with shuffle and duration control
 */

typedef struct c64_automation c64_automation_t;

typedef enum { C64_AUTO_MODE_OFF, C64_AUTO_MODE_SINGLE, C64_AUTO_MODE_FOLDER } c64_automation_mode_t;

typedef enum {
    C64_FILE_TYPE_SID,
    C64_FILE_TYPE_MOD,
    C64_FILE_TYPE_PRG,
    C64_FILE_TYPE_CRT,
    C64_FILE_TYPE_D64,
    C64_FILE_TYPE_G64,
    C64_FILE_TYPE_D71,
    C64_FILE_TYPE_G71,
    C64_FILE_TYPE_D81
} c64_file_type_t;

typedef enum { C64_FILE_SOURCE_LOCAL, C64_FILE_SOURCE_C64U } c64_file_source_t;

#ifndef C64_AUTOMATION_PATH_MAX
#define C64_AUTOMATION_PATH_MAX 4096
#endif

/**
 * Automation configuration
 */
typedef struct {
    c64_automation_mode_t mode;
    c64_file_source_t file_source;
    char folder_path[C64_AUTOMATION_PATH_MAX];
    char songlengths_path[C64_AUTOMATION_PATH_MAX];
    bool shuffle;
    bool include_subfolders;
    int duration_seconds;
    bool reset_between_items;
    bool use_songlengths;
    char d64_autostart_template[256]; // e.g. "LOAD\"*\",8,1\rRUN\r"
} c64_automation_config_t;

/**
 * Create automation engine
 * @param rest_client REST client for C64 control
 * @param keyboard Keyboard controller for D64 autostart commands
 * @return Automation instance or NULL on error
 */
c64_automation_t *c64_automation_create(void *rest_client, void *keyboard, obs_source_t *source);

/**
 * Destroy automation engine
 */
void c64_automation_destroy(c64_automation_t *automation);

/**
 * Configure automation
 */
void c64_automation_configure(c64_automation_t *automation, const c64_automation_config_t *config);

/**
 * Update runtime-only automation settings while running (e.g., duration).
 */
void c64_automation_update_runtime_config(c64_automation_t *automation, const c64_automation_config_t *config);

/**
 * Start automation
 * @return true if started successfully
 */
bool c64_automation_start(c64_automation_t *automation, int start_index);

/**
 * Stop automation immediately
 */
void c64_automation_stop(c64_automation_t *automation);

/**
 * Check if automation is running
 */
bool c64_automation_is_running(c64_automation_t *automation);

/**
 * Get current status
 * @return Status string (e.g. "idle", "playing 3/10", "stopped")
 */
const char *c64_automation_get_status(c64_automation_t *automation);

/**
 * Get currently playing file path
 * @return Full path to current file or NULL if not playing
 */
const char *c64_automation_get_current_file(c64_automation_t *automation);

/**
 * Get current index in playlist
 * @return Current index (0-based), or -1 if not running
 */
int c64_automation_get_current_index(c64_automation_t *automation);

/**
 * Get songlengths database path if loaded (may trigger discovery)
 * @return Fully qualified path or NULL if unavailable
 */
const char *c64_automation_get_songlengths_path(c64_automation_t *automation);

/**
 * Get songlength in seconds for a file path
 * @return true if songlength is available
 */
bool c64_automation_get_songlength_seconds(c64_automation_t *automation, const char *path, double *out_seconds);

/**
 * Skip to next item in playlist
 * @return true if skipped successfully
 */
bool c64_automation_skip_next(c64_automation_t *automation);

/**
 * Jump to specific playlist index
 * @param index Target index (0-based)
 * @return true if jumped successfully
 */
bool c64_automation_jump_to_index(c64_automation_t *automation, int index);

/**
 * Play a SID or MOD file
 * @param path Path to .sid or .mod file
 * @param song_number Song number (1-based, 0 for default). Ignored for MOD.
 * @return true if started successfully
 */
bool c64_automation_play_song(c64_automation_t *automation, const char *path, int song_number, int song_length_seconds);

/**
 * Run a PRG or CRT file
 * @param path Path to .prg or .crt file
 * @return true if started successfully
 */
bool c64_automation_run_program(c64_automation_t *automation, const char *path);

/**
 * Mount and start a disk image
 * @param path Path to .d64/.g64/.d71/.g71/.d81 file
 * @return true if started successfully
 */
bool c64_automation_run_disk(c64_automation_t *automation, const char *path);
