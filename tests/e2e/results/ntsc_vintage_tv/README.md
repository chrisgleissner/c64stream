# C64 Stream E2E Test Report

## Scenario: NTSC Vintage TV

- Generated: 2026-01-22 23:15:33 UTC
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
- RAM: 31Gi total, 20Gi available
- Disk (/): 1.8T total, 964G available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: Expected 30803, Received 30737, Missing 66 (0.21%)
- ✅ Network Timing: span=8008.0ms, video_mean=447.2us, audio_mean=4002.2us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 10.7 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.6s, 34 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 94% | 95.25% | 95.45% | 98.9% |
| RAM | 10146.94 MB | 10291.48 MB | 10276.84 MB | 10406 MB |
| GPU | 11% | 27% | 28.06% | 58% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8007.990 ms
- Total packets analyzed: 19903

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 19903 | 0.001 ms | 0.804 ms | 32.966 ms | 218.78% | 25.83% | 19.73% | 25.795 |
| Video | 17905 | 0.001 ms | 0.447 ms | 28.356 ms | 245.29% | 27.48% | 12.21% | 17.471 |
| Audio | 1998 | 0.001 ms | 4.002 ms | 32.966 ms | 73.60% | 17.27% | 4.80% | 3.716 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 17905 | 0.062 ms | 28.078 ms | 0 |
| Audio | 1998 | 0.783 ms | 28.968 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 265.1ms, max 778.9ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9989.0ms, video=9995.7ms (frame 598), diff=6.7ms
- • Pop #2 [R]: audio=10788.0ms, video=11566.9ms (frame 692), diff=778.9ms
- 🟢 Pop #3 [L]: audio=11594.0ms, video=11583.6ms (frame 693), diff=10.4ms
- 🟢 Pop #4 [R]: audio=12395.0ms, video=12385.9ms (frame 741), diff=9.1ms
- 🟢 Pop #5 [L]: audio=13197.0ms, video=13188.2ms (frame 789), diff=8.8ms
- • Pop #6 [R]: audio=14001.0ms, video=14776.2ms (frame 884), diff=775.2ms
- 🟢 Pop #7 [L]: audio=14801.0ms, video=14792.9ms (frame 885), diff=8.1ms
- • Pop #8 [R]: audio=15602.0ms, video=16380.8ms (frame 980), diff=778.8ms
- 🟢 Pop #9 [L]: audio=16408.0ms, video=16397.6ms (frame 981), diff=10.4ms

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
- Duration: 21.7 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 598 at 00:10.0 of the 21.7 s video above.
