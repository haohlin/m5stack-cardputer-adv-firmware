import assert from "node:assert/strict";
import { readFile, writeFile } from "node:fs/promises";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";
import { collectOrcaSnapshot, sanitizeSnapshot } from "../src/collector.js";
import { temporaryDirectory, writeExecutable } from "./helpers.js";

const fixtureDirectory = join(dirname(fileURLToPath(import.meta.url)), "fixtures");

test("collector runs only exact approved Orca JSON commands and strips private fields", async () => {
  const directory = await temporaryDirectory("orca-collector-");
  const executable = join(directory, "orca");
  const argumentLog = join(directory, "arguments.jsonl");
  await writeFile(argumentLog, "");
  await writeExecutable(executable, `#!/usr/bin/env node
const fs = require("node:fs");
const args = process.argv.slice(2);
fs.appendFileSync(process.env.ARGUMENT_LOG, JSON.stringify(args) + "\\n");
if (JSON.stringify(args) === JSON.stringify(["status", "--json"])) {
  process.stdout.write(fs.readFileSync(process.env.STATUS_FIXTURE));
} else if (JSON.stringify(args) === JSON.stringify(["worktree", "ps", "--json"])) {
  process.stdout.write(fs.readFileSync(process.env.WORKTREE_FIXTURE));
} else {
  process.exitCode = 91;
}
`);

  const snapshot = await collectOrcaSnapshot({
    orcaPath: executable,
    env: {
      ...process.env,
      ARGUMENT_LOG: argumentLog,
      STATUS_FIXTURE: join(fixtureDirectory, "orca-status.json"),
      WORKTREE_FIXTURE: join(fixtureDirectory, "worktree-ps.json")
    }
  });

  assert.deepEqual(snapshot, {
    connected: true,
    worktreeCount: 2,
    items: [
      { displayName: "Firmware", workspaceStatus: "active", agentState: "working" },
      { displayName: "Docs", workspaceStatus: "idle", agentState: "waiting" }
    ]
  });
  assert.deepEqual(
    (await readFile(argumentLog, "utf8")).trim().split("\n").map((line) => JSON.parse(line)),
    [["status", "--json"], ["worktree", "ps", "--json"]]
  );
  assert.doesNotMatch(JSON.stringify(snapshot), /terminal|prompt|toolInput|session|path|token|private/i);
});

test("collector rejects malformed command JSON instead of forwarding it", async () => {
  const directory = await temporaryDirectory("orca-collector-bad-");
  const executable = join(directory, "orca");
  await writeExecutable(executable, `#!/usr/bin/env node
process.stdout.write("not-json");
`);
  await assert.rejects(() => collectOrcaSnapshot({ orcaPath: executable }), /valid JSON/i);
});

test("collector unwraps public Orca result envelopes without retaining agent details", () => {
  const snapshot = sanitizeSnapshot(
    {
      ok: true,
      result: { runtime: { reachable: true, state: "ready", runtimeId: "private" } },
      _meta: { runtimeId: "private" }
    },
    {
      ok: true,
      result: {
        totalCount: 1,
        worktrees: [{
          displayName: "Buddy",
          workspaceStatus: "in-progress",
          path: "/private/worktree",
          preview: "private terminal preview",
          agents: [{ state: "working", prompt: "private prompt", toolInput: "private input" }]
        }]
      }
    }
  );
  assert.deepEqual(snapshot, {
    connected: true,
    worktreeCount: 1,
    items: [{ displayName: "Buddy", workspaceStatus: "in-progress", agentState: "working" }]
  });
  assert.doesNotMatch(JSON.stringify(snapshot), /runtimeId|path|preview|prompt|toolInput|private/i);
});
