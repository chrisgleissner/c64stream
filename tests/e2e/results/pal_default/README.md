# C64 Stream E2E Test Report

## Scenario: PAL Default

Generated: 2026-01-01 18:32:08 UTC

## Test configuration

- Format: PAL
- Frames: 400
- Duration: 8.0 seconds
- Video Port: 21000
- Audio Port: 21001
- OBS Enabled: true

## Build information

- Project: c64stream
- Version: 1.0.0

## System information

- OS: Ubuntu 24.04.3 LTS (kernel 6.14.0-37-generic)
- OBS: 32.0.2
- CPU: Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz (8 cores)
- RAM: 31Gi total, 23Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 29194/29194 packets (27200 video, 1994 audio)
- ⚠️ Network Timing: span=7980.0ms, video_mean=293.4us, audio_mean=4001.2us
- ✅ Frame Processing: 2392 frames processed
- ✅ Video Recording: 10.9 MB
- ✅ Content Integrity: 22.0s duration

### Resource Usage

During the test's processing window (7.6s, 16 of 49 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 44.6% | 47.85% | 50.99% | 61.9% |
| RAM | 6573.32 MB | 6600.81 MB | 6600.76 MB | 6632.23 MB |
| GPU | 27.74% | 39.0% | 37.61% | 41.35% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 27200 video, 1994 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 7980.015 ms
- Total packets analyzed: 29191

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 29191 | 0.001 ms | 0.546 ms | 13.954 ms | 199.58% | 0.35% | 34.38% | 1142.000 |
| Video | 27199 | 0.001 ms | 0.293 ms | 13.954 ms | 184.03% | 0.38% | 29.58% | 517.750 |
| Audio | 1992 | 0.003 ms | 4.001 ms | 9.500 ms | 19.96% | 1.05% | 0.05% | 1.445 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 27199 | 0.001 ms | 13.950 ms | 0 |
| Audio | 1992 | 0.382 ms | 5.251 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 4.8ms, max 6.4ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=11057.0ms, video=11052.4ms (frame 554), diff=4.6ms
- 🟢 Pop #2 [R]: audio=12014.0ms, video=12010.0ms (frame 602), diff=4.0ms
- 🟢 Pop #3 [L]: audio=12974.0ms, video=12967.6ms (frame 650), diff=6.4ms
- 🟢 Pop #4 [R]: audio=13929.0ms, video=13925.2ms (frame 698), diff=3.8ms
- 🟢 Pop #5 [L]: audio=14886.0ms, video=14882.8ms (frame 746), diff=3.2ms
- 🟢 Pop #6 [R]: audio=15846.0ms, video=15840.4ms (frame 794), diff=5.6ms
- 🟢 Pop #7 [L]: audio=16804.0ms, video=16798.0ms (frame 842), diff=6.0ms

- Channels: LRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (394 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |
| After settling | 0/0/0/0 | 4/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 10.613–21.546).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 4 | 15.945 | 0.043 | 0.120 | 15.880–16.000 |

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
- Taken from frame 555 at 00:11.1 of the 22.0 s video above.
