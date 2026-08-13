# Claude Cardputer Bridge

Local Node MCP server and WebSocket bridge for optional Cardputer ADV features.

The bridge does not replace the official Hardware Buddy BLE path. It exists for
features that official BLE cannot carry, including prompt drafts, structured
question answers, voice text, and richer summaries.

## Development

```bash
npm install
npm run build
npm run dev
```

Default local endpoints:

```text
HTTP health: http://127.0.0.1:17877/health
Hook relay:  http://127.0.0.1:17877/hook
Device WS:   ws://127.0.0.1:17877/device?token=<pairing-token>
```

For a physical Cardputer on the same Wi-Fi network, bind the bridge to the LAN
interface explicitly:

```bash
CARDPUTER_BRIDGE_BIND_HOST=0.0.0.0 npm run start
```

The firmware should still use the Mac's LAN IP address, not `0.0.0.0`. Generate
a `bridge-config` folder from the repo root with:

```bash
CARDPUTER_WIFI_SSID="..." CARDPUTER_WIFI_PASS="..." ./scripts/write_bridge_config_folder.sh
```

Drop that folder onto Claude Desktop's Hardware Buddy window. The Cardputer
stores the Wi-Fi SSID/password and bridge token in NVS, so it survives reboot
until a new config folder is sent or the device is factory reset.

Print the active config:

```bash
node dist/index.js --print-config
```

Generate a bridge config folder that can later be transferred to firmware:

```bash
npm run build
npm run pair-config
```

## MCPB

After installing the MCPB CLI:

```bash
npm install -g @anthropic-ai/mcpb
npm run build
npm run pack:mcpb
```
