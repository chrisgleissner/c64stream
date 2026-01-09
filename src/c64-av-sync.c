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

static const char *c64_av_sync_origin_str(enum c64_av_sync_origin origin)
{
    switch (origin) {
    case C64_AV_SYNC_ORIGIN_NETWORK:
        return "Network";
    case C64_AV_SYNC_ORIGIN_OBS:
        return "OBS";
    default:
        return "Unknown";
    }
}

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

static void c64_av_sync_store_match(struct c64_av_sync_state *state, const struct c64_av_sync_event *video_ev,
                                    const struct c64_av_sync_event *audio_ev)
{
    if (!state || !video_ev || !audio_ev) {
        return;
    }

    state->last_match.video_ts = video_ev->ts;
    state->last_match.audio_ts = audio_ev->ts;
    state->last_match.video_seq = video_ev->seq;
    state->last_match.audio_seq = audio_ev->seq;
    state->last_match.video_frame_num = video_ev->frame_num;
    state->last_match.valid = true;
}

static void c64_av_sync_log_network_and_obs_match(const struct c64_av_sync_match *obs_match,
                                                  const struct c64_av_sync_match *net_match, bool is_audio_trigger)
{
    if (!obs_match || !obs_match->valid) {
        return;
    }

    char detected[64];
    c64_av_sync_format_wall_clock(detected, sizeof(detected));

    int64_t obs_delta_ns = (int64_t)obs_match->audio_ts - (int64_t)obs_match->video_ts;
    double obs_delta_ms = (double)obs_delta_ns / 1000000.0;

    if (net_match && net_match->valid) {
        int64_t net_delta_ns = (int64_t)net_match->audio_ts - (int64_t)net_match->video_ts;
        double net_delta_ms = (double)net_delta_ns / 1000000.0;

        int64_t video_path_ns = (int64_t)obs_match->video_ts - (int64_t)net_match->video_ts;
        int64_t audio_path_ns = (int64_t)obs_match->audio_ts - (int64_t)net_match->audio_ts;

        const char *prefix = is_audio_trigger ? AUDIO_LOG_PREFIX : VIDEO_LOG_PREFIX;
        C64_LOG_INFO("%s AV SYNC (OBS+Network): obs_offset=%.1fms net_offset=%.1fms video=#%u audio=#%u detected=%s "
                     "video_frame=%u obs_video_ts=%" PRIu64 " obs_audio_ts=%" PRIu64 " net_video_ts=%" PRIu64
                     " net_audio_ts=%" PRIu64 " net_to_obs_video=%+.1fms net_to_obs_audio=%+.1fms",
                     prefix, obs_delta_ms, net_delta_ms, obs_match->video_seq, obs_match->audio_seq, detected,
                     (uint32_t)obs_match->video_frame_num, obs_match->video_ts, obs_match->audio_ts,
                     net_match->video_ts, net_match->audio_ts, (double)video_path_ns / 1000000.0,
                     (double)audio_path_ns / 1000000.0);
        return;
    }

    // Fallback: OBS-only.
    const char *prefix = is_audio_trigger ? AUDIO_LOG_PREFIX : VIDEO_LOG_PREFIX;
    C64_LOG_INFO("%s AV SYNC (OBS): offset=%.1fms video=#%u audio=#%u detected=%s video_frame=%u video_ts=%" PRIu64
                 " audio_ts=%" PRIu64,
                 prefix, obs_delta_ms, obs_match->video_seq, obs_match->audio_seq, detected,
                 (uint32_t)obs_match->video_frame_num, obs_match->video_ts, obs_match->audio_ts);
}

static void log_matched_pair_from_audio(enum c64_av_sync_origin origin, const struct c64_av_sync_event *video_ev,
                                        const struct c64_av_sync_event *audio_ev)
{
    char detected[64];
    c64_av_sync_format_wall_clock(detected, sizeof(detected));

    int64_t delta_ns = (int64_t)audio_ev->ts - (int64_t)video_ev->ts;
    double delta_ms = (double)delta_ns / 1000000.0;

    C64_LOG_INFO("" AUDIO_LOG_PREFIX " AV SYNC (%s): offset=%.1fms video=#%u audio=#%u detected=%s video_frame=%u "
                 "video_ts=%" PRIu64 " audio_ts=%" PRIu64,
                 c64_av_sync_origin_str(origin), delta_ms, video_ev->seq, audio_ev->seq, detected,
                 (uint32_t)video_ev->frame_num, video_ev->ts, audio_ev->ts);
}

