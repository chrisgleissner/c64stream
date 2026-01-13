/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-keyboard.h"
#include "c64-logging.h"
#include "c64-rest-client.h"
#include "c64-file.h"

#include <obs-module.h>
#include <util/platform.h>
#include <ctype.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define sleep(x) Sleep((x) * 1000)
#else
#include <dirent.h>
#include <unistd.h>
#endif
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KEYBOARD_LOG_PREFIX "🕹 KEYBOARD: "
#define MAX_KEYMAP_ENTRIES 512
#define QUEUE_SIZE 1024
#define POLL_INTERVAL_MS 50

// C64 memory locations
#define C64_KEYBOARD_BUFFER 0x0277
#define C64_KEYBOARD_LENGTH 0x00C6
#define C64_KEYBOARD_BUFFER_SIZE 10
#define C64_STOP_FLAG 0x0091        // RUN/STOP flag (bit 7)
#define C64_IRQ_VECTOR_LOW 0x0314   // IRQ vector low byte
#define C64_IRQ_VECTOR_HIGH 0x0315  // IRQ vector high byte
#define C64_BASIC_WARM_START 0xA474 // BASIC warm start routine address
#define C64_WARM_START_DELAY_MS 40  // Delay for warm start to take effect

// Keymap entry
typedef struct {
    char key[64];     // Input key name (e.g., "a", "return", "f1")
    uint8_t value;    // PETSCII code or special value
    bool is_symbolic; // True if value is symbolic name
    char symbol[32];  // Symbolic name (e.g., "c64:RETURN")
} keymap_entry_t;

// Symbolic key definitions
typedef struct {
    const char *name;
    uint8_t code;
} symbolic_key_t;

static const symbolic_key_t symbolic_keys[] = {{"c64:RETURN", 0x0D},
                                               {"c64:RUNSTOP", 0x03},
                                               {"c64:INSTDEL", 0x14},
                                               {"c64:TAB", 0x09},
                                               {"c64:F1", 0x85},
                                               {"c64:F3", 0x86},
                                               {"c64:F5", 0x87},
                                               {"c64:F7", 0x88},
                                               {"c64:F2", 0x89},
                                               {"c64:F4", 0x8A},
                                               {"c64:F6", 0x8B},
                                               {"c64:F8", 0x8C},
                                               {"c64:CURSOR_UP", 0x91},
                                               {"c64:CURSOR_DOWN", 0x11},
                                               {"c64:CURSOR_LEFT", 0x9D},
                                               {"c64:CURSOR_RIGHT", 0x1D},
                                               {"c64:HOME", 0x13},
                                               {"c64:CLEAR", 0x93},
                                               {"c64:SHIFT_RETURN", 0x8D},
                                               {"c64:SHIFT_SPACE", 0xA0},
                                               {"c64:CLR_HOME", 0x13},
                                               {"c64:INSERT", 0x94},
                                               {"c64:DELETE", 0x14},
                                               {"c64:RVS_ON", 0x12},
                                               {"c64:RVS_OFF", 0x92},
                                               {"c64:COLOR_BLACK", 0x90},
                                               {"c64:COLOR_WHITE", 0x05},
                                               {"c64:COLOR_RED", 0x1C},
                                               {"c64:COLOR_CYAN", 0x9F},
                                               {"c64:COLOR_PURPLE", 0x9C},
                                               {"c64:COLOR_GREEN", 0x1E},
                                               {"c64:COLOR_BLUE", 0x1F},
                                               {"c64:COLOR_YELLOW", 0x9E},
                                               {"c64:COLOR_ORANGE", 0x81},
                                               {"c64:COLOR_BROWN", 0x95},
                                               {"c64:COLOR_LT_RED", 0x96},
                                               {"c64:COLOR_DK_GREY", 0x97},
                                               {"c64:COLOR_GREY", 0x98},
                                               {"c64:COLOR_LT_GREEN", 0x99},
                                               {"c64:COLOR_LT_BLUE", 0x9A},
                                               {"c64:COLOR_LT_GREY", 0x9B},
                                               {NULL, 0}};

