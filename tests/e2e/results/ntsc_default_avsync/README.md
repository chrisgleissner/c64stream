# C64 Stream E2E Test Report

## Scenario: NTSC Default A/V Sync

- Generated: 2026-01-22 19:41:20 UTC
- Git Branch: cursor/c64-stream-effects-filter-4ac5
- Git ID: c325f69
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
- RAM: 31Gi total, 19Gi available
- Disk (/): 1.8T total, 972G available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: Expected 30803, Received 30770, Missing 33 (0.11%)
- ✅ Network Timing: span=8013.4ms, video_mean=364.9us, audio_mean=4004.5us
- ✅ Frame Processing: 479 frames processed
- ✅ Video Recording: 10.6 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.2s, 35 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 59.3% | 68.1% | 69.93% | 89.8% |
| RAM | 11759.12 MB | 12314.45 MB | 12193 MB | 12593.2 MB |
| GPU | 0% | 0% | 0% | 0% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8013.391 ms
- Total packets analyzed: 23960

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 23960 | 0.001 ms | 0.669 ms | 19.590 ms | 171.23% | 15.24% | 17.97% | 17.798 |
| Video | 21960 | 0.001 ms | 0.365 ms | 10.060 ms | 129.38% | 16.56% | 10.63% | 9.400 |
| Audio | 2000 | 0.001 ms | 4.004 ms | 19.590 ms | 26.42% | 2.65% | 0.25% | 1.714 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 21960 | 0.033 ms | 9.780 ms | 0 |
| Audio | 2000 | 0.315 ms | 15.593 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 178.4ms, max 804.4ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9864.0ms, video=9861.9ms (frame 590), diff=2.1ms
- 🟢 Pop #2 [R]: audio=10665.0ms, video=10664.3ms (frame 638), diff=0.7ms
- 🟢 Pop #3 [L]: audio=11470.0ms, video=11466.6ms (frame 686), diff=3.4ms
- • Pop #4 [L]: audio=12271.0ms, video=11466.6ms (frame 686), diff=804.4ms
- • Pop #5 [L]: audio=13072.0ms, video=13856.9ms (frame 829), diff=784.9ms
- 🟢 Pop #6 [L]: audio=13877.0ms, video=13873.6ms (frame 830), diff=3.4ms
- 🟢 Pop #7 [R]: audio=14678.0ms, video=14675.9ms (frame 878), diff=2.1ms
- 🟢 Pop #8 [L]: audio=15479.0ms, video=15478.2ms (frame 926), diff=0.8ms
- 🟢 Pop #9 [L]: audio=16284.0ms, video=16280.5ms (frame 974), diff=3.5ms

- Channels: LRLLLLRLL
- 🔁 Channel alternation: MISMATCH

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 21.4 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 590 at 00:09.9 of the 21.4 s video above.
