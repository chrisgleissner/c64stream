/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#ifndef C64SCRIPT_TEST_STUBS_H
#define C64SCRIPT_TEST_STUBS_H

#include "c64-keyboard.h"
#include "c64-rest-client.h"

#include <stdint.h>

// Test environment creation and destruction
c64_rest_client_t *c64script_test_rest_create(void);
void c64script_test_rest_destroy(c64_rest_client_t *client);
void c64script_test_rest_set_byte(c64_rest_client_t *client, uint16_t address, uint8_t value);
void c64script_test_rest_fail_next(c64_rest_client_t *client, const char *error);
const char *c64script_test_rest_log(const c64_rest_client_t *client);
const char *c64script_test_rest_last_action(const c64_rest_client_t *client);
const char *c64script_test_rest_last_category(const c64_rest_client_t *client);
const char *c64script_test_rest_last_item(const c64_rest_client_t *client);
const char *c64script_test_rest_last_value(const c64_rest_client_t *client);
const char *c64script_test_rest_last_drive(const c64_rest_client_t *client);
const char *c64script_test_rest_last_path(const c64_rest_client_t *client);
const char *c64script_test_rest_last_type(const c64_rest_client_t *client);
const char *c64script_test_rest_last_mode(const c64_rest_client_t *client);

c64_keyboard_t *c64script_test_keyboard_create(void);
void c64script_test_keyboard_destroy(c64_keyboard_t *keyboard);
const char *c64script_test_keyboard_log(const c64_keyboard_t *keyboard);

void c64script_test_source_stub_reset(void);
void c64script_test_source_wait_fail_next(const char *error);
void c64script_test_source_screenshot_fail_next(const char *error);
uint32_t c64script_test_source_last_wait_frame_count(void);
int c64script_test_source_wait_call_count(void);
bool c64script_test_source_last_screenshot_preview(void);
const char *c64script_test_source_last_screenshot_path(void);
int c64script_test_source_screenshot_call_count(void);

#endif // C64SCRIPT_TEST_STUBS_H
