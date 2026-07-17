#include "c64-file.h"
#include "c64-keyboard.h"
#include "c64-rest-client.h"
#include "c64-stream-control.h"

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
    int machine_input_calls;
    int machine_input_events;
    int release_all_calls;
    bool machine_input_success;
    char last_machine_input[8192];
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

bool c64_rest_machine_input(c64_rest_client_t *client, const char *json)
{
    worker_test_rest_client_t *rest_client = (worker_test_rest_client_t *)client;
    if (rest_client) {
        rest_client->machine_input_calls++;
        snprintf(rest_client->last_machine_input, sizeof(rest_client->last_machine_input), "%s", json);
        for (const char *event = json; (event = strstr(event, "\"kind\"")) != NULL; event += sizeof("\"kind\"") - 1) {
            rest_client->machine_input_events++;
        }
        return rest_client->machine_input_success;
    }
    return false;
}

bool c64_rest_machine_input_with_outcome(c64_rest_client_t *client, const char *json, c64_rest_outcome_t *outcome,
                                         long *status)
{
    const bool ok = c64_rest_machine_input(client, json);
    if (outcome) {
        *outcome = C64_REST_NOT_SUPPORTED;
    }
    if (status) {
        *status = 501;
    }
    return ok;
}

bool c64_rest_release_all(c64_rest_client_t *client)
{
    worker_test_rest_client_t *rest_client = (worker_test_rest_client_t *)client;
    if (rest_client) {
        rest_client->release_all_calls++;
    }
    return true;
}

/* Machine-control stubs (C64STR-022 async command queue). This test drives the
 * keystroke path only, so these are inert. */
bool c64_rest_joystick_input(c64_rest_client_t *client, int port, const char *input_name, const char *transition)
{
    (void)client;
    (void)port;
    (void)input_name;
    (void)transition;
    return true;
}
bool c64_rest_menu_button(c64_rest_client_t *client)
{
    (void)client;
    return true;
}
bool c64_rest_reset(c64_rest_client_t *client)
{
    (void)client;
    return true;
}
bool c64_rest_reboot(c64_rest_client_t *client)
{
    (void)client;
    return true;
}

long c64_rest_get_last_status(const c64_rest_client_t *client)
{
    (void)client;
    return 501;
}

