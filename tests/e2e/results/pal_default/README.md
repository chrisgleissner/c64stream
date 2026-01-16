# C64 Stream E2E Test Report

## Scenario: PAL Default

- Generated: 2026-01-15 16:30:07 UTC
- Git Branch: feature/rest-control
- Git ID: 7c1ee20
- Environment: local

## Test configuration

- Format: PAL
- Frames: 400
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

- ✅ UDP Packet Reception: Expected 29194, Received 29127, Missing 67 (0.23%)
- ✅ Network Timing: span=7961.2ms, video_mean=336.2us, audio_mean=4001.4us
- ✅ Frame Processing: 400 frames processed
- ✅ Video Recording: 10.5 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.1s, 37 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 39.4% | 43.6% | 43.85% | 54.3% |
| RAM | 2731.14 MB | 2804.6 MB | 2795.86 MB | 2815.32 MB |
| GPU | 30.43% | 89.83% | 74.22% | 95.65% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 27200 video, 1994 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 7961.235 ms
- Total packets analyzed: 25669

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 25669 | 0.001 ms | 0.620 ms | 6.611 ms | 165.72% | 7.30% | 13.74% | 14.907 |
| Video | 23680 | 0.001 ms | 0.336 ms | 3.119 ms | 83.83% | 7.90% | 6.52% | 6.528 |
| Audio | 1989 | 1.348 ms | 4.001 ms | 6.611 ms | 13.57% | 0.35% | 0.00% | 1.431 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 23680 | 0.017 ms | 2.831 ms | 0 |
| Audio | 1989 | 0.038 ms | 2.651 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 14.0ms, max 16.2ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9929.0ms, video=9915.2ms (frame 497), diff=13.8ms
- 🟢 Pop #2 [R]: audio=10887.0ms, video=10872.8ms (frame 545), diff=14.2ms
- 🟢 Pop #3 [L]: audio=11843.0ms, video=11830.4ms (frame 593), diff=12.6ms
- 🟢 Pop #4 [R]: audio=12803.0ms, video=12788.0ms (frame 641), diff=15.0ms
- 🟢 Pop #5 [L]: audio=13759.0ms, video=13745.6ms (frame 689), diff=13.4ms
- 🟢 Pop #6 [R]: audio=14716.0ms, video=14703.2ms (frame 737), diff=12.8ms
- 🟢 Pop #7 [L]: audio=15677.0ms, video=15660.8ms (frame 785), diff=16.2ms

- Channels: LRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (400 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 1/2/2/2 | 1/1/1/1 | 0 | 0 |
| After settling | 0/0/0/0 | 1/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 9.480–20.460).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 1 | 11.660 | 0.000 | 0.000 | 11.660–11.660 |
| 2 | 1 | 16.680 | 0.000 | 0.000 | 16.680–16.680 |

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
- Taken from frame 497 at 00:09.9 of the 21.3 s video above.
