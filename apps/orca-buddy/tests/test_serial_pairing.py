#!/usr/bin/env python3
import os
import stat
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

SCRIPT_DIR = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_DIR))

import serial_pairing


class ProvisioningPayloadTests(unittest.TestCase):
    def write_payload(self, directory: Path, payload: bytes, mode: int = 0o600) -> Path:
        path = directory / "orca-pair.txt"
        path.write_bytes(payload)
        path.chmod(mode)
        return path

    def test_accepts_one_protected_complete_orca_pair_line(self):
        with tempfile.TemporaryDirectory() as temp:
            path = self.write_payload(Path(temp), b'orca-pair {"version":"orca-cardputer/v1"}\n')
            self.assertEqual(serial_pairing.read_provisioning_payload(path), path.read_bytes())

    def test_rejects_public_or_multiline_or_wrong_prefix_payload(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            public = self.write_payload(directory, b'orca-pair {}\n', 0o644)
            with self.assertRaisesRegex(ValueError, "0600"):
                serial_pairing.read_provisioning_payload(public)

            multiline = self.write_payload(directory, b'orca-pair {}\nsecond\n')
            with self.assertRaisesRegex(ValueError, "one complete"):
                serial_pairing.read_provisioning_payload(multiline)

            wrong = self.write_payload(directory, b'pair {}\n')
            with self.assertRaisesRegex(ValueError, "orca-pair"):
                serial_pairing.read_provisioning_payload(wrong)

    def test_rejects_symlink_and_overlong_payload(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            target = self.write_payload(directory, b'orca-pair {}\n')
            link = directory / "link.txt"
            link.symlink_to(target)
            with self.assertRaisesRegex(ValueError, "regular"):
                serial_pairing.read_provisioning_payload(link)

            overlong = self.write_payload(directory, b"orca-pair " + b"x" * 12288 + b"\n")
            with self.assertRaisesRegex(ValueError, "too large"):
                serial_pairing.read_provisioning_payload(overlong)


class PairingTransportTests(unittest.TestCase):
    def test_sends_resynchronized_paced_frames_before_waiting_for_device_ack(self):
        class FakeSerialPort:
            def __init__(self):
                self.writes = []
                self.closed = False

            def reset_input_buffer(self):
                return None

            def write(self, payload):
                self.writes.append(bytes(payload))
                return len(payload)

            def flush(self):
                return None

            def read(self, _maximum):
                return b"OK secure pairing saved\n"

            def close(self):
                self.closed = True

        class FakeSerialModule:
            def __init__(self, port):
                self.port = port

            def Serial(self, path, baudrate, timeout, write_timeout):
                self.path = path
                self.baudrate = baudrate
                self.timeout = timeout
                self.write_timeout = write_timeout
                return self.port

        port = FakeSerialPort()
        module = FakeSerialModule(port)
        payload = b"orca-pair " + b"x" * 1329 + b"\n"

        with patch.dict(sys.modules, {"serial": module}), patch.object(serial_pairing.time, "sleep"):
            serial_pairing.send_pairing("/dev/cu.usbmodem-test", payload)

        self.assertEqual(port.writes[0], b"\n")
        self.assertEqual(b"".join(port.writes[1:]), payload)
        self.assertTrue(all(len(frame) <= 16 for frame in port.writes[1:]))
        self.assertTrue(port.closed)


if __name__ == "__main__":
    unittest.main()
