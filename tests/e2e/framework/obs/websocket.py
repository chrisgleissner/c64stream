from __future__ import annotations
import time
import json
import logging
import socket
import uuid
import hashlib
import base64
from typing import Optional, Dict, Any, Union

from ..environment import Environment

logger = logging.getLogger(__name__)

try:
    import websocket
    WEBSOCKET_AVAILABLE = True
except ImportError:
    WEBSOCKET_AVAILABLE = False
    websocket = None


class OBSWebsocketClient:
    """Client for OBS WebSocket API."""

    def __init__(self, env: Environment, enabled: bool = True):
        self.env = env
        self.enabled = enabled and WEBSOCKET_AVAILABLE
        self.url = "ws://127.0.0.1:4455"
        self.password = "e2etest123"

        if enabled and not WEBSOCKET_AVAILABLE:
            logger.warning("⚠️ WebSocket requested but 'websocket-client' package not found.")

    def wait_for_server(self, timeout: float = 30) -> bool:
        """Wait for OBS WebSocket server to be ready."""
        if not self.enabled:
            return False

        logger.info("Waiting for OBS WebSocket server...")
        start_time = time.time()

        while time.time() - start_time < timeout:
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(1)
                result = sock.connect_ex(('127.0.0.1', 4455))
                sock.close()

                if result == 0:
                    logger.info("✅ OBS WebSocket server is ready")
                    return True

            except Exception:
                pass

            time.sleep(1)

        logger.warning(f"⚠️ WebSocket server not ready after {timeout}s")
        return False

    def send_request(self, request_type: str, request_data: Optional[Dict[str, Any]] = None) -> Optional[Dict[str, Any]]:
        """Send a request to OBS via WebSocket API."""
        if not self.enabled:
            return None

        try:
            # Create connection
            ws = websocket.create_connection(self.url, timeout=5)

            # Receive Hello message
            hello_msg = json.loads(ws.recv())
            if hello_msg.get("op") != 0:
                raise Exception(f"Expected Hello message, got: {hello_msg}")

            identify_payload = {"rpcVersion": 1}
            auth_data = hello_msg.get("d", {}).get("authentication")
            if auth_data:
                challenge = auth_data["challenge"]
                salt = auth_data["salt"]
                secret = base64.b64encode(hashlib.sha256((self.password + salt).encode()).digest()).decode()
                auth_response = base64.b64encode(hashlib.sha256((secret + challenge).encode()).digest()).decode()
                identify_payload["authentication"] = auth_response

            # Identify
            ws.send(json.dumps({"op": 1, "d": identify_payload}))

            # Wait for identified
            identified_msg = json.loads(ws.recv())
            if identified_msg.get("op") != 2:
                raise Exception(f"Authentication failed: {identified_msg}")

            # Send request
            request_id = str(uuid.uuid4())
            request_msg = {
                "op": 6,
                "d": {
                    "requestType": request_type,
                    "requestId": request_id,
                    "requestData": request_data or {}
                }
            }
            ws.send(json.dumps(request_msg))

            # Receive response
            response = json.loads(ws.recv())
            ws.close()

            if response.get("op") == 7:  # RequestResponse
                r_status = response.get("d", {}).get("requestStatus", {})
                if not r_status.get("result", False):
                    logger.info(f"OBS WS Request '{request_type}' failed: {r_status.get('comment')}")
                return response["d"]
            else:
                logger.warning(f"Unexpected WS response: {response}")
                return None

        except Exception as e:
            logger.error(f"OBS WebSocket error: {e}")
            return None

    def set_input_settings(self, input_name: str, settings: Dict[str, Any], overlay: bool = True) -> bool:
        """Set OBS input settings via WebSocket."""
        resp = self.send_request("SetInputSettings", {
            "inputName": input_name,
            "inputSettings": settings,
            "overlay": overlay,
        })
        if not resp:
            return False

        status = resp.get("requestStatus") or {}
        return bool(status.get("result", False))

    def start_recording(self) -> bool:
        """Start recording via WebSocket."""
        resp = self.send_request("StartRecord")
        if resp:
            status = resp.get("requestStatus") or {}
            if status.get("result", False):
                logger.info("✅ Recording started via WebSocket API")
                return True
        return False
