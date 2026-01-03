# C64Stream Ultimate 64 REST Control — Implementation Prompt

Extend the OBS `c64stream` source with **explicit, opt-in** Ultimate 64 control using the **Ultimate 64 REST API only** (no other protocols).

All user interaction must be through the source’s **Properties** (no global hotkeys and no implicit activation).

For full details on the C64 REST API, see [C64U REST API](./c64u/c64u-rest-api.md) and [C64U Open API](./c64u/c64u-openapi.yaml).

## Outcomes to deliver

- Play a SID (single file or folder batch; optional shuffle; configurable per-item delay).
- Start a program (PRG or D64; single file or folder batch; optional shuffle; configurable per-item delay).
- Propagate keystrokes from the host to the C64 with **backpressure** (client-side queue; only send when the C64 is ready).
- Show a clear **preview-only** overlay indicator when keystrokes are captured.
- Enable/disable keystroke capture strictly via the source **Properties** UI.
- Provide configurable keyboard mapping via `.ini` keymap files (ship with **symbolic** and **positional** defaults; allow user import).

## REST API contract (authoritative)

- Base URL: `http://<u64-host-or-ip>`
- If a network password is configured, every request must include `X-Password: <password>`.
- The password must be configurable in a secure but easy way that is consistent with how other configuration for our plugin is handled, and it must follow best OBS practices, if defined. Do not overengineer, but keep it secure and simple. The impact of a password leak is relatively low and we can assume the OBS machine is safe and only accessible by authorized users.
- Required endpoints (from the provided OpenAPI):
  - Reset: `PUT /v1/machine:reset` (soft reset), `PUT /v1/machine:reboot` (optional).
  - DMA memory access: `GET /v1/machine:readmem`, `PUT|POST /v1/machine:writemem`.
  - SID playback: `PUT|POST /v1/runners:sidplay`.
  - PRG execution: `PUT|POST /v1/runners:run_prg`.
  - Disk mount: `PUT|POST /v1/drives/{drive}:mount` (use `drive=a` by default; `type=d64`).

## Keystroke injection (REST DMA → KERNAL keyboard buffer)

### Memory locations and constraints

- Keyboard buffer bytes: `$0277..$0280` (10 bytes).
- Keyboard buffer length: `$00C6` (1 byte; number of pending bytes).
- Injection is **KERNAL keyboard-buffer based**. It will not work for software that reads the CIA keyboard matrix directly.

### Backpressure algorithm (simple and robust)

Maintain a client-side FIFO queue of bytes to inject (PETSCII/control codes).

Loop in a dedicated worker (never in UI/render threads):

1) Poll current buffer count:
   - `GET /v1/machine:readmem?address=00C6&length=1`
2) If `count == 0` and the queue is non-empty:
   - Dequeue up to 10 bytes as `chunk`.
   - Write the chunk to `$0277`:
     - `PUT /v1/machine:writemem?address=0277&data=<hex(chunk)>`
   - Set `$00C6 = len(chunk)`:
     - `PUT /v1/machine:writemem?address=00C6&data=<hex(len)>`
3) If `count != 0`, wait and retry (e.g. 20–50ms).
4) If `count` fails to return to 0 for a configurable timeout, stop injecting and surface a user-visible status (“C64 not consuming keyboard buffer”).

Notes:
- This “send only when empty” rule avoids overwriting partially-consumed data and stays compatible with the REST API’s contiguous-memory write constraint.
- The worker must be cancellable immediately when capture is disabled or automation stops.

### Converting host input to injected bytes

Support two output modes (keymaps select which):

- `text:"..."` → convert text to injected bytes using a “C64 BASIC-friendly” ASCII→PETSCII conversion:
  - `a..z` → `A..Z` (subtract 32)
  - `A..Z` → shifted PETSCII `0xC1..0xDA` (add 128)
  - Preserve common control characters in the string such as `\r` (RETURN, 13).
- `petscii:0xNN` → inject one exact byte.

Also provide symbolic output names that expand to bytes:

- `c64:RETURN` (`0x0D`)
- `c64:RUNSTOP` (`0x03`)
- `c64:CURSOR_DOWN` (`0x11`), `c64:CURSOR_UP` (`0x91`), `c64:CURSOR_LEFT` (`0x9D`), `c64:CURSOR_RIGHT` (`0x1D`)
- `c64:CLEAR` (`0x93`)
- `c64:HOME` (`0x13`)
- `c64:DEL` (`0x14`)
- `c64:INSERT` (`0x94`)

