# Orca Cardputer desktop bridge

Independent macOS local service for Orca Cardputer Buddy v0.1. It runs only
`orca status --json` and `orca worktree ps --json`, strips all fields except
the documented status whitelist, and exposes no terminal/control operation.

Build and test:

```sh
npm ci
npm test
npm run build
npm audit --omit=dev
```

Executables after build:

- `orca-cardputer-bridge`: persistent daemon, protected Unix socket, TLS WSS
  listener at fixed `/device`.
- `orca-cardputer-mcp`: stdio MCP adapter; connects to daemon socket only.
- `orca-cardputer`: `init`, LaunchAgent lifecycle, protected provisioning-file,
  and `codex mcp` entry management.

Initialize with loopback-only listener:

```sh
orca-cardputer init
orca-cardputer install
orca-cardputer mcp install
```

For a device-reachable LAN interface, explicitly pass its address to `init`.
Wildcard addresses are rejected. Initialization prints only a `$HOME`-redacted
path. Pairing token exists only in mode-0600 protected files and provisioning
payload bytes. `provision OUTPUT_FILE` creates payload bytes but does not access
USB, serial hardware, or any device.

State lives below `~/.orca-cardputer-bridge`; directories use mode 0700 and
credentials/configuration use mode 0600. Device authenticates with CA-validated
WSS plus bearer token. This service does not use mTLS, Claude Buddy source,
private Orca hooks, terminal reads, terminal sends, or worktree controls.
