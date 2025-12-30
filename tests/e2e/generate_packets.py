#!/usr/bin/env python3
"""
C64 Stream - Packet Generator for E2E Testing
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Generates test packets following the C64 Ultimate stream specification.
Creates deterministic PAL and NTSC video/audio packets with verification pops
(A/V sync generator):
    - Video pop: random noise burst inside A/V pop indicator (bottom-right corner)
    - Audio pop: pleasant, band-limited noise burst ("rushing water"-like), instant on/off,
        alternating speakers per pop (L, R, L, R, ...), starting with LEFT
    - Pop cadence: first pop at ~1000ms after start, then every 1000ms
    - Pop duration: 2 video frames (improves robustness against 1-frame capture/encode drops)

Test Pattern Layout:
    +------------------------------------------+
    |  [TEXT BOX]               [COLOR PALETTE] |
    |  top-left                      top-right  |
    |                                           |
    |       [CENTRAL DIAGONAL PATTERN]          |
    |       (full frame, behind corners)        |
    |                                           |
    |  [FRAME PROGRESS]          [A/V POP]      |
    |  bottom-left              bottom-right    |
    +------------------------------------------+

All corner elements: 88x56 outer (1px white + 7px black frame), 72x40 inner content
C64 aspect ratio: 88/56 ≈ 1.57, similar to 320/200 = 1.6

Frame progress bar: 8 slots × 7px wide + 7 gaps × 1px = 63px, centered in 72px inner.
Temporal delta detection: afterglow-resistant - detect which slot increased brightness.

Performance: Uses multiprocessing to parallelize packet generation across all CPU cores.
"""

import os
import struct
import numpy as np
import argparse
from pathlib import Path
from typing import Optional
import math
from concurrent.futures import ProcessPoolExecutor, as_completed
import multiprocessing


C64_FONT = {
    ' ': [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00],
    '@': [0x3C, 0x66, 0x6E, 0x6E, 0x60, 0x62, 0x3C, 0x00],
    'A': [0x18, 0x3C, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00],
    'B': [0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0x00],
    'C': [0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00],
    'D': [0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00],
    'E': [0x7E, 0x60, 0x60, 0x78, 0x60, 0x60, 0x7E, 0x00],
    'F': [0x7E, 0x60, 0x60, 0x78, 0x60, 0x60, 0x60, 0x00],
    'G': [0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x3C, 0x00],
    'H': [0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00],
    'I': [0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00],
    'J': [0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x6C, 0x38, 0x00],
    'K': [0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0x00],
    'L': [0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00],
    'M': [0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0x00],
    'N': [0x66, 0x76, 0x7E, 0x7E, 0x6E, 0x66, 0x66, 0x00],
    'O': [0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00],
    'P': [0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x00],
    'Q': [0x3C, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x0E, 0x00],
    'R': [0x7C, 0x66, 0x66, 0x7C, 0x78, 0x6C, 0x66, 0x00],
    'S': [0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0x00],
    'T': [0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00],
    'U': [0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00],
    'V': [0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00],
    'W': [0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00],
    'X': [0x66, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x66, 0x00],
    'Y': [0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x00],
    'Z': [0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E, 0x00],
    '0': [0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0x00],
    '1': [0x18, 0x18, 0x38, 0x18, 0x18, 0x18, 0x7E, 0x00],
    '2': [0x3C, 0x66, 0x06, 0x0C, 0x30, 0x60, 0x7E, 0x00],
    '3': [0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0x00],
    '4': [0x06, 0x0E, 0x1E, 0x66, 0x7F, 0x06, 0x06, 0x00],
    '5': [0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0x00],
    '6': [0x3C, 0x66, 0x60, 0x7C, 0x66, 0x66, 0x3C, 0x00],
    '7': [0x7E, 0x66, 0x0C, 0x18, 0x18, 0x18, 0x18, 0x00],
    '8': [0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0x00],
    '9': [0x3C, 0x66, 0x66, 0x3E, 0x06, 0x66, 0x3C, 0x00],
    '_': [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x00],
    '-': [0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00],
    '.': [0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00],
    ':': [0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00],
    '/': [0x02, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00],
}


# Video format specifications (from doc/c64-stream-spec.md)
VIDEO_FORMATS = {
    'PAL': {
        'width': 384,
        'height': 272,
        'packets_per_frame': 68,
        'frame_rate': 50.125,
        'audio_sample_rate': 47983,  # PAL: 47983 Hz per C64 Ultimate spec
    },
    'NTSC': {
        'width': 384,
        'height': 240,
        'packets_per_frame': 60,
        'frame_rate': 59.826,
        'audio_sample_rate': 47940,  # NTSC: 47940 Hz per C64 Ultimate spec
    }
}

# Protocol constants (from doc/c64-stream-spec.md)
VIDEO_PACKET_SIZE = 780  # 12 byte header + 768 byte payload
AUDIO_PACKET_SIZE = 770  # 2 byte header + 768 byte payload
VIDEO_HEADER_SIZE = 12
AUDIO_HEADER_SIZE = 2
LINES_PER_PACKET = 4
BITS_PER_PIXEL = 4
AUDIO_SAMPLES_PER_PACKET = 192  # Stereo samples

# Corner element dimensions (all elements share these dimensions)
# Designed to match C64 inner screen aspect ratio: 320x200 = 1.6
# 88/56 ≈ 1.57, inner 72x40 is divisible by 8 for font alignment (9x5 chars)
CORNER_OUTER_WIDTH = 88   # Outer width including frame
CORNER_OUTER_HEIGHT = 56  # Outer height including frame
CORNER_INNER_WIDTH = 72   # Inner content width (88 - 8*2 = 72)
CORNER_INNER_HEIGHT = 40  # Inner content height (56 - 8*2 = 40)
CORNER_FRAME_WHITE = 1    # 1px outer border (color set in renderer)
CORNER_FRAME_BLACK = 7    # Black inner border
CORNER_FRAME_TOTAL = 8    # Total frame width on each side
MARKER_SHIFT_Y = 2        # Vertical margin for marker (keeps equal black space above/below)
TICK_BOTTOM_SHIFT = 1     # Shift bottom ticks down by 1px
TICK_HEIGHT = 3           # Bottom tick height in pixels

