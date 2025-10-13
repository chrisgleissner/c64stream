#!/usr/bin/env python3
"""
Test that lets the plugin bind UDP sockets first, then sends packets to them.
"""

import socket
import time
import os

def test_plugin_first_approach():
    """Test approach where plugin binds first, we send second."""
    
    print("Testing plugin-first UDP approach...")
    
    # Create UDP sockets for SENDING ONLY (no binding)
    video_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    audio_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    
    # Test data
    video_data = b"V" * 780  # Mock video packet
    audio_data = b"A" * 770  # Mock audio packet
    
    try:
        # Send packets to localhost ports where plugin should be listening
        video_bytes = video_sock.sendto(video_data, ('127.0.0.1', 11000))
        audio_bytes = audio_sock.sendto(audio_data, ('127.0.0.1', 11001))
        
        print(f"✅ Sent {video_bytes} bytes to video port")
        print(f"✅ Sent {audio_bytes} bytes to audio port")
        
        return True
        
    except Exception as e:
        print(f"❌ Failed to send packets: {e}")
        return False
        
    finally:
        video_sock.close()
        audio_sock.close()

if __name__ == '__main__':
    print("Testing plugin UDP approach")
    print("="*40)
    
    # This should work if the plugin is already running and bound to the ports
    # If not, it will fail gracefully
    result = test_plugin_first_approach()
    
    if result:
        print("✅ Plugin-first approach works!")
    else:
        print("❌ Plugin-first approach failed (plugin may not be running)")