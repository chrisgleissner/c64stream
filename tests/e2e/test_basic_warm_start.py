#!/usr/bin/env python3
"""
Test BASIC warm start via IRQ vector manipulation
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
        [sys.executable, server_script, '--port', '8065'],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    time.sleep(1)  # Give server time to start
    yield proc
    proc.terminate()
    proc.wait()


def test_basic_warm_start(mock_server):
    """Test BASIC warm start via IRQ vector manipulation"""
    base_url = "http://localhost:8065"

    print("Testing BASIC warm start via IRQ vector...")

    # Step 1: Initialize IRQ vector to default C64 value ($EA31 - standard IRQ handler)
    print("\n1. Setting up initial IRQ vector ($EA31)...")
    r = requests.put(f"{base_url}/v1/machine:writemem?address=0314&data=31EA")
    assert r.status_code == 200, f"Initial setup failed: {r.status_code}"

    # Verify initial IRQ vector
    r = requests.get(f"{base_url}/v1/machine:readmem?address=0314&length=2")
    assert r.status_code == 200
    assert r.content == bytes([0x31, 0xEA]), f"Wrong initial vector: {r.content.hex()}"
    print(f"✓ Initial IRQ vector: ${r.content.hex().upper()}")

    # Step 2: Read the IRQ vector (simulating what the warm start function does)
    print("\n2. Reading current IRQ vector...")
    r = requests.get(f"{base_url}/v1/machine:readmem?address=0314&length=2")
    assert r.status_code == 200
    original_low = r.content[0]
    original_high = r.content[1]
    print(f"✓ Read IRQ vector: ${original_high:02X}{original_low:02X}")

    # Step 3: Write BASIC warm start address ($A474) to IRQ vector
    print("\n3. Writing BASIC warm start vector ($A474)...")
    r = requests.put(f"{base_url}/v1/machine:writemem?address=0314&data=74A4")
    assert r.status_code == 200
    print("✓ Wrote warm start vector")

    # Verify warm start vector was written
    r = requests.get(f"{base_url}/v1/machine:readmem?address=0314&length=2")
    assert r.status_code == 200
    assert r.content == bytes([0x74, 0xA4]), f"Wrong warm start vector: {r.content.hex()}"
    print(f"✓ Verified warm start vector: ${r.content.hex().upper()}")

    # Step 4: Simulate delay (in real implementation, this happens in the code)
    print("\n4. Simulating 40ms delay...")
    time.sleep(0.04)
    print("✓ Delay completed")

    # Step 5: Restore original IRQ vector
    print("\n5. Restoring original IRQ vector...")
    restore_data = f"{original_low:02X}{original_high:02X}"
    r = requests.put(f"{base_url}/v1/machine:writemem?address=0314&data={restore_data}")
    assert r.status_code == 200
    print(f"✓ Restored vector to ${original_high:02X}{original_low:02X}")

    # Verify restoration
    r = requests.get(f"{base_url}/v1/machine:readmem?address=0314&length=2")
    assert r.status_code == 200
    assert r.content == bytes([original_low, original_high]), f"Vector not restored: {r.content.hex()}"
    print("✓ Vector restoration verified")

    print("\n✅ BASIC warm start test passed!")


def test_warm_start_repeated_calls(mock_server):
    """Test that repeated warm start calls are safe"""
    base_url = "http://localhost:8065"

    print("\nTesting repeated BASIC warm start calls...")

    # Set initial IRQ vector
    r = requests.put(f"{base_url}/v1/machine:writemem?address=0314&data=31EA")
    assert r.status_code == 200

    # Perform warm start multiple times
    for i in range(5):
        print(f"\n  Call {i+1}...")

        # Read original
        r = requests.get(f"{base_url}/v1/machine:readmem?address=0314&length=2")
        assert r.status_code == 200
        original = r.content

        # Write warm start
        r = requests.put(f"{base_url}/v1/machine:writemem?address=0314&data=74A4")
        assert r.status_code == 200

        # Verify
        r = requests.get(f"{base_url}/v1/machine:readmem?address=0314&length=2")
        assert r.status_code == 200
        assert r.content == bytes([0x74, 0xA4])

        # Restore
        r = requests.put(f"{base_url}/v1/machine:writemem?address=0314&data={original.hex()}")
        assert r.status_code == 200

        print(f"  ✓ Call {i+1} successful")

    print("\n✅ Repeated calls test passed!")


def test_ctrl_escape_reset(mock_server):
    """Test that Ctrl+Escape performs a C64 reset"""
    base_url = "http://localhost:8065"

    print("\nTesting Ctrl+Escape reset functionality...")

    # Set up some data in memory
    print("\n1. Writing test data to memory...")
    r = requests.put(f"{base_url}/v1/machine:writemem?address=C000&data=DEADBEEF")
    assert r.status_code == 200

    # Verify data was written
    r = requests.get(f"{base_url}/v1/machine:readmem?address=C000&length=4")
    assert r.status_code == 200
    assert r.content == bytes([0xDE, 0xAD, 0xBE, 0xEF])
    print("✓ Test data written")

    # Perform reset (simulating Ctrl+Escape)
    print("\n2. Performing reset (Ctrl+Escape)...")
    r = requests.put(f"{base_url}/v1/machine:reset")
    assert r.status_code == 200
    print("✓ Reset successful")

    # Verify keyboard buffer was cleared
    r = requests.get(f"{base_url}/v1/machine:readmem?address=00C6&length=1")
    assert r.status_code == 200
    assert r.content[0] == 0, f"Keyboard buffer not cleared: {r.content[0]}"
    print("✓ Keyboard buffer cleared")

    print("\n✅ Ctrl+Escape reset test passed!")


if __name__ == '__main__':
    pytest.main([__file__, '-v', '-s'])
