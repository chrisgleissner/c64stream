# Follow device palette

**Palette → Follow device** shows the colours the Ultimate is rendering with.

1. **The device setting.** The worker in `src/video/c64-palette-follow.c` reads
   *U64 Specific Settings → Palette Definition* over REST. The value is a file name in
   `/flash/data`; an empty value means the firmware's built-in palette, which equals the
   plugin's default palette. The file is downloaded over FTP
   (`ftp://<host>/Flash/data/<name>`, any user name, password = network password).
2. **The video stream (experimental).** Firmware that supports it sends palette packets when
   video is started with `palette=1` (see `doc/c64/c64u-stream-spec.md`): the first about
   0.1 s after the start, then one per second. These packets also report palettes that are
   loaded without changing the setting, for example by opening a `.vpl` file in the device's
   file browser.

A stream that delivered a palette packet within the last 5 s takes precedence, and the worker
then makes no requests. When the packets stop, the device setting applies again on the next
check.

## Worker

Each source has one worker thread. It sleeps on an event and talks to the device only while
Follow device is selected and the source is streaming. One check, every *Check device every*
interval (default 1000 ms, 250-10000 ms):

- `GET /v1/configs/U64%20Specific%20Settings/Palette%20Definition`: one small request.
- When the file name changes, and on every 10th check, `GET /v1/files/flash/data/<name>:info`
  for the file size.
- A file in the cache is shown at once. It is downloaded and compared when it is not cached,
  on every 10th check, and when the setting has just changed to it, unless it was verified in
  the last 3 s. A file replaced on the device under the same name and size is therefore shown
  with its new content as soon as it is selected, and within 10 checks while it stays selected.

Downloaded files are cached in `<Documents>/obs-studio/c64stream/settings/device-palettes` as
`<device>-<size>-<name>`. Writes use a unique temporary file and a rename. A file is accepted
only if the firmware would accept it: 8-8192 bytes, 16 colour lines (`RR GG BB`, `#` starts a
comment), the same rules as the firmware's VPL parser. The setting value must be a plain file
name; path separators and `..` are rejected.

After a failure the interval doubles up to 10 s, so an unreachable device is not flooded with
requests. A device switch, a change of mode, interval or password, and the first stream
palette packet wake the worker at once and reset the interval. On a failure the last colours
stay on screen and **Palette source** names the problem. A status that changes on every check
redraws the Properties dialog at most once a second.

Requests run through a libcurl multi handle that checks the stop flag every 50 ms, so
stopping a source never waits for a device that does not answer.

## Device switches

Switching is make-before-break: the previous device's picture keeps running until the new
device's first packet arrives. The colours follow the same rule.

- **Memory.** The last palette of up to 8 devices is kept in memory, whether it was read from
  the setting or reported in the stream.
- **Cut-over.** On a switch the new device's remembered palette (or the default for a device
  not seen before) is held back. The receive thread tags the first video packet that comes
  from the new device, and the processing thread switches the colours when it dequeues that
  packet, after dropping what the previous device left in the reorder buffer and the frame
  under assembly. Every frame of the previous device keeps its own colours, and every frame of
  the new device has its own. Polled and stream palettes that arrive before the cut-over are
  kept for it. If the new device sends no video, the colours switch after 3 s anyway.
- **Stream devices.** A device last seen with stream palettes is not polled for 2 s after a
  switch, so its setting (which may differ from what it renders) is not shown in between.
- **Settling.** Checks wait until the selection has been stable for 150 ms, so rapid
  switching does not start requests that the next switch abandons.
- **Generation.** Every switch increments a device generation, in the same locked sections
  in which `c64_update` changes the device id and the host. A check copies host, password,
  device key and generation together; its result is shown, remembered, cached and reported
  only if the generation is still current. A late answer from the previous device is
  dropped, even when the new device does not answer.

`c64_update` announces the switch (`c64_palette_follow_device_changed`) while it holds
`config_mutex`, right after it has set the new expected peer and the handover peer, and the
receive thread tags a packet only if its sender is the expected peer at that moment, so a
packet from the previous device can never carry the cut-over. The function takes only `palette_mutex`. `c64_source_apply_palette`
leaves the colours alone while Follow device stays on.

## Stream control transport

Follow device uses REST. Under *Auto* it keeps reading the setting while stream control has
fallen back to the control port. With *Force Legacy* the default palette is shown and
**Palette source** says to choose Auto or Force REST.

## Tests

- `tests/network/test_c64_device_palette.c`: setting and VPL parsing (against the firmware's
  rules), name checks, cache paths, REST and FTP against an in-process fake Ultimate
  (`tests/network/fake_ultimate.h`), password handling, error mapping per status, long names,
  and cancellation.
- `tests/network/test_c64_palette_follow.c`: the worker's decisions one check at a time
  (built-in palette, file changes, cache hits and verification, same-size overwrite, stream
  precedence and freshness, Force Legacy, wrong password, errors per cause, switch memory,
  status text and refresh rate), and with the worker thread running: late answers after a
  switch, no deadlock with `config_mutex` held, prompt wakes, fast stop. A mixed fleet of a
  stream device and a polled device on loopback: switching back and forth must always show
  each device's frames in their own colours, 600 switches with random dwell must never show a
  frame in the other device's colours, and toggling Follow device while switching must settle
  correctly.
- Both run on Linux, macOS and Windows CI (`platform-ci` label), and are clean under
  ThreadSanitizer and AddressSanitizer.
- E2E `ntsc_palette_device_poll`: the mock Ultimate changes its Palette Definition setting
  during the run. The palette assertion samples the recording in ten windows: the default
  palette first, then the new palette, never back. Where the change falls depends on how long
  OBS took to start, so no fixed position is assumed (this also fixes `ntsc_palette_device`).
- E2E `ntsc_palette_mixed_switch`: two mock devices, one reporting Night in its stream, one
  rejecting `palette=1` and serving Monochrome through its setting. A C64Script switches
  between them slowly, rapidly and while leaving and re-entering Follow device. Every sampled
  frame is classified by pattern (device) and border colour (palette); none may be in the
  other device's palette.
- Hardware: `tests/hil/c64stream_hil.py palette` (both devices polled) and `palette_mixed`
  (one device with stream palettes, one polled), including an on-screen cut-over check in
  which each wrong pairing shows a colour that cannot occur otherwise.

## Limitations

- Without stream palette packets, a palette loaded on the device without changing the setting
  (for example from the file browser) is not seen.
- The device's FTP server must be enabled to download palette files. The built-in palette needs
  only REST.
