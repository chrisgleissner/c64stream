/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-keyboard.h"
#include "c64-logging.h"
#include "c64-rest-client.h"
#include "c64-stream-control.h"
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

#ifdef _WIN32
static char *c64_strtok_r(char *str, const char *delim, char **saveptr)
{
    return strtok_s(str, delim, saveptr);
}
#else
static char *c64_strtok_r(char *str, const char *delim, char **saveptr)
{
    return strtok_r(str, delim, saveptr);
}
#endif

#define KEYBOARD_LOG_PREFIX "🕹 KEYBOARD: "
#define MAX_KEYMAP_ENTRIES 512
#define KEYMAP_VALUE_SEQUENCE_MAX 8
#define QUEUE_SIZE 1024

#ifndef C64_KEYBOARD_POLL_INITIAL_MS
#define C64_KEYBOARD_POLL_INITIAL_MS 50
#endif

#ifndef C64_KEYBOARD_POLL_MAX_MS
#define C64_KEYBOARD_POLL_MAX_MS 500
#endif

#ifndef C64_KEYBOARD_MAX_RETRIES
#define C64_KEYBOARD_MAX_RETRIES 20
#endif

// C64 memory locations
#define C64_KEYBOARD_BUFFER 0x0277
#define C64_KEYBOARD_LENGTH 0x00C6
#define C64_KEYBOARD_BUFFER_SIZE 10
#define C64_REST_INPUT_BATCH_SIZE 64
#define C64_STOP_FLAG 0x0091        // RUN/STOP flag (bit 7)
#define C64_IRQ_VECTOR_LOW 0x0314   // IRQ vector low byte
#define C64_IRQ_VECTOR_HIGH 0x0315  // IRQ vector high byte
#define C64_BASIC_WARM_START 0xA474 // BASIC warm start routine address
#define C64_WARM_START_DELAY_MS 40  // Delay for warm start to take effect

// Keymap entry
typedef struct {
    char key[64];  // Input key name (e.g., "a", "return", "f1")
    uint8_t value; // PETSCII code or special value
    uint8_t values[KEYMAP_VALUE_SEQUENCE_MAX];
    uint8_t value_count;
    uint8_t value_index;
    bool is_symbolic; // True if value is symbolic name
    char symbol[32];  // Symbolic name (e.g., "c64:RETURN")
} keymap_entry_t;

// Symbolic key definitions
typedef struct {
    const char *name;
    uint8_t code;
} symbolic_key_t;

typedef enum {
    C64_KEYBOARD_STATE_IDLE = 0,
    C64_KEYBOARD_STATE_WAIT_BUFFER_EMPTY,
    C64_KEYBOARD_STATE_WRITE,
    C64_KEYBOARD_STATE_VERIFY,
    C64_KEYBOARD_STATE_COMPLETE,
    C64_KEYBOARD_STATE_FAILED,
} c64_keyboard_state_t;

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
    int transport;
    char status[64];
    uint64_t queued_submission_count;

    // FIFO queue for keystroke bytes
    uint8_t queue[QUEUE_SIZE];
    int queue_head;
    int queue_tail;
    int queue_count;
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;

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

static int ascii_tolower(int ch)
{
    return tolower((unsigned char)ch);
}

static bool string_ieq(const char *lhs, const char *rhs)
{
    if (!lhs || !rhs) {
        return false;
    }

    while (*lhs && *rhs) {
        if (ascii_tolower(*lhs) != ascii_tolower(*rhs)) {
            return false;
        }
        lhs++;
        rhs++;
    }

    return *lhs == '\0' && *rhs == '\0';
}

static bool string_starts_with_i(const char *value, const char *prefix)
{
    if (!value || !prefix) {
        return false;
    }

    while (*prefix) {
        if (*value == '\0' || ascii_tolower(*value) != ascii_tolower(*prefix)) {
            return false;
        }
        value++;
        prefix++;
    }

    return true;
}

static bool append_modifier_prefixes(int modifiers, char normalized[64])
{
    normalized[0] = '\0';

    if (modifiers & 0x01) {
        strncat(normalized, "Shift+", 63 - strlen(normalized));
    }
    if (modifiers & 0x02) {
        strncat(normalized, "Ctrl+", 63 - strlen(normalized));
    }
    if (modifiers & 0x04) {
        strncat(normalized, "Alt+", 63 - strlen(normalized));
    }
    if (modifiers & 0x08) {
        strncat(normalized, "Meta+", 63 - strlen(normalized));
    }

    return true;
}

static bool normalize_function_key(const char *token, char normalized[64])
{
    if (!token || ascii_tolower(token[0]) != 'f') {
        return false;
    }

    const char *digits = token + 1;
    if (!isdigit((unsigned char)digits[0])) {
        return false;
    }

    const long value = strtol(digits, NULL, 10);
    if (value < 1 || value > 24) {
        return false;
    }

    snprintf(normalized, 64, "F%ld", value);
    return true;
}

