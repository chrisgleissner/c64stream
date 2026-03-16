#include "../../src/c64-interact-key.h"
#include "../../src/c64-file.h"
#include "../../src/c64-keyboard.h"
#include "../../src/c64-rest-client.h"

#include <obs-module.h>
#include <util/platform.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t memory[65536];
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
        return -1;
    }

    memcpy(buffer, &rest_client->memory[address], length);
    return (int)length;
}

bool c64_rest_write_memory(c64_rest_client_t *client, uint16_t address, const uint8_t *data, size_t length)
{
    interact_test_rest_client_t *rest_client = (interact_test_rest_client_t *)client;
    if (!rest_client || !data || ((size_t)address + length) > sizeof(rest_client->memory)) {
        return false;
    }

    memcpy(&rest_client->memory[address], data, length);
    return true;
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

static void expect_normalized(const char *input, const char *expected)
{
    char normalized[64] = {0};
    CHECK_VOID(c64_keymap_normalize_identifier(input, normalized));
    CHECK_VOID(strcmp(normalized, expected) == 0);
    CHECK_VOID(c64_keymap_identifier_is_runtime_supported(normalized));
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
    expect_translation(0x33, "\x1C", C64_INTERACT_KEY_TRANSLATED, "Digit3", "");
    expect_translation(0x31, "1", C64_INTERACT_KEY_TRANSLATED, "Digit1", "1");
    expect_translation(0x41, "a", C64_INTERACT_KEY_TRANSLATED, "KeyA", "a");
    expect_translation(0x24, "$", C64_INTERACT_KEY_TRANSLATED, "Digit4", "$");
    expect_translation(0x26, "&", C64_INTERACT_KEY_TRANSLATED, "Digit7", "&");
    expect_translation(0x28, "(", C64_INTERACT_KEY_TRANSLATED, "Digit9", "(");
    expect_translation(0x2D, "-", C64_INTERACT_KEY_TRANSLATED, "Minus", "-");
    expect_translation(0x5B, "[", C64_INTERACT_KEY_TRANSLATED, "BracketLeft", "[");
    expect_translation(0x2E, ".", C64_INTERACT_KEY_TRANSLATED, "Period", ".");
    expect_translation(0x0D, NULL, C64_INTERACT_KEY_TRANSLATED, "Enter", "");
    expect_translation(0x1B, NULL, C64_INTERACT_KEY_WARM_START, NULL, NULL);

    expect_normalized("Ctrl+a", "Ctrl+KeyA");
    expect_normalized("Ctrl+3", "Ctrl+Digit3");
    expect_normalized("Shift+Alt++", "Shift+Alt+Equal");
    expect_normalized("up", "ArrowUp");
    expect_normalized("f1", "F1");

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
    expect_petscii(positional_us, "Home", NULL, 0x01, 0x93);

    expect_petscii(symbolic_uk, NULL, "£", 0, 0x5C);
    expect_petscii(symbolic_de, NULL, "ä", 0, 0x5C);
    expect_petscii(symbolic_de, NULL, "ö", 0, 0x5D);
    expect_petscii(symbolic_de, NULL, "ü", 0, 0x5B);
    expect_petscii(symbolic_de, NULL, "ß", 0, 0x5F);
    expect_petscii(symbolic_fr, NULL, "é", 0, 0x45);
    expect_petscii(symbolic_fr, NULL, "à", 0, 0x41);
    expect_petscii(symbolic_fr, NULL, "ç", 0, 0x43);
    expect_petscii(symbolic_it, NULL, "à", 0, 0x41);
    expect_petscii(symbolic_it, NULL, "ì", 0, 0x49);
    expect_petscii(symbolic_it, NULL, "ù", 0, 0x55);
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
    expect_injected_bytes(symbolic_us, keyboard, &rest_client, 0x0D, NULL, 0, "Enter", "", (const uint8_t[]){0x0D}, 1);

    for (size_t i = 0; i < sizeof(keymap_paths) / sizeof(keymap_paths[0]); i++) {
        c64_keymap_t *keymap = c64_keymap_load(keymap_paths[i]);
        CHECK(keymap != NULL);
        expect_petscii(keymap, "Digit3", "\x1C", 0x02, 0x1C);
        expect_petscii(keymap, "Digit1", NULL, 0x04, 0x81);
        validate_keymap_identifiers(keymap_paths[i]);
        c64_keymap_destroy(keymap);
    }

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
