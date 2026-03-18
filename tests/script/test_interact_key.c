#include "c64-interact-key.h"
#include "c64-file.h"
#include "c64-keyboard.h"
#include "c64-rest-client.h"

#include <obs-module.h>
#include <util/platform.h>

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
static char *test_strtok_r(char *str, const char *delim, char **saveptr)
{
    return strtok_s(str, delim, saveptr);
}
#else
static char *test_strtok_r(char *str, const char *delim, char **saveptr)
{
    return strtok_r(str, delim, saveptr);
}
#endif

typedef struct {
    uint8_t memory[65536];
    char error[128];
} interact_test_rest_client_t;

#define CHECK(expr)                                                                                                       \
    do {                                                                                                                  \
        if (!(expr)) {                                                                                                    \
            fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #expr, __FILE__, __LINE__);                                  \
            return 1;                                                                                                     \
        }                                                                                                                 \
    } while (0)

#define CHECK_VOID(expr)                                                                                                  \
    do {                                                                                                                  \
        if (!(expr)) {                                                                                                    \
            fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #expr, __FILE__, __LINE__);                                  \
            return;                                                                                                       \
        }                                                                                                                 \
    } while (0)

obs_module_t *obs_current_module(void)
{
    return NULL;
}

bool c64_get_user_dir(c64_user_dir_type type, char *path_buffer, size_t buffer_size)
{
    (void)type;
    (void)path_buffer;
    (void)buffer_size;
    return false;
}

int c64_rest_read_memory(c64_rest_client_t *client, uint16_t address, size_t length, uint8_t *buffer,
                         size_t buffer_size)
{
    interact_test_rest_client_t *rest_client = (interact_test_rest_client_t *)client;
    if (!rest_client || !buffer || buffer_size < length || ((size_t)address + length) > sizeof(rest_client->memory)) {
        if (rest_client) {
            snprintf(rest_client->error, sizeof(rest_client->error), "read out of range");
        }
        return -1;
    }

    rest_client->error[0] = '\0';
    memcpy(buffer, &rest_client->memory[address], length);
    return (int)length;
}

bool c64_rest_write_memory(c64_rest_client_t *client, uint16_t address, const uint8_t *data, size_t length)
{
    interact_test_rest_client_t *rest_client = (interact_test_rest_client_t *)client;
    if (!rest_client || !data || ((size_t)address + length) > sizeof(rest_client->memory)) {
        if (rest_client) {
            snprintf(rest_client->error, sizeof(rest_client->error), "write out of range");
        }
        return false;
    }

    rest_client->error[0] = '\0';
    memcpy(&rest_client->memory[address], data, length);
    return true;
}

int c64_rest_read_memory_quiet(c64_rest_client_t *client, uint16_t address, size_t length, uint8_t *buffer,
                               size_t buffer_size)
{
    return c64_rest_read_memory(client, address, length, buffer, buffer_size);
}

bool c64_rest_write_memory_quiet(c64_rest_client_t *client, uint16_t address, const uint8_t *data, size_t length)
{
    return c64_rest_write_memory(client, address, data, length);
}

const char *c64_rest_get_error(c64_rest_client_t *client)
{
    interact_test_rest_client_t *rest_client = (interact_test_rest_client_t *)client;
    if (!rest_client || rest_client->error[0] == '\0') {
        return "";
    }
    return rest_client->error;
}

static const char *keymap_paths[] = {
    C64STREAM_SOURCE_DIR "/data/keymaps/positional_us.c64keymap.ini",
    C64STREAM_SOURCE_DIR "/data/keymaps/symbolic_us.c64keymap.ini",
    C64STREAM_SOURCE_DIR "/data/keymaps/symbolic_uk.c64keymap.ini",
    C64STREAM_SOURCE_DIR "/data/keymaps/symbolic_de.c64keymap.ini",
    C64STREAM_SOURCE_DIR "/data/keymaps/symbolic_fr.c64keymap.ini",
    C64STREAM_SOURCE_DIR "/data/keymaps/symbolic_it.c64keymap.ini",
    C64STREAM_SOURCE_DIR "/data/keymaps/symbolic_nl.c64keymap.ini",
};

static void trim(char *text)
{
    char *start = text;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }

    char *end = start + strlen(start);
    while (end > start && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';

    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }
}

static void expect_translation(uint32_t native_vkey, const char *text, c64_interact_key_result_t expected_result,
                               const char *expected_code, const char *expected_text)
{
    c64_interact_key_t key = {{0}};
    CHECK_VOID(c64_interact_translate_key_event(native_vkey, text, &key) == expected_result);

    if (expected_result == C64_INTERACT_KEY_TRANSLATED) {
        CHECK_VOID(strcmp(key.code, expected_code ? expected_code : "") == 0);
        CHECK_VOID(strcmp(key.text, expected_text ? expected_text : "") == 0);
    }
}