static bool normalize_known_base_token(const char *token, char normalized[64])
{
    if (!token || token[0] == '\0') {
        return false;
    }

    if (normalize_function_key(token, normalized)) {
        return true;
    }

    if (string_ieq(token, "space")) {
        return snprintf(normalized, 64, "Space") > 0;
    }
    if (string_ieq(token, "enter") || string_ieq(token, "return")) {
        return snprintf(normalized, 64, "Enter") > 0;
    }
    if (string_ieq(token, "backspace")) {
        return snprintf(normalized, 64, "Backspace") > 0;
    }
    if (string_ieq(token, "delete")) {
        return snprintf(normalized, 64, "Delete") > 0;
    }
    if (string_ieq(token, "insert")) {
        return snprintf(normalized, 64, "Insert") > 0;
    }
    if (string_ieq(token, "home")) {
        return snprintf(normalized, 64, "Home") > 0;
    }
    if (string_ieq(token, "end")) {
        return snprintf(normalized, 64, "End") > 0;
    }
    if (string_ieq(token, "pageup") || string_ieq(token, "page_up") || string_ieq(token, "prior")) {
        return snprintf(normalized, 64, "PageUp") > 0;
    }
    if (string_ieq(token, "pagedown") || string_ieq(token, "page_down") || string_ieq(token, "next")) {
        return snprintf(normalized, 64, "PageDown") > 0;
    }
    if (string_ieq(token, "capslock") || string_ieq(token, "caps_lock")) {
        return snprintf(normalized, 64, "CapsLock") > 0;
    }
    if (string_ieq(token, "escape") || string_ieq(token, "esc")) {
        return snprintf(normalized, 64, "Escape") > 0;
    }
    if (string_ieq(token, "tab")) {
        return snprintf(normalized, 64, "Tab") > 0;
    }
    if (string_ieq(token, "pause")) {
        return snprintf(normalized, 64, "Pause") > 0;
    }
    if (string_ieq(token, "arrowup") || string_ieq(token, "up")) {
        return snprintf(normalized, 64, "ArrowUp") > 0;
    }
    if (string_ieq(token, "arrowdown") || string_ieq(token, "down")) {
        return snprintf(normalized, 64, "ArrowDown") > 0;
    }
    if (string_ieq(token, "arrowleft") || string_ieq(token, "left")) {
        return snprintf(normalized, 64, "ArrowLeft") > 0;
    }
    if (string_ieq(token, "arrowright") || string_ieq(token, "right")) {
        return snprintf(normalized, 64, "ArrowRight") > 0;
    }
    if (string_ieq(token, "minus")) {
        return snprintf(normalized, 64, "Minus") > 0;
    }
    if (string_ieq(token, "equal")) {
        return snprintf(normalized, 64, "Equal") > 0;
    }
    if (string_ieq(token, "backquote")) {
        return snprintf(normalized, 64, "Backquote") > 0;
    }
    if (string_ieq(token, "bracketleft")) {
        return snprintf(normalized, 64, "BracketLeft") > 0;
    }
    if (string_ieq(token, "bracketright")) {
        return snprintf(normalized, 64, "BracketRight") > 0;
    }
    if (string_ieq(token, "backslash")) {
        return snprintf(normalized, 64, "Backslash") > 0;
    }
    if (string_ieq(token, "intlbackslash")) {
        return snprintf(normalized, 64, "IntlBackslash") > 0;
    }
    if (string_ieq(token, "semicolon")) {
        return snprintf(normalized, 64, "Semicolon") > 0;
    }
    if (string_ieq(token, "quote")) {
        return snprintf(normalized, 64, "Quote") > 0;
    }
    if (string_ieq(token, "comma")) {
        return snprintf(normalized, 64, "Comma") > 0;
    }
    if (string_ieq(token, "period")) {
        return snprintf(normalized, 64, "Period") > 0;
    }
    if (string_ieq(token, "slash")) {
        return snprintf(normalized, 64, "Slash") > 0;
    }
    if (string_ieq(token, "meta")) {
        return snprintf(normalized, 64, "Meta") > 0;
    }

    if (strlen(token) == 4 && string_starts_with_i(token, "Key") && isalpha((unsigned char)token[3])) {
        return snprintf(normalized, 64, "Key%c", (char)toupper((unsigned char)token[3])) > 0;
    }
    if (strlen(token) == 6 && string_starts_with_i(token, "Digit") && isdigit((unsigned char)token[5])) {
        return snprintf(normalized, 64, "Digit%c", token[5]) > 0;
    }

    return false;
}

static bool normalize_single_char_token(char token, bool has_modifiers, char normalized[64])
{
    if (!has_modifiers) {
        normalized[0] = token;
        normalized[1] = '\0';
        return true;
    }

    if (isalpha((unsigned char)token)) {
        return snprintf(normalized, 64, "Key%c", (char)toupper((unsigned char)token)) > 0;
    }
    if (isdigit((unsigned char)token)) {
        return snprintf(normalized, 64, "Digit%c", token) > 0;
    }

    switch (token) {
    case '-':
        return snprintf(normalized, 64, "Minus") > 0;
    case '=':
    case '+':
        return snprintf(normalized, 64, "Equal") > 0;
    case '`':
        return snprintf(normalized, 64, "Backquote") > 0;
    case '[':
        return snprintf(normalized, 64, "BracketLeft") > 0;
    case ']':
        return snprintf(normalized, 64, "BracketRight") > 0;
    case '\\':
        return snprintf(normalized, 64, "Backslash") > 0;
    case ';':
        return snprintf(normalized, 64, "Semicolon") > 0;
    case '\'':
        return snprintf(normalized, 64, "Quote") > 0;
    case ',':
        return snprintf(normalized, 64, "Comma") > 0;
    case '.':
        return snprintf(normalized, 64, "Period") > 0;
    case '/':
        return snprintf(normalized, 64, "Slash") > 0;
    case '@':
        return snprintf(normalized, 64, "Digit2") > 0;
    case '*':
        return snprintf(normalized, 64, "Digit8") > 0;
    default:
        normalized[0] = token;
        normalized[1] = '\0';
        return true;
    }
}

