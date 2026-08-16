#!/usr/bin/env python3
"""Executable version contract for all user-visible Orca Buddy pieces."""

import json
import subprocess
import tempfile
import unittest
from pathlib import Path


APP_ROOT = Path(__file__).resolve().parents[1]
CHECKER = APP_ROOT / "scripts" / "check_version_contract.py"


def write_fixture(root: Path, plugin_version: str = "0.1.1") -> None:
    (root / "orca-plugin").mkdir(parents=True)
    (root / "desktop-bridge").mkdir()
    (root / "app.env").write_text('APP_VERSION="0.1.1"\n', encoding="utf-8")
    (root / "orca-plugin" / "orca-plugin.json").write_text(
        json.dumps({"version": plugin_version}), encoding="utf-8"
    )
    (root / "desktop-bridge" / "package.json").write_text(
        json.dumps({"version": "0.1.1"}), encoding="utf-8"
    )
    (root / "desktop-bridge" / "package-lock.json").write_text(
        json.dumps({"version": "0.1.1", "packages": {"": {"version": "0.1.1"}}}),
        encoding="utf-8",
    )


class VersionContractTest(unittest.TestCase):
    def run_checker(self, root: Path) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            ["python3", str(CHECKER), "--app-root", str(root)],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

    def test_reports_shared_visible_version(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_fixture(root)
            result = self.run_checker(root)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "0.1.1\n")

    def test_rejects_plugin_version_mismatch_before_release(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_fixture(root, plugin_version="0.1.0")
            result = self.run_checker(root)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("orca-plugin.json", result.stderr)


if __name__ == "__main__":
    unittest.main()
