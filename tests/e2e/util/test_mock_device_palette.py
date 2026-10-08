#!/usr/bin/env python3
"""
C64 Stream - Mock Ultimate palette endpoint tests

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

The Follow device E2E scenario relies on the mock Ultimate answering like the
device: the Palette Definition setting and file size over REST, the file
content over FTP. These tests check the mock itself, so an E2E failure points
at the plugin.
"""

import json
import types
import unittest
import urllib.error
import urllib.request
from ftplib import FTP, error_perm

from framework.c64u_mock.server import MockC64UServer

SETTING_PATH = "/v1/configs/U64%20Specific%20Settings/Palette%20Definition"


class TestMockDevicePalette(unittest.TestCase):
    def setUp(self):
        env = types.SimpleNamespace(is_ci=False)
        self.mock = MockC64UServer(env, control_port=0, rest_port=0)
        self.assertTrue(self.mock.start())
        self.rest = f"http://127.0.0.1:{self.mock.rest_server.server_address[1]}"
        self.assertTrue(self.mock.enable_palette_files({"my palette.vpl": b"00 00 00\n" * 16}, ftp_port=0))

    def tearDown(self):
        self.mock.stop()

    def get(self, path):
        with urllib.request.urlopen(self.rest + path, timeout=5) as response:
            return response.status, json.loads(response.read())

    def test_setting_absent_until_configured(self):
        with self.assertRaises(urllib.error.HTTPError) as raised:
            self.get(SETTING_PATH)
        self.assertEqual(raised.exception.code, 404)

    def test_setting_and_file_info(self):
        self.mock.set_palette_setting("my palette.vpl")
        status, body = self.get(SETTING_PATH)
        self.assertEqual(status, 200)
        self.assertEqual(body["U64 Specific Settings"]["Palette Definition"]["current"], "my palette.vpl")
        status, body = self.get("/v1/files/flash/data/my%20palette.vpl:info")
        self.assertEqual(body["files"]["size"], 9 * 16)
        with self.assertRaises(urllib.error.HTTPError) as raised:
            self.get("/v1/files/flash/data/other.vpl:info")
        self.assertEqual(raised.exception.code, 404)
        self.assertEqual(self.mock.palette_setting_requests, 1)

    def test_ftp_download_and_errors(self):
        ftp = FTP()
        ftp.connect("127.0.0.1", self.mock.ftp_server.port, timeout=5)
        ftp.login("anyone", "")
        data = bytearray()
        ftp.retrbinary("RETR /Flash/data/my palette.vpl", data.extend)
        self.assertEqual(bytes(data), b"00 00 00\n" * 16)
        with self.assertRaises(error_perm):
            ftp.retrbinary("RETR /Flash/data/missing.vpl", data.extend)
        ftp.quit()
        self.assertEqual(self.mock.ftp_server.retrievals, ["my palette.vpl"])

    def test_ftp_checks_the_network_password(self):
        self.mock.ftp_server.password = "s3cret"
        ftp = FTP()
        ftp.connect("127.0.0.1", self.mock.ftp_server.port, timeout=5)
        with self.assertRaises(error_perm):
            ftp.login("anyone", "wrong")
        ftp.close()


if __name__ == "__main__":
    unittest.main()
