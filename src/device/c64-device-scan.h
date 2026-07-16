#pragma once

#include <obs-module.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct c64_source;

bool c64_device_scan_product_matches(const char *product);
bool c64_device_scan_is_ultimate_error(const char *body);
bool c64_device_scan_response_is_candidate(long status, const char *body);
size_t c64_device_scan_enumerate_subnet(uint32_t address, uint8_t prefix, uint32_t *out, size_t out_count);
/* Drives the Find Devices button: sets context->device_discovery_in_progress
 * until the background scan completes and requests a properties refresh. */
bool c64_device_scan_async(struct c64_source *context);
/* Blocking variant for script-driven discovery: runs on the calling thread
 * (already off the OBS UI thread), bounded by the same overall scan deadline
 * as c64_device_scan_async. port == 0 probes the default HTTP port (80). */
bool c64_device_scan_sync(struct c64_source *context, uint16_t port);
