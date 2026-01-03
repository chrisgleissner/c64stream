# C64 Stream REST Control FAQ

Frequently asked questions about REST control features in C64 Stream.

## General Questions

### Q: What Ultimate 64 firmware version do I need?

**A:** REST API features require firmware 3.11 or later. Check your firmware version in the Ultimate 64 menu (F2 → About). Update firmware from https://ultimate64.com if needed.

### Q: Does this work with VICE emulator?

**A:** No. REST control features are specifically designed for Ultimate 64 hardware and its REST API. VICE does not expose a compatible API.

### Q: Can I use REST control features remotely over the internet?

**A:** Technically yes, but not recommended:
- REST API has no authentication by default (optional password protection)
- UDP video/audio streams are unencrypted
- NAT/firewall configuration required
- Latency makes keyboard capture impractical

**Intended use:** Local network (LAN) only.

### Q: Do REST features work on macOS and Windows?

**A:** Yes, all REST control features are cross-platform:
- Windows: Full support (tested on Windows 10/11)
- macOS: Full support (tested on macOS 12+)
- Linux: Full support (primary development platform)

## Connection & Network

### Q: Why does the plugin say "Connection failed"?

**A:** Most common causes:
1. REST API not enabled on Ultimate 64 (check Settings → Network → Enable Web Server)
2. Wrong hostname/IP address in properties
3. Firewall blocking port 80
4. Ultimate 64 not powered on or network cable unplugged

**Quick test:** `curl http://192.168.1.100/v1/machine:status` (replace IP)

### Q: Should I use hostname or IP address?

**A:** Depends on your network:
- **Static IP:** Recommended (fastest, most reliable)
- **Hostname:** Works if DNS/mDNS configured properly
- **DHCP IP:** Not recommended (may change between reboots)

**Best practice:** Configure static IP on Ultimate 64, use IP address in OBS properties.

### Q: What is "Auto Detect OBS IP" and should I enable it?

**A:** When enabled, the plugin automatically detects the local IP address of your OBS machine and sends it to Ultimate 64 for video/audio streaming.

**Enable if:**
- You have multiple network interfaces
- Your network uses DHCP

**Disable if:**
- You have custom network routing (VPN, bridge, etc.)
- You want to manually specify the OBS IP

### Q: Can I connect to multiple Ultimate 64 devices?

**A:** Not simultaneously from one OBS source. Each C64 Stream source can connect to one Ultimate 64 device. To stream multiple C64s:
1. Add multiple C64 Stream sources
2. Configure each with different IP addresses
3. Switch between scenes

## Keyboard Capture

### Q: Why doesn't the red "CAPTURE" indicator disappear when I press ESC?

**A:** Common causes:
- OBS window lost focus (click preview canvas first)
- ESC key assigned to OBS hotkey (check Settings → Hotkeys)
- Preview canvas not active (click directly on C64 video)

**Workaround:** Click outside preview canvas to unfocus.

### Q: Can I type faster than 60 characters per second?

**A:** You can type faster, but the plugin applies **backpressure** when the buffer fills:
- C64 keyboard matrix scan rate: ~60Hz
- Plugin buffer: 256 keys
- When full: OBS log shows "Keyboard backpressure active"
- Keys are queued, not dropped

**Impact:** Brief delay before next key registers.

### Q: How do I map special C64 keys (RESTORE, RUN/STOP, Commodore key)?

**A:** Use a custom keymap file in `data/keymaps/`:

```ini
[map]
F12 = c64:RESTORE
Pause = c64:RUN_STOP
MetaLeft = c64:COMMODORE
```

**Supported symbolic keys:**
- Function keys: F1-F8
- Special: RESTORE, RUN_STOP, COMMODORE, CONTROL
- Cursor: CURSOR_UP, CURSOR_DOWN, CURSOR_LEFT, CURSOR_RIGHT
- Editing: INSERT_DELETE, HOME, CLEAR

### Q: What's the difference between Symbolic and Positional keymaps?

**A:**
- **Symbolic:** Key labels match (Q→Q character)
  - Use for: Typing text, BASIC programming, English keyboards

- **Positional:** Physical key positions match (QWERTY Q is at C64 Q position)
  - Use for: Games expecting specific positions, non-US keyboards

**Example:** UK keyboard with QWERTY layout
- Symbolic: Typing "hello" produces "hello" on C64
- Positional: Physical positions preserved (useful for games)

### Q: Why do some keys produce wrong characters?

**A:** Possible causes:
1. Wrong keymap type (symbolic vs positional)
2. C64 in shifted mode (press SHIFT+Commodore to toggle)
3. C64 in quote mode (press RETURN to exit)
4. PETSCII charset confusion (uppercase/lowercase mode)

**Test:** Type `PRINT "HELLO"` at BASIC prompt. If characters are wrong, switch keymap type.

## Content Automation

### Q: Can I browse files on the Ultimate 64 filesystem from OBS?

**A:** No, the REST API does not provide directory listing functionality. You must:
- Know the exact file path (e.g., `/Commodore/SID/song.sid`)
- Or use local filesystem and let the plugin upload files

