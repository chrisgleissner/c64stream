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
 * REST API client for Ultimate 64 control
 * Implements the Ultimate 64 REST API specification
 */

typedef struct c64_rest_client c64_rest_client_t;

/**
 * Outcome of the most recent REST request, classified from the HTTP status code.
 *
 * Callers use this to decide how to react when a request fails. The critical
 * rule (constraint from seamless-device-transition research): a FORBIDDEN result
 * must NEVER trigger a fallback to the unauthenticated legacy transport (TCP
 * port 64), because that would bypass authentication.
 */
typedef enum {
    C64_REST_OK,            /**< 2xx — success */
    C64_REST_NOT_SUPPORTED, /**< 404, 501 — fall back to legacy where it is safe */
    C64_REST_FORBIDDEN,     /**< 401, 403 — authentication refusal. DO NOT fall back */
    C64_REST_BAD_REQUEST,   /**< 400 — our payload is invalid. Log loudly, do not fall back */
    C64_REST_UNREACHABLE,   /**< transport error (no HTTP response) — device down; fallback will also fail */
    C64_REST_SERVER_ERROR,  /**< 5xx other than 501 — surface, do not fall back */
} c64_rest_outcome_t;

/**
 * Classify an HTTP status code into a coarse outcome.
 *
 * Status 0 means no HTTP response was received (transport failure) and maps to
 * C64_REST_UNREACHABLE. 2xx maps to C64_REST_OK. 400 maps to BAD_REQUEST.
 * 401/403 map to FORBIDDEN. 404/501 map to NOT_SUPPORTED (fallback-eligible).
 *
 * Exposed so the classification is unit-testable without a network.
 */
c64_rest_outcome_t c64_rest_classify_status(long status);
long c64_rest_get_last_status(const c64_rest_client_t *client);
c64_rest_outcome_t c64_rest_get_last_outcome(const c64_rest_client_t *client);

/**
 * Create a new REST client
 * @param base_url Base URL (e.g. "http://192.168.1.64" or "http://c64u")
 * @param password Optional password for X-Password header (NULL if none)
 * @return Client instance or NULL on error
 */
c64_rest_client_t *c64_rest_client_create(const char *base_url, const char *password);

/**
 * Destroy REST client and free resources
 */
void c64_rest_client_destroy(c64_rest_client_t *client);
bool c64_rest_stream_start(c64_rest_client_t *client, bool audio, const char *destination);
bool c64_rest_stream_stop(c64_rest_client_t *client, bool audio);
bool c64_rest_stream_start_with_outcome(c64_rest_client_t *client, bool audio, const char *destination,
                                        c64_rest_outcome_t *outcome, long *status);
bool c64_rest_stream_stop_with_outcome(c64_rest_client_t *client, bool audio, c64_rest_outcome_t *outcome,
                                       long *status);
bool c64_rest_machine_input(c64_rest_client_t *client, const char *json);
bool c64_rest_machine_input_with_outcome(c64_rest_client_t *client, const char *json, c64_rest_outcome_t *outcome,
                                         long *status);
bool c64_rest_release_all(c64_rest_client_t *client);
/* input_name is a matrix input like "up"/"down"/"left"/"right"/"fire"; transition
 * is "press" or "release" (held movement, not a tap) or "tap". */
bool c64_rest_joystick_input(c64_rest_client_t *client, int port, const char *input_name, const char *transition);

/**
 * Machine reset (soft)
 * PUT /v1/machine:reset
 */
bool c64_rest_reset(c64_rest_client_t *client);

/**
 * Machine reboot (hard)
 * PUT /v1/machine:reboot
 */
bool c64_rest_reboot(c64_rest_client_t *client);

/**
 * Pause CPU execution
 * PUT /v1/machine:pause
 */
bool c64_rest_pause(c64_rest_client_t *client);

/**
 * Resume CPU execution
 * PUT /v1/machine:resume
 */
bool c64_rest_resume(c64_rest_client_t *client);

/**
 * Power off machine
 * PUT /v1/machine:poweroff
 */
bool c64_rest_poweroff(c64_rest_client_t *client);

/**
 * Toggle the on-screen configuration menu (same effect as the front-panel
 * menu button on the physical device).
 * PUT /v1/machine:menu_button
 */
bool c64_rest_menu_button(c64_rest_client_t *client);

/**
 * Read memory via DMA
 * GET /v1/machine:readmem?address=<hex>&length=<dec>
 * @param address Memory address (hex, e.g. 0x00C6)
 * @param length Number of bytes to read
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @return Number of bytes read, or -1 on error
 */
