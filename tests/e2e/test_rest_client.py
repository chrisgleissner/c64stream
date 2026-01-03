#!/usr/bin/env python3
"""
Test the C64 REST client against mock server
"""

import subprocess
import time
import sys
import requests

def start_mock_server():
    """Start the mock C64U server"""
    proc = subprocess.Popen(
        [sys.executable, 'mock_c64u_server.py', '8064'],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    time.sleep(1)  # Give server time to start
    return proc

def test_rest_api():
    """Test REST API endpoints"""
    base_url = "http://localhost:8064"
    
    print("Testing REST API endpoints...")
    
    # Test reset
    print("\n1. Testing reset...")
    r = requests.put(f"{base_url}/v1/machine:reset")
    assert r.status_code == 200, f"Reset failed: {r.status_code}"
    print("✓ Reset OK")
    
    # Test write memory
    print("\n2. Testing write memory...")
    r = requests.put(f"{base_url}/v1/machine:writemem?address=C000&data=A9FF")
    assert r.status_code == 200, f"Write failed: {r.status_code}"
    print("✓ Write memory OK")
    
    # Test read memory
    print("\n3. Testing read memory...")
    r = requests.get(f"{base_url}/v1/machine:readmem?address=C000&length=2")
    assert r.status_code == 200, f"Read failed: {r.status_code}"
    assert r.content == bytes([0xA9, 0xFF]), f"Wrong data: {r.content.hex()}"
    print("✓ Read memory OK")
    
    # Test keyboard buffer access
    print("\n4. Testing keyboard buffer...")
    # Read keyboard buffer length
    r = requests.get(f"{base_url}/v1/machine:readmem?address=00C6&length=1")
    assert r.status_code == 200
    buf_len = r.content[0]
    print(f"  Keyboard buffer length: {buf_len}")
    
    # Write to keyboard buffer
    r = requests.put(f"{base_url}/v1/machine:writemem?address=0277&data=41424344")  # "ABCD"
    assert r.status_code == 200
    print("✓ Keyboard buffer OK")
    
    print("\n✅ All tests passed!")

if __name__ == '__main__':
    server = None
    try:
        # Start mock server
        server = start_mock_server()
        print("Mock server started on port 8064\n")
        
        # Run tests
        test_rest_api()
        
    except AssertionError as e:
        print(f"\n❌ Test failed: {e}")
        sys.exit(1)
    except Exception as e:
        print(f"\n❌ Error: {e}")
        sys.exit(1)
    finally:
        # Stop server
        if server:
            server.terminate()
            server.wait()
            print("\nMock server stopped")
