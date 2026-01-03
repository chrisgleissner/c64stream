/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-rest-client.h"
#include "c64-logging.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REST_LOG_PREFIX "[c64-rest] "
#define HTTP_TIMEOUT_SECONDS 5

struct c64_rest_client {
    char *base_url;
    char *password;
    char error_msg[512];
    CURL *curl;
};

// Callback for capturing HTTP response data
typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
} response_buffer_t;

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
        return NULL;
    }

    c64_rest_client_t *client = calloc(1, sizeof(c64_rest_client_t));
    if (!client) {
        return NULL;
    }

    client->base_url = strdup(base_url);
    if (password) {
        client->password = strdup(password);
    }

    // Initialize curl handle
    client->curl = curl_easy_init();
    if (!client->curl) {
        free(client->base_url);
        free(client->password);
        free(client);
        return NULL;
    }

    // Set common curl options
    curl_easy_setopt(client->curl, CURLOPT_TIMEOUT, HTTP_TIMEOUT_SECONDS);
    curl_easy_setopt(client->curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(client->curl, CURLOPT_FOLLOWLOCATION, 1L);

    C64_LOG_INFO(REST_LOG_PREFIX "Created REST client for %s", base_url);
    return client;
}

void c64_rest_client_destroy(c64_rest_client_t *client)
{
    if (!client) {
        return;
    }

    if (client->curl) {
        curl_easy_cleanup(client->curl);
    }
    free(client->base_url);
    free(client->password);
    free(client);
}

// Perform HTTP request
static bool http_request(c64_rest_client_t *client, const char *method, const char *endpoint, const char *query_params,
                         const uint8_t *body_data, size_t body_size, response_buffer_t *response)
{
    if (!client || !client->curl || !method || !endpoint) {
        return false;
    }

    char url[1024];
    if (query_params) {
        snprintf(url, sizeof(url), "%s%s?%s", client->base_url, endpoint, query_params);
    } else {
        snprintf(url, sizeof(url), "%s%s", client->base_url, endpoint);
    }

    // Reset curl for new request
    curl_easy_reset(client->curl);
    curl_easy_setopt(client->curl, CURLOPT_TIMEOUT, HTTP_TIMEOUT_SECONDS);
    curl_easy_setopt(client->curl, CURLOPT_NOSIGNAL, 1L);
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

    if (headers) {
        curl_easy_setopt(client->curl, CURLOPT_HTTPHEADER, headers);
    }

    // Set response callback if needed
    if (response) {
        curl_easy_setopt(client->curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(client->curl, CURLOPT_WRITEDATA, response);
    }

    // Perform request
    CURLcode res = curl_easy_perform(client->curl);

    // Clean up headers
    if (headers) {
        curl_slist_free_all(headers);
    }

    if (res != CURLE_OK) {
        snprintf(client->error_msg, sizeof(client->error_msg), "HTTP request failed: %s", curl_easy_strerror(res));
        C64_LOG_ERROR(REST_LOG_PREFIX "%s", client->error_msg);
        return false;
    }

    // Check HTTP status code
    long http_code = 0;
    curl_easy_getinfo(client->curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code < 200 || http_code >= 300) {
        snprintf(client->error_msg, sizeof(client->error_msg), "HTTP error %ld", http_code);
        C64_LOG_ERROR(REST_LOG_PREFIX "%s", client->error_msg);
        return false;
    }

    return true;
}

bool c64_rest_reset(c64_rest_client_t *client)
{
    if (!client) {
        return false;
    }

    C64_LOG_INFO(REST_LOG_PREFIX "Reset machine");
    return http_request(client, "PUT", "/v1/machine:reset", NULL, NULL, 0, NULL);
}

bool c64_rest_reboot(c64_rest_client_t *client)
{
    if (!client) {
        return false;
    }

    C64_LOG_INFO(REST_LOG_PREFIX "Reboot machine");
    return http_request(client, "PUT", "/v1/machine:reboot", NULL, NULL, 0, NULL);
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
        C64_LOG_DEBUG(REST_LOG_PREFIX "Wrote memory $%04X: %zu bytes", address, length);
    }

    return result;
}

bool c64_rest_play_sid(c64_rest_client_t *client, const uint8_t *sid_data, size_t sid_size, int song_number)
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

    // Create MIME structure (modern API)
    curl_mime *mime = curl_mime_init(client->curl);
    curl_mimepart *part = curl_mime_addpart(mime);
    curl_mime_name(part, "file");
    curl_mime_filename(part, "music.sid");
    curl_mime_data(part, (const char *)sid_data, sid_size);
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

    C64_LOG_INFO(REST_LOG_PREFIX "Playing SID song=%d size=%zu", song_number, sid_size);
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

    C64_LOG_INFO(REST_LOG_PREFIX "Running PRG size=%zu", prg_size);
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

    C64_LOG_INFO(REST_LOG_PREFIX "Mounted disk drive=%c type=%s mode=%s size=%zu", drive, type, mode, disk_size);
    return true;
}

const char *c64_rest_get_error(c64_rest_client_t *client)
{
    if (!client) {
        return "Invalid client";
    }
    return client->error_msg;
}
