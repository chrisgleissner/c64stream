# C64 Stream E2E Test Report

Generated: 2025-12-21 22:46:02 UTC

## Test configuration

- Format: NTSC
- Frames: 300
- Duration: 5.0 seconds
- Video Port: 11000
- Audio Port: 11001
- OBS Enabled: true

## Build information

- Project: c64stream
- Version: 0.8.3

## Test results

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 9.5 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)
- Top-left shows the frame index (color = frame_num % 16). Top-right shows a stable 4×4 tile of all 16 VIC colours (drift check). Center shows scrolling colour bars. Bottom-right flashes with a pop sound for A/V sync checks / afterglow tail.
- Taken from 00:04.8 of the 9.5 s video above.
