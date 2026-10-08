#include "c64-device-palette.h"

#include <ctype.h>
#include <curl/curl.h>
#include <util/threading.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PALETTE_SETTING_PATH "/v1/configs/U64%20Specific%20Settings/Palette%20Definition"

uint32_t c64_device_palette_clamp_interval(long long interval_ms)
{
    if (interval_ms <= 0) {
        return C64_DEVICE_PALETTE_POLL_DEFAULT_MS;
    }
    if (interval_ms < C64_DEVICE_PALETTE_POLL_MIN_MS) {
        return C64_DEVICE_PALETTE_POLL_MIN_MS;
    }
    if (interval_ms > C64_DEVICE_PALETTE_POLL_MAX_MS) {
        return C64_DEVICE_PALETTE_POLL_MAX_MS;
    }
    return (uint32_t)interval_ms;
}

// Copies the JSON string starting at p (just after its opening quote) into
// out, resolving the escapes the firmware can emit. Returns false if the
// string is unterminated or does not fit.
static bool copy_json_string(const char *p, char *out, size_t out_size)
{
    size_t n = 0;
    for (; *p && *p != '"'; p++) {
        char ch = *p;
        if (ch == '\\') {
            p++;
            if (!*p) {
                return false;
            }
            switch (*p) {
            case 'n':
                ch = '\n';
                break;
            case 't':
                ch = '\t';
                break;
            default:
                ch = *p; // \" \\ \/
                break;
            }
        }
        if (n + 1 >= out_size) {
            return false;
        }
        out[n++] = ch;
    }
    if (*p != '"') {
        return false;
    }
    out[n] = '\0';
    return true;
}

bool c64_device_palette_parse_setting(const char *json, char *name, size_t name_size)
{
    if (!json || !name || !name_size) {
        return false;
    }
    const char *item = strstr(json, "\"Palette Definition\"");
    const char *current = item ? strstr(item, "\"current\"") : NULL;
    if (!current) {
        return false;
    }
    const char *p = strchr(current + strlen("\"current\""), ':');
    if (!p) {
        return false;
    }
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    if (*p != '"') {
        return false;
    }
    return copy_json_string(p + 1, name, name_size);
}

bool c64_device_palette_name_is_safe(const char *name)
{
    if (!name || !name[0] || strlen(name) >= C64_DEVICE_PALETTE_NAME_MAX) {
        return false;
    }
    if (!strcmp(name, ".") || !strcmp(name, "..") || strstr(name, "..")) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        if (*p < 0x20 || *p == 0x7F || *p == '/' || *p == '\\' || *p == ':' || *p == '*' || *p == '?' || *p == '"' ||
            *p == '<' || *p == '>' || *p == '|') {
            return false;
        }
    }
    return true;
}

bool c64_device_palette_parse_vpl(const char *text, size_t length, uint32_t colors[16])
{
    if (!text || !colors || length < 8 || length > C64_DEVICE_PALETTE_VPL_MAX) {
        return false;
    }
    int count = 0;
    size_t pos = 0;
    while (pos < length && count < 16) {
        // One line, at most 79 characters as in the firmware's line buffer.
        char line[80];
        size_t n = 0;
        while (pos < length && text[pos] != '\n' && text[pos] != '\r') {
            if (n + 1 < sizeof(line)) {
                line[n++] = text[pos];
            }
            pos++;
        }
        while (pos < length && (text[pos] == '\n' || text[pos] == '\r')) {
            pos++;
        }
        line[n] = '\0';
        char *comment = strchr(line, '#');
        if (comment) {
            *comment = '\0';
        }
        size_t len = strlen(line);
        while (len && (line[len - 1] == ' ' || line[len - 1] == '\t')) {
            line[--len] = '\0';
        }
        if (!len) {
            continue;
        }
        unsigned int r, g, b;
        if (sscanf(line, "%x %x %x", &r, &g, &b) != 3) {
            break;
        }
        colors[count++] = 0xFF000000u | ((uint32_t)(b & 0xFF) << 16) | ((uint32_t)(g & 0xFF) << 8) | (r & 0xFF);
    }
    return count == 16;
}

