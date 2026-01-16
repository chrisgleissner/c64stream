/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "../../src/c64-keyboard.h"
#include "../../src/c64-rest-client.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct c64_rest_client {
    uint8_t memory[65536];
    char error[256];
};

struct c64_keyboard {
    char log[8192];
    size_t log_len;
};

c64_rest_client_t *c64script_test_rest_create(void)
{
    c64_rest_client_t *client = calloc(1, sizeof(struct c64_rest_client));
    if (!client) {
        return NULL;
    }
    snprintf(((struct c64_rest_client *)client)->error, sizeof(((struct c64_rest_client *)client)->error), "OK");
    return client;
}

void c64script_test_rest_destroy(c64_rest_client_t *client)
{
    free(client);
}

void c64script_test_rest_set_byte(c64_rest_client_t *client, uint16_t address, uint8_t value)
{
    if (!client) {
        return;
    }
    ((struct c64_rest_client *)client)->memory[address] = value;
}

c64_keyboard_t *c64script_test_keyboard_create(void)
{
    return (c64_keyboard_t *)calloc(1, sizeof(struct c64_keyboard));
}

void c64script_test_keyboard_destroy(c64_keyboard_t *keyboard)
{
    free(keyboard);
}

const char *c64script_test_keyboard_log(const c64_keyboard_t *keyboard)
{
    if (!keyboard) {
        return "";
    }
    return ((const struct c64_keyboard *)keyboard)->log;
}

// ----------------------------------------------------------------------------
// REST client stubs (sufficient for VM unit tests)
// ----------------------------------------------------------------------------

const char *c64_rest_get_error(c64_rest_client_t *client)
{
    if (!client) {
        return "No client";
    }
    return ((struct c64_rest_client *)client)->error;
}

int c64_rest_read_memory(c64_rest_client_t *client, uint16_t address, size_t length, uint8_t *buffer,
                         size_t buffer_size)
{
    if (!client || !buffer || buffer_size < length) {
        return -1;
    }
    if ((size_t)address + length > sizeof(((struct c64_rest_client *)client)->memory)) {
        snprintf(((struct c64_rest_client *)client)->error, sizeof(((struct c64_rest_client *)client)->error),
                 "Out of range");
        return -1;
    }
    memcpy(buffer, &((struct c64_rest_client *)client)->memory[address], length);
    return (int)length;
}

bool c64_rest_write_memory(c64_rest_client_t *client, uint16_t address, const uint8_t *data, size_t length)
{
    if (!client || !data) {
        return false;
    }
    if ((size_t)address + length > sizeof(((struct c64_rest_client *)client)->memory)) {
        snprintf(((struct c64_rest_client *)client)->error, sizeof(((struct c64_rest_client *)client)->error),
                 "Out of range");
        return false;
    }
    memcpy(&((struct c64_rest_client *)client)->memory[address], data, length);
    return true;
}

bool c64_rest_reset(c64_rest_client_t *client)
{
    if (!client) {
        return false;
    }
    return true;
}

bool c64_rest_reboot(c64_rest_client_t *client)
{
    if (!client) {
        return false;
    }
    return true;
}

bool c64_rest_play_sid(c64_rest_client_t *client, const uint8_t *sid_data, size_t sid_size, int song_number,
                       const uint8_t *songlengths_data, size_t songlengths_size)
{
    (void)sid_data;
    (void)sid_size;
    (void)song_number;
    (void)songlengths_data;
    (void)songlengths_size;
    return client != NULL;
}

bool c64_rest_play_sid_path(c64_rest_client_t *client, const char *c64u_path, int song_number)
{
    (void)c64u_path;
    (void)song_number;
    return client != NULL;
}

bool c64_rest_play_mod(c64_rest_client_t *client, const uint8_t *mod_data, size_t mod_size)
{
    (void)mod_data;
    (void)mod_size;
    return client != NULL;
}

bool c64_rest_play_mod_path(c64_rest_client_t *client, const char *c64u_path)
{
    (void)c64u_path;
    return client != NULL;
}

