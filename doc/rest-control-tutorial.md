# C64 Stream REST Control Tutorial

This tutorial guides you through setting up and using the REST control features of C64 Stream, including keyboard capture, content automation, and script automation.

## Prerequisites

- OBS Studio with C64 Stream plugin installed
- Ultimate 64 device (firmware 3.11+ recommended)
- Network connectivity between OBS and Ultimate 64
- REST API enabled on Ultimate 64 (Settings → Ultimate64 Config → Network → Enable Web Server)

## Chapter 1: Basic Setup

### 1.1 Configure REST Connection

1. Add C64 Stream source in OBS
2. Open source Properties
3. Navigate to "Remote Control" section
4. Configure connection:
   - **C64 Ultimate Host**: Enter hostname or IP (e.g., `192.168.1.100` or `c64u`)
   - **Password**: Enter your C64 Ultimate network password (used for both streaming and REST auth; leave empty if disabled)

### 1.2 Verify Connection

Check OBS log for connection messages:
- Success: `[c64-rest-client] Connected to http://192.168.1.100/v1`
- Failure: `[c64-rest-client] Connection failed: <error>`

Test connection by triggering a command (e.g., Enable Keyboard Capture and press a key).

## Chapter 2: Keyboard Capture

### 2.1 Enable Keyboard Capture

1. Open source Properties → Remote Control
2. Check "Enable Keyboard Capture"
3. Select keymap:
   - **Symbolic US**: Key labels match (Q→Q, useful for QWERTY keyboards)
   - **Positional US**: Physical positions match (QWERTY Q→C64 Q position)

### 2.2 Activate Capture

1. Click OBS Preview window to focus the C64 Stream source
2. Red "CAPTURE" indicator appears in preview (never in output/recording)
3. Type on your keyboard - keystrokes inject into C64 via REST API

### 2.3 Customize Indicator

Properties → Remote Control:
- **Indicator Text**: Change overlay text (default: "CAPTURE")
- **Indicator Position**: Top-right, top-left, bottom-right, or bottom-left
- **Indicator Opacity**: 0.0 (transparent) to 1.0 (opaque), default 0.7

### 2.4 Deactivate Capture

Press `ESC` to immediately disable capture, or click outside the preview window.

### 2.5 Keymap Format

Create custom keymaps in `data/keymaps/*.c64keymap.ini`:

```ini
[meta]
name=Custom Keymap
type=symbolic
fallback=text

[map]
; Format: PCKey = Output
KeyA = text:"a"
KeyF1 = c64:F1
Digit1 = petscii:0x31
Enter = c64:RETURN
```

**Supported output formats:**
- `text:"string"` - ASCII to PETSCII conversion
- `petscii:0xNN` - Raw PETSCII byte (hex)
- `c64:NAME` - Symbolic key (RETURN, F1-F8, CURSOR_UP, etc.)

## Chapter 3: Content Automation

### 3.1 Single File Playback

Play individual C64 files:

1. Properties → Remote Control → Automation Mode
2. Select "Single File"
3. Choose file source:
   - **Local Filesystem**: Browse to .sid, .prg, or .d64 file on your computer
   - **C64U Filesystem**: Enter path on Ultimate 64 device (e.g., `/Commodore/SID/songname.sid`)
4. Click outside properties to trigger playback

**What happens:**
- **.sid files**: Uploaded and played via REST API
- **.prg files**: Uploaded and executed
- **.d64 files**: Mounted to drive A, autostart sequence injected (LOAD"*",8,1 + RUN)

### 3.2 Folder Batch Automation

Automate playback of entire folders:

1. Automation Mode → "Folder"
2. File Source:
   - **Local Filesystem**: Browse to folder (supports subfolders)
   - **C64U Filesystem**: NOT SUPPORTED (API limitation)
3. Configure options:
   - **Shuffle Files**: Randomize playback order
   - **Consider Subfolders**: Include files from subdirectories
   - **Duration per Item**: How long to play each file (seconds)
   - **Reset Between Items**: Soft reset C64 between files
4. Folder enumeration begins automatically

**Playback flow:**
1. Enumerate all .sid, .prg, .d64 files
2. Optional shuffle (Fisher-Yates algorithm)
3. Play each file for configured duration
4. Optional reset between items
5. Repeat cycle indefinitely

### 3.3 D64 Autostart Template

Customize the autostart sequence for disk images:

Properties → Remote Control → D64 Autostart Template

Default: `LOAD"*",8,1\rRUN\r`

- `\r` represents RETURN key
- Supports full PETSCII sequences
- Example alternative: `LOAD"$",8\r` (directory listing)

## Chapter 4: Script Automation

### 4.1 Script File Format

Create `.c64script` files with automation commands:

```c64script
# Demo script - cycles through effects and palettes
effect CRT Monitor Warm
wait 5s
palette Pepto
wait 3s
play_sid c64u:/Commodore/SID/MySong.sid songnr=1
wait 2m
reset
stop
```

### 4.2 Available Commands

**Effect Commands:**
- `effect <preset_name>` - Apply CRT effect preset
- `effect_param <name> <value>` - Set individual parameter

