# C64 Ultimate Data Stream Specification

Source: https://1541u-documentation.readthedocs.io/en/latest/data_streams.html#data_streams

## Overview

The C64 Ultimate provides three primary real-time data streams over its built-in Ethernet interface:

1. VIC Video Stream (ID 0)
2. Audio Stream (ID 1)
3. Debug Stream (ID 2)

All streams are intended for debugging, regression testing, cycle-accurate analysis, and lossless digital capture of video and audio output directly from the hardware, without analog conversion.

## Network Protocol

- **Transport:** UDP (connectionless)
- **Delivery Model:** Best effort, no guaranteed delivery, no retransmission
- **Latency Characteristics:** Minimal, determined by packetization and network stack
- **Control Channel:** TCP socket `64` on the C64 Ultimate for stream control commands

### Addressing Modes

The C64 Ultimate supports three transmission modes, selected implicitly by the destination IP address:

| Mode      | Address Type                | Notes                                         |
| --------- | --------------------------- | --------------------------------------------- |
| Unicast   | Single host IPv4 address    | Recommended when IGMP snooping is unavailable |
| Multicast | 224.0.0.0 – 239.255.255.255 | Requires receiver to join multicast group     |
| Broadcast | Network broadcast address   | Not recommended on shared networks            |

### Multicast Operation Details

- Multicast operation relies on IGMP for group membership management.
- IGMP snooping capable switches selectively forward multicast traffic only to interested ports.
- Without IGMP snooping, multicast traffic is treated as broadcast and floods the LAN.
- IGMP switching is based solely on IP address. UDP port numbers are not considered.
- To receive multiple streams via multicast, distinct multicast IP addresses must be used.

### Practical Recommendation

- Unicast is recommended on home or mixed wired/Wi-Fi networks.
- Multicast is suitable on controlled wired LANs with IGMP snooping enabled.
- Broadcast should be avoided due to unnecessary network load.

## Video Stream (ID 0)

### Stream Purpose

The VIC Video Stream represents the active VIC-II video output window, digitally sampled and transmitted in real time.

The stream does not represent the full physical video output resolution. Instead, it transmits a precisely defined cropped window aligned to reference images used by the VICE test suite.

### Stream Format

- **Packet Size:** 780 bytes
  - Header: 12 bytes
  - Payload: 768 bytes
- **Color Depth:** 4-bit VIC color indices
- **Encoding:** Uncompressed (encoding type = 0)

### Resolution and Cropping Behavior

| Video Mode | Physical Output Resolution | Streamed Resolution |
| ---------- | -------------------------- | ------------------- |
| PAL        | 400 × 288                  | 384 × 272           |
| NTSC       | 400 × 240                  | 384 × 240           |

Cropping details:

- Horizontal cropping: 8 pixels removed from each side (400 → 384)
- Vertical cropping (PAL): 8 lines removed top and bottom (288 → 272)
- Vertical cropping (NTSC): no vertical cropping


### Frame Structure

- **Lines per packet:** 4
- **Packets per frame:**
  - PAL: 272 ÷ 4 = 68 packets
  - NTSC: 240 ÷ 4 = 60 packets

Each packet contains four consecutive raster lines.

### Packet Header (12 bytes)

| Offset | Field            | Description                                      |
| ------ | ---------------- | ------------------------------------------------ |
| 0–1    | Sequence number  | 16-bit LE, increments per packet                 |
| 2–3    | Frame number     | 16-bit LE                                        |
| 4–5    | Line number      | 16-bit LE, bit 15 indicates last packet of frame |
| 6–7    | Pixels per line  | Always 384                                       |
| 8      | Lines per packet | Always 4                                         |
| 9      | Bits per pixel   | Always 4                                         |
| 10–11  | Encoding type    | Always 0 (uncompressed)                          |

### Pixel Data

- **Format:** 4-bit VIC color indices
- **Byte Order:** Little-endian, low nibble first
- **Payload Layout:**
  4 lines × 384 pixels = 1536 pixels = 768 bytes

### VIC Color Mapping (4-bit)

The following RGB colors are only indicative examples. The actual RGB color mapping depends on the configured .vpl VICE palette file.