## Keyboard capture UX requirements

- Capture is **disabled by default** and must be enabled per-source in Properties.
- Capture is active only when:
  - source Properties setting is enabled, and
  - the OBS **preview** is focused.
- `Escape` must immediately disable capture (even if the active keymap maps it).
- If capture is disabled (manually, ESC, focus lost, network error), flush/stop the injection worker and clear any queued keystrokes.

## Preview-only overlay indicator (never in stream/record/vcam)

When capture is active, show a clear indicator rendered only in the OBS preview UI:

- It must be drawn inside the C64 border area.
- It must never appear in:
  - live stream output,
  - recordings,
  - virtual camera output.

Implementation requirement:
- Render the indicator via a **preview-only** mechanism (not the source’s normal video render path).

## Loading / starting content (REST-only)

### Play SID

- Single file:
  - Upload and play: `POST /v1/runners:sidplay?songnr=<n>` with multipart form-data (`sid` binary; optional `songlengths`).
- Folder batch:
  - For each `.sid`: upload+play, wait `duration`, then `PUT /v1/machine:reset`.

### Start PRG

- Single file:
  - Upload and run: `POST /v1/runners:run_prg` with `application/octet-stream` body.
- Folder batch:
  - For each `.prg`: upload+run, wait `duration`, then `PUT /v1/machine:reset`.

### Start D64 (mount + typed autostart)

- Upload and mount drive A:
  - `POST /v1/drives/a:mount?type=d64&mode=readonly` with `application/octet-stream` body.
- Then inject an autostart command sequence via the keyboard queue:
  - Default: `text:"LOAD\"*\",8,1\rRUN\r"`
  - Provide a Properties field to override this template (advanced users need custom loaders).
- Recommended sequence for robustness (still REST-only):
  - `PUT /v1/machine:reset`
  - short delay (configurable)
  - mount D64
  - type autostart template

## File Source Selection (Local vs C64U Filesystem)

**IMPORTANT:** The C64U REST API does not provide filesystem browsing/enumeration APIs. C64U mode requires manual path entry.

Users can select between two file sources for automation:

### Local Filesystem Mode (default)
Files are loaded from the OBS machine's local filesystem. Standard file path selection UI with file browser.

### C64U Filesystem Mode (manual path entry)
Files are accessed directly from the Ultimate 64 device's filesystem. User must know the exact file path.

**Benefits:**
- No file transfers needed
- Direct playback from C64U SD card/USB storage
- Faster playback startup

**Limitations:**
- No browsing/directory listing - must type exact paths
- No validation before playback attempt
- Requires knowledge of C64U filesystem structure

**Common Settings** (apply to both sources):
- Shuffle mode (N/A for single file mode)
- Duration per item
- Reset between items
- D64 autostart template

### REST API Reality Check

The C64U REST API (firmware 3.11+) provides:

#### Play File from C64U Filesystem
```
PUT /v1/runners:sidplay?file=/path/to/file.sid&songnr=0
PUT /v1/runners:run_prg?file=/path/to/file.prg
PUT /v1/drives/a:mount?file=/path/to/file.d64&type=d64&mode=readonly
```

The `file` parameter references a path on the Ultimate 64's filesystem. No upload body is needed.

#### File Info (single file only)
```
GET /v1/files/{path}:info
```

Returns metadata for a single file (size, extension). Does NOT list directory contents.

**Key Limitation:** No API exists for directory listing, browsing, or recursive enumeration. Users must manually specify exact file paths.

### UI/UX Implementation

**Local Filesystem:**
- Standard OBS file picker
- Folder mode: enumerate with opendir/readdir
- Single file mode: direct path

**C64U Filesystem:**
- Text entry field for file/folder path
- Placeholder: `/Commodore/SID` or `/Flash/C64/Programs/demo.prg`
- Single file mode: path to one file (e.g., `/Commodore/SID/song.sid`)
- Folder mode: NOT SUPPORTED (no directory listing API)
- No validation before playback (path checked at runtime)
- Clear documentation: "Enter exact path on C64U filesystem"

## Script Automation Scripts

Enable complex, repeatable workflows through text-based scripts. Users can create `.c64script` files to orchestrate effects, palettes, playback, and timing.

### Use Cases
- Demo recordings with synchronized effect changes
- A/V sync testing scenarios
- Automated regression testing
- Tutorial/demo content creation
- Performance benchmarking sequences

### Macro File Format

**Extension:** `.c64script`

**Syntax:** Line-based, whitespace-separated commands. Comments start with `#`.

