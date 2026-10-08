/* Hermetic discovery tests against in-process fake Ultimate devices.
 *
 * Every probe in these tests goes to a real TCP listener on the loopback
 * interface, so the full discovery path runs: libcurl /v1/info request,
 * response classification, control-port connect, retries, supersession and the
 * registry write. Nothing leaves the machine and no real device is needed, so
 * the test runs in CI on Linux, macOS and Windows, which is where the
 * platform-specific socket, threading and interface-enumeration code differs.
 */

#include "c64-device-scan.h"
#include "c64-device.h"
#include "c64-network.h"

#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util/platform.h>
#include <util/threading.h>
#ifndef _WIN32
#include <poll.h>
#include <signal.h>
#endif

#define CHECK(expr)                                                                                                       \
    do {                                                                                                                  \
        if (!(expr)) {                                                                                                    \
            fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #expr, __FILE__, __LINE__);                                \
            return false;                                                                                                 \
        }                                                                                                                 \
    } while (0)

bool c64_debug_logging = false;

static const char *const C64U_INFO = "{\n  \"product\" : \"C64 Ultimate\",\n  \"firmware_version\" : \"1.2.1\",\n"
                                     "  \"hostname\" : \"c64u\",\n  \"unique_id\" : \"5D0464\",\n"
                                     "  \"errors\" : [  ]\n}";

typedef enum {
    FAKE_REPLY,         // Send status + body.
    FAKE_BLACKHOLE,     // Accept, read the request, never answer.
    FAKE_RESET_PARTIAL, // Send half a response, then close.
} fake_mode_t;

typedef struct {
    fake_mode_t mode;
    int status;
    const char *body;
    // Replies to the first slow_requests requests are delayed by delay_ms.
    int delay_ms;
    int slow_requests;
    // When set, a request without this X-Password value gets 403.
    const char *password;
    // Pads the body to at least this many bytes (oversized /v1/info).
    size_t pad_to;

    socket_t http;
    socket_t control;
    uint16_t http_port;
    uint16_t control_port;
    bool control_enabled;
    volatile bool stop;
    volatile long requests;
    pthread_t thread;
} fake_device_t;

static socket_t listen_loopback(uint16_t *port)
{
    socket_t sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET_VALUE) {
        return sock;
    }
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof(addr);
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(sock, 64) != 0 ||
        getsockname(sock, (struct sockaddr *)&addr, &len) != 0) {
        close(sock);
        return INVALID_SOCKET_VALUE;
    }
    *port = ntohs(addr.sin_port);
    return sock;
}

static bool wait_readable(socket_t sock, int timeout_ms)
{
#ifdef _WIN32
    WSAPOLLFD fd = {0};
    fd.fd = sock;
    fd.events = POLLRDNORM;
    return WSAPoll(&fd, 1, timeout_ms) > 0;
#else
    struct pollfd fd = {0};
    fd.fd = sock;
    fd.events = POLLIN;
    return poll(&fd, 1, timeout_ms) > 0;
#endif
}

static void send_all(socket_t sock, const char *data, size_t length)
{
    while (length) {
        const int sent = (int)send(sock, data, (int)length, 0);
        if (sent <= 0) {
            return;
        }
        data += sent;
        length -= (size_t)sent;
    }
}

