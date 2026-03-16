/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-playlist-window.h"

void c64_playlist_compute_window(int playlist_count, int focus_index, int window_limit, int *window_start,
                                 int *window_count)
{
    int start = 0;
    int count = (playlist_count > 0) ? playlist_count : 0;

    if (playlist_count > 0 && window_limit > 0 && playlist_count > window_limit) {
        count = window_limit;
        start = focus_index - (window_limit / 2);
        if (start < 0) {
            start = 0;
        }
        if (start + count > playlist_count) {
            start = playlist_count - count;
        }
    }

    if (window_start) {
        *window_start = start;
    }
    if (window_count) {
        *window_count = count;
    }
}
