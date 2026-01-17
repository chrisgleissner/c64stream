# C64 Stream E2E Test Report

## Scenario: NTSC Default

- Generated: 2026-01-17 12:27:45 UTC
- Git Branch: fix/improve-keyboard-mappings
- Git ID: 1d25ef7
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
- RAM: 31Gi total, 25Gi available
- Disk (/): 1.8T total, 1023G available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: Expected 30803, Received 30769, Missing 34 (0.11%)
- ✅ Network Timing: span=8014.3ms, video_mean=343.7us, audio_mean=4004.9us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 10.7 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.3s, 35 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 52.8% | 58.1% | 58.62% | 71.3% |
| RAM | 5288.47 MB | 5423.81 MB | 5409.45 MB | 5450.66 MB |
| GPU | 8% | 22% | 25.54% | 46% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8014.257 ms
- Total packets analyzed: 25319

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 25319 | 0.001 ms | 0.633 ms | 11.274 ms | 169.69% | 10.72% | 15.93% | 16.773 |
| Video | 23319 | 0.001 ms | 0.344 ms | 6.372 ms | 109.77% | 11.63% | 8.74% | 8.278 |
| Audio | 2000 | 0.003 ms | 4.005 ms | 11.274 ms | 19.27% | 1.10% | 0.05% | 1.553 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 23319 | 0.022 ms | 6.091 ms | 0 |
| Audio | 2000 | 0.149 ms | 7.274 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 17.9ms, max 20.2ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9981.0ms, video=9962.2ms (frame 596), diff=18.8ms
- 🟢 Pop #2 [R]: audio=10780.0ms, video=10764.6ms (frame 644), diff=15.4ms
- 🟢 Pop #3 [L]: audio=11587.0ms, video=11566.9ms (frame 692), diff=20.1ms
- 🟢 Pop #4 [R]: audio=12386.0ms, video=12369.2ms (frame 740), diff=16.8ms
- 🟢 Pop #5 [L]: audio=13190.0ms, video=13171.5ms (frame 788), diff=18.5ms
- 🟢 Pop #6 [R]: audio=13993.0ms, video=13973.9ms (frame 836), diff=19.1ms
- 🟢 Pop #7 [L]: audio=14793.0ms, video=14776.2ms (frame 884), diff=16.8ms
- 🟢 Pop #8 [R]: audio=15594.0ms, video=15578.5ms (frame 932), diff=15.5ms
- 🟢 Pop #9 [L]: audio=16401.0ms, video=16380.8ms (frame 980), diff=20.2ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 2/3/3/3 | 3/1/1/2 | 0 | 0 |
| After settling | 0/0/0/0 | 1/2/2/2 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 9.578–20.576).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 5 | 11.834 | 0.033 | 0.100 | 11.784–11.884 |
| 2 | 1 | 17.484 | 0.000 | 0.000 | 17.484–17.484 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 21.6 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 596 at 00:10.0 of the 21.6 s video above.
