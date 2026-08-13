#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

CHANNEL="${1:-experimental}"
if [[ "$CHANNEL" != "stable" && "$CHANNEL" != "experimental" ]]; then
  echo "Usage: $0 [stable|experimental]" >&2
  exit 2
fi

if [[ "$CHANNEL" == "stable" ]]; then
  if [[ -n "$(git status --porcelain)" ]]; then
    echo "Refusing to refresh stable archive from a dirty working tree." >&2
    echo "Stable must represent the latest committed and pushed firmware." >&2
    exit 1
  fi
  if git rev-parse --abbrev-ref --symbolic-full-name '@{u}' >/dev/null 2>&1; then
    if [[ "$(git rev-parse HEAD)" != "$(git rev-parse '@{u}')" ]]; then
      echo "Refusing to refresh stable archive: HEAD is not equal to its upstream." >&2
      exit 1
    fi
  fi
fi

BIN="$ROOT/release/cardputer-adv-merged.bin"
if [[ ! -f "$BIN" ]]; then
  ./scripts/build_cardputer_adv.sh
fi

ts="$(date +%Y%m%d-%H%M%S)"
sha="$(git rev-parse --short HEAD)"
archive="$ROOT/release/archive/cardputer-adv-${CHANNEL}-latest.bin"

mkdir -p "$ROOT/release/archive"
cp "$BIN" "$archive"
shasum -a 256 "$archive" > "$archive.sha256"

for old in "$ROOT"/release/archive/cardputer-adv-*.bin "$ROOT"/release/archive/cardputer-adv-*.bin.sha256; do
  base="$(basename "$old")"
  case "$base" in
    cardputer-adv-stable-latest.bin|cardputer-adv-stable-latest.bin.sha256|\
    cardputer-adv-experimental-latest.bin|cardputer-adv-experimental-latest.bin.sha256)
      ;;
    *)
      rm -f "$old"
      ;;
  esac
done

stable_sha=""
experimental_sha=""
stable_source=""
experimental_source=""
[[ -f "$ROOT/release/archive/cardputer-adv-stable-latest.bin.sha256" ]] && \
  stable_sha="$(cut -d ' ' -f1 "$ROOT/release/archive/cardputer-adv-stable-latest.bin.sha256")"
[[ -f "$ROOT/release/archive/cardputer-adv-experimental-latest.bin.sha256" ]] && \
  experimental_sha="$(cut -d ' ' -f1 "$ROOT/release/archive/cardputer-adv-experimental-latest.bin.sha256")"
if git rev-parse origin/main >/dev/null 2>&1; then
  stable_source=" source=origin/main@$(git rev-parse --short origin/main)"
elif git rev-parse --abbrev-ref --symbolic-full-name '@{u}' >/dev/null 2>&1; then
  stable_source=" source=$(git rev-parse --abbrev-ref --symbolic-full-name '@{u}')@$(git rev-parse --short '@{u}')"
fi
experimental_source=" source=working-tree-on-$sha"

cat > "$ROOT/release/archive/MANIFEST.txt" <<EOF
updated=$ts
stable=cardputer-adv-stable-latest.bin sha256=$stable_sha$stable_source
experimental=cardputer-adv-experimental-latest.bin sha256=$experimental_sha$experimental_source
EOF

echo "$archive"
