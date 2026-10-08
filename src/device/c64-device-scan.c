#include "c64-device-scan.h"
#include "c64-device.h"
#include "c64-logging.h"
#include "c64-network.h"
#include "c64-types.h"

#include <curl/curl.h>
#include <ctype.h>
#ifndef _WIN32
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#endif
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util/platform.h>

// Worker pool bounds. The pool grows with the number of enumerated addresses so
// that a machine with several active interfaces (Docker/libvirt bridges,
// Hyper-V vEthernet adapters, VPNs) still sweeps every subnet before the
// overall deadline instead of silently dropping the tail of the host list.
#define C64_SCAN_MIN_WORKERS 48
#define C64_SCAN_MAX_WORKERS 128
// Share of the overall deadline the sweep may plan to use; the remainder
// absorbs control-port connects and the known-host retries.
#define C64_SCAN_BUDGET_PERCENT 60
#define C64_SCAN_MAX_HOSTS 1024
#define C64_SCAN_MAX_RESULTS 64
#define C64_SCAN_TIMEOUT_MS 650L
#define C64_SCAN_OVERALL_TIMEOUT_NS (12ULL * 1000000000ULL)
#define C64_SCAN_STARTUP_RETRY_DELAY_MS 1000
#define C64_SCAN_STARTUP_RETRY_TIMEOUT_NS (4ULL * 1000000000ULL)
#define C64_SCAN_DEFAULT_PORT 80
// Reachability attempts for an already-registered address before giving up on
// it. Keeps a saved multi-homed device from flip-flopping when one interface
// times out under scan load. Subnet-discovery hosts always get a single shot.
#define C64_SCAN_KNOWN_HOST_ATTEMPTS 3
#define C64_SCAN_KNOWN_HOST_BACKOFF_MS 250

typedef struct {
    char data[2048];
    size_t used;
} response_t;

// A single host's successful match: the address answered /v1/info, is
// streaming-capable hardware, and accepts control commands on its port.
// host_index is the candidate's position in job->hosts, which is what makes
// "first responsive address wins" deterministic -- see apply_scan_results().
typedef struct {
    c64_device_t device;
    size_t host_index;
} scan_result_t;

typedef struct {
    // Full host length, not just dotted-quad: configured and registered hosts
    // may be names such as "Ultimate-64-Elite-F83C87.fritz.box".
    char hosts[C64_SCAN_MAX_HOSTS][C64_DEVICE_HOST_MAX];
    size_t count;
    size_t next;
    // Number of leading hosts that are already-registered / configured
    // ("known"). They are enumerated first (see build_scan_job) and probed in a
    // quiet first phase, before the subnet flood, so a slow interface on a
    // saved device is measured without contention -- see scan_main.
    size_t known_count;
    // Upper bound for the current worker pass; lets scan_main run the known
    // hosts and the subnet sweep as two separate phases over one worker pool.
    size_t phase_end;
    bool retry_unmatched_only;
    bool startup_retry;
    // Sized from the host count when the job is started; see
    // c64_device_scan_worker_count().
    size_t worker_count;
    // Diagnostics for the completion log line: how many addresses were
    // actually probed, and whether the deadline cut the sweep short.
    size_t probed_count;
    size_t subnet_count;
    bool deadline_reached;
    pthread_mutex_t mutex;
    uint64_t deadline_ns;
    obs_source_t *source;
    struct c64_source *context; // Nullable; set only when scanning drives the Scan button's label.
    uint16_t port;
    uint16_t control_port;
    // Loopback is never a discovery target in normal use.  It is useful only
    // when the source itself explicitly targets a loopback address (for local
    // development and the hermetic E2E C64U mock), in which case sweeping its
    // bounded /24 lets a multi-address mock behave like real hardware.
    bool include_loopback_subnet;
    // Candidate matches, applied to the registry only after every worker has
    // finished (see scan_main). One physical device routinely answers at more
    // than one address in the same pass -- a multi-homed unit (Ethernet and
    // Wi-Fi) reports the same unique_id on each, and a stale DHCP lease can
    // still be answered alongside the current one. Collecting first, then
    // resolving by host_index, keeps the choice independent of which worker
    // happens to finish last.
    scan_result_t results[C64_SCAN_MAX_RESULTS];
    size_t result_count;
    // Snapshot of the source selection at scan start. The registry may update
    // a selected device's address without changing its ID, so completion needs
    // to know whether that selection was actually confirmed this pass.
    char selected_device_id[C64_DEVICE_ID_MAX];
    // Host actually in use at scan start. A fresh source (or one still on a
    // legacy migrated id) has no confirmed device id yet, so id-based
    // confirmation never matches even when this scan re-observes the exact
    // host it is already talking to. Falling back to a host match lets that
    // profile -- including any peer_host this pass discovers -- attach to the
    // active source instead of only ever landing in the registry unused.
    char selected_host[C64_DEVICE_HOST_MAX];
} scan_job_t;

// Outlives scan_job_t (freed before the UI-thread completion task runs).
typedef struct {
    obs_source_t *source;
    struct c64_source *context;
    // A device this scan confirms belongs to the active source: either the
    // sole device this pass discovered, or one whose host matches the host
    // already in use (see scan_job_t.selected_host). A UI scan may activate
    // it when the source has no deliberate selection.
    char auto_select_device_id[C64_DEVICE_ID_MAX];
    // Do not apply a finished scan to a device the user selected while that
    // scan was running.
    char selected_device_id[C64_DEVICE_ID_MAX];
    bool selected_device_confirmed;
    // Last publish of this scan; only it clears the Find Devices label.
    bool final;
} scan_completion_t;

static void scan_add_local_subnets(scan_job_t *job);
static void scan_run_phase(scan_job_t *job);

