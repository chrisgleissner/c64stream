/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#ifdef NDEBUG
#undef NDEBUG
#endif

#include "c64-stream-control.h"
#include "c64-rest-client.h"
#include "c64-types.h"

#include <util/platform.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

// Provided by the plugin; redefined here so the test links without plugin-main.c.
bool c64_debug_logging = false;

#define TEST(name) static void name(void)
#define RUN_TEST(name)                                                                                               \
    do {                                                                                                             \
        printf("Running test: %s ... ", #name);                                                                     \
        name();                                                                                                      \
        printf("OK\n");                                                                                              \
    } while (0)

// --- REST client stub state, controlled per-test. ---

static struct {
    int start_calls;
    int stop_calls;
    bool start_ok;
    bool stop_ok;
    c64_rest_outcome_t start_outcome;
    c64_rest_outcome_t stop_outcome;
    long start_status;
    long stop_status;
    bool last_start_audio;
    bool last_stop_audio;
    char last_destination[256];
} g_rest;

static struct {
    int calls;
    char last_host[64];
    uint32_t last_port;
    bool last_enable;
    uint8_t last_stream_id;
    char last_destination[256];
} g_legacy;

static int g_dummy_client_marker;
static c64_rest_client_t *const kDummyClient = (c64_rest_client_t *)&g_dummy_client_marker;

static void reset_stubs(void)
{
    memset(&g_rest, 0, sizeof(g_rest));
    memset(&g_legacy, 0, sizeof(g_legacy));
}

bool c64_rest_stream_start_with_outcome(c64_rest_client_t *client, bool audio, const char *destination,
                                        c64_rest_outcome_t *outcome, long *status)
{
    (void)client;
    g_rest.start_calls++;
    g_rest.last_start_audio = audio;
    if (destination) {
        snprintf(g_rest.last_destination, sizeof(g_rest.last_destination), "%s", destination);
    }
    if (outcome) {
        *outcome = g_rest.start_outcome;
    }
    if (status) {
        *status = g_rest.start_status;
    }
    return g_rest.start_ok;
}

bool c64_rest_stream_stop_with_outcome(c64_rest_client_t *client, bool audio, c64_rest_outcome_t *outcome, long *status)
{
    (void)client;
    g_rest.stop_calls++;
    g_rest.last_stop_audio = audio;
    if (outcome) {
        *outcome = g_rest.stop_outcome;
    }
    if (status) {
        *status = g_rest.stop_status;
    }
    return g_rest.stop_ok;
}

void c64_send_control_command_to(const char *host, uint32_t control_port, bool enable, uint8_t stream_id,
                                 const char *destination)
{
    g_legacy.calls++;
    if (host) {
        snprintf(g_legacy.last_host, sizeof(g_legacy.last_host), "%s", host);
    }
    g_legacy.last_port = control_port;
    g_legacy.last_enable = enable;
    g_legacy.last_stream_id = stream_id;
    if (destination) {
        snprintf(g_legacy.last_destination, sizeof(g_legacy.last_destination), "%s", destination);
    }
}

// --- Negotiation table: c64_stream_control_should_fallback, all outcomes. ---

TEST(should_fallback_only_for_not_supported)
{
    assert(!c64_stream_control_should_fallback(C64_REST_OK));
    assert(c64_stream_control_should_fallback(C64_REST_NOT_SUPPORTED));
    assert(!c64_stream_control_should_fallback(C64_REST_FORBIDDEN));
    assert(!c64_stream_control_should_fallback(C64_REST_BAD_REQUEST));
    assert(!c64_stream_control_should_fallback(C64_REST_UNREACHABLE));
    assert(!c64_stream_control_should_fallback(C64_REST_SERVER_ERROR));
}

// --- c64_stream_control_to negotiation policy. ---

TEST(rest_success_never_falls_back)
{
    reset_stubs();
    struct c64_source ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.stream_control_transport = C64_STREAM_TRANSPORT_AUTO;
    ctx.rest_client = kDummyClient;
    g_rest.start_ok = true;
    g_rest.start_outcome = C64_REST_OK;

    bool ok = c64_stream_control_to(&ctx, "1.2.3.4", 64, true, 1, "5.6.7.8:12345");

    assert(ok);
    assert(g_rest.start_calls == 1);
    assert(g_rest.last_start_audio); // stream_id == 1 => audio
    assert(strcmp(g_rest.last_destination, "5.6.7.8:12345") == 0);
    assert(g_legacy.calls == 0);
}

TEST(not_supported_404_demotes_permanently_and_falls_back)
{
    reset_stubs();
    struct c64_source ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.stream_control_transport = C64_STREAM_TRANSPORT_AUTO;
    ctx.rest_client = kDummyClient;
    g_rest.start_ok = false;
    g_rest.start_outcome = C64_REST_NOT_SUPPORTED;
    g_rest.start_status = 404;

    bool ok = c64_stream_control_to(&ctx, "1.2.3.4", 64, true, 0, "5.6.7.8:12345");

    assert(ok); // legacy fallback succeeds
    assert(g_rest.start_calls == 1);
    assert(g_legacy.calls == 1);
    assert(g_legacy.last_enable); // enable was true
    assert(g_legacy.last_stream_id == 0);
    assert(ctx.stream_rest_demoted_until_ns == UINT64_MAX); // 404 = permanent demotion
}

TEST(not_supported_501_demotes_with_expiry_and_falls_back)
{
    reset_stubs();
    struct c64_source ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.stream_control_transport = C64_STREAM_TRANSPORT_AUTO;
    ctx.rest_client = kDummyClient;
    g_rest.start_ok = false;
    g_rest.start_outcome = C64_REST_NOT_SUPPORTED;
    g_rest.start_status = 501;

    uint64_t before = os_gettime_ns();
    bool ok = c64_stream_control_to(&ctx, "1.2.3.4", 64, true, 0, "dest");
    uint64_t after = os_gettime_ns();

    assert(ok);
    assert(g_legacy.calls == 1);
    // 501 demotes with a ~60s expiry, not permanently.
    assert(ctx.stream_rest_demoted_until_ns != UINT64_MAX);
    assert(ctx.stream_rest_demoted_until_ns >= before + 59ULL * 1000000000ULL);
    assert(ctx.stream_rest_demoted_until_ns <= after + 61ULL * 1000000000ULL);
}

TEST(forbidden_403_never_falls_back)
{
    reset_stubs();
    struct c64_source ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.stream_control_transport = C64_STREAM_TRANSPORT_AUTO;
    ctx.rest_client = kDummyClient;
    g_rest.start_ok = false;
    g_rest.start_outcome = C64_REST_FORBIDDEN;
    g_rest.start_status = 403;

    bool ok = c64_stream_control_to(&ctx, "1.2.3.4", 64, true, 0, "dest");

    // Constraint: 403 must never trigger a fallback (auth bypass).
    assert(!ok);
    assert(g_rest.start_calls == 1);
    assert(g_legacy.calls == 0);
    assert(ctx.stream_rest_demoted_until_ns == 0);
}

TEST(unreachable_never_falls_back)
{
    reset_stubs();
    struct c64_source ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.stream_control_transport = C64_STREAM_TRANSPORT_AUTO;
    ctx.rest_client = kDummyClient;
    g_rest.start_ok = false;
    g_rest.start_outcome = C64_REST_UNREACHABLE;
    g_rest.start_status = 0;

    bool ok = c64_stream_control_to(&ctx, "1.2.3.4", 64, true, 0, "dest");

    assert(!ok);
    assert(g_legacy.calls == 0);
    assert(ctx.stream_rest_demoted_until_ns == 0);
}

TEST(bad_request_never_falls_back)
{
    reset_stubs();
    struct c64_source ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.stream_control_transport = C64_STREAM_TRANSPORT_AUTO;
    ctx.rest_client = kDummyClient;
    g_rest.start_ok = false;
    g_rest.start_outcome = C64_REST_BAD_REQUEST;
    g_rest.start_status = 400;

    bool ok = c64_stream_control_to(&ctx, "1.2.3.4", 64, true, 0, "dest");

    assert(!ok);
    assert(g_legacy.calls == 0);
    assert(ctx.stream_rest_demoted_until_ns == 0);
}

TEST(server_error_never_falls_back)
{
    reset_stubs();
    struct c64_source ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.stream_control_transport = C64_STREAM_TRANSPORT_AUTO;
    ctx.rest_client = kDummyClient;
    g_rest.start_ok = false;
    g_rest.start_outcome = C64_REST_SERVER_ERROR;
    g_rest.start_status = 500;

    bool ok = c64_stream_control_to(&ctx, "1.2.3.4", 64, true, 0, "dest");

    assert(!ok);
    assert(g_legacy.calls == 0);
    assert(ctx.stream_rest_demoted_until_ns == 0);
}

TEST(forced_legacy_never_tries_rest)
{
    reset_stubs();
    struct c64_source ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.stream_control_transport = C64_STREAM_TRANSPORT_LEGACY;
    ctx.rest_client = kDummyClient;

    bool ok = c64_stream_control_to(&ctx, "1.2.3.4", 64, true, 0, "dest");

    assert(ok);
    assert(g_rest.start_calls == 0);
    assert(g_legacy.calls == 1);
}

TEST(forced_rest_never_falls_back_even_when_fallback_eligible)
{
    reset_stubs();
    struct c64_source ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.stream_control_transport = C64_STREAM_TRANSPORT_REST;
    ctx.rest_client = kDummyClient;
    g_rest.start_ok = false;
    g_rest.start_outcome = C64_REST_NOT_SUPPORTED; // would normally be fallback-eligible
    g_rest.start_status = 404;

    bool ok = c64_stream_control_to(&ctx, "1.2.3.4", 64, true, 0, "dest");

    assert(!ok);
    assert(g_rest.start_calls == 1);
    assert(g_legacy.calls == 0);
    assert(ctx.stream_rest_demoted_until_ns == 0); // never recorded; forced REST short-circuits first
}

TEST(permanent_demotion_skips_rest_on_next_call)
{
    reset_stubs();
    struct c64_source ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.stream_control_transport = C64_STREAM_TRANSPORT_AUTO;
    ctx.rest_client = kDummyClient;
    ctx.stream_rest_demoted_until_ns = UINT64_MAX;

    bool ok = c64_stream_control_to(&ctx, "1.2.3.4", 64, true, 0, "dest");

    assert(ok);
    assert(g_rest.start_calls == 0);
    assert(g_legacy.calls == 1);
}

TEST(expiry_demotion_retries_rest_after_expiry)
{
    reset_stubs();
    struct c64_source ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.stream_control_transport = C64_STREAM_TRANSPORT_AUTO;
    ctx.rest_client = kDummyClient;
    ctx.stream_rest_demoted_until_ns = os_gettime_ns() - 1; // already expired
    g_rest.start_ok = true;

    bool ok = c64_stream_control_to(&ctx, "1.2.3.4", 64, true, 0, "dest");

    assert(ok);
    assert(g_rest.start_calls == 1);
    assert(g_legacy.calls == 0);
}

TEST(no_rest_client_goes_straight_to_legacy)
{
    reset_stubs();
    struct c64_source ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.stream_control_transport = C64_STREAM_TRANSPORT_AUTO;
    ctx.rest_client = NULL;

    bool ok = c64_stream_control_to(&ctx, "1.2.3.4", 64, true, 0, "dest");

    assert(ok);
    assert(g_rest.start_calls == 0);
    assert(g_legacy.calls == 1);
}

TEST(stop_uses_stop_with_outcome_and_audio_flag)
{
    reset_stubs();
    struct c64_source ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.stream_control_transport = C64_STREAM_TRANSPORT_AUTO;
    ctx.rest_client = kDummyClient;
    g_rest.stop_ok = true;

    bool ok = c64_stream_control_to(&ctx, "1.2.3.4", 64, false, 1, NULL);

    assert(ok);
    assert(g_rest.stop_calls == 1);
    assert(g_rest.start_calls == 0);
    assert(g_rest.last_stop_audio); // stream_id == 1 => audio
    assert(g_legacy.calls == 0);
}

TEST(null_context_or_host_returns_false)
{
    reset_stubs();
    struct c64_source ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.rest_client = kDummyClient;

    assert(!c64_stream_control_to(NULL, "1.2.3.4", 64, true, 0, "dest"));
    assert(!c64_stream_control_to(&ctx, NULL, 64, true, 0, "dest"));
    assert(g_rest.start_calls == 0);
    assert(g_legacy.calls == 0);
}

TEST(wrapper_reads_host_and_port_from_context)
{
    reset_stubs();
    struct c64_source ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.stream_control_transport = C64_STREAM_TRANSPORT_LEGACY;
    snprintf(ctx.ip_address, sizeof(ctx.ip_address), "%s", "192.168.1.64");
    ctx.control_port = 64;

    bool ok = c64_stream_control(&ctx, true, 0, "dest");

    assert(ok);
    assert(g_legacy.calls == 1);
    assert(strcmp(g_legacy.last_host, "192.168.1.64") == 0);
    assert(g_legacy.last_port == 64);
}

TEST(wrapper_null_context_returns_false)
{
    reset_stubs();
    assert(!c64_stream_control(NULL, true, 0, "dest"));
    assert(g_rest.start_calls == 0);
    assert(g_legacy.calls == 0);
}

int main(void)
{
    RUN_TEST(should_fallback_only_for_not_supported);
    RUN_TEST(rest_success_never_falls_back);
    RUN_TEST(not_supported_404_demotes_permanently_and_falls_back);
    RUN_TEST(not_supported_501_demotes_with_expiry_and_falls_back);
    RUN_TEST(forbidden_403_never_falls_back);
    RUN_TEST(unreachable_never_falls_back);
    RUN_TEST(bad_request_never_falls_back);
    RUN_TEST(server_error_never_falls_back);
    RUN_TEST(forced_legacy_never_tries_rest);
    RUN_TEST(forced_rest_never_falls_back_even_when_fallback_eligible);
    RUN_TEST(permanent_demotion_skips_rest_on_next_call);
    RUN_TEST(expiry_demotion_retries_rest_after_expiry);
    RUN_TEST(no_rest_client_goes_straight_to_legacy);
    RUN_TEST(stop_uses_stop_with_outcome_and_audio_flag);
    RUN_TEST(null_context_or_host_returns_false);
    RUN_TEST(wrapper_reads_host_and_port_from_context);
    RUN_TEST(wrapper_null_context_returns_false);
    return 0;
}
