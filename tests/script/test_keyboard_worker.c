#include "../../src/c64-file.h"
#include "../../src/c64-keyboard.h"
#include "../../src/c64-rest-client.h"

#include <obs-module.h>
#include <util/platform.h>

#include <stdbool.h>
#include <stdint.h>
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

#define C64_KEYBOARD_BUFFER 0x0277
#define C64_KEYBOARD_LENGTH 0x00C6
#define C64_STOP_FLAG 0x0091

typedef struct {
    uint8_t memory[65536];
    char error[128];
    int read_length_calls;
    int write_buffer_calls;
    int write_length_calls;
    int write_stop_calls;
    bool stall_buffer;
    int consume_after_reads;
    int consume_reads_remaining;
    uint8_t consumed[2048];
    size_t consumed_count;
} worker_test_rest_client_t;

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

static void worker_test_consume_buffer(worker_test_rest_client_t *client)
{
    if (!client) {
        return;
    }

    uint8_t length = client->memory[C64_KEYBOARD_LENGTH];
    if (length == 0) {
        return;
    }

    if (client->consumed_count + length > sizeof(client->consumed)) {
        length = (uint8_t)(sizeof(client->consumed) - client->consumed_count);
    }
    memcpy(&client->consumed[client->consumed_count], &client->memory[C64_KEYBOARD_BUFFER], length);
    client->consumed_count += length;
    memset(&client->memory[C64_KEYBOARD_BUFFER], 0, length);
    client->memory[C64_KEYBOARD_LENGTH] = 0;
}

int c64_rest_read_memory(c64_rest_client_t *client, uint16_t address, size_t length, uint8_t *buffer,
                         size_t buffer_size)
{
    worker_test_rest_client_t *rest_client = (worker_test_rest_client_t *)client;
    if (!rest_client || !buffer || buffer_size < length || ((size_t)address + length) > sizeof(rest_client->memory)) {
        return -1;
    }

    if (address == C64_KEYBOARD_LENGTH && length == 1) {
        rest_client->read_length_calls++;
        if (!rest_client->stall_buffer && rest_client->memory[C64_KEYBOARD_LENGTH] > 0) {
            if (rest_client->consume_reads_remaining <= 0) {
                worker_test_consume_buffer(rest_client);
            } else {
                rest_client->consume_reads_remaining--;
            }
        }
    }

    memcpy(buffer, &rest_client->memory[address], length);
    return (int)length;
}

bool c64_rest_write_memory(c64_rest_client_t *client, uint16_t address, const uint8_t *data, size_t length)
{
    worker_test_rest_client_t *rest_client = (worker_test_rest_client_t *)client;
    if (!rest_client || !data || ((size_t)address + length) > sizeof(rest_client->memory)) {
        return false;
    }

    memcpy(&rest_client->memory[address], data, length);

    if (address == C64_STOP_FLAG && length == 1) {
        rest_client->write_stop_calls++;
    } else if (address == C64_KEYBOARD_BUFFER) {
        rest_client->write_buffer_calls++;
    } else if (address == C64_KEYBOARD_LENGTH && length == 1) {
        rest_client->write_length_calls++;
        rest_client->consume_reads_remaining = rest_client->consume_after_reads;
    }

    return true;
}

const char *c64_rest_get_error(c64_rest_client_t *client)
{
    worker_test_rest_client_t *rest_client = (worker_test_rest_client_t *)client;
    if (!rest_client) {
        return "";
    }
    return rest_client->error;
}

static bool wait_for_consumed_count(worker_test_rest_client_t *client, size_t expected_count, uint32_t timeout_ms)
{
    const uint64_t deadline = os_gettime_ns() + ((uint64_t)timeout_ms * 1000000ULL);
    while (os_gettime_ns() < deadline) {
        if (client->consumed_count == expected_count) {
            return true;
        }
        os_sleep_ms(10);
    }
    return client->consumed_count == expected_count;
}

static bool wait_for_status(c64_keyboard_t *keyboard, const char *expected_status, uint32_t timeout_ms)
{
    const uint64_t deadline = os_gettime_ns() + ((uint64_t)timeout_ms * 1000000ULL);
    while (os_gettime_ns() < deadline) {
        if (strcmp(c64_keyboard_get_status(keyboard), expected_status) == 0) {
            return true;
        }
        os_sleep_ms(10);
    }
    return strcmp(c64_keyboard_get_status(keyboard), expected_status) == 0;
}

int main(void)
{
    worker_test_rest_client_t stress_client = {0};
    stress_client.consume_after_reads = 0;

    c64_keyboard_t *stress_keyboard = c64_keyboard_create(&stress_client);
    CHECK(stress_keyboard != NULL);

    for (int i = 0; i < 1000; i++) {
        c64_output_t output = {0};
        output.mode = C64_OUTPUT_PETSCII;
        output.data.petscii = (uint8_t)(i & 0xFF);
        c64_keyboard_queue_output(stress_keyboard, &output);
    }

    CHECK(wait_for_consumed_count(&stress_client, 1000, 5000));
    CHECK(wait_for_status(stress_keyboard, "idle", 1000));
    CHECK(stress_client.consumed_count == 1000);
    for (int i = 0; i < 1000; i++) {
        CHECK(stress_client.consumed[i] == (uint8_t)(i & 0xFF));
    }
    CHECK(stress_client.write_buffer_calls > 0);
    CHECK(stress_client.write_length_calls > 0);
    CHECK(stress_client.write_stop_calls == stress_client.write_length_calls);
    c64_keyboard_destroy(stress_keyboard);

    worker_test_rest_client_t stall_client = {0};
    stall_client.stall_buffer = true;
    stall_client.memory[C64_KEYBOARD_LENGTH] = 1;

    c64_keyboard_t *stall_keyboard = c64_keyboard_create(&stall_client);
    CHECK(stall_keyboard != NULL);

    c64_output_t stalled_output = {0};
    stalled_output.mode = C64_OUTPUT_PETSCII;
    stalled_output.data.petscii = 0x41;
    c64_keyboard_queue_output(stall_keyboard, &stalled_output);

    CHECK(wait_for_status(stall_keyboard, "timeout", 12000));
    CHECK(stall_client.read_length_calls == 20);
    CHECK(stall_client.write_buffer_calls == 0);
    CHECK(stall_client.write_length_calls == 0);
    CHECK(stall_client.consumed_count == 0);
    c64_keyboard_destroy(stall_keyboard);

    return 0;
}
