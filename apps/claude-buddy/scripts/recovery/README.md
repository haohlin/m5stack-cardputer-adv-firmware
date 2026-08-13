# Claude Buddy recovery tools

These helpers are recovery-only. They can erase or replace Launcher and other
flash regions. Normal delivery, installation, and runtime debugging always use
root `./cardputer` with Launcher USB MSC and Launcher OTA.

Do not use these helpers for routine builds or releases. Use them only when
Launcher recovery is explicitly approved and a known merged recovery image is
already available.

- `flash_cardputer_adv.sh` and `flash_cardputer_adv_bin.sh` write merged
  full-flash images.
- `erase_cardputer_adv.sh` erases all device flash and requires
  `CARDPUTER_RECOVERY_CONFIRM=ERASE`.
- `merge_bin.py`, `archive_cardputer_adv_fw.sh`, and `package_release.sh`
  preserve historical merged-image recovery workflows.
