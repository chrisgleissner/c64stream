# C64 Stream E2E Test Report

Generated: 2025-12-26 15:25:12 UTC

## Test configuration

- Format: PAL
- Frames: 250
- Duration: 5.0 seconds
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
- RAM: 31Gi total, 26Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Resource Usage

During the test's processing window (14.1s, 15 samples):

| Metric | Min | Median | Max |
|--------|-----|--------|-----|
| CPU | 47.3% | 54.2% | 66.2% |
| RAM | 4915.55 MB | 4986.87 MB | 5104.77 MB |
| GPU | 30.43% | 31.91% | 100.0% |

- [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 17000 video, 1246 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 21.0ms, max 21.0ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=8121.0ms, video=8100.0ms (frame 405), diff=21.0ms
- 🟢 Pop #2 [R]: audio=9121.0ms, video=9100.0ms (frame 455), diff=21.0ms
- 🟢 Pop #3 [L]: audio=10121.0ms, video=10100.0ms (frame 505), diff=21.0ms

- Channels: LRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 20.1 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)
- Top-left shows the frame index (color = frame_num % 16). Top-right shows a stable 4×4 tile of all 16 VIC colours (drift check). Center shows scrolling colour bars. Bottom-right flashes with a pop sound for A/V sync checks / afterglow tail.
- Taken from frame 405 at 00:08.1 of the 20.1 s video above.