bool c64_device_palette_cache_path(char *out, size_t out_size, const char *dir, const char *device_key,
                                   const char *name, uint64_t size)
{
    if (!out || !out_size || !dir || !device_key || !device_key[0] || !c64_device_palette_name_is_safe(name)) {
        return false;
    }
    char key[64];
    size_t n = 0;
    for (const char *p = device_key; *p && n + 1 < sizeof(key); p++) {
        key[n++] = isalnum((unsigned char)*p) ? (char)tolower((unsigned char)*p) : '-';
    }
    key[n] = '\0';
    const int written = snprintf(out, out_size, "%s/%s-%llu-%s", dir, key, (unsigned long long)size, name);
    return written > 0 && (size_t)written < out_size;
}

const char *c64_device_palette_error_text(c64_palette_error_t error)
{
    switch (error) {
    case C64_PALETTE_ERROR_NONE:
        return "";
    case C64_PALETTE_ERROR_UNREACHABLE:
        return "device not reachable";
    case C64_PALETTE_ERROR_PASSWORD:
        return "network password rejected";
    case C64_PALETTE_ERROR_NO_SETTING:
        return "firmware has no palette setting";
    case C64_PALETTE_ERROR_FILE_NOT_FOUND:
        return "palette file not found on the device";
    case C64_PALETTE_ERROR_FTP_UNREACHABLE:
        return "FTP not reachable";
    case C64_PALETTE_ERROR_INVALID_FILE:
        return "not a valid palette file";
    case C64_PALETTE_ERROR_BAD_NAME:
        return "unsupported palette file name";
    case C64_PALETTE_ERROR_CACHE:
    default:
        return "palette cache unavailable";
    }
}

void c64_device_palette_describe(c64_palette_source_t source, c64_palette_error_t error, const char *file, char *out,
                                 size_t out_size)
{
    if (!out || !out_size) {
        return;
    }
    const char *name = file && file[0] ? file : "?";
    switch (source) {
    case C64_PALETTE_SOURCE_OFF:
        snprintf(out, out_size, "off");
        break;
    case C64_PALETTE_SOURCE_WAITING:
        snprintf(out, out_size, "waiting for the device");
        break;
    case C64_PALETTE_SOURCE_STREAM:
        snprintf(out, out_size, "video stream");
        break;
    case C64_PALETTE_SOURCE_DEVICE_FILE:
        snprintf(out, out_size, "device setting, file \"%s\"", name);
        break;
    case C64_PALETTE_SOURCE_DEVICE_DEFAULT:
        snprintf(out, out_size, "device setting, built-in palette");
        break;
    case C64_PALETTE_SOURCE_NEEDS_REST:
        snprintf(
            out, out_size,
            "default palette (Follow device reads the palette over REST; set Stream Control Transport to Auto or Force REST)");
        break;
    case C64_PALETTE_SOURCE_ERROR:
    default:
        if (file && file[0]) {
            snprintf(out, out_size, "unavailable: %s (\"%s\")", c64_device_palette_error_text(error), file);
        } else {
            snprintf(out, out_size, "unavailable: %s", c64_device_palette_error_text(error));
        }
        break;
    }
}

typedef struct {
    char *data;
    size_t used;
    size_t capacity;
    bool overflow;
} fetch_buffer_t;

static size_t fetch_write(void *data, size_t size, size_t nmemb, void *opaque)
{
    fetch_buffer_t *buffer = opaque;
    const size_t bytes = size * nmemb;
    if (buffer->used + bytes + 1 > buffer->capacity) {
        buffer->overflow = true;
        return 0;
    }
    memcpy(buffer->data + buffer->used, data, bytes);
    buffer->used += bytes;
    buffer->data[buffer->used] = '\0';
    return bytes;
}

// How often a running request checks its cancel flag.
#define PALETTE_CANCEL_POLL_MS 50

