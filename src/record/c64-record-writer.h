/*
C64 Stream - asynchronous recording writer (C64CLK-005)
*/
#ifndef C64_RECORD_WRITER_H
#define C64_RECORD_WRITER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct c64_source;

enum c64_record_write_stream {
    C64_RECORD_WRITE_AUDIO = 0,
    C64_RECORD_WRITE_VIDEO,
};

bool c64_record_writer_start(struct c64_source *context);
void c64_record_writer_stop(struct c64_source *context);
bool c64_record_writer_enqueue(struct c64_source *context, enum c64_record_write_stream stream, const uint8_t *data,
                               size_t size);
bool c64_record_writer_enqueue_avi_frame(struct c64_source *context, const uint8_t *bgr, size_t frame_size);

#endif
