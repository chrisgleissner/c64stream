# C64 Stream E2E Test Report

## Scenario: NTSC Default 720p

Generated: 2026-01-07 14:41:34 UTC

## Test configuration

- Format: NTSC
- Frames: 480
- Duration: 8.0 seconds
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
- RAM: 31Gi total, 22Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 30803/30803 packets (28754 video, 1975 audio)
- ✅ Network Timing: span=8023.5ms, video_mean=277.9us, audio_mean=4003.9us
- ✅ Frame Processing: 2481 frames processed
- ✅ Video Recording: 10.9 MB
- ✅ Content Integrity: 22.0s duration

### Resource Usage

During the test's processing window (7.6s, 16 of 49 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 37.2% | 49.15% | 48.39% | 61.1% |
| RAM | 7826.66 MB | 7887.84 MB | 7878.71 MB | 7895.82 MB |
| GPU | 26.98% | 47.78% | 44.52% | 52.57% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8023.504 ms
- Total packets analyzed: 30732

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 30732 | 0.001 ms | 0.517 ms | 21.435 ms | 203.32% | 0.05% | 34.08% | 1108.500 |
| Video | 28752 | 0.001 ms | 0.278 ms | 3.607 ms | 179.89% | 0.05% | 29.56% | 434.750 |
| Audio | 1974 | 0.004 ms | 4.004 ms | 21.435 ms | 19.24% | 0.41% | 0.05% | 1.323 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 28752 | 0.001 ms | 3.603 ms | 0 |
| Audio | 1974 | 0.230 ms | 17.184 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 16.2ms, max 26.0ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=10940.0ms, video=10915.0ms (frame 653), diff=25.0ms
- 🟢 Pop #2 [R]: audio=11740.0ms, video=11717.3ms (frame 701), diff=22.7ms
- 🟢 Pop #3 [L]: audio=12542.0ms, video=12536.4ms (frame 750), diff=5.6ms
- 🟢 Pop #4 [R]: audio=13348.0ms, video=13322.0ms (frame 797), diff=26.0ms
- 🟢 Pop #5 [L]: audio=14148.0ms, video=14141.0ms (frame 846), diff=7.0ms
- 🟢 Pop #6 [R]: audio=14948.0ms, video=14943.3ms (frame 894), diff=4.7ms
- 🟢 Pop #7 [L]: audio=15753.0ms, video=15745.7ms (frame 942), diff=7.3ms
- 🟢 Pop #8 [R]: audio=16556.0ms, video=16531.3ms (frame 989), diff=24.7ms
- 🟢 Pop #9 [L]: audio=17356.0ms, video=17333.6ms (frame 1037), diff=22.4ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟡 Frame sequence OK with jitter (skips=61, back_steps=0) (post-settling)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 47/2/2/2 | 46/1/1/1 | 0 | 0 |
| After settling | 49/2/2/2 | 60/1/1/2 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 10.547–21.529).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 157 | 14.475 | 2.148 | 7.772 | 10.698–18.470 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 22.0 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 653 at 00:10.9 of the 22.0 s video above.
