# C64 Stream E2E Test Report

Generated: 2025-12-26 15:05:56 UTC

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
- RAM: 31Gi total, 25Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Resource Usage

During the test's processing window (14.1s, 15 samples) (8 cores):

| Metric | Min | Median | Max |
|--------|-----|--------|-----|
| CPU | 46.2% | 49.4% | 84.9% |
| RAM | 4843.21 MB | 5256.06 MB | 5355.46 MB |
| GPU | 30.43% | 89.83% | 98.52% |

- [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 17000 video, 1246 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 28.3ms, max 42.0ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=8097.0ms, video=8100.0ms (frame 405), diff=3.0ms
- 🟡 Pop #2 [R]: audio=9140.0ms, video=9100.0ms (frame 455), diff=40.0ms
- 🟡 Pop #3 [L]: audio=10142.0ms, video=10100.0ms (frame 505), diff=42.0ms

- Channels: LRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 20.1 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)
- Top-left shows the frame index (color = frame_num % 16). Top-right shows a stable 4×4 tile of all 16 VIC colours (drift check). Center shows scrolling colour bars. Bottom-right flashes with a pop sound for A/V sync checks / afterglow tail.
- Taken from frame 405 at 00:08.1 of the 20.1 s video above.
