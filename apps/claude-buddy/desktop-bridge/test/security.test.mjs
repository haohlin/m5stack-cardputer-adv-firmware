import assert from "node:assert/strict";
import { chmodSync, mkdtempSync, readFileSync, statSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";

import { CardputerBridge, loadConfig } from "../dist/index.js";
import { localBridgeUrl, storedHookToken } from "../../claude-plugin/hooks/relay.mjs";

const strong = "Az09_-bcDE12fgHI34jkLM56noPQ78rs";
const provenance = "bridge-csprng-v1";
const reviewerTokens = [
  "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab",
  "01234567890123456789012345678901"
];

function tempConfig() {
  const dir = mkdtempSync(join(tmpdir(), "cardputer-bridge-test-"));
  return join(dir, "config.json");
}

async function unusedLoopbackPort() {
  const { createServer } = await import("node:net");
  return await new Promise(resolve => {
    const server = createServer();
    server.listen(0, "127.0.0.1", () => {
      const address = server.address();
      server.close(() => resolve(address.port));
    });
  });
}

test("rejects every nonblank caller-provided token environment variable", () => {
  for (const name of ["CARDPUTER_PAIRING_TOKEN", "CARDPUTER_HOOK_TOKEN"]) {
    for (const value of reviewerTokens) {
      assert.throws(
        () => loadConfig({ CARDPUTER_BRIDGE_CONFIG: tempConfig(), [name]: value }),
        new RegExp(`${name} is not accepted`)
      );
    }
  }
});

test("generated credentials use 24 CSPRNG bytes encoded as 32 URL-safe characters", () => {
  const config = tempConfig();
  const loaded = loadConfig({ CARDPUTER_BRIDGE_CONFIG: config });
  assert.match(loaded.token, /^[A-Za-z0-9_-]{32}$/);
  assert.match(loaded.hookToken, /^[A-Za-z0-9_-]{32}$/);
  const persisted = JSON.parse(readFileSync(config, "utf8"));
  assert.equal(persisted.credentialProvenance, provenance);
  assert.equal(persisted.token, loaded.token);
  assert.equal(persisted.hookToken, loaded.hookToken);
});

test("rotates pre-marker credentials once and then preserves generated credentials", () => {
  const config = tempConfig();
  writeFileSync(config, `${JSON.stringify({
    hookPort: 18100,
    devicePort: 18101,
    token: reviewerTokens[0],
    hookToken: reviewerTokens[1]
  })}\n`, { mode: 0o600 });

  const rotated = loadConfig({ CARDPUTER_BRIDGE_CONFIG: config });
  assert.equal(rotated.hookPort, 18100);
  assert.equal(rotated.devicePort, 18101);
  assert.match(rotated.token, /^[A-Za-z0-9_-]{32}$/);
  assert.match(rotated.hookToken, /^[A-Za-z0-9_-]{32}$/);
  assert.notEqual(rotated.token, reviewerTokens[0]);
  assert.notEqual(rotated.hookToken, reviewerTokens[1]);
  const stored = JSON.parse(readFileSync(config, "utf8"));
  assert.equal(stored.credentialProvenance, provenance);

  const reloaded = loadConfig({ CARDPUTER_BRIDGE_CONFIG: config });
  assert.equal(reloaded.token, rotated.token);
  assert.equal(reloaded.hookToken, rotated.hookToken);
});

test("persists generated credentials 0600 and keeps hook traffic loopback", () => {
  const config = tempConfig();
  loadConfig({ CARDPUTER_BRIDGE_CONFIG: config });
  assert.equal(statSync(config).mode & 0o777, 0o600);
  assert.equal(localBridgeUrl("http://127.0.0.1:17877"), "http://127.0.0.1:17877");
  assert.equal(localBridgeUrl("https://127.0.0.1:17877"), null);
  assert.equal(localBridgeUrl("http://bridge.example:17877"), null);
});

test("relay reads hook credential only from marked protected bridge config", () => {
  const marked = tempConfig();
  writeFileSync(marked, `${JSON.stringify({ credentialProvenance: provenance, hookToken: strong })}\n`, { mode: 0o600 });
  assert.equal(storedHookToken(marked), strong);

  const legacy = tempConfig();
  writeFileSync(legacy, `${JSON.stringify({ hookToken: strong })}\n`, { mode: 0o600 });
  assert.equal(storedHookToken(legacy), null);

  const exposed = tempConfig();
  writeFileSync(exposed, `${JSON.stringify({ credentialProvenance: provenance, hookToken: strong })}\n`, { mode: 0o600 });
  chmodSync(exposed, 0o644);
  assert.equal(storedHookToken(exposed), null);
});

test("hook requires credential, bounds body, and health omits content", async () => {
  const hookPort = await unusedLoopbackPort();
  const config = {
    hookPort,
    devicePort: await unusedLoopbackPort(),
    deviceHost: "127.0.0.1",
    token: strong,
    hookToken: strong.split("").reverse().join(""),
    configPath: tempConfig()
  };
  const bridge = new CardputerBridge(config);
  await bridge.start();
  try {
    bridge.notify("Claude", "secret summary must not be in health");
    const base = `http://127.0.0.1:${hookPort}`;
    const health = await fetch(`${base}/health`);
    assert.equal(health.status, 200);
    const healthBody = await health.text();
    assert(!healthBody.includes("secret summary"));
    assert(!healthBody.includes("lastSummary"));
    assert.equal((await fetch(`${base}/hook`, { method: "POST", body: "{}" })).status, 401);
    assert.equal((await fetch(`${base}/hook`, {
      method: "POST",
      headers: { authorization: `Bearer ${config.hookToken}`, "content-type": "application/json" },
      body: "x".repeat(32 * 1024 + 1)
    })).status, 413);
  } finally {
    await bridge.stop();
  }
});

test("hook listener configures bounded admission before listening", () => {
  const bridge = new CardputerBridge({
    hookPort: 17877,
    devicePort: 17878,
    deviceHost: "127.0.0.1",
    token: strong,
    hookToken: strong.split("").reverse().join(""),
    configPath: tempConfig()
  });
  const hookServer = bridge.hookServer;
  assert.equal(hookServer.requestTimeout, 15_000);
  assert.equal(hookServer.headersTimeout, 10_000);
  assert.equal(hookServer.keepAliveTimeout, 5_000);
  assert.equal(hookServer.maxConnections, 32);
  assert.equal(hookServer.maxRequestsPerSocket, 16);
});

test("secure pairing config requires TLS and emits wss with public CA", () => {
  const configPath = tempConfig();
  const certPath = `${configPath}.cert`;
  const keyPath = `${configPath}.key`;
  const caPath = `${configPath}.ca`;
  writeFileSync(certPath, "certificate");
  writeFileSync(keyPath, "private key");
  writeFileSync(caPath, "-----BEGIN CERTIFICATE-----\npublic\n-----END CERTIFICATE-----\n");
  const bridge = new CardputerBridge({ hookPort: 17877, devicePort: 17878, deviceHost: "127.0.0.1", token: strong, hookToken: strong, configPath, tlsCertPath: certPath, tlsKeyPath: keyPath, tlsCaPath: caPath });
  const pair = bridge.pairingConfig("wss://192.168.1.10:17878/device");
  assert.equal(pair.endpoint, "wss://192.168.1.10:17878/device");
  assert.equal(pair.token, strong);
  assert.match(pair.ca, /BEGIN CERTIFICATE/);
  assert.throws(() => bridge.pairingConfig("ws://192.168.1.10:17878/device"), /must be wss/);
});
