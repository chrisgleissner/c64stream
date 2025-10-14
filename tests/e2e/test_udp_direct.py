#!/usr/bin/env python3
"""
Quick UDP test to verify packets are being sent and received
"""
import socket
import time
import subprocess
import threading
from pathlib import Path

def udp_receiver(port, duration=3):
    """Listen for UDP packets on specified port"""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(1.0)

    try:
        sock.bind(('127.0.0.1', port))
        print(f"✅ UDP receiver listening on port {port}")

        packets_received = 0
        start_time = time.time()

        while time.time() - start_time < duration:
            try:
                data, addr = sock.recvfrom(1024)
                packets_received += 1
                if packets_received <= 5:  # Show first few packets
                    print(f"📦 Received packet #{packets_received}: {len(data)} bytes from {addr}")
            except socket.timeout:
                pass

        print(f"✅ Total packets received on port {port}: {packets_received}")
        return packets_received

    except Exception as e:
        print(f"❌ UDP receiver error on port {port}: {e}")
        return 0
    finally:
        sock.close()

def main():
    print("🔍 C64 UDP Packet Reception Test")
    print("=" * 50)

    # Start receivers for both ports
    video_thread = threading.Thread(target=lambda: udp_receiver(11000, 5))
    audio_thread = threading.Thread(target=lambda: udp_receiver(11001, 5))

    video_thread.start()
    audio_thread.start()

    # Give receivers time to start
    time.sleep(0.5)

    # Send some test packets with udp_replay
    packet_dir = Path("test_packets/video/PAL")
    if packet_dir.exists():
        print("🚀 Sending test packets...")
        cmd = [
            "../../build_x86_64/tests/e2e/udp_replay",
            str(packet_dir),
            "127.0.0.1",
            "11000",
            "780"
        ]

        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
            print(f"📡 UDP replay result: {result.returncode}")
            if result.stdout:
                print(f"📤 Output: {result.stdout[:200]}...")
        except Exception as e:
            print(f"❌ Failed to run UDP replay: {e}")
    else:
        print("❌ No test packets found - generate them first")

    # Wait for receivers to finish
    video_thread.join()
    audio_thread.join()

if __name__ == "__main__":
    main()
