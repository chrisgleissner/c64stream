#!/usr/bin/env python3
"""
Simple UDP test to verify packet transmission works correctly.
This will send a real C64 packet to the plugin and verify it's received.
"""

import socket
import time
import sys
import os

def test_udp_transmission():
    """Test sending a real C64 video packet to localhost:11000"""

    # Find a real packet file
    packet_file = "test_packets/video/PAL/frame_0000_pkt_000.bin"
    if not os.path.exists(packet_file):
        print(f"❌ Packet file not found: {packet_file}")
        return False

    # Read the packet data
    with open(packet_file, 'rb') as f:
        packet_data = f.read()

    print(f"📦 Loaded packet: {len(packet_data)} bytes from {packet_file}")

    # Create UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    try:
        # Send to localhost:11000 (where plugin should be listening)
        dest = ('127.0.0.1', 11000)
        bytes_sent = sock.sendto(packet_data, dest)
        print(f"📡 Sent {bytes_sent} bytes to {dest}")

        if bytes_sent == len(packet_data):
            print("✅ UDP transmission successful")
            return True
        else:
            print(f"❌ Partial transmission: sent {bytes_sent}/{len(packet_data)} bytes")
            return False

    except Exception as e:
        print(f"❌ UDP transmission failed: {e}")
        return False
    finally:
        sock.close()

if __name__ == "__main__":
    print("🔍 Simple UDP Packet Transmission Test")
    print("=" * 50)
    success = test_udp_transmission()
    sys.exit(0 if success else 1)