struct c64_keymap {
    char name[128];
    char description[256];
    keymap_entry_t entries[MAX_KEYMAP_ENTRIES];
    int num_entries;
};

struct c64_keyboard {
    c64_rest_client_t *rest_client;
    c64_keymap_t *keymap;
    bool capturing;
    char status[64];

    // FIFO queue for keystroke bytes
    uint8_t queue[QUEUE_SIZE];
    int queue_head;
    int queue_tail;
    int queue_count;
    pthread_mutex_t queue_mutex;

    // Worker thread
    pthread_t worker_thread;
    bool worker_running;
};

static uint8_t lookup_symbolic_key(const char *name)
{
    for (int i = 0; symbolic_keys[i].name; i++) {
        if (strcmp(symbolic_keys[i].name, name) == 0) {
            return symbolic_keys[i].code;
        }
    }
    return 0;
}

static void trim(char *str)
{
    // Trim leading whitespace
    char *start = str;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }

    // Trim trailing whitespace
    char *end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) {
        end--;
    }
    *(end + 1) = '\0';

    // Move trimmed string to start
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
}

c64_keymap_t *c64_keymap_load(const char *path)
{
    if (!path) {
        return NULL;
    }

    FILE *file = fopen(path, "r");
    if (!file) {
        C64_LOG_ERROR(KEYBOARD_LOG_PREFIX "Failed to open keymap: %s", path);
        return NULL;
    }

    c64_keymap_t *keymap = calloc(1, sizeof(c64_keymap_t));
    if (!keymap) {
        fclose(file);
        return NULL;
    }

    strncpy(keymap->name, "Default", sizeof(keymap->name) - 1);

    char line[512];
    char section[64] = "";
    int line_num = 0;

    while (fgets(line, sizeof(line), file)) {
        line_num++;
        trim(line);

        // Skip empty lines and comments (allow literal '#' keys like '#=0x23')
        if (line[0] == '\0') {
            continue;
        }
        if (line[0] == '#' && strchr(line, '=') == NULL) {
            continue;
        }

        // Section header
        if (line[0] == '[' && line[strlen(line) - 1] == ']') {
            strncpy(section, line + 1, sizeof(section) - 1);
            section[strlen(section) - 1] = '\0';
            continue;
        }

        // Key=value pair
        char *equals = strchr(line, '=');
        if (!equals) {
            continue;
        }

        *equals = '\0';
        char *key = line;
        char *value = equals + 1;
        trim(key);
        trim(value);
        const char *parsed_key = key;

        // Allow literal '=' keys represented as '==<value>'
        if (parsed_key[0] == '\0' && value[0] == '=') {
            parsed_key = "=";
            value++;
            trim(value);
        }

        if (strcmp(section, "meta") == 0) {
            // Meta section
            if (strcmp(key, "name") == 0) {
                strncpy(keymap->name, value, sizeof(keymap->name) - 1);
            } else if (strcmp(key, "description") == 0) {
                strncpy(keymap->description, value, sizeof(keymap->description) - 1);
            }
        } else if (strcmp(section, "map") == 0) {
            // Map section
            if (keymap->num_entries >= MAX_KEYMAP_ENTRIES) {
                C64_LOG_WARNING(KEYBOARD_LOG_PREFIX "Too many keymap entries, ignoring: %s=%s", key, value);
                continue;
            }

            keymap_entry_t *entry = &keymap->entries[keymap->num_entries];
            strncpy(entry->key, parsed_key, sizeof(entry->key) - 1);

            // Parse value
            if (strncmp(value, "c64:", 4) == 0) {
                // Symbolic key
                entry->is_symbolic = true;
                strncpy(entry->symbol, value, sizeof(entry->symbol) - 1);
                entry->value = lookup_symbolic_key(value);
                if (entry->value == 0) {
                    C64_LOG_WARNING(KEYBOARD_LOG_PREFIX "Unknown symbolic key: %s", value);
                    continue;
                }
            } else if (strncmp(value, "0x", 2) == 0 || strncmp(value, "0X", 2) == 0) {
                // Hex value
                entry->is_symbolic = false;
                entry->value = (uint8_t)strtol(value, NULL, 16);
            } else if (isdigit((unsigned char)value[0])) {
                // Decimal value
                entry->is_symbolic = false;
                entry->value = (uint8_t)atoi(value);
            } else {
                // Literal character
                entry->is_symbolic = false;
                entry->value = (uint8_t)value[0];
            }

            keymap->num_entries++;
        }
    }

    fclose(file);

    C64_LOG_INFO(KEYBOARD_LOG_PREFIX "Loaded keymap: %s (%d entries)", keymap->name, keymap->num_entries);
    return keymap;
}

