#pragma once

#include <obs-module.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool c64_device_scan_product_matches(const char *product);
bool c64_device_scan_is_ultimate_error(const char *body);
bool c64_device_scan_response_is_candidate(long status, const char *body);
size_t c64_device_scan_enumerate_subnet(uint32_t address, uint8_t prefix, uint32_t *out, size_t out_count);
bool c64_device_scan_async(obs_source_t *source);
/* Blocking variant for script-driven discovery: runs on the calling thread
 * (already off the OBS UI thread), bounded by the same overall scan deadline
 * as c64_device_scan_async. port == 0 probes the default HTTP port (80). */
bool c64_device_scan_sync(obs_source_t *source, uint16_t port);
