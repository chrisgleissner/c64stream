# C64 Stream E2E Test Report

## Scenario: NTSC Default

Generated: 2026-01-08 20:59:07 UTC

## Test configuration

- Format: NTSC
- Frames: 60
- Duration: 1.0 seconds
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

- ⚠️ UDP Packet Reception: 80539/3850 packets (74840 video, 5356 audio, minor loss)
- ⚠️ Network Timing: span=20964.3ms, video_mean=279.5us, audio_mean=3817.8us
- ✅ Frame Processing: 6596 frames processed
- ✅ Video Recording: 7.3 MB
- ✅ Content Integrity: 14.7s duration

### Resource Usage

During the test's processing window (17.1s, 35 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 47.6% | 50.3% | 53.05% | 79.2% |
| RAM | 4104.53 MB | 4479.02 MB | 4386.85 MB | 4499.56 MB |
| GPU | 0.0% | 0.51% | 3.29% | 31.84% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 3600 video, 250 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 20964.295 ms
- Total packets analyzed: 80235

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 80235 | 0.001 ms | 0.516 ms | 29.930 ms | 216.24% | 10.95% | 31.81% | 901.400 |
| Video | 74838 | 0.001 ms | 0.280 ms | 28.258 ms | 224.11% | 11.74% | 27.11% | 594.400 |
| Audio | 5355 | 0.003 ms | 3.818 ms | 29.930 ms | 31.87% | 6.05% | 0.22% | 1.504 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 74838 | 0.002 ms | 28.253 ms | 2596 (3.5%) |
| Audio | 5355 | 0.448 ms | 25.696 ms | 207 (3.9%) |

Details: [network.json](network.json)

### A/V Sync

- ❌ Poor synchronization (0.0%): avg offset 0.0ms, max 0.0ms

#### Sync Details


- Channels: 
- 🔁 Channel alternation: MISMATCH

### Frame Progression

- 🟢 Frame sequence verified (59 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 0/0/0/0 | 4/1/2/5 | 1 | 0 |
| After settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 10.196–11.182).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 4 | 10.777 | 0.284 | 0.803 | 10.363–11.166 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 14.7 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 634 at 00:10.6 of the 14.7 s video above.
