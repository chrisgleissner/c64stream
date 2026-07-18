/*
C64 Stream - Joystick emulation tests.

Covers the full joystick-emulation surface that the OBS interact thread drives
when F10/F11 toggle keyboard-vs-joystick mode and cursor keys / Space become
joystick inputs. The test exercises:

  * c64_joystick_classify_hotkey: F10/F11 recognition across both names so
    pressing either F10 or F11 on the OBS keyboard flips the right flag.
  * c64_joystick_input_for_vkey: every direction (up/down/left/right) and fire
    on the Windows, Linux (X11), and macOS Carbon paths; plus a negative case
    to confirm an unrelated vkey (e.g. letter 'a') maps to NULL.
The machine-command queue path (c64_keyboard_queue_machine_command -> worker
-> c64_rest_joystick_input) is already covered by
tests/script/test_machine_command_queue.c for joystick press/release FIFO
ordering; this file pins vkey-to-direction classification so a future change
cannot silently swap "left" and "right" or drop the Space -> fire mapping.
*/
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "c64-joystick-emulation.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* --- Test helpers --------------------------------------------------------- */

static void expect_hotkey(const char *key_code, c64_joystick_hotkey_t expected)
{
    const c64_joystick_hotkey_t actual = c64_joystick_classify_hotkey(key_code);
    assert(actual == expected);
}

static void expect_input(uint32_t native_vkey, const char *expected)
{
    const char *actual = c64_joystick_input_for_vkey(native_vkey);
    if (expected == NULL) {
        assert(actual == NULL);
    } else {
        assert(actual != NULL);
        assert(strcmp(actual, expected) == 0);
    }
}

int main(void)
{
    /* --- F10/F11 hotkey classification ----------------------------------- */

    /* F10 toggles keyboard-vs-joystick mode. */
    expect_hotkey("F10", C64_JOYSTICK_HOTKEY_F10);
    /* F11 toggles the joystick port. */
    expect_hotkey("F11", C64_JOYSTICK_HOTKEY_F11);
    /* F9 (device menu) and unrelated keys do not collide with the joystick
     * hotkeys -- the F9 path runs separately and must not be mistaken for F10. */
    expect_hotkey("F9", C64_JOYSTICK_HOTKEY_NONE);
    expect_hotkey("F12", C64_JOYSTICK_HOTKEY_NONE);
    expect_hotkey("KeyA", C64_JOYSTICK_HOTKEY_NONE);
    expect_hotkey("ArrowUp", C64_JOYSTICK_HOTKEY_NONE);
    expect_hotkey("", C64_JOYSTICK_HOTKEY_NONE);
    expect_hotkey(NULL, C64_JOYSTICK_HOTKEY_NONE);

    /* --- Cursor keys + Space map to joystick inputs ------------------------ */

#if defined(__APPLE__) || defined(C64_TEST_MACOS_KEYCODES)
    /* macOS Carbon physical keycodes. */
    expect_input(0x7B, "left");  /* kVK_LeftArrow  */
    expect_input(0x7E, "up");    /* kVK_UpArrow    */
    expect_input(0x7C, "right"); /* kVK_RightArrow */
    expect_input(0x7D, "down");  /* kVK_DownArrow  */
    expect_input(0x31, "fire");  /* kVK_Space      */
    /* Letters and other unrelated vkeys must not map to joystick inputs on
     * macOS -- they would otherwise hijack the letter keys. */
    expect_input(0x00 /* kVK_ANSI_A */, NULL);
    expect_input(0x35 /* Escape */, NULL);
#else
    /* Windows VK_* plus X11 keysym forms. */
    expect_input(0x25, "left");
    expect_input(0xFF51, "left");
    expect_input(0x26, "up");
    expect_input(0xFF52, "up");
    expect_input(0x27, "right");
    expect_input(0xFF53, "right");
    expect_input(0x28, "down");
    expect_input(0xFF54, "down");
    expect_input(0x20, "fire");
    /* Letters and other unrelated vkeys must not map to joystick inputs --
     * the letter keys must still reach the C64 keyboard buffer when joystick
     * emulation is on (F11 selects port, F10 toggles mode; letter keys
     * themselves never become joystick events). */
    expect_input('A', NULL);
    expect_input('a', NULL);
    expect_input(0x1B /* Escape */, NULL);
    expect_input(0x70 /* F1 */, NULL);
#endif

    printf("test_joystick_emulation: PASS\n");
    return 0;
}
