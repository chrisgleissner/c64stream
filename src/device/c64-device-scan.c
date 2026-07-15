#include "c64-device-scan.h"
#include "c64-device.h"
#include "c64-network.h"

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

#define C64_SCAN_WORKERS 24
#define C64_SCAN_MAX_HOSTS 1024
#define C64_SCAN_TIMEOUT_MS 650L
#define C64_SCAN_OVERALL_TIMEOUT_NS (8ULL * 1000000000ULL)

typedef struct {
    char data[2048];
    size_t used;
} response_t;

typedef struct {
    char hosts[C64_SCAN_MAX_HOSTS][16];
    size_t count;
    size_t next;
    pthread_mutex_t mutex;
    uint64_t deadline_ns;
    obs_source_t *source;
} scan_job_t;

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
    return strstr(lower, "ultimate") || !strcmp(lower, "c64u");
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

static void scan_one_host(scan_job_t *job, const char *host)
{
    response_t body = {0};
    CURL *curl = curl_easy_init();
    if (!curl) {
        return;
    }
    char url[64];
    snprintf(url, sizeof(url), "http://%s/v1/info", host);
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
    if (password_required) {
        const size_t used = strlen(device.name);
        snprintf(device.name + used, sizeof(device.name) - used, " (password required)");
    }
    snprintf(device.host, sizeof(device.host), "%s", host);
    device.video_port = 11000;
    device.audio_port = 11001;
    device.control_port = 64;
    pthread_mutex_lock(&job->mutex);
    c64_device_registry_upsert(&device);
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
    obs_source_t *source = opaque;
    obs_source_update_properties(source);
    obs_source_release(source);
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
    pthread_mutex_destroy(&job->mutex);
    if (job->source) {
        obs_queue_task(OBS_TASK_UI, scan_complete_on_ui, job->source, false);
    }
    free(job);
    return NULL;
}

bool c64_device_scan_async(obs_source_t *source)
{
    (void)source;
    scan_job_t *job = calloc(1, sizeof(*job));
    if (!job) {
        return false;
    }
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
        return false;
    }
    for (struct ifaddrs *entry = interfaces; entry && job->count < C64_SCAN_MAX_HOSTS; entry = entry->ifa_next) {
        if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET || !(entry->ifa_flags & IFF_UP) ||
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
    pthread_mutex_init(&job->mutex, NULL);
    job->deadline_ns = os_gettime_ns() + C64_SCAN_OVERALL_TIMEOUT_NS;
    job->source = source ? obs_source_get_ref(source) : NULL;
    pthread_t thread;
    if (pthread_create(&thread, NULL, scan_main, job) != 0) {
        pthread_mutex_destroy(&job->mutex);
        free(job);
        return false;
    }
    pthread_detach(thread);
    return true;
}
