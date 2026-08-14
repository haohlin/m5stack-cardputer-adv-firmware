#!/usr/bin/env node
import { homedir } from "node:os";
import { requestDaemon } from "../ipc.js";
import { runMcpServer } from "../mcp.js";
import { bridgePaths } from "../paths.js";
import { readProtectedFile } from "../runtime.js";

const paths = bridgePaths(homedir());
const localAuthToken = (await readProtectedFile(paths.localAuthToken)).toString("utf8").trim();
await runMcpServer({
  request: (method, params) => requestDaemon({
    socketPath: paths.socket,
    authToken: localAuthToken,
    method,
    params
  })
});