static void expect_petscii(c64_keymap_t *keymap, const char *key_code, const char *key_text, int modifiers,
                           uint8_t expected_petscii)
{
    c64_output_t output = {0};
    CHECK_VOID(c64_keymap_convert(keymap, key_code, key_text, modifiers, &output));
    CHECK_VOID(output.mode == C64_OUTPUT_PETSCII);
    CHECK_VOID(output.data.petscii == expected_petscii);
}

static void reset_keyboard_buffer(interact_test_rest_client_t *rest_client)
{
    CHECK_VOID(rest_client != NULL);
    rest_client->memory[0x00C6] = 0x00;
    for (int index = 0; index < 10; index++) {
        rest_client->memory[0x0277 + index] = 0x00;
    }
}

static void expect_injected_bytes(c64_keymap_t *keymap, c64_keyboard_t *keyboard,
                                  interact_test_rest_client_t *rest_client, uint32_t native_vkey, const char *text,
                                  int modifiers, const char *expected_code, const char *expected_text,
                                  const uint8_t *expected_bytes, size_t expected_count)
{
    CHECK_VOID(keymap != NULL);
    CHECK_VOID(keyboard != NULL);
    CHECK_VOID(rest_client != NULL);
    CHECK_VOID(expected_bytes != NULL);

    c64_interact_key_t key = {{0}};
    CHECK_VOID(c64_interact_translate_key_event(native_vkey, text, &key) == C64_INTERACT_KEY_TRANSLATED);
    CHECK_VOID(strcmp(key.code, expected_code ? expected_code : "") == 0);
    CHECK_VOID(strcmp(key.text, expected_text ? expected_text : "") == 0);

    c64_output_t output = {0};
    CHECK_VOID(c64_keymap_convert(keymap, key.code, key.text, modifiers, &output));
    CHECK_VOID(output.mode == C64_OUTPUT_PETSCII);
    CHECK_VOID(output.data.petscii == expected_bytes[0]);

    reset_keyboard_buffer(rest_client);
    c64_keyboard_queue_output(keyboard, &output);

    uint8_t buffer_length = 0;
    for (int attempt = 0; attempt < 20; attempt++) {
        CHECK_VOID(c64_rest_read_memory((c64_rest_client_t *)rest_client, 0x00C6, 1, &buffer_length,
                                        sizeof(buffer_length)) == 1);
        if (buffer_length == expected_count) {
            break;
        }
        os_sleep_ms(25);
    }

    CHECK_VOID(buffer_length == expected_count);

    uint8_t actual_bytes[10] = {0};
    CHECK_VOID(c64_rest_read_memory((c64_rest_client_t *)rest_client, 0x0277, expected_count, actual_bytes,
                                    sizeof(actual_bytes)) == (int)expected_count);
    CHECK_VOID(memcmp(actual_bytes, expected_bytes, expected_count) == 0);
}

static void expect_stop_flag_cleared(c64_keymap_t *keymap, c64_keyboard_t *keyboard,
                                     interact_test_rest_client_t *rest_client, uint32_t native_vkey, const char *text,
                                     int modifiers, const char *expected_code, const char *expected_text,
                                     const uint8_t *expected_bytes, size_t expected_count)
{
    CHECK_VOID(rest_client != NULL);
    rest_client->memory[0x0091] = 0x80;
    expect_injected_bytes(keymap, keyboard, rest_client, native_vkey, text, modifiers, expected_code, expected_text,
                          expected_bytes, expected_count);
    CHECK_VOID(rest_client->memory[0x0091] == 0x00);
}

static void expect_normalized(const char *input, const char *expected)
{
    char normalized[64] = {0};
    CHECK_VOID(c64_keymap_normalize_identifier(input, normalized));
    CHECK_VOID(strcmp(normalized, expected) == 0);
    CHECK_VOID(c64_keymap_identifier_is_runtime_supported(normalized));
}

static void expect_reboot_chord(uint32_t native_vkey, uint32_t native_scancode, bool key_up, bool shift_down,
                                bool ctrl_down, bool alt_down, bool meta_down, bool escape_down, bool tab_down,
                                bool expected)
{
    CHECK_VOID(c64_interact_should_reboot_chord(native_vkey, native_scancode, key_up, shift_down, ctrl_down, alt_down,
                                                meta_down, escape_down, tab_down) == expected);
}

// Verify a CBM entry (Alt+code) against the corresponding base entry.
// cbm_code is the unmodified base key identifier; expected_petscii is the CBM PETSCII.
static void check_positional_cbm_entry(c64_keymap_t *keymap, const char *cbm_code, uint8_t expected_cbm_petscii)
{
    c64_output_t output = {0};
    // Verify the CBM (Alt) modifier entry exists and produces the expected value.
    CHECK_VOID(c64_keymap_convert(keymap, cbm_code, NULL, 0x04, &output));
    CHECK_VOID(output.mode == C64_OUTPUT_PETSCII);
    CHECK_VOID(output.data.petscii == expected_cbm_petscii);
}

