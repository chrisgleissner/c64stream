# Follow device palette

**Palette → Follow device** shows the colours the Ultimate is rendering with.

1. **The device setting.** The worker in `src/video/c64-palette-follow.c` reads
   *U64 Specific Settings → Palette Definition* over REST. The value is a file name in
   `/flash/data`; an empty value means the firmware's built-in palette, which equals the
   plugin's default palette. The file is downloaded over FTP
   (`ftp://<host>/Flash/data/<name>`, any user name, password = network password).
2. **The video stream (experimental).** Firmware that supports it sends palette packets when
   video is started with `palette=1` (see `doc/c64/c64u-stream-spec.md`). These packets also
   report palettes that are loaded without changing the setting, for example by opening a
   `.vpl` file in the device's file browser.

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
- A file is downloaded only when it is not in the cache, and on every 10th check, so a file that
  is overwritten under the same name with the same size is still picked up.

Downloaded files are cached in `<Documents>/obs-studio/c64stream/settings/device-palettes` as
`<device>-<size>-<name>`. Writes use a temporary file and a rename. A file is accepted only if
the firmware would accept it: 8-8192 bytes, 16 colour lines (`RR GG BB`, `#` starts a comment),
the same rules as the firmware's VPL parser. The setting value must be a plain file name; path
separators and `..` are rejected.

After a failure the interval doubles up to 10 s, so an unreachable device is not flooded with
requests. A device switch, a change of mode, interval or password, and the first stream
palette packet wake the worker at once and reset the interval. On a failure the last colours stay on screen and
**Palette source** names the problem.

## Device switches

The worker remembers the last palette of up to 8 devices in memory. On a switch the new
device's remembered palette is shown at once, or the default palette for a device not seen
before, and its setting is read immediately.

Every switch increments a device generation. A check copies the host, password, device key and
generation together, and its result is applied, remembered, cached or shown in the status only
if the generation is still current. A request to the previous device that completes after the
switch is therefore dropped, even when the new device does not answer.

`c64_palette_follow_device_changed` takes only `palette_mutex`, so `c64_update` can call it
while it holds `config_mutex`; it calls it after releasing `config_mutex` all the same.

## Stream control transport

Follow device uses REST. With Stream Control Transport *Force Legacy*, or *Auto* while REST is
unavailable, the default palette is shown and **Palette source** says to choose Auto or
Force REST.

## Stopping

`c64_palette_follow_stop` sets the stop flag, which also aborts a transfer in flight through
libcurl's progress callback, and joins the thread. Stopping takes well under a second even
when the device does not answer.

## Tests

- `tests/network/test_c64_device_palette.c`: setting and VPL parsing (against the firmware's
  rules), name checks, cache paths, REST and FTP against an in-process fake Ultimate
  (`tests/network/fake_ultimate.h`), password handling, error mapping and cancellation.
- `tests/network/test_c64_palette_follow.c`: the worker's decisions, mostly one synchronous
  check at a time: built-in palette, file changes, cache hits, same-size overwrite, stream
  precedence and freshness, Force Legacy, wrong password, errors per cause, device switch
  memory, status text. With the worker thread running: a late answer from the previous device
  after a switch is dropped, a switch announced while `config_mutex` is held does not
  deadlock, a wake is acted on within 500 ms, and stop returns within 1 s during a hanging
  request.
- Both run on Linux, macOS and Windows CI (`platform-ci` label), and are clean under
  ThreadSanitizer and AddressSanitizer.
- E2E `ntsc_palette_device_poll`: the mock Ultimate changes its Palette Definition setting
  during the run, and only that file may be downloaded. The palette assertion samples the
  recording in ten windows: it must show the default palette first, then the new palette, never
  back. Where the change falls in the recording depends on how long OBS took to start, so no
  fixed position is assumed (this also fixes `ntsc_palette_device`, which assumed one).
- Hardware: `tests/hil/c64stream_hil.py palette` runs the same sequence on real devices.

## Limitations

- A palette loaded on the device without changing the setting (for example from the file
  browser) is seen only through stream palette packets.
- The device's FTP server must be enabled to download palette files. The built-in palette needs
  only REST.