```
# Demo recording sequence
effect crt_classic
wait 1s
palette pepto_ntsc
wait 500ms
play_sid /Commodore/SID/Commando.sid songnr=1
wait 60s
effect phosphor_glow
wait 2s
run_prg /Commodore/PRG/Plasma.prg
wait 30s
reset
wait 1s
mount_disk /Commodore/D64/LastNinja.d64
autostart
wait 120s
stop
```

### Command Reference

#### Effect Control
```
effect <preset_name>              # Apply effect preset
effect_param <name> <value>       # Set individual effect parameter
```

Examples:
```
effect crt_classic
effect_param scan_line_strength 0.7
effect_param bloom 0.3
```

#### Palette Control
```
palette <palette_name>            # Switch color palette
```

Examples:
```
palette pepto_ntsc
palette colodore
palette vibrant
```

#### Playback Control
```
play_sid <path> [songnr=N]        # Play SID file
run_prg <path>                    # Run PRG file
mount_disk <path>                 # Mount D64/D81 disk image
autostart                         # Inject LOAD"*",8,1\rRUN\r sequence
reset                             # Soft reset (C64 RESET)
reboot                            # Hard reboot (power cycle)
```

Examples:
```
play_sid /local/path/music.sid songnr=3
play_sid c64u:/Commodore/SID/song.sid
run_prg c64u:/Commodore/PRG/demo.prg
mount_disk /local/path/game.d64
```

**Path Prefixes:**
- No prefix or `/` → local filesystem
- `c64u:` → C64U filesystem (uses path-based REST API)

#### Timing Control
```
wait <duration>                   # Pause execution
```

**Duration formats:**
- `500ms` → milliseconds
- `2s` → seconds
- `1m` → minutes
- `1.5s` → decimal seconds

#### Recording Control
```
record_start                      # Start OBS recording
record_stop                       # Stop OBS recording
```

#### Execution Control
```
stop                              # Stop macro execution
loop [count]                      # Loop previous commands (omit count = infinite)
label <name>                      # Define jump target
goto <name>                       # Jump to label
```

Examples:
```
label main_loop
effect crt_classic
wait 5s
effect phosphor_glow
wait 5s
goto main_loop
```

### Implementation Architecture

#### Macro Parser (`src/c64-macro-parser.c/h`)
- Line-by-line parser with tokenization
- Command validation and parameter parsing
- Duration parsing (ms/s/m units)
- Path resolution (local vs c64u:)
- Syntax error reporting (line numbers)

#### Macro Executor (`src/c64-macro-executor.c/h`)
- Worker thread for sequential execution
- Command dispatch to appropriate subsystems
- Timer-based wait implementation (100ms polling)
- Cancellation support (immediate stop)
- Status reporting (current line, command, progress)
- Loop counter and label/goto implementation

#### Subsystem Integration
Commands invoke existing functionality:
- Effects → `c64_properties_apply_preset()`, `obs_data_set_*()` + `obs_source_update()`
- Palettes → `c64_palette_switch()`
- Playback → `c64_rest_play_sid()`, `c64_rest_run_prg()`, etc.
- Recording → `obs_frontend_recording_start()`, `obs_frontend_recording_stop()`
- Reset → `c64_rest_reset()`, `c64_rest_reboot()`

#### Properties UI Integration
```
Script Automation
  [x] Enable Macro Mode
  Macro File: [Browse...] _________________
  Status: [Idle | Running line 23/50 | Stopped | Error]
  [▶ Start] [⏹ Stop] [⟳ Reload]
```

### Error Handling
- Syntax errors: show line number and description, refuse to start
- Runtime errors: log error, optionally stop or continue
- Missing files: report specific path that failed
- REST API errors: include HTTP status and response
- Timeout handling: configurable max wait per command

### Testing Strategy

#### Unit Tests
- `tests/e2e/test_macro_parser.py` - Syntax validation, tokenization
- `tests/e2e/test_macro_executor.py` - Command dispatch, timing accuracy

#### E2E Tests
- Simple sequence: effect → wait → palette → wait → stop
- SID playback: play_sid → wait → verify playing
- Loop test: label → commands → goto → verify iteration
- Error recovery: invalid command → verify error reporting
- Cancellation: start macro → stop → verify immediate halt

#### Example Scripts
Ship with `data/scripts/` directory:
- `demo_effects.c64script` - Cycle through all effect presets
- `palette_showcase.c64script` - Display each palette for 5s
- `benchmark.c64script` - Performance test sequence
- `regression_test.c64script` - Full feature validation

