#!/usr/bin/env node
import { runDaemon } from "../daemon.js";

runDaemon().catch((error: unknown) => {
  process.stderr.write(`orca-cardputer-bridge: ${error instanceof Error ? error.message : "startup failed"}\n`);
  process.exitCode = 1;
});
