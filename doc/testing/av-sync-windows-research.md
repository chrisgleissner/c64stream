# A/V Sync on Windows

This document contains research on A/V sync when running OBS on Windows 11. As part of that, the detailed data flow from the C64 Ultimate to an OBS recording is covered.

---

## 1. Executive Summary

An OBS source plugin (`c64stream`) receives C64 Ultimate UDP video+audio, applies an internal jitter/delay buffer, generates synthetic monotonic timestamps for both streams, and pushes frames/samples into OBS.

The observed behavior is platform-dependent: Kubuntu 24.04 produces “perfect” A/V alignment in the recording, while Windows 11 OBS recordings produces a mostly-stable ~400 ms audio lag (sometimes disappearing).

**Multiple candidate root causes remain.** The repository code contains **no explicit platform-specific +400 ms audio delay**, so the offset must arise from (a) Windows-only OBS-side buffering/offset behavior, or (b) Windows-only differences in the plugin’s real-time behavior (thread scheduling / receive backlog / timing-base establishment).

Certainty is not yet possible because the repo contains no Windows-side timing artifacts (CSV logs or debug logs) for this specific failure mode.

---

## 2. Measurement & Ground Truth

### 2.1 One-frame “pop” test definition (as implemented in repo tooling)

The repository’s E2E harness uses paired audio+video “pop” markers designed for A/V sync validation:

- **Video pop**: a bright flash in a fixed bottom-right ROI (“A/V pop indicator”).
- **Audio pop**: a short band-limited noise burst, alternating channels per pop.
- **Cadence**: every 48 video frames (≈960 ms PAL, ≈800 ms NTSC).
- **Duration discrepancy in repo**:
  - Generator defines `POP_FRAME_DURATION = 1` frame in `tests/e2e/util/generate_packets.py:176-179`.
  - The verifier docstring claims “Duration: 2 frames” in `tests/e2e/util/test_av_sync.py:8-16`.
  - Observable “source of truth” for actual generated duration is the generator constant (`tests/e2e/util/generate_packets.py:176-179`), not the comment.

**Key artifact**: the verifier pairs detected audio pop times to detected video pop times and reports `difference_ms` (audio_time_ms − video_time_ms), i.e. **positive values mean audio lags video** (see typical output structures in `tests/e2e/results/*/validation_results.json`).

### 2.2 How alignment is measured (frame-accurate vs sample-accurate)

The repo’s A/V sync verifier (`tests/e2e/util/test_av_sync.py`) uses:

- **Video timing**: pop detection in video frames via OpenCV; timestamps derived from frame time (frame index / fps), thus frame-accurate at best (see `detect_video_pop_events` in `tests/e2e/util/test_av_sync.py:102+`).
- **Audio timing**: audio is extracted via ffmpeg to PCM at 48 kHz and reduced to a coarse RMS envelope with `window_ms=10` by default (`tests/e2e/util/test_av_sync.py:57-99`). Pop timing is therefore **quantized to 10 ms bins** unless `window_ms` is reduced.

So repo tooling is **not sample-accurate** by default; it is ~10 ms-quantized on the audio side, and frame-quantized on the video side. A measured ~400 ms lag is orders of magnitude above this quantization and is unambiguous.

### 2.3 Quantified offsets: repo ground truth (Linux E2E baseline)

The repository includes completed E2E runs under `tests/e2e/results/*/validation_results.json`. Parsing those artifacts yields typical offsets (audio minus video) in the **single-digit to ~30 ms range**, depending on preset and scenario.

Examples (all are `difference_ms` stats; positive means audio lags video):

- `tests/e2e/results/ntsc_default/validation_results.json`: mean ≈ 10.55 ms, max ≈ 13.30 ms.
- `tests/e2e/results/pal_default/validation_results.json`: mean ≈ 31.72 ms, max ≈ 32.92 ms.