**Workaround:** Mount Ultimate 64 SD card on your PC via:
- USB mass storage mode
- Network share (if supported by firmware)
- Physical SD card reader

### Q: Why can't I use C64U paths for folder automation?

**A:** The REST API doesn't provide a directory enumeration endpoint (`/v1/files:list`). Without this, the plugin cannot:
- List files in a C64U directory
- Recursively scan subdirectories
- Randomize order (shuffle)

**Current support:**
- ✅ Single file automation (local or C64U)
- ✅ Folder automation (local only)
- ❌ Folder automation (C64U) - API limitation

**Future:** If Ultimate 64 firmware adds directory listing API, we can enable C64U folder automation.

### Q: What file formats are supported?

**A:**
- **.sid** - Commodore 64 music files (PSID/RSID format)
- **.prg** - Commodore 64 program files
- **.d64** - Disk images (mounted and autostarted)

**Not supported:** .t64, .tap, .crt (cartridge images require different API)

### Q: How does D64 autostart work?

**A:** When you select a .d64 file:
1. Plugin uploads file to Ultimate 64 (if local)
2. REST API mounts disk to drive A
3. Plugin injects keyboard sequence: `LOAD"*",8,1` + RETURN + `RUN` + RETURN
4. C64 loads and runs first program on disk

**Customization:** Change "D64 Autostart Template" in properties (default: `LOAD"*",8,1\rRUN\r`).

### Q: Can I customize the autostart command for disk images?

**A:** Yes, in properties:

**Examples:**
- Directory listing: `LOAD"$",8\r`
- Specific file: `LOAD"GAME",8,1\rRUN\r`
- Machine code: `LOAD"DEMO",8,1\rSYS 49152\r`

**Format:**
- `\r` = RETURN key (0x0D PETSCII)
- Any PETSCII sequence supported

### Q: Why does shuffle play the same file twice in a row?

**A:** It doesn't within a single cycle:
- Fisher-Yates algorithm ensures each file plays exactly once per cycle
- After all files play, a new shuffle occurs for next cycle
- Same file **can** appear early in next cycle (true randomness)

**Example:** 10 files, shuffle enabled
- Cycle 1: Files in order [3, 7, 1, 9, 5, 2, 8, 4, 10, 6]
- Cycle 2: New shuffle [6, 2, 8, 1, 4, 9, 3, 7, 10, 5]
- File 6 appears last in cycle 1, first in cycle 2 (valid)

### Q: Can I pause/resume content automation?

**A:** Not currently. Automation runs continuously once configured. To stop:
- Change automation mode to "None"
- Or remove the source from the scene

**Feature request:** Pause/resume buttons in properties (GitHub issue welcome).

## Script Automation

### Q: What scripting language does C64 Stream use?

**A:** Custom `.c64script` format - NOT JavaScript, Python, or Lua. It's a simple command-based language designed specifically for C64 automation:

```c64script
# Comments start with #
effect CRT Monitor Warm
wait 5s
play_sid /path/to/file.sid
stop
```

**Why custom language?** Simplicity - no programming knowledge required for basic automation.

### Q: Can scripts access OBS features like changing scenes?

**A:** Limited support:
- ✅ Start/stop recording (`record_start`, `record_stop`)
- ❌ Scene switching (not yet implemented)
- ❌ Source visibility (not yet implemented)
- ❌ Filters/effects on other sources (not yet implemented)

**Future:** OBS integration commands may be added (GitHub issues welcome with use cases).

### Q: How do I create loops in scripts?

**A:** Use `loop` and `stop`:

```c64script
# Infinite loop (runs until manually stopped)
loop
  effect Sharp Pixels
  wait 10s
  effect Soft CRT
  wait 10s

# Fixed iteration loop (stops after 5 cycles)
loop 5
  palette Pepto
  wait 3s
  palette Colodore
  wait 3s
stop
```

**Note:** Infinite loops require manual stop (Stop Script button).

### Q: Can I call one script from another?

**A:** No, script nesting/includes not supported. Workaround:
- Combine scripts manually
- Use labels and goto for organization

```c64script
# Simulate "include" by copying content
label effects_demo
effect Sharp Pixels
wait 10s
effect Soft CRT
wait 10s
goto main

label main
goto effects_demo
```

### Q: What's the difference between `reset` and `reboot`?

**A:**
- **reset:** Soft reset (like pressing C64 RESET button)
  - Preserves memory contents (some demos detect this)
  - Faster (~500ms)
  - Use for: Quick restart, testing initialization

- **reboot:** Hard reboot (power cycle)
  - Complete machine restart
  - Slower (~2-3s)
  - Use for: Full system reset, clearing corruption

### Q: Can I use variables in scripts?

**A:** No, scripts have no variable system. Everything is literal commands. For dynamic behavior, use external tools to generate scripts.

**Example:** Generate script with Python
```python
with open("generated.c64script", "w") as f:
    for i in range(10):
        f.write(f"play_sid /music/track{i:02d}.sid\n")
        f.write("wait 2m\n")
    f.write("stop\n")
```

## Recording & Output

### Q: Why does the capture indicator appear in my recordings?