// Runs a request, aborting within PALETTE_CANCEL_POLL_MS once *cancel becomes
// true. curl_easy_perform would check a progress callback only about once a
// second while it waits for the network, which makes stopping a source slow.
static CURLcode perform(CURL *curl, const volatile bool *cancel)
{
    if (!cancel) {
        return curl_easy_perform(curl);
    }
    CURLM *multi = curl_multi_init();
    if (!multi) {
        return CURLE_OUT_OF_MEMORY;
    }
    if (curl_multi_add_handle(multi, curl) != CURLM_OK) {
        curl_multi_cleanup(multi);
        return CURLE_FAILED_INIT;
    }
    CURLcode result = CURLE_ABORTED_BY_CALLBACK;
    for (;;) {
        int running = 0;
        if (curl_multi_perform(multi, &running) != CURLM_OK) {
            result = CURLE_FAILED_INIT;
            break;
        }
        if (!running) {
            int queued = 0;
            CURLMsg *message;
            result = CURLE_RECV_ERROR;
            while ((message = curl_multi_info_read(multi, &queued)) != NULL) {
                if (message->msg == CURLMSG_DONE && message->easy_handle == curl) {
                    result = message->data.result;
                }
            }
            break;
        }
        if (os_atomic_load_bool(cancel)) {
            result = CURLE_ABORTED_BY_CALLBACK;
            break;
        }
        curl_multi_poll(multi, NULL, 0, PALETTE_CANCEL_POLL_MS, NULL);
    }
    curl_multi_remove_handle(multi, curl);
    curl_multi_cleanup(multi);
    return result;
}

// GET http://<host><path> into buffer, sending the network password.
static bool http_get(const char *host, const char *path, const char *password, long timeout_ms,
                     const volatile bool *cancel, fetch_buffer_t *buffer, long *status)
{
    CURL *curl = curl_easy_init();
    if (!curl) {
        return false;
    }
    char url[512];
    if (snprintf(url, sizeof(url), "http://%s%s", host, path) >= (int)sizeof(url)) {
        curl_easy_cleanup(curl);
        return false;
    }
    struct curl_slist *headers = NULL;
    if (password && password[0]) {
        char header[300];
        snprintf(header, sizeof(header), "X-Password: %s", password);
        headers = curl_slist_append(headers, header);
    }
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fetch_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, buffer);
    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }
    const CURLcode code = perform(curl, cancel);
    *status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, status);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    return code == CURLE_OK;
}

bool c64_device_palette_fetch_setting(const char *host, const char *password, long timeout_ms,
                                      const volatile bool *cancel, char *name, size_t name_size,
                                      c64_palette_error_t *error)
{
    c64_palette_error_t ignored;
    error = error ? error : &ignored;
    *error = C64_PALETTE_ERROR_NONE;
    if (!host || !host[0] || !name || !name_size) {
        *error = C64_PALETTE_ERROR_UNREACHABLE;
        return false;
    }
    char data[2048];
    fetch_buffer_t buffer = {data, 0, sizeof(data), false};
    data[0] = '\0';
    long status = 0;
    if (!http_get(host, PALETTE_SETTING_PATH, password, timeout_ms, cancel, &buffer, &status)) {
        *error = C64_PALETTE_ERROR_UNREACHABLE;
        return false;
    }
    if (status == 401 || status == 403) {
        *error = C64_PALETTE_ERROR_PASSWORD;
        return false;
    }
    if (status != 200 && status != 404) {
        *error = C64_PALETTE_ERROR_UNREACHABLE; // e.g. a busy device answering 503
        return false;
    }
    if (status != 200 || !c64_device_palette_parse_setting(data, name, name_size)) {
        *error = C64_PALETTE_ERROR_NO_SETTING;
        return false;
    }
    return true;
}

