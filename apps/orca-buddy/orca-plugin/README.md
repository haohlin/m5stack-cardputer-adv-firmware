# Orca Cardputer Buddy plugin

Experimental local Orca Desktop plugin. It gives a focused-worktree sidebar
panel and four explicit Command Palette commands that control existing local
sidecar bridge. No separate companion UI needed: bridge sends public Orca
status to Cardputer; device display is connection truth.

## Add through Orca Desktop

1. Open **Settings → Plugins** in Orca 1.4.180 or later.
2. Enable **Plugin system**.
3. Expand **Development**.
4. Enter this path, then select **Add path**:

   ```text
   /Users/haohanl/dev/m5stack-cardputer-adv-firmware/apps/orca-buddy/orca-plugin
   ```

5. Review `orca-plugin.json`, then enable `haohlin.orca-cardputer`.

Manifest requests **Read focused worktree context** and **Show desktop
notifications**. Orca plugin API v1 has no process or USB capability yet. This
development-only plugin therefore uses local Node worker only after explicit
Command Palette command, with fixed source-controlled executable paths and
arguments. It never accepts shell command, terminal text, network address,
serial port, or pairing secret from Orca UI.

## Full device setup

First install `orca-buddy` through Cardputer **Launcher**: use collection
build/release/stage path, then select staged raw image from Launcher `tools/`
and choose **Install**. Do not direct-flash this app.

On Cardputer, press `W`, select reachable Wi-Fi, and enter passphrase once.
Firmware remembers it and directly reconnects whenever that SSID is reachable;
it does not rescan or ask again. While a connection is pending, **Del** cancels
it; **Ctrl + [** is Escape on Cardputer. Then use Orca Command Palette in this
order:

1. **Orca Cardputer: Enable Bridge**
2. Attach Cardputer USB data cable.
3. **Orca Cardputer: Pair Connected Device**
4. Disconnect USB after success.

`Enable Bridge` uses existing local bridge build when present. If build is
missing, it runs only `npm ci` then `npm run build` inside this repository’s
`apps/orca-buddy/desktop-bridge`. It ignores known tunnel interfaces
(`utun`, `tun`, `tap`, `ppp`, `ipsec`, WireGuard, and Tailscale), then accepts
exactly one non-loopback private IPv4 address (`10/8`, `172.16/12`, or
`192.168/16`). Zero or multiple remaining candidates fail closed rather than
selecting public or ambiguous network address.
It creates protected local CA/WSS/pairing state in
`~/.orca-cardputer-bridge` and starts existing per-user LaunchAgent bridge.
If a later network change leaves old bridge binding failed, explicit **Enable
Bridge** replaces stale certificate, endpoint, and pairing material for current
LAN address. Run **Pair Connected Device** once again before Wi-Fi reconnects.

`Pair Connected Device` performs protected USB serial pairing: it creates a
mode-0600 one-use USB payload and uses existing sender. Sender requires exactly
one detected Cardputer serial port; it never accepts `CARDPUTER_ADV_PORT`
environment override from Orca and never displays bearer or CA content. Fix
cable/port ambiguity, then rerun same command. Pairing succeeds only after
device returns `OK secure pairing saved`. It waits through a short macOS USB
CDC re-enumeration, resynchronizes interrupted serial input, and sends paced
frames, so reruns do not reuse a partial command. Plugin logs only safe stages
and fixed failure reasons; it never logs pairing material or raw subprocess
output. Firmware `0.1.5` stores Wi-Fi separately from bridge pairing, so a
pairing-store failure cannot prevent Wi-Fi save. If its independent erase
fails, the device truthfully reports that Wi-Fi was forgotten while pairing
remains, rather than claiming the old Wi-Fi config is still active.

Pairing requires a Cardputer USB serial device; being on Wi-Fi is not enough
until pairing has succeeded once. A missing serial port now reports that exact
condition, multiple ports ask for other serial devices to be unplugged, and a
repeated ESP32-S3 USB CDC reset reports a separate safe failure class.
For later diagnosis, plugin records only fixed lifecycle stages and error
classes in mode-0600
`~/.orca-cardputer-bridge/logs/plugin-events.jsonl`; it never records pairing
payloads, tokens, certificates, Wi-Fi credentials, or raw USB output.

After USB disconnect, firmware reconnects automatically through CA-validated
WSS to bridge on Mac. Device screen should show connected state plus public
Orca status/worktree summary. No desktop companion window involved.

## Status and stop

- **Orca Cardputer: Bridge Status** posts `running`, `stopped`, `failed`, or
  `not configured`. `failed` means launchd recorded a nonzero daemon exit.
- **Orca Cardputer: Disable Bridge** stops LaunchAgent but keeps protected
  pairing state. Re-run **Enable Bridge** to restore it.

No command runs when plugin is merely added or enabled. Removing plugin does
not remove LaunchAgent or pairing state; use explicit bridge uninstall only
when intentionally retiring connection.

## Manual recovery fallback

Use only if command reports ambiguous LAN/USB state. Both machines must join
same reachable network; do not use `127.0.0.1`, `0.0.0.0`, or VPN-only address.

```sh
REPO=/Users/haohanl/dev/m5stack-cardputer-adv-firmware
BRIDGE="$REPO/apps/orca-buddy/desktop-bridge"

cd "$BRIDGE"
npm ci
npm test
npm run build
node dist/bin/orca-cardputer.js init --host <MAC_LAN_IPV4> --port 17654
node dist/bin/orca-cardputer.js install
node dist/bin/orca-cardputer.js status
```

For deliberate USB port selection, use existing protected sender:

```sh
PAIR_FILE="$HOME/.orca-cardputer-bridge/export/orca-pair-$(date +%s).txt"
mkdir -p -m 700 "$(dirname "$PAIR_FILE")"
cd "$BRIDGE"
node dist/bin/orca-cardputer.js provision "$PAIR_FILE"
"$REPO/apps/orca-buddy/scripts/write_pairing_serial.sh" "$PAIR_FILE" /dev/cu.usbmodemXXXX
```

Bridge runs only public `orca status --json` and `orca worktree ps --json`.
No terminal-content collection, command execution, worktree mutation, or
approval control exists in v0.1.