These small offsets are consistent with the verifier’s discussion of encoder/muxer effects (`tests/e2e/util/test_av_sync.py:32-37`) and are **not** in the same regime as a 400 ms lag.

### 2.4 Quantified offsets: Linux vs Windows for the reported issue

The prompt provides qualitative and approximate quantitative observations:

- **Linux (Kubuntu 24.04)**: “Perfect A/V alignment” (interpretable as ≲ one frame and/or ≲10 ms in practice, but no artifact is provided here).
- **Windows 11**: audio lags video by **~400 ms**, mostly stable, occasionally disappears.

Because no Windows-side pop analysis artifacts (e.g. `validation_results.json`, or equivalent measurement output) are present in the repo for this failure mode, **mean/variance/min/max for the real LAN scenario cannot be derived strictly from repository data**. The repo does, however, provide the measurement procedure required to compute them deterministically (see §10).

---

## 3. Plugin Architecture & Data Flow

This section describes the actual code path in the plugin (not the E2E harness).

### 3.1 Audio pipeline: capture → timestamp → buffering → OBS submission

1. **UDP receive (audio receiver thread)**
   - Function: `audio_thread_func` (`src/c64-audio.c:21-133`).
   - Reads from a non-blocking UDP socket (socket is set non-blocking in `src/c64-network.c:440-453`).
   - On packet receipt, it records arrival time: `packet_time = os_gettime_ns()` (`src/c64-audio.c:95-100` region; visible in earlier excerpt).
   - It pushes packet bytes into the unified network buffer with a timestamp (uses `audio_now = os_gettime_ns()` and pushes that) (`src/c64-audio.c` shows push at `c64_network_buffer_push_audio(..., audio_now)` in the earlier grep output; structurally this is the same pattern as video).

2. **Network buffer (jitter/delay buffer)**
   - Data structure: ring buffers for video and audio packets (`src/c64-network-buffer.c`, API in `src/c64-network-buffer.h`).
   - Each pushed packet stores `timestamp_us = timestamp_ns/1000` (`src/c64-network-buffer.c:684-694`).
   - Pop gating: a packet is “ready” when `age_us >= delay_us` using `now_us = os_gettime_ns()/1000` (`src/c64-network-buffer.c:703-710`).

3. **Processing thread (video processor thread also processes audio)**
   - The video processor thread pops from the network buffer (`src/c64-video.c:1411-1437`).
   - When an audio packet is available, it calls `c64_process_audio_packet(context, audio_data, audio_size, timestamp_us * 1000)` (`src/c64-video.c:1430-1432`).

4. **Audio timestamp generation and OBS submission**
   - `c64_process_audio_packet` ignores the passed `timestamp_ns` for scheduling and instead generates a synthetic monotonic timestamp via `generate_monotonic_audio_timestamp` (`src/c64-audio.c:213-246`).
   - It pushes audio to OBS via `obs_source_output_audio` (`src/c64-audio.c:251-252`) with:
     - `frames = 192` (`src/c64-audio.c:241`)
     - `samples_per_sec = (uint32_t)context->audio_sample_rate` (`src/c64-audio.c:242`)
     - `timestamp = audio_timestamp` (`src/c64-audio.c:245`)

**Timestamp origin and units (audio):**

- All internal time references use `os_gettime_ns()` (nanoseconds).
- OBS receives `audio_output.timestamp` in **nanoseconds** (`uint64_t`), generated synthetically (not derived from network arrival timestamps).

### 3.2 Video pipeline: capture → timestamp → buffering → OBS submission

1. **UDP receive (video receiver thread)**
   - Function: `c64_video_thread_func` (`src/c64-video.c:922-1126`).
   - Records receipt time via `os_gettime_ns()` and pushes into network buffer (pattern visible in `src/c64-video.c` excerpt around `c64_network_buffer_push_video(..., now)`; the push API is `src/c64-network-buffer.c:666-682`).