| Code | Color Name  | RGB Hex |
| ---- | ----------- | ------- |
| 0    | Black       | #000000 |
| 1    | White       | #FFFFFF |
| 2    | Red         | #9F4E44 |
| 3    | Cyan        | #6ABFC6 |
| 4    | Purple      | #A057A3 |
| 5    | Green       | #5CAB5E |
| 6    | Blue        | #50459B |
| 7    | Yellow      | #C9D487 |
| 8    | Orange      | #A1683C |
| 9    | Brown       | #6D5412 |
| 10   | Light Red   | #CB7E75 |
| 11   | Dark Grey   | #626262 |
| 12   | Mid Grey    | #898989 |
| 13   | Light Green | #9AE29B |
| 14   | Light Blue  | #887ECB |
| 15   | Light Grey  | #ADADAD |

### Authentic C64 Display Border Dimensions

Inner screen size is identical for PAL and NTSC: **320 × 200 pixels**

| Mode | Borders (L R T B) | Streamed Resolution |
| ---- | ----------------- | ------------------- |
| NTSC | 32 32 20 20       | 384 × 240           |
| PAL  | 32 32 35 37       | 384 × 272           |

Horizontal borders are symmetric. Vertical borders differ due to PAL timing.

## Audio Stream (ID 1)

### Stream Purpose

The audio stream is sourced after the internal audio mixer and represents the exact digital audio signal delivered to HDMI and the analog audio codec.

### Stream Format

- **Packet Size:** 770 bytes
  - Header: 2 bytes
  - Payload: 768 bytes
- **Samples per Packet:** 192 stereo samples
- **Sample Format:** 16-bit signed, little-endian
- **Channel Order:** Left, Right interleaved

### Exact Sample Rates

Derived directly from hardware clock frequencies:

| Mode | Exact Frequency     | Deviation from 48 kHz |
| ---- | ------------------- | --------------------- |
| PAL  | 47982.8869047619 Hz | −356.52 ppm           |
| NTSC | 47940.3408482143 Hz | −1242.9 ppm           |

Rounded values (47983 / 47940 Hz) are for display only.

### Packet Structure

| Offset | Field           |
| ------ | --------------- |
| 0–1    | Sequence number |
| 2–769  | Audio samples   |

## Debug Stream (ID 2)

### Overview

Firmware requirement: version 3.7 or higher (Ultimate firmware ≥ 1.28).

The Debug Stream provides cycle-accurate tracing of internal bus activity for advanced diagnostics, emulator validation, and hardware-level timing analysis.

### Supported Debug Sources

- 6510 CPU
- VIC-II
- 1541 Drive CPU

Supported combinations:

- 6510 only
- VIC only
- 6510 & VIC
- 1541 only
- 6510 & 1541

### Bandwidth Constraints

- Each debug configuration consumes approximately 32 Mbps.
- Due to the 100 Mbps Ethernet limit:
  - Debug Stream cannot be used simultaneously with the Video Stream.
  - Not all debug combinations can be enabled together.

### Packet Structure

- **Payload size:** 1444 bytes
- **Entries per packet:** 360
- **Entry size:** 32 bits
- **Header:**
  - 16-bit sequence number
  - 16-bit reserved field

### Debug Entry Formats

#### 6510 / VIC

| Bit    | Signal  |
| ------ | ------- |
| 31     | PHI2    |
| 30     | GAME#   |
| 29     | EXROM#  |
| 28     | BA      |
| 27     | IRQ#    |
| 26     | ROM#    |
| 25     | NMI#    |
| 24     | R/W#    |
| 23..16 | Data    |
| 15..0  | Address |

#### 1541

| Bit    | Signal     |
| ------ | ---------- |
| 31     | 0          |
| 30     | ATN        |
| 29     | DATA       |
| 28     | CLOCK      |
| 27     | SYNC       |
| 26     | BYTE_READY |
| 25     | IRQ#       |
| 24     | R/W#       |
| 23..16 | Data       |
| 15..0  | Address    |

### Tooling

- `grab_debug.py` for capture
- `dump_bus_trace.c` for VCD conversion
- Compatible with GTKWave and similar viewers

## Control Commands

### Command Interface

- **Transport:** TCP
- **Port:** 64

### Commands

| Action         | Command |
| -------------- | ------- |
| Enable stream  | FF2n    |
| Disable stream | FF3n    |

Where `n` is the stream ID.

---

### Parameters

1. Duration in 5 ms ticks (0 = infinite)
2. Destination string (optional)

---

### Examples

```
Enable stream 0 for 1 second:
20 FF 02 00 00 C8

Enable stream 0 indefinitely to 192.168.0.119:
20 FF 0F 00 00 00 192.168.0.119

Disable stream 0:
30 FF 00 00
```
