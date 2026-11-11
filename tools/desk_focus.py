#!/usr/bin/env python3
"""Pair with a local Desk Focus device and report command outcomes over Wi-Fi."""

import argparse
import base64
import getpass
import ipaddress
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
import uuid


class DeviceError(Exception):
    pass


def local_url(value):
    parts = urllib.parse.urlsplit(value)
    host = parts.hostname or ""
    try:
        address = ipaddress.ip_address(host)
        local = address.is_private or address.is_loopback or address.is_link_local
    except ValueError:
        local = host.endswith(".local") or host == "localhost"
    if (parts.scheme != "http" or not local or parts.username or parts.password
            or parts.path not in ("", "/") or parts.query or parts.fragment):
        raise DeviceError("Use the device's local http://IP or http://name.local address.")
    return value.rstrip("/")


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise DeviceError("Device redirects are refused to protect the access code.")


class Device:
    def __init__(self, url, code):
        self.url = local_url(url)
        if not isinstance(code, str) or len(code) != 16 or any(c not in "0123456789abcdef" for c in code):
            raise DeviceError("The access code must be the 16 lowercase hex characters on the screen.")
        self.authorization = "Basic " + base64.b64encode(f"focus:{code}".encode()).decode()
        self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())

    def request(self, path, body=None):
        headers = {"Authorization": self.authorization}
        data = None
        if body is not None:
            data = json.dumps(body).encode()
            headers.update({"Content-Type": "application/json", "X-Desk-Focus": "1"})
        request = urllib.request.Request(self.url + path, data=data, headers=headers)
        try:
            with self.opener.open(request, timeout=3) as response:
                result = json.load(response)
        except urllib.error.HTTPError as error:
            raise DeviceError(f"Device returned HTTP {error.code}; check pairing or whether a newer job is active.") from error
        except (urllib.error.URLError, OSError, json.JSONDecodeError) as error:
            raise DeviceError(f"Device unavailable or returned invalid JSON: {error}") from error
        if not isinstance(result, dict):
            raise DeviceError("Device response must be a JSON object.")
        return result


def config_path():
    return Path.home() / ".config" / "desk-focus" / "device.json"


def load_device():
    try:
        config = json.loads(config_path().read_text())
    except FileNotFoundError as error:
        raise DeviceError("Not paired. Run: python3 tools/desk_focus.py configure --url http://DEVICE-IP") from error
    except (OSError, json.JSONDecodeError) as error:
        raise DeviceError(f"Could not read device configuration: {error}") from error
    if not isinstance(config, dict) or not isinstance(config.get("url"), str):
        raise DeviceError("Invalid device configuration. Run configure again.")
    return Device(config["url"], config.get("code"))


def save_config(url, code):
    path = config_path()
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", dir=path.parent, delete=False) as file:
            temporary = Path(file.name)
            os.fchmod(file.fileno(), 0o600)
            json.dump({"url": url, "code": code}, file)
            file.flush()
            os.fsync(file.fileno())
        os.replace(temporary, path)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def report(device, report_body, state):
    if device is None:
        return
    try:
        device.request("/api/job", {**report_body, "state": state})
    except DeviceError as error:
        print(f"Desk Focus: {error} (command execution is unaffected)", file=sys.stderr)


def run_command(device, kind, label, command):
    if not label or len(label) > 48 or any(ord(c) < 32 or ord(c) > 126 for c in label):
        raise DeviceError("Use a nonempty label of at most 48 printable ASCII characters.")
    if not command:
        raise DeviceError("Supply the command after --.")
    body = {"id": uuid.uuid4().hex, "kind": kind, "label": label}
    report(device, body, "running")
    try:
        process = subprocess.Popen(command, start_new_session=True)
    except OSError as error:
        report(device, body, "failed")
        print(f"Could not start command: {error}", file=sys.stderr)
        return 127
    cancelled = False
    try:
        while True:
            try:
                result = process.wait(timeout=15)
                break
            except subprocess.TimeoutExpired:
                report(device, body, "heartbeat")
    except KeyboardInterrupt:
        cancelled = True
        os.killpg(process.pid, signal.SIGINT)
        try:
            result = process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            result = process.wait()
    report(device, body, "cancelled" if cancelled else "passed" if result == 0 else "failed")
    return 130 if cancelled else result if result >= 0 else 128 - result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="action", required=True)
    setup = commands.add_parser("configure", help="Pair once; access code is entered privately.")
    setup.add_argument("--url", required=True)
    commands.add_parser("status", help="Read the device status.")
    run = commands.add_parser("run", help="Report a command without changing its output or exit status.")
    run.add_argument("kind", choices=("build", "test"))
    run.add_argument("label")
    run.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    try:
        if args.action == "configure":
            url = local_url(args.url)
            code = getpass.getpass("Access code from the ESP32 screen (not your Wi-Fi password): ").strip()
            device = Device(url, code)
            status = device.request("/api/status")
            if "remaining" not in status or "phase" not in status:
                raise DeviceError("This address did not return Desk Focus status.")
            save_config(url, code)
            print(f"Paired with {url}. Credentials saved locally with owner-only permissions.")
            return 0
        if args.action == "status":
            print(json.dumps(load_device().request("/api/status"), indent=2))
            return 0
        try:
            device = load_device()
        except DeviceError as error:
            print(f"Desk Focus: {error}\nRunning without device reporting.", file=sys.stderr)
            device = None
        command = args.command[1:] if args.command[:1] == ["--"] else args.command
        return run_command(device, args.kind, args.label, command)
    except (DeviceError, OSError) as error:
        print(f"Desk Focus: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