static void serve_http(fake_device_t *fake, socket_t client)
{
    char request[2048] = {0};
    size_t used = 0;
    while (used + 1 < sizeof(request) && !strstr(request, "\r\n\r\n") && wait_readable(client, 2000)) {
        const int got = (int)recv(client, request + used, (int)(sizeof(request) - 1 - used), 0);
        if (got <= 0) {
            break;
        }
        used += (size_t)got;
    }
    const long index = os_atomic_inc_long(&fake->requests);
    if (fake->mode == FAKE_BLACKHOLE) {
        while (!fake->stop) {
            os_sleep_ms(20);
        }
        return;
    }
    if (index <= fake->slow_requests && fake->delay_ms) {
        os_sleep_ms(fake->delay_ms);
    }
    int status = fake->status;
    const char *body = fake->body ? fake->body : "";
    if (fake->password) {
        char expected[128];
        snprintf(expected, sizeof(expected), "X-Password: %s\r\n", fake->password);
        if (!strstr(request, expected)) {
            status = 403;
            body = "{\n  \"errors\" : [ \"Forbidden.\" ]\n}";
        }
    }
    char *padded = NULL;
    if (fake->pad_to > strlen(body)) {
        // Extra fields after the identity fields, as a newer firmware might add.
        padded = malloc(fake->pad_to + 64);
        const size_t head = strlen(body) - 1; // drop the closing brace
        memcpy(padded, body, head);
        size_t at = head;
        at += (size_t)sprintf(padded + at, ",\n  \"extra\" : \"");
        while (at < fake->pad_to) {
            padded[at++] = 'x';
        }
        strcpy(padded + at, "\"\n}");
        body = padded;
    }
    char header[256];
    const int header_length = snprintf(header, sizeof(header),
                                       "HTTP/1.1 %d X\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n"
                                       "Connection: close\r\n\r\n",
                                       status, strlen(body));
    send_all(client, header, (size_t)header_length);
    if (fake->mode == FAKE_RESET_PARTIAL) {
        send_all(client, body, strlen(body) / 2);
    } else {
        send_all(client, body, strlen(body));
    }
    free(padded);
}

static void *fake_main(void *opaque)
{
    fake_device_t *fake = opaque;
    while (!fake->stop) {
        if (fake->control_enabled && wait_readable(fake->control, 0)) {
            socket_t client = accept(fake->control, NULL, NULL);
            if (client != INVALID_SOCKET_VALUE) {
                close(client);
            }
        }
        if (!wait_readable(fake->http, 10)) {
            continue;
        }
        socket_t client = accept(fake->http, NULL, NULL);
        if (client == INVALID_SOCKET_VALUE) {
            continue;
        }
        serve_http(fake, client);
        close(client);
    }
    return NULL;
}

static bool fake_start(fake_device_t *fake)
{
    fake->http = listen_loopback(&fake->http_port);
    fake->control = listen_loopback(&fake->control_port);
    if (fake->http == INVALID_SOCKET_VALUE || fake->control == INVALID_SOCKET_VALUE) {
        return false;
    }
    if (!fake->control_enabled) {
        // A port nothing listens on: closing the listener makes connects fail.
        close(fake->control);
        fake->control = INVALID_SOCKET_VALUE;
    }
    return pthread_create(&fake->thread, NULL, fake_main, fake) == 0;
}

static void fake_stop(fake_device_t *fake)
{
    fake->stop = true;
    pthread_join(fake->thread, NULL);
    close(fake->http);
    if (fake->control != INVALID_SOCKET_VALUE) {
        close(fake->control);
    }
}

static void clear_registry(void)
{
    while (c64_device_registry_count()) {
        const c64_device_t *device = c64_device_registry_get_at(0);
        char id[C64_DEVICE_ID_MAX];
        snprintf(id, sizeof(id), "%s", device->id);
        if (!c64_device_registry_delete(id)) {
            break;
        }
    }
}

static c64_device_scan_test_stats_t scan_fake(fake_device_t *fake, const char *const *hosts, size_t count)
{
    c64_device_scan_test_stats_t stats = {0};
    c64_device_scan_hosts_for_test(hosts, count, fake->http_port, fake->control_port, &stats);
    return stats;
}

static const char *const LOOPBACK[] = {"127.0.0.1"};

