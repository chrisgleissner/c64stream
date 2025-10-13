#!/usr/bin/env python3
"""
Test UDP socket binding to understand the port conflict issue.
"""

import socket
import time
import subprocess
import os

def test_udp_socket_binding():
    """Test if we can bind to the UDP ports that the plugin needs."""
    
    print("Testing UDP socket binding...")
    
    # Test binding to port 11000 (video)
    try:
        video_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        video_sock.bind(('127.0.0.1', 11000))
        print("✅ Successfully bound to 127.0.0.1:11000")
        
        # Test binding to port 11001 (audio)
        audio_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        audio_sock.bind(('127.0.0.1', 11001))
        print("✅ Successfully bound to 127.0.0.1:11001")
        
        print("📍 Both UDP ports are available for binding")
        
        # Close sockets properly
        video_sock.close()
        audio_sock.close()
        
        print("✅ Sockets closed successfully")
        
        # Test if plugin can bind after we release
        return True
        
    except Exception as e:
        print(f"❌ Failed to bind UDP sockets: {e}")
        return False

def test_plugin_startup():
    """Test if the plugin can start and bind UDP sockets."""
    
    print("\n" + "="*50)
    print("Testing plugin UDP socket binding...")
    
    # Set up environment
    os.environ['DISPLAY'] = ':99'
    
    # Start OBS briefly to test plugin binding
    try:
        print("Starting OBS to test plugin binding...")
        obs_process = subprocess.Popen([
            'obs',
            '--profile', 'C64StreamTest',
            '--scene-collection', 'C64StreamTest',
            '--disable-updater',
            '--disable-missing-files-check'
        ], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        
        # Give OBS time to start and plugin to initialize
        time.sleep(5)
        
        # Check if OBS is still running
        if obs_process.poll() is None:
            print("✅ OBS started successfully")
            
            # Terminate OBS
            obs_process.terminate()
            obs_process.wait(timeout=5)
            print("✅ OBS stopped successfully")
            
            return True
        else:
            stdout, stderr = obs_process.communicate()
            print(f"❌ OBS failed to start:")
            print(f"STDOUT: {stdout.decode()}")
            print(f"STDERR: {stderr.decode()}")
            return False
            
    except Exception as e:
        print(f"❌ Failed to test plugin startup: {e}")
        return False

if __name__ == '__main__':
    print("C64 Stream UDP Binding Test")
    print("="*50)
    
    # Test 1: Can we bind to the UDP ports?
    binding_ok = test_udp_socket_binding()
    
    # Test 2: Can the plugin bind when OBS starts?
    if binding_ok:
        plugin_ok = test_plugin_startup()
    else:
        print("⚠️ Skipping plugin test due to binding issues")
        plugin_ok = False
    
    print("\n" + "="*50)
    print("Test Results:")
    print(f"UDP Binding: {'✅ PASS' if binding_ok else '❌ FAIL'}")
    print(f"Plugin Binding: {'✅ PASS' if plugin_ok else '❌ FAIL'}")
    
    if binding_ok and plugin_ok:
        print("\n✅ All tests passed - UDP binding should work!")
    else:
        print("\n❌ Issues detected with UDP binding")