# Packaging And Release

The release process keeps firmware, bridge, and plugin artifacts reproducible
from a clean checkout.

## Build Firmware

```bash
./scripts/build_cardputer_adv.sh
```

The merged firmware image is written to:

```text
release/cardputer-adv-merged.bin
```

Refresh the current experimental archive alias:

```bash
./scripts/archive_cardputer_adv_fw.sh experimental
```

`release/archive/` intentionally keeps only two firmware aliases:
`cardputer-adv-stable-latest.bin` for the latest committed/pushed firmware and
`cardputer-adv-experimental-latest.bin` for the current working tree build.

## Build Desktop Bridge

```bash
cd desktop-bridge
npm install
npm run build
```

The bridge is a Node MCP server and can also be packed as a `.mcpb` bundle.

For physical device bridge testing:

```bash
cd desktop-bridge
CARDPUTER_BRIDGE_BIND_HOST=0.0.0.0 npm run start
```

In another terminal, generate the local config folder:

```bash
CARDPUTER_WIFI_SSID="..." CARDPUTER_WIFI_PASS="..." ./scripts/write_bridge_config_folder.sh
```

For debugging and release validation, prefer USB serial provisioning because it
returns a deterministic device ack:

```bash
CARDPUTER_BRIDGE_HOST="$(ipconfig getifaddr en1)" \
  CARDPUTER_WIFI_ON_DEVICE=1 \
  ./scripts/write_bridge_config_serial.sh
```

Folder push is still supported for non-critical data and character packs. If
you use it for bridge config, drop `release/bridge-config` onto Claude Desktop
Hardware Buddy. The Cardputer stores the Wi-Fi and bridge settings in NVS. On
the device, enable Menu -> settings -> wifi. The CLAUDE info page shows
`bridge ok` when the WebSocket path is live.

## Build Claude Plugin

The plugin source lives in `claude-plugin/`. It can be tested with:

```bash
claude --plugin-dir ./claude-plugin
```

When installed, hooks call the local bridge at
`http://127.0.0.1:17877` unless `CARDPUTER_BRIDGE_URL` is set.

## Package Everything

```bash
./scripts/package_release.sh
```

The script builds firmware and bridge outputs, packages the plugin, writes
checksums, and creates `release/dist/MANIFEST.json`.

Expected release artifacts:

```text
release/dist/cardputer-adv-stable.bin
release/dist/cardputer-adv-experimental.bin
release/dist/claude-cardputer-bridge.mcpb
release/dist/claude-cardputer-plugin.zip
release/dist/README.md
release/dist/DEBUGGING.md
release/dist/checksums.txt
release/dist/MANIFEST.json
```

`README.md` and `DEBUGGING.md` are included so release downloads can be used
without opening the source tree.

## Graphify

Use the repo wrapper instead of calling a global CLI directly:

```bash
./scripts/graphify_update.sh
```

The wrapper installs the `graphifyy` PyPI package into the project venv when
needed. The command it exposes is still `graphify`.
