# C64 Stream E2E Test Report

## Scenario: NTSC Default 720p

Generated: 2026-01-13 16:52:53 UTC
Git Branch: test/modularize-e2e
Git ID: f4f3cd8
Environment: local

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
- RAM: 31Gi total, 24Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ❌ UDP Packet Reception: Expected 30803, Received 0, Missing 30803 (100%)
- ⚠️ Network Timing: ok
- ❌ Frame Processing: 0 frames processed
- ✅ Video Recording: 1.5 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (2.1s, 5 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 98.9% | 99.3% | 99.34% | 99.8% |
| RAM | 6618.16 MB | 6618.9 MB | 6619.34 MB | 6620.71 MB |
| GPU | 30.43% | 89.83% | 72.45% | 97.13% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)


| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| Video | 0 | 0.000 ms | 0.000 ms | 0.000 ms | 0.00% | 0.00% | 0.00% | 0.000 |
| Audio | 0 | 0.000 ms | 0.000 ms | 0.000 ms | 0.00% | 0.00% | 0.00% | 0.000 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 0 | 0.000 ms | 0.000 ms | 0 |
| Audio | 0 | 0.000 ms | 0.000 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ❌ Poor synchronization (0.0%): avg offset 0.0ms, max 0.0ms

#### Sync Details


- Channels: 
- 🔁 Channel alternation: MISMATCH

### Frame Progression

- 🔴 Could not detect content boundaries or video pops

- Settling: 0.0s (pass/fail uses post-settling only)

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- No post-settling repeated/skipped markers detected in playback timeline.

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 3.1 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