static bool test_valid_device_is_registered(void)
{
    clear_registry();
    fake_device_t fake = {.mode = FAKE_REPLY, .status = 200, .body = C64U_INFO, .control_enabled = true};
    CHECK(fake_start(&fake));
    const c64_device_scan_test_stats_t stats = scan_fake(&fake, LOOPBACK, 1);
    fake_stop(&fake);
    CHECK(stats.responsive == 1);
    const c64_device_t *device = c64_device_registry_get("5d0464");
    CHECK(device != NULL);
    CHECK(strcmp(device->host, "127.0.0.1") == 0);
    CHECK(strstr(device->name, "c64u (127.0.0.1)") != NULL);
    CHECK(device->control_port == fake.control_port);
    return true;
}

static bool test_control_port_required(void)
{
    clear_registry();
    // REST answers, but the stream control port does not: it cannot drive a stream.
    fake_device_t fake = {.mode = FAKE_REPLY, .status = 200, .body = C64U_INFO, .control_enabled = false};
    CHECK(fake_start(&fake));
    const c64_device_scan_test_stats_t stats = scan_fake(&fake, LOOPBACK, 1);
    fake_stop(&fake);
    CHECK(stats.responsive == 0);
    CHECK(c64_device_registry_count() == 0);
    return true;
}

static bool test_foreign_servers_rejected(void)
{
    static const struct {
        int status;
        const char *body;
    } cases[] = {
        {200, "{\"product\":\"Ultimate II+L\",\"unique_id\":\"ABCDEF\",\"errors\":[]}"}, // no streaming hardware
        {200, "<html><body>router login</body></html>"},
        {200, "{\"product\":42}"},
        {403, "<html>Forbidden</html>"}, // generic 403, not the Ultimate error envelope
        {404, "{\"errors\":[\"Not found\"]}"},
        {500, "{\"errors\":[\"failure\"]}"},
        {302, ""},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        clear_registry();
        fake_device_t fake = {.mode = FAKE_REPLY,
                              .status = cases[i].status,
                              .body = cases[i].body,
                              .control_enabled = true};
        CHECK(fake_start(&fake));
        scan_fake(&fake, LOOPBACK, 1);
        fake_stop(&fake);
        if (c64_device_registry_count() != 0) {
            fprintf(stderr, "foreign server case %zu (status %d) was registered\n", i, cases[i].status);
            return false;
        }
    }
    return true;
}

static bool test_password_protected_device(void)
{
    clear_registry();
    fake_device_t fake = {.mode = FAKE_REPLY,
                          .status = 200,
                          .body = C64U_INFO,
                          .password = "secret",
                          .control_enabled = true};
    CHECK(fake_start(&fake));
    scan_fake(&fake, LOOPBACK, 1);
    // The identity fetch used by sender adoption sends the password.
    char unique_id[64];
    char ignored[64];
    const bool with_password =
        c64_device_fetch_unique_id("127.0.0.1", fake.http_port, "secret", 1000, unique_id, sizeof(unique_id));
    const bool without_password =
        c64_device_fetch_unique_id("127.0.0.1", fake.http_port, "", 1000, ignored, sizeof(ignored));
    fake_stop(&fake);
    // Discovered without its unique_id, so it is keyed by address and labelled.
    CHECK(c64_device_registry_count() == 1);
    const c64_device_t *device = c64_device_registry_get_at(0);
    CHECK(strcmp(device->id, "127-0-0-1") == 0);
    CHECK(strstr(device->name, "Password") != NULL);
    CHECK(!c64_device_profile_is_identified(device));
    CHECK(with_password);
    CHECK(strcmp(unique_id, "5D0464") == 0);
    CHECK(!without_password);
    return true;
}

static bool test_unresponsive_host_is_bounded(void)
{
    clear_registry();
    // Accepts the connection and never answers: the probe must give up on its
    // own timeout instead of holding the scan.
    fake_device_t fake = {.mode = FAKE_BLACKHOLE, .control_enabled = true};
    CHECK(fake_start(&fake));
    const c64_device_scan_test_stats_t stats = scan_fake(&fake, LOOPBACK, 1);
    fake_stop(&fake);
    CHECK(stats.responsive == 0);
    CHECK(stats.elapsed_ms < 3000);
    return true;
}

