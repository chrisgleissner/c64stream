# C64 Stream E2E Test Report

Generated: 2025-12-26 15:51:08 UTC

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

During the test's processing window (4.1s, 5 of 15 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 91.4% | 94.6% | 93.9% | 94.9% |
| RAM | 4827.28 MB | 4871.28 MB | 4863.77 MB | 4887.62 MB |
| GPU | 79.74% | 95.65% | 92.47% | 100.0% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 19.1ms, max 26.3ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=8190.0ms, video=8166.7ms (frame 490), diff=23.3ms
- 🟢 Pop #2 [R]: audio=9191.0ms, video=9183.3ms (frame 551), diff=7.7ms
- 🟢 Pop #3 [L]: audio=10193.0ms, video=10166.7ms (frame 610), diff=26.3ms

- Channels: LRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 19.7 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- Top-left shows the frame index (color = frame_num % 16). Top-right shows a stable 4×4 tile of all 16 VIC colours (drift check). Center shows scrolling colour bars. Bottom-right flashes with a pop sound for A/V sync checks / afterglow tail.
- Taken from frame 490 at 00:08.2 of the 19.7 s video above.