void c64_keymap_destroy(c64_keymap_t *keymap)
{
    if (!keymap) {
        return;
    }
    free(keymap);
}

const char *c64_keymap_get_name(c64_keymap_t *keymap)
{
    if (!keymap) {
        return "";
    }
    return keymap->name;
}

bool c64_keymap_convert(c64_keymap_t *keymap, const char *key_code, int modifiers, c64_output_t *output)
{
    if (!keymap || !key_code || !output) {
        return false;
    }

    // Build key string with modifiers (e.g., "Shift+KeyA")
    char key_with_mods[128] = "";
    if (modifiers & 0x01) { // Shift
        strcat(key_with_mods, "Shift+");
    }
    if (modifiers & 0x02) { // Ctrl
        strcat(key_with_mods, "Ctrl+");
    }
    if (modifiers & 0x04) { // Alt
        strcat(key_with_mods, "Alt+");
    }
    if (modifiers & 0x08) { // Meta
        strcat(key_with_mods, "Meta+");
    }
    strcat(key_with_mods, key_code);

    // Try exact match first (with modifiers)
    for (int i = 0; i < keymap->num_entries; i++) {
        if (strcmp(keymap->entries[i].key, key_with_mods) == 0) {
            if (keymap->entries[i].is_symbolic) {
                // Symbolic output
                output->mode = C64_OUTPUT_SYMBOLIC;
                strncpy(output->data.symbol, keymap->entries[i].symbol, sizeof(output->data.symbol) - 1);
                output->data.symbol[sizeof(output->data.symbol) - 1] = '\0';
            } else {
                // PETSCII output
                output->mode = C64_OUTPUT_PETSCII;
                output->data.petscii = keymap->entries[i].value;
            }
            return true;
        }
    }

    // If no match with modifiers, try plain key (ignoring modifiers)
    for (int i = 0; i < keymap->num_entries; i++) {
        if (strcmp(keymap->entries[i].key, key_code) == 0) {
            if (keymap->entries[i].is_symbolic) {
                // Symbolic output
                output->mode = C64_OUTPUT_SYMBOLIC;
                strncpy(output->data.symbol, keymap->entries[i].symbol, sizeof(output->data.symbol) - 1);
                output->data.symbol[sizeof(output->data.symbol) - 1] = '\0';
            } else {
                // PETSCII output
                output->mode = C64_OUTPUT_PETSCII;
                output->data.petscii = keymap->entries[i].value;
            }

            return true;
        }
    }

    // No mapping found
    C64_LOG_DEBUG(KEYBOARD_LOG_PREFIX "No mapping for key: %s (mods=0x%02X)", key_code, modifiers);
    return false;
}

// Queue helper functions
static bool queue_push(c64_keyboard_t *keyboard, uint8_t byte)
{
    pthread_mutex_lock(&keyboard->queue_mutex);

    if (keyboard->queue_count >= QUEUE_SIZE) {
        pthread_mutex_unlock(&keyboard->queue_mutex);
        return false; // Queue full
    }

    keyboard->queue[keyboard->queue_tail] = byte;
    keyboard->queue_tail = (keyboard->queue_tail + 1) % QUEUE_SIZE;
    keyboard->queue_count++;

    pthread_mutex_unlock(&keyboard->queue_mutex);
    return true;
}

