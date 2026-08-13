#!/usr/bin/env python3

import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
BUDDY = ROOT / "apps" / "claude-buddy"


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class ClaudeBuddySecurityTests(unittest.TestCase):
    def test_character_transfer_failure_paths_cleanup_storage(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "claude-buddy-xfer-security-test"
            subprocess.run(
                [
                    "c++",
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "tests"),
                    "-I",
                    str(BUDDY / "src"),
                    str(ROOT / "tests" / "claude-buddy-xfer-security-test.cpp"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)

    def test_embedded_security_helpers_enforce_bounds(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "claude-buddy-security-test"
            subprocess.run(
                [
                    "c++",
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(BUDDY / "src"),
                    str(ROOT / "tests" / "claude-buddy-security-test.cpp"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)

    def test_host_bridge_token_helper_matches_firmware_contract(self):
        helper = load_module("bridge_token", BUDDY / "scripts" / "bridge_token.py")
        self.assertTrue(helper.bridge_token_allowed("Az09_-bcDE12fgHI34jkLM56noPQ78rs"))
        self.assertFalse(helper.bridge_token_allowed("a" * 24))
        self.assertFalse(helper.bridge_token_allowed("a" * 32))
        self.assertFalse(helper.bridge_token_allowed("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab"))
        self.assertFalse(helper.bridge_token_allowed("01234567890123456789012345678901"))
        self.assertFalse(helper.bridge_token_allowed("Az09_-bcDE12fgHI34jkLM56noPQ78r+"))
        serial = (BUDDY / "scripts" / "write_bridge_config_serial.sh").read_text()
        folder = (BUDDY / "scripts" / "write_bridge_config_folder.sh").read_text()
        header = (BUDDY / "scripts" / "write_bridge_config_header.sh").read_text()
        self.assertIn("load_bridge_token(path)", serial)
        self.assertIn('bridge_token.py"', folder)
        self.assertIn('bridge_token.py"', header)
        with tempfile.TemporaryDirectory() as tmp:
            marked = Path(tmp) / "marked.json"
            marked.write_text(json.dumps({
                "credentialProvenance": "bridge-csprng-v1",
                "token": "Az09_-bcDE12fgHI34jkLM56noPQ78rs",
            }))
            self.assertEqual(
                helper.load_bridge_token(marked),
                "Az09_-bcDE12fgHI34jkLM56noPQ78rs",
            )
            legacy = Path(tmp) / "legacy.json"
            legacy.write_text(json.dumps({"token": "Az09_-bcDE12fgHI34jkLM56noPQ78rs"}))
            with self.assertRaisesRegex(ValueError, "bridge-generated provenance"):
                helper.load_bridge_token(legacy)

    def test_prep_rejects_manifest_name_outside_output_root(self):
        prep = load_module("prep_character", BUDDY / "tools" / "prep_character.py")
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "source"
            source.mkdir()
            victim = root / "victim"
            victim.mkdir()
            marker = victim / "keep.txt"
            marker.write_text("keep")
            Image.new("RGBA", (2, 2), (255, 0, 0, 255)).save(source / "idle.gif")
            (source / "manifest.json").write_text(
                json.dumps({"name": str(victim), "states": {"idle": "idle.gif"}})
            )
            prep.OUT_ROOT = root / "characters"

            with self.assertRaisesRegex(ValueError, "safe path component"):
                prep.install(source)

            self.assertEqual(marker.read_text(), "keep")

    def test_prep_keeps_valid_flat_character_workflow(self):
        prep = load_module("prep_character_valid", BUDDY / "tools" / "prep_character.py")
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "source"
            source.mkdir()
            Image.new("RGBA", (2, 2), (255, 0, 0, 255)).save(source / "idle.gif")
            (source / "manifest.json").write_text(
                json.dumps({"name": "safe-pet", "states": {"idle": "idle.gif"}})
            )
            prep.OUT_ROOT = root / "characters"

            prep.install(source)

            output = prep.OUT_ROOT / "safe-pet"
            self.assertTrue((output / "idle.gif").is_file())
            self.assertEqual(json.loads((output / "manifest.json").read_text())["name"], "safe-pet")

    def test_prep_rejects_unsafe_or_expanding_zip_before_extracting(self):
        prep = load_module("prep_character_zip", BUDDY / "tools" / "prep_character.py")
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            unsafe = root / "unsafe.zip"
            with zipfile.ZipFile(unsafe, "w") as archive:
                archive.writestr("../escaped", "no")
            with self.assertRaisesRegex(ValueError, "unsafe member"):
                prep.extract_character_zip(unsafe, root / "extract-unsafe")
            self.assertFalse((root / "escaped").exists())

            nonregular = root / "nonregular.zip"
            info = zipfile.ZipInfo("device")
            info.external_attr = (0o060000 << 16)  # block-device type, not a regular pack file
            with zipfile.ZipFile(nonregular, "w") as archive:
                archive.writestr(info, "no")
            with self.assertRaisesRegex(ValueError, "unsafe member"):
                prep.extract_character_zip(nonregular, root / "extract-nonregular")

            expanding = root / "expanding.zip"
            with zipfile.ZipFile(expanding, "w", compression=zipfile.ZIP_DEFLATED) as archive:
                archive.writestr("manifest.json", '{"name":"safe-pet","states":{}}')
                archive.writestr("idle.gif", b"x" * 200_000)
            with self.assertRaisesRegex(ValueError, "expansion ratio"):
                prep.extract_character_zip(expanding, root / "extract-expanding")

    def test_prep_extracts_valid_bounded_zip(self):
        prep = load_module("prep_character_zip_valid", BUDDY / "tools" / "prep_character.py")
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "valid.zip"
            with zipfile.ZipFile(source, "w") as archive:
                archive.writestr("pack/manifest.json", '{"name":"safe-pet","states":{}}')
                archive.writestr("pack/idle.gif", b"GIF89a")
            output = root / "extract-valid"
            pack = prep.extract_character_zip(source, output)
            self.assertEqual(pack, output / "pack")
            self.assertEqual((pack / "idle.gif").read_bytes(), b"GIF89a")

    def test_prep_rejects_state_name_outside_character_directory(self):
        prep = load_module("prep_character_state", BUDDY / "tools" / "prep_character.py")
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "source"
            source.mkdir()
            Image.new("RGBA", (2, 2), (255, 0, 0, 255)).save(source / "idle.gif")
            (source / "manifest.json").write_text(
                json.dumps({"name": "safe-pet", "states": {"../escaped": "idle.gif"}})
            )
            prep.OUT_ROOT = root / "characters"

            with self.assertRaisesRegex(ValueError, "state name"):
                prep.install(source)

            self.assertFalse((root / "escaped.gif").exists())

    def test_flash_rejects_nested_or_symlinked_pack_entries(self):
        tools = BUDDY / "tools"
        sys.path.insert(0, str(tools))
        try:
            flash = load_module("flash_character_security", tools / "flash_character.py")
        finally:
            sys.path.remove(str(tools))

        for unsafe_kind in ("directory", "symlink"):
            with self.subTest(unsafe_kind=unsafe_kind), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                source = root / "safe-pet"
                source.mkdir()
                (source / "manifest.json").write_text(
                    json.dumps({"name": "safe-pet", "states": {"idle": "idle.gif"}})
                )
                if unsafe_kind == "directory":
                    (source / "nested").mkdir()
                else:
                    outside = root / "outside.gif"
                    outside.write_bytes(b"outside")
                    (source / "idle.gif").symlink_to(outside)
                flash.DATA = root / "staging" / "characters"

                with self.assertRaisesRegex(SystemExit, "flat regular files"):
                    flash.flash(source)

    def test_bluetooth_off_purges_transport_and_parser_state(self):
        bridge = (BUDDY / "src" / "ble_bridge.cpp").read_text()
        data = (BUDDY / "src" / "data.h").read_text()
        main = (BUDDY / "src" / "main.cpp").read_text()

        self.assertIn("rxTail = rxHead;", bridge)
        self.assertIn("if (!radioEnabled) return 0;", bridge)
        self.assertIn("if (!radioEnabled) return -1;", bridge)
        self.assertIn("inline void dataSetBleEnabled(bool enabled)", data)
        self.assertGreaterEqual(data.count("_btLine.reset();"), 2)
        self.assertIn("dataSetBleEnabled(s.bt);", main)


    def test_bridge_config_is_not_logged_or_exposed_in_status(self):
        config = (BUDDY / "src" / "bridge_config.cpp").read_text()
        bundle = (BUDDY / "scripts" / "collect_debug_bundle.sh").read_text()
        self.assertIn('prefs.putBytes(kBridgeRecordKey, &record, sizeof(record))', config)
        self.assertIn('prefs.getBytesLength(kBridgeRecordKey)', config)
        self.assertIn('stored == sizeof(record)', config)
        self.assertNotIn('prefs.putString("br_', config)
        self.assertNotIn('prefs.getString("br_', config)
        self.assertNotIn('prefs.putBool("br_valid"', config)
        self.assertNotIn('prefs.getBool("br_valid"', config)
        self.assertIn('out[key] = "<redacted>"', bundle)
        self.assertNotIn('print(cfg.token)', config)

    def test_bridge_requires_complete_ca_validated_config(self):
        config = (BUDDY / "src" / "bridge_config.cpp").read_text()
        wifi = (BUDDY / "src" / "bridge_wifi.cpp").read_text()
        self.assertIn('constexpr char kScheme[] = "wss://"', config)
        self.assertIn('"endpoint token ca required"', config)
        self.assertIn('"secure endpoint required"', config)
        self.assertIn('"missing ca"', config)
        self.assertIn('beginSslWithCA(cfg.host, cfg.port, "/device", cfg.ca)', wifi)
        self.assertIn('"Authorization: Bearer %s\\r\\n"', wifi)
        self.assertNotIn('_ws.begin(cfg.host', wifi)

    def test_launcher_environment_includes_websocket_dependency(self):
        ini = (BUDDY / "platformio.ini").read_text()
        self.assertIn('[env:cardputer-adv-launcher-ota]', ini)
        self.assertIn('links2004/WebSockets @ ^2.6.1', ini)


if __name__ == "__main__":
    unittest.main()
