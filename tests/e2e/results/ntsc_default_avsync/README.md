# C64 Stream E2E Test Report

## Scenario: ntsc_default_avsync

Generated: 2026-01-12 17:06:18 UTC

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

- ✅ UDP Packet Reception: 30756 packets (28756 video, 0 audio)
- ✅ Network Timing: span=8010.8ms, video_mean=336.3us, audio_mean=4004.6us
- ✅ Frame Processing: 479 frames processed
- ✅ Video Recording: 9.5 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.2s, 37 of 37 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 47.3% | 49.5% | 50.41% | 67.0% |
| RAM | 5814.96 MB | 5876.18 MB | 5873.01 MB | 5904.04 MB |
| GPU | 30.4% | 89.8% | 76.69% | 100.0% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 30756 (approx)
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8010.755 ms
- Total packets analyzed: 25822

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|----------------|-------------|---------|
| All | 25822 | 0.001 ms | 0.620 ms | 6.421 ms | 169.44% | 8.37% | 15.16% | 16.429 |
| Video | 23823 | 0.001 ms | 0.336 ms | 3.239 ms | 100.95% | 9.08% | 8.07% | 8.086 |
| Audio | 1999 | 1.680 ms | 4.005 ms | 6.421 ms | 17.28% | 0.45% | 0.00% | 1.509 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 23823 | 0.012 ms | 2.960 ms | 0 |
| Audio | 1999 | 0.069 ms | 2.420 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ⚠️ Accuracy: 12.5% (1/8 perfect)

#### Sync Details

- 🟢 Pop #1 [L]: audio=9779.0ms, video=9761.6ms (frame 584), diff=17.4ms
- 🟡 Pop #2 [R]: audio=10601.0ms, video=10564.0ms (frame 632), diff=37.0ms
- 🟡 Pop #3 [L]: audio=11406.0ms, video=11366.3ms (frame 680), diff=39.7ms
- 🟡 Pop #4 [R]: audio=12208.0ms, video=12168.6ms (frame 728), diff=39.4ms
- ⚪ Pop #5 [L]: audio=13008.0ms (ignored: unmatched_audio_pop)
- 🟡 Pop #6 [L]: audio=13813.0ms, video=13773.3ms (frame 824), diff=39.7ms
- 🟡 Pop #7 [L]: audio=14614.0ms, video=14575.6ms (frame 872), diff=38.4ms
- 🟡 Pop #8 [L]: audio=15415.0ms, video=15377.9ms (frame 920), diff=37.1ms
- 🟡 Pop #9 [L]: audio=16220.0ms, video=16180.3ms (frame 968), diff=39.7ms

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 19.2 s

### Sample Frame

![Sample Frame](./c64_recording_still.png)