static bool test_truncated_response_rejected(void)
{
    clear_registry();
    fake_device_t fake = {.mode = FAKE_RESET_PARTIAL, .status = 200, .body = C64U_INFO, .control_enabled = true};
    CHECK(fake_start(&fake));
    scan_fake(&fake, LOOPBACK, 1);
    fake_stop(&fake);
    CHECK(c64_device_registry_count() == 0);
    return true;
}

static bool test_oversized_response_accepted(void)
{
    clear_registry();
    fake_device_t fake = {.mode = FAKE_REPLY,
                          .status = 200,
                          .body = C64U_INFO,
                          .pad_to = 16384,
                          .control_enabled = true};
    CHECK(fake_start(&fake));
    scan_fake(&fake, LOOPBACK, 1);
    fake_stop(&fake);
    CHECK(c64_device_registry_get("5d0464") != NULL);
    return true;
}

static bool test_known_host_survives_slow_first_answer(void)
{
    clear_registry();
    // A registered address that misses the first probe (latency spike on a
    // Wi-Fi interface under scan load) is retried; a new address is not.
    fake_device_t fake = {.mode = FAKE_REPLY,
                          .status = 200,
                          .body = C64U_INFO,
                          .delay_ms = 900,
                          .slow_requests = 1,
                          .control_enabled = true};
    CHECK(fake_start(&fake));
    scan_fake(&fake, LOOPBACK, 1);
    const bool registered_as_new = c64_device_registry_count() != 0;

    c64_device_t saved = {0};
    strcpy(saved.id, "5d0464");
    strcpy(saved.host, "127.0.0.1");
    strcpy(saved.name, "Studio C64U");
    saved.control_port = fake.control_port;
    CHECK(c64_device_registry_upsert(&saved));
    fake.requests = 0;
    scan_fake(&fake, LOOPBACK, 1);
    fake_stop(&fake);
    CHECK(!registered_as_new);
    const c64_device_t *device = c64_device_registry_get("5d0464");
    CHECK(device != NULL);
    CHECK(strcmp(device->name, "Studio C64U") == 0); // user's name kept
    CHECK(fake.requests >= 2);
    return true;
}

static bool test_two_addresses_one_device(void)
{
    clear_registry();
    // Both names reach the same fake device, like Ethernet and Wi-Fi of one
    // C64U: one profile, the first enumerated address wins, the other is kept
    // as the verified peer.
    fake_device_t fake = {.mode = FAKE_REPLY, .status = 200, .body = C64U_INFO, .control_enabled = true};
    CHECK(fake_start(&fake));
    static const char *const hosts[] = {"127.0.0.1", "localhost"};
    scan_fake(&fake, hosts, 2);
    fake_stop(&fake);
    CHECK(c64_device_registry_count() == 1);
    const c64_device_t *device = c64_device_registry_get("5d0464");
    CHECK(device != NULL);
    CHECK(strcmp(device->host, "127.0.0.1") == 0);
    CHECK(strcmp(device->peer_host, "localhost") == 0);
    return true;
}

static bool test_long_hostname_probed_intact(void)
{
    clear_registry();
    // Hostnames longer than a dotted quad (the U64 default is
    // "Ultimate-64-Elite-XXXXXX", often with a domain suffix) must reach the
    // probe untruncated. .invalid never resolves (RFC 6761), so the probe
    // fails; the saved profile must survive untouched.
    static const char *const name = "Ultimate-64-Elite-F83C87.example.invalid";
    c64_device_t saved = {0};
    strcpy(saved.id, "38c1ba");
    strcpy(saved.host, name);
    CHECK(c64_device_registry_upsert(&saved));
    fake_device_t fake = {.mode = FAKE_REPLY, .status = 200, .body = C64U_INFO, .control_enabled = true};
    CHECK(fake_start(&fake));
    const char *const hosts[] = {name};
    const c64_device_scan_test_stats_t stats = scan_fake(&fake, hosts, 1);
    fake_stop(&fake);
    CHECK(stats.probed == 1);
    CHECK(strcmp(stats.first_host, name) == 0);
    const c64_device_t *device = c64_device_registry_get("38c1ba");
    CHECK(device != NULL);
    CHECK(strcmp(device->host, name) == 0);
    return true;
}

