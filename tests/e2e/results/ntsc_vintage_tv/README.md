# C64 Stream E2E Test Report

## Scenario: NTSC Vintage TV

- Generated: 2026-01-22 20:05:55 UTC
- Git Branch: cursor/c64-stream-effects-filter-4ac5
- Git ID: c325f69
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
- Disk (/): 1.8T total, 972G available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: Expected 30803, Received 30787, Missing 16 (0.05%)
- ✅ Network Timing: span=8016.8ms, video_mean=445.7us, audio_mean=4004.3us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 10.6 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.3s, 34 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 95.5% | 96.4% | 96.59% | 98.8% |
| RAM | 11914.61 MB | 12446.91 MB | 12400.94 MB | 12820.14 MB |
| GPU | 0% | 0% | 0% | 0% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8016.802 ms
- Total packets analyzed: 19988

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 19988 | 0.001 ms | 0.802 ms | 39.692 ms | 214.15% | 22.04% | 18.54% | 26.191 |
| Video | 17986 | 0.001 ms | 0.446 ms | 27.652 ms | 238.06% | 23.37% | 10.66% | 17.291 |
| Audio | 2002 | 0.001 ms | 4.004 ms | 39.692 ms | 70.41% | 16.08% | 5.09% | 3.620 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 17986 | 0.045 ms | 27.370 ms | 0 |
| Audio | 2002 | 0.743 ms | 35.693 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 96.3ms, max 788.7ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=10004.0ms, video=9995.7ms (frame 598), diff=8.3ms
- 🟢 Pop #2 [R]: audio=10804.0ms, video=10798.0ms (frame 646), diff=6.0ms
- 🟢 Pop #3 [L]: audio=11611.0ms, video=11617.0ms (frame 695), diff=6.0ms
- 🟢 Pop #4 [R]: audio=12412.0ms, video=12402.6ms (frame 742), diff=9.4ms
- 🟢 Pop #5 [L]: audio=13213.0ms, video=13205.0ms (frame 790), diff=8.0ms
- 🟢 Pop #6 [R]: audio=14039.0ms, video=14040.7ms (frame 840), diff=1.7ms
- • Pop #7 [L]: audio=14840.0ms, video=15628.7ms (frame 935), diff=788.7ms
- 🟢 Pop #8 [R]: audio=15642.0ms, video=15645.4ms (frame 936), diff=3.4ms
- 🟢 Pop #9 [L]: audio=16449.0ms, video=16414.3ms (frame 982), diff=34.7ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 5/2/20/423 | 1/1/1/1 | 2 | 0 |
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
- Taken from frame 599 at 00:10.0 of the 21.4 s video above.
