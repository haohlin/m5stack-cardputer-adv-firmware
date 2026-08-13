#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

DIST="$ROOT/release/dist"
mkdir -p "$DIST"

./scripts/build_cardputer_adv.sh
./scripts/archive_cardputer_adv_fw.sh experimental >/dev/null

cp "$ROOT/release/archive/cardputer-adv-stable-latest.bin" "$DIST/cardputer-adv-stable.bin"
cp "$ROOT/release/archive/cardputer-adv-experimental-latest.bin" "$DIST/cardputer-adv-experimental.bin"
cp "$ROOT/README.md" "$DIST/README.md"
cp "$ROOT/docs/debugging.md" "$DIST/DEBUGGING.md"

(
  cd "$ROOT/desktop-bridge"
  if [[ -f package-lock.json ]]; then
    npm ci
  else
    npm install
  fi
  npm run build
  npm prune --omit=dev
  if command -v mcpb >/dev/null 2>&1; then
    mcpb pack
  else
    npx --yes @anthropic-ai/mcpb@^2.1.2 pack
  fi
)

bridge_bundle="$(find "$ROOT/desktop-bridge" -maxdepth 1 -name '*.mcpb' -type f | sort | tail -n 1)"
if [[ -z "${bridge_bundle:-}" ]]; then
  echo "No .mcpb bundle produced" >&2
  exit 1
fi
cp "$bridge_bundle" "$DIST/claude-cardputer-bridge.mcpb"

rm -f "$DIST/claude-cardputer-plugin.zip"
(
  cd "$ROOT/claude-plugin"
  zip -qr "$DIST/claude-cardputer-plugin.zip" . -x '*.DS_Store'
)

(
  cd "$DIST"
  shasum -a 256 cardputer-adv-stable.bin cardputer-adv-experimental.bin \
    claude-cardputer-bridge.mcpb claude-cardputer-plugin.zip \
    README.md DEBUGGING.md > checksums.txt
)

cat > "$DIST/MANIFEST.json" <<JSON
{
  "generated_at": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "git_commit": "$(git rev-parse HEAD)",
  "git_dirty": $(if [[ -n "$(git status --short)" ]]; then echo true; else echo false; fi),
  "artifacts": [
    "cardputer-adv-stable.bin",
    "cardputer-adv-experimental.bin",
    "claude-cardputer-bridge.mcpb",
    "claude-cardputer-plugin.zip",
    "README.md",
    "DEBUGGING.md",
    "checksums.txt"
  ]
}
JSON

echo "Release artifacts written to $DIST"