bool c64_rest_run_prg(c64_rest_client_t *client, const uint8_t *prg_data, size_t prg_size)
{
    (void)prg_data;
    (void)prg_size;
    return client != NULL;
}

bool c64_rest_run_prg_path(c64_rest_client_t *client, const char *c64u_path)
{
    (void)c64u_path;
    return client != NULL;
}

bool c64_rest_run_crt(c64_rest_client_t *client, const uint8_t *crt_data, size_t crt_size)
{
    (void)crt_data;
    (void)crt_size;
    return client != NULL;
}

bool c64_rest_run_crt_path(c64_rest_client_t *client, const char *c64u_path)
{
    (void)c64u_path;
    return client != NULL;
}

bool c64_rest_mount_disk(c64_rest_client_t *client, char drive, const char *type, const char *mode,
                         const uint8_t *disk_data, size_t disk_size)
{
    (void)drive;
    (void)type;
    (void)mode;
    (void)disk_data;
    (void)disk_size;
    return client != NULL;
}

bool c64_rest_mount_disk_path(c64_rest_client_t *client, char drive, const char *c64u_path)
{
    (void)drive;
    (void)c64u_path;
    return client != NULL;
}

bool c64_rest_list_files(c64_rest_client_t *client, const char *path, bool recursive, c64_file_entry_t **entries,
                         size_t *entry_count)
{
    (void)client;
    (void)path;
    (void)recursive;
    if (entries) {
        *entries = NULL;
    }
    if (entry_count) {
        *entry_count = 0;
    }
    return true;
}

// ----------------------------------------------------------------------------
// Keyboard stubs (sufficient for VM unit tests)
// ----------------------------------------------------------------------------

void c64_keyboard_queue_output(c64_keyboard_t *keyboard, const c64_output_t *output)
{
    if (!keyboard || !output) {
        return;
    }

    struct c64_keyboard *kb = (struct c64_keyboard *)keyboard;

    char entry[512] = {0};
    switch (output->mode) {
    case C64_OUTPUT_TEXT: {
        const char *text = output->data.text ? output->data.text : "";
        char escaped[384];
        size_t out_len = 0;
        for (size_t i = 0; text[i] != '\0' && out_len + 1 < sizeof(escaped); i++) {
            unsigned char c = (unsigned char)text[i];
            const char *rep = NULL;
            char tmp[5];
            if (c == '\r') {
                rep = "\\r";
            } else if (c == '\n') {
                rep = "\\n";
            } else if (c == '\t') {
                rep = "\\t";
            } else if (c == '\\') {
                rep = "\\\\";
            } else if (c >= 0x20 && c < 0x7f) {
                escaped[out_len++] = (char)c;
                continue;
            } else {
                snprintf(tmp, sizeof(tmp), "\\x%02X", (unsigned)c);
                rep = tmp;
            }

            size_t rep_len = strlen(rep);
            if (out_len + rep_len >= sizeof(escaped)) {
                break;
            }
            memcpy(&escaped[out_len], rep, rep_len);
            out_len += rep_len;
        }
        escaped[out_len] = '\0';

        snprintf(entry, sizeof(entry), "TEXT:%s\n", escaped);
        break;
    }
    case C64_OUTPUT_PETSCII:
        snprintf(entry, sizeof(entry), "PETSCII:%u\n", (unsigned)output->data.petscii);
        break;
    case C64_OUTPUT_SYMBOLIC:
        snprintf(entry, sizeof(entry), "SYMBOL:%s\n", output->data.symbol);
        break;
    default:
        snprintf(entry, sizeof(entry), "UNKNOWN\n");
        break;
    }

    size_t entry_len = strlen(entry);
    size_t remaining = sizeof(kb->log) - 1 - kb->log_len;
    if (entry_len > remaining) {
        entry_len = remaining;
    }
    memcpy(kb->log + kb->log_len, entry, entry_len);
    kb->log_len += entry_len;
    kb->log[kb->log_len] = '\0';
}
