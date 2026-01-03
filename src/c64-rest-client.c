/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-rest-client.h"
#include "c64-logging.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REST_LOG_PREFIX "[c64-rest] "

struct c64_rest_client {
    char *base_url;
    char *password;
    char error_msg[512];
};

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

    C64_LOG_INFO(REST_LOG_PREFIX "Created REST client for %s", base_url);
    return client;
}

void c64_rest_client_destroy(c64_rest_client_t *client)
{
    if (!client) {
        return;
    }

    free(client->base_url);
    free(client->password);
    free(client);
}

bool c64_rest_reset(c64_rest_client_t *client)
{
    if (!client) {
        return false;
    }

    // TODO: Implement HTTP PUT /v1/machine:reset
    C64_LOG_INFO(REST_LOG_PREFIX "Reset (stub)");
    snprintf(client->error_msg, sizeof(client->error_msg), "Not implemented");
    return false;
}

bool c64_rest_reboot(c64_rest_client_t *client)
{
    if (!client) {
        return false;
    }

    // TODO: Implement HTTP PUT /v1/machine:reboot
    C64_LOG_INFO(REST_LOG_PREFIX "Reboot (stub)");
    snprintf(client->error_msg, sizeof(client->error_msg), "Not implemented");
    return false;
}

int c64_rest_read_memory(c64_rest_client_t *client, uint16_t address, size_t length, uint8_t *buffer,
                         size_t buffer_size)
{
    if (!client || !buffer || length > buffer_size) {
        return -1;
    }

    // TODO: Implement HTTP GET /v1/machine:readmem
    C64_LOG_INFO(REST_LOG_PREFIX "Read memory $%04X len=%zu (stub)", address, length);
    snprintf(client->error_msg, sizeof(client->error_msg), "Not implemented");
    return -1;
}

bool c64_rest_write_memory(c64_rest_client_t *client, uint16_t address, const uint8_t *data, size_t length)
{
    if (!client || !data || length == 0) {
        return false;
    }

    // TODO: Implement HTTP PUT /v1/machine:writemem
    C64_LOG_INFO(REST_LOG_PREFIX "Write memory $%04X len=%zu (stub)", address, length);
    snprintf(client->error_msg, sizeof(client->error_msg), "Not implemented");
    return false;
}

bool c64_rest_play_sid(c64_rest_client_t *client, const uint8_t *sid_data, size_t sid_size, int song_number)
{
    if (!client || !sid_data || sid_size == 0) {
        return false;
    }

    // TODO: Implement HTTP POST /v1/runners:sidplay
    C64_LOG_INFO(REST_LOG_PREFIX "Play SID song=%d size=%zu (stub)", song_number, sid_size);
    snprintf(client->error_msg, sizeof(client->error_msg), "Not implemented");
    return false;
}

bool c64_rest_run_prg(c64_rest_client_t *client, const uint8_t *prg_data, size_t prg_size)
{
    if (!client || !prg_data || prg_size == 0) {
        return false;
    }

    // TODO: Implement HTTP POST /v1/runners:run_prg
    C64_LOG_INFO(REST_LOG_PREFIX "Run PRG size=%zu (stub)", prg_size);
    snprintf(client->error_msg, sizeof(client->error_msg), "Not implemented");
    return false;
}

bool c64_rest_mount_disk(c64_rest_client_t *client, char drive, const char *type, const char *mode,
                         const uint8_t *disk_data, size_t disk_size)
{
    if (!client || !type || !mode || !disk_data || disk_size == 0) {
        return false;
    }

    // TODO: Implement HTTP POST /v1/drives/{drive}:mount
    C64_LOG_INFO(REST_LOG_PREFIX "Mount disk drive=%c type=%s mode=%s size=%zu (stub)", drive, type, mode, disk_size);
    snprintf(client->error_msg, sizeof(client->error_msg), "Not implemented");
    return false;
}

const char *c64_rest_get_error(c64_rest_client_t *client)
{
    if (!client) {
        return "Invalid client";
    }
    return client->error_msg;
}
