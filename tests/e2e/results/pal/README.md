# C64 Stream E2E Test Report

Generated: 2025-12-21 16:32:33 UTC

## Test configuration

- Format: PAL
- Frames: 250
- Duration: 5.0 seconds
- Video Port: 11000
- Audio Port: 11001
- OBS Enabled: true

## Build information

- Project: c64stream
- Version: 0.8.2

## Test results

- ✅ Packet Generation: 17000 video, 1246 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 32.5 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)
- Top-left shows the frame index (color = frame_num % 16). Top-right shows a stable 4×4 tile of all 16 VIC colours (drift check). Center shows scrolling colour bars. Bottom-right flashes with a pop sound for A/V sync checks / afterglow tail.
- Taken from the 32.5 s video above.
