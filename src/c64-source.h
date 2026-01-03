/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_SOURCE_H
#define C64_SOURCE_H

#include <obs-module.h>

// Forward declarations
struct c64_source;

// OBS source interface functions
void *c64_create(obs_data_t *settings, obs_source_t *source);
void c64_destroy(void *data);
void c64_update(void *data, obs_data_t *settings);
void c64_video_tick(void *data, float seconds);
void c64_video_render(void *data, gs_effect_t *effect);
uint32_t c64_get_width(void *data);
uint32_t c64_get_height(void *data);
const char *c64_get_name(void *unused);
obs_properties_t *c64_properties(void *data);
void c64_defaults(obs_data_t *settings);

// Interaction callbacks for keyboard/mouse capture
void c64_mouse_click(void *data, const struct obs_mouse_event *event, int32_t type, bool mouse_up,
                     uint32_t click_count);
void c64_mouse_move(void *data, const struct obs_mouse_event *event, bool mouse_leave);
void c64_mouse_wheel(void *data, const struct obs_mouse_event *event, int x_delta, int y_delta);
void c64_focus(void *data, bool focus);
void c64_key_click(void *data, const struct obs_key_event *event, bool key_up);

// Streaming control functions
void c64_start_streaming(struct c64_source *context);
void c64_stop_streaming(struct c64_source *context);
void c64_async_retry_task(void *data);
void c64_schedule_retry_task(struct c64_source *context, const char *reason);

// Access functions for internal state (used by macro executor)
void *c64_source_get_rest_client(struct c64_source *context);
void *c64_source_get_keyboard(struct c64_source *context);

#endif // C64_SOURCE_H
