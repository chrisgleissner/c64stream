#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "c64-rest-client.h"
struct c64_source;

typedef enum {
    C64_STREAM_TRANSPORT_AUTO = 0,
    C64_STREAM_TRANSPORT_REST = 1,
    C64_STREAM_TRANSPORT_LEGACY = 2,
} c64_stream_transport_t;

bool c64_stream_control_should_fallback(c64_rest_outcome_t outcome);

bool c64_stream_control_to(struct c64_source *context, const char *host, uint32_t control_port, bool enable,
                           uint8_t stream_id, const char *destination);
bool c64_stream_control(struct c64_source *context, bool enable, uint8_t stream_id, const char *destination);
