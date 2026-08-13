# Applications

This folder is reserved for additional Cardputer-Adv applications that can share the same build conventions as the buddy firmware.

For now, the shipping application is the Claude Hardware Buddy firmware in `src/`. New apps should follow these rules:

- Prefer official M5Stack libraries (`M5Cardputer`, `M5Unified`, `M5GFX`) before adding board-specific drivers.
- Preserve Cardputer-Adv pin ownership from the research notes: I2C on GPIO8/GPIO9, keyboard INT on GPIO11, display on GPIO33-GPIO38, SD on GPIO12/GPIO14/GPIO39/GPIO40.
- Build through PlatformIO and produce a merged binary suitable for M5Burner or `esptool write_flash 0x0`.
- Avoid direct Anthropic API calls unless the app genuinely needs standalone cloud behavior. The buddy integration should stay on the official local BLE protocol.

When an app grows beyond a sketch, add a dedicated PlatformIO environment and a wrapper script in `scripts/` so it can be built without changing the default buddy firmware.
