#!/usr/bin/env bash

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

bash -n cardputer tools/cardputer/common.sh
./cardputer apps | rg -qx 'rfid2       RFID2 Clone Station'
./cardputer apps | rg -qx 'claude-buddy Claude Desktop Buddy'
./cardputer install-help | rg -q 'Launcher Settings -> USB MSC'

grep -qx 'APP_VERSION="1.4.0"' apps/claude-buddy/app.env
grep -qx 'APP_PLATFORMIO_ENV="cardputer-adv-launcher-ota"' apps/claude-buddy/app.env
! grep -R -nE 'flash_cardputer_adv|erase_cardputer_adv|write_flash' \
  cardputer tools/cardputer docs/development.md README.md

source tools/cardputer/common.sh
type assert_no_local_buddy_header >/dev/null
load_contract
[[ "$LAUNCHER_OTA_SIZE" == "0x4F0000" ]]
load_app rfid2
[[ "$APP_PLATFORMIO_ENV" == "cardputer-adv-rfid2" ]]
load_app claude-buddy
[[ "$APP_ID" == "claude-buddy" ]]
[[ "$APP_TITLE" == "Claude Desktop Buddy" ]]
[[ "$APP_VERSION" == "1.4.0" ]]
[[ "$APP_PLATFORMIO_ENV" == "cardputer-adv-launcher-ota" ]]
[[ "$APP_FIRMWARE_REL" == ".pio/build/cardputer-adv-launcher-ota/firmware.bin" ]]
[[ "$APP_ARTIFACT_PREFIX" == "Claude-Desktop-Buddy" ]]

guard_root="$(mktemp -d)"
trap 'rm -rf "$guard_root"' EXIT
mkdir -p "$guard_root/apps/claude-buddy/src"
touch "$guard_root/apps/claude-buddy/src/bridge_config.local.h"
if (CARDPUTER_ROOT="$guard_root"; APP_ID="claude-buddy"; assert_no_local_buddy_header); then
  echo "Buddy local compatibility header passed release guard" >&2
  exit 1
fi
rm -rf "$guard_root"
trap - EXIT

buddy_ini="apps/claude-buddy/platformio.ini"
[[ "$(rg -c '^\[env:' "$buddy_ini")" == "1" ]]
rg -q '^\[env:cardputer-adv-launcher-ota\]$' "$buddy_ini"
rg -q '^board_build\.partitions = ../../contracts/launcher/cardputer-adv-8mb\.csv$' "$buddy_ini"
if rg -q 'no_ota\.csv|^\[env:cardputer-adv\]$|^\[env:m5stickc-plus\]$' "$buddy_ini"; then
  echo "Current Buddy PlatformIO config exposes a legacy non-Launcher environment" >&2
  exit 1
fi

buddy_wrapper="apps/claude-buddy/scripts/build_cardputer_adv.sh"
rg -Fq 'exec ./scripts/pio_local.sh run -e cardputer-adv-launcher-ota "$@"' "$buddy_wrapper"
if wrapper_output="$(CARDPUTER_ADV_ENV=cardputer-adv "$buddy_wrapper" 2>&1)"; then
  echo "Buddy build wrapper accepted forbidden legacy environment override" >&2
  exit 1
fi
[[ "$wrapper_output" == "CARDPUTER_ADV_ENV is not supported; normal builds always use cardputer-adv-launcher-ota." ]]

legacy_ini="$(git show claude-buddy-v1.3.0:apps/claude-buddy/platformio.ini)"
rg -q '^\[env:cardputer-adv\]$' <<<"$legacy_ini"
rg -q '^board_build\.partitions = no_ota\.csv$' <<<"$legacy_ini"

for helper in \
  flash_cardputer_adv.sh \
  flash_cardputer_adv_bin.sh \
  erase_cardputer_adv.sh \
  archive_cardputer_adv_fw.sh \
  package_release.sh; do
  if [[ -e "apps/claude-buddy/scripts/$helper" ]]; then
    echo "Recovery helper remains exposed in normal scripts directory: $helper" >&2
    exit 1
  fi
  if [[ ! -x "apps/claude-buddy/scripts/recovery/$helper" ]]; then
    echo "Recovery helper is missing or not executable: $helper" >&2
    exit 1
  fi
done
[[ -f apps/claude-buddy/scripts/recovery/merge_bin.py ]]

# Discover every normal Buddy/root Markdown document. Exclude recovery,
# immutable history, and implementation-plan archives from operator guidance.
normal_docs=(README.md)
while IFS= read -r doc; do
  normal_docs+=("$doc")
