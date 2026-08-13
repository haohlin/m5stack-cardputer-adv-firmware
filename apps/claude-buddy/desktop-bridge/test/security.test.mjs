import assert from "node:assert/strict";
import { chmodSync, existsSync, lstatSync, mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { request } from "node:http";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { EventEmitter } from "node:events";
import { Readable } from "node:stream";
import test from "node:test";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";

import {
  CardputerBridge,
  bridgeHookSocketPath,
  configureDeviceServerAdmission,
  loadConfig,
  parseDeviceUpgradeTarget,
  registerTools
} from "../dist/index.js";
import {
  hookSocketPath,
  readHookInput,
  serializeHookRequest,
  storedHookToken,
  validateHookOutput
} from "../../claude-plugin/hooks/relay.mjs";

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

function unixRequest(socketPath, path, options = {}) {
  return new Promise((resolve, reject) => {
    const req = request({ socketPath, path, method: options.method || "GET", headers: options.headers || {} }, res => {
      const chunks = [];
      res.on("data", chunk => chunks.push(Buffer.from(chunk)));
      res.on("end", () => resolve({ status: res.statusCode, body: Buffer.concat(chunks).toString("utf8") }));
    });
    req.on("error", reject);
    if (options.body) req.write(options.body);
    req.end();
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

test("persists generated credentials in owner-only config directory", () => {
  const config = tempConfig();
  loadConfig({ CARDPUTER_BRIDGE_CONFIG: config });
  assert.equal(lstatSync(config).mode & 0o777, 0o600);
  assert.equal(lstatSync(join(config, "..")).mode & 0o777, 0o700);
  assert.equal(bridgeHookSocketPath(config), hookSocketPath(config));
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

test("hook uses owner-protected Unix socket, requires credential, and bounds body", async () => {
  const config = {
    hookPort: 17877,
    devicePort: await unusedLoopbackPort(),
    deviceHost: "127.0.0.1",
    token: strong,
    hookToken: strong.split("").reverse().join(""),
    configPath: tempConfig()
  };
  const bridge = new CardputerBridge(config);
  await bridge.start();
  try {
    const socketPath = hookSocketPath(config.configPath);
    assert(lstatSync(socketPath).isSocket());
    assert.equal(lstatSync(socketPath).mode & 0o777, 0o600);
    bridge.notify("Claude", "secret summary must not be in health");
    const health = await unixRequest(socketPath, "/health");
    assert.equal(health.status, 200);
    const healthBody = health.body;
    assert(!healthBody.includes("secret summary"));
    assert(!healthBody.includes("lastSummary"));
    assert.equal((await unixRequest(socketPath, "/hook", { method: "POST", body: "{}" })).status, 401);
    assert.equal((await unixRequest(socketPath, "/hook", {
      method: "POST",
      headers: { authorization: `Bearer ${config.hookToken}`, "content-type": "application/json" },
      body: JSON.stringify({ eventName: "Notification", event: { message: "expected" } })
    })).status, 200);
    assert.equal((await unixRequest(socketPath, "/hook", {
      method: "POST",
      headers: { authorization: `Bearer ${config.hookToken}`, "content-type": "application/json" },
      body: "x".repeat(32 * 1024 + 1)
    })).status, 413);
  } finally {
    await bridge.stop();
  }
  assert.equal(existsSync(hookSocketPath(config.configPath)), false);
});

test("hook refuses to replace a non-socket object at controlled path", async () => {
  const configPath = tempConfig();
  const socketPath = hookSocketPath(configPath);
  writeFileSync(socketPath, "keep", { mode: 0o600 });
  const bridge = new CardputerBridge({
    hookPort: 17877,
    devicePort: 17878,
    deviceHost: "127.0.0.1",
    token: strong,
    hookToken: strong.split("").reverse().join(""),
    configPath
  });
  await assert.rejects(() => bridge.start(), /not a socket/);
  assert.equal(readFileSync(socketPath, "utf8"), "keep");
});

test("hook listener configures bounded admission before Unix socket listen", () => {
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

test("MCP registration never exposes pairing bearer material", () => {
  const bridge = new CardputerBridge({ hookPort: 17877, devicePort: 17878, deviceHost: "127.0.0.1", token: strong, hookToken: strong, configPath: tempConfig() });
  const server = new McpServer({ name: "security-test", version: "1" });
  registerTools(server, bridge);
  const names = Object.keys(server._registeredTools);
  assert(names.includes("notify_cardputer"));
  assert(names.includes("device_status"));
  assert(!names.includes("generate_pairing_config"));
});

test("upgrade target parser ignores Host and rejects non-origin-form targets", () => {
  assert.equal(parseDeviceUpgradeTarget("/device")?.pathname, "/device");
  assert.equal(parseDeviceUpgradeTarget("//attacker.example/device"), null);
  assert.equal(parseDeviceUpgradeTarget("https://attacker.example/device"), null);
  assert.equal(parseDeviceUpgradeTarget("not-a-path"), null);
});

test("device listener and WebSocket admission are finite before authentication", () => {
  const bridge = new CardputerBridge({ hookPort: 17877, devicePort: 17878, deviceHost: "127.0.0.1", token: strong, hookToken: strong, configPath: tempConfig() });
  const server = configureDeviceServerAdmission({
    requestTimeout: 0,
    headersTimeout: 0,
    keepAliveTimeout: 0,
    maxConnections: 0,
    maxRequestsPerSocket: 0,
    setTimeout(milliseconds) { this.timeout = milliseconds; }
  });
  assert.equal(server.requestTimeout, 15_000);
  assert.equal(server.headersTimeout, 10_000);
  assert.equal(server.keepAliveTimeout, 5_000);
  assert.equal(server.maxConnections, 32);
  assert.equal(server.maxRequestsPerSocket, 16);
  assert.equal(server.timeout, 15_000);
  assert.equal(bridge.wsServer.options.maxPayload, 4096);
});

test("device frames reject binary or oversized input and retain only whitelisted bounded state", () => {
  class FakeSocket extends EventEmitter {
    OPEN = 1;
    readyState = 1;
    sent = [];
    closed = [];
    send(payload) { this.sent.push(payload); }
    close(code, reason) { this.closed.push([code, reason]); }
  }
  const bridge = new CardputerBridge({ hookPort: 17877, devicePort: 17878, deviceHost: "127.0.0.1", token: strong, hookToken: strong, configPath: tempConfig() });
  const socket = new FakeSocket();
  bridge.attachDevice(socket);
  socket.emit("message", Buffer.from(JSON.stringify({ v: 1, type: "hello", device: "D".repeat(200), fw: "1.4.0", token: "must-not-survive" })), false);
  socket.emit("message", Buffer.from(JSON.stringify({ v: 1, type: "state", battery: 200, ble: true, page: "home", heap: 1234, secret: "must-not-survive" })), false);
  const status = bridge.status();
  assert.deepEqual(status.deviceInfo, { device: "D".repeat(48), fw: "1.4.0" });
  assert.deepEqual(status.deviceState, { battery: 100, ble: true, page: "home", heap: 1234 });
  assert(!JSON.stringify(status).includes("must-not-survive"));
  socket.emit("message", Buffer.from("{}"), true);
  assert.equal(socket.closed.at(-1)[0], 1003);
  socket.emit("message", Buffer.alloc(4097), false);
  assert.equal(socket.closed.at(-1)[0], 1009);
});

test("relay bounds stdin, outbound body, and accepted stdout JSON", async () => {
  assert.equal(await readHookInput(Readable.from(["{" + "x".repeat(32 * 1024) + "}"])), null);
  assert.deepEqual(await readHookInput(Readable.from(['{"ok":true}'])), { ok: true });
  assert.equal(serializeHookRequest("Stop", { text: "x".repeat(32 * 1024) }), null);
  const accepted = validateHookOutput(JSON.stringify({ hookSpecificOutput: { hookEventName: "Elicitation", action: "accept", content: { answer: "yes" } } }));
  assert.equal(JSON.parse(accepted).hookSpecificOutput.action, "accept");
  assert.equal(validateHookOutput('{"arbitrary":"output"}'), null);
  assert.equal(validateHookOutput("x".repeat(32 * 1024 + 1)), null);
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
