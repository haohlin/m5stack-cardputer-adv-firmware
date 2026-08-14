# Orca Cardputer Buddy plugin

This is an experimental Orca Desktop plugin source. It adds a reviewed,
read-only Cardputer Buddy panel. The panel reads only the focused worktree's
name, branch, and terminal count through Orca's `workspace:read` capability.
It does not install or start the bridge, connect to USB, configure Wi-Fi, read
terminal content, send terminal input, or access pairing credentials.

## Add through Orca Desktop

1. Open **Settings → Plugins** in Orca 1.4.180 or later.
2. Enable **Plugin system**.
3. Expand **Development**.
4. Enter this absolute folder path, then select **Add path**:

   ```text
   <repository>/apps/orca-buddy/orca-plugin
   ```

5. Review `orca-plugin.json`. It requests only **Read focused worktree
   context**.
6. Enable `haohlin.orca-cardputer`, then select its Cardputer Buddy item in
   Orca's right sidebar.

The Development path is local and is intentionally not an Orca marketplace
installation. No host service or device changes occur during these steps.

## Set up the actual device bridge

The plugin is deliberately separate from the secure bridge. Run this once in a
Terminal after choosing the Mac LAN address that Cardputer will reach. Both
machines must join the same reachable network; do not use `127.0.0.1`,
`0.0.0.0`, or a VPN-only address.

```sh
REPO=/absolute/path/to/m5stack-cardputer-adv-firmware
BRIDGE="$REPO/apps/orca-buddy/desktop-bridge"

cd "$BRIDGE"
npm ci
npm test
npm run build
node dist/bin/orca-cardputer.js init --host <MAC_LAN_IPV4> --port 17654
node dist/bin/orca-cardputer.js install
node dist/bin/orca-cardputer.js status
```

`init` creates a local CA, WSS certificate, device bearer, protected pairing
file, and mode-restricted bridge state below `~/.orca-cardputer-bridge`.
`install` starts a per-user LaunchAgent. Neither command edits Orca's plugin
configuration or adds an MCP server.

## USB serial pairing for a Launcher-installed Cardputer

First install `orca-buddy` via Launcher. Build/release/stage use the normal
collection route; select the staged raw image from Launcher `tools/` and choose
**Install**. Do not direct-flash this app.

On Cardputer, press `W`, select the same Wi-Fi network, and enter its
passphrase. Then attach its USB data cable and create/send one protected
pairing payload:

```sh
REPO=/absolute/path/to/m5stack-cardputer-adv-firmware
BRIDGE="$REPO/apps/orca-buddy/desktop-bridge"
PAIR_DIR="$HOME/.orca-cardputer-bridge/export"
PAIR_FILE="$PAIR_DIR/orca-pair-$(date +%s).txt"
PORT=/dev/cu.usbmodemXXXX

mkdir -p -m 700 "$PAIR_DIR"
cd "$BRIDGE"
node dist/bin/orca-cardputer.js provision "$PAIR_FILE"
"$REPO/apps/orca-buddy/scripts/write_pairing_serial.sh" "$PAIR_FILE" "$PORT"
```

The sender validates the mode-0600 payload, transfers one `orca-pair` line,
waits for `OK secure pairing saved`, and never prints bearer or CA content.
Disconnect USB afterwards. Cardputer reconnects over CA-validated WSS to
`wss://<MAC_LAN_IPV4>:17654/device` and shows bridge status on-device.

## Check and stop

```sh
cd "$REPO/apps/orca-buddy/desktop-bridge"
node dist/bin/orca-cardputer.js status
orca status --json
orca worktree ps --json
```

The bridge runs only those two public Orca JSON commands. Stop and remove its
LaunchAgent when no longer needed:

```sh
node dist/bin/orca-cardputer.js stop
node dist/bin/orca-cardputer.js uninstall
```

Those commands retain protected pairing state. Forget the connection from the
Cardputer UI before destroying local pairing material.
