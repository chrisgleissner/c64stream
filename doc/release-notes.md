# Release Notes

## Unreleased

### Device discovery and switching (hardware-tested)

- Switching devices is make-before-break: the new device is started first while the old
  picture keeps running, and the old device is stopped once the new one delivers video (at most
  five seconds later). The pause without video is typically 30-60 ms (previously about 0.5 s,
  and up to 15 s when the old device was offline); a device that is slow to start no longer
  causes a blank screen.
- A source whose device is off or unreachable when OBS starts now connects as soon as the
  device appears. In 1.2.0-rc2 it never retried after the first failed start.
- A device selected from the list stays selected while it is switched off, instead of the
  startup scan moving the source to another device that happened to answer.
- Entering the Wi-Fi address of an Ultimate in **C64U Host** works again: A/V that arrives from
  another address of the same device (verified by its REST `unique_id`) is accepted. In
  1.2.0-rc2 every packet was dropped and the picture stayed black.
- Switching to a device that is switched off shows the logo instead of freezing the previous
  device's last frame, and switching back to a live device takes under 0.5 s.
- Discovery probes every local network within its deadline on machines with Docker, libvirt,
  Hyper-V or VPN adapters, starting with the network that carries the default route; long
  hostnames are no longer truncated; larger `/v1/info` responses are accepted; startup results
  apply after the first sweep instead of after the retry pass; a migrated "Default" profile is
  no longer listed twice after discovery identifies it.
- Stream start and stop requests over REST are bounded at 1.5 s. When the device's web server
  does not answer (it occasionally stalls one request), AUTO transport sends that command over
  the control port instead, without demoting REST. A stalled request previously held a device
  switch for five seconds.
- New tests: hermetic discovery tests against fake devices and switch-queue tests run in CI on
  Linux, macOS and Windows; the `ntsc_device_switch_speed` E2E scenario checks on every build
  that every switch between live devices pauses video for at most one second; a local
  hardware-in-the-loop suite (`doc/testing/discovery-hil.md`).
- A device rebooting in the middle of a legacy control command can no longer terminate OBS with
  `SIGPIPE`, and connection checks no longer use `select()` on descriptors above `FD_SETSIZE`.

### Palette: Follow device

- **Palette → Follow device** and `PALETTE "device"` show the palette selected on the Ultimate
  (*Palette Definition* setting). OBS reads the setting every second (adjustable from 250 ms to
  10 s), downloads the selected `.vpl` file over FTP using the network password, and keeps a
  copy per device, so switching back to a device shows its colours at once.
- **Palette source** shows where the colours come from (device palette, built-in palette, or
  video stream) and names the problem if the palette cannot be read, for example a rejected
  network password or FTP being disabled on the device.
- Switching between devices shows each device's frames in its own colours: the palette
  changes exactly with the new device's first frame, the last palette of each device is
  remembered, and rapid switching does not flood the devices with requests.
- Follow device needs Stream Control Transport *Auto* or *Force REST*.
- Experimental: firmware that reports its palette in the video stream is supported too. The
  reported palette is then used directly.

### Device discovery and transport fixes

- Rediscovery updates device addresses without replacing saved names, DNS settings,
  or video/audio/control ports.
- First discovery carries migrated network settings forward when it identifies
  the configured host by its hardware ID, preserving custom UDP ports.
- Force REST reports failure when its client is unavailable, and legacy stream
  control reports connection and send failures accurately. Receive-only sources
  configured with `0.0.0.0` continue to accept incoming streams.
- Device teardown attempts both video and audio stops even if video teardown fails.
- Switching devices clears cached keyboard transport capabilities so the new
  device gets its own REST negotiation.
- Failed demo-script copies always close their destination files before cleanup.

### Audio transport resilience (Discussion #114)

- Conceal isolated missing audio packets in both OBS and plugin WAV recordings,
  while retaining the sequence-derived A/V timeline.
- Drop late and duplicate audio packets instead of replaying stale PCM.
- Emit a rate-limited `Network errors (last 60s): …` warning when transport
  errors occur; routine network and A/V health diagnostics are debug-only.
- Drain audio independently of video and write recording WAV/AVI data through
  a bounded background writer so disk I/O cannot block packet processing.

### Preserve Preview Size

- Added **Preserve preview size** to both the C64 Stream input source and the C64 Stream Effects filter.
- New instances default to stable OBS-facing bounds while CRT effects keep using internal virtual scaling.
- Existing saved scenes remain on the legacy size-changing behavior unless `preserve_size` is explicitly enabled, so existing layouts are not silently changed.
- `EFFECTPARAM "preserve_size" 1` and `EFFECTPARAM "preserve_size" 0` can toggle the behavior at runtime.
- Presets no longer override `preserve_size`.