static void scan_add_host(scan_job_t *job, const char *host)
{
    if (!job || !host || !host[0] || job->count >= C64_SCAN_MAX_HOSTS) {
        return;
    }
    for (size_t i = 0; i < job->count; i++) {
        if (!strcmp(job->hosts[i], host)) {
            return;
        }
    }
    snprintf(job->hosts[job->count++], sizeof(job->hosts[0]), "%s", host);
}

static size_t scan_write(void *data, size_t size, size_t nmemb, void *opaque)
{
    response_t *response = opaque;
    const size_t bytes = size * nmemb;
    if (!response) {
        return 0;
    }
    // Keep the head of an oversized body instead of failing the transfer: the
    // identity fields come first in /v1/info, and a firmware that adds fields
    // must not make every device undiscoverable.
    const size_t room = sizeof(response->data) - 1 - response->used;
    const size_t kept = bytes < room ? bytes : room;
    memcpy(response->data + response->used, data, kept);
    response->used += kept;
    response->data[response->used] = '\0';
    return bytes;
}

bool c64_device_scan_product_matches(const char *product)
{
    if (!product) {
        return false;
    }
    char lower[128];
    size_t i = 0;
    for (; product[i] && i + 1 < sizeof(lower); i++) {
        lower[i] = (char)tolower((unsigned char)product[i]);
    }
    lower[i] = '\0';
    // Streaming-capable hardware only: the "Ultimate 64" family (Ultimate 64,
    // Ultimate 64 Elite, Ultimate 64-II) and "C64 Ultimate". Deliberately
    // excludes the "Ultimate II" family (Ultimate II/II+/II+L) -- those are
    // disk/cartridge-only add-ons with no video/audio streaming hardware.
    return strstr(lower, "ultimate 64") || strstr(lower, "c64 ultimate") || !strcmp(lower, "c64u");
}

static const char *scan_json_array_end(const char *p)
{
    if (!p || *p != '[') {
        return NULL;
    }

    int depth = 0;
    bool in_string = false;
    for (; *p; p++) {
        if (in_string) {
            if (*p == '\\' && p[1]) {
                p++;
            } else if (*p == '"') {
                in_string = false;
            }
            continue;
        }
        if (*p == '"') {
            in_string = true;
        } else if (*p == '[') {
            depth++;
        } else if (*p == ']') {
            depth--;
            if (depth == 0) {
                return p + 1;
            }
        }
    }
    return NULL;
}

bool c64_device_scan_is_ultimate_error(const char *body)
{
    if (!body) {
        return false;
    }

    const char *p = body;
    while (isspace((unsigned char)*p)) {
        p++;
    }
    if (*p++ != '{') {
        return false;
    }

    for (;;) {
        while (isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '}') {
            return false;
        }
        if (*p++ != '"') {
            return false;
        }

        const char *key = p;
        bool escaped = false;
        while (*p && (*p != '"' || escaped)) {
            escaped = (*p == '\\' && !escaped);
            if (*p != '\\') {
                escaped = false;
            }
            p++;
        }
        if (*p != '"') {
            return false;
        }
        const size_t key_len = (size_t)(p - key);
        const bool is_errors = key_len == strlen("errors") && !strncmp(key, "errors", key_len);
        p++;
        while (isspace((unsigned char)*p)) {
            p++;
        }
        if (*p++ != ':') {
            return false;
        }
        while (isspace((unsigned char)*p)) {
            p++;
        }
        if (is_errors) {
            const char *end = scan_json_array_end(p);
            while (end && isspace((unsigned char)*end)) {
                end++;
            }
            if (!end || *end++ != '}') {
                return false;
            }
            while (isspace((unsigned char)*end)) {
                end++;
            }
            return *end == '\0';
        }

        bool in_string = false;
        int depth = 0;
        for (; *p; p++) {
            if (in_string) {
                if (*p == '\\' && p[1]) {
                    p++;
                } else if (*p == '"') {
                    in_string = false;
                }
                continue;
            }
            if (*p == '"') {
                in_string = true;
            } else if (*p == '[' || *p == '{') {
                depth++;
            } else if (*p == ']' || *p == '}') {
                if (depth == 0) {
                    break;
                }
                depth--;
            } else if (*p == ',' && depth == 0) {
                break;
            }
        }
        if (!*p) {
            return false;
        }
        if (*p == '}') {
            return false;
        }
        p++;
    }
}

bool c64_device_scan_response_is_candidate(long status, const char *body)
{
    return status == 401 || (status == 403 && c64_device_scan_is_ultimate_error(body));
}

bool c64_device_scan_should_apply_selection(const char *selection_at_start, const char *selection_now,
                                            bool selected_device_confirmed, bool selection_replaceable,
                                            const char *sole_discovered_device_id)
{
    return selection_at_start && selection_now && !strcmp(selection_at_start, selection_now) &&
           (selected_device_confirmed ||
            (selection_replaceable && sole_discovered_device_id && sole_discovered_device_id[0]));
}

size_t c64_device_scan_enumerate_subnet(uint32_t address, uint8_t prefix, uint32_t *out, size_t out_count)
{
    if (!out || !out_count) {
        return 0;
    }
    if (prefix < 24) {
        prefix = 24;
    } else if (prefix > 30) {
        prefix = 30;
    }
    const uint32_t host_mask = (1u << (32 - prefix)) - 1u;
    const uint32_t network = ntohl(address) & ~host_mask;
    const uint32_t own = ntohl(address);
    size_t count = 0;
    for (uint32_t host = network + 1; host < network + host_mask && count < out_count; host++) {
        if (host != own) {
            out[count++] = htonl(host);
        }
    }
    return count;
}

static bool extract_json_string(const char *json, const char *key, char *out, size_t out_size)
{
    if (!json || !key || !out || !out_size) {
        return false;
    }
    char needle[96];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *value = strstr(json, needle);
    if (!value || !(value = strchr(value + strlen(needle), ':'))) {
        return false;
    }
    value++;
    while (*value == ' ' || *value == '\t') {
        value++;
    }
    if (*value++ != '\"') {
        return false;
    }
    const char *end = strchr(value, '\"');
    if (!end) {
        return false;
    }
    size_t length = (size_t)(end - value);
    if (length >= out_size) {
        length = out_size - 1;
    }
    memcpy(out, value, length);
    out[length] = '\0';
    return true;
}

