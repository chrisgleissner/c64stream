/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_LOGO_H
#define C64_LOGO_H

#include <stdint.h>
#include <stdbool.h>
#include <obs-module.h>
#include <graphics/graphics.h>

// Forward declarations
struct c64_source;

// Logo management functions
bool c64_logo_init(struct c64_source *context);
void c64_logo_cleanup(struct c64_source *context);
void c64_logo_render_to_frame(struct c64_source *context, uint64_t timestamp_ns);
bool c64_logo_is_available(struct c64_source *context);

// Format preference functions
void c64_logo_set_format_preference(struct c64_source *context, bool prefer_pal);

#endif // C64_LOGO_H
