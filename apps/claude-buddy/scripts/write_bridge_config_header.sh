#!/usr/bin/env bash
set -euo pipefail

echo "Compile-time bridge credentials are retired and rejected by normal builds." >&2
echo "Provision runtime state with scripts/write_bridge_config_serial.sh or scripts/write_bridge_config_folder.sh." >&2
exit 2
