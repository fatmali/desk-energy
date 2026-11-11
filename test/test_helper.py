import contextlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import importlib.util
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("desk_focus", Path(__file__).parents[1] / "tools" / "desk_focus.py")
helper = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(helper)


class Handler(BaseHTTPRequestHandler):
    records = []
    redirect = False

    def do_POST(self):
        self.records.append(json.loads(self.rfile.read(int(self.headers["Content-Length"]))))
        self.do_GET()

    def do_GET(self):
        if self.redirect:
            self.send_response(302)
            self.send_header("Location", "http://example.com/")
            self.end_headers()
            return
        if self.headers.get("Authorization") != "Basic Zm9jdXM6YWJjZGVmMDEyMzQ1Njc4OQ==":
            self.send_error(401)
            return
        body = b'{"remaining":1500,"phase":"focus"}'
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_):
        pass


class HelperTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        cls.thread = threading.Thread(target=cls.server.serve_forever)
        cls.thread.start()
        cls.url = f"http://127.0.0.1:{cls.server.server_port}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    def setUp(self):
        Handler.records = []
        Handler.redirect = False
        self.device = helper.Device(self.url, "abcdef0123456789")

    def test_authenticated_request(self):
        self.assertEqual(self.device.request("/api/status")["remaining"], 1500)
        with self.assertRaises(helper.DeviceError):
            helper.Device(self.url, "0000000000000000").request("/api/status")

    def test_local_addresses_only(self):
        for value in ("https://example.com", "http://example.com", "http://8.8.8.8", "http://user:pass@192.168.1.1", "http://192.168.1.1/path"):
            with self.subTest(value=value), self.assertRaises(helper.DeviceError):
                helper.local_url(value)
        self.assertEqual(helper.local_url("http://deskfocus-abc.local/"), "http://deskfocus-abc.local")

    def test_redirect_is_refused(self):
        Handler.redirect = True
        with self.assertRaises(helper.DeviceError):
            self.device.request("/api/status")

    def test_exit_codes_and_reports(self):
        for code in (0, 7):
            with self.subTest(code=code):
                result = helper.run_command(self.device, "test", "Example tests", [sys.executable, "-c", f"raise SystemExit({code})"])
                self.assertEqual(result, code)
                self.assertEqual(Handler.records[-2]["state"], "running")
                self.assertEqual(Handler.records[-1]["state"], "passed" if code == 0 else "failed")
                self.assertEqual(Handler.records[-2]["id"], Handler.records[-1]["id"])

    def test_offline_reporting_does_not_change_exit(self):
        with patch.object(self.device, "request", side_effect=helper.DeviceError("offline")), contextlib.redirect_stderr(io.StringIO()) as errors:
            result = helper.run_command(self.device, "build", "Offline build", [sys.executable, "-c", "raise SystemExit(9)"])
        self.assertEqual(result, 9)
        self.assertIn("offline", errors.getvalue())

    def test_missing_command_reports_failure(self):
        with contextlib.redirect_stderr(io.StringIO()):
            result = helper.run_command(self.device, "build", "Missing", ["/no/such/desk-focus-command"])
        self.assertEqual(result, 127)
        self.assertEqual(Handler.records[-1]["state"], "failed")

    def test_private_configuration(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(helper, "config_path", return_value=Path(folder) / "device.json"):
            helper.save_config(self.url, "abcdef0123456789")
            self.assertEqual(os.stat(helper.config_path()).st_mode & 0o777, 0o600)
            self.assertEqual(helper.load_device().request("/api/status")["phase"], "focus")

    def test_label_validation(self):
        for label in ("", "bad\ntext", "x" * 49):
            with self.assertRaises(helper.DeviceError):
                helper.run_command(self.device, "test", label, [sys.executable, "-V"])


if __name__ == "__main__":
    unittest.main()
