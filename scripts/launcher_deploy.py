#!/usr/bin/env python3
"""
Deploy a firmware .bin to the launcher SD card tools/ folder over WiFi.

Usage:
  launcher_deploy.py <firmware.bin> <sd_name> [--ip 192.168.x.x] [--user admin] [--pass launcher]

  <firmware.bin>  Local path to the .bin to upload
  <sd_name>       Filename to use on the SD card (e.g. RFID2-Clone-Station-v1.4.1.bin)
  --ip            Launcher IP (default: read from .launcher_ip or 192.168.1.13)
  --user          Web UI username (default: admin)
  --pass          Web UI password (default: launcher)

The script:
  1. Logs in (cookie-based session, reuses cached cookie in .launcher_session)
  2. Removes any file in /tools/ matching the same base name prefix (strips version)
  3. Uploads the new bin to /tools/<sd_name>
  4. Prints SUCCESS or SKIP (if launcher unreachable)
"""

import argparse
import hashlib
import http.cookiejar
import os
import re
import sys
import urllib.error
import urllib.parse
import urllib.request

DEFAULT_IP   = "192.168.1.13"
DEFAULT_USER = "admin"
DEFAULT_PASS = "launcher"
COOKIE_FILE  = os.path.join(os.path.dirname(__file__), ".launcher_session")
IP_FILE      = os.path.join(os.path.dirname(__file__), ".launcher_ip")


def load_ip():
    if os.path.exists(IP_FILE):
        with open(IP_FILE) as f:
            ip = f.read().strip()
            if ip:
                return ip
    return DEFAULT_IP


def save_ip(ip):
    with open(IP_FILE, "w") as f:
        f.write(ip)


class LauncherClient:
    def __init__(self, ip, user, password):
        self.base = f"http://{ip}"
        self.user = user
        self.password = password
        self.jar = http.cookiejar.MozillaCookieJar(COOKIE_FILE)
        if os.path.exists(COOKIE_FILE):
            try:
                self.jar.load(ignore_discard=True, ignore_expires=True)
            except Exception:
                pass
        self.opener = urllib.request.build_opener(
            urllib.request.HTTPCookieProcessor(self.jar)
        )

    def _save_cookies(self):
        try:
            self.jar.save(ignore_discard=True, ignore_expires=True)
        except Exception:
            pass

    def _is_authed(self):
        """Quick probe: a 200 on /systeminfo means the session is still valid."""
        try:
            r = self.opener.open(f"{self.base}/systeminfo", timeout=4)
            body = r.read(20).decode("utf-8", "replace")
            return r.status == 200 and "VERSION" in body
        except Exception:
            return False

    def login(self):
        if self._is_authed():
            return True
        data = urllib.parse.urlencode(
            {"username": self.user, "password": self.password}
        ).encode()
        req = urllib.request.Request(
            f"{self.base}/login", data=data, method="POST"
        )
        req.add_header("Content-Type", "application/x-www-form-urlencoded")
        try:
            self.opener.open(req, timeout=5)
            cookies = {c.name: c.value for c in self.jar}
            if "ESP32SESSION" in cookies:
                self._save_cookies()
                return True
            return False
        except Exception:
            return False

    def list_files(self, folder="/tools"):
        try:
            url = f"{self.base}/listfiles?folder={urllib.parse.quote(folder)}"
            r = self.opener.open(url, timeout=6)
            return r.read().decode("utf-8", "replace"), r.status
        except Exception as e:
            return str(e), 0

    def delete_file(self, path):
        try:
            url = f"{self.base}/file?name={urllib.parse.quote(path)}&action=delete"
            r = self.opener.open(url, timeout=6)
            return r.read().decode("utf-8", "replace"), r.status
        except urllib.error.HTTPError as e:
            return e.read().decode(), e.code
        except Exception as e:
            return str(e), 0

    def upload_file(self, sd_folder, sd_filename, local_path):
        with open(local_path, "rb") as f:
            data = f.read()
        boundary = "----LauncherDeploy" + hashlib.md5(sd_filename.encode()).hexdigest()[:8]
        body = (
            f"--{boundary}\r\n"
            f'Content-Disposition: form-data; name="file"; filename="{sd_filename}"\r\n'
            f"Content-Type: application/octet-stream\r\n\r\n"
        ).encode() + data + f"\r\n--{boundary}--\r\n".encode()
        url = f"{self.base}/OTAFILE?folder={urllib.parse.quote(sd_folder)}"
        req = urllib.request.Request(url, data=body, method="POST")
        req.add_header("Content-Type", f"multipart/form-data; boundary={boundary}")
        req.add_header("Content-Length", str(len(body)))
        try:
            r = self.opener.open(req, timeout=120)
            return r.read().decode("utf-8", "replace"), r.status
        except urllib.error.HTTPError as e:
            return e.read().decode(), e.code
        except Exception as e:
            return str(e), 0


def name_prefix(filename):
    """
    Strip the version suffix from a firmware filename so we can find and
    remove the old version.  E.g.:
      RFID2-Clone-Station-v1.4.0.bin  → RFID2-Clone-Station-
      Claude-Desktop-Buddy-v1.3.0.bin → Claude-Desktop-Buddy-
    """
    return re.sub(r"[-_]?v?\d+\.\d+[\.\d]*\.bin$", "-", filename, flags=re.I)


def main():
    ap = argparse.ArgumentParser(description="Deploy .bin to launcher SD card tools/ folder")
    ap.add_argument("firmware",  help="Local firmware .bin path")
    ap.add_argument("sd_name",   help="Filename on SD card, e.g. RFID2-Clone-Station-v1.4.1.bin")
    ap.add_argument("--ip",   default=None,         help="Launcher IP (default: from .launcher_ip or 192.168.1.13)")
    ap.add_argument("--user", default=DEFAULT_USER,  help="Web UI user")
    ap.add_argument("--pass", dest="password", default=DEFAULT_PASS, help="Web UI password")
    ap.add_argument("--folder", default="/tools", help="SD folder (default: /tools)")
    args = ap.parse_args()

    ip = args.ip or load_ip()
    if args.ip:
        save_ip(args.ip)  # remember for next time

    if not os.path.exists(args.firmware):
        print(f"[DEPLOY] ERROR: firmware not found: {args.firmware}", file=sys.stderr)
        sys.exit(1)

    client = LauncherClient(ip, args.user, args.password)

    if not client.login():
        print(f"[DEPLOY] SKIP: launcher web UI not reachable at {ip} (device not in launcher mode?)")
        sys.exit(0)   # exit 0 = non-fatal; don't break the build

    # Remove old versions of the same firmware (same prefix, different version)
    prefix = name_prefix(args.sd_name)
    listing, _ = client.list_files(args.folder)
    for line in listing.splitlines():
        fname = line.strip().rstrip("/")
        if not fname or fname.startswith(".."):
            continue
        if fname.startswith(prefix) and fname.endswith(".bin") and fname != args.sd_name:
            sd_path = f"{args.folder}/{fname}"
            resp, code = client.delete_file(sd_path)
            print(f"[DEPLOY] Removed old: {sd_path} ({code})")

    # Upload the new bin
    size_kb = os.path.getsize(args.firmware) / 1024
    print(f"[DEPLOY] Uploading {args.sd_name} ({size_kb:.0f} KB) → {args.folder}/")
    resp, code = client.upload_file(args.folder, args.sd_name, args.firmware)
    if code in (200, 201):
        print(f"[DEPLOY] SUCCESS: {args.folder}/{args.sd_name} on launcher SD")
    else:
        print(f"[DEPLOY] WARN: upload returned {code}: {resp[:100]}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