int c64_rest_read_memory(c64_rest_client_t *client, uint16_t address, size_t length, uint8_t *buffer,
                         size_t buffer_size);

/**
 * Write memory via DMA
 * PUT /v1/machine:writemem?address=<hex>&data=<hex>
 * @param address Memory address (hex, e.g. 0x0277)
 * @param data Bytes to write
 * @param length Number of bytes
 */
bool c64_rest_write_memory(c64_rest_client_t *client, uint16_t address, const uint8_t *data, size_t length);

/**
 * Play SID file
 * POST /v1/runners:sidplay?songnr=<n>
 * @param sid_data SID file content
 * @param sid_size Size of SID data
 * @param song_number Song number (1-based, 0 for default)
 */
bool c64_rest_play_sid(c64_rest_client_t *client, const uint8_t *sid_data, size_t sid_size, int song_number,
                       const uint8_t *songlengths_data, size_t songlengths_size);

/**
 * Play SID file from C64U filesystem
 * POST /v1/runners:sidplay?path=<path>&songnr=<n>
 * @param c64u_path Path to SID file on C64U filesystem
 * @param song_number Song number (1-based, 0 for default)
 */
bool c64_rest_play_sid_path(c64_rest_client_t *client, const char *c64u_path, int song_number);

/**
 * Play MOD file
 * POST /v1/runners:modplay
 * @param mod_data MOD file content
 * @param mod_size Size of MOD data
 */
bool c64_rest_play_mod(c64_rest_client_t *client, const uint8_t *mod_data, size_t mod_size);

/**
 * Play MOD file from C64U filesystem
 * POST /v1/runners:modplay?path=<path>
 * @param c64u_path Path to MOD file on C64U filesystem
 */
bool c64_rest_play_mod_path(c64_rest_client_t *client, const char *c64u_path);

/**
 * Run PRG file
 * POST /v1/runners:run_prg
 * @param prg_data PRG file content
 * @param prg_size Size of PRG data
 */
bool c64_rest_run_prg(c64_rest_client_t *client, const uint8_t *prg_data, size_t prg_size);

/**
 * Run PRG file from C64U filesystem
 * POST /v1/runners:run_prg?path=<path>
 * @param c64u_path Path to PRG file on C64U filesystem
 */
bool c64_rest_run_prg_path(c64_rest_client_t *client, const char *c64u_path);

/**
 * Run CRT file
 * POST /v1/runners:run_crt
 * @param crt_data CRT file content
 * @param crt_size Size of CRT data
 */
bool c64_rest_run_crt(c64_rest_client_t *client, const uint8_t *crt_data, size_t crt_size);

/**
 * Run CRT file from C64U filesystem
 * POST /v1/runners:run_crt?path=<path>
 * @param c64u_path Path to CRT file on C64U filesystem
 */
bool c64_rest_run_crt_path(c64_rest_client_t *client, const char *c64u_path);

/**
 * Mount disk image
 * POST /v1/drives/{drive}:mount?type=<type>&mode=<mode>
 * @param drive Drive letter ('a', 'b', etc.)
 * @param type Disk type ("d64", "d81", etc.)
 * @param mode Mount mode ("readonly", "readwrite")
 * @param disk_data Disk image content
 * @param disk_size Size of disk data
 */
bool c64_rest_mount_disk(c64_rest_client_t *client, char drive, const char *type, const char *mode,
                         const uint8_t *disk_data, size_t disk_size);

/**
 * Mount disk image from C64U filesystem
 * POST /v1/drives/{drive}:mount?path=<path>
 * @param drive Drive letter ('a', 'b', etc.)
 * @param c64u_path Path to disk image on C64U filesystem
 */
bool c64_rest_mount_disk_path(c64_rest_client_t *client, char drive, const char *c64u_path);

/**
 * Get drive property value from /v1/drives
 */
bool c64_rest_drive_get_property(c64_rest_client_t *client, const char *drive, const char *property, char *out_value,
                                 size_t out_size);

/**
 * Mount an image from the C64U filesystem
 * PUT /v1/drives/{drive}:mount?image=<path>&type=<type>&mode=<mode>
 */
bool c64_rest_drive_mount_image(c64_rest_client_t *client, const char *drive, const char *c64u_path, const char *type,
                                const char *mode);

/**
 * Mount an uploaded image
 * POST /v1/drives/{drive}:mount?type=<type>&mode=<mode>
 */
bool c64_rest_drive_mount_upload(c64_rest_client_t *client, const char *drive, const char *type, const char *mode,
                                 const uint8_t *data, size_t size);

/**
 * Unmount drive image
 * PUT /v1/drives/{drive}:remove
 */
