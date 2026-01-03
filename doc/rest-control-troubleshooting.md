# C64 Stream REST Control Troubleshooting

This guide helps diagnose and resolve common issues with REST control features.

## Connection Issues

### Symptom: "Connection failed" in OBS log

**Possible Causes:**
1. REST API not enabled on Ultimate 64
2. Incorrect hostname/IP address
3. Firewall blocking HTTP traffic
4. Network connectivity problems

**Diagnostic Steps:**

```bash
# Test basic connectivity
ping c64u
# or
ping 192.168.1.100

# Test HTTP REST API
curl http://192.168.1.100/v1/machine:status

# Expected response:
# {"power":"on","reset":"idle"}
```

**Solutions:**

1. **Enable REST API:**
   - Ultimate 64 Menu → F2 (Ultimate64 Config)
   - Navigate to Network Settings
   - Enable Web Server
   - Note the port (default: 80)
   - Save and restart

2. **Verify hostname resolution:**
   - OBS log shows resolved IP: `[c64-rest-client] Resolved c64u to 192.168.1.100`
   - If DNS fails, use IP address directly in properties
   - Add static entry to `/etc/hosts` (Linux/macOS) or `C:\Windows\System32\drivers\etc\hosts` (Windows):
     ```
     192.168.1.100  c64u
     ```

3. **Check firewall:**
   - Linux: `sudo ufw allow from 192.168.1.0/24`
   - Windows: Add inbound rule for OBS in Windows Defender Firewall
   - Router: Port forwarding not needed for LAN

4. **Test from OBS machine:**
   ```bash
   curl -v http://192.168.1.100/v1/machine:status
   # Look for:
   # * Connected to 192.168.1.100 (192.168.1.100) port 80
   # < HTTP/1.1 200 OK
   ```

### Symptom: Connection works initially, then fails

**Possible Causes:**
- Network instability
- Ultimate 64 firmware crash
- DHCP IP address change

**Solutions:**
1. Configure static IP on Ultimate 64:
   - Ultimate 64 Menu → F2 → Network Settings
   - Set "Use DHCP" to No
   - Enter static IP, netmask, gateway
   - Save and restart

2. Enable connection monitoring in OBS log:
   - Look for periodic heartbeat messages
   - If heartbeat stops, check Ultimate 64 responsiveness

3. Increase REST timeout (if experiencing slow responses):
   - Not currently configurable - file GitHub issue if needed

## Keyboard Capture Issues

### Symptom: ESC key doesn't disable capture

**Possible Causes:**
- OBS window not focused
- ESC key intercepted by OBS hotkey
- Preview canvas not active

**Solutions:**
1. Click directly on preview canvas before pressing ESC
2. Check OBS Settings → Hotkeys for conflicting ESC assignments
3. Alternative: Click outside preview canvas to unfocus

### Symptom: Keys not appearing on C64

**Possible Causes:**
- REST connection down
- Keyboard backpressure (buffer full)
- Incorrect keymap selection
- C64 not accepting input (program waiting for disk)

**Diagnostic Steps:**

Check OBS log for keyboard output messages:
```
[c64-keyboard] Queued output: PETSCII 0x41 (A)
[c64-rest-client] POST /v1/keyboard:inject → 200 OK
```

If no messages appear:
- Keyboard capture may not be enabled
- Preview window may not be focused

If messages appear but C64 doesn't respond:
- Check C64 is at BASIC READY prompt (not waiting for disk)
- Try soft reset via REST: `curl -X POST http://192.168.1.100/v1/machine:reset`

**Solutions:**

1. **Test with simple keymap:**
   - Switch to "Symbolic US" keymap
   - Type simple alphanumeric keys (A-Z, 0-9)
   - If these work, issue is with complex keymap

2. **Check backpressure:**
   - Log shows: `[c64-keyboard] Buffer full, applying backpressure`
   - Solution: Typing too fast, slow down or wait for buffer to drain
   - Buffer drains at ~60 keys/second (C64 keyboard matrix scan rate)