static bool test_many_hosts_finish_within_deadline(void)
{
    clear_registry();
    // 254 addresses where only one answers, the others refuse instantly; plus
    // the worker-pool sizing for a multi-interface machine.
    fake_device_t fake = {.mode = FAKE_REPLY, .status = 200, .body = C64U_INFO, .control_enabled = true};
    CHECK(fake_start(&fake));
    static char storage[254][16];
    const char *hosts[254];
    for (int i = 0; i < 254; i++) {
        snprintf(storage[i], sizeof(storage[i]), "127.0.%d.%d", i == 0 ? 0 : 1, i == 0 ? 1 : i);
        hosts[i] = storage[i];
    }
    const c64_device_scan_test_stats_t stats = scan_fake(&fake, hosts, 254);
    fake_stop(&fake);
    CHECK(stats.probed == 254);
    CHECK(c64_device_registry_get("5d0464") != NULL);
    CHECK(stats.elapsed_ms < 12000);
    return true;
}

static bool test_worker_sizing_and_subnet_order(void)
{
    const uint64_t budget = 12ULL * 1000000000ULL;
    CHECK(c64_device_scan_worker_count(10, budget) == 48);
    // Five /24 networks (a LAN plus Docker/libvirt bridges) still fit the deadline.
    const size_t workers = c64_device_scan_worker_count(1024, budget);
    CHECK(workers > 48 && workers <= 128);
    CHECK((1024 + workers - 1) / workers * 650 <= 12000 * 60 / 100 + 650);
    CHECK(c64_device_scan_worker_count(100000, budget) == 128);

    const uint32_t lan = inet_addr("192.168.1.185");
    CHECK(c64_device_scan_subnet_rank(lan, lan) == 0);
    CHECK(c64_device_scan_subnet_rank(inet_addr("172.17.0.1"), lan) == 1);
    CHECK(c64_device_scan_subnet_rank(inet_addr("10.0.3.1"), lan) == 1);
    CHECK(c64_device_scan_subnet_rank(inet_addr("100.64.0.5"), lan) == 2);
    CHECK(c64_device_scan_subnet_rank(inet_addr("169.254.10.2"), lan) == 3);
    CHECK(c64_device_scan_subnet_rank(inet_addr("192.168.1.185"), 0) == 1);
    // The configured host's network is swept first, with the /16 -> /24 clamp.
    CHECK(c64_device_scan_same_subnet(inet_addr("127.0.0.1"), 8, inet_addr("127.0.0.2")));
    CHECK(!c64_device_scan_same_subnet(inet_addr("127.0.0.1"), 8, inet_addr("127.0.1.2")));
    CHECK(c64_device_scan_same_subnet(inet_addr("172.17.0.1"), 16, inet_addr("172.17.0.9")));
    CHECK(!c64_device_scan_same_subnet(inet_addr("192.168.1.185"), 24, inet_addr("192.168.2.5")));
    return true;
}

static bool test_reachability_any_port(void)
{
    // The device-start pre-check: either port answering is enough, all ports
    // dead must fail, and an address nobody answers costs one timeout.
    fake_device_t fake = {.mode = FAKE_REPLY, .status = 200, .body = C64U_INFO, .control_enabled = false};
    CHECK(fake_start(&fake));
    const uint32_t open_then_closed[2] = {fake.http_port, fake.control_port};
    const uint32_t closed_then_open[2] = {fake.control_port, fake.http_port};
    const uint32_t closed[1] = {fake.control_port};
    CHECK(c64_test_connectivity_any("127.0.0.1", open_then_closed, 2, 250));
    CHECK(c64_test_connectivity_any("127.0.0.1", closed_then_open, 2, 250));
    CHECK(!c64_test_connectivity_any("127.0.0.1", closed, 1, 250));
    fake_stop(&fake);
    const uint32_t ports[2] = {80, 64};
    const uint64_t start = os_gettime_ns();
    CHECK(!c64_test_connectivity_any("192.0.2.1", ports, 2, 250)); // TEST-NET-1, never answers
    CHECK(os_gettime_ns() - start < 1000ULL * 1000000ULL);
    CHECK(!c64_test_connectivity_any("name.invalid", ports, 2, 250));
    return true;
}