static bool queue_pop(c64_keyboard_t *keyboard, uint8_t *byte)
{
    pthread_mutex_lock(&keyboard->queue_mutex);

    if (keyboard->queue_count == 0) {
        pthread_mutex_unlock(&keyboard->queue_mutex);
        return false; // Queue empty
    }

    *byte = keyboard->queue[keyboard->queue_head];
    keyboard->queue_head = (keyboard->queue_head + 1) % QUEUE_SIZE;
    keyboard->queue_count--;

    pthread_mutex_unlock(&keyboard->queue_mutex);
    return true;
}

static int queue_available(c64_keyboard_t *keyboard)
{
    pthread_mutex_lock(&keyboard->queue_mutex);
    int count = keyboard->queue_count;
    pthread_mutex_unlock(&keyboard->queue_mutex);
    return count;
}

static void queue_flush(c64_keyboard_t *keyboard)
{
    pthread_mutex_lock(&keyboard->queue_mutex);
    keyboard->queue_head = 0;
    keyboard->queue_tail = 0;
    keyboard->queue_count = 0;
    pthread_mutex_unlock(&keyboard->queue_mutex);
}

// Worker thread function
static void *injection_worker(void *arg)
{
    c64_keyboard_t *keyboard = (c64_keyboard_t *)arg;

    C64_LOG_INFO(KEYBOARD_LOG_PREFIX "Injection worker started");

    while (keyboard->worker_running) {
        // Check if there are bytes to inject
        int pending = queue_available(keyboard);
        if (pending == 0) {
            os_sleep_ms(POLL_INTERVAL_MS);
            continue;
        }

        // Read C64 keyboard buffer length ($00C6)
        uint8_t buffer_length = 0;
        int bytes_read =
            c64_rest_read_memory(keyboard->rest_client, C64_KEYBOARD_LENGTH, 1, &buffer_length, sizeof(buffer_length));

        if (bytes_read < 0) {
            C64_LOG_ERROR(KEYBOARD_LOG_PREFIX "Failed to read keyboard buffer length");
            os_sleep_ms(POLL_INTERVAL_MS);
            continue;
        }

        // Inject bytes only if buffer is empty
        if (buffer_length == 0) {
            // Inject up to 10 bytes (C64 keyboard buffer size)
            uint8_t inject_buffer[C64_KEYBOARD_BUFFER_SIZE];
            int inject_count = 0;

            while (inject_count < C64_KEYBOARD_BUFFER_SIZE && queue_pop(keyboard, &inject_buffer[inject_count])) {
                inject_count++;
            }

            if (inject_count > 0) {
                // Write bytes to keyboard buffer ($0277-$0280)
                if (!c64_rest_write_memory(keyboard->rest_client, C64_KEYBOARD_BUFFER, inject_buffer, inject_count)) {
                    C64_LOG_ERROR(KEYBOARD_LOG_PREFIX "Failed to write keyboard buffer");
                    continue;
                }

                // Update buffer length ($00C6)
                buffer_length = (uint8_t)inject_count;
                if (!c64_rest_write_memory(keyboard->rest_client, C64_KEYBOARD_LENGTH, &buffer_length, 1)) {
                    C64_LOG_ERROR(KEYBOARD_LOG_PREFIX "Failed to write buffer length");
                    continue;
                }

                C64_LOG_DEBUG(KEYBOARD_LOG_PREFIX "Injected %d bytes", inject_count);
            }
        }

        os_sleep_ms(POLL_INTERVAL_MS);
    }

    C64_LOG_INFO(KEYBOARD_LOG_PREFIX "Injection worker stopped");
    return NULL;
}