### File Format Design Rationale

**Line-based:** Simple parsing, easy error reporting, no complex nesting
**Whitespace-separated:** Intuitive command/parameter syntax
**Comment support:** Self-documenting scripts
**Duration units:** Explicit units avoid ambiguity (2s vs 2000ms)
**Path prefixes:** Clear distinction between local and C64U files
**Extensible:** New commands can be added without breaking existing scripts

### Future Extensions (not in initial implementation)
- Variables: `$var = value`, `play_sid $var`
- Conditionals: `if <condition> ... endif`
- Random selection: `random_effect`, `random_palette`
- External triggers: wait for keypress, wait for network event
- Macro includes: `include other.c64script`

## File Source Selection (Local vs C64U Filesystem)

The automation system supports loading files from two sources:

### Local Filesystem (default)
- Files are stored on the OBS machine
- File/folder selection via standard OBS file picker widget
- Files are uploaded to C64U via REST API before playback

### C64U Filesystem (alternative)
- Files are stored directly on the Ultimate 64's storage (SD card/USB)
- Requires C64U REST API filesystem exploration endpoints
- No upload needed - direct playback via remote path

### Implementation Requirements

#### Filesystem Toggle
Add a toggle in Properties UI:
- `File Source`: `Local Filesystem` (default) | `C64U Filesystem`

When set to "C64U Filesystem":
- Replace OBS file picker with text entry field for remote path
- Path format: `/path/to/folder` or `/path/to/file.sid`
- Validation: check path exists via `GET /v1/files:list?path=<encoded_path>`

#### Common Settings (Apply to Both Sources)
These settings work identically regardless of file source:
- `Shuffle` - randomize playback order
- `Consider Subfolders` - recursive file enumeration
- `Duration` - seconds per item
- `Reset Between Items` - soft reset between files

#### REST API Extensions Required

##### List Directory Contents
```
GET /v1/files:list?path=<url-encoded-path>[&recursive=true]
```

Response:
```json
{
  "path": "/Commodore/SID",
  "entries": [
    {"name": "tune1.sid", "type": "file", "size": 4096},
    {"name": "Subfolder", "type": "directory"}
  ]
}
```

##### Check Path Exists
```
HEAD /v1/files:stat?path=<url-encoded-path>
```

Returns: 200 OK (exists) or 404 Not Found

##### Play File from C64U Path (SID example)
```
POST /v1/runners:sidplay?path=<url-encoded-path>&songnr=0
```

Similar extensions for PRG and D64:
- `POST /v1/runners:run_prg?path=<url-encoded-path>`
- `POST /v1/drives/a:mount?path=<url-encoded-path>&type=d64&mode=readonly`

#### Automation Logic Changes

**Local Filesystem Mode:**
1. Enumerate files locally (opendir/readdir)
2. For each file: read binary → upload via POST body → play
3. Reset if enabled

**C64U Filesystem Mode:**
1. Enumerate files via `GET /v1/files:list?path=<path>&recursive=<bool>`
2. Filter for .sid/.prg/.d64 extensions
3. For each file: play directly via path parameter (no upload)
4. Reset if enabled

#### Error Handling

- If path validation fails (404), show error: "C64U path not found: /path/to/folder"
- If filesystem API not available (older C64U firmware), show error: "C64U filesystem API not supported - use Local Filesystem mode"
- If network error during enumeration, retry with exponential backoff (3 attempts)

#### UI/UX Considerations

**Local Filesystem:**
- Standard OBS file/folder picker
- Familiar workflow for OBS users

**C64U Filesystem:**
- Text entry field with validation indicator
- Example placeholder: `/Commodore/SID`
- "Browse" button (future enhancement): open modal dialog showing C64U directory tree
- Real-time validation: green checkmark if path exists, red X if not found

#### Testing Requirements

Mock C64U server must implement:
- `GET /v1/files:list` - return simulated directory listings
- `HEAD /v1/files:stat` - validate paths
- `POST /v1/runners:sidplay?path=...` - accept path parameter
- `POST /v1/runners:run_prg?path=...` - accept path parameter
- `POST /v1/drives/a:mount?path=...` - accept path parameter

Test scenarios:
1. Enumerate and play SID files from C64U path
2. Recursive folder enumeration with shuffle
3. Path validation (valid/invalid paths)
4. Graceful fallback if API not available
5. Consistent behavior between local and remote sources

## Automation mode (folder playback)

Add a Properties-driven automation mode:

