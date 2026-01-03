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