c64_keyboard_t *c64_keyboard_create(void *rest_client)
{
    if (!rest_client) {
        return NULL;
    }

    c64_keyboard_t *keyboard = calloc(1, sizeof(c64_keyboard_t));
    if (!keyboard) {
        return NULL;
    }

    keyboard->rest_client = (c64_rest_client_t *)rest_client;
    keyboard->capturing = false;
    strncpy(keyboard->status, "idle", sizeof(keyboard->status) - 1);

    // Initialize queue
    keyboard->queue_head = 0;
    keyboard->queue_tail = 0;
    keyboard->queue_count = 0;
    pthread_mutex_init(&keyboard->queue_mutex, NULL);

    // Start worker thread
    keyboard->worker_running = true;
    if (pthread_create(&keyboard->worker_thread, NULL, injection_worker, keyboard) != 0) {
        C64_LOG_ERROR(KEYBOARD_LOG_PREFIX "Failed to create worker thread");
        pthread_mutex_destroy(&keyboard->queue_mutex);
        free(keyboard);
        return NULL;
    }

    C64_LOG_INFO(KEYBOARD_LOG_PREFIX "Created keyboard controller");
    return keyboard;
}

void c64_keyboard_destroy(c64_keyboard_t *keyboard)
{
    if (!keyboard) {
        return;
    }

    // Stop worker thread
    keyboard->worker_running = false;
    pthread_join(keyboard->worker_thread, NULL);

    // Cleanup
    pthread_mutex_destroy(&keyboard->queue_mutex);
    free(keyboard);
}

void c64_keyboard_set_keymap(c64_keyboard_t *keyboard, c64_keymap_t *keymap)
{
    if (!keyboard) {
        return;
    }
    keyboard->keymap = keymap;
}

void c64_keyboard_set_capture(c64_keyboard_t *keyboard, bool enabled)
{
    if (!keyboard) {
        return;
    }

    keyboard->capturing = enabled;
    if (!enabled) {
        // Flush queue when capture disabled
        queue_flush(keyboard);
        C64_LOG_INFO(KEYBOARD_LOG_PREFIX "Capture disabled, flushing queue");
    } else {
        C64_LOG_INFO(KEYBOARD_LOG_PREFIX "Capture enabled");
    }
}

bool c64_keyboard_is_capturing(c64_keyboard_t *keyboard)
{
    if (!keyboard) {
        return false;
    }
    return keyboard->capturing;
}

void c64_keyboard_queue_output(c64_keyboard_t *keyboard, const c64_output_t *output)
{
    if (!keyboard || !output) {
        return;
    }

    // Convert output to PETSCII bytes and queue them
    if (output->mode == C64_OUTPUT_PETSCII) {
        queue_push(keyboard, output->data.petscii);
        C64_LOG_INFO(KEYBOARD_LOG_PREFIX "Key → PETSCII 0x%02X | Queue: %d", output->data.petscii,
                     queue_available(keyboard));
    } else if (output->mode == C64_OUTPUT_SYMBOLIC) {
        // Symbolic keys map to single PETSCII codes
        uint8_t code = lookup_symbolic_key(output->data.symbol);
        if (code != 0) {
            queue_push(keyboard, code);
            C64_LOG_INFO(KEYBOARD_LOG_PREFIX "Key → %s (0x%02X) | Queue: %d", output->data.symbol, code,
                         queue_available(keyboard));
        } else {
            C64_LOG_WARNING(KEYBOARD_LOG_PREFIX "Unknown symbolic key: %s", output->data.symbol);
        }
    } else if (output->mode == C64_OUTPUT_TEXT) {
        // Queue each character in the text
        size_t len = 0;
        for (size_t i = 0; i < sizeof(output->data.text) && output->data.text[i] != '\0'; i++) {
            queue_push(keyboard, (uint8_t)output->data.text[i]);
            len++;
        }
        C64_LOG_INFO(KEYBOARD_LOG_PREFIX "Text → '%s' (%zu chars) | Queue: %d", output->data.text, len,
                     queue_available(keyboard));
    }
}

const char *c64_keyboard_get_status(c64_keyboard_t *keyboard)
{
    if (!keyboard) {
        return "invalid";
    }
    return keyboard->status;
}

