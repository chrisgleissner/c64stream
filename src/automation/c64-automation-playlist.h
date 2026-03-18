/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#pragma once

#include "c64-automation.h"

/**
 * Refresh the playlist without starting playback
 * @param selected_index Preferred selected index for display/start
 * @param force_rebuild True to invalidate and rebuild the cached playlist even when the config is unchanged
 * @return true if refreshed successfully
 */
bool c64_automation_refresh_playlist(c64_automation_t *automation, const c64_automation_config_t *config,
                                     int selected_index, bool force_rebuild);

/**
 * Clear any cached playlist entries
 */
void c64_automation_clear_playlist(c64_automation_t *automation);

/**
 * Get current playlist size
 * @return Number of files in playlist, or 0 if not running
 */
int c64_automation_get_playlist_count(c64_automation_t *automation);

/**
 * Get playlist item at index
 * @param index Index into playlist (0-based)
 * @return File path or NULL if index out of range
 */
const char *c64_automation_get_playlist_item(c64_automation_t *automation, int index);