bool c64_keymap_normalize_identifier(const char *input, char normalized[64])
{
    if (!input || !normalized || input[0] == '\0') {
        return false;
    }

    int modifiers = 0;
    const char *base = input;
    bool consumed = false;

    do {
        consumed = false;
        if (string_starts_with_i(base, "Shift+")) {
            modifiers |= 0x01;
            base += 6;
            consumed = true;
        } else if (string_starts_with_i(base, "Ctrl+")) {
            modifiers |= 0x02;
            base += 5;
            consumed = true;
        } else if (string_starts_with_i(base, "Control+")) {
            modifiers |= 0x02;
            base += 8;
            consumed = true;
        } else if (string_starts_with_i(base, "Alt+")) {
            modifiers |= 0x04;
            base += 4;
            consumed = true;
        } else if (string_starts_with_i(base, "Meta+")) {
            modifiers |= 0x08;
            base += 5;
            consumed = true;
        } else if (string_starts_with_i(base, "Cmd+")) {
            modifiers |= 0x08;
            base += 4;
            consumed = true;
        } else if (string_starts_with_i(base, "Command+")) {
            modifiers |= 0x08;
            base += 8;
            consumed = true;
        }
    } while (consumed);

    if (base[0] == '\0') {
        return false;
    }

    char normalized_base[64] = "";
    if (!normalize_known_base_token(base, normalized_base)) {
        if (base[0] != '\0' && base[1] == '\0') {
            if (!normalize_single_char_token(base[0], modifiers != 0, normalized_base)) {
                return false;
            }
        } else {
            snprintf(normalized_base, sizeof(normalized_base), "%s", base);
        }
    }

    append_modifier_prefixes(modifiers, normalized);
    strncat(normalized, normalized_base, 63 - strlen(normalized));
    normalized[63] = '\0';
    return true;
}

static bool is_supported_base_identifier(const char *identifier)
{
    if (!identifier || identifier[0] == '\0') {
        return false;
    }

    // Single-byte ASCII printable (0x21-0x7E).
    if (identifier[1] == '\0') {
        return (unsigned char)identifier[0] > 0x20 && (unsigned char)identifier[0] != 0x7F;
    }

    // Multi-byte UTF-8: lead byte 0xC0+ indicates a valid non-ASCII character.
    // These are accented letters and locale-specific symbols (e.g. ä, £, ñ).
    if ((unsigned char)identifier[0] >= 0xC0) {
        return true;
    }

    if (normalize_known_base_token(identifier, (char[64]){0})) {
        return true;
    }

    if (strlen(identifier) == 4 && strncmp(identifier, "Key", 3) == 0 && isalpha((unsigned char)identifier[3])) {
        return true;
    }
    if (strlen(identifier) == 6 && strncmp(identifier, "Digit", 5) == 0 && isdigit((unsigned char)identifier[5])) {
        return true;
    }

    return false;
}

bool c64_keymap_identifier_is_runtime_supported(const char *identifier)
{
    char normalized[64] = "";
    if (!c64_keymap_normalize_identifier(identifier, normalized)) {
        return false;
    }

    int modifiers = 0;
    const char *base = normalized;

    while (true) {
        if (string_starts_with_i(base, "Shift+")) {
            modifiers |= 0x01;
            base += 6;
            continue;
        }
        if (string_starts_with_i(base, "Ctrl+")) {
            modifiers |= 0x02;
            base += 5;
            continue;
        }
        if (string_starts_with_i(base, "Alt+")) {
            modifiers |= 0x04;
            base += 4;
            continue;
        }
        if (string_starts_with_i(base, "Meta+")) {
            modifiers |= 0x08;
            base += 5;
            continue;
        }
        break;
    }

    if (modifiers == 0x02 && strcmp(base, "Meta") == 0) {
        return true;
    }

    return is_supported_base_identifier(base);
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
            if (!c64_keymap_normalize_identifier(parsed_key, entry->key)) {
                C64_LOG_WARNING(KEYBOARD_LOG_PREFIX "Invalid key identifier: %s", parsed_key);
                continue;
            }

            // Parse value
            entry->value = 0;
            entry->value_count = 0;
            entry->value_index = 0;
            if (strncmp(value, "c64:", 4) == 0) {
                // Symbolic key
                entry->is_symbolic = true;
                strncpy(entry->symbol, value, sizeof(entry->symbol) - 1);
                entry->value = lookup_symbolic_key(value);
                if (entry->value == 0) {
                    C64_LOG_WARNING(KEYBOARD_LOG_PREFIX "Unknown symbolic key: %s", value);
                    continue;
                }
            } else {
                // PETSCII value(s)
                entry->is_symbolic = false;

                char *saveptr = NULL;
                for (char *token = c64_strtok_r(value, ",", &saveptr); token;
                     token = c64_strtok_r(NULL, ",", &saveptr)) {
                    trim(token);
                    if (token[0] == '\0') {
                        continue;
                    }

                    uint8_t parsed = 0;
                    bool parsed_ok = false;
                    if (strncmp(token, "0x", 2) == 0 || strncmp(token, "0X", 2) == 0) {
                        parsed = (uint8_t)strtol(token, NULL, 16);
                        parsed_ok = true;
                    } else if (isdigit((unsigned char)token[0])) {
                        parsed = (uint8_t)atoi(token);
                        parsed_ok = true;
                    } else {
                        parsed = (uint8_t)token[0];
                        parsed_ok = true;
                    }

                    if (!parsed_ok) {
                        continue;
                    }

                    if (entry->value_count >= KEYMAP_VALUE_SEQUENCE_MAX) {
                        C64_LOG_WARNING(KEYBOARD_LOG_PREFIX "Too many values for keymap entry: %s", parsed_key);
                        break;
                    }

                    entry->values[entry->value_count++] = parsed;
                }

                if (entry->value_count == 0) {
                    C64_LOG_WARNING(KEYBOARD_LOG_PREFIX "Invalid keymap value: %s=%s", parsed_key, value);
                    continue;
                }

                entry->value = entry->values[0];
            }

            keymap->num_entries++;
        }
    }

    fclose(file);

    C64_LOG_DEBUG(KEYBOARD_LOG_PREFIX "Loaded keymap: %s (%d entries)", keymap->name, keymap->num_entries);
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

