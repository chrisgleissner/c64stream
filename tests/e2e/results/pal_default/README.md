# C64 Stream E2E Test Report

## Scenario: PAL Default

Generated: 2026-01-01 22:26:15 UTC

## Test configuration

- Format: PAL
- Frames: 250
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
- RAM: 31Gi total, 26Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 18246/18246 packets (17000 video, 1246 audio)
- ✅ Network Timing: span=4987.0ms, video_mean=293.4us, audio_mean=4000.8us
- ✅ Frame Processing: 1494 frames processed
- ✅ Video Recording: 9.3 MB
- ✅ Content Integrity: 18.9s duration

### Resource Usage

During the test's processing window (4.6s, 10 of 43 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 43.2% | 45.2% | 46.01% | 54.4% |
| RAM | 4213.06 MB | 4223.31 MB | 4231.66 MB | 4254.77 MB |
| GPU | 20.53% | 31.94% | 30.88% | 32.51% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 17000 video, 1246 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 4987.029 ms
- Total packets analyzed: 18243

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 18243 | 0.001 ms | 0.546 ms | 29.330 ms | 204.31% | 0.07% | 35.01% | 1123.500 |
| Video | 16998 | 0.001 ms | 0.293 ms | 29.330 ms | 194.44% | 0.07% | 30.27% | 507.750 |
| Audio | 1245 | 0.003 ms | 4.001 ms | 26.425 ms | 25.14% | 1.04% | 0.08% | 1.416 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 16998 | 0.001 ms | 29.326 ms | 0 |
| Audio | 1245 | 0.316 ms | 22.175 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 31.7ms, max 32.9ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=10884.0ms, video=10852.9ms (frame 544), diff=31.1ms
- 🟢 Pop #2 [R]: audio=11841.0ms, video=11810.5ms (frame 592), diff=30.5ms
- 🟢 Pop #3 [L]: audio=12801.0ms, video=12768.1ms (frame 640), diff=32.9ms
- 🟢 Pop #4 [R]: audio=13758.0ms, video=13725.7ms (frame 688), diff=32.3ms

- Channels: LRLR
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (398 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 1/2/2/2 | 1/1/1/1 | 0 | 0 |
| After settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 10.394–18.334).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 2 | 10.534 | 0.020 | 0.040 | 10.514–10.554 |

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
- Taken from frame 544 at 00:10.9 of the 18.9 s video above.
