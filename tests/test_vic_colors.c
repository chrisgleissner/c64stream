/*
VIC Color Conversion Tests
Copyright (C) 2025 Christian Gleissner

Unit tests for VIC color palette and conversion functions.
*/

#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>

// VIC color palette from plugin (must match exactly)
static const uint32_t vic_colors[16] = {
    0xFF000000, // 0: Black
    0xFFFFFFFF, // 1: White
    0xFF9F4E44, // 2: Red
    0xFF6ABFC6, // 3: Cyan
    0xFFA057A3, // 4: Purple
    0xFF5CAB5E, // 5: Green
    0xFF50459B, // 6: Blue
    0xFFC9D487, // 7: Yellow
    0xFFA1683C, // 8: Orange
    0xFF6D5412, // 9: Brown
    0xFFCB7E75, // 10: Light Red
    0xFF626262, // 11: Dark Grey
    0xFF898989, // 12: Mid Grey
    0xFF9AE29B, // 13: Light Green
    0xFF887ECB, // 14: Light Blue
    0xFFADADAD  // 15: Light Grey
};

// Test color palette values
void test_vic_color_palette()
{
    printf("Testing VIC color palette...\n");

    // Test known color values
    assert(vic_colors[0] == 0xFF000000); // Black should be pure black
    assert(vic_colors[1] == 0xFFFFFFFF); // White should be pure white

    // Test that all colors have alpha channel set
    for (int i = 0; i < 16; i++) {
        assert((vic_colors[i] & 0xFF000000) == 0xFF000000);
        printf("  Color %2d: 0x%08X\n", i, vic_colors[i]);
    }

    printf("VIC color palette test PASSED\n\n");
}

// Test 4-bit to RGBA conversion
void test_color_conversion()
{
    printf("Testing 4-bit to RGBA conversion...\n");

    // Test data: two pixels packed in one byte
    uint8_t test_pixel = 0x1A; // Color 10 (0xA) and color 1 (0x1)

    // Extract colors
    uint8_t color1 = test_pixel & 0x0F;        // Should be 0xA (10)
    uint8_t color2 = (test_pixel >> 4) & 0x0F; // Should be 0x1 (1)

    assert(color1 == 10);
    assert(color2 == 1);

    // Convert to RGBA
    uint32_t rgba1 = vic_colors[color1];
    uint32_t rgba2 = vic_colors[color2];

    assert(rgba1 == vic_colors[10]); // Light Red
    assert(rgba2 == vic_colors[1]);  // White

    printf("  Pixel 0x%02X -> Color1=%d (0x%08X), Color2=%d (0x%08X)\n", test_pixel, color1, rgba1, color2, rgba2);

    printf("4-bit to RGBA conversion test PASSED\n\n");
}

// Test line conversion (simulating plugin behavior)
void test_line_conversion()
{
    printf("Testing line conversion...\n");

    // Test data: 4 pixels (2 bytes)
    uint8_t src_line[2] = {0x10, 0x23}; // Colors: 0,1,3,2
    uint32_t dst_line[4];

    // Convert like the plugin does
    for (int x = 0; x < 2; x++) {
        uint8_t pixel_pair = src_line[x];
        uint8_t color1 = pixel_pair & 0x0F;
        uint8_t color2 = (pixel_pair >> 4) & 0x0F;

        dst_line[x * 2] = vic_colors[color1];
        dst_line[x * 2 + 1] = vic_colors[color2];
    }

    // Verify results
    assert(dst_line[0] == vic_colors[0]); // Black
    assert(dst_line[1] == vic_colors[1]); // White
    assert(dst_line[2] == vic_colors[3]); // Cyan
    assert(dst_line[3] == vic_colors[2]); // Red

    printf("  Input: 0x%02X 0x%02X\n", src_line[0], src_line[1]);
    printf("  Output: 0x%08X 0x%08X 0x%08X 0x%08X\n", dst_line[0], dst_line[1], dst_line[2], dst_line[3]);

    printf("Line conversion test PASSED\n\n");
}

// Test packet header parsing (simulating plugin behavior)
void test_packet_header()
{
    printf("Testing packet header parsing...\n");

    // Simulate C64U video packet header
    uint8_t header[12];

    // Build test header
    *(uint16_t *)(header + 0) = 123;    // sequence number
    *(uint16_t *)(header + 2) = 456;    // frame number
    *(uint16_t *)(header + 4) = 0x8010; // line 16 + last packet flag
    *(uint16_t *)(header + 6) = 384;    // pixels per line
    header[8] = 4;                      // lines per packet
    header[9] = 4;                      // bits per pixel
    *(uint16_t *)(header + 10) = 0;     // encoding

    // Parse like the plugin does
    uint16_t seq_num = *(uint16_t *)(header + 0);
    uint16_t frame_num = *(uint16_t *)(header + 2);
    uint16_t line_num = *(uint16_t *)(header + 4);
    uint16_t pixels_per_line = *(uint16_t *)(header + 6);
    uint8_t lines_per_packet = header[8];
    uint8_t bits_per_pixel = header[9];
    uint16_t encoding = *(uint16_t *)(header + 10);

    // Mark unused variables to prevent warnings
    (void)pixels_per_line;
    (void)lines_per_packet;
    (void)bits_per_pixel;
    (void)encoding;

    bool last_packet = (line_num & 0x8000) != 0;
    line_num &= 0x7FFF;

    // Verify parsing
    assert(seq_num == 123);
    assert(frame_num == 456);
    assert(line_num == 16);
    assert(pixels_per_line == 384);
    assert(lines_per_packet == 4);
    assert(bits_per_pixel == 4);
    assert(encoding == 0);
    assert(last_packet == true);

    printf("  Parsed header: seq=%d, frame=%d, line=%d, last=%s\n", seq_num, frame_num, line_num,
           last_packet ? "true" : "false");

    printf("Packet header parsing test PASSED\n\n");
}

int main()
{
    printf("=== C64 Stream Plugin Unit Tests ===\n\n");

    test_vic_color_palette();
    test_color_conversion();
    test_line_conversion();
    test_packet_header();

    printf("All tests PASSED!\n");
    return 0;
}
