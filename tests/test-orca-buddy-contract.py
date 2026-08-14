#!/usr/bin/env python3
"""Contract checks for Orca Cardputer Buddy metadata-only skeleton."""

from pathlib import Path
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "apps" / "orca-buddy"


class OrcaBuddyContractTest(unittest.TestCase):
    def test_metadata_exports_launcher_release_identity(self) -> None:
        command = (
            'set -euo pipefail; source apps/orca-buddy/app.env; '
            'printf "%s\\n" "$APP_ID" "$APP_TITLE" "$APP_VERSION" '
            '"$APP_PLATFORMIO_ENV" "$APP_FIRMWARE_REL" "$APP_ARTIFACT_PREFIX"'
        )
        result = subprocess.run(
            ["bash", "-c", command],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        self.assertEqual(
            result.stdout.splitlines(),
            [
                "orca-buddy",
                "Orca Cardputer Buddy",
                "0.1.0",
                "cardputer-adv-launcher-ota",
                ".pio/build/cardputer-adv-launcher-ota/firmware.bin",
                "Orca-Cardputer-Buddy",
            ],
        )

    def test_platformio_config_uses_launcher_partition(self) -> None:
        config = (APP / "platformio.ini").read_text()
        self.assertIn("[env:cardputer-adv-launcher-ota]", config)
        self.assertIn(
            "board_build.partitions = ../../contracts/launcher/cardputer-adv-8mb.csv",
            config,
        )


if __name__ == "__main__":
    unittest.main()
