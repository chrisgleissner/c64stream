/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#pragma once

#include <stdbool.h>
#include <stddef.h>

/**
 * Automation engine for SID/PRG/D64 playback
 * Handles single files and folder batch mode with shuffle and duration control
 */

typedef struct c64_automation c64_automation_t;

typedef enum { C64_AUTO_MODE_OFF, C64_AUTO_MODE_SINGLE, C64_AUTO_MODE_FOLDER } c64_automation_mode_t;

typedef enum { C64_FILE_TYPE_SID, C64_FILE_TYPE_PRG, C64_FILE_TYPE_D64 } c64_file_type_t;

typedef enum { C64_FILE_SOURCE_LOCAL, C64_FILE_SOURCE_C64U } c64_file_source_t;

/**
 * Automation configuration
 */
typedef struct {
    c64_automation_mode_t mode;
    c64_file_source_t file_source;
    char folder_path[512];
    bool shuffle;
    int duration_seconds;
    bool reset_between_items;
    char d64_autostart_template[256]; // e.g. "LOAD\"*\",8,1\rRUN\r"
} c64_automation_config_t;

/**
 * Create automation engine
 * @param rest_client REST client for C64 control
 * @param keyboard Keyboard controller for D64 autostart commands
 * @return Automation instance or NULL on error
 */
c64_automation_t *c64_automation_create(void *rest_client, void *keyboard);

/**
 * Destroy automation engine
 */
void c64_automation_destroy(c64_automation_t *automation);

/**
 * Configure automation
 */
void c64_automation_configure(c64_automation_t *automation, const c64_automation_config_t *config);

/**
 * Start automation
 * @return true if started successfully
 */
bool c64_automation_start(c64_automation_t *automation);

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
 * Play single SID file
 * @param path Path to .sid file
 * @param song_number Song number (1-based, 0 for default)
 * @return true if started successfully
 */
bool c64_automation_play_sid(c64_automation_t *automation, const char *path, int song_number);

/**
 * Run single PRG file
 * @param path Path to .prg file
 * @return true if started successfully
 */
bool c64_automation_run_prg(c64_automation_t *automation, const char *path);

/**
 * Mount and start single D64 file
 * @param path Path to .d64 file
 * @return true if started successfully
 */
bool c64_automation_start_d64(c64_automation_t *automation, const char *path);
