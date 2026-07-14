/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#ifdef NDEBUG
#undef NDEBUG
#endif

#include "c64-rest-client.h"

#include <assert.h>
#include <stdio.h>

// Provided by the plugin; redefined here so the test links without plugin-main.c.
bool c64_debug_logging = false;

#define TEST(name) static void name(void)
#define RUN_TEST(name)                                                                                                 \
    do {                                                                                                               \
        printf("Running test: %s ... ", #name);                                                                        \
        name();                                                                                                        \
        printf("OK\n");                                                                                                \
    } while (0)

TEST(classify_success_codes)
{
    assert(c64_rest_classify_status(200) == C64_REST_OK);
    assert(c64_rest_classify_status(201) == C64_REST_OK);
    assert(c64_rest_classify_status(204) == C64_REST_OK);
    assert(c64_rest_classify_status(299) == C64_REST_OK);
}

TEST(classify_bad_request)
{
    assert(c64_rest_classify_status(400) == C64_REST_BAD_REQUEST);
}

TEST(classify_auth_refusals_never_fallback)
{
    // 401/403 must map to FORBIDDEN so callers never fall back to the
    // unauthenticated legacy transport (constraint: 403 never triggers fallback).
    assert(c64_rest_classify_status(401) == C64_REST_FORBIDDEN);
    assert(c64_rest_classify_status(403) == C64_REST_FORBIDDEN);
}

TEST(classify_not_supported_codes)
{
    assert(c64_rest_classify_status(404) == C64_REST_NOT_SUPPORTED);
    assert(c64_rest_classify_status(501) == C64_REST_NOT_SUPPORTED);
}

TEST(classify_other_server_errors_are_surfaced)
{
    assert(c64_rest_classify_status(500) == C64_REST_SERVER_ERROR);
    assert(c64_rest_classify_status(502) == C64_REST_SERVER_ERROR);
    assert(c64_rest_classify_status(405) == C64_REST_SERVER_ERROR);
    assert(c64_rest_classify_status(429) == C64_REST_SERVER_ERROR);
}

TEST(classify_transport_failure)
{
    // status 0 = no HTTP response received (connection refused / timeout).
    assert(c64_rest_classify_status(0) == C64_REST_UNREACHABLE);
}

TEST(accessors_null_safe)
{
    assert(c64_rest_get_last_status(NULL) == 0);
    assert(c64_rest_get_last_outcome(NULL) == C64_REST_UNREACHABLE);
}

int main(void)
{
    RUN_TEST(classify_success_codes);
    RUN_TEST(classify_bad_request);
    RUN_TEST(classify_auth_refusals_never_fallback);
    RUN_TEST(classify_not_supported_codes);
    RUN_TEST(classify_other_server_errors_are_surfaced);
    RUN_TEST(classify_transport_failure);
    RUN_TEST(accessors_null_safe);
    return 0;
}