bool c64_device_palette_fetch_size(const char *host, const char *password, const char *name, long timeout_ms,
                                   const volatile bool *cancel, uint64_t *size, c64_palette_error_t *error)
{
    c64_palette_error_t ignored;
    error = error ? error : &ignored;
    *error = C64_PALETTE_ERROR_BAD_NAME;
    if (!host || !host[0] || !size || !c64_device_palette_name_is_safe(name)) {
        return false;
    }
    char *escaped = curl_easy_escape(NULL, name, 0);
    if (!escaped) {
        return false;
    }
    // Every byte of a name can escape to three characters.
    char path[64 + 3 * C64_DEVICE_PALETTE_NAME_MAX];
    const int written = snprintf(path, sizeof(path), "/v1/files/flash/data/%s:info", escaped);
    curl_free(escaped);
    if (written >= (int)sizeof(path)) {
        return false;
    }
    char data[1024];
    fetch_buffer_t buffer = {data, 0, sizeof(data), false};
    data[0] = '\0';
    long status = 0;
    if (!http_get(host, path, password, timeout_ms, cancel, &buffer, &status)) {
        *error = C64_PALETTE_ERROR_UNREACHABLE;
        return false;
    }
    if (status == 401 || status == 403) {
        *error = C64_PALETTE_ERROR_PASSWORD;
        return false;
    }
    if (status != 200) {
        // Only 404 means the file is missing; any other answer is a device
        // that cannot serve the request right now.
        *error = status == 404 ? C64_PALETTE_ERROR_FILE_NOT_FOUND : C64_PALETTE_ERROR_UNREACHABLE;
        return false;
    }
    *error = C64_PALETTE_ERROR_FILE_NOT_FOUND;
    const char *p = strstr(data, "\"size\"");
    p = p ? strchr(p, ':') : NULL;
    if (!p) {
        return false;
    }
    char *end = NULL;
    const unsigned long long value = strtoull(p + 1, &end, 10);
    if (end == p + 1) {
        return false;
    }
    *size = value;
    *error = C64_PALETTE_ERROR_NONE;
    return true;
}

bool c64_device_palette_download(const char *host, uint16_t ftp_port, const char *password, const char *name,
                                 long timeout_ms, const volatile bool *cancel, char *buffer, size_t buffer_size,
                                 size_t *length, c64_palette_error_t *error)
{
    c64_palette_error_t ignored;
    error = error ? error : &ignored;
    *error = C64_PALETTE_ERROR_NONE;
    if (!c64_device_palette_name_is_safe(name)) {
        *error = C64_PALETTE_ERROR_BAD_NAME;
        return false;
    }
    if (!host || !host[0] || !buffer || buffer_size < 2 || !length) {
        *error = C64_PALETTE_ERROR_FTP_UNREACHABLE;
        return false;
    }
    char *escaped = curl_easy_escape(NULL, name, 0);
    if (!escaped) {
        *error = C64_PALETTE_ERROR_BAD_NAME;
        return false;
    }
    char url[128 + 3 * C64_DEVICE_PALETTE_NAME_MAX];
    const int written =
        snprintf(url, sizeof(url), "ftp://%s:%u/Flash/data/%s", host, ftp_port ? ftp_port : 21, escaped);
    curl_free(escaped);
    if (written >= (int)sizeof(url)) {
        *error = C64_PALETTE_ERROR_BAD_NAME;
        return false;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        *error = C64_PALETTE_ERROR_FTP_UNREACHABLE;
        return false;
    }
    fetch_buffer_t fetched = {buffer, 0, buffer_size, false};
    buffer[0] = '\0';
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    // The Ultimate accepts any user name and checks only the network password.
    curl_easy_setopt(curl, CURLOPT_USERNAME, "c64stream");
    curl_easy_setopt(curl, CURLOPT_PASSWORD, password ? password : "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fetch_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &fetched);
    const CURLcode code = perform(curl, cancel);
    long response = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response);
    curl_easy_cleanup(curl);
    *length = fetched.used;
    if (code == CURLE_OK) {
        return true;
    }
    if (fetched.overflow) {
        *error = C64_PALETTE_ERROR_INVALID_FILE;
    } else if (code == CURLE_LOGIN_DENIED || response == 530) {
        *error = C64_PALETTE_ERROR_PASSWORD;
    } else if (code == CURLE_REMOTE_FILE_NOT_FOUND || response == 550) {
        *error = C64_PALETTE_ERROR_FILE_NOT_FOUND;
    } else {
        *error = C64_PALETTE_ERROR_FTP_UNREACHABLE;
    }
    return false;
}
