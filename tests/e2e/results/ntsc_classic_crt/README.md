# C64 Stream E2E Test Report

## Scenario: NTSC Classic CRT

- Generated: 2026-01-23 12:30:30 UTC
- Git Branch: doc/improvements
- Git ID: bd15461
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
- RAM: 31Gi total, 26Gi available
- Disk (/): 1.8T total, 962G available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: Expected 30803, Received 30784, Missing 19 (0.06%)
- ✅ Network Timing: span=8018.7ms, video_mean=441.2us, audio_mean=4004.7us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 10.5 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.0s, 34 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 88.5% | 91.25% | 90.87% | 94% |
| RAM | 4719.35 MB | 4801.71 MB | 4797.47 MB | 4837.8 MB |
| GPU | 0% | 0% | 9% | 56% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8018.698 ms
- Total packets analyzed: 20176

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 20176 | 0.001 ms | 0.795 ms | 13.058 ms | 177.73% | 22.78% | 23.77% | 22.637 |
| Video | 18174 | 0.001 ms | 0.441 ms | 10.730 ms | 177.53% | 24.88% | 15.90% | 13.691 |
| Audio | 2002 | 0.001 ms | 4.005 ms | 13.058 ms | 43.99% | 10.39% | 3.25% | 2.568 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 18174 | 0.057 ms | 10.448 ms | 1 (0.0%) |
| Audio | 2002 | 0.595 ms | 9.057 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 192.1ms, max 813.5ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9836.0ms, video=9828.5ms (frame 588), diff=7.5ms
- 🟢 Pop #2 [R]: audio=10657.0ms, video=10630.8ms (frame 636), diff=26.2ms
- 🟢 Pop #3 [L]: audio=11462.0ms, video=11433.2ms (frame 684), diff=28.8ms
- 🟢 Pop #4 [R]: audio=12263.0ms, video=12235.5ms (frame 732), diff=27.5ms
- 🟢 Pop #5 [L]: audio=13065.0ms, video=13054.5ms (frame 781), diff=10.5ms
- • Pop #6 [R]: audio=13868.0ms, video=13054.5ms (frame 781), diff=813.5ms
- • Pop #7 [L]: audio=14670.0ms, video=15428.1ms (frame 923), diff=758.1ms
- 🟢 Pop #8 [R]: audio=15471.0ms, video=15444.8ms (frame 924), diff=26.2ms
- 🟢 Pop #9 [L]: audio=16278.0ms, video=16247.1ms (frame 972), diff=30.9ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 12/2/3/397 | 11/1/3/5 | 1 | 0 |
| After settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- No post-settling repeated/skipped markers detected in playback timeline.

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 21.1 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 588 at 00:09.8 of the 21.1 s video above.