static void log_matched_pair_from_video(enum c64_av_sync_origin origin, const struct c64_av_sync_event *video_ev,
                                        const struct c64_av_sync_event *audio_ev)
{
    char detected[64];
    c64_av_sync_format_wall_clock(detected, sizeof(detected));

    int64_t delta_ns = (int64_t)audio_ev->ts - (int64_t)video_ev->ts;
    double delta_ms = (double)delta_ns / 1000000.0;

    C64_LOG_INFO("" VIDEO_LOG_PREFIX " AV SYNC (%s): offset=%.1fms video=#%u audio=#%u detected=%s video_frame=%u "
                 "video_ts=%" PRIu64 " audio_ts=%" PRIu64,
                 c64_av_sync_origin_str(origin), delta_ms, video_ev->seq, audio_ev->seq, detected,
                 (uint32_t)video_ev->frame_num, video_ev->ts, audio_ev->ts);
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

    for (size_t i = 0; i < C64_AV_SYNC_ORIGIN_COUNT; i++) {
        context->av_sync[i].audio_events_count = 0;
        context->av_sync[i].video_events_count = 0;
        context->av_sync[i].audio_pop_count = 0;
        context->av_sync[i].video_pop_count = 0;
        context->av_sync[i].last_audio_pop_ts = 0;
        context->av_sync[i].last_video_pop_ts = 0;
        context->av_sync[i].last_audio_pop_detection_ts = 0;
        context->av_sync[i].last_video_pop_detection_ts = 0;
        context->av_sync[i].last_match.valid = false;
    }
}

void c64_av_sync_cleanup(struct c64_source *context)
{
    if (!context) {
        return;
    }

    pthread_mutex_destroy(&context->av_sync_mutex);
}

void c64_av_sync_on_video_pop(struct c64_source *context, enum c64_av_sync_origin origin, uint16_t frame_num,
                              uint64_t timestamp_ns)
{
    if (!context || !c64_debug_logging) {
        return;
    }

    if ((int)origin < 0 || (size_t)origin >= C64_AV_SYNC_ORIGIN_COUNT) {
        return;
    }

    if (pthread_mutex_lock(&context->av_sync_mutex) != 0) {
        return;
    }

    struct c64_av_sync_state *state = &context->av_sync[origin];

    if (state->last_video_pop_detection_ts != 0) {
        uint64_t since_ns = timestamp_ns - state->last_video_pop_detection_ts;
        if (since_ns < C64_AV_SYNC_DEBOUNCE_NS) {
            pthread_mutex_unlock(&context->av_sync_mutex);
            return;
        }
    }
    state->last_video_pop_detection_ts = timestamp_ns;

    state->video_pop_count++;
    state->last_video_pop_ts = timestamp_ns;

    struct c64_av_sync_event video_ev = {
        .ts = timestamp_ns,
        .seq = state->video_pop_count,
        .frame_num = frame_num,
        .used = false,
    };

    // Try to match against existing audio pops before pushing (so we can match against the oldest audio event if needed)
    size_t best_audio_idx = 0;
    uint64_t best_abs_delta = 0;
    bool matched =
        find_best_match(state->audio_events, state->audio_events_count, timestamp_ns, &best_audio_idx, &best_abs_delta);

    if (matched) {
        struct c64_av_sync_event *audio_ev = &state->audio_events[best_audio_idx];
        audio_ev->used = true;
        video_ev.used = true;
        c64_av_sync_store_match(state, &video_ev, audio_ev);

        if (origin == C64_AV_SYNC_ORIGIN_OBS) {
            struct c64_av_sync_state *net_state = &context->av_sync[C64_AV_SYNC_ORIGIN_NETWORK];
            const bool related = net_state->last_match.valid &&
                                 (net_state->last_match.video_frame_num == state->last_match.video_frame_num);

            if (related) {
                c64_av_sync_log_network_and_obs_match(&state->last_match, &net_state->last_match, false);
            } else {
                c64_av_sync_log_network_and_obs_match(&state->last_match, NULL, false);
                if (net_state->last_match.valid) {
                    log_matched_pair_from_video(C64_AV_SYNC_ORIGIN_NETWORK,
                                                &(struct c64_av_sync_event){
                                                    .ts = net_state->last_match.video_ts,
                                                    .seq = net_state->last_match.video_seq,
                                                    .frame_num = net_state->last_match.video_frame_num,
                                                    .used = true,
                                                },
                                                &(struct c64_av_sync_event){
                                                    .ts = net_state->last_match.audio_ts,
                                                    .seq = net_state->last_match.audio_seq,
                                                    .frame_num = 0,
                                                    .used = true,
                                                });
                }
            }
        }
    }

    push_event(state->video_events, &state->video_events_count, video_ev);

    prune_used_or_expired(state->audio_events, &state->audio_events_count, timestamp_ns);
    prune_used_or_expired(state->video_events, &state->video_events_count, timestamp_ns);

    pthread_mutex_unlock(&context->av_sync_mutex);
}

