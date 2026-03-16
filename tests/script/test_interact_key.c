#include "../../src/c64-interact-key.h"
#include "../../src/c64-file.h"
#include "../../src/c64-keyboard.h"
#include "../../src/c64-rest-client.h"

#include <obs-module.h>

#include <stdio.h>
#include <string.h>

#define CHECK(expr)                                                                                                       \
    do {                                                                                                                  \
        if (!(expr)) {                                                                                                    \
            fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #expr, __FILE__, __LINE__);                                  \
            return 1;                                                                                                     \
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

int main(void)
{
    char key_code[64] = {0};

    CHECK(c64_interact_translate_key_code(0x26, NULL, key_code) == C64_INTERACT_KEY_TRANSLATED);
    CHECK(strcmp(key_code, "ArrowUp") == 0);

    CHECK(c64_interact_translate_key_code(0x28, NULL, key_code) == C64_INTERACT_KEY_TRANSLATED);
    CHECK(strcmp(key_code, "ArrowDown") == 0);

    CHECK(c64_interact_translate_key_code(0x25, NULL, key_code) == C64_INTERACT_KEY_TRANSLATED);
    CHECK(strcmp(key_code, "ArrowLeft") == 0);

    CHECK(c64_interact_translate_key_code(0x27, NULL, key_code) == C64_INTERACT_KEY_TRANSLATED);
    CHECK(strcmp(key_code, "ArrowRight") == 0);

    CHECK(c64_interact_translate_key_code(0xFF52, NULL, key_code) == C64_INTERACT_KEY_TRANSLATED);
    CHECK(strcmp(key_code, "ArrowUp") == 0);

    CHECK(c64_interact_translate_key_code(0x1B, NULL, key_code) == C64_INTERACT_KEY_WARM_START);

    c64_keymap_t *keymap = c64_keymap_load(C64STREAM_SOURCE_DIR "/data/keymaps/symbolic_us.c64keymap.ini");
    CHECK(keymap != NULL);

    c64_output_t output = {0};
    CHECK(c64_keymap_convert(keymap, "ArrowUp", 0, &output));
    CHECK(output.mode == C64_OUTPUT_PETSCII);
    CHECK(output.data.petscii == 0x91);

    CHECK(c64_keymap_convert(keymap, "ArrowLeft", 0, &output));
    CHECK(output.mode == C64_OUTPUT_PETSCII);
    CHECK(output.data.petscii == 0x9D);

    c64_keymap_destroy(keymap);
    return 0;
}
