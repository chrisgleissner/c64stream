# C64 Stream E2E Test Report

## Scenario: PAL Default

Generated: 2026-01-07 12:13:25 UTC

## Test configuration

- Format: PAL
- Frames: 400
- Duration: 8.0 seconds
- Video Port: 21000
- Audio Port: 21001
- OBS Enabled: true

## Build information

- Project: c64stream
- Version: 1.0.2

## System information

- OS: Ubuntu 24.04.3 LTS (kernel 6.14.0-37-generic)
- OBS: 32.0.2
- CPU: Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz (8 cores)
- RAM: 31Gi total, 23Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 29194/29194 packets (27141 video, 1955 audio)
- ✅ Network Timing: span=7979.2ms, video_mean=293.4us, audio_mean=3998.5us
- ✅ Frame Processing: 2392 frames processed
- ✅ Video Recording: 10.9 MB
- ✅ Content Integrity: 22.0s duration

### Resource Usage

During the test's processing window (7.6s, 16 of 49 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 46.8% | 57.3% | 56.48% | 70.9% |
| RAM | 6258.38 MB | 6312.66 MB | 6299.14 MB | 6324.02 MB |
| GPU | 10.71% | 47.85% | 41.18% | 59.39% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 27200 video, 1994 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 7979.215 ms
- Total packets analyzed: 29095

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 29095 | 0.001 ms | 0.542 ms | 11.909 ms | 204.63% | 8.81% | 35.69% | 914.800 |
| Video | 27139 | 0.001 ms | 0.293 ms | 11.874 ms | 197.08% | 0.12% | 33.32% | 541.750 |
| Audio | 1954 | 0.005 ms | 3.998 ms | 11.909 ms | 23.55% | 2.00% | 0.26% | 1.596 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 27139 | 0.002 ms | 11.870 ms | 0 |
| Audio | 1954 | 0.377 ms | 7.665 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 140.4ms, max 928.4ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=11062.0ms, video=11052.4ms (frame 554), diff=9.6ms
- 🟢 Pop #2 [R]: audio=12017.0ms, video=12010.0ms (frame 602), diff=7.0ms
- 🟢 Pop #3 [L]: audio=12977.0ms, video=12967.6ms (frame 650), diff=9.4ms
- 🟢 Pop #4 [R]: audio=13934.0ms, video=13925.2ms (frame 698), diff=8.8ms
- • Pop #5 [L]: audio=14892.0ms, video=15820.4ms (frame 793), diff=928.4ms
- 🟢 Pop #6 [R]: audio=15852.0ms, video=15840.4ms (frame 794), diff=11.6ms
- 🟢 Pop #7 [L]: audio=16806.0ms, video=16798.0ms (frame 842), diff=8.0ms

- Channels: LRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (400 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 7/2/2/2 | 5/1/1/2 | 0 | 0 |
| After settling | 3/2/2/2 | 5/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 10.594–21.526).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 6 | 14.694 | 0.168 | 0.419 | 14.524–14.943 |
| 2 | 4 | 10.683 | 0.022 | 0.060 | 10.653–10.713 |
| 3 | 2 | 17.696 | 0.020 | 0.040 | 17.676–17.716 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 22.0 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 554 at 00:11.1 of the 22.0 s video above.
