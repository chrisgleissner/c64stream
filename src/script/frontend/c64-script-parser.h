/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Script automation script parser
 * Parses .c64script files with commands for effects, palettes, playback, timing
 */

typedef enum {
    C64_SCRIPT_CMD_EFFECT,       // effect <preset_name>
    C64_SCRIPT_CMD_EFFECT_PARAM, // effect_param <name> <value>
    C64_SCRIPT_CMD_PALETTE,      // palette <palette_name>
    C64_SCRIPT_CMD_PLAY_SID,     // play_sid <path> [songnr <N>]
    C64_SCRIPT_CMD_RUN_PRG,      // run_prg <path>
    C64_SCRIPT_CMD_MOUNT_DISK,   // mount_disk <path>
    C64_SCRIPT_CMD_AUTOSTART,    // autostart
    C64_SCRIPT_CMD_RESET,        // reset
    C64_SCRIPT_CMD_REBOOT,       // reboot
    C64_SCRIPT_CMD_WAIT,         // wait <duration>
    C64_SCRIPT_CMD_STOP,         // stop
    C64_SCRIPT_CMD_LOOP,         // loop [count]
    C64_SCRIPT_CMD_LABEL,        // label <name>
    C64_SCRIPT_CMD_GOTO,         // goto <name>
} c64_script_command_type_t;

typedef enum {
    C64_SCRIPT_PATH_LOCAL, // Local filesystem path
    C64_SCRIPT_PATH_C64U,  // C64U filesystem path (c64u: prefix)
} c64_script_path_type_t;

typedef struct {
    c64_script_command_type_t type;
    int line_number;
    char arg1[512];       // Main argument (path, name, etc.)
    char arg2[256];       // Secondary argument (param name, value, etc.)
    uint32_t duration_ms; // For WAIT command
    int loop_count;       // For LOOP command (0 = infinite)
    c64_script_path_type_t path_type;
    int song_number; // For PLAY_SID command
} c64_script_command_t;

typedef struct {
    c64_script_command_t *commands;
    size_t num_commands;
    size_t capacity;
    char error_msg[512];
    int error_line;
} c64_script_t;

/**
 * Parse script from file
 * @param file_path Path to .c64script file
 * @return Parsed script or NULL on error
 */
c64_script_t *c64_script_parse_file(const char *file_path);

/**
 * Parse script from string
 * @param content Script content
 * @return Parsed script or NULL on error
 */
c64_script_t *c64_script_parse_string(const char *content);

/**
 * Free script
 */
void c64_script_free(c64_script_t *script);

/**
 * Get number of commands in script
 */
size_t c64_script_get_command_count(c64_script_t *script);

/**
 * Get error message from last parse operation
 * @return Error string (valid until next parse or NULL if no error)
 */
const char *c64_script_get_error(c64_script_t *script);

/**
 * Get error line number (0 if no error)
 */
int c64_script_get_error_line(c64_script_t *script);