bool c64_device_fetch_unique_id(const char *host, uint16_t port, const char *password, long timeout_ms, char *out,
                                size_t out_size)
{
    if (!host || !host[0] || !out || !out_size) {
        return false;
    }
    out[0] = '\0';
    CURL *curl = curl_easy_init();
    if (!curl) {
        return false;
    }
    response_t body = {0};
    char url[C64_DEVICE_HOST_MAX + 32];
    snprintf(url, sizeof(url), "http://%s:%u/v1/info", host, port ? port : C64_SCAN_DEFAULT_PORT);
    struct curl_slist *headers = NULL;
    if (password && password[0]) {
        char header[300];
        snprintf(header, sizeof(header), "X-Password: %s", password);
        headers = curl_slist_append(headers, header);
    }
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, scan_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }
    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    char product[64] = {0};
    if (code != CURLE_OK || status != 200 || !extract_json_string(body.data, "product", product, sizeof(product)) ||
        !c64_device_scan_product_matches(product)) {
        return false;
    }
    return extract_json_string(body.data, "unique_id", out, out_size) && out[0];
}

typedef enum {
    SCAN_PROBE_MATCH,       // Streaming-capable device that answers REST + control port; device filled.
    SCAN_PROBE_NOT_DEVICE,  // Answered /v1/info but is not streaming-capable hardware.
    SCAN_PROBE_NO_RESPONSE, // No usable answer this attempt (may just be transient load; retryable).
} scan_probe_result_t;

// Single reachability probe of one address: /v1/info (REST) plus a control-port
// connect. Fills `device` and reports `password_required` only on MATCH.
static scan_probe_result_t scan_probe_host(const char *host, uint16_t port, uint16_t control_port, c64_device_t *device,
                                           bool *password_required)
{
    response_t body = {0};
    CURL *curl = curl_easy_init();
    if (!curl) {
        return SCAN_PROBE_NO_RESPONSE;
    }
    char url[C64_DEVICE_HOST_MAX + 32];
    snprintf(url, sizeof(url), "http://%s:%u/v1/info", host, port);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, C64_SCAN_TIMEOUT_MS);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, C64_SCAN_TIMEOUT_MS);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, scan_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    *password_required = c64_device_scan_response_is_candidate(status, body.data);
    if (code != CURLE_OK || (status != 200 && !*password_required)) {
        return SCAN_PROBE_NO_RESPONSE;
    }
    char product[64] = {0};
    if (status == 200 && (!extract_json_string(body.data, "product", product, sizeof(product)) ||
                          !c64_device_scan_product_matches(product))) {
        return SCAN_PROBE_NOT_DEVICE;
    }

    memset(device, 0, sizeof(*device));
    char unique_id[64] = {0};
    extract_json_string(body.data, "unique_id", unique_id, sizeof(unique_id));
    if (!c64_device_id_from_host(device->id, sizeof(device->id), unique_id, host)) {
        return SCAN_PROBE_NO_RESPONSE;
    }
    if (!extract_json_string(body.data, "hostname", device->name, sizeof(device->name))) {
        snprintf(device->name, sizeof(device->name), "%s", product[0] ? product : host);
    }
    // The same physical device can be discovered at more than one address
    // (e.g. Ethernet and Wi-Fi); show the host -- and whether it needs a
    // password -- right in the dropdown label so entries stay distinguishable.
    // Baked into the editable name (not synthesized at display time) so the
    // user can trim it via Device name + Save if they don't want it.
    {
        const size_t used = strlen(device->name);
        snprintf(device->name + used, sizeof(device->name) - used, " (%s%s)", host,
                 *password_required ? ", Password" : "");
    }
    snprintf(device->host, sizeof(device->host), "%s", host);
    device->video_port = 11000;
    device->audio_port = 11001;
    device->control_port = control_port ? control_port : 64;

    // An address qualifies when it answers *both* control channels: /v1/info
    // (REST) above, and the control port here. Stream start/stop rides the
    // control port whenever the legacy transport is selected or REST is
    // demoted, so an address that answers REST alone cannot actually drive a
    // stream. A bare connect/close is non-destructive -- verified against live
    // hardware not to disturb a running stream -- so it runs for every
    // candidate, including the active device.
    //
    // This deliberately replaced an earlier "start a throwaway video stream and
    // wait for a packet" probe. That probe was destructive (it repointed the
    // device's single video destination and then stopped it, blacking out live
    // output) and unusable for the multi-homed devices it was meant to serve:
    // every interface of one unit shares that single destination, so probing
    // two addresses of the same device concurrently tore down each other's
    // streams. REST + control-port reachability is the property that actually
    // decides whether an address can drive a stream, and it composes cleanly
    // across interfaces.
    if (!c64_test_connectivity(host, device->control_port)) {
        return SCAN_PROBE_NO_RESPONSE;
    }
    return SCAN_PROBE_MATCH;
}

