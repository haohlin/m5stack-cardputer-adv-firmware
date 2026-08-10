# M5Stack Cardputer ADV Firmware guidance

- Use `./cardputer <command> <app>` from repository root.
- Treat `apps/rfid2` and `apps/claude-buddy` as isolated firmware projects.
- Normal installation is Launcher USB MSC then Launcher OTA. Do not direct-flash
  user firmware in normal development flow.
- Direct Buddy flash/erase scripts are recovery-only. Never erase Launcher
  without explicit user approval.
- Build before package/stage. Hardware proof needs physical device; report it
  separately from successful compilation or temporary-SD staging.
- Preserve app-specific version contracts. New tags use `rfid2-v*` or
  `claude-buddy-v*`; legacy RFID2 `v*` tags remain historical.
