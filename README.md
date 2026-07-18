# C64 Stream

[![Build](https://github.com/chrisgleissner/c64stream/actions/workflows/push.yaml/badge.svg?branch=main)](https://github.com/chrisgleissner/c64stream/actions/workflows/push.yaml)
[![E2E Tests](https://img.shields.io/badge/E2E%20Tests-Results-blue)](https://github.com/chrisgleissner/c64stream/blob/main/tests/e2e/results/README.md)
[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey)](https://github.com/chrisgleissner/c64stream/releases)

Bridge your Commodore 64 Ultimate directly to [OBS Studio](https://obsproject.com/) for seamless streaming and recording over your network connection.

<img src="./docs/images/c64stream.png" alt="C64 Stream Logo" width="200"/>

This plugin implements a native OBS source that receives video and audio streams from a Commodore 64 Ultimate or Ultimate 64.

The plugin connects directly to the Ultimate's network interface, eliminating the need for capture cards or composite video connections.

![C64 Stream Main Screen](https://raw.githubusercontent.com/chrisgleissner/c64stream/main/docs/images/main-screen.png "C64 Stream Main Screen")

**Features:**

- **Direct C64U → OBS streaming**: Video/audio streams received directly from a Commodore 64 Ultimate or Ultimate 64 over UDP/TCP (no capture card).
- **Two native OBS plugins**: A **C64 Stream** input source plus a **C64 Stream Effects** filter (they share the same effects engine; most setups use one or the other).
- **Real-time video + audio**: PAL 384x272 and NTSC 384x240 video, plus synchronized 16-bit stereo audio at ~48kHz.
- **CRT effects + palettes**: GPU-accelerated CRT effects with configurable presets (scan lines, bloom, tint, pixel geometry) and automatic VIC-II color space conversion.
- **Built-in recording + diagnostics**: Record BMP frames, AVI video, and WAV audio, plus optional CSV timing logs (`obs.csv` / `network.csv`) for debugging.
- **Keyboard capture**: Click the `Interact` button below the preview to type directly into the C64.
- **Script-based automation**: C64Script, a BASIC-inspired language for programmatic control of your stream ([tutorial](doc/c64script/c64script-tutorial.md)).
- **Saved devices**: Save network-only device profiles and switch from the Device list without retyping a host. Passwords stay in OBS source settings per device and are never written to profile `.ini` files.

Saved-device stream control defaults to Auto: REST is used when the device supports the endpoint and legacy port 64 is
used only after an explicit `404` or `501` capability response. Authentication failures never fall back. The same
transport choice controls keyboard input: Auto uses the REST matrix endpoint when available, Force REST never demotes,
and Force Legacy retains the KERNAL-buffer path. Teardown still uses REST `release_all` as a device-safety command when
the endpoint is available.

## Contents

- [C64 Stream](#c64-stream)
  - [Contents](#contents)
  - [🚀 Quick Start](#-quick-start)
    - [What You'll Need](#what-youll-need)
    - [1. Configure the C64U](#1-configure-the-c64u)
    - [2. Install the Plugin](#2-install-the-plugin)
      - [Windows (X64)](#windows-x64)
      - [Windows (ARM64)](#windows-arm64)
      - [Windows (Portable Mode)](#windows-portable-mode)
      - [macOS](#macos)
      - [Linux (Ubuntu / Debian)](#linux-ubuntu--debian)
      - [Linux (Fedora, Arch, etc.)](#linux-fedora-arch-etc)
    - [3. Configure the C64 Stream Plugin](#3-configure-the-c64-stream-plugin)
    - [Configure C64 Stream Effects Filter (OPTIONAL)](#configure-c64-stream-effects-filter-optional)
  - [🧩 Plugin Setup](#-plugin-setup)
    - [General](#general)
    - [Network](#network)
      - [Device Management](#device-management)
        - [Device Discovery Flow](#device-discovery-flow)
    - [Recording](#recording)
    - [Recording Options](#recording-options)
      - [File Organization](#file-organization)
      - [Usage Notes](#usage-notes)
      - [Debug \& Analysis CSV Logs](#debug--analysis-csv-logs)
    - [Effect settings](#effect-settings)
      - [Perfect Scan Lines](#perfect-scan-lines)
    - [Color Palettes](#color-palettes)
    - [Import/Export Configuration](#importexport-configuration)
    - [Remote Control](#remote-control)
    - [Keyboard Capture](#keyboard-capture)
      - [Activating Keyboard Capture](#activating-keyboard-capture)
      - [Compatibility](#compatibility)
      - [Special Key Mappings](#special-key-mappings)
    - [C64Script Automation](#c64script-automation)
  - [🛟 Troubleshooting](#-troubleshooting)
    - [Plugin missing from OBS?](#plugin-missing-from-obs)
    - [No video stream?](#no-video-stream)
    - [Lost / Repeated Frames?](#lost--repeated-frames)
    - [Effects not working?](#effects-not-working)
    - [Audio sync issues?](#audio-sync-issues)
    - [Connection acting up?](#connection-acting-up)
    - [Hostname not resolving?](#hostname-not-resolving)
    - [Recording troubles?](#recording-troubles)
  - [📚 Reference](#-reference)
    - [Default settings](#default-settings)
    - [File System Structure](#file-system-structure)
      - [1. Plugin](#1-plugin)
      - [2. Shipped Data](#2-shipped-data)
      - [3. User Data](#3-user-data)
    - [Network Details](#network-details)
      - [Hostname vs IP Address](#hostname-vs-ip-address)
      - [DNS Resolution](#dns-resolution)
      - [C64 Ultimate Setup](#c64-ultimate-setup)
    - [Technical Details](#technical-details)
  - [📹 End-to-end tests](#-end-to-end-tests)
  - [🛠️ For Developers](#️-for-developers)
  - [⚖️ License](#️-license)

## 🚀 Quick Start

### What You'll Need

- [OBS Studio 32.0.1](https://obsproject.com/download) or above.
- A computer that meets the [OBS system requirements](https://obsproject.com/kb/system-requirements).
- A [Commodore 64 Ultimate](https://www.commodore.net/) or [Ultimate 64](https://ultimate64.com/) from which you will stream.
- An Ethernet cable. The Ultimate device must be connected via its [Ethernet port](https://1541u-documentation.readthedocs.io/en/latest/howto/wifi.html#functionality-available-on-wifi). The OBS computer may connect via Wi-Fi if both are on the same network, but using Ethernet all the way to OBS is recommended for the most stable connection.

> [!NOTE]
> The plugin has been **verified to work** on the systems listed below. Other environments have not been verified and are not supported explicitly, but community contributions are always welcome.

The following sections will walk you through the entire setup. It should not take more than 5 minutes and consists of these steps:

1. Configure the C64U
2. Install the Plugin
3. Configure the Plugin

---

### 1. Configure the C64U

First let's ensure the C64U is assigned an IP address:

1. Turn off your C64U.
2. Connect an Ethernet cable to the back of your C64U and the other side to your router.
3. Turn on your C64U.
4. Enter its menu by holding the **Commodore** (C=) key whilst pressing **RESTORE**.
5. Press **F1** to enter the configuration section.
6. Use the cursor keys to navigate to **WIRED NETWORK SETUP** and press **RETURN**.
7. Take a note of the address shown to the right of **Active IP address**. It should be something like **192.168.x.y**. This is the IP address you'll want to configure in the C64 Stream plugin later on.

Now let's activate the network services that C64 Stream requires so it can connect to your C64U:

1. Press the **Arrow Left** key on the top left of your keyboard.
2. Use to cursor keys to navigate to **NETWORK SERVICES & TIMEZONE** and press **RETURN**.
3. Set both **Ultimate DMA Service** and **Web Remote Control Service** to **Enabled**. The first setting is needed for audio/video streaming, the latter for remote control, e.g. keyboard control or remote playback of programs/songs. You should see something like this once done: ![C64U Config Network Services](./docs/images/c64u-config-network-services.png)
4. Press the **Arrow Left** key until a confirmation appears that says **Save changes to Flash? Yes No**. Select **Yes** and press **RETURN**.

Well done. In the next section, we'll install the C64 Stream plugin in OBS.

---

### 2. Install the Plugin

In the following instructions, replace `$VERSION` with the latest released version as shown on the [Releases](../../releases) page.

#### Windows (X64)

The following applies if you have an Intel or AMD CPU. It has been verified to work on Windows 11:

1. Close OBS Studio
2. [Download](../../releases) the plugin package with name `c64stream-$VERSION-windows-x64.zip`. It should now be in your `Downloads` folder (typically `C:\Users\<YourName>\Downloads`).
3. Install the plugin to `C:\ProgramData\obs-studio\plugins` by either extracting the ZIP with a tool of your choice or by running the following in PowerShell:

```powershell
Expand-Archive -Path "$env:USERPROFILE\Downloads\c64stream-*-windows-x64.zip" -DestinationPath "C:\ProgramData\obs-studio\plugins" -Force
```

4. Start OBS Studio

If you are using Windows Firewall and block all incoming connections, you may have to set up an exception to allow incoming UDP connections to port 11000 (Video) and 11001 (Audio) from the C64 Ultimate, as follows. Be sure to adjust the `RemoteAddress` from `192.168.1.64` to the IP of your C64 Ultimate before you run this in PowerShell:

```powershell
New-NetFirewallRule -DisplayName "C64 Stream" -Direction Inbound -Protocol UDP -LocalPort 11000,11001 -RemoteAddress 192.168.1.64 -Action Allow
```

#### Windows (ARM64)

> [!NOTE]
> Windows on ARM64 support is experimental and has not yet been fully tested.
> If you would like to help with testing, please reach out via the
> *Discussions* tab of this repository.

1. Download and unzip the ARM64 build of OBS Studio:
   <https://github.com/obsproject/obs-studio/releases/download/32.0.4/OBS-Studio-32.0.4-Windows-arm64.zip>
2. Ensure OBS Studio is closed.
3. [Download](../../releases) the plugin package `c64stream-$VERSION-windows-arm64.zip` to your `Downloads` folder.
4. Install the plugin exactly as described in **Windows (X64)** above.

#### Windows (Portable Mode)

> [!NOTE]
> This section applies **only** if OBS Studio is run in [portable mode](https://obsproject.com/kb/portable-mode), for example when `portable_mode.txt` exists in the root directory of OBS.
> If you installed OBS using the Windows installer, use the *Standard Installation* instructions above.

In portable mode, OBS does **not** use `C:\ProgramData\obs-studio\plugins`. Instead, plugins are loaded relative to the OBS installation directory.

1. Download the appropriate plugin ZIP (X64 or ARM64), as described above.
2. Open PowerShell **in the root directory of your portable OBS installation**.
3. Copy/paste the following command into the PowerShell window and press **Enter**:

   ```powershell
   $zip=Get-ChildItem "$env:USERPROFILE\Downloads\c64stream-*-windows-*.zip" | Select-Object -First 1;
   Expand-Archive -Path $zip -DestinationPath "$env:TEMP\c64stream" -Force;
   New-Item -ItemType Directory -Force ".\obs-plugins\64bit" | Out-Null;
   Copy-Item "$env:TEMP\c64stream\c64stream\bin\64bit\*" ".\obs-plugins\64bit\" -Recurse -Force;
   New-Item -ItemType Directory -Force ".\data\obs-plugins\c64stream" | Out-Null;
   Copy-Item "$env:TEMP\c64stream\c64stream\data\*" ".\data\obs-plugins\c64stream\" -Recurse -Force
   ```

4. Start OBS Studio.

#### macOS

Verified on macOS Sequoia 15.7 and Tahoe 26.4 with Apple Silicon M4 (Intel systems should also work):

1. Close OBS Studio
2. [Download](../../releases) the plugin package with name `c64stream-$VERSION-macos-universal.pkg`. It should now be in your `~/Downloads` directory.
3. Install the plugin to `$HOME/Library/Application Support/obs-studio/plugins/c64stream.plugin` by running the following on the command line:

> [!NOTE]
> These commands are required due to platform packaging constraints on macOS and will be simplified in a future release.

```zsh
cd ~/Downloads && \
xattr -dr com.apple.quarantine c64stream-*-macos-universal.pkg && \
sudo installer -pkg c64stream-*-macos-universal.pkg -target / && \
mkdir -p "$HOME/Library/Application Support/obs-studio/plugins" && \
cp -R "/Library/Application Support/obs-studio/plugins/c64stream.plugin" \
  "$HOME/Library/Application Support/obs-studio/plugins/"
```

4. Start OBS Studio

#### Linux (Ubuntu / Debian)

1. Close OBS Studio
2. Install OBS Studio (32.0.1+):
   - **Ubuntu 24.04:**

     ```bash
     sudo add-apt-repository --yes ppa:obsproject/obs-studio
     sudo apt update
     sudo apt install -y obs-studio
     ```

   - **Debian 12:**

     ```bash
     sudo apt update
     sudo apt install -y -t bookworm-backports obs-studio
     ```

3. [Download](../../releases) the plugin: `c64stream-$VERSION-x86_64-linux-gnu.deb`
4. Install the plugin:

   ```bash
   sudo dpkg -i ~/Downloads/c64stream-*-x86_64-linux-gnu.deb
   ```

5. Start OBS Studio

#### Linux (Fedora, Arch, etc.)

For non-Debian-based distributions, you can extract the `.deb` package manually:

1. Close OBS Studio
2. Install OBS Studio using your distro's package manager
3. [Download](../../releases) the plugin: `c64stream-$VERSION-x86_64-linux-gnu.deb`
4. Extract and install manually:

   ```bash
   cd /tmp && ar x ~/Downloads/c64stream-*-x86_64-linux-gnu.deb && tar -xf data.tar.* && \
   sudo cp -r usr/share/obs/obs-plugins/c64stream /usr/share/obs/obs-plugins/ && \
   sudo cp -r usr/lib/obs-plugins/c64stream.so /usr/lib/obs-plugins/ && rm -rf data.tar.* control.tar.* debian-binary usr
   ```

5. Start OBS Studio

> [!NOTE]
> The plugin is built and [E2E tested](#end-to-end-tests) on Ubuntu 24.04, Debian 12, Fedora 40, and Arch Linux.

**Further Details:**
See the [OBS Plugins Guide](https://obsproject.com/kb/plugins-guide).

---

### 3. Configure the C64 Stream Plugin

**Getting Your C64 on Stream:**

1. **Add Source:** In OBS, click the "+" icon in the Sources tab. A window of all sources appears. Select "C64 Source":

  ![Select Plugin](https://raw.githubusercontent.com/chrisgleissner/c64stream/main/docs/images/select-plugin.png "Select C64 Stream Plugin")

   A new window opens. Keep the default settings and click "OK":

  ![Create Source](https://raw.githubusercontent.com/chrisgleissner/c64stream/main/docs/images/create-source.png "Create C64 Stream Source")

2. **Open Properties:** Select the "C64 Stream" source in your sources list, then click the "Properties" button to open the configuration dialog.

  ![C64 Stream Configuration](https://raw.githubusercontent.com/chrisgleissner/c64stream/main/docs/images/properties.png "C64 Stream Configuration")

3. **Configure hostnames / IPs:** Set the hostname or IP address of your C64 Ultimate and click "OK".

🎉 **DONE!** Enjoy streaming from your C64 Ultimate.

---

### Configure C64 Stream Effects Filter (OPTIONAL)

> [!NOTE]
> **New in version 1.1:** Please share feedback or open an issue if you notice anything that could be improved.

By default, **C64 Stream effects are applied internally by the C64 Stream input source** when streaming directly from a C64 Ultimate or Ultimate 64. In this common setup, **no additional filter is required**.

However, the same visual effects engine can also be applied to *any* OBS source using the **C64 Stream Effects** filter. This is useful if:

- You do not own a C64 Ultimate or Ultimate 64
- You use a real C64 with an HDMI capture card
- You work with emulators, media files, or test patterns
- You want identical CRT effects applied consistently across multiple sources

In these cases, the filter acts purely as an **alternative attachment point** for the same effects pipeline.

> **Important:**
> If you are using the **C64 Stream input source**, do **not** add this filter. Effects would otherwise be applied twice.

The screenshot below shows the **C64 Stream Effects** filter applied to an OBS *Media Source* whilst it is playing a test video:

![C64 Stream Effects](https://raw.githubusercontent.com/chrisgleissner/c64stream/main/docs/images/c64stream-effects.png "C64 Stream Effects")

**Installation:**
The filter is included automatically with the plugin package.

**Usage (non-C64 Stream sources):**

1. Select your source (HDMI capture card, Media Source, emulator, etc.) in OBS.
2. Click **Filters** → **+** → **C64 Stream Effects**.
3. Configure presets and effect parameters as described in the **Effect settings** section below.

---

## 🧩 Plugin Setup

### General

- **Version:** Information about release version, Git ID, and build time

### Network

![C64 Stream Network / Device Configuration](https://raw.githubusercontent.com/chrisgleissner/c64stream/main/docs/images/properties-network.png "C64 Stream Network / Device Configuration")

#### Device Management

Instead of typing a host/IP every time, the plugin keeps a small **device registry** of devices you've saved or discovered, shown as a single dropdown:

- **Device:** Choose a previously saved or discovered device. Selecting an entry immediately fills in its Host, DNS Server IP, and Password below (no need to reopen the dialog).
- **Device Name:** Friendly name shown in the Device list. Edit this and click **Save Device** to rename the selected device.
- **Save Device:** Saves the current Host/DNS/Password as the selected device, or creates a new device if none is selected yet.
- **Delete Device:** Removes the selected device from the list (does not affect the physical device).
- **Find Devices:** Scans the local network for devices (see [Device Discovery Flow](#device-discovery-flow) below). The button label switches to **Finding Devices…** while a scan is running.

Each entry shows the device's IP in parentheses, e.g. `u64 (192.168.1.13)`; devices that require a password are additionally marked, e.g. `c64u (192.168.1.167, Password)`. A device that answers on more than one interface (e.g. Ethernet and Wi-Fi) collapses to a single entry — the interfaces share the device's `unique_id` — and stays on whichever address it was first discovered at rather than flipping between them on later scans.

##### Device Discovery Flow

Clicking **Find Devices** runs the following, entirely in a background thread so the UI stays responsive:

1. **Enumerate candidate hosts:** every address already in the registry, the currently-configured Host, and every host on your machine's active local subnets (virtual/Docker-only interfaces are skipped).
2. **Probe `/v1/info`:** each candidate is queried over REST; devices that require a password are recognized from the 401/403 response and listed as password-protected without needing credentials.
3. **Filter by product:** only streaming-capable hardware is kept — the **Ultimate 64** family and **C64 Ultimate**. Ultimate II/II+/II+L units (disk/cartridge-only, no video/audio streaming) are rejected, and any such device previously saved by mistake is removed from the registry.
4. **Confirm the control channel:** each remaining candidate must also accept a connection on its control port (64), the channel stream start/stop rides. An address that answers `/v1/info` but not the control port cannot actually drive a stream, so it is not offered. A saved device is probed before the subnet sweep and retried, so a slower interface is not dropped for a momentary timeout.
5. **Update the registry:** confirmed devices are saved (one entry per physical device — see above) and immediately appear in the **Device** dropdown. A device already on file keeps its address unless that address has genuinely stopped responding, in which case it relocates to a working one.

- **C64 Ultimate Host:** Hostname or IP address of the currently selected device (default: `c64u`), or set to `0.0.0.0` to accept streams from any C64 Ultimate on your network (requires manual control from the device)
- **C64 Ultimate Password:** C64 Ultimate network password for REST `X-Password` header authentication. Leave empty if authentication is disabled
- **Stream Control Transport:** How start/stop streaming commands are sent to the device. **Auto** (default) prefers REST and falls back to the legacy protocol if REST is unavailable; **Force REST** and **Force Legacy** pin one transport for troubleshooting.
- **DNS resolution details:**
  - **Default:** `192.168.1.1` (most common home router DNS server)
  - **Fallback:** If router DNS fails, the plugin tries standard DNS servers
  - **Enhanced resolution:** The plugin uses multiple resolution strategies for maximum compatibility
- **OBS Server IP:** IP address where C64 Ultimate sends streams (auto-detected by default)
- **Auto-detect OBS IP:** Automatically detect and use OBS server IP in streaming commands (recommended)
- **Configure Ports:** Use the default ports (video: 11000, audio: 11001) unless network conflicts require different values
- **Buffer Delay:** Sets the network buffer for incoming UDP packets arriving from the C64 Ultimate (0–500 ms, default 10 ms). The buffer size is expressed in milliseconds to represent the time-based delay it introduces, compensating for packet loss, reordering, and variable network latency. Larger buffers improve stability under high-latency or congested conditions but increase end-to-end delay.

---

### Recording

The plugin includes built-in recording capabilities that work independently of OBS Studio's recording system, letting you save raw C64 Ultimate data streams directly to disk.

**Debug Logging:** The **Debug Logging** option lives in this section of the source properties. Enable it when troubleshooting, then check the OBS log via **Help → Log Files → Show Current Log**.

### Recording Options

The plugin offers three independent recording options that can be enabled separately or together:

**📊 Network and Streaming Events (CSV):**

- Records detailed timing data for network packets and OBS processing events
- Creates `obs.csv` (OBS processing timeline) and `network.csv` (UDP packet analysis)
- **Minimal Performance Impact:** Lightweight logging with microsecond precision
- **Use Cases:** Debug performance issues, analyze network jitter, validate frame timing
- Files: `session_YYYYMMDD_HHMMSS/obs.csv` and `session_YYYYMMDD_HHMMSS/network.csv`

**🖼️ Raw Frames (BMP):**

- Saves individual video frames as uncompressed BMP files
- Useful for debugging video issues or creating frame-by-frame analysis
- **Performance Impact:** Enabling this feature will reduce streaming performance due to disk I/O
- **Note:** CRT effects (scanlines, bloom, afterglow, etc.) are NOT applied to recorded frames. Palette changes ARE applied.
- Files saved as: `session_YYYYMMDD_HHMMSS/frames/frame_NNNNNN.bmp`

**🎬 Raw Video and Audio (AVI + WAV):**

- Records uncompressed AVI video and separate WAV audio files
- Captures the raw data stream without OBS processing
- **High Disk Usage:** Uncompressed video files are very large (~50MB per minute)
- **Note:** CRT effects (scanlines, bloom, afterglow, etc.) are NOT applied to recorded video. Palette changes ARE applied.
- Video file: `session_YYYYMMDD_HHMMSS/video.avi` (24-bit BGR format)
- Audio file: `session_YYYYMMDD_HHMMSS/audio.wav` (16-bit stereo PCM)

#### File Organization

All recording files are organized into timestamped session folders in the [recordings directory](#file-system-structure):

```text
recordings/
├── session_20240929_143052/
│   ├── frames/           # BMP frame files (if "Raw Frames" enabled)
│   ├── network.csv       # Network timings (if "CSV Events" enabled)
│   ├── obs.csv           # OBS timings (if "CSV Events" enabled)
│   ├── video.avi         # Uncompressed video (if "Raw Video" enabled)
│   └── audio.wav         # Uncompressed audio (if "Raw Video" enabled)
└── session_20240929_151234/
    └── ...
```

**Session Management:** A new session folder is automatically created each time recording is enabled. The output folder can be changed in the plugin properties.

#### Usage Notes

- **Independent Operation:** All recording operates independently of OBS Studio's built-in recording
- **Mix and Match:** All three recording options can be enabled simultaneously
- **Instant Recording:** Recording starts immediately when a checkbox is checked and continues until unchecked
- **⚠️ Persistent State:** Checkbox states persist across OBS restarts - uncheck to stop recording or risk filling disk space
- **Real-Time Writing:** Files are written in real-time as data is received from the C64 Ultimate
- **Auto-Organization:** Session folders are created automatically with proper directory structure
- **Recommended:** Enable **CSV recording** for debugging and **disable** BMP/AVI recording for normal streaming

#### Debug & Analysis CSV Logs

When **"Network and Streaming Events (CSV)"** recording is enabled, the plugin generates detailed CSV logs for debugging OBS performance and analyzing C64 Ultimate network streams. These logs enable bit-accurate recording analysis and precise frame timing measurements.

**Generated CSV Files:**

- `obs.csv` - OBS processing timeline with microsecond precision
- `network.csv` - UDP packet reception log with network timing analysis

Examples from recent automated E2E runs against a 'mocked' (i.e. simulated) Ultimate 64:

- PAL: [`obs.csv`](tests/e2e/results/pal_default/obs.csv), [`network.csv`](tests/e2e/results/pal_default/network.csv)
- NTSC: [`obs.csv`](tests/e2e/results/ntsc_default/obs.csv), [`network.csv`](tests/e2e/results/ntsc_default/network.csv)

**Sample OBS Timeline (obs.csv):**

```csv
event_type,frame_num,elapsed_us,data_size_bytes,fps,audio_samples_total,video_packets_received,audio_packets_received,sequence_errors
video,1,43874,368640,59.826,0,160,12,0
audio,0,48250,768,59.826,0,175,13,0
```

**Sample Network Analysis (network.csv):**

```csv
packet_type,elapsed_us,sequence_num,frame_num,line_num,packet_size,jitter_us
video,225,1510,7671,8,780,0
audio,2341,847,0,0,192,125
```

**Use Cases:**

- **Debug OBS Performance:** Analyze frame processing delays and audio sync issues
- **Network Stream Analysis:** Monitor UDP packet timing, jitter, and sequence errors
- **Bit-Accurate Recordings:** Capture every frame with precise timing for forensic analysis
- **C64 Ultimate Diagnostics:** Validate device streaming performance and network stability

**Sample Recording:** See [docs/recordings/session_19700101_024625](docs/recordings/session_19700101_024625) for complete examples with all file types.

**Activation:** Enable the **"Network and Streaming Events (CSV)"** checkbox in the Recording properties. CSV files are generated only when this option is explicitly enabled.

---

### Effect settings

Recreate the authentic look and feel of classic CRT monitors and TVs with configurable visual effects that simulate the characteristics of vintage displays.

![C64 Stream Effects](https://raw.githubusercontent.com/chrisgleissner/c64stream/main/docs/images/properties-effects.png "C64 Stream Effects")

**Presets:** One-click configurations for different display types

- **[Classic CRT](https://raw.githubusercontent.com/chrisgleissner/c64stream/main/docs/images/effects/classic-crt.png)** - Balanced scan lines and bloom for general retro appeal
- **[Amber Monitor](https://raw.githubusercontent.com/chrisgleissner/c64stream/main/docs/images/effects/amber-monitor.png)** - Warm amber tint reminiscent of early computer monitors
- **[Green Monitor](https://raw.githubusercontent.com/chrisgleissner/c64stream/main/docs/images/effects/green-monitor.png)** - Classic green phosphor terminal look
- **[Sharp Pixels](https://raw.githubusercontent.com/chrisgleissner/c64stream/main/docs/images/effects/sharp-pixels.png)** - Crisp pixel doubling for arcade-style clarity
- **[Phosphor Glow](https://raw.githubusercontent.com/chrisgleissner/c64stream/main/docs/images/effects/phosphor-glow.png)** - Dramatic phosphor persistence trails with extended afterglow. The sample image here was taken from the automated E2E test which shows an afterglow for each moving diagonal line.
- **[Vintage TV](https://raw.githubusercontent.com/chrisgleissner/c64stream/main/docs/images/effects/vintage-tv.png)** - Softer look with prominent scan lines for old television feel
- **[Arcade Cabinet](https://raw.githubusercontent.com/chrisgleissner/c64stream/main/docs/images/effects/arcade-cabinet.png)** - High-contrast effects for authentic arcade experience

**Customizable Effects:**

- **Scan Lines:** CRT raster line simulation with precise control (see table below). The "Scan Line Strength" slider (0.0–1.0) controls how dark the gaps appear. At 0.0, gaps are invisible; at 1.0, they are completely black.
- **Bloom:** Glow effect that makes bright pixels bleed into darker areas
- **Pixel Geometry:** Independent width/height scaling for authentic pixel aspect ratios
- **Blur Control:** Fine-tune between crisp pixels and soft scaling
- **Afterglow**: CRT phosphor persistence effect (0-250ms) with configurable decay curves
- **Screen Tint:** Amber, green, or monochrome overlays for period-accurate monitor simulation
- **Preserve preview size:** Keeps the OBS source or filter size stable when scanline and pixel settings change the internal effect size. Enabling may reduce scanline accuracy.

**Reset:** To reset to default values, simply select the "Default" preset. If you have changed individual effects whilst the "Default" preset was active, select any other preset first and then re-select the "Default" preset.


#### Perfect Scan Lines

![Perfect Scan Lines](https://raw.githubusercontent.com/chrisgleissner/c64stream/main/docs/images/perfect-scan-lines.png "Perfect Scan Lines")

> [!TIP]
> This section is for users seeking perfect display quality. The techniques described here are mostly required when using effects that include scan lines and/or pixel scaling.

The **Scan Line Distance** setting controls the gap between each pair of adjacent C64 pixel rows, simulating the dark lines between phosphor rows on a CRT monitor. Each mode uses a specific integer scaling factor internally to ensure perfectly uniform scanlines with zero variance.

With **Preserve preview size** enabled, changing presets no longer forces you to resize the scene item just to keep it in the same place. The effect still uses its own internal virtual geometry, but the OBS-facing footprint stays stable.

To achieve **pixel-perfect scanlines** without scaling artifacts such as slight blurriness, the final displayed size in OBS still matters. Because OBS does not lock aspect ratio for numeric transforms, **both height and width must be set explicitly**.

First, right-click on the C64 Stream source → **Scale Filtering → Point**. This is a one-time setting that tells OBS to use nearest-neighbor scaling.

Then, right-click the C64 Stream source in OBS → **Transform** → **Edit Transform**, then enter the exact values from the table below, assuming you are using a 1920 x 1080 ("Full HD") screen. Adjust the width/height settings as needed if you use a different screen. These target display sizes remain valid whether or not **Preserve preview size** is enabled:

| Mode       | Distance | Scale | Pattern           | Output Width | Output Height | Canvas Fit                |
| ---------- | -------- | ----- | ----------------- | ------------ | ------------- | ------------------------- |
| None       | 0%       | 4×    | No gaps           | 1456 px      | 1088 px       | Full (8 px vertical crop) |
| Tight      | 25%      | 5×    | 4 bright + 1 dark | 1820 px      | 1360 px       | Vertical overflow         |
| Normal     | 50%      | 3×    | 2 bright + 1 dark | 1092 px      | 816 px        | Letterboxed               |
| Wide       | 100%     | 4×    | 2 bright + 2 dark | 1456 px      | 1088 px       | Full (8 px vertical crop) |
| Extra Wide | 200%     | 3×    | 1 bright + 2 dark | 1092 px      | 816 px        | Letterboxed               |

The following screenshot assumes you select "Wide" scan line mode, again assuming you use a 1920 x 1080 screen:

![Edit Source Transform](https://raw.githubusercontent.com/chrisgleissner/c64stream/main/docs/images/source-transform-edit.png "Edit Source Transform in OBS")

---

### Color Palettes

Customize the VIC-II color palette to match different C64 hardware variants, personal preferences, or artistic styles. The palette system supports both shipped (preset) and user-defined (custom) palettes.

![C64 Stream Palettes](https://raw.githubusercontent.com/chrisgleissner/c64stream/main/docs/images/properties-palettes.png "C64 Stream Palettes")

**Shipped Palettes:** The plugin includes the following preset palettes:

- **Default** - Standard VIC-II colors matching original C64 hardware
- **Cool** - Blue/cyan color temperature shift
- **Inverted** - RGB color inversion (negative image)
- **Monochrome** - Grayscale conversion
- **Muted** - Reduced saturation with pastel-like tones
- **Neon Blast** - Maximum saturation for high-intensity colors
- **Night** - Red-shifted colors for comfortable late night viewing
- **Vibrant** - Increased color saturation for enhanced visual impact
- **Warm** - Amber/orange color temperature shift

**Palette Controls:**

- **Palette Dropdown:** Select from shipped palettes or any custom palettes you've added
- **Import palette:** Imports a `.vpl` file
- **Export palette:** Exports the currently active palette (with any color adjustments) to a `.vpl` file
- **Color Editor:** Expand to access 16 color pickers (0-15) for editing individual VIC-II colors. Changes apply immediately to the video output

**Auto-Save Behavior:**

- Custom palettes are automatically saved when you edit them in the color editor
- **Preset modifications:** If you edit a shipped preset palette, a custom copy is automatically created with the same name (the original preset remains unchanged)
- The settings automatically update to use the custom copy, so your changes persist across OBS restarts
- No manual save action is required for palette edits

**VPL Palette Format:**

Palettes use the standard [VICE VPL](https://1541u-documentation.readthedocs.io/en/latest/howto/palette.html) format:

```
# VICE Palette file
#
# Syntax:
# Red Green Blue
#
# TYPE:VICII
# NAME:My Palette
# DESC:Optional description shown as tooltip

00 00 00
FF FF FF
8D 2F 34
...
```

- **NAME:** (optional) Display name shown in the dropdown
- **DESC:** (optional) Description shown as tooltip when hovering over the palette
- First 16 non-comment lines are RGB hex values in `RR GG BB` format (space-separated)
- Files must have exactly 16 color entries

**Storage:** Custom palettes are saved to the [palettes directory](#file-system-structure). Shipped palettes are bundled with the plugin as read-only defaults.

### Import/Export Configuration

Import and export your complete plugin settings:

- **Import settings:** Click to load settings from a previously exported `.ini` file. All current settings will be replaced
- **Export settings:** Click to save all current settings to a `.ini` file. Use this to backup configurations, share setups, or attach to bug reports

Exported configurations are saved to the [settings directory](#file-system-structure).

---

### Remote Control

> [!NOTE]
> **New in version 1.1:** Please share feedback or open an issue if you notice anything that could be improved.

Control your Ultimate 64 remotely from within OBS Studio, enabling keyboard input capture and automated content playback.

![C64 Stream Remote Control](https://raw.githubusercontent.com/chrisgleissner/c64stream/main/docs/images/properties-remote-control.png "C64 Stream Remote Control")

**Features:**

- **Keyboard Capture:** Type directly into the C64 from OBS preview window with intelligent backpressure handling to help avoid lost keystrokes
- **Automated Playback:** Unattended playback of SID/MOD music, PRG/CRT programs, and disk images (D64, G64, D71, G71 or D81) with shuffle support. When using disk images, it loads and starts the first program in the volume using `LOAD"*",8,1:RUN`.
- **REST API Control:** Programmatic access to C64 Ultimate functions (reset, memory access, disk mounting)

**Configuration:**

- **Keymap:** Select keyboard mapping for converting PC keystrokes to C64 PETSCII codes:
  - **Symbolic keymaps:** Match key labels. For example press the `[` key on a US PC keyboard and see the `[` character on the C64U.
  - **Positional keymaps:** Match physical location of keys. For example press the `[` key on US PC keyboard and see the `@` character on the C64U. This is because the PC `[` key is in the same physical location as the C64 `@` key, to the right of the `P` key.
  - Supports built-in and custom user keymaps (`.c64keymap.ini` format)
  - **Held keys and games:** On firmware with the `machine:input` API, a key you hold stays pressed on the C64 for as long as you hold it (rather than a brief tap), so action games that read keys directly — e.g. pinball flippers — respond correctly. The two keymap types differ in how held **modifier** keys reach the C64: **positional** treats Shift, Ctrl and the Commodore key (mapped to the PC **Alt** key) as the literal C64 matrix keys and holds them down, so holding Left/Right Shift drives the two keyboard "flippers" that many games (e.g. *David's Midnight Magic*) use, and games that read Ctrl or the Commodore key see them held. **Symbolic** instead consumes these modifiers to pick characters for typing, so they do not act as held game keys there. Choose **positional** for such games. Ordinary keys (letters, digits, cursor keys, Space) hold identically in both modes. On older firmware without `machine:input`, keys always fall back to a single tap and cannot be held.
- **Joystick Emulation:** When enabled, arrow keys and Space move/fire a virtual joystick instead of sending C64 keystrokes. Toggle live anytime with **F10** — an open Properties dialog updates to match.
- **Joystick Port:** Which C64 joystick port (1 or 2, default 2) receives emulated input. Toggle live anytime with **F11**.
- **File System:** Choose between local files or C64 Ultimate storage
- **Playback Source:** Pick **Single File** or **Folder**
- **Local/C64U Path:** File or folder path, shown based on file system + playback source
- **Include Subfolders:** Include subfolders when enumerating folder playback (Folder only)
- **Shuffle Playback:** Randomize folder playback order (Folder only)
- **Duration per Item:** Playback duration in seconds before advancing (1-3600s, default 120s)
- **Use Songlengths:** When enabled for local SID playback, uses a Songlengths.md5 file to set per-song durations
- **Songlengths Path:** Optional path to a Songlengths.md5 file (local + songlengths enabled only)
- **Reset Between Items:** Perform soft reset between items
- **Playing:** Shows the current queue; selecting an entry jumps to that item (switches immediately while playing)
- **Refresh Playlist:** Rebuilds the playlist based on current settings (disabled while playing)
- **Play/Stop Content:** Starts or stops automated playback
- **Next:** Skips to the next item while playing
- **Reset Plugin:** Restarts the plugin state (no OBS restart needed)
- **Reset C64U:** Sends a C64 Ultimate reset

### Keyboard Capture

Keyboard input can be sent to the C64 through the OBS **Interact** window. In Auto mode, supported devices use the REST keyboard matrix endpoint, including shifted chords and `release_all` on teardown. Devices without that endpoint transparently fall back to the KERNAL keyboard buffer for representable text input. C64Script `TYPE`, `KEY`, `LOAD`, `RUN`, and `SYS` use this same input path.

#### Activating Keyboard Capture

1. Click inside the **OBS preview**.
2. Click **Interact** below the preview.
3. Type normally. Keystrokes are translated to PETSCII and injected into the C64.

To return keyboard control to OBS, click outside the Interact window and close it.

#### Compatibility

The legacy fallback only works with programs that read input via the **KERNAL keyboard buffer**. REST matrix input can also drive supported direct-key programs.

Many programs (especially games) read key state directly from **CIA1**. Since the **C64 Ultimate** does not allow writing to CIA1 registers, keyboard input will not work for those programs.

**Reset** and **Reboot** are exceptions because they use the **C64U REST API** and do not rely on keyboard injection.

#### Special Key Mappings

| OBS         | C64 Function         |
| ----------- | -------------------- |
| CTRL+META   | Toggle Character Set |
| ALT         | CBM                  |
| ESC         | RUN/STOP             |
| ESC + SHIFT | Reset                |
| ESC + TAB   | Reboot               |

On many keyboards, the **META** key corresponds to the **Windows key**.

Unlike the table above, the following are plugin-level hotkeys rather than C64 keystrokes, and work regardless of the current Keymap or Joystick Emulation state:

| Key | Action                                            |
| --- | ------------------------------------------------- |
| F9  | Toggle the device's on-screen configuration menu   |
| F10 | Toggle Joystick Emulation on/off                   |
| F11 | Toggle the Joystick Emulation port (1 ↔ 2)         |

---

### C64Script Automation

> [!NOTE]
> **New in version 1.1:** Please share feedback or open an issue if you notice anything that could be improved.

Automate your C64 stream with C64Script - a modernized BASIC-like language designed specifically for stream control.

![C64 Script](https://raw.githubusercontent.com/chrisgleissner/c64stream/main/docs/images/properties-script.png "C64 Script")

**What is C64Script?**

C64Script is like a simplified, modernized version of Commodore BASIC. If you've ever typed `10 PRINT "HELLO"` on a C64, you'll feel right at home. The language lets you:

- Control visual effects and palettes
- Play SID music and run C64 programs
- Read and write the C64 RAM
- Run programs on your OBS device and make HTTP requests to link your stream to external events
- Start and stop recordings
- Type text and press keys automatically
- Wait for specific times or conditions
- Use variables, loops, and conditional logic

> [!NOTE]
> C64Script never runs automatically without your consent.
> Scripts execute only when you explicitly start them from the Properties window, ensuring full control at all times.
> If desired, you may enable automatic execution when the plugin starts by selecting the Auto-start script checkbox.

**Quick Start Example - Color Palette Cycle:**

Let's run the demo program [demo_palette_cycle.c64script](./data/scripts/demo_palette_cycle.c64script) that ships with the plugin in the `scripts` folder:

1. Click on **Browse** to the right of **Script File** and select the script.
2. Click **Start Script**.
3. You should now see C64 Stream cycle through all of its color palettes.

**Debugging Your Scripts:**

The plugin includes built-in controls for running and inspecting scripts:

- **Script File** - Select the `.c64script` to run
- **Auto Start** - Start the script automatically when the source becomes active
- **Script Status** - High-level script status (idle, running, paused, error, completed)
- **Start/Stop** - Start your script from the beginning or stop it
- **Pause/Resume** - Pause at any point to inspect what's happening
- **Step** - Execute your script one line at a time (only while paused)
- **Log variables** - See all variable values in the OBS log
- **Execution state** - Shows running, paused, error, or completed
- **Last executed** - Shows which line just ran
- **Next to execute** - Shows which line will run next
- **Last error** - Shows the most recent runtime error when one occurs

Assuming you already ran the palette cycling script described earlier, let's now try and debug it:

1. Click **Start Script** to run the script
2. Click **Pause Script** and then **Step** to walk through line-by-line
3. Click **Log variables** to see any variables in the OBS log (Help → Log Files → Show Current Log)

**Syntax Highlighting in VS Code**

To enable syntax highlighting for `.c64script` files in [VS Code](https://code.visualstudio.com/):

```bash
./build-aux/install-c64script-syntax.sh
```

Then reload VS Code (`Ctrl+Shift+P` → "Reload Window"). The language mode should show "C64Script" in the bottom-right corner when viewing `.c64script` files.

**Learn More:**

- **Getting Started:** [`doc/c64script/c64script-tutorial.md`](doc/c64script/c64script-tutorial.md) - A gentle C64Script tutorial with practical starter examples
- **Full Language Reference:** [`doc/c64script/c64script-spec.md`](doc/c64script/c64script-spec.md) - Complete C64Script language documentation
- **Debugging Guide:** [`doc/c64script/c64script-debugging.md`](doc/c64script/c64script-debugging.md) - Detailed debugging workflows and tips
- **Example Scripts:** [`data/scripts/`](data/scripts/) - Demo scripts showing effects, palettes, and automation

Common effect-layout automation examples:

- `EFFECTPARAM "preserve_size" 1` keeps the OBS footprint stable while you cycle effects.
- `EFFECTPARAM "preserve_size" 0` keeps scan lines accurate by letting effect scaling change the source or filter size.

## 🛟 Troubleshooting

### Plugin missing from OBS?

- Confirm OBS Studio version 32.0.1+
- Verify plugin installed to correct directory
- Check OBS logs for plugin loading errors
- Restart OBS completely after installation

### No video stream?

- Verify that both IP addresses are correct
- Check Ultimate device has data streaming enabled
- Confirm firewall allows UDP traffic on configured ports

### Lost / Repeated Frames?

- May occur when OBS cannot keep up, typically during high CPU or GPU load.
- Reduce or disable filters. The afterglow effect is particularly CPU-intensive. Test the **Default** preset with all filters disabled.
- Lower the output resolution to 1280×720.
- Disable OBS recording and any plugin-side recording.

### Effects not working?

- **No visual change:** Ensure source is active and receiving video data
- **Performance drops:** Complex effects (high bloom/blur) may impact frame rate on older hardware
- **Preset not applying:** Try manually adjusting individual effect settings

### Audio sync issues?

- Check audio port configuration (default 11001)
- Verify OBS audio monitoring settings
- **Buffer delay changes:** If you first increase the network buffer delay (e.g., to 500ms) and then decrease it (e.g., to 200ms), audio may become delayed relative to video. **Workaround:** Remove and re-add the C64 Stream source, or restart OBS Studio to reset the audio timing reference. For best results, set your desired buffer delay when initially configuring the source.

### Connection acting up?

- Network latency should be <100ms for optimal performance
- Check for network congestion or Wi-Fi interference
- Consider wired Ethernet connection for stability

### Hostname not resolving?

If the plugin can't resolve your C64 Ultimate hostname (e.g., `c64u`), try these solutions:

*Quick Fix:*

1. **Use IP Address:** Instead of `c64u`, enter the device's IP address directly (e.g., `192.168.1.64`)
2. **Check DNS Server IP:** Verify the DNS Server IP setting matches your router's IP address
   - Common router IPs: `192.168.1.1`, `192.168.0.1`, `10.0.0.1`
   - Find your router IP: Run `ip route | grep default` (Linux) or `ipconfig` (Windows)

*Advanced Troubleshooting:*

1. **Test DNS Resolution Manually:**

   ```bash
   # Linux/macOS - Test if router can resolve the hostname
   dig @192.168.1.1 c64u

   # Windows - Test DNS resolution
   nslookup c64u 192.168.1.1
   ```

2. **Platform-Specific Issues:**
   - **Linux:** systemd-resolved may not forward local hostnames to router DNS
   - **macOS:** Similar DNS forwarding issues with local device names
   - **Windows:** System DNS typically works without issues

3. **Configure Custom DNS Server:**
   - Set **DNS Server IP** to your router's IP address (usually `192.168.1.1`)
   - Try alternative common router IPs: `192.168.0.1`, `10.0.0.1`
   - Check your router's DHCP settings for the correct DNS server IP

4. **Enable Debug Logging:**
   - In the source properties, open the **Recording** section and enable **Debug Logging**
   - Look for DNS resolution messages in OBS logs
   - Messages show which DNS resolution method succeeded

*Alternative Solutions:*

- **Static DNS Entry:** Add `192.168.1.64 c64u` to your system's hosts file
- **mDNS/Bonjour:** Use `.local` suffix (e.g., `c64u.local`) if your network supports it
- **Router Configuration:** Ensure your router's DNS server has the device hostname registered

### Recording troubles?

- **Files not created:** Verify output folder path exists and is writable
- **Performance drops with BMP saving:** Frame saving impacts performance significantly; disable if not needed
- **Large disk usage:** AVI recording creates uncompressed files (~50MB/minute); monitor disk space
- **Recording stops unexpectedly:** Check disk space and folder permissions

## 📚 Reference

### Default settings

The plugin uses a `properties.ini` file to provide default settings for connecting to your C64 Ultimate device. This file is automatically installed with the plugin and contains the standard C64 Ultimate network settings:

- **Hostname**: `c64u` (the default C64 Ultimate hostname)
- **Control Port**: `64` (the standard C64 Ultimate control port)
- **DNS Server**: `192.168.1.1` (common router DNS)
- **Video/Audio Ports**: `11000`/`11001` (C64 Ultimate streaming ports)

These settings work out-of-the-box with most C64 Ultimate setups. You can override any of these settings directly in the OBS source properties if your setup differs.

### File System Structure

The plugin uses three distinct filesystem locations:

#### 1. Plugin

This folder contains OBS plugin binary and loader files.

OBS Studio searches for plugins in multiple locations. The installation location depends on how you installed the plugin:

| Platform    | Package Install (System-Wide)                                       | User Install (Local Development)                                     |
| ----------- | ------------------------------------------------------------------- | -------------------------------------------------------------------- |
| **Windows** | `C:\ProgramData\obs-studio\plugins\c64stream\`                      | N/A (uses system path)                                               |
| **macOS**   | `/Library/Application Support/obs-studio/plugins/c64stream.plugin/` | `~/Library/Application Support/obs-studio/plugins/c64stream.plugin/` |
| **Linux**   | `/usr/lib/obs-plugins/c64stream.so`                                 | `~/.config/obs-studio/plugins/c64stream/bin/64bit/c64stream.so`      |

#### 2. Shipped Data

This folder contains read-only defaults bundled with the plugin.

OBS searches in this order:

1. User plugin directory (if it exists)
2. System plugin directory

The data directory contains:

- Effect presets (`effect_presets.ini`)
- Palette presets (`palettes/*.vpl`)
- Default network settings (`properties.ini`)
- Localization files (`locale/*.ini`)
- C64 programs (`prg/*.prg`)
- C64Script files (`scripts/*.c64script`)

| Platform    | Package Install (System-Wide)                                                          | User Install (Local Development)                                                        |
| ----------- | -------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| **Windows** | `C:\ProgramData\obs-studio\plugins\c64stream\data\`                                    | N/A (uses system path)                                                                  |
| **macOS**   | `/Library/Application Support/obs-studio/plugins/c64stream.plugin/Contents/Resources/` | `~/Library/Application Support/obs-studio/plugins/c64stream.plugin/Contents/Resources/` |
| **Linux**   | `/usr/share/obs/obs-plugins/c64stream/`                                                | `~/.config/obs-studio/plugins/c64stream/data/`                                          |

> [!NOTE]
> **Package Install** is for end users who install via `.deb` packages, `.pkg` installers, or `.zip` extraction to system directories.
>
> **User Install** is for local development using VSCode build tasks or `./build --install`. The E2E tests also use these user install paths.

#### 3. User Data

This folder contains your custom content.

For easy access, simple backups, and visibility, it is always stored in `<Documents>/obs-studio/c64stream/`, regardless of how you installed the plugin:

```
<Documents>/obs-studio/c64stream/
├── settings/        # Exported configuration files (.ini)
├── scripts/         # Sample automation scripts (.c64script)
├── palettes/        # Custom palette files (.vpl)
└── recordings/      # Recording session folders
```

**Sample Locations:**

- **Windows:** `%USERPROFILE%\Documents\obs-studio\c64stream\` (e.g., `C:\Users\YourName\Documents\obs-studio\c64stream\`)
- **macOS:** `~/Documents/obs-studio/c64stream/` (e.g., `/Users/yourname/Documents/obs-studio/c64stream/`)
- **Linux:** `~/Documents/obs-studio/c64stream/` (e.g., `/home/yourname/Documents/obs-studio/c64stream/`)

### Network Details

#### Hostname vs IP Address

The plugin supports both **hostnames** and **IP addresses** for the C64 Ultimate Host field with enhanced DNS resolution that works reliably across all platforms:

**Using Hostnames (Recommended):**

- **Default:** `c64u` - The plugin will try to resolve this hostname to an IP address
- **Custom:** `my-c64u` or `retro-pc` - Use any hostname your C64 Ultimate device is known by
- **FQDN Support:** The plugin automatically tries both `hostname` and `hostname.` (with trailing dot) for proper DNS resolution

**Using IP Addresses:**

- **Direct IP:** `192.168.1.64` - Standard IPv4 address format
- **Fallback:** `0.0.0.0` - Accept streams from any C64 Ultimate (no automatic control)

#### DNS Resolution

The plugin offers hostname resolution that works reliably on Linux and macOS where system DNS may fail for local device names:

1. **System DNS First:** Tries standard system DNS resolution (works for internet hostnames and properly configured networks)
2. **FQDN Resolution:** Attempts resolution with trailing dot (e.g., `c64u.` for some network configurations)
3. **Direct DNS Queries:** On Linux/macOS, bypasses systemd-resolved by querying DNS servers directly:
   - Uses configured **DNS Server IP** (default: `192.168.1.1`)
   - Falls back to common router IPs: `192.168.0.1`, `10.0.0.1`, `172.16.0.1`

#### C64 Ultimate Setup

**Automatic Configuration (Recommended):** The OBS plugin automatically controls streaming on the Ultimate device. When you configure the Ultimate's hostname or IP address in the OBS plugin settings, the plugin tells the Ultimate device where to send streams and sends start commands automatically. Thus, no manual streaming adjustments are needed on the Ultimate device.

**Manual Configuration:**

1. Press F2 to access the Ultimate's configuration menu
2. Navigate to "Data Streams" section
3. Set "Stream VIC to" field: `your-obs-ip:11000` (e.g., `192.168.1.100:11000`)
4. Set "Stream Audio to" field: `your-obs-ip:11001` (e.g., `192.168.1.100:11001`)
5. Save configuration changes
6. Manually start streaming from the Ultimate device

For comprehensive configuration details, refer to the [official C64 Ultimate documentation](https://1541u-documentation.readthedocs.io/en/latest/data_streams.html).

### Technical Details

This plugin implements the [C64 Ultimate Data Streams specification](./doc/c64/c64u-stream-spec.md) to receive video and audio streams from Ultimate devices via UDP/TCP network protocols.

**Supported Platforms:**

- Windows 10/11 (x64) - verified on Windows 11
- Linux with X window system or Wayland - verified on Kubuntu 24.04
- macOS 11+ (Intel/Apple Silicon) - verified on macOS Sequoia 15.7 and Tahoe 26.4

**Software Requirements:**

- [OBS Studio 32.0.1](https://obsproject.com/download) or above

**Hardware Requirements:**

One of:

- [Commodore 64 Ultimate](https://www.commodore.net/)
- [Ultimate 64 Elite](https://ultimate64.com/Ultimate-64-Elite)
- [Ultimate 64 Elite MK2](https://ultimate64.com/Ultimate-64-Elite-MK2)

**Video Formats:**

- PAL: 384x272 @ 50Hz
- NTSC: 384x240 @ 60Hz
- Color space: VIC-II palette with automatic RGB conversion
- **CRT Effects:** GPU-accelerated shader-based post-processing with configurable presets

**Audio Format:**

- 16-bit stereo PCM
- Sample rate: ~48kHz (device dependent)
- Low-latency streaming

**Network Requirements:**

- UDP/TCP connectivity to Ultimate device
- Bandwidth: ~22 Mbps total (21.7 Mbps video + 1.4 Mbps audio, uncompressed streams)
- Built-in UDP jitter compensation via configurable frame buffering

For reliable audio, connect the Ultimate and OBS machine by wired Ethernet on
the same switch. Wi-Fi and powerline links are the most common cause of packet
loss and audible crackle. C64 Stream conceals isolated audio losses and reports
sustained transport errors in the OBS log as `Network errors (last 60s): …`.
Healthy streams do not emit periodic network-status messages at normal log
level; enable debug logging when diagnosing a network problem.

**Recording Formats:**

- BMP frames: 24-bit uncompressed bitmap images
- AVI video: Uncompressed BGR24 format with precise timing
- WAV audio: 16-bit stereo PCM, sample rate matches C64 Ultimate output
- Session organization: Automatic timestamped folder creation

## 📹 End-to-end tests

This project is continuously validated with automated end-to-end (E2E) tests that simulate a C64 Ultimate, drive OBS, and verify the full pipeline from UDP packets to recorded video/audio.

Each test scenario produces a short, self-contained report with packet stats, frame progression information, A/V synch details, as well as the recorded video and a sample frame from that video. Here's an example from the `ntsc_default` scenario with no effects applied:

![E2E screenshot for ntsc_default](./tests/e2e/results/ntsc_default/c64_recording_still.png)

Here's another one from the `ntsc_green_monitor` scenario. You see how the frame progress counter on the bottom left and the central diagonal moving lines both left behind afterglow trails:

![E2E screenshot for ntsc_green_monitor](./tests/e2e/results/ntsc_green_monitor/c64_recording_still.png)

Many recent reports (without videos) are checked into this GitHub repository:

- [Main E2E results](tests/e2e/results/README.md)
- [PAL results](tests/e2e/results/pal_default/README.md)
- [NTSC results](tests/e2e/results/ntsc_default/README.md)

You can download the latest E2E results (with videos) from GitHub:

1. Open the overview of all [recent builds](https://github.com/chrisgleissner/c64stream/actions/workflows/push.yaml?query=branch%3Amain+is%3Asuccess).
2. Click on the latest (i.e. top-most) build titled `main push run`, then scroll down to its build artifacts.
3. Download the ZIP file titled `e2e-all-results-$gitId`, e.g. [https://github.com/chrisgleissner/c64stream/actions/runs/26213806030/artifacts/7131040647](https://github.com/chrisgleissner/c64stream/actions/runs/26213806030/artifacts/7131040647)
4. Unzip it to see the test result folders, one per E2E test.

For more information, see [`doc/testing/e2e.md`](doc/testing/e2e.md).

## 🛠️ For Developers

See the [Developer Documentation](doc/developer.md) for build instructions, testing procedures, and contribution guidelines.

## ⚖️ License

This project is licensed under the GPL v2 License - see the [LICENSE](LICENSE) file for details.
