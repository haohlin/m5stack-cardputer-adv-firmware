import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { mkdir, writeFile } from "node:fs/promises";
import net from "node:net";
import { join } from "node:path";
import test from "node:test";
import { initializeBridge } from "../src/management.js";
import { bridgePaths } from "../src/paths.js";
import { temporaryDirectory, writeExecutable } from "./helpers.js";

async function freePort(): Promise<number> {
  const server = net.createServer();
  await new Promise<void>((resolve, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", resolve);
  });
  const address = server.address();
  assert.ok(address && typeof address === "object");
  const port = address.port;
  await new Promise<void>((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
  return port;
}

test("built daemon closes WSS and exits when IPC startup fails", async () => {
  const packageDirectory = join(import.meta.dirname, "..");
  const home = await temporaryDirectory("orca-daemon-rollback-");
  const tools = await temporaryDirectory("orca-daemon-tools-");
  const port = await freePort();
  await initializeBridge({ home, port });
  const orca = join(tools, "orca");
  await writeExecutable(orca, `#!/usr/bin/env node
const args = process.argv.slice(2);
process.stdout.write(JSON.stringify(args[0] === "status" ? { connected: true } : { worktrees: [] }));
`);
  const paths = bridgePaths(home);
  await mkdir(paths.run, { recursive: true });
  await writeFile(paths.socket, "not a socket");
  const child = spawn(process.execPath, [join(packageDirectory, "dist", "bin", "orca-cardputer-bridge.js")], {
    env: { ...process.env, HOME: home, PATH: `${tools}:${process.env.PATH ?? ""}` },
    stdio: ["ignore", "pipe", "pipe"]
  });
  const exit = await Promise.race([
    new Promise<{ code: number | null; signal: NodeJS.Signals | null }>((resolve) => child.once("exit", (code, signal) => resolve({ code, signal }))),
    new Promise<null>((resolve) => setTimeout(() => resolve(null), 2_000))
  ]);
  if (exit === null) child.kill("SIGKILL");
  assert.notEqual(exit, null, "daemon leaked WSS listener after IPC startup failure");
  assert.equal(exit?.code, 1);

  await new Promise<void>((resolve, reject) => {
    const socket = net.createConnection({ host: "127.0.0.1", port });
    socket.once("connect", () => reject(new Error("WSS listener still accepts connections")));
    socket.once("error", () => resolve());
  });
});