// Collect all reachable PETSCII values from a keymap INI file.
// Returns the number of distinct PETSCII values found.
static int collect_reachable_petscii(const char *path, bool reachable[256])
{
    FILE *file = fopen(path, "r");
    if (!file) {
        return -1;
    }

    memset(reachable, 0, 256 * sizeof(bool));
    char section[32] = "";
    char line[512];

    while (fgets(line, sizeof(line), file)) {
        // Trim
        char *p = line + strlen(line) - 1;
        while (p >= line && (*p == '\n' || *p == '\r' || *p == ' ' || *p == '\t')) {
            *p-- = '\0';
        }

        if (line[0] == '\0') {
            continue;
        }
        if (line[0] == '#' && strchr(line, '=') == NULL) {
            continue;
        }
        // Section headers end with ']'. Entries like '[=0x5B' start with '[' but
        // are NOT section headers (they don't end with ']').
        size_t line_len = strlen(line);
        if (line[0] == '[' && line[line_len - 1] == ']') {
            strncpy(section, line, sizeof(section) - 1);
            section[sizeof(section) - 1] = '\0';
            continue;
        }
        if (strcmp(section, "[map]") != 0) {
            continue;
        }

        char *equals = strchr(line, '=');
        if (!equals) {
            continue;
        }
        char *value = equals + 1;

        // Handle the literal '=' key: stored as '==<value>' in the INI file.
        // After splitting on the first '=', key is empty and value starts with '='.
        if (equals == line && value[0] == '=') {
            value++;
        }

        // Parse comma-separated values
        char val_copy[128];
        strncpy(val_copy, value, sizeof(val_copy) - 1);
        val_copy[sizeof(val_copy) - 1] = '\0';

        char *saveptr = NULL;
        for (char *tok = test_strtok_r(val_copy, ",", &saveptr); tok; tok = test_strtok_r(NULL, ",", &saveptr)) {
            while (*tok == ' ') {
                tok++;
            }
            if (strncmp(tok, "0x", 2) == 0 || strncmp(tok, "0X", 2) == 0) {
                long v = strtol(tok, NULL, 16);
                if (v >= 0 && v <= 255) {
                    reachable[(int)v] = true;
                }
            } else if (isdigit((unsigned char)tok[0])) {
                long v = strtol(tok, NULL, 10);
                if (v >= 0 && v <= 255) {
                    reachable[(int)v] = true;
                }
            }
        }
    }

    fclose(file);

    int count = 0;
    for (int i = 0; i < 256; i++) {
        if (reachable[i]) {
            count++;
        }
    }
    return count;
}

static void check_full_reachability(const char *path)
{
    bool reachable[256];
    int count = collect_reachable_petscii(path, reachable);
    CHECK_VOID(count > 0);

    // Keyboard-accessible printable range 0x20-0x5F.
    // PETSCII 0x60-0x7E are C64 graphics characters accessible only via screen codes,
    // not through normal keyboard input - they are excluded from this check.
    for (int i = 0x20; i <= 0x5F; i++) {
        if (!reachable[i]) {
            fprintf(stderr, "REACHABILITY: %s missing PETSCII 0x%02X (printable range)\n", path, i);
            CHECK_VOID(reachable[i]);
        }
    }

    // Shifted letters 0xC1-0xDA
    for (int i = 0xC1; i <= 0xDA; i++) {
        if (!reachable[i]) {
            fprintf(stderr, "REACHABILITY: %s missing PETSCII 0x%02X (shifted letter)\n", path, i);
            CHECK_VOID(reachable[i]);
        }
    }

    // Shifted C64 punctuation codes (0xA9, 0xBA, 0xDB, 0xDD, 0xDE) are intentionally not
    // checked here. These require C64 Shift+special-key combinations that are only
    // naturally expressible via positional maps. Symbolic maps provide text-based input
    // and cannot reach them without conflicting with standard printable character lookups.

    // CBM graphics ranges
    for (int i = 0xA1; i <= 0xA8; i++) {
        if (!reachable[i]) {
            fprintf(stderr, "REACHABILITY: %s missing PETSCII 0x%02X (CBM graphics)\n", path, i);
            CHECK_VOID(reachable[i]);
        }
    }
    for (int i = 0xAB; i <= 0xAF; i++) {
        if (!reachable[i]) {
            fprintf(stderr, "REACHABILITY: %s missing PETSCII 0x%02X (CBM graphics)\n", path, i);
            CHECK_VOID(reachable[i]);
        }
    }
    for (int i = 0xB1; i <= 0xB9; i++) {
        // 0xB4 is an alternate PETSCII code for CBM+G (primary: 0xA5) - deliberately skipped.
        if (i == 0xB4) {
            continue;
        }
        if (!reachable[i]) {
            fprintf(stderr, "REACHABILITY: %s missing PETSCII 0x%02X (CBM graphics)\n", path, i);
            CHECK_VOID(reachable[i]);
        }
    }
    for (int i = 0xBB; i <= 0xBF; i++) {
        if (!reachable[i]) {
            fprintf(stderr, "REACHABILITY: %s missing PETSCII 0x%02X (CBM graphics)\n", path, i);
            CHECK_VOID(reachable[i]);
        }
    }
    static const uint8_t cbm_singles[] = {0xDC, 0xDF, 0xE2, 0xE5};
    for (size_t i = 0; i < sizeof(cbm_singles); i++) {
        if (!reachable[cbm_singles[i]]) {
            fprintf(stderr, "REACHABILITY: %s missing PETSCII 0x%02X (CBM graphics)\n", path, cbm_singles[i]);
            CHECK_VOID(reachable[cbm_singles[i]]);
        }
    }
}

