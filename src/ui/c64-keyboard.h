/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Keyboard capture, keymap management, and keystroke injection for C64
 */

typedef struct c64_keyboard c64_keyboard_t;
typedef struct c64_keymap c64_keymap_t;

/**
 * Keystroke output modes
 */
typedef enum {
    C64_OUTPUT_TEXT,    // text:"..." - ASCII to PETSCII conversion
    C64_OUTPUT_PETSCII, // petscii:0xNN - Raw PETSCII byte
    C64_OUTPUT_SYMBOLIC // c64:NAME - Symbolic name (RETURN, CURSOR_UP, etc.)
} c64_output_mode_t;

/**
 * Keystroke output descriptor
 */
typedef struct {
    c64_output_mode_t mode;
    union {
        char text[256];  // For OUTPUT_TEXT
        uint8_t petscii; // For OUTPUT_PETSCII
        char symbol[32]; // For OUTPUT_SYMBOLIC
    } data;
} c64_output_t;

/**
 * Machine-control command types (C64STR-022).
 *
 * These map to blocking REST calls (joystick input, on-screen menu, reset,
 * reboot, release-all). They are enqueued from the OBS UI/interact thread and
 * executed asynchronously on the keyboard worker thread so a slow or
 * unreachable device never blocks the OBS UI.
 */
typedef enum {
    C64_MACHINE_CMD_JOYSTICK,    // Joystick press/release on a port
    C64_MACHINE_CMD_MENU,        // Toggle device on-screen menu (F9)
    C64_MACHINE_CMD_RESET,       // Soft reset (Ctrl/Shift+ESC)
    C64_MACHINE_CMD_REBOOT,      // Reboot (ESC+TAB)
    C64_MACHINE_CMD_RELEASE_ALL, // Release all held joystick inputs
} c64_machine_cmd_type_t;

/**
 * Machine-control command descriptor (enqueued for async execution).
 */
typedef struct {
    c64_machine_cmd_type_t type;
    int joystick_port;       // For C64_MACHINE_CMD_JOYSTICK
    char joystick_input[16]; // For C64_MACHINE_CMD_JOYSTICK ("up"/"down"/...)
    bool joystick_press;     // For C64_MACHINE_CMD_JOYSTICK: press vs release
} c64_machine_command_t;

/**
 * Load keymap from file
 * @param path Path to .c64keymap.ini file
 * @return Keymap instance or NULL on error
 */
c64_keymap_t *c64_keymap_load(const char *path);

/**
 * Destroy keymap and free resources
 */
void c64_keymap_destroy(c64_keymap_t *keymap);

/**
 * Get keymap name (from [meta] name field)
 */
const char *c64_keymap_get_name(c64_keymap_t *keymap);

/**
 * Normalize a keymap identifier into the canonical runtime form.
 */
bool c64_keymap_normalize_identifier(const char *input, char normalized[64]);

/**
 * Return whether the normalized identifier can be produced by the runtime.
 */
bool c64_keymap_identifier_is_runtime_supported(const char *identifier);

/**
 * Convert keyboard input to C64 output
 * @param keymap Keymap to use
 * @param key_code Code-oriented identifier (e.g. "KeyA", "Enter")
 * @param key_text Text-oriented identifier for symbolic fallback (e.g. "a", "@")
 * @param modifiers Bitmask of modifiers (Ctrl=1, Shift=2, Alt=4, etc.)
 * @param output Output descriptor (filled on success)
 * @return true if mapping found
 */
bool c64_keymap_convert(c64_keymap_t *keymap, const char *key_code, const char *key_text, int modifiers,
                        c64_output_t *output);

/**
 * Create keyboard controller
 * @param rest_client REST client for DMA operations
 * @return Keyboard instance or NULL on error
 */
c64_keyboard_t *c64_keyboard_create(void *rest_client);

/**
 * Destroy keyboard controller
 */
void c64_keyboard_destroy(c64_keyboard_t *keyboard);

/**
 * Set active keymap
 */
void c64_keyboard_set_keymap(c64_keyboard_t *keyboard, c64_keymap_t *keymap);
void c64_keyboard_set_transport(c64_keyboard_t *keyboard, int transport);

/**
 * Enable/disable keyboard capture
 * @param enabled true to enable, false to disable and flush queue
 */
void c64_keyboard_set_capture(c64_keyboard_t *keyboard, bool enabled);

/**
 * Check if capture is currently active
 */
bool c64_keyboard_is_capturing(c64_keyboard_t *keyboard);
bool c64_keyboard_release_all(c64_keyboard_t *keyboard);

/**
 * Queue keystroke for injection
 * @param output Output descriptor from keymap conversion
 */
bool c64_keyboard_queue_output(c64_keyboard_t *keyboard, const c64_output_t *output);

/**
 * Enqueue a machine-control command (joystick/menu/reset/reboot/release-all)
 * for asynchronous execution on the worker thread (C64STR-022).
 *
 * Returns immediately without any network I/O. Commands run in FIFO order so
 * joystick press/release ordering is preserved. Returns false only if the
 * command queue is full (the worker has fallen far behind, e.g. a wedged
 * device); callers may safely ignore the result for best-effort input.
 *
 * @param keyboard Keyboard instance
 * @param cmd Command descriptor (copied)
 * @return true if enqueued, false if the queue is full or args are invalid
 */
bool c64_keyboard_queue_machine_command(c64_keyboard_t *keyboard, const c64_machine_command_t *cmd);

/**
 * Get injection worker status
 * @return Status string (e.g. "idle", "injecting", "timeout")
 */
const char *c64_keyboard_get_status(c64_keyboard_t *keyboard);

/**
 * Discover available keymaps
 * @param paths Output array of paths (caller must free)
 * @param count Output count
 * @return true on success
 */
bool c64_keyboard_discover_keymaps(char ***paths, size_t *count);

/**
 * Perform BASIC warm start via IRQ vector manipulation
 * Aborts currently running BASIC program and returns to READY prompt
 * without resetting the machine or destroying user memory.
 * Thread-safe and safe to call multiple times.
 * @param keyboard Keyboard instance
 * @return true on success, false on error
 */
bool c64_keyboard_basic_warm_start(c64_keyboard_t *keyboard);
