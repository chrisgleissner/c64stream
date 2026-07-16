/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#ifndef C64_DIMENSIONS_H
#define C64_DIMENSIONS_H

#include <pthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * C64STR-008: coherent publication of the video frame width/height pair.
 *
 * A PAL<->NTSC format change updates width and height on the video processor
 * thread while the graphics thread reads them to size/upload the render
 * texture. Writing or reading the two fields separately can hand the graphics
 * thread a torn pair (new height with old width, or a height that changes
 * between the texture-size decision and the upload stride). These helpers
 * publish and snapshot both fields together under a caller-provided lock (the
 * source's assembly_mutex), so a reader always observes one coherent pair.
 *
 * The helpers operate directly on the source's live width/height fields so the
 * production code and its regression test share the exact same synchronisation
 * primitive.
 */

/** Publish (width, height) as one coherent pair under @p lock. */
void c64_dimensions_publish(pthread_mutex_t *lock, uint32_t *width_field, uint32_t *height_field, uint32_t width,
                            uint32_t height);

/** Snapshot (width, height) as one coherent pair under @p lock. */
void c64_dimensions_snapshot(pthread_mutex_t *lock, const uint32_t *width_field, const uint32_t *height_field,
                             uint32_t *width_out, uint32_t *height_out);

#ifdef __cplusplus
}
#endif

#endif // C64_DIMENSIONS_H