# VIC-II palette indices (C64 16-color palette)
VIC_BLACK = 0
VIC_WHITE = 1
VIC_DARK_BLUE = 6
VIC_LIGHT_BLUE = 14

# Ordered progression from darkest → brightest using all 16 VIC colors.
# Sorted by approximate luminance to keep the diagonal gradient smooth.
VIC_DIAGONAL_RAMP = [
    0,              # black
    VIC_DARK_BLUE,  # blue (dark)
    9,              # brown
    2,              # red
    11,             # dark gray
    4,              # purple
    8,              # orange
    VIC_LIGHT_BLUE,  # light blue
    12,             # gray
    10,             # light red
    5,              # green
    15,             # light gray
    3,              # cyan
    13,             # light green
    7,              # yellow
    1,              # white
]

# Current scenario name (set externally before packet generation)
_scenario_name = "DEFAULT"


def set_scenario_name(name: str):
    """Set the scenario name for the text box."""
    global _scenario_name
    _scenario_name = name.upper() if name else "DEFAULT"


# A/V sync timing constants
POP_FRAME_INTERVAL = 48  # Pop every 48 frames (6 cycles of 8-slot progress bar)
POP_FRAME_DURATION = 1   # Pop lasts 1 frame only
POP_FRAME_OFFSET = 24    # First pop at frame 24 (earlier start for 30-frame tests)


def get_sync_timing_info(format_name):
    """
    Get unified sync timing information for both audio and video.
    Returns (frame_rate, frame_duration_ms, pop_interval_frames, pop_duration_frames, pop_offset_frames)

    A/V pops are synchronized with the frame progress bar:
    - Pop occurs every 48 frames (when progress bar slot 0 lights up for the 6th time)
    - This equals ~960ms at PAL (50fps) or ~800ms at NTSC (60fps)
    """
    if format_name == 'PAL':
        frame_rate = 50.125
    else:  # NTSC
        frame_rate = 59.826

    frame_duration_ms = 1000.0 / frame_rate

    return frame_rate, frame_duration_ms, POP_FRAME_INTERVAL, POP_FRAME_DURATION, POP_FRAME_OFFSET


def is_sync_marker_active(frame_num, format_name, total_frames=None, disable_pops=False):
    """
    Determine if the A/V sync marker (noise flash + audio beep) should be active.
    Returns True if this frame should show the pop.

    A/V pops are synchronized with the frame progress bar:
    - Pop when (frame_num % 48) is 0 or 1 (2-frame duration)
    - First pop at frame 48
    - This ensures pops always occur when progress bar slot 0 is lit

    If total_frames is provided, pops that would be cut off by the
    last ~1000ms boundary are skipped (ensures complete pops only).

    If disable_pops is True, always returns False (pops disabled).
    """
    if disable_pops:
        return False
    frame_rate, frame_duration_ms, pop_interval, pop_duration, pop_offset = get_sync_timing_info(format_name)

    # Before first pop offset
    if frame_num < pop_offset:
        return False

    # Calculate position within pop cycle
    frames_since_offset = frame_num - pop_offset
    position_in_cycle = frames_since_offset % pop_interval

    # Pop is active for first pop_duration frames of each cycle
    is_in_pop_window = position_in_cycle < pop_duration

    if not is_in_pop_window:
        return False

    # If total_frames is provided, skip pops near the end
    if total_frames is not None:
        # Calculate cutoff: skip pops in the last ~1000ms worth of frames
        # BUT: for short tests (<2s), allow pops everywhere
        test_duration_ms = total_frames * frame_duration_ms
        if test_duration_ms >= 2000.0:  # Only apply cutoff for tests >= 2 seconds
            cutoff_frames = int(1000.0 / frame_duration_ms)
            if frame_num > (total_frames - cutoff_frames):
                return False

    return True


def _get_font_pixel(char: str, row: int, col: int) -> bool:
    """Get whether a pixel is set in the C64 font for a character."""
    if char not in C64_FONT:
        char = ' '
    if row < 0 or row >= 8 or col < 0 or col >= 8:
        return False
    byte = C64_FONT[char][row]
    # MSB is leftmost pixel
    return bool(byte & (0x80 >> col))


def _render_text_to_bitmap(text_lines: list[str], width: int, height: int,
                          color_map: dict[tuple[int, int], int] = None) -> list[list[int]]:
    """Render text lines to a bitmap using C64 font.

    Returns a 2D array of VIC colors.
    Text is rendered with no horizontal padding to maximize character count.
    72px width / 8px per char = 9 chars per line.

    Args:
        text_lines: List of text strings to render
        width: Bitmap width in pixels
        height: Bitmap height in pixels
        color_map: Optional dict mapping (line_idx, char_idx) to VIC color index.
                   If not provided, defaults to white (1) for text, black (0) for background.
    """
    bitmap = [[0] * width for _ in range(height)]

    y_offset = 0  # No top padding - start from first row
    for line_idx, text in enumerate(text_lines):
        y_base = y_offset + line_idx * 8  # 8px char, no extra spacing
        if y_base + 8 > height:
            break

        x_offset = 0  # No left padding - use full width
        for char_idx, char in enumerate(text.upper()):
            x_base = x_offset + char_idx * 8
            if x_base + 8 > width:
                break

            # Determine color for this character
            color = 1  # Default: white
            if color_map and (line_idx, char_idx) in color_map:
                color = color_map[(line_idx, char_idx)]

            for row in range(8):
                for col in range(8):
                    if _get_font_pixel(char, row, col):
                        bitmap[y_base + row][x_base + col] = color

    return bitmap


