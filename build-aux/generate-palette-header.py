#!/usr/bin/env python3
"""
Generate c64-default-palette.h from default.vpl
Parses the VPL file and creates a C header with the default color array.
"""

import sys

# C64 palette size (VIC-II has 16 colors)
C64_PALETTE_SIZE = 16


def parse_vpl_file(vpl_path):
    """Parse VPL file and extract RGB colors."""
    colors = []

    with open(vpl_path, 'r') as f:
        for line in f:
            # Remove comments
            line = line.split('#')[0].strip()
            if not line:
                continue

            # Parse RGB values (hex format: RR GG BB)
            parts = line.split()
            if len(parts) >= 3:
                try:
                    r = int(parts[0], 16)
                    g = int(parts[1], 16)
                    b = int(parts[2], 16)

                    # Clamp values
                    r = min(255, max(0, r))
                    g = min(255, max(0, g))
                    b = min(255, max(0, b))

                    # Convert RGB to BGRA format: 0xAABBGGRR (little-endian)
                    # Alpha=0xFF, Blue in bits 16-23, Green in bits 8-15, Red in bits 0-7
                    bgra = 0xFF000000 | (b << 16) | (g << 8) | r
                    colors.append(bgra)
                except ValueError:
                    continue

    return colors


def generate_header(colors, output_path):
    """Generate C header file with color array."""

    # Standard color names for C64 palette
    color_names = [
        "Black", "White", "Red", "Cyan", "Purple", "Green",
        "Blue", "Yellow", "Orange", "Brown", "Pink", "Dark Grey",
        "Medium Grey", "Light Green", "Light Blue", "Light Grey"
    ]

    header = """/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

/* THIS FILE IS AUTO-GENERATED - DO NOT EDIT MANUALLY
 * Generated from data/palettes/default.vpl at build time
 * To modify, edit default.vpl and rebuild
 */

#ifndef C64_DEFAULT_PALETTE_H
#define C64_DEFAULT_PALETTE_H

#include <stdint.h>

// Default VIC-II color palette (16 colors) in BGRA format for OBS Studio
// Colors from C64 Ultimate default.vpl (RGB values converted to BGRA with full alpha)
static const uint32_t c64_default_palette[16] = {
"""

    for i, color in enumerate(colors):
        if i < len(color_names):
            name = color_names[i]
        else:
            name = f"Color {i}"
        header += f"    0x{color:08X}, // {i}: {name}\n"

    header += """};

#endif // C64_DEFAULT_PALETTE_H
"""

    with open(output_path, 'w') as f:
        f.write(header)


def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <input.vpl> <output.h>")
        sys.exit(1)

    vpl_path = sys.argv[1]
    output_path = sys.argv[2]

    try:
        colors = parse_vpl_file(vpl_path)

        if len(colors) != C64_PALETTE_SIZE:
            print(f"Error: Expected {C64_PALETTE_SIZE} colors, found {len(colors)}")
            sys.exit(1)

        generate_header(colors, output_path)
        print(f"Generated {output_path} with {len(colors)} colors")

    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
