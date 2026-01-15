# C64 Stream E2E Test Report

## Scenario: NTSC Green Monitor

- Generated: 2026-01-15 16:20:19 UTC
- Git Branch: feature/rest-control
- Git ID: 7c1ee20
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
- RAM: 31Gi total, 27Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: Expected 30803, Received 30764, Missing 39 (0.13%)
- ✅ Network Timing: span=8016.6ms, video_mean=432.6us, audio_mean=4004.2us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 10.6 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.2s, 37 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 88.4% | 91.1% | 90.8% | 93.7% |
| RAM | 2725.43 MB | 2806 MB | 2792.94 MB | 2815.07 MB |
| GPU | 30.43% | 89.83% | 66.27% | 100% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8016.555 ms
- Total packets analyzed: 20530

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 20530 | 0.001 ms | 0.781 ms | 12.919 ms | 180.12% | 19.89% | 22.39% | 22.662 |
| Video | 18530 | 0.001 ms | 0.433 ms | 10.882 ms | 177.17% | 21.55% | 14.55% | 13.290 |
| Audio | 2000 | 0.001 ms | 4.004 ms | 12.919 ms | 45.59% | 10.45% | 3.65% | 2.541 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 18530 | 0.049 ms | 10.599 ms | 0 |
| Audio | 2000 | 0.613 ms | 8.919 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 0.8ms, max 2.5ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9844.0ms, video=9845.2ms (frame 589), diff=1.2ms
- 🟢 Pop #2 [R]: audio=10646.0ms, video=10647.5ms (frame 637), diff=1.5ms
- 🟢 Pop #3 [L]: audio=11450.0ms, video=11449.9ms (frame 685), diff=0.1ms
- 🟢 Pop #4 [R]: audio=12252.0ms, video=12252.2ms (frame 733), diff=0.2ms
- 🟢 Pop #5 [L]: audio=13052.0ms, video=13054.5ms (frame 781), diff=2.5ms
- 🟢 Pop #6 [R]: audio=13857.0ms, video=13856.9ms (frame 829), diff=0.1ms
- 🟢 Pop #7 [L]: audio=14659.0ms, video=14659.2ms (frame 877), diff=0.2ms
- 🟢 Pop #8 [R]: audio=15461.0ms, video=15461.5ms (frame 925), diff=0.5ms
- 🟢 Pop #9 [L]: audio=16265.0ms, video=16263.8ms (frame 973), diff=1.2ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 7/4/6/434 | 6/1/5/5 | 1 | 0 |
| After settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- No post-settling repeated/skipped markers detected in playback timeline.

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 21.3 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 589 at 00:09.8 of the 21.3 s video above.
