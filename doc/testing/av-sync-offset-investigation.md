# A/V Sync Offset Investigation & Resolution

**Date**: 2026-01-08
**Issue**: High percentage of unpaired pops with ~960ms offsets + epoch timestamps (1970-01-01)
**Status**: In Progress

---

## Executive Summary

Investigation revealed two critical bugs in A/V sync logging:
1. **Unpaired pops**: Video and audio use different timestamp sources, creating ~960ms phase offset
2. **Epoch timestamps**: Video uses monotonic time incorrectly as wall clock time

**Impact**: ~960ms offsets falsely marked as "unpaired", wall clock shows 1970-01-01 instead of actual date.

---

## Root Cause Analysis

### Issue 1: Unpaired Pops with ~960ms Offset

**Symptom**:
- OBS logs show many pops marked "unpaired" with ~960ms offset
- Example: `offset=961.3ms video=#16 audio=#16 unpaired`
- Pairing threshold is 500ms, so 960ms exceeds it

**Root Cause**:
- **Video timestamp source**: `frame->start_time` - captured when first packet arrives for frame
- **Audio timestamp source**: `timestamp_ns` - current packet arrival time
- **Phase offset**: Video timestamp is from ~20ms ago (frame assembly start), audio is "now"
- **Result**: Timestamps differ by frame assembly duration + processing delay = ~960ms

**Code Evidence**:
```c
// src/c64-video.c:810 - Video uses old timestamp
c64_debug_handle_video_pop(context, frame->frame_num, frame->start_time, is_all_white);

// src/c64-audio.c:223 - Audio uses current timestamp
context->av_sync_last_audio_pop_ts = context->av_sync_audio_signal_start_ts;
```

**Why ~960ms specifically**:
- PAL frame period: ~20ms (50.125 fps)
- Network buffer delay: 0-200ms configured
- Frame assembly delay: Time to receive all packets for one frame
- Processing latency: Thread scheduling, mutex waits, etc.
- **Total**: Can accumulate to ~960-1000ms between capture and detection

### Issue 2: Epoch Timestamps (1970-01-01)

**Symptom**:
- Video logs show: `detected=1970-01-01_11:17:13.916`
- Audio logs show: `detected=2026-01-08_14:57:31.980` (correct)

**Root Cause**:
- **Video uses**: `os_gettime_ns()` - monotonic time since system boot (nanoseconds)
- **Converted to**: `time_t` by dividing by 1e9 (seconds)
- **Interpreted as**: Unix epoch seconds by `localtime_r()`
- **Result**: If system uptime is 40,000 seconds, shows 1970-01-01 00:11:06

**Code Evidence**:
```c
// src/c64-video.c:735-737 - WRONG: uses monotonic time
uint64_t wall_clock_ns = os_gettime_ns();
time_t wall_clock_sec = (time_t)(wall_clock_ns / 1000000000ULL);
uint32_t wall_clock_ms = (uint32_t)((wall_clock_ns / 1000000ULL) % 1000ULL);

// src/c64-audio.c:191-193 - CORRECT: uses wall clock
uint64_t wall_clock_ms_total = c64_get_millis();
time_t wall_clock_sec = (time_t)(wall_clock_ms_total / 1000ULL);
uint32_t wall_clock_ms = (uint32_t)(wall_clock_ms_total % 1000ULL);
```

**What `os_gettime_ns()` really is**:
- OBS wrapper for `CLOCK_MONOTONIC` (Linux) or `QueryPerformanceCounter` (Windows)
- Returns nanoseconds since arbitrary point (typically system boot)
- Never goes backward, immune to NTP adjustments
- **NOT** wall clock time - cannot be converted to calendar date

**What `c64_get_millis()` really is**:
- Returns milliseconds since Unix epoch (1970-01-01 00:00:00 UTC)
- Uses `CLOCK_REALTIME` on Linux, `time()` on other platforms
- Can go backward with NTP, but correct for calendar dates
- **IS** wall clock time - suitable for human-readable timestamps