static void scan_one_host(scan_job_t *job, const char *host, size_t host_index)
{
    // An address already on file for some device is retried before being given
    // up on. Under the 48-worker fan-out a slower interface (Wi-Fi on a
    // multi-homed unit) intermittently times out /v1/info or the control-port
    // connect even though it is up; a single such miss must not drop a saved
    // address and let a sibling address of the same device replace it, which is
    // what made a multi-homed device flip between its interfaces every scan.
    // Newly-discovered subnet hosts get a single attempt: there are ~254 of
    // them, retrying every silent one would burn the scan deadline, and a
    // genuinely new device simply shows up on the next scan.
    const bool known_host = c64_device_registry_find_by_host(host) != NULL;
    const int attempts = known_host ? C64_SCAN_KNOWN_HOST_ATTEMPTS : 1;

    c64_device_t device = {0};
    bool password_required = false;
    scan_probe_result_t result = SCAN_PROBE_NO_RESPONSE;
    for (int attempt = 0; attempt < attempts && os_gettime_ns() < job->deadline_ns; attempt++) {
        // Space retries so the set straddles a brief latency spike rather than
        // firing three times inside the same jitter window. Only between
        // attempts, and only for the few known hosts, so it costs no time on
        // the ~254 single-shot discovery hosts.
        if (attempt > 0) {
            os_sleep_ms(C64_SCAN_KNOWN_HOST_BACKOFF_MS);
        }
        result = scan_probe_host(host, job->port, job->control_port, &device, &password_required);
        if (result != SCAN_PROBE_NO_RESPONSE) {
            break;
        }
    }

    if (result == SCAN_PROBE_NOT_DEVICE) {
        // Answered /v1/info but is not streaming-capable hardware (e.g. an
        // Ultimate II family unit). If this host was previously registered
        // under an earlier, looser product filter, prune it now: only
        // streaming-capable devices belong in the list. This is a positive
        // identification, unlike a NO_RESPONSE, so acting on it is safe.
        const c64_device_t *stale = c64_device_registry_find_by_host(host);
        if (stale) {
            c64_device_registry_delete(stale->id);
        }
        return;
    }
    if (result != SCAN_PROBE_MATCH) {
        return;
    }

    pthread_mutex_lock(&job->mutex);
    if (job->result_count < C64_SCAN_MAX_RESULTS) {
        job->results[job->result_count].device = device;
        job->results[job->result_count].host_index = host_index;
        job->result_count++;
    }
    pthread_mutex_unlock(&job->mutex);
}

static void *scan_worker(void *opaque)
{
    scan_job_t *job = opaque;
    for (;;) {
        pthread_mutex_lock(&job->mutex);
        const size_t index = job->next < job->phase_end ? job->next++ : job->phase_end;
        bool already_matched = false;
        if (job->retry_unmatched_only && index < job->phase_end) {
            for (size_t i = 0; i < job->result_count; i++) {
                if (job->results[i].host_index == index) {
                    already_matched = true;
                    break;
                }
            }
        }
        pthread_mutex_unlock(&job->mutex);
        if (index >= job->phase_end) {
            return NULL;
        }
        if (os_gettime_ns() >= job->deadline_ns) {
            pthread_mutex_lock(&job->mutex);
            job->deadline_reached = true;
            pthread_mutex_unlock(&job->mutex);
            return NULL;
        }
        if (already_matched) {
            continue;
        }
        scan_one_host(job, job->hosts[index], index);
        pthread_mutex_lock(&job->mutex);
        job->probed_count++;
        pthread_mutex_unlock(&job->mutex);
    }
}

static void scan_complete_on_ui(void *opaque)
{
    scan_completion_t *completion = opaque;
    if (completion->context && completion->final) {
        completion->context->device_discovery_in_progress = false;
    }

    /* Discovery used to stop after adding an entry to the dropdown. A new
     * source still had its migrated `c64u` placeholder selected, however, so
     * the retry worker kept trying that hostname instead of the C64U it had
     * just proved reachable. Activate an unambiguous result when the selected
     * device did not answer this scan. A selected device that did answer keeps
     * its selection even when other devices are found. */
    if (completion->context) {
        obs_data_t *settings = obs_source_get_settings(completion->source);
        if (settings) {
            const char *selected = obs_data_get_string(settings, "c64_device");
            const c64_device_t *profile = selected && selected[0] ? c64_device_registry_get(selected) : NULL;
            const bool replaceable = !profile || !c64_device_profile_is_identified(profile);
            if (!completion->selected_device_confirmed && !replaceable && completion->auto_select_device_id[0]) {
                C64_LOG_INFO("DEVICE: keeping selected device '%s' although it did not answer discovery", selected);
            }
            if (c64_device_scan_should_apply_selection(completion->selected_device_id, selected,
                                                       completion->selected_device_confirmed, replaceable,
                                                       completion->auto_select_device_id)) {
                if (!completion->selected_device_confirmed) {
                    C64_LOG_INFO("DEVICE: replacing unconfirmed device '%s' with discovered device '%s'", selected,
                                 completion->auto_select_device_id);
                    obs_data_set_string(settings, "c64_device", completion->auto_select_device_id);
                } else {
                    /* A DHCP/interface change preserves the physical device's
                     * unique ID. Reapply its freshly scanned profile even
                     * though the ID did not change, otherwise c64_update
                     * leaves c64_host pointed at the old address forever. */
                    C64_LOG_INFO("DEVICE: refreshing confirmed device '%s' from discovery", selected);
                }

                c64_device_registry_apply_selected(settings);
                obs_source_update(completion->source, settings);
            }
            obs_data_release(settings);
        }
    }
    obs_source_update_properties(completion->source);
    obs_source_release(completion->source);
    free(completion);
}

