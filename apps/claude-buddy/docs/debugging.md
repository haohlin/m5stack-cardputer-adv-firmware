# Claude Buddy Debugging

Normal firmware install/debug path remains Launcher USB MSC then Launcher OTA.
Do not use direct flash or erase for normal development.

## Safe order

1. Build and stage current app with root `./cardputer` workflow.
2. Install from Launcher and confirm boot on device screen.
3. Prove official BLE advertising/pairing before enabling Wi-Fi.
4. Configure desktop TLS listener, then create a complete secure bridge config.
5. Provision bridge over USB serial only when needed. Opening USB CDC may reset
   or re-enumerate Cardputer, so this is intentional diagnostic action.
6. Enable Wi-Fi from device menu and check `bridge dial`, then `bridge ok`.

## Secure bridge prerequisites

Physical Wi-Fi requires certificate validation; legacy `ws://` bridge configs
are intentionally rejected. Keep private TLS key outside repository. Provide
public CA PEM path to both bridge and provisioning command:

```bash
cd apps/claude-buddy/desktop-bridge
CARDPUTER_BRIDGE_DEVICE_BIND_HOST=0.0.0.0 \
CARDPUTER_BRIDGE_TLS_CERT=/private/path/bridge-cert.pem \
CARDPUTER_BRIDGE_TLS_KEY=/private/path/bridge-key.pem \
CARDPUTER_BRIDGE_TLS_CA=/private/path/bridge-ca.pem \
npm run start
```

In another terminal:

```bash
cd apps/claude-buddy
CARDPUTER_BRIDGE_HOST="$(ipconfig getifaddr en0)" \
CARDPUTER_BRIDGE_TLS_CA=/private/path/bridge-ca.pem \
CARDPUTER_WIFI_ON_DEVICE=1 \
./scripts/write_bridge_config_serial.sh
```

The serial command sends `wss://<host>:<device-port>/device`, a high-entropy
pairing token, and public CA. It disarms Wi-Fi before replacing config. Use
device UI to enter Wi-Fi credentials when `CARDPUTER_WIFI_ON_DEVICE=1`.

## Local service checks

```bash
curl -fsS http://127.0.0.1:17877/health
```

Health intentionally contains only service status and ports. `/hook` requires
local bearer credential and is not a LAN service. Claude plugin relay reads this
credential from protected local bridge config; do not copy it into logs.

## Debug bundle

```bash
./scripts/collect_debug_bundle.sh
```

Bundle redacts JSON secret keys and excludes Claude summaries/log payload by
default. For private incident analysis only:

```bash
CARDPUTER_DEBUG_INCLUDE_CONTENT=1 ./scripts/collect_debug_bundle.sh
```

That opt-in is intentionally not content-redacted. Review before sharing.

## Wi-Fi failures

- `no config`: regenerate complete secure config; partial/legacy config is not
  accepted.
- `bridge dial`: confirm TLS listener is running on device port, host is LAN
  reachable, and server certificate chains to transferred CA.
- `bridge wait`: Wi-Fi joined but TLS or bearer authentication failed. Check
  desktop bridge stderr without printing secrets, then regenerate config.
- Boot instability: disable Wi-Fi in device menu, retain config, and collect
  reset/phase evidence before retrying.
