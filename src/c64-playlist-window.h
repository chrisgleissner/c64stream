/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#pragma once

void c64_playlist_compute_window(int playlist_count, int focus_index, int window_limit, int *window_start,
                                 int *window_count);
