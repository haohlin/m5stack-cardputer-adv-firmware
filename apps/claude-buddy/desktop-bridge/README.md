# Claude Cardputer Bridge

Local Node MCP service plus optional secure Wi-Fi bridge for Cardputer ADV.
Official Hardware Buddy BLE remains primary path.

## Services

- `http://127.0.0.1:17877/health`: content-free local health only.
- `http://127.0.0.1:17877/hook`: loopback-only hook endpoint; bearer hook token required.
- `wss://<lan-host>:17878/device`: optional TLS device listener; bearer pairing token in `Authorization` header.

No `ws://` device endpoint or token-in-URL configuration is accepted.

## Secure Wi-Fi setup

Device listener starts only after all TLS paths are set. `CARDPUTER_BRIDGE_TLS_CA`
must contain public PEM CA material that validates certificate supplied through
`CARDPUTER_BRIDGE_TLS_CERT`. Keep private key local and outside this repository.

```bash
cd desktop-bridge
npm ci
npm run build

CARDPUTER_BRIDGE_DEVICE_BIND_HOST=0.0.0.0 \
CARDPUTER_BRIDGE_TLS_CERT=/private/path/bridge-cert.pem \
CARDPUTER_BRIDGE_TLS_KEY=/private/path/bridge-key.pem \
CARDPUTER_BRIDGE_TLS_CA=/private/path/bridge-ca.pem \
npm run start
```

Then create a pairing folder from repository root. It transfers only public CA
material, secure endpoint, and pairing token. Script refuses to overwrite an
existing output folder.

```bash
CARDPUTER_WIFI_SSID="..." CARDPUTER_WIFI_PASS="..." \
CARDPUTER_BRIDGE_TLS_CA=/private/path/bridge-ca.pem \
./apps/claude-buddy/scripts/write_bridge_config_folder.sh
```

The bridge is the sole credential authority. It generates independent pairing
and hook credentials from 24 CSPRNG bytes, encodes each as exactly 32 URL-safe
characters, marks their provenance, persists them in
`~/.claude-cardputer-bridge/config.json` mode `0600`, and never prints them.
Nonblank `CARDPUTER_PAIRING_TOKEN` or `CARDPUTER_HOOK_TOKEN` values fail startup.
A pre-marker config rotates both credentials once, so re-provision the device.
Plugin relay reads the hook credential only from the marked mode-protected file.
Firmware pattern checks protect untrusted BLE/file parsing but cannot prove
entropy; physical BLE/USB provisioning must be trusted and use bridge output.

Loopback hook admission uses a 15-second request timeout, 10-second header
timeout, 5-second keep-alive timeout, 32-connection cap, 16 requests per socket,
and 32 KiB body cap.

## Development

```bash
npm ci
npm test
```

`--print-config` reports ports, file path, and booleans only; it never prints a
token, key, certificate body, device state, or Claude summary.
