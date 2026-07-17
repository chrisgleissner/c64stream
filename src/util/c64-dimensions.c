/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#include "c64-dimensions.h"

void c64_dimensions_publish(pthread_mutex_t *lock, uint32_t *width_field, uint32_t *height_field, uint32_t width,
                            uint32_t height)
{
    if (!lock || !width_field || !height_field) {
        return;
    }
    pthread_mutex_lock(lock);
    *width_field = width;
    *height_field = height;
    pthread_mutex_unlock(lock);
}

void c64_dimensions_snapshot(pthread_mutex_t *lock, const uint32_t *width_field, const uint32_t *height_field,
                             uint32_t *width_out, uint32_t *height_out)
{
    if (!lock || !width_field || !height_field) {
        return;
    }
    pthread_mutex_lock(lock);
    if (width_out) {
        *width_out = *width_field;
    }
    if (height_out) {
        *height_out = *height_field;
    }
    pthread_mutex_unlock(lock);
}
