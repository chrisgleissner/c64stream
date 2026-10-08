#include "c64-device-scan.h"
#include "c64-device.h"
#include "c64-network.h"

#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <stdlib.h>
#include <unistd.h>
#endif

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
    CHECK(c64_device_scan_product_matches("Ultimate 64-II"));
    CHECK(c64_device_scan_product_matches("Ultimate 64"));
    CHECK(c64_device_scan_product_matches("c64u"));
    CHECK(!c64_device_scan_product_matches("unrelated device"));
    // Ultimate II family: disk/cartridge-only add-ons, no video/audio streaming.
    CHECK(!c64_device_scan_product_matches("Ultimate II+L"));
    CHECK(!c64_device_scan_product_matches("Ultimate II+"));
    CHECK(!c64_device_scan_product_matches("Ultimate II"));
    CHECK(!c64_device_scan_product_matches("Ultimate"));
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

static bool test_selection_apply_policy(void)
{
    CHECK(c64_device_scan_should_apply_selection("", "", false, "device-a"));
    CHECK(c64_device_scan_should_apply_selection("device-a", "device-a", true, ""));
    // A responsive previously selected device is deliberate: discovery may
    // refresh its profile, but must not replace it with another device.
    CHECK(c64_device_scan_should_apply_selection("device-a", "device-a", true, "device-b"));
    CHECK(!c64_device_scan_should_apply_selection("device-a", "device-a", false, ""));
    // If the prior selection no longer answers, only one unambiguous
    // replacement is allowed to take over automatically.
    CHECK(c64_device_scan_should_apply_selection("device-a", "device-a", false, "device-b"));
    CHECK(!c64_device_scan_should_apply_selection("device-a", "device-b", false, "device-c"));
    CHECK(c64_device_scan_should_apply_selection("my-device", "my-device", false, "other-device"));
    return true;
}

// Pins apply_scan_results()'s host_index "first wins" rule: when a single
// unique_id is discovered at two addresses (e.g. Ethernet + Wi-Fi on a
// multi-homed unit), the lower host_index wins so a saved device keeps the
// address it is on file with. Also covers the single-result and
// zero-result cases per the same rule.
static bool test_apply_scan_results_supersession(void)
{
    // Case 1: two results share a device.id with different host_index values;
    // the lower host_index (the saved/known address, enumerated first) wins,
    // and its host field is what ends up in the registry.
    c64_device_t multihomed[2] = {0};
    strcpy(multihomed[0].id, "dup-device");
    strcpy(multihomed[0].host, "192.168.1.50"); // wifi, discovered later
    strcpy(multihomed[1].id, "dup-device");
    strcpy(multihomed[1].host, "192.168.1.10"); // saved ethernet address
    const size_t multihomed_indices[2] = {5, 1};
    c64_device_scan_apply_results_for_test(multihomed, multihomed_indices, 2);
    const c64_device_t *winner = c64_device_registry_get("dup-device");
    CHECK(winner != NULL);
    CHECK(strcmp(winner->host, "192.168.1.10") == 0);
    CHECK(strcmp(winner->peer_host, "192.168.1.50") == 0);
    CHECK(c64_device_registry_count() == 1);
    c64_device_registry_delete("dup-device");

    // Case 2: a single result is upserted regardless of its host_index.
    c64_device_t single[1] = {0};
    strcpy(single[0].id, "solo-device");
    strcpy(single[0].host, "192.168.1.20");
    const size_t single_indices[1] = {42};
    c64_device_scan_apply_results_for_test(single, single_indices, 1);
    CHECK(c64_device_registry_get("solo-device") != NULL);
    CHECK(c64_device_registry_count() == 1);
    c64_device_registry_delete("solo-device");

    // Case 3: zero results leave the registry untouched.
    CHECK(c64_device_registry_count() == 0);
    c64_device_scan_apply_results_for_test(NULL, NULL, 0);
    CHECK(c64_device_registry_count() == 0);

    return true;
}

