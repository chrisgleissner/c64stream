# C64 Stream E2E Test Report

Generated: 2025-12-26 14:59:30 UTC

## Test configuration

- Format: NTSC
- Frames: 300
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
| CPU | 56.5% | 60.5% | 78.3% |
| RAM | 4952.32 MB | 5093.06 MB | 5131.24 MB |
| GPU | 30.43% | 91.3% | 100.0% |

- [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 14.0ms, max 28.0ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=8057.0ms, video=8050.0ms (frame 483), diff=7.0ms
- 🟢 Pop #2 [R]: audio=9057.0ms, video=9050.0ms (frame 543), diff=7.0ms
- 🟢 Pop #3 [L]: audio=10078.0ms, video=10050.0ms (frame 603), diff=28.0ms

- Channels: LRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 19.2 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)
- Top-left shows the frame index (color = frame_num % 16). Top-right shows a stable 4×4 tile of all 16 VIC colours (drift check). Center shows scrolling colour bars. Bottom-right flashes with a pop sound for A/V sync checks / afterglow tail.
- Taken from frame 483 at 00:08.0 of the 19.2 s video above.
