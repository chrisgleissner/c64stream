#include "c64-device-scan.h"
#include "c64-network.h"

#include <assert.h>
#include <stdio.h>

bool c64_debug_logging = false;

static void test_product_matching(void)
{
    assert(c64_device_scan_product_matches("C64 Ultimate"));
    assert(c64_device_scan_product_matches("Ultimate 64 Elite"));
    assert(c64_device_scan_product_matches("c64u"));
    assert(!c64_device_scan_product_matches("unrelated device"));
}

static void test_error_envelope(void)
{
    assert(c64_device_scan_is_ultimate_error("{\"errors\":[\"forbidden\"]}"));
    assert(!c64_device_scan_is_ultimate_error("{\"message\":\"forbidden\"}"));
}

static void test_prefix_clamp_and_own_address(void)
{
    uint32_t addresses[254];
    const uint32_t own = inet_addr("192.168.7.42");
    const size_t count = c64_device_scan_enumerate_subnet(own, 16, addresses, 254);
    assert(count == 253); /* /16 clamps to /24: 254 usable hosts minus own */
    for (size_t i = 0; i < count; i++) {
        assert(addresses[i] != own);
    }
}

int main(void)
{
    test_product_matching();
    test_error_envelope();
    test_prefix_clamp_and_own_address();
    puts("c64 device scan tests passed");
    return 0;
}