3. **Verify REST endpoint:**
   ```bash
   # Manual keyboard injection test
   curl -X POST http://192.168.1.100/v1/keyboard:inject \
     -H "Content-Type: application/json" \
     -d '{"key":"A"}'
   ```

### Symptom: Wrong characters appear

**Possible Causes:**
- Incorrect keymap type (symbolic vs positional)
- PETSCII charset mismatch
- C64 in quote mode or shifted mode

**Solutions:**

1. **Understand keymap types:**
   - **Symbolic**: Matches key labels (Q key → Q character)
     - Use for: Typing text, BASIC programming
   - **Positional**: Matches physical positions (Q key → whatever is at Q position on C64)
     - Use for: Games expecting specific key positions

2. **Exit C64 special modes:**
   - If in QUOTE mode (purple cursor), press RETURN
   - If shifted (shifted charset active), press SHIFT+Commodore key

3. **Test with known sequence:**
   ```
   Type: 10 PRINT "HELLO"
   Expected on C64: 10 PRINT "HELLO"
   If different: Wrong keymap type or charset
   ```

## Content Automation Issues

### Symptom: File not found error

**Possible Causes:**
- Incorrect path format
- File doesn't exist at specified location
- Permission issues (local files)

**Diagnostic Steps:**

Check OBS log:
```
[c64-automation] Loading file: /home/user/sids/tune.sid
[c64-automation] Error: File not found
```

**Solutions:**

1. **Verify file exists:**
   ```bash
   # Local file
   ls -la /home/user/sids/tune.sid

   # C64U file
   curl http://192.168.1.100/v1/files:stat?path=/Commodore/SID/tune.sid
   ```

2. **Check path format:**
   - Local: Absolute path (Linux: `/home/...`, Windows: `C:\...`)
   - C64U: Must start with `/` (e.g., `/Commodore/SID/tune.sid`)
   - C64U: DO NOT include `c64u:` prefix in automation paths (only in scripts)

3. **Verify permissions:**
   ```bash
   # Linux: Ensure OBS user can read file
   chmod 644 /home/user/sids/tune.sid
   ```

### Symptom: Folder enumeration finds no files

**Possible Causes:**
- Empty folder
- No supported file types (.sid, .prg, .d64)
- Subfolder recursion not enabled
- C64U filesystem (not supported for folders)

**Solutions:**

1. **Check folder contents:**
   ```bash
   # Local folder
   ls -la /home/user/sids/
   find /home/user/sids/ -type f \( -name "*.sid" -o -name "*.prg" -o -name "*.d64" \)
   ```

2. **Enable subfolder recursion:**
   - Properties → Automation → "Consider Subfolders"

3. **C64U limitation:**
   - Folder automation ONLY supports local filesystem
   - Workaround: Copy files to local folder, use automation
   - Future: API enhancement needed for remote enumeration

### Symptom: Shuffle not working

**Possible Causes:**
- Only one file in folder
- Shuffle enabled but pattern repeats
- Random seed issue

**Solutions:**

1. **Verify multiple files:**
   - Need at least 2 files for shuffle to be meaningful

2. **Understand shuffle behavior:**
   - Fisher-Yates algorithm ensures each file played once per cycle
   - After all files played, shuffle re-runs for next cycle
   - Same file may appear early in next cycle (true randomness)

3. **Check log for shuffle messages:**
   ```
   [c64-automation] Enumerated 42 files
   [c64-automation] Shuffle enabled, randomizing order
   [c64-automation] Playing file 17/42: tune.sid
   ```

### Symptom: Duration timing incorrect

**Possible Causes:**
- Duration too short (file still loading)
- Duration countdown starts before playback
- Reset between items adds overhead

**Solutions:**

