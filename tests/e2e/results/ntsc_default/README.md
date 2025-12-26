# C64 Stream E2E Test Report

Generated: 2025-12-26 14:00:21 UTC

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
- RAM: 31Gi total, 26Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Resource Usage

During the test's processing window (14.2s, 15 samples) (8 cores):

- CPU: 66.9% median (max: 79.0%)
- OBS CPU: 53.16% median (max: 61.05%)
- RAM: 4657.33 MB median
- GPU: 89.83% median (max: 100.0%)
- [resource.csv](resource.csv) | [resource.json](resource.json)

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

### A/V Sync

- ❌ Poor synchronization (0.0%): avg offset 65.6ms, max 76.7ms

#### Sync Details

- 🔴 Pop #1 [L]: audio=8110.0ms, video=8033.3ms (frame 482), diff=76.7ms
- 🔴 Pop #2 [R]: audio=9110.0ms, video=9050.0ms (frame 543), diff=60.0ms
- 🔴 Pop #3 [L]: audio=10110.0ms, video=10050.0ms (frame 603), diff=60.0ms

- Channels: LRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 19.2 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)
- Top-left shows the frame index (color = frame_num % 16). Top-right shows a stable 4×4 tile of all 16 VIC colours (drift check). Center shows scrolling colour bars. Bottom-right flashes with a pop sound for A/V sync checks / afterglow tail.
- Taken from the 19.2 s video above.
