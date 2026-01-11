# C64 Stream E2E Test Report

## Scenario: NTSC Default

Generated: 2026-01-11 15:43:00 UTC

## Test configuration

- Format: NTSC
- Frames: 120
- Duration: 2.0 seconds
- Video Port: 21000
- Audio Port: 21001
- OBS Enabled: true

## Build information

- Project: c64stream
- Version: 1.0.3

## System information

- OS: Ubuntu 24.04.3 LTS (kernel 6.14.0-37-generic)
- OBS: 32.0.2
- CPU: Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz (8 cores)
- RAM: 31Gi total, 26Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ⚠️ UDP Packet Reception: 30796/30803 packets (28793 video, 2003 audio, minor loss)
- ✅ Network Timing: span=8029.1ms, video_mean=333.5us, audio_mean=4005.3us
- ✅ Frame Processing: 2481 frames processed
- ✅ Video Recording: 6.9 MB
- ✅ Content Integrity: 14.0s duration

### Resource Usage

During the test's processing window (10.1s, 21 of 33 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 50.9% | 56.4% | 57.62% | 74.0% |
| RAM | 6142.12 MB | 6398.02 MB | 6371.28 MB | 6513.72 MB |
| GPU | 9.75% | 11.09% | 11.03% | 12.85% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 7200 video, 500 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8029.085 ms
- Total packets analyzed: 26057

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 26057 | 0.001 ms | 0.616 ms | 7.433 ms | 172.57% | 14.04% | 15.65% | 16.811 |
| Video | 24055 | 0.001 ms | 0.333 ms | 4.725 ms | 111.33% | 15.19% | 8.66% | 8.125 |
| Audio | 2002 | 0.080 ms | 4.005 ms | 7.433 ms | 19.09% | 0.95% | 0.00% | 1.530 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 24055 | 0.023 ms | 4.446 ms | 0 |
| Audio | 2002 | 0.169 ms | 3.920 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 272.7ms, max 1572.4ms

#### Sync Details

- • Pop #1 [L]: audio=2924.0ms, video=4496.4ms (frame 269), diff=1572.4ms
- • Pop #2 [R]: audio=3725.0ms, video=4496.4ms (frame 269), diff=771.4ms
- 🟢 Pop #3 [L]: audio=4530.0ms, video=4513.1ms (frame 270), diff=16.9ms
- 🟢 Pop #4 [R]: audio=5331.0ms, video=5315.4ms (frame 318), diff=15.6ms
- 🟢 Pop #5 [L]: audio=6132.0ms, video=6117.7ms (frame 366), diff=14.3ms
- 🟢 Pop #6 [R]: audio=6937.0ms, video=6920.1ms (frame 414), diff=16.9ms
- 🟢 Pop #7 [L]: audio=7738.0ms, video=7722.4ms (frame 462), diff=15.6ms
- 🟢 Pop #8 [R]: audio=8539.0ms, video=8524.7ms (frame 510), diff=14.3ms
- 🟢 Pop #9 [L]: audio=9344.0ms, video=9327.0ms (frame 558), diff=17.0ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 20/2/2/2 | 22/1/1/1 | 0 | 0 |
| After settling | 20/2/2/2 | 25/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 2.524–13.523).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 30 | 8.373 | 1.134 | 4.362 | 6.118–10.480 |
| 2 | 5 | 5.028 | 0.221 | 0.585 | 4.831–5.416 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 14.0 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 270 at 00:04.5 of the 14.0 s video above.
