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

From this source checkout, initialize with a device-reachable LAN address (not
`0.0.0.0` or `::`), then install the persistent service and user Codex MCP
entry:

```sh
node dist/bin/orca-cardputer.js init --host 192.168.1.20 --port 18765
node dist/bin/orca-cardputer.js install
node dist/bin/orca-cardputer.js mcp install
```

`install` and `start` bootstrap the per-user LaunchAgent. `stop` boots it out,
so its persistent `KeepAlive` policy cannot restart the daemon; `start` loads
the retained plist again. `status` prints only `running` or `stopped`, never raw
`launchctl` details.

Wildcard addresses are rejected. Initialization prints only a `$HOME`-redacted
path. Pairing token exists only in mode-0600 protected files and provisioning
payload bytes. Create an unused payload filename in a private directory; the
command refuses to replace an existing file and prints only its redacted path:

```sh
mkdir -p -m 700 "$HOME/.orca-cardputer-bridge/export"
PAIR_FILE="$HOME/.orca-cardputer-bridge/export/orca-pair-$(date +%s).txt"
node dist/bin/orca-cardputer.js provision "$PAIR_FILE"
```

Send that file with `../scripts/write_pairing_serial.sh "$PAIR_FILE" [port]`.
The sender verifies mode `0600`, sends exactly one `orca-pair` line, waits for
the device acknowledgement, and never prints the pairing material. It does not
access a device until that command is explicitly run.

After `mcp install`, start a fresh Codex session in Orca and confirm the
`orca-cardputer` MCP server is available. The tool registration lives in the
user Codex configuration; this package never edits Orca's managed runtime
configuration directly. Remove it with:

```sh
node dist/bin/orca-cardputer.js mcp remove
node dist/bin/orca-cardputer.js stop
node dist/bin/orca-cardputer.js uninstall
```

State lives below `~/.orca-cardputer-bridge`; directories use mode 0700 and
credentials/configuration use mode 0600. Device authenticates with CA-validated
WSS plus bearer token. This service does not use mTLS, Claude Buddy source,
private Orca hooks, terminal reads, terminal sends, or worktree controls.
Upgraded device connections also have heartbeat, application-idle, absolute
lifetime, forced-close, connection-count, frame, and outbound-buffer bounds.