- `Mode`: `Off` | `Single File` | `Folder`
- `Folder path`
- `Shuffle` (boolean)
- `Duration seconds` (default `120`)
- `Reset between items` (boolean; default `true`, uses `PUT /v1/machine:reset`)
- `D64 autostart template` (string; default as above)
- `Stop button` / cancellation control (immediate)

Behavior:
- Enumerate supported files: `.sid`, `.prg`, `.d64`.
- Apply stable ordering; if shuffle enabled, randomize deterministically for the run.
- For each item:
  - start it (SID/PRG/D64 logic above),
  - wait duration (cancellable),
  - reset (if enabled),
  - proceed.

## Keymap files (`.ini`) and mapping defaults

### Discovery and UI

- Built-in keymaps shipped with the plugin (read-only).
- User keymaps live in a writable module config directory.
- Properties UI must provide:
  - dropdown listing all available keymaps,
  - “Import…” to add a new `.ini` (copy into user directory and refresh list).

### File format (simple `input = output`)

`[meta]` (optional):
- `name = ...`
- `type = symbolic|positional`
- `fallback = none|text` (default: `text` for `symbolic`, `none` for `positional`)

`[map]`:
- One mapping per line: `input-chord = output`

Input chord syntax:
- `[Ctrl+][Shift+][Alt+][AltGr+][Meta+]KeyName`
- `KeyName` must use a stable key identifier set; prefer `W3C KeyboardEvent.code` names for positional keys (examples: `KeyA`, `Digit1`, `Enter`, `Backspace`, `Space`, `ArrowUp`, `F1`).

Output syntax:
- `text:"..."`
- `petscii:0xNN`
- `c64:<NAME>` (names listed in “Converting host input to injected bytes”)

Rules:
- `Escape` always disables capture and cannot be overridden.
- If a key has no mapping:
  - `fallback=text`: printable characters are injected as `text:"<char>"` using the conversion rules; non-text keys are ignored.
  - `fallback=none`: the key is ignored.

Example:

```ini
[meta]
name = Symbolic (US)
type = symbolic
fallback = text

[map]
Enter = c64:RETURN
Backspace = c64:DEL
ArrowUp = c64:CURSOR_UP
ArrowDown = c64:CURSOR_DOWN
F1 = petscii:0x85
```

### Shipped defaults

Ship at least:

- `Symbolic (US).c64keymap.ini`: “text-first” mapping (intuitive for BASIC typing).
- `Positional (US).c64keymap.ini`: layout-independent mapping based on physical key positions (stable for non-US layouts).

## Quality and safety requirements

- All network operations must be asynchronous, cancellable, and must not block OBS UI/render threads.
- All state transitions must be explicit and reversible (disable capture immediately stops injection).
- Clearly document limitations:
  - keyboard buffer size (10 bytes),
  - KERNAL dependency (not all software will respond),
  - D64 autostart assumptions (BASIC prompt/reset timing).

---

## Implementation Details

### Architecture Overview

The REST control feature is implemented across three core modules:

#### 1. REST Client (`src/c64-rest-client.c/h`)
HTTP client using libcurl for Ultimate 64 communication.

**Features:**
- HTTP GET/PUT/POST operations with curl
- X-Password header authentication
- Modern curl_mime API for multipart uploads
- 5-second timeout for all requests
- Comprehensive error handling

**Endpoints:**
- `PUT /v1/machine:reset` - Soft reset
- `PUT /v1/machine:reboot` - Full reboot
- `GET /v1/machine:readmem` - DMA memory read
- `PUT /v1/machine:writemem` - DMA memory write
- `POST /v1/runners:sidplay` - Play SID file
- `POST /v1/runners:run_prg` - Execute PRG file
- `POST /v1/drives/{drive}:mount` - Mount disk image

#### 2. Keyboard Module (`src/c64-keyboard.c/h`)
Keystroke capture, keymap conversion, and injection with backpressure.

**Features:**
- Keymap file parser (.c64keymap.ini format)
- 100+ key definitions (F1-F8, cursors, colors, RETURN, etc.)
- FIFO queue (1024 bytes) with pthread mutex
- Worker thread polling C64 keyboard buffer every 50ms
- Backpressure: inject only when buffer empty ($00C6 == 0)
- Writes up to 10 bytes per injection to $0277-$0280
- Modifier key support (Shift+, Ctrl+, Alt+, Meta+)
- Dynamic keymap discovery (builtin + user directories)

