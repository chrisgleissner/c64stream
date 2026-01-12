# C64 Stream E2E Test Report

## Scenario: PAL Default

Generated: 2026-01-09 07:58:41 UTC

## Test configuration

- Format: PAL
- Frames: 250
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

- ⚠️ UDP Packet Reception: 16198/18246 packets (15062 video, 1079 audio, major loss)
- ✅ Network Timing: span=4430.1ms, video_mean=293.2us, audio_mean=4004.4us
- ✅ Frame Processing: 1019 frames processed
- ✅ Video Recording: 17.1 MB
- ✅ Content Integrity: 34.6s duration

### Resource Usage

During the test's processing window (3.1s, 7 of 53 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 44.6% | 47.7% | 49.9% | 58.4% |
| RAM | 6093.2 MB | 6113.09 MB | 6113.32 MB | 6146.34 MB |
| GPU | 31.77% | 32.31% | 32.98% | 35.13% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 17000 video, 1246 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 4430.064 ms
- Total packets analyzed: 16142

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 16142 | 0.001 ms | 0.541 ms | 6.187 ms | 200.61% | 0.55% | 34.78% | 1102.750 |
| Video | 15060 | 0.001 ms | 0.293 ms | 3.963 ms | 193.04% | 0.59% | 30.09% | 627.750 |
| Audio | 1078 | 2.107 ms | 4.004 ms | 6.187 ms | 14.08% | 0.19% | 0.00% | 1.273 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 15060 | 0.002 ms | 3.959 ms | 0 |
| Audio | 1078 | 0.223 ms | 2.142 ms | 0 |

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
- Duration: 34.6 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from the 34.6 s video above.
