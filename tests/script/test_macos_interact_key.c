#ifdef NDEBUG
#undef NDEBUG
#endif

#include "c64-interact-key.h"

#include <assert.h>
#include <string.h>

static void expect(uint32_t code, const char *expected)
{
    char key[64] = {0};
    assert(c64_interact_translate_key_code(code, NULL, key) == C64_INTERACT_KEY_TRANSLATED);
    assert(strcmp(key, expected) == 0);
}

int main(void)
{
    expect(0x7B, "ArrowLeft");
    expect(0x7E, "ArrowUp");
    expect(0x7C, "ArrowRight");
    expect(0x7D, "ArrowDown");
    expect(0x7A, "F1");
    expect(0x6F, "F12");
    {
        c64_interact_key_t key = {{0}};
        assert(c64_interact_translate_key_event(0x35, NULL, &key) == C64_INTERACT_KEY_WARM_START);
        assert(strcmp(key.code, "Escape") == 0);
    }
    expect(0x30, "Tab");
    assert(strcmp(c64_interact_joystick_input_for_vkey(0x7B), "left") == 0);
    assert(strcmp(c64_interact_joystick_input_for_vkey(0x31), "fire") == 0);
    assert(c64_interact_key_is_escape(0x35, 0));
    assert(c64_interact_key_is_tab(0x30, 0));

    /* C64STR-025: Carbon physical keycodes that collide with ASCII letter/digit
     * ranges (e.g. keypad-decimal 0x41, keypad-clear 0x47) must NOT be misread
     * as "KeyA"/"KeyG". They resolve from the event text instead, so the
     * vkey-only translation leaves no bogus code. */
    {
        char key[64] = {0};
        /* 0x41 = kVK_ANSI_KeypadDecimal; would be 'A' if treated as ASCII. */
        assert(c64_interact_translate_key_code(0x41, NULL, key) == C64_INTERACT_KEY_NONE);
        assert(key[0] == '\0');
    }
    {
        char key[64] = {0};
        /* 0x47 = kVK_ANSI_KeypadClear; would be 'G' if treated as ASCII. */
        assert(c64_interact_translate_key_code(0x47, NULL, key) == C64_INTERACT_KEY_NONE);
        assert(key[0] == '\0');
    }
    {
        /* A letter key still injects correctly via the event text on macOS
         * (the printable character is carried in key.text, not a vkey-derived
         * code). */
        c64_interact_key_t key = {{0}};
        assert(c64_interact_translate_key_event(0x00 /* kVK_ANSI_A */, "a", &key) == C64_INTERACT_KEY_TRANSLATED);
        assert(strcmp(key.text, "a") == 0);
    }
    return 0;
}
