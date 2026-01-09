# E2E Test Cross-Pollution Fix

## Problem

Synthetic E2E tests were receiving packets from the **real C64 Ultimate device** instead of the **mock C64U** (packet replay), causing all test results to be invalid.

### Root Cause

- UDP sockets bound to `0.0.0.0:port` receive packets from **ANY** source IP address
- Real C64U at `192.168.1.13` was streaming to ports `21000` (video) and `21001` (audio)
- Mock C64U also sends to ports `21000` (video) and `21001` (audio)  
- OBS plugin bound to `0.0.0.0:21000` and `0.0.0.0:21001` received **BOTH** streams!
- Evidence: Network CSV shows sequence numbers starting at 5000+, not 0 (mock starts at 0)

### Impact

- All synthetic E2E test results were **invalid** - mixing real and mock packets
- "Packet loss" was actually receiving wrong packets from different source
- AV sync measurements were incorrect
- Frame content analysis was analyzing real C64 output, not test patterns

## Solution: Port Isolation

**Synthetic tests**: Use ports `21000` (video) and `21001` (audio) for mock C64U

**Real device tests**: Use ports `11000` (video) and `11001` (audio) - C64U hardware defaults

This ensures zero overlap between test types.

## Implementation

### 1. Code Changes (✅ Completed)

- **e2e.py**: Added packet source verification
  - Checks first video/audio packet sequence numbers
  - Fails test if sequence > 1000 (indicates real device packets)
  - Clear error message: "CROSS-POLLUTION DETECTED"

- **e2e.sh**: Added C64U reset before synthetic tests
  - Attempts to stop real C64 from streaming
  - Warns if C64U configured for wrong ports

- **real-device-av-sync.sh**: Changed default ports to `11000`/`11001`
  - Real device tests now use C64U hardware defaults
  - Synthetic tests remain on `21000`/`21001`

Committed in: `ea43ec3`

### 2. C64U Reconfiguration (⚠️ MANUAL STEP REQUIRED)

The C64 Ultimate device is currently configured to stream to ports `21000`/`21001` from previous test runs. It needs to be reconfigured to use its hardware default ports `11000`/`11001`.

#### Option A: Web Interface (Recommended)

1. Open web browser and navigate to: `http://c64u/` (or `http://192.168.1.13/`)
2. Log in to the C64 Ultimate web interface
3. Navigate to: **Settings** → **Network** → **UDP Streaming**
4. Change settings:
   - Video Port: `11000` (was `21000`)
   - Audio Port: `11001` (was `21001`)
5. Click **Save** or **Apply**
6. Verify streaming restarts with new ports

#### Option B: REST API (If Available)

If the C64 Ultimate supports port reconfiguration via REST API:

```bash
# Set video port to 11000
curl -X PUT http://c64u/v1/config:video_port -d '{"port": 11000}'

# Set audio port to 11001  
curl -X PUT http://c64u/v1/config:audio_port -d '{"port": 11001}'

# Restart streaming
curl -X PUT http://c64u/v1/machine:reset
```

**Note**: The exact REST API endpoints are unknown and need to be discovered from C64 Ultimate documentation.

#### Option C: Configuration File (If Accessible)

If SSH access is available to the C64 Ultimate:

1. SSH into the device: `ssh user@c64u`
2. Locate configuration file (typically `/etc/c64u/config` or similar)
3. Edit UDP port settings
4. Restart the streaming service

### 3. Verification

After reconfiguring the C64U, verify the fix works:

#### Test 1: Synthetic E2E (Should Pass)

```bash
cd tests/e2e
./e2e.sh --scenario ntsc_default --duration 5
```

**Expected**: 
- ✅ "Packet Source: Mock C64U (video seq=<100, audio seq=<100)"
- ✅ No cross-pollution error
- ✅ Packet reception ~95-100%

#### Test 2: Real Device E2E (Should Pass)

```bash
cd tests/e2e
./real-device-av-sync.sh --duration 10
```

**Expected**:
- ✅ Receives packets on ports 11000/11001
- ✅ Sequence numbers >1000 (device already streaming)
- ✅ AV sync measurements valid

#### Test 3: Cross-Pollution Sequence (Should Pass)

```bash
cd tests/e2e
./e2e.sh --scenario ntsc_default --duration 5
./real-device-av-sync.sh --duration 10
./e2e.sh --scenario pal_default --duration 5
./real-device-av-sync.sh --duration 10
```

**Expected**:
- ✅ All 4 tests pass
- ✅ No cross-pollution in any test
- ✅ Correct packet sources for each test

## Future Improvements

1. **Automated C64U Configuration**: Add REST API calls to e2e.sh to automatically configure C64U ports
2. **Port Verification**: Add pre-test check to verify C64U is using correct ports
3. **Documentation**: Add C64U port configuration to test setup documentation
4. **CI Isolation**: Ensure CI environment doesn't have real C64U running

## Technical Details

### UDP Port Binding Behavior

When a UDP socket binds to `0.0.0.0:port`, it receives packets sent to:
- `127.0.0.1:port` (localhost)
- `<any-local-ip>:port` (e.g., `192.168.1.185:port`)
- `<broadcast>:port` (e.g., `192.168.1.255:port`)

From **any** source IP address. This is why port isolation is required - binding alone doesn't filter by source.

### Packet Sequence Numbers

- **Mock C64U**: Generates packets starting from sequence 0
  - Frame 0 → video packets 0-59 (NTSC) or 0-67 (PAL)
  - Audio packets start from 0

- **Real C64U**: Continuous counter, never resets
  - After running for hours: sequence numbers in tens of thousands
  - Previous test logs showed: 5275, 6993, 21060, 45842, 52720

This makes sequence numbers a reliable indicator of packet source.

## Status

- ✅ Code changes implemented and committed (ea43ec3)
- ⚠️ **BLOCKER**: C64U manual reconfiguration required
- ⏳ Awaiting C64U reconfiguration to complete testing
- ⏳ Full E2E scenario suite validation pending

Once C64U is reconfigured, all tests should pass reliably without cross-pollution.
