#!/usr/bin/env python3

import hashlib
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RFID2 = ROOT / "apps" / "rfid2"


class Rfid2SecurityTests(unittest.TestCase):
    def test_firmware_integrates_security_guards(self):
        source = (RFID2 / "src" / "main.cpp").read_text()
        self.assertIn("rfidConfirmedCommandLength(command.c_str())", source)
        self.assertIn("rfidConfirmedCommandLength(tail.c_str())", source)
        self.assertIn("serialDestructiveCardReady(PendingAction::WriteSlot", source)
        self.assertIn("serialDestructiveCardReady(PendingAction::CloneSlot", source)
        self.assertIn("armedCardUid = uid;", source)
        self.assertIn("armedCardUid = \"\";", source)
        self.assertIn("writeStoredDumpToSelectedClassic1k((uint8_t)parsedSlot)", source)
        self.assertIn("cloneStoredDumpToSelectedClassic1k((uint8_t)parsedSlot)", source)
        self.assertIn("write-block is serial-disabled", source)
        self.assertIn("rfidWriteArmDeadline(millis())", source)
        self.assertGreaterEqual(source.count("rfidPersistFileAllowed("), 3)
        self.assertGreaterEqual(source.count("rfidPersistLineAllowed("), 3)
        self.assertIn("rfidKeyCountAllowsInsert(mifareKeys.size())", source)

    def test_embedded_confirmation_and_persistence_bounds(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "rfid2-security-test"
            subprocess.run(
                [
                    "c++",
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(RFID2 / "src"),
                    str(ROOT / "tests" / "rfid2-security-test.cpp"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)

    def test_launcher_deploy_stages_over_usb_with_checksum_and_replaces_old_version(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            volume = root / "launcher"
            tools = volume / "tools"
            tools.mkdir(parents=True)
            old = tools / "RFID2-Clone-Station-v1.5.8-old.bin"
            old.write_bytes(b"old")
            firmware = root / "firmware.bin"
            payload = b"\xe9" + b"secure-firmware" * 32
            firmware.write_bytes(payload)
            target_name = "RFID2-Clone-Station-v1.5.9-test.bin"

            result = subprocess.run(
                [
                    "python3",
                    str(RFID2 / "scripts" / "launcher_deploy.py"),
                    str(firmware),
                    target_name,
                    "--volume",
                    str(volume),
                ],
                check=True,
                text=True,
                capture_output=True,
            )

            target = tools / target_name
            self.assertIn("SUCCESS", result.stdout)
            self.assertFalse(old.exists())
            self.assertEqual(target.read_bytes(), payload)
            self.assertEqual(
                hashlib.sha256(target.read_bytes()).hexdigest(),
                hashlib.sha256(payload).hexdigest(),
            )

    def test_launcher_deploy_rejects_path_like_sd_name(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            volume = root / "launcher"
            (volume / "tools").mkdir(parents=True)
            firmware = root / "firmware.bin"
            firmware.write_bytes(b"\xe9valid")

            result = subprocess.run(
                [
                    "python3",
                    str(RFID2 / "scripts" / "launcher_deploy.py"),
                    str(firmware),
                    "../escape.bin",
                    "--volume",
                    str(volume),
                ],
                text=True,
                capture_output=True,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((volume / "escape.bin").exists())

    def test_launcher_deploy_rejects_other_artifact_families(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            volume = root / "launcher"
            tools = volume / "tools"
            tools.mkdir(parents=True)
            unrelated = tools / "Another-App-v1.0.bin"
            unrelated.write_bytes(b"keep")
            firmware = root / "firmware.bin"
            firmware.write_bytes(b"\xe9valid")

            result = subprocess.run(
                [
                    "python3",
                    str(RFID2 / "scripts" / "launcher_deploy.py"),
                    str(firmware),
                    "RFID2-v1.5.9.bin",
                    "--volume",
                    str(volume),
                ],
                text=True,
                capture_output=True,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(unrelated.read_bytes(), b"keep")

    def test_launcher_deploy_rejects_symlinked_tools_directory(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            volume = root / "launcher"
            outside_tools = root / "outside-tools"
            volume.mkdir()
            outside_tools.mkdir()
            (volume / "tools").symlink_to(outside_tools, target_is_directory=True)
            firmware = root / "firmware.bin"
            firmware.write_bytes(b"\xe9valid")
            target_name = "RFID2-Clone-Station-v1.5.9-test.bin"

            result = subprocess.run(
                [
                    "python3",
                    str(RFID2 / "scripts" / "launcher_deploy.py"),
                    str(firmware),
                    target_name,
                    "--volume",
                    str(volume),
                ],
                text=True,
                capture_output=True,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((outside_tools / target_name).exists())

    def test_launcher_deploy_uses_exclusive_temp_and_fixed_cleanup_scope(self):
        source = (RFID2 / "scripts" / "launcher_deploy.py").read_text()
        self.assertIn("tempfile.mkstemp(", source)
        self.assertIn('APP_PREFIX = "RFID2-Clone-Station-"', source)
        self.assertIn("old.is_symlink()", source)
        self.assertNotIn("def name_prefix", source)


if __name__ == "__main__":
    unittest.main()
