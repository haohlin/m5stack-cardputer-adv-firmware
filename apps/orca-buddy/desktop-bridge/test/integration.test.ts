import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { mkdtemp, stat, readFile } from "node:fs/promises";
import net from "node:net";
import { join } from "node:path";
import test from "node:test";
import WebSocket from "ws";
import { initializeBridge } from "../src/management.js";
import { bridgePaths } from "../src/paths.js";
import { jsonLineQueue, temporaryDirectory, waitForEvent, writeExecutable } from "./helpers.js";

async function freePort(): Promise<number> {
  const server = net.createServer();
  await new Promise<void>((resolve, reject) => server.listen(0, "127.0.0.1", resolve).once("error", reject));
  const address = server.address();
  assert.ok(address && typeof address === "object");
  await new Promise<void>((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
  return address.port;
}

async function waitForPath(path: string, diagnostics: () => string): Promise<void> {
  for (let attempt = 0; attempt < 100; attempt += 1) {
    try {
      await stat(path);
      return;
    } catch {
      await new Promise((resolve) => setTimeout(resolve, 20));
    }
  }
  throw new Error(`timed out waiting for ${path}: ${diagnostics()}`);
}

function webSocketMessages(webSocket: WebSocket): { next: (label: string) => Promise<Record<string, unknown>> } {
  const values: Record<string, unknown>[] = [];
  const waiters: Array<(value: Record<string, unknown>) => void> = [];
  webSocket.on("message", (data) => {
    const value = JSON.parse(data.toString()) as Record<string, unknown>;
    const waiter = waiters.shift();
    if (waiter === undefined) values.push(value);
    else waiter(value);
  });
  return {
    next: (label) => new Promise((resolve, reject) => {
      const value = values.shift();
      if (value !== undefined) return resolve(value);
      const timeout = setTimeout(() => reject(new Error(`timed out waiting for WSS message: ${label}`)), 3_000);
      waiters.push((nextValue) => { clearTimeout(timeout); resolve(nextValue); });
    })
  };
}

test("built daemon, WSS device, Unix IPC, and stdio MCP complete bounded round trips", async (context) => {
  const packageDirectory = join(import.meta.dirname, "..");
  const home = await mkdtemp("/tmp/orca-int-");
  const tools = await temporaryDirectory("orca-integration-tools-");
  const port = await freePort();
  await initializeBridge({ home, port });
  const orca = join(tools, "orca");
  await writeExecutable(orca, `#!/usr/bin/env node
const args = process.argv.slice(2);
if (JSON.stringify(args) === JSON.stringify(["status", "--json"])) process.stdout.write(JSON.stringify({ ok: true, result: { runtime: { reachable: true } } }));
else if (JSON.stringify(args) === JSON.stringify(["worktree", "ps", "--json"])) process.stdout.write(JSON.stringify({ ok: true, result: { totalCount: 1, worktrees: [{ displayName: "Buddy", workspaceStatus: "in-progress", agents: [{ state: "working", prompt: "private" }] }] } }));
else process.exitCode = 91;
`);
  const env = { ...process.env, HOME: home, PATH: `${tools}:${process.env.PATH ?? ""}` };
  const daemon = spawn(process.execPath, [join(packageDirectory, "dist", "bin", "orca-cardputer-bridge.js")], { env, stdio: ["ignore", "pipe", "pipe"] });
  let daemonError = "";
  daemon.stderr?.on("data", (chunk: Buffer) => { daemonError += chunk.toString("utf8"); });
  context.after(() => { if (daemon.exitCode === null) daemon.kill("SIGKILL"); });
  await waitForPath(bridgePaths(home).socket, () => daemonError);

  const pairing = JSON.parse(await readFile(bridgePaths(home).pairingFile, "utf8")) as { endpoint: string; token: string; ca: string };
  const device = new WebSocket(pairing.endpoint, { ca: pairing.ca, headers: { Authorization: `Bearer ${pairing.token}` } });
  let deviceClose: unknown[] | undefined;
  device.on("close", (...args) => { deviceClose = args; });
  context.after(() => device.terminate());
  const deviceMessages = webSocketMessages(device);
  await waitForEvent(device, "open");
  const snapshot = await deviceMessages.next("initial snapshot");
  assert.equal(snapshot.type, "snapshot");
  assert.doesNotMatch(JSON.stringify(snapshot), /prompt|private/);

  const mcp = spawn(process.execPath, [join(packageDirectory, "dist", "bin", "orca-cardputer-mcp.js")], { env, stdio: ["pipe", "pipe", "pipe"] });
  context.after(() => { if (mcp.exitCode === null) mcp.kill("SIGKILL"); });
  assert.ok(mcp.stdin && mcp.stdout);
  const replies = jsonLineQueue(mcp.stdout);
  const call = (id: number, name: string, args: unknown = {}) => {
    mcp.stdin?.write(`${JSON.stringify({ jsonrpc: "2.0", id, method: "tools/call", params: { name, arguments: args } })}\n`);
    return replies.next();
  };

  const status = await call(1, "orca_buddy_status");
  assert.match(JSON.stringify(status), /Buddy/);
  assert.match(JSON.stringify(status), /deviceConnected\":true/, `readyState=${device.readyState} close=${JSON.stringify(deviceClose)}`);
  assert.doesNotMatch(JSON.stringify(status), new RegExp(pairing.token));

  const notifyReply = await call(2, "orca_buddy_notify", { text: "Notice" });
  assert.match(JSON.stringify(notifyReply), /delivered\":true/);
  assert.equal((await deviceMessages.next("notification")).type, "notify");

  const askReply = call(3, "orca_buddy_ask", { question: "Proceed?", labels: ["Yes", "No"] });
  const question = await deviceMessages.next("question request");
  assert.equal(question.type, "question.request");
  device.send(JSON.stringify({ version: "orca-cardputer/v1", type: "question.answer", questionId: question.questionId, answer: "No" }));
  assert.match(JSON.stringify(await askReply), /No/);

  device.send(JSON.stringify({ version: "orca-cardputer/v1", type: "prompt.draft", text: "Run tests" }));
  await new Promise((resolve) => setTimeout(resolve, 20));
  assert.match(JSON.stringify(await call(4, "orca_buddy_get_prompt")), /Run tests/);

  device.close();
  await waitForEvent(device, "close");
  mcp.stdin.end();
  daemon.kill("SIGTERM");
  const daemonExit = await new Promise<number | null>((resolve) => daemon.once("exit", (code) => resolve(code)));
  assert.equal(daemonExit, 0);
});