done < <(
  rg --files docs apps/claude-buddy -g '*.md' |
    rg -v '(^|/)(docs/history|docs/superpowers|scripts/recovery)/'
)
for required_doc in apps/claude-buddy/README.md apps/claude-buddy/docs/BUILDING.md; do
  if [[ ! " ${normal_docs[*]} " == *" $required_doc "* ]]; then
    echo "Normal Buddy documentation guard missed required file: $required_doc" >&2
    exit 1
  fi
done
if direct_guidance="$(rg -n -i '(\./scripts/(flash|erase)[^ ]*|flash_cardputer_adv|erase_cardputer_adv|pio run .* -t (upload|erase)|esptool .* (write_flash|erase_flash)|M5Burner|write_flash 0x0|erase_flash|download mode|normal merged firmware image|generated merged binary)' "${normal_docs[@]}")"; then
  echo "Direct-flash guidance found in normal Buddy documentation:" >&2
  printf '%s\n' "$direct_guidance" >&2
  exit 1
fi
if ./cardputer --help | rg -qi '(flash|erase|scripts/recovery)'; then
  echo "Root cardputer interface exposes recovery operations" >&2
  exit 1
fi

python3 - <<'PY'
import importlib.util
import os
from pathlib import Path
import tempfile

path = Path("tools/cardputer/stage_to_launcher.py")
spec = importlib.util.spec_from_file_location("stage_to_launcher", path)
module = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(module)

def expect_reject(call, label):
    try:
        call()
    except (OSError, ValueError):
        return
    raise AssertionError(f"unsafe {label} was accepted")

with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    volume = root / "volume"
    tools = volume / "tools"
    tools.mkdir(parents=True)
    firmware = root / "firmware.bin"
    payload = b"\xe9" + b"launcher-safe" * 64
    firmware.write_bytes(payload)
    name = "Claude-Desktop-Buddy-v1.4.0-test.bin"
    old = tools / "Claude-Desktop-Buddy-v1.3.9-old.bin"
    old.write_bytes(b"old")
    unrelated = tools / "Other-App-v1.0.0.bin"
    unrelated.write_bytes(b"keep")
    target = module.stage_artifact(firmware, volume, "tools", name, "Claude-Desktop-Buddy")
    assert target.read_bytes() == payload
    assert not old.exists()
    assert unrelated.read_bytes() == b"keep"

with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    real_volume = root / "real-volume"
    (real_volume / "tools").mkdir(parents=True)
    linked_volume = root / "linked-volume"
    linked_volume.symlink_to(real_volume, target_is_directory=True)
    firmware = root / "firmware.bin"
    firmware.write_bytes(b"\xe9valid")
    expect_reject(lambda: module.stage_artifact(firmware, linked_volume, "tools", "RFID2-Clone-Station-v1.5.9-test.bin", "RFID2-Clone-Station"), "volume symlink")

with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    volume = root / "volume"
    outside = root / "outside"
    volume.mkdir()
    outside.mkdir()
    (volume / "tools").symlink_to(outside, target_is_directory=True)
    firmware = root / "firmware.bin"
    firmware.write_bytes(b"\xe9valid")
    expect_reject(lambda: module.stage_artifact(firmware, volume, "tools", "RFID2-Clone-Station-v1.5.9-test.bin", "RFID2-Clone-Station"), "tools symlink")
    assert list(outside.iterdir()) == []

with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    volume = root / "volume"
    tools = volume / "tools"
    tools.mkdir(parents=True)
    outside = root / "outside.bin"
    outside.write_bytes(b"keep")
    name = "RFID2-Clone-Station-v1.5.9-test.bin"
    (tools / name).symlink_to(outside)
    firmware = root / "firmware.bin"
    firmware.write_bytes(b"\xe9valid")
    expect_reject(lambda: module.stage_artifact(firmware, volume, "tools", name, "RFID2-Clone-Station"), "target symlink")
    assert outside.read_bytes() == b"keep"

with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    volume = root / "volume"
    tools = volume / "tools"
    tools.mkdir(parents=True)
    outside = root / "outside.partial"
    outside.write_bytes(b"keep")
    name = "RFID2-Clone-Station-v1.5.9-test.bin"
    (tools / f".{name}.fixed.partial").symlink_to(outside)
    firmware = root / "firmware.bin"
    firmware.write_bytes(b"\xe9valid")
    expect_reject(lambda: module.stage_artifact(firmware, volume, "tools", name, "RFID2-Clone-Station", nonce_factory=lambda: "fixed"), "temporary symlink")
    assert outside.read_bytes() == b"keep"
PY

echo "cardputer tool static checks passed"
