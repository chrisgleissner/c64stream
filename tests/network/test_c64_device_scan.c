#include "c64-device-scan.h"
#include "c64-network.h"

#include <stdio.h>

#define CHECK(expr)                                                                                                       \
    do {                                                                                                                  \
        if (!(expr)) {                                                                                                    \
            fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #expr, __FILE__, __LINE__);                                \
            return false;                                                                                                 \
        }                                                                                                                 \
    } while (0)

bool c64_debug_logging = false;

static bool test_product_matching(void)
{
    CHECK(c64_device_scan_product_matches("C64 Ultimate"));
    CHECK(c64_device_scan_product_matches("Ultimate 64 Elite"));
    CHECK(c64_device_scan_product_matches("c64u"));
    CHECK(!c64_device_scan_product_matches("unrelated device"));
    return true;
}

static bool test_error_envelope(void)
{
    CHECK(c64_device_scan_is_ultimate_error("{\"errors\":[\"forbidden\"]}"));
    CHECK(c64_device_scan_is_ultimate_error(" { \"message\": \"forbidden\", \"errors\": [] }"));
    CHECK(!c64_device_scan_is_ultimate_error("{\"message\":\"forbidden\"}"));
    CHECK(!c64_device_scan_is_ultimate_error("{\"message\":\"\\\"errors\\\":[ ]\"}"));
    CHECK(!c64_device_scan_is_ultimate_error("{\"errors\":\"forbidden\"}"));
    CHECK(!c64_device_scan_is_ultimate_error("{\"errors\":[\"forbidden\"}"));
    CHECK(!c64_device_scan_is_ultimate_error("{\"errors\":[]} trailing"));
    CHECK(!c64_device_scan_is_ultimate_error("not json {\"errors\":[]}"));
    CHECK(c64_device_scan_response_is_candidate(401, NULL));
    CHECK(c64_device_scan_response_is_candidate(403, "{\"errors\":[\"forbidden\"]}"));
    CHECK(!c64_device_scan_response_is_candidate(403, "{\"message\":\"forbidden\"}"));
    CHECK(!c64_device_scan_response_is_candidate(500, "{\"errors\":[\"failure\"]}"));
    return true;
}

static bool test_prefix_clamp_and_own_address(void)
{
    uint32_t addresses[254];
    const uint32_t own = inet_addr("192.168.7.42");
    const size_t count = c64_device_scan_enumerate_subnet(own, 16, addresses, 254);
    CHECK(count == 253); /* /16 clamps to /24: 254 usable hosts minus own */
    for (size_t i = 0; i < count; i++) {
        CHECK(addresses[i] != own);
    }

    const size_t narrow_count = c64_device_scan_enumerate_subnet(own, 31, addresses, 254);
    CHECK(narrow_count == 1); /* /31 clamps to /30: two usable hosts, excluding own. */
    return true;
}

int main(void)
{
    if (!test_product_matching() || !test_error_envelope() || !test_prefix_clamp_and_own_address()) {
        return 1;
    }
    puts("c64 device scan tests passed");
    return 0;
}