**Palette Commands:**
- `palette <name>` - Switch color palette

**Playback Commands:**
- `play_sid <path> [songnr=N]` - Play SID file (optional song number)
- `run_prg <path>` - Execute PRG file
- `mount_disk <path>` - Mount D64 disk image
- `autostart` - Inject LOAD"*",8,1 + RUN sequence

**System Commands:**
- `reset` - Soft reset (C64 RESET button)
- `reboot` - Hard reboot (power cycle)

**Timing Commands:**
- `wait <duration>` - Pause execution
  - Formats: `500ms`, `2s`, `1.5m` (milliseconds, seconds, minutes)

**Recording Commands:**
- `record_start` - Start OBS recording
- `record_stop` - Stop OBS recording

**Control Flow:**
- `label <name>` - Define jump target
- `goto <name>` - Jump to label
- `loop [count]` - Begin loop (omit count for infinite)
- `stop` - End script execution

### 4.3 Path Resolution

**Local files:** Relative or absolute paths
```c64script
play_sid /home/user/sids/tune.sid
run_prg C:\commodore\demo.prg
```

**C64U filesystem:** Prefix with `c64u:`
```c64script
play_sid c64u:/Commodore/SID/tune.sid
mount_disk c64u:/Commodore/Games/game.d64
```

### 4.4 Script Examples

**Effect showcase:**
```c64script
# Cycle through all effect presets
effect Sharp Pixels
wait 10s
effect Soft CRT
wait 10s
effect CRT Monitor Cool
wait 10s
effect Arcade Monitor
wait 10s
stop
```

**Automated demo:**
```c64script
# Play SID, show effects, then record
label start
play_sid c64u:/HVSC/Hubbard_Rob/Commando.sid songnr=1
effect CRT Monitor Warm
palette Pepto
wait 30s
record_start
wait 2m
record_stop
reset
goto start
```

**Loop example:**
```c64script
# Play 5 different palettes
loop 5
  palette Pepto
  wait 3s
  palette Colodore
  wait 3s
  palette Vice
  wait 3s
stop
```

### 4.5 Using Scripts

1. Properties → Remote Control → Script Automation
2. Check "Enable Script Mode"
3. Select script file (.c64script)
4. Click "Start Script" button
5. Monitor "Script Status" field for progress
6. Click "Stop Script" to cancel
7. Click "Reload Script" to reparse without executing

## Chapter 5: Best Practices

### 5.1 Performance

- **Buffer Delay**: Keep at 10ms for local network (increase to 50ms for unstable networks)
- **Recording**: Disable raw video/frames recording unless debugging
- **Keyboard Capture**: Disable when not in use (reduces REST API calls)

### 5.2 Network Reliability

- Use static IP for Ultimate 64 (avoids DNS lookups)
- Enable "Auto Detect OBS IP" unless using custom network topology
- Test connectivity: `curl http://192.168.1.100/v1/machine:status`

### 5.3 Script Development

- Start with simple scripts, add complexity incrementally
- Use `wait` commands generously (allow actions to complete)
- Test scripts with "Enable Debug Messages" enabled
- Add comments (#) to document script behavior

### 5.4 Content Organization

- Organize files in dedicated folders for automation
- Use descriptive filenames (helps when shuffling)
- Keep file paths short (< 256 characters)
- Avoid special characters in paths (stick to alphanumeric + underscore)

## Chapter 6: Workflow Examples

### 6.1 Live Stream Setup

```yaml
Scenario: Live C64 demo streaming
1. Add C64 Stream source to scene
2. Enable keyboard capture for interactive demos
3. Configure automation for background music:
   - Mode: Folder
   - Path: /music/sids/
   - Shuffle: Yes
   - Duration: 180s (3 minutes per track)
4. Start streaming in OBS
5. Interact with C64 via keyboard capture
6. Automation plays background music automatically
```

### 6.2 Automated Demo Recording

```yaml
Scenario: Record multiple demos unattended
1. Create script: demo_recording.c64script
2. Script content:
   - Load demo 1
   - Apply effects
   - Record for X minutes
   - Stop recording
   - Reset
   - Load demo 2
   - Repeat
3. Enable script automation in properties
4. Start script
5. OBS records each demo automatically
6. Script stops when complete
```

### 6.3 Effect Testing

```yaml
Scenario: Test CRT effects on various content
1. Load test pattern PRG
2. Use script to cycle effects:
   effect Sharp Pixels
   wait 10s
   record_start
   wait 5s
   record_stop
   effect Soft CRT
   wait 10s
   ...
3. Review recordings to compare effects
```

## Troubleshooting

See [rest-control-troubleshooting.md](rest-control-troubleshooting.md) for detailed troubleshooting steps.

## Advanced Topics

- Custom keymap creation
- Script command extensions
- REST API endpoint reference: [c64u-rest-api.md](c64u/c64u-rest-api.md)

## Additional Resources

- Main specification: [rest-control.md](rest-control.md)
- Protocol details: [c64-stream-spec.md](c64-stream-spec.md)
- FAQ: [rest-control-faq.md](rest-control-faq.md)
