/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-av-sync.h"

#include <inttypes.h>
#include <pthread.h>
#include <stddef.h>
#include <time.h>

#include "c64-logging.h"
#include "c64-audio.h"
#include "c64-video.h"
#include "c64-types.h"

#define C64_AV_SYNC_DEBOUNCE_NS 100000000ULL          // 100ms
#define C64_AV_SYNC_MATCH_WINDOW_NS 200000000ULL      // 200ms
#define C64_AV_SYNC_EXPIRE_UNMATCHED_NS 2000000000ULL // 2s

static void c64_av_sync_format_wall_clock(char *buf, size_t buf_len)
{
    uint64_t wall_clock_ms_total = c64_get_millis();
    time_t wall_clock_sec = (time_t)(wall_clock_ms_total / 1000ULL);
    uint32_t wall_clock_ms = (uint32_t)(wall_clock_ms_total % 1000ULL);

    struct tm wall_clock_tm;
#ifdef _WIN32
    localtime_s(&wall_clock_tm, &wall_clock_sec);
#else
    localtime_r(&wall_clock_sec, &wall_clock_tm);
#endif

    (void)snprintf(buf, buf_len, "%04d-%02d-%02d_%02d:%02d:%02d.%03u", wall_clock_tm.tm_year + 1900,
                   wall_clock_tm.tm_mon + 1, wall_clock_tm.tm_mday, wall_clock_tm.tm_hour, wall_clock_tm.tm_min,
                   wall_clock_tm.tm_sec, wall_clock_ms);
}

static void prune_used_or_expired(struct c64_av_sync_event *events, size_t *count, uint64_t now_ns)
{
    size_t write_idx = 0;
    for (size_t i = 0; i < *count; i++) {
        const bool is_used = events[i].used;
        const bool is_expired = (now_ns > events[i].ts) && ((now_ns - events[i].ts) > C64_AV_SYNC_EXPIRE_UNMATCHED_NS);

        if (!is_used && !is_expired) {
            if (write_idx != i) {
                events[write_idx] = events[i];
            }
            write_idx++;
        }
    }
    *count = write_idx;
}

static void push_event(struct c64_av_sync_event *events, size_t *count, struct c64_av_sync_event ev)
{
    if (*count < C64_AV_SYNC_EVENT_QUEUE_SIZE) {
        events[*count] = ev;
        (*count)++;
        return;
    }

    // Drop oldest (index 0)
    for (size_t i = 1; i < *count; i++) {
        events[i - 1] = events[i];
    }
    events[*count - 1] = ev;
}

static bool find_best_match(const struct c64_av_sync_event *events, size_t count, uint64_t ts, size_t *best_index,
                            uint64_t *best_abs_delta)
{
    bool found = false;
    uint64_t best = 0;
    size_t best_i = 0;

    for (size_t i = 0; i < count; i++) {
        if (events[i].used) {
            continue;
        }
        uint64_t abs_delta = (events[i].ts > ts) ? (events[i].ts - ts) : (ts - events[i].ts);
        if (abs_delta > C64_AV_SYNC_MATCH_WINDOW_NS) {
            continue;
        }
        if (!found || abs_delta < best) {
            found = true;
            best = abs_delta;
            best_i = i;
        }
    }

    if (!found) {
        return false;
    }

    *best_index = best_i;
    *best_abs_delta = best;
    return true;
}

static void log_matched_pair_from_audio(const struct c64_av_sync_event *video_ev,
                                        const struct c64_av_sync_event *audio_ev)
{
    char detected[64];
    c64_av_sync_format_wall_clock(detected, sizeof(detected));

    int64_t delta_ns = (int64_t)audio_ev->ts - (int64_t)video_ev->ts;
    double delta_ms = (double)delta_ns / 1000000.0;

    C64_LOG_INFO("" AUDIO_LOG_PREFIX " AV SYNC: offset=%.1fms video=#%u audio=#%u detected=%s video_frame=%u "
                 "video_ts=%" PRIu64 " audio_ts=%" PRIu64,
                 delta_ms, video_ev->seq, audio_ev->seq, detected, (uint32_t)video_ev->frame_num, video_ev->ts,
                 audio_ev->ts);
}

static void log_matched_pair_from_video(const struct c64_av_sync_event *video_ev,
                                        const struct c64_av_sync_event *audio_ev)
{
    char detected[64];
    c64_av_sync_format_wall_clock(detected, sizeof(detected));

    int64_t delta_ns = (int64_t)audio_ev->ts - (int64_t)video_ev->ts;
    double delta_ms = (double)delta_ns / 1000000.0;

    C64_LOG_INFO("" VIDEO_LOG_PREFIX " AV SYNC: offset=%.1fms video=#%u audio=#%u detected=%s video_frame=%u "
                 "video_ts=%" PRIu64 " audio_ts=%" PRIu64,
                 delta_ms, video_ev->seq, audio_ev->seq, detected, (uint32_t)video_ev->frame_num, video_ev->ts,
                 audio_ev->ts);
}

