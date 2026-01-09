# C64 Stream E2E Test Report

## Scenario: NTSC Default

Generated: 2026-01-09 07:54:22 UTC

## Test configuration

- Format: NTSC
- Frames: 300
- Duration: 5.0 seconds
- Video Port: 21000
- Audio Port: 21001
- OBS Enabled: true

## Build information

- Project: c64stream
- Version: 1.0.2

## System information

- OS: Ubuntu 24.04.3 LTS (kernel 6.14.0-37-generic)
- OBS: 32.0.2
- CPU: Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz (8 cores)
- RAM: 31Gi total, 24Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ⚠️ UDP Packet Reception: 16195/19252 packets (15055 video, 1075 audio, major loss)
- ✅ Network Timing: span=4432.7ms, video_mean=293.7us, audio_mean=3994.4us
- ✅ Frame Processing: 1017 frames processed
- ✅ Video Recording: 17.2 MB
- ✅ Content Integrity: 34.7s duration

### Resource Usage

During the test's processing window (3.1s, 7 of 53 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 50.1% | 57.5% | 57.79% | 71.3% |
| RAM | 6156.66 MB | 6188.5 MB | 6196.54 MB | 6237.59 MB |
| GPU | 0.89% | 4.33% | 3.95% | 8.53% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 4432.658 ms
- Total packets analyzed: 16135

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 16135 | 0.001 ms | 0.540 ms | 6.769 ms | 205.87% | 0.39% | 32.89% | 1130.500 |
| Video | 15053 | 0.001 ms | 0.294 ms | 5.416 ms | 209.26% | 0.42% | 28.06% | 787.750 |
| Audio | 1074 | 1.785 ms | 3.994 ms | 6.769 ms | 17.82% | 0.84% | 0.00% | 1.442 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 15053 | 0.001 ms | 5.412 ms | 0 |
| Audio | 1074 | 0.334 ms | 2.529 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ❌ Poor synchronization (0.0%): avg offset 179769313486231569995921046774104434048386446944329178485314420765176628523124396354701868608387085564428793419425520689308708363451136055357897278026477617073498977686288394835618163294045594434929537447290627480669417384648879122776218333598953852832103282504751611691208117720159985926376802466863339536384.0ms, max 179769313486231569995921046774104434048386446944329178485314420765176628523124396354701868608387085564428793419425520689308708363451136055357897278026477617073498977686288394835618163294045594434929537447290627480669417384648879122776218333598953852832103282504751611691208117720159985926376802466863339536384.0ms

#### Sync Details

- • Pop #1 [L]: audio=21.0ms, no matching video pop found

- Channels: L
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🔴 Could not detect content boundaries or video pops

- Settling: 0s (pass/fail uses post-settling only)

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- No post-settling repeated/skipped markers detected in playback timeline.

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 34.7 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from the 34.7 s video above.
