#!/usr/bin/env node
import { runManagementCli } from "../cli.js";

runManagementCli().catch((error: unknown) => {
  process.stderr.write(`orca-cardputer: ${error instanceof Error ? error.message : "command failed"}\n`);
  process.exitCode = 1;
});