static bool test_rescan_preserves_saved_settings(void)
{
    c64_device_t saved = {0};
    strcpy(saved.id, "saved-device");
    strcpy(saved.name, "Living room C64");
    strcpy(saved.host, "192.168.1.10");
    strcpy(saved.dns_server_ip, "192.168.1.1");
    saved.video_port = 21042;
    saved.audio_port = 21043;
    saved.control_port = 1064;
    CHECK(c64_device_registry_upsert(&saved));

    // Rediscovery after DHCP changes must update the address without
    // replacing user settings with scan defaults.
    c64_device_t discovered = {0};
    strcpy(discovered.id, saved.id);
    strcpy(discovered.name, "C64 Ultimate (192.168.1.20)");
    strcpy(discovered.host, "192.168.1.20");
    discovered.video_port = 11000;
    discovered.audio_port = 11001;
    discovered.control_port = 64;
    const size_t index = 0;
    c64_device_scan_apply_results_for_test(&discovered, &index, 1);

    // Check the persisted profile too, rather than only the in-memory copy.
    c64_device_registry_cleanup();
    CHECK(c64_device_registry_init());
    const c64_device_t *actual = c64_device_registry_get(saved.id);
    CHECK(actual != NULL);
    CHECK(strcmp(actual->host, discovered.host) == 0);
    CHECK(strcmp(actual->name, saved.name) == 0);
    CHECK(strcmp(actual->dns_server_ip, saved.dns_server_ip) == 0);
    CHECK(actual->video_port == saved.video_port);
    CHECK(actual->audio_port == saved.audio_port);
    CHECK(actual->control_port == saved.control_port);
    CHECK(c64_device_registry_delete(saved.id));
    return true;
}

static bool test_discovery_preserves_migrated_profile(void)
{
    c64_device_t migrated = {0};
    strcpy(migrated.host, "192.168.1.64");
    CHECK(c64_device_id_from_host(migrated.id, sizeof(migrated.id), NULL, migrated.host));
    strcpy(migrated.name, "My C64");
    strcpy(migrated.dns_server_ip, "192.168.1.1");
    migrated.video_port = 21000;
    migrated.audio_port = 21001;
    migrated.control_port = 6400;
    CHECK(c64_device_registry_upsert(&migrated));

    c64_device_t discovered = {0};
    strcpy(discovered.id, "physical-device");
    strcpy(discovered.host, migrated.host);
    strcpy(discovered.peer_host, "192.168.1.65");
    discovered.video_port = 11000;
    discovered.audio_port = 11001;
    discovered.control_port = 64;
    CHECK(c64_device_registry_upsert_discovered(&discovered));
    const c64_device_t *actual = c64_device_registry_get(discovered.id);
    CHECK(actual != NULL);
    CHECK(strcmp(actual->id, discovered.id) == 0);
    CHECK(strcmp(actual->name, migrated.name) == 0);
    CHECK(strcmp(actual->peer_host, discovered.peer_host) == 0);
    CHECK(strcmp(actual->dns_server_ip, migrated.dns_server_ip) == 0);
    CHECK(actual->video_port == migrated.video_port);
    CHECK(actual->audio_port == migrated.audio_port);
    CHECK(actual->control_port == migrated.control_port);

    // The hardware profile becomes authoritative after the first discovery.
    migrated.video_port = 22000;
    CHECK(c64_device_registry_upsert(&migrated));
    CHECK(c64_device_registry_upsert_discovered(&discovered));
    actual = c64_device_registry_get(discovered.id);
    CHECK(actual->video_port == 21000);
    CHECK(c64_device_registry_delete(migrated.id));

    // A different physical device at the same address must use its own
    // defaults, not inherit the prior hardware's saved settings.
    strcpy(discovered.id, "replacement-device");
    CHECK(c64_device_registry_upsert_discovered(&discovered));
    actual = c64_device_registry_get(discovered.id);
    CHECK(actual->video_port == 11000);
    CHECK(c64_device_registry_delete(discovered.id));
    CHECK(c64_device_registry_delete("physical-device"));
    return true;
}

int main(void)
{
#ifndef _WIN32
    char root[] = "/tmp/c64-device-scan-test-XXXXXX";
    if (!mkdtemp(root)) {
        return 1;
    }
    if (setenv("XDG_DOCUMENTS_DIR", root, 1) != 0) {
        return 1;
    }
#endif
    if (!c64_device_registry_init()) {
        return 1;
    }
    if (!test_product_matching() || !test_error_envelope() || !test_prefix_clamp_and_own_address() ||
        !test_selection_apply_policy() || !test_apply_scan_results_supersession() ||
        !test_rescan_preserves_saved_settings() || !test_discovery_preserves_migrated_profile()) {
        return 1;
    }
    puts("c64 device scan tests passed");
    return 0;
}