static void validate_keymap_identifiers(const char *path)
{
    FILE *file = fopen(path, "r");
    CHECK_VOID(file != NULL);

    char section[32] = "";
    char line[512];
    while (fgets(line, sizeof(line), file)) {
        trim(line);
        if (line[0] == '\0') {
            continue;
        }
        if (line[0] == '#' && strchr(line, '=') == NULL) {
            continue;
        }
        if (line[0] == '[') {
            strncpy(section, line, sizeof(section) - 1);
            section[sizeof(section) - 1] = '\0';
            continue;
        }
        if (strcmp(section, "[map]") != 0) {
            continue;
        }

        char *equals = strchr(line, '=');
        CHECK_VOID(equals != NULL);
        *equals = '\0';
        char *key = line;
        char *value = equals + 1;
        trim(key);
        trim(value);
        if (key[0] == '\0' && value[0] == '=') {
            key = "=";
            value++;
            trim(value);
        }

        char normalized[64] = {0};
        CHECK_VOID(c64_keymap_normalize_identifier(key, normalized));
        if (!c64_keymap_identifier_is_runtime_supported(normalized)) {
            fprintf(stderr, "INVALID IDENTIFIER in %s: '%s'\n", path, normalized);
        }
        CHECK_VOID(c64_keymap_identifier_is_runtime_supported(normalized));
    }

    fclose(file);
}

