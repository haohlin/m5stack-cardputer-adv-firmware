#!/usr/bin/env bash

set -euo pipefail

CARDPUTER_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

die() {
  echo "cardputer: $*" >&2
  exit 1
}

load_contract() {
  # shellcheck source=../../launcher.lock
  source "$CARDPUTER_ROOT/launcher.lock"
}

load_app() {
  local requested="${1:-}"
  case "$requested" in
    rfid2|claude-buddy) ;;
    *) die "unknown app '$requested' (use: rfid2, claude-buddy)" ;;
  esac

  APP_DIR="$CARDPUTER_ROOT/apps/$requested"
  APP_CONFIG="$APP_DIR/app.env"
  [[ -f "$APP_CONFIG" ]] || die "missing app metadata: $APP_CONFIG"
  # shellcheck disable=SC1090
  source "$APP_CONFIG"
  [[ "$APP_ID" == "$requested" ]] || die "app metadata ID mismatch in $APP_CONFIG"
}

file_size_bytes() {
  if stat -f %z "$1" >/dev/null 2>&1; then
    stat -f %z "$1"
  else
    stat -c %s "$1"
  fi
}

git_revision() {
  git -C "$CARDPUTER_ROOT" rev-parse --short=12 HEAD
}

git_dirty() {
  if git -C "$CARDPUTER_ROOT" diff --quiet -- "$APP_DIR" && \
     git -C "$CARDPUTER_ROOT" diff --cached --quiet -- "$APP_DIR"; then
    echo false
  else
    echo true
  fi
}

firmware_path() {
  printf '%s/%s\n' "$APP_DIR" "$APP_FIRMWARE_REL"
}

assert_raw_app_image() {
  local image="$1"
  [[ -f "$image" ]] || die "firmware not found: $image"
  [[ "$(xxd -p -l 1 "$image")" == "e9" ]] || die "not an ESP application image: $image"

  local bytes max_bytes
  bytes="$(file_size_bytes "$image")"
  max_bytes=$((LAUNCHER_OTA_SIZE))
  (( bytes <= max_bytes )) || die "firmware is ${bytes} bytes; Launcher OTA limit is ${max_bytes}"
}

artifact_basename() {
  printf '%s-v%s-%s.bin\n' "$APP_ARTIFACT_PREFIX" "$APP_VERSION" "$(git_revision)"
}

build_app() {
  local requested="$1"
  load_app "$requested"
  "$APP_DIR/scripts/build_cardputer_adv.sh"
}

package_app() {
  local requested="$1"
  load_contract
  load_app "$requested"
  # Keep stdout machine-readable: callers capture only the packaged path.
  build_app "$requested" >&2

  local firmware artifact dist_dir sha bytes commit dirty manifest
  firmware="$(firmware_path)"
  assert_raw_app_image "$firmware"
  artifact="$(artifact_basename)"
  dist_dir="$CARDPUTER_ROOT/dist/$APP_ID"
  manifest="$dist_dir/${artifact%.bin}.json"
  mkdir -p "$dist_dir"
  cp "$firmware" "$dist_dir/$artifact"
  sha="$(shasum -a 256 "$dist_dir/$artifact" | awk '{print $1}')"
  bytes="$(file_size_bytes "$dist_dir/$artifact")"
  commit="$(git_revision)"
  dirty="$(git_dirty)"
  printf '{\n  "app_id": "%s",\n  "title": "%s",\n  "version": "%s",\n  "git_commit": "%s",\n  "git_dirty": %s,\n  "platformio_environment": "%s",\n  "launcher_contract_commit": "%s",\n  "filename": "%s",\n  "sha256": "%s",\n  "size_bytes": %s\n}\n' \
    "$APP_ID" "$APP_TITLE" "$APP_VERSION" "$commit" "$dirty" "$APP_PLATFORMIO_ENV" \
    "$LAUNCHER_COMMIT" "$artifact" "$sha" "$bytes" > "$manifest"
  echo "$dist_dir/$artifact"
}

find_launcher_sd_root() {
  if [[ -n "${CARDPUTER_SD_ROOT:-}" ]]; then
    [[ -d "$CARDPUTER_SD_ROOT/$LAUNCHER_SD_TOOLS_DIR" ]] || die "CARDPUTER_SD_ROOT has no $LAUNCHER_SD_TOOLS_DIR directory"
    printf '%s\n' "$CARDPUTER_SD_ROOT"
    return
  fi

  local volume candidate="" matches=0
  for volume in /Volumes/*; do
    [[ -d "$volume/$LAUNCHER_SD_TOOLS_DIR" ]] || continue
    if [[ -d "$volume/apps" || -d "$volume/games" || -d "$volume/downloads" ]]; then
      candidate="$volume"
      matches=$((matches + 1))
    fi
  done

  (( matches == 1 )) || die "need exactly one Launcher USB MSC volume; set CARDPUTER_SD_ROOT explicitly"
  printf '%s\n' "$candidate"
}

stage_app() {
  local requested="$1"
  load_contract
  load_app "$requested"

  local packaged sd_root sd_tools target target_sha source_sha old
  packaged="$(package_app "$requested")"
  sd_root="$(find_launcher_sd_root)"
  sd_tools="$sd_root/$LAUNCHER_SD_TOOLS_DIR"
  target="$sd_tools/$(basename "$packaged")"
  source_sha="$(shasum -a 256 "$packaged" | awk '{print $1}')"

  cp "$packaged" "$target.partial"
  target_sha="$(shasum -a 256 "$target.partial" | awk '{print $1}')"
  [[ "$source_sha" == "$target_sha" ]] || die "copied artifact checksum mismatch"
  mv -f "$target.partial" "$target"

  shopt -s nullglob
  for old in "$sd_tools"/"$APP_ARTIFACT_PREFIX"-v*.bin; do
    [[ "$old" == "$target" ]] || rm -f "$old"
  done
  shopt -u nullglob

  echo "Staged $target"
  echo "Next: eject USB MSC, exit Launcher USB MSC, then select tools/$(basename "$target") and Install."

  if [[ "${CARDPUTER_NO_EJECT:-0}" != "1" && "$(uname -s)" == "Darwin" ]]; then
    diskutil quiet eject "$sd_root" && echo "Ejected $sd_root" || echo "Eject manually: $sd_root" >&2
  fi
}
