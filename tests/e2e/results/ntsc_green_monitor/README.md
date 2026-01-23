# C64 Stream E2E Test Report

## Scenario: NTSC Green Monitor

- Generated: 2026-01-22 23:02:21 UTC
- Git Branch: cursor/c64-stream-effects-filter-4ac5
- Git ID: 912712e
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
- RAM: 31Gi total, 19Gi available
- Disk (/): 1.8T total, 964G available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: Expected 30803, Received 30793, Missing 10 (0.03%)
- ✅ Network Timing: span=8020.2ms, video_mean=479.3us, audio_mean=4004.7us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 10.6 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.3s, 34 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 93.2% | 95.45% | 95.64% | 99.4% |
| RAM | 10653.83 MB | 10834.97 MB | 10962.88 MB | 11432.85 MB |
| GPU | 0% | 29.5% | 25.26% | 53% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8020.159 ms
- Total packets analyzed: 18734

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 18734 | 0.001 ms | 0.856 ms | 36.689 ms | 212.01% | 29.55% | 21.85% | 31.330 |
| Video | 16732 | 0.001 ms | 0.479 ms | 36.689 ms | 244.83% | 31.27% | 14.68% | 20.791 |
| Audio | 2002 | 0.001 ms | 4.005 ms | 25.548 ms | 71.57% | 17.83% | 6.84% | 3.783 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 16732 | 0.109 ms | 36.431 ms | 0 |
| Audio | 2002 | 0.935 ms | 21.558 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 97.5ms, max 787.4ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9945.0ms, video=9945.5ms (frame 595), diff=0.5ms
- • Pop #2 [R]: audio=10746.0ms, video=11533.4ms (frame 690), diff=787.4ms
- 🟢 Pop #3 [L]: audio=11551.0ms, video=11550.2ms (frame 691), diff=0.8ms
- 🟢 Pop #4 [R]: audio=12353.0ms, video=12335.8ms (frame 738), diff=17.2ms
- 🟢 Pop #5 [L]: audio=13154.0ms, video=13154.8ms (frame 787), diff=0.8ms
- 🟢 Pop #6 [R]: audio=13959.0ms, video=13940.4ms (frame 834), diff=18.6ms
- 🟢 Pop #7 [L]: audio=14759.0ms, video=14742.8ms (frame 882), diff=16.2ms
- 🟢 Pop #8 [R]: audio=15561.0ms, video=15528.4ms (frame 929), diff=32.6ms
- 🟢 Pop #9 [L]: audio=16367.0ms, video=16364.1ms (frame 979), diff=2.9ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 7/4/6/431 | 6/1/5/5 | 1 | 0 |
| After settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |

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
- Taken from frame 595 at 00:09.9 of the 21.4 s video above.
