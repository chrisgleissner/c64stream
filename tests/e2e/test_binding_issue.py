#!/usr/bin/env python3
"""
Minimal test to reproduce the UDP binding conflict issue.
"""

import subprocess
import time
import os
import signal

def test_obs_plugin_binding():
    """Test if the plugin can bind UDP sockets when OBS starts."""
    
    print("Testing OBS plugin UDP binding...")
    
    # Set display for headless testing
    os.environ['DISPLAY'] = ':99'
    
    # Start Xvfb if not running
    try:
        subprocess.run(['pgrep', 'Xvfb'], check=True, capture_output=True)
        print("✅ Xvfb already running")
    except subprocess.CalledProcessError:
        print("Starting Xvfb...")
        xvfb = subprocess.Popen(['Xvfb', ':99', '-screen', '0', '1024x768x24'], 
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(2)
        print("✅ Xvfb started")
    
    # Start OBS with C64 plugin
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
        # Give OBS time to fully initialize
        print("Waiting for OBS initialization...")
        time.sleep(8)
        
        # Check if OBS is still running
        if obs_process.poll() is None:
            print("✅ OBS is running")
            
            # Give it more time for plugin initialization
            time.sleep(5)
            
            # Check the OBS log for binding results
            log_files = subprocess.run(['find', '/home/chris/.config/obs-studio/logs/', 
                                      '-name', '*.txt', '-mmin', '-2'], 
                                     capture_output=True, text=True)
            
            if log_files.stdout.strip():
                latest_log = log_files.stdout.strip().split('\n')[-1]
                print(f"Checking log: {latest_log}")
                
                # Look for UDP binding messages
                grep_result = subprocess.run(['grep', '-E', 'UDP|bind|socket|Failed.*port', latest_log],
                                           capture_output=True, text=True)
                
                if grep_result.stdout:
                    print("UDP binding messages:")
                    for line in grep_result.stdout.strip().split('\n'):
                        if 'Failed to bind UDP socket' in line:
                            print(f"❌ {line}")
                        elif 'UDP' in line or 'socket' in line:
                            print(f"📋 {line}")
                else:
                    print("No UDP binding messages found")
            
            return True
            
        else:
            stdout, stderr = obs_process.communicate()
            print(f"❌ OBS failed to start")
            print(f"STDOUT: {stdout.decode()}")
            print(f"STDERR: {stderr.decode()}")
            return False
            
    finally:
        # Clean up OBS
        if obs_process.poll() is None:
            print("Stopping OBS...")
            obs_process.terminate()
            try:
                obs_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                obs_process.kill()
                obs_process.wait()
            print("✅ OBS stopped")

if __name__ == '__main__':
    print("C64 Stream UDP Binding Reproduction Test")
    print("="*50)
    
    result = test_obs_plugin_binding()
    
    if result:
        print("\n✅ Test completed - check messages above for binding status")
    else:
        print("\n❌ Test failed - OBS could not start")