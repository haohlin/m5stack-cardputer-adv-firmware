# Cardputer ADV Claude Console Architecture

This repository is a public, single-repo product built around the official
Claude Hardware Buddy BLE protocol. The firmware must remain useful without any
desktop bridge, plugin, Wi-Fi setup, or cloud service.

## Layers

1. **Firmware baseline**
   - Speaks the official Hardware Buddy Nordic UART BLE protocol.
   - Displays Claude status, assistant turn summaries, and permission prompts.
   - Sends only documented permission decisions over official BLE.
   - Keeps bridge/Wi-Fi support optional.

2. **Desktop bridge**
   - Lives in `desktop-bridge/`.
   - Runs as a local Node MCP server and MCPB bundle.
   - Starts a localhost HTTP/WebSocket service for optional device-side features
     that official BLE does not expose.
   - Does not patch Claude Desktop, scrape the UI, or use Accessibility
     automation.

3. **Claude plugin**
   - Lives in `claude-plugin/`.
   - Targets Claude Code and Cowork plugin surfaces.
   - Provides hook relay files, skills, and commands that relay supported
     events to the local bridge.
   - Is optional; plain Claude Desktop chat is not controlled by the plugin.

## Supported Control Surfaces

- **Official BLE**: Claude Desktop/Cowork Hardware Buddy bridge.
- **MCPB**: local Claude Desktop extension packaging for the bridge process.
- **Claude plugin**: Claude Code/Cowork hooks, skills, commands, and optional
  MCP config.
- **Official deep links**: only for supported new-session/prefill flows.

Unsupported by default:

- Claude Desktop UI automation.
- Connector/Rovo approval clicking when not forwarded to Hardware Buddy.
- Current-session prompt injection without an Anthropic-supported API.
- Custom BLE services competing with the official Hardware Buddy connection.

## Data Flow

```text
Claude Desktop/Cowork
  -> official Hardware Buddy BLE
  -> Cardputer ADV firmware

Claude Desktop MCPB / Claude Code plugin
  -> localhost bridge MCP/HTTP
  -> optional Wi-Fi WebSocket
  -> Cardputer ADV firmware
```

The BLE path is the stable compatibility path. The Wi-Fi path is an optional
side channel used only for features BLE cannot carry, such as prompt drafts,
question option answers, voice text, and richer summaries.

## Packaging Model

Release artifacts are generated from one repo:

- `cardputer-adv-stable.bin`
- `claude-cardputer-bridge.mcpb`
- `claude-cardputer-plugin.zip`
- `checksums.txt`
- `MANIFEST.json`

Generated artifacts live under `release/` and are ignored by git. Source files,
scripts, docs, and package manifests are tracked.

## Security Defaults

- BLE pairing remains encrypted with the device passcode flow.
- Bridge WebSocket connections require a locally generated pairing token.
- User config and tokens are generated outside git-tracked source.
- Hook failures must fall back to normal Claude UI behavior instead of blocking.
- Bridge messages carry concise display text by default, not full transcripts.