**A:** It shouldn't! The indicator is **preview-only** by design:
- Visible in: OBS preview canvas
- Hidden in: Stream, recording, virtual camera output

**If it appears in output:**
- Bug - please report with OBS log and version
- Workaround: Disable keyboard capture before recording

### Q: Can I record REST control commands for playback later?

**A:** Not directly, but you can:
1. Monitor OBS log for REST API calls
2. Extract commands from log
3. Convert to .c64script format

**Future feature:** Record-to-script functionality (GitHub issue welcome).

### Q: Does script automation work with OBS virtual camera?

**A:** Yes, script commands affect the C64 source which is visible in:
- Preview (with capture indicator)
- Stream/recording (without indicator)
- Virtual camera output

All automation features work regardless of output method.

## Performance & Compatibility

### Q: Does keyboard capture add latency?

**A:** Minimal:
- Keyboard event: ~1-5ms (OBS event processing)
- REST API call: ~10-50ms (network latency)
- C64 key injection: ~16ms (one frame delay)

**Total:** ~30-70ms end-to-end (imperceptible for typing, noticeable for fast gaming).

### Q: Can I use REST features with OBS Studio 27 or older?

**A:** No, minimum OBS Studio 28 required:
- Uses newer OBS API features (obs-source-info v3)
- Requires Qt 6 (OBS 28+)
- Properties API enhancements

**Recommendation:** Always use latest stable OBS Studio release.

### Q: Does this plugin work with Streamlabs OBS?

**A:** Unknown, not tested. Streamlabs OBS uses a fork of OBS Studio with significant modifications. The plugin may:
- Work without issues
- Have UI rendering problems
- Crash due to API differences

**Supported:** Only official OBS Studio from obsproject.com.

### Q: What's the CPU overhead of REST features?

**A:** Very low:
- Connection monitoring: ~0.1% CPU (1 HTTP request per 5 seconds)
- Keyboard capture: ~0.2% CPU when active (per-key REST calls)
- Automation: ~0.1% CPU (periodic checks)
- Script executor: ~0.1% CPU (command dispatch loop)

**Total impact:** < 1% CPU on modern systems (tested on Intel i5-8600K).

## Advanced Topics

### Q: Can I control multiple C64s in sync?

**A:** Possible with custom scripting:
1. Create multiple C64 Stream sources (one per C64)
2. Each source connects to different Ultimate 64 IP
3. Use external tool (Python/bash) to send REST commands to all IPs simultaneously

**Plugin doesn't provide:** Built-in multi-device sync.

### Q: Can I extend the script language with custom commands?

**A:** Not via configuration, but you can:
- Fork the repository
- Add new command types in `src/c64-script-executor.c`
- Implement command handlers
- Submit pull request (if generally useful)

**See:** [developer.md](developer.md) for contribution guidelines.

### Q: How do I integrate REST control with external automation (Home Assistant, etc.)?

**A:** Use direct REST API calls to Ultimate 64:

```bash
# Play SID from external script
curl -X POST http://192.168.1.100/v1/sids:play \
  -H "Content-Type: application/json" \
  -d '{"path":"/Commodore/SID/tune.sid","song":1}'

# Reset C64
curl -X POST http://192.168.1.100/v1/machine:reset
```

**Plugin benefit:** Integrated with OBS, no external scripting needed for common tasks.

### Q: Can I use REST features without the video stream?

**A:** Yes, all REST features (keyboard, automation, scripts) work independently:
- Disable video/audio: Properties → Network → disable UDP streams
- Use REST features: Fully functional
- Benefit: Lower network bandwidth, REST-only control

**Use case:** OBS scene with overlays/graphics, C64 audio only, REST automation for content changes.

## Troubleshooting

### Q: Where do I find the OBS log?

**A:**
- **Windows:** Help → Log Files → View Current Log
- **Linux:** Help → Log Files → View Current Log (or `~/.config/obs-studio/logs/`)
- **macOS:** Help → Log Files → View Current Log

Look for lines containing `[c64-rest-client]`, `[c64-keyboard]`, `[c64-automation]`, `[c64-script-executor]`.

### Q: My issue isn't listed here - where can I get help?

**A:**
1. Check [rest-control-troubleshooting.md](rest-control-troubleshooting.md) for detailed diagnostics
2. Search GitHub issues: https://github.com/cgleissner/c64stream/issues
3. Create new issue with:
   - OBS log (Help → Log Files → Upload Current Log)
   - Ultimate 64 firmware version
   - Network configuration (static IP, firewall, etc.)
   - Exact steps to reproduce

## Additional Resources

- **Tutorial:** [rest-control-tutorial.md](rest-control-tutorial.md) - Getting started guide
- **Troubleshooting:** [rest-control-troubleshooting.md](rest-control-troubleshooting.md) - Detailed problem resolution
- **Specification:** [rest-control.md](rest-control.md) - Technical documentation
- **REST API:** [c64u/c64u-rest-api.md](c64u/c64u-rest-api.md) - Ultimate 64 API reference
- **Development:** [developer.md](developer.md) - Contributing to the plugin
