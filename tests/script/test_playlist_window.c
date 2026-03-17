#include "../../src/c64-playlist-window.h"

#include <stdio.h>

#define CHECK(expr)                                                                                                       \
    do {                                                                                                                  \
        if (!(expr)) {                                                                                                    \
            fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #expr, __FILE__, __LINE__);                                  \
            return 1;                                                                                                     \
        }                                                                                                                 \
    } while (0)

int main(void)
{
    int start = -1;
    int count = -1;

    CHECK(c64_playlist_clamp_window_start(0, 50, 25) == 0);
    CHECK(c64_playlist_clamp_window_start(10, 50, 25) == 0);
    CHECK(c64_playlist_clamp_window_start(50000, 50, -1) == 0);
    CHECK(c64_playlist_clamp_window_start(50000, 50, 25) == 25);
    CHECK(c64_playlist_clamp_window_start(50000, 50, 60000) == 49950);

    c64_playlist_compute_window(10, 4, 50, &start, &count);
    CHECK(start == 0);
    CHECK(count == 10);

    c64_playlist_compute_window(50000, 0, 50, &start, &count);
    CHECK(start == 0);
    CHECK(count == 50);

    c64_playlist_compute_window(50000, 25000, 50, &start, &count);
    CHECK(start == 24975);
    CHECK(count == 50);

    c64_playlist_compute_window(50000, 49999, 50, &start, &count);
    CHECK(start == 49950);
    CHECK(count == 50);

    return 0;
}
