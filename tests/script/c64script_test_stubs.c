/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-keyboard.h"
#include "c64-rest-client.h"
#include "c64-file.h"
#include "c64-source.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct c64_rest_client {
    uint8_t memory[65536];
    char error[256];
    char last_action[64];
    char last_drive[32];
    char last_category[128];
    char last_item[128];
    char last_value[128];
    char last_path[256];
    char last_type[32];
    char last_mode[32];
    char log[1024];
    size_t log_len;
    bool fail_next;
    char fail_error[256];
};

struct c64_keyboard {
    char log[8192];
    size_t log_len;
};

struct c64_source_stub_state {
    bool wait_should_fail;
    char wait_error[256];
    uint32_t last_wait_frame_count;
    int wait_call_count;
    bool screenshot_should_fail;
    char screenshot_error[256];
    bool last_screenshot_preview;
    char last_screenshot_path[1024];
    int screenshot_call_count;
};

static struct c64_source_stub_state g_source_stub_state;

c64_rest_client_t *c64script_test_rest_create(void)
{
    c64_rest_client_t *client = calloc(1, sizeof(struct c64_rest_client));
    if (!client) {
        return NULL;
    }
    snprintf(((struct c64_rest_client *)client)->error, sizeof(((struct c64_rest_client *)client)->error), "OK");
    snprintf(((struct c64_rest_client *)client)->last_action, sizeof(((struct c64_rest_client *)client)->last_action),
             "none");
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

void c64script_test_rest_fail_next(c64_rest_client_t *client, const char *error)
{
    if (!client) {
        return;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    stub->fail_next = true;
    if (error && error[0]) {
        snprintf(stub->fail_error, sizeof(stub->fail_error), "%s", error);
    } else {
        snprintf(stub->fail_error, sizeof(stub->fail_error), "Simulated failure");
    }
}

const char *c64script_test_rest_log(const c64_rest_client_t *client)
{
    if (!client) {
        return "";
    }
    return ((const struct c64_rest_client *)client)->log;
}

const char *c64script_test_rest_last_action(const c64_rest_client_t *client)
{
    if (!client) {
        return "";
    }
    return ((const struct c64_rest_client *)client)->last_action;
}

const char *c64script_test_rest_last_category(const c64_rest_client_t *client)
{
    if (!client) {
        return "";
    }
    return ((const struct c64_rest_client *)client)->last_category;
}

const char *c64script_test_rest_last_item(const c64_rest_client_t *client)
{
    if (!client) {
        return "";
    }
    return ((const struct c64_rest_client *)client)->last_item;
}

const char *c64script_test_rest_last_value(const c64_rest_client_t *client)
{
    if (!client) {
        return "";
    }
    return ((const struct c64_rest_client *)client)->last_value;
}

const char *c64script_test_rest_last_drive(const c64_rest_client_t *client)
{
    if (!client) {
        return "";
    }
    return ((const struct c64_rest_client *)client)->last_drive;
}

const char *c64script_test_rest_last_path(const c64_rest_client_t *client)
{
    if (!client) {
        return "";
    }
    return ((const struct c64_rest_client *)client)->last_path;
}

const char *c64script_test_rest_last_type(const c64_rest_client_t *client)
{
    if (!client) {
        return "";
    }
    return ((const struct c64_rest_client *)client)->last_type;
}

const char *c64script_test_rest_last_mode(const c64_rest_client_t *client)
{
    if (!client) {
        return "";
    }
    return ((const struct c64_rest_client *)client)->last_mode;
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

void c64script_test_source_stub_reset(void)
{
    memset(&g_source_stub_state, 0, sizeof(g_source_stub_state));
}

void c64script_test_source_wait_fail_next(const char *error)
{
    g_source_stub_state.wait_should_fail = true;
    snprintf(g_source_stub_state.wait_error, sizeof(g_source_stub_state.wait_error), "%s",
             (error && error[0]) ? error : "wait failed");
}

void c64script_test_source_screenshot_fail_next(const char *error)
{
    g_source_stub_state.screenshot_should_fail = true;
    snprintf(g_source_stub_state.screenshot_error, sizeof(g_source_stub_state.screenshot_error), "%s",
             (error && error[0]) ? error : "screenshot failed");
}

uint32_t c64script_test_source_last_wait_frame_count(void)
{
    return g_source_stub_state.last_wait_frame_count;
}

int c64script_test_source_wait_call_count(void)
{
    return g_source_stub_state.wait_call_count;
}

bool c64script_test_source_last_screenshot_preview(void)
{
    return g_source_stub_state.last_screenshot_preview;
}

const char *c64script_test_source_last_screenshot_path(void)
{
    return g_source_stub_state.last_screenshot_path;
}

int c64script_test_source_screenshot_call_count(void)
{
    return g_source_stub_state.screenshot_call_count;
}

// ----------------------------------------------------------------------------
// REST client stubs (sufficient for VM unit tests)
// ----------------------------------------------------------------------------

static bool rest_should_fail(struct c64_rest_client *stub)
{
    if (!stub) {
        return true;
    }
    if (!stub->fail_next) {
        return false;
    }
    stub->fail_next = false;
    snprintf(stub->error, sizeof(stub->error), "%s", stub->fail_error[0] ? stub->fail_error : "Simulated failure");
    return true;
}

static void rest_record_action(struct c64_rest_client *stub, const char *action)
{
    if (!stub || !action) {
        return;
    }
    snprintf(stub->last_action, sizeof(stub->last_action), "%s", action);
    size_t len = strlen(action);
    if (stub->log_len + len + 2 < sizeof(stub->log)) {
        memcpy(stub->log + stub->log_len, action, len);
        stub->log_len += len;
        stub->log[stub->log_len++] = '\n';
        stub->log[stub->log_len] = '\0';
    }
}

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
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    rest_record_action(stub, "reset");
    if (rest_should_fail(stub)) {
        return false;
    }
    return true;
}

bool c64_rest_reboot(c64_rest_client_t *client)
{
    if (!client) {
        return false;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    rest_record_action(stub, "reboot");
    if (rest_should_fail(stub)) {
        return false;
    }
    return true;
}

bool c64_rest_pause(c64_rest_client_t *client)
{
    if (!client) {
        return false;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    rest_record_action(stub, "pause");
    if (rest_should_fail(stub)) {
        return false;
    }
    return true;
}

bool c64_rest_resume(c64_rest_client_t *client)
{
    if (!client) {
        return false;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    rest_record_action(stub, "resume");
    if (rest_should_fail(stub)) {
        return false;
    }
    return true;
}

bool c64_rest_poweroff(c64_rest_client_t *client)
{
    if (!client) {
        return false;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    rest_record_action(stub, "poweroff");
    if (rest_should_fail(stub)) {
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
    if (!client) {
        return false;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    rest_record_action(stub, "playsid");
    if (rest_should_fail(stub)) {
        return false;
    }
    return true;
}

bool c64_rest_play_sid_path(c64_rest_client_t *client, const char *c64u_path, int song_number)
{
    (void)c64u_path;
    (void)song_number;
    if (!client) {
        return false;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    snprintf(stub->last_path, sizeof(stub->last_path), "%s", c64u_path ? c64u_path : "");
    rest_record_action(stub, "playsid_path");
    if (rest_should_fail(stub)) {
        return false;
    }
    return true;
}

bool c64_rest_play_mod(c64_rest_client_t *client, const uint8_t *mod_data, size_t mod_size)
{
    (void)mod_data;
    (void)mod_size;
    if (!client) {
        return false;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    rest_record_action(stub, "playmod");
    if (rest_should_fail(stub)) {
        return false;
    }
    return true;
}

bool c64_rest_play_mod_path(c64_rest_client_t *client, const char *c64u_path)
{
    (void)c64u_path;
    if (!client) {
        return false;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    snprintf(stub->last_path, sizeof(stub->last_path), "%s", c64u_path ? c64u_path : "");
    rest_record_action(stub, "playmod_path");
    if (rest_should_fail(stub)) {
        return false;
    }
    return true;
}

bool c64_rest_run_prg(c64_rest_client_t *client, const uint8_t *prg_data, size_t prg_size)
{
    (void)prg_data;
    (void)prg_size;
    if (!client) {
        return false;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    rest_record_action(stub, "runprg");
    if (rest_should_fail(stub)) {
        return false;
    }
    return true;
}

bool c64_rest_run_prg_path(c64_rest_client_t *client, const char *c64u_path)
{
    (void)c64u_path;
    if (!client) {
        return false;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    snprintf(stub->last_path, sizeof(stub->last_path), "%s", c64u_path ? c64u_path : "");
    rest_record_action(stub, "runprg_path");
    if (rest_should_fail(stub)) {
        return false;
    }
    return true;
}

bool c64_rest_run_crt(c64_rest_client_t *client, const uint8_t *crt_data, size_t crt_size)
{
    (void)crt_data;
    (void)crt_size;
    if (!client) {
        return false;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    rest_record_action(stub, "runcrt");
    if (rest_should_fail(stub)) {
        return false;
    }
    return true;
}

bool c64_rest_run_crt_path(c64_rest_client_t *client, const char *c64u_path)
{
    (void)c64u_path;
    if (!client) {
        return false;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    snprintf(stub->last_path, sizeof(stub->last_path), "%s", c64u_path ? c64u_path : "");
    rest_record_action(stub, "runcrt_path");
    if (rest_should_fail(stub)) {
        return false;
    }
    return true;
}

bool c64_source_script_wait_rendered_frames(struct c64_source *context, uint32_t frame_count, char *error_msg,
                                            size_t error_size)
{
    (void)context;
    g_source_stub_state.last_wait_frame_count = frame_count;
    g_source_stub_state.wait_call_count++;
    if (g_source_stub_state.wait_should_fail) {
        g_source_stub_state.wait_should_fail = false;
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "%s",
                     g_source_stub_state.wait_error[0] ? g_source_stub_state.wait_error : "wait failed");
        }
        return false;
    }
    if (error_msg && error_size > 0) {
        error_msg[0] = '\0';
    }
    return true;
}

bool c64_source_script_take_frontend_screenshot(struct c64_source *context, bool preview, const char *output_path,
                                                char *error_msg, size_t error_size)
{
    (void)context;
    g_source_stub_state.last_screenshot_preview = preview;
    g_source_stub_state.screenshot_call_count++;
    snprintf(g_source_stub_state.last_screenshot_path, sizeof(g_source_stub_state.last_screenshot_path), "%s",
             output_path ? output_path : "");
    if (g_source_stub_state.screenshot_should_fail) {
        g_source_stub_state.screenshot_should_fail = false;
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "%s",
                     g_source_stub_state.screenshot_error[0] ? g_source_stub_state.screenshot_error
                                                             : "screenshot failed");
        }
        return false;
    }
    if (error_msg && error_size > 0) {
        error_msg[0] = '\0';
    }
    return true;
}

bool c64_create_directory_recursive(const char *path)
{
    (void)path;
    return true;
}

bool c64_rest_mount_disk(c64_rest_client_t *client, char drive, const char *type, const char *mode,
                         const uint8_t *disk_data, size_t disk_size)
{
    (void)drive;
    (void)type;
    (void)mode;
    (void)disk_data;
    (void)disk_size;
    if (!client) {
        return false;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    snprintf(stub->last_drive, sizeof(stub->last_drive), "%c", drive);
    snprintf(stub->last_type, sizeof(stub->last_type), "%s", type ? type : "");
    snprintf(stub->last_mode, sizeof(stub->last_mode), "%s", mode ? mode : "");
    rest_record_action(stub, "mountdisk_upload");
    if (rest_should_fail(stub)) {
        return false;
    }
    return true;
}

bool c64_rest_mount_disk_path(c64_rest_client_t *client, char drive, const char *c64u_path)
{
    (void)drive;
    (void)c64u_path;
    if (!client) {
        return false;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    snprintf(stub->last_drive, sizeof(stub->last_drive), "%c", drive);
    snprintf(stub->last_path, sizeof(stub->last_path), "%s", c64u_path ? c64u_path : "");
    rest_record_action(stub, "mountdisk_path");
    if (rest_should_fail(stub)) {
        return false;
    }
    return true;
}

bool c64_rest_drive_get_property(c64_rest_client_t *client, const char *drive, const char *property, char *out_value,
                                 size_t out_size)
{
    if (!client || !drive || !property || !out_value || out_size == 0) {
        return false;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    snprintf(stub->last_drive, sizeof(stub->last_drive), "%s", drive);
    snprintf(stub->last_item, sizeof(stub->last_item), "%s", property);
    rest_record_action(stub, "drive_get");
    if (rest_should_fail(stub)) {
        return false;
    }

    if (strcmp(property, "enabled") == 0) {
        snprintf(out_value, out_size, "true");
    } else if (strcmp(property, "bus_id") == 0) {
        snprintf(out_value, out_size, "8");
    } else if (strcmp(property, "type") == 0) {
        snprintf(out_value, out_size, "1541");
    } else if (strcmp(property, "rom") == 0) {
        snprintf(out_value, out_size, "1541.rom");
    } else if (strcmp(property, "image_file") == 0) {
        snprintf(out_value, out_size, "game.d64");
    } else if (strcmp(property, "image_path") == 0) {
        snprintf(out_value, out_size, "c64u:/Games/game.d64");
    } else {
        out_value[0] = '\0';
    }

    return true;
}

bool c64_rest_drive_mount_image(c64_rest_client_t *client, const char *drive, const char *c64u_path, const char *type,
                                const char *mode)
{
    if (!client || !drive) {
        return false;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    snprintf(stub->last_drive, sizeof(stub->last_drive), "%s", drive);
    snprintf(stub->last_path, sizeof(stub->last_path), "%s", c64u_path ? c64u_path : "");
    snprintf(stub->last_type, sizeof(stub->last_type), "%s", type ? type : "");
    snprintf(stub->last_mode, sizeof(stub->last_mode), "%s", mode ? mode : "");
    rest_record_action(stub, "drive_mount_image");
    if (rest_should_fail(stub)) {
        return false;
    }
    return true;
}

bool c64_rest_drive_mount_upload(c64_rest_client_t *client, const char *drive, const char *type, const char *mode,
                                 const uint8_t *data, size_t size)
{
    (void)data;
    (void)size;
    if (!client || !drive) {
        return false;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    snprintf(stub->last_drive, sizeof(stub->last_drive), "%s", drive);
    snprintf(stub->last_type, sizeof(stub->last_type), "%s", type ? type : "");
    snprintf(stub->last_mode, sizeof(stub->last_mode), "%s", mode ? mode : "");
    rest_record_action(stub, "drive_mount_upload");
    if (rest_should_fail(stub)) {
        return false;
    }
    return true;
}

bool c64_rest_drive_unmount(c64_rest_client_t *client, const char *drive)
{
    if (!client || !drive) {
        return false;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    snprintf(stub->last_drive, sizeof(stub->last_drive), "%s", drive);
    rest_record_action(stub, "drive_unmount");
    if (rest_should_fail(stub)) {
        return false;
    }
    return true;
}

bool c64_rest_drive_reset(c64_rest_client_t *client, const char *drive)
{
    if (!client || !drive) {
        return false;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    snprintf(stub->last_drive, sizeof(stub->last_drive), "%s", drive);
    rest_record_action(stub, "drive_reset");
    if (rest_should_fail(stub)) {
        return false;
    }
    return true;
}

bool c64_rest_drive_on(c64_rest_client_t *client, const char *drive)
{
    if (!client || !drive) {
        return false;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    snprintf(stub->last_drive, sizeof(stub->last_drive), "%s", drive);
    rest_record_action(stub, "drive_on");
    if (rest_should_fail(stub)) {
        return false;
    }
    return true;
}

bool c64_rest_drive_off(c64_rest_client_t *client, const char *drive)
{
    if (!client || !drive) {
        return false;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    snprintf(stub->last_drive, sizeof(stub->last_drive), "%s", drive);
    rest_record_action(stub, "drive_off");
    if (rest_should_fail(stub)) {
        return false;
    }
    return true;
}

bool c64_rest_drive_load_rom_image(c64_rest_client_t *client, const char *drive, const char *c64u_path)
{
    if (!client || !drive) {
        return false;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    snprintf(stub->last_drive, sizeof(stub->last_drive), "%s", drive);
    snprintf(stub->last_path, sizeof(stub->last_path), "%s", c64u_path ? c64u_path : "");
    rest_record_action(stub, "drive_rom_image");
    if (rest_should_fail(stub)) {
        return false;
    }
    return true;
}

bool c64_rest_drive_load_rom_upload(c64_rest_client_t *client, const char *drive, const uint8_t *data, size_t size)
{
    (void)data;
    (void)size;
    if (!client || !drive) {
        return false;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    snprintf(stub->last_drive, sizeof(stub->last_drive), "%s", drive);
    rest_record_action(stub, "drive_rom_upload");
    if (rest_should_fail(stub)) {
        return false;
    }
    return true;
}

bool c64_rest_drive_set_mode(c64_rest_client_t *client, const char *drive, const char *mode)
{
    if (!client || !drive || !mode) {
        return false;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    snprintf(stub->last_drive, sizeof(stub->last_drive), "%s", drive);
    snprintf(stub->last_mode, sizeof(stub->last_mode), "%s", mode);
    rest_record_action(stub, "drive_mode");
    if (rest_should_fail(stub)) {
        return false;
    }
    return true;
}

static bool copy_string_list(const char **src, size_t count, char ***items, size_t *out_count)
{
    if (!items || !out_count) {
        return false;
    }
    *items = NULL;
    *out_count = 0;
    if (count == 0) {
        return true;
    }
    char **list = calloc(count, sizeof(char *));
    if (!list) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        list[i] = strdup(src[i] ? src[i] : "");
        if (!list[i]) {
            for (size_t j = 0; j < i; j++) {
                free(list[j]);
            }
            free(list);
            return false;
        }
    }
    *items = list;
    *out_count = count;
    return true;
}

bool c64_rest_config_get_value(c64_rest_client_t *client, const char *category, const char *item, char *out_value,
                               size_t out_size)
{
    if (!client || !category || !item || !out_value || out_size == 0) {
        return false;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    snprintf(stub->last_category, sizeof(stub->last_category), "%s", category);
    snprintf(stub->last_item, sizeof(stub->last_item), "%s", item);
    rest_record_action(stub, "cfg_get");
    if (rest_should_fail(stub)) {
        return false;
    }

    if (strcmp(category, "Audio Mixer") == 0 && strcmp(item, "Vol Sid Socket 1") == 0) {
        snprintf(out_value, out_size, "80");
    } else if (strcmp(category, "U64 Specific Settings") == 0 && strcmp(item, "CPU Speed") == 0) {
        snprintf(out_value, out_size, " 1");
    } else {
        out_value[0] = '\0';
    }
    return true;
}

bool c64_rest_config_set_value(c64_rest_client_t *client, const char *category, const char *item, const char *value)
{
    if (!client || !category || !item || !value) {
        return false;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    snprintf(stub->last_category, sizeof(stub->last_category), "%s", category);
    snprintf(stub->last_item, sizeof(stub->last_item), "%s", item);
    snprintf(stub->last_value, sizeof(stub->last_value), "%s", value);
    rest_record_action(stub, "cfg_set");
    if (rest_should_fail(stub)) {
        return false;
    }
    return true;
}

bool c64_rest_config_list(c64_rest_client_t *client, const char *category, char ***items, size_t *count)
{
    if (!client || !items || !count) {
        return false;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    snprintf(stub->last_category, sizeof(stub->last_category), "%s", category ? category : "");
    rest_record_action(stub, "cfg_list");
    if (rest_should_fail(stub)) {
        return false;
    }

    if (!category || category[0] == '\0') {
        static const char *cats[] = {"Audio Mixer", "U64 Specific Settings", "SID Sockets Configuration"};
        return copy_string_list(cats, sizeof(cats) / sizeof(cats[0]), items, count);
    }

    if (strcmp(category, "Audio Mixer") == 0) {
        static const char *audio_items[] = {"Vol Sid Socket 1", "Vol Sid Socket 2"};
        return copy_string_list(audio_items, sizeof(audio_items) / sizeof(audio_items[0]), items, count);
    }
    if (strcmp(category, "U64 Specific Settings") == 0) {
        static const char *u64_items[] = {"CPU Speed", "System Mode"};
        return copy_string_list(u64_items, sizeof(u64_items) / sizeof(u64_items[0]), items, count);
    }
    if (strcmp(category, "SID Sockets Configuration") == 0) {
        static const char *sid_items[] = {"SID Socket 1", "SID Socket 2"};
        return copy_string_list(sid_items, sizeof(sid_items) / sizeof(sid_items[0]), items, count);
    }

    *items = NULL;
    *count = 0;
    return true;
}

bool c64_rest_config_list_options(c64_rest_client_t *client, const char *category, const char *item, char ***options,
                                  size_t *count)
{
    if (!client || !category || !item || !options || !count) {
        return false;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    snprintf(stub->last_category, sizeof(stub->last_category), "%s", category);
    snprintf(stub->last_item, sizeof(stub->last_item), "%s", item);
    rest_record_action(stub, "cfg_options");
    if (rest_should_fail(stub)) {
        return false;
    }

    if (strcmp(category, "Audio Mixer") == 0 && strcmp(item, "Vol Sid Socket 1") == 0) {
        static const char *opts[] = {"0", "50", "100"};
        return copy_string_list(opts, sizeof(opts) / sizeof(opts[0]), options, count);
    }
    if (strcmp(category, "U64 Specific Settings") == 0 && strcmp(item, "System Mode") == 0) {
        static const char *opts[] = {"PAL", "NTSC"};
        return copy_string_list(opts, sizeof(opts) / sizeof(opts[0]), options, count);
    }

    *options = NULL;
    *count = 0;
    return true;
}

bool c64_rest_config_save(c64_rest_client_t *client)
{
    if (!client) {
        return false;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    rest_record_action(stub, "cfg_save");
    if (rest_should_fail(stub)) {
        return false;
    }
    return true;
}

bool c64_rest_config_load(c64_rest_client_t *client)
{
    if (!client) {
        return false;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    rest_record_action(stub, "cfg_load");
    if (rest_should_fail(stub)) {
        return false;
    }
    return true;
}

bool c64_rest_config_reset(c64_rest_client_t *client)
{
    if (!client) {
        return false;
    }
    struct c64_rest_client *stub = (struct c64_rest_client *)client;
    rest_record_action(stub, "cfg_reset");
    if (rest_should_fail(stub)) {
        return false;
    }
    return true;
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
        const char *text = output->data.text[0] != '\0' ? output->data.text : "";
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
