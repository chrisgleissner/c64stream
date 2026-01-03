/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-automation.h"
#include "c64-keyboard.h"
#include "c64-logging.h"
#include "c64-rest-client.h"

#include <stdlib.h>
#include <string.h>

#define AUTOMATION_LOG_PREFIX "[c64-automation] "

struct c64_automation {
    c64_rest_client_t *rest_client;
    c64_keyboard_t *keyboard;
    c64_automation_config_t config;
    bool running;
    char status[128];
    // TODO: Add worker thread
    // TODO: Add file list
};

c64_automation_t *c64_automation_create(void *rest_client, void *keyboard)
{
    if (!rest_client) {
        return NULL;
    }

    c64_automation_t *automation = calloc(1, sizeof(c64_automation_t));
    if (!automation) {
        return NULL;
    }

    automation->rest_client = (c64_rest_client_t *)rest_client;
    automation->keyboard = (c64_keyboard_t *)keyboard;
    automation->running = false;
    strncpy(automation->status, "idle", sizeof(automation->status) - 1);

    // Set defaults
    automation->config.mode = C64_AUTO_MODE_OFF;
    automation->config.duration_seconds = 120;
    automation->config.reset_between_items = true;
    strncpy(automation->config.d64_autostart_template, "LOAD\"*\",8,1\rRUN\r",
            sizeof(automation->config.d64_autostart_template) - 1);

    C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Created automation engine");
    return automation;
}

void c64_automation_destroy(c64_automation_t *automation)
{
    if (!automation) {
        return;
    }

    // TODO: Stop worker thread
    free(automation);
}

void c64_automation_configure(c64_automation_t *automation, const c64_automation_config_t *config)
{
    if (!automation || !config) {
        return;
    }

    memcpy(&automation->config, config, sizeof(c64_automation_config_t));
    C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Configured: mode=%d folder=%s shuffle=%d duration=%d", config->mode,
                 config->folder_path, config->shuffle, config->duration_seconds);
}

bool c64_automation_start(c64_automation_t *automation)
{
    if (!automation) {
        return false;
    }

    if (automation->running) {
        C64_LOG_WARNING(AUTOMATION_LOG_PREFIX "Already running");
        return false;
    }

    // TODO: Start worker thread
    // TODO: Enumerate files
    automation->running = true;
    strncpy(automation->status, "starting", sizeof(automation->status) - 1);

    C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Started automation mode=%d", automation->config.mode);
    return true;
}

void c64_automation_stop(c64_automation_t *automation)
{
    if (!automation) {
        return;
    }

    if (!automation->running) {
        return;
    }

    // TODO: Cancel worker thread
    automation->running = false;
    strncpy(automation->status, "stopped", sizeof(automation->status) - 1);

    C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Stopped automation");
}

bool c64_automation_is_running(c64_automation_t *automation)
{
    if (!automation) {
        return false;
    }
    return automation->running;
}

const char *c64_automation_get_status(c64_automation_t *automation)
{
    if (!automation) {
        return "invalid";
    }
    return automation->status;
}

bool c64_automation_play_sid(c64_automation_t *automation, const char *path, int song_number)
{
    if (!automation || !path) {
        return false;
    }

    // TODO: Load file and call c64_rest_play_sid
    C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Play SID %s song=%d (stub)", path, song_number);
    return false;
}

bool c64_automation_run_prg(c64_automation_t *automation, const char *path)
{
    if (!automation || !path) {
        return false;
    }

    // TODO: Load file and call c64_rest_run_prg
    C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Run PRG %s (stub)", path);
    return false;
}

bool c64_automation_start_d64(c64_automation_t *automation, const char *path)
{
    if (!automation || !path) {
        return false;
    }

    // TODO: Load file, call c64_rest_mount_disk, inject autostart command
    C64_LOG_INFO(AUTOMATION_LOG_PREFIX "Start D64 %s (stub)", path);
    return false;
}
