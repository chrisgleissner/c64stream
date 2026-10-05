#include "c64-stream-control.h"
#include "c64-logging.h"
#include "c64-protocol.h"
#include "c64-rest-client.h"
#include "c64-types.h"
#include <string.h>
#include <util/platform.h>

#define C64_STREAM_RETRY_NS (60ULL * 1000000000ULL)
#define STREAM_CONTROL_LOG_PREFIX "STREAM_CONTROL:"

bool c64_stream_control_should_fallback(c64_rest_outcome_t outcome)
{
    return outcome == C64_REST_NOT_SUPPORTED;
}

bool c64_stream_control_to(struct c64_source *context, const char *host, uint32_t control_port, bool enable,
                           uint8_t stream_id, const char *destination)
{
    if (!context || !host) {
        return false;
    }
    // Receive-only sources deliberately have no remote device to control.
    if (!strcmp(host, "0.0.0.0")) {
        return true;
    }

    const c64_stream_transport_t transport = (c64_stream_transport_t)context->stream_control_transport;
    if (transport == C64_STREAM_TRANSPORT_REST && !context->rest_client) {
        return false;
    }
    const uint64_t now = os_gettime_ns();
    const bool try_rest = transport != C64_STREAM_TRANSPORT_LEGACY && context->rest_client &&
                          (transport == C64_STREAM_TRANSPORT_REST || now >= context->stream_rest_demoted_until_ns);
    if (try_rest) {
        c64_rest_outcome_t outcome = C64_REST_UNREACHABLE;
        long status = 0;
        bool ok = enable ? c64_rest_stream_start_with_outcome(context->rest_client, stream_id == 1, destination,
                                                              &outcome, &status)
                         : c64_rest_stream_stop_with_outcome(context->rest_client, stream_id == 1, &outcome, &status);
        if (ok) {
            C64_LOG_INFO("" STREAM_CONTROL_LOG_PREFIX " Stream %u %s via REST", stream_id,
                         enable ? "started" : "stopped");
            return true;
        }
        if (transport == C64_STREAM_TRANSPORT_REST || !c64_stream_control_should_fallback(outcome)) {
            return false;
        }
        context->stream_rest_demoted_until_ns = status == 404 ? UINT64_MAX : now + C64_STREAM_RETRY_NS;
    }

    return c64_send_control_command_to(host, control_port, enable, stream_id, destination);
}

bool c64_stream_control_stop_all_to(struct c64_source *context, const char *host, uint32_t control_port)
{
    // Both stops must be attempted even when the first one fails.
    const bool video_stopped = c64_stream_control_to(context, host, control_port, false, 0, NULL);
    const bool audio_stopped = c64_stream_control_to(context, host, control_port, false, 1, NULL);
    return video_stopped && audio_stopped;
}

bool c64_stream_control(struct c64_source *context, bool enable, uint8_t stream_id, const char *destination)
{
    if (!context) {
        return false;
    }
    return c64_stream_control_to(context, context->ip_address, context->control_port, enable, stream_id, destination);
}
