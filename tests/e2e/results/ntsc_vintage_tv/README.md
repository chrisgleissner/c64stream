# C64 Stream E2E Test Report

## Scenario: NTSC Vintage TV

Generated: 2026-01-01 18:30:59 UTC

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
- RAM: 31Gi total, 23Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ⚠️ UDP Packet Reception: 30802/30803 packets (28800 video, 2002 audio, minor loss)
- ⚠️ Network Timing: span=8022.6ms, video_mean=278.5us, audio_mean=4005.3us
- ✅ Frame Processing: 2481 frames processed
- ✅ Video Recording: 11.0 MB
- ✅ Content Integrity: 22.2s duration

### Resource Usage

During the test's processing window (7.7s, 16 of 49 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 94.0% | 95.0% | 95.11% | 97.7% |
| RAM | 6560.7 MB | 6607.77 MB | 6602.96 MB | 6619.92 MB |
| GPU | 22.08% | 37.7% | 33.45% | 39.05% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8022.618 ms
- Total packets analyzed: 30799

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 30799 | 0.001 ms | 0.521 ms | 45.268 ms | 276.97% | 0.02% | 27.24% | 2120.667 |
| Video | 28799 | 0.001 ms | 0.279 ms | 44.731 ms | 327.40% | 0.02% | 22.73% | 1259.000 |
| Audio | 2000 | 0.002 ms | 4.005 ms | 45.268 ms | 66.28% | 14.10% | 4.35% | 3.163 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 28799 | 0.001 ms | 44.728 ms | 0 |
| Audio | 2000 | 1.033 ms | 41.036 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 94.8ms, max 778.2ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=11124.0ms, video=11115.6ms (frame 665), diff=8.4ms
- 🟢 Pop #2 [R]: audio=11924.0ms, video=11917.9ms (frame 713), diff=6.1ms
- 🟢 Pop #3 [L]: audio=12745.0ms, video=12736.9ms (frame 762), diff=8.1ms
- 🟢 Pop #4 [R]: audio=13550.0ms, video=13556.0ms (frame 811), diff=6.0ms
- 🟢 Pop #5 [L]: audio=14350.0ms, video=14341.6ms (frame 858), diff=8.4ms
- 🟢 Pop #6 [R]: audio=15150.0ms, video=15127.2ms (frame 905), diff=22.8ms
- 🟢 Pop #7 [L]: audio=15956.0ms, video=15946.2ms (frame 954), diff=9.8ms
- • Pop #8 [R]: audio=16756.0ms, video=17534.2ms (frame 1049), diff=778.2ms
- 🟢 Pop #9 [L]: audio=17556.0ms, video=17550.9ms (frame 1050), diff=5.1ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🔴 Too few valid marker samples

- Settling: 0s (pass/fail uses post-settling only)

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- No post-settling repeated/skipped markers detected in playback timeline.

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 22.2 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 665 at 00:11.1 of the 22.2 s video above.
