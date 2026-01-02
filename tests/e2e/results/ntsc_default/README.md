# C64 Stream E2E Test Report

## Scenario: NTSC Default

Generated: 2026-01-02 01:01:09 UTC

## Test configuration

- Format: NTSC
- Frames: 300
- Duration: 5.0 seconds
- Video Port: 21000
- Audio Port: 21001
- OBS Enabled: true

## Build information

- Project: c64stream
- Version: 1.0.0

## System information

- OS: Ubuntu 24.04.3 LTS (kernel 6.14.0-37-generic)
- OBS: 32.0.2
- CPU: Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz (8 cores)
- RAM: 31Gi total, 25Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 19252/19252 packets (18000 video, 1252 audio)
- ✅ Network Timing: span=5015.8ms, video_mean=278.6us, audio_mean=4005.2us
- ✅ Frame Processing: 1550 frames processed
- ✅ Video Recording: 9.4 MB
- ✅ Content Integrity: 18.9s duration

### Resource Usage

During the test's processing window (4.6s, 10 of 43 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 52.0% | 60.95% | 60.74% | 77.0% |
| RAM | 5551.56 MB | 5594.48 MB | 5587.81 MB | 5612.58 MB |
| GPU | 39.81% | 40.32% | 40.56% | 41.69% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 5015.811 ms
- Total packets analyzed: 19249

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 19249 | 0.001 ms | 0.521 ms | 7.386 ms | 207.59% | 0.30% | 32.98% | 1170.750 |
| Video | 17999 | 0.001 ms | 0.279 ms | 4.100 ms | 194.66% | 0.32% | 28.33% | 565.500 |
| Audio | 1250 | 1.056 ms | 4.005 ms | 7.386 ms | 21.85% | 1.44% | 0.00% | 1.504 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 17999 | 0.001 ms | 4.096 ms | 0 |
| Audio | 1250 | 0.524 ms | 3.188 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 10.7ms, max 13.3ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=10828.0ms, video=10814.7ms (frame 647), diff=13.3ms
- 🟢 Pop #2 [R]: audio=11628.0ms, video=11617.0ms (frame 695), diff=11.0ms
- 🟢 Pop #3 [L]: audio=12428.0ms, video=12419.3ms (frame 743), diff=8.7ms
- 🟢 Pop #4 [R]: audio=13233.0ms, video=13221.7ms (frame 791), diff=11.3ms
- 🟢 Pop #5 [L]: audio=14033.0ms, video=14024.0ms (frame 839), diff=9.0ms

- Channels: LRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (477 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |
| After settling | 0/0/0/0 | 1/5/5/5 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 10.430–18.403).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 1 | 18.403 | 0.000 | 0.000 | 18.403–18.403 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 18.9 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 647 at 00:10.8 of the 18.9 s video above.
