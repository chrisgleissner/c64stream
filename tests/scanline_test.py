#!/usr/bin/env python3
"""
Quick scanline test iteration script.
Builds plugin, launches OBS, takes screenshot via WebSocket.
"""

import subprocess
import time
import json
import base64
import hashlib
from pathlib import Path

try:
    import websocket
except ImportError:
    print("ERROR: websocket-client not available. Install with: pip3 install websocket-client")
    exit(1)

PROJECT_ROOT = Path(__file__).parent.parent
BUILD_DIR = PROJECT_ROOT / "build_x86_64"
OUTPUT_DIR = PROJECT_ROOT / "tests" / "scanline_output"
OBS_WS_PORT = 4455

def build_and_install():
    """Build and install the plugin."""
    print("Building plugin...")
    result = subprocess.run(
        ["cmake", "--build", str(BUILD_DIR)],
        cwd=PROJECT_ROOT,
        capture_output=True,
        text=True
    )
    if result.returncode != 0:
        print(f"Build failed: {result.stderr}")
        return False

    print("Installing plugin...")
    home = Path.home()
    plugin_dir = home / ".config/obs-studio/plugins/c64stream"
    (plugin_dir / "bin/64bit").mkdir(parents=True, exist_ok=True)
    (plugin_dir / "data").mkdir(parents=True, exist_ok=True)

    subprocess.run([
        "cp", str(BUILD_DIR / "c64stream.so"),
        str(plugin_dir / "bin/64bit/")
    ])
    subprocess.run([
        "cp", "-r", str(PROJECT_ROOT / "data") + "/.",
        str(plugin_dir / "data/")
    ])

    print("Plugin installed.")
    return True

def take_screenshot(ws, filename):
    """Take a screenshot via OBS WebSocket and save it."""
    # OBS WebSocket 5.x protocol
    request_id = hashlib.md5(str(time.time()).encode()).hexdigest()[:8]

    # Get current program scene screenshot
    request = {
        "op": 6,  # Request
        "d": {
            "requestType": "GetSourceScreenshot",
            "requestId": request_id,
            "requestData": {
                "sourceName": "C64 Stream",
                "imageFormat": "png",
                "imageWidth": 1920,
                "imageHeight": 1080
            }
        }
    }

    ws.send(json.dumps(request))

    # Wait for response
    for _ in range(50):  # 5 second timeout
        try:
            response = ws.recv()
            data = json.loads(response)
            if data.get("op") == 7:  # RequestResponse
                resp_data = data.get("d", {})
                if resp_data.get("requestId") == request_id:
                    if resp_data.get("requestStatus", {}).get("result"):
                        img_data = resp_data.get("responseData", {}).get("imageData", "")
                        if img_data.startswith("data:image/png;base64,"):
                            img_data = img_data[22:]

                        OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
                        output_path = OUTPUT_DIR / filename
                        with open(output_path, "wb") as f:
                            f.write(base64.b64decode(img_data))
                        print(f"Screenshot saved: {output_path}")
                        return True
                    else:
                        print(f"Screenshot failed: {resp_data.get('requestStatus', {}).get('comment', 'Unknown error')}")
                        return False
        except websocket.WebSocketTimeoutException:
            pass
        time.sleep(0.1)

    print("Timeout waiting for screenshot response")
    return False

def connect_obs():
    """Connect to OBS WebSocket."""
    ws = websocket.WebSocket()
    ws.settimeout(1.0)

    try:
        ws.connect(f"ws://localhost:{OBS_WS_PORT}")
        print(f"Connected to OBS WebSocket on port {OBS_WS_PORT}")

        # Wait for Hello message
        hello = json.loads(ws.recv())
        if hello.get("op") != 0:
            print(f"Unexpected message: {hello}")
            return None

        # Send Identify (no auth for simplicity)
        identify = {
            "op": 1,
            "d": {
                "rpcVersion": 1
            }
        }
        ws.send(json.dumps(identify))

        # Wait for Identified
        identified = json.loads(ws.recv())
        if identified.get("op") != 2:
            print(f"Identification failed: {identified}")
            return None

        print("Authenticated with OBS")
        return ws

    except Exception as e:
        print(f"Failed to connect to OBS: {e}")
        print("Make sure OBS is running with WebSocket server enabled (Tools -> WebSocket Server Settings)")
        return None

def main():
    import argparse
    parser = argparse.ArgumentParser(description="Quick scanline test iteration")
    parser.add_argument("--build", action="store_true", help="Build and install plugin first")
    parser.add_argument("--screenshot", type=str, default="scanline_test.png", help="Screenshot filename")
    args = parser.parse_args()

    if args.build:
        if not build_and_install():
            return 1

    ws = connect_obs()
    if not ws:
        return 1

    try:
        # Give OBS a moment to render
        time.sleep(0.5)

        if take_screenshot(ws, args.screenshot):
            print(f"\nScreenshot saved to: {OUTPUT_DIR / args.screenshot}")
            print("Open it to check scanline quality.")
            return 0
        else:
            return 1
    finally:
        ws.close()

if __name__ == "__main__":
    exit(main())