c64_rest_outcome_t c64_rest_get_last_outcome(const c64_rest_client_t *client)
{
    (void)client;
    return C64_REST_NOT_SUPPORTED;
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

static bool wait_for_matrix_events(worker_test_rest_client_t *client, int expected_count, uint32_t timeout_ms)
{
    const uint64_t deadline = os_gettime_ns() + ((uint64_t)timeout_ms * 1000000ULL);
    while (os_gettime_ns() < deadline) {
        if (client->machine_input_events >= expected_count) {
            return true;
        }
        os_sleep_ms(10);
    }
    return client->machine_input_events >= expected_count;
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

    worker_test_rest_client_t legacy_client = {0};
    c64_keyboard_t *legacy_keyboard = c64_keyboard_create(&legacy_client);
    CHECK(legacy_keyboard != NULL);
    c64_keyboard_set_transport(legacy_keyboard, C64_STREAM_TRANSPORT_LEGACY);
    c64_output_t legacy_output = {.mode = C64_OUTPUT_PETSCII, .data.petscii = 'A'};
    c64_keyboard_queue_output(legacy_keyboard, &legacy_output);
    CHECK(wait_for_consumed_count(&legacy_client, 1, 1000));
    CHECK(legacy_client.machine_input_calls == 0);
    CHECK(c64_keyboard_release_all(legacy_keyboard));
    CHECK(legacy_client.release_all_calls == 1);
    c64_keyboard_destroy(legacy_keyboard);

    worker_test_rest_client_t force_rest_client = {0};
    c64_keyboard_t *force_rest_keyboard = c64_keyboard_create(&force_rest_client);
    CHECK(force_rest_keyboard != NULL);
    c64_keyboard_set_transport(force_rest_keyboard, C64_STREAM_TRANSPORT_REST);
    c64_output_t force_rest_output = {.mode = C64_OUTPUT_PETSCII, .data.petscii = 'A'};
    c64_keyboard_queue_output(force_rest_keyboard, &force_rest_output);
    CHECK(wait_for_status(force_rest_keyboard, "failed", 1000));
    CHECK(force_rest_client.machine_input_calls == 1);
    CHECK(force_rest_client.write_buffer_calls == 0);
    c64_keyboard_destroy(force_rest_keyboard);

    // '(' is shift+8 on a real C64 keyboard (digit-row shift symbols:
    // shift+1..shift+9 = !"#$%&'()). Regression for a shift-list bug where
    // '(' was missing from the shift set entirely.
    worker_test_rest_client_t punctuation_client = {.machine_input_success = true};
    c64_keyboard_t *punctuation_keyboard = c64_keyboard_create(&punctuation_client);
    CHECK(punctuation_keyboard != NULL);
    c64_keyboard_set_transport(punctuation_keyboard, C64_STREAM_TRANSPORT_REST);
    c64_output_t punctuation_output = {.mode = C64_OUTPUT_PETSCII, .data.petscii = '('};
    c64_keyboard_queue_output(punctuation_keyboard, &punctuation_output);
    CHECK(wait_for_matrix_events(&punctuation_client, 1, 1000));
    CHECK(strstr(punctuation_client.last_machine_input, "\"inputs\":[\"left_shift\",\"8\"]") != NULL);
    c64_keyboard_destroy(punctuation_keyboard);

    // '*' is unshifted (its own dedicated matrix key); regression for a bug
    // where it was previously treated as shift+8 and would have typed '('.
    worker_test_rest_client_t star_client = {.machine_input_success = true};
    c64_keyboard_t *star_keyboard = c64_keyboard_create(&star_client);
    CHECK(star_keyboard != NULL);
    c64_keyboard_set_transport(star_keyboard, C64_STREAM_TRANSPORT_REST);
    c64_output_t star_output = {.mode = C64_OUTPUT_PETSCII, .data.petscii = '*'};
    c64_keyboard_queue_output(star_keyboard, &star_output);
    CHECK(wait_for_matrix_events(&star_client, 1, 1000));
    CHECK(strstr(star_client.last_machine_input, "\"inputs\":[\"star\"]") != NULL);
    c64_keyboard_destroy(star_keyboard);

    // '`', '|'/'~' and '{'/'}' have no C64 matrix chord. The first 64 queued
    // bytes must still use REST; lowercase text is covered explicitly below.
    worker_test_rest_client_t printable_client = {.machine_input_success = true};
    c64_keyboard_t *printable_keyboard = c64_keyboard_create(&printable_client);
    CHECK(printable_keyboard != NULL);
    c64_keyboard_set_transport(printable_keyboard, C64_STREAM_TRANSPORT_REST);
    for (uint8_t ch = 32; ch <= 126; ch++) {
        if (ch == '`' || ch == '|' || ch == '~' || ch == '{' || ch == '}') {
            continue;
        }
        c64_output_t printable_output = {.mode = C64_OUTPUT_PETSCII, .data.petscii = ch};
        c64_keyboard_queue_output(printable_keyboard, &printable_output);
    }
    CHECK(wait_for_matrix_events(&printable_client, 64, 1000));
    c64_keyboard_destroy(printable_keyboard);

    // '`', '{', '}' and the remaining ASCII characters with no C64 matrix
    // chord must fall back to the legacy Kernal keyboard buffer.
    static const uint8_t unmapped[] = {'`', '{', '}', '|', '~'};
    for (size_t i = 0; i < sizeof(unmapped) / sizeof(unmapped[0]); i++) {
        worker_test_rest_client_t fallback_client = {.machine_input_success = true};
        c64_keyboard_t *fallback_keyboard = c64_keyboard_create(&fallback_client);
        CHECK(fallback_keyboard != NULL);
        c64_keyboard_set_transport(fallback_keyboard, C64_STREAM_TRANSPORT_AUTO);
        c64_output_t fallback_output = {.mode = C64_OUTPUT_PETSCII, .data.petscii = unmapped[i]};
        c64_keyboard_queue_output(fallback_keyboard, &fallback_output);
        CHECK(wait_for_consumed_count(&fallback_client, 1, 1000));
        CHECK(fallback_client.machine_input_calls == 0);
        CHECK(fallback_client.write_buffer_calls == 1);
        c64_keyboard_destroy(fallback_keyboard);
    }

    // Regression: PETSCII uppercase letters (0x41-0x5A) must translate to the
    // unshifted matrix key, not "left_shift"+"<letter>". A previous version of
    // petscii_to_matrix set shift=true for this range, which made any unshifted
    // PC keystroke arrive at the C64 as SHIFT+letter and rendered the wrong
    // character on the default uppercase charset.
    worker_test_rest_client_t uppercase_client = {.machine_input_success = true};
    c64_keyboard_t *uppercase_keyboard = c64_keyboard_create(&uppercase_client);
    CHECK(uppercase_keyboard != NULL);
    c64_keyboard_set_transport(uppercase_keyboard, C64_STREAM_TRANSPORT_REST);
    c64_output_t uppercase_output = {.mode = C64_OUTPUT_PETSCII, .data.petscii = 'A'};
    c64_keyboard_queue_output(uppercase_keyboard, &uppercase_output);
    CHECK(wait_for_matrix_events(&uppercase_client, 1, 1000));
    CHECK(strstr(uppercase_client.last_machine_input, "\"inputs\":[\"a\"]") != NULL);
    CHECK(strstr(uppercase_client.last_machine_input, "left_shift") == NULL);
    c64_keyboard_destroy(uppercase_keyboard);

    // Regression: PETSCII shifted letters (0xC1-0xDA) must translate to
    // "left_shift"+"<letter>" so SHIFT+letter on the host keyboard reaches
    // the C64 as a single hardware chord.
    worker_test_rest_client_t shifted_letter_client = {.machine_input_success = true};
    c64_keyboard_t *shifted_letter_keyboard = c64_keyboard_create(&shifted_letter_client);
    CHECK(shifted_letter_keyboard != NULL);
    c64_keyboard_set_transport(shifted_letter_keyboard, C64_STREAM_TRANSPORT_REST);
    c64_output_t shifted_letter_output = {.mode = C64_OUTPUT_PETSCII, .data.petscii = 0xC1};
    c64_keyboard_queue_output(shifted_letter_keyboard, &shifted_letter_output);
    CHECK(wait_for_matrix_events(&shifted_letter_client, 1, 1000));
    CHECK(strstr(shifted_letter_client.last_machine_input, "\"inputs\":[\"left_shift\",\"a\"]") != NULL);
    c64_keyboard_destroy(shifted_letter_keyboard);

    // C64Script TYPE produces lowercase ASCII. It must remain REST-injectable
    // as the unshifted physical key, rather than forcing the whole text batch
    // through the legacy buffer or making Force REST fail.
    worker_test_rest_client_t text_client = {.machine_input_success = true};
    c64_keyboard_t *text_keyboard = c64_keyboard_create(&text_client);
    CHECK(text_keyboard != NULL);
    c64_keyboard_set_transport(text_keyboard, C64_STREAM_TRANSPORT_REST);
    c64_output_t text_output = {.mode = C64_OUTPUT_TEXT, .data.text = "hello"};
    c64_keyboard_queue_output(text_keyboard, &text_output);
    CHECK(wait_for_matrix_events(&text_client, 5, 1000));
    CHECK(strstr(text_client.last_machine_input, "\"inputs\":[\"h\"]") != NULL);
    CHECK(strstr(text_client.last_machine_input, "\"inputs\":[\"o\"]") != NULL);
    c64_keyboard_destroy(text_keyboard);

    // Matrix-key names must exactly match the C64 Ultimate API enum. These
    // values exercise dedicated punctuation and their shifted variants.
    static const struct {
        uint8_t petscii;
        const char *inputs;
    } matrix_keys[] = {{'+', "\"inputs\":[\"plus\"]"},
                       {'=', "\"inputs\":[\"equals\"]"},
                       {'[', "\"inputs\":[\"left_shift\",\"plus\"]"},
                       {']', "\"inputs\":[\"left_shift\",\"minus\"]"},
                       {'^', "\"inputs\":[\"arrow_up\"]"},
                       {'_', "\"inputs\":[\"arrow_left\"]"}};
    for (size_t i = 0; i < sizeof(matrix_keys) / sizeof(matrix_keys[0]); i++) {
        worker_test_rest_client_t matrix_client = {.machine_input_success = true};
        c64_keyboard_t *matrix_keyboard = c64_keyboard_create(&matrix_client);
        CHECK(matrix_keyboard != NULL);
        c64_keyboard_set_transport(matrix_keyboard, C64_STREAM_TRANSPORT_REST);
        c64_output_t matrix_output = {.mode = C64_OUTPUT_PETSCII, .data.petscii = matrix_keys[i].petscii};
        c64_keyboard_queue_output(matrix_keyboard, &matrix_output);
        CHECK(wait_for_matrix_events(&matrix_client, 1, 1000));
        CHECK(strstr(matrix_client.last_machine_input, matrix_keys[i].inputs) != NULL);
        c64_keyboard_destroy(matrix_keyboard);
    }

    // A byte with no matrix mapping falls back to the legacy KERNAL-buffer
    // path for its whole batch rather than being sent with a bogus key name.
    worker_test_rest_client_t unmapped_client = {.machine_input_success = true};
    c64_keyboard_t *unmapped_keyboard = c64_keyboard_create(&unmapped_client);
    CHECK(unmapped_keyboard != NULL);
    c64_keyboard_set_transport(unmapped_keyboard, C64_STREAM_TRANSPORT_AUTO);
    c64_output_t unmapped_output = {.mode = C64_OUTPUT_PETSCII, .data.petscii = '|'};
    c64_keyboard_queue_output(unmapped_keyboard, &unmapped_output);
    CHECK(wait_for_consumed_count(&unmapped_client, 1, 1000));
    CHECK(unmapped_client.machine_input_calls == 0);
    c64_keyboard_destroy(unmapped_keyboard);

    // Once firmware reports machine:input unsupported, AUTO must stop
    // re-probing it: every batch otherwise pays another failed HTTP round-trip
    // before falling back to legacy. The stub answers 501 (NOT_SUPPORTED), so
    // only the first of these five single-byte batches may reach REST.
    worker_test_rest_client_t demote_client = {.machine_input_success = false};
    c64_keyboard_t *demote_keyboard = c64_keyboard_create(&demote_client);
    CHECK(demote_keyboard != NULL);
    c64_keyboard_set_transport(demote_keyboard, C64_STREAM_TRANSPORT_AUTO);
    for (int i = 0; i < 5; i++) {
        c64_output_t demote_output = {.mode = C64_OUTPUT_PETSCII, .data.petscii = 'A'};
        c64_keyboard_queue_output(demote_keyboard, &demote_output);
        CHECK(wait_for_consumed_count(&demote_client, (size_t)(i + 1), 1000));
    }
    CHECK(demote_client.consumed_count == 5);
    CHECK(demote_client.machine_input_calls == 1);
    c64_keyboard_destroy(demote_keyboard);

    // PETSCII control codes must translate too, not just printable ASCII --
    // otherwise any batch containing one falls all the way back to the
    // legacy KERNAL-buffer path (fallback is per-batch, never per-byte).
    // RETURN (0x0D) is the most common case: nearly every typed line ends
    // with \r. Cursor movement and HOME/CLR/DEL are the next most common.
    struct {
        uint8_t petscii;
        const char *expected_inputs;
    } control_codes[] = {
        {0x0D, "\"inputs\":[\"return\"]"},
        {0x11, "\"inputs\":[\"cursor_up_down\"]"},
        {0x91, "\"inputs\":[\"left_shift\",\"cursor_up_down\"]"},
        {0x1D, "\"inputs\":[\"cursor_left_right\"]"},
        {0x9D, "\"inputs\":[\"left_shift\",\"cursor_left_right\"]"},
        {0x13, "\"inputs\":[\"clr_home\"]"},
        {0x93, "\"inputs\":[\"left_shift\",\"clr_home\"]"},
        {0x14, "\"inputs\":[\"inst_del\"]"},
        {0x94, "\"inputs\":[\"left_shift\",\"inst_del\"]"},
    };
    for (size_t i = 0; i < sizeof(control_codes) / sizeof(control_codes[0]); i++) {
        worker_test_rest_client_t control_client = {.machine_input_success = true};
        c64_keyboard_t *control_keyboard = c64_keyboard_create(&control_client);
        CHECK(control_keyboard != NULL);
        c64_keyboard_set_transport(control_keyboard, C64_STREAM_TRANSPORT_REST);
        c64_output_t control_output = {.mode = C64_OUTPUT_PETSCII, .data.petscii = control_codes[i].petscii};
        c64_keyboard_queue_output(control_keyboard, &control_output);
        CHECK(wait_for_matrix_events(&control_client, 1, 1000));
        CHECK(strstr(control_client.last_machine_input, control_codes[i].expected_inputs) != NULL);
        c64_keyboard_destroy(control_keyboard);
    }

    return 0;
}