int main(void)
{
    expect_translation(0x26, NULL, C64_INTERACT_KEY_TRANSLATED, "ArrowUp", "");
    expect_translation(0x28, NULL, C64_INTERACT_KEY_TRANSLATED, "ArrowDown", "");
    expect_translation(0x25, NULL, C64_INTERACT_KEY_TRANSLATED, "ArrowLeft", "");
    expect_translation(0x27, NULL, C64_INTERACT_KEY_TRANSLATED, "ArrowRight", "");
    expect_translation(0xFF52, NULL, C64_INTERACT_KEY_TRANSLATED, "ArrowUp", "");
    expect_translation(0xFF8D, NULL, C64_INTERACT_KEY_TRANSLATED, "Enter", "");
    expect_translation(0x33, "\x1C", C64_INTERACT_KEY_TRANSLATED, "Digit3", "");
    expect_translation(0x31, "1", C64_INTERACT_KEY_TRANSLATED, "Digit1", "1");
    expect_translation(0x41, "a", C64_INTERACT_KEY_TRANSLATED, "KeyA", "a");
    expect_translation(0, "\r", C64_INTERACT_KEY_TRANSLATED, "Enter", "");
    expect_translation(0x24, "$", C64_INTERACT_KEY_TRANSLATED, "Digit4", "$");
    expect_translation(0x26, "&", C64_INTERACT_KEY_TRANSLATED, "Digit7", "&");
    expect_translation(0x28, "(", C64_INTERACT_KEY_TRANSLATED, "Digit9", "(");
    expect_translation(0x2D, "-", C64_INTERACT_KEY_TRANSLATED, "Minus", "-");
    expect_translation(0x5B, "[", C64_INTERACT_KEY_TRANSLATED, "BracketLeft", "[");
    expect_translation(0x2E, ".", C64_INTERACT_KEY_TRANSLATED, "Period", ".");
    expect_translation(0x0D, NULL, C64_INTERACT_KEY_TRANSLATED, "Enter", "");
    expect_translation(0, "£", C64_INTERACT_KEY_TRANSLATED, "", "£");
    expect_translation(0, "ä", C64_INTERACT_KEY_TRANSLATED, "", "ä");
    expect_translation(0, "É", C64_INTERACT_KEY_TRANSLATED, "", "É");
    expect_translation(0, "Ù", C64_INTERACT_KEY_TRANSLATED, "", "Ù");
    expect_translation(0x1B, NULL, C64_INTERACT_KEY_WARM_START, NULL, NULL);

    expect_normalized("Ctrl+a", "Ctrl+KeyA");
    expect_normalized("Ctrl+3", "Ctrl+Digit3");
    expect_normalized("Shift+Alt++", "Shift+Alt+Equal");
    expect_normalized("up", "ArrowUp");
    expect_normalized("f1", "F1");

    CHECK(c64_interact_key_is_escape(0x1B, 0x01));
    CHECK(c64_interact_key_is_tab(0x09, 0x0F));
    CHECK(c64_interact_key_is_escape(0xFF1B, 0));
    CHECK(c64_interact_key_is_tab(0xFF09, 0));
    expect_reboot_chord(0x1B, 0x01, false, false, false, false, false, true, true, true);
    expect_reboot_chord(0x09, 0x0F, false, false, false, false, false, true, true, true);
    expect_reboot_chord(0xFF1B, 0, false, false, false, false, false, true, true, true);
    expect_reboot_chord(0xFF09, 0, false, false, false, false, false, true, true, true);
    expect_reboot_chord(0x1B, 0x01, false, true, false, false, false, true, true, true);
    expect_reboot_chord(0x1B, 0x01, false, true, false, false, false, true, false, false);
    expect_reboot_chord(0x1B, 0x01, false, true, false, false, false, false, true, false);
    expect_reboot_chord(0x1B, 0x01, false, true, false, true, false, true, true, false);
    expect_reboot_chord(0x41, 0x1E, false, true, false, false, false, true, true, false);
    expect_reboot_chord(0x1B, 0x01, true, true, false, false, false, true, true, false);

    c64_keymap_t *symbolic_us = c64_keymap_load(C64STREAM_SOURCE_DIR "/data/keymaps/symbolic_us.c64keymap.ini");
    c64_keymap_t *positional_us = c64_keymap_load(C64STREAM_SOURCE_DIR "/data/keymaps/positional_us.c64keymap.ini");
    c64_keymap_t *symbolic_uk = c64_keymap_load(C64STREAM_SOURCE_DIR "/data/keymaps/symbolic_uk.c64keymap.ini");
    c64_keymap_t *symbolic_de = c64_keymap_load(C64STREAM_SOURCE_DIR "/data/keymaps/symbolic_de.c64keymap.ini");
    c64_keymap_t *symbolic_fr = c64_keymap_load(C64STREAM_SOURCE_DIR "/data/keymaps/symbolic_fr.c64keymap.ini");
    c64_keymap_t *symbolic_it = c64_keymap_load(C64STREAM_SOURCE_DIR "/data/keymaps/symbolic_it.c64keymap.ini");
    c64_keymap_t *symbolic_nl = c64_keymap_load(C64STREAM_SOURCE_DIR "/data/keymaps/symbolic_nl.c64keymap.ini");

    CHECK(symbolic_us != NULL);
    CHECK(positional_us != NULL);
    CHECK(symbolic_uk != NULL);
    CHECK(symbolic_de != NULL);
    CHECK(symbolic_fr != NULL);
    CHECK(symbolic_it != NULL);
    CHECK(symbolic_nl != NULL);

    expect_petscii(symbolic_us, "ArrowUp", NULL, 0, 0x91);
    expect_petscii(symbolic_us, "ArrowLeft", NULL, 0, 0x9D);
    expect_petscii(symbolic_us, "KeyA", "a", 0, 0x41);
    expect_petscii(symbolic_us, "KeyA", "A", 0, 0xC1);
    expect_petscii(symbolic_us, "Backquote", "`", 0, 0x5F);
    expect_petscii(symbolic_us, "Digit4", "$", 0x01, 0x24);
    expect_petscii(symbolic_us, "Digit7", "&", 0x01, 0x26);
    expect_petscii(symbolic_us, "Digit9", "(", 0x01, 0x28);
    expect_petscii(symbolic_us, "Minus", "-", 0, 0x2D);
    expect_petscii(symbolic_us, "BracketLeft", "[", 0, 0x5B);
    expect_petscii(symbolic_us, "Period", ".", 0, 0x2E);
    expect_petscii(symbolic_us, "Enter", NULL, 0, 0x0D);
    expect_petscii(symbolic_us, "Digit3", "\x1C", 0x02, 0x1C);
    expect_petscii(symbolic_us, "Digit8", "\x7F", 0x02, 0x9E);
    expect_petscii(symbolic_us, "Digit1", NULL, 0x04, 0x81);
    expect_petscii(symbolic_us, "KeyA", NULL, 0x04, 0xA2);
    expect_petscii(symbolic_us, "Backslash", NULL, 0x04, 0xA8);
    expect_petscii(symbolic_us, "Equal", NULL, 0x05, 0xA6);
    expect_petscii(symbolic_us, "Minus", NULL, 0x04, 0xDC);

    expect_petscii(positional_us, "Enter", NULL, 0, 0x0D);
    expect_petscii(positional_us, "ArrowLeft", NULL, 0, 0x9D);
    expect_petscii(positional_us, "ArrowRight", NULL, 0, 0x1D);
    expect_petscii(positional_us, "KeyA", "a", 0, 0x41);
    expect_petscii(positional_us, "KeyA", "A", 0x01, 0xC1);
    expect_petscii(positional_us, "Digit3", "\x1C", 0x02, 0x1C);
    expect_petscii(positional_us, "Digit1", NULL, 0x04, 0x81);
    expect_petscii(positional_us, "Home", NULL, 0x01, 0x94);
    expect_petscii(positional_us, "Minus", NULL, 0x01, 0xDB);
    expect_petscii(positional_us, "Equal", NULL, 0x01, 0x3D);
    expect_petscii(positional_us, "BracketLeft", NULL, 0x01, 0xBA);
    expect_petscii(positional_us, "Backslash", NULL, 0x01, 0xDE);
    expect_petscii(positional_us, "Backspace", NULL, 0x01, 0xA9);

    expect_petscii(symbolic_uk, NULL, "£", 0, 0x5C);
    expect_petscii(symbolic_de, NULL, "ä", 0, 0x41);
    expect_petscii(symbolic_de, NULL, "ö", 0, 0x4F);
    expect_petscii(symbolic_de, NULL, "ü", 0, 0x55);
    expect_petscii(symbolic_de, NULL, "Ä", 0, 0xC1);
    expect_petscii(symbolic_de, NULL, "Ö", 0, 0xCF);
    expect_petscii(symbolic_de, NULL, "Ü", 0, 0xD5);
    expect_petscii(symbolic_de, NULL, "ß", 0, 0x53);
    expect_petscii(symbolic_fr, NULL, "é", 0, 0x45);
    expect_petscii(symbolic_fr, NULL, "à", 0, 0x41);
    expect_petscii(symbolic_fr, NULL, "ç", 0, 0x43);
    expect_petscii(symbolic_fr, NULL, "É", 0, 0xC5);
    expect_petscii(symbolic_fr, NULL, "À", 0, 0xC1);
    expect_petscii(symbolic_fr, NULL, "Ç", 0, 0xC3);
    expect_petscii(symbolic_it, NULL, "à", 0, 0x41);
    expect_petscii(symbolic_it, NULL, "ì", 0, 0x49);
    expect_petscii(symbolic_it, NULL, "ù", 0, 0x55);
    expect_petscii(symbolic_it, NULL, "À", 0, 0xC1);
    expect_petscii(symbolic_it, NULL, "Ì", 0, 0xC9);
    expect_petscii(symbolic_it, NULL, "Ù", 0, 0xD5);
    expect_petscii(symbolic_nl, "Digit3", "\x1C", 0x02, 0x1C);
    expect_petscii(symbolic_nl, "Digit1", NULL, 0x04, 0x81);

    interact_test_rest_client_t rest_client = {{0}};
    c64_keyboard_t *keyboard = c64_keyboard_create(&rest_client);
    CHECK(keyboard != NULL);

    expect_injected_bytes(symbolic_us, keyboard, &rest_client, 0x24, "$", 0x01, "Digit4", "$", (const uint8_t[]){0x24},
                          1);
    expect_injected_bytes(symbolic_us, keyboard, &rest_client, 0x26, "&", 0x01, "Digit7", "&", (const uint8_t[]){0x26},
                          1);
    expect_injected_bytes(symbolic_us, keyboard, &rest_client, 0x28, "(", 0x01, "Digit9", "(", (const uint8_t[]){0x28},
                          1);
    expect_injected_bytes(symbolic_us, keyboard, &rest_client, 0x2D, "-", 0, "Minus", "-", (const uint8_t[]){0x2D}, 1);
    expect_injected_bytes(symbolic_us, keyboard, &rest_client, 0x5B, "[", 0, "BracketLeft", "[",
                          (const uint8_t[]){0x5B}, 1);
    expect_injected_bytes(symbolic_us, keyboard, &rest_client, 0x2E, ".", 0, "Period", ".", (const uint8_t[]){0x2E}, 1);
    expect_stop_flag_cleared(symbolic_us, keyboard, &rest_client, 0x0D, NULL, 0, "Enter", "", (const uint8_t[]){0x0D},
                             1);

    for (size_t i = 0; i < sizeof(keymap_paths) / sizeof(keymap_paths[0]); i++) {
        c64_keymap_t *keymap = c64_keymap_load(keymap_paths[i]);
        CHECK(keymap != NULL);
        expect_petscii(keymap, "Digit3", "\x1C", 0x02, 0x1C);
        expect_petscii(keymap, "Digit1", NULL, 0x04, 0x81);
        validate_keymap_identifiers(keymap_paths[i]);
        c64_keymap_destroy(keymap);
    }

    // UTF-8 runtime regression tests.
    // Verify multi-byte event->text values reach c64_keymap_convert correctly.
    expect_translation(0, "ä", C64_INTERACT_KEY_TRANSLATED, "", "ä");
    expect_translation(0, "ö", C64_INTERACT_KEY_TRANSLATED, "", "ö");
    expect_translation(0, "ü", C64_INTERACT_KEY_TRANSLATED, "", "ü");
    expect_translation(0, "é", C64_INTERACT_KEY_TRANSLATED, "", "é");
    expect_translation(0, "è", C64_INTERACT_KEY_TRANSLATED, "", "è");
    expect_translation(0, "à", C64_INTERACT_KEY_TRANSLATED, "", "à");
    expect_translation(0, "ù", C64_INTERACT_KEY_TRANSLATED, "", "ù");
    expect_translation(0, "ç", C64_INTERACT_KEY_TRANSLATED, "", "ç");
    expect_translation(0, "ì", C64_INTERACT_KEY_TRANSLATED, "", "ì");
    expect_translation(0, "ò", C64_INTERACT_KEY_TRANSLATED, "", "ò");
    expect_translation(0, "£", C64_INTERACT_KEY_TRANSLATED, "", "£");
    expect_translation(0, "ñ", C64_INTERACT_KEY_TRANSLATED, "", "ñ");
    expect_translation(0, "ë", C64_INTERACT_KEY_TRANSLATED, "", "ë");
    expect_translation(0, "ï", C64_INTERACT_KEY_TRANSLATED, "", "ï");

    // Verify each multi-byte char produces the expected PETSCII via a locale keymap.
    expect_petscii(symbolic_de, NULL, "ä", 0, 0x41);
    expect_petscii(symbolic_de, NULL, "ö", 0, 0x4F);
    expect_petscii(symbolic_de, NULL, "ü", 0, 0x55);
    expect_petscii(symbolic_fr, NULL, "é", 0, 0x45);
    expect_petscii(symbolic_fr, NULL, "è", 0, 0x45);
    expect_petscii(symbolic_fr, NULL, "à", 0, 0x41);
    expect_petscii(symbolic_fr, NULL, "ù", 0, 0x55);
    expect_petscii(symbolic_fr, NULL, "ç", 0, 0x43);
    expect_petscii(symbolic_it, NULL, "ì", 0, 0x49);
    expect_petscii(symbolic_it, NULL, "ò", 0, 0x4F);
    expect_petscii(symbolic_uk, NULL, "£", 0, 0x5C);
    expect_petscii(symbolic_nl, NULL, "ñ", 0, 0x4E);
    expect_petscii(symbolic_fr, NULL, "ë", 0, 0x45);
    expect_petscii(symbolic_fr, NULL, "ï", 0, 0x49);

    // Dead-key normalization tests (FR entries).
    expect_petscii(symbolic_fr, NULL, "â", 0, 0x41);
    expect_petscii(symbolic_fr, NULL, "î", 0, 0x49);
    expect_petscii(symbolic_fr, NULL, "ô", 0, 0x4F);
    expect_petscii(symbolic_fr, NULL, "û", 0, 0x55);
    expect_petscii(symbolic_fr, NULL, "Â", 0, 0xC1);
    expect_petscii(symbolic_fr, NULL, "Î", 0, 0xC9);
    expect_petscii(symbolic_fr, NULL, "Ô", 0, 0xCF);
    expect_petscii(symbolic_fr, NULL, "Û", 0, 0xD5);
    expect_petscii(symbolic_fr, NULL, "ä", 0, 0x41);
    expect_petscii(symbolic_fr, NULL, "ö", 0, 0x4F);
    expect_petscii(symbolic_fr, NULL, "ü", 0, 0x55);
    expect_petscii(symbolic_fr, NULL, "ÿ", 0, 0x59);
    expect_petscii(symbolic_fr, NULL, "Ä", 0, 0xC1);
    expect_petscii(symbolic_fr, NULL, "Ë", 0, 0xC5);
    expect_petscii(symbolic_fr, NULL, "Ï", 0, 0xC9);
    expect_petscii(symbolic_fr, NULL, "Ö", 0, 0xCF);
    expect_petscii(symbolic_fr, NULL, "Ü", 0, 0xD5);
    expect_petscii(symbolic_fr, NULL, "Ÿ", 0, 0xD9);
    expect_petscii(symbolic_fr, NULL, "á", 0, 0x41);
    expect_petscii(symbolic_fr, NULL, "í", 0, 0x49);
    expect_petscii(symbolic_fr, NULL, "ó", 0, 0x4F);
    expect_petscii(symbolic_fr, NULL, "ú", 0, 0x55);
    expect_petscii(symbolic_fr, NULL, "ì", 0, 0x49);
    expect_petscii(symbolic_fr, NULL, "ò", 0, 0x4F);

    // Dead-key normalization tests (IT entries).
    expect_petscii(symbolic_it, NULL, "ç", 0, 0x43);
    expect_petscii(symbolic_it, NULL, "Ç", 0, 0xC3);
    expect_petscii(symbolic_it, NULL, "â", 0, 0x41);
    expect_petscii(symbolic_it, NULL, "î", 0, 0x49);
    expect_petscii(symbolic_it, NULL, "ô", 0, 0x4F);
    expect_petscii(symbolic_it, NULL, "û", 0, 0x55);
    expect_petscii(symbolic_it, NULL, "ä", 0, 0x41);
    expect_petscii(symbolic_it, NULL, "ë", 0, 0x45);
    expect_petscii(symbolic_it, NULL, "ï", 0, 0x49);
    expect_petscii(symbolic_it, NULL, "ö", 0, 0x4F);
    expect_petscii(symbolic_it, NULL, "ü", 0, 0x55);
    expect_petscii(symbolic_it, NULL, "á", 0, 0x41);
    expect_petscii(symbolic_it, NULL, "í", 0, 0x49);
    expect_petscii(symbolic_it, NULL, "ó", 0, 0x4F);
    expect_petscii(symbolic_it, NULL, "ú", 0, 0x55);

    // Dead-key normalization tests (NL entries).
    expect_petscii(symbolic_nl, NULL, "ä", 0, 0x41);
    expect_petscii(symbolic_nl, NULL, "ë", 0, 0x45);
    expect_petscii(symbolic_nl, NULL, "ï", 0, 0x49);
    expect_petscii(symbolic_nl, NULL, "ö", 0, 0x4F);
    expect_petscii(symbolic_nl, NULL, "ü", 0, 0x55);
    expect_petscii(symbolic_nl, NULL, "â", 0, 0x41);
    expect_petscii(symbolic_nl, NULL, "ê", 0, 0x45);
    expect_petscii(symbolic_nl, NULL, "î", 0, 0x49);
    expect_petscii(symbolic_nl, NULL, "ô", 0, 0x4F);
    expect_petscii(symbolic_nl, NULL, "û", 0, 0x55);
    expect_petscii(symbolic_nl, NULL, "é", 0, 0x45);
    expect_petscii(symbolic_nl, NULL, "á", 0, 0x41);
    expect_petscii(symbolic_nl, NULL, "à", 0, 0x41);
    expect_petscii(symbolic_nl, NULL, "è", 0, 0x45);
    expect_petscii(symbolic_nl, NULL, "ù", 0, 0x55);
    expect_petscii(symbolic_nl, NULL, "ã", 0, 0x41);
    expect_petscii(symbolic_nl, NULL, "ñ", 0, 0x4E);
    expect_petscii(symbolic_nl, NULL, "ç", 0, 0x43);
    expect_petscii(symbolic_nl, NULL, "ÿ", 0, 0x59);

    // Positional CBM consistency check.
    // Verify corrected entries: Alt+<code> produces the CBM PETSCII for the C64 key
    // at that physical position.
    // BracketLeft=0x40 (C64 @) → CBM+@ = 0xA4
    check_positional_cbm_entry(positional_us, "BracketLeft", 0xA4);
    // Minus=0x2B (C64 +) → CBM++ = 0xA6 (via Shift+Alt+Minus)
    expect_petscii(positional_us, "Minus", NULL, 0x05, 0xA6);
    // Backspace=0x5C (C64 £) → CBM+£ = 0xA8
    check_positional_cbm_entry(positional_us, "Backspace", 0xA8);
    // Equal=0x2D (C64 -) → CBM+- = 0xDC
    check_positional_cbm_entry(positional_us, "Equal", 0xDC);
    // BracketRight=0x2A (C64 *) → CBM+* = 0xDF
    check_positional_cbm_entry(positional_us, "BracketRight", 0xDF);
    // CBM+H and CBM+I
    check_positional_cbm_entry(positional_us, "KeyH", 0xE5);
    check_positional_cbm_entry(positional_us, "KeyI", 0xE2);

    // Full reachability test: every required PETSCII code must be present
    // in at least one entry of each keymap file.
    check_full_reachability(C64STREAM_SOURCE_DIR "/data/keymaps/positional_us.c64keymap.ini");
    check_full_reachability(C64STREAM_SOURCE_DIR "/data/keymaps/symbolic_uk.c64keymap.ini");
    check_full_reachability(C64STREAM_SOURCE_DIR "/data/keymaps/symbolic_de.c64keymap.ini");
    check_full_reachability(C64STREAM_SOURCE_DIR "/data/keymaps/symbolic_fr.c64keymap.ini");
    check_full_reachability(C64STREAM_SOURCE_DIR "/data/keymaps/symbolic_it.c64keymap.ini");
    check_full_reachability(C64STREAM_SOURCE_DIR "/data/keymaps/symbolic_nl.c64keymap.ini");

    c64_keymap_destroy(symbolic_us);
    c64_keymap_destroy(positional_us);
    c64_keymap_destroy(symbolic_uk);
    c64_keymap_destroy(symbolic_de);
    c64_keymap_destroy(symbolic_fr);
    c64_keymap_destroy(symbolic_it);
    c64_keymap_destroy(symbolic_nl);
    c64_keyboard_destroy(keyboard);

    return 0;
}