bool c64_keyboard_basic_warm_start(c64_keyboard_t *keyboard)
{
    if (!keyboard || !keyboard->rest_client) {
        C64_LOG_ERROR(KEYBOARD_LOG_PREFIX "Invalid keyboard or REST client for warm start");
        return false;
    }

    C64_LOG_INFO(KEYBOARD_LOG_PREFIX "Performing BASIC warm start via IRQ vector");

    // Step 1: Read current IRQ vector from $0314/$0315
    uint8_t original_vector[2];
    int bytes_read =
        c64_rest_read_memory(keyboard->rest_client, C64_IRQ_VECTOR_LOW, 2, original_vector, sizeof(original_vector));
    if (bytes_read != 2) {
        C64_LOG_ERROR(KEYBOARD_LOG_PREFIX "Failed to read IRQ vector: got %d bytes", bytes_read);
        return false;
    }

    C64_LOG_DEBUG(KEYBOARD_LOG_PREFIX "Original IRQ vector: $%02X%02X", original_vector[1], original_vector[0]);

    // Step 2: Write BASIC warm start address ($A474) to IRQ vector
    // Low byte ($74) to $0314, high byte ($A4) to $0315
    uint8_t warm_start_vector[2];
    warm_start_vector[0] = 0x74; // Low byte of $A474
    warm_start_vector[1] = 0xA4; // High byte of $A474

    if (!c64_rest_write_memory(keyboard->rest_client, C64_IRQ_VECTOR_LOW, warm_start_vector, 2)) {
        C64_LOG_ERROR(KEYBOARD_LOG_PREFIX "Failed to write warm start vector");
        return false;
    }

    C64_LOG_DEBUG(KEYBOARD_LOG_PREFIX "Wrote BASIC warm start vector $A474 to IRQ");

    // Step 3: Delay to allow warm start to take effect
    // This delay happens in the keyboard worker thread, not the video/audio thread
    os_sleep_ms(C64_WARM_START_DELAY_MS);

    // Step 4: Restore original IRQ vector
    if (!c64_rest_write_memory(keyboard->rest_client, C64_IRQ_VECTOR_LOW, original_vector, 2)) {
        C64_LOG_ERROR(KEYBOARD_LOG_PREFIX "Failed to restore original IRQ vector");
        // Continue anyway - the warm start should have taken effect
    } else {
        C64_LOG_DEBUG(KEYBOARD_LOG_PREFIX "Restored original IRQ vector: $%02X%02X", original_vector[1],
                      original_vector[0]);
    }

    C64_LOG_INFO(KEYBOARD_LOG_PREFIX "BASIC warm start completed");
    return true;
}

// Helper structure for keymap discovery
typedef struct {
    char **keymap_paths;
    size_t *count;
    size_t *capacity;
} keymap_discovery_ctx_t;

