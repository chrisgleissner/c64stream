# C64 Stream E2E Test Report

## Scenario: NTSC Green Monitor

- Generated: 2026-01-17 17:10:53 UTC
- Git Branch: fix/improve-keyboard-mappings
- Git ID: 5886543
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
- RAM: 31Gi total, 24Gi available
- Disk (/): 1.8T total, 1022G available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: Expected 30803, Received 30742, Missing 61 (0.2%)
- ✅ Network Timing: span=8010.0ms, video_mean=444.9us, audio_mean=4005.2us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 10.7 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.4s, 34 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 92.1% | 93.75% | 93.8% | 97.4% |
| RAM | 6957.96 MB | 7033.08 MB | 7033.3 MB | 7093.91 MB |
| GPU | 15% | 19% | 25.5% | 43% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8009.987 ms
- Total packets analyzed: 20004

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 20004 | 0.001 ms | 0.801 ms | 37.767 ms | 204.05% | 28.17% | 21.95% | 24.585 |
| Video | 18005 | 0.001 ms | 0.445 ms | 27.811 ms | 217.44% | 29.89% | 14.95% | 16.061 |
| Audio | 1999 | 0.001 ms | 4.005 ms | 37.767 ms | 65.45% | 14.66% | 5.10% | 3.450 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 18005 | 0.103 ms | 27.547 ms | 0 |
| Audio | 1999 | 0.756 ms | 33.772 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Acceptable synchronization (87.5%): avg offset 102.7ms, max 781.9ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9983.0ms, video=9978.9ms (frame 597), diff=4.1ms
- 🟢 Pop #2 [R]: audio=10783.0ms, video=10764.6ms (frame 644), diff=18.4ms
- 🟢 Pop #3 [L]: audio=11589.0ms, video=11566.9ms (frame 692), diff=22.1ms
- 🟢 Pop #4 [R]: audio=12391.0ms, video=12369.2ms (frame 740), diff=21.8ms
- • Pop #5 [L]: audio=13192.0ms, video=13973.9ms (frame 836), diff=781.9ms
- 🟢 Pop #6 [R]: audio=13996.0ms, video=13990.6ms (frame 837), diff=5.4ms
- 🟢 Pop #7 [L]: audio=14797.0ms, video=14776.2ms (frame 884), diff=20.8ms
- 🟢 Pop #8 [R]: audio=15599.0ms, video=15595.2ms (frame 933), diff=3.8ms
- 🟡 Pop #9 [L]: audio=16427.0ms, video=16380.8ms (frame 980), diff=46.2ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 5/6/17/422 | 6/1/5/5 | 0 | 0 |
| After settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- No post-settling repeated/skipped markers detected in playback timeline.

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 21.5 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 597 at 00:10.0 of the 21.5 s video above.