bool c64_rest_drive_unmount(c64_rest_client_t *client, const char *drive);

/**
 * Reset drive
 * PUT /v1/drives/{drive}:reset
 */
bool c64_rest_drive_reset(c64_rest_client_t *client, const char *drive);

/**
 * Turn drive on
 * PUT /v1/drives/{drive}:on
 */
bool c64_rest_drive_on(c64_rest_client_t *client, const char *drive);

/**
 * Turn drive off
 * PUT /v1/drives/{drive}:off
 */
bool c64_rest_drive_off(c64_rest_client_t *client, const char *drive);

/**
 * Load ROM from C64U filesystem
 * PUT /v1/drives/{drive}:load_rom?file=<path>
 */
bool c64_rest_drive_load_rom_image(c64_rest_client_t *client, const char *drive, const char *c64u_path);

/**
 * Load uploaded ROM
 * POST /v1/drives/{drive}:load_rom
 */
bool c64_rest_drive_load_rom_upload(c64_rest_client_t *client, const char *drive, const uint8_t *data, size_t size);

/**
 * Set drive mode
 * PUT /v1/drives/{drive}:set_mode
 */
bool c64_rest_drive_set_mode(c64_rest_client_t *client, const char *drive, const char *mode);

/**
 * Read config item value
 * GET /v1/configs/{category}/{item}
 */
bool c64_rest_config_get_value(c64_rest_client_t *client, const char *category, const char *item, char *out_value,
                               size_t out_size);

/**
 * Set config item value
 * PUT /v1/configs/{category}/{item}?value=<value>
 */
bool c64_rest_config_set_value(c64_rest_client_t *client, const char *category, const char *item, const char *value);

/**
 * List config categories or items in category
 * GET /v1/configs or /v1/configs/{category}
 */
bool c64_rest_config_list(c64_rest_client_t *client, const char *category, char ***items, size_t *count);

/**
 * List config options for a specific item
 * GET /v1/configs/{category}/{item}
 */
bool c64_rest_config_list_options(c64_rest_client_t *client, const char *category, const char *item, char ***options,
                                  size_t *count);

/**
 * Save configuration to flash
 * PUT /v1/configs:save_to_flash
 */
bool c64_rest_config_save(c64_rest_client_t *client);

/**
 * Load configuration from flash
 * PUT /v1/configs:load_from_flash
 */
bool c64_rest_config_load(c64_rest_client_t *client);

/**
 * Reset configuration to defaults
 * PUT /v1/configs:reset_to_default
 */
bool c64_rest_config_reset(c64_rest_client_t *client);

/**
 * Free a list returned by config list helpers.
 */
void c64_rest_string_list_free(char **items, size_t count);

/**
 * Get last error message
 * @return Error string (valid until next operation)
 */
const char *c64_rest_get_error(c64_rest_client_t *client);

/**
 * Get the raw HTTP status code of the most recent request.
 * @return HTTP status code, or 0 if no response was received (transport error)
 */
long c64_rest_get_last_status(const c64_rest_client_t *client);

/**
 * Get the classified outcome of the most recent request.
 *
 * Reflects the result of the last call through the client. Read this after a
 * request returns false to decide whether to fall back, surface an auth error,
 * or retry. See c64_rest_outcome_t for the reaction each value implies.
 * @return Outcome of the last request (C64_REST_UNREACHABLE if none yet)
 */
c64_rest_outcome_t c64_rest_get_last_outcome(const c64_rest_client_t *client);

/**
 * File entry from C64U filesystem
 */
typedef struct {
    char name[256];
    bool is_directory;
    uint32_t size; // File size in bytes (0 for directories)
} c64_file_entry_t;

/**
 * List files in C64U filesystem directory
 * GET /v1/files:list?path=<path>&recursive=<bool>
 * @param client REST client instance
 * @param path Directory path (e.g., "/Commodore/SID")
 * @param recursive Whether to enumerate subdirectories
 * @param entries Output array pointer (allocated by function, caller must free)
 * @param entry_count Output count of entries
 * @return true on success, false on error
 */
bool c64_rest_list_files(c64_rest_client_t *client, const char *path, bool recursive, c64_file_entry_t **entries,
                         size_t *entry_count);

/**
 * Check if a path exists in C64U filesystem
 * HEAD /v1/files:stat?path=<path>
 * @param client REST client instance
 * @param path Path to check
 * @param is_directory Output: true if path is a directory, false if file
 * @return true if path exists, false otherwise
 */
bool c64_rest_stat_file(c64_rest_client_t *client, const char *path, bool *is_directory);