2. **Network buffer (same unified jitter/delay buffer)**
   - Same gating mechanism as audio: `age_us >= delay_us` (`src/c64-network-buffer.c:696-750`).

3. **Processing & frame assembly (video processor thread)**
   - Pops video packets and calls `c64_process_video_packet_direct(context, video_data, video_size, timestamp_us*1000)` (`src/c64-video.c:1419-1421`).
   - Frame assembly is performed inside `c64_process_video_packet_direct` (`src/c64-video.c:1219+`), completing frames when transitioning to a new frame or on timeout.

4. **Video timestamp generation and OBS submission**
   - When a frame completes, `c64_render_frame_direct` outputs to OBS:
     - It **computes a synthetic “ideal” timestamp** from frame sequence number: `c64_calculate_ideal_timestamp` (`src/c64-video.c:687-718`, `src/c64-video.c:1129-1177`).
     - It assigns `obs_frame.timestamp = monotonic_timestamp` (`src/c64-video.c:717`) and calls `obs_source_output_video` (`src/c64-video.c:720-722`).

**Timestamp origin and units (video):**

- `stream_start_time_ns` is set from `os_gettime_ns()` or `audio_base_time` (`src/c64-video.c:1132-1146`).
- Per-frame timestamps are computed as:
  `ideal_timestamp = stream_start_time_ns + frame_offset * frame_interval_ns` (`src/c64-video.c:1164-1167`).

---

## 4. Cross-Platform Delta Analysis (Linux vs Windows)

This section lists **concrete, code-visible** platform differences that can plausibly affect real-time behavior and buffering.

### 4.1 Time sources and clocks

- Both platforms use `os_gettime_ns()` from OBS util (`<util/platform.h>`), called in audio/video receiver threads and in the network buffer readiness check (`src/c64-network-buffer.c:703-710`).
- No explicit Windows-only timestamp conversion exists in this repo; differences depend on OBS’s platform implementation of `os_gettime_ns()` (not in this repo).

### 4.2 Threading and scheduling

**Windows-only priority/timer changes in the video receiver thread:**

- Video receiver thread sets above-normal priority via `SetThreadPriority(..., THREAD_PRIORITY_ABOVE_NORMAL)` (`src/c64-video.c:929-935`).
- It calls `timeBeginPeriod(1)` (`src/c64-video.c:937`) and later `timeEndPeriod(1)` (`src/c64-video.c:1122-1124`).

**Audio receiver thread does not apply analogous scheduling changes:**

- No `SetThreadPriority` or `timeBeginPeriod` appears in `audio_thread_func` (`src/c64-audio.c:21-74`).

**Non-blocking receive polling differences (Windows path):**

- Audio: on `WSAEWOULDBLOCK`, sleeps `os_sleep_ms(1)` (`src/c64-audio.c:43-46`).
- Video: on `WSAEWOULDBLOCK`, sleeps `os_sleep_ms(0)` (visible in truncated section around `src/c64-video.c:1016+`; the pattern exists and is materially different from audio’s 1 ms sleep).

These differences can change which stream stays “closest to real time” under contention.

### 4.3 Network I/O behavior

- Sockets are set **non-blocking** on both platforms (`src/c64-network.c:440-453`).
- Windows sets a **larger SO_RCVBUF** (2 MB) vs Linux/macOS (1 MB) (`src/c64-network.c:394-423`).
- Windows adds a **50 ms socket readiness sleep** after UDP socket creation (`src/c64-network.c:457-461`) and an additional **100 ms sleep** before sending start commands (`src/c64-source.c:909-913`). These delays are symmetric for audio+video and therefore cannot directly explain an audio-vs-video differential, but they can affect timing-base establishment order.

### 4.4 OBS audio buffering/resampling behavior

