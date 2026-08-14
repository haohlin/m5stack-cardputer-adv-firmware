# Orca Cardputer Buddy design record

## Task 1 boundary

`orca-buddy` is an independent app identity: `Orca Cardputer Buddy`, version
`0.1.0`, and artifact prefix `Orca-Cardputer-Buddy`. This task exposes only
metadata, PlatformIO Launcher partition configuration, generic build/package/
stage/serial-monitor selection, and collection documentation. It includes no
product firmware, desktop bridge, MCP server, transport protocol, Claude
compatibility layer, character format, graphics, credentials, or generated
artifact.

Public root CLI is limited to `apps`, `build`, `release`, `stage`,
`install-help`, and `debug orca-buddy serial`. Build/release/stage are only
preparation for Launcher installation. No alternate installer or destructive
device operation belongs in normal Orca tooling.

## Install contract

Orca uses Cardputer ADV Launcher partition contract
`contracts/launcher/cardputer-adv-8mb.csv`. A future raw app image is built,
packaged with provenance, and staged to Launcher USB MSC `tools/`; Launcher
then installs it to `ota_0`. Product behavior must retain this path.

## Pairing and transport contract

USB serial pairing is local bootstrap/control transport. It may provision,
inspect, replace, or clear future Wi-Fi pairing state, but it must not carry
normal network sessions. Wi-Fi is runtime transport only after explicit local
pairing material exists.

Future Wi-Fi transport uses WSS only. Device must validate a configured CA and
endpoint, authenticate each session with dedicated pairing material, bound
incoming frames and reconnect behavior, and never place credentials in source,
artifacts, public logs, manifests, or MCP replies. Plain WS, unauthenticated
peers, certificate bypass, and implicit Wi-Fi pairing are outside contract.

## MCP lifecycle

Future desktop bridge owns MCP lifecycle: unavailable, local-pairing-required,
connecting, connected, and disconnected/error. MCP presents state and explicit
operator actions; device firmware does not become an MCP server. Pairing
credentials remain bridge/device-private and are not returned by MCP.

## Backlog controls

Control mapping is intentionally deferred with product firmware. Define and
test display states, keyboard navigation, confirmation/cancel behavior,
connection-status presentation, pairing/revocation controls, and safe handling
of transport loss before implementing input code. No control protocol is fixed
by Task 1.