static uint8_t keymap_entry_next_value(keymap_entry_t *entry)
{
    if (entry->value_count == 0) {
        return entry->value;
    }

    const uint8_t value = entry->values[entry->value_index];
    if (entry->value_count > 1) {
        entry->value_index = (uint8_t)((entry->value_index + 1) % entry->value_count);
    }

    return value;
}

static bool keymap_emit_special_ctrl_digit(const char *key_code, int modifiers, c64_output_t *output)
{
    if (!key_code || !output || modifiers != 0x02) {
        return false;
    }

    static const struct {
        const char *key_code;
        uint8_t petscii;
    } ctrl_digit_map[] = {{"Digit1", 0x90}, {"Digit2", 0x05}, {"Digit3", 0x1C}, {"Digit4", 0x9F}, {"Digit5", 0x9C},
                          {"Digit6", 0x1E}, {"Digit7", 0x1F}, {"Digit8", 0x9E}, {"Digit9", 0x12}, {"Digit0", 0x92}};

    for (size_t i = 0; i < sizeof(ctrl_digit_map) / sizeof(ctrl_digit_map[0]); i++) {
        if (strcmp(key_code, ctrl_digit_map[i].key_code) == 0) {
            output->mode = C64_OUTPUT_PETSCII;
            output->data.petscii = ctrl_digit_map[i].petscii;
            C64_LOG_DEBUG(KEYBOARD_LOG_PREFIX "Matched special key: %s -> PETSCII 0x%02X", key_code,
                          output->data.petscii);
            return true;
        }
    }

    return false;
}

static bool keymap_emit_entry(keymap_entry_t *entry, c64_output_t *output)
{
    if (!entry || !output) {
        return false;
    }

    if (entry->is_symbolic) {
        output->mode = C64_OUTPUT_SYMBOLIC;
        strncpy(output->data.symbol, entry->symbol, sizeof(output->data.symbol) - 1);
        output->data.symbol[sizeof(output->data.symbol) - 1] = '\0';
    } else {
        output->mode = C64_OUTPUT_PETSCII;
        output->data.petscii = keymap_entry_next_value(entry);
    }

    return true;
}

static bool keymap_lookup_identifier(c64_keymap_t *keymap, const char *candidate, c64_output_t *output)
{
    if (!keymap || !candidate || candidate[0] == '\0' || !output) {
        return false;
    }

    char normalized[64] = "";
    if (!c64_keymap_normalize_identifier(candidate, normalized)) {
        return false;
    }

    for (int i = 0; i < keymap->num_entries; i++) {
        if (strcmp(keymap->entries[i].key, normalized) == 0) {
            if (!keymap_emit_entry(&keymap->entries[i], output)) {
                return false;
            }

            if (output->mode == C64_OUTPUT_PETSCII) {
                C64_LOG_DEBUG(KEYBOARD_LOG_PREFIX "Matched keymap entry: %s -> PETSCII 0x%02X", normalized,
                              output->data.petscii);
            } else if (output->mode == C64_OUTPUT_SYMBOLIC) {
                C64_LOG_DEBUG(KEYBOARD_LOG_PREFIX "Matched keymap entry: %s -> %s", normalized, output->data.symbol);
            }
            return true;
        }
    }

    return false;
}

static bool keymap_lookup_with_modifiers(c64_keymap_t *keymap, const char *identifier, int modifiers,
                                         c64_output_t *output)
{
    if (!identifier || identifier[0] == '\0') {
        return false;
    }

    char candidate[128] = "";
    append_modifier_prefixes(modifiers, candidate);
    strncat(candidate, identifier, sizeof(candidate) - strlen(candidate) - 1);
    return keymap_lookup_identifier(keymap, candidate, output);
}

bool c64_keymap_convert(c64_keymap_t *keymap, const char *key_code, const char *key_text, int modifiers,
                        c64_output_t *output)
{
    if (!keymap || !output) {
        return false;
    }

    char normalized_code[64] = "";
    char normalized_text[64] = "";

    if (key_code && key_code[0] != '\0') {
        c64_keymap_normalize_identifier(key_code, normalized_code);
    }
    if (key_text && key_text[0] != '\0') {
        c64_keymap_normalize_identifier(key_text, normalized_text);
    }

    if (normalized_code[0] != '\0' && keymap_emit_special_ctrl_digit(normalized_code, modifiers, output)) {
        return true;
    }

    if (normalized_code[0] != '\0' && keymap_lookup_with_modifiers(keymap, normalized_code, modifiers, output)) {
        return true;
    }
    if (normalized_code[0] != '\0' && keymap_lookup_identifier(keymap, normalized_code, output)) {
        return true;
    }
    if (normalized_text[0] != '\0' && keymap_lookup_with_modifiers(keymap, normalized_text, modifiers, output)) {
        return true;
    }
    if (normalized_text[0] != '\0' && keymap_lookup_identifier(keymap, normalized_text, output)) {
        return true;
    }

    C64_LOG_DEBUG(KEYBOARD_LOG_PREFIX "No mapping for key: code=%s text=%s mods=0x%02X",
                  normalized_code[0] ? normalized_code : "<none>", normalized_text[0] ? normalized_text : "<none>",
                  modifiers);
    return false;
}