- The plugin outputs audio at non-48 kHz rates derived from hardware (PAL/NTSC): `47982.8869 Hz` / `47940.3408 Hz` (`src/c64-protocol.h:43-48`), and sets `audio_output.samples_per_sec` accordingly (`src/c64-audio.c:241-245`).
- OBS must therefore resample/mix to its configured output sample rate. The exact buffering behavior is OBS-internal and platform-dependent; this repo does not include OBS’s mixer implementation.

### 4.5 Platform-specific code paths in this plugin

There is **no Windows-only audio buffering constant** in the audio pipeline. The only major Windows-only behavioral changes are concentrated in **video thread scheduling** (`src/c64-video.c:929-949`) and **socket creation sleeps** (`src/c64-network.c:457-461`, `src/c64-source.c:909-913`).

---

## 5. Timestamp Correctness Audit (Critical Section)

### 5.1 Are audio and video timestamps derived from the same clock?

**Intended:** yes. Both ultimately use `os_gettime_ns()` as the base clock and operate in nanoseconds:

- Audio base selection uses `os_gettime_ns()` (`src/c64-audio.c:148-176`).
- Video base selection uses `os_gettime_ns()` or `audio_base_time` (`src/c64-video.c:1132-1146`).

**Actual:** there is no synchronization primitive protecting `timestamp_base_set`, `stream_start_time_ns`, and `audio_base_time`, which are accessed across multiple threads:

- Audio timestamp generation checks `context->timestamp_base_set` and `context->stream_start_time_ns` (`src/c64-audio.c:166-176`).
- Video timestamp generation checks `context->timestamp_base_set` and `context->audio_base_time` (`src/c64-video.c:1132-1146`).

These are plain fields in `struct c64_source` (`src/c64-types.h:104-125` region; notably `timestamp_base_set`, `stream_start_time_ns`, `audio_base_time`). **In C, unsynchronized concurrent access constitutes a data race (undefined behavior).** This is a factual code property independent of platform; platform differences can change how often it manifests.

### 5.2 Are they monotonic and in OBS-expected units?

- Video timestamps increase with frame number (`src/c64-video.c:1164-1167`) and are written to `obs_frame.timestamp` (`src/c64-video.c:717`).
- Audio timestamps increase with packet count (`src/c64-audio.c:180-184`) and are written to `audio_output.timestamp` (`src/c64-audio.c:245`).
- Both are in **nanoseconds**.

Monotonicity can be broken only if:

- `stream_start_time_ns` changes midstream (e.g., buffer delay adjustments in `src/c64-source.c:780-820`), or
- `audio_base_time` changes via drift correction (`src/c64-audio.c:185-200`).

Both are present by design.

### 5.3 Capture-time vs submit-time timestamps

Neither stream uses capture-time (device) timestamps:

- The network buffer timestamps are based on local receipt time (`os_gettime_ns()` in receiver threads, then stored as `timestamp_us` in `src/c64-network-buffer.c:679-694`).
- The timestamps delivered to OBS are **synthetic**, computed from `stream_start_time_ns` / `audio_base_time` and sequence counters, not from network receipt time (`src/c64-video.c:1129-1177`, `src/c64-audio.c:139-210`).

This means the plugin is effectively generating a “presentation clock” and asking OBS to play to it.

### 5.4 Platform-specific conversion/rounding/offset?

None is present in plugin code. The only rounding that could affect steady-state is:

- Audio interval uses double division but is stored as `uint64_t` nanoseconds (`src/c64-audio.c:154-157`).
- `samples_per_sec` is cast to `uint32_t` (`src/c64-audio.c:241-245`).

Neither can produce a fixed 400 ms offset by itself.

### 5.5 Could a fixed ~400 ms offset arise from timestamp misuse?

A stable 400 ms offset corresponds to a constant bias between the audio and video presentation clocks. In this codebase, that can happen only if:

- The **bases** differ by ~400 ms (`audio_base_time` vs `stream_start_time_ns`), or
- The sequence counters are offset such that audio is “scheduled” ~400 ms later than the corresponding video.

