# C64 Stream E2E Test Report

## Scenario: NTSC Default

Generated: 2026-01-01 17:16:06 UTC

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
- ⚠️ Network Timing: span=10261.4ms, video_mean=356.3us, audio_mean=4007.4us
- ✅ Frame Processing: 2481 frames processed
- ✅ Video Recording: 9.2 MB
- ✅ Content Integrity: 18.5s duration

### Resource Usage

During the test's processing window (10.1s, 21 of 42 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 65.6% | 70.2% | 73.2% | 92.5% |
| RAM | 7281.88 MB | 7459.51 MB | 7441.4 MB | 7637.61 MB |
| GPU | 30.25% | 32.03% | 34.53% | 51.8% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 10261.407 ms
- Total packets analyzed: 30800

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 30800 | 0.001 ms | 0.594 ms | 9.664 ms | 188.69% | 8.75% | 37.35% | 981.400 |
| Video | 28798 | 0.001 ms | 0.356 ms | 9.664 ms | 176.50% | 9.36% | 33.01% | 529.800 |
| Audio | 2002 | 0.004 ms | 4.007 ms | 8.529 ms | 26.76% | 2.45% | 0.10% | 1.616 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 28798 | 0.002 ms | 9.659 ms | 0 |
| Audio | 2002 | 0.715 ms | 4.288 ms | 0 |

Details: [network.json](network.json)

<details>
<summary>Raw network.json</summary>

```json
{
    "all": {
        "count": 30800,
        "spacing_min_us": 1.0,
        "spacing_mean_us": 593.64,
        "spacing_max_us": 9664.0,
        "spacing_median_us": 5.0,
        "spacing_std_us": 1120.14,
        "spacing_cv_pct": 188.69,
        "spacing_p95_us": 3252.0,
        "spacing_p99_us": 4907.0,
        "burst_short_pct": 8.75,
        "burst_long_pct": 37.35,
        "burst_p99_p50": 981.4
    },
    "video": {
        "count": 28798,
        "spacing_min_us": 1.0,
        "spacing_mean_us": 356.32,
        "spacing_max_us": 9664.0,
        "spacing_median_us": 5.0,
        "spacing_std_us": 628.89,
        "spacing_cv_pct": 176.5,
        "spacing_p95_us": 1336.0,
        "spacing_p99_us": 2649.0,
        "burst_short_pct": 9.36,
        "burst_long_pct": 33.01,
        "burst_p99_p50": 529.8,
        "interval_median_us": 5.0,
        "interval_mean_us": 356.32,
        "interval_min_us": 1.0,
        "interval_max_us": 9664.0,
        "jitter_median_us": 2.0,
        "jitter_max_us": 9659.0,
        "jitter_median_ms": 0.002,
        "jitter_max_ms": 9.659,
        "out_of_order_count": 0,
        "out_of_order_rate_pct": 0.0
    },
    "audio": {
        "count": 2002,
        "spacing_min_us": 4.0,
        "spacing_mean_us": 4007.4,
        "spacing_max_us": 8529.0,
        "spacing_median_us": 4241.0,
        "spacing_std_us": 1072.39,
        "spacing_cv_pct": 26.76,
        "spacing_p95_us": 5821.0,
        "spacing_p99_us": 6853.0,
        "burst_short_pct": 2.45,
        "burst_long_pct": 0.1,
        "burst_p99_p50": 1.616,
        "interval_median_us": 4241.0,
        "interval_mean_us": 4007.4,
        "interval_min_us": 4.0,
        "interval_max_us": 8529.0,
        "jitter_median_us": 715.0,
        "jitter_max_us": 4288.0,
        "jitter_median_ms": 0.715,
        "jitter_max_ms": 4.288,
        "out_of_order_count": 0,
        "out_of_order_rate_pct": 0.0
    },
    "summary": {
        "first_elapsed_us": 0.0,
        "last_elapsed_us": 10261407.0,
        "duration_us": 10261407.0,
        "duration_ms": 10261.407,
        "total_video_packets": 28798,
        "total_audio_packets": 2002,
        "total_packets": 30800,
        "analysis_complete": true
    }
}
```
</details>

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 238.9ms, max 502.7ms

#### Sync Details

- • Pop #1 [L]: audio=5150.0ms, video=5265.3ms (frame 315), diff=115.3ms
- • Pop #2 [R]: audio=5950.0ms, video=6368.5ms (frame 381), diff=418.5ms
- • Pop #3 [L]: audio=6750.0ms, video=6385.2ms (frame 382), diff=364.8ms
- • Pop #4 [R]: audio=7556.0ms, video=7371.4ms (frame 441), diff=184.6ms
- 🟢 Pop #5 [L]: audio=8356.0ms, video=8357.6ms (frame 500), diff=1.6ms
- • Pop #6 [R]: audio=9156.0ms, video=9410.6ms (frame 563), diff=254.6ms
- • Pop #7 [L]: audio=9961.0ms, video=10463.7ms (frame 626), diff=502.7ms
- • Pop #8 [R]: audio=10761.0ms, video=10480.4ms (frame 627), diff=280.6ms
- 🟢 Pop #9 [L]: audio=11561.0ms, video=11533.4ms (frame 690), diff=27.6ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (479 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 56/2/2/2 | 0/0/0/0 | 0 | 0 |
| After settling | 54/2/2/2 | 0/0/0/0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 4.764–17.985).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 110 | 8.704 | 2.357 | 7.923 | 4.831–12.754 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 18.5 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 316 at 00:05.3 of the 18.5 s video above.
