# C64 Stream E2E Test Report

## Scenario: NTSC Green Monitor

Generated: 2026-01-13 16:51:20 UTC
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
- ✅ Frame Processing: 8 frames processed
- ✅ Video Recording: 1.2 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (1.6s, 4 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 99.8% | 99.8% | 99.85% | 100% |
| RAM | 6797 MB | 6802.11 MB | 6802.9 MB | 6810.38 MB |
| GPU | 30.43% | 30.43% | 46.74% | 95.65% |

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

- 🟢 Frame sequence verified (8 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 0/0/0/0 | 2/5/5/5 | 0 | 0 |
| After settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- No post-settling repeated/skipped markers detected in playback timeline.

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 2.5 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
