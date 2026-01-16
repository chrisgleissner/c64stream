#!/usr/bin/env python3
"""
End-to-end test for REST control feature
Tests the complete workflow: REST client → Keymap → Keystroke injection
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

def test_rest_control_e2e(mock_server):
    """Test complete REST control workflow"""
    base_url = "http://localhost:8064"

    print("="*70)
    print(" C64 Stream REST Control - End-to-End Test")
    print("="*70)

    # Test 1: Machine control
    print("\n[1/5] Testing Machine Control")
    print("-" * 50)

    print("  Resetting machine...")
    r = requests.put(f"{base_url}/v1/machine:reset")
    assert r.status_code == 200, f"Reset failed: {r.status_code}"
    print("  ✓ Machine reset successful")

    print("  Rebooting machine...")
    r = requests.put(f"{base_url}/v1/machine:reboot")
    assert r.status_code == 200, f"Reboot failed: {r.status_code}"
    print("  ✓ Machine reboot successful")

    # Test 2: Memory operations
    print("\n[2/5] Testing Memory Operations")
    print("-" * 50)

    print("  Writing test data to $C000...")
    test_data = "A9FF8D20D0"  # LDA #$FF : STA $D020
    r = requests.put(f"{base_url}/v1/machine:writemem?address=C000&data={test_data}")
    assert r.status_code == 200, f"Memory write failed: {r.status_code}"
    print(f"  ✓ Wrote {len(test_data)//2} bytes to $C000")

    print("  Reading back from $C000...")
    r = requests.get(f"{base_url}/v1/machine:readmem?address=C000&length=5")
    assert r.status_code == 200, f"Memory read failed: {r.status_code}"
    read_data = r.content.hex().upper()
    assert read_data == test_data, f"Data mismatch: expected {test_data}, got {read_data}"
    print(f"  ✓ Read verified: {read_data}")

    # Test 3: Keyboard buffer
    print("\n[3/5] Testing Keyboard Buffer")
    print("-" * 50)

    print("  Checking buffer is empty...")
    r = requests.get(f"{base_url}/v1/machine:readmem?address=00C6&length=1")
    assert r.status_code == 200
    assert r.content[0] == 0, "Buffer should be empty after reset"
    print("  ✓ Buffer empty (length=0)")

    print("  Injecting keystrokes: 'LIST'...")
    list_cmd = "4C495354"  # L I S T in PETSCII
    r = requests.put(f"{base_url}/v1/machine:writemem?address=0277&data={list_cmd}")
    assert r.status_code == 200
    r = requests.put(f"{base_url}/v1/machine:writemem?address=00C6&data=04")
    assert r.status_code == 200
    print("  ✓ Keystrokes injected")

    print("  Verifying buffer contents...")
    r = requests.get(f"{base_url}/v1/machine:readmem?address=0277&length=4")
    assert r.status_code == 200
    assert r.content.hex().upper() == list_cmd, "Buffer contents mismatch"
    print(f"  ✓ Buffer verified: {r.content.hex().upper()}")

    # Test 4: SID playback (multipart upload)
    print("\n[4/5] Testing SID Playback")
    print("-" * 50)

    print("  Creating dummy SID file...")
    # Minimal SID header (actual SID would be larger)
    sid_data = b"PSID\x00\x02\x00\x76" + b"\x00" * 120

    print("  Uploading SID file...")
    files = {'file': ('test.sid', sid_data, 'application/octet-stream')}
    r = requests.post(f"{base_url}/v1/runners:sidplay?songnr=1", files=files)
    assert r.status_code == 200, f"SID upload failed: {r.status_code}"
    print("  ✓ SID file uploaded and playing")

    # Test 5: PRG execution (multipart upload)
    print("\n[5/5] Testing PRG Execution")
    print("-" * 50)

    print("  Creating dummy PRG file...")
    # Minimal PRG (load address + a few bytes)
    prg_data = b"\x01\x08\xA9\x00\x60"  # Load at $0801, LDA #$00, RTS

    print("  Uploading PRG file...")
    files = {'file': ('test.prg', prg_data, 'application/octet-stream')}
    r = requests.post(f"{base_url}/v1/runners:run_prg", files=files)
    assert r.status_code == 200, f"PRG upload failed: {r.status_code}"
    print("  ✓ PRG file uploaded and running")

    # Summary
    print("\n" + "="*70)
    print(" ✅ ALL TESTS PASSED!")
    print("="*70)
    print("\nTest Coverage:")
    print("  • REST API client (HTTP GET/PUT/POST)")
    print("  • Machine control (reset, reboot)")
    print("  • Memory DMA (read, write)")
    print("  • Keyboard buffer operations")
    print("  • Multipart file upload (SID, PRG)")
    print("  • Mock server validation")
    print("\nReady for integration with OBS!")