// Helper function to process a single directory (platform-specific implementations)
static void process_keymap_directory(const char *dir_path, bool is_user_dir, keymap_discovery_ctx_t *ctx)
{
    if (!dir_path || !ctx) {
        return;
    }

#ifdef _WIN32
    // Windows implementation
    WIN32_FIND_DATAA find_data;
    char search_path[MAX_PATH];
    int written = snprintf(search_path, sizeof(search_path), "%s\\*.c64keymap.ini", dir_path);
    if (written < 0 || (size_t)written >= sizeof(search_path)) {
        return;
    }

    HANDLE h_find = FindFirstFileA(search_path, &find_data);
    if (h_find == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }

        // Extract keymap name (strip .c64keymap.ini)
        char name[256];
        strncpy(name, find_data.cFileName, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        char *suffix = strstr(name, ".c64keymap.ini");
        if (suffix) {
            *suffix = '\0';
        }

        // Check for duplicate (user keymap overrides builtin)
        if (is_user_dir) {
            bool duplicate = false;
            for (size_t i = 0; i < *ctx->count; i++) {
                if (strcmp(ctx->keymap_paths[i], name) == 0) {
                    duplicate = true;
                    C64_LOG_INFO(KEYBOARD_LOG_PREFIX "User keymap '%s' overrides builtin", name);
                    break;
                }
            }
            if (duplicate) {
                continue;
            }
        }

        // Expand array if needed
        if (*ctx->count >= *ctx->capacity) {
            *ctx->capacity *= 2;
            char **new_paths = (char **)realloc(ctx->keymap_paths, *ctx->capacity * sizeof(char *));
            if (!new_paths) {
                FindClose(h_find);
                return;
            }
            ctx->keymap_paths = new_paths;
        }

        ctx->keymap_paths[*ctx->count] = strdup(name);
        if (ctx->keymap_paths[*ctx->count]) {
            (*ctx->count)++;
        }
    } while (FindNextFileA(h_find, &find_data));

    FindClose(h_find);
#else
    // Unix implementation
    DIR *dir = opendir(dir_path);
    if (!dir) {
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_REG) {
            continue;
        }

        // Check for .c64keymap.ini extension
        if (strstr(entry->d_name, ".c64keymap.ini") == NULL) {
            continue;
        }

        // Extract keymap name (strip .c64keymap.ini)
        char name[256];
        strncpy(name, entry->d_name, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        char *suffix = strstr(name, ".c64keymap.ini");
        if (suffix) {
            *suffix = '\0';
        }

        // Check for duplicate (user keymap overrides builtin)
        if (is_user_dir) {
            bool duplicate = false;
            for (size_t i = 0; i < *ctx->count; i++) {
                if (strcmp(ctx->keymap_paths[i], name) == 0) {
                    duplicate = true;
                    C64_LOG_INFO(KEYBOARD_LOG_PREFIX "User keymap '%s' overrides builtin", name);
                    break;
                }
            }
            if (duplicate) {
                continue;
            }
        }

        // Expand array if needed
        if (*ctx->count >= *ctx->capacity) {
            *ctx->capacity *= 2;
            char **new_paths = (char **)realloc(ctx->keymap_paths, *ctx->capacity * sizeof(char *));
            if (!new_paths) {
                closedir(dir);
                return;
            }
            ctx->keymap_paths = new_paths;
        }

        ctx->keymap_paths[*ctx->count] = strdup(name);
        if (ctx->keymap_paths[*ctx->count]) {
            (*ctx->count)++;
        }
    }

    closedir(dir);
#endif
}

bool c64_keyboard_discover_keymaps(char ***paths, size_t *count)
{
    if (!paths || !count) {
        return false;
    }

    *paths = NULL;
    *count = 0;

    // Get the plugin's data directory from OBS
    const char *data_path = obs_get_module_data_path(obs_current_module());
    if (!data_path) {
        C64_LOG_WARNING(KEYBOARD_LOG_PREFIX "Failed to get module data path");
        return false;
    }

    // Build keymaps subdirectory path
    char builtin_dir[512];
    snprintf(builtin_dir, sizeof(builtin_dir), "%s/keymaps", data_path);

    // Initialize discovery context
    size_t capacity = 16;
    char **keymap_paths = (char **)calloc(capacity, sizeof(char *));
    if (!keymap_paths) {
        return false;
    }

    keymap_discovery_ctx_t ctx = {.keymap_paths = keymap_paths, .count = count, .capacity = &capacity};

    // Process builtin directory
    process_keymap_directory(builtin_dir, false, &ctx);

    // Process user directory
    char user_keymap_dir[512];
    if (c64_get_user_dir(C64_USER_DIR_ROOT, user_keymap_dir, sizeof(user_keymap_dir))) {
        // Append /keymaps to base user directory
        strncat(user_keymap_dir, "/keymaps", sizeof(user_keymap_dir) - strlen(user_keymap_dir) - 1);
        process_keymap_directory(user_keymap_dir, true, &ctx);
    }

    if (*count == 0) {
        free(ctx.keymap_paths);
        return false;
    }

    C64_LOG_INFO(KEYBOARD_LOG_PREFIX "Discovered %zu keymaps", *count);
    *paths = ctx.keymap_paths;
    return true;
}
