# REST Control Implementation

This document describes the REST control feature for C64 Stream, enabling programmatic control of Ultimate 64 hardware via REST API.

For full details, see [C64U REST API](./c64u/c64u-rest-api.md) and [C64U Open API](./c64u/c64u-openapi.yaml).

## Overview

The REST control feature provides:
- HTTP client for Ultimate 64 REST API
- Keyboard capture and keystroke injection with backpressure
- Keymap system for ASCII→PETSCII conversion
- SID/PRG/D64 file playback automation (stub for future implementation)
- Mock server for E2E testing

## Architecture

### Core Modules

#### 1. REST Client (`c64-rest-client.c/h`)
HTTP client using libcurl for communicating with Ultimate 64.

**Features:**
- HTTP GET/PUT/POST operations
- Password authentication (X-Password header)
- Modern curl_mime API for multipart uploads
- 5-second timeout for all requests
- Error handling with detailed messages

**Endpoints:**
- `PUT /v1/machine:reset` - Reset C64
- `PUT /v1/machine:reboot` - Reboot C64
- `GET /v1/machine:readmem` - Read memory (DMA)
- `PUT /v1/machine:writemem` - Write memory (DMA)
- `POST /v1/runners:sidplay` - Play SID file
- `POST /v1/runners:run_prg` - Execute PRG file
- `POST /v1/drives/{drive}:mount` - Mount disk image

#### 2. Keyboard Module (`c64-keyboard.c/h`)
Keystroke capture, keymap conversion, and injection with backpressure.

**Features:**
- Keymap file parser (.c64keymap.ini format)
- 30+ symbolic key definitions (F1-F8, cursors, colors, RETURN, etc.)
- FIFO queue (1024 bytes) with pthread mutex
- Worker thread polling C64 keyboard buffer every 50ms
- Backpressure: inject only when buffer empty ($00C6 == 0)
- Writes up to 10 bytes per injection to $0277-$0280

**Key Injection Flow:**
1. Convert input → PETSCII via keymap
2. Queue byte in FIFO
3. Worker thread polls $00C6 (keyboard buffer length)
4. If buffer empty, dequeue up to 10 bytes
5. Write bytes to $0277 (keyboard buffer)
6. Update $00C6 with byte count

#### 3. Automation Module (`c64-automation.c/h`)
SID/PRG/D64 playback automation (scaffolded, needs implementation).

**Planned Features:**
- Single-file playback with duration
- Folder playback with shuffle
- Reset between items
- D64 autostart template (LOAD"*",8,1 + RUN)

### Supporting Files

#### Keymap Format (`.c64keymap.ini`)
INI-style keymap definition:

```ini
[meta]
name=Symbolic US
description=Symbolic mapping for US keyboards
version=1.0

[map]
# ASCII → PETSCII
a=0x41
return=c64:RETURN
f1=c64:F1
```

**Value Types:**
- Hex: `0xNN` (e.g., `0x41`)
- Decimal: `NN` (e.g., `65`)
- Symbolic: `c64:NAME` (e.g., `c64:RETURN=0x0D`)

#### Mock C64U Server (`tests/e2e/mock_c64u_server.py`)
Python HTTP server simulating Ultimate 64 REST API for testing.

**Features:**
- 64KB memory simulation
- Keyboard buffer simulation ($00C6, $0277)
- Request logging
- All endpoints implemented

## Memory Map

| Address | Size | Description |
|---------|------|-------------|
| $00C6 | 1 byte | Keyboard buffer length (0-10) |
| $0277-$0280 | 10 bytes | Keyboard buffer (PETSCII codes) |

## Backpressure Algorithm

The keystroke injection uses a backpressure algorithm to prevent buffer overflow:

1. **Poll Phase** - Read $00C6 every 50ms
2. **Check Phase** - If $00C6 == 0, buffer is empty
3. **Inject Phase** - Dequeue up to 10 bytes from FIFO
4. **Write Phase** - Write bytes to $0277
5. **Update Phase** - Write byte count to $00C6

This ensures the C64 KERNAL has processed all previous keystrokes before injecting new ones.

## Testing

### Unit Tests
- Keymap parser validation (`test_keymap.py`)
- REST API endpoints (`test_rest_client.py`)
- Keystroke injection protocol (`test_keystroke_injection.py`)

### E2E Test
Comprehensive workflow test (`test_rest_control_e2e.py`):
- Machine control (reset/reboot)
- Memory DMA operations
- Keyboard buffer injection
- Multipart file uploads (SID/PRG)

**Run all tests:**
```bash
cd tests/e2e
python3 test_rest_control_e2e.py
```

## Dependencies

- **libcurl** ≥ 7.56.0 (for curl_mime API)
- **pthread** (for worker thread)
- **OBS Studio SDK** 31.1.1

## Build

The modules are integrated into the main CMake build:

```bash
cmake --preset ubuntu-x86_64
cmake --build build_x86_64
```

libcurl is automatically detected via `pkg-config`.

## Future Work

### OBS Integration (Not Yet Implemented)
- Add Properties UI for REST configuration
- Hook OBS input events for keyboard capture
- Render preview-only overlay indicator
- ESC key disables capture

### Automation Engine (Scaffolded)
- Implement worker thread for playback
- File enumeration (.sid/.prg/.d64)
- Shuffle mode
- Duration timer
- Reset between items

### Advanced Features
- Multiple keymap support with switching
- Positional keymap mode
- Cartridge ROM loading
- Tape image (TAP) playback
- Multiple disk drive support

## Limitations

- Keyboard capture not yet integrated with OBS
- Automation engine is scaffolded but not functional
- No Properties UI yet (REST client works, no GUI)
- Worker thread cannot be paused/resumed (only stop on destroy)

## Commit History

1. **Scaffolding** - Created module structure and stubs
2. **HTTP Client** - Implemented core REST operations
3. **Multipart Upload** - Added SID/PRG/D64 support with curl_mime
4. **Keymap Parser** - INI parser with symbolic keys
5. **Keystroke Injection** - FIFO queue + worker thread + backpressure
6. **E2E Test** - Comprehensive workflow validation

## References

- [Ultimate 64 REST API Documentation](https://ultimate64.com/REST_API) (if available)
- [C64 KERNAL Keyboard Buffer](https://www.c64-wiki.com/wiki/Keyboard_buffer)
- [PETSCII Character Set](https://www.c64-wiki.com/wiki/PETSCII)