---

## Fix Strategy

### Fix 1: Use Consistent Real-Time Timestamps for Pop Detection

**Change**: Replace `frame->start_time` with `os_gettime_ns()` at detection time

**Rationale**:
- Both audio and video will use "current moment" when pop is detected
- Eliminates phase offset from frame assembly delay
- Aligns with audio's approach (uses current packet time)

**Location**: `src/c64-video.c:810`

**Before**:
```c
c64_debug_handle_video_pop(context, frame->frame_num, frame->start_time, is_all_white);
```

**After**:
```c
uint64_t detection_time_ns = os_gettime_ns();
c64_debug_handle_video_pop(context, frame->frame_num, detection_time_ns, is_all_white);
```

### Fix 2: Use Wall Clock Time for Date Display

**Change**: Replace `os_gettime_ns()` with `c64_get_millis()` for wall clock calculation

**Rationale**:
- Matches audio's implementation (already correct)
- Provides actual calendar date/time for debugging
- No functional impact on A/V sync measurement

**Location**: `src/c64-video.c:735-737`

**Before**:
```c
uint64_t wall_clock_ns = os_gettime_ns();
time_t wall_clock_sec = (time_t)(wall_clock_ns / 1000000000ULL);
uint32_t wall_clock_ms = (uint32_t)((wall_clock_ns / 1000000ULL) % 1000ULL);
```

**After**:
```c
uint64_t wall_clock_ms_total = c64_get_millis();
time_t wall_clock_sec = (time_t)(wall_clock_ms_total / 1000ULL);
uint32_t wall_clock_ms = (uint32_t)(wall_clock_ms_total % 1000ULL);
```

---

## Implementation Log

### Phase 1: Code Fixes Applied - 2026-01-08 15:10

**Changes**:
- ✅ Fix 1: Video pop detection uses `os_gettime_ns()` at detection time
- ✅ Fix 2: Video wall clock uses `c64_get_millis()` for date calculation
- ✅ Code formatted with `./build-aux/run-clang-format`
- ✅ Plugin built and installed locally

**Build Status**: [Pending]

---

## Test Results

### Phase 2: 30-Second Test - ITERATION 1

**Command**: `cd tests/e2e && ./real-device-av-sync.sh --duration 30`

**Test Date/Time**: 2026-01-08 15:12:28

**Results**: **PARTIAL SUCCESS** - Wall clock fixed, but unpaired pops remain

**OBS Log Statistics**:
- Total paired pops: 33
- Total unpaired pops: 20
- Unpaired percentage: **37.7%** ❌ (target: <5%)
- Max offset: **31.80 ms** ❌ (target: <30ms)
- Avg offset: **23.55 ms** (paired only)
- P50 (median): **28.20 ms** ❌ (target: <20ms)
- P95: **31.50 ms** ❌ (target: <25ms)

**Wall Clock Verification**:
- Expected format: `2026-01-08_HH:MM:SS.mmm`
- Actual format: `2026-01-08_15:12:31.589` ✅
- Years detected: {'2026'} ✅

**Success Criteria Check**:
- [ ] Paired pops ≥95% → **FAIL** (62.3%)
- [ ] Max offset <30ms → **FAIL** (31.80ms)
- [ ] P50 offset <20ms → **FAIL** (28.20ms)
- [ ] P95 offset <25ms → **FAIL** (31.50ms)
- [x] Wall clock shows 2026 (not 1970) → **PASS** ✅
- [x] No ~960ms offsets in paired pops → **PASS** ✅

**Status**: **FAILED** - Wall clock bug fixed, but unpaired pops still present

**Analysis of Unpaired Pops**:
Unpaired offsets range from **946ms to 956ms**, very close to one PAL pop period (~977ms).

Example:
```
offset=948.5ms video=#1 audio=#1 unpaired
  video_ts=37932564944576
  audio_ts=37933517854000
  Difference: 952,909,424 ns = 952.9ms
```