// Applies collected scan results to the registry, one entry per physical
// device: the first address that answered both REST and the control port wins.
// A multi-homed unit reports the same unique_id on each interface, so all its
// addresses share one device id and collapse to a single registry entry here.
//
// "First" is the lowest position in job->hosts, never thread completion order.
// build_scan_job() enumerates already-registered hosts first, then the
// configured host, then the local subnets in ascending address order; scan_main
// probes that known-host prefix in a quiet first phase and retries it (see
// scan_one_host). Together that gives the rule two properties worth having:
//
//   - A device already on file keeps the address it is on file with: that
//     address is enumerated first and probed without the subnet flood
//     competing, so a slower interface is not dropped for a load-induced
//     timeout and the entry does not flip-flop between interfaces every scan.
//   - A device that genuinely moved is still picked up: its old address no
//     longer answers (even retried), so it produces no result, and the new
//     address is the only -- hence first -- candidate for that id.
static void apply_scan_results(scan_job_t *job, char *auto_select_device_id, size_t auto_select_device_id_size,
                               bool *selected_device_confirmed)
{
    size_t applied_count = 0;
    char sole_device_id[C64_DEVICE_ID_MAX] = {0};
    char host_matched_device_id[C64_DEVICE_ID_MAX] = {0};
    for (size_t i = 0; i < job->result_count; i++) {
        bool superseded = false;
        for (size_t j = 0; j < job->result_count; j++) {
            if (j != i && !strcmp(job->results[j].device.id, job->results[i].device.id) &&
                job->results[j].host_index < job->results[i].host_index) {
                superseded = true;
                break;
            }
        }
        // Any address of a device counts, not only the one that wins below: a
        // source configured with a multi-homed unit's second (e.g. Wi-Fi)
        // address is still talking to that physical device.
        if (job->selected_host[0] && !strcmp(job->selected_host, job->results[i].device.host)) {
            snprintf(host_matched_device_id, sizeof(host_matched_device_id), "%s", job->results[i].device.id);
        }
        if (!superseded) {
            if (selected_device_confirmed && job->selected_device_id[0] &&
                !strcmp(job->selected_device_id, job->results[i].device.id)) {
                *selected_device_confirmed = true;
            }
            c64_device_t device = job->results[i].device;
            for (size_t j = 0; j < job->result_count; j++) {
                if (j != i && !strcmp(job->results[j].device.id, device.id) &&
                    strcmp(job->results[j].device.host, device.host) != 0) {
                    snprintf(device.peer_host, sizeof(device.peer_host), "%s", job->results[j].device.host);
                    break;
                }
            }
            if (c64_device_registry_upsert_discovered(&device)) {
                applied_count++;
                snprintf(sole_device_id, sizeof(sole_device_id), "%s", job->results[i].device.id);
            }
        }
    }
    if (auto_select_device_id && auto_select_device_id_size) {
        // A host match is a stronger signal than "only one device answered":
        // it proves this scan re-observed the exact address already in use,
        // even alongside other devices on the network -- see selected_host.
        if (host_matched_device_id[0]) {
            snprintf(auto_select_device_id, auto_select_device_id_size, "%s", host_matched_device_id);
        } else if (applied_count == 1) {
            snprintf(auto_select_device_id, auto_select_device_id_size, "%s", sole_device_id);
        }
    }
}

void c64_device_scan_hosts_for_test(const char *const *hosts, size_t count, uint16_t port, uint16_t control_port,
                                    c64_device_scan_test_stats_t *stats)
{
    scan_job_t *job = calloc(1, sizeof(*job));
    if (!job) {
        return;
    }
    job->port = port;
    job->control_port = control_port;
    for (size_t i = 0; i < count; i++) {
        scan_add_host(job, hosts[i]);
    }
    job->known_count = 0;
    job->worker_count = c64_device_scan_worker_count(job->count, C64_SCAN_OVERALL_TIMEOUT_NS);
    pthread_mutex_init(&job->mutex, NULL);
    job->deadline_ns = os_gettime_ns() + C64_SCAN_OVERALL_TIMEOUT_NS;
    const uint64_t started_ns = os_gettime_ns();
    job->next = 0;
    job->phase_end = job->count;
    scan_run_phase(job);
    if (stats) {
        stats->probed = job->probed_count;
        stats->responsive = job->result_count;
        stats->elapsed_ms = (os_gettime_ns() - started_ns) / 1000000ULL;
        snprintf(stats->first_host, sizeof(stats->first_host), "%s", job->count ? job->hosts[0] : "");
    }
    apply_scan_results(job, NULL, 0, NULL);
    pthread_mutex_destroy(&job->mutex);
    free(job);
}

size_t c64_device_scan_local_hosts_for_test(char (*hosts)[C64_DEVICE_HOST_MAX], size_t max_hosts)
{
    scan_job_t *job = calloc(1, sizeof(*job));
    if (!job) {
        return 0;
    }
    scan_add_local_subnets(job);
    const size_t count = job->count < max_hosts ? job->count : max_hosts;
    for (size_t i = 0; i < count; i++) {
        snprintf(hosts[i], C64_DEVICE_HOST_MAX, "%s", job->hosts[i]);
    }
    free(job);
    return count;
}

// Test-only entry point for the host_index "first wins" supersession rule in
// apply_scan_results(), without spinning up a real scan (see
// tests/network/test_c64_device_scan.c).
void c64_device_scan_apply_results_for_test(const c64_device_t *devices, const size_t *host_indices, size_t count)
{
    scan_job_t job = {0};
    for (size_t i = 0; i < count && i < C64_SCAN_MAX_RESULTS; i++) {
        job.results[i].device = devices[i];
        job.results[i].host_index = host_indices[i];
        job.result_count++;
    }
    apply_scan_results(&job, NULL, 0, NULL);
}

// Runs the worker pool over the host range [job->next, job->phase_end).
size_t c64_device_scan_worker_count(size_t host_count, uint64_t budget_ns)
{
    const uint64_t planned_ns = budget_ns / 100 * C64_SCAN_BUDGET_PERCENT;
    if (!planned_ns) {
        return C64_SCAN_MAX_WORKERS;
    }
    // Each silent address costs one full probe timeout on its worker.
    const uint64_t cost_ns = (uint64_t)host_count * (uint64_t)C64_SCAN_TIMEOUT_MS * 1000000ULL;
    const uint64_t needed = (cost_ns + planned_ns - 1) / planned_ns;
    if (needed < C64_SCAN_MIN_WORKERS) {
        return C64_SCAN_MIN_WORKERS;
    }
    return needed > C64_SCAN_MAX_WORKERS ? C64_SCAN_MAX_WORKERS : (size_t)needed;
}

