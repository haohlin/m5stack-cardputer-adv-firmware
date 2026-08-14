#!/usr/bin/env bash
set -euo pipefail

APP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/orca-buddy-host-tests.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT

"${CXX:-c++}" \
  -std=c++17 \
  -Wall -Wextra -Werror -pedantic \
  -I"$APP_ROOT/include" \
  "$APP_ROOT/tests/test_orca_core.cpp" \
  "$APP_ROOT/src/core/config.cpp" \
  "$APP_ROOT/src/core/protocol.cpp" \
  "$APP_ROOT/src/core/ui_model.cpp" \
  "$APP_ROOT/src/core/reconnect.cpp" \
  -o "$BUILD_DIR/orca-buddy-host-tests"

"$BUILD_DIR/orca-buddy-host-tests"
