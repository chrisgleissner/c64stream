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
    return 0;
}