static bool test_local_interfaces_enumerated(void)
{
    // Exercises getifaddrs() / GetAdaptersAddresses() on the platform running
    // the test. A CI runner always has a network adapter, so an empty list
    // there means interface enumeration is broken on that platform.
    static char hosts[1024][C64_DEVICE_HOST_MAX];
    const size_t count = c64_device_scan_local_hosts_for_test(hosts, 1024);
    printf("local sweep: %zu addresses%s%s\n", count, count ? ", first " : "", count ? hosts[0] : "");
    if (getenv("CI")) {
        CHECK(count > 0);
    }
    for (size_t i = 0; i < count; i++) {
        CHECK(strncmp(hosts[i], "127.", 4) != 0); // loopback is never swept by default
    }
    return true;
}

int main(void)
{
#ifdef _WIN32
    // The device registry lives in the user's real Documents folder on
    // Windows and cannot be redirected, and these tests clear it. Run only
    // where that is harmless (CI runners set CI=true).
    if (!getenv("CI")) {
        puts("skipped: set CI=1 to run (would modify Documents\\obs-studio\\c64stream\\settings)");
        return 0;
    }
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return 1;
    }
#else
    // The fake server writes to clients that may already have timed out.
    signal(SIGPIPE, SIG_IGN);
    // Keep the registry out of the real Documents folder: Linux reads
    // XDG_DOCUMENTS_DIR, macOS $HOME/Documents.
    char root[] = "/tmp/c64-device-scan-hermetic-XXXXXX";
    if (!mkdtemp(root) || setenv("XDG_DOCUMENTS_DIR", root, 1) != 0 || setenv("HOME", root, 1) != 0) {
        return 1;
    }
#endif
    curl_global_init(CURL_GLOBAL_DEFAULT);
    if (!c64_device_registry_init()) {
        return 1;
    }
    clear_registry();
    struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        {"valid_device_is_registered", test_valid_device_is_registered},
        {"control_port_required", test_control_port_required},
        {"foreign_servers_rejected", test_foreign_servers_rejected},
        {"password_protected_device", test_password_protected_device},
        {"unresponsive_host_is_bounded", test_unresponsive_host_is_bounded},
        {"truncated_response_rejected", test_truncated_response_rejected},
        {"oversized_response_accepted", test_oversized_response_accepted},
        {"known_host_survives_slow_first_answer", test_known_host_survives_slow_first_answer},
        {"two_addresses_one_device", test_two_addresses_one_device},
        {"long_hostname_probed_intact", test_long_hostname_probed_intact},
        {"many_hosts_finish_within_deadline", test_many_hosts_finish_within_deadline},
        {"worker_sizing_and_subnet_order", test_worker_sizing_and_subnet_order},
        {"reachability_any_port", test_reachability_any_port},
        {"local_interfaces_enumerated", test_local_interfaces_enumerated},
    };
    int failures = 0;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        const uint64_t start = os_gettime_ns();
        const bool ok = tests[i].run();
        printf("%s %s (%llu ms)\n", ok ? "PASS" : "FAIL", tests[i].name,
               (unsigned long long)((os_gettime_ns() - start) / 1000000ULL));
        failures += ok ? 0 : 1;
    }
    clear_registry();
    curl_global_cleanup();
    return failures ? 1 : 0;
}
