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
#define C64_SCAN_STREAM_TEST_WAIT_MS 900

typedef struct {
    char data[2048];
    size_t used;
} response_t;

// A single host's successful match. is_new_host is set once, at match time,
// by comparing against the registry *before* any result of this scan pass has
// been applied -- it means "this candidate's host differs from what's
// currently on file for this id (or the id isn't registered yet)".
typedef struct {
    c64_device_t device;
    bool is_new_host;
} scan_result_t;

typedef struct {
    char hosts[C64_SCAN_MAX_HOSTS][16];
    size_t count;
    size_t next;
    pthread_mutex_t mutex;
    uint64_t deadline_ns;
    obs_source_t *source;
    struct c64_source *context; // Nullable; set only when scanning drives the Scan button's label.
    uint16_t port;
    // Candidate matches, applied to the registry only after every worker has
    // finished (see scan_main). A single physical device can respond at more
    // than one address in the same pass (e.g. a stale DHCP lease still being
    // answered alongside the current one); collecting results first avoids a
    // race where whichever worker happens to finish last wins arbitrarily.
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

// Confirms `host` actually delivers a video stream, not just a working
// /v1/info response. Some Ultimate 64 units answer /v1/info identically on
// every network interface (e.g. Ethernet and Wi-Fi), but streaming may only
// work reliably over one of them; offering a non-streaming address would
// otherwise silently produce a blank preview. Starts a short-lived video
// stream to a throwaway local port and checks whether any packet arrives.
static bool scan_verify_streaming(const char *host, uint16_t port)
{
    char local_ip[64] = {0};
    if (!c64_detect_local_ip_for_host(host, NULL, local_ip, sizeof(local_ip))) {
        return false;
    }

    socket_t sock = c64_create_udp_socket(0);
    if (sock == INVALID_SOCKET_VALUE) {
        return false;
    }

    struct sockaddr_in local_addr;
    socklen_t local_len = sizeof(local_addr);
    if (getsockname(sock, (struct sockaddr *)&local_addr, &local_len) != 0) {
        close(sock);
        return false;
    }

#ifdef _WIN32
    DWORD timeout_ms = C64_SCAN_STREAM_TEST_WAIT_MS;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout_ms, sizeof(timeout_ms));
#else
    struct timeval timeout;
    timeout.tv_sec = C64_SCAN_STREAM_TEST_WAIT_MS / 1000;
    timeout.tv_usec = (C64_SCAN_STREAM_TEST_WAIT_MS % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
#endif

    char dest[80];
    snprintf(dest, sizeof(dest), "%s:%u", local_ip, ntohs(local_addr.sin_port));
    char *escaped_dest = curl_easy_escape(NULL, dest, 0);
    if (!escaped_dest) {
        close(sock);
        return false;
    }

    bool start_ok = false;
    CURL *start_curl = curl_easy_init();
    if (start_curl) {
        char url[160];
        snprintf(url, sizeof(url), "http://%s:%u/v1/streams/video:start?ip=%s", host, port, escaped_dest);
        curl_easy_setopt(start_curl, CURLOPT_URL, url);
        curl_easy_setopt(start_curl, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(start_curl, CURLOPT_CONNECTTIMEOUT_MS, C64_SCAN_TIMEOUT_MS);
        curl_easy_setopt(start_curl, CURLOPT_TIMEOUT_MS, C64_SCAN_TIMEOUT_MS);
        curl_easy_setopt(start_curl, CURLOPT_NOSIGNAL, 1L);
        CURLcode start_code = curl_easy_perform(start_curl);
        long start_status = 0;
        curl_easy_getinfo(start_curl, CURLINFO_RESPONSE_CODE, &start_status);
        start_ok = (start_code == CURLE_OK && start_status >= 200 && start_status < 300);
        curl_easy_cleanup(start_curl);
    }
    curl_free(escaped_dest);

    bool received = false;
    if (start_ok) {
        char buf[2048];
        received = recv(sock, buf, sizeof(buf), 0) > 0;
    }

    // Best-effort stop -- failures here must not affect the verdict.
    CURL *stop_curl = curl_easy_init();
    if (stop_curl) {
        char stop_url[80];
        snprintf(stop_url, sizeof(stop_url), "http://%s:%u/v1/streams/video:stop", host, port);
        curl_easy_setopt(stop_curl, CURLOPT_URL, stop_url);
        curl_easy_setopt(stop_curl, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(stop_curl, CURLOPT_CONNECTTIMEOUT_MS, C64_SCAN_TIMEOUT_MS);
        curl_easy_setopt(stop_curl, CURLOPT_TIMEOUT_MS, C64_SCAN_TIMEOUT_MS);
        curl_easy_setopt(stop_curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_perform(stop_curl);
        curl_easy_cleanup(stop_curl);
    }

    close(sock);
    return received;
}

static void scan_one_host(scan_job_t *job, const char *host)
{
    response_t body = {0};
    CURL *curl = curl_easy_init();
    if (!curl) {
        return;
    }
    char url[80];
    snprintf(url, sizeof(url), "http://%s:%u/v1/info", host, job->port);
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
    const bool password_required = c64_device_scan_response_is_candidate(status, body.data);
    if (code != CURLE_OK || (status != 200 && !password_required)) {
        return;
    }
    char product[64] = {0};
    if (status == 200 && (!extract_json_string(body.data, "product", product, sizeof(product)) ||
                          !c64_device_scan_product_matches(product))) {
        // Confirmed non-streaming hardware (e.g. an Ultimate II family unit).
        // If this host was previously registered under an earlier, looser
        // product filter, prune it now: only streaming-capable devices
        // (Ultimate 64 / C64 Ultimate) belong in the device list.
        const c64_device_t *stale = c64_device_registry_find_by_host(host);
        if (stale) {
            c64_device_registry_delete(stale->id);
        }
        return;
    }
    c64_device_t device = {0};
    char unique_id[64] = {0};
    extract_json_string(body.data, "unique_id", unique_id, sizeof(unique_id));
    if (!c64_device_id_from_host(device.id, sizeof(device.id), unique_id, host)) {
        return;
    }
    if (!extract_json_string(body.data, "hostname", device.name, sizeof(device.name))) {
        snprintf(device.name, sizeof(device.name), "%s", product[0] ? product : host);
    }
    // The same physical device can be discovered at more than one address
    // (e.g. Ethernet and Wi-Fi); show the host -- and whether it needs a
    // password -- right in the dropdown label so entries stay distinguishable.
    // Baked into the editable name (not synthesized at display time) so the
    // user can trim it via Device name + Save if they don't want it.
    {
        const size_t used = strlen(device.name);
        snprintf(device.name + used, sizeof(device.name) - used, " (%s%s)", host,
                 password_required ? ", Password" : "");
    }
    snprintf(device.host, sizeof(device.host), "%s", host);
    device.video_port = 11000;
    device.audio_port = 11001;
    device.control_port = 64;

    // Password-protected devices can't be verified without credentials --
    // keep the existing behavior of listing them unverified. Everything else
    // must prove it actually streams before it can be offered/registered.
    if (!password_required && !scan_verify_streaming(host, job->port)) {
        const c64_device_t *stale = c64_device_registry_find_by_host(host);
        if (stale) {
            c64_device_registry_delete(stale->id);
        }
        return;
    }

    const c64_device_t *existing = c64_device_registry_get(device.id);
    const bool is_new_host = !existing || strcmp(existing->host, device.host) != 0;

    pthread_mutex_lock(&job->mutex);
    if (job->result_count < C64_SCAN_MAX_RESULTS) {
        job->results[job->result_count].device = device;
        job->results[job->result_count].is_new_host = is_new_host;
        job->result_count++;
    }
    pthread_mutex_unlock(&job->mutex);
}

static void *scan_worker(void *opaque)
{
    scan_job_t *job = opaque;
    for (;;) {
        pthread_mutex_lock(&job->mutex);
        const size_t index = job->next++;
        pthread_mutex_unlock(&job->mutex);
        if (index >= job->count) {
            return NULL;
        }
        if (os_gettime_ns() >= job->deadline_ns) {
            return NULL;
        }
        scan_one_host(job, job->hosts[index]);
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

// Applies collected scan results to the registry. A device that changed
// address (is_new_host) always wins; a result that merely confirms an
// already-registered host is applied only if no changed-address result was
// seen for that same id, so a still-answering stale lease can never
// overwrite a freshly-discovered address regardless of thread completion
// order.
static void apply_scan_results(scan_job_t *job)
{
    char changed_ids[C64_SCAN_MAX_RESULTS][C64_DEVICE_ID_MAX];
    size_t changed_count = 0;

    for (size_t i = 0; i < job->result_count; i++) {
        if (job->results[i].is_new_host) {
            c64_device_registry_upsert(&job->results[i].device);
            if (changed_count < C64_SCAN_MAX_RESULTS) {
                snprintf(changed_ids[changed_count++], C64_DEVICE_ID_MAX, "%s", job->results[i].device.id);
            }
        }
    }
    for (size_t i = 0; i < job->result_count; i++) {
        if (job->results[i].is_new_host) {
            continue;
        }
        bool already_changed = false;
        for (size_t j = 0; j < changed_count; j++) {
            if (!strcmp(changed_ids[j], job->results[i].device.id)) {
                already_changed = true;
                break;
            }
        }
        if (!already_changed) {
            c64_device_registry_upsert(&job->results[i].device);
        }
    }
}

static void *scan_main(void *opaque)
{
    scan_job_t *job = opaque;
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
    apply_scan_results(job);
    pthread_mutex_destroy(&job->mutex);
    if (job->source) {
        scan_completion_t *completion = malloc(sizeof(*completion));
        if (completion) {
            completion->source = job->source;
            completion->context = job->context;
            obs_queue_task(OBS_TASK_UI, scan_complete_on_ui, completion, false);
        } else {
            obs_source_release(job->source);
        }
    }
    free(job);
    return NULL;
}

static scan_job_t *build_scan_job(obs_source_t *source, uint16_t port)
{
    scan_job_t *job = calloc(1, sizeof(*job));
    if (!job) {
        return NULL;
    }
    job->port = port ? port : C64_SCAN_DEFAULT_PORT;
    /* Scan saved and manually configured hosts as well as local subnets. */
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
    scan_job_t *job = build_scan_job(context->source, C64_SCAN_DEFAULT_PORT);
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

bool c64_device_scan_sync(obs_source_t *source, uint16_t port)
{
    scan_job_t *job = build_scan_job(source, port);
    if (!job) {
        return false;
    }
    pthread_mutex_init(&job->mutex, NULL);
    job->deadline_ns = os_gettime_ns() + C64_SCAN_OVERALL_TIMEOUT_NS;
    job->source = source ? obs_source_get_ref(source) : NULL;
    /* Called from the script executor thread, already off the OBS UI thread,
     * so blocking here (bounded by the deadline above) is safe. */
    scan_main(job);
    return true;
}
