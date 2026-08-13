# Claude Cardputer Plugin

Optional Claude Code/Cowork plugin for Cardputer ADV workflows.

The plugin includes hook relay files for supported Claude Code/Cowork events.
Installed plugin examples on this machine use the same `hooks/hooks.json`
layout; the manifest validator currently validates the plugin metadata and
command list.

Hooks relay to the local bridge:

```text
http://127.0.0.1:17877/hook
```

`CARDPUTER_BRIDGE_URL` may select a different loopback address or port only.
The hook service is intentionally not a LAN endpoint, so remote URLs are
rejected.

## Test Locally

```bash
claude --plugin-dir ./claude-plugin
```

The hooks are best-effort. If the bridge is not running, they exit without
printing a response so Claude falls back to its normal UI.
