# Orca Cardputer Buddy design record

## v0.1 boundary

`orca-buddy` is an independent app identity: `Orca Cardputer Buddy`, version
`0.1.0`, and artifact prefix `Orca-Cardputer-Buddy`. It is implemented as
independent firmware plus a local macOS bridge and MCP adapter. It does not use
a Claude compatibility layer, characters, Claude graphics, private Orca hooks,
terminal-content collection, or any Orca control protocol. The only automatic
Orca inputs are public `orca status --json` and `orca worktree ps --json`.

Public root CLI is limited to `apps`, `build`, `release`, `stage`,
`install-help`, and `debug orca-buddy serial`. Build/release/stage are only
preparation for Launcher installation. No alternate installer or destructive
device operation belongs in normal Orca tooling.

## Install contract

Orca uses Cardputer ADV Launcher partition contract
`contracts/launcher/cardputer-adv-8mb.csv`. The raw app image is built,
packaged with provenance, and staged to Launcher USB MSC `tools/`; Launcher
then installs it to `ota_0`. Product behavior retains this path.

## Pairing and transport contract

USB serial pairing is the one-time local bootstrap transport. The desktop
bridge writes one mode-0600 `orca-pair` file; the checked sender transfers that
single line and the firmware replaces pairing only after full validation and
atomic persistence. It does not carry normal network sessions. Wi-Fi is runtime
transport only after explicit local pairing material exists.

Wi-Fi transport uses WSS only. Device validates its configured CA and fixed
`/device` endpoint, authenticates with its dedicated Authorization bearer,
bounds frames/text/queues/reconnect behavior, and never places credentials in
source, artifacts, public logs, manifests, display, or MCP replies. Plain WS,
query-token pairing, unauthenticated peers, certificate bypass, and implicit
Wi-Fi pairing are outside contract. Wi-Fi SSID/passphrase are selected and
entered on Cardputer, then stored with the complete versioned/checksummed
configuration blob.

## MCP lifecycle

The persistent desktop bridge owns MCP lifecycle: unavailable,
local-pairing-required, connecting, connected, and disconnected/error. A user
LaunchAgent owns the daemon; a stdio MCP adapter reaches it through an
owner-only Unix socket. MCP exposes only status, notification, short question,
and prompt-draft operations. The device is not an MCP server. Pairing
credentials remain bridge/device-private and are never returned by MCP.

## Orca Desktop plugin boundary

`apps/orca-buddy/orca-plugin` is an experimental Orca Desktop development
plugin. The user loads its folder through **Settings → Plugins → Development**,
reviews its manifest, then enables it. It contributes one sidebar panel and
requests only `workspace:read`; the panel uses Orca's public panel bridge to
show the active worktree's bounded name, branch, and terminal count.

The plugin has no worker and no network, process, USB, storage, secret, MCP,
or terminal-send capability. It must not create credentials, bind a socket,
install a LaunchAgent, change Wi-Fi, or send the USB pairing payload. The
separate desktop bridge continues to own those security-sensitive actions and
is explicitly initialized by its documented local command. This keeps an
experimental plugin API from becoming an implicit device-control boundary.

## Backlog controls

Orca terminal input, terminal-content relay, worktree/session changes,
permission approvals, and other remote controls are intentionally deferred.
v0.1 device input is limited to choosing a bounded question answer and sending
a bounded prompt draft through the authenticated bridge. No remote-control
protocol is fixed by v0.1.
