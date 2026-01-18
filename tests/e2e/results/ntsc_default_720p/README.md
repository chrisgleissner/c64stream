# C64 Stream E2E Test Report

## Scenario: NTSC Default 720p

- Generated: 2026-01-18 14:52:09 UTC
- Git Branch: feat/c64script-extension
- Git ID: 3ad75ae
- Environment: local

## Test configuration

- Format: NTSC
- Frames: 480
- Duration: 8.0 seconds
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
- RAM: 31Gi total, 20Gi available
- Disk (/): 1.8T total, 1018G available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: Expected 30803, Received 30777, Missing 26 (0.08%)
- ✅ Network Timing: span=8015.7ms, video_mean=316.2us, audio_mean=4005.0us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 10.6 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.3s, 35 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 44.1% | 51.6% | 52.75% | 81% |
| RAM | 9953.63 MB | 10247.95 MB | 10235.93 MB | 10507.08 MB |
| GPU | 0% | 24% | 25.74% | 51% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8015.734 ms
- Total packets analyzed: 27350

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 27350 | 0.001 ms | 0.586 ms | 7.769 ms | 170.70% | 8.68% | 13.35% | 15.060 |
| Video | 25349 | 0.001 ms | 0.316 ms | 3.878 ms | 80.84% | 9.34% | 6.58% | 5.603 |
| Audio | 2001 | 0.195 ms | 4.005 ms | 7.769 ms | 12.29% | 0.25% | 0.00% | 1.418 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 25349 | 0.026 ms | 3.596 ms | 0 |
| Audio | 2001 | 0.053 ms | 3.806 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 16.2ms, max 18.4ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9761.0ms, video=9744.9ms (frame 583), diff=16.1ms
- 🟢 Pop #2 [R]: audio=10561.0ms, video=10547.3ms (frame 631), diff=13.7ms
- 🟢 Pop #3 [L]: audio=11367.0ms, video=11349.6ms (frame 679), diff=17.4ms
- 🟢 Pop #4 [R]: audio=12168.0ms, video=12151.9ms (frame 727), diff=16.1ms
- 🟢 Pop #5 [L]: audio=12970.0ms, video=12954.2ms (frame 775), diff=15.8ms
- 🟢 Pop #6 [R]: audio=13775.0ms, video=13756.6ms (frame 823), diff=18.4ms
- 🟢 Pop #7 [L]: audio=14576.0ms, video=14558.9ms (frame 871), diff=17.1ms
- 🟢 Pop #8 [R]: audio=15376.0ms, video=15361.2ms (frame 919), diff=14.8ms
- 🟢 Pop #9 [L]: audio=16180.0ms, video=16163.5ms (frame 967), diff=16.5ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- No post-settling repeated/skipped markers detected in playback timeline.

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 21.4 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 583 at 00:09.7 of the 21.4 s video above.
