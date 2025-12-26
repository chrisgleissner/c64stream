# C64 Ultimate Data Stream Specification

Source: https://1541u-documentation.readthedocs.io/en/latest/data_streams.html#data_streams

## Overview
The C64 Ultimate device provides two primary data streams over its Ethernet port:
1. VIC Video Stream (ID 0)
2. Audio Stream (ID 1)

## Network Protocol
- Transport: UDP (connectionless)
- Mode: Unicast or Multicast (224.0.0.0 to 239.255.255.255)
- Reliability: Best effort, no guaranteed delivery
- Connection: Via TCP socket '64' for control commands

## Video Stream (ID 0)

### Stream Format
- Packet Size: 780 bytes (12 byte header + 768 byte payload)
- Resolution: 384 x 272 (PAL) or 384 x 240 (NTSC)
- Color Depth: 4-bit VIC colors
- Frame Structure: 68 packets of 4 lines each (PAL)

### Packet Header (12 bytes)
1. Sequence number (16-bit LE)
2. Frame number (16-bit LE)
3. Line number (16-bit LE, bit 15 = last packet of frame)
4. Pixels per line (16-bit LE) = 384
5. Lines per packet (8-bit) = 4
6. Bits per pixel (8-bit) = 4
7. Encoding type (16-bit) = 0 (uncompressed)

### Pixel Data
- Format: 4-bit VIC colors, little-endian
- Organization: 4 lines x 384 pixels = 768 bytes
- Alignment: Matches VICE test suite reference images

### VIC Color Mapping (4-bit)
| Code | Color Name   | RGB Hex  | RGB Decimal        |
|------|-------------|----------|-------------------|
| 0    | Black       | #000000  | (0, 0, 0)        |
| 1    | White       | #FFFFFF  | (255, 255, 255)   |
| 2    | Red         | #9F4E44  | (159, 78, 68)    |
| 3    | Cyan        | #6ABFC6  | (106, 191, 198)  |
| 4    | Purple      | #A057A3  | (160, 87, 163)   |
| 5    | Green       | #5CAB5E  | (92, 171, 94)    |
| 6    | Blue        | #50459B  | (80, 69, 155)    |
| 7    | Yellow      | #C9D487  | (201, 212, 135)  |
| 8    | Orange      | #A1683C  | (161, 104, 60)   |
| 9    | Brown       | #6D5412  | (109, 84, 18)    |
| 10   | Light Red   | #CB7E75  | (203, 126, 117)  |
| 11   | Dark Grey   | #626262  | (98, 98, 98)     |
| 12   | Mid Grey    | #898989  | (137, 137, 137)  |
| 13   | Light Green | #9AE29B  | (154, 226, 155)  |
| 14   | Light Blue  | #887ECB  | (136, 126, 203)  |
| 15   | Light Grey  | #ADADAD  | (173, 173, 173)  |

### Authentic C64 Display Border Dimensions

Color transition analysis of C64 bootscreen data reveals exact VIC-II border dimensions:

**Border Colors**
- Outer Border: Light Blue (`#6D6AEF` / VIC color 14)
- Inner Screen: Blue (`#2C29B1` / VIC color 6)

**NTSC (384×240)**: L32|R32|T20|B20 → **320×200 screen**
**PAL (384×272)**: L32|R32|T35|B37 → **320×200 screen**

Both formats maintain identical 320×200 inner screen dimensions with symmetric horizontal borders.

## Audio Stream (ID 1)

### Stream Format
- Packet Size: 770 bytes (2 byte header + 768 byte payload)
- Sample Rate:
  - PAL: 47983 Hz (-356 ppm from 48kHz)
  - NTSC: 47940 Hz (-1243 ppm from 48kHz)

### Packet Structure
1. Header: Sequence number (16-bit)
2. Payload: 192 stereo samples
   - Format: 16-bit signed, little-endian
   - Channels: Left/Right interleaved
   - Sample Structure: [L][R][L][R]... (4 bytes per stereo sample)

## Data Transmission Analysis

### Video Stream Calculations

**PAL (384×272, 50.125 Hz)**
- Frame Size: 384 × 272 = 104,448 pixels
- Pixels per Packet: 384 × 4 = 1,536 pixels
- Packets per Frame: 272 ÷ 4 = **68 packets**
- Frame Interval: 19.95 ms (50.125 Hz)
- Packet Interval: 19.95 ms ÷ 68 = 0.293 ms/packet
- Packet Rate: **68 packets per 19.95 ms = 3,408 packets/sec**
- Data Rate: 68 × 780 bytes × 50.125 fps = 2,658,630 bytes/sec = **21.269 Mbps**

**NTSC (384×240, 59.826 Hz)**
- Frame Size: 384 × 240 = 92,160 pixels
- Pixels per Packet: 384 × 4 = 1,536 pixels
- Packets per Frame: 240 ÷ 4 = **60 packets**
- Frame Interval: 16.71 ms (59.826 Hz)
- Packet Interval: 16.71 ms ÷ 60 = 0.279 ms/packet
- Packet Rate: **60 packets per 16.71 ms = 3,590 packets/sec**
- Data Rate: 60 × 780 bytes × 59.826 fps = 2,799,857 bytes/sec = **22.399 Mbps**

### Audio Stream Calculations

**PAL Audio (47,983 Hz)**
- Samples per Packet: 192 stereo samples
- Packet Interval: 192 samples ÷ 47,983 Hz = 4.000 ms/packet
- Packet Rate: **250.0 packets/sec** (exact)
- Data Rate: 250.0 × 770 bytes = 192,500 bytes/sec = **1.540 Mbps**

**NTSC Audio (47,940 Hz)**
- Samples per Packet: 192 stereo samples
- Packet Interval: 192 samples ÷ 47,940 Hz = 4.004 ms/packet
- Packet Rate: **249.7 packets/sec**
- Data Rate: 249.7 × 770 bytes = 192,269 bytes/sec = **1.538 Mbps**

### Network Buffer Sizing

**Video Buffer Requirements (integer packets per delay period)**
- PAL: 68 packets per 19.95 ms frame
- NTSC: 60 packets per 16.71 ms frame
- **Maximum Rate**: 3,590 packets/sec (NTSC)

**Audio Buffer Requirements (integer packets per delay period)**
- PAL: 1 packet per 4.000 ms
- NTSC: 1 packet per 4.004 ms
- **Maximum Rate**: 250 packets/sec (PAL)

**Data Volume Ratio**: Video:Audio ≈ **14.56:1** (22.399:1.538 Mbps)

## Control Commands

### Command Format
- Start Stream: FF2n (n = stream ID)
- Stop Stream: FF3n (n = stream ID)
- Parameters:
  1. Duration (0 = infinite, unit = 5ms ticks)
  2. Destination (optional string)

### Example Commands
```
Enable stream 0 (1 second):    20 FF 02 00 00 C8
Enable stream 0 (infinite):    20 FF 0F 00 00 00 [IP as ASCII]
Disable stream 0:             30 FF 00 00
```
