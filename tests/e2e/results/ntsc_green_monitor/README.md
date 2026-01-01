# C64 Stream E2E Test Report

## Scenario: NTSC Green Monitor

Generated: 2026-01-01 18:01:28 UTC

## Test configuration

- Format: NTSC
- Frames: 480
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
- RAM: 31Gi total, 22Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 30803/30803 packets (28800 video, 2003 audio)
- ⚠️ Network Timing: span=8022.4ms, video_mean=278.5us, audio_mean=4004.7us
- ✅ Frame Processing: 2481 frames processed
- ✅ Video Recording: 11.0 MB
- ✅ Content Integrity: 22.3s duration

### Resource Usage

During the test's processing window (7.6s, 16 of 49 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 90.6% | 91.3% | 91.59% | 94.9% |
| RAM | 7639.19 MB | 7725.5 MB | 7733.05 MB | 7841.45 MB |
| GPU | 41.66% | 50.09% | 49.27% | 52.19% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8022.442 ms
- Total packets analyzed: 30800

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 30800 | 0.001 ms | 0.521 ms | 46.378 ms | 260.38% | 0.01% | 29.40% | 2017.000 |
| Video | 28799 | 0.001 ms | 0.279 ms | 44.824 ms | 295.79% | 0.01% | 24.89% | 1088.000 |
| Audio | 2001 | 0.002 ms | 4.005 ms | 46.378 ms | 58.74% | 11.54% | 4.45% | 2.799 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 28799 | 0.001 ms | 44.821 ms | 0 |
| Audio | 2001 | 0.982 ms | 42.163 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 29.9ms, max 50.8ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=11153.0ms, video=11132.3ms (frame 666), diff=20.7ms
- 🟢 Pop #2 [R]: audio=11953.0ms, video=11934.6ms (frame 714), diff=18.4ms
- 🟢 Pop #3 [L]: audio=12753.0ms, video=12736.9ms (frame 762), diff=16.1ms
- 🟡 Pop #4 [R]: audio=13558.0ms, video=13522.5ms (frame 809), diff=35.5ms
- 🟢 Pop #5 [L]: audio=14358.0ms, video=14341.6ms (frame 858), diff=16.4ms
- 🟡 Pop #6 [R]: audio=15180.0ms, video=15143.9ms (frame 906), diff=36.1ms
- 🟡 Pop #7 [L]: audio=15985.0ms, video=15946.2ms (frame 954), diff=38.8ms
- 🟡 Pop #8 [R]: audio=16785.0ms, video=16748.6ms (frame 1002), diff=36.4ms
- 🟡 Pop #9 [L]: audio=17585.0ms, video=17534.2ms (frame 1049), diff=50.8ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🔴 Position marker did not cover full range (distinct=2)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 2/6/239/472 | 0/0/0/0 | 1 | 0 |
| After settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- No post-settling repeated/skipped markers detected in playback timeline.

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 22.3 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 666 at 00:11.1 of the 22.3 s video above.
