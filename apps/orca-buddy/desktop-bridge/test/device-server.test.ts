import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { readFile } from "node:fs/promises";
import https from "node:https";
import { join } from "node:path";
import { promisify } from "node:util";
import test from "node:test";
import WebSocket from "ws";
import { startDeviceServer } from "../src/device-server.js";
import { temporaryDirectory, waitForEvent } from "./helpers.js";

const execFileAsync = promisify(execFile);

async function makeCertificate(directory: string): Promise<{ cert: Buffer; key: Buffer }> {
  const certPath = join(directory, "server-cert.pem");
  const keyPath = join(directory, "server-key.pem");
  await execFileAsync("openssl", [
    "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-sha256", "-days", "1",
    "-subj", "/CN=127.0.0.1", "-addext", "subjectAltName=IP:127.0.0.1",
    "-keyout", keyPath, "-out", certPath
  ]);
  return { cert: await readFile(certPath), key: await readFile(keyPath) };
}

function rejectedUpgrade(url: string, token?: string): Promise<number> {
  return new Promise((resolve, reject) => {
    const headers = token ? { Authorization: `Bearer ${token}` } : undefined;
    const client = new WebSocket(url, { rejectUnauthorized: false, headers });
    client.once("unexpected-response", (_request, response) => {
      resolve(response.statusCode ?? 0);
      response.resume();
    });
    client.once("open", () => reject(new Error("upgrade unexpectedly accepted")));
    client.once("error", () => {});
  });
}

test("WSS rejects non-upgrade, wrong path, bad bearer, malformed JSON, oversized frames, and excess peers", async (context) => {
  const directory = await temporaryDirectory("orca-wss-");
  const tls = await makeCertificate(directory);
  let dispatches = 0;
  const server = await startDeviceServer({
    host: "127.0.0.1",
    port: 0,
    cert: tls.cert,
    key: tls.key,
    bearerToken: "device-pairing-token",
    maxConnections: 1,
    maxFrameBytes: 64,
    onMessage: () => { dispatches += 1; }
  });
  context.after(() => server.close());
  const base = `wss://127.0.0.1:${server.port}`;

  const nonUpgradeStatus = await new Promise<number>((resolve, reject) => {
    const request = https.get(`${base.replace("wss://", "https://")}/device`, { rejectUnauthorized: false }, (response) => {
      resolve(response.statusCode ?? 0);
      response.resume();
    });
    request.once("error", reject);
  });
  assert.equal(nonUpgradeStatus, 426);
  assert.equal(await rejectedUpgrade(`${base}/wrong`, "device-pairing-token"), 404);
  assert.equal(await rejectedUpgrade(`${base}/device`), 401);
  assert.equal(await rejectedUpgrade(`${base}/device`, "bad-token"), 401);
  assert.equal(dispatches, 0);

  const accepted = new WebSocket(`${base}/device`, {
    rejectUnauthorized: false,
    headers: { Authorization: "Bearer device-pairing-token" }
  });
  await waitForEvent(accepted, "open");
  assert.equal(await rejectedUpgrade(`${base}/device`, "device-pairing-token"), 503);
  accepted.send(JSON.stringify({ version: "orca-cardputer/v1", type: "hello" }));
  await new Promise((resolve) => setTimeout(resolve, 20));
  assert.equal(dispatches, 1);
  accepted.send("not-json");
  const malformedClose = await waitForEvent(accepted, "close");
  assert.equal(malformedClose[0], 1007);

  const oversized = new WebSocket(`${base}/device`, {
    rejectUnauthorized: false,
    headers: { Authorization: "Bearer device-pairing-token" }
  });
  await waitForEvent(oversized, "open");
  oversized.send("x".repeat(65));
  const oversizedClose = await waitForEvent(oversized, "close");
  assert.equal(oversizedClose[0], 1009);
  assert.equal(dispatches, 1);
});

test("device listener defaults to loopback and rejects wildcard addresses", async (context) => {
  const directory = await temporaryDirectory("orca-wss-bind-");
  const tls = await makeCertificate(directory);
  const server = await startDeviceServer({ cert: tls.cert, key: tls.key, bearerToken: "token" });
  context.after(() => server.close());
  assert.equal(server.host, "127.0.0.1");
  await assert.rejects(
    () => startDeviceServer({ host: "0.0.0.0", cert: tls.cert, key: tls.key, bearerToken: "token" }),
    /wildcard/i
  );
});
