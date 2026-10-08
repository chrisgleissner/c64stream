/* "Follow device" palette worker: keeps a source's colours in line with the
 * device it streams from. Palette packets in the stream take precedence; when
 * there are none, the device's Palette Definition setting is read over REST
 * every palette_poll_interval_ms and its VPL file is downloaded over FTP and
 * cached. See src/device/c64-device-palette.h. */
#pragma once

#include "device/c64-device-palette.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct c64_source;

void c64_palette_follow_start(struct c64_source *context);
/* Joins the worker, aborting a transfer in flight (returns within about
 * 50 ms). Wakes after this are harmless until c64_palette_follow_release. */
void c64_palette_follow_stop(struct c64_source *context);
/* Stops the worker if needed and frees its event. Call once no other thread
 * (receive threads included) can call into this module any more. */
void c64_palette_follow_release(struct c64_source *context);
/* Asks the worker to check now. */
void c64_palette_follow_wake(struct c64_source *context);
/* The source now streams from another device, identified by device_key (the
 * device id, else its address). Shows that device's last known palette (else
 * the default) at once and drops results of checks still running for the
 * previous device. Takes palette_mutex; does not take config_mutex. */
void c64_palette_follow_device_changed(struct c64_source *context, const char *device_key);
/* True while the stream delivers palette packets (the stream's colours win).
 * Caller holds palette_mutex. */
bool c64_palette_follow_stream_is_fresh(const struct c64_source *context);
/* Translated status line ("Video stream", "Device setting: file X", ...). */
void c64_palette_follow_describe(struct c64_source *context, char *out, size_t out_size);
/* Locale keys of the status lines (one "%s" at most: the file name). */
const char *c64_palette_follow_source_key(c64_palette_source_t source, c64_palette_error_t error);
const char *c64_palette_follow_error_key(c64_palette_error_t error);
/* Records that the stream carries palette information. Receive thread, with
 * palette_mutex held. */
void c64_palette_follow_note_stream_packet(struct c64_source *context);

/* Shows a palette taken from the stream (or keeps it for the cut-over of a
 * pending device switch) and files it under the current device, so a switch
 * back to that device shows it at once. Caller holds palette_mutex. */
void c64_palette_follow_show_stream_palette(struct c64_source *context, const uint32_t colors[16]);
/* The first video packet from the selected device after a switch: its
 * colours replace the previous device's now. Video processing thread; takes
 * palette_mutex only when a switch is pending. */
void c64_palette_follow_cutover(struct c64_source *context);

/* Test-only: device service ports, cache folder, and one synchronous check. */
void c64_palette_follow_set_ports_for_test(uint16_t rest_port, uint16_t ftp_port);
void c64_palette_follow_set_cache_dir_for_test(const char *dir);
/* Rewrites the configured host into the address and ports of a fake device
 * (several fakes share 127.0.0.1 on different ports). */
typedef void (*c64_palette_follow_target_hook_t)(char *host, size_t host_size, uint16_t *rest_port, uint16_t *ftp_port);
void c64_palette_follow_set_target_hook_for_test(c64_palette_follow_target_hook_t hook);
void c64_palette_follow_poll_for_test(struct c64_source *context);
