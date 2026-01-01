# C64 Stream E2E Test Report

## Scenario: NTSC Default 720p

Generated: 2026-01-01 17:17:01 UTC

## Test configuration

- Format: NTSC
- Frames: 480
- Duration: 8.0 seconds
- Video Port: 21000
- Audio Port: 21001
- OBS Enabled: true

## Build information

- Project: c64stream
- Version: 1.0.0

## System information

- OS: Ubuntu 24.04.3 LTS (kernel 6.14.0-37-generic)
- OBS: 32.0.2
- CPU: Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz (8 cores)
- RAM: 31Gi total, 23Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 30803/30803 packets (28800 video, 2003 audio)
- ✅ Network Timing: span=8846.4ms, video_mean=307.2us, audio_mean=4005.2us
- ✅ Frame Processing: 2481 frames processed
- ✅ Video Recording: 8.4 MB
- ✅ Content Integrity: 17.0s duration

### Resource Usage

During the test's processing window (8.6s, 18 of 39 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 39.9% | 47.75% | 47.53% | 62.8% |
| RAM | 6813.93 MB | 6903.07 MB | 6888.28 MB | 6918.44 MB |
| GPU | 27.62% | 44.18% | 40.16% | 51.5% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8846.378 ms
- Total packets analyzed: 30800

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 30800 | 0.001 ms | 0.547 ms | 6.767 ms | 192.89% | 7.93% | 34.88% | 892.200 |
| Video | 28799 | 0.001 ms | 0.307 ms | 4.197 ms | 169.36% | 0.09% | 31.93% | 459.000 |
| Audio | 2001 | 1.571 ms | 4.005 ms | 6.767 ms | 17.30% | 0.20% | 0.00% | 1.365 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 28799 | 0.001 ms | 4.193 ms | 0 |
| Audio | 2001 | 0.274 ms | 2.683 ms | 0 |

Details: [network.json](network.json)

<details>
<summary>Raw network.json</summary>

```json
{
    "all": {
        "count": 30800,
        "spacing_min_us": 1.0,
        "spacing_mean_us": 547.43,
        "spacing_max_us": 6767.0,
        "spacing_median_us": 5.0,
        "spacing_std_us": 1055.94,
        "spacing_cv_pct": 192.89,
        "spacing_p95_us": 3255.0,
        "spacing_p99_us": 4461.0,
        "burst_short_pct": 7.93,
        "burst_long_pct": 34.88,
        "burst_p99_p50": 892.2
    },
    "video": {
        "count": 28799,
        "spacing_min_us": 1.0,
        "spacing_mean_us": 307.17,
        "spacing_max_us": 4197.0,
        "spacing_median_us": 4.0,
        "spacing_std_us": 520.21,
        "spacing_cv_pct": 169.36,
        "spacing_p95_us": 1103.0,
        "spacing_p99_us": 1836.0,
        "burst_short_pct": 0.09,
        "burst_long_pct": 31.93,
        "burst_p99_p50": 459.0,
        "interval_median_us": 4.0,
        "interval_mean_us": 307.17,
        "interval_min_us": 1.0,
        "interval_max_us": 4197.0,
        "jitter_median_us": 1.0,
        "jitter_max_us": 4193.0,
        "jitter_median_ms": 0.001,
        "jitter_max_ms": 4.193,
        "out_of_order_count": 0,
        "out_of_order_rate_pct": 0.0
    },
    "audio": {
        "count": 2001,
        "spacing_min_us": 1571.0,
        "spacing_mean_us": 4005.25,
        "spacing_max_us": 6767.0,
        "spacing_median_us": 4254.0,
        "spacing_std_us": 692.99,
        "spacing_cv_pct": 17.3,
        "spacing_p95_us": 5119.0,
        "spacing_p99_us": 5805.0,
        "burst_short_pct": 0.2,
        "burst_long_pct": 0.0,
        "burst_p99_p50": 1.365,
        "interval_median_us": 4254.0,
        "interval_mean_us": 4005.25,
        "interval_min_us": 1571.0,
        "interval_max_us": 6767.0,
        "jitter_median_us": 274.0,
        "jitter_max_us": 2683.0,
        "jitter_median_ms": 0.274,
        "jitter_max_ms": 2.683,
        "out_of_order_count": 0,
        "out_of_order_rate_pct": 0.0
    },
    "summary": {
        "first_elapsed_us": 0.0,
        "last_elapsed_us": 8846378.0,
        "duration_us": 8846378.0,
        "duration_ms": 8846.378,
        "total_video_packets": 28799,
        "total_audio_packets": 2001,
        "total_packets": 30800,
        "analysis_complete": true
    }
}
```
</details>

### A/V Sync

- ❌ Poor synchronization (50.0%): avg offset 239.2ms, max 426.7ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=5044.0ms, video=5064.7ms (frame 303), diff=20.7ms
- 🔴 Pop #2 [R]: audio=5844.0ms, video=5933.9ms (frame 355), diff=89.9ms
- • Pop #3 [L]: audio=6644.0ms, video=6803.1ms (frame 407), diff=159.1ms
- • Pop #4 [R]: audio=7449.0ms, video=7672.2ms (frame 459), diff=223.2ms
- • Pop #5 [L]: audio=8249.0ms, video=8541.4ms (frame 511), diff=292.4ms
- • Pop #6 [R]: audio=9049.0ms, video=9410.6ms (frame 563), diff=361.6ms
- • Pop #7 [L]: audio=9854.0ms, video=9427.3ms (frame 564), diff=426.7ms
- • Pop #8 [R]: audio=10654.0ms, video=10313.2ms (frame 617), diff=340.8ms
- • Pop #9 [L]: audio=11454.0ms, video=11215.9ms (frame 671), diff=238.1ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (479 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 19/2/2/2 | 0/0/0/0 | 0 | 0 |
| After settling | 24/2/2/2 | 0/0/0/0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 4.647–16.448).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 43 | 8.851 | 2.335 | 7.639 | 4.730–12.369 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 17.0 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 304 at 00:05.1 of the 17.0 s video above.
