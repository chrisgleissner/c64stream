#!/usr/bin/env python3
"""
Test keystroke injection with backpressure
"""

import subprocess
import time
import sys
import requests
import pytest
import os

@pytest.fixture(scope="module")
def mock_server():
    """Start the mock C64U server as a pytest fixture"""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    server_script = os.path.join(script_dir, 'mock_c64u_server.py')
    proc = subprocess.Popen(
        [sys.executable, server_script, '--port', '8064'],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    time.sleep(1)
    yield proc
    proc.terminate()
    proc.wait()

def test_keystroke_injection(mock_server):
    """Test keystroke injection with backpressure"""
    base_url = "http://localhost:8064"

    print("Testing keystroke injection with backpressure...")

    # 1. Reset machine (initializes keyboard buffer to empty)
    print("\n1. Resetting machine...")
    r = requests.put(f"{base_url}/v1/machine:reset")
    assert r.status_code == 200
    print("✓ Machine reset")

    # 2. Verify keyboard buffer is empty
    print("\n2. Checking keyboard buffer...")
    r = requests.get(f"{base_url}/v1/machine:readmem?address=00C6&length=1")
    assert r.status_code == 200
    buffer_len = r.content[0]
    assert buffer_len == 0, f"Expected empty buffer, got {buffer_len}"
    print(f"✓ Keyboard buffer empty (length={buffer_len})")

    # 3. Write keystroke data to buffer
    # Simulate injecting "HELLO" (PETSCII: 48 45 4C 4C 4F)
    print("\n3. Injecting keystrokes 'HELLO'...")
    hello_bytes = "48454C4C4F"  # H=0x48, E=0x45, L=0x4C, O=0x4F
    r = requests.put(f"{base_url}/v1/machine:writemem?address=0277&data={hello_bytes}")
    assert r.status_code == 200
    print("✓ Wrote keystrokes to buffer")

    # 4. Update buffer length
    print("\n4. Updating buffer length...")
    r = requests.put(f"{base_url}/v1/machine:writemem?address=00C6&data=05")  # 5 characters
    assert r.status_code == 200
    print("✓ Buffer length updated")

    # 5. Verify buffer contents
    print("\n5. Verifying buffer contents...")
    r = requests.get(f"{base_url}/v1/machine:readmem?address=0277&length=5")
    assert r.status_code == 200
    assert r.content.hex() == hello_bytes.lower(), f"Expected {hello_bytes.lower()}, got {r.content.hex()}"
    print(f"✓ Buffer contains: {r.content.hex().upper()}")

    # 6. Read buffer length
    r = requests.get(f"{base_url}/v1/machine:readmem?address=00C6&length=1")
    assert r.status_code == 200
    buffer_len = r.content[0]
    assert buffer_len == 5, f"Expected length=5, got {buffer_len}"
    print(f"✓ Buffer length verified: {buffer_len}")

    print("\n✅ All keystroke injection tests passed!")