The code’s logic attempts to share bases across streams (`src/c64-audio.c:166-176`, `src/c64-video.c:1132-1146`), but because it is not synchronized, a Windows-only manifestation of base divergence is plausible (see hypotheses in §8).

---

## 6. Buffering & Queue Depth Analysis

### 6.1 Plugin-level buffering that can sum to ~400 ms

There are two distinct buffering mechanisms in the plugin:

1. **Configurable network delay (“buffer delay”)**
   - Set from OBS settings `buffer_delay_ms` (default forced to 10 ms if 0) (`src/c64-source.c:414-418`).
   - Applied to both audio and video via `c64_network_buffer_set_delay(..., delay, delay)` (`src/c64-source.c:434-435`).
   - Delay is enforced per packet based on receipt timestamp (`src/c64-network-buffer.c:696-750`).

2. **Capacity to accumulate backlog in the ring buffers**
   - Hard sizing targets: `C64_MAX_DELAY_MS = 500`, `C64_MAX_JITTER_MS = 400` (`src/c64-network-buffer.h:34-39`).
   - Max capacities derived from worst-case rates and (delay+jitter):
     - `C64_MAX_VIDEO_PACKETS = 3554` (`src/c64-network-buffer.h:47`)
     - `C64_MAX_AUDIO_PACKETS = 247` (`src/c64-network-buffer.h:48`)
   - In time terms, these correspond to ~989 ms of buffering potential (both audio and video) at their respective packet intervals.

**Key math:** a 400 ms audio lag equals ~100 audio packets.

- From protocol: PAL audio packet interval ≈ 4.001417 ms (`doc/c64u-stream-spec.md:112-116`).
  100 packets ≈ 400.1417 ms.
- From buffer slot math: at 400 ms delay, the computed audio “active slots” would be `ceil(250 * 0.4) = 100` packets (`src/c64-network-buffer.c:466-469`, using `C64_MAX_AUDIO_RATE=250` from `src/c64-network-buffer.h:31-33`).

This demonstrates that a ~400 ms lag is **exactly** the scale of backlog/delay this system is designed to be able to hold.

### 6.2 Is buffering duplicated between plugin and OBS?

Yes, by architecture:

- Plugin introduces buffering via the network delay mechanism (`src/c64-network-buffer.c:696-750`) and by allowing backlog.
- OBS introduces additional buffering in:
  - audio mixing/resampling (especially when source sample rate differs from output),
  - video frame queues,
  - encoder lookahead and muxing.

Repository E2E tooling explicitly expects baseline offsets up to ~60 ms due to encode/muxer behavior (`tests/e2e/util/test_av_sync.py:32-37`). That is far smaller than 400 ms; therefore, a 400 ms offset requires either extreme OBS buffering/offset configuration or a substantial upstream backlog/clock-bias.

---

## 7. OBS API Contract Verification

### 7.1 Correct API usage (as visible in repo)

- Video: `obs_source_output_video(context->source, &obs_frame)` (`src/c64-video.c:720-722`) with `OBS_SOURCE_ASYNC_VIDEO` output flag set in `src/plugin-main.c:44-55`.
- Audio: `obs_source_output_audio(context->source, &audio_output)` (`src/c64-audio.c:251-252`) with `OBS_SOURCE_AUDIO` flag also set (`src/plugin-main.c:44-55`).

The plugin pushes audio; it does not implement `audio_render` (`src/plugin-main.c:53-55`).

### 7.2 Timestamp expectations and buffer semantics (what can be verified here)

Within this repo, the only verifiable contract-related facts are:

- Timestamps provided to OBS are synthetic and in nanoseconds (`src/c64-video.c:717`, `src/c64-audio.c:245`).
- The plugin does not tie OBS timestamps to network receipt timestamps. This means OBS is expected to honor the provided timestamps as the presentation clock.

