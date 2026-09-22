#include "c64-stream-control.h"
#include "c64-logging.h"
#include "c64-protocol.h"
#include "c64-rest-client.h"
#include "c64-types.h"
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

    const c64_stream_transport_t transport = (c64_stream_transport_t)context->stream_control_transport;
    const uint64_t now = os_gettime_ns();
    const bool wants_palette = enable && stream_id == 0 && os_atomic_load_bool(&context->follow_device_palette);
    if (wants_palette) {
        // Every video (re)start may follow a firmware reboot, which restarts the palette
        // generation counter. Drop only the ordering baseline so the last complete LUT
        // survives until the first palette packet of the new stream arrives.
        pthread_mutex_lock(&context->palette_mutex);
        context->device_palette.ordering_valid = false;
        pthread_mutex_unlock(&context->palette_mutex);
    }
    const bool try_rest = transport != C64_STREAM_TRANSPORT_LEGACY && context->rest_client &&
                          (transport == C64_STREAM_TRANSPORT_REST || now >= context->stream_rest_demoted_until_ns);
    if (try_rest) {
        c64_rest_outcome_t outcome = C64_REST_UNREACHABLE;
        long status = 0;
        const bool palette = wants_palette && os_atomic_load_bool(&context->device_palette_request_supported);
        bool ok = enable ? c64_rest_stream_start_with_outcome(context->rest_client, stream_id == 1, destination,
                                                              palette, &outcome, &status)
                         : c64_rest_stream_stop_with_outcome(context->rest_client, stream_id == 1, &outcome, &status);
        if (!ok && enable && palette && outcome == C64_REST_BAD_REQUEST) {
            C64_LOG_WARNING("" STREAM_CONTROL_LOG_PREFIX
                            " Device rejected runtime palette packets; starting video without them");
            os_atomic_set_bool(&context->device_palette_request_supported, false);
            os_atomic_set_long(&context->device_palette_status, C64_DEVICE_PALETTE_UNSUPPORTED);
            ok = c64_rest_stream_start_with_outcome(context->rest_client, false, destination, false, &outcome, &status);
        }
        if (ok) {
            if (palette && os_atomic_load_bool(&context->device_palette_request_supported)) {
                os_atomic_set_long(&context->device_palette_status, C64_DEVICE_PALETTE_REQUESTED);
            }
            C64_LOG_INFO("" STREAM_CONTROL_LOG_PREFIX " Stream %u %s via REST", stream_id,
                         enable ? "started" : "stopped");
            return true;
        }
        if (transport == C64_STREAM_TRANSPORT_REST || !c64_stream_control_should_fallback(outcome)) {
            return false;
        }
        context->stream_rest_demoted_until_ns = status == 404 ? UINT64_MAX : now + C64_STREAM_RETRY_NS;
    }

    if (wants_palette) {
        if (os_atomic_load_long(&context->device_palette_status) != C64_DEVICE_PALETTE_UNSUPPORTED) {
            C64_LOG_WARNING("" STREAM_CONTROL_LOG_PREFIX
                            " Runtime device palettes require REST stream control; continuing without them");
        }
        os_atomic_set_long(&context->device_palette_status, C64_DEVICE_PALETTE_UNSUPPORTED);
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
