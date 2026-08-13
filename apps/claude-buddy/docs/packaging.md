# Packaging And Release

The release process keeps firmware, bridge, and plugin artifacts reproducible
from a clean checkout.

## Build and release firmware

```bash
./cardputer build claude-buddy
./cardputer release claude-buddy
```

Run these commands from collection root. Versioned raw app images and provenance
manifests are written under:

```text
dist/claude-buddy/
```

Normal installation uses Launcher USB MSC and Launcher OTA:

```bash
./cardputer stage claude-buddy
```

Full-flash image packaging is recovery-only and isolated under
`scripts/recovery/`.

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
CARDPUTER_BRIDGE_DEVICE_BIND_HOST=0.0.0.0 \\
CARDPUTER_BRIDGE_TLS_CERT=/private/path/bridge-cert.pem \\
CARDPUTER_BRIDGE_TLS_KEY=/private/path/bridge-key.pem \\
CARDPUTER_BRIDGE_TLS_CA=/private/path/bridge-ca.pem \\
npm run start
```

In another terminal, generate the local config folder:

```bash
CARDPUTER_WIFI_SSID="..." CARDPUTER_WIFI_PASS="..." \\
CARDPUTER_BRIDGE_TLS_CA=/private/path/bridge-ca.pem \\
./scripts/write_bridge_config_folder.sh
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

When installed, hooks call HTTP over mode-`0600` `hook.sock` derived inside
mode-`0700` bridge config directory. No hook TCP listener or URL/socket override
exists, so relay bearer is never sent to a port-squatting local process. Pairing
config remains explicit protected local CLI/file output; MCP tools do not return
device bearer material.

Package desktop bridge and Claude plugin separately from firmware. Firmware
release remains root `./cardputer release claude-buddy`; do not bundle a merged
full-flash image into the normal Launcher artifact flow.

## Graphify

Use the repo wrapper instead of calling a global CLI directly:

```bash
./scripts/graphify_update.sh
```

The wrapper installs the `graphifyy` PyPI package into the project venv when
needed. The command it exposes is still `graphify`.
