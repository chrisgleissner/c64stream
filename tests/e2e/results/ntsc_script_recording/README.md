# C64 Stream E2E Test Report

## Scenario: NTSC Script Recording

- Generated: 2026-01-17 15:51:42 UTC
- Git Branch: fix/improve-keyboard-mappings
- Git ID: 873f7d8
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
- RAM: 31Gi total, 26Gi available
- Disk (/): 1.8T total, 1022G available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: Expected 30803, Received 30768, Missing 35 (0.11%)
- ✅ Network Timing: span=8013.8ms, video_mean=333.4us, audio_mean=4005.0us
- ❓ Frame Processing: 
- ❌ Video Recording: Missing recording
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.1s, 34 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 38.2% | 50.1% | 50.01% | 61.8% |
| RAM | 4457.98 MB | 4632 MB | 4622.52 MB | 4697.7 MB |
| GPU | 0% | 19% | 19.97% | 58% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8013.800 ms
- Total packets analyzed: 26039

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 26039 | 0.001 ms | 0.615 ms | 13.660 ms | 181.66% | 10.60% | 12.91% | 16.943 |
| Video | 24039 | 0.001 ms | 0.333 ms | 9.006 ms | 137.08% | 11.19% | 5.85% | 11.620 |
| Audio | 2000 | 0.003 ms | 4.005 ms | 13.660 ms | 28.63% | 5.10% | 0.80% | 1.954 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 24039 | 0.030 ms | 8.730 ms | 0 |
| Audio | 2000 | 0.063 ms | 9.658 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ❌ Poor synchronization (0.0%): avg offset 0.0ms, max 0.0ms

#### Sync Details


- Channels: 
- 🔁 Channel alternation: MISMATCH

### Frame Progression

- 🟡 Frame sequence verification incomplete

- Settling: 0.0s (pass/fail uses post-settling only)

### Video

- ❌ Recording not found
