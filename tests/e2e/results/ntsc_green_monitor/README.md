# C64 Stream E2E Test Report

Generated: 2025-12-27 19:21:35 UTC

## Test configuration

- Format: NTSC
- Frames: 180
- Duration: 3.0 seconds
- Video Port: 11000
- Audio Port: 11001
- OBS Enabled: true

## Build information

- Project: c64stream
- Version: 1.0.0

## System information

- OS: Ubuntu 24.04.3 LTS (kernel 6.14.0-37-generic)
- OBS: 32.0.2
- CPU: Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz (8 cores)
- RAM: 31Gi total, 23Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Resource Usage

During the test's processing window (1.1s, 3 of 26 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 99.8% | 100.0% | 99.93% | 100.0% |
| RAM | 6649.61 MB | 6691.9 MB | 6684.39 MB | 6711.67 MB |
| GPU | 0.82% | 12.79% | 12.75% | 24.66% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ❌ Poor synchronization (0.0%): avg offset 95.3ms, max 95.3ms

#### Sync Details

- 🔴 Pop #1 [L]: audio=8620.0ms, video=8524.7ms (frame 510), diff=95.3ms

- Channels: L
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟡 High frame repetition (72% stuck)

| Metric | Count | Min | Median | Max |
|--------|-------|-----|--------|-----|
| Repeated frames | 12 runs | 2 | 6 | 14 |
| Skipped frames | 7 skips | 1 | 1 | 3 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

- ↩️ Back steps: 2 (frame counter went backwards)

- ⚡ Severe jumps: 3 (large sequence discontinuity)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 16.2 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- Top-left shows the frame index (color = frame_num % 16). Top-right shows a stable 4×4 tile of all 16 VIC colours (drift check). Center shows scrolling colour bars. Bottom-right flashes with a pop sound for A/V sync checks / afterglow tail.
- Taken from frame 510 at 00:08.5 of the 16.2 s video above.
