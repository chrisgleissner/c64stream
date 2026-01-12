# C64 Stream E2E Test Report

## Scenario: ntsc_default_avsync

Generated: 2026-01-12 15:01:20 UTC

## Test configuration

- Format: NTSC
- Frames: 300
- Duration: 5.0 seconds
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

- ✅ UDP Packet Reception: 19248 packets (17996 video, 0 audio)
- ✅ Network Timing: span=5161.4ms, video_mean=333.9us, audio_mean=4005.3us
- ✅ Frame Processing: 299 frames processed
- ✅ Video Recording: 4.0 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (7.1s, 15 of 15 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 49.1% | 50.9% | 52.14% | 63.4% |
| RAM | 6334.21 MB | 6395.66 MB | 6393.30 MB | 6416.03 MB |
| GPU | 30.4% | 89.8% | 75.94% | 97.1% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 19248 (approx)
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 5161.380 ms
- Total packets analyzed: 16267

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|----------------|-------------|---------|
| All | 16267 | 0.001 ms | 0.616 ms | 6.500 ms | 170.95% | 9.53% | 15.00% | 16.764 |
| Video | 15016 | 0.001 ms | 0.334 ms | 3.077 ms | 103.84% | 10.32% | 7.92% | 8.079 |
| Audio | 1251 | 1.763 ms | 4.005 ms | 6.500 ms | 18.40% | 0.96% | 0.00% | 1.515 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 15016 | 0.014 ms | 2.798 ms | 0 |
| Audio | 1251 | 0.109 ms | 2.500 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ⚠️ Accuracy: 0.0% (0/0 perfect)

#### Sync Details

- ⚪ Pop #1 [L]: audio=1563.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #2 [L]: audio=2364.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #3 [L]: audio=3169.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #4 [R]: audio=3971.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #5 [L]: audio=4771.0ms (ignored: unmatched_audio_pop)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 8.1 s

### Sample Frame

![Sample Frame](./c64_recording_still.png)