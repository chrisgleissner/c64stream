/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-rest-client.h"
#include "c64-logging.h"

#include <curl/curl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REST_LOG_PREFIX "📡 REST: "
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
    free(client);

    C64_LOG_DEBUG(REST_LOG_PREFIX "REST client destroyed");
}

// Perform HTTP request
static bool http_request(c64_rest_client_t *client, const char *method, const char *endpoint, const char *query_params,
                         const uint8_t *body_data, size_t body_size, response_buffer_t *response)
{
    if (!client || !client->curl || !method || !endpoint) {
        C64_LOG_ERROR(REST_LOG_PREFIX "http_request called with invalid parameters");
        return false;
    }

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
        snprintf(client->error_msg, sizeof(client->error_msg), "HTTP request failed: %s", curl_easy_strerror(res));
        C64_LOG_ERROR(REST_LOG_PREFIX "%s", client->error_msg);
        return false;
    }

    // Check HTTP status code
    long http_code = 0;
    curl_easy_getinfo(client->curl, CURLINFO_RESPONSE_CODE, &http_code);
    C64_LOG_DEBUG(REST_LOG_PREFIX "HTTP response code: %ld", http_code);

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

    C64_LOG_DEBUG(REST_LOG_PREFIX "Reset machine");
    return http_request(client, "PUT", "/v1/machine:reset", NULL, NULL, 0, NULL);
}

bool c64_rest_reboot(c64_rest_client_t *client)
{
    if (!client) {
        return false;
    }

    C64_LOG_DEBUG(REST_LOG_PREFIX "Reboot machine");
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
    curl_mime_name(part, "sid");
    curl_mime_filename(part, "music.sid");
    curl_mime_data(part, (const char *)sid_data, sid_size);
    curl_mime_type(part, "application/octet-stream");

    if (songlengths_data && songlengths_size > 0) {
        curl_mimepart *songlengths_part = curl_mime_addpart(mime);
        curl_mime_name(songlengths_part, "songlengths");
        curl_mime_filename(songlengths_part, "songlengths.md5");
        curl_mime_data(songlengths_part, (const char *)songlengths_data, songlengths_size);
        curl_mime_type(songlengths_part, "text/plain");
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

    C64_LOG_DEBUG(REST_LOG_PREFIX "Playing SID from C64U: %s song=%d", c64u_path, song_number);

    // URL encode the path
    char *escaped_path = curl_easy_escape(client->curl, c64u_path, 0);
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

const char *c64_rest_get_error(c64_rest_client_t *client)
{
    if (!client) {
        return "Invalid client";
    }
    return client->error_msg;
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
