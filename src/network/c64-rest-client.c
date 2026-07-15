/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-rest-client.h"
#include "c64-logging.h"

#include <curl/curl.h>
#include <ctype.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REST_LOG_PREFIX "📡 REST: "
#define HTTP_TIMEOUT_SECONDS 5

struct c64_rest_client {
    char *base_url;
    char *password;
    char error_msg[2048];
    long last_status;
    c64_rest_outcome_t last_outcome;
    CURL *curl;
    pthread_mutex_t mutex;
};

c64_rest_outcome_t c64_rest_classify_status(long status)
{
    if (status >= 200 && status < 300) {
        return C64_REST_OK;
    }
    if (status == 0) {
        return C64_REST_UNREACHABLE;
    }
    switch (status) {
    case 400:
        return C64_REST_BAD_REQUEST;
    case 401:
    case 403:
        return C64_REST_FORBIDDEN;
    default:
        break;
    }
    // 404, 500, 501 and any other code: not an auth refusal (401/403) and not a
    // payload bug (400). Falling back to the legacy transport is the robust
    // choice and never bypasses authentication.
    if (status == 404 || status == 501) {
        return C64_REST_NOT_SUPPORTED;
    }
    return C64_REST_SERVER_ERROR;
}

// Callback for capturing HTTP response data
typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
} response_buffer_t;

static const char *skip_json_string(const char *p, const char *end)
{
    if (!p || p >= end || *p != '"') {
        return p;
    }

    p++;
    while (p < end) {
        if (*p == '\\') {
            p += (p + 1 < end) ? 2 : 1;
            continue;
        }
        if (*p == '"') {
            return p + 1;
        }
        p++;
    }
    return end;
}

static void json_copy_string(const char *start, const char *end, char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return;
    }
    size_t len = (size_t)(end - start);
    if (len >= out_size) {
        len = out_size - 1;
    }
    memcpy(out, start, len);
    out[len] = '\0';
}

static bool json_errors_empty(const char *json, size_t json_len, char *out_error, size_t out_error_size)
{
    if (!json) {
        return true;
    }

    const char *errors_key = strstr(json, "\"errors\"");
    if (!errors_key) {
        return true;
    }
    const char *p = strchr(errors_key, '[');
    if (!p) {
        return true;
    }
    p++;
    const char *end = json + json_len;
    while (p < end && isspace((unsigned char)*p)) {
        p++;
    }
    if (p >= end || *p == ']') {
        return true;
    }

    if (out_error && out_error_size > 0) {
        if (*p == '"') {
            const char *str_start = p + 1;
            const char *str_end = str_start;
            while (str_end < end) {
                if (*str_end == '\\') {
                    str_end += (str_end + 1 < end) ? 2 : 1;
                    continue;
                }
                if (*str_end == '"') {
                    break;
                }
                str_end++;
            }
            json_copy_string(str_start, str_end, out_error, out_error_size);
        } else {
            snprintf(out_error, out_error_size, "REST response contained errors");
        }
    }

    return false;
}

static bool parse_json_string_array(const char *json, size_t json_len, const char *key, char ***items, size_t *count)
{
    if (!json || !key || !items || !count) {
        return false;
    }

    *items = NULL;
    *count = 0;

    char key_buf[256];
    snprintf(key_buf, sizeof(key_buf), "\"%s\"", key);

    const char *key_pos = strstr(json, key_buf);
    if (!key_pos) {
        return false;
    }

    const char *p = strchr(key_pos, '[');
    if (!p) {
        return false;
    }
    p++;

    const char *end = json + json_len;
    size_t capacity = 8;
    char **list = calloc(capacity, sizeof(char *));
    if (!list) {
        return false;
    }

    while (p < end) {
        while (p < end && isspace((unsigned char)*p)) {
            p++;
        }
        if (p >= end || *p == ']') {
            break;
        }
        if (*p != '"') {
            p++;
            continue;
        }
        const char *str_start = p + 1;
        const char *str_end = str_start;
        while (str_end < end) {
            if (*str_end == '\\') {
                str_end += (str_end + 1 < end) ? 2 : 1;
                continue;
            }
            if (*str_end == '"') {
                break;
            }
            str_end++;
        }
        size_t len = (size_t)(str_end - str_start);
        char *value = malloc(len + 1);
        if (!value) {
            break;
        }
        memcpy(value, str_start, len);
        value[len] = '\0';
        if (*count >= capacity) {
            capacity *= 2;
            char **new_list = realloc(list, capacity * sizeof(char *));
            if (!new_list) {
                free(value);
                break;
            }
            list = new_list;
        }
        list[*count] = value;
        (*count)++;
        p = (str_end < end) ? str_end + 1 : end;
    }

    if (*count == 0) {
        free(list);
        list = NULL;
    }

    *items = list;
    return true;
}

static bool find_object_for_key(const char *json, const char *key, const char **out_start, const char **out_end)
{
    if (!json || !key || !out_start || !out_end) {
        return false;
    }

    char key_buf[256];
    snprintf(key_buf, sizeof(key_buf), "\"%s\"", key);
    const char *key_pos = strstr(json, key_buf);
    if (!key_pos) {
        return false;
    }

    const char *p = strchr(key_pos, '{');
    if (!p) {
        return false;
    }

    const char *start = p;
    int depth = 0;
    const char *end = NULL;
    const char *scan = p;
    const char *json_end = json + strlen(json);

    while (scan < json_end) {
        if (*scan == '"') {
            scan = skip_json_string(scan, json_end);
            continue;
        }
        if (*scan == '{') {
            depth++;
        } else if (*scan == '}') {
            depth--;
            if (depth == 0) {
                end = scan;
                break;
            }
        }
        scan++;
    }

    if (!end) {
        return false;
    }

    *out_start = start;
    *out_end = end;
    return true;
}

static bool parse_object_keys(const char *object_start, const char *object_end, char ***items, size_t *count)
{
    if (!object_start || !object_end || !items || !count) {
        return false;
    }

    *items = NULL;
    *count = 0;

    size_t capacity = 8;
    char **list = calloc(capacity, sizeof(char *));
    if (!list) {
        return false;
    }

    const char *p = object_start;
    int depth = 0;

    while (p < object_end) {
        if (*p == '"') {
            const char *str_start = p + 1;
            const char *str_end = str_start;
            while (str_end < object_end) {
                if (*str_end == '\\') {
                    str_end += (str_end + 1 < object_end) ? 2 : 1;
                    continue;
                }
                if (*str_end == '"') {
                    break;
                }
                str_end++;
            }

            if (depth == 1) {
                const char *after_str = str_end + 1;
                while (after_str < object_end && isspace((unsigned char)*after_str)) {
                    after_str++;
                }
                if (after_str < object_end && *after_str == ':') {
                    size_t len = (size_t)(str_end - str_start);
                    char *value = malloc(len + 1);
                    if (!value) {
                        break;
                    }
                    memcpy(value, str_start, len);
                    value[len] = '\0';
                    if (*count >= capacity) {
                        capacity *= 2;
                        char **new_list = realloc(list, capacity * sizeof(char *));
                        if (!new_list) {
                            free(value);
                            break;
                        }
                        list = new_list;
                    }
                    list[*count] = value;
                    (*count)++;
                }
            }

            p = (str_end < object_end) ? str_end + 1 : object_end;
            continue;
        }

        if (*p == '{') {
            depth++;
        } else if (*p == '}') {
            depth--;
            if (depth == 0) {
                break;
            }
        }
        p++;
    }

    if (*count == 0) {
        free(list);
        list = NULL;
    }

    *items = list;
    return true;
}

static bool parse_json_token_string(const char *p, const char *end, char *out_value, size_t out_size,
                                    const char **out_next)
{
    if (!p || !end || !out_value || out_size == 0) {
        return false;
    }

    while (p < end && isspace((unsigned char)*p)) {
        p++;
    }
    if (p >= end) {
        return false;
    }

    if (*p == '"') {
        const char *str_start = p + 1;
        const char *str_end = str_start;
        while (str_end < end) {
            if (*str_end == '\\') {
                str_end += (str_end + 1 < end) ? 2 : 1;
                continue;
            }
            if (*str_end == '"') {
                break;
            }
            str_end++;
        }
        json_copy_string(str_start, str_end, out_value, out_size);
        if (out_next) {
            *out_next = (str_end < end) ? str_end + 1 : end;
        }
        return true;
    }

    const char *token_start = p;
    while (p < end && !isspace((unsigned char)*p) && *p != ',' && *p != '}' && *p != ']') {
        p++;
    }
    json_copy_string(token_start, p, out_value, out_size);
    if (out_next) {
        *out_next = p;
    }
    return true;
}

static bool parse_drive_property(const char *json, const char *drive, const char *property, char *out_value,
                                 size_t out_size)
{
    if (!json || !drive || !property || !out_value || out_size == 0) {
        return false;
    }

    out_value[0] = '\0';

    const char *obj_start = NULL;
    const char *obj_end = NULL;
    if (!find_object_for_key(json, drive, &obj_start, &obj_end)) {
        return false;
    }

    char key_buf[256];
    snprintf(key_buf, sizeof(key_buf), "\"%s\"", property);
    const char *key_pos = strstr(obj_start, key_buf);
    if (!key_pos || key_pos >= obj_end) {
        return true;
    }

    const char *colon = strchr(key_pos, ':');
    if (!colon || colon >= obj_end) {
        return false;
    }

    return parse_json_token_string(colon + 1, obj_end, out_value, out_size, NULL);
}

static const char *c64_strip_c64u_prefix(const char *path)
{
    if (!path) {
        return NULL;
    }

    const char *prefix = "c64u:";
    for (size_t i = 0; prefix[i] != '\0'; i++) {
        if (path[i] == '\0') {
            return path;
        }
        if ((char)tolower((unsigned char)path[i]) != prefix[i]) {
            return path;
        }
    }

    return path + strlen(prefix);
}

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    response_buffer_t *buf = (response_buffer_t *)userp;

    if (buf->size + realsize > buf->capacity) {
        size_t new_capacity = buf->capacity == 0 ? 4096 : buf->capacity * 2;
        while (new_capacity < buf->size + realsize) {
            new_capacity *= 2;
        }
        uint8_t *new_data = realloc(buf->data, new_capacity);
        if (!new_data) {
            return 0; // Out of memory
        }
        buf->data = new_data;
        buf->capacity = new_capacity;
    }

    memcpy(buf->data + buf->size, contents, realsize);
    buf->size += realsize;
    return realsize;
}