static void scan_run_phase(scan_job_t *job)
{
    pthread_t workers[C64_SCAN_MAX_WORKERS];
    size_t worker_count = 0;
    size_t requested = job->worker_count ? job->worker_count : C64_SCAN_MIN_WORKERS;
    const size_t phase_hosts = job->phase_end > job->next ? job->phase_end - job->next : 0;
    if (requested > phase_hosts) {
        requested = phase_hosts;
    }
    for (size_t i = 0; i < requested && i < C64_SCAN_MAX_WORKERS; i++) {
        if (pthread_create(&workers[worker_count], NULL, scan_worker, job) != 0) {
            break;
        }
        worker_count++;
    }
    if (!worker_count && phase_hosts) {
        // Thread creation can fail under resource pressure. Probe on this
        // thread instead: slower, but a scan must never report "no devices"
        // without having looked.
        C64_LOG_WARNING("DEVICE: could not start discovery workers; probing sequentially");
        scan_worker(job);
    }
    for (size_t i = 0; i < worker_count; i++) {
        pthread_join(workers[i], NULL);
    }
}

// Applies the results collected so far and hands them to the UI thread. A
// non-final publish leaves the Find Devices label alone and keeps the job's own
// source reference for the passes still to come.
static void scan_publish(scan_job_t *job, uint64_t started_ns, bool final)
{
    char auto_select_device_id[C64_DEVICE_ID_MAX] = {0};
    bool selected_device_confirmed = false;
    apply_scan_results(job, auto_select_device_id, sizeof(auto_select_device_id), &selected_device_confirmed);
    // One summary line per pass: the evidence needed to diagnose "no device
    // found" reports from platforms and networks we cannot reproduce.
    C64_LOG_INFO("DEVICE: %s probed %zu of %zu addresses (%zu known, %zu local networks, %zu workers) "
                 "in %llu ms; %zu responsive addresses",
                 job->retry_unmatched_only ? "discovery retry" : "discovery", job->probed_count, job->count,
                 job->known_count, job->subnet_count, job->worker_count,
                 (unsigned long long)((os_gettime_ns() - started_ns) / 1000000ULL), job->result_count);
    if (job->deadline_reached && !job->retry_unmatched_only) {
        C64_LOG_WARNING("DEVICE: discovery deadline reached before every address was probed; "
                        "enter the device address in C64U Host if it was not found");
    }
    obs_source_t *source = job->source;
    if (source && !final) {
        source = obs_source_get_ref(source);
    }
    scan_completion_t *completion = source ? calloc(1, sizeof(*completion)) : NULL;
    if (completion) {
        completion->source = source;
        completion->context = job->context;
        completion->final = final;
        snprintf(completion->auto_select_device_id, sizeof(completion->auto_select_device_id), "%s",
                 auto_select_device_id);
        snprintf(completion->selected_device_id, sizeof(completion->selected_device_id), "%s", job->selected_device_id);
        completion->selected_device_confirmed = selected_device_confirmed;
        obs_queue_task(OBS_TASK_UI, scan_complete_on_ui, completion, false);
        return;
    }
    if (source && source != job->source) {
        obs_source_release(source);
    }
    if (final) {
        // No UI completion will run (no source ref, or the allocation failed).
        // Clear the flag here instead, or the Find Devices button stays stuck
        // on its "Discovering..." label for the rest of the session.
        if (job->context) {
            job->context->device_discovery_in_progress = false;
        }
        if (job->source) {
            obs_source_release(job->source);
        }
    }
}

static void *scan_main(void *opaque)
{
    scan_job_t *job = opaque;
    const uint64_t started_ns = os_gettime_ns();
    // Phase 1: probe the already-known hosts before the subnet flood starts, so
    // a saved device's slower interface (Wi-Fi on a multi-homed unit) is
    // measured while the network is quiet and does not lose its address to a
    // load-induced timeout. Phase 2: sweep the rest of the enumerated hosts for
    // newly-appeared devices.
    job->next = 0;
    job->phase_end = job->known_count;
    scan_run_phase(job);
    job->next = job->known_count;
    job->phase_end = job->count;
    scan_run_phase(job);
    if (job->startup_retry) {
        // Publish the first sweep now: a source waiting for an unambiguous
        // device must not also wait for the retry pass below.
        scan_publish(job, started_ns, false);
        const size_t first_pass_results = job->result_count;
        /* OBS can create a source before a DHCP route or USB Ethernet adapter
         * is ready. Give only hosts that were silent in the initial sweep one
         * later chance; responsive hosts are skipped, and the bounded retry
         * remains entirely off the OBS/UI thread. */
        C64_LOG_DEBUG("DEVICE: refreshing interfaces and retrying silent startup discovery hosts after %d ms",
                      C64_SCAN_STARTUP_RETRY_DELAY_MS);
        os_sleep_ms(C64_SCAN_STARTUP_RETRY_DELAY_MS);
        job->deadline_ns = os_gettime_ns() + C64_SCAN_STARTUP_RETRY_TIMEOUT_NS;
        job->retry_unmatched_only = true;
        job->probed_count = 0;
        job->deadline_reached = false;
        scan_add_local_subnets(job);
        job->worker_count = c64_device_scan_worker_count(job->count, C64_SCAN_STARTUP_RETRY_TIMEOUT_NS);
        job->next = 0;
        job->phase_end = job->known_count;
        scan_run_phase(job);
        job->next = job->known_count;
        job->phase_end = job->count;
        scan_run_phase(job);
        if (job->result_count == first_pass_results) {
            // Nothing new: the first publish already applied everything.
            C64_LOG_DEBUG("DEVICE: discovery retry found no additional addresses");
            pthread_mutex_destroy(&job->mutex);
            if (job->context) {
                job->context->device_discovery_in_progress = false;
            }
            if (job->source) {
                obs_source_release(job->source);
            }
            free(job);
            return NULL;
        }
    }
    pthread_mutex_destroy(&job->mutex);
    scan_publish(job, started_ns, true);
    free(job);
    return NULL;
}

// One local IPv4 interface address whose subnet is a sweep candidate.
typedef struct {
    uint32_t address; // network byte order
    uint8_t prefix;
} scan_subnet_t;