def _is_in_corner_frame(x: int, y: int, corner_x: int, corner_y: int) -> tuple[bool, bool]:
    """Check if pixel is in corner element frame (white outer or black inner).

    Returns (is_in_frame, is_white_border).
    """
    # Local coordinates within the corner element
    local_x = x - corner_x
    local_y = y - corner_y

    if local_x < 0 or local_x >= CORNER_OUTER_WIDTH or local_y < 0 or local_y >= CORNER_OUTER_HEIGHT:
        return False, False

    # Check if in white outer border (1px)
    if local_x == 0 or local_x == CORNER_OUTER_WIDTH - 1 or local_y == 0 or local_y == CORNER_OUTER_HEIGHT - 1:
        return True, True

    # Check if in black inner border (7px after the white border)
    if (local_x < CORNER_FRAME_TOTAL or local_x >= CORNER_OUTER_WIDTH - CORNER_FRAME_TOTAL or
        local_y < CORNER_FRAME_TOTAL or local_y >= CORNER_OUTER_HEIGHT - CORNER_FRAME_TOTAL):
        return True, False

    return False, False


def _get_corner_chamfer_color(
    x: int, y: int, corner_x: int, corner_y: int, inward_corner: str, outer_color: int, inner_color: int
) -> Optional[int]:
    """Return a closed chamfer color for the 3x3 inward corner, or None if not in chamfer area.

    The chamfer keeps the border continuous by drawing a diagonal outer line and
    filling the remaining pixels with the inner border color.
    """
    local_x = x - corner_x
    local_y = y - corner_y
    if local_x < 0 or local_x >= CORNER_OUTER_WIDTH or local_y < 0 or local_y >= CORNER_OUTER_HEIGHT:
        return None

    tip_x, tip_y = 0, 0
    if inward_corner == "tl":
        tip_x, tip_y = 0, 0
    elif inward_corner == "tr":
        tip_x, tip_y = CORNER_OUTER_WIDTH - 1, 0
    elif inward_corner == "bl":
        tip_x, tip_y = 0, CORNER_OUTER_HEIGHT - 1
    elif inward_corner == "br":
        tip_x, tip_y = CORNER_OUTER_WIDTH - 1, CORNER_OUTER_HEIGHT - 1
    else:
        return None

    u = abs(local_x - tip_x)
    v = abs(local_y - tip_y)
    if u > 2 or v > 2:
        return None

    if (u + v) > 2:
        return None

    # Diagonal line (u+v==2) uses outer border color; inside uses inner border color.
    return outer_color if (u + v) == 2 else inner_color


