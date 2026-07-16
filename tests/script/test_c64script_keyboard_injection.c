#include "c64-file.h"
#include "c64-keyboard.h"
#include "c64-rest-client.h"
#include "c64-script-runtime.h"
#include "c64-script-vm-dispatch-keyboard.h"

#include <obs-module.h>
#include <util/platform.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expr)                                                                                                       \
    do {                                                                                                                  \
        if (!(expr)) {                                                                                                    \
            fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #expr, __FILE__, __LINE__);                                \
            return 1;                                                                                                     \
        }                                                                                                                 \
    } while (0)

#define C64_KEYBOARD_BUFFER 0x0277
#define C64_KEYBOARD_LENGTH 0x00C6
#define C64_STOP_FLAG 0x0091

typedef struct {
    uint8_t memory[65536];
    uint8_t consumed[256];
    size_t consumed_count;
    int machine_input_calls;
} script_keyboard_rest_t;

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

static void consume_keybuffer(script_keyboard_rest_t *client)
{
    const uint8_t length = client->memory[C64_KEYBOARD_LENGTH];
    if (length == 0) {
        return;
    }
    memcpy(client->consumed + client->consumed_count, client->memory + C64_KEYBOARD_BUFFER, length);
    client->consumed_count += length;
    memset(client->memory + C64_KEYBOARD_BUFFER, 0, length);
    client->memory[C64_KEYBOARD_LENGTH] = 0;
}

int c64_rest_read_memory(c64_rest_client_t *rest, uint16_t address, size_t length, uint8_t *buffer, size_t buffer_size)
{
    script_keyboard_rest_t *client = (script_keyboard_rest_t *)rest;
    if (!client || !buffer || buffer_size < length || (size_t)address + length > sizeof(client->memory)) {
        return -1;
    }
    if (address == C64_KEYBOARD_LENGTH && length == 1) {
        consume_keybuffer(client);
    }
    memcpy(buffer, client->memory + address, length);
    return (int)length;
}

bool c64_rest_write_memory(c64_rest_client_t *rest, uint16_t address, const uint8_t *data, size_t length)
{
    script_keyboard_rest_t *client = (script_keyboard_rest_t *)rest;
    if (!client || !data || (size_t)address + length > sizeof(client->memory)) {
        return false;
    }
    memcpy(client->memory + address, data, length);
    return true;
}

bool c64_rest_machine_input(c64_rest_client_t *rest, const char *json)
{
    script_keyboard_rest_t *client = (script_keyboard_rest_t *)rest;
    (void)json;
    if (client) {
        client->machine_input_calls++;
    }
    return false; /* Mock 501: Auto mode must retry the complete batch via KERNAL. */
}

bool c64_rest_machine_input_with_outcome(c64_rest_client_t *rest, const char *json, c64_rest_outcome_t *outcome,
                                         long *status)
{
    const bool ok = c64_rest_machine_input(rest, json);
    if (outcome) {
        *outcome = C64_REST_NOT_SUPPORTED;
    }
    if (status) {
        *status = 501;
    }
    return ok;
}

bool c64_rest_release_all(c64_rest_client_t *rest)
{
    return rest != NULL;
}

/* C64STR-022 machine-control stubs (inert here). */
bool c64_rest_joystick_input(c64_rest_client_t *rest, int port, const char *input_name, const char *transition)
{
    (void)rest;
    (void)port;
    (void)input_name;
    (void)transition;
    return true;
}
bool c64_rest_menu_button(c64_rest_client_t *rest)
{
    (void)rest;
    return true;
}
bool c64_rest_reset(c64_rest_client_t *rest)
{
    (void)rest;
    return true;
}
bool c64_rest_reboot(c64_rest_client_t *rest)
{
    (void)rest;
    return true;
}

long c64_rest_get_last_status(const c64_rest_client_t *rest)
{
    (void)rest;
    return 501;
}

c64_rest_outcome_t c64_rest_get_last_outcome(const c64_rest_client_t *rest)
{
    (void)rest;
    return C64_REST_NOT_SUPPORTED;
}

const char *c64_rest_get_error(c64_rest_client_t *rest)
{
    (void)rest;
    return "mock 501";
}

static bool wait_for_bytes(script_keyboard_rest_t *client, size_t expected)
{
    const uint64_t deadline = os_gettime_ns() + 1000000000ULL;
    while (os_gettime_ns() < deadline) {
        if (client->consumed_count == expected) {
            return true;
        }
        os_sleep_ms(10);
    }
    return client->consumed_count == expected;
}

int main(void)
{
    script_keyboard_rest_t rest = {0};
    c64_keyboard_t *keyboard = c64_keyboard_create(&rest);
    CHECK(keyboard != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    CHECK(runtime != NULL);
    runtime->keyboard = keyboard;

    c64script_instruction_t type = {.opcode = OP_TYPE};
    CHECK(c64script_runtime_push(runtime, c64script_value_string("HI")));
    CHECK(c64script_dispatch_keyboard(runtime, &type));
    CHECK(wait_for_bytes(&rest, 2));
    CHECK(rest.machine_input_calls == 1);
    CHECK(memcmp(rest.consumed, "HI", 2) == 0);

    c64script_instruction_t key = {.opcode = OP_KEY};
    CHECK(c64script_runtime_push(runtime, c64script_value_string("not_a_key")));
    CHECK(!c64script_dispatch_keyboard(runtime, &key));
    CHECK(strstr(runtime->error_msg, "rejected") != NULL);

    c64script_runtime_destroy(runtime);
    c64_keyboard_destroy(keyboard);
    return 0;
}
