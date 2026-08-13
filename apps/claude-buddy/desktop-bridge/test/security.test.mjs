import assert from "node:assert/strict";
import { mkdtempSync, readFileSync, statSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";

import { CardputerBridge, loadConfig } from "../dist/index.js";
import { localBridgeUrl } from "../../claude-plugin/hooks/relay.mjs";

const strong = "Az09_-bcDE12fgHI34jkLM56noPQ78rs";

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

test("configured credentials require generated-token shape and reject repeated input", () => {
  const configured = tempConfig();
  const loaded = loadConfig({
    CARDPUTER_BRIDGE_CONFIG: configured,
    CARDPUTER_PAIRING_TOKEN: strong,
    CARDPUTER_HOOK_TOKEN: strong.split("").reverse().join("")
  });
  assert.equal(loaded.token, strong);
  assert.throws(() => loadConfig({ CARDPUTER_BRIDGE_CONFIG: tempConfig(), CARDPUTER_PAIRING_TOKEN: "a".repeat(24) }), /32-character URL-safe/);
  assert.throws(() => loadConfig({ CARDPUTER_BRIDGE_CONFIG: tempConfig(), CARDPUTER_PAIRING_TOKEN: "a".repeat(32) }), /32-character URL-safe/);
  assert.throws(() => loadConfig({ CARDPUTER_BRIDGE_CONFIG: tempConfig(), CARDPUTER_HOOK_TOKEN: `${strong.slice(0, 31)}+` }), /32-character URL-safe/);
});

test("generated credentials use 24 CSPRNG bytes encoded as 32 URL-safe characters", () => {
  const config = tempConfig();
  const loaded = loadConfig({ CARDPUTER_BRIDGE_CONFIG: config });
  assert.match(loaded.token, /^[A-Za-z0-9_-]{32}$/);
  assert.match(loaded.hookToken, /^[A-Za-z0-9_-]{32}$/);
  const persisted = JSON.parse(readFileSync(config, "utf8"));
  assert.equal(persisted.token, loaded.token);
  assert.equal(persisted.hookToken, loaded.hookToken);
});

test("persists generated credentials 0600 and keeps hook traffic loopback", () => {
  const config = tempConfig();
  loadConfig({ CARDPUTER_BRIDGE_CONFIG: config });
  assert.equal(statSync(config).mode & 0o777, 0o600);
  assert.equal(localBridgeUrl("http://127.0.0.1:17877"), "http://127.0.0.1:17877");
  assert.equal(localBridgeUrl("https://127.0.0.1:17877"), null);
  assert.equal(localBridgeUrl("http://bridge.example:17877"), null);
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
