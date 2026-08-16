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
atomic persistence. Sender settles USB CDC, resynchronizes an interrupted
previous line, then uses paced bounded writes; Cardputer ADV drops larger burst
writes during live USB operation. It does not carry normal network sessions.
Wi-Fi is runtime transport only after explicit local pairing material exists.

Wi-Fi transport uses WSS only. Device validates its configured CA and fixed
`/device` endpoint, authenticates with its dedicated Authorization bearer,
bounds frames/text/queues/reconnect behavior, and never places credentials in
source, artifacts, public logs, manifests, display, or MCP replies. Plain WS,
query-token pairing, unauthenticated peers, certificate bypass, and implicit
Wi-Fi pairing are outside contract. Wi-Fi SSID/passphrase are selected and
entered on Cardputer, then stored with the complete versioned/checksummed
configuration blob.

Launcher preserves one shared NVS partition across apps. Normal Orca Buddy
configuration writes use its `orca-buddy` NVS record. If that record rejects a
larger combined Wi-Fi/pairing update, firmware keeps the old NVS record intact
and stores the complete checksummed replacement in app SPIFFS. It never formats
SPIFFS automatically. At boot a valid SPIFFS fallback takes precedence; an
invalid or incomplete fallback is ignored and NVS remains source. Replacement
uses staged/current/previous files so a failed fallback update retains a valid
record. Explicit on-device **Forget** clears both stores. This is a reliability
fallback for Launcher-shared storage, not a new network, desktop, or export
surface; bearer, CA, and Wi-Fi credentials remain hidden from display/logs.

## MCP lifecycle

The persistent desktop bridge owns MCP lifecycle: unavailable,
local-pairing-required, connecting, connected, and disconnected/error. A user
LaunchAgent owns the daemon; a stdio MCP adapter reaches it through an
owner-only Unix socket. MCP exposes only status, notification, short question,
and prompt-draft operations. The device is not an MCP server. Pairing
credentials remain bridge/device-private and are never returned by MCP.

## Orca Desktop plugin boundary

`apps/orca-buddy/orca-plugin` is an experimental Orca Desktop development
plugin. User loads folder through **Settings → Plugins → Development**, reviews
manifest, then enables it. It contributes one sidebar panel and four global
Command Palette entries: enable bridge, pair connected device, bridge status,
and disable bridge. Panel uses public panel bridge for bounded focused-worktree
name, branch, and terminal count; it has no live sidecar channel. Device screen
remains runtime connection source.

Plugin requests `workspace:read` and `notifications:show`. Orca plugin API v1
does not yet provide process, network, USB, or sidecar capabilities. User has
explicitly approved local development-worker implementation: worker starts no
service on activation, accepts no panel input, and runs only on explicit global
command. Its source fixes executable paths and argument lists to local `npm`,
built bridge CLI, and protected USB pairing script. It never invokes shell,
accepts terminal text/serial-port/network-address input, logs pairing material,
or uses a public/ambiguous address. Known tunnel interfaces (`utun`, `tun`,
`tap`, `ppp`, `ipsec`, WireGuard, and Tailscale) are excluded before unique
private IPv4 selection; zero or multiple remaining candidates reject. This is
transparent development-plugin behavior, not marketplace portable capability
contract.

Persistent bridge remains separate LaunchAgent owner for WSS, certificate and
credential generation, public Orca JSON collection, and device runtime. Pair
command first requires configured bridge, creates unique protected payload, and
invokes existing sender only after user selected explicit command. It clears
`CARDPUTER_ADV_PORT` so sender detects exactly one attached Cardputer port.
Plugin removal retains bridge/pairing state; disable only stops LaunchAgent.
Plugin logs only fixed pairing lifecycle phases and fixed safe failure classes:
payload created, USB transfer begun, device acknowledgement, unavailable USB,
missing acknowledgement, device rejection, or saved-config failure. It never
logs subprocess output, payload content, bearer, CA, Wi-Fi credentials, or
serial input.

## Backlog controls

Orca terminal input, terminal-content relay, worktree/session changes,
permission approvals, and other remote controls are intentionally deferred.
v0.1 device input is limited to choosing a bounded question answer and sending
a bounded prompt draft through the authenticated bridge. No remote-control
protocol is fixed by v0.1.
