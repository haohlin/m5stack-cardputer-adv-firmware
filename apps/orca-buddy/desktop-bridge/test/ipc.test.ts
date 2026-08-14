import assert from "node:assert/strict";
import { stat } from "node:fs/promises";
import { join } from "node:path";
import test from "node:test";
import { requestDaemon, startIpcServer } from "../src/ipc.js";
import { temporaryDirectory } from "./helpers.js";

test("Unix socket is mode 0600 and requires exact authentication", async (context) => {
  const directory = await temporaryDirectory("orca-ipc-");
  const socketPath = join(directory, "bridge.sock");
  const server = await startIpcServer({
    socketPath,
    authToken: "local-auth-token-with-enough-entropy",
    handle: async (method) => ({ method, connected: true })
  });
  context.after(() => server.close());

  assert.equal((await stat(socketPath)).mode & 0o777, 0o600);
  await assert.rejects(
    () => requestDaemon({ socketPath, authToken: "wrong-token", method: "status" }),
    /unauthorized/i
  );
  assert.deepEqual(
    await requestDaemon({ socketPath, authToken: "local-auth-token-with-enough-entropy", method: "status" }),
    { method: "status", connected: true }
  );
});

test("Unix protocol rejects oversized and malformed requests before dispatch", async (context) => {
  const directory = await temporaryDirectory("orca-ipc-bounds-");
  const socketPath = join(directory, "bridge.sock");
  let dispatches = 0;
  const server = await startIpcServer({
    socketPath,
    authToken: "local-auth-token-with-enough-entropy",
    maxRequestBytes: 128,
    handle: async () => {
      dispatches += 1;
      return {};
    }
  });
  context.after(() => server.close());

  await assert.rejects(
    () => requestDaemon({
      socketPath,
      authToken: "local-auth-token-with-enough-entropy",
      method: "status",
      params: { text: "x".repeat(256) }
    }),
    /too large/i
  );
  assert.equal(dispatches, 0);
});