static bool queue_push_many(c64_keyboard_t *keyboard, const uint8_t *bytes, size_t count)
{
    if (!keyboard || (!bytes && count > 0)) {
        return false;
    }

    pthread_mutex_lock(&keyboard->queue_mutex);
    if (count > (size_t)(QUEUE_SIZE - keyboard->queue_count)) {
        pthread_mutex_unlock(&keyboard->queue_mutex);
        return false;
    }

    for (size_t i = 0; i < count; i++) {
        keyboard->queue[keyboard->queue_tail] = bytes[i];
        keyboard->queue_tail = (keyboard->queue_tail + 1) % QUEUE_SIZE;
    }
    keyboard->queue_count += (int)count;
    if (count > 0) {
        pthread_cond_signal(&keyboard->queue_cond);
    }
    pthread_mutex_unlock(&keyboard->queue_mutex);
    return true;
}

static int queue_peek_batch(c64_keyboard_t *keyboard, uint8_t *buffer, int max_count)
{
    pthread_mutex_lock(&keyboard->queue_mutex);

    if (!buffer || max_count <= 0 || keyboard->queue_count == 0) {
        pthread_mutex_unlock(&keyboard->queue_mutex);
        return 0;
    }

    int count = keyboard->queue_count;
    if (count > max_count) {
        count = max_count;
    }

    int index = keyboard->queue_head;
    for (int i = 0; i < count; i++) {
        buffer[i] = keyboard->queue[index];
        index = (index + 1) % QUEUE_SIZE;
    }

    pthread_mutex_unlock(&keyboard->queue_mutex);
    return count;
}

static void queue_discard_many(c64_keyboard_t *keyboard, int count)
{
    pthread_mutex_lock(&keyboard->queue_mutex);

    if (count <= 0) {
        pthread_mutex_unlock(&keyboard->queue_mutex);
        return;
    }

    if (count > keyboard->queue_count) {
        count = keyboard->queue_count;
    }

    keyboard->queue_head = (keyboard->queue_head + count) % QUEUE_SIZE;
    keyboard->queue_count -= count;

    pthread_mutex_unlock(&keyboard->queue_mutex);
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
    pthread_cond_broadcast(&keyboard->queue_cond);
    pthread_mutex_unlock(&keyboard->queue_mutex);
}

static void keyboard_set_status(c64_keyboard_t *keyboard, const char *status)
{
    if (!keyboard || !status) {
        return;
    }

    pthread_mutex_lock(&keyboard->queue_mutex);
    snprintf(keyboard->status, sizeof(keyboard->status), "%s", status);
    pthread_mutex_unlock(&keyboard->queue_mutex);
}

static uint64_t keyboard_next_submission_count(c64_keyboard_t *keyboard)
{
    if (!keyboard) {
        return 0;
    }

    pthread_mutex_lock(&keyboard->queue_mutex);
    const uint64_t submission_count = ++keyboard->queued_submission_count;
    pthread_mutex_unlock(&keyboard->queue_mutex);
    return submission_count;
}

static void keyboard_log_queued_submission(c64_keyboard_t *keyboard, uint64_t submission_count, const char *label,
                                           int queue_depth)
{
    if (!keyboard || !label || label[0] == '\0') {
        return;
    }

    if (c64_debug_logging) {
        C64_LOG_DEBUG(KEYBOARD_LOG_PREFIX "Queued #%llu %s | Queue: %d", (unsigned long long)submission_count, label,
                      queue_depth);
        return;
    }

    // OBS Studio's log handler (too_many_repeated_entries in obs-app.cpp) suppresses
    // messages after 30 consecutive blog() calls with the same format-string pointer.
    // Alternating between two static buffers gives blog() a different pointer on each
    // call so the repeat counter never reaches the 30-line suppression threshold.
    static char log_bufs[2][256];
    char *buf = log_bufs[submission_count & 1];
    snprintf(buf, sizeof(log_bufs[0]), "[c64stream] " KEYBOARD_LOG_PREFIX "Queued #%llu %s",
             (unsigned long long)submission_count, label);
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-security"
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
    blog(LOG_INFO, buf);
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
    (void)queue_depth;
}

static uint32_t keyboard_next_backoff_ms(uint32_t current_ms)
{
    if (current_ms >= C64_KEYBOARD_POLL_MAX_MS) {
        return C64_KEYBOARD_POLL_MAX_MS;
    }

    uint32_t next_ms = current_ms * 2;
    if (next_ms > C64_KEYBOARD_POLL_MAX_MS) {
        next_ms = C64_KEYBOARD_POLL_MAX_MS;
    }
    return next_ms;
}

static bool keyboard_wait_for_work(c64_keyboard_t *keyboard)
{
    pthread_mutex_lock(&keyboard->queue_mutex);
    while (keyboard->worker_running && keyboard->queue_count == 0) {
        pthread_cond_wait(&keyboard->queue_cond, &keyboard->queue_mutex);
    }
    bool has_work = keyboard->worker_running && keyboard->queue_count > 0;
    pthread_mutex_unlock(&keyboard->queue_mutex);
    return has_work;
}

static int keyboard_get_transport(c64_keyboard_t *keyboard)
{
    int transport;
    pthread_mutex_lock(&keyboard->queue_mutex);
    transport = keyboard->transport;
    pthread_mutex_unlock(&keyboard->queue_mutex);
    return transport;
}

/* PETSCII and text both enter the queue as bytes. Keep their matrix mapping in
 * one place so a shifted character is sent as a single hardware chord. */
