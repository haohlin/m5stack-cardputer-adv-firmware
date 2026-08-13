# Protocol Notes

This project uses two protocols: Anthropic's official Hardware Buddy BLE
protocol, and an optional local Wi-Fi bridge protocol for features outside the
official BLE command surface.

## Official Hardware Buddy BLE

The official BLE protocol is unchanged. It supports:

- heartbeat snapshots: `total`, `running`, `waiting`, `msg`, `entries`,
  `tokens`, `tokens_today`, and pending `prompt`
- completed assistant `evt:"turn"` events
- permission decisions: `once` and `deny`
- status/name/owner/time/unpair commands
- folder push for character packs and future config folders

It does not officially support:

- prompt submission
- session creation/switching/continuation
- answering `AskUserQuestion` options
- connector approval flows that Claude does not forward as Hardware Buddy
  prompts
- arbitrary desktop commands

Unsupported BLE messages, including `{"cmd":"prompt"}`, are experimental only.

## Optional Bridge Protocol

The bridge protocol is JSON over WebSocket. It is opt-in and token-protected.

Default endpoint:

```text
ws://127.0.0.1:17877/device?token=<pairing-token>
```

For physical Cardputer Wi-Fi testing, the bridge must be started with an
explicit LAN bind:

```bash
CARDPUTER_BRIDGE_BIND_HOST=0.0.0.0 npm run start
```

The device-side endpoint must use the Mac's LAN IP address, for example:

```text
ws://192.168.1.10:17877/device?token=<pairing-token>
```

Generate a `bridge-config` folder with:

```bash
CARDPUTER_WIFI_SSID="..." CARDPUTER_WIFI_PASS="..." ./scripts/write_bridge_config_folder.sh
```

Drop that folder onto Claude Desktop Hardware Buddy. The device stores the
SSID, password, host, port, and token in NVS and keeps using those values until
another config folder is sent or factory reset clears them.

Every message includes:

```json
{ "v": 1, "type": "message.kind" }
```

Replyable messages also include `id`.

### Device To Bridge

```json
{ "v": 1, "type": "hello", "device": "Claude-0E65", "fw": "0.2.0" }
{ "v": 1, "type": "state", "battery": 92, "ble": true, "page": "session" }
{ "v": 1, "type": "prompt.draft", "id": "draft_1", "text": "short prompt" }
{ "v": 1, "type": "question.answer", "id": "q_1", "answer": "Approve" }
{ "v": 1, "type": "voice.text", "id": "voice_1", "text": "summarized speech" }
```

### Bridge To Device

```json
{ "v": 1, "type": "bridge.status", "connected": true }
{ "v": 1, "type": "display.summary", "title": "Claude", "text": "Done" }
{ "v": 1, "type": "question.request", "id": "q_1", "question": "...", "options": ["Yes", "No"] }
{ "v": 1, "type": "prompt.result", "id": "draft_1", "status": "queued" }
{ "v": 1, "type": "session.hint", "text": "Open new Claude session" }
```

Text fields should stay short enough for a 240x135 display. Longer text may be
truncated by firmware.

## Hook Relay Contract

Claude Code/Cowork plugin hooks send their event JSON to the bridge HTTP
endpoint:

```text
POST http://127.0.0.1:17877/hook
```

The bridge may return a hook-compatible JSON object. If the bridge is not
available or no device answers in time, the hook prints no response and Claude
falls back to its normal UI.

`AskUserQuestion` and MCP elicitation can be routed through this path. Normal
Hardware Buddy permission approvals should continue to use official BLE unless
the user explicitly enables a bridge-mediated workflow later.
