/*
C64 Stream - asynchronous recording writer (C64CLK-005)
*/
#include "c64-record-writer.h"

#include <obs-module.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "c64-logging.h"
#include "c64-record-audio.h"
#include "c64-types.h"

#define C64_RECORD_WRITER_MAX_BYTES (8U * 1024U * 1024U)

struct c64_record_write_node {
    struct c64_record_write_node *next;
    enum c64_record_write_stream stream;
    size_t size;
    uint8_t data[];
};

struct c64_record_writer {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t ready;
    struct c64_record_write_node *head;
    struct c64_record_write_node *tail;
    size_t queued_bytes;
    bool stopping;
    bool running;
    struct c64_source *context;
};

static void record_write_drop(struct c64_source *context)
{
    os_atomic_inc_long(&context->record_write_drops);
}

static void *c64_record_writer_thread(void *opaque)
{
    struct c64_record_writer *writer = opaque;
    struct c64_source *context = writer->context;

    for (;;) {
        pthread_mutex_lock(&writer->mutex);
        while (!writer->head && !writer->stopping) {
            pthread_cond_wait(&writer->ready, &writer->mutex);
        }
        if (!writer->head && writer->stopping) {
            pthread_mutex_unlock(&writer->mutex);
            break;
        }
        struct c64_record_write_node *node = writer->head;
        writer->head = node->next;
        if (!writer->head) {
            writer->tail = NULL;
        }
        writer->queued_bytes -= node->size;
        pthread_mutex_unlock(&writer->mutex);

        FILE *file = node->stream == C64_RECORD_WRITE_AUDIO ? context->audio_file : context->video_file;
        const size_t written = file ? fwrite(node->data, 1, node->size, file) : 0;
        if (written != node->size) {
            record_write_drop(context);
            C64_LOG_WARNING("" RECORD_LOG_PREFIX " Background recording write failed (%zu/%zu bytes)", written,
                            node->size);
        } else if (node->stream == C64_RECORD_WRITE_AUDIO) {
            context->recorded_audio_bytes += written;
            const long frames = (long)(written / 4);
            long old_total;
            long new_total;
            do {
                old_total = os_atomic_load_long(&context->recorded_audio_samples);
                new_total = old_total + frames;
            } while (!os_atomic_compare_swap_long(&context->recorded_audio_samples, old_total, new_total));
        }
        free(node);
    }

    return NULL;
}

bool c64_record_writer_start(struct c64_source *context)
{
    if (!context || context->record_writer) {
        return context && context->record_writer;
    }

    struct c64_record_writer *writer = calloc(1, sizeof(*writer));
    if (!writer) {
        return false;
    }
    writer->context = context;
    if (pthread_mutex_init(&writer->mutex, NULL) != 0) {
        free(writer);
        return false;
    }
    if (pthread_cond_init(&writer->ready, NULL) != 0) {
        pthread_mutex_destroy(&writer->mutex);
        free(writer);
        return false;
    }
    if (pthread_create(&writer->thread, NULL, c64_record_writer_thread, writer) != 0) {
        pthread_cond_destroy(&writer->ready);
        pthread_mutex_destroy(&writer->mutex);
        free(writer);
        return false;
    }
    writer->running = true;
    context->record_writer = writer;
    return true;
}

void c64_record_writer_stop(struct c64_source *context)
{
    struct c64_record_writer *writer = context ? context->record_writer : NULL;
    if (!writer) {
        return;
    }

    pthread_mutex_lock(&writer->mutex);
    writer->stopping = true;
    pthread_cond_signal(&writer->ready);
    pthread_mutex_unlock(&writer->mutex);
    (void)pthread_join(writer->thread, NULL);

    pthread_cond_destroy(&writer->ready);
    pthread_mutex_destroy(&writer->mutex);
    free(writer);
    context->record_writer = NULL;
}

bool c64_record_writer_enqueue(struct c64_source *context, enum c64_record_write_stream stream, const uint8_t *data,
                               size_t size)
{
    struct c64_record_writer *writer = context ? context->record_writer : NULL;
    if (!writer || !data || size == 0) {
        return false;
    }

    struct c64_record_write_node *node = malloc(sizeof(*node) + size);
    if (!node) {
        record_write_drop(context);
        return false;
    }
    node->next = NULL;
    node->stream = stream;
    node->size = size;
    memcpy(node->data, data, size);

    pthread_mutex_lock(&writer->mutex);
    if (writer->stopping || size > C64_RECORD_WRITER_MAX_BYTES ||
        writer->queued_bytes > C64_RECORD_WRITER_MAX_BYTES - size) {
        pthread_mutex_unlock(&writer->mutex);
        free(node);
        record_write_drop(context);
        return false;
    }
    if (writer->tail) {
        writer->tail->next = node;
    } else {
        writer->head = node;
    }
    writer->tail = node;
    writer->queued_bytes += size;
    pthread_cond_signal(&writer->ready);
    pthread_mutex_unlock(&writer->mutex);
    return true;
}

bool c64_record_writer_enqueue_avi_frame(struct c64_source *context, const uint8_t *bgr, size_t frame_size)
{
    const size_t padded = frame_size + (frame_size & 1U);
    const size_t size = 8U + padded;
    uint8_t *chunk = malloc(size);
    if (!chunk) {
        record_write_drop(context);
        return false;
    }
    memcpy(chunk, "00db", 4);
    const uint32_t chunk_size = (uint32_t)frame_size;
    memcpy(chunk + 4, &chunk_size, sizeof(chunk_size));
    memcpy(chunk + 8, bgr, frame_size);
    if (frame_size & 1U) {
        chunk[8 + frame_size] = 0;
    }
    const bool queued = c64_record_writer_enqueue(context, C64_RECORD_WRITE_VIDEO, chunk, size);
    free(chunk);
    return queued;
}