**Injection Flow:**
1. Convert input → PETSCII via keymap
2. Queue byte in FIFO
3. Worker thread polls $00C6 (keyboard buffer length)
4. If buffer empty, dequeue up to 10 bytes
5. Write bytes to $0277 (keyboard buffer)
6. Update $00C6 with byte count

#### 3. Automation Module (`src/c64-automation.c/h`)
SID/PRG/D64 playback automation.

**Features:**
- Worker thread for sequential playback
- File enumeration (.sid/.prg/.d64) with opendir/readdir
- Fisher-Yates shuffle algorithm
- Duration timer with 100ms polling
- Reset between items
- D64 autostart injection (LOAD"*",8,1\rRUN\r)
- Buffer overflow protection
- Immediate cancellation support

#### 4. OBS Integration (`src/c64-source.c/h`)
OBS Studio integration for keyboard capture and overlay.

**Features:**
- Interaction callbacks (mouse_click, mouse_move, mouse_wheel, focus, key_click)
- ESC key always disables capture (VK_ESCAPE 0x1B)
- Keyboard capture state management (enabled vs active)
- Focus management (preview-only capture)
- Preview-only overlay indicator (red box, 70% opacity)
- Output detection (hide overlay when streaming/recording)

#### 5. Properties UI (`src/c64-properties.c`)
Configuration interface in OBS source properties.

**Features:**
- REST Control group with URL and password fields
- Password field (OBS_TEXT_PASSWORD type)
- Keyboard capture enable checkbox
- Dynamic keymap selection dropdown
- Automation mode (Disabled/Single/Folder)
- Folder path picker
- Shuffle, duration, reset controls

### Memory Map

| Address | Size | Description |
|---------|------|-------------|
| $00C6 | 1 byte | Keyboard buffer length (0-10) |
| $0277-$0280 | 10 bytes | Keyboard buffer (PETSCII codes) |

### Backpressure Algorithm Details

The implementation follows this precise algorithm:

1. **Poll Phase** - Read $00C6 every 50ms via `GET /v1/machine:readmem?address=00C6&length=1`
2. **Check Phase** - If $00C6 == 0, buffer is empty and ready
3. **Inject Phase** - Dequeue up to 10 bytes from FIFO
4. **Write Phase** - Write bytes to $0277 via `PUT /v1/machine:writemem?address=0277&data=<hex>`
5. **Update Phase** - Write byte count to $00C6 via `PUT /v1/machine:writemem?address=00C6&data=<hex>`

This ensures the C64 KERNAL has processed all previous keystrokes before injecting new ones.

### Testing

#### Unit Tests
- `tests/e2e/test_keymap.py` - Keymap parser validation
- `tests/e2e/test_rest_control_e2e.py` - Full REST workflow
- `tests/e2e/test_keystroke_injection.py` - Injection protocol
- `tests/e2e/test_network_*.py` - 17 network/timing tests

#### Mock Server
- `tests/e2e/mock_c64u_server.py` - Python HTTP server simulating Ultimate 64
- 64KB memory simulation
- Keyboard buffer simulation ($00C6, $0277)
- JSON responses with errors array
- X-Password authentication
- All v1 API endpoints implemented

**Run tests:**
```bash
cd tests/e2e
python3 test_rest_control_e2e.py
python3 test_keystroke_injection.py
python3 test_keymap.py
```

### Dependencies

- **libcurl** ≥ 7.56.0 (for curl_mime API)
- **pthread** (for worker threads)
- **OBS Studio SDK** 31.1.1
- **obs-frontend-api** (for output detection)

### Build Integration

The modules are integrated into the main CMake build:

```bash
cmake --preset ubuntu-x86_64
cmake --build build_x86_64
```

libcurl is automatically detected via `pkg-config`.

### Limitations

- **Keyboard buffer size:** Only 10 bytes can be injected at once
- **KERNAL dependency:** Injection won't work for software reading CIA keyboard matrix directly
- **D64 autostart:** Assumes BASIC prompt is ready; timing may vary
- **Preview overlay:** Uses frontend API; may not work in all OBS build configurations

### References

- [C64U REST API Reference](./c64u/c64u-rest-api.md) - Complete API documentation
- [C64U OpenAPI Specification](./c64u/c64u-openapi.yaml) - Machine-readable API spec
- [C64 KERNAL Keyboard Buffer](https://www.c64-wiki.com/wiki/Keyboard_buffer) - Technical details
- [PETSCII Character Set](https://www.c64-wiki.com/wiki/PETSCII) - Character encoding reference