1. **Minimum durations:**
   - .sid files: 5s minimum (load time + initialization)
   - .prg files: 10s minimum (load + startup)
   - .d64 files: 15s minimum (mount + autostart + load)

2. **Timing includes:**
   - Upload time (local files)
   - REST API latency (~50-200ms per call)
   - C64 loading time (varies by file)
   - Configured duration
   - Optional reset time (~1-2s)

3. **Adjust for content type:**
   - Short SIDs: 60-120s
   - Long SIDs: 180-300s
   - Demos: 60-600s (depends on demo)

## Script Automation Issues

### Symptom: Script syntax error

**Possible Causes:**
- Invalid command name
- Missing required parameters
- Incorrect parameter format

**Diagnostic Steps:**

Check OBS log:
```
[c64-script-executor] Error at line 5: Unknown command 'efect'
[c64-script-executor] Error at line 8: Invalid wait format '5x'
```

**Solutions:**

1. **Valid command list:**
   ```
   effect, effect_param, palette
   play_sid, run_prg, mount_disk, autostart
   reset, reboot
   wait
   record_start, record_stop
   label, goto, loop, stop
   ```

2. **Parameter formats:**
   - `effect <name>` - Use exact preset name from effect_presets.ini
   - `wait <duration>` - Formats: 100ms, 2s, 1.5m
   - `play_sid <path> [songnr=N]` - Optional song number parameter
   - `label <name>` - Alphanumeric + underscore only
   - `loop [count]` - Optional count (omit for infinite)

3. **Common mistakes:**
   ```c64script
   # WRONG:
   effect "CRT Monitor"    # No quotes
   wait 5                  # Missing unit
   play_sid /path/file     # No extension

   # CORRECT:
   effect CRT Monitor Warm
   wait 5s
   play_sid /path/file.sid
   ```

### Symptom: Script stops unexpectedly

**Possible Causes:**
- Encountered `stop` command
- Error in command execution
- Invalid jump target
- Infinite loop without explicit stop

**Solutions:**

1. **Check script for stop commands:**
   ```bash
   grep -n "stop" script.c64script
   ```

2. **Review error log:**
   ```
   [c64-script-executor] Command failed: play_sid
   [c64-rest-client] Error: 404 Not Found
   [c64-script-executor] Stopping execution due to error
   ```

3. **Validate jump targets:**
   ```c64script
   # WRONG: goto target doesn't exist
   goto start
   label begin

   # CORRECT:
   goto begin
   label begin
   ```

4. **Infinite loops require explicit stop:**
   ```c64script
   loop
     effect Sharp Pixels
     wait 10s
   # This loop runs forever - intentional for live streaming

   # To stop after N iterations:
   loop 10
     effect Sharp Pixels
     wait 10s
   stop  # Exits script after 10 iterations
   ```

### Symptom: Cannot find script file

**Possible Causes:**
- Incorrect file path
- Wrong file extension
- Permission issues

**Solutions:**

1. **Use absolute paths:**
   ```
   Linux: /home/user/scripts/demo.c64script
   Windows: C:\Users\user\scripts\demo.c64script
   ```

2. **Verify .c64script extension:**
   - File MUST end with `.c64script`
   - Case-sensitive on Linux

3. **Check permissions:**
   ```bash
   chmod 644 /home/user/scripts/demo.c64script
   ```

## Network Performance Issues

### Symptom: Stuttering video/audio

**Possible Causes:**
- Network packet loss
- High latency
- Buffer delay too low

**Solutions:**

1. **Test network quality:**
   ```bash
   # Packet loss test
   ping -c 100 192.168.1.100
   # Look for: 0% packet loss

   # Latency test
   ping -c 10 192.168.1.100
   # Look for: avg < 5ms on LAN
   ```

2. **Increase buffer delay:**
   - Properties → Network → Buffer Delay
   - Default: 10ms (LAN)
   - Unstable: 50ms
   - WiFi: 100ms

