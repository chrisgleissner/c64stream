#include "c64-device-scan.h"
#include "c64-device.h"
#include "c64-network.h"
#include "c64-types.h"

#include <curl/curl.h>
#ifndef _WIN32
#include <arpa/inet.h>
#endif
#include <ctype.h>
#ifndef _WIN32
#include <ifaddrs.h>
#include <net/if.h>
#endif
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util/platform.h>

#define C64_SCAN_WORKERS 48
#define C64_SCAN_MAX_HOSTS 1024
#define C64_SCAN_MAX_RESULTS 64
#define C64_SCAN_TIMEOUT_MS 650L
#define C64_SCAN_OVERALL_TIMEOUT_NS (12ULL * 1000000000ULL)
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
    char hosts[C64_SCAN_MAX_HOSTS][16];
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
    pthread_mutex_t mutex;
    uint64_t deadline_ns;
    obs_source_t *source;
    struct c64_source *context; // Nullable; set only when scanning drives the Scan button's label.
    uint16_t port;
    // Candidate matches, applied to the registry only after every worker has
    // finished (see scan_main). One physical device routinely answers at more
    // than one address in the same pass -- a multi-homed unit (Ethernet and
    // Wi-Fi) reports the same unique_id on each, and a stale DHCP lease can
    // still be answered alongside the current one. Collecting first, then
    // resolving by host_index, keeps the choice independent of which worker
    // happens to finish last.
    scan_result_t results[C64_SCAN_MAX_RESULTS];
    size_t result_count;
} scan_job_t;

// Outlives scan_job_t (freed before the UI-thread completion task runs).
typedef struct {
    obs_source_t *source;
    struct c64_source *context;
} scan_completion_t;

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
    size_t bytes = size * nmemb;
    if (!response || response->used + bytes >= sizeof(response->data)) {
        return 0;
    }
    memcpy(response->data + response->used, data, bytes);
    response->used += bytes;
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

typedef enum {
    SCAN_PROBE_MATCH,       // Streaming-capable device that answers REST + control port; device filled.
    SCAN_PROBE_NOT_DEVICE,  // Answered /v1/info but is not streaming-capable hardware.
    SCAN_PROBE_NO_RESPONSE, // No usable answer this attempt (may just be transient load; retryable).
} scan_probe_result_t;

// Single reachability probe of one address: /v1/info (REST) plus a control-port
// connect. Fills `device` and reports `password_required` only on MATCH.
static scan_probe_result_t scan_probe_host(const char *host, uint16_t port, c64_device_t *device,
                                           bool *password_required)
{
    response_t body = {0};
    CURL *curl = curl_easy_init();
    if (!curl) {
        return SCAN_PROBE_NO_RESPONSE;
    }
    char url[80];
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
    device->control_port = 64;

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
        result = scan_probe_host(host, job->port, &device, &password_required);
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
        pthread_mutex_unlock(&job->mutex);
        if (index >= job->phase_end) {
            return NULL;
        }
        if (os_gettime_ns() >= job->deadline_ns) {
            return NULL;
        }
        scan_one_host(job, job->hosts[index], index);
    }
}

static void scan_complete_on_ui(void *opaque)
{
    scan_completion_t *completion = opaque;
    if (completion->context) {
        completion->context->device_discovery_in_progress = false;
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
static void apply_scan_results(scan_job_t *job)
{
    for (size_t i = 0; i < job->result_count; i++) {
        bool superseded = false;
        for (size_t j = 0; j < job->result_count; j++) {
            if (j != i && !strcmp(job->results[j].device.id, job->results[i].device.id) &&
                job->results[j].host_index < job->results[i].host_index) {
                superseded = true;
                break;
            }
        }
        if (!superseded) {
            c64_device_registry_upsert(&job->results[i].device);
        }
    }
}

// Runs the worker pool over the host range [job->next, job->phase_end).
static void scan_run_phase(scan_job_t *job)
{
    pthread_t workers[C64_SCAN_WORKERS];
    size_t worker_count = 0;
    for (size_t i = 0; i < C64_SCAN_WORKERS; i++) {
        if (pthread_create(&workers[worker_count], NULL, scan_worker, job) != 0) {
            break;
        }
        worker_count++;
    }
    for (size_t i = 0; i < worker_count; i++) {
        pthread_join(workers[i], NULL);
    }
}

static void *scan_main(void *opaque)
{
    scan_job_t *job = opaque;
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
    apply_scan_results(job);
    pthread_mutex_destroy(&job->mutex);
    scan_completion_t *completion = job->source ? malloc(sizeof(*completion)) : NULL;
    if (completion) {
        completion->source = job->source;
        completion->context = job->context;
        obs_queue_task(OBS_TASK_UI, scan_complete_on_ui, completion, false);
    } else {
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
    free(job);
    return NULL;
}

static scan_job_t *build_scan_job(struct c64_source *context, uint16_t port)
{
    scan_job_t *job = calloc(1, sizeof(*job));
    if (!job) {
        return NULL;
    }
    job->port = port ? port : C64_SCAN_DEFAULT_PORT;
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
            scan_add_host(job, obs_data_get_string(settings, "c64_host"));
            obs_data_release(settings);
        }
    }
    // Everything added so far is a known host (registered profile or the
    // configured host); the subnet sweep below is pure discovery. scan_main
    // probes [0, known_count) first, unflooded.
    job->known_count = job->count;
#ifndef _WIN32
    struct ifaddrs *interfaces = NULL;
    if (getifaddrs(&interfaces) != 0) {
        free(job);
        return NULL;
    }
    for (struct ifaddrs *entry = interfaces; entry && job->count < C64_SCAN_MAX_HOSTS; entry = entry->ifa_next) {
        /* IFF_RUNNING (not just IFF_UP) excludes virtual bridges (lxcbr0,
         * custom "br-*" Docker networks, etc.) that are administratively up
         * but carry no real traffic; without it, their /24s get enumerated
         * too and can burn the scan deadline before reaching the real LAN. */
        if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET ||
            (entry->ifa_flags & (IFF_UP | IFF_RUNNING)) != (IFF_UP | IFF_RUNNING) ||
            (entry->ifa_flags & IFF_LOOPBACK) || !strncmp(entry->ifa_name, "docker", 6) ||
            !strncmp(entry->ifa_name, "virbr", 5) || !strncmp(entry->ifa_name, "veth", 4)) {
            continue;
        }
        struct sockaddr_in *addr = (struct sockaddr_in *)entry->ifa_addr;
        struct sockaddr_in *netmask = (struct sockaddr_in *)entry->ifa_netmask;
        uint32_t mask = netmask ? ntohl(netmask->sin_addr.s_addr) : 0;
        uint8_t prefix = 0;
        while (mask & 0x80000000u) {
            prefix++;
            mask <<= 1;
        }
        uint32_t addresses[254];
        size_t count = c64_device_scan_enumerate_subnet(addr->sin_addr.s_addr, prefix, addresses, 254);
        for (size_t i = 0; i < count && job->count < C64_SCAN_MAX_HOSTS; i++) {
            char host[16];
            if (inet_ntop(AF_INET, &addresses[i], host, sizeof(host))) {
                scan_add_host(job, host);
            }
        }
    }
    freeifaddrs(interfaces);
#endif
    return job;
}

bool c64_device_scan_async(struct c64_source *context)
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
    /* Called from the script executor thread, already off the OBS UI thread,
     * so blocking here (bounded by the deadline above) is safe. */
    scan_main(job);
    return true;
}
