#include "../../src/c64-interact-key.h"
#include "../../src/c64-file.h"
#include "../../src/c64-keyboard.h"
#include "../../src/c64-rest-client.h"

#include <obs-module.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    (void)client;
    (void)address;
    (void)length;
    (void)buffer;
    (void)buffer_size;
    return -1;
}

bool c64_rest_write_memory(c64_rest_client_t *client, uint16_t address, const uint8_t *data, size_t length)
{
    (void)client;
    (void)address;
    (void)data;
    (void)length;
    return false;
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

    return 0;
}