static bool petscii_to_matrix(uint8_t value, const char **key, bool *shift)
{
    static const char *const letters[] = {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
                                          "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z"};
    static const char *const digits[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
    static const char *const punctuation[128] = {
        [' '] = "space",    ['!'] = "1",         ['\"'] = "2",        ['#'] = "3",     ['$'] = "4",
        ['%'] = "5",        ['&'] = "6",         ['\''] = "7",        ['('] = "8",     [')'] = "9",
        ['*'] = "8",        ['+'] = "plus",      [','] = "comma",     ['-'] = "minus", ['.'] = "period",
        ['/'] = "slash",    [':'] = "colon",     [';'] = "semicolon", ['<'] = "comma", ['='] = "equals",
        ['>'] = "period",   ['?'] = "slash",     ['@'] = "at",        ['['] = "colon", ['\\'] = "semicolon",
        [']'] = "minus",    ['^'] = "arrow_up",  ['_'] = "plus",      ['`'] = "pound", ['{'] = "at",
        ['|'] = "asterisk", ['}'] = "semicolon", ['~'] = "clear"};
    if (!key || !shift || value < 32 || value > 126) {
        return false;
    }
    if (value >= 'A' && value <= 'Z') {
        *key = letters[value - 'A'];
        *shift = true;
        return true;
    }
    if (value >= 'a' && value <= 'z') {
        *key = letters[value - 'a'];
        *shift = false;
        return true;
    }
    if (value >= '0' && value <= '9') {
        *key = digits[value - '0'];
        *shift = false;
        return true;
    }
    *key = punctuation[value];
    *shift = strchr("!\"#$%&)*,<>?", value) != NULL;
    return *key != NULL;
}

static bool build_rest_input_batch(const uint8_t *bytes, int count, char *json, size_t json_size)
{
    if (!bytes || count <= 0 || count > C64_REST_INPUT_BATCH_SIZE || !json || json_size < 32) {
        return false;
    }
    size_t used = (size_t)snprintf(json, json_size, "{\"events\":[");
    for (int i = 0; i < count; i++) {
        const char *key = NULL;
        bool shift = false;
        if (!petscii_to_matrix(bytes[i], &key, &shift)) {
            return false;
        }
        int written = snprintf(json + used, json_size - used,
                               "%s{\"kind\":\"keyboard\",\"inputs\":[%s\"%s\"],\"transition\":\"tap\"}", i ? "," : "",
                               shift ? "\"left_shift\",\"" : "", key);
        if (written < 0 || (size_t)written >= json_size - used) {
            return false;
        }
        used += (size_t)written;
    }
    return snprintf(json + used, json_size - used, "]}") > 0;
}

// Worker thread function
static void *injection_worker(void *arg)
{
    c64_keyboard_t *keyboard = (c64_keyboard_t *)arg;
    uint8_t pending_buffer[C64_REST_INPUT_BATCH_SIZE] = {0};
    int pending_count = 0;
    c64_keyboard_state_t state = C64_KEYBOARD_STATE_IDLE;
    uint32_t retry_count = 0;
    uint32_t backoff_ms = C64_KEYBOARD_POLL_INITIAL_MS;
    bool last_batch_failed = false;

    C64_LOG_DEBUG(KEYBOARD_LOG_PREFIX "Injection worker started");

    while (keyboard->worker_running) {
        switch (state) {
        case C64_KEYBOARD_STATE_IDLE:
            if (!last_batch_failed) {
                keyboard_set_status(keyboard, "idle");
            }
            pending_count = queue_peek_batch(keyboard, pending_buffer, C64_REST_INPUT_BATCH_SIZE);
            if (pending_count == 0) {
                if (!keyboard_wait_for_work(keyboard)) {
                    continue;
                }
                pending_count = queue_peek_batch(keyboard, pending_buffer, C64_REST_INPUT_BATCH_SIZE);
                if (pending_count == 0) {
                    continue;
                }
            }

            char json[8192];
            const int transport = keyboard_get_transport(keyboard);
            if (transport != C64_STREAM_TRANSPORT_LEGACY &&
                build_rest_input_batch(pending_buffer, pending_count, json, sizeof(json))) {
                c64_rest_outcome_t outcome = C64_REST_UNREACHABLE;
                long status = 0;
                if (c64_rest_machine_input_with_outcome(keyboard->rest_client, json, &outcome, &status)) {
                    queue_discard_many(keyboard, pending_count);
                    pending_count = 0;
                    last_batch_failed = false;
                    keyboard_set_status(keyboard, "complete");
                    continue;
                }
                if (transport == C64_STREAM_TRANSPORT_REST || outcome != C64_REST_NOT_SUPPORTED) {
                    C64_LOG_ERROR(KEYBOARD_LOG_PREFIX "machine:input refused batch (HTTP %ld); no legacy fallback",
                                  status);
                    queue_discard_many(keyboard, pending_count);
                    pending_count = 0;
                    last_batch_failed = true;
                    keyboard_set_status(keyboard, "failed");
                    continue;
                }
                C64_LOG_INFO(KEYBOARD_LOG_PREFIX "machine:input unavailable; retrying batch via legacy input");
            } else if (transport == C64_STREAM_TRANSPORT_REST) {
                C64_LOG_ERROR(KEYBOARD_LOG_PREFIX "input cannot be represented by machine:input; no legacy fallback");
                queue_discard_many(keyboard, pending_count);
                pending_count = 0;
                last_batch_failed = true;
                keyboard_set_status(keyboard, "failed");
                continue;
            }
            /* Legacy KERNAL input is constrained to ten bytes. On REST
             * demotion the queue remains ordered and subsequent chunks retry. */
            if (pending_count > C64_KEYBOARD_BUFFER_SIZE) {
                pending_count = C64_KEYBOARD_BUFFER_SIZE;
            }
            last_batch_failed = false;
            retry_count = 0;
            backoff_ms = C64_KEYBOARD_POLL_INITIAL_MS;
            state = C64_KEYBOARD_STATE_WAIT_BUFFER_EMPTY;
            keyboard_set_status(keyboard, "waiting-buffer-empty");
            break;

        case C64_KEYBOARD_STATE_WAIT_BUFFER_EMPTY: {
            uint8_t buffer_length = 0;
            int bytes_read = c64_rest_read_memory(keyboard->rest_client, C64_KEYBOARD_LENGTH, 1, &buffer_length,
                                                  sizeof(buffer_length));
            if (bytes_read == 1 && buffer_length == 0) {
                retry_count = 0;
                backoff_ms = C64_KEYBOARD_POLL_INITIAL_MS;
                state = C64_KEYBOARD_STATE_WRITE;
                keyboard_set_status(keyboard, "writing");
                break;
            }

            retry_count++;
            if (retry_count >= C64_KEYBOARD_MAX_RETRIES) {
                C64_LOG_ERROR(KEYBOARD_LOG_PREFIX
                              "Keyboard buffer did not empty after %u retries, aborting %d-byte batch",
                              retry_count, pending_count);
                state = C64_KEYBOARD_STATE_FAILED;
                keyboard_set_status(keyboard, "timeout");
                break;
            }

            os_sleep_ms(backoff_ms);
            backoff_ms = keyboard_next_backoff_ms(backoff_ms);
            break;
        }

        case C64_KEYBOARD_STATE_WRITE: {
            uint8_t stop_flag = 0;
            uint8_t buffer_length = (uint8_t)pending_count;

            if (!c64_rest_write_memory(keyboard->rest_client, C64_STOP_FLAG, &stop_flag, 1) ||
                !c64_rest_write_memory(keyboard->rest_client, C64_KEYBOARD_BUFFER, pending_buffer,
                                       (size_t)pending_count) ||
                !c64_rest_write_memory(keyboard->rest_client, C64_KEYBOARD_LENGTH, &buffer_length, 1)) {
                retry_count++;
                if (retry_count >= C64_KEYBOARD_MAX_RETRIES) {
                    C64_LOG_ERROR(KEYBOARD_LOG_PREFIX "Failed to write %d-byte keyboard batch after %u retries",
                                  pending_count, retry_count);
                    state = C64_KEYBOARD_STATE_FAILED;
                    keyboard_set_status(keyboard, "timeout");
                    break;
                }

                os_sleep_ms(backoff_ms);
                backoff_ms = keyboard_next_backoff_ms(backoff_ms);
                break;
            }

            retry_count = 0;
            backoff_ms = C64_KEYBOARD_POLL_INITIAL_MS;
            state = C64_KEYBOARD_STATE_VERIFY;
            keyboard_set_status(keyboard, "verifying");
            break;
        }

        case C64_KEYBOARD_STATE_VERIFY: {
            uint8_t buffer_length = 0;
            int bytes_read = c64_rest_read_memory(keyboard->rest_client, C64_KEYBOARD_LENGTH, 1, &buffer_length,
                                                  sizeof(buffer_length));
            if (bytes_read == 1 && (buffer_length == (uint8_t)pending_count || buffer_length == 0)) {
                state = C64_KEYBOARD_STATE_COMPLETE;
                keyboard_set_status(keyboard, "complete");
                break;
            }

            retry_count++;
            if (retry_count >= C64_KEYBOARD_MAX_RETRIES) {
                C64_LOG_ERROR(KEYBOARD_LOG_PREFIX "Keyboard batch verification failed after %u retries", retry_count);
                state = C64_KEYBOARD_STATE_FAILED;
                keyboard_set_status(keyboard, "timeout");
                break;
            }

            os_sleep_ms(backoff_ms);
            backoff_ms = keyboard_next_backoff_ms(backoff_ms);
            break;
        }

        case C64_KEYBOARD_STATE_COMPLETE:
            queue_discard_many(keyboard, pending_count);
            C64_LOG_DEBUG(KEYBOARD_LOG_PREFIX "Injected %d bytes with verification", pending_count);
            pending_count = 0;
            retry_count = 0;
            backoff_ms = C64_KEYBOARD_POLL_INITIAL_MS;
            last_batch_failed = false;
            state = C64_KEYBOARD_STATE_IDLE;
            break;

        case C64_KEYBOARD_STATE_FAILED:
            queue_discard_many(keyboard, pending_count);
            pending_count = 0;
            retry_count = 0;
            backoff_ms = C64_KEYBOARD_POLL_INITIAL_MS;
            last_batch_failed = true;
            state = C64_KEYBOARD_STATE_IDLE;
            break;
        }
    }

    keyboard_set_status(keyboard, "idle");
    C64_LOG_DEBUG(KEYBOARD_LOG_PREFIX "Injection worker stopped");
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
    keyboard->transport = C64_STREAM_TRANSPORT_AUTO;
    strncpy(keyboard->status, "idle", sizeof(keyboard->status) - 1);

    // Initialize queue
    keyboard->queue_head = 0;
    keyboard->queue_tail = 0;
    keyboard->queue_count = 0;
    pthread_mutex_init(&keyboard->queue_mutex, NULL);
    pthread_cond_init(&keyboard->queue_cond, NULL);

    // Start worker thread
    keyboard->worker_running = true;
    if (pthread_create(&keyboard->worker_thread, NULL, injection_worker, keyboard) != 0) {
        C64_LOG_ERROR(KEYBOARD_LOG_PREFIX "Failed to create worker thread");
        pthread_cond_destroy(&keyboard->queue_cond);
        pthread_mutex_destroy(&keyboard->queue_mutex);
        free(keyboard);
        return NULL;
    }

    C64_LOG_DEBUG(KEYBOARD_LOG_PREFIX "Created keyboard controller");
    return keyboard;
}

void c64_keyboard_destroy(c64_keyboard_t *keyboard)
{
    if (!keyboard) {
        return;
    }

    // Stop worker thread
    keyboard->worker_running = false;
    pthread_mutex_lock(&keyboard->queue_mutex);
    pthread_cond_broadcast(&keyboard->queue_cond);
    pthread_mutex_unlock(&keyboard->queue_mutex);
    pthread_join(keyboard->worker_thread, NULL);

    // Cleanup
    pthread_cond_destroy(&keyboard->queue_cond);
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

void c64_keyboard_set_transport(c64_keyboard_t *keyboard, int transport)
{
    if (!keyboard || transport < C64_STREAM_TRANSPORT_AUTO || transport > C64_STREAM_TRANSPORT_LEGACY) {
        return;
    }
    pthread_mutex_lock(&keyboard->queue_mutex);
    keyboard->transport = transport;
    pthread_mutex_unlock(&keyboard->queue_mutex);
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
        C64_LOG_DEBUG(KEYBOARD_LOG_PREFIX "Capture disabled, flushing queue");
    } else {
        C64_LOG_DEBUG(KEYBOARD_LOG_PREFIX "Capture enabled");
    }
}

bool c64_keyboard_is_capturing(c64_keyboard_t *keyboard)
{
    if (!keyboard) {
        return false;
    }
    return keyboard->capturing;
}

bool c64_keyboard_release_all(c64_keyboard_t *keyboard)
{
    if (!keyboard || !keyboard->rest_client) {
        return false;
    }
    const bool ok = c64_rest_release_all(keyboard->rest_client);
    if (!ok) {
        C64_LOG_WARNING(KEYBOARD_LOG_PREFIX "release_all failed (HTTP %ld)",
                        c64_rest_get_last_status(keyboard->rest_client));
    }
    return ok;
}

bool c64_keyboard_queue_output(c64_keyboard_t *keyboard, const c64_output_t *output)
{
    if (!keyboard || !output) {
        return false;
    }

    const uint64_t submission_count = keyboard_next_submission_count(keyboard);
    uint8_t bytes[sizeof(output->data.text)] = {0};
    size_t count = 0;
    char label[288] = {0};

    if (output->mode == C64_OUTPUT_PETSCII) {
        bytes[count++] = output->data.petscii;
        snprintf(label, sizeof(label), "PETSCII 0x%02X", output->data.petscii);
    } else if (output->mode == C64_OUTPUT_SYMBOLIC) {
        uint8_t code = lookup_symbolic_key(output->data.symbol);
        if (code == 0) {
            C64_LOG_WARNING(KEYBOARD_LOG_PREFIX "Unknown symbolic key: %s", output->data.symbol);
            return false;
        }
        bytes[count++] = code;
        snprintf(label, sizeof(label), "%s (0x%02X)", output->data.symbol, code);
    } else if (output->mode == C64_OUTPUT_TEXT) {
        while (count < sizeof(output->data.text) && output->data.text[count] != '\0') {
            bytes[count] = (uint8_t)output->data.text[count];
            count++;
        }
        snprintf(label, sizeof(label), "text '%s' (%zu chars)", output->data.text, count);
    } else {
        C64_LOG_WARNING(KEYBOARD_LOG_PREFIX "Unknown keyboard output mode: %d", output->mode);
        return false;
    }

    if (!queue_push_many(keyboard, bytes, count)) {
        C64_LOG_WARNING(KEYBOARD_LOG_PREFIX "Failed to queue #%llu %s: queue full",
                        (unsigned long long)submission_count, label);
        return false;
    }

    keyboard_log_queued_submission(keyboard, submission_count, label, queue_available(keyboard));
    return true;
}

const char *c64_keyboard_get_status(c64_keyboard_t *keyboard)
{
#ifdef _MSC_VER
    static __declspec(thread) char status_copy[64];
#else
    static __thread char status_copy[64];
#endif

    if (!keyboard) {
        return "invalid";
    }

    pthread_mutex_lock(&keyboard->queue_mutex);
    snprintf(status_copy, sizeof(status_copy), "%s", keyboard->status);
    pthread_mutex_unlock(&keyboard->queue_mutex);

    return status_copy;
}

bool c64_keyboard_basic_warm_start(c64_keyboard_t *keyboard)
{
    if (!keyboard || !keyboard->rest_client) {
        C64_LOG_ERROR(KEYBOARD_LOG_PREFIX "Invalid keyboard or REST client for warm start");
        return false;
    }

    C64_LOG_DEBUG(KEYBOARD_LOG_PREFIX "Performing BASIC warm start via IRQ vector");

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

    C64_LOG_DEBUG(KEYBOARD_LOG_PREFIX "BASIC warm start completed");
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
                    C64_LOG_DEBUG(KEYBOARD_LOG_PREFIX "User keymap '%s' overrides builtin", name);
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
                    C64_LOG_DEBUG(KEYBOARD_LOG_PREFIX "User keymap '%s' overrides builtin", name);
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

    C64_LOG_DEBUG(KEYBOARD_LOG_PREFIX "Discovered %zu keymaps", *count);
    *paths = ctx.keymap_paths;
    return true;
}
