#include "c64-stream-control.h"
#include "c64-protocol.h"
#include "c64-rest-client.h"
#include "c64-types.h"
#include <util/platform.h>

#define C64_STREAM_RETRY_NS (60ULL * 1000000000ULL)

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

    const c64_stream_transport_t transport = (c64_stream_transport_t)context->stream_control_transport;
    const uint64_t now = os_gettime_ns();
    const bool try_rest = transport != C64_STREAM_TRANSPORT_LEGACY && context->rest_client &&
                          (transport == C64_STREAM_TRANSPORT_REST || now >= context->stream_rest_demoted_until_ns);
    if (try_rest) {
        bool ok = enable ? c64_rest_stream_start(context->rest_client, stream_id == 1, destination)
                         : c64_rest_stream_stop(context->rest_client, stream_id == 1);
        if (ok) {
            return true;
        }
        c64_rest_outcome_t outcome = c64_rest_get_last_outcome(context->rest_client);
        if (transport == C64_STREAM_TRANSPORT_REST || !c64_stream_control_should_fallback(outcome)) {
            return false;
        }
        const long status = c64_rest_get_last_status(context->rest_client);
        context->stream_rest_demoted_until_ns = status == 404 ? UINT64_MAX : now + C64_STREAM_RETRY_NS;
    }

    c64_send_control_command_to(host, control_port, enable, stream_id, destination);
    return true;
}

bool c64_stream_control(struct c64_source *context, bool enable, uint8_t stream_id, const char *destination)
{
    if (!context) {
        return false;
    }
    return c64_stream_control_to(context, context->ip_address, context->control_port, enable, stream_id, destination);
}
