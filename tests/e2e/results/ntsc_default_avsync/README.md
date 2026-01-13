# C64 Stream E2E Test Report

## Scenario: NTSC Default A/V Sync

Generated: 2026-01-12 23:58:08 UTC

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
- RAM: 31Gi total, 27Gi available
- Disk (/): 1.8T total, 1.0T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 30761 packets (28760 video, 2001 audio)
- ✅ Network Timing: span=8012.2ms, video_mean=338.7us, audio_mean=4005.0us
- ✅ Frame Processing: 479 frames processed
- ✅ Video Recording: 9.6 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.2s, 37 of 37 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 50.1% | 52.6% | 53.95% | 81.8% |
| RAM | 3927.93 MB | 4008.48 MB | 4001.07 MB | 4020.56 MB |
| GPU | 30.4% | 55.0% | 58.94% | 100.0% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 30761 (approx)
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8012.195 ms
- Total packets analyzed: 25656

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|----------------|-------------|---------|
| All | 25656 | 0.001 ms | 0.625 ms | 6.557 ms | 169.12% | 9.67% | 15.70% | 16.493 |
| Video | 23656 | 0.001 ms | 0.339 ms | 3.117 ms | 102.11% | 10.48% | 8.58% | 8.140 |
| Audio | 2000 | 1.574 ms | 4.005 ms | 6.557 ms | 17.68% | 1.10% | 0.00% | 1.521 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 23656 | 0.016 ms | 2.838 ms | 0 |
| Audio | 2000 | 0.086 ms | 2.556 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ⚠️ Accuracy: 44.4% (4/9 perfect)

#### Sync Details

- 🟢 Pop #1 [R]: audio=9895.0ms, video=9878.6ms (frame 591), diff=16.4ms
- 🟢 Pop #2 [R]: audio=10696.0ms, video=10681.0ms (frame 639), diff=15.0ms
- 🟢 Pop #3 [R]: audio=11501.0ms, video=11483.3ms (frame 687), diff=17.7ms
- 🟢 Pop #4 [L]: audio=12302.0ms, video=12285.6ms (frame 735), diff=16.4ms
- 🟡 Pop #5 [L]: audio=13124.0ms, video=13088.0ms (frame 783), diff=36.0ms
- 🟡 Pop #6 [L]: audio=13929.0ms, video=13890.3ms (frame 831), diff=38.7ms
- 🟡 Pop #7 [R]: audio=14730.0ms, video=14692.6ms (frame 879), diff=37.4ms
- 🟡 Pop #8 [L]: audio=15531.0ms, video=15494.9ms (frame 927), diff=36.1ms
- 🟡 Pop #9 [L]: audio=16336.0ms, video=16297.3ms (frame 975), diff=38.7ms

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 19.3 s

### Sample Frame

![Sample Frame](./c64_recording_still.png)