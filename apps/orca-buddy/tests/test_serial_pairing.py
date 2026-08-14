#!/usr/bin/env python3
import os
import stat
import sys
import tempfile
import unittest
from pathlib import Path

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


if __name__ == "__main__":
    unittest.main()