### 7.3 OBS compensation behavior for missing/incorrect timestamps (Windows sensitivity)

OBS behavior is not part of this repository. However, one direct implication from the plugin’s design is:

- If timestamps are **ahead of wall clock**, OBS can buffer (delay) output to match them.
- If timestamps are **behind**, OBS may drop/duplicate to catch up.

The reported symptom (“audio lags video by ~400 ms, mostly stable, occasionally disappears”) is most consistent with **audio being scheduled later than video** (timestamps bias) or **audio data arriving later than video by a persistent backlog**.

---

## 8. Hypotheses and Falsification (Ranked)

### Hypothesis 1 (Most likely): Windows OBS applies a persistent per-source or per-track audio delay/offset (~400 ms)

**Supporting evidence (facts + reasoning):**

- The plugin code does not introduce a Windows-only +400 ms audio-vs-video offset; Linux uses the same logic.
- A stable, large, near-constant offset is characteristic of an applied sync offset in OBS’s audio routing/recording pipeline rather than network jitter.
- Occasional disappearance is consistent with state-dependent routing (e.g., monitoring path vs recording path, track selection differences, or scene/source recreation).

**Counterevidence:**

- The prompt states OBS is configured for low latency; that *suggests* such an offset is unlikely, but this is not an observable repo fact.
- The offset magnitude (400 ms) is much larger than typical encode/muxer priming delays referenced in repo E2E docs (`tests/e2e/util/test_av_sync.py:32-37`), implying an explicit offset rather than incidental encoder behavior.

**Why it explains ~400 ms specifically:**

- OBS allows offsets in milliseconds; 400 ms is a “round” human-selected value and matches the repo’s jitter constant magnitude (`src/c64-network-buffer.h:39`)—a value someone might choose operationally.

**Discriminating experiment (conclusive):**

- Record two outputs on Windows from the same OBS session:
  1. a lossless/uncompressed audio codec recording (or separate WAV capture) and
  2. the normal encoder path,
  then run the repo’s pop analysis script (`tests/e2e/util/test_av_sync.py`) on both recordings.
  If the ~400 ms offset persists identically across codecs/containers, it is almost certainly an OBS-level sync offset/routing configuration rather than encoder priming.

### Hypothesis 2: Audio UDP receive backlog on Windows causes the audio stream to run ~100 packets (~400 ms) “behind” video

**Supporting evidence:**

- Video receive thread is Windows-prioritized (`src/c64-video.c:929-935`) while audio is not (`src/c64-audio.c:21-74`), and video packet rate is extremely high (≈3408–3590 pkt/s) vs audio (≈250 pkt/s) (`src/c64-network-buffer.h:24-33`, `doc/c64u-stream-spec.md:92-122`).
- Backlog of ~100 audio packets corresponds to ~400 ms exactly (math in §6.1).
- The plugin’s design can sustain ~1 second of buffered backlog without overflow (`src/c64-network-buffer.h:47-48`), making a 400 ms steady backlog feasible.

**Counterevidence:**

- A pure receive-backlog should often manifest as audible artifacts (bursty audio delivery) unless downstream buffering masks it.
- The network buffer delay is applied equally to audio and video (`src/c64-source.c:434-435`), so differential lag requires *differential receipt timing* or backlog.

**Why it explains ~400 ms specifically:**

- ~400 ms corresponds to exactly 100 audio packets at the specified packet duration (`doc/c64u-stream-spec.md:112-122`) and to the scale of “jitter budget” the plugin anticipates (`src/c64-network-buffer.h:39`).
- A scheduler-induced lag could stabilize around a fixed backlog if audio consumption chronically trails production by a small amount (OS scheduling equilibrium), occasionally draining to zero (lag disappears).

**Discriminating experiment (conclusive):**