c64_rest_client_t *c64_rest_client_create(const char *base_url, const char *password)
{
    if (!base_url) {
        C64_LOG_ERROR(REST_LOG_PREFIX "c64_rest_client_create called with NULL base_url");
        return NULL;
    }

    C64_LOG_DEBUG(REST_LOG_PREFIX "Creating REST client for %s", base_url);

    // Note: We do NOT call curl_global_init() here. OBS Studio initializes libcurl
    // globally during startup, and plugins must use only per-handle APIs like
    // curl_easy_init/cleanup. Calling curl_global_init in a plugin can cause
    // crashes on Windows when detached threads exit.

    c64_rest_client_t *client = calloc(1, sizeof(c64_rest_client_t));
    if (!client) {
        C64_LOG_ERROR(REST_LOG_PREFIX "Failed to allocate client");
        return NULL;
    }

    client->base_url = strdup(base_url);
    if (!client->base_url) {
        C64_LOG_ERROR(REST_LOG_PREFIX "Failed to duplicate base_url");
        free(client);
        return NULL;
    }

    if (password) {
        client->password = strdup(password);
        if (!client->password) {
            C64_LOG_ERROR(REST_LOG_PREFIX "Failed to duplicate password");
            free(client->base_url);
            free(client);
            return NULL;
        }
    }

    // Initialize curl handle (per-handle API, safe in OBS plugins)
    C64_LOG_DEBUG(REST_LOG_PREFIX "Initializing CURL handle");
    client->curl = curl_easy_init();
    if (!client->curl) {
        C64_LOG_ERROR(REST_LOG_PREFIX "curl_easy_init failed");
        free(client->base_url);
        free(client->password);
        free(client);
        return NULL;
    }
    pthread_mutex_init(&client->mutex, NULL);

    // Set common curl options - CRITICAL: NOSIGNAL must be set to prevent crashes on Windows
    curl_easy_setopt(client->curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(client->curl, CURLOPT_TIMEOUT, HTTP_TIMEOUT_SECONDS);
    curl_easy_setopt(client->curl, CURLOPT_FOLLOWLOCATION, 1L);

    C64_LOG_DEBUG(REST_LOG_PREFIX "Created REST client for %s", base_url);
    return client;
}

void c64_rest_client_destroy(c64_rest_client_t *client)
{
    if (!client) {
        return;
    }

    C64_LOG_DEBUG(REST_LOG_PREFIX "Destroying REST client");

    if (client->curl) {
        curl_easy_cleanup(client->curl);
        C64_LOG_DEBUG(REST_LOG_PREFIX "CURL handle cleaned up");
    }
    free(client->base_url);
    free(client->password);
    pthread_mutex_destroy(&client->mutex);
    free(client);

    C64_LOG_DEBUG(REST_LOG_PREFIX "REST client destroyed");
}

// Perform HTTP request
static bool http_request_ex_locked(c64_rest_client_t *client, const char *method, const char *endpoint,
                                   const char *query_params, const uint8_t *body_data, size_t body_size,
                                   response_buffer_t *response, bool accept_reset_close)
{
    if (!client || !client->curl || !method || !endpoint) {
        C64_LOG_ERROR(REST_LOG_PREFIX "http_request called with invalid parameters");
        return false;
    }

    // Default outcome for this request until a real HTTP response is observed.
    // status 0 / UNREACHABLE is overwritten below if we get an HTTP code back.
    client->last_status = 0;
    client->last_outcome = C64_REST_UNREACHABLE;

    C64_LOG_DEBUG(REST_LOG_PREFIX "HTTP %s %s%s", method, endpoint, query_params ? query_params : "");

    char url[1024];
    if (query_params) {
        snprintf(url, sizeof(url), "%s%s?%s", client->base_url, endpoint, query_params);
    } else {
        snprintf(url, sizeof(url), "%s%s", client->base_url, endpoint);
    }

    // Reset curl for new request
    curl_easy_reset(client->curl);

    // CRITICAL: After curl_easy_reset, we must re-set NOSIGNAL to prevent Windows crashes
    curl_easy_setopt(client->curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(client->curl, CURLOPT_TIMEOUT, HTTP_TIMEOUT_SECONDS);
    curl_easy_setopt(client->curl, CURLOPT_URL, url);

    // Set custom headers
    struct curl_slist *headers = NULL;
    if (client->password) {
        char password_header[256];
        snprintf(password_header, sizeof(password_header), "X-Password: %s", client->password);
        headers = curl_slist_append(headers, password_header);
    }

    // Set method and body
    if (strcmp(method, "GET") == 0) {
        curl_easy_setopt(client->curl, CURLOPT_HTTPGET, 1L);
    } else if (strcmp(method, "PUT") == 0) {
        curl_easy_setopt(client->curl, CURLOPT_CUSTOMREQUEST, "PUT");
        if (body_data && body_size > 0) {
            curl_easy_setopt(client->curl, CURLOPT_POSTFIELDS, body_data);
            curl_easy_setopt(client->curl, CURLOPT_POSTFIELDSIZE, (long)body_size);
        }
    } else if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(client->curl, CURLOPT_POST, 1L);
        if (body_data && body_size > 0) {
            curl_easy_setopt(client->curl, CURLOPT_POSTFIELDS, body_data);
            curl_easy_setopt(client->curl, CURLOPT_POSTFIELDSIZE, (long)body_size);
        }
    }

    // A request body is always JSON (e.g. machine:input). The device requires
    // Content-Type: application/json on such writes and returns 400 otherwise.
    if (body_data && body_size > 0) {
        headers = curl_slist_append(headers, "Content-Type: application/json");
    }

    if (headers) {
        curl_easy_setopt(client->curl, CURLOPT_HTTPHEADER, headers);
    }

    // Set response callback if needed
    if (response) {
        curl_easy_setopt(client->curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(client->curl, CURLOPT_WRITEDATA, response);
    }

    // Perform request
    C64_LOG_DEBUG(REST_LOG_PREFIX "Performing CURL request");
    CURLcode res = curl_easy_perform(client->curl);

    // Clean up headers
    if (headers) {
        curl_slist_free_all(headers);
    }

    if (res != CURLE_OK) {
        if (accept_reset_close && (res == CURLE_RECV_ERROR || res == CURLE_GOT_NOTHING)) {
            C64_LOG_WARNING(REST_LOG_PREFIX "HTTP %s %s closed the connection during reset (curl %d): %s", method, url,
                            (int)res, curl_easy_strerror(res));
            client->error_msg[0] = '\0';
            // The reset was accepted; the abrupt close is expected device behaviour.
            client->last_status = 0;
            client->last_outcome = C64_REST_OK;
            return true;
        }
        // Transport failure — no HTTP response. last_status stays 0 / UNREACHABLE.
        snprintf(client->error_msg, sizeof(client->error_msg), "HTTP %s %s failed (curl %d): %s", method, url, (int)res,
                 curl_easy_strerror(res));
        C64_LOG_ERROR(REST_LOG_PREFIX "%s", client->error_msg);
        return false;
    }

    // Check HTTP status code
    long http_code = 0;
    curl_easy_getinfo(client->curl, CURLINFO_RESPONSE_CODE, &http_code);
    C64_LOG_DEBUG(REST_LOG_PREFIX "HTTP response code: %ld", http_code);

    // Record the classified outcome of this response so callers can react.
    client->last_status = http_code;
    client->last_outcome = c64_rest_classify_status(http_code);

    if (http_code < 200 || http_code >= 300) {
        snprintf(client->error_msg, sizeof(client->error_msg), "HTTP %s %s returned status %ld", method, url,
                 http_code);
        C64_LOG_ERROR(REST_LOG_PREFIX "%s", client->error_msg);
        return false;
    }

    return true;
}

static bool http_request_ex(c64_rest_client_t *client, const char *method, const char *endpoint,
                            const char *query_params, const uint8_t *body_data, size_t body_size,
                            response_buffer_t *response, bool accept_reset_close)
{
    if (!client) {
        return false;
    }
    pthread_mutex_lock(&client->mutex);
    const bool ok = http_request_ex_locked(client, method, endpoint, query_params, body_data, body_size, response,
                                           accept_reset_close);
    pthread_mutex_unlock(&client->mutex);
    return ok;
}

static bool http_request(c64_rest_client_t *client, const char *method, const char *endpoint, const char *query_params,
                         const uint8_t *body_data, size_t body_size, response_buffer_t *response)
{
    return http_request_ex(client, method, endpoint, query_params, body_data, body_size, response, false);
}

bool c64_rest_stream_start(c64_rest_client_t *client, bool audio, const char *destination)
{
    return c64_rest_stream_start_with_outcome(client, audio, destination, NULL, NULL);
}

bool c64_rest_stream_start_with_outcome(c64_rest_client_t *client, bool audio, const char *destination,
                                        c64_rest_outcome_t *outcome, long *status)
{
    if (!client || !destination || !destination[0]) {
        return false;
    }

    char *escaped_destination = curl_easy_escape(NULL, destination, 0);
    if (!escaped_destination) {
        pthread_mutex_lock(&client->mutex);
        snprintf(client->error_msg, sizeof(client->error_msg), "Failed to URL-escape stream destination");
        client->last_status = 0;
        client->last_outcome = C64_REST_UNREACHABLE;
        pthread_mutex_unlock(&client->mutex);
        return false;
    }

    char query[160];
    snprintf(query, sizeof(query), "ip=%s", escaped_destination);
    curl_free(escaped_destination);
    pthread_mutex_lock(&client->mutex);
    const bool ok = http_request_ex_locked(client, "PUT", audio ? "/v1/streams/audio:start" : "/v1/streams/video:start",
                                           query, NULL, 0, NULL, false);
    if (outcome) {
        *outcome = client->last_outcome;
    }
    if (status) {
        *status = client->last_status;
    }
    pthread_mutex_unlock(&client->mutex);
    return ok;
}

bool c64_rest_stream_stop(c64_rest_client_t *client, bool audio)
{
    return c64_rest_stream_stop_with_outcome(client, audio, NULL, NULL);
}

bool c64_rest_stream_stop_with_outcome(c64_rest_client_t *client, bool audio, c64_rest_outcome_t *outcome, long *status)
{
    if (!client) {
        return false;
    }
    pthread_mutex_lock(&client->mutex);
    const bool ok = http_request_ex_locked(client, "PUT", audio ? "/v1/streams/audio:stop" : "/v1/streams/video:stop",
                                           NULL, NULL, 0, NULL, false);
    if (outcome) {
        *outcome = client->last_outcome;
    }
    if (status) {
        *status = client->last_status;
    }
    pthread_mutex_unlock(&client->mutex);
    return ok;
}

bool c64_rest_machine_input(c64_rest_client_t *client, const char *json)
{
    return c64_rest_machine_input_with_outcome(client, json, NULL, NULL);
}

bool c64_rest_machine_input_with_outcome(c64_rest_client_t *client, const char *json, c64_rest_outcome_t *outcome,
                                         long *status)
{
    if (!client || !json || !json[0]) {
        return false;
    }
    pthread_mutex_lock(&client->mutex);
    const bool ok = http_request_ex_locked(client, "POST", "/v1/machine:input", NULL, (const uint8_t *)json,
                                           strlen(json), NULL, false);
    if (outcome) {
        *outcome = client->last_outcome;
    }
    if (status) {
        *status = client->last_status;
    }
    pthread_mutex_unlock(&client->mutex);
    return ok;
}

bool c64_rest_release_all(c64_rest_client_t *client)
{
    return c64_rest_machine_input(client, "{\"events\":[{\"kind\":\"release_all\"}]}");
}

static bool request_json(c64_rest_client_t *client, const char *method, const char *endpoint, const char *query_params,
                         char **out_json, size_t *out_len)
{
    if (!out_json) {
        return false;
    }

    response_buffer_t response = {0};
    if (!http_request(client, method, endpoint, query_params, NULL, 0, &response)) {
        free(response.data);
        return false;
    }

    char *json = malloc(response.size + 1);
    if (!json) {
        free(response.data);
        snprintf(client->error_msg, sizeof(client->error_msg), "Out of memory");
        return false;
    }

    if (response.size > 0) {
        memcpy(json, response.data, response.size);
    }
    json[response.size] = '\0';

    free(response.data);
    *out_json = json;
    if (out_len) {
        *out_len = response.size;
    }
    return true;
}

bool c64_rest_reset(c64_rest_client_t *client)
{
    if (!client) {
        return false;
    }

    C64_LOG_DEBUG(REST_LOG_PREFIX "Reset machine");
    return http_request_ex(client, "PUT", "/v1/machine:reset", NULL, NULL, 0, NULL, true);
}

bool c64_rest_reboot(c64_rest_client_t *client)
{
    if (!client) {
        return false;
    }

    C64_LOG_DEBUG(REST_LOG_PREFIX "Reboot machine");
    return http_request(client, "PUT", "/v1/machine:reboot", NULL, NULL, 0, NULL);
}

bool c64_rest_pause(c64_rest_client_t *client)
{
    if (!client) {
        return false;
    }

    C64_LOG_DEBUG(REST_LOG_PREFIX "Pause machine");
    return http_request(client, "PUT", "/v1/machine:pause", NULL, NULL, 0, NULL);
}

bool c64_rest_resume(c64_rest_client_t *client)
{
    if (!client) {
        return false;
    }

    C64_LOG_DEBUG(REST_LOG_PREFIX "Resume machine");
    return http_request(client, "PUT", "/v1/machine:resume", NULL, NULL, 0, NULL);
}

bool c64_rest_poweroff(c64_rest_client_t *client)
{
    if (!client) {
        return false;
    }

    C64_LOG_DEBUG(REST_LOG_PREFIX "Power off machine");
    return http_request(client, "PUT", "/v1/machine:poweroff", NULL, NULL, 0, NULL);
}

int c64_rest_read_memory(c64_rest_client_t *client, uint16_t address, size_t length, uint8_t *buffer,
                         size_t buffer_size)
{
    if (!client || !buffer || length > buffer_size) {
        return -1;
    }

    char query[128];
    snprintf(query, sizeof(query), "address=%04X&length=%zu", address, length);

    response_buffer_t response = {0};
    if (!http_request(client, "GET", "/v1/machine:readmem", query, NULL, 0, &response)) {
        free(response.data);
        return -1;
    }

    // Copy response data to output buffer
    size_t copy_size = response.size < buffer_size ? response.size : buffer_size;
    memcpy(buffer, response.data, copy_size);
    free(response.data);

    C64_LOG_DEBUG(REST_LOG_PREFIX "Read memory $%04X: %zu bytes", address, copy_size);
    return (int)copy_size;
}

bool c64_rest_write_memory(c64_rest_client_t *client, uint16_t address, const uint8_t *data, size_t length)
{
    if (!client || !data || length == 0) {
        return false;
    }

    C64_LOG_DEBUG(REST_LOG_PREFIX "Writing %zu bytes to $%04X", length, address);

    // Convert data to hex string
    char *hex_data = malloc(length * 2 + 1);
    if (!hex_data) {
        return false;
    }

    for (size_t i = 0; i < length; i++) {
        sprintf(hex_data + i * 2, "%02X", data[i]);
    }
    hex_data[length * 2] = '\0';

    char query[512];
    snprintf(query, sizeof(query), "address=%04X&data=%s", address, hex_data);
    free(hex_data);

    bool result = http_request(client, "PUT", "/v1/machine:writemem", query, NULL, 0, NULL);

    if (result) {
        C64_LOG_DEBUG(REST_LOG_PREFIX "Successfully wrote memory $%04X: %zu bytes", address, length);
    } else {
        C64_LOG_ERROR(REST_LOG_PREFIX "Failed to write memory $%04X", address);
    }

    return result;
}

bool c64_rest_play_sid(c64_rest_client_t *client, const uint8_t *sid_data, size_t sid_size, int song_number,
                       const uint8_t *songlengths_data, size_t songlengths_size)
{
    if (!client || !sid_data || sid_size == 0) {
        return false;
    }

    // Build URL with song number query parameter
    char path[128];
    snprintf(path, sizeof(path), "/v1/runners:sidplay?songnr=%d", song_number);

    // Build full URL
    char url[512];
    snprintf(url, sizeof(url), "%s%s", client->base_url, path);

    // Reset CURL handle
    curl_easy_reset(client->curl);

    // CRITICAL: Re-set NOSIGNAL after reset to prevent Windows crashes
    curl_easy_setopt(client->curl, CURLOPT_NOSIGNAL, 1L);

    // Create MIME structure (modern API)
    curl_mime *mime = curl_mime_init(client->curl);
    curl_mimepart *part = curl_mime_addpart(mime);
    curl_mime_name(part, "file");
    curl_mime_filename(part, "music.sid");
    curl_mime_data(part, (const char *)sid_data, sid_size);
    curl_mime_type(part, "application/octet-stream");

    if (songlengths_data && songlengths_size > 0) {
        curl_mimepart *songlengths_part = curl_mime_addpart(mime);
        curl_mime_name(songlengths_part, "file");
        curl_mime_filename(songlengths_part, "songlengths.ssl");
        curl_mime_data(songlengths_part, (const char *)songlengths_data, songlengths_size);
        curl_mime_type(songlengths_part, "application/octet-stream");
    }

    // Set CURL options
    curl_easy_setopt(client->curl, CURLOPT_URL, url);
    curl_easy_setopt(client->curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(client->curl, CURLOPT_TIMEOUT, 5L);

    // Add password header if present
    struct curl_slist *headers = NULL;
    if (client->password && client->password[0]) {
        char auth_header[256];
        snprintf(auth_header, sizeof(auth_header), "X-Password: %s", client->password);
        headers = curl_slist_append(headers, auth_header);
        curl_easy_setopt(client->curl, CURLOPT_HTTPHEADER, headers);
    }

    // Perform request
    CURLcode res = curl_easy_perform(client->curl);

    // Cleanup
    curl_mime_free(mime);
    if (headers) {
        curl_slist_free_all(headers);
    }

    if (res != CURLE_OK) {
        snprintf(client->error_msg, sizeof(client->error_msg), "CURL error: %s", curl_easy_strerror(res));
        C64_LOG_ERROR(REST_LOG_PREFIX "%s", client->error_msg);
        return false;
    }

    // Check HTTP status code
    long http_code = 0;
    curl_easy_getinfo(client->curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        snprintf(client->error_msg, sizeof(client->error_msg), "HTTP error %ld", http_code);
        C64_LOG_ERROR(REST_LOG_PREFIX "%s", client->error_msg);
        return false;
    }

    C64_LOG_DEBUG(REST_LOG_PREFIX "Playing SID song=%d size=%zu songlengths=%s", song_number, sid_size,
                  (songlengths_data && songlengths_size > 0) ? "yes" : "no");
    return true;
}

bool c64_rest_run_prg(c64_rest_client_t *client, const uint8_t *prg_data, size_t prg_size)
{
    if (!client || !prg_data || prg_size == 0) {
        return false;
    }

    // Build full URL
    char url[512];
    snprintf(url, sizeof(url), "%s/v1/runners:run_prg", client->base_url);

    // Reset CURL handle
    curl_easy_reset(client->curl);

    // CRITICAL: Re-set NOSIGNAL after reset to prevent Windows crashes
    curl_easy_setopt(client->curl, CURLOPT_NOSIGNAL, 1L);

    // Create MIME structure (modern API)
    curl_mime *mime = curl_mime_init(client->curl);
    curl_mimepart *part = curl_mime_addpart(mime);
    curl_mime_name(part, "file");
    curl_mime_filename(part, "program.prg");
    curl_mime_data(part, (const char *)prg_data, prg_size);
    curl_mime_type(part, "application/octet-stream");

    // Set CURL options
    curl_easy_setopt(client->curl, CURLOPT_URL, url);
    curl_easy_setopt(client->curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(client->curl, CURLOPT_TIMEOUT, 5L);

    // Add password header if present
    struct curl_slist *headers = NULL;
    if (client->password && client->password[0]) {
        char auth_header[256];
        snprintf(auth_header, sizeof(auth_header), "X-Password: %s", client->password);
        headers = curl_slist_append(headers, auth_header);
        curl_easy_setopt(client->curl, CURLOPT_HTTPHEADER, headers);
    }

    // Perform request
    CURLcode res = curl_easy_perform(client->curl);

    // Cleanup
    curl_mime_free(mime);
    if (headers) {
        curl_slist_free_all(headers);
    }

    if (res != CURLE_OK) {
        snprintf(client->error_msg, sizeof(client->error_msg), "CURL error: %s", curl_easy_strerror(res));
        C64_LOG_ERROR(REST_LOG_PREFIX "%s", client->error_msg);
        return false;
    }

    // Check HTTP status code
    long http_code = 0;
    curl_easy_getinfo(client->curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        snprintf(client->error_msg, sizeof(client->error_msg), "HTTP error %ld", http_code);
        C64_LOG_ERROR(REST_LOG_PREFIX "%s", client->error_msg);
        return false;
    }

    C64_LOG_DEBUG(REST_LOG_PREFIX "Running PRG size=%zu", prg_size);
    return true;
}

bool c64_rest_mount_disk(c64_rest_client_t *client, char drive, const char *type, const char *mode,
                         const uint8_t *disk_data, size_t disk_size)
{
    if (!client || !type || !mode || !disk_data || disk_size == 0) {
        return false;
    }

    // Build URL with query parameters
    char path[256];
    snprintf(path, sizeof(path), "/v1/drives/%c:mount?type=%s&mode=%s", drive, type, mode);

    // Build full URL
    char url[512];
    snprintf(url, sizeof(url), "%s%s", client->base_url, path);

    // Reset CURL handle
    curl_easy_reset(client->curl);

    // CRITICAL: Re-set NOSIGNAL after reset to prevent Windows crashes
    curl_easy_setopt(client->curl, CURLOPT_NOSIGNAL, 1L);

    // Create MIME structure (modern API)
    curl_mime *mime = curl_mime_init(client->curl);
    curl_mimepart *part = curl_mime_addpart(mime);
    curl_mime_name(part, "file");

    char filename[32];
    snprintf(filename, sizeof(filename), "disk.%s", type);
    curl_mime_filename(part, filename);
    curl_mime_data(part, (const char *)disk_data, disk_size);
    curl_mime_type(part, "application/octet-stream");

    // Set CURL options
    curl_easy_setopt(client->curl, CURLOPT_URL, url);
    curl_easy_setopt(client->curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(client->curl, CURLOPT_TIMEOUT, 5L);

    // Add password header if present
    struct curl_slist *headers = NULL;
    if (client->password && client->password[0]) {
        char auth_header[256];
        snprintf(auth_header, sizeof(auth_header), "X-Password: %s", client->password);
        headers = curl_slist_append(headers, auth_header);
        curl_easy_setopt(client->curl, CURLOPT_HTTPHEADER, headers);
    }

    // Perform request
    CURLcode res = curl_easy_perform(client->curl);

    // Cleanup
    curl_mime_free(mime);
    if (headers) {
        curl_slist_free_all(headers);
    }

    if (res != CURLE_OK) {
        snprintf(client->error_msg, sizeof(client->error_msg), "CURL error: %s", curl_easy_strerror(res));
        C64_LOG_ERROR(REST_LOG_PREFIX "%s", client->error_msg);
        return false;
    }

    // Check HTTP status code
    long http_code = 0;
    curl_easy_getinfo(client->curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        snprintf(client->error_msg, sizeof(client->error_msg), "HTTP error %ld", http_code);
        C64_LOG_ERROR(REST_LOG_PREFIX "%s", client->error_msg);
        return false;
    }

    C64_LOG_DEBUG(REST_LOG_PREFIX "Mounted disk drive=%c type=%s mode=%s size=%zu", drive, type, mode, disk_size);
    return true;
}

bool c64_rest_play_sid_path(c64_rest_client_t *client, const char *c64u_path, int song_number)
{
    if (!client || !c64u_path) {
        return false;
    }

    const char *device_path = c64_strip_c64u_prefix(c64u_path);
    C64_LOG_DEBUG(REST_LOG_PREFIX "Playing SID from C64U: %s song=%d", device_path, song_number);

    // URL encode the path
    char *escaped_path = curl_easy_escape(client->curl, device_path, 0);
    if (!escaped_path) {
        snprintf(client->error_msg, sizeof(client->error_msg), "Failed to escape path");
        C64_LOG_ERROR(REST_LOG_PREFIX "Failed to escape path: %s", c64u_path);
        return false;
    }

    // Build URL with file parameter (not path)
    char url[512];
    snprintf(url, sizeof(url), "%s/v1/runners:sidplay?file=%s&songnr=%d", client->base_url, escaped_path, song_number);
    curl_free(escaped_path);

    C64_LOG_DEBUG(REST_LOG_PREFIX "SID URL: %s", url);

    // Reset CURL handle
    curl_easy_reset(client->curl);

    // CRITICAL: Re-set NOSIGNAL after reset to prevent Windows crashes
    curl_easy_setopt(client->curl, CURLOPT_NOSIGNAL, 1L);

    curl_easy_setopt(client->curl, CURLOPT_URL, url);
    curl_easy_setopt(client->curl, CURLOPT_CUSTOMREQUEST, "PUT"); // Use PUT not POST
    curl_easy_setopt(client->curl, CURLOPT_TIMEOUT, HTTP_TIMEOUT_SECONDS);

    // Add password header if present
    struct curl_slist *headers = NULL;
    if (client->password && client->password[0]) {
        char auth_header[256];
        snprintf(auth_header, sizeof(auth_header), "X-Password: %s", client->password);
        headers = curl_slist_append(headers, auth_header);
        curl_easy_setopt(client->curl, CURLOPT_HTTPHEADER, headers);
    }

    // Perform request
    CURLcode res = curl_easy_perform(client->curl);

    if (headers) {
        curl_slist_free_all(headers);
    }

    if (res != CURLE_OK) {
        snprintf(client->error_msg, sizeof(client->error_msg), "CURL error: %s", curl_easy_strerror(res));
        C64_LOG_ERROR(REST_LOG_PREFIX "CURL error: %s", curl_easy_strerror(res));
        return false;
    }

    // Check HTTP status
    long http_code = 0;
    curl_easy_getinfo(client->curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        snprintf(client->error_msg, sizeof(client->error_msg), "HTTP error %ld", http_code);
        C64_LOG_ERROR(REST_LOG_PREFIX "HTTP error %ld for SID playback", http_code);
        return false;
    }

    C64_LOG_DEBUG(REST_LOG_PREFIX "✅ SID playback started successfully");
    return true;
}

bool c64_rest_run_prg_path(c64_rest_client_t *client, const char *c64u_path)
{
    if (!client || !c64u_path) {
        return false;
    }

    // URL encode the path
    char *escaped_path = curl_easy_escape(client->curl, c64u_path, 0);
    if (!escaped_path) {
        snprintf(client->error_msg, sizeof(client->error_msg), "Failed to escape path");
        return false;
    }

    // Build URL
    char url[512];
    snprintf(url, sizeof(url), "%s/v1/runners:run_prg?path=%s", client->base_url, escaped_path);
    curl_free(escaped_path);

    // Reset CURL handle
    curl_easy_reset(client->curl);

    // CRITICAL: Re-set NOSIGNAL after reset to prevent Windows crashes
    curl_easy_setopt(client->curl, CURLOPT_NOSIGNAL, 1L);

    curl_easy_setopt(client->curl, CURLOPT_URL, url);
    curl_easy_setopt(client->curl, CURLOPT_POST, 1L);
    curl_easy_setopt(client->curl, CURLOPT_POSTFIELDS, "");
    curl_easy_setopt(client->curl, CURLOPT_TIMEOUT, HTTP_TIMEOUT_SECONDS);

    // Add password header if present
    struct curl_slist *headers = NULL;
    if (client->password && client->password[0]) {
        char auth_header[256];
        snprintf(auth_header, sizeof(auth_header), "X-Password: %s", client->password);
        headers = curl_slist_append(headers, auth_header);
        curl_easy_setopt(client->curl, CURLOPT_HTTPHEADER, headers);
    }

    // Perform request
    CURLcode res = curl_easy_perform(client->curl);

    if (headers) {
        curl_slist_free_all(headers);
    }

    if (res != CURLE_OK) {
        snprintf(client->error_msg, sizeof(client->error_msg), "CURL error: %s", curl_easy_strerror(res));
        return false;
    }

    // Check HTTP status
    long http_code = 0;
    curl_easy_getinfo(client->curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        snprintf(client->error_msg, sizeof(client->error_msg), "HTTP error %ld", http_code);
        return false;
    }

    C64_LOG_DEBUG(REST_LOG_PREFIX "Running PRG from C64U: %s", c64u_path);
    return true;
}

bool c64_rest_play_mod(c64_rest_client_t *client, const uint8_t *mod_data, size_t mod_size)
{
    if (!client || !mod_data || mod_size == 0) {
        return false;
    }

    // Build full URL
    char url[512];
    snprintf(url, sizeof(url), "%s/v1/runners:modplay", client->base_url);

    // Reset CURL handle
    curl_easy_reset(client->curl);

    // CRITICAL: Re-set NOSIGNAL after reset to prevent Windows crashes
    curl_easy_setopt(client->curl, CURLOPT_NOSIGNAL, 1L);

    // Create MIME structure (modern API)
    curl_mime *mime = curl_mime_init(client->curl);
    curl_mimepart *part = curl_mime_addpart(mime);
    curl_mime_name(part, "file");
    curl_mime_filename(part, "module.mod");
    curl_mime_data(part, (const char *)mod_data, mod_size);
    curl_mime_type(part, "application/octet-stream");

    // Set CURL options
    curl_easy_setopt(client->curl, CURLOPT_URL, url);
    curl_easy_setopt(client->curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(client->curl, CURLOPT_TIMEOUT, 5L);

    // Add password header if present
    struct curl_slist *headers = NULL;
    if (client->password && client->password[0]) {
        char auth_header[256];
        snprintf(auth_header, sizeof(auth_header), "X-Password: %s", client->password);
        headers = curl_slist_append(headers, auth_header);
        curl_easy_setopt(client->curl, CURLOPT_HTTPHEADER, headers);
    }

    // Perform request
    CURLcode res = curl_easy_perform(client->curl);

    // Cleanup
    curl_mime_free(mime);
    if (headers) {
        curl_slist_free_all(headers);
    }

    if (res != CURLE_OK) {
        snprintf(client->error_msg, sizeof(client->error_msg), "CURL error: %s", curl_easy_strerror(res));
        C64_LOG_ERROR(REST_LOG_PREFIX "%s", client->error_msg);
        return false;
    }

    // Check HTTP status code
    long http_code = 0;
    curl_easy_getinfo(client->curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        snprintf(client->error_msg, sizeof(client->error_msg), "HTTP error %ld", http_code);
        C64_LOG_ERROR(REST_LOG_PREFIX "%s", client->error_msg);
        return false;
    }

    C64_LOG_DEBUG(REST_LOG_PREFIX "Playing MOD size=%zu", mod_size);
    return true;
}

bool c64_rest_play_mod_path(c64_rest_client_t *client, const char *c64u_path)
{
    if (!client || !c64u_path) {
        return false;
    }

    C64_LOG_DEBUG(REST_LOG_PREFIX "Playing MOD from C64U: %s", c64u_path);

    // URL encode the path
    char *escaped_path = curl_easy_escape(client->curl, c64u_path, 0);
    if (!escaped_path) {
        snprintf(client->error_msg, sizeof(client->error_msg), "Failed to escape path");
        C64_LOG_ERROR(REST_LOG_PREFIX "Failed to escape path: %s", c64u_path);
        return false;
    }

    // Build URL with file parameter
    char url[512];
    snprintf(url, sizeof(url), "%s/v1/runners:modplay?file=%s", client->base_url, escaped_path);
    curl_free(escaped_path);

    // Reset CURL handle
    curl_easy_reset(client->curl);

    // CRITICAL: Re-set NOSIGNAL after reset to prevent Windows crashes
    curl_easy_setopt(client->curl, CURLOPT_NOSIGNAL, 1L);

    curl_easy_setopt(client->curl, CURLOPT_URL, url);
    curl_easy_setopt(client->curl, CURLOPT_CUSTOMREQUEST, "PUT"); // Use PUT not POST
    curl_easy_setopt(client->curl, CURLOPT_TIMEOUT, HTTP_TIMEOUT_SECONDS);

    // Add password header if present
    struct curl_slist *headers = NULL;
    if (client->password && client->password[0]) {
        char auth_header[256];
        snprintf(auth_header, sizeof(auth_header), "X-Password: %s", client->password);
        headers = curl_slist_append(headers, auth_header);
        curl_easy_setopt(client->curl, CURLOPT_HTTPHEADER, headers);
    }

    // Perform request
    CURLcode res = curl_easy_perform(client->curl);

    if (headers) {
        curl_slist_free_all(headers);
    }

    if (res != CURLE_OK) {
        snprintf(client->error_msg, sizeof(client->error_msg), "CURL error: %s", curl_easy_strerror(res));
        C64_LOG_ERROR(REST_LOG_PREFIX "CURL error: %s", curl_easy_strerror(res));
        return false;
    }

    // Check HTTP status
    long http_code = 0;
    curl_easy_getinfo(client->curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        snprintf(client->error_msg, sizeof(client->error_msg), "HTTP error %ld", http_code);
        C64_LOG_ERROR(REST_LOG_PREFIX "HTTP error %ld for MOD playback", http_code);
        return false;
    }

    C64_LOG_DEBUG(REST_LOG_PREFIX "✅ MOD playback started successfully");
    return true;
}

bool c64_rest_run_crt(c64_rest_client_t *client, const uint8_t *crt_data, size_t crt_size)
{
    if (!client || !crt_data || crt_size == 0) {
        return false;
    }

    // Build full URL
    char url[512];
    snprintf(url, sizeof(url), "%s/v1/runners:run_crt", client->base_url);

    // Reset CURL handle
    curl_easy_reset(client->curl);

    // CRITICAL: Re-set NOSIGNAL after reset to prevent Windows crashes
    curl_easy_setopt(client->curl, CURLOPT_NOSIGNAL, 1L);

    // Create MIME structure (modern API)
    curl_mime *mime = curl_mime_init(client->curl);
    curl_mimepart *part = curl_mime_addpart(mime);
    curl_mime_name(part, "file");
    curl_mime_filename(part, "cartridge.crt");
    curl_mime_data(part, (const char *)crt_data, crt_size);
    curl_mime_type(part, "application/octet-stream");

    // Set CURL options
    curl_easy_setopt(client->curl, CURLOPT_URL, url);
    curl_easy_setopt(client->curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(client->curl, CURLOPT_TIMEOUT, 5L);

    // Add password header if present
    struct curl_slist *headers = NULL;
    if (client->password && client->password[0]) {
        char auth_header[256];
        snprintf(auth_header, sizeof(auth_header), "X-Password: %s", client->password);
        headers = curl_slist_append(headers, auth_header);
        curl_easy_setopt(client->curl, CURLOPT_HTTPHEADER, headers);
    }

    // Perform request
    CURLcode res = curl_easy_perform(client->curl);

    // Cleanup
    curl_mime_free(mime);
    if (headers) {
        curl_slist_free_all(headers);
    }

    if (res != CURLE_OK) {
        snprintf(client->error_msg, sizeof(client->error_msg), "CURL error: %s", curl_easy_strerror(res));
        C64_LOG_ERROR(REST_LOG_PREFIX "%s", client->error_msg);
        return false;
    }

    // Check HTTP status code
    long http_code = 0;
    curl_easy_getinfo(client->curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        snprintf(client->error_msg, sizeof(client->error_msg), "HTTP error %ld", http_code);
        C64_LOG_ERROR(REST_LOG_PREFIX "%s", client->error_msg);
        return false;
    }

    C64_LOG_DEBUG(REST_LOG_PREFIX "Running CRT size=%zu", crt_size);
    return true;
}

bool c64_rest_run_crt_path(c64_rest_client_t *client, const char *c64u_path)
{
    if (!client || !c64u_path) {
        return false;
    }

    C64_LOG_DEBUG(REST_LOG_PREFIX "Running CRT from C64U: %s", c64u_path);

    // URL encode the path
    char *escaped_path = curl_easy_escape(client->curl, c64u_path, 0);
    if (!escaped_path) {
        snprintf(client->error_msg, sizeof(client->error_msg), "Failed to escape path");
        C64_LOG_ERROR(REST_LOG_PREFIX "Failed to escape path: %s", c64u_path);
        return false;
    }

    // Build URL with file parameter
    char url[512];
    snprintf(url, sizeof(url), "%s/v1/runners:run_crt?file=%s", client->base_url, escaped_path);
    curl_free(escaped_path);

    // Reset CURL handle
    curl_easy_reset(client->curl);

    // CRITICAL: Re-set NOSIGNAL after reset to prevent Windows crashes
    curl_easy_setopt(client->curl, CURLOPT_NOSIGNAL, 1L);

    curl_easy_setopt(client->curl, CURLOPT_URL, url);
    curl_easy_setopt(client->curl, CURLOPT_CUSTOMREQUEST, "PUT"); // Use PUT not POST
    curl_easy_setopt(client->curl, CURLOPT_TIMEOUT, HTTP_TIMEOUT_SECONDS);

    // Add password header if present
    struct curl_slist *headers = NULL;
    if (client->password && client->password[0]) {
        char auth_header[256];
        snprintf(auth_header, sizeof(auth_header), "X-Password: %s", client->password);
        headers = curl_slist_append(headers, auth_header);
        curl_easy_setopt(client->curl, CURLOPT_HTTPHEADER, headers);
    }

    // Perform request
    CURLcode res = curl_easy_perform(client->curl);

    if (headers) {
        curl_slist_free_all(headers);
    }

    if (res != CURLE_OK) {
        snprintf(client->error_msg, sizeof(client->error_msg), "CURL error: %s", curl_easy_strerror(res));
        C64_LOG_ERROR(REST_LOG_PREFIX "CURL error: %s", curl_easy_strerror(res));
        return false;
    }

    // Check HTTP status
    long http_code = 0;
    curl_easy_getinfo(client->curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        snprintf(client->error_msg, sizeof(client->error_msg), "HTTP error %ld", http_code);
        C64_LOG_ERROR(REST_LOG_PREFIX "HTTP error %ld for CRT execution", http_code);
        return false;
    }

    C64_LOG_DEBUG(REST_LOG_PREFIX "✅ CRT execution started successfully");
    return true;
}

bool c64_rest_mount_disk_path(c64_rest_client_t *client, char drive, const char *c64u_path)
{
    if (!client || !c64u_path) {
        return false;
    }

    // URL encode the path
    char *escaped_path = curl_easy_escape(client->curl, c64u_path, 0);
    if (!escaped_path) {
        snprintf(client->error_msg, sizeof(client->error_msg), "Failed to escape path");
        return false;
    }

    // Build URL
    char url[512];
    snprintf(url, sizeof(url), "%s/v1/drives/%c:mount?path=%s", client->base_url, drive, escaped_path);
    curl_free(escaped_path);

    // Reset CURL handle
    curl_easy_reset(client->curl);

    // CRITICAL: Re-set NOSIGNAL after reset to prevent Windows crashes
    curl_easy_setopt(client->curl, CURLOPT_NOSIGNAL, 1L);

    curl_easy_setopt(client->curl, CURLOPT_URL, url);
    curl_easy_setopt(client->curl, CURLOPT_POST, 1L);
    curl_easy_setopt(client->curl, CURLOPT_POSTFIELDS, "");
    curl_easy_setopt(client->curl, CURLOPT_TIMEOUT, HTTP_TIMEOUT_SECONDS);

    // Add password header if present
    struct curl_slist *headers = NULL;
    if (client->password && client->password[0]) {
        char auth_header[256];
        snprintf(auth_header, sizeof(auth_header), "X-Password: %s", client->password);
        headers = curl_slist_append(headers, auth_header);
        curl_easy_setopt(client->curl, CURLOPT_HTTPHEADER, headers);
    }

    // Perform request
    CURLcode res = curl_easy_perform(client->curl);

    if (headers) {
        curl_slist_free_all(headers);
    }

    if (res != CURLE_OK) {
        snprintf(client->error_msg, sizeof(client->error_msg), "CURL error: %s", curl_easy_strerror(res));
        return false;
    }

    // Check HTTP status
    long http_code = 0;
    curl_easy_getinfo(client->curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        snprintf(client->error_msg, sizeof(client->error_msg), "HTTP error %ld", http_code);
        return false;
    }

    C64_LOG_DEBUG(REST_LOG_PREFIX "Mounted disk from C64U: drive=%c path=%s", drive, c64u_path);
    return true;
}

bool c64_rest_drive_get_property(c64_rest_client_t *client, const char *drive, const char *property, char *out_value,
                                 size_t out_size)
{
    if (!client || !drive || !property || !out_value || out_size == 0) {
        if (client) {
            snprintf(client->error_msg, sizeof(client->error_msg), "Invalid parameters");
        }
        return false;
    }

    char *json = NULL;
    size_t json_len = 0;
    if (!request_json(client, "GET", "/v1/drives", NULL, &json, &json_len)) {
        return false;
    }

    char error_buf[256] = {0};
    if (!json_errors_empty(json, json_len, error_buf, sizeof(error_buf))) {
        snprintf(client->error_msg, sizeof(client->error_msg), "%s", error_buf[0] ? error_buf : "REST errors");
        free(json);
        return false;
    }

    bool ok = parse_drive_property(json, drive, property, out_value, out_size);
    if (!ok) {
        snprintf(client->error_msg, sizeof(client->error_msg), "Failed to parse drive property");
    }

    free(json);
    return ok;
}

bool c64_rest_drive_mount_image(c64_rest_client_t *client, const char *drive, const char *c64u_path, const char *type,
                                const char *mode)
{
    if (!client || !drive || !c64u_path) {
        if (client) {
            snprintf(client->error_msg, sizeof(client->error_msg), "Invalid parameters");
        }
        return false;
    }

    const char *device_path = c64_strip_c64u_prefix(c64u_path);
    char *escaped_path = curl_easy_escape(client->curl, device_path, 0);
    if (!escaped_path) {
        snprintf(client->error_msg, sizeof(client->error_msg), "Failed to escape path");
        return false;
    }

    char *type_enc = type ? curl_easy_escape(client->curl, type, 0) : NULL;
    char *mode_enc = mode ? curl_easy_escape(client->curl, mode, 0) : NULL;

    char endpoint[256];
    snprintf(endpoint, sizeof(endpoint), "/v1/drives/%s:mount", drive);

    char query[512];
    if (type_enc && mode_enc) {
        snprintf(query, sizeof(query), "image=%s&type=%s&mode=%s", escaped_path, type_enc, mode_enc);
    } else {
        snprintf(query, sizeof(query), "image=%s", escaped_path);
    }

    curl_free(escaped_path);
    if (type_enc) {
        curl_free(type_enc);
    }
    if (mode_enc) {
        curl_free(mode_enc);
    }

    return http_request(client, "PUT", endpoint, query, NULL, 0, NULL);
}

bool c64_rest_drive_mount_upload(c64_rest_client_t *client, const char *drive, const char *type, const char *mode,
                                 const uint8_t *data, size_t size)
{
    if (!client || !drive || !data || size == 0) {
        if (client) {
            snprintf(client->error_msg, sizeof(client->error_msg), "Invalid parameters");
        }
        return false;
    }

    char endpoint[256];
    snprintf(endpoint, sizeof(endpoint), "/v1/drives/%s:mount", drive);

    char *type_enc = type ? curl_easy_escape(client->curl, type, 0) : NULL;
    char *mode_enc = mode ? curl_easy_escape(client->curl, mode, 0) : NULL;

    char url[512];
    if (type_enc && mode_enc) {
        snprintf(url, sizeof(url), "%s%s?type=%s&mode=%s", client->base_url, endpoint, type_enc, mode_enc);
    } else {
        snprintf(url, sizeof(url), "%s%s", client->base_url, endpoint);
    }

    if (type_enc) {
        curl_free(type_enc);
    }
    if (mode_enc) {
        curl_free(mode_enc);
    }

    curl_easy_reset(client->curl);
    curl_easy_setopt(client->curl, CURLOPT_NOSIGNAL, 1L);

    curl_mime *mime = curl_mime_init(client->curl);
    curl_mimepart *part = curl_mime_addpart(mime);
    curl_mime_name(part, "file");
    curl_mime_filename(part, "disk.img");
    curl_mime_data(part, (const char *)data, size);
    curl_mime_type(part, "application/octet-stream");

    curl_easy_setopt(client->curl, CURLOPT_URL, url);
    curl_easy_setopt(client->curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(client->curl, CURLOPT_TIMEOUT, HTTP_TIMEOUT_SECONDS);

    struct curl_slist *headers = NULL;
    if (client->password && client->password[0]) {
        char auth_header[256];
        snprintf(auth_header, sizeof(auth_header), "X-Password: %s", client->password);
        headers = curl_slist_append(headers, auth_header);
        curl_easy_setopt(client->curl, CURLOPT_HTTPHEADER, headers);
    }

    CURLcode res = curl_easy_perform(client->curl);

    curl_mime_free(mime);
    if (headers) {
        curl_slist_free_all(headers);
    }

    if (res != CURLE_OK) {
        snprintf(client->error_msg, sizeof(client->error_msg), "CURL error: %s", curl_easy_strerror(res));
        return false;
    }

    long http_code = 0;
    curl_easy_getinfo(client->curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code < 200 || http_code >= 300) {
        snprintf(client->error_msg, sizeof(client->error_msg), "HTTP error %ld", http_code);
        return false;
    }

    return true;
}

bool c64_rest_drive_unmount(c64_rest_client_t *client, const char *drive)
{
    if (!client || !drive) {
        if (client) {
            snprintf(client->error_msg, sizeof(client->error_msg), "Invalid parameters");
        }
        return false;
    }

    char endpoint[256];
    snprintf(endpoint, sizeof(endpoint), "/v1/drives/%s:remove", drive);
    return http_request(client, "PUT", endpoint, NULL, NULL, 0, NULL);
}

bool c64_rest_drive_reset(c64_rest_client_t *client, const char *drive)
{
    if (!client || !drive) {
        if (client) {
            snprintf(client->error_msg, sizeof(client->error_msg), "Invalid parameters");
        }
        return false;
    }

    char endpoint[256];
    snprintf(endpoint, sizeof(endpoint), "/v1/drives/%s:reset", drive);
    return http_request(client, "PUT", endpoint, NULL, NULL, 0, NULL);
}

bool c64_rest_drive_on(c64_rest_client_t *client, const char *drive)
{
    if (!client || !drive) {
        if (client) {
            snprintf(client->error_msg, sizeof(client->error_msg), "Invalid parameters");
        }
        return false;
    }

    char endpoint[256];
    snprintf(endpoint, sizeof(endpoint), "/v1/drives/%s:on", drive);
    return http_request(client, "PUT", endpoint, NULL, NULL, 0, NULL);
}

bool c64_rest_drive_off(c64_rest_client_t *client, const char *drive)
{
    if (!client || !drive) {
        if (client) {
            snprintf(client->error_msg, sizeof(client->error_msg), "Invalid parameters");
        }
        return false;
    }

    char endpoint[256];
    snprintf(endpoint, sizeof(endpoint), "/v1/drives/%s:off", drive);
    return http_request(client, "PUT", endpoint, NULL, NULL, 0, NULL);
}

bool c64_rest_drive_load_rom_image(c64_rest_client_t *client, const char *drive, const char *c64u_path)
{
    if (!client || !drive || !c64u_path) {
        if (client) {
            snprintf(client->error_msg, sizeof(client->error_msg), "Invalid parameters");
        }
        return false;
    }

    const char *device_path = c64_strip_c64u_prefix(c64u_path);
    char *escaped_path = curl_easy_escape(client->curl, device_path, 0);
    if (!escaped_path) {
        snprintf(client->error_msg, sizeof(client->error_msg), "Failed to escape path");
        return false;
    }

    char endpoint[256];
    snprintf(endpoint, sizeof(endpoint), "/v1/drives/%s:load_rom", drive);

    char query[512];
    snprintf(query, sizeof(query), "file=%s", escaped_path);
    curl_free(escaped_path);

    return http_request(client, "PUT", endpoint, query, NULL, 0, NULL);
}

bool c64_rest_drive_load_rom_upload(c64_rest_client_t *client, const char *drive, const uint8_t *data, size_t size)
{
    if (!client || !drive || !data || size == 0) {
        if (client) {
            snprintf(client->error_msg, sizeof(client->error_msg), "Invalid parameters");
        }
        return false;
    }

    char endpoint[256];
    snprintf(endpoint, sizeof(endpoint), "/v1/drives/%s:load_rom", drive);

    char url[512];
    snprintf(url, sizeof(url), "%s%s", client->base_url, endpoint);

    curl_easy_reset(client->curl);
    curl_easy_setopt(client->curl, CURLOPT_NOSIGNAL, 1L);

    curl_mime *mime = curl_mime_init(client->curl);
    curl_mimepart *part = curl_mime_addpart(mime);
    curl_mime_name(part, "file");
    curl_mime_filename(part, "drive.rom");
    curl_mime_data(part, (const char *)data, size);
    curl_mime_type(part, "application/octet-stream");

    curl_easy_setopt(client->curl, CURLOPT_URL, url);
    curl_easy_setopt(client->curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(client->curl, CURLOPT_TIMEOUT, HTTP_TIMEOUT_SECONDS);

    struct curl_slist *headers = NULL;
    if (client->password && client->password[0]) {
        char auth_header[256];
        snprintf(auth_header, sizeof(auth_header), "X-Password: %s", client->password);
        headers = curl_slist_append(headers, auth_header);
        curl_easy_setopt(client->curl, CURLOPT_HTTPHEADER, headers);
    }

    CURLcode res = curl_easy_perform(client->curl);

    curl_mime_free(mime);
    if (headers) {
        curl_slist_free_all(headers);
    }

    if (res != CURLE_OK) {
        snprintf(client->error_msg, sizeof(client->error_msg), "CURL error: %s", curl_easy_strerror(res));
        return false;
    }

    long http_code = 0;
    curl_easy_getinfo(client->curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code < 200 || http_code >= 300) {
        snprintf(client->error_msg, sizeof(client->error_msg), "HTTP error %ld", http_code);
        return false;
    }

    return true;
}

bool c64_rest_drive_set_mode(c64_rest_client_t *client, const char *drive, const char *mode)
{
    if (!client || !drive || !mode) {
        if (client) {
            snprintf(client->error_msg, sizeof(client->error_msg), "Invalid parameters");
        }
        return false;
    }

    char endpoint[256];
    snprintf(endpoint, sizeof(endpoint), "/v1/drives/%s:set_mode", drive);

    char *mode_enc = curl_easy_escape(client->curl, mode, 0);
    if (!mode_enc) {
        snprintf(client->error_msg, sizeof(client->error_msg), "Failed to escape mode");
        return false;
    }

    char query[256];
    snprintf(query, sizeof(query), "mode=%s", mode_enc);
    curl_free(mode_enc);

    return http_request(client, "PUT", endpoint, query, NULL, 0, NULL);
}

static bool parse_config_item_value(const char *json, const char *category, const char *item, char *out_value,
                                    size_t out_size)
{
    if (!json || !category || !item || !out_value || out_size == 0) {
        return false;
    }

    const char *obj_start = NULL;
    const char *obj_end = NULL;
    if (!find_object_for_key(json, category, &obj_start, &obj_end)) {
        return false;
    }

    const char *p = obj_start;
    int depth = 0;
    while (p < obj_end) {
        if (*p == '"') {
            const char *key_start = p + 1;
            const char *key_end = key_start;
            while (key_end < obj_end) {
                if (*key_end == '\\') {
                    key_end += (key_end + 1 < obj_end) ? 2 : 1;
                    continue;
                }
                if (*key_end == '"') {
                    break;
                }
                key_end++;
            }

            if (depth == 1 && key_end < obj_end) {
                size_t key_len = (size_t)(key_end - key_start);
                if (strlen(item) == key_len && strncmp(key_start, item, key_len) == 0) {
                    const char *after_key = key_end + 1;
                    while (after_key < obj_end && isspace((unsigned char)*after_key)) {
                        after_key++;
                    }
                    if (after_key >= obj_end || *after_key != ':') {
                        return false;
                    }
                    const char *val = after_key + 1;
                    while (val < obj_end && isspace((unsigned char)*val)) {
                        val++;
                    }
                    if (val >= obj_end) {
                        return false;
                    }

                    if (*val == '"') {
                        const char *val_start = val + 1;
                        const char *val_end = val_start;
                        while (val_end < obj_end) {
                            if (*val_end == '\\') {
                                val_end += (val_end + 1 < obj_end) ? 2 : 1;
                                continue;
                            }
                            if (*val_end == '"') {
                                break;
                            }
                            val_end++;
                        }
                        json_copy_string(val_start, val_end, out_value, out_size);
                        return true;
                    }

                    if (*val == '{') {
                        const char *nested_start = val;
                        int nested_depth = 0;
                        const char *nested_end = NULL;
                        const char *scan = val;
                        while (scan < obj_end) {
                            if (*scan == '"') {
                                scan = skip_json_string(scan, obj_end);
                                continue;
                            }
                            if (*scan == '{') {
                                nested_depth++;
                            } else if (*scan == '}') {
                                nested_depth--;
                                if (nested_depth == 0) {
                                    nested_end = scan;
                                    break;
                                }
                            }
                            scan++;
                        }
                        if (!nested_end) {
                            return false;
                        }

                        const char *current_key = strstr(nested_start, "\"current\"");
                        if (!current_key || current_key > nested_end) {
                            return false;
                        }
                        const char *current_val = strchr(current_key, ':');
                        if (!current_val || current_val > nested_end) {
                            return false;
                        }
                        current_val++;
                        while (current_val < nested_end && isspace((unsigned char)*current_val)) {
                            current_val++;
                        }
                        if (current_val >= nested_end) {
                            return false;
                        }
                        if (*current_val == '"') {
                            const char *val_start = current_val + 1;
                            const char *val_end = val_start;
                            while (val_end < nested_end) {
                                if (*val_end == '\\') {
                                    val_end += (val_end + 1 < nested_end) ? 2 : 1;
                                    continue;
                                }
                                if (*val_end == '"') {
                                    break;
                                }
                                val_end++;
                            }
                            json_copy_string(val_start, val_end, out_value, out_size);
                            return true;
                        }
                        const char *val_end = current_val;
                        while (val_end < nested_end && !isspace((unsigned char)*val_end) && *val_end != ',' &&
                               *val_end != '}') {
                            val_end++;
                        }
                        json_copy_string(current_val, val_end, out_value, out_size);
                        return true;
                    }

                    if (isdigit((unsigned char)*val) || *val == '-') {
                        const char *val_end = val;
                        while (val_end < obj_end && (isdigit((unsigned char)*val_end) || *val_end == '.' ||
                                                     *val_end == '-' || *val_end == '+')) {
                            val_end++;
                        }
                        json_copy_string(val, val_end, out_value, out_size);
                        return true;
                    }

                    if (strncmp(val, "true", 4) == 0) {
                        const char true_literal[] = "true";
                        json_copy_string(true_literal, true_literal + (sizeof(true_literal) - 1), out_value, out_size);
                        return true;
                    }
                    if (strncmp(val, "false", 5) == 0) {
                        const char false_literal[] = "false";
                        json_copy_string(false_literal, false_literal + (sizeof(false_literal) - 1), out_value,
                                         out_size);
                        return true;
                    }

                    return false;
                }
            }

            p = (key_end < obj_end) ? key_end + 1 : obj_end;
            continue;
        }

        if (*p == '{') {
            depth++;
        } else if (*p == '}') {
            depth--;
            if (depth == 0) {
                break;
            }
        }
        p++;
    }

    return false;
}

static bool parse_config_options(const char *json, const char *category, const char *item, char ***options,
                                 size_t *count)
{
    if (!json || !category || !item || !options || !count) {
        return false;
    }

    const char *obj_start = NULL;
    const char *obj_end = NULL;
    if (!find_object_for_key(json, category, &obj_start, &obj_end)) {
        return false;
    }

    const char *p = obj_start;
    int depth = 0;
    while (p < obj_end) {
        if (*p == '"') {
            const char *key_start = p + 1;
            const char *key_end = key_start;
            while (key_end < obj_end) {
                if (*key_end == '\\') {
                    key_end += (key_end + 1 < obj_end) ? 2 : 1;
                    continue;
                }
                if (*key_end == '"') {
                    break;
                }
                key_end++;
            }

            if (depth == 1 && key_end < obj_end) {
                size_t key_len = (size_t)(key_end - key_start);
                if (strlen(item) == key_len && strncmp(key_start, item, key_len) == 0) {
                    const char *after_key = key_end + 1;
                    while (after_key < obj_end && isspace((unsigned char)*after_key)) {
                        after_key++;
                    }
                    if (after_key >= obj_end || *after_key != ':') {
                        return false;
                    }
                    const char *val = after_key + 1;
                    while (val < obj_end && isspace((unsigned char)*val)) {
                        val++;
                    }
                    if (val >= obj_end || *val != '{') {
                        return false;
                    }
                    const char *nested_start = val;
                    int nested_depth = 0;
                    const char *nested_end = NULL;
                    const char *scan = val;
                    while (scan < obj_end) {
                        if (*scan == '"') {
                            scan = skip_json_string(scan, obj_end);
                            continue;
                        }
                        if (*scan == '{') {
                            nested_depth++;
                        } else if (*scan == '}') {
                            nested_depth--;
                            if (nested_depth == 0) {
                                nested_end = scan;
                                break;
                            }
                        }
                        scan++;
                    }
                    if (!nested_end) {
                        return false;
                    }

                    size_t nested_len = (size_t)(nested_end - nested_start + 1);
                    char *nested_json = malloc(nested_len + 1);
                    if (!nested_json) {
                        return false;
                    }
                    memcpy(nested_json, nested_start, nested_len);
                    nested_json[nested_len] = '\0';

                    bool ok = parse_json_string_array(nested_json, nested_len, "values", options, count);
                    if (!ok) {
                        ok = parse_json_string_array(nested_json, nested_len, "options", options, count);
                    }
                    free(nested_json);
                    return ok;
                }
            }

            p = (key_end < obj_end) ? key_end + 1 : obj_end;
            continue;
        }

        if (*p == '{') {
            depth++;
        } else if (*p == '}') {
            depth--;
            if (depth == 0) {
                break;
            }
        }
        p++;
    }

    return false;
}

bool c64_rest_config_get_value(c64_rest_client_t *client, const char *category, const char *item, char *out_value,
                               size_t out_size)
{
    if (!client || !category || !item || !out_value || out_size == 0) {
        if (client) {
            snprintf(client->error_msg, sizeof(client->error_msg), "Invalid parameters");
        }
        return false;
    }

    char *cat_enc = curl_easy_escape(client->curl, category, 0);
    char *item_enc = curl_easy_escape(client->curl, item, 0);
    if (!cat_enc || !item_enc) {
        if (cat_enc) {
            curl_free(cat_enc);
        }
        if (item_enc) {
            curl_free(item_enc);
        }
        snprintf(client->error_msg, sizeof(client->error_msg), "Failed to escape config path");
        return false;
    }

    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "/v1/configs/%s/%s", cat_enc, item_enc);
    curl_free(cat_enc);
    curl_free(item_enc);

    char *json = NULL;
    size_t json_len = 0;
    if (!request_json(client, "GET", endpoint, NULL, &json, &json_len)) {
        return false;
    }

    char error_buf[256] = {0};
    if (!json_errors_empty(json, json_len, error_buf, sizeof(error_buf))) {
        snprintf(client->error_msg, sizeof(client->error_msg), "%s", error_buf[0] ? error_buf : "REST errors");
        free(json);
        return false;
    }

    bool ok = parse_config_item_value(json, category, item, out_value, out_size);
    if (!ok) {
        snprintf(client->error_msg, sizeof(client->error_msg), "Failed to parse config value");
    }
    free(json);
    return ok;
}

bool c64_rest_config_set_value(c64_rest_client_t *client, const char *category, const char *item, const char *value)
{
    if (!client || !category || !item || !value) {
        if (client) {
            snprintf(client->error_msg, sizeof(client->error_msg), "Invalid parameters");
        }
        return false;
    }

    char *cat_enc = curl_easy_escape(client->curl, category, 0);
    char *item_enc = curl_easy_escape(client->curl, item, 0);
    char *val_enc = curl_easy_escape(client->curl, value, 0);
    if (!cat_enc || !item_enc || !val_enc) {
        if (cat_enc) {
            curl_free(cat_enc);
        }
        if (item_enc) {
            curl_free(item_enc);
        }
        if (val_enc) {
            curl_free(val_enc);
        }
        snprintf(client->error_msg, sizeof(client->error_msg), "Failed to escape config path");
        return false;
    }

    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "/v1/configs/%s/%s", cat_enc, item_enc);
    curl_free(cat_enc);
    curl_free(item_enc);

    char query[512];
    snprintf(query, sizeof(query), "value=%s", val_enc);
    curl_free(val_enc);

    char *json = NULL;
    size_t json_len = 0;
    if (!request_json(client, "PUT", endpoint, query, &json, &json_len)) {
        return false;
    }

    char error_buf[256] = {0};
    bool ok = json_errors_empty(json, json_len, error_buf, sizeof(error_buf));
    if (!ok) {
        snprintf(client->error_msg, sizeof(client->error_msg), "%s", error_buf[0] ? error_buf : "REST errors");
    }
    free(json);
    return ok;
}

bool c64_rest_config_list(c64_rest_client_t *client, const char *category, char ***items, size_t *count)
{
    if (!client || !items || !count) {
        if (client) {
            snprintf(client->error_msg, sizeof(client->error_msg), "Invalid parameters");
        }
        return false;
    }

    char endpoint[512];
    endpoint[0] = '\0';
    if (category && category[0]) {
        char *cat_enc = curl_easy_escape(client->curl, category, 0);
        if (!cat_enc) {
            snprintf(client->error_msg, sizeof(client->error_msg), "Failed to escape config path");
            return false;
        }
        snprintf(endpoint, sizeof(endpoint), "/v1/configs/%s", cat_enc);
        curl_free(cat_enc);
    } else {
        snprintf(endpoint, sizeof(endpoint), "/v1/configs");
    }

    char *json = NULL;
    size_t json_len = 0;
    if (!request_json(client, "GET", endpoint, NULL, &json, &json_len)) {
        return false;
    }

    char error_buf[256] = {0};
    if (!json_errors_empty(json, json_len, error_buf, sizeof(error_buf))) {
        snprintf(client->error_msg, sizeof(client->error_msg), "%s", error_buf[0] ? error_buf : "REST errors");
        free(json);
        return false;
    }

    bool ok = false;
    if (!category || !category[0]) {
        ok = parse_json_string_array(json, json_len, "categories", items, count);
    } else {
        const char *obj_start = NULL;
        const char *obj_end = NULL;
        if (find_object_for_key(json, category, &obj_start, &obj_end)) {
            ok = parse_object_keys(obj_start, obj_end, items, count);
        }
    }

    if (!ok) {
        const char *cat_label = (category && category[0]) ? category : "<root>";
        snprintf(client->error_msg, sizeof(client->error_msg), "Failed to parse config list (category=%s, json=%.200s)",
                 cat_label, json ? json : "<null>");
    }
    free(json);
    return ok;
}

bool c64_rest_config_list_options(c64_rest_client_t *client, const char *category, const char *item, char ***options,
                                  size_t *count)
{
    if (!client || !category || !item || !options || !count) {
        if (client) {
            snprintf(client->error_msg, sizeof(client->error_msg), "Invalid parameters");
        }
        return false;
    }

    char *cat_enc = curl_easy_escape(client->curl, category, 0);
    char *item_enc = curl_easy_escape(client->curl, item, 0);
    if (!cat_enc || !item_enc) {
        if (cat_enc) {
            curl_free(cat_enc);
        }
        if (item_enc) {
            curl_free(item_enc);
        }
        snprintf(client->error_msg, sizeof(client->error_msg), "Failed to escape config path");
        return false;
    }

    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "/v1/configs/%s/%s", cat_enc, item_enc);
    curl_free(cat_enc);
    curl_free(item_enc);

    char *json = NULL;
    size_t json_len = 0;
    if (!request_json(client, "GET", endpoint, NULL, &json, &json_len)) {
        return false;
    }

    char error_buf[256] = {0};
    if (!json_errors_empty(json, json_len, error_buf, sizeof(error_buf))) {
        snprintf(client->error_msg, sizeof(client->error_msg), "%s", error_buf[0] ? error_buf : "REST errors");
        free(json);
        return false;
    }

    bool ok = parse_config_options(json, category, item, options, count);
    if (!ok) {
        snprintf(client->error_msg, sizeof(client->error_msg), "Failed to parse config options");
    }
    free(json);
    return ok;
}

bool c64_rest_config_save(c64_rest_client_t *client)
{
    if (!client) {
        return false;
    }

    C64_LOG_DEBUG(REST_LOG_PREFIX "Save config to flash");
    return http_request(client, "PUT", "/v1/configs:save_to_flash", NULL, NULL, 0, NULL);
}

bool c64_rest_config_load(c64_rest_client_t *client)
{
    if (!client) {
        return false;
    }

    C64_LOG_DEBUG(REST_LOG_PREFIX "Load config from flash");
    return http_request(client, "PUT", "/v1/configs:load_from_flash", NULL, NULL, 0, NULL);
}

bool c64_rest_config_reset(c64_rest_client_t *client)
{
    if (!client) {
        return false;
    }

    C64_LOG_DEBUG(REST_LOG_PREFIX "Reset config to defaults");
    return http_request(client, "PUT", "/v1/configs:reset_to_default", NULL, NULL, 0, NULL);
}

void c64_rest_string_list_free(char **items, size_t count)
{
    if (!items) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        free(items[i]);
    }
    free(items);
}

const char *c64_rest_get_error(c64_rest_client_t *client)
{
    if (!client) {
        return "Invalid client";
    }
#ifdef _MSC_VER
    static __declspec(thread) char error_copy[2048];
#else
    static __thread char error_copy[2048];
#endif
    pthread_mutex_lock(&client->mutex);
    snprintf(error_copy, sizeof(error_copy), "%s", client->error_msg);
    pthread_mutex_unlock(&client->mutex);
    return error_copy;
}

long c64_rest_get_last_status(const c64_rest_client_t *client)
{
    if (!client) {
        return 0;
    }
    pthread_mutex_lock((pthread_mutex_t *)&client->mutex);
    const long status = client->last_status;
    pthread_mutex_unlock((pthread_mutex_t *)&client->mutex);
    return status;
}

c64_rest_outcome_t c64_rest_get_last_outcome(const c64_rest_client_t *client)
{
    if (!client) {
        return C64_REST_UNREACHABLE;
    }
    pthread_mutex_lock((pthread_mutex_t *)&client->mutex);
    const c64_rest_outcome_t outcome = client->last_outcome;
    pthread_mutex_unlock((pthread_mutex_t *)&client->mutex);
    return outcome;
}

// Simple JSON parser for file list response
// Expected format: {"entries": [{"name": "...", "type": "file|directory", "size": 123}, ...]}
static bool parse_file_list_json(const char *json, size_t json_len, c64_file_entry_t **entries, size_t *entry_count)
{
    if (!json || !entries || !entry_count) {
        return false;
    }

    *entries = NULL;
    *entry_count = 0;

    // Find "entries" array
    const char *entries_start = strstr(json, "\"entries\"");
    if (!entries_start) {
        return false; // No entries array
    }

    // Find the array opening bracket
    const char *array_start = strchr(entries_start, '[');
    if (!array_start) {
        return false;
    }

    // Count entries by counting opening braces
    size_t count = 0;
    const char *p = array_start + 1;
    const char *json_end = json + json_len;
    while (p < json_end && *p != ']') {
        if (*p == '{') {
            count++;
        }
        p++;
    }

    if (count == 0) {
        return true; // Empty directory
    }

    // Allocate array
    *entries = calloc(count, sizeof(c64_file_entry_t));
    if (!*entries) {
        return false;
    }

    // Parse each entry
    p = array_start + 1;
    size_t idx = 0;
    while (p < json_end && *p != ']' && idx < count) {
        // Find next object
        const char *obj_start = strchr(p, '{');
        if (!obj_start || obj_start >= json_end) {
            break;
        }
        const char *obj_end = strchr(obj_start, '}');
        if (!obj_end || obj_end >= json_end) {
            break;
        }

        c64_file_entry_t *entry = &(*entries)[idx];

        // Extract name
        const char *name_key = strstr(obj_start, "\"name\"");
        if (name_key && name_key < obj_end) {
            const char *name_val = strchr(name_key + 6, '"');
            if (name_val && name_val < obj_end) {
                const char *name_end = strchr(name_val + 1, '"');
                if (name_end && name_end < obj_end) {
                    size_t name_len = name_end - (name_val + 1);
                    if (name_len >= sizeof(entry->name)) {
                        name_len = sizeof(entry->name) - 1;
                    }
                    memcpy(entry->name, name_val + 1, name_len);
                    entry->name[name_len] = '\0';
                }
            }
        }

        // Extract type
        const char *type_key = strstr(obj_start, "\"type\"");
        if (type_key && type_key < obj_end) {
            const char *type_val = strchr(type_key + 6, '"');
            if (type_val && type_val < obj_end) {
                entry->is_directory = (strncmp(type_val + 1, "directory", 9) == 0);
            }
        }

        // Extract size
        const char *size_key = strstr(obj_start, "\"size\"");
        if (size_key && size_key < obj_end) {
            const char *size_val = strchr(size_key + 6, ':');
            if (size_val && size_val < obj_end) {
                entry->size = (uint32_t)strtoul(size_val + 1, NULL, 10);
            }
        }

        idx++;
        p = obj_end + 1;
    }

    *entry_count = idx;
    return true;
}

bool c64_rest_list_files(c64_rest_client_t *client, const char *path, bool recursive, c64_file_entry_t **entries,
                         size_t *entry_count)
{
    if (!client || !path || !entries || !entry_count) {
        if (client) {
            snprintf(client->error_msg, sizeof(client->error_msg), "Invalid parameters");
        }
        return false;
    }

    *entries = NULL;
    *entry_count = 0;

    // URL encode the path
    char *escaped_path = curl_easy_escape(client->curl, path, 0);
    if (!escaped_path) {
        snprintf(client->error_msg, sizeof(client->error_msg), "Failed to escape path");
        return false;
    }

    // Build URL
    char url[512];
    snprintf(url, sizeof(url), "%s/v1/files:list?path=%s&recursive=%s", client->base_url, escaped_path,
             recursive ? "true" : "false");
    curl_free(escaped_path);

    // Reset CURL handle
    curl_easy_reset(client->curl);

    // CRITICAL: Re-set NOSIGNAL after reset to prevent Windows crashes
    curl_easy_setopt(client->curl, CURLOPT_NOSIGNAL, 1L);

    curl_easy_setopt(client->curl, CURLOPT_URL, url);
    curl_easy_setopt(client->curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(client->curl, CURLOPT_TIMEOUT, HTTP_TIMEOUT_SECONDS);

    // Setup response buffer
    response_buffer_t resp = {0};
    curl_easy_setopt(client->curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(client->curl, CURLOPT_WRITEDATA, &resp);

    // Add password header if present
    struct curl_slist *headers = NULL;
    if (client->password && client->password[0]) {
        char auth_header[256];
        snprintf(auth_header, sizeof(auth_header), "X-Password: %s", client->password);
        headers = curl_slist_append(headers, auth_header);
        curl_easy_setopt(client->curl, CURLOPT_HTTPHEADER, headers);
    }

    // Perform request
    CURLcode res = curl_easy_perform(client->curl);

    if (headers) {
        curl_slist_free_all(headers);
    }

    if (res != CURLE_OK) {
        snprintf(client->error_msg, sizeof(client->error_msg), "CURL error: %s", curl_easy_strerror(res));
        free(resp.data);
        return false;
    }

    // Check HTTP status
    long http_code = 0;
    curl_easy_getinfo(client->curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        snprintf(client->error_msg, sizeof(client->error_msg), "HTTP error %ld", http_code);
        free(resp.data);
        return false;
    }

    // Parse JSON response
    bool success = false;
    if (resp.data && resp.size > 0) {
        success = parse_file_list_json((const char *)resp.data, resp.size, entries, entry_count);
        if (!success && client) {
            snprintf(client->error_msg, sizeof(client->error_msg), "Failed to parse JSON response");
        }
    }

    free(resp.data);
    return success;
}

bool c64_rest_stat_file(c64_rest_client_t *client, const char *path, bool *is_directory)
{
    if (!client || !path) {
        if (client) {
            snprintf(client->error_msg, sizeof(client->error_msg), "Invalid parameters");
        }
        return false;
    }

    // URL encode the path
    char *escaped_path = curl_easy_escape(client->curl, path, 0);
    if (!escaped_path) {
        snprintf(client->error_msg, sizeof(client->error_msg), "Failed to escape path");
        return false;
    }

    // Build URL
    char url[512];
    snprintf(url, sizeof(url), "%s/v1/files:stat?path=%s", client->base_url, escaped_path);
    curl_free(escaped_path);

    // Reset CURL handle
    curl_easy_reset(client->curl);

    // CRITICAL: Re-set NOSIGNAL after reset to prevent Windows crashes
    curl_easy_setopt(client->curl, CURLOPT_NOSIGNAL, 1L);

    curl_easy_setopt(client->curl, CURLOPT_URL, url);
    curl_easy_setopt(client->curl, CURLOPT_NOBODY, 1L); // HEAD request
    curl_easy_setopt(client->curl, CURLOPT_TIMEOUT, HTTP_TIMEOUT_SECONDS);

    // Add password header if present
    struct curl_slist *headers = NULL;
    if (client->password && client->password[0]) {
        char auth_header[256];
        snprintf(auth_header, sizeof(auth_header), "X-Password: %s", client->password);
        headers = curl_slist_append(headers, auth_header);
        curl_easy_setopt(client->curl, CURLOPT_HTTPHEADER, headers);
    }

    // Perform request
    CURLcode res = curl_easy_perform(client->curl);

    if (headers) {
        curl_slist_free_all(headers);
    }

    if (res != CURLE_OK) {
        snprintf(client->error_msg, sizeof(client->error_msg), "CURL error: %s", curl_easy_strerror(res));
        return false;
    }

    // Check HTTP status
    long http_code = 0;
    curl_easy_getinfo(client->curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code == 404) {
        snprintf(client->error_msg, sizeof(client->error_msg), "Path not found");
        return false;
    }
    if (http_code != 200) {
        snprintf(client->error_msg, sizeof(client->error_msg), "HTTP error %ld", http_code);
        return false;
    }

    // Check X-File-Type header if is_directory requested
    if (is_directory) {
        *is_directory = false; // Default to file
        // Note: We'd need to capture headers to read X-File-Type
        // For now, assume file unless we enhance header capturing
    }

    return true;
}