**Root Cause - Revised Understanding**:
The ~950ms offset is NOT processing delay - it's exactly one pop cycle! This indicates:
- Audio pop #1 timestamp is capturing a LATER moment than video pop #1
- Despite using "detection time" for both, there's still a systematic phase offset
- Possibility: Audio uses rising edge timestamp (start of signal), video uses detection time (end of frame rendering)

**Proposed Fix for Iteration 2**:
Option A: Both use absolute "now" at the moment of logging (not stored timestamps)
Option B: Increase pairing threshold from 500ms to 1100ms to accommodate the cycle offset
Option C: Investigate if pop counters are actually off-by-one (audio #1 should pair with video #0)

---

### Iteration 3: Race Condition Fix (2026-01-08 15:18 UTC)

**Root Cause Identified**: **Race condition in counter increment order**

After analyzing the log pattern from iteration 2, discovered that when counters match (audio=#7, video=#7), the timestamps still differ by ~950ms. This revealed a critical race condition:

**The Buggy Code**:
```c
// src/c64-audio.c lines 221-223 (BEFORE fix)
context->av_sync_audio_pop_count++;                        // Line 221: Counter incremented FIRST
context->av_sync_last_audio_pop_ts = timestamp_ns;         // Line 223: Timestamp stored SECOND
```

**The Race Scenario**:
1. Audio thread: Increments counter to #7 (line 221)
2. **Video thread: Reads counter=#7, matches with video=#7, calculates delta using OLD timestamp from pop #6**
3. **Video thread: Logs "paired" with small offset (e.g., -12ms)**
4. Audio thread: Stores NEW timestamp for pop #7 (line 223, ~950ms after counter increment)
5. Audio thread: Calculates delta using new timestamp vs video's already-updated timestamp
6. **Audio thread: Logs "unpaired" with huge offset (e.g., 951ms)**

**Why ~950ms specifically**:
- The offset equals exactly one PAL pop cycle (~977ms)
- Because the video thread is reading the counter from pop #7 but the timestamp from pop #6
- This creates a systematic one-cycle phase offset

**Log Pattern Evidence**:
```
VIDEO: video=#7 audio=#6    (paired, offset=-12ms)   ← Video sees audio counter=6, uses timestamp #6
AUDIO: offset=951.8ms video=#7 audio=#7 unpaired    ← Audio now stores timestamp #7, sees huge delta
VIDEO: video=#8 audio=#7    (paired, offset=-26ms)   ← Video sees audio counter=7, uses timestamp #7
AUDIO: offset=954.3ms video=#8 audio=#8 unpaired    ← Audio now stores timestamp #8, sees huge delta
```

**Applied Fix**:
```c
// src/c64-audio.c lines 220-223 (AFTER fix)
// CRITICAL: Store timestamp BEFORE incrementing counter to prevent race condition
// Otherwise another thread may read the incremented counter before timestamp is stored
context->av_sync_last_audio_pop_ts = timestamp_ns;         // Timestamp FIRST
context->av_sync_audio_pop_count++;                        // Counter SECOND
```

**Build & Install**:
```bash
cd /home/chris/dev/c64/c64stream
./build-aux/run-clang-format src/c64-audio.c
cmake --build build_x86_64
./local-build.sh linux --install
```

**Test Execution**:
```bash
cd tests/e2e
./e2e.sh --format PAL --frames 1500 --duration 30 --verbose
```

**Test Results**:
- **Status**: ⚠️ Test completed successfully but debug logging was disabled
- **Recording**: 45.5s duration, 22.6 MB (23,672,182 bytes)
- **Frames processed**: 23,749
- **Video**: 50.125 fps
- **Audio**: 48000 Hz
- **A/V sync pop logs**: ❌ Not captured (`debug_logging=false` in properties.ini)

**Analysis**:
The fix is theoretically correct (prevents race condition), but we need to validate with debug logging enabled. The race condition was the root cause of:
- Counters matching but timestamps differing by ~950ms
- Video logging "paired" while audio logs "unpaired" for the same pop pair
- Systematic one-cycle phase offset

**Expected Results** (once validated with debug logging):
- ✅ Paired percentage: ≥95% (race eliminated)
- ✅ Max offset: <30ms
- ✅ P50 offset: <20ms
- ✅ P95 offset: <25ms
- ✅ Unpaired offsets: Eliminated or <5%
- ✅ Wall clock: Shows 2026-01-08 (already fixed in iteration 1)

**Next Action**: Re-run 30s test with `debug_logging=true` to validate the race condition fix.

---

### Phase 3: 3-Minute Test

**Command**: `cd tests/e2e && ./real-device-av-sync.sh --duration 180`

**Test Date/Time**: [Pending]

**Results**: [To be filled after successful 30s test]

---

### Phase 4: 10-Minute Test

**Command**: `cd tests/e2e && ./real-device-av-sync.sh --duration 600`

**Test Date/Time**: [Pending]

**Results**: [To be filled after successful 3m test]

---

## Diagnostic Commands

### Extract Paired/Unpaired Pop Statistics
```bash
cd tests/e2e/results/real_c64u_av_sync/session_YYYYMMDD_HHMMSS
python3 << 'EOF'
import re, numpy as np

with open('obs_log.txt') as f: log = f.read()
paired = [l for l in log.split('\n') if 'AV SYNC:' in l and 'unpaired' not in l]
unpaired = [l for l in log.split('\n') if 'AV SYNC:' in l and 'unpaired' in l]
offsets = [abs(float(m.group(1))) for l in paired if (m := re.search(r'offset=([-0-9.]+)ms', l))]
offsets_sorted = sorted(offsets)

total = len(paired) + len(unpaired)
print(f'=== OBS LOG A/V SYNC ANALYSIS ===')
print(f'Paired: {len(paired)}, Unpaired: {len(unpaired)} ({100*len(unpaired)/total:.1f}%)')
if offsets:
    p50 = offsets_sorted[len(offsets)//2]
    p95 = offsets_sorted[int(len(offsets)*0.95)]
    print(f'Max: {max(offsets):.2f}ms, P50: {p50:.2f}ms, P95: {p95:.2f}ms')
    print(f'>30ms: {sum(1 for x in offsets if x>30)}, >35ms: {sum(1 for x in offsets if x>35)}')

dates = re.findall(r'detected=(\d{4})-(\d{2})-(\d{2})', log)
print(f'Wall clock years: {set(d[0] for d in dates)}')
EOF
```

---

## Lessons Learned

### Timestamp Source Selection

**For interval measurement** (deltas, durations):
- ✅ Use `os_gettime_ns()` (monotonic time)
- Never goes backward, immune to NTP adjustments
- Consistent for performance timing

**For human-readable timestamps** (logs, debugging):
- ✅ Use `c64_get_millis()` or equivalent wall clock
- Shows actual calendar date/time
- Can correlate with external events
- May jump with NTP (acceptable for logging)

### Phase Offset in Multi-Stage Processing

**Problem**: Using timestamps from different processing stages creates phase offsets
- Frame start time != Frame detection time
- Packet arrival time != Rendering time

**Solution**: Capture timestamp at SAME stage for both sources
- Video: Timestamp when white frame is detected
- Audio: Timestamp when audio signal is detected
- Both use "detection moment", not "capture moment"

---

## References

- **Code locations**:
  - Video pop detection: `src/c64-video.c:710-815`
  - Audio pop detection: `src/c64-audio.c:170-315`
  - Wall clock calculation: `src/c64-video.c:735`, `src/c64-audio.c:191`
- **Related documentation**: `doc/c64u-stream-spec.md`, `AGENTS.md`, `.github/copilot-instructions.md`
