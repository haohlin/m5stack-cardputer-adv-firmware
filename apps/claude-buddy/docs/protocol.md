# Protocol Notes

Claude Buddy has two independent paths.

## Official Hardware Buddy BLE

Official BLE remains normal companion transport: heartbeat snapshots, completed
turn events, permission decisions, status/name/owner/time/unpair commands, and
folder push. Unsupported BLE messages remain experimental; BLE does not expose
arbitrary desktop command execution.

## Optional secure Wi-Fi bridge

Wi-Fi bridge is opt-in. It is disabled without a complete secure bridge config.
Device configuration requires all of these values as one replacement:

```json
{
  "v": 1,
  "type": "claude-cardputer-bridge",
  "endpoint": "wss://192.168.1.10:17878/device",
  "token": "high-entropy-pairing-token",
  "ca": "-----BEGIN CERTIFICATE-----\\n...public CA...\\n-----END CERTIFICATE-----\\n"
}
```

The device rejects `ws://`, scheme-less endpoints, query tokens, weak tokens,
missing CA material, partial updates, and legacy saved bridge state. It sends
the pairing token only in WebSocket `Authorization: Bearer ...` during TLS
upgrade. Firmware uses CA-validated `beginSslWithCA`; it never enables insecure
TLS mode.

Desktop service separates local HTTP from network device transport:

- `GET http://127.0.0.1:17877/health` is loopback-only and content-free.
- `POST http://127.0.0.1:17877/hook` is loopback-only, requires independent
  bearer hook token, and rejects bodies over 32 KiB.
- `wss://<host>:17878/device` is separate TLS listener. It requires pairing
  bearer token and never accepts token query parameters.

The bridge limits pending Cardputer questions to eight and each request timeout
to at most 300 seconds. The local relay discovers hook credential only from
configured environment or protected local bridge config and does not log it.

Provision over USB serial or a `bridge-config` folder only after desktop TLS
certificate, private key, and public CA have been set. Pairing config contains
public CA, not private key. Existing bridge configs must be regenerated.

## At-rest credentials

Wi-Fi password and pairing token remain in ESP32 NVS for operation. Repository
code does not claim source-only encryption. Production devices that need
physical extraction resistance require secure boot plus flash/NVS encryption
provisioned before deployment; see collection design and progress records.