void c64_av_sync_init(struct c64_source *context)
{
    if (!context) {
        return;
    }

    if (pthread_mutex_init(&context->av_sync_mutex, NULL) != 0) {
        C64_LOG_ERROR("Failed to initialize AV sync mutex");
        return;
    }

    context->av_sync_audio_events_count = 0;
    context->av_sync_video_events_count = 0;
}

void c64_av_sync_cleanup(struct c64_source *context)
{
    if (!context) {
        return;
    }

    pthread_mutex_destroy(&context->av_sync_mutex);
}

void c64_av_sync_on_video_pop(struct c64_source *context, uint16_t frame_num, uint64_t timestamp_ns)
{
    if (!context || !c64_debug_logging) {
        return;
    }

    if (pthread_mutex_lock(&context->av_sync_mutex) != 0) {
        return;
    }

    if (context->av_sync_last_video_pop_detection_ts != 0) {
        uint64_t since_ns = timestamp_ns - context->av_sync_last_video_pop_detection_ts;
        if (since_ns < C64_AV_SYNC_DEBOUNCE_NS) {
            pthread_mutex_unlock(&context->av_sync_mutex);
            return;
        }
    }
    context->av_sync_last_video_pop_detection_ts = timestamp_ns;

    context->av_sync_video_pop_count++;
    context->av_sync_last_video_pop_ts = timestamp_ns;

    struct c64_av_sync_event video_ev = {
        .ts = timestamp_ns,
        .seq = context->av_sync_video_pop_count,
        .frame_num = frame_num,
        .used = false,
    };

    // Try to match against existing audio pops before pushing (so we can match against the oldest audio event if needed)
    size_t best_audio_idx = 0;
    uint64_t best_abs_delta = 0;
    bool matched = find_best_match(context->av_sync_audio_events, context->av_sync_audio_events_count, timestamp_ns,
                                   &best_audio_idx, &best_abs_delta);

    if (matched) {
        struct c64_av_sync_event *audio_ev = &context->av_sync_audio_events[best_audio_idx];
        audio_ev->used = true;
        video_ev.used = true;
        log_matched_pair_from_video(&video_ev, audio_ev);
    }

    push_event(context->av_sync_video_events, &context->av_sync_video_events_count, video_ev);

    prune_used_or_expired(context->av_sync_audio_events, &context->av_sync_audio_events_count, timestamp_ns);
    prune_used_or_expired(context->av_sync_video_events, &context->av_sync_video_events_count, timestamp_ns);

    pthread_mutex_unlock(&context->av_sync_mutex);
}

void c64_av_sync_on_audio_pop(struct c64_source *context, uint64_t timestamp_ns)
{
    if (!context || !c64_debug_logging) {
        return;
    }

    if (pthread_mutex_lock(&context->av_sync_mutex) != 0) {
        return;
    }

    if (context->av_sync_last_audio_pop_detection_ts != 0) {
        uint64_t since_ns = timestamp_ns - context->av_sync_last_audio_pop_detection_ts;
        if (since_ns < C64_AV_SYNC_DEBOUNCE_NS) {
            pthread_mutex_unlock(&context->av_sync_mutex);
            return;
        }
    }
    context->av_sync_last_audio_pop_detection_ts = timestamp_ns;

    context->av_sync_audio_pop_count++;
    context->av_sync_last_audio_pop_ts = timestamp_ns;

    struct c64_av_sync_event audio_ev = {
        .ts = timestamp_ns,
        .seq = context->av_sync_audio_pop_count,
        .frame_num = 0,
        .used = false,
    };

    size_t best_video_idx = 0;
    uint64_t best_abs_delta = 0;
    bool matched = find_best_match(context->av_sync_video_events, context->av_sync_video_events_count, timestamp_ns,
                                   &best_video_idx, &best_abs_delta);

    if (matched) {
        struct c64_av_sync_event *video_ev = &context->av_sync_video_events[best_video_idx];
        video_ev->used = true;
        audio_ev.used = true;
        log_matched_pair_from_audio(video_ev, &audio_ev);
    }

    push_event(context->av_sync_audio_events, &context->av_sync_audio_events_count, audio_ev);

    prune_used_or_expired(context->av_sync_audio_events, &context->av_sync_audio_events_count, timestamp_ns);
    prune_used_or_expired(context->av_sync_video_events, &context->av_sync_video_events_count, timestamp_ns);

    pthread_mutex_unlock(&context->av_sync_mutex);
}
