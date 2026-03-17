/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#pragma once

#include "c64-automation.h"
#include "c64-automation-hvsc.h"
#include "c64-keyboard.h"
#include "c64-rest-client.h"
#include <pthread.h>

#define C64_AUTOMATION_PLAYLIST_MAX_FILES 100000
#define C64_AUTOMATION_PLAYLIST_INITIAL_CAPACITY 1024

typedef struct {
    char path[C64_AUTOMATION_PATH_MAX];
    c64_file_type_t type;
} file_entry_t;

struct c64_automation {
    c64_rest_client_t *rest_client;
    c64_keyboard_t *keyboard;
    obs_source_t *source;
    c64_automation_config_t config;
    c64_automation_config_t playlist_config;
    bool playlist_config_valid;
    bool running;
    bool should_stop;
    bool skip_requested;
    char status[128];
    char current_file_path[C64_AUTOMATION_PATH_MAX];
    bool playlist_ready;

    file_entry_t *files;
    int num_files;
    int files_capacity;
    int current_index;

    bool use_songlengths;
    c64_hvsc_songlength_db_t songlength_db;

    pthread_t worker_thread;
    pthread_t preload_thread;
    pthread_mutex_t status_mutex;
    bool preload_thread_valid;
    bool preload_running;
};

void c64_automation_clear_playlist_internal(c64_automation_t *automation);
bool c64_automation_build_playlist(c64_automation_t *automation);