void c64_av_sync_on_audio_pop(struct c64_source *context, enum c64_av_sync_origin origin, uint64_t timestamp_ns)
{
    if (!context || !c64_debug_logging) {
        return;
    }

    if ((int)origin < 0 || (size_t)origin >= C64_AV_SYNC_ORIGIN_COUNT) {
        return;
    }

    if (pthread_mutex_lock(&context->av_sync_mutex) != 0) {
        return;
    }

    struct c64_av_sync_state *state = &context->av_sync[origin];

    if (state->last_audio_pop_detection_ts != 0) {
        uint64_t since_ns = timestamp_ns - state->last_audio_pop_detection_ts;
        if (since_ns < C64_AV_SYNC_DEBOUNCE_NS) {
            pthread_mutex_unlock(&context->av_sync_mutex);
            return;
        }
    }
    state->last_audio_pop_detection_ts = timestamp_ns;

    state->audio_pop_count++;
    state->last_audio_pop_ts = timestamp_ns;

    struct c64_av_sync_event audio_ev = {
        .ts = timestamp_ns,
        .seq = state->audio_pop_count,
        .frame_num = 0,
        .used = false,
    };

    size_t best_video_idx = 0;
    uint64_t best_abs_delta = 0;
    bool matched =
        find_best_match(state->video_events, state->video_events_count, timestamp_ns, &best_video_idx, &best_abs_delta);

    if (matched) {
        struct c64_av_sync_event *video_ev = &state->video_events[best_video_idx];
        video_ev->used = true;
        audio_ev.used = true;
        c64_av_sync_store_match(state, video_ev, &audio_ev);

        if (origin == C64_AV_SYNC_ORIGIN_OBS) {
            struct c64_av_sync_state *net_state = &context->av_sync[C64_AV_SYNC_ORIGIN_NETWORK];
            const bool related = net_state->last_match.valid &&
                                 (net_state->last_match.video_frame_num == state->last_match.video_frame_num);

            if (related) {
                c64_av_sync_log_network_and_obs_match(&state->last_match, &net_state->last_match, true);
            } else {
                c64_av_sync_log_network_and_obs_match(&state->last_match, NULL, true);
                if (net_state->last_match.valid) {
                    log_matched_pair_from_audio(C64_AV_SYNC_ORIGIN_NETWORK,
                                                &(struct c64_av_sync_event){
                                                    .ts = net_state->last_match.video_ts,
                                                    .seq = net_state->last_match.video_seq,
                                                    .frame_num = net_state->last_match.video_frame_num,
                                                    .used = true,
                                                },
                                                &(struct c64_av_sync_event){
                                                    .ts = net_state->last_match.audio_ts,
                                                    .seq = net_state->last_match.audio_seq,
                                                    .frame_num = 0,
                                                    .used = true,
                                                });
                }
            }
        }
    }

    push_event(state->audio_events, &state->audio_events_count, audio_ev);

    prune_used_or_expired(state->audio_events, &state->audio_events_count, timestamp_ns);
    prune_used_or_expired(state->video_events, &state->video_events_count, timestamp_ns);

    pthread_mutex_unlock(&context->av_sync_mutex);
}