- Enable the plugin’s built-in network CSV logging (no code changes required; it writes `network.csv` via `src/c64-record.c:189-214` and `src/c64-protocol.c:92-160` logging hooks) and capture on Windows during a lagging run.
  - Compute, from `network.csv`, the distribution of inter-arrival times (`packet_interval_us`) separately for audio vs video and detect burstiness / gaps.
  - If audio shows periodic gaps and subsequent bursts consistent with ~400 ms backlog while video remains steady, this hypothesis is confirmed.

### Hypothesis 3: Unsynchronized timing-base establishment causes an audio timestamp base that is ~400 ms later than video’s base (data race / ordering sensitivity)

**Supporting evidence:**

- Audio base selection depends on `timestamp_base_set` and `stream_start_time_ns` (`src/c64-audio.c:166-176`).
- Video base selection depends on `timestamp_base_set` and `audio_base_time` (`src/c64-video.c:1132-1146`).
- These fields are accessed across multiple threads without synchronization (plain loads/stores), which is a concrete data race in C and can manifest differently across platforms and schedules.
- If audio initializes `audio_base_time` from `current_real_time` while video has already established `stream_start_time_ns`, the bases can differ by the amount of wall time between those events—hundreds of milliseconds is plausible on Windows if audio start is delayed.

**Counterevidence:**

- The audio timestamp generator includes drift correction every 250 packets (≈1 s) and clamps drift toward real time (`src/c64-audio.c:185-200`), which would tend to reduce a persistent 400 ms bias over a few seconds. A truly stable 400 ms offset would require the bias to be continuously reintroduced or the measurement to occur before convergence.
- The repo contains no evidence that bases are being repeatedly reset in normal operation; base reset logic in settings updates touches `timestamp_base_set` but does not reset `audio_base_time` (`src/c64-source.c:780-820`), making behavior complex.

**Why it explains ~400 ms specifically:**

- The magnitude would equal the time difference between when video establishes base and when audio establishes base (or fails to adopt video’s base), which can be on the order of hundreds of milliseconds during startup, shader compilation, or Windows scheduling stalls.

**Discriminating experiment (conclusive):**

- Run with `debug_logging` enabled and capture logs that include:
  - “📐 Video timing base established …” (`src/c64-video.c:1141-1144`)
  - “🎵 Audio using video timing base …” vs “Audio synthetic timestamps initialized … base=…” (`src/c64-audio.c:166-176`)
- If the lagging Windows runs show audio initializing from `current_real_time` after video already established base (and not adopting `stream_start_time_ns`), this hypothesis is confirmed.

### Hypothesis 4 (Least likely): Windows-specific OBS resampling/mixing pipeline adds ~400 ms latency for non-48 kHz source audio

**Supporting evidence:**

- The plugin outputs at ~47.94–47.98 kHz, forcing resampling in OBS (`src/c64-protocol.h:43-48`, `src/c64-audio.c:241-245`).
- OBS’s audio path is platform-dependent; Windows backends may choose different buffering strategies.

**Counterevidence:**

- Linux should also resample for a non-48 kHz source; it does not show the 400 ms lag in the prompt.
- 400 ms is far beyond typical resampler group delays; it suggests explicit buffering/offset rather than filter latency.

**Why it explains ~400 ms specifically:**

- 400 ms at 48 kHz is 19,200 samples, which is a large round number that could correspond to internal buffering sizes, but this is speculative without OBS-side evidence.

**Discriminating experiment (conclusive):**

- On Windows, force OBS to record the source audio without resampling (if possible) or compare recordings with different OBS output sample rates, then re-run pop detection. If the lag scales with resampling configuration, this hypothesis gains support; otherwise it collapses.

---

## 9. Conclusion

**Most likely root cause (current best estimate):** a **Windows-specific downstream delay/offset** applied by OBS (explicit sync offset/routing) or a **Windows-specific upstream audio receive backlog** that effectively delays the audio stream by ~100 packets (~400 ms).

