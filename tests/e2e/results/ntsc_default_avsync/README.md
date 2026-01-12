# C64 Stream E2E Test Report

## Scenario: ntsc_default_avsync

Generated: 2026-01-12 16:54:17 UTC

## Test configuration

- Format: NTSC
- Frames: 480
- Duration: 8.0 seconds
- Video Port: 21000
- Audio Port: 21001
- OBS Enabled: true

## Build information

- Version: 1.0.2

## System information

- OS: Ubuntu 24.04.3 LTS (kernel 6.14.0-37-generic)
- OBS: 32.0.2
- CPU: Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz (8 cores)
- RAM: 31Gi total, 22Gi available
- Disk (/): 1.8T total, 1.0T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 30740 packets (28739 video, 0 audio)
- ❓ Network Timing: network.json not found
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 9.5 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.2s, 37 of 37 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 66.6% | 87.9% | 85.17% | 93.3% |
| RAM | 6228.35 MB | 6481.16 MB | 6487.83 MB | 6691.91 MB |
| GPU | 30.4% | 89.8% | 79.50% | 100.0% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 30740 (approx)
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ⚠️ Accuracy: 87.5% (7/8 perfect)

#### Sync Details

- 🟢 Pop #1 [L]: audio=9736.0ms, video=9728.2ms (frame 582), diff=7.8ms
- 🟢 Pop #2 [L]: audio=10559.0ms, video=10547.3ms (frame 631), diff=11.7ms
- 🟢 Pop #3 [R]: audio=11364.0ms, video=11332.9ms (frame 678), diff=31.1ms
- 🟢 Pop #4 [L]: audio=12165.0ms, video=12151.9ms (frame 727), diff=13.1ms
- ⚪ Pop #5 [R]: audio=12966.0ms (ignored: unmatched_audio_pop)
- 🟢 Pop #6 [R]: audio=13771.0ms, video=13756.6ms (frame 823), diff=14.4ms
- 🟢 Pop #7 [R]: audio=14572.0ms, video=14558.9ms (frame 871), diff=13.1ms
- 🟢 Pop #8 [R]: audio=15373.0ms, video=15361.2ms (frame 919), diff=11.8ms
- 🟢 Pop #9 [R]: audio=16178.0ms, video=16163.5ms (frame 967), diff=14.5ms

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 19.2 s

### Sample Frame

![Sample Frame](./c64_recording_still.png)