#define C64_SCAN_MAX_SUBNETS 32

// Local address the OS would use for off-link traffic, i.e. the interface that
// carries the default route. connect() on a UDP socket only performs the route
// lookup; no packet is sent. The target is TEST-NET-1 (RFC 5737), which is
// never assigned, so this cannot reach or depend on any real host.
static bool scan_primary_ipv4(uint32_t *out)
{
    socket_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET_VALUE) {
        return false;
    }
    struct sockaddr_in target = {0};
    target.sin_family = AF_INET;
    target.sin_port = htons(9);
    target.sin_addr.s_addr = htonl(0xC0000201u); // 192.0.2.1
    struct sockaddr_in local = {0};
    socklen_t local_len = sizeof(local);
    const bool ok = connect(sock, (struct sockaddr *)&target, sizeof(target)) == 0 &&
                    getsockname(sock, (struct sockaddr *)&local, &local_len) == 0 && local.sin_addr.s_addr != 0;
    close(sock);
    if (ok) {
        *out = local.sin_addr.s_addr;
    }
    return ok;
}

bool c64_device_scan_same_subnet(uint32_t interface_address, uint8_t prefix, uint32_t address)
{
    // Same clamping as c64_device_scan_enumerate_subnet: the swept range.
    if (prefix < 24) {
        prefix = 24;
    } else if (prefix > 30) {
        prefix = 30;
    }
    const uint32_t mask = ~((1u << (32 - prefix)) - 1u);
    return (ntohl(interface_address) & mask) == (ntohl(address) & mask);
}

int c64_device_scan_subnet_rank(uint32_t address, uint32_t primary_address)
{
    if (primary_address && address == primary_address) {
        return 0;
    }
    const uint32_t host = ntohl(address);
    // RFC 1918 private ranges: where home and studio LANs live.
    const bool private_lan = (host >> 24) == 10 || (host >> 20) == ((172u << 4) | 1u) ||
                             (host >> 16) == ((192u << 8) | 168u);
    // Link-local (169.254/16) last: only reachable devices without DHCP or
    // static configuration live there.
    const bool link_local = (host >> 16) == ((169u << 8) | 254u);
    return private_lan ? 1 : (link_local ? 3 : 2);
}

static void scan_add_subnet_hosts(scan_job_t *job, uint32_t address, uint8_t prefix)
{
    uint32_t addresses[254];
    const size_t count = c64_device_scan_enumerate_subnet(address, prefix, addresses, 254);
    for (size_t i = 0; i < count && job->count < C64_SCAN_MAX_HOSTS; i++) {
        char host[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &addresses[i], host, sizeof(host))) {
            scan_add_host(job, host);
        }
    }
}

#ifndef _WIN32
static bool scan_interface_should_enumerate(const struct ifaddrs *entry, bool include_loopback)
{
    return entry && entry->ifa_name && entry->ifa_addr && entry->ifa_addr->sa_family == AF_INET &&
           (entry->ifa_flags & IFF_UP) && (include_loopback || !(entry->ifa_flags & IFF_LOOPBACK)) &&
           !(entry->ifa_flags & IFF_POINTOPOINT);
}

static size_t scan_collect_subnets(bool include_loopback, scan_subnet_t *out, size_t out_count)
{
    struct ifaddrs *interfaces = NULL;
    if (getifaddrs(&interfaces) != 0) {
        C64_LOG_DEBUG("DEVICE: getifaddrs failed while enumerating local subnets");
        return 0;
    }
    // This uses interface flags rather than names, so it works with renamed
    // adapters and on Darwin-family systems (macOS/iOS) as well as Linux.
    size_t count = 0;
    for (struct ifaddrs *entry = interfaces; entry && count < out_count; entry = entry->ifa_next) {
        if (!scan_interface_should_enumerate(entry, include_loopback)) {
            continue;
        }
        const struct sockaddr_in *addr = (const struct sockaddr_in *)entry->ifa_addr;
        const struct sockaddr_in *netmask = (const struct sockaddr_in *)entry->ifa_netmask;
        uint32_t mask = netmask ? ntohl(netmask->sin_addr.s_addr) : 0;
        uint8_t prefix = 0;
        while (mask & 0x80000000u) {
            prefix++;
            mask <<= 1;
        }
        out[count].address = addr->sin_addr.s_addr;
        out[count].prefix = prefix;
        count++;
    }
    freeifaddrs(interfaces);
    return count;
}
#else
static size_t scan_collect_subnets(bool include_loopback, scan_subnet_t *out, size_t out_count)
{
    ULONG bytes = 16 * 1024;
    IP_ADAPTER_ADDRESSES *adapters = NULL;
    DWORD status = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < 2 && status == ERROR_BUFFER_OVERFLOW; attempt++) {
        free(adapters);
        adapters = malloc(bytes);
        if (!adapters) {
            return 0;
        }
        status = GetAdaptersAddresses(AF_INET,
                                      GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, NULL,
                                      adapters, &bytes);
    }
    if (status != NO_ERROR) {
        C64_LOG_DEBUG("DEVICE: GetAdaptersAddresses failed (%lu)", (unsigned long)status);
        free(adapters);
        return 0;
    }

    // Windows exposes adapter state and IPv4 prefixes through IP Helper API.
    // Enumerate every active, non-loopback/non-tunnel adapter.
    size_t count = 0;
    for (IP_ADAPTER_ADDRESSES *adapter = adapters; adapter && count < out_count; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp ||
            (!include_loopback && adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) || adapter->IfType == IF_TYPE_TUNNEL) {
            continue;
        }
        for (IP_ADAPTER_UNICAST_ADDRESS *unicast = adapter->FirstUnicastAddress; unicast && count < out_count;
             unicast = unicast->Next) {
            if (!unicast->Address.lpSockaddr || unicast->Address.lpSockaddr->sa_family != AF_INET) {
                continue;
            }
            const struct sockaddr_in *addr = (const struct sockaddr_in *)unicast->Address.lpSockaddr;
            out[count].address = addr->sin_addr.s_addr;
            out[count].prefix = unicast->OnLinkPrefixLength;
            count++;
        }
    }
    free(adapters);
    return count;
}
#endif

