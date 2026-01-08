# C64 Stream E2E Test Report

## Scenario: NTSC Default

Generated: 2026-01-08 23:13:56 UTC

## Test configuration

- Format: NTSC
- Frames: 480
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
- RAM: 31Gi total, 26Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ⚠️ UDP Packet Reception: 13530/30803 packets (12615 video, 862 audio, major loss)
- ❌ Network Timing: span=3523.4ms, video_mean=278.7us, audio_mean=4002.5us
- ✅ Frame Processing: 785 frames processed
- ✅ Video Recording: 18.8 MB
- ✅ Content Integrity: 37.9s duration

### Resource Usage

During the test's processing window (2.1s, 5 of 59 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 50.6% | 55.8% | 59.76% | 72.2% |
| RAM | 4618.21 MB | 4635.19 MB | 4638.59 MB | 4662.71 MB |
| GPU | 4.54% | 12.54% | 16.99% | 45.12% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 3523.384 ms
- Total packets analyzed: 13480

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 13480 | 0.001 ms | 0.517 ms | 7.344 ms | 207.88% | 0.16% | 33.69% | 1165.000 |
| Video | 12613 | 0.001 ms | 0.279 ms | 4.070 ms | 194.68% | 0.17% | 29.13% | 588.250 |
| Audio | 861 | 1.063 ms | 4.002 ms | 7.344 ms | 21.98% | 0.81% | 0.00% | 1.524 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 12613 | 0.001 ms | 4.066 ms | 0 |
| Audio | 861 | 0.506 ms | 3.181 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 10.5ms, max 12.9ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=10940.0ms, video=10931.7ms (frame 654), diff=8.3ms
- 🟢 Pop #2 [R]: audio=11761.0ms, video=11750.7ms (frame 703), diff=10.3ms
- 🟢 Pop #3 [L]: audio=12566.0ms, video=12553.1ms (frame 751), diff=12.9ms

- Channels: LRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (150 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |
| After settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- No post-settling repeated/skipped markers detected in playback timeline.

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 37.9 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 655 at 00:10.9 of the 37.9 s video above.
