# C64 Stream E2E Test Report

## Scenario: NTSC Default A/V Sync

- Generated: 2026-01-15 16:13:28 UTC
- Git Branch: feature/rest-control
- Git ID: 7c1ee20
- Environment: local

## Test configuration

- Format: NTSC
- Frames: 480
- Duration: 8.0 seconds
- Video Port: 21000
- Audio Port: 21001
- OBS Enabled: true

## Build information

- Project: c64stream
- Version: 1.0.3

## System information

- OS: Ubuntu 24.04.3 LTS (kernel 6.14.0-37-generic)
- OBS: 32.0.2
- CPU: Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz (8 cores)
- RAM: 31Gi total, 28Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: Expected 30803, Received 30746, Missing 57 (0.19%)
- ✅ Network Timing: span=8007.6ms, video_mean=334.1us, audio_mean=4005.0us
- ✅ Frame Processing: 479 frames processed
- ✅ Video Recording: 10.6 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.2s, 37 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 47.1% | 48.6% | 49.48% | 63% |
| RAM | 2743.52 MB | 2830.89 MB | 2822.84 MB | 2849.11 MB |
| GPU | 30.43% | 89.83% | 74.18% | 100% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8007.647 ms
- Total packets analyzed: 25969

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 25969 | 0.001 ms | 0.617 ms | 6.292 ms | 169.97% | 8.09% | 14.90% | 16.561 |
| Video | 23970 | 0.001 ms | 0.334 ms | 2.986 ms | 99.15% | 8.77% | 7.83% | 7.932 |
| Audio | 1999 | 1.690 ms | 4.005 ms | 6.292 ms | 17.95% | 0.75% | 0.00% | 1.509 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 23970 | 0.012 ms | 2.707 ms | 0 |
| Audio | 1999 | 0.072 ms | 2.311 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ❌ Poor synchronization (22.2%): avg offset 28.3ms, max 34.2ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9857.0ms, video=9845.2ms (frame 589), diff=11.8ms
- 🟢 Pop #2 [R]: audio=10658.0ms, video=10647.5ms (frame 637), diff=10.5ms
- 🟢 Pop #3 [L]: audio=11484.0ms, video=11449.9ms (frame 685), diff=34.1ms
- 🟢 Pop #4 [R]: audio=12286.0ms, video=12252.2ms (frame 733), diff=33.8ms
- 🟢 Pop #5 [L]: audio=13086.0ms, video=13054.5ms (frame 781), diff=31.5ms
- 🟢 Pop #6 [L]: audio=13891.0ms, video=13856.9ms (frame 829), diff=34.1ms
- 🟢 Pop #7 [L]: audio=14692.0ms, video=14659.2ms (frame 877), diff=32.8ms
- 🟢 Pop #8 [L]: audio=15493.0ms, video=15461.5ms (frame 925), diff=31.5ms
- 🟢 Pop #9 [L]: audio=16298.0ms, video=16263.8ms (frame 973), diff=34.2ms

- Channels: LRLRLLLLL
- 🔁 Channel alternation: MISMATCH

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 21.3 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 589 at 00:09.8 of the 21.3 s video above.