// Adds every local subnet, the one carrying the default route first, then
// private LANs, then everything else. Host enumeration is capped and the
// sweep is deadline-bound, so the order decides which networks are covered
// when a machine has many virtual adapters (Docker, libvirt, Hyper-V, VPNs):
// the network the user's LAN is on must never be the one that is cut.
static void scan_add_local_subnets(scan_job_t *job)
{
    if (!job || job->count >= C64_SCAN_MAX_HOSTS) {
        return;
    }
    scan_subnet_t subnets[C64_SCAN_MAX_SUBNETS];
    const size_t count = scan_collect_subnets(job->include_loopback_subnet, subnets, C64_SCAN_MAX_SUBNETS);
    uint32_t primary = 0;
    if (!scan_primary_ipv4(&primary)) {
        primary = 0;
    }
    // The network the configured host is on comes before everything else:
    // the user (or a test) pointed the source there.
    struct in_addr configured;
    const bool have_configured = job->selected_host[0] && inet_pton(AF_INET, job->selected_host, &configured) == 1;
    bool added[C64_SCAN_MAX_SUBNETS] = {false};
    for (size_t i = 0; have_configured && i < count && job->count < C64_SCAN_MAX_HOSTS; i++) {
        if (c64_device_scan_same_subnet(subnets[i].address, subnets[i].prefix, configured.s_addr)) {
            scan_add_subnet_hosts(job, subnets[i].address, subnets[i].prefix);
            added[i] = true;
        }
    }
    for (int rank = 0; rank <= 3; rank++) {
        for (size_t i = 0; i < count && job->count < C64_SCAN_MAX_HOSTS; i++) {
            if (!added[i] && c64_device_scan_subnet_rank(subnets[i].address, primary) == rank) {
                scan_add_subnet_hosts(job, subnets[i].address, subnets[i].prefix);
            }
        }
    }
    job->subnet_count = count;
}

static scan_job_t *build_scan_job(struct c64_source *context, uint16_t port)
{
    scan_job_t *job = calloc(1, sizeof(*job));
    if (!job) {
        return NULL;
    }
    job->port = port ? port : C64_SCAN_DEFAULT_PORT;
    job->control_port = 64;
    obs_source_t *source = context ? context->source : NULL;
    /* Scan saved and manually configured hosts as well as local subnets.
     * Registered hosts are enumerated first, so apply_scan_results() -- which
     * keeps the lowest-index responsive address per device -- leaves an
     * already-known device on the address it is already on file with. */
    for (size_t i = 0; i < c64_device_registry_count() && job->count < C64_SCAN_MAX_HOSTS; i++) {
        const c64_device_t *device = c64_device_registry_get_at(i);
        if (device) {
            scan_add_host(job, device->host);
        }
    }
    if (source) {
        obs_data_t *settings = obs_source_get_settings(source);
        if (settings) {
            snprintf(job->selected_device_id, sizeof(job->selected_device_id), "%s",
                     obs_data_get_string(settings, "c64_device"));
            const char *configured_host = obs_data_get_string(settings, "c64_host");
            snprintf(job->selected_host, sizeof(job->selected_host), "%s", configured_host ? configured_host : "");
            scan_add_host(job, configured_host);
            job->control_port = (uint16_t)obs_data_get_int(settings, "control_port");
            if (!job->control_port) {
                job->control_port = 64;
            }
            struct in_addr configured_address;
            job->include_loopback_subnet = configured_host &&
                                           inet_pton(AF_INET, configured_host, &configured_address) == 1 &&
                                           ((ntohl(configured_address.s_addr) >> 24) == 127);
            obs_data_release(settings);
        }
    }
    // Everything added so far is a known host (registered profile or the
    // configured host); the subnet sweep below is pure discovery. scan_main
    // probes [0, known_count) first, unflooded.
    job->known_count = job->count;
    scan_add_local_subnets(job);
    job->worker_count = c64_device_scan_worker_count(job->count, C64_SCAN_OVERALL_TIMEOUT_NS);
    return job;
}

static bool c64_device_scan_async_inner(struct c64_source *context, bool startup_retry)
{
    if (!context) {
        return false;
    }
    scan_job_t *job = build_scan_job(context, C64_SCAN_DEFAULT_PORT);
    if (!job) {
        return false;
    }
    pthread_mutex_init(&job->mutex, NULL);
    job->deadline_ns = os_gettime_ns() + C64_SCAN_OVERALL_TIMEOUT_NS;
    job->startup_retry = startup_retry;
    job->source = context->source ? obs_source_get_ref(context->source) : NULL;
    job->context = context;
    pthread_t thread;
    if (pthread_create(&thread, NULL, scan_main, job) != 0) {
        pthread_mutex_destroy(&job->mutex);
        free(job);
        return false;
    }
    pthread_detach(thread);
    return true;
}

bool c64_device_scan_async(struct c64_source *context)
{
    return c64_device_scan_async_inner(context, false);
}

bool c64_device_scan_startup_async(struct c64_source *context)
{
    return c64_device_scan_async_inner(context, true);
}

bool c64_device_scan_sync(struct c64_source *context, uint16_t port)
{
    scan_job_t *job = build_scan_job(context, port);
    if (!job) {
        return false;
    }
    pthread_mutex_init(&job->mutex, NULL);
    job->deadline_ns = os_gettime_ns() + C64_SCAN_OVERALL_TIMEOUT_NS;
    obs_source_t *source = context ? context->source : NULL;
    job->source = source ? obs_source_get_ref(source) : NULL;
    job->context = context;
    /* Called from the script executor thread, already off the OBS UI thread,
     * so blocking here (bounded by the deadline above) is safe. */
    scan_main(job);
    return true;
}
