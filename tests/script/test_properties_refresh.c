#include "../../src/c64-properties-refresh.h"
#include "../../src/c64-types.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expr)                                                                                                       \
    do {                                                                                                                  \
        if (!(expr)) {                                                                                                    \
            fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #expr, __FILE__, __LINE__);                                  \
            return 1;                                                                                                     \
        }                                                                                                                 \
    } while (0)

int main(void)
{
    struct c64_source context;
    memset(&context, 0, sizeof(context));

    CHECK(c64_properties_should_request_playlist_rebuild(&context));

    c64_properties_mark_script_ui_refresh(&context);
    CHECK(context.force_ui_update);
    CHECK(context.playlist_refresh_suppressed_once);

    CHECK(!c64_properties_should_request_playlist_rebuild(&context));
    CHECK(!context.playlist_refresh_suppressed_once);
    CHECK(c64_properties_should_request_playlist_rebuild(&context));

    return 0;
}
