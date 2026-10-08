/* Follow device palette: where the colours of the "Follow device" palette come
 * from, and how the device's configured palette is read when the stream does
 * not carry it.
 *
 * Two sources, in order of preference:
 *  1. Palette packets in the video stream (firmware that supports them, video
 *     started over REST with palette=1). They report the palette the device is
 *     actually rendering with, including changes no setting reflects.
 *  2. The device setting "U64 Specific Settings / Palette Definition", read
 *     over REST. It names a VPL file in /flash/data (downloaded over FTP and
 *     cached), or is empty for the firmware's built-in palette.
 *
 * The pure helpers here are unit-tested on every platform; the fetch helpers
 * do blocking network I/O and run on the source's palette worker thread. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define C64_DEVICE_PALETTE_NAME_MAX 64
#define C64_DEVICE_PALETTE_DETAIL_MAX 128
/* Firmware limit for a VPL file (FileTypePalette::parseVplFile). */
#define C64_DEVICE_PALETTE_VPL_MAX 8192
/* Default and allowed range of the device palette check interval. */
#define C64_DEVICE_PALETTE_POLL_DEFAULT_MS 1000
#define C64_DEVICE_PALETTE_POLL_MIN_MS 250
#define C64_DEVICE_PALETTE_POLL_MAX_MS 10000

typedef enum {
    C64_PALETTE_SOURCE_OFF = 0,        // Follow device is not selected
    C64_PALETTE_SOURCE_WAITING,        // Selected; nothing read from the device yet
    C64_PALETTE_SOURCE_STREAM,         // Palette packets arrive in the video stream
    C64_PALETTE_SOURCE_DEVICE_FILE,    // Device setting names a VPL file
    C64_PALETTE_SOURCE_DEVICE_DEFAULT, // Device setting is empty: firmware built-in palette
    C64_PALETTE_SOURCE_NEEDS_REST,     // Stream control without REST: default palette
    C64_PALETTE_SOURCE_ERROR,          // Setting or file could not be read (see c64_palette_error_t)
} c64_palette_source_t;

/* Why the device palette could not be read. Each has a translated message. */
typedef enum {
    C64_PALETTE_ERROR_NONE = 0,
    C64_PALETTE_ERROR_UNREACHABLE,     // REST did not answer
    C64_PALETTE_ERROR_PASSWORD,        // REST or FTP rejected the network password
    C64_PALETTE_ERROR_NO_SETTING,      // firmware has no Palette Definition setting
    C64_PALETTE_ERROR_FILE_NOT_FOUND,  // the configured file is not in /flash/data
    C64_PALETTE_ERROR_FTP_UNREACHABLE, // FTP did not answer (service off?)
    C64_PALETTE_ERROR_INVALID_FILE,    // not a palette file the firmware accepts (or > 8 KB)
    C64_PALETTE_ERROR_BAD_NAME,        // file name that cannot be used safely
    C64_PALETTE_ERROR_CACHE,           // local cache folder unavailable
} c64_palette_error_t;

/* Clamps a configured check interval into the allowed range (0 = default). */
uint32_t c64_device_palette_clamp_interval(long long interval_ms);

/* Extracts the "current" value of the Palette Definition item from a
 * GET /v1/configs/<category>/<item> response. An empty value is valid (the
 * built-in palette). Returns false when the response has no such value. */
bool c64_device_palette_parse_setting(const char *json, char *name, size_t name_size);

/* A palette file name is used in an FTP path and a local cache path, so only
 * plain file names are accepted: no directories, no "..", no control
 * characters, at most C64_DEVICE_PALETTE_NAME_MAX - 1 bytes. */
bool c64_device_palette_name_is_safe(const char *name);

/* Parses VPL text exactly as the firmware does: 8..8192 bytes, text after '#'
 * ignored, each non-empty line read as three hex values, parsing stops at the
 * first other non-empty line, and exactly 16 colours are required. Colours are
 * returned in the plugin's 0xFFBBGGRR layout. */
bool c64_device_palette_parse_vpl(const char *text, size_t length, uint32_t colors[16]);

/* Cache file for a downloaded palette: <dir>/<device>-<size>-<name>. The size
 * is part of the key so a file that is overwritten under the same name is
 * downloaded again. Returns false for unsafe names or a too-small buffer. */
bool c64_device_palette_cache_path(char *out, size_t out_size, const char *dir, const char *device_key,
                                   const char *name, uint64_t size);

/* English one-line description, for the OBS log (the UI uses translations). */
void c64_device_palette_describe(c64_palette_source_t source, c64_palette_error_t error, const char *file, char *out,
                                 size_t out_size);
const char *c64_device_palette_error_text(c64_palette_error_t error);

/* Network helpers (blocking, bounded by timeout_ms; never on the UI thread).
 * A request aborts within 50 ms once *cancel becomes true (cancel may be NULL). The
 * REST host may carry a port ("host:port"). */
bool c64_device_palette_fetch_setting(const char *host, const char *password, long timeout_ms,
                                      const volatile bool *cancel, char *name, size_t name_size,
                                      c64_palette_error_t *error);
bool c64_device_palette_fetch_size(const char *host, const char *password, const char *name, long timeout_ms,
                                   const volatile bool *cancel, uint64_t *size, c64_palette_error_t *error);
/* Downloads /flash/data/<name> over FTP. The device accepts any user name and
 * checks only the network password. */
bool c64_device_palette_download(const char *host, uint16_t ftp_port, const char *password, const char *name,
                                 long timeout_ms, const volatile bool *cancel, char *buffer, size_t buffer_size,
                                 size_t *length, c64_palette_error_t *error);
