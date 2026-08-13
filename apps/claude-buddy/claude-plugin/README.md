# Claude Cardputer Plugin

Optional Claude Code/Cowork plugin for Cardputer ADV workflows.

The plugin includes hook relay files for supported Claude Code/Cowork events.
Installed plugin examples on this machine use the same `hooks/hooks.json`
layout; the manifest validator currently validates the plugin metadata and
command list.

Hooks relay to the local bridge:

```text
~/.claude-cardputer-bridge/hook.sock (HTTP over Unix-domain socket)
```

Socket path is derived from protected bridge config directory and has no URL,
port, or socket-path environment override. Relay reads marked mode-`0600`
config only, bounds stdin/request/response to 32 KiB, sends hook bearer as
defense in depth, and prints only validated expected hook JSON.

## Test Locally

```bash
claude --plugin-dir ./claude-plugin
```

The hooks are best-effort. If the bridge is not running, they exit without
printing a response so Claude falls back to its normal UI.