**Confidence:** 0.55 (moderate). The codebase shows multiple mechanisms that can produce hundreds of milliseconds of buffering/backlog (by design), and Windows-only scheduling differences (by code) that could make audio behave differently. However, without Windows-side timing artifacts (CSV logs or debug logs), the evidence does not collapse to a single definitive root cause.

**What additional evidence would collapse uncertainty:**

- A Windows capture of the plugin’s `network.csv` and `obs.csv` logs (enabled via existing recording toggles) during a “lagging” run and a “non-lagging” run, plus pop-detection results on the resulting recording. Those artifacts can decisively distinguish:
  - OBS-level fixed offset vs
  - packet receipt backlog vs
  - timing-base mis-establishment.

---

## 10. Appendix: Diagnostic Instrumentation (Read-Only)

_No code changes are proposed; this is a description of how to use existing instrumentation and what additional logging (if allowed in the future) would be decisive._

### 10.1 Use existing CSV logging already in the plugin

The plugin can already write two key CSVs to a session folder:

1. **`network.csv`** (per-packet reception events)
   - Header is written by `c64_network_write_header` and rows by `c64_network_log_video_packet` / `c64_network_log_audio_packet` (see `src/c64-record-network.c` and call sites in `src/c64-protocol.c:92-160`).
   - This reveals burstiness, reordering, and whether audio packet receipt is delayed relative to video.

2. **`obs.csv`** (per-delivery-to-OBS events)
   - Written by `c64_obs_log_video_event` / `c64_obs_log_audio_event` (`src/c64-record-obs.c:44-118`).
   - This shows when the plugin submits to OBS (but not the timestamps it assigns to frames/samples).

**Key metric extraction (numeric correlation plan):**

- From `network.csv`:
  - compute audio packet inter-arrival and “stall” durations;
  - compute the time offset between corresponding audio/video marker packets if your diagnostic can be recognized in payload (for real hardware this is harder; pops help).
- From the final recording:
  - run pop detection (same technique as `tests/e2e/util/test_av_sync.py`) to compute `difference_ms` per pop.

If the recording shows +400 ms while `network.csv` shows audio arriving contemporaneously with video (no backlog), the root cause shifts downstream (OBS routing/offset). If `network.csv` shows audio “late” in bursts, the root cause is upstream receive/backlog.

### 10.2 Pop timing correlation method (numerical, reproducible)

Use the repo’s established approach (adapted to your recording):

- Video pop time:
  - detect ROI brightness spikes in the known pop box (method in `tests/e2e/util/test_av_sync.py:102+`).
  - map frame index to time via fps.
- Audio pop time:
  - decode audio to PCM at a known sample rate (ffmpeg extraction in `tests/e2e/util/test_av_sync.py:57-74`),
  - compute short-window RMS envelope (10 ms default in `tests/e2e/util/test_av_sync.py:85-95`),
  - detect threshold crossings (`tests/e2e/util/test_av_sync.py:415-470`).

Compute `difference_ms = audio_pop_time_ms - video_pop_time_ms` per matched pop.

### 10.3 Logging points that would be definitive (descriptions only)

If logging were permitted in a future investigation (not in this one), the decisive additions would be:

- Log the **actual OBS timestamps** being submitted:
  - audio: `audio_output.timestamp` at `src/c64-audio.c:245`
  - video: `obs_frame.timestamp` at `src/c64-video.c:717`
- Log the **network receipt timestamp** for the specific packet that produces each pop:
  - receipt time already exists conceptually (`os_gettime_ns()` on recv) but is not currently correlated to output timestamps in a single place.
- Log the **queue depth/backlog**:
  - current head/tail distance for audio and video ring buffers at pop time.

These would immediately discriminate between:

- “audio is scheduled late” (timestamp bias) vs
- “audio data is arriving late” (backlog) vs
- “OBS is delaying audio downstream” (submission timestamp OK, but encoded output shifted).
