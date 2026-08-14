#!/usr/bin/env python3
"""Public Orca plugin package contract for Orca Cardputer Buddy."""

import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
PLUGIN = ROOT / "apps" / "orca-buddy" / "orca-plugin"


class OrcaCardputerPluginTest(unittest.TestCase):
    def test_plugin_is_loadable_from_orca_development_folder(self) -> None:
        manifest = json.loads((PLUGIN / "orca-plugin.json").read_text())

        self.assertEqual(manifest["manifestVersion"], 1)
        self.assertEqual(manifest["id"], "orca-cardputer")
        self.assertEqual(manifest["publisher"], "haohlin")
        self.assertEqual(manifest["repository"], "https://github.com/haohlin/m5stack-cardputer-adv-firmware")
        self.assertEqual(manifest["pluginApi"], 1)
        self.assertEqual(manifest["engines"], {"orca": ">=1.4.180"})
        self.assertNotIn("main", manifest)
        self.assertEqual(manifest["capabilities"], [{"kind": "workspace:read"}])
        self.assertEqual(
            manifest["contributes"]["panels"],
            [
                {
                    "id": "cardputer",
                    "title": "Cardputer Buddy",
                    "icon": "radio",
                    "entry": "panel/index.html",
                }
            ],
        )
        self.assertTrue((PLUGIN / "panel" / "index.html").is_file())

    def test_panel_explains_supported_bridge_setup_without_claiming_device_control(self) -> None:
        panel = (PLUGIN / "panel" / "index.html").read_text()

        self.assertIn("Orca Cardputer Buddy", panel)
        self.assertIn("workspace.readContext", panel)
        self.assertIn("USB serial pairing", panel)
        self.assertIn("does not create, start, or configure the bridge", panel)
        self.assertNotIn("terminal.sendText", panel)

    def test_user_guide_keeps_plugin_install_separate_from_device_pairing(self) -> None:
        guide = (PLUGIN / "README.md").read_text()

        self.assertIn("Settings → Plugins", guide)
        self.assertIn("Development", guide)
        self.assertIn("orca-plugin.json", guide)
        self.assertIn("does not install or start the bridge", guide)
        self.assertIn("USB serial pairing", guide)
        self.assertIn("Launcher", guide)


if __name__ == "__main__":
    unittest.main()