3. **Check network load:**
   - Other bandwidth-heavy applications competing
   - Solution: QoS rules to prioritize UDP ports (default: 11000-11001)

### Symptom: REST API calls timing out

**Possible Causes:**
- Ultimate 64 overloaded
- Too many concurrent requests
- REST API bug

**Solutions:**

1. **Reduce request rate:**
   - Disable unnecessary features (keyboard when not capturing)
   - Increase wait times in scripts

2. **Restart REST API:**
   ```bash
   # Soft reboot of Ultimate 64
   curl -X POST http://192.168.1.100/v1/machine:reboot
   ```

3. **Monitor REST API health:**
   ```bash
   # Check response time
   time curl http://192.168.1.100/v1/machine:status
   # Should be < 100ms on LAN
   ```

## Debug Logging

### Enable Detailed Logging

1. OBS Settings → Advanced → Logging Level → Info
2. Restart OBS
3. View log: Help → Log Files → View Current Log

### Key Log Patterns

**Successful connection:**
```
[c64-rest-client] Connecting to http://192.168.1.100/v1
[c64-rest-client] Connected, testing endpoint
[c64-rest-client] Connection established
```

**Keyboard capture:**
```
[c64-keyboard] Capture enabled, focus active
[c64-keyboard] Queued output: PETSCII 0x41
[c64-rest-client] POST /v1/keyboard:inject → 200 OK
```

**Automation cycle:**
```
[c64-automation] Mode: Folder
[c64-automation] Enumerating: /home/user/sids/
[c64-automation] Found 42 files
[c64-automation] Shuffle enabled
[c64-automation] Playing 1/42: tune.sid
[c64-rest-client] POST /v1/sids:play → 200 OK
[c64-automation] Duration: 180s
[c64-automation] Playing 2/42: another.sid
```

**Script execution:**
```
[c64-script-executor] Loading script: demo.c64script
[c64-script-executor] Parsed 15 commands
[c64-script-executor] Execution started
[c64-script-executor] Line 3: effect CRT Monitor Warm
[c64-script-executor] Line 4: wait 5s
[c64-script-executor] Line 5: palette Pepto
[c64-script-executor] Execution complete
```

## Common Error Messages

### `DNS resolution failed for 'c64u'`
- **Solution:** Use IP address or add to `/etc/hosts`

### `Connection refused (port 80)`
- **Solution:** Enable REST API on Ultimate 64

### `Keyboard backpressure active`
- **Solution:** Type slower, buffer is full

### `File upload failed: 413 Payload Too Large`
- **Solution:** File > 16MB, not supported by REST API

### `Invalid effect preset: 'XYZ'`
- **Solution:** Check `data/effect_presets.ini` for valid names

### `Script parse error: unexpected end of file`
- **Solution:** Script is incomplete, add `stop` command

### `Goto target not found: 'label_name'`
- **Solution:** Define label before goto, or fix typo

## Getting Help

If issues persist:

1. **Collect diagnostics:**
   ```bash
   # OBS log
   Help → Log Files → Upload Current Log

   # REST API test
   curl -v http://192.168.1.100/v1/machine:status > rest-test.txt 2>&1

   # Network test
   ping -c 20 192.168.1.100 > network-test.txt
   ```

2. **Create GitHub issue:**
   - Repository: https://github.com/cgleissner/c64stream
   - Include: OBS log, REST test output, network test, script content (if relevant)
   - Specify: OBS version, Ultimate 64 firmware, OS

3. **Community support:**
   - Discussions tab on GitHub
   - Include minimal reproduction steps

## Related Documentation

- Tutorial: [rest-control-tutorial.md](rest-control-tutorial.md)
- FAQ: [rest-control-faq.md](rest-control-faq.md)
- Specification: [rest-control.md](rest-control.md)
- REST API: [c64u/c64u-rest-api.md](c64u/c64u-rest-api.md)
