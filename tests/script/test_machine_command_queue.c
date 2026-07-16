/* C64 Stream - C64STR-022 regression.
 *
 * Proves joystick / menu / reset / reboot / release-all interactions enqueue
 * work instead of issuing blocking REST on the OBS UI thread: the enqueue call
 * returns promptly even when every REST call is artificially slow, the worker
 * executes the commands asynchronously, and FIFO (press/release) ordering is
 * preserved.
 *
 * Links the real src/ui/c64-keyboard.c and stubs the REST client with a
 * latency-injecting mock. */
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "c64-file.h"
#include "c64-keyboard.h"
#include "c64-rest-client.h"

#include <obs-module.h>

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define REST_LATENCY_MS 20
#define CMD_COUNT 8

/* --- executed-command log (filled by the worker via the mock) ------------- */
struct mock_rest {
    pthread_mutex_t lock;
    int executed; /* total commands executed */
    c64_machine_cmd_type_t log[64];
    char joy_input[64][16];
    bool joy_press[64];
    int joy_port[64];
};

static struct mock_rest g_mock;

static void mock_sleep_ms(int ms)
{
    struct timespec ts = {.tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

static void mock_record(c64_machine_cmd_type_t type, int port, const char *input, bool press)
{
    /* Inject latency OUTSIDE the lock, mirroring a slow device. */
    mock_sleep_ms(REST_LATENCY_MS);
    pthread_mutex_lock(&g_mock.lock);
    int i = g_mock.executed;
    if (i < (int)(sizeof(g_mock.log) / sizeof(g_mock.log[0]))) {
        g_mock.log[i] = type;
        g_mock.joy_port[i] = port;
        g_mock.joy_press[i] = press;
        if (input) {
            snprintf(g_mock.joy_input[i], sizeof(g_mock.joy_input[i]), "%s", input);
        } else {
            g_mock.joy_input[i][0] = '\0';
        }
    }
    g_mock.executed++;
    pthread_mutex_unlock(&g_mock.lock);
}

static int mock_executed(void)
{
    pthread_mutex_lock(&g_mock.lock);
    int n = g_mock.executed;
    pthread_mutex_unlock(&g_mock.lock);
    return n;
}

/* --- REST client stubs referenced by c64-keyboard.c ----------------------- */
bool c64_rest_joystick_input(c64_rest_client_t *client, int port, const char *input_name, const char *transition)
{
    (void)client;
    mock_record(C64_MACHINE_CMD_JOYSTICK, port, input_name, transition && strcmp(transition, "press") == 0);
    return true;
}
bool c64_rest_menu_button(c64_rest_client_t *client)
{
    (void)client;
    mock_record(C64_MACHINE_CMD_MENU, 0, NULL, false);
    return true;
}
bool c64_rest_reset(c64_rest_client_t *client)
{
    (void)client;
    mock_record(C64_MACHINE_CMD_RESET, 0, NULL, false);
    return true;
}
bool c64_rest_reboot(c64_rest_client_t *client)
{
    (void)client;
    mock_record(C64_MACHINE_CMD_REBOOT, 0, NULL, false);
    return true;
}
bool c64_rest_release_all(c64_rest_client_t *client)
{
    (void)client;
    mock_record(C64_MACHINE_CMD_RELEASE_ALL, 0, NULL, false);
    return true;
}
/* Keystroke-path stubs (unused here but needed for linking). */
bool c64_rest_machine_input_with_outcome(c64_rest_client_t *client, const char *json, c64_rest_outcome_t *outcome,
                                         long *status)
{
    (void)client;
    (void)json;
    if (outcome)
        *outcome = C64_REST_OK;
    if (status)
        *status = 200;
    return true;
}
int c64_rest_read_memory(c64_rest_client_t *client, uint16_t address, size_t length, uint8_t *buffer,
                         size_t buffer_size)
{
    (void)client;
    (void)address;
    (void)length;
    (void)buffer_size;
    if (buffer && length > 0)
        buffer[0] = 0;
    return (int)length;
}
bool c64_rest_write_memory(c64_rest_client_t *client, uint16_t address, const uint8_t *data, size_t length)
{
    (void)client;
    (void)address;
    (void)data;
    (void)length;
    return true;
}
long c64_rest_get_last_status(const c64_rest_client_t *client)
{
    (void)client;
    return 200;
}

/* --- misc stubs ----------------------------------------------------------- */
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

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

int main(void)
{
    memset(&g_mock, 0, sizeof(g_mock));
    pthread_mutex_init(&g_mock.lock, NULL);

    /* rest_client pointer is opaque to the keyboard; the stubs ignore it. */
    c64_keyboard_t *kb = c64_keyboard_create((void *)&g_mock);
    assert(kb != NULL);

    /* Enqueue a mixed command sequence including a joystick press/release pair
     * whose order must be preserved. */
    c64_machine_command_t seq[CMD_COUNT];
    memset(seq, 0, sizeof(seq));
    seq[0] = (c64_machine_command_t){.type = C64_MACHINE_CMD_JOYSTICK, .joystick_port = 2, .joystick_press = true};
    snprintf(seq[0].joystick_input, sizeof(seq[0].joystick_input), "up");
    seq[1] = (c64_machine_command_t){.type = C64_MACHINE_CMD_JOYSTICK, .joystick_port = 2, .joystick_press = false};
    snprintf(seq[1].joystick_input, sizeof(seq[1].joystick_input), "up");
    seq[2] = (c64_machine_command_t){.type = C64_MACHINE_CMD_MENU};
    seq[3] = (c64_machine_command_t){.type = C64_MACHINE_CMD_JOYSTICK, .joystick_port = 1, .joystick_press = true};
    snprintf(seq[3].joystick_input, sizeof(seq[3].joystick_input), "fire");
    seq[4] = (c64_machine_command_t){.type = C64_MACHINE_CMD_JOYSTICK, .joystick_port = 1, .joystick_press = false};
    snprintf(seq[4].joystick_input, sizeof(seq[4].joystick_input), "fire");
    seq[5] = (c64_machine_command_t){.type = C64_MACHINE_CMD_RESET};
    seq[6] = (c64_machine_command_t){.type = C64_MACHINE_CMD_REBOOT};
    seq[7] = (c64_machine_command_t){.type = C64_MACHINE_CMD_RELEASE_ALL};

    double t0 = now_ms();
    for (int i = 0; i < CMD_COUNT; i++) {
        assert(c64_keyboard_queue_machine_command(kb, &seq[i]));
    }
    double enqueue_ms = now_ms() - t0;

    /* The whole enqueue burst must return far faster than even a single
     * synchronous REST call would have taken. Total synchronous work would be
     * CMD_COUNT * REST_LATENCY_MS (160 ms); enqueue must be a tiny fraction. */
    printf("enqueue of %d commands took %.2f ms (sync would be >= %d ms)\n", CMD_COUNT, enqueue_ms,
           CMD_COUNT * REST_LATENCY_MS);
    assert(enqueue_ms < REST_LATENCY_MS); /* well under one REST latency */

    /* Work must still be in flight (async): not everything can have run yet. */
    int executed_right_after = mock_executed();
    assert(executed_right_after < CMD_COUNT);

    /* Drain: wait for the worker to finish, with a generous timeout. */
    double deadline = now_ms() + 5000.0;
    while (mock_executed() < CMD_COUNT && now_ms() < deadline) {
        mock_sleep_ms(2);
    }
    assert(mock_executed() == CMD_COUNT);

    /* Verify FIFO order and correct dispatch. */
    for (int i = 0; i < CMD_COUNT; i++) {
        assert(g_mock.log[i] == seq[i].type);
    }
    /* Joystick press/release ordering + fields preserved. */
    assert(g_mock.log[0] == C64_MACHINE_CMD_JOYSTICK && g_mock.joy_press[0] == true &&
           strcmp(g_mock.joy_input[0], "up") == 0 && g_mock.joy_port[0] == 2);
    assert(g_mock.log[1] == C64_MACHINE_CMD_JOYSTICK && g_mock.joy_press[1] == false &&
           strcmp(g_mock.joy_input[1], "up") == 0);
    assert(g_mock.joy_port[3] == 1 && strcmp(g_mock.joy_input[3], "fire") == 0 && g_mock.joy_press[3] == true);

    c64_keyboard_destroy(kb);
    pthread_mutex_destroy(&g_mock.lock);

    printf("test_machine_command_queue: PASS\n");
    return 0;
}
