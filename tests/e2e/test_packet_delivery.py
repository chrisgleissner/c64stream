#!/usr/bin/env python3
"""
Test sending UDP packets to check if the plugin receives them.
"""

import socket
import time
import subprocess
import os

def send_test_packets():
    """Send UDP packets directly to plugin ports and check if they're received."""
    
    print("Sending test UDP packets to plugin...")
    
    # Create test packet data
    video_data = b"V" * 780
    audio_data = b"A" * 770
    
    # Create UDP sockets
    video_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    audio_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    
    try:
        # Send a few test packets
        for i in range(5):
            video_bytes = video_sock.sendto(video_data, ('127.0.0.1', 11000))
            audio_bytes = audio_sock.sendto(audio_data, ('127.0.0.1', 11001))
            
            print(f"Packet {i+1}: Sent {video_bytes} video bytes, {audio_bytes} audio bytes")
            time.sleep(0.1)
            
        print("✅ Test packets sent successfully")
        return True
        
    except Exception as e:
        print(f"❌ Failed to send test packets: {e}")
        return False
        
    finally:
        video_sock.close()
        audio_sock.close()

def test_with_running_plugin():
    """Test packet sending while plugin is running."""
    
    print("Testing UDP packet delivery to running plugin...")
    print("="*50)
    
    # Set display for testing
    os.environ['DISPLAY'] = ':99'
    
    # Start Xvfb if not running
    try:
        subprocess.run(['pgrep', 'Xvfb'], check=True, capture_output=True)
        print("✅ Xvfb already running")
    except subprocess.CalledProcessError:
        print("Starting Xvfb...")
        subprocess.Popen(['Xvfb', ':99', '-screen', '0', '1024x768x24'], 
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(2)
        print("✅ Xvfb started")
    
    # Start OBS with plugin
    print("Starting OBS with C64Stream plugin...")
    obs_process = subprocess.Popen([
        'obs',
        '--profile', 'C64StreamTest',
        '--scene-collection', 'C64StreamTest',
        '--minimize-to-tray',
        '--disable-updater',
        '--disable-missing-files-check',
        '--multi'
    ], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    
    try:
        # Give OBS time to start and plugin to bind sockets
        print("Waiting for plugin to bind UDP sockets...")
        time.sleep(8)
        
        if obs_process.poll() is not None:
            print("❌ OBS failed to start")
            return False
        
        print("✅ OBS running, plugin should have bound UDP sockets")
        
        # Now send test packets
        success = send_test_packets()
        
        # Give plugin time to process
        time.sleep(2)
        
        # Check log for packet reception
        log_files = subprocess.run(['find', '/home/chris/.config/obs-studio/logs/', 
                                  '-name', '*.txt', '-mmin', '-2'], 
                                 capture_output=True, text=True)
        
        if log_files.stdout.strip():
            latest_log = log_files.stdout.strip().split('\n')[-1]
            print(f"\nChecking log: {latest_log}")
            
            # Look for packet reception messages
            packet_grep = subprocess.run(['grep', '-E', 'packet|received|UDP', latest_log],
                                       capture_output=True, text=True)
            
            if packet_grep.stdout:
                print("Packet-related messages:")
                for line in packet_grep.stdout.strip().split('\n')[-10:]:  # Last 10 lines
                    print(f"📋 {line}")
            else:
                print("No packet reception messages found")
        
        return success
        
    finally:
        # Clean up
        if obs_process.poll() is None:
            obs_process.terminate()
            try:
                obs_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                obs_process.kill()
                obs_process.wait()

if __name__ == '__main__':
    print("C64 Stream UDP Packet Delivery Test")
    print("="*50)
    
    result = test_with_running_plugin()
    
    if result:
        print("\n✅ Test completed - check log messages above")
    else:
        print("\n❌ Test failed")