def generate_video_packet(frame_num, packet_num, width, height, packets_per_frame, format_name, total_frames, pattern='diagonal', disable_pops=False):
    """
    Generate a single video packet with professional E2E test pattern.

    Layout (88x56 corner elements, C64 aspect ratio 1.6):
    - Top-left: Text box with scenario name (72x40 inner, 9x5 chars)
    - Top-right: 16-color VIC-II palette reference
    - Bottom-left: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
    - Bottom-right: A/V pop indicator (pops every 48 frames, synchronized with slot 0)
      * Split into left/right halves with 2px black divider
      * Left half lights for left channel audio, right half for right channel
    - Central field: Diagonal pattern cycling through all 16 C64 colors

    All corner elements: 1px white outer border + 7px black inner border
    """
    line_num = packet_num * LINES_PER_PACKET
    is_last_packet = (packet_num == packets_per_frame - 1)
    line_num_with_flag = line_num | (0x8000 if is_last_packet else 0)
    seq_num = (frame_num * packets_per_frame + packet_num) & 0xFFFF

    header = struct.pack('<HHHHBBH',
                         seq_num, frame_num, line_num_with_flag,
                         width, LINES_PER_PACKET, BITS_PER_PIXEL, 0)

    payload = bytearray(768)
    sync_active = is_sync_marker_active(frame_num, format_name, total_frames)

    # Image refinements:
    # - Outer border: light blue
    # - Central playfield background: pure black
    border_color = VIC_LIGHT_BLUE
    screen_color = VIC_BLACK
    if format_name == 'PAL':
        border_left, border_right, border_top, border_bottom = 32, 32, 35, 37
    else:
        border_left, border_right, border_top, border_bottom = 32, 32, 20, 20
    screen_x0 = border_left
    screen_x1 = width - border_right
    screen_y0 = border_top
    screen_y1 = height - border_bottom

    # Calculate pop_index for L/R channel indicator (same logic as audio)
    pop_index = -1
    if sync_active and frame_num >= POP_FRAME_OFFSET:
        frames_since_offset = frame_num - POP_FRAME_OFFSET
        pop_index = frames_since_offset // POP_FRAME_INTERVAL

    # Corner element positions (88x56 each)
    tl_x, tl_y = 0, 0  # Top-left text box
    tr_x, tr_y = width - CORNER_OUTER_WIDTH, 0  # Top-right palette
    bl_x, bl_y = 0, height - CORNER_OUTER_HEIGHT  # Bottom-left frame progress
    br_x, br_y = width - CORNER_OUTER_WIDTH, height - CORNER_OUTER_HEIGHT  # Bottom-right A/V pop

    marker_height = CORNER_INNER_HEIGHT - (MARKER_SHIFT_Y * 2)
    marker_y0 = CORNER_FRAME_TOTAL + MARKER_SHIFT_Y
    marker_y1 = marker_y0 + marker_height

    tick_bottom_y0 = CORNER_OUTER_HEIGHT - 4 + TICK_BOTTOM_SHIFT
    tick_bottom_y1 = min(CORNER_OUTER_HEIGHT, tick_bottom_y0 + TICK_HEIGHT)

    # Pre-render text box content (9 chars × 5 lines for 72×40 inner area)
    # Line 1: Plugin name with rainbow effect on "STREAM", Lines 2-5: Scenario name (light gray)
    text_lines = ["C64STREAM"]
    # Split scenario name by underscores, each part becomes a line (max 9 chars, truncated if needed)
    scenario_parts = []
    if _scenario_name:
        parts = _scenario_name.split("_")
        for part in parts[:4]:  # Max 4 parts (lines 2-5)
            scenario_parts.append(part[:9].upper())  # Truncate to 9 chars, uppercase
    # Bottom-align: add empty lines first, then scenario parts
    empty_lines_needed = 4 - len(scenario_parts)
    for _ in range(empty_lines_needed):
        text_lines.append("")
    text_lines.extend(scenario_parts)

    # Create color map: all text white (default), light gray scenario name
    color_map = {}

    # "C64STREAM" remains white (default color 1, no color_map entries needed)
    # Scenario name lines (1-4) in light gray (VIC color 15)
    for line_idx in range(1, 5):
        if line_idx < len(text_lines) and text_lines[line_idx]:
            for char_idx in range(len(text_lines[line_idx])):
                color_map[(line_idx, char_idx)] = 15  # Light gray

    text_bitmap = _render_text_to_bitmap(text_lines, CORNER_INNER_WIDTH, CORNER_INNER_HEIGHT, color_map)

    for line in range(LINES_PER_PACKET):
        for byte_idx in range(width // 2):
            pixel_y = line_num + line
            pixel_x = byte_idx * 2

            def get_pixel_color(px: int, py: int) -> int:
                """Determine color for a single pixel."""
                # ═══════════════════════════════════════════════════════════════
                # TOP-LEFT: Text Box (88x56 outer, 72x40 inner)
                # ═══════════════════════════════════════════════════════════════
                if tl_x <= px < tl_x + CORNER_OUTER_WIDTH and tl_y <= py < tl_y + CORNER_OUTER_HEIGHT:
                    in_frame, is_white = _is_in_corner_frame(px, py, tl_x, tl_y)
                    if in_frame:
                        chamfer_color = _get_corner_chamfer_color(
                            px, py, tl_x, tl_y, "br", VIC_LIGHT_BLUE, VIC_BLACK
                        )
                        if chamfer_color is not None:
                            return chamfer_color
                        return VIC_LIGHT_BLUE if is_white else VIC_BLACK
                    # Inner content area
                    inner_x = px - tl_x - CORNER_FRAME_TOTAL
                    inner_y = py - tl_y - CORNER_FRAME_TOTAL
                    if 0 <= inner_x < CORNER_INNER_WIDTH and 0 <= inner_y < CORNER_INNER_HEIGHT:
                        return text_bitmap[inner_y][inner_x]
                    return 0

                # ═══════════════════════════════════════════════════════════════
                # TOP-RIGHT: 16-Color Palette Reference (88x56 outer, 72x40 inner)
                # ═══════════════════════════════════════════════════════════════
                if tr_x <= px < tr_x + CORNER_OUTER_WIDTH and tr_y <= py < tr_y + CORNER_OUTER_HEIGHT:
                    in_frame, is_white = _is_in_corner_frame(px, py, tr_x, tr_y)
                    if in_frame:
                        chamfer_color = _get_corner_chamfer_color(
                            px, py, tr_x, tr_y, "bl", VIC_LIGHT_BLUE, VIC_BLACK
                        )
                        if chamfer_color is not None:
                            return chamfer_color
                        return VIC_LIGHT_BLUE if is_white else VIC_BLACK
                    # Inner content: 4×4 grid of all 16 VIC colors (0-15) with 2px black gaps
                    # 72×40 inner: 4 cols × 17px = 68px (+4px padding), 4 rows × 9px = 36px (+4px padding)
                    # Each swatch: 15×7 pixels with 2px black gaps (17×9 total per cell)
                    inner_x = px - tr_x - CORNER_FRAME_TOTAL
                    inner_y = py - tr_y - CORNER_FRAME_TOTAL
                    if 0 <= inner_x < CORNER_INNER_WIDTH and 0 <= inner_y < CORNER_INNER_HEIGHT:
                        # 2px padding on all sides
                        padded_x = inner_x - 2
                        padded_y = inner_y - 2
                        if padded_x < 0 or padded_y < 0:
                            return 0  # Padding
                        # 4×4 grid in 68×36 area: each cell is 17×9 pixels
                        swatch_w = 17  # 17*4 = 68
                        swatch_h = 9   # 9*4 = 36
                        col = padded_x // swatch_w
                        row = padded_y // swatch_h
                        if col >= 4 or row >= 4:
                            return 0
                        # Position within the swatch cell
                        x_in_cell = padded_x % swatch_w
                        y_in_cell = padded_y % swatch_h
                        # 2px black gap on right and bottom of each swatch
                        if x_in_cell >= 15 or y_in_cell >= 7:
                            return 0  # Gap
                        # VIC colors 0-15: row 0 = colors 0-3, row 1 = 4-7, etc.
                        color_idx = row * 4 + col
                        return color_idx
                    return 0

                # ═══════════════════════════════════════════════════════════════
                # BOTTOM-LEFT: Frame Progression Indicator (88x56 outer, 72x40 inner)
                # ═══════════════════════════════════════════════════════════════
                if bl_x <= px < bl_x + CORNER_OUTER_WIDTH and bl_y <= py < bl_y + CORNER_OUTER_HEIGHT:
                    local_x = px - bl_x
                    local_y = py - bl_y
                    in_frame, is_white = _is_in_corner_frame(px, py, bl_x, bl_y)
                    if in_frame:
                        chamfer_color = _get_corner_chamfer_color(
                            px, py, bl_x, bl_y, "tr", VIC_LIGHT_BLUE, VIC_BLACK
                        )
                        if chamfer_color is not None:
                            return chamfer_color
                        # Frame-progression tick marks (bottom only, shifted down):
                        # - 1px wide WHITE vertical ticks centered on each of 8 slots
                        # - Drawn in the black frame area between outer frame and white rectangles
                        if local_x in (15, 23, 31, 39, 47, 55, 63, 71):
                            if tick_bottom_y0 <= local_y < tick_bottom_y1:
                                return VIC_WHITE
                        if is_white:
                            return VIC_LIGHT_BLUE
                        # Keep the active marker centered within the inner area.
                        if marker_y0 <= local_y < marker_y1:
                            inner_x = local_x - CORNER_FRAME_TOTAL
                            if 0 <= inner_x < CORNER_INNER_WIDTH:
                                bar_area_x = inner_x - 4
                                if 0 <= bar_area_x < 63:
                                    slot_width = 7
                                    gap_width = 1
                                    slot_pitch = slot_width + gap_width  # 8px per slot position
                                    slot_index = bar_area_x // slot_pitch
                                    pos_in_slot = bar_area_x % slot_pitch
                                    if pos_in_slot < slot_width and slot_index == (frame_num % 8):
                                        return VIC_WHITE
                        return VIC_BLACK
                    # Inner content: black background with moving white bar
                    # Bar: 7px wide slot, 1px black gap between slots
                    # 8 slots × 7px + 7 gaps × 1px = 56 + 7 = 63px, centered in 72px (4px left padding)
                    inner_x = local_x - CORNER_FRAME_TOTAL
                    inner_y = local_y - CORNER_FRAME_TOTAL
                    if 0 <= inner_x < CORNER_INNER_WIDTH and 0 <= inner_y < CORNER_INNER_HEIGHT:
                        if marker_y0 <= local_y < marker_y1:
                            # Center the 63px bar area in 72px width (4px left padding, 5px right)
                            bar_area_x = inner_x - 4
                            if 0 <= bar_area_x < 63:
                                slot_width = 7
                                gap_width = 1
                                slot_pitch = slot_width + gap_width  # 8px per slot position
                                slot_index = bar_area_x // slot_pitch
                                pos_in_slot = bar_area_x % slot_pitch
                                # Gap between slots is black (1px)
                                if pos_in_slot >= slot_width:
                                    return 0  # Black gap
                                # Active slot is white, inactive slots are black (max contrast for heavy effects)
                                if slot_index == (frame_num % 8):
                                    return VIC_WHITE
                                return VIC_BLACK
                        return 0  # Black background (outside bar area)
                    return 0

                # ═══════════════════════════════════════════════════════════════
                # BOTTOM-RIGHT: A/V Pop Indicator (88x56 outer, 72x40 inner)
                # Split into L/R halves with 2px black divider in center
                # ═══════════════════════════════════════════════════════════════
                if br_x <= px < br_x + CORNER_OUTER_WIDTH and br_y <= py < br_y + CORNER_OUTER_HEIGHT:
                    local_x = px - br_x
                    local_y = py - br_y
                    in_frame, is_white = _is_in_corner_frame(px, py, br_x, br_y)
                    if in_frame:
                        chamfer_color = _get_corner_chamfer_color(
                            px, py, br_x, br_y, "tl", VIC_LIGHT_BLUE, VIC_BLACK
                        )
                        if chamfer_color is not None:
                            return chamfer_color
                        # A/V pop tick marks (bottom only, shifted down):
                        # - 1px wide WHITE vertical ticks centered on each half-rectangle (L/R)
                        # - Drawn in the black frame area between outer frame and white rectangles
                        if local_x in (25, 62):
                            if tick_bottom_y0 <= local_y < tick_bottom_y1:
                                return VIC_WHITE
                        if is_white:
                            return VIC_LIGHT_BLUE
                        # Keep the active marker centered within the inner area.
                        if marker_y0 <= local_y < marker_y1:
                            inner_x = local_x - CORNER_FRAME_TOTAL
                            if 0 <= inner_x < CORNER_INNER_WIDTH:
                                # Divider in center (positions 35 and 36 are black)
                                half_width = 35  # (72 - 2) / 2 = 35
                                is_left_half = inner_x < half_width
                                is_divider = half_width <= inner_x < half_width + 2
                                is_right_half = inner_x >= half_width + 2

                                if is_divider:
                                    return VIC_BLACK

                                if sync_active and pop_index >= 0:
                                    is_left_pop = (pop_index % 2) == 0
                                    should_light = (is_left_half and is_left_pop) or (
                                        is_right_half and not is_left_pop
                                    )
                                    return VIC_WHITE if should_light else VIC_BLACK
                        return VIC_BLACK
                    # Inner content: split into left/right halves with 2px black divider
                    # Layout: [left_half 35px] [divider 2px] [right_half 35px] = 72px
                    inner_x = local_x - CORNER_FRAME_TOTAL
                    inner_y = local_y - CORNER_FRAME_TOTAL
                    if 0 <= inner_x < CORNER_INNER_WIDTH and 0 <= inner_y < CORNER_INNER_HEIGHT:
                        # Divider in center (positions 35 and 36 are black)
                        half_width = 35  # (72 - 2) / 2 = 35
                        is_left_half = inner_x < half_width
                        is_divider = half_width <= inner_x < half_width + 2
                        is_right_half = inner_x >= half_width + 2

                        if is_divider:
                            return 0  # Black divider always

                        # A/V pop background is black for maximum contrast.
                        if sync_active and pop_index >= 0:
                            # Determine which half should light up based on audio channel
                            # Alternation: L, R, L, R... (pop_index 0=LEFT, 1=RIGHT, 2=LEFT...)
                            is_left_pop = (pop_index % 2) == 0
                            should_light = (is_left_half and is_left_pop) or (is_right_half and not is_left_pop)
                            return VIC_WHITE if should_light else VIC_BLACK
                        return VIC_BLACK
                    return 0

                # ═══════════════════════════════════════════════════════════════
                # BORDER AREA: solid border color outside the 320x200 screen
                # ═══════════════════════════════════════════════════════════════
                if px < screen_x0 or px >= screen_x1 or py < screen_y0 or py >= screen_y1:
                    return border_color

                # ═══════════════════════════════════════════════════════════════
                # CENTRAL FIELD: Diagonal Pattern (behind corner elements)
                # ═══════════════════════════════════════════════════════════════
                if pattern == 'solid':
                    return VIC_LIGHT_BLUE

                if pattern == 'dots':
                    if (px % 16 == 0) and (py % 16 == 0):
                        return 1  # White
                    return screen_color  # Screen background

                # Default: diagonal fields (C64 diagnostic-style), preserving orientation.
                #
                # - Diagonal stripes are selected via S = (x+y+frame) (same as before).
                # - Within each stripe, color fields progress deterministically along the diagonal.
                # - Insert a 2px black gap between neighboring color fields (no anti-aliasing).
                S = px + py + frame_num
                stripe_period = 38  # Keep existing spacing
                stripe_width = 2    # Keep existing thickness
                if (S % stripe_period) >= stripe_width:
                    return screen_color

                # Field index along the diagonal (stable, C64-ish blockiness).
                D = (px - py)
                field_pitch = 16  # Preserve existing block cadence along diagonals
                # 2px separation between neighboring fields along each diagonal.
                if (D % field_pitch) < 2:
                    return VIC_BLACK

                field_index = D // field_pitch
                # Phase offsets allowed: offset fields per stripe and animate smoothly per frame.
                stripe_index = S // stripe_period
                phase = (field_index + stripe_index) % (2 * (len(VIC_DIAGONAL_RAMP) - 1))
                ramp_idx = phase if phase < len(VIC_DIAGONAL_RAMP) else (2 * (len(VIC_DIAGONAL_RAMP) - 1) - phase)
                return VIC_DIAGONAL_RAMP[ramp_idx]

            c1 = get_pixel_color(pixel_x, pixel_y)
            c2 = get_pixel_color(pixel_x + 1, pixel_y)
            payload[line * (width // 2) + byte_idx] = (c2 << 4) | c1

    return header + bytes(payload)


def _one_pole_lowpass(x: np.ndarray, cutoff_hz: float, sample_rate: float) -> np.ndarray:
    """Simple one-pole low-pass filter."""
    if x.size == 0:
        return x
    rc = 1.0 / (2.0 * math.pi * cutoff_hz)
    dt = 1.0 / float(sample_rate)
    alpha = dt / (rc + dt)
    y = np.empty_like(x, dtype=np.float64)
    acc = float(x[0])
    y[0] = acc
    for n in range(1, x.size):
        acc = acc + alpha * (float(x[n]) - acc)
        y[n] = acc
    return y


def _one_pole_highpass(x: np.ndarray, cutoff_hz: float, sample_rate: float) -> np.ndarray:
    """Simple one-pole high-pass filter."""
    if x.size == 0:
        return x
    rc = 1.0 / (2.0 * math.pi * cutoff_hz)
    dt = 1.0 / float(sample_rate)
    alpha = rc / (rc + dt)
    y = np.empty_like(x, dtype=np.float64)
    prev_y = float(x[0])
    prev_x = float(x[0])
    y[0] = 0.0
    for n in range(1, x.size):
        yn = alpha * (prev_y + float(x[n]) - prev_x)
        y[n] = yn
        prev_y = yn
        prev_x = float(x[n])
    return y


def generate_audio_packet(audio_packet_num, sample_rate, total_frames, format_name, disable_pops=False):
    """
    Generate a single audio packet following C64 Ultimate spec.

    Packet Structure:
    1. Header: Sequence number (16-bit LE)
    2. Payload: 192 stereo samples (16-bit signed LE, interleaved L/R)

    Audio Pattern: Continuous stream with band-limited noise pop for 2 video frames
    every 48 frames (synchronized with frame progress bar slot 0), with instant on/off.
    """
    # Build header (2 bytes)
    header = struct.pack('<H', audio_packet_num)

    # Calculate timing - each audio packet represents exactly 192 samples at the given sample rate
    packet_duration_ms = (AUDIO_SAMPLES_PER_PACKET / sample_rate) * 1000
    time_in_test_ms = audio_packet_num * packet_duration_ms

    # Use unified sync timing to ensure perfect A/V alignment
    frame_rate, frame_duration_ms, pop_interval, pop_duration, pop_offset = get_sync_timing_info(format_name)

    # Convert audio time to frame number to align with video pop timing
    current_frame = time_in_test_ms / frame_duration_ms

    # Check if we're in a sync pop period using the same frame-based logic as video
    if current_frame < pop_offset:
        is_sync_pop = False
        pop_index = -1
    else:
        frames_since_offset = current_frame - pop_offset
        position_in_cycle = frames_since_offset % pop_interval
        is_sync_pop = position_in_cycle < pop_duration

        # Calculate pop index for L/R alternation
        pop_index = int(frames_since_offset // pop_interval)

        # Skip pops near the end (last ~1000ms worth of frames)
        cutoff_frames = int(1000.0 / frame_duration_ms)
        if current_frame > (total_frames - cutoff_frames):
            is_sync_pop = False

    # Generate time array for this packet's 192 samples
    t = np.linspace(time_in_test_ms / 1000.0,
                    (time_in_test_ms + packet_duration_ms) / 1000.0,
                    AUDIO_SAMPLES_PER_PACKET, endpoint=False)

    if is_sync_pop:
        # Generate band-limited noise burst (pleasant, "rushing water"-like)
        # Deterministic seed per packet for reproducibility
        rng = np.random.RandomState(0xC64A ^ audio_packet_num)
        white = rng.randn(AUDIO_SAMPLES_PER_PACKET).astype(np.float64)
        # Band-limit: high-pass ~200 Hz to remove rumble; low-pass ~3 kHz to soften harshness
        noise = _one_pole_highpass(white, cutoff_hz=200.0, sample_rate=sample_rate)
        noise = _one_pole_lowpass(noise, cutoff_hz=3000.0, sample_rate=sample_rate)
        # Normalize and scale to comfortable level
        peak = np.max(np.abs(noise)) if np.max(np.abs(noise)) > 0 else 1.0
        noise = 0.55 * (noise / peak)
        audio_signal = (noise * 32767.0).astype(np.int16)
    else:
        # Silence between pops
        audio_signal = np.zeros(AUDIO_SAMPLES_PER_PACKET, dtype=np.int16)

    # Interleave left and right channels
    # During pops, alternate speakers per pop: even pop_index => LEFT, odd => RIGHT
    payload = np.empty(AUDIO_SAMPLES_PER_PACKET * 2, dtype=np.int16)
    if is_sync_pop and pop_index >= 0:
        if (pop_index % 2) == 0:
            # LEFT pop (pop_index 0, 2, 4...)
            payload[0::2] = audio_signal   # Left
            payload[1::2] = 0              # Right
        else:
            # RIGHT pop (pop_index 1, 3, 5...)
            payload[0::2] = 0              # Left
            payload[1::2] = audio_signal   # Right
    else:
        # Silence or non-pop portion of packet
        payload[0::2] = audio_signal
        payload[1::2] = audio_signal

    return header + payload.tobytes()


# ═══════════════════════════════════════════════════════════════════════════════
# PARALLEL PACKET GENERATION HELPERS
# ═══════════════════════════════════════════════════════════════════════════════
# These functions are designed to be called from worker processes.


def _generate_video_packet_task(args):
    """Worker task for generating a single video packet."""
    frame_num, packet_num, width, height, ppf, format_name, total_frames, pattern, output_path = args
    packet_data = generate_video_packet(
        frame_num, packet_num, width, height, ppf, format_name, total_frames, pattern
    )
    packet_file = output_path / f"video_{frame_num:04d}_{packet_num:04d}.bin"
    packet_file.write_bytes(packet_data)
    return 1


def _generate_audio_packet_task(args):
    """Worker task for generating a single audio packet."""
    audio_packet_num, sample_rate, total_frames, format_name, output_path = args
    packet_data = generate_audio_packet(audio_packet_num, sample_rate, total_frames, format_name)
    packet_file = output_path / f"audio_{audio_packet_num:04d}.bin"
    packet_file.write_bytes(packet_data)
    return 1


def _generate_frame_batch_task(args):
    """Worker task for generating all packets for a batch of frames (more efficient than per-packet)."""
    frame_start, frame_end, width, height, ppf, format_name, total_frames, pattern, output_path, disable_pops = args
    count = 0
    for frame_num in range(frame_start, frame_end):
        for packet_num in range(ppf):
            packet_data = generate_video_packet(
                frame_num, packet_num, width, height, ppf, format_name, total_frames, pattern, disable_pops
            )
            packet_file = output_path / f"video_{frame_num:04d}_{packet_num:04d}.bin"
            packet_file.write_bytes(packet_data)
            count += 1
    return count


def _generate_audio_batch_task(args):
    """Worker task for generating a batch of audio packets."""
    start_idx, end_idx, sample_rate, total_frames, format_name, output_path, disable_pops = args
    count = 0
    for audio_packet_num in range(start_idx, end_idx):
        packet_data = generate_audio_packet(audio_packet_num, sample_rate, total_frames, format_name, disable_pops)
        packet_file = output_path / f"audio_{audio_packet_num:04d}.bin"
        packet_file.write_bytes(packet_data)
        count += 1
    return count


def generate_packets(output_dir, num_frames=30, formats=None, pattern='diagonal', parallel=True, disable_pops=False):
    """
    Generate test packets for specified formats with A/V sync pops.

    Args:
        output_dir: Directory to write packet files
        num_frames: Number of video frames to generate
        formats: List of formats ('PAL', 'NTSC') or None for both
        pattern: Video pattern - 'diagonal' (moving lines) or 'solid' (uniform color)
        parallel: Use multiprocessing for faster generation (default: True)
    """
    output_path = Path(output_dir)
    output_path.mkdir(parents=True, exist_ok=True)

    target_formats = formats if formats else ['PAL', 'NTSC']

    # Determine number of worker processes
    num_workers = multiprocessing.cpu_count() if parallel else 1

    for format_name in target_formats:
        fmt = VIDEO_FORMATS[format_name]
        video_dir = output_path / 'video' / format_name
        audio_dir = output_path / 'audio' / format_name
        video_dir.mkdir(parents=True, exist_ok=True)
        audio_dir.mkdir(parents=True, exist_ok=True)

        width = fmt['width']
        height = fmt['height']
        ppf = fmt['packets_per_frame']

        # Compute total test duration upfront (needed to calculate audio packets and suppress last-second pops)
        frame_duration_ms = 1000.0 / fmt['frame_rate']
        total_test_duration_ms = num_frames * frame_duration_ms

        # Calculate audio packet count
        audio_packet_duration_ms = (AUDIO_SAMPLES_PER_PACKET / fmt['audio_sample_rate']) * 1000.0
        total_audio_packets = int(total_test_duration_ms / audio_packet_duration_ms)

        # Ensure last audio packet doesn't arrive after last video packet to avoid edge-case packet loss
        # Calculate when last packets arrive (packets are 0-indexed)
        video_packet_interval_ms = frame_duration_ms / ppf
        last_video_arrival_ms = (num_frames * ppf - 1) * video_packet_interval_ms
        last_audio_arrival_ms = (total_audio_packets - 1) * audio_packet_duration_ms

        if last_audio_arrival_ms > last_video_arrival_ms:
            # Audio extends past video - reduce audio packet count to stay within video duration
            total_audio_packets = int(last_video_arrival_ms / audio_packet_duration_ms) + 1
            print(f"  ℹ️  Adjusted audio packets to {total_audio_packets} to complete before last video packet")

        if parallel and num_workers > 1 and num_frames >= num_workers:
            # ═══════════════════════════════════════════════════════════════════
            # PARALLEL GENERATION: Batch frames across workers for efficiency
            # ═══════════════════════════════════════════════════════════════════
            # Divide frames into batches for each worker
            frames_per_worker = max(1, num_frames // num_workers)
            video_tasks = []
            for i in range(num_workers):
                start = i * frames_per_worker
                end = min(start + frames_per_worker, num_frames) if i < num_workers - 1 else num_frames
                if start < end:
                    video_tasks.append((start, end, width, height, ppf, format_name,
                                        num_frames, pattern, video_dir, disable_pops))

            # Divide audio packets into batches
            audio_per_worker = max(1, total_audio_packets // num_workers)
            audio_tasks = []
            for i in range(num_workers):
                start = i * audio_per_worker
                end = min(start + audio_per_worker, total_audio_packets) if i < num_workers - 1 else total_audio_packets
                if start < end:
                    audio_tasks.append((start, end, fmt['audio_sample_rate'],
                                        num_frames, format_name, audio_dir, disable_pops))

            # Execute in parallel using ProcessPoolExecutor
            video_count = 0
            audio_count = 0
            with ProcessPoolExecutor(max_workers=num_workers) as executor:
                # Submit all video tasks
                video_futures = [executor.submit(_generate_frame_batch_task, task) for task in video_tasks]
                # Submit all audio tasks
                audio_futures = [executor.submit(_generate_audio_batch_task, task) for task in audio_tasks]

                # Collect results
                for future in as_completed(video_futures):
                    video_count += future.result()
                for future in as_completed(audio_futures):
                    audio_count += future.result()

            print(f"  ✅ {format_name}: Generated {video_count} video packets and {audio_count} audio packets (parallel, {num_workers} workers)")

        else:
            # ═══════════════════════════════════════════════════════════════════
            # SEQUENTIAL GENERATION: Original single-threaded approach
            # ═══════════════════════════════════════════════════════════════════
            for frame_num in range(num_frames):
                for packet_num in range(ppf):
                    packet_data = generate_video_packet(
                        frame_num, packet_num, width, height, ppf, format_name, num_frames, pattern, disable_pops
                    )
                    packet_file = video_dir / f"video_{frame_num:04d}_{packet_num:04d}.bin"
                    packet_file.write_bytes(packet_data)

            for audio_packet_num in range(total_audio_packets):
                packet_data = generate_audio_packet(audio_packet_num, fmt['audio_sample_rate'], num_frames, format_name, disable_pops)
                packet_file = audio_dir / f"audio_{audio_packet_num:04d}.bin"
                packet_file.write_bytes(packet_data)

            print(f"  ✅ {format_name}: Generated {num_frames*ppf} video packets and {total_audio_packets} audio packets")

    # Report disk usage
    total_size = 0
    for p in output_path.rglob('*.bin'):
        total_size += p.stat().st_size
    print(f"\n✅ Packet generation complete: {output_dir}")
    print(f"💾 Total disk usage: {total_size:,} bytes ({total_size / 1024 / 1024:.1f} MB)")


def main():
    parser = argparse.ArgumentParser(
        description='Generate C64 Ultimate test packets for e2e testing',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Generate 30 frames for both PAL and NTSC
  %(prog)s

  # Generate 60 frames for PAL only
  %(prog)s --frames 60 --format PAL

  # Generate to custom directory
  %(prog)s --output /tmp/test_packets

  # Disable parallel generation (for debugging)
  %(prog)s --no-parallel
        """
    )
    parser.add_argument('--output', '-o', default='test_packets',
                        help='Output directory (default: test_packets)')
    parser.add_argument('--frames', '-f', type=int, default=30,
                        help='Number of frames to generate (default: 30)')
    parser.add_argument('--format', choices=['PAL', 'NTSC'], action='append',
                        dest='formats',
                        help='Format(s) to generate (can specify multiple times, default: both)')
    parser.add_argument('--pattern', '-p', choices=['diagonal', 'solid', 'dots'], default='diagonal',
                        help='Video pattern: diagonal (moving lines), solid (uniform color for scanline tests), or dots (single-pixel dots for sharp pixel tests)')
    parser.add_argument('--scenario', '-s', default='DEFAULT',
                        help='Scenario name to display in top-left text box (underscores become newlines)')
    parser.add_argument('--disable-pops', action='store_true',
                        help='Disable A/V sync pops (for testing frame progression without pop interference)')
    parser.add_argument('--no-parallel', action='store_true',
                        help='Disable parallel generation (use single thread)')

    args = parser.parse_args()

    set_scenario_name(args.scenario)
    generate_packets(args.output, args.frames, args.formats, args.pattern, parallel=not args.no_parallel, disable_pops=args.disable_pops)


if __name__ == '__main__':
    main()
