#pragma once

#include "c64-device.h"

#include <obs-module.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct c64_source;

bool c64_device_scan_product_matches(const char *product);
bool c64_device_scan_is_ultimate_error(const char *body);
bool c64_device_scan_response_is_candidate(long status, const char *body);
size_t c64_device_scan_enumerate_subnet(uint32_t address, uint8_t prefix, uint32_t *out, size_t out_count);
/* Sweep order of a local interface address (network byte order): 0 for the
 * interface carrying the default route, 1 for RFC 1918 private networks, 2 for
 * anything else and 3 for link-local. Lower ranks are enumerated first. */
int c64_device_scan_subnet_rank(uint32_t address, uint32_t primary_address);
/* Whether address falls in the range swept for this interface (prefix clamped
 * to [24, 30] as in c64_device_scan_enumerate_subnet). The interface whose
 * range holds the configured host is swept first. */
bool c64_device_scan_same_subnet(uint32_t interface_address, uint8_t prefix, uint32_t address);
/* Worker pool size that lets host_count silent addresses each cost a full
 * probe timeout and still finish within the planned share of budget_ns. */
size_t c64_device_scan_worker_count(size_t host_count, uint64_t budget_ns);
/* Reads the hardware unique_id of the streaming-capable Ultimate at host via
 * GET /v1/info (sending X-Password when password is non-empty). Returns false
 * when the host does not answer, is not streaming-capable hardware, or reports
 * no unique_id. Blocking, bounded by timeout_ms; never call on the UI thread. */
bool c64_device_fetch_unique_id(const char *host, uint16_t port, const char *password, long timeout_ms, char *out,
                                size_t out_size);
/* A completed scan may update its starting selection only when the user did
 * not select another device while the scan was running, and either that device
 * was confirmed, or the selection is replaceable (no hardware-identified
 * profile: empty, unknown or a host-derived placeholder) and one replacement
 * was confirmed. A hardware-identified device that is merely switched off or
 * unreachable stays selected, so the source never hops to another machine. */
bool c64_device_scan_should_apply_selection(const char *selection_at_start, const char *selection_now,
                                            bool selected_device_confirmed, bool selection_replaceable,
                                            const char *sole_discovered_device_id);
/* Test-only: applies the host_index "first wins" supersession rule (see
 * apply_scan_results() in c64-device-scan.c) to a synthetic result set and
 * upserts survivors into the registry, without running a real scan. */
typedef struct {
    size_t probed;
    size_t responsive;
    uint64_t elapsed_ms;
    char first_host[C64_DEVICE_HOST_MAX]; // as enumerated, i.e. what was probed
} c64_device_scan_test_stats_t;
/* Test-only: probes exactly these hosts (single attempt each, or the
 * known-host retries when a host is already registered) and applies the
 * results to the registry, without enumerating local subnets. */
void c64_device_scan_hosts_for_test(const char *const *hosts, size_t count, uint16_t port, uint16_t control_port,
                                    c64_device_scan_test_stats_t *stats);
/* Test-only: the addresses a scan would sweep on this machine, in order. */
size_t c64_device_scan_local_hosts_for_test(char (*hosts)[C64_DEVICE_HOST_MAX], size_t max_hosts);
void c64_device_scan_apply_results_for_test(const c64_device_t *devices, const size_t *host_indices, size_t count);
/* Starts a detached local-network scan for an explicit Find Devices request.
 * The caller marks
 * context->device_discovery_in_progress before calling; completion clears it
 * and requests a properties refresh. */
bool c64_device_scan_async(struct c64_source *context);
/* Startup variant: after the initial sweep, retries only silent hosts once so
 * network interfaces that come up with OBS are still discovered. */
bool c64_device_scan_startup_async(struct c64_source *context);
/* Blocking variant for script-driven discovery: runs on the calling thread
 * (already off the OBS UI thread), bounded by the same overall scan deadline
 * as c64_device_scan_async. port == 0 probes the default HTTP port (80). */
bool c64_device_scan_sync(struct c64_source *context, uint16_t port);
